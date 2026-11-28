# r76 背光 B 上机判据链(2026-11-28,测前验算;纲领 §4.13③)

状态:镜像未出(CI 46107323245 在跑)。本文件写判据,**不是**结论。镜像出来、mk76 断言全绿后才给用户"可以测"。

## 一、一次开机要回答的问题(按先后)

| # | 问题 | 通过的样子 | 读数来源 |
|---|---|---|---|
| Q0 | 系统还能到桌面吗(无回归) | 好模式→Apple 标志→桌面;触摸/音量键照旧 | 用户目击+录像 |
| Q1 | AppSync 运行时挂上了吗 | 读回 `HOUJI APPSYNC: hb ran=1 lib=0 inst=0 help=0 sb=0` | logfs 记录(printf 流) |
| Q2 | backboardd 里的注入生效了吗 | 拖滑条后内核 `requests` 从 0 变化 | logfs `HOUJI BACKLIGHT: hb` 行 |
| Q3 | 亮度真被改了吗 | `applied` 偏离开机初值 307,且面板肉眼变亮/变暗 | 同上+用户目击 |

Q1 为 Q2 的前提:`ran=1` 且四项全 0,Q2 才有意义。Q1 不通=不要让用户白拖滑条(纲领禁令),直接读回找原因。

## 二、用户这一次要做的事(一次做完,别空拖)

1. 等我说"开始"+确认录像→`fastboot boot r76`(红线 2)。
2. 进桌面后:**设置→显示与亮度**(页面要能打开=A 轮已验证的回归点);在那页**把滑条先拖到最左,再拖到最右,再拖回中间**;再打开控制中心**同样拖一遍**。每次拖完停 2 秒。全程录像,镜头拍着屏。
3. 停 20 秒后**强制关机**(电源键+音量下长按)。——关机时刻的最后一条心跳就是读回对象(与 r75 同法,r75 读回=关机时刻最后心跳 requests=0 applied=307)。
4. 我烧读取器 `boot-houji-r52-logfs-reader.img`(只读,不写 logfs)→读回→判读。

## 三、验算:读数能不能读出来(用已有数据)

1. **读回通道**:r75 读回已证实路径可用——`r75_rb_dense.txt` 里读到了 `requests=0 rejected=0` 的心跳字段(OCR 有 `0`↔`θ`、`i`↔`1` 误读,需二值化+对原图复核,与 r74/r75 同)。新增的 `HOUJI APPSYNC: hb` 行是**同一个 15 秒定时器(hbTick)里紧接着 HOUJI BACKLIGHT 行的 printf**——与已读通的行同源同时序,不是新通道。
2. **记录容量**:logfs 记录只留 msgbuf 最新 60KiB(BootLog.c:2031 的 HOUJI_LOG_PAYLOAD_CAP)。心跳每 15s 一对(BACKLIGHT 约 160B+APPSYNC 约 50B),20 秒停留+之前的拖动期,**关机前最后一对一定在尾部**(r75 同法已验证)。内核其余输出约 300B/s,60KiB≈200s——所以拖滑条后**必须在 ~100s 内关机**,否则被冲掉(用户操作 3 已含"停 20 秒后强关")。
3. **字段语义**(代码定音,HoujiBacklight.cpp):`requests`=收到的亮度属性请求数;`requested`=最后请求值;`applied`=最后写面板的电位(开机初值 307=0x133);`rejected`=被 values() 拒绝的数(A 轮后接收 nits)。
4. **ran 的取值**:-1=houji_appsync_prepare 从未跑(门没开/还没到点);1=跑完;lib/inst/help/sb=0 是 vnode_lookup 成功,否则 errno(2=ENOENT=没挂上)。**注意 -1 的歧义**:若 appsync=1 参数没进内核命令行也是 -1,区分方法=读回 payload.json/unixnu.toml(mk76.sh 已断言 args 含 appsync=1)。
5. **读不到 APPSYNC 行的情形**:①心跳线程没起(hbTick 在 houji_backlight_start 里;r75 已证 stage=3 registered=1)②行被 msgbuf 环冲掉(见 2)③weak_import 符号未解析→`&houji_appsync_ran` 为 NULL→行不打。第③条 mk76 断言 'HOUJI APPSYNC: hb ran=' 串在内核里;符号链接结果无法本地验=**唯一未验算项**(归入风险申报)。

## 四、读数→判断(决策表)

| 读回 | 判读 | 下一步 |
|---|---|---|
| APPSYNC 全 0 + requests>0 + applied≠307 | **背光 B 成功**(P17 通过标准) | 写回三件套,出货 r76,收线 |
| APPSYNC 全 0 + requests>0 但 applied=307 | 请求到了但服务侧没写面板 | 回查 A 轮属性/levelForNits |
| APPSYNC 全 0 + requests=0 | 挂载成功但注入没触发:BacklightMode ABI 不符("ABI mismatch; inactive" 在用户态 NSLog,内核看不到)、或 backboardd 没加载 dylib | 立案;要用户态日志(sysdiagnose 不可用)→用 UniXNU 的 AppSync.m 自带 NSLog 也读不到;候选方案=查 backboardd 原件 LC_LOAD_DYLIB 是否加上(可本地验 build 产物) |
| ran=-1 | houji_appsync_prepare 没跑 | 查 args、查调用点是否在 native_owner_data_prepare 成功路径上 |
| ran=1 且某项 errno | 对应挂载失败(2=没挂、其它=挂载错误) | 按项定位 |
| 无新提交/坏模式 | P11 横切:连 2 坏重跑 CI,不改镜像 | 同旧规 |
| 桌面起不来/SpringBoard 崩 | AppSync 联合挂载覆盖系统路径出错(风险 a) | 回退 r75;读回找 [appsync-runtime] 行(内核日志,早期,可能被冲掉) |

## 五、先验算后测的缺口(如实)

1. **weak_import 符号解析**无法在本机完整验(需完整链接)。写法有先例(查证 2026-11-28):`package/BootLog.c:87-123` 对 `kern_exec.c` 的 `native_gpu_addon_*` 等变量全用 `extern volatile int x __attribute__((weak_import))` + `if(&x)`,r75 已上机运行;C++ late 归档里 `ButtonProbe.cpp:20`、`LegacyDisplayProbe.cpp:32` 对核心函数也用 weak_import。本次新点=C++ 里对**变量**用 `extern "C" volatile int` weak_import(先例是 C 里的变量、C++ 里的函数,组合无直接先例),编译警告/-Werror 与链接解析由 CI 的 Build kernel 判。若 CI 编译失败=按 faillog 修;若链接过但 `HOUJI APPSYNC` 行缺失而 BACKLIGHT 行在,回头查此项,不要误判成"没挂载"。
2. 注入的 ABI 自检在用户态,内核读回看不到;只能由 requests 是否变化间接判。
3. 本轮没有 `kernel.macho` 链接前的符号表可检;mk76 会 grep 内核二进制中的 `houji_appsync_ran`(链接后的符号名若被 strip 则 grep 不到,以字符串 'HOUJI APPSYNC: hb ran=' 为准)。
