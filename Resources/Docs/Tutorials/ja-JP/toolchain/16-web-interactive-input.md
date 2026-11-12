# 16: WebAssembly SDK のインタラクティブ入力

前提: 同じ SDK の TypeScript wrapper と通常版 `veroscript-engine.mjs`/`.wasm` を、ES module と top-level await が使えるプロジェクトに配置します。wrapper と本例を一緒にコンパイルし、JavaScript と engine の相対 URL を保って HTTP 配信します。例の import は生成された `veroscript-webassembly.js` です。プロジェクトに合わせて変更してください。[WebAssembly 配置ガイド](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/ja-JP/index.md)も参照してください。

`read_line`、入力文字列（トークン）、または `pause` を使うスクリプトでは
`runSourceInteractive` を使用します。フロントエンドは、VM の完全な状態を保持したまま、
1 行ごとに非同期で入力を供給します。

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

期待結果は `result.success` が `true`、`result.output` が `Ada\n`、`result.exitCode` が `3` です。入力は `Ada` と `3` の二行で、終了値 3 はエンジン失敗を意味しません。まず固定入力で確認してから端末 UI につなぎます。

EOF（入力終了）は `null` を返します。入力待機中に `dispose` を含む別の
ランタイム操作を呼び出さないでください。上限のない実行を止めるには、VM を
Worker で実行し、その Worker を終了させるのが有効です。

練習: `lines` 配列を、あなたの端末 UI の `Promise` 付き `readLine` に置き換えてみましょう。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/toolchain/16-web-interactive-input.md) · [fr-FR](../../fr-FR/toolchain/16-web-interactive-input.md) · [ja-JP](16-web-interactive-input.md) · [ko-KR](../../ko-KR/toolchain/16-web-interactive-input.md) · [zh-CN](../../zh-CN/toolchain/16-web-interactive-input.md) · [zh-TW](../../zh-TW/toolchain/16-web-interactive-input.md)
