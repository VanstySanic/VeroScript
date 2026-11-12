// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptWebAssembly.h"
#include "VeroScriptWebAssemblyC.h"

#include <algorithm>
#include <clocale>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	bool Expect(const bool Condition, const char* const Message)
	{
		if (Condition)
		{
			return true;
		}
		std::cerr << "VeroScriptWebAssemblyTests: " << Message << '\n';
		return false;
	}

	std::uint32_t ProgramHandleFromJson(const std::string& Json)
	{
		constexpr std::string_view Prefix = "\"programHandle\":";
		const std::size_t Offset = Json.find(Prefix);
		if (Offset == std::string::npos)
		{
			return 0;
		}
		const std::size_t ValueOffset = Offset + Prefix.size();
		if (ValueOffset >= Json.size() || Json[ValueOffset] < '0' || Json[ValueOffset] > '9')
		{
			return 0;
		}
		return static_cast<std::uint32_t>(std::stoul(Json.substr(ValueOffset)));
	}
}

int main()
{
	bool bOk = true;
	{
		const std::string SavedLocale = std::setlocale(LC_NUMERIC, nullptr);
		for (const char* Locale : {"de-DE", "de_DE.UTF-8", "fr_FR.UTF-8", "French_France.1252"})
		{
			if (std::setlocale(LC_NUMERIC, Locale) && std::localeconv()->decimal_point[0] == ',')
			{
				break;
			}
		}
		VeroScript::WebAssembly::FResult Timing;
		Timing.CompileMilliseconds = 1.25;
		Timing.ExecuteMilliseconds = 2.5;
		const std::string Json = VeroScript::WebAssembly::ResultToJson(Timing, "run");
		Timing.ExecuteMilliseconds = std::numeric_limits<double>::infinity();
		const std::string InvalidTimingJson = VeroScript::WebAssembly::ResultToJson(Timing, "run");
		bOk &= Expect(std::setlocale(LC_NUMERIC, SavedLocale.c_str()) != nullptr,
			"restore the host numeric locale");
		bOk &= Expect(Json.find("\"timings\":{\"compileMs\":1.25,\"executeMs\":2.5}") != std::string::npos,
			"timing JSON should use decimal points regardless of the host numeric locale");
		bOk &= Expect(InvalidTimingJson.find("\"timings\":{\"compileMs\":1.25,\"executeMs\":null}") != std::string::npos,
			"non-finite timings should be serialized as null");
	}
	VeroScript::WebAssembly::FRuntime Runtime;
	const auto Variant = Runtime.RunSource(
		"void main(){variant box=42;assert(variant_is<int>(box));print(cast<int>(box));}", "variant.vs");
	bOk &= Expect(Variant.bSuccess && Variant.Output == "42\n", "variant uses the shared Core VM without a JavaScript value mapping");

	const VeroScript::WebAssembly::FResult Run = Runtime.RunSource(
		"void main() { print(\"web sdk\"); }",
		"browser.vs");
	bOk &= Expect(Run.bSuccess, "valid source should run");
	bOk &= Expect(Run.ExitCode == 0, "successful run should return exit code zero");
	bOk &= Expect(
		Run.Backend == VeroScript::WebAssembly::EBackend::Mixed,
		"C++ WebAssembly runtime should default to mixed");
	bOk &= Expect(Run.Output == "web sdk\n", "run output should be captured exactly");
	bOk &= Expect(Run.ErrorOutput.empty(), "successful run should not emit errors");
	Runtime.SetStandardLibraryLimits({5,0,0});
	bOk &= Expect(!Runtime.RunSource("void main(){format(\"{}{}\",\"abc\",\"abc\");}", "limits.vs").bSuccess,
		"web host propagates standard library limits");
	Runtime.SetStandardLibraryLimits({});
	bOk &= Expect(Runtime.RunSource("void main(){format(\"{}{}\",\"abc\",\"abc\");}", "limits.vs").bSuccess,
		"zero removes the additional web host limit");

	const std::vector<VeroScript::WebAssembly::FSourceFile> CompilerFiles = {{
		"web-compiler.vs",
		"import \"compiler.vs\"; export void register_extension(std::compiler::registration reg) { assert(true); }"}};
	const auto CompilerCheck = Runtime.CheckSource(
		"import compiler \"web-compiler.vs\"; int main() { return 42; }", "compiler-check.vs", CompilerFiles);
	bOk &= Expect(CompilerCheck.bSuccess, "source checking explicitly selects the VM for compiler extensions");

	constexpr std::string_view ConsoleSource = R"(
int main()
{
	assert(!std::console::is_output_terminal() && !std::console::is_interactive());
	assert(!std::console::supports_color() && !std::console::supports_cursor() && !std::console::supports_clear());
	assert(std::console::width() == 0 && std::console::height() == 0);
	std::console::write("A");
	std::console::write("B");
	std::console::flush();
	assert(!std::console::set_color("red", "default") && !std::console::reset_style());
	assert(!std::console::set_cursor_visible(false) && !std::console::clear());
	assert(!std::console::clear_line("all") && !std::console::move_cursor(0, 0));
	std::console::error("E");
	print("C");
	return 42;
}
)";
	for (const auto Backend : {VeroScript::WebAssembly::EBackend::Vm, VeroScript::WebAssembly::EBackend::Mixed})
	{
		const auto ConsoleRun = Runtime.RunSource(ConsoleSource, "console.vs", {}, {}, Backend);
		bOk &= Expect(ConsoleRun.bSuccess && ConsoleRun.ExitCode == 42
			&& ConsoleRun.Output == "ABC\n" && ConsoleRun.ErrorOutput == "E\n",
			"WebAssembly facade should capture console text and report no terminal capabilities");
		const auto InvalidConsole = Runtime.RunSource(
			"void main() { std::console::set_color(\"invalid\", \"default\"); }",
			"console-invalid.vs", {}, {}, Backend);
		bOk &= Expect(!InvalidConsole.bSuccess && !InvalidConsole.Diagnostics.empty()
			&& InvalidConsole.Output.empty() && InvalidConsole.ErrorOutput.find('\x1b') == std::string::npos,
			"WebAssembly console validation should precede unsupported-terminal fallback");
	}

	for (const auto& [Severity, Name] : std::vector<std::pair<VeroScript::ECompileDiagnosticSeverity, std::string_view>>{
		{VeroScript::ECompileDiagnosticSeverity::Error, "error"},
		{VeroScript::ECompileDiagnosticSeverity::Warning, "warning"},
		{VeroScript::ECompileDiagnosticSeverity::Information, "info"},
		{VeroScript::ECompileDiagnosticSeverity::Hint, "hint"}})
	{
		VeroScript::WebAssembly::FResult DiagnosticResult;
		DiagnosticResult.bSuccess = Severity != VeroScript::ECompileDiagnosticSeverity::Error;
		DiagnosticResult.ExitCode = DiagnosticResult.bSuccess ? 0 : 3;
		VeroScript::FScriptError Diagnostic;
		Diagnostic.Severity = Severity;
		Diagnostic.Message = "core diagnostic";
		Diagnostic.Location.FileLabel = "diagnostic.vs";
		DiagnosticResult.Diagnostics.push_back(std::move(Diagnostic));
		const std::string Json = VeroScript::WebAssembly::ResultToJson(DiagnosticResult, "check");
		bOk &= Expect(Json.find("\"severity\":\"" + std::string(Name) + "\"") != std::string::npos,
			"JSON diagnostics should preserve the core severity");
		bOk &= Expect(Json.find(DiagnosticResult.bSuccess ? "\"success\":true" : "\"success\":false")
			!= std::string::npos, "non-error diagnostics should preserve success");
	}

	const std::vector<std::string_view> ScriptArgs = {"alpha", "beta"};
	constexpr std::string_view ReferenceSource = R"(
int GlobalValue = 4;
int& global_ref() { return GlobalValue; }
int main(string[] args) {
	int& alias = global_ref();
	function()->int update = [&, args]() -> int {
		alias += len(args);
		return alias;
	};
	return update();
}
)";

	for (const VeroScript::WebAssembly::EBackend Backend : {
			 VeroScript::WebAssembly::EBackend::Vm,
		VeroScript::WebAssembly::EBackend::Mixed})
	{
		const VeroScript::WebAssembly::FResult BackendRun = Runtime.RunSource(
			ReferenceSource,
			"backend.vs",
			{},
			ScriptArgs,
			Backend);
		bOk &= Expect(
			BackendRun.bSuccess,
			"VM and mixed WebAssembly backends should execute references, captures, args, and int main");
		bOk &= Expect(BackendRun.ExitCode == 6, "VM and mixed WebAssembly backends should preserve int main");
		bOk &= Expect(!BackendRun.bExitRequested, "return from int main is not exit()");
		bOk &= Expect(BackendRun.Backend == Backend, "WebAssembly result should identify its backend");
	}
	const VeroScript::WebAssembly::FResult MissingJitBackend = Runtime.RunSource(
		ReferenceSource, "missing-jit-backend.vs", {}, ScriptArgs,
		VeroScript::WebAssembly::EBackend::Jit);
	bOk &= Expect(
		!MissingJitBackend.bSuccess && MissingJitBackend.Output.empty()
			&& !MissingJitBackend.Diagnostics.empty()
			&& MissingJitBackend.Diagnostics.front().MessageId
				== "diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory",
		"WebAssembly runtime should diagnose an explicit JIT request without a selected backend");

	constexpr std::string_view NumericWidthSource = R"(
int width(float value) { return 32; }
int width(double value) { return 64; }
void main() {
	int8 narrow = -128;
	uint16 wide = 65535u;
	assert(.5f < 1.f, "Web leading-dot float assertion");
	print(width(.5f));
	print(width(.5));
	print(narrow);
	print(wide);
	print(std::to_float(1.25));
}
)";
	for (const VeroScript::WebAssembly::EBackend Backend : {
			 VeroScript::WebAssembly::EBackend::Vm,
		VeroScript::WebAssembly::EBackend::Mixed})
	{
		const VeroScript::WebAssembly::FResult NumericRun = Runtime.RunSource(
			NumericWidthSource,
			"numeric-widths.vs",
			{},
			{},
			Backend);
		bOk &= Expect(
			NumericRun.bSuccess
				&& NumericRun.Output == "32\n64\n-128\n65535\n1.25\n",
			"VM and mixed WebAssembly backends should preserve primitive widths and float identity");
	}

	const VeroScript::WebAssembly::FResult StandardNamespaceRun = Runtime.RunSource(
		R"(
import "core.vs";
int main() {
	std::array<int> values = {1, 2, 3};
	type<int> descriptor = type<int>();
	return len(values) + len(std::strings::trim(" x "));
}
)",
		"standard-namespace.vs");
	bOk &= Expect(
		StandardNamespaceRun.bSuccess && StandardNamespaceRun.ExitCode == 4,
		"WebAssembly runtime should expose hierarchical standard namespaces and reflection types");

	constexpr std::string_view UniformCallSource = R"(
struct Counter { int value = 1; };
namespace Extensions {
	int add(ref Counter self, int amount) {
		self.value += amount;
		return self.value;
	}
}
using namespace Extensions;
int main() {
	Counter counter();
	return counter.add(4);
}
)";
	for (const VeroScript::WebAssembly::EBackend Backend : {
			 VeroScript::WebAssembly::EBackend::Vm,
		VeroScript::WebAssembly::EBackend::Mixed})
	{
		const VeroScript::WebAssembly::FResult UniformCallRun = Runtime.RunSource(
			UniformCallSource,
			"uniform-call.vs",
			{},
			{},
			Backend);
		bOk &= Expect(
			UniformCallRun.bSuccess && UniformCallRun.ExitCode == 5,
			"VM and mixed WebAssembly backends should support visible ref-receiver uniform calls");
	}

	constexpr std::string_view ModernLanguageSource = R"(
int DiscardCalls = 0;
int touch() { DiscardCalls += 1; return 99; }
struct Token {
	mutable int value = 0;
	explicit Token(int input) { value = input; }
	int bump() const { value += 1; return value; }
}
int main() {
	(void)touch();
	const Token token(38);
	readonly<int> published = token.bump();
	double exact = (double)published.get();
	optional<int> restored = try_cast<int>(exact);
	if (!restored.has_value() || DiscardCalls != 1) return -1;
	Token converted = (Token)restored.get();
	const array<int> locked = {1};
	const_cast<array<int>>(locked).push(2);
	return converted.bump() + len(locked);
}
)";
	for (const VeroScript::WebAssembly::EBackend Backend : {
			 VeroScript::WebAssembly::EBackend::Vm,
		VeroScript::WebAssembly::EBackend::Mixed})
	{
		const VeroScript::WebAssembly::FResult ModernLanguageRun = Runtime.RunSource(
			ModernLanguageSource,
			"modern-language.vs",
			{},
			{},
			Backend);
		bOk &= Expect(
			ModernLanguageRun.bSuccess && ModernLanguageRun.ExitCode == 42,
			"VM and mixed WebAssembly backends should support explicit constructors, mutable fields, "
			"readonly values, cast forms, and explicit result discard");
	}

	constexpr std::string_view ExceptionSource = R"(
int main()
{
	int result = 0;
	try
	{
		throw 7;
	}
	catch (const int& value)
	{
		result = value;
	}
	finally
	{
		result += 2;
	}
	return result;
}
)";
	for (const VeroScript::WebAssembly::EBackend Backend : {
			 VeroScript::WebAssembly::EBackend::Vm,
		VeroScript::WebAssembly::EBackend::Mixed})
	{
		const VeroScript::WebAssembly::FResult ExceptionRun = Runtime.RunSource(
			ExceptionSource,
			"exception.vs",
			{},
			{},
			Backend);
		bOk &= Expect(
			ExceptionRun.bSuccess && ExceptionRun.ExitCode == 9
				&& ExceptionRun.Backend == Backend,
			"VM and mixed WebAssembly backends should preserve typed catch and finally semantics");
		const VeroScript::WebAssembly::FResult UncaughtRun = Runtime.RunSource(
			"void main() { throw \"uncaught\"; }",
			"uncaught-exception.vs",
			{},
			{},
			Backend);
		bOk &= Expect(
			!UncaughtRun.bSuccess && UncaughtRun.Backend == Backend
				&& !UncaughtRun.ErrorOutput.empty(),
			"uncaught script exceptions must remain contained by the Web API");
		const VeroScript::WebAssembly::FResult AssertRun = Runtime.RunSource(
			"void main() { assert(false, \"web assertion detail\"); }",
			"assert-message.vs",
			{},
			{},
			Backend);
		bOk &= Expect(
			!AssertRun.bSuccess && AssertRun.Backend == Backend
				&& AssertRun.ErrorOutput.find("web assertion detail") != std::string::npos,
			"assert messages must survive every WebAssembly backend error boundary");
	}

	const std::vector<VeroScript::WebAssembly::FSourceFile> SourceFiles = {
		{"helper.vs", "export int helper_value() { return 11; }"},
	};
	const VeroScript::WebAssembly::FResult OverlayRun = Runtime.RunSource(
		"import \"helper.vs\"; int main() { return helper_value(); }",
		"overlay-main.vs",
		{},
		{},
		VeroScript::WebAssembly::EBackend::Mixed,
		SourceFiles);
	bOk &= Expect(
		OverlayRun.bSuccess && OverlayRun.ExitCode == 11,
		"WebAssembly runtime should resolve in-memory virtual files");
	const auto AvailableLanguages = Runtime.GetAvailableLanguages();
	for (const std::string_view Language : {
			 "en-US", "fr-FR", "ja-JP", "ko-KR", "zh-CN", "zh-TW"})
	{
		bOk &= Expect(
			std::find(AvailableLanguages.begin(), AvailableLanguages.end(), Language)
				!= AvailableLanguages.end(),
			"WebAssembly runtime should expose every deployed localization catalog");
	}
	bOk &= Expect(Runtime.SetLanguage("zh-CN"), "WebAssembly runtime should accept zh-CN");
	bOk &= Expect(
		Runtime.SetLanguage("zh_Hans.UTF-8")
			&& Runtime.GetLanguage().rfind("zh-", 0) == 0,
		"WebAssembly runtime should expose the resolved canonical language tag");
	bOk &= Expect(
		Runtime.SetLanguage("zh-CN") && Runtime.GetLanguage() == "zh-CN",
		"WebAssembly runtime should select an exact deployed language tag");
	const VeroScript::WebAssembly::FResult LocalizedCheck = Runtime.CheckSource(
		"import \"missing.vs\";",
		"localized.vs");
	bOk &= Expect(
		!LocalizedCheck.bSuccess && !LocalizedCheck.Diagnostics.empty()
			&& VeroScript::WebAssembly::ResultToJson(LocalizedCheck, "check").find("无法解析")
				!= std::string::npos,
		"WebAssembly runtime should render embedded zh-CN diagnostics");
	bOk &= Expect(!Runtime.SetLanguage("xx-INVALID"), "unsupported Web language should fail");
	bOk &= Expect(Runtime.SetLanguage("en-US"), "WebAssembly runtime should restore en-US");
	try
	{
		(void)Runtime.RunSource(
			"void main() {}",
			"invalid-cpp-backend.vs",
			{},
			{},
			static_cast<VeroScript::WebAssembly::EBackend>(99));
		bOk &= Expect(false, "C++ API should reject an invalid backend");
	}
	catch (const std::invalid_argument&)
	{
	}

	const VeroScript::WebAssembly::FResult Check = Runtime.CheckSource(
		"void main( {",
		"broken.vs");
	bOk &= Expect(!Check.bSuccess, "invalid source should fail checking");
	bOk &= Expect(!Check.Backend.has_value(), "check results should not claim a run backend");
	bOk &= Expect(!Check.Diagnostics.empty(), "invalid source should return diagnostics");
	if (!Check.Diagnostics.empty())
	{
		bOk &= Expect(
			Check.Diagnostics.front().Location.FileLabel == "broken.vs",
			"diagnostic should retain the caller's file label");
	}

	VeroWebRuntimeHandle Handle = vero_web_create();
	bOk &= Expect(Handle != nullptr, "C ABI should create a runtime");
	bOk &= Expect(std::string(vero_web_abi_version()) == "0.1.1",
		"C ABI version should be 0.1.1");
	bOk &= Expect(vero_web_supports_threads() == 0,
		"default WebAssembly ABI should advertise the non-pthread execution contract");
	const std::string InvalidRuntime = vero_web_run_source(
		nullptr,
		"void main() {}",
		"invalid-runtime.vs",
		"");
	bOk &= Expect(
		InvalidRuntime.find("\"abiVersion\":\"0.1.1\"") != std::string::npos
			&& InvalidRuntime.find("invalid runtime handle") != std::string::npos,
		"invalid-runtime JSON should use the current ABI version");
	if (Handle)
	{
		const bool bSetSimplifiedChinese = vero_web_set_language(Handle, "zh_Hans.UTF-8") == 1;
		const std::string ResolvedChineseLanguage = vero_web_get_language(Handle);
		bOk &= Expect(
			bSetSimplifiedChinese
				&& ResolvedChineseLanguage.rfind("zh-", 0) == 0
				&& std::string(vero_web_get_available_languages(Handle)).find("\"en-US\"")
					!= std::string::npos
				&& vero_web_set_language(Handle, "xx-INVALID") == 0
				&& vero_web_set_language(Handle, "en-US") == 1,
			"C ABI should validate language tags");
		const std::string Json = vero_web_run_source(
			Handle,
			"void main() { print(\"quote: \\\" and newline\\n\"); }",
			"json.vs",
			"");
		bOk &= Expect(
			Json.find("\"operation\":\"run\"") != std::string::npos,
			"C ABI should identify run results");
		bOk &= Expect(
			Json.find("quote: \\\" and newline\\n") != std::string::npos,
			"C ABI should JSON-escape output");
		const std::string SharedJson = vero_web_run_source(
			Handle,
			"struct Point { int x = 1; } "
			"shared<T> keep_shared<T>(handle<T> value) { return value; } "
			"void main() { "
			"Point source = Point(); shared<Point> owner = shared(source); "
			"std::handle<Point> alias = owner.keep_shared(); assert(alias == owner); "
			"alias.value.x = 7; assert(owner.value.x == 7 && source.x == 1); "
			"weak<shared<Point>> observer = weak<handle<Point>>(owner); "
			"std::shared<Point> locked = observer.lock(); owner = nullptr; alias = nullptr; "
			"assert(!observer.expired() && locked.value.x == 7); locked = nullptr; "
			"assert(observer.expired() && observer.lock() == nullptr); print(\"shared-handle-ok\"); }",
			"shared-handle.vs",
			"");
		bOk &= Expect(
			SharedJson.find("\"success\":true") != std::string::npos
				&& SharedJson.find("shared-handle-ok") != std::string::npos,
			"C ABI should preserve shared/handle identity and weak lifetime");
		const char* const StableResultPointer = vero_web_run_source(
			Handle,
			"int main() { return 4; }",
			"stable-result.vs",
			"");
		const std::string StableResult = StableResultPointer;
		const char* const StableLanguagesPointer = vero_web_get_available_languages(Handle);
		const std::string StableLanguages = StableLanguagesPointer;
		const bool bResultSurvivedLanguageQuery =
			std::string(StableResultPointer) == StableResult;
		(void)vero_web_check_source(Handle, "void main() {}", "stable-languages.vs");
		bOk &= Expect(
			bResultSurvivedLanguageQuery
				&& std::string(StableLanguagesPointer) == StableLanguages,
			"runtime-owned result and language-list pointers should use independent storage");
		const char* Args[] = {"first", "second", "third"};
		const std::string ArgsJson = vero_web_run_source_with_args(
			Handle,
			"int main(string[] args) { return len(args); }",
			"args.vs",
			"",
			Args,
			3);
		bOk &= Expect(
			ArgsJson.find("\"success\":true") != std::string::npos
				&& ArgsJson.find("\"exitCode\":3") != std::string::npos
				&& ArgsJson.find("\"backend\":\"vm\"") != std::string::npos,
			"C ABI should pass script arguments and preserve int main exit code");
		if (ArgsJson.find("\"exitCode\":3") == std::string::npos)
		{
			std::cerr << "args json=[" << ArgsJson << "]\n";
		}
		const std::string InvalidArgs = vero_web_run_source_with_args(
			Handle,
			"int main(string[] args) { return len(args); }",
			"invalid-args.vs",
			"",
			nullptr,
			1);
		bOk &= Expect(
			InvalidArgs.find("args must not be null") != std::string::npos,
			"C ABI should reject a missing argument table");
		const std::string MixedJson = vero_web_run_source_with_options(
			Handle,
			"int main() { return 7; }",
			"mixed.vs",
			"",
			nullptr,
			0,
			VERO_WEB_BACKEND_MIXED);
		bOk &= Expect(
			MixedJson.find("\"success\":true") != std::string::npos
				&& MixedJson.find("\"exitCode\":7") != std::string::npos
				&& MixedJson.find("\"backend\":\"mixed\"") != std::string::npos,
			"C ABI should select mixed execution");
		const std::string JitJson = vero_web_run_source_with_options(
			Handle,
			"int main() { return 8; }",
			"jit.vs",
			"",
			nullptr,
			0,
			VERO_WEB_BACKEND_JIT);
		bOk &= Expect(
			JitJson.find("\"success\":false") != std::string::npos
				&& JitJson.find("diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory") != std::string::npos
				&& JitJson.find("\"backend\":\"jit\"") != std::string::npos
				&& JitJson.find("\"directNativeCalls\":") != std::string::npos,
			"C ABI should diagnose JIT without a selected backend");
		const std::string InvalidBackend = vero_web_run_source_with_options(
			Handle,
			"void main() {}",
			"invalid-backend.vs",
			"",
			nullptr,
			0,
			99);
		bOk &= Expect(
			InvalidBackend.find("invalid execution backend") != std::string::npos,
			"C ABI should reject an invalid backend");
		const char* OverlayPaths[] = {"overlay-helper.vs"};
		const char* OverlaySources[] = {"export int overlay_value() { return 13; }"};
		const std::string OverlayJson = vero_web_run_source_with_context(
			Handle,
			"import \"overlay-helper.vs\"; int main() { return overlay_value(); }",
			"overlay-c.vs",
			"",
			nullptr,
			0,
			VERO_WEB_BACKEND_MIXED,
			OverlayPaths,
			OverlaySources,
			1);
		bOk &= Expect(
			OverlayJson.find("\"success\":true") != std::string::npos
				&& OverlayJson.find("\"exitCode\":13") != std::string::npos,
			"C ABI should resolve in-memory virtual files");
		const std::string CompileJson = vero_web_compile_source_with_context(
			Handle,
			R"(import "io.vs";
int work(int value) { return value + 1; }
int main(string[] args) {
	int value = 0;
	for (int i = 0; i < 1000; i += 1) { value = work(value); }
	print(std::io::read_line());
	return len(args);
})",
			"compiled.vs",
			nullptr,
			nullptr,
			0);
		const std::uint32_t ProgramHandle = ProgramHandleFromJson(CompileJson);
		bOk &= Expect(
			CompileJson.find("\"operation\":\"compile\"") != std::string::npos
				&& CompileJson.find("\"compileMs\":") != std::string::npos
				&& ProgramHandle != 0,
			"C ABI should compile source once and return timing plus a program handle");
		if (ProgramHandle == 0)
		{
			std::cerr << "compile json=[" << CompileJson << "]\n";
		}
		const char* ProgramArgs[] = {"one", "two"};
		const std::string ProgramRunJson = vero_web_run_program(
			Handle,
			ProgramHandle,
			"compiled input\n",
			ProgramArgs,
			2,
			VERO_WEB_BACKEND_MIXED);
		bOk &= Expect(
			ProgramRunJson.find("\"success\":true") != std::string::npos
				&& ProgramRunJson.find("\"exitCode\":2") != std::string::npos
				&& ProgramRunJson.find("compiled input\\n") != std::string::npos
				&& ProgramRunJson.find("\"executeMs\":") != std::string::npos
				&& ProgramRunJson.find("\"compileAttempts\":0") != std::string::npos
				&& ProgramRunJson.find("\"dispatches\":0") != std::string::npos,
			"compiled programs without a backend should run through the VM with zero JIT dispatches");
		if (ProgramRunJson.find("\"success\":true") == std::string::npos
			|| ProgramRunJson.find("\"dispatches\":0") == std::string::npos)
		{
			std::cerr << "compiled run json=[" << ProgramRunJson << "]\n";
		}
		const std::string ProgramJitJson = vero_web_run_program(
			Handle, ProgramHandle, "", nullptr, 0, VERO_WEB_BACKEND_JIT);
		bOk &= Expect(
			ProgramJitJson.find("\"success\":false") != std::string::npos
				&& ProgramJitJson.find("diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory") != std::string::npos,
			"compiled programs should diagnose JIT without a selected backend");
		bOk &= Expect(
			vero_web_destroy_program(Handle, ProgramHandle) == 1
				&& vero_web_destroy_program(Handle, ProgramHandle) == 0,
			"compiled program destruction should be explicit and idempotently detectable");
		const std::string ReleasedProgram = vero_web_run_program(
			Handle, ProgramHandle, "", nullptr, 0, VERO_WEB_BACKEND_VM);
		bOk &= Expect(
			ReleasedProgram.find("invalid compiled program handle") != std::string::npos,
			"released compiled program handles should be rejected safely");
		const std::string InvalidFiles = vero_web_check_source_with_context(
			Handle,
			"void main() {}",
			"invalid-files.vs",
			nullptr,
			nullptr,
			1);
		bOk &= Expect(
			InvalidFiles.find("file_paths and file_sources must not be null")
				!= std::string::npos,
			"C ABI should reject missing virtual file tables");
		const std::string NullSource = vero_web_check_source(Handle, nullptr, "null.vs");
		bOk &= Expect(
			NullSource.find("source must not be null") != std::string::npos,
			"C ABI should reject a null source without throwing");
		bOk &= Expect(vero_web_memory_operation(Handle, 1, 0, 0, 16, 0, 0, 0) == 0, "Wasm host buffer");
		const auto Region = static_cast<uint32_t>(vero_web_memory_result(Handle, 0));
		bOk &= Expect(vero_web_memory_operation(Handle, 18, Region, 0, 1, 7, 1, UINT64_MAX) == 0,
			"Wasm uint64 big-endian write");
		bOk &= Expect(vero_web_memory_operation(Handle, 17, Region, 0, 1, 7, 1, 0) == 0 &&
						  vero_web_memory_result(Handle, 0) == UINT64_MAX,
			"Wasm lossless scalar read");
		bOk &= Expect(vero_web_memory_operation(Handle, 13, Region, 0, 0, 0, 0, 1) == 0, "Wasm write lease");
		const auto LeaseRuntime = vero_web_memory_result(Handle, 3),
				   LeaseSerial = vero_web_memory_result(Handle, 4);
		bOk &= Expect(
			vero_web_memory_operation(Handle, 4, Region, 0, 17, 0, 0, 0) == 9, "Wasm lease excludes resize");
		vero_web_destroy(Handle);
		bOk &= Expect(vero_web_memory_release_lease(LeaseRuntime, LeaseSerial) == 0,
			"Wasm lease release after runtime destruction");
	}

	return bOk ? EXIT_SUCCESS : EXIT_FAILURE;
}
