# C# SDK

C# ソース生成、bundle、NativeAOT の境界については [C# AOT](aot.md) も参照してください。

`VeroScriptCSharp` は安定した VeroScript Interop C ABI を利用する NativeAOT 対応 .NET アダプターです。マネージドセッション、値、callback、ホストバインディング、パッケージ、リフレクション、ローカライズ、JIT 統計を提供します。マネージドパッケージは .NET 8 と .NET 9 を対象とし、ネイティブ `VeroScriptInterop` は別途配置してプロセスアーキテクチャを一致させます。

現在の Interop ABI は `0x00000101`（ABI 0.1.1）です。マネージド アダプターは ABI バージョンの完全一致を要求します。セッションを作成する前に `VeroScriptRuntime.AbiVersion` を確認し、対応するリリースのマネージド パッケージとネイティブ Interop ライブラリを配置してください。

```CSharp
using System;
using VeroScript;

using var session = new VeroScriptSession(new()
{
	Print = Console.WriteLine,
	Diagnostic = Console.Error.WriteLine,
});
session.ApplyConfiguration(new VeroScriptConfiguration
{
	ExecutionBackend = VeroScriptExecutionBackend.Mixed,
});

var loaded = session.LoadSource("""
    int Add(int left, int right)
    {
        return left + right;
    }

    int main()
    {
        print(Add(20, 22));
        return 0;
    }
    """, "main.vs");
if (!loaded.Success)
{
	throw new VeroScriptException(VeroScriptStatus.EngineError, "load failed");
}

var run = session.Run();
if (!run.Success)
{
	throw new VeroScriptException(VeroScriptStatus.EngineError, "execution failed");
}
```

`VeroScriptSessionOptions.Print` はスクリプトの出力行を、`VeroScriptSessionOptions.Diagnostic` は診断行を受け取ります。どちらも省略可能で、セッションを破棄するまで参照が保持されます。

[セッション](session.md)、[値と所有権](values.md)、[ホストバインディング](host-bindings.md)、[パッケージとリフレクション](packages.md)、[C# AOT](aot.md)を参照してください。

[C# フロントエンド](frontend.md) と [C# BindGen](bindgen.md) では宣言解析、
Catalog、AOT bridge を説明します。

[C# トランスパイラー](transpiler.md) では、対応する C# サブセットから
VeroScript source への意味変換を説明します。

- [Android と iOS / MAUI](mobile.md)

## 管理メモリ

ランタイムには `VeroScriptCapabilities.Memory` が必要です。対応する旧版ランタイムでは、ネイティブ呼び出しや固定処理の前に `NotSupportedException` が発生します。コールバック内では `context.Memory`、アイドル中は `session.Memory` を使います。独自の `Memory<byte>` マネージャーは `Pin` をサポートし、最後の参照を解放するスレッドで `Unpin` を実行できる必要があります。登録に失敗した場合は固定を解除します。

コールバックのメモリ操作と `GetArgumentAddress` は、そのコールバックのマネージドスレッドで実行する必要があります。別のスレッドからのアクセスは `WrongThread` になります。リースは使用終了後に別のスレッドで解放できます。

ホスト API：`session.Memory` / `SetMemoryLimits`。所有権、上限、ビューの失効、リースについては [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/memory.md) を参照してください。

- [ホストから使う管理メモリ](memory.md)

## ソースツールの診断言語

C# フロントエンドと C# BindGen／Transpiler 固有のヘルプと診断は現在英語で、Roslyn 診断もインバリアントカルチャで取得します。BindGen／Transpiler の CLI は `--lang` を受け付けません。`--language-version` は表示言語ではなく C# の構文バージョンを選択します。`VeroScriptRuntime.CurrentLanguage` によるネイティブランタイムの診断言語設定とは独立しています。

## variant

[相互運用 SDK — variant](../../../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md#exported-value-lifetime) を参照してください。

```csharp
using var payload = new VeroScriptValue();
payload.Set(42);
using var box = session.CreateVariant("int", payload);
string type = box.VariantTypeName;
using var extracted = box.GetVariantValue();
```

`VeroScriptSession.LoadApplication(absolutePath, optionsJson)` でアプリケーションを読み込み、結果を確認してから `Run()` を呼び出します。ホストオプション内のパスは絶対パスにしてください。環境、署名、信頼の設定は [Application の読み込み規約](../../../../../../../VeroScriptApplication/Resources/Docs/User/ja-JP/index.md)に従ってください。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
