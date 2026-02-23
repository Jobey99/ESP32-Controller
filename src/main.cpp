#include "AVDiscovery.h"
#include "AppConfig.h"
#include "CaptureProxy.h"
#include "ConfigManager.h"
#include "MacroHandler.h"
#include "OTAHandler.h"
#include "PortScanner.h"
#include "RS232Handler.h"
#include "SSDPScanner.h"
#include "TerminalHandler.h"
#include "Utils.h"
#include "WebAPI.h"
#include "WiFiHelper.h"
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

// Defined in WebAPI.cpp
extern void mdnsScanLoop();
extern void pjlinkLoop();

Preferences prefs;
uint32_t bootMs;
bool shouldReboot = false;

void logAll(const String &s) {
  Serial.println(s);
  wsTextAll(wsLog, s);
}

void wsTextAll(AsyncWebSocket &ws, const String &s) { ws.textAll(s); }

void setup() {
  Serial.begin(115200);
  delay(150);
  bootMs = millis();

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  }

  prefs.begin("avtool", false);
  loadWifi();
  loadCfg();

  startWiFi();

  xTaskCreatePinnedToCore(bootScanTask, "bootScan", 4096, nullptr, 1, nullptr,
                          1);
  startLearn();

  if (!MDNS.begin("esp32-av-tool")) {
    Serial.println("Error setting up MDNS responder!");
  }

  rs232Setup(); // Initialize Serial2 and RS232 WebSocket handler
  setupRoutes();
  server.begin();

  xTaskCreatePinnedToCore(deviceMonitorTask, "devMon", 6144, nullptr, 1,
                          nullptr, 1);

  logAll(String("Ready FW ") + FW_VERSION + " UI: /  OTA: /update");

  portScanner.begin();
  ssdpScanner.begin();
  macroHandler.begin();

  otaHandler.setManifestUrl(OTA_UPDATE_URL);
  otaHandler.begin();
}

void loop() {
  if (shouldReboot) {
    delay(500);
    ESP.restart();
  }
  rs232Loop();

  ArduinoOTA.handle();

  // Non-blocking Scanners
  portScanner.loop();
  ssdpScanner.loop();
  mdnsScanLoop();
  pjlinkLoop();
  macroHandler.loop();
  otaHandler.loop();

  // WebSocket Cleanup
  wsLog.cleanupClients();
  wsTerm.cleanupClients();
  wsProxy.cleanupClients();
  wsDisc.cleanupClients();
  wsRS232.cleanupClients();
  wsUdp.cleanupClients();
  wsTcpServer.cleanupClients();
}
