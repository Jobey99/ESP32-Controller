#pragma once

#include <Arduino.h>

class OTAHandler {
public:
  void begin();
  void loop();
  void setManifestUrl(const String &url);
  void checkUpdate();
  void triggerCheck();

private:
  String _manifestUrl;
  bool _manualCheck = false;
  unsigned long _lastCheck = 0;
  const unsigned long _checkInterval = 3600000; // Check every hour

  bool isNewer(const String &serverVer, const String &currentVer);
  void performUpdate(const String &url);
  void performFSUpdate(const String &url);
};

extern OTAHandler otaHandler;
