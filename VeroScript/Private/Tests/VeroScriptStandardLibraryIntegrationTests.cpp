// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "VeroScriptEngine.h"
#include "VeroScriptPath.h"
#include "VeroScriptHost.h"
#include "VeroScriptEnvironment.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptSession.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptTestTemporaryDirectory.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <algorithm>

using namespace VeroScript;

namespace
{
	EExecutionBackend Backend = EExecutionBackend::Vm;
	std::shared_ptr<const IJitBackendFactory> Factory;
	bool Run(std::string_view Source, std::string Input = {}, FStandardLibraryLimits Limits = {}, bool Success = true,
	    FExecutionLimits ExecutionLimits = {})
	{
		std::ostringstream Output, Errors;
		std::istringstream In(Input);
		FStreamEngineHost Host(Output, Errors);
		FEngineConfig Config;
		Config.ExecutionBackend = Backend;
		Config.JitBackendFactory = Factory;
		Config.bUseBytecodeCache = false;
		Config.InputStream = &In;
		Config.StdlibSearchPaths = {VEROSCRIPT_TEST_STDLIB};
		Config.StandardLibraryLimits = Limits;
		Config.ExecutionLimits = ExecutionLimits;
		const auto Result = VeroScript::Test::FEngine::RunSource(Source, "foundation.vs", Host, ERunMode::Script, Config);
		if (Result.bSuccess != Success)
		{
			std::cerr << Errors.str() << '\n'
			          << Source << '\n';
			return false;
		}
		return true;
	}
	bool CheckSessionFailures()
	{
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FEngineConfig Config;
		Config.ExecutionBackend = Backend;
		Config.JitBackendFactory = Factory;
		Config.StdlibSearchPaths = {VEROSCRIPT_TEST_STDLIB};
		Config.bUseBytecodeCache = false;
		Session.ApplyConfig(Config);
		Session.SetHotReloadEnabled(true);
		const auto Require = [&](bool Value, const char* Message)
		{
			if (!Value)
			{
				std::cerr << Message << '\n'
				          << Errors.str();
			}
			return Value;
		};
		if (!Require(Session.LoadSource(R"VS(import "concurrent.vs"; import "algorithm.vs";
task_group<int> group = task_group<int>();
array<int> values = array<int>();
channel<int64> cell = std::concurrent::make_synchronized<int64>(9223372036854775807i64);
task<string> saved = nullptr;
namespace Local { class task { public: double await_for(double value){return value;} }; }
Local::task ordinary = Local::task();
int good(int n){return n;}
int bad(){assert(false,"foundation worker failure"); return 0;}
string text(){return "result";}
void partial(){
    auto a=std::concurrent::spawn_task<int>(good,1);
    auto b=std::concurrent::spawn_task<int>(bad);
    auto c=std::concurrent::spawn_task<int>(good,3);
    while(!a.done() || !b.done() || !c.done()){std::this_thread::sleep(1);}
    group.add(a); group.add(b); group.add(c); group.await_all();
}
int64 pending(){return group.size();}
int retry(){array<int> result=group.await_all(); return result[0];}
void fill(){for(int i=0;i<1000;i++){values.push(i);}}
void reorder(){std::algorithm::reverse(values);}
array<int> sort_values = {9,8,7,6,5,4,3,2,1,0};
void sort_descending(){std::algorithm::sort(sort_values);}
int sort_first(){return sort_values[0];}
void overflow(){std::concurrent::synchronized_fetch_add(cell,1i64);}
int64 read_cell(){return std::concurrent::synchronized_load(cell);}
void prepare(){saved=std::concurrent::spawn_task<string>(text); while(!saved.done()){std::this_thread::sleep(1);}}
string consume(){return saved.await();}
)VS",
		                        "foundation-session.vs")
		                 .bSuccess,
		        "load session failure contracts"))
		{
			return false;
		}
		FValue Value;
		bool Ok = Require(!Session.CallFunction("partial", {}).bSuccess, "partial group failure") && Require(Session.CallFunction("pending", {}, &Value).bSuccess && std::get<std::int64_t>(Value) == 1, "group retains only remaining work") && Require(Session.CallFunction("retry", {}, &Value).bSuccess && std::get<std::int32_t>(Value) == 3, "remaining group task can be consumed across VM calls");
		Ok &= Require(!Session.CallFunction("overflow", {}).bSuccess, "checked synchronized overflow") && Require(Session.CallFunction("read_cell", {}, &Value).bSuccess && std::get<std::int64_t>(Value) == INT64_MAX, "overflow preserves synchronized payload");
		Ok &= Require(Session.CallFunction("prepare", {}).bSuccess, "retain completed task ticket");
		(void)Session.SetStandardLibraryLimits({1, 0, 0});
		Ok &= Require(!Session.CallFunction("consume", {}).bSuccess, "task result quota fails before consumption");
		(void)Session.SetStandardLibraryLimits({});
		Ok &= Require(Session.CallFunction("consume", {}, &Value).bSuccess && std::get<std::string>(Value) == "result", "quota failure preserves active task result");
		if (Session.GetGlobal("ordinary", Value).bSuccess)
		{
			const auto Object = std::get<FObjectHandle>(Value);
			Ok &= Require(Session.CallMethod(Object, "await_for", {1.5}, &Value).bSuccess && std::get<double>(Value) == 1.5,
			    "user task short names do not acquire standard-library argument roles");
		}
		else
		{
			Ok = false;
		}
		{
			Session.SetExecutionLimits({});
			Session.SetMutationCallback({});
			Ok &= Require(Session.CallFunction("fill", {}).bSuccess, "prepare reorder sequence");
			if (!Require(Session.GetGlobal("values", Value).bSuccess, "retain reorder sequence")) return false;
			auto Array = std::get<FDynamicArrayHandle>(Value);
			Session.SetExecutionLimits({40, 0});
			Ok &= Require(!Session.CallFunction("reorder", {}).bSuccess, "interrupt native reverse between swaps");
			std::vector<bool> Seen(1000);
			for (std::size_t I = 0; I < Array->ElementCount(); ++I)
			{
				FValue Item;
				if (!Array->TryGetScalar(I, Item))
				{
					Item = Array->Elements[I];
				}
				std::int64_t N = 0;
				if (!Runtime::TryGetInteger64(Item, N) || N < 0 || N >= 1000 || Seen[static_cast<std::size_t>(N)])
				{
					Ok = false;
					break;
				}
				Seen[static_cast<std::size_t>(N)] = true;
			}
			Ok &= Require(Array->ElementCount() == 1000 && std::all_of(Seen.begin(), Seen.end(), [](bool B)
			{
				return B;
			}),
			    "interrupted reverse preserves the multiset");
			Array->MaterializeBoxedElements();
			std::vector<std::int32_t> Before;
			for (const auto& Item : Array->Elements)
			{
				Before.push_back(std::get<std::int32_t>(Item));
			}
			Session.SetMutationCallback([](const FMutationEvent&)
			{
			});
			bool Same = !Session.CallFunction("reorder", {}).bSuccess;
			for (std::size_t I = 0; I < Before.size(); ++I)
			{
				Same &= std::get<std::int32_t>(Array->Elements[I]) == Before[I];
			}
			Ok &= Require(Same, "observer preparation failure preserves original sequence");
		}
		Session.SetMutationCallback({});
		Session.SetExecutionLimits({24, 0});
		Ok &= Require(!Session.CallFunction("sort_descending", {}).bSuccess,
		    "governed sort retains its instruction boundary");
		Session.SetExecutionLimits({});
		Ok &= Require(Session.CallFunction("sort_first", {}, &Value).bSuccess &&
		    std::get<std::int32_t>(Value) == 9, "early sort interruption leaves the first element untouched");
		Ok &= Require(Session.CallFunction("sort_descending", {}).bSuccess &&
		    Session.CallFunction("sort_first", {}, &Value).bSuccess && std::get<std::int32_t>(Value) == 0,
		    "ordinary sort mutates the existing array");
		return Ok;
	}
}

int RunSuite()
{
	bool Ok = CheckSessionFailures();
	constexpr std::string_view TextValues = R"VS(import "string.vs";
string change(ref string text)
{
	text = "changed";
	return "\0z";
}

void main()
{
	string original = std::strings::repeat("é\0z", 32);
	string snapshot = original;
	string suffix = "\0λ";
	string combined = original;
	combined += suffix;
	assert(combined == original + suffix);
	combined += combined;
	assert(combined == (original + suffix) + (original + suffix));

	wstring wide_suffix = L"水\0😀";
	wstring wide = L"λ";
	wide += wide_suffix;
	assert(len(wide) == 5 && wide == L"λ水\0😀");
	wide += wide;
	assert(len(wide) == 10 && wide == L"λ水\0😀λ水\0😀");

	try
	{
		assert(std::strings::find(original, original) == 0);
		assert(std::strings::find(original, "\0z") == 2);
		assert(std::strings::last_find(original, "\0z") == 126);
		assert(std::strings::starts_with(original, "é\0"));
		assert(std::strings::ends_with(original, "\0z"));
		string caller = original;
		assert(std::strings::find(caller, change(ref caller)) == 2);
		assert(caller == "changed");
	}
	finally
	{
		assert(original == snapshot);
	}
})VS";
	Ok &= Run(TextValues);
	Ok &= Run(TextValues, {}, {}, true, {100000, 0});
	const auto OverLimitText = std::string{"import \"string.vs\"; void main(){string text=\""} +
	    std::string(96, 'x') + "\"; std::strings::find(text,\"x\");}";
	Ok &= Run(OverLimitText);
	Ok &= Run(OverLimitText, {}, {32, 0, 0}, false);

	Ok &= Run(R"VS(
import "string.vs"; import "path.vs"; import "numeric.vs"; import "math.vs"; import "algorithm.vs";
T read_first<T>(std::memory::buffer bytes) { return std::memory::read<T>(bytes, 0); }
void main() {
    assert(std::strings::parse<int64>(" -9223372036854775808 ") == -9223372036854775807i64 - 1i64);
    assert(std::strings::parse<uint64>("18446744073709551615") == 18446744073709551615u64);
    int value = 73;
    assert(!std::strings::try_parse("+-1", ref value)); assert(value == 73);
    assert(std::strings::try_parse<int>("ff", ref value, 16)); assert(value == 255);
    assert(std::strings::parse<bool>("true"));
    assert(std::strings::parse<float>("1.5") == 1.5f);
    assert(len(std::strings::repeat("abc", 1000)) == 3000);
    assert(std::strings::last_find("aaa", "aa") == 1);
    assert(std::strings::replace("aaaaa", "aa", "x") == "xxa");
	array<string> parts = std::strings::split("a,,b,", ",");
	assert(len(parts) == 4); assert(parts[1] == ""); assert(std::strings::join(parts, ",") == "a,,b,");
    assert(std::strings::starts_with("abc", "ab")); assert(std::strings::ends_with("abc", "bc"));
    assert(std::strings::trim("\v\f a \r\n") == "a");
    auto bytes = std::strings::encode_utf8("hello");
	assert(read_first<uint8>(bytes) == cast<uint8>(104));
    assert(std::strings::is_valid_utf8(bytes)); assert(std::strings::decode_utf8(bytes) == "hello");
    assert(std::filesystem::lexically_normal("a/./b/../", std::filesystem::path_style::posix) == "a/");
    assert(std::filesystem::root_path("C:a", std::filesystem::path_style::windows) == "C:");
    assert(!std::filesystem::is_absolute("C:a", std::filesystem::path_style::windows));
    assert(std::filesystem::is_absolute("//server/share/a", std::filesystem::path_style::windows));
    array<int> values = {2147483647, 1};
    assert(std::numeric::sum_as<int64>(values) == 2147483648i64);
    assert(std::clamp<uint64>(18446744073709551615u64, 0u64, 18446744073709551614u64) == 18446744073709551614u64);
	std::algorithm::reverse(values); assert(values[0] == 1);
	std::algorithm::rotate(values, 1); assert(values[0] == 2147483647);
	array<bool> flags = {true, false, false}; std::algorithm::reverse(flags); assert(!flags[0] && flags[2]);
	array<int64> signed_values = {9007199254740993i64, -3i64, 9007199254740992i64, -3i64};
	array<int64> signed_alias = signed_values;
	std::algorithm::sort(signed_values);
	assert(signed_alias[0] == -3i64 && signed_alias[1] == -3i64 &&
	    signed_alias[2] == 9007199254740992i64 && signed_alias[3] == 9007199254740993i64);
	array<uint64> unsigned_values = {18446744073709551615u64, 9223372036854775808u64, 0u64};
	std::algorithm::sort(unsigned_values);
	assert(unsigned_values[0] == 0u64 && unsigned_values[1] == 9223372036854775808u64 &&
	    unsigned_values[2] == 18446744073709551615u64);
	array<int> empty = array<int>(); array<int> single = {7};
	std::algorithm::sort(empty); std::algorithm::sort(single);
	assert(len(empty) == 0 && len(single) == 1 && single[0] == 7);
	array<double> decimals = {4.5, -2.5, 1.25, -2.5}; std::algorithm::sort(decimals);
	assert(decimals[0] == -2.5 && decimals[1] == -2.5 && decimals[2] == 1.25 && decimals[3] == 4.5);
}
)VS");
	Ok &= Run(R"VS(import "concurrent.vs";
int twice(int value) { return value * 2; }
int gated(channel<bool> gate) { std::concurrent::recv(gate); return 19; }
void main() {
    auto lock = std::concurrent::make_mutex();
    std::concurrent::mutex_lock(lock);
    assert(!std::concurrent::mutex_try_lock(lock));
    assert(!std::concurrent::mutex_lock_for(lock, 0));
    std::concurrent::mutex_unlock(lock);
    auto permits = std::concurrent::make_semaphore(1, 2);
    assert(std::concurrent::semaphore_try_acquire(permits));
    assert(!std::concurrent::semaphore_acquire_for(permits, 0));
    std::concurrent::semaphore_release(permits);
    auto cell = std::concurrent::make_synchronized<int64>(9007199254740993i64);
    assert(std::concurrent::synchronized_fetch_add(cell, 1i64) == 9007199254740993i64);
    assert(std::concurrent::synchronized_load(cell) == 9007199254740994i64);
    assert(std::concurrent::synchronized_exchange(cell, 8i64) == 9007199254740994i64);
    std::concurrent::synchronized_store(cell, 9i64); assert(std::concurrent::synchronized_load(cell) == 9i64);
    task<int> first = std::concurrent::spawn_task<int>(twice, 21);
    task<int> alias = first; assert(first.await() == 42); assert(!alias.valid());
    auto gate = std::concurrent::make_channel<bool>();
    task<int> waiting = std::concurrent::spawn_task<int>(gated, gate);
    assert(!waiting.await_for(0).has_value()); assert(waiting.valid());
    std::concurrent::send(gate, true); assert(waiting.await() == 19);
    task_group<int> group = task_group<int>();
    group.add(std::concurrent::spawn_task<int>(twice, 2));
    group.add(std::concurrent::spawn_task<int>(twice, 3));
    assert(group.size() == 2);
    array<int> results = group.await_all(); assert(len(results) == 2 && results[0] == 4 && results[1] == 6);
    assert(group.size() == 0); assert(len(group.await_all()) == 0);
})VS");
	Ok &= Run(R"VS(import "concurrent.vs";
void no_result(channel<int> output) {}
void main(){ auto work = std::concurrent::spawn_task<int>("no_result"); work.await(); })VS",
	    {}, {}, false);
	Ok &= Run(R"VS(import "concurrent.vs"; void main(){
auto lock=std::concurrent::make_mutex(); std::concurrent::try_send(lock,true); })VS",
	    {}, {}, false);
	Ok &= Run(R"VS(import "concurrent.vs"; void main(){
task<int> forged = task<int>(1, channel<int>()); })VS",
	    {}, {}, false);
	Ok &= Run("import \"string.vs\"; void main(){std::strings::repeat(\"x\", 1.5);}", {}, {}, false);
	Ok &= Run("import \"math.vs\"; void main(){std::clamp(1, 0.0, 2.0);}", {}, {}, false);
	Ok &= Run("void main(){format(\"{0}{0}\", \"abc\");}", {}, {5, 0, 0}, false);
	const Tests::FScopedTemporaryDirectory Temporary("VeroScript-foundation-");
	if (!Temporary) return 1;
	const auto File = PathUtil::ToUtf8(Temporary.Get() / "bytes.bin");
	const auto Empty = PathUtil::ToUtf8(Temporary.Get() / "empty.bin");
	const std::string FileSource = "import \"core.vs\"; void main(){"
	                               "auto bytes=std::strings::encode_utf8(\"a\\0b\");"
	                               "std::io::write_bytes(\"" +
	    File + "\",bytes.readonly_slice(0,3));"
	           "auto loaded=std::io::read_bytes(\"" +
	    File + "\",3);"
	           "assert(loaded.size()==3 && loaded.get(1)==0u);"
	           "std::io::append_bytes(\"" +
	    File + "\",bytes.slice(0,1));"
	           "assert(std::io::read_bytes(\"" +
	    File + "\").size()==4);"
	           "std::io::write_file(\"" +
	    Empty + "\",\"\");"
	            "assert(std::io::read_bytes(\"" +
	    Empty + "\",0).size()==0);}";
	Ok &= Run(FileSource);
	const auto Directory = PathUtil::ToUtf8(Temporary.Get());
	Ok &= Run("import \"io.vs\"; void main(){std::io::read_file(\"" + Directory + "\");}", {}, {}, false);
	Ok &= Run("import \"io.vs\"; void main(){std::io::read_bytes(\"" + Directory + "\",0);}", {}, {}, false);
	Ok &= Run("import \"io.vs\"; void main(){std::io::read_bytes(\"" + File + "\",0);}", {}, {}, false);
	Ok &= Run("import \"io.vs\"; void main(){std::io::read_bytes(\"" + File + "\");}", {}, {0, 3, 0}, false);
	for (const auto Name : {"standard_library_foundation.vs", "standard_library_algorithms.vs", "concurrent_demo.vs"})
	{
		std::ifstream Sample(std::filesystem::path(VEROSCRIPT_TEST_STDLIB).parent_path() / "examples" / Name, std::ios::binary);
		std::ostringstream Source;
		Source << Sample.rdbuf();
		Ok &= static_cast<bool>(Sample) && Run(Source.str());
	}
	Ok &= Run(R"VS(import "io.vs";
void main() {
    auto a = std::io::try_read_line(); assert(a.has_value()); assert(a.get() == "");
    auto b = std::io::try_read_line(); assert(b.has_value()); assert(b.get() == "last");
    assert(!std::io::try_read_line().has_value());
})VS",
	    "\nlast\r");
	Ok &= Run(R"VS(import "io.vs"; void main(){
assert(std::io::try_read_line().get()=="12345");
assert(std::io::read_line()=="12345");
assert(std::io::try_read_line().get()=="12345");
assert(!std::io::try_read_line().has_value());
})VS",
	    "12345\r\n12345\n12345\r", {5, 0, 0});
	Ok &= Run("import \"string.vs\"; void main(){std::strings::repeat(\"ab\",3);}", {}, {5, 0, 0}, false);
	Ok &= Run("void main(){std::io::try_read_line();}", {}, {}, false);
	const auto Old = Environment::GetUtf8("VEROSCRIPT_FOUNDATION_EMPTY_TEST");
	Ok &= Run(R"VS(void main(){
std::environment::setenv("VEROSCRIPT_FOUNDATION_EMPTY_TEST", "");
auto value = std::environment::try_getenv("VEROSCRIPT_FOUNDATION_EMPTY_TEST");
assert(value.has_value()); assert(value.get() == "");
std::environment::unsetenv("VEROSCRIPT_FOUNDATION_EMPTY_TEST");
assert(!std::environment::try_getenv("VEROSCRIPT_FOUNDATION_EMPTY_TEST").has_value());
})VS");
	if (Old)
	{
		(void)Environment::SetUtf8("VEROSCRIPT_FOUNDATION_EMPTY_TEST", *Old);
	}
	else
	{
		(void)Environment::UnsetUtf8("VEROSCRIPT_FOUNDATION_EMPTY_TEST");
	}
	return Ok ? 0 : 1;
}

int main(int Argc, char** Argv)
{
	if (Argc > 1)
	{
		std::string Error;
		Factory = LoadJitBackend(Argv[1], Error);
		if (!Factory)
		{
			std::cerr << Error << '\n';
			return 1;
		}
	}
	int Result = RunSuite();
	if (Factory)
	{
		Backend = EExecutionBackend::Mixed;
		Result |= RunSuite();
		Backend = EExecutionBackend::Jit;
		Result |= RunSuite();
	}
	return Result;
}
