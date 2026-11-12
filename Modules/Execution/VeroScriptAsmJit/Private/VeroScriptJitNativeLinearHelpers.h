// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJitAbi.h"

#include <cstdint>

namespace VeroScript::AsmJit::JitNative::LinearHelpers
{
	/**
	 * Stable internal tags for the portable 64-bit scalar slot ABI. Keep these
	 * shared by emitters and boxed-to-raw dynamic call sites.
	 */
	enum class EMixedScalarTag : std::uint8_t
	{
		I32 = 0,
		U32 = 1,
		F64 = 2,
		Bool = 3,
		I64 = 4,
		U64 = 5,
		F32 = 6,
		Enum = 8,
		I8 = 9,
		U8 = 10,
		I16 = 11,
		U16 = 12,
	};

	// Values returned by these kernels use EJitRuntimeStatus's stable raw-kernel encoding. Generic
	// failures mean the VM/helper has already produced the rich diagnostic.
	std::int32_t VEROSCRIPT_JIT_CALL InvokeCall(
		void* VmContext,
		std::uint16_t FunctionIndex,
		std::int32_t* Args,
		std::uint32_t ArgCount,
		std::int32_t* OutResult);

	std::int32_t VEROSCRIPT_JIT_CALL InvokeCallU32(
		void* VmContext,
		std::uint16_t FunctionIndex,
		std::uint32_t* Args,
		std::uint32_t ArgCount,
		std::uint32_t* OutResult);

	std::int32_t VEROSCRIPT_JIT_CALL EnterDirectScalarRecursion(
		void* VmContext,
		std::uint32_t ScalarKind);

	std::int32_t VEROSCRIPT_JIT_CALL InvokeCallF64(
		void* VmContext,
		std::uint16_t FunctionIndex,
		double* Args,
		std::uint32_t ArgCount,
		double* OutResult);

	/**
	 * Mixed scalar call: Args are 8-byte payloads (i32/u32/bool in low bits, or IEEE f64 bits).
	 * OutResult receives the same encoding. Arg types come from the callee signature.
	 */
	std::int32_t VEROSCRIPT_JIT_CALL InvokeCallMixed(
		void* VmContext,
		std::uint16_t FunctionIndex,
		std::uint64_t* Args,
		std::uint32_t ArgCount,
		std::uint64_t* OutResult);

	// Fused `LoadThis; CallBoundMethod` — uses GetJitThisObject(), no object on native stack.
	std::int32_t VEROSCRIPT_JIT_CALL InvokeBoundMethodFromThisI32(
		void* VmContext,
		std::uint16_t FunctionIndex,
		std::int32_t* Args,
		std::uint32_t ArgCount,
		std::int32_t* OutResult);
	std::int32_t VEROSCRIPT_JIT_CALL InvokeBoundMethodFromThisU32(
		void* VmContext,
		std::uint16_t FunctionIndex,
		std::uint32_t* Args,
		std::uint32_t ArgCount,
		std::uint32_t* OutResult);
	std::int32_t VEROSCRIPT_JIT_CALL InvokeBoundMethodFromThisF64(
		void* VmContext,
		std::uint16_t FunctionIndex,
		double* Args,
		std::uint32_t ArgCount,
		double* OutResult);
	std::int32_t VEROSCRIPT_JIT_CALL InvokeBoundMethodFromThisMixed(
		void* VmContext,
		std::uint16_t FunctionIndex,
		std::uint64_t* Args,
		std::uint32_t ArgCount,
		std::uint64_t* OutResult);

	// Fused `LoadThis; CallMethod` — dynamic dispatch on GetJitThisObject().
	std::int32_t VEROSCRIPT_JIT_CALL CallMethodFromThisI32(
		void* VmContext,
		std::uint16_t NameIndex,
		std::int32_t* Args,
		std::uint32_t ArgCount,
		std::int32_t* OutResult);
	std::int32_t VEROSCRIPT_JIT_CALL CallMethodFromThisU32(
		void* VmContext,
		std::uint16_t NameIndex,
		std::uint32_t* Args,
		std::uint32_t ArgCount,
		std::uint32_t* OutResult);
	std::int32_t VEROSCRIPT_JIT_CALL CallMethodFromThisF64(
		void* VmContext,
		std::uint16_t NameIndex,
		double* Args,
		std::uint32_t ArgCount,
		double* OutResult);
	std::int32_t VEROSCRIPT_JIT_CALL CallMethodFromThisMixed(
		void* VmContext,
		std::uint16_t NameIndex,
		std::uint64_t* Args,
		std::uint32_t ArgCount,
		std::uint64_t* OutResult);

	std::int32_t VEROSCRIPT_JIT_CALL LoadGlobalI32(void* VmContext, std::uint16_t Slot, std::int32_t* OutResult);
	std::int32_t VEROSCRIPT_JIT_CALL StoreGlobalI32(void* VmContext, std::uint16_t Slot, std::int32_t Value);

	std::int32_t VEROSCRIPT_JIT_CALL LoadGlobalU32(void* VmContext, std::uint16_t Slot, std::uint32_t* OutResult);
	std::int32_t VEROSCRIPT_JIT_CALL StoreGlobalU32(void* VmContext, std::uint16_t Slot, std::uint32_t Value);

	std::int32_t VEROSCRIPT_JIT_CALL LoadThisFieldI32(void* VmContext, std::uint16_t NameIndex, std::int32_t* OutResult);
	std::int32_t VEROSCRIPT_JIT_CALL StoreThisFieldI32(void* VmContext, std::uint16_t NameIndex, std::int32_t Value);

	std::int32_t VEROSCRIPT_JIT_CALL InitializeThisFieldI32(void* VmContext, std::uint16_t NameIndex, std::int32_t Value);
	// Peephole `LoadThis; IncField` — increments typed this-field without an object stack slot.
	std::int32_t VEROSCRIPT_JIT_CALL IncThisFieldI32(void* VmContext, std::uint16_t NameIndex);
	std::int32_t VEROSCRIPT_JIT_CALL LoadThisFieldU32(void* VmContext, std::uint16_t NameIndex, std::uint32_t* OutResult);
	std::int32_t VEROSCRIPT_JIT_CALL StoreThisFieldU32(void* VmContext, std::uint16_t NameIndex, std::uint32_t Value);

	std::int32_t VEROSCRIPT_JIT_CALL InitializeThisFieldU32(void* VmContext, std::uint16_t NameIndex, std::uint32_t Value);
	std::int32_t VEROSCRIPT_JIT_CALL IncThisFieldU32(void* VmContext, std::uint16_t NameIndex);

	std::int32_t VEROSCRIPT_JIT_CALL LoadThisFieldF64(void* VmContext, std::uint16_t NameIndex, double* OutResult);
	std::int32_t VEROSCRIPT_JIT_CALL StoreThisFieldF64(void* VmContext, std::uint16_t NameIndex, double Value);

	std::int32_t VEROSCRIPT_JIT_CALL InitializeThisFieldF64(void* VmContext, std::uint16_t NameIndex, double Value);
	std::int32_t VEROSCRIPT_JIT_CALL IncThisFieldF64(void* VmContext, std::uint16_t NameIndex);

	std::int32_t VEROSCRIPT_JIT_CALL LoadGlobalF64(void* VmContext, std::uint16_t Slot, double* OutResult);
	std::int32_t VEROSCRIPT_JIT_CALL StoreGlobalF64(void* VmContext, std::uint16_t Slot, double Value);

	std::int32_t VEROSCRIPT_JIT_CALL LoadGlobalMixed(void* VmContext, std::uint16_t Slot, std::uint64_t* OutResult);
	std::int32_t VEROSCRIPT_JIT_CALL StoreGlobalMixed(void* VmContext, std::uint16_t Slot, std::uint64_t Value);

	std::int32_t VEROSCRIPT_JIT_CALL LoadThisFieldMixed(
		void* VmContext,
		std::uint16_t NameIndex,
		std::uint8_t Tag,
		std::uint64_t* OutResult);
	std::int32_t VEROSCRIPT_JIT_CALL StoreThisFieldMixed(
		void* VmContext,
		std::uint16_t NameIndex,
		std::uint8_t Tag,
		std::uint64_t Value);

	std::int32_t VEROSCRIPT_JIT_CALL InitializeThisFieldMixed(
		void* VmContext,
		std::uint16_t NameIndex,
		std::uint8_t Tag,
		std::uint64_t Value);
	std::int32_t VEROSCRIPT_JIT_CALL IncThisFieldMixed(void* VmContext, std::uint16_t NameIndex);

	// In-place unary math on *Slot. BuiltinId is EBuiltinId.
	std::int32_t VEROSCRIPT_JIT_CALL MathUnaryF64(void* VmContext, std::uint8_t BuiltinId, double* Slot);
	// Zero-argument clock builtins.  Keep time reads on the homogeneous F64
	// NativeCfg path: routing them through the boxed builtin bridge would force a
	// whole otherwise-scalar function onto the broader mixed executor.
	std::int32_t VEROSCRIPT_JIT_CALL ClockNowF64(void* VmContext, std::uint8_t BuiltinId, double* OutResult);
	// Output remains observable, but a scalar double argument does not require the
	// whole caller to widen to the mixed scalar executor.
	std::int32_t VEROSCRIPT_JIT_CALL OutputF64Builtin(void* VmContext, std::uint8_t BuiltinId, double Value);

	// IEEE relational compares as 1/0 (NaN ⇒ false except Ne ⇒ true).
	std::int32_t VEROSCRIPT_JIT_CALL CmpEqF64(double Left, double Right);
	std::int32_t VEROSCRIPT_JIT_CALL CmpNeF64(double Left, double Right);
	std::int32_t VEROSCRIPT_JIT_CALL CmpLtF64(double Left, double Right);
	std::int32_t VEROSCRIPT_JIT_CALL CmpLtF32(float Left, float Right);
	std::int32_t VEROSCRIPT_JIT_CALL CmpLeF64(double Left, double Right);
	std::int32_t VEROSCRIPT_JIT_CALL CmpGtF64(double Left, double Right);
	std::int32_t VEROSCRIPT_JIT_CALL CmpGeF64(double Left, double Right);

	// Tag is EMixTag's stable raw value. Return values use the shared
	// EJitRuntimeStatus encoding described above.
	std::int32_t VEROSCRIPT_JIT_CALL BinOpMixed(
		std::uint8_t Opcode,
		std::uint8_t Tag,
		std::uint64_t Left,
		std::uint64_t Right,
		std::uint64_t* OutResult);

	std::int32_t VEROSCRIPT_JIT_CALL UnaryOpMixed(
		std::uint8_t Opcode,
		std::uint8_t Tag,
		std::uint64_t Value,
		std::uint64_t* OutResult);

	std::int32_t VEROSCRIPT_JIT_CALL BuiltinMixed(
		void* VmContext,
		std::uint8_t BuiltinId,
		std::uint8_t ArgTag,
		std::uint64_t* SlotOrPair,
		std::uint32_t ArgCount);
}
