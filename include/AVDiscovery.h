#ifndef AV_DISCOVERY_H
#define AV_DISCOVERY_H

#include "AppConfig.h"
#include <WiFi.h>
#include <vector>


struct DevStatus {
  String id;
  bool online = false;
  uint32_t lastSeenMs = 0;
  String lastIp;
  uint16_t lastPort = 0;
};

extern std::vector<DevStatus> devStatuses;
extern bool discRunning;
extern uint32_t discProgress;
extern std::vector<String> discFound;

void updateDevStatus(const String &id, bool online, const String &ip,
                     uint16_t port);
void deviceMonitorTask(void *pvParameters);
void startDisc();
void sendWol(const String &macStr);
String pjlinkCmd(const String &ip, const String &password, const String &cmd);

bool tcpProbe(const IPAddress &ip, uint16_t port, uint16_t timeoutMs);

#endif
