
import requests
import socket
import time
import argparse
import sys

def check_ip(ip):
    print(f"[*] Checking {ip}...")
    try:
        response = requests.get(f"http://{ip}/api/health", timeout=3)
        if response.status_code == 200:
            data = response.json()
            print(f"    [+] Health OK: FW={data.get('fw')} Uptime={data.get('uptime_s')}s")
            return True
        else:
            print(f"    [-] Health Check Failed: {response.status_code}")
            return False
    except Exception as e:
        print(f"    [-] Connection Error: {e}")
        return False

def check_wifi(ip):
    print("[*] Checking WiFi Config...")
    try:
        response = requests.get(f"http://{ip}/api/wifi", timeout=3)
        if response.status_code == 200:
            data = response.json()
            print(f"    [+] WiFi Mode: {data.get('mode')} AP: {data.get('apSsid')}")
        else:
            print("    [-] WiFi API Failed")
    except:
        print("    [-] WiFi API Error")

def check_udp(ip):
    print("[*] Testing UDP Loopback...")
    # Send a UDP packet to the device's listener port (default 5000)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(2)
    msg = b"TEST_PING"
    try:
        # We can't easily verify receipt unless we listen on a websocket or the device echoes back.
        # The device logs received UDP to /wsudp. 
        # For this simple script, we just verify we can send without error.
        sock.sendto(msg, (ip, 5000))
        print("    [+] UDP Packet Sent (Verification requires checking Web UI logs)")
    except Exception as e:
        print(f"    [-] UDP Send Error: {e}")

def check_discovery(ip):
    print("[*] Checking Discovery Status...")
    try:
        response = requests.get(f"http://{ip}/api/discovery/results", timeout=3)
        if response.status_code == 200:
            data = response.json()
            print(f"    [+] Discovery Running: {data.get('running')}")
            print(f"    [+] Devices Found: {len(data.get('results', []))}")
        else:
            print("    [-] Discovery API Failed")
    except:
        print("    [-] Discovery API Error")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='ESP32 AV Tool Verifier')
    parser.add_argument('--ip', required=True, help='IP address of the ESP32')
    args = parser.parse_args()

    if check_ip(args.ip):
        check_wifi(args.ip)
        check_discovery(args.ip)
        check_udp(args.ip)
        print("\n[+] Verification Request Sent. Please manually verify 'Manual Verification Checklist.md' for hardware tests.")
    else:
        sys.exit(1)
