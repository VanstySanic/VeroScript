// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVmScalarCalls.h"
#include "VeroScriptVm.h"
#include "VeroScriptVmJitDiagnostics.h"
#include "VeroScriptVmScalarCallGraph.h"
#include "VeroScriptVmScalarArithmetic.h"

#include <array>

namespace VeroScript
{
	namespace
	{
		constexpr std::size_t MaxLocals = 32;
		constexpr std::size_t MaxStack = 16;
		constexpr std::size_t MaxWrites = 16;

		std::shared_ptr<const FVmScalarCallPlan> BuildPlan(const FBytecodeModule& Module,
		    const FBytecodeFunction& Function)
		{
			auto Plan = std::make_shared<FVmScalarCallPlan>();
			std::size_t Depth = 0, Writes = 0;
			for (std::size_t Ip = 0; Ip < Function.Code.size() && Plan->Instructions.size() < 64;)
			{
				FVmScalarCallPlan::FInstruction I;
				I.Opcode = static_cast<EOpcode>(Function.Code[Ip]);
				const auto Size = Bytecode::InstructionSize(I.Opcode);
				if (!Size || Size > Function.Code.size() - Ip) return {};
				const auto Operand = [&]()
				{
					return Bytecode::ReadU16LE(Function.Code.data() + Ip + 1);
				};
				switch (I.Opcode)
				{
				case EOpcode::Nop:
					{
						break;
					}
				case EOpcode::ConstI32:
					{
						const auto Index = Operand();
						if (Index >= Module.Constants.size() || Module.Constants[Index].Kind != EConstantKind::I32) return {};
						I.Value = Module.Constants[Index].I32;
						++Depth;
						break;
					}
				case EOpcode::LoadLocal:
				case EOpcode::StoreLocal:
				case EOpcode::IncLocal:
				case EOpcode::AddLocalImm:
				case EOpcode::AddLocalLocal:
					{
						I.Slot = Operand();
						if (I.Slot >= Function.LocalCount) return {};
						if (I.Opcode == EOpcode::LoadLocal)
						{
							++Depth;
						}
						if (I.Opcode == EOpcode::StoreLocal)
						{
							if (!Depth) return {};
							--Depth;
						}
						I.Value = I.Opcode == EOpcode::AddLocalImm ? Bytecode::ReadI32LE(Function.Code.data() + Ip + 3)
						    : I.Opcode == EOpcode::AddLocalLocal   ? Bytecode::ReadU16LE(Function.Code.data() + Ip + 3)
						                                           : 1;
						if (I.Opcode == EOpcode::AddLocalLocal && I.Value >= Function.LocalCount) return {};
						break;
					}
				case EOpcode::LoadThisField:
				case EOpcode::StoreThisField:
					{
						I.Slot = Operand();
						if (I.Slot >= Module.StringPool.size()) return {};
						Plan->bUsesReceiver = true;
						if (I.Opcode == EOpcode::LoadThisField)
						{
							++Depth;
						}
						else
						{
							if (++Writes > MaxWrites) return {};
							if (!Depth) return {};
							--Depth;
						}
						break;
					}
				case EOpcode::Add:
				case EOpcode::Sub:
				case EOpcode::Mul:
				case EOpcode::Div:
				case EOpcode::Mod:
				case EOpcode::BitAnd:
				case EOpcode::BitOr:
				case EOpcode::BitXor:
					{
						if (Depth < 2) return {};
						--Depth;
						break;
					}
				case EOpcode::Dup:
					{
						if (!Depth) return {};
						++Depth;
						break;
					}
				case EOpcode::Pop:
					{
						if (!Depth) return {};
						--Depth;
						break;
					}
				case EOpcode::Return:
					{
						if (Depth != 1) return {};
						Plan->Instructions.push_back(I);
						return Plan;
					}
				default:
					return {};
				}
				if (Depth > MaxStack) return {};
				Plan->StackDepth = (std::max)(Plan->StackDepth, Depth);
				Plan->Instructions.push_back(I);
				Ip += Size;
			}
			return {};
		}
	}

	bool VeroScriptVm::TryCallScalarExpression(const std::uint16_t FunctionIndex,
	    const FValue* Arguments, const std::uint8_t ArgumentCount, const FObjectHandle& Receiver,
	    const std::vector<FValue>* Captures, FValue& OutResult)
	{
		if (Frames.empty() || bHasError || bHasPendingScriptException || bExitRequested || HasExecutionGovernance() || Debugger || bHotReloadModuleInitializing || bDemandGlobalInitializing || InstanceCallInterceptor || !ActiveModule || FunctionIndex >= ActiveModule->Functions.size() || GetLogicalCallDepth() >= CallDepthLimit)
		{
			return false;
		}
		const auto& Function = ActiveModule->Functions[FunctionIndex];
		if (Function.bDeleted || !Function.UnavailableReason.empty() || Function.bReturnsRef || BytecodeFunctionHasRefParams(Function) || !Function.ExceptionRegions.empty() || Function.ParamCount != ArgumentCount || Function.LocalCount > MaxLocals || Function.LocalCount < ArgumentCount + Function.Captures.size() || Function.Captures.size() != (Captures ? Captures->size() : 0) || Function.Code.size() > 512 || (ArgumentCount && !Arguments))
		{
			return false;
		}
		if (Receiver && (Function.bReturnsVoid || Receiver->bConstructionIncomplete || Receiver->bDestroyed)) return false;
		std::array<std::int32_t, MaxLocals> Locals{};
		for (std::size_t Index = 0; Index < ArgumentCount; ++Index)
		{
			const auto* Value = std::get_if<std::int32_t>(&Arguments[Index]);
			if (!Value) return false;
			Locals[Index] = *Value;
		}
		std::int32_t* MutableCapture = nullptr;
		std::size_t MutableCaptureSlot = 0;
		if (Captures)
		{
			for (std::size_t Index = 0; Index < Captures->size(); ++Index)
			{
				const FValue& Captured = (*Captures)[Index];
				const std::size_t Slot = ArgumentCount + Index;
				if (const auto* Value = std::get_if<std::int32_t>(&Captured))
				{
					Locals[Slot] = *Value;
					continue;
				}
				const auto& Capture = Function.Captures[Index];
				const bool bSharedReference = Capture.Mode == EClosureCaptureMode::Reference;
				// This plan only returns int/void. Reject other closure signatures before
				// resolving a shared cell or probing a plan on every optional/object read.
				if (bSharedReference && !Function.bReturnsVoid && (Function.ClosureTypeName.size() < 5 || Function.ClosureTypeName.compare(Function.ClosureTypeName.size() - 5, 5, "->int") != 0))
				{
					return false;
				}
				if ((!bSharedReference && Capture.Mode != EClosureCaptureMode::MutableValue) || !Capture.bWritable
				    || Capture.TypeName != "int" || MutableCapture || Receiver || MutationCallback
				    || !ActiveContainerCopyReads.empty())
				{
					return false;
				}
				const auto* Cell = std::get_if<FDynamicArrayHandle>(&Captured);
				if (!Cell || !*Cell || (!bSharedReference && Cell->use_count() != 1)
				    || (*Cell)->TypeName != ClosureReferenceCellTypeName
				    || (*Cell)->ElementTypeName != "__internal" || (*Cell)->HasScalarElements()
				    || (*Cell)->Elements.size() != 1 || (*Cell)->MutationOrigin.FieldIndex != 0xFFFF
				    || !(*Cell)->MutationOrigin.Object.expired() || (*Cell)->MutationOrigin.bContainerElement)
				{
					return false;
				}
				// A shared reference cell is safe here: this bounded plan cannot call
				// another observer and commits its sole captured scalar only on success.
				// Multiple potentially aliased captures and mutation origins stay in VM.
				MutableCapture = std::get_if<std::int32_t>(&(*Cell)->Elements[0]);
				if (!MutableCapture) return false;
				MutableCaptureSlot = Slot;
				Locals[Slot] = *MutableCapture;
			}
		}
		if (ScalarCallPlanModule != ActiveModule || ScalarCallPlanGeneration != ProgramGeneration)
		{
			ScalarCallPlans.clear();
			ScalarCallPlanModule = ActiveModule;
			ScalarCallPlanGeneration = ProgramGeneration;
		}
		auto Found = ScalarCallPlans.find(FunctionIndex);
		if (Found == ScalarCallPlans.end())
		{
			if (ScalarCallPlans.size() >= 1024)
			{
				ScalarCallPlans.clear();
			}
			auto NewPlan = BuildPlan(*ActiveModule, Function);
			if (!NewPlan && !Function.bIsMethod && Function.Captures.empty())
			{
				if (auto Graph = BuildScalarCallGraph(*ActiveModule, FunctionIndex))
				{
					auto CallPlan = std::make_shared<FVmScalarCallPlan>();
					CallPlan->CallGraph = std::move(Graph);
					NewPlan = std::move(CallPlan);
				}
			}
			Found = ScalarCallPlans.emplace(FunctionIndex, std::move(NewPlan)).first;
		}
		const auto& Plan = Found->second;
		FJitDiagnosticScope Diagnostic(*this, FunctionIndex, EJitExecutionKind::VmScalar);
		Diagnostic.Entered = false;
		if (Plan && Plan->CallGraph)
		{
			Diagnostic.Entered = !AotProgram && (RequestedExecutionBackend == EExecutionBackend::Vm || !JitBackendFactory)
			    && !Receiver && !Captures && OperandStack.size() <= MaxOperandStackDepth
			    && TryExecuteScalarCallGraph(*Plan->CallGraph, Arguments, CallDepthLimit - GetLogicalCallDepth(),
			        MaxOperandStackDepth - OperandStack.size(), VmCallSerial, OutResult);
			return Diagnostic.Entered;
		}
		if (!Plan || OperandStack.size() > MaxOperandStackDepth || Plan->StackDepth > MaxOperandStackDepth - OperandStack.size())
		{
			return false;
		}
		if (Plan->bUsesReceiver)
		{
			if (!Receiver || Receiver->ProgramOwner != ProgramOwner || Receiver->bDestroyed
			    || Receiver->bDestroying || Receiver->bConstructionIncomplete || MutationCallback)
			{
				return false;
			}
			if (Receiver->Kind == EScriptObjectKind::Struct)
			{
				// The caller still owns the receiver. An additional owner keeps
				// destruction out of the shortcut's argument/return boundary.
				if (Receiver.use_count() <= 1 || !Function.bIsMethod || Function.bIsStaticMethod
				    || Receiver->RuntimeTypeModule != ActiveModule
				    || Receiver->RuntimeTypeGeneration != ProgramGeneration
				    || Receiver->RuntimeTypeIndex != Function.OwnerTypeIndex
				    || Receiver->RuntimeTypeIndex >= ActiveModule->Types.size()
				    || Receiver->GetHostBase() || Receiver->MutationOrigin.FieldIndex != 0xFFFF
				    || !Receiver->MutationOrigin.Object.expired() || Receiver->MutationOrigin.bContainerElement
				    || !ActiveContainerCopyReads.empty())
				{
					return false;
				}
			}
			else if (Receiver->Kind != EScriptObjectKind::Class)
			{
				return false;
			}
		}
		std::array<std::int32_t, MaxStack> Stack;
		std::size_t Depth = 0, WriteCount = 0;
		std::array<std::pair<std::uint16_t, std::int32_t>, MaxWrites> Writes;
		const auto Type = Plan->bUsesReceiver ? ResolveObjectTypeIndex(*Receiver) : std::uint16_t{0xFFFF};
		// No calls, observers or execution checks occur inside this bounded plan.
		// Probe all arithmetic first. On failure the untouched VM executes normally,
		// preserving the original partial writes, diagnostics and stack trace.
		for (const auto& I : Plan->Instructions)
		{
			switch (I.Opcode)
			{
			case EOpcode::Nop:
				{
					break;
				}
			case EOpcode::ConstI32:
				{
					Stack[Depth++] = I.Value;
					break;
				}
			case EOpcode::LoadLocal:
				{
					Stack[Depth++] = Locals[I.Slot];
					break;
				}
			case EOpcode::StoreLocal:
				{
					Locals[I.Slot] = Stack[--Depth];
					break;
				}
			case EOpcode::IncLocal:
			case EOpcode::AddLocalImm:
			case EOpcode::AddLocalLocal:
				{
					const auto Right = I.Opcode == EOpcode::AddLocalLocal ? Locals[I.Value] : I.Value;
					if (!Runtime::CheckedAdd(Locals[I.Slot], Right, Locals[I.Slot])) return false;
					break;
				}
			case EOpcode::LoadThisField:
			case EOpcode::StoreThisField:
				{
					const auto Field = FindJitFieldIndex(Type, I.Slot);
					if (Field >= Receiver->Fields.size()) return false;
					const auto* Original = std::get_if<std::int32_t>(&Receiver->Fields[Field]);
					if (!Original) return false;
					std::int32_t Value = *Original;
					for (std::size_t Index = WriteCount; Index > 0; --Index)
					{
						if (Writes[Index - 1].first == Field)
						{
							Value = Writes[Index - 1].second;
							break;
						}
					}
					if (I.Opcode == EOpcode::LoadThisField)
					{
						Stack[Depth++] = Value;
					}
					else
					{
						Writes[WriteCount++] = {Field, Stack[--Depth]};
					}
					break;
				}
			case EOpcode::Dup:
				{
					Stack[Depth] = Stack[Depth - 1];
					++Depth;
					break;
				}
			case EOpcode::Pop:
				{
					--Depth;
					break;
				}
			case EOpcode::Return:
				{
					for (std::size_t Index = 0; Index < WriteCount; ++Index)
					{
						const auto [Field, Value] = Writes[Index];
						Receiver->Fields[Field] = Value;
						NotifyFieldMutation(*Receiver, Field);
					}
					if (MutableCapture)
					{
						// The plan cannot call out or observe staged intermediate writes.
						// A failed probe leaves the cell untouched for canonical execution.
						*MutableCapture = Locals[MutableCaptureSlot];
					}
					OutResult = Function.bReturnsVoid ? std::int32_t{0} : Stack[0];
					Diagnostic.Entered = true;
					return true;
				}
			default:
				{
					auto& Left = Stack[Depth - 2];
					const auto Right = Stack[Depth - 1];
					if (I.Opcode == EOpcode::BitAnd)
					{
						Left &= Right;
					}
					else if (I.Opcode == EOpcode::BitOr)
					{
						Left |= Right;
					}
					else if (I.Opcode == EOpcode::BitXor)
					{
						Left ^= Right;
					}
					else if (!VmDetail::TryEvalInt32Arithmetic(I.Opcode, Left, Right, Left))
					{
						return false;
					}
					--Depth;
					break;
				}
			}
		}
		return false;
	}
}
