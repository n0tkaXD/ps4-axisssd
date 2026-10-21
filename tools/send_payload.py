#!/usr/bin/env python3
"""Send a payload to GoldHEN BinLoader the way `nc -q0` does: send all bytes,
close the write side right away, then wait for the server to close.
Usage: send_payload.py <host> <port> <file>"""
import socket
import sys

host, port, path = sys.argv[1], int(sys.argv[2]), sys.argv[3]
data = open(path, "rb").read()
s = socket.create_connection((host, port), timeout=10)
s.sendall(data)
s.shutdown(socket.SHUT_WR)
try:
    while s.recv(4096):
        pass
except socket.timeout:
    pass
s.close()
print(f"sent {len(data)} bytes to {host}:{port}")
