// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"
#include "VeroScriptValue.h"

#include <cstddef>
#include <cstdint>

namespace VeroScript
{
	inline constexpr std::uint64_t JitScalarServiceCapability = 1u << 3u;

	enum class EJitScalarSetOperation : std::uint8_t
	{
		Add,
		Has,
		Remove,
	};

	enum class EJitFieldAccumulateFailure : std::uint8_t
	{
		None,
		FieldIncrement,
		AccumulatorAddition,
	};

	enum class EJitScalarMapUpdateFailure : std::uint8_t
	{
		None,
		MissingKey,
		IntegerOverflow,
		DivisionByZero,
		ShiftOutOfRange,
		Operation,
	};

	/**
	 * Optional fused operations over core-owned language values. A probe returns
	 * true with OutHandled=false when its representation does not apply; false
	 * reports a language failure. Local operations require this invocation's frame.
	 * Output values are initialized temporary operands; release an existing owned
	 * output before reusing it. Container references are ordinary borrowed operands.
	 * Scalar storage may be enabled only for verifier-proven non-escaping values.
	 * Borrowed storage remains valid across other read-only borrows, and expires
	 * before any allocation, call, mutation notification, binding or frame change.
	 * Raw field writes must commit each observable mutation; commit ends the borrow.
	 */
	class IJitScalarServices
	{
	public:
		virtual ~IJitScalarServices() = default;

		virtual bool AddLocalImm(std::uint16_t Slot, std::int32_t Imm) noexcept = 0;

		virtual bool AddLocalLocal(std::uint16_t DestSlot, std::uint16_t SrcSlot) noexcept = 0;

		virtual bool AddValueToLocal(std::uint16_t DestSlot, const FValue& Source) noexcept = 0;

		virtual bool BindLocal(std::uint16_t Slot, FValue Value) noexcept = 0;

		virtual FValue* BorrowDirectLocal(const std::uint16_t Slot) noexcept = 0;

		virtual bool BorrowLocalF32Field(
			std::uint16_t Slot,
			std::uint16_t NameIndex,
			FScriptObject*& OutObject,
			std::uint16_t& OutFieldIndex,
			float*& OutStorage) noexcept = 0;

		virtual bool BorrowLocalF64Field(
			std::uint16_t Slot,
			std::uint16_t NameIndex,
			FScriptObject*& OutObject,
			std::uint16_t& OutFieldIndex,
			double*& OutStorage) noexcept = 0;

		virtual bool BorrowLocalI32Field(
			std::uint16_t Slot,
			std::uint16_t NameIndex,
			FScriptObject*& OutObject,
			std::uint16_t& OutFieldIndex,
			std::int32_t*& OutStorage) noexcept = 0;

		virtual bool BorrowLocalScalarField(
			std::uint16_t Slot,
			std::uint16_t NameIndex,
			FScriptObject*& OutObject,
			std::uint16_t& OutFieldIndex,
			const FValue*& OutStorage) noexcept = 0;

		virtual bool BorrowLocalU32Field(
			std::uint16_t Slot,
			std::uint16_t NameIndex,
			FScriptObject*& OutObject,
			std::uint16_t& OutFieldIndex,
			std::uint32_t*& OutStorage) noexcept = 0;

		virtual bool CanUseScalarNativeCalls(
			const std::uint32_t* EncodedGuards,
			std::size_t GuardCount
		) noexcept = 0;

		virtual void CommitBorrowedFieldMutation(
			FScriptObject& Object,
			const std::uint16_t FieldIndex) noexcept = 0;

		virtual bool CompareLocals(
			EOpcode Op,
			std::uint16_t LeftSlot,
			std::uint16_t RightSlot,
			bool& bOutResult) noexcept = 0;

		virtual bool EnableScalarArrayStorage(FValue& Container) noexcept = 0;

		virtual bool EnableScalarMapStorage(FValue& Container) noexcept = 0;

		virtual bool EnableScalarSetStorage(FValue& Container) noexcept = 0;

		virtual bool EvaluateCondition(const FValue& Value, bool& bOutCondition) noexcept = 0;

		virtual bool HandleLinearThrow(
			FValue Value,
			std::size_t InstructionOffset,
			std::size_t& OutHandlerOffset) noexcept = 0;

		virtual bool LoadLocalField(
			std::uint16_t Slot,
			std::uint16_t NameIndex,
			FValue& OutValue) noexcept = 0;

		virtual bool LocalGeImm(std::uint16_t Slot, std::int32_t Immediate, bool& bOutResult) noexcept = 0;

		virtual bool LocalGeLocal(
			std::uint16_t LeftSlot,
			std::uint16_t RightSlot,
			bool& bOutResult) noexcept = 0;

		virtual bool TryAddLocalFromLocalField(
			std::uint16_t DestSlot,
			std::uint16_t ObjectSlot,
			std::uint16_t NameIndex,
			std::uint32_t CallSiteOffset,
			bool& OutHandled) noexcept = 0;

		virtual bool TryAddLocalFromScalarArray(
			std::uint16_t DestSlot,
			std::uint16_t ArraySlot,
			const FValue& Index,
			bool& OutHandled) noexcept = 0;

		virtual bool TryAddLocalFromScalarMap(
			std::uint16_t DestSlot,
			std::uint16_t MapSlot,
			const FValue& Key,
			bool& OutHandled) noexcept = 0;

		virtual bool TryAddLocalFromScalarMapModuloI32(
			std::uint16_t DestSlot,
			std::uint16_t MapSlot,
			const FValue& Key,
			std::int32_t Modulus,
			bool& OutHandled) noexcept = 0;

		virtual bool TryCallScalarContainerMethodOnLocal(
			std::uint16_t Slot,
			std::uint16_t MethodNameIndex,
			const FValue* Arguments,
			std::uint8_t ArgumentCount,
			bool bDiscardReturn,
			FValue& OutValue,
			bool& OutHandled) noexcept = 0;

		virtual bool TryCallScalarContainerMethodOnLocals(
			std::uint16_t Slot,
			std::uint16_t MethodNameIndex,
			std::uint16_t ArgumentSlot,
			bool bDiscardReturn,
			FValue& OutValue,
			bool& OutHandled) noexcept = 0;

		virtual bool TryCallScalarSetMethodOnLocal(
			std::uint16_t Slot,
			const FValue& Argument,
			EJitScalarSetOperation Operation,
			bool bDiscardReturn,
			FValue& OutValue,
			bool& OutHandled) noexcept = 0;

		virtual bool TryCallScalarSetMethodOnLocals(
			std::uint16_t Slot,
			std::uint16_t ArgumentSlot,
			EJitScalarSetOperation Operation,
			bool bDiscardReturn,
			FValue& OutValue,
			bool& OutHandled) noexcept = 0;

		virtual bool TryCheckedScalarCast(
			const FValue& Value,
			std::uint16_t TargetTypeNameIndex,
			FValue& OutResult,
			bool& OutHandled) noexcept = 0;

		virtual bool TryCompareStringArrayElements(
			std::uint16_t LeftArraySlot,
			std::uint16_t LeftIndexSlot,
			std::uint16_t RightArraySlot,
			std::uint16_t RightIndexSlot,
			bool bEqual,
			bool& OutResult,
			bool& OutHandled) noexcept = 0;

		virtual bool TryGetDynamicArrayScalar(
			const FValue& Container,
			std::int64_t Index,
			FValue& OutValue,
			bool& bOutHandled) noexcept = 0;

		virtual bool TryGetScalarIndexOnLocal(
			std::uint16_t Slot,
			const FValue& Index,
			FValue& OutValue,
			bool& bOutMapHandled,
			bool& bOutArrayHandled) noexcept = 0;

		virtual bool TryGetScalarMap(
			const FValue& Container,
			const FValue& Index,
			FValue& OutValue,
			bool& bOutHandled) noexcept = 0;

		virtual bool TryIncAndAccumulateLocalI32Field(
			std::uint16_t ObjectSlot,
			std::uint16_t NameIndex,
			std::uint16_t AccumulatorSlot,
			bool& OutHandled,
			EJitFieldAccumulateFailure& OutFailure) noexcept = 0;

		virtual bool TryIncLocalField(
			std::uint16_t Slot,
			std::uint16_t NameIndex,
			bool& OutHandled) noexcept = 0;

		virtual bool TryInsertI32SetRangeOnLocal(
			std::uint16_t Slot,
			std::int32_t Begin,
			std::int32_t End,
			bool& OutHandled) noexcept = 0;

		virtual bool TryLoadScalarGlobal(
			std::uint16_t Slot,
			FValue& OutValue,
			bool& OutHandled) noexcept = 0;

		virtual bool TryLoadScalarLocal(
			std::uint16_t Slot,
			FValue& OutValue,
			bool& OutHandled) noexcept = 0;

		virtual bool TryPushDynamicArrayScalar(
			const FValue& Container,
			const FValue& Value,
			bool& bOutHandled) noexcept = 0;

		virtual bool TryPushDynamicArrayScalarOnLocal(
			std::uint16_t Slot,
			const FValue& Value,
			bool& bOutHandled) noexcept = 0;

		virtual bool TryScalarBuiltinConversion(
			EBuiltinId Builtin,
			const FValue& Value,
			FValue& OutResult,
			bool& OutHandled) noexcept = 0;

		virtual bool TrySetDynamicArrayScalar(
			FValue& Container,
			std::int64_t Index,
			const FValue& Value,
			bool& bOutHandled) noexcept = 0;

		virtual bool TrySetDynamicArrayScalarOnLocal(
			std::uint16_t ArraySlot,
			const FValue& Index,
			const FValue& Value,
			bool& bOutHandled) noexcept = 0;

		virtual bool TrySetI32MapAffineRangeOnLocal(
			std::uint16_t MapSlot,
			std::int32_t Begin,
			std::int32_t End,
			std::int32_t Immediate,
			bool& OutHandled) noexcept = 0;

		virtual bool TrySetScalarMap(
			FValue& Container,
			const FValue& Index,
			const FValue& Value,
			bool& bOutHandled) noexcept = 0;

		virtual bool TrySetScalarMapDirectFromLocals(
			std::uint16_t MapSlot,
			std::uint16_t KeySlot,
			std::uint16_t SourceSlot,
			bool& bOutHandled) noexcept = 0;

		virtual bool TrySetScalarMapDirectOnLocal(
			std::uint16_t MapSlot,
			const FValue& Key,
			const FValue& Value,
			bool& bOutHandled) noexcept = 0;

		virtual bool TrySetScalarMapI32AddOnLocals(
			std::uint16_t MapSlot,
			std::uint16_t KeySlot,
			std::uint16_t ValueSlot,
			std::int32_t Immediate,
			bool& bOutHandled) noexcept = 0;

		virtual bool TryUpdateLocalFieldI32Modulo(
			std::uint16_t Slot,
			std::uint16_t NameIndex,
			const FValue& Delta,
			std::int32_t Modulus,
			std::uint32_t CallSiteOffset,
			bool& OutHandled) noexcept = 0;

		virtual bool TryUpdateScalarMapEntryOnLocal(
			std::uint16_t MapSlot,
			const FValue& Key,
			EOpcode Operation,
			const FValue& Operand,
			bool& OutHandled,
			EJitScalarMapUpdateFailure& OutFailure) noexcept = 0;

	};
}
