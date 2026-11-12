// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"
#include "VeroScriptNative.h"
#include <initializer_list>

namespace VeroScript::NativeBindingCache
{
	// Cache metadata only; opaque host resources remain owned by their catalog's
	// callback leases. Oversized tables still execute without Session retention.
	inline constexpr std::size_t MaxRetainedBytes = 4u * 1024u * 1024u;

	class FBudget
	{
	public:
		void Add(std::size_t Count, std::size_t Width = 1u) noexcept
		{
			if (Bytes > MaxRetainedBytes || Count > (MaxRetainedBytes - Bytes) / Width)
			{
				Bytes = MaxRetainedBytes + 1u;
				return;
			}
			Bytes += Count * Width;
		}
		void String(const std::string& Value) noexcept { Add(Value.capacity()); }
		template <typename T>
		void Storage(const std::vector<T>& Values) noexcept { Add(Values.capacity(), sizeof(T)); }
		void Strings(const std::vector<std::string>& Values) noexcept
		{
			Storage(Values);
			for (const auto& Value : Values)
			{
				if (Bytes > MaxRetainedBytes) return;
				String(Value);
			}
		}
		void Type(const FNativeTypeDesc& Value, unsigned Depth = 0) noexcept
		{
			if (Bytes > MaxRetainedBytes) return;
			if (Depth > 64u)
			{
				Bytes = MaxRetainedBytes + 1u;
				return;
			}
			String(Value.HostTypeName);
			String(Value.ElementTypeName);
			String(Value.KeyTypeName);
			String(Value.ValueTypeName);
			Storage(Value.TypeArguments);
			for (const auto& Argument : Value.TypeArguments)
			{
				if (Bytes > MaxRetainedBytes) return;
				Type(Argument, Depth + 1u);
			}
		}
		void Functions(const std::vector<FNativeFunction>& Values) noexcept
		{
			Storage(Values);
			for (const auto& Value : Values)
			{
				if (Bytes > MaxRetainedBytes) return;
				for (const auto *Text : {&Value.Origin.ProviderId, &Value.Origin.ModulePath, &Value.Name,
						 &Value.HostTypeName, &Value.ReturnHostTypeName, &Value.ReturnElementTypeName,
						 &Value.ReturnKeyTypeName, &Value.ReturnValueTypeName, &Value.RuntimeReturnTypeName,
						 &Value.ScriptReturnTypeName})
				{
					String(*Text);
				}
				for (const auto *Texts : {&Value.ReturnGenericTypeArguments, &Value.ParamHostTypeNames,
						 &Value.ParamElementTypeNames, &Value.ParamKeyTypeNames, &Value.ParamValueTypeNames,
						 &Value.RuntimeParamTypeNames, &Value.TypeParams})
				{
					Strings(*Texts);
				}
				Storage(Value.ParamTypes);
				Storage(Value.ParamFlags);
				Type(Value.StructuredReturnType);
				Storage(Value.StructuredParamTypes);
				for (const auto &Param : Value.StructuredParamTypes)
				{
					Type(Param);
				}
			}
		}
		void References(const std::vector<FBytecodeNativeReference>& Values) noexcept
		{
			Storage(Values);
			for (const auto& Value : Values)
			{
				if (Bytes > MaxRetainedBytes) return;
				String(Value.Name);
				String(Value.Signature);
				String(Value.ProviderId);
			}
		}
		std::size_t Bytes = 0;
	};

	inline std::size_t RetainedBytes(const std::vector<FNativeFunction>& Standard,
	    const std::vector<FNativeFunction>& Bound, const std::vector<FNativeFunction>& Source,
	    const std::vector<FBytecodeNativeReference>& References) noexcept
	{
		FBudget Budget;
		Budget.Functions(Standard);
		Budget.Functions(Bound);
		Budget.Functions(Source);
		Budget.References(References);
		return Budget.Bytes;
	}
}
