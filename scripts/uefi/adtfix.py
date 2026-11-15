import struct,sys
src,dst=sys.argv[1],sys.argv[2]
d=open(src,'rb').read()
def parse(o):
    np,nc=struct.unpack_from('<II',d,o); o+=8; props=[]
    for _ in range(np):
        name=d[o:o+32].split(b'\0')[0].decode(); raw=struct.unpack_from('<I',d,o+32)[0]; l=raw&0x7fffffff
        props.append([name,d[o+36:o+36+l],raw&0x80000000]); o+=36+((l+3)&~3)
    kids=[]
    for _ in range(nc):
        k,o=parse(o); kids.append(k)
    return {'p':props,'k':kids},o
def name(n): return next(v for k,v,f in n['p'] if k=='name').split(b'\0')[0].decode()
def ser(n):
    out=struct.pack('<II',len(n['p']),len(n['k']))
    for k,v,f in n['p']:
        out+=k.encode().ljust(32,b'\0')+struct.pack('<I',len(v)|f)+v+b'\0'*((-len(v))%4)
    for c in n['k']: out+=ser(c)
    return out
root,end=parse(0); assert end==len(d),(end,len(d))
kid={name(c):c for c in root['k']}
print('top nodes',list(kid))
aio=kid['arm-io']
for p in aio['p']:
    if p[0]=='ranges': print('old ranges',struct.unpack('<QQQ',p[1])); p[1]=struct.pack('<QQQ',0,0x17000000,0x1000000)
assert not any(name(c)=='intc' for c in aio['k'])
aio['k'].append({'p':[['name',b'intc\0',0],['compatible',b'arm,gic-v3\0',0],['interrupt-controller',b'',0],
  ['#interrupt-cells',struct.pack('<I',3),0],['reg',struct.pack('<QQQQ',0x100000,0x10000,0x180000,0x200000),0],
  ['redistributor-stride',struct.pack('<Q',0x40000),0]],'k':[]})
dfl=kid['defaults']
if not any(p[0]=='vmm-present' for p in dfl['p']): dfl['p'].append(['vmm-present',struct.pack('<I',1),0])
print('defaults',[(p[0],p[1].hex()) for p in dfl['p']])
print('psci' in kid, 'cpus', len(kid['cpus']['k']))
out=ser(root); r2,e2=parse.__wrapped__(out) if False else (None,None)
open(dst,'wb').write(out); print('size',len(d),'->',len(out))