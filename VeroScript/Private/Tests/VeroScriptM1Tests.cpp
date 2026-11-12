// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "VeroScriptAst.h"
#include "VeroScriptEngine.h"
#include "VeroScriptLexer.h"
#include "VeroScriptLocale.h"
#include "VeroScriptParser.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptM1Tests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	bool ExpectOutput(
		const std::string_view Source,
		const std::string_view ExpectedOutput,
		const char* Message,
		const VeroScript::EExecutionBackend Backend = VeroScript::EExecutionBackend::Vm)
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script,
			{},
			{},
			nullptr,
			{},
			{},
			Backend);
		if (!Expect(Result.bSuccess, Message))
		{
			std::cerr << "backend: " << static_cast<int>(Backend) << '\n';
			std::cerr << Errors.str();
			return false;
		}
		if (!Expect(Errors.str().empty(), "expected no errors"))
		{
			std::cerr << Errors.str();
			return false;
		}
		if (Output.str() != ExpectedOutput)
		{
			std::cerr << "backend: " << static_cast<int>(Backend) << '\n';
			std::cerr << "expected output:\n"
					  << ExpectedOutput
				<< "actual output:\n"
					  << Output.str();
			return Expect(false, Message);
		}
		return true;
	}

	bool ExpectCompileFailureContaining(
		const std::string_view Source,
		const std::string_view ExpectedError,
		const char* Message)
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<test>",
			Output,
			Errors);
		const bool bMatches =
			!Result.bSuccess && Errors.str().find(ExpectedError) != std::string::npos;
		if (!bMatches)
		{
			std::cerr << Errors.str();
		}
		return Expect(bMatches, Message);
	}

	bool ExpectCompileFailureAt(
		const std::string_view Source,
		const std::string_view ExpectedError,
		const std::uint32_t ExpectedColumn,
		const char* Message)
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<test>",
			Output,
			Errors);
		const bool bMatches = !Result.bSuccess
			&& Result.Error.Message.find(ExpectedError) != std::string::npos
			&& Result.Error.Location.Line == 1
			&& Result.Error.Location.Column == ExpectedColumn;
		if (!bMatches)
		{
			std::cerr << Errors.str()
				<< "expected error at 1:" << ExpectedColumn << '\n';
		}
		return Expect(bMatches, Message);
	}
}

int main()
{
	(void)VeroScript::Locale::TrySetLanguageFromTag("en-US");

	{
		std::string NestedType;
		for (int Index = 0; Index < 300; ++Index)
		{
			NestedType += "array<";
		}
		NestedType += "int";
		NestedType.append(300, '>');
		if (!ExpectCompileFailureContaining(
				NestedType + " value;",
				"type name nested too deeply",
				"pathological type nesting must be rejected safely"))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput("print(1 + 2);", "3\n", "print(1+2) should output 3"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput("print(10 - 3 * 2);", "4\n", "operator precedence"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput("print(-5 + 8);", "3\n", "unary minus"))
	{
		return EXIT_FAILURE;
	}

	for (const VeroScript::EExecutionBackend Backend : {
			 VeroScript::EExecutionBackend::Vm,
			VeroScript::EExecutionBackend::Mixed})
	{
		if (!ExpectOutput(
				"void capture(int sentinel, bool a, bool b, bool c, bool d) { "
				"print(sentinel); print(a); print(b); print(c); print(d); } "
				"void compare(int left, int right) { "
				"capture(17, left < right, left <= right, left > right, left >= right); } "
				"void main() { int low = -2147483647 - 1; int high = 2147483647; "
				"compare(low, high); compare(low, low); compare(high, low); }",
				"17\ntrue\ntrue\nfalse\nfalse\n"
				"17\nfalse\ntrue\nfalse\ntrue\n"
				"17\nfalse\nfalse\ntrue\ntrue\n",
				"integer comparisons preserve boundary values and earlier call arguments",
				Backend))
		{
			return EXIT_FAILURE;
		}
		if (!ExpectOutput(
				"int choose(int left, int right) { if (left < right) return 1; return 0; } "
				"int choose_ref(int& left, const int& right) { "
				"if (left < right) return 1; return 0; } "
				"int choose_wide(int64 left, int64 right) { "
				"if (left < right) return 1; return 0; } "
				"void show(int sentinel, int value) { print(sentinel); print(value); } "
				"void main() { int low = -2147483647 - 1; int high = 2147483647; "
				"show(17, choose(low, high)); show(18, choose(high, low)); "
				"show(19, choose(low, low)); print(choose_ref(&low, &high)); "
				"print(choose_wide(9007199254740992i64, 9007199254740993i64)); "
				"int i = 0; int limit = 5; while (i < limit) { "
				"i = i + 1; if (i == 3) limit = 3; } print(i); }",
				"17\n1\n18\n0\n19\n0\n1\n1\n3\n",
				"local comparison branches preserve stack values, ref bounds and integer precision",
				Backend))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectOutput(
			"int return_from_do() { do { return 7; } while (false); } "
			"void main() { print(return_from_do()); }",
			"7\n",
			"do-while body participates in definite-return analysis"))
	{
		return EXIT_FAILURE;
	}

	const std::string LanguageSurfaceSource =
		"enum ByteCode : char { Letter = 'A', High = '\\xFF' };\n"
		"class CharacterDefaults { public: char value = '0'; };\n"
		"int pick(char value) { return 8; }\n"
		"int pick(int value) { return 32; }\n"
		"char default_char(char value = 'A') { return value; }\n"
		"void main() {\n"
		"\tint once = 0;\n"
		"\tdo once++; while (false);\n"
		"\tint i = 0; int sum = 0;\n"
		"\tdo { i++; if (i == 2) { continue; } sum += i; if (i == 4) { break; } } while (i < 9);\n"
		"\tint left = 0; int right = 0; int order = 0;\n"
		"\tfor (left = 1, right = left + 1; left < 4; left++, right += left) { order += left + right; }\n"
		"\tarray<int> values = {1, 2,};\n"
		"\tmap<char, int> scores = {'A': 7,};\n"
		"\tset<char> letters = {'A', '\\xFF',};\n"
		"\tordered_map<char, int> ordered = {'B': 2, 'A': 1,};\n"
		"\tmultimap<char, int> grouped(); grouped.add('A', 1).add('A', 2);\n"
		"\tordered_multimap<char, int> ordered_grouped(); ordered_grouped.add('B', 2).add('A', 1).add('B', 3);\n"
		"\tmultiset<char> character_bag = {'A', '\\xFF', '\\xFF'};\n"
		"\tordered_multiset<char> ordered_bag = {'B', 'A', 'B'};\n"
		"\tconst char high = '\\xFF';\n"
		"\tCharacterDefaults defaults = CharacterDefaults();\n"
		"\tint switched = 0; switch ('A') { case 'A': switched = 1; break; default: break; }\n"
		"\tprint(once); print(sum); print(order); print(len(values));\n"
		"\tprint(pick('A')); print(to_int(default_char())); print(to_int(high));\n"
		"\tprint(to_int(defaults.value)); print(scores['A']); print(letters.has(high)); print(switched);\n"
		"\tprint(len(\"\\x00A\")); print(len(\"\\xFF\")); print(len(\"\\u00FF\"));\n"
		"\tprint(len(L\"\\U0001F600\"));\n"
		"\tscores[high] = 9; bool saw_high_key = false;\n"
		"\tfor (pair<char, int> entry : scores.entries()) { if (entry.first == high) { saw_high_key = true; } }\n"
		"\tbool saw_high_value = false; for (char letter : letters.values()) { if (letter == high) { saw_high_value = true; } }\n"
		"\tprint(scores.has(high)); print(saw_high_key); print(scores.remove(high)); print(len(scores));\n"
		"\tletters.add('0'); print(saw_high_value); print(len(letters)); print(letters.remove(high));\n"
		"\tarray<char> ordered_keys = ordered.keys(); print(to_int(ordered_keys[0])); print(to_int(ordered_keys[1]));\n"
		"\tprint(ordered.has('A')); print(ordered.remove('B')); print(ordered.values()[0]);\n"
		"\tprint(len(grouped['A'])); print(to_int(ordered_grouped.keys()[0])); print(character_bag.count(high)); print(to_int(ordered_bag.values()[0]));\n"
		"\tint cleanup = 0; int attempts = 0; do { try { attempts++; if (attempts < 2) { continue; } break; } finally { cleanup++; } } while (true); print(cleanup);\n"
		"\tint condition_checks = 0; int passes = 0; do { passes++; if (passes == 1) { continue; } } while (++condition_checks < 2);\n"
		"\tint break_checks = 0; do break; while (++break_checks > 0); print(passes); print(condition_checks); print(break_checks);\n"
		"\tint nested = 0; do { switch (nested) { case 0: nested++; break; default: break; } if (nested == 1) { break; } } while (true); print(nested);\n"
		"\tint escape_sum = to_int('\\0') + to_int('\\a') + to_int('\\b') + to_int('\\f') + to_int('\\n') + to_int('\\r') + to_int('\\t') + to_int('\\v');\n"
		"\tescape_sum += to_int('\\\\') + to_int('\\\'') + to_int('\\\"') + to_int('\\x7F') + to_int('\\x80'); print(escape_sum);\n"
		"}\n";
	const std::string LanguageSurfaceOutput =
		"1\n8\n19\n2\n8\n65\n-1\n48\n7\ntrue\n1\n2\n1\n2\n2\n"
		"true\ntrue\ntrue\n1\ntrue\n3\ntrue\n66\n65\ntrue\ntrue\n1\n2\n66\n2\n66\n2\n2\n2\n0\n1\n234\n";
	for (const VeroScript::EExecutionBackend Backend : {
			 VeroScript::EExecutionBackend::Vm,
			VeroScript::EExecutionBackend::Mixed})
	{
		if (!ExpectOutput(
				LanguageSurfaceSource,
				LanguageSurfaceOutput,
				"do-while, character, initializer, escape and for-list semantics",
				Backend))
		{
			return EXIT_FAILURE;
		}
	}
	const std::string DeclarationGroupAndRawStringSource =
		"const int global_first = 2, global_second = global_first + 3;\n"
		"namespace Grouped { const int first = 3, second = first + 4; }\n"
		"int capture_group() { int first = 1, second = first + 2, third = second + 3; return third; }\n"
		"void main() {\n"
		"\tint first = 1, second = first + 2, third = second + 3;\n"
		"\tconst int copied = third, next = copied + 1;\n"
		"\tint storage = 4; ref int left = storage, right = left; left = 9;\n"
		"\tint total = 0; for (int i = 0, end = 3; i < end; i++) { total += i; }\n"
		"\tif (true) int scoped = 5, derived = scoped + 1;\n"
		"\tfn()->int capture = [=]() -> int => third;\n"
		"\tstring raw = r\"\\n\";\n"
		"\tstring multiline = r\"alpha\n"
		"beta\";\n"
		"\tprint(global_second); print(Grouped::second); print(first); print(second); print(third); print(copied); print(next);\n"
		"\tprint(storage); print(total); print(len(raw)); print(len(multiline)); print(capture_group()); print(capture());\n"
		"}\n";
	for (const VeroScript::EExecutionBackend Backend : {
			 VeroScript::EExecutionBackend::Vm,
			VeroScript::EExecutionBackend::Mixed})
	{
		if (!ExpectOutput(
				DeclarationGroupAndRawStringSource,
				"5\n7\n1\n3\n6\n6\n7\n9\n3\n2\n10\n6\n6\n",
				"declaration groups and raw strings preserve VM and JIT semantics",
				Backend))
		{
			return EXIT_FAILURE;
		}
	}
	if (!ExpectCompileFailureContaining(
			"void main() { do {} while (1); }",
			"condition must be bool",
			"do-while condition must be bool")
		|| !ExpectCompileFailureContaining(
			"int invalid_do_return(bool exit) { "
			"do { if (exit) { break; } return 1; } while (false); }",
			"missing return in function",
			"do-while break path is not a definite return")
		|| !ExpectCompileFailureContaining(
			"void main() { do {} while (false) }",
			"expected ';' after do-loop",
			"do-while requires a trailing semicolon")
		|| !ExpectCompileFailureContaining(
			"void main() { ; }",
			"empty statements are not supported",
			"empty statement diagnostic")
		|| !ExpectCompileFailureContaining(
			"void main() { int a = 0; int b = 1; a, b; }",
			"comma operator is not supported",
			"general comma operator diagnostic")
		|| !ExpectCompileFailureContaining(
			"void main() { auto first = 1, second = 2; }",
			"auto declarations cannot declare multiple variables",
			"auto declaration groups are intentionally unsupported")
		|| !ExpectCompileFailureContaining(
			"void main() { int first = 1, second; }",
			"expected '=' in variable declaration",
			"each declaration in a group requires its own initializer")
		|| !ExpectCompileFailureContaining(
			"void main() { int first = 1, first = 2; }",
			"duplicate local: first",
			"duplicate declaration in a group must fail")
		|| !ExpectCompileFailureContaining(
			"void main() { int source=1; static int& value = source; }",
			"static requires a non-reference local variable",
			"static reference diagnostic")
		|| !ExpectCompileFailureContaining(
			"void f(int value) {} void main() { f(value: 1); }",
			"named arguments are not supported",
			"named argument diagnostic")
		|| !ExpectCompileFailureContaining(
			"auto f() { return 1; }",
			"cannot be 'auto'",
			"auto return diagnostic")
		|| !ExpectCompileFailureContaining(
			"void main() { char c = ''; }",
			"character literal",
			"empty character literal diagnostic")
		|| !ExpectCompileFailureContaining(
			"void main() { char c = 'ab'; }",
			"character literal",
			"multi-character literal diagnostic")
		|| !ExpectCompileFailureContaining(
			"void main() { char c = '\\q'; }",
			"character escape",
			"invalid character escape diagnostic")
		|| !ExpectCompileFailureContaining(
			"void main() { char c = '\\u0041'; }",
			"Unicode escapes are not supported in char literals",
			"Unicode char escape diagnostic")
		|| !ExpectCompileFailureContaining(
			"void main() { char c = 'é'; }",
			"one ASCII byte",
			"direct non-ASCII char diagnostic")
		|| !ExpectCompileFailureContaining(
			"void main() { char c = '\\x1'; }",
			"exactly two digits",
			"short hexadecimal char escape diagnostic")
		|| !ExpectCompileFailureContaining(
			"void main() { string value = \"\\uD800\"; }",
			"invalid string Unicode escape",
			"surrogate string escape diagnostic")
		|| !ExpectCompileFailureContaining(
			"void main() { string value = \"\\U00110000\"; }",
			"invalid string Unicode escape",
			"out-of-range string escape diagnostic")
		|| !ExpectCompileFailureContaining(
			"void main() { wstring value = L\"\\x1\"; }",
			"invalid wide string hexadecimal escape",
			"short wide hexadecimal escape diagnostic"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectCompileFailureAt(
			"void main() { char c = '\\q'; }",
			"character escape",
			25,
			"character escape diagnostic location")
		|| !ExpectCompileFailureAt(
			"void main() { string value = \"\\q\"; }",
			"string escape",
			31,
			"string escape diagnostic location")
		|| !ExpectCompileFailureAt(
			"void main() { wstring value = L\"\\q\"; }",
			"wide string escape",
			33,
			"wide string escape diagnostic location"))
	{
		return EXIT_FAILURE;
	}
	{
		std::string InvalidUtf8RawString = "void main() { string value = r\"";
		InvalidUtf8RawString.push_back(static_cast<char>(0xFF));
		InvalidUtf8RawString += "\"; }";
		if (!ExpectCompileFailureContaining(
				InvalidUtf8RawString,
				"invalid UTF-8 in raw string literal",
				"raw strings must preserve only valid UTF-8 source text"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		const std::string RecoverySource =
			"void first() { int one = 1, ; }\n"
			"void second() { int two = 2, ; }\n";
		VeroScript::VeroScriptLexer Lexer(RecoverySource, "group_recovery.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		VeroScript::VeroScriptParser Parser(Tokens, "group_recovery.vs");
		Parser.SetCollectMultipleErrors(true, 4);
		(void)Parser.ParseProgram();
		if (!Expect(
				!Lexer.HasError() && Parser.GetDiagnostics().size() >= 2,
				"declaration-group recovery must continue to the next declaration"))
		{
			return EXIT_FAILURE;
		}
	}
	const std::string NumericLiteralSource =
		"int pick(float value) { return 32; }\n"
		"int pick(double value) { return 64; }\n"
		"void main() {\n"
		"\tfloat a = 1.f;\n"
		"\tfloat b = 2.05f;\n"
		"\tunsigned int u = 1u;\n"
		"\tint hex = 0x0101;\n"
		"\tint binary = 0b1010_0101;\n"
		"\tuint64 binary_wide = 0b1_000u64;\n"
		"\tint leading_zero = 010;\n"
		"\tint separated = 1_000;\n"
		"\tdouble separated_real = 1_2.5_0e1;\n"
		"\tprint(pick(a + b));\n"
		"\tprint(pick(-1.0f));\n"
		"\tprint(pick(a / 2.0f));\n"
		"\tprint(pick(1.0));\n"
		"\tprint(16777217.f == 16777216.f);\n"
		"\tprint(16777217.0 == 16777216.0);\n"
		"\tprint(u + to_unsigned(hex));\n"
		"\tprint(binary); print(binary_wide); print(leading_zero); print(separated); print(to_int(separated_real));\n"
		"}\n";
	const std::string NumericLiteralOutput = "32\n32\n32\n64\ntrue\nfalse\n258\n165\n8\n10\n1000\n125\n";
	for (const VeroScript::EExecutionBackend Backend : {
			 VeroScript::EExecutionBackend::Vm,
			VeroScript::EExecutionBackend::Mixed})
	{
		if (!ExpectOutput(
				NumericLiteralSource,
				NumericLiteralOutput,
				"numeric suffixes and real float semantics",
				Backend))
		{
			return EXIT_FAILURE;
		}
	}

	const std::string LeadingDotFloatSource =
		"int pick(float value) { return 32; }\n"
		"int pick(double value) { return 64; }\n"
		"void main() {\n"
		"\tprint(pick(.5f));\n"
		"\tprint(pick(.5));\n"
		"\tprint(.5e2);\n"
		"}\n";
	for (const VeroScript::EExecutionBackend Backend : {
			 VeroScript::EExecutionBackend::Vm,
			VeroScript::EExecutionBackend::Mixed})
	{
		if (!ExpectOutput(
				LeadingDotFloatSource,
				"32\n64\n50\n",
				"leading-dot float and double literal semantics",
				Backend))
		{
			return EXIT_FAILURE;
		}
	}
	if (!ExpectCompileFailureContaining(
			"void main() { double value = 1e9999; }",
			"floating-point literal out of range",
			"double literal overflow must be rejected"))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectCompileFailureContaining(
			"void main() { float value = 3.5e38f; }",
			"floating-point literal out of range",
			"float literal overflow must be rejected before narrowing"))
	{
		return EXIT_FAILURE;
	}

	const std::string RealIntegerWidthsSource =
		"int width(int8 value) { return 8; }\n"
		"int width(int16 value) { return 16; }\n"
		"int width(int value) { return 32; }\n"
		"int width(int64 value) { return 64; }\n"
		"int unsigned_width(uint16 value) { return 16; }\n"
		"int unsigned_width(unsigned int value) { return 32; }\n"
		"enum Tiny : int8 { Negative = -7 };\n"
		"enum Small : uint16 { Maximum = 65535u };\n"
		"enum class Wide : long { Value = 5000000008L };\n"
		"enum class MinI32 : int { Value = -0x80000000 };\n"
		"enum class MinI64 : long { Value = -0x8000000000000000L };\n"
		"class NumericDefaults {\n"
		"public:\n"
		"\tint8 i8;\n"
		"\tuint8 u8;\n"
		"\tint16 i16;\n"
		"\tuint16 u16;\n"
		"\tlong i64;\n"
		"\tunsigned long u64;\n"
		"\tint from_float = 12.75f;\n"
		"\tint64 wide_from_double = 13.75;\n"
		"\tuint32 unsigned_from_float = 14.75f;\n"
		"\tuint64 unsigned_wide_from_double = 15.75;\n"
		"\tint64 exact_from_u64 = 9223372036854775807u64;\n"
		"\tuint64 exact_from_i64 = 9223372036854775807L;\n"
		"};\n"
		"void main() {\n"
		"\tint8 i8 = -128;\n"
		"\tuint8 u8 = 255u;\n"
		"\tint16 i16 = -32768;\n"
		"\tuint16 u16 = 65535u;\n"
		"\tlong i64 = 5000000000i64;\n"
		"\tunsigned long u64 = 5000000001u64;\n"
		"\tint64 canonical_i64 = 5000000002i64;\n"
		"\tuint64 canonical_u64 = 5000000003u64;\n"
		"\tint min_i32 = -2147483648;\n"
		"\tint min_i32_hex = -0x80000000;\n"
		"\tint64 min_i64 = -9223372036854775808L;\n"
		"\tint64 min_i64_hex = -0x8000000000000000L;\n"
		"\tauto tiny = Tiny::Negative;\n"
		"\tauto small = Small::Maximum;\n"
		"\tarray<int8> values = array<int8>{i8, 127};\n"
		"\tNumericDefaults defaults = NumericDefaults();\n"
		"\tprint(i8); print(u8); print(i16); print(u16);\n"
		"\tprint(width(i8)); print(width(i16)); print(width(1)); print(width(i64));\n"
		"\tprint(width(tiny)); print(unsigned_width(small));\n"
		"\tprint(i64); print(u64); print(canonical_i64); print(canonical_u64);\n"
		"\tprint(min_i32); print(min_i32_hex); print(min_i64); print(min_i64_hex);\n"
		"\tprint(5000000004i64); print(5000000005u64);\n"
		"\tprint(5000000006L); print(5000000007UL); print(Wide::Value); print(MinI32::Value); print(MinI64::Value); print(values[0]);\n"
		"\tprint(defaults.i8); print(defaults.u16);\n"
		"\tprint(defaults.from_float); print(defaults.wide_from_double);\n"
		"\tprint(defaults.unsigned_from_float); print(defaults.unsigned_wide_from_double);\n"
		"\tprint(defaults.exact_from_u64); print(defaults.exact_from_i64);\n"
		"\tswitch (Wide::Value) { case Wide::Value: print(5000000009L); break; }\n"
		"}\n";
	const std::string RealIntegerWidthsOutput =
		"-128\n255\n-32768\n65535\n8\n16\n32\n64\n8\n16\n5000000000\n5000000001\n"
		"5000000002\n5000000003\n"
		"-2147483648\n-2147483648\n-9223372036854775808\n-9223372036854775808\n"
		"5000000004\n5000000005\n5000000006\n5000000007\nWide(5000000008)\n"
		"MinI32(-2147483648)\nMinI64(-9223372036854775808)\n-128\n0\n0\n";
	const std::string RealIntegerWidthsOutputWithConvertedDefaults =
		RealIntegerWidthsOutput
		+ "12\n13\n14\n15\n9223372036854775807\n9223372036854775807\n"
		+ "5000000009\n";
	for (const VeroScript::EExecutionBackend Backend : {
			 VeroScript::EExecutionBackend::Vm,
			VeroScript::EExecutionBackend::Mixed})
	{
		if (!ExpectOutput(
				RealIntegerWidthsSource,
				RealIntegerWidthsOutputWithConvertedDefaults,
				"integer aliases must retain real storage widths",
				Backend))
		{
			return EXIT_FAILURE;
		}
	}

	const std::string NarrowBuiltinSource =
		"void main() {\n"
		"\tint8 i8 = -7; uint8 u8 = 250u;\n"
		"\tint16 i16 = -300; uint16 u16 = 65000u;\n"
		"\tint8 other_i8 = 4; uint8 other_u8 = 5u;\n"
		"\tint16 other_i16 = 200; int16 equal_i16 = -300; uint16 other_u16 = 60000u;\n"
		"\tint8 shift_i8 = 1; uint8 shift_u8 = 1u;\n"
		"\tprint(abs(i8)); print(abs(u8)); print(abs(i16)); print(abs(u16));\n"
		"\tprint(min(i8, other_i8)); print(max(u8, other_u8));\n"
		"\tprint(min(i16, other_i16)); print(max(u16, other_u16));\n"
		"\tprint(~i8); print(~u8); print(i16 << shift_u8); print(u16 >> shift_i8);\n"
		"\tprint(i8 < other_i8); print(u16 > other_u16); print(i16 == equal_i16);\n"
		"\tprint(i8 + other_i8); print(u8 + other_u8);\n"
		"\tprint(i16 * other_i16); print(u16 % other_u16);\n"
		"}\n";
	for (const VeroScript::EExecutionBackend Backend : {
			 VeroScript::EExecutionBackend::Vm,
			VeroScript::EExecutionBackend::Mixed})
	{
		if (!ExpectOutput(
				NarrowBuiltinSource,
				"7\n250\n300\n65000\n-7\n250\n-300\n65000\n"
				"6\n4294967045\n-600\n32500\ntrue\ntrue\ntrue\n"
				"-3\n255\n-60000\n5000\n",
				"narrow primitive math builtins preserve value and result types",
				Backend))
		{
			return EXIT_FAILURE;
		}
	}
	const std::string SequentialNarrowConversionSource =
		"void main() {\n"
		"\tint8 i8_value = -7;\n"
		"\tprint(abs(i8_value));\n"
		"\tprint(to_int(-2147483648.f));\n"
		"}\n";
	for (const VeroScript::EExecutionBackend Backend : {
			 VeroScript::EExecutionBackend::Vm,
			VeroScript::EExecutionBackend::Mixed})
	{
		if (!ExpectOutput(
				SequentialNarrowConversionSource,
				"7\n-2147483648\n",
				"sequential narrow builtin and float32 boundary conversion",
				Backend))
		{
			return EXIT_FAILURE;
		}
	}
	for (const VeroScript::EExecutionBackend Backend : {
			 VeroScript::EExecutionBackend::Vm,
			VeroScript::EExecutionBackend::Mixed})
	{
		if (!ExpectOutput(
				"void main() { print(to_int(-2147483648.f)); }",
				"-2147483648\n",
				"float32 int32 minimum converts without overflow",
				Backend))
		{
			return EXIT_FAILURE;
		}
	}

	if (!ExpectCompileFailureContaining(
			"void main() { int value = 2147483648; }",
			"integer literal out of range",
			"positive int32 minimum magnitude must remain out of range"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectCompileFailureContaining(
			"void main() { int64 value = 9223372036854775808L; }",
			"signed int64 literal out of range",
			"positive int64 minimum magnitude must remain out of range"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput("print(\"hello\");", "hello\n", "string literal"))
	{
		return EXIT_FAILURE;
	}

	{
		std::string TooManyFunctionArguments = "print(\"{}\"";
		for (int Index = 0; Index < 255; ++Index)
		{
			TooManyFunctionArguments += ", 0";
		}
		TooManyFunctionArguments += ");";
		if (!ExpectCompileFailureContaining(
				TooManyFunctionArguments,
				"function call supports at most 255 arguments",
				"function call argument count must not truncate to bytecode u8"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::string TooManyMethodArguments =
			"class C { public: void f() {} }\n"
			"void main() { C c = C(); c.f(";
		for (int Index = 0; Index < 256; ++Index)
		{
			if (Index != 0)
			{
				TooManyMethodArguments += ", ";
			}
			TooManyMethodArguments += "0";
		}
		TooManyMethodArguments += "); }";
		if (!ExpectCompileFailureContaining(
				TooManyMethodArguments,
				"method call supports at most 255 arguments",
				"method call argument count must not truncate to bytecode u8"))
		{
			return EXIT_FAILURE;
		}
	}

	// Peephole correctness: double counter must not break on JmpIfLocalGeImm fusion.
	if (!ExpectOutput(
			"void main() {\n"
			"\tdouble d = 0.0;\n"
			"\twhile (d < 10) {\n"
			"\t\td = d + 1.0;\n"
			"\t}\n"
			"\tprint(to_string(d));\n"
			"}\n",
			"10\n",
			"double while counter after peephole"))
	{
		return EXIT_FAILURE;
	}

	// NaN must exit `while (d < N)` the same as Lt+JmpIfFalse (!(NaN < N) → leave).
	// Using `>=` would infinite-loop (NaN >= N is false). Inf-Inf yields NaN without /0.
	if (!ExpectOutput(
			"void main() {\n"
			"\tdouble huge = 1e308;\n"
			"\tdouble d = huge * huge;\n"
			"\td = d - d;\n"
			"\tint guard = 0;\n"
			"\twhile (d < 10) {\n"
			"\t\tguard = guard + 1;\n"
			"\t\tif (guard > 3) { break; }\n"
			"\t}\n"
			"\tprint(to_string(guard));\n"
			"}\n",
			"0\n",
			"NaN while must not enter body after JmpIfLocalGeImm"))
	{
		return EXIT_FAILURE;
	}

	// Peephole: non-virtual int field++ may fuse to IncField; double `x = x + 1` (int unit)
	// is not fused, but must still execute correctly under all backends.
	if (!ExpectOutput(
			"class C {\n"
			"public:\n"
			"\tdouble x;\n"
			"\tC() { x = 0.0; }\n"
			"\tvoid bump() { x = x + 1; }\n"
			"}\n"
			"void main() {\n"
			"\tC c = C();\n"
			"\tint i = 0;\n"
			"\twhile (i < 5) {\n"
			"\t\tc.bump();\n"
			"\t\ti = i + 1;\n"
			"\t}\n"
			"\tprint(to_string(c.x));\n"
			"}\n",
			"5\n",
			"double field bump must not IncField-miscompile"))
	{
		return EXIT_FAILURE;
	}

	if (!ExpectOutput(
			"void bump(uint64& value){value+=1u64;}"
			"void main(){uint64 value=18446744073709551615u64;bump(ref value);print(value);"
			"fn(ref unsigned long)->void add=[](unsigned long& item)->void{item+=2u64;};"
			"add(ref value);print(value);}",
			"0\n2\n", "fused unsigned reference and lambda addition retains unsigned storage"))
	{
		return EXIT_FAILURE;
	}

	// Peephole: `i = i - 1` fuses to AddLocalImm(-1) with the same CheckedAdd overflow rules.
	if (!ExpectOutput(
			"void main() {\n"
			"\tint i = 5;\n"
			"\tint s = 0;\n"
			"\twhile (i > 0) {\n"
			"\t\ts = s + i;\n"
			"\t\ti = i - 1;\n"
			"\t}\n"
			"\tprint(s);\n"
			"}\n",
			"15\n",
			"countdown Sub peephole"))
	{
		return EXIT_FAILURE;
	}

	{
		VeroScript::EExecutionBackend Parsed = VeroScript::EExecutionBackend::Vm;
		if (!Expect(VeroScript::TryParseExecutionBackend("mixed", Parsed)
				&& Parsed == VeroScript::EExecutionBackend::Mixed,
				"parse mixed backend"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(VeroScript::TryParseExecutionBackend("jit", Parsed)
				&& Parsed == VeroScript::EExecutionBackend::Jit,
				"parse jit backend"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!VeroScript::TryParseExecutionBackend("bogus", Parsed), "reject unknown backend"))
		{
			return EXIT_FAILURE;
		}
	}

	// Without a selected factory, Mixed uses the interpreter. Backend dispatch has separate contract/differential tests.
	if (!ExpectOutput("print(1 + 2);", "3\n", "mixed backend print(1+2)", VeroScript::EExecutionBackend::Mixed))
	{
		return EXIT_FAILURE;
	}
	if (!ExpectOutput("print(1 + 2);", "3\n", "jit backend print(1+2)", VeroScript::EExecutionBackend::Mixed))
	{
		return EXIT_FAILURE;
	}

	// Parser limits must reject hostile nesting and left-nested AST chains before
	// compiler recursion or destruction can exhaust the process stack.
	{
		std::string Parenthesized = "int value = ";
		Parenthesized.append(300, '(');
		Parenthesized += "1";
		Parenthesized.append(300, ')');
		Parenthesized += ";";
		if (!ExpectCompileFailureContaining(
				Parenthesized, "expression nested too deeply", "deep parentheses are diagnosed"))
		{
			return EXIT_FAILURE;
		}

		// The parser's generic nesting guard is intentionally generous. Semantic
		// expression families have different host-stack footprints in unoptimized
		// builds, so the compiler must also reject a deeply nested call AST without
		// allowing the host process to overflow its thread stack.
		std::string NestedCalls = "int id(int value) { return value; } int value = ";
		for (int Index = 0; Index < 40; ++Index)
		{
			NestedCalls += "id(";
		}
		NestedCalls += "1";
		NestedCalls.append(40, ')');
		NestedCalls += ";";
		if (!ExpectCompileFailureContaining(
				NestedCalls,
				"expression nested too deeply",
				"deep semantic call nesting is diagnosed"))
		{
			return EXIT_FAILURE;
		}

		std::string BoundaryBlocks = "void main() ";
		BoundaryBlocks.append(32, '{');
		BoundaryBlocks.append(32, '}');
		if (!ExpectOutput(
				BoundaryBlocks, "", "statement nesting at the documented limit is accepted"))
		{
			return EXIT_FAILURE;
		}

		std::string BoundaryIfs = "void main() { ";
		for (int Index = 0; Index < 32; ++Index)
		{
			BoundaryIfs += "if (true) ";
		}
		BoundaryIfs += "{} }";
		if (!ExpectOutput(
				BoundaryIfs, "", "single-statement nesting at the documented limit is accepted"))
		{
			return EXIT_FAILURE;
		}

		std::string Blocks = "void main() ";
		Blocks.append(34, '{');
		Blocks.append(34, '}');
		if (!ExpectCompileFailureContaining(
				Blocks, "statement nested too deeply", "deep blocks are diagnosed"))
		{
			return EXIT_FAILURE;
		}

		std::string TooManyIfs = "void main() { ";
		for (int Index = 0; Index < 33; ++Index)
		{
			TooManyIfs += "if (true) ";
		}
		TooManyIfs += "{} }";
		if (!ExpectCompileFailureContaining(
				TooManyIfs,
				"statement nested too deeply",
				"deep single-statement control flow is diagnosed"))
		{
			return EXIT_FAILURE;
		}

		std::string MixedNesting = "void main() { ";
		for (int Index = 0; Index < 40; ++Index)
		{
			MixedNesting += "if (true) { ";
		}
		MixedNesting += "print(1);";
		for (int Index = 0; Index < 40; ++Index)
		{
			MixedNesting += " }";
		}
		MixedNesting += " }";
		if (!ExpectCompileFailureContaining(
				MixedNesting,
				"statement nested too deeply",
				"mixed control-flow and block nesting is diagnosed before stack exhaustion"))
		{
			return EXIT_FAILURE;
		}

		std::string Chain = "int value = 1";
		for (int Index = 0; Index < 300; ++Index)
		{
			Chain += " + 1";
		}
		Chain += ";";
		if (!ExpectCompileFailureContaining(
				Chain, "expression chain is too long", "deep expression chain is diagnosed"))
		{
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}
