// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptExecutionContext.h"
#include "VeroScriptExecutionStack.h"
#include "VeroScriptCallInterceptor.h"
#include "VeroScriptConcurrent.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptJit.h"
#include "VeroScriptJitBackend.h"
#include "VeroScriptJitDiagnostics.h"
#include "VeroScriptJitScalarServices.h"
#include "VeroScriptMutation.h"
#include "VeroScriptNative.h"
#include "VeroScriptNumericBindings.h"
#include "VeroScriptRun.h"
#include "VeroScriptExecutionGovernance.h"
#include "VeroScriptAotProgram.h"
#include "VeroScriptValue.h"
#include "VeroScriptValueRoot.h"

#include <array>
#include <chrono>
#include <deque>
#include <exception>
#include <initializer_list>
#include <limits>
#include <iosfwd>
#include <memory>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace VeroScript
{
	struct FHotReloadRollback;
	struct FHotReloadState;
	struct FSessionExecutionScratch;
	struct FExecutionCleanupState;
	class VeroScriptDebugger;
	class FVmJitRuntimeServices;
	struct FVmScalarCallPlan;
	struct FVmCallableSignature;

	/**
	 * Stack VM that executes a compiled FBytecodeModule.
	 */
	class VeroScriptVm final : private FExecutionContext, public IScriptCallInvoker, public IMemoryCallServices, public I18n::II18nCallServices, public INumericCallServices
	{
		friend struct FExecutionContext;
		friend class FVmExecutionServices;
		friend class VeroScriptSession;

		friend class FVmJitRuntimeServices;
		friend struct FJitDiagnosticScope;

		friend struct FSessionExecutionScratch;

	public:
		bool SetVmBackendFactory(std::shared_ptr<const IVmBackendFactory> Factory, std::string& Error);
		bool SetVmBackendPath(std::string_view Path, std::string& Error);
		const std::shared_ptr<const IVmBackendFactory>& GetVmBackendFactory() const noexcept
		{
			return VmExecutionDomain->GetFactory();
		}
		const std::shared_ptr<FVmExecutionDomain>& GetVmExecutionDomain() const noexcept
		{
			return VmExecutionDomain;
		}
		bool SetVmExecutionDomain(std::shared_ptr<FVmExecutionDomain> Domain, std::string& Error);
		const FVmBackendStats& GetVmBackendStats() const noexcept
		{
			return VmBackendStats;
		}
		//~ Begin IScriptCallInvoker Interface
		void NotifyExecutionCommitted() noexcept override
		{
			VmExecutionDomain->bFrozen = true;
		}
		std::uint64_t GetProgramGeneration() const override
		{
			return ProgramGeneration;
		}

		std::shared_ptr<const void> GetProgramOwner() const override
		{
			return ProgramOwner;
		}

		const FBytecodeModule* GetActiveModule() const noexcept override
		{
			return ActiveModule;
		}

		bool InvokeCallable(
		    const FFunctionHandle& Callable,
		    std::vector<FValue>& InOutArguments,
		    FValue& OutReturn,
		    std::string& OutError) override;

		bool InvokeFunction(
		    std::string_view FunctionName,
		    std::vector<FValue>& InOutArguments,
		    FValue& OutReturn,
		    std::string& OutError) override;
		std::string GetCallingModulePath(std::size_t Depth) const override;
		bool RetainForGeneration(const FValue&, FValueRoot&, std::string& Error) override;
		//~ End IScriptCallInvoker Interface

		//~ Begin IMemoryCallServices Interface
		FMemoryContext& GetMemoryContext() override;

		FMemoryWork GetMemoryWork() override;

		bool MemoryAddressesAllowed() const override
		{
			return Capabilities.bAllowMemoryAddress;
		}

		const IHostBindingCatalog* MemoryHostBindings() const override
		{
			return MemoryBindings;
		}

		FMemoryResult MemoryArgumentAddress(std::size_t Index, std::uint64_t& OutAddress) override;
		//~ End IMemoryCallServices Interface

		std::shared_ptr<I18n::FI18nServices> GetI18nServices() const override
		{
			return I18nServices;
		}
		I18n::FI18nWork GetI18nWork() override;
		FNumericOperationContext GetNumericOperationContext() override;
		void SetNumericLimits(FNumericLimits Limits);
		FNumericLimits GetNumericLimits() const;
		void SetI18nServices(std::shared_ptr<I18n::FI18nServices> Services, I18n::FI18nLimits Limits = {});

		struct FDebugCallFrameView
		{
			const FBytecodeFunction* Function = nullptr;
			std::size_t InstructionPointer = 0;
			const std::vector<FValue>* Locals = nullptr;
		};
		struct FDebugLocalStaticView
		{
			std::string_view Name;
			const FValue* Value = nullptr;
			bool bInitializing = false;
		};

		explicit VeroScriptVm(std::ostream& InOutputStream);
		~VeroScriptVm();
		void SetMemoryContext(std::shared_ptr<FMemoryContext> Context, bool OwnContext = false);
		void SetStandardLibraryLimits(FStandardLibraryLimits Limits)
		{
			StandardLibraryLimits = Limits;
		}
		FStandardLibraryLimits GetStandardLibraryLimits() const
		{
			return StandardLibraryLimits;
		}
		bool SetModuleEnvironment(
		    std::shared_ptr<const FModuleEnvironment>, std::string& Error, bool bUseBindings = true);
		std::shared_ptr<const FModuleEnvironment> GetModuleEnvironment() const
		{
			return ModuleEnvironment;
		}
		void SetMemoryHostBindings(const IHostBindingCatalog* Bindings)
		{
			MemoryBindings = Bindings;
		}
		// Storage must be an actual paused target; nullptr means identity-only inspection.
		FMemoryInspection DebugInspectMemory(const FValue& Value, FValue* Storage = nullptr);

		VeroScriptVm(std::ostream& InOutputStream, std::istream& InInputStream,
		    std::ostream& InErrorStream);

		// Optional embedded debugger. Not owned. Forces interpret while attached.
		void SetDebugger(VeroScriptDebugger* InDebugger);

		/**
		 * Install a VM-local resolved instance-call interceptor. Empty disables it.
		 * The callback may run concurrently in spawned worker VMs and therefore must
		 * synchronize any shared host state captured by it.
		 */
		void SetInstanceCallInterceptor(FInstanceCallInterceptor Interceptor);

		/**
		 * Install a VM-local mutation observer. Like native callbacks and the
		 * instance-call interceptor, it may run concurrently in spawned workers;
		 * shared host state captured by it must be synchronized.
		 */
		void SetMutationCallback(FMutationCallback Callback);

		const FMutationCallback& GetMutationCallback() const
		{
			return MutationCallback;
		}

		bool HasInstanceCallInterceptor() const
		{
			return static_cast<bool>(InstanceCallInterceptor);
		}

		// Host/session generation paired with resolved method identities.
		void SetProgramGeneration(std::uint64_t Generation);

		void SetProgramOwner(std::shared_ptr<const void> Owner)
		{
			ProgramOwner = Owner ? std::move(Owner) : std::make_shared<int>(0);
		}

		void SetWorkerCancellation(std::shared_ptr<std::atomic_bool> Cancellation)
		{
			WorkerCancellation = std::move(Cancellation);
		}

		/**
		 * Requested execution backend (vm / mixed / jit). Default Mixed.
		 * Debugging, retained initialization ranges and bounded cleanup use Vm.
		 * Ordinary functions in persistent state may use the selected JIT backend.
		 */
		void SetExecutionBackend(EExecutionBackend Backend);

		EExecutionBackend GetExecutionBackend() const
		{
			return RequestedExecutionBackend;
		}

		EExecutionBackend GetEffectiveExecutionBackend() const;

		// Selection changes are accepted only while this VM and its workers are idle.
		bool SetJitBackendFactory(
		    std::shared_ptr<const IJitBackendFactory> Factory, std::string& OutError);

		// Empty paths disable JIT. Failed loads leave the current selection intact.
		bool SetJitBackendPath(std::string_view Path, std::string& OutError);

		const std::shared_ptr<const IJitBackendFactory>& GetJitBackendFactory() const noexcept
		{
			return JitBackendFactory;
		}

		// Optional real module owner for artifacts retained across execution calls.
		bool SetJitProgramOwner(std::shared_ptr<const FBytecodeModule> Owner, std::string& OutError);

		bool SetJitResourceLimits(FJitResourceLimits Limits, std::string& OutError);

		FJitResourceUsage GetJitResourceUsage() const noexcept;

		// Resource guard, not a fixed language limit. 0 reloads the environment/default.
		void SetMaxCallDepth(std::size_t Limit);

		std::size_t GetMaxCallDepth() const
		{
			return CallDepthLimit;
		}

		// Cooperative host-side execution governance. These settings never
		// asynchronously terminate a native callback or host thread.
		void SetExecutionLimits(FExecutionLimits Limits);
		bool SetAotProgram(std::shared_ptr<const IAotProgram> Program, std::string& Error);
		std::uint64_t GetExecutedInstructionCount() const
		{
			return ExecutionGovernance.ExecutedInstructions;
		}
		void SetExecutionDeadline(std::chrono::steady_clock::time_point Deadline)
		{
			RequireIdleExecutionConfiguration();
			ExternalExecutionDeadline = Deadline;
		}
		void SetExecutionCheck(std::function<bool()> Check)
		{
			RequireIdleExecutionConfiguration();
			ExternalExecutionCheck = std::move(Check);
		}

		const FExecutionLimits& GetExecutionLimits() const
		{
			return ExecutionLimits;
		}

		void SetExecutionControl(std::shared_ptr<FExecutionControl> Control);

		const std::shared_ptr<FExecutionControl>& GetExecutionControl() const
		{
			return ExecutionControl;
		}

		void RequestCancel() const noexcept;

		bool IsCancellationRequested() const noexcept;

		void SetCapabilities(FScriptCapabilities InCapabilities);

		const FScriptCapabilities& GetCapabilities() const
		{
			return Capabilities;
		}

		// Drop all JIT entries (module replace / hot reload / debugger).
		void InvalidateJitCache();

		// Update adaptive compilation thresholds/budgets and invalidate old decisions.
		void SetJitPolicy(FJitPolicy Policy);

		const FJitPolicy& GetJitPolicy() const
		{
			return JitPolicy;
		}

		// Snapshot JIT compilation and dispatch counters.
		FJitStats GetJitStats() const;
		FJitDiagnostics GetJitDiagnostics() const;

		// Reset dispatch counters; cache entry counters reset on invalidation.
		void ResetJitStats();

		void RecordJitArrayFastPathPush(const std::size_t Count = 1)
		{
			JitArrayFastPathPushes += Count;
		}

		void RecordJitArrayFastPathGet(const std::size_t Count = 1)
		{
			JitArrayFastPathGets += Count;
		}

		void RecordJitArrayFastPathSet(const std::size_t Count = 1)
		{
			JitArrayFastPathSets += Count;
		}

		void RecordJitScalarMapFastPathGet(const std::size_t Count = 1)
		{
			JitScalarMapFastPathGets += Count;
		}

		void RecordJitScalarMapFastPathSet(const std::size_t Count = 1)
		{
			JitScalarMapFastPathSets += Count;
		}

		void RecordJitNativeTypedLoopExecution()
		{
			++JitNativeTypedLoopExecutions;
		}

		/**
		 * Invoke a host builtin from a JIT trampoline.
		 * Void builtins set bOutHasResult=false; value builtins write OutResult.
		 */
		bool JitInvokeBuiltin(
		    EBuiltinId Builtin,
		    const FValue* Arguments,
		    std::uint8_t ArgumentCount,
		    bool& bOutHasResult,
		    FValue& OutResult);

		// Report a runtime error from a JIT trampoline (sets HasError).
		void JitReportRuntimeError(std::string Message);

		bool Execute(
		    const FBytecodeModule& Module,
		    ERunMode RunMode,
		    const std::vector<std::string_view>& ScriptArgs = {},
		    const std::vector<FNativeFunction>* Natives = nullptr);

		/**
		 * Execute while retaining globals/object identity in State for the next run.
		 * State must be migrated to Module before calling when the module changed.
		 */
		bool ExecutePersistent(
		    const FBytecodeModule& Module,
		    ERunMode RunMode,
		    FHotReloadState& State,
		    FHotReloadRollback* InitializationRollback = nullptr,
		    const std::vector<std::string_view>& ScriptArgs = {},
		    const std::vector<FNativeFunction>* Natives = nullptr);

		// Release retained values while their defining module is still available.
		bool DiscardExternalValues(const FBytecodeModule& Module, std::vector<FValue>& Values,
		    const std::vector<FValue>& Keep, const std::vector<FNativeFunction>* Natives = nullptr,
		    FHotReloadState* Persistent = nullptr);
		bool MakeVariant(const FBytecodeModule& Module, std::string_view TypeName,
		    const FValue& Payload, FValue& Output, const std::vector<FNativeFunction>* Natives = nullptr);

		bool DiscardPersistentState(
		    const FBytecodeModule& Module,
		    FHotReloadState& State,
		    const std::vector<FNativeFunction>* Natives = nullptr);

		// Release retained first-class callables while their defining module is
		// still available, so captured script values run normal destructors.
		bool DiscardRetainedCallables(
		    const FBytecodeModule& Module,
		    std::vector<FFunctionHandle>& Callables,
		    const std::vector<FNativeFunction>* Natives = nullptr);

		/**
		 * Undo a failed replacement's in-place migration/init mutations.
		 * Module must be the failed replacement (still current) so discarded
		 * values can run script destructors against the correct type metadata.
		 */
		bool RollbackHotReloadState(
		    const FBytecodeModule& Module,
		    FHotReloadState& State,
		    FHotReloadRollback& Rollback,
		    const std::vector<FNativeFunction>* Natives = nullptr);

		// Run Init (if any) then a named function on this isolated VM.
		bool ExecuteFunction(
		    const FBytecodeModule& Module,
		    std::uint16_t FunctionIndex,
		    const std::vector<FValue>& Arguments,
		    const std::vector<FNativeFunction>* Natives = nullptr,
		    FValue* OutReturnValue = nullptr,
		    const std::vector<FValue>* Captures = nullptr);

		/**
		 * Install optional persistent globals (like ExecutePersistent), run module init
		 * if needed, invoke an instance method by bare name + argument count (virtual
		 * dispatch on Object->TypeName), then capture globals back. Does not run main.
		 *
		 * OutArguments (optional, size must equal Arguments.size()): after a successful
		 * call, each writable `ref` parameter slot receives the callee's final value
		 * ((*OutArguments)[i]); value / `const ref` slots are left untouched. Nothing is
		 * written back when the call fails or requests exit. OutArguments may alias
		 * Arguments (pass the same vector to mutate it in place).
		 */
		bool ExecuteInstanceMethod(
		    const FBytecodeModule& Module,
		    FObjectHandle Object,
		    std::string_view MethodName,
		    const std::vector<FValue>& Arguments,
		    FHotReloadState* PersistentState,
		    const std::vector<FNativeFunction>* Natives = nullptr,
		    FValue* OutReturnValue = nullptr,
		    FHotReloadRollback* InitializationRollback = nullptr,
		    std::vector<FValue>* OutArguments = nullptr,
		    bool bBypassEntryInterceptor = false);

		/**
		 * Resolve an instance method on Object's runtime type (and bases/interfaces)
		 * by bare method name + argument count. Does not execute.
		 */
		static bool TryResolveInstanceMethod(
		    const FBytecodeModule& Module,
		    const FObjectHandle& Object,
		    std::string_view MethodName,
		    std::uint8_t ArgumentCount,
		    std::uint16_t& OutFunctionIndex);

		/**
		 * Create a script object of TypeName (linkage or simple name), optionally
		 * invoking a matching constructor. Uses PersistentState like ExecuteInstanceMethod.
		 */
		bool ConstructObject(
		    const FBytecodeModule& Module,
		    std::string_view TypeName,
		    const std::vector<FValue>& Arguments,
		    FObjectHandle& OutObject,
		    FHotReloadState* PersistentState,
		    const std::vector<FNativeFunction>* Natives = nullptr,
		    FHotReloadRollback* InitializationRollback = nullptr);

		/**
		 * Like ExecuteInstanceMethod but for a free function (bare or mangled name + arity).
		 * Does not run main. Same persistent-state rules as ExecuteInstanceMethod.
		 * OutArguments: same `ref` write-back contract as ExecuteInstanceMethod.
		 */
		bool ExecuteFreeFunction(
		    const FBytecodeModule& Module,
		    std::string_view FunctionName,
		    const std::vector<FValue>& Arguments,
		    FHotReloadState* PersistentState,
		    const std::vector<FNativeFunction>* Natives = nullptr,
		    FValue* OutReturnValue = nullptr,
		    FHotReloadRollback* InitializationRollback = nullptr,
		    std::vector<FValue>* OutArguments = nullptr);

		/**
		 * Invoke a first-class callable on a fresh VM execution while preserving the
		 * same module-init, persistent-state, ref/out, capture, and JIT rules used by
		 * the other embed entry points. Callable identity is checked against the
		 * configured ProgramGeneration before any script code executes.
		 */
		bool ExecuteCallable(
		    const FBytecodeModule& Module,
		    const FFunctionHandle& Callable,
		    const std::vector<FValue>& Arguments,
		    FHotReloadState* PersistentState,
		    const std::vector<FNativeFunction>* Natives = nullptr,
		    FValue* OutReturnValue = nullptr,
		    FHotReloadRollback* InitializationRollback = nullptr,
		    std::vector<FValue>* OutArguments = nullptr);

		// Execute an already-resolved instance function without virtual/name lookup.
		bool ExecuteBoundInstanceFunction(
		    const FBytecodeModule& Module,
		    const FObjectHandle& Object,
		    std::uint16_t FunctionIndex,
		    FHotReloadState* PersistentState,
		    const std::vector<FNativeFunction>* Natives = nullptr,
		    FHotReloadRollback* InitializationRollback = nullptr);

		/**
		 * Validate/read/write an instance field on a live object. Validation checks
		 * the complete runtime layout before an object is exposed to a host callback.
		 * Write uses
		 * AssignClonedValue (struct/@clone / HostBase policy). Does not run init/main.
		 */
		bool ValidateObjectLayout(
		    const FBytecodeModule& Module,
		    const FObjectHandle& Object,
		    const std::vector<FNativeFunction>* Natives = nullptr);

		bool ReadObjectField(
		    const FBytecodeModule& Module,
		    const FObjectHandle& Object,
		    std::string_view FieldName,
		    FValue& OutValue,
		    const std::vector<FNativeFunction>* Natives = nullptr);

		bool WriteObjectField(
		    const FBytecodeModule& Module,
		    const FObjectHandle& Object,
		    std::string_view FieldName,
		    const FValue& Value,
		    const std::vector<FNativeFunction>* Natives = nullptr);

		bool WriteObjectFields(
		    const FBytecodeModule& Module,
		    const FObjectHandle& Object,
		    const std::vector<std::pair<std::string, FValue>>& Fields,
		    const std::vector<FNativeFunction>* Natives = nullptr);

		/**
		 * Assign Src into Dest with language value semantics (struct/@clone). Used by
		 * Session::SetGlobal for retained hot-reload slots.
		 */
		bool AssignRetainedValue(
		    const FBytecodeModule& Module,
		    FValue& Dest,
		    const FValue& Src,
		    const std::vector<FNativeFunction>* Natives = nullptr);

		bool HasError() const
		{
			return bHasError || ExecutionCleanup != nullptr;
		}

		const FScriptError& GetError() const;

		// Secondary cleanup diagnostics never replace the original termination.
		const std::vector<FScriptError>& GetCleanupDiagnostics() const;

		// Failed bounded cleanup forbids further execution until the owner resets.
		bool RequiresReset() const noexcept;

		bool WasExitRequested() const
		{
			return bExitRequested;
		}

		int GetRequestedExitCode() const
		{
			return RequestedExitCode;
		}

		// Process result for a completed script run: exit(n) takes precedence over
		// the value returned by int main(); void main()/Eval/non-entry calls yield 0.
		int GetProcessExitCode() const
		{
			return bExitRequested ? RequestedExitCode : EntryExitCode;
		}

		// ---- debug inspection (valid while debugger OnStop runs)
		std::size_t DebugGetCallDepth() const
		{
			return Frames.size();
		}

		const FBytecodeModule* DebugGetModule() const
		{
			return ActiveModule;
		}

		FDebugCallFrameView DebugGetFrame(std::size_t Index) const;
		// Inspection never executes an initializer or creates a storage cell.
		std::vector<FDebugLocalStaticView> DebugGetLocalStatics(std::size_t FrameIndex) const;

		// Write a local slot on a call frame (paused / attached debugger only).
		bool DebugSetLocal(std::size_t FrameIndex, std::size_t Slot, FValue Value);

		// Replace a nested value while preserving VM destruction/lifetime semantics.
		bool DebugAssignValue(FValue& Target, FValue Value);

		// Complete debugger-originated writes through the normal mutation barriers.
		bool DebugCommitObjectFieldMutation(
		    const FObjectHandle& Object,
		    std::uint16_t FieldIndex);

		bool DebugCommitContainerMutation(
		    const FValue& Container,
		    EMutationKind Kind = EMutationKind::ContainerSet);

		static constexpr std::size_t MaxOperandStackDepth = 65536;
		static constexpr std::size_t DefaultMaxCallDepth =
		    VEROSCRIPT_DEFAULT_MAX_CALL_DEPTH;

	private:
		std::uint64_t BeginFunctionDiagnostic(std::uint16_t FunctionIndex, EJitExecutionKind Kind) noexcept;
		void EndFunctionDiagnostic(std::uint64_t Token, bool Entered = true) noexcept;
		void SetFunctionDiagnosticKind(std::uint64_t Token, EJitExecutionKind Kind) noexcept;
		void RecordFunctionFallback(std::uint16_t FunctionIndex, EJitFallbackReason Reason) noexcept
		{
			if (JitPolicy.Diagnostics.MaxFunctions)
			{
				RecordFunctionFallbackEnabled(FunctionIndex, Reason);
			}
		}
		void RecordFunctionFallbackEnabled(std::uint16_t FunctionIndex, EJitFallbackReason Reason) noexcept;
		VeroScriptVm(std::ostream& Output, std::istream& Input, std::ostream& Error,
		    std::shared_ptr<FVmExecutionDomain> Domain, std::shared_ptr<const void> Owner);
		bool DrainNativeValueRoots(std::string& Error);
		bool InvalidateNativeValueRoots();

		void WatchMemoryMutation(const FValue& Value, FScriptObject& Object, std::uint16_t Field);

		void PollMemoryMutations();

		/**
		 * Receiver of the active materialized language frame.
		 */
		const FObjectHandle& GetJitThisObject() const;

		// Load a global slot (resolves refs).
		bool JitLoadGlobal(std::uint16_t Slot, FValue& OutValue);

		/**
		 * Store into a global slot. Honors hot-reload retained-slot skip during module init.
		 */
		bool JitStoreGlobal(std::uint16_t Slot, FValue Value);

		// Linear helpers report synchronously through the VM. Keep their current
		// bytecode operation available without an out-of-line call on the hot path;
		// the value is consulted only when a helper actually raises a diagnostic.
		void JitSetActiveInstructionOffset(const std::size_t InstructionOffset) noexcept
		{
			ActiveInstructionStart = InstructionOffset;
		}

		/**
		 * Shared arithmetic (incl. string/wstring concat for Add). Location may be empty.
		 */
		bool JitEvalArithmetic(
		    EOpcode Op,
		    const FValue& Left,
		    const FValue& Right,
		    FValue& OutResult);

		bool JitEvalUnary(EOpcode Op, const FValue& Operand, FValue& OutResult);

		bool JitEvalBitwise(
		    EOpcode Op,
		    const FValue& Left,
		    const FValue& Right,
		    FValue& OutResult);

		// Exact primitive cast bridge used by the Linear scalar-expression reader.
		// OutHandled remains false for object, enum and other ownership-bearing casts,
		// which retain the authoritative opcode path.
		bool JitTryCheckedScalarCast(
		    const FValue& Value,
		    std::uint16_t TargetTypeNameIndex,
		    FValue& OutResult,
		    bool& OutHandled);

		bool JitTryScalarBuiltinConversion(
		    EBuiltinId Builtin,
		    const FValue& Value,
		    FValue& OutResult,
		    bool& OutHandled);

		bool JitEvaluateCondition(const FValue& Value, bool& bOutCondition);

		bool JitLocalGeImm(std::uint16_t Slot, std::int32_t Immediate, bool& bOutResult);

		bool JitLocalGeLocal(
		    std::uint16_t LeftSlot,
		    std::uint16_t RightSlot,
		    bool& bOutResult);

		bool JitCompareLocals(
		    EOpcode Op,
		    std::uint16_t LeftSlot,
		    std::uint16_t RightSlot,
		    bool& bOutResult);

		/**
		 * Equality / ordering. Writes a bool into OutResult.
		 */
		bool JitCompare(
		    EOpcode Op,
		    const FValue& Left,
		    const FValue& Right,
		    bool& OutResult);

		// Create a script object (class/struct) by type index.
		bool JitNewObject(std::uint16_t TypeIndex, FValue& OutObject, bool bUninitialized = false);

		// Load/store instance fields by StringPool name index.
		bool JitLoadField(const FValue& Object, std::uint16_t NameIndex, FValue& OutValue);

		bool JitStoreField(const FValue& Object, std::uint16_t NameIndex, const FValue& Value,
		    bool bNotifyMutation = true);

		// Numeric field++ (checked i32 overflow; other numerics via Add).
		bool JitIncField(FValue Object, std::uint16_t NameIndex);

		// Fused `LoadLocal object; IncField name` path. The frame roots the handle,
		// so the hot path mutates the resolved field without copying an FValue or
		// touching the object's shared reference count.
		bool JitTryIncLocalField(
		    std::uint16_t Slot,
		    std::uint16_t NameIndex,
		    bool& OutHandled);

		// Fused checked `object.field++; accumulator += object.field` path for a
		// frame-rooted object. The field mutation remains observable even when the
		// following accumulator addition overflows, matching bytecode order.
		bool JitTryIncAndAccumulateLocalI32Field(
		    std::uint16_t ObjectSlot,
		    std::uint16_t NameIndex,
		    std::uint16_t AccumulatorSlot,
		    bool& OutHandled,
		    EJitFieldAccumulateFailure& OutFailure);

		// Borrow two indexed string/wstring elements from frame-rooted arrays and
		// compare without constructing temporary FValues or copying their payloads.
		bool JitTryCompareStringArrayElements(
		    std::uint16_t LeftArraySlot,
		    std::uint16_t LeftIndexSlot,
		    std::uint16_t RightArraySlot,
		    std::uint16_t RightIndexSlot,
		    bool bEqual,
		    bool& OutResult,
		    bool& OutHandled);

		/**
		 * Deep-copy a value for CloneStruct.
		 * Bare host structs: look up ActiveNatives `@clone` and CallNative (stamp result),
		 * matching the interpreter Opcode path. Otherwise Runtime::TryCloneValue
		 * (stamped host / script struct / T[N]); hard-fail on clone errors.
		 */
		bool JitCloneStruct(FValue& InOutValue);

		// Create empty dynamic array / map by StringPool type-name index.
		bool JitNewDynamicArray(std::uint16_t TypeNameIndex, FValue& Out);

		bool JitNewMap(std::uint16_t TypeNameIndex, FValue& Out);

		// NewSet/NewQueue/NewStack/NewOrderedMap/NewDeque/NewChannel/NewNativeArray.
		bool JitNewContainer(EOpcode Op, std::uint16_t TypeNameIndex, FValue& Out);

		// Container index get/set (array/map/...). Set leaves the container in OutContainer.
		bool JitGetIndex(const FValue& Container, const FValue& Index, FValue& OutValue);

		// Combined linear-JIT probes avoid repeating the dynamic-array/type lookup
		// once per supported scalar alternative.
		bool JitTryGetDynamicArrayScalar(
		    const FValue& Container,
		    std::int64_t Index,
		    FValue& OutValue,
		    bool& bOutHandled);

		// Borrow a rooted local container for the common linear-JIT forms. These
		// probes avoid copying a shared container handle onto the operand stack.
		bool JitTryGetScalarIndexOnLocal(
		    std::uint16_t Slot,
		    const FValue& Index,
		    FValue& OutValue,
		    bool& bOutMapHandled,
		    bool& bOutArrayHandled);

		bool JitTrySetDynamicArrayScalar(
		    FValue& Container,
		    std::int64_t Index,
		    const FValue& Value,
		    bool& bOutHandled);

		bool JitTrySetDynamicArrayScalarOnLocal(
		    std::uint16_t ArraySlot,
		    const FValue& Index,
		    const FValue& Value,
		    bool& bOutHandled);

		/**
		 * Raw-value storage for a non-escaping local map<integral, scalar>.
		 * The caller must only enable it after proving that the map cannot cross
		 * a generic/native/debug/ref boundary.
		 */
		bool JitEnableScalarMapStorage(FValue& Container);

		bool JitEnableScalarSetStorage(FValue& Container);

		bool JitEnableScalarArrayStorage(FValue& Container);

		bool JitTryGetScalarMap(
		    const FValue& Container,
		    const FValue& Index,
		    FValue& OutValue,
		    bool& bOutHandled);

		bool JitTrySetScalarMap(
		    FValue& Container,
		    const FValue& Index,
		    const FValue& Value,
		    bool& bOutHandled);

		bool JitTrySetScalarMapI32AddOnLocals(
		    std::uint16_t MapSlot,
		    std::uint16_t KeySlot,
		    std::uint16_t ValueSlot,
		    std::int32_t Immediate,
		    bool& bOutHandled);

		// Exact counted-loop batch for a proven non-escaping map<int, int>.
		// Keys are [Begin, End) and values are key + Immediate. The helper only
		// handles ranges whose value arithmetic cannot overflow, so failure timing
		// and partial-map state remain on the ordinary per-iteration path.
		bool JitTrySetI32MapAffineRangeOnLocal(
		    std::uint16_t MapSlot,
		    std::int32_t Begin,
		    std::int32_t End,
		    std::int32_t Immediate,
		    bool& OutHandled);

		bool JitTrySetScalarMapDirectOnLocal(
		    std::uint16_t MapSlot,
		    const FValue& Key,
		    const FValue& Value,
		    bool& bOutHandled);

		bool JitTrySetScalarMapDirectFromLocals(
		    std::uint16_t MapSlot,
		    std::uint16_t KeySlot,
		    std::uint16_t SourceSlot,
		    bool& bOutHandled);

		bool JitSetIndex(FValue Container, const FValue& Index, const FValue& Value, FValue& OutContainer);

		// Exact primitive push used by the FValue linear path. Unlike the legacy
		// typed entry points this also covers float, 64-bit, bool, and narrow integer
		// alternatives without generic value-cloning work.
		bool JitTryPushDynamicArrayScalar(
		    const FValue& Container,
		    const FValue& Value,
		    bool& bOutHandled);

		bool JitTryPushDynamicArrayScalarOnLocal(
		    std::uint16_t Slot,
		    const FValue& Value,
		    bool& bOutHandled);

		// Indirect call; installs captures into the callee frame. Leaves return/sentinel on stack.
		bool JitCallIndirect(const FValue& Callee, const FValue* Arguments, std::uint8_t ArgumentCount);

		/**
		 * Dynamic CallMethod leaves exactly one value on the operand stack (return or
		 * receiver). CallMethod shares ExecuteCallMethod with the interpreter.
		 * CallNative returns its value directly so the Linear JIT does not round-trip a
		 * freshly produced scalar through the VM operand stack.
		 */
		bool JitCallMethod(
		    std::uint16_t MethodNameIndex,
		    const FValue& ThisObject,
		    const FValue* Arguments,
		    std::uint8_t ArgumentCount,
		    std::uint32_t CallSiteOffset = 0xFFFFFFFFu);

		bool JitCallNative(
		    std::uint16_t NativeIndex,
		    const FValue* Arguments,
		    std::uint8_t ArgumentCount,
		    FValue& OutReturn);

		// Validate the compile-time decoded CallNative sites against the active table
		// before entering a scalar-native LinearTrampoline. Manual/mismatched tables
		// report false without rescanning the bytecode function.
		bool JitCanUseScalarNativeCalls(
		    const std::uint32_t* EncodedGuards,
		    std::size_t GuardCount) const;

		// Build a closure value; CapturedThis comes from GetJitThisObject() when the target is a method.
		bool JitMakeClosure(
		    std::uint16_t FunctionIndex,
		    const FValue* Captures,
		    std::uint8_t CaptureCount,
		    FValue& OutClosure);

		// Transfer an explicit script exception to a catch handler in the current
		// Linear frame. Returns false after installing an uncaught pending exception.
		// Finally-bearing and call-containing protected functions remain on the VM.
		bool JitHandleLinearThrow(
		    FValue Value,
		    std::size_t InstructionOffset,
		    std::size_t& OutHandlerOffset);

		// Load/store locals through ResolveRefStorage (requires an active linear frame).

		bool JitLoadLocal(std::uint16_t Slot, FValue& OutValue);

		FValue* JitBorrowDirectLinearLocal(const std::uint16_t Slot) noexcept
		{
			if (Frames.empty() || Slot >= Frames.back().Locals.size())
			{
				return nullptr;
			}
			return &Frames.back().Locals[Slot];
		}

		// Fused borrowed-local operations avoid copying an object handle through the
		// operand stack while preserving normal field/call guards and interception.
		bool JitLoadLocalField(
		    std::uint16_t Slot,
		    std::uint16_t NameIndex,
		    FValue& OutValue);

		bool JitTryCallScalarContainerMethodOnLocal(
		    std::uint16_t Slot,
		    std::uint16_t MethodNameIndex,
		    const FValue* Arguments,
		    std::uint8_t ArgumentCount,
		    bool bDiscardReturn,
		    FValue& OutValue,
		    bool& OutHandled);

		bool JitTryUpdateScalarMapEntryOnLocal(
		    std::uint16_t MapSlot,
		    const FValue& Key,
		    EOpcode Operation,
		    const FValue& Operand,
		    bool& OutHandled,
		    EJitScalarMapUpdateFailure& OutFailure);

		bool JitHasInstanceCallInterceptor() const noexcept
		{
			return static_cast<bool>(InstanceCallInterceptor);
		}

		bool JitHasMutationCallback() const noexcept
		{
			return static_cast<bool>(MutationCallback);
		}

		// Predecoded form used by a proven non-escaping scalar Set call site. It
		// avoids resolving and comparing the immutable method name in every loop
		// iteration while preserving the ordinary container implementation below.
		bool JitTryCallScalarSetMethodOnLocal(
		    std::uint16_t Slot,
		    const FValue& Argument,
		    EJitScalarSetOperation Operation,
		    bool bDiscardReturn,
		    FValue& OutValue,
		    bool& OutHandled);

		bool JitTryCallScalarSetMethodOnLocals(
		    std::uint16_t Slot,
		    std::uint16_t ArgumentSlot,
		    EJitScalarSetOperation Operation,
		    bool bDiscardReturn,
		    FValue& OutValue,
		    bool& OutHandled);

		// Exact counted-loop bulk insertion for a proven non-escaping set<int>.
		// This preserves the flat set implementation and merely removes repeated
		// FValue/method-dispatch traffic from `set.add(counter)` loops.
		bool JitTryInsertI32SetRangeOnLocal(
		    std::uint16_t Slot,
		    std::int32_t Begin,
		    std::int32_t End,
		    bool& OutHandled);

		bool JitTryCallScalarContainerMethodOnLocals(
		    std::uint16_t Slot,
		    std::uint16_t MethodNameIndex,
		    std::uint16_t ArgumentSlot,
		    bool bDiscardReturn,
		    FValue& OutValue,
		    bool& OutHandled);

		bool JitTryLoadScalarLocal(
		    std::uint16_t Slot,
		    FValue& OutValue,
		    bool& OutHandled);

		// Side-effect-free counterpart for speculative Linear expression decoding.
		// An out-of-range byte sequence declines the probe; an actual LoadGlobal is
		// still validated by JitLoadGlobal/the authoritative opcode path.
		bool JitTryLoadScalarGlobal(
		    std::uint16_t Slot,
		    FValue& OutValue,
		    bool& OutHandled);

		bool JitStoreLocal(std::uint16_t Slot, FValue Value);

		// Proven stable-shape update using the frame's generation-qualified field
		// PIC: object.field = (object.field + delta) % modulus.
		bool JitTryUpdateLocalFieldI32Modulo(
		    std::uint16_t Slot,
		    std::uint16_t NameIndex,
		    const FValue& Delta,
		    std::int32_t Modulus,
		    std::uint32_t CallSiteOffset,
		    bool& OutHandled);

		bool JitBorrowLocalI32Field(
		    std::uint16_t Slot,
		    std::uint16_t NameIndex,
		    FScriptObject*& OutObject,
		    std::uint16_t& OutFieldIndex,
		    std::int32_t*& OutStorage);

		bool JitBorrowLocalF64Field(
		    std::uint16_t Slot,
		    std::uint16_t NameIndex,
		    FScriptObject*& OutObject,
		    std::uint16_t& OutFieldIndex,
		    double*& OutStorage);

		bool JitBorrowLocalU32Field(
		    std::uint16_t Slot,
		    std::uint16_t NameIndex,
		    FScriptObject*& OutObject,
		    std::uint16_t& OutFieldIndex,
		    std::uint32_t*& OutStorage);

		bool JitBorrowLocalF32Field(
		    std::uint16_t Slot,
		    std::uint16_t NameIndex,
		    FScriptObject*& OutObject,
		    std::uint16_t& OutFieldIndex,
		    float*& OutStorage);

		// Borrow a stable field value from a directly rooted object local. The caller
		// keeps the frame live and may only retain the pointer for the current Linear
		// execution span; ref-bound locals intentionally decline this fast path.
		bool JitBorrowLocalScalarField(
		    std::uint16_t Slot,
		    std::uint16_t NameIndex,
		    FScriptObject*& OutObject,
		    std::uint16_t& OutFieldIndex,
		    const FValue*& OutStorage);

		void JitCommitBorrowedFieldMutation(
		    FScriptObject& Object,
		    const std::uint16_t FieldIndex)
		{
			NotifyFieldMutationJitFast(Object, FieldIndex);
		}

		// Proven `scalar-local += object-local.scalar-field` Linear fast path.
		bool JitTryAddLocalFromLocalField(
		    std::uint16_t DestSlot,
		    std::uint16_t ObjectSlot,
		    std::uint16_t NameIndex,
		    std::uint32_t CallSiteOffset,
		    bool& OutHandled);

		bool JitTryAddLocalFromScalarMap(
		    std::uint16_t DestSlot,
		    std::uint16_t MapSlot,
		    const FValue& Key,
		    bool& OutHandled);

		// Proven i32 reduction: dest = (dest + map[key]) % modulus. Combining the
		// lookup and arithmetic keeps a typed map loop off the boxed operand stack.
		bool JitTryAddLocalFromScalarMapModuloI32(
		    std::uint16_t DestSlot,
		    std::uint16_t MapSlot,
		    const FValue& Key,
		    std::int32_t Modulus,
		    bool& OutHandled);

		bool JitTryAddLocalFromScalarArray(
		    std::uint16_t DestSlot,
		    std::uint16_t ArraySlot,
		    const FValue& Index,
		    bool& OutHandled);

		// Rebind a local slot without write-through (BindLocal).
		bool JitBindLocal(std::uint16_t Slot, FValue Value);

		// Prefer in-place i32/u32/f64 local += Imm (IncLocal / AddLocalImm).
		bool JitAddLocalImm(std::uint16_t Slot, std::int32_t Imm);

		// Prefer in-place i32/u32/f64 locals[Dest] += locals[Src] (AddLocalLocal).
		bool JitAddLocalLocal(std::uint16_t DestSlot, std::uint16_t SrcSlot);

		bool JitAddValueToLocal(std::uint16_t DestSlot, const FValue& Source);

		bool JitMakeLocalRef(std::uint16_t Slot, FValue& OutRef);

		bool JitMakeGlobalRef(std::uint16_t Slot, FValue& OutRef);

		bool JitMakeFieldRef(FValue Object, std::uint16_t NameIndex, FValue& OutRef);

		bool JitGetIndexRef(std::uint16_t ArrayLocalSlot, FValue Index, FValue& OutRef, bool bBorrow = true);

		bool JitGetMapEntryRef(std::uint16_t MapLocalSlot, FValue Key, FValue& OutRef);

		void PrepareJitForExecution(const FBytecodeModule& Module);

		void PrepareBackendForExecution(const FBytecodeModule& Module, std::size_t PreferredFunctionIndex);

		void FinishBackendExecution() noexcept;

		// Run a backend entry or return false for VM fallback. The caller owns Receiver.
		bool TryDispatchBackendCall(
		    std::uint16_t FunctionIndex, const FValue* Arguments,
		    std::uint8_t ArgumentCount, const FObjectHandle& Receiver, bool& OutOk);

		void SetPreferredJitFunction(std::uint16_t FunctionIndex) noexcept
		{
			PreferredJitFunctionIndex = FunctionIndex;
		}

		bool ExecuteResolvedFreeFunction(
		    const FBytecodeModule& Module,
		    std::uint16_t FunctionIndex,
		    const std::vector<FValue>& Arguments,
		    FHotReloadState* PersistentState,
		    const std::vector<FNativeFunction>* Natives,
		    FValue* OutReturnValue,
		    FHotReloadRollback* InitializationRollback,
		    std::vector<FValue>* OutArguments);

		bool ExecuteNamedFreeFunction(
		    const FBytecodeModule& Module,
		    std::string_view FunctionName,
		    const std::vector<FValue>& Arguments,
		    FHotReloadState* PersistentState,
		    const std::vector<FNativeFunction>* Natives,
		    FValue* OutReturnValue,
		    FHotReloadRollback* InitializationRollback,
		    std::vector<FValue>* OutArguments,
		    ModuleMeta::FFreeFunctionLookupCache* FreeFunctionLookup);

		bool ExecuteInternal(
		    const FBytecodeModule& Module,
		    ERunMode RunMode,
		    const std::vector<std::string_view>& ScriptArgs,
		    const std::vector<FNativeFunction>* Natives,
		    FHotReloadState* PersistentState,
		    FHotReloadRollback* InitializationRollback,
		    const FInstanceMethodCall* InstanceCall = nullptr);

		/**
		 * Execute InitCode through an ephemeral function/JIT cache without changing
		 * the serialized module. OutHandled remains false when the VM path should be
		 * used (unsupported backend/body or resource limits).
		 */
		bool TryExecuteOneShotInitJit(
		    const FBytecodeModule& Module,
		    bool& OutHandled);

		bool Run();

		bool RunSelectedVm(std::uint64_t CallToken, bool bContinuation);
		bool PrepareVmBackend();
		void ReleaseVmBackend() noexcept;
		bool ExecuteVmBackend(class IVmExecutionServices&, struct FVmExecutionView&,
		    std::uint64_t CallToken, bool bContinuation, bool bInstrumented, bool bGoverned);

		bool CallFunctionAt(
		    std::uint16_t FunctionIndex,
		    const std::vector<FValue>& Arguments,
		    FObjectHandle ThisObject,
		    bool bAllowJit = true);

		// Pointer/count arg form shared by the vector overload; avoids heap args.
		bool CallFunctionAt(
		    std::uint16_t FunctionIndex,
		    const FValue* Arguments,
		    std::uint8_t ArgumentCount,
		    FObjectHandle ThisObject,
		    bool bAllowJit = true);

		// Ensure JitCache exists and PrepareModule for the effective non-Vm backend.
		void PrepareJitForModule(const FBytecodeModule& Module);

		/**
		 * Pop ArgumentCount values into StackBuffer8 (<=8) or HeapBuffer (>8).
		 * OutArguments points at the live buffer. Returns false if Pop set bHasError.
		 */
		bool PopOperandArguments(
		    std::uint8_t ArgumentCount,
		    FValue* StackBuffer8,
		    std::vector<FValue>& HeapBuffer,
		    FValue*& OutArguments);

		/**
		 * Shared numeric/string arithmetic for Add/Sub/Mul/Div/Mod. Both-int32 is the
		 * fast path; string/wstring concatenation is honored for Add only. Used by the
		 * arithmetic opcodes and the fused local-arithmetic opcodes so semantics match.
		 */
		bool EvalArithmetic(
		    EOpcode Op,
		    const FValue& Left,
		    const FValue& Right,
		    FValue& OutResult,
		    const FSourceLocation& Location);

		// Preserve text, unsigned, floating and narrow-integer promotions outside the hot dispatch.
		bool EvalGeneralArithmetic(EOpcode Op, const FValue& Left, const FValue& Right,
		    FValue& OutResult, const FSourceLocation& Location);
		// The validated signed int32/int64 combinations share one checked 64-bit implementation.
		bool EvalWideArithmetic(EOpcode Op, std::int64_t Left, std::int64_t Right,
		    FValue& OutResult, const FSourceLocation& Location);

		bool CallBuiltin(EBuiltinId Builtin, std::uint8_t ArgumentCount);
		bool CallSourceMetadataBuiltin(EBuiltinId Builtin, const FValue* Arguments, std::size_t Count);
		void BindSourceMetadataDescriptor(FScriptObject&);
		bool CallMemoryBuiltin(EBuiltinId Builtin, FValue* Arguments, std::uint8_t ArgumentCount);
		bool CallStandardLibraryBuiltin(EBuiltinId Builtin, FValue* Arguments, std::uint8_t ArgumentCount);
		bool ReportStandardLibraryResult(StandardLibrary::FServiceResult Result);
		bool CallSynchronizationBuiltin(EBuiltinId Builtin, FValue* Arguments, std::uint8_t ArgumentCount);
		bool CallTypedTaskBuiltin(EBuiltinId Builtin, FValue* Arguments, std::uint8_t ArgumentCount);
		bool TrySortIntegerArray(FDynamicArray& Array);
		bool CallArrayAlgorithmBuiltin(EBuiltinId Builtin, FValue* Arguments, std::uint8_t ArgumentCount);
		std::shared_ptr<Concurrent::FTypedTaskState> ResolveTypedTask(const FValue& Id, const FValue& Channel);
		enum class ETypedTaskWait
		{
			Ready,
			Timeout,
			TerminalFailure,
			Interrupted
		};
		ETypedTaskWait AwaitTypedTask(Concurrent::FTypedTaskState& Task, FChannel& Channel,
		    std::int64_t Timeout, FValue& Output, std::string& Failure);
		bool CallMethodBindingBuiltin(EBuiltinId Builtin, FValue* Arguments, std::uint8_t ArgumentCount);

		bool IsBuiltinAllowed(EBuiltinId Builtin, std::string_view& OutCapability) const;

		void RequireIdleExecutionConfiguration() const;
		bool PrepareExecutionRequest(const FHotReloadState* PersistentState = nullptr);
		void BeginExecutionGovernance();
		void BeginExecutionCleanup();
		void PreserveExecutionCleanupError(const FScriptError& Earlier);
		bool CheckExecutionCleanup();
		bool ChargeExecutionCleanup(std::uint64_t Units);
		void RecordExecutionCleanupFailure(const FScriptError& Failure);
		void MarkExecutionResetRequired() noexcept;
		bool SkipScriptCleanup() const noexcept;
		bool RejectExecutionReuse();

		bool CheckExecutionGovernance();
		bool ExecutionInterrupted(EExecutionInterruption Reason);
		bool TryCallScalarExpression(std::uint16_t FunctionIndex, const FValue* Arguments,
		    std::uint8_t ArgumentCount, const FObjectHandle& Receiver,
		    const std::vector<FValue>* Captures, FValue& OutResult);
		bool CheckExecutionStep(const FBytecodeFunction* Function, std::size_t Offset);
		bool ChargeExecutionWork(std::uint64_t Units);

		bool TryCallStandardValueLeaf(std::uint16_t StandardIndex, std::uint8_t ArgumentCount,
		    const FSourceLocation& Location, bool& Handled);

		bool CallNative(
		    std::uint16_t NativeIndex,
		    std::uint8_t ArgumentCount,
		    const FSourceLocation& Location);

		bool JitTryCallNativeScalar(
		    std::uint16_t NativeIndex,
		    const FValue* Arguments,
		    std::uint8_t ArgumentCount,
		    bool& bOutHandled,
		    FValue& OutReturn);

		// Bridge an already-resolved boxed instance call into the raw NativeCfg
		// scalar ABI. On a handled success this pushes the normal return value (or
		// void sentinel), so callers retain the same stack contract as CallFunctionAt.

		bool ResolveScriptObjectMethod(
		    const FObjectHandle& ThisObject,
		    std::uint16_t MethodNameIndex,
		    std::uint8_t ArgumentCount,
		    std::uint32_t CallSiteOffset,
		    FCallFrame& Frame,
		    std::uint16_t& OutFunctionIndex,
		    const FSourceLocation& SourceLocation);

		std::uint16_t ResolveObjectDispatchTypeIndex(
		    const FScriptObject& Object, std::uint16_t ActualTypeIndex) const;

		/**
		 * Shared CallMethod dispatch (containers + script objects).
		 * When bRunNestedToCompletion is true (JIT trampolines), nested CallFunctionAt
		 * frames are run to completion; the interpreter opcode leaves them for RunLoop.
		 */
		bool ExecuteCallMethod(
		    std::uint16_t MethodNameIndex,
		    const FValue& ThisValue,
		    const FValue* Arguments,
		    std::uint8_t ArgumentCount,
		    std::uint32_t CallSiteOffset,
		    const FSourceLocation& SourceLocation,
		    bool bRunNestedToCompletion);

		/**
		 * Execute an already-resolved script instance method. This is the sole
		 * interception point shared by VM and JIT bound/dynamic calls.
		 */
		bool InvokeResolvedInstanceMethod(
		    std::uint16_t FunctionIndex,
		    const FObjectHandle& ThisObject,
		    const FValue* Arguments,
		    std::uint8_t ArgumentCount,
		    const FSourceLocation& SourceLocation,
		    bool bRunNestedToCompletion,
		    bool bAllowJit = true);

		// After CallFunctionAt may have pushed a frame: Run until DepthBefore, expect one stack value.
		bool JitFinishNestedCall(std::size_t DepthBefore, std::size_t StackBefore);

		bool InvokeActiveFunctionAt(
		    std::uint16_t FunctionIndex,
		    const FObjectHandle& ThisObject,
		    const std::vector<FValue>* Captures,
		    std::vector<FValue>& InOutArguments,
		    FValue& OutReturn,
		    std::string& OutError);

		// Next whitespace-separated token; EOF / no chars -> empty string.
		std::string ReadNextToken();

		// Module path of the innermost frame (for spawn visibility).
		std::string GetCallerModulePath() const;

		// All module paths on the call stack (for spawn target visibility).
		std::vector<std::string> GetSpawnReferenceModules() const;

		FValue Pop();

		void Push(FValue Value);

		// Scalars need no intermediate variant or language-level release hook.
		template <typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
		void Push(const T Value)
		{
			ExecutionDetail::Push(OperandStack, MaxOperandStackDepth, Value,
			    [this](const char* Message)
			{
				ReportError(Message);
			});
		}

		// Release caller-owned argument temporaries after sync JIT or frame handoff.
		bool ReleasePoppedArguments(
		    FValue* Arguments,
		    std::uint8_t ArgumentCount,
		    const FSourceLocation& Location);

		FValue Peek(std::size_t Distance = 0) const;

		void ResetModuleLookupCaches();

		std::uint16_t FindTypeIndex(const std::string& TypeName) const;
		bool IsActiveObjectTypeAssignableTo(const std::string& ActualTypeName,
		    const std::string& ExpectedTypeName, std::uint16_t KnownActualIndex = 0xFFFF) const;

		std::uint16_t FindActiveNativeIndexByName(const std::string& Name) const;

		bool IsActiveHostTypeName(const std::string& TypeName) const;

		std::uint16_t FindActiveZeroArgHostCtor(const std::string& TypeName) const;

		bool SetActiveNatives(const std::vector<FNativeFunction>* Natives);

		// Resolve the generation-qualified native type tag carried by a script object.
		std::uint16_t ResolveObjectTypeIndex(const FScriptObject& Object) const;

		std::uint16_t FindFieldIndexByName(std::uint16_t TypeIndex, const std::string& FieldName) const;

		std::uint16_t FindJitFieldIndex(std::uint16_t TypeIndex, std::uint16_t NameIndex) const;

		bool InitializeObjectHostBase(const FObjectHandle& Object, const FSourceLocation& Location);

		void CompleteObjectConstruction(const FObjectHandle& Object, std::uint16_t ConstructedType);

		bool DestroyAbortedConstruction(const FCallFrame& Frame, const FSourceLocation& Location);

		FObjectTypePlan& GetObjectTypePlan(std::uint16_t TypeIndex);
		bool TryRunTrivialDefaultConstructor(std::uint16_t FunctionIndex, const FObjectHandle& Object);

		FObjectHandle CreateObject(std::uint16_t TypeIndex, const FSourceLocation& Location,
		    bool bUninitialized = false);
		bool CopyConstructFields(const FObjectHandle& Target, const FObjectHandle& Source,
		    std::uint16_t TypeIndex, const FSourceLocation& Location);
		bool CopyLanguageValue(const FValue& Value, FValue& Out,
		    const FSourceLocation& Location, bool bAllowExplicit = false, std::string_view DeclaredType = {});

		// Destroy object (once-only): dtor body -> own fields reverse -> bases reverse.
		bool DestroyObject(const FObjectHandle& Object, const FSourceLocation& Location);

		bool DestroyObjectAsType(
		    const FObjectHandle& Object,
		    std::uint16_t TypeIndex,
		    const FSourceLocation& Location);
		bool InvokeObjectDestructor(const FObjectHandle& Object, std::uint16_t TypeIndex,
		    const FSourceLocation& Location);
		bool ReleaseValueTree(FValue Value, const FSourceLocation& Location,
		    const FObjectHandle& Object = {}, std::uint16_t TypeIndex = 0xFFFF);

		/**
		 * Release ownership of Value: destroy last object handle (class/struct),
		 * and when a container/closure handle is last, release elements/captures recursively.
		 */
		bool ReleaseValue(FValue& Value, const FSourceLocation& Location);

		bool ReleasePendingScriptException();

		// Release an owned buffer in reverse order, retaining cleanup errors and
		// draining every value even when a language destructor fails.
		bool ReleaseValueList(FValue* Values, std::size_t Count, const FSourceLocation& Location);

		// Release each element (reverse order), then clear. Used by clear()/last-handle paths.
		bool ReleaseValueList(std::vector<FValue>& Values, const FSourceLocation& Location);

		bool ReleaseValueList(std::deque<FValue>& Values, const FSourceLocation& Location);

		bool ReleaseDetachedValueList(
		    std::vector<FValue>& Values,
		    const FSourceLocation& Location);

		bool ReleaseDetachedValueList(
		    std::deque<FValue>& Values,
		    const FSourceLocation& Location);

		bool ReleaseMapValues(FMap& Map, const FSourceLocation& Location);

		bool ReleaseOrderedMapValues(
		    FOrderedMap& Map,
		    const FSourceLocation& Location);

		// Unwind operands and frames created above a nested-call boundary while
		// preserving the first pending runtime error and running language destructors.
		bool DiscardExecutionAbove(
		    std::size_t FrameDepth,
		    std::size_t StackDepth,
		    const FSourceLocation& Location);

		void PopRuntimeFrame() noexcept;

		void ClearRuntimeFrames() noexcept;

		std::size_t GetLogicalCallDepth() const noexcept;

		bool EnterJitRuntimeFrame(const FBytecodeFunction* Function, std::size_t LocalCount,
		    const FValue* Arguments, std::uint8_t ArgumentCount,
		    const FObjectHandle& Receiver, std::uint64_t Identity);

		bool LeaveJitRuntimeFrame(const FBytecodeFunction* ExpectedFunction);

		bool AssignValue(FValue& Slot, FValue NewValue, const FSourceLocation& Location);

		/**
		 * Destroy exclusive script objects reachable from Roots, then detach
		 * residual cycles. Identities listed in KeepHandles (objects, containers,
		 * and closures) are left intact so a hot-reload rollback can discard
		 * failed-init allocations without mutating retained shared state.
		 */
		bool DiscardValueRoots(
		    std::vector<FValue>& Roots,
		    const std::unordered_set<const void*>* KeepHandles);

		FNativeArray CreateNativeArray(const std::string& TypeName, const FSourceLocation& Location,
		    bool bUninitialized = false);

		FDynamicArrayHandle CreateDynamicArray(const std::string& TypeName, const FSourceLocation& Location);

		bool AppendDynamicArrayValue(
		    FDynamicArray& Array,
		    const FValue& Value,
		    const FSourceLocation& Location);

		static void ReserveDynamicArrayStorage(FDynamicArray& Array, std::size_t Capacity);

		FMapHandle CreateMap(const std::string& TypeName, const FSourceLocation& Location);

		FSetHandle CreateSet(const std::string& TypeName, const FSourceLocation& Location);

		FQueueHandle CreateQueue(const std::string& TypeName, const FSourceLocation& Location);

		FStackHandle CreateStack(const std::string& TypeName, const FSourceLocation& Location);

		FChannelHandle CreateChannel(const std::string& TypeName, const FSourceLocation& Location);

		FOrderedMapHandle CreateOrderedMap(const std::string& TypeName, const FSourceLocation& Location);

		FDequeHandle CreateDeque(const std::string& TypeName, const FSourceLocation& Location);

		bool ValueMatchesElementType(
		    const FValue& Value,
		    const std::string& ElementTypeName,
		    const FSourceLocation& Location);

		bool CheckedCastValue(
		    FValue& Value,
		    const std::string& TargetTypeName,
		    const FSourceLocation& Location);

		bool TryCheckedCastValue(FValue& Value, const std::string& TargetTypeName);
		bool BoxVariantValue(const FValue& Value, const std::string& TypeName, FValue& Output);
		bool ValidateVariantValue(const FValue& Value);
		bool ExtractVariantValue(const FValue& Source, const std::string& Target, FValue& Output);

		bool ValidateAndStampNativeContainerValue(
		    FValue& Value,
		    const std::string& ExpectedTypeName,
		    const FSourceLocation& Location,
		    const FNativeTypeDesc* ExpectedDesc = nullptr);

		FValue CreateDefaultValue(const std::string& TypeName, const FSourceLocation& Location);

		bool GetField(FScriptObject& Object, std::uint16_t FieldIndex, FValue& OutValue, const FSourceLocation& Location);

		bool SetField(FScriptObject& Object, std::uint16_t FieldIndex, const FValue& Value,
		    const FSourceLocation& Location, bool bNotifyMutation = true);

		void NotifyFieldMutation(
		    FScriptObject& Object,
		    std::uint16_t FieldIndex,
		    EMutationKind Kind = EMutationKind::FieldWrite,
		    std::size_t Depth = 0);

		void NotifyFieldMutationJitFast(
		    FScriptObject& Object,
		    std::uint16_t FieldIndex,
		    EMutationKind Kind = EMutationKind::FieldWrite)
		{
			if (!MutationCallback)
			{
				++Object.MutationVersion;
				return;
			}
			NotifyFieldMutation(Object, FieldIndex, Kind);
		}

		void StampContainerMutationOrigin(FValue& Value, FScriptObject& Object,
		    std::uint16_t FieldIndex);

		void NotifyContainerMutation(const FValue& Container, EMutationKind Kind);
		const void* ContainerStorageIdentity(const FValue& Container) const;
		bool CheckContainerCopyMutation(const FValue& Container, const FSourceLocation& Location = {});

		FValue* ResolveRefStorage(
		    FValue& Value,
		    bool bUnwrapClosureCell = true,
		    std::size_t Depth = 0);

		bool TryGetReferenceMutationOrigin(
		    const FValue& Binding,
		    FContainerMutationOrigin& OutOrigin,
		    bool& bOutContainerElement,
		    std::size_t Depth = 0);

		void NotifyReferenceMutation(FValue& Binding);
		bool BindElementReference(FValue& Binding, const FSourceLocation& Location = {});

		bool PromoteReferenceCapture(FValue Binding, FValue& OutCell,
		    const FSourceLocation& Location = {});
		bool PrepareClosureCaptures(std::uint16_t FunctionIndex, std::vector<FValue>& Captures,
		    const FSourceLocation& Location = {});
		bool CreateClosure(std::uint16_t FunctionIndex, std::vector<FValue> Captures,
		    const FObjectHandle& Receiver, FFunctionHandle& Out, const FSourceLocation& Location = {});
		bool CheckLocalStaticAccess(std::uint16_t Slot, const FSourceLocation& Location = {});
		bool PrepareGlobalInitialization();
		bool EnsureGlobalInitialized(std::uint16_t Slot);
		bool BeginLocalStatic(std::uint16_t Slot, bool& bSkip, const FSourceLocation& Location = {});
		bool CommitLocalStatic(std::uint16_t Slot, FValue Value, const FSourceLocation& Location = {});
		void AbortLocalStaticInitializers(std::size_t FrameBoundary,
		    std::size_t TargetOffset = std::numeric_limits<std::size_t>::max()) noexcept;
		bool ReleaseProgramGlobals(const FSourceLocation& Location = {});
		bool ReleaseLocalStaticRoots(const FSourceLocation& Location = {});

		// Public VM/session calls return an owned value snapshot. Internal reference
		// cells never cross the host ABI boundary.
		bool ExportFunctionReturn(
		    const FBytecodeFunction& Function,
		    FValue ReturnValue,
		    FValue* OutValue);

		bool ExpectBool(const FValue& Value, const FSourceLocation& Location);

		bool TryJump(
		    FCallFrame& Frame,
		    std::size_t CodeSize,
		    std::int32_t Offset,
		    const FSourceLocation& Location);

		bool TryTransferToOffset(
		    std::size_t FrameIndex,
		    std::size_t TargetOffset,
		    const FSourceLocation& Location);

		bool BeginReturnOrComplete(
		    std::size_t FrameIndex,
		    FValue ReturnValue,
		    const FSourceLocation& Location);

		bool TryHandlePendingScriptException();

		// Fast same-frame catch transfer used by the interpreter/JIT trampoline.
		// OutHandled=false preserves the full cross-frame/finally unwinder path.
		bool TryHandleCurrentFrameScriptException(
		    FValue& Value,
		    const FSourceLocation& Location,
		    bool& OutHandled);

		bool TryHandlePendingRuntimeFinally();

		bool RaiseScriptException(FValue Value, const FSourceLocation& Location);

		bool ReleaseFinallyContinuations(
		    FCallFrame& Frame,
		    const FSourceLocation& Location,
		    std::size_t TargetOffset = std::numeric_limits<std::size_t>::max());

		bool EvalExpressionCode(
		    const std::vector<std::uint8_t>& Code,
		    FValue& OutValue,
		    const FSourceLocation& Location,
		    std::uint16_t LocalCount = 0);

		void ReportError(std::string Message, const FSourceLocation& Location = {});
		bool NativeFailure(FScriptError Diagnostic, std::string Message,
		    std::string_view NativeName, const FSourceLocation& Location = {});

		void ReportErrorId(
		    std::string_view MessageId,
		    const FSourceLocation& Location = {},
		    std::initializer_list<FDiagnosticArg> Args = {});

		/**
		 * Stamp host value capability callbacks (`@clone`, `@equals`, `@hash`)
		 * from ActiveNatives when present.
		 * Containers / AssignClonedValue use TryCloneValue (stamped -> deep-copy,
		 * unstamped bare host -> share). Opcode/Jit CloneStruct additionally look up
		 * ActiveNatives `@clone` for unstamped bare hosts before falling back.
		 */
		bool HostObjectMatchesType(const FHostObjectHandle&, std::string_view) const;
		void StampHostValueCallbacks(const FHostObjectHandle& Handle);

		bool PrepareHostKeyForInsert(
		    const FHostObjectHandle& Handle,
		    FHostObjectHandle& OutKey,
		    const FSourceLocation& Location);

		// TryCloneValue + ReportError (hard-fail).
		bool CloneValueOrReport(
		    const FValue& Value,
		    FValue& Out,
		    const FSourceLocation& Location = {});

		bool AssignClonedValue(
		    FValue& Dest,
		    const FValue& Source,
		    const FSourceLocation& Location = {});

		bool AppendClonedValue(
		    std::vector<FValue>& Dest,
		    const FValue& Source,
		    const FSourceLocation& Location = {});

		bool AppendClonedValue(
		    std::deque<FValue>& Dest,
		    const FValue& Source,
		    const FSourceLocation& Location = {});

		// Prefer per-opcode DebugLines when reporting errors (cold path).
		FSourceLocation RefineErrorLocation(const FSourceLocation& Location) const;

		bool HasExecutionGovernance() const
		{
			return bExecutionGovernanceEnabled || ExecutionCleanup || RequiresReset() || ExternalExecutionCheck || ExternalExecutionDeadline != std::chrono::steady_clock::time_point::max();
		}
	};
	inline FExecutionContext::FMemoryExecutionScope::FMemoryExecutionScope(VeroScriptVm& InVm) : Vm(InVm)
	{
		Vm.bMemoryExecutionActive = true;
		if (Vm.MemoryContext)
		{
			Vm.MemoryContext->BeginExecution();
		}
	}
	inline FExecutionContext::FMemoryExecutionScope::~FMemoryExecutionScope()
	{
		if (Vm.MemoryContext)
		{
			Vm.MemoryContext->EndExecution();
		}
		Vm.bMemoryExecutionActive = false;
	}
	inline FExecutionContext::FNativeReferenceScope::FNativeReferenceScope(VeroScriptVm& InVm,
	    const FValue* References, std::size_t Count)
	    : Vm(InVm), Previous(Vm.NativeOriginalReferences), PreviousCount(Vm.NativeOriginalReferenceCount)
	{
		Vm.NativeOriginalReferences = References;
		Vm.NativeOriginalReferenceCount = Count;
	}
	inline FExecutionContext::FNativeReferenceScope::~FNativeReferenceScope()
	{
		Vm.NativeOriginalReferences = Previous;
		Vm.NativeOriginalReferenceCount = PreviousCount;
	}
	inline FExecutionContext::FNativeValueScope::FNativeValueScope(VeroScriptVm& In,
	    const FValue* Values, std::size_t Count, const FValue& Result)
	    : Vm(In), Arguments(Values), ArgumentCount(Count), Return(Result), Previous(In.NativeValueScope)
	{
		Vm.NativeValueScope = this;
	}
	inline FExecutionContext::FNativeValueScope::~FNativeValueScope()
	{
		Vm.NativeValueScope = Previous;
	}
	inline FExecutionContext::FMemoryPollScope::FMemoryPollScope(VeroScriptVm& InVm) : Vm(InVm)
	{
		Vm.PollMemoryMutations();
	}
	inline FExecutionContext::FMemoryPollScope::~FMemoryPollScope() noexcept
	{
		try
		{
			Vm.PollMemoryMutations();
		}
		catch (...)
		{
			try
			{
				Vm.ReportErrorId("diag.memory_out_of_memory");
			}
			catch (...)
			{
			}
		}
	}
}
