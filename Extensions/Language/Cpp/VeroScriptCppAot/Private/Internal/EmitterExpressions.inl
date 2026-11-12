// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

			bool CheckIncDecTarget(const FAssignExpr& Assign)
			{
				const std::string TargetType = ResolveAliasChain(
				    ApplyTypeSubst(InferExprType(*Assign.Target)));
				if (ActiveOpenTypeParams.find(TargetType) != ActiveOpenTypeParams.end())
				{
					// Concrete specializations were checked during collection.
					return true;
				}
				const std::string BaseType = TargetType.substr(0, TargetType.find('<'));
				if (UserTypes.find(TargetType) != UserTypes.end() || OpenGenericTypes.find(BaseType) != OpenGenericTypes.end())
				{
					Fail("AOT: overloaded increment/decrement is not supported; use an explicit update method");
					return false;
				}
				return true;
			}

			void EmitIncDecAssign(const FAssignExpr& Assign)
			{
				const bool bIncrement = Assign.CompoundOp == EBinaryOp::Add;
				const bool bDecrement = Assign.CompoundOp == EBinaryOp::Subtract;
				if (!bIncrement && !bDecrement)
				{
					Fail("AOT: unsupported increment/decrement");
					return;
				}
				if (!CheckIncDecTarget(Assign))
				{
					return;
				}
				// The reference argument evaluates the target once and keeps temporary
				// array/object owners alive for the full call. The helper also checks
				// concrete scalar widths when this expression is inside an open template.
				Out << "::VsCheckedIncDec<" << (bIncrement ? "true" : "false") << ", "
				    << (Assign.bFromPostfixIncDec ? "true" : "false") << ">(";
				EmitExpr(*Assign.Target);
				Out << ')';
			}

			void EmitExpr(const FExpr& Expr, const std::string* InitTypeHint = nullptr)
			{
				if (bFailed)
				{
					return;
				}
				if (const auto Saved = RetainedReferenceReceivers.find(&Expr); Saved != RetainedReferenceReceivers.end())
				{
					Out << Saved->second;
					return;
				}
				if (TryEmitSequencedOperand(Expr,
				        [&]
				{
					EmitExpr(Expr, InitTypeHint);
				}))
				{
					return;
				}
				if ((Expr.Kind == EExprKind::Call || Expr.Kind == EExprKind::MethodCall) &&
				    TryEmitSequencedExpression(Expr, InitTypeHint))
				{
					return;
				}

				switch (Expr.Kind)
				{
				case EExprKind::CharacterLiteral:
					{
						Out << "static_cast<std::int8_t>("
						    << static_cast<int>(
						           static_cast<const FCharacterLiteralExpr&>(Expr).Value)
						    << ')';
						return;
					}
				case EExprKind::IntegerLiteral:
					{
						const auto& Lit = static_cast<const FIntegerLiteralExpr&>(Expr);
						if (Lit.bIs64 && Lit.bUnsigned)
						{
							Out << "static_cast<std::uint64_t>(" << Lit.UnsignedValue64 << "ull)";
						}
						else if (Lit.bIs64)
						{
							if (Lit.Value64 == std::numeric_limits<std::int64_t>::min())
							{
								Out << "(std::numeric_limits<std::int64_t>::min)()";
							}
							else
							{
								Out << "static_cast<std::int64_t>(" << Lit.Value64 << "ll)";
							}
						}
						else if (Lit.bUnsigned)
						{
							Out << Lit.UnsignedValue << "u";
						}
						else
						{
							Out << Lit.Value;
						}
						return;
					}
				case EExprKind::FloatLiteral:
					{
						const auto& FloatLiteral = static_cast<const FFloatLiteralExpr&>(Expr);
						// ostream's default formatting turns 0.0 into "0".  That is not
						// merely cosmetic here: a unary minus would then generate the
						// integer expression -0 and lose the IEEE negative-zero sign
						// before conversion back to double.  Emit a genuine double
						// literal and retain enough digits for an exact round trip.
						std::ostringstream Literal;
						Literal << std::setprecision(FloatLiteral.bFloat32
						                   ? std::numeric_limits<float>::max_digits10
						                   : std::numeric_limits<double>::max_digits10)
						        << (FloatLiteral.bFloat32
						                   ? static_cast<double>(static_cast<float>(FloatLiteral.Value))
						                   : FloatLiteral.Value);
						std::string Text = Literal.str();
						if (Text.find_first_of(".eE") == std::string::npos)
						{
							Text += ".0";
						}
						Out << Text;
						if (FloatLiteral.bFloat32)
						{
							Out << 'f';
						}
						return;
					}
				case EExprKind::BooleanLiteral:
					{
						Out << (static_cast<const FBooleanLiteralExpr&>(Expr).Value ? "true" : "false");
						return;
					}
				case EExprKind::StringLiteral:
					{
						const auto& Value = static_cast<const FStringLiteralExpr&>(Expr).Value;
						Out << "std::string(\"" << EscapeCppString(Value) << '"';
						if (Value.find('\0') != std::string::npos)
						{
							Out << ", " << Value.size();
						}
						Out << ')';
						return;
					}
				case EExprKind::WStringLiteral:
					{
						const auto& Value = static_cast<const FWStringLiteralExpr&>(Expr).Value;
						Out << "std::wstring(" << EscapeCppWStringLiteral(Value);
						if (Value.find(u'\0') != std::u16string::npos)
						{
							Out << ", " << Value.size();
						}
						Out << ')';
						return;
					}
				case EExprKind::NullptrLiteral:
					{
						Out << "nullptr";
						return;
					}
				case EExprKind::Ref:
					{
						EmitExpr(*static_cast<const FRefExpr&>(Expr).Operand, InitTypeHint);
						return;
					}
				case EExprKind::This:
					{
						if (!bInSelfContext)
						{
							Fail("AOT: 'this' outside method/constructor body");
							return;
						}
						if (CapturedClassThisLambdaDepth != 0u || CapturedStructThisLambdaDepth != 0u)
						{
							Out << CapturedThisName;
						}
						else if (const auto* SelfType = ResolveUserTypeForLookup(SelfTypeName);
						    ValueExpression == &Expr && SelfType && (SelfType->bIsClass || SelfType->bIsInterface))
						{
							if (bInDestructorBody)
							{
								Fail("AOT: a class 'this' value cannot be retained while the object is being destroyed");
								return;
							}
							bNeedsMemory = true;
							bNeedsObjectBase = true;
							Out << (bMemberMethodBody ? "VsRetainObject(this)" : "VsRetainObject(&" + SelfReceiverName + ')');
						}
						else
						{
							Out << (bMemberMethodBody ? "(*this)" : SelfReceiverName);
						}
						return;
					}
				case EExprKind::Identifier:
					{
						const std::string& Name =
						    static_cast<const FIdentifierExpr&>(Expr).Name;
						if (LocalTypes.find(Name) == LocalTypes.end())
						{
							if (bInSelfContext && !SelfTypeName.empty())
							{
								const auto TypeIt = UserTypes.find(SelfTypeName);
								if (TypeIt != UserTypes.end() && !FieldType(TypeIt->second, Name).empty())
								{
									EmitSelfFieldAccess(Name);
									return;
								}
							}
							const auto BareIt = EnumBareMemberCppNames.find(Name);
							if (BareIt != EnumBareMemberCppNames.end())
							{
								if (BareIt->second.empty())
								{
									Fail(
									    "AOT: ambiguous bare enum member '" + Name + "'; use Enum.Member");
									return;
								}
								Out << BareIt->second;
								return;
							}
							std::string CppName, FunctionType;
							if (TryResolveFunctionValue(static_cast<const FIdentifierExpr&>(Expr), CppName, FunctionType))
							{
								Out << MapType(FunctionType) << "(&" << CppName << ')';
								return;
							}
							if (const FVarDeclStmt* Global =
							        ResolveGlobalVariable(Name, &Expr.Location))
							{
								Out << MangleGlobal(*Global) << "()";
								return;
							}
						}
						Out << Name;
						return;
					}
				case EExprKind::Unary:
					{
						const auto& Unary = static_cast<const FUnaryExpr&>(Expr);
						const std::string OperandType = ResolveValueType(InferExprType(*Unary.Operand));
						if (TryEmitOperatorOverloadUnary(Unary, OperandType))
						{
							return;
						}
						if (Unary.Op == EUnaryOp::Negate && IsSignedIntScriptType(OperandType))
						{
							const std::string CppType = MapType(OperandType);
							Out << "(VsCheckedNeg(static_cast<" << CppType << ">(";
							EmitExpr(*Unary.Operand);
							Out << ")))";
							return;
						}
						Out << '(';
						switch (Unary.Op)
						{
						case EUnaryOp::Negate:
							{
								Out << '-';
								break;
							}
						case EUnaryOp::Not:
							{
								Out << '!';
								break;
							}
						case EUnaryOp::BitNot:
							{
								Out << '~';
								break;
							}
						default:
							{
								break;
							}
						}
						EmitExpr(*Unary.Operand);
						Out << ')';
						return;
					}
				case EExprKind::Binary:
					{
						const auto& Binary = static_cast<const FBinaryExpr&>(Expr);
						const std::string LeftType = ResolveValueType(InferExprType(*Binary.Left));
						const std::string RightType = ResolveValueType(InferExprType(*Binary.Right));
						if (TryEmitOperatorOverloadBinary(Binary, LeftType, RightType)) return;
						if (TryEmitSequencedExpression(Expr, InitTypeHint)) return;
						if (Binary.Op == EBinaryOp::In)
						{
							Out << "(((";
							EmitExpr(*Binary.Right);
							Out << ").count(";
							EmitExpr(*Binary.Left);
							Out << ") != 0))";
							return;
						}
						if (TryEmitCheckedIntegerBinary(Binary, LeftType, RightType))
						{
							return;
						}
						if (Binary.Op == EBinaryOp::Add && (IsStringType(LeftType) || IsStringType(RightType) || IsWStringType(LeftType) || IsWStringType(RightType) || Binary.Left->Kind == EExprKind::StringLiteral || Binary.Right->Kind == EExprKind::StringLiteral || Binary.Left->Kind == EExprKind::WStringLiteral || Binary.Right->Kind == EExprKind::WStringLiteral))
						{
							const bool bLeftW = IsWStringType(LeftType) || Binary.Left->Kind == EExprKind::WStringLiteral;
							const bool bRightW = IsWStringType(RightType) || Binary.Right->Kind == EExprKind::WStringLiteral;
							if (bLeftW || bRightW)
							{
								// Match VM: both operands must already be wstring (no mix / coerce).
								if (!bLeftW || !bRightW)
								{
									Fail("AOT: cannot mix string and wstring concatenation");
									return;
								}
								Out << '(';
								EmitExpr(*Binary.Left);
								Out << " + ";
								EmitExpr(*Binary.Right);
								Out << ')';
								return;
							}
							Out << "(VsStr(";
							EmitExpr(*Binary.Left);
							Out << ") + VsStr(";
							EmitExpr(*Binary.Right);
							Out << "))";
							return;
						}
						// Do not fall through to C++ pointer relational/arithmetic on
						// user types (shared_ptr `>`). Keep ==/!= for class/nullptr identity.
						if (BinaryOpOperatorName(Binary.Op) && Binary.Op != EBinaryOp::Equal && Binary.Op != EBinaryOp::NotEqual && (UserTypes.find(LeftType) != UserTypes.end() || UserTypes.find(RightType) != UserTypes.end()))
						{
							Fail("AOT: no matching operator overload for operands");
							return;
						}
						Out << '(';
						EmitExpr(*Binary.Left);
						EmitBinaryOp(Binary.Op);
						EmitExpr(*Binary.Right);
						Out << ')';
						return;
					}
				case EExprKind::Assign:
					{
						const auto& Assign = static_cast<const FAssignExpr&>(Expr);
						if (Assign.Target->Kind == EExprKind::IndexAccess)
						{
							const auto& Index = static_cast<const FIndexAccessExpr&>(*Assign.Target);
							const auto Container = ResolveValueType(InferExprType(*Index.Object));
							if (Runtime::IsMapTypeName(Container) || Runtime::IsOrderedMapTypeName(Container))
							{
								const auto* Value = ResolveUserTypeForLookup(ResolveValueType(InferExprType(*Assign.Target)));
								if (Value && !Value->bIsClass && !Value->bIsInterface && ReferenceOwnerHasCleanup(*Value))
								{
									Fail("AOT: assigning map values with script cleanup requires the VM backend");
									return;
								}
							}
						}
						if (Assign.bFromPrefixIncDec || Assign.bFromPostfixIncDec)
						{
							if (Assign.Target->Kind == EExprKind::MethodCall && TryEmitSequencedExpression(Expr, InitTypeHint)) return;
							EmitIncDecAssign(Assign);
							return;
						}
						if (Assign.bIsCompound)
						{
							const std::string TargetType = ResolveValueType(InferExprType(*Assign.Target));
							if ((Assign.CompoundOp == EBinaryOp::Add || Assign.CompoundOp == EBinaryOp::Subtract) && IsMulticastEventTypeName(TargetType))
							{
								bNeedsMemory = true;
								Out << "VsRequireObject(";
								EmitExpr(*Assign.Target);
								Out << (Assign.CompoundOp == EBinaryOp::Add ? ").add(" : ").remove(");
								EmitCoercedValue(EventHandlerTypeName(TargetType), *Assign.Value);
								Out << ')';
								return;
							}
							if (const char* const CompoundName =
							        CompoundOpOperatorName(Assign.CompoundOp))
							{
								const auto TypeIt = UserTypes.find(TargetType);
								if (TypeIt != UserTypes.end())
								{
									const std::string ValueType = ResolveAliasChain(
									    ApplyTypeSubst(InferExprType(*Assign.Value)));
									if (TryEmitMemberOperatorCall(TypeIt->second, TargetType, CompoundName,
									        ValueType, *Assign.Target, *Assign.Value, true))
									{
										return;
									}
								}
							}
						}
						if (TryEmitSequencedExpression(Expr, InitTypeHint)) return;
						if (Assign.bIsCompound)
						{
							const std::string TargetType = ResolveValueType(InferExprType(*Assign.Target));
							const std::string RightType = ResolveValueType(InferExprType(*Assign.Value));
							if (Assign.CompoundOp == EBinaryOp::Add && (IsStringType(TargetType) || IsWStringType(TargetType)))
							{
								EmitCompoundSnapshot(Assign, MapType(TargetType),
								    [&](const auto& Old, const auto& Right)
								{
									Out << "(std::move(" << Old << ") + ";
									Out << "std::move(" << Right << ')';
									Out << ')';
								});
								return;
							}
							const bool bShift = Assign.CompoundOp == EBinaryOp::ShiftLeft || Assign.CompoundOp == EBinaryOp::ShiftRight;
							const std::string ResultType = bShift
							    ? (TargetType == "int64" || TargetType == "uint64"
							              ? TargetType
							              : IsUnsignedIntScriptType(TargetType) ? "unsigned int"
							                                                    : "int")
							    : PromoteNumericScriptTypes(TargetType, RightType);
							const bool bSignedResult = IsSignedIntScriptType(ResultType);
							const bool bUnsignedResult = IsUnsignedIntScriptType(ResultType);
							const bool bFloatingResult =
							    ResultType == "float" || ResultType == "double";
							if (bSignedResult || bUnsignedResult || bFloatingResult)
							{
								const char* CheckedFn = nullptr;
								const char* NativeToken = nullptr;
								switch (Assign.CompoundOp)
								{
								case EBinaryOp::Add:
									{
										if (bSignedResult)
										{
											CheckedFn = "VsCheckedAdd";
										}
										else
										{
											NativeToken = "+";
										}
										break;
									}
								case EBinaryOp::Subtract:
									{
										if (bSignedResult)
										{
											CheckedFn = "VsCheckedSub";
										}
										else
										{
											NativeToken = "-";
										}
										break;
									}
								case EBinaryOp::Multiply:
									{
										if (bSignedResult)
										{
											CheckedFn = "VsCheckedMul";
										}
										else
										{
											NativeToken = "*";
										}
										break;
									}
								case EBinaryOp::Divide:
									{
										CheckedFn = "VsCheckedDiv";
										break;
									}
								case EBinaryOp::Modulo:
									{
										if (!bFloatingResult)
										{
											CheckedFn = "VsCheckedMod";
										}
										break;
									}
								case EBinaryOp::BitAnd:
									{
										if (!bFloatingResult)
										{
											NativeToken = "&";
										}
										break;
									}
								case EBinaryOp::BitOr:
									{
										if (!bFloatingResult)
										{
											NativeToken = "|";
										}
										break;
									}
								case EBinaryOp::BitXor:
									{
										if (!bFloatingResult)
										{
											NativeToken = "^";
										}
										break;
									}
								case EBinaryOp::ShiftLeft:
									{
										if (!bFloatingResult)
										{
											CheckedFn = "VsCheckedShiftLeft";
										}
										break;
									}
								case EBinaryOp::ShiftRight:
									{
										if (!bFloatingResult)
										{
											CheckedFn = "VsCheckedShiftRight";
										}
										break;
									}
								default:
									{
										break;
									}
								}
								if (CheckedFn || NativeToken)
								{
									const std::string TargetCppType = MapType(TargetType);
									const std::string ResultCppType = MapType(ResultType);
									EmitCompoundSnapshot(Assign, TargetCppType,
									    [&](const auto& Old, const auto& Right)
									{
										Out << "VsCheckedNumericCast<" << TargetCppType << ">(";
										if (CheckedFn)
										{
											Out << CheckedFn << "(static_cast<" << ResultCppType << ">("
											    << Old << "), ";
											if (!bShift)
											{
												Out << "static_cast<" << ResultCppType << ">(";
											}

											Out << Right << (bShift ? ")" : "))");
										}
										else
										{
											Out << "(static_cast<" << ResultCppType << ">(" << Old << ") "
											    << NativeToken << " static_cast<" << ResultCppType << ">("
											    << Right << "))";
										}
										Out << ')';
									});
									return;
								}
							}
						}
						Out << '(';
						bool bMapInsertTarget = false;
						if (!Assign.bIsCompound && Assign.Target->Kind == EExprKind::IndexAccess)
						{
							const auto& IndexTarget =
							    static_cast<const FIndexAccessExpr&>(*Assign.Target);
							const std::string ContainerType =
							    InferExprType(*IndexTarget.Object);
							bMapInsertTarget =
							    Runtime::IsMapTypeName(ContainerType) || Runtime::IsOrderedMapTypeName(ContainerType);
							if (bMapInsertTarget)
							{
								Out << '(';
								EmitExpr(*IndexTarget.Object);
								Out << ").get_or_insert(";
								EmitExpr(*IndexTarget.Index);
								Out << ')';
							}
						}
						if (!bMapInsertTarget)
						{
							EmitExpr(*Assign.Target);
						}
						if (Assign.bIsCompound)
						{
							switch (Assign.CompoundOp)
							{
							case EBinaryOp::Add:
								{
									Out << " += ";
									break;
								}
							case EBinaryOp::Subtract:
								{
									Out << " -= ";
									break;
								}
							case EBinaryOp::Multiply:
								{
									Out << " *= ";
									break;
								}
							case EBinaryOp::Divide:
								{
									Out << " /= ";
									break;
								}
							case EBinaryOp::Modulo:
								{
									Out << " %= ";
									break;
								}
							case EBinaryOp::BitAnd:
								{
									Out << " &= ";
									break;
								}
							case EBinaryOp::BitOr:
								{
									Out << " |= ";
									break;
								}
							case EBinaryOp::BitXor:
								{
									Out << " ^= ";
									break;
								}
							case EBinaryOp::ShiftLeft:
								{
									Out << " <<= ";
									break;
								}
							case EBinaryOp::ShiftRight:
								{
									Out << " >>= ";
									break;
								}
							default:
								{
									Fail("AOT: unsupported compound assignment");
									return;
								}
							}
						}
						else
						{
							Out << " = ";
						}
						{
							const std::string TargetType = ResolveValueType(InferExprType(*Assign.Target));
							if (!Assign.bIsCompound)
							{
								EmitCoercedValue(TargetType, *Assign.Value);
							}
							else
							{
								EmitExpr(*Assign.Value);
							}
						}
						Out << ')';
						return;
					}
				case EExprKind::Conditional:
					{
						const auto& Cond = static_cast<const FConditionalExpr&>(Expr);
						Out << '(';
						EmitExpr(*Cond.Condition);
						Out << " ? ";
						EmitExpr(*Cond.ThenExpr);
						Out << " : ";
						EmitExpr(*Cond.ElseExpr);
						Out << ')';
						return;
					}
				case EExprKind::Call:
					{
						EmitCallExpr(static_cast<const FCallExpr&>(Expr));
						return;
					}
				case EExprKind::MethodCall:
					{
						const auto& Call = static_cast<const FMethodCallExpr&>(Expr);
						// C++-style static member function via a type-name receiver
						// (`Counter.Next()`, `Box<int>.Seed()`): emit `Type::Method(args)`.
						{
							std::string StaticQual;
							const std::vector<FFieldDecl>* StaticFields = nullptr;
							const std::vector<FMethodDecl>* StaticMethods = nullptr;
							if (Call.Object && TryResolveStaticTypeReceiver(*Call.Object, StaticQual, StaticFields, StaticMethods))
							{
								const FMethodDecl* StaticMethod = ResolveStaticMethodInList(
								    StaticMethods, Call.Method, Call.Arguments.size());
								if (StaticMethod && StaticMethod->bIsStatic)
								{
									Out << StaticQual << "::"
									    << SanitizeSymbolName(Call.Method) << '(';
									EmitCallArgsForParams(
									    StaticMethod->Parameters, Call.Arguments);
									Out << ')';
									return;
								}
								Fail(
								    "AOT: static method '" + Call.Method + "' not found on '" + StaticQual + "'");
								return;
							}
						}
						// A namespace-qualified callable global is also represented as a
						// method-shaped call. It must retain value-call semantics and its
						// declared function signature for ref/value argument lowering.
						std::string NamespaceCallName;
						if (Call.Object && TryBuildNamespaceCallName(*Call.Object, Call.Method, NamespaceCallName))
						{
							if (const FVarDeclStmt* Global = ResolveGlobalVariable(
							        NamespaceCallName, &Call.Location))
							{
								std::vector<std::string> FnParamTypes;
								std::string FnReturnType;
								const std::string FnType = ResolveAliasChain(
								    ApplyTypeSubst(Global->TypeName));
								if (!Runtime::ParseFunctionType(
								        FnType, FnParamTypes, FnReturnType))
								{
									Fail("AOT: '" + NamespaceCallName + "' is not a function value");
									return;
								}
								EmitNamedCallable(Expr, MangleGlobal(*Global) + "()");
								Out << '(';
								EmitResolvedCallArgs(Call.Arguments, &FnParamTypes);
								Out << ')';
								return;
							}
						}
						// A namespace-qualified free function is represented by the parser as
						// a method-shaped call until semantic lookup distinguishes the receiver
						// from an object or type. Preserve static-member precedence above, then
						// route namespace calls through ordinary free-function overload rules.
						if (Call.Object && TryBuildNamespaceCallName(*Call.Object, Call.Method, NamespaceCallName))
						{
							std::vector<std::string> NamespaceArgTypes;
							NamespaceArgTypes.reserve(Call.Arguments.size());
							for (const std::unique_ptr<FExpr>& Arg : Call.Arguments)
							{
								NamespaceArgTypes.push_back(InferExprType(*Arg));
							}
							if (const FFunctionDecl* Concrete = ResolveConcreteCallOverload(
							        NamespaceCallName,
							        Call,
							        NamespaceArgTypes,
							        &Call.Location))
							{
								Out << EmitFunctionCppName(*Concrete) << '(';
								EmitCallArgsForParams(Concrete->Parameters, Call.Arguments);
								Out << ')';
								return;
							}
							std::vector<std::string> TypeArgs;
							if (const FFunctionDecl* Generic = ResolveGenericOverloadFromTypes(
							        NamespaceCallName,
							        Call.TypeArgs,
							        NamespaceArgTypes,
							        TypeArgs,
							        &Call.Location))
							{
								Out << EmitFunctionCppName(*Generic) << '<';
								for (std::size_t Index = 0; Index < TypeArgs.size(); ++Index)
								{
									if (Index > 0)
									{
										Out << ", ";
									}
									Out << MapType(TypeArgs[Index]);
								}
								Out << ">(";
								EmitCallArgsForParams(Generic->Parameters, Call.Arguments);
								Out << ')';
								return;
							}
							Fail("AOT: no matching namespace function '" + NamespaceCallName + "'");
							return;
						}
						const std::string ObjectType = InferExprType(*Call.Object);
						std::vector<std::string> AttributeArgs;
						if (bHasStandardAttribute &&
						    TryParseSoftBuiltinType(ObjectType, "attribute", AttributeArgs))
						{
							Out << '(';
							EmitExpr(*Call.Object);
							Out << ")." << Call.Method << '(';
							std::vector<std::string> Params;
							if (Call.Method == "bind")
							{
								Params = {"fn()->" + AttributeArgs[0]};
							}

							else if (Call.Method == "bind_optional")
							{
								Params = {"fn()->optional<" + AttributeArgs[0] + ">"};
							}

							else if (Call.Method == "set" || Call.Method == "get_or")
							{
								Params = {AttributeArgs[0]};
							}

							EmitResolvedCallArgs(Call.Arguments, Params.empty() ? nullptr : &Params);
							Out << ')';
							return;
						}
						std::string MetadataReceiver = ResolveValueType(ObjectType);
						if (MetadataReceiver.empty() && Call.Object->Kind == EExprKind::Call)
						{
							const auto& Constructor = static_cast<const FCallExpr&>(*Call.Object);
							const auto TypeName = ResolveAliasChain(ApplyTypeSubst(Constructor.Callee));
							if (IsRuntimeSourceMetadataType(TypeName))
							{
								MetadataReceiver = TypeName;
							}
						}
						std::vector<std::string> MetadataArgs;
						const bool bReflectionReceiver = IsRuntimeSourceMetadataType(MetadataReceiver) || TryParseSoftBuiltinType(MetadataReceiver, "type", MetadataArgs) || TryParseSoftBuiltinType(MetadataReceiver, "field_info", MetadataArgs) || TryParseSoftBuiltinType(MetadataReceiver, "method_info", MetadataArgs) || ((MetadataReceiver == "function_info" || MetadataReceiver == "global_info") && UserTypes.find(MetadataReceiver) == UserTypes.end());
						if (bReflectionReceiver && (IsRuntimeSourceMetadataType(MetadataReceiver) || Call.Method == "annotation_count" || Call.Method == "annotation" || Call.Method == "annotations" || Call.Method == "find_annotations" || Call.Method == "has_annotation" || Call.Method == "has_metadata" || Call.Method == "metadata" || Call.Method == "comment_count" || Call.Method == "comment" || Call.Method == "comments" || Call.Method == "documentation" || Call.Method == "comments_available" || Call.Method == "comments_retention" || Call.Method == "source_metadata" || Call.Method == "declaration"))
						{
							Fail("AOT: runtime source metadata requires the VeroScript runtime; use the VM backend");
							return;
						}
						if (Call.TypeArgs.size() == 1)
						{
							std::vector<std::string> ReflectionTypeArgs;
							const std::string ReceiverType = ResolveAliasChain(
							    ApplyTypeSubst(ObjectType));
							if (((Call.Method == "get" || Call.Method == "set") && TryParseSoftBuiltinType(ReceiverType, "field_info", ReflectionTypeArgs)) || ((Call.Method == "invoke" || Call.Method == "invoke_static") && TryParseSoftBuiltinType(ReceiverType, "method_info", ReflectionTypeArgs)))
							{
								Fail("AOT: executable reflection is not supported by the C++ backend");
								return;
							}
						}
						if (TryEmitContainerMethodCall(Call, ObjectType))
						{
							return;
						}
						if (TryEmitEventMethodCall(Call, ObjectType))
						{
							return;
						}
						std::vector<std::string> OptionalArgs;
						if (TryParseSoftBuiltinType(
						        ObjectType, "optional", OptionalArgs) &&
						    OptionalArgs.size() == 1 && (Call.Method == "has_value" || Call.Method == "get" || Call.Method == "value_or" || Call.Method == "reset"))
						{
							Out << '(';
							EmitExpr(*Call.Object);
							Out << ")." << Call.Method << '(';
							EmitResolvedCallArgs(Call.Arguments);
							Out << ')';
							return;
						}
						if (TryParseSoftBuiltinType(ObjectType, "readonly", OptionalArgs) && OptionalArgs.size() == 1 && Call.Method == "get")
						{
							Out << '(';
							EmitExpr(*Call.Object);
							Out << ").get()";
							return;
						}
						if (TryParseSoftBuiltinType(ObjectType, "weak", OptionalArgs) && OptionalArgs.size() == 1 && (Call.Method == "lock" || Call.Method == "expired" || Call.Method == "reset"))
						{
							Out << '(';
							EmitExpr(*Call.Object);
							Out << ")." << Call.Method << '(';
							EmitResolvedCallArgs(Call.Arguments);
							Out << ')';
							return;
						}
						std::vector<std::string> ArgTypes;
						ArgTypes.reserve(Call.Arguments.size());
						for (const std::unique_ptr<FExpr>& Arg : Call.Arguments)
						{
							// Empty is OK: bare contextual lambdas resolve via param types.
							ArgTypes.push_back(InferExprType(*Arg));
						}
						const bool bOpenTypeParam =
						    ActiveOpenTypeParams.find(ObjectType) != ActiveOpenTypeParams.end();
						if (bOpenTypeParam)
						{
							if (const FMethodDecl* Method =
							        ResolveMethodOnOpenTypeParam(ObjectType, Call.Method, ArgTypes))
							{
								// Bounds require interface implementers; class/iface map to shared_ptr.
								bNeedsMemory = true;
								Out << "VsRequireObject(";
								EmitExpr(*Call.Object);
								Out << ")." << SanitizeSymbolName(Call.Method) << '(';
								EmitCallArgsForParams(Method->Parameters, Call.Arguments);
								Out << ')';
								return;
							}
						}
						const FUserTypeInfo* ObjectTypeInfo =
						    ResolveUserTypeForLookup(ObjectType);
						const FMethodDecl* Method = ObjectTypeInfo
						    ? ResolveMethod(*ObjectTypeInfo, Call.Method, ArgTypes)
						    : nullptr;
						if (!Method && TryEmitExtensionMethodCall(Call, ObjectType))
						{
							return;
						}
						if (!ObjectTypeInfo)
						{
							Fail(bOpenTypeParam
							        ? "AOT: no matching method '" + Call.Method + "' on type parameter '" + ObjectType + "' (check where bounds or visible extensions)"
							        : "AOT: method '" + Call.Method + "' on unsupported type '" + ObjectType + "'");
							return;
						}
						if (!Method)
						{
							Fail(
							    "AOT: no matching method '" + Call.Method + "' on '" + ObjectType + "'");
							return;
						}
						if (TypeNeedsOopLayout(*ObjectTypeInfo) || ObjectTypeInfo->bIsInterface)
						{
							const bool bArrow =
							    ObjectTypeInfo->bIsClass || ObjectTypeInfo->bIsInterface;
							const bool bObjectIsThis = Call.Object->Kind == EExprKind::This || (Call.Object->Kind == EExprKind::Identifier && static_cast<const FIdentifierExpr&>(*Call.Object).Name == "this") || IsConstCastOfThis(*Call.Object);
							if (bArrow && !IsDirectObjectReceiver(*Call.Object, bObjectIsThis))
							{
								bNeedsMemory = true;
								Out << "VsRequireObject(";
								EmitExpr(*Call.Object);
								Out << ").";
							}
							else
							{
								Out << '(';
								EmitExpr(*Call.Object);
								Out << ").";
							}
							Out << SanitizeSymbolName(Call.Method) << '(';
							EmitCallArgsForUserTypeParams(
							    *ObjectTypeInfo, Method->Parameters, Call.Arguments);
							Out << ')';
							return;
						}
						Out << MangleMethod(ObjectTypeInfo->Name, Call.Method) << '(';
						EmitExpr(*Call.Object);
						if (!Method->Parameters.empty())
						{
							Out << ", ";
							EmitCallArgsForUserTypeParams(
							    *ObjectTypeInfo, Method->Parameters, Call.Arguments);
						}
						Out << ')';
						return;
					}
				case EExprKind::MemberAccess:
					{
						const auto& Access = static_cast<const FMemberAccessExpr&>(Expr);
						FIdentifierExpr Reference("");
						if (TryGetNamespaceFunctionReference(Access, Reference))
						{
							std::string CppName, FunctionType;
							if (TryResolveFunctionValue(Reference, CppName, FunctionType))
							{
								Out << MapType(FunctionType) << "(&" << CppName << ')';
								return;
							}
						}
						if (Access.Object && Access.Object->Kind == EExprKind::Identifier)
						{
							const std::string OwnerName = ResolveAliasChain(
							    ApplyTypeSubst(
							        static_cast<const FIdentifierExpr&>(*Access.Object).Name));
							if (EnumTypeNames.find(OwnerName) != EnumTypeNames.end())
							{
								const std::string Key = OwnerName + '.' + Access.Member;
								if (EnumMemberValues.find(Key) == EnumMemberValues.end())
								{
									Fail(
									    "AOT: unknown enum member '" + OwnerName + '.' + Access.Member + "'");
									return;
								}
								Out << EnumMemberCppName(OwnerName, Access.Member);
								return;
							}
						}
						// C++-style static data member via a type-name receiver
						// (`Counter.Count`, `Box<int>.Count`): emit `Type::Field`.
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
									Out << StaticQual << "::" << Access.Member;
									return;
								}
								Fail(
								    "AOT: static field '" + Access.Member + "' not found on '" + StaticQual + "'");
								return;
							}
						}
						std::string NamespaceGlobalName;
						if (Access.Object && TryBuildNamespaceCallName(*Access.Object, Access.Member, NamespaceGlobalName))
						{
							if (const FVarDeclStmt* Global = ResolveGlobalVariable(
							        NamespaceGlobalName, &Expr.Location))
							{
								Out << MangleGlobal(*Global) << "()";
								return;
							}
						}
						std::string ObjectType = InferExprType(*Access.Object);
						std::string HandleInner;
						if (Access.Member == "value" && TryParseHandleType(ObjectType, HandleInner))
						{
							bNeedsMemory = true;
							bNeedsHandle = true;
							Out << "VsRequireObject(";
							EmitExpr(*Access.Object);
							Out << ").value";
							return;
						}
						std::string PairFirst;
						std::string PairSecond;
						if (!HasUserPairTemplate() && Runtime::ParsePairType(ObjectType, PairFirst, PairSecond) && (Access.Member == "first" || Access.Member == "second"))
						{
							bNeedsUtility = true;
							Out << '(';
							EmitExpr(*Access.Object);
							Out << ")." << Access.Member;
							return;
						}
						std::vector<std::string> SoftArgs;
						if ((TryParseSoftBuiltinType(ObjectType, "optional", SoftArgs) && SoftArgs.size() == 1 && (Access.Member == "present" || Access.Member == "value")) || (TryParseSoftBuiltinType(ObjectType, "type", SoftArgs) && SoftArgs.size() == 1 && (Access.Member == "name" || Access.Member == "qualified_name")))
						{
							bNeedsUtility = true;
							Out << '(';
							EmitExpr(*Access.Object);
							if (TryParseSoftBuiltinType(ObjectType, "optional", SoftArgs))
							{
								Out << (Access.Member == "present" ? ").has_value()" : ").value_ref()");
							}

							else
							{
								Out << ")." << Access.Member;
							}

							return;
						}
						// Materialize nested generic field layouts before member emission.
						if (const auto* ResolvedType = ResolveUserTypeForLookup(ObjectType))
						{
							ObjectType = ResolvedType->Name;
						}

						const auto TypeIt = UserTypes.find(ObjectType);
						if (TypeIt == UserTypes.end())
						{
							const auto OpenIt = OpenGenericTypes.find(ObjectType);
							if (OpenIt != OpenGenericTypes.end() && OpenIt->second.Fields)
							{
								bool bFound = false;
								for (const FFieldDecl& Field : *OpenIt->second.Fields)
								{
									if (Field.Name == Access.Member)
									{
										bFound = true;
										break;
									}
								}
								if (!bFound)
								{
									Fail(
									    "AOT: unknown field '" + Access.Member + "' on '" + ObjectType + "'");
									return;
								}
								const bool bObjectIsThis = Access.Object->Kind == EExprKind::This || (Access.Object->Kind == EExprKind::Identifier && static_cast<const FIdentifierExpr&>(*Access.Object).Name == "this") || IsConstCastOfThis(*Access.Object);
								const bool bDeref = OpenIt->second.bIsClass && !IsDirectObjectReceiver(*Access.Object, bObjectIsThis);
								if (bDeref)
								{
									bNeedsMemory = true;
									Out << "VsRequireObject(";
									EmitExpr(*Access.Object);
									Out << ").";
								}
								else
								{
									Out << '(';
									EmitExpr(*Access.Object);
									Out << ").";
								}
								Out << Access.Member;
								return;
							}
							Fail("AOT: member access on unsupported type '" + ObjectType + "'");
							return;
						}
						if (FieldType(TypeIt->second, Access.Member).empty())
						{
							Fail(
							    "AOT: unknown field '" + Access.Member + "' on '" + ObjectType + "'");
							return;
						}
						// Classes/interfaces are RC (shared_ptr). Member bodies emit `this` as
						// `(*this)` already (raw object); other class values stay shared_ptr.
						const bool bObjectIsThis = Access.Object->Kind == EExprKind::This || (Access.Object->Kind == EExprKind::Identifier && static_cast<const FIdentifierExpr&>(*Access.Object).Name == "this") || IsConstCastOfThis(*Access.Object);
						const bool bDeref =
						    (TypeIt->second.bIsClass || TypeIt->second.bIsInterface) && !IsDirectObjectReceiver(*Access.Object, bObjectIsThis);
						if (bDeref)
						{
							bNeedsMemory = true;
							Out << "VsRequireObject(";
							EmitExpr(*Access.Object);
							Out << ").";
						}
						else
						{
							Out << '(';
							EmitExpr(*Access.Object);
							Out << ").";
						}
						Out << Access.Member;
						return;
					}
				case EExprKind::IndexAccess:
					{
						const auto& Access = static_cast<const FIndexAccessExpr&>(Expr);
						const std::string ObjectType = InferExprType(*Access.Object);
						const std::string IndexType = InferExprType(*Access.Index);
						const auto ObjectTypeIt = UserTypes.find(ObjectType);
						if (ObjectTypeIt != UserTypes.end())
						{
							if (TryEmitMemberOperatorCall(ObjectTypeIt->second, ObjectType, "operator[]",
							        IndexType, *Access.Object, *Access.Index, true))
							{
								return;
							}
						}
						Out << '(';
						EmitExpr(*Access.Object);
						const bool bNativeArray = Runtime::IsNativeArrayTypeName(ObjectType) || Runtime::IsScriptArgsArrayTypeName(ObjectType);
						const bool bLinearIndex = Runtime::IsDynamicArrayTypeName(ObjectType) || bNativeArray || Runtime::IsDequeTypeName(ObjectType);
						Out << (bNativeArray ? ").at(" : ")[");
						if (bLinearIndex)
						{
							Out << "VsCheckedNativeSize(";
						}
						EmitExpr(*Access.Index);
						if (bLinearIndex)
						{
							Out << ')';
						}
						Out << (bNativeArray ? ')' : ']');
						return;
					}
				case EExprKind::ArrayInit:
					{
						const auto& Init = static_cast<const FArrayInitExpr&>(Expr);
						std::string ExplicitType;
						const std::string* EffectiveTypeHint = InitTypeHint;
						const bool bUsesExplicitType =
						    (!EffectiveTypeHint || EffectiveTypeHint->empty()) && !Init.TypeName.empty();
						if (bUsesExplicitType)
						{
							ExplicitType = ResolveAliasChain(ApplyTypeSubst(Init.TypeName));
							EffectiveTypeHint = &ExplicitType;
						}
						bool bNativeArrayHint = false;
						std::string ElementTypeHint;
						if (EffectiveTypeHint && !EffectiveTypeHint->empty())
						{
							std::uint32_t NativeLength = 0;
							bNativeArrayHint = Runtime::ParseNativeArrayType(
							    *EffectiveTypeHint,
							    ElementTypeHint,
							    NativeLength);
							if (!bNativeArrayHint)
							{
								(void)Runtime::ParseDynamicArrayElementType(
								    *EffectiveTypeHint, ElementTypeHint);
							}
							if (!bNativeArrayHint || bUsesExplicitType)
							{
								Out << MapType(*EffectiveTypeHint);
							}
						}
						else
						{
							bNeedsVector = true;
						}
						Out << '{';
						for (std::size_t Index = 0; Index < Init.Elements.size(); ++Index)
						{
							if (Index > 0)
							{
								Out << ", ";
							}
							if (!ElementTypeHint.empty())
							{
								EmitCoercedValue(ElementTypeHint, *Init.Elements[Index]);
							}
							else
							{
								EmitExpr(*Init.Elements[Index]);
							}
						}
						Out << '}';
						return;
					}
				case EExprKind::MapInit:
					{
						const auto& Init = static_cast<const FMapInitExpr&>(Expr);
						std::string ExplicitType;
						const std::string* EffectiveTypeHint = InitTypeHint;
						if ((!EffectiveTypeHint || EffectiveTypeHint->empty()) && !Init.TypeName.empty())
						{
							ExplicitType = ResolveAliasChain(ApplyTypeSubst(Init.TypeName));
							EffectiveTypeHint = &ExplicitType;
						}
						if (EffectiveTypeHint && !EffectiveTypeHint->empty())
						{
							Out << MapType(*EffectiveTypeHint);
						}
						std::string KeyTypeHint;
						std::string ValueTypeHint;
						if (EffectiveTypeHint && !EffectiveTypeHint->empty())
						{
							const std::string ResolvedContainer =
							    ResolveAliasChain(ApplyTypeSubst(*EffectiveTypeHint));
							if (!Runtime::SplitMapType(
							        ResolvedContainer, KeyTypeHint, ValueTypeHint))
							{
								(void)Runtime::SplitOrderedMapType(
								    ResolvedContainer, KeyTypeHint, ValueTypeHint);
							}
							if (Runtime::IsMultiMapTypeName(ResolvedContainer) || Runtime::IsOrderedMultiMapTypeName(ResolvedContainer))
							{
								// Multimap storage maps each key to an array; brace entries are
								// lowered elsewhere and must not be coerced as scalar map values.
								KeyTypeHint.clear();
								ValueTypeHint.clear();
							}
						}
						Out << '{';
						for (std::size_t Index = 0; Index < Init.Entries.size(); ++Index)
						{
							if (Index > 0)
							{
								Out << ", ";
							}
							Out << '{';
							if (!KeyTypeHint.empty())
							{
								EmitCoercedValue(KeyTypeHint, *Init.Entries[Index].Key);
							}
							else
							{
								EmitExpr(*Init.Entries[Index].Key);
							}
							Out << ", ";
							if (!ValueTypeHint.empty())
							{
								EmitCoercedValue(ValueTypeHint, *Init.Entries[Index].Value);
							}
							else
							{
								EmitExpr(*Init.Entries[Index].Value);
							}
							Out << '}';
						}
						Out << '}';
						return;
					}
				case EExprKind::Lambda:
					{
						EmitLambda(
						    static_cast<const FLambdaExpr&>(Expr),
						    InitTypeHint ? *InitTypeHint : std::string{});
						return;
					}
				default:
					{
						Fail("AOT: unsupported expression kind");
						return;
					}
				}
			}

			// Capture kind: Implicit [=], None [], Explicit [a,b].
			void EmitLambda(const FLambdaExpr& Lambda, const std::string& ExpectedFnType = {})
			{
				const auto ExplicitValueNames = Lambda.GetCaptureNames(false);
				const auto ExplicitReferenceNames = Lambda.GetCaptureNames(true);
				// VM/JIT reference captures are promoted to managed heap cells so an
				// escaping callable cannot dangle. Emitting a raw C++ `[&]` would silently
				// weaken that guarantee; fail explicitly until AOT has equivalent lowering.
				if (Lambda.CaptureKind == ELambdaCaptureKind::Reference || !ExplicitReferenceNames.empty())
				{
					Fail("AOT: lambda reference captures require managed capture-cell lowering");
					return;
				}
				std::vector<std::string> ExpectedParams;
				std::string ExpectedReturn;
				const bool bHaveExpected =
				    !ExpectedFnType.empty() && Runtime::ParseFunctionType(ExpectedFnType, ExpectedParams, ExpectedReturn);
				if (bHaveExpected && Lambda.Parameters.size() != ExpectedParams.size())
				{
					Fail("AOT: lambda parameter count does not match expected function type");
					return;
				}
				std::vector<std::string> ParamTypeNames;
				ParamTypeNames.reserve(Lambda.Parameters.size());
				for (std::size_t Index = 0; Index < Lambda.Parameters.size(); ++Index)
				{
					std::string ParamType = Lambda.Parameters[Index].TypeName;
					if (ParamType.empty())
					{
						if (!bHaveExpected || Index >= ExpectedParams.size())
						{
							Fail(
							    "AOT: cannot infer lambda parameter types without a function-typed context");
							return;
						}
						ParamType = ExpectedParams[Index];
					}
					else if (bHaveExpected)
					{
						std::string Written =
						    ResolveAliasChain(ApplyTypeSubst(ParamType));
						if (Lambda.Parameters[Index].bIsRef && !Runtime::IsReferenceTypeName(Written))
						{
							Written = (Lambda.Parameters[Index].bIsConst ? "const ref " : "ref ") + Written;
						}
						const std::string Expected =
						    ResolveAliasChain(ApplyTypeSubst(ExpectedParams[Index]));
						if (Written != Expected)
						{
							Fail(
							    "AOT: lambda parameter type '" + Written + "' conflicts with expected '" + Expected + "'");
							return;
						}
					}
					ParamTypeNames.push_back(ParamType);
				}
				std::string ReturnType = Lambda.ReturnType;
				if (ReturnType.empty())
				{
					if (!bHaveExpected)
					{
						Fail(
						    "AOT: cannot infer lambda return type without a function-typed context");
						return;
					}
					ReturnType = ExpectedReturn;
				}
				else if (bHaveExpected)
				{
					const std::string Written =
					    ResolveAliasChain(ApplyTypeSubst(ReturnType));
					const std::string Expected =
					    ResolveAliasChain(ApplyTypeSubst(ExpectedReturn));
					if (Written != Expected)
					{
						Fail(
						    "AOT: lambda return type '" + Written + "' conflicts with expected '" + Expected + "'");
						return;
					}
				}
				bNeedsFunctional = true;
				bNeedsMemory = true;
				bool bCaptureClassThis = false;
				bool bCaptureStructThis = false;
				if (Lambda.bCapturesThis.load(std::memory_order_acquire) && (bInSelfContext || bCtorInitDirectSelf) && !SelfTypeName.empty())
				{
					const auto SelfIt = UserTypes.find(SelfTypeName);
					bCaptureClassThis = SelfIt != UserTypes.end() && (SelfIt->second.bIsClass || SelfIt->second.bIsInterface);
					bCaptureStructThis = SelfIt != UserTypes.end() && !SelfIt->second.bIsClass && !SelfIt->second.bIsInterface;
				}
				if (bCaptureClassThis && bInDestructorBody)
				{
					Fail(
					    "AOT: a lambda cannot capture class 'this' while the object is being destroyed");
					return;
				}
				std::unordered_set<std::string> CaptureCandidates;
				for (const auto& [Name, Type] : LocalTypes)
				{
					CaptureCandidates.insert(Name);
				}

				for (const auto& Parameter : Lambda.Parameters)
				{
					CaptureCandidates.erase(Parameter.Name);
				}
				const bool HasInitializers = std::any_of(Lambda.Captures.begin(), Lambda.Captures.end(),
				    [](const auto& Capture)
				{
					return Capture.Initializer != nullptr;
				});
				std::unordered_map<std::string, std::string> InitializerTypes;
				std::unordered_map<std::string, bool> CaptureConstness;
				std::unordered_map<std::string, std::string> CaptureSlots;
				if (HasInitializers)
				{
					Out << "([&]() {\n";
					for (const auto& Capture : Lambda.Captures)
					{
						if (!Capture.Initializer && !CaptureCandidates.count(Capture.Name))
						{
							continue;
						}
						const auto Type = ResolveAliasChain(ApplyTypeSubst(std::string(Runtime::StripReferenceTypeName(
						    Capture.Initializer ? InferExprType(*Capture.Initializer) : LocalTypes.at(Capture.Name)))));
						if (Type.empty() || Type == "auto")
						{
							Fail("AOT: cannot resolve initialized capture type: " + Capture.Name);
							return;
						}
						const auto Slot = GeneratedLocalName("___vs_initialized_capture_" + std::to_string(NextLocalInitializerId++));
						CaptureSlots.emplace(Capture.Name, Slot);
						InitializerTypes.emplace(Capture.Name, Type);
						CaptureConstness.emplace(Capture.Name, !Lambda.bMutable || (!Capture.Initializer && LocalConstness[Capture.Name]));
						Out << "auto " << Slot << " = std::shared_ptr<" << MapType(Type) << ">(new " << MapType(Type) << '(';
						if (Capture.Initializer)
						{
							EmitCoercedValue(Type, *Capture.Initializer);
						}
						else
						{
							Out << Capture.Name;
						}
						Out << "));\n";
					}
					Out << "return ";
				}
				for (const auto& Capture : Lambda.Captures)
				{
					if (Capture.Initializer)
					{
						CaptureCandidates.erase(Capture.Name);
					}
				}

				std::unordered_set<std::string> CaptureNames;
				if (Lambda.Body)
				{
					CollectLambdaCaptureNames(
					    *Lambda.Body, CaptureCandidates, CaptureNames);
				}

				if (Lambda.ExpressionBody)
				{
					CollectLambdaCaptureNames(
					    *Lambda.ExpressionBody, CaptureCandidates, CaptureNames);
				}

				TValueScopeRestore<decltype(RetainedReferenceAliases)> ReferenceAliasScope(
				    RetainedReferenceAliases);
				for (const auto& Capture : CaptureNames)
				{
					RetainedReferenceAliases.erase(Capture);
				}

				for (const auto& Param : Lambda.Parameters)
				{
					RetainedReferenceAliases.erase(Param.Name);
				}

				if (Lambda.CaptureKind != ELambdaCaptureKind::Implicit)
				{
					CaptureNames.clear();
					if (Lambda.CaptureKind == ELambdaCaptureKind::Explicit)
					{
						for (const auto& Name : ExplicitValueNames)
						{
							CaptureNames.insert(Name);
						}
					}
				}
				for (const auto& Capture : Lambda.Captures)
				{
					if (CaptureSlots.count(Capture.Name))
					{
						CaptureNames.insert(Capture.Name);
					}
				}
				std::vector<std::string> SortedCaptures(CaptureNames.begin(), CaptureNames.end());
				std::sort(SortedCaptures.begin(), SortedCaptures.end());
				const bool bWritableStructThis = bCaptureStructThis && !bSelfIsConst;
				if (bWritableStructThis && !Lambda.bMutable)
				{
					// Receiver snapshots keep their original method mutability. Give
					// ordinary captures const aliases when the native lambda must be mutable.
					for (const auto& Name : SortedCaptures)
					{
						if (CaptureCandidates.count(Name) && !CaptureSlots.count(Name))
						{
							CaptureSlots.emplace(Name, GeneratedLocalName("___vs_readonly_capture_" + std::to_string(NextLocalInitializerId++)));
						}
					}
				}
				if (Runtime::IsReferenceTypeName(ResolveAliasChain(ApplyTypeSubst(ReturnType))) && (bCaptureClassThis || bCaptureStructThis || std::any_of(SortedCaptures.begin(), SortedCaptures.end(), [&](const auto& Name)
				{
					return CaptureSlots.count(Name) || CaptureCandidates.count(Name);
				})))
				{
					// A native reference cannot keep the shared function/capture storage alive.
					Fail("AOT: reference returns with owned lambda state require the Core AOT exporter");
					return;
				}
				Out << "vs_Function<" << MapType(ReturnType) << '(';
				for (std::size_t Index = 0; Index < ParamTypeNames.size(); ++Index)
				{
					if (Index > 0)
					{
						Out << ", ";
					}
					Out << MapParamType(
					    ParamTypeNames[Index],
					    Lambda.Parameters[Index].bIsRef,
					    Lambda.Parameters[Index].bIsConst);
				}
				Out << ")>(";
				switch (Lambda.CaptureKind)
				{
				case ELambdaCaptureKind::None:
					{
						Out << "[](";
						break;
					}
				case ELambdaCaptureKind::Explicit:
					{
						Out << '[';
						bool HasExplicitCapture = false;
						for (const auto& Name : ExplicitValueNames)
						{
							const auto Slot = CaptureSlots.find(Name);
							if (Slot == CaptureSlots.end() && !CaptureCandidates.count(Name))
							{
								continue;
							}
							if (HasExplicitCapture)
							{
								Out << ", ";
							}
							HasExplicitCapture = true;
							Out << (Slot != CaptureSlots.end() ? Slot->second : Name);
							if (Slot != CaptureSlots.end() && !InitializerTypes.count(Name))
							{
								Out << " = " << Name;
							}
						}
						if (bCaptureClassThis)
						{
							if (HasExplicitCapture)
							{
								Out << ", ";
							}
							if (CapturedClassThisLambdaDepth == 0u)
							{
								Out << CapturedThisName << " = VsRetainObject("
								    << (bMemberMethodBody ? "this" : '&' + SelfReceiverName) << ')';
							}
							else
							{
								Out << CapturedThisName;
							}
						}
						if (bCaptureStructThis)
						{
							if (HasExplicitCapture || bCaptureClassThis)
							{
								Out << ", ";
							}
							if (CapturedStructThisLambdaDepth == 0u)
							{
								Out << CapturedThisName << " = "
								    << (bMemberMethodBody ? "*this" : SelfReceiverName);
							}
							else
							{
								Out << CapturedThisName;
							}
						}
						Out << "](";
						break;
					}
				case ELambdaCaptureKind::Implicit:
				default:
					{
						Out << "[=";
						for (const auto& Name : SortedCaptures)
						{
							const auto Slot = CaptureSlots.find(Name);
							if (Slot != CaptureSlots.end() && !InitializerTypes.count(Name))
							{
								Out << ", " << Slot->second << " = " << Name;
							}
						}
						if (bCaptureClassThis && CapturedClassThisLambdaDepth == 0u)
						{
							Out << ", " << CapturedThisName << " = VsRetainObject("
							    << (bMemberMethodBody ? "this" : '&' + SelfReceiverName) << ')';
						}
						if (bCaptureStructThis && CapturedStructThisLambdaDepth == 0u)
						{
							Out << ", " << CapturedThisName << " = "
							    << (bMemberMethodBody ? "*this" : SelfReceiverName);
						}
						Out << "](";
						break;
					}
				}
				for (std::size_t Index = 0; Index < Lambda.Parameters.size(); ++Index)
				{
					const FFunctionParam& Param = Lambda.Parameters[Index];
					if (Index > 0)
					{
						Out << ", ";
					}
					Out << MapParamType(ParamTypeNames[Index], Param.bIsRef, Param.bIsConst)
					    << ' ' << Param.Name;
				}
				Out << ')' << (Lambda.bMutable || bWritableStructThis ? " mutable" : "") << " -> " << MapType(ReturnType) << '\n';

				TValueScopeRestore<std::size_t> CapturedSelfScope(
				    CapturedClassThisLambdaDepth);
				TValueScopeRestore<std::size_t> CapturedStructSelfScope(
				    CapturedStructThisLambdaDepth);
				if (bCaptureClassThis)
				{
					++CapturedClassThisLambdaDepth;
				}
				if (bCaptureStructThis)
				{
					++CapturedStructThisLambdaDepth;
				}
				const auto SavedLocals = LocalTypes;
				const auto SavedConstness = LocalConstness;
				const std::string SavedReturnType = ActiveReturnType;
				ActiveReturnType = ReturnType;
				for (const auto& Name : SortedCaptures)
				{
					if (CaptureCandidates.count(Name))
					{
						LocalConstness[Name] = LocalConstness[Name] || !Lambda.bMutable;
					}
				}
				for (const auto& Binding : InitializerTypes)
				{
					LocalTypes[Binding.first] = Binding.second;
					LocalConstness[Binding.first] = CaptureConstness.at(Binding.first);
				}
				for (std::size_t Index = 0; Index < Lambda.Parameters.size(); ++Index)
				{
					LocalTypes[Lambda.Parameters[Index].Name] = ParamTypeNames[Index];
					LocalConstness[Lambda.Parameters[Index].Name] =
					    Lambda.Parameters[Index].bIsConst || Runtime::IsConstReferenceTypeName(ParamTypeNames[Index]);
				}
				if (!CaptureSlots.empty())
				{
					Out << "{\n";
					for (const auto& Name : SortedCaptures)
					{
						const auto Slot = CaptureSlots.find(Name);
						if (Slot == CaptureSlots.end())
						{
							continue;
						}

						Out << "\t" << (LocalConstness[Name] ? "const " : "") << "auto& " << Name << " = "
						    << (InitializerTypes.count(Name) ? "*" : "") << Slot->second << ";\n";
					}
				}
				if (Lambda.Body)
				{
					EmitStmt(*Lambda.Body, 0);
				}
				else if (Lambda.ExpressionBody)
				{
					Out << "{ return ";
					EmitCoercedValue(ReturnType, *Lambda.ExpressionBody);
					Out << "; }\n";
				}
				else
				{
					Out << "{}\n";
				}
				if (!CaptureSlots.empty())
				{
					Out << "}\n";
				}

				LocalTypes = SavedLocals;
				LocalConstness = SavedConstness;
				ActiveReturnType = SavedReturnType;
				Out << ')';
				if (HasInitializers)
				{
					Out << ";\n}())";
				}
			}
