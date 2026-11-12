// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

			std::string GeneratedLocalName(std::string Name) const
			{
				const auto Cached = GeneratedLocalNameCache.find(Name);
				if (Cached != GeneratedLocalNameCache.end()) return Cached->second;
				const std::string BaseName = Name;
				while (GeneratedLocalConflicts.count(Name))
				{
					Name += '_';
				}
				GeneratedLocalNameCache.emplace(BaseName, Name);
				return Name;
			}

			void SelectGeneratedLocalNames()
			{
				// Keep the customary spellings unless a user declaration can shadow them.
				// A single whole-program scan also covers constructor defaults, generic
				// bodies, nested lambdas and generated declarations without renaming them.
				std::unordered_set<std::string> Names;
				const auto AddName = [&](const std::string& Name)
				{
					if ((Name.rfind("self", 0) == 0 && Name.find_first_not_of('_', 4) == std::string::npos) || (Name.rfind("vs_this", 0) == 0 && Name.find_first_not_of('_', 7) == std::string::npos) || Name.rfind("___vs_", 0) == 0)
					{
						Names.insert(Name);
					}
				};
				const auto AddParameters = [&](const auto& Parameters)
				{
					for (const auto& Parameter : Parameters)
					{
						AddName(Parameter.Name);
					}
				};
				const auto AddType = [&](const auto& Type)
				{
					for (const auto& Name : Type.TypeParams)
					{
						AddName(Name);
					}
					if (Type.Methods)
					{
						for (const auto& Method : *Type.Methods)
						{
							AddParameters(Method.Parameters);
							for (const auto& Name : Method.TypeParams)
							{
								AddName(Name);
							}
						}
					}
					if (Type.Constructors)
					{
						for (const auto& Constructor : *Type.Constructors)
						{
							AddParameters(Constructor.Parameters);
						}
					}
				};
				for (const auto& Entry : UserTypes)
				{
					AddType(Entry.second);
				}
				for (const auto& Entry : OpenGenericTypes)
				{
					AddType(Entry.second);
				}
				for (const auto& Alias : Program->TypeAliases)
				{
					AddName(Alias.Name);
				}
				for (const auto& Function : Program->Functions)
				{
					AddParameters(Function.Parameters);
					for (const auto& Name : Function.TypeParams)
					{
						AddName(Name);
					}
				}
				VisitProgramNodes(*Program, [&](const FExpr& Expression)
				{
					if (Expression.Kind == EExprKind::Identifier)
					{
						AddName(static_cast<const FIdentifierExpr&>(Expression).Name);
					}
					else if (Expression.Kind == EExprKind::Lambda)
					{
						const auto& Lambda = static_cast<const FLambdaExpr&>(Expression);
						AddParameters(Lambda.Parameters);
						for (const auto& Name : Lambda.GetCaptureNames(false))
						{
							AddName(Name);
						}
						for (const auto& Name : Lambda.GetCaptureNames(true))
						{
							AddName(Name);
						}
					}
				}, [&](const FStmt& Statement)
				{
					switch (Statement.Kind)
					{
					case EStmtKind::VarDecl:
						{
							AddName(static_cast<const FVarDeclStmt&>(Statement).Name);
							break;
						}
					case EStmtKind::TypeAlias:
						{
							AddName(static_cast<const FTypeAliasStmt&>(Statement).Name);
							break;
						}
					case EStmtKind::ForEach:
						{
							const auto& Loop = static_cast<const FForEachStmt&>(Statement);
							AddName(Loop.ElementName);
							AddName(Loop.KeyName);
							break;
						}
					case EStmtKind::Try:
						{
							for (const auto& Clause : static_cast<const FTryStmt&>(Statement).Catches)
							{
								AddName(Clause.Name);
							}
							break;
						}
					default:
						{
							break;
						}
					}
				});
				GeneratedLocalConflicts = std::move(Names);
				GeneratedLocalNameCache.clear();
				SelfReceiverName = GeneratedLocalName("self");
				CapturedThisName = GeneratedLocalName("vs_this");
			}

			void IndexProgram()
			{
				static const std::vector<FFieldDecl> kEmptyFields;
				static const std::vector<FConstructorDecl> kEmptyCtors;
				for (const FTypeAliasDecl& Alias : Program->TypeAliases)
				{
					if (Alias.Scope.bLocal)
					{
						continue;
					}
					if (TypeAliases.find(Alias.Name) != TypeAliases.end())
					{
						Fail(
						    "AOT: type alias '" + Alias.Name + "' appears more than once across modules; rename one");
						return;
					}
					TypeAliases[Alias.Name] = Alias.TypeName;
					RootTypeAliases[Alias.Name] = Alias.TypeName;
				}
				for (const FEnumDecl& Decl : Program->Enums)
				{
					if (EnumTypeNames.find(Decl.Name) != EnumTypeNames.end() || UserTypes.find(Decl.Name) != UserTypes.end() || TypeAliases.find(Decl.Name) != TypeAliases.end())
					{
						Fail(
						    "AOT: type name '" + Decl.Name + "' appears more than once across modules; rename one");
						return;
					}
					EnumTypeNames.insert(Decl.Name);
					EnumUnderlyingTypeNames.emplace(Decl.Name, Decl.UnderlyingTypeName);
					if (Decl.bScoped)
					{
						ScopedEnumTypeNames.insert(Decl.Name);
					}
					for (const FEnumMemberDecl& Member : Decl.Members)
					{
						const std::string Key = Decl.Name + '.' + Member.Name;
						if (EnumMemberValues.find(Key) != EnumMemberValues.end())
						{
							Fail("AOT: duplicate enum member '" + Key + "'");
							return;
						}
						EnumMemberValues[Key] = Member.Value;
						if (Decl.bScoped)
						{
							continue;
						}
						const std::string CppName = EnumMemberCppName(Decl.Name, Member.Name);
						const auto BareIt = EnumBareMemberCppNames.find(Member.Name);
						if (BareIt == EnumBareMemberCppNames.end())
						{
							EnumBareMemberCppNames.emplace(Member.Name, CppName);
							EnumBareMemberTypeNames.emplace(
							    Member.Name, Decl.UnderlyingTypeName);
						}
						else if (!BareIt->second.empty() && BareIt->second != CppName)
						{
							// Ambiguous bare name across enums; force Enum.Member form.
							BareIt->second.clear();
							EnumBareMemberTypeNames[Member.Name].clear();
						}
					}
				}
				auto RegisterNameConflict = [&](const std::string& Name) -> bool
				{
					if (UserTypes.find(Name) != UserTypes.end() || OpenGenericTypes.find(Name) != OpenGenericTypes.end() || EnumTypeNames.find(Name) != EnumTypeNames.end() || TypeAliases.find(Name) != TypeAliases.end())
					{
						Fail(
						    "AOT: type name '" + Name + "' appears more than once across modules; rename one");
						return false;
					}
					return true;
				};

				for (const FInterfaceDecl& Decl : Program->Interfaces)
				{
					if (!RegisterNameConflict(Decl.Name))
					{
						return;
					}
					if (!Decl.TypeParams.empty())
					{
						FOpenGenericType Open;
						Open.Name = Decl.Name;
						Open.ModulePath = Decl.ModulePath;
						Open.bExported = Decl.bExported;
						Open.bFinal = Decl.bFinal;
						Open.bIsAbstract = IsCompilerAbstractType(
						    Decl.Name, Decl.ModulePath, Decl.bExported);
						Open.bIsInterface = true;
						Open.TypeParams = Decl.TypeParams;
						Open.TypeParamDefaults = Decl.TypeParamDefaults;
						Open.Bounds = Decl.Bounds;
						for (const std::string& BaseName : Decl.BaseInterfaceNames)
						{
							if (!BaseName.empty())
							{
								Open.BaseTypeNames.push_back(BaseName);
							}
						}
						Open.Fields = &Decl.Fields;
						Open.Methods = &Decl.Methods;
						Open.Constructors = &kEmptyCtors;
						OpenGenericTypes.emplace(Decl.Name, Open);
						continue;
					}
					FUserTypeInfo Info;
					Info.Name = Decl.Name;
					Info.ModulePath = Decl.ModulePath;
					Info.bExported = Decl.bExported;
					Info.bFinal = Decl.bFinal;
					Info.bIsAbstract = IsCompilerAbstractType(
					    Decl.Name, Decl.ModulePath, Decl.bExported);
					Info.bIsInterface = true;
					Info.Fields = &Decl.Fields;
					Info.Methods = &Decl.Methods;
					Info.Constructors = &kEmptyCtors;
					for (const std::string& BaseName : Decl.BaseInterfaceNames)
					{
						if (!BaseName.empty())
						{
							Info.BaseTypeNames.push_back(BaseName);
						}
					}
					UserTypes.emplace(Decl.Name, Info);
					TypeEmitOrder.push_back(Decl.Name);
				}
				for (const FStructDecl& Decl : Program->Structs)
				{
					if (AttributeModule(Decl.ModulePath))
					{
						bHasStandardAttribute = true;
						bNeedsMemory = bNeedsFunctional = bNeedsUtility = true;
						continue;
					}
					if (!RegisterNameConflict(Decl.Name))
					{
						return;
					}
					if (!Decl.TypeParams.empty())
					{
						FOpenGenericType Open;
						Open.Name = Decl.Name;
						Open.ModulePath = Decl.ModulePath;
						Open.bExported = Decl.bExported;
						Open.bFinal = Decl.bFinal;
						Open.bIsAbstract = IsCompilerAbstractType(
						    Decl.Name, Decl.ModulePath, Decl.bExported);
						Open.bIsClass = false;
						Open.bHasPureVirtualDestructor =
						    Decl.Destructor && Decl.Destructor->bPureVirtual;
						Open.TypeParams = Decl.TypeParams;
						Open.TypeParamDefaults = Decl.TypeParamDefaults;
						Open.Bounds = Decl.Bounds;
						if (!Decl.BaseTypeName.empty())
						{
							Open.BaseTypeNames.push_back(Decl.BaseTypeName);
						}
						Open.InterfaceTypeNames = Decl.InterfaceTypeNames;
						Open.Fields = &Decl.Fields;
						Open.Methods = &Decl.Methods;
						Open.Constructors = &Decl.Constructors;
						Open.Destructor = Decl.Destructor.get();
						OpenGenericTypes.emplace(Decl.Name, Open);
						continue;
					}
					FUserTypeInfo Info;
					Info.Name = Decl.Name;
					Info.ModulePath = Decl.ModulePath;
					Info.bExported = Decl.bExported;
					Info.bFinal = Decl.bFinal;
					Info.bIsAbstract = IsCompilerAbstractType(
					    Decl.Name, Decl.ModulePath, Decl.bExported);
					Info.Fields = &Decl.Fields;
					Info.Methods = &Decl.Methods;
					Info.Constructors = &Decl.Constructors;
					Info.Destructor = Decl.Destructor.get();
					Info.bIsClass = false;
					Info.bHasPureVirtualDestructor =
					    Decl.Destructor && Decl.Destructor->bPureVirtual;
					if (!Decl.BaseTypeName.empty())
					{
						Info.BaseTypeNames.push_back(Decl.BaseTypeName);
					}
					Info.InterfaceTypeNames = Decl.InterfaceTypeNames;
					UserTypes.emplace(Decl.Name, Info);
					TypeEmitOrder.push_back(Decl.Name);
				}
				for (const FClassDecl& Decl : Program->Classes)
				{
					// Soft builtins (delegate.vs): hard-coded C++ templates in MakeRuntimeSupport,
					// resolved directly by MapType from "delegate<...>" / "multicast_delegate(<...>)".
					if (Decl.Name == "delegate" || Decl.Name == "multicast_delegate")
					{
						continue;
					}
					if (!RegisterNameConflict(Decl.Name))
					{
						return;
					}
					if (!Decl.TypeParams.empty())
					{
						FOpenGenericType Open;
						Open.Name = Decl.Name;
						Open.ModulePath = Decl.ModulePath;
						Open.bExported = Decl.bExported;
						Open.bFinal = Decl.bFinal;
						Open.bIsAbstract = IsCompilerAbstractType(
						    Decl.Name, Decl.ModulePath, Decl.bExported);
						Open.bIsClass = true;
						Open.bHasPureVirtualDestructor =
						    Decl.Destructor && Decl.Destructor->bPureVirtual;
						Open.TypeParams = Decl.TypeParams;
						Open.TypeParamDefaults = Decl.TypeParamDefaults;
						Open.Bounds = Decl.Bounds;
						Open.BaseTypeNames = Decl.BaseTypeNames;
						Open.InterfaceTypeNames = Decl.InterfaceTypeNames;
						Open.Fields = &Decl.Fields;
						Open.Methods = &Decl.Methods;
						Open.Constructors = &Decl.Constructors;
						Open.Destructor = Decl.Destructor.get();
						OpenGenericTypes.emplace(Decl.Name, Open);
						continue;
					}
					FUserTypeInfo Info;
					Info.Name = Decl.Name;
					Info.ModulePath = Decl.ModulePath;
					Info.bExported = Decl.bExported;
					Info.bFinal = Decl.bFinal;
					Info.bIsAbstract = IsCompilerAbstractType(
					    Decl.Name, Decl.ModulePath, Decl.bExported);
					Info.Fields = &Decl.Fields;
					Info.Methods = &Decl.Methods;
					Info.Constructors = &Decl.Constructors;
					Info.Destructor = Decl.Destructor.get();
					Info.bIsClass = true;
					Info.bHasPureVirtualDestructor =
					    Decl.Destructor && Decl.Destructor->bPureVirtual;
					Info.BaseTypeNames = Decl.BaseTypeNames;
					Info.InterfaceTypeNames = Decl.InterfaceTypeNames;
					UserTypes.emplace(Decl.Name, Info);
					TypeEmitOrder.push_back(Decl.Name);
				}
				for (const FFunctionDecl& Function : Program->Functions)
				{
					if (AttributeModule(Function.ModulePath) ||
					    (Function.Name == "__std_bind_method" && !Function.Body))
					{
						continue;
					}

					if (IsEntryMain(Function))
					{
						bMainReturnsInt = ResolveAliasChain(Function.ReturnType) == "int";
						if (!Function.Parameters.empty())
						{
							MainArgsParamName = Function.Parameters[0].Name;
						}
					}
					if (!Function.TypeParams.empty() || !Function.Bounds.empty())
					{
						GenericFunctions.push_back(&Function);
					}
					else
					{
						ConcreteFunctions.push_back(&Function);
					}
				}
			}

			void RefreshEffectiveAbstractTypes()
			{
				// Build semantic-only `Open<T...>` entries so inherited pure slots can
				// be evaluated for the open template itself. This prevents emitting a
				// public `_new<T>` helper for an abstract generic derived type.
				std::unordered_map<std::string, std::string> OpenSemanticTypes;
				const std::unordered_set<std::string> SavedOpenParams = ActiveOpenTypeParams;
				for (const auto& Entry : OpenGenericTypes)
				{
					ActiveOpenTypeParams.clear();
					for (const std::string& Param : Entry.second.TypeParams)
					{
						ActiveOpenTypeParams.insert(Param);
					}
					const std::string SemanticName =
					    EnsureTypeMonomorph(Entry.first, Entry.second.TypeParams);
					if (!SemanticName.empty())
					{
						OpenSemanticTypes.emplace(Entry.first, SemanticName);
					}
				}
				ActiveOpenTypeParams = SavedOpenParams;

				struct FSlot
				{
					std::string Root;
					std::string Signature;
					bool bPure = false;
				};
				std::unordered_map<std::string, std::vector<FSlot>> Cache;
				std::unordered_set<std::string> Active;
				std::function<std::vector<FSlot>(const std::string&)> Collect;
				Collect = [&](const std::string& TypeName) -> std::vector<FSlot>
				{
					const auto Cached = Cache.find(TypeName);
					if (Cached != Cache.end())
					{
						return Cached->second;
					}
					if (!Active.insert(TypeName).second)
					{
						return {};
					}
					std::vector<FSlot> Slots;
					const auto TypeIt = UserTypes.find(TypeName);
					if (TypeIt != UserTypes.end())
					{
						const FUserTypeInfo& Type = TypeIt->second;
						auto MergeParent = [&](const std::string& ParentName)
						{
							for (FSlot& ParentSlot : Collect(ParentName))
							{
								const auto Duplicate = std::find_if(
								    Slots.begin(), Slots.end(),
								    [&](const FSlot& Existing)
								{
									return Existing.Root == ParentSlot.Root;
								});
								if (Duplicate == Slots.end())
								{
									Slots.push_back(std::move(ParentSlot));
								}
							}
						};
						for (const std::string& Base : Type.BaseTypeNames)
						{
							MergeParent(Base);
						}
						for (const std::string& Interface : Type.InterfaceTypeNames)
						{
							MergeParent(Interface);
						}
						if (Type.Methods)
						{
							for (const FMethodDecl& Method : *Type.Methods)
							{
								if (Method.bIsStatic || Method.bDeleted)
								{
									continue;
								}
								std::string Signature = Method.Name + "#return:" + SubstForUserType(Type, Method.ReturnType);
								Signature += Method.bIsConst ? "#const" : "#mutable";
								for (const FFunctionParam& Param : Method.Parameters)
								{
									Signature += "#param:" + SubstForUserType(Type, Param.TypeName);
									if (Param.bIsConst)
									{
										Signature += "#const";
									}
									if (Param.bIsRef)
									{
										Signature += "#ref";
									}
								}
								bool bMatched = false;
								for (FSlot& Slot : Slots)
								{
									if (Slot.Signature == Signature)
									{
										Slot.bPure = Method.bPureVirtual;
										bMatched = true;
									}
								}
								if (!bMatched && (Method.bVirtual || Method.bOverride))
								{
									Slots.push_back(FSlot{
									    TypeName + "::" + Signature,
									    std::move(Signature),
									    Method.bPureVirtual});
								}
							}
						}
					}
					Active.erase(TypeName);
					Cache.emplace(TypeName, Slots);
					return Slots;
				};

				for (auto& Entry : UserTypes)
				{
					FUserTypeInfo& Type = Entry.second;
					if (Type.bIsInterface)
					{
						continue;
					}
					const std::vector<FSlot> Slots = Collect(Entry.first);
					// Keep the compiler's authoritative result.  The local AST pass below
					// supplements it for AOT specializations, but cannot necessarily see
					// abstract slots inherited from a linked/package-provided base type.
					Type.bIsAbstract = Type.bIsAbstract || Type.bHasPureVirtualDestructor || std::any_of(Slots.begin(), Slots.end(), [](const FSlot& Slot)
					{
						return Slot.bPure;
					});
				}
				for (auto& Entry : OpenGenericTypes)
				{
					FOpenGenericType& Type = Entry.second;
					Type.bIsAbstract = Type.bIsAbstract || Type.bHasPureVirtualDestructor;
					const auto Semantic = OpenSemanticTypes.find(Entry.first);
					if (Semantic != OpenSemanticTypes.end())
					{
						const auto Concrete = UserTypes.find(Semantic->second);
						if (Concrete != UserTypes.end())
						{
							Type.bIsAbstract = Type.bIsAbstract || Concrete->second.bIsAbstract;
						}
					}
					if (Type.Methods)
					{
						Type.bIsAbstract = Type.bIsAbstract || std::any_of(Type.Methods->begin(), Type.Methods->end(), [](const FMethodDecl& Method)
						{
							return Method.bPureVirtual && !Method.bDeleted;
						});
					}
				}
			}

			void EmitEnumsPreamble()
			{
				for (const FEnumDecl& Decl : Program->Enums)
				{
					if (!Decl.bScoped)
					{
						continue;
					}
					const std::string Underlying = MapType(Decl.UnderlyingTypeName);
					Out << "enum class " << UserCppType(Decl.Name) << " : "
					    << Underlying << " {\n";
					bool bFirst = true;
					for (const FEnumMemberDecl& Member : Decl.Members)
					{
						if (!bFirst)
						{
							Out << ",\n";
						}
						bFirst = false;
						Out << "\t" << SanitizeForMangle(Member.Name) << " = ";
						if (Member.Value == std::numeric_limits<std::int64_t>::min())
						{
							Out << "(-9223372036854775807LL - 1)";
						}
						else
						{
							Out << Member.Value;
						}
					}
					Out << "\n};\n"
					    << "static inline std::ostream& operator<<(std::ostream& Stream, "
					    << UserCppType(Decl.Name) << " Value) {\n"
					    << "\treturn Stream << \"" << EscapeCppString(Decl.Name)
					    << "(\" << static_cast<std::int64_t>(Value) << ')';\n"
					    << "}\n"
					    << "static inline void VsPrint(" << UserCppType(Decl.Name)
					    << " Value) { std::cout << Value << '\\n'; }\n\n";
				}
				for (const FEnumDecl& Decl : Program->Enums)
				{
					if (Decl.bScoped || Decl.Members.empty())
					{
						continue;
					}
					Out << "enum " << SanitizeForMangle(Decl.Name)
					    << "__VSEnum : " << MapType(Decl.UnderlyingTypeName)
					    << " {\n";
					bool bFirst = true;
					for (const FEnumMemberDecl& Member : Decl.Members)
					{
						if (!bFirst)
						{
							Out << ",\n";
						}
						bFirst = false;
						Out << "\t" << EnumMemberCppName(Decl.Name, Member.Name)
						    << " = ";
						if (Member.Value == std::numeric_limits<std::int64_t>::min())
						{
							Out << "(-9223372036854775807LL - 1)";
						}
						else
						{
							Out << Member.Value;
						}
					}
					Out << "\n};\n\n";
				}
			}

			void EmitAliasesPreamble()
			{
				for (const FTypeAliasDecl& Alias : Program->TypeAliases)
				{
					if (Alias.Scope.bLocal)
					{
						continue;
					}
					Out << "using " << SanitizeSymbolName(Alias.Name) << " = " << MapType(Alias.TypeName) << ";\n";
				}
				if (!Program->TypeAliases.empty())
				{
					Out << '\n';
				}
			}

			void CollectAllSpecializations()
			{
				std::size_t TypeCount = 0;
				do
				{
					TypeCount = UserTypes.size();
					for (const std::unique_ptr<FStmt>& Statement : Program->TopLevelStatements)
					{
						if (!Statement || Statement->Kind != EStmtKind::VarDecl)
						{
							continue;
						}
						const FVarDeclStmt& Global =
						    static_cast<const FVarDeclStmt&>(*Statement);
						LocalTypes.clear();
						LocalConstness.clear();
						TypeSubst.clear();
						bInSelfContext = false;
						bSelfIsConst = false;
						SelfTypeName.clear();
						ActiveModulePath = GlobalModulePath(Global);
						ActiveNamespace = NamespaceOfSymbol(Global.Name);
						ActiveFunctionName.clear();
						// Materialize generic value types and discover calls/lambdas before
						// any C++ declarations are emitted. Global definitions themselves
						// are deliberately emitted only after type layouts and prototypes.
						(void)MapType(Global.TypeName);
						if (Global.Initializer)
						{
							CollectSpecializationsInExpr(*Global.Initializer);
						}
					}
					for (const FFunctionDecl* Function : ConcreteFunctions)
					{
						LocalTypes.clear();
						LocalConstness.clear();
						TypeSubst.clear();
						bInSelfContext = false;
						bSelfIsConst = false;
						SelfTypeName.clear();
						ActiveModulePath = Function->ModulePath;
						ActiveNamespace = NamespaceOfSymbol(Function->Name);
						ActiveFunctionName = Function->Name;
						for (const FFunctionParam& Param : Function->Parameters)
						{
							LocalTypes[Param.Name] = Param.bIsVariadic
							    ? ("array<" + Param.TypeName + ">")
							    : Param.TypeName;
							LocalConstness[Param.Name] = Param.bIsConst;
						}
						if (Function->Body)
						{
							if (!Function->Body->DefinitionModulePath.empty())
							{
								ActiveModulePath = Function->Body->DefinitionModulePath;
								ActiveNamespace = Function->Body->DefinitionNamespace;
							}
							CollectSpecializationsInStmt(*Function->Body);
						}
					}
					std::vector<std::string> TypeNames;
					TypeNames.reserve(UserTypes.size());
					for (const auto& Entry : UserTypes)
					{
						TypeNames.push_back(Entry.first);
					}
					for (const std::string& TypeName : TypeNames)
					{
						const auto EntryIt = UserTypes.find(TypeName);
						if (EntryIt == UserTypes.end())
						{
							continue;
						}
						const FUserTypeInfo& Type = EntryIt->second;
						if (Type.bResolutionOnlyMonomorph)
						{
							continue;
						}
						for (const FMethodDecl& Method : *Type.Methods)
						{
							LocalTypes.clear();
							LocalConstness.clear();
							PushTypeSubstForUserType(Type);
							bInSelfContext = true;
							bSelfIsConst = Method.bIsConst;
							SelfTypeName = Type.Name;
							ActiveModulePath = Type.ModulePath;
							ActiveNamespace = NamespaceOfSymbol(Type.Name);
							for (const FFunctionParam& Param : Method.Parameters)
							{
								LocalTypes[Param.Name] = Param.TypeName;
								LocalConstness[Param.Name] = Param.bIsConst;
							}
							if (Method.Body)
							{
								if (!Method.Body->DefinitionModulePath.empty())
								{
									ActiveModulePath = Method.Body->DefinitionModulePath;
									ActiveNamespace = Method.Body->DefinitionNamespace;
								}
								CollectSpecializationsInStmt(*Method.Body);
							}
						}
						TypeSubst.clear();
					}
				}
				while (UserTypes.size() > TypeCount);

				for (const auto& Entry : UserTypes)
				{
					const FUserTypeInfo& Type = Entry.second;
					if (Type.bIsInterface || Type.bResolutionOnlyMonomorph)
					{
						continue;
					}
					for (const FMethodDecl& Method : *Type.Methods)
					{
						LocalTypes.clear();
						LocalConstness.clear();
						PushTypeSubstForUserType(Type);
						bInSelfContext = true;
						bSelfIsConst = Method.bIsConst;
						SelfTypeName = Type.Name;
						ActiveModulePath = Type.ModulePath;
						ActiveNamespace = NamespaceOfSymbol(Type.Name);
						for (const FFunctionParam& Param : Method.Parameters)
						{
							LocalTypes[Param.Name] = Param.TypeName;
							LocalConstness[Param.Name] = Param.bIsConst;
						}
						if (Method.Body)
						{
							if (!Method.Body->DefinitionModulePath.empty())
							{
								ActiveModulePath = Method.Body->DefinitionModulePath;
								ActiveNamespace = Method.Body->DefinitionNamespace;
							}
							CollectSpecializationsInStmt(*Method.Body);
						}
						TypeSubst.clear();
					}
					for (const FConstructorDecl& Ctor : *Type.Constructors)
					{
						LocalTypes.clear();
						LocalConstness.clear();
						TypeSubst.clear();
						bInSelfContext = true;
						bSelfIsConst = false;
						SelfTypeName = Type.Name;
						ActiveModulePath = Type.ModulePath;
						ActiveNamespace = NamespaceOfSymbol(Type.Name);
						for (const FFunctionParam& Param : Ctor.Parameters)
						{
							LocalTypes[Param.Name] = Param.TypeName;
							LocalConstness[Param.Name] = Param.bIsConst;
						}
						if (Ctor.Body)
						{
							CollectSpecializationsInStmt(*Ctor.Body);
						}
					}
				}
			}

			void ValidateEmittedCppSignatures()
			{
				std::unordered_set<std::string> Seen;
				auto Consider = [this, &Seen](
				                    const std::string& CppName,
				                    const std::vector<std::string>& ParamScriptTypes)
				{
					std::string Key = CppName;
					Key.push_back('(');
					for (std::size_t Index = 0; Index < ParamScriptTypes.size(); ++Index)
					{
						if (Index > 0)
						{
							Key += ',';
						}
						Key += MapType(ParamScriptTypes[Index]);
					}
					Key.push_back(')');
					if (!Seen.insert(Key).second)
					{
						Fail(
						    "AOT: duplicate C++ overload signature for '" + CppName + "' after specialization; rename or add distinguishing parameters");
					}
				};

				for (const FFunctionDecl* Function : ConcreteFunctions)
				{
					std::vector<std::string> Params;
					for (const FFunctionParam& Param : Function->Parameters)
					{
						Params.push_back(Param.TypeName);
					}
					Consider(EmitFunctionCppName(*Function), Params);
					if (bFailed)
					{
						return;
					}
				}
				std::unordered_set<const FFunctionDecl*> SeenGenericTemplates;
				for (const FSpecialization& Spec : Specializations)
				{
					if (!SeenGenericTemplates.insert(Spec.Generic).second)
					{
						continue;
					}
					ActiveOpenTypeParams.clear();
					for (const std::string& Param : Spec.Generic->TypeParams)
					{
						ActiveOpenTypeParams.insert(Param);
					}
					std::vector<std::string> Params;
					for (const FFunctionParam& Param : Spec.Generic->Parameters)
					{
						Params.push_back(Param.TypeName);
					}
					Consider(EmitFunctionCppName(*Spec.Generic), Params);
					ActiveOpenTypeParams.clear();
					if (bFailed)
					{
						return;
					}
				}
			}
