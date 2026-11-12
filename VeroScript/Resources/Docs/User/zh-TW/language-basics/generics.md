# 泛型

泛型允許類型或函式在宣告時使用類型參數，並在使用時替換為具體類型。 VeroScript 支援泛型結構體、類別、介面和函式，也支援 `where` 類型約束。

## 泛型類型

在類型名稱後的尖括號中宣告類型參數：

```VeroScript
class Duo<T, U = int>
{
public:
	T first;
	U second;

	Duo(T first_value, U second_value)
	{
		first = first_value;
		second = second_value;
	}
};

Duo<string> text_pair = Duo<string>("score", 10);
Duo<int, string> number_pair = Duo<int, string>(10, "points");
```

尾部類型參數可以有預設類型，並且預設類型可以引用前面已經宣告的類型參數。顯式類型參數必須是前綴，後續參數可以由上下文或實參推導。

## 泛型函式

泛型函式在函式名稱後宣告類型參數：

```VeroScript
T identity<T>(T value)
{
	return value;
}

string text = identity<string>("VeroScript");
int number = identity(42);
```

如果實參足以確定類型，呼叫時可以省略類型參數。需要明確選擇某種類型時，可以明確提供類型參數。

`template<T>` 是泛型宣告的另一種可讀寫法，與名稱後的 `<T>` 遵循相同的泛型規則：

```VeroScript
template<T>
T identity_again(T value)
{
	return value;
}
```

## 類型約束

使用 `where` 限制類型參數必須滿足的介面契約：

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

T read_once<T>(T value) where T : IReadable
{
	return value;
}
```

一個類型參數可以包含多個介面約束：

```VeroScript
T describe<T>(T value) where T : IReadable, IName
{
	return value;
}
```

約束在編譯階段檢查。當呼叫泛型函式或實例化泛型類型時，實際類型必須滿足宣告中的全部約束。

介面類型別名或別名鏈也可以作為約束：宣告 `using Readable = IReadable;` 後，可寫 `where T : Readable`。指向 `IBox<int>` 等具體泛型介面實例的別名同樣支援。別名在宣告所屬模組中解析，實際類型仍須實作解析後的介面。

## 泛型成員和介面

泛型類型可以繼承介面或泛型基底類，介面本身也可以帶類型參數：

```VeroScript
interface IBox<T>
{
	virtual T get() const = 0;
};

class Box<T> : public IBox<T>
{
public:
	T value;

	T get() const override
	{
		return value;
	}
};
```

泛型宣告中的成員仍遵循普通類型的存取控制、虛擬方法、重載和靜態成員規則。泛型靜態成員在每個具體類型實例之間獨立儲存，詳見[靜態成員](static-members.md)。

## 類型參數的使用邊界

類型參數只能用於編譯器能夠根據其宣告確定的操作。需要呼叫介面方法或存取約束成員時，應宣告對應的 `where` 約束；不要依賴未宣告的隱式成員。

`T::member` 這類限定靜態存取在替換為具體型別後檢查，該型別必須提供相符且可存取的靜態欄位或方法；存取、多載及 const／參照規則仍適用。自身型別、別名與欄位預設值範例見[靜態成員](static-members.md)。

巢狀型別、區域宣告、存取規則與分段泛型實參請參閱[型別範圍](scope.md)。

## 方法樣板與值參數

`T`、`typename T`、`class T` 都宣告型別參數。一般實例方法、靜態方法及有主體的非虛擬介面輔助方法可有自己的樣板參數；內層參數不可遮蔽外層參數。方法樣板不可標記 virtual、override 或 final。

值參數支援 bool、固定寬度整數及腳本／宿主列舉，亦可寫 `template<typename T, T Value>`。例如 `template<typename T, int Count = 4> struct Buffer { T values[Count]; };`。值是不可寫的編譯期常數，只支援純量運算、受檢轉換、列舉成員及可求值的 const 初始式；允許求值合格的 [constexpr 函式](../language-reference/constexpr.md)，不執行其他函式或宿主回呼。溢位、除零、超出範圍及不同列舉身分會報錯；固定陣列長度在具現化後檢查。

有預設項後，其餘自有參數皆須有預設項。預設項在宣告位置繫結，可引用外層及前面的參數。呼叫依序套用明確前綴、從引數型別結構推導、補入預設項，不從執行期數值推導。`box.echo<string>("x")` 明確選擇樣板，`box.echo("x")` 允許推導；`f<>()` 只考慮樣板。先比較轉換品質，同等時一般候選優先，其餘僅比較可證明的結構專用性。型別先按自有參數總數精確匹配，再補預設項；多個可補全宣告會報歧義。

`identity<int>`、`box.echo<string>` 及靜態方法參照可建立 callable；明確的 fn 目標簽章可協助推導。動態字串名稱不會自動具現化開放方法樣板。類別外定義依各層 owner 與方法分組書寫 template 前綴，按位置映射，禁止重複預設項。

參見 `method_templates.vs`、`value_template_parameters.vs`、`template_methods_main.vs`。使用 Core VM／Mixed／JIT 或 Core AOT；獨立原始碼匯出器在輸出前診斷不支援的樣板能力。編譯庫保留開放宣告，消費端可在無原始碼時使用新引數；反射查詢不會自動產生方法實例。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/generics.md) · [fr-FR](../../fr-FR/language-basics/generics.md) · [ja-JP](../../ja-JP/language-basics/generics.md) · [ko-KR](../../ko-KR/language-basics/generics.md) · [zh-CN](../../zh-CN/language-basics/generics.md) · [zh-TW](generics.md)
