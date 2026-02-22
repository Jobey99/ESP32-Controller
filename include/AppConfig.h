#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#// Basic Ident
#define FW_VERSION "0.8.6"
#define FS_VERSION "0.1.0"
#define DEFAULT_HOSTNAME "esp32-av-tool"
// OTA manifest via raw.githubusercontent.com (no redirects, direct TLS)
// Update version.json in the repo root when publishing a new release
#define OTA_UPDATE_URL                                                         \
  "https://raw.githubusercontent.com/Jobey99/ESP32-Controller/main/"           \
  "version.json"
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

// Firmware version
// The previous static const char *FW_VERSION definition is replaced by the
// #define above.

// Global objects (defined in main.cpp for now, or a central location)
extern AsyncWebServer server;
extern AsyncWebSocket wsLog;
extern AsyncWebSocket wsTerm;
extern AsyncWebSocket wsProxy;
extern AsyncWebSocket wsDisc;
extern AsyncWebSocket wsRS232;
extern AsyncWebSocket wsUdp;
extern AsyncWebSocket wsTcpServer;
extern Preferences prefs;

extern uint32_t bootMs;
extern bool shouldReboot;

// Shared utilities
void logAll(const String &s);
void wsTextAll(AsyncWebSocket &ws, const String &s);

#endif
