# 字串模組

模組檔案：`string.vs`

匯入方式：

```VeroScript
import "string.vs";
```

## 查詢和截取

| 函式 | 說明 |
| --- | --- |
| `std::strings::substring(string s, int64 start, int64 count)` | 從位元組偏移 `start` 起，傳回至多 `count` 個 UTF-8 位元組。 |
| `std::strings::find(string haystack, string needle)` | 以 `int64` 傳回首個符合的 UTF-8 位元組偏移，未找到時為 `-1`。 |
| `std::strings::char_code(string s, int64 index)` | 以 `int` 傳回 `index` 處的位元組值（`0`–`255`）。 |
| `std::strings::contains(string s, string sub)` | 判斷是否包含子字串。 |
| `std::strings::starts_with(string s, string prefix)` | 判斷是否以指定字首開頭。 |
| `std::strings::ends_with(string s, string suffix)` | 判斷是否以指定字尾結尾。 |

字串索引是從 `0` 開始的 UTF-8 位元組偏移，而不是 Unicode 碼點索引。
索引超出範圍時會產生執行階段錯誤。`std::strings::find`、`std::strings::last_find`、`std::strings::substring` 和
`std::strings::char_code` 使用相同的位元組偏移約定。

## 拆分、連接和變換

| 函式 | 說明 |
| --- | --- |
| `std::strings::split(string s, string separator)` | 以分隔符號拆分為 `array<string>`。 |
| `std::strings::join(array<string> parts, string separator)` | 使用分隔符號連接字串陣列。 |
| `std::strings::trim_left(string s)` | 移除左側 ASCII 空白：製表、換行、垂直製表、換頁、回車和空格。 |
| `std::strings::trim_right(string s)` | 刪除右側空白字元。 |
| `std::strings::trim(string s)` | 刪除兩側空白字元。 |
| `std::strings::to_lower(string s)` | 轉換為小寫。 |
| `std::strings::to_upper(string s)` | 轉換為大寫。 |
| `std::strings::replace(string s, string from, string to)` | 取代符合的子字串；`from` 為空時傳回原字串。 |
| `std::strings::last_find(string haystack, string needle)` | 傳回最後一次符合的 UTF-8 位元組偏移；不存在時傳回 `-1`，空 needle 傳回 `len(haystack)`。 |
| `std::strings::repeat(string value, int64 count)` | 將字串重複 `count` 次；負數 count 會產生執行階段錯誤。 |
| `std::strings::remove_prefix(string value, string prefix)` | 存在 `prefix` 時移除它，否則傳回原字串。 |
| `std::strings::remove_suffix(string value, string suffix)` | 存在 `suffix` 時移除它，否則傳回原字串。 |

## 格式化

本模組提供兩套格式化函式：`format` 使用大括號欄位，`formatf` 使用 printf 風格格式。兩套格式語法相互獨立，不應混用。

公開簽名為：

```text
string format(string fmt, array<string> args);
string formatf(string fmt, array<string> args);
```

腳本中可以直接傳入 `string`、布林值、浮點數以及有符號或無符號整數；編譯器會把這些標量轉換為字串並收集到 `array<string>`。如果已有參數陣列，也可以將 `array<string>` 作為第二個參數直接傳入。

```VeroScript
import "string.vs";

string text = format("value={}, name={}", 42, "VeroScript");
string hex = format("{:8X}", 255);
string legacy = formatf("value=%d", 42);
```

### `format` 欄位

| 寫法 | 作用 |
| --- | --- |
| `{}` | 使用下一個自動參數。 |
| `{N}` | 使用零基底顯式索引。 |
| `{:spec}` / `{N:spec}` | 使用格式說明；格式為 `[alignment][width][.precision][type]`。 |
| `{{` / `}}` | 輸出字面量大括號。 |

對齊符為 `<`、`>`、`^`，預設右對齊；寬度和精確度範圍皆為 `0`–`1024`，使用空格填入。類型只支援 `x` 和 `X`，分別輸出小寫和大寫十六進位。精度會以固定小數位解析有限數值。自動索引和明確索引不應混用；參數不足、括號不符或格式說明非法會產生執行階段錯誤。

<a id="integer-formatting"></a>

format 的 x/X 完整支援 64 位元十進位整數：正數可達 UINT64_MAX，負數可達 INT64_MIN，負數依 64 位元二補數輸出，因此 -1 為 16 個 f。printf 整數也直接以 64 位元解析，不經過 double；寬度與精確度仍限制為 1024。

### `formatf` 轉換

轉換形式為 `%[flags][width][.precision][length]conversion`。支援的標誌是 `-`、`+`、空格、`#`、`0`；寬度和精度範圍均為 `0`–`1024`，不支援動態 `*`。長度修飾符 `h`、`hh`、`l`、`ll`、`j`、`z`、`t` 可用於來原始碼相容，但不會改變 VeroScript 值的寬度或 ABI。

支援的轉換符為：

| 轉換符 | 說明 |
| --- | --- |
| `%d`、`%i` | 有符號整數。 |
| `%u`、`%o`、`%x`、`%X` | 非負十進位、八進位或大小寫十六進位整數。 |
| `%f`、`%F`、`%e`、`%E`、`%g`、`%G` | 浮點數；未指定精確度時預設使用 `6`。 |
| `%c` | `0`–`255` 的整數或單字節字串；不接受精確度。 |
| `%s` | 字串；精度限制輸出的位元組數。 |
| `%%` | 輸出字面量 `%`，不消耗參數。 |

`printf` 使用 `formatf` 的同一套轉換語法並直接寫入標準輸出；`print` 的多參數形式使用 `format` 的花括號語法。沒有被轉換符消耗的多餘參數會被忽略。完整規則與錯誤邊界見[字串語言行為](../language-basics/strings.md#%E6%A0%BC%E5%BC%8F%E5%8C%96)。
`std::strings::to_lower` 和 `std::strings::to_upper` 只轉換 ASCII 英文字母，其餘 UTF-8 位元組保持不變；不執行 Unicode 大小寫轉換。
`std::strings::trim_left`、`std::strings::trim_right` 和 `std::strings::trim` 會移除兩端的 ASCII `0x09`～`0x0D` 及空格 (`0x20`)；中間及非 ASCII 位元組保持原樣。

`len(s)`、`std::strings::find` 和 `std::strings::last_find` 傳回 `int64`，搜尋無結果時為 `-1`。`std::strings::substring` 使用 `int64` 位元組偏移和數量：`start` 可以等於 `len(s)`，`count` 須非負，超過剩餘位元組數時擷取至字串結尾。`std::strings::char_code` 接收 `int64` 位元組索引，傳回 `int` 位元組值（`0`–`255`）。`std::strings::repeat` 接收 `int64` 次數，結果長度超出 `int64` 範圍時拒絕執行；實際配置限制仍適用。

- [標準函式庫基礎](foundation.md)

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/standard-library/string.md) · [fr-FR](../../fr-FR/standard-library/string.md) · [ja-JP](../../ja-JP/standard-library/string.md) · [ko-KR](../../ko-KR/standard-library/string.md) · [zh-CN](../../zh-CN/standard-library/string.md) · [zh-TW](string.md)
