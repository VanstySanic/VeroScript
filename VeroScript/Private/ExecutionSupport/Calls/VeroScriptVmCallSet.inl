// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		if (const auto* SetHandle = std::get_if<FSetHandle>(&ThisValue))
		{
			if (!*SetHandle)
			{
				ReportError("method call requires object", SourceLocation);
				return false;
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
					ReserveSetStorage(
						**SetHandle,
						*Count,
						VmDetail::IsScopedEnumType(
							ActiveModule, (*SetHandle)->ElementTypeName));
				}
				catch (const std::exception&)
				{
					ReportError("set reserve failed", SourceLocation);
					return false;
				}
				Push(ThisValue);
				return true;
			}
			if ((*SetHandle)->ScalarElements.index() != 0
				&& (MethodName == "add" || MethodName == "has" || MethodName == "remove"))
			{
				if (ArgumentCount != 1)
				{
					ReportError(MethodName + " expects exactly 1 argument", SourceLocation);
					return false;
				}
				bool bHandled = false;
				bool bResult = false;
				std::visit(
					[&](auto& Elements)
					{
						using TElements = std::decay_t<decltype(Elements)>;
						if constexpr (!std::is_same_v<TElements, std::monostate>)
						{
							using TElement = typename TElements::key_type;
							const auto* const Element = std::get_if<TElement>(&Arguments[0]);
							if (!Element)
							{
								return;
							}
							bHandled = true;
							if (MethodName == "has")
							{
								bResult = Elements.Contains(*Element);
							}
							else if (MethodName == "remove")
							{
								bResult = Elements.Erase(*Element);
							}
							else
							{
								(void)Elements.Insert(*Element);
							}
						}
					},
					(*SetHandle)->ScalarElements);
				if (!bHandled)
				{
					ReportError("set element type mismatch", SourceLocation);
					return false;
				}
				if (MethodName == "add")
				{
					Push(ThisValue);
				}
				else
				{
					Push(bResult);
				}
				return true;
			}
			if (MethodName == "add")
			{
				if (ArgumentCount != 1)
				{
					ReportError("add expects exactly 1 argument", SourceLocation);
					return false;
				}
				if (VmDetail::IsSigned32KeyStorageType((*SetHandle)->ElementTypeName))
				{
					std::int32_t Element = 0;
					if (!VmDetail::TryGetSigned32Key(
							Arguments[0], (*SetHandle)->ElementTypeName, Element))
					{
						ReportError("set element type mismatch", SourceLocation);
						return false;
					}
					(*SetHandle)->IntElements.insert(Element);
					Push(ThisValue);
					return true;
				}
				if ((*SetHandle)->ElementTypeName == "unsigned int")
				{
					const auto* Element = std::get_if<std::uint32_t>(&Arguments[0]);
					if (!Element)
					{
						ReportError("set element type mismatch", SourceLocation);
						return false;
					}
					(*SetHandle)->UIntElements.insert(*Element);
					Push(ThisValue);
					return true;
				}
				if ((*SetHandle)->ElementTypeName == "int64")
				{
					const auto* Element = std::get_if<std::int64_t>(&Arguments[0]);
					if (!Element)
					{
						ReportError("set element type mismatch", SourceLocation);
						return false;
					}
					(*SetHandle)->Int64Elements.insert(*Element);
					Push(ThisValue);
					return true;
				}
				if ((*SetHandle)->ElementTypeName == "uint64")
				{
					const auto* Element = std::get_if<std::uint64_t>(&Arguments[0]);
					if (!Element)
					{
						ReportError("set element type mismatch", SourceLocation);
						return false;
					}
					(*SetHandle)->UInt64Elements.insert(*Element);
					Push(ThisValue);
					return true;
				}
				if ((*SetHandle)->ElementTypeName == "string")
				{
					const auto* Element = std::get_if<std::string>(&Arguments[0]);
					if (!Element)
					{
						ReportError("set element type mismatch", SourceLocation);
						return false;
					}
					(*SetHandle)->StrElements.insert(*Element);
					Push(ThisValue);
					return true;
				}
				if ((*SetHandle)->ElementTypeName == "wstring")
				{
					const auto* Element = std::get_if<std::u16string>(&Arguments[0]);
					if (!Element)
					{
						ReportError("set element type mismatch", SourceLocation);
						return false;
					}
					(*SetHandle)->WStrElements.insert(*Element);
					Push(ThisValue);
					return true;
				}
				if (const auto* Element = std::get_if<FEnumValue>(&Arguments[0]);
					Element && Element->TypeName == Runtime::EnumValueTypeName(*ActiveModule, (*SetHandle)->ElementTypeName))
				{
					(*SetHandle)->EnumElements.insert(*Element);
					Push(ThisValue);
					return true;
				}
				if (const auto* Element = std::get_if<FHostObjectHandle>(&Arguments[0]);
					Element && *Element
						&& ValueMatchesElementType(
							Arguments[0],
							(*SetHandle)->ElementTypeName,
						SourceLocation))
				{
					StampHostValueCallbacks(*Element);
					if ((*SetHandle)->HostElements.find(*Element)
						== (*SetHandle)->HostElements.end())
					{
						FHostObjectHandle StoredElement;
						if (!PrepareHostKeyForInsert(
								*Element, StoredElement, SourceLocation))
						{
							return false;
						}
						(*SetHandle)->HostElements.insert(std::move(StoredElement));
					}
					Push(ThisValue);
					return true;
				}
				ReportError("invalid set element type", SourceLocation);
				return false;
			}
			if (MethodName == "has")
			{
				if (ArgumentCount != 1)
				{
					ReportError("has expects exactly 1 argument", SourceLocation);
					return false;
				}
				if (VmDetail::IsSigned32KeyStorageType((*SetHandle)->ElementTypeName))
				{
					std::int32_t Element = 0;
					if (!VmDetail::TryGetSigned32Key(
							Arguments[0], (*SetHandle)->ElementTypeName, Element))
					{
						ReportError("set element type mismatch", SourceLocation);
						return false;
					}
					Push((*SetHandle)->IntElements.find(Element) != (*SetHandle)->IntElements.end());
					return true;
				}
				if ((*SetHandle)->ElementTypeName == "unsigned int")
				{
					const auto* Element = std::get_if<std::uint32_t>(&Arguments[0]);
					if (!Element)
					{
						ReportError("set element type mismatch", SourceLocation);
						return false;
					}
					Push((*SetHandle)->UIntElements.find(*Element)
						!= (*SetHandle)->UIntElements.end());
					return true;
				}
				if ((*SetHandle)->ElementTypeName == "int64")
				{
					const auto* Element = std::get_if<std::int64_t>(&Arguments[0]);
					if (!Element)
					{
						ReportError("set element type mismatch", SourceLocation);
						return false;
					}
					Push((*SetHandle)->Int64Elements.find(*Element)
						!= (*SetHandle)->Int64Elements.end());
					return true;
				}
				if ((*SetHandle)->ElementTypeName == "uint64")
				{
					const auto* Element = std::get_if<std::uint64_t>(&Arguments[0]);
					if (!Element)
					{
						ReportError("set element type mismatch", SourceLocation);
						return false;
					}
					Push((*SetHandle)->UInt64Elements.find(*Element)
						!= (*SetHandle)->UInt64Elements.end());
					return true;
				}
				if ((*SetHandle)->ElementTypeName == "string")
				{
					const auto* Element = std::get_if<std::string>(&Arguments[0]);
					if (!Element)
					{
						ReportError("set element type mismatch", SourceLocation);
						return false;
					}
					Push((*SetHandle)->StrElements.find(*Element) != (*SetHandle)->StrElements.end());
					return true;
				}
				if ((*SetHandle)->ElementTypeName == "wstring")
				{
					const auto* Element = std::get_if<std::u16string>(&Arguments[0]);
					if (!Element)
					{
						ReportError("set element type mismatch", SourceLocation);
						return false;
					}
					Push((*SetHandle)->WStrElements.find(*Element) != (*SetHandle)->WStrElements.end());
					return true;
				}
				if (const auto* Element = std::get_if<FEnumValue>(&Arguments[0]);
					Element && Element->TypeName == Runtime::EnumValueTypeName(*ActiveModule, (*SetHandle)->ElementTypeName))
				{
					Push((*SetHandle)->EnumElements.find(*Element)
						!= (*SetHandle)->EnumElements.end());
					return true;
				}
				if (const auto* Element = std::get_if<FHostObjectHandle>(&Arguments[0]);
					Element && *Element
						&& ValueMatchesElementType(
							Arguments[0],
							(*SetHandle)->ElementTypeName,
							SourceLocation))
				{
					Push(
						(*SetHandle)->HostElements.find(*Element)
						!= (*SetHandle)->HostElements.end());
					return true;
				}
				ReportError("invalid set element type", SourceLocation);
				return false;
			}
			if (MethodName == "remove")
			{
				if (ArgumentCount != 1)
				{
					ReportError("remove expects exactly 1 argument", SourceLocation);
					return false;
				}
				if (VmDetail::IsSigned32KeyStorageType((*SetHandle)->ElementTypeName))
				{
					std::int32_t Element = 0;
					if (!VmDetail::TryGetSigned32Key(
							Arguments[0], (*SetHandle)->ElementTypeName, Element))
					{
						ReportError("set element type mismatch", SourceLocation);
						return false;
					}
					Push((*SetHandle)->IntElements.erase(Element) > 0);
					return true;
				}
				if ((*SetHandle)->ElementTypeName == "unsigned int")
				{
					const auto* Element = std::get_if<std::uint32_t>(&Arguments[0]);
					if (!Element)
					{
						ReportError("set element type mismatch", SourceLocation);
						return false;
					}
					Push((*SetHandle)->UIntElements.erase(*Element) > 0);
					return true;
				}
				if ((*SetHandle)->ElementTypeName == "int64")
				{
					const auto* Element = std::get_if<std::int64_t>(&Arguments[0]);
					if (!Element)
					{
						ReportError("set element type mismatch", SourceLocation);
						return false;
					}
					Push((*SetHandle)->Int64Elements.erase(*Element) > 0);
					return true;
				}
				if ((*SetHandle)->ElementTypeName == "uint64")
				{
					const auto* Element = std::get_if<std::uint64_t>(&Arguments[0]);
					if (!Element)
					{
						ReportError("set element type mismatch", SourceLocation);
						return false;
					}
					Push((*SetHandle)->UInt64Elements.erase(*Element) > 0);
					return true;
				}
				if ((*SetHandle)->ElementTypeName == "string")
				{
					const auto* Element = std::get_if<std::string>(&Arguments[0]);
					if (!Element)
					{
						ReportError("set element type mismatch", SourceLocation);
						return false;
					}
					Push((*SetHandle)->StrElements.erase(*Element) > 0);
					return true;
				}
				if ((*SetHandle)->ElementTypeName == "wstring")
				{
					const auto* Element = std::get_if<std::u16string>(&Arguments[0]);
					if (!Element)
					{
						ReportError("set element type mismatch", SourceLocation);
						return false;
					}
					Push((*SetHandle)->WStrElements.erase(*Element) > 0);
					return true;
				}
				if (const auto* Element = std::get_if<FEnumValue>(&Arguments[0]);
					Element && Element->TypeName == Runtime::EnumValueTypeName(*ActiveModule, (*SetHandle)->ElementTypeName))
				{
					Push((*SetHandle)->EnumElements.erase(*Element) > 0);
					return true;
				}
				if (const auto* Element = std::get_if<FHostObjectHandle>(&Arguments[0]);
					Element && *Element
						&& ValueMatchesElementType(
							Arguments[0],
							(*SetHandle)->ElementTypeName,
							SourceLocation))
				{
					Push((*SetHandle)->HostElements.erase(*Element) > 0);
					return true;
				}
				ReportError("invalid set element type", SourceLocation);
				return false;
			}
			if (MethodName == "values")
			{
				if (ArgumentCount != 0)
				{
					ReportError("values expects no arguments", SourceLocation);
					return false;
				}
				const std::string ValuesTypeName =
					"array<" + (*SetHandle)->ElementTypeName + ">";
				FDynamicArrayHandle ValuesArray =
					CreateDynamicArray(ValuesTypeName, SourceLocation);
				if (!ValuesArray)
				{
					return false;
				}
				// Root completed elements for exception/cancellation cleanup.
				Push(ValuesArray);
				if (VmDetail::IsSigned32KeyStorageType((*SetHandle)->ElementTypeName))
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*SetHandle)->IntElements.size());
					for (const std::int32_t Element : (*SetHandle)->IntElements)
					{
						if (!AppendDynamicArrayValue(
								*ValuesArray,
								VmDetail::MakeSigned32KeyValue(
									Element, (*SetHandle)->ElementTypeName),
								SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*SetHandle)->ElementTypeName == "unsigned int")
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*SetHandle)->UIntElements.size());
					for (const std::uint32_t Element : (*SetHandle)->UIntElements)
					{
						if (!AppendDynamicArrayValue(*ValuesArray, FValue{Element}, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*SetHandle)->ElementTypeName == "int64")
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*SetHandle)->Int64Elements.size());
					for (const std::int64_t Element : (*SetHandle)->Int64Elements)
					{
						if (!AppendDynamicArrayValue(*ValuesArray, FValue{Element}, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*SetHandle)->ElementTypeName == "uint64")
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*SetHandle)->UInt64Elements.size());
					for (const std::uint64_t Element : (*SetHandle)->UInt64Elements)
					{
						if (!AppendDynamicArrayValue(*ValuesArray, FValue{Element}, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*SetHandle)->ElementTypeName == "string")
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*SetHandle)->StrElements.size());
					for (const std::string& Element : (*SetHandle)->StrElements)
					{
						if (!AppendDynamicArrayValue(*ValuesArray, FValue{Element}, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*SetHandle)->ElementTypeName == "wstring")
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*SetHandle)->WStrElements.size());
					for (const std::u16string& Element : (*SetHandle)->WStrElements)
					{
						if (!AppendDynamicArrayValue(*ValuesArray, FValue{Element}, SourceLocation))
						{
							return false;
						}
					}
				}
				else if (!(*SetHandle)->EnumElements.empty()
					|| VmDetail::IsScopedEnumType(ActiveModule, (*SetHandle)->ElementTypeName))
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*SetHandle)->EnumElements.size());
					for (const FEnumValue& Element : (*SetHandle)->EnumElements)
					{
						if (!AppendDynamicArrayValue(*ValuesArray, FValue{Element}, SourceLocation))
						{
							return false;
						}
					}
				}
				else if (!VmDetail::IsSigned32KeyStorageType((*SetHandle)->ElementTypeName))
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*SetHandle)->HostElements.size());
					for (const FHostObjectHandle& Element : (*SetHandle)->HostElements)
					{
						FValue ClonedElement;
						if (!CloneValueOrReport(
								FValue(Element), ClonedElement, SourceLocation))
						{
							return false;
						}
						if (!AppendDynamicArrayValue(*ValuesArray, ClonedElement, SourceLocation))
						{
							return false;
						}
					}
				}
				else
				{
					ReportError("invalid set element type", SourceLocation);
					return false;
				}
				return true;
			}
			if (MethodName == "clear")
			{
				if (ArgumentCount != 0)
				{
					ReportError("clear expects no arguments", SourceLocation);
					return false;
				}
				(*SetHandle)->IntElements.clear();
				(*SetHandle)->UIntElements.clear();
				(*SetHandle)->Int64Elements.clear();
				(*SetHandle)->UInt64Elements.clear();
				(*SetHandle)->StrElements.clear();
				(*SetHandle)->WStrElements.clear();
				(*SetHandle)->EnumElements.clear();
				(*SetHandle)->HostElements.clear();
				Push(ThisValue);
				return true;
			}
			ReportError("unknown method: " + MethodName, SourceLocation);
			return false;
		}
