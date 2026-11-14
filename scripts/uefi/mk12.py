import struct, subprocess, hashlib, zlib, gzip, os
src='/mnt/d/yizhishouji/build/uefi-r12/Mu-houjif-0.img'
pay=open('/mnt/d/yizhishouji/build/payload-r1/payload/payload.img','rb').read()
b=open(src,'rb').read(); assert b[:8]==b'ANDROID!'
ks=struct.unpack_from('<I',b,8)[0]; k=b[4096:4096+ks]
d=zlib.decompressobj(16+zlib.MAX_WBITS); raw=d.decompress(k); dtb=d.unused_data
assert dtb[:4]==b'\xd0\x0d\xfe\xed', dtb[:4]
assert raw[56:60]==b'ARM\x64', raw[56:60]
raw=raw+bytes((-len(raw))%4096)+pay
assert len(raw)<=0x10000000
raw=bytearray(raw); struct.pack_into('<Q',raw,16,len(raw)); raw=bytes(raw)
kern=gzip.compress(raw,9,mtime=0)+dtb
open('/tmp/kernel12','wb').write(kern)
out='/mnt/d/yizhishouji/build/bootimg-r1/boot-houji-r12-embed.img'
subprocess.run(['/root/uvenv/bin/python3','/root/uefi-asm/Resources/Scripts/mkbootimg.py','--kernel','/tmp/kernel12','--header_version','4','--os_version','16.0.0','--os_patch_level','2026-08','-o',out],check=True)
o=open(out,'rb').read(); print('raw',len(raw),'kernel',len(kern),'img',len(o),hashlib.sha256(o).hexdigest()); print(struct.unpack_from('<III',o,8))