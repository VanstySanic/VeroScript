// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "ExecutionSupport/VeroScriptVmJitDiagnostics.h"
#include "VeroScriptVmScalarArithmetic.h"
#include "VeroScriptVmArguments.h"
#include "VeroScriptJitBackendLoader.h"

#include "VeroScriptRuntime.h"
#include "VeroScriptJitFrameServices.h"
#include "VeroScriptJitValueServices.h"
#include "VeroScriptJitCallServices.h"
#include "VeroScriptCompiledExecution.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptMethodKey.h"

#include <exception>
#include <new>
#include <utility>

namespace VeroScript
{

	// This adapter alone owns access to VM internals. Backend modules receive only
	// IJitRuntimeServices and cannot retain the adapter beyond this invocation.
	class FVmJitRuntimeServices final : public IJitRuntimeServices, public IJitFrameServices, public IJitValueServices, public IJitCallServices, public IJitScalarServices, public IExecutionGovernanceServices, public ICompiledFunctionServices
	{
	public:
		//~ Begin IJitRuntimeServices Interface
		FJitExecutionStatusView BorrowExecutionStatus() const noexcept override
		{
			Vm.bInstanceCallInterceptorPresent = static_cast<bool>(Vm.InstanceCallInterceptor);
			Vm.bMutationObserverPresent = static_cast<bool>(Vm.MutationCallback);
			return {&Vm.bHasError, &Vm.bHasPendingScriptException, &Vm.bExitRequested,
			    &Vm.bInstanceCallInterceptorPresent, &Vm.bMutationObserverPresent};
		}

		bool HasError() const noexcept override
		{
			return Vm.bHasError;
		}

		bool HasPendingControlTransfer() const noexcept override
		{
			return Vm.bHasPendingScriptException || Vm.bExitRequested;
		}

		bool IsExitRequested() const noexcept override
		{
			return Vm.bExitRequested;
		}

		bool CheckExecutionGovernance() noexcept override
		{
			return Guard([&]()
			{
				return !HasError() && !HasPendingControlTransfer() && Vm.CheckExecutionGovernance();
			});
		}

		FExecutionCheck CompilationCheck() noexcept
		{
			return {[](void* Context) noexcept
			{
				return static_cast<FVmJitRuntimeServices*>(Context)->CheckExecutionGovernance();
			}, this};
		}

		IExecutionGovernanceServices* GetExecutionGovernanceServices() noexcept override
		{
			return this;
		}
		bool Poll() noexcept override
		{
			return CheckExecutionGovernance();
		}
		std::uint64_t GetRequiredCapabilities() const noexcept override
		{
			auto Required = (Vm.HasExecutionGovernance() ? ControlledExecutionCapabilities : 0) |
				(Vm.MutationCallback || Vm.InstanceCallInterceptor ? ExecutionObservationCapability : 0);
			// Match the bytecode VM's exact-arithmetic batching admission. A quota
			// or permission restriction alone does not require asynchronous polling;
			// neither removes its instruction, permission or frame obligations.
			if ((Required & ExecutionPollingCapability) != 0 &&
			    !Vm.ExternalExecutionCheck && !Vm.ExecutionControl && !Vm.ExecutionCleanup &&
			    Vm.ExecutionGovernance.Deadline == std::chrono::steady_clock::time_point::max() &&
			    Vm.ExternalExecutionDeadline == std::chrono::steady_clock::time_point::max())
			{
				Required &= ~ExecutionPollingCapability;
			}
			return Required;
		}
		bool CheckStep(const std::size_t Offset) noexcept override
		{
			return Guard([&]()
			{
				return CheckActiveStep(Offset);
			});
		}
		bool ChargeWork(const std::uint64_t Units) noexcept override
		{
			return Guard([&]()
			{
				return !HasError() && !HasPendingControlTransfer() && Vm.ChargeExecutionWork(Units);
			});
		}

		#include "Internal/VeroScriptVmCompiledServices.inl"

		void ReportError(const std::string_view Message) noexcept override
		{
			if (Vm.bHasError)
			{
				return;
			}
			try
			{
				Vm.ReportError(std::string(Message));
			}
			catch (...)
			{
				// Error strings and stack traces may allocate. The failure flag remains
				// authoritative even when no diagnostic storage can be obtained.
				Vm.bHasError = true;
			}
		}

		void RefineErrorLocation(const std::uint16_t FunctionIndex, const std::size_t InstructionOffset,
		    const FDebugLine* Lines, std::size_t LineCount) noexcept override
		{
			(void)Guard([&]()
			{
				Vm.ActiveInstructionStart = InstructionOffset;
				// Script unwinding already records the precise throw site, including
				// replacement exceptions from cleanup. An outer native call opcode
				// must not relocate that provisional diagnostic.
				if (!Vm.bHasError || Vm.bJitErrorLocationPinned || Vm.Error.bUncaughtScriptException)
				{
					return true;
				}

				if (!Lines && Vm.ActiveModule && FunctionIndex < Vm.ActiveModule->Functions.size())
				{
					const auto& Function = Vm.ActiveModule->Functions[FunctionIndex];
					Lines = Function.DebugLines.data();
					LineCount = Function.DebugLines.size();
				}
				if (Lines && LineCount != 0 && Vm.ActiveModule)
				{
					std::size_t Probe = std::min(InstructionOffset, LineCount - 1);
					for (;;)
					{
						const auto& Line = Lines[Probe];
						if (Line.Line != 0)
						{
							auto Location = Vm.Error.Location.AsSourceLocation();
							Location.Line = Line.Line;
							Location.Column = Line.Column;
							if (Line.FileIndex < Vm.ActiveModule->DebugFiles.size() && !Vm.ActiveModule->DebugFiles[Line.FileIndex].empty())
							{
								Location.FileLabel = Vm.ActiveModule->DebugFiles[Line.FileIndex];
							}
							Vm.Error.Location = Location;
							Vm.bJitErrorLocationPinned = true;
							return true;
						}
						if (Probe == 0)
						{
							break;
						}
						--Probe;
					}
				}
				Vm.Error.Location = Vm.RefineErrorLocation(Vm.Error.Location.AsSourceLocation());
				Vm.bJitErrorLocationPinned = true;
				return true;
			});
		}

		void ReportErrorId(const std::string_view Id, const std::string_view Argument) noexcept override
		{
			(void)Guard([&]()
			{
				if (!Vm.bHasError)
				{
					if (Argument.empty())
					{
						Vm.ReportErrorId(std::string(Id));
					}
					else
					{
						Vm.ReportErrorId(std::string(Id), {}, {std::string(Argument)});
					}
				}
				return false;
			});
		}

		bool PrepareFunction(const std::uint16_t FunctionIndex, const bool Force) noexcept override
		{
			return Guard([&]()
			{
				const auto Cache = Vm.BackendCache;
				if (!Cache || !CheckExecutionGovernance()) return false;
				if (Cache->Find(FunctionIndex)) return true;
				if (!Force && Vm.GetEffectiveExecutionBackend() == EExecutionBackend::Mixed && !Cache->ShouldCompileOnCall(FunctionIndex))
				{
					return false;
				}
				if (Cache->EnsureCompiled(FunctionIndex, CompilationCheck(), GetRequiredCapabilities()) == EJitCompilationResult::Failed)
				{
					ReportError(Cache->GetLastError());
					return false;
				}
				return static_cast<bool>(Cache->Find(FunctionIndex));
			});
		}

		bool PrepareReturn(const FBytecodeFunction& Function, FValue& Value) noexcept override
		{
			return Guard([&]()
			{
				FValue* const Storage = Vm.ResolveRefStorage(Value);
				if (!Storage || (Function.bReturnsRef && Storage == &Value))
				{
					ReportError("JIT function returned an invalid reference");
					return false;
				}
				if (!Function.ClosureTypeName.empty())
				{
					std::vector<std::string> Parameters;
					std::string ReturnType;
					if (!Runtime::ParseFunctionType(Function.ClosureTypeName, Parameters, ReturnType) || !Vm.ValueMatchesElementType(*Storage, std::string(Runtime::StripReferenceTypeName(ReturnType)), Function.Location))
					{
						ReportError("JIT function return type mismatch");
						return false;
					}
				}
				if (Function.bReturnsRef)
				{
					FValue Persistent;
					if (!Vm.PromoteReferenceCapture(Value, Persistent, Function.Location))
					{
						return false;
					}
					return Vm.AssignValue(Value, std::move(Persistent), Function.Location);
				}
				if (Storage != &Value)
				{
					FValue Snapshot;
					if (!Vm.CloneValueOrReport(*Storage, Snapshot, Function.Location))
					{
						return false;
					}
					return Vm.AssignValue(Value, std::move(Snapshot), Function.Location);
				}
				// An owned value result already has the same representation produced
				// by the interpreter. Copying it again would duplicate struct results.
				return true;
			});
		}

		bool InvokeFunction(const std::uint16_t FunctionIndex, const FObjectHandle& Receiver,
		    const FValue* const Arguments, const std::uint8_t ArgumentCount, FValue& OutReturn) noexcept override
		{
			const std::size_t FrameDepth = Vm.Frames.size();
			const std::size_t StackDepth = Vm.OperandStack.size();
			const std::size_t SavedStop = Vm.StopFrameDepth;
			std::vector<FValue> CallArguments;
			const bool bOk = Guard([&]()
			{
				const auto CallInstructionStart = Vm.ActiveInstructionStart;
				if (!PrepareArguments(FunctionIndex, Receiver, Arguments, ArgumentCount, CallArguments))
				{
					return false;
				}
				const auto& Target = Vm.ActiveModule->Functions[FunctionIndex];
				Vm.ActiveInstructionStart = CallInstructionStart;
				const bool bCalled = Receiver
				    ? Vm.InvokeResolvedInstanceMethod(FunctionIndex, Receiver, CallArguments.data(), ArgumentCount, Target.Location, false, true)
				    : Vm.CallFunctionAt(FunctionIndex, CallArguments.data(), ArgumentCount, {}, true);
				if (!bCalled)
				{
					if (Vm.bHasError)
					{
						Vm.bJitErrorLocationPinned = true;
					}
					return false;
				}
				if (Vm.Frames.size() > FrameDepth)
				{
					Vm.StopFrameDepth = FrameDepth;
					if (!Vm.Run())
					{
						if (Vm.bHasError)
						{
							Vm.bJitErrorLocationPinned = true;
						}
						return false;
					}
				}
				if (HasError() || HasPendingControlTransfer())
				{
					return false;
				}
				if (Vm.Frames.size() != FrameDepth || Vm.OperandStack.size() != StackDepth + 1)
				{
					ReportError("JIT nested call left unbalanced execution state");
					return false;
				}
				return Vm.AssignValue(OutReturn, Vm.Pop(), {});
			});
			Vm.StopFrameDepth = SavedStop;
			if (!bOk)
			{
				(void)Guard([&]()
				{
					return Vm.DiscardExecutionAbove(FrameDepth, StackDepth, {});
				});
			}
			const bool bReleased = Guard([&]()
			{
				return Vm.ReleaseValueList(CallArguments, {});
			});
			return bOk && bReleased && !HasError() && !HasPendingControlTransfer();
		}

		bool CopyValue(const FValue& Value, FValue& OutCopy) noexcept override
		{
			return Guard([&]()
			{
				FValue Copy;
				return Vm.CloneValueOrReport(Value, Copy, {}) && Vm.AssignValue(OutCopy, std::move(Copy), {});
			});
		}

		bool ReleaseValue(FValue& Value) noexcept override
		{
			return Guard([&]()
			{
				return Vm.AssignValue(Value, FValue{std::int32_t{0}}, {});
			});
		}
		IJitFrameServices* GetFrameServices() noexcept override
		{
			return this;
		}
		IJitValueServices* GetValueServices() noexcept override
		{
			return this;
		}

		IJitCallServices* GetCallServices() noexcept override
		{
			return this;
		}
		IJitScalarServices* GetScalarServices() noexcept override
		{
			return this;
		}
		//~ End IJitRuntimeServices Interface

		//~ Begin IJitFrameServices Interface
		bool Enter(const FJitFrameDescription& Description, const FValue* const Arguments,
		    const std::uint8_t ArgumentCount, const FObjectHandle& Receiver,
		    FJitFrameToken& OutToken) noexcept override
		{
			OutToken = {};
			std::vector<FValue> Prepared;
			const bool bOk = Guard([&]()
			{
				if (FrameToken.Identity != 0 || Description.FunctionIndex != InvocationFunctionIndex || Vm.ActiveJitCallDepth == 0)
				{
					ReportError("JIT frame does not belong to the active invocation");
					return false;
				}
				if (!PrepareArguments(Description.FunctionIndex, Receiver, Arguments, ArgumentCount, Prepared))
				{
					return false;
				}
				const auto& Declaration = Vm.ActiveModule->Functions[Description.FunctionIndex];
				const auto* Body = Description.Body ? Description.Body : &Declaration;
				if (Body->ParamCount != Declaration.ParamCount || Body->LocalCount < ArgumentCount || Body->ClosureTypeName != Declaration.ClosureTypeName || Body->ParamFlags != Declaration.ParamFlags || Body->bReturnsVoid != Declaration.bReturnsVoid || Body->bReturnsRef != Declaration.bReturnsRef || Body->bIsMethod != Declaration.bIsMethod || Body->bIsStaticMethod != Declaration.bIsStaticMethod || Body->bIsConstMethod != Declaration.bIsConstMethod || Body->OwnerTypeIndex != Declaration.OwnerTypeIndex)
				{
					ReportError("rewritten JIT body has a different callable signature");
					return false;
				}
				const FJitFrameToken Token{AllocateJitProgramIdentity(), Vm.Frames.size()};
				if (!Vm.EnterJitRuntimeFrame(Body, Body->LocalCount, Prepared.data(), ArgumentCount, Receiver, Token.Identity))
				{
					return false;
				}
				FrameToken = Token;
				OutToken = Token;
				Vm.NotifyExecutionCommitted();
				return true;
			});
			const bool bReleased = Guard([&]()
			{
				return Vm.ReleaseValueList(Prepared, {});
			});
			return bOk && bReleased;
		}

		bool Leave(const FJitFrameToken Token) noexcept override
		{
			return Guard([&]()
			{
				if (!ValidateFrame(Token)) return false;
				const auto* Body = Vm.Frames.back().Function;
				FrameToken = {};
				return Vm.LeaveJitRuntimeFrame(Body);
			});
		}

		bool SetInstructionOffset(const FJitFrameToken Token, const std::size_t Offset) noexcept override
		{
			return Guard([&]()
			{
				return SetActiveInstruction(Token, Offset);
			});
		}

		bool LoadLocal(const FJitFrameToken Token, const std::uint16_t Slot, FValue& OutValue) noexcept override
		{
			return Guard([&]()
			{
				FValue Value;
				return ValidateFrame(Token) && Vm.JitLoadLocal(Slot, Value) && Vm.AssignValue(OutValue, std::move(Value), {});
			});
		}

		bool StoreLocal(const FJitFrameToken Token, const std::uint16_t Slot, const FValue& Value) noexcept override
		{
			return Guard([&]()
			{
				return ValidateFrame(Token) && Vm.JitStoreLocal(Slot, Value);
			});
		}

		FJitLocalStorageView BorrowDirectLocalStorage(const FJitFrameToken Token) noexcept override
		{
			if (!ValidateFrame(Token)) return {};
			auto& Locals = Vm.Frames.back().Locals;
			return {Locals.data(), Locals.size()};
		}

		FValue* BorrowLocal(const FJitFrameToken Token, const std::uint16_t Slot) noexcept override
		{
			FValue* Storage = nullptr;
			(void)Guard([&]()
			{
				if (!ValidateFrame(Token)) return false;
				if (Slot >= Vm.Frames.back().Locals.size())
				{
					ReportError("invalid JIT local slot");
					return false;
				}
				Storage = &Vm.Frames.back().Locals[Slot];
				return true;
			});
			return Storage;
		}

		FValue* BorrowResolvedLocal(const FJitFrameToken Token, const std::uint16_t Slot) noexcept override
		{
			auto* Local = BorrowLocal(Token, Slot);
			FValue* Storage = nullptr;
			(void)Guard([&]()
			{
				if (Local)
				{
					Storage = Vm.ResolveRefStorage(*Local);
				}
				if (!Storage)
				{
					ReportError("invalid JIT local reference");
				}
				return Storage != nullptr;
			});
			return Storage;
		}

		bool MakeLocalReference(const FJitFrameToken Token, const std::uint16_t Slot, FValue& OutReference) noexcept override
		{
			return Guard([&]()
			{
				FValue Reference;
				return ValidateFrame(Token) && Vm.JitMakeLocalRef(Slot, Reference) && Vm.AssignValue(OutReference, std::move(Reference), {});
			});
		}

		bool LoadGlobal(const std::uint16_t Slot, FValue& OutValue) noexcept override
		{
			return Guard([&]()
			{
				FValue Value;
				return Vm.JitLoadGlobal(Slot, Value) && Vm.AssignValue(OutValue, std::move(Value), {});
			});
		}

		bool StoreGlobal(const std::uint16_t Slot, FValue Value) noexcept override
		{
			return Guard([&]()
			{
				return Vm.JitStoreGlobal(Slot, std::move(Value));
			});
		}

		bool MakeGlobalReference(const std::uint16_t Slot, FValue& OutReference) noexcept override
		{
			return Guard([&]()
			{
				FValue Reference;
				return Vm.JitMakeGlobalRef(Slot, Reference) && Vm.AssignValue(OutReference, std::move(Reference), {});
			});
		}

		bool EvaluateArithmetic(const EOpcode Operation, const FValue& Left,
		    const FValue& Right, FValue& OutValue) noexcept override
		{
			return Guard([&]()
			{
				FValue Value;
				return Vm.JitEvalArithmetic(Operation, Left, Right, Value) && Vm.AssignValue(OutValue, std::move(Value), {});
			});
		}

		bool EvaluateUnary(const EOpcode Operation, const FValue& Value, FValue& OutValue) noexcept override
		{
			return Guard([&]()
			{
				FValue Result;
				return Vm.JitEvalUnary(Operation, Value, Result) && Vm.AssignValue(OutValue, std::move(Result), {});
			});
		}

		bool EvaluateBitwise(const EOpcode Operation, const FValue& Left,
		    const FValue& Right, FValue& OutValue) noexcept override
		{
			return Guard([&]()
			{
				FValue Value;
				return Vm.JitEvalBitwise(Operation, Left, Right, Value) && Vm.AssignValue(OutValue, std::move(Value), {});
			});
		}

		bool Compare(const EOpcode Operation, const FValue& Left,
		    const FValue& Right, bool& OutValue) noexcept override
		{
			return Guard([&]()
			{
				return Vm.JitCompare(Operation, Left, Right, OutValue);
			});
		}
		//~ End IJitFrameServices Interface

		#include "Internal/VeroScriptVmJitValueServices.inl"
		#include "Internal/VeroScriptVmJitCallServices.inl"
		#include "Internal/VeroScriptVmJitScalarServices.inl"

		explicit FVmJitRuntimeServices(VeroScriptVm& InVm,
		    const std::size_t InFunctionIndex = static_cast<std::size_t>(-1)) noexcept
		    : Vm(InVm), InvocationFunctionIndex(InFunctionIndex)
		{
		}

		bool RestoreCallDepth(const std::size_t Expected) noexcept
		{
			if (OptimizedCallDepth == 0 && Vm.ActiveJitCallDepth == Expected) return true;
			ReportError("JIT entry left unbalanced optimized call depth");
			OptimizedCallDepth = 0;
			Vm.ActiveJitCallDepth = Expected;
			return false;
		}

		template <typename F>
		bool Guard(F&& Operation) noexcept
		{
			try
			{
				return Operation();
			}
			catch (const std::bad_alloc&)
			{
				ReportError("out of memory in JIT runtime service");
			}
			catch (const std::exception& Error)
			{
				ReportError(Error.what());
			}
			catch (...)
			{
				ReportError("native exception in JIT runtime service");
			}
			return false;
		}

	private:
		struct FScalarOperand
		{
			std::int32_t Value;
			bool IsBoolean;
		};
		struct FScalarBlockState
		{
			std::array<FScalarOperand, 64> Values;
			std::size_t Depth = 0;
		};

		#include "Internal/VeroScriptVmCompiledInstruction.inl"
		#include "Internal/VeroScriptVmCompiledScalars.inl"

		template <typename F>
		bool ProduceValue(FValue& OutValue, F&& Operation) noexcept
		{
			FValue Value;
			bool bOk = Guard([&]()
			{
				return Operation(Value);
			});
			if (bOk)
			{
				bOk = Guard([&]()
				{
					// A moved native array has no payload. Transfer ownership and
					// neutralize the temporary before language cleanup examines it.
					return Vm.AssignValue(OutValue, std::exchange(Value, FValue{std::int32_t{0}}), {});
				});
			}
			const bool bReleased = ReleaseValue(Value);
			return bOk && bReleased;
		}

		template <typename F>
		bool CallService(const FValue* const Arguments, const std::uint8_t Count,
		    FValue& OutValue, const bool bStackResult, F&& Operation) noexcept
		{
			const auto FrameDepth = Vm.Frames.size();
			const auto StackDepth = Vm.OperandStack.size();
			const auto SavedStop = Vm.StopFrameDepth;
			const bool bOk = ProduceValue(OutValue, [&](FValue& Result)
			{
				if (!CheckExecutionGovernance()) return false;
				if (Count != 0 && !Arguments)
				{
					ReportError("missing JIT call arguments");
					return false;
				}
				if (!Operation(Result) || HasError() || HasPendingControlTransfer()) return false;
				if (Vm.Frames.size() != FrameDepth || Vm.OperandStack.size() != StackDepth + (bStackResult ? 1 : 0))
				{
					ReportError("JIT language call left unbalanced execution state");
					return false;
				}
				if (bStackResult) return Vm.AssignValue(Result, Vm.Pop(), {});
				return true;
			});
			Vm.StopFrameDepth = SavedStop;
			if (!bOk)
			{
				(void)Guard([&]()
				{
					return Vm.DiscardExecutionAbove(FrameDepth, StackDepth, {});
				});
			}
			return bOk;
		}

		bool PrepareArguments(const std::uint16_t FunctionIndex, const FObjectHandle& Receiver,
		    const FValue* const Arguments, const std::uint8_t ArgumentCount,
		    std::vector<FValue>& CallArguments)
		{
			if (HasError() || HasPendingControlTransfer())
			{
				return false;
			}
			if (ArgumentCount != 0 && !Arguments)
			{
				ReportError("missing JIT call arguments");
				return false;
			}
			if (!Vm.ActiveModule || FunctionIndex >= Vm.ActiveModule->Functions.size())
			{
				ReportError("invalid JIT call target");
				return false;
			}
			const auto& Target = Vm.ActiveModule->Functions[FunctionIndex];
			if (Target.ParamCount != ArgumentCount || Target.bDeleted || !Target.UnavailableReason.empty() || (Target.bIsAbstract && Target.Code.empty()) || (Target.bIsMethod && !Target.bIsStaticMethod && !Receiver) || ((!Target.bIsMethod || Target.bIsStaticMethod) && Receiver))
			{
				ReportError("invalid JIT call signature or receiver");
				return false;
			}
			const std::string* ReceiverOwner = nullptr;
			if (Receiver)
			{
				if (Target.OwnerTypeIndex < Vm.ActiveModule->Types.size())
				{
					ReceiverOwner = &Vm.ActiveModule->Types[Target.OwnerTypeIndex].Name;
				}
				else if (Target.OwnerTypeIndex == static_cast<std::uint16_t>(-1))
				{
					const auto Separator = FindMethodLinkageSeparator(Target.Name);
					std::uint16_t InterfaceIndex = 0;
					if (Separator != std::string::npos && ModuleMeta::FindInterfaceIndex(*Vm.ActiveModule, Target.Name.substr(0, Separator), InterfaceIndex))
					{
						ReceiverOwner = &Vm.ActiveModule->Interfaces[InterfaceIndex].Name;
					}
				}
			}
			if (Receiver && (!ReceiverOwner || ReceiverOwner->empty() || Vm.ResolveObjectTypeIndex(*Receiver) == static_cast<std::uint16_t>(-1) || !Vm.IsActiveObjectTypeAssignableTo(Receiver->GetTypeName(), *ReceiverOwner)))
			{
				ReportError("JIT call receiver belongs to an incompatible type or generation");
				return false;
			}
			std::vector<std::string> ParameterTypes;
			std::string ReturnType;
			if (!Target.ClosureTypeName.empty() && (!Runtime::ParseFunctionType(Target.ClosureTypeName, ParameterTypes, ReturnType) || ParameterTypes.size() != ArgumentCount))
			{
				ReportError("invalid JIT call signature metadata");
				return false;
			}
			CallArguments.resize(ArgumentCount, FValue{std::int32_t{0}});
			for (std::size_t Index = 0; Index < ArgumentCount; ++Index)
			{
				FValue Binding = Arguments[Index];
				FValue* const Storage = Vm.ResolveRefStorage(Binding);
				const bool bReference = Index < Target.ParamFlags.size()
				    ? (Target.ParamFlags[Index] & (BytecodeParamFlagRef | BytecodeParamFlagConstRef)) != 0
				    : !ParameterTypes.empty() && Runtime::IsReferenceTypeName(ParameterTypes[Index]);
				if (!Storage || (bReference && Storage == &Binding) || (!ParameterTypes.empty() && !Vm.ValueMatchesElementType(*Storage, std::string(Runtime::StripReferenceTypeName(ParameterTypes[Index])), Target.Location)))
				{
					ReportError("JIT call argument type or reference mismatch");
					return false;
				}
				if (bReference)
				{
					CallArguments[Index] = std::move(Binding);
				}
				else
				{
					// Bytecode operands already contain explicit CloneStruct operations.
					// This boundary transfers roots exactly as CallFunctionAt does.
					CallArguments[Index] = *Storage;
				}
			}
			return true;
		}

		bool SetActiveInstruction(const FJitFrameToken Token, const std::size_t Offset)
		{
			if (!ValidateFrame(Token)) return false;
			if (Offset >= Vm.Frames.back().Function->Code.size())
			{
				ReportError("invalid JIT bytecode instruction offset");
				return false;
			}
			Vm.ActiveInstructionStart = Offset;
			Vm.Frames.back().LastInstructionOffset = Offset;
			return true;
		}

		// Internal callers already hold the exception boundary for this operation.
		bool CheckActiveStep(const std::size_t Offset)
		{
			if (!SetActiveInstruction(FrameToken, Offset)) return false;
			return !HasError() && !HasPendingControlTransfer() &&
			    (!Vm.HasExecutionGovernance() || Vm.CheckExecutionStep(Vm.Frames.back().Function, Offset));
		}

		bool ValidateFrame(const FJitFrameToken Token)
		{
			if (Token.Identity == 0 || Token.Identity != FrameToken.Identity || Token.Index != FrameToken.Index || Vm.Frames.size() != Token.Index + 1 || Vm.Frames.back().JitRuntimeFrameIdentity != Token.Identity)
			{
				ReportError("invalid or inactive JIT runtime frame");
				return false;
			}
			return true;
		}

		VeroScriptVm& Vm;
		std::size_t InvocationFunctionIndex;
		FJitFrameToken FrameToken;
		std::size_t OptimizedCallDepth = 0;
		bool bCompiledActive = false;
		bool bCompiledReturned = false;
		std::size_t CompiledFrameBase = 0;
		std::size_t CompiledStackBase = 0;
		FJitCallContext* CompiledContext = nullptr;
		std::unique_ptr<FScalarBlockState> ScalarBlockState;
		// Cache metadata only. Revalidate the receiver and resolve its current field
		// storage on every borrow, including after host reentry and reference changes.
		const FBytecodeModule* FieldLookupModule = nullptr;
		std::uint64_t FieldLookupGeneration = 0;
		std::uint16_t FieldLookupTypeIndex = 0xFFFF;
		std::uint16_t FieldLookupNameIndex = 0xFFFF;
		std::uint16_t FieldLookupIndex = 0xFFFF;
	};

	bool VeroScriptVm::SetJitBackendFactory(
	    std::shared_ptr<const IJitBackendFactory> Factory, std::string& OutError)
	{
		OutError.clear();
		if (ActiveModule || ActiveJitCallDepth != 0 || !Frames.empty())
		{
			OutError = "cannot change JIT backend while the VM is executing";
			return false;
		}
		InvalidateJitCache();
		JitBackendFactory = std::move(Factory);
		return true;
	}

	bool VeroScriptVm::SetJitBackendPath(const std::string_view Path, std::string& OutError)
	{
		if (ActiveModule || ActiveJitCallDepth != 0 || !Frames.empty())
		{
			OutError = "cannot change JIT backend while the VM is executing";
			return false;
		}
		auto Factory = Path.empty() ? std::shared_ptr<const IJitBackendFactory>{} : LoadJitBackend(Path, OutError);
		if (!Path.empty() && !Factory) return false;
		return SetJitBackendFactory(std::move(Factory), OutError);
	}

	bool VeroScriptVm::SetJitProgramOwner(
	    std::shared_ptr<const FBytecodeModule> Owner, std::string& OutError)
	{
		OutError.clear();
		if (ActiveModule || ActiveJitCallDepth != 0 || !Frames.empty())
		{
			OutError = "cannot change JIT module ownership while the VM is executing";
			return false;
		}
		BackendCache.reset();
		ResetModuleLookupCaches();
		JitProgramOwner = std::move(Owner);
		return true;
	}

	bool VeroScriptVm::SetJitResourceLimits(const FJitResourceLimits Limits, std::string& OutError)
	{
		OutError.clear();
		if (ActiveModule || ActiveJitCallDepth != 0 || !Frames.empty())
		{
			OutError = "cannot change JIT resource limits while the VM is executing";
			return false;
		}
		BackendCache.reset();
		JitResourceLimits = Limits;
		return true;
	}

	FJitResourceUsage VeroScriptVm::GetJitResourceUsage() const noexcept
	{
		return BackendCache ? BackendCache->GetResourceUsage() : FJitResourceUsage{};
	}

	bool VeroScriptVm::SetAotProgram(std::shared_ptr<const IAotProgram> Program, std::string& OutError)
	{
		OutError.clear();
		if (ActiveModule || ActiveJitCallDepth || !Frames.empty())
		{
			OutError = "cannot change an AOT program while executing";
			return false;
		}
		AotProgram = std::move(Program);
		ValidatedAotModule = nullptr;
		return true;
	}

	void VeroScriptVm::PrepareBackendForExecution(
	    const FBytecodeModule& Module, const std::size_t PreferredFunctionIndex)
	{
		BackendStats = {};
		AotNativeExecutions = 0;
		ValidatedAotModule = nullptr;
		FVmJitRuntimeServices Services(*this);
		if (AotProgram && GetEffectiveExecutionBackend() == EExecutionBackend::Mixed)
		{
			std::string ValidationError;
			if (!ActiveNatives || !ValidateAotProgram(*AotProgram, Module, *ActiveNatives, ValidationError, Services.CompilationCheck()))
			{
				Services.ReportError(ValidationError.empty() ? "AOT program validation failed" : std::string_view(ValidationError));
				return;
			}
			ValidatedAotModule = &Module;
		}

		if (!JitBackendFactory && RequestedExecutionBackend == EExecutionBackend::Jit && Debugger == nullptr)
		{
			ReportError("JIT execution requires an explicitly selected backend file, directory, or factory");
			return;
		}
		if (!JitBackendFactory || GetEffectiveExecutionBackend() == EExecutionBackend::Vm ||
		    (GetEffectiveExecutionBackend() == EExecutionBackend::Mixed && HasExecutionGovernance() && !JitPolicy.bEnableControlledMixed))
		{
			BackendCache.reset();
			return;
		}
		if (JitProgramOwner && JitProgramOwner.get() != &Module)
		{
			Services.ReportError("JIT module owner does not own the executing module");
			return;
		}
		const auto RequiredGovernance = Services.GetRequiredCapabilities();
		const bool bReuse = BackendCache && BackendGovernanceRequirements == RequiredGovernance && BackendCache->GetFactory() == JitBackendFactory && BackendCache->GetProgram().Module == &Module && BackendCache->GetProgram().Identity.Generation == ProgramGeneration && BackendCache->GetProgram().Identity.NativeBindingsGeneration == JitNativeBindingsGeneration && BackendCache->GetProgram().Owner == JitProgramOwner && JitProgramOwner != nullptr;
		if (bReuse)
		{
			// Compilation checks may invalidate the VM cache just like invocation
			// callbacks. Retain this preparation without republishing it afterwards.
			const auto Cache = BackendCache;
			Cache->BeginRun();
			if (!Cache->Prepare(GetEffectiveExecutionBackend(), PreferredFunctionIndex, Services.CompilationCheck(), RequiredGovernance))
			{
				Services.ReportError(Cache->GetLastError());
			}
			return;
		}
		if (FJitBackendCache::MinimumRetainedBytes(Module.Functions.size()) > JitResourceLimits.RetainedBytes)
		{
			BackendCache.reset();
			++BackendStats.CacheCapacitySkips;
			return;
		}
		BackendCache.reset();
		FJitProgramView Program{&Module, JitProgramOwner,
		    {AllocateJitProgramIdentity(), ProgramGeneration, JitNativeBindingsGeneration}};
		std::string ErrorMessage;
		BackendCache = FJitBackendCache::Create(JitBackendFactory,
		    std::move(Program), JitPolicy, JitResourceLimits,
		    JitFrameServiceCapability | JitValueServiceCapability | JitCallServiceCapability | JitScalarServiceCapability | CompiledInstructionServiceCapability, ErrorMessage);
		if (!BackendCache)
		{
			Services.ReportError(ErrorMessage.empty() ? "JIT backend initialization failed" : std::string_view(ErrorMessage));
			return;
		}
		BackendGovernanceRequirements = RequiredGovernance;
		const auto Cache = BackendCache;
		Cache->BeginRun();
		if (!Cache->Prepare(GetEffectiveExecutionBackend(), PreferredFunctionIndex, Services.CompilationCheck(), RequiredGovernance))
		{
			Services.ReportError(Cache->GetLastError());
		}
	}

	void VeroScriptVm::FinishBackendExecution() noexcept
	{
		ReleaseVmBackend();
		ValidatedAotModule = nullptr;
		if (BackendCache && !BackendCache->GetProgram().Owner)
		{
			BackendStats = BackendCache->GetStats();
			BackendCache.reset();
		}
	}

	bool VeroScriptVm::TryDispatchBackendCall(
	    const std::uint16_t FunctionIndex, const FValue* const Arguments,
	    const std::uint8_t ArgumentCount, const FObjectHandle& Receiver, bool& OutOk)
	{
		OutOk = false;
		const auto StaticProgramOwner = AotProgram;
		// The AOT lookup is virtual. Snapshot the cache first when an AOT
		// provider could reenter, and otherwise defer ownership until invocation.
		auto Cache = StaticProgramOwner ? BackendCache : std::shared_ptr<FJitBackendCache>{};
		const auto* Cached = StaticProgramOwner ? Cache.get() : BackendCache.get();
		const auto* StaticFunction = StaticProgramOwner && ValidatedAotModule == ActiveModule &&
		        GetEffectiveExecutionBackend() == EExecutionBackend::Mixed
		    ? StaticProgramOwner->Find(FunctionIndex)
		    : nullptr;
		if ((!Cached && !StaticFunction) || GetEffectiveExecutionBackend() == EExecutionBackend::Vm ||
		    bHasPendingScriptException || !ActiveContainerCopyReads.empty())
		{
			if (JitPolicy.Diagnostics.MaxFunctions)
			{
				RecordFunctionFallback(FunctionIndex, Debugger													? EJitFallbackReason::Debugger
													  : bHasPendingScriptException || ExecutionCleanup			? EJitFallbackReason::ExceptionHandling
													  : GetEffectiveExecutionBackend() == EExecutionBackend::Vm ? EJitFallbackReason::None
																												: EJitFallbackReason::Ownership);
			}
			// Cleanup calls enter the interpreter, whose Run scope parks the pending
			// exception while destructors execute. The interpreter also preserves
			// container reads across synchronous copy callbacks.
			return false;
		}
		const auto PreferVm = [&](const FJitCompiledFunction *Candidate)
		{
			if (!Candidate || !(Candidate->bPreferVmForExecution ||
								  (Candidate->bPreferVmForNestedExecution && !Frames.empty() &&
									  Frames.back().JitRuntimeFrameIdentity == 0 && !HasExecutionGovernance())))
			{
				return false;
			}
			++JitDispatchAttempts;
			++JitFallbacks;
			if (Candidate->FallbackReason == EJitFallbackReason::CostModel || Candidate->FallbackReason == EJitFallbackReason::None)
			{
				++JitCostModelVmFallbacks;
			}
			RecordFunctionFallback(FunctionIndex, Candidate->FallbackReason == EJitFallbackReason::None ? EJitFallbackReason::CostModel : Candidate->FallbackReason);
			return true;
		};
		if (!StaticFunction && Cached)
		{
			if (Cached->IsUnsupported(FunctionIndex))
			{
				if (JitPolicy.Diagnostics.MaxFunctions)
				{
					RecordFunctionFallback(FunctionIndex, Cached->GetFunctionFallbackReason(FunctionIndex));
				}
				++JitDispatchAttempts;
				++JitFallbacks;
				return false;
			}
			if (PreferVm(Cached->FindBorrowed(FunctionIndex))) return false;
		}
		if (!StaticProgramOwner)
		{
			// Keep the cache alive across governance callbacks, compilation and
			// execution, all of which may reconfigure the VM.
			Cache = BackendCache;
		}
		FVmJitRuntimeServices Services(*this, FunctionIndex);
		if (!Services.CheckExecutionGovernance())
		{
			return true;
		}
		if (StaticFunction && (Services.GetRequiredCapabilities() & ~StaticFunction->GovernanceCapabilities) != 0)
		{
			StaticFunction = nullptr;
			if (Cache && PreferVm(Cache->FindBorrowed(FunctionIndex))) return false;
		}
		if (!StaticFunction && !Cache) return false;

		if (GetLogicalCallDepth() >= CallDepthLimit)
		{
			ReportErrorId("diag.call_stack_overflow");
			return true;
		}
		if (ActiveJitCallDepth >= (StaticFunction ? 8 : Cache->GetDescription().MaxInvocationDepth))
		{
			RecordFunctionFallback(FunctionIndex, EJitFallbackReason::InvocationDepth);
			++JitDispatchAttempts;
			++JitFallbacks;
			return false;
		}
		if (!StaticFunction && !Cache->FindBorrowed(FunctionIndex) && (GetEffectiveExecutionBackend() == EExecutionBackend::Jit || Cache->ShouldCompileOnCall(FunctionIndex)))
		{
			if (Cache->EnsureCompiled(FunctionIndex, Services.CompilationCheck(), Services.GetRequiredCapabilities()) == EJitCompilationResult::Failed)
			{
				Services.ReportError(Cache->GetLastError());
				++JitDispatchAttempts;
				++JitFailures;
				return true;
			}
			// Mixed may publish its first cost hint only on this hot call. Apply it
			// before activating the new executable, just as for a cached artifact.
			if (PreferVm(Cache->FindBorrowed(FunctionIndex))) return false;
		}
		if (!StaticFunction && !Cache->FindBorrowed(FunctionIndex))
		{
			if (JitPolicy.Diagnostics.MaxFunctions)
			{
				RecordFunctionFallback(FunctionIndex, Cache->GetFunctionFallbackReason(FunctionIndex));
			}
			++JitDispatchAttempts;
			++JitFallbacks;
			return false;
		}
		const std::size_t FrameDepth = Frames.size();
		const std::size_t StackDepth = OperandStack.size();
		FJitCallContext Context;
		Context.Services = &Services;
		Context.FunctionIndex = FunctionIndex;
		Context.Arguments = Arguments;
		Context.ArgumentCount = ArgumentCount;
		Context.Receiver = Receiver;
		FJitProgramView StaticProgram{ActiveModule, JitProgramOwner,
		    {0, ProgramGeneration, JitNativeBindingsGeneration}};
		if (StaticFunction)
		{
			Context.Program = &StaticProgram;
		}

		struct FInvocationDepthScope
		{
			explicit FInvocationDepthScope(std::size_t& InDepth) : Depth(InDepth), Baseline(InDepth)
			{
				++Depth;
			}
			~FInvocationDepthScope()
			{
				Depth = Baseline;
			}
			std::size_t& Depth;
			const std::size_t Baseline;
		};
		const FInvocationDepthScope InvocationScope(ActiveJitCallDepth);
		struct FDomainInvocationScope
		{
			VeroScriptVm& Vm;
			FJitCallContext& Context;
			std::size_t& Depth;
			FDomainInvocationScope(VeroScriptVm& InVm, FJitCallContext& InContext, std::size_t& InDepth)
			    : Vm(InVm), Context(InContext), Depth(InDepth)
			{
				++Depth;
			}
			~FDomainInvocationScope()
			{
				if (Context.bEntered)
				{
					Vm.NotifyExecutionCommitted();
				}
				--Depth;
			}
		} DomainScope(*this, Context, VmExecutionDomain->ActiveDepth);
		// The backend reports the actual entered kind below; avoid a second cache
		// lookup just to predict it, including when detailed diagnostics are off.
		FJitDiagnosticScope Diagnostic(*this, FunctionIndex, EJitExecutionKind::Unknown);
		const EJitInvocationResult EntryResult = StaticFunction
		    ? InvokeCompiledFunction(Context, StaticFunction->Entry, &AotNativeExecutions)
		    : Cache->Invoke(FunctionIndex, Context);
		Diagnostic.Entered = Context.bEntered;
		if (Diagnostic.Token)
		{
			SetFunctionDiagnosticKind(Diagnostic.Token, Context.ExecutionKind);
		}
		if (StaticFunction && Context.bEntered && Context.ExecutionKind == EJitExecutionKind::Unknown)
		{
			Context.FallbackReason = EJitFallbackReason::RuntimeGuard;
		}
		if (EntryResult == EJitInvocationResult::NotEntered || Context.FallbackReason != EJitFallbackReason::None)
		{
			RecordFunctionFallback(FunctionIndex, Context.FallbackReason == EJitFallbackReason::None ? EJitFallbackReason::RuntimeGuard : Context.FallbackReason);
		}
		if (Context.bEntered)
		{
			NotifyExecutionCommitted();
		}
		EJitInvocationResult Result = EntryResult;
		if (StaticFunction && Result == EJitInvocationResult::Returned &&
		    Context.bHasReturn == StaticProgram.Module->Functions[FunctionIndex].bReturnsVoid)
		{
			Services.ReportError("invalid AOT return state");
			Result = EJitInvocationResult::Failed;
		}
		if (!Services.RestoreCallDepth(InvocationScope.Baseline + 1))
		{
			Result = EJitInvocationResult::Failed;
		}
		if (Result == EJitInvocationResult::Returned && !Services.CheckExecutionGovernance())
		{
			Result = Services.HasPendingControlTransfer() ? EJitInvocationResult::ControlTransfer : EJitInvocationResult::Failed;
		}
		const bool bUnwoundExit = Result == EJitInvocationResult::ControlTransfer && bExitRequested && Frames.size() <= FrameDepth && OperandStack.size() <= StackDepth;
		if (!bUnwoundExit && (Frames.size() != FrameDepth || OperandStack.size() != StackDepth))
		{
			Services.ReportError("JIT entry left unbalanced execution state");
			Result = EJitInvocationResult::Failed;
		}
		if (Result == EJitInvocationResult::Returned)
		{
			const bool bPushed = Services.Guard([&]()
			{
				Push(Context.bHasReturn ? std::move(Context.ReturnValue) : FValue{std::int32_t{0}});
				Context.bHasReturn = false;
				return !bHasError;
			});
			OutOk = bPushed;
			if (!bPushed)
			{
				++JitFailures;
			}
		}
		else
		{
			(void)Services.ReleaseValue(Context.ReturnValue);
			if (Result == EJitInvocationResult::NotEntered)
			{
				return false;
			}
			(void)Services.Guard([&]()
			{
				return DiscardExecutionAbove(FrameDepth, StackDepth, {});
			});
			if (Result == EJitInvocationResult::ControlTransfer)
			{
				OutOk = bExitRequested;
			}
			else if (EntryResult != EJitInvocationResult::Failed)
			{
				++JitFailures;
			}
		}
		return true;
	}
}
