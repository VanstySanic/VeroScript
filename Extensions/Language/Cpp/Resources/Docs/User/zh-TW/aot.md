# C++ AOT

AOT 後端將 VeroScript 轉換為用於原生建置的 C++17 原始碼，適合執行階段不需要動態載入腳本的部署情境。

## 命令列產生

```powershell
veroscript aot script.vs -o generated.cpp
```

輸出是 C++ 翻譯單元，不是已經連結的可執行檔。產生結果包含所需的相容支援程式碼；目標專案使用相容的 C++17 工具鏈和所需系統程式庫編譯、連結。使用宿主繫結時，還需提供對應宿主實作。

## 程式設計 API

`VeroScriptCppAot` 提供 `EmitCppFromSource`、`EmitCppFromFile`、從已編譯 `FProgram` 產生的 `EmitCppFromProgram`、產生 `FCppAotSourceBundle` 的 `EmitCppSourceBundleFromProgram`，以及從產生原始碼建立 bundle 的 `BuildCppSourceBundle`。`FCppCodegenBackend` 的名稱為 `cpp`，副檔名為 `.cpp`。檔案寫入是交易式的，來源路徑和輸出路徑不能相同。

bundle 名稱允許 ASCII 字母、數字、`_` 和 `-`。為確保產生的檔案可跨平台使用，所有平台均不區分大小寫地拒絕 Windows 裝置名稱（`CON`、`PRN`、`AUX`、`NUL`、`COM1`–`COM9`、`LPT1`–`LPT9`）；`CON_model`、`COM10` 等名稱仍可使用。

環境未提供繫結目錄時，仍可透過 `FCodegenOptions::Bindings` 明確提供宿主繫結。準備和驗證使用相同目錄；產生的自由函式宣告需要由目標專案提供對應實作。需要執行階段原生套件契約的程式仍會被獨立 AOT 明確拒絕。

## 原始碼 bundle 與邊界

`FCppAotSourceBundle` 包含 `Files` 和 `ManifestJson`。入口 `.cpp` 會把執行階段和程式的 `.generated.inl` 片段重建為一個翻譯單元；manifest 宣告最低 C++ 標準和執行緒庫需求。不要把這些片段作為獨立翻譯單元連結。

腳本頂層全域變數和類別靜態欄位仍按原生工作執行緒隔離。AOT 會在存取器後產生使用函式區域
`thread_local` 狀態的全域儲存。某個工作執行緒第一次存取時，會按腳本宣告順序初始化全域變數；
產生的入口點會在呼叫 `main` 前建立這份狀態。需要在線程間明確分享資料時，應使用通道或同步的宿主狀態。

成員函式中的 lambda 使用接收者時，產生的 C++ 會保持語言語意：類別接收者會作為共享
物件保留，並能觀察後續的欄位修改；結構接收者則會按值捕獲為快照。巢狀 lambda 會
繼續保留已捕獲的接收者。無捕獲 lambda 不能使用 `this`；解構函式中的 lambda 若捕獲
類別接收者會被拒絕，因為接收者已進入解構程序。

如果分析能證明類別區域變數不會逸出其詞法範圍，產生器可能為它使用自動儲存。
回傳物件、取得物件欄位的參考，或會逸出的閉包則會保持共享所有權。這是產生儲存層面的
最佳化，不會改變類別身分、解構行為或語言規定的生命週期。

後端涵蓋類別、結構體、介面、繼承、虛擬派送、泛型、介面約束、容器、重載、預設和可變參數、參照、lambda、列舉、運算子、`nullptr`、`switch`、`foreach`、標籤、跳轉以及文件化的字串、數學、檔案和路徑函式庫。

純量數值的 `++`、`--` 只求值目標一次，操作期間保留暫時所有者，並保持 VM 的數值提升、縮窄檢查與溢位語意。目前不支援使用者型別重載的 `++`、`--`，請對這些型別使用明確的更新方法。

在開放泛型型別的建構函式欄位初始化清單中，C++ 目標僅支援 `T` 到 `T` 的直接初始化。以零個或多個引數建構無約束欄位型別參數的形式會在 AOT 發射階段被拒絕；請改用具體欄位型別，或先建構值再將其作為 `T` 傳入。

VM 更適合動態載入、偵錯和熱重新載入。AOT 產生碼的並行使用宿主執行緒；宿主繫結、回呼生命週期和同步規則必須在目標專案中單獨驗證。

## 目前語法涵蓋範圍

C++ AOT 也會發射有號 8 位元 `char` literal、相同型別的 local declaration group、
`do`/`while`，以及 classic `for` 的 initializer 或 step-expression list。它保留 lexical
shadowing：每個 declaration initializer 都先在新 binding 可見前的 VeroScript environment 中
求值。為保留這項 source rule，產生的 C++ 可以使用 hygiene temporary 或不同的 local name；
這些 implementation name 並非 AOT ABI 的一部分，也不表示 source-level behavior 有改變。

## 執行階段記憶體支援

獨立 AOT 目前不支援 `std::memory` 的查詢、緩衝區、檢視及相關型別。實際呼叫或使用這些型別時，產生器會回報明確錯誤；僅匯入未使用的 `memory.vs` 或 `core.vs` 仍可產生。

[Runtime SDK](index.md)

## 參考回傳與求值生命週期

<a id="reference-return-lifetime"></a>

回傳參考的呼叫用於值讀取時，AOT 會在釋放保存的接收者之前複製該值，無參數呼叫也遵循此規則。參考繫結與值轉換、泛型推導保持分離。數值參考採用與一般值相同的提升、窄化檢查及溢位檢查。透過回傳參考的方法賦值時，先求值接收者及參數，再呼叫方法一次，最後求值右側。

透過直接存取類別欄位，或呼叫直接回傳自身非靜態、非參考欄位的不可覆寫方法，區域別名可保留接收者儲存。接收者、基底類別、欄位及已知衍生類別必須無需腳本清理且不持有不透明資源。重新繫結原接收者不會使該別名失效。一般全域儲存和直接結構體接收者的參考仍受支援。

VM 中保留欄位儲存不會延後腳本解構。因此，需要託管清理的上述類別參考路徑會被 AOT 明確拒絕。回傳區域保留的別名、無法證明來源的參考，或從保留的 callable 儲存中逸出的參考也需要使用 VM 後端；產生器會回報不支援的情況。

長期索引別名和索引參考回傳需要託管儲存路徑。從暫存值接收者取得的長期參考也需要託管擁有者。這些情況會被 AOT 明確拒絕，請使用 VM 後端。

隱含結構體欄位（包括泛型欄位及預設初始化的定長陣列元素）依正常規則選擇預設建構函式並執行其本體。明確的欄位初始化器直接建構欄位值，不先執行預設建構。

## 編譯處理與原始碼中繼資料

透過 `FCodegenOptions::CompileProcessing` 設定原始碼和宣告處理器。準備階段保留跨階段處理器實例；產生已準備程式時不會再次執行處理器。編譯期原始碼中繼資料與產生宣告可用於 C++ AOT。執行時原始碼中繼資料查詢（註解、註釋、宣告／原始碼節點、註記定義）需要 VM，獨立 C++ 產生會明確報錯。

## 指令碼擴充與原始碼覆蓋

`FCppCodegenBackend` 將 `FCodegenOptions::ScriptCompileExtensions`、`SourceOverlays` 及 `StdlibSearchPaths` 傳遞給核心載入器。`EmitFromFile` 使用與 Core/Session 相同的入口覆蓋解析，支援完全來自記憶體的入口；空路徑會被拒絕。`EmitFromSource` 的明確原始碼參數作為入口，覆蓋用於相依模組。

編譯指令碼只在產生期間執行，輸出的業務 C++ 不保留其 VM，也不需要在執行階段提供僅供編譯使用的繫結。業務宿主繫結仍需對應的目標實作。編譯繫結、控制器、限制及相依設定見[宿主整合](embedding.md)；執行階段原始碼中繼資料查詢仍需 VM。

## 使用者 pair 範本

使用者自訂的 `pair<T, U>` 使用一般泛型欄位和方法。此雙參數範本取代軟內建型別時，獨立 AOT 會明確拒絕映射的 `entries()` 和鍵值項目走訪；這些操作請使用 VM 後端。映射值本身是使用者 pair 型別時，一般值走訪仍受支援。

<a id="cleanup-container-operations"></a>

當結構體 `T` 需要腳本清理（包括欄位或基底類別帶來的清理需求）時，獨立 C++ AOT 會明確拒絕 `array<T>.remove` / `remove_at` 以及 `map<K,T>` / `ordered_map<K,T>` 的索引賦值。此限制針對上述操作；VM、Mixed 和 JIT 完整支援這些操作。

獨立 C++ AOT 支援在成功的建構過程中保留或向外發佈該類別物件的 `this`。如果建構失敗且其他參考仍保留該物件，產生的程式會回報致命錯誤，避免腳本從 catch 繼續執行時子物件尚未清理。如需對此情況進行託管復原，請使用 VM 或 Core AOT 後端。例外展開期間已釋放的暫時參考不會觸發此限制。

<a id="standalone-file-io"></a>

受支援的獨立 C++ 檔案操作接受 UTF-8 路徑並拒絕內嵌 NUL。Windows 長絕對路徑（包括完整 UNC）使用原生長路徑處理；相對路徑仍保持相對，不會自動增加延伸路徑前綴。檔案系統、權限和作業系統限制仍適用。這與 [基礎參考](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/foundation.md)中的詞法路徑查詢規則分開。

將容器元素作為引用實參也需要 Core 維護元素身分。獨立 C++ 原始碼 AOT 會明確拒絕這類呼叫，避免引用隨容器重新綁定，或在後續實參執行後才接受原本不存在的元素。此類呼叫請使用 Core AOT、VM、Mixed 或 JIT；一般索引讀取和賦值繼續支援。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/aot.md) · [fr-FR](../fr-FR/aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](../ko-KR/aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](aot.md)
