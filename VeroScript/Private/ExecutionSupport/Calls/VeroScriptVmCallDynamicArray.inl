// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		if (const auto* DynamicArray = std::get_if<FDynamicArrayHandle>(&ThisValue))
		{
			if (!*DynamicArray)
			{
				ReportError("method call requires object", SourceLocation);
				return false;
			}
			if (MethodName == "push")
			{
				if (ArgumentCount != 1)
				{
					ReportError("push expects exactly 1 argument", SourceLocation);
					return false;
				}
				if (!ValueMatchesElementType(Arguments[0], (*DynamicArray)->ElementTypeName, SourceLocation))
				{
					ReportError(
						"push argument type mismatch for array<" + (*DynamicArray)->ElementTypeName + ">",
						SourceLocation);
					return false;
				}
				if ((*DynamicArray)->HasScalarElements())
				{
					if (!(*DynamicArray)->TryPushScalar(Arguments[0]))
					{
						ReportError("invalid scalar array storage", SourceLocation);
						return false;
					}
					// VeroScriptVmCallCommon's mutation guard emits the single
					// observable ContainerAdd event for both storage representations.
					Push(ThisValue);
					return true;
				}
				// TryCloneValue deep-copies value types (script/host structs, T[N]);
				// scalars and reference handles stay identity copies.
				if (!AppendClonedValue((*DynamicArray)->Elements, Arguments[0], SourceLocation))
				{
					return false;
				}
				Push(ThisValue);
				return true;
			}
			if (MethodName == "reserve")
			{
				const std::optional<std::size_t> Count = ReadReserveCount();
				if (!Count)
				{
					return false;
				}
				try
				{
					if ((*DynamicArray)->HasScalarElements())
					{
						(*DynamicArray)->ReserveScalar(*Count);
					}
					else
					{
						(*DynamicArray)->Elements.reserve(*Count);
					}
				}
				catch (const std::exception&)
				{
					ReportError("array reserve failed", SourceLocation);
					return false;
				}
				Push(ThisValue);
				return true;
			}
			if (MethodName == "capacity")
			{
				if (ArgumentCount != 0)
				{
					ReportError("capacity expects no arguments", SourceLocation);
					return false;
				}
				const std::size_t Capacity = (*DynamicArray)->HasScalarElements()
					? (*DynamicArray)->ScalarCapacity()
					: (*DynamicArray)->Elements.capacity();
				if (Capacity > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
				{
					ReportError("array capacity exceeds int64 range", SourceLocation);
					return false;
				}
				Push(static_cast<std::int64_t>(Capacity));
				return true;
			}
			if (MethodName == "clear")
			{
				if (ArgumentCount != 0)
				{
					ReportError("clear expects no arguments", SourceLocation);
					return false;
				}
				if ((*DynamicArray)->HasScalarElements())
				{
					(*DynamicArray)->ClearScalar();
				}
				else if (!ReleaseDetachedValueList(
						(*DynamicArray)->Elements, SourceLocation))
				{
					return false;
				}
				Push(ThisValue);
				return true;
			}
			if (MethodName == "remove_at")
			{
				if (ArgumentCount != 1)
				{
					ReportError("remove_at expects exactly 1 argument", SourceLocation);
					return false;
				}
				std::size_t Index = 0;
				if (!Runtime::TryGetSizeValue(Arguments[0], Index))
				{
					ReportError("remove_at index out of range", SourceLocation);
					return false;
				}
				if ((*DynamicArray)->HasScalarElements())
				{
					if (!(*DynamicArray)->EraseScalarAt(static_cast<std::size_t>(Index)))
					{
						ReportError("remove_at index out of range", SourceLocation);
						return false;
					}
					Push(ThisValue);
					return !bHasError;
				}
				if (static_cast<std::size_t>(Index) >= (*DynamicArray)->Elements.size())
				{
					ReportError("remove_at index out of range", SourceLocation);
					return false;
				}
				auto& Elements = (*DynamicArray)->Elements;
				FValue Removed = std::move(Elements[static_cast<std::size_t>(Index)]);
				Elements.erase(Elements.begin() + Index);
				if (!ReleaseValue(Removed, SourceLocation))
				{
					return false;
				}
				Push(ThisValue);
				return true;
			}
			if (MethodName == "remove")
			{
				if (ArgumentCount != 1)
				{
					ReportError("remove expects exactly 1 argument", SourceLocation);
					return false;
				}
				if (!ValueMatchesElementType(Arguments[0], (*DynamicArray)->ElementTypeName, SourceLocation))
				{
					ReportError(
						"remove argument type mismatch for array<" + (*DynamicArray)->ElementTypeName + ">",
						SourceLocation);
					return false;
				}
				bool bRemoved = false;
				if ((*DynamicArray)->HasScalarElements())
				{
					if (!(*DynamicArray)->TryRemoveFirstScalar(Arguments[0], bRemoved))
					{
						ReportError("invalid scalar array storage", SourceLocation);
						return false;
					}
					Push(bRemoved);
					return true;
				}
				auto& Elements = (*DynamicArray)->Elements;
				for (auto It = Elements.begin(); It != Elements.end(); ++It)
				{
					if (Runtime::ValuesEqual(*ActiveModule, *It, Arguments[0]))
					{
						FValue Removed = std::move(*It);
						Elements.erase(It);
						if (!ReleaseValue(Removed, SourceLocation))
						{
							return false;
						}
						bRemoved = true;
						break;
					}
				}
				Push(bRemoved);
				return true;
			}
			ReportError("unknown method: " + MethodName, SourceLocation);
			return false;
		}
