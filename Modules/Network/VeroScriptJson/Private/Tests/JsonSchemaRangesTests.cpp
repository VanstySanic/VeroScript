// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "SchemaSemantics.h"
#define VEROSCRIPT_JSON_SCHEMA_POLICY ::VeroScript::Json::Detail::FSchemaSemantics
#include <jsoncons_ext/jsonschema/common/validator.hpp>
#undef VEROSCRIPT_JSON_SCHEMA_POLICY
#include <array>
#include <iostream>

using namespace VeroScript::Json;
using namespace VeroScript::Json::Detail;
using jsoncons::jsonschema::range;
using jsoncons::jsonschema::range_collection;
static void Require(bool Value)
{
	if (!Value)
	{
		throw std::runtime_error("Schema evaluated-range regression");
	}
}
int main()
{
	try
	{
		const FLimits Limits;
		FBudget Budget(Limits);
		FSchemaWorkScope Work(Budget);
		std::uint32_t Seed = 0x6e63728d;
		auto Random = [&]
		{
			Seed ^= Seed << 13;
			Seed ^= Seed >> 17;
			Seed ^= Seed << 5;
			return Seed;
		};
		for (unsigned Case = 0; Case < 128; ++Case)
		{
			range_collection Ranges;
			std::array<bool, 129> Oracle{};
			for (unsigned Insert = 0; Insert < 32; ++Insert)
			{
				auto A = Random() % 129, B = Random() % 129;
				if (A > B)
				{
					std::swap(A, B);
				}
				Ranges.insert(range(A, B));
				for (auto I = A; I < B; ++I)
				{
					Oracle[I] = true;
				}
				for (std::size_t I = 0; I < Oracle.size(); ++I)
				{
					Require(Ranges.contains(I) == Oracle[I]);
				}
				std::size_t Previous = 0;
				bool First = true;
				for (const auto& R : Ranges)
				{
					Require(R.start() < R.end() && (First || Previous < R.start()));
					Previous = R.end();
					First = false;
				}
			}
			const auto Copy = Ranges;
			for (std::size_t I = 0; I < Oracle.size(); ++I)
			{
				Require(Copy.contains(I) == Oracle[I]);
			}
		}
		range_collection Sparse;
		for (std::size_t I = 0; I < 32768; I += 2)
		{
			Sparse.insert(range(I, I + 1));
		}
		const auto Used = Budget.Used;
		for (std::size_t I = 0; I < 32768; ++I)
		{
			Require(Sparse.contains(I) == (I % 2 == 0));
		}
		Require(Budget.Used - Used < 600000); // A full range scan would require over 268 million comparisons.
		Sparse.insert(range(0, 32768));
		Require(Sparse.size() == 1 && Sparse.contains(32767) && !Sparse.contains(32768));
		Sparse.insert(range(SIZE_MAX - 1, SIZE_MAX));
		Require(Sparse.contains(SIZE_MAX - 1) && !Sparse.contains(SIZE_MAX));
		FError Error;
		const auto Count = Sparse.size();
		FBudget Cancelled(Limits,
			{[](void*, std::uint64_t)
				{
					return false;
				},
				nullptr});
		{
			FSchemaWorkScope CancelScope(Cancelled);
			Require(!Guard(Error,
						[&]
						{
							Sparse.insert(range(1, SIZE_MAX));
						}) &&
					Error.Code == EError::Cancelled);
		}
		Require(Sparse.size() == Count && !Sparse.contains(40000));
		std::cout << "Evaluated ranges: independent bitmap oracle, sparse lookup budget and cancellation passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
