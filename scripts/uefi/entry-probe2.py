"""r14: extra progress bars inside start_first_cpu (run AFTER entry-probe.py on the same kernel copy)."""
import importlib.util, struct, subprocess, sys, tempfile
from pathlib import Path
spec = importlib.util.spec_from_file_location('ep', '/mnt/d/yizhishouji/work/entry-probe.py')
ep = importlib.util.module_from_spec(spec); spec.loader.exec_module(ep)
kern = Path(sys.argv[1])
data = bytearray(kern.read_bytes())
ep.load_segments(data)
delta = ep.unixthread_pc(data) - ep.ENTRY_VA
SFC = {n: v for v, n in ep.kernel_symbols(kern).items()}['_start_first_cpu']
CAVES = {s: v + delta for s, v in ep.CAVES.items()}
# (offset from start_first_cpu, band, args register, expected mnemonic text)
SITES = [
 (0x08, 3,  'x0',  'mov\tx20, x0'),            # after OSLAR_EL1 + DAIFSet
 (0x1c, 4,  'x20', 'ldr\tx22, [x20, #8]'),     # after VBAR_EL1
 (0x78, 5,  'x20', 'mov\tx0, #0'),             # after stacks set up
 (0x9c, 6,  'x20', 'ldr\tw1, [x0, #24]'),      # after page-table zeroing
 (0x250,8,  'x20', 'and\tx0, x25, #0xfffffffffffe'),  # after TCR_EL1
 (0x25c,9,  'x20', 'mov\tx30, x19'),           # after helper bl
 (0x26c,10, 'x20', 'mov\tx0, xzr'),            # after TTBR1_EL1
 (0x2b4,11, 'x20', 'isb'),                     # after MAIR_EL1
 (0x36c,12, 'x20', 'ldp\tx2, x3, [x0]'),       # before PAC key writes
 (0x3a8,13, 'x20', 'isb'),                     # after PAC key writes
 (0x23c, 14, 'x20', 'movk\tx1, #8, lsl #48'),     # after dsb ish (colour = CurrentEL: white=EL2, green=EL1)
 (0x24c, 15, 'x20', 'msr\tTCR_EL1, x1'),         # just before TCR_EL1 write
]
COLORS = [(0,0),(0,0),(0,0),(0xFFFF,0xFF00),(0x00FF,0xFFFF),(0xFF00,0xFFFF),(0x0000,0xFFFF),(0xFFFF,0xFFFF),(0x00FF,0xFF00),(0xFF00,0xFF00),(0x00FF,0xFFFF),(0xFF00,0xFFFF),(0xFFFF,0xFF00),(0xFFFF,0xFFFF),(0xFF00,0xFF00),(0x00FF,0xFF00)]
PAINT = """
  ldr x7, [{args}, #0x28]
  ldr x8, [{args}, #0x38]
  ldr x16, [{args}, #0x48]
  mul x8, x8, x16
  lsr x8, x8, #4
  bic x8, x8, #7
  mov x17, #{band}
  mul x27, x8, x17
  add x7, x7, x27
  movz w28, #{lo}
  movk w28, #{hi}, lsl #16
{extra}1:
  stp w28, w28, [x7], #8
  subs x8, x8, #8
  b.hi 1b
  dsb sy
{reset}  .word {orig}
  b target
"""
RST = '  mrs x1, cntpct_el0\n  ldr x2, =57600000\n  add x2, x1, x2\n1:\n  isb\n  mrs x1, cntpct_el0\n  cmp x1, x2\n  b.lo 1b\n  movz w0, #0x0009\n  movk w0, #0x8400, lsl #16\n  mov x1, xzr\n  mov x2, xzr\n  mov x3, xzr\n  smc #0\n2:\n  wfe\n  b 2b\n  .ltorg\n'
EL = '  mrs x29, CurrentEL\n  cmp x29, #8\n  movz w16, #0xFFFF\n  movk w16, #0xFFFF, lsl #16\n  csel w28, w16, w28, eq\n'
base = CAVES[4] + ep.CAVE_SIZE
mc = 'llvm-mc'
for n, (off, band, args, text) in enumerate(SITES):
    site = SFC + off
    w = struct.unpack_from('<I', data, ep.file_offset(site))[0]
    hexs = ' '.join('0x%02x' % b for b in struct.pack('<I', w))
    dis = subprocess.run([mc, '--disassemble', '-triple=arm64e', '-mattr=+v8.5a,+pauth'], input=hexs, capture_output=True, text=True).stdout
    dis = [l.strip() for l in dis.splitlines() if l.strip() and not l.strip().startswith('.')][0]
    if dis.split('//')[0].split() != text.split():
        raise SystemExit(f'{site:#x}: found "{dis}" expected "{text}"')
    cave = base + n * 0x60
    fo = ep.file_offset(cave)
    if any(data[fo:fo + 0x60]):
        raise SystemExit(f'cave {cave:#x} not empty')
    lo, hi = COLORS[band]
    src = '.text\n.global _start\n_start:\n' + PAINT.format(args=args, band=band, lo=hex(lo), hi=hex(hi), orig=hex(0xD503201F if band == 7 else w), extra=EL if band == 14 else '', reset='')
    with tempfile.TemporaryDirectory() as tmp:
        code = ep.assemble(src, cave, {'target': site + 4}, Path(tmp))
    if len(code) > 0x60:
        raise SystemExit('cave too big')
    data[fo:fo + len(code)] = code
    struct.pack_into('<I', data, ep.file_offset(site), ep.branch(site, cave))
    print(f'band {band}: site {site:#x} ({dis}) -> cave {cave:#x}, {len(code)} bytes')
kern.write_bytes(data)
