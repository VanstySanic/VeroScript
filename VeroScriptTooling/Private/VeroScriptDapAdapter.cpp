// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptDapAdapter.h"

#include "VeroScriptDiagnosticUtil.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptLocale.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptPath.h"
#include "VeroScriptVersion.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace VeroScript::Tooling
{
	struct VeroScriptDapAdapter::FState
	{
		struct FVariablePage
		{
			std::vector<FDebugVariable> Variables;
			std::unordered_set<const void*> Ancestors;
			std::size_t FrameIndex = 0;
		};
		struct FStoredBreakpoint
		{
			int Id = 0; // Stable DAP identity, independent of debugger registration IDs.
			std::string Path;
			std::string PathKey;
			std::uint32_t Line = 1;
			std::string Condition;
		};

		// Pause/terminate can race the serialized lifecycle owner. Borrow one
		// connection until its host call returns; disconnect drains these borrows
		// before the host may release it or another connection can be attached.
		struct FDomainControlScope
		{
			explicit FDomainControlScope(FState& InState) : State(InState)
			{
				std::lock_guard Lock(State.DomainControlMutex);
				bDisconnecting = State.bDomainDisconnecting;
				if (!bDisconnecting && (Domain = State.LiveDomain.load()))
				{
					++State.DomainControlsInFlight;
				}
			}

			~FDomainControlScope()
			{
				if (Domain)
				{
					std::lock_guard Lock(State.DomainControlMutex);
					if (--State.DomainControlsInFlight == 0)
					{
						State.DomainControlsIdle.notify_all();
					}
				}
			}

			FDomainControlScope(const FDomainControlScope&) = delete;

			FDomainControlScope& operator=(const FDomainControlScope&) = delete;

			FState& State;

			IVeroScriptLiveDebugDomain* Domain = nullptr;

			bool bDisconnecting = false;
		};

		int AllocateVariablesReference() const
		{
			if (NextVariablesReference > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
			{
				return 0;
			}
			return static_cast<int>(NextVariablesReference++);
		}

		void ClearChildren() const
		{
			VariablePages.clear();
			ChildReferences.clear();
		}

		void ClearStoppedHandles()
		{
			ClearChildren();
			FrameIds.clear();
			LocalsReferences.clear();
			FrameIndices.clear();
			LocalsIndices.clear();
		}

		void BindStoppedFrames()
		{
			ClearStoppedHandles();
			const auto Frames = Session.GetStackTrace();
			for (std::size_t Index = 0; Index < Frames.size(); ++Index)
			{
				if (NextFrameId > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
				{
					break;
				}
				const int Reference = AllocateVariablesReference();
				if (!Reference)
				{
					break;
				}
				const int Id = static_cast<int>(NextFrameId++);
				FrameIds.push_back(Id);
				LocalsReferences.push_back(Reference);
				FrameIndices.emplace(Id, Index);
				LocalsIndices.emplace(Reference, Index);
			}
		}

		VeroScriptDebugSession Session;
		std::atomic<IVeroScriptLiveDebugDomain*> LiveDomain{nullptr};
		std::mutex DomainControlMutex;
		std::condition_variable DomainControlsIdle;
		std::size_t DomainControlsInFlight = 0;
		bool bDomainDisconnecting = false;

		bool bDomainActivated = false;
		std::shared_ptr<FEventHandler> EventHandler;
		bool bInitialized = false;
		bool bLaunched = false;
		bool bStopOnEntry = false;
		bool bPendingEntryStop = false;
		std::atomic<bool> bStopped{false};
		std::atomic<bool> bRunning{false};
		std::atomic<bool> bDisconnectAfterRun{false};
		std::string PrimaryPath;
		// Opaque identities are never recycled during this adapter's lifetime.
		// A delayed request from an earlier stop cannot name a current frame/page.
		std::uint64_t NextFrameId = 0;
		mutable std::uint64_t NextVariablesReference = 1;
		std::vector<int> FrameIds;
		std::vector<int> LocalsReferences;
		std::unordered_map<int, std::size_t> FrameIndices;
		std::unordered_map<int, std::size_t> LocalsIndices;
		mutable std::unordered_map<int, FVariablePage> VariablePages;
		mutable std::unordered_map<int, std::unordered_map<std::size_t, int>> ChildReferences;
		std::vector<FStoredBreakpoint> Breakpoints;
		int NextBreakpointId = 1;
	};

	namespace
	{
		const void* ContainerIdentity(const FValue& Value)
		{
			if (const auto* Box = std::get_if<FScriptVariantHandle>(&Value)) return Box->get();
			if (const auto* const Object = std::get_if<FObjectHandle>(&Value))
			{
				return Object->get();
			}
			if (const auto* const Array = std::get_if<FDynamicArrayHandle>(&Value))
			{
				return Array->get();
			}
			if (const auto* const Map = std::get_if<FMapHandle>(&Value))
			{
				return Map->get();
			}
			if (const auto* const Set = std::get_if<FSetHandle>(&Value))
			{
				return Set->get();
			}
			if (const auto* const Queue = std::get_if<FQueueHandle>(&Value))
			{
				return Queue->get();
			}
			if (const auto* const Stack = std::get_if<FStackHandle>(&Value))
			{
				return Stack->get();
			}
			if (const auto* const OrderedMap = std::get_if<FOrderedMapHandle>(&Value))
			{
				return OrderedMap->get();
			}
			if (const auto* const Deque = std::get_if<FDequeHandle>(&Value))
			{
				return Deque->get();
			}
			if (const auto* const Function = std::get_if<FFunctionHandle>(&Value))
			{
				return Function->get();
			}
			if (const auto* const Host = std::get_if<FHostObjectHandle>(&Value))
			{
				return Host->get();
			}
			return nullptr;
		}

		// Pretty-print VM linkage names for DAP stack frames (drop \x1F / #fn / arity).
		std::string FormatDebugFunctionName(const std::string& Mangled)
		{
			const auto DisplayLeaf = [](const std::string_view Leaf)
			{
				// Core's compact lambda linkage is an execution identity, not a name
				// the user can navigate or write in source.
				return Leaf.rfind("$L", 0) == 0 ? std::string("<lambda>") : std::string(Leaf);
			};
			if (Mangled.empty())
			{
				return "<frame>";
			}
			if (const std::string_view MethodLeaf = MethodLinkageBareLeaf(Mangled);
				!MethodLeaf.empty())
			{
				return DisplayLeaf(MethodLeaf);
			}

			const std::size_t Signature = FindFunctionSignatureMarker(Mangled);
			const std::size_t IdentityStart = FindLinkageIdentityStart(Mangled);
			const std::string_view Head = std::string_view(Mangled).substr(
				IdentityStart,
				(Signature == std::string_view::npos ? Mangled.size() : Signature)
					- IdentityStart);
			const std::size_t Generic = Head.find('#');
			std::string_view Leaf = Head.substr(
				0,
				Generic == std::string_view::npos
					? Head.size()
					: Generic);
			return DisplayLeaf(Leaf);
		}
	}

	VeroScriptDapAdapter::VeroScriptDapAdapter()
		: State(std::make_unique<FState>())
	{
	}

	VeroScriptDapAdapter::~VeroScriptDapAdapter()
	{
		if (State->LiveDomain)
		{
			Disconnect();
		}
	}

	void VeroScriptDapAdapter::SetEventHandler(FEventHandler Handler)
	{
		State->EventHandler = Handler ? std::make_shared<FEventHandler>(std::move(Handler)) : nullptr;
	}

	FDapInitializeResult VeroScriptDapAdapter::Initialize()
	{
		State->bInitialized = true;
		FDapInitializeResult Result;
		Result.Version = VeroScript::GetVersionString();
		Result.Platform = VeroScript::GetHostPlatformString();
		Result.JitCapability = VeroScript::GetJitCapabilityString();
		return Result;
	}

	FDapLaunchResult VeroScriptDapAdapter::Launch(const FDapLaunchArgs& Args)
	{
		FDapLaunchResult Result;
		if (State->bRunning.load())
		{
			FScriptError Error;
			FillDiagnostic(Error, "dap.already_running");
			Result.Diagnostics.push_back(ToToolingDiagnostic(Error, "<dap>"));
			return Result;
		}
		if (State->LiveDomain)
		{
			FDiagnostic Diagnostic;
			Diagnostic.Message = "disconnect the live domain before loading a program";
			Result.Diagnostics.push_back(std::move(Diagnostic));
			return Result;
		}
		if (!State->bInitialized)
		{
			FScriptError Error;
			FillDiagnostic(Error, "diag.initialize_must_be_called_before_launch");
			Result.Diagnostics.push_back(ToToolingDiagnostic(Error, "<dap>"));
			return Result;
		}

		State->Session.Clear();
		State->ClearStoppedHandles();
		State->bLaunched = false;
		State->bStopOnEntry = Args.bStopOnEntry;
		State->bPendingEntryStop = Args.bStopOnEntry;

		FDebugLoadOptions Options;
		std::vector<std::string_view> ImportViews;
		ImportViews.reserve(Args.ImportPaths.size());
		for (const std::string& Path : Args.ImportPaths)
		{
			ImportViews.push_back(Path);
		}
		Options.ImportPaths = ImportViews;
		std::vector<std::string_view> StdlibViews;
		StdlibViews.reserve(Args.StdlibSearchPaths.size());
		for (const std::string& Path : Args.StdlibSearchPaths)
		{
			StdlibViews.push_back(Path);
		}
		Options.StdlibSearchPaths = StdlibViews;
		std::vector<std::string_view> ImplicitImportViews;
		ImplicitImportViews.reserve(Args.ImplicitImportPaths.size());
		for (const std::string& Path : Args.ImplicitImportPaths)
		{
			ImplicitImportViews.push_back(Path);
		}
		Options.ImplicitImportPaths = ImplicitImportViews;
		Options.bAllowCircularImports = Args.bAllowCircularImports;
		Options.bAllowProjectWideAccess = Args.bAllowProjectWideAccess;
		std::vector<std::string_view> DefineViews;
		DefineViews.reserve(Args.Defines.size());
		for (const std::string& Define : Args.Defines)
		{
			DefineViews.push_back(Define);
		}
		Options.Defines = DefineViews;
		Options.PredefinedMacros = Args.PredefinedMacros;
		Options.SourceOverlays = Args.SourceOverlays;
		Options.Bindings = Args.Bindings;
		Options.AnnotationProcessing = Args.AnnotationProcessing;
		Options.CompileProcessing = Args.CompileProcessing;
		Options.ScriptCompileExtensions = Args.ScriptCompileExtensions;
		Options.VmBackendFactory = Args.VmBackendFactory;
		Options.VmBackendPath = Args.VmBackendPath;
		Options.ModuleEnvironment = Args.ModuleEnvironment;
		State->Session.SetExecutionBackend(Args.ExecutionBackend);
		State->Session.SetMaxCallDepth(Args.MaxCallDepth);
		State->Session.SetScriptArguments(Args.ScriptArgs);
		State->Session.SetInputFromString(Args.Input);
		if (Args.bLiveBindings && (!Args.Environment.empty() || !Args.WorkingDirectory.empty()))
		{
			FScriptError Error;
			FillDiagnostic(Error, "diag.debug_live_process_context_unsupported");
			Result.Diagnostics.push_back(ToToolingDiagnostic(Error, "<dap>"));
			return Result;
		}
		State->Session.SetEnvironment(Args.Environment);
		State->Session.SetWorkingDirectory(Args.WorkingDirectory);

		FDebugLoadResult Load;
		if (!Args.Source.empty())
		{
			State->PrimaryPath = Args.FileLabel.empty() ? std::string("<source>") : Args.FileLabel;
			Load = State->Session.LoadSource(Args.Source, State->PrimaryPath, Options);
		}
		else if (!Args.Path.empty())
		{
			State->PrimaryPath = Args.Path;
			Load = State->Session.LoadFile(Args.Path, Options);
		}
		else
		{
			FScriptError Error;
			FillDiagnostic(Error, "diag.launch_requires_source_or_path");
			Result.Diagnostics.push_back(ToToolingDiagnostic(Error, "<dap>"));
			return Result;
		}

		Result.bSuccess = Load.bSuccess;
		Result.Diagnostics = std::move(Load.Diagnostics);
		if (Result.bSuccess)
		{
			State->bLaunched = true;
			ReapplyBreakpoints();
		}
		return Result;
	}

	FDapLaunchResult VeroScriptDapAdapter::Attach(const FDapLaunchArgs& Args)
	{
		return Launch(Args);
	}

	FDapLaunchResult VeroScriptDapAdapter::AttachDomain(
		IVeroScriptLiveDebugDomain& Domain,
		const std::string_view ExpectedDomainId)
	{
		FDapLaunchResult Result;
		if (!State->bInitialized || State->LiveDomain || State->bLaunched
			|| State->bRunning.load())
		{
			FDiagnostic Diagnostic;
			Diagnostic.Message = "live domain attach requires an initialized, idle adapter";
			Result.Diagnostics.push_back(std::move(Diagnostic));
			return Result;
		}
		const FLiveDebugDomainInfo Info = Domain.Info();
		if (Info.HostId.empty() || Info.DomainId.empty()
			|| (!ExpectedDomainId.empty() && Info.DomainId != ExpectedDomainId))
		{
			FDiagnostic Diagnostic;
			Diagnostic.Message = "host debug domain identity is missing or does not match";
			Result.Diagnostics.push_back(std::move(Diagnostic));
			return Result;
		}
		std::string Error;
		if (!Domain.Connect([this](const FDapEvent &Event)
							{
								Emit(Event);
							},
							Error))
		{
			FDiagnostic Diagnostic;
			Diagnostic.Message = Error.empty() ? "host debug domain refused attach" : Error;
			Result.Diagnostics.push_back(std::move(Diagnostic));
			return Result;
		}
		struct FPendingConnection
		{
			~FPendingConnection()
			{
				if (!bCommitted)
				{
					Domain.Disconnect();
				}
			}

			IVeroScriptLiveDebugDomain& Domain;

			bool bCommitted = false;
		} Connection{Domain};
		State->ClearStoppedHandles();
		State->bDomainActivated = false;
		std::vector<FDapBreakpoint> PendingChanges;
		PendingChanges.reserve(State->Breakpoints.size());
		std::vector<std::string> Paths;
		std::unordered_set<std::string> PathKeys;
		for (FState::FStoredBreakpoint& Entry : State->Breakpoints)
		{
			Entry.PathKey = PathUtil::IdentityKey(Entry.Path);
			if (PathKeys.insert(Entry.PathKey).second)
			{
				Paths.push_back(Entry.Path);
			}
		}
		for (const std::string& Path : Paths)
		{
			const std::string PathKey = PathUtil::IdentityKey(Path);
			std::vector<FLiveDebugBreakpoint> Pending;
			for (const FState::FStoredBreakpoint& Entry : State->Breakpoints)
			{
				if (Entry.PathKey == PathKey)
				{
					Pending.push_back({Entry.Id, Entry.Line, Entry.Condition});
				}
			}
			if (!Info.bCanUseConditionalBreakpoints && std::any_of(Pending.begin(), Pending.end(),
																   [](const FLiveDebugBreakpoint &Entry)
																   {
																	   return !Entry.Condition.empty();
																   }))
			{
				FDiagnostic Diagnostic;
				Diagnostic.Message = "host debug domain does not support conditional breakpoints";
				Result.Diagnostics.push_back(std::move(Diagnostic));
				return Result;
			}
			const FDapSetBreakpointsResult Bound = Domain.SetBreakpoints(Path, Pending);
			if (!Bound.bSuccess || Bound.Breakpoints.size() != Pending.size())
			{
				FDiagnostic Diagnostic;
				Diagnostic.Message = "host debug domain could not bind pending breakpoints";
				Result.Diagnostics.push_back(std::move(Diagnostic));
				return Result;
			}
			for (std::size_t Index = 0; Index < Bound.Breakpoints.size(); ++Index)
			{
				FDapBreakpoint Breakpoint = Bound.Breakpoints[Index];
				Breakpoint.Id = Pending[Index].Id;
				Breakpoint.Path = Path;
				PendingChanges.push_back(std::move(Breakpoint));
			}
		}
		// A later source may reject attachment and roll back every binding.
		// Publish initial breakpoint changes only after all sources are ready.
		for (auto& Breakpoint : PendingChanges)
		{
			FDapEvent Event;
			Event.Kind = EDapEventKind::Breakpoint;
			Event.Breakpoint = std::move(Breakpoint);
			Emit(std::move(Event));
		}
		// Publish only the complete connection. Cross-thread controls must not
		// enter Connect or a breakpoint-binding rollback that cannot drain them.
		State->LiveDomain.store(&Domain);
		Connection.bCommitted = true;
		Result.bSuccess = true;
		return Result;
	}

	bool VeroScriptDapAdapter::IsDomainAttached() const
	{
		return State->LiveDomain != nullptr;
	}

	FLiveDebugDomainInfo VeroScriptDapAdapter::AttachedDomainInfo() const
	{
		const auto* Domain = State->LiveDomain.load();
		return Domain ? Domain->Info() : FLiveDebugDomainInfo{};
	}

	FDapSetBreakpointsResult VeroScriptDapAdapter::SetBreakpoints(
		const std::string_view Path,
		const std::vector<FDapSourceBreakpoint>& Breakpoints)
	{
		FDapSetBreakpointsResult Result;
		const std::string PathOwned(Path);
		const std::string PathKey = PathUtil::IdentityKey(PathOwned);
		std::string SourcePath = PathOwned;
		// Refresh identities here: a pending relative source may have become an
		// existing file since the request that registered it. Compute each key once.
		for (FState::FStoredBreakpoint& Entry : State->Breakpoints)
		{
			Entry.PathKey = PathUtil::IdentityKey(Entry.Path);
			if (Entry.PathKey == PathKey)
			{
				SourcePath = Entry.Path;
			}
		}
		const std::vector<FState::FStoredBreakpoint> PreviousBreakpoints =
			State->LiveDomain ? State->Breakpoints : std::vector<FState::FStoredBreakpoint>{};

		std::vector<FState::FStoredBreakpoint> Remaining;
		Remaining.reserve(State->Breakpoints.size() + Breakpoints.size());
		for (const FState::FStoredBreakpoint& Entry : State->Breakpoints)
		{
			if (Entry.PathKey != PathKey)
			{
				Remaining.push_back(Entry);
			}
		}
		for (const FDapSourceBreakpoint& Breakpoint : Breakpoints)
		{
			FState::FStoredBreakpoint Stored;
			// A setBreakpoints request replaces this source's entries. Preserve the
			// identities of other sources even though engine registrations are rebuilt.
			for (;;)
			{
				const int Candidate = State->NextBreakpointId;
				State->NextBreakpointId = Candidate == std::numeric_limits<int>::max()
					? 1
					: Candidate + 1;
				const bool bUsed = std::any_of(Remaining.begin(), Remaining.end(),
					[Candidate](const FState::FStoredBreakpoint& Entry)
					{
						return Entry.Id == Candidate;
					});
				if (!bUsed)
				{
					Stored.Id = Candidate;
					break;
				}
			}
			Stored.Path = SourcePath;
			Stored.PathKey = PathKey;
			Stored.Line = Breakpoint.Line;
			Stored.Condition = Breakpoint.Condition;
			Remaining.push_back(std::move(Stored));
		}
		State->Breakpoints = std::move(Remaining);
		if (State->LiveDomain)
		{
			std::vector<FLiveDebugBreakpoint> HostBreakpoints;
			for (const FState::FStoredBreakpoint& Entry : State->Breakpoints)
			{
				if (Entry.PathKey == PathKey)
				{
					HostBreakpoints.push_back({Entry.Id, Entry.Line, Entry.Condition});
				}
			}
			if (!State->LiveDomain.load()->Info().bCanUseConditionalBreakpoints && std::any_of(HostBreakpoints.begin(), HostBreakpoints.end(),
																							   [](const FLiveDebugBreakpoint &Entry)
																							   {
																								   return !Entry.Condition.empty();
																							   }))
			{
				State->Breakpoints = PreviousBreakpoints;
				Result.bSuccess = false;
				return Result;
			}
			Result = State->LiveDomain.load()->SetBreakpoints(SourcePath, HostBreakpoints);
			if (Result.bSuccess && Result.Breakpoints.size() != HostBreakpoints.size())
			{
				Result.bSuccess = false;
			}
			if (!Result.bSuccess)
			{
				State->Breakpoints = PreviousBreakpoints;
			}
			for (std::size_t Index = 0; Index < Result.Breakpoints.size()
				&& Index < HostBreakpoints.size(); ++Index)
			{
				Result.Breakpoints[Index].Id = HostBreakpoints[Index].Id;
				Result.Breakpoints[Index].Path = PathOwned;
			}
			return Result;
		}

		State->Session.ClearBreakpoints();
		const bool bLoaded = State->Session.IsLoaded();
		for (const FState::FStoredBreakpoint& Entry : State->Breakpoints)
		{
			VeroScriptDebugSession::FBreakpointBinding Binding;
			if (bLoaded)
			{
				Binding = State->Session.BindBreakpoint(
					Entry.Path,
					Entry.Line,
					Entry.Condition);
			}
			if (Entry.PathKey != PathKey)
			{
				continue;
			}

			FDapBreakpoint Mapped;
			Mapped.Path = PathOwned;
			Mapped.Line = Binding.Id > 0 ? Binding.Line : Entry.Line;
			Mapped.Id = Entry.Id;
			Mapped.bVerified = Binding.Id > 0;
			Mapped.Message = bLoaded
				? std::move(Binding.Message)
				: Locale::TranslateId("dap.breakpoint.pending_launch");
			Result.Breakpoints.push_back(std::move(Mapped));
		}

		return Result;
	}

	FDebugRunResult VeroScriptDapAdapter::ConfigurationDone()
	{
		FDebugRunResult Result;
		if (State->bRunning.load())
		{
			FScriptError Error;
			FillDiagnostic(Error, "dap.already_running");
			Result.Diagnostics.push_back(ToToolingDiagnostic(Error, "<dap>"));
			return Result;
		}
		if (State->LiveDomain)
		{
			if (State->bDomainActivated)
			{
				Result.bSuccess = true;
				return Result;
			}
			std::string Error;
			Result.bSuccess = State->LiveDomain.load()->Activate(Error);
			State->bDomainActivated = Result.bSuccess;
			if (!Result.bSuccess)
			{
				FDiagnostic Diagnostic;
				Diagnostic.Message = Error.empty() ? "host debug domain activation failed" : Error;
				Result.Diagnostics.push_back(std::move(Diagnostic));
			}
			return Result;
		}
		if (!State->bLaunched || !State->Session.IsLoaded())
		{
			FScriptError Error;
			FillDiagnostic(Error, "diag.configuration_done_requires_a_successful_launch");
			Result.Diagnostics.push_back(ToToolingDiagnostic(Error, "<dap>"));
			return Result;
		}

		State->Session.SetStopHandler(
			[this](VeroScriptDebugSession&, const EDebugStopReason Reason)
			{
				State->BindStoppedFrames();
				State->bStopped.store(true);
				FDapEvent Event;
				Event.Kind = EDapEventKind::Stopped;
				Event.ThreadId = MainThreadId;
				Event.StopReason = MapStopReason(Reason);
				Emit(std::move(Event));
				State->bStopped = false;
			});

		if (State->bStopOnEntry)
		{
			State->Session.SetRunMode(EDebugRunMode::StepIn);
		}
		else
		{
			State->Session.SetRunMode(EDebugRunMode::Continue);
		}

		State->Session.ResetRunControl();
		State->bRunning = true;
		State->Session.SetOutputHandler(
			[this](const std::string_view Text, const bool bIsError)
			{
				if (Text.empty())
				{
					return;
				}
				FDapEvent OutputEvent;
				OutputEvent.Kind = EDapEventKind::Output;
				OutputEvent.Category = bIsError ? "stderr" : "stdout";
				OutputEvent.Output.assign(Text.data(), Text.size());
				Emit(std::move(OutputEvent));
			});
		try
		{
			Result = State->Session.Run();
		}
		catch (const std::exception& Exception)
		{
			Result.bSuccess = false;
			Result.ExitCode = 1;
			FScriptError Error;
			FillDiagnostic(Error, "diag.debug_execution_failed", {}, {Exception.what()});
			Result.Diagnostics.push_back(ToToolingDiagnostic(Error, "<dap>"));
		}
		catch (...)
		{
			Result.bSuccess = false;
			Result.ExitCode = 1;
			FScriptError Error;
			FillDiagnostic(Error, "diag.debug_execution_failed_unknown");
			Result.Diagnostics.push_back(ToToolingDiagnostic(Error, "<dap>"));
		}
		State->Session.SetOutputHandler({});
		State->bRunning = false;
		State->bStopped = false;
		State->ClearStoppedHandles();
		for (const FDiagnostic& Diagnostic : Result.Diagnostics)
		{
			FDapEvent ErrorEvent;
			ErrorEvent.Kind = EDapEventKind::Output;
			ErrorEvent.Category = "stderr";
			if (!Diagnostic.FileLabel.empty())
			{
				ErrorEvent.Output += Diagnostic.FileLabel + ":"
					+ std::to_string(Diagnostic.Range.StartLine) + ":"
					+ std::to_string(Diagnostic.Range.StartColumn) + ": ";
			}
			ErrorEvent.Output += Diagnostic.Message;
			if (ErrorEvent.Output.empty() || ErrorEvent.Output.back() != '\n')
			{
				ErrorEvent.Output.push_back('\n');
			}
			Emit(std::move(ErrorEvent));
		}

		FDapEvent Exited;
		Exited.Kind = EDapEventKind::Exited;
		Exited.ExitCode = Result.ExitCode;
		Emit(std::move(Exited));

		FDapEvent Terminated;
		Terminated.Kind = EDapEventKind::Terminated;
		Emit(std::move(Terminated));

		if (State->bDisconnectAfterRun)
		{
			ClearSessionState();
		}

		return Result;
	}

	FDapContinueResult VeroScriptDapAdapter::Continue()
	{
		return Resume(EDebugRunMode::Continue);
	}

	FDapContinueResult VeroScriptDapAdapter::Next()
	{
		return Resume(EDebugRunMode::StepOver);
	}

	FDapContinueResult VeroScriptDapAdapter::StepIn()
	{
		return Resume(EDebugRunMode::StepIn);
	}

	FDapContinueResult VeroScriptDapAdapter::StepOut()
	{
		return Resume(EDebugRunMode::StepOut);
	}

	FDapContinueResult VeroScriptDapAdapter::Pause()
	{
		FState::FDomainControlScope Control(*State);
		if (Control.bDisconnecting) return {false, false};
		if (auto* const Domain = Control.Domain)
		{
			return Domain->Info().bCanPause
				? Domain->Pause() : FDapContinueResult{false, false};
		}
		FDapContinueResult Result;
		if (!State->bRunning.load() || State->bStopped.load())
		{
			Result.bSuccess = false;
			return Result;
		}
		State->Session.RequestPause();
		return Result;
	}

	void VeroScriptDapAdapter::Terminate()
	{
		FState::FDomainControlScope Control(*State);
		if (Control.bDisconnecting) return;
		if (auto* const Domain = Control.Domain)
		{
			if (Domain->Info().bCanTerminate)
			{
				Domain->RequestCancel();
			}
			return;
		}
		if (!State->bRunning.load())
		{
			return;
		}
		State->Session.RequestCancel();
		// Cancellation is the cross-thread control. Run mode belongs to the
		// execution thread, even while a stop callback is unwinding or resuming.
	}

	FDapStackTraceResult VeroScriptDapAdapter::StackTrace() const
	{
		if (State->LiveDomain) return State->LiveDomain.load()->StackTrace();
		FDapStackTraceResult Result;
		if (!State->bStopped)
		{
			Result.bSuccess = false;
			return Result;
		}

		const std::vector<FDebugStackFrame> Frames = State->Session.GetStackTrace();
		if (Frames.size() != State->FrameIds.size())
		{
			Result.bSuccess = false;
			return Result;
		}
		Result.StackFrames.reserve(State->FrameIds.size());
		for (std::size_t Index = 0; Index < Frames.size() && Index < State->FrameIds.size(); ++Index)
		{
			FDapStackFrame Mapped;
			Mapped.Id = State->FrameIds[Index];
			Mapped.Name = FormatDebugFunctionName(Frames[Index].FunctionName);
			Mapped.Path = Frames[Index].FileLabel;
			Mapped.Line = Frames[Index].Line;
			Mapped.Column = State->Session.GetUtf16Column(Mapped.Path, Mapped.Line, Frames[Index].Column);
			Result.StackFrames.push_back(std::move(Mapped));
		}
		return Result;
	}

	bool VeroScriptDapAdapter::TryGetGeneratedSource(std::string_view FileLabel, std::string& OutSource) const
	{
		OutSource.clear();
		if (!IsStopped() || FileLabel.rfind("veroscript-generated:", 0) != 0) return false;
		if (State->LiveDomain) return State->LiveDomain.load()->TryGetGeneratedSource(FileLabel, OutSource);
		return State->Session.TryGetGeneratedSource(FileLabel, OutSource);
	}

	bool VeroScriptDapAdapter::TryGetSource(std::string_view FileLabel, std::string& OutSource) const
	{
		OutSource.clear();
		if (!IsStopped()) return false;
		if (State->LiveDomain) return State->LiveDomain.load()->TryGetGeneratedSource(FileLabel, OutSource);
		return State->Session.TryGetSource(FileLabel, OutSource);
	}
	FDapScopesResult VeroScriptDapAdapter::Scopes(const int FrameId) const
	{
		if (State->LiveDomain) return State->LiveDomain.load()->Scopes(FrameId);
		FDapScopesResult Result;
		if (!State->bStopped || FrameId < 0)
		{
			Result.bSuccess = false;
			return Result;
		}
		const auto Frame = State->FrameIndices.find(FrameId);
		if (Frame == State->FrameIndices.end())
		{
			Result.bSuccess = false;
			return Result;
		}

		FDapScope Locals;
		Locals.Name = "Locals";
		Locals.VariablesReference = State->LocalsReferences[Frame->second];
		Result.Scopes.push_back(std::move(Locals));
		return Result;
	}

	FDapVariablesResult VeroScriptDapAdapter::Variables(const int VariablesReference) const
	{
		if (State->LiveDomain) return State->LiveDomain.load()->Variables(VariablesReference);
		FDapVariablesResult Result;
		if (!State->bStopped || VariablesReference <= 0)
		{
			Result.bSuccess = false;
			return Result;
		}

		std::vector<FDebugVariable> Variables;
		std::unordered_set<const void*> Ancestors;
		std::size_t PageFrameIndex = 0;
		if (const auto Locals = State->LocalsIndices.find(VariablesReference);
			Locals != State->LocalsIndices.end())
		{
			PageFrameIndex = Locals->second;
			Variables = State->Session.GetLocals(PageFrameIndex);
		}
		else
		{
			const auto Found = State->VariablePages.find(VariablesReference);
			if (Found == State->VariablePages.end())
			{
				Result.bSuccess = false;
				return Result;
			}
			Variables = Found->second.Variables;
			Ancestors = Found->second.Ancestors;
			PageFrameIndex = Found->second.FrameIndex;
		}

		for (std::size_t VariableIndex = 0; VariableIndex < Variables.size(); ++VariableIndex)
		{
			const FDebugVariable& Variable = Variables[VariableIndex];
			FDapVariable Mapped;
			Mapped.Name = Variable.Name;
			Mapped.Value = Variable.Value;
			Mapped.Memory = Variable.Memory;
			Mapped.bReadOnly = Variable.bReadOnly;
			Mapped.VariablesReference = 0;
			const void* const Identity = Variable.bHasRawValue
				? ContainerIdentity(Variable.RawValue)
				: nullptr;
			if (Identity != nullptr && Ancestors.find(Identity) != Ancestors.end())
			{
				Mapped.Value += " <cycle>";
			}
			else
			{
				const auto& References = State->ChildReferences[VariablesReference];
				if (const auto Existing = References.find(VariableIndex); Existing != References.end())
				{
					Mapped.VariablesReference = Existing->second;
					Result.Variables.push_back(std::move(Mapped));
					continue;
				}
				std::vector<FDebugVariable> Children = State->Session.GetChildren(Variable);
				if (!Children.empty())
				{
					FState::FVariablePage Page;
					Page.Variables = std::move(Children);
					Page.Ancestors = Ancestors;
					Page.FrameIndex = PageFrameIndex;
					if (Identity != nullptr)
					{
						Page.Ancestors.insert(Identity);
					}
					Mapped.VariablesReference = State->AllocateVariablesReference();
					if (Mapped.VariablesReference)
					{
						State->VariablePages.emplace(Mapped.VariablesReference, std::move(Page));
						State->ChildReferences[VariablesReference].emplace(VariableIndex, Mapped.VariablesReference);
					}
				}
			}
			Result.Variables.push_back(std::move(Mapped));
		}
		return Result;
	}

	FDapEvaluateResult VeroScriptDapAdapter::Evaluate(
		const std::string_view Expression,
		const int FrameId,
		const std::string_view Context) const
	{
		if (State->LiveDomain)
		{
			return State->LiveDomain.load()->Info().bCanEvaluate
				? State->LiveDomain.load()->Evaluate(Expression, FrameId, Context)
				: FDapEvaluateResult{};
		}
		FDapEvaluateResult Result;
		if (!State->bStopped)
		{
			Result.Error = "evaluate requires a stopped frame";
			return Result;
		}
		const auto Frame = State->FrameIndices.find(FrameId);
		if (Frame == State->FrameIndices.end())
		{
			Result.Error = "unknown or expired frameId";
			return Result;
		}
		const FDebugEvaluateResult Eval =
			State->Session.Evaluate(Expression, Frame->second);
		Result.bSuccess = Eval.bOk;
		Result.Result = Eval.Value;
		Result.Error = Eval.Error;
		if (Eval.bOk && Eval.bHasRawValue)
		{
			FDebugVariable Variable;
			Variable.Name = std::string(Expression);
			Variable.Value = Eval.Value;
			Variable.RawValue = Eval.RawValue;
			Variable.bHasRawValue = true;
			std::vector<FDebugVariable> Children = State->Session.GetChildren(Variable);
			if (!Children.empty())
			{
				FState::FVariablePage Page;
				Page.Variables = std::move(Children);
				Page.FrameIndex = Frame->second;
				if (const void* const Identity = ContainerIdentity(Eval.RawValue))
				{
					Page.Ancestors.insert(Identity);
				}
				Result.VariablesReference = State->AllocateVariablesReference();
				if (Result.VariablesReference)
				{
					State->VariablePages.emplace(Result.VariablesReference, std::move(Page));
				}
			}
		}
		return Result;
	}

	FDapSetVariableResult VeroScriptDapAdapter::SetVariable(
		const int VariablesReference,
		const std::string_view Name,
		const std::string_view Value)
	{
		if (State->LiveDomain)
		{
			return State->LiveDomain.load()->Info().bCanSetVariable
				? State->LiveDomain.load()->SetVariable(VariablesReference, Name, Value)
				: FDapSetVariableResult{};
		}
		FDapSetVariableResult Result;
		if (!State->bStopped || VariablesReference <= 0)
		{
			Result.Error = "setVariable requires a stopped frame";
			return Result;
		}
		const auto Locals = State->LocalsIndices.find(VariablesReference);
		std::size_t FrameIndex = 0;
		FDebugEvaluateResult Set;
		if (Locals == State->LocalsIndices.end())
		{
			const auto Page = State->VariablePages.find(VariablesReference);
			if (Page == State->VariablePages.end())
			{
				Result.Error = "unknown variablesReference";
				return Result;
			}
			const auto Child = std::find_if(
				Page->second.Variables.begin(),
				Page->second.Variables.end(),
				[Name](const FDebugVariable& Variable)
				{
					return Variable.Name == Name;
				});
			if (Child == Page->second.Variables.end())
			{
				Result.Error = "unknown container child";
				return Result;
			}
			// Capture the frame before ClearChildren invalidates Page and Child.
			FrameIndex = Page->second.FrameIndex;
			Set = State->Session.SetChild(
				*Child,
				Value,
				FrameIndex);
		}
		else
		{
			FrameIndex = Locals->second;
			Set = State->Session.SetLocal(Name, Value, FrameIndex);
		}
		Result.bSuccess = Set.bOk;
		Result.Value = Set.Value;
		Result.Error = Set.Error;
		if (Result.bSuccess)
		{
			State->ClearChildren();
			if (Set.bHasRawValue)
			{
				FDebugVariable Variable;
				Variable.Name = std::string(Name);
				Variable.Value = Set.Value;
				Variable.RawValue = Set.RawValue;
				Variable.bHasRawValue = true;
				std::vector<FDebugVariable> Children = State->Session.GetChildren(Variable);
				if (!Children.empty())
				{
					FState::FVariablePage Page;
					Page.Variables = std::move(Children);
					Page.FrameIndex = FrameIndex;
					if (const void* const Identity = ContainerIdentity(Set.RawValue))
					{
						Page.Ancestors.insert(Identity);
					}
					Result.VariablesReference = State->AllocateVariablesReference();
					if (Result.VariablesReference)
					{
						State->VariablePages.emplace(Result.VariablesReference, std::move(Page));
					}
				}
			}
		}
		return Result;
	}

	void VeroScriptDapAdapter::Disconnect()
	{
		IVeroScriptLiveDebugDomain* Domain = nullptr;
		{
			std::unique_lock Lock(State->DomainControlMutex);
			Domain = State->LiveDomain.load();
			if (Domain)
			{
				State->bDomainDisconnecting = true;
				State->DomainControlsIdle.wait(Lock,
					[&]
					{
						return State->DomainControlsInFlight == 0;
					});
			}
		}
		if (Domain)
		{
			// Host callback draining must run without the adapter's control mutex.
			// New controls observe the closing state and return without host access.
			Domain->Disconnect();
			{
				std::lock_guard Lock(State->DomainControlMutex);
				State->LiveDomain.store(nullptr);
				State->bDomainDisconnecting = false;
			}
			State->bDomainActivated = false;
			ClearSessionState();
			return;
		}
		if (State->bRunning)
		{
			// Never Clear() while Execute is on the stack — only resume and finish teardown after Run.
			State->bDisconnectAfterRun = true;
			if (State->bStopped.exchange(false))
			{
				State->Session.SetRunMode(EDebugRunMode::Continue);
			}
			return;
		}

		ClearSessionState();
	}

	bool VeroScriptDapAdapter::IsLoaded() const
	{
		if (State->LiveDomain) return true;
		return State->Session.IsLoaded();
	}

	bool VeroScriptDapAdapter::IsStopped() const
	{
		if (State->LiveDomain) return State->LiveDomain.load()->IsStopped();
		return State->bStopped.load();
	}

	bool VeroScriptDapAdapter::IsRunning() const
	{
		if (State->LiveDomain) return State->LiveDomain.load()->IsContextRunning();
		return State->bRunning.load();
	}

	void VeroScriptDapAdapter::Emit(FDapEvent Event) const
	{
		const auto Handler = State->EventHandler;
		if (Handler)
		{
			(*Handler)(Event);
		}
	}

	void VeroScriptDapAdapter::ReapplyBreakpoints()
	{
		State->Session.ClearBreakpoints();
		if (!State->Session.IsLoaded())
		{
			return;
		}
		std::vector<FDapEvent> Events;
		Events.reserve(State->Breakpoints.size());
		for (const FState::FStoredBreakpoint& Entry : State->Breakpoints)
		{
			const VeroScriptDebugSession::FBreakpointBinding Binding =
				State->Session.BindBreakpoint(
				Entry.Path,
				Entry.Line,
				Entry.Condition);
			FDapEvent Event;
			Event.Kind = EDapEventKind::Breakpoint;
			Event.Breakpoint.Id = Entry.Id;
			Event.Breakpoint.Path = Entry.Path;
			Event.Breakpoint.Line = Binding.Id > 0 ? Binding.Line : Entry.Line;
			Event.Breakpoint.bVerified = Binding.Id > 0;
			Event.Breakpoint.Message = Binding.Message;
			Events.push_back(std::move(Event));
		}
		for (FDapEvent& Event : Events)
		{
			Emit(std::move(Event));
		}
	}

	void VeroScriptDapAdapter::ClearSessionState()
	{
		State->Session.Clear();
		State->Session.ClearBreakpoints();
		State->Breakpoints.clear();
		State->NextBreakpointId = 1;
		State->bLaunched = false;
		State->bRunning = false;
		State->bStopped = false;
		State->bPendingEntryStop = false;
		State->bDisconnectAfterRun = false;
		State->PrimaryPath.clear();
		State->ClearStoppedHandles();
	}

	FDapContinueResult VeroScriptDapAdapter::Resume(const EDebugRunMode Mode)
	{
		if (State->LiveDomain)
		{
			return (Mode == EDebugRunMode::Continue
				|| State->LiveDomain.load()->Info().bCanStep)
				? State->LiveDomain.load()->Resume(Mode)
				: FDapContinueResult{false, false};
		}
		FDapContinueResult Result;
		if (!State->bStopped.exchange(false))
		{
			Result.bSuccess = false;
			return Result;
		}
		State->ClearStoppedHandles();
		State->Session.SetRunMode(Mode);
		return Result;
	}

	EDapStopReason VeroScriptDapAdapter::MapStopReason(const EDebugStopReason Reason)
	{
		switch (Reason)
		{
		case EDebugStopReason::Pause:
			return EDapStopReason::Pause;
		case EDebugStopReason::Step:
			{
				if (State->bPendingEntryStop)
				{
					State->bPendingEntryStop = false;
					return EDapStopReason::Entry;
				}
				return EDapStopReason::Step;
			}
		case EDebugStopReason::Entry:
			{
				State->bPendingEntryStop = false;
				return EDapStopReason::Entry;
			}
		case EDebugStopReason::Breakpoint:
		default:
			// A breakpoint stop consumes any pending entry stop.
			{
				State->bPendingEntryStop = false;
				return EDapStopReason::Breakpoint;
			}
		}
	}
}
