# 字串

VeroScript 提供 `string` 和 `wstring` 兩種文字類型。兩者都表示文字值，但屬於不同的類型，不能在沒有明確轉換的情況下混合賦值、比較或拼接。本頁說明字串的語言行為以及 `string.vs` 提供的常用輔助函式；字串字面量的寫法見[字面量](literals.md)。

## 文字類型

普通字串字面量的類型是 `string`，寬字串字面量的類型是 `wstring`：

```VeroScript
string name = "VeroScript";
wstring title = L"Language reference";
```

`string` 與 `wstring` 都支援 `len`、相等比較和同類型拼接。拼接時，操作數必須屬於同一種文字類型：

```VeroScript
string message = "Hello, " + name;
wstring heading = L"Vero" + L"Script";
```

如果需要在兩種文字類型之間轉換，則應明確呼叫 `to_wstring` 或 `to_string`：

```VeroScript
wstring wide_name = to_wstring(name);
string narrow_name = to_string(wide_name);
```

數值和布林值轉換為文字時，也應使用 `to_string`，而不是依賴字串與其他類型的隱式拼接：

```VeroScript
string summary = "count=" + to_string(3);
```

## 字串標準庫

字串輔助函式位於 `string.vs`。使用前匯入此模組：

```VeroScript
import "string.vs";
```

常用函式如下：

| 函式 | 說明 |
| --- | --- |
| `std::strings::substring(s, start, count)` | 從 `start` 開始截取指定數量的字元 |
| `std::strings::find(s, needle)` | 尋找子字串，傳回位置；未找到時傳回負值 |
| `std::strings::char_code(s, index)` | 傳回指定位置的字元編碼 |
| `std::strings::contains(s, sub)` | 判斷是否包含子字串 |
| `std::strings::starts_with(s, prefix)` | 判斷是否以指定字首開頭 |
| `std::strings::ends_with(s, suffix)` | 判斷是否以指定字尾結尾 |
| `std::strings::trim_left(s)` / `std::strings::trim_right(s)` | 刪除左側或右側的空白字元 |
| `std::strings::trim(s)` | 刪除兩側的空白字元 |
| `std::strings::split(s, separator)` | 以分隔符號拆分為 `array<string>` |
| `std::strings::join(parts, separator)` | 使用分隔符號連接字串陣列 |
| `std::strings::to_lower(s)` / `std::strings::to_upper(s)` | 轉換為小寫或大寫 |
| `std::strings::replace(s, from, to)` | 將符合的子字串替換為指定文字 |
| `std::strings::last_find(s, needle)` | 傳回最後一次符合的 UTF-8 位元組偏移；不存在時傳回 `-1` |
| `std::strings::repeat(s, count)` | 重複 `s`；負數 count 會產生執行階段錯誤 |
| `std::strings::remove_prefix(s, prefix)` / `std::strings::remove_suffix(s, suffix)` | 刪除符合的字首或字尾，否則傳回 `s` |

這些函式以值的形式傳回結果，不會修改傳入的原始字串。例如：

```VeroScript
import "string.vs";

string raw = "  red,green,blue  ";
array<string> colors = std::strings::split(std::strings::trim(raw), ",");
string normalized = std::strings::join(colors, " | ");
print(normalized);
```

`trim_left`、`trim_right` 和 `trim` 移除 ASCII 空白位元組 `0x09` 到 `0x0D` 及空格（`0x20`）。字串索引是從零開始的 UTF-8 位元組偏移，不是 Unicode 碼點索引。`len`、`find` 和 `last_find` 傳回 `int64`，搜尋無結果時為 `-1`；切片位置與數量、`char_code` 索引和 `repeat` 次數使用 `int64`。邊界及傳回型別請見[字串模組](../standard-library/string.md)。

## 格式化

VeroScript 提供兩套格式化語法。`format` 使用花括號欄位，`formatf` 和 `printf` 使用 printf 風格的轉換說明。兩套語法相互獨立，不要在同一個格式字串中混用。

### 花括號格式化

`string.vs` 的公開簽名是：

```text
string format(string fmt, array<string> args);
```

在腳本中通常直接傳入標量參數。編譯器會將 `string`、布林值、浮點數以及有符號或無符號整數轉換為字串並收集到參數陣列中：

```VeroScript
import "string.vs";

string message = format("name={}, score={}", "Ada", 97);
print(message);
```

如果已經有 `array<string>`，也可以把它當成第二個參數直接傳入：

```VeroScript
import "string.vs";

array<string> args = array<string>();
args.push("Ada");
args.push("97");
string message = format("name={}, score={}", args);
```

欄位的基本形式如下：

| 寫法 | 意義 |
| --- | --- |
| `{}` | 使用下一個自動參數，索引從 `0` 開始。 |
| `{N}` | 使用明確的零基底參數索引，例如 `{0}`。 |
| `{:spec}` | 使用下一個自動參數，並套用格式說明。 |
| `{N:spec}` | 使用明確索引並套用格式說明。 |
| `{{` / `}}` | 輸出字面量 `{` / `}`。 |

格式說明的語法為 `[alignment][width][.precision][type]`：

| 部分 | 可用值 | 說明 |
| --- | --- | --- |
| 對齊 | `<`、`>`、`^` | 分別表示左對齊、右對齊、居中；省略時預設右對齊。 |
| 寬度 | 十進位整數 `0`–`1024` | 結果不足時使用空格填滿；寬度不會截斷內容。 |
| 精確度 | `.decimal-integer`，範圍 `0`–`1024` | 將參數以有限數值解析，並以固定小數位輸出。 |
| 類型 | `x`、`X` | 將十進位整數轉換為小寫或大寫十六進位。 |

`format` 的 `x`/`X` 轉換依目前執行階段的有符號 32 位元十進位整數規則解析，再輸出其 32 位元十六進位表示法；需要處理完整無符號 64 位元範圍時，應使用 `formatf` 的 `%x` 或 `%X`。

例如：

```VeroScript
import "string.vs";

string right = format("{:>6}", "ok");   // "    ok"
string left = format("{:<6}", "ok");    // "ok    "
string center = format("{:^6}", "ok");  // "  ok  "
string fixed = format("{:.2}", "1.5");  // "1.50"
string hex = format("{:X}", "255");     // "FF"
string repeated = format("{0} / {0}", "same");
string braces = format("{{value}}", array<string>()); // "{value}"
```

自動索引只會在自動欄位中前進。為了避免 `{0}`、`{}` 這類寫入產生意外結果，應在同一個格式字串中統一使用自動索引或明確索引。參數不足、花括號不符、格式說明非法，或數值格式無法解析參數時，會產生執行階段錯誤。未被欄位使用的多餘參數不會自動追加到結果中。

### printf 格式化

`formatf` 的公開簽名是：

```text
string formatf(string fmt, array<string> args);
```

腳本呼叫時同樣可以直接傳入標量參數。 `printf` 使用相同的格式語法，但直接寫入標準輸出。轉換說明的基本形式為：

```text
%[flags][width][.precision][length]conversion
```

| 部分 | 可用值或限制 |
| --- | --- |
| 標誌 | `-` 左對齊、`+` 顯示正號、空格顯示正數前的空格、`#` 使用替代前綴、`0` 使用零填充。 |
| 寬度 | 十進制整數，範圍 `0`–`1024`；不支援動態寬度 `*`。 |
| 精確度 | `.digits`，範圍 `0`–`1024`；不支援動態精確度 `.*`。 |
| 長度 | `h`、`hh`、`l`、`ll`、`j`、`z`、`t` 皆可接受，但不會改變 VeroScript 值的寬度或 ABI。 |
| 轉換符 | `d`、`i`、`u`、`o`、`x`、`X`、`f`、 `F`、`e`、`E`、`g`、`G`、`c`、`s`。 |

各轉換符的參數要求如下：

| 轉換符 | 參數與結果 |
| --- | --- |
| `%d`、`%i` | 有符號整數。 |
| `%u` | 非負整數。 |
| `%o` | 八進位整數。 |
| `%x`、`%X` | 小寫或大寫十六進位整數。 |
| `%f`、`%F` | 定點浮點數；預設精確度為 `6`。 |
| `%e`、`%E` | 科學計數法；大寫版本使用大寫字母。 |
| `%g`、`%G` | 預設浮點表示；大寫版本使用大寫字母。 |
| `%c` | `0`–`255` 的整數，或單字節字串；不接受精確度。 |
| `%s` | 字串；精確度表示最多輸出的位元組數。 |
| `%%` | 輸出字面量 `%`，不消耗參數。 |

`%c` 和 `%s` 只允許 `-` 標誌和寬度；數字轉換的精度優先於 `0` 零填充。格式字串末尾殘留 `%`、參數不足、轉換符不受支援、參數類型無法解析，都會產生執行階段錯誤。浮點轉換要求參數是有限數值。沒有被轉換符消耗的多餘參數會被忽略。

```VeroScript
import "string.vs";

string text = formatf("d=%+06d x=%#08X f=%.2f s=%-5.3s c=%c", -42, 255, 3.14159, "abcdef", 65);
printf("%s%% done", text);
```

### 與輸出函式的關係

```VeroScript
import "string.vs";

print("name={}, score={}", "Ada", 97); // print's multi-argument form uses braces
printf("score=%03d", 7);                // printf always uses printf syntax
string value = format("score={}", 7);   // use format when a string is needed
```

`print` 只有一個待輸出值時直接輸出；傳入格式字串和額外參數時使用花括號格式。 `format`/`formatf` 傳回字串，`printf` 直接寫入標準輸出。 `format`、`formatf` 和 `printf` 不接受參考參數或 `void` 運算式作為格式參數。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/strings.md) · [fr-FR](../../fr-FR/language-basics/strings.md) · [ja-JP](../../ja-JP/language-basics/strings.md) · [ko-KR](../../ko-KR/language-basics/strings.md) · [zh-CN](../../zh-CN/language-basics/strings.md) · [zh-TW](strings.md)
