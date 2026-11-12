# 字面量

字面量是在原始碼中直接寫出的值。 VeroScript 支援整數、浮點數、字元、字串、寬字串、布林值和 `nullptr` 字面量。

## 整數字面量

不含前綴的整數以十進位解析；`0x`/`0X` 表示十六進位，`0b`/`0B` 表示二進位：

```VeroScript
int decimal = 42;
int hexadecimal = 0x2A;
int binary = 0b101010;
```

整數可以使用後綴明確取值範圍或符號屬性：

```VeroScript
unsigned int unsigned_value = 100u;
long long_value = 100L;
int64 wide_value = 9223372036854775807i64;
uint64 unsigned_wide_value = 18446744073709551615u64;
```

支援的整數字尾包含 `u`、`l`、`ul`、`lu`、`i64` 和 `u64` 的大小寫形式。`l`/`L` 表示語言中的 64 位元 `long`，`i64`/`u64` 直接選擇 64 位元有符號/無符號字面量；其他無後綴或 `u` 字面量使用 32 位元路徑。字面量的值必須能夠表示在目標類型中。

底線只能分隔目前進位下兩個合法數字，浮點數的小數和指數數字中也可以使用：`1_000`、`0xFF_FF`、`0b1010_0101`、`1_2.5_0e1` 均合法。底線不能緊鄰進位前綴、小數點、指數標記或符號、類型字尾或另一個底線。前導零不表示八進位；`010` 仍是十進位十。

## 浮點字面量

包含小數點或指數部分的數字會被解析為浮點字面量：

```VeroScript
double pi_approximation = 3.14159;
double scientific = 1.25e2;
double half = .5;
float small = .5f;
double fifty = .5e2;
```

當小數點後面緊跟十進位數字時，可以省略整數部分。因此，`.5`、`.5f` 和 `.5e2` 都是有效的浮點字面量，分別表示 `double` 類型的 `0.5`、`float` 類型的 `0.5` 和 `double` 類型的 `50.0`。單獨的 `.` 仍然是成員存取標點，`i64` 等整數後綴不能用於這種寫法。

指數部分使用 `e` 或 `E`，後面可以帶 `+` 或 `-`。 `f` 或 `F` 後綴用於表示 `float` 字面量；無後綴字面量是 `double`。

十進位浮點常值使用 `.`，不受宿主地區設定影響。`f`／`F` 常值直接捨入至 binary32，無後綴常值捨入至 binary64。溢位及超出可接受 binary64 範圍的值是編譯錯誤；可由 `double` 表示但對 `float` 過小的值可能捨入為零，例如 `1e-100f`。這項常值規則不表示執行期文字剖析 API 也接受下溢至零。

## 字元字面量

字元字面量的靜態類型是 `char`，即語言中的有符號 8 位元整數類型：

```VeroScript
char letter = 'A';
char newline = '\n';
char byte_ff = '\xFF';
```

直接字元只能是一個 ASCII 位元組。`\xNN` 精確寫入一個 8 位元位元模式，因此 `\x80` 到 `\xFF` 對應負的 `char` 值。空字元、多字元、直接非 ASCII 字元、Unicode 逸出以及 `L'A'` 等寬字元形式都會被拒絕。

## 字串和寬字串

普通字串使用雙引號，寬字串在雙引號前加 `L`：

```VeroScript
string text = "VeroScript";
wstring wide_text = L"text";
```

字串字面量支援轉義字符，例如換行和引號：

```VeroScript
string message = "first line\nsecond line";
string quoted = "say \"hello\"";
```

| 逸出 | `string` | `wstring` | `char` |
| --- | --- | --- | --- |
| `\\`、`\"`、`\'` | 逸出反斜線或引號 | 逸出反斜線或引號 | 逸出反斜線或引號 |
| `\0`、`\a`、`\b`、`\f`、`\n`、`\r`、`\t`、`\v` | 一個位元組 | 一個 UTF-16 code unit | 一個位元組 |
| `\xNN` | 精確附加一個原始位元組 | 附加 `U+00NN` | 一個 8 位元位元模式 |
| `\uNNNN`、`\UNNNNNNNN` | Unicode 純量編碼為 UTF-8 | Unicode 純量編碼為 UTF-16 | 不支援 |

未知或不完整的逸出是編譯錯誤。Unicode 逸出拒絕代理項和超過 `U+10FFFF` 的值。`string` 是帶長度的位元組序列：原始碼文字和文字 API 使用 UTF-8，但 `\0` 與 `\xNN` 可以產生內嵌 NUL 或任意位元組，因此不能保證每個執行期 `string` 都是合法 UTF-8。`wstring` 是帶長度的 UTF-16 code-unit 序列。明確 UTF-8/UTF-16 轉換遇到非法輸入會報錯，不會靜默替換。

字串字面量不能跨越原始碼行。 `string` 和 `wstring` 是不同的類型，不能直接混合運算；需要時應使用明確轉換函式。

陣列、Map、Set 和明確類型的大括號初始化器可以帶一個尾隨逗號，例如 `{1, 2,}` 或 `{'A': 1,}`。空初始化器仍寫作 `{}`。前導逗號、連續逗號和函式引數中的尾隨逗號不受支援。

## 布林值和空值

布林字面量只有 `true` 和 `false`：

```VeroScript
bool enabled = true;
bool finished = false;
```

`nullptr` 表示空物件控制代碼或空值，常用於類別物件和可為空的執行階段值：

```VeroScript
class Node
{
public:
	int value;
};

int main()
{
	Node node = nullptr;
	if (node == nullptr)
	{
		print("empty");
	}
}
```

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/literals.md) · [fr-FR](../../fr-FR/language-basics/literals.md) · [ja-JP](../../ja-JP/language-basics/literals.md) · [ko-KR](../../ko-KR/language-basics/literals.md) · [zh-CN](../../zh-CN/language-basics/literals.md) · [zh-TW](literals.md)
