# 背光 B 前置:AppSync 集成先例四源扫描(2026-11-28,B 轮开工日完成)

状态:纯零上机,未动任何代码/补丁/配置/CI。B 轮开工直接引用本文件;结论届时写进 porting.toml display findings 随 B 轮 commit(与前例:背光 A 前置文件随 A 轮 commit 同型)。
机制预读基线=`work\背光A前置_四源扫描与属性清单_2026-11-28.md` §六(BacklightMode.m 机制、根因链),本文件不重复,只补四源扫描与打包链前提核对。

## 〇、开工三查(纲领十七)

- 本轮动作:AppSync 集成先例四源扫描 + 打包链前提逐条核对(纲领四"不满足当场立案")
- 借了谁的什么:见 §二(B 轮实施清单,**暂未动手**)
- 四源扫描记录在哪:本文件

## 一、四源扫描记录(纲领四,按序,全零上机)

1. **①官方同 SoC(UniXNU-sheng / UniXNU-official)= 借这里,全链先例完整**
   - sheng(启用态先例,设备仓已提交):
     - `patches/xnu-12377/series:18-19` = sheng-appsync-runtime.patch + sheng-appsync-user-trust.patch;位置=sheng-wifi-launchd-cache(:11)→bluetooth(:12)之后、edge-matrix(:20)之前;
     - `board.toml:61` kernel_args 末位 `"appsync=1"`;`board.toml:82` `[payload.runtime] appsync = true`;
     - `porting.toml:1406`:"The series ends with sheng-appsync-runtime.patch and sheng-appsync-user-trust.patch. appsync=0 disables their mounts and registration; trust_cache_rt_storage stays writable in either case.";`:1407`=exec 平台位清除条;`:478`=appsync=1 上机实锤(打字/Caps Lock/触摸板/指针滚动/合盖禁用全通,B st=3);`:1070-1071`=reports/ipados27-appsync.md 引用,result="combined image was accepted by fastboot boot on b714917c; desktop and application behavior remain to be observed";
     - `reports/ipados27-appsync.md`(70 行全读)=实机验收:unsigned arm64 app 经 USB 安装、启动、跨重启留存(sheng 设备)。
   - 官方 UniXNU(实现本体):
     - `scripts/appsync/`:README.md(125 行)、AppSync.m、Signing.m、BacklightMode.m(31 行)、build.py(145 行)、signer.py、signer-sources.json、appinst.m、macho.py、ldid-library.cpp、ldid-adhoc.patch、test.py、LICENSE——关键件已全读;
     - `scripts/build-boot-runtime.py:436-452`=appsync 段(os 挂载→`appsync/build.py --os-root`→addon-add-dir 追加 4 目录(lib/libexec/helper/coreservices)→appsync-service.json);`:438-439` 只认 `('ios27','24A446')`;`:525-548`=owner trust 追加(appsync_report['system_executables']→appsync-owner-trust.bundle);`:449-460`=trust 为 None 时的 IPSW trust 组装路径;
     - `scripts/unixnu/runtime.py:99-110`=spec 校验(同只认 24A446)+os_image/trust_modules 请求组装;`scripts/test-runtime-inputs.py:89-113`=官方自测两条:appsync 重建显式输入**不改 seed** ✓ / 非 24A446 拒绝 ✓。
   - 官方设备文档先例:`docs/devices/aston.md:314`=修法原文("ADT vmm-present … CoreBrightness then selects VMBLControl, which drops every brightness request. AMFI and many frameworks need the answer, so UniXNU scripts/appsync/BacklightMode.m changes only backboardd: initBLControl: gets mode 1 instead of 3.");`:331`=同症实锤(iOS boot 9:86 次 setDisplayBrightness 全部 "No container for handle Display(id:1)",内核 L st=4 req=0——与 r74/r75 症状同型)。
   - **注入机制补记(本扫描读透)**:BacklightMode.m 不是"单独塞进 backboardd",而是完整注入链——`build.py:24-30` 把 4 个 24A446 原件(installd / backboardd / MobileInstallationHelperService / SpringBoard)按 sha256 钉死(PINS);`:70-71` 用 macho.py `add_dylib` 给原件加 `/usr/lib/UniXNUAppSync.dylib` 加载命令;补丁副本与 dylib 本体进 addon(appsync/libexec、appsync/helper、appsync/coreservices;dylib=AppSync.m+Signing.m+**BacklightMode.m** 一起编,`build.py:79`);内核 runtime 补丁把 addon 目录联合挂回原系统路径(/usr/lib、/usr/libexec、helper .xpc、CoreServices)→ backboardd 启动即加载 dylib → 构造器只对 backboardd 生效(`getprogname()` 门)。运行时哈希进 owner trust bundle(build-boot-runtime.py:529-531);用户 app 哈希不进构建输入,由内核补丁按需注册(ubc_cs_blob_add)。
2. **②同设备 houji = 已扫,无可借**:lonzzi-houji / Kizashi3-houji / hannana-houji / sakayorilroha-houji 全库 'appsync' 0 命中;nian0114-houji(纲领四②是否列入待用户定,顺带扫)0 命中。方式=pwsh 递归 Select-String(2026-11-28)。
3. **③同代机型 = 已扫,无可借**:shennong 三家(baihem/sleep1234/YX2748198202-lab)、fuxi 两仓、cupid 两仓、peridot-hoshikv、marble-iltanaka、mu-silicium 全 0 命中;唯一命中=`YX2748198202-lab-UniXNU-shennong\docs\handoff\UPSTREAM-DELTA.md:20`(上游 delta 文件计数表,记录性质非集成先例)。community 11 个命中=svc_test.c 的 CFPreferencesAppSynchronize 误命中 ×7 + aston 镜像 4 文件(归④)。
4. **④src/aston = 第二设备级旁证(补丁随身,未提交启用态)**
   - `patches/xnu-12377/series:17-18` 带同两补丁(:10-11 wifi/bluetooth 在前,与 sheng 同序);`board.toml` **无 appsync 键、kernel_args 无 appsync=1**(grep 实锤;:149 trust_components 同三件、:153 gpu_addon=true)→ aston=补丁随身+诊断记录+一次构建实验,**启用态配置以 sheng 为准**;
   - `porting.toml:201`=VMBLControl 诊断(A 轮已引);`:408`="payload.runtime.appsync and appsync=1 in board.toml do not change the USB stacks"(曾启用过的旁证);`:606-607`=appsync 实验记录(b714917c)。
   - **补丁版本差异(Get-FileHash 实锤)**:sheng-appsync-runtime.patch 三仓(sheng/aston/community 镜像)逐字节相同=29885dc9…7550;**sheng-appsync-user-trust.patch aston≠sheng**(sheng=4b2a0c88…b40f,257 行;aston=24d4c75f…f240,179 行)——aston 版缺 `appsync_is_user_code()` 与 kern_exec.c 平台位清除 hunk(只动 4 文件,无 kern_exec.c)=旧快照。**借用以 sheng 版为准**(官方 README:90-92 描述的 exec 平台位清除只有 sheng 版有)。

## 二、B 轮借用清单(照纲领 §4.9,逐字节+哈希入库)

| # | 借什么 | 从哪借 | 怎么借 |
|---|---|---|---|
| 1 | sheng-appsync-user-trust.patch | sheng `patches/xnu-12377/`,sha256=4b2a0c882d5216d22027f9470b5bd9ebdd770ed739f886e54600c9b89323b40f | **逐字节**拷进本仓 device patches/xnu-12377/,series 尾部追加 |
| 2 | sheng-appsync-runtime.patch 的挂载块(新增 36 行) | sheng 同目录,sha256=29885dc99aa4b84af4f169fae9e798dcbc456268599a9a3eedc98d16b6217550 | **适配版 houji-appsync-runtime.patch**:新增行逐字节同 sheng,锚点改为"尾部三行调用"(native_cryptex_os_view/gestalt_cache_drop/ls_store_drop;本仓 zzyd 补丁 :23 context 实锤存在);坐标适配先例=本仓 `patches/xnu/series:3-8`(r74 liimagination 补丁只适配 @@ 坐标) |
| 3 | scripts/appsync 全套 | fork 已有,与官方逐字节同(实测:Get-FileHash 前 16 位 build.py=C7373C794D15772C、BacklightMode.m=A0CA3D1CB42AD549、AppSync.m=44A23A95D6C9DBB9、Signing.m=4A6EA284F6AFA859) | 无需动;开工时全量比对记全值入库 |
| 4 | build-boot-runtime.py 的 appsync 段 | fork :359-375 与官方 :436-452 同文(行号漂移因 fork 少 macOS 段;逐行比对一致) | appsync 段前加 3 行空基底兜底(见 P18 甲) |
| 5 | board.toml 两处 | sheng :61/:82 | kernel_args 末位加 `"appsync=1"`;`[payload.runtime]` 加 `appsync = true` |

## 三、前提核对表(满足/不满足+行号)

| 前提 | 结论 | 依据行号 |
|---|---|---|
| target/build=ios27/24A446 | 满足 | 本仓 board.toml:172-173;fork runtime.py:82-84、build-boot-runtime.py:361-362 只认这对值 |
| os_image 自动供给 | 满足 | fork runtime.py:90-94(appsync→request['os_image']=Ipsw.disk_image('OS'));IPSW 在 inputs(iPhone18,2_27.0.1_24A446_Restore.ipsw,12120.2MiB) |
| addon 基底存在 | **不满足→P18** | fork build-boot-runtime.py:237/242(我们 board.toml:181 gpu_addon=false→addon=None)→:372 addon-add-dir.py 无输入必崩(addon-add-dir.py:17 要求 NGPUADD2 头) |
| 内核挂载基建 | 满足 | native_gpu_addon_mount/prepare=zzzz 补丁(本仓 zzzz:90/240,CI 多轮已用);尾部三行锚点在(zzyd :23 context) |
| runtime patch 原样可套 | **不满足→P19** | sheng 原 hunk 上文=bluetooth 块(sheng-appsync-runtime.patch:5-8),本仓未带 wifi/bluetooth 两补丁 |
| user-trust patch 上下文 | 满足(CI 即判) | 本仓 device 补丁 0 处触及 ubc_subr.c/kern_trustcache.c/badtoolate/ip_free_map/trust_cache_rt_storage(grep 实锤);soc/os 级 series 不在本地,CI patch 阶段即判,失败按 r74 坐标适配先例处理 |
| CONFIG_SPTM / PMAP_CS_PPL_MONITOR 未设 | 满足 | 本仓 configs/houji_ios27_defconfig(27 行)无;fork *.config 0 命中(inc :57 的 KERN_NOT_SUPPORTED 分支不触发;sheng/aston 同 SoC 同配置已实机跑通该 inc) |
| owner trust 基建 | 满足 | defconfig:13 CONFIG_XNU_OWNER_ROOT_TRUST=y、:15 CONFIG_XNU_OWNER_CACHE_MOUNT=y;board.toml:179 trust_components 三件;build-boot-runtime.py:525-548 追加逻辑在 |
| seed 保持钉住 | 满足 | 官方自测 test-runtime-inputs.py:89-105(appsync 重建不改 seed);CI pre_build 继续钉 803d9f6c(归档 :79) |
| build.py 4 原件 PINS 匹配我们 IPSW | **待 CI 首跑即判→P20** | build.py:24-30 钉 24A446 installd/backboardd/helper/SpringBoard 的 sha256;:70-71/:101-110 preflight 先于产物;本地无法廉价预验(OS dmg 需 macOS hdiutil) |
| payload 容量 | **待验证→P21** | payload 卷 96MiB(board.toml:83)+initrd 窗口 0xA8000000-0xAE000000(board.toml:220-222);addon 含 SpringBoard.app 全副本+2 dylib+4 补丁原件,实际体积以 CI report.json 为准;超限=扩 size_mib+initrd 窗口(board 一类改动) |
| BacklightMode ABI | 满足(自检不崩) | fork BacklightMode.m 31 行全读=官方逐字节;若上机见 "initBLControl: ABI mismatch; inactive"=注入通但签名不符(判读预案既有,§六) |
| vmm-present 保留 | 满足 | board.toml:72 不动;修法只改 backboardd 行为(aston.md:314 原文) |
| CI 派发链 | 满足(照抄) | 归档长版 :79 标准 pre_build 全串(fixture 安装+DarwinBoot submodule+ipsw+钉 seed);workflow=macos.yml -R hannana-26/UniXNU --ref main -f device_ref=storage |

## 四、立案项(纲领四:不满足当场立案)

- **P18(fork 打包脚本,低风险)**:gpu_addon=false + appsync=true 时 addon=None,appsync 追加段(build-boot-runtime.py:372)崩。两案:甲(推荐)=fork scripts/build-boot-runtime.py 在 appsync 段前加 3 行——`if request.get('appsync') and addon is None:` 时造 24 字节空 NGPUADD2 基底(写法先例=同文件 build_boot_shims :105-107 官方空基底);乙=gpu_addon 翻 true(否决:build_plugin :74-92 拉 mesa/turnip/Adreno 插件全套,GPU 轮未开,体积时间暴涨)。fork 此文件与官方本已有差异(hash 实测不同),此改动=fork 侧已记录适配,commit 注明先例行号。
- **P19(kernel 补丁形态)**:sheng-appsync-runtime.patch 原样套不上(上文缺 wifi/bluetooth 块)。两案:甲=连搬 sheng-wifi-launchd-cache.patch + sheng-bluetooth-runtime.patch(代价:每次开机 wifi mount 必失败打印两条良性日志 + bluetooth=1 门控死代码);乙(推荐)=出 houji-appsync-runtime.patch 适配版(新增行逐字节同 sheng、锚点=尾部三行,r74 先例同型,改动面最小)。
- **P20(验证点,非缺陷)**:build.py 的 4 个 PINS sha256 是否匹配我们 iPhone18,2 的 24A446 IPSW 原件。CI 首跑 preflight 即判(:70-71 先于产物);若不符=立案上报,不猜、不手改 PINS。
- **P21(验证点)**:payload 96MiB 容量 vs addon 实际体积。CI report.json 出来后核对;超限才扩 size_mib+initrd 窗口。

## 五、B 轮实施清单(开工照此执行;单类改动=背光 B 打包面;**本扫描未执行任何一步**)

1. device repo(UniXNU-houji-ios27,branch storage):
   - `patches/xnu-12377/` 拷入 sheng-appsync-user-trust.patch(逐字节)+ 新写 houji-appsync-runtime.patch(P19-乙 适配版);
   - `patches/xnu-12377/series` 尾部(houji-backlight-hooks.patch 之后)追加两行,顺序照 sheng:houji-appsync-runtime.patch → sheng-appsync-user-trust.patch;
   - board.toml:kernel_args 末位(houji_bl=1 后)加 `"appsync=1"`;`[payload.runtime]` 加 `appsync = true`;
   - 借入补丁 sha256 写进 porting.toml display findings(§4.9);
   - tomllib 校验 board.toml 过了才 commit(§4.8);mk 断言链照旧(AMFI sites fixture 断言等)。
2. fork(hannana-26/UniXNU,main):scripts/build-boot-runtime.py 加 P18-甲 3 行,commit 注明先例(build_boot_shims :105-107)。
3. CI 派发=归档 `:79` 全串原样(workflow macos.yml,-f device_repository=hannana-26/UniXNU-houji-ios27 -f device_ref=storage -f defconfig=houji_ios27_defconfig -f "steps=kernel payload",pre_build 含 fixture 安装——漏装=AMFI 站点丢,断言链会拦);Build kernel 第一遍链接失败=预期、Summary 134=假失败;P11 横切=新内核先探路烧,连 2 坏重跑 CI。
4. 上机判据链(**先验算后测,纲领 §4.13③;滑条等 B 轮就绪一次做完**):
   - 内核侧读回:`[appsync-runtime] library=%d/%d installd=%d/%d helper=%d/%d SpringBoard=%d/%d`(挂载结果)、`[appsync-trust]` 系列;
   - 用户态:`[UniXNU backlight] physical brightness control selected`=注入成功;`initBLControl: ABI mismatch; inactive`=立案查签名;
   - 心跳行 requests/Requested 从 0 变化;P17 通过标准=requested>0 且 applied 跟随(电位≠307 初值);
   - 用户操作一次做完:Settings→Display & Brightness 打开 + 拖滑条 + 控制中心拖;
   - 无回归:桌面/触摸/按键/存储照旧。
5. 回退线:出货 r75(ea302bc2…)不动;B 轮新 rN(r76 起);失败先存镜像+日志(红线 4);回退=r74(3f6a48c7…)/h2(9481af92…)。

## 六、参考路径索引

- sheng:`patches/xnu-12377/{series,sheng-appsync-runtime.patch,sheng-appsync-user-trust.patch,sheng-wifi-launchd-cache.patch,sheng-bluetooth-runtime.patch}`、`board.toml:55-114`、`reports/ipados27-appsync.md`、`porting.toml:478/1070-1071/1406-1407/1446`
- 官方:`scripts/appsync/*`(README/build.py/BacklightMode.m 等)、`scripts/build-boot-runtime.py:436-560`、`scripts/unixnu/runtime.py:82-130`、`scripts/test-runtime-inputs.py:89-113`、`docs/devices/aston.md:313-331`
- fork:`scripts/build-boot-runtime.py:95-110/231-242/359-375/425-459`、`scripts/addon-add-dir.py`、`scripts/appsync/*`、`scripts/unixnu/runtime.py:34-130`、`profiles/os/`(ios-27.0.1-24A446 与 ipados-27.0.1-24A446 都在)
- aston:`patches/xnu-12377/{series,sheng-appsync-*.patch}`、`board.toml:149/153`、`porting.toml:201/408/606-607`
- 本仓:`board.toml:100-181/207-222`、`configs/houji_ios27_defconfig`、`patches/xnu/series`、`patches/xnu-12377/series`、`patches/xnu-12377/{zzzz…:83-92,zzyd…:23}`、`porting.toml:146-147`
- 归档:`交接文档_最新_2026-11-27_1945版.md:75-79`(pre_build 全串原文)
