// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNative.h"

namespace VeroScript::Runtime
{
	struct FHostValueServices
	{
		IMemoryCallServices *Memory = nullptr;
		INumericCallServices *Numeric = nullptr;
		I18n::II18nCallServices *I18n = nullptr;
		IScriptCallInvoker *Invoker = nullptr;
	};

	inline thread_local const FHostValueServices *ActiveHostValueServices = nullptr;

	// Value callbacks borrow the active VM services only for this copy operation.
	// Nested/reentrant copies restore the caller's provider on every exit path.
	class FHostValueServicesScope
	{
	  public:
		FHostValueServicesScope(IMemoryCallServices *Memory, INumericCallServices *Numeric,
								I18n::II18nCallServices *I18n, IScriptCallInvoker *Invoker)
			: Services{Memory, Numeric, I18n, Invoker}, Previous(ActiveHostValueServices)
		{
			ActiveHostValueServices = &Services;
		}

		~FHostValueServicesScope()
		{
			ActiveHostValueServices = Previous;
		}

		FHostValueServicesScope(const FHostValueServicesScope &) = delete;
		FHostValueServicesScope &operator=(const FHostValueServicesScope &) = delete;

	  private:
		FHostValueServices Services;
		const FHostValueServices *Previous;
	};

	inline void InheritHostValueServices(FNativeCallContext &Context)
	{
		if (!ActiveHostValueServices)
		{
			return;
		}
		if (!Context.MemoryServices)
		{
			Context.MemoryServices = ActiveHostValueServices->Memory;
		}
		if (!Context.NumericServices)
		{
			Context.NumericServices = ActiveHostValueServices->Numeric;
		}
		if (!Context.I18nServices)
		{
			Context.I18nServices = ActiveHostValueServices->I18n;
		}
		if (!Context.ScriptInvoker)
		{
			Context.ScriptInvoker = ActiveHostValueServices->Invoker;
		}
	}
} // namespace VeroScript::Runtime
