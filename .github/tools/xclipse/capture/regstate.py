#!/usr/bin/env python3
"""Full register state at a chosen draw in two decoded IB dumps (ac_ib_parser / RADV dumpibs format).
usage: regstate.py <a.txt> <draw#|ps:<pgm_lo>> <b.txt> <draw#|ps:<pgm_lo>>   -> prints differing registers"""
import re, sys

def state_at(path, sel):
    st, cur, n = {}, None, 0
    for ln in open(path).read().splitlines():
        m = re.match(r'^[0-9a-f]{8}\s+([A-Z0-9_]+|0x[0-9a-f]+) <- (\S+)', ln)
        if m:
            st[m.group(1)] = m.group(2); continue
        if 'DRAW_INDEX' in ln and 'VGT' not in ln:
            n += 1
            if (sel.startswith('ps:') and st.get('SPI_SHADER_PGM_LO_PS') == sel[3:]) or sel == str(n):
                return dict(st)
    return st

a = state_at(sys.argv[1], sys.argv[2]); b = state_at(sys.argv[3], sys.argv[4])
skip = re.compile(r'(BASE|ADDR|PGM_LO|PGM_HI|USER_DATA|_LO$|_HI$|SCISSOR|VPORT|VIEWPORT|WINDOW|SCREEN|GUARDBAND|CLEAR|ZRANGE|OFFSET|SIZE|SLICE|PITCH|CB_COLOR\d_(VIEW|ATTRIB|FDCC|DCC|CMASK|FMASK))')
for k in sorted(a.keys() | b.keys()):
    if skip.search(k): continue
    if a.get(k) != b.get(k):
        print(f'{k:40s} samsung={a.get(k, "-"):14s} radv={b.get(k, "-")}')
