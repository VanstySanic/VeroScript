# C# 繫結、包與相容性

## 宿主繫結模型

託管定義表示宿主函式、宿主型別、成員、運算子、註解、enum 值、文件、型別引數和協議回撥。宿主型別協議可包含 clone、相等性、雜湊和子類工廠。泛型宿主型別模板經 Interop 回撥契約執行個體化，且必須保留目錄使用的型別身份。

生成目錄是後設資料。註冊回撥負責連線真實託管物件和函式；僅有目錄不會呼叫 C# 方法。

## 包

包構建選項包括包種類、名稱、版本、宿主後設資料和除錯資訊策略。包從位元組讀取，公開後設資料和規範簽名位元組，並需要顯式釋放。只有會話接受該包及其種類/入口形狀時，載入才會消費已編譯模組。

## API 相容性

經過檢查的 API 契約是公共託管型別、建構函式、屬性、enum 值、回撥簽名和目標框架的相容邊界。必須結合 `VeroScriptInterop.h` 審查原生布局變更。只有託管測試、公共 API 契約、包驗證和原生 ABI 消費者都保持一致，包裝器修改才算完成。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/bindings-and-packages.md) · [fr-FR](../fr-FR/bindings-and-packages.md) · [ja-JP](../ja-JP/bindings-and-packages.md) · [ko-KR](../ko-KR/bindings-and-packages.md) · [zh-CN](../zh-CN/bindings-and-packages.md) · [zh-TW](bindings-and-packages.md)
