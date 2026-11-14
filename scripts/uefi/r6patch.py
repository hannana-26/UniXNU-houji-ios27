import re
d='/root/uefi-asm/Platforms/Xiaomi/UniXNUPkg/'
# driver: publish start/end via config table
p=d+'Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.c'
t=open(p).read()
if 'mRamInfo' not in t:
    t=t.replace("STATIC UINT8                    *mRamBase;","STATIC UINT8                    *mRamBase;\nSTATIC UINT64                   mRamInfo[2];\nSTATIC CONST EFI_GUID           mRamInfoGuid = { 0x5a7f3c10, 0x1d2e, 0x4b6a, { 0x9c, 0x01, 0xde, 0xad, 0xbe, 0xef, 0x00, 0x27 } };",1)
    t=t.replace("  Handle = NULL;\n  Status = gBS->InstallMultipleProtocolInterfaces","  mRamInfo[0] = Start;\n  mRamInfo[1] = End;\n  gBS->InstallConfigurationTable ((EFI_GUID *)&mRamInfoGuid, mRamInfo);\n  Handle = NULL;\n  Status = gBS->InstallMultipleProtocolInterfaces",1)
    open(p,'w').write(t)
p=d+'Library/UniXNULib/Main.c'
t=open(p).read()
t=t.replace("RamLast = B->Media->LastBlock; RamBs = B->Media->BlockSize;","RamLast = B->Media->LastBlock; RamBs = B->Media->BlockSize; RamB1 = 0;",1)
# restrict to exact 96MiB device
t=t.replace("if (B->Media->ReadOnly) {","if (B->Media->ReadOnly && B->Media->LastBlock == 196607) {",1)
t=t.replace("RamB0 = (UINT32)Sec[0];","RamB0 = ((UINT32)Sec[0] << 24) | ((UINT32)Sec[1] << 16) | ((UINT32)Sec[2] << 8) | Sec[3];",1)
t=t.replace("UINT32      RamSt = 99,","UINT64      RamStart = 0;\n    UINT32      RamSt = 99,",1)
t=t.replace("          if (B->Media->ReadOnly && B->Media->LastBlock == 196607) {","""          if (B->Media->ReadOnly && B->Media->LastBlock == 196607) {
            {
              EFI_GUID  G = { 0x5a7f3c10, 0x1d2e, 0x4b6a, { 0x9c, 0x01, 0xde, 0xad, 0xbe, 0xef, 0x00, 0x27 } };
              UINTN     K;
              for (K = 0; K < gST->NumberOfTableEntries; K++) {
                if (CompareGuid (&gST->ConfigurationTable[K].VendorGuid, &G)) { RamStart = ((UINT64 *)gST->ConfigurationTable[K].VendorTable)[0]; }
              }
            }""",1)
t=re.sub(r'L"drv %u/%u kit %u acpi %u \| blk %u disk %u max %uGB fs %u \| ram st%u b0 %x sig %x dio%u fs%u bs%u last%lx",','L"blk %u fs %u | ram st%u %08x sig%04x dio%u fs%u at%lx",',t,count=1)
t=re.sub(r'\(UINT32\)Stats\.DriversLoaded,\s*\(UINT32\)Stats\.DriversFailed,\s*\(UINT32\)Config\.KitsLoaded,\s*\(UINT32\)Stats\.AcpiInstalled,\s*\(UINT32\)Blk, \(UINT32\)Disks, \(UINT32\)MaxGb, \(UINT32\)Fs,\s*RamSt, RamB0, RamB1, RamDio, RamFs, RamBs, RamLast','(UINT32)Blk, (UINT32)Fs, RamSt, RamB0, RamB1, RamDio, RamFs, RamStart',t,count=1)
open(p,'w').write(t)