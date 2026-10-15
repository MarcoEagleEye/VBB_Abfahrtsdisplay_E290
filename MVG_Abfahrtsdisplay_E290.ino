// Berlin/VBB Edition based on MVG_Abfahrtsdisplay_E290
// Compatible OTA marker is retained inside src/portal.cpp so updates from the original project can be accepted.
#define FW_VERSION "3.0.3a-vbb"

#include "WiFi.h"
#include <HTTPClient.h>
#include <time.h>
#include "config.h"
#include "src/settings.h"
#include "src/improv_serial.h"
#include "src/portal.h"
#include "src/mvg_api.h"
#include "src/display.h"
#include "src/buttons.h"
#include "src/stats.h"
#include "src/time_utils.h"
#include "src/text_utils.h"
#include "src/wifi_diag.h"
#include "src/extras.h"

struct StationCache {
  String name;
  Departure all[MAX_DEPARTURES_SHOWN*2]; int allCount=0;
  Departure a[MAX_DEPARTURES_SHOWN]; int aCount=0;
  Departure b[MAX_DEPARTURES_SHOWN]; int bCount=0;
  bool valid=false;
  unsigned long fetchedAt=0;
};
static StationCache caches[MAX_STATIONS];
static int activeStation=0;
static bool activeA=true;
static bool showPage2=false;

static unsigned long lastErrorRetry=0,autoResetStart=0,qrModeStart=0,logModeStart=0,splashStart=0,wifiLostSince=0;
static unsigned long nextBackgroundFetch=0;
static bool autoResetPending=false,qrModeActive=false,logModeActive=false,setupScreenShown=false,portalRequested=false,updateScreenShown=false;
static bool fallbackApActive=false,onlineInitialized=false,extrasSetupDone=false;
static String fallbackApSsid;
static int updateCounter=0,lastUpdateMinute=-1,wifiDisconnectCount=0,backgroundCursor=0;
static const char* wifiErrorShownReason=nullptr;
enum SystemState{STATE_NORMAL,STATE_WIFI_ERROR,STATE_API_ERROR};static SystemState currentState=STATE_NORMAL;

String firmwareVersionText(){return String(FW_VERSION)+extrasVersionSuffix();}
bool splashShowing(){return millis()-splashStart<SPLASH_DURATION_MS;}

static void selectHome(){int f=settingsFirstStationIndex();activeStation=f>=0?f:0;showPage2=false;activeA=(f>=0)?appSettings.stations[f].defaultA:true;}
static String stationDisplayName(int i){if(i<0||i>=MAX_STATIONS)return "Bahnhof";if(caches[i].name.length())return caches[i].name;return "Bahnhof "+String(i+1);}
static String directionLabel(int i,bool a){String s=a?appSettings.stations[i].labelA:appSettings.stations[i].labelB;if(!s.length())s=a?"Richtung A":"Richtung B";return utf8ToLatin1(s);}
static bool atHome(){int f=settingsFirstStationIndex();if(activeStation!=f)return false;if(appSettings.directionView)return activeA==appSettings.stations[f].defaultA;return !showPage2;}
static bool cacheFresh(int i,unsigned long maxAge){return i>=0&&i<MAX_STATIONS&&caches[i].valid&&(millis()-caches[i].fetchedAt<maxAge);}

static String fallbackAddress(){return WiFi.softAPIP().toString();}
static void showFallbackApScreen(){if(!fallbackApActive)return;String a=fallbackAddress();displayShowSetupAp(fallbackApSsid.c_str(),WIFI_FALLBACK_AP_PASSWORD,a.c_str());setupScreenShown=true;}
static void startFallbackAp(){
  if(fallbackApActive)return;
  uint32_t suffix=(uint32_t)(ESP.getEfuseMac()&0xFFFF);char b[5];snprintf(b,sizeof(b),"%04X",suffix);
  fallbackApSsid=String(WIFI_FALLBACK_AP_PREFIX)+"-"+b;
  WiFi.mode(WIFI_AP_STA);
  if(WiFi.softAP(fallbackApSsid.c_str(),WIFI_FALLBACK_AP_PASSWORD)){
    fallbackApActive=true;portalOpen();
    Serial.printf("Setup-AP aktiv: %s / Passwort %s / http://%s/\n",fallbackApSsid.c_str(),WIFI_FALLBACK_AP_PASSWORD,fallbackAddress().c_str());
    if(!splashShowing())showFallbackApScreen();
  }else Serial.println("Setup-AP konnte nicht gestartet werden");
}
static void stopFallbackAp(){
  if(!fallbackApActive)return;
  WiFi.softAPdisconnect(true);
  // softAPdisconnect() beendet nur den AP; der Funkmodus kann sonst auf
  // WIFI_AP_STA stehen bleiben. Nach erfolgreicher STA-Verbindung deshalb
  // explizit in den normalen Client-Modus zurückschalten.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  fallbackApActive=false;
  delay(100);
  Serial.printf("Setup-AP beendet; WLAN-Modus STA, IP %s, RSSI %d dBm\n",WiFi.localIP().toString().c_str(),WiFi.RSSI());
}

static void refreshStationNames(){
  for(int i=0;i<MAX_STATIONS;i++){
    caches[i].name="";if(!appSettings.stations[i].id.length())continue;
    if(appSettings.stations[i].name.length())caches[i].name=utf8ToLatin1(appSettings.stations[i].name);
  }
}

static bool fetchStationOnce(int i){
  if(i<0||i>=MAX_STATIONS||!appSettings.stations[i].id.length())return false;
  String payload;if(!downloadDepartures(appSettings.stations[i].id.c_str(),payload)){Serial.printf("Bahnhof %d: Abruf fehlgeschlagen\n",i+1);return false;}
  Departure allTmp[MAX_DEPARTURES_SHOWN*2];Departure aTmp[MAX_DEPARTURES_SHOWN];Departure bTmp[MAX_DEPARTURES_SHOWN];
  int ac=parseDepartures(payload,DIR_FILTER_ALL,allTmp,MAX_DEPARTURES_SHOWN*2);
  int ca=parseDeparturesForTargets(payload,appSettings.stations[i].dirAJson,aTmp,MAX_DEPARTURES_SHOWN);
  int cb=parseDeparturesForTargets(payload,appSettings.stations[i].dirBJson,bTmp,MAX_DEPARTURES_SHOWN);
  if(ac<0||ca<0||cb<0){Serial.printf("Bahnhof %d: JSON-Auswertung fehlgeschlagen\n",i+1);return false;}
  StationCache&c=caches[i];
  for(int n=0;n<ac;n++)c.all[n]=allTmp[n];for(int n=0;n<ca;n++)c.a[n]=aTmp[n];for(int n=0;n<cb;n++)c.b[n]=bTmp[n];
  c.allCount=ac;c.aCount=ca;c.bCount=cb;c.valid=true;c.fetchedAt=millis();
  Serial.printf("Bahnhof %d aktualisiert: %d Abfahrten\n",i+1,ac);
  return true;
}

static bool fetchActiveOnce(){return fetchStationOnce(activeStation);}
static bool fetchActive(){
  bool ok=fetchActiveOnce();
  if(!ok&&currentState!=STATE_API_ERROR&&WiFi.status()==WL_CONNECTED){delay(API_RETRY_DELAY_MS);ok=fetchActiveOnce();}
  if(!ok){if(currentState!=STATE_API_ERROR){recordApiFail();currentState=STATE_API_ERROR;lastErrorRetry=millis();}return false;}
  currentState=STATE_NORMAL;return true;
}

static void redrawFromCache(bool fullRefresh){if(activeStation<0||activeStation>=MAX_STATIONS||!appSettings.stations[activeStation].id.length())return;StationCache&c=caches[activeStation];String name=stationDisplayName(activeStation);if(appSettings.directionView){String header=name+" > "+directionLabel(activeStation,activeA);Departure*arr=activeA?c.a:c.b;int n=activeA?c.aCount:c.bCount;displayShowDepartures(arr,n,header,false,false,false,fullRefresh);}else{int off=showPage2?MAX_DEPARTURES_SHOWN:0;int n=c.allCount-off;if(n<0)n=0;if(n>MAX_DEPARTURES_SHOWN)n=MAX_DEPARTURES_SHOWN;displayShowDepartures(c.all+off,n,name,false,false,showPage2,fullRefresh);}}
static void redrawCurrentView(){if(currentState!=STATE_API_ERROR&&caches[activeStation].valid)redrawFromCache(true);}
static void attemptUpdate(bool full){bool was=currentState==STATE_API_ERROR;if(!fetchActive()){if(!was)displayShowApiError();return;}redrawFromCache(full);}
static void showCurrentViewOrFetch(){if(activeStation<0||activeStation>=MAX_STATIONS)return;if(!cacheFresh(activeStation,ACTIVE_CACHE_MAX_AGE_MS))attemptUpdate(true);else{currentState=STATE_NORMAL;redrawFromCache(true);}}

static void backgroundFetchStep(){
  if(WiFi.status()!=WL_CONNECTED||!settingsHasStation()||(long)(millis()-nextBackgroundFetch)<0)return;
  for(int step=1;step<=MAX_STATIONS;step++){
    int i=(backgroundCursor+step)%MAX_STATIONS;
    if(i==activeStation||!appSettings.stations[i].id.length())continue;
    if(!caches[i].valid||millis()-caches[i].fetchedAt>=BACKGROUND_REFRESH_AGE_MS){
      Serial.printf("Hintergrund-Abruf Bahnhof %d\n",i+1);fetchStationOnce(i);backgroundCursor=i;nextBackgroundFetch=millis()+BACKGROUND_PRELOAD_STEP_MS;return;
    }
  }
  nextBackgroundFetch=millis()+BACKGROUND_PRELOAD_STEP_MS;
}

static String portalAddress(){String u=portalUrl();u.replace("http://","");u.replace("/","");return u;}
static void showPortalSetupScreen(){String a=portalAddress();displayShowPortalSetup(portalUrl().c_str(),a.c_str());setupScreenShown=true;}
static void resetViewToDefault(){selectHome();Serial.println("Auto-Reset: Standardansicht");}
static void armReset(){autoResetPending=!atHome();if(autoResetPending)autoResetStart=millis();}

void triggerBootShort(){if(!settingsHasStation())return;if(appSettings.directionView){if(activeA){activeA=false;}else{activeStation=settingsNextStationIndex(activeStation);activeA=true;}}else{if(!showPage2)showPage2=true;else{activeStation=settingsNextStationIndex(activeStation);showPage2=false;}}Serial.printf("BOOT kurz: Bahnhof %d, %s\n",activeStation+1,appSettings.directionView?(activeA?"Richtung A":"Richtung B"):(showPage2?"Seite 2":"Seite 1"));armReset();showCurrentViewOrFetch();updateCounter++;}
void triggerBootLong(){if(!settingsHasStation())return;activeStation=settingsNextStationIndex(activeStation);showPage2=false;activeA=appSettings.stations[activeStation].defaultA;Serial.printf("BOOT lang: direkt Bahnhof %d\n",activeStation+1);armReset();showCurrentViewOrFetch();updateCounter++;}

static void returnToDepartures(){time_t n=time(nullptr);struct tm t;localtime_r(&n,&t);if(currentState==STATE_API_ERROR||t.tm_min!=lastUpdateMinute){lastUpdateMinute=t.tm_min;attemptUpdate(true);}else redrawFromCache(true);updateCounter=0;}
void triggerQrAction(){if(!appSettings.wifiQr)return;if(qrModeActive){qrModeActive=false;returnToDepartures();}else{qrModeActive=true;qrModeStart=millis();displayShowWifiQr(appSettings.qrTitle.c_str(),appSettings.qrSsid.c_str(),appSettings.qrPassword.c_str());}}
void triggerLogAction(){if(logModeActive){logModeActive=false;returnToDepartures();}else{logModeActive=true;logModeStart=millis();portalOpen();String a=portalAddress();displayShowLog(firmwareVersionText().c_str(),a.c_str(),portalClosingTime().c_str(),wifiDisconnectCount,getApiFailCount());}}

static void showWifiErrorIfChanged(){static const char*NW="Keine WLAN-Daten";static const char*NH="Setup-WLAN wird gestartet";bool hw=settingsHasWifi();const char*r=hw?wifiDiagReasonText():NW;const char*h=hw?wifiDiagHintText():NH;if(r==wifiErrorShownReason)return;wifiErrorShownReason=r;displayShowWifiError(hw?appSettings.wifiSsid.c_str():"-",r,h,hw);}

static void initializeOnline(){
  if(onlineInitialized||WiFi.status()!=WL_CONNECTED)return;
  configTzTime(TIMEZONE_INFO,"de.pool.ntp.org","time.google.com");
  struct tm ti={};unsigned long start=millis();while(!getLocalTime(&ti,150)&&millis()-start<NTP_BOOT_WAIT_MS)delay(50);
  if(ti.tm_year>100)lastUpdateMinute=ti.tm_min;
  refreshStationNames();
  if(!extrasSetupDone){extrasSetup(firmwareVersionText().c_str());extrasSetupDone=true;}
  onlineInitialized=true;nextBackgroundFetch=millis()+BACKGROUND_PRELOAD_DELAY_MS;backgroundCursor=activeStation;
}

static void invalidateCaches(){for(int i=0;i<MAX_STATIONS;i++){caches[i].valid=false;caches[i].allCount=caches[i].aCount=caches[i].bCount=0;caches[i].fetchedAt=0;}}
static void applyNewSettings(){logModeActive=false;qrModeActive=false;autoResetPending=false;setupScreenShown=false;invalidateCaches();selectHome();refreshStationNames();if(!settingsHasStation())return;time_t n=time(nullptr);struct tm t;localtime_r(&n,&t);lastUpdateMinute=t.tm_min;currentState=STATE_NORMAL;if(WiFi.status()==WL_CONNECTED)attemptUpdate(true);updateCounter=0;nextBackgroundFetch=millis()+BACKGROUND_PRELOAD_DELAY_MS;}

static void finishSplash(){
  while(splashShowing()){if(improvLoop())portalRequested=true;extrasLoop();extrasStatus(currentState!=STATE_NORMAL);delay(20);}
  if(fallbackApActive){showFallbackApScreen();return;}
  if(!settingsHasStation()){portalOpen();showPortalSetupScreen();return;}
  if(currentState==STATE_API_ERROR)displayShowApiError();else if(caches[activeStation].valid)redrawFromCache(true);
}

static bool connectSavedWifiAtBoot(){
  if(!settingsHasWifi())return false;
  WiFi.begin(appSettings.wifiSsid.c_str(),appSettings.wifiPassword.c_str());
  unsigned long start=millis();
  while(WiFi.status()!=WL_CONNECTED&&millis()-start<WIFI_BOOT_CONNECT_TIMEOUT_MS){if(improvLoop())portalRequested=true;extrasStatus(true);delay(100);}
  if(WiFi.status()==WL_CONNECTED){Serial.printf("WLAN verbunden nach %lu ms, IP %s\n",millis()-start,WiFi.localIP().toString().c_str());return true;}
  Serial.printf("WLAN nach %lu ms nicht erreichbar; starte Recovery-AP\n",millis()-start);return false;
}

void setup(){
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  delay(300);Serial.printf("VBB Abfahrtsdisplay %s\n",firmwareVersionText().c_str());
  settingsLoad();settingsPrint();selectHome();
  improvBegin(firmwareVersionText().c_str());portalBegin(firmwareVersionText().c_str());extrasBegin();buttonsInit();displayInit();displayShowSplash(firmwareVersionText().c_str());splashStart=millis();
  WiFi.persistent(false);WiFi.setAutoReconnect(true);WiFi.mode(WIFI_STA);wifiDiagInit();
  bool connected=connectSavedWifiAtBoot();
  if(improvLoop())portalRequested=true;
  if(connected){if(portalRequested)portalOpen();initializeOnline();if(settingsHasStation())fetchActive();}
  else{currentState=STATE_WIFI_ERROR;wifiLostSince=millis();startFallbackAp();}
  finishSplash();
}

void loop(){
  if(portalUpdateRunning()){if(!updateScreenShown){displayShowUpdate();updateScreenShown=true;}portalLoop();delay(50);return;}
  if(updateScreenShown){updateScreenShown=false;applyNewSettings();}
  if(improvLoop())portalOpen();extrasLoop();checkApiFailWindow();
  if(portalLoop())applyNewSettings();

  if(WiFi.status()!=WL_CONNECTED){
    if(currentState!=STATE_WIFI_ERROR){currentState=STATE_WIFI_ERROR;wifiDisconnectCount++;wifiLostSince=millis();wifiErrorShownReason=nullptr;lastErrorRetry=millis();onlineInitialized=false;}
    if(!fallbackApActive&&millis()-wifiLostSince>=WIFI_FALLBACK_AP_DELAY_MS)startFallbackAp();
    if(fallbackApActive){if(!portalIsOpen())portalOpen();if(!setupScreenShown&&!splashShowing())showFallbackApScreen();}
    else if(millis()-wifiLostSince>=WIFI_ERROR_SCREEN_DELAY_MS)showWifiErrorIfChanged();
    if(!extrasHandlesWifiReconnect()&&!improvBusy()&&settingsHasWifi()&&millis()-lastErrorRetry>=WIFI_RETRY_INTERVAL_MS){Serial.println("WLAN: automatischer Neuversuch");WiFi.reconnect();lastErrorRetry=millis();}
    extrasStatus(true);delay(50);return;
  }

  if(fallbackApActive){stopFallbackAp();setupScreenShown=false;}
  initializeOnline();
  if(currentState==STATE_WIFI_ERROR){currentState=STATE_NORMAL;setupScreenShown=false;if(settingsHasStation())attemptUpdate(true);updateCounter=0;nextBackgroundFetch=millis()+BACKGROUND_PRELOAD_DELAY_MS;}
  if(!settingsHasStation()){portalOpen();if(!setupScreenShown)showPortalSetupScreen();delay(50);return;}
  extrasStatus(currentState!=STATE_NORMAL);

  handleQrButton(triggerQrAction,triggerLogAction);if(logModeActive){if(millis()-logModeStart>=LOG_DISPLAY_DURATION_MS){logModeActive=false;returnToDepartures();}else return;}if(qrModeActive){if(millis()-qrModeStart>=QR_DISPLAY_DURATION_MS){qrModeActive=false;returnToDepartures();}else return;}
  handleBootButton(triggerBootShort,triggerBootLong);if(autoResetPending&&millis()-autoResetStart>=VIEW_AUTO_RESET_MS){resetViewToDefault();autoResetPending=false;showCurrentViewOrFetch();updateCounter++;}
  if(currentState==STATE_API_ERROR){if(millis()-lastErrorRetry>=ERROR_RETRY_INTERVAL_MS){attemptUpdate(true);lastErrorRetry=millis();}return;}

  time_t nr=time(nullptr);struct tm ni;localtime_r(&nr,&ni);
  if(ni.tm_sec>=UPDATE_TARGET_SECOND&&ni.tm_min!=lastUpdateMinute){lastUpdateMinute=ni.tm_min;updateCounter++;bool full=updateCounter>=FULL_REFRESH_EVERY;if(full)updateCounter=0;attemptUpdate(full);nextBackgroundFetch=millis()+BACKGROUND_PRELOAD_STEP_MS;}
  backgroundFetchStep();
}
