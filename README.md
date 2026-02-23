# ESP32 AV Controller

> **A pocket-sized, web-based AV command center for installers and enthusiasts.**
> Turn a £5 ESP32 into a powerful network tool for controlling projectors, displays, switchers, and more via RS232, TCP, UDP, and PJLink.

![Platform](https://img.shields.io/badge/Platform-ESP32-blue)
![Framework](https://img.shields.io/badge/Framework-Arduino-green)
![Version](https://img.shields.io/badge/Version-0.8.7-orange)
![License](https://img.shields.io/badge/License-MIT-lightgrey)

---

## Quick Flash (No Code Required)

**Don't want to set up a developer environment?** Flash directly from your browser:

1. Buy an **ESP32 DevKit v1** ([Amazon UK](https://www.amazon.co.uk/s?k=esp32+devkit+v1) · [AliExpress](https://www.aliexpress.com/w/wholesale-esp32-devkit-v1.html) — ~£5)
2. Plug it into your PC via USB
3. Visit the **[Web Flasher](https://jobey99.github.io/ESP32-Controller/flash/)** in Chrome/Edge
4. Click **"Install"** → Done!

> **Note:** Web Serial requires Chrome, Edge, or Opera on desktop. Mobile browsers are not supported.

---

## 🛒 Hardware You Need

| Component | Required | Approx. Cost | Notes |
|-----------|----------|-------------|-------|
| ESP32 DevKit v1 |  Yes | ~£5 | Any ESP32-WROOM-32 board works |
| Micro-USB / USB-C cable |  Yes | ~£3 | For initial flashing + power |
| MAX3232 RS232-to-TTL module |  Optional | ~£3 | Only needed for RS232 serial devices |
| DB9 cable |  Optional | ~£3 | For connecting to RS232 gear |
| 5V USB power supply |  Optional | ~£5 | For permanent installation |

### Wiring (RS232 Module)

If you want to control RS232 devices (projectors, matrix switchers, etc.):

```
ESP32 GPIO 16 (RX2) ──→ MAX3232 TX
ESP32 GPIO 17 (TX2) ──→ MAX3232 RX
ESP32 3.3V          ──→ MAX3232 VCC
ESP32 GND           ──→ MAX3232 GND
MAX3232 DB9         ──→ AV Device RS232 port
```

No RS232 module? No problem — all network features (TCP, UDP, PJLink, Discovery) work without it.

---

## Features

### Control
- **Dashboard** — Live system status, device overview, quick-launch macros
- **Macros** — Save multi-step command sequences (TCP + RS232 + UDP) and replay with one click
- **RS232 Terminal** — Full serial terminal with baud rate, polarity inversion, auto-detect, loopback test
- **RS232 Profiles** — Pre-built for Extron, Blustream, Kramer, and generic devices
- **Telnet-to-Serial Bridge** — Access RS232 remotely via Telnet on port 23 (PuTTY, Crestron, AMX)
- **TCP Client** — Connect to any raw TCP server, send ASCII or HEX commands
- **TCP Server** — Listen for incoming connections, broadcast messages
- **UDP Tool** — Send/receive UDP packets
- **PJLink** — Native projector control (power, input, mute, custom commands)
- **Command Templates** — Pre-built command libraries for Extron, Kramer, Lightware, Samsung
- **Learner** — Capture and decode incoming TCP traffic for reverse engineering

### Network Tools
- **Subnet Scanner** — Ping-sweep your entire network
- **Port Scanner** — Probe specific ports on any device
- **SSDP Browser** — Discover UPnP/DLNA devices (TVs, media servers)
- **mDNS Browser** — Find Bonjour/ZeroConf services (AirPlay, Dante, Crestron NVX)
- **Ping & Wake-on-LAN** — Test connectivity and wake PCs remotely
- **DNS Lookup & Internet Check** — Verify DNS resolution and WAN connectivity
- **Subnet Calculator** — IP/CIDR math in the browser
- **TCP Proxy** — Man-in-the-middle AV protocols for debugging

### Settings
- **Wi-Fi** — AP, STA, or AP+STA mode with visual signal analyzer
- **Config Backup** — Export/import all settings as JSON
- **OTA Updates** — Remote firmware updates from GitHub + manual .bin upload
- **Firmware Rollback** — Roll back to previous firmware version

---

## Developer Setup (PlatformIO)

If you want to modify the code and build from source:

### Prerequisites
- [PlatformIO](https://platformio.org/) (VS Code extension recommended)
- USB connection to an ESP32

### Build & Flash

```bash
# Clone the repo
git clone https://github.com/Jobey99/ESP32-Controller.git
cd ESP32-Controller

# Build firmware
pio run

# Upload firmware
pio run --target upload

# Upload web UI filesystem
pio run --target uploadfs

# Open serial monitor
pio device monitor -b 115200
```

### First Boot

1. The ESP32 creates a Wi-Fi access point: **`ESP32-AV-Tool`**
2. Connect to it from your phone/laptop
3. Visit **http://192.168.4.1/** in your browser
4. Go to **Wi-Fi Settings** → scan for your network → enter credentials → save
5. The ESP32 reboots and joins your network
6. Access it at **http://esp32-av-tool.local/** (mDNS) or check serial logs for the IP

---

## Project Layout

```
src/              C++ firmware source
├── main.cpp          Entry point, loop, setup
├── WebAPI.cpp        HTTP API + WebSocket handlers
├── RS232Handler.cpp  RS232 serial + telnet bridge
├── MacroHandler.cpp  Macro scheduler engine
├── SSDPScanner.cpp   SSDP discovery
├── ...
include/          Headers
data/             Web UI (served from LittleFS)
├── index.html        Single-page app
├── app.js            Frontend logic
├── style.css         Styles
tools/            Development utilities
├── discovery-spoof.py  Fake SSDP/mDNS devices for testing
docs/flash/       ESP Web Tools browser flasher
tests/            Verification scripts
```

### Key Libraries (auto-installed by PlatformIO)
- `ESPAsyncWebServer-esphome` — Async HTTP + WebSocket server
- `ArduinoJson` v7 — JSON parsing
- `ESP32Ping` — ICMP ping

---

## Development & Testing

### Mock Server (No Hardware)

Test the web UI locally without an ESP32:

```bash
npm install
npm run start:mock
# → http://localhost:3000/
```

### SSDP/mDNS Discovery Testing

Test the discovery features using the spoof tool:

```bash
pip install zeroconf
python tools/discovery-spoof.py
```

This creates fake SSDP and mDNS devices on your network that the ESP32 will discover.

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Can't reach web UI | Check serial logs for IP. Try `http://<ip>/` instead of mDNS |
| LittleFS mount failed | Run `pio run --target uploadfs` |
| WiFi won't connect | Use the AP (`ESP32-AV-Tool`) to reconfigure credentials |
| OTA check fails (HTTP -1) | Ensure internet access. Check heap in Live Logs |
| RS232 garbled output | Try Auto-Baud scan or manually set baud rate |
| Web flasher won't connect | Use Chrome/Edge on desktop. Ensure USB drivers are installed |

---

## API Reference

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/health` | GET | System status, uptime, WiFi info |
| `/api/dashboard` | GET | Aggregate dashboard data |
| `/api/wifi` | GET/POST | WiFi configuration |
| `/api/wifi/scan` | GET | Scan visible networks |
| `/api/macros` | GET | List all macros |
| `/api/macros/save` | POST | Create/update a macro |
| `/api/macros/run` | POST | Execute a macro |
| `/api/templates` | GET | List command templates |
| `/api/ssdp/scan` | POST | Start SSDP discovery |
| `/api/mdns/scan` | POST | Start mDNS discovery |
| `/api/pjlink` | POST | Send PJLink command |
| `/api/reboot` | POST | Reboot device |

WebSocket endpoints: `/ws` (logs), `/term` (terminal), `/wsrs232`, `/wsudp`, `/wstcpserver`, `/wsproxy`, `/wsdisc`

---

## Contributing

Contributions welcome! Open an issue or PR. For feature requests, please describe your use case.

## License

MIT License — see [LICENSE](LICENSE) file.

---

