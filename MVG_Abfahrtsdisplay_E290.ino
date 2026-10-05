// Berlin/VBB Edition based on MVG_Abfahrtsdisplay_E290
// Compatible OTA marker is retained inside src/portal.cpp so updates from the original project can be accepted.
#define FW_VERSION "3.0.0-vbb"

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
};
static StationCache caches[MAX_STATIONS];
static int activeStation=0;
static bool activeA=true;
static bool showPage2=false;

static unsigned long lastErrorRetry=0,autoResetStart=0,qrModeStart=0,logModeStart=0,splashStart=0,wifiLostSince=0;
static bool autoResetPending=false,qrModeActive=false,logModeActive=false,setupScreenShown=false,portalRequested=false,updateScreenShown=false;
static int updateCounter=0,lastUpdateMinute=-1,wifiDisconnectCount=0;
static const char* wifiErrorShownReason=nullptr;
enum SystemState{STATE_NORMAL,STATE_WIFI_ERROR,STATE_API_ERROR};static SystemState currentState=STATE_NORMAL;

String firmwareVersionText(){return String(FW_VERSION)+extrasVersionSuffix();}
bool splashShowing(){return millis()-splashStart<SPLASH_DURATION_MS;}

static void selectHome(){int f=settingsFirstStationIndex();activeStation=f>=0?f:0;showPage2=false;activeA=(f>=0)?appSettings.stations[f].defaultA:true;}
static String stationDisplayName(int i){if(i<0||i>=MAX_STATIONS)return "Bahnhof";if(caches[i].name.length())return caches[i].name;return "Bahnhof "+String(i+1);}
static String directionLabel(int i,bool a){String s=a?appSettings.stations[i].labelA:appSettings.stations[i].labelB;if(!s.length())s=a?"Richtung A":"Richtung B";return utf8ToLatin1(s);}
static bool atHome(){int f=settingsFirstStationIndex();if(activeStation!=f)return false;if(appSettings.directionView)return activeA==appSettings.stations[f].defaultA;return !showPage2;}

static void refreshStationNames(){for(int i=0;i<MAX_STATIONS;i++){caches[i].name="";if(!appSettings.stations[i].id.length())continue;if(appSettings.stations[i].name.length())caches[i].name=utf8ToLatin1(appSettings.stations[i].name);else{String n;if(fetchStationName(appSettings.stations[i].id.c_str(),n))caches[i].name=n;}}}

static bool fetchAllOnce(){bool activeOk=(activeStation>=0&&activeStation<MAX_STATIONS&&caches[activeStation].valid);bool any=activeOk;for(int i=0;i<MAX_STATIONS;i++){if(!appSettings.stations[i].id.length())continue;String payload;if(!downloadDepartures(appSettings.stations[i].id.c_str(),payload)){Serial.printf("Bahnhof %d: Abruf fehlgeschlagen\n",i+1);continue;}int ac=parseDepartures(payload,DIR_FILTER_ALL,caches[i].all,MAX_DEPARTURES_SHOWN*2);int ca=parseDeparturesForTargets(payload,appSettings.stations[i].dirAJson,caches[i].a,MAX_DEPARTURES_SHOWN);int cb=parseDeparturesForTargets(payload,appSettings.stations[i].dirBJson,caches[i].b,MAX_DEPARTURES_SHOWN);if(ac<0||ca<0||cb<0)continue;caches[i].allCount=ac;caches[i].aCount=ca;caches[i].bCount=cb;caches[i].valid=true;any=true;if(i==activeStation)activeOk=true;}return activeOk||(!appSettings.stations[activeStation].id.length()&&any);}
static bool fetchAll(){bool ok=fetchAllOnce();if(!ok&&currentState!=STATE_API_ERROR&&WiFi.status()==WL_CONNECTED){delay(API_RETRY_DELAY_MS);ok=fetchAllOnce();}if(!ok){if(currentState!=STATE_API_ERROR){recordApiFail();currentState=STATE_API_ERROR;lastErrorRetry=millis();}return false;}currentState=STATE_NORMAL;return true;}

static void redrawFromCache(bool fullRefresh){if(activeStation<0||activeStation>=MAX_STATIONS||!appSettings.stations[activeStation].id.length())return;StationCache&c=caches[activeStation];String name=stationDisplayName(activeStation);if(appSettings.directionView){String header=name+" > "+directionLabel(activeStation,activeA);Departure*arr=activeA?c.a:c.b;int n=activeA?c.aCount:c.bCount;displayShowDepartures(arr,n,header,false,false,false,fullRefresh);}else{int off=showPage2?MAX_DEPARTURES_SHOWN:0;int n=c.allCount-off;if(n<0)n=0;if(n>MAX_DEPARTURES_SHOWN)n=MAX_DEPARTURES_SHOWN;displayShowDepartures(c.all+off,n,name,false,false,showPage2,fullRefresh);}}
static void redrawCurrentView(){if(currentState!=STATE_API_ERROR)redrawFromCache(true);}
static void attemptUpdate(bool full){bool was=currentState==STATE_API_ERROR;if(!fetchAll()){if(!was)displayShowApiError();return;}redrawFromCache(full);}
static void showCurrentViewOrFetch(){if(activeStation<0||activeStation>=MAX_STATIONS)return;if(!caches[activeStation].valid)attemptUpdate(true);else{currentState=STATE_NORMAL;redrawFromCache(true);}}

static void showPortalSetupScreen(){String a=WiFi.localIP().toString();displayShowPortalSetup(portalUrl().c_str(),a.c_str());setupScreenShown=true;}
static void resetViewToDefault(){selectHome();Serial.println("Auto-Reset: Standardansicht");}
static void armReset(){autoResetPending=!atHome();if(autoResetPending)autoResetStart=millis();}

void triggerBootShort(){if(!settingsHasStation())return;if(appSettings.directionView){if(activeA){activeA=false;}else{activeStation=settingsNextStationIndex(activeStation);activeA=true;}}else{if(!showPage2)showPage2=true;else{activeStation=settingsNextStationIndex(activeStation);showPage2=false;}}Serial.printf("BOOT kurz: Bahnhof %d\n",activeStation+1);armReset();showCurrentViewOrFetch();updateCounter++;}
void triggerBootLong(){if(!settingsHasStation())return;activeStation=settingsNextStationIndex(activeStation);showPage2=false;activeA=appSettings.stations[activeStation].defaultA;Serial.printf("BOOT lang: direkt Bahnhof %d\n",activeStation+1);armReset();showCurrentViewOrFetch();updateCounter++;}

static void returnToDepartures(){time_t n=time(nullptr);struct tm t;localtime_r(&n,&t);if(currentState==STATE_API_ERROR||t.tm_min!=lastUpdateMinute){lastUpdateMinute=t.tm_min;attemptUpdate(true);}else redrawFromCache(true);updateCounter=0;}
void triggerQrAction(){if(!appSettings.wifiQr)return;if(qrModeActive){qrModeActive=false;returnToDepartures();}else{qrModeActive=true;qrModeStart=millis();displayShowWifiQr(appSettings.qrTitle.c_str(),appSettings.qrSsid.c_str(),appSettings.qrPassword.c_str());}}
void triggerLogAction(){if(logModeActive){logModeActive=false;returnToDepartures();}else{logModeActive=true;logModeStart=millis();portalOpen();String a=WiFi.localIP().toString();displayShowLog(firmwareVersionText().c_str(),a.c_str(),portalClosingTime().c_str(),wifiDisconnectCount,getApiFailCount());}}

static void showWifiErrorIfChanged(){static const char*NW="Keine WLAN-Daten";static const char*NH="Einrichten per Web-Installer";bool hw=settingsHasWifi();const char*r=hw?wifiDiagReasonText():NW;const char*h=hw?wifiDiagHintText():NH;if(r==wifiErrorShownReason)return;wifiErrorShownReason=r;displayShowWifiError(hw?appSettings.wifiSsid.c_str():"-",r,h,hw);}

static void applyNewSettings(){logModeActive=false;qrModeActive=false;autoResetPending=false;setupScreenShown=false;for(int i=0;i<MAX_STATIONS;i++){caches[i].valid=false;caches[i].allCount=caches[i].aCount=caches[i].bCount=0;}selectHome();refreshStationNames();if(!settingsHasStation())return;time_t n=time(nullptr);struct tm t;localtime_r(&n,&t);lastUpdateMinute=t.tm_min;currentState=STATE_NORMAL;attemptUpdate(true);updateCounter=0;}

static void finishSplash(){while(splashShowing()){improvLoop();extrasLoop();extrasStatus(currentState!=STATE_NORMAL);delay(20);}if(!settingsHasStation()){portalOpen();showPortalSetupScreen();return;}if(currentState==STATE_API_ERROR)displayShowApiError();else redrawFromCache(true);}

void setup(){
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  delay(500);Serial.printf("VBB Abfahrtsdisplay %s\n",firmwareVersionText().c_str());
  settingsLoad();settingsPrint();selectHome();
  improvBegin(firmwareVersionText().c_str());portalBegin(firmwareVersionText().c_str());extrasBegin();buttonsInit();displayInit();displayShowSplash(firmwareVersionText().c_str());splashStart=millis();
  WiFi.persistent(false);WiFi.mode(WIFI_STA);wifiDiagInit();if(settingsHasWifi())WiFi.begin(appSettings.wifiSsid.c_str(),appSettings.wifiPassword.c_str());
  unsigned long ws=millis(),wr=millis();while(WiFi.status()!=WL_CONNECTED){if(improvLoop())portalRequested=true;extrasStatus(true);delay(250);bool due=!settingsHasWifi()||millis()-ws>=WIFI_ERROR_SCREEN_DELAY_MS;if(due&&!splashShowing())showWifiErrorIfChanged();if(settingsHasWifi()&&!improvBusy()&&millis()-wr>=ERROR_RETRY_INTERVAL_MS){WiFi.reconnect();wr=millis();}}
  if(improvLoop())portalRequested=true;if(portalRequested)portalOpen();configTzTime(TIMEZONE_INFO,"de.pool.ntp.org","time.google.com");struct tm ti={};for(int r=0;r<20&&!getLocalTime(&ti);r++)delay(500);lastUpdateMinute=ti.tm_min;
  refreshStationNames();extrasSetup(firmwareVersionText().c_str());if(settingsHasStation())fetchAll();finishSplash();
}

void loop(){
  if(portalUpdateRunning()){if(!updateScreenShown){displayShowUpdate();updateScreenShown=true;}portalLoop();delay(50);return;}if(updateScreenShown){updateScreenShown=false;applyNewSettings();}
  if(improvLoop())portalOpen();extrasLoop();checkApiFailWindow();
  if(WiFi.status()!=WL_CONNECTED){if(currentState!=STATE_WIFI_ERROR){currentState=STATE_WIFI_ERROR;wifiDisconnectCount++;wifiLostSince=millis();wifiErrorShownReason=nullptr;lastErrorRetry=millis();}if(millis()-wifiLostSince>=WIFI_ERROR_SCREEN_DELAY_MS)showWifiErrorIfChanged();if(!extrasHandlesWifiReconnect()&&!improvBusy()&&settingsHasWifi()&&millis()-lastErrorRetry>=ERROR_RETRY_INTERVAL_MS){WiFi.reconnect();lastErrorRetry=millis();}extrasStatus(true);delay(50);return;}
  if(currentState==STATE_WIFI_ERROR){currentState=STATE_NORMAL;setupScreenShown=false;if(settingsHasStation())attemptUpdate(true);updateCounter=0;}
  if(portalLoop())applyNewSettings();if(!settingsHasStation()){portalOpen();if(!setupScreenShown)showPortalSetupScreen();delay(50);return;}extrasStatus(currentState!=STATE_NORMAL);
  handleQrButton(triggerQrAction,triggerLogAction);if(logModeActive){if(millis()-logModeStart>=LOG_DISPLAY_DURATION_MS){logModeActive=false;returnToDepartures();}else return;}if(qrModeActive){if(millis()-qrModeStart>=QR_DISPLAY_DURATION_MS){qrModeActive=false;returnToDepartures();}else return;}
  handleBootButton(triggerBootShort,triggerBootLong);if(autoResetPending&&millis()-autoResetStart>=VIEW_AUTO_RESET_MS){resetViewToDefault();autoResetPending=false;redrawCurrentView();updateCounter++;}
  if(currentState==STATE_API_ERROR){if(millis()-lastErrorRetry>=ERROR_RETRY_INTERVAL_MS){attemptUpdate(true);lastErrorRetry=millis();}return;}
  time_t nr=time(nullptr);struct tm ni;localtime_r(&nr,&ni);if(ni.tm_sec>=UPDATE_TARGET_SECOND&&ni.tm_min!=lastUpdateMinute){lastUpdateMinute=ni.tm_min;updateCounter++;bool full=updateCounter>=FULL_REFRESH_EVERY;if(full)updateCounter=0;attemptUpdate(full);}
}
