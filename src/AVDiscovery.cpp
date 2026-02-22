#include "AVDiscovery.h"
#include "ConfigManager.h"
#include "Utils.h"
#include "WiFiHelper.h"
#include <ArduinoJson.h>
#include <MD5Builder.h>
#include <WiFiUdp.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>

std::vector<DevStatus> devStatuses;
bool discRunning = false;
uint32_t discProgress = 0;
uint32_t discStartedMs = 0;
std::vector<String> discFound;

static String discSubnetBase = "";
static uint8_t discFrom = 1;
static uint8_t discTo = 254;
static std::vector<uint16_t> discPorts;

static const uint16_t pjlinkPort = 4352;

struct Suggest {
  String fingerprint;
  String templateId;
  String suffix;
  uint16_t bestPort = 0;
  String nameHint;
};

static String getMacFromArp(const IPAddress &ip) {
  ip4_addr_t i;
  i.addr = ip;
  struct netif *netif = netif_list;
  while (netif) {
    eth_addr *eth_ret;
    const ip4_addr_t *ip_ret;
    if (etharp_find_addr(netif, &i, &eth_ret, &ip_ret) != -1) {
      char buf[20];
      snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
               eth_ret->addr[0], eth_ret->addr[1], eth_ret->addr[2],
               eth_ret->addr[3], eth_ret->addr[4], eth_ret->addr[5]);
      return String(buf);
    }
    netif = netif->next;
  }
  return "";
}

bool tcpProbe(const IPAddress &ip, uint16_t port, uint16_t timeoutMs) {
  WiFiClient c;
  bool ok = c.connect(ip, port, timeoutMs);
  if (ok)
    c.stop();
  return ok;
}

static bool httpBanner(const IPAddress &ip, uint16_t port, String &outBanner) {
  WiFiClient c;
  if (!c.connect(ip, port, 220))
    return false;
  c.print("GET / HTTP/1.0\r\nHost: x\r\nUser-Agent: esp32-av-tool\r\n\r\n");
  uint32_t t0 = millis();
  String head;
  while (millis() - t0 < 260) {
    while (c.available()) {
      char ch = (char)c.read();
      head += ch;
      if (head.length() > 900)
        break;
    }
    if (head.indexOf("\r\n\r\n") >= 0 || head.length() > 900)
      break;
    delay(2);
  }
  c.stop();

  String serverLine = "";
  int si = head.indexOf("Server:");
  if (si >= 0) {
    int e = head.indexOf("\r\n", si);
    if (e > si)
      serverLine = head.substring(si + 7, e);
    serverLine.trim();
  }

  outBanner = "";
  if (serverLine.length())
    outBanner += serverLine + " ";

  if (head.indexOf("Samsung") >= 0)
    outBanner += "Samsung ";
  if (head.indexOf("Extron") >= 0 || serverLine.indexOf("Extron") >= 0)
    outBanner += "Extron ";
  if (head.indexOf("Kramer") >= 0 || serverLine.indexOf("Kramer") >= 0)
    outBanner += "Kramer ";
  if (head.indexOf("Lightware") >= 0 || serverLine.indexOf("Lightware") >= 0)
    outBanner += "Lightware ";
  if (head.indexOf("AMX") >= 0 || serverLine.indexOf("AMX") >= 0)
    outBanner += "AMX ";
  if (head.indexOf("Crestron") >= 0 || serverLine.indexOf("Crestron") >= 0)
    outBanner += "Crestron ";

  outBanner.trim();
  return (outBanner.length() > 0 || serverLine.length() > 0);
}

static bool telnetBanner(const IPAddress &ip, uint16_t port, String &outText,
                         bool doKramerProbe) {
  WiFiClient c;
  if (!c.connect(ip, port, 200))
    return false;
  c.setTimeout(1);
  uint32_t t0 = millis();
  uint8_t buf[512];
  size_t got = 0;
  while (millis() - t0 < 220 && got < sizeof(buf)) {
    int a = c.available();
    if (a > 0) {
      int n = c.read(buf + got, min((int)(sizeof(buf) - got), a));
      if (n > 0)
        got += (size_t)n;
    } else {
      delay(2);
    }
  }
  if (doKramerProbe) {
    c.print("#MODEL?\r\n");
    uint32_t t1 = millis();
    while (millis() - t1 < 260 && got < sizeof(buf)) {
      int a = c.available();
      if (a > 0) {
        int n = c.read(buf + got, min((int)(sizeof(buf) - got), a));
        if (n > 0)
          got += (size_t)n;
      } else {
        delay(2);
      }
    }
  }
  c.stop();
  if (!got)
    return false;
  String s = stripTelnetIAC(buf, got);
  s.trim();
  if (!s.length())
    return false;
  outText = s;
  return true;
}

static Suggest makeSuggestion(const String &banner,
                              const std::vector<uint16_t> &openPorts) {
  Suggest s;
  String b = banner;
  b.toLowerCase();
  if (std::find(openPorts.begin(), openPorts.end(), (uint16_t)1515) !=
      openPorts.end()) {
    s.templateId = "TPL_SAMSUNG_MDC_EXAMPLE";
    s.suffix = "";
    s.bestPort = 1515;
    s.nameHint = "Samsung Display (MDC)";
  }
  if (b.indexOf("protocol 3000") >= 0 || b.indexOf("kramer") >= 0) {
    s.templateId = "TPL_KRAMER_P3000";
    s.suffix = "\\r\\n";
    if (!s.bestPort)
      s.bestPort = 5000;
    s.nameHint = "Kramer (P3000)";
  } else if (b.indexOf("extron") >= 0) {
    s.templateId = "TPL_EXTRON_TELNET";
    s.suffix = "\\r";
    if (!s.bestPort)
      s.bestPort = 23;
    s.nameHint = "Extron (Telnet)";
  } else if (b.indexOf("lightware") >= 0) {
    s.templateId = "TPL_LIGHTWARE_LW3";
    s.suffix = "\\r\\n";
    if (!s.bestPort)
      s.bestPort = 6100;
    s.nameHint = "Lightware";
  } else if (b.indexOf("amx") >= 0) {
    s.templateId = "";
    s.suffix = "\\r";
    if (!s.bestPort)
      s.bestPort = 23;
    s.nameHint = "AMX";
  } else if (b.indexOf("crestron") >= 0) {
    s.templateId = "";
    s.suffix = "\\r";
    if (!s.bestPort)
      s.bestPort = 41794;
    s.nameHint = "Crestron";
  }
  if (!s.bestPort) {
    for (auto p : openPorts) {
      if (p == 23 || p == 5000 || p == 6100 || p == 1515) {
        s.bestPort = p;
        break;
      }
    }
    if (!s.bestPort && !openPorts.empty())
      s.bestPort = openPorts[0];
  }
  s.fingerprint = banner;
  return s;
}

static void discTask(void *) {
  discRunning = true;
  discStartedMs = millis();
  discProgress = 0;
  discFound.clear();
  const uint16_t timeoutMs = 120;
  for (uint16_t host = discFrom; host <= discTo; host++) {
    if (!discRunning)
      break;
    IPAddress ip;
    ip.fromString(discSubnetBase + "." + String(host));

    // Check all ports
    std::vector<uint16_t> openPorts;
    bool alive = false;
    for (auto p : discPorts) {
      if (!discRunning)
        break;
      if (tcpProbe(ip, p, timeoutMs)) {
        alive = true;
        openPorts.push_back(p);
      }
      vTaskDelay(2 / portTICK_PERIOD_MS);
    }

    if (alive) {
      String banner = "";
      String tmp;
      bool didTel = false;
      for (auto p : openPorts) {
        if (p == 23 || p == 5000 || p == 6100) {
          bool kprobe = (p == 5000);
          if (telnetBanner(ip, p, tmp, kprobe)) {
            banner = tmp;
            didTel = true;
            break;
          }
        }
      }
      if (!didTel && std::find(openPorts.begin(), openPorts.end(),
                               (uint16_t)80) != openPorts.end()) {
        if (httpBanner(ip, 80, tmp))
          banner = tmp;
      }
      Suggest sug = makeSuggestion(banner, openPorts);
      JsonDocument row;
      row["ip"] = ip.toString();
      JsonArray open = row["openPorts"].to<JsonArray>();
      for (auto p : openPorts)
        open.add(p);
      row["fingerprint"] = sug.fingerprint;
      row["suggestedTemplateId"] = sug.templateId;
      row["suggestedSuffix"] = sug.suffix;
      row["suggestedPort"] = sug.bestPort;
      row["nameHint"] = sug.nameHint;
      String mac = getMacFromArp(ip);
      if (mac.length())
        row["mac"] = mac;
      row["seenMs"] = millis();
      String out;
      serializeJson(row, out);
      discFound.push_back(out);
      wsTextAll(wsDisc, out);
    }
    discProgress++;
    vTaskDelay(3 / portTICK_PERIOD_MS);
  }
  discRunning = false;
  wsTextAll(wsDisc, R"({"type":"done"})");
  vTaskDelete(nullptr);
}

void startDisc() {
  if (discRunning)
    return;
  discPorts = {23, 80, 443, 8080, 5000, 6100, 1515, 4352, 41794};
  IPAddress myIp = WiFi.localIP();
  discSubnetBase =
      String(myIp[0]) + "." + String(myIp[1]) + "." + String(myIp[2]);
  discFrom = 1;
  discTo = 254;
  xTaskCreate(discTask, "discTask", 5000, nullptr, 1, nullptr);
}

void updateDevStatus(const String &id, bool online, const String &ip,
                     uint16_t port) {
  DevStatus *found = nullptr;
  for (auto &s : devStatuses) {
    if (s.id == id) {
      found = &s;
      break;
    }
  }
  if (!found) {
    DevStatus ns;
    ns.id = id;
    devStatuses.push_back(ns);
    found = &devStatuses.back();
  }
  found->online = online;
  if (online)
    found->lastSeenMs = millis();
  found->lastIp = ip;
  found->lastPort = port;
}

void deviceMonitorTask(void *) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      JsonDocument doc;
      if (!deserializeJson(doc, cfgJson)) {
        if (doc["devices"].is<JsonArray>()) {
          for (JsonObject d : doc["devices"].as<JsonArray>()) {
            String id = d["id"] | "";
            String ip = d["ip"] | "";
            uint16_t port = d["portHint"] | 0;
            if (!id.length() || !ip.length() || port == 0)
              continue;
            IPAddress ipa;
            if (!ipa.fromString(ip))
              continue;
            bool ok = tcpProbe(ipa, port, 120);
            updateDevStatus(id, ok, ip, port);
            vTaskDelay(10 / portTICK_PERIOD_MS);
          }
        }
      }
    }
    vTaskDelay(8000 / portTICK_PERIOD_MS);
  }
}

void sendWol(const String &macStr) {
  uint8_t mac[6];
  int v[6];
  if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3],
             &v[4], &v[5]) != 6)
    return;
  for (int i = 0; i < 6; i++)
    mac[i] = (uint8_t)v[i];
  WiFiUDP udp;
  udp.beginPacket(IPAddress(255, 255, 255, 255), 9);
  for (int i = 0; i < 6; i++)
    udp.write(0xFF);
  for (int i = 0; i < 16; i++)
    udp.write(mac, 6);
  udp.endPacket();
  logAll("WoL sent to " + macStr);
}

String pjlinkCmd(const String &ip, const String &password, const String &cmd) {
  WiFiClient client;
  if (!client.connect(ip.c_str(), pjlinkPort))
    return "ERROR: Connect failed";
  String banner = client.readStringUntil('\r');
  if (!banner.startsWith("PJLINK ")) {
    client.stop();
    return "ERROR: Invalid banner: " + banner;
  }
  bool auth = banner.startsWith("PJLINK 1");
  String prefix = "";
  if (auth) {
    if (password.length() == 0) {
      client.stop();
      return "ERROR: Password required";
    }
    int sp = banner.indexOf(' ');
    if (sp > 0) {
      String salt = banner.substring(sp + 1);
      salt.trim();
      MD5Builder md5;
      md5.begin();
      md5.add(salt);
      md5.add(password);
      md5.calculate();
      prefix = md5.toString();
    }
  }
  String full = prefix + cmd + "\r";
  client.print(full);
  String resp = client.readStringUntil('\r');
  client.stop();
  return resp;
}
