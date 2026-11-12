# 基本語法

本文說明 VeroScript 原始檔的基本結構，以及語句、程式碼區塊、識別碼和關鍵字的寫法。

## 原始檔

VeroScript 原始檔使用 `.vs` 副檔名。一個原始檔案可以包含全域變數、函式、結構體、類別、介面、枚舉、命名空間和模組宣告。

應用程式腳本通常提供 `main` 入口；庫模組可以只匯出類型和函式，不提供入口函式。 `main` 可以回傳 `int` 或 `void`，建議回傳 `int`，以便明確表示處理程序退出碼。

```VeroScript
int global_value = 1;

int add(int left, int right)
{
	return left + right;
}

int main()
{
	print(add(global_value, 2));
	return 0;
}
```

## 推薦書寫規範

文件中的 VeroScript 程式碼片段遵循以下格式：每級使用一個 Tab 字元縮排；左花括號獨佔一行；普通語句一行一條；`class`、`struct`、`interface` 和 `enum` 類型宣告使用結尾分號。語言也接受部分省略分號的簡寫形式，但文件程式碼建議保持完整寫法。獨立範例檔案遵循 examples README 中說明的格式化器風格。

## 語句和分號

普通語句以分號結束：

```VeroScript
int value = 1;
value = value + 1;
print(value);
```

函式宣告、型別宣告和控制流程語句的程式碼區塊使用花括號。程式碼區塊中的語句可以跨越多行，空格和換行通常不會改變語意。

```VeroScript
if (value > 0)
{
	print("positive");
}
```

## 識別字

識別字用於命名變數、函式、類型、欄位、命名空間和模組成員。識別字只能由 ASCII 字母、數字和底線組成，且不能以數字開頭：

```VeroScript
int item_count = 3;
string display_name = "VeroScript";
```

識別字區分大小寫。 `item_count`、`ItemCount` 和 `itemCount` 是三個不同的名稱。

## 關鍵字

關鍵字具有語言規定的含義，不能作為普通識別字使用。關鍵字包括：

```text
void bool char short int long float double string wstring
int8 uint8 int16 uint16 int32 uint32 int64 uint64
unsigned auto channel array map multimap set multiset
queue priority_queue stack ordered_map ordered_multimap deque ordered_multiset
fn function
if else while do for switch case default return break continue fallthrough goto
try catch finally throw struct class interface enum
import export using typedef namespace friend
new delete this const ref static explicit
public protected private virtual override final
where operator template in
true false nullptr
```

關鍵字清單屬於語言語法的一部分。新增關鍵字可能使原本合法的識別碼失效，因此庫代碼不應使用關鍵字作為公共名稱。

`do` 現已保留給 `do ... while`；舊原始碼若曾將 `do` 用作識別碼，需要改名。

## 同型別宣告群組與原始字串

在頂層、命名空間和區域宣告中，當多個變數具有相同的完整前置型別及在該宣告位置有效的相同 `const` / `ref` 限定詞時，一個宣告可以引入它們。初始化式從左到右求值，名稱也依相同順序變得可見：

```VeroScript
const int first = 1, second = first + 1;
for (int index = 0, end = 3; index < end; index++)
{
	print(index);
}
```

每一項都保有自己的初始化式並接受一般的宣告檢查。特別是，`auto first = 1, second = 2;` 會被刻意拒絕；也不能用每個名稱後的陣列後綴宣告不同形狀，應將共用的陣列長度寫在型別前綴中。

UTF-8 原始字串使用固定形式 `r"..."`。其中的反斜線和換行會原樣保留，不進行逸出處理：

```VeroScript
string path = r"C:\tools\new";
string text = r"first line
second line";
```

後續遇到的第一個 `"` 即結束該字面量，因此原始文字中不能包含引號字元。目前沒有寬原始字串、自訂定界符或字串插值。

## 有意保留的語法邊界

VeroScript 拒絕單獨的空陳述式 `;`，有意使用空陳述式主體時請寫 `{}`。語言不支援通用逗號運算式、`f(value: 1)` 形式的具名引數、推導式 `auto` 回傳類型、結構化繫結，以及 `sizeof`、`alignof` 等 C++ 配置運算子。宣告群組只適用於同型別宣告；請使用明確回傳類型以及語言或容器 API。

## 剖析器安全限制

運算式、單元運算式和前處理器 `#if` 運算式的巢狀深度最多為 256 層；陳述式巢狀
最多為 32 層，運算式鏈也最多包含 256 個連結。超過限制時，編譯會回報
`expression nested too deeply`、`statement nested too deeply`、`#if expression
nested too deeply` 或 `expression chain is too long` 等診斷。對於自動產生的
深層程式碼，請拆分運算式與控制流程區塊。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/basic-syntax.md) · [fr-FR](../../fr-FR/language-basics/basic-syntax.md) · [ja-JP](../../ja-JP/language-basics/basic-syntax.md) · [ko-KR](../../ko-KR/language-basics/basic-syntax.md) · [zh-CN](../../zh-CN/language-basics/basic-syntax.md) · [zh-TW](basic-syntax.md)
