#include "TcpServerHandler.h"
#include "Utils.h"
#include "WebAPI.h" // For wsTcpServer
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h> // Pulls in AsyncServer
#include <algorithm>

TcpServerHandler tcpServerHandler;

extern AsyncWebSocket wsTcpServer;

void TcpServerHandler::begin(uint16_t port) {
  end();
  _port = port;
  _server = new AsyncServer(_port);
  _server->setNoDelay(true);

  _server->onClient(
      [](void *arg, AsyncClient *client) {
        tcpServerHandler.handleNewClient(client);
      },
      nullptr);

  _server->begin();
  Serial.println("TCPS: Server started on port " + String(_port));
}

void TcpServerHandler::end() {
  if (_server) {
    _server->end();
    // Yield to allow any pending callbacks to fire before we delete
    delay(10);
    delete _server;
    _server = nullptr;
  }

  // Create a copy to iterate safely because close() might trigger
  // handleDisconnect() which modifies the _clients vector
  std::vector<AsyncClient *> clientsCopy = _clients;
  _clients.clear(); // Clear main vector immediately

  for (auto *c : clientsCopy) {
    if (c->connected()) {
      c->close(true);
    }
    // We don't delete c here, AsyncTCP handles that
  }
}

bool TcpServerHandler::isRunning() { return _server != nullptr; }

void TcpServerHandler::broadcast(const String &msg) {
  for (auto *c : _clients) {
    if (c->connected()) {
      c->write(msg.c_str());
    }
  }
}

void TcpServerHandler::handleNewClient(AsyncClient *client) {
  Serial.println("TCPS: New Client - " + client->remoteIP().toString());
  _clients.push_back(client);

  // Notify UI
  JsonDocument doc;
  doc["type"] = "event";
  doc["event"] = "connect";
  doc["ip"] = client->remoteIP().toString();
  String s;
  serializeJson(doc, s);
  wsTcpServer.textAll(s);

  client->onData([](void *, AsyncClient *c, void *data,
                    size_t len) { tcpServerHandler.handleData(c, data, len); },
                 nullptr);

  client->onDisconnect(
      [](void *, AsyncClient *c) { tcpServerHandler.handleDisconnect(c); },
      nullptr);
}

void TcpServerHandler::handleData(AsyncClient *client, void *data, size_t len) {
  Serial.printf("TCPS: Data len=%u\n", len);
  JsonDocument doc;
  doc["type"] = "rx";
  doc["from"] = client->remoteIP().toString();
  doc["ascii"] = bytesToAscii((uint8_t *)data, len);
  doc["hex"] = bytesToHex((uint8_t *)data, len);
  String s;
  serializeJson(doc, s);
  wsTcpServer.textAll(s);
}

void TcpServerHandler::handleDisconnect(AsyncClient *client) {
  Serial.println("TCPS: Client Disconnected");
  // Notify UI
  JsonDocument doc;
  doc["type"] = "event";
  doc["event"] = "disconnect";
  doc["ip"] = client->remoteIP().toString();
  String s;
  serializeJson(doc, s);
  wsTcpServer.textAll(s);

  // Remove from vector
  auto it = std::find(_clients.begin(), _clients.end(), client);
  if (it != _clients.end()) {
    _clients.erase(it);
  }
}
