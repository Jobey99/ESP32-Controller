#pragma once

#include "WebAPI.h" // For wsUdp access if needed, or we pass it in
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>


class UdpHandler {
public:
  void begin(uint16_t port);
  void loop();
  void send(String ipStr, uint16_t port, String data);
  void setListenPort(uint16_t port);
  uint16_t getListenPort() { return _port; }

private:
  WiFiUDP _udp;
  uint16_t _port = 5000;
  bool _running = false;
  uint8_t _packetBuffer[2048]; // Buffer for incoming packets
};

extern UdpHandler udpHandler;
