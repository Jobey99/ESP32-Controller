#include "RS232Handler.h"
#include "Utils.h"
#include "WebAPI.h" // Need this for extern wsRS232
#include <ArduinoJson.h>


// AsyncWebSocket wsRS232("/wsrs232"); // Moved to WebAPI.cpp
WiFiServer rs232TelnetServer(23);
WiFiClient rs232TelnetClient;
bool rs232TelnetConnected = false;

// State
static uint32_t currentBaud = 9600;
static bool invertPolarity = false;

// Auto-baud state
static bool autoDetectRunning = false;
static int autoDetectIndex = 0;
static const uint32_t baudOptions[] = {300,   1200,  2400,  4800,  9600,
                                       19200, 38400, 57600, 74880, 115200};
static const int baudOptionsCount = 10;
static unsigned long autoDetectBaudStart = 0;
static int autoDetectGoodBytes = 0;
static int autoDetectScore = 0;

// Loopback state
static bool loopbackRunning = false;
static unsigned long loopbackStart = 0;
static String loopbackBuffer = "";
static const char *loopbackExpect = "ESP_LOOPBACK_TEST";

// Profiles
struct Profile {
  const char *name;
  uint32_t baud;
  const char *p1;
  const char *p2;
  const char *p3;
};

static const Profile profiles[] = {
    {"Generic", 9600, "PWR ON\r", "PWR OFF\r", "STATUS?\r"},
    {"Extron", 9600, "1*\r", "1%\r", "Q\r"},
    {"Blustream", 9600, "PWR ON\r", "PWR OFF\r", "VOL 50\r"},
    {"Kramer", 115200, "#POWER-MODE 1\r", "#POWER-MODE 0\r", "#POWER-MODE?\r"}};
static int currentProfile = 0;

void rs232SendStatus() {
  JsonDocument doc;
  doc["type"] = "status";
  doc["baud"] = currentBaud;
  doc["invert"] = invertPolarity;
  doc["auto"] = autoDetectRunning;
  doc["loop"] = loopbackRunning;
  doc["loop"] = loopbackRunning;
  doc["profile"] = currentProfile;
  doc["telnet"] = rs232TelnetConnected;
  if (rs232TelnetConnected)
    doc["telnetIP"] = rs232TelnetClient.remoteIP().toString();
  String out;
  serializeJson(doc, out);
  wsRS232.textAll(out);
}

void rs232BroadcastSys(const String &msg) {
  JsonDocument doc;
  doc["type"] = "sys";
  doc["msg"] = msg;
  String out;
  serializeJson(doc, out);
  wsRS232.textAll(out);
}

void rs232SetBaud(uint32_t baud) {
  if (baud == currentBaud)
    return;
  currentBaud = baud;
  Serial2.end();
  delay(10);
  Serial2.begin(currentBaud);
  // Re-configure pins if needed, but usually .begin handles it for default pins
  rs232SendStatus();
  rs232BroadcastSys("Baud changed to " + String(currentBaud));
}

void rs232SetInvert(bool invert) {
  invertPolarity = invert;
  rs232SendStatus();
  rs232BroadcastSys(String("Invert Polarity: ") + (invert ? "ON" : "OFF"));
}

void rs232SetProfile(int idx) {
  if (idx >= 0 && idx < 3) {
    currentProfile = idx;
    rs232SetBaud(profiles[idx].baud);
    rs232BroadcastSys(String("Profile set: ") + profiles[idx].name);
  }
}

void rs232TriggerPreset(int n) {
  if (currentProfile < 0 || currentProfile > 2)
    return;
  String cmd = "";
  if (n == 1)
    cmd = profiles[currentProfile].p1;
  if (n == 2)
    cmd = profiles[currentProfile].p2;
  if (n == 3)
    cmd = profiles[currentProfile].p3;

  if (cmd.length()) {
    // Basic interpretation of escaped chars in presets
    cmd.replace("\\r", "\r");
    cmd.replace("\\n", "\n");
    rs232Send(cmd, false, "");
  }
}

void rs232StartLoopback() {
  loopbackRunning = true;
  loopbackStart = millis();
  loopbackBuffer = "";
  rs232BroadcastSys("Loopback test started...");
  rs232Send(loopbackExpect, false, "\\r\\n");
  rs232SendStatus();
}

void rs232StartAutoBaud() {
  autoDetectRunning = true;
  autoDetectIndex = baudOptionsCount - 1; // start highest
  autoDetectGoodBytes = 0;
  autoDetectScore = 0;
  rs232SetBaud(baudOptions[autoDetectIndex]);
  autoDetectBaudStart = millis();
  rs232BroadcastSys("Auto-baud scan started...");
  rs232SendStatus();
}

void rs232StopAutoBaud() {
  autoDetectRunning = false;
  rs232BroadcastSys("Auto-baud stopped");
  rs232SendStatus();
}

void rs232Send(const String &payload, bool hex, const String &suffix) {
  std::vector<uint8_t> data;
  if (hex) {
    parseHexBytes(payload, data);
  } else {
    String full = payload;
    if (suffix == "\\r")
      full += "\r";
    else if (suffix == "\\n")
      full += "\n";
    else if (suffix == "\\r\\n")
      full += "\r\n";
    for (size_t i = 0; i < full.length(); i++)
      data.push_back((uint8_t)full[i]);
  }

  if (invertPolarity) {
    for (size_t i = 0; i < data.size(); i++)
      data[i] = ~data[i];
  }

  Serial2.write(data.data(), data.size());

  // Restore for display (un-invert)
  std::vector<uint8_t> displayData = data;
  if (invertPolarity) {
    for (size_t i = 0; i < displayData.size(); i++)
      displayData[i] = ~displayData[i];
  }

  if (rs232TelnetConnected && rs232TelnetClient.connected()) {
    // For telnet, we usually send the "ASCII" version (un-inverted) as it's a
    // network terminal But if we want it to act EXACTLY like the port...
    // Standard practice: Telnet is a "terminal view", so send the clean data
    // (displayData). Users connecting via putty don't want inverted garbage.
    rs232TelnetClient.write(displayData.data(), displayData.size());
  }

  JsonDocument doc;
  doc["type"] = "tx";
  doc["hex"] = bytesToHex(displayData.data(), displayData.size());
  doc["ascii"] = bytesToAscii(displayData.data(), displayData.size());
  String out;
  serializeJson(doc, out);
  wsRS232.textAll(out);
}

void rs232Setup() {
  Serial2.begin(currentBaud);
  rs232TelnetServer.begin();
  rs232TelnetServer.setNoDelay(true);

  wsRS232.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client,
                     AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      rs232SendStatus();
    } else if (type == WS_EVT_DATA) {
      JsonDocument doc;
      if (deserializeJson(doc, data, len))
        return;

      String action = doc["action"] | "";
      if (action == "baud")
        rs232SetBaud(doc["baud"] | 9600);
      else if (action == "invert")
        rs232SetInvert(doc["val"]);
      else if (action == "profile")
        rs232SetProfile(doc["id"]);
      else if (action == "preset")
        rs232TriggerPreset(doc["n"]);
      else if (action == "autobaud") {
        if (doc["start"])
          rs232StartAutoBaud();
        else
          rs232StopAutoBaud();
      } else if (action == "loopback")
        rs232StartLoopback();
      else if (action == "send") {
        rs232Send(doc["data"] | "", doc["mode"] == "hex", doc["suffix"] | "");
      }
    }
  });
}

void rs232Loop() {
  // 0. Telnet Client Management
  if (rs232TelnetServer.hasClient()) {
    if (!rs232TelnetConnected || !rs232TelnetClient.connected()) {
      if (rs232TelnetClient)
        rs232TelnetClient.stop();
      rs232TelnetClient = rs232TelnetServer.available();
      rs232TelnetConnected = true;
      rs232BroadcastSys("Telnet connected: " +
                        rs232TelnetClient.remoteIP().toString());
      rs232SendStatus();
    } else {
      // Busy
      WiFiClient busy = rs232TelnetServer.available();
      busy.println("ESP RS232 Port Busy");
      busy.stop();
    }
  }

  if (rs232TelnetConnected) {
    if (!rs232TelnetClient.connected()) {
      rs232TelnetConnected = false;
      rs232BroadcastSys("Telnet disconnected");
      rs232SendStatus();
    } else {
      // Read from Telnet -> Serial2
      while (rs232TelnetClient.available()) {
        uint8_t tbuf[64];
        int n = rs232TelnetClient.read(tbuf, sizeof(tbuf));
        if (n > 0) {
          std::vector<uint8_t> v;
          for (int i = 0; i < n; i++)
            v.push_back(tbuf[i]);

          // We won't re-invert "Send" here because we treat Telnet input as
          // "clean ASCII/bytes" The rs232Send logic will apply inversion if
          // needed. But we can't call rs232Send easily because it broadcasts
          // JSON TX events loopback. Let's manually write to Serial2 and
          // broadcast TX event.

          std::vector<uint8_t> inverted = v;
          if (invertPolarity) {
            for (size_t i = 0; i < inverted.size(); i++)
              inverted[i] = ~inverted[i];
          }
          Serial2.write(inverted.data(), inverted.size());

          JsonDocument doc;
          doc["type"] = "tx";
          doc["hex"] = bytesToHex(v.data(), v.size()); // Show clean data to UI
          doc["ascii"] = bytesToAscii(v.data(), v.size());
          String out;
          serializeJson(doc, out);
          wsRS232.textAll(out);
        }
      }
    }
  }

  // 1. Auto-baud logic
  if (autoDetectRunning) {
    int available = Serial2.available();
    while (available--) {
      uint8_t b = Serial2.read();
      if (invertPolarity)
        b = ~b;
      if ((b >= 0x20 && b <= 0x7E) || b == '\r' || b == '\n') {
        autoDetectGoodBytes++;
      }
    }

    if (millis() - autoDetectBaudStart > 400) {
      // Check score
      if (autoDetectGoodBytes >= 5) {
        autoDetectScore++;
        rs232BroadcastSys(
            "Auto-baud check: good bytes=" + String(autoDetectGoodBytes) +
            " score=" + String(autoDetectScore));
        if (autoDetectScore >= 3) {
          rs232StopAutoBaud();
          rs232BroadcastSys("Auto-baud detected: " + String(currentBaud));
        }
      } else {
        // Next baud
        autoDetectScore = 0;
        autoDetectIndex--;
        if (autoDetectIndex < 0)
          autoDetectIndex = baudOptionsCount - 1;
        rs232SetBaud(baudOptions[autoDetectIndex]);
        autoDetectBaudStart = millis();
        rs232BroadcastSys("Scanning " + String(currentBaud) + "...");
      }
      autoDetectGoodBytes = 0;
      autoDetectBaudStart = millis(); // Reset window
    }
    return; // Skip normal RX processing during scan
  }

  // 2. Normal RX
  static uint8_t buf[256];
  if (Serial2.available()) {
    int n = Serial2.read(buf, sizeof(buf));
    if (n > 0) {
      if (invertPolarity) {
        for (int i = 0; i < n; i++)
          buf[i] = ~buf[i];
      }

      // Loopback logic
      if (loopbackRunning) {
        for (int i = 0; i < n; i++)
          loopbackBuffer += (char)buf[i];
        if (loopbackBuffer.indexOf(loopbackExpect) >= 0) {
          loopbackRunning = false;
          rs232BroadcastSys("Loopback PASS");
          rs232SendStatus();
        } else if (millis() - loopbackStart > 2000) {
          loopbackRunning = false;
          rs232BroadcastSys("Loopback FAIL (Timeout)");
          rs232SendStatus();
        }
      }

      // Restore for display/telnet (un-invert)
      uint8_t dispBuf[256];
      memcpy(dispBuf, buf, n);
      if (invertPolarity) {
        for (int i = 0; i < n; i++)
          dispBuf[i] = ~dispBuf[i];
      }

      if (rs232TelnetConnected && rs232TelnetClient.connected()) {
        rs232TelnetClient.write(dispBuf, n);
      }

      JsonDocument doc;
      doc["type"] = "rx";
      doc["hex"] = bytesToHex(dispBuf, n);
      doc["ascii"] = bytesToAscii(dispBuf, n);
      String out;
      serializeJson(doc, out);
      wsRS232.textAll(out);
    }
  }

  wsRS232.cleanupClients();
}
