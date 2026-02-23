#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

struct MacroStep {
  String type;   // "tcp", "rs232", "udp", "pjlink", "delay"
  String target; // IP or "serial"
  uint16_t port;
  String payload;
  String mode;   // "ascii" or "hex"
  String suffix; // "\r", "\n", "\r\n", ""
  uint16_t delayMs;
};

struct Macro {
  String id;
  String name;
  String icon; // emoji or short label
  std::vector<MacroStep> steps;
};

class MacroHandler {
public:
  void begin();
  void loop();

  // CRUD
  String listJson();
  bool save(const String &json);
  bool remove(const String &id);
  String getById(const String &id);

  // Execution
  bool execute(const String &id);
  bool isRunning() { return _running; }

private:
  std::vector<Macro> _macros;
  bool _running = false;
  bool _pendingRun = false;
  String _pendingId;

  void load();
  void persist();
  void doExecute(const String &id);
  void executeTcpStep(const MacroStep &step);
  void executeRS232Step(const MacroStep &step);
  void executeUdpStep(const MacroStep &step);
};

extern MacroHandler macroHandler;
