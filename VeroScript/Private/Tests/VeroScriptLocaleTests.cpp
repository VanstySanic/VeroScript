// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptLocale.h"
#include "VeroScriptAst.h"
#include "VeroScriptModuleLoader.h"

#include "VeroScriptTestTemporaryDirectory.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
	bool Expect(const bool Condition, const char* Label)
	{
		if (!Condition)
		{
			std::cerr << "FAIL: " << Label << '\n';
		}
		return Condition;
	}

}

int main(int argc, char** argv)
{
	using namespace VeroScript::Locale;
	namespace fs = std::filesystem;
	if (argc >= 2 && std::string(argv[1]) == "--source-module-catalogs")
	{
		InitializeFromEnvironment();
		(void)TrySetLanguageFromTag("en-US");
		const auto SourceRoot = VeroScript::PathUtil::FindSourceModuleRoot(".");
		if (!Expect(!SourceRoot.empty(), "discover source module independent of outer folder"))
		{
			return EXIT_FAILURE;
		}
		if (argc == 3 && !Expect(TranslateId("test.source_fixture") == "renamed module", "load fixture catalogs rather than ancestor deployment"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(TranslateId("diag.expected_expression") == "expected expression", "discover source core catalogs") || !Expect(TranslateId("diag.callfunction_unknown_function") == "diag.callfunction_unknown_function", "core excludes C++ catalogs"))
		{
			return EXIT_FAILURE;
		}
		InitializeModuleFromEnvironment("Extensions/Language/Cpp/VeroScriptCpp");
		InitializeModuleFromEnvironment("VeroScriptTooling");
		InitializeModuleFromEnvironment("VeroScriptInterop");
		if (!Expect(TranslateId("diag.callfunction_unknown_function", {"Example", "2"}) == "CallFunction unknown function: Example with 2 argument(s)", "discover source C++ catalogs") || !Expect(TranslateId("lsp.code_action.format_document") == "Format document", "discover source tooling catalogs") || !Expect(TranslateId("diag.failed_to_build_package", {"Example"}) == "failed to build package: Example", "discover source Interop catalogs"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(!LoadCatalogDirectory("missing-localization-directory"), "failed replacement preserves catalogs"))
		{
			return EXIT_FAILURE;
		}
		InitializeModuleFromEnvironment("Extensions/Language/Cpp/VeroScriptCpp");
		if (!Expect(TranslateId("diag.callfunction_unknown_function") != "diag.callfunction_unknown_function", "failed replacement preserves source modules"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(LoadCatalogDirectory(VeroScript::PathUtil::ToUtf8(SourceRoot / "VeroScript/Resources/Localization")), "explicitly replace catalogs"))
		{
			return EXIT_FAILURE;
		}
		InitializeModuleFromEnvironment("Extensions/Language/Cpp/VeroScriptCpp");
		if (!Expect(TranslateId("diag.callfunction_unknown_function") == "diag.callfunction_unknown_function", "explicit replacement excludes source modules"))
		{
			return EXIT_FAILURE;
		}
		if (argc == 3)
		{
			VeroScript::VeroScriptModuleLoader Loader("source-layout-test.vs");
			Loader.SetSourceOverlay("source-layout-test.vs", "import \"source_layout_fixture.vs\"; int main() { return source_layout_fixture(); }\n");
			if (!Expect(Loader.LoadMainModule() != nullptr, "discover standard library in renamed source module"))
			{
				return EXIT_FAILURE;
			}
		}
		return EXIT_SUCCESS;
	}
	const VeroScript::Tests::FScopedTemporaryDirectory TemporaryDirectory(
	    "VeroScript-locale-");
	if (!Expect(static_cast<bool>(TemporaryDirectory),
	        "create unique locale test directory"))
	{
		return EXIT_FAILURE;
	}
	const fs::path& TemporaryRoot = TemporaryDirectory.Get();
	{
		std::ofstream(TemporaryRoot / "CMakeLists.txt") << "source repository marker";
		const fs::path Module = TemporaryRoot / fs::u8path("Engine Sources-\xE6\xB5\x8B\xE8\xAF\x95");
		const fs::path Header = "VeroScript/Public/VeroScriptVersion.h";
		fs::create_directories((Module / Header).parent_path());
		std::ofstream(Module / Header) << "source marker";
		if (!Expect(VeroScript::PathUtil::FindSourceModuleRoot(VeroScript::PathUtil::ToUtf8(TemporaryRoot)) == Module, "discover renamed UTF-8 source module") || !Expect(VeroScript::PathUtil::FindSourceModuleRoot(VeroScript::PathUtil::ToUtf8(Module)) == Module, "discover source from module cwd"))
		{
			return EXIT_FAILURE;
		}
		const fs::path Second = TemporaryRoot / "Second Module";
		fs::create_directories((Second / Header).parent_path());
		std::ofstream(Second / Header) << "source marker";
		if (!Expect(VeroScript::PathUtil::FindSourceModuleRoot(VeroScript::PathUtil::ToUtf8(TemporaryRoot)).empty(), "reject ambiguous source modules"))
		{
			return EXIT_FAILURE;
		}
		fs::remove_all(Module);
		fs::remove_all(Second);
		fs::remove(TemporaryRoot / "CMakeLists.txt");
	}

	std::string Error;
	const char* Candidates[] = {
	    "localization",
	    "VeroScript/Resources/Localization",
	};
	bool bLoaded = false;
	for (const char* Candidate : Candidates)
	{
		if (LoadCatalogDirectory(Candidate, &Error))
		{
			bLoaded = true;
			break;
		}
	}
	if (!Expect(bLoaded, "load catalog"))
	{
		std::cerr << Error << '\n';
		return EXIT_FAILURE;
	}
	for (const char* Culture : {"en-US", "zh-CN", "zh-TW", "fr-FR", "ja-JP", "ko-KR"})
	{
		if (!Expect(TrySetLanguageFromTag(Culture), "select size diagnostic locale"))
		{
			return EXIT_FAILURE;
		}
		for (const auto& Diagnostic : std::vector<std::pair<const char*, const char*>>{
		         {"diag.char_code_expects_string_int", "__char_code expects (string, int64)"},
		         {"diag.range_expects_int_argument", "range expects int64 argument"},
		         {"diag.substr_expects_string_int_int", "__substr expects (string, int64, int64)"},
		         {"diag.array_index_must_be_int", "array index must be an integer"},
		         {"diag.index_ref_index_must_be_int", "index-ref index must be an integer"},
		         {"diag.remove_at_expects_int_index", "remove_at expects integer index"},
		         {"diag.reserve_capacity_out_of_range", "reserve capacity must be a non-negative integer within this platform's range"},
		         {"diag.array_capacity_exceeds_int64_range", "array capacity exceeds int64 range"},
		         {"diag.multiset_count_exceeds_int64_range", "multiset count exceeds int64 range"},
		         {"diag.channel_capacity_must_be_positive_int64", "channel capacity must be a positive int64"},
		         {"diag.channel_size_exceeds_int64_range", "channel size exceeds int64 range"},
		         {"diag.find_result_exceeds_int64_range", "find result exceeds int64 range"},
		         {"diag.len_result_exceeds_int64_range", "len result exceeds int64 range"}})
		{
			const std::string Message = TranslateId(Diagnostic.first);
			if (!Expect(Message != Diagnostic.first && Translate(Diagnostic.second) == Message,
			        "size diagnostic English lookup retains localized message ID"))
			{
				return EXIT_FAILURE;
			}
			if (std::string(Culture) != "en-US" && !Expect(Message != Diagnostic.second, "size diagnostic has localized translation"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	for (const char* Culture : {"en-US", "zh-CN", "zh-TW", "fr-FR", "ja-JP", "ko-KR"})
	{
		if (!TrySetLanguageFromTag(Culture)) return EXIT_FAILURE;
		for (const auto& Diagnostic : std::vector<std::pair<const char*, const char*>>{
		         {"diag.template.value_domain", "template value parameter requires an integral, bool, or enum type"},
		         {"diag.template.virtual_modifiers", "method templates cannot be virtual, override, or final"},
		         {"diag.template.required_after_default", "a required template parameter cannot follow a defaulted parameter"}})
		{
			const auto Message = TranslateId(Diagnostic.first);
			if (!Expect(Message != Diagnostic.first && Translate(Diagnostic.second) == Message &&
			            (std::string(Culture) == "en-US" || Message != Diagnostic.second),
			        "template diagnostics retain stable identity and localized text"))
			{
				return EXIT_FAILURE;
			}
		}
		std::string Id;
		std::vector<std::string> Arguments;
		if (!Expect(TryResolveEnglish("invalid template default for 'Count': overflow", Id, Arguments) && Id == "diag.template.default_named" && Arguments == std::vector<std::string>{"Count", "overflow"} && Translate("invalid template default for 'Count': overflow") == TranslateId(Id, {"Count", "overflow"}), "template diagnostics preserve dynamic arguments"))
		{
			return EXIT_FAILURE;
		}
	}

	// Preserve core diagnostic identity and dynamic arguments in every locale.
	for (const char* Culture : {"en-US", "zh-CN", "zh-TW", "fr-FR", "ja-JP", "ko-KR"})
	{
		if (!Expect(TrySetLanguageFromTag(Culture), "select core diagnostic locale"))
		{
			return EXIT_FAILURE;
		}
		for (const auto& Diagnostic : std::vector<std::pair<const char*, const char*>>{
		#include "Suites/VeroScriptLocaleParserDiagnostics.inl"
		#include "Suites/VeroScriptLocaleCommonDiagnostics.inl"
		#include "Suites/VeroScriptLocaleMemoryDiagnostics.inl"
		#include "Suites/VeroScriptLocaleI18nDiagnostics.inl"
		     })
		{
			std::string Id;
			std::vector<std::string> Args;
			if (!Expect(TryResolveEnglish(Diagnostic.second, Id, Args) && Id == Diagnostic.first, "core English lookup retains message identity"))
			{
				std::cerr << Diagnostic.second << '\n';
				return EXIT_FAILURE;
			}
			const std::string Message = Translate(Diagnostic.second);
			if (std::string(Culture) != "en-US" && !Expect(Message != Diagnostic.second, "core diagnostic has localized translation"))
			{
				return EXIT_FAILURE;
			}
			if (!Expect(Args.empty() || Message.find("ExampleType") != std::string::npos,
			        "core translation retains dynamic argument"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	// Resolve both stable IDs and raw English messages, including dynamic paths.
	for (const char* Culture : {"en-US", "zh-CN", "zh-TW", "fr-FR", "ja-JP", "ko-KR"})
	{
		if (!Expect(TrySetLanguageFromTag(Culture), "select backend and reflection diagnostic locale"))
		{
			return EXIT_FAILURE;
		}
		for (const auto& Diagnostic : std::vector<std::pair<const char*, const char*>>{
		         {"diag.reflected_field_value_has_an_incompatible_source_type", "reflected field value has an incompatible source type"},
		         {"diag.reflected_argument_type_is_unknown", "reflected argument type is unknown"},
		         {"diag.reflected_argument_must_have_a_value_type", "reflected argument must have a value type"},
		         {"diag.reflected_reference_argument_must_have_addressable_storage", "reflected reference argument must have addressable storage"},
		         {"diag.reflected_field_operations_require_value_arguments", "reflected field operations require value arguments"},
		         {"diag.operator_reference_parameter_requires_compatible_addressable_storage", "operator reference parameter requires compatible addressable storage"},
		         {"diag.operator_call_supports_at_most_255_parameters", "operator call supports at most 255 parameters"},
		         {"diag.binary_operator_result_cannot_be_assigned_to_compound_target", "binary operator result cannot be assigned to compound target"},
		         {"diag.undeclared_global", "undeclared global: {0}"},
		         {"diag.cannot_bind_mutable_reference_to_a_field_of_a_temporary_struct", "cannot bind mutable reference to a field of a temporary struct"},
		         {"diag.missing_jit_closure_captures", "missing JIT closure captures"},
		         {"diag.invalid_interpreter_invocation_from_jit", "invalid interpreter invocation from JIT"},
		         {"diag.interpreted_jit_invocation_left_unbalanced_execution_state", "interpreted JIT invocation left unbalanced execution state"},
		         {"diag.unbalanced_optimized_call_scope", "unbalanced optimized call scope"},
		         {"diag.dynamic_jit_libraries_are_unavailable_on_this_platform_use_vm_or_a_static_portable_backend", "dynamic JIT libraries are unavailable on this platform; use VM or a static portable backend"},
		         {"diag.dynamic_jit_loading_requires_the_shared_msvc_runtime_md_or_mdd", "dynamic JIT loading requires the shared MSVC runtime (/MD or /MDd)"},
		         {"diag.jit_backend_requires_a_nonempty_file_or_directory_path_without_nul_characters", "JIT backend requires a nonempty file or directory path without NUL characters"},
		         {"diag.jit_backend_path_is_not_an_existing_file_or_directory", "JIT backend path is not an existing file or directory: {0}"},
		         {"diag.jit_directory_exceeds_the_4096_candidate_search_limit", "JIT directory exceeds the 4096-candidate search limit"},
		         {"diag.no_compatible_usable_jit_backend_in_directory", "no compatible usable JIT backend in directory: {0}"},
		         {"diag.jit_backend_loading_failed", "JIT backend loading failed"},
		         {"diag.jit_linear_frame_leave_mismatch", "JIT linear frame: leave mismatch"},
		         {"diag.jit_function_returned_an_invalid_reference", "JIT function returned an invalid reference"},
		         {"diag.jit_function_return_type_mismatch", "JIT function return type mismatch"},
		         {"diag.jit_nested_call_left_unbalanced_execution_state", "JIT nested call left unbalanced execution state"},
		         {"diag.jit_frame_does_not_belong_to_the_active_invocation", "JIT frame does not belong to the active invocation"},
		         {"diag.rewritten_jit_body_has_a_different_callable_signature", "rewritten JIT body has a different callable signature"},
		         {"diag.invalid_jit_bytecode_instruction_offset", "invalid JIT bytecode instruction offset"},
		         {"diag.invalid_jit_local_slot", "invalid JIT local slot"},
		         {"diag.invalid_jit_local_reference", "invalid JIT local reference"},
		         {"diag.jit_entry_left_unbalanced_optimized_call_depth", "JIT entry left unbalanced optimized call depth"},
		         {"diag.out_of_memory_in_jit_runtime_service", "out of memory in JIT runtime service"},
		         {"diag.native_exception_in_jit_runtime_service", "native exception in JIT runtime service"},
		         {"diag.missing_jit_call_arguments", "missing JIT call arguments"},
		         {"diag.jit_language_call_left_unbalanced_execution_state", "JIT language call left unbalanced execution state"},
		         {"diag.invalid_jit_call_target", "invalid JIT call target"},
		         {"diag.invalid_jit_call_signature_or_receiver", "invalid JIT call signature or receiver"},
		         {"diag.jit_call_receiver_belongs_to_an_incompatible_type_or_generation", "JIT call receiver belongs to an incompatible type or generation"},
		         {"diag.invalid_jit_call_signature_metadata", "invalid JIT call signature metadata"},
		         {"diag.jit_call_argument_type_or_reference_mismatch", "JIT call argument type or reference mismatch"},
		         {"diag.invalid_or_inactive_jit_runtime_frame", "invalid or inactive JIT runtime frame"},
		         {"diag.cannot_change_jit_backend_while_the_vm_is_executing", "cannot change JIT backend while the VM is executing"},
		         {"diag.cannot_change_jit_module_ownership_while_the_vm_is_executing", "cannot change JIT module ownership while the VM is executing"},
		         {"diag.cannot_change_jit_resource_limits_while_the_vm_is_executing", "cannot change JIT resource limits while the VM is executing"},
		         {"diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory", "JIT execution requires an explicitly selected backend file, directory, or factory"},
		         {"diag.jit_module_owner_does_not_own_the_executing_module", "JIT module owner does not own the executing module"},
		         {"diag.jit_entry_left_unbalanced_execution_state", "JIT entry left unbalanced execution state"},
		         {"diag.channel_capacity_exceeds_int64_range", "channel capacity exceeds int64 range"},
		         {"diag.only_operator_and_operator_may_be_static", "only operator() and operator[] may be static"},
		         {"diag.increment_decrement_overload_has_invalid_parameter_count", "increment/decrement overload has invalid parameter count"},
		         {"diag.postfix_increment_decrement_tag_must_be_a_by_value_int", "postfix increment/decrement tag must be a by-value int"},
		         {"diag.operator_must_be_a_member_operator", "operator= must be a member operator"},
		         {"diag.invalid_reflected_call_signature", "invalid reflected call signature"},
		         {"diag.reflected_method_argument_violates_type_or_ref_const_rules", "reflected method argument violates type or ref/const rules"},
		         {"diag.reflected_method_argument_requires_an_invalid_implicit_object_conversion", "reflected method argument requires an invalid implicit object conversion"},
		         {"diag.reflected_reference_argument_has_no_valid_storage", "reflected reference argument has no valid storage"},
		         {"diag.reflected_method_argument_does_not_match_its_call_site_type", "reflected method argument does not match its call-site type"}})
		{
			const std::string Argument = "C:/module path/Unicode-\xE6\xB5\x8B\xE8\xAF\x95";
			std::string English = Diagnostic.second;
			const std::size_t Placeholder = English.find("{0}");
			const bool HasArgument = Placeholder != std::string::npos;
			if (HasArgument)
			{
				English.replace(Placeholder, 3, Argument);
			}
			const std::string Message = TranslateId(Diagnostic.first,
			    HasArgument ? std::vector<std::string_view>{Argument} : std::vector<std::string_view>{});
			if (!Expect(Message != Diagnostic.first && Translate(English) == Message,
			        "backend/reflection diagnostic retains localized message ID and arguments") ||
			    !Expect(!HasArgument || Message.find(Argument) != std::string::npos,
			        "translated diagnostic preserves opaque UTF-8 path") ||
			    !Expect(std::string(Culture) == "en-US" || Message != English,
			        "backend/reflection diagnostic is translated"))
			{
				return EXIT_FAILURE;
			}
		}
	}

	for (const char* Alias : {"C", "c.UTF-8", " POSIX.UTF-8@modifier ", "\tC@modifier\r\n"})
	{
		(void)TrySetLanguageFromTag("zh-CN");
		if (!Expect(TrySetLanguageFromTag(Alias) && GetLanguageTag() == "en-US",
		        "normalize POSIX English aliases after trimming encoding and modifiers"))
		{
			return EXIT_FAILURE;
		}
	}
	(void)TrySetLanguageFromTag("en-US");
	for (const auto& ScriptCase : std::vector<std::pair<const char*, const char*>>{
	         {"zh-Hans-TW", "zh-CN"}, {"zh-Hant-CN", "zh-TW"},
	         {"zh-SG-x-Hant", "zh-CN"}, {"zh-MO-x-Hans", "zh-TW"}})
	{
		if (!Expect(TrySetLanguageFromTag(ScriptCase.first) && GetLanguageTag() == ScriptCase.second,
		        "explicit Chinese script wins over region; private use does not select script"))
		{
			return EXIT_FAILURE;
		}
	}
	(void)TrySetLanguageFromTag("en-US");
	if (!Expect(
	        TranslateId("diag.expected_expression") == "expected expression",
	        "en id"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(
	        TrySetLanguageFromTag("zh_Hans.UTF-8") && GetLanguageTag().rfind("zh-", 0) == 0,
	        "normalize system-style language tag") ||
	    !Expect(
	        TrySetLanguageFromTag("en_GB.UTF-8") && GetLanguageTag().rfind("en-", 0) == 0,
	        "normalize English region tag") ||
	    !Expect(
	        !TrySetLanguageFromTag("invalid/tag"),
	        "reject an invalid language tag"))
	{
		return EXIT_FAILURE;
	}

	(void)TrySetLanguageFromTag("zh-CN");
	if (!Expect(
	        TranslateId("diag.expected_expression") == "期望表达式",
	        "zh id") ||
	    !Expect(
	        Translate("expected expression") == "期望表达式",
	        "zh legacy english key") ||
	    !Expect(
	        Translate("cannot resolve import: x.vs") == "无法解析 import：x.vs",
	        "zh prefixed legacy") ||
	    !Expect(
	        FormatEnglish("diag.cannot_resolve_import", {"x.vs"}) == "cannot resolve import: x.vs",
	        "format english") ||
	    !Expect(
	        FormatDiagnosticMessage(
	            "diag.cannot_resolve_import",
	            {"x.vs"},
	            "") == "无法解析 import：x.vs",
	        "format diagnostic id") ||
	    !Expect(
	        FormatDiagnosticMessage(
	            "diag.wrong_number_of_type_arguments",
	            {"Box", "1", "2"},
	            "") == "类型实参个数错误：Box（期望 1，实际 2）",
	        "zh multi-arg type args") ||
	    !Expect(
	        FormatDiagnosticMessage(
	            "diag.class_can_only_inherit_from_class_got_struct",
	            {"S"},
	            "") == "class 只能继承自 class（得到 struct S）",
	        "zh inherit kind"))
	{
		return EXIT_FAILURE;
	}

	{
		std::string ResolvedId;
		std::vector<std::string> ResolvedArgs;
		if (!Expect(
		        TryResolveEnglish(
		            "cannot resolve import: demo.vs",
		            ResolvedId,
		            ResolvedArgs) &&
		            ResolvedId == "diag.cannot_resolve_import" && ResolvedArgs.size() == 1 && ResolvedArgs[0] == "demo.vs",
		        "resolve english prefix") ||
		    !Expect(
		        TryResolveEnglish("division by zero", ResolvedId, ResolvedArgs) && ResolvedId == "diag.division_by_zero" && ResolvedArgs.empty(),
		        "resolve english exact") ||
		    !Expect(
		        TryResolveEnglish(
		            "cannot compare int and string",
		            ResolvedId,
		            ResolvedArgs) &&
		            ResolvedId == "diag.cannot_compare_and" && ResolvedArgs.size() == 2 && ResolvedArgs[0] == "int" && ResolvedArgs[1] == "string",
		        "resolve multi-arg") ||
		    !Expect(
		        !TryResolveEnglish(
		            "division by zero in expression",
		            ResolvedId,
		            ResolvedArgs),
		        "reject complete-phrase false prefix"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const fs::path Dir = TemporaryRoot / "noise";
		std::error_code Ec;
		fs::create_directories(Dir / "en-US", Ec);
		fs::create_directories(Dir / "zh-CN", Ec);
		{
			std::ofstream En(Dir / "en-US" / "messages.json");
			En << "{\n"
			      "  \"diag.noise\": \")\",\n"
			      "  \"diag.short\": \": \",\n"
			      "  \"diag.good\": \"cannot open script: {0}\"\n"
			      "}\n";
		}
		{
			std::ofstream Zh(Dir / "zh-CN" / "messages.json");
			Zh << "{\n"
			      "  \"diag.noise\": \")\",\n"
			      "  \"diag.short\": \": \",\n"
			      "  \"diag.good\": \"无法打开脚本：{0}\"\n"
			      "}\n";
		}
		if (!Expect(LoadCatalogDirectory(Dir.string(), &Error), "load noise catalog"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		std::string ResolvedId;
		std::vector<std::string> ResolvedArgs;
		(void)TrySetLanguageFromTag("zh-CN");
		if (!Expect(
		        !TryResolveEnglish(")trailing", ResolvedId, ResolvedArgs),
		        "reject short prefix noise") ||
		    !Expect(
		        TryResolveEnglish(
		            "cannot open script: a.vs",
		            ResolvedId,
		            ResolvedArgs) &&
		            ResolvedId == "diag.good" && ResolvedArgs.size() == 1 && ResolvedArgs[0] == "a.vs",
		        "prefer real template over noise") ||
		    !Expect(
		        FormatDiagnosticMessage("diag.good", {"a.vs"}, "") == "无法打开脚本：a.vs",
		        "noise catalog format"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		const fs::path Dir = TemporaryRoot / "format";
		std::error_code Ec;
		fs::create_directories(Dir / "en-US", Ec);
		fs::create_directories(Dir / "zh-CN", Ec);
		{
			std::ofstream En(Dir / "en-US" / "messages.json");
			En << "{\n  \"diag.hello\": \"Hello {0}\"\n}\n";
		}
		{
			std::ofstream Zh(Dir / "zh-CN" / "messages.json");
			Zh << "{\n  \"diag.hello\": \"你好 {0}\"\n}\n";
		}
		if (!Expect(LoadCatalogDirectory(Dir.string(), &Error), "load fmt catalog"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		(void)TrySetLanguageFromTag("zh-CN");
		if (!Expect(
		        TranslateId("diag.hello", {"世界"}) == "你好 世界",
		        "placeholder zh"))
		{
			return EXIT_FAILURE;
		}
		(void)TrySetLanguageFromTag("en-US");
		if (!Expect(
		        TranslateId("diag.hello", {"world"}) == "Hello world",
		        "placeholder en"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Per-module localization: MergeCatalogDirectory augments (not replaces) the
		// active catalog. New module keys are added; existing keys are preserved unless
		// bOverride is requested.
		const fs::path Base = TemporaryRoot / "merge-base";
		const fs::path Ext = TemporaryRoot / "merge-extension";
		std::error_code Ec;
		fs::create_directories(Base / "en-US", Ec);
		fs::create_directories(Base / "zh-CN", Ec);
		fs::create_directories(Ext / "en-US", Ec);
		fs::create_directories(Ext / "zh-CN", Ec);
		{
			std::ofstream En(Base / "en-US" / "messages.json");
			En << "{\n  \"diag.base\": \"base en\"\n}\n";
			std::ofstream Zh(Base / "zh-CN" / "messages.json");
			Zh << "{\n  \"diag.base\": \"base zh\"\n}\n";
		}
		{
			std::ofstream En(Ext / "en-US" / "messages.json");
			En << "{\n  \"diag.base\": \"ext en\",\n  \"diag.module_only\": \"module en\"\n}\n";
			std::ofstream Zh(Ext / "zh-CN" / "messages.json");
			Zh << "{\n  \"diag.base\": \"ext zh\",\n  \"diag.module_only\": \"module zh\"\n}\n";
		}
		if (!Expect(LoadCatalogDirectory(Base.string(), &Error), "load merge base"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		(void)TrySetLanguageFromTag("en-US");
		if (!Expect(
		        MergeCatalogDirectory(Ext.string(), /*bOverride=*/false, &Error),
		        "merge module catalog"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
		        TranslateId("diag.module_only") == "module en",
		        "merged new module key resolves") ||
		    !Expect(
		        TranslateId("diag.base") == "base en",
		        "existing key preserved on non-override merge"))
		{
			return EXIT_FAILURE;
		}
		if (!Expect(
		        MergeCatalogDirectory(Ext.string(), /*bOverride=*/true, &Error),
		        "merge module catalog (override)"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		if (!Expect(
		        TranslateId("diag.base") == "ext en",
		        "override merge replaces existing key"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// A catalog root may be split by component. English-only component catalogs are
		// valid and provide the fallback for languages that omit those keys.
		const fs::path Root = TemporaryRoot / "split";
		std::error_code Ec;
		fs::remove_all(Root, Ec);
		fs::create_directories(Root / "en-US", Ec);
		fs::create_directories(Root / "zh-CN", Ec);
		{
			std::ofstream En(Root / "en-US" / "compiler.json");
			En << "{\n  \"split.compiler\": \"compiler en\"\n}\n";
			std::ofstream Zh(Root / "zh-CN" / "compiler.json");
			Zh << "{\n  \"split.compiler\": \"compiler zh\"\n}\n";
		}
		{
			std::ofstream En(Root / "en-US" / "runtime.json");
			En << "{\n  \"split.runtime\": \"runtime en\"\n}\n";
		}
		if (!Expect(LoadCatalogDirectory(Root.string(), &Error), "load split catalog tree"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		(void)TrySetLanguageFromTag("zh-CN");
		if (!Expect(
		        TranslateId("split.compiler") == "compiler zh",
		        "split component translation") ||
		    !Expect(
		        TranslateId("split.runtime") == "runtime en",
		        "split component missing-language fallback"))
		{
			return EXIT_FAILURE;
		}

		{
			std::ofstream En(Root / "en-US" / "duplicate.json");
			En << "{\n  \"split.compiler\": \"duplicate\"\n}\n";
		}
		if (!Expect(
		        !LoadCatalogDirectory(Root.string(), &Error),
		        "reject duplicate ids across split catalogs"))
		{
			return EXIT_FAILURE;
		}
		fs::remove(Root / "en-US" / "duplicate.json", Ec);
		{
			std::ofstream ZhOnly(Root / "zh-CN" / "orphan.json");
			ZhOnly << "{\n  \"split.orphan\": \"orphan zh\"\n}\n";
		}
		if (!Expect(
		        !LoadCatalogDirectory(Root.string(), &Error),
		        "reject zh-CN file without en-US counterpart"))
		{
			return EXIT_FAILURE;
		}
		fs::remove_all(Root, Ec);
	}

	{
		// The pre-1.0 domain-first en.json/zh-CN.json layout is intentionally not
		// accepted. Published resources have one canonical layout only.
		const fs::path Root = TemporaryRoot / "legacy-layout";
		std::error_code Ec;
		fs::remove_all(Root, Ec);
		fs::create_directories(Root / "compiler", Ec);
		std::ofstream En(Root / "compiler" / "en.json");
		En << "{\n  \"legacy.message\": \"legacy\"\n}\n";
		En.close();
		if (!Expect(
		        !LoadCatalogDirectory(Root.string(), &Error),
		        "reject legacy locale directory layout"))
		{
			return EXIT_FAILURE;
		}
		fs::remove_all(Root, Ec);
	}

	{
		const fs::path Root = TemporaryRoot / "invalid-json";
		std::error_code Ec;
		fs::remove_all(Root, Ec);
		fs::create_directories(Root / "en-US", Ec);
		{
			std::ofstream En(Root / "en-US" / "messages.json");
			En << "{\n  \"duplicate.key\": \"first\",\n"
			      "  \"duplicate.key\": \"second\"\n}\n";
		}
		if (!Expect(
		        !LoadCatalogDirectory(Root.string(), &Error),
		        "reject duplicate ids within one localization file"))
		{
			return EXIT_FAILURE;
		}
		{
			std::ofstream En(Root / "en-US" / "messages.json", std::ios::trunc);
			En << "{\n"
			      "  \"unicode.escape\": \"\\u4F60\\u597D \\uD83D\\uDE00\",\n"
			      "  \"control.escape\": \"line\\nfeed\\tvalue\"\n"
			      "}\n";
		}
		if (!Expect(
		        LoadCatalogDirectory(Root.string(), &Error),
		        "accept complete JSON string escapes") ||
		    !Expect(
		        TranslateId("unicode.escape") == "\xE4\xBD\xA0\xE5\xA5\xBD \xF0\x9F\x98\x80",
		        "decode JSON unicode and surrogate-pair escapes") ||
		    !Expect(
		        TranslateId("control.escape") == "line\nfeed\tvalue",
		        "decode JSON control escapes"))
		{
			return EXIT_FAILURE;
		}
		{
			std::ofstream En(Root / "en-US" / "messages.json", std::ios::trunc);
			En << "{\n  \"invalid.escape\": \"\\q\"\n}\n";
		}
		if (!Expect(
		        !LoadCatalogDirectory(Root.string(), &Error),
		        "reject invalid JSON escape"))
		{
			return EXIT_FAILURE;
		}
		{
			std::ofstream En(Root / "en-US" / "messages.json", std::ios::trunc);
			En << "{\n  \"invalid.surrogate\": \"\\uD800\"\n}\n";
		}
		if (!Expect(
		        !LoadCatalogDirectory(Root.string(), &Error),
		        "reject incomplete JSON surrogate pair"))
		{
			return EXIT_FAILURE;
		}
		{
			std::ofstream En(
			    Root / "en-US" / "messages.json",
			    std::ios::binary | std::ios::trunc);
			En << "{\n  \"invalid.utf8\": \"";
			En.put(static_cast<char>(0xC0));
			En.put(static_cast<char>(0xAF));
			En << "\"\n}\n";
		}
		if (!Expect(
		        !LoadCatalogDirectory(Root.string(), &Error),
		        "reject invalid UTF-8 in localization JSON"))
		{
			return EXIT_FAILURE;
		}
		{
			std::ofstream En(Root / "en-US" / "messages.json", std::ios::trunc);
			En << "{\n  \"valid.key\": \"value\"\n}\ntrailing";
		}
		if (!Expect(
		        !LoadCatalogDirectory(Root.string(), &Error),
		        "reject trailing content in localization JSON"))
		{
			return EXIT_FAILURE;
		}
		{
			std::ofstream En(Root / "en-US" / "messages.json", std::ios::trunc);
			En << "{\n  \"valid.key\": \"value\"\n}\n";
			std::ofstream InvalidCulturePath(Root / "zh-CN");
			InvalidCulturePath << "not a directory";
		}
		if (!Expect(
		        !LoadCatalogDirectory(Root.string(), &Error),
		        "reject localization culture file in place of directory"))
		{
			return EXIT_FAILURE;
		}
		fs::remove_all(Root, Ec);
	}

	{
		// A caller-added stdlib root must register its localization immediately; InitSearchPaths
		// has already run by the time AddStdlibSearchRoot is called.
		const fs::path Root = TemporaryRoot / "custom-root";
		std::error_code Ec;
		fs::create_directories(Root / "localization" / "en-US", Ec);
		fs::create_directories(Root / "localization" / "zh-CN", Ec);
		{
			std::ofstream En(Root / "localization" / "en-US" / "messages.json");
			En << "{\n  \"custom_root.message\": \"custom root en\"\n}\n";
			std::ofstream Zh(Root / "localization" / "zh-CN" / "messages.json");
			Zh << "{\n  \"custom_root.message\": \"custom root zh\"\n}\n";
		}
		VeroScript::VeroScriptModuleLoader Loader("");
		Loader.AddStdlibSearchRoot(Root.string());
		(void)TrySetLanguageFromTag("en-US");
		if (!Expect(
		        TranslateId("custom_root.message") == "custom root en",
		        "custom stdlib root localization"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// Source overlays retain their resolved path identity and therefore must load the
		// same sibling localization as a module read from disk.
		const fs::path Dir = TemporaryRoot / "overlay";
		const fs::path ModulePath = Dir / "main.vs";
		std::error_code Ec;
		fs::create_directories(Dir / "localization" / "en-US", Ec);
		fs::create_directories(Dir / "localization" / "zh-CN", Ec);
		{
			std::ofstream En(Dir / "localization" / "en-US" / "messages.json");
			En << "{\n  \"overlay.message\": \"overlay en\"\n}\n";
			std::ofstream Zh(Dir / "localization" / "zh-CN" / "messages.json");
			Zh << "{\n  \"overlay.message\": \"overlay zh\"\n}\n";
		}
		VeroScript::VeroScriptModuleLoader Loader(ModulePath.string());
		Loader.SetSourceOverlay(ModulePath.string(), "void main() {}\n");
		if (!Expect(Loader.LoadMainModule() != nullptr, "load source overlay module") || !Expect(TranslateId("overlay.message") == "overlay en", "source overlay sibling localization"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		// The loader's path contract is UTF-8, including on Windows where
		// filesystem::path(string) would otherwise interpret bytes using the ANSI code page.
		const fs::path Dir =
		    TemporaryRoot / VeroScript::PathUtil::FromUtf8("veroscript_locale_\xE8\xB7\xAF\xE5\xBE\x84");
		const fs::path ModulePath = Dir / "main.vs";
		std::error_code Ec;
		fs::remove_all(Dir, Ec);
		fs::create_directories(Dir / "localization" / "en-US", Ec);
		{
			std::ofstream En(Dir / "localization" / "en-US" / "messages.json");
			En << "{\n  \"unicode_path.message\": \"unicode path en\"\n}\n";
		}
		const std::string ModulePathUtf8 = VeroScript::PathUtil::ToUtf8(ModulePath);
		VeroScript::VeroScriptModuleLoader Loader(ModulePathUtf8);
		Loader.SetSourceOverlay(ModulePathUtf8, "void main() {}\n");
		(void)TrySetLanguageFromTag("en-US");
		const auto Module = Loader.LoadMainModule();
		if (!Expect(Module != nullptr, "load UTF-8 path overlay module") || !Expect(TranslateId("unicode_path.message") == "unicode path en", "load sibling localization from UTF-8 path"))
		{
			return EXIT_FAILURE;
		}
		fs::remove_all(Dir, Ec);
	}

	{
		const fs::path Root = TemporaryRoot / "bare-language";
		std::error_code Ec;
		fs::remove_all(Root, Ec);
		fs::create_directories(Root / "en-US", Ec);
		fs::create_directories(Root / "ja", Ec);
		{
			std::ofstream En(Root / "en-US" / "messages.json");
			En << "{\n  \"dynamic.hello\": \"hello\"\n}\n";
			std::ofstream Ja(Root / "ja" / "messages.json");
			Ja << "{\n  \"dynamic.hello\": \"ja\"\n}\n";
		}
		if (!Expect(
		        !LoadCatalogDirectory(Root.string(), &Error),
		        "reject a bare language directory without a region suffix"))
		{
			return EXIT_FAILURE;
		}
		fs::remove_all(Root, Ec);
	}

	{
		const fs::path Root = TemporaryRoot / "simplified-only";
		std::error_code Ec;
		fs::remove_all(Root, Ec);
		fs::create_directories(Root / "en-US", Ec);
		fs::create_directories(Root / "zh-CN", Ec);
		{
			std::ofstream En(Root / "en-US" / "messages.json");
			En << "{\n  \"dynamic.hello\": \"hello\"\n}\n";
			std::ofstream ZhCn(Root / "zh-CN" / "messages.json");
			ZhCn << "{\n  \"dynamic.hello\": \"zh-hans\"\n}\n";
		}
		if (!Expect(LoadCatalogDirectory(Root.string(), &Error), "load simplified-only catalog") || !Expect(!TrySetLanguageFromTag("zh-Hant-HK"), "do not map Traditional Chinese to a Simplified Chinese catalog"))
		{
			return EXIT_FAILURE;
		}
		fs::remove_all(Root, Ec);
	}

	{
		const fs::path Root = TemporaryRoot / "dynamic-cultures";
		std::error_code Ec;
		fs::remove_all(Root, Ec);
		for (const char* Culture : {"en-US", "zh-CN", "zh-TW", "ja-JP", "ko-KR", "fr-FR"})
		{
			fs::create_directories(Root / Culture, Ec);
		}
		{
			std::ofstream En(Root / "en-US" / "messages.json");
			En << "{\n  \"dynamic.hello\": \"hello\",\n  \"dynamic.fallback\": \"fallback\"\n}\n";
			std::ofstream ZhCn(Root / "zh-CN" / "messages.json");
			ZhCn << "{\n  \"dynamic.hello\": \"zh-hans\"\n}\n";
			std::ofstream ZhTw(Root / "zh-TW" / "messages.json");
			ZhTw << "{\n  \"dynamic.hello\": \"zh-hant\"\n}\n";
			std::ofstream Ja(Root / "ja-JP" / "messages.json");
			Ja << "{\n  \"dynamic.hello\": \"ja\"\n}\n";
			std::ofstream Ko(Root / "ko-KR" / "messages.json");
			Ko << "{\n  \"dynamic.hello\": \"ko\"\n}\n";
			std::ofstream Fr(Root / "fr-FR" / "messages.json");
			Fr << "{\n  \"dynamic.hello\": \"fr\"\n}\n";
		}
		std::vector<std::string> ExpectedCultures;
		for (const auto& Entry : fs::directory_iterator(Root))
		{
			if (Entry.is_directory())
			{
				ExpectedCultures.push_back(Entry.path().filename().string());
			}
		}
		std::sort(ExpectedCultures.begin(), ExpectedCultures.end());
		if (!Expect(LoadCatalogDirectory(Root.string(), &Error), "load dynamically discovered cultures") || !Expect(GetAvailableLanguageTags() == ExpectedCultures, "enumerate dynamically discovered cultures") || !Expect(TrySetLanguageFromTag("ja") && GetLanguageTag() == "ja-JP" && TranslateId("dynamic.hello") == "ja", "resolve Japanese language-only tag") || !Expect(TrySetLanguageFromTag("ko_KR.UTF-8") && GetLanguageTag() == "ko-KR" && TranslateId("dynamic.hello") == "ko", "normalize Korean system locale tag") || !Expect(TrySetLanguageFromTag("zh-Hant-HK") && GetLanguageTag() == "zh-TW" && TranslateId("dynamic.hello") == "zh-hant", "resolve Traditional Chinese by script") || !Expect(TranslateId("dynamic.fallback") == "fallback", "fall back to en-US for a missing translated key") || !Expect(!TrySetLanguageFromTag("de-DE"), "reject a language without a catalog"))
		{
			std::cerr << Error << '\n';
			return EXIT_FAILURE;
		}
		fs::remove_all(Root, Ec);
	}

	std::cout << "VeroScriptLocaleTests passed\n";
	return EXIT_SUCCESS;
}
