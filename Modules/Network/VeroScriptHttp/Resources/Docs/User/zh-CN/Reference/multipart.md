# multipart 流式上传与增量读取

`VeroScriptHttpMultipart.h` 提供独立于 VM 的增量读取接口，可与服务端请求流组合。
协议依据 [RFC 7578](https://www.rfc-editor.org/rfc/rfc7578) 和 [RFC 2046](https://www.rfc-editor.org/rfc/rfc2046)。

## 原生接口

`FMultipartReader::TryCreate(content_type, out, error, options, context)` 从 Content-Type 的 boundary 创建 reader。
失败保留 out；context 可省略，省略时由原生调用方管理总保留量。
传 context 时，reader 的缓冲和所有交付载荷计入该 context；复制事件共享不可变数据，额度跟随实际存储释放。
已交付的事件在 context 关闭后仍可读，reader 不再接受新工作。

`Feed(bytes, final, event, work)` 返回实际接受的字节数及状态。每次最多交付一个事件，
调用方重新提交未消费后缀；最后一块的后缀仍需保留 final=true。只有 EventReady 更新 event。
final 表示输入结束，不能用一次空输入代替“暂时没有数据”。work 只在本次调用中借用，支持取消和预算检查。

| 状态 | 数值 | 行为 |
| --- | --- | --- |
| NeedInput | 0 | 等待后续输入 |
| EventReady | 1 | 取得本次事件，再推进未消费输入 |
| Done | 2 | 结束边界和最终输入均已处理 |
| Failed | 3 | 查看 Error；创建新 reader 重新开始 |

| 事件 | 数值 | 数据 |
| --- | --- | --- |
| PartBegin | 0 | 从 0 开始的 PartIndex、不可变 Head |
| Data | 1 | PartIndex、不可变 Data，非空且不超过 16 KiB/当前 context 单缓冲额度 |
| PartEnd | 2 | PartIndex |
| End | 3 | multipart 结束；仍需排空 HTTP 正文中的 epilogue 并确认 EOF |

Head 保留名称、可选文件名、Content-Type 和有序重复头。缺少 filename 与 filename="" 分别表达。
form-data 必须有单份 Content-Disposition、form-data 类型和 name 参数；name 可以是空字符串。
其他 multipart 子类型允许没有字段名，嵌套 multipart 正文作为原始数据交付，可显式交给另一个 reader。

头名称大小写不敏感；折行按空格展开。引号和 quoted-pair 正确处理，重复元数据参数及模糊的重复 disposition/type 失败。
正文保留 NUL、原始编码和重复字段顺序，不自动进行 charset、百分号或 Content-Transfer-Encoding 解码。
文件名是元数据，本接口不创建文件。合法结束边界、跨块边界、空部分、preamble/epilogue 和 transport padding 均有状态处理。

## 限额

默认最多 256 部分；单部分头 64 KiB、累计头 1 MiB、单部分正文 64 MiB、累计正文 256 MiB、
输入总量 272 MiB，preamble/epilogue 各 64 KiB。总输入包含 framing；各限额独立生效。
读取器只保存有界的头/边界窗口，正文按事件逐块交付，不聚合整个部分。
外部长期保留的事件继续占用 context 配额；超限报告 Limit。

## 脚本接口

`http::make_multipart_reader(context, content_type)` 或
`make_multipart_reader_with_options(context, content_type, options)` 返回共享 reader。
`http::make_multipart_options()` 提供独立值选项及对应 getter/setter：
max_parts、max_header_bytes、max_total_header_bytes、max_part_body_bytes、max_total_body_bytes、
max_input_bytes、max_preamble_bytes、max_epilogue_bytes。

reader 提供 feed(chunk, final)、state()、event()、error_code()、error()。
event 提供 kind()、part_index()、name()、has_filename()、filename()、content_type()、
header_count(name)、header(name,index)、data()。在错误的事件种类上访问不存在的载荷会明确失败。

配合 exchange.body_stream() 分块读取；WouldBlock 时交还应用调度器。
先处理 EventReady 和未消费后缀，再取下一块；HTTP reader 返回 End 后，以 final=true 排空解析状态直至 Done。
执行取消会使当前 native 调用失败，普通格式错误保存在 reader 的状态和错误中。

## 客户端上传

`FRequest::Multipart` 的每个 `FMultipartPart` 可采用拥有的 Body、单次 Upload，或原生 UploadFactory。
非空 Body 与流来源互斥。UploadLength 可省略；已知长度定义上传范围，源提前结束报告 Protocol，
超过范围的剩余数据不发送，终态取消源。未知长度需要生产者 Finish，不能把 WouldBlock 当作 EOF。

同一请求可混合缓冲和流，保留字段顺序、重复名称、可选文件名和二进制正文。
单次源只能提交一次；配置自动重试或重定向时，每个流部分都需要返回新源的原生工厂。
工厂在原生 I/O 执行器上调用，不得执行脚本或阻塞文件工作；重定向 rewind 要保持该部分的已知/未知长度一致。
每个部分绑定单消费者，重复使用同一 reader 会失败。请求失败、取消、超时或完成会取消不再使用的来源。

MaxUploadBytes 限制一次正文所有部分的源载荷，MaxTotalUploadBytes 累计所有尝试和重放；均不含 MIME framing。
缓冲部分每次提交或重放前预扣，流按接受用于上传的数据计费。暂停等待来源不会阻塞其他传输。
元数据和缓冲部分受 context 单缓冲总量限制；流正文通过有界管道供应，无需聚合整个文件。
Content-Type、Content-Length 和 Transfer-Encoding 由后端生成，不能由请求手工覆盖。

脚本先 begin_multipart，再调用：

- add_field(name, bytes) / add_file(name, filename, content_type, bytes)。
- add_field_stream(name, reader, length) / add_file_stream(name, filename, content_type, reader, length)。

length=-1 为未知长度，其他有效值非负。脚本最多 256 部分，元数据与拥有的缓冲数据累计最多 16 MiB；
流正文另受上传额度限制。脚本流为单次来源，不提供从 I/O 线程调用脚本的重放工厂。
普通正文、表单、multipart 和原始上传的 setter 会清理旧正文模式及来源。

显式文件来源与落地适配见[文件传输](files.md)；filename 元数据自身不触发磁盘读写。

## 完整样例

源码仓库中的 `Modules/Network/VeroScriptHttp/Resources/Examples/multipart_stream.vs`
在本地动态端口演示发送端暂停/供数、服务端增量解析、二进制正文、完整响应和优雅关闭。
先注册 network/http 并声明依赖，再由宿主运行该单文件。
样例使用逐字节输入展示边界处理；实际应用可分块读取，并保留 Feed 未消费的后缀。

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/multipart.md) · [fr-FR](../../fr-FR/Reference/multipart.md) · [ja-JP](../../ja-JP/Reference/multipart.md) · [ko-KR](../../ko-KR/Reference/multipart.md) · [zh-CN](multipart.md) · [zh-TW](../../zh-TW/Reference/multipart.md)
