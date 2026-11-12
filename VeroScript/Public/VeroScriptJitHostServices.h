// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBuiltins.h"
#include "VeroScriptValue.h"

#include <string>
#include <string_view>

namespace VeroScript
{
	/**
	 * Immutable language helpers supplied by the owning core. A backend binds this
	 * table to its factory; it must not link a second copy of the script runtime.
	 * The table and its implementation remain alive for all backend-owned objects.
	 */
	struct FJitHostServices
	{
		std::string_view HostTarget;
		FValue (*ConstantToValue)(const FBytecodeModule&, const FBytecodeConstant&) = nullptr;
		bool (*IsPrimitiveValueTypeName)(std::string_view) = nullptr;
		bool (*ParseDynamicArrayElementType)(std::string_view, std::string&) = nullptr;
		bool (*SplitMapType)(std::string_view, std::string&, std::string&) = nullptr;
		bool (*SplitSetElementType)(std::string_view, std::string&) = nullptr;
		const Builtins::FDescriptor* (*LookupBuiltin)(EBuiltinId) = nullptr;
	};

	const FJitHostServices& GetJitHostServices() noexcept;
}
