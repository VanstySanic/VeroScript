// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "NetworkBindingSupport.h"
#include <type_traits>

namespace VeroScript::NetworkBinding
{
	template <class T>
	bool MakeTask(
		FState& S, const char* Name, FHostObjectHandle Owner, std::shared_ptr<T> Task, FValue& Out, std::string& Error)
	{
		FOperationAdmission Guard{Task->Operation(), Context(Owner)};
		Guard.Committed = Make(S, Name, FHandle<T>{Owner, std::move(Task)}, Out, Error);
		return Guard.Committed;
	}
	template <class T, class F>
	bool StartTask(FState& S, const char* Name, FHostObjectHandle Owner, F&& Function, FValue& Out, std::string& Error)
	{
		static_assert(std::is_nothrow_move_assignable_v<T>, "Started tasks must move without allocating");
		// Allocation must precede the native action, which can already modify external state.
		auto Task = std::make_shared<T>();
		*Task = Function();
		return MakeTask(S, Name, std::move(Owner), std::move(Task), Out, Error);
	}
} // namespace VeroScript::NetworkBinding
