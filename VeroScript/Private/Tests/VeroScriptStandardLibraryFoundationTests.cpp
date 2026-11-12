// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptStandardLibraryServices.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptConcurrent.h"
#include "VeroScriptFloatParsing.h"
#include "../Parser/VeroScriptParserDetail.h"

#include <cerrno>
#include <cfenv>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>

using namespace VeroScript;
using namespace VeroScript::StandardLibrary;

namespace
{
	bool Expect(bool Ok, const char* Message)
	{
		if (!Ok)
		{
			std::cerr << "FAIL: " << Message << '\n';
		}
		return Ok;
	}
	bool Parse(std::string_view Text, std::string_view Type, FValue& Out, int Base = 10)
	{
		return static_cast<bool>(ParseScalar(Text, Type, Base, true, Out));
	}
}

int main()
{
	bool Ok = true;
	{
		auto Map = std::make_shared<FMap>();
		Map->TypeName = "map<int,int>";
		Map->KeyTypeName = "int";
		Map->ValueTypeName = "int";
		Map->IntEntries.emplace(1, std::int32_t{7});
		Ok &= Expect(Runtime::ValueToString(FValue{Map}) == "map<int,int>[1]", "boxed map display size");
		auto& Ints = Map->ScalarEntries.emplace<FMap::FIntI32Entries>();
		Ok &= Expect(Runtime::ValueToString(FValue{Map}) == "map<int,int>[0]", "scalar map storage is authoritative");
		Ints.InsertOrAssign(3, 9);
		Ints.InsertOrAssign(5, 11);
		Ok &= Expect(Runtime::ValueToString(FValue{Map}) == "map<int,int>[2]", "scalar integer map display size");
		Map->TypeName = "map<string,int>";
		Map->KeyTypeName = "string";
		Map->ScalarEntries.emplace<FMap::FStringI32Entries>().InsertOrAssign("a", 1);
		Ok &= Expect(Runtime::ValueToString(FValue{Map}) == "map<string,int>[1]", "scalar string map display size");
		Map->TypeName = "map<uint64,int64>";
		Map->KeyTypeName = "uint64";
		Map->ValueTypeName = "int64";
		Map->ScalarEntries.emplace<FMap::FUInt64ExtendedEntries>().InsertOrAssign(UINT64_MAX, std::int64_t{INT64_MIN});
		Ok &= Expect(Runtime::ValueToString(FValue{Map}) == "map<uint64,int64>[1]", "extended scalar map display size");
		auto Set = std::make_shared<FSet>();
		Set->TypeName = "set<int>";
		Set->ElementTypeName = "int";
		Set->IntElements.emplace(1);
		Ok &= Expect(Runtime::ValueToString(FValue{Set}) == "set<int>[1]", "boxed set display size");
		auto& Elements = Set->ScalarElements.emplace<FSet::FIntScalarElements>();
		Ok &= Expect(Runtime::ValueToString(FValue{Set}) == "set<int>[0]", "scalar set storage is authoritative");
		Elements.Insert(3);
		Elements.Insert(5);
		Ok &= Expect(Runtime::ValueToString(FValue{Set}) == "set<int>[2]", "scalar integer set display size");
		Set->TypeName = "set<string>";
		Set->ElementTypeName = "string";
		Set->ScalarElements.emplace<FSet::FStringScalarElements>().Insert("a");
		Ok &= Expect(Runtime::ValueToString(FValue{Set}) == "set<string>[1]", "scalar string set display size");
		Ok &= Expect(Runtime::ValueToString(FValue{FMapHandle{}}) == "map<?,?>" && Runtime::ValueToString(FValue{FSetHandle{}}) == "set<?>", "null container display");
	}
	FValue Value = std::int32_t{73};
	for (const auto Text : {"+-1", "-+1", "++1", "--1", "1_0", "0xFF", "1 2", ""})
	{
		Ok &= Expect(!Parse(Text, "int", Value) && std::get<std::int32_t>(Value) == 73, "invalid integer preserves output");
	}
	Ok &= Expect(Parse("\t-9223372036854775808\r\n", "int64", Value) && std::get<std::int64_t>(Value) == INT64_MIN, "int64 minimum");
	Ok &= Expect(Parse("18446744073709551615", "uint64", Value) && std::get<std::uint64_t>(Value) == UINT64_MAX, "uint64 maximum");
	Ok &= Expect(!Parse("18446744073709551616", "uint64", Value), "uint64 overflow");
	Ok &= Expect(!Parse("-0", "uint64", Value), "unsigned negative zero");
	Ok &= Expect(Parse("-0", "int8", Value) && std::get<std::int8_t>(Value) == 0, "signed negative zero");
	Ok &= Expect(Parse("0x", "int", Value, 36) && std::get<std::int32_t>(Value) == 33, "radix letters are digits");
	for (const auto Text : {"+-1.5", "+-0", "1e+-3", "nan", "inf", "0x1p0", ".", "1e", "1.2.3"})
	{
		Ok &= Expect(!Parse(Text, "double", Value), "invalid float grammar");
	}
	for (const auto Text : {".5", "1.", "+1.25e-3", "-0", "4.9406564584124654e-324"})
	{
		Ok &= Expect(Parse(Text, "double", Value), "valid decimal float");
	}
	Ok &= Expect(Parse("-0", "float", Value) && std::signbit(std::get<float>(Value)), "float negative zero");
	Ok &= Expect(!Parse("1e-9999", "double", Value), "float underflow");
	const std::string LongMantissa = "0." + std::string(10000, '0') + "1e10001";
	Ok &= Expect(Parse(LongMantissa, "double", Value) && std::get<double>(Value) == 1., "large cancelling exponent");
	// Values around a binary32 midpoint must round directly to binary32.
	struct FFloatCase
	{
		const char* Text;
		float Expected;
	};
	const FFloatCase FloatCases[] = {
	    {"1.000000059604644775390625", 1.0f},
	    {"1.000000059604644775390626", 0x1.000002p0f},
	    {"1.000000059604644775390624", 1.0f},
	    {"3.40282346638528859811704183484516925440e38", std::numeric_limits<float>::max()},
	    {"1.401298464324817070923729583289916131280e-45", std::numeric_limits<float>::denorm_min()}};
	for (const auto& Test : FloatCases)
	{
		Ok &= Expect(Parse(Test.Text, "float", Value) && std::get<float>(Value) == Test.Expected, "direct binary32 rounding");
		float Fallback = 17.f;
		const auto Result = FloatParsingDetail::FromCharsDecimalFallback(Test.Text, Fallback);
		Ok &= Expect(Result.ec == std::errc{} && Fallback == Test.Expected, "fallback direct binary32 rounding");
		double Literal = 0.;
		std::string LiteralError;
		Ok &= Expect(ParserDetail::ParseFloatLiteral(std::string(Test.Text) + 'f', Literal, LiteralError) && Literal == static_cast<double>(Test.Expected), "float literal rounds directly to binary32");
	}
	{
		double Fallback = 17.;
		Ok &= Expect(FloatParsingDetail::FromCharsDecimalFallback("4.9406564584124654e-324", Fallback).ec == std::errc{} && Fallback == std::numeric_limits<double>::denorm_min(), "fallback accepts binary64 subnormal");
		Fallback = 17.;
		Ok &= Expect(FloatParsingDetail::FromCharsDecimalFallback("1e-9999", Fallback).ec == std::errc::result_out_of_range && Fallback == 17., "fallback rejects zero underflow without assigning output");
		Ok &= Expect(FloatParsingDetail::FromCharsDecimalFallback("-0e9999", Fallback).ec == std::errc{} && Fallback == 0. && std::signbit(Fallback), "fallback preserves negative zero");
		std::string LiteralError;
		Ok &= Expect(ParserDetail::ParseFloatLiteral("1e-100f", Fallback, LiteralError) && Fallback == 0.,
		    "float literal retains representable-double underflow range");
		Ok &= Expect(ParserDetail::ParseFloatLiteral("-1e-100f", Fallback, LiteralError) && Fallback == 0. && std::signbit(Fallback), "underflowed float literal retains sign");
		Ok &= Expect(!ParserDetail::ParseFloatLiteral("1e-9999f", Fallback, LiteralError) && LiteralError == "floating-point literal out of range", "float literal retains double underflow rejection");
		Ok &= Expect(FloatParsingDetail::FromCharsDecimalFallback(LongMantissa, Fallback).ec == std::errc{} && Fallback == 1., "fallback combines long fractional prefix and cancelling exponent");
		const std::string LongInteger = "1" + std::string(10000, '0') + "e-10000";
		Ok &= Expect(FloatParsingDetail::FromCharsDecimalFallback(LongInteger, Fallback).ec == std::errc{} && Fallback == 1., "fallback combines long integer prefix and cancelling exponent");
		const std::string HugeExponent(10000, '9');
		Ok &= Expect(FloatParsingDetail::FromCharsDecimalFallback("-0e" + HugeExponent, Fallback).ec == std::errc{} && Fallback == 0. && std::signbit(Fallback), "fallback accepts signed zero with huge exponent");
		Fallback = 17.;
		Ok &= Expect(FloatParsingDetail::FromCharsDecimalFallback("1e" + HugeExponent, Fallback).ec == std::errc::result_out_of_range && Fallback == 17., "fallback bounds huge nonzero exponent");
		float StickyValue = 0.f;
		const std::string LongMidpoint = "1.000000059604644775390625" + std::string(5000, '0');
		Ok &= Expect(FloatParsingDetail::FromCharsDecimalFallback(LongMidpoint, StickyValue).ec == std::errc{} && StickyValue == 1.f, "fallback keeps exact long midpoint tie");
		Ok &= Expect(FloatParsingDetail::FromCharsDecimalFallback(LongMidpoint + '1', StickyValue).ec == std::errc{} && StickyValue == 0x1.000002p0f, "fallback retains rounding tail beyond bounded prefix");
		for (const auto Text : {"+1.5", " 1.5", "1.5 ", "0x1p0", "nan", "inf", "1,5", "1e", ".", "--1"})
		{
			Fallback = 17.;
			Ok &= Expect(FloatParsingDetail::FromCharsDecimalFallback(Text, Fallback).ec == std::errc::invalid_argument && Fallback == 17., "fallback requires full decimal grammar");
		}
		const std::string EmbeddedNull("1.5\0extra", 9);
		Ok &= Expect(FloatParsingDetail::FromCharsDecimalFallback(EmbeddedNull, Fallback).ec == std::errc::invalid_argument,
		    "fallback rejects embedded NUL suffix");
		const int SavedRounding = std::fegetround();
		for (const int Mode : {
		         FE_TONEAREST,
		#if defined(FE_DOWNWARD)
		         FE_DOWNWARD,
		#endif
		#if defined(FE_UPWARD)
		         FE_UPWARD,
		#endif
		#if defined(FE_TOWARDZERO)
		         FE_TOWARDZERO,
		#endif
		     })
		{
			if (SavedRounding == -1 || std::fesetround(Mode) != 0 || std::fegetround() != Mode)
			{
				continue;
			}
			for (const auto& Test : FloatCases)
			{
				float Rounded = 17.f;
				const auto Result = FloatParsingDetail::FromCharsDecimalFallback(Test.Text, Rounded);
				Ok &= Expect(Result.ec == std::errc{} && Rounded == Test.Expected && std::fegetround() == Mode,
				    "fallback uses nearest rounding and restores caller mode");
			}
		}
		if (SavedRounding != -1)
		{
			std::fesetround(SavedRounding);
		}
		errno = EDOM;
		Ok &= Expect(FloatParsingDetail::FromCharsDecimalFallback("1e-9999", Fallback).ec == std::errc::result_out_of_range && errno == EDOM, "fallback preserves caller errno");
		const char* CurrentLocale = std::setlocale(LC_NUMERIC, nullptr);
		const std::string SavedLocale = CurrentLocale ? CurrentLocale : "C";
		bool CommaLocale = false;
		for (const char* Candidate : {"de_DE.UTF-8", "fr_FR.UTF-8", "German_Germany.1252", "French_France.1252", "de-DE"})
		{
			if (std::setlocale(LC_NUMERIC, Candidate) && std::localeconv()->decimal_point[0] == ',')
			{
				CommaLocale = true;
				break;
			}
		}
		if (CommaLocale)
		{
			Ok &= Expect(FloatParsingDetail::FromCharsDecimalFallback("1.5", Fallback).ec == std::errc{} && Fallback == 1.5,
			    "fallback ignores process decimal separator");
			Ok &= Expect(Parse("1.5", "double", Value) && std::get<double>(Value) == 1.5,
			    "scalar parsing ignores process decimal separator");
			double Literal = 0.;
			std::string LiteralError;
			Ok &= Expect(ParserDetail::ParseFloatLiteral("1_2.5f", Literal, LiteralError) && Literal == 12.5,
			    "literal parsing ignores process decimal separator");
			Ok &= Expect(std::localeconv()->decimal_point[0] == ',', "fallback restores thread numeric locale");
		}
		else
		{
			std::cout << "SKIP: comma numeric locale is not installed\n";
		}
		Ok &= Expect(std::setlocale(LC_NUMERIC, SavedLocale.c_str()) != nullptr, "restore test numeric locale");
	}
	std::int32_t Integer = 12;
	double Floating = 12.;
	std::string Error;
	Ok &= Expect(!Runtime::TryParseInt32("+-1", Integer, Error) && Integer == 12, "legacy int parser rejects double sign");
	Ok &= Expect(!Runtime::TryParseDouble("+-1.5", Floating, Error) && Floating == 12., "legacy float parser rejects double sign");
	std::string Formatted;
	Ok &= Expect(Runtime::TryFormatString("{:x} {:X} {:x}", {"-1", "18446744073709551615", "4294967296"}, Formatted, Error) && Formatted == "ffffffffffffffff FFFFFFFFFFFFFFFF 100000000", "64-bit hexadecimal formatting");
	std::string Result = "previous";
	Ok &= Expect(Repeat("abc", 1000, Result) && Result == std::string("") + []
	{
		std::string S;
		for (int I = 0; I < 1000; ++I)
		{
			S += "abc";
		}
		return S;
	}(),
	    "repeat chunks preserve the pattern");
	Ok &= Expect(Replace("aaaaa", "aa", "x", Result) && Result == "xxa", "replace non-overlapping matches");
	Ok &= Expect(Replace("abc", "", "x", Result) && Result == "abc", "empty replacement pattern");
	std::int64_t Found = 0;
	Ok &= Expect(LastFind("aaa", "aa", Found) && Found == 1, "overlapping last_find");
	Ok &= Expect(LastFind("abc", "", Found) && Found == 3, "empty last_find");
	std::vector<std::string> Parts;
	Ok &= Expect(Split("a,,b,", ",", Parts) && Parts == std::vector<std::string>{"a", "", "b", ""}, "split empty parts");
	Ok &= Expect(Join({"a", "", "b", ""}, ",", Result) && Result == "a,,b,", "join empty parts");
	Ok &= Expect(Trim("\v\f a \r\n", true, true, Result) && Result == "a", "ASCII whitespace trim");
	FServiceContext Limited{{5, 0, 2}, {}};
	Result = "previous";
	Ok &= Expect(Repeat("ab", 3, Result, Limited).Status == EServiceStatus::LimitExceeded && Result == "previous", "repeat limit is transactional");
	Ok &= Expect(Split("a,b,c", ",", Parts, Limited).Status == EServiceStatus::LimitExceeded && Parts.size() == 4, "split limit preserves output");
	std::uint64_t Work = 0;
	FServiceContext Interrupted{{}, {[](void* Data, std::uint64_t N)
	{
		auto& Used = *static_cast<std::uint64_t*>(Data);
		Used += N;
		return Used <= 1200;
	}, &Work}};
	Ok &= Expect(Replace(std::string(10000, 'a'), "aaaab", "b", Result, Interrupted).Status == EServiceStatus::Interrupted && Result == "previous", "adversarial search cancellation preserves output");
	const std::string Utf8 = std::string("\xef\xbb\xbf") + "\xe4\xb8\xad" + std::string("\0\r\n", 3);
	Ok &= Expect(static_cast<bool>(ValidateUtf8(Utf8)), "UTF-8 accepts BOM NUL and newlines");
	for (const std::string Bytes : {std::string("\xc0\x80"), std::string("\xed\xa0\x80"),
	         std::string("\xf4\x90\x80\x80"), std::string("\xe4\xb8"), std::string("\x80")})
	{
		Ok &= Expect(ValidateUtf8(Bytes).Status == EServiceStatus::InvalidText, "reject malformed UTF-8");
	}
	struct FPathCase
	{
		const char* Input;
		EPathStyle Style;
		bool Absolute;
		const char* Root;
		const char* Normal;
	};
	for (const FPathCase C : {
	         FPathCase{"", EPathStyle::Posix, false, "", "."},
	         {"a/./b/../", EPathStyle::Posix, false, "", "a/"},
	         {"../../a/..", EPathStyle::Posix, false, "", "../.."},
	         {"/../../a", EPathStyle::Posix, true, "/", "/a"},
	         {"C:a/..", EPathStyle::Windows, false, "C:", "C:"},
	         {"C:../a", EPathStyle::Windows, false, "C:", "C:../a"},
	         {"/a/..", EPathStyle::Windows, false, "/", "/"},
	         {"C:/a/..", EPathStyle::Windows, true, "C:/", "C:/"},
	         {"//server/share/a/..", EPathStyle::Windows, true, "//server/share/", "//server/share/"}})
	{
		FLexicalPath P;
		Ok &= Expect(LexicalPath(C.Input, C.Style, P) && P.Absolute == C.Absolute && P.Root == C.Root && P.Normal == C.Normal, "portable lexical path vectors");
	}
	for (const auto Path : {"//server", "//?/C:/file", "//./device", "/??/C:/file"})
	{
		FLexicalPath P;
		Ok &= Expect(LexicalPath(Path, EPathStyle::Windows, P).Status == EServiceStatus::InvalidArgument, "reject ambiguous device/UNC paths");
	}
	std::istringstream Lines("\nlast\r");
	std::optional<std::string> Line;
	Ok &= Expect(ReadInput(Lines, false, Line) && Line && Line->empty(), "blank line is not EOF");
	Ok &= Expect(ReadInput(Lines, false, Line) && Line && *Line == "last", "unterminated last line strips CR");
	Ok &= Expect(ReadInput(Lines, false, Line) && !Line, "normal EOF is absent");
	for (const auto Text : {"12345\n", "12345\r\n", "12345\r"})
	{
		std::istringstream ExactLine(Text);
		Ok &= Expect(ReadInput(ExactLine, false, Line, Limited) && Line && *Line == "12345",
		    "line limit measures content consistently for LF, CRLF and trailing CR");
	}
	std::istringstream InteriorCR("1234\ra\n");
	std::istringstream DoubleCR("1234\r\r\n");
	Ok &= Expect(ReadInput(DoubleCR, false, Line, Limited) && Line && *Line == "1234\r",
	    "only one final CR is removed and earlier CR remains content");
	Line = "previous";
	Ok &= Expect(ReadInput(InteriorCR, false, Line, Limited).Status == EServiceStatus::LimitExceeded && Line && *Line == "previous", "interior CR counts toward the line limit and preserves output on failure");
	std::istringstream Tokens("  a\nrest");
	Ok &= Expect(ReadInput(Tokens, true, Line) && Line && *Line == "a", "read token");
	Ok &= Expect(ReadInput(Tokens, false, Line) && Line && Line->empty(), "token leaves delimiter");
	std::istringstream Oversized("123456");
	Result = "previous";
	Ok &= Expect(ReadText(Oversized, Result, Limited).Status == EServiceStatus::LimitExceeded && Result == "previous", "read text limit preserves output");
	struct FShortOutput : std::streambuf
	{
		std::streamsize xsputn(const char*, std::streamsize N) override
		{
			return N ? N - 1 : 0;
		}
	} ShortBuffer;
	std::ostream ShortStream(&ShortBuffer);
	FMemoryLease Lease;
	Lease.Data = reinterpret_cast<const std::uint8_t*>(Utf8.data());
	Lease.Size = Utf8.size();
	Ok &= Expect(WriteBytes(ShortStream, Lease).Status == EServiceStatus::IoError, "short writes fail");
	auto Mutex = FChannel::MakeMutex();
	Concurrent::FExecutorState Owner, Foreign;
	Owner.PrepareMutex(Mutex);
	bool Acquired = false;
	Ok &= Expect(Mutex->AcquireSynchronization(true, Owner.Identity(), -1, Acquired, Error, {}) && Acquired, "mutex acquire");
	Ok &= Expect(Mutex->AcquireSynchronization(true, Owner.Identity(), 0, Acquired, Error, {}) && !Acquired, "mutex self try fails without recursion");
	Ok &= Expect(!Mutex->ReleaseSynchronization(true, Foreign.Identity(), Error), "foreign mutex unlock rejected");
	Ok &= Expect(!Mutex->TrySend(true) && !Mutex->ConfigureCapacity(3, Error), "special role rejects ordinary mutations");
	Owner.AbandonMutexes();
	Ok &= Expect(Mutex->IsClosed() && !Mutex->AcquireSynchronization(true, Foreign.Identity(), 0, Acquired, Error, {}), "abandoned mutex closes");
	auto Semaphore = FChannel::MakeSemaphore(INT32_MAX, INT32_MAX, Error);
	Ok &= Expect(Semaphore && Semaphore->Size() == INT32_MAX, "semaphore large permits use constant storage");
	Ok &= Expect(!Semaphore->ReleaseSynchronization(false, {}, Error), "semaphore overflow preserves count");
	Ok &= Expect(Semaphore->AcquireSynchronization(false, {}, 0, Acquired, Error, {}) && Acquired && Semaphore->ReleaseSynchronization(false, {}, Error) && Semaphore->Size() == INT32_MAX, "semaphore acquire/release");
	auto Cell = FChannel::MakeSynchronized("channel<int>", "int", std::int32_t{7}, Error);
	auto Snapshot = Cell->SnapshotSynchronized(Error);
	bool Published = false;
	Ok &= Expect(Cell->PublishSynchronized(Snapshot, std::make_shared<const FValue>(std::int32_t{9}), true, Published, Error) && Published, "synchronized publish");
	Ok &= Expect(Cell->PublishSynchronized(Snapshot, std::make_shared<const FValue>(std::int32_t{11}), true, Published, Error) && !Published && std::get<std::int32_t>(*Cell->SnapshotSynchronized(Error)) == 9, "stale compare does not publish");
	Ok &= Expect(!CheckedNumericAdd(std::int32_t{INT32_MAX}, std::int32_t{1}, Value), "checked synchronized arithmetic");
	Work = 0;
	const FServiceContext Counted{{}, {[](void* Data, std::uint64_t Units)
	{
		*static_cast<std::uint64_t*>(Data) += Units;
		return true;
	}, &Work}};
	const std::string Adversarial(200000, 'a');
	Ok &= Expect(LastFind(Adversarial, std::string(999, 'a') + 'b', Found, Counted) && Found == -1 && Work < 3 * (Adversarial.size() + 1000), "search work remains linear on repeated prefixes");
	Ok &= Expect(FirstFind("ababa", "aba", Found) && Found == 0, "first find stops at first overlapping match");
	const std::string BinarySearchText("a\0a\0", 4);
	const std::string NullNeedle(1, '\0');
	Work = 0;
	Ok &= Expect(LastFind(BinarySearchText, NullNeedle, Found, Counted) && Found == 3 && Work == 5,
	    "single-byte search preserves binary text and prefix/scan work charges");
	for (const std::size_t Position : {std::size_t{0}, std::size_t{1022}, std::size_t{1023}, std::size_t{1024}})
	{
		std::string Text(2050, 'x');
		Text[Position] = '\0';
		Work = 0;
		Ok &= Expect(FirstFind(Text, NullNeedle, Found, Counted) &&
		        Found == static_cast<std::int64_t>(Position) && Work == Position + 2,
		    "single-byte first search charges through the match on both sides of a poll");
	}
	Found = 73;
	Work = 0;
	Ok &= Expect(FirstFind(std::string(2050, 'x'), NullNeedle, Found, Interrupted).Status == EServiceStatus::Interrupted && Found == 73,
	    "single-byte first search preserves output when work is interrupted");
	std::string PollText(1024, 'x');
	const FServiceContext ChangeAtPoll{{}, {[](void* Data, std::uint64_t Units)
	{
		if (Units == 1024)
		{
			static_cast<std::string*>(Data)->back() = '\0';
		}
		return true;
	}, &PollText}};
	Ok &= Expect(FirstFind(PollText, NullNeedle, Found, ChangeAtPoll) && Found == 1023,
	    "single-byte search observes the boundary byte after the work callback");
	Ok &= Expect(Split(BinarySearchText, NullNeedle, Parts) && Parts == std::vector<std::string>({"a", "a", ""}),
	    "single-byte split retains empty trailing fields and embedded NUL separators");
	Result = "previous";
	Work = 0;
	Ok &= Expect(Replace(std::string(10000, 'a'), "a", "b", Result, Interrupted).Status == EServiceStatus::Interrupted && Result == "previous", "single-byte search retains cancellation and atomic output");
	FServiceResult Failure;
	Ok &= Expect(Runtime::TryPrintfFormatString("%d %u %c", {"-9223372036854775808", "18446744073709551615", "x"}, Formatted, Error) && Formatted == "-9223372036854775808 18446744073709551615 x", "printf 64-bit integers and literal character");
	Ok &= Expect(!Runtime::TryFormatString("{}{}", {"abc", "abc"}, Formatted, Error, &Limited, &Failure) && Failure.Status == EServiceStatus::LimitExceeded, "format expansion limit");
	FMemoryContext Memory;
	FMemoryResult MemoryError;
	FValue Buffer = std::int32_t{73};
	std::istringstream Nonempty("a");
	Ok &= Expect(ReadBytes(Nonempty, Memory, 0, Buffer, MemoryError).Status == EServiceStatus::LimitExceeded && std::get<std::int32_t>(Buffer) == 73, "zero read maximum rejects nonempty input and preserves output");
	std::istringstream Binary(std::string("a\0b", 3));
	Ok &= Expect(static_cast<bool>(ReadBytes(Binary, Memory, 3, Buffer, MemoryError)), "binary read exact maximum");
	FMemoryLease BinaryLease;
	Ok &= Expect(Memory::AcquireLease(Memory, Buffer, false, BinaryLease) && BinaryLease.Size == 3 && BinaryLease.Data[1] == 0, "binary read preserves NUL");
	if (BinaryLease.Token.Serial)
	{
		(void)Memory::ReleaseLease(BinaryLease.Token);
	}
	return Ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
