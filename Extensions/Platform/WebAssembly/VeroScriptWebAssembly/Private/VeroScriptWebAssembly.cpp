// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptWebAssembly.h"
#include "VeroScriptWebAssemblyC.h"

#include "VeroScriptEngine.h"
#include "VeroScriptFloatFormatting.h"
#include "VeroScriptHost.h"
#include "VeroScriptLocale.h"
#include "VeroScriptMemory.h"
#include "VeroScriptSession.h"
#include "VeroScriptBytecodeVmAssembly.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <sstream>
#include <streambuf>
#include <stdexcept>
#include <utility>
#include <type_traits>
#include <unordered_map>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

extern "C" char* VeroWebAwaitInputLine(void* Runtime);
#endif

namespace VeroScript::WebAssembly
{
	namespace
	{
		void AppendMilliseconds(std::string& Out, const std::optional<double>& Value)
		{
			if (Value && std::isfinite(*Value) && *Value >= 0.0)
			{
				char Buffer[32];
				const auto Converted = FloatFormattingDetail::ToCharsGeneral(Buffer, Buffer + sizeof(Buffer), *Value);
				if (Converted.ec == std::errc{})
				{
					Out.append(Buffer, Converted.ptr);
					return;
				}
			}
			Out += "null";
		}

		void AppendJsonString(std::string& Out, const std::string_view Value)
		{
			static constexpr char Hex[] = "0123456789abcdef";
			Out.push_back('"');
			for (const unsigned char Byte : Value)
			{
				switch (Byte)
				{
				case '"':
					{
						Out += "\\\"";
						break;
					}
				case '\\':
					{
						Out += "\\\\";
						break;
					}
				case '\b':
					{
						Out += "\\b";
						break;
					}
				case '\f':
					{
						Out += "\\f";
						break;
					}
				case '\n':
					{
						Out += "\\n";
						break;
					}
				case '\r':
					{
						Out += "\\r";
						break;
					}
				case '\t':
					{
						Out += "\\t";
						break;
					}
				default:
					{
						if (Byte < 0x20)
						{
							Out += "\\u00";
							Out.push_back(Hex[(Byte >> 4) & 0x0f]);
							Out.push_back(Hex[Byte & 0x0f]);
						}
						else
						{
							Out.push_back(static_cast<char>(Byte));
						}
						break;
					}
				}
			}
			Out.push_back('"');
		}

		void AppendDiagnostic(std::string& Out, const FScriptError& Diagnostic)
		{
			Out += "{\"severity\":";
			switch (Diagnostic.Severity)
			{
			case ECompileDiagnosticSeverity::Warning:
				{
					AppendJsonString(Out, "warning");
					break;
				}
			case ECompileDiagnosticSeverity::Information:
				{
					AppendJsonString(Out, "info");
					break;
				}
			case ECompileDiagnosticSeverity::Hint:
				{
					AppendJsonString(Out, "hint");
					break;
				}
			case ECompileDiagnosticSeverity::Error:
				{
					AppendJsonString(Out, "error");
					break;
				}
			default:
				{
					AppendJsonString(Out, "error");
					break;
				}
			}
			Out += ",\"message\":";
			AppendJsonString(
				Out,
				Locale::FormatDiagnosticMessage(
					Diagnostic.MessageId,
					Diagnostic.MessageArgs,
					Diagnostic.Message));
			Out += ",\"code\":";
			AppendJsonString(Out, Diagnostic.MessageId);
			Out += ",\"file\":";
			AppendJsonString(Out, Diagnostic.Location.FileLabel);
			Out += ",\"line\":" + std::to_string(Diagnostic.Location.Line);
			Out += ",\"column\":" + std::to_string(Diagnostic.Location.Column) + '}';
		}

		FResult FromRunResult(
			FRunResult Result,
			std::string Output,
			std::string ErrorOutput)
		{
			FResult WebResult;
			WebResult.bSuccess = Result.bSuccess;
			WebResult.ExitCode = Result.ExitCode;
			WebResult.bExitRequested = Result.bExitRequested;
			WebResult.Output = std::move(Output);
			WebResult.ErrorOutput = std::move(ErrorOutput);
			WebResult.Diagnostics = std::move(Result.Diagnostics);
			if (!WebResult.bSuccess && WebResult.Diagnostics.empty()
				&& (!Result.Error.Message.empty() || !Result.Error.MessageId.empty()))
			{
				WebResult.Diagnostics.push_back(std::move(Result.Error));
			}
			return WebResult;
		}

		FRunResult RunWithCompileDiagnostics(VeroScriptSession& Session, FRunResult Compilation)
		{
			FRunResult Result = Session.Run(ERunMode::Script);
			// Compilation may succeed with warnings; execution must not discard them.
			Result.Diagnostics.insert(Result.Diagnostics.begin(),
				std::make_move_iterator(Compilation.Diagnostics.begin()),
				std::make_move_iterator(Compilation.Diagnostics.end()));
			return Result;
		}

		EExecutionBackend ToCoreBackend(const EBackend Backend)
		{
			switch (Backend)
			{
			case EBackend::Vm:
				return EExecutionBackend::Vm;
			case EBackend::Mixed:
				return EExecutionBackend::Mixed;
			case EBackend::Jit:
				return EExecutionBackend::Jit;
			default:
				{
					break;
				}
			}
			throw std::invalid_argument("invalid execution backend");
		}

		FResult::FJitStatistics ToWebAssemblyJitStatistics(const FJitStats& Stats)
		{
			FResult::FJitStatistics Result;
			Result.CompileAttempts = Stats.CompileAttempts;
			Result.CompiledEntries = Stats.CompiledEntries;
			Result.BridgeTrampolineEntries = Stats.BridgeTrampolineEntries;
			Result.LinearTrampolineEntries = Stats.LinearTrampolineEntries;
			Result.VmTrampolineEntries = Stats.VmTrampolineEntries;
			Result.UnsupportedEntries = Stats.UnsupportedEntries;
			Result.DispatchAttempts = Stats.DispatchAttempts;
			Result.Dispatches = Stats.Dispatches;
			Result.Fallbacks = Stats.Fallbacks;
			Result.HotFunctions = Stats.HotFunctions;
			Result.DirectNativeCalls = Stats.DirectNativeCalls;
			return Result;
		}
	}

	bool FRuntime::SetLanguage(const std::string_view InLanguageTag)
	{
		Locale::InitializeFromEnvironment();
		if (!Locale::TrySetLanguageFromTag(InLanguageTag))
		{
			return false;
		}
		LanguageTag = Locale::GetLanguageTag();
		return true;
	}

	std::vector<std::string> FRuntime::GetAvailableLanguages() const
	{
		Locale::InitializeFromEnvironment();
		return Locale::GetAvailableLanguageTags();
	}

	std::string_view BackendName(const EBackend Backend)
	{
		switch (Backend)
		{
		case EBackend::Vm:
			return "vm";
		case EBackend::Mixed:
			return "mixed";
		case EBackend::Jit:
			return "jit";
		default:
			{
				break;
			}
		}
		return "invalid";
	}

	FResult FRuntime::RunSource(
		const std::string_view Source,
		const std::string_view FileLabel,
		const std::string_view Input,
		const std::vector<std::string_view>& ScriptArgs,
		const EBackend Backend,
		const std::vector<FSourceFile>& SourceFiles) const
	{
		ApplyLanguage();
		std::ostringstream Output;
		std::ostringstream Errors;
		std::istringstream InputStream{std::string(Input)};
		const EExecutionBackend CoreBackend = ToCoreBackend(Backend);
		FStreamEngineHost Host(Output, Errors);
		FEngineConfig Config;
		Config.MemoryLimits = {16 * 1024 * 1024, 32 * 1024 * 1024};
		Config.StandardLibraryLimits = StandardLibraryLimits;
		Config.ScriptArgs = ScriptArgs;
		Config.InputStream = &InputStream;
		Config.ExecutionBackend = CoreBackend;
		Config.SourceOverlays.reserve(SourceFiles.size());
		for (const FSourceFile& File : SourceFiles)
		{
			Config.SourceOverlays.push_back({std::string(File.Path), std::string(File.Source)});
		}
		VeroScriptSession Session(Host);
		VeroScript::BytecodeVm::Configure(Session);
		Session.ApplyConfig(Config);
		FRunResult Result = Session.LoadSource(
			Source,
			FileLabel.empty() ? std::string_view("<webassembly>") : FileLabel);
		const bool bLoaded = Result.bSuccess;
		if (bLoaded)
		{
			Result = RunWithCompileDiagnostics(Session, std::move(Result));
		}
		FResult WebResult = FromRunResult(std::move(Result), Output.str(), Errors.str());
		WebResult.Backend = Backend;
		if (bLoaded)
		{
			WebResult.Jit = ToWebAssemblyJitStatistics(Session.GetLastJitStats());
		}
		return WebResult;
	}

	FResult FRuntime::RunSourceWithInputStream(
		const std::string_view Source,
		const std::string_view FileLabel,
		std::istream& InputStream,
		const std::vector<std::string_view>& ScriptArgs,
		const EBackend Backend,
		const std::vector<FSourceFile>& SourceFiles) const
	{
		ApplyLanguage();
		std::ostringstream Output;
		std::ostringstream Errors;
		const EExecutionBackend CoreBackend = ToCoreBackend(Backend);
		FStreamEngineHost Host(Output, Errors);
		FEngineConfig Config;
		Config.MemoryLimits = {16 * 1024 * 1024, 32 * 1024 * 1024};
		Config.StandardLibraryLimits = StandardLibraryLimits;
		Config.ScriptArgs = ScriptArgs;
		Config.InputStream = &InputStream;
		Config.ExecutionBackend = CoreBackend;
		Config.SourceOverlays.reserve(SourceFiles.size());
		for (const FSourceFile& File : SourceFiles)
		{
			Config.SourceOverlays.push_back({std::string(File.Path), std::string(File.Source)});
		}
		VeroScriptSession Session(Host);
		VeroScript::BytecodeVm::Configure(Session);
		Session.ApplyConfig(Config);
		FRunResult Result = Session.LoadSource(
			Source,
			FileLabel.empty() ? std::string_view("<webassembly>") : FileLabel);
		const bool bLoaded = Result.bSuccess;
		if (bLoaded)
		{
			Result = RunWithCompileDiagnostics(Session, std::move(Result));
		}
		FResult WebResult = FromRunResult(std::move(Result), Output.str(), Errors.str());
		WebResult.Backend = Backend;
		if (bLoaded)
		{
			WebResult.Jit = ToWebAssemblyJitStatistics(Session.GetLastJitStats());
		}
		return WebResult;
	}

	FResult FRuntime::CheckSource(
		const std::string_view Source,
		const std::string_view FileLabel,
		const std::vector<FSourceFile>& SourceFiles) const
	{
		ApplyLanguage();
		std::ostringstream Errors;
		std::ostringstream Output;
		FStreamEngineHost Host(Output, Errors);
		FEngineConfig Config;
		VeroScript::BytecodeVm::Configure(Config);
		Config.MemoryLimits = {16 * 1024 * 1024, 32 * 1024 * 1024};
		Config.StandardLibraryLimits = StandardLibraryLimits;
		Config.SourceOverlays.reserve(SourceFiles.size());
		for (const FSourceFile& File : SourceFiles)
		{
			Config.SourceOverlays.push_back({std::string(File.Path), std::string(File.Source)});
		}
		FRunResult Result = VeroScriptEngine::CheckSource(
			Source,
			FileLabel.empty() ? std::string_view("<webassembly>") : FileLabel,
			Host,
			Config);
		return FromRunResult(std::move(Result), {}, Errors.str());
	}

	void FRuntime::ApplyLanguage() const
	{
		Locale::InitializeFromEnvironment();
		(void)Locale::TrySetLanguageFromTag(LanguageTag);
	}

	std::string ResultToJson(const FResult& Result, const std::string_view Operation)
	{
		std::string Json;
		Json.reserve(Result.Output.size() + Result.ErrorOutput.size() + 256);
		Json += "{\"abiVersion\":";
		AppendJsonString(Json, AbiVersion);
		Json += ",\"operation\":";
		AppendJsonString(Json, Operation);
		Json += ",\"success\":";
		Json += Result.bSuccess ? "true" : "false";
		Json += ",\"exitCode\":" + std::to_string(Result.ExitCode);
		Json += ",\"exitRequested\":";
		Json += Result.bExitRequested ? "true" : "false";
		Json += ",\"backend\":";
		if (Result.Backend)
		{
			AppendJsonString(Json, BackendName(*Result.Backend));
		}
		else
		{
			Json += "null";
		}
		Json += ",\"output\":";
		AppendJsonString(Json, Result.Output);
		Json += ",\"errorOutput\":";
		AppendJsonString(Json, Result.ErrorOutput);
		Json += ",\"diagnostics\":[";
		for (std::size_t Index = 0; Index < Result.Diagnostics.size(); ++Index)
		{
			if (Index != 0)
			{
				Json.push_back(',');
			}
			AppendDiagnostic(Json, Result.Diagnostics[Index]);
		}
		Json += "],\"programHandle\":";
		Json += Result.ProgramHandle ? std::to_string(*Result.ProgramHandle) : "null";
		Json += ",\"timings\":";
		if (Result.CompileMilliseconds || Result.ExecuteMilliseconds)
		{
			Json += "{\"compileMs\":";
			AppendMilliseconds(Json, Result.CompileMilliseconds);
			Json += ",\"executeMs\":";
			AppendMilliseconds(Json, Result.ExecuteMilliseconds);
			Json += '}';
		}
		else
		{
			Json += "null";
		}
		Json += ",\"jit\":";
		if (Result.Jit)
		{
			const FResult::FJitStatistics& Jit = *Result.Jit;
			Json += "{\"compileAttempts\":" + std::to_string(Jit.CompileAttempts);
			Json += ",\"compiledEntries\":" + std::to_string(Jit.CompiledEntries);
			Json += ",\"bridgeTrampolineEntries\":" + std::to_string(Jit.BridgeTrampolineEntries);
			Json += ",\"linearTrampolineEntries\":" + std::to_string(Jit.LinearTrampolineEntries);
			Json += ",\"vmTrampolineEntries\":" + std::to_string(Jit.VmTrampolineEntries);
			Json += ",\"unsupportedEntries\":" + std::to_string(Jit.UnsupportedEntries);
			Json += ",\"dispatchAttempts\":" + std::to_string(Jit.DispatchAttempts);
			Json += ",\"dispatches\":" + std::to_string(Jit.Dispatches);
			Json += ",\"fallbacks\":" + std::to_string(Jit.Fallbacks);
			Json += ",\"hotFunctions\":" + std::to_string(Jit.HotFunctions);
			Json += ",\"directNativeCalls\":" + std::to_string(Jit.DirectNativeCalls) + '}';
		}
		else
		{
			Json += "null";
		}
		Json += '}';
		return Json;
	}
}

namespace
{
	#if defined(__EMSCRIPTEN__)
	class FInteractiveInputBuffer final : public std::streambuf
	{
	public:
		explicit FInteractiveInputBuffer(const VeroWebRuntimeHandle InRuntime)
			: Runtime(InRuntime)
		{
		}

	protected:
		int_type underflow() override
		{
			if (gptr() && gptr() < egptr())
			{
				return traits_type::to_int_type(*gptr());
			}
			std::unique_ptr<char, decltype(&std::free)> Input(
				VeroWebAwaitInputLine(Runtime), &std::free);
			if (!Input)
			{
				return traits_type::eof();
			}
			// Keep ownership even if copying the submitted line runs out of memory.
			Line = Input.get();
			Input.reset();
			if (Line.empty() || Line.back() != '\n')
			{
				Line.push_back('\n');
			}
			setg(Line.data(), Line.data(), Line.data() + Line.size());
			return traits_type::to_int_type(*gptr());
		}

	private:
		VeroWebRuntimeHandle Runtime = nullptr;
		std::string Line;
	};
	#endif

	struct FWebRuntimeState
	{
		struct FProgram
		{
			FProgram()
				: Host(Output, Errors), Session(Host)
			{
				VeroScript::BytecodeVm::Configure(Session);
			}

			void ResetStreams()
			{
				Output.str({});
				Output.clear();
				Errors.str({});
				Errors.clear();
			}

			std::ostringstream Output;
			std::ostringstream Errors;
			VeroScript::FStreamEngineHost Host;
			VeroScript::VeroScriptSession Session;

		};

		std::uint32_t AddProgram(std::unique_ptr<FProgram> Program)
		{
			for (std::uint64_t Attempt = 0; Attempt < std::numeric_limits<std::uint32_t>::max(); ++Attempt)
			{
				const std::uint32_t Candidate = NextProgramHandle++;
				if (NextProgramHandle == 0)
				{
					NextProgramHandle = 1;
				}
				if (Candidate != 0 && Programs.find(Candidate) == Programs.end())
				{
					Programs.emplace(Candidate, std::move(Program));
					return Candidate;
				}
			}
			throw std::runtime_error("compiled program handle space exhausted");
		}

		VeroScript::WebAssembly::FRuntime Runtime;
		std::string LastJson;
		std::string AvailableLanguagesJson;
		std::unordered_map<std::uint32_t, std::unique_ptr<FProgram>> Programs;
		std::uint32_t NextProgramHandle = 1;
		VeroScript::FMemoryContext MemoryContext{{16 * 1024 * 1024, 32 * 1024 * 1024}};
		std::unordered_map<std::uint32_t, VeroScript::FValue> MemoryValues;
		std::uint32_t NextMemoryHandle = 1;
		std::uint64_t MemoryResult[8]{};
	};

	// Asyncify state, the JavaScript input provider, and localization catalogs
	// are shared by every runtime in one Wasm module instance. Block all raw ABI
	// activity while any runtime is suspended so a second runtime cannot mutate
	// module-wide state during an interactive call.
	bool GInteractiveExecutionActive = false;

	bool IsInteractiveExecutionActive() noexcept
	{
		return GInteractiveExecutionActive;
	}

	constexpr const char* InvalidRuntimeJson =
		"{\"abiVersion\":\"" VERO_WEB_ABI_VERSION "\""
		",\"operation\":\"api\",\"success\":false,"
		"\"exitCode\":2,\"exitRequested\":false,\"backend\":null,\"output\":\"\","
		"\"errorOutput\":\"invalid runtime handle\",\"diagnostics\":[],"
		"\"programHandle\":null,\"timings\":null,\"jit\":null}";
	constexpr const char* BusyRuntimeJson =
		"{\"abiVersion\":\"" VERO_WEB_ABI_VERSION "\""
		",\"operation\":\"api\",\"success\":false,"
		"\"exitCode\":2,\"exitRequested\":false,\"backend\":null,\"output\":\"\","
		"\"errorOutput\":\"runtime is awaiting interactive input\",\"diagnostics\":[],"
		"\"programHandle\":null,\"timings\":null,\"jit\":null}";

	class FInteractiveExecutionGuard final
	{
	public:
		FInteractiveExecutionGuard() noexcept
			: bAcquired(!GInteractiveExecutionActive)
		{
			if (bAcquired)
			{
				GInteractiveExecutionActive = true;
			}
		}

		~FInteractiveExecutionGuard()
		{
			if (bAcquired)
			{
				GInteractiveExecutionActive = false;
			}
		}

		FInteractiveExecutionGuard(const FInteractiveExecutionGuard&) = delete;
		FInteractiveExecutionGuard& operator=(const FInteractiveExecutionGuard&) = delete;

		bool IsAcquired() const noexcept
		{
			return bAcquired;
		}

	private:
		bool bAcquired;
	};

	const char* StoreApiFailure(FWebRuntimeState& State, const char* Message) noexcept
	{
		try
		{
			VeroScript::WebAssembly::FResult Result;
			Result.ExitCode = 2;
			Result.ErrorOutput = Message;
			State.LastJson = VeroScript::WebAssembly::ResultToJson(Result, "api");
			return State.LastJson.c_str();
		}
		catch (...)
		{
			return InvalidRuntimeJson;
		}
	}

	bool BuildScriptArgs(
		const char* const* const Args,
		const uint32_t ArgCount,
		std::vector<std::string_view>& Out,
		const char*& OutError)
	{
		Out.clear();
		if (ArgCount != 0 && !Args)
		{
			OutError = "args must not be null when arg_count is non-zero";
			return false;
		}
		Out.reserve(ArgCount);
		for (uint32_t Index = 0; Index < ArgCount; ++Index)
		{
			if (!Args[Index])
			{
				OutError = "script arguments must not contain null";
				return false;
			}
			Out.emplace_back(Args[Index]);
		}
		return true;
	}

	bool BuildSourceFiles(
		const char* const* const Paths,
		const char* const* const Sources,
		const uint32_t Count,
		std::vector<VeroScript::WebAssembly::FSourceFile>& Out,
		const char*& OutError)
	{
		Out.clear();
		if (Count != 0 && (!Paths || !Sources))
		{
			OutError = "file_paths and file_sources must not be null when file_count is non-zero";
			return false;
		}
		Out.reserve(Count);
		for (uint32_t Index = 0; Index < Count; ++Index)
		{
			if (!Paths[Index] || !Sources[Index])
			{
				OutError = "virtual file entries must not contain null";
				return false;
			}
			if (Paths[Index][0] == '\0')
			{
				OutError = "virtual file paths must not be empty";
				return false;
			}
			Out.push_back({Paths[Index], Sources[Index]});
		}
		return true;
	}

	bool ParseBackend(
		const uint32_t Backend,
		VeroScript::WebAssembly::EBackend& Out)
	{
		switch (Backend)
		{
		case VERO_WEB_BACKEND_VM:
			{
				Out = VeroScript::WebAssembly::EBackend::Vm;
				return true;
			}
		case VERO_WEB_BACKEND_MIXED:
			{
				Out = VeroScript::WebAssembly::EBackend::Mixed;
				return true;
			}
		case VERO_WEB_BACKEND_JIT:
			{
				Out = VeroScript::WebAssembly::EBackend::Jit;
				return true;
			}
		default:
			return false;
		}
	}

	VeroScript::WebAssembly::FResult RunProgram(
		FWebRuntimeState::FProgram& Program,
		const std::vector<std::string_view>& ScriptArgs,
		const VeroScript::WebAssembly::EBackend Backend,
		std::istream* const Input, const VeroScript::FStandardLibraryLimits& Limits)
	{
		Program.ResetStreams();
		VeroScript::FEngineConfig Config;
		Config.MemoryLimits = {16 * 1024 * 1024, 32 * 1024 * 1024};
		Config.ScriptArgs = ScriptArgs;
		Config.StandardLibraryLimits = Limits;
		Config.InputStream = Input;
		Config.ExecutionBackend = VeroScript::WebAssembly::ToCoreBackend(Backend);
		Program.Session.ApplyConfig(Config);
		const auto Start = std::chrono::steady_clock::now();
		VeroScript::FRunResult RunResult = Program.Session.Run();
		const auto End = std::chrono::steady_clock::now();
		VeroScript::WebAssembly::FResult Result = VeroScript::WebAssembly::FromRunResult(
			std::move(RunResult), Program.Output.str(), Program.Errors.str());
		Result.Backend = Backend;
		Result.ExecuteMilliseconds = std::chrono::duration<double, std::milli>(End - Start).count();
		Result.Jit = VeroScript::WebAssembly::ToWebAssemblyJitStatistics(Program.Session.GetLastJitStats());
		return Result;
	}
}

extern "C" uint32_t vero_web_set_stdlib_limits(VeroWebRuntimeHandle Runtime,
	uint64_t Text, uint64_t Read, uint64_t Parts)
{
	if (!Runtime) return 1;
	if (IsInteractiveExecutionActive()) return 9;
	static_cast<FWebRuntimeState*>(Runtime)->Runtime.SetStandardLibraryLimits({Text, Read, Parts});
	return 0;
}

extern "C" const char* vero_web_abi_version(void)
{
	return VERO_WEB_ABI_VERSION;
}

extern "C" uint32_t vero_web_supports_threads(void)
{
	#if defined(__EMSCRIPTEN_PTHREADS__)
	return 1u;
	#else
	return 0u;
	#endif
}

extern "C" VeroWebRuntimeHandle vero_web_create(void)
{
	if (IsInteractiveExecutionActive())
	{
		return nullptr;
	}
	try
	{
		return new (std::nothrow) FWebRuntimeState();
	}
	catch (...)
	{
		return nullptr;
	}
}

extern "C" void vero_web_destroy(const VeroWebRuntimeHandle runtime)
{
	auto* const State = static_cast<FWebRuntimeState*>(runtime);
	if (!State || IsInteractiveExecutionActive())
	{
		return;
	}
	delete State;
}

extern "C" uint32_t vero_web_set_language(
	const VeroWebRuntimeHandle runtime,
	const char* const language_tag)
{
	auto* const State = static_cast<FWebRuntimeState*>(runtime);
	if (!State || IsInteractiveExecutionActive() || !language_tag)
	{
		return 0;
	}
	try
	{
		return State->Runtime.SetLanguage(language_tag) ? 1u : 0u;
	}
	catch (...)
	{
		return 0;
	}
}

extern "C" const char* vero_web_get_language(const VeroWebRuntimeHandle runtime)
{
	const auto* const State = static_cast<const FWebRuntimeState*>(runtime);
	return State && !IsInteractiveExecutionActive()
		? State->Runtime.GetLanguage().data()
		: nullptr;
}

extern "C" const char* vero_web_get_available_languages(
	const VeroWebRuntimeHandle runtime)
{
	auto* const State = static_cast<FWebRuntimeState*>(runtime);
	if (!State || IsInteractiveExecutionActive())
	{
		return nullptr;
	}
	try
	{
		State->AvailableLanguagesJson = "[";
		const auto Languages = State->Runtime.GetAvailableLanguages();
		for (std::size_t Index = 0; Index < Languages.size(); ++Index)
		{
			if (Index != 0)
			{
				State->AvailableLanguagesJson.push_back(',');
			}
			VeroScript::WebAssembly::AppendJsonString(State->AvailableLanguagesJson, Languages[Index]);
		}
		State->AvailableLanguagesJson.push_back(']');
		return State->AvailableLanguagesJson.c_str();
	}
	catch (...)
	{
		return nullptr;
	}
}

extern "C" const char* vero_web_run_source(
	const VeroWebRuntimeHandle runtime,
	const char* const source,
	const char* const file_label,
	const char* const input)
{
	auto* const State = static_cast<FWebRuntimeState*>(runtime);
	if (!State)
	{
		return InvalidRuntimeJson;
	}
	if (IsInteractiveExecutionActive())
	{
		return BusyRuntimeJson;
	}
	if (!source)
	{
		return StoreApiFailure(*State, "source must not be null");
	}
	try
	{
		const VeroScript::WebAssembly::FResult Result = State->Runtime.RunSource(
			source,
			file_label ? file_label : "<webassembly>",
			input ? input : "",
			{},
			VeroScript::WebAssembly::EBackend::Vm);
		State->LastJson = VeroScript::WebAssembly::ResultToJson(Result, "run");
		return State->LastJson.c_str();
	}
	catch (const std::exception& Exception)
	{
		return StoreApiFailure(*State, Exception.what());
	}
	catch (...)
	{
		return StoreApiFailure(*State, "unknown WebAssembly runtime failure");
	}
}

extern "C" const char* vero_web_run_source_with_args(
	const VeroWebRuntimeHandle runtime,
	const char* const source,
	const char* const file_label,
	const char* const input,
	const char* const* const args,
	const uint32_t arg_count)
{
	auto* const State = static_cast<FWebRuntimeState*>(runtime);
	if (!State)
	{
		return InvalidRuntimeJson;
	}
	if (IsInteractiveExecutionActive())
	{
		return BusyRuntimeJson;
	}
	if (!source)
	{
		return StoreApiFailure(*State, "source must not be null");
	}
	if (arg_count != 0 && !args)
	{
		return StoreApiFailure(*State, "args must not be null when arg_count is non-zero");
	}
	try
	{
		std::vector<std::string_view> ScriptArgs;
		ScriptArgs.reserve(arg_count);
		for (uint32_t Index = 0; Index < arg_count; ++Index)
		{
			if (!args[Index])
			{
				return StoreApiFailure(*State, "script arguments must not contain null");
			}
			ScriptArgs.emplace_back(args[Index]);
		}
		const VeroScript::WebAssembly::FResult Result = State->Runtime.RunSource(
			source,
			file_label ? file_label : "<webassembly>",
			input ? input : "",
			ScriptArgs,
			VeroScript::WebAssembly::EBackend::Vm);
		State->LastJson = VeroScript::WebAssembly::ResultToJson(Result, "run");
		return State->LastJson.c_str();
	}
	catch (const std::exception& Exception)
	{
		return StoreApiFailure(*State, Exception.what());
	}
	catch (...)
	{
		return StoreApiFailure(*State, "unknown WebAssembly runtime failure");
	}
}

extern "C" const char* vero_web_run_source_with_options(
	const VeroWebRuntimeHandle runtime,
	const char* const source,
	const char* const file_label,
	const char* const input,
	const char* const* const args,
	const uint32_t arg_count,
	const uint32_t backend)
{
	auto* const State = static_cast<FWebRuntimeState*>(runtime);
	if (!State)
	{
		return InvalidRuntimeJson;
	}
	if (IsInteractiveExecutionActive())
	{
		return BusyRuntimeJson;
	}
	if (!source)
	{
		return StoreApiFailure(*State, "source must not be null");
	}
	if (arg_count != 0 && !args)
	{
		return StoreApiFailure(*State, "args must not be null when arg_count is non-zero");
	}
	VeroScript::WebAssembly::EBackend WebAssemblyBackend;
	switch (backend)
	{
	case VERO_WEB_BACKEND_VM:
		{
			WebAssemblyBackend = VeroScript::WebAssembly::EBackend::Vm;
			break;
		}
	case VERO_WEB_BACKEND_MIXED:
		{
			WebAssemblyBackend = VeroScript::WebAssembly::EBackend::Mixed;
			break;
		}
	case VERO_WEB_BACKEND_JIT:
		{
			WebAssemblyBackend = VeroScript::WebAssembly::EBackend::Jit;
			break;
		}
	default:
		return StoreApiFailure(*State, "invalid execution backend");
	}
	try
	{
		std::vector<std::string_view> ScriptArgs;
		ScriptArgs.reserve(arg_count);
		for (uint32_t Index = 0; Index < arg_count; ++Index)
		{
			if (!args[Index])
			{
				return StoreApiFailure(*State, "script arguments must not contain null");
			}
			ScriptArgs.emplace_back(args[Index]);
		}
		const VeroScript::WebAssembly::FResult Result = State->Runtime.RunSource(
			source,
			file_label ? file_label : "<webassembly>",
			input ? input : "",
			ScriptArgs,
			WebAssemblyBackend);
		State->LastJson = VeroScript::WebAssembly::ResultToJson(Result, "run");
		return State->LastJson.c_str();
	}
	catch (const std::exception& Exception)
	{
		return StoreApiFailure(*State, Exception.what());
	}
	catch (...)
	{
		return StoreApiFailure(*State, "unknown WebAssembly runtime failure");
	}
}

extern "C" const char* vero_web_run_source_with_context(
	const VeroWebRuntimeHandle runtime,
	const char* const source,
	const char* const file_label,
	const char* const input,
	const char* const* const args,
	const uint32_t arg_count,
	const uint32_t backend,
	const char* const* const file_paths,
	const char* const* const file_sources,
	const uint32_t file_count)
{
	auto* const State = static_cast<FWebRuntimeState*>(runtime);
	if (!State)
	{
		return InvalidRuntimeJson;
	}
	if (IsInteractiveExecutionActive())
	{
		return BusyRuntimeJson;
	}
	if (!source)
	{
		return StoreApiFailure(*State, "source must not be null");
	}
	VeroScript::WebAssembly::EBackend WebAssemblyBackend;
	switch (backend)
	{
	case VERO_WEB_BACKEND_VM:
		{
			WebAssemblyBackend = VeroScript::WebAssembly::EBackend::Vm;
			break;
		}
	case VERO_WEB_BACKEND_MIXED:
		{
			WebAssemblyBackend = VeroScript::WebAssembly::EBackend::Mixed;
			break;
		}
	case VERO_WEB_BACKEND_JIT:
		{
			WebAssemblyBackend = VeroScript::WebAssembly::EBackend::Jit;
			break;
		}
	default:
		return StoreApiFailure(*State, "invalid execution backend");
	}
	try
	{
		const char* Error = nullptr;
		std::vector<std::string_view> ScriptArgs;
		if (!BuildScriptArgs(args, arg_count, ScriptArgs, Error))
		{
			return StoreApiFailure(*State, Error);
		}
		std::vector<VeroScript::WebAssembly::FSourceFile> SourceFiles;
		if (!BuildSourceFiles(file_paths, file_sources, file_count, SourceFiles, Error))
		{
			return StoreApiFailure(*State, Error);
		}
		const VeroScript::WebAssembly::FResult Result = State->Runtime.RunSource(
			source,
			file_label ? file_label : "<webassembly>",
			input ? input : "",
			ScriptArgs,
			WebAssemblyBackend,
			SourceFiles);
		State->LastJson = VeroScript::WebAssembly::ResultToJson(Result, "run");
		return State->LastJson.c_str();
	}
	catch (const std::exception& Exception)
	{
		return StoreApiFailure(*State, Exception.what());
	}
	catch (...)
	{
		return StoreApiFailure(*State, "unknown WebAssembly runtime failure");
	}
}

extern "C" const char* vero_web_compile_source_with_context(
	const VeroWebRuntimeHandle runtime,
	const char* const source,
	const char* const file_label,
	const char* const* const file_paths,
	const char* const* const file_sources,
	const uint32_t file_count)
{
	auto* const State = static_cast<FWebRuntimeState*>(runtime);
	if (!State)
	{
		return InvalidRuntimeJson;
	}
	if (IsInteractiveExecutionActive())
	{
		return BusyRuntimeJson;
	}
	if (!source)
	{
		return StoreApiFailure(*State, "source must not be null");
	}
	try
	{
		const char* Error = nullptr;
		std::vector<VeroScript::WebAssembly::FSourceFile> SourceFiles;
		if (!BuildSourceFiles(file_paths, file_sources, file_count, SourceFiles, Error))
		{
			return StoreApiFailure(*State, Error);
		}
		(void)State->Runtime.SetLanguage(State->Runtime.GetLanguage());
		auto Program = std::make_unique<FWebRuntimeState::FProgram>();
		VeroScript::FEngineConfig Config;
		Config.StandardLibraryLimits = State->Runtime.GetStandardLibraryLimits();
		Config.MemoryLimits = {16 * 1024 * 1024, 32 * 1024 * 1024};
		Config.SourceOverlays.reserve(SourceFiles.size());
		for (const VeroScript::WebAssembly::FSourceFile& File : SourceFiles)
		{
			Config.SourceOverlays.push_back({std::string(File.Path), std::string(File.Source)});
		}
		Program->Session.ApplyConfig(Config);
		const auto Start = std::chrono::steady_clock::now();
		VeroScript::FRunResult CompileResult = Program->Session.LoadSource(
			source, file_label && file_label[0] != '\0' ? file_label : "<webassembly>");
		const auto End = std::chrono::steady_clock::now();
		VeroScript::WebAssembly::FResult Result = VeroScript::WebAssembly::FromRunResult(
			std::move(CompileResult), Program->Output.str(), Program->Errors.str());
		Result.CompileMilliseconds =
			std::chrono::duration<double, std::milli>(End - Start).count();
		if (Result.bSuccess)
		{
			Result.ProgramHandle = State->AddProgram(std::move(Program));
		}
		try
		{
			State->LastJson = VeroScript::WebAssembly::ResultToJson(Result, "compile");
		}
		catch (...)
		{
			// The caller cannot release a program whose handle was never returned.
			if (Result.ProgramHandle)
			{
				State->Programs.erase(*Result.ProgramHandle);
			}
			throw;
		}
		return State->LastJson.c_str();
	}
	catch (const std::exception& Exception)
	{
		return StoreApiFailure(*State, Exception.what());
	}
	catch (...)
	{
		return StoreApiFailure(*State, "unknown WebAssembly compile failure");
	}
}

extern "C" const char* vero_web_run_program(
	const VeroWebRuntimeHandle runtime,
	const uint32_t program_handle,
	const char* const input,
	const char* const* const args,
	const uint32_t arg_count,
	const uint32_t backend)
{
	auto* const State = static_cast<FWebRuntimeState*>(runtime);
	if (!State)
	{
		return InvalidRuntimeJson;
	}
	if (IsInteractiveExecutionActive())
	{
		return BusyRuntimeJson;
	}
	const auto ProgramIt = State->Programs.find(program_handle);
	if (ProgramIt == State->Programs.end())
	{
		return StoreApiFailure(*State, "invalid compiled program handle");
	}
	VeroScript::WebAssembly::EBackend WebAssemblyBackend;
	if (!ParseBackend(backend, WebAssemblyBackend))
	{
		return StoreApiFailure(*State, "invalid execution backend");
	}
	try
	{
		const char* Error = nullptr;
		std::vector<std::string_view> ScriptArgs;
		if (!BuildScriptArgs(args, arg_count, ScriptArgs, Error))
		{
			return StoreApiFailure(*State, Error);
		}
		(void)State->Runtime.SetLanguage(State->Runtime.GetLanguage());
		std::istringstream Input{input ? input : ""};
		VeroScript::WebAssembly::FResult Result = RunProgram(
			*ProgramIt->second, ScriptArgs, WebAssemblyBackend, &Input, State->Runtime.GetStandardLibraryLimits());
		State->LastJson = VeroScript::WebAssembly::ResultToJson(Result, "run");
		return State->LastJson.c_str();
	}
	catch (const std::exception& Exception)
	{
		return StoreApiFailure(*State, Exception.what());
	}
	catch (...)
	{
		return StoreApiFailure(*State, "unknown compiled program failure");
	}
}

extern "C" const char* vero_web_run_program_interactive(
	const VeroWebRuntimeHandle runtime,
	const uint32_t program_handle,
	const char* const* const args,
	const uint32_t arg_count,
	const uint32_t backend)
{
	#if !defined(__EMSCRIPTEN__)
	(void)program_handle;
	(void)args;
	(void)arg_count;
	(void)backend;
	auto* const State = static_cast<FWebRuntimeState*>(runtime);
	return State ? StoreApiFailure(*State, "interactive input requires the WebAssembly build")
		: InvalidRuntimeJson;
	#else
	auto* const State = static_cast<FWebRuntimeState*>(runtime);
	if (!State)
	{
		return InvalidRuntimeJson;
	}
	if (IsInteractiveExecutionActive())
	{
		return BusyRuntimeJson;
	}
	const auto ProgramIt = State->Programs.find(program_handle);
	if (ProgramIt == State->Programs.end())
	{
		return StoreApiFailure(*State, "invalid compiled program handle");
	}
	VeroScript::WebAssembly::EBackend WebAssemblyBackend;
	if (!ParseBackend(backend, WebAssemblyBackend))
	{
		return StoreApiFailure(*State, "invalid execution backend");
	}
	try
	{
		const char* Error = nullptr;
		std::vector<std::string_view> ScriptArgs;
		if (!BuildScriptArgs(args, arg_count, ScriptArgs, Error))
		{
			return StoreApiFailure(*State, Error);
		}
		FInteractiveExecutionGuard ExecutionGuard;
		if (!ExecutionGuard.IsAcquired())
		{
			return BusyRuntimeJson;
		}
		(void)State->Runtime.SetLanguage(State->Runtime.GetLanguage());
		FInteractiveInputBuffer InputBuffer(runtime);
		std::istream Input(&InputBuffer);
		// EOF is normal; a failed input-buffer allocation must remain an API failure.
		Input.exceptions(std::ios::badbit);
		VeroScript::WebAssembly::FResult Result = RunProgram(
			*ProgramIt->second, ScriptArgs, WebAssemblyBackend, &Input, State->Runtime.GetStandardLibraryLimits());
		State->LastJson = VeroScript::WebAssembly::ResultToJson(Result, "run");
		return State->LastJson.c_str();
	}
	catch (const std::exception& Exception)
	{
		return StoreApiFailure(*State, Exception.what());
	}
	catch (...)
	{
		return StoreApiFailure(*State, "unknown compiled program failure");
	}
	#endif
}

extern "C" uint32_t vero_web_destroy_program(
	const VeroWebRuntimeHandle runtime,
	const uint32_t program_handle)
{
	auto* const State = static_cast<FWebRuntimeState*>(runtime);
	return State && !IsInteractiveExecutionActive() && program_handle != 0
		&& State->Programs.erase(program_handle) != 0 ? 1u : 0u;
}

extern "C" const char* vero_web_check_source(
	const VeroWebRuntimeHandle runtime,
	const char* const source,
	const char* const file_label)
{
	auto* const State = static_cast<FWebRuntimeState*>(runtime);
	if (!State)
	{
		return InvalidRuntimeJson;
	}
	if (IsInteractiveExecutionActive())
	{
		return BusyRuntimeJson;
	}
	if (!source)
	{
		return StoreApiFailure(*State, "source must not be null");
	}
	try
	{
		const VeroScript::WebAssembly::FResult Result = State->Runtime.CheckSource(
			source,
			file_label ? file_label : "<webassembly>");
		State->LastJson = VeroScript::WebAssembly::ResultToJson(Result, "check");
		return State->LastJson.c_str();
	}
	catch (const std::exception& Exception)
	{
		return StoreApiFailure(*State, Exception.what());
	}
	catch (...)
	{
		return StoreApiFailure(*State, "unknown WebAssembly runtime failure");
	}
}

extern "C" const char* vero_web_run_source_interactive_with_context(
	const VeroWebRuntimeHandle runtime,
	const char* const source,
	const char* const file_label,
	const char* const* const args,
	const uint32_t arg_count,
	const uint32_t backend,
	const char* const* const file_paths,
	const char* const* const file_sources,
	const uint32_t file_count)
{
	#if !defined(__EMSCRIPTEN__)
	(void)source;
	(void)file_label;
	(void)args;
	(void)arg_count;
	(void)backend;
	(void)file_paths;
	(void)file_sources;
	(void)file_count;
	auto* const State = static_cast<FWebRuntimeState*>(runtime);
	return State ? StoreApiFailure(*State, "interactive input requires the WebAssembly build")
		: InvalidRuntimeJson;
	#else
	auto* const State = static_cast<FWebRuntimeState*>(runtime);
	if (!State)
	{
		return InvalidRuntimeJson;
	}
	if (IsInteractiveExecutionActive())
	{
		return BusyRuntimeJson;
	}
	if (!source)
	{
		return StoreApiFailure(*State, "source must not be null");
	}
	VeroScript::WebAssembly::EBackend WebAssemblyBackend;
	switch (backend)
	{
	case VERO_WEB_BACKEND_VM:
		{
			WebAssemblyBackend = VeroScript::WebAssembly::EBackend::Vm;
			break;
		}
	case VERO_WEB_BACKEND_MIXED:
		{
			WebAssemblyBackend = VeroScript::WebAssembly::EBackend::Mixed;
			break;
		}
	case VERO_WEB_BACKEND_JIT:
		{
			WebAssemblyBackend = VeroScript::WebAssembly::EBackend::Jit;
			break;
		}
	default:
		return StoreApiFailure(*State, "invalid execution backend");
	}
	try
	{
		const char* Error = nullptr;
		std::vector<std::string_view> ScriptArgs;
		if (!BuildScriptArgs(args, arg_count, ScriptArgs, Error))
		{
			return StoreApiFailure(*State, Error);
		}
		std::vector<VeroScript::WebAssembly::FSourceFile> SourceFiles;
		if (!BuildSourceFiles(file_paths, file_sources, file_count, SourceFiles, Error))
		{
			return StoreApiFailure(*State, Error);
		}
		FInteractiveExecutionGuard ExecutionGuard;
		if (!ExecutionGuard.IsAcquired())
		{
			return BusyRuntimeJson;
		}
		FInteractiveInputBuffer InputBuffer(runtime);
		std::istream Input(&InputBuffer);
		Input.exceptions(std::ios::badbit);
		const VeroScript::WebAssembly::FResult Result = State->Runtime.RunSourceWithInputStream(
			source, file_label ? file_label : "<webassembly>", Input, ScriptArgs, WebAssemblyBackend, SourceFiles);
		State->LastJson = VeroScript::WebAssembly::ResultToJson(Result, "run");
		return State->LastJson.c_str();
	}
	catch (const std::exception& Exception)
	{
		return StoreApiFailure(*State, Exception.what());
	}
	catch (...)
	{
		return StoreApiFailure(*State, "unknown WebAssembly runtime failure");
	}
	#endif
}

extern "C" const char* vero_web_check_source_with_context(
	const VeroWebRuntimeHandle runtime,
	const char* const source,
	const char* const file_label,
	const char* const* const file_paths,
	const char* const* const file_sources,
	const uint32_t file_count)
{
	auto* const State = static_cast<FWebRuntimeState*>(runtime);
	if (!State)
	{
		return InvalidRuntimeJson;
	}
	if (IsInteractiveExecutionActive())
	{
		return BusyRuntimeJson;
	}
	if (!source)
	{
		return StoreApiFailure(*State, "source must not be null");
	}
	try
	{
		const char* Error = nullptr;
		std::vector<VeroScript::WebAssembly::FSourceFile> SourceFiles;
		if (!BuildSourceFiles(file_paths, file_sources, file_count, SourceFiles, Error))
		{
			return StoreApiFailure(*State, Error);
		}
		const VeroScript::WebAssembly::FResult Result = State->Runtime.CheckSource(
			source,
			file_label ? file_label : "<webassembly>",
			SourceFiles);
		State->LastJson = VeroScript::WebAssembly::ResultToJson(Result, "check");
		return State->LastJson.c_str();
	}
	catch (const std::exception& Exception)
	{
		return StoreApiFailure(*State, Exception.what());
	}
	catch (...)
	{
		return StoreApiFailure(*State, "unknown WebAssembly runtime failure");
	}
}

#include "VeroScriptWebAssemblyMemory.inl"
