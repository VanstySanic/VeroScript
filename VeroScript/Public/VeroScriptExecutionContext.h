// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"
#include "VeroScriptCallInterceptor.h"
#include "VeroScriptConcurrent.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptJit.h"
#include "VeroScriptJitBackend.h"
#include "VeroScriptJitScalarServices.h"
#include "VeroScriptMutation.h"
#include "VeroScriptNative.h"
#include "VeroScriptNumericBindings.h"
#include "VeroScriptRun.h"
#include "VeroScriptExecutionGovernance.h"
#include "VeroScriptAotProgram.h"
#include "VeroScriptValue.h"
#include "VeroScriptValueRoot.h"
#include "VeroScriptVmBackend.h"

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
	struct FLocalStaticState;
	struct FSessionExecutionScratch;
	struct FExecutionCleanupState;
	class VeroScriptDebugger;
	class FVmJitRuntimeServices;
	struct FVmScalarCallPlan;
	struct FVmCallableSignature;
	struct FJitDiagnosticState;

	namespace ModuleMeta
	{
		class FFreeFunctionLookupCache;
	}

	class VeroScriptVm;
	class FVmExecutionServices;
	struct FVmObjectLifetimeScope
	{
		FVmObjectLifetimeScope(const FVmObjectLifetimeScope*& InHead, std::size_t Depth,
		    const FObjectHandle& Receiver, std::uint16_t OwnerType)
		    : Head(InHead), Previous(InHead), Object(Receiver.get()), FrameDepth(Depth), TypeIndex(OwnerType)
		{
			Head = this;
		}
		~FVmObjectLifetimeScope()
		{
			Head = Previous;
		}
		FVmObjectLifetimeScope(const FVmObjectLifetimeScope&) = delete;
		FVmObjectLifetimeScope& operator=(const FVmObjectLifetimeScope&) = delete;
		const FVmObjectLifetimeScope*& Head;
		const FVmObjectLifetimeScope* Previous;
		const FScriptObject* Object;
		std::size_t FrameDepth;
		std::uint16_t TypeIndex;
	};

	/** Core-owned canonical execution state. Backends receive bounded views only. */
	struct FExecutionContext
	{
		friend class VeroScriptVm;
		friend class VeroScriptSession;
		friend class FVmJitRuntimeServices;
		friend class FVmExecutionServices;
		friend struct FVmExecutionView;
		static constexpr std::size_t DefaultMaxCallDepth = VEROSCRIPT_DEFAULT_MAX_CALL_DEPTH;
		FExecutionContext(std::ostream& Output, std::istream& Input, std::ostream& Error,
		    std::shared_ptr<FVmExecutionDomain> Domain = {}, std::shared_ptr<const void> Owner = {})
		    : OutputStream(Output), InputStream(Input), ErrorStream(Error),
		      ProgramOwner(Owner ? std::move(Owner) : std::make_shared<int>(0))
		{
			VmExecutionDomain = Domain ? std::move(Domain) : std::make_shared<FVmExecutionDomain>();
		}
		FExecutionContext(const FExecutionContext&) = delete;
		FExecutionContext& operator=(const FExecutionContext&) = delete;
		struct FInstanceMethodCall
		{
			FObjectHandle Object;
			std::string MethodName;
			const FValue* Arguments = nullptr;
			std::uint8_t ArgumentCount = 0;
			FValue* OutReturn = nullptr;
			/**
			 * Optional write-back target (ArgumentCount elements, may alias Arguments).
			 * On success, writable `ref` parameter slots receive the callee's final
			 * values; other slots are untouched. No write-back on failure / exit.
			 */
			FValue* OutArguments = nullptr;
			std::string ConstructTypeName;
			FObjectHandle* OutConstructed = nullptr;
			bool bFreeFunction = false;
			bool bResolvedFreeFunction = false;
			ModuleMeta::FFreeFunctionLookupCache* FreeFunctionLookup = nullptr;
			FFunctionHandle Callable;
			std::uint16_t BoundFunctionIndex = static_cast<std::uint16_t>(-1);
			// Bypass interception for this host entry call only. Nested calls made
			// by the method body remain interceptable.
			bool bBypassEntryInterceptor = false;
		};

		struct FMethodInlineCacheEntry
		{
			std::uint32_t CallSiteOffset = 0xFFFFFFFFu;
			std::uint16_t NameIndex = 0xFFFF;
			std::uint16_t TypeIndex = 0xFFFF;
			std::uint16_t FunctionIndex = 0xFFFF;
			std::uint8_t ArgumentCount = 0xFF;
		};

		struct FFieldInlineCacheEntry
		{
			const FBytecodeModule* Module = nullptr;
			std::weak_ptr<FScriptObject> Object;
			FValue* Storage = nullptr;
			std::uint64_t ProgramGeneration = 0;
			std::uint32_t CallSiteOffset = 0xFFFFFFFFu;
			std::uint16_t NameIndex = 0xFFFF;
			std::uint16_t TypeIndex = 0xFFFF;
			std::uint16_t FieldIndex = 0xFFFF;
		};

		struct FMemoryMutationWatch
		{
			std::weak_ptr<FHostObject> Region;
			std::weak_ptr<FScriptObject> Object;
			std::uint16_t Field = 0;
			std::uint64_t Version = 0;
			bool Expired = false;
		};

		struct FMemoryExecutionScope
		{
			explicit FMemoryExecutionScope(VeroScriptVm& InVm);
			~FMemoryExecutionScope();

			VeroScriptVm& Vm;
		};

		struct FNativeReferenceScope
		{
			FNativeReferenceScope(VeroScriptVm& InVm, const FValue* References, std::size_t Count);
			~FNativeReferenceScope();

			VeroScriptVm& Vm;
			const FValue* Previous;
			std::size_t PreviousCount;
		};

		struct FNativeValueScope
		{
			FNativeValueScope(VeroScriptVm& In, const FValue* Values, std::size_t Count, const FValue& Result);
			~FNativeValueScope();
			VeroScriptVm& Vm;
			const FValue* Arguments;
			std::size_t ArgumentCount;
			const FValue& Return;
			FNativeValueScope* Previous;
		};

		struct FMemoryPollScope
		{
			explicit FMemoryPollScope(VeroScriptVm& InVm);
			~FMemoryPollScope() noexcept;

			VeroScriptVm& Vm;
		};

		// FCallFrame embeds these caches by value and requires the complete template.
		template <typename T, std::size_t N>
		class TLazyFrameInlineCache
		{
		public:
			TLazyFrameInlineCache() = default;

			TLazyFrameInlineCache(TLazyFrameInlineCache&&) noexcept = default;

			TLazyFrameInlineCache& operator=(TLazyFrameInlineCache&&) noexcept = default;

			TLazyFrameInlineCache(const TLazyFrameInlineCache&) = delete;

			TLazyFrameInlineCache& operator=(const TLazyFrameInlineCache&) = delete;

			T& operator[](const std::size_t Index)
			{
				if (!Entries)
				{
					Entries = std::make_unique<std::array<T, N>>();
				}
				return (*Entries)[Index];
			}

			void Clear()
			{
				if (Entries)
				{
					*Entries = {};
				}
			}

			static constexpr std::size_t Capacity = N;

		private:
			std::unique_ptr<std::array<T, N>> Entries;
		};

		struct FCallFrame
		{
			enum class EFinallyContinuationKind : std::uint8_t
			{
				Jump,
				Return,
				Exception,
				FatalError,
			};

			struct FFinallyContinuation
			{
				EFinallyContinuationKind Kind = EFinallyContinuationKind::Jump;
				std::size_t TargetOffset = 0;
				std::size_t FinallyBeginOffset = 0;
				std::size_t FinallyEndOffset = 0;
				FValue Value = std::int32_t{0};
				FScriptError Error;
			};

			const FBytecodeFunction* Function = nullptr;
			std::size_t InstructionPointer = 0;
			std::size_t LastInstructionOffset = 0;
			std::size_t StackBase = 0;
			std::uint64_t JitRuntimeFrameIdentity = 0;
			std::vector<FValue> Locals;
			FObjectHandle ThisObject;
			bool bHasReturn = false;
			bool bExpressionEval = false;
			std::uint16_t InitializingGlobalSlot = 0xFFFF;
			// A function selected for authoritative VM execution because its only
			// profitable complex-loop lowering is unavailable must keep its nested
			// calls in the VM as well. Otherwise a portable build can pay the Linear
			// bridge cost once per loop iteration after deliberately rejecting the
			// outer JIT entry. Descendant interpreter frames inherit this bit.
			bool bDisableNestedJit = false;
			FValue ReturnValue = std::int32_t{0};
			std::vector<FFinallyContinuation> FinallyContinuations;
			// Tiny polymorphic L1 CallMethod cache. A frame commonly alternates a
			// few stable receiver types at distinct call sites (for example a.value()
			// and b.value()); keeping both avoids falling through to the shared cache.
			// Argument count remains part of the key so overloads cannot alias.
			// Four call-site sets, each with four receiver-type ways.  Four stable
			// implementations are common for visitor/state interfaces; keeping them
			// resident avoids turning that ordinary polymorphism into a resolver walk.
			// The cache stays frame-local and contains only indices, so reload and VM
			// isolation semantics remain unchanged.
			// Allocate each cache family only when that frame actually executes the
			// corresponding operation. Pure scalar recursion therefore carries two
			// pointers instead of roughly a KiB of empty PIC entries per depth.
			TLazyFrameInlineCache<FMethodInlineCacheEntry, 16> MethodIc;
			std::array<std::uint8_t, 4> MethodIcNextWay{};
			// Per-call-site field caches retain validated type-stable field indices.
			// Borrowed storage is qualified by the program generation.
			TLazyFrameInlineCache<FFieldInlineCacheEntry, 8> FieldIc;
			std::array<std::uint8_t, 4> FieldIcNextWay{};
			// Monomorphic IC for IncField / LoadThisField / StoreThisField (name+type -> field index).
			std::uint16_t IncFieldIcNameIndex = 0xFFFF;
			std::uint16_t IncFieldIcTypeIndex = 0xFFFF;
			std::uint16_t IncFieldIcFieldIndex = 0xFFFF;
			std::uint64_t DiagnosticToken = 0;
			const FBytecodeModule* CacheModule = nullptr;
			const FBytecodeFunction* CacheFunction = nullptr;
			std::uint64_t CacheGeneration = 0;
			void PrepareInlineCaches(const FBytecodeModule* Module, const FBytecodeFunction* InFunction, std::uint64_t Generation)
			{
				if (CacheModule == Module && CacheFunction == InFunction && CacheGeneration == Generation) return;
				MethodIc.Clear(); MethodIcNextWay = {};
				FieldIc.Clear(); FieldIcNextWay = {};
				IncFieldIcNameIndex = IncFieldIcTypeIndex = IncFieldIcFieldIndex = 0xFFFF;
				CacheModule = Module; CacheFunction = InFunction; CacheGeneration = Generation;
			}
		};

		// Stack-owned stages cover synchronous native lifecycle calls and cleanup
		// between script frames without changing the object's actual layout/type.
		using FObjectLifetimeScope = FVmObjectLifetimeScope;

		struct FWeakTarget
		{
			std::weak_ptr<FScriptObject> Script;
			std::weak_ptr<FHostObject> Host;
			bool bHost = false;
			std::uint32_t Generation = 1;
		};

		struct FJitFieldCacheEntry
		{
			const FBytecodeModule* Module = nullptr;
			std::uint64_t ProgramGeneration = 0;
			std::uint16_t NameIndex = 0xFFFF;
			std::uint16_t TypeIndex = 0xFFFF;
			std::uint16_t FieldIndex = 0xFFFF;
		};

		struct FJitTypeFieldCacheSet
		{
			// Stable shape slot hash table, built once for a runtime type after the
			// compact L1 observes a collision. The power-of-two table stays at or below
			// 50% load, making a random field site one integer hash plus a short probe
			// instead of a binary search over the type's complete field list.
			// Packed entry: high 16 bits = name index, low 16 bits = field slot;
			// 0xFFFFFFFF is the empty sentinel.
			std::vector<std::uint32_t> Slots;
			bool bInitialized = false;
		};

		struct FMethodTypeDispatchCacheEntry
		{
			std::uint16_t NameIndex = 0xFFFF;
			std::uint16_t FunctionIndex = 0xFFFF;
			std::uint8_t ArgumentCount = 0xFF;
		};

		struct FMethodTypeDispatchCacheSet
		{
			// A runtime type commonly exposes several hot overloaded/member calls.
			// Four generation-scoped ways avoid evicting unrelated methods while
			// retaining a compact linear probe on the dispatch hot path.
			std::array<FMethodTypeDispatchCacheEntry, 4> Ways{};
			std::uint8_t NextWay = 0;
		};

		struct FMethodDispatchCacheEntry
		{
			const FBytecodeModule* Module = nullptr;
			std::uint64_t ProgramGeneration = 0;
			std::uint16_t NameIndex = 0xFFFF;
			std::uint16_t TypeIndex = 0xFFFF;
			std::uint16_t FunctionIndex = 0xFFFF;
			std::uint8_t ArgumentCount = 0xFF;
		};

	protected:
		std::shared_ptr<FVmExecutionDomain> VmExecutionDomain;
		std::shared_ptr<IVmBackendInstance> VmBackendInstance;
		const FBytecodeModule* VmBoundProgram = nullptr;
		std::uint64_t VmBoundGeneration = 0;
		std::uint64_t VmCallSerial = 0;
		std::size_t VmEntryDepth = 0;
		FVmBackendStats VmBackendStats;
		std::size_t StopFrameDepth = 0;
		std::ostream& OutputStream;
		std::istream& InputStream;
		std::ostream& ErrorStream;
		const FBytecodeModule* ActiveModule = nullptr;
		std::vector<const FBytecodeGlobalInitRange*> DemandGlobalInitializers;
		std::vector<std::unique_ptr<FBytecodeFunction>> DemandGlobalCode;
		std::vector<std::uint8_t> DemandGlobalStates;
		std::unordered_map<std::size_t, std::uint16_t> DemandGlobalStarts;
		bool bDemandGlobalInitializing = false;
		std::shared_ptr<const FModuleEnvironment> ModuleEnvironment;
		bool bUseEnvironmentBindings = true;
		const std::vector<FNativeFunction>* ActiveNatives = nullptr;
		const IHostBindingCatalog* MemoryBindings = nullptr;
		std::vector<FMemoryMutationWatch> MemoryMutationWatches;
		bool bPollingMemoryMutations = false;
		std::vector<FNativeFunction> StandardNativeStorage;
		std::vector<FNativeFunction> BoundNativeStorage;
		const FBytecodeModule* BoundNativeModule = nullptr;
		const std::vector<FNativeFunction>* BoundNativeSource = nullptr;
		std::uint64_t BoundNativeRevision = 0;
		std::shared_ptr<const void> BoundNativeOwner;
		std::shared_ptr<FMemoryContext> MemoryContext;
		std::shared_ptr<I18n::FI18nServices> I18nServices;
		// Bounded, module-generation-scoped plans contain no language roots.
		std::unordered_map<std::uint16_t, std::shared_ptr<const FVmScalarCallPlan>> ScalarCallPlans;
		const FBytecodeModule* ScalarCallPlanModule = nullptr;
		std::uint64_t ScalarCallPlanGeneration = 0;
		I18n::FI18nLimits I18nLimits;
		FStandardLibraryLimits StandardLibraryLimits;
		FNumericLimits NumericLimits;
		bool bOwnMemoryContext = true;
		bool bMemoryExecutionActive = false;
		const FValue* NativeOriginalReferences = nullptr;
		std::size_t NativeOriginalReferenceCount = 0;
		FInstanceCallInterceptor InstanceCallInterceptor;
		FMutationCallback MutationCallback;
		// Updated at callback registration; borrowed status readers observe changes
		// synchronously, including registration from a reentrant host callback.
		bool bInstanceCallInterceptorPresent = false;
		bool bMutationObserverPresent = false;
		std::uint64_t ProgramGeneration = 1;
		std::shared_ptr<const void> ProgramOwner = std::make_shared<int>(0);
		std::shared_ptr<FValueRootOwner> NativeValueRoots;
		using FNativeRootCleanup = std::function<bool(std::vector<FValue>&, const std::vector<FValue>&,
		    std::vector<FValue>&, std::string&)>;
		FNativeRootCleanup NativeRootCleanup;
		FNativeValueScope* NativeValueScope = nullptr;
		bool bNativeRetentionAllowed = true;
		bool bNativeRootExecutionActive = false;
		bool bDrainingNativeRoots = false;
		Concurrent::FTaskGroup SpawnedTasks;
		std::shared_ptr<Concurrent::FExecutorState> Executor = std::make_shared<Concurrent::FExecutorState>();
		std::shared_ptr<std::atomic_bool> WorkerCancellation;
		std::vector<FChannelHandle> LiveChannels;

		// Per-task channel passed to __spawn; used to unblock only that worker on join timeout.
		std::unordered_map<std::int32_t, FChannelHandle> SpawnTaskChannels;
		std::vector<FWeakTarget> WeakTargets;

		// Entry script module (for module-init spawn visibility).
		std::string EntryModulePath;
		VeroScriptDebugger* Debugger = nullptr;
		std::vector<FValue> OperandStack;
		std::vector<FCallFrame> Frames;

		// Recycled call frames to reuse Locals vector capacity across calls.
		std::vector<FCallFrame> FreeFrames;
		std::vector<FValue> Globals;
		std::shared_ptr<FLocalStaticState> LocalStatics;

		// Retained slots whose initializer stores are ignored during hot reload.
		std::vector<bool> HotReloadRetainedGlobalSlots;

		/**
		 * Per-InitCode-byte skip map used during hot-reload module init.
		 * Built by filling GlobalInitRanges outermost-first so the innermost
		 * retained/non-retained decision wins (handles nested new inits under a
		 * retained outer, reverse nesting, and multi-level retained/new nesting).
		 */
		std::vector<std::uint8_t> HotReloadSkipInitByte;
		bool bHotReloadModuleInitializing = false;

		// Persistent requests require the VM's hot-reload services for initialization
		// and fallback; ordinary functions retain their selected JIT eligibility.
		bool bPersistentExecution = false;

		// Records enabled governance independently of backend eligibility.
		bool bExecutionGovernanceEnabled = false;

		EExecutionBackend RequestedExecutionBackend = EExecutionBackend::Mixed;
		FExecutionLimits ExecutionLimits;
		std::shared_ptr<FExecutionControl> ExecutionControl;
		FScriptCapabilities Capabilities;
		FExecutionGovernanceState ExecutionGovernance;
		std::shared_ptr<FExecutionCleanupState> ExecutionCleanup;
		std::shared_ptr<FExecutionCleanupState>* ExternalExecutionCleanup = nullptr;
		bool bBorrowedExecutionCleanup = false;
		bool bExecutionResetRequired = false;
		bool bExecutionStateInvalid = false;
		bool* ExternalExecutionResetRequired = nullptr;
		std::chrono::steady_clock::time_point ExternalExecutionDeadline = std::chrono::steady_clock::time_point::max();
		std::function<bool()> ExternalExecutionCheck;
		std::shared_ptr<const IJitBackendFactory> JitBackendFactory;
		std::shared_ptr<const FBytecodeModule> JitProgramOwner;
		std::shared_ptr<FJitBackendCache> BackendCache;
		FJitStats BackendStats;
		std::uint64_t BackendGovernanceRequirements = 0;
		std::shared_ptr<const IAotProgram> AotProgram;
		const FBytecodeModule* ValidatedAotModule = nullptr;
		std::size_t AotNativeExecutions = 0;
		FJitResourceLimits JitResourceLimits;
		std::uint64_t JitNativeBindingsGeneration = 1;
		// Set only by an owner that publishes runtime binding revisions. Raw VM
		// tables retain conservative invalidation when this is zero.
		std::uint64_t JitNativeBindingsRevision = 0;
		bool bJitHadNativeBindings = false;
		FJitPolicy JitPolicy;
		std::size_t JitDispatchAttempts = 0;
		std::size_t JitDispatches = 0;
		std::size_t JitFallbacks = 0;
		std::size_t JitCostModelVmFallbacks = 0;
		std::size_t JitFailures = 0;

		// Native/JIT calls do not push FCallFrame, so track them separately to
		// preserve the same recursion limit as interpreted calls.
		std::size_t ActiveJitCallDepth = 0;
		std::size_t ActiveBackendFrameCredits = 0;
		std::size_t CallDepthLimit = DefaultMaxCallDepth;
		std::size_t JitCacheInvalidations = 0;
		std::size_t JitArrayFastPathPushes = 0;
		std::size_t JitArrayFastPathGets = 0;
		std::size_t JitArrayFastPathSets = 0;
		std::size_t JitScalarMapFastPathGets = 0;
		std::size_t JitScalarMapFastPathSets = 0;
		std::size_t JitNativeTypedLoopExecutions = 0;
		std::size_t JitScalarMethodFastPathI32 = 0;
		std::size_t JitScalarMethodFastPathU32 = 0;
		std::size_t JitScalarMethodFastPathF64 = 0;
		std::size_t JitScalarMethodFastPathMixed = 0;
		std::size_t JitScalarFunctionFastPathI32 = 0;
		std::size_t JitScalarFunctionFastPathU32 = 0;
		std::size_t JitScalarFunctionFastPathF64 = 0;
		std::size_t JitScalarFunctionFastPathMixed = 0;
		std::size_t JitScalarNativeFastPathCalls = 0;
		std::size_t JitDirectNativeCalls = 0;
		std::size_t JitLinearFrameReuses = 0;
		std::size_t JitLinearScratchReuses = 0;
		std::size_t JitLinearScratchAllocations = 0;
		std::size_t JitLinearArgumentScratchLeases = 0;
		std::size_t JitLinearArgumentScratchReuses = 0;

		// Per-module-generation type index. Exact linkage names are inserted first;
		// unqualified aliases retain the VM's historical first-match semantics.
		mutable const FBytecodeModule* TypeIndexModule = nullptr;
		mutable std::uint64_t TypeIndexGeneration = 0;
		mutable std::unordered_map<std::string, std::uint16_t> TypeIndexByName;
		mutable std::string LastTypeIndexName;
		mutable std::uint16_t LastTypeIndex = 0xFFFF;
		// Use existing index padding so cold lifecycle state does not shift dispatch data.
		bool bTeardownErrorActive = false;
		// Bounded canonical-type compatibility memo, scoped to this VM and program
		// generation. Alias/interface requests retain ModuleMeta's ambiguity rules.
		mutable const FBytecodeModule* TypeCompatibilityModule = nullptr;
		mutable std::uint64_t TypeCompatibilityGeneration = 0;
		mutable std::unordered_map<std::uint64_t, bool> TypeCompatibility;
		mutable bool bModuleLookupArraysDirty = false;

		// Native metadata lookup for object factories, host defaults, and host-value
		// callbacks. Rebuilt lazily after SetActiveNatives, then allocation-free on
		// hot hits.
		mutable const std::vector<FNativeFunction>* ActiveNativeIndexSource = nullptr;
		mutable std::unordered_map<std::string, std::uint16_t> ActiveNativeIndexByName;
		mutable std::unordered_map<std::string, std::uint16_t> ActiveNativeProviderByType;
		mutable std::unordered_set<std::string> ActiveHostTypeNames;
		mutable std::unordered_map<std::string, std::uint16_t> ActiveZeroArgHostCtorByType;

		// Small polymorphic JIT helper cache for object field name/type lookups.
		mutable std::array<FJitFieldCacheEntry, 64> JitFieldIc{};
		mutable std::array<std::uint8_t, 16> JitFieldIcNextWay{};

		// Lazily activated after a genuine fixed-cache collision. Narrow objects stay
		// entirely in the allocation-free L1; wide shapes receive a complete stable
		// field-slot table so they cannot thrash a small per-type associativity limit.
		mutable const FBytecodeModule* JitTypeFieldIcModule = nullptr;
		mutable std::uint64_t JitTypeFieldIcGeneration = 0;
		mutable std::vector<FJitTypeFieldCacheSet> JitTypeFieldIc;

		// Allocation-free L2 cache shared by VM frames. Thirty-two four-way sets
		// retain several colliding runtime types without turning a megamorphic call
		// site into repeated hierarchy walks. Entries remain generation-qualified.
		std::array<FMethodDispatchCacheEntry, 128> MethodDispatchIc{};
		std::array<std::uint8_t, 32> MethodDispatchIcNextWay{};

		// Activated lazily only after the fixed L2 observes a real collision. Two
		// compact method entries per runtime type prevent an arbitrarily wide
		// virtual site from repeatedly rebuilding inheritance traversal state.
		const FBytecodeModule* MethodDispatchTypeIcModule = nullptr;
		std::uint64_t MethodDispatchTypeIcGeneration = 0;
		std::vector<FMethodTypeDispatchCacheSet> MethodDispatchTypeIc;

		// Start IP of the instruction currently being executed (before advance).
		std::size_t ActiveInstructionStart = 0;

		/**
		 * While releasing locals after a frame was popped, RefineErrorLocation uses
		 * TeardownErrorFunction/Ip (Function may be null for InitCode -> InitDebugLines)
		 * unless Frames.size() > TeardownFrameDepth (nested destructor Run).
		 * Nested teardowns save/restore these fields so the outer scope is not cleared.
		 */
		const FObjectLifetimeScope* ObjectLifetimeScope = nullptr;
		const FBytecodeFunction* TeardownErrorFunction = nullptr;
		std::size_t TeardownErrorIp = 0;
		std::size_t TeardownFrameDepth = 0;
		bool bHasError = false;

		// Prevent an outer Linear trampoline from replacing a precise error site
		// already recovered in a failed nested JIT frame.
		bool bJitErrorLocationPinned = false;
		bool bHasPendingScriptException = false;
		std::uint32_t LanguageCopyDepth = 0;
		std::vector<const void*> ActiveContainerCopyReads;
		const FBytecodeModule* LanguageCopyPlanModule = nullptr;
		std::vector<bool> LanguageCopyHookTypes;
		FValue PendingScriptException = std::int32_t{0};
		FSourceLocation PendingScriptExceptionLocation;
		bool bExitRequested = false;
		int RequestedExitCode = 0;
		int EntryExitCode = 0;
		FScriptError Error;

		// Session-only cold state stays after the interpreter/JIT hot fields.
		// The snapshot belongs to the session lease owner, never to a raw VM.
		// The owning session alone can grant the one-shot module reuse permission.
		FJitStats* JitStatsAtRunStart = nullptr;
		std::size_t PreferredJitFunctionIndex = static_cast<std::size_t>(-1);

		// Keep cold signature metadata after the interpreter's frequently used state.
		std::unordered_map<std::uint16_t, std::shared_ptr<const FVmCallableSignature>> CallableSignatures;
		const FBytecodeModule* CallableSignatureModule = nullptr;
		std::uint64_t CallableSignatureGeneration = 0;

		struct FObjectTypePlan
		{
			std::shared_ptr<const std::string> NameIdentity;
			std::vector<FValue> ScalarDefaults;
			std::uint16_t DefaultConstructorIndex = 0xFFFF;
			std::uint8_t DefaultConstructorDepth = 0;
			bool bHasScalarDefaults = false;
			bool bHasTrivialCleanup = false;
		};
		// Only instantiated types acquire a plan. Object values are never retained.
		const FBytecodeModule* ObjectTypePlanModule = nullptr;
		std::uint64_t ObjectTypePlanGeneration = 0;
		std::unordered_map<std::uint16_t, FObjectTypePlan> ObjectTypePlans;

		// Binding snapshots are cold between execution entries. Keep them after
		// the existing state so caching does not shift interpreter/JIT hot fields.
		// They validate mutable raw tables and retain no script value roots.
		std::vector<FNativeFunction> NativeBindingSourceSnapshot;
		std::vector<FBytecodeNativeReference> BoundNativeReferenceSnapshot;
		std::size_t NativeBindingCacheBytes = 0;
		// Optional profiling is cold; keep its owner away from dispatch fields.
		std::shared_ptr<FJitDiagnosticState> JitDiagnostics;
	};
}
