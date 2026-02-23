#!/usr/bin/env python3
"""
SSDP & mDNS Discovery Spoof Tool
=================================
Run this on a PC on the same network as your ESP32.
It advertises fake SSDP and mDNS services so the ESP32's
discovery scanners have something to find.

Usage:
    pip install zeroconf
    python discovery-spoof.py [--ip YOUR_PC_IP]

The script will:
  1. Listen for SSDP M-SEARCH on 239.255.255.250:1900 and respond
     with fake device descriptions (projector, display, media renderer)
  2. Register fake mDNS services (_http._tcp, _pjlink._tcp)

Press Ctrl+C to stop.
"""

import argparse
import socket
import struct
import sys
import threading
import time

# --- Attempt to import zeroconf for mDNS ---
try:
    from zeroconf import Zeroconf, ServiceInfo
    HAS_ZEROCONF = True
except ImportError:
    HAS_ZEROCONF = False
    print("[!] 'zeroconf' not installed. mDNS spoofing disabled.")
    print("    Install with: pip install zeroconf")


SSDP_ADDR = "239.255.255.250"
SSDP_PORT = 1900


def get_local_ip():
    """Best-effort local IP detection."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except Exception:
        return "127.0.0.1"
    finally:
        s.close()


# ── SSDP Fake Devices ──────────────────────────────────────────────

FAKE_SSDP_DEVICES = [
    {
        "ST": "urn:schemas-upnp-org:device:MediaRenderer:1",
        "USN": "uuid:fake-media-renderer-001::urn:schemas-upnp-org:device:MediaRenderer:1",
        "SERVER": "FakeOS/1.0 UPnP/1.1 FakeRenderer/3.0",
        "FRIENDLY": "Fake Media Renderer",
    },
    {
        "ST": "upnp:rootdevice",
        "USN": "uuid:fake-projector-001::upnp:rootdevice",
        "SERVER": "FakeOS/1.0 UPnP/1.1 FakeProjector/2.0",
        "FRIENDLY": "Fake Epson Projector",
    },
    {
        "ST": "urn:schemas-upnp-org:device:Basic:1",
        "USN": "uuid:fake-display-001::urn:schemas-upnp-org:device:Basic:1",
        "SERVER": "FakeOS/1.0 UPnP/1.1 FakeDisplay/1.0",
        "FRIENDLY": "Fake Samsung Display",
    },
]


def build_ssdp_response(device: dict, local_ip: str) -> bytes:
    """Build an SSDP M-SEARCH response for a fake device."""
    location = f"http://{local_ip}:8080/device.xml"
    resp = (
        "HTTP/1.1 200 OK\r\n"
        f"CACHE-CONTROL: max-age=1800\r\n"
        f"LOCATION: {location}\r\n"
        f"SERVER: {device['SERVER']}\r\n"
        f"ST: {device['ST']}\r\n"
        f"USN: {device['USN']}\r\n"
        f"X-FRIENDLY-NAME: {device['FRIENDLY']}\r\n"
        "\r\n"
    )
    return resp.encode()


def ssdp_listener(local_ip: str):
    """Listen for SSDP M-SEARCH and reply with fake devices."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    # Windows & Linux differ on multicast bind
    try:
        sock.bind(("", SSDP_PORT))
    except OSError as e:
        print(f"[!] SSDP bind failed: {e}")
        print("    Another process may be using port 1900.")
        print("    Try closing any UPnP/DLNA software and retry.")
        return

    # Join multicast group
    mreq = struct.pack(
        "4s4s",
        socket.inet_aton(SSDP_ADDR),
        socket.inet_aton(local_ip),
    )
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    sock.settimeout(1.0)

    print(f"[SSDP] Listening on {SSDP_ADDR}:{SSDP_PORT}")
    print(f"[SSDP] Will respond as {len(FAKE_SSDP_DEVICES)} fake devices")

    while True:
        try:
            data, addr = sock.recvfrom(2048)
        except socket.timeout:
            continue
        except OSError:
            break

        msg = data.decode(errors="replace")
        if "M-SEARCH" not in msg:
            continue

        print(f"\n[SSDP] <-- M-SEARCH from {addr[0]}:{addr[1]}")

        # Reply with each fake device
        for dev in FAKE_SSDP_DEVICES:
            reply = build_ssdp_response(dev, local_ip)
            sock.sendto(reply, addr)
            print(f"[SSDP] --> Replied as '{dev['FRIENDLY']}' to {addr[0]}")

        time.sleep(0.05)  # Small delay between responses


# ── mDNS Fake Services ─────────────────────────────────────────────

def start_mdns_services(local_ip: str):
    """Register fake mDNS services using zeroconf."""
    if not HAS_ZEROCONF:
        print("[mDNS] Skipped (zeroconf not installed)")
        return None, []

    zc = Zeroconf()
    services = []
    ip_bytes = socket.inet_aton(local_ip)

    fake_services = [
        {
            "type": "_http._tcp.local.",
            "name": "Fake AV Controller._http._tcp.local.",
            "port": 80,
            "properties": {"path": "/", "manufacturer": "FakeAV Inc."},
        },
        {
            "type": "_http._tcp.local.",
            "name": "Fake NAS Web UI._http._tcp.local.",
            "port": 8080,
            "properties": {"path": "/admin", "model": "FakeNAS-200"},
        },
        {
            "type": "_pjlink._tcp.local.",
            "name": "Fake Epson PJLink._pjlink._tcp.local.",
            "port": 4352,
            "properties": {"class": "1", "manufacturer": "Fake Epson"},
        },
        {
            "type": "_telnet._tcp.local.",
            "name": "Fake Extron Switcher._telnet._tcp.local.",
            "port": 23,
            "properties": {"model": "FakeSW-4HD", "protocol": "SIS"},
        },
    ]

    for svc in fake_services:
        info = ServiceInfo(
            svc["type"],
            svc["name"],
            addresses=[ip_bytes],
            port=svc["port"],
            properties=svc["properties"],
            server=f"fake-device-{svc['port']}.local.",
        )
        zc.register_service(info)
        services.append(info)
        print(f"[mDNS] Registered: {svc['name']}  (port {svc['port']})")

    print(f"[mDNS] {len(services)} services registered on {local_ip}")
    return zc, services


# ── Main ────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Spoof SSDP + mDNS devices for ESP32 discovery testing"
    )
    parser.add_argument(
        "--ip",
        default=None,
        help="Your PC's local IP address (auto-detected if omitted)",
    )
    args = parser.parse_args()

    local_ip = args.ip or get_local_ip()
    print(f"╔══════════════════════════════════════════════════╗")
    print(f"║  ESP32 Discovery Spoof Tool                     ║")
    print(f"║  Local IP: {local_ip:<38}║")
    print(f"╚══════════════════════════════════════════════════╝")
    print()

    # Start mDNS
    zc, mdns_services = start_mdns_services(local_ip)

    # Start SSDP listener in a daemon thread
    ssdp_thread = threading.Thread(target=ssdp_listener, args=(local_ip,), daemon=True)
    ssdp_thread.start()

    print()
    print("Press Ctrl+C to stop.\n")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n[*] Shutting down...")

    # Cleanup mDNS
    if zc:
        for svc in mdns_services:
            zc.unregister_service(svc)
        zc.close()
        print("[mDNS] Services unregistered.")

    print("[*] Done.")


if __name__ == "__main__":
    main()
