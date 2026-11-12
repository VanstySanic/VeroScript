// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <type_traits>

namespace VeroScript
{
	bool VeroScriptVm::TrySortIntegerArray(FDynamicArray& Array)
	{
		// The script heap remains authoritative when its instruction boundaries
		// or individual writes are observable, including object mutation versions.
		if (Debugger || HasExecutionGovernance() || bHotReloadModuleInitializing ||
		    MutationCallback || InstanceCallInterceptor || !ActiveContainerCopyReads.empty() ||
		    Array.MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
		{
			return false;
		}
		if (!Runtime::IsPrimitiveValueTypeName(Array.ElementTypeName)) return false;

		return std::visit([&](auto& Values)
		{
			using TValues = std::decay_t<decltype(Values)>;
			if constexpr (!std::is_same_v<TValues, std::monostate>)
			{
				using TValue = typename TValues::value_type;
				if constexpr (std::is_integral_v<TValue> && !std::is_same_v<TValue, bool>)
				{
					// Compact storage can cross the host ABI. Check its declared type
					// once before bypassing the element assignment checks.
					if (!ValueMatchesElementType(FValue{TValue{}}, Array.ElementTypeName, {})) return false;
					std::sort(Values.begin(), Values.end());
					return true;
				}
			}
			return false;
		}, Array.ScalarElements);
	}

	bool VeroScriptVm::CallArrayAlgorithmBuiltin(EBuiltinId Builtin, FValue* A, std::uint8_t)
	{
		using namespace StandardLibrary;
		if (!CheckContainerCopyMutation(A[0])) return false;
		const FServiceContext Context{StandardLibraryLimits, GetMemoryWork()};
		if (!Context.Check()) return false;
		const auto* Handle = std::get_if<FDynamicArrayHandle>(&A[0]);
		if (!Handle || !*Handle)
		{
			ReportErrorId("diag.stdlib_arguments");
			return false;
		}
		auto& Array = **Handle;
		if (Builtin == EBuiltinId::RandomNextUInt || Builtin == EBuiltinId::RandomIndex)
		{
			if (OperandStack.size() >= MaxOperandStackDepth)
			{
				ReportError("operand stack overflow");
				return false;
			}
			OperandStack.reserve(OperandStack.size() + 1);
			if (Builtin == EBuiltinId::RandomNextUInt)
			{
				std::uint32_t Result = 0;
				if (!ReportStandardLibraryResult(NextRandomUInt(Array, Result, Context.Work))) return false;
				Push(Result);
			}
			else
			{
				std::int64_t Bound = 0, Result = 0;
				if (!Runtime::TryGetInteger64(A[1], Bound))
				{
					ReportErrorId("diag.stdlib_arguments");
					return false;
				}
				if (!ReportStandardLibraryResult(RandomIndex(Array, Bound, Result, Context.Work))) return false;
				Push(Result);
			}
			NotifyContainerMutation(A[0], EMutationKind::ContainerSet);
			return !bHasError;
		}
		const auto Count = Array.ElementCount();
		std::size_t Middle = 0;
		FDynamicArrayHandle Engine;
		if (Builtin == EBuiltinId::ArrayRotate && (!Runtime::TryGetSizeValue(A[1], Middle) || Middle > Count))
		{
			ReportErrorId("diag.stdlib_arguments");
			return false;
		}
		if (Builtin == EBuiltinId::ArrayShuffle)
		{
			const auto* Rng = std::get_if<FDynamicArrayHandle>(&A[1]);
			if (!Rng || !*Rng || Rng->get() == Handle->get() || !NativeElementTypeNamesEqual((*Rng)->ElementTypeName, "unsigned int"))
			{
				ReportErrorId("diag.stdlib_arguments");
				return false;
			}
			Engine = *Rng;
		}
		if (Count < 2 || (Builtin == EBuiltinId::ArrayRotate && (Middle == 0 || Middle == Count))) return true;
		const bool Transaction = static_cast<bool>(MutationCallback);
		bool Changed = false, RandomUsed = false;
		const auto Run = [&](auto&& Swap) -> bool
		{
			const auto Reverse = [&](std::size_t Left, std::size_t Right) -> bool
			{
				while (Left < Right && Left < --Right)
				{
					if (!Context.Check(1)) return false;
					Swap(Left++, Right);
					Changed = true;
				}
				return true;
			};
			if (Builtin == EBuiltinId::ArrayReverse) return Reverse(0, Count);
			if (Builtin == EBuiltinId::ArrayRotate) return Reverse(0, Middle) && Reverse(Middle, Count) && Reverse(0, Count);
			for (std::size_t End = Count - 1; End; --End)
			{
				std::int64_t Selected = 0;
				RandomUsed = true;
				if (!ReportStandardLibraryResult(RandomIndex(*Engine, static_cast<std::int64_t>(End + 1), Selected, Context.Work))) return false;
				if (!Context.Check(1)) return false;
				if (static_cast<std::size_t>(Selected) != End)
				{
					Swap(End, static_cast<std::size_t>(Selected));
					Changed = true;
				}
			}
			return true;
		};
		bool Ok = false;
		if (Transaction)
		{
			// Prepare indices and empty destination slots. The commit contains only
			// non-throwing moves and one storage swap; it never calls @clone.
			std::vector<std::size_t> Order;
			Order.reserve(Count);
			for (std::size_t I = 0; I < Count; ++I)
			{
				if (!Context.Check(1)) return false;
				Order.push_back(I);
			}
			if (!Run([&](std::size_t X, std::size_t Y)
			{
				std::swap(Order[X], Order[Y]);
			}))
			{
				return false;
			}
			if (!Changed)
			{
				Ok = true;
			}
			else
			{
				const auto Commit = [&](auto& Values) -> bool
				{
					using V = std::decay_t<decltype(Values)>;
					V Destination;
					Destination.reserve(std::max(Count, Values.capacity()));
					for (std::size_t I = 0; I < Count; ++I)
					{
						if (!Context.Check(1)) return false;
						Destination.emplace_back();
					}
					if (!Context.Check()) return false;
					for (std::size_t I = 0; I < Count; ++I)
					{
						Destination[I] = std::move(Values[Order[I]]);
					}
					Values.swap(Destination);
					return true;
				};
				if (Array.HasScalarElements())
				{
					std::visit([&](auto& Values)
					{
						using V = std::decay_t<decltype(Values)>;
						if constexpr (!std::is_same_v<V, std::monostate>)
						{
							Ok = Commit(Values);
						}
					}, Array.ScalarElements);
				}
				else
				{
					Ok = Commit(Array.Elements);
				}
			}
		}
		else
		{
			const auto Reorder = [&](auto& Values)
			{
				Ok = Run([&](std::size_t X, std::size_t Y)
				{
					std::iter_swap(Values.begin() + X, Values.begin() + Y);
				});
			};
			if (Array.HasScalarElements())
			{
				std::visit([&](auto& Values)
				{
					using V = std::decay_t<decltype(Values)>;
					if constexpr (!std::is_same_v<V, std::monostate>)
					{
						Reorder(Values);
					}
				}, Array.ScalarElements);
			}
			else
			{
				Reorder(Array.Elements);
			}
		}
		if (Changed && (Ok || !Transaction))
		{
			NotifyContainerMutation(A[0], EMutationKind::ContainerSet);
		}
		if (RandomUsed)
		{
			NotifyContainerMutation(A[1], EMutationKind::ContainerSet);
		}
		return Ok && !bHasError;
	}
}
