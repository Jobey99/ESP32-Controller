#include "WiFiHelper.h"
#include <ArduinoJson.h>
#include <esp_wifi.h>

WifiCfg wifiCfg;
static String lastScanJson =
    R"({"networks":[],"count":0,"note":"No scan yet"})";
static uint32_t lastScanMs = 0;

void wifiNoSleep() {
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
}

void loadWifi() {
  wifiCfg.mode = prefs.getString("w_mode", wifiCfg.mode);
  wifiCfg.staSsid = prefs.getString("w_staSsid", wifiCfg.staSsid);
  wifiCfg.staPass = prefs.getString("w_staPass", wifiCfg.staPass);
  wifiCfg.apSsid = prefs.getString("w_apSsid", wifiCfg.apSsid);
  wifiCfg.apPass = prefs.getString("w_apPass", wifiCfg.apPass);
  wifiCfg.apChan = prefs.getUChar("w_apChan", wifiCfg.apChan);

  // Never allow empty AP SSID (prevents "blank AP name" bug)
  if (!wifiCfg.apSsid.length())
    wifiCfg.apSsid = "ESP32-AV-Tool";
  if (wifiCfg.apPass.length() && wifiCfg.apPass.length() < 8)
    wifiCfg.apPass = "changeme123";
}

void saveWifi() {
  prefs.putString("w_mode", wifiCfg.mode);
  prefs.putString("w_staSsid", wifiCfg.staSsid);
  prefs.putString("w_staPass", wifiCfg.staPass);
  prefs.putString("w_apSsid", wifiCfg.apSsid);
  prefs.putString("w_apPass", wifiCfg.apPass);
  prefs.putUChar("w_apChan", wifiCfg.apChan);
}

void startWiFi() {
  if (wifiCfg.mode == "sta")
    WiFi.mode(WIFI_STA);
  else if (wifiCfg.mode == "ap") {
    WiFi.disconnect(true, true); // Erase & Disconnect
    WiFi.mode(WIFI_AP);
    WiFi.setAutoConnect(false); // Do not let SDK auto-connect
  } else
    WiFi.mode(WIFI_AP_STA);

  // wifiNoSleep();
  // WiFi.setTxPower(WIFI_POWER_19_5dBm); // MAX POWER

  if (wifiCfg.mode != "sta") {
    bool ok = WiFi.softAP(wifiCfg.apSsid.c_str(), wifiCfg.apPass.c_str(),
                          wifiCfg.apChan);
    logAll(String("AP ") + (ok ? "started" : "failed") +
           " SSID=" + wifiCfg.apSsid + " IP=" + WiFi.softAPIP().toString());
  }

  if (wifiCfg.mode != "ap" && wifiCfg.staSsid.length()) {
    WiFi.begin(wifiCfg.staSsid.c_str(), wifiCfg.staPass.c_str());
    logAll("STA connecting: " + wifiCfg.staSsid);
  } else {
    // CRITICAL: Ensure no ghost connection from SDK NVS
    WiFi.disconnect(true);
  }
}

// Helper to generate JSON from scan results
String _genScanJson(int n) {
  JsonDocument doc;
  doc["count"] = (n >= 0) ? n : 0;
  String note = "Scan Done.";
  if (n == WIFI_SCAN_FAILED)
    note = "Scan Failed.";
  else if (n == WIFI_SCAN_RUNNING)
    note = "Scanning...";
  else if (n == 0)
    note = "No networks found.";

  doc["note"] = note;
  doc["debug_code"] = n;

  JsonArray arr = doc["networks"].to<JsonArray>();
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      JsonObject o = arr.add<JsonObject>();
      o["ssid"] = WiFi.SSID(i);
      o["rssi"] = WiFi.RSSI(i);
      o["chan"] = WiFi.channel(i);
      o["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    }
  }
  String out;
  serializeJson(doc, out);
  return out;
}

void tryScanAsync() {
  if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
    WiFi.scanNetworks(true); // Async = true
    Serial.println("WIFI: Async Scan started");
  }
}

String doScan(bool forceFresh) {
  int n = WiFi.scanComplete();

  // If scan is finished (n >= 0), update cache
  if (n >= 0) {
    lastScanJson = _genScanJson(n);
    lastScanMs = millis();
    WiFi.scanDelete(); // Reset for next scan

    // If forced fresh, we just consumed the result.
    // If not forced, we just updated the cache.
  }

  // If forced fresh and NOT running, start a new one
  if (forceFresh && n != WIFI_SCAN_RUNNING) {
    tryScanAsync();
    // Return status saying "Scanning started"
    // But we usually want to return existing data while scanning
  }

  // If running, or just updated, return cache
  return lastScanJson;
}

void bootScanTask(void *) {
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  tryScanAsync();
  vTaskDelete(nullptr);
}
