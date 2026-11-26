# 触摸轮总结(2026-11-26,固件上电轮 r70 + 内核驱动轮 r71,一次实机通过各一次)

位置固定:`D:\yizhishouji\触摸轮总结_2026-11-26.md`;同内容副本 `src\UniXNU-houji-ios27\docs\touch-round-2026-11-26.md`。
本文是触摸轮的单一完整叙述(第 0 步核对→固件轮→内核轮→读回),交接文档与 porting.toml 保留逐轮流水,本文给"一段话能讲清全貌"的版本。
证据分类:**实锤**=命令/文件/照片/录像当场核实;**推测**=未读全或无日志佐证。

## 一、结果一句话

小米14 houji(SM8650)移植 iOS 27.0.1:**触摸屏(Synaptics S3910,GPIO 位拍 SPI + TouchCom v1)从固件上电到内核点按全链 device-verified**——手指点桌面 Settings 图标,应用完整打开;驱动体检报告 stage=5 error=0,内核连续运行 4.98 小时零错误。

## 二、三段时间线(每段的关键证据)

### 第 0 步:上电核对(零上机,dfd799a)
- 问题:lonzzi 在 UEFI 阶段给触摸上电(ldob9/ldob12+三路 QUP 时钟),我们的启动链有没有?
- 结论(实锤,三重证据):**没有**。①解剖烧机固件 Mu-houjif-r26.img(解 gzip+LZMA FV 后 HOUJI TOUCH UEFI/houji-touch-vdd/HoujiStorageProbe 全 0 命中,阳性对照命中);②源码层 HoujiStorageProbe 只编译不装箱(fdf 零引用);③lonzzi 的调用只在其存储诊断 app 分支内,我们的菜单链路不可达。
- 决策:触摸轮拆成固件上电轮+内核驱动轮。全文=触摸轮第0步_上电核对与TouchProbe设计_2026-11-26.md。

### 固件上电轮 r70(10:40 验收,一次通过)
- 改动:work/patch-uefi-touch-power.py(幂等)把 lonzzi 同 sha 的 TouchHandoff.h 78 行移植进 UniXNULib/Boot.c(XNU/efi 分叉前),UniXNULib.inf += QcomPkg.dec + gEfiClockProtocolGuid;重建 UEFI **精确复现 r26 模板 sha c5a1d9ce…(差异恰好只有触摸补丁)**。
- 实机(帧 ftouch/f0141.4.jpg):HOUJI TOUCH UEFI 全 sequence Success——版本匹配(RPMh=10001/CmdDb=10000)、三路时钟 id 10005/10006/10120、ldob9(41500/slave4)与 ldob12(40900/slave4)六次 RPMh 申请全 Success(3000/1800mV+HPM+enable)、SE clock 精确 19200000 Hz、三时钟 on=1、**firmware handoff ready + preparation=EFI_SUCCESS; driver enabled=1**;桌面+按键无回归。

### 内核驱动轮 r71(12:28 上机,触摸即生效;17:31 读回体检全绿)
- 改动(commit a166ee2):package/TouchProbe.cpp(lonzzi HoujiTouchDriver.inc 逐字适配:printf 替 IOLog、IOSleep 替等待、每帧一次 publish 当前按下集合[sheng 契约]、静态全 POD 避 mod_init)+ 5 个协议头逐字节复制(哈希校验一致)+ 删 7 个 r68 替身 + NativeHidInput 轴常数 30480/20320→1200/2670 + late probe 链加 native_touch_probe + builder 接线 + args offline-touch=1 sheng-touch-hid=1(42 参 846B)。
- CI 四次:①msg={0} 缺字段初始化 ②gTouchGpio 未声明(重写时漏声明) ③借来头文件的 {0} 触发我们 -Wextra → builder 对 TouchProbe 单 TU 加 -Wno-missing-field-initializers(保持头逐字节) ④45752557343 成功。
- 实机(照片 5a28e6bd…915a):桌面→点 Settings 图标→**应用完整打开**。打开应用唯一途径=触摸(auto-home 只按 Home 键)⇒全链实锤。
- 读回(视频 f4c185f9…,恰逢坏模式但读取器显示先于挂点,数据完整):心跳行完整(rbheart/f0082.5.jpg)=**HOUJI TOUCH: stage=5 error=0 frames=617 events=617 irq-polls=619 device-write=1024 host-write=515 config=128 last-header=001211e5 discard=4 extent x[15..1151] y[24..2660]**(t=17922s≈4.98h)。

## 三、体检报告逐项解读

| 字段 | 值 | 判定 |
|---|---|---|
| stage | 5 | 初始化全过(芯片身份/坐标/描述符/使能全通过);stage=5 隐式证明芯片 build=4323384 与 lonzzi 一致(不符会停 stage3 报 141) |
| error | 0 | 4.98 小时轮询零错误 |
| frames/events | 617/617 | 617 个触摸帧解码并发布 |
| irq-polls | 619 | 中断线响应 619 次 |
| device-write | 1024 | 芯片声明的写长度(lonlzi 文档同值) |
| host-write | 515 | 主机限幅=min(声明,515) 实测正确 |
| config | 128 | 读芯片默认 128 字节报告配置(设计如此) |
| discard | 4 | 4 个大帧正确排空 |
| extent | x[15..1151] y[24..2660] | 触摸覆盖全屏;Y 方向正确(左上角图标打开正确,若颠倒不可能);用户体感"基本对得上" |

佐证:记录尾大量 [IOUserHIDEventDriver.cpp:1985] createEventForDigitizerCollection=内核 HID 栈持续消费触摸报告。

## 四、资产清单(后续轮直接用)

- 源码:package/TouchProbe.cpp + HoujiTouch{Gpio,Wire,Report,Descriptor,Probe}.h(5 头逐字节,liszzi sha 校验一致);TouchProbe.cpp 的替换点清单在文件头注释。
- 固件:WSL /root/uefi-asm 树(补丁脚本 work/patch-uefi-touch-power.py 可复现);模板 build/uefi-touch1/Mu-houjif-touch1.img sha 26f059b4…(所有后续镜像的 UEFI 基底——含触摸上电)。
- 打包:work/mk70.sh(固件轮)/mk71.sh(内核轮)断言模板可复用;树快照 work/uefi-tree-before-touch1.txt。
- 证据:build/bootimg-r10/r70/(上电视频+帧)、r71/(触摸视频 r71.mp4 8DF95BBB…、读回 r71rb.mp4 F4C185F9…、Settings 照片 5a28e6bd…、心跳帧)。
- 经验已写入基本原则.md v3.4(§4 第 8-12 条:TOML 编辑后必校验/借用文本零改动用 TU 级 -Wno/重写后全标识符自查/late 归档静态 POD-only/驱动诊断心跳)。

## 五、候补(不阻塞,待用户另批)

- 触摸体验微调:滚动手感、边缘手势(int-163 的 FromEdgeTip 路径)、多点/双指——现状单指点按已可用。
- ShengHidInput 上游 2 提交(1140614 macOS 门控配对+19e1da5 参数改名 sheng-touch-hid→touch-hid):跟进则 board.toml 参数名随改。
- Y 方向/坐标精调: extent 已证方向正确,若后续发现像素级偏移再调。

## 六、复评触发器

- lonzzi/nian0114 触及 Touch*/TouchHandoff 新提交 → 复扫(参考源扫描 §五既有触发器)。
- 触摸出现新症状(漂移/丢点/边缘失效)→ 先读回心跳行(error/extent)再立案。
- ShengHidInput 上游触及 publish/sink 契约 → 内核侧跟进评估。
