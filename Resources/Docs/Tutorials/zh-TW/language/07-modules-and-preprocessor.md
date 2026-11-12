# 07：模組、匯入和預處理器

當程式超過一個檔案時，模組負責組織依賴和公開 API；預處理器則適合在編譯前根據條件選擇程式碼。本篇先做一個私有實作，再透過 `export` 公開需要的函式。

## 第一步：建立模組

建立 `math_utils.vs`：

```VeroScript
export int square(int value)
{
	return value * value;
}

int hidden_value()
{
	return 99;
}
```

`square` 前面的 `export` 表示它是模組的公開宣告；`hidden_value` 沒有匯出，其他模組不能把它當作公開 API 使用。

再建立 `main.vs`：

```VeroScript
import "math_utils.vs";

int main()
{
	print(square(7));
	return 0;
}
```

在兩個檔案所在目錄執行：

```powershell
veroscript --check main.vs
veroscript main.vs
```

## 第二步：使用重新匯出

如需提供統一的匯入入口，可以建立 `facade.vs`：

```VeroScript
export import "math_utils.vs";
```

然後讓入口檔案只匯入門面：

```VeroScript
import "facade.vs";

int main()
{
	print(square(8));
	return 0;
}
```

重新匯出只會繼續公開原模組已經匯出的宣告，不會把 `hidden_value` 變成公用函式。

## 第三步：加入編譯條件

預處理器在編譯時選擇參與的來源程式碼；執行階段判斷應使用普通的 `if`。例如：

```VeroScript
#define MESSAGE "development"

#ifdef RELEASE
#define MESSAGE "release"
#endif

int main()
{
	print(MESSAGE);
	return 0;
}
```

執行階段傳入 `-D RELEASE`：

```powershell
veroscript --check main.vs
veroscript -D RELEASE main.vs
```

宏在編譯前展開。巨集名稱和函式名稱不要隨意重名，複雜邏輯優先寫成真正的函式。

## 第四步：使用匿名命名空間組織宣告

匿名命名空間寫作 `namespace { ... }`，可以在不增加額外限定的情況下組織宣告。在 VeroScript 中它是透明的，不會建立合成命名空間，也不提供 C++ 式的內部連結。

```VeroScript
namespace
{
	int local_helper()
	{
		return 6;
	}
}

int main()
{
	print(local_helper());
	return 0;
}
```

在具名命名空間內部，成員會保留外層命名空間限定：

```VeroScript
namespace Diagnostics
{
	namespace
	{
		int code()
		{
			return 200;
		}
	}
}

int main()
{
	print(Diagnostics::code());
	return 0;
}
```

如果其他模組需要使用某個宣告，應在該宣告上單獨使用 `export`。匿名命名空間本身不會被匯出，也不會僅憑自身將宣告變為私有。

## 練習

1. 匯出一個 `cube` 函式，並從主模組呼叫。
2. 建立一個門面模組，同時重新匯出字串模組和自己的數學模組。
3. 增加 `#if` 分支，讓偵錯版本列印額外資訊。
4. 在具名命名空間內部的匿名命名空間中放置一個輔助函式，並透過外層命名空間限定呼叫它。

模組細節見[模組](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/modules.md)、[預處理器](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/preprocessor.md)和[命名空間](../../../../../VeroScript/Resources/Docs/User/zh-TW/language-basics/namespaces.md)。可對照 [`examples/reexport_main.vs`](../../../../../VeroScript/Resources/Scripts/examples/reexport_main.vs) 和 [`examples/preprocessor.vs`](../../../../../VeroScript/Resources/Scripts/examples/preprocessor.vs)。

下一篇進入標準函式庫，用腳本處理真實文字和檔案：[字串、檔案和路徑](../standard-library/08-strings-files-and-paths.md)。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/language/07-modules-and-preprocessor.md) · [fr-FR](../../fr-FR/language/07-modules-and-preprocessor.md) · [ja-JP](../../ja-JP/language/07-modules-and-preprocessor.md) · [ko-KR](../../ko-KR/language/07-modules-and-preprocessor.md) · [zh-CN](../../zh-CN/language/07-modules-and-preprocessor.md) · [zh-TW](07-modules-and-preprocessor.md)
