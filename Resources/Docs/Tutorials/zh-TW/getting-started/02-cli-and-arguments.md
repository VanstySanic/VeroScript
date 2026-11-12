# 02：檢查、執行和讀取命令列參數

一個腳本一旦需要處理不同輸入，就不應該把資料寫死在原始檔中。本篇會讓腳本讀取命令列參數，並學習如何用 `--check` 和不同後端來驗證它。

## 第一步：接收參數

建立 `argv_echo.vs`：

```VeroScript
int main(string[] args)
{
	print("argc=" + to_string(len(args)));
	for (int i = 0; i < len(args); i = i + 1)
	{
		print(args[i]);
	}
	return 0;
}
```

`main(string[] args)` 接收腳本路徑之後的參數。腳本檔名本身不會進入 `args`，所以 `len(args)` 表示真正傳給腳本的參數數。

## 第二步：傳入參數

```powershell
veroscript --check argv_echo.vs
veroscript argv_echo.vs red blue
```

輸出類似：

```text
argc=2
red
blue
```

參數都是字串。`to_int` 等數值轉換函式接受數值或布林值，不負責將字串解析為數字。需要數值參數時，應先定義並驗證允許的文字格式，再用於計算。詳見[轉換函式](../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/builtins.md)。

## 第三步：做一個問候工具

將下面的程式儲存為 `greet.vs`，放在 `argv_echo.vs` 所在目錄：

```VeroScript
int main(string[] args)
{
	string name = "world";
	if (len(args) > 0)
	{
		name = args[0];
	}

	print("hello, " + name);
	return 0;
}
```

先檢查程式，再分別以無參數和有參數的方式執行：

```powershell
veroscript --check greet.vs
veroscript greet.vs
veroscript greet.vs VeroScript
```

無參數執行時使用預設值；有參數執行時使用第一個參數覆蓋預設值。

## 第四步：選擇執行後端

普通執行預設使用混合後端。為了比較不同執行方式，可以明確指定：

```powershell
veroscript --vm-backend=vs-bytecode --backend=vm argv_echo.vs one two
veroscript --jit-backend="/path/to/Jit" --backend=jit argv_echo.vs one two
```

VM 命令使用 Runtime 隨附的直譯器。嘗試 JIT 命令前，將 `/path/to/Jit` 換成相容後端；載入失敗時，CLI 會在執行腳本前停止。成功後，兩條命令都像前面一樣把 `one` 和 `two` 傳給 `argv_echo.vs`。專案預設值、自動選擇與停用 JIT 的規則見 [CLI 後端參考](../../../../../VeroScriptCli/Resources/Docs/User/zh-TW/index.md#cli-backend-policy)。

偵錯時會使用 VM 偵錯路徑；不要用 JIT 選項判斷偵錯器是否生效。現在先記住：開發階段優先使用 `--check`，需要比較執行方式時再指定後端。

## 練習

1. 如果沒有參數，輸出 `usage: greet.vs <name>`，然後回傳一個非零狀態碼。
2. 檢查第二個參數是否存在，並用 `len` 輸出它的長度。
3. 使用 `--lang zh-TW` 和 `--lang en-US` 比較診斷語言。

下一篇開始學習變數、條件與迴圈：[變數、條件和迴圈](../language/03-values-and-control-flow.md)。

對應的完整範例是 [`examples/argv_echo.vs`](../../../../../VeroScript/Resources/Scripts/examples/argv_echo.vs)，CLI 選項詳見[命令列工具](../../../../../VeroScriptCli/Resources/Docs/User/zh-TW/index.md)。

比較效能時，使用固定輸入和後端的一般執行，並等待暖機。偵錯、保留初始化和有界清理使用 VM；持久工作階段中符合條件的一般函式可以使用所選 JIT。位元組碼快取減少啟動時的編譯工作。工作階段 JIT 快取的行為和限制見 [C++ 嵌入指南](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/embedding.md#threads-and-hot-reload)。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/getting-started/02-cli-and-arguments.md) · [fr-FR](../../fr-FR/getting-started/02-cli-and-arguments.md) · [ja-JP](../../ja-JP/getting-started/02-cli-and-arguments.md) · [ko-KR](../../ko-KR/getting-started/02-cli-and-arguments.md) · [zh-CN](../../zh-CN/getting-started/02-cli-and-arguments.md) · [zh-TW](02-cli-and-arguments.md)
