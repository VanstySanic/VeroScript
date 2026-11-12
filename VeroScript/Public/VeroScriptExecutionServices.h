// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptExecutionContext.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptVariant.h"
#include <exception>

namespace VeroScript
{
	/** Transports an exception from an owning Core service through a backend.
	 * Only Core constructs this marker; the original outer execution guard handles it.
	 */
	class FVmServiceException final
	{
		friend class FVmExecutionServices;

	public:
		const std::exception_ptr& GetException() const noexcept
		{
			return Exception;
		}

	private:
		explicit FVmServiceException(std::exception_ptr Value) noexcept : Exception(std::move(Value))
		{
		}
		std::exception_ptr Exception;
	};
	using FVmCallFrame = FExecutionContext::FCallFrame;
	/** Borrowed canonical state. Valid only during a synchronous backend entry.
	 * Frame/operand elements must be reacquired after calls, callbacks and release.
	 * Containers themselves belong to Core and remain stable for that entry.
	 * Inline accessors share one context reference for the dispatch hot path.
	 */
	struct FVmExecutionView
	{
		friend class FVmExecutionServices;

	public:
		const std::size_t& GetStopFrameDepth() const noexcept
		{
			return Context.StopFrameDepth;
		}
		const FBytecodeModule* const& GetActiveModule() const noexcept
		{
			return Context.ActiveModule;
		}
		const std::vector<FNativeFunction>* const& GetActiveNatives() const noexcept
		{
			return Context.ActiveNatives;
		}
		const FInstanceCallInterceptor& GetInstanceCallInterceptor() const noexcept
		{
			return Context.InstanceCallInterceptor;
		}
		const std::uint64_t& GetProgramGeneration() const noexcept
		{
			return Context.ProgramGeneration;
		}
		const std::shared_ptr<const void>& GetProgramOwner() const noexcept
		{
			return Context.ProgramOwner;
		}
		const std::shared_ptr<const FBytecodeModule>& GetImmutableProgramOwner() const noexcept
		{
			return Context.JitProgramOwner;
		}
		VeroScriptDebugger* const& GetDebugger() const noexcept
		{
			return Context.Debugger;
		}
		std::vector<FValue>& GetOperandStack() const noexcept
		{
			return Context.OperandStack;
		}
		std::vector<FVmCallFrame>& GetFrames() const noexcept
		{
			return Context.Frames;
		}
		std::vector<FVmCallFrame>& GetFreeFrames() const noexcept
		{
			return Context.FreeFrames;
		}
		std::vector<FValue>& GetGlobals() const noexcept
		{
			return Context.Globals;
		}
		std::vector<bool>& GetHotReloadRetainedGlobalSlots() const noexcept
		{
			return Context.HotReloadRetainedGlobalSlots;
		}
		std::vector<std::uint8_t>& GetHotReloadSkipInitByte() const noexcept
		{
			return Context.HotReloadSkipInitByte;
		}
		bool HasDemandGlobalInitialization() const noexcept
		{
			return Context.bDemandGlobalInitializing;
		}
		bool& GetHotReloadInitializingFlag() const noexcept
		{
			return Context.bHotReloadModuleInitializing;
		}
		bool& GetTeardownErrorActiveFlag() const noexcept
		{
			return Context.bTeardownErrorActive;
		}
		std::size_t& GetActiveInstructionStart() const noexcept
		{
			return Context.ActiveInstructionStart;
		}
		const FBytecodeFunction*& GetTeardownErrorFunction() const noexcept
		{
			return Context.TeardownErrorFunction;
		}
		std::size_t& GetTeardownErrorIp() const noexcept
		{
			return Context.TeardownErrorIp;
		}
		std::size_t& GetTeardownFrameDepth() const noexcept
		{
			return Context.TeardownFrameDepth;
		}
		bool& GetErrorFlag() const noexcept
		{
			return Context.bHasError;
		}
		bool& GetPendingExceptionFlag() const noexcept
		{
			return Context.bHasPendingScriptException;
		}
		bool IsCopyingLanguageValue() const noexcept
		{
			return Context.LanguageCopyDepth != 0;
		}
		std::vector<const void*>& GetActiveContainerCopyReads() const noexcept
		{
			return Context.ActiveContainerCopyReads;
		}
		bool& GetExitRequestedFlag() const noexcept
		{
			return Context.bExitRequested;
		}
		FScriptError& GetError() const noexcept
		{
			return Context.Error;
		}
		const FVmObjectLifetimeScope*& GetObjectLifetimeScope() const noexcept
		{
			return Context.ObjectLifetimeScope;
		}
		std::size_t& GetActiveBackendFrameCredits() const noexcept
		{
			return Context.ActiveBackendFrameCredits;
		}
		bool CanEnterAdditionalCall() const noexcept
		{
			// A materialized backend frame and its native invocation are one call.
			return Context.Frames.size() + Context.ActiveJitCallDepth -
			    Context.ActiveBackendFrameCredits < Context.CallDepthLimit;
		}
		FExecutionGovernanceState& GetExecutionGovernance() const noexcept
		{
			return Context.ExecutionGovernance;
		}
		const FExecutionLimits& GetExecutionLimits() const noexcept
		{
			return Context.ExecutionLimits;
		}
		const std::shared_ptr<FExecutionCleanupState>& GetExecutionCleanup() const noexcept
		{
			return Context.ExecutionCleanup;
		}
		const std::shared_ptr<FExecutionControl>& GetExecutionControl() const noexcept
		{
			return Context.ExecutionControl;
		}
		const std::chrono::steady_clock::time_point& GetExternalExecutionDeadline() const noexcept
		{
			return Context.ExternalExecutionDeadline;
		}
		const bool& GetResetRequiredFlag() const noexcept
		{
			return Context.bExecutionResetRequired;
		}
		bool HasExternalExecutionCheck() const noexcept
		{
			return bHasExternalCheck;
		}
		bool HasExecutionObservers() const noexcept
		{
			return Context.bMutationObserverPresent || Context.bInstanceCallInterceptorPresent;
		}
		// Synchronous Core-owned poll. Its owner and exception handling stay in
		// Core; the borrowed entry removes a virtual forwarding hop in hot loops.
		bool PollGovernance() const
		{
			return GovernancePoll(GovernanceOwner);
		}

	private:
		explicit FVmExecutionView(FExecutionContext& InContext, bool (*InPoll)(void*), void* InOwner) noexcept
		    : Context(InContext), bHasExternalCheck(static_cast<bool>(InContext.ExternalExecutionCheck)),
		      GovernancePoll(InPoll), GovernanceOwner(InOwner)
		{
		}
		FExecutionContext& Context;
		const bool bHasExternalCheck;
		bool (*const GovernancePoll)(void*);
		void* const GovernanceOwner;
	};

	/** Language operations retain their Core owner. No facade is exposed. */
	class IVmExecutionServices
	{
	public:
		using FCallFrame = FVmCallFrame;
		virtual ~IVmExecutionServices() = default;
		virtual bool CheckExecutionStep(const FBytecodeFunction* Function, std::size_t Offset) = 0;
		virtual bool PollGovernance() = 0;
		virtual bool ExecutionInterrupted(EExecutionInterruption Reason) = 0;
		virtual bool CheckCleanupInstruction(bool Pause) = 0;
		virtual void InstructionLimitExceeded() = 0;
		virtual bool CallFunctionAt(
		    std::uint16_t FunctionIndex,
		    const std::vector<FValue>& Arguments,
		    FObjectHandle ThisObject,
		    bool bAllowJit = true) = 0;
		virtual bool CallFunctionAt(
		    std::uint16_t FunctionIndex,
		    const FValue* Arguments,
		    std::uint8_t ArgumentCount,
		    FObjectHandle ThisObject,
		    bool bAllowJit = true) = 0;
		virtual bool PopOperandArguments(
		    std::uint8_t ArgumentCount,
		    FValue* StackBuffer8,
		    std::vector<FValue>& HeapBuffer,
		    FValue*& OutArguments) = 0;
		virtual bool EvalArithmetic(
		    EOpcode Op,
		    const FValue& Left,
		    const FValue& Right,
		    FValue& OutResult,
		    const FSourceLocation& Location) = 0;
		virtual bool CallBuiltin(EBuiltinId Builtin, std::uint8_t ArgumentCount) = 0;
		virtual bool TryCallScalarExpression(std::uint16_t FunctionIndex, const FValue* Arguments,
		    std::uint8_t ArgumentCount, const FObjectHandle& Receiver,
		    const std::vector<FValue>* Captures, FValue& OutResult) = 0;
		virtual bool TryCallStandardValueLeaf(std::uint16_t StandardIndex, std::uint8_t ArgumentCount,
		    const FSourceLocation& Location, bool& Handled) = 0;
		virtual bool CallNative(
		    std::uint16_t NativeIndex,
		    std::uint8_t ArgumentCount,
		    const FSourceLocation& Location) = 0;
		virtual bool ExecuteCallMethod(
		    std::uint16_t MethodNameIndex,
		    const FValue& ThisValue,
		    const FValue* Arguments,
		    std::uint8_t ArgumentCount,
		    std::uint32_t CallSiteOffset,
		    const FSourceLocation& SourceLocation,
		    bool bRunNestedToCompletion) = 0;
		virtual bool InvokeResolvedInstanceMethod(
		    std::uint16_t FunctionIndex,
		    const FObjectHandle& ThisObject,
		    const FValue* Arguments,
		    std::uint8_t ArgumentCount,
		    const FSourceLocation& SourceLocation,
		    bool bRunNestedToCompletion,
		    bool bAllowJit = true) = 0;
		virtual bool ReleasePoppedArguments(
		    FValue* Arguments,
		    std::uint8_t ArgumentCount,
		    const FSourceLocation& Location) = 0;
		virtual std::uint16_t FindTypeIndex(const std::string& TypeName) const = 0;
		virtual std::uint16_t FindActiveNativeIndexByName(const std::string& Name) const = 0;
		virtual std::uint16_t ResolveObjectTypeIndex(const FScriptObject& Object) const = 0;
		virtual std::uint16_t FindJitFieldIndex(std::uint16_t TypeIndex, std::uint16_t NameIndex) const = 0;
		virtual void CompleteObjectConstruction(const FObjectHandle& Object, std::uint16_t ConstructedType) = 0;
		virtual bool DestroyAbortedConstruction(const FCallFrame& Frame, const FSourceLocation& Location) = 0;
		virtual FObjectHandle CreateObject(std::uint16_t TypeIndex, const FSourceLocation& Location,
		    bool bUninitialized = false) = 0;
		virtual bool CopyConstructFields(const FObjectHandle& Target, const FObjectHandle& Source,
		    std::uint16_t TypeIndex, const FSourceLocation& Location) = 0;
		virtual bool CopyLanguageValue(const FValue& Value, FValue& Out,
		    const FSourceLocation& Location, bool bAllowExplicit = false, std::string_view DeclaredType = {}) = 0;
		virtual bool DestroyObject(const FObjectHandle& Object, const FSourceLocation& Location) = 0;
		virtual bool ReleaseValue(FValue& Value, const FSourceLocation& Location) = 0;
		virtual bool ReleaseValueList(FValue* Values, std::size_t Count, const FSourceLocation& Location) = 0;
		virtual bool ReleaseValueList(std::vector<FValue>& Values, const FSourceLocation& Location) = 0;
		virtual bool ReleaseValueList(std::deque<FValue>& Values, const FSourceLocation& Location) = 0;
		virtual bool DiscardExecutionAbove(
		    std::size_t FrameDepth,
		    std::size_t StackDepth,
		    const FSourceLocation& Location) = 0;
		virtual bool AssignValue(FValue& Slot, FValue NewValue, const FSourceLocation& Location) = 0;
		virtual FNativeArray CreateNativeArray(const std::string& TypeName, const FSourceLocation& Location,
		    bool bUninitialized = false) = 0;
		virtual FDynamicArrayHandle CreateDynamicArray(const std::string& TypeName, const FSourceLocation& Location) = 0;
		virtual FMapHandle CreateMap(const std::string& TypeName, const FSourceLocation& Location) = 0;
		virtual FSetHandle CreateSet(const std::string& TypeName, const FSourceLocation& Location) = 0;
		virtual FQueueHandle CreateQueue(const std::string& TypeName, const FSourceLocation& Location) = 0;
		virtual FStackHandle CreateStack(const std::string& TypeName, const FSourceLocation& Location) = 0;
		virtual FChannelHandle CreateChannel(const std::string& TypeName, const FSourceLocation& Location) = 0;
		virtual FOrderedMapHandle CreateOrderedMap(const std::string& TypeName, const FSourceLocation& Location) = 0;
		virtual FDequeHandle CreateDeque(const std::string& TypeName, const FSourceLocation& Location) = 0;
		virtual bool ValueMatchesElementType(
		    const FValue& Value,
		    const std::string& ElementTypeName,
		    const FSourceLocation& Location) = 0;
		virtual bool CheckedCastValue(
		    FValue& Value,
		    const std::string& TargetTypeName,
		    const FSourceLocation& Location) = 0;
		virtual bool TryCheckedCastValue(FValue& Value, const std::string& TargetTypeName) = 0;
		virtual bool BoxVariantValue(const FValue& Value, const std::string& TypeName, FValue& Output) = 0;
		virtual bool ValidateVariantValue(const FValue& Value) = 0;
		virtual bool ExtractVariantValue(const FValue& Source, const std::string& Target, FValue& Output) = 0;
		virtual bool GetField(FScriptObject& Object, std::uint16_t FieldIndex, FValue& OutValue, const FSourceLocation& Location) = 0;
		virtual bool SetField(FScriptObject& Object, std::uint16_t FieldIndex, const FValue& Value,
		    const FSourceLocation& Location, bool bNotifyMutation = true) = 0;
		virtual void NotifyFieldMutation(
		    FScriptObject& Object,
		    std::uint16_t FieldIndex,
		    EMutationKind Kind = EMutationKind::FieldWrite,
		    std::size_t Depth = 0) = 0;
		virtual const void* ContainerStorageIdentity(const FValue& Container) const = 0;
		virtual bool CheckContainerCopyMutation(const FValue& Container, const FSourceLocation& Location = {}) = 0;
		virtual FValue* ResolveRefStorage(
		    FValue& Value,
		    bool bUnwrapClosureCell = true,
		    std::size_t Depth = 0) = 0;
		virtual void NotifyReferenceMutation(FValue& Binding) = 0;
		virtual bool BindElementReference(FValue& Binding, const FSourceLocation& Location = {}) = 0;
		virtual bool PromoteReferenceCapture(FValue Binding, FValue& OutCell,
		    const FSourceLocation& Location = {}) = 0;
		virtual bool CreateClosure(std::uint16_t FunctionIndex, std::vector<FValue> Captures,
		    const FObjectHandle& Receiver, FFunctionHandle& Out, const FSourceLocation& Location = {}) = 0;

		virtual bool CheckLocalStaticAccess(std::uint16_t Slot, const FSourceLocation& Location) = 0;
		virtual bool PrepareGlobalInitialization() = 0;
		virtual bool BeginLocalStatic(std::uint16_t Slot, bool& bSkip, const FSourceLocation& Location) = 0;
		virtual bool CommitLocalStatic(std::uint16_t Slot, FValue Value, const FSourceLocation& Location) = 0;
		virtual bool ExpectBool(const FValue& Value, const FSourceLocation& Location) = 0;
		virtual bool TryJump(
		    FCallFrame& Frame,
		    std::size_t CodeSize,
		    std::int32_t Offset,
		    const FSourceLocation& Location) = 0;
		virtual bool TryTransferToOffset(
		    std::size_t FrameIndex,
		    std::size_t TargetOffset,
		    const FSourceLocation& Location) = 0;
		virtual bool BeginReturnOrComplete(
		    std::size_t FrameIndex,
		    FValue ReturnValue,
		    const FSourceLocation& Location) = 0;
		virtual bool TryHandleCurrentFrameScriptException(
		    FValue& Value,
		    const FSourceLocation& Location,
		    bool& OutHandled) = 0;
		virtual bool RaiseScriptException(FValue Value, const FSourceLocation& Location) = 0;
		virtual bool ReleaseFinallyContinuations(
		    FCallFrame& Frame,
		    const FSourceLocation& Location,
		    std::size_t TargetOffset = std::numeric_limits<std::size_t>::max()) = 0;
		virtual void ReportError(std::string Message, const FSourceLocation& Location = {}) = 0;
		virtual void ReportErrorId(
		    std::string_view MessageId,
		    const FSourceLocation& Location = {},
		    std::initializer_list<FDiagnosticArg> Args = {}) = 0;
		virtual void StampHostValueCallbacks(const FHostObjectHandle& Handle) = 0;
		virtual bool PrepareHostKeyForInsert(
		    const FHostObjectHandle& Handle,
		    FHostObjectHandle& OutKey,
		    const FSourceLocation& Location) = 0;
		virtual bool CloneValueOrReport(
		    const FValue& Value,
		    FValue& Out,
		    const FSourceLocation& Location = {}) = 0;
		virtual bool AssignClonedValue(
		    FValue& Dest,
		    const FValue& Source,
		    const FSourceLocation& Location = {}) = 0;
		virtual void DebugBeforeInstruction(const FBytecodeFunction* Function, const FSourceLocation& Location, std::uint32_t& LastLine, std::string& LastFile) = 0;
		virtual void NotifyContainerMutation(const FValue& Container, EMutationKind Kind) = 0;
		virtual FValue* FindHostMapValue(FMap&, const FHostObjectHandle&) = 0;
		virtual FValue* FindHostMapValue(FOrderedMap&, const FHostObjectHandle&) = 0;
		virtual bool AssignHostMapValue(FMap&, const FHostObjectHandle&, const FValue&, const FSourceLocation&) = 0;
		virtual bool AssignHostMapValue(FOrderedMap&, const FHostObjectHandle&, const FValue&, const FSourceLocation&) = 0;
		// Language roots must not retain deleters emitted by an unloadable backend.
		virtual FFunctionHandle CreateFunctionValue() = 0;
		virtual void SetObjectHostBase(FScriptObject& Object, FHostObjectHandle Value) = 0;
		virtual void ClearObjectCopyBases(FScriptObject& Object) = 0;
		// Optional diagnostic completion, after a VM frame's return cleanup.
		// Backends call this only for nonzero Core-assigned frame tokens.
		virtual void EndFunctionDiagnostic(std::uint64_t) noexcept {}
	};

	/** Immutable helpers and owner identity supplied by the actual Core. */
	struct FVmHostServices
	{
		const void* OwnerIdentity = nullptr;
		decltype(&Variant::IsVariant) VariantIsVariant = nullptr;
		decltype(&Variant::HasValue) VariantHasValue = nullptr;
		decltype(&Variant::Is) VariantIs = nullptr;
		std::size_t (*StandardNativeCount)() = nullptr;
		decltype(&Runtime::ConstantToValue) Runtime_ConstantToValue = nullptr;
		decltype(&Runtime::ToDouble) Runtime_ToDouble = nullptr;
		decltype(&Runtime::ToSignedInteger64) Runtime_ToSignedInteger64 = nullptr;
		decltype(&Runtime::ToUnsignedInteger64) Runtime_ToUnsignedInteger64 = nullptr;
		decltype(&Runtime::AddIntegerImmediate) Runtime_AddIntegerImmediate = nullptr;
		decltype(&Runtime::TryAppendStringValue) Runtime_TryAppendStringValue = nullptr;
		decltype(&Runtime::TryGetSizeValue) Runtime_TryGetSizeValue = nullptr;
		decltype(&Runtime::ValuesEqual) Runtime_ValuesEqual = nullptr;
		decltype(&ModuleMeta::TypeSimpleName) ModuleMeta_TypeSimpleName = nullptr;
		decltype(&ModuleMeta::FindFieldIndex) ModuleMeta_FindFieldIndex = nullptr;
	};
	const FVmHostServices& GetVmHostServices() noexcept;
}
