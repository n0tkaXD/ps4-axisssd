#!/usr/bin/env python3
"""Read-only snapshot of PS4 system folders over GoldHEN FTP.

Usage: ps4snap.py <host> <port> <out_dir>
Writes <out_dir>/listing.txt (path, type, size, date for every entry) and
downloads small system files (databases, licenses, appmeta, playgo records).
Never writes to the console.
"""
import ftplib
import os
import sys

WALK = [
    "/user/app",
    "/user/appmeta",
    "/user/license",
    "/user/playgo",
    "/user/patch",
    "/user/addcont",
    "/system_data/playgo",
    "/system_data/priv/appmeta",
    "/system_data/priv/mms",
]
# Files under these prefixes are downloaded if smaller than MAX_DL.
DOWNLOAD = [
    "/system_data/priv/mms/",
    "/user/license/",
    "/user/playgo/",
    "/system_data/playgo/",
    "/system_data/priv/appmeta/",
]
MAX_DL = 64 << 20
SKIP_DL_NAMES = {"app.pkg", "patch.pkg", "ac.pkg"}


def parse_list(lines):
    out = []
    for ln in lines:
        parts = ln.split(None, 8)
        if len(parts) < 9:
            continue
        perm, size, name = parts[0], parts[4], parts[8]
        if name in (".", ".."):
            continue
        date = " ".join(parts[5:8])
        target = None
        if perm.startswith("l") and " -> " in name:
            name, target = name.split(" -> ", 1)
        out.append((perm[0], int(size) if size.isdigit() else -1, date, name, target))
    return out


def listdir(ftp, path):
    lines = []
    try:
        ftp.cwd(path)
        ftp.retrlines("LIST", lines.append)
    except ftplib.error_perm as e:
        return None, str(e)
    return parse_list(lines), None


def main():
    host, port, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    os.makedirs(out, exist_ok=True)
    ftp = ftplib.FTP()
    ftp.connect(host, port, timeout=30)
    ftp.login()
    ftp.set_pasv(True)
    listing = open(os.path.join(out, "listing.txt"), "w")
    stack = list(reversed(WALK))
    n_dl = 0
    while stack:
        path = stack.pop()
        entries, err = listdir(ftp, path)
        if entries is None:
            listing.write(f"{path}\tERR\t{err}\n")
            continue
        for kind, size, date, name, target in sorted(entries, key=lambda e: e[3]):
            full = path.rstrip("/") + "/" + name
            extra = f"\t-> {target}" if target else ""
            listing.write(f"{full}\t{kind}\t{size}\t{date}{extra}\n")
            if kind == "d":
                stack.append(full)
            elif kind == "-" and name not in SKIP_DL_NAMES and 0 <= size <= MAX_DL \
                    and any(full.startswith(p) for p in DOWNLOAD):
                local = os.path.join(out, "files", full.lstrip("/"))
                os.makedirs(os.path.dirname(local), exist_ok=True)
                with open(local, "wb") as f:
                    ftp.retrbinary("RETR " + full, f.write)
                n_dl += 1
    listing.close()
    ftp.quit()
    print(f"done: {out} ({n_dl} files downloaded)")


if __name__ == "__main__":
    main()
