# 16: Interactive input in the WebAssembly SDK

Prerequisites: use the matching SDK TypeScript wrapper and normal `veroscript-engine.mjs`/`.wasm` pair in a TypeScript project with ES modules and top-level await. Compile the wrapper and this lesson together, deploy their JavaScript and engine files with working relative URLs, and serve the browser application over HTTP. The example imports the emitted `veroscript-webassembly.js`; adjust that path to your project. See the [WebAssembly deployment guide](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/en-US/index.md).

Use `runSourceInteractive` for scripts that call `read_line`, typed/token input,
or `pause`. The frontend supplies one logical line asynchronously while the VM
keeps the complete script state.

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

Expected result: `result.success` is `true`, `result.output` is `Ada\n` and `result.exitCode` is `3`. The input provider supplies two lines, `Ada` and `3`; an integer exit code of 3 is not an engine failure. This fixed input makes the first run repeatable before connecting a terminal UI.

Return `null` for EOF. Do not call another runtime operation or `dispose` while
input is pending. Put the runtime in a Worker and terminate that Worker to stop
an unbounded script. Exercise: replace the array with your terminal UI's Promise.

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](16-web-interactive-input.md) · [fr-FR](../../fr-FR/toolchain/16-web-interactive-input.md) · [ja-JP](../../ja-JP/toolchain/16-web-interactive-input.md) · [ko-KR](../../ko-KR/toolchain/16-web-interactive-input.md) · [zh-CN](../../zh-CN/toolchain/16-web-interactive-input.md) · [zh-TW](../../zh-TW/toolchain/16-web-interactive-input.md)
