// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageOperation.h"
namespace VeroScript::Packages::Detail
{
	namespace
	{
		thread_local std::set<std::string> ActiveOperations;
	}
	FOperationScope::FOperationScope(std::string R) : Root(std::move(R))
	{
		if (!ActiveOperations.insert(Root).second)
		{
			Fail("operation-reentry", "cannot reenter package management from an active callback");
		}
	}
	FOperationScope::~FOperationScope()
	{
		ActiveOperations.erase(Root);
	}
}
