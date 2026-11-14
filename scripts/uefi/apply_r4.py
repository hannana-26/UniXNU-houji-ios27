#!/usr/bin/env python3
"""r4: add UniXNURamDiskDxe + DarwinBoot + SEC entry-arg HOB to the houjif platform in /root/uefi-asm."""
import shutil, sys
from pathlib import Path
U = Path('/root/uefi-asm')
X = U / 'Platforms/Xiaomi'
OFF = Path('/mnt/d/yizhishouji/src/UniXNU-official')
peridot = Path('/mnt/d/yizhishouji/src/community/UniXNU-peridot/scripts/apply-peridot-firmware.py')
# 1. DarwinBootPkg
dst = X / 'DarwinBootPkg'
if dst.exists(): shutil.rmtree(dst)
shutil.copytree(OFF / 'drivers/DarwinBoot/DarwinBootPkg', dst)
# 2. DSC components + FDF/inc
dsc = X / 'houjifPkg/houjif.dsc'
t = dsc.read_text()
add = ['UniXNUPkg/Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.inf', 'DarwinBootPkg/Driver/DarwinBoot.inf']
for a in add:
    if a not in t:
        t = t.replace('  UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf\n', '  UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf\n  ' + a + '\n', 1)
dsc.write_text(t)
inc = X / 'houjifPkg/Include/DXE.inc'
t = inc.read_text()
for a in add:
    line = '  INF ' + a + '\n'
    if line not in t:
        t = t.rstrip('\n') + '\n' + line
inc.write_text(t)
# 3. SEC entry-arg patch from peridot (patch_sec)
import importlib.util
spec = importlib.util.spec_from_file_location('ap', peridot)
ap = importlib.util.module_from_spec(spec); spec.loader.exec_module(ap)
ap.patch_sec(U)
print('r4 ok')