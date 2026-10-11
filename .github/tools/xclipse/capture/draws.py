import re, sys
# Print, for every DRAW in a decoded IB dump (ac_ib_parser / RADV_DEBUG=dumpibs output), the
# DB/SPI/PA registers in effect. usage: draws.py <decoded ibs.txt>
# For each DRAW packet, print the last value (and decoded fields) of interesting registers before it.
regs = ["PA_SC_MODE_CNTL_1", "PA_SC_AA_CONFIG", "DB_ALPHA_TO_MASK", "SPI_PS_IN_CONTROL", "DB_EQAA", 'SPI_SHADER_PGM_LO_PS', 'DB_SHADER_CONTROL', 'SPI_SHADER_Z_FORMAT', 'SPI_SHADER_COL_FORMAT',
        'DB_RENDER_CONTROL', 'DB_DEPTH_CONTROL', 'DB_STENCIL_CONTROL', 'DB_COUNT_CONTROL', 'CB_SHADER_MASK',
        'SPI_PS_INPUT_ENA', 'SPI_PS_INPUT_ADDR', 'PA_SC_SHADER_CONTROL', 'DB_Z_INFO', 'DB_STENCIL_INFO']
state, fields, cur = {}, {}, None
lines = open(sys.argv[1]).read().splitlines()
n = 0
for ln in lines:
    m = re.match(r'^[0-9a-f]{8}\s+([A-Z0-9_]+) <- (\S+)', ln)
    if m:
        cur = m.group(1)
        if cur in regs:
            state[cur] = m.group(2); fields[cur] = []
        continue
    m = re.match(r'^\s{20,}([A-Z0-9_]+) = (\S+)', ln)
    if m and cur in regs and m.group(2) not in ('0',):
        fields[cur].append(f'{m.group(1)}={m.group(2)}')
        continue
    if 'DRAW_INDEX' in ln and 'VGT' not in ln:
        n += 1
        print(f'--- draw {n}: {ln.strip()}')
        for r in regs:
            if r in state:
                print(f'   {r:22s} {state[r]:12s} {" ".join(fields[r])}')
