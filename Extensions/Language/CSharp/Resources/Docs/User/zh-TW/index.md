# C# SDK

C# 原始碼產生、bundle 與 NativeAOT 邊界請參閱 [C# AOT](aot.md)。

`VeroScriptCSharp` 是面向 .NET、支援 NativeAOT 的 VeroScript Interop C ABI 配接器，提供工作階段、值、回呼、宿主繫結、套件、反射、在地化和 JIT 統計。託管套件以 .NET 8 和 .NET 9 為目標；原生 `VeroScriptInterop` 是分開部署的受信任輸入，必須與處理程序架構相符。

目前 Interop ABI 為 `0x00000101`（ABI 0.1.1）。託管配接器要求 ABI 版本完全一致。建立工作階段前可檢查 `VeroScriptRuntime.AbiVersion`；託管套件與原生 Interop 應來自匹配的發佈版本。

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

`VeroScriptSessionOptions.Print` 接收指令碼輸出行，`VeroScriptSessionOptions.Diagnostic` 接收診斷行。兩個回呼皆可省略，其受控參考會保留至工作階段釋放。

請參閱[工作階段與執行時期](session.md)、[值與所有權](values.md)、[宿主繫結](host-bindings.md)、[套件、反射與在地化](packages.md)以及[C# AOT](aot.md)。

另請參閱[C# 前端](frontend.md)與[C# BindGen](bindgen.md)，了解宣告分析、
Catalog、註冊程式碼和 AOT bridge。

另請參閱[C# 轉換器](transpiler.md)，了解將支援的 C# 子集進行語意轉換並產生
VeroScript 原始碼。

- [Android 與 iOS / MAUI](mobile.md)

## 受管理記憶體

執行階段須提供 `VeroScriptCapabilities.Memory`；舊版相容執行階段會在原生呼叫或固定記憶體前擲回 `NotSupportedException`。回呼內使用 `context.Memory`，工作階段閒置時使用 `session.Memory`。自訂 `Memory<byte>` 管理器必須支援 `Pin`，並允許在最後釋放參考的執行緒執行 `Unpin`；註冊失敗時會解除固定記憶體。

回呼記憶體操作和 `GetArgumentAddress` 必須在該回呼的受控執行緒執行；跨執行緒存取會回報 `WrongThread`。租約使用完畢後可在其他執行緒釋放。

宿主介面：`session.Memory` / `SetMemoryLimits`。所有權、配額、檢視失效與租借規則請參閱 [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/memory.md)。

- [宿主中的受管理記憶體](memory.md)

## 原始碼工具的診斷語言

C# 前端及 C# BindGen/Transpiler 自身的說明與診斷目前使用英文，Roslyn 診斷按不因文化特性而異的設定讀取。BindGen/Transpiler CLI 不接受 `--lang`；`--language-version` 選擇 C# 語法版本，而非顯示語言。透過 `VeroScriptRuntime.CurrentLanguage` 選擇的原生執行時期診斷語言是另一項獨立設定。

## variant

請參閱[Interop SDK — variant](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md#exported-value-lifetime)。

```csharp
using var payload = new VeroScriptValue();
payload.Set(42);
using var box = session.CreateVariant("int", payload);
string type = box.VariantTypeName;
using var extracted = box.GetVariantValue();
```

使用 `VeroScriptSession.LoadApplication(absolutePath, optionsJson)` 載入應用程式，確認結果後再呼叫 `Run()`。主程式選項中的路徑必須為絕對路徑。環境、簽章及信任設定遵循 [Application 載入約定](../../../../../../../VeroScriptApplication/Resources/Docs/User/zh-TW/index.md)。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
