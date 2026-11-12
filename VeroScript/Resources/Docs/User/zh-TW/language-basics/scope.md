# 作用域

作用域決定一個名稱在哪些原始碼位置有效。 VeroScript 的常見作用域包括模組作用域、命名空間作用域、函式作用域、程式碼區塊作用域和成員作用域。

## 程式碼區塊作用域

在函式或控制流程語句中使用花括號可以形成程式碼區塊。局部變數從宣告點開始有效，到所在程式碼區塊結束：

```VeroScript
int main()
{
	int outer = 1;

	if (outer > 0)
	{
		int inner = 2;
		print(outer);
		print(inner);
	}

	// inner is out of scope here.
	print(outer);
}
```

內部程式碼區塊可以存取外層程式碼區塊中已經宣告的名稱。外層程式碼區塊不能存取內部程式碼區塊的局部變數。

## 函式作用域

函式參數和函式體中的局部變數屬於函式作用域。不同函式可以使用相同的局部變數名，它們表示不同的宣告：

```VeroScript
void first()
{
	int value = 1;
	print(value);
}

void second()
{
	int value = 2;
	print(value);
}
```

## 模組作用域

在來源檔案頂層宣告的變數、函式和類型屬於目前模組。未匯出的宣告通常只供模組內部使用；使用 `export` 匯出的宣告可以依照模組匯入規則供其他模組使用。

## 命名空間作用域

命名空間可以把一組宣告放入獨立的名稱範圍：

```VeroScript
namespace math
{
	int add(int left, int right)
	{
		return left + right;
	}
}

int main()
{
	print(math::add(1, 2));
}
```

命名空間成員可以使用 `.` 或 `::` 存取。命名空間不能取代模組；模組控製檔案和套件的匯入關係，命名空間控制宣告名稱的組織方式。

### 全域作用域限定

前綴 `::` 會強制從全域作用域查找，不使用目前命名空間、本地別名或 `using namespace` 的回退結果。需要明確引用根層級宣告時使用 `::Name`。

```VeroScript
int GlobalValue = 3;

namespace Feature
{
	int GlobalValue = 9;

	int read_root_value()
	{
		return ::GlobalValue;
	}
}
```

它也可以限定根層級型別或命名空間，`using namespace ::Name` 同樣有效。

## 成員作用域

結構體、類別和介面中的欄位及方法屬於類型的成員作用域。成員存取受到 `public`、`protected` 和 `private` 等存取說明符限制：

```VeroScript
class Counter
{
private:
	int value = 0;

public:
	int get()
	{
		return value;
	}
};
```

成員函式可以透過 `this` 存取目前物件。靜態成員不依賴某個物件實例，存取方式和一般成員不同。

## 名稱隱藏

內層作用域可以宣告與外層相同的名稱。此時內層名稱會隱藏外層名稱；為了保持程式碼清晰，應避免不必要的同名宣告。


## 巢狀型別與區域型別

型別內可宣告巢狀 class、struct、interface、enum 與型別別名。使用 `Outer::Inner`，各泛型區段分別提供自己的引數，例如 `Outer<int>::Inner<string>`。公開子型別可經匯出的外層型別使用，私有／受保護路徑與公開別名仍套用一般存取檢查。

可執行區塊可宣告區域型別，名稱自宣告處至區塊結束有效，內層區塊可以遮蔽。區域型別可使用可見型別、別名、列舉常數及泛型參數，不捕捉外層變數或外層 `this`；存取執行個體成員時須明確傳入物件。靜態成員依具體型別與執行個體儲存，不因重複經過宣告而重新初始化。

```VeroScript
class Outer<T>
{
public:
	struct Inner<U>
	{
		T key;
		U value;
	};
};

void main()
{
	Outer<int>::Inner<string> item = Outer<int>::Inner<string>();
	item.key = 7;
	item.value = "saved";
	struct Result
	{
		int count = 1;
	};
	Result result = Result();
	print("{} {} {}", item.key, item.value, result.count);
}
```

非區域巢狀方法支援 `T Outer<T>::Inner<U>::read() { ... }`，可在匯入宣告的另一來源檔提供對應定義。區域型別方法在型別內或處理器產生的主體中定義。不支援獨立的不完整型別前置宣告、重新開啟型別主體或跨檔定義區域方法。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/scope.md) · [fr-FR](../../fr-FR/language-basics/scope.md) · [ja-JP](../../ja-JP/language-basics/scope.md) · [ko-KR](../../ko-KR/language-basics/scope.md) · [zh-CN](../../zh-CN/language-basics/scope.md) · [zh-TW](scope.md)
