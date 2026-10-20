import re, sys, collections
import json, os
if sys.argv[1]=="--diff":
    new=json.load(open("/tmp/sz_"+sys.argv[2]+".json")); old=json.load(open("/tmp/sz_"+sys.argv[3]+".json"))
    print("== diff new-old, KB (archives) ==")
    rows=sorted(((new.get(k,0)-old.get(k,0),k,new.get(k,0),old.get(k,0)) for k in set(new)|set(old)), reverse=True)
    for d,k,n,o in rows[:30]+rows[-8:]: print("%+7.1f  new %6.1f old %6.1f %s"%(d/1024,n/1024,o/1024,k))
    print("sum new %.1f old %.1f"%(sum(new.values())/1024,sum(old.values())/1024))
    sys.exit(0)
mp = sys.argv[1]
lines = open(mp, errors="replace").read().split("\n")
# locate sections
try:
    i0 = next(i for i,l in enumerate(lines) if l.startswith("Linker script and memory map"))
except StopIteration:
    i0 = 0
try:
    ic = next(i for i,l in enumerate(lines) if l.startswith("Cross Reference Table"))
except StopIteration:
    ic = len(lines)
sec_re = re.compile(r"^ (\.\S+)\s*$")
full_re = re.compile(r"^ (\.\S+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S.*)$")
cont_re = re.compile(r"^\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S.*)$")
sizes = collections.Counter(); objs = collections.Counter()
pending = None
for l in lines[i0:ic]:
    m = full_re.match(l)
    if m: name, addr, sz, path = m.groups(); pending=None
    else:
        m = sec_re.match(l)
        if m: pending = m.group(1); continue
        m = cont_re.match(l)
        if m and pending: name=pending; addr, sz, path = m.groups(); pending=None
        else: pending=None; continue
    sz = int(sz,16); addr=int(addr,16)
    if sz==0 or addr==0 or re.search(r"bss|noinit|COMMON|\.rtc\.|debug|\.comment|\.xt\.", name): continue
    if "(" in path: arch = path.split("(")[0].split("/")[-1]; ob = path.split("(")[1].rstrip(")")
    else: arch = path.split("/")[-1]; ob = arch
    if arch.endswith(".o") and ("/src/" in path or "/lib/" in path or "esp-wrover" in path): arch = "PROJECT:" + path.split("/")[-2] + "/" + arch
    sizes[arch]+=sz; objs[arch+"|"+ob]+=sz
import json, os
json.dump(dict(sizes), open("/tmp/sz_"+os.path.basename(mp)+".json","w"))
tot = sum(sizes.values())
print("TOTAL flash-ish bytes in map: %d (%.1f KB)" % (tot, tot/1024))
print("== by archive/object-dir (KB) ==")
for a,s in sizes.most_common(45): print("%7.1f %s" % (s/1024, a))
print("== top 40 objects (KB) ==")
for a,s in objs.most_common(40): print("%6.1f %s" % (s/1024, a))
# cref
cref = lines[ic:]
defs = {}  # symbol -> (defining file, [referrers])
cur = None
for l in cref:
    if not l.strip() or l.startswith("Cross") or l.startswith("Symbol"): continue
    if not l.startswith(" "):
        parts = l.split(None, 1)
        if len(parts)==1: cur=[parts[0], None, []]; defs[parts[0]]=cur; continue
        cur=[parts[0], parts[1].strip(), []]; defs[parts[0]]=cur
    elif cur:
        t=l.strip()
        if cur[1] is None: cur[1]=t
        else: cur[2].append(t)
def arch_of(p): return p.split("(")[0].split("/")[-1]
targets = sys.argv[2:]
for t in targets:
    agg = collections.defaultdict(set)
    for sym,(_n,d,refs) in defs.items():
        if not d or arch_of(d)!=t: continue
        for r in refs:
            if arch_of(r)!=t: agg[arch_of(r)+"("+(r.split("(")[1] if "(" in r else "")].add(sym)
    print("== referrers of %s (%d) ==" % (t, sizes.get(t,0)))
    for r,s in sorted(agg.items(), key=lambda x:-len(x[1]))[:12]: print("  %s <- %s" % (r[:70], ",".join(sorted(s))[:110]))
