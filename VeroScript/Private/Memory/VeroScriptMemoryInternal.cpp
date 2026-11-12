// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptMemoryInternal.h"

namespace VeroScript::Memory::Detail
{
	bool ContainsMemoryType(std::string_view Type) noexcept
	{
		const auto IsNameCharacter = [](char Character)
		{
			return (Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z') ||
				   (Character >= '0' && Character <= '9') || Character == '_' || Character == '.';
		};
		for (std::size_t Offset = 0; Offset < Type.size();)
		{
			if (!IsNameCharacter(Type[Offset]))
			{
				++Offset;
				continue;
			}
			const auto Begin = Offset;
			while (Offset < Type.size() && IsNameCharacter(Type[Offset]))
			{
				++Offset;
			}
			if (IsTypeName(Type.substr(Begin, Offset - Begin)))
			{
				return true;
			}
		}
		return false;
	}
} // namespace VeroScript::Memory::Detail
