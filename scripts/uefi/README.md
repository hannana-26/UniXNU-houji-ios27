houjif UEFI (OnePlus 13R OEM DXE + houjiPkg) 构建记录:
1. apply_r4.py: 加入 UniXNURamDiskDxe/DarwinBoot/SEC entry-arg HOB(取自 peridot), 并设 WITH_DARWINBOOT=1
2. r5patch.py: UniXNULib 信息行内存盘诊断(临时调试)
3. build_uefi.py -d houjif -r DEBUG; mk5.py 把 payload.img 作为 ramdisk 打成 header v4 boot 镜像
路径为本机 WSL 绝对路径, 尚未参数化。仅 fastboot boot。
