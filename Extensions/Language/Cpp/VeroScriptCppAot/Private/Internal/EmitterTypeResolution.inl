// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

			void PushTypeSubstForUserType(const FUserTypeInfo& Type)
			{
				TypeSubst.clear();
				if (Type.TypeParams.empty() || Type.MonoTypeArgs.empty())
				{
					return;
				}
				for (std::size_t Index = 0; Index < Type.TypeParams.size()
					&& Index < Type.MonoTypeArgs.size();
					++Index)
				{
					TypeSubst.emplace(Type.TypeParams[Index], Type.MonoTypeArgs[Index]);
				}
			}

			std::string MapType(const std::string& RawTypeName) const
			{
				const std::string SubstitutedTypeName = ApplyTypeSubst(RawTypeName);
				if (Runtime::IsReferenceTypeName(SubstitutedTypeName))
				{
					const std::string ValueType = MapType(
						std::string(Runtime::StripReferenceTypeName(SubstitutedTypeName)));
					return Runtime::IsConstReferenceTypeName(SubstitutedTypeName)
						? "const " + ValueType + "&"
						: ValueType + "&";
				}
				const std::string TypeName = ResolveAliasChain(SubstitutedTypeName);
				if (IsRuntimeSourceMetadataType(TypeName))
				{
					Fail("AOT: runtime source metadata requires the VeroScript runtime; use the VM backend");
					return "void";
				}
				if (HostBindings)
				{
					std::string RuntimeTypeName;
					std::string AliasError;
					if (!ResolveHostRuntimeTypeName(
							*HostBindings, TypeName, RuntimeTypeName, AliasError))
					{
						Fail("AOT: invalid host type alias '" + TypeName + "': " + AliasError);
						return "void";
					}
					if (RuntimeTypeName != TypeName)
					{
						return MapType(RuntimeTypeName);
					}
				}
				if (SubstitutedTypeName.rfind("::", 0) != 0
					&& ActiveOpenTypeParams.find(TypeName) != ActiveOpenTypeParams.end())
				{
					return TypeName;
				}
				if (TypeName == "void")
				{
					return "void";
				}
				if (TypeName == "int" || TypeName == "bool")
				{
					return TypeName;
				}
				if (TypeName == "char")
				{
					return "std::int8_t";
				}
				if (TypeName == "short")
				{
					return "std::int16_t";
				}
				if (TypeName == "long")
				{
					return "std::int64_t";
				}
				if (TypeName == "unsigned" || TypeName == "unsigned int")
				{
					return "std::uint32_t";
				}
				if (TypeName == "unsigned char")
				{
					return "std::uint8_t";
				}
				if (TypeName == "unsigned short")
				{
					return "std::uint16_t";
				}
				if (TypeName == "unsigned long")
				{
					return "std::uint64_t";
				}
				if (TypeName == "int64")
				{
					return "std::int64_t";
				}
				if (TypeName == "uint64")
				{
					return "std::uint64_t";
				}
				if (TypeName == "float")
				{
					return "float";
				}
				if (TypeName == "double")
				{
					return "double";
				}
				if (TypeName == "string")
				{
					return "std::string";
				}
				if (TypeName == "wstring")
				{
					return "std::wstring";
				}
				if (Runtime::IsScriptArgsArrayTypeName(TypeName))
				{
					bNeedsVector = true;
					return "std::vector<std::string>";
				}
				if (TypeName == "auto")
				{
					return "auto";
				}
				if (EnumTypeNames.find(TypeName) != EnumTypeNames.end())
				{
					return ScopedEnumTypeNames.find(TypeName) != ScopedEnumTypeNames.end()
						? UserCppType(TypeName)
						: "int";
				}
				std::string ElementType;
				if (Runtime::ParseDynamicArrayElementType(TypeName, ElementType))
				{
					bNeedsVector = true;
					bNeedsMemory = true;
					return "vs_Array<" + MapType(ElementType) + ">";
				}
				std::uint32_t NativeLength = 0;
				if (Runtime::ParseNativeArrayType(TypeName, ElementType, NativeLength))
				{
					bNeedsArray = true;
					return "std::array<" + MapType(ElementType) + ", "
						+ std::to_string(NativeLength) + ">";
				}
				std::string MapKeyType;
				std::string MapValueType;
				if (Runtime::ParseMapType(TypeName, MapKeyType, MapValueType))
				{
					bNeedsMap = true;
					bNeedsMemory = true;
					return "vs_Map<" + MapType(MapKeyType) + ", "
						+ MapType(MapValueType) + ">";
				}
				if (Runtime::ParseOrderedMapType(TypeName, MapKeyType, MapValueType))
				{
					bNeedsMap = true;
					bNeedsVector = true;
					bNeedsMemory = true;
					return "vs_OrderedMap<" + MapType(MapKeyType) + ", "
						+ MapType(MapValueType) + ">";
				}
				if (Runtime::ParseMultiSetElementType(TypeName, ElementType))
				{
					bNeedsMap = true;
					bNeedsVector = true;
					bNeedsMemory = true;
					return std::string(
						Runtime::IsOrderedMultiSetTypeName(TypeName)
							? "vs_OrderedMultiSet<"
							: "vs_MultiSet<")
						+
						MapType(ElementType) + ">";
				}
				if (Runtime::ParseSetElementType(TypeName, ElementType))
				{
					bNeedsSet = true;
					bNeedsMemory = true;
					return "vs_Set<" + MapType(ElementType) + ">";
				}
				if (Runtime::ParseDequeElementType(TypeName, ElementType))
				{
					bNeedsDeque = true;
					bNeedsMemory = true;
					return "vs_Deque<" + MapType(ElementType) + ">";
				}
				if (Runtime::ParseQueueElementType(TypeName, ElementType))
				{
					bNeedsQueue = true;
					bNeedsMemory = true;
					return std::string(
						Runtime::IsPriorityQueueTypeName(TypeName)
							? "vs_PriorityQueue<"
							: "vs_Queue<")
						+
						MapType(ElementType) + ">";
				}
				if (Runtime::ParseStackElementType(TypeName, ElementType))
				{
					bNeedsStack = true;
					bNeedsMemory = true;
					return "vs_Stack<" + MapType(ElementType) + ">";
				}
				if (TryParseChannelType(TypeName, ElementType))
				{
					bNeedsChannel = true;
					bNeedsMemory = true;
					return "std::shared_ptr<vs_Channel<" + MapType(ElementType) + ">>";
				}
				{
					std::string PairFirst;
					std::string PairSecond;
					if (!HasUserPairTemplate() && Runtime::ParsePairType(TypeName, PairFirst, PairSecond))
					{
						bNeedsUtility = true;
						return "vs_InternalPair<" + MapType(PairFirst) + ", "
							+ MapType(PairSecond) + ">";
					}
				}
				{
					std::vector<std::string> FnParamTypes;
					std::string FnReturnType;
					if (Runtime::ParseFunctionType(TypeName, FnParamTypes, FnReturnType))
					{
						bNeedsFunctional = true;
						bNeedsMemory = true;
						std::string Result = "vs_Function<" + MapType(FnReturnType) + '(';
						for (std::size_t Index = 0; Index < FnParamTypes.size(); ++Index)
						{
							if (Index > 0)
							{
								Result += ", ";
							}
							Result += MapType(FnParamTypes[Index]);
						}
						Result += ")>";
						return Result;
					}
				}
				{
					std::string HandleInner;
					if (TryParseHandleType(TypeName, HandleInner))
					{
						bNeedsMemory = true;
						bNeedsHandle = true;
						return "std::shared_ptr<vs_SoftHandle<" + MapType(HandleInner) + ">>";
					}
				}
				{
					std::vector<std::string> Args;
					if (bHasStandardAttribute && TryParseSoftBuiltinType(TypeName, "attribute", Args) && Args.size() == 1)
					{
						bNeedsMemory = bNeedsFunctional = bNeedsUtility = true;
						return "vs_Attribute<" + MapType(Args[0]) + ">";
					}
					if (TryParseSoftBuiltinType(TypeName, "optional", Args) && Args.size() == 1)
					{
						bNeedsUtility = true;
						return "vs_Optional<" + MapType(Args[0]) + ">";
					}
					if (TryParseSoftBuiltinType(TypeName, "readonly", Args)
						&& Args.size() == 1)
					{
						bNeedsUtility = true;
						return "vs_Readonly<" + MapType(Args[0]) + ">";
					}
					if (TryParseSoftBuiltinType(TypeName, "type", Args)
						&& Args.size() == 1)
					{
						bNeedsUtility = true;
						return "vs_TypeToken<" + MapType(Args[0]) + ">";
					}
					if (TryParseSoftBuiltinType(TypeName, "weak", Args)
						&& Args.size() == 1)
					{
						bNeedsUtility = true;
						bNeedsMemory = true;
						return "vs_Weak<" + MapType(Args[0]) + ">";
					}
				}
				if (TypeName == "multicast_delegate")
				{
					bNeedsMemory = true;
					bNeedsFunctional = true;
					bNeedsVector = true;
					bNeedsEvents = true;
					return "std::shared_ptr<vs_MulticastDelegate0>";
				}
				{
					std::vector<std::string> McArgs;
					if (TryParseTypeApplication(TypeName, "multicast_delegate", McArgs))
					{
						bNeedsMemory = true;
						bNeedsFunctional = true;
						bNeedsVector = true;
						bNeedsEvents = true;
						if (McArgs.size() == 1)
						{
							return "std::shared_ptr<vs_MulticastDelegate1<" + MapType(McArgs[0]) + ">>";
						}
						if (McArgs.size() == 2)
						{
							return "std::shared_ptr<vs_MulticastDelegate2<" + MapType(McArgs[0])
								+ ", " + MapType(McArgs[1]) + ">>";
						}
						if (McArgs.size() == 3)
						{
							return "std::shared_ptr<vs_MulticastDelegate3<" + MapType(McArgs[0])
								+ ", " + MapType(McArgs[1]) + ", " + MapType(McArgs[2]) + ">>";
						}
						Fail("AOT: unsupported multicast_delegate arity");
						return "void";
					}
				}
				{
					std::vector<std::string> DelArgs;
					if (TryParseTypeApplication(TypeName, "delegate", DelArgs))
					{
						bNeedsMemory = true;
						bNeedsFunctional = true;
						bNeedsEvents = true;
						if (DelArgs.size() == 1)
						{
							return "std::shared_ptr<vs_Delegate0<" + MapType(DelArgs[0]) + ">>";
						}
						if (DelArgs.size() == 2)
						{
							return "std::shared_ptr<vs_Delegate1<" + MapType(DelArgs[0]) + ", "
								+ MapType(DelArgs[1]) + ">>";
						}
						if (DelArgs.size() == 3)
						{
							return "std::shared_ptr<vs_Delegate2<" + MapType(DelArgs[0]) + ", "
								+ MapType(DelArgs[1]) + ", " + MapType(DelArgs[2]) + ">>";
						}
						if (DelArgs.size() == 4)
						{
							return "std::shared_ptr<vs_Delegate3<" + MapType(DelArgs[0]) + ", "
								+ MapType(DelArgs[1]) + ", " + MapType(DelArgs[2]) + ", "
								+ MapType(DelArgs[3]) + ">>";
						}
						Fail("AOT: unsupported delegate arity");
						return "void";
					}
				}
				{
					const auto TypeIt = UserTypes.find(TypeName);
					if (TypeIt != UserTypes.end())
					{
						// Monomorph cache entries still used for resolution; emit as open templates.
						if (!TypeIt->second.OpenGenericName.empty()
							&& !TypeIt->second.MonoTypeArgs.empty())
						{
							std::string Mapped = UserCppType(TypeIt->second.OpenGenericName);
							Mapped += '<';
							for (std::size_t Index = 0; Index < TypeIt->second.MonoTypeArgs.size();
								++Index)
							{
								if (Index > 0)
								{
									Mapped += ", ";
								}
								Mapped += MapType(TypeIt->second.MonoTypeArgs[Index]);
							}
							Mapped += '>';
							if (TypeIt->second.bIsInterface || TypeIt->second.bIsClass)
							{
								bNeedsMemory = true;
								return "std::shared_ptr<" + Mapped + ">";
							}
							return Mapped;
						}
						// Bare open generic name (e.g. `this` inside template<typename T> struct vs_Box).
						if (!TypeIt->second.TypeParams.empty()
							&& TypeIt->second.MonoTypeArgs.empty()
							&& !ActiveOpenTypeParams.empty())
						{
							std::string Mapped = UserCppType(TypeName);
							Mapped += '<';
							for (std::size_t Index = 0; Index < TypeIt->second.TypeParams.size();
								++Index)
							{
								if (Index > 0)
								{
									Mapped += ", ";
								}
								Mapped += TypeIt->second.TypeParams[Index];
							}
							Mapped += '>';
							if (TypeIt->second.bIsInterface || TypeIt->second.bIsClass)
							{
								bNeedsMemory = true;
								return "std::shared_ptr<" + Mapped + ">";
							}
							return Mapped;
						}
						if (TypeIt->second.bIsInterface || TypeIt->second.bIsClass)
						{
							bNeedsMemory = true;
							return "std::shared_ptr<" + UserCppType(TypeName) + ">";
						}
						return UserCppType(TypeName);
					}
				}
				{
					// Open-generic application, e.g. Box<int> -> vs_Box<int> (C++ template).
					const std::string::size_type Lt = TypeName.find('<');
					if (Lt != std::string::npos && TypeName.back() == '>')
					{
						const std::string BaseName = TypeName.substr(0, Lt);
						std::vector<std::string> TypeArgs;
						SplitTopLevelTypeArgs(
							TypeName.substr(Lt + 1, TypeName.size() - Lt - 2),
							TypeArgs);
						const auto OpenIt = OpenGenericTypes.find(BaseName);
						if (OpenIt != OpenGenericTypes.end())
						{
							std::string Mapped = UserCppType(BaseName);
							Mapped += '<';
							for (std::size_t Index = 0; Index < TypeArgs.size(); ++Index)
							{
								if (Index > 0)
								{
									Mapped += ", ";
								}
								Mapped += MapType(TypeArgs[Index]);
							}
							Mapped += '>';
							if (OpenIt->second.bIsInterface || OpenIt->second.bIsClass)
							{
								bNeedsMemory = true;
								return "std::shared_ptr<" + Mapped + ">";
							}
							return Mapped;
						}
						if (UserTypes.find(BaseName) != UserTypes.end())
						{
							const std::string MonoName = EnsureTypeMonomorph(BaseName, TypeArgs);
							if (MonoName.empty())
							{
								return "void";
							}
							return MapType(MonoName);
						}
					}
				}
				if (TypeName.rfind("field_info<", 0) == 0
					|| TypeName.rfind("method_info<", 0) == 0)
				{
					Fail("AOT: executable reflection is not supported by the C++ backend");
				}
				else
				{
					Fail("AOT: unsupported type '" + TypeName + "'");
				}
				return "void";
			}

			static bool BlockEndsWithFallthrough(const FBlockStmt* Body)
			{
				if (!Body || Body->Statements.empty() || !Body->Statements.back())
				{
					return false;
				}
				return Body->Statements.back()->Kind == EStmtKind::Fallthrough;
			}

			std::string EnumMemberCppName(const std::string& EnumName, const std::string& Member) const
			{
				if (ScopedEnumTypeNames.find(EnumName) != ScopedEnumTypeNames.end())
				{
					return UserCppType(EnumName) + "::" + SanitizeForMangle(Member);
				}
				return "vs_" + SanitizeForMangle(EnumName) + '_' + SanitizeForMangle(Member);
			}

			std::string UserCppType(const std::string& TypeName) const
			{
				// Namespaced types arrive dotted (`Ns.Box`). Fold `.` with an
				// unambiguous token so `Ns.Box` → `vs_Ns__dot__Box` cannot collide
				// with a user type literally named `Ns_Box` → `vs_Ns_Box`.
				if (TypeName.find('.') == std::string::npos)
				{
					return "vs_" + TypeName;
				}
				std::string Folded;
				Folded.reserve(TypeName.size() * 2 + 8);
				for (const char Ch : TypeName)
				{
					if (Ch == '.')
					{
						Folded += "__dot__";
					}
					else
					{
						Folded.push_back(Ch);
					}
				}
				return "vs_" + Folded;
			}

			// Constructor helpers for an open generic are emitted under the template
			// owner's name (`vs_Base_init<T>`), while resolution may refer to either
			// `Base<T>` or an internal monomorph cache key (`Base_g_int`).  Collapse
			// those spellings before comparing explicit base initializers or naming
			// the helper; MapBaseCppType still supplies the concrete C++ cast type.
			std::string CtorInitOwnerName(const std::string& TypeName) const
			{
				const auto Concrete = UserTypes.find(TypeName);
				if (Concrete != UserTypes.end()
					&& !Concrete->second.OpenGenericName.empty())
				{
					return Concrete->second.OpenGenericName;
				}
				const std::string::size_type Lt = TypeName.find('<');
				if (Lt != std::string::npos && !TypeName.empty() && TypeName.back() == '>')
				{
					const std::string OpenName = TypeName.substr(0, Lt);
					if (OpenGenericTypes.find(OpenName) != OpenGenericTypes.end())
					{
						return OpenName;
					}
				}
				return TypeName;
			}

			bool IsClassBaseType(const std::string& TypeName) const
			{
				const auto Concrete = UserTypes.find(TypeName);
				if (Concrete != UserTypes.end())
				{
					return Concrete->second.bIsClass;
				}
				const auto Open = OpenGenericTypes.find(CtorInitOwnerName(TypeName));
				return Open != OpenGenericTypes.end() && Open->second.bIsClass;
			}

			// Map a type for C++ base-specifier lists (never wrap class/iface in shared_ptr).
			std::string MapBaseCppType(const std::string& RawTypeName) const
			{
				const std::string Mapped = MapType(RawTypeName);
				static constexpr std::string_view kSharedPtr = "std::shared_ptr<";
				if (Mapped.size() > kSharedPtr.size() + 1
					&& Mapped.compare(0, kSharedPtr.size(), kSharedPtr) == 0
					&& Mapped.back() == '>')
				{
					return Mapped.substr(
						kSharedPtr.size(),
						Mapped.size() - kSharedPtr.size() - 1);
				}
				return Mapped;
			}

			// Resolve a bound interface type name to its method list (open or concrete).
			const std::vector<FMethodDecl>* FindBoundInterfaceMethods(
				const std::string& InterfaceTypeName) const
			{
				const std::string Resolved =
					ResolveAliasChain(ApplyTypeSubst(InterfaceTypeName));
				const auto TypeIt = UserTypes.find(Resolved);
				if (TypeIt != UserTypes.end() && TypeIt->second.bIsInterface
					&& TypeIt->second.Methods)
				{
					return TypeIt->second.Methods;
				}
				const std::string::size_type Lt = Resolved.find('<');
				const std::string BaseName =
					(Lt != std::string::npos && Resolved.back() == '>')
					? Resolved.substr(0, Lt)
					: Resolved;
				const auto OpenIt = OpenGenericTypes.find(BaseName);
				if (OpenIt != OpenGenericTypes.end() && OpenIt->second.bIsInterface
					&& OpenIt->second.Methods)
				{
					return OpenIt->second.Methods;
				}
				const auto BareIt = UserTypes.find(BaseName);
				if (BareIt != UserTypes.end() && BareIt->second.bIsInterface
					&& BareIt->second.Methods)
				{
					return BareIt->second.Methods;
				}
				return nullptr;
			}

			/**
			 * Map a `where` bound interface type (`IHasId` / `IChild<int>`) to a
			 * UserTypes entry suitable for ResolveMethod (parents flattened via mono).
			 */
			const FUserTypeInfo* ResolveBoundInterfaceType(
				const std::string& InterfaceTypeName) const
			{
				const std::string Resolved =
					ResolveAliasChain(ApplyTypeSubst(InterfaceTypeName));
				const auto Exact = UserTypes.find(Resolved);
				if (Exact != UserTypes.end() && Exact->second.bIsInterface)
				{
					return &Exact->second;
				}
				const std::string::size_type Lt = Resolved.find('<');
				if (Lt != std::string::npos && Resolved.back() == '>')
				{
					const std::string BaseName = Resolved.substr(0, Lt);
					std::vector<std::string> TypeArgs;
					SplitTopLevelTypeArgs(
						Resolved.substr(Lt + 1, Resolved.size() - Lt - 2),
						TypeArgs);
					if (OpenGenericTypes.find(BaseName) != OpenGenericTypes.end())
					{
						const std::string Mono = EnsureTypeMonomorph(BaseName, TypeArgs);
						if (Mono.empty())
						{
							return nullptr;
						}
						const auto MonoIt = UserTypes.find(Mono);
						if (MonoIt != UserTypes.end() && MonoIt->second.bIsInterface)
						{
							return &MonoIt->second;
						}
					}
				}
				const auto Bare = UserTypes.find(Resolved);
				if (Bare != UserTypes.end() && Bare->second.bIsInterface)
				{
					return &Bare->second;
				}
				// Bare open name while emitting templates.
				const auto OpenIt = OpenGenericTypes.find(Resolved);
				if (OpenIt != OpenGenericTypes.end() && OpenIt->second.bIsInterface)
				{
					const auto Registered = UserTypes.find(Resolved);
					if (Registered != UserTypes.end())
					{
						return &Registered->second;
					}
				}
				return nullptr;
			}

			// Resolve a source type spelling to the AOT semantic entry used for
			// inherited member lookup.  MapType returns a C++ spelling and therefore
			// must not be used as a UserTypes key (notably for `Base<T>`).
			const FUserTypeInfo* ResolveUserTypeForLookup(
				const std::string& RawTypeName) const
			{
				const std::string Resolved =
					ResolveAliasChain(ApplyTypeSubst(RawTypeName));
				std::string PairFirst;
				std::string PairSecond;
				if (!HasUserPairTemplate() && Runtime::ParsePairType(Resolved, PairFirst, PairSecond))
				{
					return nullptr;
				}
				const auto Exact = UserTypes.find(Resolved);
				if (Exact != UserTypes.end())
				{
					return &Exact->second;
				}
				const std::string::size_type Lt = Resolved.find('<');
				if (Lt != std::string::npos && !Resolved.empty() && Resolved.back() == '>')
				{
					const std::string BaseName = Resolved.substr(0, Lt);
					if (OpenGenericTypes.find(BaseName) != OpenGenericTypes.end())
					{
						std::vector<std::string> TypeArgs;
						SplitTopLevelTypeArgs(
							Resolved.substr(Lt + 1, Resolved.size() - Lt - 2),
							TypeArgs);
						const std::string Mono = EnsureTypeMonomorph(BaseName, TypeArgs);
						const auto MonoIt = UserTypes.find(Mono);
						if (MonoIt != UserTypes.end())
						{
							return &MonoIt->second;
						}
					}
				}
				// Open-template emission registers a bare semantic entry before bodies.
				const auto Bare = UserTypes.find(CtorInitOwnerName(Resolved));
				return Bare == UserTypes.end() ? nullptr : &Bare->second;
			}

			const FMethodDecl* ResolveMethodOnOpenTypeParam(
				const std::string& TypeParam,
				const std::string& MethodName,
				const std::vector<std::string>& ArgTypes) const
			{
				for (const FTypeParamBound& Bound : ActiveOpenBounds)
				{
					if (Bound.TypeParam != TypeParam)
					{
						continue;
					}
					if (const FUserTypeInfo* Iface =
						ResolveBoundInterfaceType(Bound.InterfaceTypeName))
					{
						if (const FMethodDecl* Method =
							ResolveMethod(*Iface, MethodName, ArgTypes))
						{
							return Method;
						}
					}
					// Fallback: own methods only (no parent walk) when mono unavailable.
					const std::vector<FMethodDecl>* Methods =
						FindBoundInterfaceMethods(Bound.InterfaceTypeName);
					if (!Methods)
					{
						continue;
					}
					FUserTypeInfo Fake;
					Fake.bIsInterface = true;
					Fake.Methods = Methods;
					if (const FMethodDecl* Method =
						ResolveMethod(Fake, MethodName, ArgTypes))
					{
						return Method;
					}
				}
				return nullptr;
			}

			// Type of a method resolved through a `where` bound (subst iface apps).
			std::string BoundMethodReturnType(
				const std::string& TypeParam,
				const std::string& MethodName,
				const std::vector<std::string>& ArgTypes) const
			{
				for (const FTypeParamBound& Bound : ActiveOpenBounds)
				{
					if (Bound.TypeParam != TypeParam)
					{
						continue;
					}
					if (const FUserTypeInfo* Iface =
						ResolveBoundInterfaceType(Bound.InterfaceTypeName))
					{
						if (const FMethodDecl* Method =
							ResolveMethod(*Iface, MethodName, ArgTypes))
						{
							return ResolveAliasChain(
								SubstForUserType(*Iface, Method->ReturnType));
						}
					}
					const std::vector<FMethodDecl>* Methods =
						FindBoundInterfaceMethods(Bound.InterfaceTypeName);
					if (!Methods)
					{
						continue;
					}
					FUserTypeInfo Fake;
					Fake.bIsInterface = true;
					Fake.Methods = Methods;
					if (const FMethodDecl* Method =
						ResolveMethod(Fake, MethodName, ArgTypes))
					{
						return ResolveAliasChain(ApplyTypeSubst(Method->ReturnType));
					}
				}
				return {};
			}

			bool IsEntryMain(const FFunctionDecl& Function) const
			{
				const std::string ReturnType = ResolveAliasChain(Function.ReturnType);
				if (Function.Name != "main"
					|| (ReturnType != "void" && ReturnType != "int")
					|| !Function.TypeParams.empty())
				{
					return false;
				}
				if (!(EntryModulePath.empty() || Function.ModulePath == EntryModulePath))
				{
					return false;
				}
				if (Function.Parameters.empty())
				{
					return true;
				}
				return Function.Parameters.size() == 1
					&& Runtime::IsScriptArgsArrayTypeName(
						ResolveAliasChain(Function.Parameters[0].TypeName));
			}

			std::string MangleFreeFunction(
				const std::string& Name,
				const std::string& ModulePath,
				const bool bExported) const
			{
				// True C++ overloads: same base name, distinguished by parameter types.
				// Non-exported names keep a module suffix to avoid cross-module collisions
				// when import flattens into one TU.
				std::string Result = "vs_" + SanitizeSymbolName(Name);
				if (!bExported)
				{
					Result += "_m";
					Result += EncodeModuleSuffix(ModulePath.empty() ? "local" : ModulePath);
				}
				return Result;
			}

			std::string EmitFunctionCppName(const FFunctionDecl& Function) const
			{
				if (IsEntryMain(Function))
				{
					return kScriptMainName;
				}
				std::string Name = MangleFreeFunction(
					Function.Name,
					Function.ModulePath,
					Function.bExported);
				// C++ considers `f(T)` and `f(T&)` ambiguous for an lvalue. VeroScript
				// distinguishes explicit/implicit ref binding, so encode ref qualifiers in
				// the generated symbol instead of relying on C++ overload resolution.
				for (std::size_t Index = 0; Index < Function.Parameters.size(); ++Index)
				{
					const FFunctionParam& Param = Function.Parameters[Index];
					if (!Param.bIsRef)
					{
						continue;
					}
					Name += Param.bIsConst ? "_cref" : "_ref";
					Name += std::to_string(Index);
				}
				return Name;
			}

			std::string MangleMethod(
				const std::string& TypeName,
				const std::string& MethodName) const
			{
				return "vs_" + SanitizeForMangle(TypeName) + '_'
					+ SanitizeSymbolName(MethodName);
			}

			std::string MangleCtor(const std::string& TypeName) const
			{
				return "vs_" + SanitizeForMangle(TypeName) + "_new";
			}

			std::string MangleCtorInit(const std::string& TypeName) const
			{
				return "vs_" + SanitizeForMangle(TypeName) + "_init";
			}

			std::string MangleSpecialization(
				const FFunctionDecl& Generic,
				const std::vector<std::string>& TypeArgs) const
			{
				// Keep type-arg suffix so monomorphizations cannot collide when
				// parameter lists are identical (e.g. noop<T>()) or only return type differs.
				return MangleFreeFunction(
					Generic.Name,
					Generic.ModulePath,
					Generic.bExported)
						+
					"_g_" + JoinMangledTypes(TypeArgs);
			}

			static std::string MakeSpecializationKey(
				const FFunctionDecl& Generic,
				const std::vector<std::string>& TypeArgs)
			{
				std::string Key = Generic.ModulePath + "\n" + Generic.Name + "\n"
					+ std::to_string(Generic.Parameters.size());
				for (const std::string& Arg : TypeArgs)
				{
					Key += '\n';
					Key += Arg;
				}
				for (const FFunctionParam& Param : Generic.Parameters)
				{
					Key += '\n';
					Key += Param.TypeName;
				}
				return Key;
			}

			bool BindTypePatternWithParams(
				const std::string& Pattern,
				const std::string& Concrete,
				const std::vector<std::string>& TypeParams,
				std::unordered_map<std::string, std::string>& Bindings) const
			{
				for (const std::string& TypeParam : TypeParams)
				{
					if (Pattern == TypeParam)
					{
						const auto Bound = Bindings.find(TypeParam);
						if (Bound == Bindings.end())
						{
							Bindings.emplace(TypeParam, Concrete);
							return true;
						}
						return Bound->second == Concrete;
					}
				}
				const std::size_t PatternOpen = Pattern.find('<');
				const std::size_t ConcreteOpen = Concrete.find('<');
				if (PatternOpen != std::string::npos
					&& ConcreteOpen != std::string::npos
					&& Pattern.substr(0, PatternOpen) == Concrete.substr(0, ConcreteOpen))
				{
					std::vector<std::string> PatternArgs;
					std::vector<std::string> ConcreteArgs;
					if (TryParseTypeApplication(
							Pattern,
							Pattern.substr(0, PatternOpen),
							PatternArgs)
						&& TryParseTypeApplication(
							Concrete,
							Concrete.substr(0, ConcreteOpen),
							ConcreteArgs)
						&& PatternArgs.size() == ConcreteArgs.size())
					{
						for (std::size_t Index = 0; Index < PatternArgs.size(); ++Index)
						{
							if (!BindTypePatternWithParams(
									PatternArgs[Index],
									ConcreteArgs[Index],
									TypeParams,
									Bindings))
							{
								return false;
							}
						}
						return true;
					}
				}
				return Pattern == Concrete;
			}

			std::string SubstituteWithMap(
				const std::string& TypeName,
				const std::unordered_map<std::string, std::string>& Bindings) const
			{
				const auto Exact = Bindings.find(TypeName);
				if (Exact != Bindings.end())
				{
					return Exact->second;
				}
				std::string ElementType;
				if (Runtime::ParseDynamicArrayElementType(TypeName, ElementType))
				{
					return "array<" + SubstituteWithMap(ElementType, Bindings) + ">";
				}
				std::string Result = TypeName;
				for (const auto& Entry : Bindings)
				{
					const std::string& From = Entry.first;
					const std::string& To = Entry.second;
					std::size_t Pos = 0;
					while ((Pos = Result.find(From, Pos)) != std::string::npos)
					{
						const bool bStartOk = Pos == 0
							|| (!std::isalnum(static_cast<unsigned char>(Result[Pos - 1]))
								&& Result[Pos - 1] != '_');
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

			std::size_t FindSpecializationIndex(
				const FFunctionDecl& Generic,
				const std::vector<std::string>& TypeArgs) const
			{
				for (std::size_t Index = 0; Index < Specializations.size(); ++Index)
				{
					const FSpecialization& Spec = Specializations[Index];
					if (Spec.Generic == &Generic && Spec.TypeArgs == TypeArgs)
					{
						return Index;
					}
				}
				return static_cast<std::size_t>(-1);
			}

			bool HasCollectedGenericCall(
				const FFunctionDecl& Generic,
				const std::vector<std::string>& TypeArgs) const
			{
				if (FindSpecializationIndex(Generic, TypeArgs) != static_cast<std::size_t>(-1))
				{
					return true;
				}
				// Open C++ templates share one declaration/body across concrete instances.
				for (const std::string& TypeArg : TypeArgs)
				{
					if (TypeMentionsActiveOpenParam(TypeArg))
					{
						for (const FSpecialization& Spec : Specializations)
						{
							if (Spec.Generic == &Generic) return true;
						}
						return false;
					}
				}
				return false;
			}

			std::size_t FindOrCreateSpecialization(
				const FFunctionDecl& Generic,
				const std::vector<std::string>& TypeArgs)
			{
				const std::string Key = MakeSpecializationKey(Generic, TypeArgs);
				if (SpecializationKeys.find(Key) != SpecializationKeys.end())
				{
					return FindSpecializationIndex(Generic, TypeArgs);
				}

				std::unordered_map<std::string, std::string> Bindings;
				if (TypeArgs.size() != Generic.TypeParams.size())
				{
					Fail("AOT: internal specialization arity error");
					return static_cast<std::size_t>(-1);
				}
				for (std::size_t Index = 0; Index < TypeArgs.size(); ++Index)
				{
					Bindings.emplace(Generic.TypeParams[Index], TypeArgs[Index]);
				}

				FSpecialization Spec;
				Spec.Generic = &Generic;
				Spec.TypeArgs = TypeArgs;
				Spec.ReturnType = SubstituteWithMap(Generic.ReturnType, Bindings);
				Spec.ParamTypes.reserve(Generic.Parameters.size());
				for (const FFunctionParam& Param : Generic.Parameters)
				{
					Spec.ParamTypes.push_back(SubstituteWithMap(Param.TypeName, Bindings));
				}
				Spec.CppName = MangleSpecialization(Generic, TypeArgs);
				Specializations.push_back(std::move(Spec));
				SpecializationKeys.insert(Key);
				const std::size_t CreatedIndex = Specializations.size() - 1;

				// Collect nested specializations from the specialized body.
				TValueScopeRestore<decltype(LocalTypes)> LocalScope(LocalTypes);
				TValueScopeRestore<decltype(LocalConstness)> ConstScope(LocalConstness);
				TValueScopeRestore<decltype(bInSelfContext)> SelfScope(bInSelfContext);
				TValueScopeRestore<decltype(bSelfIsConst)> SelfConstScope(bSelfIsConst);
				TValueScopeRestore<decltype(SelfTypeName)> SelfTypeScope(SelfTypeName);
				TValueScopeRestore<decltype(ActiveFunctionName)> FunctionScope(ActiveFunctionName);
				const auto SavedSubst = TypeSubst;
				const std::string SavedModule = ActiveModulePath;
				const std::string SavedNamespace = ActiveNamespace;
				TypeSubst = std::move(Bindings);
				ActiveModulePath = Generic.ModulePath;
				ActiveNamespace = NamespaceOfSymbol(Generic.Name);
				ActiveFunctionName = Generic.Name;
				LocalTypes.clear();
				LocalConstness.clear();
				bInSelfContext = false;
				bSelfIsConst = false;
				SelfTypeName.clear();
				for (const FFunctionParam& Param : Generic.Parameters)
				{
					LocalTypes[Param.Name] = Param.bIsVariadic
						? ("array<" + Param.TypeName + ">") : Param.TypeName;
					LocalConstness[Param.Name] = Param.bIsConst;
				}
				if (Generic.Body)
				{
					CollectSpecializationsInStmt(*Generic.Body);
				}
				TypeSubst = SavedSubst;
				ActiveModulePath = SavedModule;
				ActiveNamespace = SavedNamespace;
				return CreatedIndex;
			}

			bool IsVisibleFromActiveModule(const std::string& DeclModulePath, const bool bExported) const
			{
				if (!Program)
				{
					return DeclModulePath == ActiveModulePath || bExported;
				}
				return ModuleVisibility::CanAccessSymbol(
					*Program,
					DeclModulePath,
					bExported,
					ActiveModulePath);
			}

			bool HasNonGenericHandleType() const
			{
				const auto Found = UserTypes.find("handle");
				return Found != UserTypes.end() && Found->second.TypeParams.empty()
					&& IsVisibleFromActiveModule(Found->second.ModulePath, Found->second.bExported);
			}

			static std::string NamespaceOfSymbol(std::string Name)
			{
				for (std::size_t Pos = 0;
					(Pos = Name.find("::", Pos)) != std::string::npos;)
				{
					Name.replace(Pos, 2, ".");
					++Pos;
				}
				const std::size_t Generic = Name.find('<');
				if (Generic != std::string::npos)
				{
					Name.resize(Generic);
				}
				const std::size_t Dot = Name.rfind('.');
				return Dot == std::string::npos ? std::string{} : Name.substr(0, Dot);
			}

			bool NamespaceExists(const std::string& Name) const
			{
				if (!Program || Name.empty())
				{
					return false;
				}
				if (std::any_of(
						Program->Namespaces.begin(),
						Program->Namespaces.end(),
						[&](const FNamespaceDecl& Decl)
						{
							return Decl.Name == Name;
						}))
				{
					return true;
				}
				const std::string Prefix = Name + ".";
				const auto HasPrefix = [&](const std::string& SymbolName)
				{
					return SymbolName.rfind(Prefix, 0) == 0;
				};
				for (const FStructDecl& Decl : Program->Structs)
				{
					if (HasPrefix(Decl.Name))
					{
						return true;
					}
				}
				for (const FClassDecl& Decl : Program->Classes)
				{
					if (HasPrefix(Decl.Name))
					{
						return true;
					}
				}
				for (const FInterfaceDecl& Decl : Program->Interfaces)
				{
					if (HasPrefix(Decl.Name))
					{
						return true;
					}
				}
				for (const FEnumDecl& Decl : Program->Enums)
				{
					if (HasPrefix(Decl.Name))
					{
						return true;
					}
				}
				for (const FTypeAliasDecl& Decl : Program->TypeAliases)
				{
					if (HasPrefix(Decl.Name))
					{
						return true;
					}
				}
				for (const FFunctionDecl& Decl : Program->Functions)
				{
					if (HasPrefix(Decl.Name))
					{
						return true;
					}
				}
				for (const std::unique_ptr<FStmt>& Statement : Program->TopLevelStatements)
				{
					if (Statement && Statement->Kind == EStmtKind::VarDecl
						&& HasPrefix(static_cast<const FVarDeclStmt&>(*Statement).Name))
					{
						return true;
					}
				}
				if (!HostBindings)
				{
					return false;
				}
				const auto HostNameHasPrefix = [&](const std::string_view HostName)
				{
					std::string Normalized(HostName);
					for (std::size_t Pos = 0;
						(Pos = Normalized.find("::", Pos)) != std::string::npos;)
					{
						Normalized.replace(Pos, 2, ".");
						++Pos;
					}
					return HasPrefix(Normalized);
				};
				return std::any_of(
						HostBindings->GetTypes().begin(),
						HostBindings->GetTypes().end(),
						[&](const FHostTypeDesc& Type)
						{
							return HostNameHasPrefix(Type.Name);
						})
					|| std::any_of(
						HostBindings->GetTypeAliases().begin(),
						HostBindings->GetTypeAliases().end(),
						[&](const FHostTypeAliasDesc& Alias)
						{
							return HostNameHasPrefix(Alias.Name);
						})
					|| std::any_of(
						HostBindings->GetGenericTypeTemplates().begin(),
						HostBindings->GetGenericTypeTemplates().end(),
						[&](const FGenericHostTypeTemplateDesc& Template)
						{
							return HostNameHasPrefix(Template.Name);
						});
			}

			std::vector<std::string> BuildFreeFunctionLookupNames(
				std::string Name,
				const FSourceLocation* const ReferenceLocation = nullptr) const
			{
				if (!Program || Name.empty())
				{
					return {};
				}
				const bool bGlobalQualified = Name.rfind("::", 0) == 0;
				if (bGlobalQualified)
				{
					Name.erase(0, 2);
				}
				for (std::size_t Pos = 0;
					(Pos = Name.find("::", Pos)) != std::string::npos;)
				{
					Name.replace(Pos, 2, ".");
					++Pos;
				}
				if (bGlobalQualified
					|| Name.find('.') != std::string::npos
					|| Name.find('\x1F') != std::string::npos)
				{
					return {Name};
				}

				const auto HasAccessibleFunction = [&](const std::string& Candidate)
				{
					return std::any_of(
						Program->Functions.begin(),
						Program->Functions.end(),
						[&](const FFunctionDecl& Function)
						{
							return Function.Name == Candidate
									&& IsVisibleFromActiveModule(
										Function.ModulePath, Function.bExported);
						});
				};

				std::string Scope = ActiveNamespace;
				while (!Scope.empty())
				{
					const std::string Candidate = Scope + "." + Name;
					if (HasAccessibleFunction(Candidate))
					{
						return {Candidate};
					}
					const std::size_t Dot = Scope.rfind('.');
					Scope = Dot == std::string::npos ? std::string{} : Scope.substr(0, Dot);
				}
				if (HasAccessibleFunction(Name))
				{
					return {Name};
				}

				std::vector<std::string> Names;
				for (const FUsingNamespaceDecl& Directive : Program->UsingNamespaces)
				{
					if (Directive.ModulePath != ActiveModulePath
						|| (!Directive.ScopeNamespace.empty()
							&& ActiveNamespace != Directive.ScopeNamespace
							&& ActiveNamespace.rfind(Directive.ScopeNamespace + ".", 0) != 0))
					{
						continue;
					}
					const auto LookupLocation = ReferenceLocation ? ResolveProgramLookupLocation(Program, *ReferenceLocation) : FSourceLocation{};
					if (ReferenceLocation && !LookupLocation.FileLabel.empty()
						&& (LookupLocation.Line < Directive.Location.Line
							|| (LookupLocation.Line == Directive.Location.Line
								&& LookupLocation.Column < Directive.Location.Column)))
					{
						continue;
					}

					const bool bRootDirective =
						Directive.NamespaceName.rfind("::", 0) == 0;
					std::string DirectiveScope = bRootDirective
						? std::string{}
						: Directive.ScopeNamespace;
					for (;;)
					{
						const std::string DirectiveName = bRootDirective
							? Directive.NamespaceName.substr(2)
							: Directive.NamespaceName;
						const std::string Target = DirectiveScope.empty()
							? DirectiveName
							: DirectiveScope + "." + DirectiveName;
						if (NamespaceExists(Target))
						{
							const std::string Candidate = Target + "." + Name;
							if (HasAccessibleFunction(Candidate)
								&& std::find(Names.begin(), Names.end(), Candidate) == Names.end())
							{
								Names.push_back(Candidate);
							}
							break;
						}
						if (DirectiveScope.empty())
						{
							break;
						}
						const std::size_t Dot = DirectiveScope.rfind('.');
						DirectiveScope = Dot == std::string::npos
							? std::string{}
							: DirectiveScope.substr(0, Dot);
					}
				}
				return Names;
			}

			std::string GlobalModulePath(const FVarDeclStmt& Global) const
			{
				return std::string(Global.Location.FileLabel);
			}

			std::string MangleGlobal(const FVarDeclStmt& Global) const
			{
				std::string Result = "vs_global_" + EncodeModuleSuffix(Global.Name);
				if (!Global.bExported)
				{
					const std::string ModulePath = GlobalModulePath(Global);
					Result += "_m";
					Result += EncodeModuleSuffix(ModulePath.empty() ? "local" : ModulePath);
				}
				return Result;
			}

			const FVarDeclStmt* FindGlobalExact(const std::string& Name) const
			{
				if (!Program || Name.empty())
				{
					return nullptr;
				}
				const FVarDeclStmt* Selected = nullptr;
				for (const std::unique_ptr<FStmt>& Statement : Program->TopLevelStatements)
				{
					if (!Statement || Statement->Kind != EStmtKind::VarDecl)
					{
						continue;
					}
					const FVarDeclStmt& Global = static_cast<const FVarDeclStmt&>(*Statement);
					const std::string ModulePath = GlobalModulePath(Global);
					if (Global.Name != Name
						|| !IsVisibleFromActiveModule(ModulePath, Global.bExported))
					{
						continue;
					}
					if (ModulePath == ActiveModulePath)
					{
						return &Global;
					}
					if (!Selected)
					{
						Selected = &Global;
					}
				}
				return Selected;
			}

			const FVarDeclStmt* ResolveGlobalVariable(
				std::string Name,
				const FSourceLocation* const ReferenceLocation = nullptr) const
			{
				if (!Program || Name.empty())
				{
					return nullptr;
				}
				const bool bGlobalQualified = Name.rfind("::", 0) == 0;
				if (bGlobalQualified)
				{
					Name.erase(0, 2);
				}
				for (std::size_t Pos = 0;
					(Pos = Name.find("::", Pos)) != std::string::npos;)
				{
					Name.replace(Pos, 2, ".");
					++Pos;
				}
				if (bGlobalQualified
					|| Name.find('.') != std::string::npos
					|| Name.find('\x1F') != std::string::npos)
				{
					return FindGlobalExact(Name);
				}

				std::string Scope = ActiveNamespace;
				while (!Scope.empty())
				{
					if (const FVarDeclStmt* Global = FindGlobalExact(Scope + "." + Name))
					{
						return Global;
					}
					const std::size_t Dot = Scope.rfind('.');
					Scope = Dot == std::string::npos ? std::string{} : Scope.substr(0, Dot);
				}
				if (const FVarDeclStmt* Global = FindGlobalExact(Name))
				{
					return Global;
				}

				const FVarDeclStmt* Selected = nullptr;
				std::string SelectedName;
				for (const FUsingNamespaceDecl& Directive : Program->UsingNamespaces)
				{
					if (Directive.ModulePath != ActiveModulePath
						|| (!Directive.ScopeNamespace.empty()
							&& ActiveNamespace != Directive.ScopeNamespace
							&& ActiveNamespace.rfind(Directive.ScopeNamespace + ".", 0) != 0))
					{
						continue;
					}
					const auto LookupLocation = ReferenceLocation ? ResolveProgramLookupLocation(Program, *ReferenceLocation) : FSourceLocation{};
					if (ReferenceLocation && !LookupLocation.FileLabel.empty()
						&& (LookupLocation.Line < Directive.Location.Line
							|| (LookupLocation.Line == Directive.Location.Line
								&& LookupLocation.Column < Directive.Location.Column)))
					{
						continue;
					}

					const bool bRootDirective = Directive.NamespaceName.rfind("::", 0) == 0;
					std::string DirectiveScope = bRootDirective
						? std::string{}
						: Directive.ScopeNamespace;
					for (;;)
					{
						const std::string DirectiveName = bRootDirective
							? Directive.NamespaceName.substr(2)
							: Directive.NamespaceName;
						const std::string Target = DirectiveScope.empty()
							? DirectiveName
							: DirectiveScope + "." + DirectiveName;
						if (NamespaceExists(Target))
						{
							const std::string Candidate = Target + "." + Name;
							if (const FVarDeclStmt* Global = FindGlobalExact(Candidate))
							{
								if (Selected && SelectedName != Candidate)
								{
									Fail(
										"AOT: ambiguous global name from using namespace: "
											+ Name);
									return nullptr;
								}
								Selected = Global;
								SelectedName = Candidate;
							}
							break;
						}
						if (DirectiveScope.empty())
						{
							break;
						}
						const std::size_t Dot = DirectiveScope.rfind('.');
						DirectiveScope = Dot == std::string::npos
							? std::string{}
							: DirectiveScope.substr(0, Dot);
					}
				}
				return Selected;
			}

			bool IsConstExtensionReceiver(const FExpr& Expression)
			{
				switch (Expression.Kind)
				{
				case EExprKind::This:
					return bSelfIsConst;
				case EExprKind::Ref:
					{
						const FRefExpr& Ref = static_cast<const FRefExpr&>(Expression);
						return Ref.Operand && IsConstExtensionReceiver(*Ref.Operand);
					}
				case EExprKind::Identifier:
					{
						const std::string& Name =
							static_cast<const FIdentifierExpr&>(Expression).Name;
						const auto Local = LocalConstness.find(Name);
						if (Local != LocalConstness.end())
						{
							return Local->second;
						}
						if (bInSelfContext)
						{
							const auto TypeIt = UserTypes.find(SelfTypeName);
							if (TypeIt != UserTypes.end() && TypeIt->second.Fields)
							{
								for (const FFieldDecl& Field : *TypeIt->second.Fields)
								{
									if (Field.Name == Name)
									{
										return !Field.bIsMutable
										&& (bSelfIsConst || Field.bIsConst);
									}
								}
							}
						}
						if (const FVarDeclStmt* Global =
								ResolveGlobalVariable(Name, &Expression.Location))
						{
							return Global->bIsConst;
						}
						return false;
					}
				case EExprKind::MemberAccess:
					{
						const FMemberAccessExpr& Member =
							static_cast<const FMemberAccessExpr&>(Expression);
						if (!Member.Object)
						{
							return false;
						}
						std::string NamespaceGlobalName;
						if (TryBuildNamespaceCallName(
								*Member.Object, Member.Member, NamespaceGlobalName))
						{
							if (const FVarDeclStmt* Global = ResolveGlobalVariable(
									NamespaceGlobalName, &Expression.Location))
							{
								return Global->bIsConst;
							}
						}
						const std::string ObjectType = InferExprType(*Member.Object);
						const auto TypeIt = UserTypes.find(ObjectType);
						if (TypeIt != UserTypes.end() && TypeIt->second.Fields)
						{
							for (const FFieldDecl& Field : *TypeIt->second.Fields)
							{
								if (Field.Name == Member.Member)
								{
									if (Field.bIsMutable)
									{
										return false;
									}
									if (IsConstExtensionReceiver(*Member.Object))
									{
										return true;
									}
									return Field.bIsConst;
								}
							}
						}
						return IsConstExtensionReceiver(*Member.Object);
					}
				case EExprKind::IndexAccess:
					{
						const FIndexAccessExpr& Index =
							static_cast<const FIndexAccessExpr&>(Expression);
						return Index.Object && IsConstExtensionReceiver(*Index.Object);
					}
				case EExprKind::Call:
					{
						const FCallExpr& Call = static_cast<const FCallExpr&>(Expression);
						if (Call.Callee == "const_cast" && !Call.TypeArgs.empty())
						{
							return false;
						}
						const std::string Type = InferExprType(Expression);
						if (Runtime::IsConstReferenceTypeName(Type))
						{
							return true;
						}
						return Call.Callee == "cast" && Call.Arguments.size() == 1
							&& Call.Arguments.front()
							&& IsConstExtensionReceiver(*Call.Arguments.front());
					}
				case EExprKind::MethodCall:
					{
						const FMethodCallExpr& Call =
							static_cast<const FMethodCallExpr&>(Expression);
						std::vector<std::string> Args;
						return (Call.Object && Call.Method == "get"
								&& TryParseSoftBuiltinType(
									InferExprType(*Call.Object), "readonly", Args)
								&& Args.size() == 1)
							|| Runtime::IsConstReferenceTypeName(InferExprType(Expression));
					}
				default:
					return false;
				}
			}

			bool CanBindAotImplicitRefArgument(const FExpr& Expression)
			{
				switch (Expression.Kind)
				{
				case EExprKind::This:
					return bInSelfContext;
				case EExprKind::Ref:
					{
						const FRefExpr& Ref = static_cast<const FRefExpr&>(Expression);
						return Ref.Operand && CanBindAotImplicitRefArgument(*Ref.Operand);
					}
				case EExprKind::Identifier:
					{
						const std::string& Name =
							static_cast<const FIdentifierExpr&>(Expression).Name;
						if (LocalTypes.find(Name) != LocalTypes.end())
						{
							return true;
						}
						if (bInSelfContext)
						{
							if (const FUserTypeInfo* Type = ResolveUserTypeForLookup(SelfTypeName))
							{
								if (!FieldType(*Type, Name).empty())
								{
									return true;
								}
							}
						}
						return ResolveGlobalVariable(Name, &Expression.Location) != nullptr;
					}
				case EExprKind::MemberAccess:
					{
						const FMemberAccessExpr& Member =
							static_cast<const FMemberAccessExpr&>(Expression);
						if (!Member.Object)
						{
							return false;
						}
						std::string NamespaceGlobalName;
						if (TryBuildNamespaceCallName(
								*Member.Object, Member.Member, NamespaceGlobalName)
							&& ResolveGlobalVariable(
								NamespaceGlobalName, &Expression.Location))
						{
							return true;
						}
						const std::string ObjectType = InferExprType(*Member.Object);
						const FUserTypeInfo* Type = ResolveUserTypeForLookup(ObjectType);
						return Type && !FieldType(*Type, Member.Member).empty();
					}
				case EExprKind::IndexAccess:
					{
						const FExpr* Root = &Expression;
						while (Root && Root->Kind == EExprKind::IndexAccess)
						{
							Root = static_cast<const FIndexAccessExpr&>(*Root).Object.get();
						}
						return Root && CanBindAotImplicitRefArgument(*Root);
					}
				default:
					return false;
				}
			}

			std::string SpawnCallerModulePath() const
			{
				if (ActiveFunctionName == "__std_spawn"
					&& ActiveModulePath.find("concurrent.vs") != std::string::npos)
				{
					return NormalizeModulePath(EntryModulePath);
				}
				return NormalizeModulePath(ActiveModulePath);
			}

			std::string NormalizeModulePath(const std::string& ModulePath) const
			{
				return ModulePath.empty() ? "local" : ModulePath;
			}

			bool IsSpawnVisibleFromCaller(
				const std::string& WorkerModulePath,
				const bool bExported,
				const std::string& CallerModulePath) const
			{
				if (!Program)
				{
					return bExported
						|| NormalizeModulePath(WorkerModulePath)
						== NormalizeModulePath(CallerModulePath);
				}
				return ModuleVisibility::CanAccessSymbol(
					*Program,
					WorkerModulePath,
					bExported,
					CallerModulePath);
			}

			const FFunctionDecl* ResolveSpawnWorker(
				const std::string& WorkerName,
				const std::string& ChannelElemType) const
			{
				const std::string CallerModule = SpawnCallerModulePath();
				std::vector<const FFunctionDecl*> Matches;
				for (const FFunctionDecl* Function : ConcreteFunctions)
				{
					if (Function->Name != WorkerName || Function->ReturnType != "void"
						|| Function->Parameters.size() != 1)
					{
						continue;
					}
					std::string Elem;
					if (!TryParseChannelType(
							Function->Parameters[0].TypeName, Elem)
								|| Elem != ChannelElemType)
					{
						continue;
					}
					if (!IsSpawnVisibleFromCaller(
							Function->ModulePath, Function->bExported, CallerModule))
					{
						continue;
					}
					Matches.push_back(Function);
				}
				if (Matches.empty())
				{
					return nullptr;
				}
				std::vector<const FFunctionDecl*> Local;
				for (const FFunctionDecl* Function : Matches)
				{
					if (NormalizeModulePath(Function->ModulePath) == CallerModule)
					{
						Local.push_back(Function);
					}
				}
				if (Local.size() == 1)
				{
					return Local[0];
				}
				if (Local.size() > 1)
				{
					return nullptr;
				}
				return Matches.size() == 1 ? Matches[0] : nullptr;
			}

			const FFunctionDecl* ResolveConcreteOverload(
				const std::string& Name,
				const std::vector<std::string>& ArgTypes,
				const FSourceLocation* const ReferenceLocation = nullptr) const
			{
				const std::vector<std::string> LookupNames =
					BuildFreeFunctionLookupNames(Name, ReferenceLocation);
				std::vector<const FFunctionDecl*> Visible;
				for (const FFunctionDecl* Function : ConcreteFunctions)
				{
					if (std::find(LookupNames.begin(), LookupNames.end(), Function->Name)
						== LookupNames.end())
					{
						continue;
					}
					if (!IsVisibleFromActiveModule(Function->ModulePath, Function->bExported))
					{
						continue;
					}
					if (Function->Parameters.size() != ArgTypes.size())
					{
						continue;
					}
					bool bOk = true;
					for (std::size_t Index = 0; Index < ArgTypes.size(); ++Index)
					{
						if (!TypesCompatible(Function->Parameters[Index].TypeName, ArgTypes[Index]))
						{
							bOk = false;
							break;
						}
					}
					if (!bOk)
					{
						continue;
					}
					Visible.push_back(Function);
				}
				if (Visible.empty())
				{
					return nullptr;
				}
				return Visible.size() == 1 ? Visible[0] : nullptr;
			}

			bool IsAotSubtypeOf(
				const std::string& RawDerived,
				const std::string& RawBase) const
			{
				const std::string Derived = ResolveAliasChain(ApplyTypeSubst(RawDerived));
				const std::string Base = ResolveAliasChain(ApplyTypeSubst(RawBase));
				if (Derived == Base)
				{
					return true;
				}
				std::vector<std::string> Pending{Derived};
				std::unordered_set<std::string> Visited;
				while (!Pending.empty())
				{
					const std::string Current = std::move(Pending.back());
					Pending.pop_back();
					if (!Visited.insert(Current).second)
					{
						continue;
					}
					const auto Found = UserTypes.find(Current);
					if (Found == UserTypes.end())
					{
						continue;
					}
					bool bFoundBase = false;
					auto AddParent = [&](const std::string& RawParent)
					{
						const std::string Parent = ResolveAliasChain(ApplyTypeSubst(RawParent));
						if (Parent == Base)
						{
							bFoundBase = true;
							return;
						}
						Pending.push_back(Parent);
					};
					for (const std::string& Parent : Found->second.BaseTypeNames)
					{
						AddParent(Parent);
					}
					for (const std::string& Parent : Found->second.InterfaceTypeNames)
					{
						AddParent(Parent);
					}
					if (bFoundBase)
					{
						return true;
					}
				}
				return false;
			}

			std::vector<std::string> CollectAotBaseTypes(
				const std::string& RawTypeName) const
			{
				const std::string TypeName = ResolveAliasChain(ApplyTypeSubst(RawTypeName));
				std::vector<std::string> Result;
				std::vector<std::string> Pending{TypeName};
				std::unordered_set<std::string> Visited{TypeName};
				while (!Pending.empty())
				{
					const std::string Current = std::move(Pending.back());
					Pending.pop_back();
					const auto Found = UserTypes.find(Current);
					if (Found == UserTypes.end())
					{
						continue;
					}
					const auto AddParent = [&](const std::string& RawParent)
					{
						const std::string Parent = ResolveAliasChain(ApplyTypeSubst(RawParent));
						if (!Parent.empty() && Visited.insert(Parent).second)
						{
							Result.push_back(Parent);
							Pending.push_back(Parent);
						}
					};
					for (const std::string& Parent : Found->second.BaseTypeNames)
					{
						AddParent(Parent);
					}
					for (const std::string& Parent : Found->second.InterfaceTypeNames)
					{
						AddParent(Parent);
					}
				}
				return Result;
			}

			int ScoreAotValueConversion(
				const std::string& RawParam,
				const std::string& RawArg,
				const bool bAllowUserDefinedConversions = true) const
			{
				if (RawArg.empty())
				{
					return 1;
				}
				const std::string Param = ResolveAliasChain(ApplyTypeSubst(RawParam));
				const std::string ResolvedArg = ResolveAliasChain(ApplyTypeSubst(RawArg));
				// Reference binding/constness is checked by the caller. Conversion
				// scoring concerns the value read from a reference-returning call.
				const std::string Arg = Runtime::IsReferenceTypeName(ResolvedArg)
					? std::string(Runtime::StripReferenceTypeName(ResolvedArg)) : ResolvedArg;
				if (Param == Arg)
				{
					return 16;
				}
				if (IsAotSubtypeOf(Arg, Param))
				{
					return 12;
				}

				auto IntegerInfo = [](const std::string& Type, bool& bUnsigned, int& Bits)
				{
					bUnsigned = false;
					Bits = 0;
					if (Type == "char" || Type == "int8")
					{
						Bits = 8;
					}
					else if (Type == "short" || Type == "int16")
					{
						Bits = 16;
					}
					else if (Type == "int" || Type == "int32")
					{
						Bits = 32;
					}
					else if (Type == "long" || Type == "int64")
					{
						Bits = 64;
					}
					else if (Type == "unsigned char" || Type == "uint8")
					{
						bUnsigned = true;
						Bits = 8;
					}
					else if (Type == "unsigned short" || Type == "uint16")
					{
						bUnsigned = true;
						Bits = 16;
					}
					else if (Type == "unsigned" || Type == "unsigned int" || Type == "uint32")
					{
						bUnsigned = true;
						Bits = 32;
					}
					else if (Type == "unsigned long" || Type == "uint64")
					{
						bUnsigned = true;
						Bits = 64;
					}
					return Bits != 0;
				};
				bool bArgUnsigned = false;
				bool bParamUnsigned = false;
				int ArgBits = 0;
				int ParamBits = 0;
				const bool bArgInteger = IntegerInfo(Arg, bArgUnsigned, ArgBits);
				const bool bParamInteger = IntegerInfo(Param, bParamUnsigned, ParamBits);
				const bool bArgNumeric = bArgInteger || Arg == "float" || Arg == "double";
				const bool bParamNumeric = bParamInteger || Param == "float" || Param == "double";
				if (!bArgNumeric || !bParamNumeric)
				{
					if (bAllowUserDefinedConversions)
					{
						const auto TargetIt = UserTypes.find(Param);
						if (TargetIt != UserTypes.end() && !TargetIt->second.bIsInterface)
						{
							const FConstructorDecl* const Constructor = ResolveCtor(
								TargetIt->second, {Arg}, false, false);
							if (Constructor
								&& Constructor->Parameters.size() == 1
								&& !Constructor->Parameters.front().bIsRef
								&& !Constructor->Parameters.front().bIsVariadic)
							{
								// Any standard conversion is preferable to a user-defined one.
								return 4;
							}
						}
					}
					return -1;
				}
				const bool bSafeInteger = bArgInteger && bParamInteger
					&& ((bArgUnsigned == bParamUnsigned && ArgBits <= ParamBits)
						|| (bArgUnsigned && !bParamUnsigned && ArgBits < ParamBits));
				const bool bSafeFloat = Arg == "float" && Param == "double";
				const bool bSafeIntegerToFloat = bArgInteger
					&& ((Param == "float" && ArgBits <= 16)
						|| (Param == "double" && ArgBits <= 32));
				return (bSafeInteger || bSafeFloat || bSafeIntegerToFloat) ? 10 : 6;
			}

			const FFunctionDecl* ResolveConcreteCallOverloadFromArgs(
				const std::string& Name,
				const std::vector<std::unique_ptr<FExpr>>& Arguments,
				const bool bImplicitReceiverRefBinding,
				const std::vector<std::string>& ArgTypes,
				const FSourceLocation* const ReferenceLocation = nullptr)
			{

				const std::vector<std::string> LookupNames =
					BuildFreeFunctionLookupNames(Name, ReferenceLocation);
				const FFunctionDecl* Match = nullptr;
				int BestScore = -1;
				bool bAmbiguous = false;
				for (const FFunctionDecl* Function : ConcreteFunctions)
				{
					if (std::find(LookupNames.begin(), LookupNames.end(), Function->Name)
						== LookupNames.end()
						|| !IsVisibleFromActiveModule(Function->ModulePath, Function->bExported))
					{
						continue;
					}

					const bool bVariadic = !Function->Parameters.empty()
						&& Function->Parameters.back().bIsVariadic;
					const std::size_t FixedCount = Function->Parameters.size() - (bVariadic ? 1u : 0u);
					std::size_t RequiredCount = 0;
					for (std::size_t Index = 0; Index < FixedCount; ++Index)
					{
						if (!Function->Parameters[Index].DefaultValue)
						{
							++RequiredCount;
						}
					}
					if (ArgTypes.size() < RequiredCount || (!bVariadic && ArgTypes.size() > FixedCount))
					{
						continue;
					}

					int Score = 0;
					bool bCompatible = true;
					for (std::size_t Index = 0; Index < ArgTypes.size(); ++Index)
					{
						const std::size_t ParamIndex = Index < FixedCount
							? Index
							: Function->Parameters.size() - 1;
						const FFunctionParam& Param = Function->Parameters[ParamIndex];
						const bool bArgumentIsRef = Index < Arguments.size()
							&& (Arguments[Index]->Kind == EExprKind::Ref
								|| (bImplicitReceiverRefBinding && Index == 0));
						if (Param.bIsRef != bArgumentIsRef)
						{
							bCompatible = false;
							break;
						}
						if (bArgumentIsRef && !Param.bIsConst && Index < Arguments.size())
						{
							const FExpr* Target = Arguments[Index].get();
							if (Target->Kind == EExprKind::Ref)
							{
								Target = static_cast<const FRefExpr&>(*Target).Operand.get();
							}
							if (Target && IsConstExtensionReceiver(*Target))
							{
								bCompatible = false;
								break;
							}
						}
						const int Contribution = ScoreAotValueConversion(
							Param.TypeName, ArgTypes[Index]);
						if (Contribution < 0
							|| (Param.bIsRef && Contribution < 16))
						{
							bCompatible = false;
							break;
						}
						Score += Contribution;
					}
					if (!bCompatible)
					{
						continue;
					}
					if (Score > BestScore)
					{
						BestScore = Score;
						Match = Function;
						bAmbiguous = false;
					}
					else if (Score == BestScore)
					{
						bAmbiguous = true;
					}
				}
				return bAmbiguous ? nullptr : Match;
			}

			const FFunctionDecl* ResolveConcreteCallOverload(
				const std::string& Name,
				const FCallExpr& Call,
				const std::vector<std::string>& ArgTypes,
				const FSourceLocation* const ReferenceLocation = nullptr)
			{
				return ResolveConcreteCallOverloadFromArgs(
					Name,
					Call.Arguments,
					Call.bImplicitReceiverRefBinding,
					ArgTypes,
					ReferenceLocation);
			}

			const FFunctionDecl* ResolveConcreteCallOverload(
				const std::string& Name,
				const FMethodCallExpr& Call,
				const std::vector<std::string>& ArgTypes,
				const FSourceLocation* const ReferenceLocation = nullptr)
			{
				return ResolveConcreteCallOverloadFromArgs(
					Name, Call.Arguments, false, ArgTypes, ReferenceLocation);
			}

			std::string QualifyGenericDefaultType(const FFunctionDecl& Generic, std::string TypeName) const
			{
				// Preserve declaration references when a caller has an identically
				// named open parameter. Dependent defaults still use their bindings.
				for (std::size_t Position = 0; Position < TypeName.size();)
				{
					const auto First = Position;
					const auto Character = static_cast<unsigned char>(TypeName[Position]);
					if (!std::isalpha(Character) && Character != '_')
					{
						++Position;
						continue;
					}
					while (Position < TypeName.size() && (std::isalnum(static_cast<unsigned char>(TypeName[Position]))
						|| TypeName[Position] == '_'))
					{
						++Position;
					}
					const auto Name = TypeName.substr(First, Position - First);
					if ((First != 0 && (TypeName[First - 1] == ':' || TypeName[First - 1] == '.'
						|| TypeName[First - 1] == '\x1F'))
						|| std::find(Generic.TypeParams.begin(), Generic.TypeParams.end(), Name) != Generic.TypeParams.end())
					{
						continue;
					}
					if (UserTypes.find(Name) != UserTypes.end() || RootTypeAliases.find(Name) != RootTypeAliases.end()
						|| EnumTypeNames.find(Name) != EnumTypeNames.end())
					{
						TypeName.insert(First, "::");
						Position += 2;
					}
				}
				return TypeName;
			}

			bool InferGenericTypeArgsFromTypes(
				const FFunctionDecl& Generic,
				const std::vector<std::string>& ExplicitTypeArgs,
				const std::vector<std::string>& ArgTypes,
				const std::string& CalleeName,
				std::vector<std::string>& OutTypeArgs,
				const bool bReportErrors)
			{
				OutTypeArgs.clear();
				if (!Generic.TypeParamDefaults.empty()
					&& Generic.TypeParamDefaults.size() != Generic.TypeParams.size())
				{
					if (bReportErrors)
					{
						Fail("AOT: invalid generic default type-argument metadata for '"
							+ CalleeName + "'");
					}
					return false;
				}

				std::unordered_map<std::string, std::string> Bindings;
				const std::size_t ExplicitCount = ExplicitTypeArgs.size();
				if (!ExplicitTypeArgs.empty())
				{
					if (ExplicitTypeArgs.size() > Generic.TypeParams.size())
					{
						if (bReportErrors)
						{
							Fail(
								"AOT: generic call '" + CalleeName
									+ "' type argument count mismatch");
						}
						return false;
					}
					for (std::size_t Index = 0; Index < ExplicitCount; ++Index)
					{
						Bindings.emplace(Generic.TypeParams[Index], ApplyTypeSubst(ExplicitTypeArgs[Index]));
					}
				}

				const bool bVariadic = !Generic.Parameters.empty()
					&& Generic.Parameters.back().bIsVariadic;
				const std::size_t FixedCount = Generic.Parameters.size() - (bVariadic ? 1u : 0u);
				std::size_t RequiredCount = FixedCount;
				while (RequiredCount > 0 && Generic.Parameters[RequiredCount - 1].DefaultValue)
				{
					--RequiredCount;
				}
				if (ArgTypes.size() < RequiredCount || (!bVariadic && ArgTypes.size() > FixedCount))
				{
					if (bReportErrors)
					{
						Fail(
							"AOT: generic call '" + CalleeName
								+ "' argument count mismatch");
					}
					return false;
				}
				for (std::size_t Index = 0; Index < ArgTypes.size(); ++Index)
				{
					const std::size_t ParamIndex = Index < FixedCount
						? Index : Generic.Parameters.size() - 1;
					const std::string ResolvedArgType = ResolveAliasChain(ApplyTypeSubst(ArgTypes[Index]));
					const std::string ArgType = Runtime::IsReferenceTypeName(ResolvedArgType)
						? std::string(Runtime::StripReferenceTypeName(ResolvedArgType)) : ResolvedArgType;
					if (ArgType.empty())
					{
						// Null and contextual expressions contribute no deduction. Explicit
						// type arguments or another argument can still bind the parameter.
						continue;
					}
					const std::string Pattern = SubstituteWithMap(
						Generic.Parameters[ParamIndex].TypeName,
						Bindings);
					bool bMentionsUnboundParam = false;
					for (const std::string& TypeParam : Generic.TypeParams)
					{
						if (Bindings.find(TypeParam) != Bindings.end())
						{
							continue;
						}
						std::size_t Pos = Pattern.find(TypeParam);
						while (Pos != std::string::npos)
						{
							const bool bStartOk = Pos == 0
								|| (!std::isalnum(static_cast<unsigned char>(Pattern[Pos - 1]))
									&& Pattern[Pos - 1] != '_');
							const std::size_t End = Pos + TypeParam.size();
							const bool bEndOk = End >= Pattern.size()
								|| (!std::isalnum(static_cast<unsigned char>(Pattern[End]))
									&& Pattern[End] != '_');
							if (bStartOk && bEndOk)
							{
								bMentionsUnboundParam = true;
								break;
							}
							Pos = Pattern.find(TypeParam, End);
						}
						if (bMentionsUnboundParam)
						{
							break;
						}
					}
					if (bMentionsUnboundParam
						&& !BindTypePatternWithParams(
							Pattern,
							ArgType,
							Generic.TypeParams,
							Bindings))
					{
						if (bReportErrors)
						{
							Fail(
								"AOT: cannot specialize generic '" + CalleeName
									+ "' (parameter type mismatch)");
						}
						return false;
					}
				}
				OutTypeArgs.reserve(Generic.TypeParams.size());
				for (std::size_t Index = 0; Index < Generic.TypeParams.size(); ++Index)
				{
					const std::string& TypeParam = Generic.TypeParams[Index];
					const auto It = Bindings.find(TypeParam);
					if (It == Bindings.end())
					{
						const std::string DeclaredDefault =
							Index < Generic.TypeParamDefaults.size()
								? Generic.TypeParamDefaults[Index]
								: std::string{};
						std::string DefaultType = SubstituteWithMap(
							QualifyGenericDefaultType(Generic, DeclaredDefault), Bindings);
						if (DefaultType.empty())
						{
							if (bReportErrors)
							{
								Fail(
									"AOT: cannot specialize generic '" + CalleeName
										+ "': unbound type parameter '" + TypeParam + "'");
							}
							return false;
						}
						OutTypeArgs.push_back(DefaultType);
						Bindings.emplace(TypeParam, std::move(DefaultType));
						continue;
					}
					OutTypeArgs.push_back(It->second);
				}
				return true;
			}

			bool InferGenericTypeArgs(
				const FFunctionDecl& Generic,
				const FCallExpr& Call,
				std::vector<std::string>& OutTypeArgs,
				const bool bReportErrors)
			{
				std::vector<std::string> ArgTypes;
				ArgTypes.reserve(Call.Arguments.size());
				for (const std::unique_ptr<FExpr>& Argument : Call.Arguments)
				{
					ArgTypes.push_back(InferExprType(*Argument));
				}
				return InferGenericTypeArgsFromTypes(
					Generic,
					Call.TypeArgs,
					ArgTypes,
					Call.Callee,
					OutTypeArgs,
					bReportErrors);
			}

			const FFunctionDecl* ResolveGenericOverloadFromTypes(
				const std::string& Name,
				const std::vector<std::string>& ExplicitTypeArgs,
				const std::vector<std::string>& ArgTypes,
				std::vector<std::string>& OutTypeArgs,
				const FSourceLocation* const ReferenceLocation = nullptr)
			{
				const std::vector<std::string> LookupNames =
					BuildFreeFunctionLookupNames(Name, ReferenceLocation);
				const FFunctionDecl* Match = nullptr;
				std::vector<std::string> MatchArgs;
				for (const FFunctionDecl* Function : GenericFunctions)
				{
					if (std::find(LookupNames.begin(), LookupNames.end(), Function->Name)
						== LookupNames.end())
					{
						continue;
					}
					if (!IsVisibleFromActiveModule(Function->ModulePath, Function->bExported))
					{
						continue;
					}
					std::vector<std::string> TypeArgs;
					if (!InferGenericTypeArgsFromTypes(
							*Function,
							ExplicitTypeArgs,
							ArgTypes,
							Name,
							TypeArgs,
							false))
					{
						continue;
					}
					if (Match)
					{
						OutTypeArgs.clear();
						return nullptr;
					}
					Match = Function;
					MatchArgs = std::move(TypeArgs);
				}
				if (Match)
				{
					OutTypeArgs = std::move(MatchArgs);
				}
				return Match;
			}

			const FFunctionDecl* ResolveGenericOverload(
				const std::string& Name,
				const FCallExpr& Call,
				std::vector<std::string>& OutTypeArgs)
			{
				std::vector<std::string> ArgTypes;
				ArgTypes.reserve(Call.Arguments.size());
				for (const std::unique_ptr<FExpr>& Argument : Call.Arguments)
				{
					ArgTypes.push_back(InferExprType(*Argument));
				}
				return ResolveGenericOverloadFromTypes(
					Name,
					Call.TypeArgs,
					ArgTypes,
					OutTypeArgs,
					&Call.Location);
			}

			const FFunctionDecl* ResolveExtensionOverload(
				const FMethodCallExpr& Call,
				const std::string& ObjectType,
				std::vector<std::string>& OutTypeArgs,
				bool& OutGeneric)
			{
				OutTypeArgs.clear();
				OutGeneric = false;
				std::vector<std::string> ArgTypes;
				ArgTypes.reserve(Call.Arguments.size() + 1);
				ArgTypes.push_back(ObjectType);
				for (const std::unique_ptr<FExpr>& Argument : Call.Arguments)
				{
					ArgTypes.push_back(InferExprType(*Argument));
				}
				const std::vector<std::string> LookupNames =
					BuildFreeFunctionLookupNames(Call.Method, &Call.Location);
				if (Call.TypeArgs.empty())
				{
					const bool bReceiverCanBindRef = Call.Object
						&& CanBindAotImplicitRefArgument(*Call.Object);
					const bool bConstReceiver = Call.Object
						&& IsConstExtensionReceiver(*Call.Object);
					struct FBestMatch
					{
						void Add(const FFunctionDecl* Candidate, const int CandidateScore)
						{
							if (CandidateScore > Score)
							{
								Function = Candidate;
								Score = CandidateScore;
								bAmbiguous = false;
							}
							else if (CandidateScore == Score)
							{
								bAmbiguous = true;
							}
						}

						const FFunctionDecl* Function = nullptr;
						int Score = -1;
						bool bAmbiguous = false;

					};
					FBestMatch RefMatch;
					FBestMatch ValueMatch;
					for (const FFunctionDecl* Function : ConcreteFunctions)
					{
						if (std::find(LookupNames.begin(), LookupNames.end(), Function->Name)
								== LookupNames.end()
							|| Function->Parameters.empty()
							|| !IsVisibleFromActiveModule(
								Function->ModulePath, Function->bExported))
						{
							continue;
						}
						const bool bVariadic = Function->Parameters.back().bIsVariadic;
						const std::size_t FixedCount = Function->Parameters.size()
							- (bVariadic ? 1u : 0u);
						std::size_t RequiredCount = FixedCount;
						while (RequiredCount > 0
							&& Function->Parameters[RequiredCount - 1].DefaultValue)
						{
							--RequiredCount;
						}
						if (ArgTypes.size() < RequiredCount
							|| (!bVariadic && ArgTypes.size() > FixedCount))
						{
							continue;
						}
						int Score = 0;
						bool bCompatible = true;
						for (std::size_t Index = 0; Index < ArgTypes.size(); ++Index)
						{
							const std::size_t ParamIndex = Index < FixedCount
								? Index
								: Function->Parameters.size() - 1;
							const FFunctionParam& Param = Function->Parameters[ParamIndex];
							bool bArgumentIsRef = false;
							const FExpr* RefTarget = nullptr;
							if (Index == 0)
							{
								bArgumentIsRef = Param.bIsRef && bReceiverCanBindRef;
								RefTarget = Call.Object.get();
							}
							else if (Index - 1 < Call.Arguments.size())
							{
								const FExpr* Argument = Call.Arguments[Index - 1].get();
								bArgumentIsRef = Argument->Kind == EExprKind::Ref;
								RefTarget = bArgumentIsRef
									? static_cast<const FRefExpr&>(*Argument).Operand.get()
									: Argument;
							}
							if (Param.bIsRef != bArgumentIsRef
								|| (bArgumentIsRef && !Param.bIsConst && RefTarget
									&& IsConstExtensionReceiver(*RefTarget)))
							{
								bCompatible = false;
								break;
							}
							const int Contribution = ScoreAotValueConversion(
								Param.TypeName, ArgTypes[Index]);
							if (Contribution < 0 || (Param.bIsRef && Contribution < 16))
							{
								bCompatible = false;
								break;
							}
							Score += Contribution;
						}
						if (!bCompatible
							|| (Function->Parameters.front().bIsRef && bConstReceiver
								&& !Function->Parameters.front().bIsConst))
						{
							continue;
						}
						auto& Match = Function->Parameters.front().bIsRef
							? RefMatch
							: ValueMatch;
						Match.Add(Function, Score);
					}
					if (RefMatch.Function && !RefMatch.bAmbiguous)
					{
						return RefMatch.Function;
					}
					if (!RefMatch.Function && ValueMatch.Function && !ValueMatch.bAmbiguous)
					{
						return ValueMatch.Function;
					}
				}
				const FFunctionDecl* Generic = ResolveGenericOverloadFromTypes(
					Call.Method,
					Call.TypeArgs,
					ArgTypes,
					OutTypeArgs,
					&Call.Location);
				OutGeneric = Generic != nullptr;
				return Generic;
			}

			const FFunctionDecl* FindGenericByName(
				const std::string& Name,
				const FSourceLocation* const ReferenceLocation = nullptr) const
			{
				const std::vector<std::string> LookupNames =
					BuildFreeFunctionLookupNames(Name, ReferenceLocation);
				const FFunctionDecl* Match = nullptr;
				for (const FFunctionDecl* Function : GenericFunctions)
				{
					if (std::find(LookupNames.begin(), LookupNames.end(), Function->Name)
						== LookupNames.end())
					{
						continue;
					}
					if (!IsVisibleFromActiveModule(Function->ModulePath, Function->bExported))
					{
						continue;
					}
					if (Match)
					{
						return nullptr;
					}
					Match = Function;
				}
				return Match;
			}

			std::string SubstForUserType(
				const FUserTypeInfo& Type,
				const std::string& RawType) const
			{
				if (Type.TypeParams.empty() || Type.MonoTypeArgs.empty())
				{
					return ApplyTypeSubst(RawType);
				}
				std::unordered_map<std::string, std::string> Bindings;
				for (std::size_t Index = 0; Index < Type.TypeParams.size()
					&& Index < Type.MonoTypeArgs.size();
					++Index)
				{
					Bindings.emplace(Type.TypeParams[Index], Type.MonoTypeArgs[Index]);
				}
				return SubstituteWithMap(ApplyTypeSubst(RawType), Bindings);
			}

			const FMethodDecl* ResolveMethod(
				const FUserTypeInfo& Type,
				const std::string& MethodName,
				const std::vector<std::string>& ArgTypes,
				const FUserTypeInfo** OutDeclaringType = nullptr) const
			{
				if (OutDeclaringType)
				{
					*OutDeclaringType = nullptr;
				}
				const FMethodDecl* Match = nullptr;
				int BestScore = -1;
				bool bAmbiguous = false;
				for (const FMethodDecl& Method : *Type.Methods)
				{
					if (Method.Name != MethodName)
					{
						continue;
					}
					const bool bVariadic = !Method.Parameters.empty()
						&& Method.Parameters.back().bIsVariadic;
					const std::size_t FixedCount = Method.Parameters.size() - (bVariadic ? 1u : 0u);
					std::size_t RequiredCount = FixedCount;
					while (RequiredCount > 0
						&& Method.Parameters[RequiredCount - 1].DefaultValue)
					{
						--RequiredCount;
					}
					if (ArgTypes.size() < RequiredCount
						|| (!bVariadic && ArgTypes.size() > FixedCount))
					{
						continue;
					}
					int Score = 0;
					bool bOk = true;
					for (std::size_t Index = 0; Index < ArgTypes.size(); ++Index)
					{
						const std::size_t ParamIndex = Index < FixedCount
							? Index
							: Method.Parameters.size() - 1;
						const FFunctionParam& Param = Method.Parameters[ParamIndex];
						const int Contribution = ScoreAotValueConversion(
							SubstForUserType(Type, Param.TypeName), ArgTypes[Index]);
						if (Contribution < 0 || (Param.bIsRef && Contribution < 16))
						{
							bOk = false;
							break;
						}
						Score += Contribution;
					}
					if (!bOk)
					{
						continue;
					}
					if (Score > BestScore)
					{
						BestScore = Score;
						Match = &Method;
						bAmbiguous = false;
					}
					else if (Score == BestScore)
					{
						bAmbiguous = true;
					}
				}
				if (Match || bAmbiguous)
				{
					if (Match && !bAmbiguous && OutDeclaringType)
					{
						*OutDeclaringType = &Type;
					}
					return bAmbiguous ? nullptr : Match;
				}
				// Generic lookup may lazily materialize a monomorph and rehash UserTypes;
				// do not retain references to Type's vectors across that operation.
				const std::vector<std::string> BaseTypes = Type.BaseTypeNames;
				const std::vector<std::string> InterfaceTypes = Type.InterfaceTypeNames;
				for (const std::string& Base : BaseTypes)
				{
					const FUserTypeInfo* const BaseType = ResolveUserTypeForLookup(Base);
					if (!BaseType)
					{
						continue;
					}
					if (const FMethodDecl* BaseMethod =
						ResolveMethod(*BaseType, MethodName, ArgTypes, OutDeclaringType))
					{
						return BaseMethod;
					}
				}
				for (const std::string& Iface : InterfaceTypes)
				{
					const FUserTypeInfo* const IfaceType = ResolveUserTypeForLookup(Iface);
					if (!IfaceType)
					{
						continue;
					}
					if (const FMethodDecl* IfaceMethod =
						ResolveMethod(*IfaceType, MethodName, ArgTypes, OutDeclaringType))
					{
						return IfaceMethod;
					}
				}
				return nullptr;
			}

			const FConstructorDecl* ResolveCtor(
				const FUserTypeInfo& Type,
				const std::vector<std::string>& ArgTypes,
				const bool bAllowExplicit = true,
				const bool bAllowParamUserDefinedConversions = true) const
			{
				if (Type.Constructors->empty() && ArgTypes.empty())
				{
					return nullptr; // synthesized default
				}
				const FConstructorDecl* Match = nullptr;
				int BestScore = -1;
				bool bAmbiguous = false;
				for (const FConstructorDecl& Ctor : *Type.Constructors)
				{
					if (Ctor.bExplicit && !bAllowExplicit)
					{
						continue;
					}
					const bool bVariadic = !Ctor.Parameters.empty()
						&& Ctor.Parameters.back().bIsVariadic;
					const std::size_t FixedCount = Ctor.Parameters.size() - (bVariadic ? 1u : 0u);
					std::size_t RequiredCount = FixedCount;
					while (RequiredCount > 0
						&& Ctor.Parameters[RequiredCount - 1].DefaultValue)
					{
						--RequiredCount;
					}
					if (ArgTypes.size() < RequiredCount
						|| (!bVariadic && ArgTypes.size() > FixedCount))
					{
						continue;
					}
					int Score = 0;
					bool bOk = true;
					for (std::size_t Index = 0; Index < ArgTypes.size(); ++Index)
					{
						const std::size_t ParamIndex = Index < FixedCount
							? Index
							: Ctor.Parameters.size() - 1;
						const FFunctionParam& Param = Ctor.Parameters[ParamIndex];
						const int Contribution = ScoreAotValueConversion(
							SubstForUserType(Type, Param.TypeName),
							ArgTypes[Index],
							bAllowParamUserDefinedConversions);
						if (Contribution < 0 || (Param.bIsRef && Contribution < 16))
						{
							bOk = false;
							break;
						}
						Score += Contribution;
					}
					if (!bOk)
					{
						continue;
					}
					if (Score > BestScore)
					{
						BestScore = Score;
						Match = &Ctor;
						bAmbiguous = false;
					}
					else if (Score == BestScore)
					{
						bAmbiguous = true;
					}
				}
				return bAmbiguous ? nullptr : Match;
			}

			std::string FieldType(
				const FUserTypeInfo& Type,
				const std::string& Member,
				const int Depth = 0) const
			{
				if (Depth > 64)
				{
					return {};
				}
				if (Type.Fields)
				{
					for (const FFieldDecl& Field : *Type.Fields)
					{
						if (Field.Name != Member)
						{
							continue;
						}
						std::string Result = Field.TypeName;
						if (!Type.TypeParams.empty()
							&& Type.TypeParams.size() == Type.MonoTypeArgs.size())
						{
							std::unordered_map<std::string, std::string> Bindings;
							for (std::size_t Index = 0; Index < Type.TypeParams.size(); ++Index)
							{
								Bindings.emplace(Type.TypeParams[Index], Type.MonoTypeArgs[Index]);
							}
							Result = SubstituteWithMap(Result, Bindings);
						}
						return Result;
					}
				}
				// Inherited lookup may lazily register a generic monomorph and rehash
				// UserTypes; copy the names before Type can be invalidated.
				const std::vector<std::string> BaseTypes = Type.BaseTypeNames;
				const std::vector<std::string> InterfaceTypes = Type.InterfaceTypeNames;
				const auto FindInherited = [&](const std::string& RawTypeName) -> std::string
				{
					const FUserTypeInfo* const BaseType =
						ResolveUserTypeForLookup(RawTypeName);
					return !BaseType
						? std::string{}
						: FieldType(*BaseType, Member, Depth + 1);
				};
				for (const std::string& Base : BaseTypes)
				{
					if (const std::string Found = FindInherited(Base); !Found.empty())
					{
						return Found;
					}
				}
				for (const std::string& Interface : InterfaceTypes)
				{
					if (const std::string Found = FindInherited(Interface); !Found.empty())
					{
						return Found;
					}
				}
				return {};
			}

			static const FFieldDecl* FindFieldInList(
				const std::vector<FFieldDecl>* Fields,
				const std::string& Member)
			{
				if (!Fields)
				{
					return nullptr;
				}
				for (const FFieldDecl& Field : *Fields)
				{
					if (Field.Name == Member)
					{
						return &Field;
					}
				}
				return nullptr;
			}

			/**
			 * Resolve a static member function overload by name and arity from a method
			 * list (open generic or concrete). Prefers an exact arity match, else the
			 * first same-named candidate.
			 */
			static const FMethodDecl* ResolveStaticMethodInList(
				const std::vector<FMethodDecl>* Methods,
				const std::string& Name,
				const std::size_t ArgCount)
			{
				if (!Methods)
				{
					return nullptr;
				}
				const FMethodDecl* Fallback = nullptr;
				for (const FMethodDecl& Method : *Methods)
				{
					if (Method.Name != Name)
					{
						continue;
					}
					if (Method.Parameters.size() == ArgCount)
					{
						return &Method;
					}
					if (!Fallback)
					{
						Fallback = &Method;
					}
				}
				return Fallback;
			}

			/**
			 * True when `Expr` is a bare type-name receiver for a C++-style static member
			 * (`Counter`, `Box<int>`) rather than a value. Fills the C++ `::`-qualifier
			 * (`vs_Counter`, `vs_Box<int>`) and the owning declaration's field/method
			 * lists (from the open template for type applications).
			 */
			/**
			 * Fold a receiver expression into a (possibly dotted / type-applied) type
			 * name: an `Identifier` (`Box`, `Box<int>`, or a parser-folded `Ns.Box`),
			 * or a `MemberAccess` chain of plain identifiers (`Ns.Box`, `A.B.C`). Calls
			 * or any non-identifier leaf make it not a type name.
			 */
