// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNative.h"
#include "VeroScriptNumerics.h"

namespace VeroScript
{
	class INumericCallServices
	{
	  public:
		virtual ~INumericCallServices() = default;
		virtual FNumericOperationContext GetNumericOperationContext() = 0;
	};
	namespace Numerics
	{
		enum class EArgumentRole : std::uint8_t
		{
			Ordinary,
			Integer,
			Floating
		};
		const IHostBindingCatalog& GetBindingCatalog();
		bool IsTypeName(std::string_view) noexcept;
		bool IsValue(const FValue&) noexcept;
		bool ToScriptValue(const FBigInteger&, FValue&, FNumericError&) noexcept;
		bool ToScriptValue(const FRational&, FValue&, FNumericError&) noexcept;
		bool FromScriptValue(const FValue&, FBigInteger&, FNumericError&) noexcept;
		bool FromScriptValue(const FValue&, FRational&, FNumericError&) noexcept;
		EArgumentRole ArgumentRole(const FNativeFunction&, std::size_t) noexcept;
		bool ValidateArgumentType(const FNativeFunction&, std::size_t, std::string_view) noexcept;
		bool ValidateArgument(const FNativeFunction&, std::size_t, const FValue&, std::string& Error);
		bool FormatValue(const FValue&, std::string&, FNumericOperationContext&, FNumericError&) noexcept;
		bool ConvertValue(const FValue&, std::string_view Target, FValue&, FNumericOperationContext&,
						  FNumericError&) noexcept;
		bool UsesRuntimeNumerics(const FBytecodeModule&, const std::vector<FNativeFunction>* = nullptr);
	} // namespace Numerics
} // namespace VeroScript
