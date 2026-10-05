#pragma once

// Berlin/VBB-Fassung fuer Heltec Vision Master E290
#define MAX_STATIONS 4
#define STATION_GLOBAL_ID ""
#define FEATURE_DIRECTION_VIEW 0
#define DEFAULT_VIEW_A 1
#define ZENTRUM_IS_H 1              // nur Kompatibilitaet zu Altmodulen
#define DEFAULT_VIEW_ZENTRUM DEFAULT_VIEW_A
#define SHOW_SBAHN 1
#define SHOW_UBAHN 1
#define SHOW_TRAM  1
#define SHOW_BUS   1
#define SHOW_BAHN  1
#define FEATURE_WIFI_QR 0
#define QR_SCREEN_TITLE "WLAN"

// Kompatibilitaet mit dem unveraenderten Original-Displaymodul.
// Unsere VBB-Hauptlogik zeichnet A/B selbst in den Header und ruft den
// alten Zentrum/Auswaerts-Zweig nicht auf; die Konstanten muessen aber
// beim Kompilieren von display.cpp vorhanden sein.
#define LABEL_ZENTRUM "Richtung A"
#define LABEL_ZENTRUM_SHORT "A"
#define LABEL_AUSWAERTS "Richtung B"
#define LABEL_AUSWAERTS_SHORT "B"

#define BOOT_BUTTON 0
#define QR_BUTTON 21
#define MAX_DEPARTURES_SHOWN 4
#define MAX_RAW_ENTRIES 60
#define API_DEPARTURE_LIMIT 60
#define EARLY_DEPARTURE_MIN 3
#define UPDATE_TARGET_SECOND 1
#define FULL_REFRESH_EVERY 10
#define BUTTON_DEBOUNCE_MS 350UL
#define BUTTON_EDGE_STABLE_MS 30UL
#define BUTTON_LATCH_MAX_AGE_MS 15000UL
#define ERROR_RETRY_INTERVAL_MS 10000UL
#define API_RETRY_DELAY_MS 2000UL
#define SPLASH_DURATION_MS 5000UL
#define WIFI_ERROR_SCREEN_DELAY_MS 20000UL
#define HTTP_TIMEOUT_MS 12000
#define VIEW_AUTO_RESET_MS 30000UL
#define DIRECTION_AUTO_RESET_MS VIEW_AUTO_RESET_MS
#define PAGE_AUTO_RESET_MS VIEW_AUTO_RESET_MS
#define QR_DISPLAY_DURATION_MS 60000UL
#define LOG_DISPLAY_DURATION_MS 60000UL
#define LONG_PRESS_MS 3000UL
#define BOOT_LONG_PRESS_MS 2000UL
#define PORTAL_DURATION_MS 1800000UL
#define API_FAIL_WINDOW_MS 86400000UL
#define API_FAIL_HISTORY_SIZE 50
#define TIMEZONE_INFO "CET-1CEST,M3.5.0,M10.5.0/3"

#if !(SHOW_SBAHN || SHOW_UBAHN || SHOW_TRAM || SHOW_BUS || SHOW_BAHN)
#error "Mindestens ein Verkehrsmittel muss aktiviert sein"
#endif
