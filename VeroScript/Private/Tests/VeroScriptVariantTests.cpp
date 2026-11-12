// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "VeroScriptEngine.h"
#include "VeroScriptHost.h"
#include "VeroScriptSession.h"
#include "VeroScriptVariant.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptAst.h"
#include "VeroScriptTestTemporaryDirectory.h"
#include "VeroScriptPath.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptRuntime.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <limits>

using namespace VeroScript;
int main(int argc, char** argv)
{
	bool Ok = true;
	FEngineConfig Config;
	Config.StdlibSearchPaths = {VEROSCRIPT_TEST_STDLIB};
	Config.bUseBytecodeCache = false;
	if (argc > 1)
	{
		std::string Error;
		Config.JitBackendFactory = LoadJitBackend(argv[1], Error);
		if (!Config.JitBackendFactory)
		{
			std::cerr << Error;
			return 1;
		}
	}
	const auto Run = [&](std::string_view Source, bool Success = true)
	{
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend == EExecutionBackend::Jit && !Config.JitBackendFactory)
			{
				continue;
			}
			Config.ExecutionBackend = Backend;
			std::ostringstream Out, Err;
			FStreamEngineHost Host(Out, Err);
			const auto Result = VeroScript::Test::FEngine::RunSource(Source, "variant.vs", Host, ERunMode::Script, Config);
			if (Result.bSuccess != Success)
			{
				std::cerr << "unexpected result: " << Err.str() << '\n'
				          << Source << '\n';
				Ok = false;
				VeroScriptLexer Lexer(Source, "variant.vs");
				VeroScriptParser Parser(Lexer.Tokenize(), "variant.vs");
				auto Program = Parser.ParseProgram();
				VeroScriptCompiler Compiler;
				if (auto M = Program ? Compiler.Compile(*Program) : nullptr)
				{
					for (const auto& E : M->Enums)
					{
						std::cerr << "enum identity: " << E.Name << '\n';
					}
					for (const auto& F : M->Functions)
					{
						for (std::size_t Ip = 0; Ip < F.Code.size(); Ip += Bytecode::InstructionSize(static_cast<EOpcode>(F.Code[Ip])))
						{
							if (static_cast<EOpcode>(F.Code[Ip]) == EOpcode::BoxVariant)
							{
								std::cerr << "box identity: " << M->StringPool[Bytecode::ReadU16LE(F.Code.data() + Ip + 1)] << '\n';
							}
						}
					}
				}
			}
		}
	};
	Run(R"VS(
variant empty;
assert(!variant_has_value(empty));
assert(variant_type_name(empty) == "");
variant number = 42;
assert(variant_has_value(number));
assert(variant_is<int>(number));
assert(!variant_is<double>(number));
assert(variant_type_name(number) == "int");
assert(cast<int>(number) == 42);
assert(cast<double>(number) == 42.0);
variant copy = variant(number);
assert(copy == number);
variant another = cast<variant>(42);
assert(another == number);
assert(variant() == empty);
assert(variant(42.0) != number);
std::variant text = "hello";
assert(std::variant_is<string>(text));
assert(cast<string>(text) == "hello");
assert(std::variant_has_value(text));
assert(std::variant_type_name(text) == "string");
using Dynamic = variant;
Dynamic alias = Dynamic(6);
assert(variant_is<int>(alias));
)VS");
	Run(R"VS(
import "core.vs";
variant make(int n) { return n; }
int read(variant v) { return cast<int>(v); }
variant v = make(12);
assert(read(13) == 13);
assert(cast<int>(v) == 12);
optional<int> good = try_cast<int>(v);
optional<int> bad = try_cast<int>(variant("bad"));
optional<int> none = try_cast<int>(variant());
assert(good.has_value() && good.get() == 12);
assert(!bad.has_value() && !none.has_value());
array<variant> values = array<variant>();
values.push(2); values.push("text");
assert(cast<int>(values[0]) == 2);
assert(cast<string>(values[1]) == "text");
map<string, variant> table = map<string, variant>();
table["value"] = 3;
assert(cast<int>(table["value"]) == 3);
)VS");
	Run("variant v = nullptr;", false);
	Run("variant v = 1; int n = v;", false);
	Run("variant v = 1; int n = v + v;", false);
	Run("variant v = 1; bool b = v == 1;", false);
	Run("variant v = 1; bool b = v < v;", false);
	Run("variant v = 1; bool b = variant_is<variant>(v);", false);
	Run("import \"core.vs\"; auto v = try_cast<variant>(1);", false);
	Run("map<variant,int> values;", false);
	Run("set<variant> values;", false);
	Run("channel<variant> values;", false);
	Run("priority_queue<variant> values;", false);
	Run("variant v; int n = cast<int>(v);", false);
	Run(R"VS(
int destroyed = 0;
class Trace { public: ~Trace() { destroyed += 1; } };
void query_temp() { assert(variant_has_value(variant(Trace()))); }
query_temp(); assert(destroyed == 1);
void compare_temp() { bool same = variant(Trace()) == variant(Trace()); assert(!same); }
compare_temp(); assert(destroyed == 3);
struct Point { public: int x = 1; };
Point original = Point();
variant point = original;
original.x = 2;
Point first = cast<Point>(point);
assert(first.x == 1);
first.x = 3;
Point second = cast<Point>(point);
assert(second.x == 1);
class Base { public: virtual int value() { return 1; } };
class Derived : public Base { public: int value() override { return 2; } };
Base base = Derived(); variant boxed = base;
assert(variant_is<Base>(boxed));
assert(!variant_is<Derived>(boxed));
Derived derived = cast<Derived>(boxed);
assert(derived.value() == 2);
Base nil = nullptr; variant typed_null = nil;
assert(variant_has_value(typed_null) && variant_is<Base>(typed_null));
Base recovered_null = cast<Base>(typed_null); assert(recovered_null == nullptr);
assert(typed_null != variant());
int f(variant a, int b, int c) { return 1; }
int f(double a, double b, double c) { return 2; }
assert(f(1,2,3) == 2);
variant identity<T>(T value) { return value; }
assert(cast<int>(identity<int>(7)) == 7);
void caught() {
  try { throw variant(12); }
  catch(int) { assert(false); }
  catch(variant error) { assert(cast<int>(error) == 12); }
}
caught();
assert(type<variant>().kind() == "variant");
int cref(const ref variant value) { return cast<int>(value); }
variant ref_value = 5;
assert(cref(ref ref_value) == 5);
enum Color { Red = 3 };
enum class State { Ready = 4 };
Color color = Red; variant e = color;
assert(variant_is<int>(e) && cast<int>(e) == 3);
State state = State::Ready; variant strong = state;
assert(variant_is<State>(strong));
assert(cast<int>(strong) == 4);
State restored = cast<State>(strong); assert(restored == State::Ready);
)VS");
	Run(R"VS(
import "core.vs";
function(int)->int add = function(int x)->int => x+2;
variant callable = add;
assert(variant_is<function(int)->int>(callable));
function(int)->int recovered = cast<function(int)->int>(callable);
assert(recovered(40) == 42);
std::bigint large = std::bigint("18446744073709551616");
variant big = large;
assert(variant_is<std::bigint>(big));
assert(cast<std::bigint>(big) == large);
std::rational fraction = std::rational(1,3);
variant exact = fraction;
assert(cast<std::rational>(exact) == fraction);
std::attribute<variant> attribute = std::attribute<variant>(7);
assert(cast<int>(attribute.get()) == 7);
class Holder<T>{public:T value;};
Holder<variant> holder = Holder<variant>();
assert(!variant_has_value(holder.value));
holder.value = 8;
assert(cast<int>(holder.value) == 8);
)VS");
	Run("variant v=1; if(v){}", false);
	Run("variant v=1; bool b=!v;", false);
	Run("variant v=1; auto a=true?v:3;", false);
	Run("variant v=1; auto a={v,3};", false);
	Run(R"VS(
variant choose(variant value){return value;}
int choose<T>(T value){return 29;}
int n=choose(1); assert(n==29);
)VS");
	Run("fn(int)->bool variant_is = fn(int n)->bool => true;", false);
	Run("void test(){auto variant_has_value=fn()->bool =>true;}", false);
	Run("void main(){variant values[2]; assert(!variant_has_value(values[0])); values[1]=9; assert(cast<int>(values[1])==9);}");
	Run("void main(string[] args){variant box=args; assert(variant_is<string[]>(box));}");
	Run("void main(){queue<variant> q=queue<variant>();q.push(7);assert(cast<int>(q.pop())==7);stack<variant> s=stack<variant>();s.push(8);assert(cast<int>(s.pop())==8);deque<variant> d=deque<variant>();d.push_back(9);assert(cast<int>(d.pop_front())==9);}");
	Run("int variant_is(int n) { return n; }", false);
	Run("using variant = int;", false);
	Run("variant f<variant>(variant x) { return x; }", false);
	Run("variant v = 1; switch(v) { default: break; }", false);
	Run("import \"concurrent.vs\"; variant global = 1; void work(){} std::concurrent::spawn(work);", false);
	Run("import \"concurrent.vs\"; void work(variant v){} std::concurrent::spawn(work, 1);", false);
	{
		std::ostringstream Stream;
		VeroScriptVm Vm(Stream);
		VeroScript::Test::Configure(Vm);
		FBytecodeModule Module;
		FValue Box, Copy;
		if (!Vm.MakeVariant(Module, "int", std::int32_t{42}, Box))
		{
			Ok = false;
		}
		for (const auto* Name : {"variant", "std::variant", "std.variant", " variant "})
		{
			if (!Vm.MakeVariant(Module, Name, Box, Copy) || std::get<FScriptVariantHandle>(Copy) != std::get<FScriptVariantHandle>(Box))
			{
				std::cerr << "canonical variant copy must preserve the flat box\n";
				Ok = false;
			}
			const FValue Empty = Variant::Empty();
			if (!Vm.MakeVariant(Module, Name, Empty, Copy) || !Variant::IsVariant(Copy) || Variant::HasValue(Copy))
			{
				std::cerr << "canonical empty variant copy\n";
				Ok = false;
			}
		}
		Copy = std::int32_t{73};
		if (Vm.MakeVariant(Module, "std::variant", std::int32_t{42}, Copy) || std::get<std::int32_t>(Copy) != 73)
		{
			std::cerr << "variant copy must reject an unboxed source without writing output\n";
			Ok = false;
		}
		FValue NaNBox;
		if (!Vm.MakeVariant(Module, "double", std::numeric_limits<double>::quiet_NaN(), NaNBox) || Runtime::ValuesEqual(Module, NaNBox, NaNBox))
		{
			std::cerr << "variant NaN equality\n";
			Ok = false;
		}
		FHostBindingRegistry Bindings;
		FHostTypeId Type;
		std::string Error;
		const auto Native = [](const FNativeCallContext&, FValue& Out, std::string&)
		{
			Out = std::int32_t{1};
			return true;
		};
		for (const auto* Name : {"variant", "variant_is", "std::variant_has_value", "std.variant_type_name"})
		{
			if (Bindings.RegisterNative(Name, ENativeType::Int, {}, Native, nullptr, Error))
			{
				std::cerr << "native intrinsic name collision\n";
				Ok = false;
			}
		}
		if (!Bindings.RegisterNative("Example.variant_is", ENativeType::Int, {}, Native, nullptr, Error))
		{
			std::cerr << "qualified user callable was reserved\n";
			Ok = false;
		}
		if (!Bindings.RegisterType("Packet", EHostTypeKind::Struct, Type, Error))
		{
			std::cerr << Error;
			Ok = false;
		}
		auto Payload = std::make_shared<FHostObject>();
		Payload->TypeName = "Packet";
		Payload->TypeId = Type.Value;
		int CloneCalls = 0;
		Payload->CloneUserData = &CloneCalls;
		Payload->CloneCallback = [](const FNativeCallContext& Context, FValue&, std::string& Failure)
		{
			++*static_cast<int*>(Context.UserData);
			Failure = "forced clone failure";
			return false;
		};
		Vm.SetMemoryHostBindings(&Bindings);
		FValue Output = std::int32_t{73};
		if (Vm.MakeVariant(Module, "Packet", FValue(Payload), Output) || CloneCalls != 1 || std::get<std::int32_t>(Output) != 73)
		{
			std::cerr << "failed boxing must preserve output and report clone failure\n";
			Ok = false;
		}
	}
	{
		std::ostringstream Out, Err;
		FStreamEngineHost Host(Out, Err);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.ApplyConfig(Config);
		Session.SetExecutionBackend(EExecutionBackend::Vm);
		Session.SetHotReloadEnabled(true);
		const auto Require = [&](bool Result, const char* Name)
		{
			if (!Result)
			{
				std::cerr << Name << '\n'
				          << Err.str();
				Ok = false;
			}
			return Result;
		};
		Require(Session.LoadSource(R"VS(
int destroyed = 0;
class Trace { public: variant next; ~Trace(){destroyed += 1;} };
variant make(){return Trace();}
variant cycle(){Trace n=Trace();variant box=n;n.next=box;return box;}
int count(){return destroyed;}
)VS",
		                   "variant-roots.vs")
		            .bSuccess,
		    "load external roots");
		FValue Value;
		FValueRoot Root, Copy;
		std::string Error;
		Require(Session.CallFunction("make", {}, &Value).bSuccess, "export boxed class");
		Require(Session.RetainValue(Value, Root, Error) && Session.RetainValue(Value, Copy, Error), "retain independent external root slots");
		{
			VeroScriptDebugger Debugger;
			FDebugVariable Variable;
			Variable.bHasRawValue = true;
			Variable.RawValue = *Root.Read();
			const auto Children = Debugger.GetChildren(Variable);
			Require(Children.size() == 3 && Children[0].Name == "$present" && Children[1].Name == "$type" && Children[2].Name == "$value" && Children[2].bReadOnly, "debug variant children");
			if (Children.size() == 3)
			{
				for (const auto& Field : Debugger.GetChildren(Children[2]))
				{
					Require(Field.bReadOnly && Field.TargetKind == FDebugVariable::ETargetKind::None, "debug payload descendants are read-only");
				}
			}
		}
		Value = std::int32_t{0};
		Require(!Session.LoadSource("int x = 0;", "replacement.vs").bSuccess, "live roots prevent module replacement");
		Root.Reset();
		Require(Session.CallFunction("count", {}, &Value).bSuccess && std::get<std::int32_t>(Value) == 0, "shared box remains live");
		std::thread Retire([&]
		{
			Copy.Reset();
		});
		Retire.join();
		Require(Session.CallFunction("count", {}, &Value).bSuccess && std::get<std::int32_t>(Value) == 1, "owner safe point runs the destructor once");
		Require(Session.CallFunction("cycle", {}, &Value).bSuccess && Session.RetainValue(Value, Root, Error), "retain variant cycle");
		Value = std::int32_t{0};
		Root.Reset();
		Require(Session.CallFunction("count", {}, &Value).bSuccess && std::get<std::int32_t>(Value) == 2, "external root cleanup breaks variant cycles");
		Require(Session.LoadSource("int replacement = 1;", "replacement.vs").bSuccess, "retired roots allow replacement");
	}
	{
		std::ostringstream Out, Err;
		FStreamEngineHost Host(Out, Err);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.ApplyConfig(Config);
		Session.SetExecutionBackend(EExecutionBackend::Vm);
		Session.SetHotReloadEnabled(true);
		FValueRoot Held, Returned;
		std::string Error;
		Session.SetExecutionBackend(Config.JitBackendFactory ? EExecutionBackend::Jit : EExecutionBackend::Vm);
		const auto Require = [&](bool Good, const char* Label)
		{
			if (!Good)
			{
				std::cerr << Label << '\n'
				          << Err.str();
				Ok = false;
			}
			return Good;
		};
		Require(Session.RegisterNative("take_root", ENativeType::Variant, {},
		            [](const FNativeCallContext& Context, FValue& Value, std::string&)
		{
			auto& Root = *static_cast<FValueRoot*>(Context.UserData);
			if (!Root.Read()) return false;
			Value = *Root.Read();
			Root.Reset();
			return true;
		}, &Held),
		    "register returning root retirement");
		Require(Session.LoadSource("int destroyed=0; class Item{public:~Item(){destroyed+=1;}}; variant make(){return Item();} variant relay(){return take_root();} int consume(variant value){take_root();return 0;} int count(){return destroyed;}", "variant-transit.vs").bSuccess,
		    "load returning root retirement");
		FValue Value;
		Require(Session.CallFunction("make", {}, &Value).bSuccess && Session.RetainValue(Value, Held, Error), "retain transit source");
		Value = std::int32_t{0};
		Require(Session.CallFunction("relay", {}, &Value).bSuccess && !Variant::TypeName(Value).empty() && Session.RetainValue(Value, Returned, Error), "return graph survives root retirement before caller retention");
		Value = std::int32_t{0};
		Require(Session.CallFunction("count", {}, &Value).bSuccess && std::get<std::int32_t>(Value) == 0, "returned object remains alive");
		Returned.Reset();
		Require(Session.CallFunction("count", {}, &Value).bSuccess && std::get<std::int32_t>(Value) == 1, "returned object is eventually destroyed once");
		Require(Session.CallFunction("make", {}, &Value).bSuccess && Session.RetainValue(Value, Held, Error), "retain borrowed input source");
		Value = std::int32_t{0};
		const std::vector<FValue> Borrowed{*Held.Read()};
		Require(Session.CallFunction("consume", Borrowed, &Value).bSuccess, "consume borrowed input");
		Require(Session.CallFunction("count", {}, &Value).bSuccess && std::get<std::int32_t>(Value) == 2,
		    "input-only borrows expire at the call boundary without leaking their last retired root");
	}
	{
		const std::string Source = "void main(){variant v=9;assert(variant_is<int>(v));assert(cast<int>(v)==9);}";
		VeroScriptLexer Lexer(Source, "variant-bytecode.vs");
		const auto& Tokens = Lexer.Tokenize();
		VeroScriptParser Parser(Tokens, "variant-bytecode.vs");
		auto Program = Parser.ParseProgram();
		VeroScriptCompiler Compiler;
		auto Module = Program ? Compiler.Compile(*Program) : nullptr;
		if (!Module)
		{
			std::cerr << "compile bytecode fixture\n";
			Ok = false;
		}
		else
		{
			std::vector<std::uint8_t> Bytes;
			std::string Error;
			const bool Serialized = VeroScriptBytecodeIO::SerializeBytecode(*Module, 9, Bytes, Error);
			auto Loaded = Serialized ? VeroScriptBytecodeIO::DeserializeBytecode(FByteView(Bytes), 9, Error) : nullptr;
			std::ostringstream Stream;
			VeroScriptVm Vm(Stream);
			VeroScript::Test::Configure(Vm);
			if (!Loaded || !Vm.Execute(*Loaded, ERunMode::Script))
			{
				std::cerr << "variant bytecode round trip: " << Error;
				Ok = false;
			}
			bool Tampered = false;
			for (const auto& F : Module->Functions)
			{
				for (std::size_t Ip = 0; Ip < F.Code.size(); Ip += Bytecode::InstructionSize(static_cast<EOpcode>(F.Code[Ip])))
				{
					if (static_cast<EOpcode>(F.Code[Ip]) == EOpcode::BoxVariant)
					{
						Module->StringPool[Bytecode::ReadU16LE(F.Code.data() + Ip + 1)] = "variant";
						Tampered = true;
						break;
					}
				}
				if (Tampered)
				{
					break;
				}
			}
			if (!Tampered || VeroScriptBytecodeIO::ValidateModule(*Module, Error))
			{
				std::cerr << "invalid variant operand accepted\n";
				Ok = false;
			}
		}
	}
	{
		Tests::FScopedTemporaryDirectory Directory("variant-reload-");
		const auto FilePath = Directory.Get() / "reload.vs";
		const auto Write = [&](const std::string& Text)
		{
			std::ofstream File(FilePath, std::ios::binary | std::ios::trunc);
			File << Text;
			return static_cast<bool>(File);
		};
		const std::string Source = "enum class Choice{Ready=4}; variant tagged=Choice::Ready; class Item{public:int x=7;}; variant saved=Item(); array<variant> values={saved}; int read(){return cast<Item>(values[0]).x+cast<int>(tagged)-4;}";
		std::ostringstream Out, Err;
		FStreamEngineHost Host(Out, Err);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.ApplyConfig(Config);
		Session.SetExecutionBackend(EExecutionBackend::Vm);
		Session.SetHotReloadEnabled(true);
		FValue Value;
		const auto Check = [&](bool Good, const char* Label)
		{
			if (!Good)
			{
				std::cerr << Label << '\n'
				          << Err.str();
				Ok = false;
			}
			return Good;
		};
		Check(Directory && Write(Source) && Session.LoadFile(PathUtil::ToUtf8(FilePath)).bSuccess && Session.Run(ERunMode::Eval).bSuccess, "prepare variant reload");
		Check(Session.CallFunction("read", {}, &Value).bSuccess && std::get<std::int32_t>(Value) == 7, "before reload");
		const std::string Replacement = "enum class Choice{Ready=4}; variant tagged=Choice::Ready; class Item{public:int x=7;int added;}; variant saved=Item(); array<variant> values={saved}; int read(){return cast<Item>(values[0]).x + cast<Item>(saved).added+cast<int>(tagged)-4;}";
		Check(Write(Replacement) && Session.Reload().bSuccess && Session.Run(ERunMode::Eval).bSuccess, "migrate boxes and shared container slots");
		Check(Session.CallFunction("read", {}, &Value).bSuccess && std::get<std::int32_t>(Value) == 7, "after reload");
		const std::string Failure = Replacement + " int fail(){for(int i=0;i<2000;i++){values.push(i);} assert(false);return 0;} int doomed=fail();";
		Check(Write(Failure) && Session.Reload().bSuccess && !Session.Run(ERunMode::Eval).bSuccess, "failed reload rolls back resized variant containers");
		Check(Session.CallFunction("read", {}, &Value).bSuccess && std::get<std::int32_t>(Value) == 7, "rollback restores original box generation");
	}
	return Ok ? 0 : 1;
}
