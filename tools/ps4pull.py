#!/usr/bin/env python3
"""Download a PS4 folder tree over GoldHEN FTP (read-only). Usage: ps4pull.py host port remote_dir local_dir"""
import ftplib, os, sys
host, port, rdir, ldir = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
f = ftplib.FTP(); f.connect(host, port, timeout=60); f.login()
n = 0
def walk(r, l):
    global n
    L = []; f.cwd(r); f.retrlines("LIST", L.append)
    os.makedirs(l, exist_ok=True)
    for line in L:
        x = line.split(None, 8)
        if len(x) < 9 or x[8] in (".", ".."): continue
        if x[0][0] == "d": walk(r + "/" + x[8], os.path.join(l, x[8]))
        elif x[0][0] == "-":
            with open(os.path.join(l, x[8]), "wb") as o: f.retrbinary("RETR " + r + "/" + x[8], o.write)
            n += 1
walk(rdir, ldir); f.quit(); print("done", n, "files")
