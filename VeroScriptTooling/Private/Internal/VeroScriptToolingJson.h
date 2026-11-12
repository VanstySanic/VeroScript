// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace VeroScript::Tooling::Detail
{
	struct FJson
	{
		using FObject = std::map<std::string, FJson>;
		using FArray = std::vector<FJson>;
		using FValue = std::variant<std::nullptr_t, bool, double, std::string, FObject, FArray>;

		static FJson Null()
		{
			return FJson{nullptr};
		}

		static FJson Bool(const bool V)
		{
			return FJson{V};
		}

		static FJson Number(const double V)
		{
			return FJson{V};
		}

		static FJson String(std::string V)
		{
			return FJson{std::move(V)};
		}

		static FJson Object(FObject V = {})
		{
			return FJson{std::move(V)};
		}

		static FJson Array(FArray V = {})
		{
			return FJson{std::move(V)};
		}

		const FObject* ObjectPtr() const
		{
			return std::get_if<FObject>(&Value);
		}

		const FArray* ArrayPtr() const
		{
			return std::get_if<FArray>(&Value);
		}

		bool IsNull() const
		{
			return std::holds_alternative<std::nullptr_t>(Value);
		}

		const FJson* Find(const std::string& Key) const
		{
			const FObject* Object = ObjectPtr();
			if (!Object)
			{
				return nullptr;
			}
			const auto It = Object->find(Key);
			return It == Object->end() ? nullptr : &It->second;
		}

		std::string GetString(const std::string& Key, const std::string& Default = {}) const
		{
			const FJson* Field = Find(Key);
			if (!Field)
			{
				return Default;
			}
			if (const std::string* Text = std::get_if<std::string>(&Field->Value))
			{
				return *Text;
			}
			return Default;
		}

		bool GetBool(const std::string& Key, const bool Default = false) const
		{
			const FJson* Field = Find(Key);
			if (!Field)
			{
				return Default;
			}
			if (const bool* Flag = std::get_if<bool>(&Field->Value))
			{
				return *Flag;
			}
			return Default;
		}

		int GetInt(const std::string& Key, const int Default = 0) const
		{
			const FJson* Field = Find(Key);
			if (!Field)
			{
				return Default;
			}
			if (const double* Number = std::get_if<double>(&Field->Value))
			{
				if (!std::isfinite(*Number)
					|| std::trunc(*Number) != *Number
					|| *Number < static_cast<double>(std::numeric_limits<int>::min())
					|| *Number > static_cast<double>(std::numeric_limits<int>::max()))
				{
					return Default;
				}
				return static_cast<int>(*Number);
			}
			return Default;
		}

		FValue Value;
	};

	FJson ParseJson(std::string Text);
	std::string ToJsonString(const FJson& Value);
}
