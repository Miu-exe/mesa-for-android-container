#!/usr/bin/env python3
"""Build a 'reg value' file of context registers where Samsung's state differs from RADV's at a draw.
usage: mkregfile.py <samsung ibs.txt> <sel> <radv dump> <sel> > regs.txt"""
import json, re, sys
R = __import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)), '..', '..', '..', '..', 'src', 'amd', 'registers') + '/'
addr = {}
for f in ('gfx103.json', 'gfx11.json'):
    for m in json.load(open(R + f))['register_mappings']:
        if 'map' in m and m['map'].get('to') == 'mm':
            addr.setdefault(m['name'], m['map']['at'])

def state_at(path, sel):
    st, n = {}, 0
    for ln in open(path).read().splitlines():
        m = re.match(r'^[0-9a-f]{8}\s+([A-Z0-9_]+|0x[0-9a-f]+) <- (\S+)', ln)
        if m:
            raw = ln.split()[0]
            st[m.group(1)] = int(raw, 16)
            continue
        if 'DRAW_INDEX' in ln and 'VGT' not in ln:
            n += 1
            if (sel.startswith('ps:') and st.get('SPI_SHADER_PGM_LO_PS') == int(sel[3:], 16)) or sel == str(n):
                return dict(st)
    return st

skip = re.compile(r'^(CB_COLOR|CB_BLEND|CB_SHADER_MASK|CB_TARGET_MASK|DB_Z_|DB_STENCIL_(INFO|READ|WRITE)|DB_DEPTH_(VIEW|SIZE|INFO)|DB_HTILE|'
                  r'PA_SC_(VPORT|WINDOW|SCREEN|GENERIC|CLIPRECT)|PA_CL_(VPORT|GB|NGG|VS_OUT|VTE|UCP)|SPI_PS_INPUT|SPI_SHADER_(Z|COL)_FORMAT|'
                  r'SPI_VS_OUT|SPI_SHADER_POS|SPI_SHADER_IDX|VGT_|GE_|SPI_PS_IN_CONTROL|DB_SHADER_CONTROL|SX_)')
a = state_at(sys.argv[1], sys.argv[2]); b = state_at(sys.argv[3], sys.argv[4])
for k in sorted(a):
    ad = int(k, 16) if k.startswith('0x') else addr.get(k)
    if ad is None or not (0x28000 <= ad < 0x29000) or skip.search(k):
        continue
    if a[k] != b.get(k):
        print(f'0x{ad:05x} 0x{a[k]:08x}  # {k} radv={b.get(k)}')
