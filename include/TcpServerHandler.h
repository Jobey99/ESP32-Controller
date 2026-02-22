#pragma once

#include <Arduino.h>
#include <vector>

// Forward declarations to avoid heavy includes in header if possible, or
// include implementation deps
class AsyncServer;
class AsyncClient;

class TcpServerHandler {
public:
  void begin(uint16_t port);
  void end();
  void broadcast(const String &msg);
  bool isRunning();
  uint16_t getPort() { return _port; }

  // Called by static callbacks
  void handleNewClient(AsyncClient *client);
  void handleData(AsyncClient *client, void *data, size_t len);
  void handleDisconnect(AsyncClient *client);

private:
  AsyncServer *_server = nullptr;
  uint16_t _port = 0;
  std::vector<AsyncClient *> _clients;
};

extern TcpServerHandler tcpServerHandler;
