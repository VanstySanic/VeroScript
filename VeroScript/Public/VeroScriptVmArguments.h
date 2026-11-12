// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptValue.h"

#include <cstddef>
#include <new>
#include <vector>

namespace VeroScript::VmDetail
{
	// Construct only live argument slots. The caller retains language-level
	// release responsibility; this owner performs the final C++ destruction.
	class FArgumentBuffer
	{
	public:
		// Keep lifetime code out of the large interpreter dispatch function.
		explicit FArgumentBuffer(std::size_t Count);
		~FArgumentBuffer();
		FArgumentBuffer(const FArgumentBuffer&) = delete;
		FArgumentBuffer& operator=(const FArgumentBuffer&) = delete;
		FValue* data() noexcept
		{
			return Values;
		}
		std::size_t size() const noexcept
		{
			return CountValue;
		}
		FValue& front() noexcept
		{
			return *Values;
		}
		const FValue& front() const noexcept
		{
			return *Values;
		}
		FValue& operator[](const std::size_t Index) noexcept
		{
			return Values[Index];
		}
		const FValue& operator[](const std::size_t Index) const noexcept
		{
			return Values[Index];
		}

		static constexpr std::size_t InlineCapacity = 8;

	private:
		alignas(FValue) unsigned char Inline[InlineCapacity * sizeof(FValue)];
		std::vector<FValue> Heap;
		FValue* Values = nullptr;
		std::size_t CountValue = 0;
	};
}

namespace VeroScript::VmDetail
{
	inline FArgumentBuffer::FArgumentBuffer(const std::size_t Count) : CountValue(Count)
	{
		if (Count > InlineCapacity)
		{
			Heap.resize(Count);
			Values = Heap.data();
		}
		else
		{
			// Start an array lifetime so all contiguous argument accesses stay within it.
			Values = Count == 0 ? nullptr : new (Inline) FValue[Count];
		}
	}

	inline FArgumentBuffer::~FArgumentBuffer()
	{
		if (CountValue <= InlineCapacity)
		{
			for (std::size_t Index = CountValue; Index != 0; --Index)
			{
				Values[Index - 1].~FValue();
			}
		}
	}
}
