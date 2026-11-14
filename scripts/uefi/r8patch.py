p='/root/uefi-asm/Platforms/Xiaomi/UniXNUPkg/Library/UniXNULib/Main.c'
t=open(p).read()
t=t.replace("B->Media->ReadOnly && B->Media->LastBlock == 196607","B->Media->ReadOnly && (RamSt == 99 || B->Media->LastBlock < RamLast)",1)
t=t.replace('dio%u fs%u at%lx",','dio%u fs%u at%lx n%lx",',1)
t=t.replace("RamDio, RamFs, RamStart\n","RamDio, RamFs, RamStart, RamLast\n",1)
t=t.replace("    (VOID)RamBs; (VOID)RamLast;\n","    (VOID)RamBs;\n",1)
open(p,'w').write(t)