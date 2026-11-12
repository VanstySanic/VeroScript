# 介面

`interface` 用於宣告一組可由結構體或類別實作的成員契約。介面不能直接實例化，但可以作為參數、傳回值和變數類型，用於以統一方式存取不同的實作類型。

## 介面宣告

介面方法通常宣告為純虛方法。純虛方法必須使用 `virtual`，並以 `= 0;` 結束：

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};
```

純虛方法必須是公開成員。介面方法的參數、傳回類型和 `const` 修飾共同組成實作契約。

## 實作介面

結構體或類別在繼承清單中寫出介面名稱，並提供與介面簽章相符的實作：

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

class Counter : public IReadable
{
public:
	int value = 0;

	int read() const override
	{
		return value;
	}
};
```

實作方法可以使用 `override` 明確表示其覆寫介面方法。缺少純虛方法實作的類別或結構體不能作為具體物件實例化。

一個類型可以實作多個介面：

```VeroScript
interface IId
{
	virtual int id() const = 0;
};

interface IName
{
	virtual string name() const = 0;
};

class Person : public IId, public IName
{
public:
	int id() const override
	{
		return 42;
	}

	string name() const override
	{
		return "Ada";
	}
};
```

## 預設實作

介面方法也可以提供方法體，形成預設實作。實作類型可以直接繼承該實作，也可以使用 `override` 提供自己的版本：

```VeroScript
interface IDescribable
{
	virtual string name() const = 0;

	string describe() const
	{
		return "name=" + name();
	}
};
```

預設方法仍然屬於介面契約的一部分；只有沒有實作的純虛方法會阻止具體類型實例化。

## 介面繼承

介面可以繼承一個或多個父介面：

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

interface IResource : IReadable
{
	virtual void close() = 0;
};
```

實作 `IResource` 的類型必須滿足它本身以及父介面的全部純虛方法。介面繼承清單中的每個父類型都必須是介面。

## 介面值和轉換

結構體轉換為介面類型時，會形成介面所需的結構體值副本；透過介面修改該副本不會回寫原結構體。類別轉換為介面時保留類別物件的共用身分：

```VeroScript
interface ICounter
{
	virtual void add(int amount) = 0;
};

struct ValueCounter : ICounter
{
	int value = 0;
	void add(int amount)
	{
		value += amount;
	}
};

class SharedCounter : ICounter
{
public:
	int value = 0;
	void add(int amount) override
	{
		value += amount;
	}
};
```

因此，介面變數的複製和呼叫行為取決於其實際物件來源：結構體介面值遵循值語意，類別介面值遵循類別控制代碼語意。介面變數本身不能用來建構一個新的實作物件。

巢狀型別、區域宣告、存取規則與分段泛型實參請參閱[型別範圍](scope.md)。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/interfaces.md) · [fr-FR](../../fr-FR/language-basics/interfaces.md) · [ja-JP](../../ja-JP/language-basics/interfaces.md) · [ko-KR](../../ko-KR/language-basics/interfaces.md) · [zh-CN](../../zh-CN/language-basics/interfaces.md) · [zh-TW](interfaces.md)
