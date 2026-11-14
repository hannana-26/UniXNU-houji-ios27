p='/root/uefi-asm/Platforms/Xiaomi/UniXNUPkg/Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.c'
t=open(p).read()
anchor="  mRamBase                = (UINT8 *)(UINTN)Start;\n"
add='''  //
  // houji: ABL may hand over the vendor ramdisk as initrd. Search forward for
  // the FAT32 payload volume and use it when the initrd itself is not FAT.
  //
  {
    CONST UINT8  *S = (CONST UINT8 *)(UINTN)Start;
    if (!((S[510] == 0x55) && (S[511] == 0xAA) && (CompareMem (S + 82, "FAT32   ", 8) == 0))) {
      UINT64  A;
      for (A = Start & ~0xFFFULL; A < Start + (256ULL << 20); A += 0x1000) {
        CONST UINT8  *M = (CONST UINT8 *)(UINTN)A;
        if ((M[0] == 0xEB) && (M[2] == 0x90) && (M[11] == 0x00) && (M[12] == 0x02) &&
            (M[510] == 0x55) && (M[511] == 0xAA) && (CompareMem (M + 82, "FAT32   ", 8) == 0))
        {
          UINT32  Total = (UINT32)M[32] | ((UINT32)M[33] << 8) | ((UINT32)M[34] << 16) | ((UINT32)M[35] << 24);
          if (Total != 0) {
            DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: FAT32 payload found at 0x%lx (%u sectors)\\n", A, Total));
            Start = A;
            End   = A + (UINT64)Total * RAM_BLOCK_SIZE;
            break;
          }
        }
      }
    }
  }

'''
if 'FAT32 payload found' not in t:
    t=t.replace(anchor,add+anchor,1)
    if 'BaseMemoryLib' not in t:
        t=t.replace('#include <Library/DebugLib.h>','#include <Library/DebugLib.h>\n#include <Library/BaseMemoryLib.h>',1)
    open(p,'w').write(t)
print('ok', 'FAT32 payload found' in open(p).read())