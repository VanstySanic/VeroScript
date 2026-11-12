// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptValue.h"

namespace VeroScript
{
	struct FBytecodeModule;
	struct FProgram;
	struct FNativeFunction;
	struct FVariantSourceStamp;
	/** A type validated by the current VM/session; names alone are not credentials. */
	class FVariantTypeDescriptor final
	{
		friend class VeroScriptVm;
		friend struct FVariantAccess;
		std::string Name;
		std::shared_ptr<const FVariantSourceStamp> Source;
	};

	namespace Variant
	{
		FValue Empty() noexcept;
		bool IsVariant(const FValue& Value) noexcept;
		bool HasValue(const FValue& Value) noexcept;
		std::string_view TypeName(const FValue& Value) noexcept;
		bool Is(const FValue& Value, std::string_view CanonicalType) noexcept;
		bool Validate(const FValue& Value, std::string& Error);
		bool ValidateSource(const FValue& Value, const std::shared_ptr<const void>& Owner,
			std::uint64_t Generation, std::string& Error);
		/** Type must have been obtained from semantic validation in the owning session.
		 * Output is preserved on failure. Value types are snapshotted exactly once. */
		bool Box(const FVariantTypeDescriptor& Type, const FValue& Value, FValue& Output, std::string& Error);
		/** Copies according to the payload's language value/reference semantics.
		 * Does not expose writable storage. Retain module-dependent results with a value root. */
		bool Snapshot(const FValue& Value, FValue& Output, std::string& Error);
		bool ContainsType(std::string_view TypeName) noexcept;
		bool ContainsType(const FBytecodeModule& Module, std::string_view TypeName);
		bool UsesRuntime(const FBytecodeModule& Module, const std::vector<FNativeFunction>* Natives = nullptr);
		bool UsesRuntime(const FProgram& Program);
	}
}
