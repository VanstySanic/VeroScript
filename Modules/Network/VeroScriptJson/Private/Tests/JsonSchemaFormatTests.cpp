// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJsonSchema.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace VeroScript::Json;
static void Check(bool Value, int Line)
{
	if (!Value)
	{
		throw std::runtime_error("Schema format regression at line " + std::to_string(Line));
	}
}
#define CHECK(X) Check((X), __LINE__)
static FJsonValue Parse(std::string_view Text)
{
	FJsonValue Out;
	FError E;
	CHECK(TryParse(Text, Out, E));
	return Out;
}
static FJsonValue Get(const FJsonValue& Value, const char* Name)
{
	FJsonValue Out;
	FError E;
	CHECK(Value.TryGet(Name, Out, E));
	return Out;
}
static FJsonValue At(const FJsonValue& Value, std::int64_t I)
{
	FJsonValue Out;
	FError E;
	CHECK(Value.TryAt(I, Out, E));
	return Out;
}
static std::int64_t Size(const FJsonValue& Value)
{
	std::int64_t N = 0;
	FError E;
	CHECK(Value.TrySize(N, E));
	return N;
}
static std::string String(const FJsonValue& Value)
{
	std::string S;
	FError E;
	CHECK(Value.TryString(S, E));
	return S;
}
static FSchema Compile(
	const FJsonValue& Value, FSchemaOptions Options = {}, std::vector<FSchemaResource> Resources = {})
{
	FSchema Out;
	FError Error;
	if (!FSchema::TryCompile(Value, Out, Error, Options, Resources))
	{
		std::string Text;
		FError Diagnostic;
		TryWrite(Value, Text, Diagnostic);
		throw std::runtime_error(Error.Message + " while compiling " + Text);
	}
	return Out;
}
static bool Valid(const FSchema& Schema, const FJsonValue& Data)
{
	bool Out = false;
	FError Error;
	if (!Schema.TryIsValid(Data, Out, Error))
	{
		throw std::runtime_error(Error.Message);
	}
	FSchemaReport Report;
	CHECK(Schema.TryValidate(Data, Report, Error));
	CHECK(Report.bValid == Out);
	return Out;
}
static FJsonValue Meta(bool Assert = true, bool Validation = true, bool Applicator = true, bool Optional = false)
{
	std::string S =
		R"({"$schema":"https://json-schema.org/draft/2020-12/schema","$id":"https://example.test/meta","$ref":"https://json-schema.org/draft/2020-12/schema","$vocabulary":{"https://json-schema.org/draft/2020-12/vocab/core":true)";
	if (Assert)
	{
		S += R"(,"https://json-schema.org/draft/2020-12/vocab/format-assertion":)" +
			 std::string(Optional ? "false" : "true");
	}
	if (Validation)
	{
		S += R"(,"https://json-schema.org/draft/2020-12/vocab/validation":)" + std::string(Optional ? "false" : "true");
	}
	if (Applicator)
	{
		S += R"(,"https://json-schema.org/draft/2020-12/vocab/applicator":true)";
	}
	return Parse(S + "}}");
}
static void Contracts()
{
	FError Error;
	FSchemaOptions Force;
	Force.bAssertFormats = true;
	const auto Source = Parse(R"({"format":"email"})");
	CHECK(Valid(Compile(Source), Parse(R"("bad")")));
	CHECK(!Valid(Compile(Source, Force), Parse(R"("bad")")));
	CHECK(Valid(Compile(Source, Force), Parse("9007199254740993")));
	const auto Strict = Compile(Parse(R"({"$schema":"https://example.test/meta","format":"email"})"),
		{},
		{{"https://example.test/meta", Meta()}});
	CHECK(!Valid(Strict, Parse(R"("bad")")) && Valid(Strict, Parse(R"("x@example.test")")));
	const auto Optional = Compile(Parse(R"({"$schema":"https://example.test/meta","format":"email","type":"string"})"),
		{},
		{{"https://example.test/meta", Meta(true, true, true, true)}});
	CHECK(!Valid(Optional, Parse(R"("bad")")) && !Valid(Optional, Parse("1")));
	FSchema Old = Strict;
	CHECK(!FSchema::TryCompile(Parse(R"({"format":"not-a-standard-format"})"), Old, Error, Force));
	CHECK(Error.Code == EError::Schema && !Valid(Old, Parse(R"("bad")")));
	CHECK(Valid(Compile(Parse(R"({"format":"not-a-standard-format"})")), Parse("1")));
	CHECK(!FSchema::TryCompile(Parse(R"({"$schema":"https://example.test/meta","format":"not-a-standard-format"})"),
		Old,
		Error,
		{},
		{{"https://example.test/meta", Meta()}}));
	CHECK(Error.Code == EError::Schema);
	auto UnknownMeta = Parse(
		R"({"$schema":"https://json-schema.org/draft/2020-12/schema","$vocabulary":{"https://example.test/unknown":true}})");
	CHECK(!FSchema::TryCompile(Parse(R"({"$schema":"https://example.test/meta"})"),
		Old,
		Error,
		{},
		{{"https://example.test/meta", UnknownMeta}}));
	CHECK(Error.Code == EError::Unsupported);
	CHECK(Valid(Compile(UnknownMeta), Parse("1")));
	const auto NoValidation = Compile(Parse(R"({"$schema":"https://example.test/meta","minimum":10})"),
		{},
		{{"https://example.test/meta", Meta(false, false, true)}});
	CHECK(Valid(NoValidation, Parse("1")));
	FSchemaReport Report;
	CHECK(NoValidation.TryValidate(Parse("1"), Report, Error));
	CHECK(Report.Annotations.size() == 1 && Report.Annotations[0].Keyword == "minimum");
	const auto AppOnly = Compile(Parse(R"({"$schema":"https://example.test/meta","allOf":[false]})"),
		{},
		{{"https://example.test/meta", Meta(false, false, true)}});
	CHECK(!Valid(AppOnly, Parse("1")));
	const auto NoApp = Compile(Parse(R"({"$schema":"https://example.test/meta","allOf":[false],"type":"integer"})"),
		{},
		{{"https://example.test/meta", Meta(false, true, false)}});
	CHECK(Valid(NoApp, Parse("1")) && !Valid(NoApp, Parse(R"("bad")")));
	const auto Contains = Compile(Parse(R"({"$schema":"https://example.test/meta","contains":true,"minContains":0})"),
		{},
		{{"https://example.test/meta", Meta(false, false, true)}});
	CHECK(!Valid(Contains, Parse("[]")) && Valid(Contains, Parse("[1]")));
	// Embedded resources inherit their enclosing dialect, unless explicitly changed.
	const auto Embedded = Compile(
		Parse(
			R"({"$schema":"https://example.test/meta","properties":{"x":{"$id":"https://example.test/embedded","format":"email"}}})"),
		{},
		{{"https://example.test/meta", Meta()}});
	CHECK(!Valid(Embedded, Parse(R"({"x":"bad"})")));
	const auto Switched = Compile(
		Parse(
			R"({"$schema":"https://example.test/meta","properties":{"x":{"$id":"https://example.test/embedded","$schema":"https://json-schema.org/draft/2020-12/schema","format":"email"}}})"),
		{},
		{{"https://example.test/meta", Meta()}});
	CHECK(Valid(Switched, Parse(R"({"x":"bad"})")));
	// Separate documents without $schema use the module's fixed default dialect.
	const auto External = Compile(
		Parse(R"({"$schema":"https://example.test/meta","properties":{"x":{"$ref":"https://example.test/plain"}}})"),
		{},
		{{"https://example.test/meta", Meta()}, {"https://example.test/plain", Source}});
	CHECK(Valid(External, Parse(R"({"x":"bad"})")));
	const auto SelfMeta = Parse(
		R"({"$id":"https://example.test/self","$schema":"https://example.test/self","$vocabulary":{"https://json-schema.org/draft/2020-12/vocab/core":true,"https://json-schema.org/draft/2020-12/vocab/validation":true,"https://json-schema.org/draft/2020-12/vocab/format-assertion":true},"type":["object","boolean"]})");
	const auto Self = Compile(Parse(R"({"$schema":"https://example.test/self","format":"email"})"),
		{},
		{{"https://example.test/self", SelfMeta}});
	CHECK(!Valid(Self, Parse(R"("bad")")));
	const auto CycleA = Parse(
		R"({"$id":"https://example.test/a","$schema":"https://example.test/b","$vocabulary":{"https://json-schema.org/draft/2020-12/vocab/core":true,"https://json-schema.org/draft/2020-12/vocab/format-assertion":true},"type":["object","boolean"]})");
	const auto CycleB = Parse(
		R"({"$id":"https://example.test/b","$schema":"https://example.test/a","$vocabulary":{"https://json-schema.org/draft/2020-12/vocab/core":true,"https://json-schema.org/draft/2020-12/vocab/format-assertion":true},"type":["object","boolean"]})");
	const auto Cycle = Compile(Parse(R"({"$schema":"https://example.test/a","format":"email"})"),
		{},
		{{"https://example.test/a", CycleA}, {"https://example.test/b", CycleB}});
	CHECK(!Valid(Cycle, Parse(R"("bad")")));
	const auto U = Compile(Parse(R"({"format":"idn-hostname"})"), Force);
	CHECK(Valid(U, Parse(R"("cafe\u0301.com")")));
	CHECK(!Valid(U, Parse(R"("0a.א")")));
	bool Previous = true;
	CHECK(!U.TryIsValid(Parse(R"("example.test")"),
			  Previous,
			  Error,
			  {},
			  {[](void*, std::uint64_t)
				  {
					  return false;
				  },
				  nullptr}) &&
		  Previous && Error.Code == EError::Cancelled);
	// Unknown syntax remains a compile error even if the instance would be non-string.
	CHECK(!FSchema::TryCompile(Parse(R"({"format":""})"), Old, Error, Force) && Error.Code == EError::Schema);
	FJsonValue Large;
	CHECK(FJsonValue::TryString(std::string(5000, 'a'), Large, Error));
	Previous = true;
	CHECK(!U.TryIsValid(Large, Previous, Error) && Previous && Error.Code == EError::Limit);
	for (std::uint64_t Countdown = 1; Countdown < 256; Countdown += 11)
	{
		auto Remaining = Countdown;
		Previous = false;
		const bool Success = U.TryIsValid(Parse(R"("例え.テスト")"),
			Previous,
			Error,
			{},
			{[](void* P, std::uint64_t)
				{
					auto& N = *static_cast<std::uint64_t*>(P);
					if (!--N)
					{
						throw std::runtime_error("work failed");
					}
					return true;
				},
				&Remaining});
		CHECK(Success ? Previous : Error.Code == EError::Callback && !Previous);
	}
	std::cout << "Format options, dialect vocabulary ownership and resource boundaries passed\n";
}
static void Corpus(const std::filesystem::path& Root)
{
	std::uint64_t Passed = 0, Failed = 0;
	std::vector<std::filesystem::path> Files;
	for (const auto& File : std::filesystem::directory_iterator(Root / "tests/draft2020-12/optional/format"))
	{
		if (File.is_regular_file() && File.path().extension() == ".json")
		{
			Files.push_back(File.path());
		}
	}
	std::sort(Files.begin(), Files.end());
	CHECK(!Files.empty());
	for (const auto& File : Files)
	{
		std::ifstream Input(File, std::ios::binary);
		std::string Text{std::istreambuf_iterator<char>(Input), {}};
		const auto Groups = Parse(Text);
		for (std::int64_t G = 0; G < Size(Groups); ++G)
		{
			const auto Group = At(Groups, G), Source = Get(Group, "schema"), Tests = Get(Group, "tests");
			FSchemaOptions Options;
			Options.bAssertFormats = File.stem() != "unknown";
			const auto Schema = Compile(Source, Options);
			for (std::int64_t T = 0; T < Size(Tests); ++T)
			{
				const auto Test = At(Tests, T);
				bool Expected = false;
				FError Error;
				CHECK(Get(Test, "valid").TryBoolean(Expected, Error));
				if (Valid(Schema, Get(Test, "data")) == Expected)
				{
					++Passed;
				}
				else
				{
					++Failed;
					std::cerr << File.filename().generic_string() << ": " << String(Get(Test, "description")) << '\n';
				}
			}
		}
	}
	std::cout << "Public Schema format corpus: " << Passed << " passed, " << Failed << " failed\n";
	CHECK(!Failed);
}
int main(int Argc, char** Argv)
{
	try
	{
		Contracts();
		if (Argc > 1)
		{
			Corpus(std::filesystem::absolute(Argv[1]));
		}
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
