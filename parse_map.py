import sys
bss_start = False
sizes = []
for line in open('build/espclaw.map'):
    if line.startswith('.dram0.bss'): bss_start = True
    if line.startswith('.dram0.data'): bss_start = False
    if not bss_start: continue
    parts = line.split()
    if len(parts) >= 3 and parts[0].startswith('0x') and parts[1].startswith('0x') and 'espclaw/build' in line:
        try:
            sz = int(parts[1], 16)
            if sz > 500: sizes.append((sz, parts[-1]))
        except: pass
for sz, name in sorted(sizes, reverse=True)[:20]: print(f"{sz:6d} {name.split('/')[-1]}")
