# 模組

模組以來源檔案為邊界組織宣告，並控制宣告的可見性。模組通常透過 `.vs` 檔案表示，使用 `import` 引入，使用 `export` 將宣告提供給其他模組。

## 匯入模組

使用 `import` 匯入原始檔或函式庫套件：

```VeroScript
import "string.vs";

int main()
{
	print(std::strings::join(std::strings::split("a,b,c", ","), "-"));
}
```

同一個模組被多次匯入時，模組身分會抑制重複載入。匯入模組不會把它的私有宣告全部暴露給目前模組；只有被匯出的宣告可以被外部使用。

## 匯出宣告

在宣告前使用 `export`，可以讓其他模組匯入該宣告：

```VeroScript
export struct Box<T>
{
	T value;

	Box(T initial)
	{
		value = initial;
	}

	T get() const
	{
		return value;
	}
};

int hidden_value()
{
	return 99;
}
```

上例中的 `Box` 可以被其他模組使用，而 `hidden_value` 則沒有匯出，不能當作函式庫模組的公開 API。匯出適用於函式、變數、結構體、類別、介面、枚舉和泛型宣告。

## 重新匯出模組

`export import` 匯入一個模組，並把目標模組的匯出宣告繼續提供給目前模組：

```VeroScript
export import "string.vs";
```

其他模組只需要匯入這個門面模組，就可以使用 `string.vs` 匯出的宣告：

```VeroScript
import "string_facade.vs";

string text = std::strings::join(std::strings::split("a,b", ","), "-");
```

重新匯出不會把目標模組的私人宣告變成公開宣告，也不會複製來源檔案文字。

## 模組和命名空間

模組解決原始檔案之間的依賴和可見性問題；命名空間解決同一模組內的名稱組織問題。一個模組可以宣告多個命名空間，也可以從其他模組匯入命名空間中的公開宣告。

## 匯入函式庫套件

來源模組和二進位函式函式庫套件使用相同的 `import` 語法：

```VeroScript
import "DemoLibrary.vslib";
```

函式庫套件只能提供已經匯出的宣告。套件的建置、檢查和發佈方式請見[套件與發佈](../../../../../../VeroScriptCli/Resources/Docs/User/zh-TW/packages.md)。

## 公開巨集

請參閱[預處理器 — 公開巨集](preprocessor.md#exported-macro-contract)。

巢狀型別、區域宣告、存取規則與分段泛型實參請參閱[型別範圍](scope.md)。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/modules.md) · [fr-FR](../../fr-FR/language-basics/modules.md) · [ja-JP](../../ja-JP/language-basics/modules.md) · [ko-KR](../../ko-KR/language-basics/modules.md) · [zh-CN](../../zh-CN/language-basics/modules.md) · [zh-TW](modules.md)
