// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptPath.h"

namespace VeroScript::Detail
{
	// Match the ASCII suffix consistently without changing filesystem path identity.
	inline bool IsLibraryPackagePath(const std::string_view Path)
	{
		std::string Extension = PathUtil::ToUtf8(PathUtil::FromUtf8(Path).extension());
		for (char& Character : Extension)
		{
			if (Character >= 'A' && Character <= 'Z')
			{
				Character = static_cast<char>(Character - 'A' + 'a');
			}
		}
		return Extension == ".vslib";
	}
}
