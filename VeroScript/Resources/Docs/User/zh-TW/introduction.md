# VeroScript 概述

VeroScript 是一種以嵌入式場景為導向的跨平台腳本語言。它採用熟悉的 C++ 風格語法，同時擁有獨立的類型系統、模組系統和執行階段。原始檔使用 `.vs` 副檔名。

原生 Runtime 和 SDK 面向 Windows、Linux 和 macOS；瀏覽器及 JavaScript 宿主使用 [WebAssembly SDK](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/zh-TW/index.md)，Android 和 iOS 則使用[實驗性行動適配器](../../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/User/zh-TW/index.md)。各產品可用的元件、執行後端和發行套件因目標與版本而異，安裝和部署需求見[安裝與分發](../../../../../VeroScriptInstaller/Resources/Docs/User/zh-TW/index.md)及相應 SDK 指南。

VeroScript 適合把一部分邏輯從宿主程式中分離出來，讓這部分邏輯能夠獨立開發、檢查、運作和重新載入。遊戲邏輯、編輯器工具、自動化任務和應用程式內擴展，都是典型的使用場景。

要注意的是，VeroScript 不是 C++ 的子集，也不是一個可以直接編譯 C++ 原始檔的解譯器。它借鑑了 C++ 的語法和物件導向模型，但腳本的型別語意、記憶體模型和可用能力由 VeroScript 自己定義。

## 執行模型

腳本從原始碼到執行，大致經過以下步驟：

```text
.vs 原始檔
  -> 預處理
  -> 詞法分析和語法分析
  -> 語意分析與編譯
  -> 位元組碼
  -> VeroScript VM 執行
```

預處理器支援條件編譯、物件巨集和函式巨集、字串化、標記拼接以及檔案包含。編譯階段負責型別檢查、重載解析、泛型實例化和繼承關係檢查等工作。模組透過 `import` 和 `export` 組織，模組載入器會進一步處理模組路徑、隱式匯入、套件依賴和循環相依性。

常規執行以虛擬機器為基礎，預設使用混合後端；在支援的環境中，執行階段可以使用更快的執行後端，並在必要時回退到虛擬機器。 VeroScript 也可以產生位元組碼快取，以減少後續啟動時的編譯開銷。

## 語言範圍

下面的表格用於快速了解 VeroScript 的語言範圍，具體語法和規則應以相應的語言檔案為準。

| 領域 | 主要內容 |
| --- | --- |
| 基礎類型 | 整數、浮點數、布林值、字串、寬字串、枚舉和 `nullptr` |
| 資料結構 | 陣列、映射、集合、佇列、堆疊、雙端佇列、優先權佇列和通道 |
| 類型系統 | `struct`、`class`、`interface`、命名空間、型別別名與存取控制 |
| 物件導向 | 繼承、介面、虛函式、重寫、靜態成員、建構子與析構函式 |
| 泛型與重載 | 泛型類型與函式、`where` 約束、預設型別參數、函式/方法/運算子重載 |
| 函式能力 | 預設參數、可變參數、引用參數、引用傳回、引用別名、函式值、Lambda 和引用擷取 |
| 控制流程 | 條件、循環、`foreach`、`switch`、跳躍、標籤和傳回 |
| 標準函式庫 | 字串、路徑、數學、格式化、檔案、輸入輸出與並發 |

`struct` 用於表達具有值語意的資料；`class` 用於表達具有共享身分和生命週期的資料。介面、繼承、虛擬呼叫和泛型則用於建構可重複使用的腳本抽象化。

請參閱[命令列工具 — 後端選擇](../../../../../VeroScriptCli/Resources/Docs/User/zh-TW/index.md#cli-backend-policy)。

效能測量應使用非偵錯的一般執行，固定輸入和後端，並等待正常暖機後再比較。位元組碼快取只會降低啟動時的編譯成本，不會改變腳本實際執行的工作量。

## 執行腳本

使用命令列工具可以直接執行一個腳本：

```powershell
veroscript hello.vs
```

常用命令如下：

```powershell
# Check without executing
veroscript --check hello.vs

# Start debugging mode
veroscript --debug hello.vs

# Package as a runnable application
veroscript --emit-package app.vsapp hello.vs
```

腳本需要提供 `main` 入口。入口可以傳回 `int`，也可以回傳 `void`。建議使用 `int main`，傳回值會成為處理程序退出碼；`void main` 仍受支援，並按成功退出處理。無參數入口和帶命令列參數的入口分別如下：

```VeroScript
int main()
{
	print("Hello, VeroScript!");
	return 0;
}
```

```VeroScript
int main(string[] args)
{
	print(len(args));
	return 0;
}
```

傳遞給程式的命令列參數可以從 `args` 中讀取；腳本檔案路徑本身不包含在 `args` 中。

VeroScript 支援三種常見檔案形式：`.vsapp` 是可執行的應用程式套件，`.vslib` 是供其他腳本匯入的函式庫套件，`.vsbc` 是位元組碼快取檔案。函式庫套件不能直接作為應用程式運作。

## 嵌入 C++ 宿主程式

VeroScript 的一個主要用途是嵌入 C++ 應用。宿主程式可以載入或編譯腳本，呼叫腳本函式、方法和建構函式，也可以把宿主能力註冊給腳本使用。可註冊的內容包括原生函式、宿主類型、欄位、方法、運算子、泛型類型和類型別名。

`VeroScriptEngine` 提供檢查和執行腳本的高層入口；`VeroScriptSession` 用於需要更細粒度控制模組、綁定和執行階段狀態的整合場景。宿主還可以接收診斷、輸出和偵錯回呼，並查詢或修改腳本中的類型、欄位和全域值。

如果宿主不是 C++，可以使用 `VeroScriptInterop` 的 C11/C++17 ABI。它透過不透明會話和值控制代碼提供跨語言呼叫、宿主綁定、套件、反射 JSON、序列化和回呼能力，不要求適配器依賴 C++ 物件佈局。瀏覽器和 WebAssembly 場景則使用獨立的 `VeroScriptWebAssembly` SDK：它提供 TypeScript 包裝層、語言目錄發現、記憶體中的原始碼檢查/執行和虛擬匯入檔案。

熱重載須明確啟用，預設關閉。函式識別、擷取名稱及型別保持相容時，全域量、物件、容器和閉包（包括擷取的 `this`）可以遷移。活動通道、不相容的型別變更和擷取配置變更仍會被拒絕。宿主原生程式碼的外部副作用不屬於腳本狀態交易。

## 工具鏈與 AOT

語言工具使用與編譯器相同的預處理和語意分析基礎，因此可以提供診斷、程式碼大綱、語意標記、跳躍、懸停、補全、簽名協助、引用查找、重新命名和格式化等功能。偵錯器透過 DAP 工作，偵錯時使用虛擬機器執行路徑。

在不需要動態載入的場景下，可以使用 AOT 後端把腳本產生 C++17 原始檔案，然後交給本機編譯器建置。 AOT 和虛擬機器是兩種不同的執行方式，具體選擇取決於宿主程式的部署和偵錯需求。

## 版本說明

本文文件描述目前公開的語言能力。腳本、位元組碼快取、套件和宿主綁定應使用相互相容的版本。

VeroScript 有意保持獨立的執行階段邊界。目前版本不提供腳本指標語意和獨立的 C++ 概念語法；可變參數巨集的 `__VA_OPT__`、引用擷取、引用回傳和執行階段反射屬於目前公開語言能力，但仍遵循 VeroScript 自己的生命週期與型別規則。圖形介面也不屬於核心執行階段。

完整檔案從[檔案首頁](../../../../../Resources/Docs/User/zh-TW/index.md)開始。語言基本語法與語意見[語言基本特性](language-basics/index.md)，標準函式庫 API 見[標準函式庫參考](standard-library/index.md)，指令列、除錯、AOT、宿主整合、WebAssembly/WASM 和 Interop SDK 見[工具鏈](../../../../../Resources/Docs/User/zh-TW/toolchain/index.md)。

<a id="backend-selection"></a>

## 服務後端選擇

主程式或 CLI 提供已安裝／已部署的模組環境後，未指定的後端先繼承環境的 `services.<service>.backend`；環境也未設定時，`auto` 按後端 ID 升冪嘗試已啟用的候選，選擇首個相容且能成功建立的工廠。明確的 ID、工廠及檔案／目錄優先，失敗不會悄悄改用其他 ID。後端 ID 如 `asmjit`，安裝套件名稱如 `vs-asmjit`，兩者用途不同。

明確的 `auto` 可覆寫環境預設值；`none` 停用服務，設定中明確儲存的空後端值也表示停用。「未設定」與「儲存空值」不同。產品組裝的 VM 僅在自動套件選擇找不到可用解譯器時作為退回選項。沒有可用的選用 JIT 時，Mixed 使用 VM；強制 JIT 仍要求可用且相容的後端。必要服務沒有可用候選時明確回報錯誤。

選擇只發生在工廠建立與準備階段，業務開始後不替換後端，執行錯誤不會重播操作。解譯器執行域在開始執行後仍會固定。配額、取消、有界清理、偵錯與熱重載的執行規則維持不變。工廠和實例保留模組及環境擁有者。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/introduction.md) · [fr-FR](../fr-FR/introduction.md) · [ja-JP](../ja-JP/introduction.md) · [ko-KR](../ko-KR/introduction.md) · [zh-CN](../zh-CN/introduction.md) · [zh-TW](introduction.md)
