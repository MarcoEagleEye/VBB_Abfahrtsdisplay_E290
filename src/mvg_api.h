#pragma once
#include <Arduino.h>

struct Departure {
  String line;
  String destination;
  String time;
  int delayMin;
  bool cancelled;
  bool realtime;
  bool hasWarning;
  bool isBus;
};

enum DirectionFilter { DIR_FILTER_H, DIR_FILTER_R, DIR_FILTER_ALL };

bool fetchStationName(const char* stopId, String& nameOut);
bool downloadDepartures(const char* stopId, String& payloadOut);
bool searchStations(const String& query, String& jsonOut);
bool listDirections(const char* stopId, String& jsonOut);
int parseDepartures(const String& payload, DirectionFilter filter, Departure result[], int maxResults);
int parseDeparturesForTargets(const String& payload, const String& targetsJson, Departure result[], int maxResults);
