import sys, struct, os
def parse(path):
    d = open(path, 'rb').read(); off = 4; recs = []
    while off + 12 <= len(d):
        k, l1, l2 = struct.unpack_from('<III', d, off); recs.append((k, d[off+12:off+12+l1])); off += 12 + l1
    return recs
def show(v):
    if v and all(32 <= b < 127 for b in v): return v.decode()[:60]
    return v.hex()
tasks = sys.argv[1:]
rows = {t: dict(parse(f"snapshots/bgft/user_bgft/task/{t}/d0.pdb")) for t in tasks}
order = [k for k, _ in parse(f"snapshots/bgft/user_bgft/task/{tasks[0]}/d0.pdb")]
allk = order + sorted(set().union(*[set(r) for r in rows.values()]) - set(order))
for k in allk:
    vals = [show(rows[t].get(k, b'<none>')) if k in rows[t] else '-' for t in tasks]
    mark = '' if len(set(vals)) == 1 else ' *'
    print(f"{k:#7x}{mark} | " + ' | '.join(v[:40] for v in vals))
