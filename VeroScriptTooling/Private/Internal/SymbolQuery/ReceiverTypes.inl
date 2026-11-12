// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		std::vector<FSymbolQueryHit> CollectGlobalValueReceiverHits(const FProgram& Program,
		    const FDocumentSymbolResult& Symbols, const FTextPosition Position,
		    const std::string_view Name, const std::string_view ActiveNamespace)
		{
			CheckAnalysisCancellation();
			if (Name.empty()) return {};
			std::string LookupName = NormalizeNamespaceSpelling(Name);
			const bool bGlobal = LookupName.front() == '.';
			if (bGlobal)
			{
				LookupName.erase(0, 1);
			}
			if (LookupName.empty()) return {};
			const auto Separator = LookupName.rfind('.');
			std::vector<FSymbolQueryHit> Hits;
			CollectHits(Symbols.Symbols, Separator == std::string::npos ? LookupName : LookupName.substr(Separator + 1), {}, Hits, &Position, Program.EntryModulePath);
			Hits.erase(std::remove_if(Hits.begin(), Hits.end(), [](const auto& Hit)
			{
				return !Hit.ContainerName.empty() || (Hit.Kind != EDocumentSymbolKind::Variable && Hit.Kind != EDocumentSymbolKind::Constant);
			}),
			    Hits.end());
			if (bGlobal)
			{
				Hits.erase(std::remove_if(Hits.begin(), Hits.end(), [&](const auto& Hit)
				{
					return NormalizeNamespaceSpelling(Hit.Name) != LookupName;
				}),
				    Hits.end());
				return Hits;
			}
			PreferVisibleNamespaceHits(Program, Symbols.Symbols, ActiveNamespace, Position, Name, Hits);
			return Hits;
		}

		bool IsKnownValueReceiver(const FProgram& Program, const FTextPosition Position,
		    const std::string_view Name, const std::vector<FSymbolQueryHit>& Locals,
		    const FDocumentSymbolResult& Symbols, const std::string_view ActiveNamespace)
		{
			const std::string_view Root = Name.substr(0, Name.find('.'));
			if (Root == "this" || Root == "Super")
			{
				return !ResolveImplicitThisOwnerName(Program, Position).empty();
			}
			for (const auto& Local : Locals)
			{
				if (Local.Name == Root) return Local.Kind != EDocumentSymbolKind::TypeAlias;
			}
			const auto Owner = ResolveImplicitThisOwnerName(Program, Position);
			std::vector<FSymbolQueryHit> Fields;
			if (!Owner.empty())
			{
				CollectMemberHitsAlongHierarchy(Program, Symbols.Symbols, Owner, Root, Fields);
			}

			return std::any_of(Fields.begin(), Fields.end(),
			           [](const FSymbolQueryHit& Hit)
			{
				return Hit.Kind == EDocumentSymbolKind::Field;
			}) ||
			    !CollectGlobalValueReceiverHits(Program, Symbols, Position, Root, ActiveNamespace).empty() || (Name != Root && !CollectGlobalValueReceiverHits(Program, Symbols, Position, Name, ActiveNamespace).empty());
		}

		void SetReceiverForwardTypes(VeroScriptParser& Parser, const FProgram& Program,
		    const IHostBindingCatalog* Bindings, const std::vector<FSymbolQueryHit>& Locals)
		{
			std::unordered_set<std::string> TypeNames, InterfaceNames;
			const auto AddTypes = [&TypeNames](const auto& Types)
			{
				for (const auto& Type : Types)
				{
					TypeNames.insert(StripGenericArgs(Type.Name));
				}
			};
			AddTypes(Program.Classes);
			AddTypes(Program.Structs);
			AddTypes(Program.Interfaces);
			AddTypes(Program.Enums);
			AddTypes(Program.TypeAliases);
			for (const auto& Local : Locals)
			{
				if (Local.Kind == EDocumentSymbolKind::TypeAlias)
				{
					TypeNames.insert(Local.Name);
				}
			}
			if (Bindings)
			{
				AddTypes(Bindings->GetTypes());
				AddTypes(Bindings->GetTypeAliases());
				AddTypes(Bindings->GetGenericTypeTemplates());
				std::unordered_set<std::string> HostNames;
				for (const auto& Type : Bindings->GetTypes())
				{
					HostNames.insert(Type.Name);
				}
				for (const auto& Alias : Bindings->GetTypeAliases())
				{
					HostNames.insert(Alias.Name);
				}
				for (const auto& Template : Bindings->GetGenericTypeTemplates())
				{
					HostNames.insert(Template.Name);
				}
				Parser.SetHostTypeNames(std::move(HostNames));
			}
			Parser.SetAdditionalForwardTypeNames(std::move(TypeNames), std::move(InterfaceNames));
		}

		std::string ResolveReceiverTypeApplication(const FProgram& Program, const FTextPosition Position,
		    const std::string_view Expression, const IHostBindingCatalog* Bindings,
		    const std::string_view ActiveNamespace, const std::vector<FSymbolQueryHit>& Locals)
		{
			if (Expression.empty() || Expression.size() > 65536) return {};
			CheckAnalysisCancellation();
			VeroScriptLexer Lexer(Expression, Program.EntryModulePath);
			const auto& Tokens = Lexer.Tokenize();
			if (Lexer.HasError()) return {};
			VeroScriptParser Parser(Tokens, Program.EntryModulePath, &Program);
			SetReceiverForwardTypes(Parser, Program, Bindings, Locals);
			FParserBindingStore FragmentBindings;
			FragmentBindings.Aliases = FragmentBindings.Runtime = {EmptyBindingHead};
			FragmentBindings.AliasOrder = FragmentBindings.RuntimeOrder = {1};
			FragmentBindings.NextOrder = 1;
			for (const auto& Local : Locals)
			{
				if (Local.Kind == EDocumentSymbolKind::TypeAlias)
				{
					FragmentBindings.Add(FragmentBindings.Aliases, Local.Name, Local.Detail);
				}
				else
				{
					FragmentBindings.Add(FragmentBindings.Runtime, Local.Name);
				}
			}
			FDeferredSyntax FragmentContext;
			std::vector<FCallableScope> Callables;
			const auto* Scope = FindEnclosingCallable(Program, Position, Callables);
			if (Scope && Scope->Body && Scope->Body->BodyContext)
			{
				// Reuse declaration identity and template parameters without copying
				// the callable's source text or replacing the current local bindings.
				const auto& Context = *Scope->Body->BodyContext;
				FragmentContext.TypeScope = Context.TypeScope;
				FragmentContext.TypeOwner = Context.TypeOwner;
				FragmentContext.ModulePath = Context.ModulePath;
				FragmentContext.ScopeId = Context.ScopeId;
				FragmentContext.FunctionOwner = Context.FunctionOwner;
				FragmentContext.FunctionTypeParameters = Context.FunctionTypeParameters;
				FragmentContext.CallableTemplateParameters = Context.CallableTemplateParameters;
				FragmentContext.TypeParameters = Context.TypeParameters;
				FragmentContext.ValueParameters = Context.ValueParameters;
			}
			FragmentContext.Bindings = FragmentBindings.Capture();
			Parser.RestoreDeclarationContext(FragmentContext, false);
			const auto TypeName = Parser.ParseTypeFragment(std::string(ActiveNamespace), FragmentContext.TypeParameters);
			if (Parser.HasError() || TypeName.empty()) return {};
			if (Bindings)
			{
				// A concrete catalog type can expose statics without publishing a
				// constructor. Type access must not depend on constructor inference.
				if (const auto* HostType = Bindings->FindType(TypeName)) return HostType->Name;
			}
			// Core resolves concrete arity, defaults and nested type identity. This
			// signature-only query never constructs an object or runs user code.
			FTemplateCallTypeQuery Query(Program, Bindings, Program.EntryModulePath, std::string(ActiveNamespace));
			FCallExpr Constructor(TypeName, {});
			return Query.ResolveExpressionType(Constructor);
		}

		std::string ResolveReceiverExpressionType(const FProgram& Program,
		    const FDocumentSymbolResult& Symbols, const FTextPosition Position,
		    const std::string_view Expression, const IHostBindingCatalog* Bindings,
		    const std::string_view ActiveNamespace, const std::vector<FSymbolQueryHit>* PreLocals)
		{
			if (Expression.empty() || Expression.size() > 65536)
			{
				return {};
			}
			// Incomplete auto declarations can form cycles; bound nested initializer inference.
			static thread_local unsigned InitializerDepth = 0;
			if (InitializerDepth >= 32) return {};
			struct FInitializerGuard
			{
				explicit FInitializerGuard(unsigned& D) : Depth(D)
				{
					++Depth;
				}

				~FInitializerGuard()
				{
					--Depth;
				}

				unsigned& Depth;
			} InitializerGuard(InitializerDepth);
			CheckAnalysisCancellation();
			// Field-initializer lambdas have caller-recovered bindings outside the
			// normal callable walk. Reuse those, and avoid repeating that walk.
			std::vector<FSymbolQueryHit> OwnedLocals;
			if (!PreLocals)
			{
				CollectVisibleLocals(Program, Position, OwnedLocals);
			}
			const auto& Locals = PreLocals ? *PreLocals : OwnedLocals;
			// A trailing template application needs a grammar terminator. The
			// private grouping supplies ')' while keeping complete consumption;
			// it never becomes a source edit or a public coordinate range.
			const std::string Source = "(" + std::string(Expression) + ")";
			VeroScriptLexer Lexer(Source);
			const auto& Tokens = Lexer.Tokenize();
			if (Lexer.HasError())
			{
				return {};
			}
			VeroScriptParser Parser(Tokens, Program.EntryModulePath, &Program);
			SetReceiverForwardTypes(Parser, Program, Bindings, Locals);
			// Preserve local aliases and value shadowing while Core resolves nested
			// type applications against the real program and module identity.
			FParserBindingStore FragmentBindings;
			FragmentBindings.Aliases = FragmentBindings.Runtime = {EmptyBindingHead};
			FragmentBindings.AliasOrder = FragmentBindings.RuntimeOrder = {1};
			FragmentBindings.NextOrder = 1;
			for (const auto& Local : Locals)
			{
				if (Local.Kind == EDocumentSymbolKind::TypeAlias)
				{
					FragmentBindings.Add(FragmentBindings.Aliases, Local.Name, Local.Detail);
				}
				else
				{
					FragmentBindings.Add(FragmentBindings.Runtime, Local.Name);
				}
			}
			FDeferredSyntax FragmentContext;
			FragmentContext.Bindings = FragmentBindings.Capture();
			Parser.RestoreDeclarationContext(FragmentContext, false);
			auto Fragment = Parser.ParseExpressionFragment(std::string(ActiveNamespace), {});
			if (!Fragment || Parser.HasError()) return {};
			FScopeStack Stack(1);
			for (const auto& Local : Locals)
			{
				Stack.front().insert_or_assign(Local.Name, Local);
			}
			const std::string AccessContext = VeroScriptSymbolQuery::ResolveEnclosingTypeName(Program, Position);
			FCallableAccessContext CallableContext;
			const FCallableAccessContext* CallableAccess =
			    VeroScriptSymbolQuery::ResolveEnclosingCallableAccessContext(Program, Position, CallableContext)
			    ? &CallableContext
			    : nullptr;
			// A common return type is safe even if overload selection is incomplete.
			// Different return types remain unknown rather than picking the first.
			const auto CommonType = [](const std::vector<std::string>& Types) -> std::string
			{
				if (Types.empty() || Types.front().empty()) return {};
				return std::all_of(Types.begin(), Types.end(),
				           [&](const std::string& Type)
				{
					return Type == Types.front();
				})
				    ? Types.front()
				    : std::string{};
			};
			const auto CallableReturn = [](const std::string& Type) -> std::string
			{
				std::vector<std::string> Parameters;
				std::string Return;
				return Runtime::ParseFunctionType(Type, Parameters, Return) ? TypeNameFromDetail(Return) : std::string{};
			};
			std::vector<std::pair<std::string, std::string>> LocalAliases;
			bool bLocalAliasesCollected = false;
			const auto CallSiteType = [&](const std::string_view Type) -> std::string
			{
				if (!bLocalAliasesCollected)
				{
					for (const auto& [Name, Local] : Stack.front())
					{
						if (Local.Kind == EDocumentSymbolKind::TypeAlias)
						{
							LocalAliases.emplace_back(Name, Local.Detail);
						}
					}

					bLocalAliasesCollected = true;
				}
				return ResolveDeclarationTypeName(Program, std::string(ActiveNamespace) + ".__receiver",
				    Program.EntryModulePath, Type, LocalAliases, true);
			};
			std::unique_ptr<FTemplateCallTypeQuery> TemplateQuery;
			const auto GetTemplateQuery = [&]() -> FTemplateCallTypeQuery&
			{
				if (!TemplateQuery)
				{
					TemplateQuery = std::make_unique<FTemplateCallTypeQuery>(Program, Bindings,
					    Program.EntryModulePath, std::string(ActiveNamespace));
				}
				return *TemplateQuery;
			};
			const auto FunctionReferenceType = [&](const FExpr& Expression, const std::string& Name) -> std::string
			{
				std::string Lookup = NormalizeNamespaceSpelling(Name);
				const bool bQualified = Lookup.find('.') != std::string::npos;
				if (!Lookup.empty() && Lookup.front() == '.')
				{
					Lookup.erase(0, 1);
				}
				const auto Dot = Lookup.rfind('.');
				std::vector<FSymbolQueryHit> Functions;
				CollectHits(Symbols.Symbols, Lookup.substr(Dot == std::string::npos ? 0 : Dot + 1), {},
				    Functions, &Position, Program.EntryModulePath);
				Functions.erase(std::remove_if(Functions.begin(), Functions.end(), [&](const auto& Hit)
				{
					return Hit.Kind != EDocumentSymbolKind::Function || !Hit.ContainerName.empty() || (bQualified && NormalizeNamespaceSpelling(StripGenericArgs(Hit.Name)) != Lookup);
				}),
				    Functions.end());
				if (!bQualified)
				{
					PreferVisibleNamespaceHits(Program, Symbols.Symbols, ActiveNamespace, Position, Name, Functions);
				}
				if (Functions.empty()) return {};
				const auto Qualified = StripGenericArgs(Functions.front().Name);
				if (std::any_of(Functions.begin(), Functions.end(), [&](const auto& Hit)
				{
					return StripGenericArgs(Hit.Name) != Qualified;
				}))
				{
					return {};
				}
				std::vector<const FFunctionDecl*> Declarations;
				for (const auto& Decl : Program.Functions)
				{
					if (Decl.Name != Qualified)
					{
						continue;
					}
					if (std::any_of(Functions.begin(), Functions.end(), [&](const auto& Hit)
					{
						return PathUtil::Equal(Decl.ModulePath, Hit.FileLabel) || PathUtil::Equal(std::string(Decl.Location.FileLabel), Hit.FileLabel);
					}))
					{
						Declarations.push_back(&Decl);
					}
				}
				if (Declarations.size() == 1 && Declarations.front()->bDeleted) return {};
				if (Expression.Kind == EExprKind::Identifier)
				{
					// Resolve lexical using-directives at the real source position,
					// then give Core the selected name and its original type arguments.
					const auto& Original = static_cast<const FIdentifierExpr&>(Expression);
					FIdentifierExpr Reference(Qualified);
					Reference.Location = Original.Location;
					Reference.TypeArgs = Original.TypeArgs;
					Reference.bExplicitTemplateArguments = Original.bExplicitTemplateArguments;
					Reference.bStandardLibraryQualified = Original.bStandardLibraryQualified;
					return GetTemplateQuery().ResolveExpressionType(Reference);
				}
				// Keep member structure so real value prefixes still hide namespaces.
				return GetTemplateQuery().ResolveExpressionType(Expression);
			};
			std::function<std::string(const FExpr&, unsigned)> Infer;
			Infer = [&](const FExpr& Expr, const unsigned Depth) -> std::string
			{
				CheckAnalysisCancellation();
				if (Depth >= 32) return {};
				if (Expr.Kind <= EExprKind::NullptrLiteral)
				{
					return GetTemplateQuery().ResolveExpressionType(Expr);
				}
				if (Expr.Kind == EExprKind::Ref)
				{
					const auto& Reference = static_cast<const FRefExpr&>(Expr);
					const auto Type = Reference.Operand ? Infer(*Reference.Operand, Depth + 1) : std::string{};
					return Type.empty() ? Type : "ref " + Type;
				}

				if (const std::string MemoryType = InferMemoryInitializerType(Expr, Stack); !MemoryType.empty())
				{
					return MemoryType;
				}
				if (Expr.Kind == EExprKind::Identifier)
				{
					const auto& Name = static_cast<const FIdentifierExpr&>(Expr).Name;
					if (!IsKnownValueReceiver(Program, Position, Name, Locals, Symbols, ActiveNamespace))
					{
						// Core resolves unique function values, including explicit generic
						// applications. Value/field lookup above retains shadowing priority.
						return FunctionReferenceType(Expr, Name);
					}
					if (LookupLocal(Stack, Name) || Name == "Super")
					{
						return ResolveReceiverTypeName(Program, Symbols, Position, Name, &Locals);
					}

					const auto Owner = ResolveImplicitThisOwnerName(Program, Position);
					std::unordered_map<std::string, FInheritedMemberCompletion> Fields;
					AppendCompletionMembersAlongHierarchy(Program, Symbols.Symbols, Owner, Name,
					    Fields, AccessContext, CallableAccess, true);
					std::vector<std::string> Types;
					for (const auto& [Key, Field] : Fields)
					{
						(void)Key;
						if (Field.Label == Name && Field.Kind == EDocumentSymbolKind::Field)
						{
							Types.push_back(TypeNameFromDetail(Field.ValueType));
						}
					}
					if (!Types.empty()) return CommonType(Types);
					// A hidden/inaccessible field still owns the name; it must not
					// fall through to a same-named global value.
					std::vector<FSymbolQueryHit> DeclaredFields;
					if (!Owner.empty())
					{
						CollectMemberHitsAlongHierarchy(Program, Symbols.Symbols, Owner, Name, DeclaredFields);
					}
					if (std::any_of(DeclaredFields.begin(), DeclaredFields.end(), [](const auto& Hit)
					{
						return Hit.Kind == EDocumentSymbolKind::Field;
					}))
					{
						return {};
					}
					const auto Globals = CollectGlobalValueReceiverHits(Program, Symbols, Position, Name, ActiveNamespace);
					// Keep ambiguous using-directive targets unresolved even when
					// their declared types happen to match.
					if (Globals.size() != 1) return {};
					return ResolveDeclarationTypeName(Program, Globals.front().Name, Globals.front().FileLabel,
					    TypeNameFromDetail(Globals.front().Detail));
				}
				if (Expr.Kind == EExprKind::ArrayInit)
				{
					const auto& Initializer = static_cast<const FArrayInitExpr&>(Expr);
					return Initializer.TypeName.empty() ? std::string{} : CallSiteType(Initializer.TypeName);
				}
				if (Expr.Kind == EExprKind::This)
				{
					return ResolveReceiverTypeName(Program, Symbols, Position, "this", &Locals);
				}
				if (Expr.Kind == EExprKind::IndexAccess)
				{
					const auto& Index = static_cast<const FIndexAccessExpr&>(Expr);
					std::string Type;
					return Index.Object && ResolveContainerIndexElementType(Infer(*Index.Object, Depth + 1), Type)
					    ? Type
					    : std::string{};
				}
				if (Expr.Kind == EExprKind::Call)
				{
					const auto& Call = static_cast<const FCallExpr&>(Expr);
					if (Call.CalleeExpr) return CallableReturn(Infer(*Call.CalleeExpr, Depth + 1));
					if (Call.Callee.empty()) return {};
					if (const auto* Local = LookupLocal(Stack, Call.Callee);
					    Local && Local->Kind != EDocumentSymbolKind::TypeAlias)
					{
						const auto Type = ResolveReceiverTypeName(Program, Symbols, Position, Call.Callee, &Locals);
						return CallableReturn(Type);
					}
					// These source declarations can be hidden behind their public
					// std aliases in the symbol list. Their explicit first type
					// argument determines the result without invoking a processor.
					if (!Call.TypeArgs.empty())
					{
						for (const auto& Decl : Program.Functions)
						{
							if (Decl.Name == Call.Callee && Decl.bExported &&
							    ModuleVisibility::CanAccessSymbol(
							        Program, Decl.ModulePath, true, Program.EntryModulePath) &&
							    (StandardLibrary::IsMethodBindingFactoryDeclaration(Decl) ||
							        (Decl.Name == "__std_make_weak_attribute" &&
							            StandardLibrary::IsAttributeImplementationModule(
							                Program, Decl.ModulePath))))
							{
								return Decl.Name == "__std_bind_method"
								    ? CallSiteType(Call.TypeArgs[0])
								    : "attribute<" + CallSiteType(Call.TypeArgs[0]) + ">";
							}
						}
					}

					std::vector<FSymbolQueryHit> Functions;
					if (!LookupLocal(Stack, Call.Callee))
					{
						CollectHits(Symbols.Symbols, Call.Callee, {}, Functions);
					}

					if (Call.Callee.find('.') == std::string::npos)
					{
						PreferVisibleNamespaceHits(Program, Symbols.Symbols, ActiveNamespace, Position, Call.Callee, Functions);
					}
					if (!Functions.empty())
					{
						std::vector<std::string> Arguments;
						for (const auto* Argument : Call.GetArguments())
						{
							Arguments.push_back(Argument ? Infer(*Argument, Depth + 1) : std::string{});
						}
						const auto Return = GetTemplateQuery().ResolveReturnType({}, Call.Callee, Arguments,
						    (Call.bExplicitTemplateArguments || !Call.TypeArgs.empty()) ? &Call.TypeArgs : nullptr);
						if (!Return.empty()) return TypeNameFromDetail(Return);
					}
					std::vector<std::string> Returns;
					std::unordered_map<std::string, std::unordered_set<std::string>> WantedSources;
					for (const auto& Function : Functions)
					{
						if (Function.Kind != EDocumentSymbolKind::Function ||
						    !Function.ContainerName.empty() ||
						    (Call.Callee.find('.') != std::string::npos &&
						        NormalizeNamespaceSpelling(StripGenericArgs(Function.Name)) != Call.Callee))
						{
							continue;
						}

						WantedSources[StripGenericArgs(Function.Name)].insert(
						    PathUtil::IdentityKey(Function.FileLabel));
					}
					std::unordered_set<std::string> MatchedSources;
					// Visit declarations once even for a large overload set.
					for (const auto& Decl : Program.Functions)
					{
						CheckAnalysisCancellation();
						const auto Wanted = WantedSources.find(Decl.Name);
						if (Wanted == WantedSources.end())
						{
							continue;
						}

						std::string FileKey = PathUtil::IdentityKey(std::string(Decl.Location.FileLabel));
						if (Wanted->second.find(FileKey) == Wanted->second.end())
						{
							FileKey = PathUtil::IdentityKey(Decl.ModulePath);
						}

						if (Wanted->second.find(FileKey) == Wanted->second.end())
						{
							continue;
						}

						MatchedSources.insert(Decl.Name + '\x1f' + FileKey);
						if (Decl.bDeleted || Call.TypeArgs.size() > Decl.TypeParams.size())
						{
							continue;
						}

						std::vector<std::pair<std::string, std::string>> TypeBindings;
						for (std::size_t Index = 0; Index < Decl.TypeParams.size(); ++Index)
						{
							std::string Arg;
							if (Index < Call.TypeArgs.size())
							{
								Arg = CallSiteType(Call.TypeArgs[Index]);
							}

							else if (Index < Decl.TypeParamDefaults.size() &&
							    !Decl.TypeParamDefaults[Index].empty())
							{
								Arg = ResolveDeclarationTypeName(Program, Decl.Name, Decl.ModulePath,
								    Decl.TypeParamDefaults[Index], TypeBindings);
							}

							TypeBindings.emplace_back(Decl.TypeParams[Index], std::move(Arg));
						}
						Returns.push_back(TypeNameFromDetail(ResolveDeclarationTypeName(
						    Program, Decl.Name, Decl.ModulePath, Decl.ReturnType, TypeBindings)));
					}
					for (const auto& Function : Functions)
					{
						const auto Name = StripGenericArgs(Function.Name);
						const auto FileKey = PathUtil::IdentityKey(Function.FileLabel);
						const auto Wanted = WantedSources.find(Name);
						if (Wanted != WantedSources.end() &&
						    Wanted->second.find(FileKey) != Wanted->second.end() &&
						    MatchedSources.find(Name + '\x1f' + FileKey) == MatchedSources.end())
						{
							Returns.push_back(TypeNameFromDetail(Function.Detail));
						}
					}
					if (!Returns.empty()) return CommonType(Returns);
					if (Call.Callee.find('.') == std::string::npos && !ResolveImplicitThisOwnerName(Program, Position).empty())
					{
						FMethodCallExpr Implicit(std::make_unique<FThisExpr>(), Call.Callee, {});
						Implicit.BorrowArgumentsFrom(Call.GetArguments());
						if (auto Type = Infer(Implicit, Depth + 1); !Type.empty()) return Type;
					}
					// Parsed type constructors can carry the whole application in
					// Callee (`Box<>`), rather than the function-call TypeArgs vector.
					const auto TypeHead = StripGenericArgs(Call.Callee);
					const auto* Local = LookupLocal(Stack, TypeHead);
					const std::string_view LookupHead = TypeHead.rfind("::", 0) == 0
					    ? std::string_view(TypeHead).substr(2)
					    : std::string_view(TypeHead);
					if (!(Local && Local->Kind == EDocumentSymbolKind::TypeAlias) &&
					    !IsKnownTypeName(LookupHead, Symbols.Symbols, Bindings))
					{
						return {};
					}

					std::string Type = Call.Callee;
					if (Type.find('<') == std::string::npos && (Call.bExplicitTemplateArguments || !Call.TypeArgs.empty()))
					{
						Type += "<";
						for (std::size_t Index = 0; Index < Call.TypeArgs.size(); ++Index)
						{
							if (Index)
							{
								Type += ",";
							}

							Type += Call.TypeArgs[Index];
						}
						Type += ">";
					}
					return ResolveTypeNameWithLocals(Program, Locals, Type);
				}
				if (Expr.Kind != EExprKind::MemberAccess && Expr.Kind != EExprKind::MethodCall) return {};
				const bool bCall = Expr.Kind == EExprKind::MethodCall;
				if (!bCall)
				{
					const FExpr* Root = &Expr;
					std::vector<std::string_view> Parts;
					while (Root && Root->Kind == EExprKind::MemberAccess && Parts.size() < 32)
					{
						const auto& Member = static_cast<const FMemberAccessExpr&>(*Root);
						// The leaf can apply template arguments to a function reference.
						// A templated intermediate receiver is handled as a type below.
						if (!Parts.empty() && (Member.bExplicitTemplateArguments || !Member.TypeArgs.empty()))
						{
							break;
						}
						Parts.push_back(Member.Member);
						Root = Member.Object.get();
					}
					if (Root && Root->Kind == EExprKind::Identifier && !Parts.empty())
					{
						const auto& Head = static_cast<const FIdentifierExpr&>(*Root).Name;
						if (!IsKnownValueReceiver(Program, Position, Head, Locals, Symbols, ActiveNamespace) && !IsKnownTypeName(Head, Symbols.Symbols, Bindings))
						{
							std::string Qualified = Head;
							for (auto Part = Parts.rbegin(); Part != Parts.rend(); ++Part)
							{
								Qualified += "." + std::string(*Part);
							}
							const auto Globals = CollectGlobalValueReceiverHits(Program, Symbols, Position, Qualified, ActiveNamespace);
							if (Globals.size() == 1)
							{
								return ResolveDeclarationTypeName(Program, Globals.front().Name, Globals.front().FileLabel,
								    TypeNameFromDetail(Globals.front().Detail));
							}
							if (!Globals.empty()) return {};
							if (auto Type = FunctionReferenceType(Expr, Qualified); !Type.empty()) return Type;
						}
					}
				}
				const FExpr* Object = bCall ? static_cast<const FMethodCallExpr&>(Expr).Object.get()
				                            : static_cast<const FMemberAccessExpr&>(Expr).Object.get();
				const std::string& Name = bCall ? static_cast<const FMethodCallExpr&>(Expr).Method
				                                : static_cast<const FMemberAccessExpr&>(Expr).Member;
				const bool TypeReceiver = Object && Object->Kind == EExprKind::Identifier &&
				    !IsKnownValueReceiver(Program, Position, static_cast<const FIdentifierExpr&>(*Object).Name, Locals, Symbols, ActiveNamespace);
				std::string Owner;
				if (TypeReceiver)
				{
					const auto& Identifier = static_cast<const FIdentifierExpr&>(*Object);
					if (Identifier.Name.find('<') != std::string::npos || Identifier.bExplicitTemplateArguments || !Identifier.TypeArgs.empty())
					{
						// Keep the complete type application. Looking up only its head
						// loses the arguments used by static field and method types.
						std::string Application = Identifier.Name;
						if (Application.find('<') == std::string::npos)
						{
							Application += "<";
							for (std::size_t Index = 0; Index < Identifier.TypeArgs.size(); ++Index)
							{
								if (Index)
								{
									Application += ",";
								}
								Application += Identifier.TypeArgs[Index];
							}
							Application += ">";
						}
						// The AST may already contain a canonical nested declaration
						// identity, which is not source syntax to lex and parse again.
						const auto Type = CallSiteType(Application);
						if (const auto* HostType = Bindings ? Bindings->FindType(Type) : nullptr)
						{
							// Host static access does not require a published constructor.
							Owner = HostType->Name;
						}
						else
						{
							FCallExpr Constructor(Type, {});
							Owner = GetTemplateQuery().ResolveExpressionType(Constructor);
						}
					}
					else
					{
						Owner = ResolveReceiverTypeName(Program, Symbols, Position, Identifier.Name, &Locals);
					}
				}
				else if (Object)
				{
					Owner = Infer(*Object, Depth + 1);
				}
				if (Owner.empty()) return {};
				// An alias may already have expanded to a closed type application.
				// Declaration symbols carry formal parameters, not those concrete arguments.
				if (TypeReceiver && !IsKnownTypeName(StripGenericArgs(Owner), Symbols.Symbols, Bindings)) return {};
				std::unordered_map<std::string, FInheritedMemberCompletion> Members;
				AppendCompletionMembersAlongHierarchy(Program, Symbols.Symbols, Owner, Name,
				    Members, AccessContext, CallableAccess, true);
				std::optional<std::string> HostReturn;
				bool bHostReturnResolved = false;
				const auto GetHostReturn = [&]() -> const std::optional<std::string>&
				{
					if (bHostReturnResolved) return HostReturn;
					bHostReturnResolved = true;
					if (const auto* Host = FindHostTypeAlongHierarchy(Bindings, Program, Owner))
					{
						std::vector<std::string> Types;
						for (const auto& Member : Host->Members)
						{
							CheckAnalysisCancellation();
							const bool bMethod = Member.Kind == EHostMemberKind::Method || Member.Kind == EHostMemberKind::StaticMethod;
							const bool bValue = Member.Kind == EHostMemberKind::Field || Member.Kind == EHostMemberKind::PropertyGetter || Member.Kind == EHostMemberKind::StaticConstant;
							if (Member.Name == Name && HostMemberCanBeNamedFromReceiver(Member, TypeReceiver) && (bCall ? bMethod : bValue))
							{
								// Display annotations must not become part of the value type.
								Types.push_back(TypeNameFromDetail(VeroScript::FormatHostMemberSignature(Member, Host->Name)));
							}
						}
						HostReturn = CommonType(Types);
					}
					return HostReturn;
				};
				std::vector<std::string> Returns;
				for (const auto& [Key, Member] : Members)
				{
					(void)Key;
					if (Member.Label == Name && (!TypeReceiver || Member.bIsStatic) &&
					    (bCall ? IsCallableCompletionKind(Member.Kind)
					           : (Member.Kind == EDocumentSymbolKind::Field || (TypeReceiver && Member.Kind == EDocumentSymbolKind::Constant))))
					{
						if (Member.bSoftBuiltin)
						{
							if (const auto& Return = GetHostReturn(); Return) return *Return;
						}
						Returns.push_back(TypeNameFromDetail(Member.ValueType));
					}
				}
				if (bCall && !Returns.empty())
				{
					const auto& Call = static_cast<const FMethodCallExpr&>(Expr);
					std::vector<std::string> Arguments;
					for (const auto* Argument : Call.GetArguments())
					{
						Arguments.push_back(Argument ? Infer(*Argument, Depth + 1) : std::string{});
					}
					const auto Return = GetTemplateQuery().ResolveReturnType(Owner, Name, Arguments,
					    (Call.bExplicitTemplateArguments || !Call.TypeArgs.empty()) ? &Call.TypeArgs : nullptr,
					    Owner.rfind("const ", 0) == 0, TypeReceiver);
					if (!Return.empty()) return TypeNameFromDetail(Return);
				}
				if (!Returns.empty()) return CommonType(Returns);
				if (Bindings && !bCall)
				{
					// Inaccessible/hidden source declarations still own the name.
					// Do not recover a same-named member from an exposed host base.
					std::vector<FSymbolQueryHit> Declared;
					CollectMemberHitsAlongHierarchy(Program, Symbols.Symbols, Owner, Name, Declared);
					if (std::any_of(Declared.begin(), Declared.end(), [](const auto& Hit) { return !Hit.bSoftBuiltin; })) return {};
				}
				if (const auto& Return = GetHostReturn(); Return && !Return->empty()) return *Return;
				if (TypeReceiver) return {};
				if (!bCall) return {};
				std::string SoftReturn;
				if (ResolveSoftContainerMethodReturnType(Owner, Name, SoftReturn, &Program)) return SoftReturn;
				for (const auto& Function : Program.Functions)
				{
					std::string Return;
					if (!Function.bDeleted && ExtensionMethodLeafName(Function.Name) == Name &&
					    ExtensionFunctionIsVisible(Program, Function, Program.EntryModulePath,
					        ActiveNamespace, Position, Bindings) &&
					    ResolveExtensionMethodReturnType(Function, Owner, Return))
					{
						Returns.push_back(std::move(Return));
					}
				}
				return CommonType(Returns);
			};
			return Infer(*Fragment, 0);
		}

		void RefineCallableLocalType(const FProgram& Program, const FDocumentSymbolResult& Symbols,
		    const IHostBindingCatalog* Bindings, FSymbolQueryHit& Hit)
		{
			if (Hit.InitializerRange.SourceId.empty() || TypeNameFromDetail(Hit.Detail) != "auto") return;
			const auto& Range = Hit.InitializerRange;
			const auto* Map = ProgramPreprocessorLineMap(Program, Range.SourceId);
			if (!Map || Range.Begin >= Range.End || Range.End > Map->PreprocessedSource.size() || Range.End - Range.Begin > 65536)
			{
				return;
			}
			const auto Infer = [&]() -> std::string
			{
				const auto Expression = std::string_view(Map->PreprocessedSource).substr(Range.Begin, Range.End - Range.Begin);
				VeroScriptLexer Lexer(Expression);
				const auto& Tokens = Lexer.Tokenize();
				// Keep ordinary literal/constructor auto inference on its existing
				// paths; this query enriches callable values only.
				if (Lexer.HasError() || Tokens.empty() || (Tokens.front().Kind != ETokenKind::Identifier && Tokens.front().Lexeme != "::" && Tokens.front().Lexeme != "("))
				{
					return {};
				}
				const FTextPosition Position{Range.Position.Begin.Line, Range.Position.Begin.Column};
				const auto Type = ResolveReceiverExpressionType(Program, Symbols, Position, Expression, Bindings,
				    VeroScriptSymbolQuery::ResolveEnclosingNamespaceName(Program, Position,
				        FindActiveNamespaceAt(Map->PreprocessedSource, Range.SourceId, Position)));
				return Runtime::IsFunctionTypeName(Type) ? Type : std::string{};
			};
			const auto Type = ActiveCallableQueryScope && ActiveCallableQueryScope->Matches(Program)
			    ? ActiveCallableQueryScope->CallableInitializerType(Range, Bindings, Infer)
			    : Infer();
			if (Type.empty()) return;
			const auto Auto = Hit.Detail.find("auto");
			if (Auto != std::string::npos)
			{
				Hit.Detail.replace(Auto, 4, FormatStandardSourceSpelling(Type));
			}
		}
