# VeroScriptNetwork

前端安装与后端选择的公共规则统一见 [Network](../index.md#backend-selection)。

基础包 `vs-network` 提供 context、operation、URL、DNS、TCP/UDP、字节管道、文件执行器和共享 TLS 配置。Http 与 WebSocket 是独立模块。

## URL

`FUrl::TryResolve` 将相对引用解析为绝对网络 URL，依照 [RFC 3986 第 5 节](https://www.rfc-editor.org/rfc/rfc3986.html#section-5)。
仅规范化路径中的字面 `.` / `..`，保留重复斜杠、百分号编码及 query/fragment 中的点段。
空查询 `?` 与没有查询分别保留；空引用继承原查询并移除旧 fragment。
此类型表示带 authority 的网络 URL，authority 后空路径统一表示为 `/`；不接受 `g:h` 一类不透明 URI。
解析失败保留原输出。百分号编解码对字节执行一次，form 模式显式启用 `+` 与空格转换。

## 操作与派发

原生后台模式使用每个 context 一个 I/O 线程，未按请求创建模块工作线程；系统解析器可能有自己的线程。
HostDriven 模式由创建 context 的线程限量调用 `PollIo`。`PollIo` 不运行完成回调且禁止递归调用，`Dispatch` 在创建线程运行回调。
完成回调至多注册一次、派发一次；回调新产生的事件留待下一批，禁止递归派发。
操作终态可先于完成回调入队被观察到；宿主需持续正常派发，单次 Dispatch 不保证收齐刚完成操作的全部回调。
已完成操作的回调注册在队列扩容失败时保留未注册状态，调用方可重试。
`Dispatch` 若无法分配本批缓冲，返回 0 / Allocation，原队列与额度保持不变；
回调错误即使无法分配诊断文本也保留错误码，并正常解除派发状态。

`Wait` 只是观察；`WaitTimedOut` 不取消请求。HostDriven 的未完成操作返回 `RequiresPoll`。
操作的总期限从提交时计算，包含排队时间。取消与完成只发布一个终态。
`Result<T>` 仅在类型匹配时返回只读结果。`Shutdown` 取消操作并关闭所登记资源。
operation 的定时器及取出的结果保留底层 I/O/代码 Owner，允许晚于公开 context 销毁；关闭后不会恢复 I/O。
RetainCodeOwner 按 shared_ptr 的所有权去重，接受拥有控制块的别名句柄（包括空指针别名）；
仅有裸地址而没有控制块的句柄不构成有效 Owner。地址相同但所有权不同的句柄分别保留。
MaxRetainedBytes 默认 256 MiB，限制由 context 管理的应用字节缓冲和排队载荷。
管道按整环容量计费；TCP/UDP 收发、HTTP 双端正文及 WebSocket 消息与控制队列纳入核算。
终态结果或请求快照仍被保留时，不提前释放其额度；Shutdown 本身不会丢弃外部仍持有的数据。
MemoryUsage 返回当前保留量、峰值和上限，脚本对应 retained_bytes、peak_retained_bytes、memory_limit。
这是模块管理额度，解析器头缓冲和控制队列存在保守预留；不包含后端/操作系统内部存储、
分配器元数据、调用方自行复制的数据或容器扩容时的临时双份分配，也不等同于进程物理内存硬上限。
I/O 执行器中的分配异常或未处理异常使 context 关闭、未完成操作进入失败并唤醒等待者，
分别报告 Allocation/Internal。发生此类失败后仍须正常 Shutdown/销毁 context，以回收资源。
关闭先发布未完成操作的取消终态，再逐项回收定时器、取消任务、资源和待派发回调；
这些回收步骤不构造临时操作列表，也不依赖新的诊断字符串分配。
若原生 provider 的资源 Owner 在 I/O 处理内释放 context，回收延迟到当前执行器调用退出后进行；
执行过程保留底层状态。后台线程在回收完成后结束，HostDriven 模式在本次 PollIo 返回前完成回收。
内存不足时错误文本可以为空，调用方应使用错误码判断结果。
系统 DNS 的物理回收可能晚于取消的逻辑终态；关闭耗时取决于原生解析器。
通常的 PollIo 只处理已经就绪的 I/O；若其中触发延迟关闭，本次返回前的回收可能等待底层原生工作退出。

TCP 和 UDP 取消进行中的系统收发会关闭该 socket，其他未完成操作也会结束。
TCP 同时接受一个 read 和一个有界串行 write 队列。`MaxQueuedWrites` 限制每个 socket 的未完成写入数量，
`MaxBufferBytes` 同时限制该队列的总字节；排队项的期限从提交时计算。取消尚未开始的写入只移除该项；
取消已开始的写入会关闭 socket。`FTcpListener` 提供独立监听、异步 accept 与幂等关闭。
UDP 保留零长度报文，截断结果同时提供实际返回长度、原报文长度和标志，不把空报文当 EOF。
`SetBroadcast` 切换广播发送；`JoinMulticast/LeaveMulticast` 显式选择 IPv4 接口地址或 IPv6 接口索引。
实际广播/组播交付取决于对应网络接口与平台。

## 原生 SDK 与部署

安装后按需分别使用 `find_package(VeroScriptNetwork 0.1.1 EXACT CONFIG REQUIRED)`、
`find_package(VeroScriptHttp 0.1.1 EXACT CONFIG REQUIRED)`、`find_package(VeroScriptWebSocket 0.1.1 EXACT CONFIG REQUIRED)`，
链接相应的 `VeroScript::Network`、`VeroScript::Http`、`VeroScript::WebSocket`。
Boost SDK 的导出 target 会自动链接这一依赖；部署时须保留安装在 `lib/veroscript/modules` 的运行时库。
三个 provider 与运行时应使用相同源码和兼容的编译配置；更新时同步部署配套产物。
Windows 的独立宿主应将运行时 DLL 放在可执行文件旁；模块加载器也支持从模块所在目录查找它。
Linux/macOS 的已安装动态模块通过相对 loader 路径查找同目录运行时。
iOS 仅支持静态注册，运行时随同一宿主映像静态链接；这些平台的实际设备验证仍未完成。

需要静态原生注册 SDK 时，安装数据 SDK 和匹配 Core SDK 后，分别请求 `find_package(VeroScriptNetwork 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Bindings)`、
`find_package(VeroScriptHttp 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Bindings)` 和
`find_package(VeroScriptWebSocket 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Bindings)`，
链接对应的 `VeroScript::NetworkBindings/HttpBindings/WebSocketBindings`。动态扩展安装在 `lib/veroscript/modules`。
安装 component 分别为 `VeroScriptNetwork`、`VeroScriptHttp`、`VeroScriptWebSocket`，绑定层分别追加 `Bindings`。
也可使用 `find_package(VeroScriptNetwork 0.1.1 EXACT REQUIRED)` 与对应 Http/WebSocket package。
各 package 的脚本组件名为 `Bindings`。基础包 `VeroScriptNetwork` 只查找自身依赖，不要求 curl，
也不会隐式加载 Http/WebSocket；协议包各自自动查找基础包。C++ 命名空间为 `VeroScript::Network`，
脚本命名空间为 `Network`，原生入口 ID 为 `network`。

## 脚本入口

脚本命名空间为 `Network`，原生入口 ID 为 `network`，CMake 包为 `VeroScriptNetwork`，target 为 `VeroScript::Network`。

| 入口 | 主要方法 |
| --- | --- |
| `Network::parse_url(text)` / `Network::resolve_url(base, reference)` | URL 值；`scheme/host/port/path/query/fragment/has_query/has_fragment/explicit_port/ipv6/authority/target/to_string/without_fragment` |
| `Network::percent_encode(text, form)` / `Network::percent_decode(text, form)` | 组件字节编解码；字符串内 NUL 保留 |
| `Network::create_context(host_driven)` | `poll/shutdown/closed` |
| `Network::connect(context, host, port, timeout)` | 返回 operation；完成后 `socket()` 取 TCP 句柄 |
| `Network::listen(context, address, port)` | `port/accept(timeout)/close` |
| `Network::bind_udp(context, address, port)` | `port/send_to(bytes, address, port, timeout)/receive(limit, timeout)/close` |

每个模块的 operation 提供 `done/state/cancel/error/error_code/wait`，以及受检的结果访问。
`wait(milliseconds)` 的返回值为 0 Completed、1 WaitTimedOut、2 RequiresPoll；观察超时不会取消操作。
操作期限参数为毫秒，-1 表示无限，0 表示立即到期；默认选项中的 30 秒不替代显式传入的参数。
`count()` 返回受检的 `int64`，只适用于返回数量的操作。

- [字节管道](streaming.md)
- [文件执行器](files.md)
- [共享 TLS 配置](tls.md)
- [完成回调](completion.md)
- [诊断与本地化](diagnostics.md)
- [HTTP](../../../../../../VeroScriptHttp/Resources/Docs/User/zh-CN/Reference/README.md)
- [WebSocket](../../../../../../VeroScriptWebSocket/Resources/Docs/User/zh-CN/Reference/README.md)

浏览器/Node provider、Core buffer 适配及其他宿主和移动平台的完整资格仍待后续实现和验证。

## 句柄与回调

所有句柄保留 context；context 保留生产后端的代码 Owner，直至 I/O、结果和资源清理完成。
脚本本轮调用结束后不保留借用的 VM 服务；显式 `wait` 在当前调用内检查执行工作预算。
三个协议的 operation 均支持 `on_complete`，通过 `context.dispatch(max_callbacks)` 显式执行；
函数捕获、程序代次与关闭规则见 [完成回调](completion.md)。接口沿用现有 operation，不新增语言 await。

## 扩展包与数据

离线安装需显式声明 Network 本地归档来源，详见
[扩展包管理](../../../../../../../../VeroScriptPackageManager/Resources/Docs/User/zh-CN/index.md)。

三个模块分别提供 C++ SDK 归档和 `.vspkg`；原生入口必须与 Core 构建、平台、架构及工具链匹配。
脚本数据使用保留 NUL 的 string，不自动转换字符编码。TCP/UDP 的 `data()` 读取完成结果；
任务未完成、失败或结果类型不匹配时，结果访问明确失败。各协议继续使用专属 operation 结果句柄。

公开头不包含 Boost 或 curl 类型，Json 与 Network 数据层独立；使用已安装 SDK 不需要后端私有头。

## 默认值

| API | 值 |
| --- | --- |
| `FContextOptions::MaxOperations` | `1024` |
| `MaxCallbacks` | `1024` |
| `MaxConnections` | `256` |
| `MaxQueuedWrites` | `64` |
| `MaxBufferBytes` | `16 MiB` |
| `MaxRetainedBytes` | `256 MiB` |

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](README.md) · [zh-TW](../../zh-TW/Reference/README.md)
