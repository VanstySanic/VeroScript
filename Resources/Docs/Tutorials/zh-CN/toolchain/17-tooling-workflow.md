# Tooling 工作流：连接编辑器和调试器

本教程将一个 VeroScript 项目连接到语言服务器和调试适配器。你会先验证 CLI，
再配置 LSP、体验编辑器能力，最后启动 DAP 调试会话。

## 第 1 步：验证 CLI

使用同一版本 Runtime 提供的 CLI 和 VM、版本匹配的语言服务器，以及支持 LSP 和 DAP 的客户端。将下面两个文件保存到同一个可写的练习目录。在该目录运行终端命令，并将它作为编辑器的工作区打开。

### hello.vs

```VeroScript
import "lesson-utils.vs";

namespace Lesson
{
	int twice(int value)
	{
		return value * 2;
	}
}

int main(string[] args)
{
	std::chrono::now();
	int value = Lesson::twice(lessonSeed());
	print("value={}, argc={}", value, len(args));
	return 0;
}
```

### lesson-utils.vs

```VeroScript
export int lessonSeed()
{
	return 21;
}
```

```powershell
veroscript --version
veroscript --check hello.vs
veroscript --vm-backend=vs-bytecode --backend=vm hello.vs red blue
```

检查成功时不会执行脚本。运行时输出 `value=42, argc=2`，退出码为 0。

如果这里失败，请先修复项目或安装环境。CLI 与 LSP 的结果不一致时，通常是模块路径、
宏或绑定目录设置不同。

## 第 2 步：启动语言服务器

让编辑器通过标准输入输出启动相匹配版本的 `veroscript-lsp`。编辑器发送
`initialize`，等待响应后发送 `initialized`，并把项目根目录作为 workspace root。

将下面的字段与工作区、客户端能力一起放入 `initialize` 请求的 `params`。 让语言服务器在练习目录中启动，使 `.` 指向该目录。如果编辑器从其他位置启动服务器，请使用绝对路径，例如 Windows 上的 `C:/work/project`。如果其他项目需要指定单独的标准库目录，请设置 `stdlibPath`。

```json
{
	"locale": "zh-CN",
	"initializationOptions": {
		"veroscript": {
			"importPaths": [
				"."
			]
		}
	}
}
```

如果客户端支持动态注册文件监视，服务器会在 `initialized` 后请求监视
`**/*.vs` 和 `**/*.vslib`。如果不支持，请在客户端配置这些模式，或显式发送
`workspace/didChangeWatchedFiles` 通知。

打开 `.vs` 文档后，服务器会在内存中分析打开的缓冲区，因此未保存的导入也能参与
解析。使用增量 `didChange`；版本较旧的变更会被忽略。

## 第 3 步：体验编辑器能力

打开第 1 步保存的 `hello.vs`。

1. 临时制造一个语法或类型错误，确认诊断已经发布，然后撤销该错误。
2. 将 `std::chrono::now();` 替换为 `std`，把光标放在它后面，调用补全并接受命名空间项；它会插入 `std::`。
   输入 `ch` 并接受 `chrono`，会插入 `chrono::`，随后即可查看其中的成员。最后将这一行补全为
   `std::chrono::now();`，使示例恢复为有效代码。
   只有替换范围后面尚无 `::` 时，补全才会追加它。
3. 将 `Lesson::twice(lessonSeed())` 临时替换为 `Lesson::tw`，然后调用补全。若客户端声明支持 `textDocument.completion.completionItem.snippetSupport`，检查 `twice` 的参数占位符。继续前恢复原调用。
4. 悬停查看命名空间、函数或类型的解析信息。
5. 从 `hello.vs` 中的调用跳转到 `lessonSeed` 的定义，确认打开 `lesson-utils.vs`。在文档符号中找到 `Lesson` 和 `main`，再用 `workspace/symbol` 搜索 `Lesson::twice`，用 `workspaceSymbol/resolve` 解析结果。
6. 解析补全项，查看可用的 Markdown 说明。打开 `import "lesson-utils.vs";` 所在行的链接。
7. 请求语义 token，再使用上一次的 result id 请求语义 token delta。
8. 在 `Lesson` 上准备重命名，将它改为 `Practice`，确认声明和限定调用一起更新。
9. 先格式化文档，再格式化选区，确认编辑覆盖选区所在的完整行。试用 `source.formatDocument`。要试用 `source.organizeImports`，复制一遍导入行，确认该操作移除了重复行。保存两个文件，再运行 `veroscript --check hello.vs`。

泛型类型和可调用项补全在适用的上下文中可以使用 LSP snippet。只有客户端声明支持
`textDocument.completion.completionItem.snippetSupport` 时服务器才会发送 snippet；其他情况插入普通文本。
标准库命名空间可以导航，但不能重命名。
服务器提供完整格式化，以及只删除完全重复导入的保守整理导入操作；二者都不会重写无关源码。

## 第 4 步：加入宿主绑定目录

如果你在操作使用宿主绑定的项目，请完成这一步。如果只使用本课的两个文件，可以直接进入第 5 步。

把项目宿主使用的绑定目录按同样顺序加入 `initializationOptions.veroscript.bindingCatalogPaths`。其中的函数、类型、
成员、注解、泛型模板和文档会参与分析、补全、悬停和符号查询。

服务器运行期间修改绑定目录，下一次请求会重新加载并重新分析打开的文档。绑定目录
只是元数据，宿主仍然必须注册运行时实现。

## 第 5 步：切换诊断语言

在打开文档前把 BCP 47 标签放入 `initialize.locale`：

```json
{
	"locale": "fr-FR"
}
```

用新的 `locale` 重启语言服务器。再次加入第 3 步中的同一个临时错误，比较诊断代码和翻译后的消息。代码保持不变，缺少翻译时使用 `en-US`。调试前修复错误并保存文件。

## 第 6 步：启动调试器

在 DAP 客户端中选择连接方式。由客户端启动适配器时，让它在练习目录中使用下面的命令，通过标准输入输出连接。

```powershell
veroscript --debug hello.vs
```

单独启动客户端时，在练习目录中启动 TCP 适配器，再将客户端连接到 `127.0.0.1:4711`。

```powershell
veroscript --debug --dap-port=4711 hello.vs
```

使用标准输入输出且需要预设脚本参数时，用下面的命令替换第一条命令。

```powershell
veroscript --debug hello.vs red blue
```

在客户端将 `cwd` 设为练习目录，启动 `hello.vs`。在 `print` 所在行设置断点，并通过启动请求的 `args` 传入 `["red", "blue"]`。如果已通过 CLI 预设参数，可以省略 `args`。在断点停下后继续执行，调试输出应显示 `value=42, argc=2`。请求顺序和选项见[调试参考](../../../../../VeroScriptTooling/Resources/Docs/User/zh-CN/debugging.md)。

在 DAP `launch` 或 `attach` 请求中，使用 `program` 或内联 `source`，再通过 `args`
传给 `main(string[] args)`。使用 `input` 提供脚本标准输入，使用 `env` 提供环境变量
覆盖，使用 `cwd` 指定工作目录，使用 `stdlibSearchPaths` 指定标准库搜索根目录。脚本
输入与 DAP 协议输入流相互独立。相对路径以 `cwd` 为基准；未提供工作目录时以
`workspace` 为基准。调试、保留初始化区间和有界清理使用 VM，性能测试应使用普通运行。

DAP `initialize` 参数支持使用 BCP 47 `locale` 指定诊断和错误消息的显示语言。
客户端应根据 DAP 的成功或错误字段进行分支，而不要根据翻译后的消息文本判断。

源代码行断点通过 `setBreakpoints` 设置。不支持异常断点和函数断点；相关请求会
明确失败，因此客户端不应将这两类断点显示为已启用。

`configurationDone` 之后，线协议宿主会先确认请求，再在线程中执行 VM，因此运行期间
仍能处理协议请求。可以对运行中的 VM 发送 `pause` 请求协作式暂停；随后 `stopped` 事件
的原因会是 `pause`。使用 `continue`、`next`、`stepIn` 或 `stepOut` 恢复执行，使用
`terminate` 请求取消。`disconnect` 对 `launch` 默认会结束调试对象，对 `attach` 默认不
会结束；多会话 TCP 宿主中，`terminateDebuggee=false` 会保留监听器。

执行过程中，`std::console::write` 和 `std::console::error` 不会追加换行。
调用 `std::console::flush()` 可以立即显示进度片段。适配器会在进程退出前，
把标准输出和错误输出作为不同的 DAP output 事件转发。

LSP 会话应以 `shutdown` 后跟 `exit` 结束。

## 练习

1. 让 CLI 和 LSP 解析同一个导入模块。
2. 在较新版本后发送旧的 `didChange`，确认较新的缓冲区仍然有效。
3. 加入绑定目录，确认宿主函数出现在补全和悬停中。
4. 切换语言，确认诊断 code 与脚本结果不变。
5. 修改磁盘上的导入模块，发送 `workspace/didChangeWatchedFiles`，确认打开文档的诊断
   已经刷新。

协议详情见[语言服务器参考](../../../../../VeroScriptTooling/Resources/Docs/User/zh-CN/lsp.md)，公共查询层见
[Tooling 模块参考](../../../../../VeroScriptTooling/Resources/Docs/User/zh-CN/index.md)。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/toolchain/17-tooling-workflow.md) · [fr-FR](../../fr-FR/toolchain/17-tooling-workflow.md) · [ja-JP](../../ja-JP/toolchain/17-tooling-workflow.md) · [ko-KR](../../ko-KR/toolchain/17-tooling-workflow.md) · [zh-CN](17-tooling-workflow.md) · [zh-TW](../../zh-TW/toolchain/17-tooling-workflow.md)
