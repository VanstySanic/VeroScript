// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJsonSchema.h"
#include <atomic>
#include <iostream>
#include <thread>

using namespace VeroScript::Json;
static void Check(bool Value, int Line)
{
	if (!Value)
	{
		throw std::runtime_error("Schema annotation regression at line " + std::to_string(Line));
	}
}
#define CHECK(X) Check((X), __LINE__)
static FJsonValue Parse(const char* Text)
{
	FJsonValue Out;
	FError Error;
	CHECK(TryParse(Text, Out, Error));
	return Out;
}
static FSchema Compile(const char* Text)
{
	FSchema Out;
	FError Error;
	if (!FSchema::TryCompile(Parse(Text), Out, Error))
	{
		throw std::runtime_error(Error.Message);
	}
	return Out;
}
static FSchemaReport Validate(const FSchema& Schema, const char* Text, const FSchemaReportOptions& Options = {})
{
	FSchemaReport Out;
	FError Error;
	if (!Schema.TryValidateWithOptions(Parse(Text), Out, Error, Options))
	{
		throw std::runtime_error(Error.Message);
	}
	return Out;
}
static const FSchemaReport::FAnnotation& Find(
	const FSchemaReport& Report, std::string_view Keyword, std::string_view Pointer = "", std::string_view Path = {})
{
	for (const auto& A : Report.Annotations)
	{
		if (A.Keyword == Keyword && A.InstancePointer == Pointer && (Path.empty() || A.EvaluationPath == Path))
		{
			return A;
		}
	}
	throw std::runtime_error(
		"Missing annotation: " + std::string(Keyword) + " at " + std::string(Pointer) + " via " + std::string(Path));
}
static bool Equals(const FJsonValue& Value, const char* Text)
{
	bool Out = false;
	FError Error;
	CHECK(Value.TryEquals(Parse(Text), Out, Error));
	return Out;
}
int main()
{
	try
	{
		const auto Schema = Compile(
			R"({"$id":"https://example.test/model","title":"root","$comment":"private","default":9007199254740993.5,"x-editor":{"mode":[1,true]},"anyOf":[{"type":"integer","title":"integer"},{"type":"number","description":"number"},{"type":"string","title":"discard"}]})");
		auto Report = Validate(Schema, "1");
		CHECK(Report.bValid && !Report.bTruncated && Report.Annotations.size() == 5);
		CHECK(Equals(Find(Report, "title", "", "/anyOf/0/title").Value, R"("integer")"));
		CHECK(Find(Report, "description").EvaluationPath == "/anyOf/1/description");
		CHECK(Find(Report, "title", "", "/title").SchemaLocation == "https://example.test/model#/title");
		CHECK(Equals(Find(Report, "default").Value, "9007199254740993.5"));
		CHECK(Equals(Find(Report, "x-editor").Value, R"({"mode":[1,true]})"));
		auto Invalid = Validate(Schema, "false");
		CHECK(!Invalid.bValid && Invalid.Annotations.empty());
		CHECK(Validate(Compile(R"({"oneOf":[{"title":"a"},{"title":"b"}]})"), "1").Annotations.empty());
		CHECK(Validate(Compile(R"({"not":{"title":"hidden"}})"), "1").Annotations.empty());
		auto Conditional = Validate(
			Compile(R"({"if":{"type":"integer","title":"condition"},"then":{"title":"then"},"else":{"title":"else"}})"),
			"1");
		CHECK(Conditional.Annotations.size() == 2);
		CHECK(Equals(Find(Conditional, "title", "", "/then/title").Value, R"("then")"));
		Conditional =
			Validate(Compile(R"({"if":{"type":"integer","title":"condition"},"else":{"title":"else"}})"), "false");
		CHECK(Conditional.Annotations.size() == 1 && Conditional.Annotations[0].EvaluationPath == "/else/title");
		auto Object = Validate(
			Compile(
				R"({"properties":{"a/b~c":{"title":"escaped"}},"patternProperties":{"^x":{"description":"x"}},"additionalProperties":{"title":"extra"}})"),
			R"({"a/b~c":1,"x1":2,"z":3})");
		CHECK(Find(Object, "title", "/a~1b~0c").EvaluationPath == "/properties/a~1b~0c/title");
		CHECK(Find(Object, "description", "/x1").EvaluationPath == "/patternProperties/^x/description");
		const auto NestedPattern =
			Validate(Compile(R"({"patternProperties":{"^x":{"contains":true}}})"), R"({"x1":[1]})");
		CHECK(Find(NestedPattern, "contains", "/x1").SchemaLocation ==
			  "urn:veroscript:json-schema:root#/patternProperties/%5Ex/contains");
		CHECK(Equals(Find(Object, "properties").Value, R"(["a/b~c"])"));
		CHECK(Equals(Find(Object, "patternProperties").Value, R"(["x1"])"));
		CHECK(Equals(Find(Object, "additionalProperties").Value, R"(["z"])"));
		auto Unevaluated = Validate(
			Compile(R"({"properties":{"a":true},"unevaluatedProperties":{"title":"rest"}})"), R"({"a":1,"b":2})");
		CHECK(Equals(Find(Unevaluated, "unevaluatedProperties").Value, R"(["b"])"));
		auto Array =
			Validate(Compile(R"({"contains":{"type":"integer","title":"match"},"minContains":0})"), "[1,false,2]");
		CHECK(Array.Annotations.size() == 3 && Equals(Find(Array, "contains").Value, "[0,2]"));
		CHECK(Equals(Find(Array, "title", "/2").Value, R"("match")"));
		CHECK(Equals(Find(Validate(Compile(R"({"contains":true})"), "[1,2]"), "contains").Value, "true"));
		CHECK(Equals(Find(Validate(Compile(R"({"prefixItems":[true]})"), "[1,2]"), "prefixItems").Value, "0"));
		CHECK(
			Equals(Find(Validate(Compile(R"({"prefixItems":[true],"items":true})"), "[1,2]"), "items").Value, "true"));
		CHECK(Equals(
			Find(Validate(Compile(R"({"prefixItems":[true],"items":true})"), "[1,2]"), "prefixItems").Value, "0"));
		CHECK(Equals(
			Find(Validate(Compile(R"({"prefixItems":[true],"unevaluatedItems":true})"), "[1,2]"), "unevaluatedItems")
				.Value,
			"true"));
		const auto Sparse = Compile(
			R"({"allOf":[{"contains":{"type":"number"},"unevaluatedItems":{"type":"string"}}],"unevaluatedItems":false})");
		bool SparseValid = false;
		FError SparseError;
		CHECK(Sparse.TryIsValid(Parse(R"([1,"a",2,"b"])"), SparseValid, SparseError) && SparseValid);
		CHECK(Validate(Sparse, R"([1,"a",2,"b"])").bValid);
		const auto Tail = Validate(Compile(R"({"prefixItems":[true],"items":{"title":"tail"}})"), "[1,2]");
		CHECK(Find(Tail, "title", "/1").SchemaLocation == "urn:veroscript:json-schema:root#/items/title");
		CHECK(Find(Tail, "title", "/1").EvaluationPath == "/items/title");
		const auto BadItems = Validate(Compile(R"({"unevaluatedItems":false})"), "[1]");
		CHECK(!BadItems.bValid && BadItems.Issues[0].Keyword == "unevaluatedItems" &&
			  BadItems.Issues[0].SchemaLocation == "urn:veroscript:json-schema:root#/unevaluatedItems");
		auto Ref = Validate(Compile(R"({"$defs":{"data":{"title":"target"}},"$ref":"#/$defs/data"})"), "1");
		CHECK(Find(Ref, "title").EvaluationPath == "/$ref/title");
		CHECK(Find(Ref, "title").SchemaLocation == "urn:veroscript:json-schema:root#/$defs/data/title");
		CHECK(Find(Validate(Compile(R"({"x%2F":17})"), "1"), "x%2F").SchemaLocation ==
			  "urn:veroscript:json-schema:root#/x%252F");
		FSchema ResourceSchema;
		FError Error;
		{
			std::vector<FSchemaResource> Resources{
				{"https://example.test/external", Parse(R"({"title":"owned","default":[{"x":1}]})")}};
			CHECK(FSchema::TryCompile(
				Parse(R"({"$ref":"https://example.test/external"})"), ResourceSchema, Error, {}, Resources));
		}
		auto Owned = Validate(ResourceSchema, "1");
		ResourceSchema = {};
		CHECK(Equals(Find(Owned, "default").Value, R"([{"x":1}])"));
		const auto Content = Compile(
			R"({"contentMediaType":"application/json","contentSchema":{"type":"integer"},"$comment":"hidden"})");
		CHECK(Validate(Content, "1").Annotations.empty());
		CHECK(Validate(Content, R"("1")").Annotations.size() == 2);
		CHECK(Validate(Compile(R"({"contentSchema":{"title":"ignored"}})"), R"("x")").Annotations.empty());
		FSchemaReportOptions Options;
		Options.MaxAnnotations = 0;
		auto Limited = Validate(Schema, "1", Options);
		CHECK(Limited.bValid && Limited.bTruncated && Limited.bAnnotationsTruncated && Limited.Annotations.empty());
		Limited = Validate(Compile(R"({"anyOf":[{"type":"string","title":"discard"},true]})"), "1", Options);
		CHECK(Limited.bValid && !Limited.bAnnotationsTruncated);
		Options.bCollectAnnotations = false;
		Limited = Validate(Schema, "1", Options);
		CHECK(Limited.bValid && Limited.Annotations.empty() && !Limited.bTruncated);
		Options.bCollectAnnotations = true;
		Options.MaxAnnotations = 256;
		Options.Limits.OutputBytes = 8;
		auto Output = Owned;
		CHECK(!Schema.TryValidateWithOptions(Parse("1"), Output, Error, Options) && Error.Code == EError::Limit);
		CHECK(Output.Annotations.size() == Owned.Annotations.size());
		for (std::uint64_t Remaining = 0; Remaining < 256; Remaining += 7)
		{
			std::uint64_t Budget = Remaining;
			Output = Owned;
			const bool Success = Schema.TryValidateWithOptions(Parse("1"),
				Output,
				Error,
				{},
				{[](void* P, std::uint64_t Units)
					{
						auto& N = *static_cast<std::uint64_t*>(P);
						if (Units > N)
						{
							return false;
						}
						N -= Units;
						return true;
					},
					&Budget});
			CHECK(
				Success || (Error.Code == EError::Cancelled && Output.Annotations.size() == Owned.Annotations.size()));
		}
		CHECK(!Schema.TryValidateWithOptions(Parse("1"),
				  Output,
				  Error,
				  {},
				  {[](void*, std::uint64_t)
					  {
						  return false;
					  },
					  nullptr}) &&
			  Error.Code == EError::Cancelled);
		CHECK(Output.Annotations.size() == Owned.Annotations.size());
		std::atomic<bool> Good{true};
		std::vector<std::thread> Workers;
		for (int I = 0; I < 4; ++I)
		{
			Workers.emplace_back(
				[&]
				{
					try
					{
						for (int J = 0; J < 20; ++J)
						{
							if (Validate(Schema, "1").Annotations.size() != 5)
							{
								Good = false;
							}
						}
					}
					catch (...)
					{
						Good = false;
					}
				});
		}
		for (auto& Worker : Workers)
		{
			Worker.join();
		}
		CHECK(Good);
		std::cout << "Schema annotation branches, locations, ownership, bounds and concurrency passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
