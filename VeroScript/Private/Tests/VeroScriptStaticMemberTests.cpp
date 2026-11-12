// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"

#include <iostream>
#include <sstream>
#include <string>

namespace
{
	bool Expect(const bool Condition, const char* Message)
	{
		if (!Condition)
		{
			std::cerr << "FAIL: " << Message << '\n';
		}
		return Condition;
	}

	std::unique_ptr<VeroScript::FBytecodeModule> CompileSource(const std::string& Source)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "static_member.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			std::cerr << "lex error\n";
			return nullptr;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "static_member.vs");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		if (!Program || Parser.HasError())
		{
			std::cerr << "parse: " << Parser.GetError().Message << '\n';
			return nullptr;
		}
		VeroScript::VeroScriptCompiler Compiler;
		auto Module = Compiler.Compile(*Program);
		if (!Module || Compiler.HasError())
		{
			std::cerr << "compile: " << Compiler.GetError().Message << '\n';
			return nullptr;
		}
		return Module;
	}

	bool RunSource(const std::string& Source, const std::string& ExpectedOutput, const char* Label)
	{
		std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
		if (!Expect(static_cast<bool>(Module), Label))
		{
			return false;
		}

		std::ostringstream Output;
		std::istringstream Input;
		std::ostringstream Errors;
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		VeroScript::Test::Configure(Vm);
		if (!Expect(Vm.Execute(*Module, VeroScript::ERunMode::Script), Label))
		{
			std::cerr << Vm.GetError().Message << '\n';
			return false;
		}
		if (!Expect(Output.str() == ExpectedOutput, Label))
		{
			std::cerr << "expected:\n"
					  << ExpectedOutput << "got:\n"
					  << Output.str() << '\n';
			return false;
		}
		return true;
	}

	bool CompileFails(const std::string& Source, const char* Label, const char* Needle = nullptr)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "static_member.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			const std::string Err = Lexer.GetErrorMessage();
			if (Needle && Err.find(Needle) == std::string::npos)
			{
				std::cerr << "FAIL: " << Label << " (lex needle)\n"
						  << Err << '\n';
				return false;
			}
			return true;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "static_member.vs");
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		if (!Program || Parser.HasError())
		{
			const std::string Err = Parser.GetError().Message;
			if (Needle && Err.find(Needle) == std::string::npos)
			{
				std::cerr << "FAIL: " << Label << " (parse needle)\n"
						  << Err << '\n';
				return false;
			}
			return true;
		}
		VeroScript::VeroScriptCompiler Compiler;
		auto Module = Compiler.Compile(*Program);
		if (!Module || Compiler.HasError())
		{
			const std::string Err = Compiler.GetError().Message;
			if (Needle && Err.find(Needle) == std::string::npos)
			{
				std::cerr << "FAIL: " << Label << " (compile needle)\n"
						  << Err << '\n';
				return false;
			}
			return true;
		}
		std::cerr << "FAIL: " << Label << " (expected failure)\n";
		return false;
	}
}

int main()
{
	int Failures = 0;
	for (const std::string OwnerKind : {"class", "struct"})
	{
		for (const std::string ValueKind : {"class", "struct"})
		{
			const std::string Source = ValueKind + R"vs( Item { public:
	int number = 7;
	static int shared = 11;
	int Read() const { return number; }
	void Bump() { number++; }
	static int ReadShared() { return shared; }
};
)vs" + OwnerKind + R"vs( Cache<T> { public:
	static Item value = Item();
	static Item Make() { return Item(); }
};
int Pick(int value) { return value; }
int Pick(string value) { return -1; }
void main() {
	print(Cache<int>::value.Read());
	auto read = Cache<int>::value.Read();
	print(Pick(read));
	Cache<int>::value.Bump();
	int& number = Cache<int>::value.number;
	number += 1;
	print(Cache<int>::value.Read());
	print(Cache<long>::value.Read());
	print(Cache<int>::value.shared);
	Cache<int>::value.shared = 13;
	print(Cache<long>::value.ReadShared());
	print(Cache<int>::Make().Read());
	Cache<int> instance = Cache<int>();
	print(instance.value.Read());
}
)vs";
			if (!RunSource(Source, "7\n7\n9\n7\n11\n13\n7\n9\n",
			        "generic static field values preserve instance dispatch, inference and storage identity"))
			{
				++Failures;
			}
		}
	}
	if (!RunSource(R"vs(
namespace N {
	class Item<T> { public: T number = 7; T Read() const { return number; } };
	class Cache<T> { public: static T value = T(); };
}
using Store = N::Cache<N::Item<int>>;
void main() {
	print(N::Cache<N::Item<int>>::value.Read());
	auto value = Store::value.Read();
	print(value);
}
)vs", "7\n7\n", "namespaced nested generic arguments and aliases preserve static field receiver types"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Item { public: int Read() { return 7; } };
class Outer<T> { public:
	class Inner { public:
		static Item value = Item();
		static Item Make() { return Item(); }
	};
};
void main() {
	print(Outer<int>::Inner::Make().Read());
	print(Outer<int>::Inner::value.Read());
}
)vs", "7\n7\n", "nested types in generic owners remain type receivers before accessing their static values"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Cache<T> { public: static array<int> values = {7, 8, 9}; };
void main() {
	Cache<int>::values.reserve(16);
	assert(Cache<int>::values.capacity() >= 16);
	Cache<int>::values.remove_at(0);
	print(Cache<int>::values[0]);
	print(len(Cache<long>::values));
}
)vs", "8\n3\n", "generic static container fields keep builtin dispatch and specialization storage"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Order { public: static int made = 0; };
class Item { public: Item() { Order::made++; } int Read() { return 7; } };
class Cache<T> { public: static Item value = Item(); };
class State { public: static int result = Cache<int>::value.Read(); };
void main() { print(State::result); print(Order::made); }
)vs", "7\n1\n", "generic static field method receivers retain initialization order and evaluate once"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Item { public: int Read() const { return 7; } };
class Cache<T> { public: static const Item value = Item(); };
void main() { print(Cache<int>::value.Read()); }
)vs", "7\n", "generic const static field receivers retain const-qualified method dispatch"))
	{
		++Failures;
	}
	if (!CompileFails(R"vs(
class Item { public: int number = 7; void Bump() { number++; } };
class Cache<T> { public: static const Item value = Item(); };
void main() { Cache<int>::value.Bump(); }
)vs", "generic static field receivers retain const method checks"))
	{
		++Failures;
	}
	if (!CompileFails(R"vs(
class Item { int Read() { return 7; } };
class Cache<T> { public: static Item value = Item(); };
void main() { print(Cache<int>::value.Read()); }
)vs", "generic static field receivers retain method access checks", "cannot access"))
	{
		++Failures;
	}
	if (!CompileFails(R"vs(
class Item { public: int Read() { return 7; } };
class Cache<T> { static Item value = Item(); };
void main() { print(Cache<int>::value.Read()); }
)vs", "generic static field receivers retain field access checks", "cannot access"))
	{
		++Failures;
	}
	if (!CompileFails(R"vs(
class Item { public: int Read(int value) { return value; } };
class Cache<T> { public: static Item value = Item(); };
void main() { print(Cache<int>::value.Read("wrong")); }
)vs", "generic static field receivers retain argument type checks"))
	{
		++Failures;
	}
	for (const std::string Kind : {"class", "struct"})
	{
		const std::string Source = R"vs(
class Order { public: static int trace = 0; static int observed = -1; };
int Mark(int value) { Order::trace = Order::trace * 10 + value; return value; }
class Needed<T> { public: static int seed = Mark(1) + 6; };
struct Item { ~Item() { Order::observed = Needed<int>::seed; Mark(2); } };
)vs" + Kind + R"vs( Wrapper { public: Item values[1]; };
int work() { Wrapper value = Wrapper(); return 0; }
class State { public: static int value = work(); static int later = Mark(3); };
void main() { print(Order::observed); print(Order::trace); }
)vs";
		if (!RunSource(Source, "7\n123\n", "wrapper array-field destruction prepares static dependencies"))
		{
			++Failures;
		}
	}
	for (const bool bRetained : {false, true})
	{
		const std::string Source = R"vs(
class Order { public: static int trace = 0; static int observed = -1; };
int Mark(int value) { Order::trace = Order::trace * 10 + value; return value; }
class Needed<T> { public: static int seed = Mark(1) + 6; };
struct Item { ~Item() { Order::observed = Needed<int>::seed; Mark(2); } };
class Inner { public: Item values[1]; };
)vs" + std::string(bRetained ? R"vs(
Inner make() { return Inner(); }
class State { public: static Inner value = make(); static int later = Mark(3); };
)vs" : R"vs(
class Wrapper { public: Inner nullable; };
int work() { Wrapper value = Wrapper(); return 0; }
class State { public: static int value = work(); static int later = Mark(3); };
)vs") + R"vs(
void main() { print(Order::observed); print(Order::trace); }
)vs";
		if (!RunSource(Source, "-1\n31\n", "retained and nullable array owners defer element destruction dependencies"))
		{
			++Failures;
		}
	}
	if (!RunSource(R"vs(
class Needed<T> { public: static int seed = 7; };
int zero() { return 0; }
int needed() { return Needed<int>::seed; }
class Config { public: static fn()->int read = zero; };
fn()->int current() { return Config::read; }
void selectNeeded() { Config::read = needed; }
int work() { auto first = current(); selectNeeded(); auto read = current(); return read(); }
class State { public: static int value = work(); };
void main() { print(State::value); print(Needed<int>::seed); }
)vs", "7\n7\n", "returned callable dependencies refresh after a global write"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Needed<T> { public: static int seed = 7; };
int zero() { return 0; }
int needed() { return Needed<int>::seed; }
class Config { public: static fn()->int read = zero; };
void selectNeeded() { Config::read = needed; }
void selectZero() { Config::read = zero; }
int work() { selectNeeded(); selectZero(); selectNeeded(); auto read = Config::read; return read(); }
class State { public: static int value = work(); };
void main() { print(State::value); print(Needed<int>::seed); }
)vs", "7\n7\n", "repeated stateful calls retain their callable writes"))
	{
		++Failures;
	}
	for (const bool bGlobal : {false, true})
	{
		const std::string Source = R"vs(
using Callback = fn()->int;
class Needed<T> { public: static int seed = 7; };
int zero() { return 0; }
int needed() { return Needed<int>::seed; }
class Config { public: static Callback read = zero; };
void selectNeeded(Callback& callback) { callback = needed; }
void selectZeroByValue(Callback callback) { callback = zero; }
)vs" + std::string(bGlobal ?
			"int work() { auto before = Config::read; selectNeeded(ref Config::read); auto read = Config::read; return read(); }" :
			"int work() { Callback read = zero; Callback& alias = read; selectNeeded(ref alias); selectZeroByValue(read); return read(); }") + R"vs(
class State { public: static int value = work(); };
void main() { print(State::value); print(Needed<int>::seed); }
)vs";
		if (!RunSource(Source, "7\n7\n", "reference parameter writes refresh caller callable dependencies"))
		{
			++Failures;
		}
	}
	if (!RunSource(R"vs(
class Order { public: static int observed = -1; };
class Needed<T> { public: static int seed = 7; };
int zero() { return 0; }
int needed() { return Needed<int>::seed; }
class Temporary { public: ~Temporary() { auto read = State::callback; Order::observed = read(); } };
void use() { Temporary item = Temporary(); }
int work() { use(); assert(Order::observed == 0); State::callback = needed; use(); return Order::observed; }
class State { public: static fn()->int callback = zero; static int value = work(); };
void main() { print(State::value); print(Needed<int>::seed); }
)vs", "7\n7\n", "repeated destructor dependencies refresh after a global callable write"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Needed<T> { public: static int seed = 7; };
int work()
{
	fn(int)->int recurse = [](int value)->int { return value; };
	recurse = [&recurse](int value)->int { if (value == 0) return Needed<int>::seed; return recurse(value - 1); };
	return recurse(3);
}
class State { public: static int value = work(); };
void main() { print(State::value); }
)vs", "7\n", "recursive callable references remain alive throughout static dependency analysis"))
	{
		++Failures;
	}
	{
		auto Module = CompileSource(R"vs(
class Value<T> { public: static int seed = 7; };
class State { public: Value<int> early = nullptr; static int result = Value<int>::seed; };
void main() { print(State::result); }
)vs");
		bool bSeparateRanges = Module != nullptr;
		if (Module)
		{
			for (std::size_t Left = 0; Left < Module->GlobalInitRanges.size(); ++Left)
			{
				for (std::size_t Right = Left + 1; Right < Module->GlobalInitRanges.size(); ++Right)
				{
					const auto& A = Module->GlobalInitRanges[Left];
					const auto& B = Module->GlobalInitRanges[Right];
					bSeparateRanges = bSeparateRanges &&
						(A.EndOffset <= B.BeginOffset || B.EndOffset <= A.BeginOffset);
				}
			}
		}
		if (!Expect(bSeparateRanges, "dependency globals retain disjoint initialization ranges for reload"))
		{
			++Failures;
		}
	}
	if (!RunSource(R"vs(
class Value<T> { public: static int seed = 7; };
int Read() { return Value<int>::seed; }
int ReadWide() { return Value<int64>::seed; }
fn()->int Make() { return []()->int { return Value<string>::seed; }; }
int UseStored() { auto callback = State::callback; return callback(); }
int UseReturned() { auto callback = Make(); return callback(); }
class State
{
public:
	static fn()->int callback = Read;
	static int first = UseStored();
	static int second = UseReturned();
	static int third = [callback = ReadWide](int add)->int { return callback() + add; }(2);
};
void main() { print(State::first); print(State::second); print(State::third); }
)vs", "7\n7\n9\n", "executed stored, returned and captured callables retain static dependencies"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Order { public: static int trace = 0; static int observed = -1; };
int Mark(int value) { Order::trace = Order::trace * 10 + value; return value; }
class Needed<T> { public: static int seed = Mark(1) + 6; };
struct Local { ~Local() { Order::observed = Needed<int>::seed; Mark(2); } };
int work() { Local value = Local(); return 0; }
class State { public: static int value = work(); static int later = Mark(3); };
void main() { print(Order::observed); print(Order::trace); }
)vs", "7\n123\n", "static calls prepare dependencies of locally released destructors"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Order { public: static int trace = 0; static int observed = -1; };
int Mark(int value) { Order::trace = Order::trace * 10 + value; return value; }
class Needed<T> { public: static int seed = Mark(1) + 6; };
class Retained { public: ~Retained() { Order::observed = Needed<int>::seed; Mark(2); } };
class State { public: static Retained value = Retained(); static int later = Mark(3); };
void main() { print(Order::observed); print(Order::trace); }
)vs", "-1\n31\n", "retained global objects keep their destructor dependencies deferred"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Needed<T> { public: static int seed = 7; };
struct Item
{
	int value = 0;
	Item() = default;
	Item(const Item& other) { value = Needed<int>::seed; }
};
int work() { Item values[1]; Item copied = values[0]; return copied.value; }
class State { public: static int value = work(); };
void main() { print(State::value); print(Needed<int>::seed); }
)vs", "7\n7\n", "static calls prepare the selected array-element copy constructor"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Order { public: static int trace = 0; };
int Mark(int value) { Order::trace = Order::trace * 10 + value; return value; }
class Needed<T> { public: static int seed = Mark(1); };
int work() { return 0; return Needed<int>::seed; }
class State { public: static int value = work(); static int later = Mark(3); };
void main() { print(State::value); print(Order::trace); }
)vs", "0\n31\n", "unreachable return paths do not introduce static dependencies"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Order { public: static int trace = 0; };
int Mark(int value) { Order::trace = Order::trace * 10 + value; return value; }
class Needed<T> { public: static int seed = Mark(1); };
int work() { goto done; return Needed<int>::seed; done: return 0; }
class State { public: static int value = work(); static int later = Mark(3); };
void main() { print(State::value); print(Order::trace); }
)vs", "0\n31\n", "unconditional jumps exclude unreachable dependency paths"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class State { public: static array<int> values = {1, 2}; };
void main() { print(State::values[1]); }
)vs", "2\n", "static initializer temporaries retain their module init local slots"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Value<T> { public: static int seed = 7; int copy = Value<T>::seed; };
class Nested { public: Value<int> item = Value<int>(); };
class State { public: static Nested now = Nested(); };
void main() { print(State::now.item.copy); print(Value<int>::seed); }
)vs", "7\n7\n", "ordinary wrapper construction prepares nested generic field defaults"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
struct Value<T> { static int seed = 7; int copy = Value<T>::read(); static int read() { return seed; } };
struct Nested<T> { Value<T> item = Value<T>(); };
class State { public: Nested<int> early = Nested<int>(); static Nested<int> now = Nested<int>(); };
void main() { print(State::now.item.copy); print(Value<int>::seed); }
)vs", "7\n7\n", "generic wrapper construction prepares nested struct method dependencies"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Value<T> { public: static int seed = 7; };
class Nested { public: int copy; Nested() { copy = Value<int>::seed; } };
class State { public: static Nested now = Nested(); };
void main() { print(State::now.copy); print(Value<int>::seed); }
)vs", "7\n7\n", "static construction prepares its selected constructor body"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Order { public: static int trace = 0; };
int Mark(int value) { Order::trace = Order::trace * 10 + value; return value; }
class Unrelated<T> { public: static int seed = Mark(2); };
class Needed<T> { public: static int seed = Mark(1); };
class Nested
{
public:
	int copy;
	Nested(int add) { copy = Needed<int>::seed + add; }
	Nested(string text) { copy = Unrelated<int>::seed; }
};
class State { public: static Nested now = Nested(5); static int later = Mark(3); };
void main() { print(State::now.copy); print(Order::trace); }
)vs", "6\n132\n", "only the selected constructor contributes early static dependencies"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Order { public: static int trace = 0; };
int Mark(int value) { Order::trace = Order::trace * 10 + value; return value; }
class Unrelated<T> { public: static int seed = Mark(2); };
class Needed<T> { public: static int seed = Mark(1); };
class Nested { public: int copy = Needed<int>::seed; };
class State
{
public:
	Unrelated<int> early = nullptr;
	static Nested first = Nested();
	static Nested second = Nested();
	static int later = Mark(3);
};
void main() { print(State::first.copy); print(State::second.copy); print(Order::trace); }
)vs", "1\n1\n132\n", "static initialization never becomes part of a per-instance field default"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Value<T> { public: static int seed = 7; };
class State { public: static int copy = []() -> int { return Value<int>::seed; }(); };
void main() { print(State::copy); print(Value<int>::seed); }
)vs", "7\n7\n", "immediately invoked closures prepare their executing static dependencies"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Order { public: static int trace = 0; };
int Mark(int value) { Order::trace = Order::trace * 10 + value; return value; }
class Needed<T> { public: static int seed = Mark(1); };
class Nested { public: Needed<int> item = nullptr; };
class State { public: static Nested now = Nested(); static int later = Mark(3); };
void main() { print(State::now.item == nullptr); print(Order::trace); }
)vs", "true\n31\n", "nullable fields do not eagerly initialize their declared element type"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Order { public: static int trace = 0; };
int Mark(int value) { Order::trace = Order::trace * 10 + value; return value; }
class Needed<T> { public: static int seed = Mark(1); };
class Nested { public: fn()->int read = []()->int { return Needed<int>::seed; }; };
class State { public: static Nested now = Nested(); static int later = Mark(3); };
void main() { print(Order::trace); auto read = State::now.read; print(read()); }
)vs", "31\n1\n", "stored field closures keep their bodies deferred during static construction"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Counter { public: static int read() { return 9; } };
interface Source { int copy = Counter::read(); };
class Value : Source {};
void main() { Value item = Value(); print(item.copy); }
)vs", "9\n", "ordinary interface field defaults see class static method declarations"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
void main() { type<int> info = type<int>(); print(info.name); print(info.qualified_name); }
)vs", "int\nint\n", "synthesized reflection type defaults survive the closed field-default pass"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Counter { public: static int read() { return 9; } };
interface Source<T> { int copy = Source<T>::read(); static int read() { return Counter::read(); } };
interface Closed : Source<int> {};
class Value : Closed {};
void main() { Value item = Value(); print(item.copy); }
)vs", "9\n", "early interface instantiation can call ordinary static method declarations"))
	{
		++Failures;
	}
	if (!CompileFails(R"vs(
class Counter { static int read() { return 9; } };
class Value<T> { public: int copy = Counter::read(); };
void main() { Value<int> item = Value<int>(); }
)vs", "early static method declarations retain private access checks", "cannot access"))
	{
		++Failures;
	}
	if (!CompileFails(R"vs(
struct Value<T> { static int read() = delete; int copy = Value<T>::read(); };
void main() { Value<int> item = Value<int>(); }
)vs", "early static method declarations retain deleted method checks", "deleted"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class ClassValue
{
public:
	const int copy = ClassValue::read(2);
	static int read(int add = 3) { return 5 + add; }
};
struct StructValue
{
	int copy = StructValue::read();
	static int read() { return 9; }
};
void main() { ClassValue first = ClassValue(); StructValue second = StructValue(); print(first.copy); print(second.copy); }
)vs", "7\n9\n", "ordinary field defaults can call later static method declarations"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class ClassValue<T>
{
public:
	static int seed = 7;
	T copy = ClassValue<T>::read();
	static T read() { return cast<T>(ClassValue<T>::seed); }
};
struct StructValue<T>
{
	T copy = StructValue<T>::read(2);
	static T read(int add) { return cast<T>(5 + add); }
};
class State { public: ClassValue<int> first = ClassValue<int>(); StructValue<int64> second = StructValue<int64>(); };
void main()
{
	State state = State();
	print(state.first.copy);
	print(state.second.copy);
	ClassValue<int>::seed = 11;
	ClassValue<int> later = ClassValue<int>();
	print(later.copy);
	print(state.first.copy);
}
)vs", "7\n7\n11\n7\n", "generic field defaults call self static methods after their owner layout is complete"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
struct Value<T>
{
	int copy = Value<T>::convert<int>(9);
	static U convert<U>(U input) { return input; }
};
class State { public: Value<string> item = Value<string>(); };
void main() { State state = State(); print(state.item.copy); }
)vs", "9\n", "field defaults can instantiate a static method template on their own generic owner"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
interface Source<T>
{
	static int seed = 7;
	int copy = Source<T>::read();
	static int read() { return Source<T>::seed; }
};
class Value<T> : Source<T> {};
class State { public: Value<int> item = Value<int>(); };
void main() { State state = State(); print(state.item.copy); }
)vs", "7\n", "generic interface defaults can call their own static methods"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
struct Outer { static int seed = 21; };
namespace Library
{
	struct Outer<T>
	{
		static int seed = 3;
		int copy = ::Outer::seed;
	};
}
void main() { Library::Outer<int> item = Library::Outer<int>(); print(item.copy); }
)vs", "21\n", "global static qualifiers do not bind a namespaced lexical type with the same leaf name"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
struct Other { int seed = 17; };
struct Outer<T>
{
	struct Inner
	{
		static int seed = 3;
		int local = Inner::seed;
		int current = Outer<T>::Inner::seed;
		int fixed = Outer<int>::Inner::seed;
		int read() { Other Inner = Other(); return Inner.seed; }
	};
};
class State { public: Outer<string>::Inner item = Outer<string>::Inner(); };
void main()
{
	State first = State();
	Outer<int>::Inner::seed = 11;
	Outer<string>::Inner::seed = 13;
	State later = State();
	print(later.item.local);
	print(later.item.current);
	print(later.item.fixed);
	print(first.item.current);
	print(later.item.read());
}
)vs", "13\n13\n11\n3\n17\n", "nested self static defaults preserve lexical names and explicit enclosing arguments"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
namespace Library
{
	struct Outer<T>
	{
		struct Inner<U>
		{
			static U seed = cast<U>(5);
			U local = Inner<U>::seed;
			U current = Outer<T>::Inner<U>::seed;
		};
	};
}
class State { public: Library::Outer<string>::Inner<int> item = Library::Outer<string>::Inner<int>(); };
void main() { State state = State(); print(state.item.local); print(state.item.current); }
)vs", "5\n5\n", "nested template static defaults retain captured and own arguments inside a namespace"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
template<typename T, int N> struct Outer
{
	struct Middle<U>
	{
		struct Inner<V>
		{
			static int seed = N;
			int local = Inner<V>::seed;
			int qualified = Outer<T, N>::Middle<U>::Inner<V>::seed;
		};
	};
};
class State { public: Outer<int, 7>::Middle<string>::Inner<bool> item = Outer<int, 7>::Middle<string>::Inner<bool>(); };
void main() { State state = State(); print(state.item.local); print(state.item.qualified); }
)vs", "7\n7\n", "deeply nested self static defaults preserve enclosing type and value parameters"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Value<T> { public: static int value = 7; int copy = Value<T>::value; };
class State { public: Value<int> early = Value<int>(); };
void main()
{
	State state = State();
	print(state.early.copy);
	Value<int>::value = 11;
	Value<int> later = Value<int>();
	print(later.copy);
	print(state.early.copy);
}
)vs", "7\n11\n7\n", "generic class field defaults read live self static storage without recursive layout"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
struct Value<T> { T copy = Value<T>::value; static T value = 3; };
struct State { Value<int> first = Value<int>(); Value<int64> second = Value<int64>(); };
void main()
{
	State state = State();
	print(state.first.copy);
	print(state.second.copy);
	Value<int>::value = 8;
	Value<int> later = Value<int>();
	print(later.copy);
	print(Value<int64>::value);
}
)vs", "3\n3\n8\n3\n", "generic struct defaults support later static declarations and isolated concrete storage"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
namespace Library
{
	class Base<T> { public: static T value = 9; };
	class Value<T> : public Base<T>
	{
		static const int extra = 2;
	public:
		T copy = Value<T>::value + cast<T>(Value<T>::extra);
	};
}
using Item = Library::Value<int>;
class State { public: Item item = Item(); };
void main() { State state = State(); print(state.item.copy); }
)vs", "11\n", "self static defaults retain namespace aliases, inherited storage and private access"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Value<T> { public: static int value = 7; int copy = Value<T>::value; };
class State { public: static Value<int> item = Value<int>(); };
void main() { print(State::item.copy); print(Value<int>::value); }
)vs", "7\n7\n", "self static defaults initialize before construction from an ordinary static field"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Order { public: static int trace = 0; };
int Mark(int value) { Order::trace = Order::trace * 10 + value; return value; }
struct Value<T> { static T value = cast<T>(Mark(2)); T copy = value; };
class State { public: Value<int> item = Value<int>(); static int first = Mark(1); };
void main() { State state = State(); print(Order::trace); print(state.item.copy); print(Order::trace); }
)vs", "12\n2\n12\n", "early self static storage preserves initialization order and unqualified lookup"))
	{
		++Failures;
	}
	if (!CompileFails(R"vs(
class Hidden<T> { static int value = 7; };
class Reader<T> { public: int copy = Hidden<T>::value; };
class State { public: Reader<int> item = Reader<int>(); };
void main() {}
)vs", "early generic storage still rejects private access from another owner", "cannot access"))
	{
		++Failures;
	}
	if (!CompileFails(R"vs(
struct Value<T> { static const int value = 7; int copy = (Value<T>::value = 9); };
struct State { Value<int> item = Value<int>(); };
void main() {}
)vs", "self static default expressions retain const write checks"))
	{
		++Failures;
	}
	if (!CompileFails(R"vs(
struct Value<T> { static int value = 7; Value<T> copy = Value<T>(); };
struct State { Value<int> item = Value<int>(); };
void main() {}
)vs", "early static storage does not publish an incomplete recursive value layout", "recursive"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Counter { public: static int64 value = 7; };
class Reader<T> { public: T read() const { return cast<T>(Counter::value); } };
class State { public: Reader<int64> reader = Reader<int64>(); };
void main() { State state = State(); print(state.reader.read()); Counter::value++; print(state.reader.read()); }
)vs", "7\n8\n", "member instantiation registers ordinary class statics before generic bodies"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Order { public: static int trace = 0; };
int Mark(int value) { Order::trace = Order::trace * 10 + value; return value; }
class Counter { public: static int value = Mark(1); };
class Later { public: static int value = Mark(2); };
class Reader<T>
{
public:
    static int total = Counter::value + Later::value;
    T read() const { T copy = cast<T>(Counter::value + Later::value); return copy + cast<T>(Reader<T>::total); }
};
class State { public: Reader<int64> reader = Reader<int64>(); };
void main() { State a = State(); State b = State(); print(Order::trace); print(a.reader.read()); print(b.reader.read()); print(Order::trace); }
)vs", "12\n6\n6\n12\n", "early generic member types defer initialization until ordinary statics and initialize only once"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
interface Counter { static int value = 9; };
interface Reader<T> { static T value = cast<T>(Counter::value); };
class State { public: Reader<int64> reader = nullptr; };
void main() { State state = State(); print(Reader<int64>::value); }
)vs", "9\n", "early generic interface types retain concrete types and initialize after ordinary interface statics"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Counter { public: static int value = 10; };
class Reader<T> { public: static int value = Counter::value; static int read() { return Reader<T>::value; } };
int value = Reader<string>::read();
void main() { print(value); }
)vs", "10\n", "generic statics first used in top-level initializers remain initialized before the use"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Value<T>
{
public:
	static int value = 7;
	static int read() { return Value<T>::value; }
};
class State
{
public:
	Value<int> early = Value<int>();
	static int result = Value<int>::read();
};
void main() { print(State::result); print(Value<int>::value); }
)vs", "7\n7\n", "static method calls initialize an already queued generic owner before use"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Counter { public: static int value = 9; };
class First<T> { public: static int value = Second<T>::read(); };
class Second<T>
{
public:
	static int value = Counter::value;
	static int read() { return Second<T>::value; }
};
class State { public: First<int> first = First<int>(); Second<int> second = Second<int>(); };
void main() { print(First<int>::value); print(Second<int>::value); }
)vs", "9\n9\n", "queued generic initializers resolve dependencies reached through static methods"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Base<T>
{
public:
	static int value = 9;
	static int read() { return Base<T>::value; }
};
class Derived<T> : public Base<T> {};
class State
{
public:
	Derived<int> early = Derived<int>();
	static int result = Derived<int>::read();
};
void main() { print(State::result); print(Base<int>::value); }
)vs", "9\n9\n", "inherited static method calls initialize their declaring generic owner"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Order { public: static int trace = 0; };
int Mark(int value) { Order::trace = Order::trace * 10 + value; return value; }
class Unrelated<T> { public: static int value = Mark(2); };
class Needed<T>
{
public:
	static int value = Mark(1);
	static int read() { return Needed<T>::value; }
};
class State
{
public:
	Unrelated<int> unrelated = Unrelated<int>();
	Needed<int> needed = Needed<int>();
	static int result = Needed<int>::read();
};
void main() { print(State::result); print(Order::trace); }
)vs", "1\n12\n", "static method dependencies initialize once without flushing unrelated queued owners"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Value<T> { public: static int value = 7; };
class Counter
{
public:
    Value<int> early = Value<int>();
    static int fromEarly = Value<int>::value;
    static int fromLater = Value<string>::value;
};
void main() { print(Counter::fromEarly); print(Counter::fromLater); }
)vs", "7\n7\n", "ordinary static initializers retain reads from both queued and newly instantiated generic statics"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Counter { public: static int value = 9; };
class First<T> { public: static int value = Second<T>::value; };
class Second<T> { public: static int value = Counter::value; };
class State { public: First<int> first = First<int>(); Second<int> second = Second<int>(); };
void main() { State state = State(); print(First<int>::value); print(Second<int>::value); }
)vs", "9\n9\n", "queued generic static dependencies initialize before their consumers"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Gate { public: static bool open = false; };
class Value<T> { public: static int value = 7; static int read() { return Value<T>::value; } };
class State
{
public:
    Value<int> early = Value<int>();
    static bool first = Gate::open && Value<int>::value > 0;
    static bool second = Gate::open && Value<int>::read() > 0;
};
void main() { print(Value<int>::value); }
)vs", "7\n", "queued generic initialization is unconditional across short-circuit expressions"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Gate { public: static bool open = false; };
class Value<T> { public: static int value = 7; static int read() { return Value<T>::value; } };
class Holder { public: Value<int> early = nullptr; static Value<int> item = nullptr; };
class State { public: static bool check = Gate::open && Holder::item.read() > 0; };
void main() { print(Value<int>::value); }
)vs", "7\n", "conditional static calls through a field receiver do not make initialization conditional"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Value<T> { public: static int value = 7; int copy; Value() { copy = Value<T>::value; } };
class State { public: Value<int> early = Value<int>(); static Value<int> instance = Value<int>(); };
void main() { print(State::instance.copy); }
)vs", "7\n", "queued generic statics initialize before a static field constructs their instance"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Order { public: static int trace = 0; };
int Mark(int value) { Order::trace = Order::trace * 10 + value; return value; }
class Value<T> { public: static int value = Mark(2); };
class State
{
public:
    Value<int> early = Value<int>();
    static fn()->int read = []()->int { return Value<int>::value; };
    static int later = Mark(1);
};
void main() { auto callback = State::read; print(Order::trace); print(callback()); }
)vs", "12\n2\n", "deferred lambda bodies do not hoist generic initialization into an earlier static field"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
struct Counter { static int value; };
struct Reader<T> { T read() const { Counter::value += 2; return cast<T>(Counter::value); } };
struct State { Reader<int> reader = Reader<int>(); };
void main() { State state = State(); print(state.reader.read()); print(Counter::value); }
)vs", "2\n2\n", "member instantiation registers zero-initialized struct statics for assignment"))
	{
		++Failures;
	}
	if (!CompileFails(R"vs(
class Counter { static int value = 7; };
class Reader<T> { public: T read() const { return cast<T>(Counter::value); } };
class State { public: Reader<int> reader = Reader<int>(); };
void main() { State state = State(); print(state.reader.read()); }
)vs", "early ordinary static registration retains access checks", "cannot access"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Counter { public: static int64 value = 7; };
class Derived : public Counter {};
class Reader<T> { public: T read() const { return cast<T>(Derived::value); } };
class State { public: Reader<int64> reader = Reader<int64>(); };
void main() { State state = State(); print(state.reader.read()); Derived::value++; print(Counter::value); }
)vs", "7\n8\n", "early inherited static access registers the storage owner, not the receiver"))
	{
		++Failures;
	}

	{
		std::string Source;
		for (int Index = 0; Index < 128; ++Index)
		{
			Source += "struct Unrelated" + std::to_string(Index)
				+ " { int value = 1; int read() { return value; } };\n";
			Source += "class PrivateUnrelated" + std::to_string(Index)
				+ " { int value = 1; public: int read() { return value; } };\n";
		}
		Source += R"vs(
namespace Indexed {
	struct Store<T> {
		static T shared = 3;
		T value = 0;
		T read() { return value; }
		int choose(int input) { return input + 1; }
		string choose(string input) { return input + "!"; }
	};
	struct Store<T,U> { static U shared = "pair"; T value = 4; };
	struct Derived<T> : public Store<T> {};
	class Secret<T> { T value; public: T read() { return value; } void write(T input) { value = input; } };
}
using Value = Indexed::Derived<int>;
void main() {
	Value left = Value(); left.value = 7;
	Value right = left; right.value = 9; left = right;
	left.shared = 11;
	print(left.read()); print(left.choose(4)); print(left.choose("ok"));
	print(Indexed::Store<int>::shared); print(Indexed::Store<long>::shared);
	Indexed::Store<int,string> pair = Indexed::Store<int,string>();
	print(pair.value); print(pair.shared);
	Indexed::Secret<int> secret = Indexed::Secret<int>(); secret.write(5); print(secret.read());
}
)vs";
		if (!RunSource(Source, "9\n5\nok!\n11\n3\n4\npair\n5\n",
			"indexed lookups retain generic arity, inheritance, aliases and memberwise copies"))
		{
			++Failures;
		}
	}

	if (!RunSource(
			"namespace Library\n"
			"{\n"
			"\tpublic static class Numbers\n"
			"\t{\n"
			"\t\tint Base = 5;\n"
			"\t\tint Add(int value) { return Numbers::Base + value; }\n"
			"\t}\n"
			"}\n"
			"void main() { print(Library::Numbers::Add(3)); }\n",
			"8\n",
			"namespace declarations support type-header modifiers"))
	{
		++Failures;
	}

	if (!RunSource(
			"public static class Math\n"
			"{\n"
			"\tpublic int Value = 40;\n"
			"\tprivate int Hidden() { return 1; }\n"
			"\tpublic int Add(int value) { return Math::Value + value; }\n"
			"}\n"
			"void main()\n"
			"{\n"
			"\tprint(Math::Add(2));\n"
			"\tprint(Math::Value);\n"
			"}\n",
			"42\n40\n",
			"static class normalizes members and honors access sections"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace Library\n"
			"{\n"
			"\tstatic struct Numbers\n"
			"\t{\n"
			"\t\tint Base = 5;\n"
			"\t\tint Add(int value) { return Numbers::Base + value; }\n"
			"\t}\n"
			"}\n"
			"void main() { print(Library::Numbers::Add(3)); print(Library::Numbers::Base); }\n",
			"8\n5\n",
			"static struct normalizes members while preserving struct default access"))
	{
		++Failures;
	}

	if (!RunSource(
			"public class PublicDefault { int Value = 4; int Read() { return Value; } }\n"
			"private struct PrivateDefault { int Value = 6; public: int Read() { return Value; } }\n"
			"void main() { PublicDefault a = PublicDefault(); PrivateDefault b = PrivateDefault(); print(a.Value); print(b.Read()); }\n",
			"4\n6\n",
			"type header access changes only the default member access"))
	{
		++Failures;
	}

	if (!RunSource(
			"class InlineClass\n"
			"{\n"
			"\tprivate int Hidden = 40;\n"
			"\tpublic int Visible = 2;\n"
			"\tprotected static int Shared = 7;\n"
			"\tpublic int ReadHidden() { return Hidden; }\n"
			"\tint FollowsClassDefault() { return Visible; }\n"
			"}\n"
			"struct InlineStruct\n"
			"{\n"
			"\tprivate int Hidden = 3;\n"
			"\tpublic int Visible = 4;\n"
			"\tprotected static int Shared = 5;\n"
			"\tpublic int ReadHidden() { return Hidden; }\n"
			"\tint FollowsStructDefault = 6;\n"
			"}\n"
			"class ProtectedBase { protected int Value = 9; }\n"
			"class ProtectedChild : public ProtectedBase\n"
			"{\n"
			"\tpublic int ReadBaseValue() { return Value; }\n"
			"}\n"
			"interface IInline\n"
			"{\n"
			"\tprivate static int Hidden = 8;\n"
			"\tpublic static int Read() { return IInline.Hidden; }\n"
			"}\n"
			"void main()\n"
			"{\n"
			"\tInlineClass a = InlineClass();\n"
			"\tInlineStruct b = InlineStruct();\n"
			"\tProtectedChild child = ProtectedChild();\n"
			"\tprint(a.Visible);\n"
			"\tprint(a.ReadHidden());\n"
			"\tprint(b.Visible);\n"
			"\tprint(b.FollowsStructDefault);\n"
			"\tprint(b.ReadHidden());\n"
			"\tprint(child.ReadBaseValue());\n"
			"\tprint(IInline.Read());\n"
			"}\n",
			"2\n40\n4\n6\n3\n9\n8\n",
			"inline access modifiers apply only to class, struct, and interface members"))
	{
		++Failures;
	}

	if (!RunSource(
			"private interface IDefaultAccess\n"
			"{\n"
			"\tstatic int Hidden = 8;\n"
			"\tpublic static int Read() { return IDefaultAccess.Hidden; }\n"
			"}\n"
			"void main() { print(IDefaultAccess.Read()); }\n",
			"8\n",
			"interface type header applies a default member access"))
	{
		++Failures;
	}

	if (!CompileFails(
			"private interface IDefaultAccess { static int Hidden = 8; }\n"
			"void main() { print(IDefaultAccess.Hidden); }\n",
			"interface type-header private default is enforced",
			"cannot access member"))
	{
		++Failures;
	}

	if (!CompileFails(
			"class AccessCheck { private int Value = 1; }\n"
			"void main() { AccessCheck value = AccessCheck(); print(value.Value); }\n",
			"inline private field remains inaccessible outside its type",
			"cannot access member"))
	{
		++Failures;
	}

	if (!CompileFails(
			"class Invalid { public private int Value = 1; }\nvoid main() {}\n",
			"duplicate inline access modifiers are rejected",
			"duplicate access modifier"))
	{
		++Failures;
	}

	if (!CompileFails(
			"static class Invalid { Invalid() {} }\nvoid main() {}\n",
			"static class rejects constructors",
			"cannot declare a constructor"))
	{
		++Failures;
	}

	if (!CompileFails(
			"static struct Invalid { Invalid() {} }\nvoid main() {}\n",
			"static struct rejects constructors",
			"cannot declare a constructor"))
	{
		++Failures;
	}

	if (!CompileFails(
			"static struct Math { int Add(int x) { return x; } }\nvoid main() { Math value = Math(); }\n",
			"static struct cannot be constructed",
			"cannot instantiate static struct"))
	{
		++Failures;
	}

	if (!CompileFails(
			"static interface Invalid {}\nvoid main() {}\n",
			"static interface remains rejected",
			"only supported on class or struct"))
	{
		++Failures;
	}

	if (!CompileFails(
			"static class Base {}\nclass Derived : public Base {}\nvoid main() {}\n",
			"static class cannot be inherited",
			"cannot inherit from static class"))
	{
		++Failures;
	}

	if (!CompileFails(
			"static struct Base {}\nstruct Derived : public Base {}\nvoid main() {}\n",
			"static struct cannot be inherited",
			"cannot inherit from static struct"))
	{
		++Failures;
	}

	if (!CompileFails(
			"static class Math { int Add(int x) { return x; } }\nvoid main() { Math value = Math(); }\n",
			"static class cannot be constructed",
			"cannot instantiate static class"))
	{
		++Failures;
	}

	if (!RunSource(
			"class Counter\n"
			"{\n"
			"public:\n"
			"	static int Count = 0;\n"
			"	static const int Max = 100;\n"
			"	static int Next()\n"
			"	{\n"
			"		Counter.Count = Counter.Count + 1;\n"
			"		return Counter.Count;\n"
			"	}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	print(Counter.Max);\n"
			"	print(Counter.Next());\n"
			"	print(Counter.Count);\n"
			"	Counter c = Counter();\n"
			"	print(c.Next());\n"
			"	print(Counter.Count);\n"
			"}\n",
			"100\n1\n1\n2\n2\n",
			"class static field and method"))
	{
		++Failures;
	}

	if (!RunSource(
			"struct MathUtil\n"
			"{\n"
			"	static int Square(int x) { return x * x; }\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	print(MathUtil.Square(9));\n"
			"}\n",
			"81\n",
			"struct static method"))
	{
		++Failures;
	}

	if (!RunSource(
			"interface IFactory\n"
			"{\n"
			"	static int MakeId() { return 42; }\n"
			"	static int Tag = 7;\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	print(IFactory.MakeId());\n"
			"	print(IFactory.Tag);\n"
			"}\n",
			"42\n7\n",
			"interface static method and field"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace Util\n"
			"{\n"
			"	int Offset = 3;\n"
			"	int Add(int x) { return x + Util.Offset; }\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	print(Util.Add(10));\n"
			"	Util.Offset = 5;\n"
			"	print(Util.Add(10));\n"
			"}\n",
			"13\n15\n",
			"namespace variable"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace Ns\n"
			"{\n"
			"	class Box\n"
			"	{\n"
			"	public:\n"
			"		static int Value = 9;\n"
			"		static int Get() { return Ns.Box.Value; }\n"
			"	}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	print(Ns.Box.Get());\n"
			"}\n",
			"9\n",
			"namespaced type static members"))
	{
		++Failures;
	}

	if (!CompileFails(
			"class C\n"
			"{\n"
			"public:\n"
			"	static const int Max = 10;\n"
			"}\n"
			"void main() { C.Max = 1; }\n",
			"static const assign rejected",
			"const"))
	{
		++Failures;
	}

	if (!RunSource(
			"class C\n"
			"{\n"
			"public:\n"
			"	static int N = 0;\n"
			"	int dummy = 0;\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	C c = C();\n"
			"	c.N = 5;\n"
			"	print(c.N);\n"
			"	c.N++;\n"
			"	print(C.N);\n"
			"}\n",
			"5\n6\n",
			"instance.staticField read/write/increment"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace U\n"
			"{\n"
			"	int x = 1;\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	U.x += 2;\n"
			"	print(U.x);\n"
			"	U.x++;\n"
			"	print(U.x);\n"
			"}\n",
			"3\n4\n",
			"namespace-qualified global compound assign"))
	{
		++Failures;
	}

	if (!CompileFails(
			"class D\n"
			"{\n"
			"public:\n"
			"	static int x = 0;\n"
			"	int x = 0;\n"
			"}\n"
			"void main() {}\n",
			"duplicate field rejected",
			"duplicate field"))
	{
		++Failures;
	}

	if (!CompileFails(
			"class C\n"
			"{\n"
			"public:\n"
			"	static virtual int F() { return 1; }\n"
			"}\n"
			"void main() {}\n",
			"static virtual rejected",
			"static method cannot be virtual"))
	{
		++Failures;
	}

	if (!CompileFails(
			"class C\n"
			"{\n"
			"public:\n"
			"	static int F() const { return 1; }\n"
			"}\n"
			"void main() {}\n",
			"static const method rejected",
			"static method cannot be const"))
	{
		++Failures;
	}

	if (!CompileFails(
			"interface I\n"
			"{\n"
			"	static virtual int F() = 0;\n"
			"}\n"
			"void main() {}\n",
			"static interface abstract rejected",
			"static method cannot be virtual"))
	{
		++Failures;
	}

	if (!CompileFails(
			"class C\n"
			"{\n"
			"public:\n"
			"	static int F()\n"
			"	{\n"
			"		return this.id;\n"
			"	}\n"
			"	int id = 0;\n"
			"}\n"
			"void main() {}\n",
			"this in static method rejected",
			"this"))
	{
		++Failures;
	}

	// Generic classes: each mangled instantiation keeps its own static storage
	// (`Box<int>.Count` / `Box<int>::Count` is distinct from `Box<string>.Count`).
	if (!RunSource(
			"class Box<T>\n"
			"{\n"
			"public:\n"
			"	static int Count = 0;\n"
			"	T value;\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	Box<int> a = Box<int>();\n"
			"	a.Count = 1;\n"
			"	print(a.Count);\n"
			"	Box<string> s = Box<string>();\n"
			"	print(s.Count);\n"
			"	a.Count = a.Count + 1;\n"
			"	print(a.Count);\n"
			"	print(s.Count);\n"
			"	Box<int>.Count = 9;\n"
			"	print(a.Count);\n"
			"	print(Box<string>.Count);\n"
			"}\n",
			"1\n0\n2\n0\n9\n0\n",
			"generic class per-instantiation static storage"))
	{
		++Failures;
	}

	if (!RunSource(
			"class Box<T>\n"
			"{\n"
			"public:\n"
			"	static int Count = 0;\n"
			"	T value;\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	Box<int> a = Box<int>();\n"
			"	Box<int>::Count = 1;\n"
			"	print(a.Count);\n"
			"	Box<string> s = Box<string>();\n"
			"	print(Box<string>::Count);\n"
			"	Box<int>::Count = Box<int>::Count + 1;\n"
			"	print(Box<int>::Count);\n"
			"	print(s.Count);\n"
			"}\n",
			"1\n0\n2\n0\n",
			"generic class :: static access equivalent"))
	{
		++Failures;
	}

	// Generic structs: same per-instantiation static storage.
	if (!RunSource(
			"struct Slot<T>\n"
			"{\n"
			"	static int N = 0;\n"
			"	T value;\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	Slot<int> a = Slot<int>();\n"
			"	Slot<int>.N = 3;\n"
			"	Slot<bool> b = Slot<bool>();\n"
			"	print(Slot<int>.N);\n"
			"	print(Slot<bool>.N);\n"
			"	print(a.N);\n"
			"}\n",
			"3\n0\n3\n",
			"generic struct per-instantiation static storage"))
	{
		++Failures;
	}

	// `static T` field type must follow the looked-up instantiation, not the
	// ambient ActiveTypeSubst of the enclosing generic method (common `T` name).
	if (!RunSource(
			"class Box<T>\n"
			"{\n"
			"public:\n"
			"	static T Value;\n"
			"	T pad;\n"
			"}\n"
			"class Writer<T>\n"
			"{\n"
			"public:\n"
			"	T pad;\n"
			"	void Write()\n"
			"	{\n"
			"		Box<string>.Value = \"ok\";\n"
			"	}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	Writer<int> w = Writer<int>();\n"
			"	w.Write();\n"
			"	print(Box<string>.Value);\n"
			"}\n",
			"ok\n",
			"generic static T field type uses looked-up subst"))
	{
		++Failures;
	}

	// Type-name static method call on a generic instantiation (`Box<int>.Seed()`).
	// Must work even when the only use of Box<int> is the type-name call itself
	// (no prior instance / constructor — Ensure materializes methods on demand).
	if (!RunSource(
			"class Box<T>\n"
			"{\n"
			"public:\n"
			"	static int Count = 0;\n"
			"	T value;\n"
			"	static int Seed()\n"
			"	{\n"
			"		Box<int>.Count = Box<int>.Count + 1;\n"
			"		return Box<int>.Count;\n"
			"	}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	print(Box<int>.Seed());\n"
			"	print(Box<int>.Seed());\n"
			"	print(Box<string>.Count);\n"
			"}\n",
			"1\n2\n0\n",
			"generic type-name static method call"))
	{
		++Failures;
	}

	// Same as above but with a prior instance (regression for Ensure + existing path).
	if (!RunSource(
			"class Box2<T>\n"
			"{\n"
			"public:\n"
			"	static int Count = 0;\n"
			"	T value;\n"
			"	static int Seed()\n"
			"	{\n"
			"		Box2<int>.Count = Box2<int>.Count + 1;\n"
			"		return Box2<int>.Count;\n"
			"	}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	Box2<int> a = Box2<int>();\n"
			"	print(Box2<int>.Seed());\n"
			"	print(Box2<int>.Seed());\n"
			"	print(Box2<string>.Count);\n"
			"}\n",
			"1\n2\n0\n",
			"generic type-name static method after instance"))
	{
		++Failures;
	}

	// Generic static initializers run after non-generic statics so they can
	// read already-initialized peers (declaration-order within the TU).
	if (!RunSource(
			"class A\n"
			"{\n"
			"public:\n"
			"	static int X = 10;\n"
			"}\n"
			"class B<T>\n"
			"{\n"
			"public:\n"
			"	static int Y = A.X;\n"
			"	T pad;\n"
			"}\n"
			"void main()\n"
			"{\n"
			"	print(B<int>.Y);\n"
			"}\n",
			"10\n",
			"generic static init after non-generic static"))
	{
		++Failures;
	}

	if (!RunSource(
			"class Inspector\n"
			"{\n"
			"public:\n"
			"\tint Read(Secret value) { return value.Code; }\n"
			"}\n"
			"class Secret\n"
			"{\n"
			"\tfriend Inspector;\n"
			"\tint Code = 41;\n"
			"public:\n"
			"\tSecret() {}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"\tInspector reader = Inspector();\n"
			"\tprint(reader.Read(Secret()));\n"
			"}\n",
			"41\n",
			"friend type accesses private field"))
	{
		++Failures;
	}

	if (!RunSource(
			"class Base\n"
			"{\n"
			"protected:\n"
			"\tint Code = 52;\n"
			"}\n"
			"class Inspector\n"
			"{\n"
			"public:\n"
			"\tint Read(Derived value) { return value.Code; }\n"
			"}\n"
			"class Derived : public Base\n"
			"{\n"
			"\tfriend Inspector;\n"
			"}\n"
			"void main() { print(Inspector().Read(Derived())); }\n",
			"52\n",
			"derived type friend accesses inherited protected state"))
	{
		++Failures;
	}

	if (!CompileFails(
			"class Base\n"
			"{\n"
			"\tint Code = 1;\n"
			"}\n"
			"class Inspector\n"
			"{\n"
			"public:\n"
			"\tint Leak(Derived value) { return value.Code; }\n"
			"}\n"
			"class Derived : public Base\n"
			"{\n"
			"\tfriend Inspector;\n"
			"}\n"
			"void main() {}\n",
			"derived friendship does not grant base private access",
			"cannot access"))
	{
		++Failures;
	}

	if (!CompileFails(
			"class Base\n"
			"{\n"
			"protected:\n"
			"\tint Code = 1;\n"
			"}\n"
			"class Inspector\n"
			"{\n"
			"public:\n"
			"\tint Leak(Base value) { return value.Code; }\n"
			"}\n"
			"class Derived : public Base\n"
			"{\n"
			"\tfriend Inspector;\n"
			"}\n"
			"void main() {}\n",
			"derived friendship retains the protected receiver restriction",
			"cannot access"))
	{
		++Failures;
	}

	if (!RunSource(
			"class Base\n"
			"{\n"
			"protected:\n"
			"\tint Code = 53;\n"
			"}\n"
			"class Derived : public Base\n"
			"{\n"
			"\tfriend int Inspect(Derived value);\n"
			"}\n"
			"int Inspect(Derived value) { return value.Code; }\n"
			"void main() { print(Inspect(Derived())); }\n",
			"53\n",
			"derived function friend accesses inherited protected state"))
	{
		++Failures;
	}

	if (!RunSource(
			"class Reader\n"
			"{\n"
			"public:\n"
			"\tint Read(Secret value) { return value.Code; }\n"
			"}\n"
			"class Secret\n"
			"{\n"
			"\tfriend int Reader::Read(Secret value);\n"
			"\tint Code = 42;\n"
			"public:\n"
			"\tSecret() {}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"\tReader reader = Reader();\n"
			"\tprint(reader.Read(Secret()));\n"
			"}\n",
			"42\n",
			"friend member exact signature"))
	{
		++Failures;
	}

	if (!RunSource(
			"class Secret\n"
			"{\n"
			"\tfriend int Inspect(Secret value);\n"
			"\tint Code = 43;\n"
			"public:\n"
			"\tSecret() {}\n"
			"}\n"
			"int Inspect(Secret value) { return value.Code; }\n"
			"void main() { print(Inspect(Secret())); }\n",
			"43\n",
			"friend free function exact signature"))
	{
		++Failures;
	}

	if (!RunSource(
			"class Inspector\n"
			"{\n"
			"public:\n"
			"\tint Read(Secret value) { return value.Code; }\n"
			"}\n"
			"using InspectorAlias = Inspector;\n"
			"class Secret\n"
			"{\n"
			"\tfriend InspectorAlias;\n"
			"\tint Code = 50;\n"
			"public:\n"
			"\tSecret() {}\n"
			"}\n"
			"void main() { print(Inspector().Read(Secret())); }\n",
			"50\n",
			"friend type alias resolves to the underlying type"))
	{
		++Failures;
	}

	if (!RunSource(
			"class Reader\n"
			"{\n"
			"public:\n"
			"\tint Read(Secret value) { return value.Code; }\n"
			"}\n"
			"using ReaderAlias = Reader;\n"
			"class Secret\n"
			"{\n"
			"\tfriend int ReaderAlias::Read(Secret value);\n"
			"\tint Code = 51;\n"
			"public:\n"
			"\tSecret() {}\n"
			"}\n"
			"void main() { print(Reader().Read(Secret())); }\n",
			"51\n",
			"friend member owner alias resolves to the underlying type"))
	{
		++Failures;
	}

	if (!CompileFails(
			"class Reader\n"
			"{\n"
			"public:\n"
			"\tint Read(Secret value) { return value.Code; }\n"
			"\tint Leak(Secret value) { return value.Code; }\n"
			"}\n"
			"class Secret\n"
			"{\n"
			"\tfriend int Reader::Read(Secret value);\n"
			"\tint Code = 1;\n"
			"public:\n"
			"\tSecret() {}\n"
			"}\n"
			"void main() {}\n",
			"friend method does not grant sibling overload or method",
			"cannot access"))
	{
		++Failures;
	}

	if (!CompileFails(
			"class Inspector\n"
			"{\n"
			"}\n"
			"class Derived : public Inspector\n"
			"{\n"
			"public:\n"
			"\tint Leak(Secret value) { return value.Code; }\n"
			"}\n"
			"class Secret\n"
			"{\n"
			"\tfriend class Inspector;\n"
			"\tint Code = 1;\n"
			"}\n"
			"void main() {}\n",
			"friendship is not inherited",
			"cannot access"))
	{
		++Failures;
	}

	if (!RunSource(
			"class Reader\n"
			"{\n"
			"public:\n"
			"\tint Read(Secret value) const { return value.Code; }\n"
			"}\n"
			"class Secret\n"
			"{\n"
			"\tfriend int Reader::Read(Secret value) const;\n"
			"\tint Code = 44;\n"
			"public:\n"
			"\tSecret() {}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"\tReader reader = Reader();\n"
			"\tprint(reader.Read(Secret()));\n"
			"}\n",
			"44\n",
			"const friend member exact signature"))
	{
		++Failures;
	}

	if (!CompileFails(
			"class Reader\n"
			"{\n"
			"public:\n"
			"\tint Read(Secret value, string tag) { return value.Code; }\n"
			"}\n"
			"class Secret\n"
			"{\n"
			"\tfriend int Reader::Read(Secret value, int tag);\n"
			"\tint Code = 1;\n"
			"}\n"
			"void main() {}\n",
			"friend member grants only the declared overload",
			"cannot access"))
	{
		++Failures;
	}

	if (!RunSource(
			"class Secret\n"
			"{\n"
			"\tfriend int Inspect(Secret value);\n"
			"\tint Code = 45;\n"
			"public:\n"
			"\tSecret() {}\n"
			"}\n"
			"int Inspect(Secret value)\n"
			"{\n"
			"\tfn()->int read = [value]() -> int => value.Code;\n"
			"\treturn read();\n"
			"}\n"
			"void main() { print(Inspect(Secret())); }\n",
			"45\n",
			"lambda inherits enclosing friend function access"))
	{
		++Failures;
	}

	if (!RunSource(
			"namespace Tools\n"
			"{\n"
			"\tclass Reader\n"
			"\t{\n"
			"\tpublic:\n"
			"\t\tint Read(Secret value) { return value.Code; }\n"
			"\t}\n"
			"}\n"
			"class Secret\n"
			"{\n"
			"\tfriend int Tools::Reader::Read(Secret value);\n"
			"\tint Code = 46;\n"
			"public:\n"
			"\tSecret() {}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"\tTools::Reader reader = Tools::Reader();\n"
			"\tprint(reader.Read(Secret()));\n"
			"}\n",
			"46\n",
			"namespace-qualified friend member"))
	{
		++Failures;
	}

	if (!RunSource(
			"class Inspector<T>\n"
			"{\n"
			"\tT pad;\n"
			"public:\n"
			"\tint Read(Secret<T> value) { return value.Code; }\n"
			"}\n"
			"class Secret<T>\n"
			"{\n"
			"\tfriend class Inspector<T>;\n"
			"\tint Code = 47;\n"
			"public:\n"
			"\tSecret() {}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"\tInspector<int> reader = Inspector<int>();\n"
			"\tprint(reader.Read(Secret<int>()));\n"
			"}\n",
			"47\n",
			"generic friend type matches the same specialization"))
	{
		++Failures;
	}

	if (!RunSource(
			"class Reader<T>\n"
			"{\n"
			"public:\n"
			"\tint Read(Secret<T> value) { return value.Code; }\n"
			"}\n"
			"class Secret<T>\n"
			"{\n"
			"\tfriend int Reader<T>::Read(Secret<T> value);\n"
			"\tint Code = 49;\n"
			"public:\n"
			"\tSecret() {}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"\tReader<int> reader = Reader<int>();\n"
			"\tprint(reader.Read(Secret<int>()));\n"
			"}\n",
			"49\n",
			"generic friend member exact signature"))
	{
		++Failures;
	}

	if (!CompileFails(
			"class Inspector<T>\n"
			"{\n"
			"\tT pad;\n"
			"public:\n"
			"\tint Read(Secret<int> value) { return value.Code; }\n"
			"}\n"
			"class Secret<T>\n"
			"{\n"
			"\tfriend class Inspector<T>;\n"
			"\tint Code = 1;\n"
			"public:\n"
			"\tSecret() {}\n"
			"}\n"
			"void main()\n"
			"{\n"
			"\tInspector<string> reader = Inspector<string>();\n"
			"\tprint(reader.Read(Secret<int>()));\n"
			"}\n",
			"generic friendship does not cross specializations",
			"cannot access"))
	{
		++Failures;
	}

	if (!RunSource(
			"interface ISecret\n"
			"{\n"
			"\tfriend class Inspector;\n"
			"private:\n"
			"\tint Code = 48;\n"
			"}\n"
			"class Secret : ISecret\n"
			"{\n"
			"}\n"
			"class Inspector\n"
			"{\n"
			"public:\n"
			"\tint Read(Secret value) { return value.Code; }\n"
			"}\n"
			"void main()\n"
			"{\n"
			"\tInspector reader = Inspector();\n"
			"\tprint(reader.Read(Secret()));\n"
			"}\n",
			"48\n",
			"interface friend type accesses interface state"))
	{
		++Failures;
	}

	if (!RunSource(R"vs(
class Store<T> { public:
    static T value = 3;
    static T read() { return Store<T>::value; }
    static T call() { return Store<T>::read(); }
    static void bump(T& item) { item++; }
    static void bump_self() { Store<T>::bump(ref Store<T>::value); }
};
void main() {
    Store<int>::bump_self();
    int a = Store<int>::call(); long b = Store<long>::call();
    print(a); print(b);
}
)vs", "4\n3\n", "generic static fields and method receivers use the active specialization"))
	{
		++Failures;
	}

	if (!RunSource(R"vs(
class Value { public:
    static int n = 3;
    static int read() { return Value::n; }
    static int& alias() { return Value::n; }
};
class Reader<T> { public:
    T pad;
    int read() { return T::read() + T::n; }
    void bump(int& n) { n++; }
    void run() { bump(ref T::n); int& n = T::alias(); n++; }
};
void main() { Reader<Value> r = Reader<Value>(); print(r.read()); r.run(); print(Value::n); }
)vs", "6\n5\n", "type parameters resolve before classifying static field and method receivers"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
class Value { public: static int n = 3; static int read() { return Value::n; } };
class Reader<T> { public: T pad;
    int read() { Value T = Value(); return T.read() + T.n; }
};
void main() { Reader<Value> r = Reader<Value>(); print(r.read()); }
)vs", "6\n", "value bindings still take precedence over type parameters"))
	{
		++Failures;
	}
	if (!CompileFails(R"vs(
class Value { static int n = 3; };
class Reader<T> { public: T pad; int read() { return T::n; } };
void main() { Reader<Value> r = Reader<Value>(); print(r.read()); }
)vs", "type-parameter static receivers enforce access", "cannot access"))
	{
		++Failures;
	}
	if (!CompileFails(R"vs(
class Value { public: static const int n = 3; };
class Reader<T> { public: T pad; void bump(int& n) { n++; }
    void run() { bump(ref T::n); }
};
void main() { Reader<Value> r = Reader<Value>(); r.run(); }
)vs", "type-parameter static references enforce const"))
	{
		++Failures;
	}

	if (!RunSource(R"vs(
class Value<T> { public: static T n = 3; static T& read() { return Value<T>::n; } };
class Reader<T,U> { public: T pad;
    U read() { return T::read(); }
    void bump(U& n) { n++; }
    void run() { bump(ref T::n); U& n = T::read(); n++; }
};
void main() { Reader<Value<int>,int> r = Reader<Value<int>,int>();
    print(r.read()); r.run(); print(Value<int>::n);
    print(Value<long>::n); Value<int64>::n = 7i64; print(Value<long>::read());
}
)vs", "3\n5\n3\n7\n", "generic static storage canonicalizes primitive aliases before lazy registration"))
	{
		++Failures;
	}

	if (!RunSource(R"vs(
namespace Library {
    struct Store<T> {
        static T value = 3;
        static T read() { return value; }
        static T read(T input) { return input + cast<T>(1); }
        static T& alias() { return value; }
        static U identity<U>(U input) { return input; }
    };
    int read() { return 11; }
}
using First = Library::Store<int>;
using Value = First;
void main() {
    print(Value::read()); print(Value::read(7)); print(Value::identity<int>(9));
    Value::value = 5; int& item = Value::alias(); item++;
    print(Value::value); print(Library::read());
}
)vs", "3\n8\n9\n6\n11\n", "type aliases retain static member and namespace call grammar"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
struct Base<T> {
    struct Inner { T value; Inner(T input) : value(input) {} };
};
struct Derived<T> : public Base<T> {};
using Owner = Derived<int>;
void main() { Owner::Inner value = Owner::Inner(7); print(value.value); }
)vs", "7\n", "type aliases still resolve inherited nested type constructors"))
	{
		++Failures;
	}
	if (!RunSource(R"vs(
struct Store { static int read() { return 7; } };
using Value = Store;
void main() { print(Value::read()); }
)vs", "7\n", "non-generic aliases invoke static methods"))
	{
		++Failures;
	}
	if (!CompileFails(R"vs(
class Store { static int read() { return 7; } };
using Value = Store;
void main() { print(Value::read()); }
)vs", "aliased static calls retain access checks", "cannot access"))
	{
		++Failures;
	}
	if (!CompileFails(R"vs(
struct Store { static int read(int input) { return input; } };
using Value = Store;
void main() { print(Value::read("wrong")); }
)vs", "aliased static calls retain overload checks"))
	{
		++Failures;
	}

	if (Failures > 0)
	{
		std::cerr << Failures << " test(s) failed\n";
		return 1;
	}
	std::cout << "VeroScriptStaticMemberTests: all passed\n";
	return 0;
}
