#ifndef APP_UTILS_H
#define APP_UTILS_H

#include <Arduino.h>
#include <vector>

String bytesToHex(const uint8_t *data, size_t len);
String bytesToAscii(const uint8_t *data, size_t len);
String stripTelnetIAC(const uint8_t *data, size_t len);
String detectSuffix(const uint8_t *data, size_t len);
String simpleHash(const String &s);
String genId();
bool parseHexBytes(const String &hex, std::vector<uint8_t> &out);

#endif
