# 診斷與錯誤

VeroScript 的問題通常分為編譯診斷和執行時錯誤。編譯診斷必須在腳本執行前修復；執行時錯誤只會在對應路徑實際執行時出現。

## 編譯診斷

編譯階段會檢查詞法、語法、名稱解析、型別轉換、重載、泛型約束、介面實作、模組可見度和宣告規則。可以使用 `--check` 只檢查腳本：

```powershell
veroscript --check script.vs
```

檢查模式不會呼叫 `main`，適合編輯器儲存檢查、CI 驗證和套件建置前檢查。診斷通常包含檔案、行列位置和錯誤說明；同一根因可能產生多個關聯診斷，應先修復最早的錯誤。

## 執行階段錯誤

執行階段錯誤可能來自：

- 陣列、字串或容器索引越界；
- 條件為 `false` 的 `assert`；
- 呼叫未綁定的委託或執行其他無效的執行階段操作；
- 檔案、目錄或工作目錄操作失敗；
- 對已關閉通道執行不合法操作；
- 遞歸深度、類型操作或宿主綁定違反執行階段限制。

執行階段錯誤會終止目前執行或透過宿主的執行結果和錯誤流報告。宿主程式應檢查 `FRunResult`，並根據需要儲存錯誤文字和診斷位置。

VM、混合和 JIT 執行使用相同的語言診斷。JIT 核心回報算術或轉換失敗時，執行階段會將它
對映到可取得的最佳原始碼位置，並保留更具體的輔助診斷。宿主應使用結構化診斷和位置，
不要比對本地化後的顯示文字。

## 腳本例外

`throw` 會擲回一個可以跨越函式呼叫傳播、並由 `try`/`catch` 處理的腳本值。型別處理器或
`catch (...)` 可以處理腳本例外，但不能攔截引擎/執行階段錯誤，包括失敗的 `assert`。無論哪種
錯誤傳播，`finally` 區塊都會執行。完整語法和配對規則請參閱[控制流](../language-basics/control-flow.md#%E4%BE%8B%E5%A4%96)。

## 語言和輸出

命令列工具可以選擇診斷語言：

```powershell
veroscript --lang zh-TW --check script.vs
```

也可以透過 `VERO_LANG` 設定預設語言。腳本的標準輸出和診斷錯誤流是不同的通道，宿主整合時應分別接收。

## 排查順序

檔案參考頁應優先確認宣告、作用域、類型和模組可見性；實際排查時建議先用 `--check` 排除編譯問題，再用最小輸入複現執行階段問題，最後檢查宿主權限、匯入路徑和標準庫搜尋路徑。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-reference/diagnostics.md) · [fr-FR](../../fr-FR/language-reference/diagnostics.md) · [ja-JP](../../ja-JP/language-reference/diagnostics.md) · [ko-KR](../../ko-KR/language-reference/diagnostics.md) · [zh-CN](../../zh-CN/language-reference/diagnostics.md) · [zh-TW](diagnostics.md)
