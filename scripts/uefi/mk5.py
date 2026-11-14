import struct, subprocess, hashlib, sys
src='/mnt/d/yizhishouji/build/uefi-r5/Mu-houjif-0.img'
b=open(src,'rb').read()
assert b[:8]==b'ANDROID!'
ks,rs=struct.unpack_from('<II',b,8)
print('header_version',struct.unpack_from('<I',b,40)[0],'kernel',ks,'ramdisk',rs)
open('/tmp/kernel','wb').write(b[4096:4096+ks])
out='/mnt/d/yizhishouji/build/bootimg-r1/boot-houji-r5-payload1.img'
import os; os.makedirs(os.path.dirname(out),exist_ok=True)
subprocess.run(['/root/uvenv/bin/python3','/root/uefi-asm/Resources/Scripts/mkbootimg.py','--kernel','/tmp/kernel','--ramdisk','/mnt/d/yizhishouji/build/payload-r1/payload/payload.img','--header_version','4','--os_version','16.0.0','--os_patch_level','2026-08','-o',out],check=True)
d=open(out,'rb').read(); print(len(d),hashlib.sha256(d).hexdigest()); print(struct.unpack_from('<III',d,8))