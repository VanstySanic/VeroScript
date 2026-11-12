# 开发文档 — VS Code

编辑器与原生宿主的公共职责及六语言维护统一见 [公共编辑器集成](../../../../../../../../Resources/Docs/Developer/zh-CN/editors.md)。

Core、Tooling、VM、JIT 与 GUI 提供者应来自匹配的原生源码／构建配置，包括 C++ 语言模式；仅产品版本相同不能证明 ABI 兼容。SDK 集成用例通过补全和悬停覆盖静态泛型、handle 和嵌套类型接收者。可选的 `GetLastJitDiagnostics` 快照属于 C++ 宿主 API，当前 LSP／DAP 线协议不提供该接口。

运行时 UI 消息采用 `vscode.l10n.t` 与 `Resources/Localization/Bundles`。`Resources/Localization/<locale>/manifest.json` 生成 `package.nls`；保持参数编号与清单引用一致。

使用 Node.js 22.18.0+ 和桌面 VS Code 1.106.0+，在模块目录执行以下命令。单元测试覆盖配置和界面资源；集成测试需要桌面 Electron/VS Code 和当前 Runtime。运行域协议夹具不执行 VM。检查 package:contents 和 VSIX 许可证；原生验收、远程宿主、真实应用运行域分别提供报告。

```sh
npm ci
npm run typecheck
npm run build
npm run test:unit
npm run test:integration
npm run package
npm run package:contents
```

桌面 Extension Host 测试应逐套串行运行。多个 VS Code 测试窗口同时打开时，可能争用补全确认、撤销和回车用例所需的编辑器焦点。

原生参数测试允许每次 CLI 调用最多 180 秒，不注入未使用的宿主宏，使参数转发验证独立于完整 GUI 冷编译。宏配置由专门的配置／协议用例覆盖，真实 GUI 编译另行验收。这些测试预算不改变 LSP 或调试期限。比较实际进程参数前，先规范化 JIT 夹具路径。

[Standards](../../../../../../../../Resources/Docs/Standards/zh-CN/GENERAL.md)



## 开发调试与验收

在本模块运行 npm ci、npm run typecheck、npm run test:unit、npm run package，VSIX 生成于 out/veroscript-0.1.1.vsix。npm run dev:setup 创建不会覆盖现有配置的开发调试文件。npm run watch 监听源码和本地化。

设置 VEROSCRIPT_TEST_RUNTIME 为解压后的 Runtime 绝对目录，再运行 npm run test:integration 可执行真实编辑器测试；追加 -- --packaged 可测试安装后的 VSIX。VEROSCRIPT_TEST_VSCODE_VERSION 可指定编辑器版本。下载和测试工作区默认位于仓库 .tmp/vscode-plugin；VEROSCRIPT_TEST_DIRECTORY 可将测试工作区和用户配置重定向到其他绝对目录，例如 WSL 中的 Linux 原生文件系统。结束相关进程后可以清理。


## 宿主集成与运行域协议

宿主可以是 C++、UE 或其他程序，但需要提供兼容的 TCP DAP 端点与真实原生绑定。原生 Embed 示例的 RunDapTcp 展示了使用 Stdio.LiveBindings 启动 VeroScriptDapTcp::Run。绑定生命周期、脚本执行线程和断开后的监听生命周期由宿主负责。插件调试脚本 VM；C++/UE 原生代码使用对应调试器。宿主已经运行时可以新建脚本调试会话；运行域在线附加需要下述宿主预备的调试桥；不能接管已进入 JIT 的 VM。

宿主须提供 IVeroScriptLiveDebugDomain，并在 initialize 中声明 veroscriptSupportsDomainAttach: true。缺少声明会拒绝连接，不会回退加载脚本；初始连接/initialize 期限为 10 秒。输出记录宿主、运行域身份及不透明的源码/绑定修订；暂不自动比较源码或同步 LSP 元数据。变量修改遵循 veroscriptCanSetVariable，不支持的暂停/修改请求会明确报错。断开发送 terminateDebuggee: false；连接丢失会结束会话，不自动重连。宿主负责取消、生命周期与安全点调度，执行前准备调试桥，并在当前单活动上下文模型中序列化调试调用。实际 UE 接入仍需单独验证。

宿主能力标记也控制单步、表达式求值和条件断点，省略的标记视为不支持。插件更新 VS Code 可表达的对应能力，并明确拒绝不支持的请求；普通断点仍可使用。即使宿主声明 veroscriptCanTerminate，停止/断开也保留由宿主取消脚本的策略。 宿主发出 `terminated` 后，插件仍完成编辑器的最终 `disconnect` 握手，并保持 `terminateDebuggee: false`。未完成操作只失败一次，迟到响应和重复终止事件被忽略；传输已关闭时，本地确认最终断开请求。

[DAP](../../../../../../../../VeroScriptTooling/Resources/Docs/Developer/zh-CN/workspace-and-debug.md)

## 内存查看契约

DAP 的 `memoryReference` 和 `veroMemory` 字段应作为不透明的十进制字符串保留，包括超出 JavaScript 安全整数范围的值。变量查看子项只读，句柄仅在当前暂停有效。转发宿主能力声明，不能从元数据推断支持 `readMemory`／`writeMemory`。集成测试覆盖原生内存类型的编辑、运行、检查、调试及在线域元数据转发；在线域协议夹具不验证真实宿主 VM。

## 客户端调度与双向 DAP

编辑器事件批次共用一次最新文档状态同步；同步期间到达的事件和显式重启请求保留到下一轮，当前轮失败后仍继续处理。在线域 DAP 会加入本地事件并重编号输出消息，反向请求响应的 `request_seq` 必须映射回宿主原编号，也包括乱序返回。协议夹具验证编号映射，不实际启动终端或子调试会话。

## 原生模块源码接入

`ModuleSources` 在语言客户端边界转换 URI，并调用服务器公布的 `moduleSourceMethod`。编辑器 URI 保留原始协议 URI、服务器生命周期和编译指纹；发回服务器时仅使用原始 URI。只读视图不进入源码覆盖层，也不成为可写编辑目标。缺失、已退役、已取消或已变化的快照均被拒绝，包括等待响应期间发生的变化。模块符号和诊断保持所属客户端归属。 自动重启时，即使客户端对象和编译指纹未变，也必须建立新的源码生命周期。

设置 `VEROSCRIPT_TEST_MODULE_LSP` 为 `VeroScriptModuleEnvironmentToolingTests` 可执行文件的绝对路径，可在 Extension Host 测试中运行原生 `--lsp` 夹具；未提供时会跳过，不能据此宣称完成原生模块环境验收。默认 Runtime 服务使用原生启动选项：插件不增加原生处理器的 JSON 配置，也不自动注入 CLI 包环境。DAP 的 `sourceReference` 和冻结源码响应继续由 VS Code 标准链路处理。

注解名称匹配、规范拼写和签名参数解析归原生工具处理，包括多行标签、嵌套泛型和含逗号的字面量。客户端通过 SDK 转发结果。集成测试在不替换客户端的情况下修改同一绑定目录的 `caseSensitive` 策略，并通过 VS Code 应用 `source.organizeImports` 编辑，核对精确范围、CRLF 保留和幂等性。

## 变量展示与精确数值类型

通过 SDK 和运行域桥完整保留每个 DAP 变量的 `presentationHint`，特别是 `attributes: ["readOnly"]`。`supportsSetVariable` 和 `veroscriptCanSetVariable` 描述会话操作能力，不代表所有变量都可写；原生校验仍是最终依据。回归用例覆盖不可变 `variant` 的子项、嵌套载荷、监视子项与普通局部变量的可编辑性，以及两种运行域编辑能力设置下的提示透传。

精确数值类型保留原生名称和文本值。上下文 TextMate 用例覆盖 `std::bigint`、`std::rational` 和 `std::numeric::rounding_mode`，不把它们的叶子名称保留为关键字。新增或修改的用例在使用匹配工具完成类型检查、真实 Extension Host 及 VSIX 验证之前仍属未验证，旧报告不能证明新原生源码已通过验收。

## 配套插件工具 API

`Public/api.d.ts` 定义工具 API 1。`getToolchain(scopeUri, sourceFile?)` 复用 Runtime、编译参数、环境、运行后端／参数与源范围。scope URI 选择工作区设置，可选源文件展开文件变量。解析可能查询 CLI 版本，故仅显式执行时调用。`compileArguments` 不含命令、入口或执行后端；调用方追加自己的命令。GUI 检查／编译强制 VM 并省略 JIT 模块。VSIX 保留 `Public` 声明，并保持测试所用 `ready`／`snapshot` 导出。应使用匹配 Runtime，在真实 Extension Host 和安装后的 VSIX 中验证此 API；原生 GUI 执行另有独立验收要求。

解释器实现已独立到 BytecodeVm 模块。迁移编辑器测试环境时保留完整发行 Runtime，包括 `Vm/`。原生 LSP/DAP 启动层提供默认工厂；插件不会添加新的线协议字段或推测自定义宿主的工厂。SDK 回归覆盖推导出的函数捕获悬停、补全、参数提示、定义跳转、UTF-16 范围，以及不完整编辑后当前接收者的悬停更新。

迁移原生模块测试夹具时，还应将 `VEROSCRIPT_TEST_VM_BACKEND` 设为 Runtime 的 `Vm/` 目录中匹配原生 VM 模块的绝对路径。这会覆盖夹具构建时记录的 VM 路径；普通 Runtime 的 LSP/DAP 仍由原生启动层提供默认工厂。

发布者以 `package.json` 的声明为准，GUI 依赖必须与主插件标识一致。根 README 随包提供，作为扩展详情页，不链接到未打包的仓库文件；功能包文档过滤仅豁免 `extension/readme.md`。发布 Marketplace 需要对应已注册发布者的权限并完成验收，修改清单不等于注册发布者。

文件图标由[共享图标生成器](../../../../../../../../Resources/Assets/icons/README.md#regeneration)重新生成。构建时将[共享 256px 产品 PNG](../../../../../../../../Resources/Assets/icons/veroscript-icon-256.png)复制为扩展展示图标。

<!-- docs-navigation -->

---

[开发文档](../../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
