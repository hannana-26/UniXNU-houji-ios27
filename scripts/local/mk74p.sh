#!/usr/bin/env bash
# r74p = r74 KERNEL BYTES UNCHANGED + one arg changed: houji-logfs-period=1 -> 65535 (local FAT swap, no CI).
# Purpose (2026-11-27 audit): the "short session" plan cannot keep early kernel lines - the record is only the
# newest 60 KiB of msgbuf and the boot burst alone exceeds that before the desktop (r29: 47,810 B by kernel 60 s).
# BootLog.c:2010 commits on the first writer round unconditionally, then only every <period> rounds (2 s each).
# period=65535 => exactly ONE commit, made right after UFS comes up (r29: first commit = 14,561 B, already holding
# "vm_page_bootstrap: N free pages"). 2 GiB gave 493,228 free pages; 8 GiB should give ~2,000,000.
set -e
P=/mnt/d/yizhishouji/build/payload-r74/payload.img
O=/tmp/r74p; mkdir -p $O
cp $P $O/payload.img
timeout 60 mcopy -n -o -i $O/payload.img ::/EFI/UniXNU/unixnu.toml /root/u74.toml
python3 - <<'PYEOF'
import re
p='/root/u74.toml'; s=open(p,'rb').read().decode('utf-8')
m=re.search(r'^args = "(.*)"', s, re.M); a=m.group(1)
assert a.count('houji-logfs-period=1 ')==1 or a.endswith('houji-logfs-period=1'), a
assert 'houji-logfs=1' in a and 'houji-logfs-recovery' not in a and 'data-reset' not in a
b=re.sub(r'houji-logfs-period=1(?=\s|$)','houji-logfs-period=65535',a)
assert b!=a and 'houji-logfs-period=65535' in b and len(b.split())==len(a.split())
s=s[:m.start(1)]+b+s[m.end(1):]
open(p,'wb').write(s.encode('utf-8')); print('args ok, count', len(b.split()))
PYEOF
timeout 60 mcopy -o -i $O/payload.img /root/u74.toml ::/EFI/UniXNU/unixnu.toml
timeout 60 mtype -i $O/payload.img ::/EFI/UniXNU/unixnu.toml < /dev/null | grep '^args' | tee /root/r74pargs
grep -q 'houji-logfs-period=65535' /root/r74pargs || { echo FATAL; exit 1; }
# kernel identity: everything except unixnu.toml must be byte-identical to payload-r74
for f in $(mdir -/ -b -i $P ::/ | grep -v unixnu.toml); do
  case "$f" in */) continue;; esac
  a=$(mtype -i $P "$f" </dev/null | sha256sum | cut -c1-16); b=$(mtype -i $O/payload.img "$f" </dev/null | sha256sum | cut -c1-16)
  [ "$a" = "$b" ] || { echo "FATAL: $f differs"; exit 1; }
done
echo 'payload files other than unixnu.toml: identical to r74'
/root/uvenv/bin/python3 /mnt/d/yizhishouji/work/mk73h.py $O/payload.img r74p-mem-firstcommit
