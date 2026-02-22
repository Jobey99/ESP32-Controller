#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "AppConfig.h"

extern String cfgJson;

void loadCfg();
void saveCfg();
String defaultCfgJson();

bool updateCfgWithDevice(const String &name, const String &ip,
                         uint16_t portHint, const String &suffixHint,
                         const String &notes, const String &templateId,
                         const String &payloadType, const String &mac);

bool removeDevice(const String &id);

#endif
