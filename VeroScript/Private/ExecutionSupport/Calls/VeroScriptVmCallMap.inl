// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		if (const auto* MapHandle = std::get_if<FMapHandle>(&ThisValue))
		{
			if (!*MapHandle)
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
					ReserveMapStorage(
						**MapHandle,
						*Count,
						VmDetail::IsScopedEnumType(
							ActiveModule, (*MapHandle)->KeyTypeName));
				}
				catch (const std::exception&)
				{
					ReportError("map reserve failed", SourceLocation);
					return false;
				}
				Push(ThisValue);
				return true;
			}
			const bool bMultiSet = Runtime::IsMultiSetTypeName((*MapHandle)->TypeName);
			if (!bMultiSet && (*MapHandle)->ScalarEntries.index() != 0
				&& (MethodName == "has" || MethodName == "remove"))
			{
				if (ArgumentCount != 1)
				{
					ReportError(MethodName + " expects exactly 1 argument", SourceLocation);
					return false;
				}
				bool bTypeMatched = false;
				bool bResult = false;
				std::visit(
					[&](auto& Entries)
					{
						using TEntries = std::decay_t<decltype(Entries)>;
						if constexpr (!std::is_same_v<TEntries, std::monostate>)
						{
							using TKey = typename TEntries::key_type;
							const auto* const Key = std::get_if<TKey>(&Arguments[0]);
							if (!Key)
							{
								return;
							}
							bTypeMatched = true;
							bResult = MethodName == "has"
									? Entries.Find(*Key) != nullptr
									: Entries.Erase(*Key);
						}
					},
					(*MapHandle)->ScalarEntries);
				if (!bTypeMatched)
				{
					ReportError("map key type mismatch", SourceLocation);
					return false;
				}
				Push(bResult);
				return true;
			}
			if (bMultiSet
				&& (MethodName == "add"
					|| MethodName == "count"
					|| MethodName == "remove"))
			{
				if (ArgumentCount != 1)
				{
					ReportError(MethodName + " expects exactly 1 argument", SourceLocation);
					return false;
				}
				bool bMatchedKey = false;
				bool bOperationSucceeded = false;
				const auto VisitBucket = [&](auto& Entries, const auto& Key)
				{
					bMatchedKey = true;
					auto It = Entries.find(Key);
					if (MethodName == "add")
					{
						if (It == Entries.end())
						{
							FDynamicArrayHandle Occurrences =
								CreateDynamicArray("array<bool>", SourceLocation);
							if (!Occurrences)
							{
								return;
							}
							It = Entries.emplace(
								Key,
								FValue(std::move(Occurrences)))
									 .first;
						}
						auto* Occurrences = std::get_if<FDynamicArrayHandle>(&It->second);
						if (!Occurrences || !*Occurrences)
						{
							ReportError("invalid multiset occurrence storage", SourceLocation);
							return;
						}
						if (!AppendDynamicArrayValue(
								**Occurrences, FValue{true}, SourceLocation))
						{
							return;
						}
						bOperationSucceeded = true;
						return;
					}
					if (MethodName == "count")
					{
						std::int64_t Count = 0;
						if (It != Entries.end())
						{
							const auto* Occurrences =
								std::get_if<FDynamicArrayHandle>(&It->second);
							if (!Occurrences || !*Occurrences)
							{
								ReportError(
									"invalid multiset occurrence storage",
									SourceLocation);
								return;
							}
							const std::size_t Length = (*Occurrences)->ElementCount();
							if (Length > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
							{
								ReportError("multiset count exceeds int64 range", SourceLocation);
								return;
							}
							Count = static_cast<std::int64_t>(Length);
						}
						Push(Count);
						bOperationSucceeded = true;
						return;
					}
					if (It == Entries.end())
					{
						Push(false);
						bOperationSucceeded = true;
						return;
					}
					auto* Occurrences = std::get_if<FDynamicArrayHandle>(&It->second);
					const std::size_t OccurrenceCount = Occurrences && *Occurrences
						? ((*Occurrences)->HasScalarElements()
							? (*Occurrences)->ScalarSize()
							: (*Occurrences)->Elements.size())
						: 0u;
					if (OccurrenceCount == 0)
					{
						ReportError("invalid multiset occurrence storage", SourceLocation);
						return;
					}
					if (OccurrenceCount > 1)
					{
						if ((*Occurrences)->HasScalarElements())
						{
							(void)(*Occurrences)->EraseScalarAt(OccurrenceCount - 1u);
						}
						else
						{
							(*Occurrences)->Elements.pop_back();
						}
					}
					else
					{
						FValue Removed = std::move(It->second);
						Entries.erase(It);
						if (!ReleaseValue(Removed, SourceLocation))
						{
							return;
						}
					}
					Push(true);
					bOperationSucceeded = true;
				};
				std::int32_t Signed32Key = 0;
				if (VmDetail::TryGetSigned32Key(
						Arguments[0], (*MapHandle)->KeyTypeName, Signed32Key))
				{
					VisitBucket((*MapHandle)->IntEntries, Signed32Key);
				}
				else if ((*MapHandle)->KeyTypeName == "unsigned int")
				{
					if (const auto* Key = std::get_if<std::uint32_t>(&Arguments[0]))
					{
						VisitBucket((*MapHandle)->UIntEntries, *Key);
					}
				}
				else if ((*MapHandle)->KeyTypeName == "int64")
				{
					if (const auto* Key = std::get_if<std::int64_t>(&Arguments[0]))
					{
						VisitBucket((*MapHandle)->Int64Entries, *Key);
					}
				}
				else if ((*MapHandle)->KeyTypeName == "uint64")
				{
					if (const auto* Key = std::get_if<std::uint64_t>(&Arguments[0]))
					{
						VisitBucket((*MapHandle)->UInt64Entries, *Key);
					}
				}
				else if ((*MapHandle)->KeyTypeName == "string")
				{
					if (const auto* Key = std::get_if<std::string>(&Arguments[0]))
					{
						VisitBucket((*MapHandle)->StrEntries, *Key);
					}
				}
				else if ((*MapHandle)->KeyTypeName == "wstring")
				{
					if (const auto* Key = std::get_if<std::u16string>(&Arguments[0]))
					{
						VisitBucket((*MapHandle)->WStrEntries, *Key);
					}
				}
				else if (const auto* EnumKey = std::get_if<FEnumValue>(&Arguments[0]);
					EnumKey && EnumKey->TypeName == Runtime::EnumValueTypeName(*ActiveModule, (*MapHandle)->KeyTypeName))
				{
					VisitBucket((*MapHandle)->EnumEntries, *EnumKey);
				}
				else if (const auto* HostKey = std::get_if<FHostObjectHandle>(&Arguments[0]);
					HostKey && *HostKey
						&& ValueMatchesElementType(
							Arguments[0],
							(*MapHandle)->KeyTypeName,
							SourceLocation))
				{
					VisitBucket((*MapHandle)->HostEntries, *HostKey);
				}
				if (!bMatchedKey)
				{
					ReportError("multiset element type mismatch", SourceLocation);
					return false;
				}
				if (!bOperationSucceeded)
				{
					return false;
				}
				if (MethodName == "add")
				{
					Push(ThisValue);
				}
				return true;
			}
			if (bMultiSet && MethodName == "values")
			{
				if (ArgumentCount != 0)
				{
					ReportError("values expects no arguments", SourceLocation);
					return false;
				}
				std::string ElementType;
				if (!Runtime::ParseMultiSetElementType(
						(*MapHandle)->TypeName,
						ElementType))
				{
					ReportError("invalid multiset type", SourceLocation);
					return false;
				}
				FDynamicArrayHandle Values =
					CreateDynamicArray("array<" + ElementType + ">", SourceLocation);
				if (!Values)
				{
					return false;
				}
				const auto AppendEntries = [&](const auto& Entries) -> bool
				{
					for (const auto& Entry : Entries)
					{
						const auto* Occurrences =
							std::get_if<FDynamicArrayHandle>(&Entry.second);
						if (!Occurrences || !*Occurrences)
						{
							ReportError("invalid multiset occurrence storage", SourceLocation);
							return false;
						}
						const std::size_t Count = (*Occurrences)->HasScalarElements()
							? (*Occurrences)->ScalarSize()
							: (*Occurrences)->Elements.size();
						for (std::size_t Index = 0;
							Index < Count;
							++Index)
						{
							FValue ElementValue;
							using TKey = std::decay_t<decltype(Entry.first)>;
							if constexpr (std::is_same_v<TKey, std::int32_t>)
							{
								ElementValue = VmDetail::MakeSigned32KeyValue(
									Entry.first, (*MapHandle)->KeyTypeName);
							}
							else
							{
								ElementValue = FValue(Entry.first);
							}
							if (!AppendDynamicArrayValue(
									*Values, ElementValue, SourceLocation))
							{
								return false;
							}
						}
					}
					return true;
				};
				const bool bAppended = VmDetail::IsSigned32KeyStorageType(
					(*MapHandle)->KeyTypeName)
					? AppendEntries((*MapHandle)->IntEntries)
					: (*MapHandle)->KeyTypeName == "unsigned int"
						? AppendEntries((*MapHandle)->UIntEntries)
						: (*MapHandle)->KeyTypeName == "int64"
							? AppendEntries((*MapHandle)->Int64Entries)
							: (*MapHandle)->KeyTypeName == "uint64"
								? AppendEntries((*MapHandle)->UInt64Entries)
								: (*MapHandle)->KeyTypeName == "string"
									? AppendEntries((*MapHandle)->StrEntries)
									: (*MapHandle)->KeyTypeName == "wstring"
										? AppendEntries((*MapHandle)->WStrEntries)
										: !(*MapHandle)->EnumEntries.empty()
											? AppendEntries((*MapHandle)->EnumEntries)
											: AppendEntries((*MapHandle)->HostEntries);
				if (!bAppended)
				{
					return false;
				}
				Push(Values);
				return true;
			}
			if (MethodName == "add" && Runtime::IsMultiMapTypeName((*MapHandle)->TypeName))
			{
				if (ArgumentCount != 2)
				{
					ReportError("add expects exactly 2 arguments", SourceLocation);
					return false;
				}
				std::string ElementTypeName;
				if (!Runtime::ParseDynamicArrayElementType(
						(*MapHandle)->ValueTypeName,
						ElementTypeName)
					|| !ValueMatchesElementType(
						Arguments[1],
						ElementTypeName,
						SourceLocation))
				{
					ReportError("multimap value type mismatch", SourceLocation);
					return false;
				}
				const auto AddValue = [&](auto& Entries, const auto& Key) -> bool
				{
					auto It = Entries.find(Key);
					if (It == Entries.end())
					{
						FDynamicArrayHandle Values =
							CreateDynamicArray((*MapHandle)->ValueTypeName, SourceLocation);
						if (!Values)
						{
							return false;
						}
						It = Entries.emplace(Key, FValue(std::move(Values))).first;
					}
					auto* Values = std::get_if<FDynamicArrayHandle>(&It->second);
					if (!Values || !*Values)
					{
						ReportError("invalid multimap value storage", SourceLocation);
						return false;
					}
					return AppendDynamicArrayValue(
						**Values, Arguments[1], SourceLocation);
				};
				bool bAdded = false;
				std::int32_t Signed32Key = 0;
				if (VmDetail::TryGetSigned32Key(
						Arguments[0], (*MapHandle)->KeyTypeName, Signed32Key))
				{
					bAdded = AddValue((*MapHandle)->IntEntries, Signed32Key);
				}
				else if ((*MapHandle)->KeyTypeName == "unsigned int")
				{
					if (const auto* Key = std::get_if<std::uint32_t>(&Arguments[0]))
					{
						bAdded = AddValue((*MapHandle)->UIntEntries, *Key);
					}
				}
				else if ((*MapHandle)->KeyTypeName == "int64")
				{
					if (const auto* Key = std::get_if<std::int64_t>(&Arguments[0]))
					{
						bAdded = AddValue((*MapHandle)->Int64Entries, *Key);
					}
				}
				else if ((*MapHandle)->KeyTypeName == "uint64")
				{
					if (const auto* Key = std::get_if<std::uint64_t>(&Arguments[0]))
					{
						bAdded = AddValue((*MapHandle)->UInt64Entries, *Key);
					}
				}
				else if ((*MapHandle)->KeyTypeName == "string")
				{
					if (const auto* Key = std::get_if<std::string>(&Arguments[0]))
					{
						bAdded = AddValue((*MapHandle)->StrEntries, *Key);
					}
				}
				else if ((*MapHandle)->KeyTypeName == "wstring")
				{
					if (const auto* Key = std::get_if<std::u16string>(&Arguments[0]))
					{
						bAdded = AddValue((*MapHandle)->WStrEntries, *Key);
					}
				}
				else if (const auto* EnumKey = std::get_if<FEnumValue>(&Arguments[0]);
					EnumKey && EnumKey->TypeName == Runtime::EnumValueTypeName(*ActiveModule, (*MapHandle)->KeyTypeName))
				{
					bAdded = AddValue((*MapHandle)->EnumEntries, *EnumKey);
				}
				else if (const auto* HostKey = std::get_if<FHostObjectHandle>(&Arguments[0]);
					HostKey && *HostKey
						&& ValueMatchesElementType(
							Arguments[0],
							(*MapHandle)->KeyTypeName,
							SourceLocation))
				{
					bAdded = AddValue((*MapHandle)->HostEntries, *HostKey);
				}
				else
				{
					ReportError("multimap key type mismatch", SourceLocation);
					return false;
				}
				if (!bAdded)
				{
					return false;
				}
				Push(ThisValue);
				return true;
			}
			if (MethodName == "has")
			{
				if (ArgumentCount != 1)
				{
					ReportError("has expects exactly 1 argument", SourceLocation);
					return false;
				}
				if (VmDetail::IsSigned32KeyStorageType((*MapHandle)->KeyTypeName))
				{
					std::int32_t Key = 0;
					if (!VmDetail::TryGetSigned32Key(
							Arguments[0], (*MapHandle)->KeyTypeName, Key))
					{
						ReportError("map key type mismatch", SourceLocation);
						return false;
					}
					Push((*MapHandle)->IntEntries.find(Key) != (*MapHandle)->IntEntries.end());
					return true;
				}
				if ((*MapHandle)->KeyTypeName == "unsigned int")
				{
					const auto* Key = std::get_if<std::uint32_t>(&Arguments[0]);
					if (!Key)
					{
						ReportError("map key type mismatch", SourceLocation);
						return false;
					}
					Push((*MapHandle)->UIntEntries.find(*Key) != (*MapHandle)->UIntEntries.end());
					return true;
				}
				if ((*MapHandle)->KeyTypeName == "int64")
				{
					const auto* Key = std::get_if<std::int64_t>(&Arguments[0]);
					if (!Key)
					{
						ReportError("map key type mismatch", SourceLocation);
						return false;
					}
					Push((*MapHandle)->Int64Entries.find(*Key) != (*MapHandle)->Int64Entries.end());
					return true;
				}
				if ((*MapHandle)->KeyTypeName == "uint64")
				{
					const auto* Key = std::get_if<std::uint64_t>(&Arguments[0]);
					if (!Key)
					{
						ReportError("map key type mismatch", SourceLocation);
						return false;
					}
					Push((*MapHandle)->UInt64Entries.find(*Key) != (*MapHandle)->UInt64Entries.end());
					return true;
				}
				if ((*MapHandle)->KeyTypeName == "string")
				{
					const auto* Key = std::get_if<std::string>(&Arguments[0]);
					if (!Key)
					{
						ReportError("map key type mismatch", SourceLocation);
						return false;
					}
					Push((*MapHandle)->StrEntries.find(*Key) != (*MapHandle)->StrEntries.end());
					return true;
				}
				if ((*MapHandle)->KeyTypeName == "wstring")
				{
					const auto* Key = std::get_if<std::u16string>(&Arguments[0]);
					if (!Key)
					{
						ReportError("map key type mismatch", SourceLocation);
						return false;
					}
					Push((*MapHandle)->WStrEntries.find(*Key) != (*MapHandle)->WStrEntries.end());
					return true;
				}
				if (const auto* Key = std::get_if<FEnumValue>(&Arguments[0]);
					Key && Key->TypeName == Runtime::EnumValueTypeName(*ActiveModule, (*MapHandle)->KeyTypeName))
				{
					Push((*MapHandle)->EnumEntries.find(*Key)
						!= (*MapHandle)->EnumEntries.end());
					return true;
				}
				if (const auto* Key = std::get_if<FHostObjectHandle>(&Arguments[0]);
					Key && *Key
						&& ValueMatchesElementType(
							Arguments[0],
							(*MapHandle)->KeyTypeName,
							SourceLocation))
				{
					Push((*MapHandle)->HostEntries.find(*Key) != (*MapHandle)->HostEntries.end());
					return true;
				}
				ReportError("invalid map key type", SourceLocation);
				return false;
			}
			if (MethodName == "remove")
			{
				if (ArgumentCount != 1)
				{
					ReportError("remove expects exactly 1 argument", SourceLocation);
					return false;
				}
				if (VmDetail::IsSigned32KeyStorageType((*MapHandle)->KeyTypeName))
				{
					std::int32_t Key = 0;
					if (!VmDetail::TryGetSigned32Key(
							Arguments[0], (*MapHandle)->KeyTypeName, Key))
					{
						ReportError("map key type mismatch", SourceLocation);
						return false;
					}
					auto It = (*MapHandle)->IntEntries.find(Key);
					if (It == (*MapHandle)->IntEntries.end())
					{
						Push(false);
						return true;
					}
					FValue Removed = std::move(It->second);
					(*MapHandle)->IntEntries.erase(It);
					if (!ReleaseValue(Removed, SourceLocation))
					{
						return false;
					}
					Push(true);
					return true;
				}
				if ((*MapHandle)->KeyTypeName == "unsigned int")
				{
					const auto* Key = std::get_if<std::uint32_t>(&Arguments[0]);
					if (!Key)
					{
						ReportError("map key type mismatch", SourceLocation);
						return false;
					}
					auto It = (*MapHandle)->UIntEntries.find(*Key);
					if (It == (*MapHandle)->UIntEntries.end())
					{
						Push(false);
						return true;
					}
					FValue Removed = std::move(It->second);
					(*MapHandle)->UIntEntries.erase(It);
					if (!ReleaseValue(Removed, SourceLocation))
					{
						return false;
					}
					Push(true);
					return true;
				}
				if ((*MapHandle)->KeyTypeName == "int64")
				{
					const auto* Key = std::get_if<std::int64_t>(&Arguments[0]);
					if (!Key)
					{
						ReportError("map key type mismatch", SourceLocation);
						return false;
					}
					auto It = (*MapHandle)->Int64Entries.find(*Key);
					if (It == (*MapHandle)->Int64Entries.end())
					{
						Push(false);
						return true;
					}
					FValue Removed = std::move(It->second);
					(*MapHandle)->Int64Entries.erase(It);
					if (!ReleaseValue(Removed, SourceLocation))
					{
						return false;
					}
					Push(true);
					return true;
				}
				if ((*MapHandle)->KeyTypeName == "uint64")
				{
					const auto* Key = std::get_if<std::uint64_t>(&Arguments[0]);
					if (!Key)
					{
						ReportError("map key type mismatch", SourceLocation);
						return false;
					}
					auto It = (*MapHandle)->UInt64Entries.find(*Key);
					if (It == (*MapHandle)->UInt64Entries.end())
					{
						Push(false);
						return true;
					}
					FValue Removed = std::move(It->second);
					(*MapHandle)->UInt64Entries.erase(It);
					if (!ReleaseValue(Removed, SourceLocation))
					{
						return false;
					}
					Push(true);
					return true;
				}
				if ((*MapHandle)->KeyTypeName == "string")
				{
					const auto* Key = std::get_if<std::string>(&Arguments[0]);
					if (!Key)
					{
						ReportError("map key type mismatch", SourceLocation);
						return false;
					}
					auto It = (*MapHandle)->StrEntries.find(*Key);
					if (It == (*MapHandle)->StrEntries.end())
					{
						Push(false);
						return true;
					}
					FValue Removed = std::move(It->second);
					(*MapHandle)->StrEntries.erase(It);
					if (!ReleaseValue(Removed, SourceLocation))
					{
						return false;
					}
					Push(true);
					return true;
				}
				if ((*MapHandle)->KeyTypeName == "wstring")
				{
					const auto* Key = std::get_if<std::u16string>(&Arguments[0]);
					if (!Key)
					{
						ReportError("map key type mismatch", SourceLocation);
						return false;
					}
					auto It = (*MapHandle)->WStrEntries.find(*Key);
					if (It == (*MapHandle)->WStrEntries.end())
					{
						Push(false);
						return true;
					}
					FValue Removed = std::move(It->second);
					(*MapHandle)->WStrEntries.erase(It);
					if (!ReleaseValue(Removed, SourceLocation))
					{
						return false;
					}
					Push(true);
					return true;
				}
				if (const auto* Key = std::get_if<FEnumValue>(&Arguments[0]);
					Key && Key->TypeName == Runtime::EnumValueTypeName(*ActiveModule, (*MapHandle)->KeyTypeName))
				{
					auto It = (*MapHandle)->EnumEntries.find(*Key);
					if (It == (*MapHandle)->EnumEntries.end())
					{
						Push(false);
						return true;
					}
					FValue Removed = std::move(It->second);
					(*MapHandle)->EnumEntries.erase(It);
					if (!ReleaseValue(Removed, SourceLocation))
					{
						return false;
					}
					Push(true);
					return true;
				}
				if (const auto* Key = std::get_if<FHostObjectHandle>(&Arguments[0]);
					Key && *Key
						&& ValueMatchesElementType(
							Arguments[0],
							(*MapHandle)->KeyTypeName,
							SourceLocation))
				{
					auto It = (*MapHandle)->HostEntries.find(*Key);
					if (It == (*MapHandle)->HostEntries.end())
					{
						Push(false);
						return true;
					}
					FValue Removed = std::move(It->second);
					(*MapHandle)->HostEntries.erase(It);
					if (!ReleaseValue(Removed, SourceLocation))
					{
						return false;
					}
					Push(true);
					return true;
				}
				ReportError("invalid map key type", SourceLocation);
				return false;
			}
			if (MethodName == "keys")
			{
				if (ArgumentCount != 0)
				{
					ReportError("keys expects no arguments", SourceLocation);
					return false;
				}
				// Order of keys is unspecified (unordered map).
				const std::string KeysTypeName = "array<" + (*MapHandle)->KeyTypeName + ">";
				FDynamicArrayHandle KeysArray = CreateDynamicArray(KeysTypeName, SourceLocation);
				if (!KeysArray)
				{
					return false;
				}
				// Root completed elements for exception/cancellation cleanup.
				Push(KeysArray);
				if (VmDetail::IsSigned32KeyStorageType((*MapHandle)->KeyTypeName))
				{
					ReserveDynamicArrayStorage(*KeysArray, (*MapHandle)->IntEntries.size());
					for (const auto& Entry : (*MapHandle)->IntEntries)
					{
						if (!AppendDynamicArrayValue(
								*KeysArray,
								VmDetail::MakeSigned32KeyValue(
									Entry.first, (*MapHandle)->KeyTypeName),
								SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*MapHandle)->KeyTypeName == "unsigned int")
				{
					ReserveDynamicArrayStorage(*KeysArray, (*MapHandle)->UIntEntries.size());
					for (const auto& Entry : (*MapHandle)->UIntEntries)
					{
						if (!AppendDynamicArrayValue(*KeysArray, FValue{Entry.first}, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*MapHandle)->KeyTypeName == "int64")
				{
					ReserveDynamicArrayStorage(*KeysArray, (*MapHandle)->Int64Entries.size());
					for (const auto& Entry : (*MapHandle)->Int64Entries)
					{
						if (!AppendDynamicArrayValue(*KeysArray, FValue{Entry.first}, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*MapHandle)->KeyTypeName == "uint64")
				{
					ReserveDynamicArrayStorage(*KeysArray, (*MapHandle)->UInt64Entries.size());
					for (const auto& Entry : (*MapHandle)->UInt64Entries)
					{
						if (!AppendDynamicArrayValue(*KeysArray, FValue{Entry.first}, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*MapHandle)->KeyTypeName == "string")
				{
					ReserveDynamicArrayStorage(*KeysArray, (*MapHandle)->StrEntries.size());
					for (const auto& Entry : (*MapHandle)->StrEntries)
					{
						if (!AppendDynamicArrayValue(*KeysArray, FValue{Entry.first}, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*MapHandle)->KeyTypeName == "wstring")
				{
					ReserveDynamicArrayStorage(*KeysArray, (*MapHandle)->WStrEntries.size());
					for (const auto& Entry : (*MapHandle)->WStrEntries)
					{
						if (!AppendDynamicArrayValue(*KeysArray, FValue{Entry.first}, SourceLocation))
						{
							return false;
						}
					}
				}
				else if (!(*MapHandle)->EnumEntries.empty()
					|| VmDetail::IsScopedEnumType(ActiveModule, (*MapHandle)->KeyTypeName))
				{
					ReserveDynamicArrayStorage(*KeysArray, (*MapHandle)->EnumEntries.size());
					for (const auto& Entry : (*MapHandle)->EnumEntries)
					{
						if (!AppendDynamicArrayValue(*KeysArray, FValue{Entry.first}, SourceLocation))
						{
							return false;
						}
					}
				}
				else if (!VmDetail::IsSigned32KeyStorageType((*MapHandle)->KeyTypeName))
				{
					ReserveDynamicArrayStorage(*KeysArray, (*MapHandle)->HostEntries.size());
					for (const auto& Entry : (*MapHandle)->HostEntries)
					{
						FValue ClonedKey;
						if (!CloneValueOrReport(
								FValue(Entry.first), ClonedKey, SourceLocation))
						{
							return false;
						}
						if (!AppendDynamicArrayValue(*KeysArray, ClonedKey, SourceLocation))
						{
							return false;
						}
					}
				}
				else
				{
					ReportError("invalid map key type", SourceLocation);
					return false;
				}
				return true;
			}
			if (MethodName == "values")
			{
				if (ArgumentCount != 0)
				{
					ReportError("values expects no arguments", SourceLocation);
					return false;
				}
				const std::string ValuesTypeName =
					"array<" + (*MapHandle)->ValueTypeName + ">";
				FDynamicArrayHandle ValuesArray =
					CreateDynamicArray(ValuesTypeName, SourceLocation);
				if (!ValuesArray)
				{
					return false;
				}
				// Root completed elements for exception/cancellation cleanup.
				Push(ValuesArray);
				if (VmDetail::IsSigned32KeyStorageType((*MapHandle)->KeyTypeName))
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*MapHandle)->IntEntries.size());
					for (const auto& Entry : (*MapHandle)->IntEntries)
					{
						if (!AppendDynamicArrayValue(*ValuesArray, Entry.second, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*MapHandle)->KeyTypeName == "unsigned int")
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*MapHandle)->UIntEntries.size());
					for (const auto& Entry : (*MapHandle)->UIntEntries)
					{
						if (!AppendDynamicArrayValue(
								*ValuesArray, Entry.second, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*MapHandle)->KeyTypeName == "int64")
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*MapHandle)->Int64Entries.size());
					for (const auto& Entry : (*MapHandle)->Int64Entries)
					{
						if (!AppendDynamicArrayValue(*ValuesArray, Entry.second, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*MapHandle)->KeyTypeName == "uint64")
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*MapHandle)->UInt64Entries.size());
					for (const auto& Entry : (*MapHandle)->UInt64Entries)
					{
						if (!AppendDynamicArrayValue(*ValuesArray, Entry.second, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*MapHandle)->KeyTypeName == "string")
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*MapHandle)->StrEntries.size());
					for (const auto& Entry : (*MapHandle)->StrEntries)
					{
						if (!AppendDynamicArrayValue(*ValuesArray, Entry.second, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*MapHandle)->KeyTypeName == "wstring")
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*MapHandle)->WStrEntries.size());
					for (const auto& Entry : (*MapHandle)->WStrEntries)
					{
						if (!AppendDynamicArrayValue(*ValuesArray, Entry.second, SourceLocation))
						{
							return false;
						}
					}
				}
				else if (!(*MapHandle)->EnumEntries.empty()
					|| VmDetail::IsScopedEnumType(ActiveModule, (*MapHandle)->KeyTypeName))
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*MapHandle)->EnumEntries.size());
					for (const auto& Entry : (*MapHandle)->EnumEntries)
					{
						if (!AppendDynamicArrayValue(
								*ValuesArray, Entry.second, SourceLocation))
						{
							return false;
						}
					}
				}
				else if (!VmDetail::IsSigned32KeyStorageType((*MapHandle)->KeyTypeName))
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*MapHandle)->HostEntries.size());
					for (const auto& Entry : (*MapHandle)->HostEntries)
					{
						if (!AppendDynamicArrayValue(*ValuesArray, Entry.second, SourceLocation))
						{
							return false;
						}
					}
				}
				else
				{
					ReportError("invalid map key type", SourceLocation);
					return false;
				}
				return true;
			}
			if (MethodName == "entries")
			{
				if (ArgumentCount != 0)
				{
					ReportError("entries expects no arguments", SourceLocation);
					return false;
				}
				const std::string PairTypeName = Runtime::MakePairTypeName(
					(*MapHandle)->KeyTypeName,
					(*MapHandle)->ValueTypeName);
				std::uint16_t PairTypeIndex = FindTypeIndex(PairTypeName);
				if (PairTypeIndex == static_cast<std::uint16_t>(-1))
				{
					for (std::size_t Index = 0; Index < ActiveModule->Types.size(); ++Index)
					{
						const FBytecodeTypeDesc& TypeDesc = ActiveModule->Types[Index];
						if (TypeDesc.Fields.size() < 2)
						{
							continue;
						}
						const std::string& TypeName = TypeDesc.Name;
						const bool bPairName =
							TypeName.rfind("pair#", 0) == 0
								|| TypeName.find("\x1Fpair#") != std::string::npos
								|| Runtime::IsPairTypeName(TypeName);
						if (!bPairName)
						{
							continue;
						}
						if (Runtime::EnumValueTypeName(*ActiveModule, TypeDesc.Fields[0].TypeName) == Runtime::EnumValueTypeName(*ActiveModule, (*MapHandle)->KeyTypeName)
							&& Runtime::EnumValueTypeName(*ActiveModule, TypeDesc.Fields[1].TypeName) == Runtime::EnumValueTypeName(*ActiveModule, (*MapHandle)->ValueTypeName))
						{
							PairTypeIndex = static_cast<std::uint16_t>(Index);
							break;
						}
					}
				}
				if (PairTypeIndex == static_cast<std::uint16_t>(-1)
					|| PairTypeIndex >= ActiveModule->Types.size()
					|| ActiveModule->Types[PairTypeIndex].Fields.size() < 2)
				{
					ReportError("entries requires pair type", SourceLocation);
					return false;
				}
				const std::string EntriesTypeName = "array<" + PairTypeName + ">";
				FDynamicArrayHandle EntriesArray =
					CreateDynamicArray(EntriesTypeName, SourceLocation);
				if (!EntriesArray)
				{
					return false;
				}
				// Root completed elements for exception/cancellation cleanup.
				Push(EntriesArray);
				const auto PushEntry =
					[&](FValue Key, FValue Value) -> bool
					{
						FObjectHandle PairObject =
								CreateObject(PairTypeIndex, SourceLocation, true);
						if (!PairObject)
						{
							return false;
						}
						PairObject->Fields[0] = std::move(Key);
						PairObject->Fields[1] = std::move(Value);
						PairObject->bConstructionIncomplete = false;
						EntriesArray->Elements.push_back(std::move(PairObject));
						return true;
					};
				if (VmDetail::IsSigned32KeyStorageType((*MapHandle)->KeyTypeName))
				{
					EntriesArray->Elements.reserve((*MapHandle)->IntEntries.size());
					for (const auto& Entry : (*MapHandle)->IntEntries)
					{
						{
							FValue ClonedEntry;
							if (!CloneValueOrReport(Entry.second, ClonedEntry, SourceLocation)
								|| !PushEntry(
									VmDetail::MakeSigned32KeyValue(
										Entry.first, (*MapHandle)->KeyTypeName),
									std::move(ClonedEntry)))
							{
								return false;
							}
						}
					}
				}
				else if ((*MapHandle)->KeyTypeName == "unsigned int")
				{
					EntriesArray->Elements.reserve((*MapHandle)->UIntEntries.size());
					for (const auto& Entry : (*MapHandle)->UIntEntries)
					{
						FValue ClonedEntry;
						if (!CloneValueOrReport(Entry.second, ClonedEntry, SourceLocation)
							|| !PushEntry(Entry.first, std::move(ClonedEntry)))
						{
							return false;
						}
					}
				}
				else if ((*MapHandle)->KeyTypeName == "int64")
				{
					EntriesArray->Elements.reserve((*MapHandle)->Int64Entries.size());
					for (const auto& Entry : (*MapHandle)->Int64Entries)
					{
						FValue ClonedEntry;
						if (!CloneValueOrReport(Entry.second, ClonedEntry, SourceLocation)
							|| !PushEntry(Entry.first, std::move(ClonedEntry)))
						{
							return false;
						}
					}
				}
				else if ((*MapHandle)->KeyTypeName == "uint64")
				{
					EntriesArray->Elements.reserve((*MapHandle)->UInt64Entries.size());
					for (const auto& Entry : (*MapHandle)->UInt64Entries)
					{
						FValue ClonedEntry;
						if (!CloneValueOrReport(Entry.second, ClonedEntry, SourceLocation)
							|| !PushEntry(Entry.first, std::move(ClonedEntry)))
						{
							return false;
						}
					}
				}
				else if ((*MapHandle)->KeyTypeName == "string")
				{
					EntriesArray->Elements.reserve((*MapHandle)->StrEntries.size());
					for (const auto& Entry : (*MapHandle)->StrEntries)
					{
						{
							FValue ClonedEntry;
							if (!CloneValueOrReport(Entry.second, ClonedEntry, SourceLocation)
								|| !PushEntry(Entry.first, std::move(ClonedEntry)))
							{
								return false;
							}
						}
					}
				}
				else if ((*MapHandle)->KeyTypeName == "wstring")
				{
					EntriesArray->Elements.reserve((*MapHandle)->WStrEntries.size());
					for (const auto& Entry : (*MapHandle)->WStrEntries)
					{
						{
							FValue ClonedEntry;
							if (!CloneValueOrReport(Entry.second, ClonedEntry, SourceLocation)
								|| !PushEntry(Entry.first, std::move(ClonedEntry)))
							{
								return false;
							}
						}
					}
				}
				else if (!(*MapHandle)->EnumEntries.empty()
					|| VmDetail::IsScopedEnumType(ActiveModule, (*MapHandle)->KeyTypeName))
				{
					EntriesArray->Elements.reserve((*MapHandle)->EnumEntries.size());
					for (const auto& Entry : (*MapHandle)->EnumEntries)
					{
						FValue ClonedEntry;
						if (!CloneValueOrReport(Entry.second, ClonedEntry, SourceLocation)
							|| !PushEntry(Entry.first, std::move(ClonedEntry)))
						{
							return false;
						}
					}
				}
				else if (!VmDetail::IsSigned32KeyStorageType((*MapHandle)->KeyTypeName))
				{
					EntriesArray->Elements.reserve((*MapHandle)->HostEntries.size());
					for (const auto& Entry : (*MapHandle)->HostEntries)
					{
						FValue ClonedKey;
						FValue ClonedEntry;
						if (!CloneValueOrReport(
								FValue(Entry.first), ClonedKey, SourceLocation)
							|| !CloneValueOrReport(
								Entry.second, ClonedEntry, SourceLocation)
							|| !PushEntry(
								std::move(ClonedKey), std::move(ClonedEntry)))
						{
							return false;
						}
					}
				}
				else
				{
					ReportError("invalid map key type", SourceLocation);
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
				if (!ReleaseMapValues(**MapHandle, SourceLocation))
				{
					return false;
				}
				Push(ThisValue);
				return true;
			}
			ReportError("unknown method: " + MethodName, SourceLocation);
			return false;
		}
