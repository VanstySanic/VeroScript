// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "SchemaArithmetic.h"
#include <iostream>

using namespace VeroScript::Json;
using namespace VeroScript::Json::Detail;

static void Require(bool Value)
{
	if (!Value)
	{
		throw std::runtime_error("Exact Schema arithmetic regression");
	}
}

int main()
{
	try
	{
		const FLimits Limits;
		FBudget Budget(Limits);
		auto Multiple = [&](const std::string& A, const std::string& B)
		{
			return IsMultiple(Decimal(A, Limits), Decimal(B, Limits), Budget);
		};
		Require(Multiple("0", "1e-100000"));
		Require(Multiple("1e100000", "0.00000001"));
		Require(!Multiple("1e100000", "3"));
		Require(Multiple("1.2", "0.03"));
		Require(Multiple("-0.25", "0.05"));
		Require(!Multiple("0.25", "0.2"));
		Require(!Multiple("9007199254740993", "9007199254740992"));
		Require(Multiple("1.2345678901234567890123456789", "0.0000000000000000000000000001"));
		Require(Multiple("1", "0.00000000000000000000000000005"));
		Require(!Multiple("1", "0.00000000000000000000000000003"));
		// Independent small-integer oracle: A*10^-AScale divided by B*10^-BScale.
		constexpr std::int64_t Powers[] = {1, 10, 100, 1000};
		for (int A = -32; A <= 32; ++A)
		{
			for (int B = 1; B <= 32; ++B)
			{
				for (unsigned AScale = 0; AScale < 4; ++AScale)
				{
					for (unsigned BScale = 0; BScale < 4; ++BScale)
					{
						const bool Expected = (A * Powers[BScale]) % (B * Powers[AScale]) == 0;
						Require(Multiple(std::to_string(A) + "e-" + std::to_string(AScale),
									std::to_string(B) + "e-" + std::to_string(BScale)) == Expected);
					}
				}
			}
		}
		Require(IsInteger(Decimal("0.0", Limits)) && IsInteger(Decimal("1.00e3", Limits)));
		Require(!IsInteger(Decimal("1.001", Limits)));
		FError Error;
		Require(!Guard(Error,
					[&]
					{
						Multiple("1", "0");
					}) &&
				Error.Code == EError::Range);
		FLimits Small = Limits;
		Small.WorkUnits = 1;
		FBudget Limited(Small);
		Require(!Guard(Error,
					[&]
					{
						IsMultiple(Decimal("1e3", Limits), Decimal("125", Limits), Limited);
					}) &&
				Error.Code == EError::Limit);
		FWork Cancel;
		Cancel.Check = [](void*, std::uint64_t)
		{
			return false;
		};
		FBudget Cancelled(Limits, Cancel);
		Require(!Guard(Error,
					[&]
					{
						IsMultiple(Decimal("1", Limits), Decimal("2", Limits), Cancelled);
					}) &&
				Error.Code == EError::Cancelled);
		std::cout << "Exact Schema arithmetic and bounded work tests passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
