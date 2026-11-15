set -e
O=/mnt/d/yizhishouji/build/payload-r31; mkdir -p $O /root/probe; cd /root/probe
cp /mnt/d/yizhishouji/build/payload-ci8/payload/payload.img $O/payload.img
timeout 120 mcopy -n -o -i $O/payload.img ::/EFI/UniXNU/Payloads/kernel.macho kernel.macho
cp kernel.macho orig42.macho
cp /mnt/d/yizhishouji/work/entry-probe.py /mnt/d/yizhishouji/work/entry-probe2.py .
UNIXNU_UEFI=/root/uefi-asm python3 entry-probe.py kernel.macho --no-reset --no-stage2 --panic-screen
timeout 60 mcopy -n -o -i $O/payload.img ::/EFI/UniXNU/Payloads/kernel.macho.adt adt.bin
python3 - <<'EOF'
import struct
d=bytearray(open('adt.bin','rb').read()); i=d.find(b'darwinboot-memory-size\0'); assert i>0 and struct.unpack_from('<I',d,i+32)[0]==8
print('window',hex(struct.unpack_from('<Q',d,i+36)[0]),'->0x80000000'); struct.pack_into('<Q',d,i+36,0x80000000); open('adt.bin','wb').write(d)
EOF
timeout 60 mcopy -n -o -i $O/payload.img adt.bin ::/EFI/UniXNU/Payloads/kernel.macho.adt
timeout 120 mcopy -n -o -i $O/payload.img kernel.macho ::/EFI/UniXNU/Payloads/kernel.macho
timeout 60 mtype -i $O/payload.img ::/EFI/UniXNU/unixnu.toml < /dev/null | sed 's/kt_fixed_zones=120"/kt_fixed_zones=120 -no_slto_panic use_contiguous_hint=0"/' > unixnu.toml
grep '^args' unixnu.toml
timeout 60 mcopy -n -o -i $O/payload.img unixnu.toml ::/EFI/UniXNU/unixnu.toml < /dev/null
sed -e 's#payload-r2#payload-r31#; s#r13-probe#r42-probe#' /mnt/d/yizhishouji/src/UniXNU-houji-ios27/scripts/uefi/mk13.py > mk14.py
/root/uvenv/bin/python3 mk14.py