// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// The host loads VeroScriptJson. A closed table selects concrete classes; it never loads a type by input name.
@ JsonConverter(type = "AnimalJson") @ JsonFactory(type = "AnimalJson") class Animal
{
public:
	int id = 1;
	virtual string voice() = 0;
};
class Cat : public Animal
{
public:
	int lives = 9;
	Animal next = nullptr;
	string voice() override
	{
		return "cat";
	}
};
class Dog : public Animal
{
public:
	string name = "dog";
	string voice() override
	{
		return "dog";
	}
};
class UnknownCat : public Cat
{
};
@ JsonPolymorphic(base = "Animal", discriminator = "kind", payload = "value", variants = (cat = "Cat", dog = (type = "Dog", tag = "good-dog"))) class AnimalJson
{
};
int main()
{
	Animal original = Cat();
	json::value wire = json::converter<Animal>::write(ref original);
	if (wire.get("kind").as_string() != "cat" || wire.get("value").get("lives").as_int64() != 9) return 1;
	Animal restored = json::converter<Animal>::read(wire);
	if (restored.voice() != "cat" || cast<Cat>(restored).lives != 9) return 2;
	json::schema schema = json::compile_schema(json::converter<Animal>::schema());
	if (!schema.is_valid(wire) || !schema.is_valid(json::from_null())) return 3;
	json::error error = json::error();
	Animal before = restored;
	if (json::converter<Animal>::try_read(json::parse("{\"kind\":\"missing\",\"value\":{}}"), ref restored, ref error) || restored != before || error.instance_pointer() != "/kind") return 4;
	if (json::converter<Animal>::try_read(json::parse("{\"kind\":\"cat\",\"value\":null}"), ref restored, ref error) || restored != before || error.instance_pointer() != "/value") return 5;
	Cat recursive = Cat();
	recursive.next = Dog();
	original = recursive;
	wire = json::converter<Animal>::write(ref original);
	if (!schema.is_valid(wire) || wire.get("value").get("next").get("kind").as_string() != "good-dog") return 6;
	recursive.next = recursive;
	json::value saved = wire;
	if (json::converter<Animal>::try_write(ref original, ref wire, ref error) || error.code() == 0 || !wire.equals(saved)) return 7;
	recursive.next = nullptr;
	original = UnknownCat();
	if (json::converter<Animal>::try_write(ref original, ref wire, ref error) || !wire.equals(saved)) return 8;
	original = nullptr;
	if (json::converter<Animal>::write(ref original).kind() != 0 || json::converter<Animal>::read(json::from_null()) != nullptr) return 9;
	return 0;
}
