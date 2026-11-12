# Closed polymorphic mapping

Native and script mappings use an explicitly closed table and an outer object `{"kind":"cat","value":{"lives":9}}`. Both key names are configurable. kind is a string, value is the derived converter’s non-null result; the outer root may be null. The envelope avoids member collisions and permits string/array custom payloads. Register 1–256 concrete variants with unique types/tags. Unknown actual types/tags and missing/null payloads fail; no fallback to a registered base.

C++: register each derived converter, then `TPolymorphicMapping<Base>.Add<Derived>(tag).Register`. Base must be polymorphic and may be abstract; derivation must be public/unambiguous, derived types concrete. Decode uses the normal factory and verifies its actual type. The fourth table-constructor argument retains the table code Owner; Add’s second retains the derived Owner, covering RTTI/conversion/destruction. Result shared_ptrs keep these Owners by the ordinary mapping rules. Failed registration preserves the old table; calls use immutable snapshots; dispatch callbacks are destroyed before Owners.

TryDecode<T> returns a shared_ptr<T> owning a newly constructed T. Decoding a non-null Animal directly is TryDecode<Animal>; a nullable shared_ptr<Animal> target produces an outer shared_ptr<shared_ptr<Animal>>. Object fields of shared_ptr<Animal> are directly nullable. Generate Schema for the actual target type.

Script: JsonPolymorphic(base, discriminator, payload, variants) marks a nontemplate table class and generates typed static create/encode/decode/schema. Register variants with `cat="Cat"` or `dog=(type="Dog",tag="good-dog")`; expanded tags allow Unicode. Tables may be module/namespace/local/nested classes without type/value parameters, including captured outer parameters. Bases/variants may be closed generics and follow ordinary access checks. Resolve type names in the table’s scope, aliases in their declaring module, and imported/re-exported visibility. Use custom_converter<Base,Table> if Base cannot be modified; JsonConverter/JsonFactory on Base makes ordinary converter<Base> use the table. Exported tables/types can cross modules and processor-built .vslib libraries; ordinary libraries do not acquire methods automatically.

Input/output Schema uses oneOf and derived Schema, with $defs for recursion. Only input ignore_unknown can allow extra envelope keys; output always rejects them. Errors preserve escaped JSON Pointers for discriminator, payload and derived fields. Failure preserves output; budgets, cancellation and cycle detection share the context. External factory/converter effects do not roll back. Shared graphs serialize by repeated values; cycles report Cycle.

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

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](polymorphic-mapping.md) · [fr-FR](../../fr-FR/Reference/polymorphic-mapping.md) · [ja-JP](../../ja-JP/Reference/polymorphic-mapping.md) · [ko-KR](../../ko-KR/Reference/polymorphic-mapping.md) · [zh-CN](../../zh-CN/Reference/polymorphic-mapping.md) · [zh-TW](../../zh-TW/Reference/polymorphic-mapping.md)
