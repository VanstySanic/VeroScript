# 16：WebAssembly SDK 交互式输入

前提：在支持 ES module 与顶层 await 的 TypeScript 项目中使用同一 SDK 的 wrapper 和普通版 `veroscript-engine.mjs`／`.wasm`。将 wrapper 与本例一同编译，保留 JavaScript 与引擎文件的有效相对 URL，通过 HTTP 提供浏览器应用。示例导入编译得到的 `veroscript-webassembly.js`，请按项目调整路径。参见 [WebAssembly 部署指南](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/zh-CN/index.md)。

对会调用 `read_line`、`token` 输入（逐行输入）或 `pause` 的脚本，请使用
`runSourceInteractive`。前端可以在保持 VM 完整脚本状态的前提下，
异步提供单行输入。

```typescript
import { createVeroScriptRuntime } from "./veroscript-webassembly.js";

const runtime = await createVeroScriptRuntime();
try {
	const lines = ["Ada", "3"];
	const result = await runtime.runSourceInteractive(
		`import "io.vs";
int main()
{
	print(std::io::read_line());
	return std::io::read_int();
}`,
		{ async readLine() { return lines.shift() ?? null; } },
	);
	console.log(result.output, result.exitCode);
}
finally
{
	runtime.dispose();
}
```

预期结果：`result.success` 为 `true`，`result.output` 为 `Ada\n`，`result.exitCode` 为 `3`。输入提供者依次给出 `Ada`、`3` 两行；退出码 3 不代表引擎失败。先用固定输入完成可复现的操作，再接入终端 UI。

当到达输入结束时返回 `null`（EOF）。输入尚未完成时，不要调用
`dispose` 或其他运行时操作。若要终止可能无限运行的脚本，建议将
VM 放到 Worker 中并终止该 Worker。

练习：将 `lines` 数组替换为你自己的终端 UI 提供的 `Promise`。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/toolchain/16-web-interactive-input.md) · [fr-FR](../../fr-FR/toolchain/16-web-interactive-input.md) · [ja-JP](../../ja-JP/toolchain/16-web-interactive-input.md) · [ko-KR](../../ko-KR/toolchain/16-web-interactive-input.md) · [zh-CN](16-web-interactive-input.md) · [zh-TW](../../zh-TW/toolchain/16-web-interactive-input.md)
