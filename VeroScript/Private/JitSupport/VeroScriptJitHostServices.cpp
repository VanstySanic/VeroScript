// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJitHostServices.h"
#include "VeroScriptJitBackend.h"
#include "VeroScriptRuntime.h"

namespace VeroScript
{
	const FJitHostServices& GetJitHostServices() noexcept
	{
		static const FJitHostServices Services
		{
			GetJitHostTarget(), &Runtime::ConstantToValue, &Runtime::IsPrimitiveValueTypeName,
			&Runtime::ParseDynamicArrayElementType, &Runtime::SplitMapType,
			&Runtime::SplitSetElementType, &Builtins::LookupById,
		};
		return Services;
	}
}
