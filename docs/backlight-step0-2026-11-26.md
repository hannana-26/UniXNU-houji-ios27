# 背光轮第 0 步(2026-11-26,零上机零代码):机制核对+四源扫描+实施设计

目的:回答"亮度怎么调"——机制、现成轮子、集成方案、风险;产出实施轮设计,待用户批准后动代码。
证据分类:实锤=文件/行号/哈希/lonzzi 实机记录;推测=未上机验证项。

## 一、结论一句话

houji 主屏(xiaomi AMOLED,DSC 命令模式)亮度=**DSI DCS `0x51` 写(16 位高字节在前)/`0x52` 读回**,DSI0 控制器基址 **0x0ae94000**(与我们 houji.dts 逐字一致);**lonzzi/nian0114 有同源且实机验证过的内核背光服务 `HoujiBacklight.inc`+`HoujiBacklightProtocol.h`(两仓 sha 完全一致,lonzzi r229 用户确认调光有响应、DSI 日志无错误)**;iOS 27 消费契约(sheng 原文)=`backlight-control=true`+`IOObjectConformsTo("AppleARMBacklight")`+backboardd 属性授权。集成=借引擎 2 文件+2 个 xnu 小挂点+我们 LegacyDisplayProbe 启动+printf 心跳,不碰 MDSS 顶层、不碰 GPU。

## 二、硬件机制(实锤,houji.dts 行号)

- 主面板节点:`qcom,mdss_dsi_n2_42_02_0a_p2_dsc_cmd`(houji.dts:31613-31680,`mi,panel-id=0x4e32 0x42020a`,`dsi_cmd_mode` 命令模式 DSC);另有多个变体节点(33547/35161 等),全部 `qcom,dsi-ctrl-num=0`(DSI0)。
- 调光属性:`qcom,mdss-dsi-bl-inverted-dbv`+`qcom,mdss-dsi-panel-dimming-enabled`(:31645-31646)=亮度经 DCS DBV(0x51);`qcom,mdss-brightness-init-level=0x133`(=307,:31693);`mi,mdss-dsi-fac-bl-max-level=0xb32`(=2866,:31670);三个变体节点常量一致。
- 唯一 `bl_ctrl_pwm` 在 `qcom,mdss_dsi_sharp_1080p_cmd`(:41194-41219)=sharp 1080p LCD 屏(非 houji 主屏),PMIC PWM 路线与我们无关。
- DSI 控制器:`qcom,mdss_dsi_ctrl0@ae94000`(reg=`0xae94000 0x1000`+disp_cc `0xaf0f000`+mdp_intf `0xae36000`,compatible `qcom,dsi-ctrl-hw-v2.8`,:41981-42010);ctrl1@ae96000 未被任何面板引用。
- PMIC PWM 外设(pmk8550/pm8550 qcom,pwm-lpg,:13679-14464/42546-42650)与主屏调光无关。

## 三、四源扫描(纲领四,逐条)

① **官方同 SoC(sheng/official)**:已借鉴 2 条——(a)sheng `package/AppleARMBacklight.cpp`(468 行,ShengBacklight 类)的 **iOS 27 消费契约原文**(:349-354):CoreBrightness 以 `backlight-control=true`+`IOObjectConformsTo(service,"AppleARMBacklight")` 找背光(CBDisplayContaineriOS findBacklight),只给内建屏;conforms 不成立时 libEDR server 不启动,QuartzCore IOMFBServer 的同步 send 会握着 display server 锁卡死显示(int-236 教训)——**两个属性必须同时发布**;(b)backboardd 沙盒拒 FadePeriod(int-276)→需内核侧授权机制(sheng 用 zzwb-native-sandbox-grant-set-properties.patch+`NativeSandboxGrantProcess` 属性;nian0114 用 IOUserClient 挂点旁路,见②)。sheng 引擎=GeniI2c 双引擎 I2C(地址 0x11,BrtLsb/Msb),**硬件不适用 houji(DSI DCS)**。official components.toml 无 Backlight 组件(背光=板级自担,官方不背)。
② **同设备 houji(lonzzi/nian0114)=借鉴主体**:`kernel/overlay/iokit/Kernel/arm/HoujiBacklight.inc`(212 行)+`HoujiBacklightProtocol.h`(90 行)——**nian0114 与 lonzzi 两仓 sha 完全一致(inc=528B5391DC74B97A…,Protocol 同)=同源单文件**,可哈希钉住带入。实机记录=lonzzi `docs/performance.md:218-227`:绑 DSI0 0x0ae94000+n3 面板,DCS 0x52 读初值、0x51 高字节在前,正常最大 2866/最小非零 25/不进 HBM;接 CoreBrightness 的 `IODisplayParameters`/`FadePeriod`;独立工作循环合并请求+渐变,每命令 50ms 上限,失败停发;**r227=DSI 初值回读+服务注册成功(210 次属性请求因参数解析被拒)→r229=用户确认调光有响应、DSI 日志无错误**;完整亮度范围/连续拖动/熄屏唤醒待验(其 repo 自述)。集成挂点(local.patch 两 hunk,实锤行号):`iokit/Kernel/IOUserClient.cpp` is_io_object_conforms_to 里 conforms 兜底(:2448 后 `if(!*conforms)*conforms=houji_backlight_conforms(...)`,ARM64_BOARD_CONFIG_SWEET 门);同文件 io_registry_entry_set_properties 的 CONFIG_MACF 分支 `&& !houji_backlight_allows_properties(entry,obj)`(:4097 前后);启动点=SweetFramebuffer.cpp:456 `houji_backlight_start(this)`(我们改挂 LegacyDisplayProbe late probe)。引擎细节(Protocol.h):FIFO@0x17c/触发 0x3c bit30/schedule 0x390/长度 0x4c/状态 0x110(W1C 交错,掩码 0x55115501 防误清他人事件)/回包计数 0x1d4/回包 FIFO 0x6c/ack 错误 0x68/tpg 0x15c;亮度包={0xc0390003,0xff000051|(hi<<8|lo)|padding}+读包=0xa0060052(BTA);回包判定 bytes=4 或 8 且头 0x22、值≤4095;曲线=fraction²×MAX/SCALE²、下限 25;启动门槛 `(read(4)&5)==5`+无其他 FIFO 主;失败码表 1-7齐;boot-arg `houji_bl` 默认开。
③ **同 SoC 同代(peridot/fuxi/cupid/shennong)**:peridot `docs/porting/display.md` 组件依赖表(GopFramebuffer+DisplayTimingProbe+AppleARMBacklight+GeniI2c)11-24 已录=证明"背光是板级服务"惯例,其引擎(I2C)不适用;其余仓无背光记录。无可直接借鉴。
④ **aston**:`package/AppleARMBacklight.cpp`(31.9KB)+`BacklightProbe.cpp`(1.6KB)同族=iOS 27 同 fixture(d94ap/24A446)消费侧第二参照,引擎硬件不适用;消费契约与 sheng 一致。不搬引擎。

## 四、实施设计(拆轮,待批)

**背光轮=内核背光服务轮(一类)**;体验微调(连续拖动/熄屏唤醒/自动亮度/HBM)列候补不阻塞。
改动清单(全部零 GPU、不碰 MDSS 顶层寄存器、不碰 SDE):
1. 新增 `package/HoujiBacklight.cpp`=nian0114 .inc 的**标注适配副本**(头部注明:正文借自 sha 528B5391… 的 HoujiBacklight.inc;适配点=①IOLog→printf×3(logfs 只收 printf,触摸轮教训)②新增 15s 周期心跳行(stage/requests/applied/readback,纲领 §4.12;_snapshot 快照函数保留)③启动点由 SweetFramebuffer 改为我们 LegacyDisplayProbe late probe 调 `houji_backlight_start(provider)`,新增导出壳)。
2. 新增 `package/HoujiBacklightProtocol.h`=**逐字节**带入(sha 钉住,纲领 §4.9;若与我们 -Wextra 冲突→该 TU 级 -Wno 并注 CI run)。
3. series 新增 1 个补丁(或并入现有 houji 补丁):IOUserClient.cpp 两 hunk(①conforms 兜底 ②MACF 分支旁路),宏门改用我们的板级宏,借 hunk 逐行对照后手写进我们 series(不是整 patch 照搬——其 local.patch 与我们 series 基线不同)。
4. builder:两个新文件编译入 libNativeLegacyDisplay.late.a;late 归档静态纪律自查(其静态仅 POD 数组=合规,纲领 §4.11)。
5. board.toml args += `houji_bl=1`(43 参 ~858B<1000B);mkNN 断言:内核含 HOUJI BACKLIGHT 串、args 含 houji_bl、两文件 sha 记录、模板=uefi-touch1、seed 钉住、AMFI/gatecheck/ADT 照旧。
上机判据(一次 boot):好模式→读回心跳行 `HOUJI BACKLIGHT SNAPSHOT/…`——registered=1、boot-measured=0x52 读回初值(预期≈307,不符不阻塞=fallback);屏幕侧=用户拉亮度滑条看变化(lonrzi r229 判据)。失败模式按其码表 1-7 读 failure 字段立项,不盲改寄存器。
预期现象:亮度滑条生效;conforms/属性授权两条 sheng 教训由设计覆盖(int-236 同时发布、int-276 授权挂点)。

## 五、风险与未确认(假设申报, v3.3)

1. **面板变体**(未确认):我们这台是 n2 还是 n3 变体未实测——但两变体调光常量一致(0xb32/307/inverted-dbv),且 0x52 读回自校准,首轮以读回为准;不阻塞。
2. **DSI 时钟在内核期是否仍开**(未确认,设计内诊断):UEFI 交接后 DSI ctrl 寄存器是否可达=启动门槛 `(read(4)&5)==5` 直接回答;读不到=fail(1) 进读回,再立项。固件"votes retained"先例(r70 触摸上电)支持可达(推测)。
3. **iOS 27 CoreBrightness 兼容**(未确认,设计覆盖):契约=sheng 同 build 原文;conforms 由挂点满足、backlight-control 同时发布、backboardd 属性由 MACF 旁路满足——三件齐但组合在 iOS 27 上机前=推测。
4. ** Sandbox/AMFI 对背光属性的影响**(未确认):int-276 是 sheng iOS 27 实测,我们同 build 大概率同形(推测);挂点已覆盖,若仍被拒读回 failure/rejected 计数可判。
5. 自动亮度(环境光传感器)、熄屏/唤醒联动、HBM=本轮不做,列候补。

## 六、复评触发器

- lonzzi/nian0114 触及 HoujiBacklight*/DSI 的新提交→复扫。
- GPU 轮立项时:背光服务与 IOMFB/BacklightDisplay 的关系需再核(GPU 上线后 IOMFB 可能接管亮度通道)——留到 GPU 轮前置核对项。
