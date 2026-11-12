# 範例索引

範例檔案用於驗證語言和執行階段行為，也可以作為編寫檔案時的可執行交叉引用；範例不是 API 規範，具體簽章、錯誤條件和相容性要求仍以語言參考、標準庫參考和工具鏈參考為準。範例檔案透過 Resources 或原始碼儲存庫提供，使用時請將檔案名稱替換為實際位置。

執行範例：

```powershell
veroscript hello.vs
veroscript --check reflection.vs
```

[屬性與方法函式值](standard-library/attribute.md)：`attribute_demo.vs`、`attribute_functions.vs`、`attribute_methods.vs`、`attribute_weak.vs`。

## 執行後端、資源與權限

存在已安裝模組環境時，省略 `--jit-backend` 會繼承其 JIT 選擇，未設定時預設為 `auto`。自動選擇找不到可用後端時，預設的 `mixed` 策略使用 VM。以下範例手動固定相容的後端檔案或目錄：

```powershell
veroscript --jit-backend="/absolute/path/to/backend" --backend=jit integer_aliases.vs
```

請將路徑替換為實際後端安裝位置。標準函式庫不在 Runtime 資源配置中時，使用 `--stdlib-path` 指定目錄。嵌入宿主明確提供模組或虛擬原始碼資源，並接入輸出擷取。

嵌入及行動端可從 `portable_basics.vs` 開始：它匯入 `numeric.vs`，驗證按值擷取的 Lambda，並輸出 `portable basics: 42`，腳本僅需輸出權限。`runtime_utilities.vs` 的 `std::filesystem::cwd()` 需要檔案讀取權限，`script_tool.vs` 需要環境變數讀取權限，`concurrent_demo.vs` 需要任務建立權限及隔離 VM 工作執行緒。零逾時等待可能取得已完成的結果，應只消費一次。`path_helpers.vs`、`windows_path_text.vs` 僅處理文字，不存取檔案。應用程式在工作執行緒中執行腳本，並設定適當的執行預算。

C# 行動指南提供 MAUI 範例，Python 行動指南提供嵌入式 CPython 驗收腳本。行動編譯、應用程式整合及裝置驗收須獨立完成，桌面腳本測試不能取代這些驗證。

## 閱讀範例

選擇所需主題，查看檔案註解與對應參考，再修改輸入並重新執行。`int main()` 傳回處理程序結束代碼，`int main(string[] args)` 還會接收指令碼引數。不需要傳回值時，也可以使用 `void main()`。

## 入門與綜合能力

| 範例 | 主要內容 |
| --- | --- |
| `hello.vs` | 最小腳本、`main` 入口和輸出。 |
| `portable_basics.vs` | 明確匯入數值模組、陣列、按值擷取 Lambda 及結果檢查，腳本僅需輸出權限。 |
| `argv_echo.vs` | `int main(string[] args)` 和命令列參數。 |
| `object_basics.vs` | 結構體方法、宣告型別時同時定義實例，以及可為空的類別參考。 |
| `language_features.vs` | 約束、裝箱、重載、預設參數和變長參數。 |
| `fib.vs` | 遞歸函式與條件控制流。 |
| `goto_demo.vs` | `goto` 附標籤。 |
| `exceptions.vs` | 型別例外、萬用處理器、重新擲回與 `finally`。 |

## 值、陣列、容器和引用

| 範例 | 主要內容 |
| --- | --- |
| `arrays.vs` | 原生陣列和陣列存取。 |
| `dynamic_array.vs` | 動態陣列。 |
| `brace_initialization.vs` | 花括號初始化。 |
| `integer_aliases.vs` | 整數別名與明確型別。 |
| `numeric_primitives.vs` | 獨立的 `float`/`double`、數字後綴、省略整數部分的浮點字面量，以及 8/16/32/64 位元整數。 |
| `pair_map.vs` | `pair`、映射項目檢視、解構，以及結構體與弱參考值的首次寫入。 |
| `multimap.vs` | 多重對映。 |
| `multiset.vs` | 多重集合與重複元素。 |
| `priority_queue.vs` | 優先權佇列。 |
| `ref_foreach.vs` | 引用變數與基於範圍的遍歷。 |
| `reference_returns.vs` | 引用別名、引用回傳值和類別控制代碼槽重新綁定。 |

## 類型、泛型和可呼叫對象

| 範例 | 主要內容 |
| --- | --- |
| `operators.vs` | 運算子參考參數、可寫及 const 索引參考、賦值參考與後綴快照。 |
| `static_members.vs` | 靜態成員與靜態初始化。 |
| `default_delete.vs` | 預設和刪除的特殊成員函式。 |
| `constructor_initializers.vs` | 建構初始化列表、`const` 欄位和初始化順序。 |
| `friend_access.vs` | `friend` 存取規則。 |
| `handle_struct.vs` | `shared<T>` / `handle<T>` 的結構體共享身分、複製建構及 weak 失效。 |
| `pure_virtual_out_of_class.vs` | 類別外定義與純虛函式語法。 |
| `template_prefix.vs` | 範本成員定義中的前綴語法。 |
| `method_templates.vs` | 方法自有參數與 callable 參照。 |
| `value_template_parameters.vs` | 受檢值參數、列舉策略及固定陣列長度。 |
| `constexpr.vs` | 編譯期純量、純函式及一般執行期呼叫。 |
| `template_methods_main.vs` | 跨檔案樣板方法定義。 |
| `generic_constraints.vs` | 泛型限制和 `where`。 |
| `lambda.vs` | Lambda、值／參考初始化擷取、共享 mutable 計數器及首次到達初始化的區域 static。 |
| `delegates.vs` | `std::delegate` / `std::multicast_delegate`、小寫成員、捕獲、重新綁定與廣播快照。 |
| `delegate_functions.vs` | 具名函式綁定、零到三個參數；可用於 VM 與 C++/C#/Python AOT。 |
| `reflection.vs` | 反射類型、欄位、方法、宣告元資料以及 `mutable` 欄位。 |
| `reflection_executable.vs` | 型別化欄位讀寫、實例／靜態／介面預設方法呼叫、參考參數及回傳值、按值參數隔離。 |

## 模組、預處理器和套件

| 範例 | 主要內容 |
| --- | --- |
| `include_alias.vs` | include/import 別名。 |
| `alias_exports.vs`, `alias_reexports.vs` | 跨檔案 `export using`／`export typedef`、命名空間查找及門面轉匯出；配套模組與門面檔案僅供匯入。 |
| `macro_scope.vs` 與 `macro_scope_module.vs` | 執行入口腳本，驗證一般 `#define` 的模組私有性；後者僅供匯入。 |
| `macro_exports.vs`, `macro_exports_module.vs`, `macro_exports_facade.vs` | 公開物件、函式、可變參數巨集、門面轉匯出與私有隔離；執行 `macro_exports.vs`。 |
| `macro_implicit.vs` | 不必在業務腳本中撰寫匯入即可使用公開巨集；執行 `veroscript --implicit-import macro_exports_facade.vs macro_implicit.vs`。 |
| `reexport_facade.vs` | 模組重新匯出門面。 |
| `reexport_main.vs` | 從入口模組重新匯出宣告。 |
| `preprocessor.vs` | 預處理器條件與巨集。 |
| `package/package_library.vs` | 套件庫的來源檔案。 |
| `package/package_app.vs` | 套件應用入口。 |
| 套件範例說明 | 套件範例的目錄和執行說明。 |

## 標準函式庫、執行階段和工具

| 範例 | 主要內容 |
| --- | --- |
| `memory_operations.vs` | 參考識別、二進位緩衝區、位元組順序、共用檢視與獨立副本。 |
| `formatting.vs` | `print`、`printf`、`format` 與 `formatf`。 |
| `path_helpers.vs` | 路徑標準函式庫。 |
| `standard_library_algorithms.vs` | 泛型演算法、確定性的隨機範圍和可重現的隨機排列。 |
| `standard_namespace.vs` | 建議的 `std::` 寫法、`readonly` 值和明確轉換。 |
| `windows_path_text.vs` | 任意平台上的 Windows 風格路徑文字；不存取檔案系統。 |
| `runtime_utilities.vs` | `range`、目前目錄和時間工具。 |
| `concurrent_demo.vs` | 頻道、函式、靜態方法與 Lambda 的 callable 任務，以及協作式取消。 |
| `script_tool.vs` | 腳本工具參數、環境變數、字串、斷言和退出狀態。 |

## 宿主和綁定設定範例

| 檔案 | 主要內容 |
| --- | --- |
| `workspace.analysis.sample.json` | 語言服務分析設定。 |
| `bindings.sample.json` | 宿主綁定目錄的最小範例。 |

將 `workspace.analysis.sample.json` 的內容作為 `initializationOptions.veroscript` 或 `workspace/didChangeConfiguration.settings.veroscript` 傳入，或套用至對應 IDE 設定。語言伺服器不會自動讀取此檔案。`bindingCatalogPaths` 應指向實際的綁定目錄檔案；直接向伺服器傳入設定時使用絕對路徑。

完整執行約定見[範例目錄說明](../../../Scripts/examples/README.md)。語言規則見[語言參考](language-reference/index.md)，標準函式庫簽章見[標準函式庫參考](standard-library/index.md)，CLI 選項見[命令列工具](../../../../../VeroScriptCli/Resources/Docs/User/zh-TW/index.md)。

- `console_demo.vs`: `std::console` (終端控制).

[應用程式國際化 — std::i18n](standard-library/i18n.md): `i18n_demo.vs` · `VeroScript/Resources/Examples/I18n/`.

- [標準函式庫基礎](standard-library/foundation.md)：`standard_library_foundation.vs`

## variant

[variant — 動態值盒](standard-library/variant.md): `variant.vs`.

```text
veroscript variant.vs
```
<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/examples.md) · [fr-FR](../fr-FR/examples.md) · [ja-JP](../ja-JP/examples.md) · [ko-KR](../ko-KR/examples.md) · [zh-CN](../zh-CN/examples.md) · [zh-TW](examples.md)
