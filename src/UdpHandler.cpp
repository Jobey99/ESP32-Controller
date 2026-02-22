#include "UdpHandler.h"
#include "Utils.h"

UdpHandler udpHandler;

// Extern WebSockets from WebAPI.cpp to send RX data
extern AsyncWebSocket wsUdp;

void UdpHandler::begin(uint16_t port) {
  _port = port;
  if (_running)
    _udp.stop();
  if (_udp.begin(_port)) {
    _running = true;
  }
}

void UdpHandler::setListenPort(uint16_t port) {
  if (port != _port) {
    begin(port);
  }
}

void UdpHandler::send(String ipStr, uint16_t port, String data) {
  IPAddress ip;
  if (ip.fromString(ipStr)) {
    _udp.beginPacket(ip, port);
    _udp.print(data);
    _udp.endPacket();
  }
}

void UdpHandler::loop() {
  if (!_running)
    return;

  int packetSize = _udp.parsePacket();
  if (packetSize) {
    int len = _udp.read(_packetBuffer, sizeof(_packetBuffer) - 1);
    if (len > 0) {
      _packetBuffer[len] = 0;

      JsonDocument doc;
      doc["type"] = "rx";
      doc["from"] = _udp.remoteIP().toString();
      doc["port"] = _udp.remotePort();
      doc["ascii"] = bytesToAscii(_packetBuffer, len);
      doc["hex"] = bytesToHex(_packetBuffer, len);

      String out;
      serializeJson(doc, out);
      wsUdp.textAll(out);
    }
  }
}
