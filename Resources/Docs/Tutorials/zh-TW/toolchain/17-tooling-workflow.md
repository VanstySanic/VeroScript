# Tooling 工作流程：連接編輯器與偵錯器

本教學會把 VeroScript 專案連接到語言伺服器和偵錯配接器。你會先驗證 CLI，
再設定 LSP、體驗編輯器能力，最後啟動 DAP 偵錯工作階段。

## 第 1 步：驗證 CLI

使用同一版本 Runtime 提供的 CLI 和 VM、版本相符的語言伺服器，以及支援 LSP 和 DAP 的用戶端。將下方兩個檔案儲存到同一個可寫入的練習目錄。在該目錄執行終端機命令，並在編輯器中將它開啟為工作區。

### hello.vs

```VeroScript
import "lesson-utils.vs";

namespace Lesson
{
	int twice(int value)
	{
		return value * 2;
	}
}

int main(string[] args)
{
	std::chrono::now();
	int value = Lesson::twice(lessonSeed());
	print("value={}, argc={}", value, len(args));
	return 0;
}
```

### lesson-utils.vs

```VeroScript
export int lessonSeed()
{
	return 21;
}
```

```powershell
veroscript --version
veroscript --check hello.vs
veroscript --vm-backend=vs-bytecode --backend=vm hello.vs red blue
```

檢查成功時不會執行腳本。執行時會輸出 `value=42, argc=2`，結束代碼為 0。

如果這裡失敗，請先修正專案或安裝環境。CLI 和 LSP 結果不一致時，通常是模組路徑、
巨集或繫結目錄設定不同。

## 第 2 步：啟動語言伺服器

讓編輯器透過標準輸入輸出啟動相符版本的 `veroscript-lsp`。編輯器傳送
`initialize`、等待回應後傳送 `initialized`，並把專案根目錄作為 workspace root。

將下方欄位與工作區、用戶端能力一起放入 `initialize` 請求的 `params`。 讓語言伺服器在練習目錄中啟動，使 `.` 指向該目錄。如果編輯器從其他位置啟動伺服器，請使用絕對路徑，例如 Windows 上的 `C:/work/project`。若其他專案需要指定獨立的標準函式庫目錄，請設定 `stdlibPath`。

```json
{
	"locale": "zh-TW",
	"initializationOptions": {
		"veroscript": {
			"importPaths": [
				"."
			]
		}
	}
}
```

如果用戶端支援動態註冊檔案監看，伺服器會在 `initialized` 後要求監看
`**/*.vs` 和 `**/*.vslib`。如果不支援，請在用戶端設定這些模式，或明確傳送
`workspace/didChangeWatchedFiles` 通知。

開啟 `.vs` 文件後，伺服器會在記憶體中分析開啟的緩衝區，因此未儲存的 import 也能
參與解析。使用增量 `didChange`；較舊版本的變更會被忽略。

## 第 3 步：體驗編輯器能力

開啟第 1 步儲存的 `hello.vs`。

1. 暫時製造語法或型別錯誤，確認診斷已發布，然後還原錯誤。
2. 將 `std::chrono::now();` 替換為 `std`，把游標放在它後面，呼叫補全並接受 namespace 項目；它會插入 `std::`。
   輸入 `ch` 並接受 `chrono`，會插入 `chrono::`，接著即可查看其中的成員。最後將這一行補全為
   `std::chrono::now();`，讓範例恢復為有效程式碼。
   只有替換範圍後面還沒有 `::` 時，補全才會追加它。
3. 將 `Lesson::twice(lessonSeed())` 暫時換成 `Lesson::tw`，再呼叫補全。若用戶端宣告支援 `textDocument.completion.completionItem.snippetSupport`，檢查 `twice` 的參數預留位置。繼續前還原原本的呼叫。
4. 懸停查看命名空間、函式或型別的解析資訊。
5. 從 `hello.vs` 中的呼叫跳到 `lessonSeed` 的定義，確認會開啟 `lesson-utils.vs`。在文件符號中找到 `Lesson` 和 `main`，再用 `workspace/symbol` 搜尋 `Lesson::twice`，以 `workspaceSymbol/resolve` 解析結果。
6. 解析補全項目，查看可用的 Markdown 說明。開啟 `import "lesson-utils.vs";` 所在行的連結。
7. 請求語意 token，再使用上一次的 result id 請求語意 token delta。
8. 在 `Lesson` 上準備重新命名，將它改為 `Practice`，確認宣告及使用限定名稱的呼叫一起更新。
9. 先格式化文件，再格式化選取範圍，確認編輯涵蓋選取範圍所在的完整行。試用 `source.formatDocument`。若要試用 `source.organizeImports`，將匯入行複製一次，確認動作移除了重複行。儲存兩個檔案，再執行 `veroscript --check hello.vs`。

泛型型別和可呼叫項目補全在適用的情境中可以使用 LSP snippet。只有用戶端宣告支援
`textDocument.completion.completionItem.snippetSupport` 時伺服器才會傳送 snippet；其他情況插入一般文字。
標準函式庫命名空間可以導覽，但不能重新命名。
伺服器提供完整格式化，以及只移除完全重複 import 的保守整理操作；
兩者都不會改寫無關原始碼。

## 第 4 步：加入宿主繫結目錄

若你正在操作使用宿主繫結的專案，請完成這一步。若只使用本課的兩個檔案，可以直接進入第 5 步。

將專案宿主使用的繫結目錄依相同順序加入 `initializationOptions.veroscript.bindingCatalogPaths`。其中的函式、型別、
成員、註解、泛型範本和文件會參與分析、補全、懸停和符號查詢。

伺服器執行期間修改繫結目錄，下一次請求會重新載入並重新分析開啟的文件。繫結目錄
只有中繼資料，宿主仍然必須註冊執行階段實作。

## 第 5 步：切換診斷語言

在開啟文件前，把 BCP 47 標籤放入 `initialize.locale`：

```json
{
	"locale": "fr-FR"
}
```

用新的 `locale` 重新啟動語言伺服器。再次加入第 3 步中的同一個暫時錯誤，比較診斷代碼和翻譯後的訊息。代碼保持不變，缺少翻譯時使用 `en-US`。偵錯前修正錯誤並儲存檔案。

## 第 6 步：啟動偵錯器

在 DAP 用戶端選擇連線方式。由用戶端啟動配接器時，讓它在練習目錄中使用下列命令，透過標準輸入輸出連線。

```powershell
veroscript --debug hello.vs
```

獨立啟動用戶端時，在練習目錄中啟動 TCP 配接器，再將用戶端連到 `127.0.0.1:4711`。

```powershell
veroscript --debug --dap-port=4711 hello.vs
```

使用標準輸入輸出且需要預設腳本引數時，用下列命令取代第一個命令。

```powershell
veroscript --debug hello.vs red blue
```

在用戶端將 `cwd` 設為練習目錄，啟動 `hello.vs`。在 `print` 所在行設定中斷點，並透過啟動請求的 `args` 傳入 `["red", "blue"]`。若已透過 CLI 預設引數，可以省略 `args`。在中斷點停下後繼續執行，偵錯輸出應顯示 `value=42, argc=2`。請求順序和選項見[偵錯參考](../../../../../VeroScriptTooling/Resources/Docs/User/zh-TW/debugging.md)。

在 DAP `launch` 或 `attach` 請求中，使用 `program` 或內嵌 `source`，再透過 `args`
傳給 `main(string[] args)`。使用 `input` 提供腳本標準輸入，使用 `env` 提供環境變數
覆寫，使用 `cwd` 指定工作目錄，使用 `stdlibSearchPaths` 指定標準函式庫搜尋根目錄。
腳本輸入與 DAP 協定輸入串流相互獨立。相對路徑以 `cwd` 為基準；未提供工作目錄時以
`workspace` 為基準。偵錯使用 VM。請在一般執行時測量效能。

DAP `initialize` 參數支援使用 BCP 47 `locale` 指定診斷和錯誤訊息的顯示語言。
用戶端應根據 DAP 的成功或錯誤欄位進行分支，而不要根據翻譯後的訊息文字判斷。

原始碼行斷點透過 `setBreakpoints` 設定。不支援例外斷點和函式斷點；相關請求會
明確失敗，因此用戶端不應將這兩類斷點顯示為已啟用。

`configurationDone` 之後，線協定宿主會先確認請求，再於執行緒中執行 VM，因此執行期間
仍能處理協定請求。可以對執行中的 VM 傳送 `pause` 請求協作式暫停；隨後 `stopped` 事件
的原因會是 `pause`。使用 `continue`、`next`、`stepIn` 或 `stepOut` 恢復執行，使用
`terminate` 請求取消。`disconnect` 對 `launch` 預設會結束偵錯物件，對 `attach` 預設不
會結束；多工作階段 TCP 宿主中，`terminateDebuggee=false` 會保留監聽器。

執行期間，`std::console::write` 和 `std::console::error` 不會追加換行。
呼叫 `std::console::flush()` 可以立即顯示進度片段。配接器會在程序結束前，
把標準輸出和錯誤輸出作為不同的 DAP output 事件轉送。

LSP 工作階段應以 `shutdown` 後接 `exit` 結束。

## 練習

1. 讓 CLI 和 LSP 解析相同的 import 模組。
2. 在較新版本後傳送舊的 `didChange`，確認較新的緩衝區仍然有效。
3. 加入繫結目錄，確認宿主函式出現在補全和懸停中。
4. 切換語言，確認診斷 code 和腳本結果不變。
5. 修改磁碟上的匯入模組，傳送 `workspace/didChangeWatchedFiles`，確認開啟文件的診斷
   已經更新。

協定詳情見[語言伺服器參考](../../../../../VeroScriptTooling/Resources/Docs/User/zh-TW/lsp.md)，公開查詢層見
[Tooling 模組參考](../../../../../VeroScriptTooling/Resources/Docs/User/zh-TW/index.md)。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/toolchain/17-tooling-workflow.md) · [fr-FR](../../fr-FR/toolchain/17-tooling-workflow.md) · [ja-JP](../../ja-JP/toolchain/17-tooling-workflow.md) · [ko-KR](../../ko-KR/toolchain/17-tooling-workflow.md) · [zh-CN](../../zh-CN/toolchain/17-tooling-workflow.md) · [zh-TW](17-tooling-workflow.md)
