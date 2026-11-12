// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJson.h"
#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>

namespace VeroScript::Json::Detail
{
	struct FDecimal
	{
		bool bNegative = false;
		std::string Digits;
		std::int64_t Exponent = 0;
	};

	struct FNode
	{
		EKind Kind = EKind::Null;
		bool Boolean = false;
		std::string Text;
		FDecimal Decimal;
		std::vector<FJsonValue> Array;
		std::vector<std::pair<std::string, FJsonValue>> Object;
		std::shared_ptr<const std::unordered_map<std::string, std::size_t>> ObjectIndex;
		std::uint64_t Depth = 1;
	};

	struct FAccess
	{
		static const FNode& Get(const FJsonValue& Value);
		static FJsonValue Make(FNode Node);
	};

	struct FFailure
	{
		EError Code;
		const char* Message;
	};

	[[noreturn]] void Fail(EError Code, const char* Message);
	void SetError(FError& Error, EError Code, const char* Message) noexcept;
	void CopyError(FError& Out, const FError& Error) noexcept;
	bool CheckWork(FWork Work, std::uint64_t Units);
	void ValidateUtf8(std::string_view Text);
	std::string Quote(std::string_view Text);
	FDecimal Decimal(std::string_view Text, const FLimits& Limits);
	int Compare(const FDecimal& A, const FDecimal& B);
	std::string DoubleText(double Value);
	std::string ExactDoubleText(double Value);
	std::uint64_t TreeDepth(const FNode& Node);
	std::size_t FindMember(const FNode& Node, std::string_view Key);

	class FBudget
	{
	public:
		explicit FBudget(const FLimits& InLimits, FWork InWork = {});
		void Check(std::uint64_t Units = 1);
		const FLimits& Limits;
		FWork Work;
		std::uint64_t Used = 0;
	};

	template <class F>
	bool Guard(FError& Error, F&& Function)
	{
		Error = {};
		try
		{
			Function();
			return true;
		}
		catch (const FFailure& Failure)
		{
			SetError(Error, Failure.Code, Failure.Message);
		}
		catch (const std::bad_alloc&)
		{
			SetError(Error, EError::Allocation, "Allocation failed");
		}
		catch (const std::length_error&)
		{
			SetError(Error, EError::Limit, "Container capacity exceeded");
		}
		return false;
	}

	bool Equal(const FJsonValue& A, const FJsonValue& B, FBudget& Budget);
	void Write(const FJsonValue& Value, std::string& Out, const FWriteOptions& Options, FBudget& Budget,
		std::uint64_t Depth = 0);
} // namespace VeroScript::Json::Detail
