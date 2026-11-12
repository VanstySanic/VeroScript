// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJsonSchema.h"
#include <jsoncons/utility/uri.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <algorithm>
#include <cstdlib>

using namespace VeroScript::Json;

static FJsonValue Read(const std::filesystem::path& Path)
{
	std::ifstream File(Path, std::ios::binary);
	if (!File)
	{
		throw std::runtime_error("Missing schema fixture: " + Path.generic_string());
	}
	std::string Text{std::istreambuf_iterator<char>(File), {}};
	FJsonValue Out;
	FError Error;
	if (!TryParse(Text, Out, Error))
	{
		throw std::runtime_error(Error.Message);
	}
	return Out;
}

static FJsonValue Get(const FJsonValue& Object, const char* Name)
{
	FJsonValue Out;
	FError Error;
	if (!Object.TryGet(Name, Out, Error))
	{
		throw std::runtime_error(Error.Message);
	}
	return Out;
}

static FJsonValue At(const FJsonValue& Array, std::int64_t Index)
{
	FJsonValue Out;
	FError Error;
	if (!Array.TryAt(Index, Out, Error))
	{
		throw std::runtime_error(Error.Message);
	}
	return Out;
}

static std::int64_t Size(const FJsonValue& Value)
{
	std::int64_t Out = 0;
	FError Error;
	if (!Value.TrySize(Out, Error))
	{
		throw std::runtime_error(Error.Message);
	}
	return Out;
}

int main(int Argc, char** Argv)
{
	try
	{
		if (Argc != 2)
		{
			throw std::runtime_error("Expected the JSON Schema Test Suite root directory");
		}
		const auto Root = std::filesystem::absolute(Argv[1]);
		std::vector<std::filesystem::path> Files;
		for (const auto& File : std::filesystem::directory_iterator(Root / "tests/draft2020-12"))
		{
			if (File.is_regular_file() && File.path().extension() == ".json")
			{
				Files.push_back(File.path());
			}
		}
		std::sort(Files.begin(), Files.end());
		if (Files.empty())
		{
			throw std::runtime_error("Schema corpus contains no required fixtures");
		}
		FSchemaResolver Resolver = [&](std::string_view Uri, FJsonValue& Out, FError&) -> bool
		{
			constexpr std::string_view Prefix = "http://localhost:1234/";
			if (Uri.substr(0, Prefix.size()) != Prefix)
			{
				throw std::runtime_error("Unknown fixture URI");
			}
			const auto Path = jsoncons::uri(Uri).path();
			if (Path.empty() || Path.front() != '/')
			{
				throw std::runtime_error("Invalid fixture path");
			}
			const auto Relative = std::filesystem::path(Path.substr(1)).lexically_normal();
			if (Relative.is_absolute() || Relative.empty())
			{
				throw std::runtime_error("Invalid fixture path");
			}
			for (const auto& Part : Relative)
			{
				if (Part == "..")
				{
					throw std::runtime_error("Invalid fixture path");
				}
			}
			Out = Read(Root / "remotes" / Relative);
			return true;
		};
		std::uint64_t Passed = 0, Failed = 0;
		for (const auto& File : Files)
		{
			const auto Groups = Read(File);
			for (std::int64_t I = 0; I < Size(Groups); ++I)
			{
				const auto Group = At(Groups, I);
				const auto Tests = Get(Group, "tests");
				FSchema Schema;
				FError Error;
				FSchemaOptions Options;
				Options.RetrievalUri = "https://jsoncons.com";
				if (!FSchema::TryCompile(Get(Group, "schema"), Schema, Error, Options, {}, Resolver))
				{
					Failed += static_cast<std::uint64_t>(Size(Tests));
					std::cout << File.filename().generic_string() << " group " << I << ": " << Error.Id() << ' '
							  << Error.Message << '\n';
					continue;
				}
				for (std::int64_t T = 0; T < Size(Tests); ++T)
				{
					const auto Test = At(Tests, T);
					if (std::getenv("VEROSCRIPT_JSON_SCHEMA_TRACE"))
					{
						std::cout << File.filename().generic_string() << " group " << I << " test " << T << std::endl;
					}
					bool Expected = false, Actual = false;
					if (!Get(Test, "valid").TryBoolean(Expected, Error))
					{
						throw std::runtime_error(Error.Message);
					}
					FSchemaReport Report;
					if (Schema.TryIsValid(Get(Test, "data"), Actual, Error) && Actual == Expected &&
						Schema.TryValidate(Get(Test, "data"), Report, Error) && Report.bValid == Expected)
					{
						++Passed;
					}
					else
					{
						++Failed;
						std::cout << File.filename().generic_string() << " group " << I << " test " << T << ": "
								  << Error.Id() << ' ' << Error.Message << '\n';
					}
				}
			}
		}
		std::cout << "Public Schema corpus: " << Passed << " passed, " << Failed << " failed\n";
		return Failed ? 1 : 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 2;
	}
}
