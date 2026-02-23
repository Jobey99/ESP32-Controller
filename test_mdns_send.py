import socket

MCAST_GRP = '224.0.0.251'
MCAST_PORT = 5353

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)

# A simple raw mDNS payload asking for _dante._udp.local
# Transaction ID: 0x0000, Flags: 0x0000, Questions: 1, Answers rrs: 0, Authority rrs: 0, Additional rrs: 0
# Query: _dante._udp.local type PTR, class IN
payload = bytes.fromhex("000000000001000000000000065f64616e7465045f756470056c6f63616c00000c0001")

print(f"Sending raw mDNS query to {MCAST_GRP}:{MCAST_PORT}...")
sock.sendto(payload, (MCAST_GRP, MCAST_PORT))
print("Sent.")
