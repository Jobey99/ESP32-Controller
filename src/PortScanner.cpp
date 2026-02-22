#include "PortScanner.h"
#include <WiFiClient.h>

PortScanner portScanner;

void PortScanner::begin() { _scanning = false; }

void PortScanner::startScan(const String &ip, const std::vector<int> &ports) {
  _targetIp = ip;
  _portsToScan = ports;
  _openPorts.clear();
  _currentIndex = 0;
  _scanning = true;
  _lastScanTime = 0;
}

void PortScanner::loop() {
  if (!_scanning)
    return;

  // Small delay to yield time to other tasks
  if (millis() - _lastScanTime < 20)
    return;
  _lastScanTime = millis();

  if (_currentIndex >= _portsToScan.size()) {
    _scanning = false;
    return;
  }

  int port = _portsToScan[_currentIndex];

  WiFiClient c;
  // We use blocking connect, but it is limited to one per loop iteration
  // The timeout is 200ms, which is safe for the main loop wdt (usually 5s)
  if (c.connect(_targetIp.c_str(), port, 200)) {
    _openPorts.push_back(port);
    c.stop();
  } else {
    c.stop();
  }

  _currentIndex++;
}

String PortScanner::getResultsJson() {
  JsonDocument doc;
  doc["running"] = _scanning;
  doc["progress"] = _scanning
                        ? (_currentIndex * 100 /
                           (_portsToScan.size() ? _portsToScan.size() : 1))
                        : 100;
  JsonArray open = doc["open"].to<JsonArray>();
  for (int p : _openPorts)
    open.add(p);
  String out;
  serializeJson(doc, out);
  return out;
}
