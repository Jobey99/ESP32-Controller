#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>


// Global WebSocket for RS232
extern AsyncWebSocket wsRS232;
extern WiFiServer rs232TelnetServer;

// Initialize Serial2 and WebSocket handlers
void rs232Setup();

// Handle main loop tasks (reading Serial2, autobaud, loopback)
void rs232Loop();

// Helper to send data to Serial2
void rs232Send(const String &data, bool hex, const String &suffix);

// Helper to change baud rate
void rs232SetBaud(uint32_t baud);

// Advanced Features
void rs232SetInvert(bool invert);
void rs232StartAutoBaud();
void rs232StopAutoBaud();
void rs232StartLoopback();
void rs232SetProfile(int idx);
void rs232TriggerPreset(int n);
