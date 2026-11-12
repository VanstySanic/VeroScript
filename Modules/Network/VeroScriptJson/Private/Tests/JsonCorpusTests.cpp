// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJson.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int Argc, char** Argv)
{
	if (Argc != 2)
	{
		return 2;
	}
	unsigned Cases = 0, Failures = 0;
	VeroScript::Json::FParseOptions Options;
	// Corpus y_ files include duplicate names; the strict default has separate tests.
	Options.DuplicateKeys = VeroScript::Json::EDuplicateKeys::KeepLast;
	for (const auto& Entry : std::filesystem::directory_iterator(Argv[1]))
	{
		auto Name = Entry.path().filename().string();
		if (!Entry.is_regular_file() || Entry.path().extension() != ".json" || Name.size() < 2 ||
			(Name[0] != 'y' && Name[0] != 'n') || Name[1] != '_')
		{
			continue;
		}
		std::ifstream Input(Entry.path(), std::ios::binary);
		std::string Text{std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
		VeroScript::Json::FError Error;
		VeroScript::Json::FJsonValue Value;
		bool Accepted = VeroScript::Json::TryParse(Text, Value, Error, Options);
		++Cases;
		if (Accepted != (Name[0] == 'y'))
		{
			++Failures;
			std::cerr << Name << ": accepted=" << Accepted << ' ' << Error.Message << '\n';
		}
	}
	std::cout << Cases << " corpus cases, " << Failures << " failures\n";
	return Cases && !Failures ? 0 : 1;
}
