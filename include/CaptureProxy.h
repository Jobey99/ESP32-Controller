#ifndef CAPTURE_PROXY_H
#define CAPTURE_PROXY_H

#include "AppConfig.h"
#include <vector>

struct Capture {
  String id;
  uint32_t ts;
  String srcIp;
  uint16_t srcPort;
  uint16_t localPort;
  String hex;
  String ascii;
  bool pinned = false;
  uint32_t repeats = 1;
  uint32_t lastTs;
  String hash;
  String suffixHint;
  String payloadType; // "ascii" or "hex"
};

extern std::vector<Capture> caps;
extern uint16_t learnPort;
extern bool learnEnabled;

extern bool proxyRunning;
extern bool proxyCaptureToLearn;
extern uint16_t proxyListenPort;
extern String proxyTargetHost;
extern uint16_t proxyTargetPort;

void startLearn();
void stopLearn();
void addCapture(const String &srcIp, uint16_t srcPort, uint16_t localPort,
                const uint8_t *data, size_t len);
bool getCaptureById(const String &id, Capture &out);

void proxyStart();
void proxyStop();

#endif
