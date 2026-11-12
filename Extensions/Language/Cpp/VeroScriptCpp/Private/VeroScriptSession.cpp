// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSession.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptVmBackendLoader.h"
#include "VeroScriptModuleEnvironment.h"

#include "VeroScriptBuiltins.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptEngineInternal.h"
#include "VeroScriptHotReloadInternal.h"
#include "VeroScriptHost.h"
#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptModuleMetaInternal.h"
#include "VeroScriptLocale.h"
#include "VeroScriptPackage.h"
#include "VeroScriptPath.h"
#include "VeroScriptVm.h"
#include "VeroScriptVariantInternal.h"
#include "ExecutionSupport/VeroScriptExecutionCleanup.h"
#include "ExecutionSupport/VeroScriptNativeBindingCache.h"
#if defined(VEROSCRIPT_WITH_APPLICATION)
#include "VeroScriptApplication.h"
#include "VeroScriptCompile.h"
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <deque>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace VeroScript
{
	struct FSessionJitState
	{
		const FBytecodeModule* Module = nullptr;
		std::uint64_t Generation = 0;
		EExecutionBackend Backend = EExecutionBackend::Vm;
		FJitPolicy Policy;
		std::shared_ptr<FJitBackendCache> BackendCache;
		std::uint64_t GovernanceRequirements = 0;
		std::shared_ptr<const IJitBackendFactory> Factory;
		const IHostBindingCatalog* Bindings = nullptr;
		std::uint64_t BindingRevision = 0;
		FJitStats StatsAtRunStart;
		FSessionJitCacheStats Resources;
	};

	struct FSessionExecutionScratch
	{
		static bool FitsMethodBudget(const std::vector<FValue>& Stack,
		    const std::vector<VeroScriptVm::FCallFrame>& ActiveFrames,
		    const std::vector<VeroScriptVm::FCallFrame>& PooledFrames) noexcept
		{
			// A single deep host callback must not pin its high-water allocation
			// for the rest of the Session. Preserve the separate Run/free-call pool.
			return Stack.capacity() <= 512 && ActiveFrames.capacity() <= 128 &&
			    PooledFrames.capacity() <= 16 &&
			    std::all_of(PooledFrames.begin(), PooledFrames.end(), [](const auto& Frame)
			{
				return Frame.Locals.capacity() <= 128 && Frame.FinallyContinuations.capacity() <= 16;
			});
		}

		void LeaseTo(VeroScriptVm& Vm) noexcept
		{
			OperandStack.swap(Vm.OperandStack);
			Frames.swap(Vm.Frames);
			FreeFrames.swap(Vm.FreeFrames);
			// A scratch lease can cross a program/native-binding revision. Storage
			// reuse does not authorize reuse of a previous execution's lookup keys.
			for (auto& Frame : Vm.FreeFrames)
			{
				Frame.CacheModule = nullptr;
			}
		}

		void RecoverFrom(VeroScriptVm& Vm) noexcept
		{
			// ExecuteInternal's Finish path clears all live roots before dropping
			// ActiveModule. If that invariant is not true, leave every buffer with
			// the VM so its destructor remains the sole cleanup authority.
			if (Vm.ActiveModule != nullptr || !Vm.OperandStack.empty() || !Vm.Frames.empty())
			{
				return;
			}
			OperandStack.swap(Vm.OperandStack);
			Frames.swap(Vm.Frames);
			FreeFrames.swap(Vm.FreeFrames);
			if (Vm.NativeBindingCacheBytes != 0 && Vm.NativeBindingCacheBytes <= NativeBindingCache::MaxRetainedBytes)
			{
				SwapNativeBindings(Vm);
			}
		}
		void SwapNativeBindings(VeroScriptVm& Vm) noexcept
		{
			// Configure the idle VM's environment before leasing these snapshots:
			// changing environments must retire the VM's previous binding leases.
			StandardNativeStorage.swap(Vm.StandardNativeStorage);
			BoundNativeStorage.swap(Vm.BoundNativeStorage);
			NativeBindingSourceSnapshot.swap(Vm.NativeBindingSourceSnapshot);
			BoundNativeReferenceSnapshot.swap(Vm.BoundNativeReferenceSnapshot);
			std::swap(BoundNativeModule, Vm.BoundNativeModule);
			std::swap(BoundNativeSource, Vm.BoundNativeSource);
			std::swap(BoundNativeRevision, Vm.BoundNativeRevision);
			BoundNativeOwner.swap(Vm.BoundNativeOwner);
			std::swap(NativeBindingCacheBytes, Vm.NativeBindingCacheBytes);
		}
		void ClearNativeBindings() noexcept
		{
			std::vector<FNativeFunction>{}.swap(StandardNativeStorage);
			std::vector<FNativeFunction>{}.swap(BoundNativeStorage);
			std::vector<FNativeFunction>{}.swap(NativeBindingSourceSnapshot);
			std::vector<FBytecodeNativeReference>{}.swap(BoundNativeReferenceSnapshot);
			BoundNativeModule = nullptr;
			BoundNativeSource = nullptr;
			BoundNativeRevision = 0;
			BoundNativeOwner.reset();
			NativeBindingCacheBytes = 0;
		}

		FHostStreamBridge& GetCallBridge(IEngineHost& Host)
		{
			if (!CallBridge)
			{
				CallBridge = std::make_unique<FHostStreamBridge>(Host);
			}
			return *CallBridge;
		}

		std::vector<FValue> OperandStack;
		std::vector<VeroScriptVm::FCallFrame> Frames;
		std::vector<VeroScriptVm::FCallFrame> FreeFrames;
		std::vector<FNativeFunction> StandardNativeStorage, BoundNativeStorage, NativeBindingSourceSnapshot;
		std::vector<FBytecodeNativeReference> BoundNativeReferenceSnapshot;
		const FBytecodeModule* BoundNativeModule = nullptr;
		const std::vector<FNativeFunction>* BoundNativeSource = nullptr;
		std::uint64_t BoundNativeRevision = 0;
		std::shared_ptr<const void> BoundNativeOwner;
		std::size_t NativeBindingCacheBytes = 0;
		std::unique_ptr<FHostStreamBridge> CallBridge;
		ModuleMeta::FFreeFunctionLookupCache FreeFunctionLookup;
	};

	namespace
	{
		std::uint64_t AllocateRetainedCallableId() noexcept
		{
			// Session-local counters let a handle from another Session accidentally
			// resolve to an unrelated callable with the same generation and ID.
			static std::atomic<std::uint64_t> NextId{1};
			auto Id = NextId.load(std::memory_order_relaxed);
			do
			{
				if (Id == 0) return 0; // Exhaustion never reuses a live identity.
			}
			while (!NextId.compare_exchange_weak(Id, Id + 1, std::memory_order_relaxed));
			return Id;
		}

		struct FSessionCallbackGuard
		{
			explicit FSessionCallbackGuard(bool& InFlag) noexcept
			    : Flag(InFlag), Previous(InFlag)
			{
				Flag = true;
			}
			~FSessionCallbackGuard() noexcept
			{
				Flag = Previous;
			}
			bool& Flag;
			bool Previous;
		};

		// Inputs may borrow the old output's storage (including argument slots and
		// string_view names). Publish only after the VM has consumed those inputs.
		template <typename T>
		class FSessionOutputScope final
		{
		public:
			FSessionOutputScope(T* InOutput, bool& InRunning) noexcept
			    : Output(InOutput), Running(InRunning)
			{
			}
			~FSessionOutputScope() noexcept
			{
				if (Output)
				{
					const FSessionCallbackGuard CleanupGuard(Running);
					*Output = std::move(Value);
				}
			}
			FSessionOutputScope(const FSessionOutputScope&) = delete;
			FSessionOutputScope& operator=(const FSessionOutputScope&) = delete;
			T* Get() noexcept
			{
				return Output ? &Value : nullptr;
			}
			T Value{};

		private:
			T* Output;
			bool& Running;
		};

		// Declared before the VM so unwinding destroys the VM before dropping the
		// debugger. Finish also clears a debugger left attached by a thrown native.
		class FSessionDebugExecutionScope final
		{
		public:
			FSessionDebugExecutionScope(
			    std::shared_ptr<ISessionDebugBridge> InBridge,
			    const FSessionDebugProgramView& Program,
			    const ESessionDebugExecutionKind Kind,
			    const std::string_view EntryName,
			    const FObjectHandle& Object,
			    const std::shared_ptr<FExecutionControl>& Control,
			    const ERunMode RunMode = ERunMode::Script) noexcept
			    : Bridge(std::move(InBridge))
			{
				if (Bridge)
				{
					Debugger = Bridge->BeginExecution({Program, Kind, EntryName, Object, std::this_thread::get_id(), Control, RunMode});
				}
			}
			~FSessionDebugExecutionScope() noexcept
			{
				Finish();
			}
			FSessionDebugExecutionScope(const FSessionDebugExecutionScope&) = delete;
			FSessionDebugExecutionScope& operator=(const FSessionDebugExecutionScope&) = delete;
			VeroScriptDebugger* GetDebugger() const noexcept
			{
				return Debugger.get();
			}
			void Finish() noexcept
			{
				if (Bridge)
				{
					if (Debugger)
					{
						Debugger->DetachVm();
					}
					Bridge->EndExecution();
					Bridge.reset();
				}
				// Keep the debugger alive until the VM itself leaves scope, even after
				// explicit Finish: the VM still holds a non-owning debugger pointer.
			}

		private:
			std::shared_ptr<ISessionDebugBridge> Bridge;
			std::shared_ptr<VeroScriptDebugger> Debugger;
		};

		struct FReusableHostBridgeScope
		{
			explicit FReusableHostBridgeScope(FHostStreamBridge& InBridge) noexcept
			    : Bridge(InBridge)
			{
				Bridge.ResetForReuse();
			}

			~FReusableHostBridgeScope() noexcept
			{
				Bridge.ResetForReuse();
			}

			FHostStreamBridge& Bridge;
		};

		// Must run before Vm and its leased cache are destroyed. Normal completion
		// disarms this guard after accounting; native C++ unwinding still consumes
		// compilation and clears gauges without retaining a partially used cache.
		struct FSessionJitExceptionGuard
		{
			~FSessionJitExceptionGuard() noexcept
			{
				if (!State)
				{
					return;
				}
				LastStats = Vm.GetJitStats();
				State->Resources.CompileAttempts += LastStats.CompileAttempts;
				const FJitResourceUsage Usage = Vm.GetJitResourceUsage();
				State->Resources.bResourceLimitReached |= Usage.bResourceLimitReached || Usage.ExecutableBytes > Limits.MaxNativeBytes || Usage.RetainedBytes > Limits.MaxRetainedPayloadBytes;
				State->Resources.RetainedPayloadBytes = 0;
				State->Resources.NativeBytes = 0;
			}

			FSessionJitState* State;
			const VeroScriptVm& Vm;
			FJitStats& LastStats;
			const FSessionJitCacheLimits& Limits;
		};

		bool PackageKindMatchesModule(
		    const EPackageKind Kind,
		    const FBytecodeModule& Module)
		{
			switch (Kind)
			{
			case EPackageKind::Application:
				return Module.MainFunctionIndex >= 0;
			case EPackageKind::Library:
				return Module.MainFunctionIndex < 0 && Module.PackageInterfaceError.empty();
			default:
				{
					break;
				}
			}
			return false;
		}

		const FExternalSerializerDesc* FindExternalSerializer(
		    const std::unordered_map<std::string, FExternalSerializerDesc>& Serializers,
		    const FBytecodeModule& Module,
		    const std::string& RuntimeTypeName)
		{
			if (const auto Exact = Serializers.find(RuntimeTypeName);
			    Exact != Serializers.end())
			{
				return &Exact->second;
			}
			std::uint16_t RuntimeTypeIndex = static_cast<std::uint16_t>(-1);
			if (!ModuleMeta::FindTypeIndex(Module, RuntimeTypeName, RuntimeTypeIndex))
			{
				return nullptr;
			}
			for (const auto& Entry : Serializers)
			{
				std::uint16_t RegisteredTypeIndex = static_cast<std::uint16_t>(-1);
				if (ModuleMeta::FindTypeIndex(
				        Module, Entry.first, RegisteredTypeIndex) &&
				    RegisteredTypeIndex == RuntimeTypeIndex)
				{
					return &Entry.second;
				}
			}
			return nullptr;
		}
	}

	VeroScriptSession::VeroScriptSession(IEngineHost& InHost)
	    : Host(InHost), MemoryContext(std::make_shared<FMemoryContext>())
	{
		Locale::InitializeModuleFromEnvironment("Extensions/Language/Cpp/VeroScriptCpp");
	}

	VeroScriptSession::~VeroScriptSession() noexcept
	{
		// A staged replacement has not committed its initialization. Restore its
		// defining metadata before releasing callables from the previous program.
		try
		{
			RestorePendingHotReload();
		}
		catch (...)
		{
		}
		const bool bAbandonReload = PendingHotReloadRollback != nullptr;
		std::vector<FValue> AbandonedRoots;
		try
		{
			std::string Error;
			if (!ValueRoots->Close([this, &Error, bAbandonReload, &AbandonedRoots](auto& Values, const auto& Keep)
			{
				if (bAbandonReload)
				{
					AbandonedRoots = std::move(Values);
					return true;
				}
				return CleanValueRoots(Values, Keep, &Error);
			}, Error))
			{
				FScriptError Failure;
				Failure.Message = Error;
				Failure.Location.FileLabel = "<value-cleanup>";
				FHostStreamBridge Bridge(Host);
				EngineInternal::WriteError(Bridge.ErrorStream(), Failure);
			}
		}
		catch (...)
		{
		}
		const FSessionCallbackGuard CallbackGuard(bRunning);
		const FSessionCallbackGuard CleanupGuard(bCleaningUp);
		if (auto Bridge = std::move(DebugBridge))
		{
			Bridge->Detached();
		}
		// Destruction must not terminate the host if cleanup encounters resource
		// exhaustion or an exceptional host callback. Keep the phases isolated so
		// one failure cannot prevent the remaining owned state from being released.
		try
		{
			if (bAbandonReload)
			{
				DiscardUnsettledHotReloadState(AbandonedRoots);
			}
			else
			{
				DiscardRetainedCallables(Module.get());
			}
		}
		catch (...)
		{
			// Member destruction below still releases the session's direct ownership.
		}
		if (!bAbandonReload)
		{
			try
			{
				DiscardHotReloadState();
			}
			catch (...)
			{
				// HotReloadState and Module members remain RAII-owned.
			}
		}
		MemoryContext->Close();
	}

	bool VeroScriptSession::ApplyConfig(const FEngineConfig& Input, std::string* OutError)
	{
		FRunResult Rejection;
		if (RejectIfRunning("ApplyConfig", &Rejection))
		{
			if (OutError)
			{
				*OutError = std::move(Rejection.Error.Message);
			}
			return false;
		}
		const auto Reject = [&](const std::string_view Error)
		{
			FHostStreamBridge Bridge(Host);
			Bridge.ErrorStream() << "VeroScript: " << Error << '\n';
			if (OutError)
			{
				OutError->assign(Error);
			}
			return false;
		};
		FEngineConfig Resolved;
		const FEngineConfig* Effective = &Input;
		const bool bHostDisablesJit = Input.bDisableJitBackend || (!Input.JitBackendFactory && bDisableJitBackend);
		if (Input.FallbackVmBackendFactory || !Input.FallbackVmBackendPath.empty() || (Input.ModuleEnvironment && (Input.ModuleEnvironment->GetDescription().ExecutionBackends.Vm || Input.ModuleEnvironment->GetDescription().ExecutionBackends.Jit)))
		{
			const FSessionCallbackGuard SelectionGuard(bRunning);
			auto Requested = Input;
			Requested.bDisableJitBackend = bHostDisablesJit;
			if (!Requested.VmBackendFactory && Requested.VmBackendPath.empty() && !Requested.bClearVmBackend)
			{
				Requested.bClearVmBackend = bDisableEnvironmentVm;
				if (!Requested.FallbackVmBackendFactory && Requested.FallbackVmBackendPath.empty())
				{
					Requested.FallbackVmBackendFactory = VmFallbackFactory;
					Requested.FallbackVmBackendPath = VmFallbackFactory ? std::string{} : VmFallbackPath;
				}
				if (bVmBackendFallback && !VmExecutionDomain->IsFrozen())
				{
					if (!Requested.FallbackVmBackendFactory && Requested.FallbackVmBackendPath.empty())
					{
						Requested.FallbackVmBackendFactory = VmExecutionDomain->Factory;
					}
				}
				else
				{
					Requested.VmBackendFactory = VmExecutionDomain->Factory;
				}
			}
			std::string SelectionError;
			if (!Requested.JitBackendFactory && !Requested.bDisableJitBackend && !JitBackendSelectionRequest.empty() && Requested.ExecutionBackend != EExecutionBackend::Vm && Requested.ModuleEnvironment && Requested.ModuleEnvironment->GetDescription().ExecutionBackends.Jit)
			{
				if (!Requested.ModuleEnvironment->GetDescription().ExecutionBackends.Jit(
				        JitBackendSelectionRequest, Requested.JitBackendFactory, SelectionError))
				{
					return Reject(SelectionError);
				}
				Requested.bDisableJitBackend = !Requested.JitBackendFactory;
			}
			if (!ResolveExecutionBackends(Requested, Resolved, SelectionError))
			{
				return Reject(SelectionError);
			}
			Effective = &Resolved;
		}
		const auto& Config = *Effective;
		if ((Config.VmBackendFactory && !Config.VmBackendPath.empty()) || (Config.bClearVmBackend && (Config.VmBackendFactory || !Config.VmBackendPath.empty())))
		{
			return Reject("VM factory, path and clear settings are exclusive");
		}
		// Identity normalization may collapse an embedded NUL to an existing file.
		// Reject it before the same-selection shortcut can bypass loader validation.
		if (Config.VmBackendPath.find('\0') != std::string::npos)
		{
			return Reject("VM backend path cannot contain NUL characters");
		}
		const std::string RequestedVmPath = Config.VmBackendPath.empty()
		    ? std::string{}
		    : PathUtil::IdentityKey(std::string(Config.VmBackendPath));
		const bool SameVmPath = !RequestedVmPath.empty() && VmExecutionDomain->Factory && (RequestedVmPath == VmBackendSelectionPath || RequestedVmPath == PathUtil::IdentityKey(VmExecutionDomain->LibraryPath));
		const bool UpdateVm = (Config.bClearVmBackend && VmExecutionDomain->Factory) || (!Config.VmBackendPath.empty() && !SameVmPath) || (Config.VmBackendFactory && Config.VmBackendFactory != VmExecutionDomain->Factory);
		std::shared_ptr<const IVmBackendFactory> NextVmFactory;
		std::string NextVmPath;
		if (UpdateVm)
		{
			if (VmExecutionDomain->IsFrozen())
			{
				return Reject("VM backend selection is fixed after execution begins");
			}
			FHostStreamBridge Bridge(Host);
			VeroScriptVm Candidate(Bridge.OutputStream(), InputStreamOrCin(), Bridge.ErrorStream());
			std::string Failure;
			const bool Ok = Config.VmBackendFactory
			    ? Candidate.SetVmBackendFactory(Config.VmBackendFactory, Failure)
			    : Candidate.SetVmBackendPath(Config.VmBackendPath, Failure);
			if (!Ok)
			{
				return Reject(Failure);
			}
			NextVmFactory = Candidate.GetVmBackendFactory();
			NextVmPath = Candidate.GetVmExecutionDomain()->GetLibraryPath();
		}
		if (Module && ModuleEnvironment != Config.ModuleEnvironment)
		{
			return Reject("Clear the loaded program before changing its module environment");
		}
		const auto* ConfiguredCatalog = Config.ModuleEnvironment
		    ? Config.ModuleEnvironment->GetDescription().Bindings.get()
		    : nullptr;
		if (ConfiguredCatalog && ConfiguredCatalog != &Bindings && (!Bindings.GetNativeFunctions().empty() || !Bindings.GetFunctions().empty() || !Bindings.GetTypes().empty() || !Bindings.GetTypeAliases().empty() || !Bindings.GetGenericTypeTemplates().empty() || !Bindings.GetAnnotations().empty()))
		{
			return Reject("module environment conflicts with existing Session bindings; prepare a combined catalog before ApplyConfig");
		}
		FNumericLimits NormalizedNumericLimits;
		FNumericError NumericError;
		if (!Numerics::ValidateLimits(Config.NumericLimits, NormalizedNumericLimits, NumericError))
		{
			ReportNativeError(NumericError.MessageId);
			if (OutError)
			{
				*OutError = MakeDiagnostic(NumericError.MessageId).Message;
			}
			return false;
		}
		// Applying a configuration is an explicit execution-context boundary.
		if (const auto Result = MemoryContext->SetLimits(Config.MemoryLimits); !Result)
		{
			ReportNativeError(Result.MessageId);
			if (OutError)
			{
				*OutError = MakeDiagnostic(Result.MessageId).Message;
			}
			return false;
		}
		// In particular, the backend and governance fields below may make the
		if (UpdateVm)
		{
			VmExecutionDomain->IdleInstance.reset();
			VmExecutionDomain->Factory = std::move(NextVmFactory);
			VmExecutionDomain->LibraryPath = std::move(NextVmPath);
			VmBackendSelectionPath = RequestedVmPath;
		}
		// current cache ineligible before another call has a chance to settle it.
		ReusableJit.reset();
		ImportPaths.clear();
		ImportPaths.reserve(Config.ImportPaths.size());
		for (const std::string_view Path : Config.ImportPaths)
		{
			ImportPaths.emplace_back(Path);
		}
		ImplicitImportPaths.clear();
		ImplicitImportPaths.reserve(Config.ImplicitImportPaths.size());
		for (const std::string_view Path : Config.ImplicitImportPaths)
		{
			ImplicitImportPaths.emplace_back(Path);
		}
		bAllowCircularImports = Config.bAllowCircularImports;
		bAllowProjectWideAccess = Config.bAllowProjectWideAccess;

		ScriptArgs.clear();
		ScriptArgs.reserve(Config.ScriptArgs.size());
		for (const std::string_view Arg : Config.ScriptArgs)
		{
			ScriptArgs.emplace_back(Arg);
		}

		Defines.clear();
		Defines.reserve(Config.Defines.size());
		for (const std::string_view Define : Config.Defines)
		{
			if (!Define.empty())
			{
				Defines.emplace_back(Define);
			}
		}
		PredefinedMacros = Config.PredefinedMacros;

		bUseBytecodeCache = Config.bUseBytecodeCache;
		ModuleEnvironment = Config.ModuleEnvironment;
		AnnotationProcessing = Config.AnnotationProcessing;
		CompileProcessing = Config.CompileProcessing;
		ScriptCompileExtensions = Config.ScriptCompileExtensions;
		BytecodeCacheDirectory.assign(Config.BytecodeCacheDirectory);
		ExecutionBackend = Config.ExecutionBackend;
		JitBackendFactory = Config.bDisableJitBackend ? nullptr : Config.JitBackendFactory;
		bDisableJitBackend = bHostDisablesJit;
		if (Input.JitBackendFactory || Input.bDisableJitBackend)
		{
			JitBackendSelectionRequest.clear();
		}
		if (Config.bClearVmBackend || Config.VmBackendFactory || !Config.VmBackendPath.empty())
		{
			bDisableEnvironmentVm = Input.bClearVmBackend || (!Input.VmBackendFactory && Input.VmBackendPath.empty() && bDisableEnvironmentVm);
			bVmBackendFallback = !Input.VmBackendFactory && Input.VmBackendPath.empty() && !Input.bClearVmBackend && ((Config.VmBackendFactory && Config.VmBackendFactory == Config.FallbackVmBackendFactory) || (!Config.VmBackendPath.empty() && Config.VmBackendPath == Config.FallbackVmBackendPath));
		}
		if (Input.FallbackVmBackendFactory || !Input.FallbackVmBackendPath.empty())
		{
			VmFallbackFactory = Input.FallbackVmBackendFactory;
			VmFallbackPath = Input.FallbackVmBackendPath;
			if (bVmBackendFallback)
			{
				VmFallbackFactory = VmExecutionDomain->Factory;
			}
		}
		BackendDefaultsEnvironment = Effective == &Resolved ? Config.ModuleEnvironment : nullptr;
		BackendDefaultsMode = Config.ExecutionBackend;
		AotProgram = Config.AotProgram;
		MaxCallDepth = Config.MaxCallDepth;
		ExecutionLimits = Config.ExecutionLimits;
		std::atomic_store(&ExecutionControl, Config.ExecutionControl);
		Capabilities = Config.Capabilities;
		I18nServices = Config.I18nServices;
		I18nLimits = Config.I18nLimits;
		StandardLibraryLimits = Config.StandardLibraryLimits;
		NumericLimits = NormalizedNumericLimits;
		StdlibSearchPaths.clear();
		StdlibSearchPaths.reserve(Config.StdlibSearchPaths.size());
		for (const std::string_view Path : Config.StdlibSearchPaths)
		{
			StdlibSearchPaths.emplace_back(Path);
		}
		SourceOverlays = Config.SourceOverlays;
		HostInputStream = Config.InputStream;
		// Non-null host stream replaces any previously owned session input.
		if (Config.InputStream != nullptr)
		{
			OwnedInputStream.reset();
		}
		if (OutError)
		{
			OutError->clear();
		}
		return true;
	}

	FMemoryResult VeroScriptSession::SetMemoryLimits(const FMemoryLimits Limits)
	{
		if (RejectIfRunning("SetMemoryLimits"))
		{
			return {EMemoryStatus::Busy, "diag.memory_busy"};
		}
		return MemoryContext->SetLimits(Limits);
	}

	FMemoryResult VeroScriptSession::SetStandardLibraryLimits(FStandardLibraryLimits Limits)
	{
		if (RejectIfRunning("SetStandardLibraryLimits")) return {EMemoryStatus::Busy, "diag.memory_busy"};
		StandardLibraryLimits = Limits;
		return {};
	}

	bool VeroScriptSession::SetNumericLimits(FNumericLimits Limits, FNumericError& Error)
	{
		if (RejectIfRunning("SetNumericLimits"))
		{
			Error = {ENumericStatus::InvalidArgument, "diag.numeric_busy", "active session"};
			return false;
		}
		FNumericLimits Normalized;
		if (!Numerics::ValidateLimits(Limits, Normalized, Error)) return false;
		NumericLimits = Normalized;
		return true;
	}

	void VeroScriptSession::SetInstanceCallInterceptor(
	    FInstanceCallInterceptor Interceptor)
	{
		if (RejectIfRunning("SetInstanceCallInterceptor"))
		{
			return;
		}
		InstanceCallInterceptor = std::move(Interceptor);
		ReusableJit.reset();
	}

	void VeroScriptSession::SetMutationCallback(FMutationCallback Callback)
	{
		if (RejectIfRunning("SetMutationCallback"))
		{
			return;
		}
		MutationCallback = std::move(Callback);
		ReusableJit.reset();
	}

	void VeroScriptSession::SetOwnedInputStream(std::unique_ptr<std::istream> Stream)
	{
		if (RejectIfRunning("SetOwnedInputStream"))
		{
			return;
		}
		OwnedInputStream = std::move(Stream);
	}

	void VeroScriptSession::SetInputFromString(std::string Content)
	{
		SetOwnedInputStream(std::make_unique<std::istringstream>(std::move(Content)));
	}

	void VeroScriptSession::SetUseBytecodeCache(const bool bEnable)
	{
		if (RejectIfRunning("SetUseBytecodeCache"))
		{
			return;
		}
		bUseBytecodeCache = bEnable;
	}

	void VeroScriptSession::SetAnnotationProcessing(FAnnotationProcessingOptions Options)
	{
		if (RejectIfRunning("SetAnnotationProcessing")) return;
		AnnotationProcessing = std::move(Options);
	}

	void VeroScriptSession::SetCompileProcessing(FCompileProcessingOptions Options)
	{
		if (RejectIfRunning("SetCompileProcessing")) return;
		CompileProcessing = std::move(Options);
	}

	void VeroScriptSession::SetScriptCompileExtensions(FScriptCompileExtensionsOptions Options)
	{
		if (RejectIfRunning("SetScriptCompileExtensions")) return;
		ScriptCompileExtensions = std::move(Options);
	}

	void VeroScriptSession::SetHotReloadEnabled(const bool bEnable)
	{
		if (bHotReloadEnabled == bEnable || RejectIfRunning("SetHotReloadEnabled"))
		{
			return;
		}
		if (!bEnable)
		{
			RestorePendingHotReload();
			if (PendingHotReloadRollback) return;
			DiscardHotReloadState();
		}
		bHotReloadEnabled = bEnable;
		ReusableJit.reset();
		if (bEnable && !HotReloadState)
		{
			HotReloadState = std::make_unique<FHotReloadState>();
		}
		if (bEnable)
		{
			bTemporaryHotReloadState = false;
		}
	}

	void VeroScriptSession::SetExecutionBackend(const EExecutionBackend Backend)
	{
		if (RejectIfRunning("SetExecutionBackend"))
		{
			return;
		}
		if (ExecutionBackend == Backend)
		{
			return;
		}
		ExecutionBackend = Backend;
		// Backend identity is part of the reusable-code context. Release it at
		// the configuration boundary so memory and settled gauges cannot describe
		// a context that is no longer selectable, even before the next call.
		ReusableJit.reset();
	}

	void VeroScriptSession::SetJitBackendFactory(std::shared_ptr<const IJitBackendFactory> Factory)
	{
		if (RejectIfRunning("SetJitBackendFactory"))
		{
			return;
		}
		ReusableJit.reset();
		JitBackendFactory = std::move(Factory);
		bDisableJitBackend = !JitBackendFactory;
		JitBackendSelectionRequest.clear();
		BackendDefaultsEnvironment.reset();
	}

	bool VeroScriptSession::SetVmBackendFactory(std::shared_ptr<const IVmBackendFactory> Factory, std::string& Error)
	{
		if (RejectIfRunning("SetVmBackendFactory"))
		{
			Error = "Session is executing";
			return false;
		}
		FHostStreamBridge Bridge(Host);
		VeroScriptVm Vm(Bridge.OutputStream(), InputStreamOrCin(), Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
		Vm.VmExecutionDomain = VmExecutionDomain;
		if (!Vm.SetVmBackendFactory(std::move(Factory), Error)) return false;
		VmBackendSelectionPath.clear();
		bDisableEnvironmentVm = !VmExecutionDomain->Factory;
		bVmBackendFallback = false;
		BackendDefaultsEnvironment.reset();
		return true;
	}

	bool VeroScriptSession::SetFallbackVmBackendFactory(std::shared_ptr<const IVmBackendFactory> Factory, std::string& Error)
	{
		if (RejectIfRunning("SetFallbackVmBackendFactory"))
		{
			Error = "Session is executing";
			return false;
		}
		if (bDisableEnvironmentVm || (VmExecutionDomain->Factory && !bVmBackendFallback))
		{
			VmFallbackFactory = std::move(Factory);
			VmFallbackPath.clear();
			Error.clear();
			return true;
		}
		FEngineConfig Request, Selected;
		Request.ModuleEnvironment = ModuleEnvironment;
		Request.ExecutionBackend = EExecutionBackend::Vm;
		Request.FallbackVmBackendFactory = Factory;
		{
			const FSessionCallbackGuard SelectionGuard(bRunning);
			if (!ResolveExecutionBackends(Request, Selected, Error)) return false;
		}
		if (!SetVmBackendFactory(Selected.VmBackendFactory, Error))
		{
			return false;
		}
		bVmBackendFallback = Selected.VmBackendFactory && Selected.VmBackendFactory == Factory;
		bDisableEnvironmentVm = false;
		VmFallbackFactory = std::move(Factory);
		VmFallbackPath.clear();
		return true;
	}

	bool VeroScriptSession::SetVmBackendPath(std::string_view Path, std::string& Error)
	{
		if (RejectIfRunning("SetVmBackendPath"))
		{
			Error = "Session is executing";
			return false;
		}
		if (Path == DisabledBackend)
		{
			return SetVmBackendFactory({}, Error);
		}
		if (Path == AutomaticBackend || (ModuleEnvironment && ModuleEnvironment->GetDescription().ExecutionBackends.Vm && !Path.empty() && Path.find_first_of("/\\.") == std::string_view::npos))
		{
			const std::string Request(Path);
			std::shared_ptr<const IVmBackendFactory> Factory;
			if (ModuleEnvironment && ModuleEnvironment->GetDescription().ExecutionBackends.Vm)
			{
				const FSessionCallbackGuard SelectionGuard(bRunning);
				if (!ModuleEnvironment->GetDescription().ExecutionBackends.Vm(Request, Factory, Error)) return false;
			}
			const bool bFallback = Request == AutomaticBackend && !Factory;
			if (bFallback)
			{
				Factory = VmFallbackFactory;
				if (!Factory && !VmFallbackPath.empty())
				{
					Factory = LoadVmBackend(VmFallbackPath, Error);
					if (!Factory) return false;
				}
			}
			if (!Factory)
			{
				Error = "no usable backend for service: vm";
				return false;
			}
			if (!SetVmBackendFactory(std::move(Factory), Error))
			{
				return false;
			}
			bVmBackendFallback = bFallback;
			if (bFallback)
			{
				VmFallbackFactory = VmExecutionDomain->Factory;
			}
			return true;
		}
		// The input may borrow the error buffer. Resolve its identity before publishing errors.
		const std::string SelectionPath(Path);
		const std::string SelectionKey = SelectionPath.empty() ? std::string{} : PathUtil::IdentityKey(SelectionPath);
		FHostStreamBridge Bridge(Host);
		VeroScriptVm Vm(Bridge.OutputStream(), InputStreamOrCin(), Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
		Vm.VmExecutionDomain = VmExecutionDomain;
		if (!Vm.SetVmBackendPath(SelectionPath, Error)) return false;
		VmBackendSelectionPath = SelectionKey;
		bDisableEnvironmentVm = SelectionPath.empty();
		bVmBackendFallback = false;
		BackendDefaultsEnvironment.reset();
		return true;
	}

	void VeroScriptSession::SetAotProgram(std::shared_ptr<const IAotProgram> Program)
	{
		if (RejectIfRunning("SetAotProgram")) return;
		AotProgram = std::move(Program);
	}

	bool VeroScriptSession::SetJitBackendPath(const std::string_view Path, std::string& OutError)
	{
		if (RejectIfRunning("SetJitBackendPath"))
		{
			OutError = "cannot change JIT backend while the session is executing";
			return false;
		}
		if (Path == AutomaticBackend || (ModuleEnvironment && ModuleEnvironment->GetDescription().ExecutionBackends.Jit && Path != DisabledBackend && !Path.empty() && Path.find_first_of("/\\.") == std::string_view::npos))
		{
			const std::string Request(Path);
			std::shared_ptr<const IJitBackendFactory> Factory;
			if (ModuleEnvironment && ModuleEnvironment->GetDescription().ExecutionBackends.Jit)
			{
				const FSessionCallbackGuard SelectionGuard(bRunning);
				if (!ModuleEnvironment->GetDescription().ExecutionBackends.Jit(Request, Factory, OutError)) return false;
			}
			OutError.clear();
			SetJitBackendFactory(std::move(Factory));
			bDisableJitBackend = false;
			JitBackendSelectionRequest = Request;
			return true;
		}
		// Path may borrow OutError's storage. Publish the error only after loading
		// has consumed that view, and retain whether the caller requested disable.
		const bool bDisable = Path.empty() || Path == DisabledBackend;
		std::string Error;
		auto Factory = bDisable ? std::shared_ptr<const IJitBackendFactory>{} : LoadJitBackend(Path, Error);
		OutError = std::move(Error);
		if (!bDisable && !Factory) return false;
		SetJitBackendFactory(std::move(Factory));
		return true;
	}

	void VeroScriptSession::SetMaxCallDepth(const std::size_t Limit)
	{
		if (RejectIfRunning("SetMaxCallDepth"))
		{
			return;
		}
		MaxCallDepth = Limit;
	}

	void VeroScriptSession::SetExecutionLimits(const FExecutionLimits Limits)
	{
		if (RejectIfRunning("SetExecutionLimits"))
		{
			return;
		}
		ExecutionLimits = Limits;
		ReusableJit.reset();
	}

	void VeroScriptSession::SetExecutionControl(std::shared_ptr<FExecutionControl> Control)
	{
		if (RejectIfRunning("SetExecutionControl"))
		{
			return;
		}
		std::atomic_store(&ExecutionControl, std::move(Control));
		ReusableJit.reset();
	}

	void VeroScriptSession::RequestCancel() const noexcept
	{
		const auto Control = std::atomic_load(&ExecutionControl);
		if (Control)
		{
			Control->RequestCancel();
		}
	}

	bool VeroScriptSession::SetDebugBridge(std::shared_ptr<ISessionDebugBridge> Bridge)
	{
		if (RejectIfRunning("SetDebugBridge")) return false;
		if (Bridge == DebugBridge) return true;
		const FSessionCallbackGuard CallbackGuard(bRunning);
		if (auto Previous = std::move(DebugBridge))
		{
			Previous->Detached();
		}
		ReusableJit.reset();
		DebugBridge = std::move(Bridge);
		if (DebugBridge)
		{
			DebugBridge->ProgramChanged(GetDebugProgram());
		}
		return true;
	}

	FSessionDebugProgramView VeroScriptSession::GetDebugProgram() const noexcept
	{
		return {Module.get(), &EffectiveBindings(), &SourceOverlays,
		    FilePath, ProgramGeneration, ProgramOwner, ModuleEnvironment.get()};
	}

	void VeroScriptSession::SetCapabilities(const FScriptCapabilities InCapabilities)
	{
		if (RejectIfRunning("SetCapabilities"))
		{
			return;
		}
		Capabilities = InCapabilities;
		ReusableJit.reset();
	}

	void VeroScriptSession::SetJitPolicy(const FJitPolicy Policy)
	{
		if (RejectIfRunning("SetJitPolicy"))
		{
			return;
		}
		JitPolicy = Policy;
		ReusableJit.reset();
	}

	void VeroScriptSession::SetJitCacheLimits(const FSessionJitCacheLimits Limits)
	{
		if (RejectIfRunning("SetJitCacheLimits"))
		{
			return;
		}
		JitCacheLimits = Limits;
		ReusableJit.reset();
	}

	FSessionJitCacheStats VeroScriptSession::GetJitCacheStats() const
	{
		return ReusableJit ? ReusableJit->Resources : FSessionJitCacheStats{};
	}

	bool VeroScriptSession::RegisterNative(
	    std::string Name,
	    const ENativeType ReturnType,
	    std::vector<ENativeType> ParamTypes,
	    const FNativeCallback Callback,
	    void* const UserData,
	    std::string ReturnHostTypeName,
	    std::vector<std::string> ParamHostTypeNames,
	    std::string ReturnElementTypeName,
	    std::vector<std::string> ParamElementTypeNames,
	    std::vector<std::uint8_t> ParamFlags,
	    std::string ReturnKeyTypeName,
	    std::string ReturnValueTypeName,
	    std::vector<std::string> ParamKeyTypeNames,
	    std::vector<std::string> ParamValueTypeNames)
	{
		if (RejectBindingMutation("RegisterNative"))
		{
			return false;
		}
		if (Builtins::Lookup(Name))
		{
			ReportNativeError("diag.registernative_name_conflicts_with_builtin", {Name});
			return false;
		}

		std::string ErrorId;
		const std::string NameForArgs = Name;
		if (!Bindings.RegisterNative(
		        std::move(Name),
		        ReturnType,
		        std::move(ParamTypes),
		        Callback,
		        UserData,
		        ErrorId,
		        std::move(ReturnHostTypeName),
		        std::move(ParamHostTypeNames),
		        std::move(ReturnElementTypeName),
		        std::move(ParamElementTypeNames),
		        std::move(ParamFlags),
		        std::move(ReturnKeyTypeName),
		        std::move(ReturnValueTypeName),
		        std::move(ParamKeyTypeNames),
		        std::move(ParamValueTypeNames)))
		{
			if (ErrorId == "diag.registernative_requires_a_non_empty_name" || ErrorId == "diag.registernative_table_is_full")
			{
				ReportNativeError(ErrorId);
			}
			else
			{
				ReportNativeError(ErrorId, {NameForArgs});
			}
			return false;
		}
		ReusableJit.reset();
		return true;
	}

	bool VeroScriptSession::RegisterNative(
	    std::string Name,
	    FNativeTypeDesc ReturnType,
	    std::vector<FNativeParamDesc> Params,
	    const FNativeCallback Callback,
	    void* const UserData)
	{
		if (RejectBindingMutation("RegisterNative"))
		{
			return false;
		}
		if (Builtins::Lookup(Name))
		{
			ReportNativeError("diag.registernative_name_conflicts_with_builtin", {Name});
			return false;
		}

		std::string ErrorId;
		const std::string NameForArgs = Name;
		if (!Bindings.RegisterNative(
		        std::move(Name),
		        std::move(ReturnType),
		        std::move(Params),
		        Callback,
		        UserData,
		        ErrorId))
		{
			if (ErrorId == "diag.registernative_requires_a_non_empty_name" || ErrorId == "diag.registernative_table_is_full")
			{
				ReportNativeError(ErrorId);
			}
			else
			{
				ReportNativeError(ErrorId, {NameForArgs});
			}
			return false;
		}
		ReusableJit.reset();
		return true;
	}

	bool VeroScriptSession::RegisterHostType(
	    std::string Name,
	    const EHostTypeKind Kind,
	    FHostTypeId* const OutTypeId)
	{
		if (RejectBindingMutation("RegisterHostType"))
		{
			return false;
		}
		FHostTypeId TypeId;
		std::string ErrorId;
		const std::string NameForArgs = Name;
		if (!Bindings.RegisterType(std::move(Name), Kind, TypeId, ErrorId))
		{
			if (ErrorId == "diag.hostbinding_type_requires_a_non_empty_name" || ErrorId == "diag.hostbinding_type_table_is_full")
			{
				ReportNativeError(ErrorId);
			}
			else
			{
				ReportNativeError(ErrorId, {NameForArgs});
			}
			return false;
		}
		if (OutTypeId)
		{
			*OutTypeId = TypeId;
		}
		return true;
	}

	bool VeroScriptSession::RegisterGenericHostTypeTemplate(
	    std::string Name,
	    const std::size_t Arity,
	    const FGenericHostTypeInstantiateCallback Instantiate,
	    void* const UserData,
	    FGenericHostTypeTemplateId* const OutTemplateId,
	    std::string Documentation)
	{
		if (RejectBindingMutation("RegisterGenericHostTypeTemplate"))
		{
			return false;
		}
		FGenericHostTypeTemplateId TemplateId;
		std::string ErrorId;
		if (!Bindings.RegisterGenericHostTypeTemplate(
		        std::move(Name),
		        Arity,
		        Instantiate,
		        UserData,
		        TemplateId,
		        ErrorId,
		        std::move(Documentation)))
		{
			ReportNativeError(ErrorId);
			return false;
		}
		if (OutTemplateId)
		{
			*OutTemplateId = TemplateId;
		}
		return true;
	}

	bool VeroScriptSession::InstantiateGenericHostType(
	    const FGenericHostTypeTemplateId TemplateId,
	    std::vector<std::string> TypeArguments,
	    FHostTypeId* const OutTypeId)
	{
		if (RejectBindingMutation("InstantiateGenericHostType"))
		{
			return false;
		}
		FHostTypeId TypeId;
		std::string ErrorId;
		if (!Bindings.InstantiateGenericHostType(
		        TemplateId, std::move(TypeArguments), TypeId, ErrorId))
		{
			ReportNativeError(ErrorId);
			return false;
		}
		if (OutTypeId)
		{
			*OutTypeId = TypeId;
		}
		return true;
	}

	bool VeroScriptSession::RegisterHostMember(
	    const FHostTypeId TypeId,
	    FHostMemberDesc Member)
	{
		if (RejectBindingMutation("RegisterHostMember"))
		{
			return false;
		}
		std::string ErrorId;
		const std::string NameForArgs = Member.Name;
		if (!Bindings.RegisterMember(TypeId, std::move(Member), ErrorId))
		{
			if (ErrorId == "diag.hostbinding_unknown_type_id" || ErrorId == "diag.hostbinding_enum_member_requires_enum_type" || ErrorId == "diag.hostbinding_static_constant_requires_value_signature" || ErrorId == "diag.hostbinding_type_requires_a_non_empty_name" || ErrorId == "diag.hostbinding_type_table_is_full" || ErrorId == "diag.hostbinding_clone_requires_struct_type")
			{
				ReportNativeError(ErrorId);
			}
			else
			{
				ReportNativeError(ErrorId, {NameForArgs});
			}
			return false;
		}
		return true;
	}

	bool VeroScriptSession::RegisterHostOperator(
	    const FHostTypeId TypeId,
	    FHostMemberDesc Operator)
	{
		if (RejectBindingMutation("RegisterHostOperator"))
		{
			return false;
		}
		std::string ErrorId;
		const std::string NameForArgs = Operator.Name;
		if (!Bindings.RegisterOperator(TypeId, std::move(Operator), ErrorId))
		{
			ReportNativeError(ErrorId, {NameForArgs});
			return false;
		}
		return true;
	}

	bool VeroScriptSession::RegisterHostTypeClone(
	    const FHostTypeId TypeId,
	    const FNativeCallback Callback,
	    void* const UserData)
	{
		if (RejectBindingMutation("RegisterHostTypeClone"))
		{
			return false;
		}
		std::string ErrorId;
		if (!Bindings.RegisterTypeClone(TypeId, Callback, UserData, ErrorId))
		{
			ReportNativeError(ErrorId);
			return false;
		}
		return true;
	}

	bool VeroScriptSession::SetHostTypeInheritable(
	    const FHostTypeId TypeId,
	    const bool bInheritable)
	{
		if (RejectBindingMutation("SetHostTypeInheritable"))
		{
			return false;
		}
		std::string ErrorId;
		if (!Bindings.SetTypeInheritable(TypeId, bInheritable, ErrorId))
		{
			ReportNativeError(ErrorId);
			return false;
		}
		return true;
	}

	bool VeroScriptSession::SetHostEnumScoped(
	    const FHostTypeId TypeId,
	    const bool bScoped)
	{
		if (RejectBindingMutation("SetHostEnumScoped"))
		{
			return false;
		}
		std::string ErrorId;
		if (!Bindings.SetEnumScoped(TypeId, bScoped, ErrorId))
		{
			ReportNativeError(ErrorId);
			return false;
		}
		return true;
	}

	bool VeroScriptSession::RegisterHostTypeSubclassFactory(
	    const FHostTypeId TypeId,
	    const FNativeCallback Callback,
	    void* const UserData)
	{
		if (RejectBindingMutation("RegisterHostTypeSubclassFactory"))
		{
			return false;
		}
		std::string ErrorId;
		if (!Bindings.RegisterTypeSubclassFactory(TypeId, Callback, UserData, ErrorId))
		{
			ReportNativeError(ErrorId);
			return false;
		}
		return true;
	}

	FHostBindingRegistry& VeroScriptSession::GetBindingRegistry()
	{
		if (RejectBindingMutation("GetBindingRegistry"))
		{
			throw std::logic_error(
			    "Session binding registry is unavailable during execution or with an environment-owned catalog");
		}

		return Bindings;
	}

	const FHostBindingRegistry& VeroScriptSession::GetBindingRegistry() const
	{
		if (ModuleEnvironment && ModuleEnvironment->GetDescription().Bindings)
		{
			throw std::logic_error("Use GetBindingCatalog for an environment-owned binding catalog");
		}

		return Bindings;
	}

	bool VeroScriptSession::ExportBindingsToFile(const std::string_view CatalogFilePath)
	{
		FRunResult PathFailure;
		if (CatalogFilePath.find('\0') != std::string_view::npos)
		{
			FHostStreamBridge Bridge(Host);
			EngineInternal::ValidateFilePath(CatalogFilePath, Bridge.ErrorStream(), PathFailure);
			return false;
		}
		std::string Error;
		if (!SaveBindingCatalogFile(EffectiveBindings(), CatalogFilePath, Error))
		{
			FHostStreamBridge Bridge(Host);
			Bridge.ErrorStream() << "VeroScript: " << Error << '\n';
			return false;
		}
		return true;
	}

	FRunResult VeroScriptSession::LoadFile(const std::string_view InFilePath)
	{
		if (RequiresReset()) return MakeResetRequiredResult();
		if (InFilePath.find('\0') != std::string_view::npos)
		{
			FHostStreamBridge Bridge(Host);
			FRunResult Failure;
			EngineInternal::ValidateFilePath(InFilePath, Bridge.ErrorStream(), Failure);
			return Failure;
		}
		#if defined(VEROSCRIPT_WITH_APPLICATION)
		const auto ApplicationPath = PathUtil::FromUtf8(InFilePath);
		auto Extension = PathUtil::ToUtf8(ApplicationPath.extension());
		std::transform(Extension.begin(), Extension.end(), Extension.begin(), [](unsigned char C)
		{
			return static_cast<char>(std::tolower(C));
		});
		std::error_code ApplicationPathError;
		if (Extension == ".vsapp" || Extension == ".zip" || ApplicationPath.filename() == "veroscript-app.json" || std::filesystem::is_directory(ApplicationPath, ApplicationPathError))
		{
			auto Services = Application::CreateApplicationServices();
			Services.Environment = ModuleEnvironment;
			Services.StandardLibraryPaths.insert(Services.StandardLibraryPaths.end(), StdlibSearchPaths.begin(), StdlibSearchPaths.end());
			const auto Root = ApplicationPath.filename() == "veroscript-app.json" ? ApplicationPath.parent_path() : ApplicationPath;
			return LoadApplication(PathUtil::ToUtf8(std::filesystem::absolute(Root.empty() ? std::filesystem::path(".") : Root)), Services);
		}
		#endif
		if (RejectIfRunning("LoadFile"))
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.loadfile_is_not_allowed_while_run_is_in_progress");
			Result.Error.Location.FileLabel = "<session>";
			return Result;
		}
		std::unique_ptr<FBytecodeModule> NewModule;
		const FRunResult CompileResult = CompileFile(InFilePath, NewModule);
		if (!CompileResult.bSuccess || !NewModule)
		{
			return CompileResult;
		}
		auto Result = CommitCompiledFile(NewModule, std::string(InFilePath));
		Result.Diagnostics.insert(Result.Diagnostics.begin(), CompileResult.Diagnostics.begin(), CompileResult.Diagnostics.end());
		return Result;
	}

	FRunResult VeroScriptSession::LoadSource(
	    const std::string_view Source,
	    const std::string_view FileLabel)
	{
		if (RequiresReset()) return MakeResetRequiredResult();
		if (RejectIfRunning("LoadSource"))
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.loadsource_is_not_allowed_while_run_is_in_progress");
			Result.Error.Location.FileLabel = "<session>";
			return Result;
		}
		FHostStreamBridge Bridge(Host);
		std::unique_ptr<FBytecodeModule> NewModule;
		const std::vector<std::string_view> ImportViews = MakeImportPathViews();
		const std::vector<std::string_view> ImplicitImportViews = MakeImplicitImportPathViews();
		const std::vector<std::string_view> DefineViews = MakeDefineViews();
		const std::vector<std::string_view> StdlibViews = MakeStdlibSearchPathViews();
		FRunResult CompileResult;
		{
			const FSessionCallbackGuard CompileGuard(bCompiling);
			const FSessionCallbackGuard CallbackGuard(bRunning);
			const auto ScriptOptions = MakeScriptCompileExtensionOptions();
			CompileResult = EngineInternal::CompileSourceToModule(
			    Source, FileLabel, ImportViews, Bridge.ErrorStream(), NewModule,
			    &EffectiveBindings().GetNativeFunctions(), &SourceOverlays, &DefineViews, &PredefinedMacros,
			    &StdlibViews, &EffectiveBindings(), &ImplicitImportViews, bAllowCircularImports,
			    bAllowProjectWideAccess, bUseBytecodeCache && !bHotReloadEnabled, BytecodeCacheDirectory,
			    &AnnotationProcessing, &CompileProcessing, &ScriptOptions, ModuleEnvironment);
		}
		return CommitModule(NewModule, std::string{}, CompileResult);
	}

	FRunResult VeroScriptSession::LoadPackage(
	    FPackageData& Package,
	    std::string LogicalPath)
	{
		if (RequiresReset()) return MakeResetRequiredResult();
		if (RejectIfRunning("LoadPackage"))
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic(
			    "diag.action_not_allowed_while_run_in_progress",
			    {},
			    {"LoadPackage"});
			Result.Error.Location.FileLabel = "<session>";
			return Result;
		}
		if (!Package.Module)
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.failed_to_load_module");
			Result.Error.Location.FileLabel = "<session>";
			return Result;
		}
		if (!PackageKindMatchesModule(Package.Kind, *Package.Module))
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic(
			    "diag.failed_to_load_module",
			    FSourceLocation{
			        LogicalPath.empty() ? "<package>" : LogicalPath, 1, 1});
			Result.Error.Message =
			    "package kind does not match its module entry-point shape";
			Result.Diagnostics = {Result.Error};
			return Result;
		}
		std::string ValidationError;
		if (!VeroScriptBytecodeIO::ValidateModule(
		        *Package.Module, ValidationError))
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic(
			    "diag.failed_to_load_module",
			    FSourceLocation{
			        LogicalPath.empty() ? "<package>" : LogicalPath, 1, 1});
			Result.Error.Message = ValidationError;
			Result.Diagnostics = {Result.Error};
			return Result;
		}
		FRunResult Ok;
		Ok.bSuccess = true;
		Ok.ExitCode = 0;
		FRunResult Result = CommitModule(
		    Package.Module,
		    LogicalPath.empty() ? "<package>" : std::move(LogicalPath),
		    Ok,
		    Package.Kind);
		if (Result.bSuccess)
		{
			ReloadSourcePath.clear();
		}
		return Result;
	}

	FRunResult VeroScriptSession::LoadApplication(std::shared_ptr<const Application::FApplication> App)
	{
		return LoadApplicationInternal(std::move(App), false);
	}

	FRunResult VeroScriptSession::ReloadApplication(std::shared_ptr<const Application::FApplication> App)
	{
		return LoadApplicationInternal(std::move(App), true);
	}

	FRunResult VeroScriptSession::LoadApplication(const std::string_view Path,
	    const Application::FApplicationServices& Services)
	{
		#if defined(VEROSCRIPT_WITH_APPLICATION)
		if (RejectIfRunning("LoadApplication"))
		{
			FRunResult R;
			R.ExitCode = 2;
			R.Error.Message = "cannot load an application while Session is active";
			return R;
		}
		Application::FDiagnostic E;
		std::shared_ptr<const Application::FApplication> App;
		{
			const FSessionCallbackGuard CompileGuard(bCompiling);
			const FSessionCallbackGuard CallbackGuard(bRunning);
			App = Application::FApplication::Open(std::string(Path), Services, E);
		}
		if (!App)
		{
			FRunResult R;
			R.ExitCode = 2;
			R.Error.Message = E.Code + ": " + E.Message;
			R.Error.Location.FileLabel = std::string(Path);
			return R;
		}
		return LoadApplication(std::move(App));
		#else
		(void)Path;
		(void)Services;
		FRunResult R;
		R.ExitCode = 2;
		R.Error.Message = "application container support was not built";
		return R;
		#endif
	}

	FRunResult VeroScriptSession::CompileFile(
	    const std::string_view InFilePath,
	    std::unique_ptr<FBytecodeModule>& OutModule)
	{
		OutModule.reset();
		if (RejectIfRunning("CompileFile"))
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic(
			    "diag.action_not_allowed_while_run_in_progress",
			    {},
			    {"CompileFile"});
			Result.Error.Location.FileLabel = "<session>";
			return Result;
		}
		FHostStreamBridge Bridge(Host);
		const std::vector<std::string_view> ImportViews = MakeImportPathViews();
		const std::vector<std::string_view> ImplicitImportViews = MakeImplicitImportPathViews();
		const std::vector<std::string_view> DefineViews = MakeDefineViews();
		const std::vector<std::string_view> StdlibViews = MakeStdlibSearchPathViews();
		const FSessionCallbackGuard CompileGuard(bCompiling);
		const FSessionCallbackGuard CallbackGuard(bRunning);
		const auto ScriptOptions = MakeScriptCompileExtensionOptions();
		return EngineInternal::CompileFileToModule(
		    InFilePath, ImportViews,
		    bUseBytecodeCache && EffectiveBindings().GetNativeFunctions().empty() && EffectiveBindings().GetTypes().empty() &&
		        !bHotReloadEnabled,
		    Bridge.ErrorStream(), OutModule, &EffectiveBindings().GetNativeFunctions(), &SourceOverlays,
		    &DefineViews, &PredefinedMacros, BytecodeCacheDirectory, &StdlibViews, &EffectiveBindings(),
		    &ImplicitImportViews, bAllowCircularImports, bAllowProjectWideAccess, &AnnotationProcessing,
		    &CompileProcessing, &ScriptOptions, ModuleEnvironment);
	}

	FRunResult VeroScriptSession::CommitCompiledFile(
	    std::unique_ptr<FBytecodeModule>& NewModule,
	    std::string InFilePath)
	{
		if (RequiresReset()) return MakeResetRequiredResult();
		if (RejectIfRunning("CommitCompiledFile"))
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic(
			    "diag.action_not_allowed_while_run_in_progress",
			    {},
			    {"CommitCompiledFile"});
			Result.Error.Location.FileLabel = "<session>";
			return Result;
		}
		if (!NewModule)
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.failed_to_load_module");
			Result.Error.Location.FileLabel = "<session>";
			return Result;
		}
		FRunResult Ok;
		Ok.bSuccess = true;
		Ok.ExitCode = 0;
		return CommitModule(NewModule, std::move(InFilePath), Ok);
	}

	FRunResult VeroScriptSession::Reload()
	{
		if (RequiresReset()) return MakeResetRequiredResult();
		FRunResult Rejection;
		if (RejectIfRunning("Reload", &Rejection)) return Rejection;
		if (!BeginReloadOperation()) return MakeResetRequiredResult();
		if (ReloadSourcePath.empty())
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = Module
			    ? MakeDiagnostic("diag.reload_requires_a_file_backed_module_use_loadfile")
			    : MakeDiagnostic("diag.nothing_to_reload");
			Result.Error.Location.FileLabel = "<session>";
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		// A replacement is only accepted after its first module initialization.
		// If another edit arrives first, return to the last validated generation.
		RestorePendingHotReload();

		FHostStreamBridge Bridge(Host);
		std::unique_ptr<FBytecodeModule> NewModule;
		const std::vector<std::string_view> ImportViews = MakeImportPathViews();
		const std::vector<std::string_view> ImplicitImportViews = MakeImplicitImportPathViews();
		const std::vector<std::string_view> DefineViews = MakeDefineViews();
		const std::vector<std::string_view> StdlibViews = MakeStdlibSearchPathViews();
		FRunResult CompileResult;
		{
			const FSessionCallbackGuard CompileGuard(bCompiling);
			const FSessionCallbackGuard CallbackGuard(bRunning);
			const auto ScriptOptions = MakeScriptCompileExtensionOptions();
			CompileResult = EngineInternal::CompileFileToModule(
			    ReloadSourcePath, ImportViews,
			    bUseBytecodeCache && EffectiveBindings().GetNativeFunctions().empty() && EffectiveBindings().GetTypes().empty() &&
			        !bHotReloadEnabled,
			    Bridge.ErrorStream(), NewModule, &EffectiveBindings().GetNativeFunctions(), &SourceOverlays,
			    &DefineViews, &PredefinedMacros, BytecodeCacheDirectory, &StdlibViews, &EffectiveBindings(),
			    &ImplicitImportViews, bAllowCircularImports, bAllowProjectWideAccess, &AnnotationProcessing,
			    &CompileProcessing, &ScriptOptions, ModuleEnvironment);
		}
		if (!CompileResult.bSuccess || !NewModule)
		{
			return CompileResult;
		}

		return StageReloadedModule(
		    NewModule, FilePath, CompileResult, false, std::nullopt);
	}

	FRunResult VeroScriptSession::ReloadCompiledModule(
	    std::unique_ptr<FBytecodeModule>& NewModule,
	    std::string LogicalPath)
	{
		if (RequiresReset()) return MakeResetRequiredResult();
		FRunResult Rejection;
		if (RejectIfRunning("ReloadCompiledModule", &Rejection)) return Rejection;
		if (!BeginReloadOperation()) return MakeResetRequiredResult();
		if (!Module)
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.nothing_to_reload");
			Result.Error.Location.FileLabel = "<session>";
			return Result;
		}
		if (!NewModule)
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.failed_to_load_module");
			Result.Error.Location.FileLabel = "<session>";
			return Result;
		}
		if ((Module->MainFunctionIndex >= 0) != (NewModule->MainFunctionIndex >= 0))
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic(
			    "diag.failed_to_load_module",
			    FSourceLocation{
			        LogicalPath.empty() ? "<compiled-module>" : LogicalPath,
			        1,
			        1});
			Result.Error.Message =
			    "reload module entry-point shape does not match the loaded module";
			Result.Diagnostics = {Result.Error};
			return Result;
		}

		std::string ValidationError;
		if (!VeroScriptBytecodeIO::ValidateModule(
		        *NewModule, ValidationError))
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic(
			    "diag.failed_to_load_module",
			    FSourceLocation{
			        LogicalPath.empty() ? "<compiled-module>" : LogicalPath,
			        1,
			        1});
			Result.Error.Message = ValidationError;
			Result.Diagnostics = {Result.Error};
			return Result;
		}

		RestorePendingHotReload();
		FRunResult Ok;
		Ok.bSuccess = true;
		Ok.ExitCode = 0;
		FRunResult Result = StageReloadedModule(
		    NewModule,
		    LogicalPath.empty()
		        ? "<compiled-module>"
		        : std::move(LogicalPath),
		    Ok,
		    true,
		    std::nullopt);
		if (Result.bSuccess)
		{
			ReloadSourcePath.clear();
		}
		return Result;
	}

	FRunResult VeroScriptSession::ReloadPackage(
	    FPackageData& Package,
	    std::string LogicalPath)
	{
		if (RequiresReset()) return MakeResetRequiredResult();
		FRunResult Rejection;
		if (RejectIfRunning("ReloadPackage", &Rejection)) return Rejection;
		if (!BeginReloadOperation()) return MakeResetRequiredResult();
		if (!Module)
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.nothing_to_reload");
			Result.Error.Location.FileLabel = "<session>";
			return Result;
		}
		if (!Package.Module)
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.failed_to_load_module");
			Result.Error.Location.FileLabel = "<session>";
			return Result;
		}
		if (!PackageKindMatchesModule(Package.Kind, *Package.Module))
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic(
			    "diag.failed_to_load_module",
			    FSourceLocation{
			        LogicalPath.empty() ? "<package>" : LogicalPath, 1, 1});
			Result.Error.Message =
			    "package kind does not match its module entry-point shape";
			Result.Diagnostics = {Result.Error};
			return Result;
		}
		const EPackageKind CurrentKind =
		    Module->MainFunctionIndex >= 0
		    ? EPackageKind::Application
		    : EPackageKind::Library;
		if (Package.Kind != CurrentKind)
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic(
			    "diag.failed_to_load_module",
			    FSourceLocation{
			        LogicalPath.empty() ? "<package>" : LogicalPath, 1, 1});
			Result.Error.Message =
			    "reload package kind does not match the loaded module";
			Result.Diagnostics = {Result.Error};
			return Result;
		}

		std::string ValidationError;
		if (!VeroScriptBytecodeIO::ValidateModule(
		        *Package.Module, ValidationError))
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic(
			    "diag.failed_to_load_module",
			    FSourceLocation{
			        LogicalPath.empty() ? "<package>" : LogicalPath, 1, 1});
			Result.Error.Message = ValidationError;
			Result.Diagnostics = {Result.Error};
			return Result;
		}

		RestorePendingHotReload();
		FRunResult Ok;
		Ok.bSuccess = true;
		Ok.ExitCode = 0;
		FRunResult Result = StageReloadedModule(
		    Package.Module,
		    LogicalPath.empty() ? "<package>" : std::move(LogicalPath),
		    Ok,
		    true,
		    Package.Kind);
		if (Result.bSuccess)
		{
			// A deployed package has no source path for Reload().
			ReloadSourcePath.clear();
		}
		return Result;
	}

	FReloadStatus VeroScriptSession::GetReloadStatus() const noexcept
	{
		auto Status = ReloadStatus;
		Status.CurrentGeneration = Module ? ProgramGeneration : 0;
		Status.CommittedGeneration = Module ? CommittedProgramGeneration : 0;
		if (RequiresReset())
		{
			Status.State = EReloadState::ResetRequired;
		}
		return Status;
	}

	namespace
	{
		struct FSessionRunningGuard
		{
			explicit FSessionRunningGuard(bool& InFlag)
			    : Flag(InFlag)
			{
				Flag = true;
			}

			~FSessionRunningGuard()
			{
				Flag = false;
			}

			bool& Flag;
		};

		FRunResult MakeSessionNoModuleResult(IEngineHost& Host)
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.no_module_loaded");
			Result.Error.Location.FileLabel = "<session>";
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}

		FRunResult MakeSessionReentryResult(IEngineHost& Host, const std::string_view Action)
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic(
			    "diag.action_not_allowed_while_run_in_progress",
			    {},
			    {Action});
			Result.Error.Location.FileLabel = "<session>";
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}

		FRunResult FinishVmCall(
		    VeroScriptVm& Vm,
		    std::ostream& ErrorStream,
		    FJitStats* const OutJitStats,
		    FJitDiagnostics* const OutDiagnostics = nullptr)
		{
			if (OutDiagnostics)
			{
				try
				{
					*OutDiagnostics = Vm.GetJitDiagnostics();
				}
				catch (...)
				{
					*OutDiagnostics = {};
				}
			}
			FRunResult Result;
			if (OutJitStats)
			{
				*OutJitStats = Vm.GetJitStats();
			}
			if (Vm.HasError())
			{
				Result.ExitCode = 1;
				Result.Error = Vm.GetError();
				Result.Diagnostics = {Result.Error};
				const auto& Cleanup = Vm.GetCleanupDiagnostics();
				Result.Diagnostics.insert(Result.Diagnostics.end(), Cleanup.begin(), Cleanup.end());
				if (Vm.RequiresReset())
				{
					Result.Diagnostics.push_back(MakeDiagnostic("diag.execution_state_reset_required"));
				}
				for (const auto& Diagnostic : Result.Diagnostics)
				{
					EngineInternal::WriteError(ErrorStream, Diagnostic);
				}
				return Result;
			}
			Result.bSuccess = true;
			Result.bExitRequested = Vm.WasExitRequested();
			Result.ExitCode = Vm.GetProcessExitCode();
			return Result;
		}
	}

	FRunResult VeroScriptSession::Run(const ERunMode RunMode)
	{
		if (RequiresReset()) return MakeResetRequiredResult();
		FValueSafePoint ValueSafePoint(*this);
		const auto Dispatch = [&]() -> FRunResult
		{
			if (!Module)
			{
				FRunResult Result;
				Result.ExitCode = 2;
				Result.Error = MakeDiagnostic("diag.no_module_loaded");
				Result.Error.Location.FileLabel = "<session>";
				FHostStreamBridge Bridge(Host);
				EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
				return Result;
			}
			if (LoadedPackageKind == EPackageKind::Library)
			{
				FRunResult Result;
				Result.ExitCode = 2;
				Result.Error = MakeDiagnostic("diag.failed_to_load_module");
				Result.Error.Message =
				    "library package cannot be run directly; load an application package";
				Result.Error.Location.FileLabel =
				    FilePath.empty() ? "<package>" : FilePath;
				Result.Diagnostics = {Result.Error};
				FHostStreamBridge Bridge(Host);
				EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
				return Result;
			}
			if (bRunning)
			{
				FRunResult Result;
				Result.ExitCode = 2;
				Result.Error = MakeDiagnostic("diag.run_is_not_allowed_while_run_is_in_progress");
				Result.Error.Location.FileLabel = "<session>";
				FHostStreamBridge Bridge(Host);
				EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
				return Result;
			}

			struct FRunningGuard
			{
				explicit FRunningGuard(bool& InFlag)
				    : Flag(InFlag)
				{
					Flag = true;
				}

				~FRunningGuard()
				{
					Flag = false;
				}

				bool& Flag;
			};

			const FRunningGuard RunningGuard(bRunning);
			FHostStreamBridge Bridge(Host);
			const std::vector<std::string_view> ArgViews = MakeScriptArgViews();
			FSessionDebugExecutionScope DebugScope(DebugBridge, GetDebugProgram(),
			    ESessionDebugExecutionKind::Run,
			    RunMode == ERunMode::Script ? "main" : "<init>", {}, ExecutionControl, RunMode);
			VeroScriptVm Vm(Bridge.OutputStream(), InputStreamOrCin(), Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
			if (!ReusableExecution)
			{
				ReusableExecution = std::make_unique<FSessionExecutionScratch>();
			}
			ReusableExecution->LeaseTo(Vm);
			const bool bConfigured = ConfigureExecutionVm(Vm);
			if (bConfigured)
			{
				ReusableExecution->SwapNativeBindings(Vm);
			}
			Vm.SetDebugger(DebugScope.GetDebugger());
			const bool bCanRetainCode = bConfigured && LeaseReusableJit(Vm);
			FSessionJitExceptionGuard ExceptionGuard{
			    bCanRetainCode ? ReusableJit.get() : nullptr, Vm, LastJitStats, JitCacheLimits};
			FHotReloadState* const ExecutionState = bHotReloadEnabled || PendingHotReloadRollback
			    ? HotReloadState.get()
			    : nullptr;
			const bool bExecuted = bConfigured && (
			    ExecutionState
			    ? Vm.ExecutePersistent(*Module, RunMode, *ExecutionState, PendingHotReloadRollback.get(),
			          ArgViews, &EffectiveBindings().GetNativeFunctions())
			    : Vm.Execute(*Module, RunMode, ArgViews, &EffectiveBindings().GetNativeFunctions()));
			FRunResult Result = FinishVmCall(Vm, Bridge.ErrorStream(), &LastJitStats, &LastJitDiagnostics);
			if (!bExecuted && Result.bSuccess)
			{
				Result.bSuccess = false;
				Result.ExitCode = 1;
				Result.Error = Vm.GetError();
				Result.Diagnostics = {Result.Error};
				EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			}
			ReusableExecution->RecoverFrom(Vm);
			RecoverReusableJit(Vm, bCanRetainCode, bExecuted && Result.bSuccess);
			ExceptionGuard.State = nullptr;
			DebugScope.Finish();
			SettlePendingHotReload(Result, Bridge.ErrorStream());
			return Result;
		};
		return ValueSafePoint.Finish(ValueSafePoint.Ready() ? Dispatch() : FRunResult{});
	}

	FRunResult VeroScriptSession::CallMethod(
	    const FObjectHandle& Object,
	    const std::string_view MethodName,
	    const std::vector<FValue>& Arguments,
	    FValue* const OutReturn,
	    const EMissingMethodPolicy MissingPolicy)
	{
		return CallMethodInternal(
		    Object,
		    MethodName,
		    Arguments,
		    nullptr,
		    OutReturn,
		    MissingPolicy,
		    false);
	}

	FRunResult VeroScriptSession::CallMethod(
	    const FObjectHandle& Object,
	    const std::string_view MethodName,
	    std::vector<FValue>& Arguments,
	    FValue* const OutReturn,
	    const EMissingMethodPolicy MissingPolicy)
	{
		return CallMethodInternal(
		    Object,
		    MethodName,
		    Arguments,
		    &Arguments,
		    OutReturn,
		    MissingPolicy,
		    false);
	}

	FRunResult VeroScriptSession::CallMethodBypassingInterceptor(
	    const FObjectHandle& Object,
	    const std::string_view MethodName,
	    const std::vector<FValue>& Arguments,
	    FValue* const OutReturn,
	    const EMissingMethodPolicy MissingPolicy)
	{
		return CallMethodInternal(
		    Object,
		    MethodName,
		    Arguments,
		    nullptr,
		    OutReturn,
		    MissingPolicy,
		    true);
	}

	FRunResult VeroScriptSession::CallMethodBypassingInterceptor(
	    const FObjectHandle& Object,
	    const std::string_view MethodName,
	    std::vector<FValue>& Arguments,
	    FValue* const OutReturn,
	    const EMissingMethodPolicy MissingPolicy)
	{
		return CallMethodInternal(
		    Object,
		    MethodName,
		    Arguments,
		    &Arguments,
		    OutReturn,
		    MissingPolicy,
		    true);
	}

	bool VeroScriptSession::HasInstanceMethod(
	    const FObjectHandle& Object,
	    const std::string_view MethodName,
	    const std::uint8_t ArgumentCount) const
	{
		if (!Module || !Object || MethodName.empty())
		{
			return false;
		}
		return ModuleMeta::HasInstanceMethodOnType(
		    *Module,
		    Object->GetTypeName(),
		    MethodName,
		    ArgumentCount);
	}

	FRunResult VeroScriptSession::ConstructObject(
	    const std::string_view TypeName,
	    const std::vector<FValue>& Arguments,
	    FObjectHandle& OutObject)
	{
		if (RequiresReset()) return MakeResetRequiredResult();
		FValueSafePoint ValueSafePoint(*this, &Arguments, nullptr, &OutObject);
		const auto Dispatch = [&]() -> FRunResult
		{
			FSessionOutputScope<FObjectHandle> Output(&OutObject, bRunning);
			if (!Module)
			{
				return MakeSessionNoModuleResult(Host);
			}
			if (bRunning)
			{
				return MakeSessionReentryResult(Host, "ConstructObject");
			}
			if (TypeName.empty())
			{
				FRunResult Result;
				Result.ExitCode = 2;
				Result.Error = MakeDiagnostic("diag.constructobject_requires_a_non_empty_type_name");
				Result.Error.Location.FileLabel = "<session>";
				FHostStreamBridge Bridge(Host);
				EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
				return Result;
			}
			if (Arguments.size() > 255)
			{
				FRunResult Result;
				Result.ExitCode = 2;
				Result.Error = MakeDiagnostic("diag.constructobject_supports_at_most_255_arguments");
				Result.Error.Location.FileLabel = "<session>";
				FHostStreamBridge Bridge(Host);
				EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
				return Result;
			}
			{
				std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
				bool bAmbiguous = false;
				if (!ModuleMeta::FindTypeIndex(*Module, TypeName, TypeIndex, &bAmbiguous))
				{
					FRunResult Result;
					Result.ExitCode = 4;
					Result.Error = MakeDiagnostic(
					    bAmbiguous
					        ? "diag.constructobject_ambiguous_type_name"
					        : "diag.constructobject_unknown_type",
					    FSourceLocation{"<session>", 1, 1},
					    {TypeName});
					Result.Diagnostics = {Result.Error};
					FHostStreamBridge Bridge(Host);
					EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
					return Result;
				}
				if (!Arguments.empty())
				{
					const std::uint8_t ArgumentCount = static_cast<std::uint8_t>(Arguments.size());
					std::uint16_t CtorIndex = static_cast<std::uint16_t>(-1);
					const EMethodResolveStatus CtorStatus = ModuleMeta::TryResolveConstructorOnType(
					    *Module,
					    Module->Types[TypeIndex].Name,
					    ArgumentCount,
					    CtorIndex,
					    Arguments.data());
					if (CtorStatus != EMethodResolveStatus::Found)
					{
						const char* MessageId = CtorStatus == EMethodResolveStatus::Ambiguous
						    ? "diag.constructobject_ambiguous_constructor"
						    : "diag.constructobject_unknown_constructor";
						const std::string ArgCountText = std::to_string(ArgumentCount);
						FRunResult Result;
						Result.ExitCode = 4;
						Result.Error = MakeDiagnostic(
						    MessageId,
						    FSourceLocation{"<session>", 1, 1},
						    {TypeName, ArgCountText});
						Result.Diagnostics = {Result.Error};
						FHostStreamBridge Bridge(Host);
						EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
						return Result;
					}
				}
			}

			const FSessionRunningGuard RunningGuard(bRunning);
			FValueSafePoint PendingOutput(*this, nullptr, nullptr, Output.Get());
			FHostStreamBridge Bridge(Host);
			FSessionDebugExecutionScope DebugScope(DebugBridge, GetDebugProgram(),
			    ESessionDebugExecutionKind::ConstructObject, TypeName, {}, ExecutionControl);
			VeroScriptVm Vm(
			    Bridge.OutputStream(),
			    InputStreamOrCin(),
			    Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
			const bool bConfigured = ConfigureExecutionVm(Vm);
			Vm.SetDebugger(DebugScope.GetDebugger());
			const bool bExecuted = bConfigured && Vm.ConstructObject(
			    *Module, TypeName, Arguments, Output.Value,
			    (bHotReloadEnabled || PendingHotReloadRollback) ? HotReloadState.get() : nullptr,
			    &EffectiveBindings().GetNativeFunctions(), PendingHotReloadRollback.get());
			DebugScope.Finish();
			FRunResult Result = FinishVmCall(Vm, Bridge.ErrorStream(), &LastJitStats, &LastJitDiagnostics);
			if (!bExecuted && Result.bSuccess)
			{
				Result.bSuccess = false;
				Result.ExitCode = 1;
				Result.Error = Vm.GetError();
				Result.Diagnostics = {Result.Error};
				EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			}
			if (!bExecuted)
			{
				Output.Value = {};
			}
			SettlePendingHotReload(Result, Bridge.ErrorStream());
			return Result;
		};
		FRunResult Completed = ValueSafePoint.Ready() ? Dispatch() : FRunResult{};
		ValueSafePoint.Arguments = nullptr; // constructor inputs are no longer borrowed
		return ValueSafePoint.Finish(std::move(Completed));
	}

	FRunResult VeroScriptSession::CallFunction(
	    const std::string_view FunctionName,
	    const std::vector<FValue>& Arguments,
	    FValue* const OutReturn,
	    const EMissingMethodPolicy MissingPolicy)
	{
		return CallFunctionInternal(
		    FunctionName,
		    Arguments,
		    nullptr,
		    OutReturn,
		    MissingPolicy);
	}

	FRetainedCallableHandle VeroScriptSession::RetainCallable(
	    const FFunctionHandle& Callable,
	    const bool bOwnerThreadOnly,
	    const ERetainedThisCapture ThisCapture)
	{
		if (bCleaningUp || bCompiling || !Callable || !Module || Callable->ProgramGeneration != ProgramGeneration || Callable->ProgramOwner != ProgramOwner || Callable->FunctionIndex >= Module->Functions.size())
		{
			return {};
		}
		const FBytecodeFunction& Target = Module->Functions[Callable->FunctionIndex];
		if (Callable->FunctionName.empty() || Callable->FunctionName != Target.Name || Callable->TypeName != Target.ClosureTypeName)
		{
			return {};
		}
		FRetainedCallableEntry Entry;
		Entry.Generation = ProgramGeneration;
		Entry.Callable = Callable;
		Entry.bOwnerThreadOnly = bOwnerThreadOnly;
		if (ThisCapture == ERetainedThisCapture::Weak && Callable->CapturedThis)
		{
			Entry.WeakThis = Callable->CapturedThis;
			Entry.bWeakThis = true;
			Entry.Callable = std::make_shared<FFunctionValue>(*Callable);
			Entry.Callable->CapturedThis.reset();
		}
		std::lock_guard<std::mutex> Lock(RetainedCallableMutex);
		const auto Id = AllocateRetainedCallableId();
		if (Id == 0) return {};
		const auto Inserted = RetainedCallables.emplace(Id, std::move(Entry));
		if (!Inserted.second)
		{
			return {};
		}
		return {Id, ProgramGeneration};
	}

	bool VeroScriptSession::ReleaseCallable(const FRetainedCallableHandle Callable)
	{
		if (!Callable || bRunning || Callable.ProgramGeneration != ProgramGeneration)
		{
			return false;
		}
		FValueRoot Retirement;
		std::string Error;
		{
			std::lock_guard<std::mutex> Lock(RetainedCallableMutex);
			const auto It = RetainedCallables.find(Callable.Id);
			if (It == RetainedCallables.end() || !It->second.Callable || It->second.Generation != Callable.ProgramGeneration || (It->second.bOwnerThreadOnly && std::this_thread::get_id() != OwnerThreadId))
			{
				return false;
			}
			// Retiring one handle must preserve graphs still owned by another
			// callable, an exported value root, or persistent globals. Reserve the
			// cleanup slot before removing the entry so failure leaves it usable.
			if (!ValueRoots->Reserve(Retirement, Error)) return false;
			FValue Value = It->second.Callable;
			if (!ValueRoots->CommitReserved(Retirement, Value, Error)) return false;
			RetainedCallables.erase(It);
		}
		Retirement.Reset();
		// Cross-thread callables may be released by a serialized foreign thread;
		// language cleanup remains an operation of the value owner's thread.
		if (ValueRoots->IsOwnerThread() && !DrainValueRoots(Error))
		{
			const FSessionCallbackGuard CallbackGuard(bRunning);
			FHostStreamBridge Bridge(Host);
			FScriptError Failure;
			Failure.Message = std::move(Error);
			Failure.Location.FileLabel = "<callable-cleanup>";
			EngineInternal::WriteError(Bridge.ErrorStream(), Failure);
		}
		return true;
	}

	bool VeroScriptSession::IsCallableValid(const FRetainedCallableHandle Callable) const
	{
		if (!Callable || !Module || Callable.ProgramGeneration != ProgramGeneration)
		{
			return false;
		}
		std::lock_guard<std::mutex> Lock(RetainedCallableMutex);
		const auto It = RetainedCallables.find(Callable.Id);
		if (It == RetainedCallables.end() || It->second.Generation != Callable.ProgramGeneration || !It->second.Callable || It->second.Callable->ProgramGeneration != ProgramGeneration || It->second.Callable->FunctionIndex >= Module->Functions.size() || (It->second.bWeakThis && It->second.WeakThis.expired()))
		{
			return false;
		}
		const FBytecodeFunction& Target =
		    Module->Functions[It->second.Callable->FunctionIndex];
		return It->second.Callable->FunctionName == Target.Name && It->second.Callable->TypeName == Target.ClosureTypeName;
	}

	FRunResult VeroScriptSession::InvokeCallable(
	    const FRetainedCallableHandle Callable,
	    const std::vector<FValue>& Arguments,
	    FValue* const OutReturn)
	{
		return InvokeCallableInternal(Callable, Arguments, nullptr, OutReturn);
	}

	FRunResult VeroScriptSession::InvokeCallable(
	    const FRetainedCallableHandle Callable,
	    std::vector<FValue>& Arguments,
	    FValue* const OutReturn)
	{
		return InvokeCallableInternal(Callable, Arguments, &Arguments, OutReturn);
	}

	FRunResult VeroScriptSession::ApplyClassDefaults(const FObjectHandle& Object)
	{
		if (RequiresReset()) return MakeResetRequiredResult();
		FValueSafePoint ValueSafePoint(*this, nullptr, nullptr, &Object);
		const auto Dispatch = [&]() -> FRunResult
		{
			if (!Module)
			{
				return MakeSessionNoModuleResult(Host);
			}
			if (bRunning)
			{
				return MakeSessionReentryResult(Host, "ApplyClassDefaults");
			}
			if (!Object)
			{
				FRunResult Result;
				Result.ExitCode = 2;
				Result.Error = MakeDiagnostic("diag.applyclassdefaults_requires_object");
				Result.Error.Location.FileLabel = "<session>";
				Result.Diagnostics = {Result.Error};
				FHostStreamBridge Bridge(Host);
				EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
				return Result;
			}

			std::uint16_t ObjectTypeIndex = static_cast<std::uint16_t>(-1);
			bool bAmbiguous = false;
			if (!ModuleMeta::FindTypeIndex(
			        *Module, Object->GetTypeName(), ObjectTypeIndex, &bAmbiguous))
			{
				FRunResult Result;
				Result.ExitCode = 4;
				Result.Error.MessageId = "diag.applyclassdefaults_unknown_object_type";
				Result.Error.Message = "class defaults object type is not present in the loaded module";
				Result.Error.MessageArgs = {Object->GetTypeName()};
				Result.Error.Location.FileLabel = "<session>";
				Result.Diagnostics = {Result.Error};
				return Result;
			}
			FHostStreamBridge Bridge(Host);
			VeroScriptVm LayoutVm(
			    Bridge.OutputStream(), InputStreamOrCin(), Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
			if (!ConfigureExecutionVm(LayoutVm) ||
				!LayoutVm.ValidateObjectLayout(*Module, Object, &EffectiveBindings().GetNativeFunctions()))
			{
				return FinishVmCall(LayoutVm, Bridge.ErrorStream(), nullptr);
			}

			std::vector<std::uint16_t> OrderedTypes;
			std::unordered_set<std::uint16_t> Visiting;
			std::unordered_set<std::uint16_t> Visited;
			const auto Visit = [&](auto&& Self, const std::uint16_t TypeIndex) -> bool
			{
				if (TypeIndex >= Module->Types.size())
				{
					return false;
				}
				if (Visited.find(TypeIndex) != Visited.end())
				{
					return true;
				}
				if (!Visiting.insert(TypeIndex).second)
				{
					return false;
				}
				for (const std::uint16_t BaseIndex : Module->Types[TypeIndex].BaseTypeIndices)
				{
					if (!Self(Self, BaseIndex))
					{
						return false;
					}
				}
				Visiting.erase(TypeIndex);
				Visited.insert(TypeIndex);
				OrderedTypes.push_back(TypeIndex);
				return true;
			};
			if (!Visit(Visit, ObjectTypeIndex))
			{
				FRunResult Result;
				Result.ExitCode = 1;
				Result.Error.MessageId = "diag.applyclassdefaults_invalid_inheritance_graph";
				Result.Error.Message = "class defaults encountered an invalid inheritance graph";
				Result.Error.MessageArgs = {Object->GetTypeName()};
				Result.Error.Location.FileLabel = "<session>";
				Result.Diagnostics = {Result.Error};
				return Result;
			}

			const FSessionRunningGuard RunningGuard(bRunning);
			FRunResult Result;
			Result.bSuccess = true;
			for (const std::uint16_t TypeIndex : OrderedTypes)
			{
				std::string DefaultsLinkage;
				for (const auto& Entry : Module->Types[TypeIndex].DeclMeta)
				{
					if (Entry.first == ClassDefaultsLinkageMetaKey)
					{
						DefaultsLinkage = Entry.second;
						break;
					}
				}
				if (DefaultsLinkage.empty())
				{
					continue;
				}
				const auto FunctionIt = Module->FunctionIndices.find(DefaultsLinkage);
				if (FunctionIt == Module->FunctionIndices.end() || FunctionIt->second >= Module->Functions.size())
				{
					Result = {};
					Result.ExitCode = 1;
					Result.Error.MessageId = "diag.applyclassdefaults_missing_compiled_body";
					Result.Error.Message = "class defaults metadata references a missing function";
					Result.Error.MessageArgs = {Module->Types[TypeIndex].Name};
					Result.Error.Location.FileLabel = "<session>";
					Result.Diagnostics = {Result.Error};
					EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
					SettlePendingHotReload(Result, Bridge.ErrorStream());
					return Result;
				}

				FSessionDebugExecutionScope DebugScope(DebugBridge, GetDebugProgram(),
				    ESessionDebugExecutionKind::ApplyClassDefaults, DefaultsLinkage, Object, ExecutionControl);
				VeroScriptVm Vm(Bridge.OutputStream(), InputStreamOrCin(), Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
				const bool bConfigured = ConfigureExecutionVm(Vm);
				Vm.SetDebugger(DebugScope.GetDebugger());
				const bool bExecuted = bConfigured && Vm.ExecuteBoundInstanceFunction(
				    *Module, Object, FunctionIt->second,
				    (bHotReloadEnabled || PendingHotReloadRollback) ? HotReloadState.get() : nullptr,
				    &EffectiveBindings().GetNativeFunctions(), PendingHotReloadRollback.get());
				DebugScope.Finish();
				Result = FinishVmCall(Vm, Bridge.ErrorStream(), &LastJitStats, &LastJitDiagnostics);
				if (!bExecuted && Result.bSuccess)
				{
					Result.bSuccess = false;
					Result.ExitCode = 1;
					Result.Error = Vm.GetError();
					Result.Diagnostics = {Result.Error};
				}
				if (!Result.bSuccess)
				{
					SettlePendingHotReload(Result, Bridge.ErrorStream());
					return Result;
				}
			}

			Result.ExitCode = 0;
			SettlePendingHotReload(Result, Bridge.ErrorStream());
			return Result;
		};
		FRunResult Completed = ValueSafePoint.Ready() ? Dispatch() : FRunResult{};
		return ValueSafePoint.Finish(std::move(Completed));
	}

	bool VeroScriptSession::RegisterExternalSerializer(
	    std::string TypeName,
	    FExternalSerializerDesc Serializer,
	    std::string* const OutError)
	{
		if (RejectIfRunning("RegisterExternalSerializer"))
		{
			if (OutError)
			{
				*OutError = "session is running";
			}
			return false;
		}
		if (TypeName.empty() || !Serializer.Serialize || !Serializer.Deserialize)
		{
			if (OutError)
			{
				*OutError = "external serializer requires a type name and both callbacks";
			}
			return false;
		}
		if (!ExternalSerializers.emplace(std::move(TypeName), std::move(Serializer)).second)
		{
			if (OutError)
			{
				*OutError = "external serializer is already registered for this type";
			}
			return false;
		}
		if (OutError)
		{
			OutError->clear();
		}
		return true;
	}

	bool VeroScriptSession::SerializeObject(
	    const FObjectHandle& Object,
	    std::vector<std::uint8_t>& OutBytes,
	    std::uint32_t& OutVersion,
	    std::string& OutError) const
	{
		if (RequiresReset())
		{
			OutError = MakeDiagnostic("diag.execution_state_reset_required").Message;
			return false;
		}
		OutBytes.clear();
		OutVersion = 0;
		OutError.clear();
		if (bRunning || !Module || !Object)
		{
			OutError = bRunning
			    ? "cannot serialize an object while the session is running"
			    : "object serialization requires a loaded module and object";
			return false;
		}
		const FSessionRunningGuard RunningGuard(bRunning);
		const FExternalSerializerDesc* const Serializer =
		    FindExternalSerializer(ExternalSerializers, *Module, Object->GetTypeName());
		if (!Serializer)
		{
			OutError = "no external serializer registered for type: " + Object->GetTypeName();
			return false;
		}
		FHostStreamBridge Bridge(Host);
		VeroScriptVm Vm(Bridge.OutputStream(), InputStreamOrCin(), Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
		if (!ConfigureExecutionVm(Vm) ||
			!Vm.ValidateObjectLayout(*Module, Object, &EffectiveBindings().GetNativeFunctions()))
		{
			OutError = Vm.GetError().Message.empty()
			    ? "object layout does not match the loaded module"
			    : Vm.GetError().Message;
			return false;
		}
		std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
		if (!ModuleMeta::FindTypeIndex(*Module, Object->GetTypeName(), TypeIndex) || TypeIndex >= Module->Types.size())
		{
			OutError = "object layout does not match the loaded module";
			return false;
		}
		FExternalObjectView View;
		View.TypeName = Object->GetTypeName();
		View.MutationVersion = Object->MutationVersion;
		View.Fields.reserve(Object->Fields.size());
		for (std::size_t Index = 0; Index < Object->Fields.size(); ++Index)
		{
			const FBytecodeFieldDesc& Field = Module->Types[TypeIndex].Fields[Index];
			const FValue* const VisibleValue =
			    UnwrapClosureReferenceCell(&Object->Fields[Index]);
			if (!VisibleValue)
			{
				OutError = "object contains an invalid reference cell: " + Field.Name;
				return false;
			}
			View.Fields.push_back({Field.Name,
			    Field.DeclaredTypeName.empty() ? Field.TypeName : Field.DeclaredTypeName,
			    Field.TypeName,
			    static_cast<std::uint16_t>(Index),
			    VisibleValue});
		}
		OutVersion = Serializer->Version;
		try
		{
			if (Serializer->Serialize(View, OutBytes, OutError))
			{
				return true;
			}
		}
		catch (const std::exception& Exception)
		{
			OutError = std::string("external serializer threw: ") + Exception.what();
		}
		catch (...)
		{
			OutError = "external serializer threw";
		}
		OutBytes.clear();
		OutVersion = 0;
		if (OutError.empty())
		{
			OutError = "external serializer failed";
		}
		return false;
	}

	bool VeroScriptSession::DeserializeObject(
	    const FObjectHandle& Object,
	    const std::uint8_t* const Bytes,
	    const std::size_t ByteCount,
	    const std::uint32_t EncodedVersion,
	    std::string& OutError)
	{
		if (RequiresReset())
		{
			OutError = MakeDiagnostic("diag.execution_state_reset_required").Message;
			return false;
		}
		OutError.clear();
		if (bRunning || !Module || !Object || (ByteCount != 0 && Bytes == nullptr))
		{
			OutError = bRunning
			    ? "cannot deserialize an object while the session is running"
			    : "object deserialization requires a loaded module, object, and valid byte view";
			return false;
		}
		const FSessionRunningGuard RunningGuard(bRunning);
		const FExternalSerializerDesc* const Serializer =
		    FindExternalSerializer(ExternalSerializers, *Module, Object->GetTypeName());
		if (!Serializer)
		{
			OutError = "no external serializer registered for type: " + Object->GetTypeName();
			return false;
		}
		FHostStreamBridge Bridge(Host);
		VeroScriptVm Vm(Bridge.OutputStream(), InputStreamOrCin(), Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
		if (!ConfigureExecutionVm(Vm) ||
			!Vm.ValidateObjectLayout(*Module, Object, &EffectiveBindings().GetNativeFunctions()))
		{
			OutError = Vm.GetError().Message.empty()
			    ? "object layout does not match the loaded module"
			    : Vm.GetError().Message;
			return false;
		}
		std::vector<FExternalFieldAssignment> Assignments;
		try
		{
			if (!Serializer->Deserialize(
			        Bytes, ByteCount, EncodedVersion, Assignments, OutError))
			{
				if (OutError.empty())
				{
					OutError = "external deserializer failed";
				}
				return false;
			}
		}
		catch (const std::exception& Exception)
		{
			OutError = std::string("external deserializer threw: ") + Exception.what();
			return false;
		}
		catch (...)
		{
			OutError = "external deserializer threw";
			return false;
		}
		std::vector<std::pair<std::string, FValue>> Fields;
		Fields.reserve(Assignments.size());
		for (FExternalFieldAssignment& Assignment : Assignments)
		{
			Fields.emplace_back(std::move(Assignment.Name), std::move(Assignment.Value));
		}
		FValueOperationScope ValueScope(Vm, HotReloadState.get());
		if (!Vm.WriteObjectFields(*Module, Object, Fields, &EffectiveBindings().GetNativeFunctions()))
		{
			OutError = Vm.GetError().Message.empty()
			    ? "external field commit failed"
			    : Vm.GetError().Message;
			return false;
		}
		return true;
	}

	bool VeroScriptSession::EnumerateObjectReferences(
	    const FObjectHandle& Object,
	    const FExternalReferenceVisitor& Visitor,
	    std::string& OutError) const
	{
		if (RequiresReset())
		{
			OutError = MakeDiagnostic("diag.execution_state_reset_required").Message;
			return false;
		}
		OutError.clear();
		if (bRunning || !Object || !Visitor)
		{
			OutError = bRunning
			    ? "cannot enumerate references while the session is running"
			    : "reference enumeration requires an object and visitor";
			return false;
		}
		const FSessionRunningGuard RunningGuard(bRunning);
		std::unordered_set<const void*> Visited;
		try
		{
			// Pin the root and borrow graph slots without cloning value-owned arrays.
			const FValue RootValue = Object;
			const auto& Root = std::get<FObjectHandle>(RootValue);
			std::vector<const FValue*> Pending;
			// Host bases, map/set keys and captured receivers have no FValue slot.
			// deque keeps their lightweight handle wrappers stable as more are added.
			std::deque<FValue> HandleValues;
			const auto EnqueueValue = [&](const FValue& Value)
			{
				std::visit([&](const auto& Storage)
				{
					using T = std::decay_t<decltype(Storage)>;
					if constexpr (std::is_same_v<T, FNativeArray>)
					{
						if (!Storage.Storage().Elements.empty())
						{
							Pending.push_back(&Value);
						}
					}
					else if constexpr (std::is_same_v<T, FScriptVariantHandle> || std::is_same_v<T, FObjectHandle> || std::is_same_v<T, FHostObjectHandle> || std::is_same_v<T, FDynamicArrayHandle> || std::is_same_v<T, FQueueHandle> || std::is_same_v<T, FStackHandle> || std::is_same_v<T, FDequeHandle> || std::is_same_v<T, FMapHandle> || std::is_same_v<T, FOrderedMapHandle> || std::is_same_v<T, FSetHandle> || std::is_same_v<T, FFunctionHandle>)
					{
						if (Storage)
						{
							Pending.push_back(&Value);
						}
					}
				}, Value);
			};
			const auto EnqueueHandle = [&](const auto& Handle)
			{
				HandleValues.emplace_back(Handle);
				EnqueueValue(HandleValues.back());
			};
			const auto VisitValue = [&](const FValue& Value)
			{
				if (const auto* Handle = std::get_if<FScriptVariantHandle>(&Value))
				{
					if (!*Handle || !Visited.insert(Handle->get()).second) return;
					// Borrow the immutable payload: a snapshot would clone value types
					// and can require VM callbacks, neither of which belongs in enumeration.
					EnqueueValue((*Handle)->Value);
					return;
				}
				if (const auto* Handle = std::get_if<FObjectHandle>(&Value))
				{
					if (!*Handle || !Visited.insert(Handle->get()).second)
					{
						return;
					}
					Visitor({EExternalReferenceKind::ScriptObject, *Handle, {}});
					if ((*Handle)->GetHostBase())
					{
						EnqueueHandle((*Handle)->GetHostBase());
					}
					for (const FValue& Field : (*Handle)->Fields)
					{
						EnqueueValue(Field);
					}
					return;
				}
				if (const auto* Handle = std::get_if<FHostObjectHandle>(&Value))
				{
					if (!*Handle || !Visited.insert(Handle->get()).second)
					{
						return;
					}
					Visitor({EExternalReferenceKind::HostObject, {}, *Handle});
					return;
				}
				if (const auto* Array = std::get_if<FNativeArray>(&Value))
				{
					for (const FValue& Element : Array->Storage().Elements)
					{
						EnqueueValue(Element);
					}
					return;
				}
				const auto VisitSequence = [&](const auto& Handle)
				{
					if (!Handle || !Visited.insert(Handle.get()).second)
					{
						return;
					}
					for (const FValue& Element : Handle->Elements)
					{
						EnqueueValue(Element);
					}
				};
				if (const auto* Handle = std::get_if<FDynamicArrayHandle>(&Value))
				{
					VisitSequence(*Handle);
					return;
				}
				if (const auto* Handle = std::get_if<FQueueHandle>(&Value))
				{
					VisitSequence(*Handle);
					return;
				}
				if (const auto* Handle = std::get_if<FStackHandle>(&Value))
				{
					VisitSequence(*Handle);
					return;
				}
				if (const auto* Handle = std::get_if<FDequeHandle>(&Value))
				{
					VisitSequence(*Handle);
					return;
				}
				const auto VisitMap = [&](const auto& Handle)
				{
					if (!Handle || !Visited.insert(Handle.get()).second)
					{
						return;
					}
					const auto VisitEntries = [&](const auto& Entries)
					{
						for (const auto& Entry : Entries)
						{
							EnqueueValue(Entry.second);
						}
					};
					VisitEntries(Handle->IntEntries);
					VisitEntries(Handle->UIntEntries);
					VisitEntries(Handle->Int64Entries);
					VisitEntries(Handle->UInt64Entries);
					VisitEntries(Handle->StrEntries);
					VisitEntries(Handle->WStrEntries);
					VisitEntries(Handle->EnumEntries);
					VisitEntries(Handle->HostEntries);
					for (const FHostObjectHandle& Key : Handle->HostKeyOrder)
					{
						EnqueueHandle(Key);
					}
				};
				if (const auto* Handle = std::get_if<FMapHandle>(&Value))
				{
					if (!*Handle || !Visited.insert(Handle->get()).second)
					{
						return;
					}
					const auto VisitEntries = [&](const auto& Entries)
					{
						for (const auto& Entry : Entries)
						{
							EnqueueValue(Entry.second);
						}
					};
					VisitEntries((*Handle)->IntEntries);
					VisitEntries((*Handle)->UIntEntries);
					VisitEntries((*Handle)->Int64Entries);
					VisitEntries((*Handle)->UInt64Entries);
					VisitEntries((*Handle)->StrEntries);
					VisitEntries((*Handle)->WStrEntries);
					VisitEntries((*Handle)->EnumEntries);
					VisitEntries((*Handle)->HostEntries);
					for (const auto& Entry : (*Handle)->HostEntries)
					{
						EnqueueHandle(Entry.first);
					}
					return;
				}
				if (const auto* Handle = std::get_if<FOrderedMapHandle>(&Value))
				{
					VisitMap(*Handle);
					return;
				}
				if (const auto* Handle = std::get_if<FSetHandle>(&Value))
				{
					if (!*Handle || !Visited.insert(Handle->get()).second)
					{
						return;
					}
					for (const FHostObjectHandle& Element : (*Handle)->HostElements)
					{
						EnqueueHandle(Element);
					}
					return;
				}
				if (const auto* Handle = std::get_if<FFunctionHandle>(&Value))
				{
					if (!*Handle || !Visited.insert(Handle->get()).second)
					{
						return;
					}
					if ((*Handle)->CapturedThis)
					{
						EnqueueHandle((*Handle)->CapturedThis);
					}
					for (const FValue& Capture : (*Handle)->Captures)
					{
						EnqueueValue(Capture);
					}
				}
			};
			Visited.insert(Root.get());
			if (Root->GetHostBase())
			{
				EnqueueHandle(Root->GetHostBase());
			}
			for (const FValue& Field : Root->Fields)
			{
				EnqueueValue(Field);
			}
			std::reverse(Pending.begin(), Pending.end());
			while (!Pending.empty())
			{
				const FValue* const Value = Pending.back();
				Pending.pop_back();
				const auto ChildrenBegin = Pending.size();
				VisitValue(*Value);
				// Children were collected in the original order. Reverse only this
				// stack segment so each subtree still completes before the next sibling.
				std::reverse(Pending.begin() + static_cast<std::ptrdiff_t>(ChildrenBegin), Pending.end());
			}
			return true;
		}
		catch (const std::exception& Exception)
		{
			OutError = std::string("reference visitor threw: ") + Exception.what();
		}
		catch (...)
		{
			OutError = "reference visitor threw";
		}
		return false;
	}

	FRunResult VeroScriptSession::CallFunction(
	    const std::string_view FunctionName,
	    std::vector<FValue>& Arguments,
	    FValue* const OutReturn,
	    const EMissingMethodPolicy MissingPolicy)
	{
		return CallFunctionInternal(
		    FunctionName,
		    Arguments,
		    &Arguments,
		    OutReturn,
		    MissingPolicy);
	}

	bool VeroScriptSession::HasFreeFunction(
	    const std::string_view FunctionName,
	    const std::uint8_t ArgumentCount) const
	{
		if (!Module || FunctionName.empty())
		{
			return false;
		}
		return ModuleMeta::HasFreeFunction(*Module, FunctionName, ArgumentCount);
	}

	FRunResult VeroScriptSession::GetField(
	    const FObjectHandle& Object,
	    const std::string_view FieldName,
	    FValue& OutValue)
	{
		if (RequiresReset()) return MakeResetRequiredResult();
		FSessionOutputScope<FValue> Output(&OutValue, bRunning);
		if (!Module)
		{
			return MakeSessionNoModuleResult(Host);
		}
		if (bRunning)
		{
			return MakeSessionReentryResult(Host, "GetField");
		}
		if (!Object)
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.getfield_requires_object");
			Result.Error.Location.FileLabel = "<session>";
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		if (FieldName.empty())
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.getfield_requires_a_non_empty_name");
			Result.Error.Location.FileLabel = "<session>";
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		FHostStreamBridge Bridge(Host);
		const FSessionRunningGuard RunningGuard(bRunning);
		FValueSafePoint Inputs(*this, nullptr, &OutValue, &Object);
		FValueSafePoint PendingOutput(*this, nullptr, Output.Get());
		VeroScriptVm Vm(Bridge.OutputStream(), InputStreamOrCin(), Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
		if (!ConfigureExecutionVm(Vm) || !Vm.ReadObjectField(*Module, Object, FieldName, Output.Value,
		        &EffectiveBindings().GetNativeFunctions()))
		{
			FRunResult Result;
			Result.ExitCode = 4;
			Result.Error = MakeDiagnostic(
			    "diag.getfield_unknown_field",
			    FSourceLocation{"<session>", 1, 1},
			    {FieldName});
			if (Vm.HasError() && Vm.GetError().MessageId != "diag.unknown_field")
			{
				Result.ExitCode = 1;
				Result.Error = Vm.GetError();
			}
			Result.Diagnostics = {Result.Error};
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		FRunResult Result;
		Result.bSuccess = true;
		Result.ExitCode = 0;
		return Result;
	}

	FRunResult VeroScriptSession::SetField(
	    const FObjectHandle& Object,
	    const std::string_view FieldName,
	    const FValue& Value)
	{
		if (RequiresReset()) return MakeResetRequiredResult();
		if (!Module)
		{
			return MakeSessionNoModuleResult(Host);
		}
		if (bRunning)
		{
			return MakeSessionReentryResult(Host, "SetField");
		}
		if (!Object)
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.setfield_requires_object");
			Result.Error.Location.FileLabel = "<session>";
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		if (FieldName.empty())
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.setfield_requires_a_non_empty_name");
			Result.Error.Location.FileLabel = "<session>";
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		const FSessionRunningGuard RunningGuard(bRunning);
		FHostStreamBridge Bridge(Host);
		VeroScriptVm Vm(Bridge.OutputStream(), InputStreamOrCin(), Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
		if (!ConfigureExecutionVm(Vm)) return FinishVmCall(Vm, Bridge.ErrorStream(), nullptr);
		FValueOperationScope ValueScope(Vm, HotReloadState.get());
		if (!Vm.WriteObjectField(*Module, Object, FieldName, Value,
		        &EffectiveBindings().GetNativeFunctions()))
		{
			FRunResult Result;
			Result.ExitCode = 4;
			Result.Error = MakeDiagnostic(
			    "diag.setfield_unknown_field",
			    FSourceLocation{"<session>", 1, 1},
			    {FieldName});
			if (Vm.HasError() && Vm.GetError().MessageId != "diag.unknown_field")
			{
				Result.ExitCode = 1;
				Result.Error = Vm.GetError();
			}
			Result.Diagnostics = {Result.Error};
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		FRunResult Result;
		Result.bSuccess = true;
		Result.ExitCode = 0;
		return Result;
	}

	FRunResult VeroScriptSession::GetGlobal(
	    const std::string_view GlobalName,
	    FValue& OutValue)
	{
		if (RequiresReset()) return MakeResetRequiredResult();
		FSessionOutputScope<FValue> Output(&OutValue, bRunning);
		if (!Module)
		{
			return MakeSessionNoModuleResult(Host);
		}
		if (bRunning)
		{
			return MakeSessionReentryResult(Host, "GetGlobal");
		}
		if (!bHotReloadEnabled || !HotReloadState)
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.getglobal_requires_hot_reload");
			Result.Error.Location.FileLabel = "<session>";
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		if (HotReloadState->bNeedsModuleInitialization)
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.getglobal_requires_initialized_state");
			Result.Error.Location.FileLabel = "<session>";
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		if (GlobalName.empty())
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.getglobal_requires_a_non_empty_name");
			Result.Error.Location.FileLabel = "<session>";
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		const auto SlotIt = Module->GlobalSlots.find(std::string(GlobalName));
		std::uint16_t Slot = static_cast<std::uint16_t>(-1);
		bool bAmbiguousGlobal = false;
		if (SlotIt != Module->GlobalSlots.end())
		{
			Slot = SlotIt->second;
		}
		else
		{
			std::vector<std::uint16_t> LeafHits;
			for (const auto& Entry : Module->GlobalSlots)
			{
				if (ModuleMeta::TypeNameMatches(Entry.first, GlobalName))
				{
					LeafHits.push_back(Entry.second);
				}
			}
			if (LeafHits.size() == 1)
			{
				Slot = LeafHits[0];
			}
			else if (LeafHits.size() > 1)
			{
				bAmbiguousGlobal = true;
			}
		}
		if (bAmbiguousGlobal)
		{
			FRunResult Result;
			Result.ExitCode = 4;
			Result.Error = MakeDiagnostic(
			    "diag.getglobal_ambiguous_global",
			    FSourceLocation{"<session>", 1, 1},
			    {GlobalName});
			Result.Diagnostics = {Result.Error};
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		if (Slot == static_cast<std::uint16_t>(-1) || Slot >= HotReloadState->Globals.size() || (Slot < Module->GlobalMetadata.size() && Module->GlobalMetadata[Slot].bLocalStatic))
		{
			FRunResult Result;
			Result.ExitCode = 4;
			Result.Error = MakeDiagnostic(
			    "diag.getglobal_unknown_global",
			    FSourceLocation{"<session>", 1, 1},
			    {GlobalName});
			Result.Diagnostics = {Result.Error};
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		const FSessionRunningGuard RunningGuard(bRunning);
		const FValue* const Visible =
		    UnwrapClosureReferenceCell(&HotReloadState->Globals[Slot]);
		if (!Visible)
		{
			FRunResult Result;
			Result.ExitCode = 1;
			Result.Error = MakeDiagnostic("diag.invalid_ref_binding");
			Result.Error.Location.FileLabel = "<session>";
			Result.Diagnostics = {Result.Error};
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		Output.Value = *Visible;
		FRunResult Result;
		Result.bSuccess = true;
		Result.ExitCode = 0;
		return Result;
	}

	FRunResult VeroScriptSession::SetGlobal(
	    const std::string_view GlobalName,
	    const FValue& Value)
	{
		if (RequiresReset()) return MakeResetRequiredResult();
		if (!Module)
		{
			return MakeSessionNoModuleResult(Host);
		}
		if (bRunning)
		{
			return MakeSessionReentryResult(Host, "SetGlobal");
		}
		if (!bHotReloadEnabled || !HotReloadState)
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.setglobal_requires_hot_reload");
			Result.Error.Location.FileLabel = "<session>";
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		if (HotReloadState->bNeedsModuleInitialization)
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.setglobal_requires_initialized_state");
			Result.Error.Location.FileLabel = "<session>";
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		if (GlobalName.empty())
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.setglobal_requires_a_non_empty_name");
			Result.Error.Location.FileLabel = "<session>";
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		const auto SlotIt = Module->GlobalSlots.find(std::string(GlobalName));
		std::uint16_t Slot = static_cast<std::uint16_t>(-1);
		bool bAmbiguousGlobal = false;
		if (SlotIt != Module->GlobalSlots.end())
		{
			Slot = SlotIt->second;
		}
		else
		{
			std::vector<std::uint16_t> LeafHits;
			for (const auto& Entry : Module->GlobalSlots)
			{
				if (ModuleMeta::TypeNameMatches(Entry.first, GlobalName))
				{
					LeafHits.push_back(Entry.second);
				}
			}
			if (LeafHits.size() == 1)
			{
				Slot = LeafHits[0];
			}
			else if (LeafHits.size() > 1)
			{
				bAmbiguousGlobal = true;
			}
		}
		if (bAmbiguousGlobal)
		{
			FRunResult Result;
			Result.ExitCode = 4;
			Result.Error = MakeDiagnostic(
			    "diag.setglobal_ambiguous_global",
			    FSourceLocation{"<session>", 1, 1},
			    {GlobalName});
			Result.Diagnostics = {Result.Error};
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		if (Slot == static_cast<std::uint16_t>(-1) || Slot >= HotReloadState->Globals.size() || (Slot < Module->GlobalMetadata.size() && Module->GlobalMetadata[Slot].bLocalStatic))
		{
			FRunResult Result;
			Result.ExitCode = 4;
			Result.Error = MakeDiagnostic(
			    "diag.setglobal_unknown_global",
			    FSourceLocation{"<session>", 1, 1},
			    {GlobalName});
			Result.Diagnostics = {Result.Error};
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		const FSessionRunningGuard RunningGuard(bRunning);
		FHostStreamBridge Bridge(Host);
		VeroScriptVm Vm(Bridge.OutputStream(), InputStreamOrCin(), Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
		if (!ConfigureExecutionVm(Vm)) return FinishVmCall(Vm, Bridge.ErrorStream(), nullptr);
		FValueOperationScope ValueScope(Vm, HotReloadState.get());
		if (!Vm.AssignRetainedValue(*Module, Vm.Globals[Slot], Value,
		        &EffectiveBindings().GetNativeFunctions()))
		{
			FRunResult Result;
			Result.ExitCode = 1;
			Result.Error = Vm.GetError();
			Result.Diagnostics = {Result.Error};
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			return Result;
		}
		FRunResult Result;
		Result.bSuccess = true;
		Result.ExitCode = 0;
		return Result;
	}

	bool VeroScriptSession::EnumerateTypes(
	    std::vector<FLoadedTypeInfo>& Out,
	    const bool bHostSubclassesOnly) const
	{
		Out.clear();
		if (!Module)
		{
			return false;
		}
		ModuleMeta::EnumerateTypes(*Module, Out, bHostSubclassesOnly);
		return true;
	}

	bool VeroScriptSession::EnumerateFields(
	    const std::string_view TypeName,
	    std::vector<FLoadedFieldInfo>& Out,
	    const bool bDeclaredOnly) const
	{
		Out.clear();
		if (!Module)
		{
			return false;
		}
		return ModuleMeta::EnumerateFields(*Module, TypeName, Out, bDeclaredOnly);
	}

	bool VeroScriptSession::HasInstanceMethodOnType(
	    const std::string_view TypeName,
	    const std::string_view MethodName,
	    const std::uint8_t ArgumentCount) const
	{
		if (!Module)
		{
			return false;
		}
		return ModuleMeta::HasInstanceMethodOnType(
		    *Module,
		    TypeName,
		    MethodName,
		    ArgumentCount);
	}

	bool VeroScriptSession::EnumerateInstanceMethods(
	    const std::string_view TypeName,
	    std::vector<FInstanceMethodInfo>& Out,
	    const bool bIncludeInherited,
	    const bool bIncludeConstructors,
	    const bool bIncludeDestructors) const
	{
		Out.clear();
		if (!Module)
		{
			return false;
		}
		return ModuleMeta::EnumerateInstanceMethods(
		    *Module,
		    TypeName,
		    Out,
		    bIncludeInherited,
		    bIncludeConstructors,
		    bIncludeDestructors);
	}

	bool VeroScriptSession::EnumerateFreeFunctions(std::vector<FFreeFunctionInfo>& Out) const
	{
		Out.clear();
		if (!Module)
		{
			return false;
		}
		ModuleMeta::EnumerateFreeFunctions(*Module, Out);
		return true;
	}

	bool VeroScriptSession::EnumerateGlobals(std::vector<FLoadedGlobalInfo>& Out) const
	{
		Out.clear();
		if (!Module)
		{
			return false;
		}
		ModuleMeta::EnumerateGlobals(*Module, Out);
		return true;
	}

	bool VeroScriptSession::EnumerateStaticMethods(
	    const std::string_view TypeName,
	    std::vector<FStaticMethodInfo>& Out) const
	{
		Out.clear();
		if (!Module)
		{
			return false;
		}
		return ModuleMeta::EnumerateStaticMethods(*Module, TypeName, Out);
	}

	bool VeroScriptSession::EnumerateInterfaces(std::vector<FLoadedInterfaceInfo>& Out) const
	{
		Out.clear();
		if (!Module)
		{
			return false;
		}
		ModuleMeta::EnumerateInterfaces(*Module, Out);
		return true;
	}

	bool VeroScriptSession::EnumerateEnums(std::vector<FLoadedEnumInfo>& Out) const
	{
		Out.clear();
		if (!Module)
		{
			return false;
		}
		ModuleMeta::EnumerateEnums(*Module, Out);
		return true;
	}

	void VeroScriptSession::Clear()
	{
		if (RejectIfRunning("Clear"))
		{
			return;
		}
		ActiveExecutionCleanup.reset();
		RestorePendingHotReload();
		if (PendingHotReloadRollback) return;
		std::string RootError;
		if (!PrepareValueRootReplacement(RootError))
		{
			if (!RootError.empty())
			{
				FHostStreamBridge Bridge(Host);
				FScriptError Failure;
				Failure.Message = std::move(RootError);
				Failure.Location.FileLabel = "<value-cleanup>";
				EngineInternal::WriteError(Bridge.ErrorStream(), Failure);
			}
			return;
		}
		DiscardRetainedCallables(Module.get(), ProgramGeneration);
		Executor->AbandonMutexes();
		DiscardHotReloadState();
		ReusableJit.reset();
		Module.reset();
		AdvanceProgramGeneration();
		FilePath.clear();
		ReloadSourcePath.clear();
		LoadedPackageKind.reset();
		LastJitStats = {};
		LastJitDiagnostics = {};
		bExecutionResetRequired = false;
		NotifyDebugProgramChanged();
	}

	bool VeroScriptSession::IsLoaded() const
	{
		return static_cast<bool>(Module);
	}

	std::vector<std::string_view> VeroScriptSession::MakeImportPathViews() const
	{
		std::vector<std::string_view> Views;
		Views.reserve(ImportPaths.size());
		for (const std::string& Path : ImportPaths)
		{
			Views.emplace_back(Path);
		}
		return Views;
	}

	std::vector<std::string_view> VeroScriptSession::MakeImplicitImportPathViews() const
	{
		std::vector<std::string_view> Views;
		Views.reserve(ImplicitImportPaths.size());
		for (const std::string& Path : ImplicitImportPaths)
		{
			Views.emplace_back(Path);
		}
		return Views;
	}

	std::vector<std::string_view> VeroScriptSession::MakeScriptArgViews() const
	{
		std::vector<std::string_view> Views;
		Views.reserve(ScriptArgs.size());
		for (const std::string& Arg : ScriptArgs)
		{
			Views.emplace_back(Arg);
		}
		return Views;
	}

	std::vector<std::string_view> VeroScriptSession::MakeDefineViews() const
	{
		std::vector<std::string_view> Views;
		Views.reserve(Defines.size());
		for (const std::string& Define : Defines)
		{
			Views.emplace_back(Define);
		}
		return Views;
	}

	std::vector<std::string_view> VeroScriptSession::MakeStdlibSearchPathViews() const
	{
		std::vector<std::string_view> Views;
		Views.reserve(StdlibSearchPaths.size());
		for (const std::string& Path : StdlibSearchPaths)
		{
			Views.emplace_back(Path);
		}
		return Views;
	}

	FScriptCompileExtensionsOptions VeroScriptSession::MakeScriptCompileExtensionOptions() const
	{
		auto Options = ScriptCompileExtensions;
		if (!Options.VmBackendFactory && Options.VmBackendPath.empty())
		{
			Options.VmBackendFactory = VmExecutionDomain->GetFactory();
		}
		if (!Options.ExecutionControl)
		{
			Options.ExecutionControl = ExecutionControl;
		}

		return Options;
	}

	std::istream* VeroScriptSession::ResolveInputStream() const
	{
		if (OwnedInputStream)
		{
			return OwnedInputStream.get();
		}
		return HostInputStream;
	}

	std::istream& VeroScriptSession::InputStreamOrCin() const
	{
		if (std::istream* const Stream = ResolveInputStream())
		{
			return *Stream;
		}
		return std::cin;
	}

	FRunResult VeroScriptSession::CommitModule(
	    std::unique_ptr<FBytecodeModule>& NewModule,
	    std::string NewFilePath,
	    const FRunResult& CompileResult,
	    const std::optional<EPackageKind> NewPackageKind,
	    std::shared_ptr<const FModuleEnvironment> NewEnvironment)
	{
		if (!CompileResult.bSuccess || !NewModule)
		{
			return CompileResult;
		}
		std::string RootError;
		if (!PrepareValueRootReplacement(RootError))
		{
			FRunResult Failed;
			Failed.ExitCode = 2;
			Failed.Error = MakeDiagnostic("diag.failed_to_load_module");
			Failed.Error.Message = RootError.empty() ? "release external value roots before replacing the module" : RootError;
			Failed.Diagnostics = {Failed.Error};
			return Failed;
		}
		// Cleanup may restore a staged generation and its environment. Preserve
		// the environment under which this replacement was actually compiled.
		auto InstallationEnvironment = NewEnvironment ? std::move(NewEnvironment) : ModuleEnvironment;
		if (Module)
		{
			DiscardHotReloadState();
		}
		if (PendingHotReloadRollback)
		{
			FRunResult Failed;
			Failed.ExitCode = 2;
			Failed.Error = MakeDiagnostic("diag.failed_to_load_module");
			Failed.Error.Message = "could not restore the pending replacement before loading a module";
			Failed.Diagnostics = {Failed.Error};
			return Failed;
		}
		if (bHotReloadEnabled && !HotReloadState)
		{
			HotReloadState = std::make_unique<FHotReloadState>();
		}

		DiscardRetainedCallables(Module.get(), ProgramGeneration);
		ReusableJit.reset();
		Module = std::move(NewModule);
		ModuleEnvironment = std::move(InstallationEnvironment);
		AdvanceProgramGeneration();
		FilePath = std::move(NewFilePath);
		ReloadSourcePath = FilePath;
		LoadedPackageKind = NewPackageKind;
		NotifyDebugProgramChanged();
		return CompileResult;
	}

	FRunResult VeroScriptSession::StageReloadedModule(
	    std::unique_ptr<FBytecodeModule>& NewModule,
	    std::string LogicalPath,
	    const FRunResult& SuccessResult,
	    const bool bForceTransaction,
	    const std::optional<EPackageKind> NewPackageKind,
	    std::shared_ptr<const FModuleEnvironment> NewEnvironment)
	{
		if (!NewModule)
		{
			FRunResult Result;
			Result.ExitCode = 2;
			Result.Error = MakeDiagnostic("diag.failed_to_load_module");
			Result.Error.Location.FileLabel = "<session>";
			return Result;
		}

		const bool bUseTransaction = bHotReloadEnabled || bForceTransaction;
		std::string RootError;
		if (!PrepareValueRootReplacement(RootError, bUseTransaction))
		{
			FRunResult Failed;
			Failed.ExitCode = 2;
			Failed.Error = MakeDiagnostic("diag.failed_to_load_module");
			Failed.Error.Message = RootError.empty() ? "release external value roots before reloading the module" : RootError;
			Failed.Diagnostics = {Failed.Error};
			return Failed;
		}
		if (!bUseTransaction)
		{
			DiscardRetainedCallables(Module.get(), ProgramGeneration);
		}
		bool bCreatedTemporaryState = false;
		if (bUseTransaction && !HotReloadState)
		{
			HotReloadState = std::make_unique<FHotReloadState>();
			bCreatedTemporaryState = bForceTransaction && !bHotReloadEnabled;
		}
		if (bUseTransaction && Module && HotReloadState->Types.empty() && HotReloadState->Functions.empty())
		{
			// A host-only graph can exist without an earlier persistent Run.
			// Its old schema still belongs to the currently loaded generation.
			HotReloadState->Types = Module->Types;
			HotReloadState->ProgramGeneration = ProgramGeneration;
			for (const auto& Function : Module->Functions)
			{
				HotReloadState->Functions.emplace_back(Function, *Module);
			}
		}

		std::unique_ptr<FHotReloadRollback> Rollback;
		bool bMigratedSchema = false;
		struct FRootMigrationGuard
		{
			std::shared_ptr<FValueRootOwner> Owner;
			std::vector<FValueRoot>& Roots;
			std::unique_ptr<FHotReloadState>& State;
			bool Staged = false;
			~FRootMigrationGuard()
			{
				if (!Staged)
				{
					std::string Ignored;
					Owner->CompleteMigration(Roots, nullptr, Ignored);
					Roots.clear();
					if (State)
					{
						State->ExternalRoots.clear();
					}
				}
			}
		} RootMigration{ValueRoots, PendingMigratableRoots, HotReloadState};
		if (bUseTransaction)
		{
			if (!ValueRoots->PrepareMigration(PendingMigratableRoots, HotReloadState->ExternalRoots, RootError))
			{
				FRunResult Failed;
				Failed.ExitCode = 2;
				Failed.Error.Message = RootError;
				Failed.Diagnostics = {Failed.Error};
				return Failed;
			}
			Rollback = std::make_unique<FHotReloadRollback>();
			const bool bPreviouslyEmpty = HotReloadState->Empty();
			if (!bPreviouslyEmpty)
			{
				std::string MigrationError;
				std::uint64_t NextProgramGeneration = ProgramGeneration + 1;
				if (NextProgramGeneration == 0)
				{
					NextProgramGeneration = 1;
				}
				if (!MigrateHotReloadState(*HotReloadState, *NewModule, Rollback.get(), MigrationError,
				        &EffectiveBindings(), NextProgramGeneration, ProgramOwner))
				{
					if (bCreatedTemporaryState)
					{
						HotReloadState.reset();
					}
					FRunResult Result;
					Result.ExitCode = 3;
					Result.Error = MakeDiagnostic(
					    "diag.hot_reload_state_migration_failed",
					    FSourceLocation{
					        LogicalPath.empty() ? "<session>" : LogicalPath,
					        1,
					        1},
					    {MigrationError});
					Result.Diagnostics = SuccessResult.Diagnostics;
					Result.Diagnostics.push_back(Result.Error);
					FHostStreamBridge Bridge(Host);
					EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
					return Result;
				}
				bMigratedSchema = true;
			}
			if (bPreviouslyEmpty)
			{
				// An initialized module can have no globals. Its replacement still
				// needs an initialization transaction, including failure rollback.
				Rollback->PreviousState = *HotReloadState;
				HotReloadState->bNeedsModuleInitialization = true;
			}
			PreviousHotReloadFilePath = FilePath;
			PreviousReloadSourcePath = ReloadSourcePath;
			PreviousHotReloadPackageKind = LoadedPackageKind;
			PreviousHotReloadProgramGeneration = ProgramGeneration;
			PreviousHotReloadModule = std::move(Module);
			PreviousHotReloadEnvironment = ModuleEnvironment;
			PendingHotReloadRollback = std::move(Rollback);
			if (bForceTransaction && !bHotReloadEnabled)
			{
				bTemporaryHotReloadState = true;
			}
		}

		ReusableJit.reset();
		Module = std::move(NewModule);
		if (NewEnvironment)
		{
			ModuleEnvironment = std::move(NewEnvironment);
		}
		FilePath = std::move(LogicalPath);
		LoadedPackageKind = NewPackageKind;
		AdvanceProgramGeneration();
		if (bMigratedSchema)
		{
			// Migration already built the replacement type and function schemas.
			HotReloadState->RememberModuleSchema(*Module, Module);
		}
		RootMigration.Staged = true;
		ReloadStatus.CandidateGeneration = ProgramGeneration;
		ReloadStatus.State = PendingHotReloadRollback ? EReloadState::Pending : EReloadState::Applied;
		NotifyDebugProgramChanged();
		return SuccessResult;
	}

	FRunResult VeroScriptSession::LoadApplicationInternal(std::shared_ptr<const Application::FApplication> App, bool bReload)
	{
		if (RequiresReset()) return MakeResetRequiredResult();
		FRunResult R;
		R.ExitCode = 2;
		#if defined(VEROSCRIPT_WITH_APPLICATION)
		if (RejectIfRunning("LoadApplication", &R)) return R;
		if (!App)
		{
			R.Error.Message = "application is absent";
			return R;
		}
		if (bReload && !BeginReloadOperation()) return MakeResetRequiredResult();
		if (bReload)
		{
			RestorePendingHotReload();
			if (!Module)
			{
				R.Error.Message = "no application is loaded for reload";
				return R;
			}
		}
		Application::FPreparedApplication Prepared;
		Application::FDiagnostic E;
		std::unique_ptr<FBytecodeModule> NewModule;
		{
			const FSessionCallbackGuard CompileGuard(bCompiling);
			const FSessionCallbackGuard CallbackGuard(bRunning);
			if (!App->Prepare(Prepared, E))
			{
				R.Error.Message = E.Code + ": " + E.Message;
				return R;
			}
			if (Prepared.Package)
			{
				NewModule = std::move(Prepared.Package->Module);
			}
			else
			{
				FCompileOptions Options;
				Options.ModuleEnvironment = Prepared.Environment;
				Options.ImportPaths = MakeImportPathViews();
				Options.Defines = MakeDefineViews();
				Options.StdlibSearchPaths = MakeStdlibSearchPathViews();
				Options.ImplicitImportPaths = MakeImplicitImportPathViews();
				Options.PredefinedMacros = PredefinedMacros;
				Options.SourceOverlays = SourceOverlays;
				Options.bAllowCircularImports = bAllowCircularImports;
				Options.bAllowProjectWideAccess = bAllowProjectWideAccess;
				Options.CompileProcessing = CompileProcessing;
				Options.AnnotationProcessing = AnnotationProcessing;
				Options.ScriptCompileExtensions = MakeScriptCompileExtensionOptions();
				Options.Bindings = Prepared.Environment->GetDescription().Bindings ? Prepared.Environment->GetDescription().Bindings.get() : &Bindings;
				auto Compiled = Prepared.CompileSource(Options);
				if (!Compiled.bSuccess)
				{
					R.Error = Compiled.Error;
					R.Diagnostics = std::move(Compiled.Diagnostics);
					R.ExitCode = 3;
					return R;
				}
				NewModule = std::move(Compiled.Module);
			}
		}
		std::string RootError;
		if (!DrainValueRoots(RootError) || ValueRoots->HasExternalRoots(!bReload))
		{
			R.Error.Message = "release external value roots before replacing the application";
			return R;
		}
		if (bReload)
		{
			const auto OldCatalog = ModuleEnvironment ? ModuleEnvironment->GetDescription().Bindings : nullptr;
			if (OldCatalog != Prepared.Environment->GetDescription().Bindings)
			{
				R.Error.Message = "application reload cannot replace the native binding catalog; use LoadApplication";
				return R;
			}
			R.bSuccess = true;
			R.ExitCode = 0;
			R = StageReloadedModule(NewModule, Prepared.LogicalPath, R, true, EPackageKind::Application, Prepared.Environment);
			if (R.bSuccess)
			{
				ReloadSourcePath.clear();
			}
			return R;
		}
		R.bSuccess = true;
		R.ExitCode = 0;
		R = CommitModule(NewModule, Prepared.LogicalPath, R, Prepared.Package ? std::optional<EPackageKind>(EPackageKind::Application) : std::nullopt, Prepared.Environment);
		if (R.bSuccess)
		{
			ReloadSourcePath.clear();
		}
		#else
		(void)App;
		(void)bReload;
		R.Error.Message = "application container support was not built";
		#endif
		return R;
	}

	void VeroScriptSession::ReportNativeError(
	    const std::string_view MessageId,
	    const std::initializer_list<FDiagnosticArg> Args)
	{
		FScriptError Error = MakeDiagnostic(
		    MessageId,
		    FSourceLocation{"<session>", 1, 1},
		    Args);
		FHostStreamBridge Bridge(Host);
		EngineInternal::WriteError(Bridge.ErrorStream(), Error);
	}

	bool VeroScriptSession::RejectIfRunning(const std::string_view Action, FRunResult* const OutResult)
	{
		const auto Reject = [&](FScriptError Error)
		{
			Error.Location.FileLabel = "<session>";
			FHostStreamBridge Bridge(Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Error);
			if (OutResult)
			{
				OutResult->ExitCode = 2;
				OutResult->Error = std::move(Error);
			}
			return true;
		};
		if (bRunning)
		{
			return Reject(Action == "Reload"
			    ? MakeDiagnostic("diag.reload_is_not_allowed_while_run_is_in_progress")
			    : MakeDiagnostic("diag.action_not_allowed_while_run_in_progress", {}, {Action}));
		}

		const bool IsReload = Action.rfind("Reload", 0) == 0;
		if (IsReload || Action == "LoadSource" || Action == "LoadFile" || Action == "LoadPackage" || Action == "LoadApplication" || Action == "CommitCompiledFile" || Action == "Clear")
		{
			std::string RootError;
			if (!DrainValueRoots(RootError) || ValueRoots->HasExternalRoots(!IsReload))
			{
				FScriptError Error;
				Error.Message = RootError.empty()
				    ? "release external value roots before replacing the module"
				    : std::move(RootError);
				return Reject(std::move(Error));
			}
		}
		return false;
	}

	void VeroScriptSession::DiscardHotReloadState()
	{
		RestorePendingHotReload();
		if (PendingHotReloadRollback) return;
		std::string RootError;
		if (!ValueRoots->InvalidateGeneration(RootError) ||
		    !ValueRoots->Drain([this, &RootError](auto& Values, const auto& Keep)
		{
			return CleanValueRoots(Values, Keep, &RootError);
		}, RootError))
		{
			FHostStreamBridge Bridge(Host);
			FScriptError Failure;
			Failure.Message = std::move(RootError);
			EngineInternal::WriteError(Bridge.ErrorStream(), Failure);
			return;
		}
		// Cleanup runs script destructors and native/host callbacks. They must
		// not replace the module, bindings or streams borrowed by the cleanup VM.
		const FSessionCallbackGuard CallbackGuard(bRunning);
		const FSessionCallbackGuard CleanupGuard(bCleaningUp);
		if (!HotReloadState)
		{
			return;
		}
		// Drop the rollback journal first: it intentionally holds duplicate old
		// references, which would otherwise suppress last-owner script destructors.
		PendingHotReloadRollback.reset();
		PreviousHotReloadModule.reset();
		PreviousHotReloadEnvironment.reset();
		PreviousHotReloadProgramGeneration = ProgramGeneration;
		PreviousHotReloadFilePath.clear();
		PreviousReloadSourcePath.clear();
		PreviousHotReloadPackageKind.reset();
		bTemporaryHotReloadState = false;
		if (Module && !HotReloadState->Empty())
		{
			FHostStreamBridge Bridge(Host);
			VeroScriptVm Vm(
			    Bridge.OutputStream(),
			    InputStreamOrCin(),
			    Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
			if (!ConfigureExecutionVm(Vm) || !Vm.DiscardPersistentState(*Module, *HotReloadState,
			        &EffectiveBindings().GetNativeFunctions()))
			{
				EngineInternal::WriteError(Bridge.ErrorStream(), Vm.GetError());
			}
		}
		HotReloadState.reset();
	}

	void VeroScriptSession::DiscardUnsettledHotReloadState(std::vector<FValue>& Roots)
	{
		// A failed rollback can leave values from both layouts. Sever their
		// graphs together, with no active module to replay script destructors.
		AppendRollbackKeepValues(Roots);
		if (HotReloadState)
		{
			for (FValue& Value : HotReloadState->Globals)
			{
				Roots.push_back(std::move(Value));
			}
			for (FValue& Value : HotReloadState->ExternalRoots)
			{
				Roots.push_back(std::move(Value));
			}
		}
		{
			const std::lock_guard<std::mutex> Lock(RetainedCallableMutex);
			for (auto& Entry : RetainedCallables)
			{
				Roots.emplace_back(std::move(Entry.second.Callable));
			}
			RetainedCallables.clear();
		}
		FHostStreamBridge Bridge(Host);
		VeroScriptVm Vm(Bridge.OutputStream(), InputStreamOrCin(), Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
		(void)Vm.DiscardValueRoots(Roots, nullptr);
	}

	bool VeroScriptSession::DiscardRetainedCallables(
	    const FBytecodeModule* const DefiningModule,
	    const std::uint64_t DefiningGeneration,
	    const bool bPreserveValues,
	    FScriptError* const OutError)
	{
		const FSessionCallbackGuard CallbackGuard(bRunning);
		const FSessionCallbackGuard CleanupGuard(bCleaningUp);
		{
			const std::lock_guard<std::mutex> Lock(RetainedCallableMutex);
			// Most reloads have no matching callbacks. Avoid copying live roots
			// (including value-owned native arrays) for an empty cleanup pass.
			if (RetainedCallables.empty() || (DefiningGeneration != 0 && std::none_of(RetainedCallables.begin(), RetainedCallables.end(), [DefiningGeneration](const auto& Entry)
			{
				return Entry.second.Generation == DefiningGeneration;
			})))
			{
				return true;
			}
		}
		const bool bPreserveCurrentValues = bPreserveValues || (DefiningModule && DefiningModule != Module.get() && DefiningGeneration != 0);
		std::vector<FValue> Keep;
		if (bPreserveCurrentValues)
		{
			// Old-generation callbacks may capture objects that were migrated in
			// place. Snapshot every current owner before retiring those callbacks.
			Keep = ValueRoots->SnapshotLiveValues();
			AppendRollbackKeepValues(Keep);
			if (HotReloadState)
			{
				Keep.insert(Keep.end(), HotReloadState->Globals.begin(), HotReloadState->Globals.end());
			}
			for (auto* Scope = ActiveValueSafePoint; Scope; Scope = Scope->Previous)
			{
				if (Scope->Arguments)
				{
					Keep.insert(Keep.end(), Scope->Arguments->begin(), Scope->Arguments->end());
				}
				if (Scope->Return)
				{
					Keep.push_back(*Scope->Return);
				}
				if (Scope->Object && *Scope->Object)
				{
					Keep.emplace_back(*Scope->Object);
				}
			}
		}
		std::vector<FFunctionHandle> Callables;
		{
			std::lock_guard<std::mutex> Lock(RetainedCallableMutex);
			Callables.reserve(RetainedCallables.size());
			if (bPreserveCurrentValues)
			{
				Keep.reserve(Keep.size() + RetainedCallables.size());
			}
			for (auto It = RetainedCallables.begin(); It != RetainedCallables.end();)
			{
				if (DefiningGeneration == 0 || It->second.Generation == DefiningGeneration)
				{
					Callables.push_back(std::move(It->second.Callable));
					It = RetainedCallables.erase(It);
				}
				else
				{
					if (bPreserveCurrentValues)
					{
						Keep.emplace_back(It->second.Callable);
					}
					++It;
				}
			}
		}
		if (!DefiningModule || Callables.empty())
		{
			return true;
		}

		FHostStreamBridge Bridge(Host);
		VeroScriptVm Vm(
		    Bridge.OutputStream(), InputStreamOrCin(), Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
		const bool bConfigured = ConfigureExecutionVm(Vm);
		// A retiring callback can release current-generation native roots. Drain
		// them at the outer Session safe point with their own metadata/globals,
		// never through this old-generation cleanup VM.
		Vm.NativeValueRoots.reset();
		Vm.NativeRootCleanup = {};
		const FWeakSlotScope WeakSlots(Vm, HotReloadState.get());
		bool bCleaned = false;
		const bool bUseCurrentState = DefiningModule == Module.get() && HotReloadState;
		if (bPreserveCurrentValues || bUseCurrentState)
		{
			std::vector<FValue> Values;
			Values.reserve(Callables.size());
			for (auto& Callable : Callables)
			{
				Values.emplace_back(std::move(Callable));
			}
			// Current callbacks can use live globals/statics during cleanup. Old
			// metadata must never borrow current-generation global-slot storage.
			bCleaned = bConfigured && Vm.DiscardExternalValues(*DefiningModule, Values, Keep,
			    &EffectiveBindings().GetNativeFunctions(), bUseCurrentState ? HotReloadState.get() : nullptr);
		}
		else
		{
			bCleaned = bConfigured && Vm.DiscardRetainedCallables(*DefiningModule, Callables,
			    &EffectiveBindings().GetNativeFunctions());
		}
		if (!bCleaned)
		{
			if (OutError)
			{
				*OutError = Vm.GetError();
			}
			EngineInternal::WriteError(Bridge.ErrorStream(), Vm.GetError());
		}
		return bCleaned;
	}

	void VeroScriptSession::RestorePendingHotReload()
	{
		const FSessionCallbackGuard CallbackGuard(bRunning);
		const FSessionCallbackGuard CleanupGuard(bCleaningUp);
		if (!PendingHotReloadRollback || !PreviousHotReloadModule || ReloadStatus.State == EReloadState::ResetRequired)
		{
			return;
		}
		std::string RootError;
		if (!ValueRoots->InvalidateGeneration(RootError) ||
		    !ValueRoots->Drain([this, &RootError](auto& Values, const auto& Keep)
		{
			return CleanValueRoots(Values, Keep, &RootError);
		}, RootError))
		{
			FHostStreamBridge Bridge(Host);
			FScriptError Failure;
			Failure.Message = std::move(RootError);
			EngineInternal::WriteError(Bridge.ErrorStream(), Failure);
			bExecutionResetRequired = true;
			ReloadStatus.State = EReloadState::ResetRequired;
			return;
		}
		// Replacement callbacks can have retained new-generation closures before
		// initialization failed. Retire them with replacement metadata, preserving
		// the live graph that the rollback journal is about to restore.
		if (!DiscardRetainedCallables(Module.get(), ProgramGeneration, true))
		{
			bExecutionResetRequired = true;
			ReloadStatus.State = EReloadState::ResetRequired;
			return;
		}
		if (HotReloadState && Module)
		{
			// Module is still the failed replacement: discarded values need its
			// type metadata so script destructors resolve correctly.
			FHostStreamBridge Bridge(Host);
			VeroScriptVm Vm(
			    Bridge.OutputStream(),
			    InputStreamOrCin(),
			    Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
			if (!ConfigureExecutionVm(Vm) || !Vm.RollbackHotReloadState(*Module, *HotReloadState, *PendingHotReloadRollback,
			        &EffectiveBindings().GetNativeFunctions()))
			{
				EngineInternal::WriteError(Bridge.ErrorStream(), Vm.GetError());
				bExecutionResetRequired = true;
				ReloadStatus.State = EReloadState::ResetRequired;
				return;
			}
		}
		else if (HotReloadState)
		{
			PendingHotReloadRollback->Clear();
			HotReloadState->Clear();
		}
		if (!SettleMigratableRoots(false, RootError)) return;
		ReusableJit.reset();
		Module = std::move(PreviousHotReloadModule);
		ModuleEnvironment = std::move(PreviousHotReloadEnvironment);
		ProgramGeneration = PreviousHotReloadProgramGeneration;
		FilePath = std::move(PreviousHotReloadFilePath);
		ReloadSourcePath = std::move(PreviousReloadSourcePath);
		LoadedPackageKind = PreviousHotReloadPackageKind;
		PreviousHotReloadPackageKind.reset();
		PendingHotReloadRollback.reset();
		if (bTemporaryHotReloadState)
		{
			HotReloadState.reset();
			bTemporaryHotReloadState = false;
		}
		ReloadStatus.State = EReloadState::RolledBack;
		NotifyDebugProgramChanged();
	}

	bool VeroScriptSession::BeginReloadOperation()
	{
		RestorePendingHotReload();
		if (PendingHotReloadRollback || RequiresReset()) return false;
		if (++ReloadOperationId == 0)
		{
			std::terminate();
		}
		ReloadStatus = {};
		ReloadStatus.OperationId = ReloadOperationId;
		ReloadStatus.PreviousGeneration = ProgramGeneration;
		ReloadStatus.State = EReloadState::Rejected;
		return true;
	}

	void VeroScriptSession::AdvanceProgramGeneration()
	{
		ReusableJit.reset();
		if (ReusableExecution)
		{
			ReusableExecution->ClearNativeBindings();
		}
		++ProgramGeneration;
		if (ProgramGeneration == 0)
		{
			ProgramGeneration = 1;
		}
		if (!PendingHotReloadRollback)
		{
			CommittedProgramGeneration = ProgramGeneration;
		}
	}

	bool VeroScriptSession::ConfigureExecutionVm(VeroScriptVm& Vm) const
	{
		Vm.VmExecutionDomain = VmExecutionDomain;
		Vm.ExternalExecutionResetRequired = &bExecutionResetRequired;
		Vm.ExternalExecutionCleanup = &ActiveExecutionCleanup;
		if (bCleaningUp && ActiveValueSafePoint && ActiveExecutionCleanup)
		{
			Vm.ExecutionCleanup = ActiveExecutionCleanup;
			Vm.bBorrowedExecutionCleanup = true;
		}
		{
			std::string Error;
			// Cleanup uses the established VM domain. Resolving an unrelated new
			// execution backend here can block destructors and leave owned cycles alive.
			if (!bCleaningUp && ModuleEnvironment && (!JitBackendFactory || !VmExecutionDomain->Factory || (bVmBackendFallback && !VmExecutionDomain->IsFrozen())) && (BackendDefaultsEnvironment != ModuleEnvironment || BackendDefaultsMode != ExecutionBackend))
			{
				FEngineConfig Requested, Selected;
				Requested.ModuleEnvironment = ModuleEnvironment;
				Requested.ExecutionBackend = ExecutionBackend;
				Requested.bDisableJitBackend = bDisableJitBackend;
				Requested.bClearVmBackend = bDisableEnvironmentVm;
				Requested.JitBackendFactory = JitBackendFactory;
				if (!Requested.bDisableJitBackend && !Requested.JitBackendFactory && !JitBackendSelectionRequest.empty() && ExecutionBackend != EExecutionBackend::Vm && ModuleEnvironment->GetDescription().ExecutionBackends.Jit)
				{
					if (!ModuleEnvironment->GetDescription().ExecutionBackends.Jit(
					        JitBackendSelectionRequest, Requested.JitBackendFactory, Error))
					{
						Vm.JitReportRuntimeError(std::move(Error));
						return false;
					}
					Requested.bDisableJitBackend = !Requested.JitBackendFactory;
				}
				Requested.VmBackendFactory = bVmBackendFallback && !VmExecutionDomain->IsFrozen()
				    ? nullptr
				    : VmExecutionDomain->Factory;
				Requested.FallbackVmBackendFactory = bVmBackendFallback ? VmExecutionDomain->Factory : nullptr;
				if (!ResolveExecutionBackends(Requested, Selected, Error))
				{
					Vm.JitReportRuntimeError(std::move(Error));
					return false;
				}
				// Resolving a new environment can select the already-frozen product
				// VM. Reusing that factory is not a request to change the VM domain.
				const bool bChangeVmSelection =
					(Selected.VmBackendFactory && Selected.VmBackendFactory != VmExecutionDomain->Factory) ||
					(Selected.bClearVmBackend && VmExecutionDomain->Factory);
				if (bChangeVmSelection && !Vm.SetVmBackendFactory(Selected.VmBackendFactory, Error))
				{
					Vm.JitReportRuntimeError(std::move(Error));
					return false;
				}
				JitBackendFactory = std::move(Selected.JitBackendFactory);
				bVmBackendFallback = Selected.VmBackendFactory && Selected.VmBackendFactory == Selected.FallbackVmBackendFactory;
				BackendDefaultsEnvironment = ModuleEnvironment;
				BackendDefaultsMode = ExecutionBackend;
			}
			if (!Vm.SetAotProgram(AotProgram, Error) || !Vm.SetJitBackendFactory(JitBackendFactory, Error) || !Vm.SetJitProgramOwner(Module, Error))
			{
				Vm.JitReportRuntimeError(std::move(Error));
				return false;
			}
			if (!Vm.SetJitResourceLimits({JitCacheLimits.MaxNativeBytes, JitCacheLimits.MaxRetainedPayloadBytes}, Error))
			{
				Vm.JitReportRuntimeError(std::move(Error));
				return false;
			}
		}
		Vm.SetJitPolicy(JitPolicy);
		Vm.SetExecutionBackend(ExecutionBackend);
		Vm.SetMaxCallDepth(MaxCallDepth);
		Vm.SetExecutionLimits(ExecutionLimits);
		Vm.SetExecutionControl(ExecutionControl);
		Vm.SetCapabilities(Capabilities);
		Vm.SetMemoryContext(MemoryContext);
		Vm.SetStandardLibraryLimits(StandardLibraryLimits);
		Vm.SetNumericLimits(NumericLimits);
		Vm.Executor = Executor;
		std::string EnvironmentError;
		if (!Vm.SetModuleEnvironment(ModuleEnvironment, EnvironmentError))
		{
			Vm.JitReportRuntimeError(std::move(EnvironmentError));
			return false;
		}
		Vm.SetMemoryHostBindings(&EffectiveBindings());
		Vm.JitNativeBindingsRevision = EffectiveBindings().GetRevision();
		if (I18nServices || !ModuleEnvironment || I18nLimits.MaxOutputBytes)
		{
			Vm.SetI18nServices(I18nServices ? I18nServices
			        : ModuleEnvironment     ? Vm.GetI18nServices()
			                                : nullptr,
			    I18nLimits);
		}
		Vm.SetInstanceCallInterceptor(InstanceCallInterceptor);
		Vm.SetMutationCallback(MutationCallback);
		Vm.SetProgramGeneration(ProgramGeneration);
		Vm.SetProgramOwner(ProgramOwner);
		Vm.NativeValueRoots = ValueRoots;
		Vm.bNativeRetentionAllowed = !bCleaningUp;
		Vm.NativeRootCleanup = [this, &Vm](auto& Values, const auto& Keep, auto& Globals, auto& Error)
		{
			return CleanValueRoots(Values, Keep, &Error, &Globals, &Vm);
		};
		return true;
	}

	FRunResult VeroScriptSession::MakeResetRequiredResult() const
	{
		FRunResult Result;
		Result.ExitCode = 1;
		Result.Error = MakeDiagnostic("diag.execution_state_reset_required");
		Result.Error.Location.FileLabel = "<session>";
		Result.Diagnostics = {Result.Error};
		FHostStreamBridge Bridge(Host);
		EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
		return Result;
	}

	bool VeroScriptSession::LeaseReusableJit(VeroScriptVm& Vm)
	{
		// Callback setters invalidate immediately. Native catalogs must additionally
		// publish a runtime revision; an unversioned custom table stays conservative.
		const auto& Catalog = EffectiveBindings();
		const bool bCanRetainCode = JitPolicy.bReuseSessionCode && JitBackendFactory != nullptr &&
			(Catalog.GetNativeFunctions().empty() || Catalog.GetRevision() != 0) &&
			Vm.GetEffectiveExecutionBackend() != EExecutionBackend::Vm;
		if (!bCanRetainCode)
		{
			ReusableJit.reset();
		}
		else
		{
			const auto SamePolicy = [](const FJitPolicy& A, const FJitPolicy& B)
			{
				return A.MixedHotCallThreshold == B.MixedHotCallThreshold && A.MaxCompileAttempts == B.MaxCompileAttempts && A.MaxCachedEntries == B.MaxCachedEntries && A.EagerCompileBudget == B.EagerCompileBudget && A.bReuseSessionCode == B.bReuseSessionCode && A.bEnableControlledMixed == B.bEnableControlledMixed;
			};
			if (!ReusableJit || ReusableJit->Module != Module.get() || ReusableJit->Generation != ProgramGeneration || ReusableJit->Backend != ExecutionBackend || ReusableJit->Factory != JitBackendFactory || ReusableJit->Bindings != &Catalog || ReusableJit->BindingRevision != Catalog.GetRevision() || !SamePolicy(ReusableJit->Policy, JitPolicy))
			{
				ReusableJit = std::make_unique<FSessionJitState>();
				ReusableJit->Module = Module.get();
				ReusableJit->Generation = ProgramGeneration;
				ReusableJit->Backend = ExecutionBackend;
				ReusableJit->Policy = JitPolicy;
				ReusableJit->Factory = JitBackendFactory;
				ReusableJit->Bindings = &Catalog;
				ReusableJit->BindingRevision = Catalog.GetRevision();
			}
			if (ReusableJit->BackendCache)
			{
				Vm.BackendCache = std::move(ReusableJit->BackendCache);
				// A fresh invocation owns its execution counters, while compilation
				// budgets and generated code remain cumulative in the leased cache.
				Vm.BackendCache->ResetExecutionStatistics();
				// Core validates this context before reusing a cache. A fresh VM's
				// default mask would discard governed entries and restart compilation.
				Vm.BackendGovernanceRequirements = ReusableJit->GovernanceRequirements;
				ReusableJit->StatsAtRunStart = Vm.BackendCache->GetStats();
				Vm.JitStatsAtRunStart = &ReusableJit->StatsAtRunStart;
			}
			else
			{
				auto& Resources = ReusableJit->Resources;
				// Dense function slots alone may already exceed the retention budget.
				// Reject before allocating/compiling them, not after a costly first run.
				const std::size_t MinimumPayload = FJitBackendCache::MinimumRetainedBytes(Module->Functions.size());
				Resources.bResourceLimitReached |= MinimumPayload > JitCacheLimits.MaxRetainedPayloadBytes;
				if (Resources.bResourceLimitReached || (JitPolicy.MaxCompileAttempts != 0 && Resources.CompileAttempts >= JitPolicy.MaxCompileAttempts))
				{
					Vm.SetExecutionBackend(EExecutionBackend::Vm);
				}
				else if (Resources.CompileAttempts != 0 && JitPolicy.MaxCompileAttempts != 0)
				{
					FJitPolicy Remaining = JitPolicy;
					Remaining.MaxCompileAttempts -= Resources.CompileAttempts;
					Vm.SetJitPolicy(Remaining);
				}
			}
		}
		return bCanRetainCode;
	}

	void VeroScriptSession::RecoverReusableJit(VeroScriptVm& Vm, const bool bCanRetainCode,
	    const bool bSucceeded)
	{
		if (bCanRetainCode && ReusableJit)
		{
			auto& Resources = ReusableJit->Resources;
			// Run-scoped deltas also include attempts from a failing invocation.
			Resources.CompileAttempts += LastJitStats.CompileAttempts;
			if (Vm.BackendCache)
			{
				const auto Usage = Vm.GetJitResourceUsage();
				Resources.NativeBytes = Usage.ExecutableBytes;
				Resources.RetainedPayloadBytes = Usage.RetainedBytes;
				Resources.bResourceLimitReached |= Usage.bResourceLimitReached || Usage.ExecutableBytes > JitCacheLimits.MaxNativeBytes || Usage.RetainedBytes > JitCacheLimits.MaxRetainedPayloadBytes;
			}
			if (!Resources.bResourceLimitReached && bSucceeded && Vm.BackendCache)
			{
				ReusableJit->GovernanceRequirements = Vm.BackendGovernanceRequirements;
				ReusableJit->BackendCache = std::move(Vm.BackendCache);
			}
			else
			{
				// The code dies with this VM; the independent controller survives.
				Resources.RetainedPayloadBytes = 0;
				Resources.NativeBytes = 0;
			}
		}
	}

	void VeroScriptSession::NotifyDebugProgramChanged() noexcept
	{
		const FSessionCallbackGuard CallbackGuard(bRunning);
		if (DebugBridge)
		{
			DebugBridge->ProgramChanged(GetDebugProgram());
		}
	}

	void VeroScriptSession::SettlePendingHotReload(
	    FRunResult& Result,
	    std::ostream& ErrorStream)
	{
		if (!PendingHotReloadRollback)
		{
			return;
		}
		const bool bInitializationCompleted =
		    HotReloadState != nullptr && !HotReloadState->bNeedsModuleInitialization;
		if (!bInitializationCompleted)
		{
			const std::string FailedLogicalPath = FilePath;
			RestorePendingHotReload();
			// exit(0) / incomplete init can still report VM success — surface failure
			// so hosts do not treat the rolled-back generation as applied. The exit
			// belonged to the discarded generation, so neither the flag nor its code
			// may surface here. A genuine failure keeps its own ExitCode (e.g. 1).
			const bool bDiscardedExit = Result.bExitRequested;
			Result.bSuccess = false;
			Result.bExitRequested = false;
			if (bDiscardedExit || Result.ExitCode == 0)
			{
				Result.ExitCode = 3;
			}
			if (Result.Error.MessageId.empty() && Result.Error.Message.empty())
			{
				Result.Error = MakeDiagnostic(
				    "diag.hot_reload_initialization_failed",
				    FSourceLocation{
				        FailedLogicalPath.empty()
				            ? "<session>"
				            : FailedLogicalPath,
				        1,
				        1});
				Result.Diagnostics = {Result.Error};
				EngineInternal::WriteError(ErrorStream, Result.Error);
			}
		}
		else
		{
			std::string RootError;
			if (!SettleMigratableRoots(true, RootError))
			{
				Result.bSuccess = false;
				Result.ExitCode = 1;
				const auto Failure = MakeDiagnostic("diag.execution_state_reset_required");
				if (Result.Error.Message.empty())
				{
					Result.Error = Failure;
				}
				Result.Diagnostics.push_back(Failure);
				ReloadStatus.State = EReloadState::ResetRequired;
				return;
			}
			CommittedProgramGeneration = ProgramGeneration;
			ReloadStatus.State = Result.bSuccess ? EReloadState::Applied : EReloadState::AppliedWithErrors;
			PendingHotReloadRollback.reset();
			{
				struct FRestoreEnvironment
				{
					std::shared_ptr<const FModuleEnvironment>& Slot;
					std::shared_ptr<const FModuleEnvironment> Active;
					~FRestoreEnvironment()
					{
						Slot = std::move(Active);
					}
				} Restore{ModuleEnvironment, ModuleEnvironment};
				ModuleEnvironment = PreviousHotReloadEnvironment;
				FScriptError CleanupError;
				if (!DiscardRetainedCallables(PreviousHotReloadModule.get(), PreviousHotReloadProgramGeneration, false, &CleanupError))
				{
					Result.bSuccess = false;
					Result.bExitRequested = false;
					if (Result.ExitCode == 0)
					{
						Result.ExitCode = 1;
					}
					if (Result.Error.Message.empty())
					{
						Result.Error = CleanupError;
					}
					Result.Diagnostics.push_back(std::move(CleanupError));
					ReloadStatus.State = RequiresReset() ? EReloadState::ResetRequired : EReloadState::AppliedWithErrors;
				}
			}
			PreviousHotReloadModule.reset();
			PreviousHotReloadEnvironment.reset();
			PreviousHotReloadProgramGeneration = ProgramGeneration;
			PreviousHotReloadFilePath.clear();
			PreviousReloadSourcePath.clear();
			PreviousHotReloadPackageKind.reset();
			if (bTemporaryHotReloadState)
			{
				// Preserve the historical fresh-state-per-run behavior for a
				// non-hot-reload session after package initialization succeeds.
				DiscardHotReloadState();
			}
		}
	}

	FRunResult VeroScriptSession::CallMethodInternal(
	    const FObjectHandle& Object,
	    const std::string_view MethodName,
	    const std::vector<FValue>& Arguments,
	    std::vector<FValue>* const OutArguments,
	    FValue* const OutReturn,
	    const EMissingMethodPolicy MissingPolicy,
	    const bool bBypassEntryInterceptor)
	{
		if (RequiresReset()) return MakeResetRequiredResult();
		FValueSafePoint ValueSafePoint(*this, &Arguments, OutReturn, &Object);
		const auto Dispatch = [&]() -> FRunResult
		{
			if (!Module)
			{
				return MakeSessionNoModuleResult(Host);
			}
			if (bRunning)
			{
				return MakeSessionReentryResult(Host, "CallMethod");
			}
			if (!Object)
			{
				FRunResult Result;
				Result.ExitCode = 2;
				Result.Error = MakeDiagnostic("diag.callmethod_requires_object");
				Result.Error.Location.FileLabel = "<session>";
				FHostStreamBridge Bridge(Host);
				EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
				return Result;
			}
			if (MethodName.empty())
			{
				FRunResult Result;
				Result.ExitCode = 2;
				Result.Error = MakeDiagnostic("diag.callmethod_requires_a_non_empty_name");
				Result.Error.Location.FileLabel = "<session>";
				FHostStreamBridge Bridge(Host);
				EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
				return Result;
			}
			if (Arguments.size() > 255)
			{
				FRunResult Result;
				Result.ExitCode = 2;
				Result.Error = MakeDiagnostic("diag.callmethod_supports_at_most_255_arguments");
				Result.Error.Location.FileLabel = "<session>";
				FHostStreamBridge Bridge(Host);
				EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
				return Result;
			}
			const std::uint8_t ArgumentCount = static_cast<std::uint8_t>(Arguments.size());
			std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
			const EMethodResolveStatus MethodStatus = ModuleMeta::TryResolveInstanceMethodOnType(
			    *Module,
			    Object->GetTypeName(),
			    MethodName,
			    ArgumentCount,
			    FunctionIndex,
			    Arguments.empty() ? nullptr : Arguments.data());
			if (MethodStatus != EMethodResolveStatus::Found)
			{
				const char* MessageId = MethodStatus == EMethodResolveStatus::Ambiguous
				    ? "diag.callmethod_ambiguous_method"
				    : "diag.callmethod_unknown_method";
				const std::string ArgCountText = std::to_string(ArgumentCount);
				FRunResult Result;
				Result.ExitCode = 4;
				Result.Error = MakeDiagnostic(
				    MessageId,
				    FSourceLocation{"<session>", 1, 1},
				    {MethodName, ArgCountText});
				Result.Diagnostics = {Result.Error};
				if (MissingPolicy != EMissingMethodPolicy::Quiet)
				{
					FHostStreamBridge Bridge(Host);
					EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
				}
				return Result;
			}
			const FSessionRunningGuard RunningGuard(bRunning);
			FSessionOutputScope<FValue> Output(OutReturn, bRunning);
			FValueSafePoint PendingOutput(*this, nullptr, Output.Get());

			if (!ReusableExecution)
			{
				ReusableExecution = std::make_unique<FSessionExecutionScratch>();
			}
			FHostStreamBridge Bridge(Host);
			FSessionDebugExecutionScope DebugScope(DebugBridge, GetDebugProgram(),
			    ESessionDebugExecutionKind::CallMethod, MethodName, Object, ExecutionControl);
			VeroScriptVm Vm(
			    Bridge.OutputStream(),
			    InputStreamOrCin(),
			    Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
			const bool bLeaseMethodScratch = FSessionExecutionScratch::FitsMethodBudget(
			    ReusableExecution->OperandStack, ReusableExecution->Frames, ReusableExecution->FreeFrames);
			if (bLeaseMethodScratch)
			{
				ReusableExecution->LeaseTo(Vm);
			}
			const bool bConfigured = ConfigureExecutionVm(Vm);
			if (bConfigured && bLeaseMethodScratch)
			{
				ReusableExecution->SwapNativeBindings(Vm);
			}
			Vm.SetDebugger(DebugScope.GetDebugger());
			const bool bCanRetainCode = bConfigured && LeaseReusableJit(Vm);
			FSessionJitExceptionGuard ExceptionGuard{
			    bCanRetainCode ? ReusableJit.get() : nullptr, Vm, LastJitStats, JitCacheLimits};
			const bool bExecuted = bConfigured && Vm.ExecuteInstanceMethod(
			    *Module, Object, MethodName, Arguments,
			    (bHotReloadEnabled || PendingHotReloadRollback) ? HotReloadState.get() : nullptr,
			    &EffectiveBindings().GetNativeFunctions(), Output.Get(), PendingHotReloadRollback.get(),
			    OutArguments, bBypassEntryInterceptor);
			DebugScope.Finish();
			FRunResult Result = FinishVmCall(Vm, Bridge.ErrorStream(), &LastJitStats, &LastJitDiagnostics);
			if (bLeaseMethodScratch && FSessionExecutionScratch::FitsMethodBudget(
			        Vm.OperandStack, Vm.Frames, Vm.FreeFrames))
			{
				ReusableExecution->RecoverFrom(Vm);
			}
			RecoverReusableJit(Vm, bCanRetainCode, bExecuted && Result.bSuccess);
			ExceptionGuard.State = nullptr;
			if (!bExecuted && Result.bSuccess)
			{
				Result.bSuccess = false;
				Result.ExitCode = 1;
				Result.Error = Vm.GetError();
				Result.Diagnostics = {Result.Error};
				EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			}
			SettlePendingHotReload(Result, Bridge.ErrorStream());
			return Result;
		};
		FRunResult Completed = ValueSafePoint.Ready() ? Dispatch() : FRunResult{};
		ValueSafePoint.Arguments = OutArguments;
		ValueSafePoint.Object = nullptr; // only actual outputs remain live after execution
		return ValueSafePoint.Finish(std::move(Completed));
	}

	FRunResult VeroScriptSession::CallFunctionInternal(
	    const std::string_view FunctionName,
	    const std::vector<FValue>& Arguments,
	    std::vector<FValue>* const OutArguments,
	    FValue* const OutReturn,
	    const EMissingMethodPolicy MissingPolicy)
	{
		if (RequiresReset()) return MakeResetRequiredResult();
		FValueSafePoint ValueSafePoint(*this, &Arguments, OutReturn);
		const auto Dispatch = [&]() -> FRunResult
		{
			if (!Module)
			{
				return MakeSessionNoModuleResult(Host);
			}
			if (bRunning)
			{
				return MakeSessionReentryResult(Host, "CallFunction");
			}
			if (FunctionName.empty())
			{
				FRunResult Result;
				Result.ExitCode = 2;
				Result.Error = MakeDiagnostic("diag.callfunction_requires_a_non_empty_name");
				Result.Error.Location.FileLabel = "<session>";
				FHostStreamBridge Bridge(Host);
				EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
				return Result;
			}
			if (Arguments.size() > 255)
			{
				FRunResult Result;
				Result.ExitCode = 2;
				Result.Error = MakeDiagnostic("diag.callfunction_supports_at_most_255_arguments");
				Result.Error.Location.FileLabel = "<session>";
				FHostStreamBridge Bridge(Host);
				EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
				return Result;
			}
			const std::uint8_t ArgumentCount = static_cast<std::uint8_t>(Arguments.size());
			std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
			const auto* ArgumentData = Arguments.empty() ? nullptr : Arguments.data();
			const EMethodResolveStatus Status = ReusableExecution
			    ? ReusableExecution->FreeFunctionLookup.Resolve(Module, ProgramGeneration,
			          FunctionName, ArgumentCount, FunctionIndex, ArgumentData)
			    : ModuleMeta::TryResolveFreeFunction(*Module, FunctionName, ArgumentCount,
			          FunctionIndex, ArgumentData);
			if (Status != EMethodResolveStatus::Found)
			{
				const char* MessageId = Status == EMethodResolveStatus::Ambiguous
				    ? "diag.callfunction_ambiguous_function"
				    : "diag.callfunction_unknown_function";
				const std::string ArgCountText = std::to_string(ArgumentCount);
				FRunResult Result;
				Result.ExitCode = 4;
				Result.Error = MakeDiagnostic(
				    MessageId,
				    FSourceLocation{"<session>", 1, 1},
				    {FunctionName, ArgCountText});
				Result.Diagnostics = {Result.Error};
				if (MissingPolicy != EMissingMethodPolicy::Quiet)
				{
					FHostStreamBridge Bridge(Host);
					EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
				}
				return Result;
			}
			const FSessionRunningGuard RunningGuard(bRunning);
			FSessionOutputScope<FValue> Output(OutReturn, bRunning);
			FValueSafePoint PendingOutput(*this, nullptr, Output.Get());

			if (!ReusableExecution)
			{
				ReusableExecution = std::make_unique<FSessionExecutionScratch>();
			}
			FHostStreamBridge& Bridge = ReusableExecution->GetCallBridge(Host);
			const FReusableHostBridgeScope BridgeScope(Bridge);
			FSessionDebugExecutionScope DebugScope(DebugBridge, GetDebugProgram(),
			    ESessionDebugExecutionKind::CallFunction, FunctionName, {}, ExecutionControl);
			VeroScriptVm Vm(
			    Bridge.OutputStream(),
			    InputStreamOrCin(),
			    Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
			ReusableExecution->LeaseTo(Vm);
			const bool bConfigured = ConfigureExecutionVm(Vm);
			if (bConfigured)
			{
				ReusableExecution->SwapNativeBindings(Vm);
			}
			Vm.SetDebugger(DebugScope.GetDebugger());
			// Initializers may have observable host effects before the historical
			// second overload resolution. Keep reranking the live arguments there
			// while reusing the structural candidates from this Session.
			const bool bCanUseResolvedEntry = Module->InitCode.empty();
			if (bCanUseResolvedEntry)
			{
				Vm.SetPreferredJitFunction(FunctionIndex);
			}
			const bool bCanRetainCode = bConfigured && LeaseReusableJit(Vm);
			FSessionJitExceptionGuard ExceptionGuard{
			    bCanRetainCode ? ReusableJit.get() : nullptr, Vm, LastJitStats,
			    JitCacheLimits};
			const bool bExecuted = bConfigured && (
			    bCanUseResolvedEntry
			    ? Vm.ExecuteResolvedFreeFunction(
			          *Module, FunctionIndex, Arguments,
			          (bHotReloadEnabled || PendingHotReloadRollback) ? HotReloadState.get() : nullptr,
			          &EffectiveBindings().GetNativeFunctions(), Output.Get(), PendingHotReloadRollback.get(),
			          OutArguments)
			    : Vm.ExecuteNamedFreeFunction(
			          *Module, FunctionName, Arguments,
			          (bHotReloadEnabled || PendingHotReloadRollback) ? HotReloadState.get() : nullptr,
			          &EffectiveBindings().GetNativeFunctions(), Output.Get(), PendingHotReloadRollback.get(),
			          OutArguments, &ReusableExecution->FreeFunctionLookup));
			DebugScope.Finish();
			FRunResult Result = FinishVmCall(Vm, Bridge.ErrorStream(), &LastJitStats, &LastJitDiagnostics);
			ReusableExecution->RecoverFrom(Vm);
			RecoverReusableJit(Vm, bCanRetainCode, bExecuted && Result.bSuccess);
			ExceptionGuard.State = nullptr;
			if (!bExecuted && Result.bSuccess)
			{
				Result.bSuccess = false;
				Result.ExitCode = 1;
				Result.Error = Vm.GetError();
				Result.Diagnostics = {Result.Error};
				EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			}
			SettlePendingHotReload(Result, Bridge.ErrorStream());
			return Result;
		};
		FRunResult Completed = ValueSafePoint.Ready() ? Dispatch() : FRunResult{};
		ValueSafePoint.Arguments = OutArguments;
		return ValueSafePoint.Finish(std::move(Completed));
	}

	FRunResult VeroScriptSession::InvokeCallableInternal(
	    const FRetainedCallableHandle Callable,
	    const std::vector<FValue>& Arguments,
	    std::vector<FValue>* const OutArguments,
	    FValue* const OutReturn)
	{
		if (RequiresReset()) return MakeResetRequiredResult();
		// Serialized foreign-thread invocations cannot drain the owner registry.
		// Owner calls still retire released values before and after execution.
		std::optional<FValueSafePoint> OwnerSafePoint;
		if (ValueRoots->IsOwnerThread())
		{
			OwnerSafePoint.emplace(*this, &Arguments, OutReturn);
		}
		const auto Dispatch = [&]() -> FRunResult
		{
			if (!Module)
			{
				return MakeSessionNoModuleResult(Host);
			}
			if (bRunning)
			{
				return MakeSessionReentryResult(Host, "InvokeCallable");
			}
			if (Arguments.size() > 255)
			{
				FRunResult Result;
				Result.ExitCode = 2;
				Result.Error.MessageId = "diag.invokecallable_supports_at_most_255_arguments";
				Result.Error.Message = "InvokeCallable supports at most 255 arguments";
				Result.Error.Location.FileLabel = "<session>";
				Result.Diagnostics = {Result.Error};
				return Result;
			}

			FFunctionHandle Function;
			bool bOwnerThreadOnly = true;
			bool bExpiredWeakThis = false;
			{
				std::lock_guard<std::mutex> Lock(RetainedCallableMutex);
				const auto It = RetainedCallables.find(Callable.Id);
				if (!Callable || It == RetainedCallables.end() || Callable.ProgramGeneration != ProgramGeneration || It->second.Generation != Callable.ProgramGeneration)
				{
					FRunResult Result;
					Result.ExitCode = 4;
					Result.Error.MessageId = "diag.invokecallable_stale_or_unknown";
					Result.Error.Message = "retained callable is stale or belongs to another session generation";
					Result.Error.Location.FileLabel = "<session>";
					Result.Diagnostics = {Result.Error};
					return Result;
				}
				Function = It->second.Callable;
				bOwnerThreadOnly = It->second.bOwnerThreadOnly;
				if (It->second.bWeakThis)
				{
					const FObjectHandle CapturedThis = It->second.WeakThis.lock();
					if (!CapturedThis)
					{
						bExpiredWeakThis = true;
					}
					else
					{
						Function = std::make_shared<FFunctionValue>(*Function);
						Function->CapturedThis = CapturedThis;
					}
				}
			}
			if (bOwnerThreadOnly && std::this_thread::get_id() != OwnerThreadId)
			{
				FRunResult Result;
				Result.ExitCode = 2;
				Result.Error.MessageId = "diag.invokecallable_wrong_thread";
				Result.Error.Message = "retained callable must be invoked on its session owner thread";
				Result.Error.Location.FileLabel = "<session>";
				Result.Diagnostics = {Result.Error};
				return Result;
			}
			if (bExpiredWeakThis || !IsCallableValid(Callable))
			{
				FRunResult Result;
				Result.ExitCode = 4;
				Result.Error.MessageId = "diag.invokecallable_stale_or_unknown";
				Result.Error.Message = "retained callable no longer matches the loaded program";
				Result.Error.Location.FileLabel = "<session>";
				Result.Diagnostics = {Result.Error};
				return Result;
			}

			const FSessionRunningGuard RunningGuard(bRunning);
			FValueSafePoint Inputs(*this, OwnerSafePoint ? nullptr : &Arguments, OwnerSafePoint ? nullptr : OutReturn);
			FSessionOutputScope<FValue> Output(OutReturn, bRunning);
			FValueSafePoint PendingOutput(*this, nullptr, Output.Get());
			FHostStreamBridge Bridge(Host);
			FSessionDebugExecutionScope DebugScope(DebugBridge, GetDebugProgram(),
			    ESessionDebugExecutionKind::InvokeCallable,
			    Function->FunctionName, Function->CapturedThis, ExecutionControl);
			VeroScriptVm Vm(Bridge.OutputStream(), InputStreamOrCin(), Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
			const bool bConfigured = ConfigureExecutionVm(Vm);
			Vm.SetDebugger(DebugScope.GetDebugger());
			const bool bCanRetainCode = bConfigured && LeaseReusableJit(Vm);
			FSessionJitExceptionGuard ExceptionGuard{
			    bCanRetainCode ? ReusableJit.get() : nullptr, Vm, LastJitStats, JitCacheLimits};
			const bool bExecuted = bConfigured && Vm.ExecuteCallable(
			    *Module, Function, Arguments,
			    (bHotReloadEnabled || PendingHotReloadRollback) ? HotReloadState.get() : nullptr,
			    &EffectiveBindings().GetNativeFunctions(), Output.Get(), PendingHotReloadRollback.get(),
			    OutArguments);
			DebugScope.Finish();
			FRunResult Result = FinishVmCall(Vm, Bridge.ErrorStream(), &LastJitStats, &LastJitDiagnostics);
			RecoverReusableJit(Vm, bCanRetainCode, bExecuted && Result.bSuccess);
			ExceptionGuard.State = nullptr;
			if (!bExecuted && Result.bSuccess)
			{
				Result.bSuccess = false;
				Result.ExitCode = 1;
				Result.Error = Vm.GetError();
				Result.Diagnostics = {Result.Error};
				EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
			}
			SettlePendingHotReload(Result, Bridge.ErrorStream());
			return Result;
		};
		if (!OwnerSafePoint) return Dispatch();
		FRunResult Completed = OwnerSafePoint->Ready() ? Dispatch() : FRunResult{};
		OwnerSafePoint->Arguments = OutArguments;
		return OwnerSafePoint->Finish(std::move(Completed));
	}

	const IHostBindingCatalog& VeroScriptSession::EffectiveBindings() const
	{
		if (ModuleEnvironment && ModuleEnvironment->GetDescription().Bindings)
		{
			return *ModuleEnvironment->GetDescription().Bindings;
		}
		return Bindings;
	}

	bool VeroScriptSession::RejectBindingMutation(const std::string_view Action)
	{
		if (RejectIfRunning(Action)) return true;
		if (ModuleEnvironment && ModuleEnvironment->GetDescription().Bindings)
		{
			FHostStreamBridge Bridge(Host);
			Bridge.ErrorStream() << "VeroScript: " << Action
			                     << " cannot mutate an environment-owned binding catalog; prepare a combined snapshot before ApplyConfig\n";
			return true;
		}
		return false;
	}

}
