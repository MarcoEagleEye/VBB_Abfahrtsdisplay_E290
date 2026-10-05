#pragma once
#include <Arduino.h>
#include "../config.h"

struct StationSettings {
  String id;
  String name;
  String dirAJson;   // JSON-Array der Ziele, z.B. ["Pankow"]
  String dirBJson;
  String labelA;
  String labelB;
  bool defaultA;
};

struct DeviceSettings {
  String wifiSsid;
  String wifiPassword;
  StationSettings stations[MAX_STATIONS];
  bool directionView;
  bool showSbahn;
  bool showUbahn;
  bool showTram;
  bool showBus;
  bool showBahn;
  bool wifiQr;
  String qrTitle;
  String qrSsid;
  String qrPassword;
};

extern DeviceSettings appSettings;

class SettingsLock {
 public:
  SettingsLock();
  ~SettingsLock();
  SettingsLock(const SettingsLock&) = delete;
  SettingsLock& operator=(const SettingsLock&) = delete;
};

void settingsLoad();
bool settingsSave();
bool settingsSaveWifi(const String& ssid, const String& password);
bool settingsFactoryReset();
bool settingsHasWifi();
bool settingsHasStation();
int settingsFirstStationIndex();
int settingsNextStationIndex(int current);
void settingsPrint();
