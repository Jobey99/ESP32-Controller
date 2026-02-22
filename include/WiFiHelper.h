#ifndef WIFI_HELPER_H
#define WIFI_HELPER_H

#include "AppConfig.h"
#include <WiFi.h>

struct WifiCfg {
  String mode = "apsta"; // ap | sta | apsta
  String staSsid = "";
  String staPass = "";
  String apSsid = "ESP32-AV-Tool";
  String apPass = "changeme123";
  uint8_t apChan = 6;
};

extern WifiCfg wifiCfg;

void loadWifi();
void saveWifi();
void startWiFi();
void wifiNoSleep();
String doScan(bool forceFresh);
void bootScanTask(void *pvParameters);

#endif
