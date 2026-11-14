set -e
O=/mnt/d/yizhishouji/build/payload-r7; mkdir -p $O /root/probe; cd /root/probe
cp /mnt/d/yizhishouji/build/payload-r1/payload/payload.img $O/payload.img
timeout 120 mcopy -n -o -i $O/payload.img ::/EFI/UniXNU/Payloads/kernel.macho kernel.macho
cp /mnt/d/yizhishouji/work/entry-probe.py /mnt/d/yizhishouji/work/entry-probe2.py .
UNIXNU_UEFI=/root/uefi-asm python3 entry-probe.py kernel.macho --reset-delay 6
python3 entry-probe2.py kernel.macho
timeout 120 mcopy -n -o -i $O/payload.img kernel.macho ::/EFI/UniXNU/Payloads/kernel.macho
sed -e 's#payload-r2#payload-r7#; s#r13-probe#r18-probe#' /mnt/d/yizhishouji/src/UniXNU-houji-ios27/scripts/uefi/mk13.py > mk14.py
/root/uvenv/bin/python3 mk14.py