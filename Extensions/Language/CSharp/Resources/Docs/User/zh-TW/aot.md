# C# AOT

`veroscript aot --target=csharp` 會將通過編譯檢查的 VeroScript 程式轉換成 C# 原始碼。
產生的應用程式可使用一般 .NET 建置或 NativeAOT 發布，執行時不需要
`VeroScriptInterop`；它與嵌入用的 `VeroScriptCSharp` SDK 是不同的邊界。

## 單檔輸出

```powershell
veroscript aot --target=csharp app.vs -o Program.cs
```

產物是 C# 原始碼而非執行檔。請將它加入目標為 `net8.0` 或 `net9.0` 的 SDK-style
專案，再使用 `dotnet` 建置；必要時以 `-I`/`--import-path` 增加應用程式模組路徑。

## 原始碼 bundle

```powershell
veroscript aot --target=csharp app.vs --output-dir generated --bundle-name App
dotnet publish generated/App.csproj -c Release -r win-x64 --self-contained true
```

bundle 包含 `App.runtime.g.cs`、`App.program.g.cs`、`App.entry.g.cs`、
`App.csproj` 與 `App.manifest.json`；manifest 記錄產生檔案與編譯單元順序。

<a id="aot-project-inputs"></a>

### 產生專案中的宿主輸入

產生的 `.csproj` 使用 `EnableDefaultCompileItems=false`，只編譯明確列出的 `Compile` 項。原始託管宿主宣告與產生的 AOT bridge 須作為額外專案輸入加入；只將檔案放在專案旁邊不會自動包含。bridge 的 partial class 實作須與產生程式在同一組件編譯。保留 manifest 列出的所有產生單元；即時 VM 註冊原始碼屬於另一種 SDK 接入構件。

重新產生 bundle 會替換產生的專案檔。產生後重新套用額外 `Compile` 項，或在應用程式自行維護的專案引用產生單元與宿主輸入。具體路徑和步驟見[綁定教學](../../../../../../../Resources/Docs/Tutorials/zh-TW/csharp/01-csharp-source-tools.md)。

## 支援範圍與限制

支援標量、字串、`pair`、`auto`、容器、自由函式、無約束 open generic、預設參數、
variadic、overload、`ref`、namespace、`using namespace`、enum、global、import、控制
流程、`switch`、跳轉、`throw`、`try`/`catch`/`finally` 以及
`algorithm`/`numeric`/`random` 原始碼模組。也支援函式值與間接呼叫、表達式/區塊
lambda 與捕獲、腳本類別和結構、建構函式、執行個體/靜態/虛擬方法、單一類別繼承、
參考回傳和區域參考別名。

還支援有號 8 位元 `char` 常值、同型別區域宣告群組、`do`/`while` 與傳統 `for` 的
初始化或步進運算式清單。一般字串和原始字串會保留精確的 VeroScript 位元組序列，
包括由 `\xNN` 引入的孤立位元組；Unicode 換行純量會發射為逸出字元，以免使產生的 C# 常值
或原始碼位置註解失效。

泛型型別參數以及詞法遮蔽的區域變數、欄位、迴圈/catch 繫結和 lambda 參數都會取得衛生的
C# 名稱。初始化器會在新繫結可見前針對先前的 VeroScript 環境發射；即使 C# 否則會將它
繫結到自身或拒絕巢狀，仍會保持 VeroScript 語意。

在開放泛型型別的建構函式欄位初始化清單中，C# 目標僅支援 `T` 到 `T` 的直接初始化。
以零個或多個引數建構無約束欄位型別參數的形式會在 AOT 發射階段被拒絕；請改用具體欄位
型別，或先建構值再將其作為 `T` 傳入。

當 lambda 使用執行個體成員時，捕獲的類別 `this` 會保留接收者並觀察後續欄位修改；
結構的 `this` 則是值快照。巢狀 lambda 會繼續保留已捕獲的接收者。沒有擷取的 lambda
不能使用 `this`。

Binding Catalog v1 中相容的類別、結構、列舉、成員和容器，可透過
`--binding-catalog` 與產生的 AOT bridge 轉為直接託管呼叫，不依賴執行時期反射。

## 明確排除

介面及介面實作、多個具體類別基底、非公開或結構繼承、確定性解構、friend/using-member、
受約束或帶預設值的泛型參數、僅 const 或參考方向不同的多載、預編譯函式體、map 解構、
參考 `foreach`、固定長度原生陣列、宿主運算子/索引器、泛型宿主成員、宿主屬性遞增遞減，
以及需要元素轉換的宿主容器都會被拒絕。工具會回報診斷，而不會靜默改變語意。`concurrent.vs`、`channel<T>`、`task<T>`、`task_group<T>` 尚不支援 C# AOT；
需要 runtime concurrency 或宿主功能時請使用 [C# SDK](index.md)。

支援的容器也包括 `multi_map`、`ordered_multi_map`、`multi_set`、
`ordered_multi_set` 和 `priority_queue`，以及一般的 map、ordered map、set、
queue、stack 與 deque。

C# AOT 的浮點轉整數轉換向零截斷，並在強制轉換前按二進位值檢查目標範圍，保留可表示的 64 位元整數值，不經過十進位捨入。無號目標拒絕負數輸入，包括負小數；NaN、無窮和超出範圍的值均失敗。

<a id="aot-floating-conversions"></a>

C# AOT 的整數轉 float 維持核心經 double 中間值轉換的規則，並涵蓋混合運算與複合賦值。含浮點運算元的比較按 double 進行；浮點取餘與核心一致，回報取模僅對整數有定義的錯誤。

## 執行階段記憶體支援

獨立 AOT 目前不支援 `std::memory` 的查詢、緩衝區、檢視及相關型別。實際呼叫或使用這些型別時，產生器會回報明確錯誤；僅匯入未使用的 `memory.vs` 或 `core.vs` 仍可產生。

[Runtime SDK](index.md)

## 宣告準備與擷取限制

C# 產生使用 Core 的宣告準備流程，包括類別外定義及產生的方法。原生主機透過 `FCSharpCodegenBackend` 產生時，可在 `FCodegenOptions.AnnotationProcessing` 中提供註記處理器；處理器程式碼須在產生期間保持有效。

以值擷取的繫結預設唯讀。`mutable` lambda 對自有擷取的修改會保留至後續呼叫；複製函式值共用同一環境，分別求值建立的閉包擁有獨立環境。以值初始化擷取在外層作用域由左至右求值一次，依普通名稱擷取 const 繫結仍保留唯讀性質。純量區域 `static` 首次使用時初始化，並依宣告共用儲存，包括同一 lambda 運算式建立的不同閉包；泛型實例與產生程式的每次進入點呼叫分別隔離狀態。初始化失敗後可重試，遞迴初始化會報錯。參考初始化擷取、非純量區域 static，以及巢狀 lambda 存取外層函式的區域 static 需要 Core 執行階段路徑，獨立 C# 產生會明確拒絕。

獨立 C# AOT 支援主控台能力查詢、尺寸、顏色、游標和清屏操作。輸出重新導向時不提供控制能力，也不輸出跳脫序列。入口執行邊界恢復 Windows 主控台原始屬性與游標狀態；其他終端在修改樣式或可見性後重設樣式並顯示游標，腳本失敗時同樣執行清理。

C# AOT 對函式簽章中等價的基本型別別名重用同一個產生的委派型別，涵蓋參考參數、參考傳回與巢狀值容器。`long`/`int64` 與 `unsigned long`/`uint64` 保持等價；正負號、位元寬度、物件型別身分與 `ref`/`const ref` 限定仍須相符。

## 使用者 pair 範本

使用者自訂的 `pair<T, U>` 使用一般泛型欄位和方法。此雙參數範本取代軟內建型別時，獨立 AOT 會明確拒絕映射的 `entries()` 和鍵值項目走訪；這些操作請使用 VM 後端。映射值本身是使用者 pair 型別時，一般值走訪仍受支援。

接收選項的檔案產生入口（`FCSharpCodegenBackend.EmitFromFile`、`EmitCSharpSourceBundleFromFile`）透過 Core 模組載入器統一載入入口和匯入模組。不可變模組環境及 `SourceOverlays` 同樣適用於虛擬入口路徑。需要目標執行階段介接器的原生套件契約仍會被獨立輸出明確拒絕。

接收選項的 `EmitCSharpSourceBundleFromProgram` 多載同樣使用模組環境發佈的繫結目錄，並拒絕與之衝突的明確指定目錄。

產生的入口會在 `main` 之前、執行例外狀況邊界之內，依編譯器記錄的順序初始化指令碼靜態資料成員及全域變數。初始化期間的例外狀況與 `exit` 採用與入口執行相同的處理規則。初始化前讀取或參考非 `int` 儲存空間需要 Core 執行階段：其原始零值無法由強型別 C# 欄位忠實表示。啟動檢查會追蹤靜態已知的呼叫與直接可呼叫繫結；此類儲存空間尚未初始化時，無法解析目標的間接啟動呼叫會遭拒絕。僅建立延後呼叫的可呼叫值仍受支援，直線執行中的先寫後讀，以及一般 `int` 零值的向前讀取也仍受支援。

Core 編譯成功後，啟動時函式呼叫與型別初始化圖的巢狀過深時，會傳回要求使用 Core 執行階段的診斷並拒絕產生，
以限制產生器的原生檢查堆疊。一般遞迴與循序啟動呼叫仍受支援。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/aot.md) · [fr-FR](../fr-FR/aot.md) · [ja-JP](../ja-JP/aot.md) · [ko-KR](../ko-KR/aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](aot.md)
