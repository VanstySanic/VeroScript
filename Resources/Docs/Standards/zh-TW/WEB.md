# TypeScript 與 JavaScript 程式碼規範（暫定）

適用範圍：[VeroScript 本專案開發與貢獻](README.md)。

TypeScript/JavaScript 遵循 [GENERAL.md](GENERAL.md) 的分塊、註解和最小必要說明原則。下列子句換行規則為強制要求；其他花括號布局暫時遵循所在模組的既有風格。

## 當前暫定規則

- `else`、`else if`、`catch`、`finally` 和 `do…while` 尾部的 `while` 依 [GENERAL.md](GENERAL.md)
  的換行規則另起一行；區塊本體的左花括號也另起一行，與子句對齊。
- 每個邏輯層級使用一個真實 Tab，預設顯示寬度為四欄；遵循 [GENERAL.md](GENERAL.md) 的縮排規則。
- 型別、函式、條件、迴圈和 `switch` 按語義分塊，避免無意義的單行壓縮。
- 每個 `switch` 必須有 `default`。TypeScript 的窮盡性檢查可以放在 `default` 中實作，但不能
  因為靜態檢查而省略 `default`；該分支必須明確處理未知值。
- 每個 `case` 和 `default` 標籤的執行內容使用獨立花括號作用域；不依賴隱式貫穿。確有意共享
  處理邏輯時，使用緊鄰的說明註解表達該意圖。唯一例外是該標籤唯一執行路徑為直接結束當前函式的
  `return`；其中不得混入其他語句。
- 所有控制分支必須使用花括號。唯一例外是分支體僅有一條直接 `return` 或 `return <表示式>` 的
  單行衛語句；它不得與 `else` 鏈、日誌、清理或其他語句組合。
- TypeScript 在模組範圍內的型別按 `enum` → `interface` → `type`/其他型別別名 → `class` 排列；
  泛型介面和泛型類分別歸入其自身類別。JavaScript 沒有對應型別宣告時，按匯出關係和依賴保持
  同等清晰的順序。
- 類內容參照 C++ 的整體順序：關聯別名/常量、繼承或介面實作、內部型別、方法、欄位；欄位放在
  對應訪問語義區域的最後。
- 多介面實作或介面卡分塊需要時使用配對 `//~ Begin <Name> Interface` / `//~ End <Name> Interface`
  註解。
- 匯出型別、介面和類使用 PascalCase；函式、欄位、參數和區域變數使用 camelCase；外部協議欄位
  保持其契約名稱。
- 對外公開的資料、錯誤和資源釋放語義必須透過型別或必要註解表達。

## 待補充

後續可決定花括號風格、引號/分號/尾逗號、`readonly`、非同步取消、Wasm 資源生命週期和型別檢查
要求。
