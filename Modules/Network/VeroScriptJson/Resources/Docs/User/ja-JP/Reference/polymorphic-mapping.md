# 閉じた多態マッピング

明示した閉じた型表で {"kind":"cat","value":{"lives":9}} の envelope を使います。二つの key 名は変更でき、kind は string tag、value は derived converter の非 null 結果、root 自体は null 可です。member と衝突せず string／array payload も可能です。具体型 1–256 件、型／tag は一意、未知実型／tag、欠落／null payload は拒否し、登録 base へ縮退しません。

C++ は各 derived converter を登録してから TPolymorphicMapping<Base>.Add<Derived>(tag).Register を呼びます。Base は多態型で abstract 可、derived は concrete かつ public／曖昧でない継承です。通常 factory と実型を検査します。constructor の第4引数は table Owner、Add の第2引数は derived Owner で、RTTI／変換／破棄を保持します。結果 shared_ptr は通常規則で Owner を保持し、登録失敗は旧表を保ち、呼び出しは不変 snapshot、dispatch callback は Owner より先に破棄します。

TryDecode<T> の出力は新しい T を保持する shared_ptr<T> です。TryDecode<Animal> は非 null Animal、nullable shared_ptr<Animal> を対象にする場合は外側が shared_ptr<shared_ptr<Animal>> です。object field の shared_ptr<Animal> は直接 nullable として扱い、Schema は実際の対象型に合わせます。

JsonPolymorphic(base,discriminator,payload,variants) は非 template table class に型付き static create/encode/decode/schema を生成します。cat="Cat" または dog=(type="Dog",tag="good-dog")、Unicode tag も可能です。module／namespace／local／nested で宣言でき、type/value parameter や外側 parameter 捕捉は不可です。base／variant は closed generic 可、アクセス検査を受けます。型名は table scope、alias は宣言 module と import/re-export 可視性で解決します。Base を変更できなければ custom_converter<Base,Table>、通常 converter<Base> へ適用するなら Base に JsonConverter/JsonFactory を指定します。export 型と表は module／processor 付き .vslib を越えて使え、通常 library に method は自動追加されません。

入出力 Schema は oneOf と derived Schema、再帰 $defs を使います。envelope の未知 key を許す ignore_unknown は入力のみ、出力は常に拒否します。tag／payload／field の escape 済み JSON Pointer を保持します。失敗時は出力保持、予算／取消／cycle は共通 context、外部副作用は rollback しません。共有 graph は値の反復、循環は Cycle です。

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

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/polymorphic-mapping.md) · [fr-FR](../../fr-FR/Reference/polymorphic-mapping.md) · [ja-JP](polymorphic-mapping.md) · [ko-KR](../../ko-KR/Reference/polymorphic-mapping.md) · [zh-CN](../../zh-CN/Reference/polymorphic-mapping.md) · [zh-TW](../../zh-TW/Reference/polymorphic-mapping.md)
