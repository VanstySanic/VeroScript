// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

			static bool TryExtractStaticReceiverName(
			    const FExpr& Expr,
			    std::string& OutName,
			    const std::unordered_map<std::string, std::string>* const Locals)
			{
				if (Expr.Kind == EExprKind::Identifier)
				{
					OutName = static_cast<const FIdentifierExpr&>(Expr).Name;
					return true;
				}
				if (Expr.Kind == EExprKind::MemberAccess)
				{
					const auto& Access = static_cast<const FMemberAccessExpr&>(Expr);
					if (!Access.Object)
					{
						return false;
					}
					// Value binding on the leftmost identifier (`obj.field`) is an
					// instance path, not a namespaced type (`Ns.Box`).
					if (Locals && Access.Object->Kind == EExprKind::Identifier)
					{
						const std::string& Leaf =
						    static_cast<const FIdentifierExpr&>(*Access.Object).Name;
						if (Locals->find(Leaf) != Locals->end())
						{
							return false;
						}
					}
					std::string BaseName;
					if (!TryExtractStaticReceiverName(*Access.Object, BaseName, Locals))
					{
						return false;
					}
					OutName = BaseName + '.' + Access.Member;
					return true;
				}
				return false;
			}

			bool TryBuildNamespaceCallName(
			    const FExpr& Receiver,
			    const std::string& MethodName,
			    std::string& OutName) const
			{
				OutName.clear();
				std::string ReceiverName;
				if (!TryExtractStaticReceiverName(Receiver, ReceiverName, &LocalTypes) || LocalTypes.find(ReceiverName) != LocalTypes.end())
				{
					return false;
				}
				const bool bGlobalQualified = ReceiverName.rfind("::", 0) == 0;
				if (bGlobalQualified)
				{
					ReceiverName.erase(0, 2);
				}
				for (std::size_t Pos = 0;
				    (Pos = ReceiverName.find("::", Pos)) != std::string::npos;)
				{
					ReceiverName.replace(Pos, 2, ".");
					++Pos;
				}
				if (!NamespaceExists(ReceiverName))
				{
					return false;
				}
				OutName = bGlobalQualified ? "::" : std::string{};
				OutName += ReceiverName;
				OutName += '.';
				OutName += MethodName;
				return true;
			}

			bool IsConstCastOfThis(const FExpr& Expression) const
			{
				if (Expression.Kind != EExprKind::Call)
				{
					return false;
				}
				const FCallExpr& Call = static_cast<const FCallExpr&>(Expression);
				if (Call.Callee != "const_cast" || Call.TypeArgs.size() != 1 || Call.Arguments.size() != 1 || !Call.Arguments.front())
				{
					return false;
				}
				const FExpr& Operand = *Call.Arguments.front();
				return Operand.Kind == EExprKind::This || (Operand.Kind == EExprKind::Identifier && static_cast<const FIdentifierExpr&>(Operand).Name == "this");
			}

			bool TryResolveStaticTypeReceiver(
			    const FExpr& Expr,
			    std::string& OutCppQualifier,
			    const std::vector<FFieldDecl>*& OutFields,
			    const std::vector<FMethodDecl>*& OutMethods) const
			{
				OutFields = nullptr;
				OutMethods = nullptr;
				OutCppQualifier.clear();
				std::string Name;
				if (!TryExtractStaticReceiverName(Expr, Name, &LocalTypes))
				{
					return false;
				}
				Name = ResolveAliasChain(ApplyTypeSubst(Name));
				// A value binding (local/param) shadows any same-named type.
				if (LocalTypes.find(Name) != LocalTypes.end())
				{
					return false;
				}
				if (EnumTypeNames.find(Name) != EnumTypeNames.end())
				{
					return false;
				}
				// Type application, e.g. `Box<int>` -> open template `Box`.
				const std::string::size_type Lt = Name.find('<');
				if (Lt != std::string::npos && !Name.empty() && Name.back() == '>')
				{
					const std::string Base = Name.substr(0, Lt);
					const auto OpenIt = OpenGenericTypes.find(Base);
					if (OpenIt == OpenGenericTypes.end())
					{
						return false;
					}
					OutFields = OpenIt->second.Fields;
					OutMethods = OpenIt->second.Methods;
					OutCppQualifier = MapBaseCppType(Name);
					return true;
				}
				const auto It = UserTypes.find(Name);
				if (It != UserTypes.end() && It->second.OpenGenericName.empty() && It->second.TypeParams.empty())
				{
					OutFields = It->second.Fields;
					OutMethods = It->second.Methods;
					OutCppQualifier = MapBaseCppType(Name);
					return true;
				}
				return false;
			}

			// Bare field in method/ctor: (*this).f / (*self).f / self.f.
			void EmitSelfFieldAccess(const std::string& FieldName)
			{
				if (CapturedClassThisLambdaDepth != 0u)
				{
					Out << "(*" << CapturedThisName << ")." << FieldName;
					return;
				}
				if (CapturedStructThisLambdaDepth != 0u)
				{
					Out << CapturedThisName << '.' << FieldName;
					return;
				}
				if (bMemberMethodBody)
				{
					Out << "(*this)." << FieldName;
					return;
				}
				if (bCtorInitDirectSelf)
				{
					Out << SelfReceiverName << '.' << FieldName;
					return;
				}
				const auto TypeIt = UserTypes.find(SelfTypeName);
				const bool bDeref = TypeIt != UserTypes.end() && (TypeIt->second.bIsClass || TypeIt->second.bIsInterface);
				Out << (bDeref ? "(*" + SelfReceiverName + ")." : SelfReceiverName + '.') << FieldName;
			}

			bool TryResolveFunctionValue(const FIdentifierExpr& Identifier,
			    std::string& CppName, std::string& FunctionType)
			{
				// Explicit template function values retain the Core AOT boundary.
				if (Identifier.bExplicitTemplateArguments || !Identifier.TypeArgs.empty() || LocalTypes.count(Identifier.Name))
				{
					return false;
				}
				if (bInSelfContext)
				{
					const auto* Self = ResolveUserTypeForLookup(SelfTypeName);
					if (Self && !FieldType(*Self, Identifier.Name).empty()) return false;
				}
				const auto Names = BuildFreeFunctionLookupNames(Identifier.Name, &Identifier.Location);
				const FFunctionDecl* Match = nullptr;
				for (const auto* Function : ConcreteFunctions)
				{
					if (Function->bDeleted || !Function->TypeParams.empty() || std::find(Names.begin(), Names.end(), Function->Name) == Names.end() || !IsVisibleFromActiveModule(Function->ModulePath, Function->bExported))
					{
						continue;
					}
					if (Match) return false; // Leave contextual overload selection to EmitCoercedValue.
					Match = Function;
				}
				if (!Match) return false;
				TValueScopeRestore<std::string> ModuleScope(ActiveModulePath);
				TValueScopeRestore<std::string> NamespaceScope(ActiveNamespace);
				ActiveModulePath = Match->ModulePath;
				ActiveNamespace = NamespaceOfSymbol(Match->Name);
				std::vector<std::string> Parameters;
				for (const auto& Parameter : Match->Parameters)
				{
					if (Parameter.bIsVariadic) return false;
					auto Type = ResolveAliasChain(Parameter.TypeName);
					if (Parameter.bIsRef && !Runtime::IsReferenceTypeName(Type))
					{
						Type = (Parameter.bIsConst ? "const ref " : "ref ") + Type;
					}
					Parameters.push_back(std::move(Type));
				}
				FunctionType = Runtime::MakeFunctionTypeName(Parameters, ResolveAliasChain(Match->ReturnType));
				CppName = EmitFunctionCppName(*Match);
				return true;
			}

			bool TryGetNamespaceFunctionReference(const FMemberAccessExpr& Access, FIdentifierExpr& Reference)
			{
				if (!Access.Object) return false;
				const FExpr* Root = Access.Object.get();
				while (Root && Root->Kind == EExprKind::MemberAccess)
				{
					Root = static_cast<const FMemberAccessExpr&>(*Root).Object.get();
				}
				if (Root && Root->Kind == EExprKind::Identifier)
				{
					const auto& Name = static_cast<const FIdentifierExpr&>(*Root).Name;
					if (Name == "Super" || LocalTypes.count(Name) || ResolveGlobalVariable(Name, &Root->Location))
					{
						return false;
					}
					const auto TypeName = ResolveAliasChain(ApplyTypeSubst(Name));
					if (UserTypes.count(TypeName) || OpenGenericTypes.count(TypeName) || EnumTypeNames.count(TypeName) || ActiveOpenTypeParams.count(TypeName))
					{
						return false;
					}
					if (bInSelfContext)
					{
						const auto* Self = ResolveUserTypeForLookup(SelfTypeName);
						if (Self && !FieldType(*Self, Name).empty()) return false;
					}
				}
				if (!TryBuildNamespaceCallName(*Access.Object, Access.Member, Reference.Name) || ResolveGlobalVariable(Reference.Name, &Access.Location))
				{
					return false;
				}
				Reference.Location = Access.Location;
				Reference.TypeArgs = Access.TypeArgs;
				Reference.bExplicitTemplateArguments = Access.bExplicitTemplateArguments;
				return true;
			}

			std::string InferExprType(const FExpr& Expr)
			{
				if (bFailed)
				{
					return {};
				}
				switch (Expr.Kind)
				{
				case EExprKind::CharacterLiteral:
					return "char";
				case EExprKind::IntegerLiteral:
					{
						const auto& Literal = static_cast<const FIntegerLiteralExpr&>(Expr);
						if (Literal.bIs64)
						{
							return Literal.bUnsigned ? "uint64" : "int64";
						}
						return Literal.bUnsigned ? "unsigned int" : "int";
					}
				case EExprKind::FloatLiteral:
					return static_cast<const FFloatLiteralExpr&>(Expr).bFloat32
					    ? "float"
					    : "double";
				case EExprKind::BooleanLiteral:
					return "bool";
				case EExprKind::StringLiteral:
					return "string";
				case EExprKind::WStringLiteral:
					return "wstring";
				case EExprKind::NullptrLiteral:
					return {};
				case EExprKind::Ref:
					return InferExprType(*static_cast<const FRefExpr&>(Expr).Operand);
				case EExprKind::This:
					return SelfTypeName;
				case EExprKind::Identifier:
					{
						const std::string& Name =
						    static_cast<const FIdentifierExpr&>(Expr).Name;
						const auto It = LocalTypes.find(Name);
						if (It != LocalTypes.end())
						{
							return ResolveAliasChain(ApplyTypeSubst(It->second));
						}
						if (bInSelfContext && !SelfTypeName.empty())
						{
							const auto TypeIt = UserTypes.find(SelfTypeName);
							if (TypeIt != UserTypes.end())
							{
								const std::string Field = FieldType(TypeIt->second, Name);
								if (!Field.empty())
								{
									return ResolveAliasChain(ApplyTypeSubst(Field));
								}
							}
						}
						const auto BareIt = EnumBareMemberTypeNames.find(Name);
						if (BareIt != EnumBareMemberTypeNames.end() && !BareIt->second.empty())
						{
							return ResolveAliasChain(ApplyTypeSubst(BareIt->second));
						}
						// Bare identifiers follow Core: unique free functions precede globals.
						std::string CppName, FunctionType;
						if (TryResolveFunctionValue(static_cast<const FIdentifierExpr&>(Expr), CppName, FunctionType))
						{
							return FunctionType;
						}
						if (const FVarDeclStmt* Global =
						        ResolveGlobalVariable(Name, &Expr.Location))
						{
							return ResolveAliasChain(ApplyTypeSubst(Global->TypeName));
						}
						return {};
					}
				case EExprKind::MemberAccess:
					{
						const auto& Access = static_cast<const FMemberAccessExpr&>(Expr);
						FIdentifierExpr Reference("");
						if (TryGetNamespaceFunctionReference(Access, Reference))
						{
							std::string CppName, FunctionType;
							if (TryResolveFunctionValue(Reference, CppName, FunctionType)) return FunctionType;
						}
						std::string QualifiedGlobal;
						if (Access.Object &&
						    TryBuildNamespaceCallName(*Access.Object, Access.Member, QualifiedGlobal))
						{
							if (const auto* Global = ResolveGlobalVariable(QualifiedGlobal, &Expr.Location))
							{
								return ResolveAliasChain(ApplyTypeSubst(Global->TypeName));
							}
						}

						if (Access.Object && Access.Object->Kind == EExprKind::Identifier)
						{
							const std::string OwnerName = ResolveAliasChain(
							    ApplyTypeSubst(
							        static_cast<const FIdentifierExpr&>(*Access.Object).Name));
							if (EnumTypeNames.find(OwnerName) != EnumTypeNames.end())
							{
								if (ScopedEnumTypeNames.find(OwnerName) != ScopedEnumTypeNames.end())
								{
									return OwnerName;
								}
								const auto Underlying = EnumUnderlyingTypeNames.find(OwnerName);
								return Underlying == EnumUnderlyingTypeNames.end()
								    ? std::string{"int"}
								    : ResolveAliasChain(ApplyTypeSubst(Underlying->second));
							}
						}
						// Type-name static data member (`Counter.Count`, `Box<int>.Count`).
						{
							std::string StaticQual;
							const std::vector<FFieldDecl>* StaticFields = nullptr;
							const std::vector<FMethodDecl>* StaticMethods = nullptr;
							if (Access.Object && TryResolveStaticTypeReceiver(*Access.Object, StaticQual, StaticFields, StaticMethods))
							{
								const FFieldDecl* StaticField =
								    FindFieldInList(StaticFields, Access.Member);
								if (StaticField && StaticField->bIsStatic)
								{
									// Substitute against the *receiver* type args
									// (`Box<int>` → T=int), never the ambient TypeSubst
									// of the body currently being emitted.
									std::string Effective = StaticField->TypeName;
									std::string RecvName;
									if (!Access.Object || !TryExtractStaticReceiverName(*Access.Object, RecvName, &LocalTypes))
									{
										RecvName.clear();
									}
									const std::string::size_type Lt = RecvName.find('<');
									if (Lt != std::string::npos && !RecvName.empty() && RecvName.back() == '>')
									{
										const std::string Base = RecvName.substr(0, Lt);
										const auto OpenIt = OpenGenericTypes.find(Base);
										std::vector<std::string> Args;
										if (OpenIt != OpenGenericTypes.end() && TryParseTypeApplication(RecvName, Base, Args) && Args.size() == OpenIt->second.TypeParams.size())
										{
											std::unordered_map<std::string, std::string> Bindings;
											for (std::size_t Index = 0;
											    Index < Args.size();
											    ++Index)
											{
												Bindings.emplace(
												    OpenIt->second.TypeParams[Index],
												    Args[Index]);
											}
											Effective = SubstituteWithMap(Effective, Bindings);
										}
									}
									return ResolveAliasChain(Effective);
								}
							}
						}
						std::string NamespaceGlobalName;
						if (Access.Object && TryBuildNamespaceCallName(*Access.Object, Access.Member, NamespaceGlobalName))
						{
							if (const FVarDeclStmt* Global = ResolveGlobalVariable(
							        NamespaceGlobalName, &Expr.Location))
							{
								return ResolveAliasChain(ApplyTypeSubst(Global->TypeName));
							}
						}
						const std::string ObjectType = InferExprType(*Access.Object);
						std::string HandleInner;
						if (Access.Member == "value" && TryParseHandleType(ObjectType, HandleInner))
						{
							return HandleInner;
						}
						std::vector<std::string> SoftArgs;
						if (TryParseSoftBuiltinType(ObjectType, "optional", SoftArgs) && SoftArgs.size() == 1)
						{
							if (Access.Member == "present")
							{
								return "bool";
							}
							if (Access.Member == "value")
							{
								return SoftArgs[0];
							}
						}
						if (TryParseSoftBuiltinType(ObjectType, "type", SoftArgs) && SoftArgs.size() == 1 && (Access.Member == "name" || Access.Member == "qualified_name"))
						{
							return "string";
						}
						std::string PairFirst;
						std::string PairSecond;
						if (!HasUserPairTemplate() && Runtime::ParsePairType(ObjectType, PairFirst, PairSecond))
						{
							if (Access.Member == "first")
							{
								return PairFirst;
							}
							if (Access.Member == "second")
							{
								return PairSecond;
							}
						}
						const FUserTypeInfo* TypeInfo = ResolveUserTypeForLookup(ObjectType);
						return TypeInfo
						    ? ResolveAliasChain(ApplyTypeSubst(FieldType(*TypeInfo, Access.Member)))
						    : std::string{};
					}
				case EExprKind::Unary:
					{
						const auto& Unary = static_cast<const FUnaryExpr&>(Expr);
						const std::string Operand = ResolveValueType(InferExprType(*Unary.Operand));
						if (const char* const OpName = UnaryOpOperatorName(Unary.Op))
						{
							const auto TypeIt = UserTypes.find(Operand);
							if (TypeIt != UserTypes.end())
							{
								if (const FMethodDecl* Method =
								        ResolveMethod(TypeIt->second, OpName, {}))
								{
									return ResolveAliasChain(
									    SubstForUserType(TypeIt->second, Method->ReturnType));
								}
							}
							if (const FFunctionDecl* Function =
							        ResolveConcreteOverload(OpName, {Operand}, &Unary.Location))
							{
								return ResolveAliasChain(ApplyTypeSubst(Function->ReturnType));
							}
						}
						if (Unary.Op == EUnaryOp::Not)
						{
							return "bool";
						}
						if (IsUnsignedIntScriptType(Operand))
						{
							return ResolveAliasChain(ApplyTypeSubst(Operand)) == "uint64"
							    ? "uint64"
							    : "unsigned int";
						}
						if (IsSignedIntScriptType(Operand))
						{
							return ResolveAliasChain(ApplyTypeSubst(Operand)) == "int64"
							    ? "int64"
							    : "int";
						}
						if (Operand == "float" || Operand == "double")
						{
							// Unary +/- preserves floating width, matching the compiler,
							// VM and generated C++ expression semantics.
							return Operand;
						}
						return Operand;
					}
				case EExprKind::Binary:
					{
						const auto& Binary = static_cast<const FBinaryExpr&>(Expr);
						const std::string Left = ResolveValueType(InferExprType(*Binary.Left));
						const std::string Right = ResolveValueType(InferExprType(*Binary.Right));
						if (Binary.Op == EBinaryOp::Add && (IsWStringType(Left) || IsWStringType(Right)))
						{
							return "wstring";
						}
						if (Binary.Op == EBinaryOp::Add && (IsStringType(Left) || IsStringType(Right)))
						{
							return "string";
						}
						if (Binary.Op == EBinaryOp::In)
						{
							return "bool";
						}
						if (Binary.Op == EBinaryOp::Equal || Binary.Op == EBinaryOp::NotEqual || Binary.Op == EBinaryOp::Less || Binary.Op == EBinaryOp::LessEqual || Binary.Op == EBinaryOp::Greater || Binary.Op == EBinaryOp::GreaterEqual || Binary.Op == EBinaryOp::LogicalAnd || Binary.Op == EBinaryOp::LogicalOr)
						{
							return "bool";
						}
						if (const char* const OpName = BinaryOpOperatorName(Binary.Op))
						{
							const auto LeftIt = UserTypes.find(Left);
							if (LeftIt != UserTypes.end())
							{
								if (const FMethodDecl* Method =
								        ResolveMethod(LeftIt->second, OpName, {Right}))
								{
									return ResolveAliasChain(
									    SubstForUserType(LeftIt->second, Method->ReturnType));
								}
							}
							const auto RightIt = UserTypes.find(Right);
							if (RightIt != UserTypes.end())
							{
								if (const FMethodDecl* Method =
								        ResolveMethod(RightIt->second, OpName, {Left}))
								{
									return ResolveAliasChain(
									    SubstForUserType(RightIt->second, Method->ReturnType));
								}
							}
							if (const FFunctionDecl* Function =
							        ResolveConcreteOverload(OpName, {Left, Right}, &Binary.Location))
							{
								return ResolveAliasChain(ApplyTypeSubst(Function->ReturnType));
							}
							if (const FFunctionDecl* Function =
							        ResolveConcreteOverload(OpName, {Right, Left}, &Binary.Location))
							{
								return ResolveAliasChain(ApplyTypeSubst(Function->ReturnType));
							}
						}
						if (Binary.Op == EBinaryOp::ShiftLeft || Binary.Op == EBinaryOp::ShiftRight)
						{
							const std::string EffectiveLeft =
							    ResolveAliasChain(ApplyTypeSubst(Left));
							if (EffectiveLeft == "int64" || EffectiveLeft == "uint64")
							{
								return EffectiveLeft;
							}
							if (IsUnsignedIntScriptType(EffectiveLeft))
							{
								return "unsigned int";
							}
							if (IsSignedIntScriptType(EffectiveLeft))
							{
								return "int";
							}
						}
						if (const std::string Promoted =
						        PromoteNumericScriptTypes(Left, Right);
						    !Promoted.empty())
						{
							return Promoted;
						}
						if (!Left.empty())
						{
							return Left;
						}
						return Right;
					}
				case EExprKind::Assign:
					return InferExprType(*static_cast<const FAssignExpr&>(Expr).Target);
				case EExprKind::Conditional:
					{
						const auto& Cond = static_cast<const FConditionalExpr&>(Expr);
						const std::string ThenType = InferExprType(*Cond.ThenExpr);
						return ThenType.empty() ? InferExprType(*Cond.ElseExpr) : ThenType;
					}
				case EExprKind::Call:
					{
						const auto& Call = static_cast<const FCallExpr&>(Expr);
						if (Call.Callee == "__std_bind_method" && IsMethodBindingFactoryCall(Call))
						{
							return ApplyTypeSubst(Call.TypeArgs[0]);
						}
						if ((IsMethodBindingFactoryCall(Call) && Call.Callee != "__std_bind_method") ||
						    IsWeakAttributeFactoryCall(Call))
						{
							return "attribute<" + ApplyTypeSubst(Call.TypeArgs[0]) + ">";
						}

						if ((Call.Callee == "cast" || Call.Callee == "try_cast" || Call.Callee == "const_cast") && Call.TypeArgs.size() == 1)
						{
							const std::string TargetType = ResolveAliasChain(
							    ApplyTypeSubst(Call.TypeArgs.front()));
							return Call.Callee == "try_cast"
							    ? "optional<" + TargetType + ">"
							    : TargetType;
						}
						if (Call.Callee.empty() && Call.CalleeExpr)
						{
							const std::string CalleeType = ResolveAliasChain(
							    ApplyTypeSubst(InferExprType(*Call.CalleeExpr)));
							std::vector<std::string> FnParams;
							std::string FnReturn;
							if (Runtime::ParseFunctionType(CalleeType, FnParams, FnReturn))
							{
								return FnReturn;
							}
							return {};
						}
						if (!IsFormatFamilyIntrinsicCall(Call))
						{
							const auto LocalIt = LocalTypes.find(Call.Callee);
							if (LocalIt != LocalTypes.end())
							{
								std::vector<std::string> FnParamTypes;
								std::string FnReturnType;
								const std::string LocalType = ResolveAliasChain(
								    ApplyTypeSubst(LocalIt->second));
								std::vector<std::string> AttributeArgs;
								if (bHasStandardAttribute &&
								    TryParseSoftBuiltinType(LocalType, "attribute", AttributeArgs) &&
								    AttributeArgs.size() == 1)
								{
									return AttributeArgs[0];
								}

								return Runtime::ParseFunctionType(LocalType, FnParamTypes, FnReturnType)
								    ? FnReturnType
								    : std::string{};
							}
						}
						if (Call.Callee == "len" || Call.Callee == "__find" || Call.Callee == "__channel_size" || Call.Callee == "__channel_capacity")
						{
							return "int64";
						}
						if (Call.Callee == "print" || Call.Callee == "printf" || Call.Callee == "assert")
						{
							return "void";
						}
						if (Call.Callee == "format" || Call.Callee == "formatf")
						{
							return "string";
						}
						if (Call.Callee == "to_string")
						{
							return "string";
						}
						if (Call.Callee == "to_wstring")
						{
							return "wstring";
						}
						if (Call.Callee == "to_int")
						{
							return "int";
						}
						if (Call.Callee == "to_int64")
						{
							return "int64";
						}
						if (Call.Callee == "to_uint64")
						{
							return "uint64";
						}
						if (Call.Callee == "to_double")
						{
							return "double";
						}
						if (Call.Callee == "to_float")
						{
							return "float";
						}
						if (Call.Callee == "abs" && Call.Arguments.size() == 1)
						{
							return InferExprType(*Call.Arguments[0]);
						}
						if ((Call.Callee == "min" || Call.Callee == "max") && Call.Arguments.size() == 2)
						{
							return PromoteNumericScriptTypes(
							    InferExprType(*Call.Arguments[0]),
							    InferExprType(*Call.Arguments[1]));
						}
						{
							std::string HandleInnerCheck;
							if (Call.Callee == "handle" && !HasNonGenericHandleType() && OpenGenericTypes.find("handle") == OpenGenericTypes.end())
							{
								std::string InnerType;
								if (!Call.Arguments.empty())
								{
									InnerType = InferExprType(*Call.Arguments[0]);
								}
								return InnerType.empty() ? std::string{} : ("handle<" + InnerType + ">");
							}
							if (TryParseHandleType(Call.Callee, HandleInnerCheck))
							{
								return Call.Callee;
							}
							std::vector<std::string> SoftArgs;
							if ((TryParseSoftBuiltinType(Call.Callee, "optional", SoftArgs) || TryParseSoftBuiltinType(Call.Callee, "readonly", SoftArgs) || TryParseSoftBuiltinType(Call.Callee, "type", SoftArgs) || TryParseSoftBuiltinType(Call.Callee, "weak", SoftArgs) || (bHasStandardAttribute && TryParseSoftBuiltinType(Call.Callee, "attribute", SoftArgs))) && SoftArgs.size() == 1)
							{
								return Call.Callee;
							}
							std::vector<std::string> ScratchArgs;
							if (IsMulticastEventTypeName(Call.Callee) || TryParseTypeApplication(Call.Callee, "delegate", ScratchArgs))
							{
								return Call.Callee;
							}
						}
						if (!Call.TypeArgs.empty())
						{
							const std::string TypeApp = ReconstructTypeFromCall(Call);
							if (Runtime::IsDynamicArrayTypeName(TypeApp) || Runtime::IsMapTypeName(TypeApp) || Runtime::IsOrderedMapTypeName(TypeApp) || Runtime::IsSetTypeName(TypeApp) || Runtime::IsMultiSetTypeName(TypeApp) || Runtime::IsOrderedMultiSetTypeName(TypeApp) || Runtime::IsQueueTypeName(TypeApp) || Runtime::IsDequeTypeName(TypeApp))
							{
								return TypeApp;
							}
						}
						{
							const std::string EffectiveCallee =
							    ResolveAliasChain(ApplyTypeSubst(Call.Callee));
							if (Runtime::IsDynamicArrayTypeName(EffectiveCallee) || Runtime::IsMapTypeName(EffectiveCallee) || Runtime::IsOrderedMapTypeName(EffectiveCallee) || Runtime::IsSetTypeName(EffectiveCallee) || Runtime::IsMultiSetTypeName(EffectiveCallee) || Runtime::IsOrderedMultiSetTypeName(EffectiveCallee) || Runtime::IsQueueTypeName(EffectiveCallee) || Runtime::IsDequeTypeName(EffectiveCallee))
							{
								return EffectiveCallee;
							}
						}
						const auto TypeIt = UserTypes.find(Call.Callee);
						if (TypeIt != UserTypes.end())
						{
							return Call.TypeArgs.empty()
							    ? TypeIt->first
							    : ReconstructTypeFromCall(Call);
						}
						if (!Call.TypeArgs.empty() && OpenGenericTypes.find(Call.Callee) != OpenGenericTypes.end())
						{
							return ReconstructTypeFromCall(Call);
						}
						// Constructor callees can also contain a complete application,
						// including an empty argument list that selects type defaults.
						const std::string ConstructorType = ReconstructTypeFromCall(Call);
						if (const FUserTypeInfo* Type = ResolveUserTypeForLookup(ConstructorType))
						{
							return Type->bResolutionOnlyMonomorph ? ConstructorType : Type->Name;
						}

						if ((Call.Callee == "__std_spawn" || Call.Callee == "__std_spawn_task") && !Call.Arguments.empty() && ResolveAliasChain(ApplyTypeSubst(InferExprType(*Call.Arguments.front()))) != "string")
						{
							Fail("AOT: callable task entry is not supported by the C++ backend");
							return {};
						}
						std::vector<std::string> ArgTypes;
						ArgTypes.reserve(Call.Arguments.size());
						for (const std::unique_ptr<FExpr>& Arg : Call.Arguments)
						{
							ArgTypes.push_back(InferExprType(*Arg));
						}
						if (!Call.TypeArgs.empty())
						{
							std::vector<std::string> TypeArgs;
							if (const FFunctionDecl* Generic =
							        ResolveGenericOverload(Call.Callee, Call, TypeArgs))
							{
								std::unordered_map<std::string, std::string> Bindings;
								for (std::size_t Index = 0; Index < TypeArgs.size(); ++Index)
								{
									Bindings.emplace(Generic->TypeParams[Index], TypeArgs[Index]);
								}
								return SubstituteWithMap(Generic->ReturnType, Bindings);
							}
							return {};
						}
						if (const FFunctionDecl* Concrete =
						        ResolveConcreteOverload(Call.Callee, ArgTypes, &Call.Location))
						{
							return ApplyTypeSubst(Concrete->ReturnType);
						}
						{
							std::vector<std::string> TypeArgs;
							if (const FFunctionDecl* Generic =
							        ResolveGenericOverload(Call.Callee, Call, TypeArgs))
							{
								std::unordered_map<std::string, std::string> Bindings;
								for (std::size_t Index = 0; Index < TypeArgs.size(); ++Index)
								{
									Bindings.emplace(Generic->TypeParams[Index], TypeArgs[Index]);
								}
								return SubstituteWithMap(Generic->ReturnType, Bindings);
							}
						}
						// Keep the compiler's ordinary-call precedence: only after local,
						// free, generic and builtin resolution fails does an unqualified call
						// inside an instance body bind to implicit `this`.
						if (bInSelfContext && !SelfTypeName.empty())
						{
							const auto SelfIt = UserTypes.find(SelfTypeName);
							if (SelfIt != UserTypes.end())
							{
								if (const FMethodDecl* Method =
								        ResolveMethod(SelfIt->second, Call.Callee, ArgTypes))
								{
									return ResolveAliasChain(SubstForUserType(
									    SelfIt->second, Method->ReturnType));
								}
							}
						}
						return {};
					}
				case EExprKind::MethodCall:
					{
						const auto& Call = static_cast<const FMethodCallExpr&>(Expr);
						std::vector<std::string> ArgTypes;
						ArgTypes.reserve(Call.Arguments.size());
						for (const std::unique_ptr<FExpr>& Arg : Call.Arguments)
						{
							ArgTypes.push_back(InferExprType(*Arg));
						}
						std::string NamespaceCallName;
						if (Call.Object && TryBuildNamespaceCallName(*Call.Object, Call.Method, NamespaceCallName))
						{
							if (const FFunctionDecl* Concrete = ResolveConcreteCallOverload(
							        NamespaceCallName, Call, ArgTypes, &Call.Location))
							{
								return ResolveAliasChain(ApplyTypeSubst(Concrete->ReturnType));
							}
							std::vector<std::string> TypeArgs;
							if (const FFunctionDecl* Generic = ResolveGenericOverloadFromTypes(
							        NamespaceCallName,
							        Call.TypeArgs,
							        ArgTypes,
							        TypeArgs,
							        &Call.Location))
							{
								std::unordered_map<std::string, std::string> Bindings;
								for (std::size_t Index = 0; Index < TypeArgs.size(); ++Index)
								{
									Bindings.emplace(Generic->TypeParams[Index], TypeArgs[Index]);
								}
								return ResolveAliasChain(
								    SubstituteWithMap(Generic->ReturnType, Bindings));
							}
						}
						const std::string ObjectType = InferExprType(*Call.Object);
						std::string ContainerReturn;
						if (TryInferContainerMethodReturnType(
						        ObjectType,
						        Call.Method,
						        Call.Arguments.size(),
						        ContainerReturn))
						{
							return ContainerReturn;
						}
						std::string EventReturn;
						if (TryInferEventMethodReturnType(ObjectType, Call.Method, EventReturn))
						{
							return EventReturn;
						}
						std::vector<std::string> AttributeArgs;
						if (bHasStandardAttribute &&
						    TryParseSoftBuiltinType(ObjectType, "attribute", AttributeArgs) &&
						    AttributeArgs.size() == 1)
						{
							if (Call.Method == "get" || Call.Method == "get_or" ||
							    Call.Method == "operator()")
							{
								return AttributeArgs[0];
							}
							if (Call.Method == "try_get")
							{
								return Call.GetArguments().empty() ? "optional<" + AttributeArgs[0] + ">" : "bool";
							}
							if (Call.Method == "is_set" || Call.Method == "is_bound" ||
							    Call.Method == "has_same_configuration")
							{
								return "bool";
							}
							return "void";
						}
						std::vector<std::string> SoftArgs;
						if (TryParseSoftBuiltinType(ObjectType, "optional", SoftArgs) && SoftArgs.size() == 1)
						{
							if (Call.Method == "has_value")
							{
								return "bool";
							}
							if (Call.Method == "get" || Call.Method == "value_or")
							{
								return SoftArgs[0];
							}
							if (Call.Method == "reset")
							{
								return "void";
							}
						}
						if (TryParseSoftBuiltinType(ObjectType, "readonly", SoftArgs) && SoftArgs.size() == 1 && Call.Method == "get")
						{
							return SoftArgs[0];
						}
						if (TryParseSoftBuiltinType(ObjectType, "weak", SoftArgs) && SoftArgs.size() == 1)
						{
							if (Call.Method == "lock")
							{
								return SoftArgs[0];
							}
							if (Call.Method == "expired")
							{
								return "bool";
							}
							if (Call.Method == "reset")
							{
								return "void";
							}
						}
						if (ActiveOpenTypeParams.find(ObjectType) != ActiveOpenTypeParams.end())
						{
							const std::string BoundReturn =
							    BoundMethodReturnType(ObjectType, Call.Method, ArgTypes);
							if (!BoundReturn.empty())
							{
								return BoundReturn;
							}
						}
						if (const FUserTypeInfo* Type = ResolveUserTypeForLookup(ObjectType))
						{
							const FMethodDecl* Method =
							    ResolveMethod(*Type, Call.Method, ArgTypes);
							if (Method)
							{
								return ResolveAliasChain(
								    SubstForUserType(*Type, Method->ReturnType));
							}
						}
						std::vector<std::string> ExtensionTypes;
						bool bGenericExtension = false;
						if (const FFunctionDecl* Extension = ResolveExtensionOverload(
						        Call, ObjectType, ExtensionTypes, bGenericExtension))
						{
							if (!bGenericExtension)
							{
								return ResolveAliasChain(ApplyTypeSubst(Extension->ReturnType));
							}
							std::unordered_map<std::string, std::string> Bindings;
							for (std::size_t Index = 0; Index < ExtensionTypes.size(); ++Index)
							{
								Bindings.emplace(Extension->TypeParams[Index], ExtensionTypes[Index]);
							}
							return ResolveAliasChain(
							    SubstituteWithMap(Extension->ReturnType, Bindings));
						}
						return {};
					}
				case EExprKind::IndexAccess:
					{
						const auto& IndexExpr = static_cast<const FIndexAccessExpr&>(Expr);
						const std::string ObjectType = InferExprType(*IndexExpr.Object);
						std::string ElementType;
						if (Runtime::ParseDynamicArrayElementType(ObjectType, ElementType))
						{
							return ElementType;
						}
						std::uint32_t NativeLength = 0;
						if (Runtime::ParseNativeArrayType(ObjectType, ElementType, NativeLength))
						{
							return ElementType;
						}
						std::string MapKeyType;
						std::string MapValueType;
						if (Runtime::ParseMapType(ObjectType, MapKeyType, MapValueType) || Runtime::ParseOrderedMapType(ObjectType, MapKeyType, MapValueType))
						{
							return MapValueType;
						}
						if (IsStringType(ObjectType))
						{
							return "int";
						}
						const auto ObjectTypeIt = UserTypes.find(ObjectType);
						if (ObjectTypeIt != UserTypes.end())
						{
							const std::vector<std::string> ArgTypes{
							    InferExprType(*IndexExpr.Index)};
							const FMethodDecl* Method =
							    ResolveMethod(ObjectTypeIt->second, "operator[]", ArgTypes);
							return Method ? Method->ReturnType : std::string{};
						}
						return {};
					}
				case EExprKind::ArrayInit:
					{
						const auto& Init = static_cast<const FArrayInitExpr&>(Expr);
						return Init.TypeName.empty()
						    ? std::string{}
						    : ResolveAliasChain(ApplyTypeSubst(Init.TypeName));
					}
				case EExprKind::MapInit:
					{
						const auto& Init = static_cast<const FMapInitExpr&>(Expr);
						return Init.TypeName.empty()
						    ? std::string{}
						    : ResolveAliasChain(ApplyTypeSubst(Init.TypeName));
					}
				case EExprKind::Lambda:
					{
						const auto& Lambda = static_cast<const FLambdaExpr&>(Expr);
						std::vector<std::string> ParamTypeNames;
						ParamTypeNames.reserve(Lambda.Parameters.size());
						for (const FFunctionParam& Param : Lambda.Parameters)
						{
							if (Param.TypeName.empty())
							{
								// Needs ExpressionExpectedType / call-site hint.
								return {};
							}
							auto Type = Param.TypeName;
							if (Param.bIsRef && !Runtime::IsReferenceTypeName(Type))
							{
								Type = (Param.bIsConst ? "const ref " : "ref ") + Type;
							}
							ParamTypeNames.push_back(std::move(Type));
						}
						if (Lambda.ReturnType.empty())
						{
							return {};
						}
						return Runtime::MakeFunctionTypeName(ParamTypeNames, Lambda.ReturnType);
					}
				default:
					return {};
				}
			}

			std::string ResolveVariableType(const FVarDeclStmt& Var)
			{
				std::string Type = ApplyTypeSubst(Var.TypeName);
				if (Type == "auto" && Var.Initializer && OpenGenericTypes.find("handle") == OpenGenericTypes.end())
				{
					const std::string Inferred = ResolveAliasChain(
					    ApplyTypeSubst(InferExprType(*Var.Initializer)));
					std::string Payload;
					if (TryParseHandleType(Inferred, Payload) || (Inferred == "handle" && HasNonGenericHandleType()))
					{
						Type = Inferred;
					}
				}
				return Type;
			}

			void NoteMemberOperatorArgumentBox(
			    const FExpr& Receiver, const FExpr& Argument, const char* OperatorName)
			{
				if (!OperatorName) return;
				const std::string OwnerName = InferExprType(Receiver);
				const std::string ArgumentType = InferExprType(Argument);
				const FUserTypeInfo* Owner = ResolveUserTypeForLookup(OwnerName);
				if (!Owner) return;
				const FUserTypeInfo* DeclaringType = nullptr;
				const FMethodDecl* Method = ResolveMethod(*Owner, OperatorName, {ArgumentType}, &DeclaringType);
				if (Method && Method->Parameters.size() == 1)
				{
					NoteStructInterfaceBox(SubstForUserType(*DeclaringType, Method->Parameters.front().TypeName), ArgumentType);
				}
			}

			void CollectSpecializationsInExpr(const FExpr& Expr)
			{
				if (bFailed)
				{
					return;
				}
				switch (Expr.Kind)
				{
				case EExprKind::Ref:
					{
						CollectSpecializationsInExpr(
						    *static_cast<const FRefExpr&>(Expr).Operand);
						return;
					}
				case EExprKind::Unary:
					{
						CollectSpecializationsInExpr(*static_cast<const FUnaryExpr&>(Expr).Operand);
						return;
					}
				case EExprKind::Binary:
					{
						const auto& Binary = static_cast<const FBinaryExpr&>(Expr);
						CollectSpecializationsInExpr(*Binary.Left);
						CollectSpecializationsInExpr(*Binary.Right);
						const auto NoteArguments = [&](const char* Name)
						{
							NoteMemberOperatorArgumentBox(*Binary.Left, *Binary.Right, Name);
							NoteMemberOperatorArgumentBox(*Binary.Right, *Binary.Left, Name);
						};
						NoteArguments(BinaryOpOperatorName(Binary.Op));
						if (Binary.Op == EBinaryOp::NotEqual)
						{
							NoteArguments("operator==");
						}
						if (const std::optional<EBinaryOp> Mirror = MirrorBinaryOp(Binary.Op))
						{
							NoteArguments(BinaryOpOperatorName(*Mirror));
						}
						return;
					}
				case EExprKind::Assign:
					{
						const auto& Assign = static_cast<const FAssignExpr&>(Expr);
						CollectSpecializationsInExpr(*Assign.Target);
						CollectSpecializationsInExpr(*Assign.Value);
						// Validate the concrete target while generic substitutions are
						// available; the emitted open template alone only sees T.
						if ((Assign.bFromPrefixIncDec || Assign.bFromPostfixIncDec) && !CheckIncDecTarget(Assign))
						{
							return;
						}
						if (Assign.bIsCompound)
						{
							NoteMemberOperatorArgumentBox(*Assign.Target, *Assign.Value, CompoundOpOperatorName(Assign.CompoundOp));
						}
						return;
					}
				case EExprKind::Conditional:
					{
						const auto& Cond = static_cast<const FConditionalExpr&>(Expr);
						CollectSpecializationsInExpr(*Cond.Condition);
						CollectSpecializationsInExpr(*Cond.ThenExpr);
						CollectSpecializationsInExpr(*Cond.ElseExpr);
						return;
					}
				case EExprKind::Call:
					{
						const auto& Call = static_cast<const FCallExpr&>(Expr);
						const std::string TypeLookupCallee =
						    ResolveAliasChain(ApplyTypeSubst(Call.Callee));
						if (Call.CalleeExpr)
						{
							CollectSpecializationsInExpr(*Call.CalleeExpr);
						}
						for (const std::unique_ptr<FExpr>& Arg : Call.Arguments)
						{
							CollectSpecializationsInExpr(*Arg);
						}
						// cast<T>/try_cast<T> are language intrinsics, not generic
						// functions requiring monomorph collection. Their argument still
						// participates in collection through the loop above.
						if (Call.Callee == "__numeric_type") return;
						if ((Call.Callee == "cast" || Call.Callee == "try_cast" || Call.Callee == "const_cast") && !Call.TypeArgs.empty())
						{
							return;
						}
						// Register constructor applications before emitting declarations
						// and definitions, including defaults encoded in the callee name.
						if (Call.TypeArgs.empty() && TypeLookupCallee.find('<') != std::string::npos && !TypeLookupCallee.empty() && TypeLookupCallee.back() == '>' && ResolveUserTypeForLookup(TypeLookupCallee))
						{
							return;
						}
						if (Call.Callee.empty() || Call.Callee == "print" || Call.Callee == "len" || Call.Callee == "to_string" || Call.Callee == "to_int" || Call.Callee == "to_int64" || Call.Callee == "to_uint64" || Call.Callee == "to_double" || Call.Callee == "to_float" || Call.Callee == "assert" || Call.Callee == "printf" || Call.Callee == "format" || Call.Callee == "formatf" || Call.Callee == "handle" || IsEventTypeName(Call.Callee) || UserTypes.find(TypeLookupCallee) != UserTypes.end() || OpenGenericTypes.find(TypeLookupCallee) != OpenGenericTypes.end())
						{
							return;
						}
						{
							std::string HandleInnerCheck;
							if (TryParseHandleType(Call.Callee, HandleInnerCheck))
							{
								return;
							}
						}
						if (!Call.TypeArgs.empty())
						{
							const std::string TypeApp = ReconstructTypeFromCall(Call);
							if (!TypeApp.empty() && (Runtime::IsDynamicArrayTypeName(TypeApp) || Runtime::IsMapTypeName(TypeApp) || Runtime::IsOrderedMapTypeName(TypeApp) || Runtime::IsSetTypeName(TypeApp) || Runtime::IsMultiSetTypeName(TypeApp) || Runtime::IsOrderedMultiSetTypeName(TypeApp) || Runtime::IsQueueTypeName(TypeApp) || Runtime::IsDequeTypeName(TypeApp)))
							{
								return;
							}
						}
						{
							const std::string EffectiveCallee =
							    ResolveAliasChain(ApplyTypeSubst(Call.Callee));
							if (Runtime::IsDynamicArrayTypeName(EffectiveCallee) || Runtime::IsMapTypeName(EffectiveCallee) || Runtime::IsOrderedMapTypeName(EffectiveCallee) || Runtime::IsSetTypeName(EffectiveCallee) || Runtime::IsMultiSetTypeName(EffectiveCallee) || Runtime::IsOrderedMultiSetTypeName(EffectiveCallee) || Runtime::IsQueueTypeName(EffectiveCallee) || Runtime::IsDequeTypeName(EffectiveCallee))
							{
								return;
							}
						}
						std::vector<std::string> ArgTypes;
						ArgTypes.reserve(Call.Arguments.size());
						for (const std::unique_ptr<FExpr>& Arg : Call.Arguments)
						{
							ArgTypes.push_back(InferExprType(*Arg));
						}

						const FExprArgumentView EffectiveArguments = Call.GetArguments();
						if ((Call.Callee == "__std_spawn" || Call.Callee == "__std_spawn_task") && !EffectiveArguments.empty() && ResolveAliasChain(ApplyTypeSubst(InferExprType(*EffectiveArguments[0]))) != "string")
						{
							Fail("AOT: callable task entry is not supported by the C++ backend");
							return;
						}
						if (IsMethodBindingFactoryCall(Call) || IsWeakAttributeFactoryCall(Call)) return;
						// Explicit type args always select a specialization path.
						if (!Call.TypeArgs.empty())
						{
							std::vector<std::string> TypeArgs;
							if (const FFunctionDecl* Generic =
							        ResolveGenericOverload(Call.Callee, Call, TypeArgs))
							{
								FindOrCreateSpecialization(*Generic, TypeArgs);
							}
							else
							{
								Fail(
								    "AOT: unresolved generic call '" + Call.Callee + "' with explicit type arguments");
							}
							return;
						}

						if (ResolveConcreteOverload(
						        Call.Callee, ArgTypes, &Call.Location) != nullptr)
						{
							return;
						}
						{
							std::vector<std::string> TypeArgs;
							if (const FFunctionDecl* Generic =
							        ResolveGenericOverload(Call.Callee, Call, TypeArgs))
							{
								FindOrCreateSpecialization(*Generic, TypeArgs);
							}
						}
						return;
					}
				case EExprKind::MethodCall:
					{
						const auto& Call = static_cast<const FMethodCallExpr&>(Expr);
						CollectSpecializationsInExpr(*Call.Object);
						for (const std::unique_ptr<FExpr>& Arg : Call.Arguments)
						{
							CollectSpecializationsInExpr(*Arg);
						}
						const std::string ObjectType = InferExprType(*Call.Object);
						std::vector<std::string> ArgTypes;
						ArgTypes.reserve(Call.Arguments.size());
						for (const std::unique_ptr<FExpr>& Arg : Call.Arguments)
						{
							ArgTypes.push_back(InferExprType(*Arg));
						}
						std::string NamespaceCallName;
						if (Call.Object && TryBuildNamespaceCallName(*Call.Object, Call.Method, NamespaceCallName))
						{
							std::vector<std::string> TypeArgs;
							if (const FFunctionDecl* Generic = ResolveGenericOverloadFromTypes(
							        NamespaceCallName,
							        Call.TypeArgs,
							        ArgTypes,
							        TypeArgs,
							        &Call.Location))
							{
								FindOrCreateSpecialization(*Generic, TypeArgs);
							}
							return;
						}
						std::string ExistingReturn;
						bool bHasRealMethod = TryInferContainerMethodReturnType(
						                          ObjectType,
						                          Call.Method,
						                          Call.Arguments.size(),
						                          ExistingReturn) ||
						    TryInferEventMethodReturnType(ObjectType, Call.Method, ExistingReturn);
						std::vector<std::string> SoftArgs;
						if (!bHasRealMethod && TryParseSoftBuiltinType(ObjectType, "optional", SoftArgs) && SoftArgs.size() == 1)
						{
							bHasRealMethod = Call.Method == "has_value" || Call.Method == "get" || Call.Method == "value_or" || Call.Method == "reset";
						}
						SoftArgs.clear();
						if (!bHasRealMethod && TryParseSoftBuiltinType(ObjectType, "weak", SoftArgs) && SoftArgs.size() == 1)
						{
							bHasRealMethod = Call.Method == "lock" || Call.Method == "expired" || Call.Method == "reset";
						}
						if (!bHasRealMethod && ActiveOpenTypeParams.find(ObjectType) != ActiveOpenTypeParams.end())
						{
							bHasRealMethod = !BoundMethodReturnType(
							    ObjectType, Call.Method, ArgTypes)
							                      .empty();
						}
						const auto TypeIt = UserTypes.find(ObjectType);
						if (!bHasRealMethod && TypeIt != UserTypes.end())
						{
							bHasRealMethod = ResolveMethod(
							                     TypeIt->second, Call.Method, ArgTypes) != nullptr;
						}
						if (!bHasRealMethod)
						{
							std::vector<std::string> ExtensionTypes;
							bool bGenericExtension = false;
							if (const FFunctionDecl* Extension = ResolveExtensionOverload(
							        Call, ObjectType, ExtensionTypes, bGenericExtension);
							    Extension && bGenericExtension)
							{
								FindOrCreateSpecialization(*Extension, ExtensionTypes);
							}
						}
						return;
					}
				case EExprKind::MemberAccess:
					{
						CollectSpecializationsInExpr(
						    *static_cast<const FMemberAccessExpr&>(Expr).Object);
						return;
					}
				case EExprKind::IndexAccess:
					{
						const auto& Access = static_cast<const FIndexAccessExpr&>(Expr);
						CollectSpecializationsInExpr(*Access.Object);
						CollectSpecializationsInExpr(*Access.Index);
						NoteMemberOperatorArgumentBox(*Access.Object, *Access.Index, "operator[]");
						return;
					}
				case EExprKind::ArrayInit:
					{
						const auto& Init = static_cast<const FArrayInitExpr&>(Expr);
						for (const std::unique_ptr<FExpr>& Element : Init.Elements)
						{
							CollectSpecializationsInExpr(*Element);
						}
						return;
					}
				case EExprKind::MapInit:
					{
						const auto& Init = static_cast<const FMapInitExpr&>(Expr);
						for (const FMapInitEntry& Entry : Init.Entries)
						{
							if (Entry.Key)
							{
								CollectSpecializationsInExpr(*Entry.Key);
							}
							if (Entry.Value)
							{
								CollectSpecializationsInExpr(*Entry.Value);
							}
						}
						return;
					}
				case EExprKind::Lambda:
					{
						const auto& Lambda = static_cast<const FLambdaExpr&>(Expr);
						const auto SavedLocals = LocalTypes;
						const auto SavedConstness = LocalConstness;
						std::vector<std::pair<std::string, std::string>> InitializedTypes;
						for (const auto& Capture : Lambda.Captures)
						{
							if (!Capture.Initializer)
							{
								continue;
							}
							// Every initializer is evaluated in the outer scope, before
							// introducing capture names or lambda parameters.
							CollectSpecializationsInExpr(*Capture.Initializer);
							InitializedTypes.emplace_back(Capture.Name, std::string(Runtime::StripReferenceTypeName(ResolveAliasChain(ApplyTypeSubst(InferExprType(*Capture.Initializer))))));
						}
						for (const auto& Binding : InitializedTypes)
						{
							LocalTypes[Binding.first] = Binding.second;
							LocalConstness[Binding.first] = !Lambda.bMutable;
						}
						for (const FFunctionParam& Param : Lambda.Parameters)
						{
							LocalTypes[Param.Name] = Param.TypeName;
							LocalConstness[Param.Name] = Param.bIsConst;
						}
						if (Lambda.Body)
						{
							CollectSpecializationsInStmt(*Lambda.Body);
						}
						if (Lambda.ExpressionBody)
						{
							CollectSpecializationsInExpr(*Lambda.ExpressionBody);
						}
						LocalTypes = SavedLocals;
						LocalConstness = SavedConstness;
						return;
					}
				default:
					return;
				}
			}

			void CollectSpecializationsInStmt(const FStmt& Stmt)
			{
				if (bFailed)
				{
					return;
				}
				switch (Stmt.Kind)
				{
				case EStmtKind::Block:
					{
						TValueScopeRestore<decltype(LocalTypes)> LocalScope(LocalTypes);
						TValueScopeRestore<decltype(LocalConstness)> ConstScope(LocalConstness);
						TValueScopeRestore<decltype(TypeAliases)> AliasScope(TypeAliases);
						for (const std::unique_ptr<FStmt>& Child :
						    static_cast<const FBlockStmt&>(Stmt).Statements)
						{
							if (Child)
							{
								CollectSpecializationsInStmt(*Child);
							}
						}
						return;
					}
				case EStmtKind::Expr:
					{
						if (static_cast<const FExprStmt&>(Stmt).Expression)
						{
							CollectSpecializationsInExpr(
							    *static_cast<const FExprStmt&>(Stmt).Expression);
						}
						return;
					}
				case EStmtKind::Return:
					{
						if (static_cast<const FReturnStmt&>(Stmt).Value)
						{
							CollectSpecializationsInExpr(
							    *static_cast<const FReturnStmt&>(Stmt).Value);
						}
						return;
					}
				case EStmtKind::Throw:
					{
						if (static_cast<const FThrowStmt&>(Stmt).Value)
						{
							CollectSpecializationsInExpr(
							    *static_cast<const FThrowStmt&>(Stmt).Value);
						}
						return;
					}
				case EStmtKind::Try:
					{
						const auto& TryStmt = static_cast<const FTryStmt&>(Stmt);
						if (TryStmt.TryBlock)
						{
							CollectSpecializationsInStmt(*TryStmt.TryBlock);
						}
						for (const FCatchClause& Clause : TryStmt.Catches)
						{
							TValueScopeRestore<decltype(LocalTypes)> LocalScope(LocalTypes);
							TValueScopeRestore<decltype(LocalConstness)> ConstScope(LocalConstness);
							TValueScopeRestore<decltype(TypeAliases)> AliasScope(TypeAliases);
							if (!Clause.bCatchAll)
							{
								(void)MapType(ApplyTypeSubst(Clause.TypeName));
								if (!Clause.Name.empty())
								{
									LocalTypes[Clause.Name] =
									    ResolveAliasChain(ApplyTypeSubst(Clause.TypeName));
									LocalConstness[Clause.Name] = Clause.bIsConst;
								}
							}
							if (Clause.Body)
							{
								CollectSpecializationsInStmt(*Clause.Body);
							}
						}
						if (TryStmt.FinallyBlock)
						{
							CollectSpecializationsInStmt(*TryStmt.FinallyBlock);
						}
						return;
					}
				case EStmtKind::VarDecl:
					{
						const auto& Var = static_cast<const FVarDeclStmt&>(Stmt);
						const std::string Resolved = ResolveVariableType(Var);
						(void)MapType(Resolved);
						std::string LocalType = ResolveAliasChain(Resolved);
						{
							const std::string::size_type Lt = LocalType.find('<');
							if (Lt != std::string::npos)
							{
								const std::string Base = LocalType.substr(0, Lt);
								if (OpenGenericTypes.find(Base) != OpenGenericTypes.end())
								{
									std::vector<std::string> Args;
									const std::string Full = LocalType;
									SplitTopLevelTypeArgs(
									    Full.substr(Lt + 1, Full.size() - Lt - 2),
									    Args);
									const std::string Mono = EnsureTypeMonomorph(Base, Args);
									if (!Mono.empty())
									{
										LocalType = Mono;
									}
								}
							}
						}
						std::string InitializerType;
						if (Var.Initializer)
						{
							CollectSpecializationsInExpr(*Var.Initializer);
							InitializerType = InferExprType(*Var.Initializer);
						}
						LocalTypes[Var.Name] = LocalType;
						LocalConstness[Var.Name] = Var.bIsConst;
						if (Var.Initializer)
						{
							NoteStructInterfaceBox(LocalType, InitializerType);
						}
						return;
					}
				case EStmtKind::VarDeclGroup:
					{
						const auto& Group = static_cast<const FVarDeclGroupStmt&>(Stmt);
						for (const std::unique_ptr<FVarDeclStmt>& Declaration :
						    Group.Declarations)
						{
							if (Declaration)
							{
								CollectSpecializationsInStmt(*Declaration);
							}
						}
						return;
					}
				case EStmtKind::If:
					{
						const auto& IfStmt = static_cast<const FIfStmt&>(Stmt);
						CollectSpecializationsInExpr(*IfStmt.Condition);
						if (IfStmt.ThenBranch)
						{
							TValueScopeRestore<decltype(LocalTypes)> LocalScope(LocalTypes);
							TValueScopeRestore<decltype(LocalConstness)> ConstScope(LocalConstness);
							TValueScopeRestore<decltype(TypeAliases)> AliasScope(TypeAliases);
							CollectSpecializationsInStmt(*IfStmt.ThenBranch);
						}
						if (IfStmt.ElseBranch)
						{
							TValueScopeRestore<decltype(LocalTypes)> LocalScope(LocalTypes);
							TValueScopeRestore<decltype(LocalConstness)> ConstScope(LocalConstness);
							TValueScopeRestore<decltype(TypeAliases)> AliasScope(TypeAliases);
							CollectSpecializationsInStmt(*IfStmt.ElseBranch);
						}
						return;
					}
				case EStmtKind::While:
					{
						const auto& WhileStmt = static_cast<const FWhileStmt&>(Stmt);
						CollectSpecializationsInExpr(*WhileStmt.Condition);
						if (WhileStmt.Body)
						{
							TValueScopeRestore<decltype(LocalTypes)> LocalScope(LocalTypes);
							TValueScopeRestore<decltype(LocalConstness)> ConstScope(LocalConstness);
							TValueScopeRestore<decltype(TypeAliases)> AliasScope(TypeAliases);
							CollectSpecializationsInStmt(*WhileStmt.Body);
						}
						return;
					}
				case EStmtKind::DoWhile:
					{
						const auto& DoWhileStmt = static_cast<const FDoWhileStmt&>(Stmt);
						if (DoWhileStmt.Body)
						{
							CollectSpecializationsInStmt(*DoWhileStmt.Body);
						}
						if (DoWhileStmt.Condition)
						{
							CollectSpecializationsInExpr(*DoWhileStmt.Condition);
						}
						return;
					}
				case EStmtKind::For:
					{
						TValueScopeRestore<decltype(LocalTypes)> LocalScope(LocalTypes);
						TValueScopeRestore<decltype(LocalConstness)> ConstScope(LocalConstness);
						TValueScopeRestore<decltype(TypeAliases)> AliasScope(TypeAliases);
						const auto& ForStmt = static_cast<const FForStmt&>(Stmt);
						if (ForStmt.Init)
						{
							CollectSpecializationsInStmt(*ForStmt.Init);
						}
						for (const std::unique_ptr<FExpr>& InitExpression :
						    ForStmt.InitExpressions)
						{
							if (InitExpression)
							{
								CollectSpecializationsInExpr(*InitExpression);
							}
						}
						if (ForStmt.Condition)
						{
							CollectSpecializationsInExpr(*ForStmt.Condition);
						}
						if (ForStmt.Step)
						{
							CollectSpecializationsInExpr(*ForStmt.Step);
						}
						for (const std::unique_ptr<FExpr>& StepExpression :
						    ForStmt.StepExpressions)
						{
							if (StepExpression)
							{
								CollectSpecializationsInExpr(*StepExpression);
							}
						}
						if (ForStmt.Body)
						{
							CollectSpecializationsInStmt(*ForStmt.Body);
						}
						return;
					}
				case EStmtKind::ForEach:
					{
						TValueScopeRestore<decltype(LocalTypes)> LocalScope(LocalTypes);
						TValueScopeRestore<decltype(LocalConstness)> ConstScope(LocalConstness);
						TValueScopeRestore<decltype(TypeAliases)> AliasScope(TypeAliases);
						const auto& ForEachStmt = static_cast<const FForEachStmt&>(Stmt);
						if (ForEachStmt.Collection)
						{
							CollectSpecializationsInExpr(*ForEachStmt.Collection);
						}
						std::string ElementType = ForEachStmt.ElementTypeName;
						std::string KeyType = ForEachStmt.KeyTypeName;
						if ((ElementType == "auto" || (ForEachStmt.bHasKeyBinding && KeyType == "auto")) && ForEachStmt.Collection)
						{
							std::string CollType = InferExprType(*ForEachStmt.Collection);
							CollType = ResolveAliasChain(ApplyTypeSubst(CollType));
							std::string ParsedElement;
							std::uint32_t NativeLength = 0;
							if (Runtime::ParseDynamicArrayElementType(CollType, ParsedElement))
							{
								if (ElementType == "auto")
								{
									ElementType = ParsedElement;
								}
							}
							else if (Runtime::ParseNativeArrayType(CollType, ParsedElement, NativeLength))
							{
								if (ElementType == "auto")
								{
									ElementType = ParsedElement;
								}
							}
							else
							{
								std::string ParsedKey;
								if (Runtime::ParseMapType(CollType, ParsedKey, ParsedElement) || Runtime::ParseOrderedMapType(CollType, ParsedKey, ParsedElement))
								{
									if (ElementType == "auto")
									{
										ElementType = ParsedElement;
									}
									if (KeyType == "auto")
									{
										KeyType = ParsedKey;
									}
								}
							}
						}
						LocalTypes[ForEachStmt.ElementName] = ApplyTypeSubst(ElementType);
						LocalConstness[ForEachStmt.ElementName] = ForEachStmt.bIsConst;
						if (ForEachStmt.bHasKeyBinding)
						{
							LocalTypes[ForEachStmt.KeyName] = ApplyTypeSubst(KeyType);
							LocalConstness[ForEachStmt.KeyName] = ForEachStmt.bKeyIsConst;
						}
						if (ForEachStmt.Body)
						{
							CollectSpecializationsInStmt(*ForEachStmt.Body);
						}
						return;
					}
				case EStmtKind::Switch:
					{
						const auto& SwitchStmt = static_cast<const FSwitchStmt&>(Stmt);
						if (SwitchStmt.Discriminant)
						{
							CollectSpecializationsInExpr(*SwitchStmt.Discriminant);
						}
						for (const FSwitchCase& Case : SwitchStmt.Cases)
						{
							for (const std::unique_ptr<FExpr>& Label : Case.Labels)
							{
								if (Label)
								{
									CollectSpecializationsInExpr(*Label);
								}
							}
							if (Case.Body)
							{
								CollectSpecializationsInStmt(*Case.Body);
							}
						}
						return;
					}
				case EStmtKind::TypeAlias:
					{
						const auto& Alias = static_cast<const FTypeAliasStmt&>(Stmt);
						TypeAliases[Alias.Name] = Alias.TypeName;
						return;
					}
				default:
					return;
				}
			}
