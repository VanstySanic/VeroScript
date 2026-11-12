// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJitLinear.h"
#include "VeroScriptJitKindStates.h"
#include "VeroScriptJitText.h"

#include "VeroScriptJitIr.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptAsmJitExecutionContext.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <utility>

namespace VeroScript::AsmJit::JitLinear
{
	namespace
	{
		constexpr std::uint32_t InvalidInstruction = 0xFFFFFFFFu;
		// Hot loops normally need fewer than a dozen typed operands. Keeping a small
		// inline stack avoids reserving several KiB in every executor activation;
		// unusual generated expressions use the plan-sized heap fallback below.
		constexpr std::size_t MaxTypedLoopStack = 32u;

		using ELocalKind = ETypedLoopLocalKind;
		using FLocalBinding = FTypedLoopNativeBinding;

// Keep this read-only kernel out of the large multi-kind executor. Its
// scalar state is invocation-local; the caller commits at the same exits
// as the original fused loop, including each individual arithmetic failure.
#if defined(_MSC_VER)
		__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
		__attribute__((noinline))
#endif
		FAffineSetResult ExecuteAffineSetKernel(
		    const FTypedLoopPlan::FDirectAffineSetI32& Plan,
		    const FSet::FIntScalarElements::FReadOnlyProbe Values,
		    std::int32_t Counter, const std::int32_t Limit,
		    std::int32_t State, std::int32_t Total) noexcept
		{
			const std::int32_t Multiplier = Plan.StateMultiplier;
			const std::int32_t Increment = Plan.StateIncrement;
			const std::int32_t Divisor = Plan.StateModulus;
			const std::uint32_t UnsignedDivisor = static_cast<std::uint32_t>(Divisor);
			const bool PowerOfTwo = Divisor > 0 && (UnsignedDivisor & (UnsignedDivisor - 1u)) == 0u;
			const std::uint64_t Reciprocal = Divisor > 0
			    ? (std::uint64_t{1} << 32u) / UnsignedDivisor
			    : 0u;
			const auto Reduce = [&](const std::uint32_t Value)
			{
				if (PowerOfTwo) return Value & (UnsignedDivisor - 1u);
				const std::uint64_t Quotient = (std::uint64_t{Value} * Reciprocal) >> 32u;
				const std::uint32_t Remainder = Value - static_cast<std::uint32_t>(Quotient * UnsignedDivisor);
				return Remainder >= UnsignedDivisor ? Remainder - UnsignedDivisor : Remainder;
			};
			// Prove the entire read-only recurrence before omitting per-iteration
			// overflow calls. Unproven inputs keep the original partial-effect exits.
			const auto Remaining = static_cast<std::int64_t>(Limit) - Counter;
			const auto KeyDivisor = static_cast<std::uint32_t>(Plan.bModuloKey ? Plan.KeyModulus : Divisor);
			const auto KeyReciprocal = KeyDivisor != 0u ? (std::uint64_t{1} << 32u) / KeyDivisor : 0u;
			const auto KeyFor = [&](const std::uint32_t Value)
			{
				if (!Plan.bModuloKey) return Value;
				const auto Quotient = (std::uint64_t{Value} * KeyReciprocal) >> 32u;
				const auto Remainder = Value - static_cast<std::uint32_t>(Quotient * KeyDivisor);
				return Remainder >= KeyDivisor ? Remainder - KeyDivisor : Remainder;
			};
			if (Remaining > 0 && State >= 0 && State < Divisor && (!Plan.bModuloKey || Plan.KeyModulus > 0) && Multiplier >= 0 && Increment >= 0 && (static_cast<std::int64_t>(Divisor) - 1) * Multiplier + Increment <= std::numeric_limits<std::int32_t>::max() && static_cast<std::int64_t>(Total) + Remaining <= std::numeric_limits<std::int32_t>::max())
			{
				// Amortize hashing only for long read-only loops over a small proven
				// universe. The bounded invocation-local snapshot never escapes.
				if (KeyDivisor <= 65536u && Remaining >= 4096 && Values.GetCapacity() <= static_cast<std::uint64_t>(Remaining))
				{
					std::array<std::uint64_t, 1024> Bitmap;
					Values.BuildNonnegativeBitmap(Bitmap.data(), (KeyDivisor + 63u) / 64u);
					while (Counter < Limit)
					{
						const auto Value = Reduce(static_cast<std::uint32_t>(State * Multiplier + Increment));
						State = static_cast<std::int32_t>(Value);
						const auto Key = KeyFor(Value);
						Total += static_cast<std::int32_t>((Bitmap[Key / 64u] >> (Key % 64u)) & 1u);
						++Counter;
					}
					return {Counter, State, Total, 0u, nullptr};
				}
				while (Counter < Limit)
				{
					State = static_cast<std::int32_t>(Reduce(static_cast<std::uint32_t>(State * Multiplier + Increment)));
					Total += Values.Contains(static_cast<std::int32_t>(KeyFor(static_cast<std::uint32_t>(State)))) ? 1 : 0;
					++Counter;
				}
				return {Counter, State, Total, 0u, nullptr};
			}
			const auto Fail = [&](const std::uint16_t Instruction, const char* Error)
			{
				return FAffineSetResult{Counter, State, Total, Instruction, Error};
			};
			while (Counter < Limit)
			{
				std::int32_t Product = 0, Sum = 0;
				if (!Runtime::CheckedMul(State, Multiplier, Product))
				{
					return Fail(Plan.StateMulInstruction, "integer overflow");
				}
				if (!Runtime::CheckedAdd(Product, Increment, Sum))
				{
					return Fail(Plan.StateAddInstruction, "integer overflow");
				}
				// Same exact reciprocal reduction as FastModuloAt below. Negative
				// operands and invalid divisors retain the checked signed operation.
				if (Sum >= 0 && Divisor > 0)
				{
					const std::uint32_t Value = static_cast<std::uint32_t>(Sum);
					State = static_cast<std::int32_t>(Reduce(Value));
				}
				else
				{
					if (Divisor == 0)
					{
						return Fail(Plan.StateModInstruction, "division by zero");
					}
					if (Sum == std::numeric_limits<std::int32_t>::min() && Divisor == -1)
					{
						return Fail(Plan.StateModInstruction, "integer overflow");
					}
					State = Sum % Divisor;
				}
				std::int32_t Key = State;
				if (Plan.bModuloKey)
				{
					if (Plan.KeyModulus == 0) return Fail(Plan.KeyModInstruction, "division by zero");
					if (State == std::numeric_limits<std::int32_t>::min() && Plan.KeyModulus == -1)
					{
						return Fail(Plan.KeyModInstruction, "integer overflow");
					}
					Key %= Plan.KeyModulus;
				}
				if (Values.Contains(Key))
				{
					if (!Runtime::CheckedAdd(Total, 1, Sum))
					{
						return Fail(Plan.TotalIncrementInstruction, "integer overflow");
					}
					Total = Sum;
				}
				if (!Runtime::CheckedAdd(Counter, 1, Sum))
				{
					return Fail(Plan.CounterInstruction, "integer overflow");
				}
				Counter = Sum;
			}
			return {Counter, State, Total, 0u, nullptr};
		}

		struct FPositiveI32Reducer
		{
			FPositiveI32Reducer() noexcept = default;

			explicit FPositiveI32Reducer(const std::int32_t InDivisor) noexcept
			{
				if (InDivisor <= 0)
				{
					return;
				}
				Divisor = static_cast<std::uint32_t>(InDivisor);
				Mask = Divisor - 1u;
				bPowerOfTwo = (Divisor & Mask) == 0u;
				Reciprocal = (std::uint64_t{1} << 32u) / Divisor;
				if (bPowerOfTwo)
				{
					std::uint32_t Remaining = Divisor;
					while (Remaining > 1u)
					{
						Remaining >>= 1u;
						++Shift;
					}
				}
			}

			std::uint32_t Mod(const std::uint32_t Value) const noexcept
			{
				if (bPowerOfTwo)
				{
					return Value & Mask;
				}
				const std::uint64_t Quotient =
				    (static_cast<std::uint64_t>(Value) * Reciprocal) >> 32u;
				std::uint32_t Remainder = Value - static_cast<std::uint32_t>(Quotient * Divisor);
				if (Remainder >= Divisor)
				{
					Remainder -= Divisor;
				}
				return Remainder;
			}

			std::uint32_t Div(const std::uint32_t Value) const noexcept
			{
				if (bPowerOfTwo)
				{
					return Value >> Shift;
				}
				std::uint64_t Quotient =
				    (static_cast<std::uint64_t>(Value) * Reciprocal) >> 32u;
				if (static_cast<std::uint64_t>(Value) - Quotient * Divisor >= Divisor)
				{
					++Quotient;
				}
				return static_cast<std::uint32_t>(Quotient);
			}

			std::uint32_t Divisor = 0;
			std::uint32_t Mask = 0;
			std::uint64_t Reciprocal = 0;
			std::uint8_t Shift = 0;
			bool bPowerOfTwo = false;
		};

		struct FBoundedStateMapF64Result
		{
			std::int32_t State = 0;
			std::int32_t Key = 0;
			double Value = 0.0;
			double Total = 0.0;
		};

		// Dense storage and the complete nonnegative integer domain are proven by
		// the caller. Keep floating operations and writes in their source order.
		template <typename FPlan>
#if defined(_MSC_VER)
		__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
		__attribute__((noinline))
#endif
		FBoundedStateMapF64Result ExecuteBoundedStateMapF64(const FPlan& Plan,
		    double* const* const Entries, const std::int32_t Size,
		    const std::int32_t First, const std::int32_t Limit,
		    std::int32_t State, double Total) noexcept
		{
			const FPositiveI32Reducer StateModulo(Plan.StateModulus);
			const FPositiveI32Reducer KeyModulo(Size);
			const FPositiveI32Reducer ValueModulo(Plan.StateValueModulus);
			std::int32_t Key = 0;
			double Value = 0.0;
			for (std::int32_t Counter = First; Counter < Limit; ++Counter)
			{
				State = static_cast<std::int32_t>(StateModulo.Mod(
				    static_cast<std::uint32_t>(State * Plan.StateMultiplier + Plan.StateIncrement)));
				Key = static_cast<std::int32_t>(KeyModulo.Mod(
				    static_cast<std::uint32_t>(State + Counter * Plan.KeyMultiplier)));
				double* const Entry = Entries[static_cast<std::size_t>(Key)];
				Value = *Entry * Plan.ValueMultiplier + static_cast<double>(ValueModulo.Mod(static_cast<std::uint32_t>(State))) * Plan.StateValueScale;
				if (Value > Plan.Threshold)
				{
					Value *= Plan.ConditionalScale;
				}
				*Entry = Value;
				Total += Value;
			}
			return {State, Key, Value, Total};
		}

		enum class EFastArrayVirtualIntrinsic : std::uint8_t
		{
			None,
			AddSaltModulo,
			XorSaltMaskAddModulo,
		};

		struct FArrayVirtualResolvedTarget
		{
			const FValue* Value = nullptr;
			const FTypedLoopPlan::FPureI32MethodExpression* Expression = nullptr;
			EFastArrayVirtualIntrinsic Intrinsic = EFastArrayVirtualIntrinsic::None;
			std::int32_t Operand0 = 0;
			std::int32_t Operand1 = 0;
			std::int32_t Operand2 = 0;
			FPositiveI32Reducer Operand1Reducer;
			FPositiveI32Reducer Operand2Reducer;
		};

		struct FFastArrayVirtualResult
		{
			std::int32_t Counter = 0;
			std::int32_t State = 0;
			std::int32_t Index = 0;
			std::int32_t Accumulator = 0;
			std::size_t Completed = 0;
		};

// Runtime preflight proves every arithmetic range and both indexed lookups.
// This compiled C++ kernel is the multi-target native lowering: the hot loop
// contains only raw integers, reciprocal reductions and one intrinsic tag.
#if defined(_MSC_VER)
		template <bool bReduceResult>
		__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
		template <bool bReduceResult>
		__attribute__((noinline))
#else
		template <bool bReduceResult>
#endif
		FFastArrayVirtualResult ExecuteFastArrayVirtualKernel(
		    const FTypedLoopPlan::FStateArrayVirtualI32& D,
		    const std::int32_t* const Order,
		    const FArrayVirtualResolvedTarget* const Targets,
		    std::int32_t Counter,
		    const std::int32_t Limit,
		    std::int32_t State,
		    std::int32_t Accumulator) noexcept
		{
			const FPositiveI32Reducer StateModulo(D.StateModulus);
			const FPositiveI32Reducer PositionModulo(D.PositionOperand);
			FFastArrayVirtualResult Result;
			while (Counter < Limit)
			{
				const std::uint32_t StateSum = static_cast<std::uint32_t>(State) * static_cast<std::uint32_t>(D.StateMultiplier) + static_cast<std::uint32_t>(D.StateIncrement);
				State = static_cast<std::int32_t>(StateModulo.Mod(StateSum));
				const std::uint32_t PositionSource = static_cast<std::uint32_t>(State) + static_cast<std::uint32_t>(Accumulator);
				const std::uint32_t Position = D.bPositionUsesMask
				    ? PositionSource & static_cast<std::uint32_t>(D.PositionOperand)
				    : PositionModulo.Mod(PositionSource);
				Result.Index = Order[Position];
				const FArrayVirtualResolvedTarget& Target =
				    Targets[static_cast<std::size_t>(Result.Index)];
				if (Target.Intrinsic == EFastArrayVirtualIntrinsic::AddSaltModulo)
				{
					const std::uint32_t Intermediate =
					    static_cast<std::uint32_t>(Accumulator) + static_cast<std::uint32_t>(Target.Operand0);
					const std::uint32_t Salt = Target.Operand1Reducer.Mod(
					    static_cast<std::uint32_t>(State));
					const std::uint32_t Value = Intermediate + Salt;
					if constexpr (bReduceResult)
					{
						Accumulator = static_cast<std::int32_t>(Target.Operand2Reducer.Mod(Value));
					}
					else
					{
						Accumulator = static_cast<std::int32_t>(Value);
					}
				}
				else
				{
					const std::uint32_t Intermediate =
					    static_cast<std::uint32_t>(Accumulator) ^ (static_cast<std::uint32_t>(State) & static_cast<std::uint32_t>(Target.Operand0));
					const std::uint32_t Value =
					    Intermediate + static_cast<std::uint32_t>(Target.Operand1);
					if constexpr (bReduceResult)
					{
						Accumulator = static_cast<std::int32_t>(Target.Operand2Reducer.Mod(Value));
					}
					else
					{
						Accumulator = static_cast<std::int32_t>(Value);
					}
				}
				++Counter;
				++Result.Completed;
			}
			Result.Counter = Counter;
			Result.State = State;
			Result.Accumulator = Accumulator;
			return Result;
		}

#if defined(_MSC_VER)
		__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
		__attribute__((noinline))
#endif
		FFastArrayVirtualResult ExecuteFastArrayVirtualPairNoReductionKernel(
		    const FTypedLoopPlan::FStateArrayVirtualI32& D,
		    const std::int32_t* const Order,
		    const FArrayVirtualResolvedTarget& AddTarget,
		    const FArrayVirtualResolvedTarget& XorTarget,
		    const std::int32_t AddIndex,
		    std::int32_t Counter,
		    const std::int32_t Limit,
		    std::int32_t State,
		    std::int32_t Accumulator) noexcept
		{
			const FPositiveI32Reducer StateModulo(D.StateModulus);
			const FPositiveI32Reducer PositionModulo(D.PositionOperand);
			FFastArrayVirtualResult Result;
			while (Counter < Limit)
			{
				const std::uint32_t StateSum = static_cast<std::uint32_t>(State) * static_cast<std::uint32_t>(D.StateMultiplier) + static_cast<std::uint32_t>(D.StateIncrement);
				State = static_cast<std::int32_t>(StateModulo.Mod(StateSum));
				const std::uint32_t PositionSource = static_cast<std::uint32_t>(State) + static_cast<std::uint32_t>(Accumulator);
				const std::uint32_t Position = D.bPositionUsesMask
				    ? PositionSource & static_cast<std::uint32_t>(D.PositionOperand)
				    : PositionModulo.Mod(PositionSource);
				Result.Index = Order[Position];

				const std::uint32_t AddValue = static_cast<std::uint32_t>(Accumulator) + static_cast<std::uint32_t>(AddTarget.Operand0) + AddTarget.Operand1Reducer.Mod(static_cast<std::uint32_t>(State));
				const std::uint32_t XorValue =
				    (static_cast<std::uint32_t>(Accumulator) ^ (static_cast<std::uint32_t>(State) & static_cast<std::uint32_t>(XorTarget.Operand0))) + static_cast<std::uint32_t>(XorTarget.Operand1);
				const std::uint32_t AddMask = 0u - static_cast<std::uint32_t>(Result.Index == AddIndex);
				Accumulator = static_cast<std::int32_t>(
				    (AddValue & AddMask) | (XorValue & ~AddMask));
				++Counter;
				++Result.Completed;
			}
			Result.Counter = Counter;
			Result.State = State;
			Result.Accumulator = Accumulator;
			return Result;
		}

		struct FFastStateIndirectResult
		{
			std::int32_t Counter = 0;
			std::int32_t State = 0;
			std::int32_t Total = 0;
			std::int32_t Position = 0;
			std::int32_t Index = 0;
			std::int32_t Delta = 0;
			std::int32_t Stored = 0;
			std::size_t Completed = 0;
		};

// The executor proves all ranges before entering this kernel. Keeping it out
// of the multi-kind dispatcher gives the optimizer a compact loop with no VM
// calls, boxed values, hardware division, or per-operation error branches.
#if defined(_MSC_VER)
		__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
		__attribute__((noinline))
#endif
		FFastStateIndirectResult ExecuteFastStateIndirectKernel(
		    const FTypedLoopPlan::FIndirectI32& D,
		    const std::int32_t* const Order,
		    std::int32_t* const Values,
		    std::int32_t Counter,
		    const std::int32_t Limit,
		    std::int32_t State,
		    std::int32_t Total) noexcept
		{
			const FPositiveI32Reducer StateModulo(D.CursorModulus);
			const FPositiveI32Reducer CounterModulo(D.SecondaryIndexModulus);
			const FPositiveI32Reducer PositionModulo(D.PositionModulus);
			const FPositiveI32Reducer DeltaDivisor(D.DeltaDivisor);
			const FPositiveI32Reducer DeltaModulo(D.DeltaModulus);
			const FPositiveI32Reducer ElementModulo(D.ElementModulus);
			const FPositiveI32Reducer TotalModulo(D.TotalModulus);
			FFastStateIndirectResult Result;
			while (Counter < Limit)
			{
				const std::uint32_t StateSum = static_cast<std::uint32_t>(State) * static_cast<std::uint32_t>(D.CursorMultiplier) + static_cast<std::uint32_t>(D.CursorIncrement);
				State = static_cast<std::int32_t>(StateModulo.Mod(StateSum));
				const std::uint32_t CounterIndex = CounterModulo.Mod(
				    static_cast<std::uint32_t>(Counter));
				const std::uint32_t PositionSum = static_cast<std::uint32_t>(State) + CounterIndex * static_cast<std::uint32_t>(D.PositionMultiplier);
				Result.Position = static_cast<std::int32_t>(PositionModulo.Mod(PositionSum));
				Result.Index = Order[static_cast<std::size_t>(Result.Position)];
				const std::uint32_t DeltaBase = DeltaDivisor.Div(
				    static_cast<std::uint32_t>(State));
				Result.Delta = static_cast<std::int32_t>(
				    DeltaModulo.Mod(DeltaBase) + static_cast<std::uint32_t>(D.DeltaAddend));
				std::int32_t& Element = Values[static_cast<std::size_t>(Result.Index)];
				Result.Stored = static_cast<std::int32_t>(ElementModulo.Mod(
				    static_cast<std::uint32_t>(Element) + static_cast<std::uint32_t>(Result.Delta)));
				Element = Result.Stored;
				const std::uint32_t PositionContribution =
				    static_cast<std::uint32_t>(Result.Position & D.PositionMask);
				Total = static_cast<std::int32_t>(TotalModulo.Mod(
				    static_cast<std::uint32_t>(Total) + static_cast<std::uint32_t>(Result.Stored) + PositionContribution));
				++Counter;
				++Result.Completed;
			}
			Result.Counter = Counter;
			Result.State = State;
			Result.Total = Total;
			return Result;
		}

		struct FFastStringCompareResult
		{
			std::int32_t Counter = 0;
			std::int32_t State = 0;
			std::int32_t LeftIndex = 0;
			std::int32_t RightIndex = 0;
			std::int32_t Total = 0;
			std::size_t Completed = 0;
		};

#if defined(_MSC_VER)
		__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
		__attribute__((noinline))
#endif
		FFastStringCompareResult ExecuteFastStringCompareKernel(
		    const FTypedLoopPlan::FStateDualStringArrayCompareI32& D,
		    const std::vector<std::string>& Left,
		    const std::vector<std::string>& Right,
		    const std::size_t* const LeftHashes,
		    const std::size_t* const RightHashes,
		    std::int32_t Counter,
		    const std::int32_t Limit,
		    std::int32_t State,
		    std::int32_t Total) noexcept
		{
			const FPositiveI32Reducer StateModulo(D.StateModulus);
			const FPositiveI32Reducer LeftModulo(D.LeftIndexModulus);
			const FPositiveI32Reducer RightModulo(D.RightIndexModulus);
			FFastStringCompareResult Result;
			while (Counter < Limit)
			{
				const std::uint32_t StateSum = static_cast<std::uint32_t>(State) * static_cast<std::uint32_t>(D.StateMultiplier) + static_cast<std::uint32_t>(D.StateIncrement);
				State = static_cast<std::int32_t>(StateModulo.Mod(StateSum));
				Result.LeftIndex = static_cast<std::int32_t>(
				    LeftModulo.Mod(static_cast<std::uint32_t>(State)));
				const std::uint32_t RightSource = static_cast<std::uint32_t>(State) + (static_cast<std::uint32_t>(Counter) & static_cast<std::uint32_t>(D.CounterMask));
				Result.RightIndex = static_cast<std::int32_t>(RightModulo.Mod(RightSource));
				const std::size_t L = static_cast<std::size_t>(Result.LeftIndex);
				const std::size_t R = static_cast<std::size_t>(Result.RightIndex);
				if (LeftHashes[L] == RightHashes[R] && Left[L] == Right[R])
				{
					++Total;
				}
				++Counter;
				++Result.Completed;
			}
			Result.Counter = Counter;
			Result.State = State;
			Result.Total = Total;
			return Result;
		}

		struct FMethodSignedDivMagic32
		{
			std::int32_t Multiplier = 0;
			std::uint8_t Shift = 0;
		};

		// Precompute invariant signed-division constants once at method binding.
		// Zero and +/-1 remain explicit machine-code cases and never reach here.
		FMethodSignedDivMagic32 ComputeMethodSignedDivMagic32(
		    const std::int32_t Divisor)
		{
			const std::uint64_t AbsoluteDivisor = Divisor < 0
			    ? static_cast<std::uint64_t>(-static_cast<std::int64_t>(Divisor))
			    : static_cast<std::uint64_t>(Divisor);
			constexpr std::uint64_t Two31 = std::uint64_t{1} << 31u;
			const std::uint64_t Sign = static_cast<std::uint32_t>(Divisor) >> 31u;
			const std::uint64_t T = Two31 + Sign;
			const std::uint64_t Anc = T - 1u - T % AbsoluteDivisor;
			std::uint32_t P = 31u;
			std::uint64_t Q1 = Two31 / Anc;
			std::uint64_t R1 = Two31 - Q1 * Anc;
			std::uint64_t Q2 = Two31 / AbsoluteDivisor;
			std::uint64_t R2 = Two31 - Q2 * AbsoluteDivisor;
			std::uint64_t Delta = 0;
			do
			{
				++P;
				Q1 *= 2u;
				R1 *= 2u;
				if (R1 >= Anc)
				{
					++Q1;
					R1 -= Anc;
				}
				Q2 *= 2u;
				R2 *= 2u;
				if (R2 >= AbsoluteDivisor)
				{
					++Q2;
					R2 -= AbsoluteDivisor;
				}
				Delta = AbsoluteDivisor - R2;
			}
			while (Q1 < Delta || (Q1 == Delta && R1 == 0u));
			std::int32_t Multiplier = static_cast<std::int32_t>(
			    static_cast<std::uint32_t>(Q2 + 1u));
			if (Divisor < 0)
			{
				Multiplier = static_cast<std::int32_t>(
				    0u - static_cast<std::uint32_t>(Multiplier));
			}
			return {Multiplier, static_cast<std::uint8_t>(P - 32u)};
		}

		enum class EStackKind : std::uint8_t
		{
			I32,
			U32,
			F32,
			F64,
			Bool,
			ArrayI32,
			ArrayU32,
			ArrayF32,
			ArrayF64,
			ArrayString,
			ArrayWString,
			String,
			WString,
			MapI32I32,
			MapI32U32,
			MapI32F32,
			MapI32F64,
			MapU32I32,
			MapU32U32,
			MapU32F32,
			MapU32F64,
			MapStringI32,
			MapWStringI32,
			SetI32,
			SetU32,
			SetString,
			SetWString,
			ArrayObject,
			Object,
		};

		struct FTypedValue
		{
			EStackKind Kind = EStackKind::I32;
			std::int32_t I32 = 0;
			void* Storage = nullptr;
			std::uint16_t SourceSlot = 0xFFFFu;
			double F64 = 0.0;
			float F32 = 0.0f;
			std::uint32_t U32 = 0;
		};

		bool TryCheckedArithmetic(
		    const EOpcode Opcode,
		    const std::int32_t Left,
		    const std::int32_t Right,
		    std::int32_t& Out,
		    const char*& OutError)
		{
			OutError = nullptr;
			switch (Opcode)
			{
			case EOpcode::Add:
				{
					if (Runtime::CheckedAdd(Left, Right, Out))
					{
						return true;
					}
					OutError = "integer overflow";
					return false;
				}
			case EOpcode::Sub:
				{
					if (Runtime::CheckedSub(Left, Right, Out))
					{
						return true;
					}
					OutError = "integer overflow";
					return false;
				}
			case EOpcode::Mul:
				{
					if (Runtime::CheckedMul(Left, Right, Out))
					{
						return true;
					}
					OutError = "integer overflow";
					return false;
				}
			case EOpcode::Div:
				{
					if (Right == 0)
					{
						OutError = "division by zero";
						return false;
					}
					if (Left == std::numeric_limits<std::int32_t>::min() && Right == -1)
					{
						OutError = "integer overflow";
						return false;
					}
					Out = Left / Right;
					return true;
				}
			case EOpcode::Mod:
				{
					if (Right == 0)
					{
						OutError = "division by zero";
						return false;
					}
					if (Left == std::numeric_limits<std::int32_t>::min() && Right == -1)
					{
						OutError = "integer overflow";
						return false;
					}
					Out = Left % Right;
					return true;
				}
			default:
				{
					OutError = "unsupported typed arithmetic";
					return false;
				}
			}
		}

	}

#if VEROSCRIPT_ENABLE_JIT_TEST_HOOKS
	thread_local FAffineSetObserver AffineSetObserver = nullptr;
	FAffineSetResult TestExecuteAffineSetKernel(
	    const FTypedLoopPlan::FDirectAffineSetI32& Plan,
	    const FSet::FIntScalarElements::FReadOnlyProbe Values,
	    const std::int32_t Counter, const std::int32_t Limit,
	    const std::int32_t State, const std::int32_t Total) noexcept
	{
		return ExecuteAffineSetKernel(Plan, Values, Counter, Limit, State, Total);
	}
#endif

// Isolate the large hot executor from page-offset changes in cold code.
// Other toolchains retain their default placement until independently tested.
#if defined(_MSC_VER) && defined(_M_X64)
	__declspec(code_seg(".vsjit"))
#endif
	FTypedLoopExecutionResult ExecuteTypedLoopPlan(
	    FExecutionContext& Vm,
	    const FTypedLoopPlanSet& Plans,
	    const std::size_t HeaderOffset)
	{
		FTypedLoopExecutionResult Result;
		Result.NextBytecodeOffset = HeaderOffset;
		Result.ErrorBytecodeOffset = HeaderOffset;
		if (HeaderOffset >= Plans.PlanAtOffset.size())
		{
			return Result;
		}
		const std::uint16_t EncodedIndex = Plans.PlanAtOffset[HeaderOffset];
		if (EncodedIndex == 0u || EncodedIndex > Plans.Plans.size())
		{
			return Result;
		}
		const FTypedLoopPlan& Plan = Plans.Plans[EncodedIndex - 1u];
		if (!Plan.FieldMethods.empty() || std::any_of(Plan.GenericPureMethods.begin(), Plan.GenericPureMethods.end(),
			[](const auto& Method) {return !Method.FieldNameIndices.empty();}))
		{
			// A zero-trip loop must not scan a retained object array merely to
			// prepare a kernel it will never enter. Evaluate only a pure, canonical
			// initial guard with an exact exit; every other header keeps normal flow.
			const auto I32Local = [&](const std::uint16_t Slot) -> const std::int32_t*
			{
				const auto* Value = Vm.JitBorrowDirectLinearLocal(Slot);
				return Value ? std::get_if<std::int32_t>(Value) : nullptr;
			};
			const auto IsExit = [&](const FTypedLoopInstruction& Branch)
			{
				return Branch.BranchInstruction == 0xFFFFFFFFu && Branch.ExternalBranchOffset == Plan.ExitOffset;
			};
			const auto& Instructions = Plan.Instructions;
			bool bEmpty = false;
			if (!Instructions.empty() && Instructions[0].Opcode == EOpcode::JmpIfLocalGeImm && IsExit(Instructions[0]))
			{
				const auto* Counter = I32Local(Instructions[0].Operand0);
				bEmpty = Counter && *Counter >= Instructions[0].Immediate;
			}
			else if (Instructions.size() >= 4u && Instructions[0].Opcode == EOpcode::LoadLocal && Instructions[2].Opcode == EOpcode::Lt && Instructions[3].Opcode == EOpcode::JmpIfFalse && IsExit(Instructions[3]))
			{
				const auto* Counter = I32Local(Instructions[0].Operand0);
				const auto* Limit = Instructions[1].Opcode == EOpcode::LoadLocal ? I32Local(Instructions[1].Operand0)
					: Instructions[1].Opcode == EOpcode::ConstI32 && !Instructions[1].bImmediateU32 && !Instructions[1].bImmediateF32 && Instructions[1].WideImmediateKind == ETypedLoopLocalKind::Unsupported ? &Instructions[1].Immediate : nullptr;
				bEmpty = Counter && Limit && *Counter >= *Limit;
			}
			if (bEmpty) return {ETypedLoopExecution::Completed, Plan.ExitOffset, Plan.HeaderOffset};
		}
		if (Plan.bRuntimeBoundArrayPush && (Vm.JitHasInstanceCallInterceptor() || Vm.JitHasMutationCallback()))
		{
			return Result;
		}

		using FDirectFieldBinding = FTypedLoopNativeFieldBinding;
		std::vector<FDirectFieldBinding> DirectFields;
		std::vector<FTypedLoopNativeMethodBinding> DirectMethods;
		std::vector<std::vector<std::int32_t*>> DirectMethodFields;
		std::vector<FTypedLoopNativePolymorphicMethodBinding> PolymorphicMethods;
		std::vector<std::vector<FTypedLoopNativePolymorphicTarget>>
		    PolymorphicCandidates;
		std::vector<std::vector<FTypedLoopNativePolymorphicTarget>>
		    PolymorphicCaches;
		const bool bUsesDirectFieldMethods =
		    Plan.FusedKind == FTypedLoopPlan::EFusedKind::None && std::any_of(Plan.Instructions.begin(), Plan.Instructions.end(), [](const FTypedLoopInstruction& Instruction)
		{
			return (Instruction.Opcode == EOpcode::CallMethod || Instruction.Opcode == EOpcode::CallBoundMethod) && Instruction.Immediate == 0 && (Instruction.ArgumentCount == 1u || Instruction.FieldMethodIndex != 0xFFFFu) && Instruction.Operand1 != 0xFFFFu;
		});
		// Reject unavailable direct method bindings before allocating loop scratch.
		// Hoisted receivers must first be materialized at their original source point.
		const auto PrepareDirectFieldMethods = [&]() -> bool
		{
			if (bUsesDirectFieldMethods)
			{
				// A direct method kernel retains the receiver and its field slot for the
				// complete loop. Interceptors and mutation observers may invalidate either
				// assumption, so they remain on the authoritative ordinary call path.
				if (!Vm.JitCanUseDirectScalarMethod() || Vm.JitHasInstanceCallInterceptor() || Vm.JitHasMutationCallback())
				{
					return false;
				}
				for (std::size_t Index = 0; Index < Plan.Instructions.size(); ++Index)
				{
					const FTypedLoopInstruction& Instruction = Plan.Instructions[Index];
					if ((Instruction.Opcode != EOpcode::CallMethod && Instruction.Opcode != EOpcode::CallBoundMethod) || Instruction.Immediate != 0 || (Instruction.ArgumentCount != 1u && Instruction.FieldMethodIndex == 0xFFFFu))
					{
						continue;
					}
					FTypedLoopNativeMethodBinding Method;
					if (Instruction.FieldMethodIndex != 0xFFFFu)
					{
						if (Instruction.FieldMethodIndex >= Plan.FieldMethods.size()) return false;
						const auto& Expression = Plan.FieldMethods[Instruction.FieldMethodIndex];
						if (Expression.ArgumentCount != Instruction.ArgumentCount) return false;
						const auto* Value = Vm.JitBorrowDirectLinearLocal(Instruction.Operand1);
						const auto* Handle = Value ? std::get_if<FObjectHandle>(Value) : nullptr;
						if (!Handle || !*Handle || !Expression.NativeCode || Vm.JitRequiresCoreVirtualDispatch(**Handle, Expression.FunctionIndex)) return false;
						if (DirectMethodFields.empty())
						{
							DirectMethodFields.resize(Plan.Instructions.size());
						}
						auto& Fields = DirectMethodFields[Index];
						Fields.resize(Expression.FieldNameIndices.size());
						for (std::size_t Field = 0; Field < Fields.size(); ++Field)
						{
							FScriptObject* Object = nullptr;
							std::uint16_t FieldIndex = 0xFFFFu;
							if (!Vm.JitBorrowLocalI32Field(Instruction.Operand1, Expression.FieldNameIndices[Field], Object, FieldIndex, Fields[Field]) || !Fields[Field] || Object != Handle->get()) return false;
						}
						Method.Object = Handle->get();
						Method.FunctionIndex = Expression.FunctionIndex;
						Method.ReceiverSlot = Instruction.Operand1;
						Method.FieldCode = Expression.NativeCode;
						Method.ArgumentCount = Expression.ArgumentCount;
						Method.FieldState.Fields = Fields.data();
						if (DirectMethods.empty())
						{
							DirectMethods.resize(Plan.Instructions.size());
						}
						DirectMethods[Index] = Method;
						continue;
					}
					bool bHandled = false;
					bool bReturnsI32 = false;
					const bool bBorrowed = Instruction.Opcode == EOpcode::CallBoundMethod
					    ? Vm.JitBorrowDirectI32BoundFieldMethod(
					          Instruction.Operand1, Instruction.Operand0,
					          Method.Object, Method.FieldIndex, Method.Storage,
					          Method.Addend, Method.ArgumentModulus, Method.ResultModulus,
					          bHandled, &bReturnsI32)
					    : Vm.JitBorrowDirectI32FieldMethod(
					          Instruction.Operand1, Instruction.Operand0,
					          Instruction.BytecodeOffset, Method.Object,
					          Method.FieldIndex, Method.Storage, Method.Addend,
					          Method.ArgumentModulus, Method.ResultModulus,
					          bHandled, &bReturnsI32);
					if (!bBorrowed)
					{
						Result = {ETypedLoopExecution::Failed, HeaderOffset, Instruction.BytecodeOffset};
						return false;
					}
					if (!bHandled || !bReturnsI32 || !Method.Object || !Method.Storage)
					{
						return false;
					}
					const auto PrepareMagic = [](const std::int32_t Divisor,
					                              std::int32_t& OutMagic, std::uint8_t& OutShift)
					{
						if (Divisor == 0 || Divisor == 1 || Divisor == -1)
						{
							OutMagic = 0;
							OutShift = 0;
							return;
						}
						const FMethodSignedDivMagic32 Magic =
						    ComputeMethodSignedDivMagic32(Divisor);
						OutMagic = Magic.Multiplier;
						OutShift = Magic.Shift;
					};
					PrepareMagic(Method.ArgumentModulus,
					    Method.ArgumentMagic, Method.ArgumentShift);
					PrepareMagic(Method.ResultModulus,
					    Method.ResultMagic, Method.ResultShift);
					if (DirectMethods.empty())
					{
						DirectMethods.resize(Plan.Instructions.size());
					}
					DirectMethods[Index] = Method;
				}
			}
			return true;
		};
		if (!Plan.bHoistsNonEscapingObject && !PrepareDirectFieldMethods()) return Result;

		if (Plan.bUsesTextOperations && (Vm.JitHasInstanceCallInterceptor() || Vm.JitHasMutationCallback())) return Result;
		if (std::any_of(Plan.Instructions.begin(), Plan.Instructions.end(), [](const auto &I)
				{
					return I.TextFunctionIndex != 0xFFFFu;
				}) &&
			!Vm.JitCanInlineTextProjection())
		{
			return Result;
		}
		auto* const TextBuffers = Plan.TextBuffers.get();
		if (TextBuffers && TextBuffers->Active.exchange(true, std::memory_order_acquire)) return Result;
		struct FTextBufferLease
		{
			FTypedLoopTextBufferPool* Pool;
			~FTextBufferLease()
			{
				if (Pool)
				{
					Pool->Active.store(false, std::memory_order_release);
				}
			}
		} TextBufferLease{TextBuffers};

		constexpr std::size_t InlineTypedLocalCount = 64u;
		std::array<FLocalBinding, InlineTypedLocalCount> InlineLocals{};
		std::vector<FLocalBinding> ExtendedLocals;
		FLocalBinding* Locals = InlineLocals.data();
		const std::size_t LocalCount = Plan.LocalCount;
		if (Plan.bRuntimeBoundArrayPush && LocalCount > InlineLocals.size()) return Result;
		// Compact, rooted receiver views for object arrays. Each element binding
		// retains its authoritative FValue while exposing only the raw object pointer
		// to generated code; no shared_ptr/FValue work remains inside the loop.
		std::vector<std::vector<FTypedLoopNativeObjectView>> NativeObjectElementBindings;
		std::size_t ObjectArrayPrefixCount = std::numeric_limits<std::size_t>::max();
		if (Plan.ObjectArrayPrefixCounterSlot != 0xFFFFu)
		{
			const auto* CounterValue = Vm.JitBorrowDirectLinearLocal(Plan.ObjectArrayPrefixCounterSlot);
			const auto* Counter = CounterValue ? std::get_if<std::int32_t>(CounterValue) : nullptr;
			const auto* LimitValue = Plan.ObjectArrayPrefixLimitSlot == 0xFFFFu ? nullptr
				: Vm.JitBorrowDirectLinearLocal(Plan.ObjectArrayPrefixLimitSlot);
			const auto* Limit = Plan.ObjectArrayPrefixLimitSlot == 0xFFFFu ? &Plan.ObjectArrayPrefixLimitImmediate
				: LimitValue ? std::get_if<std::int32_t>(LimitValue) : nullptr;
			// The structural proof is conditional on the actual entry values. A
			// resumed/nonzero or differently typed induction variable keeps full scan.
			if (Counter && *Counter == 0 && Limit)
			{
				ObjectArrayPrefixCount = *Limit > 0 ? static_cast<std::size_t>(*Limit) : 0u;
			}
		}
		bool bHasLifetimeVirtualReceiver = false;
		const auto RecordLifetimeReceiver = [&](const FScriptObject& Object)
		{
			if (bHasLifetimeVirtualReceiver || Plan.GenericPureMethods.empty() || (!Object.bConstructionIncomplete && !Object.bDestroying))
			{
				return;
			}
			bHasLifetimeVirtualReceiver = std::any_of(
			    Plan.GenericPureMethods.begin(), Plan.GenericPureMethods.end(),
			    [&](const auto& Method)
			{
				return Vm.JitRequiresCoreVirtualDispatch(Object, Method.FunctionIndex);
			});
		};
		if (LocalCount > InlineLocals.size())
		{
			ExtendedLocals.resize(LocalCount);
			Locals = ExtendedLocals.data();
		}
		const auto Bind = [&](const std::uint16_t Slot) -> bool
		{
			if (Slot >= LocalCount)
			{
				return false;
			}
			if (Locals[Slot].Kind != ELocalKind::Unsupported)
			{
				return true;
			}
			FValue* const Value = Vm.JitBorrowDirectLinearLocal(Slot);
			if (!Value)
			{
				return false;
			}
			if (auto* Scalar = std::get_if<std::int32_t>(Value))
			{
				Locals[Slot] = {ELocalKind::I32, Scalar};
				return true;
			}
			if (auto* Scalar = std::get_if<std::uint32_t>(Value))
			{
				Locals[Slot] = {ELocalKind::U32, Scalar};
				return true;
			}
			if (Plan.bRuntimeBoundArrayPush && !std::holds_alternative<bool>(*Value) &&
				!std::holds_alternative<FDynamicArrayHandle>(*Value))
			{
				return false;
			}
			if (auto* Scalar = std::get_if<std::int64_t>(Value))
			{
				Locals[Slot] = {ELocalKind::I64, Scalar};
				return true;
			}
			if (auto* Scalar = std::get_if<std::uint64_t>(Value))
			{
				Locals[Slot] = {ELocalKind::U64, Scalar};
				return true;
			}
			if (auto* Scalar = std::get_if<float>(Value))
			{
				Locals[Slot] = {ELocalKind::F32, Scalar};
				return true;
			}
			if (auto* Scalar = std::get_if<double>(Value))
			{
				Locals[Slot] = {ELocalKind::F64, Scalar};
				return true;
			}
			if (auto* Scalar = std::get_if<bool>(Value))
			{
				Locals[Slot] = {ELocalKind::Bool, Scalar};
				return true;
			}
			if (auto* Scalar = std::get_if<std::string>(Value))
			{
				Locals[Slot] = {ELocalKind::String, Scalar};
				return true;
			}
			if (auto* Scalar = std::get_if<std::u16string>(Value))
			{
				Locals[Slot] = {ELocalKind::WString, Scalar};
				return true;
			}
			if (auto* Handle = std::get_if<FDynamicArrayHandle>(Value);
			    Handle && *Handle && (*Handle)->MutationOrigin.FieldIndex == static_cast<std::uint16_t>(-1))
			{
				if (Plan.bRuntimeBoundArrayPush && !(*Handle)->Elements.empty()) return false;
				if (auto* Storage = std::get_if<std::vector<std::int32_t>>(
				        &(*Handle)->ScalarElements))
				{
					const auto& Type = (*Handle)->ElementTypeName;
					if (Plan.bRuntimeBoundArrayPush && Type != "int" && Type != "int32") return false;
					Locals[Slot] = {ELocalKind::ArrayI32, Storage,
					    Storage->data(), Storage->size()};
					return true;
				}
				if (auto* Storage = std::get_if<std::vector<std::uint32_t>>(
				        &(*Handle)->ScalarElements))
				{
					const auto& Type = (*Handle)->ElementTypeName;
					if (Plan.bRuntimeBoundArrayPush && Type != "unsigned int" && Type != "uint" &&
						Type != "unsigned" && Type != "uint32")
					{
						return false;
					}
					Locals[Slot] = {ELocalKind::ArrayU32, Storage,
					    Storage->data(), Storage->size()};
					return true;
				}
				// An optional probe never walks a boxed receiver or creates object bindings.
				if (Plan.bRuntimeBoundArrayPush) return false;
				if (auto* Storage = std::get_if<std::vector<float>>(
				        &(*Handle)->ScalarElements))
				{
					Locals[Slot] = {ELocalKind::ArrayF32, Storage};
					return true;
				}
				if (auto* Storage = std::get_if<std::vector<double>>(
				        &(*Handle)->ScalarElements))
				{
					Locals[Slot] = {ELocalKind::ArrayF64, Storage};
					return true;
				}
				if (auto* Storage = std::get_if<std::vector<std::string>>(
				        &(*Handle)->ScalarElements))
				{
					Locals[Slot] = {ELocalKind::ArrayString, Storage,
					    Storage->data(), Storage->size()};
					return true;
				}
				if (auto* Storage = std::get_if<std::vector<std::u16string>>(
				        &(*Handle)->ScalarElements))
				{
					Locals[Slot] = {ELocalKind::ArrayWString, Storage};
					return true;
				}
				if (!(*Handle)->HasScalarElements() && (!(*Handle)->Elements.empty() || ObjectArrayPrefixCount == 0u))
				{
					const auto PreparedSize = std::min((*Handle)->Elements.size(), ObjectArrayPrefixCount);
					for (std::size_t Index = 0; Index < PreparedSize; ++Index)
					{
						const auto* Object = std::get_if<FObjectHandle>(&(*Handle)->Elements[Index]);
						if (!Object || !*Object) return false;
						RecordLifetimeReceiver(**Object);
					}
					if (NativeObjectElementBindings.empty())
					{
						NativeObjectElementBindings.resize(LocalCount);
					}
					auto& Views = NativeObjectElementBindings[Slot];
					Views.resize(PreparedSize);
					Locals[Slot] = {ELocalKind::ArrayObject, &(*Handle)->Elements,
					    Views.data(), (*Handle)->Elements.size()};
					return true;
				}
			}
			if (auto* Handle = std::get_if<FMapHandle>(Value);
			    Handle && *Handle && (*Handle)->MutationOrigin.FieldIndex == static_cast<std::uint16_t>(-1))
			{
				if (auto* Storage = std::get_if<FMap::FIntI32Entries>(
				        &(*Handle)->ScalarEntries))
				{
					Locals[Slot] = {ELocalKind::MapI32I32, Storage};
					return true;
				}
				if (auto* Storage = std::get_if<FMap::FIntU32Entries>(
				        &(*Handle)->ScalarEntries))
				{
					Locals[Slot] = {ELocalKind::MapI32U32, Storage};
					return true;
				}
				if (auto* Storage = std::get_if<FMap::FIntF32Entries>(
				        &(*Handle)->ScalarEntries))
				{
					Locals[Slot] = {ELocalKind::MapI32F32, Storage};
					return true;
				}
				if (auto* Storage = std::get_if<FMap::FIntF64Entries>(
				        &(*Handle)->ScalarEntries))
				{
					Locals[Slot] = {ELocalKind::MapI32F64, Storage};
					return true;
				}
				if (auto* Storage = std::get_if<FMap::FUIntI32Entries>(
				        &(*Handle)->ScalarEntries))
				{
					Locals[Slot] = {ELocalKind::MapU32I32, Storage};
					return true;
				}
				if (auto* Storage = std::get_if<FMap::FUIntU32Entries>(
				        &(*Handle)->ScalarEntries))
				{
					Locals[Slot] = {ELocalKind::MapU32U32, Storage};
					return true;
				}
				if (auto* Storage = std::get_if<FMap::FUIntF32Entries>(
				        &(*Handle)->ScalarEntries))
				{
					Locals[Slot] = {ELocalKind::MapU32F32, Storage};
					return true;
				}
				if (auto* Storage = std::get_if<FMap::FUIntF64Entries>(
				        &(*Handle)->ScalarEntries))
				{
					Locals[Slot] = {ELocalKind::MapU32F64, Storage};
					return true;
				}
				if (auto* Storage = std::get_if<FMap::FStringI32Entries>(
				        &(*Handle)->ScalarEntries))
				{
					Locals[Slot] = {ELocalKind::MapStringI32, Storage};
					return true;
				}
				if (auto* Storage = std::get_if<FMap::FWStringI32Entries>(
				        &(*Handle)->ScalarEntries))
				{
					Locals[Slot] = {ELocalKind::MapWStringI32, Storage};
					return true;
				}
			}
			if (auto* Handle = std::get_if<FSetHandle>(Value);
			    Handle && *Handle && (*Handle)->MutationOrigin.FieldIndex == static_cast<std::uint16_t>(-1))
			{
				if (auto* Storage = std::get_if<FSet::FIntScalarElements>(
				        &(*Handle)->ScalarElements))
				{
					Locals[Slot] = {ELocalKind::SetI32, Storage};
					return true;
				}
				if (auto* Storage = std::get_if<FSet::FUIntScalarElements>(
				        &(*Handle)->ScalarElements))
				{
					Locals[Slot] = {ELocalKind::SetU32, Storage};
					return true;
				}
				if (auto* Storage = std::get_if<FSet::FStringScalarElements>(
				        &(*Handle)->ScalarElements))
				{
					Locals[Slot] = {ELocalKind::SetString, Storage};
					return true;
				}
				if (auto* Storage = std::get_if<FSet::FWStringScalarElements>(
				        &(*Handle)->ScalarElements))
				{
					Locals[Slot] = {ELocalKind::SetWString, Storage};
					return true;
				}
			}
			if (auto* Handle = std::get_if<FObjectHandle>(Value); Handle && *Handle)
			{
				Locals[Slot] = {ELocalKind::Object, Handle->get()};
				Locals[Slot].SourceValue = Value;
				RecordLifetimeReceiver(**Handle);
				return true;
			}
			return false;
		};
		// Bind every value that is already materialized at loop entry. Compiler
		// temporaries may still be monostate here; StoreLocal initializes and binds
		// those lazily before their first LoadLocal.
		std::array<std::uint8_t, InlineTypedLocalCount> InlineWrittenByPlan{};
		std::vector<std::uint8_t> ExtendedWrittenByPlan;
		std::uint8_t* WrittenByPlan = InlineWrittenByPlan.data();
		if (LocalCount > InlineWrittenByPlan.size())
		{
			ExtendedWrittenByPlan.assign(LocalCount, 0u);
			WrittenByPlan = ExtendedWrittenByPlan.data();
		}
		struct FHoistedObjectRollback
		{
			~FHoistedObjectRollback()
			{
				if (bArmed && Vm)
				{
					(void)Vm->JitStoreLocal(Slot, std::move(Previous));
				}
			}

			void Commit() noexcept
			{
				bArmed = false;
			}

			FExecutionContext* Vm = nullptr;
			std::uint16_t Slot = 0xFFFFu;
			FValue Previous;
			bool bArmed = false;
		};
		FHoistedObjectRollback HoistedObjectRollback;
		std::int32_t HoistedObjectCounterBefore = 0;
		if (Plan.bHoistsNonEscapingObject)
		{
			if (!Vm.JitCanUseDirectScalarMethod() || Vm.JitHasInstanceCallInterceptor() || Vm.JitHasMutationCallback() || Plan.HoistedObjectSlot >= LocalCount || Plan.HoistedObjectCounterSlot >= LocalCount)
			{
				return Result;
			}
			FValue* const CounterValue = Vm.JitBorrowDirectLinearLocal(
			    Plan.HoistedObjectCounterSlot);
			const auto* const Counter = CounterValue
			    ? std::get_if<std::int32_t>(CounterValue)
			    : nullptr;
			if (!Counter)
			{
				return Result;
			}
			// Do not materialize the hoisted allocation when the source loop would
			// execute zero times.  The planner accepts the two canonical for/while
			// guards emitted by the compiler; an unfamiliar guard conservatively
			// keeps the authoritative VM path.
			bool bEntryGuardKnown = false;
			bool bEntersLoop = false;
			if (!Plan.Instructions.empty() && Plan.Instructions[0].Opcode == EOpcode::JmpIfLocalGeImm && Plan.Instructions[0].Operand0 == Plan.HoistedObjectCounterSlot)
			{
				bEntryGuardKnown = true;
				bEntersLoop = *Counter < Plan.Instructions[0].Immediate;
			}
			else if (Plan.Instructions.size() >= 4u && Plan.Instructions[0].Opcode == EOpcode::LoadLocal && Plan.Instructions[0].Operand0 == Plan.HoistedObjectCounterSlot && Plan.Instructions[2].Opcode == EOpcode::Lt && Plan.Instructions[3].Opcode == EOpcode::JmpIfFalse)
			{
				if (Plan.Instructions[1].Opcode == EOpcode::LoadLocal)
				{
					FValue* const LimitValue = Vm.JitBorrowDirectLinearLocal(
					    Plan.Instructions[1].Operand0);
					if (const auto* const Limit = LimitValue
					        ? std::get_if<std::int32_t>(LimitValue)
					        : nullptr)
					{
						bEntryGuardKnown = true;
						bEntersLoop = *Counter < *Limit;
					}
				}
				else if (Plan.Instructions[1].Opcode == EOpcode::ConstI32 && !Plan.Instructions[1].bImmediateU32)
				{
					bEntryGuardKnown = true;
					bEntersLoop = *Counter < Plan.Instructions[1].Immediate;
				}
			}
			if (!bEntryGuardKnown || !bEntersLoop)
			{
				return Result;
			}
			HoistedObjectCounterBefore = *Counter;
			FValue* const PreviousObject = Vm.JitBorrowDirectLinearLocal(
			    Plan.HoistedObjectSlot);
			if (!PreviousObject)
			{
				return Result;
			}
			HoistedObjectRollback.Vm = &Vm;
			HoistedObjectRollback.Slot = Plan.HoistedObjectSlot;
			HoistedObjectRollback.Previous = *PreviousObject;
			FValue Object;
			Vm.JitRefineRuntimeErrorLocation(Plan.HoistedObjectInstructionOffset);
			if (!Vm.JitNewObject(Plan.HoistedObjectTypeIndex, Object) || !Vm.JitStoreLocal(Plan.HoistedObjectSlot, std::move(Object)))
			{
				return {ETypedLoopExecution::Failed, HeaderOffset,
				    Plan.HoistedObjectInstructionOffset};
			}
			HoistedObjectRollback.bArmed = true;
		}
		// Fully self-contained fused plans perform their complete binding and
		// runtime-kind validation before their first source-visible mutation. They
		// can therefore execute before the general typed-opcode preflight.
		if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::StateDualStringArrayCompare)
		{
			const auto& D = Plan.StateStringArrays;
			const auto I32 = [&](const std::uint16_t Slot) -> std::int32_t*
			{
				return Bind(Slot) && Locals[Slot].Kind == ELocalKind::I32
				    ? static_cast<std::int32_t*>(Locals[Slot].Storage)
				    : nullptr;
			};
			std::int32_t* const Counter = I32(D.CounterSlot);
			std::int32_t* const Limit = I32(D.LimitSlot);
			std::int32_t* const State = I32(D.StateSlot);
			std::int32_t* const LeftIndex = I32(D.LeftIndexSlot);
			std::int32_t* const RightIndex = I32(D.RightIndexSlot);
			std::int32_t* const Total = I32(D.TotalSlot);
			if (!Counter || !Limit || !State || !LeftIndex || !RightIndex || !Total || !Bind(D.LeftArraySlot) || Locals[D.LeftArraySlot].Kind != ELocalKind::ArrayString || !Bind(D.RightArraySlot) || Locals[D.RightArraySlot].Kind != ELocalKind::ArrayString)
			{
				return Result;
			}
			auto& Left = *static_cast<std::vector<std::string>*>(
			    Locals[D.LeftArraySlot].Storage);
			auto& Right = *static_cast<std::vector<std::string>*>(
			    Locals[D.RightArraySlot].Storage);
			// Hash immutable borrowed elements once. Equality still compares bytes on
			// a hash match, so collisions cannot change language semantics.
			constexpr std::size_t InlineHashCount = 32u;
			std::array<std::size_t, InlineHashCount> InlineLeftHashes{};
			std::array<std::size_t, InlineHashCount> InlineRightHashes{};
			std::vector<std::size_t> HeapLeftHashes;
			std::vector<std::size_t> HeapRightHashes;
			if (Left.size() > InlineHashCount)
			{
				HeapLeftHashes.resize(Left.size());
			}
			if (Right.size() > InlineHashCount)
			{
				HeapRightHashes.resize(Right.size());
			}
			std::size_t* const LeftHashes = Left.size() <= InlineHashCount
			    ? InlineLeftHashes.data()
			    : HeapLeftHashes.data();
			std::size_t* const RightHashes = Right.size() <= InlineHashCount
			    ? InlineRightHashes.data()
			    : HeapRightHashes.data();
			const std::hash<std::string_view> Hash;
			for (std::size_t Index = 0; Index < Left.size(); ++Index)
			{
				LeftHashes[Index] = Hash(Left[Index]);
			}
			for (std::size_t Index = 0; Index < Right.size(); ++Index)
			{
				RightHashes[Index] = Hash(Right[Index]);
			}

			std::int32_t CounterValue = *Counter;
			const std::int32_t LimitValue = *Limit;
			std::int32_t StateValue = *State;
			std::int32_t LeftIndexValue = *LeftIndex;
			std::int32_t RightIndexValue = *RightIndex;
			std::int32_t TotalValue = *Total;
			const auto PhysicalInstructionIndex = [&](const std::size_t LogicalIndex)
			{
				return !D.bHasInlinedCallNop && LogicalIndex > 11u
				    ? LogicalIndex - 1u
				    : LogicalIndex;
			};
			const auto Commit = [&]() noexcept
			{
				*Counter = CounterValue;
				*State = StateValue;
				*LeftIndex = LeftIndexValue;
				*RightIndex = RightIndexValue;
				*Total = TotalValue;
			};
			std::size_t Completed = 0;
			const auto FailAt = [&](const std::size_t LogicalInstruction,
			                        const char* const Error) -> FTypedLoopExecutionResult
			{
				Commit();
				Vm.RecordJitArrayFastPathGet(Completed * 2u);
				const std::uint32_t Offset = Plan.Instructions[PhysicalInstructionIndex(LogicalInstruction)].BytecodeOffset;
				Vm.JitRefineRuntimeErrorLocation(Offset);
				Vm.JitReportRuntimeError(Error);
				return {ETypedLoopExecution::Failed, HeaderOffset, Offset};
			};
			const std::int64_t RemainingIterations =
			    static_cast<std::int64_t>(LimitValue) - CounterValue;
			const bool bCanUseRangeProvenKernel = CounterValue >= 0 && LimitValue >= CounterValue && StateValue >= 0 && D.StateMultiplier >= 0 && D.StateIncrement >= 0 && D.StateModulus > 0 && StateValue < D.StateModulus && D.LeftIndexModulus > 0 && D.RightIndexModulus > 0 && static_cast<std::size_t>(D.LeftIndexModulus) <= Left.size() && static_cast<std::size_t>(D.RightIndexModulus) <= Right.size() && D.CounterMask >= 0 && TotalValue >= 0 && static_cast<std::int64_t>(D.StateModulus - 1) * D.StateMultiplier + D.StateIncrement <= std::numeric_limits<std::int32_t>::max() && static_cast<std::int64_t>(D.StateModulus - 1) + D.CounterMask <= std::numeric_limits<std::int32_t>::max() && RemainingIterations <= static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) - TotalValue;
			if (bCanUseRangeProvenKernel)
			{
				const FFastStringCompareResult Fast = ExecuteFastStringCompareKernel(
				    D, Left, Right, LeftHashes, RightHashes, CounterValue, LimitValue,
				    StateValue, TotalValue);
				CounterValue = Fast.Counter;
				StateValue = Fast.State;
				TotalValue = Fast.Total;
				Completed = Fast.Completed;
				if (Completed != 0u)
				{
					LeftIndexValue = Fast.LeftIndex;
					RightIndexValue = Fast.RightIndex;
				}
			}
			else
			{
				while (CounterValue < LimitValue)
				{
					std::int32_t Product = 0;
					std::int32_t Sum = 0;
					if (!Runtime::CheckedMul(StateValue, D.StateMultiplier, Product))
					{
						return FailAt(6u, "integer overflow");
					}
					if (!Runtime::CheckedAdd(Product, D.StateIncrement, Sum))
					{
						return FailAt(8u, "integer overflow");
					}
					if (D.StateModulus == 0)
					{
						return FailAt(10u, "division by zero");
					}
					if (Sum == std::numeric_limits<std::int32_t>::min() && D.StateModulus == -1)
					{
						return FailAt(10u, "integer overflow");
					}
					StateValue = Sum % D.StateModulus;
					if (D.LeftIndexModulus == 0)
					{
						return FailAt(15u, "division by zero");
					}
					if (StateValue == std::numeric_limits<std::int32_t>::min() && D.LeftIndexModulus == -1)
					{
						return FailAt(15u, "integer overflow");
					}
					LeftIndexValue = StateValue % D.LeftIndexModulus;
					if (!Runtime::CheckedAdd(StateValue,
					        CounterValue & D.CounterMask, Sum))
					{
						return FailAt(21u, "integer overflow");
					}
					if (D.RightIndexModulus == 0)
					{
						return FailAt(23u, "division by zero");
					}
					if (Sum == std::numeric_limits<std::int32_t>::min() && D.RightIndexModulus == -1)
					{
						return FailAt(23u, "integer overflow");
					}
					RightIndexValue = Sum % D.RightIndexModulus;
					if (LeftIndexValue < 0 || static_cast<std::size_t>(LeftIndexValue) >= Left.size())
					{
						return FailAt(27u, "array index out of bounds");
					}
					if (RightIndexValue < 0 || static_cast<std::size_t>(RightIndexValue) >= Right.size())
					{
						return FailAt(30u, "array index out of bounds");
					}
					const std::size_t L = static_cast<std::size_t>(LeftIndexValue);
					const std::size_t R = static_cast<std::size_t>(RightIndexValue);
					if (LeftHashes[L] == RightHashes[R] && Left[L] == Right[R])
					{
						if (!Runtime::CheckedAdd(TotalValue, 1, Sum))
						{
							return FailAt(33u, "integer overflow");
						}
						TotalValue = Sum;
					}
					if (!Runtime::CheckedAdd(CounterValue, 1, Sum))
					{
						return FailAt(34u, "integer overflow");
					}
					CounterValue = Sum;
					++Completed;
				}
			}
			Commit();
			Vm.RecordJitArrayFastPathGet(Completed * 2u);
			return {ETypedLoopExecution::Completed, Plan.ExitOffset, Plan.HeaderOffset};
		}
		if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::StateMapF64Update)
		{
			// These are compiler-generated temporaries whose first access in the
			// recognized loop is an F64 StoreLocal. FValue's default alternative is
			// i32, which would otherwise make the preflight reject the plan at every
			// header. Initialize only proven store-before-load slots.
			const auto PrepareF64Temporary = [&](const std::uint16_t Slot)
			{
				FValue* const Value = Vm.JitBorrowDirectLinearLocal(Slot);
				if (Value && !std::holds_alternative<double>(*Value))
				{
					*Value = 0.0;
				}
			};
			PrepareF64Temporary(Plan.StateMapF64.ValueSlot);
			PrepareF64Temporary(Plan.StateMapF64.StoredValueSlot);
		}
		for (const FTypedLoopInstruction& Instruction : Plan.Instructions)
		{
			if (Instruction.Opcode == EOpcode::StoreLocal)
			{
				WrittenByPlan[Instruction.Operand0] |= 1u;
				continue;
			}
			if ((Instruction.Opcode == EOpcode::LoadLocal && (WrittenByPlan[Instruction.Operand0] & 1u) == 0u) || ((Instruction.Opcode == EOpcode::IncLocal || Instruction.Opcode == EOpcode::AddLocalImm || Instruction.Opcode == EOpcode::JmpIfLocalGeImm) && (WrittenByPlan[Instruction.Operand0] & 1u) == 0u))
			{
				(void)Bind(Instruction.Operand0);
			}
			if (Instruction.Opcode == EOpcode::LoadLocal && !Plan.GenericPureMethods.empty() && (WrittenByPlan[Instruction.Operand0] & 2u) == 0u)
			{
				WrittenByPlan[Instruction.Operand0] |= 2u;
				// A conditional first StoreLocal can leave the entry receiver alive.
				// Inspect only this plan input, without freezing its binding kind.
				if (Locals[Instruction.Operand0].Kind == ELocalKind::Unsupported)
				{
					const FValue* const Value = Vm.JitBorrowDirectLinearLocal(Instruction.Operand0);
					const auto RecordValue = [&](const FValue& Input)
					{
						const auto* const Handle = std::get_if<FObjectHandle>(&Input);
						if (Handle && *Handle)
						{
							RecordLifetimeReceiver(**Handle);
						}
					};
					if (Value)
					{
						RecordValue(*Value);
						const auto* const Array = std::get_if<FDynamicArrayHandle>(Value);
						if (Array && *Array && !(*Array)->HasScalarElements())
						{
							for (const FValue& Element : (*Array)->Elements)
							{
								RecordValue(Element);
								if (bHasLifetimeVirtualReceiver)
								{
									break;
								}
							}
						}
					}
				}
			}
			if (Instruction.Opcode == EOpcode::AddLocalLocal)
			{
				if ((WrittenByPlan[Instruction.Operand0] & 1u) == 0u)
				{
					(void)Bind(Instruction.Operand0);
				}
				if ((WrittenByPlan[Instruction.Operand1] & 1u) == 0u)
				{
					(void)Bind(Instruction.Operand1);
				}
			}
		}
		if (Plan.bHoistsNonEscapingObject && !PrepareDirectFieldMethods()) return Result;
		if (!Plan.GenericPureMethods.empty())
		{
			if (!Vm.JitCanUseDirectScalarMethod() || Vm.JitHasInstanceCallInterceptor() || Vm.JitHasMutationCallback())
			{
				return Result;
			}
			if (bHasLifetimeVirtualReceiver) return Result;
			PolymorphicMethods.resize(Plan.Instructions.size());
			PolymorphicCandidates.resize(Plan.Instructions.size());
			PolymorphicCaches.resize(Plan.Instructions.size());
			for (std::size_t Index = 0; Index < Plan.Instructions.size(); ++Index)
			{
				const FTypedLoopInstruction& Instruction = Plan.Instructions[Index];
				if (Instruction.Opcode != EOpcode::CallMethod || Instruction.Immediate != 0 || Instruction.ArgumentCount != 1u || Instruction.Operand1 != 0xFFFFu)
				{
					continue;
				}
				auto& Candidates = PolymorphicCandidates[Index];
				Candidates.reserve(Plan.GenericPureMethods.size());
				for (const auto& Expression : Plan.GenericPureMethods)
				{
					if (Expression.NativeCode)
					{
						Candidates.push_back({nullptr, Expression.NativeCode,
						    Expression.FunctionIndex});
						if (!Expression.FieldNameIndices.empty())
						{
							Candidates.back().FieldNameIndices = &Expression.FieldNameIndices;
						}
					}
				}
				if (Candidates.empty())
				{
					return Result;
				}
				auto& Cache = PolymorphicCaches[Index];
				const bool bFieldAware = std::any_of(Candidates.begin(), Candidates.end(),
					[](const auto& Candidate) {return Candidate.FieldNameIndices != nullptr;});
				const std::size_t ReceiverCount = Instruction.ReceiverArraySlot < NativeObjectElementBindings.size()
					? NativeObjectElementBindings[Instruction.ReceiverArraySlot].size() : 0u;
				Cache.resize(bFieldAware ? std::min<std::size_t>(ReceiverCount, 256u) : std::max<std::size_t>(Candidates.size(), 16u));
				auto& Method = PolymorphicMethods[Index];
				Method.Candidates = Candidates.data();
				Method.CandidateCount = Candidates.size();
				Method.Cache = Cache.data();
				Method.CacheCapacity = Cache.size();
				Method.MethodNameIndex = Instruction.Operand0;
				if (bFieldAware)
				{
					// Resolve every actual receiver before entering native code. Cache
					// layouts per immutable type identity, not per retained object, so
					// large arrays require no per-object field-pointer allocations.
					if (Instruction.ReceiverArraySlot >= NativeObjectElementBindings.size()) return Result;
					auto& Receivers = NativeObjectElementBindings[Instruction.ReceiverArraySlot];
					const auto& ArrayBinding = Locals[Instruction.ReceiverArraySlot];
					if (Receivers.empty() || ArrayBinding.Kind != ELocalKind::ArrayObject || !ArrayBinding.Storage) return Result;
					auto& Sources = *static_cast<std::vector<FValue>*>(ArrayBinding.Storage);
					if (Sources.size() < Receivers.size()) return Result;
					for (std::size_t ReceiverIndex = 0; ReceiverIndex < Receivers.size(); ++ReceiverIndex)
					{
						auto& Receiver = Receivers[ReceiverIndex];
						const auto* Handle = std::get_if<FObjectHandle>(&Sources[ReceiverIndex]);
						if (!Handle || !*Handle) return Result;
						auto& Object = **Handle;
						// GetIndexCopy may run a user-defined copy constructor for a
						// struct. A class receiver preserves identity on that boundary.
						if (Object.Kind != EScriptObjectKind::Class) return Result;
						std::uint16_t TypeIndex = 0xFFFFu;
						if (!Vm.JitGetObjectLayout(Object, TypeIndex)) return Result;
						Receiver.TypeIndex = TypeIndex;
						const auto* Identity = &Object.GetTypeName();
						auto Found = std::find_if(Cache.begin(), Cache.begin() + Method.CacheCount,
							[&](const auto& Target) {return Target.TypeIdentity == Identity && Target.TypeIndex == TypeIndex;});
						if (Found == Cache.begin() + Method.CacheCount)
						{
							if (Method.CacheCount == Method.CacheCapacity) return Result;
							std::uint16_t FunctionIndex = 0xFFFFu;
							bool bHandled = false;
							if (!Vm.JitResolveDirectI32MethodTarget(*Handle, Instruction.Operand0, 1u,
								Instruction.BytecodeOffset, FunctionIndex, bHandled))
							{
								return {ETypedLoopExecution::Failed, HeaderOffset, Instruction.BytecodeOffset};
							}
							const auto Candidate = std::find_if(Candidates.begin(), Candidates.end(),
								[&](const auto& Target) {return Target.FunctionIndex == FunctionIndex;});
							if (Candidate == Candidates.end() || Vm.JitRequiresCoreVirtualDispatch(Object, FunctionIndex)) return Result;
							Found = Cache.begin() + Method.CacheCount++;
							*Found = *Candidate;
							Found->TypeIdentity = Identity;
							Found->TypeIndex = TypeIndex;
							if (Found->FieldNameIndices)
							{
								for (std::size_t Field = 0; Field < Found->FieldNameIndices->size(); ++Field)
								{
									std::int32_t* Storage = nullptr;
									if (!Vm.JitBorrowObjectI32Field(*Handle, (*Found->FieldNameIndices)[Field], Found->FieldIndices[Field], Storage)) return Result;
								}
							}
						}
						if (Found->FieldNameIndices)
						{
							for (std::size_t Field = 0; Field < Found->FieldNameIndices->size(); ++Field)
							{
								const auto Slot = Found->FieldIndices[Field];
								if (Slot >= Object.Fields.size() || !std::holds_alternative<std::int32_t>(Object.Fields[Slot])) return Result;
							}
						}
						if (Vm.JitRequiresCoreVirtualDispatch(Object, Found->FunctionIndex)) return Result;
					}
				}
			}
		}
		if (Plan.bUsesDirectScalarFields)
		{
			DirectFields.resize(Plan.Instructions.size());
			// A host mutation callback may synchronously re-enter and change object
			// state/layout. The ordinary path re-resolves after every callback; a plan
			// that retains raw slots therefore declines before its first mutation.
			if (Plan.bMutatesDirectScalarFields && Vm.JitHasMutationCallback())
			{
				return Result;
			}
			for (std::size_t Index = 0; Index < Plan.Instructions.size(); ++Index)
			{
				const auto& Instruction = Plan.Instructions[Index];
				if (Instruction.Opcode != EOpcode::LoadField && Instruction.Opcode != EOpcode::StoreField && Instruction.Opcode != EOpcode::LoadFieldKeep && Instruction.Opcode != EOpcode::IncField)
				{
					continue;
				}
				FDirectFieldBinding& Field = DirectFields[Index];
				std::int32_t* I32Storage = nullptr;
				if (!Vm.JitBorrowLocalI32Field(
				        Instruction.Operand1, Instruction.Operand0,
				        Field.Object, Field.FieldIndex, I32Storage))
				{
					return Result;
				}
				if (I32Storage)
				{
					Field.Storage = I32Storage;
					Field.Kind = FDirectFieldBinding::EKind::I32;
				}
				else
				{
					std::uint32_t* U32Storage = nullptr;
					if (!Vm.JitBorrowLocalU32Field(
					        Instruction.Operand1, Instruction.Operand0,
					        Field.Object, Field.FieldIndex, U32Storage))
					{
						return Result;
					}
					if (U32Storage)
					{
						Field.Storage = U32Storage;
						Field.Kind = FDirectFieldBinding::EKind::U32;
					}
					else
					{
						float* F32Storage = nullptr;
						if (!Vm.JitBorrowLocalF32Field(
						        Instruction.Operand1, Instruction.Operand0,
						        Field.Object, Field.FieldIndex, F32Storage))
						{
							return Result;
						}
						if (F32Storage)
						{
							Field.Storage = F32Storage;
							Field.Kind = FDirectFieldBinding::EKind::F32;
						}
						else
						{
							double* F64Storage = nullptr;
							if (!Vm.JitBorrowLocalF64Field(
							        Instruction.Operand1, Instruction.Operand0,
							        Field.Object, Field.FieldIndex, F64Storage) ||
							    !F64Storage)
							{
								return Result;
							}
							Field.Storage = F64Storage;
							Field.Kind = FDirectFieldBinding::EKind::F64;
						}
					}
				}
				if (!Field.Object || !Field.Storage)
				{
					return Result;
				}
			}
		}
		FScriptObject* DirectFieldMutationObject = nullptr;
		if (Plan.bMutatesDirectScalarFields)
		{
			for (std::size_t Index = 0; Index < Plan.Instructions.size(); ++Index)
			{
				const EOpcode Opcode = Plan.Instructions[Index].Opcode;
				if (Opcode != EOpcode::StoreField && Opcode != EOpcode::IncField)
				{
					continue;
				}
				FScriptObject* const Object = DirectFields[Index].Object;
				if (!Object)
				{
					return Result;
				}
				if (!DirectFieldMutationObject)
				{
					DirectFieldMutationObject = Object;
				}
			}
		}
		// A concrete but unsupported local (for example set<string> or an escaped
		// boxed container) must be rejected before the first source write. FValue's
		// default alternative is i32, so compiler temporaries are already bindable and
		// do not need a separate uninitialized sentinel here.
		std::fill_n(WrittenByPlan, LocalCount, std::uint8_t{0});
		for (const FTypedLoopInstruction& Instruction : Plan.Instructions)
		{
			if (Instruction.Opcode == EOpcode::StoreLocal)
			{
				WrittenByPlan[Instruction.Operand0] = true;
				continue;
			}
			if (Instruction.Opcode != EOpcode::LoadLocal || WrittenByPlan[Instruction.Operand0] || Locals[Instruction.Operand0].Kind != ELocalKind::Unsupported)
			{
				continue;
			}
			return Result;
		}

		if (Plan.NativeI32Code || Plan.bWideCandidate || Plan.bOwnsStringTemporaries)
		{
			// Validate the complete typed CFG before the first source write.  The
			// generated kernel is intentionally monomorphic: any runtime kind or join
			// mismatch keeps the portable typed executor authoritative.
			using K = ETypedLoopLocalKind;
			std::vector<K> LocalKinds(LocalCount, K::Unsupported);
			std::vector<K> InstructionKinds(
			    Plan.Instructions.size(), K::Unsupported);
			std::vector<std::uint8_t> Referenced(LocalCount, 0u);
			std::vector<std::uint8_t> FirstAccessSeen(LocalCount, 0u);
			std::vector<std::uint8_t> FirstAccessIsStore(LocalCount, 0u);
			for (const FTypedLoopInstruction& Instruction : Plan.Instructions)
			{
				const auto MarkAccess = [&](const std::uint16_t Slot,
				                            const bool bStore)
				{
					Referenced[Slot] = 1u;
					if (!FirstAccessSeen[Slot])
					{
						FirstAccessSeen[Slot] = 1u;
						FirstAccessIsStore[Slot] = bStore ? 1u : 0u;
					}
				};
				switch (Instruction.Opcode)
				{
				case EOpcode::LoadLocal:
				case EOpcode::IncLocal:
				case EOpcode::AddLocalImm:
				case EOpcode::JmpIfLocalGeImm:
					{
						MarkAccess(Instruction.Operand0, false);
						break;
					}
				case EOpcode::StoreLocal:
					{
						MarkAccess(Instruction.Operand0, true);
						break;
					}
				case EOpcode::AddLocalLocal:
					{
						MarkAccess(Instruction.Operand0, false);
						MarkAccess(Instruction.Operand1, false);
						break;
					}
				default:
					{
						break;
					}
				}
			}
			bool bNativeKindsValid = true;
			for (std::size_t Slot = 0; Slot < LocalCount; ++Slot)
			{
				if (!Referenced[Slot])
				{
					continue;
				}
				if (FirstAccessIsStore[Slot])
				{
					// Compiler temporaries are established by their first StoreLocal.
					// Binding the default FValue alternative here would incorrectly
					// freeze an Array/Object alias temporary as i32.
					continue;
				}
				if (Locals[Slot].Kind == K::Unsupported && !Bind(static_cast<std::uint16_t>(Slot)))
				{
					bNativeKindsValid = false;
					break;
				}
				LocalKinds[Slot] = Locals[Slot].Kind;
				if (LocalKinds[Slot] != K::I32 && LocalKinds[Slot] != K::U32 && LocalKinds[Slot] != K::I64 && LocalKinds[Slot] != K::U64 && LocalKinds[Slot] != K::Bool && LocalKinds[Slot] != K::String && LocalKinds[Slot] != K::ArrayI32 && LocalKinds[Slot] != K::ArrayU32 && LocalKinds[Slot] != K::ArrayString && LocalKinds[Slot] != K::MapI32I32 && LocalKinds[Slot] != K::MapI32U32 && LocalKinds[Slot] != K::MapU32I32 && LocalKinds[Slot] != K::MapU32U32 && LocalKinds[Slot] != K::MapStringI32 && LocalKinds[Slot] != K::SetI32 && LocalKinds[Slot] != K::SetU32 && LocalKinds[Slot] != K::SetString && LocalKinds[Slot] != K::ArrayObject && LocalKinds[Slot] != K::Object)
				{
					bNativeKindsValid = false;
					break;
				}
			}

			const auto IsInteger = [](const K Kind)
			{
				return Kind == K::I32 || Kind == K::U32 || Kind == K::I64 || Kind == K::U64;
			};
			const auto CommonInteger = [&](const K Left, const K Right) -> K
			{
				if (!IsInteger(Left) || !IsInteger(Right)) return K::Unsupported;
				if (Left == Right) return Left;
				if ((Left == K::I32 && Right == K::I64) || (Left == K::I64 && Right == K::I32)) return K::I64;
				if ((Left == K::U32 && Right == K::U64) || (Left == K::U64 && Right == K::U32)) return K::U64;
				return K::Unsupported;
			};
			FPackedKindStates States(Plan.Instructions, Plan.MaxStackDepth);
			if (!States.IsValid()) return Result;
			std::vector<K> Stack;
			Stack.reserve(Plan.MaxStackDepth);
			std::vector<std::uint32_t> Worklist;
			Worklist.reserve(Plan.Instructions.size());
			if (bNativeKindsValid)
			{
				bool Added = false;
				bNativeKindsValid = States.Merge(0u, Stack, Added);
				if (bNativeKindsValid)
				{
					Worklist.push_back(0u);
				}
			}
			const auto Merge = [&](const std::uint32_t Target,
			                       const std::vector<K>& Stack) -> bool
			{
				bool Added = false;
				if (!States.Merge(Target, Stack, Added)) return false;
				if (Added)
				{
					Worklist.push_back(Target);
				}
				return true;
			};
			while (bNativeKindsValid && !Worklist.empty())
			{
				const std::uint32_t Pc = Worklist.back();
				Worklist.pop_back();
				const FTypedLoopInstruction& Instruction = Plan.Instructions[Pc];
				if (!States.Load(Pc, Stack))
				{
					bNativeKindsValid = false;
					break;
				}
				const auto PopKind = [&](K& Out) -> bool
				{
					if (Stack.empty())
					{
						return false;
					}
					Out = Stack.back();
					Stack.pop_back();
					return true;
				};
				const auto RequireBinaryScalar = [&](const bool bComparison) -> bool
				{
					K Right = K::Unsupported;
					K Left = K::Unsupported;
					if (!PopKind(Right) || !PopKind(Left) || CommonInteger(Left, Right) == K::Unsupported)
					{
						return false;
					}
					InstructionKinds[Pc] = CommonInteger(Left, Right);
					Stack.push_back(bComparison ? K::Bool : InstructionKinds[Pc]);
					return true;
				};
				bool bHasFallthrough = true;
				switch (Instruction.Opcode)
				{
				case EOpcode::Nop:
					{
						break;
					}
				case EOpcode::ConstI32:
					{
						InstructionKinds[Pc] = Instruction.WideImmediateKind;
						Stack.push_back(Instruction.WideImmediateKind != K::Unsupported
						        ? Instruction.WideImmediateKind
						        : Instruction.bImmediateU32 ? K::U32
						                                    : K::I32);
						break;
					}
				case EOpcode::ConstStr:
					{
						InstructionKinds[Pc] = K::String;
						Stack.push_back(K::String);
						break;
					}
				case EOpcode::ConstBool:
					{
						Stack.push_back(K::Bool);
						break;
					}
				case EOpcode::LoadLocal:
					{
						if (Instruction.Operand0 >= LocalKinds.size() || LocalKinds[Instruction.Operand0] == K::Unsupported)
						{
							bNativeKindsValid = false;
						}
						else
						{
							InstructionKinds[Pc] = LocalKinds[Instruction.Operand0];
							Stack.push_back(LocalKinds[Instruction.Operand0]);
						}
						break;
					}
				case EOpcode::StoreLocal:
					{
						K Value = K::Unsupported;
						if (!PopKind(Value) || Instruction.Operand0 >= LocalKinds.size())
						{
							bNativeKindsValid = false;
						}
						else if (LocalKinds[Instruction.Operand0] == K::Unsupported)
						{
							LocalKinds[Instruction.Operand0] = Value;
						}
						else if (Value != LocalKinds[Instruction.Operand0])
						{
							bNativeKindsValid = false;
						}
						InstructionKinds[Pc] = Value;
						// Owning conversion plans commit strings at each StoreLocal. A live
						// borrowed stack string could observe that write through an older view.
						if (Plan.bOwnsStringTemporaries && Value == K::String && std::find(Stack.begin(), Stack.end(), K::String) != Stack.end())
						{
							bNativeKindsValid = false;
						}
						break;
					}
				case EOpcode::IncLocal:
				case EOpcode::AddLocalImm:
					{
						bNativeKindsValid = Instruction.Operand0 < LocalKinds.size() && IsInteger(LocalKinds[Instruction.Operand0]);
						if (bNativeKindsValid)
						{
							InstructionKinds[Pc] = LocalKinds[Instruction.Operand0];
						}
						break;
					}
				case EOpcode::AddLocalLocal:
					{
						bNativeKindsValid = Instruction.Operand0 < LocalKinds.size() && Instruction.Operand1 < LocalKinds.size() && LocalKinds[Instruction.Operand0] == LocalKinds[Instruction.Operand1] &&
							(Instruction.bTextAdd ? LocalKinds[Instruction.Operand0] == K::String : IsInteger(LocalKinds[Instruction.Operand0]));
						// A compact append mutates its destination without consuming the
						// operand stack. Preserve any earlier language string snapshot.
						if (Instruction.bTextAdd && std::find(Stack.begin(), Stack.end(), K::String) != Stack.end())
						{
							bNativeKindsValid = false;
						}
						if (bNativeKindsValid)
						{
							InstructionKinds[Pc] = LocalKinds[Instruction.Operand0];
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
						if (Instruction.bTextAdd)
						{
							K Right = K::Unsupported, Left = K::Unsupported;
							bNativeKindsValid = PopKind(Right) && PopKind(Left) && Right == K::String && Left == K::String;
							InstructionKinds[Pc] = K::String;
							Stack.push_back(K::String);
						}
						else
						{
							bNativeKindsValid = RequireBinaryScalar(false);
						}
						break;
					}
				case EOpcode::Shl:
				case EOpcode::Shr:
					{
						K Right = K::Unsupported, Left = K::Unsupported;
						bNativeKindsValid = PopKind(Right) && PopKind(Left) && IsInteger(Right) && IsInteger(Left);
						if (bNativeKindsValid)
						{
							InstructionKinds[Pc] = Left;
							Stack.push_back(Left);
						}
						break;
					}

				case EOpcode::Eq:
				case EOpcode::Ne:
					{
						K Right = K::Unsupported;
						K Left = K::Unsupported;
						bNativeKindsValid = PopKind(Right) && PopKind(Left) && ((CommonInteger(Left, Right) != K::Unsupported) || (Right == K::String && Left == K::String));
						if (bNativeKindsValid)
						{
							InstructionKinds[Pc] = Left == K::String ? K::String : CommonInteger(Left, Right);
							Stack.push_back(K::Bool);
						}
						break;
					}
				case EOpcode::Lt:
				case EOpcode::Le:
				case EOpcode::Gt:
				case EOpcode::Ge:
					{
						bNativeKindsValid = RequireBinaryScalar(true);
						break;
					}
				case EOpcode::Neg:
				case EOpcode::BitNot:
					{
						bNativeKindsValid = !Stack.empty() && IsInteger(Stack.back());
						if (bNativeKindsValid)
						{
							InstructionKinds[Pc] = Stack.back();
						}
						break;
					}
				case EOpcode::Not:
					{
						bNativeKindsValid = !Stack.empty() && Stack.back() == K::Bool;
						break;
					}
				case EOpcode::CallBuiltin:
					{
						if (Instruction.bTextIdentity)
						{
							bNativeKindsValid = !Stack.empty() && Stack.back() == K::String;
							InstructionKinds[Pc] = K::String;
							break;
						}
						if (static_cast<EBuiltinId>(Instruction.Immediate) == EBuiltinId::CharCode)
						{
							K Index = K::Unsupported, Text = K::Unsupported;
							bNativeKindsValid = Instruction.ArgumentCount == 2u && PopKind(Index) && PopKind(Text)
								&& IsInteger(Index) && Text == K::String;
							InstructionKinds[Pc] = Index;
							Stack.push_back(K::I32);
							break;
						}
						K Argument = K::Unsupported;
						const EBuiltinId Builtin = static_cast<EBuiltinId>(
						    Instruction.Immediate);
						bNativeKindsValid = Instruction.ArgumentCount == 1u && PopKind(Argument) && ((Builtin == EBuiltinId::Len && (Argument == K::String || Argument == K::ArrayI32 || Argument == K::ArrayU32)) || (IsInteger(Argument) && (Builtin == EBuiltinId::ToString || Builtin == EBuiltinId::ToInt || Builtin == EBuiltinId::ToUnsigned || Builtin == EBuiltinId::ToInt64 || Builtin == EBuiltinId::ToUInt64)));
						if (bNativeKindsValid)
						{
							InstructionKinds[Pc] = Argument;
							Stack.push_back(Builtin == EBuiltinId::ToString                        ? K::String
							        : Builtin == EBuiltinId::ToInt64 || Builtin == EBuiltinId::Len ? K::I64
							        : Builtin == EBuiltinId::ToUInt64                              ? K::U64
							        : Builtin == EBuiltinId::ToUnsigned                            ? K::U32
							                                                                       : K::I32);
						}
						break;
					}
				case EOpcode::Dup:
					{
						if (Stack.empty())
						{
							bNativeKindsValid = false;
						}
						else
						{
							Stack.push_back(Stack.back());
						}
						break;
					}
				case EOpcode::Pop:
					{
						K Ignored = K::Unsupported;
						bNativeKindsValid = PopKind(Ignored);
						break;
					}
				case EOpcode::GetIndexCopy:
					{
						K IndexKind = K::Unsupported;
						K ContainerKind = K::Unsupported;
						bNativeKindsValid = PopKind(IndexKind) && PopKind(ContainerKind);
						if (!bNativeKindsValid)
						{
							break;
						}
						if (IsInteger(IndexKind) && ContainerKind == K::ArrayI32)
						{
							Stack.push_back(K::I32);
						}
						else if (IsInteger(IndexKind) && ContainerKind == K::ArrayU32)
						{
							Stack.push_back(K::U32);
						}
						else if ((IndexKind == K::I32 || IndexKind == K::U32) && ContainerKind == K::ArrayObject)
						{
							Stack.push_back(K::Object);
						}
						else if (IndexKind == K::I32 && ContainerKind == K::ArrayString)
						{
							Stack.push_back(K::String);
						}
						else if (IndexKind == K::I32 && ContainerKind == K::MapI32I32)
						{
							Stack.push_back(K::I32);
						}
						else if (IndexKind == K::I32 && ContainerKind == K::MapI32U32)
						{
							Stack.push_back(K::U32);
						}
						else if (IndexKind == K::U32 && ContainerKind == K::MapU32I32)
						{
							Stack.push_back(K::I32);
						}
						else if (IndexKind == K::U32 && ContainerKind == K::MapU32U32)
						{
							Stack.push_back(K::U32);
						}
						else if (IndexKind == K::String && ContainerKind == K::MapStringI32)
						{
							Stack.push_back(K::I32);
						}
						else
						{
							bNativeKindsValid = false;
						}
						if (bNativeKindsValid)
						{
							InstructionKinds[Pc] = IndexKind;
						}
						break;
					}
				case EOpcode::SetIndex:
					{
						K ValueKind = K::Unsupported;
						K IndexKind = K::Unsupported;
						K ContainerKind = K::Unsupported;
						bNativeKindsValid = PopKind(ValueKind) && PopKind(IndexKind) && PopKind(ContainerKind) && (((ValueKind == K::I32 && ContainerKind == K::ArrayI32 || ValueKind == K::U32 && ContainerKind == K::ArrayU32) && IsInteger(IndexKind)) || (ValueKind == K::I32 && IndexKind == K::I32 && (ContainerKind == K::ArrayI32 || ContainerKind == K::MapI32I32)) || (ValueKind == K::U32 && (ContainerKind == K::ArrayU32 && (IndexKind == K::I32 || IndexKind == K::U32))) || (ValueKind == K::U32 && IndexKind == K::I32 && ContainerKind == K::MapI32U32) || (ValueKind == K::I32 && IndexKind == K::U32 && ContainerKind == K::MapU32I32) || (ValueKind == K::U32 && IndexKind == K::U32 && ContainerKind == K::MapU32U32) || (ValueKind == K::I32 && IndexKind == K::String && ContainerKind == K::MapStringI32));
						if (bNativeKindsValid)
						{
							InstructionKinds[Pc] = IndexKind;
							Stack.push_back(ContainerKind);
						}
						break;
					}
				case EOpcode::CallMethod:
				case EOpcode::CallBoundMethod:
					{
						K Argument = K::Unsupported;
						K Receiver = K::Unsupported;
						if (Instruction.FieldMethodIndex != 0xFFFFu)
						{
							bNativeKindsValid = Instruction.ArgumentCount <= 8u;
							for (std::uint8_t Index = 0; bNativeKindsValid && Index < Instruction.ArgumentCount; ++Index)
							{
								bNativeKindsValid = PopKind(Argument) && Argument == K::I32;
							}
							bNativeKindsValid = bNativeKindsValid && PopKind(Receiver) && Receiver == K::Object
								&& Pc < DirectMethods.size() && DirectMethods[Pc].Object && DirectMethods[Pc].FieldCode;
							if (bNativeKindsValid)
							{
								Stack.push_back(K::I32);
							}
							break;
						}
						bNativeKindsValid = Instruction.ArgumentCount == 1u && PopKind(Argument) && PopKind(Receiver);
						if (!bNativeKindsValid)
						{
							break;
						}
						InstructionKinds[Pc] = Argument;
						if (Instruction.Immediate == 0 && Argument == K::I32 && Receiver == K::Object && ((Instruction.Operand1 != 0xFFFFu && Pc < DirectMethods.size() && DirectMethods[Pc].Object && (DirectMethods[Pc].Storage || DirectMethods[Pc].FieldCode)) || (Instruction.Operand1 == 0xFFFFu && Pc < PolymorphicMethods.size() && PolymorphicMethods[Pc].CandidateCount != 0u)))
						{
							Stack.push_back(K::I32);
						}
						else if (Instruction.Immediate == 1 && ((Argument == K::I32 && (Receiver == K::MapI32I32 || Receiver == K::MapI32U32 || Receiver == K::SetI32)) || (Argument == K::U32 && (Receiver == K::MapU32I32 || Receiver == K::MapU32U32 || Receiver == K::SetU32)) || (Argument == K::String && (Receiver == K::MapStringI32 || Receiver == K::SetString))))
						{
							Stack.push_back(K::Bool);
						}
						else if (Instruction.Immediate == 2 && ((Argument == K::I32 && Receiver == K::ArrayI32) || (Argument == K::U32 && Receiver == K::ArrayU32)))
						{
							Stack.push_back(Receiver);
						}
						else if (Instruction.Immediate == 3 && ((Argument == K::I32 && Receiver == K::SetI32) || (Argument == K::U32 && Receiver == K::SetU32)))
						{
							Stack.push_back(Receiver);
						}
						else
						{
							bNativeKindsValid = false;
						}
						break;
					}
				case EOpcode::LoadField:
				case EOpcode::LoadFieldKeep:
					{
						K Receiver = K::Unsupported;
						bNativeKindsValid = PopKind(Receiver) && Receiver == K::Object && Pc < DirectFields.size() && (DirectFields[Pc].Kind == FDirectFieldBinding::EKind::I32 || DirectFields[Pc].Kind == FDirectFieldBinding::EKind::U32);
						if (bNativeKindsValid)
						{
							if (Instruction.Opcode == EOpcode::LoadFieldKeep)
							{
								Stack.push_back(K::Object);
							}
							Stack.push_back(DirectFields[Pc].Kind == FDirectFieldBinding::EKind::U32 ? K::U32 : K::I32);
						}
						break;
					}
				case EOpcode::StoreField:
					{
						K Value = K::Unsupported;
						K Receiver = K::Unsupported;
						bNativeKindsValid = PopKind(Value) && PopKind(Receiver) && Receiver == K::Object && Pc < DirectFields.size() && ((Value == K::I32 && DirectFields[Pc].Kind == FDirectFieldBinding::EKind::I32) || (Value == K::U32 && DirectFields[Pc].Kind == FDirectFieldBinding::EKind::U32));
						if (bNativeKindsValid)
						{
							Stack.push_back(Value);
						}
						break;
					}
				case EOpcode::IncField:
					{
						K Receiver = K::Unsupported;
						bNativeKindsValid = PopKind(Receiver) && Receiver == K::Object && Pc < DirectFields.size() && (DirectFields[Pc].Kind == FDirectFieldBinding::EKind::I32 || DirectFields[Pc].Kind == FDirectFieldBinding::EKind::U32);
						if (bNativeKindsValid)
						{
							InstructionKinds[Pc] = DirectFields[Pc].Kind == FDirectFieldBinding::EKind::U32 ? K::U32 : K::I32;
						}
						break;
					}
				case EOpcode::JmpIfFalse:
					{
						K Condition = K::Unsupported;
						bNativeKindsValid = PopKind(Condition) && Condition == K::Bool;
						if (bNativeKindsValid && Instruction.BranchInstruction != InvalidInstruction)
						{
							bNativeKindsValid = Merge(
							    Instruction.BranchInstruction, Stack);
						}
						break;
					}
				case EOpcode::JmpIfLocalGeImm:
					{
						bNativeKindsValid = Instruction.Operand0 < LocalKinds.size() && IsInteger(LocalKinds[Instruction.Operand0]);
						if (bNativeKindsValid)
						{
							InstructionKinds[Pc] = LocalKinds[Instruction.Operand0];
						}
						if (bNativeKindsValid && Instruction.BranchInstruction != InvalidInstruction)
						{
							bNativeKindsValid = Merge(
							    Instruction.BranchInstruction, Stack);
						}
						break;
					}
				case EOpcode::Jmp:
					{
						bHasFallthrough = false;
						if (Instruction.BranchInstruction != InvalidInstruction)
						{
							bNativeKindsValid = Merge(
							    Instruction.BranchInstruction, Stack);
						}
						break;
					}
				default:
					{
						bNativeKindsValid = false;
						break;
					}
				}
				if (bNativeKindsValid && bHasFallthrough)
				{
					if (Instruction.NextInstruction == InvalidInstruction)
					{
						bNativeKindsValid = false;
					}
					else
					{
						bNativeKindsValid = Merge(Instruction.NextInstruction, Stack);
					}
				}
			}

			if (Plan.bOwnsStringTemporaries && !bNativeKindsValid) return Result;
			const bool bUsesWide = std::any_of(LocalKinds.begin(), LocalKinds.end(), [](const K Kind)
			{
				return Kind == K::I64 || Kind == K::U64;
			}) ||
			    std::any_of(InstructionKinds.begin(), InstructionKinds.end(), [](const K Kind)
			{
				return Kind == K::I64 || Kind == K::U64;
			}) ||
			    std::any_of(Plan.Instructions.begin(), Plan.Instructions.end(), [](const auto& Instruction)
			{
				return Instruction.Opcode == EOpcode::CallBuiltin && (static_cast<EBuiltinId>(Instruction.Immediate) == EBuiltinId::ToInt64 || static_cast<EBuiltinId>(Instruction.Immediate) == EBuiltinId::ToUInt64 || static_cast<EBuiltinId>(Instruction.Immediate) == EBuiltinId::Len);
			});
			if (Plan.bOwnsStringTemporaries && bUsesWide && !Plan.NativeWideCode) return Result;
			if (Plan.bUsesTextOperations)
			{
				for (std::size_t Pc = 0; Pc < Plan.Instructions.size(); ++Pc)
				{
					// Keep cleanup allocation-free: these plans own scalar/string
					// stores, never an unresolved owning container/object alias cycle.
					if (Plan.Instructions[Pc].Opcode == EOpcode::StoreLocal &&
						!IsInteger(InstructionKinds[Pc]) && InstructionKinds[Pc] != K::Bool && InstructionKinds[Pc] != K::String)
					{
						return Result;
					}
				}
			}
			if (Plan.bRuntimeBoundArrayPush && (bUsesWide || !bNativeKindsValid))
			{
				return Result;
			}
			if (bUsesWide)
			{
				for (std::size_t Slot = 0; Slot < LocalCount; ++Slot)
				{
					if (Referenced[Slot] && !IsInteger(LocalKinds[Slot]) && LocalKinds[Slot] != K::Bool && LocalKinds[Slot] != K::ArrayI32 && LocalKinds[Slot] != K::ArrayU32 && LocalKinds[Slot] != K::String)
					{
						return Result;
					}
				}
				for (const auto& Instruction : Plan.Instructions)
				{
					if (((Instruction.Opcode == EOpcode::CallMethod || Instruction.Opcode == EOpcode::CallBoundMethod) && Instruction.Immediate != 2) || Instruction.Opcode == EOpcode::LoadField || Instruction.Opcode == EOpcode::LoadFieldKeep || Instruction.Opcode == EOpcode::StoreField || Instruction.Opcode == EOpcode::IncField)
					{
						return Result;
					}
				}
				if (!bNativeKindsValid) return Result;
			}
			else if (!Plan.NativeI32Code)
			{
				bNativeKindsValid = false;
			}
			if (Plan.bOwnsStringTemporaries && !bNativeKindsValid) return Result;

			if (bNativeKindsValid)
			{
				for (std::size_t Slot = 0; Slot < LocalCount; ++Slot)
				{
					if (!Referenced[Slot] || Locals[Slot].Kind != K::Unsupported)
					{
						continue;
					}
					// A first StoreLocal may be conditional. Preserve an already typed
					// value until that store executes, including an existing array alias.
					// Only compiler temporaries need their default alternative replaced.
					if (Bind(static_cast<std::uint16_t>(Slot)) && Locals[Slot].Kind == LocalKinds[Slot])
					{
						continue;
					}
					// Optional runtime-bound plans may decline after validation.
					// Their preflight cannot change local tags or release owners.
					if (Plan.bRuntimeBoundArrayPush) return Result;
					Locals[Slot] = {};
					if (LocalKinds[Slot] == K::I32)
					{
						if (!Bind(static_cast<std::uint16_t>(Slot)) || Locals[Slot].Kind != K::I32)
						{
							bNativeKindsValid = false;
						}
					}
					else if (LocalKinds[Slot] == K::I64 || LocalKinds[Slot] == K::U64)
					{
						const FValue Zero = LocalKinds[Slot] == K::I64 ? FValue(std::int64_t{0}) : FValue(std::uint64_t{0});
						if (!Vm.JitStoreLocal(static_cast<std::uint16_t>(Slot), Zero) || !Bind(static_cast<std::uint16_t>(Slot)))
						{
							bNativeKindsValid = false;
						}
					}
					else if (LocalKinds[Slot] == K::U32)
					{
						if (!Vm.JitStoreLocal(static_cast<std::uint16_t>(Slot),
						        FValue(std::uint32_t{0})) ||
						    !Bind(static_cast<std::uint16_t>(Slot)))
						{
							bNativeKindsValid = false;
						}
					}
					else if (LocalKinds[Slot] == K::Bool)
					{
						if (!Vm.JitStoreLocal(static_cast<std::uint16_t>(Slot), FValue(false)) || !Bind(static_cast<std::uint16_t>(Slot)))
						{
							bNativeKindsValid = false;
						}
					}
					else if (LocalKinds[Slot] == K::String)
					{
						if (!Vm.JitStoreLocal(static_cast<std::uint16_t>(Slot),
						        FValue(std::string{})) ||
						    !Bind(static_cast<std::uint16_t>(Slot)))
						{
							bNativeKindsValid = false;
						}
					}
					else if (LocalKinds[Slot] != K::ArrayI32 && LocalKinds[Slot] != K::ArrayU32 && LocalKinds[Slot] != K::ArrayString && LocalKinds[Slot] != K::MapI32I32 && LocalKinds[Slot] != K::MapI32U32 && LocalKinds[Slot] != K::MapU32I32 && LocalKinds[Slot] != K::MapU32U32 && LocalKinds[Slot] != K::MapStringI32 && LocalKinds[Slot] != K::SetI32 && LocalKinds[Slot] != K::SetU32 && LocalKinds[Slot] != K::SetString && LocalKinds[Slot] != K::ArrayObject && LocalKinds[Slot] != K::Object)
					{
						bNativeKindsValid = false;
					}
				}
			}

			if (bNativeKindsValid)
			{
				// Link only actual aliases once at the loop boundary. A push then refreshes
				// the usually-single binding in O(alias count), rather than scanning every
				// local on each iteration of an array-construction loop.
				for (std::size_t Slot = 0; Slot < LocalCount; ++Slot)
				{
					FLocalBinding& Binding = Locals[Slot];
					if ((Binding.Kind != K::ArrayI32 && Binding.Kind != K::ArrayU32) || !Binding.Storage)
					{
						continue;
					}
					Binding.AliasRoot = &Binding;
					Binding.NextAlias = nullptr;
					for (std::size_t Previous = 0; Previous < Slot; ++Previous)
					{
						FLocalBinding& Candidate = Locals[Previous];
						if (Candidate.Kind != Binding.Kind || Candidate.Storage != Binding.Storage)
						{
							continue;
						}
						FLocalBinding* const Root = Candidate.AliasRoot
						    ? Candidate.AliasRoot
						    : &Candidate;
						Binding.AliasRoot = Root;
						Binding.NextAlias = Root->NextAlias;
						Root->NextAlias = &Binding;
						break;
					}
				}
				// Strings remain owned by their frame/array values. Build compact borrowed
				// views once per invocation so native comparisons and Map/Set probes can
				// reuse lengths and hashes rather than inspecting/hashing std::string on
				// every loop iteration.
				bool bNeedsStringHash = false;
				for (std::size_t Pc = 0; Pc < Plan.Instructions.size(); ++Pc)
				{
					const auto Op = Plan.Instructions[Pc].Opcode;
					if (InstructionKinds[Pc] == K::String && (Op == EOpcode::Eq || Op == EOpcode::Ne || Op == EOpcode::GetIndexCopy || Op == EOpcode::SetIndex || Op == EOpcode::CallMethod))
					{
						bNeedsStringHash = true;
						break;
					}
				}
				std::vector<FTypedLoopNativeStringView> NativeStringLocals(LocalCount);
				std::vector<std::vector<FTypedLoopNativeStringView>>
				    NativeStringArrays(LocalCount);
				for (std::size_t Slot = 0; Slot < LocalCount; ++Slot)
				{
					if (!Referenced[Slot])
					{
						continue;
					}
					if (LocalKinds[Slot] == K::String)
					{
						const auto* const Value = static_cast<const std::string*>(
						    Locals[Slot].Storage);
						if (!Value)
						{
							bNativeKindsValid = false;
							break;
						}
						NativeStringLocals[Slot] = {
						    Value, Value->data(), Value->size(), bNeedsStringHash ? std::hash<std::string>{}(*Value) : 0u};
					}
					else if (LocalKinds[Slot] == K::ArrayString)
					{
						const auto* const Values = static_cast<const std::vector<std::string>*>(
						    Locals[Slot].Storage);
						if (!Values)
						{
							bNativeKindsValid = false;
							break;
						}
						auto& Views = NativeStringArrays[Slot];
						Views.reserve(Values->size());
						for (const std::string& Value : *Values)
						{
							Views.push_back({&Value, Value.data(), Value.size(),
							    std::hash<std::string>{}(Value)});
						}
						Locals[Slot].NativeData = Views.data();
						Locals[Slot].NativeSize = Views.size();
					}
				}
				if (!bNativeKindsValid)
				{
					return Result;
				}

				std::vector<std::uint8_t> NativeStringHashValid(
					Plan.bOwnsStringTemporaries && bNeedsStringHash ? LocalCount : 0u, 1u);
				FTypedLoopNativeStringScratch StringScratch{
					TextBuffers ? TextBuffers->Values.data() : nullptr,
					TextBuffers ? TextBuffers->Views.data() : nullptr, NativeStringLocals.data(),
					TextBuffers ? TextBuffers->Values.size() : 0u, LocalCount};
				StringScratch.Instructions = Plan.Instructions.data();
				StringScratch.InstructionCount = Plan.Instructions.size();
				StringScratch.LocalHashValid = NativeStringHashValid.empty() ? nullptr : NativeStringHashValid.data();
				if (TextBuffers)
				{
					for (std::size_t Pc = 0; Pc < Plan.Instructions.size(); ++Pc)
					{
						const auto Producer = Plan.Instructions[Pc].TextValueSlot;
						if (Plan.Instructions[Pc].Opcode == EOpcode::CallBuiltin && Producer < TextBuffers->Kinds.size())
						{
							TextBuffers->Kinds[Producer] = InstructionKinds[Pc];
						}
					}
					StringScratch.ProducerKinds = TextBuffers->Kinds.data();
					StringScratch.TemporaryHashValid = TextBuffers->HashValid.data();
				}
				std::vector<std::uint64_t> NativeLocals(LocalCount, 0u);
				for (std::size_t Slot = 0; Slot < LocalCount; ++Slot)
				{
					if (!Referenced[Slot])
					{
						continue;
					}
					switch (LocalKinds[Slot])
					{
					case K::I32:
						{
							NativeLocals[Slot] = bUsesWide
							    ? static_cast<std::uint64_t>(static_cast<std::int64_t>(*static_cast<std::int32_t*>(Locals[Slot].Storage)))
							    : static_cast<std::uint32_t>(*static_cast<std::int32_t*>(Locals[Slot].Storage));
							break;
						}
					case K::I64:
						{
							NativeLocals[Slot] = static_cast<std::uint64_t>(*static_cast<std::int64_t*>(Locals[Slot].Storage));
							break;
						}
					case K::U64:
						{
							NativeLocals[Slot] = *static_cast<std::uint64_t*>(Locals[Slot].Storage);
							break;
						}
					case K::U32:
						{
							NativeLocals[Slot] = *static_cast<std::uint32_t*>(
							    Locals[Slot].Storage);
							break;
						}
					case K::Bool:
						{
							NativeLocals[Slot] = *static_cast<bool*>(Locals[Slot].Storage)
							    ? 1u
							    : 0u;
							break;
						}
					case K::String:
						{
							NativeLocals[Slot] = reinterpret_cast<std::uint64_t>(
							                         &NativeStringLocals[Slot]) |
							    TypedLoopNativeStringTag;
							break;
						}
					default:
						{
							if (Locals[Slot].Kind != K::Unsupported)
							{
								NativeLocals[Slot] = reinterpret_cast<std::uint64_t>(
								    &Locals[Slot]);
							}
							break;
						}
					}
				}
				std::vector<std::uint64_t> NativeStack(
				    std::max<std::size_t>(Plan.MaxStackDepth, 1u));
				std::vector<FTypedLoopNativeObjectViewRange> ObjectViewRanges;
				for (std::size_t Slot = 0; Slot < NativeObjectElementBindings.size(); ++Slot)
				{
					auto& Views = NativeObjectElementBindings[Slot];
					if (!Views.empty())
					{
						auto& Values = *static_cast<std::vector<FValue>*>(Locals[Slot].Storage);
						ObjectViewRanges.push_back({Views.data(), Views.size(), Values.data()});
					}
				}
				FTypedLoopNativeContext Context;
				Context.ObjectViewRanges = ObjectViewRanges.data();
				Context.ObjectViewRangeCount = ObjectViewRanges.size();
				Context.Vm = &Vm;
				Context.Bindings = Locals;
				Context.InstructionKinds = InstructionKinds.data();
				Context.Locals = NativeLocals.data();
				Context.Stack = NativeStack.data();
				if (bUsesWide)
				{
					Context.Instructions = Plan.Instructions.data();
				}
				else
				{
					Context.Fields = DirectFields.empty() ? nullptr : DirectFields.data();
				}
				Context.Methods = DirectMethods.empty() ? nullptr : DirectMethods.data();
				Context.PolymorphicMethods = PolymorphicMethods.empty()
				    ? nullptr
				    : PolymorphicMethods.data();
				Context.DirectFieldMutationObject = DirectFieldMutationObject;
				Context.Strings = Plan.bOwnsStringTemporaries ? &StringScratch : nullptr;
				const auto Kernel = reinterpret_cast<JitNative::FTypedLoopI32Kernel>(
				    bUsesWide ? Plan.NativeWideCode : Plan.NativeI32Code);
				// From this point the transformed loop owns source-visible state.  A
				// runtime failure must retain the current iteration object just like the
				// ordinary bytecode path, so rollback is only for preflight rejection.
				HoistedObjectRollback.Commit();
				const std::int32_t Status = Kernel ? Kernel(&Context) : ExecuteWideTypedLoop(Context, Plan);
				struct FPendingTextFailure
				{
					FExecutionContext& Vm;
					const FTypedLoopPlan& Plan;
					FTypedLoopNativeContext& Context;
					std::int32_t Status;
					~FPendingTextFailure()
					{
						if (Status == 0 || !Plan.bUsesTextOperations) return;
						const char* Message = Context.FailureMessage;
						if (!Message)
						{
							switch (static_cast<EJitRuntimeStatus>(Status))
							{
							case EJitRuntimeStatus::IntegerOverflow:
								{
									Message = "integer overflow";
									break;
								}
							case EJitRuntimeStatus::DivisionByZero:
								{
									Message = "division by zero";
									break;
								}
							case EJitRuntimeStatus::ShiftOutOfRange:
								{
									Message = "shift count out of range";
									break;
								}
							case EJitRuntimeStatus::ToIntOverflow:
								{
									Message = "to_int overflow";
									break;
								}
							case EJitRuntimeStatus::ToUnsignedOverflow:
								{
									Message = "to_unsigned overflow";
									break;
								}
							default:
								{
									Message = "JIT typed loop: native execution failed";
									break;
								}
							}
						}
						std::uint16_t Function = 0xFFFFu;
						std::uint32_t Callee = 0u;
						for (const auto& I : Plan.Instructions)
						{
							if (I.BytecodeOffset == Context.FailureOffset)
							{
								Function = I.TextFunctionIndex;
								Callee = I.TextCalleeOffset;
								break;
							}
						}
						Vm.JitReportTextFailure(Message, Context.FailureOffset, Function, Callee);
					}
				} PendingTextFailure{Vm, Plan, Context, Status};
				// Commit all native field writes before any fallible bookkeeping.
				// A later method may have failed after earlier receivers were changed.
				for (const FTypedLoopNativeMethodBinding& Method : DirectMethods)
				{
					if (Method.FieldCode)
					{
						Method.Object->MutationVersion += Method.FieldState.MutationCount;
					}
				}
				if (Kernel)
				{
					Vm.RecordJitNativeTypedLoopExecution();
				}
				// Resolve every borrowed string token before mutating any frame-owned
				// string.  Native string assignment moves tokens rather than characters;
				// a swap such as `tmp = a; a = b; b = tmp` can therefore leave b's token
				// pointing at a's original storage.  Committing a first would otherwise
				// change the value subsequently observed while committing b, making the
				// result depend on local-slot order.  The staging area is invocation-local
				// and only allocated when a string local was actually rebound.
				std::vector<std::string> ResolvedStringLocals;
				std::vector<std::uint8_t> HasResolvedStringLocal;
				for (std::size_t Slot = 0; Slot < LocalCount; ++Slot)
				{
					if (!Referenced[Slot] || LocalKinds[Slot] != K::String)
					{
						continue;
					}
					const std::uint64_t OwnToken = reinterpret_cast<std::uint64_t>(
					                                   &NativeStringLocals[Slot]) |
					    TypedLoopNativeStringTag;
					if (NativeLocals[Slot] == OwnToken)
					{
						continue;
					}
					if (ResolvedStringLocals.empty())
					{
						ResolvedStringLocals.resize(LocalCount);
						HasResolvedStringLocal.assign(LocalCount, std::uint8_t{0});
					}
					const auto* const Source = reinterpret_cast<
					    const FTypedLoopNativeStringView*>(
					    static_cast<std::uintptr_t>(NativeLocals[Slot] & TypedLoopNativePayloadMask));
					if (!Source || !Source->Value)
					{
						Vm.JitReportRuntimeError(
						    "JIT typed loop: invalid borrowed string");
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    Context.FailureOffset};
					}
					ResolvedStringLocals[Slot] = *Source->Value;
					HasResolvedStringLocal[Slot] = 1u;
				}
				// Container and object assignments use the same token scheme as strings.
				// Snapshot every aliased destination before the first frame write so handle
				// swaps and cycles cannot observe a source slot that was already committed.
				// FValue copies preserve the language's shared-handle/value ownership rules.
				std::vector<FValue> ResolvedAliasLocals;
				std::vector<std::uint8_t> HasResolvedAliasLocal;
				for (std::size_t Slot = 0; Slot < LocalCount; ++Slot)
				{
					if (!Referenced[Slot])
					{
						continue;
					}
					const bool bAliasKind = LocalKinds[Slot] == K::ArrayI32 || LocalKinds[Slot] == K::ArrayU32 || LocalKinds[Slot] == K::ArrayString || LocalKinds[Slot] == K::MapI32I32 || LocalKinds[Slot] == K::MapI32U32 || LocalKinds[Slot] == K::MapU32I32 || LocalKinds[Slot] == K::MapU32U32 || LocalKinds[Slot] == K::MapStringI32 || LocalKinds[Slot] == K::SetI32 || LocalKinds[Slot] == K::SetU32 || LocalKinds[Slot] == K::SetString || LocalKinds[Slot] == K::ArrayObject || LocalKinds[Slot] == K::Object;
					if (!bAliasKind)
					{
						continue;
					}
					const std::uint64_t OwnToken = Locals[Slot].Kind == K::Unsupported
					    ? 0u
					    : reinterpret_cast<std::uint64_t>(&Locals[Slot]);
					if (NativeLocals[Slot] == 0u || NativeLocals[Slot] == OwnToken)
					{
						continue;
					}
					FValue* Source = nullptr;
					if (IsTypedLoopNativeObjectView(NativeLocals[Slot]))
					{
						if (LocalKinds[Slot] == K::Object)
						{
							(void)FindTypedLoopNativeObjectView(NativeLocals[Slot],
								ObjectViewRanges.data(), ObjectViewRanges.size(), Source);
						}
					}
					else
					{
						auto* const SourceBinding = reinterpret_cast<FLocalBinding*>(
							static_cast<std::uintptr_t>(NativeLocals[Slot]));
						const auto Address = reinterpret_cast<std::uintptr_t>(SourceBinding);
						const auto Begin = reinterpret_cast<std::uintptr_t>(Locals);
						const auto Offset = Address >= Begin ? Address - Begin : static_cast<std::uintptr_t>(-1);
						if (Offset % sizeof(FLocalBinding) == 0u && Offset / sizeof(FLocalBinding) < LocalCount)
						{
							Source = SourceBinding->SourceValue;
							if (!Source)
							{
								Source = Vm.JitBorrowDirectLinearLocal(
									static_cast<std::uint16_t>(Offset / sizeof(FLocalBinding)));
							}
						}
					}
					if (!Source)
					{
						Vm.JitRefineRuntimeErrorLocation(Context.FailureOffset);
						Vm.JitReportRuntimeError(
						    "JIT typed loop: invalid native local alias");
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    Context.FailureOffset};
					}
					if (ResolvedAliasLocals.empty())
					{
						ResolvedAliasLocals.resize(LocalCount);
						HasResolvedAliasLocal.assign(LocalCount, std::uint8_t{0});
					}
					ResolvedAliasLocals[Slot] = *Source;
					HasResolvedAliasLocal[Slot] = 1u;
				}
				for (std::size_t Slot = 0; Slot < LocalCount; ++Slot)
				{
					if (!Referenced[Slot])
					{
						continue;
					}
					if (LocalKinds[Slot] == K::I32)
					{
						*static_cast<std::int32_t*>(Locals[Slot].Storage) =
						    static_cast<std::int32_t>(NativeLocals[Slot]);
					}
					else if (LocalKinds[Slot] == K::I64)
					{
						*static_cast<std::int64_t*>(Locals[Slot].Storage) = static_cast<std::int64_t>(NativeLocals[Slot]);
					}
					else if (LocalKinds[Slot] == K::U64)
					{
						*static_cast<std::uint64_t*>(Locals[Slot].Storage) = NativeLocals[Slot];
					}
					else if (LocalKinds[Slot] == K::U32)
					{
						*static_cast<std::uint32_t*>(Locals[Slot].Storage) =
						    static_cast<std::uint32_t>(NativeLocals[Slot]);
					}
					else if (LocalKinds[Slot] == K::Bool)
					{
						*static_cast<bool*>(Locals[Slot].Storage) =
						    NativeLocals[Slot] != 0u;
					}
					else if (LocalKinds[Slot] == K::String)
					{
						const std::uint64_t OwnToken = reinterpret_cast<std::uint64_t>(
						                                   &NativeStringLocals[Slot]) |
						    TypedLoopNativeStringTag;
						if (NativeLocals[Slot] == OwnToken)
						{
							continue;
						}
						if (!HasResolvedStringLocal[Slot])
						{
							Vm.JitReportRuntimeError(
							    "JIT typed loop: invalid borrowed string");
							return {ETypedLoopExecution::Failed, HeaderOffset,
							    Context.FailureOffset};
						}
						if (Locals[Slot].Kind == K::String)
						{
							*static_cast<std::string*>(Locals[Slot].Storage) =
							    ResolvedStringLocals[Slot];
						}
						else if (!Vm.JitStoreLocal(static_cast<std::uint16_t>(Slot),
						             FValue(ResolvedStringLocals[Slot])))
						{
							return {ETypedLoopExecution::Failed, HeaderOffset,
							    Context.FailureOffset};
						}
					}
					else if (LocalKinds[Slot] == K::ArrayI32 || LocalKinds[Slot] == K::ArrayU32 || LocalKinds[Slot] == K::ArrayString || LocalKinds[Slot] == K::MapI32I32 || LocalKinds[Slot] == K::MapI32U32 || LocalKinds[Slot] == K::MapU32I32 || LocalKinds[Slot] == K::MapU32U32 || LocalKinds[Slot] == K::MapStringI32 || LocalKinds[Slot] == K::SetI32 || LocalKinds[Slot] == K::SetU32 || LocalKinds[Slot] == K::SetString || LocalKinds[Slot] == K::ArrayObject || LocalKinds[Slot] == K::Object)
					{
						const std::uint64_t OwnToken = Locals[Slot].Kind == K::Unsupported
						    ? 0u
						    : reinterpret_cast<std::uint64_t>(&Locals[Slot]);
						if (NativeLocals[Slot] == 0u || NativeLocals[Slot] == OwnToken)
						{
							continue;
						}
						if (HasResolvedAliasLocal.empty() || !HasResolvedAliasLocal[Slot] || !Vm.JitStoreLocal(static_cast<std::uint16_t>(Slot), std::move(ResolvedAliasLocals[Slot])))
						{
							return {ETypedLoopExecution::Failed, HeaderOffset,
							    Context.FailureOffset};
						}
					}
				}
				Vm.RecordJitArrayFastPathGet(Context.ArrayGets);
				Vm.RecordJitArrayFastPathSet(Context.ArraySets);
				Vm.RecordJitArrayFastPathPush(Context.ArrayPushes);
				Vm.RecordJitScalarMapFastPathGet(Context.MapGets);
				Vm.RecordJitScalarMapFastPathSet(Context.MapSets);
				for (FTypedLoopNativeMethodBinding& Method : DirectMethods)
				{
					if (Method.FieldCode)
					{
						if (Method.CallCount != 0u && !Vm.JitCommitDirectScalarMethod(EJitScalarValueKind::I32, Method.CallCount))
						{
							return {ETypedLoopExecution::Failed, HeaderOffset, Context.FailureOffset};
						}
						continue;
					}
					if (Method.CallCount == 0u)
					{
						continue;
					}
					if (!Vm.JitCommitDirectI32FieldMethod(
					        *Method.Object, Method.FieldIndex, Method.CallCount))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    Context.FailureOffset};
					}
				}
				if (Plan.bHoistsNonEscapingObject)
				{
					FValue* const CounterValue = Vm.JitBorrowDirectLinearLocal(
					    Plan.HoistedObjectCounterSlot);
					const auto* const Counter = CounterValue
					    ? std::get_if<std::int32_t>(CounterValue)
					    : nullptr;
					if (!Counter || *Counter < HoistedObjectCounterBefore || !Vm.JitCommitDirectScalarMethod(EJitScalarValueKind::I32, static_cast<std::size_t>(*Counter - HoistedObjectCounterBefore)))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    Context.FailureOffset};
					}
				}
				std::size_t PolymorphicCallCount = 0u;
				for (const auto& Method : PolymorphicMethods)
				{
					PolymorphicCallCount += Method.CallCount;
				}
				if (PolymorphicCallCount != 0u && !Vm.JitCommitDirectScalarMethod(EJitScalarValueKind::I32, PolymorphicCallCount))
				{
					return {ETypedLoopExecution::Failed, HeaderOffset,
					    Context.FailureOffset};
				}
				if (Status == 0)
				{
					return {ETypedLoopExecution::Completed, Plan.ExitOffset,
					    Plan.HeaderOffset};
				}
				if (Plan.bUsesTextOperations)
				{
					return {ETypedLoopExecution::Failed, HeaderOffset, Context.FailureOffset};
				}
				Vm.JitRefineRuntimeErrorLocation(Context.FailureOffset);
				if (Vm.HasError())
				{
					return {ETypedLoopExecution::Failed, HeaderOffset,
					    Context.FailureOffset};
				}
				if (Context.FailureMessage)
				{
					Vm.JitReportRuntimeError(Context.FailureMessage);
				}
				else if (Status == static_cast<std::int32_t>(EJitRuntimeStatus::IntegerOverflow))
				{
					Vm.JitReportRuntimeError("integer overflow");
				}
				else if (Status == static_cast<std::int32_t>(EJitRuntimeStatus::DivisionByZero))
				{
					Vm.JitReportRuntimeError("division by zero");
				}
				else if (Status == static_cast<std::int32_t>(EJitRuntimeStatus::ShiftOutOfRange))
				{
					Vm.JitReportRuntimeError("shift count out of range");
				}
				else if (Status == static_cast<std::int32_t>(EJitRuntimeStatus::ToIntOverflow))
				{
					Vm.JitReportRuntimeError("to_int overflow");
				}
				else if (Status == static_cast<std::int32_t>(EJitRuntimeStatus::ToUnsignedOverflow))
				{
					Vm.JitReportRuntimeError("to_unsigned overflow");
				}
				else
				{
					Vm.JitReportRuntimeError("JIT typed loop: native execution failed");
				}
				return {ETypedLoopExecution::Failed, HeaderOffset,
				    Context.FailureOffset};
			}
		}
		// Direct object methods are implemented by the native loop kernel only.
		// If that kernel is unavailable (for example on A64), reject the plan
		// before the portable executor can mutate locals and reach an unsupported call.
		if (Plan.bRuntimeBoundArrayPush) return Result;
		if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::None && std::any_of(Plan.Instructions.begin(), Plan.Instructions.end(), [](const FTypedLoopInstruction& Instruction)
		{
			return Instruction.Opcode == EOpcode::CallBoundMethod || (Instruction.Opcode == EOpcode::CallMethod && Instruction.Immediate == 0);
		}))
		{
			return Result;
		}
		if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::DirectFieldAccumulate)
		{
			const auto& D = Plan.DirectFieldAccumulator;
			// The fused executor snapshots the loop limit.  A source loop may legally
			// use that same local as its accumulator, in which case the comparison
			// must observe the value written by every iteration.  Keep that aliasing
			// form on the ordinary opcode path instead of changing its termination.
			if (D.TotalSlot == D.LimitSlot || D.TotalSlot == D.CounterSlot || D.LimitSlot == D.CounterSlot)
			{
				return Result;
			}
			if (!Bind(D.CounterSlot) || !Bind(D.LimitSlot) || !Bind(D.TotalSlot) || Locals[D.CounterSlot].Kind != ELocalKind::I32 || Locals[D.LimitSlot].Kind != ELocalKind::I32)
			{
				return Result;
			}
			std::int32_t& Counter = *static_cast<std::int32_t*>(
			    Locals[D.CounterSlot].Storage);
			const std::int32_t Limit = *static_cast<std::int32_t*>(
			    Locals[D.LimitSlot].Storage);
			if (D.FieldInstruction >= DirectFields.size())
			{
				return Result;
			}
			const FDirectFieldBinding& Field = DirectFields[D.FieldInstruction];
			const auto FailAt = [&](const std::uint16_t InstructionIndex,
			                        const char* Error) -> FTypedLoopExecutionResult
			{
				const std::size_t Offset = Plan.Instructions[InstructionIndex].BytecodeOffset;
				Vm.JitRefineRuntimeErrorLocation(Offset);
				Vm.JitReportRuntimeError(Error);
				return {ETypedLoopExecution::Failed, HeaderOffset, Offset};
			};
			if (Field.Kind == FDirectFieldBinding::EKind::I32 && Locals[D.TotalSlot].Kind == ELocalKind::I32)
			{
				const std::int32_t Value = *static_cast<std::int32_t*>(Field.Storage);
				std::int32_t& Total = *static_cast<std::int32_t*>(
				    Locals[D.TotalSlot].Storage);
				while (Counter < Limit)
				{
					std::int32_t Updated = 0;
					if (!Runtime::CheckedAdd(Total, Value, Updated))
					{
						return FailAt(D.AddInstruction, "integer overflow");
					}
					Total = Updated;
					if (!Runtime::CheckedAdd(Counter, 1, Updated))
					{
						return FailAt(D.CounterInstruction, "integer overflow");
					}
					Counter = Updated;
				}
			}
			else if (Field.Kind == FDirectFieldBinding::EKind::F64 && Locals[D.TotalSlot].Kind == ELocalKind::F64)
			{
				const double Value = *static_cast<double*>(Field.Storage);
				double& Total = *static_cast<double*>(Locals[D.TotalSlot].Storage);
				while (Counter < Limit)
				{
					Total += Value;
					std::int32_t Updated = 0;
					if (!Runtime::CheckedAdd(Counter, 1, Updated))
					{
						return FailAt(D.CounterInstruction, "integer overflow");
					}
					Counter = Updated;
				}
			}
			else if (Field.Kind == FDirectFieldBinding::EKind::F32 && Locals[D.TotalSlot].Kind == ELocalKind::F32)
			{
				const float Value = *static_cast<float*>(Field.Storage);
				float& Total = *static_cast<float*>(Locals[D.TotalSlot].Storage);
				while (Counter < Limit)
				{
					Total += Value;
					std::int32_t Updated = 0;
					if (!Runtime::CheckedAdd(Counter, 1, Updated))
					{
						return FailAt(D.CounterInstruction, "integer overflow");
					}
					Counter = Updated;
				}
			}
			else if (Field.Kind == FDirectFieldBinding::EKind::U32 && Locals[D.TotalSlot].Kind == ELocalKind::U32)
			{
				const std::uint32_t Value = *static_cast<std::uint32_t*>(Field.Storage);
				std::uint32_t& Total = *static_cast<std::uint32_t*>(
				    Locals[D.TotalSlot].Storage);
				while (Counter < Limit)
				{
					Total += Value;
					std::int32_t Updated = 0;
					if (!Runtime::CheckedAdd(Counter, 1, Updated))
					{
						return FailAt(D.CounterInstruction, "integer overflow");
					}
					Counter = Updated;
				}
			}
			else
			{
				return Result;
			}
			return {ETypedLoopExecution::Completed, Plan.ExitOffset, Plan.HeaderOffset};
		}
		if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::DirectFieldBatchUpdate)
		{
			// A mutation callback may re-enter the VM after every StoreField and can
			// replace object state. Keep the ordinary opcode path in that observable
			// configuration; otherwise one shape guard safely covers the whole batch.
			if (Vm.JitHasMutationCallback())
			{
				return Result;
			}
			const auto& D = Plan.DirectFieldBatch;
			const auto I32 = [&](const std::uint16_t Slot) -> std::int32_t*
			{
				return Bind(Slot) && Locals[Slot].Kind == ELocalKind::I32
				    ? static_cast<std::int32_t*>(Locals[Slot].Storage)
				    : nullptr;
			};
			std::int32_t* const Counter = I32(D.CounterSlot);
			std::int32_t* const Limit = I32(D.LimitSlot);
			if (!Counter || !Limit || D.FieldCount == 0u)
			{
				return Result;
			}

			std::array<FScriptObject*, 32> Objects{};
			std::array<std::uint16_t, 32> FieldIndices{};
			std::array<std::int32_t*, 32> Fields{};
			for (std::size_t Field = 0; Field < D.FieldCount; ++Field)
			{
				if (!Vm.JitBorrowLocalI32Field(
				        D.ObjectSlot, D.FieldNameIndices[Field], Objects[Field],
				        FieldIndices[Field], Fields[Field]))
				{
					return Vm.HasError()
					    ? FTypedLoopExecutionResult{ETypedLoopExecution::Failed,
					          HeaderOffset, Plan.HeaderOffset}
					    : Result;
				}
				if (!Objects[Field] || !Fields[Field] || (Field != 0u && Objects[Field] != Objects[0]))
				{
					return Result;
				}
			}

			std::int32_t CounterValue = *Counter;
			const std::int32_t LimitValue = *Limit;
			std::size_t MutationCount = 0u;
			const auto CommitCounter = [&]() noexcept
			{
				*Counter = CounterValue;
				Objects[0]->MutationVersion += MutationCount;
				MutationCount = 0u;
			};
			const auto FailAt = [&](const std::uint16_t InstructionIndex,
			                        const char* Error) -> FTypedLoopExecutionResult
			{
				CommitCounter();
				const std::size_t Offset = InstructionIndex < Plan.Instructions.size()
				    ? Plan.Instructions[InstructionIndex].BytecodeOffset
				    : Plan.HeaderOffset;
				Vm.JitRefineRuntimeErrorLocation(Offset);
				Vm.JitReportRuntimeError(Error);
				return {ETypedLoopExecution::Failed, HeaderOffset, Offset};
			};
			while (CounterValue < LimitValue)
			{
				for (std::size_t Field = 0; Field < D.FieldCount; ++Field)
				{
					std::int32_t Updated = 0;
					if (!Runtime::CheckedAdd(*Fields[Field], D.Deltas[Field], Updated))
					{
						return FailAt(D.AdditionInstructions[Field], "integer overflow");
					}
					*Fields[Field] = Updated;
					++MutationCount;
				}
				std::int32_t UpdatedCounter = 0;
				if (!Runtime::CheckedAdd(CounterValue, 1, UpdatedCounter))
				{
					return FailAt(D.CounterInstruction, "integer overflow");
				}
				CounterValue = UpdatedCounter;
			}
			CommitCounter();
			return {ETypedLoopExecution::Completed, Plan.ExitOffset, Plan.HeaderOffset};
		}
		if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::DirectStateFieldAccumulate)
		{
			// Every source-level field write must remain observable to mutation
			// callbacks.  The optimized stable-shape path is therefore used only when
			// writes have no external observer; otherwise the ordinary typed loop keeps
			// the exact callback cadence.
			if (Vm.JitHasMutationCallback())
			{
				return Result;
			}
			const auto& D = Plan.DirectFields;
			const auto I32 = [&](const std::uint16_t Slot) -> std::int32_t*
			{
				return Bind(Slot) && Locals[Slot].Kind == ELocalKind::I32
				    ? static_cast<std::int32_t*>(Locals[Slot].Storage)
				    : nullptr;
			};
			std::int32_t* const Counter = I32(D.CounterSlot);
			std::int32_t* const Limit = I32(D.LimitSlot);
			std::int32_t* const State = I32(D.StateSlot);
			std::int32_t* const Choice = I32(D.ChoiceSlot);
			std::int32_t* const Total = I32(D.TotalSlot);
			if (!Counter || !Limit || !State || !Choice || !Total)
			{
				return Result;
			}

			std::array<FScriptObject*, 32> Objects{};
			std::array<std::uint16_t, 32> FieldIndices{};
			std::array<std::int32_t*, 32> Fields{};
			for (std::size_t Field = 0; Field < D.FieldCount; ++Field)
			{
				if (!Vm.JitBorrowLocalI32Field(
				        D.ObjectSlot, D.FieldNameIndices[Field], Objects[Field],
				        FieldIndices[Field], Fields[Field]))
				{
					return Vm.HasError()
					    ? FTypedLoopExecutionResult{ETypedLoopExecution::Failed,
					          HeaderOffset, Plan.HeaderOffset}
					    : Result;
				}
				if (!Objects[Field] || !Fields[Field] || (Field != 0u && Objects[Field] != Objects[0]))
				{
					return Result;
				}
			}

			std::int32_t CounterValue = *Counter;
			const std::int32_t LimitValue = *Limit;
			std::int32_t StateValue = *State;
			std::int32_t ChoiceValue = *Choice;
			std::int32_t TotalValue = *Total;
			std::size_t MutationCount = 0u;
			const auto CommitState = [&]() noexcept
			{
				*Counter = CounterValue;
				*State = StateValue;
				*Choice = ChoiceValue;
				*Total = TotalValue;
				Objects[0]->MutationVersion += MutationCount;
				MutationCount = 0u;
			};
			const auto FailAt = [&](const std::uint16_t InstructionIndex,
			                        const char* Error) -> FTypedLoopExecutionResult
			{
				CommitState();
				const std::size_t Offset = InstructionIndex < Plan.Instructions.size()
				    ? Plan.Instructions[InstructionIndex].BytecodeOffset
				    : Plan.HeaderOffset;
				Vm.JitRefineRuntimeErrorLocation(Offset);
				Vm.JitReportRuntimeError(Error);
				return {ETypedLoopExecution::Failed, HeaderOffset, Offset};
			};
			while (CounterValue < LimitValue)
			{
				std::int32_t Product = 0;
				std::int32_t Sum = 0;
				if (!Runtime::CheckedMul(StateValue, D.StateMultiplier, Product))
				{
					return FailAt(D.StateMulInstruction, "integer overflow");
				}
				if (!Runtime::CheckedAdd(Product, D.StateIncrement, Sum))
				{
					return FailAt(D.StateAddInstruction, "integer overflow");
				}
				if (D.StateModulus == 0)
				{
					return FailAt(D.StateModInstruction, "division by zero");
				}
				if (Sum == std::numeric_limits<std::int32_t>::min() && D.StateModulus == -1)
				{
					return FailAt(D.StateModInstruction, "integer overflow");
				}
				StateValue = Sum % D.StateModulus;

				if (D.ChoiceModulus == 0)
				{
					return FailAt(D.ChoiceModInstruction, "division by zero");
				}
				if (StateValue == std::numeric_limits<std::int32_t>::min() && D.ChoiceModulus == -1)
				{
					return FailAt(D.ChoiceModInstruction, "integer overflow");
				}
				ChoiceValue = StateValue % D.ChoiceModulus;

				std::size_t Selected = D.FieldCount - 1u;
				for (std::size_t Field = 0; Field + 1u < D.FieldCount; ++Field)
				{
					if (ChoiceValue == D.SelectorValues[Field])
					{
						Selected = Field;
						break;
					}
				}
				std::int32_t UpdatedField = 0;
				if (!Runtime::CheckedAdd(*Fields[Selected], 1, UpdatedField))
				{
					return FailAt(D.FieldIncrementInstructions[Selected],
					    "integer overflow");
				}
				*Fields[Selected] = UpdatedField;
				++MutationCount;
				if (!Runtime::CheckedAdd(TotalValue, UpdatedField, Sum))
				{
					return FailAt(D.TotalAddInstructions[Selected], "integer overflow");
				}
				TotalValue = Sum;

				if (D.TotalModulus == 0)
				{
					return FailAt(D.TotalModInstruction, "division by zero");
				}
				if (TotalValue == std::numeric_limits<std::int32_t>::min() && D.TotalModulus == -1)
				{
					return FailAt(D.TotalModInstruction, "integer overflow");
				}
				TotalValue %= D.TotalModulus;
				if (!Runtime::CheckedAdd(CounterValue, 1, Sum))
				{
					return FailAt(D.CounterInstruction, "integer overflow");
				}
				CounterValue = Sum;
			}
			CommitState();
			return {ETypedLoopExecution::Completed, Plan.ExitOffset, Plan.HeaderOffset};
		}
		if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::StateIndirectFieldUpdate)
		{
			// A field-mutation observer makes every source write externally visible;
			// retain the ordinary typed/VM path in that case instead of batching shape
			// resolution across the loop.
			if (Vm.JitHasMutationCallback())
			{
				return Result;
			}
			const auto& D = Plan.StateFields;
			const auto I32 = [&](const std::uint16_t Slot) -> std::int32_t*
			{
				return Bind(Slot) && Locals[Slot].Kind == ELocalKind::I32
				    ? static_cast<std::int32_t*>(Locals[Slot].Storage)
				    : nullptr;
			};
			std::int32_t* const Counter = I32(D.CounterSlot);
			std::int32_t* const Limit = I32(D.LimitSlot);
			std::int32_t* const State = I32(D.StateSlot);
			std::int32_t* const Position = I32(D.PositionSlot);
			std::int32_t* const Choice = I32(D.ChoiceSlot);
			std::int32_t* const Delta = I32(D.DeltaSlot);
			if (!Counter || !Limit || !State || !Position || !Choice || !Delta || !Bind(D.ChoicesSlot) || Locals[D.ChoicesSlot].Kind != ELocalKind::ArrayI32)
			{
				return Result;
			}
			std::int32_t CounterValue = *Counter;
			const std::int32_t LimitValue = *Limit;
			std::int32_t StateValue = *State;
			std::int32_t PositionValue = *Position;
			std::int32_t ChoiceValue = *Choice;
			std::int32_t DeltaValue = *Delta;
			std::size_t MutationCount = 0u;
			auto& Choices = *static_cast<std::vector<std::int32_t>*>(
			    Locals[D.ChoicesSlot].Storage);
			std::array<FScriptObject*, 16> Objects{};
			std::array<std::uint16_t, 16> FieldIndices{};
			std::array<std::int32_t*, 16> Fields{};
			std::array<std::int32_t*, 16> Temporaries{};
			for (std::size_t Field = 0; Field < D.FieldCount; ++Field)
			{
				Temporaries[Field] = I32(D.TemporarySlots[Field]);
				if (!Temporaries[Field] || !Vm.JitBorrowLocalI32Field(D.ObjectSlot, D.FieldNameIndices[Field], Objects[Field], FieldIndices[Field], Fields[Field]))
				{
					return Vm.HasError()
					    ? FTypedLoopExecutionResult{ETypedLoopExecution::Failed,
					          HeaderOffset, Plan.HeaderOffset}
					    : Result;
				}
				if (!Objects[Field] || !Fields[Field] || (Field != 0u && Objects[Field] != Objects[0]))
				{
					return Result;
				}
			}
			const auto CommitState = [&]() noexcept
			{
				*Counter = CounterValue;
				*State = StateValue;
				*Position = PositionValue;
				*Choice = ChoiceValue;
				*Delta = DeltaValue;
				Objects[0]->MutationVersion += MutationCount;
				MutationCount = 0u;
			};
			const auto FailAt = [&](const std::size_t InstructionIndex,
			                        const char* Error) -> FTypedLoopExecutionResult
			{
				CommitState();
				const std::size_t Offset = Plan.Instructions[InstructionIndex].BytecodeOffset;
				Vm.JitRefineRuntimeErrorLocation(Offset);
				Vm.JitReportRuntimeError(Error);
				return {ETypedLoopExecution::Failed, HeaderOffset, Offset};
			};
			std::size_t Completed = 0;
			while (CounterValue < LimitValue)
			{
				std::int32_t Product = 0;
				std::int32_t Sum = 0;
				if (!Runtime::CheckedMul(StateValue, D.StateMultiplier, Product))
				{
					return FailAt(6u, "integer overflow");
				}
				if (!Runtime::CheckedAdd(Product, D.StateIncrement, Sum))
				{
					return FailAt(8u, "integer overflow");
				}
				if (D.StateModulus == 0)
				{
					return FailAt(10u, "division by zero");
				}
				if (Sum == std::numeric_limits<std::int32_t>::min() && D.StateModulus == -1)
				{
					return FailAt(10u, "integer overflow");
				}
				StateValue = Sum % D.StateModulus;

				if (D.CounterModulus == 0)
				{
					return FailAt(16u, "division by zero");
				}
				if (CounterValue == std::numeric_limits<std::int32_t>::min() && D.CounterModulus == -1)
				{
					return FailAt(16u, "integer overflow");
				}
				const std::int32_t CounterIndex = CounterValue % D.CounterModulus;
				if (!Runtime::CheckedMul(
				        CounterIndex, D.PositionMultiplier, Product))
				{
					return FailAt(18u, "integer overflow");
				}
				if (!Runtime::CheckedAdd(StateValue, Product, Sum))
				{
					return FailAt(19u, "integer overflow");
				}
				if (D.PositionModulus == 0)
				{
					return FailAt(21u, "division by zero");
				}
				if (Sum == std::numeric_limits<std::int32_t>::min() && D.PositionModulus == -1)
				{
					return FailAt(21u, "integer overflow");
				}
				PositionValue = Sum % D.PositionModulus;
				if (PositionValue < 0 || static_cast<std::size_t>(PositionValue) >= Choices.size())
				{
					return FailAt(25u, "array index out of bounds");
				}
				ChoiceValue = Choices[static_cast<std::size_t>(PositionValue)];

				if (D.DeltaDivisor == 0)
				{
					return FailAt(29u, "division by zero");
				}
				if (StateValue == std::numeric_limits<std::int32_t>::min() && D.DeltaDivisor == -1)
				{
					return FailAt(29u, "integer overflow");
				}
				std::int32_t DeltaBase = StateValue / D.DeltaDivisor;
				if (D.DeltaModulus == 0)
				{
					return FailAt(31u, "division by zero");
				}
				if (DeltaBase == std::numeric_limits<std::int32_t>::min() && D.DeltaModulus == -1)
				{
					return FailAt(31u, "integer overflow");
				}
				DeltaBase %= D.DeltaModulus;
				if (!Runtime::CheckedAdd(DeltaBase, D.DeltaAddend, DeltaValue))
				{
					return FailAt(33u, "integer overflow");
				}

				std::size_t Selected = D.FieldCount - 1u;
				for (std::size_t Field = 0; Field + 1u < D.FieldCount; ++Field)
				{
					if (ChoiceValue == D.SelectorValues[Field])
					{
						Selected = Field;
						break;
					}
				}
				if (!Runtime::CheckedAdd(*Fields[Selected], DeltaValue, Sum))
				{
					return FailAt(D.AdditionInstructionIndices[Selected],
					    "integer overflow");
				}
				*Fields[Selected] = Sum;
				*Temporaries[Selected] = Sum;
				++MutationCount;
				if (!Runtime::CheckedAdd(CounterValue, 1, Sum))
				{
					return FailAt(Plan.Instructions.size() - 2u, "integer overflow");
				}
				CounterValue = Sum;
				++Completed;
			}
			CommitState();
			Vm.RecordJitArrayFastPathGet(Completed);
			return {ETypedLoopExecution::Completed, Plan.ExitOffset, Plan.HeaderOffset};
		}
		if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::StateStringMapLookup)
		{
			const auto& D = Plan.StateStrings;
			const auto I32 = [&](const std::uint16_t Slot) -> std::int32_t*
			{
				return Bind(Slot) && Locals[Slot].Kind == ELocalKind::I32
				    ? static_cast<std::int32_t*>(Locals[Slot].Storage)
				    : nullptr;
			};
			std::int32_t* const Counter = I32(D.CounterSlot);
			std::int32_t* const Limit = I32(D.LimitSlot);
			std::int32_t* const State = I32(D.StateSlot);
			std::int32_t* const Total = I32(D.TotalSlot);
			if (!Counter || !Limit || !State || !Total || !Bind(D.KeysSlot) || Locals[D.KeysSlot].Kind != ELocalKind::ArrayString || !Bind(D.MapSlot) || Locals[D.MapSlot].Kind != ELocalKind::MapStringI32)
			{
				return Result;
			}
			std::int32_t CounterValue = *Counter;
			const std::int32_t LimitValue = *Limit;
			std::int32_t StateValue = *State;
			std::int32_t TotalValue = *Total;
			const auto CommitState = [&]() noexcept
			{
				*Counter = CounterValue;
				*State = StateValue;
				*Total = TotalValue;
			};
			auto& Keys = *static_cast<std::vector<std::string>*>(
			    Locals[D.KeysSlot].Storage);
			auto& Values = *static_cast<FMap::FStringI32Entries*>(
			    Locals[D.MapSlot].Storage);
			const auto FailAt = [&](const std::size_t InstructionIndex,
			                        const char* Error) -> FTypedLoopExecutionResult
			{
				CommitState();
				const std::size_t Offset = Plan.Instructions[InstructionIndex].BytecodeOffset;
				Vm.JitRefineRuntimeErrorLocation(Offset);
				Vm.JitReportRuntimeError(Error);
				return {ETypedLoopExecution::Failed, HeaderOffset, Offset};
			};
			std::size_t Completed = 0;
			while (CounterValue < LimitValue)
			{
				std::int32_t Product = 0;
				std::int32_t Sum = 0;
				if (!Runtime::CheckedMul(StateValue, D.StateMultiplier, Product))
				{
					return FailAt(6u, "integer overflow");
				}
				if (!Runtime::CheckedAdd(Product, D.StateIncrement, Sum))
				{
					return FailAt(8u, "integer overflow");
				}
				if (D.StateModulus == 0)
				{
					return FailAt(10u, "division by zero");
				}
				if (Sum == std::numeric_limits<std::int32_t>::min() && D.StateModulus == -1)
				{
					return FailAt(10u, "integer overflow");
				}
				StateValue = Sum % D.StateModulus;
				if (D.PositionModulus == 0)
				{
					return FailAt(17u, "division by zero");
				}
				if (StateValue == std::numeric_limits<std::int32_t>::min() && D.PositionModulus == -1)
				{
					return FailAt(17u, "integer overflow");
				}
				const std::int32_t Index = StateValue % D.PositionModulus;
				if (Index < 0 || static_cast<std::size_t>(Index) >= Keys.size())
				{
					return FailAt(18u, "array index out of bounds");
				}
				const std::int32_t* const Value = Values.Find(
				    Keys[static_cast<std::size_t>(Index)]);
				if (!Value)
				{
					return FailAt(19u, "map key not found");
				}
				if (!Runtime::CheckedAdd(TotalValue, *Value, Sum))
				{
					return FailAt(20u, "integer overflow");
				}
				if (D.TotalModulus == 0)
				{
					return FailAt(22u, "division by zero");
				}
				if (Sum == std::numeric_limits<std::int32_t>::min() && D.TotalModulus == -1)
				{
					return FailAt(22u, "integer overflow");
				}
				TotalValue = Sum % D.TotalModulus;
				if (!Runtime::CheckedAdd(CounterValue, 1, Sum))
				{
					return FailAt(24u, "integer overflow");
				}
				CounterValue = Sum;
				++Completed;
			}
			CommitState();
			Vm.RecordJitArrayFastPathGet(Completed);
			Vm.RecordJitScalarMapFastPathGet(Completed);
			return {ETypedLoopExecution::Completed, Plan.ExitOffset, Plan.HeaderOffset};
		}
		if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::StateIndirectStringLookupCompare)
		{
			const auto& D = Plan.StateStrings;
			const auto I32 = [&](const std::uint16_t Slot) -> std::int32_t*
			{
				return Bind(Slot) && Locals[Slot].Kind == ELocalKind::I32
				    ? static_cast<std::int32_t*>(Locals[Slot].Storage)
				    : nullptr;
			};
			std::int32_t* const Counter = I32(D.CounterSlot);
			std::int32_t* const Limit = I32(D.LimitSlot);
			std::int32_t* const State = I32(D.StateSlot);
			std::int32_t* const Position = I32(D.PositionSlot);
			std::int32_t* const Total = I32(D.TotalSlot);
			if (!Counter || !Limit || !State || !Position || !Total || !Bind(D.OrderSlot) || Locals[D.OrderSlot].Kind != ELocalKind::ArrayI32 || !Bind(D.KeysSlot) || Locals[D.KeysSlot].Kind != ELocalKind::ArrayString || !Bind(D.OtherSlot) || Locals[D.OtherSlot].Kind != ELocalKind::ArrayString || !Bind(D.MapSlot) || Locals[D.MapSlot].Kind != ELocalKind::MapStringI32)
			{
				return Result;
			}
			std::int32_t CounterValue = *Counter;
			const std::int32_t LimitValue = *Limit;
			std::int32_t StateValue = *State;
			std::int32_t PositionValue = *Position;
			std::int32_t TotalValue = *Total;
			const auto CommitState = [&]() noexcept
			{
				*Counter = CounterValue;
				*State = StateValue;
				*Position = PositionValue;
				*Total = TotalValue;
			};
			auto& Order = *static_cast<std::vector<std::int32_t>*>(
			    Locals[D.OrderSlot].Storage);
			auto& Keys = *static_cast<std::vector<std::string>*>(
			    Locals[D.KeysSlot].Storage);
			auto& Other = *static_cast<std::vector<std::string>*>(
			    Locals[D.OtherSlot].Storage);
			auto& Values = *static_cast<FMap::FStringI32Entries*>(
			    Locals[D.MapSlot].Storage);
			const auto FailAt = [&](const std::size_t InstructionIndex,
			                        const char* Error) -> FTypedLoopExecutionResult
			{
				CommitState();
				const std::size_t Offset = Plan.Instructions[InstructionIndex].BytecodeOffset;
				Vm.JitRefineRuntimeErrorLocation(Offset);
				Vm.JitReportRuntimeError(Error);
				return {ETypedLoopExecution::Failed, HeaderOffset, Offset};
			};
			std::size_t Completed = 0;
			while (CounterValue < LimitValue)
			{
				std::int32_t Product = 0;
				std::int32_t Sum = 0;
				if (!Runtime::CheckedMul(StateValue, D.StateMultiplier, Product))
				{
					return FailAt(6u, "integer overflow");
				}
				if (!Runtime::CheckedAdd(Product, D.StateIncrement, Sum))
				{
					return FailAt(8u, "integer overflow");
				}
				if (D.StateModulus == 0)
				{
					return FailAt(10u, "division by zero");
				}
				if (Sum == std::numeric_limits<std::int32_t>::min() && D.StateModulus == -1)
				{
					return FailAt(10u, "integer overflow");
				}
				StateValue = Sum % D.StateModulus;
				if (D.CounterModulus == 0)
				{
					return FailAt(17u, "division by zero");
				}
				if (CounterValue == std::numeric_limits<std::int32_t>::min() && D.CounterModulus == -1)
				{
					return FailAt(17u, "integer overflow");
				}
				const std::int32_t CounterIndex = CounterValue % D.CounterModulus;
				if (!Runtime::CheckedAdd(StateValue, CounterIndex, Sum))
				{
					return FailAt(18u, "integer overflow");
				}
				if (D.PositionModulus == 0)
				{
					return FailAt(20u, "division by zero");
				}
				if (Sum == std::numeric_limits<std::int32_t>::min() && D.PositionModulus == -1)
				{
					return FailAt(20u, "integer overflow");
				}
				const std::int32_t OrderIndex = Sum % D.PositionModulus;
				if (OrderIndex < 0 || static_cast<std::size_t>(OrderIndex) >= Order.size())
				{
					return FailAt(21u, "array index out of bounds");
				}
				PositionValue = Order[static_cast<std::size_t>(OrderIndex)];
				if (PositionValue < 0 || static_cast<std::size_t>(PositionValue) >= Keys.size() || static_cast<std::size_t>(PositionValue) >= Other.size())
				{
					return FailAt(27u, "array index out of bounds");
				}
				const std::string& Key = Keys[static_cast<std::size_t>(PositionValue)];
				const std::int32_t* const Value = Values.Find(Key);
				if (!Value)
				{
					return FailAt(28u, "map key not found");
				}
				if (!Runtime::CheckedAdd(TotalValue, *Value, Sum))
				{
					return FailAt(29u, "integer overflow");
				}
				if (D.TotalModulus == 0)
				{
					return FailAt(31u, "division by zero");
				}
				if (Sum == std::numeric_limits<std::int32_t>::min() && D.TotalModulus == -1)
				{
					return FailAt(31u, "integer overflow");
				}
				TotalValue = Sum % D.TotalModulus;
				if (Key == Other[static_cast<std::size_t>(PositionValue)])
				{
					if (!Runtime::CheckedAdd(TotalValue, D.EqualityIncrement, Sum))
					{
						return FailAt(43u, "integer overflow");
					}
					if (Sum == std::numeric_limits<std::int32_t>::min() && D.TotalModulus == -1)
					{
						return FailAt(45u, "integer overflow");
					}
					TotalValue = Sum % D.TotalModulus;
				}
				if (!Runtime::CheckedAdd(CounterValue, 1, Sum))
				{
					return FailAt(47u, "integer overflow");
				}
				CounterValue = Sum;
				++Completed;
			}
			CommitState();
			Vm.RecordJitArrayFastPathGet(Completed * 4u);
			Vm.RecordJitScalarMapFastPathGet(Completed);
			return {ETypedLoopExecution::Completed, Plan.ExitOffset, Plan.HeaderOffset};
		}
		if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::StateIndirectVirtualTransform)
		{
			if (!Vm.JitCanUseDirectScalarMethod() || Vm.JitHasInstanceCallInterceptor())
			{
				return Result;
			}
			const auto& D = Plan.StateVirtual;
			const auto I32 = [&](const std::uint16_t Slot) -> std::int32_t*
			{
				return Bind(Slot) && Locals[Slot].Kind == ELocalKind::I32
				    ? static_cast<std::int32_t*>(Locals[Slot].Storage)
				    : nullptr;
			};
			std::int32_t* const Counter = I32(D.CounterSlot);
			std::int32_t* const Limit = D.bLimitImmediate ? nullptr : I32(D.LimitSlot);
			std::int32_t* const State = I32(D.StateSlot);
			std::int32_t* const Position = D.bDirectStateIndex
			    ? nullptr
			    : I32(D.PositionSlot);
			std::int32_t* const Choice = I32(D.ChoiceSlot);
			std::int32_t* const Accumulator = I32(D.AccumulatorSlot);
			if (!Counter || (!D.bLimitImmediate && !Limit) || !State || (!D.bDirectStateIndex && !Position) || !Choice || !Accumulator || !Bind(D.ChoicesSlot) || Locals[D.ChoicesSlot].Kind != ELocalKind::ArrayI32)
			{
				return Result;
			}
			std::int32_t CounterValue = *Counter;
			const std::int32_t LimitValue = D.bLimitImmediate
			    ? D.LimitImmediate
			    : *Limit;
			std::int32_t StateValue = *State;
			std::int32_t PositionValue = Position ? *Position : StateValue;
			std::int32_t ChoiceValue = *Choice;
			std::int32_t AccumulatorValue = *Accumulator;
			const auto CommitState = [&]() noexcept
			{
				*Counter = CounterValue;
				*State = StateValue;
				if (Position)
				{
					*Position = PositionValue;
				}
				*Choice = ChoiceValue;
				*Accumulator = AccumulatorValue;
			};
			if (CounterValue >= LimitValue)
			{
				// Do not resolve receivers or compile method dependencies for a loop
				// whose bytecode header would branch directly to the exit.
				return {ETypedLoopExecution::Completed,
				    Plan.ExitOffset, Plan.HeaderOffset};
			}
			auto& Choices = *static_cast<std::vector<std::int32_t>*>(
			    Locals[D.ChoicesSlot].Storage);
			FValue* const SelectedValue =
			    Vm.JitBorrowDirectLinearLocal(D.SelectedReceiverSlot);
			std::array<FValue*, 8> ReceiverValues{};
			std::array<std::int32_t, 8> Addends{};
			std::array<std::int32_t, 8> Moduli{};
			bool bAllResolved = SelectedValue != nullptr;
			for (std::size_t Receiver = 0;
			    bAllResolved && Receiver < D.ReceiverCount; ++Receiver)
			{
				ReceiverValues[Receiver] =
				    Vm.JitBorrowDirectLinearLocal(D.ReceiverSlots[Receiver]);
				FScriptObject* Object = nullptr;
				bool bHandled = false;
				bAllResolved = ReceiverValues[Receiver] && Vm.JitBorrowDirectI32TransformMethod(D.ReceiverSlots[Receiver], D.MethodNameIndex, Plan.Instructions[D.CallInstructionIndex].BytecodeOffset, Object, Addends[Receiver], Moduli[Receiver], bHandled) && bHandled && Object;
			}
			if (Vm.HasError())
			{
				return {ETypedLoopExecution::Failed, HeaderOffset,
				    Plan.Instructions[D.CallInstructionIndex].BytecodeOffset};
			}
			const auto* const InitialSelected = SelectedValue
			    ? std::get_if<FObjectHandle>(SelectedValue)
			    : nullptr;
			bAllResolved = bAllResolved && InitialSelected && *InitialSelected && std::any_of(ReceiverValues.begin(), ReceiverValues.begin() + D.ReceiverCount, [&](const FValue* const ReceiverValue)
			{
				const auto* const Handle = ReceiverValue
				    ? std::get_if<FObjectHandle>(ReceiverValue)
				    : nullptr;
				return Handle && *Handle && *Handle == *InitialSelected;
			});
			if (!bAllResolved)
			{
				return Result;
			}
			const auto FailAt = [&](const std::size_t InstructionIndex,
			                        const char* Error) -> FTypedLoopExecutionResult
			{
				CommitState();
				const std::size_t Offset = Plan.Instructions[InstructionIndex].BytecodeOffset;
				Vm.JitRefineRuntimeErrorLocation(Offset);
				Vm.JitReportRuntimeError(Error);
				return {ETypedLoopExecution::Failed, HeaderOffset, Offset};
			};
			std::size_t Completed = 0;
			std::size_t LastSelected = 0;
			while (CounterValue < LimitValue)
			{
				std::int32_t Product = 0;
				std::int32_t Sum = 0;
				if (!Runtime::CheckedMul(StateValue, D.StateMultiplier, Product))
				{
					return FailAt(D.StateMulInstruction, "integer overflow");
				}
				if (!Runtime::CheckedAdd(Product, D.StateIncrement, Sum))
				{
					return FailAt(D.StateAddInstruction, "integer overflow");
				}
				if (D.StateModulus == 0)
				{
					return FailAt(D.StateModInstruction, "division by zero");
				}
				if (Sum == std::numeric_limits<std::int32_t>::min() && D.StateModulus == -1)
				{
					return FailAt(D.StateModInstruction, "integer overflow");
				}
				StateValue = Sum % D.StateModulus;
				if (D.bDirectStateIndex)
				{
					PositionValue = StateValue;
				}
				else
				{
					if (D.CounterModulus == 0)
					{
						return FailAt(D.CounterModInstruction, "division by zero");
					}
					if (CounterValue == std::numeric_limits<std::int32_t>::min() && D.CounterModulus == -1)
					{
						return FailAt(D.CounterModInstruction, "integer overflow");
					}
					const std::int32_t CounterIndex = CounterValue % D.CounterModulus;
					if (!Runtime::CheckedMul(
					        CounterIndex, D.PositionMultiplier, Product))
					{
						return FailAt(D.PositionMulInstruction, "integer overflow");
					}
					if (!Runtime::CheckedAdd(StateValue, Product, Sum))
					{
						return FailAt(D.PositionAddInstruction, "integer overflow");
					}
					if (D.PositionModulus == 0)
					{
						return FailAt(D.PositionModInstruction, "division by zero");
					}
					if (Sum == std::numeric_limits<std::int32_t>::min() && D.PositionModulus == -1)
					{
						return FailAt(D.PositionModInstruction, "integer overflow");
					}
					PositionValue = Sum % D.PositionModulus;
				}
				if (PositionValue < 0 || static_cast<std::size_t>(PositionValue) >= Choices.size())
				{
					return FailAt(D.LookupInstruction, "array index out of bounds");
				}
				ChoiceValue = Choices[static_cast<std::size_t>(PositionValue)];

				LastSelected = D.ReceiverCount - 1u;
				for (std::size_t Receiver = 0;
				    Receiver + 1u < D.ReceiverCount; ++Receiver)
				{
					if (ChoiceValue == D.SelectorValues[Receiver])
					{
						LastSelected = Receiver;
						break;
					}
				}
				if (!Runtime::CheckedAdd(
				        AccumulatorValue, Addends[LastSelected], Sum))
				{
					*SelectedValue = *ReceiverValues[LastSelected];
					if (Completed != 0 && !Vm.JitCommitDirectI32TransformMethod(Completed))
					{
						CommitState();
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    Plan.Instructions[D.CallInstructionIndex].BytecodeOffset};
					}
					CommitState();
					Vm.JitReportDirectI32TransformFailure(
					    D.ReceiverSlots[LastSelected], D.MethodNameIndex, 0xFFFFu,
					    Plan.Instructions[D.CallInstructionIndex].BytecodeOffset,
					    6u, "integer overflow");
					return {ETypedLoopExecution::Failed, HeaderOffset,
					    Plan.Instructions[D.CallInstructionIndex].BytecodeOffset};
				}
				if (Moduli[LastSelected] == -1 && Sum == std::numeric_limits<std::int32_t>::min())
				{
					*SelectedValue = *ReceiverValues[LastSelected];
					if (Completed != 0 && !Vm.JitCommitDirectI32TransformMethod(Completed))
					{
						CommitState();
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    Plan.Instructions[D.CallInstructionIndex].BytecodeOffset};
					}
					CommitState();
					Vm.JitReportDirectI32TransformFailure(
					    D.ReceiverSlots[LastSelected], D.MethodNameIndex, 0xFFFFu,
					    Plan.Instructions[D.CallInstructionIndex].BytecodeOffset,
					    10u, "integer overflow");
					return {ETypedLoopExecution::Failed, HeaderOffset,
					    Plan.Instructions[D.CallInstructionIndex].BytecodeOffset};
				}
				AccumulatorValue = Moduli[LastSelected] == 0
				    ? Sum
				    : Sum % Moduli[LastSelected];
				if (!Runtime::CheckedAdd(CounterValue, 1, Sum))
				{
					return FailAt(D.CounterInstruction, "integer overflow");
				}
				CounterValue = Sum;
				++Completed;
			}
			if (Completed != 0 && !Vm.JitCommitDirectI32TransformMethod(Completed))
			{
				CommitState();
				return {ETypedLoopExecution::Failed, HeaderOffset,
				    Plan.Instructions[D.CallInstructionIndex].BytecodeOffset};
			}
			CommitState();
			if (Completed != 0)
			{
				*SelectedValue = *ReceiverValues[LastSelected];
			}
			Vm.RecordJitArrayFastPathGet(Completed);
			return {ETypedLoopExecution::Completed, Plan.ExitOffset, Plan.HeaderOffset};
		}
		if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::StateArrayVirtualTransform)
		{
			if (!Vm.JitCanUseDirectScalarMethod() || Vm.JitHasInstanceCallInterceptor())
			{
				return Result;
			}
			const auto& D = Plan.StateArrayVirtual;
			const auto I32 = [&](const std::uint16_t Slot) -> std::int32_t*
			{
				return Bind(Slot) && Locals[Slot].Kind == ELocalKind::I32
				    ? static_cast<std::int32_t*>(Locals[Slot].Storage)
				    : nullptr;
			};
			std::int32_t* const Counter = I32(D.CounterSlot);
			std::int32_t* const Limit = I32(D.LimitSlot);
			std::int32_t* const State = I32(D.StateSlot);
			std::int32_t* const Index = D.bDirectReceiverIndex ? nullptr : I32(D.IndexSlot);
			std::int32_t* const Accumulator = I32(D.AccumulatorSlot);
			FValue* const Selected =
			    Vm.JitBorrowDirectLinearLocal(D.SelectedReceiverSlot);
			if (!Counter || !Limit || !State || (!D.bDirectReceiverIndex && !Index) || !Accumulator || !Selected || (!D.bDirectReceiverIndex && (!Bind(D.OrderSlot) || Locals[D.OrderSlot].Kind != ELocalKind::ArrayI32)) || !Bind(D.ReceiverArraySlot) || Locals[D.ReceiverArraySlot].Kind != ELocalKind::ArrayObject)
			{
				return Result;
			}
			const std::vector<std::int32_t> NoOrder;
			const auto& Order = D.bDirectReceiverIndex ? NoOrder
			                                           : *static_cast<std::vector<std::int32_t>*>(Locals[D.OrderSlot].Storage);
			auto& Receivers = *static_cast<std::vector<FValue>*>(
			    Locals[D.ReceiverArraySlot].Storage);
			if (Receivers.empty())
			{
				return Result;
			}

			using FResolvedTarget = FArrayVirtualResolvedTarget;
			constexpr std::size_t InlineTargetCount = 16u;
			std::array<FResolvedTarget, InlineTargetCount> InlineTargets{};
			std::vector<FResolvedTarget> HeapTargets;
			if (Receivers.size() > InlineTargetCount)
			{
				HeapTargets.resize(Receivers.size());
			}
			FResolvedTarget* const Targets = Receivers.size() <= InlineTargetCount
			    ? InlineTargets.data()
			    : HeapTargets.data();
			for (std::size_t ReceiverIndex = 0;
			    ReceiverIndex < Receivers.size(); ++ReceiverIndex)
			{
				const auto* const Handle =
				    std::get_if<FObjectHandle>(&Receivers[ReceiverIndex]);
				if (!Handle || !*Handle)
				{
					return Result;
				}
				std::uint16_t FunctionIndex = 0xFFFFu;
				bool bHandled = false;
				if (!Vm.JitResolveDirectI32MethodTarget(
				        *Handle, D.MethodNameIndex, 2u,
				        Plan.Instructions[D.CallInstructionIndex].BytecodeOffset,
				        FunctionIndex, bHandled))
				{
					return {ETypedLoopExecution::Failed, HeaderOffset,
					    Plan.Instructions[D.CallInstructionIndex].BytecodeOffset};
				}
				if (!bHandled)
				{
					return Result;
				}
				const auto Found = std::find_if(
				    D.Methods.begin(), D.Methods.end(),
				    [FunctionIndex](
				        const FTypedLoopPlan::FPureI32MethodExpression& Candidate)
				{
					return Candidate.FunctionIndex == FunctionIndex;
				});
				if (Found == D.Methods.end())
				{
					return Result;
				}
				FResolvedTarget Target;
				Target.Value = &Receivers[ReceiverIndex];
				Target.Expression = &*Found;
				const auto& M = Found->Instructions;
				if (M.size() == 9u && M[0].Opcode == EOpcode::LoadLocal && M[0].Operand0 == 0u && M[1].Opcode == EOpcode::ConstI32 && M[2].Opcode == EOpcode::Add && M[3].Opcode == EOpcode::LoadLocal && M[3].Operand0 == 1u && M[4].Opcode == EOpcode::ConstI32 && M[5].Opcode == EOpcode::Mod && M[6].Opcode == EOpcode::Add && M[7].Opcode == EOpcode::ConstI32 && M[8].Opcode == EOpcode::Mod)
				{
					Target.Intrinsic = EFastArrayVirtualIntrinsic::AddSaltModulo;
					Target.Operand0 = M[1].Immediate;
					Target.Operand1 = M[4].Immediate;
					Target.Operand2 = M[7].Immediate;
					Target.Operand1Reducer = FPositiveI32Reducer(Target.Operand1);
					Target.Operand2Reducer = FPositiveI32Reducer(Target.Operand2);
				}
				else if (M.size() == 9u && M[0].Opcode == EOpcode::LoadLocal && M[0].Operand0 == 0u && M[1].Opcode == EOpcode::LoadLocal && M[1].Operand0 == 1u && M[2].Opcode == EOpcode::ConstI32 && M[3].Opcode == EOpcode::BitAnd && M[4].Opcode == EOpcode::BitXor && M[5].Opcode == EOpcode::ConstI32 && M[6].Opcode == EOpcode::Add && M[7].Opcode == EOpcode::ConstI32 && M[8].Opcode == EOpcode::Mod)
				{
					Target.Intrinsic = EFastArrayVirtualIntrinsic::XorSaltMaskAddModulo;
					Target.Operand0 = M[2].Immediate;
					Target.Operand1 = M[5].Immediate;
					Target.Operand2 = M[7].Immediate;
					Target.Operand2Reducer = FPositiveI32Reducer(Target.Operand2);
				}
				Targets[ReceiverIndex] = Target;
			}

			std::int32_t CounterValue = *Counter;
			const std::int32_t LimitValue = *Limit;
			std::int32_t StateValue = *State;
			std::int32_t IndexValue = Index ? *Index : 0;
			std::int32_t AccumulatorValue = *Accumulator;
			const FValue* LastSelected = nullptr;
			const auto CommitState = [&]()
			{
				*Counter = CounterValue;
				*State = StateValue;
				if (Index)
				{
					*Index = IndexValue;
				}
				*Accumulator = AccumulatorValue;
				if (LastSelected)
				{
					*Selected = *LastSelected;
				}
			};
			std::size_t Completed = 0;
			std::size_t ArrayGets = 0;
			const bool bRangeProvenStateRecurrence = StateValue >= 0 && StateValue < D.StateModulus && D.StateModulus > 0 && D.StateMultiplier >= 0 && D.StateIncrement >= 0 && static_cast<std::int64_t>(D.StateModulus - 1) * D.StateMultiplier + D.StateIncrement <= std::numeric_limits<std::int32_t>::max();
			const bool bRangeProvenCounter = CounterValue >= 0 && LimitValue >= CounterValue;
			const FPositiveI32Reducer FastStateModulo(D.StateModulus);
			const auto CommitCalls = [&]() -> bool
			{
				return Completed == 0u || Vm.JitCommitDirectScalarMethod(EJitScalarValueKind::I32, Completed);
			};
			const auto FailAt = [&](const std::size_t InstructionIndex,
			                        const char* const Error) -> FTypedLoopExecutionResult
			{
				CommitState();
				(void)CommitCalls();
				Vm.RecordJitArrayFastPathGet(ArrayGets);
				const std::uint32_t Offset =
				    Plan.Instructions[InstructionIndex].BytecodeOffset;
				Vm.JitRefineRuntimeErrorLocation(Offset);
				Vm.JitReportRuntimeError(Error);
				return {ETypedLoopExecution::Failed, HeaderOffset, Offset};
			};

			const auto Evaluate = [&](const FTypedLoopPlan::FPureI32MethodExpression& Method,
			                          const std::int32_t Argument0, const std::int32_t Argument1,
			                          std::int32_t& OutValue, std::size_t& OutFailureOffset,
			                          const char*& OutError) -> bool
			{
				if (Method.NativeCode)
				{
					std::uint32_t FailureOffset = InvalidJitInstructionOffset;
					const std::int32_t Status = reinterpret_cast<
					    JitNative::FPureI32ExpressionKernel>(Method.NativeCode)(
					    Argument0, Argument1, &OutValue, &FailureOffset);
					if (Status == 0)
					{
						return true;
					}
					OutFailureOffset = FailureOffset;
					switch (static_cast<EJitRuntimeStatus>(Status))
					{
					case EJitRuntimeStatus::IntegerOverflow:
						{
							OutError = "integer overflow";
							break;
						}
					case EJitRuntimeStatus::DivisionByZero:
						{
							OutError = "division by zero";
							break;
						}
					default:
						{
							OutError = "JIT scalar method failed";
							break;
						}
					}
					return false;
				}
				std::array<std::int32_t, 32> Stack{};
				std::size_t Depth = 0;
				for (const FTypedLoopInstruction& Instruction : Method.Instructions)
				{
					switch (Instruction.Opcode)
					{
					case EOpcode::LoadLocal:
						{
							Stack[Depth++] = Instruction.Operand0 == 0u
							    ? Argument0
							    : Argument1;
							break;
						}
					case EOpcode::ConstI32:
						{
							Stack[Depth++] = Instruction.Immediate;
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
							const std::int32_t Right = Stack[--Depth];
							const std::int32_t Left = Stack[--Depth];
							std::int32_t Value = 0;
							if (Instruction.Opcode == EOpcode::BitAnd)
							{
								Value = Left & Right;
							}
							else if (Instruction.Opcode == EOpcode::BitOr)
							{
								Value = Left | Right;
							}
							else if (Instruction.Opcode == EOpcode::BitXor)
							{
								Value = Left ^ Right;
							}
							else if (!TryCheckedArithmetic(
							             Instruction.Opcode, Left, Right, Value, OutError))
							{
								OutFailureOffset = Instruction.BytecodeOffset;
								return false;
							}
							Stack[Depth++] = Value;
							break;
						}
					default:
						{
							OutFailureOffset = Instruction.BytecodeOffset;
							OutError = "JIT pure scalar method plan mismatch";
							return false;
						}
					}
				}
				if (Depth != 1u)
				{
					OutFailureOffset = 0u;
					OutError = "JIT pure scalar method plan mismatch";
					return false;
				}
				OutValue = Stack[0];
				return true;
			};
			const auto EvaluateTarget = [&](const FResolvedTarget& Target,
			                                const std::int32_t Argument0, const std::int32_t Argument1,
			                                std::int32_t& OutValue, std::size_t& OutFailureOffset,
			                                const char*& OutError) -> bool
			{
				const auto& M = Target.Expression->Instructions;
				std::int32_t Intermediate = 0;
				if (Target.Intrinsic == EFastArrayVirtualIntrinsic::AddSaltModulo)
				{
					if (!Runtime::CheckedAdd(Argument0, Target.Operand0, Intermediate))
					{
						OutFailureOffset = M[2].BytecodeOffset;
						OutError = "integer overflow";
						return false;
					}
					if (Target.Operand1 == 0)
					{
						OutFailureOffset = M[5].BytecodeOffset;
						OutError = "division by zero";
						return false;
					}
					if (Argument1 == std::numeric_limits<std::int32_t>::min() && Target.Operand1 == -1)
					{
						OutFailureOffset = M[5].BytecodeOffset;
						OutError = "integer overflow";
						return false;
					}
					const std::int32_t Salt = Argument1 >= 0 && Target.Operand1 > 0
					    ? static_cast<std::int32_t>(Target.Operand1Reducer.Mod(
					          static_cast<std::uint32_t>(Argument1)))
					    : Argument1 % Target.Operand1;
					if (!Runtime::CheckedAdd(Intermediate, Salt, OutValue))
					{
						OutFailureOffset = M[6].BytecodeOffset;
						OutError = "integer overflow";
						return false;
					}
				}
				else if (Target.Intrinsic == EFastArrayVirtualIntrinsic::XorSaltMaskAddModulo)
				{
					Intermediate = Argument0 ^ (Argument1 & Target.Operand0);
					if (!Runtime::CheckedAdd(Intermediate, Target.Operand1, OutValue))
					{
						OutFailureOffset = M[6].BytecodeOffset;
						OutError = "integer overflow";
						return false;
					}
				}
				else
				{
					return Evaluate(*Target.Expression, Argument0, Argument1,
					    OutValue, OutFailureOffset, OutError);
				}
				if (Target.Operand2 == 0)
				{
					OutFailureOffset = M[8].BytecodeOffset;
					OutError = "division by zero";
					return false;
				}
				if (OutValue == std::numeric_limits<std::int32_t>::min() && Target.Operand2 == -1)
				{
					OutFailureOffset = M[8].BytecodeOffset;
					OutError = "integer overflow";
					return false;
				}
				OutValue = OutValue >= 0 && Target.Operand2 > 0
				    ? static_cast<std::int32_t>(Target.Operand2Reducer.Mod(
				          static_cast<std::uint32_t>(OutValue)))
				    : OutValue % Target.Operand2;
				return true;
			};

			const std::int64_t RemainingIterations =
			    static_cast<std::int64_t>(LimitValue) - CounterValue;
			bool bCanUseIntrinsicKernel = !D.bDirectReceiverIndex && RemainingIterations > 0 && bRangeProvenStateRecurrence && bRangeProvenCounter && AccumulatorValue >= 0;
			std::int64_t MaxAccumulator = AccumulatorValue;
			std::int64_t MinResultModulus = std::numeric_limits<std::int32_t>::max();
			std::int64_t MaxResultGrowth = 0;
			if (bCanUseIntrinsicKernel)
			{
				for (std::size_t TargetIndex = 0;
				    TargetIndex < Receivers.size(); ++TargetIndex)
				{
					const FResolvedTarget& Target = Targets[TargetIndex];
					if (Target.Intrinsic == EFastArrayVirtualIntrinsic::None || Target.Operand2 <= 0)
					{
						bCanUseIntrinsicKernel = false;
						break;
					}
					MaxAccumulator = std::max<std::int64_t>(
					    MaxAccumulator, Target.Operand2 - 1);
					MinResultModulus = std::min<std::int64_t>(
					    MinResultModulus, Target.Operand2);
				}
			}
			const std::int64_t MaxState = D.StateModulus - 1;
			if (bCanUseIntrinsicKernel && MaxState + MaxAccumulator <= std::numeric_limits<std::int32_t>::max())
			{
				for (std::size_t TargetIndex = 0;
				    TargetIndex < Receivers.size(); ++TargetIndex)
				{
					const FResolvedTarget& Target = Targets[TargetIndex];
					if (Target.Intrinsic == EFastArrayVirtualIntrinsic::AddSaltModulo)
					{
						const std::int64_t MaxSalt = Target.Operand1 > 0
						    ? std::min<std::int64_t>(MaxState, Target.Operand1 - 1)
						    : -1;
						if (Target.Operand0 < 0 || MaxSalt < 0 || MaxAccumulator + Target.Operand0 + MaxSalt > std::numeric_limits<std::int32_t>::max())
						{
							bCanUseIntrinsicKernel = false;
							break;
						}
						MaxResultGrowth = std::max<std::int64_t>(MaxResultGrowth,
						    static_cast<std::int64_t>(Target.Operand0) + MaxSalt);
					}
					else
					{
						if (Target.Operand0 < 0 || Target.Operand1 < 0)
						{
							bCanUseIntrinsicKernel = false;
							break;
						}
						const std::int64_t MaxXorInput = std::max<std::int64_t>(
						    MaxAccumulator,
						    std::min<std::int64_t>(MaxState, Target.Operand0));
						std::int64_t MaxXor = 0;
						for (std::int64_t Bit = 1; Bit <= MaxXorInput; Bit <<= 1)
						{
							MaxXor = (Bit << 1) - 1;
						}
						if (MaxXor + Target.Operand1 > std::numeric_limits<std::int32_t>::max())
						{
							bCanUseIntrinsicKernel = false;
							break;
						}
						MaxResultGrowth = std::max<std::int64_t>(MaxResultGrowth,
						    static_cast<std::int64_t>(Target.Operand0) + Target.Operand1);
					}
				}
			}
			else
			{
				bCanUseIntrinsicKernel = false;
			}
			bool bCanSkipResultReduction = false;
			if (bCanUseIntrinsicKernel)
			{
				// Every supported intrinsic is non-decreasing by at most this bound:
				// x ^ mask <= x + mask for non-negative x/mask.  If even the
				// worst target sequence cannot reach the smallest result modulus,
				// all per-iteration result reductions are identities.
				const std::int64_t GrowthBudget =
				    MinResultModulus - 1 - AccumulatorValue;
				bCanSkipResultReduction = GrowthBudget >= 0 && (MaxResultGrowth == 0 || RemainingIterations <= GrowthBudget / MaxResultGrowth);
			}
			std::int64_t MaxPosition = -1;
			if (bCanUseIntrinsicKernel)
			{
				const std::int64_t MaxPositionSource = MaxState + MaxAccumulator;
				if (D.bPositionUsesMask && D.PositionOperand >= 0)
				{
					MaxPosition = std::min<std::int64_t>(
					    MaxPositionSource, D.PositionOperand);
				}
				else if (!D.bPositionUsesMask && D.PositionOperand > 0)
				{
					MaxPosition = std::min<std::int64_t>(
					    MaxPositionSource, D.PositionOperand - 1);
				}
				else
				{
					bCanUseIntrinsicKernel = false;
				}
			}
			if (bCanUseIntrinsicKernel)
			{
				const std::uint64_t PositionsToProve =
				    static_cast<std::uint64_t>(MaxPosition) + 1u;
				bCanUseIntrinsicKernel = MaxPosition >= 0 && static_cast<std::uint64_t>(MaxPosition) < Order.size() && PositionsToProve <= (1u << 20u) && PositionsToProve <= static_cast<std::uint64_t>(RemainingIterations) * 2u;
			}
			if (bCanUseIntrinsicKernel)
			{
				for (std::int64_t Position = 0; Position <= MaxPosition; ++Position)
				{
					const std::int32_t TargetIndex =
					    Order[static_cast<std::size_t>(Position)];
					if (TargetIndex < 0 || static_cast<std::size_t>(TargetIndex) >= Receivers.size())
					{
						bCanUseIntrinsicKernel = false;
						break;
					}
				}
			}
			if (bCanUseIntrinsicKernel)
			{
				const FResolvedTarget* AddTarget = nullptr;
				const FResolvedTarget* XorTarget = nullptr;
				std::int32_t AddIndex = -1;
				if (bCanSkipResultReduction && Receivers.size() == 2u)
				{
					for (std::size_t TargetIndex = 0; TargetIndex < 2u; ++TargetIndex)
					{
						if (Targets[TargetIndex].Intrinsic == EFastArrayVirtualIntrinsic::AddSaltModulo)
						{
							AddTarget = &Targets[TargetIndex];
							AddIndex = static_cast<std::int32_t>(TargetIndex);
						}
						else if (Targets[TargetIndex].Intrinsic == EFastArrayVirtualIntrinsic::XorSaltMaskAddModulo)
						{
							XorTarget = &Targets[TargetIndex];
						}
					}
				}
				const FFastArrayVirtualResult Fast = AddTarget && XorTarget
				    ? ExecuteFastArrayVirtualPairNoReductionKernel(D, Order.data(),
				          *AddTarget, *XorTarget, AddIndex, CounterValue, LimitValue,
				          StateValue, AccumulatorValue)
				    : bCanSkipResultReduction
				    ? ExecuteFastArrayVirtualKernel<false>(D, Order.data(), Targets,
				          CounterValue, LimitValue, StateValue, AccumulatorValue)
				    : ExecuteFastArrayVirtualKernel<true>(D, Order.data(), Targets,
				          CounterValue, LimitValue, StateValue, AccumulatorValue);
				CounterValue = Fast.Counter;
				StateValue = Fast.State;
				IndexValue = Fast.Index;
				AccumulatorValue = Fast.Accumulator;
				Completed = Fast.Completed;
				ArrayGets = Completed * 2u;
				LastSelected = Targets[static_cast<std::size_t>(Fast.Index)].Value;
				CommitState();
				if (!CommitCalls())
				{
					return {ETypedLoopExecution::Failed, HeaderOffset,
					    Plan.Instructions[D.CallInstructionIndex].BytecodeOffset};
				}
				Vm.RecordJitArrayFastPathGet(ArrayGets);
				return {ETypedLoopExecution::Completed,
				    Plan.ExitOffset, Plan.HeaderOffset};
			}

			while (CounterValue < LimitValue)
			{
				std::int32_t Sum = 0;
				if (bRangeProvenStateRecurrence)
				{
					const std::uint32_t StateSum = static_cast<std::uint32_t>(StateValue) * static_cast<std::uint32_t>(D.StateMultiplier) + static_cast<std::uint32_t>(D.StateIncrement);
					StateValue = static_cast<std::int32_t>(FastStateModulo.Mod(StateSum));
				}
				else
				{
					std::int32_t Product = 0;
					if (!Runtime::CheckedMul(StateValue, D.StateMultiplier, Product))
					{
						return FailAt(6u, "integer overflow");
					}
					if (!Runtime::CheckedAdd(Product, D.StateIncrement, Sum))
					{
						return FailAt(8u, "integer overflow");
					}
					if (D.StateModulus == 0)
					{
						return FailAt(10u, "division by zero");
					}
					if (Sum == std::numeric_limits<std::int32_t>::min() && D.StateModulus == -1)
					{
						return FailAt(10u, "integer overflow");
					}
					StateValue = Sum % D.StateModulus;
				}

				if (D.bDirectReceiverIndex)
				{
					Sum = StateValue;
				}
				else if (!Runtime::CheckedAdd(StateValue, AccumulatorValue, Sum))
				{
					return FailAt(16u, "integer overflow");
				}
				std::int32_t Position = 0;
				if (D.bPositionUsesMask)
				{
					Position = Sum & D.PositionOperand;
				}
				else
				{
					if (D.PositionOperand == 0)
					{
						return FailAt(D.PositionInstruction, "division by zero");
					}
					if (Sum == std::numeric_limits<std::int32_t>::min() && D.PositionOperand == -1)
					{
						return FailAt(D.PositionInstruction, "integer overflow");
					}
					Position = Sum % D.PositionOperand;
				}
				if (D.bDirectReceiverIndex)
				{
					IndexValue = Position;
				}
				else
				{
					if (Position < 0 || static_cast<std::size_t>(Position) >= Order.size())
					{
						return FailAt(19u, "array index out of bounds");
					}
					IndexValue = Order[static_cast<std::size_t>(Position)];
					++ArrayGets;
				}
				if (IndexValue < 0 || static_cast<std::size_t>(IndexValue) >= Receivers.size())
				{
					return FailAt(D.ReceiverLookupInstruction, "array index out of bounds");
				}
				const FResolvedTarget& Target =
				    Targets[static_cast<std::size_t>(IndexValue)];
				LastSelected = Target.Value;
				++ArrayGets;

				std::int32_t Returned = 0;
				std::size_t CalleeFailureOffset = 0;
				const char* CalleeError = nullptr;
				if (!EvaluateTarget(Target, AccumulatorValue, StateValue,
				        Returned, CalleeFailureOffset, CalleeError))
				{
					CommitState();
					(void)CommitCalls();
					Vm.RecordJitArrayFastPathGet(ArrayGets);
					Vm.JitReportDirectI32TransformFailure(
					    0xFFFFu, D.MethodNameIndex,
					    Target.Expression->FunctionIndex,
					    Plan.Instructions[D.CallInstructionIndex].BytecodeOffset,
					    CalleeFailureOffset,
					    CalleeError ? CalleeError : "JIT scalar method failed");
					return {ETypedLoopExecution::Failed, HeaderOffset,
					    Plan.Instructions[D.CallInstructionIndex].BytecodeOffset};
				}
				AccumulatorValue = Returned;
				if (bRangeProvenCounter)
				{
					++CounterValue;
				}
				else
				{
					if (!Runtime::CheckedAdd(CounterValue, 1, Sum))
					{
						return FailAt(D.CounterInstruction, "integer overflow");
					}
					CounterValue = Sum;
				}
				++Completed;
			}
			CommitState();
			if (!CommitCalls())
			{
				return {ETypedLoopExecution::Failed, HeaderOffset,
				    Plan.Instructions[D.CallInstructionIndex].BytecodeOffset};
			}
			Vm.RecordJitArrayFastPathGet(ArrayGets);
			return {ETypedLoopExecution::Completed, Plan.ExitOffset, Plan.HeaderOffset};
		}
		if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::DirectConstantScalarIndexedUpdate)
		{
			const auto& D = Plan.DirectIndexed;
			const auto I32 = [&](const std::uint16_t Slot) -> std::int32_t*
			{
				return Bind(Slot) && Locals[Slot].Kind == ELocalKind::I32
				    ? static_cast<std::int32_t*>(Locals[Slot].Storage)
				    : nullptr;
			};
			std::int32_t* const Counter = I32(D.CounterSlot);
			std::int32_t* const State = I32(D.StateSlot);
			if (!Counter || !State || !Bind(D.StoredValueSlot) || !Bind(D.ContainerSlot))
			{
				return Result;
			}
			std::int32_t CounterValue = *Counter;
			std::int32_t StateValue = *State;
			const auto Commit = [&]() noexcept
			{
				*Counter = CounterValue;
				*State = StateValue;
			};
			const auto Fail = [&](const std::uint16_t InstructionIndex,
			                      const char* Error) -> FTypedLoopExecutionResult
			{
				Commit();
				const std::uint32_t Offset = Plan.Instructions[InstructionIndex].BytecodeOffset;
				Vm.JitRefineRuntimeErrorLocation(Offset);
				Vm.JitReportRuntimeError(Error);
				return {ETypedLoopExecution::Failed, HeaderOffset, Offset};
			};
			const auto Run = [&](auto* ArrayValues, auto* MapValues,
			                     auto* Stored, const auto Delta) -> FTypedLoopExecutionResult
			{
				using TValue = std::decay_t<decltype(Delta)>;
				std::size_t Completed = 0;
				while (CounterValue < D.LimitImmediate)
				{
					std::int32_t Product = 0;
					std::int32_t Sum = 0;
					if (!Runtime::CheckedMul(StateValue, D.StateMultiplier, Product))
					{
						return Fail(D.StateMulInstruction, "integer overflow");
					}
					if (!Runtime::CheckedAdd(Product, D.StateIncrement, Sum))
					{
						return Fail(D.StateAddInstruction, "integer overflow");
					}
					if (D.StateModulus == 0)
					{
						return Fail(D.StateModInstruction, "division by zero");
					}
					if (Sum == std::numeric_limits<std::int32_t>::min() && D.StateModulus == -1)
					{
						return Fail(D.StateModInstruction, "integer overflow");
					}
					StateValue = Sum % D.StateModulus;
					*State = StateValue;
					TValue* Element = nullptr;
					if (ArrayValues)
					{
						if (StateValue < 0 || static_cast<std::size_t>(StateValue) >= ArrayValues->size())
						{
							return Fail(D.LookupInstruction, "array index out of bounds");
						}
						Element = &(*ArrayValues)[static_cast<std::size_t>(StateValue)];
					}
					else
					{
						Element = MapValues->FindMutable(StateValue);
						if (!Element)
						{
							return Fail(D.LookupInstruction, "map key not found");
						}
					}
					if constexpr (std::is_same_v<TValue, std::int32_t>)
					{
						if (!Runtime::CheckedAdd(*Element, Delta, *Stored))
						{
							return Fail(D.ElementAddInstruction, "integer overflow");
						}
					}
					else
					{
						*Stored = static_cast<TValue>(*Element + Delta);
					}
					*Element = *Stored;
					std::int32_t NextCounter = 0;
					if (!Runtime::CheckedAdd(CounterValue, 1, NextCounter))
					{
						return Fail(D.CounterInstruction, "integer overflow");
					}
					CounterValue = NextCounter;
					++Completed;
				}
				Commit();
				if (ArrayValues)
				{
					Vm.RecordJitArrayFastPathGet(Completed);
					Vm.RecordJitArrayFastPathSet(Completed);
				}
				else
				{
					Vm.RecordJitScalarMapFastPathGet(Completed);
					Vm.RecordJitScalarMapFastPathSet(Completed);
				}
				return {ETypedLoopExecution::Completed, Plan.ExitOffset, Plan.HeaderOffset};
			};
			const FTypedLoopInstruction& DeltaInstruction =
			    Plan.Instructions[14u];
			const FLocalBinding& Container = Locals[D.ContainerSlot];
			const FLocalBinding& Stored = Locals[D.StoredValueSlot];
			if (Container.Kind == ELocalKind::ArrayI32 && Stored.Kind == ELocalKind::I32 && DeltaInstruction.Opcode == EOpcode::ConstI32 && !DeltaInstruction.bImmediateU32)
			{
				return Run(static_cast<std::vector<std::int32_t>*>(Container.Storage),
				    static_cast<FMap::FIntI32Entries*>(nullptr),
				    static_cast<std::int32_t*>(Stored.Storage), DeltaInstruction.Immediate);
			}
			if (Container.Kind == ELocalKind::MapI32I32 && Stored.Kind == ELocalKind::I32 && DeltaInstruction.Opcode == EOpcode::ConstI32 && !DeltaInstruction.bImmediateU32)
			{
				return Run(static_cast<std::vector<std::int32_t>*>(nullptr),
				    static_cast<FMap::FIntI32Entries*>(Container.Storage),
				    static_cast<std::int32_t*>(Stored.Storage), DeltaInstruction.Immediate);
			}
			if (Container.Kind == ELocalKind::ArrayU32 && Stored.Kind == ELocalKind::U32 && DeltaInstruction.bImmediateU32)
			{
				return Run(static_cast<std::vector<std::uint32_t>*>(Container.Storage),
				    static_cast<FMap::FIntU32Entries*>(nullptr),
				    static_cast<std::uint32_t*>(Stored.Storage),
				    static_cast<std::uint32_t>(DeltaInstruction.Immediate));
			}
			if (Container.Kind == ELocalKind::MapI32U32 && Stored.Kind == ELocalKind::U32 && DeltaInstruction.bImmediateU32)
			{
				return Run(static_cast<std::vector<std::uint32_t>*>(nullptr),
				    static_cast<FMap::FIntU32Entries*>(Container.Storage),
				    static_cast<std::uint32_t*>(Stored.Storage),
				    static_cast<std::uint32_t>(DeltaInstruction.Immediate));
			}
			const bool bIntegerDelta = DeltaInstruction.Opcode == EOpcode::ConstI32;
			const double FloatingDelta = bIntegerDelta
			    ? (DeltaInstruction.bImmediateU32
			              ? static_cast<double>(static_cast<std::uint32_t>(DeltaInstruction.Immediate))
			              : static_cast<double>(DeltaInstruction.Immediate))
			    : DeltaInstruction.ImmediateF64;
			if (Container.Kind == ELocalKind::ArrayF32 && Stored.Kind == ELocalKind::F32)
			{
				return Run(static_cast<std::vector<float>*>(Container.Storage),
				    static_cast<FMap::FIntF32Entries*>(nullptr),
				    static_cast<float*>(Stored.Storage), static_cast<float>(FloatingDelta));
			}
			if (Container.Kind == ELocalKind::MapI32F32 && Stored.Kind == ELocalKind::F32)
			{
				return Run(static_cast<std::vector<float>*>(nullptr),
				    static_cast<FMap::FIntF32Entries*>(Container.Storage),
				    static_cast<float*>(Stored.Storage), static_cast<float>(FloatingDelta));
			}
			if (Container.Kind == ELocalKind::ArrayF64 && Stored.Kind == ELocalKind::F64)
			{
				return Run(static_cast<std::vector<double>*>(Container.Storage),
				    static_cast<FMap::FIntF64Entries*>(nullptr),
				    static_cast<double*>(Stored.Storage), FloatingDelta);
			}
			if (Container.Kind == ELocalKind::MapI32F64 && Stored.Kind == ELocalKind::F64)
			{
				return Run(static_cast<std::vector<double>*>(nullptr),
				    static_cast<FMap::FIntF64Entries*>(Container.Storage),
				    static_cast<double*>(Stored.Storage), FloatingDelta);
			}
			return Result;
		}
		if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::StateMapF64Update)
		{
			const auto& D = Plan.StateMapF64;
			const auto I32 = [&](const std::uint16_t Slot) -> std::int32_t*
			{
				return Bind(Slot) && Locals[Slot].Kind == ELocalKind::I32
				    ? static_cast<std::int32_t*>(Locals[Slot].Storage)
				    : nullptr;
			};
			const auto F64 = [&](const std::uint16_t Slot) -> double*
			{
				return Bind(Slot) && Locals[Slot].Kind == ELocalKind::F64
				    ? static_cast<double*>(Locals[Slot].Storage)
				    : nullptr;
			};
			std::int32_t* const Counter = I32(D.CounterSlot);
			std::int32_t* const Limit = I32(D.LimitSlot);
			std::int32_t* const State = I32(D.StateSlot);
			std::int32_t* const Size = I32(D.SizeSlot);
			std::int32_t* const Key = I32(D.KeySlot);
			double* const Value = F64(D.ValueSlot);
			double* const StoredValue = F64(D.StoredValueSlot);
			double* const Total = F64(D.TotalSlot);
			if (!Counter || !Limit || !State || !Size || !Key || !Value || !StoredValue || !Total || !Bind(D.MapSlot) || Locals[D.MapSlot].Kind != ELocalKind::MapI32F64)
			{
				return Result;
			}
			auto& Values = *static_cast<FMap::FIntF64Entries*>(
			    Locals[D.MapSlot].Storage);
			std::int32_t CounterValue = *Counter;
			const std::int32_t LimitValue = *Limit;
			std::int32_t StateValue = *State;
			const std::int32_t SizeValue = *Size;
			std::int32_t KeyValue = *Key;
			double ValueValue = *Value;
			double StoredValueValue = *StoredValue;
			double TotalValue = *Total;
			// This plan has proven that the map's key set is not mutated by the
			// loop. Build a stable zero-based slot view once, then keep hashing and
			// probing out of the hot loop. If the runtime map is sparse or has a
			// different key domain, decline the optimization before changing state.
			std::vector<double*> DenseValues;
			if (CounterValue < LimitValue)
			{
				if (SizeValue <= 0 || !Values.TryGetDenseZeroBasedValues(static_cast<std::size_t>(SizeValue), DenseValues))
				{
					return Result;
				}
			}
			std::size_t Gets = 0;
			std::size_t Sets = 0;
			const auto PhysicalInstructionIndex = [&](const std::size_t LogicalIndex)
			{
				return !D.bHasInlinedCallNop && LogicalIndex > 11u
				    ? LogicalIndex - 1u
				    : LogicalIndex;
			};
			const auto Commit = [&]() noexcept
			{
				*Counter = CounterValue;
				*State = StateValue;
				*Key = KeyValue;
				*Value = ValueValue;
				*StoredValue = StoredValueValue;
				*Total = TotalValue;
			};
			const auto FailAt = [&](const std::size_t InstructionIndex,
			                        const char* const Error) -> FTypedLoopExecutionResult
			{
				Commit();
				Vm.RecordJitScalarMapFastPathGet(Gets);
				Vm.RecordJitScalarMapFastPathSet(Sets);
				const std::uint32_t Offset = Plan.Instructions[PhysicalInstructionIndex(InstructionIndex)].BytecodeOffset;
				Vm.JitRefineRuntimeErrorLocation(Offset);
				Vm.JitReportRuntimeError(Error);
				return {ETypedLoopExecution::Failed, HeaderOffset, Offset};
			};
			// Prove the complete integer domain before omitting per-step checks.
			const std::int64_t LargestState = std::max<std::int64_t>(
			    StateValue, static_cast<std::int64_t>(D.StateModulus) - 1);
			const bool bBounded = CounterValue >= 0 && CounterValue < LimitValue && StateValue >= 0 && D.StateMultiplier >= 0 && D.StateIncrement >= 0 && D.StateModulus > 0 && D.StateValueModulus > 0 && D.KeyMultiplier >= 0 && LargestState * D.StateMultiplier + D.StateIncrement <= std::numeric_limits<std::int32_t>::max() && static_cast<std::int64_t>(LimitValue - 1) * D.KeyMultiplier + D.StateModulus - 1 <= std::numeric_limits<std::int32_t>::max();
			if (bBounded)
			{
				const auto Bounded = ExecuteBoundedStateMapF64(
				    D, DenseValues.data(), SizeValue, CounterValue, LimitValue, StateValue, TotalValue);
				Gets = Sets = static_cast<std::size_t>(LimitValue - CounterValue);
				CounterValue = LimitValue;
				StateValue = Bounded.State;
				KeyValue = Bounded.Key;
				ValueValue = StoredValueValue = Bounded.Value;
				TotalValue = Bounded.Total;
			}
			else
			{
				while (CounterValue < LimitValue)
				{
					std::int32_t Product = 0;
					std::int32_t Sum = 0;
					if (!Runtime::CheckedMul(StateValue, D.StateMultiplier, Product))
					{
						return FailAt(6u, "integer overflow");
					}
					if (!Runtime::CheckedAdd(Product, D.StateIncrement, Sum))
					{
						return FailAt(8u, "integer overflow");
					}
					if (D.StateModulus == 0)
					{
						return FailAt(10u, "division by zero");
					}
					if (Sum == std::numeric_limits<std::int32_t>::min() && D.StateModulus == -1)
					{
						return FailAt(10u, "integer overflow");
					}
					StateValue = Sum % D.StateModulus;

					if (!Runtime::CheckedMul(CounterValue, D.KeyMultiplier, Product))
					{
						return FailAt(16u, "integer overflow");
					}
					if (!Runtime::CheckedAdd(StateValue, Product, Sum))
					{
						return FailAt(17u, "integer overflow");
					}
					if (SizeValue == 0)
					{
						return FailAt(19u, "division by zero");
					}
					if (Sum == std::numeric_limits<std::int32_t>::min() && SizeValue == -1)
					{
						return FailAt(19u, "integer overflow");
					}
					KeyValue = Sum % SizeValue;
					double* const Entry = KeyValue >= 0 && static_cast<std::size_t>(KeyValue) < DenseValues.size()
					    ? DenseValues[static_cast<std::size_t>(KeyValue)]
					    : nullptr;
					if (!Entry)
					{
						return FailAt(23u, "map key not found");
					}
					++Gets;

					if (D.StateValueModulus == 0)
					{
						return FailAt(28u, "division by zero");
					}
					if (StateValue == std::numeric_limits<std::int32_t>::min() && D.StateValueModulus == -1)
					{
						return FailAt(28u, "integer overflow");
					}
					ValueValue = *Entry * D.ValueMultiplier + static_cast<double>(StateValue % D.StateValueModulus) * D.StateValueScale;
					if (ValueValue > D.Threshold)
					{
						ValueValue *= D.ConditionalScale;
					}
					StoredValueValue = ValueValue;
					*Entry = StoredValueValue;
					++Sets;
					TotalValue += StoredValueValue;
					if (!Runtime::CheckedAdd(CounterValue, 1, Sum))
					{
						return FailAt(51u, "integer overflow");
					}
					CounterValue = Sum;
				}
			}
			Commit();
			Vm.RecordJitScalarMapFastPathGet(Gets);
			Vm.RecordJitScalarMapFastPathSet(Sets);
			return {ETypedLoopExecution::Completed, Plan.ExitOffset, Plan.HeaderOffset};
		}
		if (Plan.FusedKind != FTypedLoopPlan::EFusedKind::None)
		{
			const auto& D = Plan.Indirect;
			const auto I32 = [&](const std::uint16_t Slot) -> std::int32_t*
			{
				return Bind(Slot) && Locals[Slot].Kind == ELocalKind::I32
				    ? static_cast<std::int32_t*>(Locals[Slot].Storage)
				    : nullptr;
			};
			std::int32_t* const Counter = I32(D.CounterSlot);
			const bool bImmediateLimit =
			    (Plan.FusedKind == FTypedLoopPlan::EFusedKind::DirectAffineIndexedUpdate && Plan.DirectIndexed.bLimitImmediate) || (Plan.FusedKind == FTypedLoopPlan::EFusedKind::DirectAffineSetContains && Plan.DirectSet.bLimitImmediate);
			std::int32_t* const Limit = bImmediateLimit ? nullptr : I32(D.LimitSlot);
			std::int32_t* const Cursor = I32(D.CursorSlot);
			std::int32_t* const Total = I32(D.TotalSlot);
			if (!Counter || (!bImmediateLimit && !Limit) || !Cursor || !Total)
			{
				return Result;
			}
			// Keep loop-carried primitive state in native locals. Container lookups and
			// mutation accounting are opaque calls from the optimizer's point of view;
			// repeatedly dereferencing VM-local pointers around them otherwise forces
			// spills on every iteration. Commit at every semantic exit so runtime-error
			// state remains identical to the bytecode backend.
			std::int32_t CounterValue = *Counter;
			const std::int32_t LimitValue = bImmediateLimit
			    ? (Plan.FusedKind == FTypedLoopPlan::EFusedKind::DirectAffineIndexedUpdate
			              ? Plan.DirectIndexed.LimitImmediate
			              : Plan.DirectSet.LimitImmediate)
			    : *Limit;
			std::int32_t CursorValue = *Cursor;
			std::int32_t TotalValue = *Total;
			const auto CommitLoopState = [&]() noexcept
			{
				*Counter = CounterValue;
				*Cursor = CursorValue;
				*Total = TotalValue;
			};
			if (!Bind(D.ContainerSlot))
			{
				return Result;
			}
			const bool bNeedsOrder =
			    Plan.FusedKind == FTypedLoopPlan::EFusedKind::StateIndirectIndexedUpdate || Plan.FusedKind == FTypedLoopPlan::EFusedKind::StateIndirectSetContains || Plan.FusedKind == FTypedLoopPlan::EFusedKind::IndirectArrayUpdate || Plan.FusedKind == FTypedLoopPlan::EFusedKind::IndirectMapUpdate || Plan.FusedKind == FTypedLoopPlan::EFusedKind::IndirectSetContains;
			std::vector<std::int32_t>* Order = nullptr;
			if (bNeedsOrder)
			{
				if (!Bind(D.OrderSlot) || Locals[D.OrderSlot].Kind != ELocalKind::ArrayI32)
				{
					return Result;
				}
				Order = static_cast<std::vector<std::int32_t>*>(
				    Locals[D.OrderSlot].Storage);
			}
			const auto FailFused = [&](const std::size_t InstructionIndex,
			                           const char* Error) -> FTypedLoopExecutionResult
			{
				CommitLoopState();
				const auto& Instruction = Plan.Instructions[InstructionIndex];
				Vm.JitRefineRuntimeErrorLocation(Instruction.BytecodeOffset);
				Vm.JitReportRuntimeError(Error);
				return {ETypedLoopExecution::Failed, HeaderOffset,
				    Instruction.BytecodeOffset};
			};
			std::size_t FusedFailureOffset = Plan.HeaderOffset;
			const auto CheckedAddAt = [&](const std::int32_t Left,
			                              const std::int32_t Right, std::int32_t& Out,
			                              const std::size_t InstructionIndex) -> bool
			{
				if (Runtime::CheckedAdd(Left, Right, Out))
				{
					return true;
				}
				CommitLoopState();
				FusedFailureOffset = Plan.Instructions[InstructionIndex].BytecodeOffset;
				Vm.JitRefineRuntimeErrorLocation(
				    FusedFailureOffset);
				Vm.JitReportRuntimeError("integer overflow");
				return false;
			};
			const auto CheckedMulAt = [&](const std::int32_t Left,
			                              const std::int32_t Right, std::int32_t& Out,
			                              const std::size_t InstructionIndex) -> bool
			{
				if (Runtime::CheckedMul(Left, Right, Out))
				{
					return true;
				}
				CommitLoopState();
				FusedFailureOffset = Plan.Instructions[InstructionIndex].BytecodeOffset;
				Vm.JitRefineRuntimeErrorLocation(
				    FusedFailureOffset);
				Vm.JitReportRuntimeError("integer overflow");
				return false;
			};
			const auto CheckedModAt = [&](const std::int32_t Left,
			                              const std::int32_t Right, std::int32_t& Out,
			                              const std::size_t InstructionIndex) -> bool
			{
				if (Right == 0)
				{
					CommitLoopState();
					FusedFailureOffset = Plan.Instructions[InstructionIndex].BytecodeOffset;
					Vm.JitRefineRuntimeErrorLocation(
					    FusedFailureOffset);
					Vm.JitReportRuntimeError("division by zero");
					return false;
				}
				if (Left == std::numeric_limits<std::int32_t>::min() && Right == -1)
				{
					CommitLoopState();
					FusedFailureOffset = Plan.Instructions[InstructionIndex].BytecodeOffset;
					Vm.JitRefineRuntimeErrorLocation(
					    FusedFailureOffset);
					Vm.JitReportRuntimeError("integer overflow");
					return false;
				}
				Out = Left % Right;
				return true;
			};
			const auto CheckedDivAt = [&](const std::int32_t Left,
			                              const std::int32_t Right, std::int32_t& Out,
			                              const std::size_t InstructionIndex) -> bool
			{
				if (Right == 0)
				{
					CommitLoopState();
					FusedFailureOffset = Plan.Instructions[InstructionIndex].BytecodeOffset;
					Vm.JitRefineRuntimeErrorLocation(FusedFailureOffset);
					Vm.JitReportRuntimeError("division by zero");
					return false;
				}
				if (Left == std::numeric_limits<std::int32_t>::min() && Right == -1)
				{
					CommitLoopState();
					FusedFailureOffset = Plan.Instructions[InstructionIndex].BytecodeOffset;
					Vm.JitRefineRuntimeErrorLocation(FusedFailureOffset);
					Vm.JitReportRuntimeError("integer overflow");
					return false;
				}
				Out = Left / Right;
				return true;
			};
			struct FInvariantModulo
			{
				std::int32_t Divisor = 0;
				std::uint64_t Reciprocal = 0;
				std::uint32_t Mask = 0;
				std::uint8_t Shift = 0;
				bool bPowerOfTwo = false;
			};
			const auto PrepareModulo = [](const std::int32_t Divisor)
			{
				FInvariantModulo Prepared;
				Prepared.Divisor = Divisor;
				if (Divisor > 0)
				{
					const std::uint32_t Unsigned = static_cast<std::uint32_t>(Divisor);
					Prepared.bPowerOfTwo = (Unsigned & (Unsigned - 1u)) == 0u;
					Prepared.Mask = Unsigned - 1u;
					Prepared.Reciprocal = (std::uint64_t{1} << 32u) / Unsigned;
					if (Prepared.bPowerOfTwo)
					{
						std::uint32_t Remaining = Unsigned;
						while (Remaining > 1u)
						{
							Remaining >>= 1u;
							++Prepared.Shift;
						}
					}
				}
				return Prepared;
			};
			const auto FastModuloAt = [&](const std::int32_t Left,
			                              const FInvariantModulo& Modulo, std::int32_t& Out,
			                              const std::size_t InstructionIndex) -> bool
			{
				// The divisor is loop-invariant. For non-negative operands, an exact
				// reciprocal reduction avoids a hardware division while preserving `%`.
				// Signed/invalid cases retain the authoritative checked implementation.
				if (Left >= 0 && Modulo.Divisor > 0)
				{
					const std::uint32_t Value = static_cast<std::uint32_t>(Left);
					if (Modulo.bPowerOfTwo)
					{
						Out = static_cast<std::int32_t>(Value & Modulo.Mask);
						return true;
					}
					const std::uint64_t Quotient =
					    (std::uint64_t{Value} * Modulo.Reciprocal) >> 32u;
					std::uint32_t Remainder = Value - static_cast<std::uint32_t>(Quotient * static_cast<std::uint32_t>(Modulo.Divisor));
					if (Remainder >= static_cast<std::uint32_t>(Modulo.Divisor))
					{
						Remainder -= static_cast<std::uint32_t>(Modulo.Divisor);
					}
					Out = static_cast<std::int32_t>(Remainder);
					return true;
				}
				return CheckedModAt(
				    Left, Modulo.Divisor, Out, InstructionIndex);
			};
			const auto FastDivideAt = [&](const std::int32_t Left,
			                              const FInvariantModulo& Divisor, std::int32_t& Out,
			                              const std::size_t InstructionIndex) -> bool
			{
				// floor(2^32 / d) can underestimate the unsigned quotient by at
				// most one.  Correct it from the exact remainder, retaining the
				// language's truncation-toward-zero semantics for non-negative input.
				if (Left >= 0 && Divisor.Divisor > 0)
				{
					const std::uint32_t Value = static_cast<std::uint32_t>(Left);
					if (Divisor.bPowerOfTwo)
					{
						Out = static_cast<std::int32_t>(Value >> Divisor.Shift);
						return true;
					}
					std::uint64_t Quotient =
					    (std::uint64_t{Value} * Divisor.Reciprocal) >> 32u;
					const std::uint64_t Product = Quotient * static_cast<std::uint32_t>(Divisor.Divisor);
					if (static_cast<std::uint64_t>(Value) - Product >= static_cast<std::uint32_t>(Divisor.Divisor))
					{
						++Quotient;
					}
					Out = static_cast<std::int32_t>(Quotient);
					return true;
				}
				return CheckedDivAt(
				    Left, Divisor.Divisor, Out, InstructionIndex);
			};
			const FInvariantModulo CursorModulo = PrepareModulo(D.CursorModulus);
			const FInvariantModulo SecondaryIndexModulo =
			    PrepareModulo(D.SecondaryIndexModulus);
			const FInvariantModulo PositionModulo = PrepareModulo(D.PositionModulus);
			const FInvariantModulo DeltaModulo = PrepareModulo(D.DeltaModulus);
			const FInvariantModulo DeltaDivisor = PrepareModulo(D.DeltaDivisor);
			const FInvariantModulo ElementModulo = PrepareModulo(D.ElementModulus);
			const FInvariantModulo TotalModulo = PrepareModulo(D.TotalModulus);
			const auto AdvanceCursor = [&](const std::size_t MulIndex,
			                               const std::size_t AddIndex, const std::size_t ModIndex) -> bool
			{
				std::int32_t Product = 0;
				std::int32_t Sum = 0;
				std::int32_t Next = 0;
				if (!CheckedMulAt(CursorValue, D.CursorMultiplier, Product, MulIndex) || !CheckedAddAt(Product, D.CursorIncrement, Sum, AddIndex) || !FastModuloAt(Sum, CursorModulo, Next, ModIndex))
				{
					return false;
				}
				CursorValue = Next;
				return true;
			};
			const auto OrderAt = [&](const std::int32_t Index,
			                         std::int32_t& Out, const std::size_t InstructionIndex) -> bool
			{
				if (!Order || Index < 0 || static_cast<std::size_t>(Index) >= Order->size())
				{
					CommitLoopState();
					FusedFailureOffset = Plan.Instructions[InstructionIndex].BytecodeOffset;
					Vm.JitRefineRuntimeErrorLocation(
					    FusedFailureOffset);
					Vm.JitReportRuntimeError("array index out of bounds");
					return false;
				}
				Out = (*Order)[static_cast<std::size_t>(Index)];
				return true;
			};

			std::size_t Completed = 0;
			if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::DirectIndexedUpdate)
			{
				std::int32_t* const IndexLocal = I32(D.IndexOrKeySlot);
				std::int32_t* const StoredLocal = I32(D.StoredValueSlot);
				if (!IndexLocal || !StoredLocal)
				{
					return Result;
				}
				std::vector<std::int32_t>* ArrayValues = nullptr;
				FMap::FIntI32Entries* MapValues = nullptr;
				if (Locals[D.ContainerSlot].Kind == ELocalKind::ArrayI32)
				{
					ArrayValues = static_cast<std::vector<std::int32_t>*>(
					    Locals[D.ContainerSlot].Storage);
				}
				else if (Locals[D.ContainerSlot].Kind == ELocalKind::MapI32I32)
				{
					MapValues = static_cast<FMap::FIntI32Entries*>(
					    Locals[D.ContainerSlot].Storage);
				}
				else
				{
					return Result;
				}
				while (CounterValue < LimitValue)
				{
					if (!AdvanceCursor(6u, 8u, 10u) || !FastModuloAt(CursorValue, SecondaryIndexModulo, *IndexLocal, 14u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					std::int32_t* Element = nullptr;
					if (ArrayValues)
					{
						if (*IndexLocal < 0 || static_cast<std::size_t>(*IndexLocal) >= ArrayValues->size())
						{
							return FailFused(20u, "array index out of bounds");
						}
						Element = &(*ArrayValues)[static_cast<std::size_t>(*IndexLocal)];
					}
					else
					{
						Element = MapValues->FindMutable(*IndexLocal);
						if (!Element)
						{
							return FailFused(20u, "map key not found");
						}
					}
					std::int32_t Delta = 0;
					std::int32_t ElementSum = 0;
					if (!FastModuloAt(CursorValue, DeltaModulo, Delta, 23u) || !CheckedAddAt(*Element, Delta, ElementSum, 24u) || !CheckedAddAt(ElementSum, D.DeltaAddend, ElementSum, 26u) || !FastModuloAt(ElementSum, ElementModulo, *StoredLocal, 28u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					*Element = *StoredLocal;
					std::int32_t TotalSum = 0;
					if (!CheckedAddAt(TotalValue, *StoredLocal, TotalSum, 39u) || !FastModuloAt(TotalSum, TotalModulo, TotalValue, 41u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					std::int32_t NextCounter = 0;
					if (!CheckedAddAt(CounterValue, 1, NextCounter, 43u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					CounterValue = NextCounter;
					++Completed;
				}
				if (ArrayValues)
				{
					Vm.RecordJitArrayFastPathGet(Completed * 2u);
					Vm.RecordJitArrayFastPathSet(Completed);
				}
				else
				{
					Vm.RecordJitScalarMapFastPathGet(Completed * 2u);
					Vm.RecordJitScalarMapFastPathSet(Completed);
				}
			}
			else if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::DirectAffineIndexedUpdate)
			{
				// VM local/element writes are int32_t too. Snapshot the immutable
				// descriptor so they cannot alias its loop-invariant scalar fields.
				const auto X = Plan.DirectIndexed;
				std::int32_t* const DeltaLocal = X.bDeltaConstant
				    ? nullptr
				    : I32(X.DeltaSlot);
				std::int32_t* const StoredLocal = I32(X.StoredValueSlot);
				if ((!X.bDeltaConstant && !DeltaLocal) || !StoredLocal)
				{
					return Result;
				}
				std::vector<std::int32_t>* ArrayValues = nullptr;
				FMap::FIntI32Entries* MapValues = nullptr;
				if (Locals[X.ContainerSlot].Kind == ELocalKind::ArrayI32)
				{
					ArrayValues = static_cast<std::vector<std::int32_t>*>(
					    Locals[X.ContainerSlot].Storage);
				}
				else if (Locals[X.ContainerSlot].Kind == ELocalKind::MapI32I32)
				{
					MapValues = static_cast<FMap::FIntI32Entries*>(
					    Locals[X.ContainerSlot].Storage);
				}
				else
				{
					return Result;
				}

				const FInvariantModulo StateModulo = PrepareModulo(X.StateModulus);
				const FInvariantModulo DirectDeltaModulo = PrepareModulo(X.DeltaModulus);
				const FInvariantModulo DirectTotalModulo = PrepareModulo(X.TotalModulus);
				while (CounterValue < LimitValue)
				{
					std::int32_t Product = 0;
					std::int32_t Sum = 0;
					std::int32_t DeltaValue = X.DeltaAddend;
					if (!CheckedMulAt(CursorValue, X.StateMultiplier, Product,
					        X.StateMulInstruction) ||
					    !CheckedAddAt(Product, X.StateIncrement, Sum,
					        X.StateAddInstruction) ||
					    !FastModuloAt(Sum, StateModulo, CursorValue,
					        X.StateModInstruction))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					if (!X.bDeltaConstant)
					{
						if (!FastModuloAt(CursorValue, DirectDeltaModulo, DeltaValue,
						        X.DeltaModInstruction) ||
						    !CheckedAddAt(DeltaValue, X.DeltaAddend, DeltaValue,
						        X.DeltaAddInstruction))
						{
							return {ETypedLoopExecution::Failed, HeaderOffset,
							    FusedFailureOffset};
						}
						*DeltaLocal = DeltaValue;
					}

					std::int32_t* Element = nullptr;
					if (ArrayValues)
					{
						if (CursorValue < 0 || static_cast<std::size_t>(CursorValue) >= ArrayValues->size())
						{
							return FailFused(X.LookupInstruction,
							    "array index out of bounds");
						}
						Element = &(*ArrayValues)[static_cast<std::size_t>(CursorValue)];
					}
					else
					{
						Element = MapValues->FindMutable(CursorValue);
						if (!Element)
						{
							return FailFused(X.LookupInstruction, "map key not found");
						}
					}
					if (!CheckedAddAt(*Element, DeltaValue, *StoredLocal,
					        X.ElementAddInstruction))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					*Element = *StoredLocal;
					if (!CheckedAddAt(TotalValue, *StoredLocal, Sum,
					        X.TotalAddInstruction) ||
					    !FastModuloAt(Sum, DirectTotalModulo, TotalValue,
					        X.TotalModInstruction))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					if (!CheckedAddAt(CounterValue, 1, Sum, X.CounterInstruction))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					CounterValue = Sum;
					++Completed;
				}
				if (ArrayValues)
				{
					Vm.RecordJitArrayFastPathGet(Completed * 2u);
					Vm.RecordJitArrayFastPathSet(Completed);
				}
				else
				{
					Vm.RecordJitScalarMapFastPathGet(Completed * 2u);
					Vm.RecordJitScalarMapFastPathSet(Completed);
				}
			}
			else if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::DirectIndexedSetContains || Plan.FusedKind == FTypedLoopPlan::EFusedKind::DirectAffineSetContains)
			{
				auto X = Plan.DirectSet;
				if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::DirectIndexedSetContains)
				{
					X.SetSlot = D.ContainerSlot;
					X.StateMultiplier = D.CursorMultiplier;
					X.StateIncrement = D.CursorIncrement;
					X.StateModulus = D.CursorModulus;
					X.StateMulInstruction = 6u;
					X.StateAddInstruction = 8u;
					X.StateModInstruction = 10u;
					X.TotalIncrementInstruction = 18u;
					X.CounterInstruction = 19u;
					X.bModuloKey = true;
					X.KeyModulus = D.SecondaryIndexModulus;
					X.KeyModInstruction = 15u;
				}
				if (Locals[X.SetSlot].Kind != ELocalKind::SetI32)
				{
					return Result;
				}
				// This fused shape has no mutation, allocation or callback. The
				// rooted set outlives this local borrow; only scalar locals change.
				const auto Values = static_cast<const FSet::FIntScalarElements*>(
				    Locals[X.SetSlot].Storage)
				                        ->BorrowReadOnlyProbe();
				const auto Kernel = ExecuteAffineSetKernel(
				    X, Values, CounterValue, LimitValue, CursorValue, TotalValue);
				#if VEROSCRIPT_ENABLE_JIT_TEST_HOOKS
				if (AffineSetObserver && Plan.FusedKind == FTypedLoopPlan::EFusedKind::DirectAffineSetContains)
				{
					AffineSetObserver(Vm, Plan, Kernel);
				}
				#endif
				CounterValue = Kernel.Counter;
				CursorValue = Kernel.State;
				TotalValue = Kernel.Total;
				if (Kernel.Error)
				{
					return FailFused(Kernel.FailureInstruction, Kernel.Error);
				}
			}
			else if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::StateIndirectIndexedUpdate)
			{
				const auto InstructionIndex = [&](const std::size_t Canonical)
				{
					return Canonical - (!D.bStateUpdateHasNop && Canonical > 11u ? 1u : 0u);
				};
				std::int32_t* const PositionLocal = I32(
				    Plan.Instructions[InstructionIndex(22u)].Operand0);
				std::int32_t* const IndexLocal = I32(D.IndexOrKeySlot);
				std::int32_t* const DeltaLocal = I32(D.DeltaOrValueSlot);
				std::int32_t* const StoredLocal = I32(D.StoredValueSlot);
				if (!PositionLocal || !IndexLocal || !DeltaLocal || !StoredLocal)
				{
					return Result;
				}
				std::vector<std::int32_t>* ArrayValues = nullptr;
				FMap::FIntI32Entries* MapValues = nullptr;
				if (Locals[D.ContainerSlot].Kind == ELocalKind::ArrayI32)
				{
					ArrayValues = static_cast<std::vector<std::int32_t>*>(
					    Locals[D.ContainerSlot].Storage);
				}
				else if (Locals[D.ContainerSlot].Kind == ELocalKind::MapI32I32)
				{
					MapValues = static_cast<FMap::FIntI32Entries*>(
					    Locals[D.ContainerSlot].Storage);
				}
				else
				{
					return Result;
				}

				// A common indirect-array loop has a closed positive range after each
				// modulus. Validate that range once, including every indirect index and
				// current element, then run a compact unchecked kernel. Any unusual signed,
				// overflowing, sparse, or map case remains on the diagnostic-exact path.
				bool bCanUseRangeProvenKernel = ArrayValues && CounterValue >= 0 && LimitValue > CounterValue && CursorValue >= 0 && TotalValue >= 0 && D.CursorMultiplier >= 0 && D.CursorIncrement >= 0 && D.CursorModulus > 0 && CursorValue < D.CursorModulus && D.SecondaryIndexModulus > 0 && D.PositionMultiplier >= 0 && D.PositionModulus > 0 && D.DeltaDivisor > 0 && D.DeltaModulus > 0 && D.DeltaAddend >= 0 && D.ElementModulus > 0 && D.TotalModulus > 0 && TotalValue < D.TotalModulus && static_cast<std::size_t>(D.PositionModulus) <= Order->size();
				if (bCanUseRangeProvenKernel)
				{
					const std::uint64_t Iterations = static_cast<std::uint64_t>(
					    static_cast<std::int64_t>(LimitValue) - CounterValue);
					constexpr std::uint64_t MaxProofItems = 1u << 20u;
					const std::uint64_t ArrayItems = static_cast<std::uint64_t>(
					    ArrayValues->size());
					const std::uint64_t ProofItems =
					    static_cast<std::uint64_t>(D.PositionModulus) + ArrayItems;
					// The proof scans both containers. Require enough remaining work to
					// amortize that scan; otherwise the exact fused loop is faster for short
					// invocations. The absolute cap also prevents input-sized proof latency.
					bCanUseRangeProvenKernel = ArrayItems <= MaxProofItems && ProofItems <= MaxProofItems && ProofItems <= Iterations * 2u;
				}
				if (bCanUseRangeProvenKernel)
				{
					const std::int64_t MaxStateExpression =
					    static_cast<std::int64_t>(D.CursorModulus - 1) * D.CursorMultiplier + D.CursorIncrement;
					const std::int64_t MaxPositionExpression =
					    static_cast<std::int64_t>(D.CursorModulus - 1) + static_cast<std::int64_t>(D.SecondaryIndexModulus - 1) * D.PositionMultiplier;
					const std::int64_t MaxDelta =
					    (static_cast<std::int64_t>(D.CursorModulus - 1) / D.DeltaDivisor) % D.DeltaModulus + D.DeltaAddend;
					const std::int64_t MaxTotalExpression =
					    static_cast<std::int64_t>(D.TotalModulus - 1) + (D.ElementModulus - 1) + (D.PositionModulus - 1);
					bCanUseRangeProvenKernel =
					    MaxStateExpression <= std::numeric_limits<std::int32_t>::max() && MaxPositionExpression <= std::numeric_limits<std::int32_t>::max() && MaxDelta <= std::numeric_limits<std::int32_t>::max() && static_cast<std::int64_t>(D.ElementModulus - 1) + MaxDelta <= std::numeric_limits<std::int32_t>::max() && MaxTotalExpression <= std::numeric_limits<std::int32_t>::max();
				}
				if (bCanUseRangeProvenKernel)
				{
					for (std::int32_t Position = 0;
					    Position < D.PositionModulus; ++Position)
					{
						const std::int32_t Index = (*Order)[static_cast<std::size_t>(Position)];
						if (Index < 0 || static_cast<std::size_t>(Index) >= ArrayValues->size())
						{
							bCanUseRangeProvenKernel = false;
							break;
						}
					}
				}
				if (bCanUseRangeProvenKernel)
				{
					for (const std::int32_t Value : *ArrayValues)
					{
						if (Value < 0 || Value >= D.ElementModulus)
						{
							bCanUseRangeProvenKernel = false;
							break;
						}
					}
				}
				if (bCanUseRangeProvenKernel)
				{
					const FFastStateIndirectResult Fast = ExecuteFastStateIndirectKernel(
					    D, Order->data(), ArrayValues->data(), CounterValue, LimitValue,
					    CursorValue, TotalValue);
					CounterValue = Fast.Counter;
					CursorValue = Fast.State;
					TotalValue = Fast.Total;
					Completed = Fast.Completed;
					if (Completed != 0u)
					{
						*PositionLocal = Fast.Position;
						*IndexLocal = Fast.Index;
						*DeltaLocal = Fast.Delta;
						*StoredLocal = Fast.Stored;
					}
				}
				else
				{
					while (CounterValue < LimitValue)
					{
						if (!AdvanceCursor(InstructionIndex(6u), InstructionIndex(8u),
						        InstructionIndex(10u)))
						{
							return {ETypedLoopExecution::Failed, HeaderOffset,
							    FusedFailureOffset};
						}
						std::int32_t CounterIndex = 0;
						std::int32_t PositionTerm = 0;
						std::int32_t PositionSum = 0;
						if (!FastModuloAt(CounterValue, SecondaryIndexModulo, CounterIndex,
						        InstructionIndex(16u)) ||
						    !CheckedMulAt(CounterIndex, D.PositionMultiplier,
						        PositionTerm, InstructionIndex(18u)) ||
						    !CheckedAddAt(CursorValue, PositionTerm, PositionSum,
						        InstructionIndex(19u)) ||
						    !FastModuloAt(PositionSum, PositionModulo,
						        *PositionLocal, InstructionIndex(21u)) ||
						    !OrderAt(*PositionLocal, *IndexLocal, InstructionIndex(25u)))
						{
							return {ETypedLoopExecution::Failed, HeaderOffset,
							    FusedFailureOffset};
						}
						std::int32_t DeltaBase = 0;
						if (!FastDivideAt(CursorValue, DeltaDivisor, DeltaBase,
						        InstructionIndex(29u)) ||
						    !FastModuloAt(DeltaBase, DeltaModulo, DeltaBase,
						        InstructionIndex(31u)) ||
						    !CheckedAddAt(DeltaBase, D.DeltaAddend, *DeltaLocal,
						        InstructionIndex(33u)))
						{
							return {ETypedLoopExecution::Failed, HeaderOffset,
							    FusedFailureOffset};
						}

						std::int32_t* Element = nullptr;
						if (ArrayValues)
						{
							if (*IndexLocal < 0 || static_cast<std::size_t>(*IndexLocal) >= ArrayValues->size())
							{
								return FailFused(
								    InstructionIndex(39u), "array index out of bounds");
							}
							Element = &(*ArrayValues)[static_cast<std::size_t>(*IndexLocal)];
						}
						else
						{
							Element = MapValues->FindMutable(*IndexLocal);
							if (!Element)
							{
								return FailFused(
								    InstructionIndex(39u), "map key not found");
							}
						}
						std::int32_t Updated = 0;
						if (!CheckedAddAt(*Element, *DeltaLocal, Updated,
						        InstructionIndex(41u)) ||
						    !FastModuloAt(Updated, ElementModulo, *StoredLocal,
						        InstructionIndex(43u)))
						{
							return {ETypedLoopExecution::Failed, HeaderOffset,
							    FusedFailureOffset};
						}
						*Element = *StoredLocal;
						std::int32_t TotalSum = 0;
						std::int32_t PositionContribution = *PositionLocal & D.PositionMask;
						if (!CheckedAddAt(TotalValue, *StoredLocal, TotalSum,
						        InstructionIndex(54u)) ||
						    !CheckedAddAt(TotalSum, PositionContribution, TotalSum,
						        InstructionIndex(58u)) ||
						    !FastModuloAt(TotalSum, TotalModulo, TotalValue,
						        InstructionIndex(60u)))
						{
							return {ETypedLoopExecution::Failed, HeaderOffset,
							    FusedFailureOffset};
						}
						std::int32_t NextCounter = 0;
						if (!CheckedAddAt(CounterValue, 1, NextCounter, InstructionIndex(62u)))
						{
							return {ETypedLoopExecution::Failed, HeaderOffset,
							    FusedFailureOffset};
						}
						CounterValue = NextCounter;
						++Completed;
					}
				}
				Vm.RecordJitArrayFastPathGet(Completed);
				if (ArrayValues)
				{
					Vm.RecordJitArrayFastPathGet(Completed * 2u);
					Vm.RecordJitArrayFastPathSet(Completed);
				}
				else
				{
					Vm.RecordJitScalarMapFastPathGet(Completed * 2u);
					Vm.RecordJitScalarMapFastPathSet(Completed);
				}
			}
			else if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::StateIndirectSetContains)
			{
				std::int32_t* const PositionLocal = I32(Plan.Instructions[22].Operand0);
				std::int32_t* const KeyLocal = I32(D.IndexOrKeySlot);
				if (!PositionLocal || !KeyLocal || Locals[D.ContainerSlot].Kind != ELocalKind::SetI32)
				{
					return Result;
				}
				auto& Values = *static_cast<FSet::FIntScalarElements*>(
				    Locals[D.ContainerSlot].Storage);
				while (CounterValue < LimitValue)
				{
					if (!AdvanceCursor(6u, 8u, 10u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					std::int32_t CounterIndex = 0;
					std::int32_t PositionTerm = 0;
					std::int32_t PositionSum = 0;
					if (!FastModuloAt(CounterValue, SecondaryIndexModulo, CounterIndex, 16u) || !CheckedMulAt(CounterIndex, D.PositionMultiplier, PositionTerm, 18u) || !CheckedAddAt(CursorValue, PositionTerm, PositionSum, 19u) || !FastModuloAt(PositionSum, PositionModulo, *PositionLocal, 21u) || !OrderAt(*PositionLocal, *KeyLocal, 25u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					if ((CursorValue & D.PositionMask) != 0)
					{
						std::int32_t Adjusted = 0;
						if (!CheckedAddAt(*KeyLocal, 1, Adjusted, 33u))
						{
							return {ETypedLoopExecution::Failed, HeaderOffset,
							    FusedFailureOffset};
						}
						*KeyLocal = Adjusted;
					}
					const bool bContains = Values.Contains(*KeyLocal);
					const std::int32_t Increment = bContains
					    ? D.TrueIncrement
					    : D.FalseIncrement;
					std::int32_t UpdatedTotal = 0;
					const std::size_t AddIndex = bContains ? 40u : 47u;
					const std::size_t ModIndex = bContains ? 42u : 49u;
					if (!CheckedAddAt(TotalValue, Increment, UpdatedTotal, AddIndex) || !FastModuloAt(UpdatedTotal, TotalModulo, TotalValue, ModIndex))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					std::int32_t NextCounter = 0;
					if (!CheckedAddAt(CounterValue, 1, NextCounter, 51u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					CounterValue = NextCounter;
					++Completed;
				}
				Vm.RecordJitArrayFastPathGet(Completed);
			}
			else if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::IndirectArrayUpdate)
			{
				if (Locals[D.ContainerSlot].Kind != ELocalKind::ArrayI32)
				{
					return Result;
				}
				std::int32_t* const IndexLocal = I32(D.IndexOrKeySlot);
				std::int32_t* const DeltaLocal = I32(D.DeltaOrValueSlot);
				std::int32_t* const StoredLocal = I32(D.StoredValueSlot);
				if (!IndexLocal || !DeltaLocal || !StoredLocal)
				{
					return Result;
				}
				auto& Values = *static_cast<std::vector<std::int32_t>*>(
				    Locals[D.ContainerSlot].Storage);
				while (CounterValue < LimitValue)
				{
					if (!AdvanceCursor(6u, 8u, 10u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					if (!OrderAt(CursorValue, *IndexLocal, 14u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					std::int32_t Secondary = 0;
					std::int32_t SecondaryIndex = 0;
					std::int32_t DeltaBase = 0;
					if (!CheckedAddAt(CursorValue, D.SecondaryOffset, Secondary, 19u) || !FastModuloAt(Secondary, SecondaryIndexModulo, SecondaryIndex, 21u) || !OrderAt(SecondaryIndex, DeltaBase, 22u) || !FastModuloAt(DeltaBase, DeltaModulo, DeltaBase, 24u) || !CheckedAddAt(DeltaBase, D.DeltaAddend, *DeltaLocal, 26u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					if (*IndexLocal < 0 || static_cast<std::size_t>(*IndexLocal) >= Values.size())
					{
						return FailFused(34u, "array index out of bounds");
					}
					std::int32_t& Element = Values[static_cast<std::size_t>(*IndexLocal)];
					if (!CheckedAddAt(Element, *DeltaLocal, *StoredLocal, 36u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					Element = *StoredLocal;
					std::int32_t TotalSum = 0;
					if (!CheckedAddAt(TotalValue, Element, TotalSum, 49u) || !FastModuloAt(TotalSum, TotalModulo, TotalValue, 51u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					std::int32_t NextCounter = 0;
					if (!CheckedAddAt(CounterValue, 1, NextCounter, 53u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					CounterValue = NextCounter;
					++Completed;
				}
				Vm.RecordJitArrayFastPathGet(Completed * 4u);
				Vm.RecordJitArrayFastPathSet(Completed);
			}
			else if (Plan.FusedKind == FTypedLoopPlan::EFusedKind::IndirectMapUpdate)
			{
				if (Locals[D.ContainerSlot].Kind != ELocalKind::MapI32I32)
				{
					return Result;
				}
				std::int32_t* const KeyLocal = I32(D.IndexOrKeySlot);
				std::int32_t* const ValueLocal = I32(D.DeltaOrValueSlot);
				if (!KeyLocal || !ValueLocal)
				{
					return Result;
				}
				auto& Values = *static_cast<FMap::FIntI32Entries*>(
				    Locals[D.ContainerSlot].Storage);
				while (CounterValue < LimitValue)
				{
					if (!AdvanceCursor(6u, 8u, 10u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					if (!OrderAt(CursorValue, *KeyLocal, 14u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					std::int32_t* const Element = Values.FindMutable(*KeyLocal);
					if (!Element)
					{
						return FailFused(18u, "map key not found");
					}
					*ValueLocal = *Element;
					std::int32_t Secondary = 0;
					std::int32_t SecondaryIndex = 0;
					std::int32_t Delta = 0;
					if (!CheckedAddAt(CursorValue, D.SecondaryOffset, Secondary, 24u) || !FastModuloAt(Secondary, SecondaryIndexModulo, SecondaryIndex, 26u) || !OrderAt(SecondaryIndex, Delta, 27u) || !FastModuloAt(Delta, DeltaModulo, Delta, 29u) || !CheckedAddAt(Delta, D.DeltaAddend, Delta, 31u) || !CheckedAddAt(*ValueLocal, Delta, *ValueLocal, 32u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					*Element = *ValueLocal;
					std::int32_t TotalSum = 0;
					if (!CheckedAddAt(TotalValue, *ValueLocal, TotalSum, 45u) || !FastModuloAt(TotalSum, TotalModulo, TotalValue, 47u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					std::int32_t NextCounter = 0;
					if (!CheckedAddAt(CounterValue, 1, NextCounter, 49u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					CounterValue = NextCounter;
					++Completed;
				}
				Vm.RecordJitArrayFastPathGet(Completed * 2u);
				Vm.RecordJitScalarMapFastPathGet(Completed);
				Vm.RecordJitScalarMapFastPathSet(Completed);
			}
			else
			{
				if (Locals[D.ContainerSlot].Kind != ELocalKind::SetI32)
				{
					return Result;
				}
				auto& Values = *static_cast<FSet::FIntScalarElements*>(
				    Locals[D.ContainerSlot].Storage);
				while (CounterValue < LimitValue)
				{
					if (!AdvanceCursor(6u, 8u, 10u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					std::int32_t Key = 0;
					if (!OrderAt(CursorValue, Key, 15u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					if (Values.Contains(Key))
					{
						std::int32_t Updated = 0;
						if (!CheckedAddAt(TotalValue, 1, Updated, 18u))
						{
							return {ETypedLoopExecution::Failed, HeaderOffset,
							    FusedFailureOffset};
						}
						TotalValue = Updated;
					}
					std::int32_t NextCounter = 0;
					if (!CheckedAddAt(CounterValue, 1, NextCounter, 19u))
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    FusedFailureOffset};
					}
					CounterValue = NextCounter;
					++Completed;
				}
			}
			CommitLoopState();
			return {ETypedLoopExecution::Completed, Plan.ExitOffset,
			    Plan.HeaderOffset};
		}

		std::array<FTypedValue, MaxTypedLoopStack> InlineStack{};
		std::vector<FTypedValue> ExtendedStack;
		FTypedValue* Stack = InlineStack.data();
		std::size_t StackCapacity = InlineStack.size();
		if (Plan.MaxStackDepth > InlineStack.size())
		{
			ExtendedStack.resize(Plan.MaxStackDepth);
			Stack = ExtendedStack.data();
			StackCapacity = ExtendedStack.size();
		}
		std::size_t StackSize = 0;
		const auto Push = [&](const FTypedValue& Value) -> bool
		{
			if (StackSize >= StackCapacity)
			{
				return false;
			}
			Stack[StackSize++] = Value;
			return true;
		};
		const auto Pop = [&](FTypedValue& Out) -> bool
		{
			if (StackSize == 0u)
			{
				return false;
			}
			Out = Stack[--StackSize];
			return true;
		};
		const auto Fail = [&](const FTypedLoopInstruction& Instruction,
		                      const char* Error) -> FTypedLoopExecutionResult
		{
			Vm.JitRefineRuntimeErrorLocation(Instruction.BytecodeOffset);
			Vm.JitReportRuntimeError(Error);
			return {ETypedLoopExecution::Failed, HeaderOffset,
			    Instruction.BytecodeOffset};
		};
		bool bStateMutated = false;
		const auto NotHandled = [&]() -> FTypedLoopExecutionResult
		{
			if (bStateMutated)
			{
				Vm.JitRefineRuntimeErrorLocation(Result.ErrorBytecodeOffset);
				Vm.JitReportRuntimeError("JIT typed loop: runtime plan mismatch");
				return {ETypedLoopExecution::Failed, HeaderOffset,
				    Result.ErrorBytecodeOffset};
			}
			return {ETypedLoopExecution::NotHandled, HeaderOffset, HeaderOffset};
		};

		// Verified plans can execute many bytecode operations per source iteration.
		// Charge the runaway guard only on CFG backedges instead of every decoded
		// instruction; this preserves finite-loop diagnostics without adding a branch
		// to every scalar/container operation in the hot interpreter dispatch.
		std::uint64_t BackEdgeBudget = std::uint64_t{1} << 40u;
		std::uint32_t Pc = 0;
		std::size_t ArrayGets = 0;
		std::size_t ArraySets = 0;
		std::size_t ArrayPushes = 0;
		std::size_t MapGets = 0;
		std::size_t MapSets = 0;
		while (Pc < Plan.Instructions.size())
		{
			const FTypedLoopInstruction& Instruction = Plan.Instructions[Pc];
			Result.ErrorBytecodeOffset = Instruction.BytecodeOffset;
			const auto Continue = [&]()
			{
				Pc = Instruction.NextInstruction;
			};
			switch (Instruction.Opcode)
			{
			case EOpcode::Nop:
				{
					Continue();
					break;
				}
			case EOpcode::ConstI32:
				{
					if (Instruction.bImmediateU32)
					{
						FTypedValue Value;
						Value.Kind = EStackKind::U32;
						Value.U32 = static_cast<std::uint32_t>(Instruction.Immediate);
						if (!Push(Value))
						{
							return NotHandled();
						}
					}
					else if (!Push({EStackKind::I32, Instruction.Immediate, nullptr, 0xFFFFu}))
					{
						return NotHandled();
					}
					Continue();
					break;
				}
			case EOpcode::ConstF64:
				{
					FTypedValue Value;
					if (Instruction.bImmediateF32)
					{
						Value.Kind = EStackKind::F32;
						Value.F32 = static_cast<float>(Instruction.ImmediateF64);
					}
					else
					{
						Value.Kind = EStackKind::F64;
						Value.F64 = Instruction.ImmediateF64;
					}
					if (!Push(Value))
					{
						return NotHandled();
					}
					Continue();
					break;
				}
			case EOpcode::ConstBool:
				{
					if (!Push({EStackKind::Bool, Instruction.Immediate, nullptr, 0xFFFFu}))
					{
						return NotHandled();
					}
					Continue();
					break;
				}
			case EOpcode::LoadLocal:
				{
					if (!Bind(Instruction.Operand0))
					{
						return NotHandled();
					}
					const FLocalBinding& Local = Locals[Instruction.Operand0];
					FTypedValue Value;
					Value.SourceSlot = Instruction.Operand0;
					Value.Storage = Local.Storage;
					switch (Local.Kind)
					{
					case ELocalKind::I32:
						{
							Value.Kind = EStackKind::I32;
							Value.I32 = *static_cast<std::int32_t*>(Local.Storage);
							break;
						}
					case ELocalKind::U32:
						{
							Value.Kind = EStackKind::U32;
							Value.U32 = *static_cast<std::uint32_t*>(Local.Storage);
							break;
						}
					case ELocalKind::F32:
						{
							Value.Kind = EStackKind::F32;
							Value.F32 = *static_cast<float*>(Local.Storage);
							break;
						}
					case ELocalKind::F64:
						{
							Value.Kind = EStackKind::F64;
							Value.F64 = *static_cast<double*>(Local.Storage);
							break;
						}
					case ELocalKind::Bool:
						{
							Value.Kind = EStackKind::Bool;
							Value.I32 = *static_cast<bool*>(Local.Storage) ? 1 : 0;
							break;
						}
					case ELocalKind::String:
						{
							Value.Kind = EStackKind::String;
							break;
						}
					case ELocalKind::WString:
						{
							Value.Kind = EStackKind::WString;
							break;
						}
					case ELocalKind::ArrayI32:
						{
							Value.Kind = EStackKind::ArrayI32;
							break;
						}
					case ELocalKind::ArrayU32:
						{
							Value.Kind = EStackKind::ArrayU32;
							break;
						}
					case ELocalKind::ArrayF32:
						{
							Value.Kind = EStackKind::ArrayF32;
							break;
						}
					case ELocalKind::ArrayF64:
						{
							Value.Kind = EStackKind::ArrayF64;
							break;
						}
					case ELocalKind::ArrayString:
						{
							Value.Kind = EStackKind::ArrayString;
							break;
						}
					case ELocalKind::ArrayWString:
						{
							Value.Kind = EStackKind::ArrayWString;
							break;
						}
					case ELocalKind::MapI32I32:
						{
							Value.Kind = EStackKind::MapI32I32;
							break;
						}
					case ELocalKind::MapI32U32:
						{
							Value.Kind = EStackKind::MapI32U32;
							break;
						}
					case ELocalKind::MapI32F32:
						{
							Value.Kind = EStackKind::MapI32F32;
							break;
						}
					case ELocalKind::MapI32F64:
						{
							Value.Kind = EStackKind::MapI32F64;
							break;
						}
					case ELocalKind::MapU32I32:
						{
							Value.Kind = EStackKind::MapU32I32;
							break;
						}
					case ELocalKind::MapU32U32:
						{
							Value.Kind = EStackKind::MapU32U32;
							break;
						}
					case ELocalKind::MapU32F32:
						{
							Value.Kind = EStackKind::MapU32F32;
							break;
						}
					case ELocalKind::MapU32F64:
						{
							Value.Kind = EStackKind::MapU32F64;
							break;
						}
					case ELocalKind::MapStringI32:
						{
							Value.Kind = EStackKind::MapStringI32;
							break;
						}
					case ELocalKind::MapWStringI32:
						{
							Value.Kind = EStackKind::MapWStringI32;
							break;
						}
					case ELocalKind::SetI32:
						{
							Value.Kind = EStackKind::SetI32;
							break;
						}
					case ELocalKind::SetU32:
						{
							Value.Kind = EStackKind::SetU32;
							break;
						}
					case ELocalKind::SetString:
						{
							Value.Kind = EStackKind::SetString;
							break;
						}
					case ELocalKind::SetWString:
						{
							Value.Kind = EStackKind::SetWString;
							break;
						}
					case ELocalKind::Object:
						{
							Value.Kind = EStackKind::Object;
							break;
						}
					default:
						return NotHandled();
					}
					if (!Push(Value))
					{
						return NotHandled();
					}
					Continue();
					break;
				}
			case EOpcode::StoreLocal:
				{
					FTypedValue Value;
					if (!Pop(Value))
					{
						return NotHandled();
					}
					FLocalBinding& Local = Locals[Instruction.Operand0];
					if (Value.Kind == EStackKind::I32)
					{
						if (Local.Kind == ELocalKind::Unsupported)
						{
							if (!Vm.JitStoreLocal(Instruction.Operand0, FValue(Value.I32)) || !Bind(Instruction.Operand0))
							{
								return Fail(Instruction, "JIT typed loop: scalar assignment failed");
							}
						}
						if (Local.Kind != ELocalKind::I32)
						{
							return NotHandled();
						}
						*static_cast<std::int32_t*>(Local.Storage) = Value.I32;
						bStateMutated = true;
					}
					else if (Value.Kind == EStackKind::U32)
					{
						if (Local.Kind == ELocalKind::Unsupported)
						{
							if (!Vm.JitStoreLocal(Instruction.Operand0, FValue(Value.U32)) || !Bind(Instruction.Operand0))
							{
								return Fail(Instruction,
								    "JIT typed loop: scalar assignment failed");
							}
						}
						if (Local.Kind != ELocalKind::U32)
						{
							return NotHandled();
						}
						*static_cast<std::uint32_t*>(Local.Storage) = Value.U32;
						bStateMutated = true;
					}
					else if (Value.Kind == EStackKind::F32)
					{
						if (Local.Kind == ELocalKind::Unsupported)
						{
							if (!Vm.JitStoreLocal(Instruction.Operand0, FValue(Value.F32)) || !Bind(Instruction.Operand0))
							{
								return Fail(Instruction,
								    "JIT typed loop: scalar assignment failed");
							}
						}
						if (Local.Kind != ELocalKind::F32)
						{
							return NotHandled();
						}
						*static_cast<float*>(Local.Storage) = Value.F32;
						bStateMutated = true;
					}
					else if (Value.Kind == EStackKind::F64)
					{
						if (Local.Kind == ELocalKind::Unsupported)
						{
							if (!Vm.JitStoreLocal(Instruction.Operand0, FValue(Value.F64)) || !Bind(Instruction.Operand0))
							{
								return Fail(Instruction, "JIT typed loop: scalar assignment failed");
							}
						}
						if (Local.Kind != ELocalKind::F64)
						{
							return NotHandled();
						}
						*static_cast<double*>(Local.Storage) = Value.F64;
						bStateMutated = true;
					}
					else if (Value.Kind == EStackKind::Bool)
					{
						if (Local.Kind == ELocalKind::Unsupported)
						{
							if (!Vm.JitStoreLocal(
							        Instruction.Operand0, FValue(Value.I32 != 0)) ||
							    !Bind(Instruction.Operand0))
							{
								return Fail(Instruction, "JIT typed loop: scalar assignment failed");
							}
						}
						if (Local.Kind != ELocalKind::Bool)
						{
							return NotHandled();
						}
						*static_cast<bool*>(Local.Storage) = Value.I32 != 0;
						bStateMutated = true;
					}
					else if (Value.Kind == EStackKind::String || Value.Kind == EStackKind::WString)
					{
						if (!Value.Storage)
						{
							return NotHandled();
						}
						if (Local.Kind == ELocalKind::Unsupported)
						{
							const FValue Copy = Value.Kind == EStackKind::String
							    ? FValue(*static_cast<const std::string*>(Value.Storage))
							    : FValue(*static_cast<const std::u16string*>(Value.Storage));
							if (!Vm.JitStoreLocal(Instruction.Operand0, Copy) || !Bind(Instruction.Operand0))
							{
								return Fail(Instruction,
								    "JIT typed loop: string assignment failed");
							}
						}
						if (Value.Kind == EStackKind::String && Local.Kind == ELocalKind::String)
						{
							const std::string Copy =
							    *static_cast<const std::string*>(Value.Storage);
							*static_cast<std::string*>(Local.Storage) = Copy;
						}
						else if (Value.Kind == EStackKind::WString && Local.Kind == ELocalKind::WString)
						{
							const std::u16string Copy =
							    *static_cast<const std::u16string*>(Value.Storage);
							*static_cast<std::u16string*>(Local.Storage) = Copy;
						}
						else
						{
							return NotHandled();
						}
						bStateMutated = true;
					}
					else
					{
						const bool bContainer = Value.Kind == EStackKind::ArrayI32 || Value.Kind == EStackKind::ArrayU32 || Value.Kind == EStackKind::ArrayF32 || Value.Kind == EStackKind::ArrayF64 || Value.Kind == EStackKind::ArrayString || Value.Kind == EStackKind::ArrayWString || Value.Kind == EStackKind::MapI32I32 || Value.Kind == EStackKind::MapI32U32 || Value.Kind == EStackKind::MapI32F32 || Value.Kind == EStackKind::MapI32F64 || Value.Kind == EStackKind::MapU32I32 || Value.Kind == EStackKind::MapU32U32 || Value.Kind == EStackKind::MapU32F32 || Value.Kind == EStackKind::MapU32F64 || Value.Kind == EStackKind::MapStringI32 || Value.Kind == EStackKind::MapWStringI32 || Value.Kind == EStackKind::SetI32 || Value.Kind == EStackKind::SetU32 || Value.Kind == EStackKind::SetString || Value.Kind == EStackKind::SetWString;
						if (!bContainer || Value.SourceSlot >= LocalCount)
						{
							return Fail(Instruction,
							    "JIT typed loop: unsupported local assignment");
						}
						// Compound indexed assignment keeps the mutated container as the
						// expression result.  Compiler temporaries may receive that handle
						// before it is stored back to the original local. Establish the alias
						// once with the authoritative VM ownership helper; subsequent loop
						// iterations observe identical raw storage and require no refcount work.
						if (Value.Storage != Local.Storage)
						{
							FValue* const Source =
							    Vm.JitBorrowDirectLinearLocal(Value.SourceSlot);
							if (!Source || !Vm.JitStoreLocal(Instruction.Operand0, FValue(*Source)))
							{
								return Fail(Instruction,
								    "JIT typed loop: container assignment failed");
							}
							Local = Locals[Value.SourceSlot];
							bStateMutated = true;
						}
					}
					Continue();
					break;
				}
			case EOpcode::IncLocal:
			case EOpcode::AddLocalImm:
				{
					if (!Bind(Instruction.Operand0))
					{
						return NotHandled();
					}
					FLocalBinding& Local = Locals[Instruction.Operand0];
					const std::int32_t Delta = Instruction.Opcode == EOpcode::IncLocal
					    ? 1
					    : Instruction.Immediate;
					if (Local.Kind == ELocalKind::F64)
					{
						*static_cast<double*>(Local.Storage) += static_cast<double>(Delta);
					}
					else if (Local.Kind == ELocalKind::F32)
					{
						*static_cast<float*>(Local.Storage) += static_cast<float>(Delta);
					}
					else if (Local.Kind == ELocalKind::U32)
					{
						*static_cast<std::uint32_t*>(Local.Storage) +=
						    static_cast<std::uint32_t>(Delta);
					}
					else
					{
						if (Local.Kind != ELocalKind::I32)
						{
							return NotHandled();
						}
						std::int32_t& Value = *static_cast<std::int32_t*>(Local.Storage);
						std::int32_t Updated = 0;
						if (!Runtime::CheckedAdd(Value, Delta, Updated))
						{
							return Fail(Instruction, "integer overflow");
						}
						Value = Updated;
					}
					bStateMutated = true;
					Continue();
					break;
				}
			case EOpcode::AddLocalLocal:
				{
					if (!Bind(Instruction.Operand0) || !Bind(Instruction.Operand1))
					{
						return NotHandled();
					}
					FLocalBinding& Dest = Locals[Instruction.Operand0];
					const FLocalBinding& Source = Locals[Instruction.Operand1];
					if (Dest.Kind == ELocalKind::F64 && (Source.Kind == ELocalKind::F64 || Source.Kind == ELocalKind::I32 || Source.Kind == ELocalKind::U32))
					{
						*static_cast<double*>(Dest.Storage) += Source.Kind == ELocalKind::F64
						    ? *static_cast<double*>(Source.Storage)
						    : Source.Kind == ELocalKind::U32
						    ? static_cast<double>(*static_cast<std::uint32_t*>(Source.Storage))
						    : static_cast<double>(*static_cast<std::int32_t*>(Source.Storage));
					}
					else if (Dest.Kind == ELocalKind::F32 && (Source.Kind == ELocalKind::F32 || Source.Kind == ELocalKind::I32 || Source.Kind == ELocalKind::U32))
					{
						*static_cast<float*>(Dest.Storage) += Source.Kind == ELocalKind::F32
						    ? *static_cast<float*>(Source.Storage)
						    : Source.Kind == ELocalKind::U32
						    ? static_cast<float>(*static_cast<std::uint32_t*>(Source.Storage))
						    : static_cast<float>(*static_cast<std::int32_t*>(Source.Storage));
					}
					else if (Dest.Kind == ELocalKind::U32 && Source.Kind == ELocalKind::U32)
					{
						*static_cast<std::uint32_t*>(Dest.Storage) +=
						    *static_cast<std::uint32_t*>(Source.Storage);
					}
					else
					{
						if (Dest.Kind != ELocalKind::I32 || Source.Kind != ELocalKind::I32)
						{
							return NotHandled();
						}
						std::int32_t Updated = 0;
						if (!Runtime::CheckedAdd(
						        *static_cast<std::int32_t*>(Dest.Storage),
						        *static_cast<std::int32_t*>(Source.Storage), Updated))
						{
							return Fail(Instruction, "integer overflow");
						}
						*static_cast<std::int32_t*>(Dest.Storage) = Updated;
					}
					bStateMutated = true;
					Continue();
					break;
				}
			case EOpcode::Add:
			case EOpcode::Sub:
			case EOpcode::Mul:
			case EOpcode::Div:
			case EOpcode::Mod:
				{
					FTypedValue Right, Left;
					if (!Pop(Right) || !Pop(Left))
					{
						return NotHandled();
					}
					const bool bLeftNumeric = Left.Kind == EStackKind::I32 || Left.Kind == EStackKind::U32 || Left.Kind == EStackKind::F32 || Left.Kind == EStackKind::F64;
					const bool bRightNumeric = Right.Kind == EStackKind::I32 || Right.Kind == EStackKind::U32 || Right.Kind == EStackKind::F32 || Right.Kind == EStackKind::F64;
					if (!bLeftNumeric || !bRightNumeric)
					{
						return NotHandled();
					}
					if (Left.Kind == EStackKind::F64 || Right.Kind == EStackKind::F64)
					{
						if (Instruction.Opcode == EOpcode::Mod)
						{
							return NotHandled();
						}
						const double L = Left.Kind == EStackKind::F64
						    ? Left.F64
						    : Left.Kind == EStackKind::F32
						    ? static_cast<double>(Left.F32)
						    : Left.Kind == EStackKind::U32
						    ? static_cast<double>(Left.U32)
						    : static_cast<double>(Left.I32);
						const double R = Right.Kind == EStackKind::F64
						    ? Right.F64
						    : Right.Kind == EStackKind::F32
						    ? static_cast<double>(Right.F32)
						    : Right.Kind == EStackKind::U32
						    ? static_cast<double>(Right.U32)
						    : static_cast<double>(Right.I32);
						if (Instruction.Opcode == EOpcode::Div && R == 0.0)
						{
							return Fail(Instruction, "division by zero");
						}
						FTypedValue Value;
						Value.Kind = EStackKind::F64;
						if (Instruction.Opcode == EOpcode::Add)
						{
							Value.F64 = L + R;
						}
						else if (Instruction.Opcode == EOpcode::Sub)
						{
							Value.F64 = L - R;
						}
						else if (Instruction.Opcode == EOpcode::Mul)
						{
							Value.F64 = L * R;
						}
						else if (Instruction.Opcode == EOpcode::Div)
						{
							Value.F64 = L / R;
						}
						else
						{
							return NotHandled();
						}
						if (!Push(Value))
						{
							return NotHandled();
						}
					}
					else if (Left.Kind == EStackKind::F32 || Right.Kind == EStackKind::F32)
					{
						if (Instruction.Opcode == EOpcode::Mod)
						{
							return NotHandled();
						}
						const float L = Left.Kind == EStackKind::F32
						    ? Left.F32
						    : Left.Kind == EStackKind::U32
						    ? static_cast<float>(Left.U32)
						    : static_cast<float>(Left.I32);
						const float R = Right.Kind == EStackKind::F32
						    ? Right.F32
						    : Right.Kind == EStackKind::U32
						    ? static_cast<float>(Right.U32)
						    : static_cast<float>(Right.I32);
						if (Instruction.Opcode == EOpcode::Div && R == 0.0f)
						{
							return Fail(Instruction, "division by zero");
						}
						FTypedValue Value;
						Value.Kind = EStackKind::F32;
						if (Instruction.Opcode == EOpcode::Add)
						{
							Value.F32 = L + R;
						}
						else if (Instruction.Opcode == EOpcode::Sub)
						{
							Value.F32 = L - R;
						}
						else if (Instruction.Opcode == EOpcode::Mul)
						{
							Value.F32 = L * R;
						}
						else if (Instruction.Opcode == EOpcode::Div)
						{
							Value.F32 = L / R;
						}
						else
						{
							return NotHandled();
						}
						if (!Push(Value))
						{
							return NotHandled();
						}
					}
					else if (Left.Kind == EStackKind::U32 && Right.Kind == EStackKind::U32)
					{
						if ((Instruction.Opcode == EOpcode::Div || Instruction.Opcode == EOpcode::Mod) && Right.U32 == 0u)
						{
							return Fail(Instruction, "division by zero");
						}
						FTypedValue Value;
						Value.Kind = EStackKind::U32;
						if (Instruction.Opcode == EOpcode::Add)
						{
							Value.U32 = Left.U32 + Right.U32;
						}
						else if (Instruction.Opcode == EOpcode::Sub)
						{
							Value.U32 = Left.U32 - Right.U32;
						}
						else if (Instruction.Opcode == EOpcode::Mul)
						{
							Value.U32 = Left.U32 * Right.U32;
						}
						else if (Instruction.Opcode == EOpcode::Div)
						{
							Value.U32 = Left.U32 / Right.U32;
						}
						else if (Instruction.Opcode == EOpcode::Mod)
						{
							Value.U32 = Left.U32 % Right.U32;
						}
						else
						{
							return NotHandled();
						}
						if (!Push(Value))
						{
							return NotHandled();
						}
					}
					else
					{
						if (Left.Kind != EStackKind::I32 || Right.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						std::int32_t Value = 0;
						const char* Error = nullptr;
						if (!TryCheckedArithmetic(
						        Instruction.Opcode, Left.I32, Right.I32, Value, Error))
						{
							return Fail(Instruction, Error);
						}
						if (!Push({EStackKind::I32, Value, nullptr, 0xFFFFu}))
						{
							return NotHandled();
						}
					}
					Continue();
					break;
				}
			case EOpcode::BitAnd:
			case EOpcode::BitOr:
			case EOpcode::BitXor:
			case EOpcode::Shl:
			case EOpcode::Shr:
				{
					FTypedValue Right, Left;
					if (!Pop(Right) || !Pop(Left))
					{
						return NotHandled();
					}
					if (Left.Kind == EStackKind::U32 && Right.Kind == EStackKind::U32)
					{
						if ((Instruction.Opcode == EOpcode::Shl || Instruction.Opcode == EOpcode::Shr) && Right.U32 >= 32u)
						{
							return Fail(Instruction, "shift count out of range");
						}
						FTypedValue Value;
						Value.Kind = EStackKind::U32;
						if (Instruction.Opcode == EOpcode::BitAnd)
						{
							Value.U32 = Left.U32 & Right.U32;
						}
						else if (Instruction.Opcode == EOpcode::BitOr)
						{
							Value.U32 = Left.U32 | Right.U32;
						}
						else if (Instruction.Opcode == EOpcode::BitXor)
						{
							Value.U32 = Left.U32 ^ Right.U32;
						}
						else if (Instruction.Opcode == EOpcode::Shl)
						{
							Value.U32 = Left.U32 << Right.U32;
						}
						else
						{
							Value.U32 = Left.U32 >> Right.U32;
						}
						if (!Push(Value))
						{
							return NotHandled();
						}
						Continue();
						break;
					}
					if (Right.Kind != EStackKind::I32 || Left.Kind != EStackKind::I32)
					{
						return NotHandled();
					}
					std::int32_t Value = 0;
					if (Instruction.Opcode == EOpcode::BitAnd)
					{
						Value = Left.I32 & Right.I32;
					}
					else if (Instruction.Opcode == EOpcode::BitOr)
					{
						Value = Left.I32 | Right.I32;
					}
					else if (Instruction.Opcode == EOpcode::BitXor)
					{
						Value = Left.I32 ^ Right.I32;
					}
					else
					{
						if (Right.I32 < 0 || Right.I32 >= 32)
						{
							return Fail(Instruction, "shift count out of range");
						}
						if (Instruction.Opcode == EOpcode::Shl)
						{
							Value = static_cast<std::int32_t>(
							    static_cast<std::uint32_t>(Left.I32) << Right.I32);
						}
						else
						{
							Value = Left.I32 >> Right.I32;
						}
					}
					if (!Push({EStackKind::I32, Value, nullptr, 0xFFFFu}))
					{
						return NotHandled();
					}
					Continue();
					break;
				}
			case EOpcode::Eq:
			case EOpcode::Ne:
			case EOpcode::Lt:
			case EOpcode::Le:
			case EOpcode::Gt:
			case EOpcode::Ge:
				{
					FTypedValue Right, Left;
					if (!Pop(Right) || !Pop(Left))
					{
						return NotHandled();
					}
					bool Value = false;
					if ((Left.Kind == EStackKind::String && Right.Kind == EStackKind::String) || (Left.Kind == EStackKind::WString && Right.Kind == EStackKind::WString))
					{
						if (Instruction.Opcode != EOpcode::Eq && Instruction.Opcode != EOpcode::Ne)
						{
							return NotHandled();
						}
						const bool bEqual = Left.Kind == EStackKind::String
						    ? *static_cast<const std::string*>(Left.Storage) == *static_cast<const std::string*>(Right.Storage)
						    : *static_cast<const std::u16string*>(Left.Storage) == *static_cast<const std::u16string*>(Right.Storage);
						Value = Instruction.Opcode == EOpcode::Eq ? bEqual : !bEqual;
					}
					else
					{
						const bool bLeftNumeric = Left.Kind == EStackKind::I32 || Left.Kind == EStackKind::U32 || Left.Kind == EStackKind::F32 || Left.Kind == EStackKind::F64;
						const bool bRightNumeric = Right.Kind == EStackKind::I32 || Right.Kind == EStackKind::U32 || Right.Kind == EStackKind::F32 || Right.Kind == EStackKind::F64;
						if (!bLeftNumeric || !bRightNumeric)
						{
							return NotHandled();
						}
						if (Left.Kind == EStackKind::F64 || Right.Kind == EStackKind::F64)
						{
							const double L = Left.Kind == EStackKind::F64
							    ? Left.F64
							    : Left.Kind == EStackKind::F32
							    ? static_cast<double>(Left.F32)
							    : Left.Kind == EStackKind::U32
							    ? static_cast<double>(Left.U32)
							    : static_cast<double>(Left.I32);
							const double R = Right.Kind == EStackKind::F64
							    ? Right.F64
							    : Right.Kind == EStackKind::F32
							    ? static_cast<double>(Right.F32)
							    : Right.Kind == EStackKind::U32
							    ? static_cast<double>(Right.U32)
							    : static_cast<double>(Right.I32);
							switch (Instruction.Opcode)
							{
							case EOpcode::Eq:
								{
									Value = L == R;
									break;
								}
							case EOpcode::Ne:
								{
									Value = L != R;
									break;
								}
							case EOpcode::Lt:
								{
									Value = L < R;
									break;
								}
							case EOpcode::Le:
								{
									Value = L <= R;
									break;
								}
							case EOpcode::Gt:
								{
									Value = L > R;
									break;
								}
							case EOpcode::Ge:
								{
									Value = L >= R;
									break;
								}
							default:
								{
									break;
								}
							}
						}
						else if (Left.Kind == EStackKind::F32 || Right.Kind == EStackKind::F32)
						{
							const float L = Left.Kind == EStackKind::F32
							    ? Left.F32
							    : Left.Kind == EStackKind::U32
							    ? static_cast<float>(Left.U32)
							    : static_cast<float>(Left.I32);
							const float R = Right.Kind == EStackKind::F32
							    ? Right.F32
							    : Right.Kind == EStackKind::U32
							    ? static_cast<float>(Right.U32)
							    : static_cast<float>(Right.I32);
							switch (Instruction.Opcode)
							{
							case EOpcode::Eq:
								{
									Value = L == R;
									break;
								}
							case EOpcode::Ne:
								{
									Value = L != R;
									break;
								}
							case EOpcode::Lt:
								{
									Value = L < R;
									break;
								}
							case EOpcode::Le:
								{
									Value = L <= R;
									break;
								}
							case EOpcode::Gt:
								{
									Value = L > R;
									break;
								}
							case EOpcode::Ge:
								{
									Value = L >= R;
									break;
								}
							default:
								{
									break;
								}
							}
						}
						else if (Left.Kind == EStackKind::U32 && Right.Kind == EStackKind::U32)
						{
							switch (Instruction.Opcode)
							{
							case EOpcode::Eq:
								{
									Value = Left.U32 == Right.U32;
									break;
								}
							case EOpcode::Ne:
								{
									Value = Left.U32 != Right.U32;
									break;
								}
							case EOpcode::Lt:
								{
									Value = Left.U32 < Right.U32;
									break;
								}
							case EOpcode::Le:
								{
									Value = Left.U32 <= Right.U32;
									break;
								}
							case EOpcode::Gt:
								{
									Value = Left.U32 > Right.U32;
									break;
								}
							case EOpcode::Ge:
								{
									Value = Left.U32 >= Right.U32;
									break;
								}
							default:
								{
									break;
								}
							}
						}
						else
						{
							if (Left.Kind != EStackKind::I32 || Right.Kind != EStackKind::I32)
							{
								return NotHandled();
							}
							switch (Instruction.Opcode)
							{
							case EOpcode::Eq:
								{
									Value = Left.I32 == Right.I32;
									break;
								}
							case EOpcode::Ne:
								{
									Value = Left.I32 != Right.I32;
									break;
								}
							case EOpcode::Lt:
								{
									Value = Left.I32 < Right.I32;
									break;
								}
							case EOpcode::Le:
								{
									Value = Left.I32 <= Right.I32;
									break;
								}
							case EOpcode::Gt:
								{
									Value = Left.I32 > Right.I32;
									break;
								}
							case EOpcode::Ge:
								{
									Value = Left.I32 >= Right.I32;
									break;
								}
							default:
								{
									break;
								}
							}
						}
					}
					if (!Push({EStackKind::Bool, Value ? 1 : 0, nullptr, 0xFFFFu}))
					{
						return NotHandled();
					}
					Continue();
					break;
				}
			case EOpcode::Neg:
			case EOpcode::BitNot:
			case EOpcode::Not:
				{
					if (StackSize == 0u)
					{
						return NotHandled();
					}
					FTypedValue& Value = Stack[StackSize - 1u];
					if (Instruction.Opcode == EOpcode::Not)
					{
						if (Value.Kind != EStackKind::Bool)
						{
							return NotHandled();
						}
						Value.I32 = Value.I32 == 0 ? 1 : 0;
					}
					else
					{
						if (Instruction.Opcode == EOpcode::Neg)
						{
							if (Value.Kind == EStackKind::F64)
							{
								Value.F64 = -Value.F64;
							}
							else if (Value.Kind == EStackKind::F32)
							{
								Value.F32 = -Value.F32;
							}
							else
							{
								if (Value.Kind != EStackKind::I32)
								{
									return NotHandled();
								}
								if (Value.I32 == std::numeric_limits<std::int32_t>::min())
								{
									return Fail(Instruction, "integer overflow");
								}
								Value.I32 = -Value.I32;
							}
						}
						else
						{
							if (Value.Kind == EStackKind::U32)
							{
								Value.U32 = ~Value.U32;
							}
							else
							{
								if (Value.Kind != EStackKind::I32)
								{
									return NotHandled();
								}
								Value.I32 = ~Value.I32;
							}
						}
					}
					Continue();
					break;
				}
			case EOpcode::Dup:
				{
					if (StackSize == 0u || !Push(Stack[StackSize - 1u]))
					{
						return NotHandled();
					}
					Continue();
					break;
				}
			case EOpcode::Pop:
				{
					if (StackSize == 0u)
					{
						return NotHandled();
					}
					--StackSize;
					Continue();
					break;
				}
			case EOpcode::CallBuiltin:
				{
					if (Instruction.ArgumentCount != 1u)
					{
						return NotHandled();
					}
					FTypedValue Argument;
					if (!Pop(Argument))
					{
						return NotHandled();
					}
					const EBuiltinId Builtin = static_cast<EBuiltinId>(Instruction.Immediate);
					FTypedValue Converted;
					if (Builtin == EBuiltinId::ToInt)
					{
						Converted.Kind = EStackKind::I32;
						if (Argument.Kind == EStackKind::I32)
						{
							Converted.I32 = Argument.I32;
						}
						else if (Argument.Kind == EStackKind::U32)
						{
							if (Argument.U32 > static_cast<std::uint32_t>(
							                       std::numeric_limits<std::int32_t>::max()))
							{
								return Fail(Instruction, "to_int overflow");
							}
							Converted.I32 = static_cast<std::int32_t>(Argument.U32);
						}
						else if (Argument.Kind == EStackKind::Bool)
						{
							Converted.I32 = Argument.I32 != 0 ? 1 : 0;
						}
						else if (Argument.Kind == EStackKind::F64 || Argument.Kind == EStackKind::F32)
						{
							const double Scalar = Argument.Kind == EStackKind::F64
							    ? Argument.F64
							    : static_cast<double>(Argument.F32);
							if (!std::isfinite(Scalar))
							{
								return Fail(Instruction, "to_int overflow");
							}
							const long double Truncated = std::trunc(
							    static_cast<long double>(Scalar));
							if (Truncated < static_cast<long double>(
							                    std::numeric_limits<std::int32_t>::min()) ||
							    Truncated > static_cast<long double>(
							                    std::numeric_limits<std::int32_t>::max()))
							{
								return Fail(Instruction, "to_int overflow");
							}
							Converted.I32 = static_cast<std::int32_t>(Truncated);
						}
						else
						{
							return NotHandled();
						}
					}
					else if (Builtin == EBuiltinId::ToUnsigned)
					{
						Converted.Kind = EStackKind::U32;
						if (Argument.Kind == EStackKind::U32)
						{
							Converted.U32 = Argument.U32;
						}
						else if (Argument.Kind == EStackKind::I32)
						{
							if (Argument.I32 < 0)
							{
								return Fail(Instruction, "to_unsigned overflow");
							}
							Converted.U32 = static_cast<std::uint32_t>(Argument.I32);
						}
						else if (Argument.Kind == EStackKind::Bool)
						{
							Converted.U32 = Argument.I32 != 0 ? 1u : 0u;
						}
						else if (Argument.Kind == EStackKind::F64 || Argument.Kind == EStackKind::F32)
						{
							const double Scalar = Argument.Kind == EStackKind::F64
							    ? Argument.F64
							    : static_cast<double>(Argument.F32);
							if (!std::isfinite(Scalar) || Scalar < 0.0 || Scalar >= 4294967296.0)
							{
								return Fail(Instruction, "to_unsigned overflow");
							}
							Converted.U32 = static_cast<std::uint32_t>(Scalar);
						}
						else
						{
							return NotHandled();
						}
					}
					else if (Builtin == EBuiltinId::ToFloat)
					{
						Converted.Kind = EStackKind::F32;
						if (Argument.Kind == EStackKind::F32)
						{
							Converted.F32 = Argument.F32;
						}
						else if (Argument.Kind == EStackKind::F64)
						{
							Converted.F32 = static_cast<float>(Argument.F64);
						}
						else if (Argument.Kind == EStackKind::U32)
						{
							Converted.F32 = static_cast<float>(Argument.U32);
						}
						else if (Argument.Kind == EStackKind::I32)
						{
							Converted.F32 = static_cast<float>(Argument.I32);
						}
						else if (Argument.Kind == EStackKind::Bool)
						{
							Converted.F32 = Argument.I32 != 0 ? 1.0f : 0.0f;
						}
						else
						{
							return NotHandled();
						}
					}
					else if (Builtin == EBuiltinId::ToDouble)
					{
						Converted.Kind = EStackKind::F64;
						if (Argument.Kind == EStackKind::F64)
						{
							Converted.F64 = Argument.F64;
						}
						else if (Argument.Kind == EStackKind::F32)
						{
							Converted.F64 = static_cast<double>(Argument.F32);
						}
						else if (Argument.Kind == EStackKind::U32)
						{
							Converted.F64 = static_cast<double>(Argument.U32);
						}
						else if (Argument.Kind == EStackKind::I32)
						{
							Converted.F64 = static_cast<double>(Argument.I32);
						}
						else if (Argument.Kind == EStackKind::Bool)
						{
							Converted.F64 = Argument.I32 != 0 ? 1.0 : 0.0;
						}
						else
						{
							return NotHandled();
						}
					}
					else
					{
						return NotHandled();
					}
					if (!Push(Converted))
					{
						return NotHandled();
					}
					Continue();
					break;
				}
			case EOpcode::GetIndexCopy:
				{
					FTypedValue Index, Container;
					if (!Pop(Index) || !Pop(Container))
					{
						return NotHandled();
					}
					std::int32_t Value = 0;
					if (Container.Kind == EStackKind::ArrayI32)
					{
						if (Index.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<std::vector<std::int32_t>*>(Container.Storage);
						if (Index.I32 < 0 || static_cast<std::size_t>(Index.I32) >= Values.size())
						{
							return Fail(Instruction, "array index out of bounds");
						}
						Value = Values[static_cast<std::size_t>(Index.I32)];
						++ArrayGets;
					}
					else if (Container.Kind == EStackKind::ArrayU32)
					{
						if (Index.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<std::vector<std::uint32_t>*>(Container.Storage);
						if (Index.I32 < 0 || static_cast<std::size_t>(Index.I32) >= Values.size())
						{
							return Fail(Instruction, "array index out of bounds");
						}
						FTypedValue FoundValue;
						FoundValue.Kind = EStackKind::U32;
						FoundValue.U32 = Values[static_cast<std::size_t>(Index.I32)];
						if (!Push(FoundValue))
						{
							return NotHandled();
						}
						++ArrayGets;
						Continue();
						break;
					}
					else if (Container.Kind == EStackKind::ArrayF32)
					{
						if (Index.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<std::vector<float>*>(Container.Storage);
						if (Index.I32 < 0 || static_cast<std::size_t>(Index.I32) >= Values.size())
						{
							return Fail(Instruction, "array index out of bounds");
						}
						FTypedValue FoundValue;
						FoundValue.Kind = EStackKind::F32;
						FoundValue.F32 = Values[static_cast<std::size_t>(Index.I32)];
						if (!Push(FoundValue))
						{
							return NotHandled();
						}
						++ArrayGets;
						Continue();
						break;
					}
					else if (Container.Kind == EStackKind::ArrayString)
					{
						if (Index.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<std::vector<std::string>*>(Container.Storage);
						if (Index.I32 < 0 || static_cast<std::size_t>(Index.I32) >= Values.size())
						{
							return Fail(Instruction, "array index out of bounds");
						}
						if (!Push({EStackKind::String, 0,
						        &Values[static_cast<std::size_t>(Index.I32)], 0xFFFFu}))
						{
							return NotHandled();
						}
						++ArrayGets;
						Continue();
						break;
					}
					else if (Container.Kind == EStackKind::ArrayF64)
					{
						if (Index.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<std::vector<double>*>(Container.Storage);
						if (Index.I32 < 0 || static_cast<std::size_t>(Index.I32) >= Values.size())
						{
							return Fail(Instruction, "array index out of bounds");
						}
						FTypedValue FoundValue;
						FoundValue.Kind = EStackKind::F64;
						FoundValue.F64 = Values[static_cast<std::size_t>(Index.I32)];
						if (!Push(FoundValue))
						{
							return NotHandled();
						}
						++ArrayGets;
						Continue();
						break;
					}
					else if (Container.Kind == EStackKind::ArrayWString)
					{
						if (Index.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<std::vector<std::u16string>*>(Container.Storage);
						if (Index.I32 < 0 || static_cast<std::size_t>(Index.I32) >= Values.size())
						{
							return Fail(Instruction, "array index out of bounds");
						}
						if (!Push({EStackKind::WString, 0,
						        &Values[static_cast<std::size_t>(Index.I32)], 0xFFFFu}))
						{
							return NotHandled();
						}
						++ArrayGets;
						Continue();
						break;
					}
					else if (Container.Kind == EStackKind::MapI32I32)
					{
						if (Index.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<FMap::FIntI32Entries*>(Container.Storage);
						const std::int32_t* const Found = Values.Find(Index.I32);
						if (!Found)
						{
							return Fail(Instruction, "map key not found");
						}
						Value = *Found;
						++MapGets;
					}
					else if (Container.Kind == EStackKind::MapI32U32)
					{
						if (Index.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<FMap::FIntU32Entries*>(Container.Storage);
						const std::uint32_t* const Found = Values.Find(Index.I32);
						if (!Found)
						{
							return Fail(Instruction, "map key not found");
						}
						FTypedValue FoundValue;
						FoundValue.Kind = EStackKind::U32;
						FoundValue.U32 = *Found;
						if (!Push(FoundValue))
						{
							return NotHandled();
						}
						++MapGets;
						Continue();
						break;
					}
					else if (Container.Kind == EStackKind::MapI32F32)
					{
						if (Index.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<FMap::FIntF32Entries*>(Container.Storage);
						const float* const Found = Values.Find(Index.I32);
						if (!Found)
						{
							return Fail(Instruction, "map key not found");
						}
						FTypedValue FoundValue;
						FoundValue.Kind = EStackKind::F32;
						FoundValue.F32 = *Found;
						if (!Push(FoundValue))
						{
							return NotHandled();
						}
						++MapGets;
						Continue();
						break;
					}
					else if (Container.Kind == EStackKind::MapI32F64)
					{
						if (Index.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<FMap::FIntF64Entries*>(Container.Storage);
						const double* const Found = Values.Find(Index.I32);
						if (!Found)
						{
							return Fail(Instruction, "map key not found");
						}
						FTypedValue FoundValue;
						FoundValue.Kind = EStackKind::F64;
						FoundValue.F64 = *Found;
						if (!Push(FoundValue))
						{
							return NotHandled();
						}
						++MapGets;
						Continue();
						break;
					}
					else if (Container.Kind == EStackKind::MapU32I32)
					{
						if (Index.Kind != EStackKind::U32)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<FMap::FUIntI32Entries*>(Container.Storage);
						const std::int32_t* const Found = Values.Find(Index.U32);
						if (!Found)
						{
							return Fail(Instruction, "map key not found");
						}
						Value = *Found;
						++MapGets;
					}
					else if (Container.Kind == EStackKind::MapU32U32)
					{
						if (Index.Kind != EStackKind::U32)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<FMap::FUIntU32Entries*>(Container.Storage);
						const std::uint32_t* const Found = Values.Find(Index.U32);
						if (!Found)
						{
							return Fail(Instruction, "map key not found");
						}
						FTypedValue FoundValue;
						FoundValue.Kind = EStackKind::U32;
						FoundValue.U32 = *Found;
						if (!Push(FoundValue))
						{
							return NotHandled();
						}
						++MapGets;
						Continue();
						break;
					}
					else if (Container.Kind == EStackKind::MapU32F32)
					{
						if (Index.Kind != EStackKind::U32)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<FMap::FUIntF32Entries*>(Container.Storage);
						const float* const Found = Values.Find(Index.U32);
						if (!Found)
						{
							return Fail(Instruction, "map key not found");
						}
						FTypedValue FoundValue;
						FoundValue.Kind = EStackKind::F32;
						FoundValue.F32 = *Found;
						if (!Push(FoundValue))
						{
							return NotHandled();
						}
						++MapGets;
						Continue();
						break;
					}
					else if (Container.Kind == EStackKind::MapU32F64)
					{
						if (Index.Kind != EStackKind::U32)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<FMap::FUIntF64Entries*>(Container.Storage);
						const double* const Found = Values.Find(Index.U32);
						if (!Found)
						{
							return Fail(Instruction, "map key not found");
						}
						FTypedValue FoundValue;
						FoundValue.Kind = EStackKind::F64;
						FoundValue.F64 = *Found;
						if (!Push(FoundValue))
						{
							return NotHandled();
						}
						++MapGets;
						Continue();
						break;
					}
					else if (Container.Kind == EStackKind::MapStringI32)
					{
						if (Index.Kind != EStackKind::String || !Index.Storage)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<FMap::FStringI32Entries*>(Container.Storage);
						const auto& Key = *static_cast<const std::string*>(Index.Storage);
						const std::int32_t* const Found = Values.Find(Key);
						if (!Found)
						{
							return Fail(Instruction, "map key not found");
						}
						Value = *Found;
						++MapGets;
					}
					else if (Container.Kind == EStackKind::MapWStringI32)
					{
						if (Index.Kind != EStackKind::WString || !Index.Storage)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<FMap::FWStringI32Entries*>(Container.Storage);
						const auto& Key = *static_cast<const std::u16string*>(Index.Storage);
						const std::int32_t* const Found = Values.Find(Key);
						if (!Found)
						{
							return Fail(Instruction, "map key not found");
						}
						Value = *Found;
						++MapGets;
					}
					else
					{
						return NotHandled();
					}
					if (!Push({EStackKind::I32, Value, nullptr, 0xFFFFu}))
					{
						return NotHandled();
					}
					Continue();
					break;
				}
			case EOpcode::SetIndex:
				{
					FTypedValue Value, Index, Container;
					if (!Pop(Value) || !Pop(Index) || !Pop(Container))
					{
						return NotHandled();
					}
					if (Container.Kind == EStackKind::ArrayI32)
					{
						if (Index.Kind != EStackKind::I32 || Value.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<std::vector<std::int32_t>*>(Container.Storage);
						if (Index.I32 < 0 || static_cast<std::size_t>(Index.I32) >= Values.size())
						{
							return Fail(Instruction, "array index out of bounds");
						}
						Values[static_cast<std::size_t>(Index.I32)] = Value.I32;
						++ArraySets;
						bStateMutated = true;
					}
					else if (Container.Kind == EStackKind::MapI32F32)
					{
						if (Index.Kind != EStackKind::I32 || (Value.Kind != EStackKind::F32 && Value.Kind != EStackKind::I32 && Value.Kind != EStackKind::U32))
						{
							return NotHandled();
						}
						const float Scalar = Value.Kind == EStackKind::F32
						    ? Value.F32
						    : Value.Kind == EStackKind::U32
						    ? static_cast<float>(Value.U32)
						    : static_cast<float>(Value.I32);
						static_cast<FMap::FIntF32Entries*>(Container.Storage)
						    ->InsertOrAssign(Index.I32, Scalar);
						++MapSets;
						bStateMutated = true;
					}
					else if (Container.Kind == EStackKind::ArrayU32)
					{
						if (Index.Kind != EStackKind::I32 || Value.Kind != EStackKind::U32)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<std::vector<std::uint32_t>*>(Container.Storage);
						if (Index.I32 < 0 || static_cast<std::size_t>(Index.I32) >= Values.size())
						{
							return Fail(Instruction, "array index out of bounds");
						}
						Values[static_cast<std::size_t>(Index.I32)] = Value.U32;
						++ArraySets;
						bStateMutated = true;
					}
					else if (Container.Kind == EStackKind::ArrayF32)
					{
						if (Index.Kind != EStackKind::I32 || (Value.Kind != EStackKind::F32 && Value.Kind != EStackKind::I32 && Value.Kind != EStackKind::U32))
						{
							return NotHandled();
						}
						auto& Values = *static_cast<std::vector<float>*>(Container.Storage);
						if (Index.I32 < 0 || static_cast<std::size_t>(Index.I32) >= Values.size())
						{
							return Fail(Instruction, "array index out of bounds");
						}
						Values[static_cast<std::size_t>(Index.I32)] = Value.Kind == EStackKind::F32
						    ? Value.F32
						    : Value.Kind == EStackKind::U32
						    ? static_cast<float>(Value.U32)
						    : static_cast<float>(Value.I32);
						++ArraySets;
						bStateMutated = true;
					}
					else if (Container.Kind == EStackKind::ArrayF64)
					{
						if (Index.Kind != EStackKind::I32 || (Value.Kind != EStackKind::F64 && Value.Kind != EStackKind::I32))
						{
							return NotHandled();
						}
						auto& Values = *static_cast<std::vector<double>*>(Container.Storage);
						if (Index.I32 < 0 || static_cast<std::size_t>(Index.I32) >= Values.size())
						{
							return Fail(Instruction, "array index out of bounds");
						}
						Values[static_cast<std::size_t>(Index.I32)] = Value.Kind == EStackKind::F64
						    ? Value.F64
						    : static_cast<double>(Value.I32);
						++ArraySets;
						bStateMutated = true;
					}
					else if (Container.Kind == EStackKind::MapI32I32)
					{
						if (Index.Kind != EStackKind::I32 || Value.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						static_cast<FMap::FIntI32Entries*>(Container.Storage)
						    ->InsertOrAssign(Index.I32, Value.I32);
						++MapSets;
						bStateMutated = true;
					}
					else if (Container.Kind == EStackKind::MapI32U32)
					{
						if (Index.Kind != EStackKind::I32 || Value.Kind != EStackKind::U32)
						{
							return NotHandled();
						}
						static_cast<FMap::FIntU32Entries*>(Container.Storage)
						    ->InsertOrAssign(Index.I32, Value.U32);
						++MapSets;
						bStateMutated = true;
					}
					else if (Container.Kind == EStackKind::MapI32F64)
					{
						if (Index.Kind != EStackKind::I32 || (Value.Kind != EStackKind::F64 && Value.Kind != EStackKind::I32))
						{
							return NotHandled();
						}
						const double Scalar = Value.Kind == EStackKind::F64
						    ? Value.F64
						    : static_cast<double>(Value.I32);
						static_cast<FMap::FIntF64Entries*>(Container.Storage)
						    ->InsertOrAssign(Index.I32, Scalar);
						++MapSets;
						bStateMutated = true;
					}
					else if (Container.Kind == EStackKind::MapU32I32)
					{
						if (Index.Kind != EStackKind::U32 || Value.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						static_cast<FMap::FUIntI32Entries*>(Container.Storage)
						    ->InsertOrAssign(Index.U32, Value.I32);
						++MapSets;
						bStateMutated = true;
					}
					else if (Container.Kind == EStackKind::MapU32U32)
					{
						if (Index.Kind != EStackKind::U32 || Value.Kind != EStackKind::U32)
						{
							return NotHandled();
						}
						static_cast<FMap::FUIntU32Entries*>(Container.Storage)
						    ->InsertOrAssign(Index.U32, Value.U32);
						++MapSets;
						bStateMutated = true;
					}
					else if (Container.Kind == EStackKind::MapU32F32)
					{
						if (Index.Kind != EStackKind::U32 || (Value.Kind != EStackKind::F32 && Value.Kind != EStackKind::I32 && Value.Kind != EStackKind::U32))
						{
							return NotHandled();
						}
						const float Scalar = Value.Kind == EStackKind::F32
						    ? Value.F32
						    : Value.Kind == EStackKind::U32
						    ? static_cast<float>(Value.U32)
						    : static_cast<float>(Value.I32);
						static_cast<FMap::FUIntF32Entries*>(Container.Storage)
						    ->InsertOrAssign(Index.U32, Scalar);
						++MapSets;
						bStateMutated = true;
					}
					else if (Container.Kind == EStackKind::MapU32F64)
					{
						if (Index.Kind != EStackKind::U32 || (Value.Kind != EStackKind::F64 && Value.Kind != EStackKind::I32 && Value.Kind != EStackKind::U32))
						{
							return NotHandled();
						}
						const double Scalar = Value.Kind == EStackKind::F64
						    ? Value.F64
						    : Value.Kind == EStackKind::U32
						    ? static_cast<double>(Value.U32)
						    : static_cast<double>(Value.I32);
						static_cast<FMap::FUIntF64Entries*>(Container.Storage)
						    ->InsertOrAssign(Index.U32, Scalar);
						++MapSets;
						bStateMutated = true;
					}
					else if (Container.Kind == EStackKind::ArrayString)
					{
						if (Index.Kind != EStackKind::I32 || Value.Kind != EStackKind::String || !Value.Storage)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<std::vector<std::string>*>(Container.Storage);
						if (Index.I32 < 0 || static_cast<std::size_t>(Index.I32) >= Values.size())
						{
							return Fail(Instruction, "array index out of bounds");
						}
						Values[static_cast<std::size_t>(Index.I32)] =
						    *static_cast<const std::string*>(Value.Storage);
						++ArraySets;
						bStateMutated = true;
					}
					else if (Container.Kind == EStackKind::ArrayWString)
					{
						if (Index.Kind != EStackKind::I32 || Value.Kind != EStackKind::WString || !Value.Storage)
						{
							return NotHandled();
						}
						auto& Values = *static_cast<std::vector<std::u16string>*>(Container.Storage);
						if (Index.I32 < 0 || static_cast<std::size_t>(Index.I32) >= Values.size())
						{
							return Fail(Instruction, "array index out of bounds");
						}
						Values[static_cast<std::size_t>(Index.I32)] =
						    *static_cast<const std::u16string*>(Value.Storage);
						++ArraySets;
						bStateMutated = true;
					}
					else if (Container.Kind == EStackKind::MapStringI32)
					{
						if (Index.Kind != EStackKind::String || !Index.Storage || Value.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						static_cast<FMap::FStringI32Entries*>(Container.Storage)->InsertOrAssign(*static_cast<const std::string*>(Index.Storage), Value.I32);
						++MapSets;
						bStateMutated = true;
					}
					else if (Container.Kind == EStackKind::MapWStringI32)
					{
						if (Index.Kind != EStackKind::WString || !Index.Storage || Value.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						static_cast<FMap::FWStringI32Entries*>(Container.Storage)->InsertOrAssign(*static_cast<const std::u16string*>(Index.Storage), Value.I32);
						++MapSets;
						bStateMutated = true;
					}
					else
					{
						return NotHandled();
					}
					if (!Push(Container))
					{
						return NotHandled();
					}
					Continue();
					break;
				}
			case EOpcode::CallMethod:
				{
					FTypedValue Argument, Receiver;
					if (Instruction.ArgumentCount != 1u || !Pop(Argument) || !Pop(Receiver))
					{
						return NotHandled();
					}
					if (Instruction.Immediate == 1 && Receiver.Kind == EStackKind::MapI32I32)
					{
						if (Argument.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						const bool Found = static_cast<FMap::FIntI32Entries*>(
						                       Receiver.Storage)
						                       ->Find(Argument.I32) != nullptr;
						if (!Push({EStackKind::Bool, Found ? 1 : 0, nullptr, 0xFFFFu}))
						{
							return NotHandled();
						}
					}
					else if (Instruction.Immediate == 1 && Receiver.Kind == EStackKind::MapI32U32)
					{
						if (Argument.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						const bool Found = static_cast<FMap::FIntU32Entries*>(
						                       Receiver.Storage)
						                       ->Find(Argument.I32) != nullptr;
						if (!Push({EStackKind::Bool, Found ? 1 : 0, nullptr, 0xFFFFu}))
						{
							return NotHandled();
						}
					}
					else if (Instruction.Immediate == 1 && Receiver.Kind == EStackKind::MapI32F32)
					{
						if (Argument.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						const bool Found = static_cast<FMap::FIntF32Entries*>(
						                       Receiver.Storage)
						                       ->Find(Argument.I32) != nullptr;
						if (!Push({EStackKind::Bool, Found ? 1 : 0, nullptr, 0xFFFFu}))
						{
							return NotHandled();
						}
					}
					else if (Instruction.Immediate == 1 && Receiver.Kind == EStackKind::MapI32F64)
					{
						if (Argument.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						const bool Found = static_cast<FMap::FIntF64Entries*>(
						                       Receiver.Storage)
						                       ->Find(Argument.I32) != nullptr;
						if (!Push({EStackKind::Bool, Found ? 1 : 0, nullptr, 0xFFFFu}))
						{
							return NotHandled();
						}
					}
					else if (Instruction.Immediate == 1 && Receiver.Kind == EStackKind::MapU32I32)
					{
						if (Argument.Kind != EStackKind::U32)
						{
							return NotHandled();
						}
						const bool Found = static_cast<FMap::FUIntI32Entries*>(
						                       Receiver.Storage)
						                       ->Find(Argument.U32) != nullptr;
						if (!Push({EStackKind::Bool, Found ? 1 : 0, nullptr, 0xFFFFu}))
						{
							return NotHandled();
						}
					}
					else if (Instruction.Immediate == 1 && Receiver.Kind == EStackKind::MapU32U32)
					{
						if (Argument.Kind != EStackKind::U32)
						{
							return NotHandled();
						}
						const bool Found = static_cast<FMap::FUIntU32Entries*>(
						                       Receiver.Storage)
						                       ->Find(Argument.U32) != nullptr;
						if (!Push({EStackKind::Bool, Found ? 1 : 0, nullptr, 0xFFFFu}))
						{
							return NotHandled();
						}
					}
					else if (Instruction.Immediate == 1 && Receiver.Kind == EStackKind::MapU32F32)
					{
						if (Argument.Kind != EStackKind::U32)
						{
							return NotHandled();
						}
						const bool Found = static_cast<FMap::FUIntF32Entries*>(
						                       Receiver.Storage)
						                       ->Find(Argument.U32) != nullptr;
						if (!Push({EStackKind::Bool, Found ? 1 : 0, nullptr, 0xFFFFu}))
						{
							return NotHandled();
						}
					}
					else if (Instruction.Immediate == 1 && Receiver.Kind == EStackKind::MapU32F64)
					{
						if (Argument.Kind != EStackKind::U32)
						{
							return NotHandled();
						}
						const bool Found = static_cast<FMap::FUIntF64Entries*>(
						                       Receiver.Storage)
						                       ->Find(Argument.U32) != nullptr;
						if (!Push({EStackKind::Bool, Found ? 1 : 0, nullptr, 0xFFFFu}))
						{
							return NotHandled();
						}
					}
					else if (Instruction.Immediate == 1 && Receiver.Kind == EStackKind::MapStringI32)
					{
						if (Argument.Kind != EStackKind::String || !Argument.Storage)
						{
							return NotHandled();
						}
						const bool Found = static_cast<FMap::FStringI32Entries*>(
						                       Receiver.Storage)
						                       ->Find(
						                           *static_cast<const std::string*>(Argument.Storage)) != nullptr;
						if (!Push({EStackKind::Bool, Found ? 1 : 0, nullptr, 0xFFFFu}))
						{
							return NotHandled();
						}
					}
					else if (Instruction.Immediate == 1 && Receiver.Kind == EStackKind::MapWStringI32)
					{
						if (Argument.Kind != EStackKind::WString || !Argument.Storage)
						{
							return NotHandled();
						}
						const bool Found = static_cast<FMap::FWStringI32Entries*>(
						                       Receiver.Storage)
						                       ->Find(
						                           *static_cast<const std::u16string*>(Argument.Storage)) != nullptr;
						if (!Push({EStackKind::Bool, Found ? 1 : 0, nullptr, 0xFFFFu}))
						{
							return NotHandled();
						}
					}
					else if (Instruction.Immediate == 1 && Receiver.Kind == EStackKind::SetI32)
					{
						if (Argument.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						const bool Found = static_cast<FSet::FIntScalarElements*>(
						    Receiver.Storage)
						                       ->Contains(Argument.I32);
						if (!Push({EStackKind::Bool, Found ? 1 : 0, nullptr, 0xFFFFu}))
						{
							return NotHandled();
						}
					}
					else if (Instruction.Immediate == 1 && Receiver.Kind == EStackKind::SetU32)
					{
						if (Argument.Kind != EStackKind::U32)
						{
							return NotHandled();
						}
						const bool Found = static_cast<FSet::FUIntScalarElements*>(
						    Receiver.Storage)
						                       ->Contains(Argument.U32);
						if (!Push({EStackKind::Bool, Found ? 1 : 0, nullptr, 0xFFFFu}))
						{
							return NotHandled();
						}
					}
					else if (Instruction.Immediate == 2 && Receiver.Kind == EStackKind::ArrayI32)
					{
						if (Argument.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						static_cast<std::vector<std::int32_t>*>(Receiver.Storage)
						    ->push_back(Argument.I32);
						++ArrayPushes;
						if (!Push(Receiver))
						{
							return NotHandled();
						}
						bStateMutated = true;
					}
					else if (Instruction.Immediate == 2 && Receiver.Kind == EStackKind::ArrayU32)
					{
						if (Argument.Kind != EStackKind::U32)
						{
							return NotHandled();
						}
						static_cast<std::vector<std::uint32_t>*>(Receiver.Storage)
						    ->push_back(Argument.U32);
						++ArrayPushes;
						if (!Push(Receiver))
						{
							return NotHandled();
						}
						bStateMutated = true;
					}
					else if (Instruction.Immediate == 2 && Receiver.Kind == EStackKind::ArrayF32)
					{
						if (Argument.Kind != EStackKind::F32 && Argument.Kind != EStackKind::I32 && Argument.Kind != EStackKind::U32)
						{
							return NotHandled();
						}
						const float Scalar = Argument.Kind == EStackKind::F32
						    ? Argument.F32
						    : Argument.Kind == EStackKind::U32
						    ? static_cast<float>(Argument.U32)
						    : static_cast<float>(Argument.I32);
						static_cast<std::vector<float>*>(Receiver.Storage)->push_back(Scalar);
						++ArrayPushes;
						if (!Push(Receiver))
						{
							return NotHandled();
						}
						bStateMutated = true;
					}
					else if (Instruction.Immediate == 2 && Receiver.Kind == EStackKind::ArrayF64)
					{
						if (Argument.Kind != EStackKind::F64 && Argument.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						static_cast<std::vector<double>*>(Receiver.Storage)->push_back(Argument.Kind == EStackKind::F64 ? Argument.F64 : static_cast<double>(Argument.I32));
						++ArrayPushes;
						if (!Push(Receiver))
						{
							return NotHandled();
						}
						bStateMutated = true;
					}
					else if (Instruction.Immediate == 2 && Receiver.Kind == EStackKind::ArrayString)
					{
						if (Argument.Kind != EStackKind::String || !Argument.Storage)
						{
							return NotHandled();
						}
						// Copy before push: the borrowed argument may refer to an element
						// of this same vector and push_back may reallocate it.
						const std::string Copy =
						    *static_cast<const std::string*>(Argument.Storage);
						static_cast<std::vector<std::string>*>(Receiver.Storage)->push_back(Copy);
						++ArrayPushes;
						if (!Push(Receiver))
						{
							return NotHandled();
						}
						bStateMutated = true;
					}
					else if (Instruction.Immediate == 2 && Receiver.Kind == EStackKind::ArrayWString)
					{
						if (Argument.Kind != EStackKind::WString || !Argument.Storage)
						{
							return NotHandled();
						}
						const std::u16string Copy =
						    *static_cast<const std::u16string*>(Argument.Storage);
						static_cast<std::vector<std::u16string>*>(Receiver.Storage)->push_back(Copy);
						++ArrayPushes;
						if (!Push(Receiver))
						{
							return NotHandled();
						}
						bStateMutated = true;
					}
					else if (Instruction.Immediate == 3 && Receiver.Kind == EStackKind::SetI32)
					{
						if (Argument.Kind != EStackKind::I32)
						{
							return NotHandled();
						}
						static_cast<FSet::FIntScalarElements*>(Receiver.Storage)
						    ->Insert(Argument.I32);
						if (!Push(Receiver))
						{
							return NotHandled();
						}
						bStateMutated = true;
					}
					else if (Instruction.Immediate == 3 && Receiver.Kind == EStackKind::SetU32)
					{
						if (Argument.Kind != EStackKind::U32)
						{
							return NotHandled();
						}
						static_cast<FSet::FUIntScalarElements*>(Receiver.Storage)
						    ->Insert(Argument.U32);
						if (!Push(Receiver))
						{
							return NotHandled();
						}
						bStateMutated = true;
					}
					else if (Instruction.Immediate == 1 && Receiver.Kind == EStackKind::SetString)
					{
						if (Argument.Kind != EStackKind::String || !Argument.Storage)
						{
							return NotHandled();
						}
						const bool Found = static_cast<FSet::FStringScalarElements*>(
						    Receiver.Storage)
						                       ->Contains(
						                           *static_cast<const std::string*>(Argument.Storage));
						if (!Push({EStackKind::Bool, Found ? 1 : 0, nullptr, 0xFFFFu}))
						{
							return NotHandled();
						}
					}
					else if (Instruction.Immediate == 1 && Receiver.Kind == EStackKind::SetWString)
					{
						if (Argument.Kind != EStackKind::WString || !Argument.Storage)
						{
							return NotHandled();
						}
						const bool Found = static_cast<FSet::FWStringScalarElements*>(
						    Receiver.Storage)
						                       ->Contains(
						                           *static_cast<const std::u16string*>(Argument.Storage));
						if (!Push({EStackKind::Bool, Found ? 1 : 0, nullptr, 0xFFFFu}))
						{
							return NotHandled();
						}
					}
					else if (Instruction.Immediate == 3 && Receiver.Kind == EStackKind::SetString)
					{
						if (Argument.Kind != EStackKind::String || !Argument.Storage)
						{
							return NotHandled();
						}
						static_cast<FSet::FStringScalarElements*>(Receiver.Storage)->Insert(*static_cast<const std::string*>(Argument.Storage));
						if (!Push(Receiver))
						{
							return NotHandled();
						}
						bStateMutated = true;
					}
					else if (Instruction.Immediate == 3 && Receiver.Kind == EStackKind::SetWString)
					{
						if (Argument.Kind != EStackKind::WString || !Argument.Storage)
						{
							return NotHandled();
						}
						static_cast<FSet::FWStringScalarElements*>(Receiver.Storage)->Insert(*static_cast<const std::u16string*>(Argument.Storage));
						if (!Push(Receiver))
						{
							return NotHandled();
						}
						bStateMutated = true;
					}
					else
					{
						return NotHandled();
					}
					Continue();
					break;
				}
			case EOpcode::LoadField:
			case EOpcode::LoadFieldKeep:
				{
					FTypedValue Receiver;
					if (!Pop(Receiver) || Receiver.Kind != EStackKind::Object)
					{
						return NotHandled();
					}
					const FDirectFieldBinding& Field = DirectFields[Pc];
					if (!Field.Storage || Receiver.SourceSlot != Instruction.Operand1)
					{
						return NotHandled();
					}
					if (Instruction.Opcode == EOpcode::LoadFieldKeep && !Push(Receiver))
					{
						return NotHandled();
					}
					if (Field.Kind == FDirectFieldBinding::EKind::I32)
					{
						if (!Push({EStackKind::I32,
						        *static_cast<std::int32_t*>(Field.Storage), nullptr, 0xFFFFu}))
						{
							return NotHandled();
						}
					}
					else if (Field.Kind == FDirectFieldBinding::EKind::U32)
					{
						FTypedValue Value;
						Value.Kind = EStackKind::U32;
						Value.U32 = *static_cast<std::uint32_t*>(Field.Storage);
						if (!Push(Value))
						{
							return NotHandled();
						}
					}
					else if (Field.Kind == FDirectFieldBinding::EKind::F32)
					{
						FTypedValue Value;
						Value.Kind = EStackKind::F32;
						Value.F32 = *static_cast<float*>(Field.Storage);
						if (!Push(Value))
						{
							return NotHandled();
						}
					}
					else if (Field.Kind == FDirectFieldBinding::EKind::F64)
					{
						FTypedValue Value;
						Value.Kind = EStackKind::F64;
						Value.F64 = *static_cast<double*>(Field.Storage);
						if (!Push(Value))
						{
							return NotHandled();
						}
					}
					else
					{
						return NotHandled();
					}
					Continue();
					break;
				}
			case EOpcode::IncField:
				{
					FTypedValue Receiver;
					if (!Pop(Receiver) || Receiver.Kind != EStackKind::Object)
					{
						return NotHandled();
					}
					FDirectFieldBinding& Field = DirectFields[Pc];
					if (!Field.Storage || !Field.Object || Receiver.SourceSlot != Instruction.Operand1)
					{
						return NotHandled();
					}
					if (Field.Kind == FDirectFieldBinding::EKind::I32)
					{
						std::int32_t Updated = 0;
						auto& Storage = *static_cast<std::int32_t*>(Field.Storage);
						if (!Runtime::CheckedAdd(Storage, 1, Updated))
						{
							return Fail(Instruction, "integer overflow");
						}
						Storage = Updated;
					}
					else if (Field.Kind == FDirectFieldBinding::EKind::F64)
					{
						*static_cast<double*>(Field.Storage) += 1.0;
					}
					else if (Field.Kind == FDirectFieldBinding::EKind::F32)
					{
						*static_cast<float*>(Field.Storage) += 1.0f;
					}
					else if (Field.Kind == FDirectFieldBinding::EKind::U32)
					{
						++*static_cast<std::uint32_t*>(Field.Storage);
					}
					else
					{
						return NotHandled();
					}
					Vm.JitCommitBorrowedFieldMutation(*Field.Object, Field.FieldIndex);
					if (Vm.HasError())
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    Instruction.BytecodeOffset};
					}
					bStateMutated = true;
					Continue();
					break;
				}
			case EOpcode::StoreField:
				{
					FTypedValue Value, Receiver;
					if (!Pop(Value) || !Pop(Receiver) || Receiver.Kind != EStackKind::Object)
					{
						return NotHandled();
					}
					FDirectFieldBinding& Field = DirectFields[Pc];
					if (!Field.Storage || !Field.Object || Receiver.SourceSlot != Instruction.Operand1)
					{
						return NotHandled();
					}
					if (Field.Kind == FDirectFieldBinding::EKind::I32 && Value.Kind == EStackKind::I32)
					{
						*static_cast<std::int32_t*>(Field.Storage) = Value.I32;
					}
					else if (Field.Kind == FDirectFieldBinding::EKind::U32 && Value.Kind == EStackKind::U32)
					{
						*static_cast<std::uint32_t*>(Field.Storage) = Value.U32;
					}
					else if (Field.Kind == FDirectFieldBinding::EKind::F32 && (Value.Kind == EStackKind::F32 || Value.Kind == EStackKind::I32 || Value.Kind == EStackKind::U32))
					{
						*static_cast<float*>(Field.Storage) = Value.Kind == EStackKind::F32
						    ? Value.F32
						    : Value.Kind == EStackKind::U32
						    ? static_cast<float>(Value.U32)
						    : static_cast<float>(Value.I32);
					}
					else if (Field.Kind == FDirectFieldBinding::EKind::F64 && (Value.Kind == EStackKind::F64 || Value.Kind == EStackKind::F32 || Value.Kind == EStackKind::I32 || Value.Kind == EStackKind::U32))
					{
						*static_cast<double*>(Field.Storage) = Value.Kind == EStackKind::F64
						    ? Value.F64
						    : Value.Kind == EStackKind::F32
						    ? static_cast<double>(Value.F32)
						    : Value.Kind == EStackKind::U32
						    ? static_cast<double>(Value.U32)
						    : static_cast<double>(Value.I32);
					}
					else
					{
						return NotHandled();
					}
					Vm.JitCommitBorrowedFieldMutation(*Field.Object, Field.FieldIndex);
					if (Vm.HasError())
					{
						return {ETypedLoopExecution::Failed, HeaderOffset,
						    Instruction.BytecodeOffset};
					}
					bStateMutated = true;
					if (!Push(Value))
					{
						return NotHandled();
					}
					Continue();
					break;
				}
			case EOpcode::Jmp:
				{
					if (Instruction.BranchInstruction == InvalidInstruction)
					{
						Result = {ETypedLoopExecution::Completed,
						    Instruction.ExternalBranchOffset, Instruction.BytecodeOffset};
						Pc = static_cast<std::uint32_t>(Plan.Instructions.size());
					}
					else
					{
						if (Instruction.BranchInstruction <= Pc && BackEdgeBudget-- == 0u)
						{
							return Fail(Instruction, "JIT typed loop: step budget exceeded");
						}
						Pc = Instruction.BranchInstruction;
					}
					break;
				}
			case EOpcode::JmpIfFalse:
				{
					FTypedValue Condition;
					if (!Pop(Condition) || Condition.Kind != EStackKind::Bool)
					{
						return NotHandled();
					}
					if (Condition.I32 == 0)
					{
						if (Instruction.BranchInstruction == InvalidInstruction)
						{
							Result = {ETypedLoopExecution::Completed,
							    Instruction.ExternalBranchOffset, Instruction.BytecodeOffset};
							Pc = static_cast<std::uint32_t>(Plan.Instructions.size());
						}
						else
						{
							if (Instruction.BranchInstruction <= Pc && BackEdgeBudget-- == 0u)
							{
								return Fail(Instruction,
								    "JIT typed loop: step budget exceeded");
							}
							Pc = Instruction.BranchInstruction;
						}
					}
					else
					{
						Continue();
					}
					break;
				}
			case EOpcode::JmpIfLocalGeImm:
				{
					if (!Bind(Instruction.Operand0))
					{
						return NotHandled();
					}
					const FLocalBinding& Local = Locals[Instruction.Operand0];
					const bool bJump = Local.Kind == ELocalKind::I32
					    ? *static_cast<std::int32_t*>(Local.Storage) >= Instruction.Immediate
					    : Local.Kind == ELocalKind::U32
					    ? *static_cast<std::uint32_t*>(Local.Storage) >= static_cast<std::uint32_t>(Instruction.Immediate)
					    : Local.Kind == ELocalKind::F64
					    ? !(*static_cast<double*>(Local.Storage) < static_cast<double>(Instruction.Immediate))
					    : Local.Kind == ELocalKind::F32
					    ? !(*static_cast<float*>(Local.Storage) < static_cast<float>(Instruction.Immediate))
					    : false;
					if (Local.Kind != ELocalKind::I32 && Local.Kind != ELocalKind::U32 && Local.Kind != ELocalKind::F32 && Local.Kind != ELocalKind::F64)
					{
						return NotHandled();
					}
					if (bJump)
					{
						if (Instruction.BranchInstruction == InvalidInstruction)
						{
							Result = {ETypedLoopExecution::Completed,
							    Instruction.ExternalBranchOffset, Instruction.BytecodeOffset};
							Pc = static_cast<std::uint32_t>(Plan.Instructions.size());
						}
						else
						{
							if (Instruction.BranchInstruction <= Pc && BackEdgeBudget-- == 0u)
							{
								return Fail(Instruction,
								    "JIT typed loop: step budget exceeded");
							}
							Pc = Instruction.BranchInstruction;
						}
					}
					else
					{
						Continue();
					}
					break;
				}
			default:
				return NotHandled();
			}
		}

		if (Result.Status == ETypedLoopExecution::Completed)
		{
			Vm.RecordJitArrayFastPathPush(ArrayPushes);
			Vm.RecordJitArrayFastPathGet(ArrayGets);
			Vm.RecordJitArrayFastPathSet(ArraySets);
			Vm.RecordJitScalarMapFastPathGet(MapGets);
			Vm.RecordJitScalarMapFastPathSet(MapSets);
		}
		return Result;
	}
}
