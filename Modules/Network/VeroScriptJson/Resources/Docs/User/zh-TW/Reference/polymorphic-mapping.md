# 封閉多型對映

使用明確封閉型別表及外層 {"kind":"cat","value":{"lives":9}}。兩鍵名可修改；kind 是字串標籤，value 是衍生轉換器的非 null 結果，外層根可 null。外層避免成員衝突，也接受字串/陣列自訂載荷。表含 1–256 個具體型別，型別/標籤分別唯一；未登記實型/標籤、缺少/null 載荷均失敗，不退回已登記基底。

C++ 先登記各衍生轉換器，再 TPolymorphicMapping<Base>.Add<Derived>(tag).Register。Base 須多型可抽象，衍生須具體、公開、無歧義。解碼呼叫正常工廠並驗證實型。建構第4參數保有表 Owner，Add 第2參數保有衍生 Owner，涵蓋 RTTI/轉換/解構。結果 shared_ptr 依一般規則保有它們；登記失敗不換舊表，呼叫使用不變快照，派發回呼先於 Owner 解構。

TryDecode<T> 輸出 shared_ptr<T> 擁有新建 T。TryDecode<Animal> 建構非 null Animal；以可空 shared_ptr<Animal> 為目標時，外層為 shared_ptr<shared_ptr<Animal>>。物件欄位 shared_ptr<Animal> 直接可空，Schema 應符合實際目標型別。

JsonPolymorphic(base,discriminator,payload,variants) 標記非範本表類別，產生型別化靜態 create/encode/decode/schema。可寫 cat="Cat" 或 dog=(type="Dog",tag="good-dog")，完整寫法容許 Unicode 標籤。表可在模組/命名空間/區域/巢狀宣告，但不可含型別/值參數或捕捉外層參數。基底與變體可為封閉泛型，仍受普通存取檢查。型別名在表範圍、別名在所屬模組及匯入/再匯出可見性下解析。不能修改 Base 時用 custom_converter<Base,Table>；希望 converter<Base> 自動採用時，在 Base 加 JsonConverter/JsonFactory。匯出型別/表可跨模組和處理器建置的 .vslib；普通庫不自動獲得方法。

入出 Schema 以 oneOf、衍生 Schema、遞迴 $defs 表示。僅輸入 ignore_unknown 可放行外層多餘鍵，輸出永遠拒絕。標籤/載荷/欄位保留跳脫 JSON Pointer。失敗保留輸出，共用預算、取消和循環 context；外部副作用不回復。共享圖按值重複，循環回報 Cycle。

```json
{"kind":"cat","value":{"lives":9}}
```

```cpp
TObjectMapping<Cat> cat("cat");
cat.Field("lives", &Cat::Lives);
cat.Register(registry, error);
TObjectMapping<Dog> dog("dog");
dog.Field("name", &Dog::Name);
dog.Register(registry, error);
TPolymorphicMapping<Animal> animals("animal");
animals.Add<Cat>("cat").Add<Dog>("dog");
animals.Register(registry, error);
std::shared_ptr<Animal> source = std::make_shared<Cat>();
TryEncode(source, value, error, registry);
std::shared_ptr<Animal> result;
TryDecode(value, result, error, registry);
```

```VeroScript
@JsonConverter(type="AnimalJson")
@JsonFactory(type="AnimalJson")
class Animal
{
public:
	virtual string voice() = 0;
};
class Cat : public Animal
{
public:
	string voice() override
	{
		return "cat";
	}

	int lives = 9;
};
class Dog : public Animal
{
public:
	string voice() override
	{
		return "dog";
	}
};
@JsonPolymorphic(base="Animal", discriminator="kind", payload="value",
	variants=(cat="Cat", dog=(type="Dog",tag="good-dog")))
class AnimalJson
{
};

Animal source=Cat();
json::value data=json::converter<Animal>::write(ref source);
Animal copy=json::converter<Animal>::read(data);
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/polymorphic-mapping.md) · [fr-FR](../../fr-FR/Reference/polymorphic-mapping.md) · [ja-JP](../../ja-JP/Reference/polymorphic-mapping.md) · [ko-KR](../../ko-KR/Reference/polymorphic-mapping.md) · [zh-CN](../../zh-CN/Reference/polymorphic-mapping.md) · [zh-TW](polymorphic-mapping.md)
