// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHostBindingCatalog.h"

#include <iostream>
#include <string>

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::cerr << "expected generated catalog path\n";
		return 2;
	}
	VeroScript::FHostBindingRegistry Registry;
	std::string Error;
	if (!VeroScript::LoadBindingCatalogFile(argv[1], Registry, Error))
	{
		std::cerr << "generated C# catalog failed core import: " << Error << '\n';
		return 1;
	}
	const VeroScript::FHostTypeDesc* Counter = Registry.FindType("Demo::Counter");
	const VeroScript::FHostTypeDesc* Mode = Registry.FindType("Demo::Mode");
	if (!Counter || !Mode || !Mode->bScopedEnum
		|| Mode->EnumUnderlyingTypeName != "unsigned char")
	{
		std::cerr << "generated C# catalog lost type or enum metadata\n";
		return 1;
	}
	bool FoundReadonlyRef = false;
	bool LeakedPrivateSetter = false;
	for (const VeroScript::FHostMemberDesc& Member : Counter->Members)
	{
		FoundReadonlyRef = FoundReadonlyRef || (Member.Name == "Add"
			&& Member.ParamFlags.size() == 1 && Member.ParamFlags[0] == 3u);
		LeakedPrivateSetter = LeakedPrivateSetter || (Member.Name == "ReadOnlyOutside"
			&& Member.Kind == VeroScript::EHostMemberKind::PropertySetter);
	}
	if (!FoundReadonlyRef || LeakedPrivateSetter)
	{
		std::cerr << "generated C# catalog lost ref flags or leaked a private setter\n";
		return 1;
	}
	return 0;
}
