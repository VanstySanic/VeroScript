# 标准库基础

<a id="foundation-imports"></a>

字符串、I/O、路径、数值聚合与并发需要导入 `core.vs` 或相应模块；`std::io::read_*` 需要 `io.vs`。环境、时间和控制台入口仍由运行时直接提供。完整命名空间不能代替模块导入。

解析支持定宽整数、float、double、bool 及其别名。去除外围 ASCII 空白后必须完整匹配；至多一个符号，无符号拒绝负号（含 -0）。显式 base 仅用于整数，范围 2..36，不剥离进制前缀。浮点只接受有限十进制数，拒绝 NaN、Infinity、十六进制和范围错误；直接按目标精度舍入。bool 仅接受 true/false。普通格式或范围失败使 try_parse 返回 false，输出不变；非法参数、限额、取消等仍报错。

字符串位置继续采用 UTF-8 字节偏移。replace 从左到右替换不重叠匹配，空模式返回原文；split 保留空分片。修剪和大小写转换使用 ASCII 规则。UTF-8 严格拒绝坏编码，encode/decode 保留 NUL、BOM、换行，不替换错误字节。下面 region 表示 buffer、view、readonly_view 的真实重载，不是新类型。

<a id="io-contract"></a>

只有正常 EOF 返回空 optional；空行是有值的空字符串，未以换行结尾的末行也有值。移除 LF 及末尾一个 CR；MaxTextBytes 按移除后的行内容计量，行内 CR 仍计入；token 保留结尾分隔符。旧 read_line 在 EOF 返回空串，严格 token 读取在 EOF 报错。read_bytes 的显式上限超出时报错，不截断，0 仅允许空文件。字节 I/O 使用受检 memory 借用。写入失败时文件可能已被截断或部分修改，flush 不承诺持久化。

路径查询只做词法处理，不访问磁盘。native 使用宿主语法，posix/windows 为明确的跨平台语法。Windows 的 C:a、/a 不是绝对路径，完整 UNC 是绝对路径，输出分隔符为 /。拒绝 NUL、不完整 UNC 和设备前缀；归一化不能证明沙箱包含关系。sum_as 逐项转成 R 后受检相加，空数组报错；accumulate_as 空数组返回初值。R 必须显式指定为非 bool 基本数值类型。clamp 推导相同的 T，拒绝隐式混合类型、反向或 NaN 边界，保留 NaN 输入。

同步工厂创建私有通道角色；通用 send/recv/容量修改拒绝这些角色，关闭与只读查询仍可用。mutex 非递归并由逻辑执行器拥有；其他执行器解锁报错，自身 try/限时加锁返回 false，拥有者退出时关闭尚持有的锁。semaphore 使用计数存储并拒绝超额释放。synchronized 通过已准备的快照及提交执行 load/store/exchange/fetch_add，算术失败不会掏空单元，不承诺硬件无锁。

任务别名共享 Core 状态。await 等待 worker 完成后消费唯一结果；worker 错误或结果缺失／多余会报错，不继续无限接收。await_for 只有超时才返回空 optional，此时保持活动；消费前准备或限额失败保留结果。组先校验空、无效和重复能力；部分失败后已消费任务不回滚，size 为剩余范围，终结失败项从范围移除。when_all 保留原数组但消费任务状态。cancel_all 先请求全部取消，worker 实际退出前仍由 VM 持有；取消不能强制打断阻塞宿主调用。

宿主按单次操作配置 MaxTextBytes、MaxReadBytes、MaxSplitParts（uint64，0 不额外限制），平台及 VM 上限始终有效，worker 继承配置。原生分块工作复用当前预算与取消。reverse/rotate/shuffle 中断后保持元素多重集；有观察者时私有准备后一次发布，不调用 clone。shuffle 拒绝数组与随机引擎为同一身份，已消费随机数不回滚。独立 AOT 在生成前明确拒绝需要这些 Core 服务的操作，未使用的标准库导入仍允许。

参阅[字符串模块](string.md#integer-formatting)。

setenv(name, "") 设置存在的空值；删除使用 unsetenv，删除不存在项也成功。try_getenv 区分缺失与空值。名称非空且不能含 = 或 NUL，值不能含 NUL。Windows 环境名称沿系统规则不区分大小写，POSIX 区分大小写。

## API

```text
// string.vs
T std::strings::parse<T>(string text);
T std::strings::parse<T>(string text, int base);
bool std::strings::try_parse<T>(string text, ref T output);
bool std::strings::try_parse<T>(string text, ref T output, int base);
std::memory::buffer std::strings::encode_utf8(string text);
string std::strings::decode_utf8(region bytes);
bool std::strings::is_valid_utf8(string text);
bool std::strings::is_valid_utf8(region bytes);
// io.vs
optional<string> std::io::try_read_line();
optional<string> std::io::try_read_token();
std::memory::buffer std::io::read_bytes(string path);
std::memory::buffer std::io::read_bytes(string path, int64 max_bytes);
void std::io::write_bytes(string path, region source);
void std::io::append_bytes(string path, region source);
// path.vs: each query has both one- and two-argument overloads
bool std::filesystem::is_absolute(string path);
bool std::filesystem::is_absolute(string path, std::filesystem::path_style style);
string std::filesystem::root_path(string path);
string std::filesystem::root_path(string path, std::filesystem::path_style style);
string std::filesystem::lexically_normal(string path);
string std::filesystem::lexically_normal(string path, std::filesystem::path_style style);
// path_style: native, posix, windows
// numeric.vs / math.vs: R explicit, T inferred
R std::numeric::accumulate_as<R, T>(array<T> values, R initial);
R std::numeric::sum_as<R, T>(array<T> values);
T std::clamp<T>(T value, T low, T high);
// runtime-provided
optional<string> std::environment::try_getenv(string name);
void std::environment::unsetenv(string name);
```

## SDK

```text
C++: FEngineConfig.StandardLibraryLimits;
     VeroScriptSession::SetStandardLibraryLimits(FStandardLibraryLimits);
     VeroScriptVm::SetStandardLibraryLimits(FStandardLibraryLimits);
C:   vs_interop_session_set_stdlib_limits(session, &limits);
     vs_interop_stdlib_limits { struct_size, reserved=0, max_text_bytes,
                                max_read_bytes, max_split_parts };
C#:  VeroScriptSession.SetStandardLibraryLimits(VeroScriptStandardLibraryLimits);
Python: Session.set_stdlib_limits(max_text_bytes=0, max_read_bytes=0, max_split_parts=0);
WebAssembly: runtime.setStandardLibraryLimits(maxTextBytes, maxReadBytes, maxSplitParts);
```

参阅[并发模块 — 任务取消请求](concurrent.md#concurrency-aot-boundary)。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/standard-library/foundation.md) · [fr-FR](../../fr-FR/standard-library/foundation.md) · [ja-JP](../../ja-JP/standard-library/foundation.md) · [ko-KR](../../ko-KR/standard-library/foundation.md) · [zh-CN](foundation.md) · [zh-TW](../../zh-TW/standard-library/foundation.md)
