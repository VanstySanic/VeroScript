// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptExecutionServices.h"
#include "VeroScriptExecutionStep.h"
#include "VeroScriptExecutionStack.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptVmScalarArithmetic.h"
#include <type_traits>

namespace VeroScript
{
	class FBytecodeVmExecution
	{
	public:
		FBytecodeVmExecution(IVmExecutionServices& InServices, FVmExecutionView& InState,
		    const FVmHostServices& InHost) : Services(InServices), State(InState), Host(InHost)
		{
		}
		template <bool bInstrumented, bool bGoverned, bool bBatchArithmetic = false, bool bGlobalInitializing = bInstrumented>
		bool RunLoop();

#if defined(_MSC_VER) && defined(_M_X64)
		__declspec(code_seg(".vstext"))
#endif
		bool RunEnhancedLoop(bool bEnableIndexedExpressions);

		bool CanBatchArithmetic() const noexcept
		{
			return !State.HasExternalExecutionCheck() && !State.GetExecutionControl() &&
			    !State.GetExecutionCleanup() &&
			    State.GetExecutionGovernance().Deadline == std::chrono::steady_clock::time_point::max() &&
			    State.GetExternalExecutionDeadline() == std::chrono::steady_clock::time_point::max();
		}

	private:
		using FCallFrame = FVmCallFrame;
		using FObjectLifetimeScope = FVmObjectLifetimeScope;
		enum class ETextReadFusion : std::uint8_t {None, Complete, Failed};
		static constexpr std::size_t MaxOperandStackDepth = 65536;
		void CompleteFusion(FCallFrame& Frame, EOpcode LastOpcode) noexcept;
		bool TryFuseLocalScalarIndex(const std::vector<std::uint8_t>& Code, FCallFrame& Frame,
		    std::uint16_t Slot);
		template <bool bAllowWideAppend>
#if defined(_MSC_VER) && defined(_M_X64)
		__declspec(code_seg(".vstext"))
#endif
		ETextReadFusion TryFuseTextRead(FValue& Value, FCallFrame& Frame,
		    const std::vector<std::uint8_t>& Code, bool AllowWrapper);
#if defined(_MSC_VER) && defined(_M_X64)
		__declspec(code_seg(".vstext"))
#endif
		ETextReadFusion TryFuseWideTextAppend(FValue& Value, FCallFrame& Frame,
		    const std::vector<std::uint8_t>& Code, std::uint16_t SourceSlot);
#if defined(_MSC_VER) && defined(_M_X64)
		__declspec(code_seg(".vstext"))
#endif
		bool TryFuseTextIdentity(const FValue& Value, FCallFrame& Frame,
		    const std::vector<std::uint8_t>& Code, std::uint16_t Slot);
		std::uint16_t ResolveObjectTypeIndex(const FScriptObject& Object) const;
		std::uint16_t FindFieldIndex(FCallFrame& Frame, std::uint16_t TypeIndex, std::uint16_t NameIndex) const;
		// Keep diagnostic string construction outside the dispatch function.
		void ReportError(const char* Message, const FSourceLocation& Location = {});
		template <bool bBatchArithmetic>
		bool CheckExecutionStep(const FBytecodeFunction* Function, std::size_t Offset)
		{
			return ExecutionDetail::CheckInstruction(*State.GetActiveModule(), Function, Offset,
			    State.GetActiveInstructionStart(), State.GetFrames(), State.GetExecutionGovernance(), State.GetExecutionLimits(),
			    static_cast<bool>(State.GetExecutionCleanup()), [&]()
			{
				if constexpr (bBatchArithmetic)
				{
					// This dispatcher admits only idle-configured quota and permission checks.
					return (!State.GetExecutionCleanup() && !State.GetResetRequiredFlag()) || State.PollGovernance();
				}
				return PollGovernance();
			}, [&](bool Pause)
			{
				return Services.CheckCleanupInstruction(Pause);
			}, [&]()
			{
				Services.InstructionLimitExceeded();
			});
		}
		bool PollGovernance()
		{
			if (State.GetExecutionCleanup() || State.GetResetRequiredFlag() || State.HasExternalExecutionCheck())
			{
				return State.PollGovernance();
			}
			return ExecutionDetail::PollCancellationAndDeadline(State.GetExecutionGovernance(), State.GetExecutionControl(),
			    State.GetExternalExecutionDeadline(),
			    [&](EExecutionInterruption Reason)
			{
				return Services.ExecutionInterrupted(Reason);
			});
		}
		FValue* ResolveRefStorage(FValue& Value)
		{
			// These alternatives cannot contain a reference or a closure cell.
			// All reference identities and user-owned storage remain Core services.
			if (Runtime::IsNumericValue(Value) || std::holds_alternative<bool>(Value) || std::holds_alternative<std::string>(Value) || std::holds_alternative<std::u16string>(Value) || std::holds_alternative<FEnumValue>(Value))
			{
				return &Value;
			}
			return Services.ResolveRefStorage(Value);
		}
		std::int64_t ToSignedInteger64(const FValue& Value) const
		{
			if (const auto* Number = std::get_if<std::int64_t>(&Value)) return *Number;
			if (const auto* Number = std::get_if<std::int32_t>(&Value)) return *Number;
			return Host.Runtime_ToSignedInteger64(Value);
		}
		std::uint64_t ToUnsignedInteger64(const FValue& Value) const
		{
			if (const auto* Number = std::get_if<std::uint64_t>(&Value)) return *Number;
			if (const auto* Number = std::get_if<std::uint32_t>(&Value)) return *Number;
			return Host.Runtime_ToUnsignedInteger64(Value);
		}
		double ToDouble(const FValue& Value) const
		{
			if (const auto* Number = std::get_if<double>(&Value)) return *Number;
			if (const auto* Number = std::get_if<std::int64_t>(&Value)) return static_cast<double>(*Number);
			if (const auto* Number = std::get_if<std::int32_t>(&Value)) return static_cast<double>(*Number);
			return Host.Runtime_ToDouble(Value);
		}
		bool AddIntegerImmediate(FValue& Value, std::int32_t Immediate) const
		{
			if (auto* Number = std::get_if<std::int64_t>(&Value))
			{
				std::int64_t Result = 0;
				if (VmDetail::EvalSigned64Arithmetic(EOpcode::Add, *Number, Immediate, Result) != VmDetail::ESignedArithmeticResult::Ok)
				{
					return false;
				}
				*Number = Result;
				return true;
			}
			return Host.Runtime_AddIntegerImmediate(Value, Immediate);
		}
		bool TryJump(FCallFrame& Frame, std::size_t CodeSize, std::int32_t Offset, const FSourceLocation& Location)
		{
			// Ordinary bytecode branches only update the borrowed instruction cursor.
			// Protected transfers and invalid targets retain Core's unwind/diagnostic path.
			if (Frame.FinallyContinuations.empty() && (!Frame.Function || Frame.Function->ExceptionRegions.empty()))
			{
				const auto Next = static_cast<std::int64_t>(Frame.InstructionPointer) + Offset;
				if (Next >= 0 && static_cast<std::size_t>(Next) <= CodeSize)
				{
					Frame.InstructionPointer = static_cast<std::size_t>(Next);
					return true;
				}
			}
			return Services.TryJump(Frame, CodeSize, Offset, Location);
		}
		static bool IsScalarArgument(const FValue& Value)
		{
			return Runtime::IsNumericValue(Value) || std::holds_alternative<bool>(Value) || std::holds_alternative<FEnumValue>(Value);
		}
		bool PopOperandArguments(std::uint8_t Count, FValue* Inline,
		    std::vector<FValue>& Heap, FValue*& Arguments)
		{
			auto& Stack = State.GetOperandStack();
			const auto Base = State.GetFrames().empty() ? 0 : State.GetFrames().back().StackBase;
			if (Count <= 8 && Stack.size() >= Base && Stack.size() - Base >= Count)
			{
				const auto Begin = Stack.size() - Count;
				bool Scalars = true;
				for (std::size_t I = Begin; I < Stack.size(); ++I)
				{
					Scalars &= IsScalarArgument(Stack[I]);
				}
				if (Scalars)
				{
					for (std::size_t I = Count; I > 0; --I)
					{
						Inline[I - 1] = std::move(Stack.back());
						Stack.pop_back();
					}
					Arguments = Inline;
					return true;
				}
			}
			return Services.PopOperandArguments(Count, Inline, Heap, Arguments);
		}
		bool ReleasePoppedArguments(FValue* Arguments, std::uint8_t Count, const FSourceLocation& Location)
		{
			if (!State.GetPendingExceptionFlag() && !State.GetExitRequestedFlag() && (Count == 0 || Arguments))
			{
				bool Scalars = true;
				for (std::size_t I = 0; I < Count; ++I)
				{
					Scalars &= IsScalarArgument(Arguments[I]);
				}
				if (Scalars)
				{
					for (std::size_t I = Count; I > 0; --I)
					{
						Arguments[I - 1] = std::int32_t{0};
					}
					return true;
				}
			}
			return Services.ReleasePoppedArguments(Arguments, Count, Location);
		}
		FValue Pop()
		{
			return ExecutionDetail::Pop(State.GetOperandStack(), State.GetFrames(),
			    [this](const char* Message)
			{
				ReportError(Message);
			});
		}
		// Preserve the scalar calling convention of Core: a value travels in a
		// register rather than through an addressable forwarding-reference slot.
		template <typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
		void Push(const T Value)
		{
			ExecutionDetail::Push(State.GetOperandStack(), MaxOperandStackDepth, Value,
			    [this](const char* Message)
			{
				ReportError(Message);
			});
		}
		template <typename T, std::enable_if_t<!std::is_arithmetic_v<std::decay_t<T>>, int> = 0>
		void Push(T&& Value)
		{
			ExecutionDetail::Push(State.GetOperandStack(), MaxOperandStackDepth, std::forward<T>(Value),
			    [this](const char* Message)
			{
				ReportError(Message);
			});
		}
		void PopRuntimeFrame() noexcept
		{
			ExecutionDetail::PopFrame(State.GetFrames(), State.GetActiveBackendFrameCredits());
		}
		IVmExecutionServices& Services;
		const FVmExecutionView State;
		const FVmHostServices& Host;
	};

}

#if !VEROSCRIPT_BYTECODE_VM_DYNAMIC
#define VEROSCRIPT_VM_MEMBER_INLINE inline
#include "VeroScriptBytecodeVmMembers.inl"
#undef VEROSCRIPT_VM_MEMBER_INLINE
#endif
