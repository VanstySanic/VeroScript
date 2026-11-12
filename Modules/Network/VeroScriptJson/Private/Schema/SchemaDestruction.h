// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstddef>

namespace VeroScript::Json::Detail
{
	// Postorder traversal uses fixed storage. Every child is empty before its
	// parent releases it, so deep values do not recurse through their destructors.
	template <class J>
	void ClearSchemaChildren(J& Root) noexcept
	{
		struct FFrame
		{
			J* Value;
			std::size_t Next;
		};
		using EStorage = decltype(Root.storage_kind());
		std::array<FFrame, 257> Frames;
		std::size_t Count = 1;
		Frames[0] = {&Root, 0};
		while (Count)
		{
			auto& Current = Frames[Count - 1];
			if (Current.Next == Current.Value->size())
			{
				Current.Value->clear();
				--Count;
				continue;
			}
			J* Child = Current.Value->storage_kind() == EStorage::array
						   ? &*(Current.Value->array_range().begin() + Current.Next)
						   : &(Current.Value->object_range().begin() + Current.Next)->value();
			++Current.Next;
			if ((Child->storage_kind() != EStorage::array && Child->storage_kind() != EStorage::object) ||
				Child->empty())
			{
				continue;
			}
			if (Count == Frames.size())
			{
				// Defensive chunking for internal values outside the adapter's 256-depth domain.
				Child->clear();
			}
			else
			{
				Frames[Count++] = {Child, 0};
			}
		}
	}
} // namespace VeroScript::Json::Detail
