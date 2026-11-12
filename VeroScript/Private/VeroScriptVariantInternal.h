// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptVariant.h"

namespace VeroScript
{
	struct FVariantSourceStamp final
	{
		std::shared_ptr<const void> ProgramOwner;
		std::uint64_t Generation = 0;
	};

	// Immutable to script and host callers. Only final graph cleanup and private
	// hot-reload construction may move/reset Value to close or detach graph edges.
	struct FScriptVariantStorage final
	{
		std::string CanonicalTypeName;
		std::shared_ptr<const FVariantSourceStamp> Source;
		FValue Value;
		// Captured while semantic metadata is alive; never inspect RuntimeTypeModule
		// from a context-free snapshot after the owning module may have expired.
		bool bCopyRequiresVm = false;
	};
	namespace Variant
	{
		bool CopyRequiresVm(const FBytecodeModule& Module, const FValue& Value);
	}
	struct FVariantAccess
	{
		static const std::string& Name(const FVariantTypeDescriptor& Type)
		{
			return Type.Name;
		}
		static const auto& Source(const FVariantTypeDescriptor& Type)
		{
			return Type.Source;
		}
	};
}
