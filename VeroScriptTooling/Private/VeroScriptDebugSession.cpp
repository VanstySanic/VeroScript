// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptDebugSession.h"
#include "Internal/VeroScriptToolingCompile.h"

#include "VeroScriptCompile.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptDiagnosticUtil.h"
#include "VeroScriptEnvironment.h"
#include "VeroScriptLocale.h"
#include "Internal/VeroScriptPreprocessorQuery.h"
#include "VeroScriptOverlayUtil.h"
#include "VeroScriptPath.h"
#include "VeroScriptVm.h"

#include <algorithm>
#include <fstream>
#include <filesystem>
#include <exception>
#include <iostream>
#include <optional>
#include <mutex>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace VeroScript::Tooling
{
	struct VeroScriptDebugSession::FState
	{
		std::shared_ptr<FVmExecutionDomain> VmDomain = std::make_shared<FVmExecutionDomain>();
		std::string VmBackendSelectionPath;
		std::weak_ptr<const IVmBackendFactory> VmBackendSelectionFactory;
		bool MatchesVmBackendPath(const std::string_view Path) const
		{
			const auto& Factory = VmDomain->GetFactory();
			if (!Factory) return false;
			const std::string Key = PathUtil::IdentityKey(std::string(Path));
			return (!VmBackendSelectionPath.empty() && VmBackendSelectionFactory.lock() == Factory && Key == VmBackendSelectionPath) || (!VmDomain->GetLibraryPath().empty() && Key == PathUtil::IdentityKey(VmDomain->GetLibraryPath()));
		}
		std::unique_ptr<VeroScript::FBytecodeModule> Module;
		std::vector<FGeneratedDeclarationDocument> GeneratedDocuments;
		Detail::FSourceColumnMaps SourceColumns;
		Detail::FVirtualSourceMap VirtualSources;
		VeroScript::VeroScriptDebugger Debugger;
		std::shared_ptr<FStopHandler> StopHandler;
		FOutputHandler OutputHandler;
		bool bRunning = false;
		EDebugRunMode RunMode = EDebugRunMode::Continue;
		EExecutionBackend ExecutionBackend = EExecutionBackend::Mixed;
		std::size_t MaxCallDepth = 0;
		FJitPolicy JitPolicy;
		FJitStats LastJitStats;
		FJitDiagnostics LastJitDiagnostics;
		const VeroScript::IHostBindingCatalog* Bindings = nullptr;
		std::shared_ptr<const FModuleEnvironment> ModuleEnvironment;
		std::unordered_map<std::string, VeroScript::FPreprocessLineMap> LineMapsByFile;
		std::vector<std::string> DefineSpecs;
		std::unordered_map<std::string, std::string> InitialDefines;
		std::vector<VeroScript::FPredefinedMacro> PredefinedMacros;
		std::vector<std::string> ScriptArgs;
		std::vector<std::pair<std::string, std::string>> Environment;
		std::string WorkingDirectory;
		std::string Input;
		std::shared_ptr<VeroScript::FExecutionControl> ExecutionControl =
		    std::make_shared<VeroScript::FExecutionControl>();
	};

	namespace
	{
		/**
		 * Captures a VM stream while forwarding observable chunks to a caller. stdout is
		 * line-buffered so print(value) remains one DAP output event; stderr is emitted
		 * immediately, matching its unbuffered console semantics.  Explicit flushes
		 * (std::console::flush) flush stdout's pending fragment immediately.
		 */
		class FStreamingCaptureBuffer final : public std::streambuf
		{
		public:
			~FStreamingCaptureBuffer() override
			{
				FlushPending();
			}

			FStreamingCaptureBuffer(
			    std::string& InCapture,
			    const VeroScriptDebugSession::FOutputHandler& InHandler,
			    const bool bInIsError)
			    : Capture(InCapture), Handler(InHandler), bIsError(bInIsError)
			{
			}

		protected:
			//~ Begin std::streambuf Interface
			int_type overflow(const int_type Character) override
			{
				if (traits_type::eq_int_type(Character, traits_type::eof()))
				{
					return traits_type::not_eof(Character);
				}
				const char Value = traits_type::to_char_type(Character);
				Append(&Value, 1);
				return Character;
			}

			std::streamsize xsputn(const char* const Text, const std::streamsize Count) override
			{
				if (Text && Count > 0)
				{
					Append(Text, static_cast<std::size_t>(Count));
				}
				return Count;
			}

			int sync() override
			{
				FlushPending();
				return 0;
			}
			//~ End std::streambuf Interface

		private:
			void Append(const char* const Text, const std::size_t Count)
			{
				Capture.append(Text, Count);
				if (bIsError)
				{
					Pending.append(Text, Count);
					FlushPending();
					return;
				}
				for (std::size_t Index = 0; Index < Count; ++Index)
				{
					Pending.push_back(Text[Index]);
					if (Text[Index] == '\n' || Pending.size() >= 4096)
					{
						FlushPending();
					}
				}
			}

			void FlushPending()
			{
				if (Pending.empty() || !Handler)
				{
					Pending.clear();
					return;
				}
				try
				{
					Handler(Pending, bIsError);
				}
				catch (...)
				{
					// A UI/event subscriber must not make the debugged process fail.
				}
				Pending.clear();
			}

			std::string& Capture;
			VeroScriptDebugSession::FOutputHandler Handler;
			bool bIsError = false;
			std::string Pending;
		};

		VeroScript::EDebugRunMode ToEngineRunMode(const EDebugRunMode Mode)
		{
			switch (Mode)
			{
			case EDebugRunMode::StepIn:
				return VeroScript::EDebugRunMode::StepIn;
			case EDebugRunMode::StepOver:
				return VeroScript::EDebugRunMode::StepOver;
			case EDebugRunMode::StepOut:
				return VeroScript::EDebugRunMode::StepOut;
			case EDebugRunMode::Continue:
			default:
				return VeroScript::EDebugRunMode::Continue;
			}
		}

		EDebugStopReason FromEngineStopReason(const VeroScript::EDebugStopReason Reason)
		{
			switch (Reason)
			{
			case VeroScript::EDebugStopReason::Pause:
				return EDebugStopReason::Pause;
			case VeroScript::EDebugStopReason::Step:
				return EDebugStopReason::Step;
			case VeroScript::EDebugStopReason::Entry:
				return EDebugStopReason::Entry;
			case VeroScript::EDebugStopReason::Breakpoint:
			default:
				return EDebugStopReason::Breakpoint;
			}
		}

		class FScopedProcessContext final
		{
		public:
			FScopedProcessContext(
			    const std::string& WorkingDirectory,
			    const std::vector<std::pair<std::string, std::string>>& EnvOverrides)
			{
				ContextLock = std::unique_lock<std::recursive_mutex>(ProcessContextMutex());
				std::error_code Error;
				OriginalDirectory = std::filesystem::current_path(Error);
				bRestoreDirectory = !Error;
				if (Error && !WorkingDirectory.empty())
				{
					ErrorId = "diag.debug_working_directory_failed";
					ErrorArgs = {WorkingDirectory, Error.message()};
					return;
				}
				if (!WorkingDirectory.empty())
				{
					try
					{
						std::filesystem::current_path(PathUtil::FromUtf8(WorkingDirectory), Error);
					}
					catch (const std::exception& Exception)
					{
						ErrorId = "diag.debug_working_directory_failed";
						ErrorArgs = {WorkingDirectory, Exception.what()};
						return;
					}
					if (Error)
					{
						ErrorId = "diag.debug_working_directory_failed";
						ErrorArgs = {WorkingDirectory, Error.message()};
						return;
					}
				}

				for (const auto& [Name, Value] : EnvOverrides)
				{
					if (Name.empty() || Name.find('=') != std::string::npos || Name.find('\0') != std::string::npos || Value.find('\0') != std::string::npos)
					{
						ErrorId = "diag.debug_invalid_environment_variable";
						return;
					}
					SavedEnvironment.emplace_back(Name, Environment::GetUtf8(Name));
					if (!Environment::SetUtf8(Name, Value))
					{
						ErrorId = "diag.debug_environment_variable_failed";
						ErrorArgs = {Name};
						return;
					}
				}
				bValid = true;
			}

			~FScopedProcessContext()
			{
				for (auto It = SavedEnvironment.rbegin(); It != SavedEnvironment.rend(); ++It)
				{
					if (It->second)
					{
						Environment::SetUtf8(It->first, *It->second);
					}
					else
					{
						Environment::UnsetUtf8(It->first);
					}
				}
				if (bRestoreDirectory)
				{
					std::error_code Ignored;
					std::filesystem::current_path(OriginalDirectory, Ignored);
				}
			}

			bool IsValid() const noexcept
			{
				return bValid;
			}
			const std::string& GetErrorId() const noexcept
			{
				return ErrorId;
			}
			const std::vector<std::string>& GetErrorArgs() const noexcept
			{
				return ErrorArgs;
			}

		private:
			static std::recursive_mutex& ProcessContextMutex()
			{
				static std::recursive_mutex Mutex;
				return Mutex;
			}

			std::filesystem::path OriginalDirectory;
			std::vector<std::pair<std::string, std::optional<std::string>>> SavedEnvironment;
			std::unique_lock<std::recursive_mutex> ContextLock;
			bool bRestoreDirectory = false;
			bool bValid = false;
			std::string ErrorId;
			std::vector<std::string> ErrorArgs;
		};

		class FScopedDebuggerConnection final
		{
		public:
			FScopedDebuggerConnection(
			    VeroScript::VeroScriptVm& InVm,
			    VeroScript::VeroScriptDebugger& InDebugger) noexcept
			    : Vm(InVm), Debugger(InDebugger)
			{
			}

			~FScopedDebuggerConnection()
			{
				// Execute normally detaches the debugger itself, but a throwing host
				// callback can bypass the VM's ordinary Finish path. Never leave the
				// reusable tooling debugger pointing at a destroyed stack-local VM.
				Vm.SetDebugger(nullptr);
				Debugger.DetachVm();
			}

			FScopedDebuggerConnection(const FScopedDebuggerConnection&) = delete;
			FScopedDebuggerConnection& operator=(const FScopedDebuggerConnection&) = delete;

		private:
			VeroScript::VeroScriptVm& Vm;
			VeroScript::VeroScriptDebugger& Debugger;
		};

		FDiagnostic MakeDiagnostic(
		    const VeroScript::FScriptError& Error,
		    const std::string_view FallbackFileLabel)
		{
			return ToToolingDiagnostic(Error, FallbackFileLabel, "debug session failed");
		}

		FDebugLoadResult FromCompile(
		    VeroScript::FCompileResult&& CompileResult,
		    std::unique_ptr<VeroScript::FBytecodeModule>& OutModule,
		    const std::string_view FallbackFileLabel,
		    const std::unordered_map<std::string, FPreprocessLineMap>& LineMaps)
		{
			FDebugLoadResult Result;
			Result.bSuccess = CompileResult.bSuccess;
			const auto Append = [&](const FScriptError& Error)
			{
				auto Diagnostic = MakeDiagnostic(Error, FallbackFileLabel);
				const auto Map = LineMaps.find(PathUtil::IdentityKey(Diagnostic.FileLabel));
				if (!Diagnostic.bPhysicalLocation && Map != LineMaps.end())
				{
					std::uint32_t Line = 0, Column = 0;
					if (Detail::TryRemapPreprocessedToPhysical(Map->second,
					        Diagnostic.Range.StartLine, Diagnostic.Range.StartColumn, Line, Column))
					{
						Diagnostic.Range.StartLine = Line;
						Diagnostic.Range.StartColumn = Column;
						Diagnostic.bPhysicalLocation = true;
					}
					if (Detail::TryRemapPreprocessedToPhysical(Map->second,
					        Diagnostic.Range.EndLine, Diagnostic.Range.EndColumn, Line, Column))
					{
						Diagnostic.Range.EndLine = Line;
						Diagnostic.Range.EndColumn = Column;
					}
				}
				Result.Diagnostics.push_back(std::move(Diagnostic));
			};
			for (const auto& Diagnostic : CompileResult.Diagnostics)
			{
				Append(Diagnostic);
			}

			if (!CompileResult.bSuccess)
			{
				if (Result.Diagnostics.empty())
				{
					Append(CompileResult.Error);
				}
				return Result;
			}
			OutModule = std::move(CompileResult.Module);
			return Result;
		}

		bool ExpandDebugExpression(
		    const std::string_view Expression,
		    const std::unordered_map<std::string, std::string>& Defines,
		    const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros,
		    std::string& OutExpression,
		    std::string& OutError)
		{
			VeroScript::FPreprocessContext Context;
			Context.LogicalPath = "<debug-expression>";
			return VeroScript::PreprocessSource(
			    Expression,
			    Defines,
			    PredefinedMacros,
			    Context,
			    OutExpression,
			    OutError);
		}

		std::uint32_t MapBreakpointLine(
		    const std::unordered_map<std::string, VeroScript::FPreprocessLineMap>& LineMapsByFile,
		    const std::string_view FileLabel,
		    const std::uint32_t PhysicalLine)
		{
			const std::string Key = VeroScript::PathUtil::IdentityKey(std::string(FileLabel));
			const auto Found = LineMapsByFile.find(Key);
			if (Found == LineMapsByFile.end())
			{
				return PhysicalLine;
			}
			std::uint32_t PreprocessedLine = 0;
			if (Detail::MapPhysicalLineToPreprocessedLine(
			        Found->second,
			        PhysicalLine,
			        PreprocessedLine))
			{
				return PreprocessedLine;
			}
			return PhysicalLine;
		}

		void MapStackPosition(
		    const std::unordered_map<std::string, VeroScript::FPreprocessLineMap>& LineMapsByFile,
		    const std::string_view FileLabel,
		    std::uint32_t& InOutLine,
		    std::uint32_t& InOutColumn)
		{
			const std::string Key = VeroScript::PathUtil::IdentityKey(std::string(FileLabel));
			const auto Found = LineMapsByFile.find(Key);
			if (Found == LineMapsByFile.end())
			{
				return;
			}
			std::uint32_t PhysicalLine = 0;
			std::uint32_t PhysicalColumn = 0;
			if (Detail::TryRemapPreprocessedToPhysical(
			        Found->second,
			        InOutLine,
			        InOutColumn,
			        PhysicalLine,
			        PhysicalColumn))
			{
				InOutLine = PhysicalLine;
				InOutColumn = PhysicalColumn;
			}
		}

		std::vector<FDebugVariable> MapDebugVariables(
		    const std::vector<VeroScript::FDebugVariable>& EngineVariables)
		{
			std::vector<FDebugVariable> Variables;
			Variables.reserve(EngineVariables.size());
			for (const VeroScript::FDebugVariable& Engine : EngineVariables)
			{
				FDebugVariable Variable;
				Variable.Name = Engine.Name;
				Variable.Value = Engine.Value;
				Variable.RawValue = Engine.RawValue;
				Variable.bHasRawValue = Engine.bHasRawValue;
				Variable.bReadOnly = Engine.bReadOnly;
				Variable.Memory = Engine.Memory;
				Variable.EngineVariable =
				    std::make_shared<VeroScript::FDebugVariable>(Engine);
				Variables.push_back(std::move(Variable));
			}
			return Variables;
		}
	}

	VeroScriptDebugSession::VeroScriptDebugSession()
	    : State(std::make_unique<FState>())
	{
		Locale::InitializeModuleFromEnvironment("VeroScriptTooling");
		State->Debugger.SetStopCallback(
		    [this](VeroScript::VeroScriptDebugger&, const VeroScript::EDebugStopReason Reason)
		{
			// Retain the active callable when it replaces or clears itself.
			// Share its identity so mutable captures persist between stops.
			const auto Handler = State->StopHandler;
			if (Handler)
			{
				(*Handler)(*this, FromEngineStopReason(Reason));
			}
		});
	}

	bool VeroScriptDebugSession::SetVmBackendFactory(std::shared_ptr<const IVmBackendFactory> Factory, std::string& Error)
	{
		if (State->bRunning)
		{
			Error = "debug session is running";
			return false;
		}
		std::ostringstream Output;
		VeroScriptVm Candidate(Output);
		if (!Candidate.SetVmExecutionDomain(State->VmDomain, Error) || !Candidate.SetVmBackendFactory(std::move(Factory), Error))
		{
			return false;
		}
		State->VmBackendSelectionPath.clear();
		State->VmBackendSelectionFactory.reset();
		return true;
	}

	bool VeroScriptDebugSession::SetVmBackendPath(std::string_view Path, std::string& Error)
	{
		if (State->bRunning)
		{
			Error = "debug session is running";
			return false;
		}
		// Path may borrow Error; SetVmExecutionDomain clears that buffer.
		const std::string SelectionPath(Path);
		const std::string SelectionKey = SelectionPath.empty() ? std::string{} : PathUtil::IdentityKey(SelectionPath);
		std::ostringstream Output;
		VeroScriptVm Candidate(Output);
		if (!Candidate.SetVmExecutionDomain(State->VmDomain, Error) || !Candidate.SetVmBackendPath(SelectionPath, Error))
		{
			return false;
		}
		State->VmBackendSelectionPath = SelectionKey;
		State->VmBackendSelectionFactory = State->VmDomain->GetFactory();
		return true;
	}

	std::shared_ptr<FVmExecutionDomain> VeroScriptDebugSession::GetVmExecutionDomain() const
	{
		return State->VmDomain;
	}

	VeroScriptDebugSession::~VeroScriptDebugSession() = default;

	FDebugLoadResult VeroScriptDebugSession::LoadSource(
	    const std::string_view Source,
	    const std::string_view FileLabel,
	    const FDebugLoadOptions& Options)
	try
	{
		if (State->bRunning)
		{
			FDebugLoadResult Result;
			FScriptError Error;
			FillDiagnostic(Error, "dap.already_running");
			Result.Diagnostics.push_back(MakeDiagnostic(Error, FileLabel));
			return Result;
		}
		if ((Options.VmBackendFactory && !Options.VmBackendPath.empty()))
		{
			Clear();
			return ToolingModuleEnvironmentFailure<FDebugLoadResult>("VM factory and path are exclusive");
		}
		if ((Options.VmBackendFactory && Options.VmBackendFactory != State->VmDomain->GetFactory()) || (!Options.VmBackendPath.empty() && !State->MatchesVmBackendPath(Options.VmBackendPath)))
		{
			std::string Failure;
			if (!(Options.VmBackendFactory ? SetVmBackendFactory(Options.VmBackendFactory, Failure)
			                               : SetVmBackendPath(Options.VmBackendPath, Failure)))
			{
				Clear();
				return ToolingModuleEnvironmentFailure<FDebugLoadResult>(Failure);
			}
		}
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				Clear();
				return ToolingModuleEnvironmentFailure<FDebugLoadResult>(Failure);
			}
			return LoadSource(Source, FileLabel, Effective);
		}
		State->LineMapsByFile.clear();
		VeroScript::FCompileOptions CompileOptions;
		CompileOptions.bBuildPreprocessLineMaps = true;
		CompileOptions.ImportPaths = Options.ImportPaths;
		CompileOptions.StdlibSearchPaths = Options.StdlibSearchPaths;
		CompileOptions.ImplicitImportPaths = Options.ImplicitImportPaths;
		CompileOptions.bAllowCircularImports = Options.bAllowCircularImports;
		CompileOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
		CompileOptions.Defines = Options.Defines;
		CompileOptions.PredefinedMacros = Options.PredefinedMacros;
		CompileOptions.SourceOverlays = ToEngineOverlays(Options.SourceOverlays);
		CompileOptions.Bindings = Options.Bindings;
		CompileOptions.AnnotationProcessing = Options.AnnotationProcessing;
		CompileOptions.CompileProcessing = Options.CompileProcessing;
		CompileOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
		if (!CompileOptions.ScriptCompileExtensions.VmBackendFactory && CompileOptions.ScriptCompileExtensions.VmBackendPath.empty())
		{
			CompileOptions.ScriptCompileExtensions.VmBackendFactory = State->VmDomain->GetFactory();
		}
		CompileOptions.ModuleEnvironment = Options.ModuleEnvironment;
		State->Bindings = Options.Bindings;
		State->ModuleEnvironment = Options.ModuleEnvironment;
		State->DefineSpecs.clear();
		for (const std::string_view Define : Options.Defines)
		{
			State->DefineSpecs.emplace_back(Define);
		}
		State->InitialDefines.clear();
		std::string DefineError;
		VeroScript::BuildDefineMap(Options.Defines, State->InitialDefines, DefineError);
		State->PredefinedMacros = Options.PredefinedMacros;
		auto CompileResult =
		    Detail::CompileToolingSource(Source, FileLabel, CompileOptions, &State->SourceColumns, &State->VirtualSources);
		State->GeneratedDocuments = CompileResult.PreparedProgram
		    ? CompileResult.PreparedProgram->GetGeneratedDocuments()
		    : std::vector<FGeneratedDeclarationDocument>{};
		for (const auto& Document : State->GeneratedDocuments)
		{
			State->SourceColumns.insert_or_assign(
			    PathUtil::IdentityKey(Document.FileLabel), Detail::FSourceColumns::Capture(Document.Source));
		}

		State->LineMapsByFile = std::move(CompileResult.PreprocessLineMaps);
		auto Result = FromCompile(std::move(CompileResult), State->Module, FileLabel, State->LineMapsByFile);
		if (!Result.bSuccess)
		{
			Clear();
		}

		return Result;
	}
	catch (...)
	{
		Clear();
		throw;
	}

	FDebugLoadResult VeroScriptDebugSession::LoadFile(
	    const std::string_view FilePath,
	    const FDebugLoadOptions& Options)
	try
	{
		if (State->bRunning)
		{
			FDebugLoadResult Result;
			FScriptError Error;
			FillDiagnostic(Error, "dap.already_running");
			Result.Diagnostics.push_back(MakeDiagnostic(Error, FilePath));
			return Result;
		}
		if ((Options.VmBackendFactory && !Options.VmBackendPath.empty()))
		{
			Clear();
			return ToolingModuleEnvironmentFailure<FDebugLoadResult>("VM factory and path are exclusive");
		}
		if ((Options.VmBackendFactory && Options.VmBackendFactory != State->VmDomain->GetFactory()) || (!Options.VmBackendPath.empty() && !State->MatchesVmBackendPath(Options.VmBackendPath)))
		{
			std::string Failure;
			if (!(Options.VmBackendFactory ? SetVmBackendFactory(Options.VmBackendFactory, Failure)
			                               : SetVmBackendPath(Options.VmBackendPath, Failure)))
			{
				Clear();
				return ToolingModuleEnvironmentFailure<FDebugLoadResult>(Failure);
			}
		}
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				Clear();
				return ToolingModuleEnvironmentFailure<FDebugLoadResult>(Failure);
			}
			return LoadFile(FilePath, Effective);
		}
		State->LineMapsByFile.clear();
		VeroScript::FCompileOptions CompileOptions;
		CompileOptions.bBuildPreprocessLineMaps = true;
		CompileOptions.ImportPaths = Options.ImportPaths;
		CompileOptions.StdlibSearchPaths = Options.StdlibSearchPaths;
		CompileOptions.ImplicitImportPaths = Options.ImplicitImportPaths;
		CompileOptions.bAllowCircularImports = Options.bAllowCircularImports;
		CompileOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
		CompileOptions.Defines = Options.Defines;
		CompileOptions.PredefinedMacros = Options.PredefinedMacros;
		CompileOptions.SourceOverlays = ToEngineOverlays(Options.SourceOverlays);
		CompileOptions.Bindings = Options.Bindings;
		CompileOptions.AnnotationProcessing = Options.AnnotationProcessing;
		CompileOptions.CompileProcessing = Options.CompileProcessing;
		CompileOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
		if (!CompileOptions.ScriptCompileExtensions.VmBackendFactory && CompileOptions.ScriptCompileExtensions.VmBackendPath.empty())
		{
			CompileOptions.ScriptCompileExtensions.VmBackendFactory = State->VmDomain->GetFactory();
		}
		CompileOptions.ModuleEnvironment = Options.ModuleEnvironment;
		State->Bindings = Options.Bindings;
		State->ModuleEnvironment = Options.ModuleEnvironment;
		State->DefineSpecs.clear();
		for (const std::string_view Define : Options.Defines)
		{
			State->DefineSpecs.emplace_back(Define);
		}
		State->InitialDefines.clear();
		std::string DefineError;
		VeroScript::BuildDefineMap(Options.Defines, State->InitialDefines, DefineError);
		State->PredefinedMacros = Options.PredefinedMacros;
		auto CompileResult = Detail::CompileToolingFile(FilePath, CompileOptions, &State->SourceColumns, &State->VirtualSources);
		State->GeneratedDocuments = CompileResult.PreparedProgram
		    ? CompileResult.PreparedProgram->GetGeneratedDocuments()
		    : std::vector<FGeneratedDeclarationDocument>{};
		for (const auto& Document : State->GeneratedDocuments)
		{
			State->SourceColumns.insert_or_assign(
			    PathUtil::IdentityKey(Document.FileLabel), Detail::FSourceColumns::Capture(Document.Source));
		}

		State->LineMapsByFile = std::move(CompileResult.PreprocessLineMaps);
		auto Result = FromCompile(std::move(CompileResult), State->Module, FilePath, State->LineMapsByFile);
		if (!Result.bSuccess)
		{
			Clear();
		}

		return Result;
	}
	catch (...)
	{
		Clear();
		throw;
	}

	void VeroScriptDebugSession::Clear()
	{
		if (State->bRunning) return;
		State->Module.reset();
		State->GeneratedDocuments.clear();
		State->SourceColumns.clear();
		State->VirtualSources.clear();
		State->LineMapsByFile.clear();
		State->Bindings = nullptr;
		State->ModuleEnvironment.reset();
		State->DefineSpecs.clear();
		State->InitialDefines.clear();
		State->PredefinedMacros.clear();
		State->LastJitStats = {};
		State->LastJitDiagnostics = {};
		State->Debugger.ClearBreakpoints();
		State->Debugger.DetachVm();
	}

	bool VeroScriptDebugSession::IsLoaded() const
	{
		return static_cast<bool>(State->Module);
	}

	bool VeroScriptDebugSession::TryGetGeneratedSource(std::string_view FileLabel, std::string& OutSource) const
	{
		OutSource.clear();
		const auto Document = std::find_if(State->GeneratedDocuments.begin(), State->GeneratedDocuments.end(),
		    [&](const auto& Doc)
		{
			return Doc.FileLabel == FileLabel;
		});
		if (Document == State->GeneratedDocuments.end()) return false;
		OutSource = Document->Source;
		return true;
	}

	bool VeroScriptDebugSession::TryGetSource(std::string_view FileLabel, std::string& OutSource) const
	{
		if (const auto Source = State->VirtualSources.find(PathUtil::IdentityKey(std::string(FileLabel))); Source != State->VirtualSources.end())
		{
			OutSource = Source->second;
			return true;
		}
		if (State->ModuleEnvironment &&
		    State->SourceColumns.count(PathUtil::IdentityKey(std::string(FileLabel))))
		{
			if (const auto* Module = State->ModuleEnvironment->FindSource(std::string(FileLabel));
			    Module && Module->Source)
			{
				OutSource = *Module->Source;
				return true;
			}
		}

		return TryGetGeneratedSource(FileLabel, OutSource);
	}

	std::uint32_t VeroScriptDebugSession::GetUtf16Column(std::string_view FileLabel,
	    std::uint32_t Line, std::uint32_t ByteColumn) const
	{
		const auto Found = State->SourceColumns.find(PathUtil::IdentityKey(std::string(FileLabel)));
		return Found == State->SourceColumns.end() ? ByteColumn : Found->second.Utf16Column(Line, ByteColumn);
	}

	int VeroScriptDebugSession::SetBreakpoint(
	    std::string FileLabel,
	    const std::uint32_t Line,
	    std::string Condition)
	{
		const std::uint32_t MappedLine =
		    MapBreakpointLine(State->LineMapsByFile, FileLabel, Line);
		std::string ExpandedCondition;
		std::string PreprocessError;
		if (!Condition.empty() && ExpandDebugExpression(Condition, State->InitialDefines, State->PredefinedMacros, ExpandedCondition, PreprocessError))
		{
			Condition = std::move(ExpandedCondition);
		}
		return State->Debugger.SetBreakpoint(
		    std::move(FileLabel),
		    MappedLine,
		    std::move(Condition));
	}

	VeroScriptDebugSession::FBreakpointBinding VeroScriptDebugSession::BindBreakpoint(
	    const std::string_view SourcePath,
	    const std::uint32_t PhysicalLine,
	    std::string Condition)
	{
		FBreakpointBinding Result;
		Result.Line = PhysicalLine;
		if (!State->Module)
		{
			Result.Message = Locale::TranslateId("dap.breakpoint.debuggee_not_loaded");
			return Result;
		}
		if (SourcePath.empty() || PhysicalLine == 0)
		{
			Result.Message = Locale::TranslateId("dap.breakpoint.invalid_location");
			return Result;
		}

		const VeroScript::FBytecodeModule& Module = *State->Module;
		const auto DebugFile = [&Module](
		                           const VeroScript::FDebugLine& Line,
		                           const std::string_view Fallback) -> std::string_view
		{
			return Line.FileIndex < Module.DebugFiles.size() && !Module.DebugFiles[Line.FileIndex].empty()
			    ? std::string_view(Module.DebugFiles[Line.FileIndex])
			    : Fallback;
		};
		// Only opcode offsets with a recorded location are candidates. Merely
		// registering a line with VeroScriptDebugger does not make it executable.
		std::unordered_set<std::string> CompiledFiles;
		const auto CollectFiles = [&](const std::vector<VeroScript::FDebugLine>& Lines,
		                              const std::size_t CodeSize, const std::string_view Fallback)
		{
			for (std::size_t Ip = 0; Ip < Lines.size() && Ip < CodeSize; ++Ip)
			{
				if (Lines[Ip].Line != 0)
				{
					const std::string_view File = DebugFile(Lines[Ip], Fallback);
					if (!File.empty())
					{
						CompiledFiles.emplace(File);
					}
				}
			}
		};
		CollectFiles(Module.InitDebugLines, Module.InitCode.size(), {});
		for (const VeroScript::FBytecodeFunction& Function : Module.Functions)
		{
			CollectFiles(Function.DebugLines, Function.Code.size(),
			    Function.Location.FileLabel);
		}

		const std::string SourceKey = PathUtil::IdentityKey(std::string(SourcePath));
		std::string SelectedFile;
		std::size_t Matches = 0;
		for (const std::string& File : CompiledFiles)
		{
			if (PathUtil::IdentityKey(File) == SourceKey)
			{
				SelectedFile = File;
				++Matches;
			}
		}
		if (Matches != 1)
		{
			Result.Message = Locale::TranslateId(Matches == 0
			        ? "dap.breakpoint.no_compiled_source"
			        : "dap.breakpoint.ambiguous_source");
			return Result;
		}

		const std::string SelectedKey = PathUtil::IdentityKey(SelectedFile);
		const VeroScript::FPreprocessLineMap* LineMap = nullptr;
		if (const auto Found = State->LineMapsByFile.find(
		        SelectedKey);
		    Found != State->LineMapsByFile.end())
		{
			LineMap = &Found->second;
		}
		else if (const auto SourceMap = State->LineMapsByFile.find(SourceKey);
		    SourceMap != State->LineMapsByFile.end())
		{
			LineMap = &SourceMap->second;
		}
		if (!LineMap)
		{
			Result.Message = Locale::TranslateId("dap.breakpoint.mapping_unavailable");
			return Result;
		}
		if (PhysicalLine >= LineMap->PhysicalLineActiveCode.size())
		{
			Result.Message = Locale::TranslateId("dap.breakpoint.outside_source");
			return Result;
		}
		if (!LineMap->PhysicalLineActiveCode[PhysicalLine])
		{
			Result.Message = Locale::TranslateId("dap.breakpoint.inactive_source");
			return Result;
		}

		const auto ToPhysical = [LineMap](
		                            const VeroScript::FDebugLine& Line) -> std::uint32_t
		{
			if (!LineMap)
			{
				return Line.Line;
			}
			std::uint32_t Physical = 0;
			std::uint32_t Column = 0;
			return Detail::TryRemapPreprocessedToPhysical(
			           *LineMap, Line.Line, Line.Column, Physical, Column)
			    ? Physical
			    : 0;
		};
		struct FCandidate
		{
			std::uint32_t Physical = 0;
			std::uint32_t Preprocessed = 0;
		};
		std::optional<FCandidate> Best;
		const auto Consider = [&Best, PhysicalLine](
		                          const VeroScript::FDebugLine& Line, const std::uint32_t Physical)
		{
			if (Physical < PhysicalLine)
			{
				return;
			}
			if (!Best || Physical < Best->Physical || (Physical == Best->Physical && Line.Line < Best->Preprocessed))
			{
				Best = FCandidate{Physical, Line.Line};
			}
		};
		for (const VeroScript::FBytecodeFunction& Function : Module.Functions)
		{
			std::uint32_t First = 0;
			std::uint32_t Last = 0;
			for (std::size_t Ip = 0;
			    Ip < Function.DebugLines.size() && Ip < Function.Code.size(); ++Ip)
			{
				const VeroScript::FDebugLine& Line = Function.DebugLines[Ip];
				if (Line.Line == 0 || DebugFile(Line, Function.Location.FileLabel) != SelectedFile)
				{
					continue;
				}
				const std::uint32_t Physical = ToPhysical(Line);
				if (Physical == 0)
				{
					continue;
				}
				First = First == 0 ? Physical : std::min(First, Physical);
				Last = std::max(Last, Physical);
			}
			if (Last == 0)
			{
				continue;
			}
			std::uint32_t Start = First;
			if (Function.Location.FileLabel == SelectedFile && Function.Location.Line != 0)
			{
				const VeroScript::FDebugLine Declaration{
				    Function.Location.Line, Function.Location.Column, 0};
				const std::uint32_t Mapped = ToPhysical(Declaration);
				if (Mapped != 0)
				{
					Start = std::min(Start, Mapped);
				}
			}
			if (PhysicalLine < Start || PhysicalLine > Last)
			{
				continue;
			}
			for (std::size_t Ip = 0;
			    Ip < Function.DebugLines.size() && Ip < Function.Code.size(); ++Ip)
			{
				const VeroScript::FDebugLine& Line = Function.DebugLines[Ip];
				if (Line.Line != 0 && DebugFile(Line, Function.Location.FileLabel) == SelectedFile)
				{
					Consider(Line, ToPhysical(Line));
				}
			}
		}
		// Module-initializer instructions have no enclosing function range.
		// Only exact physical lines are safe to bind there.
		for (std::size_t Ip = 0;
		    Ip < Module.InitDebugLines.size() && Ip < Module.InitCode.size(); ++Ip)
		{
			const VeroScript::FDebugLine& Line = Module.InitDebugLines[Ip];
			if (Line.Line != 0 && DebugFile(Line, {}) == SelectedFile && ToPhysical(Line) == PhysicalLine)
			{
				Consider(Line, PhysicalLine);
			}
		}
		if (!Best)
		{
			Result.Message = Locale::TranslateId("dap.breakpoint.no_executable_instruction");
			return Result;
		}
		std::string ExpandedCondition;
		std::string PreprocessError;
		if (!Condition.empty() && ExpandDebugExpression(Condition, State->InitialDefines, State->PredefinedMacros, ExpandedCondition, PreprocessError))
		{
			Condition = std::move(ExpandedCondition);
		}
		Result.Id = State->Debugger.SetExactBreakpoint(
		    std::move(SelectedFile), Best->Preprocessed, std::move(Condition));
		Result.Line = Best->Physical;
		return Result;
	}

	void VeroScriptDebugSession::ClearBreakpoints()
	{
		State->Debugger.ClearBreakpoints();
	}

	void VeroScriptDebugSession::SetStopHandler(FStopHandler Handler)
	{
		State->StopHandler = Handler ? std::make_shared<FStopHandler>(std::move(Handler)) : nullptr;
	}

	void VeroScriptDebugSession::SetOutputHandler(FOutputHandler Handler)
	{
		State->OutputHandler = std::move(Handler);
	}

	void VeroScriptDebugSession::SetRunMode(const EDebugRunMode Mode)
	{
		State->RunMode = Mode;
		State->Debugger.SetRunMode(ToEngineRunMode(Mode));
	}

	EDebugRunMode VeroScriptDebugSession::GetRunMode() const
	{
		return State->RunMode;
	}

	void VeroScriptDebugSession::SetExecutionBackend(const EExecutionBackend Backend)
	{
		State->ExecutionBackend = Backend;
	}

	EExecutionBackend VeroScriptDebugSession::GetExecutionBackend() const
	{
		return State->ExecutionBackend;
	}

	void VeroScriptDebugSession::SetMaxCallDepth(const std::size_t Limit)
	{
		State->MaxCallDepth = Limit;
	}

	std::size_t VeroScriptDebugSession::GetMaxCallDepth() const
	{
		return State->MaxCallDepth;
	}

	void VeroScriptDebugSession::SetJitPolicy(const FJitPolicy Policy)
	{
		State->JitPolicy = Policy;
	}

	const FJitPolicy& VeroScriptDebugSession::GetJitPolicy() const
	{
		return State->JitPolicy;
	}

	const FJitStats& VeroScriptDebugSession::GetLastJitStats() const
	{
		return State->LastJitStats;
	}

	const FJitDiagnostics& VeroScriptDebugSession::GetLastJitDiagnostics() const
	{
		return State->LastJitDiagnostics;
	}

	void VeroScriptDebugSession::SetScriptArguments(std::vector<std::string> Arguments)
	{
		State->ScriptArgs = std::move(Arguments);
	}

	void VeroScriptDebugSession::SetEnvironment(
	    std::vector<std::pair<std::string, std::string>> Environment)
	{
		State->Environment = std::move(Environment);
	}

	void VeroScriptDebugSession::SetWorkingDirectory(std::string Directory)
	{
		State->WorkingDirectory = std::move(Directory);
	}

	void VeroScriptDebugSession::SetInputFromString(std::string Input)
	{
		State->Input = std::move(Input);
	}

	void VeroScriptDebugSession::ResetRunControl() noexcept
	{
		State->ExecutionControl->Reset();
		State->Debugger.ResetPauseRequest();
	}

	void VeroScriptDebugSession::RequestPause() noexcept
	{
		State->Debugger.RequestPause();
	}

	void VeroScriptDebugSession::RequestCancel() noexcept
	{
		State->ExecutionControl->RequestCancel();
	}

	std::vector<FDebugStackFrame> VeroScriptDebugSession::GetStackTrace() const
	{
		std::vector<FDebugStackFrame> Frames;
		for (const VeroScript::FDebugStackFrame& Frame : State->Debugger.GetStackTrace())
		{
			FDebugStackFrame Mapped;
			Mapped.FunctionName = Frame.FunctionName;
			Mapped.FileLabel = Frame.FileLabel;
			Mapped.Line = Frame.Line;
			Mapped.Column = Frame.Column;
			MapStackPosition(
			    State->LineMapsByFile,
			    Frame.FileLabel,
			    Mapped.Line,
			    Mapped.Column);
			Frames.push_back(std::move(Mapped));
		}
		return Frames;
	}

	std::vector<FDebugVariable> VeroScriptDebugSession::GetLocals(const std::size_t FrameIndex) const
	{
		return MapDebugVariables(State->Debugger.GetLocals(FrameIndex));
	}

	std::vector<FDebugVariable> VeroScriptDebugSession::GetChildren(
	    const FDebugVariable& Variable) const
	{
		if (Variable.EngineVariable)
		{
			return MapDebugVariables(State->Debugger.GetChildren(*Variable.EngineVariable));
		}
		VeroScript::FDebugVariable Fallback;
		Fallback.Name = Variable.Name;
		Fallback.Value = Variable.Value;
		Fallback.RawValue = Variable.RawValue;
		Fallback.bHasRawValue = Variable.bHasRawValue;
		Fallback.bReadOnly = Variable.bReadOnly;
		return MapDebugVariables(State->Debugger.GetChildren(Fallback));
	}

	FDebugEvaluateResult VeroScriptDebugSession::Evaluate(
	    const std::string_view Expression,
	    const std::size_t FrameIndex) const
	{
		FDebugEvaluateResult Result;
		std::string Expanded;
		if (!ExpandDebugExpression(
		        Expression,
		        State->InitialDefines,
		        State->PredefinedMacros,
		        Expanded,
		        Result.Error))
		{
			return Result;
		}
		const VeroScript::FDebugEvaluateResult Engine =
		    State->Debugger.Evaluate(Expanded, FrameIndex);
		Result.bOk = Engine.bOk;
		Result.Value = Engine.Value;
		Result.Error = Engine.Error;
		Result.RawValue = Engine.RawValue;
		Result.bHasRawValue = Engine.bHasRawValue;
		return Result;
	}

	FDebugEvaluateResult VeroScriptDebugSession::SetLocal(
	    const std::string_view Name,
	    const std::string_view ValueExpression,
	    const std::size_t FrameIndex)
	{
		FDebugEvaluateResult Result;
		std::string Expanded;
		if (!ExpandDebugExpression(
		        ValueExpression,
		        State->InitialDefines,
		        State->PredefinedMacros,
		        Expanded,
		        Result.Error))
		{
			return Result;
		}
		const VeroScript::FDebugEvaluateResult Engine =
		    State->Debugger.SetLocal(Name, Expanded, FrameIndex);
		Result.bOk = Engine.bOk;
		Result.Value = Engine.Value;
		Result.Error = Engine.Error;
		Result.RawValue = Engine.RawValue;
		Result.bHasRawValue = Engine.bHasRawValue;
		return Result;
	}

	FDebugEvaluateResult VeroScriptDebugSession::SetChild(
	    const FDebugVariable& Variable,
	    const std::string_view ValueExpression,
	    const std::size_t FrameIndex)
	{
		FDebugEvaluateResult Result;
		if (!Variable.EngineVariable)
		{
			Result.Error = "variable is not an editable child";
			return Result;
		}
		std::string Expanded;
		if (!ExpandDebugExpression(
		        ValueExpression,
		        State->InitialDefines,
		        State->PredefinedMacros,
		        Expanded,
		        Result.Error))
		{
			return Result;
		}
		const VeroScript::FDebugEvaluateResult Engine =
		    State->Debugger.SetChild(*Variable.EngineVariable, Expanded, FrameIndex);
		Result.bOk = Engine.bOk;
		Result.Value = Engine.Value;
		Result.Error = Engine.Error;
		Result.RawValue = Engine.RawValue;
		Result.bHasRawValue = Engine.bHasRawValue;
		return Result;
	}

	FDebugRunResult VeroScriptDebugSession::Run()
	{
		FDebugRunResult Result;
		if (State->bRunning)
		{
			FScriptError Error;
			FillDiagnostic(Error, "dap.already_running");
			Result.Diagnostics.push_back(MakeDiagnostic(Error, "<debug>"));
			return Result;
		}
		if (!State->Module)
		{
			FScriptError Error;
			FillDiagnostic(Error, "diag.no_module_loaded");
			Result.Diagnostics.push_back(MakeDiagnostic(Error, "<debug>"));
			return Result;
		}

		struct FRunningScope
		{
			bool& bRunning;
			explicit FRunningScope(bool& InRunning) : bRunning(InRunning)
			{
				bRunning = true;
			}
			~FRunningScope()
			{
				bRunning = false;
			}
		} RunningScope(State->bRunning);
		// Execute consumes these views after module initialization, whose stop
		// callbacks may change the arguments configured for the next Run.
		const auto ScriptArgs = State->ScriptArgs;

		FScopedProcessContext ProcessContext(
		    State->WorkingDirectory,
		    State->Environment);
		if (!ProcessContext.IsValid())
		{
			FScriptError Error;
			Error.MessageId = ProcessContext.GetErrorId();
			Error.MessageArgs = ProcessContext.GetErrorArgs();
			std::vector<std::string_view> ArgViews;
			ArgViews.reserve(Error.MessageArgs.size());
			for (const std::string& Arg : Error.MessageArgs)
			{
				ArgViews.push_back(Arg);
			}
			Error.Message = Locale::FormatEnglish(Error.MessageId, ArgViews);
			Result.Diagnostics.push_back(MakeDiagnostic(Error, "<debug>"));
			return Result;
		}

		FStreamingCaptureBuffer OutputBuffer(Result.Output, State->OutputHandler, false);
		FStreamingCaptureBuffer ErrorBuffer(Result.ErrorOutput, State->OutputHandler, true);
		std::ostream Output(&OutputBuffer);
		std::ostream Errors(&ErrorBuffer);
		std::istringstream Input(State->Input);
		VeroScript::VeroScriptVm Vm(Output, Input, Errors);
		std::string EnvironmentError;
		if (!Vm.SetVmExecutionDomain(State->VmDomain, EnvironmentError))
		{
			FScriptError Error;
			Error.Message = EnvironmentError;
			Result.Diagnostics.push_back(MakeDiagnostic(Error, "<debug>"));
			return Result;
		}
		if (!Vm.SetModuleEnvironment(State->ModuleEnvironment, EnvironmentError))
		{
			FScriptError Error;
			Error.Message = EnvironmentError;
			Result.Diagnostics.push_back(MakeDiagnostic(Error, "<debug>"));
			return Result;
		}
		// Apply requested backend + policy first; SetDebugger then forces effective backend
		// to Vm and invalidates any JIT cache (same contract as CLI --debug / embed).
		Vm.SetMaxCallDepth(State->MaxCallDepth);
		Vm.SetJitPolicy(State->JitPolicy);
		Vm.SetExecutionBackend(State->ExecutionBackend);
		Vm.SetMemoryHostBindings(State->Bindings);
		Vm.SetExecutionControl(State->ExecutionControl);
		State->Debugger.SetRunMode(ToEngineRunMode(State->RunMode));
		Vm.SetDebugger(&State->Debugger);
		FScopedDebuggerConnection DebuggerConnection(Vm, State->Debugger);
		std::vector<std::string_view> ArgumentViews;
		ArgumentViews.reserve(ScriptArgs.size());
		for (const std::string& Argument : ScriptArgs)
		{
			ArgumentViews.emplace_back(Argument);
		}
		const bool bOk = Vm.Execute(
		    *State->Module,
		    VeroScript::ERunMode::Script,
		    ArgumentViews,
		    State->Bindings ? &State->Bindings->GetNativeFunctions() : nullptr);
		State->LastJitStats = Vm.GetJitStats();
		State->LastJitDiagnostics = {};
		if (Vm.GetJitPolicy().Diagnostics.MaxFunctions)
		{
			// Stop callbacks may already have changed the next Run's policy.
			// Retain this VM's optional snapshot without changing execution results
			// if diagnostic materialization fails.
			try
			{
				State->LastJitDiagnostics = Vm.GetJitDiagnostics();
			}
			catch (...)
			{
			}
		}

		Output.flush();
		Errors.flush();
		Result.bSuccess = bOk;
		Result.ExitCode = bOk ? Vm.GetProcessExitCode() : 1;
		if (!bOk)
		{
			Result.Diagnostics.push_back(MakeDiagnostic(Vm.GetError(), "<debug>"));
		}
		return Result;
	}
}
