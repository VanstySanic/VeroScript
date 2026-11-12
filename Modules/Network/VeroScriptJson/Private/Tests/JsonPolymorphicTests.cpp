// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJsonMapping.h"
#include "VeroScriptJsonSchema.h"
#include <iostream>
#include <stdexcept>

namespace J = VeroScript::Json;
#define CHECK(X) \
	do \
	{ \
		if (!(X)) \
		{ \
			throw std::runtime_error(std::string("Line ") + std::to_string(__LINE__) + ": " + #X); \
		} \
	} \
	while (false)
struct FBase
{
	virtual ~FBase() = default;
	virtual int Type() const = 0;
};
struct FCat : FBase
{
	int Type() const override
	{
		return 1;
	}
	int Lives = 9;
	std::shared_ptr<FBase> Next;
};
struct FDog : FBase
{
	int Type() const override
	{
		return 2;
	}
	std::string Name = "dog";
};
struct FUnknown : FCat
{
};
struct FOther
{
	virtual ~FOther() = default;
	int Padding = 17;
};
struct FMultiple : FOther, FBase
{
	int Type() const override
	{
		return 3;
	}
	int Value = 7;
};
static J::FJsonValue Parse(const std::string& Text)
{
	J::FJsonValue V;
	J::FError E;
	CHECK(J::TryParse(Text, V, E));
	return V;
}
static bool Valid(const J::FSchema& S, const J::FJsonValue& V)
{
	bool Result = false;
	J::FError E;
	CHECK(S.TryIsValid(V, Result, E));
	return Result;
}
static bool Equal(const J::FJsonValue& A, const J::FJsonValue& B)
{
	bool Result = false;
	J::FError E;
	CHECK(A.TryEquals(B, Result, E));
	return Result;
}
static void AssignmentOwnerTest(bool Move)
{
	bool SafeOrder = false;
	std::weak_ptr<int> Case;
	auto TableOwner = std::shared_ptr<int>(new int(1),
	    [&](int* Value)
	{
		SafeOrder = Case.expired();
		delete Value;
	});
	auto CaseOwner = std::make_shared<int>(2);
	Case = CaseOwner;
	J::TPolymorphicMapping<FBase> Table("old", "kind", "value", TableOwner);
	Table.Add<FDog>("dog", CaseOwner);
	TableOwner.reset();
	CaseOwner.reset();
	J::TPolymorphicMapping<FBase> Replacement("new");
	Replacement.Add<FCat>("cat");
	if (Move)
	{
		Table = std::move(Replacement);
	}
	else
	{
		Table = Replacement;
	}
	CHECK(SafeOrder && Case.expired());
	J::FMappingRegistry Registry;
	J::FError Error;
	CHECK(Table.Register(Registry, Error));
	Table = Table;
	CHECK(Table.Register(Registry, Error, true));
}

int main()
{
	try
	{
		AssignmentOwnerTest(false);
		AssignmentOwnerTest(true);
		J::FMappingRegistry R;
		J::FError E;
		J::TObjectMapping<FCat> Cat("cat");
		Cat.Field("lives", &FCat::Lives, {true}).Field("next", &FCat::Next);
		J::TObjectMapping<FDog> Dog("dog");
		Dog.Field("name", &FDog::Name, {true});
		J::TObjectMapping<FMultiple> Multiple("multiple");
		Multiple.Field("value", &FMultiple::Value);
		CHECK(Cat.Register(R, E) && Dog.Register(R, E) && Multiple.Register(R, E));
		J::TPolymorphicMapping<FBase> Poly("animal");
		Poly.Add<FCat>("cat").Add<FDog>("dog").Add<FMultiple>("multiple");
		CHECK(Poly.Register(R, E));
		J::FJsonValue Encoded = Parse("17"), Definition;
		std::shared_ptr<FBase> Animal = std::make_shared<FCat>();
		CHECK(J::TryEncode(Animal, Encoded, E, R));
		J::FJsonValue Tag;
		std::string TagText;
		CHECK(Encoded.TryGet("kind", Tag, E) && Tag.TryString(TagText, E) && TagText == "cat");
		std::shared_ptr<FBase> Copy;
		CHECK(J::TryDecode(Encoded, Copy, E, R));
		CHECK(dynamic_cast<FCat*>(Copy.get()) && dynamic_cast<FCat*>(Copy.get())->Lives == 9);
		CHECK(J::TryMappingSchema<std::shared_ptr<FBase>>(J::EMappingDirection::Input, Definition, E, R));
		J::FSchema Input;
		CHECK(J::FSchema::TryCompile(Definition, Input, E));
		CHECK(Valid(Input, Encoded) && Valid(Input, Parse("null")));
		for (const auto& Text : {"{}",
		         "{\"kind\":\"cat\"}",
		         "{\"kind\":7,\"value\":{}}",
		         "{\"kind\":\"other\",\"value\":{}}",
		         "{\"kind\":\"cat\",\"value\":null}",
		         "{\"kind\":\"cat\",\"value\":{}}",
		         "{\"kind\":\"cat\",\"value\":{\"lives\":9},\"extra\":0}"})
		{
			const auto Value = Parse(Text);
			auto Before = Copy;
			CHECK(!J::TryDecode(Value, Copy, E, R) && Copy == Before && !Valid(Input, Value));
		}
		CHECK(!J::TryDecode(Parse("{\"kind\":7,\"value\":{}}"), Copy, E, R) && E.InstancePointer == "/kind");
		CHECK(!J::TryDecode(Parse("{\"kind\":\"cat\",\"value\":{\"lives\":2147483648}}"), Copy, E, R) &&
		    E.InstancePointer == "/value/lives");
		Animal = std::make_shared<FMultiple>();
		CHECK(J::TryEncode(Animal, Encoded, E, R) && J::TryDecode(Encoded, Copy, E, R));
		CHECK(dynamic_cast<FMultiple*>(Copy.get()) && dynamic_cast<FMultiple*>(Copy.get())->Value == 7);
		auto Recursive = std::make_shared<FCat>();
		Recursive->Next = std::make_shared<FDog>();
		Animal = Recursive;
		CHECK(J::TryEncode(Animal, Encoded, E, R) && Valid(Input, Encoded) && J::TryDecode(Encoded, Copy, E, R));
		CHECK(dynamic_cast<FDog*>(dynamic_cast<FCat*>(Copy.get())->Next.get()));
		Recursive->Next = Recursive;
		auto Before = Encoded;
		CHECK(!J::TryEncode(Animal, Encoded, E, R) && E.Code == J::EError::Cycle && Equal(Encoded, Before));
		Recursive->Next.reset();
		Animal = std::make_shared<FUnknown>();
		CHECK(!J::TryEncode(Animal, Encoded, E, R) && E.Code == J::EError::Type && Equal(Encoded, Before));
		J::TPolymorphicMapping<FBase> Duplicate("duplicate");
		Duplicate.Add<FCat>("same").Add<FDog>("same");
		CHECK(!Duplicate.Register(R, E, true) && E.Code == J::EError::DuplicateKey);
		Duplicate = J::TPolymorphicMapping<FBase>("duplicate");
		Duplicate.Add<FCat>("a").Add<FCat>("b");
		CHECK(!Duplicate.Register(R, E, true) && E.Code == J::EError::DuplicateKey);
		J::FMappingOptions Options;
		Options.bIgnoreUnknownFields = true;
		CHECK(J::TryMappingSchema<std::shared_ptr<FBase>>(J::EMappingDirection::Input, Definition, E, R, Options));
		CHECK(J::FSchema::TryCompile(Definition, Input, E));
		const auto UnknownField = Parse("{\"kind\":\"dog\",\"value\":{\"name\":\"d\"},\"extra\":0}");
		CHECK(Valid(Input, UnknownField) && J::TryDecode(UnknownField, Copy, E, R, Options));
		CHECK(J::TryMappingSchema<std::shared_ptr<FBase>>(J::EMappingDirection::Output, Definition, E, R, Options));
		J::FSchema Output;
		CHECK(J::FSchema::TryCompile(Definition, Output, E) && !Valid(Output, UnknownField));
		J::FWork Cancel;
		Cancel.Check = [](void*, std::uint64_t)
		{
			return false;
		};
		CHECK(!J::TryEncode(Animal, Encoded, E, R, {}, Cancel) && E.Code == J::EError::Cancelled);
		Animal.reset();
		CHECK(J::TryEncode(Animal, Encoded, E, R) && Encoded.Kind() == J::EKind::Null);
		std::shared_ptr<std::shared_ptr<FBase>> Nullable;
		CHECK(J::TryDecode<std::shared_ptr<FBase>>(Encoded, Nullable, E, R) && Nullable && !*Nullable);
		auto BeforeNull = Copy;
		CHECK(!J::TryDecode(Encoded, Copy, E, R) && Copy == BeforeNull);
		J::TObjectMapping<FCat> WrongFactory("cat",
		    []
		{
			return std::make_shared<FUnknown>();
		});
		WrongFactory.Field("lives", &FCat::Lives, {true}).Field("next", &FCat::Next);
		CHECK(WrongFactory.Register(R, E, true));
		Copy = std::make_shared<FDog>();
		auto Original = Copy;
		CHECK(!J::TryDecode(Parse("{\"kind\":\"cat\",\"value\":{\"lives\":9}}"), Copy, E, R) && Copy == Original &&
		    E.Code == J::EError::Type && E.InstancePointer == "/value");
		std::weak_ptr<int> TableOwner, CaseOwner;
		std::shared_ptr<FBase> Survivor;
		{
			J::FMappingRegistry Owned;
			auto First = std::make_shared<int>(1), Second = std::make_shared<int>(2);
			TableOwner = First;
			CaseOwner = Second;
			CHECK(Dog.Register(Owned, E));
			J::TPolymorphicMapping<FBase> Table("owned", "kind", "value", First);
			Table.Add<FDog>("dog", Second);
			CHECK(Table.Register(Owned, E));
			CHECK(J::TryDecode(Parse("{\"kind\":\"dog\",\"value\":{\"name\":\"retained\"}}"), Survivor, E, Owned));
		}
		CHECK(!TableOwner.expired() && !CaseOwner.expired() && Survivor->Type() == 2);
		Survivor.reset();
		CHECK(TableOwner.expired() && CaseOwner.expired());
		std::cout << "Native closed polymorphic tables, recursive Schema, exact types and transactions passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
