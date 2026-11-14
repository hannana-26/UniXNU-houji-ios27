#!/bin/sh
# r13: patch the CI kernel with the UniXNU entry probe (blue/magenta bars, PSCI reset 6 s after MMU on),
# replace it in a copy of the payload, then embed the payload in the kernel Image (mk13.py).
# entry-probe.py = UniXNU scripts/patch-xnu-entry-probe.py with fileset-tolerant llvm-nm parsing.
set -e
O=/mnt/d/yizhishouji/build/payload-r2; mkdir -p $O /root/probe; cd /root/probe
cp /mnt/d/yizhishouji/build/payload-r1/payload/payload.img $O/payload.img
mcopy -o -i $O/payload.img ::/EFI/UniXNU/Payloads/kernel.macho kernel.macho
UNIXNU_UEFI=/root/uefi-asm python3 entry-probe.py kernel.macho --reset-delay 6
mcopy -o -i $O/payload.img kernel.macho ::/EFI/UniXNU/Payloads/kernel.macho
/root/uvenv/bin/python3 mk13.py