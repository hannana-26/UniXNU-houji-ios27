p='/root/uefi-asm/Platforms/Xiaomi/UniXNUPkg/Library/UniXNULib/Main.c'
t=open(p).read()
t=t.replace("UINT64      RamStart = 0, RamHandle = 0;","UINT64      RamStart = 0, RamHandle = 0, RamFound = 0;",1)
t=t.replace("    (VOID)RamBs;\n","""    (VOID)RamBs;
    if (RamStart != 0) {
      UINT64  A;
      for (A = RamStart; A < RamStart + (160ULL << 20); A += 0x1000) {
        CONST UINT8  *M = (CONST UINT8 *)(UINTN)A;
        if (M[0] == 0xEB && M[1] == 0x58 && M[2] == 0x90 && M[3] == 'M' && M[4] == 'T' && M[5] == 'O' && M[6] == 'O') { RamFound = A; break; }
      }
    }
""",1)
t=t.replace('at%lx n%lx",','at%lx n%lx found%lx",',1)
t=t.replace("RamStart, RamLast\n","RamStart, RamLast, RamFound\n",1)
open(p,'w').write(t)