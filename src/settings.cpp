#include <Arduino.h>
#include <Preferences.h>
#include "../config.h"
#include "settings.h"

#if __has_include("../secrets.h")
#include "../secrets.h"
#define HAS_SECRETS_H 1
#else
#define HAS_SECRETS_H 0
#endif

static const char* const NVS_NAMESPACE = "abfahrt";
static SemaphoreHandle_t settingsMutex = nullptr;
static bool storedValuesFound = false;
DeviceSettings appSettings;

SettingsLock::SettingsLock() { if (settingsMutex) xSemaphoreTakeRecursive(settingsMutex, portMAX_DELAY); }
SettingsLock::~SettingsLock() { if (settingsMutex) xSemaphoreGiveRecursive(settingsMutex); }

static String key(const char* prefix, int i) { return String(prefix) + String(i); }
static void readString(Preferences& p, const char* k, String& v) { if (p.isKey(k)) v = p.getString(k, v); }
static void readString(Preferences& p, const String& k, String& v) { readString(p, k.c_str(), v); }
static void readBool(Preferences& p, const char* k, bool& v) { if (p.isKey(k)) v = p.getBool(k, v); }
static void readBool(Preferences& p, const String& k, bool& v) { readBool(p, k.c_str(), v); }
static bool writeString(Preferences& p, const String& k, const String& v) { return p.putString(k.c_str(), v) == v.length(); }
static bool writeBool(Preferences& p, const String& k, bool v) { return p.putBool(k.c_str(), v) == 1; }

static void setTransportDefaults(DeviceSettings& s) {
  s.showSbahn = SHOW_SBAHN; s.showUbahn = SHOW_UBAHN; s.showTram = SHOW_TRAM;
  s.showBus = SHOW_BUS; s.showBahn = SHOW_BAHN;
}

static void setDefaults(DeviceSettings& s) {
#if HAS_SECRETS_H
  s.wifiSsid = ssid; s.wifiPassword = password;
  s.qrSsid = qrWlanSsid; s.qrPassword = qrWlanPassword;
#else
  s.wifiSsid = ""; s.wifiPassword = ""; s.qrSsid = ""; s.qrPassword = "";
#endif
  for (int i=0;i<MAX_STATIONS;i++) {
    s.stations[i].id = (i==0 ? STATION_GLOBAL_ID : "");
    s.stations[i].name = "";
    s.stations[i].dirAJson = "[]";
    s.stations[i].dirBJson = "[]";
    s.stations[i].labelA = "Richtung A";
    s.stations[i].labelB = "Richtung B";
    s.stations[i].defaultA = DEFAULT_VIEW_A;
  }
  s.directionView = FEATURE_DIRECTION_VIEW;
  setTransportDefaults(s);
  s.wifiQr = FEATURE_WIFI_QR;
  s.qrTitle = QR_SCREEN_TITLE;
}

static void validate(DeviceSettings& s) {
  for (int i=0;i<MAX_STATIONS;i++) {
    s.stations[i].id.trim(); s.stations[i].name.trim();
    if (!s.stations[i].dirAJson.length()) s.stations[i].dirAJson = "[]";
    if (!s.stations[i].dirBJson.length()) s.stations[i].dirBJson = "[]";
    if (!s.stations[i].labelA.length()) s.stations[i].labelA = "Richtung A";
    if (!s.stations[i].labelB.length()) s.stations[i].labelB = "Richtung B";
  }
  if (!(s.showSbahn || s.showUbahn || s.showTram || s.showBus || s.showBahn)) setTransportDefaults(s);
}

void settingsLoad() {
  if (!settingsMutex) settingsMutex = xSemaphoreCreateRecursiveMutex();
  SettingsLock lock;
  setDefaults(appSettings);
  storedValuesFound = false;
  Preferences p;
  if (!p.begin(NVS_NAMESPACE, true)) return;
  storedValuesFound = true;
  readString(p, "wifiSsid", appSettings.wifiSsid);
  readString(p, "wifiPass", appSettings.wifiPassword);
  readBool(p, "dirView", appSettings.directionView);
  readBool(p, "sbahn", appSettings.showSbahn); readBool(p, "ubahn", appSettings.showUbahn);
  readBool(p, "tram", appSettings.showTram); readBool(p, "bus", appSettings.showBus); readBool(p, "bahn", appSettings.showBahn);
  readBool(p, "qrOn", appSettings.wifiQr); readString(p, "qrTitle", appSettings.qrTitle);
  readString(p, "qrSsid", appSettings.qrSsid); readString(p, "qrPass", appSettings.qrPassword);

  bool hasNewStationKeys = false;
  for (int i=0;i<MAX_STATIONS;i++) {
    String k = key("st",i); if (p.isKey(k.c_str())) hasNewStationKeys = true;
    readString(p, k, appSettings.stations[i].id);
    readString(p, key("sn",i), appSettings.stations[i].name);
    readString(p, key("da",i), appSettings.stations[i].dirAJson);
    readString(p, key("db",i), appSettings.stations[i].dirBJson);
    readString(p, key("la",i), appSettings.stations[i].labelA);
    readString(p, key("lb",i), appSettings.stations[i].labelB);
    readBool(p, key("dfa",i), appSettings.stations[i].defaultA);
  }
  // Migration von der Original-MVG-Firmware: WLAN/QR/Filter bleiben erhalten,
  // die alte MVG-Stations-ID aber NICHT. MVG-globalId und VBB-stopId sind
  // verschiedene ID-Systeme; nach dem Update soll deshalb das Portal direkt
  // eine Berliner/VBB-Haltestelle abfragen statt mit der Muenchner ID zu scheitern.
  if (!hasNewStationKeys && p.isKey("station")) {
    appSettings.stations[0].id = "";
    appSettings.stations[0].name = "";
    appSettings.directionView = false; // H/R-Zuordnung ist bei VBB nicht uebertragbar.
  }
  p.end();
  validate(appSettings);
}

bool settingsSave() {
  SettingsLock lock; validate(appSettings);
  Preferences p; if (!p.begin(NVS_NAMESPACE, false)) return false;
  bool ok = true;
  ok &= writeString(p,"wifiSsid",appSettings.wifiSsid); ok &= writeString(p,"wifiPass",appSettings.wifiPassword);
  ok &= writeBool(p,"dirView",appSettings.directionView);
  ok &= writeBool(p,"sbahn",appSettings.showSbahn); ok &= writeBool(p,"ubahn",appSettings.showUbahn);
  ok &= writeBool(p,"tram",appSettings.showTram); ok &= writeBool(p,"bus",appSettings.showBus); ok &= writeBool(p,"bahn",appSettings.showBahn);
  ok &= writeBool(p,"qrOn",appSettings.wifiQr); ok &= writeString(p,"qrTitle",appSettings.qrTitle);
  ok &= writeString(p,"qrSsid",appSettings.qrSsid); ok &= writeString(p,"qrPass",appSettings.qrPassword);
  for (int i=0;i<MAX_STATIONS;i++) {
    ok &= writeString(p,key("st",i),appSettings.stations[i].id); ok &= writeString(p,key("sn",i),appSettings.stations[i].name);
    ok &= writeString(p,key("da",i),appSettings.stations[i].dirAJson); ok &= writeString(p,key("db",i),appSettings.stations[i].dirBJson);
    ok &= writeString(p,key("la",i),appSettings.stations[i].labelA); ok &= writeString(p,key("lb",i),appSettings.stations[i].labelB);
    ok &= writeBool(p,key("dfa",i),appSettings.stations[i].defaultA);
  }
  p.end(); if (ok) storedValuesFound = true;
  Serial.println(ok ? "Einstellungen gespeichert" : "Fehler beim Speichern der Einstellungen");
  return ok;
}

bool settingsSaveWifi(const String& ssid, const String& password) {
  Preferences p; if (!p.begin(NVS_NAMESPACE,false)) return false;
  bool ok = writeString(p,"wifiSsid",ssid) && writeString(p,"wifiPass",password); p.end();
  if (ok) { SettingsLock lock; appSettings.wifiSsid=ssid; appSettings.wifiPassword=password; storedValuesFound=true; }
  return ok;
}

bool settingsFactoryReset() { Preferences p; if(!p.begin(NVS_NAMESPACE,false)) return false; bool ok=p.clear(); p.end(); return ok; }
bool settingsHasWifi() { return appSettings.wifiSsid.length()>0; }
bool settingsHasStation() { return settingsFirstStationIndex() >= 0; }
int settingsFirstStationIndex() { for(int i=0;i<MAX_STATIONS;i++) if(appSettings.stations[i].id.length()) return i; return -1; }
int settingsNextStationIndex(int current) {
  for(int step=1;step<=MAX_STATIONS;step++){int i=(current+step)%MAX_STATIONS;if(appSettings.stations[i].id.length())return i;} return current;
}
void settingsPrint() {
  Serial.println("--- VBB Einstellungen ---");
#if HAS_SECRETS_H
  // Exakter Marker bleibt absichtlich erhalten: das originale Export-Skript
  // verweigert damit die Veroeffentlichung einer Firmware mit WLAN-Geheimnissen.
  Serial.println("Hinweis: secrets.h eingebunden (Vorbelegung WLAN/WLAN-QR)");
#endif
  Serial.printf("WLAN: %s\n", appSettings.wifiSsid.length()?appSettings.wifiSsid.c_str():"(keins)");
  for(int i=0;i<MAX_STATIONS;i++) if(appSettings.stations[i].id.length())
    Serial.printf("Bahnhof %d: %s [%s]\n", i+1, appSettings.stations[i].name.c_str(), appSettings.stations[i].id.c_str());
  Serial.printf("Anzeige: %s\n", appSettings.directionView?"Richtung A/B":"gemischt");
  Serial.println("-------------------------");
}
