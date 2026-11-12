// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptStandardLibraryServices.h"

#include <algorithm>

namespace VeroScript::StandardLibrary::Detail
{
	inline constexpr std::size_t WorkBlock = 1024;
	class FWorkMeter
	{
	public:
		explicit FWorkMeter(const FServiceContext& InContext) : Context(InContext)
		{
		}
		FServiceResult Add(std::size_t Units = 1)
		{
			Pending += Units;
			if (Pending < WorkBlock) return {};
			return Flush();
		}
		FServiceResult Flush()
		{
			const auto Units = Pending;
			Pending = 0;
			return Context.Check(Units);
		}

	private:
		const FServiceContext& Context;
		std::uint64_t Pending = 0;
	};

	inline FServiceResult Append(std::string& Output, std::string_view Text, const FServiceContext& Context)
	{
		for (std::size_t I = 0; I < Text.size();)
		{
			const auto Count = std::min(WorkBlock, Text.size() - I);
			if (auto R = Context.Check(Count); !R) return R;
			Output.append(Text.data() + I, Count);
			I += Count;
		}
		return {};
	}

	inline FServiceResult CheckedLength(std::size_t Size, const FServiceContext& Context)
	{
		if (Size > std::string{}.max_size()) return {EServiceStatus::LimitExceeded, 0};
		return Context.CheckText(Size);
	}
}
