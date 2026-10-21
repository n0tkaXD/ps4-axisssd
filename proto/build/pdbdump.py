import sys, struct
def dump(path):
    d = open(path, 'rb').read()
    print(f"== {path} ({len(d)} bytes) head={d[:4].hex()}")
    off = 4
    while off + 12 <= len(d) - 4:
        key, l1, l2 = struct.unpack_from('<III', d, off)
        val = d[off+12:off+12+l1]
        txt = val.decode('latin1') if val and all(32 <= b < 127 for b in val) else val.hex()
        print(f"  {key:#x} len={l1}/{l2} {txt[:110]}")
        off += 12 + l1
    print(f"  trailer {d[off:].hex()}")
for p in sys.argv[1:]:
    dump(p)
