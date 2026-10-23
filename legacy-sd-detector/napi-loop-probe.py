#!/usr/bin/env python3
# Non-invasive single-port L2 loopback probe.
# Sends a few raw Ethernet frames (custom ethertype + random nonce) FROM the
# interface's own MAC, then listens briefly for that exact frame to come back
# IN on the same interface. A real switch never reflects a frame to its source
# port, so an inbound echo means the TX pair is physically looped to the RX pair.
# Touches no IP configuration -> safe to run on a live, in-use interface.
# Exit 0 = loopback detected, 1 = no loopback, 2 = error.
import socket, sys, os, struct, time

IF = sys.argv[1] if len(sys.argv) > 1 else "end0"
ETH_P_ALL = 0x0003
ETHERTYPE = 0x88B5                     # IEEE local-experimental ethertype
PACKET_OUTGOING = 4
LISTEN_S = float(os.environ.get("PROBE_LISTEN", "1.0"))

def mac_bytes(ifname):
    with open(f"/sys/class/net/{ifname}/address") as f:
        return bytes.fromhex(f.read().strip().replace(":", ""))

try:
    src = mac_bytes(IF)
except Exception as e:
    print(f"probe: cannot read MAC of {IF}: {e}", file=sys.stderr)
    sys.exit(2)

nonce = os.urandom(8)
payload = b"NAPILOOP" + nonce
frame = b"\xff\xff\xff\xff\xff\xff" + src + struct.pack("!H", ETHERTYPE) + payload
if len(frame) < 60:
    frame += b"\x00" * (60 - len(frame))

try:
    s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(ETH_P_ALL))
    s.bind((IF, 0))
except Exception as e:
    print(f"probe: cannot open socket on {IF}: {e}", file=sys.stderr)
    sys.exit(2)

s.settimeout(0.3)
for _ in range(3):
    try:
        s.send(frame)
    except Exception:
        pass

found = False
deadline = time.time() + LISTEN_S
while time.time() < deadline:
    try:
        data, addr = s.recvfrom(2048)
    except socket.timeout:
        continue
    except Exception:
        break
    # addr = (ifname, proto, pkttype, hatype, hwaddr)
    if addr[2] == PACKET_OUTGOING:
        continue                        # ignore our own transmitted copy
    if len(data) >= 14 + len(payload):
        if struct.unpack("!H", data[12:14])[0] == ETHERTYPE and \
           data[14:14 + len(payload)] == payload:
            found = True
            break

s.close()
sys.exit(0 if found else 1)
