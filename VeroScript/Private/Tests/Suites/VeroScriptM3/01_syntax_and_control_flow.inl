// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Internal fragment: VeroScriptM3Tests / syntax and control flow suite. Included by the owning translation unit.

const FScopedTestTempRoot TestTempRoot;
static_cast<void>(TestTempRoot);
(void)VeroScript::Locale::TrySetLanguageFromTag("en-US");

if (!ExpectOutput(
			"int Calls = 0;\n"
			"int touch(int value) { Calls += 1; return value; }\n"
			"void main() {\n"
			"  (void) touch(41);\n"
			"  for ((void) touch(42); Calls < 3; Calls += 1) {}\n"
			"  touch(43);\n"
			"  Calls;\n"
			"  print(Calls);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"4\n",
			"explicit void discard executes once and discards only the result"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"import \"algorithm.vs\";\n"
			"import \"random.vs\";\n"
			"void main() {\n"
			"    array<int> values = {1, 2, 3, 4};\n"
			"    array<int> copied = std::algorithm::copy(values);\n"
			"    print(std::algorithm::equal(values, copied));\n"
			"    print(std::algorithm::contains(values, 3));\n"
			"    print(std::algorithm::is_sorted(values));\n"
			"    print(std::algorithm::min_index(values));\n"
			"    print(std::algorithm::max_index(values));\n"
			"    std::algorithm::rotate(copied, 2);\n"
			"    print(copied[0]); print(copied[3]);\n"
			"    std::algorithm::fill(copied, 7);\n"
			"    print(std::algorithm::count(copied, 7));\n"
			"    array<int> first = std::algorithm::copy(values);\n"
			"    array<int> second = std::algorithm::copy(values);\n"
			"    array<unsigned int> first_engine = std::random::make_engine(77u);\n"
			"    array<unsigned int> second_engine = std::random::make_engine(77u);\n"
			"    std::algorithm::shuffle(first, first_engine);\n"
			"    std::algorithm::shuffle(second, second_engine);\n"
			"    print(std::algorithm::equal(first, second));\n"
			"    std::algorithm::sort(first);\n"
			"    print(std::algorithm::equal(first, values));\n"
			"    bool bounded = true;\n"
			"    for (int i = 0; i < 100; i++) {\n"
			"        int die = std::random::next_int(first_engine, 1, 7);\n"
			"        bounded = bounded && die >= 1 && die < 7;\n"
			"    }\n"
			"    print(bounded);\n"
			"    print(std::random::next_bounded_uint(first_engine, 1u));\n"
			"    double sample = std::random::next_double_range(first_engine, -2.0, 3.0);\n"
			"    print(sample >= -2.0 && sample < 3.0);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\ntrue\ntrue\n0\n3\n3\n2\n4\ntrue\ntrue\ntrue\n0\ntrue\n",
			"expanded standard algorithms and bounded random generation"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			R"Vero(import "core.vs";
void main() {
    array<unsigned int> full = std::random::make_engine(4071982377u);
    assert(std::random::next_bounded_uint(full, 4294967295u) == 0u);
    array<unsigned int> narrow = std::random::make_engine(12345u);
    double hi = 1.0000000000000002;
    double sample = std::random::next_double_range(narrow, 1.0, hi);
    assert(sample >= 1.0 && sample < hi);
    double wide = std::random::next_double_range(narrow, -1e308, 1e308);
    assert(std::isfinite(wide) && wide >= -1e308 && wide < 1e308);
    array<double> large = {1e308, 1e308};
    assert(std::numeric::average(large) == 1e308);
    array<double> opposed = {1e308, -1e308};
    assert(std::numeric::average(opposed) == 0.0);
    assert(std::filesystem::dirname("C:\\foo.txt") == "C:\\");
    print("ok");
}
)Vero",
			VeroScript::ERunMode::Script,
			"ok\n",
			"standard library numeric and path boundaries"))
{
	return EXIT_FAILURE;
}

for (const std::string_view Source : {
		 std::string_view(
				"import \"random.vs\";\n"
				"void main() {\n"
				"    array<unsigned int> engine = std::random::make_engine(1u);\n"
				"    std::random::next_bounded_uint(engine, 0u);\n"
				"}\n"),
			std::string_view(
				"import \"algorithm.vs\";\n"
				"void main() {\n"
				"    array<int> values = {1};\n"
				"    std::algorithm::rotate(values, 2);\n"
				"}\n"),
			std::string_view(
				"import \"random.vs\";\n"
				"void main() {\n"
				"    array<unsigned int> engine = std::random::make_engine(1u);\n"
				"    std::random::next_double_range(engine, 2.0, 2.0);\n"
				"}\n"),
			std::string_view(
				"import \"random.vs\";\n"
				"void main() {\n"
				"    array<unsigned int> engine = std::random::make_engine(1u);\n"
				"    engine[0] = 0u;\n"
				"    std::random::next_uint(engine);\n"
				"}\n")})
{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<invalid-stdlib-range>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(
				!Result.bSuccess && !Errors.str().empty(),
				"standard-library invalid ranges fail safely"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

if (!ExpectOutput(
			"enum class CastFlag : uint32 { Ready = 7u };\n"
			"struct ExplicitBox {\n"
			"  int value;\n"
			"  explicit ExplicitBox(int input) { value = input; }\n"
			"};\n"
			"namespace CastNs {\n"
			"  struct Box { int value; explicit Box(int input) { value = input; } };\n"
			"}\n"
			"void main() {\n"
			"  double source = 2.75;\n"
			"  int converted = (int)source + 3;\n"
			"  uint32 flag = (uint32)CastFlag::Ready;\n"
			"  CastFlag grouped = (CastFlag::Ready);\n"
			"  ExplicitBox box = (ExplicitBox)41;\n"
			"  CastNs::Box namespaced = (CastNs::Box)42;\n"
			"  ::CastNs::Box rooted = (::CastNs::Box)43;\n"
			"  array<int> values = {1, 2};\n"
			"  array<int> same = (array<int>)values;\n"
			"  int left = 2; int right = 3;\n"
			"  print(converted); print(flag); print((uint32)grouped); print(box.value);\n"
			"  print(namespaced.value); print(rooted.value);\n"
			"  print(same[1]); print((left + right) * 2);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"5\n7\n7\n41\n42\n43\n2\n10\n",
			"C-style casts reuse cast<T> semantics without breaking parentheses"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"struct ShadowCallable {};\n"
			"struct ShadowObject { int value; ShadowObject(int input) { value = input; } };\n"
			"struct ShadowArray {};\n"
			"struct ShadowNumber {};\n"
			"void main() {\n"
			"  function(int)->int ShadowCallable = function(int value)->int { return value + 10; };\n"
			"  ShadowObject ShadowObject(20);\n"
			"  array<int> ShadowArray = {30};\n"
			"  int ShadowNumber = 41;\n"
			"  print((ShadowCallable)(1));\n"
			"  print((ShadowObject).value);\n"
			"  print((ShadowArray)[0]);\n"
			"  print((ShadowNumber)-1);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"11\n20\n30\n40\n",
			"C-style cast lookahead preserves shadowing value expressions"))
{
	return EXIT_FAILURE;
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"int touch() { return 1; } void main() { int value = (void)touch(); }\n",
			"<void-cast-value-rejected>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "void discard is rejected in a value context"))
	{
		return EXIT_FAILURE;
	}
}

if (!ExpectOutput(
			"int GlobalValue = 3;\n"
			"using Score = int;\n"
			"array<int> GlobalValues = array<int>();\n"
			"int& global_ref() { return GlobalValue; }\n"
			"const int& readonly_global() { return GlobalValue; }\n"
			"Score& alias_ref() { return GlobalValue; }\n"
			"int& forward_ref(int& value) { return value; }\n"
			"int add_one(int value) { return value + 1; }\n"
			"int& first_value() { return GlobalValues[0]; }\n"
			"class RefBox { public: int value = 5; int& get() { return this.value; } const int& read() const { return this.value; } };\n"
			"class RefNode { public: int value; RefNode(int input) { value = input; } };\n"
			"RefNode FirstNode = RefNode(1);\n"
			"RefNode SecondNode = RefNode(2);\n"
			"RefNode& selected_node() { return FirstNode; }\n"
			"class OutOfClassRef { public: int value = 17; int& get(); };\n"
			"int& OutOfClassRef::get() { return this.value; }\n"
			"class RefIndex { public: int value = 30; int& operator[](int index) { return value; } };\n"
			"class GenericRefBox<T> { public: T value; GenericRefBox(T input) { value = input; } T& get() { return value; } };\n"
			"void main() {\n"
			"  print(global_ref());\n"
			"  global_ref() = 7;\n"
			"  int& alias = global_ref();\n"
			"  alias++;\n"
			"  forward_ref(ref alias) += 2;\n"
			"  RefBox box = RefBox();\n"
			"  box.get() = 11;\n"
			"  print(GlobalValue);\n"
			"  print(readonly_global());\n"
			"  print(box.read());\n"
			"  fn()->int& callable = []() -> int& { return GlobalValue; };\n"
			"  callable() = 13;\n"
			"  print(GlobalValue);\n"
			"  print(global_ref() + 2);\n"
			"  print(add_one(global_ref()));\n"
			"  print(global_ref() < 20);\n"
			"  GlobalValues.push(20);\n"
			"  first_value() += 2;\n"
			"  print(GlobalValues[0]);\n"
			"  print(first_value() + 1);\n"
			"  print(selected_node().value);\n"
			"  selected_node() = SecondNode;\n"
			"  print(FirstNode.value);\n"
			"  OutOfClassRef external = OutOfClassRef();\n"
			"  external.get() = 19;\n"
			"  print(external.value);\n"
			"  RefIndex indexed = RefIndex();\n"
			"  int& indexed_alias = indexed[0];\n"
			"  indexed_alias += 2;\n"
			"  auto& inferred_alias = indexed[0];\n"
			"  inferred_alias++;\n"
			"  print(indexed[0]);\n"
			"  alias_ref() = 34;\n"
			"  print(GlobalValue);\n"
			"  GenericRefBox<int> generic_box = GenericRefBox<int>(40);\n"
			"  generic_box.get() += 2;\n"
			"  print(generic_box.get());\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n10\n10\n11\n13\n15\n14\ntrue\n22\n23\n1\n2\n19\n33\n34\n42\n",
			"reference returns preserve writable and const bindings"))
{
	return EXIT_FAILURE;
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"int& bad() { int local = 1; return local; } void main() {}\n",
			"<dangling-reference-return>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "reference return rejects ordinary stack locals"))
	{
		return EXIT_FAILURE;
	}
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"interface IValue { virtual int get() const = 0; }\n"
			"interface IOther { virtual int other() const = 0; }\n"
			"struct Value : IValue {\n"
			"    int n = 0;\n"
			"    Value(int value) { n = value; }\n"
			"    int get() const { return n; }\n"
			"}\n"
			"void main() {\n"
			"    Value original(41);\n"
			"    optional<IValue> boxed = try_cast<IValue>(original);\n"
			"    assert(boxed.has_value() && boxed.get().get() == 41);\n"
			"    IValue view = original;\n"
			"    optional<Value> restored = try_cast<Value>(view);\n"
			"    assert(restored.has_value() && restored.get().n == 41);\n"
			"    Value copy = cast<Value>(view);\n"
			"    copy.n = 99;\n"
			"    assert(view.get() == 41 && copy.n == 99);\n"
			"    optional<IOther> missing = try_cast<IOther>(view);\n"
			"    assert(!missing.has_value());\n"
			"}\n",
			"<struct-interface-casts>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(Result.bSuccess,
				"struct/interface checked casts preserve value semantics"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

if (!ExpectOutput(
			"struct Meter {\n"
			"    int value = 0;\n"
			"    Meter(int input) { value = input; }\n"
			"}\n"
			"struct Token {\n"
			"    int value = 0;\n"
			"    explicit Token(int input) { value = input; }\n"
			"}\n"
			"int read(Meter value) { return value.value; }\n"
			"void main() {\n"
			"    Meter assigned = 7;\n"
			"    print(assigned.value);\n"
			"    print(read(8));\n"
			"    Token direct(9);\n"
			"    Token converted = cast<Token>(10);\n"
			"    print(direct.value + converted.value);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\n8\n19\n",
			"converting and explicit constructors obey conversion context"))
{
	return EXIT_FAILURE;
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"struct Token { explicit Token(int value) {} }\n"
			"void main() { Token value = 1; }\n",
			"<explicit-constructor-rejects-implicit-conversion>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess,
				"explicit constructor must not participate in implicit conversion"))
	{
		return EXIT_FAILURE;
	}
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"enum class Mode { Idle = 3 }\n"
			"enum class OtherMode { Idle = 3 }\n"
			"enum class LongNamedEnumForCheckedNumericConversions : int64 { Wide = 5000000008L, Negative = -7L }\n"
			"class Base {}\n"
			"class Derived : Base {}\n"
			"class Other : Base {}\n"
			"int cast(int value) { return value + 1; }\n"
			"int read_const(const ref int value) { return value; }\n"
			"void main() {\n"
			"    double d = cast<double>(7);\n"
			"    int n = cast<int>(2.9);\n"
			"    assert(cast(2) == 3);\n"
			"    Mode mode = cast<Mode>(3);\n"
			"    optional<OtherMode> other_mode = try_cast<OtherMode>(mode);\n"
			"    assert(d == 7.0 && n == 2 && cast<int>(mode) == 3);\n"
			"    auto wide = LongNamedEnumForCheckedNumericConversions::Wide;\n"
			"    assert(cast<int64>(wide) == 5000000008L && cast<double>(wide) == 5000000008.0);\n"
			"    assert(!try_cast<int>(wide).has_value());\n"
			"    assert(cast<int>(LongNamedEnumForCheckedNumericConversions::Negative) == -7);\n"
			"    assert(!other_mode.has_value());\n"
			"    Base base = Derived();\n"
			"    Derived derived = cast<Derived>(base);\n"
			"    optional<Derived> found = try_cast<Derived>(base);\n"
			"    optional<Other> missing = try_cast<Other>(base);\n"
			"    optional<Other> null_value = try_cast<Other>(nullptr);\n"
			"    assert(derived != nullptr && found.has_value() && !missing.has_value());\n"
			"    assert(null_value.has_value() && null_value.get() == nullptr);\n"
			"    optional<int> narrowed = try_cast<int>(2.9);\n"
			"    assert(narrowed.has_value() && narrowed.get() == 2);\n"
			"    const int immutable = cast<int>(narrowed.get());\n"
			"    assert(read_const(ref immutable) == 2);\n"
			"    auto inferred = try_cast<int>(2.9);\n"
			"    assert(inferred.has_value() && inferred.get() == 2);\n"
			"}\n",
			"<cast-intrinsics>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(Result.bSuccess, "cast<T> and try_cast<T> intrinsics"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"class Base {}\n"
			"class Derived : Base {}\n"
			"class Other : Base {}\n"
			"void main() { Base value = Derived(); cast<Other>(value); }\n",
			"<cast-failure>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess && Errors.str().find("cast failed") != std::string::npos,
				"cast<T> should report failed checked reference casts"))
	{
		return EXIT_FAILURE;
	}
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"int Value = 1; const int& read() { return Value; } void main() { read() = 2; }\n",
			"<const-reference-return-write>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "const reference return rejects writes"))
	{
		return EXIT_FAILURE;
	}
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"int value() { return 1; } void main() { value() = 2; }\n",
			"<value-return-write>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "ordinary value-returning calls are not assignable"))
	{
		return EXIT_FAILURE;
	}
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"int Value = 1; const int& read() { return Value; } "
			"int& bad() { return read(); } void main() {}\n",
			"<mutable-reference-from-const-return>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "mutable reference returns reject const bindings"))
	{
		return EXIT_FAILURE;
	}
}

if (!ExpectOutput(
			"class Empty {};\n"
			"struct Plain {};\n"
			"struct Counter { int value = 7; } First, Second();\n"
			"class Reader { public: int value = 9; int get() { return this.value; } } GlobalReader;\n"
			"struct WithArg { int value; WithArg(int v) { this.value = v; } } Arg(13);\n"
			"struct AssignedValue { int value; AssignedValue(int v) { this.value = v; } } Assigned = AssignedValue(17);\n"
			"namespace N { struct Item { int value = 11; } Value; }\n"
			"struct LegacyNoSemicolon {}\n"
			"print(\"top-level call\");\n"
			"void main() {\n"
			"  print(First.value);\n"
			"  print(Second.value);\n"
			"  print(GlobalReader.get());\n"
			"  print(Arg.value);\n"
			"  print(Assigned.value);\n"
			"  print(N::Value.value);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"top-level call\n7\n7\n9\n13\n17\n11\n",
			"optional trailing type semicolon and definition declarators"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"void main() {\n"
			"  int64 a = 9223372036854775800i64;\n"
			"  int64 b = 7;\n"
			"  uint64 u = 18446744073709551615u64;\n"
			"  print(a + b);\n"
			"  print(u);\n"
			"  print((u >> 63) == 1u64);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"9223372036854775807\n18446744073709551615\ntrue\n",
			"int64/uint64 literals and arithmetic"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"int select_signed(int value) { return 1; }\n"
			"int select_signed(int64 value) { return 2; }\n"
			"int select_unsigned(unsigned int value) { return 3; }\n"
			"int select_unsigned(uint64 value) { return 4; }\n"
			"void main() {\n"
			"  print(select_signed(-1i64));\n"
			"  print(select_signed(1i64 + 2));\n"
			"  print(select_signed(min(1i64, 2)));\n"
			"  print(select_unsigned(~1u64));\n"
			"  print(select_unsigned(max(1u64, 2u64)));\n"
			"  print(\"wide=\" + 9223372036854775807i64);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n2\n2\n4\n4\nwide=9223372036854775807\n",
			"int64/uint64 expression inference preserves overload width"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"void main() {\n"
			"  int32 value = 11;\n"
			"  uint32 key = 4294967295u;\n"
			"  map<uint32,int32> m = map<uint32,int32>();\n"
			"  m[key] = value;\n"
			"  print(m.has(key)); print(m[key]); print(m.keys()[0]);\n"
			"  set<uint32> s = set<uint32>();\n"
			"  s.add(key); print(s.has(key)); print(s.values()[0]);\n"
			"  ordered_map<uint32,int32> o = ordered_map<uint32,int32>();\n"
			"  o[key] = 12; print(o.keys()[0]); print(o.values()[0]);\n"
			"  print(m.remove(key)); print(s.remove(key)); print(len(o));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\n11\n4294967295\ntrue\n4294967295\n"
			"4294967295\n12\ntrue\ntrue\n1\n",
			"int32/uint32 aliases and uint32 container keys"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"enum class SignedByte : int8 { Min = -128, Max = 127 }\n"
			"enum class Byte : uint8 { Zero = 0u, Max = 255u }\n"
			"enum class SignedWord : int16 { Min = -32768, Max = 32767 }\n"
			"enum class Word : uint16 { Zero = 0u, Max = 65535u }\n"
			"enum class RawByte : unsigned char { Max = 255u }\n"
			"enum class RawWord : unsigned short { Max = 65535u }\n"
			"int8 echo8(int8 value) { return value; }\n"
			"uint16 echo16(uint16 value) { return value; }\n"
			"void main() {\n"
			"  int8 i8 = -128;\n"
			"  uint8 u8 = 255u;\n"
			"  int16 i16 = -32768;\n"
			"  uint16 u16 = 65535u;\n"
			"  array<int8> signed_values = array<int8>{i8, 127};\n"
			"  array<uint16> words = array<uint16>{u16, 1u};\n"
			"  print(echo8(signed_values[0]));\n"
			"  print(u8);\n"
			"  print(i16);\n"
			"  print(echo16(words[0]));\n"
			"  print(SignedByte::Min);\n"
			"  print(Byte::Max);\n"
			"  print(SignedWord::Max);\n"
			"  print(Word::Max);\n"
			"  print(RawByte::Max);\n"
			"  print(RawWord::Max);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"-128\n255\n-32768\n65535\n"
			"SignedByte(-128)\nByte(255)\nSignedWord(32767)\nWord(65535)\n"
			"RawByte(255)\nRawWord(65535)\n",
			"int8 uint8 int16 uint16 aliases and enum underlying types"))
{
	return EXIT_FAILURE;
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"enum class TooWide : uint8 { Value = 256u }\n"
			"void main() {}\n",
			"<uint8_enum_range>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "uint8-backed enum must reject values above 255"))
	{
		return EXIT_FAILURE;
	}
}

if (!ExpectOutput(
			"int[][] echo(int[][] values) { return values; }\n"
			"void main() {\n"
			"  int[] row = int[]();\n"
			"  row.push(7);\n"
			"  int[][] grid = int[][]();\n"
			"  grid.push(row);\n"
			"  int[][] copy = echo(grid);\n"
			"  print(copy[0][0]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\n",
			"T[][] dynamic array sugar"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"void main() {\n"
			"  int64 i = 9223372036854775807i64;\n"
			"  uint64 u = 18446744073709551615u64;\n"
			"  switch (i) { case 9223372036854775807i64: print(1); default: print(0); }\n"
			"  switch (u) { case 18446744073709551615u64: print(2); default: print(0); }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n2\n",
			"int64/uint64 switch preserves full precision"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"void main() {\n"
			"  int64 ik = 9223372036854775807i64;\n"
			"  uint64 uk = 18446744073709551615u64;\n"
			"  map<int64,string> m = map<int64,string>();\n"
			"  m[ik] = \"max\";\n"
			"  print(m.has(ik)); print(m[ik]); print(len(m));\n"
			"  set<uint64> s = set<uint64>();\n"
			"  s.add(uk); print(s.has(uk)); print(s.values()[0]);\n"
			"  ordered_map<uint64,int> o = ordered_map<uint64,int>();\n"
			"  o[uk] = 9; print(o.keys()[0]); print(o.values()[0]);\n"
			"  print(m.remove(ik)); print(s.remove(uk)); print(len(o));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\nmax\n1\ntrue\n18446744073709551615\n"
			"18446744073709551615\n9\ntrue\ntrue\n1\n",
			"int64/uint64 map set and ordered_map keys"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"enum class Color { Red = 1, Green = 2 }\n"
			"enum Legacy { One = 1, Two = 2 }\n"
			"void main() {\n"
			"  Color c = Color.Red;\n"
			"  print(c == Color.Red);\n"
			"  print(c != Color.Green);\n"
			"  int legacy = One;\n"
			"  print(legacy + Two);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\ntrue\n3\n",
			"strong enum runtime tag and legacy enum compatibility"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"enum class Big : int64 {\n"
			"  Small = 1,\n"
			"  Huge = 9223372036854775807i64\n"
			"}\n"
			"enum class Bounds : int64 {\n"
			"  Min = -9223372036854775808i64,\n"
			"  Zero = 0\n"
			"}\n"
			"void main() {\n"
			"  Big value = Big.Huge;\n"
			"  print(value);\n"
			"  print(Bounds.Min);\n"
			"  map<Big,int> values = map<Big,int>();\n"
			"  values[Big.Huge] = 9;\n"
			"  print(values[Big.Huge]);\n"
			"  switch (value) {\n"
			"    case Big.Small: print(1); break;\n"
			"    case Big.Huge: print(2); break;\n"
			"  }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"Big(9223372036854775807)\n"
			"Bounds(-9223372036854775808)\n9\n2\n",
			"int64-backed strong enum preserves full-width values"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"enum class Flags : uint32 {\n"
			"  None = 0u,\n"
			"  All = 4294967295u\n"
			"}\n"
			"enum class Small : int32 { Value = 7 }\n"
			"void main() {\n"
			"  Flags flags = Flags.All;\n"
			"  print(flags);\n"
			"  print(Small.Value);\n"
			"  switch (flags) { case Flags.None: print(0); break; "
				"case Flags.All: print(1); break; }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"Flags(4294967295)\nSmall(7)\n1\n",
			"int32 and uint32 enum class underlying types"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"enum Wide : int64 { Huge = 9223372036854775807i64 }\n"
			"void main() {\n"
			"  int64 value = Huge;\n"
			"  print(value);\n"
			"  switch (value) {\n"
			"    case Wide.Huge: print(1); break;\n"
			"    default: print(0);\n"
			"  }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"9223372036854775807\n1\n",
			"int64-backed unscoped enum preserves full-width constants"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"enum class Color { Red = 1, Green = 2, Blue = 3 }\n"
			"void bump(ref int value) { value = value + 1; }\n"
			"void main() {\n"
			"  map<Color,int> scores = map<Color,int>();\n"
			"  scores[Color.Red] = 10;\n"
			"  scores[Color.Green] = 20;\n"
			"  bump(ref scores[Color.Red]);\n"
			"  print(scores[Color.Red]);\n"
			"  print(scores.has(Color.Green));\n"
			"  print(len(scores));\n"
			"  print(scores.remove(Color.Green));\n"
			"  print(len(scores.entries()));\n"
			"  set<Color> selected = set<Color>();\n"
			"  selected.add(Color.Red);\n"
			"  selected.add(Color.Green);\n"
			"  selected.add(Color.Red);\n"
			"  print(len(selected));\n"
			"  print(selected.has(Color.Green));\n"
			"  print(selected.remove(Color.Green));\n"
			"  print(len(selected.values()));\n"
			"  ordered_map<Color,int> ordered = ordered_map<Color,int>();\n"
			"  ordered[Color.Green] = 20;\n"
			"  ordered[Color.Red] = 10;\n"
			"  ordered[Color.Green] = 21;\n"
			"  print(len(ordered));\n"
			"  print(ordered.keys()[0]);\n"
			"  print(ordered.values()[0]);\n"
			"  print(ordered.keys()[1]);\n"
			"  print(len(ordered.entries()));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"11\ntrue\n2\ntrue\n1\n2\ntrue\ntrue\n1\n"
			"2\nColor(2)\n21\nColor(1)\n2\n",
			"strong enum map set and ordered_map keys"))
{
	return EXIT_FAILURE;
}

for (const std::string_view InvalidStrongEnum : {
		 std::string_view{
			 "enum class Color { Red = 1 }\n"
				"void main() { int value = Color.Red; }\n"},
			std::string_view{
			 "enum class Color { Red = 1 }\n"
				"void main() { Color value = 1; }\n"},
			std::string_view{
			 "enum class Color { Red = 1 }\n"
				"void main() { Color value = Color.Red; print(value + 1); }\n"},
			std::string_view{
			 "enum class A { Value = 1 }\n"
				"enum class B { Value = 1 }\n"
				"void main() { print(A.Value == B.Value); }\n"},
			std::string_view{
			 "enum class A { Value = 1 }\n"
				"enum class B { Value = 1 }\n"
				"void main() { A a = A.Value; switch (a) { case B.Value: print(1); } }\n"}})
{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			InvalidStrongEnum,
			"<strong-enum-negative>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "enum class must reject implicit integer conversion/arithmetic"))
	{
		return EXIT_FAILURE;
	}
}

if (!ExpectOutput(
			"print(1);\n"
			"void main() { print(2); }\n",
			VeroScript::ERunMode::Script,
			"1\n2\n",
			"top-level init before main"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"int g = 0;\n"
			"void main() { g = g + 1; print(g); }\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"module-level global variable"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"int one() { return 1; }\n"
			"void main() { print(one()); }\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"literal return"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"int id(int n) { return n; }\n"
			"void main() { print(id(5)); }\n",
			VeroScript::ERunMode::Script,
			"5\n",
			"param return"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"int fib2(int n) {\n"
			"    if (n <= 1) { return n; }\n"
			"    return 0;\n"
			"}\n"
			"void main() { print(fib2(1)); }\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"fib2 if true branch"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"int zero(int n) { return 0; }\n"
			"void main() { print(zero(2)); }\n",
			VeroScript::ERunMode::Script,
			"0\n",
			"constant return with param"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"int f(int n) { if (n <= 1) return n; return 0; }\n"
			"void main() { print(f(2)); }\n",
			VeroScript::ERunMode::Script,
			"0\n",
			"if unbraced fallthrough"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"int f(int n) { if (n <= 1) { return n; } else { return 0; } }\n"
			"void main() { print(f(2)); }\n",
			VeroScript::ERunMode::Script,
			"0\n",
			"if else return"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"int fib2(int n) {\n"
			"    if (n <= 1) { return n; }\n"
			"    return 0;\n"
			"}\n"
			"void main() { print(fib2(2)); }\n",
			VeroScript::ERunMode::Script,
			"0\n",
			"fib2 if false fallthrough"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"int fib2(int n) {\n"
			"    if (n <= 1) { return n; }\n"
			"    return fib2(1);\n"
			"}\n"
			"void main() { print(fib2(2)); }\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"single recursive call"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"int fib2(int n) {\n"
			"    if (n <= 1) { return n; }\n"
			"    return fib2(n - 1) + fib2(n - 2);\n"
			"}\n"
			"void main() { print(fib2(5)); }\n",
			VeroScript::ERunMode::Script,
			"5\n",
			"recursive fib2"))
{
	return EXIT_FAILURE;
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			"examples/fib.vs",
			Output,
			Errors);
	if (!Expect(Result.bSuccess, "examples/fib.vs should run"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
	if (!Expect(
				Output.str() == "count = 1\nfib(10) = 55\n",
				"fib.vs output"))
	{
		std::cerr << "got:\n"
				  << Output.str();
		return EXIT_FAILURE;
	}
}

{
	const char* Source =
			"struct Vec2 {\n"
			"    int x = 0;\n"
			"    int y = 0;\n"
			"}\n"
			"void main() {\n"
			"    auto a = Vec2();\n"
			"    a.x = 1;\n"
			"    auto b = a;\n"
			"    b.x = 99;\n"
			"    print(to_string(a.x));\n"
			"    print(to_string(b.x));\n"
			"}\n";

	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(Result.bSuccess, "struct copy test should run"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
	if (!Expect(Output.str() == "1\n99\n", "struct deep copy"))
	{
		std::cerr << "got:\n"
				  << Output.str();
		return EXIT_FAILURE;
	}
}

{
	const char* Source =
			"struct S { int x = 0; }\n"
			"void set99(S v) { v.x = 99; }\n"
			"void main() {\n"
			"    auto a = S();\n"
			"    set99(a);\n"
			"    print(a.x);\n"
			"}\n";

	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(Result.bSuccess, "struct pass-by-value should run"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
	if (!Expect(Output.str() == "0\n", "struct pass-by-value"))
	{
		std::cerr << "got:\n"
				  << Output.str();
		return EXIT_FAILURE;
	}
}

{
	const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-nested-import-test";
	std::error_code RemoveError;
	std::filesystem::remove_all(TempRoot, RemoveError);
	std::filesystem::create_directories(TempRoot / "lib");

	{
		std::ofstream UtilFile(TempRoot / "lib" / "util.vs");
		UtilFile << "export int util_val() { return 42; }\n";
	}
	{
		std::ofstream MainFile(TempRoot / "main.vs");
		MainFile << "import \"lib/util.vs\";\n"
				<< "void main() { print(util_val()); }\n";
	}

	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").string(),
			Output,
			Errors);
	std::filesystem::remove_all(TempRoot, RemoveError);
	if (!Expect(Result.bSuccess, "nested import should run"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
	if (!Expect(Output.str() == "42\n", "nested import output"))
	{
		std::cerr << "got:\n"
				  << Output.str();
		return EXIT_FAILURE;
	}
}

{
	const char* Source =
			"struct S { int x = 0; }\n"
			"void main() {\n"
			"    S s = S();\n"
			"    s.x = 5;\n"
			"    print(s.x);\n"
			"}\n";

	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(Result.bSuccess, "typed struct local should run"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
	if (!Expect(Output.str() == "5\n", "typed struct local"))
	{
		std::cerr << "got:\n"
				  << Output.str();
		return EXIT_FAILURE;
	}
}

{
	const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-import-type-test";
	std::error_code RemoveError;
	std::filesystem::remove_all(TempRoot, RemoveError);
	std::filesystem::create_directories(TempRoot);

	{
		std::ofstream UtilFile(TempRoot / "types.vs");
		UtilFile << "export struct S { int x = 0; }\n"
				<< "export int read_x(S v) { return v.x; }\n";
	}
	{
		std::ofstream MainFile(TempRoot / "main.vs");
		MainFile << "import \"types.vs\";\n"
				<< "void main() {\n"
				<< "    S s = S();\n"
				<< "    s.x = 7;\n"
				<< "    print(read_x(s));\n"
				<< "}\n";
	}

	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").string(),
			Output,
			Errors);
	std::filesystem::remove_all(TempRoot, RemoveError);
	if (!Expect(Result.bSuccess, "imported struct type should run"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
	if (!Expect(Output.str() == "7\n", "imported struct type"))
	{
		std::cerr << "got:\n"
				  << Output.str();
		return EXIT_FAILURE;
	}
}

{
	const char* Source =
			"struct Inner { int x = 0; }\n"
			"struct Outer { Inner i; }\n"
			"void main() {\n"
			"    auto o = Outer();\n"
			"    o.i = Inner();\n"
			"    o.i.x = 3;\n"
			"    print(o.i.x);\n"
			"}\n";

	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source,
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(Result.bSuccess, "nested struct field type should run"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
	if (!Expect(Output.str() == "3\n", "nested struct field type"))
	{
		std::cerr << "got:\n"
				  << Output.str();
		return EXIT_FAILURE;
	}
}

if (!ExpectOutput(
			"void main() {\n"
			"    print(true && false);\n"
			"    print(true || false);\n"
			"    print(!false);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"false\ntrue\ntrue\n",
			"logical operators"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"void main() {\n"
			"    int sum = 0;\n"
			"    for (int i = 0; i < 5; i = i + 1) {\n"
			"        sum = sum + i;\n"
			"    }\n"
			"    print(sum);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"10\n",
			"for loop"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"void main() {\n"
			"    int n = 0;\n"
			"    for (int i = 0; i < 10; i = i + 1) {\n"
			"        if (i == 5) { break; }\n"
			"        n = n + 1;\n"
			"    }\n"
			"    print(n);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"5\n",
			"break in for loop"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"void main() {\n"
			"    int n = 0;\n"
			"    for (int i = 0; i < 5; i = i + 1) {\n"
			"        if (i == 2) { continue; }\n"
			"        n = n + 1;\n"
			"    }\n"
			"    print(n);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"4\n",
			"continue in for loop"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"void main() {\n"
			"    int x = 2;\n"
			"    switch (x) {\n"
			"        case 1:\n"
			"            print(10);\n"
			"        case 2:\n"
			"        case 3:\n"
			"            print(20);\n"
			"            break;\n"
			"            print(99);\n"
			"        default:\n"
			"            print(30);\n"
			"    }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"20\n",
			"switch shared case labels and break"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"enum Color { Red, Green = 2, Blue }\n"
			"void main() {\n"
			"    switch (Green) {\n"
			"        case Red:\n"
			"            print(1);\n"
			"        case Color.Green:\n"
			"            print(2);\n"
			"        default:\n"
			"            print(0);\n"
			"    }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n",
			"switch enum case labels"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"void main() {\n"
			"    int y = 9;\n"
			"    switch (y) {\n"
			"        default:\n"
			"            print(7);\n"
			"        case 1:\n"
			"            print(8);\n"
			"    }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\n",
			"switch default branch"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"void main() {\n"
			"    int x = -1;\n"
			"    switch (x) {\n"
			"        case -1:\n"
			"            print(11);\n"
			"        default:\n"
			"            print(0);\n"
			"    }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"11\n",
			"switch negative case label"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"void main() {\n"
			"    unsigned u = 2;\n"
			"    switch (u) {\n"
			"        case 1:\n"
			"            print(1);\n"
			"        case 2:\n"
			"            print(2);\n"
			"        default:\n"
			"            print(0);\n"
			"    }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n",
			"switch unsigned discriminant"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"void main() {\n"
			"    unsigned u = 4000000000u;\n"
			"    switch (u) {\n"
			"        case 4000000000u:\n"
			"            print(40);\n"
			"        default:\n"
			"            print(0);\n"
			"    }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"40\n",
			"switch large unsigned case label"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"void main() {\n"
			"    int n = 0;\n"
			"    for (int i = 0; i < 3; i = i + 1) {\n"
			"        switch (i) {\n"
			"            case 1:\n"
			"                continue;\n"
			"            default:\n"
			"                n = n + 1;\n"
			"        }\n"
			"    }\n"
			"    print(n);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n",
			"continue in switch inside loop"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"void main() {\n"
			"    int x = 1;\n"
			"    switch (x) {\n"
			"        case 1:\n"
			"        default:\n"
			"        case 2:\n"
			"            print(120);\n"
			"    }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"120\n",
			"switch default between shared case labels"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"void main() {\n"
			"    int x = 9;\n"
			"    switch (x) {\n"
			"        default:\n"
			"        case 5:\n"
			"            print(55);\n"
			"    }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"55\n",
			"switch default before shared case labels"))
{
	return EXIT_FAILURE;
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    switch (1) {\n"
			"        case 1:\n"
			"            continue;\n"
			"    }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "continue in switch should fail"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
				Errors.str().find("continue outside loop") != std::string::npos,
				"continue in switch error message"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

	// Retained capturing closures remap captures by stable name/type, including
	// an explicit capture-order change in the replacement module.
{
	struct FCaptureReloadHost final : VeroScript::IEngineHost
	{
		//~ Begin VeroScript::IEngineHost Interface
		void OnPrintLine(const std::string_view Line) override
		{
			PrintText.append(Line);
			PrintText.push_back('\n');
		}

		void OnDiagnosticLine(const std::string_view Line) override
		{
			DiagnosticText.append(Line);
			DiagnosticText.push_back('\n');
		}
		//~ End VeroScript::IEngineHost Interface

		std::string PrintText;
		std::string DiagnosticText;

	};

	const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-hot-reload-captures";
	std::error_code RemoveError;
	std::filesystem::remove_all(TempRoot, RemoveError);
	std::filesystem::create_directories(TempRoot);
	const std::filesystem::path ScriptPath = TempRoot / "captures.vs";
	const auto WriteScript = [&ScriptPath](const std::string_view Source)
	{
		std::ofstream Out(ScriptPath, std::ios::binary | std::ios::trunc);
		Out << Source;
	};

	WriteScript(
			"fn()->int saved = []() -> int => 0;\n"
			"int runs = 0;\n"
			"void main() {\n"
			"    if (runs == 0) {\n"
			"        int a = 20; int b = 2;\n"
			"        saved = [a, b]() -> int => a + b;\n"
			"    }\n"
			"    runs++;\n"
			"    print(saved());\n"
			"}\n");
	FCaptureReloadHost Host;
	VeroScript::VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
	Session.SetHotReloadEnabled(true);
	if (!Expect(Session.LoadFile(ScriptPath.generic_string()).bSuccess,
				"capture hot reload initial load")
			|| !Expect(Session.Run().bSuccess, "capture hot reload initial run")
			|| !Expect(Host.PrintText == "22\n", "capture hot reload initial output"))
	{
		std::cerr << Host.DiagnosticText;
		std::filesystem::remove_all(TempRoot, RemoveError);
		return EXIT_FAILURE;
	}

	WriteScript(
			"fn()->int saved = []() -> int => 0;\n"
			"int runs = 0;\n"
			"void main() {\n"
			"    if (runs == 0) {\n"
			"        int a = 20; int b = 2;\n"
			"        saved = [b, a]() -> int => a * 100 + b;\n"
			"    }\n"
			"    runs++;\n"
			"    print(saved());\n"
			"}\n");
	Host.PrintText.clear();
	if (!Expect(Session.Reload().bSuccess, "capture hot reload replacement")
			|| !Expect(Session.Run().bSuccess, "capture hot reload replacement run")
			|| !Expect(
				Host.PrintText == "2002\n",
				"capture hot reload preserves values across slot reorder"))
	{
		std::cerr << Host.DiagnosticText;
		std::filesystem::remove_all(TempRoot, RemoveError);
		return EXIT_FAILURE;
	}
	Session.Clear();
	std::filesystem::remove_all(TempRoot, RemoveError);
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    switch (1) {\n"
			"        case 1:\n"
			"            print(1);\n"
			"        case 1:\n"
			"            print(2);\n"
			"    }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "duplicate case should fail"))
	{
		return EXIT_FAILURE;
	}
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    switch (1.5) {\n"
			"        default:\n"
			"            print(1);\n"
			"    }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "non-integer non-string switch discriminant should fail"))
	{
		return EXIT_FAILURE;
	}
}

if (!ExpectOutput(
			"void main() {\n"
			"    string s = \"b\";\n"
			"    switch (s) {\n"
			"        case \"a\":\n"
			"            print(1);\n"
			"        case \"b\":\n"
			"        case \"c\":\n"
			"            print(2);\n"
			"            break;\n"
			"        default:\n"
			"            print(0);\n"
			"    }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n",
			"string switch shared labels and break"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"void main() {\n"
			"    switch (\"z\") {\n"
			"        case \"a\":\n"
			"            print(1);\n"
			"        default:\n"
			"            print(9);\n"
			"    }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"9\n",
			"string switch default"))
{
	return EXIT_FAILURE;
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    switch (\"a\") {\n"
			"        case 1:\n"
			"            print(1);\n"
			"    }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "string switch with int case should fail"))
	{
		return EXIT_FAILURE;
	}
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    switch (1) {\n"
			"        case \"a\":\n"
			"            print(1);\n"
			"    }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "int switch with string case should fail"))
	{
		return EXIT_FAILURE;
	}
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    switch (\"a\") {\n"
			"        case \"a\":\n"
			"            print(1);\n"
			"        case \"a\":\n"
			"            print(2);\n"
			"    }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "duplicate string case should fail"))
	{
		return EXIT_FAILURE;
	}
}

if (!ExpectOutput(
			"void main() {\n"
			"    wstring s = L\"b\";\n"
			"    switch (s) {\n"
			"        case L\"a\":\n"
			"            print(1);\n"
			"        case L\"b\":\n"
			"        case L\"c\":\n"
			"            print(2);\n"
			"            break;\n"
			"        default:\n"
			"            print(0);\n"
			"    }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n",
			"wstring switch shared labels and break"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"void main() {\n"
			"    switch (L\"z\") {\n"
			"        case L\"a\":\n"
			"            print(1);\n"
			"        default:\n"
			"            print(9);\n"
			"    }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"9\n",
			"wstring switch default"))
{
	return EXIT_FAILURE;
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    switch (L\"a\") {\n"
			"        case \"a\":\n"
			"            print(1);\n"
			"    }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "wstring switch with string case should fail"))
	{
		return EXIT_FAILURE;
	}
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    switch (\"a\") {\n"
			"        case L\"a\":\n"
			"            print(1);\n"
			"    }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "string switch with wstring case should fail"))
	{
		return EXIT_FAILURE;
	}
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() {\n"
			"    switch (L\"a\") {\n"
			"        case L\"a\":\n"
			"            print(1);\n"
			"        case L\"a\":\n"
			"            print(2);\n"
			"    }\n"
			"}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "duplicate wstring case should fail"))
	{
		return EXIT_FAILURE;
	}
}

if (!ExpectOutput(
			"void main() {\n"
			"    switch (L\"\") {\n"
			"        case L\"\":\n"
			"            print(1);\n"
			"        default:\n"
			"            print(0);\n"
			"    }\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"1\n",
			"wstring switch empty literal"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"void main() {\n"
			"    int arr[3];\n"
			"    arr[0] = 10;\n"
			"    arr[1] = 20;\n"
			"    print(len(arr));\n"
			"    print(arr[0]);\n"
			"    print(arr[1]);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3\n10\n20\n",
			"native C++ array T[N]"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"struct S { int xs[2]; }\n"
			"void main() {\n"
			"    S s = S();\n"
			"    print(len(s.xs));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"2\n",
			"struct native array field"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"struct Inner { int x = 5; }\n"
			"struct Outer { Inner i = Inner(); }\n"
			"void main() {\n"
			"    Outer o = Outer();\n"
			"    print(o.i.x);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"5\n",
			"struct field constructor default"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"struct S { int x = 0; }\n"
			"void main() {\n"
			"    S a = S();\n"
			"    S b = S();\n"
			"    a.x = 1;\n"
			"    b.x = 1;\n"
			"    print(a == b);\n"
			"    b.x = 2;\n"
			"    print(a == b);\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"true\nfalse\n",
			"struct field-wise equality"))
{
	return EXIT_FAILURE;
}

{
	const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-stdlib-math-test";
	std::error_code RemoveError;
	std::filesystem::remove_all(TempRoot, RemoveError);
	std::filesystem::create_directories(TempRoot);
	{
		std::ofstream MainFile(TempRoot / "main.vs");
		MainFile
				<< "import \"math.vs\";\n"
				<< "void main() {\n"
				<< "    print(abs(-5));\n"
				<< "    print(min(2, 7));\n"
				<< "    print(max(2, 7));\n"
				<< "    print(to_string(std::sqrt(9.0)));\n"
				<< "    print(to_string(std::round(2.5)));\n"
				<< "    print(to_string(std::pow(2.0, 3.0)));\n"
				<< "    print(std::isfinite(std::log(1.0)));\n"
				<< "    print(to_string(std::trunc(-2.9)));\n"
				<< "    print(to_string(std::exp(0.0)));\n"
				<< "    print(to_string(std::log2(8.0)));\n"
				<< "    print(to_string(std::asin(0.0)));\n"
				<< "    print(to_string(std::acos(1.0)));\n"
				<< "    print(to_string(std::atan2(0.0, 1.0)));\n"
				<< "    print(std::isnan(std::log(-1.0)));\n"
				<< "    print(std::pi() > 3.0 && std::e() > 2.0);\n"
				<< "    print(std::chrono::steady_now() > 0.0);\n"
				<< "}\n";
	}

	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").string(),
			Output,
			Errors);
	std::filesystem::remove_all(TempRoot, RemoveError);
	if (!Expect(Result.bSuccess, "stdlib math.vs should run"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
	if (!Expect(
				Output.str() == "5\n2\n7\n3\n3\n8\ntrue\n-2\n1\n3\n0\n0\n0\ntrue\ntrue\ntrue\n",
				"stdlib math.vs"))
	{
		std::cerr << "got:\n"
				  << Output.str();
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

{
	const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-stdlib-io-test";
	std::error_code RemoveError;
	std::filesystem::remove_all(TempRoot, RemoveError);
	std::filesystem::create_directories(TempRoot);

	const std::filesystem::path DataFile = TempRoot / "data.txt";
	{
		std::ofstream MainFile(TempRoot / "main.vs");
		MainFile
				<< "import \"io.vs\";\n"
				<< "void main() {\n"
				<< "    std::io::write_file(\"" << DataFile.generic_string() << "\", \"hello\");\n"
				<< "    print(std::filesystem::exists(\"" << DataFile.generic_string() << "\"));\n"
				<< "    print(std::io::read_file(\"" << DataFile.generic_string() << "\"));\n"
				<< "    print(std::filesystem::delete_file(\"" << DataFile.generic_string() << "\"));\n"
				<< "    print(std::filesystem::exists(\"" << DataFile.generic_string() << "\"));\n"
				<< "}\n";
	}

	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").string(),
			Output,
			Errors);
	std::filesystem::remove_all(TempRoot, RemoveError);
	if (!Expect(Result.bSuccess, "stdlib io.vs should run"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
	if (!Expect(Output.str() == "true\nhello\ntrue\nfalse\n", "stdlib io.vs read/write/delete"))
	{
		std::cerr << "got:\n"
				  << Output.str();
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

{
	const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-stdlib-fs-types-test";
	std::error_code RemoveError;
	std::filesystem::remove_all(TempRoot, RemoveError);
	const std::filesystem::path EmptyDir = TempRoot / "empty";
	const std::filesystem::path NonEmptyDir = TempRoot / "nonempty";
	const std::filesystem::path DataFile = TempRoot / "data.txt";
	std::filesystem::create_directories(EmptyDir);
	std::filesystem::create_directories(NonEmptyDir);
	std::ofstream(DataFile) << "data";
	std::ofstream(NonEmptyDir / "child.txt") << "child";
	const std::string Source =
			"import \"io.vs\"; void main() {"
			"assert(std::filesystem::is_file(\"" +
		DataFile.generic_string() + "\"));"
			"assert(!std::filesystem::is_directory(\"" +
		DataFile.generic_string() + "\"));"
			"assert(!std::filesystem::is_file(\"" +
		(DataFile / "child").generic_string() + "\"));"
			"assert(!std::filesystem::is_directory(\"" +
		(DataFile / "child").generic_string() + "\"));"
			"assert(std::filesystem::is_directory(\"" +
		EmptyDir.generic_string() + "\"));"
			"assert(!std::filesystem::is_file(\"" +
		EmptyDir.generic_string() + "\"));"
			"assert(!std::filesystem::is_file(\"" +
		(TempRoot / "missing").generic_string() + "\"));"
			"assert(!std::filesystem::is_directory(\"" +
		(TempRoot / "missing").generic_string() + "\"));"
			"assert(std::filesystem::remove_dir(\"" +
		EmptyDir.generic_string() + "\"));"
			"assert(!std::filesystem::remove_dir(\"" +
		EmptyDir.generic_string() + "\"));"
			"}";
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source, "<filesystem-types>", Output, Errors, VeroScript::ERunMode::Script);
	const bool bEmptyRemoved = !std::filesystem::exists(EmptyDir);
	if (!Expect(Result.bSuccess && bEmptyRemoved, "filesystem type queries and empty removal"))
	{
		std::cerr << Errors.str();
		std::filesystem::remove_all(TempRoot, RemoveError);
		return EXIT_FAILURE;
	}
	std::ostringstream FailureOutput;
	std::ostringstream FailureErrors;
	const std::string FailureSource =
			"import \"io.vs\"; void main() { std::filesystem::remove_dir(\""
			+ NonEmptyDir.generic_string() + "\"); }";
	const VeroScript::FRunResult Failure = VeroScript::Test::FEngine::RunSource(
			FailureSource, "<remove-nonempty-dir>", FailureOutput, FailureErrors,
			VeroScript::ERunMode::Script);
	const bool bNonEmptySurvived = std::filesystem::exists(NonEmptyDir / "child.txt");
	std::ostringstream FileFailureOutput;
	std::ostringstream FileFailureErrors;
	const VeroScript::FRunResult FileFailure =
			VeroScript::Test::FEngine::RunSource(
				"import \"io.vs\"; void main() { std::filesystem::remove_dir(\""
					+ DataFile.generic_string() + "\"); }",
				"<remove-file-as-dir>", FileFailureOutput, FileFailureErrors,
				VeroScript::ERunMode::Script);
	const bool bFileSurvived = std::filesystem::is_regular_file(DataFile);
	std::filesystem::remove_all(TempRoot, RemoveError);
	if (!Expect(!Failure.bSuccess && bNonEmptySurvived
				&& Failure.Error.MessageId == "diag.cannot_remove_directory"
				&& !FileFailure.bSuccess && bFileSurvived
				&& FileFailure.Error.MessageId == "diag.remove_dir_requires_directory",
			"remove_dir rejects files and nonempty directories"))
	{
		std::cerr << FailureErrors.str();
		std::cerr << FileFailureErrors.str();
		return EXIT_FAILURE;
	}
}

{
	const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-stdlib-fs-links-test";
	std::error_code FsError;
	std::filesystem::remove_all(TempRoot, FsError);
	std::filesystem::create_directories(TempRoot / "folder");
	std::ofstream(TempRoot / "file.txt") << "data";
	const std::filesystem::path FileLink = TempRoot / "file-link";
	const std::filesystem::path DirLink = TempRoot / "dir-link";
	const std::filesystem::path DanglingLink = TempRoot / "dangling-link";
	std::filesystem::create_symlink(TempRoot / "file.txt", FileLink, FsError);
	const bool bFileLinkSupported = !FsError;
	FsError.clear();
	std::filesystem::create_directory_symlink(TempRoot / "folder", DirLink, FsError);
	const bool bDirLinkSupported = !FsError;
	FsError.clear();
	std::filesystem::create_symlink(TempRoot / "missing", DanglingLink, FsError);
	const bool bDanglingLinkSupported = !FsError;
	if (bFileLinkSupported || bDirLinkSupported || bDanglingLinkSupported)
	{
		std::string Source = "import \"io.vs\"; void main() {";
		if (bFileLinkSupported)
		{
			Source += "assert(std::filesystem::is_file(\""
					+ FileLink.generic_string() + "\"));";
		}
		if (bDirLinkSupported)
		{
			Source += "assert(std::filesystem::is_directory(\""
					+ DirLink.generic_string() + "\"));";
		}
		if (bDanglingLinkSupported)
		{
			Source += "assert(!std::filesystem::is_file(\""
					+ DanglingLink.generic_string() + "\"));";
			Source += "assert(!std::filesystem::is_directory(\""
					+ DanglingLink.generic_string() + "\"));";
		}
		Source += "}";
		std::ostringstream Output;
		std::ostringstream Errors;
		const auto Result = VeroScript::Test::FEngine::RunSource(
				Source, "<filesystem-links>", Output, Errors, VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "filesystem queries follow symbolic links"))
		{
			std::cerr << Errors.str();
			std::filesystem::remove_all(TempRoot, FsError);
			return EXIT_FAILURE;
		}
	}
	if (bDirLinkSupported)
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const auto Result = VeroScript::Test::FEngine::RunSource(
				"import \"io.vs\"; void main() { std::filesystem::remove_dir(\""
					+ DirLink.generic_string() + "\"); }",
				"<remove-dir-link>", Output, Errors, VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess && std::filesystem::is_symlink(DirLink),
				"remove_dir rejects directory symbolic links"))
		{
			std::cerr << Errors.str();
			std::filesystem::remove_all(TempRoot, FsError);
			return EXIT_FAILURE;
		}
	}
	std::filesystem::remove_all(TempRoot, FsError);
}

{
	if (!ExpectOutput(
				R"Vero(import "path.vs";
import "string.vs";
void main() {
    assert(std::filesystem::join_path("", "a") == "a");
    assert(std::filesystem::join_path("a", "") == "a");
    assert(std::filesystem::join_path("a", "/b") == "/b");
    assert(std::filesystem::join_path("a", "\\b") == "\\b");
    assert(std::filesystem::join_path("a", "C:/b") == "C:/b");
    assert(std::filesystem::join_path("a", "c:b") == "c:b");
    assert(std::filesystem::join_path("a", "foo:bar") == "a/foo:bar");
    assert(std::filesystem::join_path("C:", "b") == "C:b");
    assert(std::filesystem::join_path("a/", "b") == "a/b");
    assert(std::strings::trim("\t\n\v\f\r x \t\n\v\f\r") == "x");
    assert(std::strings::trim("x\v y") == "x\v y");
    print("ok");
}
)Vero",
				VeroScript::ERunMode::Script,
				"ok\n",
				"stdlib path and ASCII trim boundary cases"))
	{
		return EXIT_FAILURE;
	}
}

{
	if (!ExpectOutput(
				R"Vero(import "algorithm.vs";
class OrderedValue {
public:
    int key;
    OrderedValue(int value) { key = value; }
    bool operator<(OrderedValue other) { return key < other.key; }
}
void main() {
    array<OrderedValue> values = {OrderedValue(1), OrderedValue(3)};
    assert(std::algorithm::binary_search(values, OrderedValue(3)));
    assert(std::algorithm::is_sorted(values));
    assert(!std::algorithm::binary_search(values, OrderedValue(2)));
    print("ok");
}
)Vero",
				VeroScript::ERunMode::Script,
				"ok\n",
				"ordered algorithms require only operator<"))
	{
		return EXIT_FAILURE;
	}
}

{
	const std::filesystem::path Directory =
			std::filesystem::temp_directory_path() / "VeroScript-delete-file-directory-test";
	std::error_code RemoveError;
	std::filesystem::remove_all(Directory, RemoveError);
	std::filesystem::create_directories(Directory);
	std::ostringstream Output;
	std::ostringstream Errors;
	const std::string Source =
			"import \"io.vs\"; void main() { std::filesystem::delete_file(\""
			+ Directory.generic_string() + "\"); }";
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source, "<delete-file-directory>", Output, Errors, VeroScript::ERunMode::Script);
	const bool bDirectorySurvived = std::filesystem::is_directory(Directory);
	std::filesystem::remove_all(Directory, RemoveError);
	if (!Expect(!Result.bSuccess && bDirectorySurvived
				&& Errors.str().find("delete_file cannot delete a directory") != std::string::npos,
			"delete_file must reject directories"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

{
	const std::string NonAscii = "\xC3\x89"; // UTF-8 capital E with acute.
	const std::string Source =
			"import \"string.vs\"; void main() {"
			"assert(std::strings::to_lower(\"" +
		NonAscii + "A\") == \""
			+ NonAscii + "a\");"
			"assert(std::strings::to_upper(\"" +
		NonAscii + "a\") == \""
			+ NonAscii + "A\"); }";
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			Source, "<ascii-case-conversion>", Output, Errors, VeroScript::ERunMode::Script);
	if (!Expect(Result.bSuccess, "ASCII case conversion preserves UTF-8 bytes"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

{
	const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-stdlib-string-test";
	std::error_code RemoveError;
	std::filesystem::remove_all(TempRoot, RemoveError);
	std::filesystem::create_directories(TempRoot);
	{
		std::ofstream MainFile(TempRoot / "main.vs");
		MainFile
				<< "import \"string.vs\";\n"
				<< "void main() {\n"
				<< "    print(std::strings::substring(\"hello\", 1, 3));\n"
				<< "    print(std::strings::find(\"hello\", \"ll\"));\n"
				<< "    print(std::strings::contains(\"hello\", \"lo\"));\n"
				<< "    print(std::strings::starts_with(\"hello\", \"he\"));\n"
				<< "    print(std::strings::ends_with(\"hello\", \"lo\"));\n"
				<< "    print(std::strings::trim(\"  hi  \"));\n"
				<< "    array<string> parts = std::strings::split(\"a,b,c\", \",\");\n"
				<< "    print(len(parts));\n"
				<< "    print(parts[1]);\n"
				<< "    print(std::strings::join(parts, \"-\"));\n"
				<< "    print(std::strings::last_find(\"ababa\", \"ba\"));\n"
				<< "    print(std::strings::last_find(\"aaa\", \"aa\"));\n"
				<< "    print(std::strings::repeat(\"xy\", 3));\n"
				<< "    assert(std::strings::repeat(\"\", 9223372036854775807i64) == \"\", \"wide empty repeat\");\n"
				<< "    print(std::strings::remove_prefix(\"prefix-value\", \"prefix-\"));\n"
				<< "    print(std::strings::remove_suffix(\"value.txt\", \".txt\"));\n"
				<< "}\n";
	}

	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").string(),
			Output,
			Errors);
	std::filesystem::remove_all(TempRoot, RemoveError);
	if (!Expect(Result.bSuccess, "stdlib string.vs should run"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
	if (!Expect(
				Output.str() == "ell\n2\ntrue\ntrue\ntrue\nhi\n3\nb\na-b-c\n3\n1\nxyxyxy\nvalue\nvalue\n",
				"stdlib string.vs"))
	{
		std::cerr << "got:\n"
				  << Output.str();
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

{
	const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-stdlib-path-test";
	std::error_code RemoveError;
	std::filesystem::remove_all(TempRoot, RemoveError);
	std::filesystem::create_directories(TempRoot);
	{
		std::ofstream MainFile(TempRoot / "main.vs");
		MainFile
				<< "import \"path.vs\";\n"
				<< "void main() {\n"
				<< "    print(std::filesystem::basename(\"a/b/c.txt\"));\n"
				<< "    print(std::filesystem::dirname(\"a/b/c.txt\"));\n"
				<< "    print(std::filesystem::join_path(\"a/b\", \"c.txt\"));\n"
				<< "    print(std::filesystem::basename(\"c.txt\"));\n"
				<< "    print(std::filesystem::dirname(\"c.txt\"));\n"
				<< "    print(std::filesystem::join_path(\"a/\", \"b\"));\n"
				<< "}\n";
	}

	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").string(),
			Output,
			Errors);
	std::filesystem::remove_all(TempRoot, RemoveError);
	if (!Expect(Result.bSuccess, "stdlib path.vs should run"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
	if (!Expect(
				Output.str() == "c.txt\na/b\na/b/c.txt\nc.txt\n.\na/b\n",
				"stdlib path.vs"))
	{
		std::cerr << "got:\n"
				  << Output.str();
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

{
	const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-stdlib-core-test";
	std::error_code RemoveError;
	std::filesystem::remove_all(TempRoot, RemoveError);
	std::filesystem::create_directories(TempRoot);
	{
		std::ofstream MainFile(TempRoot / "main.vs");
		MainFile
				<< "import \"core.vs\";\n"
				<< "void main() {\n"
				<< "    print(abs(-2));\n"
				<< "    print(std::strings::contains(\"abc\", \"b\"));\n"
				<< "    print(std::filesystem::basename(\"a/b.txt\"));\n"
				<< "    array<int> values = {9, 2, 7, 2, 5};\n"
				<< "    std::algorithm::sort(values);\n"
				<< "    print(values[0]);\n"
				<< "    print(std::algorithm::binary_search(values, 7));\n"
				<< "    print(std::numeric::sum(values));\n"
				<< "    print(std::numeric::accumulate(values, 10));\n"
				<< "    print(std::numeric::average(values));\n"
				<< "    array<int> empty = array<int>();\n"
				<< "    std::algorithm::sort(empty);\n"
				<< "    std::algorithm::reverse(empty);\n"
				<< "    print(std::numeric::accumulate(empty, 4));\n"
				<< "    std::algorithm::reverse(values);\n"
				<< "    print(values[0]);\n"
				<< "    print(std::algorithm::find_index(values, 6));\n"
				<< "    std::algorithm::sort(values);\n"
				<< "    print(std::algorithm::lower_bound(values, 1));\n"
				<< "    print(std::algorithm::lower_bound(values, 10));\n"
				<< "    array<unsigned int> first = std::random::make_engine(123u);\n"
				<< "    array<unsigned int> second = std::random::make_engine(123u);\n"
				<< "    print(std::random::next_uint(first) == std::random::next_uint(second));\n"
				<< "    std::random::seed(first, 123u);\n"
				<< "    std::random::seed(second, 123u);\n"
				<< "    print(std::random::next_uint(first) == std::random::next_uint(second));\n"
				<< "}\n";
	}

	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").string(),
			Output,
			Errors);
	std::filesystem::remove_all(TempRoot, RemoveError);
	if (!Expect(Result.bSuccess, "stdlib core.vs should run"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
	if (!Expect(
			Output.str() == "2\ntrue\nb.txt\n2\ntrue\n25\n35\n5\n4\n9\n-1\n0\n5\ntrue\ntrue\n",
			"stdlib core.vs aggregate import"))
	{
		std::cerr << "got:\n"
				  << Output.str();
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

if (!ExpectOutput(
			"import \"math.vs\";\n"
			"void main() { print(abs(-4)); }\n",
			VeroScript::ERunMode::Script,
			"4\n",
			"RunSource resolves stdlib imports"))
{
	return EXIT_FAILURE;
}

if (!ExpectOutput(
			"import \"math.vs\"\n"
			"void main() { print(abs(-5)); }\n",
			VeroScript::ERunMode::Script,
			"5\n",
			"import without trailing semicolon"))
{
	return EXIT_FAILURE;
}

{
	const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-export-import-no-semi-test";
	std::error_code RemoveError;
	std::filesystem::remove_all(TempRoot, RemoveError);
	std::filesystem::create_directories(TempRoot);
	{
		std::ofstream(TempRoot / "lib.vs") << "export int answer() { return 11; }\n";
		std::ofstream(TempRoot / "facade.vs") << "export import \"lib.vs\"\n";
		std::ofstream(TempRoot / "main.vs")
				<< "import \"facade.vs\"\n"
				<< "void main() { print(answer()); }\n";
	}
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").generic_string(),
			Output,
			Errors);
	std::filesystem::remove_all(TempRoot, RemoveError);
	if (!Expect(Result.bSuccess && Output.str() == "11\n", "export import without semicolon"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

{
	const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-nested-import-reject-test";
	std::error_code RemoveError;
	std::filesystem::remove_all(TempRoot, RemoveError);
	std::filesystem::create_directories(TempRoot);
	{
		std::ofstream(TempRoot / "lib.vs") << "export int answer() { return 1; }\n";
		std::ofstream(TempRoot / "main.vs")
				<< "void main() {\n"
				<< "  import \"lib.vs\";\n"
				<< "  print(1);\n"
				<< "}\n";
	}
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").generic_string(),
			Output,
			Errors);
	std::filesystem::remove_all(TempRoot, RemoveError);
	if (!Expect(!Result.bSuccess, "nested import should fail parse (not preload)"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
	if (!Expect(
				Errors.str().find("cannot resolve import") == std::string::npos
					&& Errors.str().find("cannot open import") == std::string::npos,
				"nested import must not surface as import resolve failure"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

{
	const std::filesystem::path TempRoot =
			std::filesystem::temp_directory_path() / "VeroScript-dot-import-dedupe-test";
	std::error_code RemoveError;
	std::filesystem::remove_all(TempRoot, RemoveError);
	std::filesystem::create_directories(TempRoot);
	{
		std::ofstream(TempRoot / "lib.vs") << "export int answer() { return 9; }\n";
		std::ofstream(TempRoot / "main.vs")
				<< "import \"lib.vs\";\n"
				<< "import \"./lib.vs\";\n"
				<< "void main() { print(answer()); }\n";
	}
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunFile(
			(TempRoot / "main.vs").generic_string(),
			Output,
			Errors);
	std::filesystem::remove_all(TempRoot, RemoveError);
	if (!Expect(Result.bSuccess && Output.str() == "9\n", "import ./lib.vs dedupes with lib.vs"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

if (!ExpectOutput(
			"struct S { int x = 0; }\n"
			"int id(S s) { return s.x; }\n"
			"void main() {\n"
			"  auto take = id;\n"
			"  S s = S();\n"
			"  s.x = 7;\n"
			"  print(take(s));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"7\n",
			"fn(private S) qualifies for assignability"))
{
	return EXIT_FAILURE;
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"auto oops() { return 1; }\n"
			"void main() {}\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "reject auto function return type"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
	if (!Expect(
				Errors.str().find("auto") != std::string::npos,
				"auto return diagnostic mentions auto"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void sink(auto value) {}\n"
			"void main() { sink(1); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "reject auto function parameter type"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { array<auto> a = array<auto>(); }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "reject array<auto>"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

{
	std::ostringstream Output;
	std::ostringstream Errors;
	const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { fn(auto)->int f = fn(int x) -> int => x; }\n",
			"<test>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
	if (!Expect(!Result.bSuccess, "reject fn(auto)->int"))
	{
		std::cerr << Errors.str();
		return EXIT_FAILURE;
	}
}

if (!ExpectOutput(
			"void main() {\n"
			"    print(to_string(abs(-3.5)));\n"
			"    print(min(2, 7));\n"
			"    print(to_string(max(2.5, 7.0)));\n"
			"}\n",
			VeroScript::ERunMode::Script,
			"3.5\n2\n7\n",
			"global abs/min/max numeric dispatch"))
{
	return EXIT_FAILURE;
}
