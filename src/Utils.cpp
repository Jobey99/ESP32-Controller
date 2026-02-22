#include "Utils.h"
#include <Arduino.h>

String bytesToHex(const uint8_t *data, size_t len) {
  static const char *h = "0123456789ABCDEF";
  String out;
  out.reserve(len * 3);
  for (size_t i = 0; i < len; i++) {
    out += h[(data[i] >> 4) & 0xF];
    out += h[data[i] & 0xF];
    if (i + 1 < len)
      out += ' ';
  }
  return out;
}

String bytesToAscii(const uint8_t *data, size_t len) {
  String out;
  out.reserve(len);
  for (size_t i = 0; i < len; i++) {
    char c = (char)data[i];
    out += (c >= 32 && c <= 126) ? c : '.';
  }
  return out;
}

String stripTelnetIAC(const uint8_t *data, size_t len) {
  String out;
  out.reserve(len);
  for (size_t i = 0; i < len; i++) {
    uint8_t b = data[i];
    if (b == 0xFF) {
      if (i + 2 < len) {
        i += 2;
      }
      continue;
    }
    if (b >= 32 && b <= 126)
      out += (char)b;
    else if (b == '\r' || b == '\n' || b == '\t')
      out += (char)b;
  }
  return out;
}

String detectSuffix(const uint8_t *data, size_t len) {
  bool hasCR = false, hasLF = false;
  for (size_t i = 0; i < len; i++) {
    if (data[i] == 0x0D)
      hasCR = true;
    if (data[i] == 0x0A)
      hasLF = true;
  }
  for (size_t i = 0; i + 1 < len; i++) {
    if (data[i] == 0x0D && data[i + 1] == 0x0A)
      return "\\r\\n";
  }
  if (hasCR && !hasLF)
    return "\\r";
  if (!hasCR && hasLF)
    return "\\n";
  if (hasCR && hasLF)
    return "\\r\\n";
  return "";
}

String simpleHash(const String &s) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < s.length(); i++) {
    h ^= (uint8_t)s[i];
    h *= 16777619u;
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%08lX", (unsigned long)h);
  return String(buf);
}

String genId() {
  uint32_t r = esp_random();
  char buf[16];
  snprintf(buf, sizeof(buf), "%08lX", (unsigned long)r);
  return String(buf);
}

bool parseHexBytes(const String &hex, std::vector<uint8_t> &out) {
  out.clear();
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F')
      return 10 + (c - 'A');
    return -1;
  };
  int i = 0;
  while (i < (int)hex.length()) {
    while (i < (int)hex.length() && hex[i] == ' ')
      i++;
    if (i >= (int)hex.length())
      break;
    if (i + 1 >= (int)hex.length())
      return false;
    int n1 = nib(hex[i++]);
    int n2 = nib(hex[i++]);
    if (n1 < 0 || n2 < 0)
      return false;
    out.push_back((uint8_t)((n1 << 4) | n2));
  }
  return true;
}
