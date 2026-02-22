#include "ConfigManager.h"
#include "Utils.h"
#include <ArduinoJson.h>


String cfgJson;

String defaultCfgJson() {
  return R"JSON({
  "devices": [],
  "templates": [
    {
      "id":"TPL_EXTRON_TELNET",
      "name":"Extron (Generic Telnet)",
      "kind":"extron",
      "defaultPort":23,
      "defaultSuffix":"\\r",
      "defaultCommands":[
        {"name":"Info", "payloadType":"ascii", "payload":"I", "suffix":"\\r"}
      ]
    },
    {
      "id":"TPL_KRAMER_P3000",
      "name":"Kramer Protocol 3000",
      "kind":"kramer",
      "defaultPort":5000,
      "defaultSuffix":"\\r\\n",
      "defaultCommands":[
        {"name":"Model?", "payloadType":"ascii", "payload":"#MODEL?", "suffix":"\\r\\n"}
      ]
    },
    {
      "id":"TPL_LIGHTWARE_LW3",
      "name":"Lightware (example)",
      "kind":"lightware",
      "defaultPort":6100,
      "defaultSuffix":"\\r\\n",
      "defaultCommands":[
        {"name":"Help", "payloadType":"ascii", "payload":"help", "suffix":"\\r\\n"}
      ]
    },
    {
      "id":"TPL_SAMSUNG_MDC_EXAMPLE",
      "name":"Samsung MDC (example)",
      "kind":"samsung",
      "defaultPort":1515,
      "defaultSuffix":"",
      "defaultCommands":[
        {"name":"Power On (example)", "payloadType":"hex", "payload":"AA 11 01 01", "suffix":""}
      ]
    }
  ]
})JSON";
}

void loadCfg() {
  cfgJson = prefs.getString("cfg_json", defaultCfgJson());
  if (cfgJson.length() < 10)
    cfgJson = defaultCfgJson();
}

void saveCfg() { prefs.putString("cfg_json", cfgJson); }

bool updateCfgWithDevice(const String &name, const String &ip,
                         uint16_t portHint, const String &suffixHint,
                         const String &notes, const String &templateId,
                         const String &payloadType, const String &mac) {
  JsonDocument doc;
  if (deserializeJson(doc, cfgJson))
    return false;

  if (!doc["devices"].is<JsonArray>())
    doc["devices"] = JsonArray();

  JsonArray devs = doc["devices"].as<JsonArray>();
  JsonObject d = devs.add<JsonObject>();
  d["id"] = genId();
  d["name"] = name;
  d["ip"] = ip;
  d["portHint"] = portHint;
  d["defaultSuffix"] = suffixHint;
  d["notes"] = notes;
  d["templateId"] = templateId;
  d["defaultPayloadType"] = payloadType;
  d["mac"] = mac;
  d["lastSeenMs"] = millis();

  String out;
  serializeJson(doc, out);
  cfgJson = out;
  saveCfg();
  return true;
}

bool removeDevice(const String &id) {
  JsonDocument doc;
  if (deserializeJson(doc, cfgJson))
    return false;
  if (!doc["devices"].is<JsonArray>())
    return false;

  JsonArray devs = doc["devices"].as<JsonArray>();
  for (int i = 0; i < (int)devs.size(); i++) {
    if (devs[i]["id"] == id) {
      devs.remove(i);
      String out;
      serializeJson(doc, out);
      cfgJson = out;
      saveCfg();
      return true;
    }
  }
  return false;
}
