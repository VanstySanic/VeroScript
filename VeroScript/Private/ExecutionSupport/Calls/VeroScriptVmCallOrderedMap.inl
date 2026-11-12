// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		if (const auto* OrderedMapHandle = std::get_if<FOrderedMapHandle>(&ThisValue))
		{
			if (!*OrderedMapHandle)
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
					ReserveOrderedMapStorage(
						**OrderedMapHandle,
						*Count,
						VmDetail::IsScopedEnumType(
							ActiveModule, (*OrderedMapHandle)->KeyTypeName));
				}
				catch (const std::exception&)
				{
					ReportError("ordered map reserve failed", SourceLocation);
					return false;
				}
				Push(ThisValue);
				return true;
			}
			const bool bOrderedMultiSet =
				Runtime::IsOrderedMultiSetTypeName((*OrderedMapHandle)->TypeName);
			if (bOrderedMultiSet
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
				const auto VisitBucket =
					[&](auto& Entries, auto& KeyOrder, const auto& Key)
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
								KeyOrder.push_back(Key);
							}
							auto* Occurrences =
									std::get_if<FDynamicArrayHandle>(&It->second);
							if (!Occurrences || !*Occurrences)
							{
								ReportError(
										"invalid ordered_multiset occurrence storage",
										SourceLocation);
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
											"invalid ordered_multiset occurrence storage",
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
						auto* Occurrences =
								std::get_if<FDynamicArrayHandle>(&It->second);
						const std::size_t OccurrenceCount = Occurrences && *Occurrences
								? ((*Occurrences)->HasScalarElements()
									? (*Occurrences)->ScalarSize()
									: (*Occurrences)->Elements.size())
								: 0u;
						if (OccurrenceCount == 0)
						{
							ReportError(
									"invalid ordered_multiset occurrence storage",
									SourceLocation);
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
							KeyOrder.erase(
									std::remove(KeyOrder.begin(), KeyOrder.end(), Key),
									KeyOrder.end());
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
						Arguments[0], (*OrderedMapHandle)->KeyTypeName, Signed32Key))
				{
					VisitBucket(
						(*OrderedMapHandle)->IntEntries,
						(*OrderedMapHandle)->IntKeyOrder,
						Signed32Key);
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "unsigned int")
				{
					if (const auto* Key = std::get_if<std::uint32_t>(&Arguments[0]))
					{
						VisitBucket(
							(*OrderedMapHandle)->UIntEntries,
							(*OrderedMapHandle)->UIntKeyOrder,
							*Key);
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "int64")
				{
					if (const auto* Key = std::get_if<std::int64_t>(&Arguments[0]))
					{
						VisitBucket(
							(*OrderedMapHandle)->Int64Entries,
							(*OrderedMapHandle)->Int64KeyOrder,
							*Key);
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "uint64")
				{
					if (const auto* Key = std::get_if<std::uint64_t>(&Arguments[0]))
					{
						VisitBucket(
							(*OrderedMapHandle)->UInt64Entries,
							(*OrderedMapHandle)->UInt64KeyOrder,
							*Key);
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "string")
				{
					if (const auto* Key = std::get_if<std::string>(&Arguments[0]))
					{
						VisitBucket(
							(*OrderedMapHandle)->StrEntries,
							(*OrderedMapHandle)->StrKeyOrder,
							*Key);
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "wstring")
				{
					if (const auto* Key = std::get_if<std::u16string>(&Arguments[0]))
					{
						VisitBucket(
							(*OrderedMapHandle)->WStrEntries,
							(*OrderedMapHandle)->WStrKeyOrder,
							*Key);
					}
				}
				else if (const auto* EnumKey = std::get_if<FEnumValue>(&Arguments[0]);
					EnumKey && EnumKey->TypeName == Runtime::EnumValueTypeName(*ActiveModule, (*OrderedMapHandle)->KeyTypeName))
				{
					VisitBucket(
						(*OrderedMapHandle)->EnumEntries,
						(*OrderedMapHandle)->EnumKeyOrder,
						*EnumKey);
				}
				else if (const auto* HostKey = std::get_if<FHostObjectHandle>(&Arguments[0]);
					HostKey && *HostKey
						&& ValueMatchesElementType(
							Arguments[0],
							(*OrderedMapHandle)->KeyTypeName,
							SourceLocation))
				{
					VisitBucket(
						(*OrderedMapHandle)->HostEntries,
						(*OrderedMapHandle)->HostKeyOrder,
						*HostKey);
				}
				if (!bMatchedKey)
				{
					ReportError("ordered_multiset element type mismatch", SourceLocation);
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
			if (bOrderedMultiSet && MethodName == "values")
			{
				if (ArgumentCount != 0)
				{
					ReportError("values expects no arguments", SourceLocation);
					return false;
				}
				std::string ElementType;
				if (!Runtime::ParseMultiSetElementType(
						(*OrderedMapHandle)->TypeName,
						ElementType))
				{
					ReportError("invalid ordered_multiset type", SourceLocation);
					return false;
				}
				FDynamicArrayHandle Values =
					CreateDynamicArray("array<" + ElementType + ">", SourceLocation);
				if (!Values)
				{
					return false;
				}
				const auto AppendOrdered =
					[&](const auto& Entries, const auto& KeyOrder) -> bool
					{
						for (const auto& Key : KeyOrder)
						{
							const auto It = Entries.find(Key);
							if (It == Entries.end())
							{
								ReportError(
										"invalid ordered_multiset key order",
										SourceLocation);
								return false;
							}
							const auto* Occurrences =
									std::get_if<FDynamicArrayHandle>(&It->second);
							if (!Occurrences || !*Occurrences)
							{
								ReportError(
										"invalid ordered_multiset occurrence storage",
										SourceLocation);
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
								using TKey = std::decay_t<decltype(Key)>;
								if constexpr (std::is_same_v<TKey, std::int32_t>)
								{
									ElementValue = VmDetail::MakeSigned32KeyValue(
											Key, (*OrderedMapHandle)->KeyTypeName);
								}
								else
								{
									ElementValue = FValue(Key);
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
					(*OrderedMapHandle)->KeyTypeName)
					? AppendOrdered(
						(*OrderedMapHandle)->IntEntries,
						(*OrderedMapHandle)->IntKeyOrder)
					: (*OrderedMapHandle)->KeyTypeName == "unsigned int"
						? AppendOrdered(
							(*OrderedMapHandle)->UIntEntries,
							(*OrderedMapHandle)->UIntKeyOrder)
						: (*OrderedMapHandle)->KeyTypeName == "int64"
							? AppendOrdered(
								(*OrderedMapHandle)->Int64Entries,
								(*OrderedMapHandle)->Int64KeyOrder)
							: (*OrderedMapHandle)->KeyTypeName == "uint64"
								? AppendOrdered(
									(*OrderedMapHandle)->UInt64Entries,
									(*OrderedMapHandle)->UInt64KeyOrder)
								: (*OrderedMapHandle)->KeyTypeName == "string"
									? AppendOrdered(
										(*OrderedMapHandle)->StrEntries,
										(*OrderedMapHandle)->StrKeyOrder)
									: (*OrderedMapHandle)->KeyTypeName == "wstring"
										? AppendOrdered(
											(*OrderedMapHandle)->WStrEntries,
											(*OrderedMapHandle)->WStrKeyOrder)
										: !(*OrderedMapHandle)->EnumKeyOrder.empty()
											? AppendOrdered(
												(*OrderedMapHandle)->EnumEntries,
												(*OrderedMapHandle)->EnumKeyOrder)
											: AppendOrdered(
												(*OrderedMapHandle)->HostEntries,
												(*OrderedMapHandle)->HostKeyOrder);
				if (!bAppended)
				{
					return false;
				}
				Push(Values);
				return true;
			}
			if (MethodName == "add"
				&& Runtime::IsOrderedMultiMapTypeName((*OrderedMapHandle)->TypeName))
			{
				if (ArgumentCount != 2)
				{
					ReportError("add expects exactly 2 arguments", SourceLocation);
					return false;
				}
				std::string ElementTypeName;
				if (!Runtime::ParseDynamicArrayElementType(
						(*OrderedMapHandle)->ValueTypeName,
						ElementTypeName)
					|| !ValueMatchesElementType(
						Arguments[1],
						ElementTypeName,
						SourceLocation))
				{
					ReportError("ordered_multimap value type mismatch", SourceLocation);
					return false;
				}
				const auto AddValue =
					[&](auto& Entries, auto& KeyOrder, const auto& Key) -> bool
					{
						auto It = Entries.find(Key);
						if (It == Entries.end())
						{
							FDynamicArrayHandle Values = CreateDynamicArray(
									(*OrderedMapHandle)->ValueTypeName,
									SourceLocation);
							if (!Values)
							{
								return false;
							}
							It = Entries.emplace(Key, FValue(std::move(Values))).first;
							KeyOrder.push_back(Key);
						}
						auto* Values = std::get_if<FDynamicArrayHandle>(&It->second);
						if (!Values || !*Values)
						{
							ReportError(
									"invalid ordered_multimap value storage",
									SourceLocation);
							return false;
						}
						return AppendDynamicArrayValue(
								**Values, Arguments[1], SourceLocation);
					};
				bool bAdded = false;
				std::int32_t Signed32Key = 0;
				if (VmDetail::TryGetSigned32Key(
						Arguments[0], (*OrderedMapHandle)->KeyTypeName, Signed32Key))
				{
					bAdded = AddValue(
						(*OrderedMapHandle)->IntEntries,
						(*OrderedMapHandle)->IntKeyOrder,
						Signed32Key);
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "unsigned int")
				{
					if (const auto* Key = std::get_if<std::uint32_t>(&Arguments[0]))
					{
						bAdded = AddValue(
							(*OrderedMapHandle)->UIntEntries,
							(*OrderedMapHandle)->UIntKeyOrder,
							*Key);
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "int64")
				{
					if (const auto* Key = std::get_if<std::int64_t>(&Arguments[0]))
					{
						bAdded = AddValue(
							(*OrderedMapHandle)->Int64Entries,
							(*OrderedMapHandle)->Int64KeyOrder,
							*Key);
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "uint64")
				{
					if (const auto* Key = std::get_if<std::uint64_t>(&Arguments[0]))
					{
						bAdded = AddValue(
							(*OrderedMapHandle)->UInt64Entries,
							(*OrderedMapHandle)->UInt64KeyOrder,
							*Key);
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "string")
				{
					if (const auto* Key = std::get_if<std::string>(&Arguments[0]))
					{
						bAdded = AddValue(
							(*OrderedMapHandle)->StrEntries,
							(*OrderedMapHandle)->StrKeyOrder,
							*Key);
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "wstring")
				{
					if (const auto* Key = std::get_if<std::u16string>(&Arguments[0]))
					{
						bAdded = AddValue(
							(*OrderedMapHandle)->WStrEntries,
							(*OrderedMapHandle)->WStrKeyOrder,
							*Key);
					}
				}
				else if (const auto* EnumKey = std::get_if<FEnumValue>(&Arguments[0]);
					EnumKey && EnumKey->TypeName == Runtime::EnumValueTypeName(*ActiveModule, (*OrderedMapHandle)->KeyTypeName))
				{
					bAdded = AddValue(
						(*OrderedMapHandle)->EnumEntries,
						(*OrderedMapHandle)->EnumKeyOrder,
						*EnumKey);
				}
				else if (const auto* HostKey = std::get_if<FHostObjectHandle>(&Arguments[0]);
					HostKey && *HostKey
						&& ValueMatchesElementType(
							Arguments[0],
							(*OrderedMapHandle)->KeyTypeName,
							SourceLocation))
				{
					bAdded = AddValue(
						(*OrderedMapHandle)->HostEntries,
						(*OrderedMapHandle)->HostKeyOrder,
						*HostKey);
				}
				else
				{
					ReportError("ordered_multimap key type mismatch", SourceLocation);
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
				if (VmDetail::IsSigned32KeyStorageType((*OrderedMapHandle)->KeyTypeName))
				{
					std::int32_t Key = 0;
					if (!VmDetail::TryGetSigned32Key(
							Arguments[0], (*OrderedMapHandle)->KeyTypeName, Key))
					{
						ReportError("ordered_map key type mismatch", SourceLocation);
						return false;
					}
					Push(
						(*OrderedMapHandle)->IntEntries.find(Key)
							!= (*OrderedMapHandle)->IntEntries.end());
					return true;
				}
				if ((*OrderedMapHandle)->KeyTypeName == "unsigned int")
				{
					const auto* Key = std::get_if<std::uint32_t>(&Arguments[0]);
					if (!Key)
					{
						ReportError("ordered_map key type mismatch", SourceLocation);
						return false;
					}
					Push((*OrderedMapHandle)->UIntEntries.find(*Key)
						!= (*OrderedMapHandle)->UIntEntries.end());
					return true;
				}
				if ((*OrderedMapHandle)->KeyTypeName == "int64")
				{
					const auto* Key = std::get_if<std::int64_t>(&Arguments[0]);
					if (!Key)
					{
						ReportError("ordered_map key type mismatch", SourceLocation);
						return false;
					}
					Push((*OrderedMapHandle)->Int64Entries.find(*Key)
						!= (*OrderedMapHandle)->Int64Entries.end());
					return true;
				}
				if ((*OrderedMapHandle)->KeyTypeName == "uint64")
				{
					const auto* Key = std::get_if<std::uint64_t>(&Arguments[0]);
					if (!Key)
					{
						ReportError("ordered_map key type mismatch", SourceLocation);
						return false;
					}
					Push((*OrderedMapHandle)->UInt64Entries.find(*Key)
						!= (*OrderedMapHandle)->UInt64Entries.end());
					return true;
				}
				if ((*OrderedMapHandle)->KeyTypeName == "string")
				{
					const auto* Key = std::get_if<std::string>(&Arguments[0]);
					if (!Key)
					{
						ReportError("ordered_map key type mismatch", SourceLocation);
						return false;
					}
					Push(
						(*OrderedMapHandle)->StrEntries.find(*Key)
							!= (*OrderedMapHandle)->StrEntries.end());
					return true;
				}
				if ((*OrderedMapHandle)->KeyTypeName == "wstring")
				{
					const auto* Key = std::get_if<std::u16string>(&Arguments[0]);
					if (!Key)
					{
						ReportError("ordered_map key type mismatch", SourceLocation);
						return false;
					}
					Push(
						(*OrderedMapHandle)->WStrEntries.find(*Key)
							!= (*OrderedMapHandle)->WStrEntries.end());
					return true;
				}
				if (const auto* Key = std::get_if<FEnumValue>(&Arguments[0]);
					Key && Key->TypeName == Runtime::EnumValueTypeName(*ActiveModule, (*OrderedMapHandle)->KeyTypeName))
				{
					Push((*OrderedMapHandle)->EnumEntries.find(*Key)
						!= (*OrderedMapHandle)->EnumEntries.end());
					return true;
				}
				if (const auto* Key = std::get_if<FHostObjectHandle>(&Arguments[0]);
					Key && *Key
						&& ValueMatchesElementType(
							Arguments[0],
							(*OrderedMapHandle)->KeyTypeName,
							SourceLocation))
				{
					Push(
						(*OrderedMapHandle)->HostEntries.find(*Key)
						!= (*OrderedMapHandle)->HostEntries.end());
					return true;
				}
				ReportError("invalid ordered_map key type", SourceLocation);
				return false;
			}
			if (MethodName == "remove")
			{
				if (ArgumentCount != 1)
				{
					ReportError("remove expects exactly 1 argument", SourceLocation);
					return false;
				}
				if (VmDetail::IsSigned32KeyStorageType((*OrderedMapHandle)->KeyTypeName))
				{
					std::int32_t Key = 0;
					if (!VmDetail::TryGetSigned32Key(
							Arguments[0], (*OrderedMapHandle)->KeyTypeName, Key))
					{
						ReportError("ordered_map key type mismatch", SourceLocation);
						return false;
					}
					auto It = (*OrderedMapHandle)->IntEntries.find(Key);
					if (It == (*OrderedMapHandle)->IntEntries.end())
					{
						Push(false);
						return true;
					}
					FValue Removed = std::move(It->second);
					(*OrderedMapHandle)->IntEntries.erase(It);
					auto& Order = (*OrderedMapHandle)->IntKeyOrder;
					Order.erase(std::remove(Order.begin(), Order.end(), Key), Order.end());
					if (!ReleaseValue(Removed, SourceLocation))
					{
						return false;
					}
					Push(true);
					return true;
				}
				if ((*OrderedMapHandle)->KeyTypeName == "unsigned int")
				{
					const auto* Key = std::get_if<std::uint32_t>(&Arguments[0]);
					if (!Key)
					{
						ReportError("ordered_map key type mismatch", SourceLocation);
						return false;
					}
					auto It = (*OrderedMapHandle)->UIntEntries.find(*Key);
					if (It == (*OrderedMapHandle)->UIntEntries.end())
					{
						Push(false);
						return true;
					}
					FValue Removed = std::move(It->second);
					(*OrderedMapHandle)->UIntEntries.erase(It);
					auto& Order = (*OrderedMapHandle)->UIntKeyOrder;
					Order.erase(std::remove(Order.begin(), Order.end(), *Key), Order.end());
					if (!ReleaseValue(Removed, SourceLocation))
					{
						return false;
					}
					Push(true);
					return true;
				}
				if ((*OrderedMapHandle)->KeyTypeName == "int64")
				{
					const auto* Key = std::get_if<std::int64_t>(&Arguments[0]);
					if (!Key)
					{
						ReportError("ordered_map key type mismatch", SourceLocation);
						return false;
					}
					auto It = (*OrderedMapHandle)->Int64Entries.find(*Key);
					if (It == (*OrderedMapHandle)->Int64Entries.end())
					{
						Push(false);
						return true;
					}
					FValue Removed = std::move(It->second);
					(*OrderedMapHandle)->Int64Entries.erase(It);
					auto& Order = (*OrderedMapHandle)->Int64KeyOrder;
					Order.erase(std::remove(Order.begin(), Order.end(), *Key), Order.end());
					if (!ReleaseValue(Removed, SourceLocation))
					{
						return false;
					}
					Push(true);
					return true;
				}
				if ((*OrderedMapHandle)->KeyTypeName == "uint64")
				{
					const auto* Key = std::get_if<std::uint64_t>(&Arguments[0]);
					if (!Key)
					{
						ReportError("ordered_map key type mismatch", SourceLocation);
						return false;
					}
					auto It = (*OrderedMapHandle)->UInt64Entries.find(*Key);
					if (It == (*OrderedMapHandle)->UInt64Entries.end())
					{
						Push(false);
						return true;
					}
					FValue Removed = std::move(It->second);
					(*OrderedMapHandle)->UInt64Entries.erase(It);
					auto& Order = (*OrderedMapHandle)->UInt64KeyOrder;
					Order.erase(std::remove(Order.begin(), Order.end(), *Key), Order.end());
					if (!ReleaseValue(Removed, SourceLocation))
					{
						return false;
					}
					Push(true);
					return true;
				}
				if ((*OrderedMapHandle)->KeyTypeName == "string")
				{
					const auto* Key = std::get_if<std::string>(&Arguments[0]);
					if (!Key)
					{
						ReportError("ordered_map key type mismatch", SourceLocation);
						return false;
					}
					auto It = (*OrderedMapHandle)->StrEntries.find(*Key);
					if (It == (*OrderedMapHandle)->StrEntries.end())
					{
						Push(false);
						return true;
					}
					FValue Removed = std::move(It->second);
					(*OrderedMapHandle)->StrEntries.erase(It);
					auto& Order = (*OrderedMapHandle)->StrKeyOrder;
					Order.erase(
						std::remove(Order.begin(), Order.end(), *Key),
						Order.end());
					if (!ReleaseValue(Removed, SourceLocation))
					{
						return false;
					}
					Push(true);
					return true;
				}
				if ((*OrderedMapHandle)->KeyTypeName == "wstring")
				{
					const auto* Key = std::get_if<std::u16string>(&Arguments[0]);
					if (!Key)
					{
						ReportError("ordered_map key type mismatch", SourceLocation);
						return false;
					}
					auto It = (*OrderedMapHandle)->WStrEntries.find(*Key);
					if (It == (*OrderedMapHandle)->WStrEntries.end())
					{
						Push(false);
						return true;
					}
					FValue Removed = std::move(It->second);
					(*OrderedMapHandle)->WStrEntries.erase(It);
					auto& Order = (*OrderedMapHandle)->WStrKeyOrder;
					Order.erase(
						std::remove(Order.begin(), Order.end(), *Key),
						Order.end());
					if (!ReleaseValue(Removed, SourceLocation))
					{
						return false;
					}
					Push(true);
					return true;
				}
				if (const auto* Key = std::get_if<FEnumValue>(&Arguments[0]);
					Key && Key->TypeName == Runtime::EnumValueTypeName(*ActiveModule, (*OrderedMapHandle)->KeyTypeName))
				{
					auto It = (*OrderedMapHandle)->EnumEntries.find(*Key);
					if (It == (*OrderedMapHandle)->EnumEntries.end())
					{
						Push(false);
						return true;
					}
					FValue Removed = std::move(It->second);
					(*OrderedMapHandle)->EnumEntries.erase(It);
					auto& Order = (*OrderedMapHandle)->EnumKeyOrder;
					Order.erase(std::remove(Order.begin(), Order.end(), *Key), Order.end());
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
							(*OrderedMapHandle)->KeyTypeName,
							SourceLocation))
				{
					auto It = (*OrderedMapHandle)->HostEntries.find(*Key);
					if (It == (*OrderedMapHandle)->HostEntries.end())
					{
						Push(false);
						return true;
					}
					FValue Removed = std::move(It->second);
					const FHostObjectHandle RemovedKey = It->first;
					(*OrderedMapHandle)->HostEntries.erase(It);
					auto& Order = (*OrderedMapHandle)->HostKeyOrder;
					const FHostObjectKeyEqual KeysEqual;
					Order.erase(
						std::remove_if(
							Order.begin(),
							Order.end(),
							[&RemovedKey, &KeysEqual](const FHostObjectHandle& Candidate)
							{
								return KeysEqual(Candidate, RemovedKey);
							}),
						Order.end());
					if (!ReleaseValue(Removed, SourceLocation))
					{
						return false;
					}
					Push(true);
					return true;
				}
				ReportError("invalid ordered_map key type", SourceLocation);
				return false;
			}
			if (MethodName == "keys")
			{
				if (ArgumentCount != 0)
				{
					ReportError("keys expects no arguments", SourceLocation);
					return false;
				}
				const std::string KeysTypeName =
					"array<" + (*OrderedMapHandle)->KeyTypeName + ">";
				FDynamicArrayHandle KeysArray = CreateDynamicArray(KeysTypeName, SourceLocation);
				if (!KeysArray)
				{
					return false;
				}
				// Root completed elements for exception/cancellation cleanup.
				Push(KeysArray);
				if (VmDetail::IsSigned32KeyStorageType((*OrderedMapHandle)->KeyTypeName))
				{
					ReserveDynamicArrayStorage(*KeysArray, (*OrderedMapHandle)->IntKeyOrder.size());
					for (const std::int32_t Key : (*OrderedMapHandle)->IntKeyOrder)
					{
						if (!AppendDynamicArrayValue(
								*KeysArray,
								VmDetail::MakeSigned32KeyValue(
									Key, (*OrderedMapHandle)->KeyTypeName),
								SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "unsigned int")
				{
					ReserveDynamicArrayStorage(*KeysArray, (*OrderedMapHandle)->UIntKeyOrder.size());
					for (const std::uint32_t Key : (*OrderedMapHandle)->UIntKeyOrder)
					{
						if (!AppendDynamicArrayValue(*KeysArray, FValue{Key}, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "int64")
				{
					ReserveDynamicArrayStorage(*KeysArray, (*OrderedMapHandle)->Int64KeyOrder.size());
					for (const std::int64_t Key : (*OrderedMapHandle)->Int64KeyOrder)
					{
						if (!AppendDynamicArrayValue(*KeysArray, FValue{Key}, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "uint64")
				{
					ReserveDynamicArrayStorage(*KeysArray, (*OrderedMapHandle)->UInt64KeyOrder.size());
					for (const std::uint64_t Key : (*OrderedMapHandle)->UInt64KeyOrder)
					{
						if (!AppendDynamicArrayValue(*KeysArray, FValue{Key}, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "string")
				{
					ReserveDynamicArrayStorage(*KeysArray, (*OrderedMapHandle)->StrKeyOrder.size());
					for (const std::string& Key : (*OrderedMapHandle)->StrKeyOrder)
					{
						if (!AppendDynamicArrayValue(*KeysArray, FValue{Key}, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "wstring")
				{
					ReserveDynamicArrayStorage(*KeysArray, (*OrderedMapHandle)->WStrKeyOrder.size());
					for (const std::u16string& Key : (*OrderedMapHandle)->WStrKeyOrder)
					{
						if (!AppendDynamicArrayValue(*KeysArray, FValue{Key}, SourceLocation))
						{
							return false;
						}
					}
				}
				else if (!(*OrderedMapHandle)->EnumEntries.empty()
					|| VmDetail::IsScopedEnumType(ActiveModule, (*OrderedMapHandle)->KeyTypeName))
				{
					ReserveDynamicArrayStorage(*KeysArray, (*OrderedMapHandle)->EnumKeyOrder.size());
					for (const FEnumValue& Key : (*OrderedMapHandle)->EnumKeyOrder)
					{
						if (!AppendDynamicArrayValue(*KeysArray, FValue{Key}, SourceLocation))
						{
							return false;
						}
					}
				}
				else if (!VmDetail::IsSigned32KeyStorageType((*OrderedMapHandle)->KeyTypeName))
				{
					ReserveDynamicArrayStorage(*KeysArray, (*OrderedMapHandle)->HostKeyOrder.size());
					for (const FHostObjectHandle& Key : (*OrderedMapHandle)->HostKeyOrder)
					{
						FValue ClonedKey;
						if (!CloneValueOrReport(FValue(Key), ClonedKey, SourceLocation))
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
					ReportError("invalid ordered_map key type", SourceLocation);
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
					"array<" + (*OrderedMapHandle)->ValueTypeName + ">";
				FDynamicArrayHandle ValuesArray =
					CreateDynamicArray(ValuesTypeName, SourceLocation);
				if (!ValuesArray)
				{
					return false;
				}
				// Root completed elements for exception/cancellation cleanup.
				Push(ValuesArray);
				if (VmDetail::IsSigned32KeyStorageType((*OrderedMapHandle)->KeyTypeName))
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*OrderedMapHandle)->IntKeyOrder.size());
					for (const std::int32_t Key : (*OrderedMapHandle)->IntKeyOrder)
					{
						const auto It = (*OrderedMapHandle)->IntEntries.find(Key);
						if (It == (*OrderedMapHandle)->IntEntries.end())
						{
							ReportError("ordered_map key order desync", SourceLocation);
							return false;
						}
						if (!AppendDynamicArrayValue(*ValuesArray, It->second, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "unsigned int")
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*OrderedMapHandle)->UIntKeyOrder.size());
					for (const std::uint32_t Key : (*OrderedMapHandle)->UIntKeyOrder)
					{
						const auto It = (*OrderedMapHandle)->UIntEntries.find(Key);
						if (It == (*OrderedMapHandle)->UIntEntries.end())
						{
							ReportError("ordered_map key order desync", SourceLocation);
							return false;
						}
						if (!AppendDynamicArrayValue(
								*ValuesArray, It->second, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "int64")
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*OrderedMapHandle)->Int64KeyOrder.size());
					for (const std::int64_t Key : (*OrderedMapHandle)->Int64KeyOrder)
					{
						const auto It = (*OrderedMapHandle)->Int64Entries.find(Key);
						if (It == (*OrderedMapHandle)->Int64Entries.end())
						{
							ReportError("ordered_map key order desync", SourceLocation);
							return false;
						}
						if (!AppendDynamicArrayValue(*ValuesArray, It->second, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "uint64")
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*OrderedMapHandle)->UInt64KeyOrder.size());
					for (const std::uint64_t Key : (*OrderedMapHandle)->UInt64KeyOrder)
					{
						const auto It = (*OrderedMapHandle)->UInt64Entries.find(Key);
						if (It == (*OrderedMapHandle)->UInt64Entries.end())
						{
							ReportError("ordered_map key order desync", SourceLocation);
							return false;
						}
						if (!AppendDynamicArrayValue(*ValuesArray, It->second, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "string")
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*OrderedMapHandle)->StrKeyOrder.size());
					for (const std::string& Key : (*OrderedMapHandle)->StrKeyOrder)
					{
						const auto It = (*OrderedMapHandle)->StrEntries.find(Key);
						if (It == (*OrderedMapHandle)->StrEntries.end())
						{
							ReportError("ordered_map key order desync", SourceLocation);
							return false;
						}
						if (!AppendDynamicArrayValue(*ValuesArray, It->second, SourceLocation))
						{
							return false;
						}
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "wstring")
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*OrderedMapHandle)->WStrKeyOrder.size());
					for (const std::u16string& Key : (*OrderedMapHandle)->WStrKeyOrder)
					{
						const auto It = (*OrderedMapHandle)->WStrEntries.find(Key);
						if (It == (*OrderedMapHandle)->WStrEntries.end())
						{
							ReportError("ordered_map key order desync", SourceLocation);
							return false;
						}
						if (!AppendDynamicArrayValue(*ValuesArray, It->second, SourceLocation))
						{
							return false;
						}
					}
				}
				else if (!(*OrderedMapHandle)->EnumEntries.empty()
					|| VmDetail::IsScopedEnumType(ActiveModule, (*OrderedMapHandle)->KeyTypeName))
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*OrderedMapHandle)->EnumKeyOrder.size());
					for (const FEnumValue& Key : (*OrderedMapHandle)->EnumKeyOrder)
					{
						const auto It = (*OrderedMapHandle)->EnumEntries.find(Key);
						if (It == (*OrderedMapHandle)->EnumEntries.end())
						{
							ReportError("ordered_map key order desync", SourceLocation);
							return false;
						}
						if (!AppendDynamicArrayValue(
								*ValuesArray, It->second, SourceLocation))
						{
							return false;
						}
					}
				}
				else if (!VmDetail::IsSigned32KeyStorageType((*OrderedMapHandle)->KeyTypeName))
				{
					ReserveDynamicArrayStorage(*ValuesArray, (*OrderedMapHandle)->HostKeyOrder.size());
					for (const FHostObjectHandle& Key : (*OrderedMapHandle)->HostKeyOrder)
					{
						const auto It = (*OrderedMapHandle)->HostEntries.find(Key);
						if (It == (*OrderedMapHandle)->HostEntries.end())
						{
							ReportError("ordered_map key order desync", SourceLocation);
							return false;
						}
						if (!AppendDynamicArrayValue(*ValuesArray, It->second, SourceLocation))
						{
							return false;
						}
					}
				}
				else
				{
					ReportError("invalid ordered_map key type", SourceLocation);
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
					(*OrderedMapHandle)->KeyTypeName,
					(*OrderedMapHandle)->ValueTypeName);
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
						if (Runtime::EnumValueTypeName(*ActiveModule, TypeDesc.Fields[0].TypeName) == Runtime::EnumValueTypeName(*ActiveModule, (*OrderedMapHandle)->KeyTypeName)
							&& Runtime::EnumValueTypeName(*ActiveModule, TypeDesc.Fields[1].TypeName) == Runtime::EnumValueTypeName(*ActiveModule, (*OrderedMapHandle)->ValueTypeName))
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
				if (VmDetail::IsSigned32KeyStorageType((*OrderedMapHandle)->KeyTypeName))
				{
					EntriesArray->Elements.reserve((*OrderedMapHandle)->IntKeyOrder.size());
					for (const std::int32_t Key : (*OrderedMapHandle)->IntKeyOrder)
					{
						const auto It = (*OrderedMapHandle)->IntEntries.find(Key);
						if (It == (*OrderedMapHandle)->IntEntries.end())
						{
							ReportError("ordered_map key order desync", SourceLocation);
							return false;
						}
						{
							FValue ClonedEntry;
							if (!CloneValueOrReport(It->second, ClonedEntry, SourceLocation)
								|| !PushEntry(
									VmDetail::MakeSigned32KeyValue(
										Key, (*OrderedMapHandle)->KeyTypeName),
									std::move(ClonedEntry)))
							{
								return false;
							}
						}
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "unsigned int")
				{
					EntriesArray->Elements.reserve((*OrderedMapHandle)->UIntKeyOrder.size());
					for (const std::uint32_t Key : (*OrderedMapHandle)->UIntKeyOrder)
					{
						const auto It = (*OrderedMapHandle)->UIntEntries.find(Key);
						if (It == (*OrderedMapHandle)->UIntEntries.end())
						{
							ReportError("ordered_map key order desync", SourceLocation);
							return false;
						}
						FValue ClonedEntry;
						if (!CloneValueOrReport(It->second, ClonedEntry, SourceLocation)
							|| !PushEntry(Key, std::move(ClonedEntry)))
						{
							return false;
						}
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "int64")
				{
					EntriesArray->Elements.reserve((*OrderedMapHandle)->Int64KeyOrder.size());
					for (const std::int64_t Key : (*OrderedMapHandle)->Int64KeyOrder)
					{
						const auto It = (*OrderedMapHandle)->Int64Entries.find(Key);
						if (It == (*OrderedMapHandle)->Int64Entries.end())
						{
							ReportError("ordered_map key order desync", SourceLocation);
							return false;
						}
						FValue ClonedKey;
						FValue ClonedEntry;
						if (!CloneValueOrReport(FValue(Key), ClonedKey, SourceLocation)
							|| !CloneValueOrReport(
								It->second, ClonedEntry, SourceLocation)
							|| !PushEntry(
								std::move(ClonedKey), std::move(ClonedEntry)))
						{
							return false;
						}
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "uint64")
				{
					EntriesArray->Elements.reserve((*OrderedMapHandle)->UInt64KeyOrder.size());
					for (const std::uint64_t Key : (*OrderedMapHandle)->UInt64KeyOrder)
					{
						const auto It = (*OrderedMapHandle)->UInt64Entries.find(Key);
						if (It == (*OrderedMapHandle)->UInt64Entries.end())
						{
							ReportError("ordered_map key order desync", SourceLocation);
							return false;
						}
						FValue ClonedEntry;
						if (!CloneValueOrReport(It->second, ClonedEntry, SourceLocation)
							|| !PushEntry(Key, std::move(ClonedEntry)))
						{
							return false;
						}
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "string")
				{
					EntriesArray->Elements.reserve((*OrderedMapHandle)->StrKeyOrder.size());
					for (const std::string& Key : (*OrderedMapHandle)->StrKeyOrder)
					{
						const auto It = (*OrderedMapHandle)->StrEntries.find(Key);
						if (It == (*OrderedMapHandle)->StrEntries.end())
						{
							ReportError("ordered_map key order desync", SourceLocation);
							return false;
						}
						{
							FValue ClonedEntry;
							if (!CloneValueOrReport(It->second, ClonedEntry, SourceLocation)
								|| !PushEntry(Key, std::move(ClonedEntry)))
							{
								return false;
							}
						}
					}
				}
				else if ((*OrderedMapHandle)->KeyTypeName == "wstring")
				{
					EntriesArray->Elements.reserve((*OrderedMapHandle)->WStrKeyOrder.size());
					for (const std::u16string& Key : (*OrderedMapHandle)->WStrKeyOrder)
					{
						const auto It = (*OrderedMapHandle)->WStrEntries.find(Key);
						if (It == (*OrderedMapHandle)->WStrEntries.end())
						{
							ReportError("ordered_map key order desync", SourceLocation);
							return false;
						}
						{
							FValue ClonedEntry;
							if (!CloneValueOrReport(It->second, ClonedEntry, SourceLocation)
								|| !PushEntry(Key, std::move(ClonedEntry)))
							{
								return false;
							}
						}
					}
				}
				else if (!(*OrderedMapHandle)->EnumEntries.empty()
					|| VmDetail::IsScopedEnumType(
						ActiveModule, (*OrderedMapHandle)->KeyTypeName))
				{
					EntriesArray->Elements.reserve(
						(*OrderedMapHandle)->EnumKeyOrder.size());
					for (const FEnumValue& Key : (*OrderedMapHandle)->EnumKeyOrder)
					{
						const auto It = (*OrderedMapHandle)->EnumEntries.find(Key);
						if (It == (*OrderedMapHandle)->EnumEntries.end())
						{
							ReportError("ordered_map key order desync", SourceLocation);
							return false;
						}
						FValue ClonedEntry;
						if (!CloneValueOrReport(It->second, ClonedEntry, SourceLocation)
							|| !PushEntry(Key, std::move(ClonedEntry)))
						{
							return false;
						}
					}
				}
				else if (!VmDetail::IsSigned32KeyStorageType((*OrderedMapHandle)->KeyTypeName))
				{
					EntriesArray->Elements.reserve((*OrderedMapHandle)->HostKeyOrder.size());
					for (const FHostObjectHandle& Key : (*OrderedMapHandle)->HostKeyOrder)
					{
						const auto It = (*OrderedMapHandle)->HostEntries.find(Key);
						if (It == (*OrderedMapHandle)->HostEntries.end())
						{
							ReportError("ordered_map key order desync", SourceLocation);
							return false;
						}
						FValue ClonedKey;
						FValue ClonedEntry;
						if (!CloneValueOrReport(FValue(Key), ClonedKey, SourceLocation)
							|| !CloneValueOrReport(
								It->second, ClonedEntry, SourceLocation)
							|| !PushEntry(
								std::move(ClonedKey), std::move(ClonedEntry)))
						{
							return false;
						}
					}
				}
				else
				{
					ReportError("invalid ordered_map key type", SourceLocation);
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
				if (!ReleaseOrderedMapValues(**OrderedMapHandle, SourceLocation))
				{
					return false;
				}
				Push(ThisValue);
				return true;
			}
			ReportError("unknown method: " + MethodName, SourceLocation);
			return false;
		}
