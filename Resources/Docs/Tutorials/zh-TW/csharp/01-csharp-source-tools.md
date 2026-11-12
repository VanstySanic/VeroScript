# 01：產生 C# 綁定構件

從同一組託管宣告產生綁定目錄、API 參考、即時註冊原始碼與 AOT bridge。語意前端在建置時分析原始碼，不建立腳本 Session。

準備可從 PATH 呼叫的相符版本 `veroscript-csharp-bindgen`、`veroscript`，可寫的教學目錄，以及與所選 SDK 相容的 .NET 宿主專案。所有命令從該目錄執行。產生建置輸入、編譯宿主及註冊回呼是獨立步驟。

下方 AOT 執行使用產生的 `net8.0` 專案；請安裝能建置此目標的 .NET SDK，以及執行所需的相符 .NET 執行階段。

## 第一步：準備輸入

將宿主宣告存成 `HostApi.cs`：

```csharp
public sealed class HostApi
{
	public static int Add(int left, int right)
	{
		return left + right;
	}
}
```

將呼叫腳本存成 `app.vs`：

```VeroScript
int main()
{
	print(HostApi.Add(20, 22));
	return 0;
}
```

## 第二步：產生即時註冊

```powershell
veroscript-csharp-bindgen HostApi.cs `
	--catalog generated/bindings.json `
	--registration generated/Bindings.g.cs `
	--api-reference generated/Bindings.vs.txt
veroscript bindings validate generated/bindings.json
```

預期取得 `bindings.json`、`Bindings.g.cs`、`Bindings.vs.txt`。將註冊原始碼與託管 C# SDK、原始 `HostApi.cs` 一同編譯，載入腳本前呼叫產生的註冊入口。目錄只描述介面，不會安裝執行期回呼。設定完成的宿主執行該腳本時應印出 `42` 並返回零。

在設定好的 VM 宿主中，載入 `app.vs` 前呼叫以下產生入口；`session` 是宿主現有的 `VeroScriptSession`：

```csharp
VeroScript.GeneratedBindings.VeroScriptCSharpBindings.Register(session);
```

## 第三步：產生 AOT bridge

```powershell
veroscript-csharp-bindgen HostApi.cs `
	--catalog generated/bindings.json `
	--aot-bridge generated/Bindings.Aot.g.cs
veroscript aot --target=csharp app.vs `
	--binding-catalog generated/bindings.json `
	--output-dir generated/app --bundle-name App
```

將 `HostApi.cs`、`Bindings.Aot.g.cs` 與所有產生的應用程式單元放在同一 .NET 專案編譯。bridge 直接呼叫託管程式碼，不載入 `VeroScriptInterop`。保留產生專案的執行期依賴；只產生原始碼尚未取得執行檔。

產生的 `generated/app/App.csproj` 設定 `EnableDefaultCompileItems=false`，只明確列出產生單元。保留現有項目，將以下 `ItemGroup` 加入其 `Project` 元素內。路徑以 `App.csproj` 所在目錄為基準：

```xml
<ItemGroup>
	<Compile Include="../../HostApi.cs" />
	<Compile Include="../Bindings.Aot.g.cs" />
</ItemGroup>
```

從教學工作目錄執行託管應用程式。預期印出 `42`，結束碼為零：

```powershell
dotnet run --project generated/app/App.csproj --configuration Release
```

此 AOT 專案使用 `Bindings.Aot.g.cs`；`Bindings.g.cs` 屬於第二步的 VM 宿主。重新產生 bundle 會替換 `App.csproj`，產生後須重新加入這些額外輸入。

## 第四步：產生專案

建立 `Models/State.cs` 並寫入以下宣告，同時保留第一步的 `HostApi.cs`：

```csharp
public enum HostState
{
	Ready,
	Done
}
```

```powershell
veroscript-csharp-bindgen HostApi.cs Models/State.cs `
	--catalog generated/bindings.json `
	--registration generated/Bindings.g.cs `
	--aot-bridge generated/Bindings.Aot.g.cs `
	--api-reference-dir generated/api `
	--manifest generated/bindings.manifest.json
```

一次傳入兩份檔案，保留同一輪的目錄、註冊、bridge、參考與 manifest。partial 與相關宣告在輸出前合併。同名檔案主幹以穩定路徑雜湊後綴區分。

若在此專案產生步驟後重新建置 AOT 應用程式，先以新合併目錄重複第三步的 AOT 命令，再重新加入專案輸入，包括 `<Compile Include="../../Models/State.cs" />`，然後建置。目錄、bridge、宿主宣告與產生應用程式必須來自同一組輸入。

## 第五步：檢查契約

對照目錄名稱、簽章、宿主原始碼及產生的 API 參考。預設考慮公開宣告；定義並使用 `[VeroScriptBinding]` 後，以 `--require-attribute` 選擇標記型別，使用 `[VeroScriptIgnore]` 排除宣告。不支援的 nullable、指標或配接器簽章會回報診斷；應提供明確包裝，不編輯產生的中繼資料來隱藏不一致。

## 練習

1. 加入受支援的唯讀屬性並檢查契約。
2. 定義綁定屬性標記一個型別，比較 `--require-attribute` 輸出。
3. 說明即時註冊與 AOT bridge 分別由何種宿主使用。
4. 加入不支援的 nullable 簽章，閱讀診斷後恢復受支援介面。

[BindGen 參考](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-TW/bindgen.md) · [C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-TW/aot.md) · [C# 原始碼轉換](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-TW/transpiler.md) · [執行期值與所有權](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-TW/index.md)

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/csharp/01-csharp-source-tools.md) · [fr-FR](../../fr-FR/csharp/01-csharp-source-tools.md) · [ja-JP](../../ja-JP/csharp/01-csharp-source-tools.md) · [ko-KR](../../ko-KR/csharp/01-csharp-source-tools.md) · [zh-CN](../../zh-CN/csharp/01-csharp-source-tools.md) · [zh-TW](01-csharp-source-tools.md)
