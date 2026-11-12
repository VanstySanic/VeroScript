// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

			void EmitGlobalDeclaration(const FVarDeclStmt& Var)
			{
				const std::string ResolvedType = ApplyTypeSubst(Var.TypeName);
				// A namespace-scope thread_local with a non-trivial initializer can
				// be entered before its dynamic TLS initializer on MSVC's static CRT.
				// Declare an accessor instead; its function-local thread_local is
				// initialized on first use for the current worker thread.
				Out << "static ";
				if (Var.bIsConst)
				{
					Out << "const ";
				}
				Out << MapType(ResolvedType);
				Out << "& " << MangleGlobal(Var) << "();\n";
			}

			bool StatementPreventsFallthrough(const FStmt& Stmt) const
			{
				switch (Stmt.Kind)
				{
				case EStmtKind::Return:
				case EStmtKind::Throw:
					return true;
				case EStmtKind::Block:
					{
						const auto& Block = static_cast<const FBlockStmt&>(Stmt);
						return !Block.Statements.empty() && Block.Statements.back() && StatementPreventsFallthrough(*Block.Statements.back());
					}
				case EStmtKind::If:
					{
						const auto& If = static_cast<const FIfStmt&>(Stmt);
						return If.ThenBranch && If.ElseBranch && StatementPreventsFallthrough(*If.ThenBranch) && StatementPreventsFallthrough(*If.ElseBranch);
					}
				default:
					return false;
				}
			}

			void EmitStmt(const FStmt& Stmt, const int Indent)
			{
				if (bFailed)
				{
					return;
				}
				const std::string Pad(static_cast<std::size_t>(Indent), '\t');
				switch (Stmt.Kind)
				{
				case EStmtKind::Block:
					{
						const auto& Block = static_cast<const FBlockStmt&>(Stmt);
						TValueScopeRestore<decltype(LocalTypes)> LocalScope(LocalTypes);
						TValueScopeRestore<decltype(LocalConstness)> ConstScope(LocalConstness);
						TValueScopeRestore<decltype(RetainedReferenceAliases)> ReferenceAliasScope(RetainedReferenceAliases);
						TValueScopeRestore<decltype(TypeAliases)> AliasScope(TypeAliases);
						TValueScopeRestore<decltype(StackClassLocals)> StackObjectScope(
						    StackClassLocals);
						Out << Pad << "{\n";
						for (std::size_t ChildIndex = 0;
						    ChildIndex < Block.Statements.size(); ++ChildIndex)
						{
							const std::unique_ptr<FStmt>& Child = Block.Statements[ChildIndex];
							if (Child)
							{
								if (Child->Kind == EStmtKind::VarDeclGroup)
								{
									const auto& Group =
									    static_cast<const FVarDeclGroupStmt&>(*Child);
									for (std::size_t DeclarationIndex = 0;
									    DeclarationIndex < Group.Declarations.size();
									    ++DeclarationIndex)
									{
										const std::unique_ptr<FVarDeclStmt>& Declaration =
										    Group.Declarations[DeclarationIndex];
										if (!Declaration)
										{
											Fail("AOT: invalid variable declaration group");
											return;
										}
										const bool bStackClassDeclaration = CanUseStackClassLocal(
										    *Declaration,
										    Block,
										    ChildIndex,
										    &Group,
										    DeclarationIndex);
										{
											TValueScopeRestore<std::string> DeclarationScope(
											    ActiveStackClassDeclaration);
											ActiveStackClassDeclaration = bStackClassDeclaration
											    ? Declaration->Name
											    : std::string{};
											EmitStmt(*Declaration, Indent + 1);
										}
										if (bFailed)
										{
											return;
										}
										// Each declarator shadows the outer local only after its own
										// initializer, before the next declarator is evaluated.
										StackClassLocals.erase(Declaration->Name);
										if (bStackClassDeclaration)
										{
											StackClassLocals.insert(Declaration->Name);
										}
									}
									continue;
								}
								bool bStackClassDeclaration = false;
								std::string DeclaredName;
								if (Child->Kind == EStmtKind::VarDecl)
								{
									const auto& Var = static_cast<const FVarDeclStmt&>(*Child);
									DeclaredName = Var.Name;
									bStackClassDeclaration =
									    CanUseStackClassLocal(Var, Block, ChildIndex);
								}
								{
									TValueScopeRestore<std::string> DeclarationScope(
									    ActiveStackClassDeclaration);
									ActiveStackClassDeclaration = bStackClassDeclaration
									    ? DeclaredName
									    : std::string{};
									EmitStmt(*Child, Indent + 1);
								}
								if (!DeclaredName.empty())
								{
									// The new declaration shadows any same-spelled outer local
									// only after its initializer has been emitted.
									StackClassLocals.erase(DeclaredName);
									if (bStackClassDeclaration)
									{
										StackClassLocals.insert(DeclaredName);
									}
								}
							}
						}
						if (Indent == 0 && bEmitImplicitMainReturnZero && !StatementPreventsFallthrough(Block))
						{
							Out << "\treturn 0;\n";
						}
						Out << Pad << "}\n";
						return;
					}
				case EStmtKind::Expr:
					{
						const auto& ExprStmt = static_cast<const FExprStmt&>(Stmt);
						Out << Pad;
						if (ExprStmt.Expression)
						{
							EmitExpr(*ExprStmt.Expression);
						}
						Out << ";\n";
						return;
					}
				case EStmtKind::Return:
					{
						const auto& ReturnStmt = static_cast<const FReturnStmt&>(Stmt);
						if (ReturnStmt.Value && Runtime::IsReferenceTypeName(ResolveAliasChain(ApplyTypeSubst(ActiveReturnType))))
						{
							const bool bOwnField = ReturnStmt.Value->Kind == EExprKind::MemberAccess && bInSelfContext && static_cast<const FMemberAccessExpr&>(*ReturnStmt.Value).Object && static_cast<const FMemberAccessExpr&>(*ReturnStmt.Value).Object->Kind == EExprKind::This;
							if (!bOwnField && ReferenceNeedsReturnedOwner(*ReturnStmt.Value))
							{
								Fail("AOT: returned field reference requires managed owner retention; use the VM backend");
								return;
							}
							for (const auto& Alias : RetainedReferenceAliases)
							{
								if (ExprContainsIdentifier(*ReturnStmt.Value, Alias))
								{
									Fail("AOT: returned field alias requires managed owner retention; use the VM backend");
									return;
								}
							}
						}
						Out << Pad << "return";
						if (ReturnStmt.Value)
						{
							Out << ' ';
							if (!ActiveReturnType.empty() && ActiveReturnType != "void")
							{
								const auto* ReturnType = ResolveUserTypeForLookup(ActiveReturnType);
								const bool CopyLocal = ReturnStmt.Value->Kind == EExprKind::Identifier && !Runtime::IsReferenceTypeName(ActiveReturnType) && ReturnType && !ReturnType->bIsClass && !ReturnType->bIsInterface && ReferenceOwnerHasCleanup(*ReturnType);
								if (CopyLocal)
								{
									Out << "static_cast<const " << MapType(ActiveReturnType) << "&>(";
								}
								EmitCoercedValue(ActiveReturnType, *ReturnStmt.Value);
								if (CopyLocal)
								{
									Out << ')';
								}
							}
							else
							{
								EmitExpr(*ReturnStmt.Value);
							}
						}
						Out << ";\n";
						return;
					}
				case EStmtKind::Throw:
					{
						const auto& ThrowStmt = static_cast<const FThrowStmt&>(Stmt);
						if (!ThrowStmt.Value)
						{
							if (!ActiveCatchRefRebinds.empty() && !ActiveCatchRefRebinds.back().empty())
							{
								Out << Pad << ActiveCatchRefRebinds.back() << ";\n";
							}
							Out << Pad << "throw;\n";
							return;
						}
						Out << Pad << "throw";
						if (ThrowStmt.Value)
						{
							bNeedsUtility = true;
							const std::string ThrownType = ResolveAliasChain(
							    ApplyTypeSubst(InferExprType(*ThrowStmt.Value)));
							const std::vector<std::string> BaseTypes =
							    CollectAotBaseTypes(ThrownType);
							bool bWrappedValueEmitted = false;
							if (BaseTypes.empty())
							{
								Out << " vs_MakeException(";
							}
							else
							{
								const std::string MappedThrownType = MapType(ThrownType);
								static constexpr std::string_view SharedPtrPrefix =
								    "std::shared_ptr<";
								const bool bReferenceBacked =
								    MappedThrownType.compare(0, SharedPtrPrefix.size(), SharedPtrPrefix) == 0;
								if (bReferenceBacked)
								{
									Out << " vs_MakeExceptionAs<" << MappedThrownType;
									for (const std::string& BaseType : BaseTypes)
									{
										Out << ", " << MapType(BaseType);
									}
									Out << ">(";
									EmitExpr(*ThrowStmt.Value);
									Out << ')';
									bWrappedValueEmitted = true;
								}
								else
								{
									std::vector<std::string> ValueBases;
									std::vector<std::string> HandleBases;
									for (const std::string& BaseType : BaseTypes)
									{
										const std::string MappedBase = MapType(BaseType);
										if (MappedBase.compare(
										        0, SharedPtrPrefix.size(), SharedPtrPrefix) == 0)
										{
											HandleBases.push_back(MapBaseCppType(BaseType));
										}
										else
										{
											ValueBases.push_back(MappedBase);
										}
									}
									Out << " vs_MakeValueExceptionAs(";
									EmitExpr(*ThrowStmt.Value);
									Out << ", vs_TypeList<";
									for (std::size_t Index = 0; Index < ValueBases.size(); ++Index)
									{
										if (Index != 0)
										{
											Out << ", ";
										}
										Out << ValueBases[Index];
									}
									Out << ">{}, vs_TypeList<";
									for (std::size_t Index = 0; Index < HandleBases.size(); ++Index)
									{
										if (Index != 0)
										{
											Out << ", ";
										}
										Out << HandleBases[Index];
									}
									Out << ">{})";
									bWrappedValueEmitted = true;
								}
							}
							if (!bWrappedValueEmitted)
							{
								EmitExpr(*ThrowStmt.Value);
								Out << ')';
							}
						}
						Out << ";\n";
						return;
					}
				case EStmtKind::Try:
					{
						const auto& TryStmt = static_cast<const FTryStmt&>(Stmt);
						bNeedsUtility = true;
						const std::uint32_t FinallyId = NextFinallyId++;
						const std::string ActiveName =
						    GeneratedLocalName("___vs_finally_active_" + std::to_string(FinallyId));
						const std::string GuardName =
						    GeneratedLocalName("___vs_finally_guard_" + std::to_string(FinallyId));
						const std::string ExceptionName =
						    GeneratedLocalName("___vs_exception_" + std::to_string(FinallyId));
						if (TryStmt.FinallyBlock)
						{
							Out << Pad << "bool " << ActiveName << " = true;\n";
							Out << Pad << "auto " << GuardName << " = vs_MakeFinally([&]()\n";
							EmitStmt(*TryStmt.FinallyBlock, Indent);
							Out << Pad << ", " << ActiveName << ");\n";
							Out << Pad << "try\n"
							    << Pad << "{\n";
						}
						const int BodyIndent = Indent + (TryStmt.FinallyBlock ? 1 : 0);
						const std::string BodyPad(static_cast<std::size_t>(BodyIndent), '\t');
						if (!TryStmt.Catches.empty())
						{
							Out << BodyPad << "try\n";
							if (TryStmt.TryBlock)
							{
								EmitStmt(*TryStmt.TryBlock, BodyIndent);
							}
							Out << BodyPad << "catch (vs_ExceptionCarrier& " << ExceptionName << ")\n"
							    << BodyPad << "{\n";
							bool bHasConditional = false;
							bool bHasCatchAll = false;
							for (std::size_t CatchIndex = 0;
							    CatchIndex < TryStmt.Catches.size(); ++CatchIndex)
							{
								const FCatchClause& Clause = TryStmt.Catches[CatchIndex];
								TValueScopeRestore<decltype(LocalTypes)> LocalScope(LocalTypes);
								TValueScopeRestore<decltype(LocalConstness)> ConstScope(LocalConstness);
								TValueScopeRestore<decltype(StackClassLocals)> StackObjectScope(
								    StackClassLocals);
								Out << BodyPad << "\t";
								if (bHasConditional)
								{
									Out << "else" << (Clause.bCatchAll ? '\n' : ' ');
									if (Clause.bCatchAll)
									{
										Out << BodyPad << "\t";
									}
								}
								if (Clause.bCatchAll)
								{
									Out << "{\n";
									bHasCatchAll = true;
								}
								else
								{
									const std::string MappedType =
									    MapType(ApplyTypeSubst(Clause.TypeName));
									const std::string CaughtName = GeneratedLocalName("___vs_caught_" + std::to_string(FinallyId) + "_" + std::to_string(CatchIndex));
									Out << "if (auto* " << CaughtName << " = " << ExceptionName
									    << ".get<" << MappedType << ">())\n"
									    << BodyPad << "\t{\n"
									    << BodyPad << "\t\t";
									if (!Clause.Name.empty())
									{
										if (Clause.bIsConst)
										{
											Out << "const ";
										}
										Out << MappedType;
										if (Clause.bIsRef)
										{
											Out << '&';
										}
										Out << ' ' << Clause.Name << " = *" << CaughtName << ";\n";
										LocalTypes[Clause.Name] = ApplyTypeSubst(Clause.TypeName);
										LocalConstness[Clause.Name] = Clause.bIsConst;
										StackClassLocals.erase(Clause.Name);
									}
									bHasConditional = true;
								}
								const bool bNeedsRefRebind =
								    Clause.bIsRef && !Clause.Name.empty();
								ActiveCatchRefRebinds.push_back(bNeedsRefRebind
								        ? ExceptionName + ".rebind(" + Clause.Name + ")"
								        : std::string{});
								if (Clause.Body)
								{
									EmitStmt(*Clause.Body, BodyIndent + 2);
								}
								ActiveCatchRefRebinds.pop_back();
								Out << BodyPad << "\t}\n";
							}
							if (!bHasCatchAll)
							{
								Out << BodyPad << "\t";
								if (bHasConditional)
								{
									Out << "else\n"
									    << BodyPad << "\t";
								}
								Out << "{\n"
								    << BodyPad << "\t\tthrow;\n"
								    << BodyPad << "\t}\n";
							}
							Out << BodyPad << "}\n";
						}
						else if (TryStmt.TryBlock)
						{
							EmitStmt(*TryStmt.TryBlock, BodyIndent);
						}
						if (TryStmt.FinallyBlock)
						{
							Out << Pad << "}\n"
							    << Pad << "catch (...)\n"
							    << Pad << "{\n";
							Out << Pad << "\t" << ActiveName << " = false;\n";
							EmitStmt(*TryStmt.FinallyBlock, Indent + 1);
							Out << Pad << "\tthrow;\n"
							    << Pad << "}\n";
							Out << Pad << ActiveName << " = false;\n";
							EmitStmt(*TryStmt.FinallyBlock, Indent);
						}
						return;
					}
				case EStmtKind::VarDecl:
					{
						const auto& Var = static_cast<const FVarDeclStmt&>(Stmt);
						TValueScopeRestore<decltype(RetainedReferenceReceivers)> ReceiverScope(RetainedReferenceReceivers);
						// Consume the top-level marker before mapping the type or
						// initializer. Both operations may recursively emit function
						// bodies (generic monomorphs/lambdas), whose locals must remain
						// ordinary automatic storage.
						const bool bTopLevelVariable = bEmittingTopLevelVariable;
						bEmittingTopLevelVariable = false;
						const std::string ResolvedType = ResolveVariableType(Var);
						// Touch MapType early so open generics like Box<int> monomorphize.
						(void)MapType(ResolvedType);
						std::string EffectiveType = ResolveAliasChain(ResolvedType);
						if (Var.bIsLocalStatic)
						{
							if (EffectiveType == "auto" && Var.Initializer)
							{
								EffectiveType = ResolveValueType(InferExprType(*Var.Initializer));
							}
							if (!Runtime::IsPrimitiveValueTypeName(EffectiveType))
							{
								Fail("AOT: local static objects and dependent types require the Core AOT exporter");
								return;
							}
							bNeedsLocalStatics = true;
							bNeedsMemory = true;
							bNeedsVector = true;
							Out << Pad << (Var.bIsConst ? "const " : "") << "auto& " << Var.Name
							    << " = *::VsLocalStatic<" << MapType(EffectiveType) << ">([&]() -> " << MapType(EffectiveType) << " { return ";
							if (Var.Initializer)
							{
								EmitCoercedValue(EffectiveType, *Var.Initializer);
							}
							else
							{
								Out << MapType(EffectiveType) << "{}";
							}
							Out << "; });\n";
							LocalTypes[Var.Name] = EffectiveType;
							LocalConstness[Var.Name] = Var.bIsConst;
							return;
						}
						if (EffectiveType == "auto" && Var.Initializer)
						{
							const auto Inferred =
							    ResolveAliasChain(ApplyTypeSubst(InferExprType(*Var.Initializer)));
							if (!Inferred.empty())
							{
								EffectiveType = Inferred;
							}
						}
						// Prefer concrete monomorph name in LocalTypes when applicable.
						{
							const std::string::size_type Lt = EffectiveType.find('<');
							if (Lt != std::string::npos)
							{
								const std::string Base = EffectiveType.substr(0, Lt);
								if (OpenGenericTypes.find(Base) != OpenGenericTypes.end())
								{
									std::vector<std::string> Args;
									const std::string Full = EffectiveType;
									SplitTopLevelTypeArgs(
									    Full.substr(Lt + 1, Full.size() - Lt - 2),
									    Args);
									const std::string Mono = EnsureTypeMonomorph(Base, Args);
									if (!Mono.empty())
									{
										EffectiveType = Mono;
									}
								}
							}
						}
						if (bTopLevelVariable)
						{
							const std::string CppType = MapType(ResolvedType);
							const std::string GlobalName = MangleGlobal(Var);
							Out << Pad << "static ";
							if (Var.bIsConst)
							{
								Out << "const ";
							}
							Out << CppType << "& " << GlobalName << "()\n"
							    << Pad << "{\n";
							const std::string InnerPad = Pad + "\t";
							// The initialization barrier eagerly visits globals in script declaration
							// order. Re-entering it here also makes direct global access from a
							// worker thread establish that worker's independent script state.
							Out << InnerPad << "vs_ensure_globals();\n";
							if (Var.bIsRef && Var.Initializer)
							{
								PrepareReferenceInitializer(*Var.Initializer, InnerPad, true);
							}

							if (Var.bIsRef)
							{
								Out << InnerPad << "thread_local ";
								if (Var.bIsConst)
								{
									Out << "const ";
								}
								Out << CppType << "* const Value = &";
							}
							else
							{
								Out << InnerPad << "thread_local ";
								if (Var.bIsConst)
								{
									Out << "const ";
								}
								Out << CppType << " Value";
							}
							if (Var.Initializer)
							{
								Out << " = ";
							}
							else
							{
								Out << "{}";
							}
							const bool bContextualArrayInit =
							    Var.Initializer && Var.Initializer->Kind == EExprKind::ArrayInit && static_cast<const FArrayInitExpr&>(*Var.Initializer).TypeName.empty();
							const bool bContextualMapInit =
							    Var.Initializer && Var.Initializer->Kind == EExprKind::MapInit && static_cast<const FMapInitExpr&>(*Var.Initializer).TypeName.empty();
							if (Var.Initializer)
							{
								const std::string* Hint =
								    (bContextualArrayInit || bContextualMapInit) ? &ResolvedType : nullptr;
								if (Hint || Var.bIsRef)
								{
									EmitExpr(*Var.Initializer, Hint);
								}

								else
								{
									EmitCoercedValue(ResolvedType, *Var.Initializer);
								}
							}
							Out << ";\n"
							    << InnerPad << "return "
							    << (Var.bIsRef ? "*Value" : "Value") << ";\n"
							    << Pad << "}\n";
							return;
						}
						const std::size_t PreviousReceivers = RetainedReferenceReceivers.size();
						if (Var.bIsRef && Var.Initializer)
						{
							PrepareReferenceInitializer(*Var.Initializer, Pad);
						}

						bool bRetainedAlias =
						    Var.bIsRef && RetainedReferenceReceivers.size() != PreviousReceivers;
						if (Var.bIsRef && Var.Initializer)
						{
							for (const auto& Alias : RetainedReferenceAliases)
							{
								bRetainedAlias |= ExprContainsIdentifier(*Var.Initializer, Alias);
							}
						}

						auto ActivateLocal = [&]()
						{
							LocalTypes[Var.Name] = EffectiveType;
							LocalConstness[Var.Name] = Var.bIsConst;
							RetainedReferenceAliases.erase(Var.Name);
							if (bRetainedAlias)
							{
								RetainedReferenceAliases.insert(Var.Name);
							}
						};
						auto EmitLocalInitializer = [&]()
						{
							const bool bContextualArrayInit =
							    Var.Initializer->Kind == EExprKind::ArrayInit && static_cast<const FArrayInitExpr&>(*Var.Initializer).TypeName.empty();
							const bool bContextualMapInit =
							    Var.Initializer->Kind == EExprKind::MapInit && static_cast<const FMapInitExpr&>(*Var.Initializer).TypeName.empty();
							const std::string* Hint =
							    (bContextualArrayInit || bContextualMapInit)
							    ? &ResolvedType
							    : nullptr;
							if (Hint || Var.bIsRef)
							{
								EmitExpr(*Var.Initializer, Hint);
							}
							else
							{
								EmitCoercedValue(ResolvedType, *Var.Initializer);
							}
						};
						std::string PrecomputedInitializer;
						if (Var.Initializer && ExprContainsIdentifier(*Var.Initializer, Var.Name))
						{
							PrecomputedInitializer = "___vs_initializer_" + std::to_string(NextLocalInitializerId++);
							// Preserve the initializer's value category.  Binding the outer value to
							// auto&& avoids an observable extra copy for shadowing declarations and
							// also keeps a temporary alive until the real local has been initialized.
							Out << Pad << "auto&& " << PrecomputedInitializer << " = ";
							EmitLocalInitializer();
							Out << ";\n";
						}
						if (ActiveStackClassDeclaration == Var.Name)
						{
							const auto& Call = static_cast<const FCallExpr&>(*Var.Initializer);
							const auto TypeIt = UserTypes.find(EffectiveType);
							if (TypeIt == UserTypes.end())
							{
								Fail("AOT: stack class local type disappeared during emission");
								return;
							}
							std::vector<std::string> ArgTypes;
							ArgTypes.reserve(Call.Arguments.size());
							for (const auto& Arg : Call.Arguments)
							{
								ArgTypes.push_back(Arg ? InferExprType(*Arg) : std::string{});
							}
							const FConstructorDecl* Ctor = ResolveCtor(TypeIt->second, ArgTypes);
							if (!TypeIt->second.Constructors->empty() && !Ctor)
							{
								Fail("AOT: no matching stack-local constructor for '" + EffectiveType + "'");
								return;
							}
							Out << Pad << MapBaseCppType(EffectiveType) << ' '
							    << Var.Name << "{};\n";
							Out << Pad;
							const auto EmitInit = [&]
							{
								Out << MangleCtorInit(EffectiveType) << '(' << Var.Name;
								if (Ctor)
								{
									if (!Ctor->Parameters.empty())
									{
										Out << ", ";
									}

									EmitCallArgsForUserTypeParams(
									    TypeIt->second, Ctor->Parameters, Call.Arguments);
								}
								Out << ", VsConstructionAddress(" << Var.Name << "))";
							};
							if (!TryEmitSequencedExpression(Call, nullptr, EmitInit))
							{
								EmitInit();
							}

							Out << ";\n";
							ActivateLocal();
							return;
						}
						ActivateLocal();
						Out << Pad;
						if (Var.bIsConst)
						{
							Out << "const ";
						}
						Out << MapType(ResolvedType);
						if (Var.bIsRef)
						{
							Out << '&';
						}
						Out << ' ' << (bTopLevelVariable ? MangleGlobal(Var) : Var.Name);
						if (Var.Initializer)
						{
							Out << " = ";
							if (!PrecomputedInitializer.empty())
							{
								if (Var.bIsRef)
								{
									Out << PrecomputedInitializer;
								}
								else
								{
									bNeedsUtility = true;
									Out << "std::forward<decltype("
									    << PrecomputedInitializer << ")>("
									    << PrecomputedInitializer << ')';
								}
							}
							else
							{
								EmitLocalInitializer();
							}
						}
						Out << ";\n";
						return;
					}
				case EStmtKind::VarDeclGroup:
					{
						const auto& Group = static_cast<const FVarDeclGroupStmt&>(Stmt);
						for (const std::unique_ptr<FVarDeclStmt>& Declaration :
						    Group.Declarations)
						{
							if (!Declaration)
							{
								Fail("AOT: invalid variable declaration group");
								return;
							}
							EmitStmt(*Declaration, Indent);
							if (bFailed)
							{
								return;
							}
							StackClassLocals.erase(Declaration->Name);
						}
						return;
					}
				case EStmtKind::If:
					{
						const auto& IfStmt = static_cast<const FIfStmt&>(Stmt);
						Out << Pad << "if (";
						EmitExpr(*IfStmt.Condition);
						Out << ")\n";
						if (IfStmt.ThenBranch)
						{
							TValueScopeRestore<decltype(LocalTypes)> LocalScope(LocalTypes);
							TValueScopeRestore<decltype(LocalConstness)> ConstScope(LocalConstness);
							TValueScopeRestore<decltype(TypeAliases)> AliasScope(TypeAliases);
							EmitStmt(*IfStmt.ThenBranch, Indent);
						}
						else
						{
							Out << Pad << "{\n"
							    << Pad << "}\n";
						}
						if (IfStmt.ElseBranch)
						{
							Out << Pad << "else\n";
							TValueScopeRestore<decltype(LocalTypes)> LocalScope(LocalTypes);
							TValueScopeRestore<decltype(LocalConstness)> ConstScope(LocalConstness);
							TValueScopeRestore<decltype(TypeAliases)> AliasScope(TypeAliases);
							EmitStmt(*IfStmt.ElseBranch, Indent);
						}
						return;
					}
				case EStmtKind::While:
					{
						const auto& WhileStmt = static_cast<const FWhileStmt&>(Stmt);
						Out << Pad << "while (";
						EmitExpr(*WhileStmt.Condition);
						Out << ")\n";
						if (WhileStmt.Body)
						{
							TValueScopeRestore<decltype(LocalTypes)> LocalScope(LocalTypes);
							TValueScopeRestore<decltype(LocalConstness)> ConstScope(LocalConstness);
							TValueScopeRestore<decltype(TypeAliases)> AliasScope(TypeAliases);
							EmitStmt(*WhileStmt.Body, Indent);
						}
						else
						{
							Out << Pad << "{\n"
							    << Pad << "}\n";
						}
						return;
					}
				case EStmtKind::DoWhile:
					{
						const auto& DoWhileStmt = static_cast<const FDoWhileStmt&>(Stmt);
						if (!DoWhileStmt.Condition)
						{
							Fail("AOT: do-while statement has no condition");
							return;
						}
						Out << Pad << "do\n";
						if (DoWhileStmt.Body)
						{
							TValueScopeRestore<decltype(LocalTypes)> LocalScope(LocalTypes);
							TValueScopeRestore<decltype(LocalConstness)> ConstScope(LocalConstness);
							TValueScopeRestore<decltype(TypeAliases)> AliasScope(TypeAliases);
							EmitStmt(*DoWhileStmt.Body, Indent);
						}
						else
						{
							Out << Pad << "{\n"
							    << Pad << "}\n";
						}
						Out << Pad << "while (";
						EmitExpr(*DoWhileStmt.Condition);
						Out << ");\n";
						return;
					}
				case EStmtKind::For:
					{
						TValueScopeRestore<decltype(LocalTypes)> LocalScope(LocalTypes);
						TValueScopeRestore<decltype(RetainedReferenceAliases)> ReferenceAliasScope(RetainedReferenceAliases);
						TValueScopeRestore<decltype(LocalConstness)> ConstScope(LocalConstness);
						TValueScopeRestore<decltype(TypeAliases)> AliasScope(TypeAliases);
						TValueScopeRestore<decltype(StackClassLocals)> StackObjectScope(
						    StackClassLocals);
						const auto& ForStmt = static_cast<const FForStmt&>(Stmt);
						bool bLowerDeclarationInit = false;
						if (ForStmt.Init && ForStmt.Init->Kind == EStmtKind::VarDecl)
						{
							const auto& Var = static_cast<const FVarDeclStmt&>(*ForStmt.Init);
							bLowerDeclarationInit = Var.bIsLocalStatic || (Var.Initializer && (ExprContainsIdentifier(*Var.Initializer, Var.Name) || (Var.bIsRef && Var.Initializer->Kind != EExprKind::Identifier)));
						}
						else if (ForStmt.Init && ForStmt.Init->Kind == EStmtKind::VarDeclGroup)
						{
							for (const std::unique_ptr<FVarDeclStmt>& Declaration :
							    static_cast<const FVarDeclGroupStmt&>(
							        *ForStmt.Init)
							        .Declarations)
							{
								if (Declaration && (Declaration->bIsLocalStatic || (Declaration->Initializer && (ExprContainsIdentifier(*Declaration->Initializer, Declaration->Name) || (Declaration->bIsRef && Declaration->Initializer->Kind != EExprKind::Identifier)))))
								{
									bLowerDeclarationInit = true;
									break;
								}
							}
						}
						int ForIndent = Indent;
						std::string ForPad = Pad;
						if (bLowerDeclarationInit)
						{
							Out << Pad << "{\n";
							ForIndent = Indent + 1;
							ForPad.assign(static_cast<std::size_t>(ForIndent) * 4, ' ');
							EmitStmt(*ForStmt.Init, ForIndent);
							if (bFailed)
							{
								return;
							}
							if (ForStmt.Init->Kind == EStmtKind::VarDecl)
							{
								StackClassLocals.erase(
								    static_cast<const FVarDeclStmt&>(*ForStmt.Init).Name);
							}
						}
						Out << ForPad << "for (";
						bool bHasInitExpression = false;
						if (ForStmt.Init && !bLowerDeclarationInit)
						{
							if (ForStmt.Init->Kind == EStmtKind::VarDecl)
							{
								const auto& Var = static_cast<const FVarDeclStmt&>(*ForStmt.Init);
								const std::string ResolvedType = ApplyTypeSubst(Var.TypeName);
								if (Var.bIsConst)
								{
									Out << "const ";
								}
								Out << MapType(ResolvedType);
								if (Var.bIsRef)
								{
									Out << '&';
								}
								Out << ' ' << Var.Name;
								if (Var.Initializer)
								{
									Out << " = ";
									EmitCoercedValue(ResolvedType, *Var.Initializer);
								}
								LocalTypes[Var.Name] = ResolvedType;
								LocalConstness[Var.Name] = Var.bIsConst;
								StackClassLocals.erase(Var.Name);
								bHasInitExpression = true;
							}
							else if (ForStmt.Init->Kind == EStmtKind::VarDeclGroup)
							{
								const auto& Group =
								    static_cast<const FVarDeclGroupStmt&>(*ForStmt.Init);
								if (Group.Declarations.empty() || !Group.Declarations.front())
								{
									Fail("AOT: invalid for-init variable declaration group");
									return;
								}
								const FVarDeclStmt& First = *Group.Declarations.front();
								const std::string ResolvedType = ApplyTypeSubst(First.TypeName);
								if (First.bIsConst)
								{
									Out << "const ";
								}
								Out << MapType(ResolvedType) << ' ';
								for (std::size_t Index = 0;
								    Index < Group.Declarations.size(); ++Index)
								{
									const std::unique_ptr<FVarDeclStmt>& Declaration =
									    Group.Declarations[Index];
									if (!Declaration)
									{
										Fail("AOT: invalid for-init variable declaration group");
										return;
									}
									const std::string DeclarationType =
									    ApplyTypeSubst(Declaration->TypeName);
									if (DeclarationType != ResolvedType || Declaration->bIsConst != First.bIsConst || Declaration->bIsRef != First.bIsRef)
									{
										Fail("AOT: inconsistent for-init variable declaration group");
										return;
									}
									if (Index != 0)
									{
										Out << ", ";
									}
									if (Declaration->bIsRef)
									{
										Out << '&';
									}
									Out << Declaration->Name;
									if (Declaration->Initializer)
									{
										Out << " = ";
										EmitCoercedValue(
										    DeclarationType, *Declaration->Initializer);
									}
									LocalTypes[Declaration->Name] = DeclarationType;
									LocalConstness[Declaration->Name] = Declaration->bIsConst;
									StackClassLocals.erase(Declaration->Name);
								}
								bHasInitExpression = true;
							}
							else if (ForStmt.Init->Kind == EStmtKind::Expr)
							{
								const auto& ExprStmt = static_cast<const FExprStmt&>(*ForStmt.Init);
								if (ExprStmt.Expression)
								{
									EmitExpr(*ExprStmt.Expression);
									bHasInitExpression = true;
								}
							}
							else
							{
								Fail("AOT: unsupported for-init statement");
								return;
							}
						}
						for (const std::unique_ptr<FExpr>& InitExpression :
						    ForStmt.InitExpressions)
						{
							if (!InitExpression)
							{
								Fail("AOT: invalid for-init expression");
								return;
							}
							if (bHasInitExpression)
							{
								Out << ", ";
							}
							EmitExpr(*InitExpression);
							bHasInitExpression = true;
						}
						Out << "; ";
						if (ForStmt.Condition)
						{
							EmitExpr(*ForStmt.Condition);
						}
						Out << "; ";
						bool bHasStepExpression = false;
						if (ForStmt.Step)
						{
							EmitExpr(*ForStmt.Step);
							bHasStepExpression = true;
						}
						for (const std::unique_ptr<FExpr>& StepExpression :
						    ForStmt.StepExpressions)
						{
							if (!StepExpression)
							{
								Fail("AOT: invalid for-step expression");
								return;
							}
							if (bHasStepExpression)
							{
								Out << ", ";
							}
							EmitExpr(*StepExpression);
							bHasStepExpression = true;
						}
						Out << ")\n";
						if (ForStmt.Body)
						{
							EmitStmt(*ForStmt.Body, ForIndent);
						}
						else
						{
							Out << ForPad << "{\n"
							    << ForPad << "}\n";
						}
						if (bLowerDeclarationInit)
						{
							Out << Pad << "}\n";
						}
						return;
					}
				case EStmtKind::Break:
					{
						Out << Pad << "break;\n";
						return;
					}
				case EStmtKind::Continue:
					{
						Out << Pad << "continue;\n";
						return;
					}
				case EStmtKind::TypeAlias:
					{
						const auto& Alias = static_cast<const FTypeAliasStmt&>(Stmt);
						TypeAliases[Alias.Name] = Alias.TypeName;
						Out << Pad << "using " << Alias.Name << " = " << MapType(Alias.TypeName)
						    << ";\n";
						return;
					}
				case EStmtKind::ForEach:
					{
						TValueScopeRestore<decltype(LocalTypes)> LocalScope(LocalTypes);
						TValueScopeRestore<decltype(LocalConstness)> ConstScope(LocalConstness);
						TValueScopeRestore<decltype(TypeAliases)> AliasScope(TypeAliases);
						TValueScopeRestore<decltype(StackClassLocals)> StackObjectScope(
						    StackClassLocals);
						const auto& ForEachStmt = static_cast<const FForEachStmt&>(Stmt);
						if (!ForEachStmt.Collection)
						{
							Fail("AOT: foreach missing collection");
							return;
						}
						std::string CollType = ResolveAliasChain(
						    ApplyTypeSubst(InferExprType(*ForEachStmt.Collection)));
						std::string ElementType;
						std::string MapKeyType;
						std::uint32_t NativeLength = 0;
						const bool bDynamicArray =
						    Runtime::ParseDynamicArrayElementType(CollType, ElementType);
						const bool bNativeArray = !bDynamicArray && Runtime::ParseNativeArrayType(CollType, ElementType, NativeLength);
						const bool bMapForeach = !bDynamicArray && !bNativeArray && (Runtime::ParseMapType(CollType, MapKeyType, ElementType) || Runtime::ParseOrderedMapType(CollType, MapKeyType, ElementType));
						const bool bSetForeach = !bDynamicArray && !bNativeArray && !bMapForeach && Runtime::ParseSetElementType(CollType, ElementType);
						const bool bMultiSetForeach =
						    !bDynamicArray && !bNativeArray && !bMapForeach && !bSetForeach && Runtime::ParseMultiSetElementType(CollType, ElementType);
						const bool bDequeForeach = !bDynamicArray && !bNativeArray && !bMapForeach && !bSetForeach && !bMultiSetForeach && Runtime::ParseDequeElementType(CollType, ElementType);
						const bool bQueueForeach = !bDynamicArray && !bNativeArray && !bMapForeach && !bSetForeach && !bMultiSetForeach && !bDequeForeach && Runtime::ParseQueueElementType(CollType, ElementType);

						if (!bDynamicArray && !bNativeArray && !bMapForeach && !bSetForeach && !bMultiSetForeach && !bDequeForeach && !bQueueForeach)
						{
							Fail(
							    "AOT: foreach currently supports array, map, set, deque, or queue");
							return;
						}
						if (ForEachStmt.bHasKeyBinding && !bMapForeach)
						{
							Fail("AOT: map destructure foreach requires map or ordered_map");
							return;
						}

						if (ForEachStmt.bHasKeyBinding)
						{
							std::string DeclKey = ForEachStmt.KeyTypeName;
							if (DeclKey == "auto")
							{
								DeclKey = MapKeyType;
							}
							DeclKey = ResolveAliasChain(ApplyTypeSubst(DeclKey));
							std::string DeclVal = ForEachStmt.ElementTypeName;
							if (DeclVal == "auto")
							{
								DeclVal = ElementType;
							}
							DeclVal = ResolveAliasChain(ApplyTypeSubst(DeclVal));
							const bool bStructuredBinding =
							    ForEachStmt.bIsRef && ForEachStmt.KeyTypeName == "auto" && ForEachStmt.ElementTypeName == "auto";
							if (bStructuredBinding)
							{
								Out << Pad << "for (";
								if (ForEachStmt.bIsConst)
								{
									Out << "const ";
								}
								Out << "auto& [" << ForEachStmt.KeyName << ", "
								    << ForEachStmt.ElementName << "] : ";
								EmitExpr(*ForEachStmt.Collection);
								Out << ")\n";
								LocalTypes[ForEachStmt.KeyName] = DeclKey;
								const FUserTypeInfo* ElementInfo = ResolveUserTypeForLookup(DeclVal);
								LocalTypes[ForEachStmt.ElementName] = ElementInfo ? ElementInfo->Name : DeclVal;
								LocalConstness[ForEachStmt.KeyName] = ForEachStmt.bKeyIsConst;
								LocalConstness[ForEachStmt.ElementName] = ForEachStmt.bIsConst;
								StackClassLocals.erase(ForEachStmt.KeyName);
								StackClassLocals.erase(ForEachStmt.ElementName);
								if (ForEachStmt.Body)
								{
									EmitStmt(*ForEachStmt.Body, Indent);
								}
								else
								{
									Out << Pad << "{\n"
									    << Pad << "}\n";
								}
								return;
							}

							Out << Pad << "for (auto& " << GeneratedLocalName("___vs_kv") << " : ";
							EmitExpr(*ForEachStmt.Collection);
							Out << ")\n"
							    << Pad << "{\n";
							Out << Pad << "\t";
							if (ForEachStmt.bKeyIsConst)
							{
								Out << "const ";
							}
							Out << MapType(DeclKey) << ' ' << ForEachStmt.KeyName
							    << " = " << GeneratedLocalName("___vs_kv") << ".first;\n";
							Out << Pad << "\t";
							if (ForEachStmt.bIsConst)
							{
								Out << "const ";
							}
							Out << MapType(DeclVal);
							if (ForEachStmt.bIsRef)
							{
								Out << '&';
							}
							Out << ' ' << ForEachStmt.ElementName;
							Out << " = " << GeneratedLocalName("___vs_kv") << ".second;\n";
							LocalTypes[ForEachStmt.KeyName] = DeclKey;
							const FUserTypeInfo* ElementInfo = ResolveUserTypeForLookup(DeclVal);
							LocalTypes[ForEachStmt.ElementName] = ElementInfo ? ElementInfo->Name : DeclVal;
							LocalConstness[ForEachStmt.KeyName] = ForEachStmt.bKeyIsConst;
							LocalConstness[ForEachStmt.ElementName] = ForEachStmt.bIsConst;
							StackClassLocals.erase(ForEachStmt.KeyName);
							StackClassLocals.erase(ForEachStmt.ElementName);
							if (ForEachStmt.Body)
							{
								EmitStmt(*ForEachStmt.Body, Indent + 1);
							}
							Out << Pad << "}\n";
							return;
						}

						if (bMapForeach)
						{
							std::string Declared = ForEachStmt.ElementTypeName;
							if (Declared == "auto")
							{
								Declared = ElementType;
							}
							Declared = ResolveAliasChain(ApplyTypeSubst(Declared));
							std::string PairFirst;
							std::string PairSecond;
							const bool bPairElem = Runtime::ParsePairType(Declared, PairFirst, PairSecond) && MapType(PairFirst) == MapType(MapKeyType) && MapType(PairSecond) == MapType(ElementType);
							if (bPairElem && HasUserPairTemplate())
							{
								Fail("AOT: map entries with a user pair<T,U> template require the VM backend");
								return;
							}
							if (bPairElem)
							{
								// pair / ref pair foreach over map -> entry view.
								bNeedsUtility = true;
								Out << Pad << "for (auto& " << GeneratedLocalName("___vs_kv") << " : ";
								EmitExpr(*ForEachStmt.Collection);
								Out << ")\n"
								    << Pad << "{\n"
								    << Pad << "\t";
								if (ForEachStmt.bIsRef)
								{
									Out << "vs_MapEntryRef<" << MapType(PairFirst) << ", "
									    << MapType(PairSecond) << "> "
									    << ForEachStmt.ElementName
									    << "{" << GeneratedLocalName("___vs_kv") << ".first, " << GeneratedLocalName("___vs_kv") << ".second};\n";
								}
								else
								{
									Out << "vs_InternalPair<" << MapType(PairFirst) << ", "
									    << MapType(PairSecond) << "> "
									    << ForEachStmt.ElementName
									    << "{" << GeneratedLocalName("___vs_kv") << ".first, " << GeneratedLocalName("___vs_kv") << ".second};\n";
								}
								LocalTypes[ForEachStmt.ElementName] = Declared;
								LocalConstness[ForEachStmt.ElementName] = ForEachStmt.bIsConst;
								StackClassLocals.erase(ForEachStmt.ElementName);
								if (ForEachStmt.Body)
								{
									EmitStmt(*ForEachStmt.Body, Indent + 1);
								}
								Out << Pad << "}\n";
								return;
							}
							Out << Pad << "for (auto& " << GeneratedLocalName("___vs_kv") << " : ";
							EmitExpr(*ForEachStmt.Collection);
							Out << ")\n"
							    << Pad << "{\n";
							Out << Pad << "\t";
							if (ForEachStmt.bIsConst)
							{
								Out << "const ";
							}
							Out << MapType(Declared);
							if (ForEachStmt.bIsRef)
							{
								Out << '&';
							}
							Out << ' ' << ForEachStmt.ElementName << " = " << GeneratedLocalName("___vs_kv") << ".second;\n";
							const FUserTypeInfo* ElementInfo = ResolveUserTypeForLookup(Declared);
							LocalTypes[ForEachStmt.ElementName] = ElementInfo ? ElementInfo->Name : Declared;
							LocalConstness[ForEachStmt.ElementName] = ForEachStmt.bIsConst;
							StackClassLocals.erase(ForEachStmt.ElementName);
							if (ForEachStmt.Body)
							{
								EmitStmt(*ForEachStmt.Body, Indent + 1);
							}
							Out << Pad << "}\n";
							return;
						}

						std::string Declared = ForEachStmt.ElementTypeName;
						if (Declared == "auto")
						{
							Declared = ElementType;
						}
						Declared = ResolveAliasChain(ApplyTypeSubst(Declared));
						Out << Pad << "for (";
						if (ForEachStmt.bIsConst)
						{
							Out << "const ";
						}
						Out << MapType(Declared);
						if (ForEachStmt.bIsRef)
						{
							Out << '&';
						}
						Out << ' ' << ForEachStmt.ElementName << " : ";
						if (bQueueForeach || bMultiSetForeach)
						{
							Out << '(';
							EmitExpr(*ForEachStmt.Collection);
							Out << ").values()";
						}
						else
						{
							EmitExpr(*ForEachStmt.Collection);
						}
						Out << ")\n";
						const FUserTypeInfo* ElementInfo = ResolveUserTypeForLookup(Declared);
						LocalTypes[ForEachStmt.ElementName] = ElementInfo ? ElementInfo->Name : Declared;
						LocalConstness[ForEachStmt.ElementName] = ForEachStmt.bIsConst;
						StackClassLocals.erase(ForEachStmt.ElementName);
						if (ForEachStmt.Body)
						{
							EmitStmt(*ForEachStmt.Body, Indent);
						}
						else
						{
							Out << Pad << "{\n"
							    << Pad << "}\n";
						}
						return;
					}
				case EStmtKind::Switch:
					{
						const auto& SwitchStmt = static_cast<const FSwitchStmt&>(Stmt);
						if (!SwitchStmt.Discriminant)
						{
							Fail("AOT: switch missing discriminant");
							return;
						}
						std::string DiscType = ResolveAliasChain(
						    ApplyTypeSubst(InferExprType(*SwitchStmt.Discriminant)));
						if (DiscType != "int" && DiscType != "int64" && DiscType != "uint64" && DiscType != "unsigned" && DiscType != "unsigned int" && EnumTypeNames.find(DiscType) == EnumTypeNames.end())
						{
							Fail("AOT: switch currently supports integer/enum only");
							return;
						}
						Out << Pad << "switch (";
						EmitExpr(*SwitchStmt.Discriminant);
						Out << ")\n"
						    << Pad << "{\n";
						for (const FSwitchCase& Case : SwitchStmt.Cases)
						{
							TValueScopeRestore<decltype(LocalTypes)> LocalScope(LocalTypes);
							TValueScopeRestore<decltype(LocalConstness)> ConstScope(LocalConstness);
							TValueScopeRestore<decltype(TypeAliases)> AliasScope(TypeAliases);
							if (Case.bIsDefault)
							{
								Out << Pad << "default:\n";
							}
							else
							{
								for (const std::unique_ptr<FExpr>& Label : Case.Labels)
								{
									if (!Label)
									{
										continue;
									}
									Out << Pad << "case ";
									EmitExpr(*Label);
									Out << ":\n";
								}
							}
							Out << Pad << "{\n";
							if (Case.Body)
							{
								for (const std::unique_ptr<FStmt>& Child : Case.Body->Statements)
								{
									if (!Child || Child->Kind == EStmtKind::Fallthrough)
									{
										continue;
									}
									EmitStmt(*Child, Indent + 2);
								}
							}
							if (!BlockEndsWithFallthrough(Case.Body.get()))
							{
								Out << Pad << "\tbreak;\n";
							}
							Out << Pad << "}\n";
						}
						Out << Pad << "}\n";
						return;
					}
				case EStmtKind::Goto:
					{
						const auto& GotoStmt = static_cast<const FGotoStmt&>(Stmt);
						Out << Pad << "goto " << GotoStmt.Name << ";\n";
						return;
					}
				case EStmtKind::Label:
					{
						const auto& LabelStmt = static_cast<const FLabelStmt&>(Stmt);
						Out << Pad << LabelStmt.Name << ":\n";
						return;
					}
				case EStmtKind::Fallthrough:
					// Marker consumed by switch emit; ignore if reached here.
					return;
				default:
					{
						Fail("AOT: unsupported statement kind");
						return;
					}
				}
			}

			void CheckParamSupport(const FFunctionParam& /*Param*/)
			{
				// Default / variadic supported (2.45): defaults filled at call sites;
				// variadic maps to std::vector<T>.
			}

			std::string MapParamTypeAllowVariadic(const FFunctionParam& Param) const
			{
				if (Param.bIsVariadic)
				{
					bNeedsVector = true;
					return "std::vector<" + MapType(Param.TypeName) + ">";
				}
				return MapParamType(Param.TypeName, Param.bIsRef, Param.bIsConst);
			}

			void EmitFunctionSignature(const FFunctionDecl& Function, const bool bPrototype)
			{
				if (bFailed || Function.bDeleted)
				{
					return;
				}
				Out << MapType(Function.ReturnType) << ' ' << EmitFunctionCppName(Function) << '(';
				for (std::size_t Index = 0; Index < Function.Parameters.size(); ++Index)
				{
					const FFunctionParam& Param = Function.Parameters[Index];
					if (Index > 0)
					{
						Out << ", ";
					}
					Out << MapParamTypeAllowVariadic(Param) << ' ' << Param.Name;
				}
				Out << ')';
				if (bPrototype)
				{
					Out << ";\n";
				}
			}

			void EmitFunction(const FFunctionDecl& Function)
			{
				TValueScopeRestore<decltype(RetainedReferenceAliases)> ReferenceAliasScope(RetainedReferenceAliases);
				RetainedReferenceAliases.clear();
				if (bFailed || Function.bDeleted)
				{
					return;
				}
				LocalTypes.clear();
				LocalConstness.clear();
				bInSelfContext = false;
				bSelfIsConst = false;
				SelfTypeName.clear();
				TypeSubst.clear();
				ActiveModulePath = Function.ModulePath;
				ActiveNamespace = NamespaceOfSymbol(Function.Name);
				ActiveFunctionName = Function.Name;
				ActiveReturnType = Function.ReturnType;
				for (const FFunctionParam& Param : Function.Parameters)
				{
					LocalConstness[Param.Name] = Param.bIsConst;
					if (Param.bIsVariadic)
					{
						LocalTypes[Param.Name] = "array<" + Param.TypeName + ">";
					}
					else if (Runtime::IsScriptArgsArrayTypeName(Param.TypeName))
					{
						// Treat argv as dynamic string array for len/foreach.
						LocalTypes[Param.Name] = "array<string>";
					}
					else
					{
						LocalTypes[Param.Name] = Param.TypeName;
					}
				}
				EmitFunctionSignature(Function, false);
				Out << '\n';
				if (!Function.Body)
				{
					Fail("AOT: function body required");
					return;
				}
				bEmitImplicitMainReturnZero =
				    IsEntryMain(Function) && ResolveAliasChain(Function.ReturnType) == "int";
				if (!Function.Body->DefinitionModulePath.empty())
				{
					ActiveModulePath = Function.Body->DefinitionModulePath;
					ActiveNamespace = Function.Body->DefinitionNamespace;
				}
				EmitStmt(*Function.Body, 0);
				bEmitImplicitMainReturnZero = false;
				Out << '\n';
			}

			void EmitSpecializationSignature(const FSpecialization& Spec, const bool bPrototype)
			{
				if (bFailed || !Spec.Generic)
				{
					return;
				}
				// Emit once per generic as an open C++ template (not per monomorph).
				if (EmittedGenericTemplates.find(Spec.Generic) != EmittedGenericTemplates.end())
				{
					return;
				}
				EmittedGenericTemplates.insert(Spec.Generic);
				ActiveOpenTypeParams.clear();
				for (const std::string& Param : Spec.Generic->TypeParams)
				{
					ActiveOpenTypeParams.insert(Param);
				}
				Out << "template <";
				for (std::size_t Index = 0; Index < Spec.Generic->TypeParams.size(); ++Index)
				{
					if (Index > 0)
					{
						Out << ", ";
					}
					Out << "typename " << Spec.Generic->TypeParams[Index];
				}
				ActiveOpenBounds = Spec.Generic->Bounds;
				EmitEnableIfTemplateParam(Spec.Generic->Bounds);
				Out << ">\n";
				Out << MapType(Spec.Generic->ReturnType) << ' '
				    << EmitFunctionCppName(*Spec.Generic) << '(';
				for (std::size_t Index = 0; Index < Spec.Generic->Parameters.size(); ++Index)
				{
					const FFunctionParam& Param = Spec.Generic->Parameters[Index];
					CheckParamSupport(Param);
					if (bFailed)
					{
						ActiveOpenBounds.clear();
						ActiveOpenTypeParams.clear();
						return;
					}
					if (Index > 0)
					{
						Out << ", ";
					}
					Out << MapParamTypeAllowVariadic(Param)
					    << ' ' << Param.Name;
				}
				Out << ')';
				ActiveOpenBounds.clear();
				ActiveOpenTypeParams.clear();
				if (bPrototype)
				{
					Out << ";\n";
				}
			}

			void EmitSpecialization(const FSpecialization& Spec)
			{
				TValueScopeRestore<decltype(RetainedReferenceAliases)> ReferenceAliasScope(RetainedReferenceAliases);
				RetainedReferenceAliases.clear();
				if (bFailed || !Spec.Generic)
				{
					return;
				}
				if (EmittedGenericTemplateBodies.find(Spec.Generic) != EmittedGenericTemplateBodies.end())
				{
					return;
				}
				EmittedGenericTemplateBodies.insert(Spec.Generic);
				LocalTypes.clear();
				LocalConstness.clear();
				bInSelfContext = false;
				bSelfIsConst = false;
				SelfTypeName.clear();
				TypeSubst.clear();
				ActiveOpenTypeParams.clear();
				for (const std::string& Param : Spec.Generic->TypeParams)
				{
					ActiveOpenTypeParams.insert(Param);
				}
				ActiveModulePath = Spec.Generic->ModulePath;
				ActiveNamespace = NamespaceOfSymbol(Spec.Generic->Name);
				ActiveFunctionName = Spec.Generic->Name;
				ActiveReturnType = Spec.Generic->ReturnType;
				for (const FFunctionParam& Param : Spec.Generic->Parameters)
				{
					LocalTypes[Param.Name] = Param.bIsVariadic
					    ? ("array<" + Param.TypeName + ">")
					    : Param.TypeName;
					LocalConstness[Param.Name] = Param.bIsConst;
				}
				Out << "template <";
				for (std::size_t Index = 0; Index < Spec.Generic->TypeParams.size(); ++Index)
				{
					if (Index > 0)
					{
						Out << ", ";
					}
					Out << "typename " << Spec.Generic->TypeParams[Index];
				}
				ActiveOpenBounds = Spec.Generic->Bounds;
				EmitEnableIfTemplateParam(Spec.Generic->Bounds, false);
				Out << ">\n";
				Out << MapType(Spec.Generic->ReturnType) << ' '
				    << EmitFunctionCppName(*Spec.Generic) << '(';
				for (std::size_t Index = 0; Index < Spec.Generic->Parameters.size(); ++Index)
				{
					const FFunctionParam& Param = Spec.Generic->Parameters[Index];
					CheckParamSupport(Param);
					if (bFailed)
					{
						ActiveOpenBounds.clear();
						ActiveOpenTypeParams.clear();
						return;
					}
					if (Index > 0)
					{
						Out << ", ";
					}
					Out << MapParamTypeAllowVariadic(Param)
					    << ' ' << Param.Name;
				}
				Out << ")\n";
				if (!Spec.Generic->Body)
				{
					Fail("AOT: specialized function body required");
					ActiveOpenBounds.clear();
					ActiveOpenTypeParams.clear();
					return;
				}
				if (!Spec.Generic->Body->DefinitionModulePath.empty())
				{
					ActiveModulePath = Spec.Generic->Body->DefinitionModulePath;
					ActiveNamespace = Spec.Generic->Body->DefinitionNamespace;
				}
				EmitStmt(*Spec.Generic->Body, 0);
				Out << '\n';
				ActiveOpenBounds.clear();
				ActiveOpenTypeParams.clear();
				TypeSubst.clear();
			}
