# 16：WebAssembly SDK 互動式輸入

前提：在支援 ES module 與頂層 await 的 TypeScript 專案，使用同一 SDK 的 wrapper 和一般版 `veroscript-engine.mjs`／`.wasm`。將 wrapper 與本例一同編譯，保留 JavaScript 與引擎檔案的有效相對 URL，透過 HTTP 提供瀏覽器應用程式。範例匯入編譯得到的 `veroscript-webassembly.js`，請依專案調整路徑。參閱 [WebAssembly 部署指南](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/zh-TW/index.md)。

對於會呼叫 `read_line`、字串/token 輸入或 `pause` 的腳本，請使用
`runSourceInteractive`。前端會在保留 VM 完整腳本狀態的同時，
以非同步方式逐行提供輸入。

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

預期結果：`result.success` 為 `true`，`result.output` 為 `Ada\n`，`result.exitCode` 為 `3`。輸入提供者依序給出 `Ada`、`3` 兩行；結束碼 3 不代表引擎失敗。先使用固定輸入完成可重現操作，再接上終端 UI。

當到達輸入結束時回傳 `null`（EOF）。在輸入待定時，請勿呼叫
`dispose` 或其他 Runtime 操作。若要停止可能不受限的執行，建議將
VM 放進 Worker 中，並終止該 Worker。

練習：將 `lines` 陣列改為你的終端 UI 所提供的 `Promise` 版 `readLine`。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/toolchain/16-web-interactive-input.md) · [fr-FR](../../fr-FR/toolchain/16-web-interactive-input.md) · [ja-JP](../../ja-JP/toolchain/16-web-interactive-input.md) · [ko-KR](../../ko-KR/toolchain/16-web-interactive-input.md) · [zh-CN](../../zh-CN/toolchain/16-web-interactive-input.md) · [zh-TW](16-web-interactive-input.md)
