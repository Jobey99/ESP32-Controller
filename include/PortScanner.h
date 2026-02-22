#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>


class PortScanner {
public:
  void begin();
  void loop();
  void startScan(const String &ip, const std::vector<int> &ports);
  bool isScanning() { return _scanning; }
  String getResultsJson();

private:
  bool _scanning = false;
  String _targetIp;
  std::vector<int> _portsToScan;
  std::vector<int> _openPorts;
  size_t _currentIndex = 0;
  unsigned long _lastScanTime = 0;
};

extern PortScanner portScanner;
