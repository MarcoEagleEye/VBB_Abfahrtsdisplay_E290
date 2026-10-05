#pragma once
#include <Arduino.h>
void portalBegin(const char* firmwareVersion);
void portalOpen();
bool portalIsOpen();
String portalUrl();
String portalClosingTime();
bool portalUpdateRunning();
bool portalLoop();
