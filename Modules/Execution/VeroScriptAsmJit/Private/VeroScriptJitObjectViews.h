// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptValue.h"

#include <cstddef>
#include <cstdint>

namespace VeroScript::AsmJit::JitLinear
{
	// Array elements retain their authoritative contiguous FValue owners. Only
	// their resolved type stamp varies per element; the range owns the source view.
	struct FTypedLoopNativeObjectView
	{
		std::uint16_t TypeIndex = 0xFFFFu;
	};
	static_assert(alignof(FTypedLoopNativeObjectView) >= 2);
	static_assert(sizeof(FTypedLoopNativeObjectView) == sizeof(std::uint16_t));

	struct FTypedLoopNativeObjectViewRange
	{
		FTypedLoopNativeObjectView* Data = nullptr;
		std::size_t Size = 0;
		FValue* SourceValues = nullptr;
	};

	// Frame bindings are aligned and untagged; borrowed strings use the high bit.
	// The low bit is used only for invocation-local object-array element views.
	inline constexpr std::uint64_t TypedLoopNativeObjectViewTag = 1u;
	inline bool IsTypedLoopNativeObjectView(const std::uint64_t Token) noexcept
	{
		return (Token & TypedLoopNativeObjectViewTag) != 0u;
	}
	inline std::uint64_t MakeTypedLoopNativeObjectViewToken(
		const FTypedLoopNativeObjectView* View) noexcept
	{
		return reinterpret_cast<std::uintptr_t>(View) | TypedLoopNativeObjectViewTag;
	}
	inline FTypedLoopNativeObjectView* FindTypedLoopNativeObjectView(
		const std::uint64_t Token, const FTypedLoopNativeObjectViewRange* Ranges,
		const std::size_t RangeCount, FValue*& SourceValue) noexcept
	{
		SourceValue = nullptr;
		if (!IsTypedLoopNativeObjectView(Token) || !Ranges) return nullptr;
		const auto Address = static_cast<std::uintptr_t>(Token & ~TypedLoopNativeObjectViewTag);
		for (std::size_t Index = 0; Index < RangeCount; ++Index)
		{
			if (!Ranges[Index].Data || !Ranges[Index].SourceValues || Ranges[Index].Size == 0u)
			{
				continue;
			}
			const auto Begin = reinterpret_cast<std::uintptr_t>(Ranges[Index].Data);
			if (Address < Begin)
			{
				continue;
			}
			const auto Offset = Address - Begin;
			if (Offset % sizeof(FTypedLoopNativeObjectView) == 0u &&
				Offset / sizeof(FTypedLoopNativeObjectView) < Ranges[Index].Size)
			{
				const auto Element = Offset / sizeof(FTypedLoopNativeObjectView);
				SourceValue = Ranges[Index].SourceValues + Element;
				return Ranges[Index].Data + Element;
			}
		}
		return nullptr;
	}

	inline FTypedLoopNativeObjectView* FindTypedLoopNativeObjectViewAt(
		const FTypedLoopNativeObjectView* Begin, const std::size_t Element,
		const FTypedLoopNativeObjectViewRange* Ranges, const std::size_t RangeCount) noexcept
	{
		if (!Begin || !Ranges) return nullptr;
		for (std::size_t Index = 0; Index < RangeCount; ++Index)
		{
			const auto& Range = Ranges[Index];
			if (Range.Data == Begin && Range.SourceValues && Element < Range.Size)
			{
				return Range.Data + Element;
			}
		}
		return nullptr;
	}
}
