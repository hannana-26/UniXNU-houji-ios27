d='/root/uefi-asm/Platforms/Xiaomi/UniXNUPkg/'
p=d+'Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.c'
t=open(p).read()
t=t.replace("mRamInfo[2];","mRamInfo[3];",1)
t=t.replace("  DEBUG ((DEBUG_ERROR, \"UNIXNURAMDISK: initrd block device","  mRamInfo[2] = (UINT64)(UINTN)Handle;\n  DEBUG ((DEBUG_ERROR, \"UNIXNURAMDISK: initrd block device",1)
open(p,'w').write(t)
p=d+'Library/UniXNULib/Main.c'
t=open(p).read()
t=t.replace("B->Media->ReadOnly && (RamSt == 99 || B->Media->LastBlock < RamLast)","RamHandle == (UINT64)(UINTN)H[I]",1)
t=t.replace("UINT64      RamStart = 0;","UINT64      RamStart = 0, RamHandle = 0;\n    {\n      EFI_GUID  G0 = { 0x5a7f3c10, 0x1d2e, 0x4b6a, { 0x9c, 0x01, 0xde, 0xad, 0xbe, 0xef, 0x00, 0x27 } };\n      UINTN     K0;\n      for (K0 = 0; K0 < gST->NumberOfTableEntries; K0++) {\n        if (CompareGuid (&gST->ConfigurationTable[K0].VendorGuid, &G0)) { RamHandle = ((UINT64 *)gST->ConfigurationTable[K0].VendorTable)[2]; }\n      }\n    }",1)
open(p,'w').write(t)