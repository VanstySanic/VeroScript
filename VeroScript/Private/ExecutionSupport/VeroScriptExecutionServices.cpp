// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptExecutionServices.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptVmInternal.h"

namespace VeroScript
{
	class FVmExecutionServices final : public IVmExecutionServices
	{
	public:
		explicit FVmExecutionServices(VeroScriptVm& InVm) : Vm(InVm)
		{
		}
		void EndFunctionDiagnostic(const std::uint64_t Token) noexcept override
		{
			Vm.EndFunctionDiagnostic(Token);
		}
		template <typename TAction>
		static decltype(auto) Invoke(TAction&& Action)
		{
			try
			{
				return Action();
			}
			catch (const FVmServiceException&)
			{
				throw;
			}
			catch (...)
			{
				throw FVmServiceException(std::current_exception());
			}
		}
		FVmExecutionView View()
		{
			return FVmExecutionView(static_cast<FExecutionContext&>(Vm), &PollCore, &Vm);
		}
		static bool PollCore(void* Owner)
		{
			return Invoke([Owner]()
			{
				return static_cast<VeroScriptVm*>(Owner)->CheckExecutionGovernance();
			});
		}
		bool CheckExecutionStep(const FBytecodeFunction* Function, std::size_t Offset) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CheckExecutionStep(Function, Offset);
			});
		}
		bool PollGovernance() override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CheckExecutionGovernance();
			});
		}
		bool ExecutionInterrupted(EExecutionInterruption Reason) override
		{
			return Invoke([&]()
			{
				return Vm.ExecutionInterrupted(Reason);
			});
		}
		bool CheckCleanupInstruction(bool Pause) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Pause ? Vm.CheckExecutionCleanup() : Vm.ChargeExecutionCleanup(1);
			});
		}
		void InstructionLimitExceeded() override
		{
			return Invoke([&]() -> decltype(auto)
			{
				Vm.ReportErrorId("diag.runtime_execution_instruction_limit_exceeded");
				Vm.BeginExecutionCleanup();
			});
		}
		void NotifyContainerMutation(const FValue& Container, EMutationKind Kind) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				Vm.NotifyContainerMutation(Container, Kind);
			});
		}
		FFunctionHandle CreateFunctionValue() override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return std::make_shared<FFunctionValue>();
			});
		}
		void SetObjectHostBase(FScriptObject& Object, FHostObjectHandle Value) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				Object.SetHostBase(std::move(Value));
			});
		}
		void ClearObjectCopyBases(FScriptObject& Object) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				Object.ClearCompletedCopyBases();
			});
		}
		FValue* FindHostMapValue(FMap& Map, const FHostObjectHandle& Key) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return FindHostValue(Map, Key);
			});
		}
		FValue* FindHostMapValue(FOrderedMap& Map, const FHostObjectHandle& Key) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return FindHostValue(Map, Key);
			});
		}
		bool AssignHostMapValue(FMap& Map, const FHostObjectHandle& Key, const FValue& Value, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return AssignHostValue(Map, Key, Value, Location);
			});
		}
		bool AssignHostMapValue(FOrderedMap& Map, const FHostObjectHandle& Key, const FValue& Value, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return AssignHostValue(Map, Key, Value, Location, &Map.HostKeyOrder);
			});
		}
		bool CallFunctionAt(std::uint16_t FunctionIndex, const std::vector<FValue>& Arguments, FObjectHandle ThisObject, bool bAllowJit) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CallFunctionAt(std::move(FunctionIndex), Arguments, std::move(ThisObject), std::move(bAllowJit));
			});
		}
		bool CallFunctionAt(std::uint16_t FunctionIndex, const FValue* Arguments, std::uint8_t ArgumentCount, FObjectHandle ThisObject, bool bAllowJit) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CallFunctionAt(std::move(FunctionIndex), Arguments, std::move(ArgumentCount), std::move(ThisObject), std::move(bAllowJit));
			});
		}
		bool PopOperandArguments(std::uint8_t ArgumentCount, FValue* StackBuffer8, std::vector<FValue>& HeapBuffer, FValue*& OutArguments) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.PopOperandArguments(std::move(ArgumentCount), StackBuffer8, HeapBuffer, OutArguments);
			});
		}
		bool EvalArithmetic(EOpcode Op, const FValue& Left, const FValue& Right, FValue& OutResult, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.EvalArithmetic(std::move(Op), Left, Right, OutResult, Location);
			});
		}
		bool CallBuiltin(EBuiltinId Builtin, std::uint8_t ArgumentCount) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CallBuiltin(std::move(Builtin), std::move(ArgumentCount));
			});
		}
		bool TryCallScalarExpression(std::uint16_t FunctionIndex, const FValue* Arguments, std::uint8_t ArgumentCount, const FObjectHandle& Receiver, const std::vector<FValue>* Captures, FValue& OutResult) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.TryCallScalarExpression(std::move(FunctionIndex), Arguments, std::move(ArgumentCount), Receiver, Captures, OutResult);
			});
		}
		bool TryCallStandardValueLeaf(std::uint16_t StandardIndex, std::uint8_t ArgumentCount, const FSourceLocation& Location, bool& Handled) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.TryCallStandardValueLeaf(std::move(StandardIndex), std::move(ArgumentCount), Location, Handled);
			});
		}
		bool CallNative(std::uint16_t NativeIndex, std::uint8_t ArgumentCount, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CallNative(std::move(NativeIndex), std::move(ArgumentCount), Location);
			});
		}
		bool ExecuteCallMethod(std::uint16_t MethodNameIndex, const FValue& ThisValue, const FValue* Arguments, std::uint8_t ArgumentCount, std::uint32_t CallSiteOffset, const FSourceLocation& SourceLocation, bool bRunNestedToCompletion) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.ExecuteCallMethod(std::move(MethodNameIndex), ThisValue, Arguments, std::move(ArgumentCount), std::move(CallSiteOffset), SourceLocation, std::move(bRunNestedToCompletion));
			});
		}
		bool InvokeResolvedInstanceMethod(std::uint16_t FunctionIndex, const FObjectHandle& ThisObject, const FValue* Arguments, std::uint8_t ArgumentCount, const FSourceLocation& SourceLocation, bool bRunNestedToCompletion, bool bAllowJit) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.InvokeResolvedInstanceMethod(std::move(FunctionIndex), ThisObject, Arguments, std::move(ArgumentCount), SourceLocation, std::move(bRunNestedToCompletion), std::move(bAllowJit));
			});
		}
		bool ReleasePoppedArguments(FValue* Arguments, std::uint8_t ArgumentCount, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.ReleasePoppedArguments(Arguments, std::move(ArgumentCount), Location);
			});
		}
		std::uint16_t FindTypeIndex(const std::string& TypeName) const override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.FindTypeIndex(TypeName);
			});
		}
		std::uint16_t FindActiveNativeIndexByName(const std::string& Name) const override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.FindActiveNativeIndexByName(Name);
			});
		}
		std::uint16_t ResolveObjectTypeIndex(const FScriptObject& Object) const override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.ResolveObjectTypeIndex(Object);
			});
		}
		std::uint16_t FindJitFieldIndex(std::uint16_t TypeIndex, std::uint16_t NameIndex) const override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.FindJitFieldIndex(std::move(TypeIndex), std::move(NameIndex));
			});
		}
		void CompleteObjectConstruction(const FObjectHandle& Object, std::uint16_t ConstructedType) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				Vm.CompleteObjectConstruction(Object, std::move(ConstructedType));
			});
		}
		bool DestroyAbortedConstruction(const FCallFrame& Frame, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.DestroyAbortedConstruction(Frame, Location);
			});
		}
		FObjectHandle CreateObject(std::uint16_t TypeIndex, const FSourceLocation& Location, bool bUninitialized) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CreateObject(std::move(TypeIndex), Location, std::move(bUninitialized));
			});
		}
		bool CopyConstructFields(const FObjectHandle& Target, const FObjectHandle& Source, std::uint16_t TypeIndex, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CopyConstructFields(Target, Source, std::move(TypeIndex), Location);
			});
		}
		bool CopyLanguageValue(const FValue& Value, FValue& Out, const FSourceLocation& Location, bool bAllowExplicit, std::string_view DeclaredType) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CopyLanguageValue(Value, Out, Location, std::move(bAllowExplicit), std::move(DeclaredType));
			});
		}
		bool DestroyObject(const FObjectHandle& Object, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.DestroyObject(Object, Location);
			});
		}
		bool ReleaseValue(FValue& Value, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.ReleaseValue(Value, Location);
			});
		}
		bool ReleaseValueList(FValue* Values, std::size_t Count, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.ReleaseValueList(Values, std::move(Count), Location);
			});
		}
		bool ReleaseValueList(std::vector<FValue>& Values, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.ReleaseValueList(Values, Location);
			});
		}
		bool ReleaseValueList(std::deque<FValue>& Values, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.ReleaseValueList(Values, Location);
			});
		}
		bool DiscardExecutionAbove(std::size_t FrameDepth, std::size_t StackDepth, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.DiscardExecutionAbove(std::move(FrameDepth), std::move(StackDepth), Location);
			});
		}
		bool AssignValue(FValue& Slot, FValue NewValue, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.AssignValue(Slot, std::move(NewValue), Location);
			});
		}
		FNativeArray CreateNativeArray(const std::string& TypeName, const FSourceLocation& Location, bool bUninitialized) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CreateNativeArray(TypeName, Location, std::move(bUninitialized));
			});
		}
		FDynamicArrayHandle CreateDynamicArray(const std::string& TypeName, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CreateDynamicArray(TypeName, Location);
			});
		}
		FMapHandle CreateMap(const std::string& TypeName, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CreateMap(TypeName, Location);
			});
		}
		FSetHandle CreateSet(const std::string& TypeName, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CreateSet(TypeName, Location);
			});
		}
		FQueueHandle CreateQueue(const std::string& TypeName, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CreateQueue(TypeName, Location);
			});
		}
		FStackHandle CreateStack(const std::string& TypeName, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CreateStack(TypeName, Location);
			});
		}
		FChannelHandle CreateChannel(const std::string& TypeName, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CreateChannel(TypeName, Location);
			});
		}
		FOrderedMapHandle CreateOrderedMap(const std::string& TypeName, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CreateOrderedMap(TypeName, Location);
			});
		}
		FDequeHandle CreateDeque(const std::string& TypeName, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CreateDeque(TypeName, Location);
			});
		}
		bool ValueMatchesElementType(const FValue& Value, const std::string& ElementTypeName, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.ValueMatchesElementType(Value, ElementTypeName, Location);
			});
		}
		bool CheckedCastValue(FValue& Value, const std::string& TargetTypeName, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CheckedCastValue(Value, TargetTypeName, Location);
			});
		}
		bool TryCheckedCastValue(FValue& Value, const std::string& TargetTypeName) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.TryCheckedCastValue(Value, TargetTypeName);
			});
		}
		bool BoxVariantValue(const FValue& Value, const std::string& TypeName, FValue& Output) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.BoxVariantValue(Value, TypeName, Output);
			});
		}
		bool ValidateVariantValue(const FValue& Value) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.ValidateVariantValue(Value);
			});
		}
		bool ExtractVariantValue(const FValue& Source, const std::string& Target, FValue& Output) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.ExtractVariantValue(Source, Target, Output);
			});
		}
		bool GetField(FScriptObject& Object, std::uint16_t FieldIndex, FValue& OutValue, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.GetField(Object, std::move(FieldIndex), OutValue, Location);
			});
		}
		bool SetField(FScriptObject& Object, std::uint16_t FieldIndex, const FValue& Value, const FSourceLocation& Location, bool bNotifyMutation) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.SetField(Object, std::move(FieldIndex), Value, Location, std::move(bNotifyMutation));
			});
		}
		void NotifyFieldMutation(FScriptObject& Object, std::uint16_t FieldIndex, EMutationKind Kind, std::size_t Depth) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				Vm.NotifyFieldMutation(Object, std::move(FieldIndex), std::move(Kind), std::move(Depth));
			});
		}
		const void* ContainerStorageIdentity(const FValue& Container) const override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.ContainerStorageIdentity(Container);
			});
		}
		bool CheckContainerCopyMutation(const FValue& Container, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CheckContainerCopyMutation(Container, Location);
			});
		}
		FValue* ResolveRefStorage(FValue& Value, bool bUnwrapClosureCell, std::size_t Depth) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.ResolveRefStorage(Value, std::move(bUnwrapClosureCell), std::move(Depth));
			});
		}
		void NotifyReferenceMutation(FValue& Binding) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				Vm.NotifyReferenceMutation(Binding);
			});
		}
		bool PrepareGlobalInitialization() override
		{
			return Invoke([&]()
			{
				return Vm.PrepareGlobalInitialization();
			});
		}
		bool BindElementReference(FValue& Binding, const FSourceLocation& Location) override
		{
			return Invoke([&]()
			{
				return Vm.BindElementReference(Binding, Location);
			});
		}
		bool PromoteReferenceCapture(FValue Binding, FValue& OutCell, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.PromoteReferenceCapture(std::move(Binding), OutCell, Location);
			});
		}
		bool CreateClosure(std::uint16_t FunctionIndex, std::vector<FValue> Captures,
		    const FObjectHandle& Receiver, FFunctionHandle& Out, const FSourceLocation& Location) override
		{
			return Invoke([&]()
			{
				return Vm.CreateClosure(FunctionIndex, std::move(Captures), Receiver, Out, Location);
			});
		}

		bool CheckLocalStaticAccess(std::uint16_t Slot, const FSourceLocation& Location) override
		{
			return Invoke([&]()
			{
				return Vm.CheckLocalStaticAccess(Slot, Location);
			});
		}
		bool BeginLocalStatic(std::uint16_t Slot, bool& bSkip, const FSourceLocation& Location) override
		{
			return Invoke([&]()
			{
				return Vm.BeginLocalStatic(Slot, bSkip, Location);
			});
		}
		bool CommitLocalStatic(std::uint16_t Slot, FValue Value, const FSourceLocation& Location) override
		{
			return Invoke([&]()
			{
				return Vm.CommitLocalStatic(Slot, std::move(Value), Location);
			});
		}
		bool ExpectBool(const FValue& Value, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.ExpectBool(Value, Location);
			});
		}
		bool TryJump(FCallFrame& Frame, std::size_t CodeSize, std::int32_t Offset, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.TryJump(Frame, std::move(CodeSize), std::move(Offset), Location);
			});
		}
		bool TryTransferToOffset(std::size_t FrameIndex, std::size_t TargetOffset, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.TryTransferToOffset(std::move(FrameIndex), std::move(TargetOffset), Location);
			});
		}
		bool BeginReturnOrComplete(std::size_t FrameIndex, FValue ReturnValue, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.BeginReturnOrComplete(std::move(FrameIndex), std::move(ReturnValue), Location);
			});
		}
		bool TryHandleCurrentFrameScriptException(FValue& Value, const FSourceLocation& Location, bool& OutHandled) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.TryHandleCurrentFrameScriptException(Value, Location, OutHandled);
			});
		}
		bool RaiseScriptException(FValue Value, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.RaiseScriptException(std::move(Value), Location);
			});
		}
		bool ReleaseFinallyContinuations(FCallFrame& Frame, const FSourceLocation& Location, std::size_t TargetOffset) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.ReleaseFinallyContinuations(Frame, Location, std::move(TargetOffset));
			});
		}
		void ReportError(std::string Message, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				Vm.ReportError(std::move(Message), Location);
			});
		}
		void ReportErrorId(std::string_view MessageId, const FSourceLocation& Location, std::initializer_list<FDiagnosticArg> Args) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				Vm.ReportErrorId(std::move(MessageId), Location, std::move(Args));
			});
		}
		void StampHostValueCallbacks(const FHostObjectHandle& Handle) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				Vm.StampHostValueCallbacks(Handle);
			});
		}
		bool PrepareHostKeyForInsert(const FHostObjectHandle& Handle, FHostObjectHandle& OutKey, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.PrepareHostKeyForInsert(Handle, OutKey, Location);
			});
		}
		bool CloneValueOrReport(const FValue& Value, FValue& Out, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.CloneValueOrReport(Value, Out, Location);
			});
		}
		bool AssignClonedValue(FValue& Dest, const FValue& Source, const FSourceLocation& Location) override
		{
			return Invoke([&]() -> decltype(auto)
			{
				return Vm.AssignClonedValue(Dest, Source, Location);
			});
		}
		void DebugBeforeInstruction(const FBytecodeFunction* Function, const FSourceLocation& Location, std::uint32_t& LastLine, std::string& LastFile) override
		{
			return Invoke([&]()
			{
				if (Vm.Debugger && Vm.ActiveModule)
				{
					FSourceLocation StopLocation;
					EDebugStopReason StopReason = EDebugStopReason::Breakpoint;
					if (Vm.Debugger->ShouldStopBeforeInstruction(
					        *Vm.ActiveModule,
					        Function,
					        Vm.Frames.back().InstructionPointer,
					        Vm.Frames.size(),
					        StopLocation,
					        StopReason))
					{
						const bool bSameBreakpointLine =
						    StopReason == EDebugStopReason::Breakpoint && StopLocation.Line == LastLine && std::string(StopLocation.FileLabel) == LastFile;
						if (!bSameBreakpointLine)
						{
							if (StopReason == EDebugStopReason::Breakpoint)
							{
								LastLine = StopLocation.Line;
								LastFile = std::string(StopLocation.FileLabel);
							}
							Vm.Debugger->AttachVm(&Vm);
							Vm.Debugger->NotifyStopped(StopReason);
						}
					}
					else
					{
						// Only clear the latch when we reach a *mapped* instruction on another
						// source line. Unmapped opcodes fall back to the function location and
						// must not reset LastBreakpoint*, or one source line with several IPs
						// would re-fire the same breakpoint 2-3 times.
						const std::vector<FDebugLine>& MappedLines = Function
						    ? Function->DebugLines
						    : Vm.ActiveModule->InitDebugLines;
						if (Vm.Frames.back().InstructionPointer < MappedLines.size() && MappedLines[Vm.Frames.back().InstructionPointer].Line != 0)
						{
							const FDebugLine& Mapped = MappedLines[Vm.Frames.back().InstructionPointer];
							std::string MappedFile = std::string(Location.FileLabel);
							if (Mapped.FileIndex < Vm.ActiveModule->DebugFiles.size() && !Vm.ActiveModule->DebugFiles[Mapped.FileIndex].empty())
							{
								MappedFile = Vm.ActiveModule->DebugFiles[Mapped.FileIndex];
							}
							if (Mapped.Line != LastLine || MappedFile != LastFile)
							{
								LastLine = 0;
								LastFile.clear();
							}
						}
					}
				}
			});
		}

	private:
		template <typename TMap>
		static FValue* FindHostValue(TMap& Map, const FHostObjectHandle& Key)
		{
			const auto It = Map.HostEntries.find(Key);
			return It == Map.HostEntries.end() ? nullptr : &It->second;
		}
		template <typename TMap>
		bool AssignHostValue(TMap& Map, const FHostObjectHandle& Key, const FValue& Value,
		    const FSourceLocation& Location, std::vector<FHostObjectHandle>* Order = nullptr)
		{
			Vm.StampHostValueCallbacks(Key);
			if (auto* Existing = FindHostValue(Map, Key)) return Vm.AssignClonedValue(*Existing, Value, Location);
			FHostObjectHandle StoredKey;
			if (!Vm.PrepareHostKeyForInsert(Key, StoredKey, Location)) return false;
			return VmDetail::AssignMapEntry(Map.HostEntries, StoredKey, [&](FValue& Destination)
			{
				return Vm.AssignClonedValue(Destination, Value, Location);
			}, Order);
		}
		VeroScriptVm& Vm;
	};
	template <auto Function, typename TSignature = decltype(Function)>
	struct TGuardedVmHostFunction;
	template <typename R, typename... A, R (*Function)(A...)>
	struct TGuardedVmHostFunction<Function, R (*)(A...)>
	{
		static R Call(A... Arguments)
		{
			return FVmExecutionServices::Invoke([&]() -> R
			{
				return Function(std::forward<A>(Arguments)...);
			});
		}
	};
	template <typename R, typename... A, R (*Function)(A...) noexcept>
	struct TGuardedVmHostFunction<Function, R (*)(A...) noexcept>
	{
		static R Call(A... Arguments) noexcept
		{
			return Function(std::forward<A>(Arguments)...);
		}
	};
	const FVmHostServices& GetVmHostServices() noexcept
	{
		static const int Owner = 0;
		static const FVmHostServices Services{&Owner, &TGuardedVmHostFunction<&Variant::IsVariant>::Call, &TGuardedVmHostFunction<&Variant::HasValue>::Call, &TGuardedVmHostFunction<&Variant::Is>::Call,
		    []()
		{
			return FVmExecutionServices::Invoke([]()
			{
				return StandardLibrary::GetBindingCatalog().GetNativeFunctions().size();
			});
		},
		    &TGuardedVmHostFunction<&Runtime::ConstantToValue>::Call, &TGuardedVmHostFunction<&Runtime::ToDouble>::Call, &TGuardedVmHostFunction<&Runtime::ToSignedInteger64>::Call, &TGuardedVmHostFunction<&Runtime::ToUnsignedInteger64>::Call, &TGuardedVmHostFunction<&Runtime::AddIntegerImmediate>::Call, &TGuardedVmHostFunction<&Runtime::TryAppendStringValue>::Call, &TGuardedVmHostFunction<&Runtime::TryGetSizeValue>::Call, &TGuardedVmHostFunction<&Runtime::ValuesEqual>::Call, &TGuardedVmHostFunction<&ModuleMeta::TypeSimpleName>::Call, &TGuardedVmHostFunction<&ModuleMeta::FindFieldIndex>::Call};
		return Services;
	}

	bool VeroScriptVm::RunSelectedVm(std::uint64_t CallToken, bool bContinuation)
	{
		FVmExecutionServices Services(*this);
		auto View = Services.View();
		return ExecuteVmBackend(Services, View, CallToken, bContinuation,
		    Debugger != nullptr || bHotReloadModuleInitializing || JitPolicy.Diagnostics.MaxFunctions != 0,
		    HasExecutionGovernance());
	}
}
