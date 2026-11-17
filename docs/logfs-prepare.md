# logfs 日志通道准备与回收 SOP(2026-11-17,借 lonzzi-houji 管线)

内核侧已落地(commit 3e34f56 + 01d5dbb):`houji-logfs=1` 打开 logfs/misc 写窗口,
`houji-logfs-recovery=1` 在首次成功提交后写 BCB 并复位回 recovery。两参数在 logfs
未准备前惰性:`houji_logfs_open` 校验不过 = 零写入。核心判据在
`package/HoujiLogFsCore.h`(内核与 host 共用);准备与回收工具在 `scripts/`。

## 红线
- 步骤 A/B 只读手机或只碰本地;步骤 C 写 logfs = 动 userdata 以外分区,必须用户对
  完整命令点头才执行;不做 erase/format,不碰 boot/recovery/misc 之外的分区(BCB 由
  内核在带 houji-logfs-recovery=1 的启动里写,host 不写 misc)。
- 写回只允许写本 SOP 产出的镜像;回滚=写回步骤 A 的原备份。

## 步骤 A:只读备份(手机 root recovery,例如 OrangeFox)
```
python3 scripts/capture-logfs-recovery.py --serial 6e0f97c8 --adb-port <P> --adb adb \
    --output build/logfs-r1/backup
```
输出 `logfs.bin`(整分区,双读一致校验)+ `capture.json`。顺带本地记 sha256。

## 步骤 B:本地核对与重建(不碰手机)
```
python3 scripts/logfs-fat.py scan  build/logfs-r1/backup/logfs.bin
python3 scripts/logfs-fat.py build build/logfs-r1/backup/logfs.bin build/logfs-r1/built.bin
python3 scripts/logfs-fat.py decode build/logfs-r1/built.bin build/logfs-r1/decode-test
```
- `scan` 必须确认:几何=内核校验的同款 FAT12(4096B 簇、2 FAT、512 根目录项),且
  预留簇 69..84 空闲;占用则停,先由用户决定是否换链(改 `HoujiLogFsCore.h` 的
  `HOUJI_LOG_CLUSTER_FIRST` 并同步 `scripts/logfs-fat.py`,重跑两个自测)。
- `build` 产出新镜像 + `built.build.json`(前后 sha256);`decode-test` 此时应报
  "no committed record: log header magic"(pristine),属预期。

## 步骤 C:写回 logfs(红线动作,先给用户完整命令)
经 recovery root adb `dd` 写回 `built.bin`,写前读一次比对、写后再读回比对 sha256;
再跑步骤 A 的 capture 确认分配有效(pristine)。回滚:
```
dd if=build/logfs-r1/backup/logfs.bin of=<logfs节点> bs=4096
```

## 步骤 D:回收日志
正常 `fastboot boot` r25+ 镜像(args 已带 houji-logfs=1 与 recovery 参数)。内核每
houji-logfs-period 轮(默认≈10 s)提交一条记录;带 recovery 参数的启动在首次成功
提交后自动回 recovery。回收=再跑步骤 A 的 capture 脚本,`XnuLog.txt` 即为本轮
msgbuf 尾+横幅(带 run id 与 CRC 校验,陈旧/损坏记录会被拒)。

## 已知局限
- 保存点=内核检查点:无蓝条的启动(内核未执行)拿不到内核日志,该模式仍缺证据。
- 最后一条提交未必包含硬复位前最后一条指令(lonzi 同款限制)。
