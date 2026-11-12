# 常數

常數是不能透過指定名稱修改的變數或成員。 VeroScript 使用 `const` 宣告常數。

`const` 表示唯讀，不要求編譯期初始化。需要支援範圍內的純量值在編譯時確定時，使用 [constexpr](../language-reference/constexpr.md)。

## 常數變數

`const` 可以用於局部變數和模組級變數。常數必須在宣告時初始化：

```VeroScript
const int max_count = 100;

int main()
{
	const string product_name = "VeroScript";
	print(product_name);
}
```

常數初始化完成後，就不能再透過該名稱賦值：

```VeroScript
const int limit = 10;
// limit = 20; // Compile error.
```

常量的類型仍然是宣告中指定的類型。 `const` 不會把值轉換成另一種類型，也不會改變其作用域。

## 常數成員

結構體或類別可以宣告常數欄位：

```VeroScript
class Settings
{
public:
	const int version = 1;
};
```

沒有宣告初始化器的非靜態欄位，也可以改由建構初始化列表初始化：

```VeroScript
class Settings
{
public:
	const int version;

	Settings(int initial_version)
		: version(initial_version)
	{
	}
};
```

初始化列表只能命名目前型別直接宣告的執行個體欄位。建構初始化列表中的直接基底類別項目會先執行；接著，該列表中的欄位項目依欄位宣告順序執行，最後才執行建構函式主體。已有宣告初始化器的欄位不能再次出現在建構初始化列表中，同一欄位也不能重複出現。建構函式主體內的寫入仍屬於一般指派，因此不能修改 `const` 欄位。宣告初始化器以及列表中省略的欄位仍套用既有的預設建構規則。

## `const` 成員函式

成員函式可以在參數列表後使用 `const`，表示函式不能修改目前物件的可變狀態：

```VeroScript
class Counter
{
public:
	int value = 0;

	int get() const
	{
		return value;
	}
};
```

`const` 成員函式適合用於查詢物件狀態。是否能夠呼叫某個成員函式，由目前物件的常數屬性和函式的 `const` 修飾共同決定。

### `mutable` 例外

```VeroScript
struct Label
{
	string source;
	mutable string cached;

	string text() const
	{
		if (cached == "")
		{
			cached = source;
		}
		return cached;
	}
};
```

此例外只適用於標記的欄位。它不會讓 const 物件整體可寫，也不會保證並行存取安全。`mutable` 欄位不能同時是 `const`、`static` 或參考欄位。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/constants.md) · [fr-FR](../../fr-FR/language-basics/constants.md) · [ja-JP](../../ja-JP/language-basics/constants.md) · [ko-KR](../../ko-KR/language-basics/constants.md) · [zh-CN](../../zh-CN/language-basics/constants.md) · [zh-TW](constants.md)
