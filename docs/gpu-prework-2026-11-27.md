# GPU 轮前置三项(2026-11-27,零上机零写入)

证据分类:**实锤**=本次当场读到的文件原文(带路径:行号);**推测**=有推理无文件佐证;**未确认**=查不到。
前置阅读(不重复其内容):`GPU差异分析_740对750_2026-11-26.md`(含 §七 更正)、`src\UniXNU-houji-ios27\docs\gpu-740-vs-750-2026-11-26.md`。本文档把那两份往前推三步:①最小必须集、②寄存器逐项对照、③固件来源。
nian0114 源码定位说明:其 750 自举**不在 drivers/ 下**,在 `src\nian0114-houji\kernel\overlay\iokit\Kernel\houji-ufs-root\`(经 kernel\patches\local.patch 进 XNU 构建,实锤:local.patch:2139-2166)。`archive\` 为历史快照,本报告只引 live 覆盖层。

---

## §0 三句话大白话(给用户)

1. **上电要借的东西盘清楚了**:nian0114 那约 67 个自造文件,真正"不渲染也要"的是 A 层约 40 个(GMU 上电+HFI 握手+只读探针),渲染相关的 B 层约 20 个官方都有更新更强的版本——**B 层倾向不照抄,直接嫁接官方最新**;另有 6 个文件逐字节就是官方 kit 原文,不用"抄"。
2. **740 和 750 的寄存器"门牌号"一模一样,差的是"写什么值"**:而且官方 740 驱动根本不消费那两张表(HwInit 表在官方代码里 0 处引用,上电是 GMU 固件自己干的),所以"把官方表的数据换成 750"是空操作;**750 的寄存器数据要整块用 nian0114 的 GpuGen7Table.h(它本来就是 750 专属数据,来自一加 gen7_9_1 内核)**。
3. **固件能拿到**:GMU/SQE/AQE/ZAP 四件套在 Linux linux-firmware 里就有,尺寸和 nian0114 钉的完全对上(GMU 78,588、AQE 33,176、ZAP 12,088 一致;SQE 差 64 字节待核);装载走"构建期私有头文件 + sha256 钉死"的现成路线(和官方 sheng 同款),不碰手机也能先把四件钉进 firmware-inputs.json。

---

## §1 nian0114 的 750 自举"最小必须集"清单(A/B 两层)

**规模核实(实锤)**:本次清点 `kernel\overlay\iokit\Kernel\houji-ufs-root\` 下 GPU 自举文件共 **81 个文件 / 11,564 行**(含 6 个 vendor 文件、ring 2 件、HFI 3 件)。旧文档"约 67 文件/9,800 行"口径偏小(推测:未含 vendor/、ring 与部分 Houji 图形头),本文以实际清点为准。

**总装调用链(实锤)**:`HoujiUfsRead.cpp:585-598` 按序 include 11 个探针 .inc → `houji_ufs_root_attach()`(HoujiUfsRead.cpp:599)按 boot-arg 依次调 `houjiGpuCpuPageProbeOnce → houjiGpuMemoryProbeOnce → houjiGpuTableProbeOnce → (houji_gpu_handoff_probe) houjiGpuHandoffProbeOnce → (houji_gpu_held_tcm_probe) → houjiGpuProbeOnce → (houji_gpu_internal_exec_probe) → (houji_gpu_external_canary_probe) → (houji_gpu_fw_mem_probe) → (houji_gpu_scm_probe) → houjiGpuFullHostProbeOnce → (失败才) houjiGpuStockFirmwareProbeOnce`(HoujiUfsRead.cpp:629-685)。FullHost 链内部:`HoujiGpuFullHostProbe.inc:178` include NativeCp → `HoujiGpuNativeCp.inc:113` include NativeLive → `HoujiGpuNativeLive.inc:100` include NativeProvider → `HoujiGpuNativeProvider.inc:194` 调 `native_adreno_publish(...)`(官方后端入口)。

### A 层 = 上电必需(GPU 起来、GMU/HFI 握手、中断/只读诊断,不渲染)

#### A1 电源/总线/时钟

| 文件(相对 houji-ufs-root\) | 行数 | 职责一句话 | 依据 |
|---|---|---|---|
| GpuPowerReport.h | 90 | 电源报告线格式(gpp_report,EFI/XNU 共用 POD)。 | 被 GpuPowerPolicy.h include(GpuPowerPolicy.h:9);Handoff 校验消费(实锤 GpuPowerOnlyHandoff.h:91-107) |
| GpuPowerPolicy.h | 262 | 一次性固件电源准备策略(gpp_* 资源枚举,无回写)。 | 被 GpuBusProbe.h include(GpuBusProbe.h:8) |
| GpuPowerObserve.h | 78 | SM8650 电源窗口偏移观察("CX hw-control 是 0x9168,不是 sheng 的 0x953c")。 | 电源组观察数据(实锤头注释:9-11 行) |
| GpuBusProbe.h | 159 | 保持复位时总线/时钟门只读探测(hgb_*,8 项 CFG/MEMNOC/DVM/CX…白名单)。 | 被 GpuPowerOnlyHandoff.h:33 include;hph_run 调 hgb_probe(:339) |
| GpuClockWitness.h | 110 | GMU/HUB 时钟只读解码见证(hcw_*,绑定小米内核 gpucc-pineapple)。 | 被 GpuPowerOnlyHandoff.h:34 include;hcw_run 在 :316 调用 |
| GpuPowerOnlyHandoff.h | 429 | UEFI→XNU"仅保电"线格式(HPH1,1376 字节)+XNU 侧只读复查 hph_recheck。**这是官方没有的独创交接协议**。 | 依赖 A1 全组(:33-34);被 HoujiGpuHandoffProbe.inc/StockFirmwareProbe.inc:144 消费(hph_recheck) |
| HoujiGpuHandoffProbe.inc | 166 | handoff 探针(boot-arg houji_gpu_handoff_probe 门控)。 | HoujiUfsRead.cpp:632-636 调用 |

#### A2 GMU 上电/HFI 握手(核心)

| 文件 | 行数 | 职责 | 依据 | 官方等价物 |
|---|---|---|---|---|
| GpuFirmwareMemory.h | 131 | SM8650 固件/HFI 内存布局规划+审计(hfm_*,INACTIVE 状态,9 行资源)。 | 被 GpuStockGmu.h include;`ready()` 断言 15 记录/5 预留/77808 字节装载(houji_stock_native.c:88) | 有:官方 kit AdrenoFirmware 计划器(下) |
| vendor\AdrenoFirmware\AdrenoFirmware.c/.h | 67/37 | GMU 固件块计划器(16 字节头遍历)。 | 被 GpuFirmwareMemory.h include(:7) | **逐字节=官方 kits\AdrenoFirmware\AdrenoFirmware.c/.h(实锤 SHA256 C3CC714A/6B2E308F 相同)→直接用官方 kit,不照抄** |
| houji_hfi_inactive.c/.h + houji_hfi_private.h | 225/127/57 | HFI 内存"未激活"准备桥(hhi_prepare:分配 9 行、建 SMMU 表、校验固件,全部不触碰 MMIO)。 | HoujiGpuFirmwareMemoryProbe.inc:144 调 hhi_prepare;houji_stock_native.c:130 ready(h) 消费其结果 | 官方等价物=NativeAdrenoGmuContext/GmuMemory(GMU 上下文),模型不同 |
| houji_stock_native.c/.h | 193/122 | 一次性 C ABI 桥:把 hhi 结果+固件字节交给原味 hgf_run(HSN_ABI v2)。 | HoujiGpuStockFirmwareProbe.inc:255 调 hsn_run/hsn_run_host_profile;内部 houji_stock_native.c:143 调 hgf_run、:117 调 hhp_run | 无(官方是 NativeAdrenoService.cpp 的另一套流程) |
| GpuStockGmu.h | 245 | **核心**:gen7-9-1 原厂固件+HFI 就绪+一次属性查询(hgf_run;可选 HWSCHED 引导止步于资源请求;无 CP/PM4/PAS)。 | houji_stock_native.c:143 与 houji_full_host_runtime.c:99 两处调用;头注释(1-9 行) | 无 |
| GpuHfiBootstrap.h | 101 | HWSCHED 初始化(hhb_run,9 步,止步未知资源)。 | houji_stock_native.c:117 host_bootstrap 调 hhp_run(hhp_run 定义于 GpuHostProfile.h:21,基于 hhb 消息层) | 类似官方 kit AdrenoHfiStartup(消息编码同型) |
| GpuHfiTables.h | 42 | houji HFI 线格式表(make-gpu-hfi-tables.py 生成,自家 AOP/FDT 数据)。 | 被 GpuHfiBootstrap.h include(:7) | 官方等价物=AdrenoHfiVotes 板级数据→**嫁接官方 kit,数据取 nian** |
| GpuHostProfile.h | 36 | 主机档 GMU 冷启动协议(hhp_run,5 步,自编 perf/start 消息)。 | houji_stock_native.c:10 include;:117 调用 | 同上 |
| vendor\Ace5Gmu\giulia_gpu_firmware.h | 107 | GMU 固件布局解析(gg_fw_plan,块/区域合法性)。 | 被 giulia_gpu_memory.h include;GpuFirmwareMemory.h:9 include memory | 无(一加 Ace5 同 SM8650 的事实,官方没有) |
| vendor\Ace5Gmu\giulia_gpu_hfi.h | 78 | gen7 HWSCHED 线布局(17 队列/210 表字/NC 映射要求)。 | 被 GpuFirmwareMemory.h:10 include;ggh_send/receive 全链使用 | 类似官方 AdrenoHfi.h→嫁接官方 |
| vendor\Ace5Gmu\giulia_gpu_memory.h | 62 | GMU 分配元数据→4KiB 预留计划。 | 被 GpuFirmwareMemory.h:9 include | 无 |
| vendor\Ace5Gmu\giulia_gpu_scm.h | 150 | A750v2 固定 secure-CX 初始化(SCM 调用,一加 qcom_scm ABI)。 | 被 HoujiGpuSecureCxProbe.inc 使用 | 官方无(官方 sheng SCM 走 NativeAdrenoGmuPlatform) |
| HoujiGpuFirmwareMemoryProbe.inc | 150 | fw_mem 探针(boot-arg houji_gpu_fw_mem_probe),调 hhi_prepare。 | HoujiUfsRead.cpp:657-668;:144 调 hhi_prepare | — |

#### A3 SMMU 表基础设施(GMU 表+审计)

| 文件 | 行数 | 职责 | 依据 | 官方等价物 |
|---|---|---|---|---|
| ArmSmmu4K.c/.h | 108/61 | ARM SMMU 4K 表构建核心。 | local.patch:2144 进构建;houji_stock_native.c:136-139 调 ArmSmmu4KAuditTables | **vendor\ArmSmmu4K\ArmSmmu4K.c/.h 逐字节=官方 kits\ArmSmmu4K(实锤 SHA256 0ADB19B4/E25063B9 相同)→用官方 kit** |
| ArmSmmu4KAudit.c/.h | 172/64 | 表审计(6 表页/139 叶页/3072 描述符断言)。 | houji_stock_native.c:139、HoujiGpuStockFirmwareProbe.inc:205 | 官方 kit 同名文件 |
| ArmSmmu4KContext.c/.h | 84/47 | 上下文编码(CBA2R/CBAR/TCR/TTBR…SM8650 SMMU-I 0x4c017e18/0x5511)。 | HoujiGpuStockFirmwareProbe.inc:252 调 ArmSmmu4KEncodeContext | 官方 kit 同名 |
| ArmSmmu4KProgram.c/.h | 225(根)/nian 版 | 表程序构建。 | ArmSmmu4K.h 依赖链 | **与官方 kit 版不同(实锤 hash 3B1D4911 vs 93DF000C)→按官方 kit+nian 差异审查后取舍,不整抄** |
| ArmSmmu4KLive.c/.h、ArmSmmu4KStreams.c/.h | 585/135、170/68 | 表热更新/流管理(BO 发布时增量改表)。 | 被 NativeAdrenoVm/FullHost 图使用(houji_full_host_runtime.h:4 include Live) | 官方 kit 也有 Live/Streams→对照取舍 |
| GpuSmmuPrepare.h / GpuSmmuProfile.h / GpuSmmuContext.h | 92/16/158 | GPU 侧 SMMU:未激活表准备/ bank2 事实("GMU 用 bank2,SM8650")/禁用态上下文寄存器测试。 | 被 GpuStockGmu.h include(GpuStockGmu.h:7) | 无(官方 GMU 上下文在 NativeAdrenoGmuContext,模型不同) |
| HoujiGpuTableProbe.inc | 212 | 表探针(boot-arg 门控)。 | HoujiUfsRead.cpp:631 | — |
| HoujiGpuMemoryProbe.inc / HoujiGpuCpuPageProbe.inc(共享) | 193/104 | BO 池/CPU16K 页几何探针。 | HoujiUfsRead.cpp:629-630 | — |
| GpuCpuPageGeometry.h / GpuSharedMemory.h / HoujiGpuAddressLayout.h / HoujiGpuCanaryRanges.h | 63/74/16/19 | CPU16K/GPU4K 几何事实;共享内存池(HSC_POOL 0x61000000 系);4GiB 孔径/表页常量;金丝雀区段辅助。 | 全链依赖(GpuCodeMemory.h、GpuFirmwareMemory.h include) | 官方等价物=NativeAdrenoMemory 页几何;数据需换 houji 版 |

#### A4 GMU 微码探针(只读/固定微码, qualifying CM3/DMA 通道)

| 文件 | 行数 | 职责 | 依据 |
|---|---|---|---|
| GmuHeldTcm.h | 229 | 保持复位时 GMU 内部 TCM 只读+136 字节固定探针(hgd,sha 钉死)。 | 被 GmuInternalExec.h include(:10);boot-arg houji_gpu_held_tcm_probe(HoujiUfsRead.cpp:637-642) |
| GmuInternalExec.h | 299 | 固定 136 字节内部 CM3 程序执行试验(无固件/DDR/DMA;来源=一加 gen7_reg.h/adreno_gen7_gmu.c)。 | boot-arg houji_gpu_internal_exec_probe(HoujiUfsRead.cpp:645-650);HoujiGpuInternalExecProbe.inc(307 行) |
| GmuExternalCanary.h | 519 | XNU 持有的 bank2/ASID750/SID5 外部内存 DMA 金丝雀(houji_gmu_dma_probe.S,sha 钉死;正写+负读双证明)。 | boot-arg houji_gpu_external_canary_probe(HoujiUfsRead.cpp:651-656);HoujiGpuExternalCanaryProbe.inc(563 行) |
| GpuDmaCode.h / GpuGmuCode.h | 19/17 | 上述两个探针的固定微码字数组(生成物)。 | 被上两文件 include |
| HoujiGpuHeldTcmProbe.inc / HoujiGpuInternalExecProbe.inc / HoujiGpuExternalCanaryProbe.inc | 266/307/563 | 三个探针的 XNU 侧驱动。 | HoujiUfsRead.cpp:592-594 include,boot-arg 门控 |
| HoujiGpuSecureCxProbe.inc | 194 | 固定 secure-CX SCM 前置(boot-arg houji_gpu_scm_probe;stock 全链的前置资格)。 | HoujiUfsRead.cpp:669-673;houjiStockConfiguration() 断言 scm.init_succeeded(HoujiGpuStockFirmwareProbe.inc:50-54) |
| GpuPasProbe.h | 125 | PAS 能力**只读**查询(不加载)。 | 头注释(1-9 行);zap 链前置 |
| HoujiGpuProbe.inc / HoujiGpuProbeLog.inc | 65/65 | 基础 GPU MMIO 只读探针+日志。 | HoujiUfsRead.cpp:587/643 |
| GpuReadPolicy.h | 15 | 3 个电源窗口的只读白名单。 | 被 Handoff/Stock 探针复用 |

#### A5 固件/ZAP(上电收尾)

| 文件 | 行数 | 职责 | 依据 | 官方等价物 |
|---|---|---|---|---|
| HoujiGpuStockFirmwareProbe.inc | 263 | **stock 固件一次性尝试**(9 窗口 MMIO 白名单、时钟门序、2 检查点、release 后终局 panic+PSCI)。 | HoujiUfsRead.cpp:682;:245-262 主体 | 无 |
| houji_full_host_runtime.c/.h + houji_native_full_host_graph.c/.h | 150/110+150/111 | **全链运行时**:stock→host→bank0(LPAC)→GX→code bind→gen7 寄存器→pre-CP 边界(FHR_ABI v3;15 行内存图+3 固件资产 sha 断言)。 | HoujiGpuFullHostProbe.inc 驱动;:99 调 hgf_run,:69-84 依次 hup_run/hsc_run_lpac/hup_gx_run/hcb_run/h7c_run | 无 |
| GpuGxStart.h | 70 | 最小 DCVS 票+GX OOB 租约+只读快照(hup_gx_run)。 | houji_full_host_runtime.c:76 调用 | 无 |
| GpuCodeBind.h / GpuCodeMemory.h | 30/39 | 4 个 CP 指令基址绑定(SQE/AQE IOVA);自有 GPU 代码区规划(HCC 0x63000000,SQE 85,300B/AQE 33,176B)。 | houji_full_host_runtime.c:78 调 hcb_run;HCC 常量被全链引用 | 类似官方 NativeAdrenoCpInit(只绑 SQE)→嫁接官方 |
| GpuGen7Config.h / GpuGen7Table.h | 69/193 | **750 寄存器数据表**:102 项初始化操作+75 项电源寄存器清单(h7c_ops/h7c_power_regs,源自一加 graphics-kernel 453d8875 gen7_9_1)。 | houji_full_host_runtime.c:80 调 h7c_run;FHR_GEN7_OPS=102/PAIRS=75 常量对齐(houji_full_host_runtime.h:12-13) | **官方无此表(详见 §2)→必须整块借** |
| GpuZapAssets.h / GpuZapLoad.h / HoujiZapHandoff.h / HoujiZapTrial.h | 336/178/93/6 | ZAP 固件资产(内嵌 metadata 4044B+签名段 1072B,源 sha ab6035de…)+加载策略+低物理地址线保留(HZR 0x9b098000/0x9b09a000)。 | GpuZapAssets.h:1-11;HZR 常量 HoujiZapHandoff.h:7-11 | 官方等价物=sheng GpuPowerProbe.cpp zapAndSecure(PAS13)+build 嵌入 native_adreno_zap_image→**嫁接官方加载器,资产取 houji 版** |
| HoujiGpuFullHostProbe.inc | 197 | 全链探针(mode3 组合下半管道)→ 收尾进 NativeCp/Live/Provider。 | HoujiUfsRead.cpp:674;include 链(见总装) | — |

**A 层小结**:上电必需 ≈ 56 个文件(A1-A5 合计,其中 5 个逐字节=官方 kit 原文,可"嫁接不照抄")。

### B 层 = 渲染增强(提交队列、缓冲、per-context、性能)

| 文件 | 行数 | 职责 | 依据 | 官方等价物 |
|---|---|---|---|---|
| NativeAdrenoRing.cpp/.h | 306/115 | 精简 ring 实现(128 IB/42 词帧捕获+WPTR 校验)。 | 被 NativeCp/Provider/Submit 使用(NativeAdrenoRing.h:11-13) | **官方 NativeAdrenoRing.cpp 37,851 字节新版远超此件→用官方** |
| GpuHostRing.h | 157 | gen7 host ring 固定初始化+2 个设备写金丝雀(HHR_*;ring 寄存器 0x3d02000…0x3d02a64)。 | 被 HoujiGpuNativeCp.inc:7 include;官方等价=NativeAdrenoRing 的 ring 部分 |
| HoujiGpuNativeCp.inc | 137 | CP_INIT+两 MEM_WRITE 固定档(r131 profile;SQE 使能 0x3d02020=1、WPTR 门控)。 | FullHost after_prepare 回调进入(FhrOps.ops.after_prepare;houji_full_host_runtime.c:83) | 官方 NativeAdrenoCpInit.cpp→嫁接 |
| HoujiGpuNativeLive.inc | 178 | 同主 live 交接+UFS DMA 共存资格(fs_trial/coexist;gHoujiLive)。 | NativeCp:113 include;HoujiUfsRead.cpp:675-680 依据 gHoujiLive.retained 决定后路 | 无(状态机层) |
| HoujiGpuNativeProvider.inc | 225 | IOKit 包装+发布:gHoujiDeviceInfo={0x43051401,3<<20,…}(NativeProvider.inc:31)+provider 轮次(BO 分配/2D blit/CRC 0xe843b26a)+DVFS。 | :194 调 native_adreno_publish 官方后端 | **角色=官方 NativeAdrenoService.cpp→用官方 Service,nian 只出数据** |
| HoujiGpuNativeSubmit.inc / NativeVm.inc / NativeClient.inc / NativeGfx.inc | 122/152/132/152 | 提交状态/VM 绑定/客户端/图形探针(gHoujiSubmit、gHoujiVm.binding 12 boot 区、NC 视图证明)。 | Provider/FullHost 链 include | 官方 NativeAdrenoSubmit/Vm/UserClient→**整块用官方最新版** |
| GpuKnownImage2d.h | 298 | 已知 2D 图 GPU 读回(CP_BLIT,Mesa 派生命令;CRC 判据)。 | Provider 轮次引用(hgki_*,NativeProvider.inc:162-166) | 无(测试资产,保留) |
| GpuFirstContext.h | 100 | 首个私有上下文提交前缀(**OFFLINE 候选,未集成**)。 | 头注释"OFFLINE candidate; Not integrated"(3-5 行) | 官方 per-context 在 NativeAdrenoGpuContext.cpp→嫁接官方 |
| GpuScratchContext.h | 143 | bank0 普通域 scratch 上下文(SID0/SID1 附加)。 | FullHost bank0 阶段(hsc_run_lpac,houji_full_host_runtime.c:74) | 官方 NativeAdrenoSmmuContext→对照 |
| GpuDesktopPerfProfile.h / HoujiGpuFactoryOpp.h / HoujiGpuGovernorPolicy.h / HoujiGpuDvfs.inc | 75/41/61/117 | 桌面性能档(CP 前 HFI30 500MHz 票)/12 档 OPP 表(231000→903MHz)/触屏唤醒门控/DVFS 轮询线程。 | NativeProvider.inc:52 include Dvfs;hup_perf 被 FullHost 使用 | 官方等价物=AdrenoHfiVotes 板级数据→**数据取 nian,编码用官方 kit** |
| HoujiGpuCoherencePolicy.h/.inc(HoujiGpuCoherenceProbe.inc) | 43/238 | CPU16K 两向一致性证明(引用 sheng GpuPowerProbe.cpp coherenceProbe)。 | NativeProvider/FullHost 引用 | 无(自有诊断,可保留) |
| HoujiGpuClientLayout.h | 31 | 客户端窗口布局(5 客户端/Preferences 槽)。 | NativeVm/Client 引用 | 官方 NativeAdrenoVmAlias→对照 |
| Adreno740\(26 个改造后端文件) | ~1,180 行级 | 官方 31 文件子集的 iOS16/houji 改造版(Publish 1,209 行/Submit 331/Vm 517/UserClient 728/Memory 362…;ABI v1/maxBuf16+method17)。 | local.patch:2141-2166 进构建 | **=官方通用后端,但绑 iOS16 ABI→整块换官方现行主线(ABI v2/maxBuf128),不照抄**(此前文档已定,实锤复认:Adreno740\NativeAdrenoABI.h:14 `NativeAdrenoAbiVersion = 1`,官方为 version=2) |

**B 层小结**:渲染增强 ≈ 25 件,其中 8 件有官方现行等价物且官方版更强,其余多数会随"整块借官方后端"被替代;真正需要从 nian 保留下来的 B 层资产只有:**数据表(OPP/HFI 表)、GpuKnownImage2d 测试资产、Coherence 探针**。

**官方等价物复认(实锤,SHA256 相同)**:vendor\AdrenoFirmware\AdrenoFirmware.c/.h=官方 kits\AdrenoFirmware 同名件;vendor\ArmSmmu4K\ArmSmmu4K.c/.h=官方 kits\ArmSmmu4K 同名件;Adreno740\NativeAdrenoFirmware.cpp=官方现行驱动同名件(87606CAC)、NativeAdrenoSha256.cpp(EE624A48)。

---

## §2 gen7_9_1 寄存器表 vs 官方 A740HwInit/PwrupRegs 逐项核对

### ② 方法与总表

官方 `NativeAdrenoA740HwInit.h` 用 **dword 偏移**(自述"Generated by derive-a740-hwinit.py",NativeAdrenoA740HwInit.h:5-6);nian `GpuGen7Table.h` 用**字节绝对地址**(0x3d00000 基)。换算 `字节=0x3d00000+4×dword` 后逐项求交(本次脚本执行,实锤):

**55 个 A740 定义 → 30 个在 750 表中同名同址命中;25 个未命中**。

命中样例(名字/官方 dword/换算地址/750 表地址):RBBM_INT_0_MASK 0x00038→0x03d000e0 ✓;RBBM_SECVID_TSB_CNTL 0x0f803→0x03d3e00c ✓;UCHE_TRAP_BASE 0x00e09→0x03d03824 ✓;UCHE_CACHE_WAYS 0x00e17→0x03d0385c ✓;CP_AHB_CNTL 0x0098d→0x03d02634 ✓;CP_PROTECT_CNTL 0x0084f→0x03d0213c ✓;CP_APRIV_CNTL 0x00844→0x03d02110 ✓;CP_BV_APRIV_CNTL 0x00ad0→0x03d02b40 ✓;CP_LPAC_APRIV_CNTL 0x00b31→0x03d02cc4 ✓;RB/TPL1/SP/GRAS/UCHE 各 NC_MODE_CNTL 全部同址 ✓;GPU_GMU_AO_GMU_CGC_{MODE,DELAY,HYST}_CNTL 0x23b09/0a/0b→0x03d8ec24/28/2c ✓;CP_PROTECT 0x00850→0x03d02140 ✓。(完整 55 行对照已执行,以上为代表性摘录;方法=§5 索引 e。)

25 个未命中分三类(实锤,分类依据=本次 grep):
1. **地址其实存在于 nian 的 ring/CP/Gx 代码(17 项)**:CP_RB_BASE/CTL/RPTR_ADDR(0x3d02000/08/10, GpuHostRing.h:89)、CP_RB_RPTR/WPTR(0x3d02018/1c, GpuHostRing.h:76,102)、CP_BV_RB_RPTR_ADDR(0x3d02a60, GpuHostRing.h:89、HoujiGpuNativeCp.inc:48)、CP_SQE_INSTR_BASE(0x3d020c0, GpuCodeBind.h:11)、CP_SQE_CNTL(0x3d02020, HoujiGpuNativeCp.inc:51)、RBBM_STATUS(0x3d00840, GpuGxStart.h:64、GpuHostRing.h:60)、RBBM_INT_0_STATUS(0x3d00804, GpuHostRing.h:61)、CP_HW_FAULT(0x3d02084, GpuHostRing.h:61)、GBIF_HALT(0x3d0f114, GpuStockGmu.h:32、HoujiGpuNativeCp.inc:59)、RBBM_GBIF_HALT(0x3d00058, HoujiGpuNativeCp.inc:23,59)、CP_ALWAYS_ON_COUNTER(0x3d02600, HoujiGpuNativeCp.inc:23)、CP_APERTURE_CNTL_HOST(0x3d02800, HoujiGpuNativeCp.inc:23,59)。**即这些"无对应"只是不在 GpuGen7Table.h 里,地址映射仍然完全一致。**
2. **设计性缺席(推测,有同型先例)**:RBBM_SECVID_TRUST_CNTL(0x3d3d000)——nian 走 ZAP/PAS 路线,不写 TRUST(对照官方 sheng 的注释 "sheng uses a zap shader, so RBBM_SECVID_TRUST_CNTL is never written",UniXNU-sheng\package\GpuPowerProbe.cpp:40-41);RBBM_SW_RESET_CMD(0x3d0010c)——nian 全链"无复位、保留到平台重启"(GpuStockGmu.h:8-9 头注释),无 SW reset;RBBM_INT_CLEAR_CMD(0x3d000dc)——nian 中断走 GMU GMU2HOST 通道,未发现 GPU int clear。
3. **真无对应(9 项)**:RBBM_CLOCK_CNTL(0x3d002b8;750 用**相邻的另一寄存器** RBBM_CLOCK_CNTL_GLOBAL 0x3d002b4+GLOBAL_LOAD_CMD/P2S 触发加载,GpuGen7Table.h:108-112)、RBBM_PERFCTR_CNTL(0x3d01400)、RBBM_NC_MODE_CNTL(0x3d014d0)、CP_INTERRUPT_STATUS(0x3d0208c)、CP_SQE_STAT_ADDR/DATA(0x3d02420/24)、RBBM_STATUS3(0x3d0084c)、RBBM_INTERFACE_HANG_INT_CNTL(0x3d0007c 在表但 CGC 组外)。

**PwrupRegs 16 项(官方 NativeAdrenoPwrupRegs.h:8-25,dword 0xe09…0xb602)**:换算后 16/16 全部命中 nian `h7c_power_regs`(GpuGen7Table.h:115-191,含 RB_CONTEXT_SWITCH_GMEM_SAVE_RESTORE 0x3d23940、UCHE_GBIF_GX_CONFIG 0x3d038e8 等)。实锤。

### ②' 值的差异(同为"写"时)

**CP_PROTECT 对照(实锤,脚本执行)**:官方 46 项(NativeAdrenoA740HwInit.h:120-167)vs nian 48 项连续写(GpuGen7Table.h:48-95):**同值仅 5 项**(idx0,1,7,25,41),**41 项值不同**,且 750 多出 idx45/46 两项。740 列表从 idx9 起与 750 错位(750 在 idx2-6 插入了 0x04f40584/0x00000706/0x007c0720/0x027c0760/0x82080800 等 750 专属保护区间)。**结论:保护表必须整表用 750 版,不能沿用官方 46 项。**

**CGC(时钟门控)机制差异(实锤)**:官方是 54 项逐块 Hwcg 值表(NativeAdrenoA740HwInit.h:62-117)+46 项 Protect,但**该表在官方驱动中 0 处被引用**(本次 grep A740_*/Hwcg/Protect 于 drivers\Adreno740\*.cpp 仅命中 Pwrup 计数宏,实锤)——官方真正做的只有:PwrupRegs **只读**采样打包给 CP_INIT(NativeAdrenoPwrup.cpp:12-22,由 NativeAdrenoService.cpp:479 调用)、IRQ 屏蔽/清除(IrqRbbmMask=0x38/IrqRbbmClear=0x37,NativeAdrenoIrq.cpp:12-13,84,99)、WPTR/GBIF halt(Preempt/Ring)、SQE 基址(CpInit)。**其余初始化(UCHE/NC/PROTECT/CGC)在官方模型里由 GMU 固件+CP_INIT+SQE 微码自身完成**。nian 则是驱动直接写 102 项(一加内核模型)。

### ③ 结论:嫁接还是整块换

1. **"只换官方数据表"这条路不成立(实锤)**:官方主线根本不消费 A740HwInit 表;官方 740 上电模型="嵌入 GMU 固件 → GMU 上电 → CP_INIT(消费 pwrup reglist+SQE)→ GMU HFI 投票"(NativeAdrenoService.cpp:352-420)。把这张表换成 750 值没有任何代码会读它。
2. **必须整块借的是 nian 的 A 层自举(约 56 件,§1)**,因为它实现了官方驱动里不存在的能力:驱动自主上电+固件规划+HFI 握手+secure-CX SCM+ZAP 认证。这块是"数据+探针"形态,与官方后端通过 `native_adreno_publish(NativeAdrenoVm*,Submit*,Ring*,…,DeviceInfo*)`(HoujiGpuNativeProvider.inc:8)对接——**嫁接缝天然存在,不需要改官方后端**。
3. **伴随常量清单(必须一起换,不只换一张表)**:
   - GMU 固件三件 bytes/sha(§3;nian 已钉 78,588/85,300/33,176,firmware-inputs.json:2-16);
   - CP_PROTECT 48 项 750 值(GpuGen7Table.h:48-95);
   - CGC 机制:750 用 GLOBAL_LOAD_CMD 序列(GpuGen7Table.h:108-112),官方 54 项 Hwcg 的 750 逐块值**nian 未带、上游亦未见=未确认**;
   - HFI perf 表/OPP 12 档(hhb_perf GpuHfiTables.h:9-14、houjiGpuOpps HoujiGpuFactoryOpp.h:10-23);
   - 板级 GMU 常量:官方 SHENG_GMU_*(Command DB 基址/VOTES_CHIP_ID/CORE_GPU_DELTA=0x6a000,NativeAdrenoService.cpp 引用)是 sheng 板级,houji 换 nian 的 giulia/houji 窗口常量(kHoujiStockBase 9 窗口,HoujiGpuStockFirmwareProbe.inc:15-16);
   - pwrup 16 项偏移**无需换**(16/16 同址,实锤);
   - RSC/PDC:官方 19 项 setup 由 prepare-sheng-gmu.py 从 Linux a6xx_gmu.c A740 变体推导(UniXNU-sheng\scripts\prepare-sheng-gmu.py:77-85);nian 750 链无独立 RSC 表(由 GMU 固件自身流程覆盖,推测:未在 hgf/hhb 路径外发现 RSC 写序列)。
4. **不要照抄的**:nian 的 Adreno740\26 件改造后端(ABI v1/16+method17 与官方 v2/128 分叉)、NativeAdrenoRing 精简版、HoujiGpuNative*.inc 状态机(角色=官方 Service)。

---

## §3 A750 固件来源确认(sqe/gmu/zap)

### ① 官方 kit 与 sheng 先例(实锤)

- 官方 `kits\AdrenoFirmware`(README:3-5 "GMU cold start and HFI work on sheng hardware")**只含协议/计划器代码**(AdrenoFirmware.c=固件块计划器、AdrenoHfi/HfiStartup/HfiVotes=HFI 消息编码),**不含固件字节**。
- sheng 的固件装载=**构建期嵌入**:`UniXNU-sheng\scripts\build-adreno740-kits.py:64-66` 把三个文件嵌为 weak symbol——`_native_adreno_firmware_image`=qcom/gmu_gen70200.bin、`_native_adreno_sqe_firmware_image`=qcom/a740_sqe.fw、`_native_adreno_zap_image`=qcom/sm8550/sheng/a740_zap.mbn;原始文件在 `.cache/firmware/sheng-gpu/`(collect-linux-readonly.py:79 收集,未进仓)。
- 钉法:bytes+sha256 由 `scripts/data/adreno-inputs.json` 钉、`prepare-sheng-gmu.py:91-99` 生成 `ShengGmuSource.h`(仅 bytes 数与 32 字节 sha 常量);驱动运行时 `NativeAdrenoFirmware::validate`(NativeAdrenoFirmware.cpp:5-18)对嵌入镜像做 sha256 校验(NativeAdrenoService.cpp:352-368;无嵌入时打 "no embedded GMU firmware")。
- ZAP 加载(官方 740 已验证路径):`GpuPowerProbe.cpp:621-703` zapAndSecure——MDT/ELF 解析→PAS13 INIT_IMAGE→MEM_SETUP→段拷贝→AUTH_AND_RESET→CP_SET_SECURE_MODE(0)(boot-arg gpu-zap 分级)。

### ② nian0114 的 750 固件处理(实锤)

- **嵌入+钉死**:`kernel\firmware-inputs.json` 钉三件——GMU 78,588B sha 9685f62c…、SQE 85,300B sha 8e2c5ce7…、AQE 33,176B sha 25237c16…;`tools\make-firmware-headers.py:10-28` 从用户提供的 gmu/sqe/aqe 文件生成**私有头文件**(HoujiGmuFirmwareImage.h、HoujiCpFirmwareImages.h,sha 不符即拒绝),放入 houji-ufs-root\ 编译进内核;**这两个头文件不在仓库里**(glob 无命中,实锤;与其 AGENTS.md "不提交固件字节"一致)。development.md:58-68 自述此流程。
- **ZAP 不是文件而是内嵌资产**:GpuZapAssets.h:3-8——"Generated by scripts/make-gpu-zap-assets.py from this phone's signed OEM image",源 sha `ab6035de…1981931e`,内嵌 metadata 4044B(ELF 头+phdr+hash 段)+签名段 1072B;运行时经 HoujiZapHandoff(0x9b09a000 gpu_microcode_region,8KiB 保留)+PAS13 认证(方案=lonzzi gpu-zap-native-handoff.md 全文)。
- 头文件缺失时编译如何失败/是否必需=未确认(推测:make-firmware-headers 产物缺失时链接失败)。

### ③ Linux 上游(实锤,2026-11-27 main 树 web 核实)

linux-firmware 现有(gen7_9_1 = A750/SM8650 家族,注意**上游现名不是 a750_***):
- `qcom/gmu_gen70900.bin` **78,588 字节** —— 与 nian 钉的 GMU bytes 完全一致;
- `qcom/gen70900_aqe.fw` **33,176 字节** —— 与 nian 钉的 AQE 完全一致;
- `qcom/gen70900_sqe.fw` **85,364 字节** —— 与 nian 钉的 SQE 85,300 差 **64 字节**(推测=版本更新或头部差异;装载裁剪解释不通:GpuCodeMemory.h:12-13 钉 HCC_SQE_BYTES=85,300,而 giulia_gpu_firmware.h:101-102 的注释只说"SQE/AQE 装载时省略文件首 dword"=4 字节);
- `qcom/sm8650/gen70900_zap.mbn` **12,088 字节** —— 与 lonzzi 用的 OEM 文件(source_bytes=12,088,zap-assets-source.json:5)尺寸一致;
- 上游 DTS 用法佐证:[sm8650-qrd 补丁](https://www.mail-archive.com/freedreno@lists.freedesktop.org/msg29883.html) `firmware-name = "qcom/sm8650/gen70900_zap.mbn"`;Ubuntu 26.04 Snapdragon X Elite(同 Adreno 750)指南确认 `linux-firmware-qualcomm-graphics` 提供 A750 GPU 固件([讨论帖](https://discourse.ubuntu.com/t/ubuntu-concept-snapdragon-x-elite-48800/137))。
- **sha 逐字节相同与否=未确认**(git.kernel.org 现有 Anubis 反爬,本次未下载校验;尺寸证据支持 GMU/AQE/ZAP 同物)。

### ④ 我们的 userdata 路线(实锤)

- payload=`[payload]` HOUJIPAY FAT 卷 96MiB(loader="firmware",board.toml:81-84)+`[payload.runtime]` trust_components 三件(board.toml:170-181),`gpu_addon=false` 且注释"houji GPU addon 尚未配置"(board.toml:180-181)。
- **NGPUADD2 GPU addon 不是内核固件槽**:官方 make-gpu-addon.py:1-20 自述它装的是**用户态** NativeAdrenoMetal.bundle 等文件,经 /chosen/memory-map/GpuAddon 由内核写进 Data 卷——与内核固件无关,不要往里塞 a750 固件。
- 内核固件的两个可用槽位(均有先例):
  1. **构建期嵌入(推荐,=sheng 模式)**:固件文件作为"不进仓的板级输入"(与 UNIXNU_UNIQUE_CHIP_ID 同类,设备仓 AGENTS.md 明言固件 checkout 不在任何仓库)由 CI fork 提供,构建脚本把 gmu/sqe(/aqe)嵌为 weak symbol,bytes+sha256 钉进生成的头;官方 NativeAdrenoFirmware::validate 现成可用。
  2. **文件系统装载(备用)**:内核已有"从 GPT 分区 FAT 读固件文件"先例(nian battery\FatVolume.c+HoujiFirmwareGpt.h:46 modem_a,用于 ADSP;houji-houji 同仓)与 Data 持久窗(sheng-data-persist=2,board.toml:113-116)——GPU 固件走这条路**没有任何先例=未确认**,且引入"GPU 上电前要先有 UFS/文件系统"的顺序耦合,不推荐首轮使用。

### ⑤ lonzzi(实锤)

`src\lonzzi-houji\archive\native\gpu\zap-assets-source.json`:源=`gen70900_zap.mbn`(12,088B,sha ab6035de…,来自 houji-gpu-firmware-20261025 提取),metadata=ELF 头[0,148)+hash[8192,12088)、段=1072B@4096、目标 0x9b09a000、PAS13、保留 8,192B;`archive\docs\gpu-zap-native-handoff.md` 给出完整 UEFI→XNU 交接方案(16KiB 页含 IPA/GSI 尾部,只准 NC 映射+精确 1072B 写)。**lonzzi 没有 GMU/SQE 文件级处理**(其 GPU 止步于 UEFI 上电/金丝雀)。nian 的 GpuZapAssets.h 与 lonzzi 资产同源(sha/尺寸完全一致,实锤)。

### 结论:能拿到;槽位建议

| 固件 | 能否拿到 | 来源 | 槽位 | 钉法 |
|---|---|---|---|---|
| GMU(gen70900_gmu) | **能** | linux-firmware qcom/gmu_gen70900.bin(尺寸与 nian 钉一致)或手机 vendor 分区 | 构建期嵌入 weak symbol(首选) | bytes=78,588+sha256(nian 已给 9685f62c…) |
| SQE(gen70900_sqe) | **能,但要重钉** | linux-firmware 现版 85,364B(nian 钉 85,300B,差 64B)或手机 vendor 对版 | 同上 | 下载后重算 bytes+sha256,更新 firmware-inputs.json |
| AQE(gen70900_aqe) | **能** | linux-firmware(33,176B 与 nian 一致) | 同上 | 25237c16… |
| ZAP(gen70900_zap) | **能** | linux-firmware qcom/sm8650/gen70900_zap.mbn=lonzzi/nian 用的 OEM 文件(尺寸一致) | 构建期嵌入 `_native_adreno_zap_image`+PAS13 加载器(借官方 zapAndSecure 模型) | ab6035de…(下游已钉) |

**sh256 钉死的最省事路径**:沿用 nian 的 `firmware-inputs.json + make-firmware-headers.py` 机制(现成、无改动、缺文件即构建失败),或改用官方 ShengGmuSource.h 生成器模式;二选一,建议前者(数据已是 750 的)。

---

## §4 未确认清单 + 复评触发器

1. **SQE 64 字节差**:nian 钉 85,300 vs linux-firmware 现 85,364。需下载 gen70900_sqe.fw(或从手机 vendor 分区提取 a750_sqe.fw)算 sha 对版。(未确认)
2. **四件固件的 sha256 与 nian/lonzzi 钉值是否逐字节相同**:尺寸三件一致,sha 未核(git.kernel.org 反爬)。(未确认)
3. **官方 `src\UniXNU-official` 当前 HEAD 是否=2962e28**:按纪律本次不做 git 操作,引用按工作树现状。(未确认)
   - 【lead 核注 2026-11-27 15:1x,闭合为实锤】本会话用户已批准并完成参考仓更新:official 主树=e7bddb9,`git submodule status` 实测 drivers/Adreno740=2962e289c9690aeb668072c170679e6fb28bf8de(=报告引用的工作树版本,AdrenoMetal=b25e48e/ArmCpu=5f37026/AdrenoFirmware kit=20ad3d7 同批更新)。报告所有"官方现行"引用即最新 pin。
4. **HHI_FIRMWARE_BYTES 与 bytes_loaded==77808 的关系**(houji_stock_native.c:88):78,588−7,780?未读 houji_hfi_inactive.h 全段,推测 780B 为头部/元数据不进装载。(推测)
5. **750 的逐块 Hwcg 值**(官方 54 项表的 750 对应):nian 未带,上游未见;若 GLOBAL_LOAD_CMD 路径在 750 生效则不需要(推测,待 GPU-0 验证)。
6. **ZAP/PAS 在 iOS27+XNU 的可用性**:sheng 740 已证;750 未上机。(未确认)
7. **"真无对应"9 项**(§2)是否在 nian 其他未检索文件中出现:本次只做定点 grep。(未确认)
8. **nian0114 头文件缺失时的编译行为**(§3②)。(未确认)

**复评触发器**:①linux-firmware gen70900_* 任一文件更新或改名;②官方 UniXNU/Adreno740 出现 750/gen7_9_1 支持或 houji 设备页;③nian0114 提交触及 Gpu*/firmware-inputs/GpuGen7Table;④#315/AdrenoMetal#71 合并;⑤SQE 对版结果与 nian 钉值不符;⑥GPU-0 上电日志显示 GMU 固件版本与钉值不符。

---

## §5 证据路径索引

- 前置文档:`GPU差异分析_740对750_2026-11-26.md`;`src\UniXNU-houji-ios27\docs\gpu-740-vs-750-2026-11-26.md`
- nian0114 自举主目录:`src\nian0114-houji\kernel\overlay\iokit\Kernel\houji-ufs-root\`(本报告所有 Gpu*/Gmu*/Houji*/houji_*/NativeAdreno*/vendor\ 引用的根)
- 构建接入:`src\nian0114-houji\kernel\patches\local.patch`:2139-2166
- 调用链:HoujiUfsRead.cpp:585-598(include 序),:629-685(boot-arg 门控调用);HoujiGpuStockFirmwareProbe.inc:255(hsn_run);houji_stock_native.c:88,117,143(hgf/hhp);houji_full_host_runtime.c:69-99,150(FHR 链);HoujiGpuFullHostProbe.inc:178→HoujiGpuNativeCp.inc:113→HoujiGpuNativeLive.inc:100→HoujiGpuNativeProvider.inc:31,194
- 寄存器:GpuGen7Table.h:10-113(h7c_ops 102 项),:115-191(h7c_power_regs 75 项),:192(常量);官方 src\UniXNU-official\drivers\Adreno740\NativeAdrenoA740HwInit.h(全文)、NativeAdrenoPwrupRegs.h(全文)、NativeAdrenoPwrup.cpp:12-22、NativeAdrenoService.cpp:330-420,479、NativeAdrenoIrq.cpp:12-13,84,99、NativeAdrenoRing.cpp:18,178,539、NativeAdrenoCpInit.cpp:14,43-44、NativeAdrenoPreempt.cpp:13-16
- 固件:src\nian0114-houji\kernel\firmware-inputs.json(全文);tools\make-firmware-headers.py(全文);docs\development.md:58-68;官方 kits\AdrenoFirmware\(README+AdrenoHfiStartup.c);UniXNU-sheng\scripts\build-adreno740-kits.py:64-66、prepare-sheng-gmu.py:77-105、package\collect-linux-readonly.py:79、package\GpuPowerProbe.cpp:27-44,621-703;官方 scripts\make-gpu-addon.py:1-20、scripts\unixnu\runtime.py:84-99
- 设备仓:src\UniXNU-houji-ios27\board.toml:81-84,113-116,170-181
- lonzzi:src\lonzzi-houji\archive\native\gpu\zap-assets-source.json(全文)、archive\docs\gpu-zap-native-handoff.md(全文)、archive\scripts\make-gpu-zap-assets.py
- 拆轮口径:修复顺序重议_性价比版_2026-11-27.md:86-103(§十/§十一),:112-123(执行记录)
- web:[linux-firmware qcom 目录](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/qcom?h=main)(gmu_gen70900.bin 78,588 / gen70900_sqe.fw 85,364 / gen70900_aqe.fw 33,176)、[qcom/sm8650 目录](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/qcom/sm8650)(gen70900_zap.mbn 12,088)、[sm8650-qrd DTS 补丁](https://www.mail-archive.com/freedreno@lists.freedesktop.org/msg29883.html)、[Ubuntu X Elite Adreno750 指南](https://discourse.ubuntu.com/t/ubuntu-concept-snapdragon-x-elite-48800/137)

---

## 建议的 GPU 轮拆轮方案(与 §十/§十一口径一致:一轮改一类、好/坏/回退/判据)

前置依赖:多核轮(h2 收线)+ 内存轮 8GiB 收线先落地(修复顺序 §十 序 1-2;nian0114 在 8GiB 机上仍遇 OutOfMemory(Device),GPU 轮在 2GiB 上基本不可行,§九发现 B)。三轮都是"单变量=一个 boot-arg/一组数据文件",全部可 boot-arg 关闭回退——nian 的探针体系天然按 `houji_gpu_*_probe` 门控(HoujiUfsRead.cpp:629-685),与"回退=镜像回滚"双保险。

**GPU-0 上电轮(A 层嫁接,不渲染)**
- 改动:A1-A5 自举块进我们的 iOS27 内核(数据嫁接:GpuGen7Table/GpuHfiTables/GpuZapAssets 取 nian;机制嫁接:ArmSmmu4K/AdrenoFirmware 取官方 kit)+ 固件四件嵌入(firmware-inputs.json+私有头);**不接 B 层、不接官方后端、不接 Metal**。
- 好=按序日志出:`handoff recheck OK → secure-CX OK → fw_ready=1 hfi_ready=1 query_ack=1`(HOUJI STOCK/PROVIDER 同型判据,HoujiGpuStockFirmwareProbe.inc:152,237-243)+ 只读诊断表(UCHE/RBBM_STATUS/gfsr)无错;坏=GMU 看门狗/掉电/panic(与 nian 的 HPH_RETAIN/terminal 设计一致,失败路径全是 PSCI 保留);回退=关 boot-arg(镜像不变)。
- 判据升级线:GMU 固件 sha 校验日志(nian 钉值 vs 实际)== §3 SQE 对版的最终实锤。

**GPU-1 提交轮(B 层前半+官方通用后端)**
- 改动:官方现行 31 文件后端(ABI v2)按官方方式接入 + nian 的 ring/CP 段降级为"数据/校验器"(GpuHostRing 帧格式、GpuKnownImage2d CRC 资产保留);CP_INIT 走官方 NativeAdrenoCpInit。
- 好=内核日志出 fence 完成+known-image 读回 CRC 一致(GpuKnownImage2d 判据,Provider 轮次 CRC 0xe843b26a 同型);坏=TSB fault/GPU hang(protect 表错值的第一嫌疑=CP_PROTECT 48 项移植错误,§2);回退=关 houji_gpu_probe 组。
- 判据:至少 1 条固定 2D blit 全字读回一致;不要求帧率。

**GPU-2 Metal 轮(上半全链)**
- 改动:官方 AdrenoMetal(ios27,注意 #315 编译开关)+metal2vulkan/Turnip + payload trust;GPU addon 仍不开(它是 Data 卷用户态插件容器)。
- 好=SpringBoard 拿到 Metal device+完成 command buffer(draws 计数,判据=官方 sheng "149 draws" 同型,§七 口径);坏=用户态黑屏/掉帧(nian 天花板:掉帧+紫影已知,不承诺流畅);回退=回到 GPU-1 状态镜像。
- 判据:iconservicesagent/SpringBoard 拿到 Adreno Metal device + ≥1 完成缓冲 + 用户目测设置列表可滚动(体感判据按纲领十八)。

(执行记录区待各轮收线后按 §十三 格式追加。)
