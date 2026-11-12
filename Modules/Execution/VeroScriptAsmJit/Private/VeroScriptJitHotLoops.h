// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptValue.h"

#include <cstddef>
#include <cstdint>

namespace VeroScript::AsmJit::JitHotLoops
{
	enum class EAffineI32UpdateFailure : std::uint8_t
	{
		None,
		StateMultiplyOverflow,
		StateAddOverflow,
		StateDivisionByZero,
		StateDivisionOverflow,
		TermDivisionByZero,
		TermDivisionOverflow,
		IndexDivisionByZero,
		IndexDivisionOverflow,
		IndexOutOfBounds,
		ElementDeltaDivisionByZero,
		ElementDeltaDivisionOverflow,
		ElementDeltaAddOverflow,
		ElementAddOverflow,
		ElementPostAddOverflow,
		ElementDivisionByZero,
		ElementDivisionOverflow,
		AccumulatorAddOverflow,
		AccumulatorDivisionByZero,
		AccumulatorDivisionOverflow,
	};

	/**
	 * Typed source for the value added to a container element on each iteration.
	 * Constant preserves the ordinary `element += c` form. StateModuloAdd models
	 * the general checked expression `element += (state % modulus) + addend`.
	 * The latter deliberately carries its own failure sites so a fused loop can
	 * reproduce interpreter diagnostics and partial writes exactly.
	 */
	enum class EI32ElementDeltaKind : std::uint8_t
	{
		Constant,
		StateModuloAdd,
		// Source order: element + (state % modulus), then + addend.
		StateModuloElementThenAdd,
	};

	enum class EAffineF64ArrayFailure : std::uint8_t
	{
		None,
		StateMultiplyOverflow,
		StateAddOverflow,
		StateDivisionByZero,
		StateDivisionOverflow,
		IndexDivisionByZero,
		IndexDivisionOverflow,
		IndexOutOfBounds,
	};

	struct FAffineU32ArrayResult
	{
		std::uint32_t State = 0;
		std::uint32_t Accumulator = 0;
		std::size_t CompletedIterations = 0;
	};

	/**
	 * Execute a proven unsigned affine recurrence and masked typed-array read.
	 * Returns false at the first out-of-bounds access; Result then contains the
	 * failing recurrence state and the number of preceding successful iterations.
	 */
	bool AccumulateAffineU32ArrayMasked(
		const std::uint32_t* Values,
		std::size_t ValueCount,
		std::size_t IterationCount,
		std::uint32_t Multiplier,
		std::uint32_t Increment,
		std::uint32_t Mask,
		std::uint32_t InitialState,
		std::uint32_t InitialAccumulator,
		FAffineU32ArrayResult& OutResult);

	// Same recurrence as above, storing each generated state at its masked index.
	bool WriteAffineU32ArrayMasked(
		std::uint32_t* Values,
		std::size_t ValueCount,
		std::size_t IterationCount,
		std::uint32_t Multiplier,
		std::uint32_t Increment,
		std::uint32_t Mask,
		std::uint32_t InitialState,
		FAffineU32ArrayResult& OutResult);

	struct FI32ElementDelta
	{
		EI32ElementDeltaKind Kind = EI32ElementDeltaKind::Constant;
		std::int32_t Constant = 0;
		std::int32_t Modulus = 1;
		std::int32_t Addend = 0;
	};

	struct FAffineI32UpdateResult
	{
		std::int32_t State = 0;
		std::int32_t Accumulator = 0;
		std::int32_t LastElement = 0;
		std::size_t CompletedIterations = 0;
		EAffineI32UpdateFailure Failure = EAffineI32UpdateFailure::None;
		std::int32_t LastElementDelta = 0;
		bool bElementDeltaStored = false;
		std::int32_t LastIndex = 0;
		bool bElementStored = false;
	};

	/**
	 * Typed loop IR for the common general random-access form:
	 *   state = (state * multiplier + increment) % stateModulus;
	 *   values[state] = values[state] + elementIncrement;
	 *   total = (total + values[state]) % accumulatorModulus;
	 * Every checked operation is committed in source order. On failure the result
	 * identifies the exact semantic operation and preserves all preceding writes.
	 */
	bool UpdateAffineI32ArrayModulo(
		std::int32_t* Values,
		std::size_t ValueCount,
		std::size_t IterationCount,
		std::int32_t Multiplier,
		std::int32_t Increment,
		std::int32_t StateModulus,
		FI32ElementDelta ElementDelta,
		std::int32_t AccumulatorModulus,
		std::int32_t InitialState,
		std::int32_t InitialAccumulator,
		FAffineI32UpdateResult& OutResult);

	bool UpdateAffineI32MapModulo(
		FMap::FIntI32Entries& Entries,
		std::size_t IterationCount,
		std::int32_t Multiplier,
		std::int32_t Increment,
		std::int32_t StateModulus,
		FI32ElementDelta ElementDelta,
		std::int32_t AccumulatorModulus,
		std::int32_t InitialState,
		std::int32_t InitialAccumulator,
		FAffineI32UpdateResult& OutResult);

	/**
	 * General indexed form of the typed update loop. The source index is
	 * state % IndexModulus and a zero modulus remains a runtime error.
	 * The stored value is sum % ElementModulus; a zero modulus is an error. These
	 * are separate semantic operations so callers can
	 * map every failure back to the original bytecode instruction.
	 */
	bool UpdateAffineI32IndexedArrayModulo(
		std::int32_t* Values,
		std::size_t ValueCount,
		std::size_t IterationCount,
		std::int32_t Multiplier,
		std::int32_t Increment,
		std::int32_t StateModulus,
		std::int32_t IndexModulus,
		FI32ElementDelta ElementDelta,
		std::int32_t ElementModulus,
		std::int32_t AccumulatorModulus,
		std::int32_t InitialState,
		std::int32_t InitialAccumulator,
		FAffineI32UpdateResult& OutResult);

	bool UpdateAffineI32IndexedMapModulo(
		FMap::FIntI32Entries& Entries,
		std::size_t IterationCount,
		std::int32_t Multiplier,
		std::int32_t Increment,
		std::int32_t StateModulus,
		std::int32_t IndexModulus,
		FI32ElementDelta ElementDelta,
		std::int32_t ElementModulus,
		std::int32_t AccumulatorModulus,
		std::int32_t InitialState,
		std::int32_t InitialAccumulator,
		FAffineI32UpdateResult& OutResult);

	// Execute a checked scalar affine/modulo recurrence and modular accumulation.
	bool AccumulateAffineI32Modulo(
		std::size_t IterationCount,
		std::int32_t Multiplier,
		std::int32_t Increment,
		std::int32_t StateModulus,
		std::int32_t TermModulus,
		std::int32_t AccumulatorModulus,
		std::int32_t InitialState,
		std::int32_t InitialAccumulator,
		FAffineI32UpdateResult& OutResult);

	// Execute a typed integral-set membership loop without per-iteration boxing.
	bool CountAffineI32SetModulo(
		const FSet::FIntScalarElements& Entries,
		std::size_t IterationCount,
		std::int32_t Multiplier,
		std::int32_t Increment,
		std::int32_t StateModulus,
		std::int32_t InitialState,
		std::int32_t InitialAccumulator,
		FAffineI32UpdateResult& OutResult);

	bool CountAffineI32IndexedSetModulo(
		const FSet::FIntScalarElements& Entries,
		std::size_t IterationCount,
		std::int32_t Multiplier,
		std::int32_t Increment,
		std::int32_t StateModulus,
		std::int32_t IndexModulus,
		std::int32_t InitialState,
		std::int32_t InitialAccumulator,
		FAffineI32UpdateResult& OutResult);

	struct FAffineF64ArrayResult
	{
		std::int32_t State = 0;
		double Accumulator = 0.0;
		std::size_t CompletedIterations = 0;
		EAffineF64ArrayFailure Failure = EAffineF64ArrayFailure::None;
	};

	/**
	 * Execute a typed table-driven floating recurrence without per-iteration
	 * FValue/container bridging. Floating operations remain ordered as two
	 * multiplies followed by one add; integer state/index errors retain source order.
	 */
	bool AccumulateAffineF64Array(
		const double* Values,
		std::size_t ValueCount,
		std::size_t IterationCount,
		std::int32_t Multiplier,
		std::int32_t Increment,
		std::int32_t StateModulus,
		std::int32_t IndexModulus,
		double AccumulatorMultiplier,
		double ElementMultiplier,
		std::int32_t InitialState,
		double InitialAccumulator,
		FAffineF64ArrayResult& OutResult);
}
