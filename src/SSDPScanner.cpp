#include "SSDPScanner.h"

#include <WiFi.h>

extern void logAll(const String &s);

SSDPScanner ssdpScanner;

void SSDPScanner::begin() {
  _scanning = false;
  _pendingScan = false;
}

// Called from HTTP handler - just sets a flag, no blocking or WS calls
void SSDPScanner::startScan() {
  if (_scanning)
    return;
  _pendingScan = true;
}

// Called from loop() - safe to do UDP here
void SSDPScanner::doStartScan() {
  _pendingScan = false;
  _results.clear();
  _scanning = true;
  _scanStartTime = millis();

  if (WiFi.status() != WL_CONNECTED) {
    logAll("SSDP: WiFi STA not connected.");
    _scanning = false;
    return;
  }

  IPAddress localIP = WiFi.localIP();
  logAll("SSDP: WiFi OK, localIP=" + localIP.toString());

  _udp.stop();

  // Simple bind on ephemeral port - SSDP devices respond via unicast
  if (!_udp.begin(WiFi.localIP(), 8888)) {
    logAll("SSDP: UDP begin failed, trying without IP bind...");
    if (!_udp.begin(8888)) {
      logAll("SSDP: UDP begin(8888) also failed!");
      _scanning = false;
      return;
    }
  }
  logAll("SSDP: UDP socket ready on port 8888");

  const char *msg = "M-SEARCH * HTTP/1.1\r\n"
                    "HOST: 239.255.255.250:1900\r\n"
                    "MAN: \"ssdp:discover\"\r\n"
                    "MX: 3\r\n"
                    "ST: ssdp:all\r\n"
                    "\r\n";

  logAll("SSDP: Sending M-SEARCH x3...");
  IPAddress ssdpIP(239, 255, 255, 250);
  int sent = 0;
  for (int i = 0; i < 3; i++) {
    if (_udp.beginPacket(ssdpIP, 1900)) {
      _udp.write((uint8_t *)msg, strlen(msg));
      if (_udp.endPacket()) {
        sent++;
      } else {
        logAll("SSDP: endPacket FAILED for packet " + String(i + 1));
      }
    } else {
      logAll("SSDP: beginPacket FAILED for packet " + String(i + 1));
    }
  }
  logAll("SSDP: Sent " + String(sent) +
         "/3 M-SEARCH packets. Listening for 5s...");
}

void SSDPScanner::loop() {
  // Check if a scan was requested
  if (_pendingScan) {
    doStartScan();
  }

  if (!_scanning)
    return;

  // Timeout check (5 seconds)
  if (millis() - _scanStartTime > 5000) {
    _scanning = false;
    logAll("SSDP: Scan complete. Found " + String(_results.size()) +
           " devices.");
    _udp.stop();
    return;
  }

  // Poll for packets
  int limit = 5;
  while (limit--) {
    int len = _udp.parsePacket();
    if (len <= 0)
      break;

    // Read payload
    String data = "";
    while (_udp.available())
      data += (char)_udp.read();

    String fromIP = _udp.remoteIP().toString();
    logAll("SSDP: Rx " + String(len) + " bytes from " + fromIP);

    // Parse - Case Insensitive Header Search
    SSDPDevice dev;
    dev.ip = fromIP;

    String dataUpper = data;
    dataUpper.toUpperCase();

    int locIdx = dataUpper.indexOf("LOCATION:");
    if (locIdx != -1) {
      int end = dataUpper.indexOf("\r", locIdx);
      if (end == -1)
        end = dataUpper.indexOf("\n", locIdx);
      if (end > locIdx) {
        String s = data.substring(locIdx + 9, end);
        s.trim();
        dev.url = s;
      }
    }

    int usnIdx = dataUpper.indexOf("USN:");
    if (usnIdx != -1) {
      int end = dataUpper.indexOf("\r", usnIdx);
      if (end == -1)
        end = dataUpper.indexOf("\n", usnIdx);
      if (end > usnIdx) {
        String s = data.substring(usnIdx + 4, end);
        s.trim();
        dev.usn = s;
      }
    }

    int stIdx = dataUpper.indexOf("ST:");
    if (stIdx != -1) {
      int end = dataUpper.indexOf("\r", stIdx);
      if (end == -1)
        end = dataUpper.indexOf("\n", stIdx);
      if (end > stIdx) {
        String s = data.substring(stIdx + 3, end);
        s.trim();
        dev.st = s;
      }
    }

    // Deduplicate
    bool exists = false;
    for (const auto &d : _results) {
      if (d.usn.length() > 0 && d.usn == dev.usn) {
        exists = true;
        break;
      }
      if (d.ip == dev.ip && d.st == dev.st) {
        exists = true;
        break;
      }
    }

    if (!exists)
      _results.push_back(dev);
  }
}

String SSDPScanner::getResultsJson() {
  JsonDocument doc;
  doc["running"] = _scanning;
  JsonArray arr = doc["results"].to<JsonArray>();

  for (const auto &d : _results) {
    JsonObject obj = arr.add<JsonObject>();
    obj["ip"] = d.ip;
    obj["url"] = d.url;
    obj["usn"] = d.usn;
    obj["st"] = d.st;
    obj["friendlyName"] = d.st; // fallback
  }

  String out;
  serializeJson(doc, out);
  return out;
}
