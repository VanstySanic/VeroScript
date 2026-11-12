# VeroScript C# 技術文件

本目錄是 C# 語言組共享的開發者文件，與 C# 原始碼、Interop 頭、CMake 目標和託管測試繫結；它不是終端使用者教程，也不復制公共 API XML 文件。

## 模組歸屬

- `VeroScriptCSharp`：相容 NativeAOT 的 .NET 8/9 執行時介面，基於 `VeroScriptInterop` 提供會話、值、包、回撥、宿主繫結、容器、反射和外部序列化。
- `VeroScriptCSharpAot`：提供 C# 源及面向 NativeAOT 的發射器介面，直接依賴語言核心。
- `VeroScriptCSharpBindGen`：讀取支援的 C# 宣告，發射 Binding Catalog 後設資料和麵向註冊的生成程式碼。
- `VeroScriptCSharpFrontend`：為 C# 構建期工具提供共享的語義宣告模型。

託管介面卡必須使用穩定 C ABI，不能包含 C++ 私有頭或在託管程式碼中復刻編譯器/執行時行為。

## ABI 與生命週期規則

Interop 控制代碼是不透明所有權物件。每個 lease、buffer、包、可呼叫物件、回撥註冊和會話操作都必須遵循 `VeroScriptInterop.h` 及託管包裝器映象的生命週期規則；不得在規定期限外暴露指標、借用字串或回撥上下文。包裝器負責 UTF-8/UTF-16、buffer、狀態碼、診斷和回撥異常的準確編組，可改善易用性但不得將 ABI 錯誤改為成功或悄悄丟棄失敗。

## 前端與生成器邊界

前端不執行使用者程式碼，僅解析宣告。BindGen 消費前端模型併發射後設資料，不建立原生呼叫 thunk；不支援的簽名、所有權模式、預設引數和繼承形式必須在替換生成輸出前給出源診斷。AOT 是獨立降級路徑，其可接受語言和輸出契約以發射器與測試為準。

## 相容性規則

受檢查 API 契約與包驗證測試管理公共型別、編組佈局、enum、回撥呼叫約定、包後設資料和目標框架的變更；版本、ABI 標識、JSON 鍵、CLI 選項和 C# 識別符號均為不翻譯的技術 token。

## 託管執行時細節

`VeroScriptRuntime` 是程序級相容性閘門；在使用會話前呼叫 `EnsureCompatible()`。`VeroScriptSession` 擁有原生控制代碼和託管根；`VeroScriptValue` 可能是擁有控制代碼或借用檢視，借用值只在 borrow scope 有效，容器列舉在原生返回後報告捕獲的回撥異常。`VeroScriptCallable` 的建立執行緒和活躍呼叫釋放規則只保證生命週期，不授權跨執行緒呼叫。`VeroScriptPackage` 公開後設資料、規範簽名位元組和可載入模組狀態。

## 繫結與生成程式碼細節

宿主繫結保留文件、註解、型別引數、引數後設資料、enum 底層型別、const/mutable 標誌、運算子、clone/相等/雜湊協議、子類工廠和泛型宿主型別回撥；生成註冊是後設資料加回撥，而非函式指標表。

## 詳細頁面

- [C# 執行時與 Interop](runtime-and-interop.md)
- [C# 繫結、包與相容性](bindings-and-packages.md)
- [C# 工具鏈與包契約](toolchain-and-packaging.md)

## 託管執行契約

`VeroScriptConfiguration` 包括執行深度、`MaxInstructions`、`TimeoutMilliseconds`、執行控制、能力策略與 `VeroScriptExecutionBackend`；零指令/超時不限。`RequestCancellation` 是唯一可由非所有者執行緒呼叫的會話操作，且會話必須存活到其返回。`VeroScriptJitPolicy`、`GetLastJitStats`、`SetJitCacheLimits` 與 `GetJitCacheStats` 公開自適應編譯、最近執行和可複用會話快取統計；當宣告 `SessionJitCache` 能力時，快取資源有界。不支援的入口回退到 VM。偵錯器附加時和初始化保留狀態時使用 VM，同一世代內符合條件的呼叫可重用已編譯程式碼。

## 依據

以 `VeroScriptCSharp/`、前端/BindGen 專案、Interop 公共頭、CMake 和託管測試為依據。ABI、所有權、執行緒、生成程式碼或包契約變更必須同步更新文件。

`VeroScriptCSharp`: [Android 與 iOS / MAUI](../../User/zh-TW/mobile.md).

行動封裝仍使用同一專案與套件識別名稱：`VeroScriptIncludeMobile=true` 將桌面 .NET 8/9 與 .NET 10 Android/iOS 目標及已驗證的原生 SDK 一起封裝。


[Android/iOS 中使用 CSharp](mobile.md)

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
