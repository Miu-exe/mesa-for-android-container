#!/usr/bin/env python3
"""Decode AMD buffer (V#) / image (T#) descriptors with Mesa's gfx*-rsrc.json register dbs.
usage: rsrcdec.py <gfx10|gfx11> <buf|img|samp> <hex dwords...>"""
import json, os, sys
# Mesa's register database, relative to this file (.github/tools/xclipse/capture -> src/amd/registers)
REG = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', '..', 'src', 'amd', 'registers') + os.sep
gen, kind, words = sys.argv[1], sys.argv[2], [int(w, 16) for w in sys.argv[3:]]
db = json.load(open(REG + gen + '-rsrc.json'))
prefix = {'buf': 'SQ_BUF_RSRC_WORD', 'img': 'SQ_IMG_RSRC_WORD', 'samp': 'SQ_IMG_SAMP_WORD'}[kind]
seen = set()
for m in db['register_mappings']:
    if not m['name'].startswith(prefix): continue
    idx = int(m['name'][-1])
    if 'type_ref' not in m or idx >= len(words) or (idx, m['type_ref']) in seen: continue
    seen.add((idx, m['type_ref']))
    ty = db['register_types'][m['type_ref']]
    val = words[idx]
    fields = []
    for f in ty['fields']:
        lo, hi = f['bits']
        v = (val >> lo) & ((1 << (hi - lo + 1)) - 1)
        if not v: continue
        s = f"{f['name']}={v}"
        if 'enum_ref' in f:
            en = {e['value']: e['name'] for e in db['enums'][f['enum_ref']]['entries']}
            s += f"({en.get(v, '?')})"
        fields.append(s)
    print(f"  w{idx} {val:08x} [{m['type_ref']}] " + ' '.join(fields))
