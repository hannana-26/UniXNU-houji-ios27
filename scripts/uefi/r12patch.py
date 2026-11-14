p='/root/uefi-asm/Platforms/Xiaomi/UniXNUPkg/Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.c'
t=open(p).read()
old='''  Chosen = FdtPathOffset (Fdt, "/chosen");'''
new='''  //
  // houji: ABL does not hand the boot.img ramdisk over as initrd. The FAT32
  // payload is appended to the kernel Image instead and stays in the
  // reserved Kernel window A8000000-B8000000 (method: lonzzi HoujiRamDiskDxe).
  //
  {
    UINT64  A;
    for (A = 0xA8000000ULL; A < 0xB8000000ULL; A += 512) {
      CONST UINT8  *M = (CONST UINT8 *)(UINTN)A;
      if ((M[510] == 0x55) && (M[511] == 0xAA) && (M[11] == 0x00) && (M[12] == 0x02) &&
          (CompareMem (M + 82, "FAT32   ", 8) == 0) && (CompareMem (M + 71, "HOUJIPAY   ", 11) == 0))
      {
        UINT32  Total = (UINT32)M[32] | ((UINT32)M[33] << 8) | ((UINT32)M[34] << 16) | ((UINT32)M[35] << 24);
        if ((Total != 0) && (A + (UINT64)Total * RAM_BLOCK_SIZE <= 0xB8000000ULL)) {
          Start = A;
          End   = A + (UINT64)Total * RAM_BLOCK_SIZE;
          DEBUG ((DEBUG_ERROR, "UNIXNURAMDISK: payload in Kernel window at 0x%lx\\n", A));
          goto Publish;
        }
      }
    }
  }

  Chosen = FdtPathOffset (Fdt, "/chosen");'''
if 'Kernel window at' not in t:
    t=t.replace(old,new,1)
    t=t.replace("  mRamBase                = (UINT8 *)(UINTN)Start;\n","Publish:\n  mRamBase                = (UINT8 *)(UINTN)Start;\n",1)
    open(p,'w').write(t)
print('ok','Publish:' in t)