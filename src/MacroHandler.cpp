#include "MacroHandler.h"
#include "AppConfig.h"
#include "RS232Handler.h"
#include "Utils.h"
#include <WiFi.h>
#include <WiFiUdp.h>

MacroHandler macroHandler;
extern void logAll(const String &s);

void MacroHandler::begin() { load(); }

void MacroHandler::loop() {
  if (_pendingRun) {
    _pendingRun = false;
    doExecute(_pendingId);
  }
}

void MacroHandler::load() {
  _macros.clear();
  String raw = prefs.getString("macros", "[]");
  JsonDocument doc;
  if (deserializeJson(doc, raw))
    return;
  if (!doc.is<JsonArray>())
    return;

  for (JsonObject m : doc.as<JsonArray>()) {
    Macro macro;
    macro.id = m["id"].as<String>();
    macro.name = m["name"].as<String>();
    macro.icon = m["icon"] | "▶";
    if (m["steps"].is<JsonArray>()) {
      for (JsonObject s : m["steps"].as<JsonArray>()) {
        MacroStep step;
        step.type = s["type"].as<String>();
        step.target = s["target"] | "";
        step.port = s["port"] | 0;
        step.payload = s["payload"] | "";
        step.mode = s["mode"] | "ascii";
        step.suffix = s["suffix"] | "";
        step.delayMs = s["delay"] | 500;
        macro.steps.push_back(step);
      }
    }
    _macros.push_back(macro);
  }
  logAll("Macros: loaded " + String(_macros.size()));
}

void MacroHandler::persist() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto &m : _macros) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = m.id;
    obj["name"] = m.name;
    obj["icon"] = m.icon;
    JsonArray steps = obj["steps"].to<JsonArray>();
    for (const auto &s : m.steps) {
      JsonObject so = steps.add<JsonObject>();
      so["type"] = s.type;
      so["target"] = s.target;
      so["port"] = s.port;
      so["payload"] = s.payload;
      so["mode"] = s.mode;
      so["suffix"] = s.suffix;
      so["delay"] = s.delayMs;
    }
  }
  String out;
  serializeJson(doc, out);
  prefs.putString("macros", out);
}

String MacroHandler::listJson() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto &m : _macros) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = m.id;
    obj["name"] = m.name;
    obj["icon"] = m.icon;
    obj["stepCount"] = m.steps.size();
  }
  String out;
  serializeJson(doc, out);
  return out;
}

String MacroHandler::getById(const String &id) {
  for (const auto &m : _macros) {
    if (m.id == id) {
      JsonDocument doc;
      doc["id"] = m.id;
      doc["name"] = m.name;
      doc["icon"] = m.icon;
      JsonArray steps = doc["steps"].to<JsonArray>();
      for (const auto &s : m.steps) {
        JsonObject so = steps.add<JsonObject>();
        so["type"] = s.type;
        so["target"] = s.target;
        so["port"] = s.port;
        so["payload"] = s.payload;
        so["mode"] = s.mode;
        so["suffix"] = s.suffix;
        so["delay"] = s.delayMs;
      }
      String out;
      serializeJson(doc, out);
      return out;
    }
  }
  return "{}";
}

bool MacroHandler::save(const String &json) {
  JsonDocument doc;
  if (deserializeJson(doc, json))
    return false;

  String id = doc["id"] | "";
  bool isNew = (id.length() == 0);
  if (isNew)
    id = genId();

  // Find or create
  Macro *target = nullptr;
  for (auto &m : _macros) {
    if (m.id == id) {
      target = &m;
      break;
    }
  }
  if (!target) {
    _macros.push_back(Macro());
    target = &_macros.back();
    target->id = id;
  }

  target->name = doc["name"] | "Unnamed Macro";
  target->icon = doc["icon"] | "▶";
  target->steps.clear();

  if (doc["steps"].is<JsonArray>()) {
    for (JsonObject s : doc["steps"].as<JsonArray>()) {
      MacroStep step;
      step.type = s["type"].as<String>();
      step.target = s["target"] | "";
      step.port = s["port"] | 0;
      step.payload = s["payload"] | "";
      step.mode = s["mode"] | "ascii";
      step.suffix = s["suffix"] | "";
      step.delayMs = s["delay"] | 500;
      target->steps.push_back(step);
    }
  }

  persist();
  logAll("Macro saved: " + target->name + " (" + String(target->steps.size()) +
         " steps)");
  return true;
}

bool MacroHandler::remove(const String &id) {
  for (auto it = _macros.begin(); it != _macros.end(); ++it) {
    if (it->id == id) {
      logAll("Macro deleted: " + it->name);
      _macros.erase(it);
      persist();
      return true;
    }
  }
  return false;
}

bool MacroHandler::execute(const String &id) {
  if (_running)
    return false;
  _pendingRun = true;
  _pendingId = id;
  return true;
}

// Static FreeRTOS task function
static void macroTaskCode(void *parameter) {
  Macro *macroCopy = static_cast<Macro *>(parameter);

  logAll("▶ Macro Task: " + macroCopy->name + " (" +
         String(macroCopy->steps.size()) + " steps)");

  for (size_t i = 0; i < macroCopy->steps.size(); i++) {
    const auto &step = macroCopy->steps[i];
    logAll("  Step " + String(i + 1) + ": " + step.type + " → " + step.target);

    if (step.type == "tcp") {
      macroHandler.executeTcpStep(step);
    } else if (step.type == "rs232") {
      macroHandler.executeRS232Step(step);
    } else if (step.type == "udp") {
      macroHandler.executeUdpStep(step);
    } else if (step.type == "delay") {
      vTaskDelay(pdMS_TO_TICKS(step.delayMs));
    }

    // Inter-step delay
    if (step.type != "delay" && step.delayMs > 0) {
      vTaskDelay(pdMS_TO_TICKS(step.delayMs));
    }
  }

  logAll("✓ Macro Task complete: " + macroCopy->name);

  // Cleanup
  delete macroCopy;
  macroHandler.setRunning(false);
  vTaskDelete(NULL);
}

void MacroHandler::doExecute(const String &id) {
  const Macro *sourceMacro = nullptr;
  for (const auto &m : _macros) {
    if (m.id == id) {
      sourceMacro = &m;
      break;
    }
  }

  if (!sourceMacro) {
    logAll("Macro not found: " + id);
    return;
  }

  _running = true;

  // Make a copy of the macro to pass to the task (in case _macros is modified
  // while running)
  Macro *macroCopy = new Macro(*sourceMacro);

  // Spawn FreeRTOS task on Core 1 (App Core) to avoid blocking the
  // webserver/main loop
  xTaskCreatePinnedToCore(macroTaskCode, // Task function
                          "MacroTask",   // Name of task
                          4096,          // Stack size
                          macroCopy,     // Parameter
                          1, // Priority (1 is standard Arduino loop priority)
                          NULL, // Task handle
                          1);   // Core to run on (1 = App core)
}

void MacroHandler::executeTcpStep(const MacroStep &step) {
  WiFiClient client;
  if (client.connect(step.target.c_str(), step.port, 3000)) {
    String data = step.payload;
    if (step.suffix == "\\r")
      data += "\r";
    else if (step.suffix == "\\n")
      data += "\n";
    else if (step.suffix == "\\r\\n")
      data += "\r\n";

    if (step.mode == "hex") {
      std::vector<uint8_t> bytes;
      if (parseHexBytes(data, bytes))
        client.write(bytes.data(), bytes.size());
    } else {
      client.print(data);
    }

    // Brief read for response
    delay(200);
    String resp = "";
    while (client.available()) {
      resp += (char)client.read();
    }
    if (resp.length() > 0) {
      logAll("    Response: " + resp.substring(0, 80));
    }
    client.stop();
  } else {
    logAll("    TCP connect failed: " + step.target + ":" + String(step.port));
  }
}

void MacroHandler::executeRS232Step(const MacroStep &step) {
  rs232Send(step.payload, step.mode == "hex", step.suffix);
}

void MacroHandler::executeUdpStep(const MacroStep &step) {
  WiFiUDP udp;
  if (udp.beginPacket(step.target.c_str(), step.port)) {
    String data = step.payload;
    if (step.suffix == "\\r")
      data += "\r";
    else if (step.suffix == "\\n")
      data += "\n";
    else if (step.suffix == "\\r\\n")
      data += "\r\n";
    udp.write((uint8_t *)data.c_str(), data.length());
    udp.endPacket();
  }
}
