// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJitNativeLinearHelpers.h"

#include "VeroScriptBytecode.h"
#include "VeroScriptAsmJitRuntime.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptAsmJitExecutionContext.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace VeroScript::AsmJit::JitNative::LinearHelpers
{
	namespace
	{
		constexpr std::int32_t JitStatus(const EJitRuntimeStatus Status)
		{
			return static_cast<std::int32_t>(Status);
		}
		bool IsIntOrBoolTypeName(const std::string_view Type)
		{
			return Type == "int" || Type == "bool";
		}

		bool IsUintTypeName(const std::string_view Type)
		{
			return Type == "uint" || Type == "unsigned int" || Type == "unsigned";
		}

		bool IsDoubleTypeName(const std::string_view Type)
		{
			return Type == "double";
		}

		bool ClosureMatches(
			const std::string_view TypeName,
			bool (*OkParam)(std::string_view),
			bool (*OkRet)(std::string_view))
		{
			if (TypeName.size() < 6 || TypeName.substr(0, 3) != "fn(")
			{
				return false;
			}
			const std::size_t Close = TypeName.find(')');
			if (Close == std::string_view::npos || Close + 2 >= TypeName.size()
				|| TypeName.substr(Close + 1, 2) != "->")
			{
				return false;
			}
			const std::string_view Params = TypeName.substr(3, Close - 3);
			const std::string_view Ret = TypeName.substr(Close + 3);
			if (Ret != "void" && !OkRet(Ret))
			{
				return false;
			}
			for (std::size_t Begin = 0; Begin < Params.size();)
			{
				const std::size_t Comma = Params.find(',', Begin);
				const std::string_view Part = Params.substr(
					Begin,
					Comma == std::string_view::npos ? std::string_view::npos : Comma - Begin);
				if (!Part.empty() && !OkParam(Part))
				{
					return false;
				}
				if (Comma == std::string_view::npos)
				{
					break;
				}
				Begin = Comma + 1;
			}
			return true;
		}

		bool ClosureIsIntBoolOnly(const std::string_view TypeName)
		{
			return ClosureMatches(TypeName, IsIntOrBoolTypeName, IsIntOrBoolTypeName);
		}

		bool ClosureIsUintOnly(const std::string_view TypeName)
		{
			return ClosureMatches(TypeName, IsUintTypeName, IsUintTypeName);
		}

		bool ClosureIsDoubleOnly(const std::string_view TypeName)
		{
			return ClosureMatches(TypeName, IsDoubleTypeName, IsDoubleTypeName);
		}

		bool ValueToI32Bits(const FValue& Value, std::int32_t& OutBits)
		{
			if (const auto* IntValue = std::get_if<std::int32_t>(&Value))
			{
				OutBits = *IntValue;
				return true;
			}
			if (const auto* BoolValue = std::get_if<bool>(&Value))
			{
				OutBits = *BoolValue ? 1 : 0;
				return true;
			}
			return false;
		}

		/**
		 * Native scalar calls overwhelmingly have only a handful of parameters.
		 * Keep those FValue boxes on the stack and allocate only for uncommon
		 * wider signatures.
		 */
		class FSmallValueBuffer
		{
		public:
			explicit FSmallValueBuffer(const std::uint32_t Count)
			{
				if (Count > InlineCapacity)
				{
					Overflow.resize(Count);
				}
			}

			FValue* Data()
			{
				return Overflow.empty() ? Inline.data() : Overflow.data();
			}

			FValue& operator[](const std::size_t Index)
			{
				return Data()[Index];
			}

		private:
			static constexpr std::size_t InlineCapacity = 4;
			std::array<FValue, InlineCapacity> Inline{};
			std::vector<FValue> Overflow;
		};

		bool PackI32ArgsFromClosure(
			const FBytecodeFunction& Callee,
			const std::int32_t* Args,
			std::uint32_t ArgCount,
			FValue* OutValues);
	}

	std::int32_t VEROSCRIPT_JIT_CALL InvokeCall(
		void* const VmContext,
		const std::uint16_t FunctionIndex,
		std::int32_t* const Args,
		const std::uint32_t ArgCount,
		std::int32_t* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FBytecodeModule* const Module = Vm->DebugGetModule();
			if (!Module || FunctionIndex >= Module->Functions.size() || (ArgCount > 0 && !Args))
			{
				Vm->JitReportRuntimeError("JIT native call: invalid target");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const auto& Callee = Module->Functions[FunctionIndex];
			if (!ClosureIsIntBoolOnly(Callee.ClosureTypeName) || ArgCount != Callee.ParamCount)
			{
				Vm->JitReportRuntimeError("JIT native call: unsupported callee");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			bool bDirectHandled = false;
			if (!Vm->JitInvokeFunctionI32Fast(
					FunctionIndex,
					Args,
					static_cast<std::uint8_t>(ArgCount),
					OutResult,
					bDirectHandled))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (bDirectHandled)
			{
				return 0;
			}
			FSmallValueBuffer Values(ArgCount);
			if (!PackI32ArgsFromClosure(Callee, Args, ArgCount, Values.Data()))
			{
				Vm->JitReportRuntimeError("JIT native call: bad arg pack");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (!Vm->JitInvokeCall(FunctionIndex, Values.Data(), static_cast<std::uint8_t>(ArgCount)))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FValue Ret;
			if (!Vm->JitTakeReturn(Ret))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (Callee.bReturnsVoid)
			{
				if (OutResult)
				{
					*OutResult = 0;
				}
				return 0;
			}
			std::int32_t Bits = 0;
			if (!ValueToI32Bits(Ret, Bits))
			{
				Vm->JitReportRuntimeError("JIT native call: bad return type");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (OutResult)
			{
				*OutResult = Bits;
			}
			return 0;

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL EnterDirectScalarRecursion(
		void* const VmContext,
		const std::uint32_t ScalarKind)
	{
		try
		{
			return static_cast<FExecutionContext*>(VmContext)->JitEnterDirectScalarRecursion(
				static_cast<EJitScalarValueKind>(ScalarKind));

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL InvokeCallU32(
		void* const VmContext,
		const std::uint16_t FunctionIndex,
		std::uint32_t* const Args,
		const std::uint32_t ArgCount,
		std::uint32_t* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FBytecodeModule* const Module = Vm->DebugGetModule();
			if (!Module || FunctionIndex >= Module->Functions.size() || (ArgCount > 0 && !Args))
			{
				Vm->JitReportRuntimeError("JIT native call: invalid target");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const auto& Callee = Module->Functions[FunctionIndex];
			if (!ClosureIsUintOnly(Callee.ClosureTypeName) || ArgCount != Callee.ParamCount)
			{
				Vm->JitReportRuntimeError("JIT native call: unsupported uint callee");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			bool bDirectHandled = false;
			if (!Vm->JitInvokeFunctionU32Fast(
					FunctionIndex,
					Args,
					static_cast<std::uint8_t>(ArgCount),
					OutResult,
					bDirectHandled))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (bDirectHandled)
			{
				return 0;
			}
			FSmallValueBuffer Values(ArgCount);
			for (std::uint32_t I = 0; I < ArgCount; ++I)
			{
				Values[I] = Args[I];
			}
			if (!Vm->JitInvokeCall(FunctionIndex, Values.Data(), static_cast<std::uint8_t>(ArgCount)))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FValue Ret;
			if (!Vm->JitTakeReturn(Ret))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (Callee.bReturnsVoid)
			{
				if (OutResult)
				{
					*OutResult = 0;
				}
				return 0;
			}
			const auto* const U = std::get_if<std::uint32_t>(&Ret);
			if (!U)
			{
				Vm->JitReportRuntimeError("JIT native call: bad uint return");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (OutResult)
			{
				*OutResult = *U;
			}
			return 0;

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL InvokeCallF64(
		void* const VmContext,
		const std::uint16_t FunctionIndex,
		double* const Args,
		const std::uint32_t ArgCount,
		double* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FBytecodeModule* const Module = Vm->DebugGetModule();
			if (!Module || FunctionIndex >= Module->Functions.size() || (ArgCount > 0 && !Args))
			{
				Vm->JitReportRuntimeError("JIT native call: invalid target");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const auto& Callee = Module->Functions[FunctionIndex];
			if (!ClosureIsDoubleOnly(Callee.ClosureTypeName) || ArgCount != Callee.ParamCount)
			{
				Vm->JitReportRuntimeError("JIT native call: unsupported double callee");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			bool bDirectHandled = false;
			if (!Vm->JitInvokeFunctionF64Fast(
					FunctionIndex,
					Args,
					static_cast<std::uint8_t>(ArgCount),
					OutResult,
					bDirectHandled))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (bDirectHandled)
			{
				return 0;
			}
			FSmallValueBuffer Values(ArgCount);
			for (std::uint32_t I = 0; I < ArgCount; ++I)
			{
				Values[I] = Args[I];
			}
			if (!Vm->JitInvokeCall(FunctionIndex, Values.Data(), static_cast<std::uint8_t>(ArgCount)))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FValue Ret;
			if (!Vm->JitTakeReturn(Ret))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (Callee.bReturnsVoid)
			{
				if (OutResult)
				{
					*OutResult = 0.0;
				}
				return 0;
			}
			const auto* const D = std::get_if<double>(&Ret);
			if (!D)
			{
				Vm->JitReportRuntimeError("JIT native call: bad double return");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (OutResult)
			{
				*OutResult = *D;
			}
			return 0;

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	namespace
	{
		bool PackI32ArgsFromClosure(
			const FBytecodeFunction& Callee,
			const std::int32_t* const Args,
			const std::uint32_t ArgCount,
			FValue* const OutValues)
		{
			if (!ClosureIsIntBoolOnly(Callee.ClosureTypeName) || ArgCount != Callee.ParamCount)
			{
				return false;
			}
			const std::string_view Type = Callee.ClosureTypeName;
			const std::size_t Close = Type.find(')');
			const std::string_view Params = Type.substr(3, Close - 3);
			std::size_t Begin = 0;
			for (std::uint32_t I = 0; I < ArgCount; ++I)
			{
				const std::size_t Comma = Params.find(',', Begin);
				const std::string_view ParamType = Params.substr(
					Begin,
					Comma == std::string_view::npos ? std::string_view::npos : Comma - Begin);
				if (ParamType == "bool")
				{
					OutValues[I] = Args[I] != 0;
				}
				else
				{
					OutValues[I] = Args[I];
				}
				Begin = Comma == std::string_view::npos ? Params.size() : Comma + 1;
			}
			return true;
		}

		std::int32_t FinishI32Return(
			FExecutionContext* const Vm,
			const bool bReturnsVoid,
			FValue& Ret,
			std::int32_t* const OutResult)
		{
			if (bReturnsVoid)
			{
				if (OutResult)
				{
					*OutResult = 0;
				}
				return 0;
			}
			std::int32_t Bits = 0;
			if (!ValueToI32Bits(Ret, Bits))
			{
				Vm->JitReportRuntimeError("JIT native call: bad return type");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (OutResult)
			{
				*OutResult = Bits;
			}
			return 0;
		}

		bool MethodLeafNameMatches(const std::string& CandidateName, const std::string& LeafName)
		{
			return MethodLinkageLeafMatches(CandidateName, LeafName);
		}

		// Resolve a unique scalar CallMethod leaf for packing / void return.
		enum class EResolveCallMethodLeaf : std::uint8_t
		{
			Ok,
			Missing,
			Ambiguous,
		};

		EResolveCallMethodLeaf ResolveCallMethodLeaf(
			const FBytecodeModule& Module,
			const std::string& LeafName,
			const std::uint32_t ArgCount,
			bool (*const ClosureOk)(std::string_view),
			const FBytecodeFunction*& OutCallee)
		{
			OutCallee = nullptr;
			for (const auto& Candidate : Module.Functions)
			{
				if (!Candidate.bIsMethod || Candidate.ParamCount != ArgCount
					|| !MethodLeafNameMatches(Candidate.Name, LeafName)
					|| !ClosureOk(Candidate.ClosureTypeName))
				{
					continue;
				}
				if (OutCallee
					&& (OutCallee->ClosureTypeName != Candidate.ClosureTypeName
						|| OutCallee->bReturnsVoid != Candidate.bReturnsVoid))
				{
					OutCallee = nullptr;
					return EResolveCallMethodLeaf::Ambiguous;
				}
				OutCallee = &Candidate;
			}
			return OutCallee != nullptr
				? EResolveCallMethodLeaf::Ok
				: EResolveCallMethodLeaf::Missing;
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL InvokeBoundMethodFromThisI32(
		void* const VmContext,
		const std::uint16_t FunctionIndex,
		std::int32_t* const Args,
		const std::uint32_t ArgCount,
		std::int32_t* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FBytecodeModule* const Module = Vm->DebugGetModule();
			if (!Module || FunctionIndex >= Module->Functions.size() || (ArgCount > 0 && !Args))
			{
				Vm->JitReportRuntimeError("JIT bound-from-this: invalid target");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const auto& Callee = Module->Functions[FunctionIndex];
			if (!Callee.bIsMethod)
			{
				Vm->JitReportRuntimeError("JIT bound-from-this: not a method");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const FObjectHandle& ThisObject = Vm->GetJitThisObject();
			if (!ThisObject)
			{
				Vm->JitReportRuntimeError("JIT bound-from-this: no this");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			bool bFastHandled = false;
			if (!Vm->JitInvokeBoundMethodI32Fast(
					FunctionIndex,
					ThisObject,
					Args,
					static_cast<std::uint8_t>(ArgCount),
					OutResult,
					bFastHandled))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (bFastHandled)
			{
				return 0;
			}
			// Only box parameters for the interpreter/interceptor fallback. NativeCfg
			// callees consume the raw scalar arguments above, so eagerly constructing
			// FValue variants here would add pure overhead to every hot method call.
			FSmallValueBuffer Values(ArgCount);
			if (!PackI32ArgsFromClosure(Callee, Args, ArgCount, Values.Data()))
			{
				Vm->JitReportRuntimeError("JIT bound-from-this: unsupported callee");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const FValue ThisValue = ThisObject;
			if (!Vm->JitInvokeBoundMethod(
					FunctionIndex, ThisValue, Values.Data(), static_cast<std::uint8_t>(ArgCount)))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FValue Ret;
			if (!Vm->JitTakeReturn(Ret))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			return FinishI32Return(Vm, Callee.bReturnsVoid, Ret, OutResult);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL InvokeBoundMethodFromThisU32(
		void* const VmContext,
		const std::uint16_t FunctionIndex,
		std::uint32_t* const Args,
		const std::uint32_t ArgCount,
		std::uint32_t* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FBytecodeModule* const Module = Vm->DebugGetModule();
			if (!Module || FunctionIndex >= Module->Functions.size() || (ArgCount > 0 && !Args))
			{
				Vm->JitReportRuntimeError("JIT bound-from-this: invalid target");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const auto& Callee = Module->Functions[FunctionIndex];
			if (!Callee.bIsMethod)
			{
				Vm->JitReportRuntimeError("JIT bound-from-this: not a method");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const FObjectHandle& ThisObject = Vm->GetJitThisObject();
			if (!ThisObject)
			{
				Vm->JitReportRuntimeError("JIT bound-from-this: no this");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			bool bFastHandled = false;
			if (!Vm->JitInvokeBoundMethodU32Fast(
					FunctionIndex,
					ThisObject,
					Args,
					static_cast<std::uint8_t>(ArgCount),
					OutResult,
					bFastHandled))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (bFastHandled)
			{
				return 0;
			}
			if (!ClosureIsUintOnly(Callee.ClosureTypeName)
				|| ArgCount != Callee.ParamCount)
			{
				Vm->JitReportRuntimeError("JIT bound-from-this: unsupported uint callee");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FSmallValueBuffer Values(ArgCount);
			for (std::uint32_t I = 0; I < ArgCount; ++I)
			{
				Values[I] = Args[I];
			}
			const FValue ThisValue = ThisObject;
			if (!Vm->JitInvokeBoundMethod(
					FunctionIndex, ThisValue, Values.Data(), static_cast<std::uint8_t>(ArgCount)))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FValue Ret;
			if (!Vm->JitTakeReturn(Ret))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (Callee.bReturnsVoid)
			{
				if (OutResult)
				{
					*OutResult = 0;
				}
				return 0;
			}
			const auto* const U = std::get_if<std::uint32_t>(&Ret);
			if (!U)
			{
				Vm->JitReportRuntimeError("JIT bound-from-this: bad uint return");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (OutResult)
			{
				*OutResult = *U;
			}
			return 0;

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL InvokeBoundMethodFromThisF64(
		void* const VmContext,
		const std::uint16_t FunctionIndex,
		double* const Args,
		const std::uint32_t ArgCount,
		double* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FBytecodeModule* const Module = Vm->DebugGetModule();
			if (!Module || FunctionIndex >= Module->Functions.size() || (ArgCount > 0 && !Args))
			{
				Vm->JitReportRuntimeError("JIT bound-from-this: invalid target");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const auto& Callee = Module->Functions[FunctionIndex];
			if (!Callee.bIsMethod)
			{
				Vm->JitReportRuntimeError("JIT bound-from-this: not a method");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const FObjectHandle& ThisObject = Vm->GetJitThisObject();
			if (!ThisObject)
			{
				Vm->JitReportRuntimeError("JIT bound-from-this: no this");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			bool bFastHandled = false;
			if (!Vm->JitInvokeBoundMethodF64Fast(
					FunctionIndex,
					ThisObject,
					Args,
					static_cast<std::uint8_t>(ArgCount),
					OutResult,
					bFastHandled))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (bFastHandled)
			{
				return 0;
			}
			if (!ClosureIsDoubleOnly(Callee.ClosureTypeName)
				|| ArgCount != Callee.ParamCount)
			{
				Vm->JitReportRuntimeError("JIT bound-from-this: unsupported double callee");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FSmallValueBuffer Values(ArgCount);
			for (std::uint32_t I = 0; I < ArgCount; ++I)
			{
				Values[I] = Args[I];
			}
			const FValue ThisValue = ThisObject;
			if (!Vm->JitInvokeBoundMethod(
					FunctionIndex, ThisValue, Values.Data(), static_cast<std::uint8_t>(ArgCount)))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FValue Ret;
			if (!Vm->JitTakeReturn(Ret))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (Callee.bReturnsVoid)
			{
				if (OutResult)
				{
					*OutResult = 0.0;
				}
				return 0;
			}
			const auto* const D = std::get_if<double>(&Ret);
			if (!D)
			{
				Vm->JitReportRuntimeError("JIT bound-from-this: bad double return");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (OutResult)
			{
				*OutResult = *D;
			}
			return 0;

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL CallMethodFromThisI32(
		void* const VmContext,
		const std::uint16_t NameIndex,
		std::int32_t* const Args,
		const std::uint32_t ArgCount,
		std::int32_t* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FBytecodeModule* const Module = Vm->DebugGetModule();
			if (!Module || NameIndex >= Module->StringPool.size() || (ArgCount > 0 && !Args))
			{
				Vm->JitReportRuntimeError("JIT callmethod-from-this: bad name");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const FBytecodeFunction* Callee = nullptr;
			const EResolveCallMethodLeaf Resolve = ResolveCallMethodLeaf(
				*Module, Module->StringPool[NameIndex], ArgCount, ClosureIsIntBoolOnly, Callee);
			if (Resolve == EResolveCallMethodLeaf::Ambiguous)
			{
				Vm->JitReportRuntimeError("JIT callmethod-from-this: ambiguous signature");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (Resolve != EResolveCallMethodLeaf::Ok || !Callee)
			{
				Vm->JitReportRuntimeError("JIT callmethod-from-this: unsupported callee");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FSmallValueBuffer Values(ArgCount);
			if (!PackI32ArgsFromClosure(*Callee, Args, ArgCount, Values.Data()))
			{
				Vm->JitReportRuntimeError("JIT callmethod-from-this: bad arg pack");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const FObjectHandle& ThisObject = Vm->GetJitThisObject();
			if (!ThisObject)
			{
				Vm->JitReportRuntimeError("JIT callmethod-from-this: no this");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const FValue ThisValue = ThisObject;
			if (!Vm->JitCallMethod(
					NameIndex, ThisValue, Values.Data(), static_cast<std::uint8_t>(ArgCount)))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FValue Ret;
			if (!Vm->JitTakeReturn(Ret))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			return FinishI32Return(Vm, Callee->bReturnsVoid, Ret, OutResult);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL CallMethodFromThisU32(
		void* const VmContext,
		const std::uint16_t NameIndex,
		std::uint32_t* const Args,
		const std::uint32_t ArgCount,
		std::uint32_t* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FBytecodeModule* const Module = Vm->DebugGetModule();
			if (!Module || NameIndex >= Module->StringPool.size() || (ArgCount > 0 && !Args))
			{
				Vm->JitReportRuntimeError("JIT callmethod-from-this: bad name");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const FBytecodeFunction* Callee = nullptr;
			const EResolveCallMethodLeaf Resolve = ResolveCallMethodLeaf(
				*Module, Module->StringPool[NameIndex], ArgCount, ClosureIsUintOnly, Callee);
			if (Resolve == EResolveCallMethodLeaf::Ambiguous)
			{
				Vm->JitReportRuntimeError("JIT callmethod-from-this: ambiguous signature");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (Resolve != EResolveCallMethodLeaf::Ok || !Callee)
			{
				Vm->JitReportRuntimeError("JIT callmethod-from-this: unsupported uint callee");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FSmallValueBuffer Values(ArgCount);
			for (std::uint32_t I = 0; I < ArgCount; ++I)
			{
				Values[I] = Args[I];
			}
			const FObjectHandle& ThisObject = Vm->GetJitThisObject();
			if (!ThisObject)
			{
				Vm->JitReportRuntimeError("JIT callmethod-from-this: no this");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const FValue ThisValue = ThisObject;
			if (!Vm->JitCallMethod(
					NameIndex, ThisValue, Values.Data(), static_cast<std::uint8_t>(ArgCount)))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FValue Ret;
			if (!Vm->JitTakeReturn(Ret))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (Callee->bReturnsVoid)
			{
				if (OutResult)
				{
					*OutResult = 0;
				}
				return 0;
			}
			const auto* const U = std::get_if<std::uint32_t>(&Ret);
			if (!U)
			{
				Vm->JitReportRuntimeError("JIT callmethod-from-this: bad uint return");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (OutResult)
			{
				*OutResult = *U;
			}
			return 0;

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL CallMethodFromThisF64(
		void* const VmContext,
		const std::uint16_t NameIndex,
		double* const Args,
		const std::uint32_t ArgCount,
		double* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FBytecodeModule* const Module = Vm->DebugGetModule();
			if (!Module || NameIndex >= Module->StringPool.size() || (ArgCount > 0 && !Args))
			{
				Vm->JitReportRuntimeError("JIT callmethod-from-this: bad name");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const FBytecodeFunction* Callee = nullptr;
			const EResolveCallMethodLeaf Resolve = ResolveCallMethodLeaf(
				*Module, Module->StringPool[NameIndex], ArgCount, ClosureIsDoubleOnly, Callee);
			if (Resolve == EResolveCallMethodLeaf::Ambiguous)
			{
				Vm->JitReportRuntimeError("JIT callmethod-from-this: ambiguous signature");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (Resolve != EResolveCallMethodLeaf::Ok || !Callee)
			{
				Vm->JitReportRuntimeError("JIT callmethod-from-this: unsupported double callee");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FSmallValueBuffer Values(ArgCount);
			for (std::uint32_t I = 0; I < ArgCount; ++I)
			{
				Values[I] = Args[I];
			}
			const FObjectHandle& ThisObject = Vm->GetJitThisObject();
			if (!ThisObject)
			{
				Vm->JitReportRuntimeError("JIT callmethod-from-this: no this");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const FValue ThisValue = ThisObject;
			if (!Vm->JitCallMethod(
					NameIndex, ThisValue, Values.Data(), static_cast<std::uint8_t>(ArgCount)))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FValue Ret;
			if (!Vm->JitTakeReturn(Ret))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (Callee->bReturnsVoid)
			{
				if (OutResult)
				{
					*OutResult = 0.0;
				}
				return 0;
			}
			const auto* const D = std::get_if<double>(&Ret);
			if (!D)
			{
				Vm->JitReportRuntimeError("JIT callmethod-from-this: bad double return");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (OutResult)
			{
				*OutResult = *D;
			}
			return 0;

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL LoadGlobalI32(void* const VmContext, const std::uint16_t Slot, std::int32_t* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			FValue Value;
			if (!Vm->JitLoadGlobal(Slot, Value))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			std::int32_t Bits = 0;
			if (!ValueToI32Bits(Value, Bits))
			{
				Vm->JitReportRuntimeError("JIT native global: expected int/bool");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (OutResult)
			{
				*OutResult = Bits;
			}
			return 0;

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL StoreGlobalI32(void* const VmContext, const std::uint16_t Slot, const std::int32_t Value)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FBytecodeModule* const Module = Vm->DebugGetModule();
			if (!Module || Slot >= Module->GlobalTypeNames.size())
			{
				Vm->JitReportRuntimeError("JIT native global: bad slot");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const std::string& TypeName = Module->GlobalTypeNames[Slot];
			FValue Stored;
			if (TypeName == "bool")
			{
				Stored = Value != 0;
			}
			else if (TypeName == "int")
			{
				Stored = Value;
			}
			else
			{
				Vm->JitReportRuntimeError("JIT native global: unsupported type");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			return Vm->JitStoreGlobal(Slot, std::move(Stored))
				? JitStatus(EJitRuntimeStatus::Ok)
				: JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL LoadGlobalU32(void* const VmContext, const std::uint16_t Slot, std::uint32_t* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			FValue Value;
			if (!Vm->JitLoadGlobal(Slot, Value))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const auto* const U = std::get_if<std::uint32_t>(&Value);
			if (!U)
			{
				Vm->JitReportRuntimeError("JIT native global: expected uint");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (OutResult)
			{
				*OutResult = *U;
			}
			return 0;

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL StoreGlobalU32(void* const VmContext, const std::uint16_t Slot, const std::uint32_t Value)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			return Vm->JitStoreGlobal(Slot, Value)
				? JitStatus(EJitRuntimeStatus::Ok)
				: JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL LoadThisFieldI32(
		void* const VmContext,
		const std::uint16_t NameIndex,
		std::int32_t* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::int32_t Bits = 0;
			if (!Vm->JitLoadThisFieldI32(NameIndex, Bits))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (OutResult)
			{
				*OutResult = Bits;
			}
			return 0;

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL StoreThisFieldI32(
		void* const VmContext,
		const std::uint16_t NameIndex,
		const std::int32_t Value)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			return Vm->JitStoreThisFieldI32(NameIndex, Value)
				? JitStatus(EJitRuntimeStatus::Ok)
				: JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL InitializeThisFieldI32(
		void* const VmContext,
		const std::uint16_t NameIndex,
		const std::int32_t Value)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			return Vm->JitInitializeThisFieldI32(NameIndex, Value)
				? JitStatus(EJitRuntimeStatus::Ok)
				: JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL IncThisFieldI32(void* const VmContext, const std::uint16_t NameIndex)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FObjectHandle& ThisObject = Vm->GetJitThisObject();
			if (!ThisObject)
			{
				Vm->JitReportRuntimeError("JIT IncField: no this");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			return Vm->JitIncField(ThisObject, NameIndex)
				? JitStatus(EJitRuntimeStatus::Ok)
				: JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL LoadThisFieldU32(
		void* const VmContext,
		const std::uint16_t NameIndex,
		std::uint32_t* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			std::uint32_t Value = 0;
			if (!Vm->JitLoadThisFieldU32(NameIndex, Value))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (OutResult)
			{
				*OutResult = Value;
			}
			return 0;

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL StoreThisFieldU32(
		void* const VmContext,
		const std::uint16_t NameIndex,
		const std::uint32_t Value)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			return Vm->JitStoreThisFieldU32(NameIndex, Value)
				? JitStatus(EJitRuntimeStatus::Ok)
				: JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL InitializeThisFieldU32(
		void* const VmContext,
		const std::uint16_t NameIndex,
		const std::uint32_t Value)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			return Vm->JitInitializeThisFieldU32(NameIndex, Value)
				? JitStatus(EJitRuntimeStatus::Ok)
				: JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL IncThisFieldU32(void* const VmContext, const std::uint16_t NameIndex)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FObjectHandle& ThisObject = Vm->GetJitThisObject();
			if (!ThisObject)
			{
				Vm->JitReportRuntimeError("JIT IncField: no this");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			return Vm->JitIncField(ThisObject, NameIndex)
				? JitStatus(EJitRuntimeStatus::Ok)
				: JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL LoadThisFieldF64(
		void* const VmContext,
		const std::uint16_t NameIndex,
		double* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			double Value = 0.0;
			if (!Vm->JitLoadThisFieldF64(NameIndex, Value))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (OutResult)
			{
				*OutResult = Value;
			}
			return 0;

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL StoreThisFieldF64(
		void* const VmContext,
		const std::uint16_t NameIndex,
		const double Value)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			return Vm->JitStoreThisFieldF64(NameIndex, Value)
				? JitStatus(EJitRuntimeStatus::Ok)
				: JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL InitializeThisFieldF64(
		void* const VmContext,
		const std::uint16_t NameIndex,
		const double Value)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			return Vm->JitInitializeThisFieldF64(NameIndex, Value)
				? JitStatus(EJitRuntimeStatus::Ok)
				: JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL IncThisFieldF64(void* const VmContext, const std::uint16_t NameIndex)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FObjectHandle& ThisObject = Vm->GetJitThisObject();
			if (!ThisObject)
			{
				Vm->JitReportRuntimeError("JIT IncField: no this");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			return Vm->JitIncField(ThisObject, NameIndex)
				? JitStatus(EJitRuntimeStatus::Ok)
				: JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL LoadGlobalF64(void* const VmContext, const std::uint16_t Slot, double* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			FValue Value;
			if (!Vm->JitLoadGlobal(Slot, Value))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const auto* const D = std::get_if<double>(&Value);
			if (!D)
			{
				Vm->JitReportRuntimeError("JIT native global: expected double");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (OutResult)
			{
				*OutResult = *D;
			}
			return 0;

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL StoreGlobalF64(void* const VmContext, const std::uint16_t Slot, const double Value)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			return Vm->JitStoreGlobal(Slot, Value)
				? JitStatus(EJitRuntimeStatus::Ok)
				: JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL MathUnaryF64(void* const VmContext, const std::uint8_t BuiltinId, double* const Slot)
	{
		try
		{
			(void)VmContext;
			if (!Slot)
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const auto Id = static_cast<EBuiltinId>(BuiltinId);
			switch (Id)
			{
			case EBuiltinId::Sqrt:
				{
					*Slot = std::sqrt(*Slot);
					return 0;
				}
			case EBuiltinId::Sin:
				{
					*Slot = std::sin(*Slot);
					return 0;
				}
			case EBuiltinId::Cos:
				{
					*Slot = std::cos(*Slot);
					return 0;
				}
			case EBuiltinId::Tan:
				{
					*Slot = std::tan(*Slot);
					return 0;
				}
			case EBuiltinId::Floor:
				{
					*Slot = std::floor(*Slot);
					return 0;
				}
			case EBuiltinId::Ceil:
				{
					*Slot = std::ceil(*Slot);
					return 0;
				}
			case EBuiltinId::Round:
				{
					*Slot = std::round(*Slot);
					return 0;
				}
			case EBuiltinId::Trunc:
				{
					*Slot = std::trunc(*Slot);
					return 0;
				}
			case EBuiltinId::Exp:
				{
					*Slot = std::exp(*Slot);
					return 0;
				}
			case EBuiltinId::Log:
				{
					*Slot = std::log(*Slot);
					return 0;
				}
			case EBuiltinId::Log2:
				{
					*Slot = std::log2(*Slot);
					return 0;
				}
			case EBuiltinId::Asin:
				{
					*Slot = std::asin(*Slot);
					return 0;
				}
			case EBuiltinId::Acos:
				{
					*Slot = std::acos(*Slot);
					return 0;
				}
			case EBuiltinId::Abs:
				{
					*Slot = std::fabs(*Slot);
					return 0;
				}
			default:
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL ClockNowF64(
		void* const VmContext,
		const std::uint8_t BuiltinId,
		double* const OutResult)
	{
		try
		{
			(void)VmContext;
			if (!OutResult)
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			switch (static_cast<EBuiltinId>(BuiltinId))
			{
			case EBuiltinId::Now:
				{
					*OutResult = std::chrono::duration<double>(
					std::chrono::system_clock::now().time_since_epoch())
									 .count();
					return 0;
				}
			case EBuiltinId::SteadyNow:
				{
					*OutResult = std::chrono::duration<double>(
					std::chrono::steady_clock::now().time_since_epoch())
									 .count();
					return 0;
				}
			default:
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL OutputF64Builtin(
		void* const VmContext,
		const std::uint8_t BuiltinId,
		const double Value)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const auto Id = static_cast<EBuiltinId>(BuiltinId);
			if (Id != EBuiltinId::Print && Id != EBuiltinId::Eprint)
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const FValue Argument = Value;
			bool bHasResult = false;
			FValue Result;
			return Vm->JitInvokeBuiltin(Id, &Argument, 1, bHasResult, Result)
				&& !bHasResult
				? JitStatus(EJitRuntimeStatus::Ok)
				: JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL CmpEqF64(const double Left, const double Right)
	{
		return Left == Right ? 1 : 0;
	}
	std::int32_t VEROSCRIPT_JIT_CALL CmpNeF64(const double Left, const double Right)
	{
		return Left != Right ? 1 : 0;
	}
	std::int32_t VEROSCRIPT_JIT_CALL CmpLtF64(const double Left, const double Right)
	{
		return Left < Right ? 1 : 0;
	}
	std::int32_t VEROSCRIPT_JIT_CALL CmpLtF32(const float Left, const float Right)
	{
		return Left < Right ? 1 : 0;
	}
	std::int32_t VEROSCRIPT_JIT_CALL CmpLeF64(const double Left, const double Right)
	{
		return Left <= Right ? 1 : 0;
	}
	std::int32_t VEROSCRIPT_JIT_CALL CmpGtF64(const double Left, const double Right)
	{
		return Left > Right ? 1 : 0;
	}
	std::int32_t VEROSCRIPT_JIT_CALL CmpGeF64(const double Left, const double Right)
	{
		return Left >= Right ? 1 : 0;
	}

	namespace
	{
		using EMixTag = EMixedScalarTag;

		EJitScalarValueKind JitKindFromMixTag(const EMixTag Tag)
		{
			switch (Tag)
			{
			case EMixTag::I8:
				return EJitScalarValueKind::I8;
			case EMixTag::U8:
				return EJitScalarValueKind::U8;
			case EMixTag::I16:
				return EJitScalarValueKind::I16;
			case EMixTag::U16:
				return EJitScalarValueKind::U16;
			case EMixTag::I32:
				return EJitScalarValueKind::I32;
			case EMixTag::U32:
				return EJitScalarValueKind::U32;
			case EMixTag::I64:
				return EJitScalarValueKind::I64;
			case EMixTag::U64:
				return EJitScalarValueKind::U64;
			case EMixTag::F32:
				return EJitScalarValueKind::F32;
			case EMixTag::F64:
				return EJitScalarValueKind::F64;
			case EMixTag::Bool:
				return EJitScalarValueKind::Bool;
			case EMixTag::Enum:
				return EJitScalarValueKind::None;
			default:
				{
					break;
				}
			}
			return EJitScalarValueKind::None;
		}

		bool TagFromName(const std::string_view Name, EMixTag& Out)
		{
			if (Name == "int8" || Name == "char")
			{
				Out = EMixTag::I8;
				return true;
			}
			if (Name == "uint8" || Name == "unsigned char")
			{
				Out = EMixTag::U8;
				return true;
			}
			if (Name == "int16" || Name == "short")
			{
				Out = EMixTag::I16;
				return true;
			}
			if (Name == "uint16" || Name == "unsigned short")
			{
				Out = EMixTag::U16;
				return true;
			}
			if (Name == "int")
			{
				Out = EMixTag::I32;
				return true;
			}
			if (Name == "uint" || Name == "unsigned int" || Name == "unsigned")
			{
				Out = EMixTag::U32;
				return true;
			}
			if (Name == "double")
			{
				Out = EMixTag::F64;
				return true;
			}
			if (Name == "float")
			{
				Out = EMixTag::F32;
				return true;
			}
			if (Name == "int64" || Name == "long")
			{
				Out = EMixTag::I64;
				return true;
			}
			if (Name == "uint64" || Name == "unsigned long")
			{
				Out = EMixTag::U64;
				return true;
			}
			if (Name == "bool")
			{
				Out = EMixTag::Bool;
				return true;
			}
			return false;
		}

		bool ParseFnTags(
			const std::string_view TypeName,
			std::vector<EMixTag>& OutParams,
			EMixTag& OutRet,
			bool& bVoid)
		{
			OutParams.clear();
			bVoid = false;
			if (TypeName.size() < 6 || TypeName.substr(0, 3) != "fn(")
			{
				return false;
			}
			const std::size_t Close = TypeName.find(')');
			if (Close == std::string_view::npos || Close + 2 >= TypeName.size()
				|| TypeName.substr(Close + 1, 2) != "->")
			{
				return false;
			}
			const std::string_view Params = TypeName.substr(3, Close - 3);
			const std::string_view Ret = TypeName.substr(Close + 3);
			if (Ret == "void")
			{
				bVoid = true;
			}
			else if (!TagFromName(Ret, OutRet))
			{
				return false;
			}
			for (std::size_t Begin = 0; Begin < Params.size();)
			{
				const std::size_t Comma = Params.find(',', Begin);
				EMixTag Tag{};
				if (!TagFromName(
						Params.substr(
							Begin,
							Comma == std::string_view::npos ? std::string_view::npos : Comma - Begin),
						Tag))
				{
					return false;
				}
				OutParams.push_back(Tag);
				if (Comma == std::string_view::npos)
				{
					break;
				}
				Begin = Comma + 1;
			}
			return true;
		}

		std::uint64_t PackI32(const std::int32_t V)
		{
			return static_cast<std::uint32_t>(V);
		}

		std::uint64_t PackU32(const std::uint32_t V)
		{
			return V;
		}

		std::uint64_t PackBool(const bool V)
		{
			return V ? 1u : 0u;
		}

		std::uint64_t PackF64(const double V)
		{
			std::uint64_t Bits = 0;
			std::memcpy(&Bits, &V, sizeof(Bits));
			return Bits;
		}

		std::uint64_t PackF32(const float V)
		{
			std::uint32_t Bits = 0;
			std::memcpy(&Bits, &V, sizeof(Bits));
			return Bits;
		}

		double UnpackF64(const std::uint64_t Bits)
		{
			double V = 0.0;
			std::memcpy(&V, &Bits, sizeof(V));
			return V;
		}

		float UnpackF32(const std::uint64_t Bits)
		{
			const std::uint32_t Narrow = static_cast<std::uint32_t>(Bits);
			float V = 0.0f;
			std::memcpy(&V, &Narrow, sizeof(V));
			return V;
		}

		bool ValueFromBits(const EMixTag Tag, const std::uint64_t Bits, FValue& Out)
		{
			switch (Tag)
			{
			case EMixTag::I8:
				{
					Out = static_cast<std::int8_t>(static_cast<std::uint8_t>(Bits));
					return true;
				}
			case EMixTag::U8:
				{
					Out = static_cast<std::uint8_t>(Bits);
					return true;
				}
			case EMixTag::I16:
				{
					Out = static_cast<std::int16_t>(static_cast<std::uint16_t>(Bits));
					return true;
				}
			case EMixTag::U16:
				{
					Out = static_cast<std::uint16_t>(Bits);
					return true;
				}
			case EMixTag::I32:
				{
					Out = static_cast<std::int32_t>(Bits);
					return true;
				}
			case EMixTag::U32:
				{
					Out = static_cast<std::uint32_t>(Bits);
					return true;
				}
			case EMixTag::Bool:
				{
					Out = (Bits & 1u) != 0;
					return true;
				}
			case EMixTag::F64:
				{
					Out = UnpackF64(Bits);
					return true;
				}
			case EMixTag::I64:
				{
					Out = static_cast<std::int64_t>(Bits);
					return true;
				}
			case EMixTag::U64:
				{
					Out = Bits;
					return true;
				}
			case EMixTag::F32:
				{
					Out = UnpackF32(Bits);
					return true;
				}
			case EMixTag::Enum:
				return false;
			default:
				{
					break;
				}
			}
			return false;
		}

		bool BitsFromValue(const EMixTag Tag, const FValue& Value, std::uint64_t& Out)
		{
			switch (Tag)
			{
			case EMixTag::I8:
				{
					if (const auto* V = std::get_if<std::int8_t>(&Value))
					{
						Out = PackI32(static_cast<std::int32_t>(*V));
						return true;
					}
					return false;
				}
			case EMixTag::U8:
				{
					if (const auto* V = std::get_if<std::uint8_t>(&Value))
					{
						Out = PackU32(*V);
						return true;
					}
					return false;
				}
			case EMixTag::I16:
				{
					if (const auto* V = std::get_if<std::int16_t>(&Value))
					{
						Out = PackI32(static_cast<std::int32_t>(*V));
						return true;
					}
					return false;
				}
			case EMixTag::U16:
				{
					if (const auto* V = std::get_if<std::uint16_t>(&Value))
					{
						Out = PackU32(*V);
						return true;
					}
					return false;
				}
			case EMixTag::I32:
				{
					if (const auto* V = std::get_if<std::int32_t>(&Value))
					{
						Out = PackI32(*V);
						return true;
					}
					return false;
				}
			case EMixTag::U32:
				{
					if (const auto* V = std::get_if<std::uint32_t>(&Value))
					{
						Out = PackU32(*V);
						return true;
					}
					return false;
				}
			case EMixTag::Bool:
				{
					if (const auto* V = std::get_if<bool>(&Value))
					{
						Out = PackBool(*V);
						return true;
					}
					return false;
				}
			case EMixTag::F64:
				{
					if (const auto* V = std::get_if<double>(&Value))
					{
						Out = PackF64(*V);
						return true;
					}
					return false;
				}
			case EMixTag::I64:
				{
					if (const auto* V = std::get_if<std::int64_t>(&Value))
					{
						Out = static_cast<std::uint64_t>(*V);
						return true;
					}
					return false;
				}
			case EMixTag::U64:
				{
					if (const auto* V = std::get_if<std::uint64_t>(&Value))
					{
						Out = *V;
						return true;
					}
					return false;
				}
			case EMixTag::F32:
				{
					if (const auto* V = std::get_if<float>(&Value))
					{
						Out = PackF32(*V);
						return true;
					}
					return false;
				}
			case EMixTag::Enum:
				return false;
			default:
				{
					break;
				}
			}
			return false;
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL InvokeCallMixed(
		void* const VmContext,
		const std::uint16_t FunctionIndex,
		std::uint64_t* const Args,
		const std::uint32_t ArgCount,
		std::uint64_t* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FBytecodeModule* const Module = Vm->DebugGetModule();
			if (!Module || FunctionIndex >= Module->Functions.size()
				|| ArgCount > std::numeric_limits<std::uint8_t>::max()
				|| (ArgCount > 0 && !Args))
			{
				Vm->JitReportRuntimeError("JIT mixed call: invalid target");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			bool bFastHandled = false;
			if (!Vm->JitInvokeFunctionMixedFast(
					FunctionIndex,
					Args,
					static_cast<std::uint8_t>(ArgCount),
					OutResult,
					bFastHandled))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (bFastHandled)
			{
				return 0;
			}
			const auto& Callee = Module->Functions[FunctionIndex];
			std::vector<EMixTag> Params;
			EMixTag Ret = EMixTag::I32;
			bool bVoid = false;
			if (!ParseFnTags(Callee.ClosureTypeName, Params, Ret, bVoid)
				|| ArgCount != Callee.ParamCount
				|| Params.size() != ArgCount
				|| Callee.bReturnsVoid != bVoid)
			{
				Vm->JitReportRuntimeError("JIT mixed call: unsupported callee");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FSmallValueBuffer Values(ArgCount);
			for (std::uint32_t I = 0; I < ArgCount; ++I)
			{
				if (!ValueFromBits(Params[I], Args[I], Values[I]))
				{
					Vm->JitReportRuntimeError("JIT mixed call: bad arg pack");
					return JitStatus(EJitRuntimeStatus::GenericFailure);
				}
			}
			if (!Vm->JitInvokeCall(FunctionIndex, Values.Data(), static_cast<std::uint8_t>(ArgCount)))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FValue RetValue;
			if (!Vm->JitTakeReturn(RetValue))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (bVoid)
			{
				if (OutResult)
				{
					*OutResult = 0;
				}
				return 0;
			}
			std::uint64_t Bits = 0;
			if (!BitsFromValue(Ret, RetValue, Bits))
			{
				Vm->JitReportRuntimeError("JIT mixed call: bad return pack");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (OutResult)
			{
				*OutResult = Bits;
			}
			return 0;

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL InvokeBoundMethodFromThisMixed(
		void* const VmContext,
		const std::uint16_t FunctionIndex,
		std::uint64_t* const Args,
		const std::uint32_t ArgCount,
		std::uint64_t* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FBytecodeModule* const Module = Vm->DebugGetModule();
			if (!Module || FunctionIndex >= Module->Functions.size()
				|| ArgCount > std::numeric_limits<std::uint8_t>::max()
				|| (ArgCount > 0 && !Args))
			{
				Vm->JitReportRuntimeError("JIT mixed bound-from-this: invalid target");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const FObjectHandle& ThisObject = Vm->GetJitThisObject();
			if (!ThisObject)
			{
				Vm->JitReportRuntimeError("JIT mixed bound-from-this: no this");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			bool bFastHandled = false;
			if (!Vm->JitInvokeBoundMethodMixedFast(
					FunctionIndex,
					ThisObject,
					Args,
					static_cast<std::uint8_t>(ArgCount),
					OutResult,
					bFastHandled))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (bFastHandled)
			{
				return 0;
			}
			const auto& Callee = Module->Functions[FunctionIndex];
			std::vector<EMixTag> Params;
			EMixTag Ret = EMixTag::I32;
			bool bVoid = false;
			if (!Callee.bIsMethod
				|| !ParseFnTags(Callee.ClosureTypeName, Params, Ret, bVoid)
				|| ArgCount != Callee.ParamCount
				|| Params.size() != ArgCount
				|| Callee.bReturnsVoid != bVoid)
			{
				Vm->JitReportRuntimeError("JIT mixed bound-from-this: unsupported callee");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FSmallValueBuffer Values(ArgCount);
			for (std::uint32_t I = 0; I < ArgCount; ++I)
			{
				if (!ValueFromBits(Params[I], Args[I], Values[I]))
				{
					Vm->JitReportRuntimeError("JIT mixed bound-from-this: bad arg pack");
					return JitStatus(EJitRuntimeStatus::GenericFailure);
				}
			}
			const FValue ThisValue = ThisObject;
			if (!Vm->JitInvokeBoundMethod(
					FunctionIndex, ThisValue, Values.Data(), static_cast<std::uint8_t>(ArgCount)))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FValue RetValue;
			if (!Vm->JitTakeReturn(RetValue))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (bVoid)
			{
				if (OutResult)
				{
					*OutResult = 0;
				}
				return 0;
			}
			std::uint64_t Bits = 0;
			if (!BitsFromValue(Ret, RetValue, Bits))
			{
				Vm->JitReportRuntimeError("JIT mixed bound-from-this: bad return pack");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (OutResult)
			{
				*OutResult = Bits;
			}
			return 0;

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL CallMethodFromThisMixed(
		void* const VmContext,
		const std::uint16_t NameIndex,
		std::uint64_t* const Args,
		const std::uint32_t ArgCount,
		std::uint64_t* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FBytecodeModule* const Module = Vm->DebugGetModule();
			if (!Module || NameIndex >= Module->StringPool.size() || (ArgCount > 0 && !Args))
			{
				Vm->JitReportRuntimeError("JIT mixed callmethod-from-this: bad name");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const std::string& LeafName = Module->StringPool[NameIndex];
			std::vector<EMixTag> Params;
			EMixTag Ret = EMixTag::I32;
			bool bVoid = false;
			bool bFound = false;
			for (const auto& Candidate : Module->Functions)
			{
				if (!Candidate.bIsMethod || Candidate.ParamCount != ArgCount)
				{
					continue;
				}
				if (!MethodLinkageLeafMatches(Candidate.Name, LeafName))
				{
					continue;
				}
				std::vector<EMixTag> CandParams;
				EMixTag CandRet = EMixTag::I32;
				bool CandVoid = false;
				if (!ParseFnTags(Candidate.ClosureTypeName, CandParams, CandRet, CandVoid)
					|| CandParams.size() != ArgCount
					|| Candidate.bReturnsVoid != CandVoid)
				{
					continue;
				}
				if (bFound
					&& (CandParams != Params || CandRet != Ret || CandVoid != bVoid))
				{
					Vm->JitReportRuntimeError("JIT mixed callmethod-from-this: ambiguous signature");
					return JitStatus(EJitRuntimeStatus::GenericFailure);
				}
				Params = std::move(CandParams);
				Ret = CandRet;
				bVoid = CandVoid;
				bFound = true;
			}
			if (!bFound)
			{
				Vm->JitReportRuntimeError("JIT mixed callmethod-from-this: unsupported callee");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FSmallValueBuffer Values(ArgCount);
			for (std::uint32_t I = 0; I < ArgCount; ++I)
			{
				if (!ValueFromBits(Params[I], Args[I], Values[I]))
				{
					Vm->JitReportRuntimeError("JIT mixed callmethod-from-this: bad arg pack");
					return JitStatus(EJitRuntimeStatus::GenericFailure);
				}
			}
			const FObjectHandle& ThisObject = Vm->GetJitThisObject();
			if (!ThisObject)
			{
				Vm->JitReportRuntimeError("JIT mixed callmethod-from-this: no this");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const FValue ThisValue = ThisObject;
			if (!Vm->JitCallMethod(
					NameIndex, ThisValue, Values.Data(), static_cast<std::uint8_t>(ArgCount)))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FValue RetValue;
			if (!Vm->JitTakeReturn(RetValue))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (bVoid)
			{
				if (OutResult)
				{
					*OutResult = 0;
				}
				return 0;
			}
			std::uint64_t Bits = 0;
			if (!BitsFromValue(Ret, RetValue, Bits))
			{
				Vm->JitReportRuntimeError("JIT mixed callmethod-from-this: bad return pack");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (OutResult)
			{
				*OutResult = Bits;
			}
			return 0;

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL LoadGlobalMixed(void* const VmContext, const std::uint16_t Slot, std::uint64_t* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FBytecodeModule* const Module = Vm->DebugGetModule();
			if (!Module || Slot >= Module->GlobalTypeNames.size() || !OutResult)
			{
				Vm->JitReportRuntimeError("JIT mixed global: bad slot");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			EMixTag Tag{};
			if (!TagFromName(Module->GlobalTypeNames[Slot], Tag))
			{
				Vm->JitReportRuntimeError("JIT mixed global: unsupported type");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FValue Value;
			if (!Vm->JitLoadGlobal(Slot, Value))
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (!BitsFromValue(Tag, Value, *OutResult))
			{
				Vm->JitReportRuntimeError("JIT mixed global: bad load pack");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			return 0;

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL StoreGlobalMixed(void* const VmContext, const std::uint16_t Slot, const std::uint64_t Value)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FBytecodeModule* const Module = Vm->DebugGetModule();
			if (!Module || Slot >= Module->GlobalTypeNames.size())
			{
				Vm->JitReportRuntimeError("JIT mixed global: bad slot");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			EMixTag Tag{};
			if (!TagFromName(Module->GlobalTypeNames[Slot], Tag))
			{
				Vm->JitReportRuntimeError("JIT mixed global: unsupported type");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			FValue Stored;
			if (!ValueFromBits(Tag, Value, Stored))
			{
				Vm->JitReportRuntimeError("JIT mixed global: bad store pack");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			return Vm->JitStoreGlobal(Slot, std::move(Stored))
				? JitStatus(EJitRuntimeStatus::Ok)
				: JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL LoadThisFieldMixed(
		void* const VmContext,
		const std::uint16_t NameIndex,
		const std::uint8_t Tag,
		std::uint64_t* const OutResult)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			if (!OutResult)
			{
				Vm->JitReportRuntimeError("JIT mixed this-field: bad out");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const EJitScalarValueKind Kind = JitKindFromMixTag(static_cast<EMixTag>(Tag));
			return Kind != EJitScalarValueKind::None
				&& Vm->JitLoadThisFieldMixed(NameIndex, Kind, *OutResult)
				? JitStatus(EJitRuntimeStatus::Ok)
				: JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL StoreThisFieldMixed(
		void* const VmContext,
		const std::uint16_t NameIndex,
		const std::uint8_t Tag,
		const std::uint64_t Value)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const EJitScalarValueKind Kind = JitKindFromMixTag(static_cast<EMixTag>(Tag));
			return Kind != EJitScalarValueKind::None
				&& Vm->JitStoreThisFieldMixed(NameIndex, Kind, Value)
				? JitStatus(EJitRuntimeStatus::Ok)
				: JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL InitializeThisFieldMixed(
		void* const VmContext,
		const std::uint16_t NameIndex,
		const std::uint8_t Tag,
		const std::uint64_t Value)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const EJitScalarValueKind Kind = JitKindFromMixTag(static_cast<EMixTag>(Tag));
			return Kind != EJitScalarValueKind::None
				&& Vm->JitInitializeThisFieldMixed(NameIndex, Kind, Value)
				? JitStatus(EJitRuntimeStatus::Ok)
				: JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL IncThisFieldMixed(void* const VmContext, const std::uint16_t NameIndex)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			const FObjectHandle& ThisObject = Vm->GetJitThisObject();
			if (!ThisObject)
			{
				Vm->JitReportRuntimeError("JIT IncField: no this");
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			return Vm->JitIncField(ThisObject, NameIndex)
				? JitStatus(EJitRuntimeStatus::Ok)
				: JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL BinOpMixed(
		const std::uint8_t Opcode,
		const std::uint8_t Tag,
		const std::uint64_t Left,
		const std::uint64_t Right,
		std::uint64_t* const OutResult)
	{
		if (!OutResult)
		{
			return JitStatus(EJitRuntimeStatus::GenericFailure);
		}
		const auto Op = static_cast<EOpcode>(Opcode);
		const auto T = static_cast<EMixTag>(Tag);
		if (T == EMixTag::I32)
		{
			const auto A = static_cast<std::int32_t>(Left);
			const auto B = static_cast<std::int32_t>(Right);
			std::int32_t R = 0;
			switch (Op)
			{
			case EOpcode::Add:
				{
					if (!Runtime::CheckedAdd(A, B, R))
					{
						return JitStatus(EJitRuntimeStatus::IntegerOverflow);
					}
					break;
				}
			case EOpcode::Sub:
				{
					if (!Runtime::CheckedSub(A, B, R))
					{
						return JitStatus(EJitRuntimeStatus::IntegerOverflow);
					}
					break;
				}
			case EOpcode::Mul:
				{
					if (!Runtime::CheckedMul(A, B, R))
					{
						return JitStatus(EJitRuntimeStatus::IntegerOverflow);
					}
					break;
				}
			case EOpcode::Div:
			case EOpcode::Mod:
				{
					if (B == 0)
					{
						return JitStatus(EJitRuntimeStatus::DivisionByZero);
					}
					if (A == std::numeric_limits<std::int32_t>::min() && B == -1)
					{
						return JitStatus(EJitRuntimeStatus::IntegerOverflow);
					}
					R = Op == EOpcode::Div ? A / B : A % B;
					break;
				}
			case EOpcode::BitAnd:
				{
					R = A & B;
					break;
				}
			case EOpcode::BitOr:
				{
					R = A | B;
					break;
				}
			case EOpcode::BitXor:
				{
					R = A ^ B;
					break;
				}

			case EOpcode::Shl:
			case EOpcode::Shr:
				{
					if (B < 0 || B > 31)
					{
						return JitStatus(EJitRuntimeStatus::ShiftOutOfRange);
					}
					R = Op == EOpcode::Shl
					? static_cast<std::int32_t>(static_cast<std::uint32_t>(A) << static_cast<std::uint32_t>(B))
					: static_cast<std::int32_t>(A >> B);
					break;
				}
			case EOpcode::Eq:
				{
					*OutResult = PackBool(A == B);
					return 0;
				}
			case EOpcode::Ne:
				{
					*OutResult = PackBool(A != B);
					return 0;
				}
			case EOpcode::Lt:
				{
					*OutResult = PackBool(A < B);
					return 0;
				}
			case EOpcode::Le:
				{
					*OutResult = PackBool(A <= B);
					return 0;
				}
			case EOpcode::Gt:
				{
					*OutResult = PackBool(A > B);
					return 0;
				}
			case EOpcode::Ge:
				{
					*OutResult = PackBool(A >= B);
					return 0;
				}
			default:
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			*OutResult = PackI32(R);
			return 0;
		}
		if (T == EMixTag::U32)
		{
			const auto A = static_cast<std::uint32_t>(Left);
			const auto B = static_cast<std::uint32_t>(Right);
			std::uint32_t R = 0;
			switch (Op)
			{
			case EOpcode::Add:
				{
					R = A + B;
					break;
				}
			case EOpcode::Sub:
				{
					R = A - B;
					break;
				}
			case EOpcode::Mul:
				{
					R = A * B;
					break;
				}
			case EOpcode::Div:
			case EOpcode::Mod:
				{
					if (B == 0)
					{
						return JitStatus(EJitRuntimeStatus::DivisionByZero);
					}
					R = Op == EOpcode::Div ? A / B : A % B;
					break;
				}
			case EOpcode::BitAnd:
				{
					R = A & B;
					break;
				}
			case EOpcode::BitOr:
				{
					R = A | B;
					break;
				}
			case EOpcode::BitXor:
				{
					R = A ^ B;
					break;
				}

			case EOpcode::Shl:
			case EOpcode::Shr:
				{
					if (B > 31)
					{
						return JitStatus(EJitRuntimeStatus::ShiftOutOfRange);
					}
					R = Op == EOpcode::Shl ? (A << B) : (A >> B);
					break;
				}
			case EOpcode::Eq:
				{
					*OutResult = PackBool(A == B);
					return 0;
				}
			case EOpcode::Ne:
				{
					*OutResult = PackBool(A != B);
					return 0;
				}
			case EOpcode::Lt:
				{
					*OutResult = PackBool(A < B);
					return 0;
				}
			case EOpcode::Le:
				{
					*OutResult = PackBool(A <= B);
					return 0;
				}
			case EOpcode::Gt:
				{
					*OutResult = PackBool(A > B);
					return 0;
				}
			case EOpcode::Ge:
				{
					*OutResult = PackBool(A >= B);
					return 0;
				}
			default:
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			*OutResult = PackU32(R);
			return 0;
		}
		if (T == EMixTag::I64)
		{
			std::int64_t A = 0;
			std::int64_t B = 0;
			std::memcpy(&A, &Left, sizeof(A));
			std::memcpy(&B, &Right, sizeof(B));
			std::int64_t R = 0;
			bool bOverflow = false;
			switch (Op)
			{
			case EOpcode::Add:
				{
					bOverflow = (B > 0 && A > std::numeric_limits<std::int64_t>::max() - B)
						|| (B < 0 && A < std::numeric_limits<std::int64_t>::min() - B);
					if (!bOverflow)
					{
						R = A + B;
					}
					break;
				}
			case EOpcode::Sub:
				{
					bOverflow = (B < 0 && A > std::numeric_limits<std::int64_t>::max() + B)
						|| (B > 0 && A < std::numeric_limits<std::int64_t>::min() + B);
					if (!bOverflow)
					{
						R = A - B;
					}
					break;
				}
			case EOpcode::Mul:
				{
					if (A != 0 && B != 0)
					{
						bOverflow = (A == -1 && B == std::numeric_limits<std::int64_t>::min())
							|| (B == -1 && A == std::numeric_limits<std::int64_t>::min())
							|| (A > 0 && B > 0 && A > std::numeric_limits<std::int64_t>::max() / B)
							|| (A > 0 && B < 0 && B < std::numeric_limits<std::int64_t>::min() / A)
							|| (A < 0 && B > 0 && A < std::numeric_limits<std::int64_t>::min() / B)
							|| (A < 0 && B < 0 && A < std::numeric_limits<std::int64_t>::max() / B);
					}
					if (!bOverflow)
					{
						R = A * B;
					}
					break;
				}
			case EOpcode::Div:
			case EOpcode::Mod:
				{
					if (B == 0)
					{
						return JitStatus(EJitRuntimeStatus::DivisionByZero);
					}
					if (A == std::numeric_limits<std::int64_t>::min() && B == -1)
					{
						return JitStatus(EJitRuntimeStatus::IntegerOverflow);
					}
					R = Op == EOpcode::Div ? A / B : A % B;
					break;
				}
			case EOpcode::BitAnd:
				{
					R = A & B;
					break;
				}
			case EOpcode::BitOr:
				{
					R = A | B;
					break;
				}
			case EOpcode::BitXor:
				{
					R = A ^ B;
					break;
				}

			case EOpcode::Shl:
			case EOpcode::Shr:
				{
					if (B < 0 || B > 63)
					{
						return JitStatus(EJitRuntimeStatus::ShiftOutOfRange);
					}
					R = Op == EOpcode::Shl
					? static_cast<std::int64_t>(static_cast<std::uint64_t>(A) << static_cast<unsigned>(B))
					: static_cast<std::int64_t>(A >> B);
					break;
				}
			case EOpcode::Eq:
				{
					*OutResult = PackBool(A == B);
					return 0;
				}
			case EOpcode::Ne:
				{
					*OutResult = PackBool(A != B);
					return 0;
				}
			case EOpcode::Lt:
				{
					*OutResult = PackBool(A < B);
					return 0;
				}
			case EOpcode::Le:
				{
					*OutResult = PackBool(A <= B);
					return 0;
				}
			case EOpcode::Gt:
				{
					*OutResult = PackBool(A > B);
					return 0;
				}
			case EOpcode::Ge:
				{
					*OutResult = PackBool(A >= B);
					return 0;
				}
			default:
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (bOverflow)
			{
				return JitStatus(EJitRuntimeStatus::IntegerOverflow);
			}
			*OutResult = static_cast<std::uint64_t>(R);
			return 0;
		}
		if (T == EMixTag::U64)
		{
			const std::uint64_t A = Left;
			const std::uint64_t B = Right;
			std::uint64_t R = 0;
			switch (Op)
			{
			case EOpcode::Add:
				{
					R = A + B;
					break;
				}
			case EOpcode::Sub:
				{
					R = A - B;
					break;
				}
			case EOpcode::Mul:
				{
					R = A * B;
					break;
				}
			case EOpcode::Div:
			case EOpcode::Mod:
				{
					if (B == 0)
					{
						return JitStatus(EJitRuntimeStatus::DivisionByZero);
					}
					R = Op == EOpcode::Div ? A / B : A % B;
					break;
				}
			case EOpcode::BitAnd:
				{
					R = A & B;
					break;
				}
			case EOpcode::BitOr:
				{
					R = A | B;
					break;
				}
			case EOpcode::BitXor:
				{
					R = A ^ B;
					break;
				}

			case EOpcode::Shl:
			case EOpcode::Shr:
				{
					if (B > 63)
					{
						return JitStatus(EJitRuntimeStatus::ShiftOutOfRange);
					}
					R = Op == EOpcode::Shl ? A << static_cast<unsigned>(B) : A >> static_cast<unsigned>(B);
					break;
				}
			case EOpcode::Eq:
				{
					*OutResult = PackBool(A == B);
					return 0;
				}
			case EOpcode::Ne:
				{
					*OutResult = PackBool(A != B);
					return 0;
				}
			case EOpcode::Lt:
				{
					*OutResult = PackBool(A < B);
					return 0;
				}
			case EOpcode::Le:
				{
					*OutResult = PackBool(A <= B);
					return 0;
				}
			case EOpcode::Gt:
				{
					*OutResult = PackBool(A > B);
					return 0;
				}
			case EOpcode::Ge:
				{
					*OutResult = PackBool(A >= B);
					return 0;
				}
			default:
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			*OutResult = R;
			return 0;
		}
		if (T == EMixTag::F32)
		{
			const float A = UnpackF32(Left);
			const float B = UnpackF32(Right);
			float R = 0.0f;
			switch (Op)
			{
			case EOpcode::Add:
				{
					R = A + B;
					break;
				}
			case EOpcode::Sub:
				{
					R = A - B;
					break;
				}
			case EOpcode::Mul:
				{
					R = A * B;
					break;
				}
			case EOpcode::Div:
				{
					if (B == 0.0f)
					{
						return JitStatus(EJitRuntimeStatus::DivisionByZero);
					}
					R = A / B;
					break;
				}
			case EOpcode::Eq:
				{
					*OutResult = PackBool(A == B);
					return 0;
				}
			case EOpcode::Ne:
				{
					*OutResult = PackBool(A != B);
					return 0;
				}
			case EOpcode::Lt:
				{
					*OutResult = PackBool(A < B);
					return 0;
				}
			case EOpcode::Le:
				{
					*OutResult = PackBool(A <= B);
					return 0;
				}
			case EOpcode::Gt:
				{
					*OutResult = PackBool(A > B);
					return 0;
				}
			case EOpcode::Ge:
				{
					*OutResult = PackBool(A >= B);
					return 0;
				}
			default:
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			*OutResult = PackF32(R);
			return 0;
		}
		if (T == EMixTag::F64)
		{
			const double A = UnpackF64(Left);
			const double B = UnpackF64(Right);
			double R = 0.0;
			switch (Op)
			{
			case EOpcode::Add:
				{
					R = A + B;
					break;
				}
			case EOpcode::Sub:
				{
					R = A - B;
					break;
				}
			case EOpcode::Mul:
				{
					R = A * B;
					break;
				}
			case EOpcode::Div:
				{
					if (B == 0.0)
					{
						return JitStatus(EJitRuntimeStatus::DivisionByZero);
					}
					R = A / B;
					break;
				}
			case EOpcode::Eq:
				{
					*OutResult = PackBool(A == B);
					return 0;
				}
			case EOpcode::Ne:
				{
					*OutResult = PackBool(A != B);
					return 0;
				}
			case EOpcode::Lt:
				{
					*OutResult = PackBool(A < B);
					return 0;
				}
			case EOpcode::Le:
				{
					*OutResult = PackBool(A <= B);
					return 0;
				}
			case EOpcode::Gt:
				{
					*OutResult = PackBool(A > B);
					return 0;
				}
			case EOpcode::Ge:
				{
					*OutResult = PackBool(A >= B);
					return 0;
				}
			default:
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			*OutResult = PackF64(R);
			return 0;
		}
		if (T == EMixTag::Bool)
		{
			const bool A = (Left & 1u) != 0;
			const bool B = (Right & 1u) != 0;
			if (Op == EOpcode::Eq)
			{
				*OutResult = PackBool(A == B);
				return 0;
			}
			if (Op == EOpcode::Ne)
			{
				*OutResult = PackBool(A != B);
				return 0;
			}
			return JitStatus(EJitRuntimeStatus::GenericFailure);
		}
		if (T == EMixTag::Enum)
		{
			const std::int64_t A = static_cast<std::int64_t>(Left);
			const std::int64_t B = static_cast<std::int64_t>(Right);
			if (Op == EOpcode::Eq)
			{
				*OutResult = PackBool(A == B);
				return 0;
			}
			if (Op == EOpcode::Ne)
			{
				*OutResult = PackBool(A != B);
				return 0;
			}
			return JitStatus(EJitRuntimeStatus::GenericFailure);
		}
		return JitStatus(EJitRuntimeStatus::GenericFailure);
	}

	std::int32_t VEROSCRIPT_JIT_CALL UnaryOpMixed(
		const std::uint8_t Opcode,
		const std::uint8_t Tag,
		const std::uint64_t Value,
		std::uint64_t* const OutResult)
	{
		if (!OutResult)
		{
			return JitStatus(EJitRuntimeStatus::GenericFailure);
		}
		const auto Op = static_cast<EOpcode>(Opcode);
		const auto T = static_cast<EMixTag>(Tag);
		if (Op == EOpcode::Not && T == EMixTag::Bool)
		{
			*OutResult = PackBool((Value & 1u) == 0);
			return 0;
		}
		if (Op == EOpcode::Neg)
		{
			if (T == EMixTag::I32)
			{
				const auto A = static_cast<std::int32_t>(Value);
				if (A == std::numeric_limits<std::int32_t>::min())
				{
					return JitStatus(EJitRuntimeStatus::IntegerOverflow);
				}
				*OutResult = PackI32(-A);
				return 0;
			}
			if (T == EMixTag::U32)
			{
				*OutResult = PackU32(0u - static_cast<std::uint32_t>(Value));
				return 0;
			}
			if (T == EMixTag::F64)
			{
				*OutResult = PackF64(-UnpackF64(Value));
				return 0;
			}
			if (T == EMixTag::F32)
			{
				*OutResult = PackF32(-UnpackF32(Value));
				return 0;
			}
			if (T == EMixTag::I64)
			{
				std::int64_t A = 0;
				std::memcpy(&A, &Value, sizeof(A));
				if (A == std::numeric_limits<std::int64_t>::min())
				{
					return JitStatus(EJitRuntimeStatus::IntegerOverflow);
				}
				*OutResult = static_cast<std::uint64_t>(-A);
				return 0;
			}
			if (T == EMixTag::U64)
			{
				*OutResult = std::uint64_t{0} - Value;
				return 0;
			}
		}
		if (Op == EOpcode::BitNot)
		{
			if (T == EMixTag::I32)
			{
				*OutResult = PackI32(~static_cast<std::int32_t>(Value));
				return 0;
			}
			if (T == EMixTag::U32)
			{
				*OutResult = PackU32(~static_cast<std::uint32_t>(Value));
				return 0;
			}
			if (T == EMixTag::I64 || T == EMixTag::U64)
			{
				*OutResult = ~Value;
				return 0;
			}
		}
		return JitStatus(EJitRuntimeStatus::GenericFailure);
	}

	std::int32_t VEROSCRIPT_JIT_CALL BuiltinMixed(
		void* const VmContext,
		const std::uint8_t BuiltinId,
		const std::uint8_t ArgTag,
		std::uint64_t* const SlotOrPair,
		const std::uint32_t ArgCount)
	{
		try
		{
			auto* const Vm = static_cast<FExecutionContext*>(VmContext);
			if (!SlotOrPair)
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			const auto Id = static_cast<EBuiltinId>(BuiltinId);
			const auto T = static_cast<EMixTag>(ArgTag);
			if (Id == EBuiltinId::Now || Id == EBuiltinId::SteadyNow || Id == EBuiltinId::ReadInt
				|| Id == EBuiltinId::ReadDouble || Id == EBuiltinId::ReadBool)
			{
				if (ArgCount != 0)
				{
					return JitStatus(EJitRuntimeStatus::GenericFailure);
				}
				bool bHasResult = false;
				FValue Result;
				if (!Vm->JitInvokeBuiltin(Id, nullptr, 0, bHasResult, Result)
					|| !bHasResult)
				{
					return JitStatus(EJitRuntimeStatus::GenericFailure);
				}
				if (Id == EBuiltinId::Now || Id == EBuiltinId::SteadyNow
					|| Id == EBuiltinId::ReadDouble)
				{
					const auto* const DoubleResult = std::get_if<double>(&Result);
					if (!DoubleResult)
					{
						return JitStatus(EJitRuntimeStatus::GenericFailure);
					}
					SlotOrPair[0] = PackF64(*DoubleResult);
					return 0;
				}
				if (Id == EBuiltinId::ReadInt)
				{
					const auto* const IntResult = std::get_if<std::int32_t>(&Result);
					if (!IntResult)
					{
						return JitStatus(EJitRuntimeStatus::GenericFailure);
					}
					SlotOrPair[0] = PackI32(*IntResult);
					return 0;
				}
				const auto* const BoolResult = std::get_if<bool>(&Result);
				if (!BoolResult)
				{
					return JitStatus(EJitRuntimeStatus::GenericFailure);
				}
				SlotOrPair[0] = PackI32(*BoolResult ? 1 : 0);
				return 0;
			}
			if (Id == EBuiltinId::Assert || Id == EBuiltinId::Sleep
				|| Id == EBuiltinId::Exit)
			{
				if (ArgCount != 1)
				{
					return JitStatus(EJitRuntimeStatus::GenericFailure);
				}
				FValue Argument;
				if (!ValueFromBits(T, SlotOrPair[0], Argument))
				{
					return JitStatus(EJitRuntimeStatus::GenericFailure);
				}
				bool bHasResult = false;
				FValue Result;
				if (!Vm->JitInvokeBuiltin(Id, &Argument, 1, bHasResult, Result)
					|| bHasResult)
				{
					return JitStatus(EJitRuntimeStatus::GenericFailure);
				}
				SlotOrPair[0] = PackI32(0);
				return 0;
			}
			if (Id == EBuiltinId::Print || Id == EBuiltinId::Eprint)
			{
				if (ArgCount != 1)
				{
					return JitStatus(EJitRuntimeStatus::GenericFailure);
				}
				FValue Argument;
				if (!ValueFromBits(T, SlotOrPair[0], Argument))
				{
					return JitStatus(EJitRuntimeStatus::GenericFailure);
				}
				bool bHasResult = false;
				FValue Result;
				if (!Vm->JitInvokeBuiltin(Id, &Argument, 1, bHasResult, Result) || bHasResult)
				{
					return JitStatus(EJitRuntimeStatus::GenericFailure);
				}
				SlotOrPair[0] = PackI32(0);
				return 0;
			}
			if (Id == EBuiltinId::Min || Id == EBuiltinId::Max)
			{
				if (ArgCount != 2)
				{
					return JitStatus(EJitRuntimeStatus::GenericFailure);
				}
				if (T == EMixTag::I32)
				{
					const auto A = static_cast<std::int32_t>(SlotOrPair[0]);
					const auto B = static_cast<std::int32_t>(SlotOrPair[1]);
					SlotOrPair[0] = PackI32(Id == EBuiltinId::Min ? (A < B ? A : B) : (A > B ? A : B));
					return 0;
				}
				if (T == EMixTag::U32)
				{
					const auto A = static_cast<std::uint32_t>(SlotOrPair[0]);
					const auto B = static_cast<std::uint32_t>(SlotOrPair[1]);
					SlotOrPair[0] = PackU32(Id == EBuiltinId::Min ? (A < B ? A : B) : (A > B ? A : B));
					return 0;
				}
				if (T == EMixTag::F64)
				{
					const double A = UnpackF64(SlotOrPair[0]);
					const double B = UnpackF64(SlotOrPair[1]);
					SlotOrPair[0] = PackF64(Id == EBuiltinId::Min ? (A < B ? A : B) : (A > B ? A : B));
					return 0;
				}
				if (T == EMixTag::F32)
				{
					const float A = UnpackF32(SlotOrPair[0]);
					const float B = UnpackF32(SlotOrPair[1]);
					SlotOrPair[0] = PackF32(Id == EBuiltinId::Min
						? (A < B ? A : B)
							: (A > B ? A : B));
					return 0;
				}
				if (T == EMixTag::I64)
				{
					const auto A = static_cast<std::int64_t>(SlotOrPair[0]);
					const auto B = static_cast<std::int64_t>(SlotOrPair[1]);
					SlotOrPair[0] = static_cast<std::uint64_t>(Id == EBuiltinId::Min
						? (A < B ? A : B)
							: (A > B ? A : B));
					return 0;
				}
				if (T == EMixTag::U64)
				{
					const auto A = SlotOrPair[0];
					const auto B = SlotOrPair[1];
					SlotOrPair[0] = Id == EBuiltinId::Min
						? (A < B ? A : B)
						: (A > B ? A : B);
					return 0;
				}
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (ArgCount != 1)
			{
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (Id == EBuiltinId::Abs)
			{
				if (T == EMixTag::I32)
				{
					const auto A = static_cast<std::int32_t>(SlotOrPair[0]);
					if (A == std::numeric_limits<std::int32_t>::min())
					{
						return JitStatus(EJitRuntimeStatus::IntegerOverflow);
					}
					SlotOrPair[0] = PackI32(A < 0 ? -A : A);
					return 0;
				}
				if (T == EMixTag::U32)
				{
					return 0;
				}
				if (T == EMixTag::F64)
				{
					const double V = UnpackF64(SlotOrPair[0]);
					SlotOrPair[0] = PackF64(std::fabs(V));
					return 0;
				}
				if (T == EMixTag::F32)
				{
					const float V = UnpackF32(SlotOrPair[0]);
					SlotOrPair[0] = PackF32(std::fabs(V));
					return 0;
				}
				if (T == EMixTag::I64)
				{
					const auto V = static_cast<std::int64_t>(SlotOrPair[0]);
					if (V == std::numeric_limits<std::int64_t>::min())
					{
						return JitStatus(EJitRuntimeStatus::IntegerOverflow);
					}
					SlotOrPair[0] = static_cast<std::uint64_t>(V < 0 ? -V : V);
					return 0;
				}
				if (T == EMixTag::U64)
				{
					return 0;
				}
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (Id == EBuiltinId::Sqrt || Id == EBuiltinId::Sin || Id == EBuiltinId::Cos
				|| Id == EBuiltinId::Tan || Id == EBuiltinId::Floor || Id == EBuiltinId::Ceil
				|| Id == EBuiltinId::Round || Id == EBuiltinId::Trunc || Id == EBuiltinId::Exp
				|| Id == EBuiltinId::Log || Id == EBuiltinId::Log2 || Id == EBuiltinId::Asin
				|| Id == EBuiltinId::Acos)
			{
				double V = 0.0;
				if (T == EMixTag::F64)
				{
					V = UnpackF64(SlotOrPair[0]);
				}
				else if (T == EMixTag::I32)
				{
					V = static_cast<double>(static_cast<std::int32_t>(SlotOrPair[0]));
				}
				else if (T == EMixTag::U32)
				{
					V = static_cast<double>(static_cast<std::uint32_t>(SlotOrPair[0]));
				}
				else if (T == EMixTag::F32)
				{
					V = static_cast<double>(UnpackF32(SlotOrPair[0]));
				}
				else if (T == EMixTag::I64)
				{
					V = static_cast<double>(static_cast<std::int64_t>(SlotOrPair[0]));
				}
				else if (T == EMixTag::U64)
				{
					V = static_cast<double>(SlotOrPair[0]);
				}
				else
				{
					return JitStatus(EJitRuntimeStatus::GenericFailure);
				}
				const std::int32_t St = MathUnaryF64(VmContext, BuiltinId, &V);
				if (St != 0)
				{
					return St;
				}
				SlotOrPair[0] = PackF64(V);
				return 0;
			}
			if (Id == EBuiltinId::ToInt || Id == EBuiltinId::ToInt8
				|| Id == EBuiltinId::ToInt16 || Id == EBuiltinId::ToDouble
				|| Id == EBuiltinId::ToFloat || Id == EBuiltinId::ToUnsigned
				|| Id == EBuiltinId::ToUInt8 || Id == EBuiltinId::ToUInt16
				|| Id == EBuiltinId::ToInt64 || Id == EBuiltinId::ToUInt64)
			{
				// Keep the two ubiquitous 32-bit checked conversions inside the raw
				// scalar bridge.  Calling the general builtin dispatcher would construct
				// an FValue and eagerly publish a VM diagnostic, preventing the enclosing
				// encoded native failure from attaching its exact (possibly inlined)
				// instruction offset.  Returning the typed status preserves both the VM
				// conversion semantics (finite, truncating, range checked) and the native
				// error-location protocol.
				if (Id == EBuiltinId::ToInt || Id == EBuiltinId::ToUnsigned)
				{
					long double Numeric = 0.0L;
					if (T == EMixTag::I32)
					{
						Numeric = static_cast<long double>(
								static_cast<std::int32_t>(SlotOrPair[0]));
					}
					else if (T == EMixTag::U32)
					{
						Numeric = static_cast<long double>(
								static_cast<std::uint32_t>(SlotOrPair[0]));
					}
					else if (T == EMixTag::I64)
					{
						Numeric = static_cast<long double>(
								static_cast<std::int64_t>(SlotOrPair[0]));
					}
					else if (T == EMixTag::U64)
					{
						Numeric = static_cast<long double>(SlotOrPair[0]);
					}
					else if (T == EMixTag::F32)
					{
						Numeric = static_cast<long double>(UnpackF32(SlotOrPair[0]));
					}
					else if (T == EMixTag::F64)
					{
						Numeric = static_cast<long double>(UnpackF64(SlotOrPair[0]));
					}
					else if (T == EMixTag::Bool)
					{
						Numeric = (SlotOrPair[0] & 1u) != 0u ? 1.0L : 0.0L;
					}
					else
					{
						return JitStatus(EJitRuntimeStatus::GenericFailure);
					}

					const long double Truncated = std::trunc(Numeric);
					if (!std::isfinite(Numeric))
					{
						return JitStatus(Id == EBuiltinId::ToInt
							? EJitRuntimeStatus::ToIntOverflow
							: EJitRuntimeStatus::ToUnsignedOverflow);
					}
					if (Id == EBuiltinId::ToInt)
					{
						if (Truncated < static_cast<long double>(
								std::numeric_limits<std::int32_t>::min())
							|| Truncated > static_cast<long double>(
								std::numeric_limits<std::int32_t>::max()))
						{
							return JitStatus(EJitRuntimeStatus::ToIntOverflow);
						}
						SlotOrPair[0] = PackI32(static_cast<std::int32_t>(Truncated));
						return 0;
					}
					if (Numeric < 0.0L || Truncated > static_cast<long double>(
							std::numeric_limits<std::uint32_t>::max()))
					{
						return JitStatus(EJitRuntimeStatus::ToUnsignedOverflow);
					}
					SlotOrPair[0] = PackU32(static_cast<std::uint32_t>(Truncated));
					return 0;
				}
				// Exact-width integer conversions are common at assignments and call
				// boundaries. Keep their checked scalar path out of FValue construction
				// and the general VM builtin dispatcher. The long-double route deliberately
				// mirrors TryNarrow{Signed,Unsigned}Integer, including truncation.
				if (Id == EBuiltinId::ToInt8 || Id == EBuiltinId::ToInt16
					|| Id == EBuiltinId::ToUInt8 || Id == EBuiltinId::ToUInt16)
				{
					long double Numeric = 0.0L;
					if (T == EMixTag::I32)
					{
						Numeric = static_cast<long double>(
							static_cast<std::int32_t>(SlotOrPair[0]));
					}
					else if (T == EMixTag::U32)
					{
						Numeric = static_cast<long double>(
							static_cast<std::uint32_t>(SlotOrPair[0]));
					}
					else if (T == EMixTag::I64)
					{
						Numeric = static_cast<long double>(
							static_cast<std::int64_t>(SlotOrPair[0]));
					}
					else if (T == EMixTag::U64)
					{
						Numeric = static_cast<long double>(SlotOrPair[0]);
					}
					else if (T == EMixTag::F32)
					{
						Numeric = static_cast<long double>(UnpackF32(SlotOrPair[0]));
					}
					else if (T == EMixTag::F64)
					{
						Numeric = static_cast<long double>(UnpackF64(SlotOrPair[0]));
					}
					else if (T == EMixTag::Bool)
					{
						Numeric = (SlotOrPair[0] & 1u) != 0 ? 1.0L : 0.0L;
					}
					else
					{
						return JitStatus(EJitRuntimeStatus::GenericFailure);
					}
					const bool bUnsigned = Id == EBuiltinId::ToUInt8
						|| Id == EBuiltinId::ToUInt16;
					const long double Truncated = std::trunc(Numeric);
					const long double Minimum = bUnsigned ? 0.0L
						: Id == EBuiltinId::ToInt8
							? static_cast<long double>(std::numeric_limits<std::int8_t>::min())
							: static_cast<long double>(std::numeric_limits<std::int16_t>::min());
					const long double Maximum = Id == EBuiltinId::ToInt8
						? static_cast<long double>(std::numeric_limits<std::int8_t>::max())
						: Id == EBuiltinId::ToUInt8
							? static_cast<long double>(std::numeric_limits<std::uint8_t>::max())
						: Id == EBuiltinId::ToInt16
							? static_cast<long double>(std::numeric_limits<std::int16_t>::max())
							: static_cast<long double>(std::numeric_limits<std::uint16_t>::max());
					if (!std::isfinite(Numeric) || (bUnsigned && Numeric < 0.0L)
						|| Truncated < Minimum || Truncated > Maximum)
					{
						Vm->JitReportRuntimeError(
							Id == EBuiltinId::ToInt8 ? "char conversion overflow"
							: Id == EBuiltinId::ToUInt8 ? "unsigned char conversion overflow"
							: Id == EBuiltinId::ToInt16 ? "short conversion overflow"
							: "unsigned short conversion overflow");
						return JitStatus(EJitRuntimeStatus::GenericFailure);
					}
					if (bUnsigned)
					{
						SlotOrPair[0] = PackU32(static_cast<std::uint32_t>(Truncated));
					}
					else
					{
						SlotOrPair[0] = PackI32(static_cast<std::int32_t>(Truncated));
					}
					return 0;
				}
				FValue Argument;
				if (!ValueFromBits(T, SlotOrPair[0], Argument))
				{
					return JitStatus(EJitRuntimeStatus::GenericFailure);
				}
				bool bHasResult = false;
				FValue Result;
				if (!Vm->JitInvokeBuiltin(Id, &Argument, 1, bHasResult, Result)
					|| !bHasResult)
				{
					return JitStatus(EJitRuntimeStatus::GenericFailure);
				}
				if (Id == EBuiltinId::ToInt8)
				{
					const auto* const Value = std::get_if<std::int8_t>(&Result);
					if (!Value)
					{
						return JitStatus(EJitRuntimeStatus::GenericFailure);
					}
					SlotOrPair[0] = PackI32(static_cast<std::int32_t>(*Value));
					return 0;
				}
				if (Id == EBuiltinId::ToInt16)
				{
					const auto* const Value = std::get_if<std::int16_t>(&Result);
					if (!Value)
					{
						return JitStatus(EJitRuntimeStatus::GenericFailure);
					}
					SlotOrPair[0] = PackI32(static_cast<std::int32_t>(*Value));
					return 0;
				}
				if (Id == EBuiltinId::ToUInt8)
				{
					const auto* const Value = std::get_if<std::uint8_t>(&Result);
					if (!Value)
					{
						return JitStatus(EJitRuntimeStatus::GenericFailure);
					}
					SlotOrPair[0] = PackU32(static_cast<std::uint32_t>(*Value));
					return 0;
				}
				if (Id == EBuiltinId::ToUInt16)
				{
					const auto* const Value = std::get_if<std::uint16_t>(&Result);
					if (!Value)
					{
						return JitStatus(EJitRuntimeStatus::GenericFailure);
					}
					SlotOrPair[0] = PackU32(static_cast<std::uint32_t>(*Value));
					return 0;
				}
				const EMixTag ResultTag = Id == EBuiltinId::ToInt ? EMixTag::I32
					: Id == EBuiltinId::ToUnsigned ? EMixTag::U32
					: Id == EBuiltinId::ToInt64 ? EMixTag::I64
					: Id == EBuiltinId::ToUInt64 ? EMixTag::U64
					: Id == EBuiltinId::ToFloat ? EMixTag::F32
					: EMixTag::F64;
				return BitsFromValue(ResultTag, Result, SlotOrPair[0])
					? JitStatus(EJitRuntimeStatus::Ok)
					: JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (Id == EBuiltinId::ToInt)
			{
				if (T == EMixTag::I32)
				{
					return 0;
				}
				if (T == EMixTag::U32)
				{
					const auto U = static_cast<std::uint32_t>(SlotOrPair[0]);
					if (U > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
					{
						Vm->JitReportRuntimeError("to_int overflow");
						return JitStatus(EJitRuntimeStatus::GenericFailure);
					}
					SlotOrPair[0] = PackI32(static_cast<std::int32_t>(U));
					return 0;
				}
				if (T == EMixTag::F64)
				{
					const double D = UnpackF64(SlotOrPair[0]);
					if (!std::isfinite(D) || D >= 2147483648.0 || D <= -2147483649.0)
					{
						Vm->JitReportRuntimeError("to_int overflow");
						return JitStatus(EJitRuntimeStatus::GenericFailure);
					}
					SlotOrPair[0] = PackI32(static_cast<std::int32_t>(D));
					return 0;
				}
				if (T == EMixTag::Bool)
				{
					SlotOrPair[0] = PackI32((SlotOrPair[0] & 1u) != 0 ? 1 : 0);
					return 0;
				}
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (Id == EBuiltinId::ToDouble)
			{
				if (T == EMixTag::F64)
				{
					return 0;
				}
				if (T == EMixTag::I32)
				{
					SlotOrPair[0] = PackF64(static_cast<double>(static_cast<std::int32_t>(SlotOrPair[0])));
					return 0;
				}
				if (T == EMixTag::U32)
				{
					SlotOrPair[0] = PackF64(static_cast<double>(static_cast<std::uint32_t>(SlotOrPair[0])));
					return 0;
				}
				if (T == EMixTag::Bool)
				{
					SlotOrPair[0] = PackF64((SlotOrPair[0] & 1u) != 0 ? 1.0 : 0.0);
					return 0;
				}
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			if (Id == EBuiltinId::ToUnsigned)
			{
				if (T == EMixTag::U32)
				{
					return 0;
				}
				if (T == EMixTag::I32)
				{
					const auto A = static_cast<std::int32_t>(SlotOrPair[0]);
					if (A < 0)
					{
						Vm->JitReportRuntimeError("to_unsigned overflow");
						return JitStatus(EJitRuntimeStatus::GenericFailure);
					}
					SlotOrPair[0] = PackU32(static_cast<std::uint32_t>(A));
					return 0;
				}
				if (T == EMixTag::F64)
				{
					const double D = UnpackF64(SlotOrPair[0]);
					if (!std::isfinite(D) || D < 0.0 || D >= 4294967296.0)
					{
						Vm->JitReportRuntimeError("to_unsigned overflow");
						return JitStatus(EJitRuntimeStatus::GenericFailure);
					}
					SlotOrPair[0] = PackU32(static_cast<std::uint32_t>(D));
					return 0;
				}
				if (T == EMixTag::Bool)
				{
					SlotOrPair[0] = PackU32((SlotOrPair[0] & 1u) != 0 ? 1u : 0u);
					return 0;
				}
				return JitStatus(EJitRuntimeStatus::GenericFailure);
			}
			return JitStatus(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (VmContext)
			{
				static_cast<FExecutionContext *>(VmContext)->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}
}
