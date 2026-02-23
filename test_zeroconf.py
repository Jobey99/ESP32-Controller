from zeroconf import Zeroconf, ServiceBrowser
import time

class MyListener:
    def remove_service(self, zeroconf, type, name): pass
    def update_service(self, zeroconf, type, name): pass
    def add_service(self, zeroconf, type, name):
        info = zeroconf.get_service_info(type, name)
        print("FOUND:", name)
        
print("Scanning for _http._tcp.local. and _dante._udp.local. (10 seconds)...")
zc = Zeroconf()
browser1 = ServiceBrowser(zc, "_http._tcp.local.", MyListener())
browser2 = ServiceBrowser(zc, "_dante._udp.local.", MyListener())

time.sleep(10)
browser1.cancel()
browser2.cancel()
zc.close()
print("Done")
