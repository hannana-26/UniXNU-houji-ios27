p='/root/uefi-asm/Platforms/Xiaomi/UniXNUPkg/Library/UniXNULib/Main.c'
t=open(p).read()
t=t.replace("UINTN       N, I, Blk = 0, Fs = 0, Disks = 0, MaxGb = 0;","UINTN       N, I, Blk = 0, Fs = 0, Disks = 0, MaxGb = 0;\n    UINT32      RamSt = 99, RamB0 = 0, RamB1 = 0, RamDio = 0, RamFs = 0, RamBs = 0;\n    UINT64      RamLast = 0;",1)
t=t.replace("          Disks++;\n","""          Disks++;
          if (B->Media->ReadOnly) {
            UINT8  Sec[512];
            VOID   *Tmp;
            RamLast = B->Media->LastBlock; RamBs = B->Media->BlockSize;
            ZeroMem (Sec, sizeof (Sec));
            RamSt = (UINT32)(B->Media->BlockSize == 512 ? B->ReadBlocks (B, B->Media->MediaId, 0, 512, Sec) : 98);
            RamB0 = (UINT32)Sec[0]; RamB1 = ((UINT32)Sec[510] << 8) | Sec[511];
            RamDio = !EFI_ERROR (gBS->HandleProtocol (H[I], &gEfiDiskIoProtocolGuid, &Tmp));
            RamFs  = !EFI_ERROR (gBS->HandleProtocol (H[I], &gEfiSimpleFileSystemProtocolGuid, &Tmp));
          }
""",1)
t=t.replace('L"drv %u/%u kit %u acpi %u | blk %u disk %u max %uGB fs %u",','L"drv %u/%u kit %u acpi %u | blk %u disk %u max %uGB fs %u | ram st%u b0 %x sig %x dio%u fs%u bs%u last%lx",',1)
t=t.replace("(UINT32)Blk, (UINT32)Disks, (UINT32)MaxGb, (UINT32)Fs\n      );","(UINT32)Blk, (UINT32)Disks, (UINT32)MaxGb, (UINT32)Fs,\n      RamSt, RamB0, RamB1, RamDio, RamFs, RamBs, RamLast\n      );",1)
if 'DiskIo.h' not in t: t=t.replace('#include <Protocol/BlockIo.h>','#include <Protocol/BlockIo.h>\n#include <Protocol/DiskIo.h>',1)
open(p,'w').write(t)