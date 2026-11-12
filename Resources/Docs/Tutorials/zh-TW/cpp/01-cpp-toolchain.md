# 18：選擇和使用 C++ 工具鏈模組

本教學完成一個原生整合流程：依照任務選擇嵌入、繫結產生、C++ 到 VeroScript 的轉換或 AOT，並執行最小的正確流程。

前提：相符的 `veroscript`、Cpp BindGen 和 Transpiler 工具可從執行檔搜尋路徑呼叫，且工作目錄可寫。下文路徑表示你提供的輸入：準備含支援宣告的 `Include/Types.hpp`、`Include/Api.hpp`，原始碼定義 `Source/Types.cpp`、`Source/Game.cpp` 及有效的 `script.vs`，或將所有路徑替換為既有檔案。命令從該工作目錄執行。預期取得合併的 `bindings.json`、API 參考及清單，帶原始碼對映的 VeroScript 模組，以及 AOT 步驟的 `generated.cpp`；產生這些檔案不會註冊執行階段宿主回呼。

## 第一步：選擇邊界

先閱讀 [C++ SDK 文件](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/index.md)：長期工作階段和腳本呼叫使用 `VeroScriptCpp`；從 C++ 宣告產生繫結目錄使用 `VeroScriptCppBindGen`；將支援的 C++ 原始碼轉換為 VeroScript 模組使用 `VeroScriptCppTranspiler`；將 VeroScript 轉為原生建置所需的 C++17 原始碼使用 `VeroScriptCppAot` 或 `veroscript aot`。

BindGen 描述宿主 API，Transpiler 轉換原始碼行為，AOT 將 VeroScript 轉換為 C++；三者方向不同，不能互相取代。

## 第二步：產生並校驗繫結目錄

```powershell
VeroScriptCppBindGen Include/Types.hpp Include/Api.hpp `
  --catalog build/bindings.json `
  --api-reference-dir build/api `
  --manifest build/bindings.manifest.json
```

繫結目錄供編譯器和工具使用，API 參考供人閱讀；它不會替你註冊回呼。宿主完成對應註冊後，校驗目錄並檢查腳本：

```powershell
veroscript bindings validate build/bindings.json
veroscript --check script.vs --binding-catalog build/bindings.json
```

多個輸入應使用專案模式，在產生輸出前合併宣告：

```powershell
VeroScriptCppTranspiler Source/Types.cpp Source/Game.cpp --output-dir build/generated --manifest build/generated/transpile.manifest.json
```

遇到不支援的宣告時，應撰寫明確的宿主包裝，而不是修改目錄來掩蓋指標或所有權問題。

## 第三步：轉換原始碼並產生 AOT

```powershell
VeroScriptCppTranspiler Source/Types.cpp `
  -o build/Types.vs `
  --source-map build/Types.vs.map.json

veroscript aot script.vs -o build/generated.cpp
```

多個輸入應使用專案模式，並把 manifest 和原始碼對映與產生原始碼一起保存。AOT 輸出是 C++17 原始碼而不是可執行檔；請在目標專案中驗證執行階段連結、回呼生命週期和宿主狀態同步。

需要確定性的多檔案 C++ 產物時，使用 `FCppAotSourceBundle` API，並將入口原始碼、產生片段及清單一起保存。這些檔案是目標 C++ 專案的建置輸入，不是已建置的可執行檔。

產生的頂層全域變數和類別靜態欄位仍按原生工作執行緒隔離。AOT 會在 `main` 之前按宣告順序
初始化每個工作執行緒的全域存取器。不要把全域變數當作隱式的跨執行緒分享機制；需要分享時，
應使用通道或同步的宿主狀態。

當成員 Lambda 使用接收者時，AOT 會保留原始型別的語意：類別接收者仍然是存活的
保留物件，結構接收者則是值快照。巢狀 Lambda 會保留已經擷取的接收者，沒有擷取
的 Lambda 不能使用 `this`。C++ AOT 也會拒絕在解構函式本體中擷取類別接收者，
因為此時接收者已經進入解構流程。

## 練習

1. 將一個目錄項目與對應的 C++ 註冊程式碼對照。
2. 用專案模式轉換兩個 C++ 輸入並查看 manifest。
3. 為使用 channel 的腳本產生 AOT，並寫下需要同步的宿主狀態。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/cpp/01-cpp-toolchain.md) · [fr-FR](../../fr-FR/cpp/01-cpp-toolchain.md) · [ja-JP](../../ja-JP/cpp/01-cpp-toolchain.md) · [ko-KR](../../ko-KR/cpp/01-cpp-toolchain.md) · [zh-CN](../../zh-CN/cpp/01-cpp-toolchain.md) · [zh-TW](01-cpp-toolchain.md)
