// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

			bool TryEmitContainerMethodCall(const FMethodCallExpr& Call, const std::string& ObjectType)
			{
				std::string ElementType;
				if (Runtime::IsDynamicArrayTypeName(ObjectType)
					|| Runtime::ParseDynamicArrayElementType(ObjectType, ElementType))
				{
					if (Call.Method == "remove_at" || Call.Method == "remove")
					{
						Runtime::ParseDynamicArrayElementType(ObjectType, ElementType);
						const auto* Element = ResolveUserTypeForLookup(ElementType);
						if (Element && !Element->bIsClass && !Element->bIsInterface && ReferenceOwnerHasCleanup(*Element))
						{
							Fail("AOT: removing array values with script cleanup requires the VM backend");
							return true;
						}
					}
					if (Call.Method == "reserve")
					{
						if (Call.Arguments.size() != 1)
						{
							Fail("AOT: reserve expects exactly one argument");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << " { auto& _vs_a = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_a.reserve(";
						EmitCoercedValue("int64", *Call.Arguments[0]);
						Out << "); return _vs_a; }())";
						return true;
					}
					if (Call.Method == "capacity")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: capacity expects no arguments");
							return true;
						}
						Out << '(';
						EmitExpr(*Call.Object);
						Out << ").capacity()";
						return true;
					}
					if (Call.Method == "push")
					{
						if (Call.Arguments.size() != 1)
						{
							Fail("AOT: push expects exactly one argument");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << " { auto& _vs_a = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_a.push_back(";
						Runtime::ParseDynamicArrayElementType(ObjectType, ElementType);
						const auto* Element = ResolveUserTypeForLookup(ElementType);
						if (Element && !Element->bIsClass && !Element->bIsInterface && ReferenceOwnerHasCleanup(*Element))
						{
							EmitCoercedValue(ElementType, *Call.Arguments[0]);
						}
						else
						{
							EmitExpr(*Call.Arguments[0]);
						}
						Out << "); return _vs_a; }())";
						return true;
					}
					if (Call.Method == "clear")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: clear expects no arguments");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << " { auto& _vs_a = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_a.clear(); return _vs_a; }())";
						return true;
					}
					if (Call.Method == "pop")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: pop expects no arguments");
							return true;
						}
						const std::string ElemCpp = MapType(ElementType);
						Out << "([&]() -> " << ElemCpp << " { auto& _vs_v = ";
						EmitExpr(*Call.Object);
						Out << "; return _vs_v.pop_value(); }())";
						return true;
					}
					if (Call.Method == "remove_at")
					{
						if (Call.Arguments.size() != 1)
						{
							Fail("AOT: remove_at expects exactly one argument");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << " { auto& _vs_a = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_a.remove_at(VsCheckedNativeSize(";
						EmitCoercedValue("int64", *Call.Arguments[0]);
						Out << ")); return _vs_a; }())";
						return true;
					}
					if (Call.Method == "remove")
					{
						if (Call.Arguments.size() != 1)
						{
							Fail("AOT: remove expects exactly one argument");
							return true;
						}
						Out << "([&]() -> bool { auto& _vs_a = ";
						EmitExpr(*Call.Object);
						Out << "; return _vs_a.remove_value(";
						EmitExpr(*Call.Arguments[0]);
						Out << "); }())";
						return true;
					}
				}
				if (Runtime::IsMapTypeName(ObjectType) || Runtime::IsOrderedMapTypeName(ObjectType))
				{
					if (Call.Method == "reserve")
					{
						if (Call.Arguments.size() != 1)
						{
							Fail("AOT: reserve expects exactly one argument");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << " { auto& _vs_m = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_m.reserve(";
						EmitCoercedValue("int64", *Call.Arguments[0]);
						Out << "); return _vs_m; }())";
						return true;
					}
					if (Call.Method == "add"
						&& (Runtime::IsMultiMapTypeName(ObjectType)
							|| Runtime::IsOrderedMultiMapTypeName(ObjectType)))
					{
						if (Call.Arguments.size() != 2)
						{
							Fail("AOT: multimap.add expects exactly two arguments");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << "& { auto&& _vs_m = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_m.get_or_insert(";
						EmitExpr(*Call.Arguments[0]);
						Out << ").push_back(";
						EmitExpr(*Call.Arguments[1]);
						Out << "); return _vs_m; }())";
						return true;
					}
					if (Call.Method == "has")
					{
						if (Call.Arguments.size() != 1)
						{
							Fail("AOT: has expects exactly one argument");
							return true;
						}
						Out << '(';
						EmitExpr(*Call.Object);
						Out << ").count(";
						EmitExpr(*Call.Arguments[0]);
						Out << ") != 0";
						return true;
					}
					if (Call.Method == "remove")
					{
						if (Call.Arguments.size() != 1)
						{
							Fail("AOT: remove expects exactly one argument");
							return true;
						}
						Out << "([&](){ auto&& _vs_c = ";
						EmitExpr(*Call.Object);
						Out << "; return _vs_c.erase(";
						EmitExpr(*Call.Arguments[0]);
						Out << ") > 0; }())";
						return true;
					}
					if (Call.Method == "clear")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: clear expects no arguments");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << " { auto& _vs_m = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_m.clear(); return _vs_m; }())";
						return true;
					}
					if (Call.Method == "keys" || Call.Method == "values" || Call.Method == "entries")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: keys/values/entries expects no arguments");
							return true;
						}
						bNeedsVector = true;
						std::string KeyType;
						std::string ValueType;
						const bool bOrdered = Runtime::ParseOrderedMapType(ObjectType, KeyType, ValueType);
						if (!bOrdered && !Runtime::ParseMapType(ObjectType, KeyType, ValueType))
						{
							return false;
						}
						if (Call.Method == "keys")
						{
							Out << "([&](){ vs_Array<" << MapType(KeyType)
								<< "> ___vs_out; for (const auto& ___vs_kv : ";
							EmitExpr(*Call.Object);
							Out << ") ___vs_out.push_back(___vs_kv.first); return ___vs_out; }())";
							return true;
						}
						if (Call.Method == "values")
						{
							Out << "([&](){ vs_Array<" << MapType(ValueType)
								<< "> ___vs_out; for (const auto& ___vs_kv : ";
							EmitExpr(*Call.Object);
							Out << ") ___vs_out.push_back(___vs_kv.second); return ___vs_out; }())";
							return true;
						}
						bNeedsUtility = true;
						if (HasUserPairTemplate())
						{
							Fail("AOT: map entries with a user pair<T,U> template require the VM backend");
							return true;
						}
						Out << "([&](){ vs_Array<vs_InternalPair<" << MapType(KeyType) << ", "
							<< MapType(ValueType) << ">> ___vs_out; for (const auto& ___vs_kv : ";
						EmitExpr(*Call.Object);
						Out << ") ___vs_out.push_back(vs_InternalPair<" << MapType(KeyType) << ", "
							<< MapType(ValueType)
							<< ">{___vs_kv.first, ___vs_kv.second}); return ___vs_out; }())";
						return true;
					}
				}
				if (Runtime::IsMultiSetTypeName(ObjectType)
					|| Runtime::IsOrderedMultiSetTypeName(ObjectType))
				{
					std::string MultiSetElement;
					if (!Runtime::ParseMultiSetElementType(ObjectType, MultiSetElement))
					{
						return false;
					}
					if (Call.Method == "reserve")
					{
						if (Call.Arguments.size() != 1)
						{
							Fail("AOT: multiset.reserve expects exactly one argument");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << "& { auto&& _vs_s = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_s.reserve(";
						EmitCoercedValue("int64", *Call.Arguments[0]);
						Out << "); return _vs_s; }())";
						return true;
					}
					if (Call.Method == "add")
					{
						if (Call.Arguments.size() != 1)
						{
							Fail("AOT: multiset.add expects exactly one argument");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << "& { auto&& _vs_s = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_s.add(";
						EmitExpr(*Call.Arguments[0]);
						Out << "); return _vs_s; }())";
						return true;
					}
					if (Call.Method == "has" || Call.Method == "count"
						|| Call.Method == "remove")
					{
						if (Call.Arguments.size() != 1)
						{
							Fail("AOT: multiset method expects exactly one argument");
							return true;
						}
						Out << '(';
						EmitExpr(*Call.Object);
						Out << ")." << (Call.Method == "remove" ? "remove_one" : Call.Method)
							<< '(';
						EmitExpr(*Call.Arguments[0]);
						Out << ')';
						return true;
					}
					if (Call.Method == "clear")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: multiset.clear expects no arguments");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << "& { auto&& _vs_s = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_s.clear(); return _vs_s; }())";
						return true;
					}
					if (Call.Method == "values")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: multiset.values expects no arguments");
							return true;
						}
						Out << "([&](){ vs_Array<" << MapType(MultiSetElement)
							<< "> _vs_out; for (const auto& _vs_value : (";
						EmitExpr(*Call.Object);
						Out << ").values()) _vs_out.push_back(_vs_value); return _vs_out; }())";
						return true;
					}
				}
				if (Runtime::IsSetTypeName(ObjectType))
				{
					if (Call.Method == "reserve")
					{
						if (Call.Arguments.size() != 1)
						{
							Fail("AOT: set.reserve expects exactly one argument");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << " { auto& _vs_s = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_s.reserve(";
						EmitCoercedValue("int64", *Call.Arguments[0]);
						Out << "); return _vs_s; }())";
						return true;
					}
					if (Call.Method == "has")
					{
						if (Call.Arguments.size() != 1)
						{
							Fail("AOT: has expects exactly one argument");
							return true;
						}
						Out << '(';
						EmitExpr(*Call.Object);
						Out << ").count(";
						EmitExpr(*Call.Arguments[0]);
						Out << ") != 0";
						return true;
					}
					if (Call.Method == "remove")
					{
						if (Call.Arguments.size() != 1)
						{
							Fail("AOT: remove expects exactly one argument");
							return true;
						}
						Out << "([&](){ auto&& _vs_c = ";
						EmitExpr(*Call.Object);
						Out << "; return _vs_c.erase(";
						EmitExpr(*Call.Arguments[0]);
						Out << ") > 0; }())";
						return true;
					}
					if (Call.Method == "add")
					{
						if (Call.Arguments.size() != 1)
						{
							Fail("AOT: add expects exactly one argument");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << " { auto& _vs_s = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_s.insert(";
						EmitExpr(*Call.Arguments[0]);
						Out << "); return _vs_s; }())";
						return true;
					}
					if (Call.Method == "clear")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: clear expects no arguments");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << " { auto& _vs_s = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_s.clear(); return _vs_s; }())";
						return true;
					}
					if (Call.Method == "values")
					{
						if (!Call.Arguments.empty()
							|| !Runtime::ParseSetElementType(ObjectType, ElementType))
						{
							Fail("AOT: values expects no arguments");
							return true;
						}
						Out << "([&](){ vs_Array<" << MapType(ElementType)
							<< "> _vs_out; for (const auto& _vs_value : ";
						EmitExpr(*Call.Object);
						Out << ") _vs_out.push_back(_vs_value); return _vs_out; }())";
						return true;
					}
				}
				if (Runtime::IsDequeTypeName(ObjectType))
				{
					if (Call.Method == "push_back")
					{
						if (Call.Arguments.size() != 1)
						{
							Fail("AOT: push_back expects exactly one argument");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << " { auto& _vs_d = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_d.push_back(";
						EmitExpr(*Call.Arguments[0]);
						Out << "); return _vs_d; }())";
						return true;
					}
					if (Call.Method == "push_front")
					{
						if (Call.Arguments.size() != 1)
						{
							Fail("AOT: push_front expects exactly one argument");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << " { auto& _vs_d = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_d.push_front(";
						EmitExpr(*Call.Arguments[0]);
						Out << "); return _vs_d; }())";
						return true;
					}
					if (Call.Method == "pop_back")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: pop_back expects no arguments");
							return true;
						}
						if (!Runtime::ParseDequeElementType(ObjectType, ElementType))
						{
							return false;
						}
						const std::string ElemCpp = MapType(ElementType);
						Out << "([&]() -> " << ElemCpp << " { auto& _vs_v = ";
						EmitExpr(*Call.Object);
						Out << "; return _vs_v.pop_back_value(); }())";
						return true;
					}
					if (Call.Method == "pop_front")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: pop_front expects no arguments");
							return true;
						}
						if (!Runtime::ParseDequeElementType(ObjectType, ElementType))
						{
							return false;
						}
						const std::string ElemCpp = MapType(ElementType);
						Out << "([&]() -> " << ElemCpp << " { auto& _vs_v = ";
						EmitExpr(*Call.Object);
						Out << "; return _vs_v.pop_front_value(); }())";
						return true;
					}
					if (Call.Method == "front")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: front expects no arguments");
							return true;
						}
						Out << '(';
						EmitExpr(*Call.Object);
						Out << ").front()";
						return true;
					}
					if (Call.Method == "back")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: back expects no arguments");
							return true;
						}
						Out << '(';
						EmitExpr(*Call.Object);
						Out << ").back()";
						return true;
					}
					if (Call.Method == "clear")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: clear expects no arguments");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << " { auto& _vs_d = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_d.clear(); return _vs_d; }())";
						return true;
					}
					if (Call.Method == "values")
					{
						if (!Call.Arguments.empty()
							|| !Runtime::ParseDequeElementType(ObjectType, ElementType))
						{
							Fail("AOT: values expects no arguments");
							return true;
						}
						Out << "([&](){ vs_Array<" << MapType(ElementType)
							<< "> _vs_out; for (const auto& _vs_value : ";
						EmitExpr(*Call.Object);
						Out << ") _vs_out.push_back(_vs_value); return _vs_out; }())";
						return true;
					}
				}
				std::string QElem;
				if (Runtime::ParseQueueElementType(ObjectType, QElem))
				{
					bNeedsQueue = true;
					if (Call.Method == "push")
					{
						if (Call.Arguments.size() != 1)
						{
							Fail("AOT: queue.push expects exactly one argument");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << " { auto _vs_q = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_q.push(";
						EmitExpr(*Call.Arguments[0]);
						Out << "); return _vs_q; }())";
						return true;
					}
					if (Call.Method == "pop")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: queue.pop expects no arguments");
							return true;
						}
						const std::string ElemCpp = MapType(QElem);
						Out << "([&]() -> " << ElemCpp << " { auto& _vs_q = ";
						EmitExpr(*Call.Object);
						Out << "; return _vs_q.pop_value(); }())";
						return true;
					}
					if ((Call.Method == "front"
							&& !Runtime::IsPriorityQueueTypeName(ObjectType))
						|| (Call.Method == "top"
							&& Runtime::IsPriorityQueueTypeName(ObjectType)))
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: queue access expects no arguments");
							return true;
						}
						Out << '(';
						EmitExpr(*Call.Object);
						Out << ")."
							<< (Runtime::IsPriorityQueueTypeName(ObjectType)
								? "top()"
								: "front()");
						return true;
					}
					if (Call.Method == "clear")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: queue.clear expects no arguments");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << " { auto _vs_q = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_q.clear(); return _vs_q; }())";
						return true;
					}
					if (Call.Method == "values")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: queue.values expects no arguments");
							return true;
						}
						Out << "([&](){ vs_Array<" << MapType(QElem)
							<< "> _vs_out; for (const auto& _vs_value : (";
						EmitExpr(*Call.Object);
						Out << ").values()) _vs_out.push_back(_vs_value); return _vs_out; }())";
						return true;
					}
				}
				if (Runtime::ParseStackElementType(ObjectType, QElem))
				{
					bNeedsStack = true;
					if (Call.Method == "push")
					{
						if (Call.Arguments.size() != 1)
						{
							Fail("AOT: stack.push expects exactly one argument");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << " { auto& _vs_s = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_s.push(";
						EmitExpr(*Call.Arguments[0]);
						Out << "); return _vs_s; }())";
						return true;
					}
					if (Call.Method == "pop")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: stack.pop expects no arguments");
							return true;
						}
						const std::string ElemCpp = MapType(QElem);
						Out << "([&]() -> " << ElemCpp << " { auto& _vs_s = ";
						EmitExpr(*Call.Object);
						Out << "; return _vs_s.pop_value(); }())";
						return true;
					}
					if (Call.Method == "top")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: stack.top expects no arguments");
							return true;
						}
						Out << '(';
						EmitExpr(*Call.Object);
						Out << ").top()";
						return true;
					}
					if (Call.Method == "clear")
					{
						if (!Call.Arguments.empty())
						{
							Fail("AOT: stack.clear expects no arguments");
							return true;
						}
						Out << "([&]() -> " << MapType(ObjectType) << " { auto& _vs_s = ";
						EmitExpr(*Call.Object);
						Out << "; _vs_s.clear(); return _vs_s; }())";
						return true;
					}
				}
				return false;
			}

			// delegate/multicast_delegate method calls pass through as `(obj)->Method(args...)`.
			bool TryEmitEventMethodCall(const FMethodCallExpr& Call, const std::string& ObjectType)
			{
				if (!IsEventTypeName(ObjectType))
				{
					return false;
				}
				bNeedsMemory = true;
				Out << "VsRequireObject(";
				EmitExpr(*Call.Object);
				Out << ")." << Call.Method << '(';
				std::vector<std::string> Parameters;
				const std::string HandlerType = EventHandlerTypeName(ObjectType);
				if (Call.Method == "bind" || Call.Method == "add" || Call.Method == "remove")
				{
					Parameters.push_back(HandlerType);
				}

				else if (Call.Method == "execute" || Call.Method == "execute_if_bound" ||
					Call.Method == "broadcast")
				{
					std::string ReturnType;
					Runtime::ParseFunctionType(HandlerType, Parameters, ReturnType);
				}
				EmitResolvedCallArgs(Call.Arguments, &Parameters);
				Out << ')';
				return true;
			}

			bool EmitEmptyContainerCtor(const std::string& TypeName)
			{
				if (Runtime::IsDynamicArrayTypeName(TypeName))
				{
					bNeedsVector = true;
					Out << MapType(TypeName) << "{}";
					return true;
				}
				if (Runtime::IsMapTypeName(TypeName) || Runtime::IsOrderedMapTypeName(TypeName))
				{
					bNeedsMap = true;
					Out << MapType(TypeName) << "{}";
					return true;
				}
				if (Runtime::IsMultiSetTypeName(TypeName)
					|| Runtime::IsOrderedMultiSetTypeName(TypeName))
				{
					bNeedsMap = true;
					Out << MapType(TypeName) << "{}";
					return true;
				}
				if (Runtime::IsSetTypeName(TypeName))
				{
					bNeedsSet = true;
					Out << MapType(TypeName) << "{}";
					return true;
				}
				if (Runtime::IsDequeTypeName(TypeName))
				{
					bNeedsDeque = true;
					Out << MapType(TypeName) << "{}";
					return true;
				}
				if (Runtime::IsQueueTypeName(TypeName))
				{
					bNeedsQueue = true;
					Out << MapType(TypeName) << "{}";
					return true;
				}
				if (Runtime::IsStackTypeName(TypeName))
				{
					bNeedsStack = true;
					Out << MapType(TypeName) << "{}";
					return true;
				}
				if (Runtime::IsChannelTypeName(TypeName))
				{
					std::string Elem;
					if (!TryParseChannelType(TypeName, Elem))
					{
						return false;
					}
					bNeedsChannel = true;
					bNeedsMemory = true;
					Out << "VsMakeChannel<" << MapType(Elem) << ">()";
					return true;
				}
				std::string PairFirst;
				std::string PairSecond;
				if (!HasUserPairTemplate() && Runtime::ParsePairType(TypeName, PairFirst, PairSecond))
				{
					bNeedsUtility = true;
					Out << MapType(TypeName) << "{}";
					return true;
				}
				return false;
			}

			static bool TypesEqual(const std::string& Left, const std::string& Right)
			{
				return Left == Right;
			}

			// Empty Arg means "unknown" (e.g. bare contextual lambda) and matches any Param.
			static bool TypesCompatible(const std::string& Param, const std::string& Arg)
			{
				if (Arg.empty())
				{
					return true;
				}
				return Param == Arg;
			}

			std::string ApplyTypeSubst(const std::string& TypeName) const
			{
				if (TypeSubst.empty())
				{
					return TypeName;
				}
				const auto Exact = TypeSubst.find(TypeName);
				if (Exact != TypeSubst.end())
				{
					return Exact->second;
				}

				std::string ElementType;
				if (Runtime::ParseDynamicArrayElementType(TypeName, ElementType))
				{
					return "array<" + ApplyTypeSubst(ElementType) + ">";
				}

				// Replace whole type-param identifiers that appear as free segments.
				std::string Result = TypeName;
				for (const auto& Entry : TypeSubst)
				{
					const std::string& From = Entry.first;
					const std::string& To = Entry.second;
					std::size_t Pos = 0;
					while ((Pos = Result.find(From, Pos)) != std::string::npos)
					{
						const bool bStartOk = Pos == 0
							|| (!std::isalnum(static_cast<unsigned char>(Result[Pos - 1]))
								&& Result[Pos - 1] != '_' && Result[Pos - 1] != '.'
								&& Result[Pos - 1] != ':' && Result[Pos - 1] != '\x1F');
						const std::size_t End = Pos + From.size();
						const bool bEndOk = End >= Result.size()
							|| (!std::isalnum(static_cast<unsigned char>(Result[End]))
								&& Result[End] != '_');
						if (bStartOk && bEndOk)
						{
							Result.replace(Pos, From.size(), To);
							Pos += To.size();
						}
						else
						{
							Pos = End;
						}
					}
				}
				return Result;
			}

			std::string ResolveAliasChain(std::string TypeName) const
			{
				const bool bGlobalQualified = TypeName.rfind("::", 0) == 0;
				if (bGlobalQualified)
				{
					TypeName.erase(0, 2);
				}
				const auto& Aliases = bGlobalQualified ? RootTypeAliases : TypeAliases;
				std::unordered_set<std::string> Seen;
				while (true)
				{
					const auto It = Aliases.find(TypeName);
					if (It == Aliases.end())
					{
						break;
					}
					if (!Seen.insert(TypeName).second)
					{
						break;
					}
					TypeName = It->second;
				}
				return TypeName;
			}

			/** Create or reuse a monomorph cache entry `OpenName_g_...` for resolution/ctors.
			 *  MapType / emit prefer open C++ templates (`vs_Box<T>`); this cache is not the TU layout. */
			std::string EnsureTypeMonomorph(
				const std::string& OpenName,
				std::vector<std::string> TypeArgs) const
			{
				// Soft pair<T,U> keeps its own identity beside user templates of other arities.
				if (OpenName == "pair" && TypeArgs.size() == 2 && !HasUserPairTemplate())
				{
					return Runtime::MakePairTypeName(TypeArgs[0], TypeArgs[1]);
				}
				// Mutable cache: MapType is const but monomorph collection is lazy.
				auto* Self = const_cast<FCppEmitter*>(this);
				const auto OpenIt = Self->OpenGenericTypes.find(OpenName);
				if (OpenIt == Self->OpenGenericTypes.end())
				{
					Fail("AOT: unknown open generic type '" + OpenName + "'");
					return {};
				}
				const FOpenGenericType& Open = OpenIt->second;
				std::size_t Required = Open.TypeParams.size();
				if (Open.TypeParamDefaults.size() == Open.TypeParams.size())
				{
					while (Required > 0 && !Open.TypeParamDefaults[Required - 1].empty())
					{
						--Required;
					}
				}
				if (TypeArgs.size() < Required || TypeArgs.size() > Open.TypeParams.size())
				{
					Fail(
						"AOT: generic '" + OpenName + "' expects "
							+ (Required == Open.TypeParams.size()
								? std::to_string(Required)
								: std::to_string(Required) + " to "
									+ std::to_string(Open.TypeParams.size()))
							+ " type args");
					return {};
				}
				if (TypeArgs.size() < Open.TypeParams.size())
				{
					std::unordered_map<std::string, std::string> DefaultBindings;
					for (std::size_t Index = 0; Index < TypeArgs.size(); ++Index)
					{
						DefaultBindings.emplace(Open.TypeParams[Index], TypeArgs[Index]);
					}
					while (TypeArgs.size() < Open.TypeParams.size())
					{
						const std::size_t Index = TypeArgs.size();
						std::string DefaultType = Self->SubstituteWithMap(
							Open.TypeParamDefaults[Index],
							DefaultBindings);
						TypeArgs.push_back(DefaultType);
						DefaultBindings.emplace(Open.TypeParams[Index], std::move(DefaultType));
					}
				}
				const std::string Key = MonoTypeKey(OpenName, TypeArgs);
				if (Self->TypeMonoKeys.find(Key) != Self->TypeMonoKeys.end())
				{
					return Self->MonoCppTypeName(OpenName, TypeArgs);
				}
				const std::string MonoName = Self->MonoCppTypeName(OpenName, TypeArgs);
				if (Self->UserTypes.find(MonoName) != Self->UserTypes.end())
				{
					Self->TypeMonoKeys.insert(Key);
					return MonoName;
				}

				FUserTypeInfo Info;
				Info.Name = MonoName;
				Info.ModulePath = Open.ModulePath;
				Info.bExported = Open.bExported;
				Info.bFinal = Open.bFinal;
				Info.bIsAbstract = Open.bIsAbstract;
				Info.bHasPureVirtualDestructor = Open.bHasPureVirtualDestructor;
				Info.bIsClass = Open.bIsClass;
				Info.bIsInterface = Open.bIsInterface;
				Info.Fields = Open.Fields;
				Info.Methods = Open.Methods;
				Info.Constructors = Open.Constructors;
				Info.Destructor = Open.Destructor;
				Info.TypeParams = Open.TypeParams;
				Info.MonoTypeArgs = TypeArgs;
				Info.OpenGenericName = OpenName;
				Info.bResolutionOnlyMonomorph = std::any_of(
					TypeArgs.begin(), TypeArgs.end(),
					[&](const std::string& Arg)
					{
						return TypeMentionsActiveOpenParam(Arg);
					});

				// Substitute base/interface type apps for the inheritance list.
				std::unordered_map<std::string, std::string> Bindings;
				for (std::size_t Index = 0; Index < Open.TypeParams.size(); ++Index)
				{
					Bindings.emplace(Open.TypeParams[Index], TypeArgs[Index]);
				}
				auto SubstType = [&](const std::string& Raw) -> std::string
				{
					return Self->SubstituteWithMap(Raw, Bindings);
				};
				for (const std::string& Base : Open.BaseTypeNames)
				{
					const std::string Sub = SubstType(Base);
					const std::string::size_type Lt = Sub.find('<');
					if (Lt != std::string::npos && Sub.back() == '>')
					{
						const std::string BaseOpen = Sub.substr(0, Lt);
						std::vector<std::string> BaseArgs;
						SplitTopLevelTypeArgs(Sub.substr(Lt + 1, Sub.size() - Lt - 2), BaseArgs);
						Info.BaseTypeNames.push_back(EnsureTypeMonomorph(BaseOpen, BaseArgs));
					}
					else
					{
						Info.BaseTypeNames.push_back(Sub);
					}
				}
				for (const std::string& Iface : Open.InterfaceTypeNames)
				{
					const std::string Sub = SubstType(Iface);
					const std::string::size_type Lt = Sub.find('<');
					if (Lt != std::string::npos && Sub.back() == '>')
					{
						const std::string IfaceOpen = Sub.substr(0, Lt);
						std::vector<std::string> IfaceArgs;
						SplitTopLevelTypeArgs(Sub.substr(Lt + 1, Sub.size() - Lt - 2), IfaceArgs);
						Info.InterfaceTypeNames.push_back(EnsureTypeMonomorph(IfaceOpen, IfaceArgs));
					}
					else
					{
						Info.InterfaceTypeNames.push_back(Sub);
					}
				}

				Self->UserTypes.emplace(MonoName, Info);
				Self->TypeMonoKeys.insert(Key);
				Self->TypeEmitOrder.push_back(MonoName);
				return MonoName;
			}
