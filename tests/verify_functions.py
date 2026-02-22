
import socket
import threading
import time
import json
from http.server import HTTPServer, BaseHTTPRequestHandler

# Configuration
LOCAL_IP = "192.168.0.100"
TCP_SERVER_PORT = 8080   # Port for ESP32 to connect TO
UDP_PORT_LISTEN = 5000   # Port to listen for UDP FROM ESP32
UDP_PORT_SEND = 5000     # Port to send UDP TO ESP32 (if it listens on 5000)
ESP32_IP = "192.168.0.245"
ESP32_TCP_SERVER_PORT = 23

# State
received_tcp = []
received_udp = []
server_running = True

def log(msg):
    print(f"[TEST-SERVER] {msg}")

# TCP Server (Simulates a device ESP32 connects to)
def run_tcp_server():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.bind((LOCAL_IP, TCP_SERVER_PORT))
    server.listen(1)
    log(f"TCP Server listening on {LOCAL_IP}:{TCP_SERVER_PORT}")
    
    while server_running:
        try:
            server.settimeout(1.0)
            conn, addr = server.accept()
            log(f"TCP Connection from {addr}")
            conn.sendall(b"Hello from Python TCP Server\n")
            while True:
                data = conn.recv(1024)
                if not data: break
                msg = data.decode('utf-8', errors='ignore')
                log(f"TCP RX: {msg}")
                received_tcp.append(msg)
                # Echo back
                conn.sendall(f"Echo: {msg}".encode('utf-8'))
            conn.close()
        except socket.timeout:
            continue
        except Exception as e:
            if server_running: log(f"TCP Server error: {e}")

# UDP Listener
def run_udp_listener():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((LOCAL_IP, UDP_PORT_LISTEN))
    sock.settimeout(1.0)
    log(f"UDP Listener on {LOCAL_IP}:{UDP_PORT_LISTEN}")

    while server_running:
        try:
            data, addr = sock.recvfrom(1024)
            msg = data.decode('utf-8', errors='ignore')
            log(f"UDP RX from {addr}: {msg}")
            received_udp.append(msg)
            # Echo back to sender
            sock.sendto(f"Ack: {msg}".encode('utf-8'), addr)
        except socket.timeout:
            continue
        except Exception as e:
            if server_running: log(f"UDP Error: {e}")

# Control API (to be queried by agent or script)
# Simple HTTP server to report status or trigger sends
class ControlHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/status':
            self.send_response(200)
            self.end_headers()
            status = {
                "tcp_rx": received_tcp,
                "udp_rx": received_udp
            }
            self.wfile.write(json.dumps(status).encode())
        elif self.path.startswith('/send_tcp'):
            # Trigger sending TO ESP32 TCP Server
            # /send_tcp?msg=hello
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.settimeout(2)
                s.connect((ESP32_IP, ESP32_TCP_SERVER_PORT))
                s.sendall(b"Hello from Python Client")
                s.close()
                self.send_response(200)
                self.end_headers()
                self.wfile.write(b"Sent TCP")
            except Exception as e:
                self.send_response(500)
                self.end_headers()
                self.wfile.write(str(e).encode())

        elif self.path.startswith('/send_udp'):
            # Trigger sending TO ESP32 UDP Listener
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                s.sendto(b"Hello from Python UDP", (ESP32_IP, UDP_PORT_SEND))
                self.send_response(200)
                self.end_headers()
                self.wfile.write(b"Sent UDP")
            except Exception as e:
                self.send_response(500)
                self.end_headers()
                self.wfile.write(str(e).encode())

def run_control_server():
    httpd = HTTPServer(('127.0.0.1', 8888), ControlHandler)
    log("Control API listening on http://127.0.0.1:8888")
    while server_running:
        httpd.handle_request()

if __name__ == "__main__":
    t1 = threading.Thread(target=run_tcp_server)
    t2 = threading.Thread(target=run_udp_listener)
    t3 = threading.Thread(target=run_control_server)
    
    t1.start()
    t2.start()
    t3.start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        server_running = False
        t1.join()
        t2.join()
        t3.join()
