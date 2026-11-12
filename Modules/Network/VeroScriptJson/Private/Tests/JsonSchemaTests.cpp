// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJsonSchema.h"
#include <iostream>
#include <thread>
#include <atomic>

using namespace VeroScript::Json;

static void Check(bool Value, int Line)
{
	if (!Value)
	{
		throw std::runtime_error("Schema regression at line " + std::to_string(Line));
	}
}
#define CHECK(X) Check((X), __LINE__)

static FJsonValue Parse(const char* Text)
{
	FJsonValue Value;
	FError Error;
	CHECK(TryParse(Text, Value, Error));
	return Value;
}

static FSchema Compile(const char* Text)
{
	FSchema Value;
	FError Error;
	if (!FSchema::TryCompile(Parse(Text), Value, Error))
	{
		throw std::runtime_error(Error.Message);
	}
	return Value;
}

static bool Valid(const FSchema& Schema, const char* Text)
{
	bool Value = false;
	FError Error;
	if (!Schema.TryIsValid(Parse(Text), Value, Error))
	{
		throw std::runtime_error(Error.Message);
	}
	return Value;
}

int main()
{
	try
	{
		FError Error;
		bool ScopeIsolated = true;
		for (const auto& Case :
			std::vector<std::pair<const char*, const char*>>{
				{R"({"contains":{"type":"array","prefixItems":[true]},"unevaluatedItems":false})", R"([7,[1]])"},
				{R"({"unevaluatedProperties":{"type":"object","properties":{"x":true},"unevaluatedProperties":false}})",
					R"({"a":{"x":1},"x":7})"},
				{R"({"unevaluatedItems":{"type":"array","prefixItems":[true,true],"unevaluatedItems":false}})",
					R"([[1,2],7])"}})
		{
			if (Valid(Compile(Case.first), Case.second))
			{
				std::cerr << "Nested evaluation escaped its instance scope: " << Case.first << '\n';
				ScopeIsolated = false;
			}
		}
		FSchemaReport UnevaluatedReport;
		CHECK(Compile(R"({"unevaluatedProperties":{"type":"integer"}})")
				.TryValidate(Parse(R"({"bad":"x"})"), UnevaluatedReport, Error));
		if (UnevaluatedReport.Issues.empty() || UnevaluatedReport.Issues[0].InstancePointer != "/bad")
		{
			std::cerr << "Unevaluated property diagnostic lost its instance location\n";
			ScopeIsolated = false;
		}
		CHECK(ScopeIsolated);
		auto Schema = Compile(R"({"type":"number","maximum":9007199254740992,"multipleOf":0.01})");
		CHECK(Valid(Schema, "9007199254740992.0"));
		CHECK(!Valid(Schema, "9007199254740993.0"));
		CHECK(!Valid(Schema, "0.001"));
		CHECK(Valid(Compile(R"({"const":{"x":1,"y":2}})"), R"({"y":2.0,"x":1.00})"));
		CHECK(Valid(Compile(R"({"type":"integer"})"), "1e100000"));
		CHECK(!Valid(Compile(R"({"uniqueItems":true})"), R"([{"a":1,"b":2},{"b":2,"a":1.0}])"));
		auto Pattern = Compile(R"({"type":"string","pattern":"(?<=a+)b"})");
		CHECK(Valid(Pattern, R"("aaab")"));
		CHECK(!Valid(Pattern, R"("b")"));
		CHECK(Valid(Compile(R"({"pattern":"^\\p{Letter}+$"})"), R"("中文")"));
		CHECK(Valid(Compile(R"({"pattern":"^a\\u0000b$"})"), R"("a\u0000b")"));
		// Annotation-only format does not apply a partial format assertion vocabulary.
		CHECK(Valid(Compile(R"({"format":"email"})"), R"("not an email")"));
		FSchema Previous = Pattern;
		for (const char* Bad : {R"({"type":12})", R"({"multipleOf":0})", R"({"minItems":-1})", R"({"pattern":"["})"})
		{
			CHECK(!FSchema::TryCompile(Parse(Bad), Pattern, Error));
			CHECK(Error.Code == EError::Schema && Pattern.IsCompiled() && Valid(Pattern, R"("ab")"));
		}
		CHECK(!FSchema::TryCompile(
				  Parse(R"({"$schema":"https://json-schema.org/draft/2019-09/schema"})"), Pattern, Error) &&
			  Error.Code == EError::Unsupported);
		FSchema OrdinaryVocabulary;
		// $vocabulary describes a meta-schema only when that resource is used as one.
		CHECK(FSchema::TryCompile(Parse(R"({"$vocabulary":{"https://example.test/unknown":true},"format":"email"})"),
			OrdinaryVocabulary,
			Error));
		CHECK(Valid(OrdinaryVocabulary, R"("not an email")"));

		CHECK(
			!FSchema::TryCompile(
				Parse(R"({"$defs":{"a":{"$id":"https://example.test/same"},"b":{"$id":"https://example.test/same"}}})"),
				Pattern,
				Error) &&
			Error.Code == EError::Schema);
		FSchemaOptions Options;
		Options.RetrievalUri = "https://example.test/root.json";
		unsigned Calls = 0;
		{
			FSchemaResolver Resolver = [&](std::string_view Uri, FJsonValue& Out, FError&)
			{
				++Calls;
				CHECK(Uri == "https://example.test/nested.json");
				Out = Parse(R"({"$defs":{"item":{"type":"integer"}}})");
				return true;
			};
			CHECK(FSchema::TryCompile(
				Parse(R"({"$ref":"nested.json#/$defs/item"})"), Schema, Error, Options, {}, Resolver));
		}
		CHECK(Calls == 1 && Valid(Schema, "3") && !Valid(Schema, "3.1") && Calls == 1);
		CHECK(FSchema::TryCompile(
			Parse(
				R"({"allOf":[{"$ref":"item.json?version=1#/$defs/value"},{"$ref":"item.json?version=2#/$defs/value"}]})"),
			Schema,
			Error,
			Options,
			{{"https://example.test/item.json?version=1", Parse(R"({"$defs":{"value":{"minimum":1}}})")},
				{"https://example.test/item.json?version=2", Parse(R"({"$defs":{"value":{"maximum":2}}})")}}));
		CHECK(Valid(Schema, "1.5") && !Valid(Schema, "0") && !Valid(Schema, "3"));
		CHECK(!FSchema::TryCompile(Parse(R"({"$ref":"missing.json"})"), Pattern, Error, Options) &&
			  Error.Code == EError::Resource);
		CHECK(Error.Message.find("https://example.test/missing.json") != std::string::npos);
		FSchemaResolver Failing = [](std::string_view, FJsonValue&, FError& Failure)
		{
			Failure.Code = EError::Resource;
			Failure.Message = "Provider detail retained";
			Failure.bHasSource = true;
			Failure.ByteOffset = 7;
			return false;
		};
		CHECK(!FSchema::TryCompile(Parse(R"({"$ref":"missing.json"})"), Pattern, Error, Options, {}, Failing) &&
			  Error.Message == "Provider detail retained" && Error.ByteOffset == 7 && Error.bHasSource);
		FSchemaOptions Self;
		Self.RetrievalUri = "https://example.test/self";
		FSchema SelfSchema;
		CHECK(FSchema::TryCompile(Parse(R"({"$schema":"https://example.test/self","$id":"https://example.test/self"})"),
			SelfSchema,
			Error,
			Self));
		CHECK(Valid(SelfSchema, "null"));
		CHECK(FSchema::TryCompile(Parse(R"({"$schema":"https://example.test/core-only","type":12})"),
			Pattern,
			Error,
			{},
			{{"https://example.test/core-only",
				Parse(
					R"({"$schema":"https://json-schema.org/draft/2020-12/schema","$id":"https://example.test/core-only","$vocabulary":{"https://json-schema.org/draft/2020-12/vocab/core":true}})")}}));
		CHECK(Valid(Pattern, "null"));
		CHECK(!FSchema::TryCompile(
				  Parse(R"({"properties":{"x":{"$schema":"https://json-schema.org/draft/2019-09/schema"}}})"),
				  Pattern,
				  Error) &&
			  Error.Code == EError::Unsupported);
		CHECK(!FSchema::TryCompile(Parse("true"),
				  Pattern,
				  Error,
				  Options,
				  {{"https://example.test/duplicate", Parse("true")},
					  {"https://example.test/duplicate", Parse("false")}}) &&
			  Error.Code == EError::Resource);
		Schema = Compile(
			R"({"type":"object","required":["a","b"],"properties":{"a":{"type":"integer"},"b":{"minimum":2}}})");
		FSchemaReport Report;
		CHECK(Schema.TryValidate(Parse(R"({"a":"bad","b":1})"), Report, Error));
		CHECK(!Report.bValid && !Report.bTruncated && Report.Issues.size() == 2);
		CHECK(Report.Issues[0].InstancePointer == "/a" && Report.Issues[0].Keyword == "type");
		CHECK(Schema.TryValidate(Parse(R"({"a":"bad","b":1})"), Report, Error, 1));
		CHECK(!Report.bValid && Report.bTruncated && Report.Issues.size() == 1);
		FWork Cancel;
		Cancel.Check = [](void*, std::uint64_t)
		{
			return false;
		};
		CHECK(!Schema.TryValidate(Parse("{}"), Report, Error, 256, {}, Cancel) && Error.Code == EError::Cancelled);
		CHECK(!Report.bValid && Report.Issues.size() == 1);
		FLimits Limits;
		Limits.Nodes = 1;
		bool Result = true;
		CHECK(!Schema.TryIsValid(Parse(R"({"a":1})"), Result, Error, Limits) && Error.Code == EError::Limit && Result);
		CHECK(
			!FSchema::TryCompile(Parse("true"), Pattern, Error, {}, {}, {}, Cancel) && Error.Code == EError::Cancelled);
		const auto Expensive = Compile(R"({"pattern":"^(a+)+$"})");
		FJsonValue Input;
		CHECK(FJsonValue::TryString(std::string(40, 'a') + "!", Input, Error));
		bool ReachedInterpreter = false;
		FWork StopInsideRegex;
		StopInsideRegex.UserData = &ReachedInterpreter;
		StopInsideRegex.Check = [](void* Data, std::uint64_t Units)
		{
			if (Units >= 10000)
			{
				*static_cast<bool*>(Data) = true;
				return false;
			}
			return true;
		};
		CHECK(!Expensive.TryIsValid(Input, Result, Error, {}, StopInsideRegex) && Error.Code == EError::Cancelled &&
			  ReachedInterpreter);
		const auto PropertyRef = Compile(
			R"({"$ref":"#/patternProperties/a~1b~0","patternProperties":{"a/b~":{"type":"integer"},"other":{"type":"boolean"}}})");
		CHECK(Valid(PropertyRef, "1") && !Valid(PropertyRef, "false"));
		const auto DependentRef = Compile(
			R"({"$ref":"#/dependentSchemas/a~1b~0","dependentSchemas":{"a/b~":{"type":"integer"},"other":{"type":"boolean"}}})");
		CHECK(Valid(DependentRef, "1") && !Valid(DependentRef, "false"));
		auto Loop = Compile(R"({"$ref":"#"})");
		CHECK(!Loop.TryIsValid(Parse("null"), Result, Error) && Error.Code == EError::Limit);
		const auto Shared = Compile(R"({"type":"string","pattern":"^\\p{Letter}+$"})");
		std::atomic<bool> Passed{true};
		std::vector<std::thread> Workers;
		for (unsigned I = 0; I < 4; ++I)
		{
			Workers.emplace_back(
				[&]
				{
					try
					{
						for (unsigned J = 0; J < 30; ++J)
						{
							if (!Valid(Shared, R"("中文abc")") || Valid(Shared, R"("123")"))
							{
								Passed = false;
							}
						}
					}
					catch (...)
					{
						Passed = false;
					}
				});
		}
		for (auto& Worker : Workers)
		{
			Worker.join();
		}
		CHECK(Passed);
		const auto DeepText = std::string("{\"const\":") + std::string(250, '[') + "0" + std::string(250, ']') + "}";
		FJsonValue DeepSource;
		FSchema DeepSchema;
		CHECK(TryParse(DeepText, DeepSource, Error));
		CHECK(FSchema::TryCompile(DeepSource, DeepSchema, Error));
		DeepSchema = {};
		std::cout << "Schema API, precision, resources, reports, cancellation and concurrency tests passed\n";
		return 0;
	}
	catch (const std::exception& Failure)
	{
		std::cerr << Failure.what() << '\n';
		return 1;
	}
}
