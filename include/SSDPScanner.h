#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <vector>

struct SSDPDevice {
  String ip;
  String url;
  String usn;
  String st;
  String friendlyName;
};

class SSDPScanner {
public:
  void begin();
  void loop();
  void startScan();
  bool isScanning() { return _scanning; }
  String getResultsJson();

private:
  void doStartScan();
  bool _scanning = false;
  bool _pendingScan = false;
  unsigned long _scanStartTime = 0;
  WiFiUDP _udp;
  std::vector<SSDPDevice> _results;
};

extern SSDPScanner ssdpScanner;
