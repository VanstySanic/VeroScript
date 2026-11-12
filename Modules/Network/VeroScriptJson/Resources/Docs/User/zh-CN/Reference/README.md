# VeroScriptJson

脚本的按值捕获遵循 Core 的只读绑定规则。需要对捕获的宿主值调用可变原生成员时，使用 `mutable` Lambda，或对可写绑定进行引用捕获。初始化捕获与局部 static 沿用 Core 的普通语义。

原生数据接口、脚本绑定及所需编译处理器分别按下述契约使用。

## 当前数据 API

`VeroScriptJson.h` 提供独立的 C++17 数据接口，无需链接 Core。

`VeroScriptJsonMapping.h` 提供 C++ 字段、属性、枚举、容器与转换器映射，以及按输入／输出方向生成 Schema。
接口、所有权和事务规则见 [C++ 类型映射](native-mapping.md)。
可选 C++ 组合分别见 [HTTP / SSE JSON 适配](../../../../../../../Network/VeroScriptHttp/Resources/Docs/User/zh-CN/Reference/json-adapters.md) 和 [WebSocket JSON 适配](../../../../../../../Network/VeroScriptWebSocket/Resources/Docs/User/zh-CN/Reference/json-adapters.md)。

- 严格 JSON 文本解析、格式化输出、Unicode 标量检查和重复键策略。
- `FJsonValue` 的六类值、原始数字文本、精确十进制比较与受检数值转换。
- 不可变共享节点；复制值和读取子值后，修改各自独立。单次修改失败不提交接收者。
- `FBuilder` 批量构建数组或对象；复制 builder 共享构建状态，`TryFinish` 关闭该状态，`TryReset` 重用。
- JSON Pointer 与可复用的 `FPointer`、RFC 6902 Patch、RFC 7396 Merge Patch；根删除通过 `FDocument::bHasValue` 表达。
- 增量事件读取和写入；输出 sink 可以暂停，已接受的事件通过 `Resume` 推进。

`Try*` 成功返回 true、清空错误；失败返回 false，保留输出值。非 Try 构造与工厂仍可能抛出分配异常。
分配失败时，错误文本允许为空；`FError::Code` 和无需分配的 `Id()` 仍可读取。外部工作检查回调抛出异常时，
报告 `Callback`；输出 sink 的普通异常报告 `Sink`，分配异常报告 `Allocation`。
数组和对象的数量、索引使用 `int64`；原生分配使用受检的 `size_t`。
默认深度上限为 256，DOM 的硬上限也是 256。`as_double` 可以舍入；`TryDouble(..., true)` 必须精确。

普通 `FJsonValue::TryPush/TrySet` 复制当前容器以维持快照。循环构建大容器请用 builder。
并发读取快照可以共享；修改同一个包装变量或同一个 builder 状态需要调用方同步。

### 流

`FReader::Feed` 返回消费字节数。`EventReady` 时读取一个事件；未消费的后缀仍由调用方持有。
对最后一个输入块传 `bFinal=true`，处理所有事件及剩余后缀，直到 `Done`。数字可以跨块延续，
例如非最终块 `1` 后接 `2 ` 会产生数字 `12`。

`FReader::Visit` 在同一次调用内逐个派发 SAX 事件。visitor 返回 false 时在当前事件后暂停，结果为
EventReady；重新提交未消费后缀即可继续，该事件不会重复派发。visitor 抛出异常后进入 Failed，
须 reset 后重新开始。visitor 和工作检查只在当前调用期间使用，不保留借用的执行上下文。

`FWriter::Write` 通过 `bAccepted` 告知事件是否已接受。接受后只调用 `Resume`，不要再次提交同一事件。
sink 返回 `0 + NeedOutput` 是正常背压，`0 + Progress` 是错误。`Finish` 可以暂停，完成后可重复调用。
流式对象按用户给定顺序输出，不支持事后排序；需要排序时使用 DOM 的 `TryWrite`。

### 选项与结构化错误

`json::limits`、`json::parse_options`、`json::write_options` 是独立值，复制后修改互不影响。
`options.limits()` 返回限制的副本，修改后用 `options.set_limits(limits)` 提交。
limits 支持 `input_bytes/output_bytes/token_bytes/nodes/depth/number_bytes/exponent/work_units` 及对应 `set_*`。
脚本数量为非负 `int64`；设置更高深度不会取消数据层 256 的硬上限。

解析选项提供 `duplicate_keys/set_duplicate_keys`（0 Reject、1 KeepFirst、2 KeepLast、3 ReportAll）和
`allow_leading_bom/set_allow_leading_bom`。ReportAll 仅用于 reader/SAX，DOM 不接受该策略。
写入选项提供 `pretty/sort_keys/indent` 和对应 setter，缩进为 0–16。

带选项的入口为 `json::parse_with_options(text, options)`、
`json::try_parse_with_options(text, ref value, ref error, options)`、`value.write_with_options(options)`、
`json::make_reader_with_options(options)` 和 `json::make_writer_with_options(options)`。
这些名称避免依赖当前宿主成员注册所不支持的普通方法重载。

| 非抛错入口 | 输出参数 |
| --- | --- |
| `try_parse(text, ref value, ref error)` | DOM 与错误 |
| `try_write(value, ref text, ref error)` | 文本与错误 |
| `try_write_with_options(value, ref text, ref error, options)` | 文本与错误 |
| `try_number(text, ref value, ref error)` | 精确数字与错误 |
| `try_from_string/try_from_double(input, ref value, ref error)` | JSON 值与错误 |
| `try_compile_pointer(text, uri_fragment, ref pointer, ref error)` | 可复用路径与错误 |
| `try_as_int64/try_as_uint64/try_as_double/try_as_double_exact/try_as_bool/try_as_string(value, ref result, ref error)` | 对应原生类型与错误 |
| `try_resolve(document, pointer, ref value, ref error)` | 定位结果与错误 |
| `try_patch(document, patch, ref document_result, ref error)` | 文档与错误 |
| `try_merge_patch(value, patch, ref value_result, ref error)` | DOM 与错误 |

以上函数均位于 `json`；普通数据错误返回 false、保留原结果并写入结构化错误，成功清空错误。
执行取消和无法交付错误/结果对象的失败仍使 native 调用失败。`error` 提供
`code/message/message_id/has_source/byte_offset/line/column/instance_pointer/patch_operation`。
Pointer 的编译和查询、Patch 路径扫描也检查工作预算与取消，编译后的路径在查询时重新检查当前深度和 token 限额。

### 脚本 builder 和流

`json::array_builder()` / `json::object_builder()` 返回共享 builder 句柄，支持 `push/set/reserve/size/capacity/finish`。
`finish()` 返回独立 DOM 并关闭当前构建；`reset_array/reset_object` 开始新构建，所有别名观察同一状态。

builder 还提供 try_push、try_set、try_reserve、try_finish、try_reset_array/object、try_size/capacity，
最后一参为 ref error；finish/size/capacity 的第一参为 ref 输出。普通失败保留结果，关闭或类型错误可用 error 检查。
DOM 另有 try_contains(key, ref found, ref error) 与 try_keys(ref array, ref error)。

`json::make_reader()` 返回共享 reader。`feed(chunk, final)` 返回实际消费的 `int64` 字节数；
`state()` 为 0 NeedInput、1 EventReady、2 Done、3 Failed。EventReady 时用 `event()` 取得独立事件值，
通过 `kind/text/boolean` 读取。失败后可用 `error()` 取得结构化错误，再用 `reset()` 开始新文档。
调用方必须保留并重新提交未消费的输入后缀。

`json::make_writer()` 接收 `write(event)`，返回事件是否已接受。`state()` 为 0 Active、1 NeedOutput、
2 Done、3 Failed。`take()` 取走当前缓冲字节（每批最多 64 KiB）；NeedOutput 时先取走字节，再 `resume()`，
不要重复提交已接受事件。`finish()` 可暂停；继续 take/resume 直到 Done，并取走最后一批。流的总输出仍受限额约束。
reader/writer 的语法与状态失败通过 state/error 报告；执行取消仍使 native 调用失败，避免继续运行已取消脚本。

`reader.visit(chunk, final, visitor)` 提供 SAX，visitor 类型为 `function(json::event)->bool`：
返回 true 继续，false 在当前事件后暂停。返回值仍为消费字节数，EventReady 表示暂停；使用未消费后缀恢复。
visitor 只在本次合法 VM 调用内执行，不保留到后台。visitor 的脚本错误使外层调用失败；禁止在 visitor 内
重入同一 reader 的 feed/visit/reset。捕获其他独立 reader 或 writer 不受该限制。

`json::make_event(kind, text, boolean)` 可手工建立事件；kind 按 `EEvent` 定义依次为 BeginArray、EndArray、
BeginObject、EndObject、Key、Null、Boolean、Number、String（0–8）。数字 text 保留精度，交给 writer 校验。

## JSON Schema

`VeroScriptJsonSchema.h` 提供 `FSchema`。`TryCompile` 构造不可变的编译结果，复制后可以并发读取；
每次验证使用独立预算和报告。`TryIsValid` 返回是否完成验证，通过输出 bool 表达实例是否符合约束；
实例不符合约束是正常结果。`TryValidate` 另提供实例 Pointer、Schema 位置、关键字、说明和子错误。

```vs
json::schema schema = json::compile_schema(json::parse("{\"type\":\"integer\",\"minimum\":0}"));
bool valid = schema.is_valid(json::from_number("1.0"));
json::schema_report report = schema.validate(json::from_string("bad"));
print(report.issue(0).get("keyword").as_string());
```

`schema.validate_with_limits(value, max_issues, limits)` 控制报告和执行预算。
注解及其控制选项见 [Schema 注解报告](schema-annotations.md)。
`schema_report` 提供 `valid/truncated/size/issue`；`issue(index)` 返回带 `details` 的 JSON 对象。
`truncated` 表示报告边界已到达，后续可能仍有错误；不会把不完整报告视为验证成功。
`json::try_compile_schema(value, ref schema, ref error)` 和
`json::try_validate_schema(schema, value, ref report, ref error)` 遵循保留原结果的规则。

`json::schema_options` 提供 `retrieval_uri/limits/max_resources` 和相应的 `set_*` 方法。
空检索 URI 使用 `urn:veroscript:json-schema:root`；相对引用不依赖工作目录。
`json::compile_schema_with_options(schema, options, resources)` 的最后一项为 URI → Schema 的 JSON 对象。
C++ 另支持编译期间借用的同步 `FSchemaResolver`；解析到的资源全部由编译结果拥有，不保留回调。
Json 不自行下载引用。内置 Draft 2020-12 元 Schema 和八种标准词汇资源随实现提供。

支持的基础是 Draft 2020-12，包括精确数值、动态引用和 unevaluated 组合。`1` 与 `1.0` 数学相等，
不把大整数或小数约束先转换成 double。`pattern` 和 `patternProperties` 使用 Unicode ECMAScript 语义。
`format` 默认作为注解；可通过编译选项或元 Schema 的 `format-assertion` 词汇启用全部 19 种标准格式断言。
选项、方言规则及资格范围见 [Schema 格式断言](schema-formats.md)。
内容编码和内容媒体类型也保持注解，不自动解码或执行其他格式。

编译和验证检查预算、取消、节点、文本以及递归深度；URI 最多 64 KiB，正则模式最多 4096 字节，
正则单次执行的内部保留分配最多 16 MiB。还存在宿主栈和执行步数保护，触发时返回 Limit。
报告项限制为 1–16384，包含子错误；内部诊断同样有数量和字节预算。

带资源表与选项的结构化 Schema 入口为
`try_compile_schema_with_options(value, options, resources, ref schema, ref error)`；
报告上限入口为 `try_validate_schema_with_limits(schema, value, max_issues, limits, ref report, ref error)`。

引用对象也提供非抛错入口：`pointer.try_resolve(value, ref out, ref error)`、
`pointer.try_resolve_with_limits(value, ref out, ref error, limits)` 和
`document.try_get(ref out, ref error)`。缺少根值时 document.try_get 返回 false 并保留 out。
`try_compile_pointer_with_limits(text, uri_fragment, ref pointer, ref error, limits)` 检查当前编译额度。

```cmake
find_package(VeroScriptJson 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Json)
```

`FSchemaOptions::MaxResources = 256`

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](README.md) · [zh-TW](../../zh-TW/Reference/README.md)
