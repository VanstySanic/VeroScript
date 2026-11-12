// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJitHotLoops.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace VeroScript::AsmJit::JitHotLoops
{
	namespace
	{
		bool CheckedAddI32(
			const std::int32_t Left,
			const std::int32_t Right,
			std::int32_t& Out) noexcept
		{
			const std::int64_t Wide = static_cast<std::int64_t>(Left)
				+ static_cast<std::int64_t>(Right);
			if (Wide < std::numeric_limits<std::int32_t>::min()
				|| Wide > std::numeric_limits<std::int32_t>::max())
			{
				return false;
			}
			Out = static_cast<std::int32_t>(Wide);
			return true;
		}

		bool CheckedMulI32(
			const std::int32_t Left,
			const std::int32_t Right,
			std::int32_t& Out) noexcept
		{
			const std::int64_t Wide = static_cast<std::int64_t>(Left)
				* static_cast<std::int64_t>(Right);
			if (Wide < std::numeric_limits<std::int32_t>::min()
				|| Wide > std::numeric_limits<std::int32_t>::max())
			{
				return false;
			}
			Out = static_cast<std::int32_t>(Wide);
			return true;
		}

		bool TryEvaluateElementDelta(
			const FI32ElementDelta& Delta,
			const std::int32_t State,
			std::int32_t& Out,
			EAffineI32UpdateFailure& OutFailure) noexcept
		{
			if (Delta.Kind == EI32ElementDeltaKind::Constant)
			{
				Out = Delta.Constant;
				return true;
			}
			if (Delta.Modulus == 0)
			{
				OutFailure = EAffineI32UpdateFailure::ElementDeltaDivisionByZero;
				return false;
			}
			if (State == std::numeric_limits<std::int32_t>::min()
				&& Delta.Modulus == -1)
			{
				OutFailure = EAffineI32UpdateFailure::ElementDeltaDivisionOverflow;
				return false;
			}
			const std::int32_t Remainder = State % Delta.Modulus;
			if (Delta.Kind == EI32ElementDeltaKind::StateModuloElementThenAdd)
			{
				Out = Remainder;
				return true;
			}
			if (!CheckedAddI32(Remainder, Delta.Addend, Out))
			{
				OutFailure = EAffineI32UpdateFailure::ElementDeltaAddOverflow;
				return false;
			}
			return true;
		}

		template <typename TResolveElement>
		bool UpdateAffineI32IndexedImpl(
			TResolveElement&& ResolveElement,
			const std::size_t IterationCount,
			const std::int32_t Multiplier,
			const std::int32_t Increment,
			const std::int32_t StateModulus,
			const std::int32_t IndexModulus,
			const FI32ElementDelta ElementDelta,
			const std::int32_t ElementModulus,
			const std::int32_t AccumulatorModulus,
			const std::int32_t InitialState,
			const std::int32_t InitialAccumulator,
			FAffineI32UpdateResult& OutResult)
		{
			std::int32_t State = InitialState;
			std::int32_t Accumulator = InitialAccumulator;
			std::int32_t LastElement = 0;
			std::int32_t LastDelta = 0;
			std::int32_t LastIndex = 0;
			bool bDeltaStored = false;
			bool bElementStored = false;
			const auto Finish = [&](const std::size_t Completed,
				const EAffineI32UpdateFailure Failure) -> bool
			{
				OutResult = {State, Accumulator, LastElement, Completed, Failure,
					LastDelta, bDeltaStored, LastIndex, bElementStored};
				return Failure == EAffineI32UpdateFailure::None;
			};

			for (std::size_t Iteration = 0; Iteration < IterationCount; ++Iteration)
			{
				std::int32_t Product = 0;
				if (!CheckedMulI32(State, Multiplier, Product))
				{
					return Finish(Iteration, EAffineI32UpdateFailure::StateMultiplyOverflow);
				}
				std::int32_t Sum = 0;
				if (!CheckedAddI32(Product, Increment, Sum))
				{
					return Finish(Iteration, EAffineI32UpdateFailure::StateAddOverflow);
				}
				if (StateModulus == 0)
				{
					return Finish(Iteration, EAffineI32UpdateFailure::StateDivisionByZero);
				}
				if (Sum == std::numeric_limits<std::int32_t>::min() && StateModulus == -1)
				{
					return Finish(Iteration, EAffineI32UpdateFailure::StateDivisionOverflow);
				}
				State = Sum % StateModulus;

				if (IndexModulus == 0)
				{
					return Finish(Iteration, EAffineI32UpdateFailure::IndexDivisionByZero);
				}
				if (State == std::numeric_limits<std::int32_t>::min()
					&& IndexModulus == -1)
				{
					return Finish(Iteration, EAffineI32UpdateFailure::IndexDivisionOverflow);
				}
				LastIndex = State % IndexModulus;

				// Resolve the source element before evaluating the right-hand delta. This is
				// the script's left-to-right evaluation order and matters when both an index
				// and a later arithmetic expression would fail on the same iteration.
				std::int32_t* const Stored = ResolveElement(LastIndex);
				if (!Stored)
				{
					return Finish(Iteration, EAffineI32UpdateFailure::IndexOutOfBounds);
				}

				EAffineI32UpdateFailure DeltaFailure = EAffineI32UpdateFailure::None;
				if (!TryEvaluateElementDelta(ElementDelta, State, LastDelta, DeltaFailure))
				{
					return Finish(Iteration, DeltaFailure);
				}
				bDeltaStored = true;
				std::int32_t Element = 0;
				if (!CheckedAddI32(*Stored, LastDelta, Element))
				{
					return Finish(Iteration, EAffineI32UpdateFailure::ElementAddOverflow);
				}
				if (ElementDelta.Kind == EI32ElementDeltaKind::StateModuloElementThenAdd)
				{
					if (!CheckedAddI32(Element, ElementDelta.Addend, Element))
					{
						return Finish(Iteration,
								EAffineI32UpdateFailure::ElementPostAddOverflow);
					}
					LastDelta = ElementDelta.Addend;
				}
				if (ElementModulus == 0)
				{
					return Finish(Iteration, EAffineI32UpdateFailure::ElementDivisionByZero);
				}
				if (Element == std::numeric_limits<std::int32_t>::min()
					&& ElementModulus == -1)
				{
					return Finish(Iteration, EAffineI32UpdateFailure::ElementDivisionOverflow);
				}
				Element %= ElementModulus;
				*Stored = Element;
				LastElement = Element;
				bElementStored = true;

				if (!CheckedAddI32(Accumulator, Element, Sum))
				{
					return Finish(Iteration, EAffineI32UpdateFailure::AccumulatorAddOverflow);
				}
				if (AccumulatorModulus == 0)
				{
					return Finish(Iteration, EAffineI32UpdateFailure::AccumulatorDivisionByZero);
				}
				if (Sum == std::numeric_limits<std::int32_t>::min()
					&& AccumulatorModulus == -1)
				{
					return Finish(Iteration, EAffineI32UpdateFailure::AccumulatorDivisionOverflow);
				}
				Accumulator = Sum % AccumulatorModulus;
			}
			return Finish(IterationCount, EAffineI32UpdateFailure::None);
		}
	}

	bool AccumulateAffineU32ArrayMasked(
		const std::uint32_t* const Values,
		const std::size_t ValueCount,
		const std::size_t IterationCount,
		const std::uint32_t Multiplier,
		const std::uint32_t Increment,
		const std::uint32_t Mask,
		const std::uint32_t InitialState,
		const std::uint32_t InitialAccumulator,
		FAffineU32ArrayResult& OutResult)
	{
		// A low-bit mask turns the index recurrence into a finite deterministic
		// state machine. For long read-only loops, discover one bounded period and
		// sum it arithmetically. Unsigned addition and multiplication are defined
		// modulo 2^32, so grouping iterations does not change language semantics.
		const bool bContiguousLowMask = Mask <= 0xFFFFu
			&& (Mask & (Mask + 1u)) == 0u;
		const std::size_t DomainSize = static_cast<std::size_t>(Mask) + 1u;
		if (bContiguousLowMask && IterationCount > DomainSize + 64u)
		{
			constexpr std::uint32_t Unseen = std::numeric_limits<std::uint32_t>::max();
			std::vector<std::uint32_t> Seen(DomainSize, Unseen);
			std::vector<std::uint32_t> PrefixSums;
			PrefixSums.reserve(DomainSize + 1u);
			PrefixSums.push_back(0u);

			std::uint32_t FullState = InitialState;
			std::uint32_t LowState = InitialState & Mask;
			Seen[LowState] = 0u;
			std::size_t CycleStart = 0;
			std::size_t CycleEnd = 0;
			for (std::size_t Step = 1; Step <= DomainSize + 1u; ++Step)
			{
				FullState = FullState * Multiplier + Increment;
				LowState = (LowState * Multiplier + Increment) & Mask;
				const std::size_t Index = static_cast<std::size_t>(LowState);
				if (Index >= ValueCount)
				{
					OutResult = {FullState,
						InitialAccumulator + PrefixSums.back(), Step - 1u};
					return false;
				}
				PrefixSums.push_back(PrefixSums.back() + Values[Index]);
				if (Seen[LowState] != Unseen)
				{
					CycleStart = Seen[LowState];
					CycleEnd = Step;
					break;
				}
				Seen[LowState] = static_cast<std::uint32_t>(Step);
			}

			if (CycleEnd != 0u)
			{
				std::uint32_t Added = 0u;
				if (IterationCount <= CycleEnd)
				{
					Added = PrefixSums[IterationCount];
				}
				else
				{
					const std::size_t CycleLength = CycleEnd - CycleStart;
					const std::uint32_t CycleSum =
						PrefixSums[CycleEnd] - PrefixSums[CycleStart];
					const std::size_t Remaining = IterationCount - CycleStart;
					const std::size_t FullCycles = Remaining / CycleLength;
					const std::size_t Remainder = Remaining % CycleLength;
					Added = PrefixSums[CycleStart]
						+ static_cast<std::uint32_t>(FullCycles) * CycleSum
						+ (PrefixSums[CycleStart + Remainder]
							- PrefixSums[CycleStart]);
				}

				std::uint32_t ResultMultiplier = 1u;
				std::uint32_t ResultIncrement = 0u;
				std::uint32_t PowerMultiplier = Multiplier;
				std::uint32_t PowerIncrement = Increment;
				for (std::size_t Remaining = IterationCount;
					Remaining != 0u; Remaining >>= 1u)
				{
					if ((Remaining & 1u) != 0u)
					{
						ResultIncrement =
							PowerMultiplier * ResultIncrement + PowerIncrement;
						ResultMultiplier *= PowerMultiplier;
					}
					PowerIncrement =
						PowerMultiplier * PowerIncrement + PowerIncrement;
					PowerMultiplier *= PowerMultiplier;
				}
				OutResult = {
					ResultMultiplier * InitialState + ResultIncrement,
					InitialAccumulator + Added,
					IterationCount};
				return true;
			}
		}

		std::uint32_t State = InitialState;
		std::uint32_t Accumulator = InitialAccumulator;
		for (std::size_t Iteration = 0; Iteration < IterationCount; ++Iteration)
		{
			State = State * Multiplier + Increment;
			const std::size_t Index = static_cast<std::size_t>(State & Mask);
			if (Index >= ValueCount)
			{
				OutResult = {State, Accumulator, Iteration};
				return false;
			}
			Accumulator += Values[Index];
		}
		OutResult = {State, Accumulator, IterationCount};
		return true;
	}

	bool WriteAffineU32ArrayMasked(
		std::uint32_t* const Values,
		const std::size_t ValueCount,
		const std::size_t IterationCount,
		const std::uint32_t Multiplier,
		const std::uint32_t Increment,
		const std::uint32_t Mask,
		const std::uint32_t InitialState,
		FAffineU32ArrayResult& OutResult)
	{
		std::uint32_t State = InitialState;
		for (std::size_t Iteration = 0; Iteration < IterationCount; ++Iteration)
		{
			State = State * Multiplier + Increment;
			const std::size_t Index = static_cast<std::size_t>(State & Mask);
			if (Index >= ValueCount)
			{
				OutResult = {State, 0u, Iteration};
				return false;
			}
			Values[Index] = State;
		}
		OutResult = {State, 0u, IterationCount};
		return true;
	}

	bool UpdateAffineI32ArrayModulo(
		std::int32_t* const Values,
		const std::size_t ValueCount,
		const std::size_t IterationCount,
		const std::int32_t Multiplier,
		const std::int32_t Increment,
		const std::int32_t StateModulus,
		const FI32ElementDelta ElementDelta,
		const std::int32_t AccumulatorModulus,
		const std::int32_t InitialState,
		const std::int32_t InitialAccumulator,
		FAffineI32UpdateResult& OutResult)
	{
		std::int32_t State = InitialState;
		std::int32_t Accumulator = InitialAccumulator;
		std::int32_t LastElement = 0;
		std::int32_t LastElementDelta = 0;
		bool bElementDeltaStored = false;
		struct FDeltaResultGuard
		{
			~FDeltaResultGuard()
			{
				Result.LastElementDelta = Delta;
				Result.bElementDeltaStored = bStored;
			}

			FAffineI32UpdateResult& Result;
			const std::int32_t& Delta;
			const bool& bStored;

		} DeltaResultGuard{OutResult, LastElementDelta, bElementDeltaStored};
		// Version the loop once when range facts prove that every checked operation
		// succeeds. This preserves the language's checked-i32 semantics while keeping
		// the hot body to a masked recurrence, one raw array update and a conditional
		// modular reduction. If any proof fails, the exact checked path below remains
		// authoritative and reports the original failing operation.
		const bool bPowerOfTwoStateModulus = StateModulus > 0
			&& (static_cast<std::uint32_t>(StateModulus)
				& (static_cast<std::uint32_t>(StateModulus) - 1u)) == 0u;
		std::int32_t MaxElementIncrement = ElementDelta.Constant;
		const bool bNonNegativeElementDelta =
			ElementDelta.Kind == EI32ElementDeltaKind::Constant
				? ElementDelta.Constant >= 0
				: bPowerOfTwoStateModulus && ElementDelta.Modulus > 0
					&& ElementDelta.Addend >= 0
					&& CheckedAddI32(
						std::min(StateModulus - 1, ElementDelta.Modulus - 1),
						ElementDelta.Addend, MaxElementIncrement);
		if (IterationCount != 0u && bPowerOfTwoStateModulus
			&& Multiplier >= 0 && Increment >= 0 && bNonNegativeElementDelta
			&& InitialState >= 0 && InitialState < StateModulus
			&& AccumulatorModulus > 0
			&& InitialAccumulator >= 0 && InitialAccumulator < AccumulatorModulus
			&& static_cast<std::size_t>(StateModulus) <= ValueCount)
		{
			const std::int64_t MaxStateUpdate =
				static_cast<std::int64_t>(StateModulus - 1) * Multiplier + Increment;
			std::int64_t MaxElement = 0;
			bool bElementsNonNegative = true;
			// The recurrence is proven to stay in [0, StateModulus). Elements past
			// that domain are unreachable and must not disable this specialization.
			for (std::size_t Index = 0;
				Index < static_cast<std::size_t>(StateModulus); ++Index)
			{
				if (Values[Index] < 0)
				{
					bElementsNonNegative = false;
					break;
				}
				MaxElement = std::max(MaxElement,
					static_cast<std::int64_t>(Values[Index]));
			}
			const bool bGrowthFitsU64 = MaxElementIncrement == 0
				|| static_cast<std::uint64_t>(IterationCount)
					<= std::numeric_limits<std::uint64_t>::max()
						/ static_cast<std::uint64_t>(MaxElementIncrement);
			const std::uint64_t MaxElementGrowth = bGrowthFitsU64
				? static_cast<std::uint64_t>(MaxElementIncrement)
					* static_cast<std::uint64_t>(IterationCount)
				: std::numeric_limits<std::uint64_t>::max();
			const bool bElementAfterFitsU64 = bGrowthFitsU64
				&& MaxElementGrowth <= std::numeric_limits<std::uint64_t>::max()
					- static_cast<std::uint64_t>(MaxElement);
			const std::uint64_t MaxElementAfter = bElementAfterFitsU64
				? static_cast<std::uint64_t>(MaxElement) + MaxElementGrowth
				: std::numeric_limits<std::uint64_t>::max();
			const bool bAccumulatorSumFitsU64 = MaxElementAfter
				<= std::numeric_limits<std::uint64_t>::max()
					- static_cast<std::uint64_t>(AccumulatorModulus - 1);
			const std::uint64_t MaxAccumulatorSum = bAccumulatorSumFitsU64
				? static_cast<std::uint64_t>(AccumulatorModulus - 1) + MaxElementAfter
				: std::numeric_limits<std::uint64_t>::max();
			if (bElementsNonNegative && bElementAfterFitsU64 && bAccumulatorSumFitsU64
				&& MaxStateUpdate <= std::numeric_limits<std::int32_t>::max()
				&& MaxElementAfter <= static_cast<std::uint64_t>(
					std::numeric_limits<std::int32_t>::max())
				&& MaxAccumulatorSum <= static_cast<std::uint64_t>(
					std::numeric_limits<std::int32_t>::max())
				&& MaxAccumulatorSum < static_cast<std::uint64_t>(AccumulatorModulus) * 2u)
			{
				const std::uint32_t Mask = static_cast<std::uint32_t>(StateModulus - 1);
				std::uint32_t FastState = static_cast<std::uint32_t>(InitialState);
				std::int32_t FastAccumulator = InitialAccumulator;
				const std::size_t DomainSize = static_cast<std::size_t>(StateModulus);
				if (DomainSize <= (1u << 20) && IterationCount > DomainSize + 64u)
				{
					std::vector<std::int32_t> Seen(DomainSize, -1);
					std::vector<std::uint32_t> Sequence;
					Sequence.reserve(DomainSize);
					std::size_t CycleStart = 0;
					while (Sequence.size() < IterationCount)
					{
						FastState = (FastState * static_cast<std::uint32_t>(Multiplier)
							+ static_cast<std::uint32_t>(Increment)) & Mask;
						const std::int32_t First = Seen[FastState];
						if (First >= 0)
						{
							CycleStart = static_cast<std::size_t>(First);
							break;
						}
						Seen[FastState] = static_cast<std::int32_t>(Sequence.size());
						Sequence.push_back(FastState);
					}
					std::vector<std::uint64_t> Visits(DomainSize, 0u);
					if (Sequence.size() == IterationCount)
					{
						for (const std::uint32_t Index : Sequence)
						{
							++Visits[Index];
						}
						FastState = Sequence.back();
					}
					else
					{
						for (std::size_t Index = 0; Index < CycleStart; ++Index)
						{
							++Visits[Sequence[Index]];
						}
						const std::size_t CycleLength = Sequence.size() - CycleStart;
						const std::size_t Remaining = IterationCount - CycleStart;
						const std::uint64_t FullCycles = Remaining / CycleLength;
						const std::size_t Remainder = Remaining % CycleLength;
						for (std::size_t Index = CycleStart; Index < Sequence.size(); ++Index)
						{
							Visits[Sequence[Index]] += FullCycles;
						}
						for (std::size_t Index = 0; Index < Remainder; ++Index)
						{
							++Visits[Sequence[CycleStart + Index]];
						}
						FastState = Sequence[CycleStart + (Remaining - 1u) % CycleLength];
					}
					const std::uint64_t Modulus =
						static_cast<std::uint64_t>(AccumulatorModulus);
					std::uint64_t AddedModulo = 0u;
					for (std::size_t Index = 0; Index < DomainSize; ++Index)
					{
						const std::uint64_t Count = Visits[Index];
						if (Count == 0u)
						{
							continue;
						}
						const std::uint64_t Initial = static_cast<std::uint64_t>(Values[Index]);
						const std::uint64_t CountMod = Count % Modulus;
						std::uint64_t IncrementContribution = 0u;
						std::int32_t DeltaForIndex = 0;
						EAffineI32UpdateFailure IgnoredFailure =
							EAffineI32UpdateFailure::None;
						if (!TryEvaluateElementDelta(ElementDelta,
							static_cast<std::int32_t>(Index), DeltaForIndex,
							IgnoredFailure))
						{
							return false;
						}
						if (DeltaForIndex != 0)
						{
							std::uint64_t Left = Count;
							std::uint64_t Right = Count + 1u;
							if ((Left & 1u) == 0u)
							{
								Left /= 2u;
							}
							else
							{
								Right /= 2u;
							}
							const std::uint64_t TriangularMod =
								(Left % Modulus) * (Right % Modulus) % Modulus;
							IncrementContribution =
								(static_cast<std::uint64_t>(DeltaForIndex) % Modulus)
									* TriangularMod;
						}
						const std::uint64_t Contribution =
							((Initial % Modulus) * CountMod
								+ IncrementContribution) % Modulus;
						AddedModulo = (AddedModulo + Contribution) % Modulus;
						Values[Index] += static_cast<std::int32_t>(
							static_cast<std::uint64_t>(DeltaForIndex) * Count);
					}
					FastAccumulator = static_cast<std::int32_t>(
						(static_cast<std::uint64_t>(FastAccumulator) + AddedModulo) % Modulus);
					LastElement = Values[FastState];
					EAffineI32UpdateFailure IgnoredFailure =
						EAffineI32UpdateFailure::None;
					if (!TryEvaluateElementDelta(ElementDelta,
							static_cast<std::int32_t>(FastState), LastElementDelta,
							IgnoredFailure))
					{
						return false;
					}
					bElementDeltaStored = true;
				}
				else
				{
					for (std::size_t Iteration = 0; Iteration < IterationCount; ++Iteration)
					{
						FastState = (FastState * static_cast<std::uint32_t>(Multiplier)
							+ static_cast<std::uint32_t>(Increment)) & Mask;
						std::int32_t Delta = 0;
						EAffineI32UpdateFailure IgnoredFailure =
							EAffineI32UpdateFailure::None;
						if (!TryEvaluateElementDelta(ElementDelta,
							static_cast<std::int32_t>(FastState), Delta,
							IgnoredFailure))
						{
							return false;
						}
						LastElementDelta = Delta;
						bElementDeltaStored = true;
						std::int32_t& Element = Values[FastState];
						Element += Delta;
						LastElement = Element;
						FastAccumulator += Element;
						if (FastAccumulator >= AccumulatorModulus)
						{
							FastAccumulator -= AccumulatorModulus;
						}
					}
				}
				OutResult = {static_cast<std::int32_t>(FastState), FastAccumulator,
					LastElement, IterationCount, EAffineI32UpdateFailure::None};
				return true;
			}
		}
		for (std::size_t Iteration = 0; Iteration < IterationCount; ++Iteration)
		{
			std::int32_t Product = 0;
			if (!CheckedMulI32(State, Multiplier, Product))
			{
				OutResult = {State, Accumulator, LastElement, Iteration,
					EAffineI32UpdateFailure::StateMultiplyOverflow};
				return false;
			}
			std::int32_t Sum = 0;
			if (!CheckedAddI32(Product, Increment, Sum))
			{
				OutResult = {State, Accumulator, LastElement, Iteration,
					EAffineI32UpdateFailure::StateAddOverflow};
				return false;
			}
			if (StateModulus == 0)
			{
				OutResult = {State, Accumulator, LastElement, Iteration,
					EAffineI32UpdateFailure::StateDivisionByZero};
				return false;
			}
			if (Sum == std::numeric_limits<std::int32_t>::min()
				&& StateModulus == -1)
			{
				OutResult = {State, Accumulator, LastElement, Iteration,
					EAffineI32UpdateFailure::StateDivisionOverflow};
				return false;
			}
			State = Sum % StateModulus;
			std::int32_t ElementIncrement = 0;
			EAffineI32UpdateFailure DeltaFailure = EAffineI32UpdateFailure::None;
			if (!TryEvaluateElementDelta(
					ElementDelta, State, ElementIncrement, DeltaFailure))
			{
				OutResult = {State, Accumulator, LastElement, Iteration, DeltaFailure};
				return false;
			}
			LastElementDelta = ElementIncrement;
			bElementDeltaStored = true;
			if (State < 0 || static_cast<std::size_t>(State) >= ValueCount)
			{
				OutResult = {State, Accumulator, LastElement, Iteration,
					EAffineI32UpdateFailure::IndexOutOfBounds};
				return false;
			}
			std::int32_t Element = 0;
			if (!CheckedAddI32(Values[static_cast<std::size_t>(State)],
					ElementIncrement, Element))
			{
				OutResult = {State, Accumulator, LastElement, Iteration,
					EAffineI32UpdateFailure::ElementAddOverflow};
				return false;
			}
			Values[static_cast<std::size_t>(State)] = Element;
			LastElement = Element;
			if (!CheckedAddI32(Accumulator, Element, Sum))
			{
				OutResult = {State, Accumulator, LastElement, Iteration,
					EAffineI32UpdateFailure::AccumulatorAddOverflow};
				return false;
			}
			if (AccumulatorModulus == 0)
			{
				OutResult = {State, Accumulator, LastElement, Iteration,
					EAffineI32UpdateFailure::AccumulatorDivisionByZero};
				return false;
			}
			if (Sum == std::numeric_limits<std::int32_t>::min()
				&& AccumulatorModulus == -1)
			{
				OutResult = {State, Accumulator, LastElement, Iteration,
					EAffineI32UpdateFailure::AccumulatorDivisionOverflow};
				return false;
			}
			Accumulator = Sum % AccumulatorModulus;
		}
		OutResult = {State, Accumulator, LastElement, IterationCount,
			EAffineI32UpdateFailure::None};
		return true;
	}

	bool AccumulateAffineI32Modulo(
		const std::size_t IterationCount,
		const std::int32_t Multiplier,
		const std::int32_t Increment,
		const std::int32_t StateModulus,
		const std::int32_t TermModulus,
		const std::int32_t AccumulatorModulus,
		const std::int32_t InitialState,
		const std::int32_t InitialAccumulator,
		FAffineI32UpdateResult& OutResult)
	{
		std::int32_t State = InitialState;
		std::int32_t Accumulator = InitialAccumulator;
		const bool bPowerOfTwoStateModulus = StateModulus > 0
			&& (static_cast<std::uint32_t>(StateModulus)
				& (static_cast<std::uint32_t>(StateModulus) - 1u)) == 0u;
		if (IterationCount != 0u && bPowerOfTwoStateModulus
			&& StateModulus <= (1 << 20) && TermModulus > 0
			&& AccumulatorModulus > 0 && Multiplier >= 0 && Increment >= 0
			&& State >= 0 && State < StateModulus
			&& Accumulator >= 0 && Accumulator < AccumulatorModulus)
		{
			const std::int64_t MaxStateUpdate =
				static_cast<std::int64_t>(StateModulus - 1) * Multiplier + Increment;
			const std::int32_t MaxTerm = std::min(StateModulus - 1, TermModulus - 1);
			const std::int64_t MaxAccumulatorSum =
				static_cast<std::int64_t>(AccumulatorModulus - 1) + MaxTerm;
			if (MaxStateUpdate <= std::numeric_limits<std::int32_t>::max()
				&& MaxAccumulatorSum <= std::numeric_limits<std::int32_t>::max()
				&& MaxAccumulatorSum < static_cast<std::int64_t>(AccumulatorModulus) * 2)
			{
				const std::uint32_t Mask = static_cast<std::uint32_t>(StateModulus - 1);
				std::uint32_t FastState = static_cast<std::uint32_t>(State);
				const std::size_t DomainSize = static_cast<std::size_t>(StateModulus);
				if (IterationCount <= DomainSize + 64u)
				{
					for (std::size_t Iteration = 0; Iteration < IterationCount; ++Iteration)
					{
						FastState = (FastState * static_cast<std::uint32_t>(Multiplier)
							+ static_cast<std::uint32_t>(Increment)) & Mask;
						Accumulator += static_cast<std::int32_t>(FastState)
							% TermModulus;
						if (Accumulator >= AccumulatorModulus)
						{
							Accumulator -= AccumulatorModulus;
						}
					}
				}
				else
				{
					std::vector<std::int32_t> Seen(DomainSize, -1);
					std::vector<std::uint32_t> Sequence;
					Sequence.reserve(DomainSize);
					std::size_t CycleStart = 0;
					while (Sequence.size() < IterationCount)
					{
						FastState = (FastState * static_cast<std::uint32_t>(Multiplier)
							+ static_cast<std::uint32_t>(Increment)) & Mask;
						const std::int32_t First = Seen[FastState];
						if (First >= 0)
						{
							CycleStart = static_cast<std::size_t>(First);
							break;
						}
						Seen[FastState] = static_cast<std::int32_t>(Sequence.size());
						Sequence.push_back(FastState);
					}
					const std::uint64_t Modulus = static_cast<std::uint64_t>(AccumulatorModulus);
					const auto SumRange = [&](const std::size_t Begin,
						const std::size_t End) -> std::uint64_t
					{
						std::uint64_t Sum = 0u;
						for (std::size_t Index = Begin; Index < End; ++Index)
						{
							Sum = (Sum + Sequence[Index]
									% static_cast<std::uint32_t>(TermModulus)) % Modulus;
						}
						return Sum;
					};
					std::uint64_t Added = 0u;
					if (Sequence.size() == IterationCount)
					{
						Added = SumRange(0u, Sequence.size());
						FastState = Sequence.back();
					}
					else
					{
						Added = SumRange(0u, CycleStart);
						const std::size_t CycleLength = Sequence.size() - CycleStart;
						const std::size_t Remaining = IterationCount - CycleStart;
						const std::uint64_t FullCycles = Remaining / CycleLength;
						const std::size_t Remainder = Remaining % CycleLength;
						const std::uint64_t CycleSum = SumRange(CycleStart, Sequence.size());
						Added = (Added + CycleSum * (FullCycles % Modulus)
							+ SumRange(CycleStart, CycleStart + Remainder)) % Modulus;
						FastState = Sequence[CycleStart + (Remaining - 1u) % CycleLength];
					}
					Accumulator = static_cast<std::int32_t>(
						(static_cast<std::uint64_t>(Accumulator) + Added) % Modulus);
				}
				OutResult = {static_cast<std::int32_t>(FastState), Accumulator, 0,
					IterationCount, EAffineI32UpdateFailure::None};
				return true;
			}
		}

		for (std::size_t Iteration = 0; Iteration < IterationCount; ++Iteration)
		{
			std::int32_t Product = 0;
			if (!CheckedMulI32(State, Multiplier, Product))
			{
				OutResult = {State, Accumulator, 0, Iteration,
					EAffineI32UpdateFailure::StateMultiplyOverflow};
				return false;
			}
			std::int32_t Sum = 0;
			if (!CheckedAddI32(Product, Increment, Sum))
			{
				OutResult = {State, Accumulator, 0, Iteration,
					EAffineI32UpdateFailure::StateAddOverflow};
				return false;
			}
			if (StateModulus == 0)
			{
				OutResult = {State, Accumulator, 0, Iteration,
					EAffineI32UpdateFailure::StateDivisionByZero};
				return false;
			}
			if (Sum == std::numeric_limits<std::int32_t>::min() && StateModulus == -1)
			{
				OutResult = {State, Accumulator, 0, Iteration,
					EAffineI32UpdateFailure::StateDivisionOverflow};
				return false;
			}
			State = Sum % StateModulus;
			if (TermModulus == 0)
			{
				OutResult = {State, Accumulator, 0, Iteration,
					EAffineI32UpdateFailure::TermDivisionByZero};
				return false;
			}
			if (State == std::numeric_limits<std::int32_t>::min() && TermModulus == -1)
			{
				OutResult = {State, Accumulator, 0, Iteration,
					EAffineI32UpdateFailure::TermDivisionOverflow};
				return false;
			}
			const std::int32_t Term = State % TermModulus;
			if (!CheckedAddI32(Accumulator, Term, Sum))
			{
				OutResult = {State, Accumulator, 0, Iteration,
					EAffineI32UpdateFailure::AccumulatorAddOverflow};
				return false;
			}
			if (AccumulatorModulus == 0)
			{
				OutResult = {State, Accumulator, 0, Iteration,
					EAffineI32UpdateFailure::AccumulatorDivisionByZero};
				return false;
			}
			if (Sum == std::numeric_limits<std::int32_t>::min()
				&& AccumulatorModulus == -1)
			{
				OutResult = {State, Accumulator, 0, Iteration,
					EAffineI32UpdateFailure::AccumulatorDivisionOverflow};
				return false;
			}
			Accumulator = Sum % AccumulatorModulus;
		}
		OutResult = {State, Accumulator, 0, IterationCount,
			EAffineI32UpdateFailure::None};
		return true;
	}

	bool CountAffineI32SetModulo(
		const FSet::FIntScalarElements& Entries,
		const std::size_t IterationCount,
		const std::int32_t Multiplier,
		const std::int32_t Increment,
		const std::int32_t StateModulus,
		const std::int32_t InitialState,
		const std::int32_t InitialAccumulator,
		FAffineI32UpdateResult& OutResult)
	{
		std::int32_t State = InitialState;
		std::int32_t Total = InitialAccumulator;
		const bool bPowerOfTwoStateModulus = StateModulus > 0
			&& (static_cast<std::uint32_t>(StateModulus)
				& (static_cast<std::uint32_t>(StateModulus) - 1u)) == 0u;
		if (bPowerOfTwoStateModulus && Multiplier >= 0 && Increment >= 0
			&& State >= 0 && State < StateModulus && Total >= 0
			&& IterationCount <= static_cast<std::size_t>(
				std::numeric_limits<std::int32_t>::max() - Total)
			&& static_cast<std::int64_t>(StateModulus - 1) * Multiplier + Increment
				<= std::numeric_limits<std::int32_t>::max())
		{
			const std::uint32_t Mask = static_cast<std::uint32_t>(StateModulus - 1);
			std::uint32_t FastState = static_cast<std::uint32_t>(State);
			std::vector<std::uint8_t> Membership;
			if (StateModulus <= (1 << 20)
				&& IterationCount > static_cast<std::size_t>(StateModulus) + 64u)
			{
				Membership.resize(static_cast<std::size_t>(StateModulus));
				for (std::int32_t Value = 0; Value < StateModulus; ++Value)
				{
					Membership[static_cast<std::size_t>(Value)] =
							Entries.Contains(Value) ? 1u : 0u;
				}
			}
			for (std::size_t Iteration = 0; Iteration < IterationCount; ++Iteration)
			{
				FastState = (FastState * static_cast<std::uint32_t>(Multiplier)
					+ static_cast<std::uint32_t>(Increment)) & Mask;
				if (!Membership.empty() ? Membership[FastState] != 0u
					: Entries.Contains(static_cast<std::int32_t>(FastState)))
				{
					++Total;
				}
			}
			OutResult = {static_cast<std::int32_t>(FastState), Total, 0,
				IterationCount, EAffineI32UpdateFailure::None};
			return true;
		}
		for (std::size_t Iteration = 0; Iteration < IterationCount; ++Iteration)
		{
			std::int32_t Product = 0;
			if (!CheckedMulI32(State, Multiplier, Product))
			{
				OutResult = {State, Total, 0, Iteration,
					EAffineI32UpdateFailure::StateMultiplyOverflow};
				return false;
			}
			std::int32_t Sum = 0;
			if (!CheckedAddI32(Product, Increment, Sum))
			{
				OutResult = {State, Total, 0, Iteration,
					EAffineI32UpdateFailure::StateAddOverflow};
				return false;
			}
			if (StateModulus == 0)
			{
				OutResult = {State, Total, 0, Iteration,
					EAffineI32UpdateFailure::StateDivisionByZero};
				return false;
			}
			if (Sum == std::numeric_limits<std::int32_t>::min() && StateModulus == -1)
			{
				OutResult = {State, Total, 0, Iteration,
					EAffineI32UpdateFailure::StateDivisionOverflow};
				return false;
			}
			State = Sum % StateModulus;
			if (Entries.Contains(State))
			{
				if (Total == std::numeric_limits<std::int32_t>::max())
				{
					OutResult = {State, Total, 0, Iteration,
						EAffineI32UpdateFailure::AccumulatorAddOverflow};
					return false;
				}
				++Total;
			}
		}
		OutResult = {State, Total, 0, IterationCount,
			EAffineI32UpdateFailure::None};
		return true;
	}

	bool UpdateAffineI32MapModulo(
		FMap::FIntI32Entries& Entries,
		const std::size_t IterationCount,
		const std::int32_t Multiplier,
		const std::int32_t Increment,
		const std::int32_t StateModulus,
		const FI32ElementDelta ElementDelta,
		const std::int32_t AccumulatorModulus,
		const std::int32_t InitialState,
		const std::int32_t InitialAccumulator,
		FAffineI32UpdateResult& OutResult)
	{
		std::int32_t State = InitialState;
		std::int32_t Accumulator = InitialAccumulator;
		std::int32_t LastElement = 0;
		std::int32_t LastElementDelta = 0;
		bool bElementDeltaStored = false;
		struct FDeltaResultGuard
		{
			~FDeltaResultGuard()
			{
				Result.LastElementDelta = Delta;
				Result.bElementDeltaStored = bStored;
			}

			FAffineI32UpdateResult& Result;
			const std::int32_t& Delta;
			const bool& bStored;

		} DeltaResultGuard{OutResult, LastElementDelta, bElementDeltaStored};
		// A dense, bounded integer key domain can use the same checked typed loop
		// as Array after resolving every key once. This is a representation-level
		// optimization: it does not depend on insertion order, bucket layout or a
		// particular recurrence, and missing/sparse domains retain the authoritative
		// per-iteration lookup below so the first observable error is unchanged.
		const bool bPowerOfTwoStateModulus = StateModulus > 0
			&& (static_cast<std::uint32_t>(StateModulus)
				& (static_cast<std::uint32_t>(StateModulus) - 1u)) == 0u;
		if (IterationCount != 0u && bPowerOfTwoStateModulus
			&& StateModulus <= (1 << 20)
			&& InitialState >= 0 && InitialState < StateModulus)
		{
			const std::size_t DomainSize = static_cast<std::size_t>(StateModulus);
			std::vector<std::int32_t*> Slots;
			std::vector<std::int32_t> Values;
			if (Entries.TryGetDenseZeroBasedValues(DomainSize, Slots))
			{
				Values.reserve(DomainSize);
				for (std::int32_t* const Slot : Slots)
				{
					Values.push_back(*Slot);
				}
				FAffineI32UpdateResult DenseResult;
				const bool bOk = UpdateAffineI32ArrayModulo(
					Values.data(), Values.size(), IterationCount,
					Multiplier, Increment, StateModulus, ElementDelta,
					AccumulatorModulus, InitialState, InitialAccumulator, DenseResult);
				for (std::size_t Index = 0; Index < DomainSize; ++Index)
				{
					*Slots[Index] = Values[Index];
				}
				LastElementDelta = DenseResult.LastElementDelta;
				bElementDeltaStored = DenseResult.bElementDeltaStored;
				OutResult = DenseResult;
				return bOk;
			}
		}
		for (std::size_t Iteration = 0; Iteration < IterationCount; ++Iteration)
		{
			std::int32_t Product = 0;
			if (!CheckedMulI32(State, Multiplier, Product))
			{
				OutResult = {State, Accumulator, LastElement, Iteration,
					EAffineI32UpdateFailure::StateMultiplyOverflow};
				return false;
			}
			std::int32_t Sum = 0;
			if (!CheckedAddI32(Product, Increment, Sum))
			{
				OutResult = {State, Accumulator, LastElement, Iteration,
					EAffineI32UpdateFailure::StateAddOverflow};
				return false;
			}
			if (StateModulus == 0)
			{
				OutResult = {State, Accumulator, LastElement, Iteration,
					EAffineI32UpdateFailure::StateDivisionByZero};
				return false;
			}
			if (Sum == std::numeric_limits<std::int32_t>::min()
				&& StateModulus == -1)
			{
				OutResult = {State, Accumulator, LastElement, Iteration,
					EAffineI32UpdateFailure::StateDivisionOverflow};
				return false;
			}
			State = Sum % StateModulus;
			std::int32_t ElementIncrement = 0;
			EAffineI32UpdateFailure DeltaFailure = EAffineI32UpdateFailure::None;
			if (!TryEvaluateElementDelta(
					ElementDelta, State, ElementIncrement, DeltaFailure))
			{
				OutResult = {State, Accumulator, LastElement, Iteration, DeltaFailure};
				return false;
			}
			LastElementDelta = ElementIncrement;
			bElementDeltaStored = true;
			std::int32_t* const Stored = Entries.FindMutable(State);
			if (!Stored)
			{
				OutResult = {State, Accumulator, LastElement, Iteration,
					EAffineI32UpdateFailure::IndexOutOfBounds};
				return false;
			}
			std::int32_t Element = 0;
			if (!CheckedAddI32(*Stored, ElementIncrement, Element))
			{
				OutResult = {State, Accumulator, LastElement, Iteration,
					EAffineI32UpdateFailure::ElementAddOverflow};
				return false;
			}
			*Stored = Element;
			LastElement = Element;
			if (!CheckedAddI32(Accumulator, Element, Sum))
			{
				OutResult = {State, Accumulator, LastElement, Iteration,
					EAffineI32UpdateFailure::AccumulatorAddOverflow};
				return false;
			}
			if (AccumulatorModulus == 0)
			{
				OutResult = {State, Accumulator, LastElement, Iteration,
					EAffineI32UpdateFailure::AccumulatorDivisionByZero};
				return false;
			}
			if (Sum == std::numeric_limits<std::int32_t>::min()
				&& AccumulatorModulus == -1)
			{
				OutResult = {State, Accumulator, LastElement, Iteration,
					EAffineI32UpdateFailure::AccumulatorDivisionOverflow};
				return false;
			}
			Accumulator = Sum % AccumulatorModulus;
		}
		OutResult = {State, Accumulator, LastElement, IterationCount,
			EAffineI32UpdateFailure::None};
		return true;
	}

	bool UpdateAffineI32IndexedArrayModulo(
		std::int32_t* const Values,
		const std::size_t ValueCount,
		const std::size_t IterationCount,
		const std::int32_t Multiplier,
		const std::int32_t Increment,
		const std::int32_t StateModulus,
		const std::int32_t IndexModulus,
		const FI32ElementDelta ElementDelta,
		const std::int32_t ElementModulus,
		const std::int32_t AccumulatorModulus,
		const std::int32_t InitialState,
		const std::int32_t InitialAccumulator,
		FAffineI32UpdateResult& OutResult)
	{
		return UpdateAffineI32IndexedImpl(
			[Values, ValueCount](const std::int32_t Index) -> std::int32_t*
			{
				return Values && Index >= 0 && static_cast<std::size_t>(Index) < ValueCount
						? Values + static_cast<std::size_t>(Index)
					: nullptr;
			},
			IterationCount, Multiplier, Increment, StateModulus, IndexModulus,
			ElementDelta, ElementModulus, AccumulatorModulus,
			InitialState, InitialAccumulator, OutResult);
	}

	bool UpdateAffineI32IndexedMapModulo(
		FMap::FIntI32Entries& Entries,
		const std::size_t IterationCount,
		const std::int32_t Multiplier,
		const std::int32_t Increment,
		const std::int32_t StateModulus,
		const std::int32_t IndexModulus,
		const FI32ElementDelta ElementDelta,
		const std::int32_t ElementModulus,
		const std::int32_t AccumulatorModulus,
		const std::int32_t InitialState,
		const std::int32_t InitialAccumulator,
		FAffineI32UpdateResult& OutResult)
	{
		// Dense integer domains are representation-compatible with Array. Resolve
		// bucket addresses once and run the typed loop over contiguous scratch;
		// sparse maps retain direct typed lookups and exact missing-key behavior.
		if (IndexModulus > 0 && IndexModulus <= (1 << 20))
		{
			const std::size_t DomainSize = static_cast<std::size_t>(IndexModulus);
			std::vector<std::int32_t*> Slots;
			if (Entries.TryGetDenseZeroBasedValues(DomainSize, Slots))
			{
				std::vector<std::int32_t> Values;
				Values.reserve(DomainSize);
				for (const std::int32_t* const Slot : Slots)
				{
					Values.push_back(*Slot);
				}
				const bool bOk = UpdateAffineI32IndexedArrayModulo(
					Values.data(), Values.size(), IterationCount, Multiplier, Increment,
					StateModulus, IndexModulus, ElementDelta, ElementModulus,
					AccumulatorModulus, InitialState, InitialAccumulator, OutResult);
				for (std::size_t Index = 0; Index < DomainSize; ++Index)
				{
					*Slots[Index] = Values[Index];
				}
				return bOk;
			}
		}
		return UpdateAffineI32IndexedImpl(
			[&Entries](const std::int32_t Index)
			{
				return Entries.FindMutable(Index);
			},
			IterationCount, Multiplier, Increment, StateModulus, IndexModulus,
			ElementDelta, ElementModulus, AccumulatorModulus,
			InitialState, InitialAccumulator, OutResult);
	}

	bool CountAffineI32IndexedSetModulo(
		const FSet::FIntScalarElements& Entries,
		const std::size_t IterationCount,
		const std::int32_t Multiplier,
		const std::int32_t Increment,
		const std::int32_t StateModulus,
		const std::int32_t IndexModulus,
		const std::int32_t InitialState,
		const std::int32_t InitialAccumulator,
		FAffineI32UpdateResult& OutResult)
	{
		std::int32_t State = InitialState;
		std::int32_t Total = InitialAccumulator;
		std::int32_t LastIndex = 0;
		std::vector<std::uint8_t> Membership;
		if (IndexModulus > 0 && IndexModulus <= (1 << 20)
			&& IterationCount > static_cast<std::size_t>(IndexModulus))
		{
			Membership.resize(static_cast<std::size_t>(IndexModulus));
			for (std::int32_t Value = 0; Value < IndexModulus; ++Value)
			{
				Membership[static_cast<std::size_t>(Value)] = Entries.Contains(Value) ? 1u : 0u;
			}
		}
		const auto Finish = [&](const std::size_t Completed,
			const EAffineI32UpdateFailure Failure) -> bool
		{
			OutResult = {State, Total, 0, Completed, Failure, 0, false, LastIndex, false};
			return Failure == EAffineI32UpdateFailure::None;
		};
		for (std::size_t Iteration = 0; Iteration < IterationCount; ++Iteration)
		{
			std::int32_t Product = 0;
			if (!CheckedMulI32(State, Multiplier, Product))
			{
				return Finish(Iteration, EAffineI32UpdateFailure::StateMultiplyOverflow);
			}
			std::int32_t Sum = 0;
			if (!CheckedAddI32(Product, Increment, Sum))
			{
				return Finish(Iteration, EAffineI32UpdateFailure::StateAddOverflow);
			}
			if (StateModulus == 0)
			{
				return Finish(Iteration, EAffineI32UpdateFailure::StateDivisionByZero);
			}
			if (Sum == std::numeric_limits<std::int32_t>::min() && StateModulus == -1)
			{
				return Finish(Iteration, EAffineI32UpdateFailure::StateDivisionOverflow);
			}
			State = Sum % StateModulus;
			if (IndexModulus == 0)
			{
				return Finish(Iteration, EAffineI32UpdateFailure::IndexDivisionByZero);
			}
			if (State == std::numeric_limits<std::int32_t>::min() && IndexModulus == -1)
			{
				return Finish(Iteration, EAffineI32UpdateFailure::IndexDivisionOverflow);
			}
			LastIndex = State % IndexModulus;
			const bool bContains = LastIndex >= 0
				&& (!Membership.empty()
					? Membership[static_cast<std::size_t>(LastIndex)] != 0u
					: Entries.Contains(LastIndex));
			if (bContains)
			{
				if (Total == std::numeric_limits<std::int32_t>::max())
				{
					return Finish(Iteration, EAffineI32UpdateFailure::AccumulatorAddOverflow);
				}
				++Total;
			}
		}
		return Finish(IterationCount, EAffineI32UpdateFailure::None);
	}

	bool AccumulateAffineF64Array(
		const double* const Values,
		const std::size_t ValueCount,
		const std::size_t IterationCount,
		const std::int32_t Multiplier,
		const std::int32_t Increment,
		const std::int32_t StateModulus,
		const std::int32_t IndexModulus,
		const double AccumulatorMultiplier,
		const double ElementMultiplier,
		const std::int32_t InitialState,
		const double InitialAccumulator,
		FAffineF64ArrayResult& OutResult)
	{
		std::int32_t State = InitialState;
		double Accumulator = InitialAccumulator;
		const bool bPowerOfTwoStateModulus = StateModulus > 0
			&& (static_cast<std::uint32_t>(StateModulus)
				& (static_cast<std::uint32_t>(StateModulus) - 1u)) == 0u;
		const bool bPowerOfTwoIndexModulus = IndexModulus > 0
			&& (static_cast<std::uint32_t>(IndexModulus)
				& (static_cast<std::uint32_t>(IndexModulus) - 1u)) == 0u;
		// Once the recurrence is inside a positive modulus and its maximum
		// multiply-add fits i32, every later iteration is proven non-negative and
		// overflow-free.  This proof is independent of the modulus being a power of
		// two; retaining that old restriction forced an idiv plus two redundant
		// overflow branches for the common prime-modulus LCG shape.
		const bool bUncheckedState = StateModulus > 0
			&& Multiplier >= 0 && Increment >= 0
			&& State >= 0 && State < StateModulus
			&& static_cast<std::int64_t>(StateModulus - 1) * Multiplier + Increment
				<= std::numeric_limits<std::int32_t>::max();
		const std::uint32_t StateMask = bPowerOfTwoStateModulus
			? static_cast<std::uint32_t>(StateModulus - 1)
			: 0u;
		const std::uint32_t IndexMask = bPowerOfTwoIndexModulus
			? static_cast<std::uint32_t>(IndexModulus - 1)
			: 0u;
		const std::uint64_t StateReciprocal = StateModulus > 0
			? (std::uint64_t{1} << 32u) / static_cast<std::uint32_t>(StateModulus)
			: 0u;
		const std::uint64_t IndexReciprocal = IndexModulus > 0
			? (std::uint64_t{1} << 32u) / static_cast<std::uint32_t>(IndexModulus)
			: 0u;
		const auto PositiveRemainder = [](const std::uint32_t Value,
			const std::uint32_t Divisor, const std::uint64_t Reciprocal) noexcept
		{
			const std::uint64_t Quotient =
				(static_cast<std::uint64_t>(Value) * Reciprocal) >> 32u;
			std::uint32_t Remainder = Value
				- static_cast<std::uint32_t>(Quotient * Divisor);
			// floor(2^32 / d) underestimates the exact quotient by at most one.
			if (Remainder >= Divisor)
			{
				Remainder -= Divisor;
			}
			return Remainder;
		};

		for (std::size_t Iteration = 0; Iteration < IterationCount; ++Iteration)
		{
			if (bUncheckedState)
			{
				const std::uint32_t Sum = static_cast<std::uint32_t>(State)
					* static_cast<std::uint32_t>(Multiplier)
					+ static_cast<std::uint32_t>(Increment);
				State = static_cast<std::int32_t>(bPowerOfTwoStateModulus
					? Sum & StateMask
					: PositiveRemainder(Sum,
						static_cast<std::uint32_t>(StateModulus), StateReciprocal));
			}
			else
			{
				std::int32_t Product = 0;
				if (!CheckedMulI32(State, Multiplier, Product))
				{
					OutResult = {State, Accumulator, Iteration,
						EAffineF64ArrayFailure::StateMultiplyOverflow};
					return false;
				}
				std::int32_t Sum = 0;
				if (!CheckedAddI32(Product, Increment, Sum))
				{
					OutResult = {State, Accumulator, Iteration,
						EAffineF64ArrayFailure::StateAddOverflow};
					return false;
				}
				if (StateModulus == 0)
				{
					OutResult = {State, Accumulator, Iteration,
						EAffineF64ArrayFailure::StateDivisionByZero};
					return false;
				}
				if (Sum == std::numeric_limits<std::int32_t>::min()
					&& StateModulus == -1)
				{
					OutResult = {State, Accumulator, Iteration,
						EAffineF64ArrayFailure::StateDivisionOverflow};
					return false;
				}
				State = Sum % StateModulus;
			}

			std::int32_t Index = 0;
			if (IndexModulus > 0 && State >= 0)
			{
				const std::uint32_t Value = static_cast<std::uint32_t>(State);
				Index = static_cast<std::int32_t>(bPowerOfTwoIndexModulus
					? Value & IndexMask
					: PositiveRemainder(Value,
						static_cast<std::uint32_t>(IndexModulus), IndexReciprocal));
			}
			else
			{
				if (IndexModulus == 0)
				{
					OutResult = {State, Accumulator, Iteration,
						EAffineF64ArrayFailure::IndexDivisionByZero};
					return false;
				}
				if (State == std::numeric_limits<std::int32_t>::min()
					&& IndexModulus == -1)
				{
					OutResult = {State, Accumulator, Iteration,
						EAffineF64ArrayFailure::IndexDivisionOverflow};
					return false;
				}
				Index = State % IndexModulus;
			}
			if (!Values || Index < 0 || static_cast<std::size_t>(Index) >= ValueCount)
			{
				OutResult = {State, Accumulator, Iteration,
					EAffineF64ArrayFailure::IndexOutOfBounds};
				return false;
			}

			// Volatile intermediates prohibit contraction into FMA. VM, Linear and
			// native backends therefore observe the same two binary64 roundings before
			// the final addition on x64 and AArch64.
			volatile double LeftProduct = Accumulator * AccumulatorMultiplier;
			volatile double RightProduct = Values[static_cast<std::size_t>(Index)]
				* ElementMultiplier;
			Accumulator = LeftProduct + RightProduct;
		}
		OutResult = {State, Accumulator, IterationCount,
			EAffineF64ArrayFailure::None};
		return true;
	}
}
