#include "OTAHandler.h"
#include "AppConfig.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

OTAHandler otaHandler;

// Minimum free heap required before attempting TLS connection (~40KB for TLS)
static const uint32_t MIN_HEAP_FOR_TLS = 45000;

void OTAHandler::begin() { _lastCheck = millis() - _checkInterval + 10000; }

void OTAHandler::setManifestUrl(const String &url) { _manifestUrl = url; }

void OTAHandler::triggerCheck() { _manualCheck = true; }

void OTAHandler::loop() {
  if (_manifestUrl.isEmpty())
    return;

  if (_manualCheck || (millis() - _lastCheck >= _checkInterval)) {
    _manualCheck = false;
    _lastCheck = millis();
    if (WiFi.status() == WL_CONNECTED) {
      // Run on a dedicated task with 16KB stack (TLS needs lots of stack)
      xTaskCreatePinnedToCore(
          [](void *param) {
            OTAHandler *self = (OTAHandler *)param;
            self->checkUpdate();
            vTaskDelete(NULL);
          },
          "otaCheck", 16384, this, 1, NULL, 1);
    }
  }
}

void OTAHandler::checkUpdate() {
  uint32_t freeHeap = ESP.getFreeHeap();
  logAll("OTA: Free heap = " + String(freeHeap) + " bytes");

  if (freeHeap < MIN_HEAP_FOR_TLS) {
    logAll("OTA: Not enough heap for TLS (" + String(freeHeap) + " < " +
           String(MIN_HEAP_FOR_TLS) + "). Skipping.");
    return;
  }

  logAll("OTA: Connecting to GitHub...");

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10); // 10 second timeout

  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(15000); // 15 second timeout
  http.begin(client, _manifestUrl);

  logAll("OTA: Sending GET request...");
  int httpCode = http.GET();
  logAll("OTA: HTTP response code = " + String(httpCode));

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    logAll("OTA: Manifest received (" + String(payload.length()) + " bytes)");

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      // 1. Check Filesystem
      if (doc["filesystem"].is<JsonObject>()) {
        String fsVer = doc["filesystem"]["version"].as<String>();
        String fsUrl = doc["filesystem"]["url"].as<String>();

        if (isNewer(fsVer, FS_VERSION)) {
          logAll("New Filesystem version available: " + fsVer);
          performFSUpdate(fsUrl);
          return;
        }
      }

      // 2. Check Firmware
      if (doc["firmware"].is<JsonObject>()) {
        String fwVer = doc["firmware"]["version"].as<String>();
        String fwUrl = doc["firmware"]["url"].as<String>();

        logAll("Manifest FW: " + fwVer + " (Current: " + FW_VERSION + ")");

        if (isNewer(fwVer, FW_VERSION)) {
          logAll("New Firmware available! Starting update...");
          performUpdate(fwUrl);
        } else {
          logAll("Firmware is up to date.");
        }
      }
      // 3. Backward Compatibility (old manifest style)
      else if (doc["version"].is<String>()) {
        String newVersion = doc["version"].as<String>();
        String binUrl = doc["url"].as<String>();
        if (isNewer(newVersion, FW_VERSION)) {
          logAll("New Firmware available (Legacy)! Starting update...");
          performUpdate(binUrl);
        }
      }

    } else {
      logAll("OTA: Failed to parse manifest: " + String(error.c_str()));
    }
  } else {
    logAll("OTA: Update check failed, HTTP error: " + String(httpCode));
  }

  http.end();
}

bool OTAHandler::isNewer(const String &serverVer, const String &currentVer) {
  return serverVer > currentVer;
}

void OTAHandler::performUpdate(const String &url) {
  logAll("OTA: Starting FW download from: " + url);

  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.rebootOnUpdate(false);
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  t_httpUpdate_return ret = httpUpdate.update(client, url);

  switch (ret) {
  case HTTP_UPDATE_FAILED:
    logAll("FW Update Failed: " + httpUpdate.getLastErrorString());
    break;
  case HTTP_UPDATE_NO_UPDATES:
    logAll("No FW updates");
    break;
  case HTTP_UPDATE_OK:
    logAll("FW Update Success! Rebooting soon...");
    shouldReboot = true;
    break;
  }
}

void OTAHandler::performFSUpdate(const String &url) {
  logAll("OTA: Starting FS download from: " + url);

  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.rebootOnUpdate(false);
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  t_httpUpdate_return ret = httpUpdate.update(client, url, U_SPIFFS);

  switch (ret) {
  case HTTP_UPDATE_FAILED:
    logAll("FS Update Failed: " + httpUpdate.getLastErrorString());
    break;
  case HTTP_UPDATE_NO_UPDATES:
    logAll("No FS updates");
    break;
  case HTTP_UPDATE_OK:
    logAll("FS Update Success! Rebooting soon...");
    shouldReboot = true;
    break;
  }
}
