# Conversion polymorphe fermée

Une table explicitement fermée utilise l’enveloppe {"kind":"cat","value":{"lives":9}}. Les deux noms sont configurables ; kind est un tag string, value le résultat non null du convertisseur dérivé. La racine peut être null. L’enveloppe évite les collisions et permet des charges string/tableau. Déclarez 1–256 variantes concrètes, types/tags uniques. Type réel/tag inconnu, charge absente/null échouent ; aucun repli vers une base enregistrée.

C++ : enregistrez chaque convertisseur dérivé, puis TObjectMapping polymorphe via TPolymorphicMapping<Base>.Add<Derived>(tag).Register. Base doit être polymorphe, éventuellement abstraite ; héritage public non ambigu, dérivées concrètes. La fabrique normale est appelée et son type réel vérifié. Le quatrième argument du constructeur garde l’Owner de la table ; le deuxième de Add garde l’Owner dérivé, couvrant RTTI/conversion/destruction. Les shared_ptr résultants prolongent ces durées de vie. Échec d’enregistrement : ancienne table conservée ; appel sur instantané immuable ; callbacks détruits avant Owners.

TryDecode<T> retourne un shared_ptr<T> possédant le nouveau T. TryDecode<Animal> construit un Animal non null ; la cible nullable shared_ptr<Animal> donne un shared_ptr<shared_ptr<Animal>> extérieur. Un champ shared_ptr<Animal> est directement nullable. Générez la Schema du type cible réel.

JsonPolymorphic(base,discriminator,payload,variants) marque une classe table non générique et produit create/encode/decode/schema statiques typés. Formes : cat="Cat" ou dog=(type="Dog",tag="good-dog"), tags Unicode possibles. Table de module/namespace/locale/imbriquée, sans paramètres type/valeur ni capture de paramètres extérieurs. Bases/variantes peuvent être génériques fermées et suivent l’accès normal. Noms dans la portée de table, alias dans leur module, imports/réexports respectés. custom_converter<Base,Table> sert une base non modifiable ; JsonConverter/JsonFactory sur Base configure converter<Base>. Types/tables exportés traversent modules et .vslib avec processeur ; une bibliothèque ordinaire n’acquiert pas automatiquement ces méthodes.

Schema entrée/sortie utilise oneOf, la Schema dérivée et $defs récursifs. Seule l’entrée ignore_unknown autorise les clés supplémentaires d’enveloppe ; sortie les refuse toujours. Les erreurs conservent les JSON Pointers échappés de tag/charge/champ. Échec : sortie inchangée ; budgets, annulation et cycles partagés. Effets externes non annulés ; graphes partagés répétés par valeur, cycles signalés Cycle.

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

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/polymorphic-mapping.md) · [fr-FR](polymorphic-mapping.md) · [ja-JP](../../ja-JP/Reference/polymorphic-mapping.md) · [ko-KR](../../ko-KR/Reference/polymorphic-mapping.md) · [zh-CN](../../zh-CN/Reference/polymorphic-mapping.md) · [zh-TW](../../zh-TW/Reference/polymorphic-mapping.md)
