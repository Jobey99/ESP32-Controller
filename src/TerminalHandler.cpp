#include "TerminalHandler.h"
#include "Utils.h"
#include "WebAPI.h" // For wsTerm
#include <ArduinoJson.h>

static AsyncClient *termClient = nullptr;
bool termConnected = false;
String termHost = "";
uint16_t termPort = 0;

void termSendStatus() {
  JsonDocument d;
  d["type"] = "status";
  d["connected"] = termConnected;
  d["host"] = termHost;
  d["port"] = termPort;
  String s;
  serializeJson(d, s);
  wsTerm.textAll(s);
}

static void _onData(void *arg, AsyncClient *c, void *data, size_t len) {
  JsonDocument d;
  d["type"] = "rx";
  d["hex"] = bytesToHex((uint8_t *)data, len);
  d["ascii"] = bytesToAscii((uint8_t *)data, len);
  String s;
  serializeJson(d, s);
  wsTerm.textAll(s);
}

static void _onConnect(void *arg, AsyncClient *c) {
  termConnected = true;
  termSendStatus();
  JsonDocument d;
  d["type"] = "log";
  d["msg"] = "TCP Connected";
  String s;
  serializeJson(d, s);
  wsTerm.textAll(s);
}

static void _onDisconnect(void *arg, AsyncClient *c) {
  termConnected = false;
  termSendStatus();
  termClient = nullptr;
  delete c;
}

static void _onError(void *arg, AsyncClient *c, int8_t error) {
  termConnected = false;
  termSendStatus();
  JsonDocument d;
  d["type"] = "error";
  d["msg"] = String("TCP Error: ") + error;
  String s;
  serializeJson(d, s);
  wsTerm.textAll(s);
}

void termRequestConnect(String host, uint16_t port) {
  Serial.println("TERM: Connect Request");
  if (termClient) {
    Serial.println("TERM: Closing existing");
    if (termClient->connected())
      termClient->close(true);
    delete termClient;
    termClient = nullptr;
  }

  termHost = host;
  termPort = port;
  termConnected = false;

  Serial.println("TERM: Allocating AsyncClient");
  termClient = new AsyncClient();
  if (!termClient) {
    Serial.println("TERM: Allocation failed!");
    return;
  }

  termClient->onConnect(_onConnect, nullptr);
  termClient->onDisconnect(_onDisconnect, nullptr);
  termClient->onData(_onData, nullptr);
  termClient->onError(_onError, nullptr);

  // Try parsing IP to avoid DNS lookup if possible
  IPAddress ip;
  bool isIp = ip.fromString(host);

  Serial.print("TERM: Connecting to ");
  Serial.print(host);
  Serial.print(":");
  Serial.println(port);

  bool result = false;
  const int maxRetries = 3;

  for (int attempt = 1; attempt <= maxRetries && !result; attempt++) {
    if (attempt > 1) {
      delay(500); // Brief delay between retries
      Serial.printf("TERM: Retry attempt %d/%d\n", attempt, maxRetries);
    }

    if (isIp) {
      result = termClient->connect(ip, port);
    } else {
      result = termClient->connect(host.c_str(), port);
    }
  }

  if (!result) {
    Serial.println("TERM: Connect failed after retries");
    JsonDocument d;
    d["type"] = "error";
    d["msg"] = "Connect failed after " + String(maxRetries) + " attempts";
    String s;
    serializeJson(d, s);
    wsTerm.textAll(s);
    delete termClient;
    termClient = nullptr;
  } else {
    Serial.println("TERM: Connect initiated");
    JsonDocument d;
    d["type"] = "log";
    d["msg"] = "Connecting to " + host + ":" + String(port) + "...";
    String s;
    serializeJson(d, s);
    wsTerm.textAll(s);
  }
}

void termRequestDisconnect() {
  if (termClient) {
    termClient->close(true);
  }
}

void termRequestSend(const uint8_t *data, size_t len) {
  if (termClient && termClient->connected()) {
    if (termClient->space() > len) {
      termClient->write((const char *)data, len);
    } else {
      // buffer full
      wsTerm.textAll("{\"type\":\"error\",\"msg\":\"TX Buffer Full\"}");
    }
  } else {
    wsTerm.textAll("{\"type\":\"error\",\"msg\":\"Not connected\"}");
  }
}
