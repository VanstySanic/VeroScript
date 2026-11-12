// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "SchemaIdna.h"
#include "SchemaSemantics.h"
#include <fstream>
#include <iostream>
#include <sstream>

using namespace VeroScript::Json;
using namespace VeroScript::Json::Detail;
static std::vector<std::uint32_t> Points(const std::string& Text)
{
	std::istringstream Input(Text);
	std::vector<std::uint32_t> Out;
	std::uint32_t C;
	while (Input >> std::hex >> C)
	{
		Out.push_back(C);
	}
	return Out;
}
int main(int Argc, char** Argv)
{
	try
	{
		if (Argc != 2)
		{
			return 2;
		}
		std::ifstream Input(Argv[1], std::ios::binary);
		if (!Input)
		{
			throw std::runtime_error("Missing Unicode normalization corpus");
		}
		const FLimits Limits;
		FBudget Budget(Limits);
		FSchemaWorkScope Scope(Budget);
		std::string Line;
		std::uint64_t Count = 0, Number = 0;
		while (std::getline(Input, Line))
		{
			++Number;
			if (Line.empty() || Line.front() == '#' || Line.front() == '@')
			{
				continue;
			}
			std::vector<std::vector<std::uint32_t>> Columns;
			std::size_t P = 0;
			for (unsigned I = 0; I < 5; ++I)
			{
				const auto End = Line.find(';', P);
				if (End == std::string::npos)
				{
					throw std::runtime_error("Malformed Unicode fixture");
				}
				Columns.push_back(Points(Line.substr(P, End - P)));
				P = End + 1;
			}
			for (unsigned I = 0; I < 5; ++I)
			{
				if (Formats::Normalize(Columns[I]) != Columns[I < 3 ? 1 : 3])
				{
					throw std::runtime_error("Unicode NFC conformance failed at line " + std::to_string(Number));
				}
			}
			++Count;
		}
		if (Count < 10000)
		{
			throw std::runtime_error("Incomplete Unicode corpus");
		}
		std::cout << "Unicode 17 NFC: " << Count << " rows, " << Count * 5
				  << " independent conformance checks passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
