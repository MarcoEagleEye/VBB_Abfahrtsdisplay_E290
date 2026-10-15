#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "../config.h"
#include "settings.h"
#include "mvg_api.h"
#include "text_utils.h"

// Primary VBB endpoint plus BVG-compatible fallback. Both expose the same
// hafas-client v6 schema and use the same VBB stop IDs (900...).
static const char* API_BASES[] = {
  "https://v6.vbb.transport.rest",
  "https://v6.bvg.transport.rest"
};
static const size_t API_BASE_COUNT = sizeof(API_BASES) / sizeof(API_BASES[0]);
static const int DEPARTURE_RESULTS = 32;

static String urlEncode(const String& text) {
  static const char H[] = "0123456789ABCDEF";
  String out;
  out.reserve(text.length() * 3);
  for (unsigned int i = 0; i < text.length(); i++) {
    uint8_t c = (uint8_t)text[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else { out += '%'; out += H[c >> 4]; out += H[c & 15]; }
  }
  return out;
}

static String bp(bool v) { return v ? "true" : "false"; }

static String isoTime(const char* iso) {
  if (!iso) return "--:--";
  String s(iso);
  int t = s.indexOf('T');
  return (t >= 0 && s.length() >= (unsigned)(t + 6)) ? s.substring(t + 1, t + 6) : "--:--";
}

static time_t isoEpochLocal(const char* iso) {
  if (!iso || !*iso) return (time_t)0;
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
  int n = sscanf(iso, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se);
  if (n < 5 || y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59) return (time_t)0;
  struct tm tmv = {};
  tmv.tm_year = y - 1900; tmv.tm_mon = mo - 1; tmv.tm_mday = d;
  tmv.tm_hour = h; tmv.tm_min = mi; tmv.tm_sec = (n >= 6 ? se : 0); tmv.tm_isdst = -1;
  return mktime(&tmv);
}

static bool looksLikeJson(const String& s) {
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isspace((unsigned char)c)) continue;
    return c == '{' || c == '[';
  }
  return false;
}

// Robust HTTP read for ESP32-S3:
// - HTTP/1.0 avoids problematic chunked responses on some proxies/CDNs.
// - Accept-Encoding: identity avoids compressed bodies the ESP32 HTTPClient
//   does not transparently decode.
// - Empty HTTP-200 bodies are treated as an error and trigger fallback.
static bool httpGetJson(const String& url, String& payloadOut, const char* label) {
  HTTPClient h;
  h.setConnectTimeout(HTTP_TIMEOUT_MS);
  h.setTimeout(HTTP_TIMEOUT_MS);
  h.setReuse(false);
  h.useHTTP10(true);

  if (!h.begin(url)) {
    Serial.printf("%s: HTTP begin fehlgeschlagen\n", label);
    return false;
  }

  h.addHeader("Accept", "application/json");
  h.addHeader("Accept-Encoding", "identity");
  h.addHeader("User-Agent", "VBB-E290/3.0.3");

  int code = h.GET();
  int announced = h.getSize();
  payloadOut = "";
  if (code == HTTP_CODE_OK) payloadOut = h.getString();
  h.end();

  Serial.printf("%s: HTTP %d, Content-Length %d, empfangen %u Bytes, Heap %u\n",
                label, code, announced, (unsigned)payloadOut.length(), (unsigned)ESP.getFreeHeap());

  if (code != HTTP_CODE_OK) return false;
  if (payloadOut.length() == 0) {
    Serial.printf("%s: HTTP 200 aber leerer Body\n", label);
    return false;
  }
  if (!looksLikeJson(payloadOut)) {
    Serial.printf("%s: Antwort ist kein JSON\n", label);
    return false;
  }
  return true;
}

static JsonArray depArray(JsonDocument& doc) {
  if (doc.is<JsonArray>()) return doc.as<JsonArray>();
  if (doc["departures"].is<JsonArray>()) return doc["departures"].as<JsonArray>();
  return JsonArray();
}

static DeserializationError parseDepartureJson(JsonDocument& doc, const String& payload) {
  JsonDocument filter;
  unsigned int p = 0;
  while (p < payload.length() && isspace((unsigned char)payload[p])) p++;
  JsonObject f;
  if (p < payload.length() && payload[p] == '[') {
    JsonArray a = filter.to<JsonArray>();
    f = a.add<JsonObject>();
  } else {
    JsonArray a = filter["departures"].to<JsonArray>();
    f = a.add<JsonObject>();
  }
  f["direction"] = true;
  f["line"]["name"] = true;
  f["line"]["product"] = true;
  f["plannedWhen"] = true;
  f["when"] = true;
  f["delay"] = true;
  f["cancelled"] = true;
  f["remarks"][0]["type"] = true;
  return deserializeJson(doc, payload, DeserializationOption::Filter(filter));
}

static String productsText(JsonObject p) {
  String s;
  auto add = [&](const char* x) { if (s.length()) s += ", "; s += x; };
  if (p["suburban"] | false) add("S-Bahn");
  if (p["subway"] | false) add("U-Bahn");
  if (p["tram"] | false) add("Tram");
  if (p["bus"] | false) add("Bus");
  if (p["ferry"] | false) add("Faehre");
  if (p["regional"] | false) add("Regionalzug");
  if (p["express"] | false) add("Fernzug");
  return s;
}

bool searchStations(const String& query, String& jsonOut) {
  for (size_t b = 0; b < API_BASE_COUNT; b++) {
    String url = String(API_BASES[b]) + "/locations?query=" + urlEncode(query) +
      "&results=15&stops=true&addresses=false&poi=false&linesOfStops=false&language=de&pretty=false";
    String payload;
    if (!httpGetJson(url, payload, b == 0 ? "VBB Suche" : "BVG Suche (Fallback)")) continue;

    JsonDocument filter;
    JsonArray fa = filter.to<JsonArray>();
    JsonObject ff = fa.add<JsonObject>();
    ff["type"] = true; ff["id"] = true; ff["name"] = true; ff["products"] = true;
    ff["location"]["latitude"] = true; ff["location"]["longitude"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
    if (err) { Serial.printf("Stations-JSON (%s): %s\n", API_BASES[b], err.c_str()); continue; }

    JsonDocument out;
    JsonArray list = out.to<JsonArray>();
    for (JsonObject loc : doc.as<JsonArray>()) {
      const char* type = loc["type"] | "";
      const char* id = loc["id"] | "";
      if (strcmp(type, "stop") || !*id) continue;
      JsonObject e = list.add<JsonObject>();
      e["n"] = loc["name"] | "";
      e["p"] = b == 0 ? "VBB" : "BVG";
      e["id"] = id;
      e["t"] = loc["products"].is<JsonObject>() ? productsText(loc["products"].as<JsonObject>()) : "";
      e["z"] = "";
      if (!loc["location"]["latitude"].isNull()) {
        e["lat"] = loc["location"]["latitude"];
        e["lon"] = loc["location"]["longitude"];
      }
      if (list.size() >= 15) break;
    }
    jsonOut = "";
    serializeJson(out, jsonOut);
    return true;
  }
  return false;
}

bool fetchStationName(const char* stopId, String& nameOut) {
  for (size_t b = 0; b < API_BASE_COUNT; b++) {
    String url = String(API_BASES[b]) + "/stops/" + urlEncode(stopId) + "?language=de&pretty=false";
    String payload;
    if (!httpGetJson(url, payload, b == 0 ? "VBB Bahnhof" : "BVG Bahnhof (Fallback)")) continue;
    JsonDocument f; f["name"] = true;
    JsonDocument d;
    if (deserializeJson(d, payload, DeserializationOption::Filter(f))) continue;
    const char* n = d["name"];
    if (!n) continue;
    nameOut = utf8ToLatin1(String(n));
    return true;
  }
  return false;
}

bool downloadDepartures(const char* stopId, String& payloadOut) {
  // Keep the response deliberately small. We only show up to 8 entries per
  // station; 32 raw results are ample even with several lines/directions.
  // remarks=false avoids very large disruption texts. Warning support can be
  // re-enabled later with a separate lightweight request if desired.
  for (size_t b = 0; b < API_BASE_COUNT; b++) {
    String url = String(API_BASES[b]) + "/stops/" + urlEncode(stopId) +
      "/departures?duration=90&results=" + String(DEPARTURE_RESULTS) +
      "&remarks=false&linesOfStops=false&language=de&pretty=false";
    url += "&suburban=" + bp(appSettings.showSbahn);
    url += "&subway=" + bp(appSettings.showUbahn);
    url += "&tram=" + bp(appSettings.showTram);
    url += "&bus=" + bp(appSettings.showBus);
    url += "&ferry=false";
    url += "&regional=" + bp(appSettings.showBahn);
    url += "&express=" + bp(appSettings.showBahn);

    String payload;
    const char* label = b == 0 ? "VBB Abfahrten" : "BVG Abfahrten (Fallback)";
    if (!httpGetJson(url, payload, label)) continue;

    // The actual filtered JSON parse happens in parseDepartureJson().
    // Here we only reject empty/non-JSON bodies so a second backend can be tried.
    payloadOut = payload;
    return true;
  }

  payloadOut = "";
  return false;
}

bool listDirections(const char* stopId, String& jsonOut) {
  String payload;
  if (!downloadDepartures(stopId, payload)) return false;
  JsonDocument doc;
  DeserializationError err = parseDepartureJson(doc, payload);
  if (err) { Serial.printf("VBB Richtungen JSON: %s\n", err.c_str()); return false; }

  JsonDocument out;
  JsonArray list = out.to<JsonArray>();
  for (JsonObject dep : depArray(doc)) {
    const char* z = dep["direction"] | "";
    const char* l = dep["line"]["name"] | "";
    if (!*z) continue;
    bool known = false;
    for (JsonObject e : list) {
      if (strcmp(e["z"] | "", z) == 0) {
        String lines = e["l"] | "";
        if (*l && lines.indexOf(l) < 0) { if (lines.length()) lines += ", "; lines += l; e["l"] = lines; }
        known = true;
        break;
      }
    }
    if (!known) { JsonObject e = list.add<JsonObject>(); e["z"] = z; e["l"] = l; }
  }
  jsonOut = "";
  serializeJson(out, jsonOut);
  return true;
}

static bool targetAllowed(const String& dest, JsonArray targets, bool useTargets) {
  if (!useTargets) return true;
  for (JsonVariant v : targets) if (dest == String(v.as<const char*>())) return true;
  return false;
}

static int parseInternal(const String& payload, const String* targetsJson, Departure result[], int maxResults) {
  JsonDocument doc;
  DeserializationError err = parseDepartureJson(doc, payload);
  if (err) { Serial.printf("VBB JSON: %s\n", err.c_str()); return -1; }
  JsonArray deps = depArray(doc);
  if (deps.isNull()) return -1;

  JsonDocument td;
  JsonArray targets;
  bool useTargets = targetsJson != nullptr;
  if (useTargets) {
    if (deserializeJson(td, *targetsJson)) return -1;
    targets = td.as<JsonArray>();
    if (targets.isNull() || targets.size() == 0) return 0;
  }

  int count = 0;
  for (JsonObject dep : deps) {
    if (count >= maxResults) break;
    const char* ln = dep["line"]["name"] | "";
    const char* dr = dep["direction"] | "";
    if (!*ln || !*dr) continue;
    String destUtf8(dr);
    if (!targetAllowed(destUtf8, targets, useTargets)) continue;

    Departure d;
    d.line = String(ln); d.line.replace(" ", "");
    d.destination = utf8ToLatin1(destUtf8);
    d.cancelled = dep["cancelled"] | false;
    const char* planned = dep["plannedWhen"];
    const char* actual = dep["when"];
    const char* baseTime = planned ? planned : actual;
    d.time = isoTime(baseTime);
    d.planned = isoEpochLocal(baseTime);
    d.multiDest = destUtf8.indexOf('/') >= 0;
    d.delayMin = 0;
    d.realtime = false;
    if (!dep["delay"].isNull()) {
      long sec = dep["delay"].as<long>();
      d.delayMin = sec >= 0 ? (int)((sec + 30) / 60) : (int)((sec - 30) / 60);
      if (d.delayMin < 0 && -d.delayMin < EARLY_DEPARTURE_MIN) d.delayMin = 0;
      d.realtime = true;
    } else if (planned && actual && strcmp(planned, actual)) d.realtime = true;
    const char* product = dep["line"]["product"] | "";
    d.isBus = !strcmp(product, "bus");
    d.hasWarning = false; // remarks=false in the robust request path
    result[count++] = d;
  }
  return count;
}

int parseDepartures(const String& payload, DirectionFilter filter, Departure result[], int maxResults) {
  (void)filter;
  return parseInternal(payload, nullptr, result, maxResults);
}

int parseDeparturesForTargets(const String& payload, const String& targetsJson, Departure result[], int maxResults) {
  return parseInternal(payload, &targetsJson, result, maxResults);
}
