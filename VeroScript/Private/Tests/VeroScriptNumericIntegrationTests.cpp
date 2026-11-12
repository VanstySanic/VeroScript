// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptNumericBindings.h"
#include "VeroScriptCompile.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptPackage.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptVm.h"

#include <iostream>
#include <sstream>
#include <cstdlib>
#include <type_traits>

using namespace VeroScript;
namespace
{
	std::shared_ptr<const IJitBackendFactory> Factory;
	FCompileOptions Options = VeroScript::Test::MakeCompileOptions();
	unsigned Checks = 0;
	bool ValueIdentity()
	{
		const auto Check = [](const auto& Number)
		{
			++Checks;
			FValue Value;
			FNumericError Error;
			if (!Numerics::ToScriptValue(Number, Value, Error)) return false;
			const auto Original = std::get<FHostObjectHandle>(Value);
			auto Other = std::make_shared<FHostObject>(*Original);
			FValue Alias = FHostObjectHandle(Original, Other.get());
			std::decay_t<decltype(Number)> Output;
			if (Numerics::IsValue(Alias) || Numerics::FromScriptValue(Alias, Output, Error)) return false;
			const auto Payload = Original->Payload;
			Original->Payload = nullptr;
			const bool ChangedPayloadRejected = !Numerics::IsValue(Value) && !Numerics::FromScriptValue(Value, Output, Error);
			Original->Payload = Payload;
			FValue Copy;
			FNativeCallContext Context;
			Context.Args = &Value;
			Context.ArgCount = 1;
			std::string Failure;
			return ChangedPayloadRejected && Original->CloneCallback(Context, Copy, Failure) && Numerics::IsValue(Copy) && std::get<FHostObjectHandle>(Copy) != Original;
		};
		const bool Success = Check(FBigInteger{}) && Check(FRational{});
		if (!Success)
		{
			std::cerr << "numeric value identity or clone ownership mismatch\n";
		}
		return Success;
	}
	bool Run(const char* Label, std::string_view Source, const char* Failure = nullptr,
	    FNumericLimits Limits = {}, FExecutionLimits Execution = {})
	{
		++Checks;
		auto Compiled = VeroScriptCompile::CompileSource(Source, "numerics-test.vs", Options);
		if (!Compiled.bSuccess)
		{
			std::cerr << Label << ": compile " << Compiled.Error.Location.FileLabel << ':'
			          << Compiled.Error.Location.Line << ": " << Compiled.Error.Message << '\n';
			return false;
		}
		for (auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend == EExecutionBackend::Jit && !Factory)
			{
				continue;
			}
			std::vector<std::uint8_t> Bytes;
			std::string Error;
			FPackageBuildOptions PackageOptions;
			PackageOptions.PackageName = "numeric-test";
			FPackageData Package;
			if (!BuildPackage(*Compiled.Module, PackageOptions, Bytes, Error) ||
			    !ReadPackage(FByteView(Bytes), Package, Error))
			{
				std::cerr << Label << ": package " << Error << '\n';
				return false;
			}
			std::ostringstream Output, Errors;
			std::istringstream Input;
			VeroScriptVm Vm(Output, Input, Errors);
			VeroScript::Test::Configure(Vm);
			Vm.SetNumericLimits(Limits);
			Vm.SetExecutionLimits(Execution);
			Vm.SetExecutionBackend(Backend);
			if (Factory && !Vm.SetJitBackendFactory(Factory, Error))
			{
				std::cerr << Error;
				return false;
			}
			const bool Success = Vm.Execute(*Package.Module, ERunMode::Script);
			if (Success != !Failure || (Failure && *Failure && Vm.GetError().MessageId != Failure))
			{
				std::cerr << Label << ": backend " << static_cast<int>(Backend) << ": "
				          << Vm.GetError().Location.Line << ": " << Vm.GetError().MessageId << ": "
				          << Vm.GetError().Message << '\n';
				return false;
			}
		}
		return true;
	}
	bool NativeServices()
	{
		++Checks;
		FNativeFunction Native;
		Native.Name = "host_numeric_services";
		Native.ReturnType = ENativeType::Int;
		Native.Callback = [](const FNativeCallContext& Call, FValue& Out, std::string& Error)
		{
			if (!Call.NumericServices)
			{
				Error = "missing active numeric services";
				return false;
			}
			auto Context = Call.NumericServices->GetNumericOperationContext();
			FBigInteger Integer, Result;
			FNumericError E;
			if (!Numerics::TryParse("2", Integer, Context, E) ||
			    !Numerics::TryPow(Integer, 100, Result, Context, E))
			{
				Error = E.MessageId;
				return false;
			}
			Out = std::int32_t(Result.BitLength() == 101 ? 42 : 0);
			return true;
		};
		std::vector<FNativeFunction> Natives{Native};
		auto Config = Options;
		Config.Natives = &Natives;
		auto Compiled = VeroScriptCompile::CompileSource(
		    "int sample(){return host_numeric_services();}void main(){assert(sample()==42);}",
		    "numeric-native.vs", Config);
		if (!Compiled.bSuccess)
		{
			std::cerr << Compiled.Error.Message;
			return false;
		}
		for (auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend == EExecutionBackend::Jit && !Factory)
			{
				continue;
			}
			std::ostringstream Out, Errors;
			std::istringstream Input;
			VeroScriptVm Vm(Out, Input, Errors);
			VeroScript::Test::Configure(Vm);
			Vm.SetExecutionBackend(Backend);
			std::string Error;
			if (Factory && !Vm.SetJitBackendFactory(Factory, Error))
			{
				return false;
			}
			if (!Vm.Execute(*Compiled.Module, ERunMode::Script, {}, &Natives))
			{
				std::cerr << Vm.GetError().Message;
				return false;
			}
			if (Backend == EExecutionBackend::Jit &&
			    (Factory->GetDescription().Capabilities & JitDirectScalarCallsCapability) &&
			    Vm.GetJitStats().ScalarNativeFastPathCalls == 0)
			{
				std::cerr << "numeric test did not enter scalar JIT native bridge";
				return false;
			}
		}
		return true;
	}
	bool Reject(std::string_view Source)
	{
		++Checks;
		auto Compiled = VeroScriptCompile::CompileSource(Source, "numeric-reject.vs", Options);
		if (Compiled.bSuccess)
		{
			std::cerr << "Unexpected compile success: " << Source << '\n';
			return false;
		}
		return true;
	}
} // namespace
int main(int Argc, char** Argv)
{
	Options.StdlibSearchPaths = {VEROSCRIPT_TEST_STDLIB};
	const char* BackendPath = Argc > 1 ? Argv[1] : std::getenv("VEROSCRIPT_TEST_JIT_BACKEND");
	if (BackendPath && *BackendPath)
	{
		std::string Error;
		Factory = LoadJitBackend(BackendPath, Error);
		if (!Factory)
		{
			std::cerr << Error;
			return 1;
		}
	}
	bool Ok = ValueIdentity();
	Ok &= Run("arithmetic and exact constructors", R"VS(
import "rational.vs";
void main() {
    auto a = std::bigint("18446744073709551616");
    assert(a + std::bigint(1) == std::bigint("18446744073709551617"));
    assert(std::bigint(18446744073709551615u64).to_uint64() == 18446744073709551615u64);
    assert(std::rational(-7, 3) % std::rational(2, 3) == std::rational(-1, 3));
    assert(std::rational(2u64, -4i64).to_string() == "-1/2");
    assert(std::bigint(-3) >> 1 == std::bigint(-2));
    assert(~std::bigint(-3) == std::bigint(2));
    auto dm = std::numeric::divmod(std::bigint(-7), std::bigint(3));
    assert(dm.first == std::bigint(-2) && dm.second == std::bigint(-1));
    assert(std::numeric::gcd(std::bigint(-60), std::bigint(24)) == std::bigint(12));
    assert(std::numeric::lcm(std::bigint(-60), std::bigint(24)) == std::bigint(120));
    assert(std::numeric::pow(std::bigint(2), 100).bit_length() == 101i64);
    assert(std::numeric::pow(std::rational(2, 3), -2) == std::rational(9, 4));
})VS");
	Ok &= Run("parse and transactional try outputs", R"VS(
void main() {
    std::bigint value = std::bigint(19);
    assert(!std::bigint::try_parse("not a number", ref value)); assert(value == std::bigint(19));
    assert(std::bigint::try_parse_base("FF", ref value, 16)); assert(value.to_string_base(2) == "11111111");
    auto r = std::rational::parse("1.250e-1"); assert(r == std::rational(1,8));
    assert(r.to_decimal(4) == "0.1250");
    assert(std::rational(5,2).round() == std::bigint(2));
    assert(std::rational(-5,2).round_with_mode(std::numeric::rounding_mode::half_away_from_zero) == std::bigint(-3));
    assert(std::rational(-1,3).floor() == std::bigint(-1));
    assert(!std::rational(1,3).has_finite_decimal());
    int64 i = 37i64; assert(!std::bigint("9223372036854775808").try_to_int64(ref i)); assert(i==37i64);
    assert(std::rational::from_double_exact(0.1).to_string() == "3602879701896397/36028797018963968");
    assert(std::bigint::from_double_trunc(-1.9).to_int64() == -1i64);
})VS");
	Ok &= Run("value copies, containers, parameters and formatting", R"VS(
import "core.vs";
std::bigint increment(std::bigint x) { return x + std::bigint(1); }
void update(ref std::bigint x) { x += std::bigint(1); }
struct Box { public: std::bigint n=std::bigint(0); };
void main() {
    std::bigint a=std::bigint("9007199254740993"); std::bigint b=a; b+=std::bigint(1);
    assert(a.to_string()=="9007199254740993" && b==increment(a));
    update(ref b); assert(b==a+std::bigint(2));
    array<std::bigint> values={a,b}; values[0]+=std::bigint(3); assert(a!=values[0]);
    map<std::bigint,string> names={}; names[a]="value"; assert(names[std::bigint("9007199254740993")]=="value");
    set<std::rational> fractions={}; fractions.add(std::rational(1,2)); assert(fractions.has(std::rational(2,4)));
    Box box=Box(); box.n=a; Box copy=box; copy.n+=std::bigint(1); assert(box.n==a);
    std::attribute<std::bigint> attr=a; assert(attr.get()==a);
    assert(std::format("{} {}", a, std::rational(1,3))=="9007199254740993 1/3");
    assert(std::to_int64(a)==9007199254740993i64 && std::to_double(std::rational(1,2))==0.5);
    array<std::rational> items={std::rational(1,3), std::rational(1,6)};
    assert(std::numeric::sum(items)==std::rational(1,2));
    assert(std::numeric::average(items)==0.25);
    print(a);
})VS");
	Ok &= Run("division by zero", "void main(){std::bigint(1)/std::bigint();}",
	    "diag.numeric_division_by_zero");
	Ok &= Run("worker local arithmetic", R"VS(import "concurrent.vs";
string work(){return (std::bigint("18446744073709551616")+std::bigint(1)).to_string();}
void main(){auto t=std::concurrent::spawn_task<string>(work);assert(t.await()=="18446744073709551617");})VS");
	Ok &= Run("fallback does not repeat side effects", R"VS(
int calls=0;
std::bigint compute(){calls++;return std::bigint("123456789012345678901234567890")*std::bigint(3);}
void main(){std::bigint v=compute();assert(calls==1);assert(v.to_string()=="370370367037037036703703703670");})VS");
	Ok &= Run("worker inherits numeric limits", R"VS(import "concurrent.vs";
string work(){return std::bigint(256).to_string();}
void main(){auto t=std::concurrent::spawn_task<string>(work);t.await();})VS",
	    "", {8});
	Ok &=
	    Run("native work contributes to instruction budget",
	        "import \"bigint.vs\"; void main(){std::numeric::pow(std::bigint(3), 10000);}", "", {}, {100, 0});
	Ok &= Run("reflection preserves numeric fields and arguments", R"VS(
struct NumberBox {
public:
    std::bigint value=std::bigint(0);
    std::bigint add(std::bigint amount){value+=amount;return value;}
};
void main(){
    NumberBox box=NumberBox(); type<NumberBox> info=type<NumberBox>();
    field_info<NumberBox> field=field_info<NumberBox>(info.qualified_name,0);
    field.set<std::bigint>(box, std::bigint("18446744073709551616"));
    assert(field.get<std::bigint>(box)==std::bigint("18446744073709551616"));
    int index=-1;
    for(int i=0;i<info.method_count();i++){method_info<NumberBox> m=method_info<NumberBox>(info.qualified_name,i);if(m.name()=="add")index=i;}
    method_info<NumberBox> method=method_info<NumberBox>(info.qualified_name,index);
    assert(method.invoke<std::bigint>(box,std::bigint(1))==std::bigint("18446744073709551617"));
})VS");
	Ok &= Run("invalid parse propagates", "void main(){std::rational::parse(\"1/0\");}",
	    "diag.numeric_invalid_text");
	Ok &= Run("exact float error", "void main(){std::rational(1,3).to_double_exact();}",
	    "diag.numeric_inexact_conversion");
	Ok &= Run("source count range before narrowing",
	    "import \"bigint.vs\";void main(){std::numeric::pow(std::bigint(2),18446744073709551615u64);}",
	    "diag.numeric_out_of_range");
	Ok &= Run("base range before narrowing", "void main(){std::bigint::parse_base(\"1\",4294967306u64);}",
	    "diag.numeric_out_of_range");
	Ok &= Run("global initializer limit", "std::bigint x=std::bigint(256);void main(){}",
	    "diag.numeric_limit", {8});
	Ok &= Run("constructor normalizes before final size check",
	    "void "
	    "main(){assert(std::rational(256,256)==std::rational(1));assert(std::bigint::from_double_trunc("
	    "0.1).is_zero());}",
	    nullptr, {8});
	FNumericLimits Work;
	Work.MaxWorkUnits = 20;
	Ok &= Run("try parse does not hide resource limit",
	    "void main(){std::bigint "
	    "n=std::bigint();std::bigint::try_parse(\"1234567890123456789012345678901234567890\", ref n);}",
	    "diag.numeric_limit", Work);
	Ok &= Reject("void main(){std::bigint(1.5);}");
	Ok &= Reject("T identity<T>(T x){return x;} U identity<U>(U x){return x;} void main(){}");
	Ok &= Reject("void main(){std::bigint(true);}");
	Ok &= Reject("void main(){__std_numeric_bigint_pow(std::bigint(2),3);}");
	Ok &= Reject("void main(){std::bigint::from_double_exact(1);}");
	Ok &= Reject("void main(){std::bigint(1)<<1.5;}");
	Ok &= Reject("void main(){std::rational(1,2).to_decimal(1.5);}");
	Ok &= Reject("import \"bigint.vs\";void main(){std::numeric::pow(std::bigint(2),1.5);}");
	Ok &= NativeServices();
	std::cout << Checks << " numeric integration scenarios " << (Ok ? "passed" : "failed") << '\n';
	return Ok ? 0 : 1;
}
