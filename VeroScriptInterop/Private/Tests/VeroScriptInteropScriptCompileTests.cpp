// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInterop.h"
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace
{
	vs_interop_string_view Text(const char* S)
	{
		return {S, std::strlen(S)};
	}
	bool Check(bool Okay, const char* Stage, vs_interop_session* Session)
	{
		if (!Okay)
		{
			char Error[2048]{};
			uint64_t Size = 0;
			vs_interop_session_copy_last_error(Session, Error, sizeof(Error), &Size);
			std::cerr << "FAILED: " << Stage << " " << Error << '\n';
		}
		return Okay;
	}
	constexpr const char* Extension = R"vs(
using handler=fn(std::compiler::context,std::compiler::output)->void;
int calls=0;
handler make(){return [](std::compiler::context ctx,std::compiler::output out)->void{
 calls+=1;
 if(ctx.phase()==std::compiler::phase::types)
  out.add_declaration(ctx.top_scope(),"struct Generated{int value=1;};");
 if(ctx.phase()==std::compiler::phase::bodies){
  if(calls!=5)out.report_diagnostic("lost extension state",0);
  auto amount=ctx.configuration("amount");
  if(!amount.exists())out.report_diagnostic("missing configuration",0);
  auto ds=ctx.declarations();for(int i=0;i<len(ds);++i)
   if(ds[i].module_id()==ctx.module_id() && ds[i].name()=="pending")
    out.complete_body(ds[i].id(),"{return "+amount.value()+";}");
  out.report_diagnostic("script compile note",1);
 }
};}
export void register_extension(std::compiler::registration reg){
 std::compiler::subscription s=std::compiler::subscription();s.event=std::compiler::event::module;
 std::compiler::processor_definition d=std::compiler::processor_definition();d.id="interop.script";d.version="1";
 d.phases=array<std::compiler::phase>{std::compiler::phase::raw_source,std::compiler::phase::active_source,std::compiler::phase::types,std::compiler::phase::members,std::compiler::phase::bodies};
 d.operations=array<std::compiler::operation>{std::compiler::operation::add_declaration,std::compiler::operation::complete_body};
 d.subscriptions=array<std::compiler::subscription>{s};reg.register_processor(d,make);
}
)vs";
	constexpr const char* Source = "int pending();int answer(){Generated g=Generated();return pending()+g.value;}int main(){return answer();}";
}

int main(int Argc, char** Argv)
{
	struct FCleanup
	{
		~FCleanup()
		{
			vs_interop_package_destroy(Package);
			vs_interop_buffer_destroy(Error);
			vs_interop_buffer_destroy(Bytes);
			vs_interop_value_destroy(Value);
			if (Session)
			{
				vs_interop_session_destroy(Session);
			}
		}

		vs_interop_session* Session = nullptr;

		vs_interop_value* Value = nullptr;

		vs_interop_buffer* Bytes = nullptr;

		vs_interop_buffer* Error = nullptr;

		vs_interop_package* Package = nullptr;
	} C;
	if (vs_interop_session_create(nullptr, &C.Session) != VS_INTEROP_STATUS_OK) return 1;
	const auto CheckStage = [&](bool Okay, const char* Stage)
	{
		return Check(Okay, Stage, C.Session);
	};
	if (Argc > 1 && !CheckStage(vs_interop_session_set_vm_backend(C.Session, Text(Argv[1])) == VS_INTEROP_STATUS_OK, "script compilation VM selection"))
	{
		return 1;
	}
	if (!CheckStage((vs_interop_get_compile_capabilities() & VS_INTEROP_COMPILE_CAP_SCRIPT_EXTENSIONS) != 0,
	        "capability"))
	{
		return 1;
	}

	vs_interop_source_overlay Overlays[] = {
	    {Text("interop-builder.vs"), Text(Extension)}, {Text("interop-main.vs"), Text(Source)}};
	vs_interop_config Config{};
	Config.struct_size = sizeof(Config);
	Config.execution_backend = VS_INTEROP_BACKEND_VM;
	Config.source_overlays = Overlays;
	Config.source_overlay_count = 2;
	Config.enable_execution_control = 1;
	if (!CheckStage(vs_interop_session_apply_config(C.Session, &Config) == VS_INTEROP_STATUS_OK, "initial config")) return 1;
	vs_interop_compile_extension_root Root{Text("interop-builder.vs"), {}};
	std::string Amount = "41";
	vs_interop_compile_configuration Configuration{Text("amount"), {Amount.data(), Amount.size()}};
	vs_interop_script_compile_options Options{};
	Options.struct_size = sizeof(Options);
	Options.allow_source_imports = 1;
	Options.roots = &Root;
	Options.root_count = 1;
	Options.configuration = &Configuration;
	Options.configuration_count = 1;
	Options.max_instructions = 1000000;
	Options.timeout_milliseconds = 2000;
	#if defined(VEROSCRIPT_TEST_EMULATOR)
	Options.timeout_milliseconds = 20000;
	#endif
	if (!CheckStage(vs_interop_session_set_script_compile_options(C.Session, &Options) == VS_INTEROP_STATUS_OK, "set options")) return 1;
	Amount[0] = '9'; // Input storage belongs to the caller; the accepted configuration must be a copy.
	vs_interop_run_result Result{};
	Result.struct_size = sizeof(Result);
	const auto Load = [&](const char* Code)
	{
		return vs_interop_session_load_source(C.Session, Text(Code), Text("interop-main.vs"), &Result);
	};
	const auto Answer = [&]
	{
		int32_t Value = 0;
		return vs_interop_session_call_function(C.Session, Text("answer"), nullptr, 0, C.Value, &Result) == VS_INTEROP_STATUS_OK && vs_interop_value_get_int32(C.Value, &Value) == VS_INTEROP_STATUS_OK && Value == 42;
	};
	if (!CheckStage(vs_interop_value_create(&C.Value) == VS_INTEROP_STATUS_OK &&
	            Load(Source) == VS_INTEROP_STATUS_OK && Result.success && Result.diagnostic_count == 1,
	        "cross-phase compilation"))
	{
		return 1;
	}

	uint32_t Severity = 99;
	if (!CheckStage(
	        vs_interop_session_get_diagnostic_severity(C.Session, 0, &Severity) == VS_INTEROP_STATUS_OK &&
	            Severity == VS_INTEROP_DIAGNOSTIC_SEVERITY_WARNING && Answer(),
	        "diagnostic and generated function"))
	{
		return 1;
	}

	if (!CheckStage(vs_interop_session_apply_config(C.Session, &Config) == VS_INTEROP_STATUS_OK &&
	            Load(Source) == VS_INTEROP_STATUS_OK && Answer(),
	        "ApplyConfig preserves script extensions"))
	{
		return 1;
	}

	vs_interop_status Foreign = VS_INTEROP_STATUS_OK;
	std::thread Thread(
	    [&]
	{
		Foreign = vs_interop_session_set_script_compile_options(C.Session, nullptr);
	});
	Thread.join();
	if (!CheckStage(Foreign == VS_INTEROP_STATUS_WRONG_THREAD, "creator-thread guard")) return 1;
	const auto Reject = [&](const vs_interop_script_compile_options& Bad)
	{
		return vs_interop_session_set_script_compile_options(C.Session, &Bad) == VS_INTEROP_STATUS_INVALID_ARGUMENT && Load(Source) == VS_INTEROP_STATUS_OK && Answer();
	};
	auto Bad = Options;
	Bad.struct_size -= 1;
	if (!CheckStage(Reject(Bad), "truncated descriptor")) return 1;
	Bad = Options;
	Bad.root_count = 65;
	if (!CheckStage(Reject(Bad), "root limit")) return 1;
	Bad = Options;
	Bad.roots = nullptr;
	if (!CheckStage(Reject(Bad), "missing roots")) return 1;
	Bad = Options;
	Bad.allow_source_imports = 2;
	if (!CheckStage(Reject(Bad), "invalid boolean")) return 1;
	Bad = Options;
	Bad.reserved[0] = 1;
	if (!CheckStage(Reject(Bad), "reserved byte")) return 1;
	const vs_interop_compile_configuration Duplicates[] = {Configuration, Configuration};
	Bad = Options;
	Bad.configuration = Duplicates;
	Bad.configuration_count = 2;
	if (!CheckStage(Reject(Bad), "duplicate configuration")) return 1;
	const char NulPath[] = "interop-builder.vs\0ignored";
	vs_interop_compile_extension_root NulRoot{{NulPath, sizeof(NulPath) - 1}, {}};
	Bad = Options;
	Bad.roots = &NulRoot;
	if (!CheckStage(Reject(Bad), "embedded NUL path")) return 1;
	NulRoot = {Root.path, {NulPath, sizeof(NulPath) - 1}};
	Bad = Options;
	Bad.roots = &NulRoot;
	if (!CheckStage(Reject(Bad), "embedded NUL entry")) return 1;
	const vs_interop_string_view AllowedNul{NulPath, sizeof(NulPath) - 1};
	Bad = Options;
	Bad.allowed_roots = &AllowedNul;
	Bad.allowed_root_count = 1;
	if (!CheckStage(Reject(Bad), "embedded NUL allowed root")) return 1;
	const vs_interop_string_view DisallowedDirectory = Text("not-an-extension-directory");
	Bad = Options;
	Bad.allowed_roots = &DisallowedDirectory;
	Bad.allowed_root_count = 1;
	if (!CheckStage(vs_interop_session_set_script_compile_options(C.Session, &Bad) == VS_INTEROP_STATUS_OK &&
	            Load(Source) == VS_INTEROP_STATUS_ENGINE_ERROR && Answer(),
	        "resolved path policy"))
	{
		return 1;
	}

	Amount = "41";
	Configuration.value = {Amount.data(), Amount.size()};
	Bad = Options;
	Bad.max_instructions = 1;
	if (!CheckStage(vs_interop_session_set_script_compile_options(C.Session, &Bad) == VS_INTEROP_STATUS_OK &&
	            Load(Source) == VS_INTEROP_STATUS_ENGINE_ERROR && !Result.success && Answer(),
	        "budget failure preserves loaded program"))
	{
		return 1;
	}

	if (!CheckStage(
	        vs_interop_session_set_script_compile_options(C.Session, &Options) == VS_INTEROP_STATUS_OK &&
	            vs_interop_session_request_cancel(C.Session) == VS_INTEROP_STATUS_OK &&
	            Load(Source) == VS_INTEROP_STATUS_ENGINE_ERROR && !Result.success &&
	            vs_interop_session_apply_config(C.Session, &Config) == VS_INTEROP_STATUS_OK &&
	            Load(Source) == VS_INTEROP_STATUS_OK && Answer(),
	        "inherited cancellation and reset"))
	{
		return 1;
	}

	Options.root_count = 0;
	Options.allow_source_imports = 0;
	const std::string Imported = "import compiler \"interop-builder.vs\";\n" + std::string(Source);
	if (!CheckStage(
	        vs_interop_session_set_script_compile_options(C.Session, &Options) == VS_INTEROP_STATUS_OK &&
	            Load(Imported.c_str()) == VS_INTEROP_STATUS_ENGINE_ERROR && Answer(),
	        "source import policy"))
	{
		return 1;
	}

	Options.allow_source_imports = 1;
	if (!CheckStage(
	        vs_interop_session_set_script_compile_options(C.Session, &Options) == VS_INTEROP_STATUS_OK &&
	            Load(Imported.c_str()) == VS_INTEROP_STATUS_OK && Answer(),
	        "source-selected extension"))
	{
		return 1;
	}

	Options.root_count = 1;
	if (!CheckStage(vs_interop_session_set_script_compile_options(C.Session, &Options) == VS_INTEROP_STATUS_OK, "restore configured root")) return 1;
	vs_interop_package_build_options PackageOptions{};
	PackageOptions.struct_size = sizeof(PackageOptions);
	PackageOptions.kind = VS_INTEROP_PACKAGE_APPLICATION;
	PackageOptions.package_name = Text("interop-compiled");
	if (!CheckStage(vs_interop_session_build_package_file(C.Session, Text("interop-main.vs"), &PackageOptions,
	                    &C.Bytes, &Result) == VS_INTEROP_STATUS_OK &&
	            vs_interop_package_read(vs_interop_buffer_view(C.Bytes), &C.Package, &C.Error) ==
	                VS_INTEROP_STATUS_OK,
	        "generated business package"))
	{
		return 1;
	}

	if (!CheckStage(
	        vs_interop_session_set_script_compile_options(C.Session, nullptr) == VS_INTEROP_STATUS_OK &&
	            vs_interop_session_load_package(C.Session, C.Package, Text("compiled.vsapp"), &Result) ==
	                VS_INTEROP_STATUS_OK &&
	            Answer() && Load(Source) == VS_INTEROP_STATUS_ENGINE_ERROR && Answer(),
	        "package executes without extension; defaults clear configured root"))
	{
		return 1;
	}

	std::cout << "script compile C ABI passed\n";
}
