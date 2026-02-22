# Manual Verification Checklist

**Target Device IP:** `192.168.0.245`

## 1. Physical Interface
- [ ] **Power Cycle**: Unplug and replug the device. Ensure it boots within 5 seconds.
- [ ] **LEDs**: Verify if any status LEDs (if configured) blink correctly during boot and WiFi connection.
- [ ] **RS232 Loopback**:
    - Connect RX to TX on the DB9 connector.
    - Open the Web Terminal.
    - Enable "Hex" mode.
    - Send `AA BB CC`.
    - Verify `AA BB CC` is received immediately.

## 2. WiFi Operations
- [ ] **AP Mode**:
    - Hold the "Boot" button (if programmed to reset WiFi) or use the "Forget WiFi" button in the web UI.
    - Verify "ESP32-AV-Tool" AP appears.
    - Connect to it (default IP usually `192.168.4.1`).
- [ ] **Station Mode**:
    - Configure your local WiFi credentials.
    - Verify it connects and retrieves the IP `192.168.0.245` (if reserved) or similar.

## 3. Web UI Reliability
- [ ] **Simultaneous Connections**: Open the UI on both a laptop and a phone.
- [ ] **Reboot**: Click "Reboot" in Settings. Verify the web page reloads automatically or manually after ~10 seconds.
- [ ] **Discovery**: Run a "Subnet Scan". Verify it doesn't crash the device.
- [ ] **Proxy**: Start the TCP Proxy. Ensure latency is acceptable.

## 4. Stability
- [ ] Leave the "Device Monitor" page open for 10 minutes. Ensure no "Reconnecting..." toasts appear repeatedly.
