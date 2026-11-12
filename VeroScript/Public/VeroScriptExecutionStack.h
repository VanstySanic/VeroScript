// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptValue.h"
#include <utility>

namespace VeroScript::ExecutionDetail
{
	template <typename T, typename TError>
	void Push(std::vector<FValue>& Stack, std::size_t Maximum, T&& Value, TError&& Error)
	{
		if (Stack.size() >= Maximum)
		{
			Error("operand stack overflow");
			return;
		}
		Stack.emplace_back(std::forward<T>(Value));
	}
	template <typename TFrames, typename TError>
	FValue Pop(std::vector<FValue>& Stack, const TFrames& Frames, TError&& Error)
	{
		const auto Base = Frames.empty() ? 0 : Frames.back().StackBase;
		if (Stack.size() <= Base)
		{
			Error("operand stack underflow");
			return std::int32_t{0};
		}
		FValue Value = std::move(Stack.back());
		Stack.pop_back();
		return Value;
	}
	template <typename TFrames>
	void PopFrame(TFrames& Frames, std::size_t& BackendCredits) noexcept
	{
		if (Frames.back().JitRuntimeFrameIdentity != 0)
		{
			--BackendCredits;
		}
		Frames.pop_back();
	}
}
