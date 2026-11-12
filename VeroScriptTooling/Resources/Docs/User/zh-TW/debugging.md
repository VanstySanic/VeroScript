# 偵錯

VeroScript 偵錯器使用 Debug Adapter Protocol（DAP），並透過 VM 執行腳本。偵錯和熱重載場景不會使用 JIT 執行路徑。

## 標準輸入輸出模式

```powershell
veroscript --debug script.vs
```

此模式透過標準輸入輸出傳輸 DAP 訊息，適合由編輯器或其他偵錯用戶端啟動並管理。

## TCP 模式

```powershell
veroscript --debug --dap-port=4711 script.vs
veroscript --debug --dap-port=4711 --dap-bind=127.0.0.1 script.vs
```

`--dap-port` 啟動 TCP DAP 服務，連接埠範圍為 `1` 到 `65535`；`--dap-bind` 指定綁定位址，預設為 `127.0.0.1`。只在確實需要遠端或獨立客戶端連線時開放非回環位址。

`veroscript-dap` 是同一偵錯宿主的薄封裝。 IDE 的 F6 啟動和附加偵錯也使用 DAP；偵錯會話斷開時，用戶端可以透過 `terminateDebuggee` 選擇是否結束偵錯處理程序。

## 會話生命週期與啟動參數

用戶端必須先傳送 `initialize`，再傳送 `launch` 或 `attach`。配接器會回傳包含
`supportsTerminateRequest` 在內的能力，以及 `name`、`version`、`platform` 和
`jitCapability` 中繼資料。線協定宿主會在 `initialize` 回應之後傳送 `initialized` 事件。

配接器目前回報以下 DAP 能力：

| 能力 | 值 |
| --- | --- |
| `supportsConfigurationDoneRequest` | `true` |
| `supportsStepInRequest` | `true` |
| `supportsStepOutRequest` | `true` |
| `supportsStepBackRequest` | `false` |
| `supportsConditionalBreakpoints` | `true` |
| `supportsEvaluateRequest` | `true` |
| `supportsSetVariable` | `true` |
| `supportsTerminateRequest` | `true` |

`jitCapability` 表示核心 JIT 服務契約（目前為 `backend-services`），不表示已安裝或選用原生後端。

`attachMode: "domain"` 與 `domainId` 用於連線至宿主預備的持久執行域，偵錯連線可跨越多次指令碼呼叫。
用戶端必須先確認 `initialize.body.veroscriptSupportsDomainAttach` 嚴格等於 `true`，否則應拒絕
線上附加，不能退回成載入程式。`configurationDone` 只啟用連線，不載入或啟動指令碼；具體能力以
附加回應為準。預設中斷按宿主約定恢復暫停執行並保留宿主。此模式不能接管已進入 JIT 執行的 VM。
詳見[執行域契約](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/session-debugging.md)。

下文的程式載入流程適用於 `launch` 和程式模式的 `attach`（`attachMode: "program"` 或省略）。
兩者都透過相同的偵錯載入器載入程式。`attach` 用於 IDE 連線到由嵌入式
宿主或 TCP 宿主持有真實繫結的工作階段；它不是附加到已經執行中的 VM。設定斷點後傳送
`configurationDone`，程式才會開始執行。

`launch` 和 `attach` 支援以下參數：

| 欄位 | 說明 |
| --- | --- |
| `program` | 腳本路徑。CLI 偵錯命令可以提供預設值。 |
| `source` | 內嵌原始碼。非空時優先於 `program`。 |
| `fileLabel` | 內嵌 `source` 的顯示標籤和原始碼身分。 |
| `workspace` | 相對啟動路徑的基準目錄。 |
| `cwd` | 腳本執行目錄和相對路徑解析目錄；也接受別名 `workingDirectory`。未提供時，在可用的情況下使用 `workspace`，目錄必須存在。 |
| `args` | 傳給 `main(string[] args)` 的字串陣列，不包含腳本路徑。 |
| `input` | 腳本的初始標準輸入，與 DAP 協定輸入串流相互獨立。 |
| `env` | 本次偵錯的環境變數覆寫。規範形式是物件；`environment` 是別名，也接受由 `{ "name", "value" }` 項目組成的陣列。 |
| `stdlibSearchPaths` | 有序的標準函式庫搜尋根目錄；`stdlibPath` 是單一根目錄的相容別名。 |
| `importPaths`、`implicitImportPaths` | 一般模組搜尋路徑和隱式匯入路徑。 |
| `defines`、`predefinedMacros` | 前置處理器定義和宿主風格的預先定義巨集。 |
| `sourceOverlays` | 解析匯入模組時使用的記憶體原始碼檔案。 |
| `bindingCatalogPaths` | 依順序載入的繫結目錄檔案，作為中繼資料/樁繫結使用；嵌入式宿主提供 `LiveBindings` 時，真實繫結優先。 |
| `allowCircularImports`、`allowProjectWideAccess` | 匯入策略和專案可見性策略。 |
| `stopOnEntry` | 是否在入口點暫停，再繼續正常執行。 |
| `backend`、`maxCallDepth` | 後端偏好和呼叫深度限制；偵錯執行仍使用 VM 路徑。 |

`program`、匯入路徑、標準函式庫路徑、隱式匯入路徑、覆寫檔案路徑和繫結目錄路徑中的
相對路徑，會以 `cwd`（若已設定）為基準，否則以 `workspace` 為基準。提供 `workspace` 但
不提供 `cwd` 時，工作區也會成為腳本執行目錄。環境變數覆寫只在本次偵錯期間生效，結束
後會還原。使用真實繫結的附加工作階段不能同時指定環境變數覆寫或工作目錄覆寫。

CLI 允許在腳本路徑後提供腳本參數，並將它們作為 DAP 請求的預設參數：

```powershell
veroscript --debug hello.vs red blue
veroscript --debug --dap-port=4711 hello.vs red blue
```

明確的 DAP `args` 陣列會取代這些預設值。單次啟動最多接受 4096 個腳本參數、三個模組
路徑陣列各最多 2048 項，以及最多 1024 個環境變數項目。環境變數名稱不能為空、不能包含
`=` 或 NUL；環境變數值不能包含 NUL。

## 非同步執行與控制

對於標準輸入輸出和 TCP 線協定宿主，`configurationDone` 會在 VM 執行完成前獲得確認，
VM 在執行工作執行緒中執行，因此協定讀取執行緒仍可處理控制請求和暫停狀態請求。`stopped`
事件的 `reason` 為 `entry`、`breakpoint`、`step` 或 `pause`；當協作式暫停中斷正在執行
的 VM 時，原因是 `pause`。

`continue`、`next`、`stepIn` 和 `stepOut` 只能在收到 `stopped` 後使用，並會恢復執行。
`stackTrace`、`scopes`、`variables`、`evaluate` 和 `setVariable` 也只能在暫停期間使用。
應在 `configurationDone` 前或暫停期間設定斷點；執行期間修改斷點會被拒絕。`pause` 只能
在 VM 執行時使用，並請求一次協作式暫停；`terminate` 請求協作式取消。用戶端應等待隨後
的 `exited` 和 `terminated` 事件，不應假定請求回應本身已經結束執行。

`threads` 請求會公開一個邏輯 VM 執行緒，ID 為 `1`，名稱為 `main`。這是偵錯器的腳本
執行視圖；線協定執行緒和執行工作執行緒不會作為腳本執行緒公開。

對於 `disconnect`，`launch` 的 `terminateDebuggee` 預設值為 `true`，`attach` 的預設值為
`false`。在多工作階段 TCP 宿主中，`terminateDebuggee=false` 只結束目前用戶端工作階段並
保留監聽器；`true` 會停止宿主監聽。`terminate` 始終取消偵錯執行並要求宿主停止。

## TCP 宿主與嵌入

CLI TCP 宿主預設服務一個用戶端工作階段。SDK 宿主可以使用 `VeroScriptDapTcp::Run` 和
`FDapTcpRunOptions`：`MaxSessions=0` 會持續服務用戶端直到宿主終止，`Port=0` 會選擇暫時
連接埠並透過 `OnListening` 回報，`BindAddress` 通常應保持為 `127.0.0.1`。嵌入式宿主可以
設定 `FDapStdioRunOptions::LiveBindings`，讓 `attach` 工作階段執行已註冊的宿主函式，而不
是繫結目錄樁。真實繫結目錄必須在 DAP 執行期間保持有效。嵌入式宿主自行回報就緒狀態時，
可以將 `bAnnounceListen` 設為 `false`；預設監聽通知會寫入宿主錯誤串流。
`VeroScriptDapStdio::Run` 在工作階段正常結束時回傳 `0`，協定或 I/O 失敗時回傳 `1`，用戶端
要求終止宿主時回傳 `2`。
對於 `VeroScriptDapTcp::Run`，用戶端要求終止宿主屬於正常回傳（`0`）；監聽、接受連線或
其他非正常工作階段失敗會回傳非零值。

## 斷點支援

配接器透過 `setBreakpoints` 支援原始碼行斷點，不支援例外斷點和函式斷點。
不含任何篩選器的 `setExceptionBreakpoints` 僅作為標準用戶端啟動交握中的清除操作成功；
包含例外篩選器的請求以及所有 `setFunctionBreakpoints` 請求都會回傳失敗的 DAP 回應。
用戶端不應將這兩類斷點顯示為已啟用。

`setBreakpoints` 使用從 1 開始計數的實體原始碼行。在 `launch` 或 `attach` 載入偵錯目標之前，
回傳的中斷點為 `verified: false` 的待繫結狀態。程式碼載入後，配接器會繫結它，並以相同的 DAP
中斷點 ID 發出標準 `breakpoint` 事件，`reason` 為 `"changed"`。只有路徑唯一解析為已編譯原始檔、
要求行在前置處理後仍處於作用中狀態且存在可執行位置時，中斷點才會驗證成功；絕不只按檔案基名
比對。繫結失敗的中斷點保持未驗證，並附帶在地化 `message`。

函式內不可執行的實體行可繫結到該函式中後續的可執行實體行；用戶端必須採用回應傳回的
`line`。模組初始化器中的中斷點必須命中精確的實體行。被前置處理移除的行、超出範圍的行以及
不存在或有歧義的原始檔都不會繫結。

其他不支援的 DAP 命令同樣會回傳失敗回應，並附帶在地化錯誤訊息。可以在 DAP
`initialize` 參數的 `locale` 中設定 BCP 47 標籤來選擇顯示語言。用戶端應根據 DAP
的 success/error 欄位判斷結果，不應根據翻譯後的訊息文字分支。

## 偵錯期間的輸出

`std::console::write` 和 `std::console::error` 寫入時不會追加換行。
呼叫 `std::console::flush()` 可以立即顯示等待中的標準輸出。DAP 配接器會在程式仍在
執行時，將標準輸出和錯誤輸出作為不同的 `output` 事件轉送；最終執行結果也會分別保留這兩個串流。

## 執行限制

偵錯器使用完整的 VM 偵錯路徑，支援斷點、單步和變數檢查。請求 `--backend=mixed` 或 `--backend=jit` 不會讓目前偵錯執行切換到 JIT；偵錯結束後，普通執行可以恢復請求的後端。

偵錯器和宿主綁定的更細粒度整合見[宿主整合](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/embedding.md)。

## 記憶體檢查

暫停時展開物件或區域，可檢查 `[object_id]`、`[byte_length]` 等唯讀中繼資料，位址僅在允許時顯示。ID 識別存活物件，位址只是瞬時觀察值。檢查中繼資料不能指派。DAP `variablesReference` 是協定控制代碼，任意 `readMemory` / `writeMemory` 要求仍不支援。

## 偵錯堆疊框架中的原始碼快照

框架提供正數 `sourceReference` 時，用戶端應使用 DAP `source` 請求文字。傳回內容是內嵌、產生或記憶體原始碼的實際編譯快照，也包括磁碟檔案過時或缺失的原始碼覆寫。應以唯讀方式顯示；修改同名磁碟檔案不會改變正在執行的程式。繼續執行後，已擷取的原始碼文字仍可讀取，但框架與變數控制代碼會失效。重新載入或清空程式後，舊原始碼參照也會失效，應從目前框架重新取得。

## 原生偵錯主程式生命週期

在所屬執行緒循序處理生命週期操作。Run 執行期間（含回呼）會拒絕 LoadSource/LoadFile 和巢狀 Run，保留目前程式；Clear 不執行操作，應在 Run 返回或因例外結束後呼叫。Run 開始時建立指令碼引數快照，回呼內修改僅影響下次執行。替換／清空 SetStopHandler 或 DAP 事件處理器僅影響後續通知；目前回呼及擷取值保留至返回。這不允許並行存取。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/debugging.md) · [fr-FR](../fr-FR/debugging.md) · [ja-JP](../ja-JP/debugging.md) · [ko-KR](../ko-KR/debugging.md) · [zh-CN](../zh-CN/debugging.md) · [zh-TW](debugging.md)
