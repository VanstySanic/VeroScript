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

enum class ERole : std::uint64_t
{
	User = 1,
	Large = UINT64_MAX
};
struct FRecord
{
	ERole Role = ERole::User;
	std::int64_t Id = 7;
	std::string Name = "default";
	std::vector<std::int64_t> Values;
	std::map<std::string, std::optional<int>> Metadata;
	int OutputOnly = 42, InputOnly = 0, Ignored = 0;
	static unsigned Constructed;
	FRecord()
	{
		++Constructed;
	}
};
unsigned FRecord::Constructed = 0;
struct FNode
{
	int Value = 0;
	std::shared_ptr<FNode> Next;
};
struct FPair
{
	std::shared_ptr<FNode> Left, Right;
};
struct FNoDefault
{
	explicit FNoDefault(int N) : Value(N)
	{
	}
	int Value;
};
struct FReadOnly
{
	const int Value = 3;
};
struct FChange
{
};

struct FToken
{
	std::string Value;
};
struct FProperty
{
	int Value = 0;
};
struct FChanging
{
	FChange First;
	int Second = 2;
};

static J::FJsonValue Parse(const char* Text)
{
	J::FJsonValue V;
	J::FError E;
	CHECK(J::TryParse(Text, V, E));
	return V;
}
static std::string Write(const J::FJsonValue& V)
{
	std::string S;
	J::FError E;
	CHECK(J::TryWrite(V, S, E));
	return S;
}
template <class T>
static void RoundTrip(const T& V, J::FMappingRegistry& Registry, J::FMappingOptions O = {})
{
	J::FError E;
	J::FJsonValue Encoded;
	std::shared_ptr<T> Decoded;
	CHECK(J::TryEncode(V, Encoded, E, Registry, O));
	CHECK(J::TryDecode(Encoded, Decoded, E, Registry, O) && *Decoded == V);
}
static void CheckSchema(const J::FJsonValue& Schema, const J::FJsonValue& Value, bool Expected)
{
	J::FError E;
	J::FSchema Compiled;
	bool Valid;
	if (!J::FSchema::TryCompile(Schema, Compiled, E))
	{
		throw std::runtime_error(E.Message + " " + Write(Schema));
	}
	CHECK(Compiled.TryIsValid(Value, Valid, E) && Valid == Expected);
}
static void NestedDecodedOwnerTests()
{
	J::FMappingRegistry Registry;
	J::FError Error;
	auto Owner = std::make_shared<int>(1);
	std::weak_ptr<int> Weak = Owner;
	bool DestroyedWithOwner = false;
	J::TMappingConverter<FToken> Converter;
	Converter.Name = "nested-token";
	Converter.Owner = Owner;
	Converter.Decode = [Weak, &DestroyedWithOwner](
	                       const J::FJsonValue& V, J::FMappingContext& C, std::shared_ptr<FToken>& Out)
	{
		std::string Text;
		if (!V.TryString(Text, C.Error()))
		{
			return false;
		}
		Out = std::shared_ptr<FToken>(new FToken{std::move(Text)},
		    [Weak, &DestroyedWithOwner](FToken* Value)
		{
			DestroyedWithOwner = !Weak.expired();
			delete Value;
		});
		return true;
	};
	CHECK(J::RegisterConverter(Registry, std::move(Converter), Error));
	std::shared_ptr<std::vector<std::shared_ptr<FToken>>> Aggregate;
	CHECK(J::TryDecode(Parse("[\"nested\"]"), Aggregate, Error, Registry));
	auto Nested = Aggregate->front();
	Aggregate.reset();
	Registry = J::FMappingRegistry{};
	Owner.reset();
	CHECK(!Weak.expired() && Nested->Value == "nested");
	Nested.reset();
	CHECK(DestroyedWithOwner && Weak.expired());
}
static void CallbackOwnerTests()
{
	bool Alive = true;
	for (unsigned Kind = 0; Kind != 3; ++Kind)
	{
		auto Owner = std::make_shared<int>(1);
		std::weak_ptr<int> Weak = Owner;
		auto Probe = std::shared_ptr<int>(new int(0),
		    [Weak, &Alive](int* Value)
		{
			Alive = Alive && !Weak.expired();
			delete Value;
		});
		if (Kind == 0)
		{
			J::TObjectMapping<FProperty> Mapping(
			    "owned",
			    [Probe]
			{
				return std::make_shared<FProperty>();
			},
			    Owner);
			Owner.reset();
			Probe.reset();
			Mapping = J::TObjectMapping<FProperty>("replacement");
		}
		else if (Kind == 1)
		{
			J::FTypeAdapter Adapter;
			Adapter.Name = "owned";
			Adapter.Owner = Owner;
			Adapter.Encode = [Probe](const void*, auto&, auto&)
			{
				return true;
			};
			Owner.reset();
			Probe.reset();
			Adapter = J::FTypeAdapter{};
		}
		else
		{
			J::TMappingConverter<FProperty> Converter;
			Converter.Name = "owned";
			Converter.Owner = Owner;
			Converter.Encode = [Probe](const auto&, auto&, auto&)
			{
				return true;
			};
			Owner.reset();
			Probe.reset();
			Converter = J::TMappingConverter<FProperty>{};
		}
		CHECK(Alive && Weak.expired());
	}
	J::FMappingRegistry Registry;
	J::FError Error;
	bool Armed = false, Reentered = false;
	auto Probe = std::shared_ptr<int>(new int(0),
	    [&](int* P)
	{
		delete P;
		if (Armed)
		{
			J::FTypeAdapter Added;
			Added.Name = "from-deleter";
			Added.Encode = [](const void*, auto&, auto&)
			{
				return true;
			};
			J::FError Failure;
			Reentered = Registry.Register(typeid(FToken), std::move(Added), Failure);
		}
	});
	J::FTypeAdapter First;
	First.Name = "first";
	First.Encode = [Probe](const void*, auto&, auto&)
	{
		return true;
	};
	CHECK(Registry.Register(typeid(FProperty), std::move(First), Error));
	// A moved-from std::function may retain its callable (notably in libc++).
	First = {};
	Probe.reset();
	Armed = true;
	J::FTypeAdapter Second;
	Second.Name = "second";
	Second.Encode = [](const void*, auto&, auto&)
	{
		return true;
	};
	CHECK(Registry.Register(typeid(FProperty), std::move(Second), Error, true) && Reentered);
	auto KeyOwner = std::make_shared<int>(1);
	std::weak_ptr<int> KeyOwnerProbe = KeyOwner;
	J::FMappingRegistry Keys;
	J::FTypeAdapter Key;
	Key.Name = "type-info-owner";
	Key.Owner = KeyOwner;
	Key.Encode = [](const void*, auto&, auto&)
	{
		return true;
	};
	CHECK(Keys.Register(typeid(FProperty), std::move(Key), Error));
	KeyOwner.reset();
	J::FTypeAdapter Replacement;
	Replacement.Name = "replacement";
	Replacement.Encode = [](const void*, auto&, auto&)
	{
		return true;
	};
	CHECK(Keys.Register(typeid(FProperty), std::move(Replacement), Error, true));
	CHECK(!KeyOwnerProbe.expired());
	Keys = J::FMappingRegistry{};
	CHECK(KeyOwnerProbe.expired());
}

int main()
{
	try
	{
		CallbackOwnerTests();
		NestedDecodedOwnerTests();
		J::FMappingRegistry R;
		J::FError E;
		CHECK(J::RegisterEnum(R,
		    "role",
		    std::vector<std::pair<std::string, ERole>>{
		        {"user", ERole::User}, {"large", ERole::Large}, {"alias", ERole::User}},
		    E));
		J::FFieldOptions Required;
		Required.bRequired = true;
		J::FFieldOptions Output;
		Output.bDeserialize = false;
		J::FFieldOptions Input;
		Input.bSerialize = false;
		J::FFieldOptions Ignore;
		Ignore.bIgnore = true;
		CHECK(J::TObjectMapping<FRecord>("record")
		        .Field("role", &FRecord::Role)
		        .Field("id", &FRecord::Id)
		        .Field("display/name~", &FRecord::Name, Required)
		        .Field("values", &FRecord::Values)
		        .Field("metadata", &FRecord::Metadata)
		        .Field("output", &FRecord::OutputOnly, Output)
		        .Field("input", &FRecord::InputOnly, Input)
		        .Field("legacy", &FRecord::Ignored, Ignore)
		        .Register(R, E));
		FRecord Record;
		Record.Role = ERole::Large;
		Record.Id = INT64_MIN;
		Record.Name = std::string("A\0B", 3);
		Record.Values = {INT64_MIN, INT64_MAX};
		Record.Metadata = {{"empty", {}}, {"value", 17}};
		J::FJsonValue Encoded;
		CHECK(J::TryEncode(Record, Encoded, E, R));
		CHECK(Write(Encoded).find("\"role\":\"large\"") != std::string::npos);
		J::FJsonValue Value;
		CHECK(!Encoded.TryGet("input", Value, E) && !Encoded.TryGet("legacy", Value, E));
		J::FJsonValue InputJson = Encoded;
		CHECK(InputJson.TryErase("output", E));
		CHECK(InputJson.TrySet("legacy", Parse("[1,2,3]"), E));
		std::shared_ptr<FRecord> Decoded;
		CHECK(J::TryDecode(InputJson, Decoded, E, R));
		CHECK(Decoded->Role == Record.Role && Decoded->Id == INT64_MIN && Decoded->Name == Record.Name &&
		    Decoded->Values == Record.Values && Decoded->Metadata == Record.Metadata);
		auto Original = Decoded;
		const auto Count = FRecord::Constructed;
		CHECK(!J::TryDecode(Parse("{}"), Decoded, E, R) && E.InstancePointer == "/display~1name~0" &&
		    Decoded == Original);
		CHECK(!J::TryDecode(Parse("{\"display/name~\":17}"), Decoded, E, R) &&
		    E.InstancePointer == "/display~1name~0" && Decoded == Original);
		CHECK(FRecord::Constructed == Count);
		CHECK(!J::TryDecode(Parse("{\"display/name~\":\"x\",\"unknown\":1}"), Decoded, E, R) &&
		    E.InstancePointer == "/unknown");
		J::FMappingOptions Lenient;
		Lenient.bIgnoreUnknownFields = true;
		CHECK(J::TryDecode(Parse("{\"display/name~\":\"x\",\"unknown\":1}"), Decoded, E, R, Lenient));
		CHECK(Decoded->Id == 7);
		CHECK(!J::TryDecode(Parse("{\"display/name~\":\"x\",\"output\":1}"), Decoded, E, R, Lenient) &&
		    E.InstancePointer == "/output");
		J::FJsonValue InputSchema, OutputSchema;
		CHECK(J::TryMappingSchema<FRecord>(J::EMappingDirection::Input, InputSchema, E, R));
		CHECK(J::TryMappingSchema<FRecord>(J::EMappingDirection::Output, OutputSchema, E, R));
		CheckSchema(InputSchema, InputJson, true);
		CheckSchema(InputSchema, Encoded, false);
		CheckSchema(OutputSchema, Encoded, true);
		CheckSchema(InputSchema, Parse("{}"), false);
		CheckSchema(InputSchema, Parse("{\"display/name~\":\"x\",\"other\":1}"), false);
		CHECK(J::TryMappingSchema<FRecord>(J::EMappingDirection::Input, InputSchema, E, R, Lenient));
		CheckSchema(InputSchema, Parse("{\"display/name~\":\"x\",\"other\":1}"), true);
		CheckSchema(InputSchema, Parse("{\"display/name~\":\"x\",\"output\":1}"), false);

		CHECK(
		    J::TObjectMapping<FNode>("node").Field("value", &FNode::Value).Field("next", &FNode::Next).Register(R, E));
		CHECK(
		    J::TObjectMapping<FPair>("pair").Field("left", &FPair::Left).Field("right", &FPair::Right).Register(R, E));
		auto Node = std::make_shared<FNode>();
		Node->Value = 9;
		Node->Next = Node;
		J::FJsonValue Before = Parse("123");
		CHECK(!J::TryEncode(Node, Before, E, R) && E.Code == J::EError::Cycle && E.InstancePointer == "/next" &&
		    Write(Before) == "123");
		Node->Next.reset();
		CHECK(J::TryEncode(FPair{Node, Node}, Encoded, E, R));
		CHECK(J::TryMappingSchema<FNode>(J::EMappingDirection::Input, InputSchema, E, R));
		CheckSchema(InputSchema, Parse("{\"value\":3,\"next\":{\"value\":4,\"next\":null}}"), true);
		std::shared_ptr<FNode> Tree;
		CHECK(J::TryDecode(Parse("{\"value\":3,\"next\":{\"value\":4}}"), Tree, E, R) && Tree->Next->Value == 4);
		CHECK(!J::TObjectMapping<FReadOnly>("readonly").Field("value", &FReadOnly::Value).Register(R, E));
		CHECK(J::TObjectMapping<FReadOnly>("readonly").Field("value", &FReadOnly::Value, Output).Register(R, E));
		CHECK(J::TObjectMapping<FNoDefault>("factory",
		    []
		{
			return std::make_shared<FNoDefault>(5);
		})
		        .Field("value", &FNoDefault::Value)
		        .Register(R, E));
		std::shared_ptr<FNoDefault> Created;
		CHECK(J::TryDecode(Parse("{\"value\":10}"), Created, E, R) && Created->Value == 10);
		CHECK(!J::TObjectMapping<FRecord>("bad").Field("x", &FRecord::Id).Field("x", &FRecord::Name).Register(R, E) &&
		    E.Code == J::EError::DuplicateKey);

		RoundTrip(std::vector<bool>{true, false, true}, R);
		RoundTrip(std::list<int>{1, 2, 3}, R);
		RoundTrip(std::deque<std::string>{"", std::string("a\0", 2)}, R);
		RoundTrip(std::array<int, 3>{1, 2, 3}, R);
		RoundTrip(std::set<int>{1, 2, 3}, R);
		RoundTrip(std::multiset<int>{1, 1, 2}, R);
		RoundTrip(std::optional<int>{}, R);
		RoundTrip(std::optional<int>{9}, R);
		RoundTrip(std::map<std::string, int>{{"x", 1}}, R);
		std::shared_ptr<std::set<int>> Set;
		CHECK(!J::TryDecode(Parse("[1,1]"), Set, E, R) && E.Code == J::EError::DuplicateKey);
		std::shared_ptr<std::array<int, 2>> Array;
		CHECK(!J::TryDecode(Parse("[1]"), Array, E, R));
		std::map<int, std::string> IntMap{{1, "one"}, {2, "two"}};
		CHECK(!J::TryEncode(IntMap, Encoded, E, R) && E.Code == J::EError::Unsupported);
		J::FMappingOptions Pairs;
		Pairs.bMapAsPairs = true;
		RoundTrip(IntMap, R, Pairs);
		RoundTrip(std::multimap<int, int>{{1, 2}, {1, 3}}, R, Pairs);
		std::shared_ptr<std::map<int, int>> Map;
		CHECK(!J::TryDecode(Parse("[{\"key\":1,\"value\":2},{\"key\":1,\"value\":3}]"), Map, E, R, Pairs) &&
		    E.Code == J::EError::DuplicateKey);
		std::shared_ptr<std::uint32_t> Small;
		CHECK(!J::TryDecode(Parse("4294967296"), Small, E, R));
		CHECK(!J::TryDecode(Parse("-1"), Small, E, R));
		RoundTrip(std::uint64_t{UINT64_MAX}, R);
		J::FMappingOptions Numeric;
		Numeric.bEnumsAsNumbers = true;
		RoundTrip(ERole::Large, R, Numeric);
		CHECK(J::TryMappingSchema<ERole>(J::EMappingDirection::Input, InputSchema, E, R, Numeric));
		CheckSchema(InputSchema, Parse("18446744073709551615"), true);
		J::FMappingOptions Exact;
		Exact.bExactFloatingPoint = true;
		std::shared_ptr<float> Float;
		CHECK(!J::TryDecode(Parse("0.1"), Float, E, R, Exact));
		CHECK(J::TryDecode(Parse("0.1"), Float, E, R));
		for (const auto* Text : {"1e-100", "-1e-100"})
		{
			auto OriginalFloat = std::make_shared<float>(7.0f);
			Float = OriginalFloat;
			CHECK(!J::TryDecode(Parse(Text), Float, E, R) && E.Code == J::EError::Range && Float == OriginalFloat &&
			    *Float == 7.0f);
		}
		RoundTrip(std::numeric_limits<float>::denorm_min(), R);
		RoundTrip(-std::numeric_limits<float>::denorm_min(), R);
		CHECK(!J::TryEncode(std::numeric_limits<double>::infinity(), Encoded, E, R));
		J::FMappingOptions Limited;
		Limited.Limits.Depth = 1;
		CHECK(!J::TryDecode(Parse("{\"value\":3,\"next\":{\"value\":4}}"), Tree, E, R, Limited) &&
		    E.Code == J::EError::Limit);
		J::FWork Cancelled;
		Cancelled.Check = [](void*, std::uint64_t)
		{
			return false;
		};
		CHECK(!J::TryEncode(Record, Encoded, E, R, {}, Cancelled) && E.Code == J::EError::Cancelled);

		J::FMappingRegistry Changing;
		J::FTypeAdapter Change;
		Change.Name = "change";
		Change.Encode = [&](const void*, J::FMappingContext&, J::FJsonValue& Out)
		{
			J::FTypeAdapter Int;
			Int.Name = "custom-int";
			Int.Encode = [](const void*, J::FMappingContext&, J::FJsonValue& J)
			{
				J = J::FJsonValue(std::int64_t{999});
				return true;
			};
			J::FError Error;
			CHECK(Changing.Register(typeid(int), std::move(Int), Error, true));
			Out = {};
			return true;
		};
		CHECK(Changing.Register(typeid(FChange), std::move(Change), E));
		CHECK(J::TObjectMapping<FChanging>("changing")
		        .Field("first", &FChanging::First)
		        .Field("second", &FChanging::Second)
		        .Register(Changing, E));
		CHECK(J::TryEncode(FChanging{}, Encoded, E, Changing));
		CHECK(Encoded.TryGet("second", Value, E) && Write(Value) == "2");
		CHECK(J::TryEncode(FChanging{}, Encoded, E, Changing));
		CHECK(Encoded.TryGet("second", Value, E) && Write(Value) == "999");
		CHECK(!J::TryMappingSchema<int>(J::EMappingDirection::Output, InputSchema, E, Changing) &&
		    E.Code == J::EError::Unsupported);
		J::FMappingRegistry Owned;
		auto Owner = std::make_shared<int>(1);
		std::weak_ptr<int> WeakOwner = Owner;
		J::TMappingConverter<FToken> Converter;
		Converter.Name = "token";
		Converter.Owner = Owner;
		Converter.Encode = [](const FToken& T, J::FMappingContext& C, J::FJsonValue& O)
		{
			return C.Encode(T.Value, O);
		};
		Converter.Decode = [](const J::FJsonValue& V, J::FMappingContext& C, std::shared_ptr<FToken>& O)
		{
			std::shared_ptr<std::string> S;
			if (!C.Decode(V, S))
			{
				return false;
			}
			O = std::make_shared<FToken>(FToken{*S});
			return true;
		};
		Converter.Schema = [](J::FMappingContext& C, J::EMappingDirection D, J::FJsonValue& O)
		{
			return C.Schema<std::string>(D, O);
		};
		CHECK(J::RegisterConverter(Owned, std::move(Converter), E));
		std::shared_ptr<FToken> Token;
		CHECK(J::TryDecode(Parse("\"owned\""), Token, E, Owned) && Token->Value == "owned");
		Owned = J::FMappingRegistry{};
		Owner.reset();
		CHECK(!WeakOwner.expired());
		Token.reset();
		CHECK(WeakOwner.expired());
		J::TObjectMapping<FProperty> Property("property");
		Property.Property<int>(
		    "value",
		    [](const auto& V)
		{
			if (V.Value < 0)
			{
				throw std::runtime_error("getter");
			}
			return V.Value;
		},
		    [](auto& V, int X)
		{
			if (X == 13)
			{
				throw std::runtime_error("setter");
			}
			V.Value = X;
		});
		CHECK(Property.Register(R, E));
		auto PropertyValue = std::make_shared<FProperty>();
		auto OldProperty = PropertyValue;
		CHECK(!J::TryDecode(Parse("{\"value\":13}"), PropertyValue, E, R) && E.InstancePointer == "/value" &&
		    E.Code == J::EError::Callback && PropertyValue == OldProperty);
		CHECK(!J::TryEncode(FProperty{-1}, Encoded, E, R) && E.InstancePointer == "/value");
		Limited = {};
		Limited.Limits.OutputBytes = 3;
		Before = Parse("123");
		CHECK(!J::TryEncode(std::string("ab"), Before, E, R, Limited) && E.Code == J::EError::Limit &&
		    Write(Before) == "123");
		std::cout << "JSON mapping, schema, exact numbers, cycles, transactional construction and registry snapshots "
		             "passed\n";
		return 0;
	}
	catch (const std::exception& E)
	{
		std::cerr << E.what() << '\n';
		return 1;
	}
}
