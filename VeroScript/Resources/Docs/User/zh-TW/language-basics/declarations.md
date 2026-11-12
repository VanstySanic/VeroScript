# 類型別名與特殊宣告

VeroScript 支援型別別名，以及使用 `= default` 和 `= delete` 明確控制建構函式、析構函式、方法、運算子或自由函式的宣告狀態。

## 類型別名

使用 `using` 為已有類型建立別名：

```VeroScript
using Ints = array<int>;
using Scores = map<string, int>;

Ints values = Ints{1, 2, 3};
Scores scores = Scores();
```

`typedef` 也可以用於型別別名：

```VeroScript
typedef Names = array<string>;
Names names = Names();
```

類型別名不會建立新的執行階段類型。別名可以在模組作用域或局部程式碼區塊中宣告，局部別名只在其作用域內可見。

## 匯出型別別名

在模組作用域（包括命名空間內）使用 `export using` 或 `export typedef`。兩者都採用 `Alias = Type` 寫法。一般別名僅在所屬模組內可見，區域別名僅在所屬作用域內可見。匯入端可以使用帶命名空間限定的公開別名；`export import` 會重新匯出公開別名，不會公開私有別名。`using namespace` 只影響所屬模組與作用域的名稱查找，`export using namespace` 會被拒絕。

儲存為 aliases.vs：

```VeroScript
namespace Measures
{
	export using Count = int;
	export typedef Label = string;
	using InternalCount = int;
}
```

```VeroScript
import "aliases.vs";

int main()
{
	Measures::Count count = 42;
	Measures::Label label = "total";
	print(count);
	return 0;
}
```

將使用端儲存為 main.vs，執行 `veroscript main.vs`，輸出 `42`。也可以改為匯入包含 `export import "aliases.vs";` 的門面模組。需要散發二進位程式庫時，執行 `veroscript --emit-package aliases.vslib aliases.vs`，再將使用端的匯入目標改為 `"aliases.vslib"`。

## 預設實作

建構函式和析構函式可以使用 `= default` 請求編譯器產生預設實作：

```VeroScript
struct Token
{
	int value = 7;

	Token() = default;
	~Token() = default;
};
```

預設構造會結合欄位預設值和類型的預設規則。明確寫出 `= default` 可以表達「使用產生實作」的意圖，並使宣告出現在類型的公開介面中。

## 刪除宣告

使用 `= delete` 可以保留一個簽名，同時明確禁止該呼叫形式：

```VeroScript
struct Token
{
	Token() = default;
	Token(string text) = delete;

	int read() const
	{
		return 7;
	}
	int read(string mode) const = delete;
};
```

呼叫被刪除的建構函式、方法、運算子或函式會產生編譯診斷。刪除宣告常用於阻止不希望支援的隱式呼叫或錯誤類型重載；它不會在執行階段執行一個空函式。

`= default` 和 `= delete` 是宣告的一部分，不能與相同宣告的函式體同時使用。虛函式或介面槽位仍必須滿足繼承和實作規則。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/declarations.md) · [fr-FR](../../fr-FR/language-basics/declarations.md) · [ja-JP](../../ja-JP/language-basics/declarations.md) · [ko-KR](../../ko-KR/language-basics/declarations.md) · [zh-CN](../../zh-CN/language-basics/declarations.md) · [zh-TW](declarations.md)
