# 驱动适配次序调研报告(order-research,2026-11-27 20:10)

只读调研。没改任何仓库文件,也没碰手机。这里每个结论都标了 **实锤**(附链接或本地文件行号)或 **推测**。
本地路径都以 `D:\yizhishouji\src\` 为根。

---

## 0. 一句话结论

1. **背光 P17 应该提前,而且是最便宜的一项**。实锤:我们的 HoujiBacklight 只发布了 iOS 17 那套属性。官方文档写明 iOS/iPadOS 27 的 CoreBrightness 少了 `backlight-marketing-table` 就建不起显示("Display creation fails without it")。另外 `vmm-present=1` 会让 CoreBrightness 选中 VMBLControl,把**所有**亮度请求丢掉。这两点正好对上我们看到的现象:内核 requested=0,Display & Brightness 页白转圈。现成轮子有三个:sheng `AppleARMBacklight.cpp:343-381` 的属性块、官方 `scripts/appsync/BacklightMode.m`、OneiOS(iOS 27 / 24A446)的完整踩坑记录。预计 1~2 轮。
2. **调频值得单独做一轮,但很小**。官方把它放在第 7 组(电源)。不过 sheng 和 aston 都是一个启动参数 `cpufreq=4` 加 CpuFreqProbe.cpp 就跑起来了,houji 还有 nian0114 现成的 SM8650 四域 EPSS 表(HoujiCpuDvfs.inc)。它也是 GPU DCVS 和触摸升频的前提。预计 1 轮,读回验证再加 1 轮。
3. **GPU 在背光和调频之后马上开始**。官方顺序是 显示 → 输入 → GPU,前提是"平台 DMA + 基本显示呈现完成"。这两项我们都满足了。

---

## 1. 官方推荐的适配顺序与依赖(unixnu.com)

来源是 [porting/drivers](https://unixnu.com/porting/drivers)(已在线抓取)。各子系统指南看的是本地 `UniXNU-official\docs\porting\*.md`(e7bddb9,和网站同源)。

| 序 | 能力 | 官方指南 | 参考实现 |
|---|---|---|---|
| 1 | CPU、中断、计时器、RAM、DMA | platform | ArmDTPlatform、ArmCpu、ArmSmmu4K |
| 2 | 存储 / 根 / Data | ufs | ShengUfs、RamBlock、VerifiedRoot |
| 3 | **Framebuffer、刷新、亮度** | display | GopFramebuffer、DisplayTimingProbe、**AppleARMBacklight、GeniI2c** |
| 4 | 触摸 + 实体键 | input | ShengNovatekTouch、ShengHidInput、ButtonProbe |
| 5 | GPU / Metal | gpu | Adreno740、AdrenoMetal、Sm8550Platform、AdrenoFirmware、AdrenoPM4、AirNir |
| 6 | Wi-Fi / 蓝牙 / USB(并列) | network / bluetooth / usb | Ath12kWifi、QcaBluetooth、QcomDwc3 |
| 7 | DSP、音频、**电池、CPU 调频**、平台电源 | power | AdspProbe、ShengBattery、**CpuFreqProbe**、ShengPlatformPower |
| 8 | OS 服务与打包 | — | SoftwareAES、AppleKeyStoreState… |

依赖关系(都是实锤,来自官方文档原文):

- **亮度**属于第 3 组,和显示放在一起。`display.md:141-196` 列了 iOS 27 必须发布的属性,`display.md:175-181` 讲了 vmm-present 的问题。亮度本身不依赖 GPU。
- **GPU** 的前提是"Complete platform DMA and basic display presentation first"(`gpu.md:3`),还要先确认 swap completion 和 IOSurface 生命周期(`display.md:50-51`)。GPU 的 DCVS 调速器在 `GpuPowerProbe.cpp` 里(`power.md:245`)。
- **电池**依赖 DSP 传输(ADSP PAS + SMEM + SMP2P + GLINK)。顺序是先 DSP、再 battmgr、最后 IOPMPowerSource(`power.md:24-51, 143-181`)。
- **CPU 调频**依赖 platform 完成。它和 GPU 是联动的:触摸升频、帧驱动升频、GPU 提交会把 CPU 拉到 60%(`power.md:259-265`)。
- **睡眠唤醒**:`platform-sleep=1` 选的是板级 XNU sleep hook 加 ArmCpu suspend 路径。它依赖背光(唤醒后两帧再开背光)、输入(电源键唤醒)和 Wi-Fi 恢复(`power.md:270-282`;sheng 提交 2061710、002ad09)。所以睡眠应该排在背光、输入、Wi-Fi 之后。
- bring-up 文档(`bring-up.md`)只讲 UEFI → 用户态这 7 个阶段,外设一律按 drivers 清单走。

---

## 2. GitHub 最新成果(gh,2026-11-13 之后)

### 2.1 UniXNU 组织

| 仓 | 近两周 | 对我们的意义 |
|---|---|---|
| UniXNU-sheng | 11-17 sleep/wake 合入(#127、#134),Wi-Fi 唤醒扫描,背光时序;11-18 触摸 | 睡眠方案 = ArmCpu platform sleep + ShengSleep.cpp(实锤:提交 2061710、002ad09) |
| ArmCpu | 11-17 `feat(power): add platform sleep and GIC resume support (#7)` 4b81740 | 我们 pin 的是 official components.toml 里的 5f37026,已经含 board sleep hook。睡眠的 ArmCpu 那一半是现成的(实锤) |
| Adreno740 | 11-25 合并 #30(保留被拒提交的诊断信息);11-12 null-AS 映射失败修复 | 稳定,没有 A750 分支(实锤) |
| AdrenoMetal | 11-22~25 大量 bridge 提交,迁移到 runtime v2,pin UniXNU/metal2vulkan | 活跃,iOS 27 runtime 在跟进 |
| UniXNU 主仓 | 11-25 metal2vulkan、Turnip 中断等待;11-17 ArmCpu sleep 选入 | — |
| Ath12kWifi | 11-17 跨睡眠的扫描与网络老化 | — |
| QcomDwc3 | 11-16 runtime;components 标 `ipados-27.0.1-24A446` | USB 有 iOS 27 ABI(实锤:components.toml) |
| GopFramebuffer、sm8550-common | 无新提交 | — |
| 新仓 NativeMetal | 独立的 Metal → A740 驱动 | 观望 |

官方 components.toml 里**没有**背光、电池、调频、睡眠组件。这些都是板级代码,在 sheng 的 `package/` 下:AppleARMBacklight.cpp、BacklightProbe.cpp、ShengBattery.cpp、ShengBattmgr.c、AdspProbe.cpp、CpuFreqProbe.cpp、GpuPowerProbe.cpp、ShengSleep.cpp、ThermalProbe.cpp、QcomRpmh.c(实锤:本地 `UniXNU-sheng\package` 目录)。

### 2.2 sheng 各子系统状态(`UniXNU-sheng\porting.toml`)

- 显示、触摸、GPU、Wi-Fi、音频、电源都是 implemented;键盘、蓝牙、USB 是 device-verified。
- 电源 area(`porting.toml:912-960`):
  - 设备证据:cpufreq=4 下两个域在 499~2803 / 595~2956 MHz 之间跑;GPU 投票全部被接受。
  - 电池:PPS 8.72V/1.3A,读数真实。
- 背光(官方 [docs/drivers/sheng-backlight.md](https://github.com/UniXNU/UniXNU/blob/main/docs/drivers/sheng-backlight.md)):iPadOS 27 下 **"Manual brightness: Working"**,熄屏后背光关闭这一项还没验证。
- sheng 的背光是怎么接到 CoreBrightness 的:**不经过显示节点**。CoreBrightness 自己去 IORegistry 找满足两个条件的服务:`backlight-control=true`,并且 `IOObjectConformsTo(svc,"AppleARMBacklight")`。找到后再读 iOS 27 那套 nits 属性。backboardd 由 AppSync 注入 BacklightMode.m,强制选 BLControl(实锤:`display.md:146-181`)。

### 2.3 nian0114/UniXNU-houji(同机型,iOS 16,最后提交 8f30445,11-25)

- README `:36-47`(实锤):
  - CPU/GPU 调频:实验性可用(工厂表 + 负载 + 触控 + TSENS 限温)。
  - Adreno:部分可用(真实提交、桌面渲染)。
  - 电量 / USB 插拔:基础可用(原厂 ADSP / PMIC GLINK,r263)。
  - 声音:待适配。
- 11-23~25 新成果:**Wi-Fi WCN7850 Stage 4 实机连网成功**(提交 709ca00 集成 Ath12kWifi;c0f1022 SMMU + modem_a 固件 + MHI;2d58102 等 PCIe0 / QMP PHY 时钟)。houji 上 PCIe0 RC 的初始化坑已经有人踩过了(实锤:`known-issues.md:63-65`)。
- 现成的调频源码:`kernel\overlay\iokit\Kernel\arm\HoujiCpuDvfs.inc`。四个 EPSS 域从 0x17d91000 起,每域 +0x1000;CPU→域映射 `{0,0,3,3,3,1,1,2}`;本机上限 caps 和下限都有(`:32-38`)。代码里注明策略来自 sheng 的 CpuFreqProbe(实锤)。
- 现成的电池源码:`kernel\overlay\iokit\Kernel\houji-ufs-root\battery\`(HoujiAdsp*.cpp、QcomGlink.c、HoujiBattery.cpp)。houji 的 SMEM、ADSP 地址和 sheng 不同:SMEM 0x81d00000,ADSP fw 0x9df00000(实锤:`docs/battery.md:21-23`)。
- 背光:r229 起 iOS 16 上 DSI 亮度有可见响应(`known-issues.md:44`)。**iOS 16 没有 vmm / VMBLControl 和 nits 属性这两道坎**,所以不能直接照搬它的判据(推测,依据是 OneiOS 和 sheng 的说明都只针对 iOS/iPadOS 26~27)。

### 2.4 其他 SM8550/SM8650 及 iOS 27 设备

- **aston**(SM8550,iOS 27 24A446,最后提交 11-14):
  - 背光:属性三件套齐了,但**没有 AppSync / BacklightMode**。结果是 iOS boot 9 里 86 次 setDisplayBrightness 全部报 `No container for handle Display(id: 1)`,内核 req=0(实锤:官方 `docs/devices/aston.md:331`)。这和我们的症状一模一样。
  - GPU、battery(sheng-battery=2,bq28z610)、cpufreq=4、Wi-Fi(sheng-wifi=5)都已接入(`aston\board.toml:114-133`)。
- **akshaynexus/OneiOS**(Galaxy S24U,iOS 27 24A446,11-25):其 [drivers/brightness/README.md](https://github.com/akshaynexus/OneiOS/blob/main/drivers/brightness/README.md) 是迄今最完整的 iOS 27 亮度链分析(实锤)。要点:
  1. `vmm-present` → `MGGetBoolAnswer("IsVirtualDevice")` → `initBLControl:` 收到 mode 3 → VMBLControl 把请求走 XPC 发给虚拟化宿主 → **没有 display container**,SpringBoard 报 "No container for handle Display(id: 1)"。
  2. 修法一:BacklightMode 把 mode 改成 1。
  3. 修法二:`useSyncBrightnessTransactionForDisplay:` 要返回 NO。否则亮度走 CoreAnimation 帧同步路径,而 CPU / GOP 显示没有 handler,会打日志 "Display 1, no brightness handler registered"。
  4. `LmidProduct` 和 `LminProduct` 必须用 16.16。
  5. 要接收 `brightness-nits`(有符号 16.16)和 `DisplayBrightnessSliderPosition`。`DisplayBrightness` 是 Float32,经 IOKit 序列化后会被截断成 0,不能用。
- **cupid-lengcece**(SM8450,iOS 27):11-27 还在修 CI,外设没有进展。
- **shennong**(YX2748198202-lab):11-27 "Retire the legacy shennong port"。sleep1234、baihem 两个仓近两周没有提交。
- **Kizashi3/UniXNU-houji**:11-24 有一次总览提交。lonzzi、SAKAYORILROHA 近两周没有提交。
- 没找到有人在 iOS 27 上做出 A750 GPU、睡眠或音频的新方案。睡眠的现成参考只有 sheng 和 ArmCpu(iPadOS 17 已验证;iPadOS 27 下熄屏关背光未验证)。

---

## 3. 官方现成组件 vs 我们 board.toml

我们当前的 include(`UniXNU-houji-ios27\board.toml:14`):ArmDTPlatform、ArmCpu、XnuPrelink、DarwinBoot、ShengUfs、RamBlock、GopFramebuffer、AppleKeyStoreState。

| 官方组件 | 我们 | sheng/aston iOS 27 链 | 说明 |
|---|---|---|---|
| **Adreno740 + AdrenoMetal**(依赖 Sm8550Platform、AdrenoPM4、AdrenoFirmware、ArmSmmu4K) | 没启用 | 都启用(`sheng\board.toml:118`、`aston\board.toml:198`) | GPU 轮要用。注意 os_abi 只标了 ipados-17 和 macos-27,iOS 27 由板级 build-gop-legacy-display-27 和 gpu_addon 适配。A750 ≠ A740,固件和寄存器差异要另外处理(推测) |
| ShengPreflight | 没启用 | 都启用 | 只是 UEFI 预检,可有可无 |
| QcomDwc3 | 没启用 | sheng 用 usb=1 | 已标 iPadOS 27 ABI,USB 轮可以直接用(实锤:components.toml) |
| Ath12kWifi | 没启用(仓里只有 WifiFirmware.c) | sheng / aston 用 wifi=5 | nian0114 已在 houji 上实机连通(iOS 16) |
| QcaBluetooth | 没启用 | sheng macOS / iOS 27 | 标 ipados-27 ABI |
| SoftwareAES、VerifiedRoot、VirtioMmioBlock | 没启用 | 旧链用 | 低优先 |
| QcomGeniSpi、ShengNovatekTouch、ShengHidInput | 没启用 | iPadOS 17 链 | 我们走的是板级 TouchProbe 和 NativeHidInput,不需要 |
| QcomBaseband | 没启用 | — | 蜂窝最后再做 |

**非组件、但官方主仓自带的现成件**:`UniXNU-official\scripts\appsync\`(含 BacklightMode.m)。启用方式是 `payload.runtime.appsync = true` + 启动参数 `appsync=1` + 内核补丁 `sheng-appsync-runtime.patch` 和 `sheng-appsync-user-trust.patch`(在 `UniXNU-sheng\patches\xnu-12377\series:18-19`)。`build-boot-runtime.py:437-439` 只允许 ios27/24A446,我们的 `[payload.runtime] target="ios27" build="24A446"` 正好满足。**我们没启用**(实锤:board.toml 里没有 appsync)。

---

## 4. 我们背光链断在哪(对照证据)

- 我们的服务只发布了 `IODisplayParameters.brightness{min,max,value}`、`backlight-control`、`NativeSandboxGrantProcess`(`UniXNU-houji-ios27\package\HoujiBacklight.cpp:147-156`),setProperties 也只认 `brightness`/`FadePeriod`(`:230-231`)。
- 缺的东西(对照 sheng `AppleARMBacklight.cpp:343-381, 486-492`):
  1. `BrightnessMilliNits`
  2. `backlight-marketing-table`(缺了它显示建不起来)
  3. `LmaxProduct`(4 字节 16.16)
  4. `LmidProduct` / `LminProduct`
  5. `backlight-calibration-parameters`
  6. 接收 `brightness-nits` 请求
- 另外 `vmm-present = 1`(`board.toml:72`),却没有 BacklightMode。按官方文档,这会让 CoreBrightness 丢掉所有请求(实锤:`display.md:175-181`)。
- 和症状对得上:内核侧 requested=0(`porting.toml:144`),Display & Brightness 页打不开,aston 同款日志 "No container"。**推测**白转圈就是 CoreBrightness 显示容器创建失败导致的,下一次读回可以看 backboardd 日志里有没有 `No container for handle` 或 `Set VM Mode 3 BLControl` 来确认。
- porting.toml:144 的头号候选是"CPU 复制显示路径没和背光服务建立连接"。这一条**基本可以排除**:sheng 的机制是 IORegistry 按 conforms 查找,和显示节点无关,aston 也走 CPU 显示,能找到服务,只是没有 container(推测,依据充分)。

---

## 5. 建议修复次序(大白话)

| 次序 | 做什么 | 为什么排这里 | 借谁的轮子 | 预计 | 风险 |
|---|---|---|---|---|---|
| **1. 背光 P17(提前)** | A 轮(纯内核,很小):HoujiBacklight 补上 5 个 iOS 27 属性,setProperties 加上 `brightness-nits`(16.16 有符号)和 `DisplayBrightnessSliderPosition`。B 轮(用户态):启用 appsync runtime 加 BacklightMode(mode 3→1);如果读回显示 "no brightness handler",再补 `useSyncBrightnessTransactionForDisplay:` 返回 NO | 官方顺序第 3 组。是现成代码加属性,改动小,还能修好 Settings 页打不开的用户可见问题。睡眠唤醒以后也要用背光 | sheng `AppleARMBacklight.cpp:343-381,486-492`(DSI 引擎不换);官方 `scripts/appsync/BacklightMode.m`;OneiOS brightness README(sync 事务、16.16 坑) | 1~2 轮(A 轮单独上机就能看 Settings 页能不能打开;真正拖滑条生效可能要 B 轮) | 中低。AppSync 要加两个内核补丁、替换 backboardd 签名副本、扩 trust cache,打包链变化比 A 轮大。A 轮几乎零风险。**推测**:只做 A 轮,页面可能打得开但滑条仍然无效(VMBLControl 还在) |
| **2. CPU 调频** | 照 sheng `CpuFreqProbe.cpp` 的接口和参数(cpufreq=4),换成 houji 四域 EPSS 表(nian0114 HoujiCpuDvfs.inc:32-38) | 一轮小改,带来全局体感,还是 GPU DCVS、帧升频、触摸升频的基础(`power.md:259-265`)。也可以顺便做一次每核计数读回 | sheng CpuFreqProbe.cpp(19979 B);nian0114 HoujiCpuDvfs.inc | 1 轮,读回 0~1 轮 | 中。sheng 是 3 域、houji 是 4 域,要改 domain 映射;发热问题要加 TSENS 限温(nian0114 r242 结温到过 98°C) |
| **3. GPU-0/1/2** | GPU-0 上电(GMU / HFI)→ GPU-1 提交 → GPU-2 Metal | 前提已满足(DMA + 显示呈现)。官方第 5 组 | Adreno740 + AdrenoMetal 组件链(仿 aston `board.toml:114-122`);A750 部分参考 nian0114 `kernel\...\houji-ufs-root\Gpu*.h`(A750 GMU、ZAP、工厂 OPP 231~903 MHz) | 多轮(≥5,推测) | 高。A750 ≠ A740;nian0114 iOS 16 上也还有卡顿和粉色问题 |
| 4. 电池 / USB | 电池:ADSP PAS + GLINK + battmgr → IOPMPowerSource;USB:QcomDwc3 | 官方第 7 组,依赖 DSP 传输。nian0114 的 houji 地址可以直接用 | nian0114 battery\(HoujiAdsp*、QcomGlink.c);sheng ShengBattery / Battmgr | 2~3 轮 | 中 |
| 5. Wi-Fi | PCIe0 RC → MHI → Ath12kWifi | nian0114 11-25 刚在 houji 实机连通,轮子很新 | nian0114 c0f1022 / 709ca00 / 2d58102;Ath12kWifi 组件 | 2~4 轮 | 中高。我们是 iOS 27,要走 sheng / aston 的 wifi=5 和 userspace |
| 6. 睡眠唤醒 | platform-sleep=1 + 板级 sleep hook;唤醒后恢复背光、Wi-Fi | 依赖背光、输入、Wi-Fi 先就绪(`power.md:270-282`) | ArmCpu 4b81740(我们 pin 的 5f37026 已有 hook);sheng ShengSleep.cpp | 2 轮 | 中高 |
| 7. 音频 / 蓝牙 / 蜂窝 | — | 最后做 | sheng AudioProbe / QcaBluetooth;houji 用 WCD939x + CS35L41,和 sheng 不同 | 多轮 | 高 |

### 三个具体问题的回答

1. **背光 P17 该不该提前?** 该提前(实锤 + 推测)。根因基本确认是"缺现成属性 + 缺现成用户态补丁",不是新驱动的问题。属性部分只是复制 sheng 的代码,很便宜。BacklightMode 部分是现成文件,但启用 AppSync 的打包链改动属于中等成本。可以拆成 A/B 两小轮,A 轮插在调频之前。
2. **调频值不值得单独一轮?** 值得,但只要一小轮(推测)。sheng、aston、nian0114 都已证明可行,改动集中在一个 probe 文件加启动参数,收益是全局的,也是 GPU DCVS 的前置。
3. **GPU 何时开始?** 背光 A/B 和调频之后马上开始(推测;官方前提条件已满足,是实锤)。不建议把 GPU 拖到电池或 Wi-Fi 后面,因为官方顺序里 GPU 是第 5 组,早于第 6、7 组。

---

## 6. 关键链接与文件

- 官方:[porting/drivers](https://unixnu.com/porting/drivers);本地 `UniXNU-official\docs\porting\display.md:141-196`、`power.md:240-282`、`gpu.md:1-5`;`docs\drivers\sheng-backlight.md`;`scripts\appsync\BacklightMode.m`;`scripts\build-boot-runtime.py:437-452`
- sheng:`UniXNU-sheng\package\AppleARMBacklight.cpp:343-430,486-492`;`board.toml:61`(参数全集);`porting.toml:912-960`
- aston:官方 `docs\devices\aston.md:331`("No container",没有 AppSync)
- OneiOS:https://github.com/akshaynexus/OneiOS/blob/main/drivers/brightness/README.md
- nian0114:`README.md:36-47`;`docs\known-issues.md:44,63-65`;`kernel\overlay\iokit\Kernel\arm\HoujiCpuDvfs.inc:1-70`;`...\houji-ufs-root\battery\`
- 我们:`UniXNU-houji-ios27\board.toml:14,72,163-168`;`package\HoujiBacklight.cpp:147-156,230-231`;`porting.toml:144`
