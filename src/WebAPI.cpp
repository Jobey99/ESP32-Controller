#include "WebAPI.h"
#include <ArduinoJson.h>
#include <ESP32Ping.h> // Keep looking for header, but rely on PlatformIO finding it
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFiUdp.h>

#include "AVDiscovery.h"
#include "CaptureProxy.h"
#include "ConfigManager.h"
#include "OTAHandler.h"
#include "PortScanner.h"
#include "RS232Handler.h"
#include "SSDPScanner.h"

// mDNS scan state (flag-based, runs from main loop)
static volatile bool mdnsScanPending = false;
static volatile bool mdnsScanning = false;
static String mdnsPendingService = "";
static String mdnsPendingProto = "";
static String mdnsResultsJson = "{\"running\":false,\"results\":[]}";

// Called from loop() in main.cpp — safe to use MDNS library here
void mdnsScanLoop() {
  if (!mdnsScanPending)
    return;
  mdnsScanPending = false;
  mdnsScanning = true;

  String service = mdnsPendingService;
  String proto = mdnsPendingProto;

  // Strip leading underscore — MDNS.queryService expects bare name
  if (service.startsWith("_"))
    service.remove(0, 1);
  if (proto.startsWith("_"))
    proto.remove(0, 1);

  Serial.printf("mDNS: Querying service=%s proto=%s\n", service.c_str(),
                proto.c_str());
  int n = MDNS.queryService(service.c_str(), proto.c_str());
  Serial.printf("mDNS: Found %d services\n", n);

  JsonDocument res;
  res["running"] = false;
  res["count"] = n;
  JsonArray arr = res["results"].to<JsonArray>();
  for (int i = 0; i < n; ++i) {
    JsonObject o = arr.add<JsonObject>();
    o["hostname"] = MDNS.hostname(i);
    o["ip"] = MDNS.IP(i).toString();
    o["port"] = MDNS.port(i);
  }
  serializeJson(res, mdnsResultsJson);
  mdnsScanning = false;
}
#include "TcpServerHandler.h" // Added
#include "TerminalHandler.h"
#include "UdpHandler.h" // Added
#include "Utils.h"
#include "WiFiHelper.h"

extern bool learnEnabled;
extern uint16_t learnPort;

AsyncWebServer server(80);
AsyncWebSocket wsLog("/ws");
AsyncWebSocket wsTerm("/term");
AsyncWebSocket wsProxy("/wsproxy");
AsyncWebSocket wsDisc("/wsdisc");
AsyncWebSocket wsRS232("/wsrs232");
AsyncWebSocket wsUdp("/wsudp");             // New UDP WebSocket
AsyncWebSocket wsTcpServer("/wstcpserver"); // TCP Server WebSocket

void setupRoutes() {

  server.on(
      "/api/tcpserver", HTTP_POST, [](AsyncWebServerRequest *req) {}, NULL,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index,
         size_t total) {
        JsonDocument doc;
        deserializeJson(doc, data, len);
        String action = doc["action"];
        if (action == "start") {
          uint16_t port = doc["port"] | 23;
          tcpServerHandler.begin(port);
        } else if (action == "stop") {
          tcpServerHandler.end();
        }
        JsonDocument res;
        res["running"] = tcpServerHandler.isRunning();
        res["port"] = tcpServerHandler.getPort();
        String out;
        serializeJson(res, out);
        req->send(200, "application/json", out);
      });

  server.on("/api/tcpserver", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["running"] = tcpServerHandler.isRunning();
    doc["port"] = tcpServerHandler.getPort();
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/api/health", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["fw"] = FW_VERSION;
    doc["uptime_s"] = (millis() - bootMs) / 1000;
    doc["heap_free"] = ESP.getFreeHeap();

    doc["wifi"]["mode"] = wifiCfg.mode;
    doc["wifi"]["staConnected"] = (WiFi.status() == WL_CONNECTED);
    doc["wifi"]["staIp"] = WiFi.localIP().toString();
    doc["wifi"]["staSsid"] = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "";
    doc["wifi"]["apIp"] = WiFi.softAPIP().toString();
    doc["wifi"]["apSsid"] = wifiCfg.apSsid;
    doc["wifi"]["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;

    doc["learn"]["enabled"] = learnEnabled;
    doc["learn"]["port"] = learnPort;

    doc["term"]["connected"] = termConnected;
    doc["term"]["host"] = termHost;
    doc["term"]["port"] = termPort;

    doc["proxy"]["running"] = proxyRunning;
    doc["proxy"]["listenPort"] = proxyListenPort;
    doc["proxy"]["targetHost"] = proxyTargetHost;
    doc["proxy"]["targetPort"] = proxyTargetPort;
    doc["proxy"]["captureToLearn"] = proxyCaptureToLearn;

    doc["disc"]["running"] = discRunning;
    doc["disc"]["progress"] = discProgress;

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html",
                  "<form method='POST' action='/update' "
                  "enctype='multipart/form-data'><input type='file' "
                  "name='update'><input type='submit' value='Update'></form>");
  });

  server.on(
      "/update", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(
            200, "application/json",
            shouldReboot ? "{\"ok\":true}" : "{\"error\":\"OTA Failed\"}");
        response->addHeader("Connection", "close");
        request->send(response);
      },
      [](AsyncWebServerRequest *request, String filename, size_t index,
         uint8_t *data, size_t len, bool final) {
        if (!index) {
          int cmd = U_FLASH;
          if (request->hasParam("type", true)) {
            String type = request->getParam("type", true)->value();
            if (type == "fs")
              cmd = U_SPIFFS;
          }
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
            Update.printError(Serial);
          }
        }
        if (!Update.hasError()) {
          if (Update.write(data, len) != len) {
            Update.printError(Serial);
          }
        }
        if (final) {
          if (Update.end(true)) {
            Serial.printf("Update Success: %uB\n", index + len);
          } else {
            Update.printError(Serial);
          }
        }
      });

  server.on("/api/rollback", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (Update.canRollBack()) {
      if (Update.rollBack()) {
        req->send(200, "application/json",
                  "{\"ok\":true, \"note\":\"Rolled back. Rebooting...\"}");
        shouldReboot = true;
      } else {
        req->send(500, "application/json", "{\"error\":\"Rollback failed\"}");
      }
    } else {
      req->send(
          400, "application/json",
          "{\"error\":\"Rollback not supported or no previous version\"}");
    }
  });

  server.on("/api/ota/check", HTTP_POST, [](AsyncWebServerRequest *req) {
    // We can't return the result of the check easily because it logs to serial
    // But we can trigger it.
    // Ideally we modify OTAHandler to return status, but for now:
    otaHandler.checkUpdate();
    req->send(200, "application/json",
              "{\"ok\":true, \"msg\":\"Check triggered. See Logs.\"}");
  });

  // --- New Network Tools ---

  // DNS Lookup
  server.on(
      "/api/dns", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len))
          return req->send(400);
        String host = doc["host"] | "google.com";

        IPAddress ip;
        bool found = (WiFi.hostByName(host.c_str(), ip) == 1);

        JsonDocument res;
        res["ok"] = found;
        res["ip"] = found ? ip.toString() : "";
        String out;
        serializeJson(res, out);
        req->send(200, "application/json", out);
      });

  // Simple Port Scanner (Targeted)
  server.on(
      "/api/portscan", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          return req->send(400, "text/plain", "Invalid JSON");
        }
        if (doc["host"].is<String>() && doc["ports"].is<JsonArray>()) {
          String ip = doc["host"];
          JsonArray ports = doc["ports"];
          std::vector<int> portList;
          for (JsonVariant v : ports) {
            portList.push_back(v.as<int>());
          }
          portScanner.startScan(ip, portList);
          req->send(200, "application/json", "{\"status\":\"started\"}");
        } else {
          req->send(400, "text/plain",
                    "Bad Request: Missing 'host' or 'ports'");
        }
      });

  // GET /api/portscan/status
  server.on(
      "/api/portscan/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", portScanner.getResultsJson());
      });

  // Internet Check
  server.on("/api/internet", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument res;

    // Check 1: DNS Resolve
    IPAddress ip;
    bool dns = (WiFi.hostByName("google.com", ip) == 1);
    res["dns"] = dns;

    // Check 2: Ping Google DNS
    bool ping = Ping.ping("8.8.8.8", 1);
    res["ping"] = ping;

    // Check 3: Public IP (optional, might be slow so maybe skip or do simple
    // http get) Keeping it simple for speed

    String out;
    serializeJson(res, out);
    req->send(200, "application/json", out);
  });

  // --- UDP Tools ---

  // API: Send UDP
  server.on(
      "/api/udp/send", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len))
          return req->send(400);

        String ip = doc["ip"];
        int port = doc["port"];
        String msg = doc["data"];

        if (ip.length() > 0 && port > 0) {
          udpHandler.send(ip, port, msg);
          req->send(200, "application/json", "{\"ok\":true}");
        } else {
          req->send(400, "application/json", "{\"error\":\"bad args\"}");
        }
      });

  // API: Set Listen Port
  server.on(
      "/api/udp/listen", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        deserializeJson(doc, data, len);
        int port = doc["port"] | 5000;
        udpHandler.setListenPort(port);
        req->send(200, "application/json", "{\"ok\":true}");
      });

  // API: MDNS Scan (async — runs on background task)
  server.on(
      "/api/mdns/scan", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        if (mdnsScanning) {
          req->send(200, "application/json",
                    "{\"status\":\"already_running\"}");
          return;
        }
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        String service = doc["service"] | "_http";
        String proto = doc["proto"] | "tcp";

        // Set pending flag — main loop will run the query
        mdnsPendingService = service;
        mdnsPendingProto = proto;
        mdnsResultsJson = "{\"running\":true,\"results\":[]}";
        mdnsScanPending = true;

        req->send(200, "application/json", "{\"status\":\"started\"}");
      });

  // API: MDNS Status/Results
  server.on("/api/mdns/status", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json", mdnsResultsJson);
  });

  // API: SSDP Scan Start
  server.on("/api/ssdp/scan", HTTP_POST, [](AsyncWebServerRequest *req) {
    ssdpScanner.startScan();
    req->send(200, "application/json", "{\"status\":\"started\"}");
  });

  // API: SSDP Status/Results
  server.on("/api/ssdp/status", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json", ssdpScanner.getResultsJson());
  });

  wsLog.onEvent([](AsyncWebSocket *, AsyncWebSocketClient *c, AwsEventType t,
                   void *, uint8_t *, size_t) {
    if (t == WS_EVT_CONNECT)
      c->text("log connected");
  });

  wsUdp.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client,
                   AwsEventType type, void *arg, uint8_t *data, size_t len) {
    // Optional: handle UDP send requests from UI via WS if we want
  });

  // TCP Server WS
  wsTcpServer.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client,
                         AwsEventType type, void *arg, uint8_t *data,
                         size_t len) {
    if (type == AwsEventType::WS_EVT_CONNECT) {
      // Send initial status?
    }
  });
  server.addHandler(&wsTcpServer);

  server.addHandler(&wsLog);
  server.addHandler(&wsTerm);
  server.addHandler(&wsProxy);
  server.addHandler(&wsDisc);
  server.addHandler(&wsRS232);
  server.addHandler(&wsUdp); // Register UDP WS

  wsTerm.onEvent([](AsyncWebSocket *, AsyncWebSocketClient *c, AwsEventType t,
                    void *, uint8_t *data, size_t len) {
    if (t != WS_EVT_DATA)
      return;

    JsonDocument doc;
    if (deserializeJson(doc, data, len))
      return;

    String action = doc["action"] | "";
    if (action == "connect") {
      String host = doc["host"] | "";
      uint16_t port = doc["port"] | 0;

      // Use non-blocking request to avoid WDT trigger in AsyncTCP callback
      termRequestConnect(host, port);

      // We don't send "connected" here immediately.
      // The termPumpTask will send a status update when it connects or fails.
      logAll("Terminal connect requested to " + host + ":" + String(port));
      return;
    }

    if (action == "disconnect") {
      termRequestDisconnect();
      logAll("Terminal disconnected");
      return;
    }

    if (action == "send") {
      if (!termConnected) {
        c->text(R"({"type":"error","msg":"Not connected"})");
        return;
      }
      String mode = doc["mode"] | "ascii";
      String payload = doc["data"] | "";
      String suffix = doc["suffix"] | "";
      if (mode == "hex") {
        std::vector<uint8_t> bytes;
        if (!parseHexBytes(payload, bytes)) {
          c->text(R"({"type":"error","msg":"Bad hex"})");
          return;
        }
        termRequestSend(bytes.data(), bytes.size());
      } else {
        String out = payload;
        if (suffix == "\\r")
          out += "\r";
        else if (suffix == "\\n")
          out += "\n";
        else if (suffix == "\\r\\n")
          out += "\r\n";
        else if (suffix.length())
          out += suffix;
        termRequestSend((const uint8_t *)out.c_str(), out.length());
      }
      c->text(R"({"type":"tx","ok":true})");
      return;
    }
  });

  server.on("/api/ping", HTTP_GET, [](AsyncWebServerRequest *req) {
    String host =
        req->hasParam("host") ? req->getParam("host")->value() : "8.8.8.8";
    bool ret = Ping.ping(host.c_str(), 1);
    JsonDocument doc;
    doc["host"] = host;
    doc["ok"] = ret;
    doc["avg_time_ms"] = Ping.averageTime();
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // Removed: duplicate blocking GET /api/ssdp/scan
  // The correct async POST handler is registered above at /api/ssdp/scan

  server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *req) {
    bool fresh =
        req->hasParam("fresh") && req->getParam("fresh")->value() == "1";
    req->send(200, "application/json", doScan(fresh));
  });

  server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["mode"] = wifiCfg.mode;
    doc["staSsid"] = wifiCfg.staSsid;
    doc["apSsid"] = wifiCfg.apSsid;
    doc["apChan"] = wifiCfg.apChan;
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on(
      "/api/wifi", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        wifiCfg.mode = doc["mode"] | wifiCfg.mode;
        String staSsid = doc["staSsid"] | "";
        String staPass = doc["staPass"] | "";
        if (staSsid.length())
          wifiCfg.staSsid = staSsid;
        if (staPass.length())
          wifiCfg.staPass = staPass;
        String apSsid = doc["apSsid"] | "";
        String apPass = doc["apPass"] | "";
        if (apSsid.length())
          wifiCfg.apSsid = apSsid;
        if (apPass.length())
          wifiCfg.apPass = apPass;
        if (doc["apChan"].is<uint8_t>())
          wifiCfg.apChan = doc["apChan"].as<uint8_t>();
        saveWifi();
        req->send(200, "application/json",
                  "{\"ok\":true,\"note\":\"reboot required\"}");
      });

  server.on("/api/wifi/forget", HTTP_POST, [](AsyncWebServerRequest *req) {
    wifiCfg.staSsid = "";
    wifiCfg.staPass = "";
    wifiCfg.mode = "ap";
    saveWifi();
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_AP);
    req->send(200, "application/json",
              "{\"ok\":true,\"note\":\"Forget OK. Rebooting...\"}");
    shouldReboot = true;
  });

  server.on("/api/scan/subnet", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (discRunning) {
      req->send(409, "application/json",
                "{\"error\":\"scan already running\"}");
      return;
    }
    startDisc();
    req->send(200, "application/json", "{\"ok\":true}");
  });

  server.on(
      "/api/discovery/start", HTTP_POST, [](AsyncWebServerRequest *req) {},
      nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        String subnet = doc["subnet"] | "";
        uint8_t from = doc["from"] | 1;
        uint8_t to = doc["to"] | 254;
        std::vector<uint16_t> ports;
        if (doc["ports"].is<JsonArray>()) {
          for (JsonVariant v : doc["ports"].as<JsonArray>()) {
            uint16_t p = v.as<uint16_t>();
            if (p > 0)
              ports.push_back(p);
          }
        }
        if (ports.empty())
          ports = {23, 80, 443, 5000, 1515, 6100};
        if (!subnet.length()) {
          IPAddress ip = WiFi.localIP();
          subnet = String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]);
        }
        bool ok = true; // startDiscovery implementation needed
        // For now, call the one we have
        startDisc();
        req->send(200, "application/json", "{\"ok\":true}");
      });

  server.on("/api/discovery/results", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    doc["running"] = discRunning;
    doc["progress"] = discProgress;
    JsonArray arr = doc["results"].to<JsonArray>();
    for (auto &line : discFound) {
      JsonDocument row;
      if (!deserializeJson(row, line))
        arr.add(row.as<JsonObject>());
    }
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/api/captures", HTTP_GET, [](AsyncWebServerRequest *req) {
    String filter =
        req->hasParam("filter") ? req->getParam("filter")->value() : "";
    bool pinnedOnly =
        req->hasParam("pinned") && req->getParam("pinned")->value() == "1";
    JsonDocument doc;
    JsonArray arr = doc["captures"].to<JsonArray>();
    for (int i = (int)caps.size() - 1; i >= 0; i--) {
      const auto &c = caps[i];
      if (pinnedOnly && !c.pinned)
        continue;
      if (filter.length() && c.srcIp.indexOf(filter) < 0)
        continue;
      JsonObject o = arr.add<JsonObject>();
      o["id"] = c.id;
      o["ts"] = c.ts;
      o["srcIp"] = c.srcIp;
      o["srcPort"] = c.srcPort;
      o["localPort"] = c.localPort;
      o["hex"] = c.hex;
      o["ascii"] = c.ascii;
      o["pinned"] = c.pinned;
      o["repeats"] = c.repeats;
      o["lastTs"] = c.lastTs;
      o["suffixHint"] = c.suffixHint;
      o["payloadType"] = c.payloadType;
    }
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on(
      "/api/capture/pin", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        String id = doc["id"] | "";
        bool pin = doc["pin"] | true;
        for (auto &c : caps)
          if (c.id == id) {
            c.pinned = pin;
            break;
          }
        req->send(200, "application/json", "{\"ok\":true}");
      });

  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json", cfgJson);
  });

  server.on(
      "/api/config", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument tmp;
        if (deserializeJson(tmp, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        cfgJson = String((const char *)data).substring(0, len);
        saveCfg();
        req->send(200, "application/json", "{\"ok\":true}");
      });

  server.on("/api/devices", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    if (deserializeJson(doc, cfgJson)) {
      req->send(500, "application/json", "{\"error\":\"cfg parse\"}");
      return;
    }
    String out;
    serializeJson(doc["devices"], out);
    req->send(200, "application/json", out);
  });

  server.on("/api/devices/status", HTTP_GET, [](AsyncWebServerRequest *req) {
    JsonDocument doc;
    JsonArray arr = doc["status"].to<JsonArray>();
    for (auto &s : devStatuses) {
      JsonObject o = arr.add<JsonObject>();
      o["id"] = s.id;
      o["online"] = s.online;
      o["lastSeenMs"] = s.lastSeenMs;
      o["ip"] = s.lastIp;
      o["port"] = s.lastPort;
    }
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on(
      "/api/devices/add", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument d;
        if (deserializeJson(d, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        String name = d["name"] | "Device";
        String ip = d["ip"] | "";
        uint16_t portHint = d["portHint"] | 0;
        String suffixHint = d["defaultSuffix"] | "";
        String notes = d["notes"] | "";
        String templateId = d["templateId"] | "";
        String payloadType = d["defaultPayloadType"] | "";
        String mac = d["mac"] | "";
        if (!ip.length()) {
          req->send(400, "application/json", "{\"error\":\"missing ip\"}");
          return;
        }
        if (!updateCfgWithDevice(name, ip, portHint, suffixHint, notes,
                                 templateId, payloadType, mac)) {
          req->send(500, "application/json",
                    "{\"error\":\"cfg update failed\"}");
          return;
        }
        req->send(200, "application/json", "{\"ok\":true}");
      });

  server.on(
      "/api/devices/delete", HTTP_POST, [](AsyncWebServerRequest *req) {},
      nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        String id = doc["id"] | "";
        if (removeDevice(id))
          req->send(200, "application/json", "{\"ok\":true}");
        else
          req->send(404, "application/json", "{\"error\":\"not found\"}");
      });

  server.on(
      "/api/pjlink", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        String ip = doc["ip"] | "";
        String pass = doc["pass"] | "";
        String cmd = doc["cmd"] | "";
        if (!ip.length() || !cmd.length()) {
          req->send(400, "application/json",
                    "{\"error\":\"missing ip or cmd\"}");
          return;
        }
        String resp = pjlinkCmd(ip, pass, cmd);
        JsonDocument res;
        res["response"] = resp;
        String out;
        serializeJson(res, out);
        req->send(200, "application/json", out);
      });

  // Removed: duplicate /api/mdns/scan POST handler
  // The correct async version is registered above and defers to mdnsScanLoop()

  server.on(
      "/api/wol", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        String mac = doc["mac"] | "";
        sendWol(mac);
        req->send(200, "application/json", "{\"ok\":true}");
      });

  server.on(
      "/api/proxy/start", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        proxyListenPort = doc["listenPort"] | proxyListenPort;
        proxyTargetHost = (const char *)(doc["targetHost"] | "");
        proxyTargetPort = doc["targetPort"] | 0;
        proxyCaptureToLearn = doc["captureToLearn"] | false;
        proxyStart();
        req->send(200, "application/json", "{\"ok\":true}");
      });

  server.on("/api/proxy/stop", HTTP_POST, [](AsyncWebServerRequest *req) {
    proxyStop();
    req->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *req) {
    req->send(200, "application/json", "{\"ok\":true}");
    delay(200);
    ESP.restart();
  });
  server.on(
      "/api/learner", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t,
         size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        if (doc["enabled"].is<bool>())
          learnEnabled = doc["enabled"];
        if (doc["port"].is<uint16_t>())
          learnPort = doc["port"];

        // If enabling, we might want to ensure the server is restarted or
        // relevant logic applied, but for now just updating globals as
        // requested.
        req->send(200, "application/json", "{\"ok\":true}");
      });

  server.on("/api/capture/get", HTTP_GET, [](AsyncWebServerRequest *req) {
    String id = req->hasParam("id") ? req->getParam("id")->value() : "";
    JsonDocument doc;
    bool found = false;
    for (const auto &c : caps) {
      if (c.id == id) {
        doc["id"] = c.id;
        doc["ts"] = c.ts;
        doc["srcIp"] = c.srcIp;
        doc["srcPort"] = c.srcPort;
        doc["localPort"] = c.localPort;
        doc["hex"] = c.hex;
        doc["ascii"] = c.ascii;
        doc["pinned"] = c.pinned;
        doc["repeats"] = c.repeats;
        doc["lastTs"] = c.lastTs;
        doc["suffixHint"] = c.suffixHint;
        doc["payloadType"] = c.payloadType;
        found = true;
        break;
      }
    }
    if (found) {
      String out;
      serializeJson(doc, out);
      req->send(200, "application/json", out);
    } else {
      req->send(404, "application/json", "{\"error\":\"not found\"}");
    }
  });

  server.on("/api/discovery/stop", HTTP_POST, [](AsyncWebServerRequest *req) {
    discRunning = false;
    // We might want to give it a moment or rely on the loop checking the flag
    req->send(200, "application/json", "{\"ok\":true}");
  });
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.serveStatic("/rs232_pro.html", LittleFS, "/rs232_pro.html");
}
