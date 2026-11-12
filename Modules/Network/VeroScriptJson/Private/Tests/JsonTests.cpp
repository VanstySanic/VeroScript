// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJson.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace VeroScript::Json;

static void Check(bool Passed, const char* Expression, int Line)
{
	if (!Passed)
	{
		throw std::runtime_error(std::string("Line ") + std::to_string(Line) + ": " + Expression);
	}
}
#define CHECK(X) Check((X), #X, __LINE__)

static FJsonValue Parse(std::string_view Text)
{
	FJsonValue Value;
	FError Error;
	CHECK(TryParse(Text, Value, Error));
	return Value;
}

static std::string Text(const FJsonValue& Value)
{
	std::string Out;
	FError Error;
	CHECK(TryWrite(Value, Out, Error));
	return Out;
}

static void DataTests()
{
	FError E;
	for (auto Source : {"null",
			 "true",
			 "false",
			 "[]",
			 "{}",
			 "\"\"",
			 "-0",
			 "1e400",
			 "9007199254740993",
			 "18446744073709551615",
			 "0.12345678901234567890123456789"})
	{
		CHECK(Text(Parse(Source)) == Source);
	}
	FJsonValue Out(std::int64_t{42});
	for (auto Bad : {"",
			 " ",
			 "01",
			 "-",
			 "1.",
			 "1e",
			 "NaN",
			 "[1,]",
			 "{\"a\":1,}",
			 "[}",
			 "true x",
			 "null null",
			 "\"\\uD800\"",
			 "\"\\uDC00\"",
			 "\"\\uDC00\\uD800\"",
			 "{\"a\":0,\"\\u0061\":1}",
			 "\"\\x\"",
			 "\"a\n\""})
	{
		CHECK(!TryParse(Bad, Out, E));
		CHECK(E && E.bHasSource);
		CHECK(Text(Out) == "42");
	}
	CHECK(!TryParse(std::string("\"\xc0\x80\"", 4), Out, E));
	CHECK(!TryParse("\xef\xbb\xbf[]", Out, E));
	FParseOptions Options;
	Options.bAllowLeadingBom = true;
	CHECK(TryParse("\xef\xbb\xbf[]", Out, E, Options));
	CHECK(Text(Out) == "[]");
	Options.DuplicateKeys = EDuplicateKeys::KeepFirst;
	CHECK(TryParse("{\"a\":1,\"b\":2,\"a\":3}", Out, E, Options));
	CHECK(Text(Out) == "{\"a\":1,\"b\":2}");
	Options.DuplicateKeys = EDuplicateKeys::KeepLast;
	CHECK(TryParse("{\"a\":1,\"b\":2,\"a\":3}", Out, E, Options));
	CHECK(Text(Out) == "{\"a\":3,\"b\":2}");
	auto Original = Parse("{\"a\":[1,2]}");
	auto Copy = Original;
	FJsonValue Child;
	CHECK(Copy.TryGet("a", Child, E));
	bool Found=false;
	CHECK(Copy.TryContains("a",Found,E) && Found);
	CHECK(Copy.TryContains("missing",Found,E) && !Found);
	CHECK(!FJsonValue{}.TryContains("a",Found,E) && !Found && E.Code==EError::Type);
	CHECK(Child.TryPush(FJsonValue(std::int64_t{3}), E));
	CHECK(Text(Copy) == Text(Original));
	CHECK(Copy.TrySet("a", Child, E));
	CHECK(Text(Copy) == "{\"a\":[1,2,3]}");
	CHECK(Text(Original) == "{\"a\":[1,2]}");
	bool Equal = false;
	CHECK(Parse("{\"a\":1,\"b\":-0}").TryEquals(Parse("{\"b\":0,\"a\":1.0}"), Equal, E) && Equal);
	CHECK(Parse("1").TryEquals(Parse("1e0"), Equal, E) && Equal);
	CHECK(Parse("9007199254740993").TryEquals(Parse("9007199254740992"), Equal, E) && !Equal);
	std::int64_t Signed = 17;
	std::uint64_t Unsigned = 17;
	CHECK(Parse("-9223372036854775808").TryInt64(Signed, E) && Signed == INT64_MIN);
	CHECK(Parse("9223372036854775807").TryInt64(Signed, E) && Signed == INT64_MAX);
	CHECK(Parse("18446744073709551615").TryUInt64(Unsigned, E) && Unsigned == UINT64_MAX);
	CHECK(!Parse("9223372036854775808").TryInt64(Signed, E) && Signed == INT64_MAX);
	CHECK(!Parse("18446744073709551616").TryUInt64(Unsigned, E) && Unsigned == UINT64_MAX);
	CHECK(Parse("1.0").TryInt64(Signed, E) && Signed == 1);
	CHECK(!Parse("1.5").TryInt64(Signed, E));
	double Number = 7;
	CHECK(Parse("0.5").TryDouble(Number, E, true) && Number == 0.5);
	CHECK(!Parse("0.1").TryDouble(Number, E, true) && Number == 0.5);
	CHECK(Parse("0.1").TryDouble(Number, E));
	CHECK(!Parse("1e400").TryDouble(Number, E));
	CHECK(!Parse("1e-400").TryDouble(Number, E));
	CHECK(!FJsonValue::TryDouble(std::numeric_limits<double>::infinity(), Out, E));
	CHECK(Text(Parse("\"\\ud83d\\ude00\\u0000\"")) == "\"\xf0\x9f\x98\x80\\u0000\"");
	Options = {};
	Options.Limits.InputBytes = 3;
	CHECK(!TryParse("null", Out, E, Options) && E.Code == EError::Limit);
	FWork Cancel{[](void*, std::uint64_t)
		{
			return false;
		},
		nullptr};
	CHECK(!TryParse("[]", Out, E, {}, Cancel) && E.Code == EError::Cancelled);
	std::uint64_t Units = 0;
	FWork Account{[](void* State, std::uint64_t Count)
		{
			*static_cast<std::uint64_t*>(State) += Count;
			return true;
		},
		&Units};
	CHECK(TryParse("[1,true]", Out, E, {}, Account) && Units == 8);
	std::string Output = "unchanged";
	CHECK(!TryWrite(Original, Output, E, {}, Cancel) && Output == "unchanged");
	Options = {};
	Options.Limits.Depth = 1;
	CHECK(TryParse("[]", Out, E, Options));
	CHECK(!TryParse("[null]", Out, E, Options) && E.Code == EError::Limit);
	CHECK(Parse("-0e100000").TryDouble(Number, E, true) && Number == 0);
	FBuilder Builder;
	auto SharedBuilder = Builder;
	CHECK(Builder.TryReserve(10000, E));
	for (std::int64_t I = 0; I < 10000; ++I)
	{
		CHECK(Builder.TryPush(FJsonValue(I), E));
	}
	CHECK(SharedBuilder.TryFinish(Out, E));
	CHECK(Out.TrySize(Signed, E) && Signed == 10000);
	CHECK(!Builder.TryPush({}, E) && E.Code == EError::State);
	CHECK(Builder.TryReset(EKind::Object, E));
	CHECK(Builder.TrySet("a", FJsonValue(std::int64_t{1}), E));
	CHECK(Builder.TrySet("a", FJsonValue(std::int64_t{2}), E));
	CHECK(Builder.TryFinish(Out, E) && Text(Out) == "{\"a\":2}");
	CHECK(Builder.TryReset(EKind::Object, E));
	for (std::int64_t I = 0; I < 10000; ++I)
	{
		CHECK(Builder.TrySet(std::to_string(I), FJsonValue(I), E));
	}
	CHECK(Builder.TryFinish(Out, E));
	for (std::int64_t I = 0; I < 10000; ++I)
	{
		FJsonValue Member;
		CHECK(Out.TryGet(std::to_string(I), Member, E));
		CHECK(Member.TryInt64(Signed, E) && Signed == I);
	}
	auto Snapshot = Out;
	CHECK(Out.TryErase("0", E));
	CHECK(!Out.TryGet("0", Child, E));
	CHECK(Out.TryGet("9999", Child, E) && Child.TryInt64(Signed, E) && Signed == 9999);
	CHECK(Snapshot.TryGet("0", Child, E));
	CHECK(Out.TrySet("0", FJsonValue(std::int64_t{123}), E));
	CHECK(Out.TryGet("0", Child, E) && Child.TryInt64(Signed, E) && Signed == 123);
}

static void PatchTests()
{
	FError E;
	FDocument Input{true, Parse("{\"a\":[0,1,2,3],\"~1/\":7,\"\":8}")}, Out;
	FJsonValue Value;
	CHECK(TryResolve(Input, "/~01~1", Value, E) && Text(Value) == "7");
	CHECK(TryResolve(Input, "/", Value, E) && Text(Value) == "8");
	CHECK(!TryResolve(Input, "/a/01", Value, E));
	CHECK(!TryResolve(Input, "/a/-", Value, E));
	std::vector<std::string> Tokens;
	CHECK(TryParsePointer("#/%7E01%7E1", Tokens, E, true) && Tokens[0] == "~1/");
	CHECK(!TryParsePointer("/~2", Tokens, E));
	FPointer Compiled;
	CHECK(FPointer::TryCompile("#/%7E01%7E1", Compiled, E, true));
	CHECK(Compiled.TryResolve(Input, Value, E) && Text(Value) == "7");
	CHECK(!FPointer::TryCompile("/bad~", Compiled, E));
	CHECK(Compiled.TryResolve(Input, Value, E) && Text(Value) == "7");
	FLimits PointerLimits;
	PointerLimits.Depth = 1;
	CHECK(!TryParsePointer("/a/b", Tokens, E, false, PointerLimits) && E.Code == EError::Limit);
	PointerLimits.Depth = 256;
	PointerLimits.TokenBytes = 2;
	CHECK(!Compiled.TryResolve(Input, Value, E, PointerLimits) && E.Code == EError::Limit);
	PointerLimits.TokenBytes = 3;
	CHECK(!FPointer::TryCompile("/abcd", Compiled, E, false, PointerLimits) && E.Code == EError::Limit);
	CHECK(Compiled.TryResolve(Input, Value, E) && Text(Value) == "7");
	FWork Stop{[](void*, std::uint64_t)
		{
			return false;
		},
		nullptr};
	CHECK(!TryResolve(Input, "", Value, E, {}, Stop) && E.Code == EError::Cancelled && Text(Value) == "7");
	std::uint64_t WorkCalls = 0;
	FWork Later{[](void* P, std::uint64_t)
		{
			return ++*static_cast<std::uint64_t*>(P) < 4;
		},
		&WorkCalls};
	CHECK(!FPointer::TryCompile("/" + std::string(10000, 'x'), Compiled, E, false, {}, Later));
	CHECK(E.Code == EError::Cancelled && WorkCalls == 4);
	CHECK(Compiled.TryResolve(Input, Value, E) && Text(Value) == "7");
	CHECK(!TryParsePointer(std::string(257, '/'), Tokens, E) && E.Code == EError::Limit);
	CHECK(TryPatch(Input,
		Parse(R"([{"op":"move","from":"/a/1","path":"/a/3"},{"op":"test","path":"/a/3","value":1.0}])"),
		Out,
		E));
	CHECK(TryResolve(Out, "/a", Value, E) && Text(Value) == "[0,2,3,1]");
	CHECK(TryPatch(Input, Parse(R"([{"op":"remove","path":""}])"), Out, E) && !Out.bHasValue);
	CHECK(TryPatch(Out, Parse(R"([{"op":"add","path":"","value":null}])"), Out, E) && Out.bHasValue &&
		  Out.Value.Kind() == EKind::Null);
	Out = Input;
	CHECK(!TryPatch(Input, Parse(R"([{"op":"remove","path":"/a"},{"op":"remove","path":"/missing"}])"), Out, E));
	CHECK(Text(Out.Value) == Text(Input.Value) && E.PatchOperation == 1);
	CHECK(!TryPatch(Input, Parse(R"([{"op":"move","from":"/a","path":"/a/0"}])"), Out, E));
	CHECK(TryPatch(Input, Parse(R"([{"op":"move","from":"/a","path":"/a"}])"), Out, E));
	CHECK(TryMergePatch(Parse(R"({"a":1,"b":{"c":2}})"), Parse(R"({"a":null,"b":{"d":3}})"), Value, E));
	CHECK(Text(Value) == R"({"b":{"c":2,"d":3}})");
	CHECK(TryMergePatch(Value, Parse("null"), Value, E) && Value.Kind() == EKind::Null);
}

static void StreamTests()
{
	const std::string Source = R"({"s":"a\n\ud83d\ude00","n":123.45e-6,"a":[true,false,null,{},[]]})";
	for (std::size_t ChunkSize = 1; ChunkSize <= Source.size(); ++ChunkSize)
	{
		FReader Reader;
		FError E;
		std::string Output;
		bool Paused = false;
		FWriter Writer(
			[&](std::string_view Part)
			{
				Paused = !Paused;
				if (Paused)
				{
					return FSinkResult{0, ESinkState::NeedOutput};
				}
				auto N = std::min<std::size_t>(Part.size(), 2);
				Output.append(Part.substr(0, N));
				return FSinkResult{N, ESinkState::Progress};
			});
		std::size_t Offset = 0;
		bool Done = false;
		while (!Done)
		{
			auto End = std::min(Source.size(), Offset + ChunkSize);
			auto Chunk = std::string_view(Source).substr(Offset, End - Offset);
			FEvent Event;
			auto R = Reader.Feed(Chunk, End == Source.size(), Event, E);
			CHECK(R.State != EReadState::Failed);
			Offset += R.Consumed;
			if (R.State == EReadState::EventReady)
			{
				bool Accepted = false;
				auto State = Writer.Write(Event, Accepted, E);
				CHECK(Accepted && State != EWriteState::Failed);
				while (State == EWriteState::NeedOutput)
				{
					State = Writer.Resume(E);
				}
				CHECK(State == EWriteState::Active);
			}
			Done = R.State == EReadState::Done;
		}
		CHECK(Writer.Finish(E) == EWriteState::Done);
		CHECK(Writer.Finish(E) == EWriteState::Done);
		CHECK(Output == Text(Parse(Source)));
	}
	FReader Reader;
	FEvent Event;
	FError E;
	CHECK(Reader.Feed("1", false, Event, E).State == EReadState::NeedInput);
	CHECK(Reader.Feed("2 ", false, Event, E).State == EReadState::EventReady && Event.Text == "12");
	CHECK(Reader.Feed(" ", true, Event, E).State == EReadState::Done);
	FWriter Writer(
		[](std::string_view)
		{
			return FSinkResult{0, ESinkState::Progress};
		});
	bool Accepted = false;
	CHECK(Writer.Write({}, Accepted, E) == EWriteState::Failed && Accepted);
	FWork Throwing;
	Throwing.Check = [](void*, std::uint64_t) -> bool
	{
		throw std::logic_error("callback");
	};
	FJsonValue Preserved(std::int64_t{17});
	CHECK(!TryParse("null", Preserved, E, {}, Throwing) && E.Code == EError::Callback);
	CHECK(Text(Preserved) == "17" && std::string(E.Id()) == "json.callback");
	FReader Sax;
	std::string Data = "[1,true]";
	std::vector<EEvent> Seen;
	auto Visitor = [&](const FEvent& Current)
	{
		Seen.push_back(Current.Kind);
		return false;
	};
	std::size_t Consumed = 0;
	for (;;)
	{
		auto Next = Sax.Visit(std::string_view(Data).substr(Consumed), true, Visitor, E);
		Consumed += Next.Consumed;
		CHECK(Next.State != EReadState::Failed);
		if (Next.State == EReadState::Done)
		{
			break;
		}
	}
	CHECK(Consumed == Data.size() &&
		  Seen == std::vector<EEvent>({EEvent::BeginArray, EEvent::Number, EEvent::Boolean, EEvent::EndArray}));
	Sax.Reset();
	CHECK(Sax.Visit(
				 "null",
				 true,
				 [](const FEvent&) -> bool
				 {
					 throw std::runtime_error("visitor");
				 },
				 E)
			  .State == EReadState::Failed);
	CHECK(E.Code == EError::Callback && Sax.Feed("", true, Event, E).State == EReadState::Failed);
	const auto Deep = std::string(256, '[') + std::string(256, ']');
	CHECK(Text(Parse(Deep)) == Deep);
	FReader DeepReader;
	std::string DeepOutput;
	FWriter DeepWriter(
		[&](std::string_view Part)
		{
			DeepOutput.append(Part);
			return FSinkResult{Part.size(), ESinkState::Progress};
		});
	auto Visited = DeepReader.Visit(
		Deep,
		true,
		[&](const FEvent& Current)
		{
			bool Accepted = false;
			CHECK(DeepWriter.Write(Current, Accepted, E) == EWriteState::Active && Accepted);
			return true;
		},
		E);
	CHECK(Visited.State == EReadState::Done && DeepWriter.Finish(E) == EWriteState::Done && DeepOutput == Deep);
	FWriter InvalidKind(
		[](std::string_view Part)
		{
			return FSinkResult{Part.size(), ESinkState::Progress};
		});
	CHECK(InvalidKind.Write({static_cast<EEvent>(255), {}, false}, Accepted, E) == EWriteState::Failed && !Accepted);
	FWriter InvalidSink(
		[](std::string_view Part)
		{
			return FSinkResult{Part.size(), static_cast<ESinkState>(255)};
		});
	CHECK(InvalidSink.Write({}, Accepted, E) == EWriteState::Failed && E.Code == EError::Sink);
	FParseOptions InvalidPolicy;
	InvalidPolicy.DuplicateKeys = static_cast<EDuplicateKeys>(255);
	CHECK(!TryParse("null", Preserved, E, InvalidPolicy) && E.Code == EError::State);
}

int main()
{
	try
	{
		DataTests();
		PatchTests();
		StreamTests();
		std::cout << "JSON data, number, patch and incremental stream tests passed\n";
		return EXIT_SUCCESS;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return EXIT_FAILURE;
	}
}
