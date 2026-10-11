#!/usr/bin/env python3
# Register state at the first draw: RADV vs Samsung's driver, as two replayed register files.
#
#   python3 firstdraw-regdiff.py radv.txt samsung_dir/     (run from the Mesa source root)
#
# radv.txt: RADV_DEBUG=dumpibs output (LD_PRELOAD=libnosubmit.so is enough), with the colour codes
# removed, plus RADV's gfx_init buffer as "XCLINIT <dword>" lines. The init buffer is jumped to with
# INDIRECT_BUFFER and isn't in the dump; print it with a temporary hook after the memcpy in
# radv_device.c (gfx_init creation):  for (i < cs->b->cdw) fprintf(stderr, "XCLINIT %08x\n", buf[i]);
# samsung_dir: ib_*.bin from capture/ibdump.c (IBDUMP=log with the Android test builds).
# Registers that differ per run (addresses, shader pointers, user data, ...) are skipped.
import json, re, struct, sys, glob
d = json.load(open("src/amd/registers/gfx103.json"))
a2n = {}
for r in d["register_mappings"]:
    if r["map"]["to"] == "mm": a2n.setdefault(r["map"]["at"], r["name"])
BASE = {0x69: 0x28000, 0x76: 0xB000, 0x9B: 0xB000, 0x79: 0x30000, 0x7A: 0x30000}
DRAWS = {0x2d, 0x2e, 0x27, 0x38, 0x35}  # DRAW_INDEX_AUTO, DRAW_INDEX_IMMD, DRAW_INDEX_2, DRAW_INDIRECT, ...
def walk(words, state):
    i = 0
    while i < len(words):
        h = words[i]
        if h >> 30 == 3:
            n = ((h >> 16) & 0x3fff) + 1; op = (h >> 8) & 0xff
            if op in BASE and i + 1 < len(words):
                reg = (words[i + 1] & 0xffff) * 4 + BASE[op]
                for k, v in enumerate(words[i + 2: i + 1 + n]): state[a2n.get(reg + 4 * k, hex(reg + 4 * k))] = v
            if op == 0x12: state["<CLEAR_STATE>"] = 1
            if op in DRAWS: return True
            i += n + 1
        else: i += 1
    return False
radv_txt, sam_dir = sys.argv[1], sys.argv[2]
txt = open(radv_txt).read().splitlines()
init = [int(l.split()[1], 16) for l in txt if l.startswith("XCLINIT ")]
radv = {}; walk(init, radv)
# RADV text sections: take register writes by name, stop at first draw packet in a Main IB
sec = None; drew = False
for l in txt:
    if "IB begin" in l: sec = l
    m = re.match(r"^[0-9a-f]{8} ([A-Z0-9_]+):", l)
    if m and m.group(1).startswith("DRAW") and sec and "Main" in sec: drew = True; break
    m = re.match(r"^[0-9a-f]{8}\s+([A-Z][A-Z0-9_]+) <- (0x[0-9a-f]+|-?\d+|[-0-9.e]+f)", l)
    if m and sec:
        v = m.group(2)
        try: v = int(v, 0) & 0xffffffff
        except ValueError: v = struct.unpack("<I", struct.pack("<f", float(v[:-1])))[0]
        radv[m.group(1)] = v
sam = {}
for f in sorted(glob.glob(sam_dir + "/ib_*.bin")):
    b = open(f, "rb").read()
    if walk(list(struct.unpack("<%dI" % (len(b) // 4), b[: len(b) // 4 * 4])), sam): break
skip = re.compile(r"PGM_LO|PGM_HI|USER_DATA|_BASE|BASE_|ADDR|SCISSOR|VPORT|WINDOW_OFFSET|_DIM_|NUM_THREAD|DISPATCH_INITIATOR|CB_COLOR.*(VIEW|PITCH|SLICE|ATTRIB|INFO)|DB_.*(SIZE|INFO|READ|WRITE)|COHER|USER_ACCUM|NUM_INDICES|NUM_INSTANCES|INDEX_TYPE|PRIMITIVE_TYPE|TMPRING")
def hx(v): return "-" if v is None else hex(v)
rows = []
for n in sorted(set(radv) | set(sam)):
    if skip.search(n): continue
    rv, sv = radv.get(n), sam.get(n)
    if rv != sv: rows.append((("only-sam" if rv is None else "only-radv" if sv is None else "differ"), n, hx(rv), hx(sv)))
for kind in ("only-sam", "differ", "only-radv"):
    sel = [r for r in rows if r[0] == kind]
    print(f"== {kind}: {len(sel)}")
    for _, n, rv, sv in sel: print(f"   {n:40s} radv={rv:12s} samsung={sv}")
