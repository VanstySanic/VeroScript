// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#if defined(VEROSCRIPT_JSON_ADAPTED_SCHEMA)
#include "SchemaBackend.h"
#else
#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonschema/jsonschema.hpp>
#endif
#include <filesystem>
#include <fstream>
#include <iostream>

#if defined(VEROSCRIPT_JSON_ADAPTED_SCHEMA)
// Exercise the same sorted backend representation as the public Schema API.
using J = jsoncons::json;
#else
using J = jsoncons::ojson;
#endif

static J Read(const std::filesystem::path& Path)
{
	std::ifstream Input(Path, std::ios::binary);
	if (!Input)
	{
		throw std::runtime_error("Missing schema fixture: " + Path.generic_string());
	}
	return J::parse(Input, jsoncons::json_options{}.lossless_number(true).max_nesting_depth(256));
}

int main(int Argc, char** Argv)
{
	if (Argc < 3)
	{
		std::cerr << "Usage: backend-corpus-qualification <test-file> <remotes-directory> [--format]\n";
		return 2;
	}
	unsigned Passed = 0, Failed = 0;
	try
	{
		const auto Root = std::filesystem::absolute(Argv[2]).lexically_normal();
		const auto Cases = Read(Argv[1]);
		const auto Options = jsoncons::jsonschema::evaluation_options{}.require_format_validation(Argc > 3);
		auto Resolver = [&](const jsoncons::uri& Uri) -> J
		{
			const std::string Prefix = "http://localhost:1234/";
			const auto Text = Uri.string();
			if (Text.compare(0, Prefix.size(), Prefix) != 0)
			{
				throw std::runtime_error("Unknown schema fixture URI: " + Text);
			}
			const auto UriPath = Uri.path();
			if (UriPath.empty() || UriPath.front() != '/')
			{
				throw std::runtime_error("Invalid schema fixture path");
			}
			const auto Relative =
				std::filesystem::path(std::string(UriPath.data() + 1, UriPath.size() - 1)).lexically_normal();
			if (Relative.empty() || Relative.is_absolute())
			{
				throw std::runtime_error("Invalid schema fixture URI");
			}
			for (const auto& Part : Relative)
			{
				if (Part == "..")
				{
					throw std::runtime_error("Invalid schema fixture URI");
				}
			}
			return Read(Root / Relative);
		};
		for (const auto& Group : Cases.array_range())
		{
			try
			{
				#if defined(VEROSCRIPT_JSON_ADAPTED_SCHEMA)
				const VeroScript::Json::FLimits Limits;
				VeroScript::Json::Detail::FBudget Budget(Limits);
				VeroScript::Json::Detail::FSchemaWorkScope Scope(Budget);
				#endif
				auto Schema = jsoncons::jsonschema::make_json_schema(Group.at("schema"), Resolver, Options);
				for (const auto& Test : Group.at("tests").array_range())
				{
					try
					{
						const bool Valid = Schema.is_valid(Test.at("data"));
						if (Valid == Test.at("valid").as<bool>())
						{
							++Passed;
						}
						else
						{
							++Failed;
							std::cout << "FAIL " << Group.at("description").as<std::string>() << " / "
									  << Test.at("description").as<std::string>() << '\n';
						}
					}
					catch (const std::exception& Error)
					{
						++Failed;
						std::cout << "EXCEPTION " << Group.at("description").as<std::string>() << " / "
								  << Test.at("description").as<std::string>() << ": " << Error.what() << '\n';
					}
					catch (...)
					{
						++Failed;
						std::cout << "STOPPED " << Group.at("description").as<std::string>() << " / "
								  << Test.at("description").as<std::string>() << '\n';
					}
				}
			}
			catch (const std::exception& Error)
			{
				Failed += static_cast<unsigned>(Group.at("tests").size());
				std::cout << "EXCEPTION " << Group.at("description").as<std::string>() << ": " << Error.what() << '\n';
			}
			catch (...)
			{
				Failed += static_cast<unsigned>(Group.at("tests").size());
				std::cout << "STOPPED " << Group.at("description").as<std::string>() << '\n';
			}
		}
		std::cout << "RESULT " << Passed << ' ' << Failed << '\n';
		return Failed ? 1 : 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 2;
	}
}
