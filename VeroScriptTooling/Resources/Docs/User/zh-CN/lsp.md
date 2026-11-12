# VeroScript 语言服务器

`veroscript-lsp` 是 VeroScript 的语言服务器，通过标准输入输出传输使用
`Content-Length` 分帧的 JSON-RPC 消息。请启动与项目使用的 VeroScript 发行包相匹配的
版本。

服务器直接使用编译器和 Tooling 引擎，因此语言规则、导入、预处理器、宿主绑定元数据
和诊断目录与 CLI 保持一致。

悬停返回结构化 Markdown：符号标题、VeroScript 签名代码块及逐行重载、文档和定义位置。
原生 Tooling 及 CLI 的悬停摘要仍保留普通文本。

## 传输与生命周期

正常顺序是 `initialize`、`initialized`、文档和工作区通知、功能请求、`shutdown`，最后
是 `exit`。必须先收到 `shutdown` 的响应再发送 `exit`；未完成 shutdown 就退出时，进程
结果为非零。

收到 `exit` 通知后会取消排队中和正在执行的语义分析，进程退出不会等待分析期限。

服务器在请求执行期间继续读取输入，并按接收顺序应用文档更新。客户端可以对等待中的
请求发送 `$/cancelRequest`；被取消的请求返回 JSON-RPC 错误码 `-32800`。闭工作区符号扫描
会在枚举文件以及各个文件分析之间协作检查取消；请求也有受限的服务器超时，避免过期查询长期占用 CPU。
输入队列有界，会施加反压而不会无限增长。`Content-Length` 按 UTF-8 字节数计算，分帧或
JSON 错误会写入错误流。

文件 URI 遵循标准 LSP file-URI 规则。`file://server/share/module.vs` 这样的 UNC URI
会保持为网络路径，在往返转换时不会丢失主机名或共享目录。

如果客户端声明支持 `workspace/didChangeWatchedFiles` 的动态注册，服务器会在
`initialized` 后发送 `client/registerCapability`，请求监听 `**/*.vs` 和 `**/*.[vV][sS][lL][iI][bB]`。
不支持动态注册的客户端可以自行配置这些匹配模式，并显式发送
`workspace/didChangeWatchedFiles` 通知。

## 语义分析与请求预算

文档更新按接收顺序应用。语义 token 分析使用独立工作线程及自有文档/绑定快照，因此着色期间仍可响应悬停和补全。未变化文档会复用有效的解析快照与完整 token 流。文档和依赖变化使相关结果失效；分析设置、受监视文件及绑定变化触发重新分析。仅调整请求预算、跟踪或结果上限会保留语义快照；工作区扫描上限更新符号搜索，不清除已解析程序。复用前还会检查磁盘依赖的时间戳与大小。被替代的分析返回 `-32801`，不能发布过时 token。

关闭并重新打开同一 URI 会开始新的文档生命周期：即使版本号重新开始，旧语义 token 结果 ID 也会清除。携带旧 ID 的 delta 请求会收到完整 token 数据；后续请求应使用新返回的 `resultId`。

预算从协议线程开始调度请求时计算；语义分析队列的等待时间计入预算，输入队列的等待时间不计入。取消返回 `-32800`，超时返回 `-32001`。排队中的过期请求无需等当前分析结束即可释放。为兼容旧配置，仅设置 `lspRequestTimeoutMs` 时，它也会设置分析预算；请显式设置 `lspAnalysisTimeoutMs` 来分开两类预算。跟踪包含 `lex`、`preprocess`、`parse`、导入路径解析（`imports`）、符号、查询、编码及分析队列耗时。`load` 包含其内部解析、预处理和导入阶段，不能把这些耗时直接相加。

客户端超时还应考虑协议队列等待及文档更新/诊断处理时间。这些协作式分析预算不保证端到端响应时间上限。

## 初始化与配置

`initialize.locale` 是应在打开文档前设置的 BCP 47 语言偏好。配置放在
`initializationOptions.veroscript` 下，也可以在
`workspace/didChangeConfiguration.settings.veroscript` 中用同一个对象更新：

```json
{
	"locale": "zh-CN",
	"initializationOptions": {
		"veroscript": {
			"importPaths": ["src", "shared"],
			"stdlibPath": "stdlib",
			"defines": ["FEATURE_SCORE=1"],
			"implicitImportPaths": [],
			"allowCircularImports": true,
			"allowProjectWideAccess": false,
			"lspRequestTimeoutMs": 2000,
			"lspAnalysisTimeoutMs": 5000,
			"lspTraceAnalysis": false,
			"maxCompletionItems": 512,
			"maxWorkspaceSymbolResults": 2000,
			"maxReferenceResults": 5000,
			"maxWorkspaceFiles": 10000,
			"maxWorkspaceFileBytes": 2097152,
			"bindingCatalogPaths": ["bindings/core.json", "bindings/game.json"],
			"predefinedMacros": [
				{
					"name": "FEATURE",
					"parameters": ["value"],
					"variadic": false,
					"body": "value",
					"sourceName": "host"
				}
			]
		}
	}
}
```

| 配置项 | 类型 | 作用 |
| --- | --- | --- |
| `importPaths` | 字符串数组 | 追加模块搜索根目录。 |
| `stdlibPath` | 字符串 | 标准库搜索根目录；空值会清除自定义根目录。 |
| `defines` | 字符串数组 | 对象式预处理定义，例如 `FEATURE_SCORE=1`。 |
| `implicitImportPaths` | 字符串数组 | 按正常可见性规则提供给每个源文件的文件。 |
| `allowCircularImports` | 布尔值 | 是否允许循环模块依赖。 |
| `allowProjectWideAccess` | 布尔值 | 是否允许项目范围共享未导出声明。 |
| `lspRequestTimeoutMs` | 整数 | 交互请求的超时毫秒数（50–30000），默认 2000。 |
| `lspAnalysisTimeoutMs` | 整数 | 语义 full/delta 的预算毫秒数（50–30000），默认 5000。 |
| `lspTraceAnalysis` | boolean | 通过 `window/logMessage` 输出分析阶段耗时，默认 false。 |
| `maxCompletionItems` | 整数 | 单次补全的最大条目数（32–5000）；被截断的列表会标记为不完整。 |
| `maxWorkspaceSymbolResults` | 整数 | 单次 `workspace/symbol` 返回的最大符号数（64–20000）。 |
| `maxReferenceResults` | 整数 | 单次引用查询返回的最大位置数（64–50000）。 |
| `maxWorkspaceFiles` | 整数 | LSP 工作区根下参与闭文件索引的最大 `.vs` 文件数（1–100000）。 |
| `maxWorkspaceFileBytes` | 整数 | 闭文件索引的单文件大小上限（1024–67108864 字节）。 |
| `bindingCatalogPaths` | 字符串数组 | 按顺序合并的宿主绑定目录；重复定义会被拒绝。 |
| `predefinedMacros` | 对象数组 | 含 `name`、`parameters`、`variadic`、`body` 和可选 `sourceName` 的宿主宏。 |

这些设置应与同一项目使用的 CLI 保持一致。服务器启动时以及文件变化时都会检查
`bindingCatalogPaths`。目录发生变化后会重新加载，并重新分析所有打开的文档；绑定目录
只提供元数据，不会自行注册运行时回调。

`initialize` 响应还包含 `serverInfo`，其中有服务器名称、生成的产品版本、宿主平台和
JIT 能力。这些字段描述当前运行的发行包，客户端可以显示它们，但不应硬编码特定版本，
也不应根据平台或 JIT 值推断语言语义。

## 已提供的能力

| 能力 | 行为 |
| --- | --- |
| 文档同步 | 打开/关闭通知、增量变更，以及携带文本的保存通知。 |
| 补全 | 触发字符为 `.`, `:`, `@`；补全项会携带可用的解析后文档，resolve 会保留这些文档。只有缺少 documentation 时才会将 `detail` 转为 Markdown。 |
| 悬停 | 已解析的符号详情，以及连续的源码或宿主文档。源码中的 `@param`、`@return(s)` 标记使用本地化标签。 |
| 导航 | definition、declaration、type definition 和 implementation。 |
| 符号与范围 | 嵌套文档符号，以及 token、行、全文档的选择范围。 |
| 文档链接 | 为带引号的 `import` 和 `#include` 目标生成链接。 |
| 语义 token | 支持全文档 token 与 full delta；不提供范围请求。 |
| 签名与引用 | 签名帮助、引用和文档高亮。 |
| 重命名与格式化 | `prepareRename`、重命名、全文档、范围和输入 `;`、`}`、换行后的局部格式化。 |
| 折叠范围 | 基于词法分析的代码块、连续注释和导入组折叠。 |
| 代码操作 | `source.formatDocument`、安全的 `source.organizeImports` 重复导入删除、生成方法实现的 `refactor.rewrite`，以及 `codeAction/resolve`。 |
| 工作区符号 | 对打开缓冲区、工作区根、宿主符号和 soft symbol 进行不区分大小写的搜索。 |

## 协议方法范围

服务器接受以下生命周期和同步消息：

- `initialize`、`initialized`、`shutdown` 和 `exit`；
- `textDocument/didOpen`、`textDocument/didChange`、`textDocument/didSave` 和
  `textDocument/didClose`；
- `workspace/didChangeConfiguration`、`workspace/didChangeWorkspaceFolders` 和
  `workspace/didChangeWatchedFiles`；以及
- 用于取消等待中请求的 `$/cancelRequest`。

功能请求方法包括：

- `textDocument/completion` 和 `completionItem/resolve`；
- `textDocument/hover`、`textDocument/definition`、
  `textDocument/declaration`、`textDocument/typeDefinition` 和
  `textDocument/implementation`；
- `textDocument/prepareTypeHierarchy`、`typeHierarchy/supertypes` 和
  `typeHierarchy/subtypes`；
- `textDocument/documentSymbol`、`textDocument/selectionRange` 和
  `textDocument/documentLink`、`textDocument/foldingRange`；
- `textDocument/semanticTokens/full` 和
  `textDocument/semanticTokens/full/delta`；
- `textDocument/signatureHelp`、`textDocument/references` 和
  `textDocument/documentHighlight`；
- `textDocument/prepareRename` 和 `textDocument/rename`；
- `textDocument/formatting`、`textDocument/rangeFormatting`、
  `textDocument/onTypeFormatting`、`textDocument/codeAction` 和
  `codeAction/resolve`；以及
- `workspace/symbol` 和 `workspaceSymbol/resolve`。

## 文档同步

文档同步使用增量模式（`textDocumentSync.change = 2`）。服务器按照客户端发送顺序将
变更应用到内存覆盖层，同时接受完整文本变更和范围变更。LSP 行号从 0 开始，字符位置
使用 UTF-16 code unit。

每个 URI 在一次打开期间的文档版本必须递增。版本号不大于上次接受版本的 `didChange` 会被忽略，防止
延迟消息覆盖较新的文本。`didSave` 可以携带完整文本，并在发布诊断前更新覆盖层。
`didClose` 会移除覆盖层并清理该 URI 已发布的诊断。打开的缓冲区在导入解析中优先于磁盘
文件。

`initialize.workspaceFolders`（或 `rootUri`）会建立明确的闭文件索引根。
`workspace/didChangeConfiguration` 会应用新配置并重新分析打开的文档。
`workspace/didChangeWorkspaceFolders` 会先增加或移除这些根目录。
`workspace/didChangeWatchedFiles` 会使工作区符号索引失效，并重新分析打开的文档，使磁盘
上的变化生效。

## Tooling 语义

- 补全会将可调用的同名重载合并为一个条目，并在详情中逐行列出签名。在调用上下文中，
  合并的重载项使用 `Select(${0})` 这样的空参数停靠点，不猜测应插入哪一个重载的参数。
  非重载的可调用项可以使用 `Helper(${1:count}, ${2:name})` 等 LSP
  snippet；泛型类型项可以使用 `HostPair<${1}, ${2}>` 等 snippet。若泛型补全的替换范围后已跟随 `<`
  （中间可有空格或制表符），服务器会省略泛型 snippet，只插入类型名，避免再次插入 `<`；
  可调用项的补全目标后面已有 `(` 时，会省略 snippet，避免重复生成调用语法。
  只有客户端声明支持 `textDocument.completion.completionItem.snippetSupport` 时才返回 snippet；
  否则使用普通文本插入。客户端声明支持 snippet 时，可变参数宿主格式化函数在调用上下文中使用一个空参数停靠点，
  例如 `print(${0})`，而不会从合并后的重载详情中推导参数占位符。不支持 snippet 时只插入函数名。
- 来自限定命名空间的补全会在 `label` 中保留叶子名称。存在可用的可调用或泛型类型 snippet 且客户端声明支持时，
  `insertText`（以及存在时的 `textEdit.newText`）才使用该 snippet；否则只插入叶子名称。其容器名称位于
  标准字段 `labelDetails.description`。可选的
  `$valencePresentation` 元数据为 Valence 提供最终标签／声明文本和 UTF-16 语义片段，其他
  LSP 客户端可以忽略它。它不会改变过滤、排序或实际插入文本。
- 命名空间段补全会在 `label` 和 `filterText` 中保留叶子名称。在表达式或语句中的限定路径里，接受命名空间补全时，
  如果替换范围后面还没有 `::`，插入文本会补上它；若已有 `::`，或正在补全 `using namespace` 目标，则只插入名称。
- 补全先按文本匹配质量、再按上下文优先级排序：精确匹配、忽略大小写的精确匹配、前缀、
  单词边界／camel-case、包含和子序列匹配。`sortText` 携带这套完整顺序。只有首项处于精确、
  忽略大小写精确或前缀等级，且严格优于所有其他返回项时，才会标记 `preselect`。
- 基元和容器类型词都是内置类型。基元拼写在补全、悬停和语义令牌中仍分类为 `keyword`；容器
  拼写则分类为 `type`／type alias。两者的悬停详情均遵循当前本地化，均没有源定义，也不能重命名。
- `completionItem/resolve` 保留原始补全项及已有文档。仅当缺少 documentation 且存在 `detail` 时，才把 detail 作为 Markdown 文档返回。
- 悬停会读取源码声明前的连续注释。源码定义的预处理宏补全项可以携带其前置注释；宿主补全文档来自绑定元数据。悬停会使用当前语言目录的标签呈现 `@param`、`@return` 和 `@returns`。
- 用户命名空间和保留的 `std::` 树在悬停、导航、引用、重命名、大纲和语义 token 中都
  按 namespace 符号处理。标准库命名空间不可重命名。
- `textDocument/declaration` 返回与 definition 相同的位置。implementation 会查找派生类型
  与匹配的接口／虚函数覆写，而不是复用 definition。type definition 会查找
class、struct、interface、enum 或 type alias 的声明，并能在不完整缓冲区中使用大纲和
声明头恢复。只有参数和 `const` 限定符匹配、且不是 `final` 的 virtual、`override` 或
abstract 成员才会作为实现目标；类型名存在歧义时会安全地排除，不会把 definition 当作
implementation 返回。
- selection range 按 token、行、全文档逐级嵌套。document link 会从当前文档解析带引号的
  `import` 和 `#include` 路径，也支持可选的 `export` 前缀。
  链接复用核心模块解析规则，包含当前目录、配置的搜索路径、标准库目录和未保存的打开文档；
  尚未解析成功的目标保留本地链接。
- semantic-token delta 在前一个结果未知或过期时返回完整数据；结果标识匹配时返回最小的
  连续整数流编辑（或空编辑列表）和新的 result id。
- semantic token 图例的完整类型为 `namespace`、`type`、`class`、`enum`、`interface`、
  `struct`、`variable`、`property`、`enumMember`、`function`、`method`、`macro`、
  `keyword`、`decorator`、`decoratorArgument`、`constant`、`label`、`string`、`number`
  、`comment` 和 `preprocessor`。词法基线还会分类语言／类型词、指令头和宏名称；它覆盖每个
  有效物理行且不会生成重叠的 LSP 范围，并在声明语义从不完整输入恢复时持续可用。
- range formatting 会安全地扩展到完整物理行并保留外层代码块缩进。整文档、范围和输入时格式化都遵循
  `insertSpaces`：设为 `false` 时每级缩进输出一个制表符；否则每级使用 `tabSize` 个空格（最多 16，
  缺失或无效时默认 4）。输入 `;`、`}` 或换行后可获得同样的局部编辑。
- code action 在未被 `context.only` 排除时返回 `source.formatDocument`。只有能安全识别并删除
  完全重复的导入行时，才会返回 `source.organizeImports`；该操作只删除这些重复项。
- 在类或结构体正文中有效的空光标位置，代码操作会为补全提供的相同方法实现生成
  `refactor.rewrite` 操作；`context.only` 可用 `refactor` 或 `refactor.rewrite` 筛选它们。
- 在类或结构体正文中的空位置或标识符前缀位置，补全会提供可访问、尚未被实现且不是 `final` 的
  继承虚方法，以及可实现或覆盖的接口方法（包括待实现的纯接口要求和可覆盖的虚拟默认方法）。
  泛型签名会按派生类型具体化。接受候选项会插入包含可编辑
  `throw 0;` 方法体的声明；纯接口实现不会添加 `override`。客户端不支持 snippet 时会收到普通方法文本。
- 工作区符号查询不区分大小写，并按空白拆分查询词。它会匹配符号名、限定名、容器名、
  detail 和文件标签；`::` 与旧式 `.` 分隔符会按同一形式处理。精确匹配和前缀匹配会排在
  模糊子序列匹配之前。每个结果都带有稳定的 `data.veroscriptSymbolId`；
  `workspaceSymbol/resolve` 会刷新 detail 与 containerName，找不到该 ID 时保留原始项。
- 文档符号和工作区符号会公开 namespace 容器。同一个声明 identity 即使通过多个导入路径
  被观察到，也只返回一次。没有源文件的宿主符号使用 `veroscript:host-bindings` URI 和
  零范围。

打开、接受的变更、保存、配置变化，以及工作区文件变化后，服务器会发布
`textDocument/publishDiagnostics`。诊断按源文件分组，也会覆盖导入文件；空数组会清除旧
标记。每条诊断都包含 LSP severity、message、可用时的稳定 code，以及
`source: "veroscript"`。没有源文件的宿主符号使用 `veroscript:host-bindings` URI 和零范围。

当请求没有适用结果时，服务器会返回该 LSP 方法对应的 null 或空结果。不支持的方法返回
JSON-RPC `-32601`；当方法要求特定文档或位置而参数格式错误时，返回 `-32602`。

## 本地化

服务器使用与 CLI 和运行时相同的本地化策略。`initialize.locale` 只改变诊断和 Tooling
的显示，不改变脚本语义或符号解析。客户端应使用消息 ID/诊断 code，而不是翻译后的文本
进行逻辑判断。缺少的翻译文件或键会回退到 `en-US`。参见[诊断本地化](../../../../../VeroScript/Resources/Docs/User/zh-CN/localization.md)。

## 相关参考

另请参阅 [Tooling 模块](index.md)、[CLI 参考](../../../../../VeroScriptCli/Resources/Docs/User/zh-CN/index.md)、[宿主绑定](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/bindings.md)
和[调试](debugging.md)。

## 宿主提供的模块源码

宿主可以提供已解析的模块环境和编译处理器。标准 Runtime 服务器不会自动沿用 CLI 的包选择，也不会自动获得应用的原生回调。请使用与所编辑程序一致的导入路径、绑定目录和宿主服务；绑定目录提供描述，不会安装可执行处理器。

当 `capabilities.experimental.veroscriptModuleEnvironment` 声明 `moduleSourceMethod` 时，支持此能力的客户端可以通过该方法（`veroscript/moduleSource`），以只读方式打开返回的 `veroscript-module:` 位置。这些内容是源码快照，不是可编辑的工作区文件。服务器重启或编译指纹改变后，应从所属源码重新跳转；模块不存在或无权访问时返回 `null`。普通磁盘源码仍使用文件 URI。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/lsp.md) · [fr-FR](../fr-FR/lsp.md) · [ja-JP](../ja-JP/lsp.md) · [ko-KR](../ko-KR/lsp.md) · [zh-CN](lsp.md) · [zh-TW](../zh-TW/lsp.md)
