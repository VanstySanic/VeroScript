# 內建函式

請參閱[標準函式庫基礎](foundation.md#foundation-imports)。

內建函式與[標準函式庫模組](index.md)的邊界如下：內建函式適合表達執行階段最基礎的操作；更高層的字串、檔案、路徑、並發和事件功能應優先使用對應的標準函式庫模組。

## 輸出

| 函式 | 說明 |
| --- | --- |
| `print(value)` | 將一個值寫入標準輸出。傳入格式字串和額外參數時，使用花括號格式化；`{{` 和 `}}` 分別表示字面量花括號。 |
| `printf(format, ...)` | 使用 printf 風格轉換說明格式化並寫入標準輸出；它與 `print` 使用不同的格式語法。 |
| `eprint(value)` | 將一個值寫入錯誤輸出流。 |

```VeroScript
int main()
{
	int score = 97;
	print("score={}", score);
	printf("score=%03d", score);
	eprint("diagnostic");
	return 0;
}
```

需要將格式化結果儲存在變數中時，使用標準函式庫中的 `format` 或 `formatf`。花括號欄位、printf 轉換符、寬度、精確度和錯誤邊界見[字串語言行為](../language-basics/strings.md#%E6%A0%BC%E5%BC%8F%E5%8C%96)與[字串模組](string.md)。

## 主控台串流

`std::console` 函式由執行階段提供，不需要匯入模組。它們只提供命名空間形式，
以明確表達輸出串流和互動式終端語意：

| 函式 | 說明 |
| --- | --- |
| `std::console::write(value)` | 將一個值寫入標準輸出，不追加換行。 |
| `std::console::error(value)` | 將一個值寫入錯誤串流，不追加換行。 |
| `std::console::flush()` | 刷新等待中的標準輸出。 |
| `std::console::clear()` | 在宿主支援時清除互動式輸出，並傳回是否執行了清除。 |
| `std::console::is_interactive()` | 傳回標準輸入和標準輸出是否都處於互動模式。 |

需要在下一次換行或將控制權交給宿主工具前顯示進度片段時，呼叫
`std::console::flush()`。偵錯配接器會把刷新後的標準輸出和錯誤輸出作為不同的輸出事件轉送。

### 終端控制

| API | |
| --- | --- |
| `supports_color()`, `supports_cursor()`, `supports_clear()`, `is_output_terminal()` | 分別查詢色彩、游標、清屏／清行能力及輸出是否為終端。 |
| `set_color(string foreground, string background)`, `reset_style()` | 設定前景／背景色；重設樣式。 |
| `clear_line(string mode)` | 清除游標之後、之前或整行，包含目前字元位置；不移動游標。 |
| `move_cursor(int column, int row)`, `set_cursor_visible(bool visible)` | 移至從 0 開始的欄／列；顯示或隱藏游標。 |
| `width()`, `height()` | 可見視窗欄数／列數；未知時回傳 0。 |

控制函式回傳 `bool`，不支援或失敗時為 `false`。重新導向輸出不會自動寫入控制碼，輸入不必同時可互動。`clear()` 清除可見視窗並移至原點，不刪除歷史內容。座標必須是 `0..32766` 的整數，超出已知視窗時回傳 `false`。錯誤型別、色彩名稱及清行模式會報錯。修改操作需要控制台輸出權限。色彩為 `black`、`red`、`green`、`yellow`、`blue`、`magenta`、`cyan`、`white` 及 `bright_` 變體和 `default`。

執行結束、錯誤或取消時會盡力恢復樣式與游標顯示。POSIX／自訂宿主恢復預設樣式與可見游標；Windows VM 與三種 AOT 恢復原生初始狀態。嵌入宿主預設不提供控制；C++、C ABI、C#、Python 可設定查詢與控制回呼及不加換行的精確文字回呼，並同步工作執行緒。行動端／瀏覽器文字擷取保持一般文字。範例：`console_demo.vs`。

```VeroScript
if (std::console::supports_color())
{
	std::console::set_color("bright_green", "default");
}
std::console::write("Ready\n");
std::console::flush();
std::console::reset_style();
```

## 類型轉換

| 函式 | 說明 |
| --- | --- |
| `to_int(value)` | 轉換為 `int`。浮點數向零截斷；超出範圍或非有限值時報告錯誤。 |
| `to_double(value)` | 轉換為 `double`。 |
| `to_float(value)` | 轉換為 `float`；結果依 binary32 儲存。 |
| `to_unsigned(value)` | 轉換為 `unsigned int`；負數、超出範圍和非法值會報告錯誤。浮點數向零截斷。 |
| `to_int64(value)` | 轉換為 `int64`，並進行範圍檢查。 |
| `to_uint64(value)` | 轉換為 `uint64`，並進行範圍檢查。 |
| `to_string(value)` | 將值轉換為字串。 |
| `to_wstring(value)` | 將字串值明確轉換為寬字串；參數必須是 `string` 或 `wstring`。 |

```VeroScript
int main()
{
	int count = to_int(12.75);
	double ratio = to_double(3);
	unsigned int flags = to_unsigned(true);
	string text = to_string(count);
	wstring wide = to_wstring(text);
	print("count={}, ratio={}, flags={}", count, ratio, flags);
	print(wide);
	return 0;
}
```

轉換函式不會把任意文字自動解析為數字。需要讀取數位輸入時，應使用 `std::io::read_int` 或 `std::io::read_double`；當需要解析檔案或協定文字時，應在腳本中明確實作解析規則。

## 數值和集合

| 函式 | 說明 |
| --- | --- |
| `abs(value)` | 傳回數值的絕對值。 |
| `min(a, b)` | 傳回兩個數值中的較小值。 |
| `max(a, b)` | 傳回兩個數值中的較大值。 |
| `len(value)` | 傳回字串、陣列或標準容器的元素數量。多重集合傳回所有元素出現次數總和。 |
| `range(int64 count)` | 接收 `int64` 數量，傳回包含 `0` 到 `count - 1` 的 `array<int>`。負數數量產生空陣列；數量大於 `1000000` 時產生執行階段錯誤。 |

```VeroScript
int main()
{
	array<int> values = range(4);
	print("size={}", len(values));
	print("low={}, high={}, distance={}", min(-2, 5), max(-2, 5), abs(-9));
	return 0;
}
```

`len(value)` 傳回非負 `int64`（`0` 到 `9223372036854775807`）。空值、不支援的型別或無法表示的長度會產生執行階段錯誤。既有容器的大小、容量及元素數量查詢也傳回 `int64`。長度和線性索引宜使用 `int64`；賦值給 `int` 時執行一般數值範圍檢查，超出其範圍則失敗。型別變寬不會取消原生配置或後端容器限制。

## 環境、時間和流程

| 函式 | 說明 |
| --- | --- |
| `std::environment::getenv(name)` | 讀取宿主處理程序環境變數；變數不存在時傳回空字串。名稱不能為空。 |
| `std::environment::setenv(name, value)` | setenv(name, "") 設定存在的空值；刪除使用 unsetenv，刪除不存在項目也成功。try_getenv 區分缺少與空值。名稱不可為空且不能含 = 或 NUL，值不能含 NUL。Windows 環境名稱沿系統規則不區分大小寫，POSIX 則區分。 |
| `std::chrono::now()` | 傳回基於系統時脈的 Unix 時間起點以來的秒數，類型為 `double`。 |
| `std::chrono::steady_now()` | 傳回單調時脈的秒數。測量經過時間時應比較兩次讀取的差值，不要把它當作牆上時鐘時間戳。 |
| `std::this_thread::sleep(milliseconds)` | 讓目前腳本執行緒休眠指定的毫秒數。參數必須為非負整數。 |
| `assert(condition[, message])` | 接收一個 `bool` 條件和可選的 `string` 訊息。條件為 `false` 時報告執行階段錯誤；條件為 `true` 時不產生輸出。 |
| `exit(code)` | 請求腳本以指定的整數退出碼結束執行。 |

```VeroScript
int main()
{
	std::environment::setenv("VERO_MODE", "docs");
	print(std::environment::getenv("VERO_MODE"));

	double started = std::chrono::steady_now();
	std::this_thread::sleep(1);
	assert(std::chrono::steady_now() >= started);
	exit(0);
	return 0;
}
```

`assert` 接收一個或兩個參數。第一個參數必須是 `bool`；如果提供第二個參數，則必須是 `string`。斷言失敗時，非空訊息會包含在診斷中；空訊息使用預設斷言診斷。斷言失敗是執行階段錯誤而不是腳本例外，因此不會被 `catch (...)` 或型別處理器捕捉。

環境變數屬於宿主處理程序狀態。腳本修改環境變數後，是否影響宿主程式的後續邏輯取決於宿主平臺和整合方式。

## 標準輸入

| 函式 | 說明 |
| --- | --- |
| `std::io::read_token()` | 從標準輸入讀取下一個以空白分隔的令牌。 |
| `std::io::read_string()` | 讀取下一個字串令牌。 |
| `std::io::read_int()` | 讀取並解析下一個整數令牌。 |
| `std::io::read_double()` | 讀取並解析下一個浮點數令牌。 |
| `std::io::read_bool()` | 讀取 `true` 或 `false` 並解析為布林值。 |

輸入到達檔案末端或令牌格式不正確時，讀取函式會報告診斷。它們適合簡單的命令列輸入；更複雜的輸入協定應由宿主程式或標準庫檔案介面負責。

## 實作內部名稱

以 `__` 開頭的名稱（例如 `__read_file`、`__format`）是編譯器或執行階段內部輔助函式，不屬於穩定的腳本公共 API。腳本不應直接依賴這些名稱；請使用[標準庫參考](index.md)中記錄的公開函式。

- [標準函式庫基礎](foundation.md)

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/standard-library/builtins.md) · [fr-FR](../../fr-FR/standard-library/builtins.md) · [ja-JP](../../ja-JP/standard-library/builtins.md) · [ko-KR](../../ko-KR/standard-library/builtins.md) · [zh-CN](../../zh-CN/standard-library/builtins.md) · [zh-TW](builtins.md)
