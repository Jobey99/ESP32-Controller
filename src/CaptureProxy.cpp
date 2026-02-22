#include "CaptureProxy.h"
#include "Utils.h"
#include <ArduinoJson.h>


std::vector<Capture> caps;
static const size_t MAX_CAPS = 160;

uint16_t learnPort = 5000;
bool learnEnabled = true;
static AsyncServer *learnServer = nullptr;

bool proxyRunning = false;
bool proxyCaptureToLearn = false;
uint16_t proxyListenPort = 23001;
String proxyTargetHost = "";
uint16_t proxyTargetPort = 0;
static AsyncServer *proxyServer = nullptr;

struct ProxyPair {
  AsyncClient *inClient = nullptr;
  AsyncClient *outClient = nullptr;
};
static ProxyPair proxyPair;

void addCapture(const String &srcIp, uint16_t srcPort, uint16_t localPort,
                const uint8_t *data, size_t len) {
  Capture c;
  c.id = genId();
  c.ts = millis();
  c.lastTs = c.ts;
  c.srcIp = srcIp;
  c.srcPort = srcPort;
  c.localPort = localPort;
  c.hex = bytesToHex(data, len);
  c.ascii = bytesToAscii(data, len);
  c.suffixHint = detectSuffix(data, len);

  int textCount = 0;
  for (size_t i = 0; i < len; i++) {
    if (data[i] >= 32 && data[i] <= 126)
      textCount++;
    else if (data[i] == '\r' || data[i] == '\n' || data[i] == '\t')
      textCount++;
  }
  c.payloadType = (len > 0 && (float)textCount / len > 0.85) ? "ascii" : "hex";

  c.hash = simpleHash(c.srcIp + ":" + String(c.srcPort) + "|" + c.hex);

  if (!caps.empty()) {
    Capture &last = caps.back();
    if (last.hash == c.hash && (c.ts - last.lastTs) < 1500) {
      last.repeats++;
      last.lastTs = c.ts;
      return;
    }
  }

  if (caps.size() >= MAX_CAPS)
    caps.erase(caps.begin());
  caps.push_back(c);
}

void stopLearn() {
  if (learnServer) {
    learnServer->end();
    delete learnServer;
    learnServer = nullptr;
  }
}

void startLearn() {
  stopLearn();
  if (!learnEnabled)
    return;

  learnServer = new AsyncServer(learnPort);
  learnServer->onClient(
      [](void *, AsyncClient *client) {
        client->onData(
            [](void *, AsyncClient *c, void *data, size_t len) {
              addCapture(c->remoteIP().toString(), c->remotePort(),
                         c->localPort(), (uint8_t *)data, len);
            },
            nullptr);
      },
      nullptr);

  learnServer->begin();
  logAll("Learner TCP listening on port " + String(learnPort));
}

bool getCaptureById(const String &id, Capture &out) {
  for (auto &c : caps)
    if (c.id == id) {
      out = c;
      return true;
    }
  return false;
}

void proxyStop() {
  proxyRunning = false;

  if (proxyPair.inClient) {
    proxyPair.inClient->close(true);
    proxyPair.inClient = nullptr;
  }
  if (proxyPair.outClient) {
    proxyPair.outClient->close(true);
    proxyPair.outClient = nullptr;
  }

  if (proxyServer) {
    proxyServer->end();
    delete proxyServer;
    proxyServer = nullptr;
  }
  wsTextAll(wsProxy, R"({"type":"status","running":false})");
}

static void proxyLog(const char *dir, const uint8_t *data, size_t len) {
  JsonDocument d;
  d["type"] = "data";
  d["dir"] = dir;
  d["hex"] = bytesToHex(data, len);
  d["ascii"] = bytesToAscii(data, len);
  String s;
  serializeJson(d, s);
  wsTextAll(wsProxy, s);

  if (proxyCaptureToLearn) {
    String src = String("PROXY ") + String(dir);
    addCapture(src, 0, proxyListenPort, data, len);
  }
}

void proxyStart() {
  proxyStop();

  if (!proxyTargetHost.length() || proxyTargetPort == 0 ||
      proxyListenPort == 0) {
    wsTextAll(wsProxy,
              R"({"type":"error","msg":"Missing target or listen port"})");
    return;
  }

  proxyServer = new AsyncServer(proxyListenPort);

  proxyServer->onClient(
      [](void *, AsyncClient *inClient) {
        if (proxyPair.inClient) {
          inClient->close(true);
          return;
        }
        proxyPair.inClient = inClient;

        proxyPair.outClient = new AsyncClient();
        AsyncClient *out = proxyPair.outClient;

        out->onConnect(
            [](void *, AsyncClient *) {
              wsTextAll(wsProxy,
                        R"({"type":"status","running":true,"connected":true})");
            },
            nullptr);

        out->onError(
            [](void *, AsyncClient *, int8_t err) {
              JsonDocument d;
              d["type"] = "error";
              d["msg"] = String("Target connect error ") + String(err);
              String s;
              serializeJson(d, s);
              wsTextAll(wsProxy, s);
              proxyStop();
            },
            nullptr);

        out->onDisconnect(
            [](void *, AsyncClient *) {
              wsTextAll(
                  wsProxy,
                  R"({"type":"status","running":true,"connected":false})");
              proxyStop();
            },
            nullptr);

        out->onData(
            [](void *, AsyncClient *, void *data, size_t len) {
              if (!proxyPair.inClient)
                return;
              proxyPair.inClient->write((const char *)data, len);
              proxyLog("RX(target->client)", (uint8_t *)data, len);
            },
            nullptr);

        inClient->onData(
            [](void *, AsyncClient *, void *data, size_t len) {
              if (!proxyPair.outClient)
                return;
              proxyPair.outClient->write((const char *)data, len);
              proxyLog("TX(client->target)", (uint8_t *)data, len);
            },
            nullptr);

        inClient->onDisconnect([](void *, AsyncClient *) { proxyStop(); },
                               nullptr);

        IPAddress ip;
        if (!ip.fromString(proxyTargetHost)) {
          IPAddress resolved;
          if (WiFi.hostByName(proxyTargetHost.c_str(), resolved) != 1) {
            wsTextAll(wsProxy,
                      R"({"type":"error","msg":"DNS failed for target"})");
            proxyStop();
            return;
          }
          ip = resolved;
        }

        out->connect(ip, proxyTargetPort);
      },
      nullptr);

  proxyServer->begin();
  proxyRunning = true;

  JsonDocument st;
  st["type"] = "status";
  st["running"] = true;
  st["connected"] = false;
  st["listenPort"] = proxyListenPort;
  st["targetHost"] = proxyTargetHost;
  st["targetPort"] = proxyTargetPort;
  st["captureToLearn"] = proxyCaptureToLearn;
  String s;
  serializeJson(st, s);
  wsTextAll(wsProxy, s);

  logAll("Proxy listening :" + String(proxyListenPort) + " -> " +
         proxyTargetHost + ":" + String(proxyTargetPort));
}
