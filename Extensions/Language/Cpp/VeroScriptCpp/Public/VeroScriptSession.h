// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptDiagnostics.h"
#include "VeroScriptConcurrent.h"
#include "VeroScriptCallInterceptor.h"
#include "VeroScriptHost.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptExternalSerialization.h"
#include "VeroScriptJit.h"
#include "VeroScriptJitDiagnostics.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptMutation.h"
#include "VeroScriptNative.h"
#include "VeroScriptPackage.h"
#include "VeroScriptRun.h"
#include "VeroScriptSessionDebug.h"
#include "VeroScriptValue.h"
#include "VeroScriptValueRoot.h"
#include "VeroScriptVmBackend.h"

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace VeroScript
{
	namespace Application
	{
		class FApplication;
		struct FApplicationServices;
	}
	struct FBytecodeModule;
	struct FHotReloadRollback;
	struct FHotReloadState;
	struct FExecutionCleanupState;
	struct FSessionJitState;
	struct FSessionExecutionScratch;
	class VeroScriptVm;

	struct FSessionJitCacheLimits
	{
		std::size_t MaxNativeBytes = 8u * 1024u * 1024u;
		std::size_t MaxRetainedPayloadBytes = 32u * 1024u * 1024u;
	};

	struct FSessionJitCacheStats
	{
		std::size_t CompileAttempts = 0;
		std::size_t RetainedPayloadBytes = 0;
		std::size_t NativeBytes = 0;
		bool bResourceLimitReached = false;
	};

	enum class EReloadState : std::uint8_t
	{
		None,
		Rejected,
		Pending,
		RolledBack,
		Applied,
		AppliedWithErrors,
		ResetRequired
	};

	struct FReloadStatus
	{
		std::uint64_t OperationId = 0;
		std::uint64_t PreviousGeneration = 0;
		std::uint64_t CandidateGeneration = 0;
		std::uint64_t CurrentGeneration = 0;
		std::uint64_t CommittedGeneration = 0;
		EReloadState State = EReloadState::None;
	};

	/**
	 * Long-lived embed session: load / run / reload with isolation.
	 * Host must outlive the session. Runs use fresh VM execution state; globals
	 * persist only when the explicit hot-reload mode is enabled.
	 * Failed Load/Reload keeps the previously loaded module.
	 * RegisterNative bindings persist across Reload/Clear.
	 *
	 * Ordinary Session calls are not internally synchronized. RequestCancel is
	 * the cross-thread exception described below. Embedded hosts should invoke
	 * Load/Run/Reload/Clear from one controlling thread (the UE game thread is a
	 * suitable owner) and request reloads at a point where Run is not active.
	 * Script cleanup also counts as active execution: destructor/native/host
	 * callbacks cannot re-enter APIs that mutate or execute the Session.
	 * Spawned script workers are different: registered native callbacks, instance
	 * interceptors, and mutation callbacks reachable from a worker may execute
	 * concurrently on that worker thread. Their captured state/UserData must outlive
	 * Run and synchronize shared access; Run does not return while owned workers remain.
	 */
	class VeroScriptSession
	{
	public:
		explicit VeroScriptSession(IEngineHost& Host);
		~VeroScriptSession() noexcept;

		VeroScriptSession(const VeroScriptSession&) = delete;
		VeroScriptSession& operator=(const VeroScriptSession&) = delete;

		/**
		 * Copy compilation/run configuration, including source overlays.
		 * Host-owned InputStream pointer is retained (must outlive Runs that use read_line).
		 * If Config.InputStream is non-null, clears any SetOwnedInputStream so the host pointer wins.
		 * If Config.InputStream is null, leaves owned stream unchanged (owned still preferred at Run).
		 * Rejected with a session diagnostic while an execution is in progress.
		 * Invalid memory limits are also rejected with a diagnostic before other
		 * configuration is changed; use SetMemoryLimits for a returned memory status.
		 * Returns false with an optional rejection reason before committing changes.
		 * Allocation/callback-copy exceptions propagate and may leave partial changes.
		 */
		bool ApplyConfig(const FEngineConfig& Config, std::string* OutError = nullptr);
		/**
		 * Borrowed owner-thread context for host memory values and external regions.
		 * Shared by successive execution VMs; Clear/reload do not close the domain.
		 * Session destruction closes it after script cleanup. Outstanding leases
		 * retain their bytes and can be released afterwards through Memory::ReleaseLease.
		 * Do not retain this reference beyond Session lifetime or manually call the
		 * context's Close/BeginExecution/EndExecution lifecycle operations.
		 */
		FMemoryContext& GetMemoryContext()
		{
			return *MemoryContext;
		}
		// Idle owner-thread operation. Zero limits are unlimited. Failure preserves
		// the previous limits, including reductions below live retained allocations.
		FMemoryResult SetMemoryLimits(FMemoryLimits Limits);
		FMemoryResult SetStandardLibraryLimits(FStandardLibraryLimits Limits);
		FStandardLibraryLimits GetStandardLibraryLimits() const
		{
			return StandardLibraryLimits;
		}
		bool SetNumericLimits(FNumericLimits Limits, FNumericError& Error);
		FNumericLimits GetNumericLimits() const
		{
			return NumericLimits;
		}
		// Snapshot of this Session's shared budget, including its script workers.
		FMemoryStats GetMemoryStats() const
		{
			return MemoryContext->GetStats();
		}

		/**
		 * Install a Session-local instance method interceptor. Each execution VM,
		 * including spawned worker VMs, receives a copy. Worker copies may execute
		 * concurrently and must synchronize shared captured state. Rejected while running.
		 */
		void SetInstanceCallInterceptor(FInstanceCallInterceptor Interceptor);
		// Worker VMs receive a copy; the callback may execute concurrently.
		void SetMutationCallback(FMutationCallback Callback);
		bool HasInstanceCallInterceptor() const
		{
			return static_cast<bool>(InstanceCallInterceptor);
		}

		/**
		 * Take ownership of an input stream for read_line (preferred over host pointer).
		 * Pass nullptr to release ownership and fall back to ApplyConfig's host pointer / cin.
		 * Rejected while running so an active VM cannot retain a dangling stream reference.
		 */
		void SetOwnedInputStream(std::unique_ptr<std::istream> Stream);

		// Convenience for SetOwnedInputStream; likewise rejected while running.
		void SetInputFromString(std::string Content);

		// Compilation setting; rejected while running.
		void SetUseBytecodeCache(bool bEnable);
		// The frozen registry is shared; callbacks run synchronously during compilation.
		// Configuration changes and execution are rejected from preparation callbacks.
		void SetAnnotationProcessing(FAnnotationProcessingOptions Options);
		const FAnnotationProcessingOptions& GetAnnotationProcessing() const
		{
			return AnnotationProcessing;
		}
		// Applies to the next compilation/reload. Callbacks use the compilation guard.
		void SetCompileProcessing(FCompileProcessingOptions Options);
		const FCompileProcessingOptions& GetCompileProcessing() const
		{
			return CompileProcessing;
		}
		// A null compile control inherits the Session execution control and follows
		// later replacements. An explicitly supplied control remains independent.
		// Applies to the next compilation/reload; rejected from active callbacks.
		void SetScriptCompileExtensions(FScriptCompileExtensionsOptions Options);
		// Returns configured options: a null control retains runtime-token inheritance.
		const FScriptCompileExtensionsOptions& GetScriptCompileExtensions() const
		{
			return ScriptCompileExtensions;
		}
		bool GetUseBytecodeCache() const
		{
			return bUseBytecodeCache;
		}

		/**
		 * Opt in to language-level state-preserving reload (VeroScript 0.1.1).
		 *
		 * After a module completes global initialization, subsequent Runs retain
		 * globals and script object/container identity (including after a runtime
		 * error in main). Reload validates and migrates that state transactionally
		 * before replacing the last good module. The old generation remains available
		 * until the new module completes global initialization; initialization
		 * failure (including exit() during init) restores it and runs destructors
		 * for exclusive allocations created by the failed replacement.
		 *
		 * Compatible changes keep identity (same-typed globals/fields; added fields
		 * receive language defaults). Incompatible changes are rejected and keep the
		 * last good module: removed retained globals/fields, type changes, live
		 * channels, and incompatible closure captures. Host-native side effects are
		 * outside the script transaction. Hot-reload sessions skip bytecode cache so Reload
		 * always recompiles from source. Disable/Clear releases retained values
		 * normally, including retained reference cycles.
		 */
		void SetHotReloadEnabled(bool bEnable);
		bool IsHotReloadEnabled() const
		{
			return bHotReloadEnabled;
		}

		/**
		 * Requested execution backend (vm / mixed / jit). Default Mixed.
		 * Hot-reload sessions and debugger runs still force the interpreter.
		 */
		void SetExecutionBackend(EExecutionBackend Backend);

		void SetJitBackendFactory(std::shared_ptr<const IJitBackendFactory> Factory);
		bool SetVmBackendFactory(std::shared_ptr<const IVmBackendFactory> Factory, std::string& Error);
		bool SetFallbackVmBackendFactory(std::shared_ptr<const IVmBackendFactory> Factory, std::string& Error);
		bool SetVmBackendPath(std::string_view Path, std::string& Error);
		const std::shared_ptr<FVmExecutionDomain>& GetVmExecutionDomain() const noexcept
		{
			return VmExecutionDomain;
		}
		void SetAotProgram(std::shared_ptr<const IAotProgram> Program);
		const std::shared_ptr<const IAotProgram>& GetAotProgram() const noexcept
		{
			return AotProgram;
		}

		// Empty paths disable JIT. Failed loads preserve the current backend/cache.
		bool SetJitBackendPath(std::string_view Path, std::string& OutError);

		const std::shared_ptr<const IJitBackendFactory>& GetJitBackendFactory() const noexcept
		{
			return JitBackendFactory;
		}
		EExecutionBackend GetExecutionBackend() const
		{
			return ExecutionBackend;
		}
		// Per-run recursion/resource limit. JIT safely falls back to iterative VM
		// frames before native recursion can consume the host thread stack.
		void SetMaxCallDepth(std::size_t Limit);
		std::size_t GetMaxCallDepth() const
		{
			return MaxCallDepth;
		}
		void SetExecutionLimits(FExecutionLimits Limits);
		const FExecutionLimits& GetExecutionLimits() const
		{
			return ExecutionLimits;
		}
		void SetExecutionControl(std::shared_ptr<FExecutionControl> Control);
		// Owner-thread borrowed reference; not a synchronized snapshot.
		const std::shared_ptr<FExecutionControl>& GetExecutionControl() const
		{
			return ExecutionControl;
		}
		/**
		 * Thread-safe, including while executing or the owner replaces the control.
		 * Cancels a shared snapshot of the currently configured control; nullptr is
		 * a no-op. Cancellation is not transferred to a different control. Sessions
		 * sharing the same token share cancellation. Complete prior cancellation
		 * callers before Reset/reuse of that token. Session must outlive this call.
		 */
		void RequestCancel() const noexcept;
		/**
		 * Install/remove the native live-debug bridge at an idle owner-thread point.
		 * Returns false while executing; the existing bridge is then unchanged.
		 * Installation does not load or run code. nullptr detaches the bridge.
		 * The Session shares ownership until removal/destruction. Use a different
		 * bridge for each Session and weak Session references inside the bridge.
		 */
		bool SetDebugBridge(std::shared_ptr<ISessionDebugBridge> Bridge);
		// Borrowed view: call only on the controlling thread; not a synchronized snapshot.
		FSessionDebugProgramView GetDebugProgram() const noexcept;
		void SetCapabilities(FScriptCapabilities InCapabilities);
		const FScriptCapabilities& GetCapabilities() const
		{
			return Capabilities;
		}

		/**
		 * Adaptive JIT thresholds and compile/entry budgets for subsequent execution.
		 * Eligible Run, function/method and retained-callable invocations may retain
		 * compiled code across fresh VMs when bReuseSessionCode is true and the
		 * binding/configuration revisions remain stable, including persistent state.
		 * Debugging, retained initialization and bounded cleanup use the VM path.
		 */
		void SetJitPolicy(FJitPolicy Policy);
		// Reusable session code only; excludes transient compiler/VM
		// memory and allocator bookkeeping. Zero permits no retained payload of
		// that kind. Exceeding a limit stops JIT for this generation/configuration.
		// Explicit policy/limit changes begin a new budget context. Rejected while running.
		void SetJitCacheLimits(FSessionJitCacheLimits Limits);
		FSessionJitCacheLimits GetJitCacheLimits() const
		{
			return JitCacheLimits;
		}
		// Settled at invocation completion, including C++ exception unwinding;
		// not a live usage counter while executing. Existing external-sync rules apply.
		FSessionJitCacheStats GetJitCacheStats() const;
		const FJitPolicy& GetJitPolicy() const
		{
			return JitPolicy;
		}

		// Per-invocation snapshot from the most recent Run/call execution. This is
		// distinct from cumulative GetJitCacheStats() and is cleared by Clear().
		const FJitStats& GetLastJitStats() const
		{
			return LastJitStats;
		}
		/** Optional, owning snapshot captured before the invocation's VM is destroyed.
		 * Collection is disabled by default. A run with collection disabled, or Clear,
		 * clears this snapshot; copy it to retain it across later Session calls. */
		const FJitDiagnostics& GetLastJitDiagnostics() const
		{
			return LastJitDiagnostics;
		}

		/**
		 * Register a host callback callable from script by Name.
		 * Survives Reload/Clear. Register before Load so the compiler can resolve calls.
		 * A callback reachable from a spawned worker runs on that worker thread; its
		 * UserData and host state must remain valid through Run and be thread-safe.
		 * Rejects empty names, null callbacks, Void params, duplicates, and builtin names.
		 * HostObject / Enum / Function slots use named-type parallels; unary
		 * containers use element names; map variants use key/value parallels.
		 * ParamFlags: optional ref / const-ref markers.
		 */
		bool RegisterNative(
		    std::string Name,
		    ENativeType ReturnType,
		    std::vector<ENativeType> ParamTypes,
		    FNativeCallback Callback,
		    void* UserData = nullptr,
		    std::string ReturnHostTypeName = {},
		    std::vector<std::string> ParamHostTypeNames = {},
		    std::string ReturnElementTypeName = {},
		    std::vector<std::string> ParamElementTypeNames = {},
		    std::vector<std::uint8_t> ParamFlags = {},
		    std::string ReturnKeyTypeName = {},
		    std::string ReturnValueTypeName = {},
		    std::vector<std::string> ParamKeyTypeNames = {},
		    std::vector<std::string> ParamValueTypeNames = {});

		// Preferred structured native registration API.
		bool RegisterNative(
		    std::string Name,
		    FNativeTypeDesc ReturnType,
		    std::vector<FNativeParamDesc> Params,
		    FNativeCallback Callback,
		    void* UserData = nullptr);

		/**
		 * Catalog a host class (enum/struct metadata also accepted) and wire live ctor/method
		 * natives when members are registered with Callback. Survives Reload/Clear.
		 * Opaque host objects use FHostObjectHandle; see MakeHostObject.
		 */
		bool RegisterHostType(std::string Name, EHostTypeKind Kind, FHostTypeId* OutTypeId = nullptr);

		bool RegisterGenericHostTypeTemplate(
		    std::string Name,
		    std::size_t Arity,
		    FGenericHostTypeInstantiateCallback Instantiate,
		    void* UserData = nullptr,
		    FGenericHostTypeTemplateId* OutTemplateId = nullptr,
		    std::string Documentation = {});
		bool InstantiateGenericHostType(
		    FGenericHostTypeTemplateId TemplateId,
		    std::vector<std::string> TypeArguments,
		    FHostTypeId* OutTypeId = nullptr);

		/**
		 * Register a member on a host type. Constructor / Method / StaticMethod /
		 * StaticConstant / Property*
		 * / Field with Callback become callable from script (`Type(...)`, `obj.method(...)`,
		 * `obj.prop` / `obj.prop = v`, `Type.staticMethod(...)`, `Type::Constant`).
		 */
		bool RegisterHostMember(FHostTypeId TypeId, FHostMemberDesc Member);
		bool RegisterHostOperator(FHostTypeId TypeId, FHostMemberDesc Operator);

		/**
		 * Deep-copy callback for host structs (pass-by-value / nested fields / CloneStruct).
		 * When set, native returns stamp `@clone`; stamped copies deep-copy. Without it,
		 * bare host-struct handles are shared. Script `struct X : HostStruct` HostBase
		 * requires `@clone` at construct time.
		 */
		bool RegisterHostTypeClone(
		    FHostTypeId TypeId,
		    FNativeCallback Callback,
		    void* UserData = nullptr);

		/**
		 * Allow script `class/struct X : HostName` for a Class/Struct host type.
		 */
		bool SetHostTypeInheritable(FHostTypeId TypeId, bool bInheritable);

		// Enable or disable strong/scoped value semantics for a registered host enum.
		bool SetHostEnumScoped(FHostTypeId TypeId, bool bScoped = true);

		/**
		 * Register `@subclass` factory used when constructing a script subclass of Host.
		 * Args[0] is the script type linkage name (string); return a HostObject of this type.
		 */
		bool RegisterHostTypeSubclassFactory(
		    FHostTypeId TypeId,
		    FNativeCallback Callback,
		    void* UserData = nullptr);

		// Borrowed catalog; valid until the current module environment is replaced.
		const IHostBindingCatalog& GetBindingCatalog() const
		{
			return EffectiveBindings();
		}

		// Mutable registry for advanced hosts that register in bulk while idle.
		// Throws std::logic_error when an environment supplies the published catalog.
		// The mutable overload also rejects execution/compilation/cleanup callbacks.
		// Do not mutate it during compilation, execution or cleanup callbacks;
		// a borrowed registry reference cannot enforce Session's reentry guards.
		FHostBindingRegistry& GetBindingRegistry();
		const FHostBindingRegistry& GetBindingRegistry() const;
		// Retain the immutable snapshot when inspecting its catalog or memory sources.
		std::shared_ptr<const FModuleEnvironment> GetModuleEnvironment() const
		{
			return ModuleEnvironment;
		}

		/**
		 * Write the current binding catalog to a UTF-8 JSON file for IDE / LSP / DAP.
		 * On failure writes a host diagnostic line and returns false.
		 */
		bool ExportBindingsToFile(std::string_view FilePath);

		FRunResult LoadFile(std::string_view FilePath);
		FRunResult LoadSource(std::string_view Source, std::string_view FileLabel);
		/**
		 * Load a package module as the current generation. This has Load semantics
		 * (no state migration); Package.Module is consumed only on success.
		 * Verify Package.CanonicalSignedBytes before calling when trust is required.
		 */
		FRunResult LoadPackage(
		    FPackageData& Package,
		    std::string LogicalPath);
		// Container/directory loading preserves the current module on preparation failure.
		FRunResult LoadApplication(std::shared_ptr<const Application::FApplication>);
		// Preserves script state transactionally. Changing native catalogs requires Load.
		FRunResult ReloadApplication(std::shared_ptr<const Application::FApplication>);
		FRunResult LoadApplication(std::string_view AbsolutePath, const Application::FApplicationServices&);
		/**
		 * Compile a file without replacing the loaded module.
		 * On failure OutModule is left empty and the current module is untouched.
		 * Rejected while a session call is in progress.
		 */
		FRunResult CompileFile(
		    std::string_view FilePath,
		    std::unique_ptr<FBytecodeModule>& OutModule);
		/**
		 * Commit a module previously produced by CompileFile. Discards hot-reload
		 * retained state (Load semantics, not Reload). Rejected while a session call
		 * is in progress. On failure NewModule is left owned by the caller; on
		 * success it is taken and the session owns the module.
		 */
		FRunResult CommitCompiledFile(
		    std::unique_ptr<FBytecodeModule>& NewModule,
		    std::string FilePath);
		/**
		 * Re-read last LoadFile path from disk. Fails if last load was source-only.
		 * With hot reload enabled: compile/migrate transactionally; on success the
		 * replacement is staged until the next Run completes global initialization.
		 */
		FRunResult Reload();
		/**
		 * Transactionally stage an already compiled module without requiring a
		 * source file or package envelope. NewModule remains owned by the caller
		 * on validation/migration failure and is consumed once staging succeeds.
		 * The previous generation is restored if first initialization fails.
		 */
		FRunResult ReloadCompiledModule(
		    std::unique_ptr<FBytecodeModule>& NewModule,
		    std::string LogicalPath);
		/**
		 * Transactionally stage a validated package module without requiring its
		 * original .vs source. Package.Module remains owned by the caller on every
		 * failure and is consumed only after migration succeeds. The old generation
		 * remains available until the replacement completes global initialization.
		 *
		 * Enable hot reload before the original module's first Run when live
		 * globals/objects must migrate. Without retained state, the replacement
		 * is still staged and initialization is rolled back transactionally, but
		 * there is no prior runtime state available to migrate.
		 *
		 * Hosts must verify any detached signature over
		 * Package.CanonicalSignedBytes before calling this API.
		 */
		FRunResult ReloadPackage(
		    FPackageData& Package,
		    std::string LogicalPath);
		// Owner-thread snapshot. OperationId is independent of reusable program
		// generation numbers. Pending is not an applied replacement; AppliedWithErrors
		// means initialization committed before a later business/cleanup failure.
		FReloadStatus GetReloadStatus() const noexcept;
		/** Run the loaded application. Inspect both bSuccess and ExitCode: a
		 * successful int main or exit(n) may intentionally return a non-zero code. */
		FRunResult Run(ERunMode RunMode = ERunMode::Script);

		/**
		 * Invoke an instance method by bare name + argument count (virtual dispatch on
		 * Object's runtime type). Does not run main. Rejected while Run/CallMethod/
		 * ConstructObject is already in progress. With hot reload enabled, installs and
		 * captures persistent globals like Run(Eval).
		 * Missing method → ExitCode 4 + MessageId diag.callmethod_unknown_method
		 * (Quiet skips writing the diagnostic stream).
		 */
		FRunResult CallMethod(
		    const FObjectHandle& Object,
		    std::string_view MethodName,
		    const std::vector<FValue>& Arguments = {},
		    FValue* OutReturn = nullptr,
		    EMissingMethodPolicy MissingPolicy = EMissingMethodPolicy::Error);

		/**
		 * Non-const-argument overload with `ref` parameter write-back: after a
		 * successful call, Arguments[i] receives the callee's final value for every
		 * writable `ref` parameter (including whole-value reassignment such as
		 * `Param = T();`). Value and `const ref` parameters are left untouched, and
		 * nothing is written back when the call fails (ExitCode != 0) or exits.
		 */
		FRunResult CallMethod(
		    const FObjectHandle& Object,
		    std::string_view MethodName,
		    std::vector<FValue>& Arguments,
		    FValue* OutReturn = nullptr,
		    EMissingMethodPolicy MissingPolicy = EMissingMethodPolicy::Error);

		/**
		 * Invoke the resolved entry method body without intercepting that entry.
		 * Nested instance calls made by the body remain interceptable. Use this for
		 * a later host/inbound dispatch; inside an interceptor callback, use Proceed
		 * so the already-resolved target and overload cannot change.
		 */
		FRunResult CallMethodBypassingInterceptor(
		    const FObjectHandle& Object,
		    std::string_view MethodName,
		    const std::vector<FValue>& Arguments = {},
		    FValue* OutReturn = nullptr,
		    EMissingMethodPolicy MissingPolicy = EMissingMethodPolicy::Error);
		FRunResult CallMethodBypassingInterceptor(
		    const FObjectHandle& Object,
		    std::string_view MethodName,
		    std::vector<FValue>& Arguments,
		    FValue* OutReturn = nullptr,
		    EMissingMethodPolicy MissingPolicy = EMissingMethodPolicy::Error);

		/**
		 * True when Object's runtime type exposes MethodName with ArgumentCount
		 * (unique or overloaded). Does not execute; safe while Run is active.
		 */
		bool HasInstanceMethod(
		    const FObjectHandle& Object,
		    std::string_view MethodName,
		    std::uint8_t ArgumentCount) const;

		/**
		 * Construct a script object by linkage or simple type name, optionally calling a
		 * matching constructor. Does not run main. Same reentry / hot-reload rules as
		 * CallMethod. Missing/ambiguous type or constructor (when args are passed) →
		 * ExitCode 4 + diag.constructobject_* MessageIds.
		 */
		FRunResult ConstructObject(
		    std::string_view TypeName,
		    const std::vector<FValue>& Arguments,
		    FObjectHandle& OutObject);

		/**
		 * Invoke a free function by bare or mangled name + argument count.
		 * Does not run main. Same reentry / hot-reload / missing policy rules as CallMethod
		 * (ExitCode 4 + diag.callfunction_* MessageIds).
		 */
		FRunResult CallFunction(
		    std::string_view FunctionName,
		    const std::vector<FValue>& Arguments = {},
		    FValue* OutReturn = nullptr,
		    EMissingMethodPolicy MissingPolicy = EMissingMethodPolicy::Error);

		/**
		 * Retain a first-class script callable beyond the current native callback.
		 * Rejected during script cleanup so teardown cannot acquire new retained roots.
		 * The returned identity belongs to this session. By default later invocation
		 * is restricted to the thread that created the session; pass false only when
		 * the host externally serializes all access to this session.
		 * This does not transfer the Session's memory domain: std::memory region
		 * operations still require the Session owner thread, even with false.
		 */
		FRetainedCallableHandle RetainCallable(
		    const FFunctionHandle& Callable,
		    bool bOwnerThreadOnly = true,
		    ERetainedThisCapture ThisCapture = ERetainedThisCapture::Strong);
		// Releases only this registration; other roots keep shared captures alive.
		// A serialized foreign-thread release of a cross-thread callable retires
		// its value for cleanup at the next owner-thread safe point.
		bool ReleaseCallable(FRetainedCallableHandle Callable);
		bool IsCallableValid(FRetainedCallableHandle Callable) const;
		FRunResult InvokeCallable(
		    FRetainedCallableHandle Callable,
		    const std::vector<FValue>& Arguments = {},
		    FValue* OutReturn = nullptr);
		FRunResult InvokeCallable(
		    FRetainedCallableHandle Callable,
		    std::vector<FValue>& Arguments,
		    FValue* OutReturn = nullptr);

		/**
		 * Apply base-to-derived class-default blocks to a live object. The host may
		 * create/configure external subobjects before calling this entry point.
		 */
		FRunResult ApplyClassDefaults(const FObjectHandle& Object);

		bool RegisterExternalSerializer(
		    std::string TypeName,
		    FExternalSerializerDesc Serializer,
		    std::string* OutError = nullptr);
		bool SerializeObject(
		    const FObjectHandle& Object,
		    std::vector<std::uint8_t>& OutBytes,
		    std::uint32_t& OutVersion,
		    std::string& OutError) const;
		bool DeserializeObject(
		    const FObjectHandle& Object,
		    const std::uint8_t* Bytes,
		    std::size_t ByteCount,
		    std::uint32_t EncodedVersion,
		    std::string& OutError);
		// Read-only traversal: the visitor may retain handles but must not mutate
		// graph fields or container storage until enumeration returns.
		bool EnumerateObjectReferences(
		    const FObjectHandle& Object,
		    const FExternalReferenceVisitor& Visitor,
		    std::string& OutError) const;

		/**
		 * Non-const-argument overload with `ref` parameter write-back
		 * (same contract as the CallMethod write-back overload).
		 */
		FRunResult CallFunction(
		    std::string_view FunctionName,
		    std::vector<FValue>& Arguments,
		    FValue* OutReturn = nullptr,
		    EMissingMethodPolicy MissingPolicy = EMissingMethodPolicy::Error);

		// True when a free function matches name + arity (unique or overloaded).
		bool HasFreeFunction(
		    std::string_view FunctionName,
		    std::uint8_t ArgumentCount) const;

		/**
		 * Read/write a script instance field by name (flattened layout).
		 * Set clones via AssignClonedValue (struct/@clone policy). Rejected while Run
		 * is active. Unknown field → ExitCode 4 + diag.getfield_unknown_field /
		 * diag.setfield_unknown_field; validation failures → ExitCode 2.
		 */
		FRunResult GetField(
		    const FObjectHandle& Object,
		    std::string_view FieldName,
		    FValue& OutValue);
		FRunResult SetField(
		    const FObjectHandle& Object,
		    std::string_view FieldName,
		    const FValue& Value);

		/**
		 * Read/write a retained global by name. Requires hot reload enabled and a
		 * completed module initialization (after Run/CallMethod/ConstructObject/CallFunction).
		 * Missing/ambiguous → ExitCode 4 + diag.getglobal_* / diag.setglobal_*;
		 * missing hot reload or init → ExitCode 2.
		 */
		FRunResult GetGlobal(std::string_view GlobalName, FValue& OutValue);
		FRunResult SetGlobal(std::string_view GlobalName, const FValue& Value);

		/**
		 * Loaded bytecode module, or null when nothing is loaded.
		 * Valid until Clear / successful Load / Reload replaces it. Prefer the
		 * Enumerate* helpers for UClass / lifecycle registration. For advanced
		 * resolve / field-index helpers use ModuleMeta::* on this module.
		 * Read-only inspection: while session JIT reuse is enabled, do not mutate
		 * loaded bytecode through const_cast or aliases retained before commit.
		 * Submit changes through Load/Reload/Commit to create a new generation.
		 */
		const FBytecodeModule* GetLoadedModule() const
		{
			return Module.get();
		}

		/**
		 * Copy type-table snapshots. When bHostSubclassesOnly, only types with a
		 * HostBase (script `class/struct X : Host`) are included.
		 */
		bool EnumerateTypes(
		    std::vector<FLoadedTypeInfo>& Out,
		    bool bHostSubclassesOnly = false) const;

		/**
		 * Flattened script fields for TypeName (name + type name). When bDeclaredOnly,
		 * only fields introduced on that type (not inherited).
		 */
		bool EnumerateFields(
		    std::string_view TypeName,
		    std::vector<FLoadedFieldInfo>& Out,
		    bool bDeclaredOnly = false) const;

		/**
		 * True when TypeName (linkage or simple) exposes MethodName with ArgumentCount
		 * (unique or overloaded) after virtual/base/interface resolution.
		 */
		bool HasInstanceMethodOnType(
		    std::string_view TypeName,
		    std::string_view MethodName,
		    std::uint8_t ArgumentCount) const;

		/**
		 * Enumerate instance methods on TypeName. Most-derived override wins when
		 * bIncludeInherited. Constructors/destructors omitted unless requested.
		 * TypeName may also be an interface.
		 */
		bool EnumerateInstanceMethods(
		    std::string_view TypeName,
		    std::vector<FInstanceMethodInfo>& Out,
		    bool bIncludeInherited = true,
		    bool bIncludeConstructors = false,
		    bool bIncludeDestructors = false) const;

		/**
		 * Enumerate free functions in the loaded module (deduped by FunctionIndex).
		 */
		bool EnumerateFreeFunctions(std::vector<FFreeFunctionInfo>& Out) const;

		// Values held across calls must be rooted. Retiring a root is thread safe;
		// cleanup and access remain owner-thread operations.
		// Module-dependent external roots can only be published after replacement
		// initialization commits. Native generation retention remains available.
		bool RetainValue(const FValue& Value, FValueRoot& Output, std::string& Error);
		// Opt-in roots join the same graph migration as globals. Read() is unavailable
		// between staging and settlement. Load/Clear still require their release.
		bool RetainMigratableValue(const FValue& Value, FValueRoot& Output, std::string& Error);
		bool CanExportModuleValues() const noexcept
		{
			return !PendingHotReloadRollback && !bCleaningUp;
		}
		bool ReserveValueResult(FValueRoot& Output, std::string& Error)
		{
			return ValueRoots->Reserve(Output, Error);
		}
		bool StageValueResult(FValueRoot& Root, FValue& Prepared, std::string& Error)
		{
			if (PendingHotReloadRollback && ValueRequiresOwner(Prepared))
			{
				Error = "cannot export module values before replacement initialization commits";
				return false;
			}
			if (bCleaningUp && ValueRequiresOwner(Prepared))
			{
				Error = "cannot export module values during final graph cleanup";
				return false;
			}
			return ValueRoots->CommitReserved(Root, Prepared, Error);
		}
		bool DrainValueRoots(std::string& Error);
		void BeginCloseValues() noexcept
		{
			ValueRoots->BeginClose();
		}
		bool ValuesOpen() const noexcept
		{
			return ValueRoots->IsOpen();
		}
		bool MakeVariant(std::string_view TypeName, const FValue& Payload, FValue& Output, std::string& Error);

		// Enumerate top-level globals (static members remain field metadata).
		bool EnumerateGlobals(std::vector<FLoadedGlobalInfo>& Out) const;

		/**
		 * Enumerate `static` methods on TypeName (class/struct or interface).
		 */
		bool EnumerateStaticMethods(
		    std::string_view TypeName,
		    std::vector<FStaticMethodInfo>& Out) const;

		/**
		 * Enumerate script interfaces (fields + DeclMeta) from the loaded module.
		 */
		bool EnumerateInterfaces(std::vector<FLoadedInterfaceInfo>& Out) const;

		/**
		 * Enumerate script enums (members + DeclMeta) from the loaded module.
		 */
		bool EnumerateEnums(std::vector<FLoadedEnumInfo>& Out) const;

		void Clear();

		// Clear and reload a fresh program after bounded cleanup could not finish.
		bool RequiresReset() const noexcept
		{
			return bExecutionResetRequired;
		}

		/**
		 * True when a module is loaded. Clear drops the module; held FObjectHandle values
		 * remain alive but Invoke APIs return diag.no_module_loaded until the next Load.
		 * Do not CallMethod/ConstructObject across Clear / non–hot-reload Load with stashed
		 * instances from a previous generation.
		 */
		bool IsLoaded() const;
		// Source path for LoadFile, host-supplied logical path for packages, and
		// empty for LoadSource. A package logical path is never recompiled by Reload().
		const std::string& LoadedFilePath() const
		{
			return FilePath;
		}

	private:
		struct FRetainedCallableEntry
		{
			std::uint64_t Generation = 0;
			FFunctionHandle Callable;
			std::weak_ptr<FScriptObject> WeakThis;
			bool bOwnerThreadOnly = true;
			bool bWeakThis = false;
		};

		std::vector<std::string_view> MakeImportPathViews() const;

		std::vector<std::string_view> MakeImplicitImportPathViews() const;

		std::vector<std::string_view> MakeScriptArgViews() const;

		std::vector<std::string_view> MakeDefineViews() const;

		std::vector<std::string_view> MakeStdlibSearchPathViews() const;
		FScriptCompileExtensionsOptions MakeScriptCompileExtensionOptions() const;

		std::istream* ResolveInputStream() const;

		// Owned / host input stream, or std::cin when neither is set.
		std::istream& InputStreamOrCin() const;

		FRunResult CommitModule(
		    std::unique_ptr<FBytecodeModule>& NewModule,
		    std::string NewFilePath,
		    const FRunResult& CompileResult,
		    std::optional<EPackageKind> NewPackageKind = std::nullopt,
		    std::shared_ptr<const FModuleEnvironment> NewEnvironment = {});

		FRunResult StageReloadedModule(
		    std::unique_ptr<FBytecodeModule>& NewModule,
		    std::string LogicalPath,
		    const FRunResult& SuccessResult,
		    bool bForceTransaction,
		    std::optional<EPackageKind> NewPackageKind = std::nullopt,
		    std::shared_ptr<const FModuleEnvironment> NewEnvironment = {});
		FRunResult LoadApplicationInternal(std::shared_ptr<const Application::FApplication>, bool bReload);

		void ReportNativeError(
		    std::string_view MessageId,
		    std::initializer_list<FDiagnosticArg> Args = {});

		bool RejectIfRunning(std::string_view Action, FRunResult* OutResult = nullptr);

		void DiscardHotReloadState();
		void DiscardUnsettledHotReloadState(std::vector<FValue>& Roots);

		bool DiscardRetainedCallables(
		    const FBytecodeModule* DefiningModule,
		    std::uint64_t DefiningGeneration = 0,
		    bool bPreserveValues = false,
		    FScriptError* OutError = nullptr);

		void RestorePendingHotReload();
		bool BeginReloadOperation();

		void AdvanceProgramGeneration();

		// A failed configuration must never enter a VM operation that begins a request.
		[[nodiscard]] bool ConfigureExecutionVm(VeroScriptVm& Vm) const;
		FRunResult MakeResetRequiredResult() const;

		bool LeaseReusableJit(VeroScriptVm& Vm);

		void RecoverReusableJit(VeroScriptVm& Vm, bool bCanRetainCode, bool bSucceeded);
		void NotifyDebugProgramChanged() noexcept;

		// After Run/Call: commit or restore staged reload; marks Result failed on restore.
		void SettlePendingHotReload(FRunResult& Result, std::ostream& ErrorStream);

		// Shared body for the const / write-back CallMethod overloads.
		FRunResult CallMethodInternal(
		    const FObjectHandle& Object,
		    std::string_view MethodName,
		    const std::vector<FValue>& Arguments,
		    std::vector<FValue>* OutArguments,
		    FValue* OutReturn,
		    EMissingMethodPolicy MissingPolicy,
		    bool bBypassEntryInterceptor);

		// Shared body for the const / write-back CallFunction overloads.
		FRunResult CallFunctionInternal(
		    std::string_view FunctionName,
		    const std::vector<FValue>& Arguments,
		    std::vector<FValue>* OutArguments,
		    FValue* OutReturn,
		    EMissingMethodPolicy MissingPolicy);

		FRunResult InvokeCallableInternal(
		    FRetainedCallableHandle Callable,
		    const std::vector<FValue>& Arguments,
		    std::vector<FValue>* OutArguments,
		    FValue* OutReturn);

		const IHostBindingCatalog& EffectiveBindings() const;
		bool RejectBindingMutation(std::string_view Action);

		IEngineHost& Host;
		std::vector<std::string> ImportPaths;
		std::vector<std::string> ImplicitImportPaths;
		bool bAllowCircularImports = true;
		bool bAllowProjectWideAccess = false;
		std::vector<std::string> ScriptArgs;
		std::vector<std::string> Defines;
		std::vector<FPredefinedMacro> PredefinedMacros;
		FInstanceCallInterceptor InstanceCallInterceptor;
		FMutationCallback MutationCallback;
		std::shared_ptr<ISessionDebugBridge> DebugBridge;
		std::uint64_t ProgramGeneration = 1;
		std::shared_ptr<const void> ProgramOwner = std::make_shared<int>(0);
		std::shared_ptr<FValueRootOwner> ValueRoots = std::make_shared<FValueRootOwner>();
		bool CleanValueRoots(std::vector<FValue>& Values, const std::vector<FValue>& Keep,
		    std::string* Error = nullptr, std::vector<FValue>* ActiveGlobals = nullptr, VeroScriptVm* ActiveVm = nullptr) const;
		void AppendRollbackKeepValues(std::vector<FValue>& Keep) const;
		bool RetainValueImpl(const FValue& Value, FValueRoot& Output, std::string& Error, bool Migratable);
		bool PrepareValueRootReplacement(std::string& Error, bool AllowMigration = false);
		bool SettleMigratableRoots(bool Applied, std::string& Error);
		// Cleanup runs in a separate VM, but weak IDs still refer to the owning
		// execution/session table. Borrow it without copying or leasing globals.
		struct FWeakSlotScope
		{
			FWeakSlotScope(VeroScriptVm& InVm, FHotReloadState* State, VeroScriptVm* InActiveVm = nullptr);
			~FWeakSlotScope();
			FWeakSlotScope(const FWeakSlotScope&) = delete;
			FWeakSlotScope& operator=(const FWeakSlotScope&) = delete;
			VeroScriptVm& Vm;
			VeroScriptVm* ActiveVm;
			std::shared_ptr<void> Persistent;
		};
		struct FValueOperationScope
		{
			FValueOperationScope(VeroScriptVm& InVm, FHotReloadState* InState);
			~FValueOperationScope();
			FValueOperationScope(const FValueOperationScope&) = delete;
			FValueOperationScope& operator=(const FValueOperationScope&) = delete;
			VeroScriptVm& Vm;
			FHotReloadState* State;
			FWeakSlotScope WeakSlots;
		};
		struct FValueSafePoint
		{
			explicit FValueSafePoint(VeroScriptSession& In, const std::vector<FValue>* InArguments = nullptr,
			    const FValue* InReturn = nullptr, const FObjectHandle* InObject = nullptr);
			~FValueSafePoint() noexcept;
			bool Ready() const noexcept
			{
				return Error.empty();
			}
			FRunResult Finish(FRunResult Result);

			VeroScriptSession& Session;
			const std::vector<FValue>* Arguments;
			const FValue* Return;
			const FObjectHandle* Object;
			FValueSafePoint* Previous;
			std::uint64_t PendingReloadOperation = 0;
			std::string Error;
			bool bFinished = false;
		};
		FValueSafePoint* ActiveValueSafePoint = nullptr;
		const std::thread::id OwnerThreadId = std::this_thread::get_id();
		mutable std::mutex RetainedCallableMutex;
		std::unordered_map<std::uint64_t, FRetainedCallableEntry> RetainedCallables;
		std::unordered_map<std::string, FExternalSerializerDesc> ExternalSerializers;
		FHostBindingRegistry Bindings;
		std::shared_ptr<const FModuleEnvironment> ModuleEnvironment;

		FAnnotationProcessingOptions AnnotationProcessing;
		FCompileProcessingOptions CompileProcessing;
		FScriptCompileExtensionsOptions ScriptCompileExtensions;
		bool bUseBytecodeCache = false;
		std::string BytecodeCacheDirectory;
		std::vector<std::string> StdlibSearchPaths;

		// Owned open-buffer contents used by LoadSource/LoadFile/Reload import resolution.
		std::vector<FSourceOverlay> SourceOverlays;

		// Const inspection APIs also execute host callbacks and therefore participate
		// in the same no-reentry contract as script execution.
		mutable bool bRunning = false;
		bool bCompiling = false;
		mutable bool bCleaningUp = false;
		mutable bool bExecutionResetRequired = false;
		mutable std::shared_ptr<FExecutionCleanupState> ActiveExecutionCleanup;
		bool bHotReloadEnabled = false;
		EExecutionBackend ExecutionBackend = EExecutionBackend::Mixed;
		mutable std::shared_ptr<const IJitBackendFactory> JitBackendFactory;
		bool bDisableJitBackend = false;
		// Explicit automatic selection can succeed without an optional JIT factory.
		std::string JitBackendSelectionRequest;
		bool bDisableEnvironmentVm = false;
		mutable bool bVmBackendFallback = false;
		std::shared_ptr<const IVmBackendFactory> VmFallbackFactory;
		std::string VmFallbackPath;
		mutable std::shared_ptr<const FModuleEnvironment> BackendDefaultsEnvironment;
		mutable EExecutionBackend BackendDefaultsMode = EExecutionBackend::Vm;
		std::shared_ptr<FVmExecutionDomain> VmExecutionDomain = std::make_shared<FVmExecutionDomain>();
		std::string VmBackendSelectionPath;
		std::shared_ptr<const IAotProgram> AotProgram;

		// 0 means resolve VERO_MAX_CALL_DEPTH/default when each VM is created.
		std::size_t MaxCallDepth = 0;
		FExecutionLimits ExecutionLimits;
		std::shared_ptr<FExecutionControl> ExecutionControl;
		FScriptCapabilities Capabilities;
		std::shared_ptr<FMemoryContext> MemoryContext;
		std::shared_ptr<I18n::FI18nServices> I18nServices;
		I18n::FI18nLimits I18nLimits;
		FStandardLibraryLimits StandardLibraryLimits;
		FNumericLimits NumericLimits;
		std::shared_ptr<Concurrent::FExecutorState> Executor = std::make_shared<Concurrent::FExecutorState>();
		FJitPolicy JitPolicy;
		FJitStats LastJitStats;
		FJitDiagnostics LastJitDiagnostics;

		// Optional host-owned stdin for read_line; null uses std::cin when no owned stream.
		std::istream* HostInputStream = nullptr;

		// Optional session-owned stream; preferred over HostInputStream when set.
		std::unique_ptr<std::istream> OwnedInputStream;
		std::shared_ptr<FBytecodeModule> Module;

		// Clean execution buffers and the ordinary CallFunction stream bridge.
		// No script roots or VM state may cross a call through this sidecar.
		std::unique_ptr<FSessionExecutionScratch> ReusableExecution;

		// Destroyed before Module; private layout change requires C++ host rebuild.
		std::unique_ptr<FSessionJitState> ReusableJit;
		FSessionJitCacheLimits JitCacheLimits;

		// Previous module retained until the replacement initializes successfully.
		std::shared_ptr<FBytecodeModule> PreviousHotReloadModule;
		std::shared_ptr<const FModuleEnvironment> PreviousHotReloadEnvironment;
		std::uint64_t PreviousHotReloadProgramGeneration = 1;
		std::string PreviousHotReloadFilePath;
		std::string PreviousReloadSourcePath;
		std::optional<EPackageKind> PreviousHotReloadPackageKind;
		std::unique_ptr<FHotReloadState> HotReloadState;
		std::unique_ptr<FHotReloadRollback> PendingHotReloadRollback;
		std::vector<FValueRoot> PendingMigratableRoots;
		FReloadStatus ReloadStatus;
		std::uint64_t ReloadOperationId = 0;
		std::uint64_t CommittedProgramGeneration = 1;

		// Package reloads require first-initialization rollback even when persistent
		// source hot reload is disabled. This state is discarded after settlement.
		bool bTemporaryHotReloadState = false;
		std::string FilePath;
		std::optional<EPackageKind> LoadedPackageKind;

		// Empty for source strings and deployed packages; Reload() only uses this path.
		std::string ReloadSourcePath;
	};
}
