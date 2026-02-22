#ifndef TERMINAL_HANDLER_H
#define TERMINAL_HANDLER_H

#include "AppConfig.h"
#include <ESPAsyncWebServer.h>


extern bool termConnected;
extern String termHost;
extern uint16_t termPort;

void termRequestConnect(String host, uint16_t port);
void termRequestDisconnect();
void termRequestSend(const uint8_t *data, size_t len);

// No longer needed
// void termPumpTask(void *pvParameters);

#endif
