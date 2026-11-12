// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		case EExprKind::Call:
			{
				const FCallExpr& Call = static_cast<const FCallExpr&>(Expression);
				const bool bIntrinsicCast = !Call.TypeArgs.empty() &&
				    (Call.Callee == "cast" || Call.Callee == "try_cast" || Call.Callee == "const_cast");
				if (!Call.CalleeExpr && !Call.bStandardLibraryQualified && !Call.bImplementationBuiltinAccess && !bIntrinsicCast &&
				    !ResolveLocalEnumConstantLinkageName(Call.Callee).empty())
				{
					ReportError("enum constant is not callable: " + Call.Callee, Expression.Location);
					return false;
				}
				const auto ParameterType = [&](const FFunctionDecl& Function, std::size_t Index)
				{
					auto Type = QualifyTypeName(Function.Parameters[Index].TypeName, Function.ModulePath);
					if (Type.find('[') != std::string::npos)
					{
						const auto Bound = SpecializedFreeFunctionParamTypes(Function, {});
						if (Index < Bound.size())
						{
							Type.assign(Runtime::StripReferenceTypeName(Bound[Index]));
							if (Type.size() >= 3 && Type.compare(Type.size() - 3, 3, "...") == 0)
							{
								Type.resize(Type.size() - 3);
							}
						}
					}
					return Type;
				};

				if (auto StaticCall = MakeLexicalStaticCall(Call))
				{
					return CompileExpression(*StaticCall);
				}
				if (!Call.CalleeExpr && IsForbiddenRuntimeCapture(Call.Callee))
				{
					ReportError("a local type cannot capture an enclosing runtime binding: " + Call.Callee, Expression.Location);
					return false;
				}
				if (Call.Callee == "variant" || Call.Callee == "variant_is" || Call.Callee == "variant_has_value" || Call.Callee == "variant_type_name")
				{
					return CompileVariantCall(Call, Emitter);
				}
				if (Call.GetArguments().size() > std::numeric_limits<std::uint8_t>::max())
				{
					ReportError("function call supports at most 255 arguments", Expression.Location);
					return false;
				}
				const auto TryEmitOperatorCall = [&](const FExpr& CalleeExpr,
				                                     const std::string& CalleeType) -> bool
				{
					if (!TypeSupportsOperatorOverload(CalleeType)) return false;
					std::vector<const FExpr*> Args;
					for (const FExpr* Arg : Call.GetArguments())
					{
						Args.push_back(Arg);
					}
					FResolvedScriptOperator Resolved;
					bool bAmbiguous = false;
					if (ResolveScriptOperator("operator()", CalleeExpr, Args, Resolved, bAmbiguous))
					{
						return CompileScriptOperator(Emitter, "operator()", CalleeExpr, Args, Resolved, Expression.Location);
					}
					if (bAmbiguous)
					{
						ReportErrorId("diag.ambiguous_overload_for_method", Expression.Location, {"operator()"});
					}
					return false;
				};

				if (Call.CalleeExpr)
				{
					std::string CalleeType;
					if (!TryResolveExprTypeName(*Call.CalleeExpr, CalleeType))
					{
						ReportError("expression callee is not a function value", Expression.Location);
						return false;
					}
					if (TypeSupportsOperatorOverload(CalleeType))
					{
						if (TryEmitOperatorCall(*Call.CalleeExpr, CalleeType))
						{
							return true;
						}
						if (HasError())
						{
							return false;
						}
					}
					if (!Runtime::IsFunctionTypeName(CalleeType))
					{
						ReportError("expression callee is not a function value", Expression.Location);
						return false;
					}
					std::vector<std::string> ParamTypes;
					std::string ReturnType;
					if (!Runtime::ParseFunctionType(CalleeType, ParamTypes, ReturnType))
					{
						ReportError("invalid function type: " + CalleeType, Expression.Location);
						return false;
					}
					if (Call.GetArguments().size() != ParamTypes.size())
					{
						ReportError(
						    "wrong argument count for function expression call",
						    Expression.Location);
						return false;
					}
					for (std::size_t ArgumentIndex = 0; ArgumentIndex < Call.GetArguments().size(); ++ArgumentIndex)
					{
						const FExpr& RawArgument = *Call.GetArguments()[ArgumentIndex];
						const std::string& EncodedParamType = ParamTypes[ArgumentIndex];
						const bool bConstRefParam =
						    EncodedParamType.rfind("const ref ", 0) == 0;
						const bool bRefParam = bConstRefParam || EncodedParamType.rfind("ref ", 0) == 0;
						const bool bRefArgument = RawArgument.Kind == EExprKind::Ref;
						if (bRefArgument != bRefParam)
						{
							ReportErrorId(
							    bRefParam
							        ? "diag.ref_parameter_requires_ref_argument"
							        : "diag.ref_argument_requires_ref_parameter",
							    Expression.Location);
							return false;
						}
						const std::string ValueParamType = bConstRefParam
						    ? EncodedParamType.substr(10)
						    : (bRefParam ? EncodedParamType.substr(4) : EncodedParamType);
						const FExpr& Argument = UnwrapRefOperand(RawArgument);
						if (bRefParam)
						{
							if (!ValidateRefArgumentType(
							        ValueParamType, Argument, Expression.Location) ||
							    !CompileRefArgument(
							        Emitter,
							        Argument,
							        Expression.Location,
							        !bConstRefParam))
							{
								return false;
							}
							continue;
						}
						const std::string PreviousExpectedType = ExpressionExpectedType;
						ExpressionExpectedType = ValueParamType;
						const bool bCompiledArg = CompileExpression(Argument);
						const bool bCoerced = bCompiledArg && EmitValueCoercionForTarget(Emitter, ValueParamType, Argument, Expression.Location);
						if (bCoerced && NeedsPassByValueCopy(ValueParamType) && !CanTransferCoercedValue(Argument, ValueParamType))
						{
							EmitValueCopy(Emitter, ValueParamType);
						}
						ExpressionExpectedType = PreviousExpectedType;
						if (!bCompiledArg || !bCoerced)
						{
							return false;
						}
					}
					if (!CompileExpression(*Call.CalleeExpr))
					{
						return false;
					}
					Emitter.EmitCallIndirect(static_cast<std::uint8_t>(Call.GetArguments().size()));
					return true;
				}
				if (!Call.TypeArgs.empty())
				{
					const auto* Declaration = FindUniqueFreeFunction(Call.Callee, CurrentModulePath);
					if (Declaration && IsMethodBindingFactoryDeclaration(*Declaration))
					{
						return CompileMethodBindingFactory(Call, Emitter);
					}
				}
				std::string EffectiveCallee = SubstituteTypeName(Call.Callee);
				if (!Call.TypeArgs.empty() || (Call.bExplicitTemplateArguments && IsTypeNameWithoutValueBinding(Call.Callee)))
				{
					// Reconstruct Foo<T,U> so arity-overloaded type ctors resolve correctly.
					std::string TypeApp = EffectiveCallee;
					TypeApp.push_back('<');
					for (std::size_t TypeArgIndex = 0; TypeArgIndex < Call.TypeArgs.size(); ++TypeArgIndex)
					{
						if (TypeArgIndex > 0)
						{
							TypeApp.push_back(',');
						}
						TypeApp += Call.TypeArgs[TypeArgIndex];
					}
					TypeApp.push_back('>');
					EffectiveCallee = SubstituteTypeName(TypeApp);
				}
				{
					const std::string ExpandedCallee =
					    ExpandUserTypeAlias(EffectiveCallee, CurrentModulePath);
					if (ExpandedCallee != EffectiveCallee)
					{
						EffectiveCallee = ExpandedCallee;
					}
				}
				if (Call.TypeArgs.empty() && CanonicalTypeNameForCoercion(EffectiveCallee) == "variant")
				{
					return CompileVariantCall(Call, Emitter);
				}
				// The callable overload is a compiler intrinsic: script variadics cannot
				// describe its heterogeneous argument list. Keep the string overload on
				// the ordinary imported function path.
				if (Call.bStandardLibraryQualified && (Call.Callee == "__std_spawn" || Call.Callee == "__std_spawn_task") && !Call.GetArguments().empty())
				{
					std::string CallableType;
					const FFunctionDecl* DirectFunction = nullptr;
					std::uint16_t DirectStaticFunctionIndex =
					    static_cast<std::uint16_t>(-1);
					bool bHaveCallableType =
					    TryResolveExprTypeName(*Call.GetArguments().front(), CallableType) && Runtime::IsFunctionTypeName(CallableType);
					if (!bHaveCallableType && Call.GetArguments().front()->Kind == EExprKind::Identifier)
					{
						const std::string& Name = static_cast<const FIdentifierExpr&>(
						    *Call.GetArguments().front())
						                              .Name;
						if (FindFunctionDeclForReference(Name, CurrentModulePath))
						{
							std::vector<std::string> SuppliedTypes;
							for (std::size_t Index = 1; Index < Call.GetArguments().size(); ++Index)
							{
								std::string SuppliedType;
								(void)TryResolveExprTypeName(
								    *Call.GetArguments()[Index], SuppliedType);
								SuppliedTypes.push_back(std::move(SuppliedType));
							}
							if (!ResolveFunctionForCall(
							        Name, SuppliedTypes, DirectFunction,
							        Expression.Location))
							{
								return false;
							}
							if (DirectFunction && TemplateParameterNames(*DirectFunction).empty() && !DirectFunction->bDeleted)
							{
								CallableType = MakeFunctionTypeFromDecl(*DirectFunction);
								bHaveCallableType = true;
							}
						}
					}
					if (!bHaveCallableType && Call.GetArguments().front()->Kind == EExprKind::MemberAccess)
					{
						const FMemberAccessExpr& Access =
						    static_cast<const FMemberAccessExpr&>(
						        *Call.GetArguments().front());
						std::string OwnerName;
						if (TryBuildQualifiedExpressionName(*Access.Object, OwnerName) && IsTypeNameWithoutValueBinding(OwnerName))
						{
							std::vector<std::string> SuppliedTypes;
							for (std::size_t Index = 1; Index < Call.GetArguments().size(); ++Index)
							{
								std::string SuppliedType;
								(void)TryResolveExprTypeName(
								    *Call.GetArguments()[Index], SuppliedType);
								SuppliedTypes.push_back(std::move(SuppliedType));
							}
							std::string MethodOwner;
							const FMethodDecl* Method = nullptr;
							if (!ResolveMethodForCall(
							        OwnerName, Access.Member, SuppliedTypes,
							        MethodOwner, Method, Expression.Location,
							        false, true))
							{
								return false;
							}
							if (!Method || !Method->bIsStatic || Method->bDeleted || !ValidateMemberAccess(Expression.Location, OwnerName, Access.Member, true, SuppliedTypes.size(), Method))
							{
								if (!HasError())
								{
									ReportError("callable task requires an accessible static method",
									    Expression.Location);
								}
								return false;
							}
							const std::string DeclModule = ResolveOwnerModulePath(OwnerName);
							const std::vector<std::string> MethodParams =
							    CanonicalMethodParamTypeNames(*Method, DeclModule, {});
							const std::string MethodReturn = Method->ReturnType.empty()
							    ? "void"
							    : QualifyTypeName(
							          NormalizeTypeAlias(SubstituteTypeName(Method->ReturnType)),
							          DeclModule);
							CallableType = Runtime::MakeFunctionTypeName(
							    MethodParams, MethodReturn);
							DirectStaticFunctionIndex = FindFunctionIndex(
							    MakeMethodFunctionName(
							        MethodOwner, Access.Member, MethodParams,
							        Method->bIsConst));
							if (DirectStaticFunctionIndex == static_cast<std::uint16_t>(-1))
							{
								ReportError("callable task static method is not linked",
								    Expression.Location);
								return false;
							}
							bHaveCallableType = true;
						}
					}
					if (bHaveCallableType)
					{
						const bool bWithResult = Call.Callee == "__std_spawn_task";
						if (!FindFunctionDeclForReference(Call.Callee, CurrentModulePath))
						{
							ReportError("callable task entry requires importing concurrent.vs",
							    Expression.Location);
							return false;
						}
						if ((bWithResult && Call.TypeArgs.size() != 1) || (!bWithResult && !Call.TypeArgs.empty()))
						{
							ReportError("callable spawn_task requires exactly one result type",
							    Expression.Location);
							return false;
						}
						if (bWithResult && Call.GetArguments().size() == 255)
						{
							ReportError("callable spawn_task supports at most 253 arguments",
							    Expression.Location);
							return false;
						}
						std::vector<std::string> ParamTypes;
						std::string ReturnType;
						if (!Runtime::ParseFunctionType(
						        CallableType, ParamTypes, ReturnType) ||
						    ParamTypes.size() + 1 != Call.GetArguments().size())
						{
							ReportError("callable task argument count does not match its signature",
							    Expression.Location);
							return false;
						}
						std::string TaskLinkage;
						if (bWithResult)
						{
							const std::string ResultType = QualifyTypeName(
							    NormalizeTypeAlias(SubstituteTypeName(Call.TypeArgs.front())),
							    CurrentModulePath);
							if (!Runtime::IsAllowedChannelElementType(ResultType) || ResultType != ReturnType)
							{
								ReportError("callable task result must exactly match a transferable T",
								    Expression.Location);
								return false;
							}
							std::uint16_t TaskTypeIndex = static_cast<std::uint16_t>(-1);
							if (!EnsureStructTypeResolved(
							        "task<" + ResultType + ">",
							        TaskLinkage,
							        &TaskTypeIndex,
							        Expression.Location) ||
							    TaskTypeIndex == static_cast<std::uint16_t>(-1))
							{
								return false;
							}
						}
						else if (ReturnType != "void")
						{
							ReportError("callable spawn requires a void return type",
							    Expression.Location);
							return false;
						}
						for (const std::string& ParamType : ParamTypes)
						{
							if (!Runtime::IsAllowedChannelElementType(ParamType))
							{
								ReportError("callable task parameter is not transferable: " + ParamType, Expression.Location);
								return false;
							}
						}
						if (Call.GetArguments().front()->Kind == EExprKind::Lambda)
						{
							const FLambdaExpr& Lambda =
							    static_cast<const FLambdaExpr&>(*Call.GetArguments().front());
							if (Lambda.CaptureKind == ELambdaCaptureKind::Reference || !Lambda.GetCaptureNames(true).empty())
							{
								ReportError("callable task cannot capture by reference",
								    Lambda.Location);
								return false;
							}
						}
						if (bWithResult)
						{
							FBytecodeConstant TypeArgument;
							TypeArgument.Kind = EConstantKind::Str;
							TypeArgument.bIsLinkageString = true;
							TypeArgument.StringIndex = AddString(TaskLinkage);
							Emitter.EmitU16Operand(
							    EOpcode::ConstStr, AddConstant(TypeArgument));
						}
						if (DirectStaticFunctionIndex != static_cast<std::uint16_t>(-1))
						{
							Module->Functions[DirectStaticFunctionIndex].ClosureTypeName =
							    CallableType;
							Emitter.EmitMakeClosure(DirectStaticFunctionIndex, 0);
						}
						else if (DirectFunction)
						{
							const std::uint16_t FunctionIndex = FindFunctionIndex(
							    MakeFreeFunctionLinkageName(*DirectFunction));
							if (FunctionIndex == static_cast<std::uint16_t>(-1))
							{
								ReportError("callable task function is not linked",
								    Expression.Location);
								return false;
							}
							Module->Functions[FunctionIndex].ClosureTypeName = CallableType;
							Emitter.EmitMakeClosure(FunctionIndex, 0);
						}
						else
						{
							const FLambdaExpr* const Previous = IsolatedCallableLambda;
							IsolatedCallableLambda =
							    Call.GetArguments().front()->Kind == EExprKind::Lambda
							    ? static_cast<const FLambdaExpr*>(Call.GetArguments().front())
							    : nullptr;
							const bool bCompiled = CompileExpression(*Call.GetArguments().front());
							IsolatedCallableLambda = Previous;
							if (!bCompiled)
							{
								return false;
							}
						}
						for (std::size_t Index = 0; Index < ParamTypes.size(); ++Index)
						{
							const FExpr& Argument = *Call.GetArguments()[Index + 1];
							if (Argument.Kind == EExprKind::Ref || !CompileExpression(Argument) || !EmitValueCoercionForTarget(Emitter, ParamTypes[Index], Argument, Expression.Location))
							{
								if (!HasError())
								{
									ReportError("callable task argument must be a transferable value",
									    Argument.Location);
								}
								return false;
							}
						}
						if (Call.GetArguments().front()->Kind == EExprKind::Lambda && static_cast<const FLambdaExpr&>(*Call.GetArguments().front()).bCapturesThis.load(std::memory_order_acquire))
						{
							ReportError("callable task cannot capture this",
							    Expression.Location);
							return false;
						}
						Emitter.EmitCallBuiltin(
						    bWithResult ? EBuiltinId::SpawnCallableResult
						                : EBuiltinId::SpawnCallable,
						    static_cast<std::uint8_t>(Call.GetArguments().size() + (bWithResult ? 1 : 0)));
						return true;
					}
				}
				// The generic spelling is the intrinsic; an ordinary user function named
				// cast/try_cast/const_cast remains callable without type arguments.
				if (Call.Callee == "const_cast" && !Call.TypeArgs.empty())
				{
					if (Call.TypeArgs.size() != 1)
					{
						ReportErrorId(
						    "diag.wrong_number_of_type_arguments",
						    Expression.Location,
						    {Call.Callee, "1", std::to_string(Call.TypeArgs.size())});
						return false;
					}
					if (Call.GetArguments().size() != 1)
					{
						ReportErrorId(
						    "diag.name_expects_exactly_n_arguments",
						    Expression.Location,
						    {Call.Callee, "1"});
						return false;
					}
					const FExpr& Argument = *Call.GetArguments().front();
					if (Argument.Kind == EExprKind::Ref)
					{
						ReportErrorId("diag.builtin_does_not_accept_ref_arguments", Expression.Location);
						return false;
					}
					std::string TargetType = SubstituteTypeName(Call.TypeArgs.front());
					TargetType = QualifyTypeName(
					    NormalizeTypeAlias(TargetType, CurrentModulePath), CurrentModulePath);
					std::string SourceType;
					if (!TryResolveExprTypeName(Argument, SourceType))
					{
						ReportErrorId(
						    "diag.could_not_resolve_const_cast_source_type",
						    Expression.Location);
						return false;
					}
					SourceType = CanonicalTypeNameForCoercion(SourceType);
					const std::string CanonicalTarget = CanonicalTypeNameForCoercion(TargetType);
					if (SourceType != CanonicalTarget)
					{
						ReportErrorId(
						    "diag.const_cast_cannot_change_type",
						    Expression.Location,
						    {DisplayTypeName(SourceType), DisplayTypeName(CanonicalTarget)});
						return false;
					}
					if (!IsConstAssignTarget(Argument))
					{
						ReportErrorId(
						    "diag.const_cast_source_is_not_const",
						    Expression.Location);
						return false;
					}
					// Constness is a compile-time access capability. The VM value is already
					// the underlying T, so removing that capability emits no bytecode.
					return CompileExpression(Argument);
				}
				if ((Call.Callee == "cast" || Call.Callee == "try_cast") && !Call.TypeArgs.empty())
				{
					const bool bTryCast = Call.Callee == "try_cast";
					if (Call.TypeArgs.size() != 1)
					{
						ReportErrorId(
						    "diag.wrong_number_of_type_arguments",
						    Expression.Location,
						    {Call.Callee, "1", std::to_string(Call.TypeArgs.size())});
						return false;
					}
					if (Call.GetArguments().size() != 1)
					{
						ReportErrorId(
						    "diag.name_expects_exactly_n_arguments",
						    Expression.Location,
						    {Call.Callee, "1"});
						return false;
					}
					const FExpr& Argument = *Call.GetArguments().front();
					if (Argument.Kind == EExprKind::Ref)
					{
						ReportErrorId("diag.builtin_does_not_accept_ref_arguments", Expression.Location);
						return false;
					}

					std::string TargetType = SubstituteTypeName(Call.TypeArgs.front());
					TargetType = QualifyTypeName(
					    NormalizeTypeAlias(TargetType, CurrentModulePath), CurrentModulePath);
					const std::string RuntimeTargetType =
					    CanonicalTypeNameForCoercion(TargetType);
					std::string SourceType;
					if (!TryResolveExprTypeName(Argument, SourceType))
					{
						ReportError(
						    "could not resolve source type for " + Call.Callee,
						    Expression.Location);
						return false;
					}
					SourceType = CanonicalTypeNameForCoercion(SourceType);
					if (RuntimeTargetType == "variant")
					{
						if (bTryCast)
						{
							ReportErrorId("diag.try_cast_variant_target_is_meaningless", Expression.Location);
							return false;
						}
						return CompileExpression(Argument) && EmitValueCoercionForTarget(Emitter, "variant", Argument, Expression.Location);
					}
					if (SourceType == "variant")
					{
						if (Runtime::IsReferenceTypeName(TargetType) || TargetType == "void" || TargetType == "nullptr" || !ValidateFieldTypeName(TargetType, Expression.Location))
						{
							if (!HasError())
							{
								ReportError("variant extraction requires a concrete value target", Expression.Location);
							}
							return false;
						}
						std::string OptionalLinkage;
						std::uint16_t OptionalIndex = 0;
						if (bTryCast && !EnsureStructTypeResolved("optional<" + TargetType + ">", OptionalLinkage, &OptionalIndex, Expression.Location)) return false;
						if (!CompileExpression(Argument)) return false;
						if (bTryCast)
						{
							Emitter.EmitU16U16Operands(EOpcode::TryCast, AddString(RuntimeTargetType), OptionalIndex);
						}
						else
						{
							Emitter.EmitU16Operand(EOpcode::CheckedCast, AddString(RuntimeTargetType));
						}
						return true;
					}
					if (SourceType == "void" || TargetType == "void")
					{
						ReportError("cast source and target must be value types", Expression.Location);
						return false;
					}

					const auto IsScriptReference = [this](const std::string& TypeName)
					{
						return (IsClassOrStructType(TypeName) && !IsStructType(TypeName)) || IsInterfaceTypeName(TypeName);
					};
					const auto IsHostReference = [this](const std::string& TypeName)
					{
						const FHostTypeDesc* const HostType =
						    ActiveBindings ? ActiveBindings->FindType(TypeName) : nullptr;
						return HostType && HostType->Kind == EHostTypeKind::Class;
					};
					const bool bSourceReference = IsScriptReference(SourceType) || IsHostReference(SourceType);
					const bool bTargetReference = IsScriptReference(RuntimeTargetType) || IsHostReference(RuntimeTargetType);
					const bool bNullSource = SourceType == "nullptr";
					const bool bSourceEnum = IsScopedEnumTypeName(SourceType);
					const bool bTargetEnum = IsScopedEnumTypeName(RuntimeTargetType);
					const bool bStructToInterface = IsStructType(SourceType) && IsInterfaceTypeName(RuntimeTargetType) && IsSubtypeOf(SourceType, RuntimeTargetType);
					const bool bInterfaceToStruct = IsInterfaceTypeName(SourceType) && IsStructType(RuntimeTargetType);

					if (bTryCast)
					{
						const bool bNumericCast =
						    (Runtime::IsPrimitiveValueTypeName(SourceType) && Runtime::IsPrimitiveValueTypeName(RuntimeTargetType));
						const bool bSupported = bNumericCast || bSourceEnum || bTargetEnum || (bSourceReference && bTargetReference) || bStructToInterface || bInterfaceToStruct || (bNullSource && bTargetReference);
						if (!bSupported)
						{
							ReportError(
							    "try_cast supports primitive, scoped-enum, and reference conversions only",
							    Expression.Location);
							return false;
						}
						std::string OptionalLinkage;
						std::uint16_t OptionalTypeIndex = static_cast<std::uint16_t>(-1);
						const std::string OptionalType = "optional<" + TargetType + ">";
						if (!EnsureStructTypeResolved(
						        OptionalType,
						        OptionalLinkage,
						        &OptionalTypeIndex,
						        Expression.Location))
						{
							ReportError("could not resolve " + OptionalType, Expression.Location);
							return false;
						}
						if (!CompileExpression(Argument))
						{
							return false;
						}
						if (bStructToInterface)
						{
							// Interface values retain class identity, but structs enter an
							// interface as an independent value box.
							if (!CanTransferCoercedValue(Argument, SourceType))
							{
								EmitValueCopy(Emitter, SourceType);
							}
						}
						Emitter.EmitU16U16Operands(
						    EOpcode::TryCast,
						    AddString(RuntimeTargetType),
						    OptionalTypeIndex);
						return true;
					}

					if (IsStructType(RuntimeTargetType) && (SourceType == RuntimeTargetType || (!IsInterfaceTypeName(SourceType) && IsSubtypeOf(SourceType, RuntimeTargetType))))
					{
						if (!CompileExpression(Argument)) return false;
						if (!ExpressionYieldsUnaliasedByValue(Argument, RuntimeTargetType))
						{
							EmitValueCopy(Emitter, RuntimeTargetType, true);
						}
						return true;
					}
					// A statically safe up-cast needs no runtime work. Every other cast
					// within one reference domain is checked at runtime: this covers both
					// ordinary down-casts and interface cross-casts whose concrete object
					// type is only known while executing.
					const bool bSameReferenceDomain =
					    (IsScriptReference(SourceType) && IsScriptReference(RuntimeTargetType)) || (IsHostReference(SourceType) && IsHostReference(RuntimeTargetType));
					const bool bCheckedReferenceCast = bSameReferenceDomain && !IsSubtypeOf(SourceType, RuntimeTargetType);
					if (bCheckedReferenceCast || bInterfaceToStruct || bSourceEnum || bTargetEnum)
					{
						const bool bValidEnumCast = !bSourceEnum || !bTargetEnum || SourceType == RuntimeTargetType;
						if (!bValidEnumCast)
						{
							ReportError(
							    "cast between distinct scoped enums requires an explicit underlying integer conversion",
							    Expression.Location);
							return false;
						}
						if (!CompileExpression(Argument))
						{
							return false;
						}
						Emitter.EmitU16Operand(EOpcode::CheckedCast, AddString(RuntimeTargetType));
						return true;
					}

					// cast<T>(value) is also the explicit construction context for T.
					// Resolve this before ordinary assignment coercion so an `explicit`
					// single-argument constructor is usable here but nowhere implicitly.
					if (IsClassOrStructType(RuntimeTargetType) && SourceType != RuntimeTargetType)
					{
						const FConstructorDecl* Constructor = nullptr;
						std::string ConstructorKey;
						bool bAmbiguous = false;
						if (ResolveConstructorForCall(
						        RuntimeTargetType,
						        {SourceType},
						        Constructor,
						        ConstructorKey,
						        Expression.Location,
						        false,
						        &bAmbiguous,
						        true,
						        false) &&
						    Constructor && Constructor->Parameters.size() == 1 && !Constructor->Parameters.front().bIsRef && !Constructor->Parameters.front().bIsVariadic)
						{
							const std::uint16_t ConstructorIndex = FindFunctionIndex(ConstructorKey);
							const std::uint16_t TypeIndex = FindOrAddType(RuntimeTargetType);
							if (ConstructorIndex == static_cast<std::uint16_t>(-1) || TypeIndex == static_cast<std::uint16_t>(-1) || !RequireConcreteType(RuntimeTargetType, Expression.Location))
							{
								return false;
							}
							std::unordered_map<std::string, std::string> OwnerSubst;
							if (!TryBuildGenericOwnerSubst(
							        RuntimeTargetType, OwnerSubst, Expression.Location))
							{
								return false;
							}
							const std::string ParameterType = OwnerSubst.empty()
							    ? SubstituteTypeName(Constructor->Parameters.front().TypeName)
							    : SubstituteTypeNameWith(
							          Constructor->Parameters.front().TypeName, OwnerSubst);
							Emitter.EmitU16Operand(EOpcode::NewObject, TypeIndex);
							// Preserve value construction copies; class results stay on the stack.
							const std::uint16_t ObjectTemp = Module->Types[TypeIndex].Kind == EScriptObjectKind::Class
							    ? static_cast<std::uint16_t>(-1)
							    : AllocateAssignTemp();
							Emitter.Emit(EOpcode::Dup);
							if (ObjectTemp != static_cast<std::uint16_t>(-1))
							{
								Emitter.EmitU16Operand(EOpcode::StoreLocal, ObjectTemp);
							}
							if (!CompileExpression(Argument) || !EmitValueCoercionBetweenTypes(Emitter, ParameterType, SourceType, Expression.Location, false, IsConstAssignTarget(Argument)))
							{
								return false;
							}
							if (NeedsPassByValueCopy(ParameterType) && !CanTransferCoercedValue(Argument, ParameterType))
							{
								EmitValueCopy(Emitter, ParameterType);
							}
							Emitter.EmitCallBoundMethod(ConstructorIndex, 1);
							Emitter.Emit(EOpcode::Pop);
							if (ObjectTemp != static_cast<std::uint16_t>(-1))
							{
								Emitter.EmitU16Operand(EOpcode::TakeLocal, ObjectTemp);
							}
							return true;
						}
						if (bAmbiguous)
						{
							ReportErrorId(
							    "diag.ambiguous_overload_for_method",
							    Expression.Location,
							    {DisplayTypeName(RuntimeTargetType)});
							return false;
						}
					}

					if (!CompileExpression(Argument))
					{
						return false;
					}
					return EmitValueCoercionBetweenTypes(
					    Emitter,
					    RuntimeTargetType,
					    SourceType,
					    Expression.Location,
					    true, IsConstAssignTarget(Argument));
				}
				if ((Call.Callee == "print" && Call.GetArguments().size() > 1) || Call.Callee == "printf" || Call.Callee == "format" || Call.Callee == "__format" || Call.Callee == "formatf" || Call.Callee == "__format_printf")
				{
					return CompileFormatFamilyCall(Call, Emitter);
				}
				{
					std::string CalleeType;
					std::uint16_t CalleeLocalSlot = 0;
					std::uint16_t CalleeGlobalSlot = 0;
					const bool bHasLocal = ResolveLocal(Call.Callee, CalleeLocalSlot);
					if (bHasLocal)
					{
						if (!TryResolveVariableTypeName(Call.Callee, CalleeType))
						{
							ReportError(
							    "'" + Call.Callee + "' is not a function value",
							    Expression.Location);
							return false;
						}
						if (TypeSupportsOperatorOverload(CalleeType))
						{
							auto CalleeIdent = std::make_unique<FIdentifierExpr>(Call.Callee);
							CalleeIdent->Location = Expression.Location;
							if (TryEmitOperatorCall(*CalleeIdent, CalleeType))
							{
								return true;
							}
							if (HasError())
							{
								return false;
							}
						}
						if (!Runtime::IsFunctionTypeName(CalleeType))
						{
							ReportError(
							    "'" + Call.Callee + "' is not a function value",
							    Expression.Location);
							return false;
						}
					}
					bool bHasGlobalFn = false;
					if (!bHasLocal)
					{
						const std::string GlobalLinkageName =
						    ResolveGlobalLinkageName(Call.Callee, CurrentModulePath);
						if (!GlobalLinkageName.empty() && ResolveGlobal(GlobalLinkageName, CalleeGlobalSlot) && TryResolveVariableTypeName(Call.Callee, CalleeType))
						{
							if (TypeSupportsOperatorOverload(CalleeType))
							{
								auto CalleeIdent = std::make_unique<FIdentifierExpr>(Call.Callee);
								CalleeIdent->Location = Expression.Location;
								if (TryEmitOperatorCall(*CalleeIdent, CalleeType))
								{
									return true;
								}
								if (HasError())
								{
									return false;
								}
							}
							if (!Runtime::IsFunctionTypeName(CalleeType))
							{
								ReportError(
								    "'" + Call.Callee + "' is not a function value",
								    Expression.Location);
								return false;
							}
							bHasGlobalFn = true;
						}
					}
					if (bHasLocal || bHasGlobalFn)
					{
						std::vector<std::string> ParamTypes;
						std::string ReturnType;
						if (!Runtime::ParseFunctionType(CalleeType, ParamTypes, ReturnType))
						{
							ReportError("invalid function type: " + CalleeType, Expression.Location);
							return false;
						}
						if (Call.GetArguments().size() != ParamTypes.size())
						{
							ReportErrorId(
							    "diag.wrong_argument_count_for_function",
							    Expression.Location,
							    {Call.Callee});
							return false;
						}
						for (std::size_t ArgumentIndex = 0; ArgumentIndex < Call.GetArguments().size(); ++ArgumentIndex)
						{
							const FExpr& RawArgument = *Call.GetArguments()[ArgumentIndex];
							const std::string& EncodedParamType = ParamTypes[ArgumentIndex];
							const bool bConstRefParam =
							    EncodedParamType.rfind("const ref ", 0) == 0;
							const bool bRefParam = bConstRefParam || EncodedParamType.rfind("ref ", 0) == 0;
							const bool bRefArgument = RawArgument.Kind == EExprKind::Ref;
							if (bRefArgument != bRefParam)
							{
								ReportErrorId(
								    bRefParam
								        ? "diag.ref_parameter_requires_ref_argument"
								        : "diag.ref_argument_requires_ref_parameter",
								    Expression.Location);
								return false;
							}
							const std::string ValueParamType = bConstRefParam
							    ? EncodedParamType.substr(10)
							    : (bRefParam ? EncodedParamType.substr(4) : EncodedParamType);
							const FExpr& Argument = UnwrapRefOperand(RawArgument);
							if (bRefParam)
							{
								if (!ValidateRefArgumentType(
								        ValueParamType, Argument, Expression.Location) ||
								    !CompileRefArgument(
								        Emitter,
								        Argument,
								        Expression.Location,
								        !bConstRefParam))
								{
									return false;
								}
								continue;
							}
							const std::string PreviousExpectedType = ExpressionExpectedType;
							ExpressionExpectedType = ValueParamType;
							const bool bCompiledArg = CompileExpression(Argument);
							const bool bCoerced = bCompiledArg && EmitValueCoercionForTarget(Emitter, ValueParamType, Argument, Expression.Location);
							if (bCoerced && NeedsPassByValueCopy(ValueParamType) && !CanTransferCoercedValue(Argument, ValueParamType))
							{
								EmitValueCopy(Emitter, ValueParamType);
							}
							ExpressionExpectedType = PreviousExpectedType;
							if (!bCompiledArg || !bCoerced)
							{
								return false;
							}
						}
						if (bHasLocal)
						{
							Emitter.EmitU16Operand(EOpcode::LoadLocal, CalleeLocalSlot);
						}
						else
						{
							Emitter.EmitU16Operand(EOpcode::LoadGlobal, CalleeGlobalSlot);
						}
						Emitter.EmitCallIndirect(static_cast<std::uint8_t>(Call.GetArguments().size()));
						return true;
					}
				}
				const bool bBuiltinAccessible =
				    Call.bStandardLibraryQualified || Call.bImplementationBuiltinAccess || StandardLibrary::RequiredNamespaceForFunction(Call.Callee).empty();
				if (const Builtins::FDescriptor* const Builtin =
				        bBuiltinAccessible ? Builtins::Lookup(Call.Callee) : nullptr)
				{
					if (Builtin->Id == EBuiltinId::MethodBindingInvoke)
					{
						return CompileBoundMethodInvoke(Call, Emitter);
					}

					if ((Builtin->Id == EBuiltinId::MethodBindingResolve ||
					        Builtin->Id == EBuiltinId::MethodBindingValidate) &&
					    !Call.bImplementationBuiltinAccess)
					{
						ReportError("method binding runtime intrinsics are internal", Call.Location);
						return false;
					}
					const bool bReflectionIntrinsic =
					    (Builtin->Id >= EBuiltinId::MemoryAvailable && Builtin->Id <= EBuiltinId::MemoryWrite)
					    ? false
					    : Builtin->Id == EBuiltinId::ReflectionString ||
					        Builtin->Id == EBuiltinId::ReflectionInt ||
					        Builtin->Id == EBuiltinId::ReflectionBool ||
					        Builtin->Id == EBuiltinId::ReflectionInt64 ||
					        Builtin->Id == EBuiltinId::ReflectionBind ||
					        Builtin->Id == EBuiltinId::ReflectionGet ||
					        Builtin->Id == EBuiltinId::ReflectionSet ||
					        Builtin->Id == EBuiltinId::ReflectionInvoke ||
					        Builtin->Id == EBuiltinId::ReflectionInvokeStatic ||
					        (Builtin->Id >= EBuiltinId::SourceMetadataString && Builtin->Id <= EBuiltinId::SourceMetadataDefinition);
					if ((Builtin->Id >= EBuiltinId::MemoryAvailable && Builtin->Id <= EBuiltinId::MemoryWrite))
					{
						return CompileMemoryCall(Call, Builtin->Id, Emitter);
					}
					if (Builtin->Id == EBuiltinId::ScalarParse || Builtin->Id == EBuiltinId::ScalarTryParse || Builtin->Id == EBuiltinId::NumericTypeValidate || (Builtin->Id >= EBuiltinId::MakeSynchronized && Builtin->Id <= EBuiltinId::SynchronizedFetchAdd) || Builtin->Id == EBuiltinId::TypedTaskAwait || Builtin->Id == EBuiltinId::TypedTaskAwaitFor || Builtin->Id == EBuiltinId::TaskGroupAwaitAll || Builtin->Id == EBuiltinId::TaskArrayAwaitAll)
					{
						return CompileTypedStandardLibraryCall(Call, Builtin->Id, Emitter);
					}
					if (Builtin->Id == EBuiltinId::TryReadLine || Builtin->Id == EBuiltinId::TryReadToken || Builtin->Id == EBuiltinId::TryGetEnv)
					{
						return CompileStandardLibraryOptionalCall(Call, Builtin->Id, Emitter);
					}
					if (bReflectionIntrinsic)
					{
						bool bInsideReflectionType = false;
						if (CurrentFunction && Module && CurrentFunction->OwnerTypeIndex < Module->Types.size())
						{
							const std::string Owner = ModuleMeta::TypeSimpleName(
							    Module->Types[CurrentFunction->OwnerTypeIndex].Name);
							bInsideReflectionType = Owner == "type" || Owner == "field_info" || Owner == "method_info" || Owner == "module_info" || Owner == "function_info" || Owner == "global_info" || Owner == "enum_member_info" || Owner == "source_node_info" || Owner == "annotation_info" || Owner == "annotation_argument_info" || Owner == "comment_info" || Owner == "annotation_definition_info" || Owner.rfind("type<", 0) == 0 || Owner.rfind("field_info<", 0) == 0 || Owner.rfind("method_info<", 0) == 0 || Owner.rfind("enum_member_info<", 0) == 0;
						}
						if (!bInsideReflectionType)
						{
							ReportError(
							    "reflection runtime intrinsics are internal",
							    Expression.Location);
							return false;
						}
					}
					if (!Call.TypeArgs.empty())
					{
						ReportError(
						    "explicit type arguments require a generic function",
						    Expression.Location);
						return false;
					}
					const std::uint8_t BuiltinArgumentCount =
					    static_cast<std::uint8_t>(Call.GetArguments().size());
					if (!Builtin->AcceptsArgCount(BuiltinArgumentCount))
					{
						if (Builtin->ArgCount != Builtin->MaxArgCount)
						{
							ReportErrorId(
							    "diag.name_expects_between_n_and_m_arguments",
							    Expression.Location,
							    {std::string(Builtin->Name),
							        std::to_string(Builtin->ArgCount),
							        std::to_string(Builtin->MaxArgCount)});
						}
						else
						{
							ReportErrorId("diag.name_expects_exactly_n_arguments", Expression.Location, {std::string(Builtin->Name), std::to_string(Builtin->ArgCount)});
						}
						return false;
					}
					for (const FExpr* Argument : Call.GetArguments())
					{
						if (Argument->Kind == EExprKind::Ref)
						{
							ReportErrorId("diag.builtin_does_not_accept_ref_arguments", Expression.Location);
							return false;
						}
						std::string ArgType;
						if (TryResolveExprTypeName(*Argument, ArgType) && ArgType == "void")
						{
							ReportError("cannot use void value in expression", Expression.Location);
							return false;
						}
					}
					for (std::size_t I = 0; I < Call.GetArguments().size(); ++I)
					{
						const bool Integer = Builtin->Id == EBuiltinId::MakeSemaphore || (I == 1 && (Builtin->Id == EBuiltinId::StringRepeat || Builtin->Id == EBuiltinId::ReadBytes || Builtin->Id == EBuiltinId::MutexLockFor || Builtin->Id == EBuiltinId::SemaphoreAcquireFor || Builtin->Id == EBuiltinId::ArrayRotate || Builtin->Id == EBuiltinId::RandomIndex));
						if (!Integer)
						{
							continue;
						}
						std::string Type;
						if (!TryResolveExprTypeName(*Call.GetArguments()[I], Type) || !StandardLibrary::IsScalarParseType(CanonicalTypeNameForCoercion(Type), true))
						{
							ReportErrorId("diag.stdlib_arguments", Expression.Location);
							return false;
						}
					}
					if (Builtin->Id == EBuiltinId::ConsoleSetColor || Builtin->Id == EBuiltinId::ConsoleClearLine || Builtin->Id == EBuiltinId::ConsoleSetCursorVisible || Builtin->Id == EBuiltinId::ConsoleMoveCursor)
					{
						for (const FExpr* Argument : Call.GetArguments())
						{
							std::string Type;
							if (!TryResolveExprTypeName(*Argument, Type))
							{
								continue;
							}

							const bool bValid = Builtin->Id == EBuiltinId::ConsoleMoveCursor
							    ? IsSignedIntegerTypeName(Type) || Runtime::IsUnsignedTypeName(Type)
							    : Type ==
							        (Builtin->Id == EBuiltinId::ConsoleSetCursorVisible ? "bool" : "string");
							if (!bValid)
							{
								ReportError("std::console: invalid argument type", Argument->Location);
								return false;
							}
						}
					}
					if (Builtin->Id == EBuiltinId::ToWString || Builtin->Id == EBuiltinId::ToUnsigned || Builtin->Id == EBuiltinId::ToInt || Builtin->Id == EBuiltinId::ToDouble || Builtin->Id == EBuiltinId::ToFloat || Builtin->Id == EBuiltinId::Min || Builtin->Id == EBuiltinId::Max || Builtin->Id == EBuiltinId::Split || Builtin->Id == EBuiltinId::Join || Builtin->Id == EBuiltinId::Assert || Builtin->Id == EBuiltinId::Exit || Builtin->Id == EBuiltinId::Sleep || Builtin->Id == EBuiltinId::Format || Builtin->Id == EBuiltinId::FormatPrintf || Builtin->Id == EBuiltinId::SetEnv || Builtin->Id == EBuiltinId::Range || Builtin->Id == EBuiltinId::GetEnv || Builtin->Id == EBuiltinId::MkDir || Builtin->Id == EBuiltinId::ChDir || Builtin->Id == EBuiltinId::WriteFile || Builtin->Id == EBuiltinId::AppendFile || Builtin->Id == EBuiltinId::ReadFile || Builtin->Id == EBuiltinId::Exists || Builtin->Id == EBuiltinId::DeleteFile || Builtin->Id == EBuiltinId::IsFile || Builtin->Id == EBuiltinId::IsDirectory || Builtin->Id == EBuiltinId::RemoveDir || Builtin->Id == EBuiltinId::Abs || Builtin->Id == EBuiltinId::Len || Builtin->Id == EBuiltinId::Substr || Builtin->Id == EBuiltinId::Find || Builtin->Id == EBuiltinId::CharCode || Builtin->Id == EBuiltinId::ListDir || Builtin->Id == EBuiltinId::ToLower || Builtin->Id == EBuiltinId::ToUpper || Builtin->Id == EBuiltinId::Sqrt || Builtin->Id == EBuiltinId::Sin || Builtin->Id == EBuiltinId::Cos || Builtin->Id == EBuiltinId::Tan || Builtin->Id == EBuiltinId::Floor || Builtin->Id == EBuiltinId::Ceil || Builtin->Id == EBuiltinId::Round || Builtin->Id == EBuiltinId::Trunc || Builtin->Id == EBuiltinId::Pow || Builtin->Id == EBuiltinId::Exp || Builtin->Id == EBuiltinId::Log || Builtin->Id == EBuiltinId::Log2 || Builtin->Id == EBuiltinId::Asin || Builtin->Id == EBuiltinId::Acos || Builtin->Id == EBuiltinId::Atan2 || Builtin->Id == EBuiltinId::IsFinite || Builtin->Id == EBuiltinId::IsNan || Builtin->Id == EBuiltinId::ChannelSend || Builtin->Id == EBuiltinId::ChannelRecv || Builtin->Id == EBuiltinId::ChannelClose || Builtin->Id == EBuiltinId::Spawn || Builtin->Id == EBuiltinId::SpawnResult || Builtin->Id == EBuiltinId::JoinTask || Builtin->Id == EBuiltinId::CancelTask || Builtin->Id == EBuiltinId::ReflectionString || Builtin->Id == EBuiltinId::ReflectionInt || Builtin->Id == EBuiltinId::ReflectionBool || Builtin->Id == EBuiltinId::ReflectionInt64)
					{
						std::vector<std::string> ArgTypes(Call.GetArguments().size());
						std::vector<bool> ArgKnown(Call.GetArguments().size(), false);
						bool bAllKnown = true;
						for (std::size_t ArgumentIndex = 0; ArgumentIndex < Call.GetArguments().size(); ++ArgumentIndex)
						{
							if (TryResolveExprTypeName(*Call.GetArguments()[ArgumentIndex], ArgTypes[ArgumentIndex]))
							{
								ArgKnown[ArgumentIndex] = true;
							}
							else
							{
								bAllKnown = false;
							}
						}
						auto RequireKnown = [&](const std::size_t Index) -> bool
						{
							return Index < ArgKnown.size() && ArgKnown[Index];
						};
						if (Builtin->Id == EBuiltinId::Assert)
						{
							if (RequireKnown(0) && ArgTypes[0] != "bool")
							{
								ReportErrorId(
								    "diag.assert_expects_bool_argument",
								    Expression.Location);
								return false;
							}
							if (Call.GetArguments().size() == 2 && RequireKnown(1) && ArgTypes[1] != "string")
							{
								ReportErrorId(
								    "diag.assert_expects_string_message",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::Exit)
						{
							if (RequireKnown(0) && ArgTypes[0] != "int")
							{
								ReportError("exit expects int argument", Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::Sleep)
						{
							if (RequireKnown(0) && ArgTypes[0] != "int")
							{
								ReportError("sleep expects int argument", Expression.Location);
								return false;
							}
							// Reject obvious negative literals early (sleep(-1), sleep(0-1)).
							const FExpr* SleepArg = Call.GetArguments()[0];
							bool bNegativeLiteral = false;
							if (SleepArg->Kind == EExprKind::Unary)
							{
								const auto& Unary = static_cast<const FUnaryExpr&>(*SleepArg);
								if (Unary.Op == EUnaryOp::Negate && Unary.Operand && Unary.Operand->Kind == EExprKind::IntegerLiteral)
								{
									const auto& Literal =
									    static_cast<const FIntegerLiteralExpr&>(*Unary.Operand);
									bNegativeLiteral = !Literal.bUnsigned && Literal.Value > 0;
								}
							}
							else if (SleepArg->Kind == EExprKind::Binary)
							{
								const auto& Binary = static_cast<const FBinaryExpr&>(*SleepArg);
								if (Binary.Op == EBinaryOp::Subtract && Binary.Left && Binary.Right && Binary.Left->Kind == EExprKind::IntegerLiteral && Binary.Right->Kind == EExprKind::IntegerLiteral)
								{
									const auto& Left =
									    static_cast<const FIntegerLiteralExpr&>(*Binary.Left);
									const auto& Right =
									    static_cast<const FIntegerLiteralExpr&>(*Binary.Right);
									if (!Left.bUnsigned && !Right.bUnsigned && Left.Value < Right.Value)
									{
										bNegativeLiteral = true;
									}
								}
							}
							if (bNegativeLiteral)
							{
								ReportError(
								    "sleep expects non-negative milliseconds",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::Format || Builtin->Id == EBuiltinId::FormatPrintf)
						{
							if (bAllKnown && (ArgTypes[0] != "string" || ArgTypes[1] != "array<string>"))
							{
								ReportError(
								    Builtin->Id == EBuiltinId::FormatPrintf
								        ? "__format_printf expects (string, array<string>)"
								        : "__format expects (string, array<string>)",
								    Expression.Location);
								return false;
							}
							if (RequireKnown(0) && ArgTypes[0] != "string")
							{
								ReportError(
								    Builtin->Id == EBuiltinId::FormatPrintf
								        ? "__format_printf expects (string, array<string>)"
								        : "__format expects (string, array<string>)",
								    Expression.Location);
								return false;
							}
							if (RequireKnown(1) && ArgTypes[1] != "array<string>")
							{
								ReportError(
								    Builtin->Id == EBuiltinId::FormatPrintf
								        ? "__format_printf expects (string, array<string>)"
								        : "__format expects (string, array<string>)",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::SetEnv)
						{
							if (RequireKnown(0) && ArgTypes[0] != "string")
							{
								ReportError(
								    "setenv expects (string, string)",
								    Expression.Location);
								return false;
							}
							if (RequireKnown(1) && ArgTypes[1] != "string")
							{
								ReportError(
								    "setenv expects (string, string)",
								    Expression.Location);
								return false;
							}
							if (Call.GetArguments()[0]->Kind == EExprKind::StringLiteral)
							{
								const std::string& Name =
								    static_cast<const FStringLiteralExpr&>(*Call.GetArguments()[0]).Value;
								if (Name.empty() || Name.find('=') != std::string::npos)
								{
									ReportError(
									    "setenv name must be non-empty and must not contain '='",
									    Expression.Location);
									return false;
								}
							}
						}
						else if (Builtin->Id == EBuiltinId::GetEnv)
						{
							if (RequireKnown(0) && ArgTypes[0] != "string")
							{
								ReportError("getenv expects string argument", Expression.Location);
								return false;
							}
							if (Call.GetArguments()[0]->Kind == EExprKind::StringLiteral && static_cast<const FStringLiteralExpr&>(*Call.GetArguments()[0]).Value.empty())
							{
								ReportError("getenv name must be non-empty", Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::MkDir)
						{
							if (RequireKnown(0) && ArgTypes[0] != "string")
							{
								ReportError("__mkdir expects string path", Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::ChDir)
						{
							if (RequireKnown(0) && ArgTypes[0] != "string")
							{
								ReportError("__chdir expects string path", Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::WriteFile || Builtin->Id == EBuiltinId::AppendFile)
						{
							if (RequireKnown(0) && ArgTypes[0] != "string")
							{
								ReportError(
								    std::string(Builtin->Name) + " expects (string, string)",
								    Expression.Location);
								return false;
							}
							if (RequireKnown(1) && ArgTypes[1] != "string")
							{
								ReportError(
								    std::string(Builtin->Name) + " expects (string, string)",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::ReadFile || Builtin->Id == EBuiltinId::Exists || Builtin->Id == EBuiltinId::DeleteFile || Builtin->Id == EBuiltinId::IsFile || Builtin->Id == EBuiltinId::IsDirectory || Builtin->Id == EBuiltinId::RemoveDir || Builtin->Id == EBuiltinId::ListDir)
						{
							if (RequireKnown(0) && ArgTypes[0] != "string")
							{
								ReportError(
								    std::string(Builtin->Name) + " expects string path",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::Range)
						{
							if (RequireKnown(0) && !CanAssignBetweenTypes("int64", ArgTypes[0], false))
							{
								ReportError("range expects int64 argument", Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::ToWString)
						{
							if (RequireKnown(0) && ArgTypes[0] != "string" && ArgTypes[0] != "wstring")
							{
								ReportError(
								    "to_wstring expects string or wstring argument",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::Split)
						{
							if (RequireKnown(0) && ArgTypes[0] != "string")
							{
								ReportError(
								    "__split expects (string, string)",
								    Expression.Location);
								return false;
							}
							if (RequireKnown(1) && ArgTypes[1] != "string")
							{
								ReportError(
								    "__split expects (string, string)",
								    Expression.Location);
								return false;
							}
							if (Call.GetArguments()[1]->Kind == EExprKind::StringLiteral && static_cast<const FStringLiteralExpr&>(*Call.GetArguments()[1]).Value.empty())
							{
								ReportError(
								    "__split separator must not be empty",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::Join)
						{
							if (RequireKnown(0) && ArgTypes[0] != "array<string>")
							{
								ReportError(
								    "__join expects (array<string>, string)",
								    Expression.Location);
								return false;
							}
							if (RequireKnown(1) && ArgTypes[1] != "string")
							{
								ReportError(
								    "__join expects (array<string>, string)",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::Substr)
						{
							if (RequireKnown(0) && ArgTypes[0] != "string")
							{
								ReportError(
								    "__substr expects (string, int64, int64)",
								    Expression.Location);
								return false;
							}
							if (RequireKnown(1) && !CanAssignBetweenTypes("int64", ArgTypes[1], false))
							{
								ReportError(
								    "__substr expects (string, int64, int64)",
								    Expression.Location);
								return false;
							}
							if (RequireKnown(2) && !CanAssignBetweenTypes("int64", ArgTypes[2], false))
							{
								ReportError(
								    "__substr expects (string, int64, int64)",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::Find)
						{
							if (RequireKnown(0) && ArgTypes[0] != "string")
							{
								ReportError(
								    "__find expects (string, string)",
								    Expression.Location);
								return false;
							}
							if (RequireKnown(1) && ArgTypes[1] != "string")
							{
								ReportError(
								    "__find expects (string, string)",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::CharCode)
						{
							if (RequireKnown(0) && ArgTypes[0] != "string")
							{
								ReportError(
								    "__char_code expects (string, int64)",
								    Expression.Location);
								return false;
							}
							if (RequireKnown(1) && !CanAssignBetweenTypes("int64", ArgTypes[1], false))
							{
								ReportError(
								    "__char_code expects (string, int64)",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::ToLower || Builtin->Id == EBuiltinId::ToUpper)
						{
							if (RequireKnown(0) && ArgTypes[0] != "string")
							{
								ReportError(
								    std::string(Builtin->Name) + " expects string argument",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::Abs)
						{
							if (RequireKnown(0) && !IsNumericTypeName(ArgTypes[0]))
							{
								ReportError("abs expects numeric argument", Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::Len)
						{
							if (RequireKnown(0) && !IsLenCompatibleTypeName(ArgTypes[0]))
							{
								ReportError(
								    "len expects string, array, or container argument",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::Sqrt || Builtin->Id == EBuiltinId::Sin || Builtin->Id == EBuiltinId::Cos || Builtin->Id == EBuiltinId::Tan || Builtin->Id == EBuiltinId::Floor || Builtin->Id == EBuiltinId::Ceil || Builtin->Id == EBuiltinId::Round || Builtin->Id == EBuiltinId::Trunc || Builtin->Id == EBuiltinId::Exp || Builtin->Id == EBuiltinId::Log || Builtin->Id == EBuiltinId::Log2 || Builtin->Id == EBuiltinId::Asin || Builtin->Id == EBuiltinId::Acos || Builtin->Id == EBuiltinId::IsFinite || Builtin->Id == EBuiltinId::IsNan)
						{
							if (RequireKnown(0) && !IsNumericTypeName(ArgTypes[0]))
							{
								ReportError(
								    std::string(Builtin->Name) + " expects numeric argument",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::Pow || Builtin->Id == EBuiltinId::Atan2)
						{
							if ((RequireKnown(0) && !IsNumericTypeName(ArgTypes[0])) || (RequireKnown(1) && !IsNumericTypeName(ArgTypes[1])))
							{
								ReportError(
								    std::string(Builtin->Name) + " expects numeric arguments",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::ToUnsigned)
						{
							if (RequireKnown(0) && !IsSignedIntegerTypeName(ArgTypes[0]) && !Runtime::IsUnsignedTypeName(ArgTypes[0]) && ArgTypes[0] != "float" && ArgTypes[0] != "double" && ArgTypes[0] != "bool" && !Numerics::IsTypeName(ArgTypes[0]))
							{
								ReportError(
								    "to_unsigned expects numeric or bool argument",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::ToInt || Builtin->Id == EBuiltinId::ToInt64 || Builtin->Id == EBuiltinId::ToUInt64 || Builtin->Id == EBuiltinId::ToDouble || Builtin->Id == EBuiltinId::ToFloat)
						{
							if (RequireKnown(0) && !IsSignedIntegerTypeName(ArgTypes[0]) && !Runtime::IsUnsignedTypeName(ArgTypes[0]) && ArgTypes[0] != "float" && ArgTypes[0] != "double" && ArgTypes[0] != "bool" && !Numerics::IsTypeName(ArgTypes[0]))
							{
								ReportError(
								    std::string(Builtin->Name) + " expects numeric or bool argument",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::Min || Builtin->Id == EBuiltinId::Max)
						{
							if (RequireKnown(0) && !IsNumericTypeName(ArgTypes[0]))
							{
								ReportError(
								    std::string(Builtin->Name) + " expects numeric arguments",
								    Expression.Location);
								return false;
							}
							if (RequireKnown(1) && !IsNumericTypeName(ArgTypes[1]))
							{
								ReportError(
								    std::string(Builtin->Name) + " expects numeric arguments",
								    Expression.Location);
								return false;
							}
							if (bAllKnown)
							{
								const bool bLeftUnsigned = Runtime::IsUnsignedTypeName(ArgTypes[0]);
								const bool bRightUnsigned = Runtime::IsUnsignedTypeName(ArgTypes[1]);
								const bool bLeftSigned = IsSignedIntegerTypeName(ArgTypes[0]);
								const bool bRightSigned = IsSignedIntegerTypeName(ArgTypes[1]);
								if ((bLeftUnsigned && bRightSigned) || (bRightUnsigned && bLeftSigned))
								{
									ReportError("cannot mix signed and unsigned", Expression.Location);
									return false;
								}
							}
						}
						else if (Builtin->Id == EBuiltinId::ChannelSend)
						{
							if (RequireKnown(0) && !Runtime::IsChannelTypeName(ArgTypes[0]))
							{
								ReportError(
								    "__channel_send expects (channel<T>, T)",
								    Expression.Location);
								return false;
							}
							if (RequireKnown(0) && RequireKnown(1))
							{
								std::string ElementType;
								if (!Runtime::ParseChannelElementType(ArgTypes[0], ElementType) || ArgTypes[1] != ElementType)
								{
									ReportError(
									    "__channel_send value type must match channel element type",
									    Expression.Location);
									return false;
								}
							}
						}
						else if (Builtin->Id == EBuiltinId::ChannelRecv || Builtin->Id == EBuiltinId::ChannelClose)
						{
							if (RequireKnown(0) && !Runtime::IsChannelTypeName(ArgTypes[0]))
							{
								ReportError(
								    std::string(Builtin->Name) + " expects channel<T>",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::Spawn || Builtin->Id == EBuiltinId::SpawnResult)
						{
							if (RequireKnown(0) && ArgTypes[0] != "string")
							{
								ReportError(
								    "__spawn expects (string, channel<T>)",
								    Expression.Location);
								return false;
							}
							if (RequireKnown(1) && !Runtime::IsChannelTypeName(ArgTypes[1]))
							{
								ReportError(
								    "__spawn expects (string, channel<T>)",
								    Expression.Location);
								return false;
							}
							if (Call.GetArguments().size() >= 1 && Call.GetArguments()[0] && Call.GetArguments()[0]->Kind == EExprKind::StringLiteral)
							{
								const std::string& TargetName =
								    static_cast<const FStringLiteralExpr&>(*Call.GetArguments()[0]).Value;
								std::vector<const FFunctionDecl*> Matches;
								for (const FFunctionDecl& Function : ActiveProgram->Functions)
								{
									if (Function.Name == TargetName && CanAccessSymbol(Function.ModulePath, Function.bExported, CurrentModulePath))
									{
										Matches.push_back(&Function);
									}
								}
								if (Matches.empty())
								{
									ReportErrorId(
									    "diag.spawn_target_not_found",
									    Expression.Location,
									    {TargetName});
									return false;
								}
								if (Matches.size() > 1)
								{
									ReportError(
									    "spawn target is ambiguous (multiple overloads): " + TargetName,
									    Expression.Location);
									return false;
								}
								const FFunctionDecl& Target = *Matches.front();
								if (Target.Parameters.size() != 1 || !Runtime::IsChannelTypeName(Target.Parameters[0].TypeName))
								{
									ReportErrorId(
									    "diag.spawn_target_must_take_exactly_one_channel_parameter",
									    Expression.Location,
									    {TargetName});
									return false;
								}
								if (NormalizeTypeAlias(Target.ReturnType) != "void")
								{
									ReportErrorId(
									    "diag.spawn_target_must_return_void",
									    Expression.Location,
									    {TargetName});
									return false;
								}
							}
						}
						else if (Builtin->Id == EBuiltinId::JoinTask)
						{
							if (RequireKnown(0) && ArgTypes[0] != "int")
							{
								ReportError("__join_task expects int task id", Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::CancelTask)
						{
							if (RequireKnown(0) && ArgTypes[0] != "int")
							{
								ReportError(
								    "__cancel_task expects int task id",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::WeakCreate)
						{
							if (RequireKnown(0) && ArgTypes[0] != "nullptr" && !((IsClassOrStructType(ArgTypes[0]) && !IsStructType(ArgTypes[0])) || IsInterfaceTypeName(ArgTypes[0])) && (!ActiveBindings || !ActiveBindings->FindType(ArgTypes[0]) || ActiveBindings->FindType(ArgTypes[0])->Kind != EHostTypeKind::Class))
							{
								ReportError(
								    "__weak_create requires a class reference",
								    Expression.Location);
								return false;
							}
						}
						else if (Builtin->Id == EBuiltinId::WeakLock || Builtin->Id == EBuiltinId::WeakExpired || Builtin->Id == EBuiltinId::WeakReset)
						{
							if (RequireKnown(0) && ArgTypes[0] != "int64")
							{
								ReportError(
								    std::string(Builtin->Name) + " expects int64 weak slot",
								    Expression.Location);
								return false;
							}
						}
					}
					for (const FExpr* Argument : Call.GetArguments())
					{
						if (!CompileExpression(*Argument))
						{
							return false;
						}
						if ((Builtin->Id == EBuiltinId::Range || ((Builtin->Id == EBuiltinId::Substr || Builtin->Id == EBuiltinId::CharCode) && Argument != Call.GetArguments().front())) && !EmitValueCoercionForTarget(Emitter, "int64", *Argument, Expression.Location))
						{
							return false;
						}
					}
					Emitter.EmitCallBuiltin(Builtin->Id, BuiltinArgumentCount);
					if (Builtin->bPushVoidResult)
					{
						FBytecodeConstant Zero;
						Zero.Kind = EConstantKind::I32;
						Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Zero));
					}
					return true;
				}

				if (Runtime::IsDynamicArrayTypeName(EffectiveCallee))
				{
					if (!Call.GetArguments().empty())
					{
						ReportError("dynamic array constructor expects no arguments", Expression.Location);
						return false;
					}
					Emitter.EmitU16Operand(
					    EOpcode::NewDynamicArray,
					    AddString(QualifyTypeName(NormalizeTypeAlias(EffectiveCallee), CurrentModulePath)));
					return true;
				}

				if (Runtime::IsMapTypeName(EffectiveCallee))
				{
					if (!Call.GetArguments().empty())
					{
						ReportError("map constructor expects no arguments", Expression.Location);
						return false;
					}
					Emitter.EmitU16Operand(
					    EOpcode::NewMap,
					    AddString(QualifyTypeName(NormalizeTypeAlias(EffectiveCallee), CurrentModulePath)));
					return true;
				}

				if (Runtime::IsSetTypeName(EffectiveCallee))
				{
					if (!Call.GetArguments().empty())
					{
						ReportError("set constructor expects no arguments", Expression.Location);
						return false;
					}
					Emitter.EmitU16Operand(
					    EOpcode::NewSet,
					    AddString(QualifyTypeName(NormalizeTypeAlias(EffectiveCallee), CurrentModulePath)));
					return true;
				}
				if (Runtime::IsMultiSetTypeName(EffectiveCallee) || Runtime::IsOrderedMultiSetTypeName(EffectiveCallee))
				{
					if (!Call.GetArguments().empty())
					{
						ReportError(
						    "multiset constructor expects no arguments",
						    Expression.Location);
						return false;
					}
					Emitter.EmitU16Operand(
					    Runtime::IsOrderedMultiSetTypeName(EffectiveCallee)
					        ? EOpcode::NewOrderedMap
					        : EOpcode::NewMap,
					    AddString(QualifyTypeName(NormalizeTypeAlias(EffectiveCallee), CurrentModulePath)));
					return true;
				}

				if (Runtime::IsQueueTypeName(EffectiveCallee))
				{
					if (!Call.GetArguments().empty())
					{
						ReportError("queue constructor expects no arguments", Expression.Location);
						return false;
					}
					Emitter.EmitU16Operand(
					    EOpcode::NewQueue,
					    AddString(QualifyTypeName(NormalizeTypeAlias(EffectiveCallee), CurrentModulePath)));
					return true;
				}

				if (Runtime::IsChannelTypeName(EffectiveCallee))
				{
					if (!Call.GetArguments().empty())
					{
						ReportError("channel constructor expects no arguments", Expression.Location);
						return false;
					}
					std::string ElementType;
					if (!Runtime::ParseChannelElementType(EffectiveCallee, ElementType))
					{
						ReportError(
						    "invalid channel type (scalars only): " + EffectiveCallee,
						    Expression.Location);
						return false;
					}
					Emitter.EmitU16Operand(
					    EOpcode::NewChannel,
					    AddString(QualifyTypeName(NormalizeTypeAlias(EffectiveCallee), CurrentModulePath)));
					return true;
				}

				if (Runtime::IsStackTypeName(EffectiveCallee))
				{
					if (!Call.GetArguments().empty())
					{
						ReportError("stack constructor expects no arguments", Expression.Location);
						return false;
					}
					Emitter.EmitU16Operand(
					    EOpcode::NewStack,
					    AddString(QualifyTypeName(NormalizeTypeAlias(EffectiveCallee), CurrentModulePath)));
					return true;
				}

				if (Runtime::IsOrderedMapTypeName(EffectiveCallee))
				{
					if (!Call.GetArguments().empty())
					{
						ReportError("ordered_map constructor expects no arguments", Expression.Location);
						return false;
					}
					Emitter.EmitU16Operand(
					    EOpcode::NewOrderedMap,
					    AddString(QualifyTypeName(NormalizeTypeAlias(EffectiveCallee), CurrentModulePath)));
					return true;
				}

				if (Runtime::IsDequeTypeName(EffectiveCallee))
				{
					if (!Call.GetArguments().empty())
					{
						ReportError("deque constructor expects no arguments", Expression.Location);
						return false;
					}
					Emitter.EmitU16Operand(
					    EOpcode::NewDeque,
					    AddString(QualifyTypeName(NormalizeTypeAlias(EffectiveCallee), CurrentModulePath)));
					return true;
				}

				// Host constructors are resolved from structured member metadata so
				// multiple signatures can coexist without overloading a native-table
				// name. Manually supplied legacy natives still fall through to the
				// ordinary name lookup below.
				std::string HostConstructorType =
				    CanonicalTypeNameForCoercion(EffectiveCallee);
				if (ActiveBindings && ActiveBindings->FindType(HostConstructorType))
				{
					std::vector<std::string> ArgTypeNames;
					ArgTypeNames.reserve(Call.GetArguments().size());
					for (const FExpr* Argument : Call.GetArguments())
					{
						std::string ArgType;
						if (!Argument || !TryResolveExprTypeName(UnwrapRefOperand(*Argument), ArgType))
						{
							ArgType.clear();
						}
						else if (Argument->Kind == EExprKind::Ref && !ArgType.empty())
						{
							ArgType = EncodeRefCallArgType(
							    UnwrapRefOperand(*Argument), std::move(ArgType));
						}
						ArgTypeNames.push_back(std::move(ArgType));
					}
					bool bAmbiguousConstructor = false;
					std::uint16_t ConstructorNativeIndex =
					    static_cast<std::uint16_t>(-1);
					if (const FHostMemberDesc* const Constructor =
					        ResolveHostConstructor(
					            HostConstructorType,
					            ArgTypeNames,
					            bAmbiguousConstructor,
					            &ConstructorNativeIndex))
					{
						FNativeFunction Signature;
						Signature.Callback = Constructor->Callback;
						Signature.HostTypeName = HostConstructorType;
						Signature.ParamTypes = Constructor->ParamTypes;
						Signature.ParamHostTypeNames = Constructor->ParamHostTypeNames;
						Signature.ParamElementTypeNames = Constructor->ParamElementTypeNames;
						Signature.ParamKeyTypeNames = Constructor->ParamKeyTypeNames;
						Signature.ParamValueTypeNames = Constructor->ParamValueTypeNames;
						Signature.ParamFlags = Constructor->ParamFlags;
						for (std::size_t ArgumentIndex = 0;
						    ArgumentIndex < Call.GetArguments().size();
						    ++ArgumentIndex)
						{
							if (Call.GetArguments()[ArgumentIndex]->Kind == EExprKind::Ref && !NativeParamFlagIsRef(NativeParamFlagAt(Signature, ArgumentIndex)))
							{
								ReportErrorId("diag.native_does_not_accept_ref_arguments",
								    Expression.Location);
								return false;
							}
							const std::string ParamTypeName =
							    ResolveNativeParamTypeName(Signature, ArgumentIndex);
							if (Call.GetArguments()[ArgumentIndex]->Kind == EExprKind::Ref)
							{
								const auto Flag = NativeParamFlagAt(Signature, ArgumentIndex);
								if (!CompileOperatorArgument(Emitter, *Call.GetArguments()[ArgumentIndex],
								        ((Flag & NativeParamFlagConstRef) ? "const ref " : "ref ") + ParamTypeName, Expression.Location))
								{
									return false;
								}
								continue;
							}
							std::string NumericArgumentType;
							if (Numerics::ArgumentRole(Signature, ArgumentIndex) !=
							        Numerics::EArgumentRole::Ordinary &&
							    (!TryResolveExprTypeName(*Call.GetArguments()[ArgumentIndex],
							         NumericArgumentType) ||
							        !Numerics::ValidateArgumentType(
							            Signature, ArgumentIndex,
							            CanonicalTypeNameForCoercion(NumericArgumentType))))
							{
								ReportErrorId("diag.numeric_type_mismatch", Expression.Location);
								return false;
							}
							const bool bNumericInteger = Numerics::ArgumentRole(Signature, ArgumentIndex) ==
							    Numerics::EArgumentRole::Integer;
							const std::string PreviousExpectedType = ExpressionExpectedType;
							ExpressionExpectedType = bNumericInteger ? std::string{} : ParamTypeName;
							const bool bCompiled = CompileExpression(*Call.GetArguments()[ArgumentIndex]);
							const bool bCoerced =
							    bCompiled && (bNumericInteger || EmitValueCoercionForTarget(Emitter, ParamTypeName, *Call.GetArguments()[ArgumentIndex], Expression.Location));
							ExpressionExpectedType = PreviousExpectedType;
							if (!bCompiled || !bCoerced)
							{
								return false;
							}
						}
						Emitter.EmitCallNative(ConstructorNativeIndex,
						    static_cast<std::uint8_t>(Call.GetArguments().size()));
						return true;
					}
					if (bAmbiguousConstructor)
					{
						ReportErrorId(
						    "diag.ambiguous_call_to",
						    Expression.Location,
						    {DisplayTypeName(HostConstructorType)});
						return false;
					}
				}

				std::uint16_t NativeIndex = static_cast<std::uint16_t>(-1);
				if (const FNativeFunction* const Native = FindNativeForCall(Call.Callee, Call.GetArguments(), NativeIndex, Expression.Location))
				{
					if (!Call.bImplementationBuiltinAccess && (Native->Name == "__std_numeric_bigint_pow" || Native->Name == "__std_numeric_rational_pow" || Native->Name == "__std_numeric_bigint_divmod"))
					{
						ReportError("numeric runtime helpers are internal", Expression.Location);
						return false;
					}
					if (Native->bGenericHostObjectCast)
					{
						if (Native->ReturnType != ENativeType::HostObject || Native->ParamTypes.size() != 1 || Native->ParamTypes.front() != ENativeType::HostObject)
						{
							ReportErrorId("diag.hostbinding_generic_cast_has_invalid_signature", Expression.Location);
							return false;
						}
						if (Call.TypeArgs.size() != Native->TypeParams.size() || Call.TypeArgs.empty())
						{
							ReportErrorId("diag.wrong_number_of_type_arguments", Expression.Location,
							    {
							        Call.Callee,
							        std::to_string(Native->TypeParams.size()),
							        std::to_string(Call.TypeArgs.size()),
							    });
							return false;
						}
						if (Call.GetArguments().size() != Native->ParamTypes.size())
						{
							ReportErrorId("diag.name_expects_exactly_n_arguments", Expression.Location,
							    {
							        std::string(Native->Name),
							        std::to_string(Native->ParamTypes.size()),
							    });
							return false;
						}
						if (Call.TypeArgs.size() >= std::numeric_limits<std::uint8_t>::max())
						{
							ReportError("function call supports at most 255 arguments", Expression.Location);
							return false;
						}
						const auto ReturnParameter = std::find(Native->TypeParams.begin(),
						    Native->TypeParams.end(), Native->ScriptReturnTypeName);
						if (ReturnParameter == Native->TypeParams.end())
						{
							ReportErrorId("diag.hostbinding_generic_cast_return_must_be_a_type_parameter", Expression.Location);
							return false;
						}
						const auto ReturnIndex = static_cast<std::size_t>(ReturnParameter - Native->TypeParams.begin());
						const auto RuntimeArgumentCount = NativeRuntimeArgumentCount(*Native);
						std::vector<std::string> ConcreteTypeArguments;
						ConcreteTypeArguments.reserve(Call.TypeArgs.size());
						for (const auto& TypeArgument : Call.TypeArgs)
						{
							const auto Concrete = CanonicalTypeNameForCoercion(
							    QualifyTypeName(TypeArgument, CurrentModulePath));
							if (Concrete != "void" && !ValidateFieldTypeName(Concrete, Expression.Location))
							{
								return false;
							}
							ConcreteTypeArguments.push_back(Concrete);
						}
						const auto& TargetType = ConcreteTypeArguments[ReturnIndex];
						const FHostTypeDesc* const TargetHostType =
						    ActiveBindings ? ActiveBindings->FindType(TargetType) : nullptr;
						const bool bTargetScriptClass =
						    IsClassOrStructType(TargetType) && !IsStructType(TargetType);
						const bool bTargetHostClass =
						    TargetHostType && TargetHostType->Kind == EHostTypeKind::Class;
						if (!bTargetScriptClass && !bTargetHostClass)
						{
							ReportError(
							    "generic host cast target must be a class type: " + TargetType,
							    Expression.Location);
							return false;
						}

						std::string SourceType;
						if (!TryResolveExprTypeName(*Call.GetArguments()[0], SourceType))
						{
							ReportError(
							    "could not resolve generic host cast source type",
							    Expression.Location);
							return false;
						}
						const std::string CanonicalSource =
						    CanonicalTypeNameForCoercion(SourceType);
						const FHostTypeDesc* const SourceHostType =
						    ActiveBindings ? ActiveBindings->FindType(CanonicalSource) : nullptr;
						const bool bSourceScriptReference =
						    (IsClassOrStructType(CanonicalSource) && !IsStructType(CanonicalSource)) || IsInterfaceTypeName(CanonicalSource);
						const bool bSourceHostReference =
						    SourceHostType && SourceHostType->Kind == EHostTypeKind::Class;
						if (CanonicalSource != "nullptr" && !bSourceScriptReference && !bSourceHostReference)
						{
							ReportError(
							    "generic host cast source must be a class reference",
							    Expression.Location);
							return false;
						}
						if (!CompileExpression(*Call.GetArguments()[0]))
						{
							return false;
						}
						for (const auto& Concrete : ConcreteTypeArguments)
						{
							FBytecodeConstant TypeArgument;
							TypeArgument.Kind = EConstantKind::Str;
							TypeArgument.bIsLinkageString = true;
							TypeArgument.StringIndex = AddString(Concrete);
							if (bHasError)
							{
								return false;
							}
							Emitter.EmitU16Operand(EOpcode::ConstStr, AddConstant(TypeArgument));
						}
						Emitter.EmitCallNative(
						    NativeIndex,
						    static_cast<std::uint8_t>(RuntimeArgumentCount));
						return true;
					}
					if (!Call.TypeArgs.empty())
					{
						ReportError(
						    "explicit type arguments require a generic function",
						    Expression.Location);
						return false;
					}
					if (Call.GetArguments().size() != Native->ParamTypes.size())
					{
						ReportErrorId("diag.name_expects_exactly_n_arguments", Expression.Location, {std::string(Native->Name), std::to_string(Native->ParamTypes.size())});
						return false;
					}
					for (std::size_t ArgumentIndex = 0; ArgumentIndex < Call.GetArguments().size(); ++ArgumentIndex)
					{
						if (Call.GetArguments()[ArgumentIndex]->Kind == EExprKind::Ref && !NativeParamFlagIsRef(NativeParamFlagAt(*Native, ArgumentIndex)))
						{
							ReportErrorId("diag.native_does_not_accept_ref_arguments", Expression.Location);
							return false;
						}
						const std::string ParamTypeName = ResolveNativeParamTypeName(*Native, ArgumentIndex);
						if (Call.GetArguments()[ArgumentIndex]->Kind == EExprKind::Ref)
						{
							const auto Flag = NativeParamFlagAt(*Native, ArgumentIndex);
							if (!CompileOperatorArgument(Emitter, *Call.GetArguments()[ArgumentIndex],
							        ((Flag & NativeParamFlagConstRef) ? "const ref " : "ref ") + ParamTypeName, Expression.Location))
							{
								return false;
							}
							continue;
						}
						std::string NumericArgumentType;
						if (Numerics::ArgumentRole(*Native, ArgumentIndex) !=
						        Numerics::EArgumentRole::Ordinary &&
						    (!TryResolveExprTypeName(*Call.GetArguments()[ArgumentIndex],
						         NumericArgumentType) ||
						        !Numerics::ValidateArgumentType(
						            *Native, ArgumentIndex, CanonicalTypeNameForCoercion(NumericArgumentType))))
						{
							ReportErrorId("diag.numeric_type_mismatch", Expression.Location);
							return false;
						}
						const bool bNumericInteger = Numerics::ArgumentRole(*Native, ArgumentIndex) ==
						    Numerics::EArgumentRole::Integer;
						const std::string PreviousExpectedType = ExpressionExpectedType;
						ExpressionExpectedType = bNumericInteger ? std::string{} : ParamTypeName;
						const bool bCompiled = CompileExpression(*Call.GetArguments()[ArgumentIndex]);
						const bool bCoerced =
						    bCompiled && (bNumericInteger || EmitValueCoercionForTarget(Emitter, ParamTypeName, *Call.GetArguments()[ArgumentIndex], Expression.Location));
						ExpressionExpectedType = PreviousExpectedType;
						if (!bCompiled || !bCoerced)
						{
							return false;
						}
					}
					Emitter.EmitCallNative(NativeIndex,
					    static_cast<std::uint8_t>(Call.GetArguments().size()));
					if (Native->ReturnType == ENativeType::Void)
					{
						FBytecodeConstant Zero;
						Zero.Kind = EConstantKind::I32;
						Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Zero));
					}
					return true;
				}

				if (HasError()) return false;
				const FFunctionDecl* FuncDecl = nullptr;
				std::string FunctionLinkageName;
				std::vector<std::string> CallArgTypeNames;
				CallArgTypeNames.reserve(Call.GetArguments().size());
				for (std::size_t ArgIndex = 0;
				    ArgIndex < Call.GetArguments().size();
				    ++ArgIndex)
				{
					const FExpr* ArgExpr = Call.GetArguments()[ArgIndex];
					if (!ArgExpr)
					{
						CallArgTypeNames.emplace_back();
						continue;
					}
					const bool bArgIsRef = ArgExpr->Kind == EExprKind::Ref;
					const FExpr& Arg = UnwrapRefOperand(*ArgExpr);
					std::string ArgType;
					if (!TryResolveExprTypeName(Arg, ArgType))
					{
						ArgType.clear();
					}
					else if ((bArgIsRef || (Call.bImplicitReceiverRefBinding && ArgIndex == 0)) && !ArgType.empty())
					{
						ArgType = EncodeRefCallArgType(Arg, std::move(ArgType));
					}
					CallArgTypeNames.push_back(std::move(ArgType));
				}
				{
					const FFunctionDecl* GenericCandidate = nullptr;
					if ((Call.bExplicitTemplateArguments || !Call.TypeArgs.empty()) && !FindFunctionDeclForReference(Call.Callee, CurrentModulePath))
					{
						std::string ImplicitOwner;
						const FMethodDecl* ImplicitMethod = nullptr;
						if (ResolveImplicitThisMethodForCall(Call, ImplicitOwner, ImplicitMethod) && ImplicitMethod)
						{
							auto Receiver = std::make_unique<FThisExpr>();
							Receiver->Location = Call.Location;
							FMethodCallExpr ImplicitCall(std::move(Receiver), Call.Callee, {});
							ImplicitCall.Location = Call.Location;
							ImplicitCall.TypeArgs = Call.TypeArgs;
							ImplicitCall.bExplicitTemplateArguments = Call.bExplicitTemplateArguments;
							ImplicitCall.BorrowArgumentsFrom(Call.GetArguments());
							bPreserveNextReferenceResult = bCurrentExpressionPreservesReference;
							return CompileExpression(ImplicitCall);
						}
					}
					if ((Call.bExplicitTemplateArguments || !Call.TypeArgs.empty()) && !Call.bStandardLibraryQualified && !Call.bImplementationBuiltinAccess && !FindFunctionDeclForReference(Call.Callee, CurrentModulePath))
					{
						if (const std::string_view Required =
						        StandardLibrary::RequiredNamespaceForFunction(Call.Callee);
						    !Required.empty())
						{
							ReportErrorId(
							    "diag.function_requires_its_standard_namespace",
							    Expression.Location,
							    {Call.Callee, std::string(Required)});
							return false;
						}
					}
					if (ResolveGenericFreeFunctionForCall(
					        Call.Callee,
					        Call.TypeArgs,
					        CallArgTypeNames,
					        GenericCandidate,
					        Expression.Location, Call.bExplicitTemplateArguments))
					{
						// Explicit type args always take the generic. Without them, prefer
						// a matching non-generic overload when value-arg arity overlaps.
						if (Call.TypeArgs.empty() && !Call.bExplicitTemplateArguments)
						{
							const FFunctionDecl* Concrete = nullptr;
							if (!ResolveFunctionForCall(
							        Call.Callee,
							        CallArgTypeNames,
							        Concrete,
							        Expression.Location) &&
							    HasError())
							{
								return false;
							}
							if (Concrete)
							{
								FuncDecl = Concrete;
								FunctionLinkageName = MakeFreeFunctionLinkageName(*Concrete);
							}
							else
							{
								FuncDecl = GenericCandidate;
								FunctionLinkageName = GetSymbolLinkageName(
								    GenericCandidate->ModulePath,
								    GenericCandidate->bExported,
								    GenericCandidate->Name);
							}
						}
						else
						{
							FuncDecl = GenericCandidate;
							FunctionLinkageName = GetSymbolLinkageName(
							    GenericCandidate->ModulePath,
							    GenericCandidate->bExported,
							    GenericCandidate->Name);
						}
					}
					else if (HasError())
					{
						return false;
					}
					else if ((Call.bExplicitTemplateArguments || !Call.TypeArgs.empty()))
					{
						// Explicit type args with no matching generic already reported.
						return false;
					}
					else if (FindFunctionDeclForReference(Call.Callee, CurrentModulePath))
					{
						// Generic miss (no TypeArgs): resolve among non-generic overloads
						// only. Do not bind the first same-named generic by declaration
						// order — that would shadow a later concrete overload.
						if (!ResolveFunctionForCall(
						        Call.Callee,
						        CallArgTypeNames,
						        FuncDecl,
						        Expression.Location) &&
						    HasError())
						{
							return false;
						}
						else if (FuncDecl)
						{
							FunctionLinkageName = MakeFreeFunctionLinkageName(*FuncDecl);
						}
					}
				}

				std::vector<std::string> ConcreteTypeParams;
				std::vector<std::string> SpecializedParamTypes;
				if (FuncDecl)
				{
					for (std::size_t I = 0; I < CallArgTypeNames.size(); ++I)
					{
						if (StandardLibrary::IsDiscreteArgument(FuncDecl->Name, I) && !StandardLibrary::IsScalarParseType(CanonicalTypeNameForCoercion(CallArgTypeNames[I]), true))
						{
							ReportErrorId("diag.stdlib_arguments", Expression.Location);
							return false;
						}
					}
					if (FuncDecl->Name == "__std_clamp" && Call.TypeArgs.empty() && CallArgTypeNames.size() == 3 && (CallArgTypeNames[0] != CallArgTypeNames[1] || CallArgTypeNames[0] != CallArgTypeNames[2]))
					{
						ReportErrorId("diag.stdlib_arguments", Expression.Location);
						return false;
					}
				}
				if (FuncDecl && FuncDecl->bDeleted)
				{
					ReportErrorId(
					    "diag.use_of_deleted_declaration",
					    Expression.Location,
					    {Call.Callee});
					return false;
				}
				if (FuncDecl && !TemplateParameterNames(*FuncDecl).empty())
				{
					if (!ParamsAcceptArgCount(FuncDecl->Parameters, Call.GetArguments().size()))
					{
						ReportErrorId(
						    "diag.wrong_argument_count_for_function",
						    Expression.Location,
						    {Call.Callee});
						return false;
					}
					if (!TryInferGenericFunctionTypeParams(
					        *FuncDecl,
					        CallArgTypeNames,
					        Call.TypeArgs,
					        ConcreteTypeParams))
					{
						ReportError("could not infer generic argument types", Expression.Location);
						return false;
					}
					// Structured inference has already completed and qualified these arguments.
					if (FuncDecl->TemplateParameters.IsEmpty() && !CompleteGenericFunctionTypeArguments(*FuncDecl, ConcreteTypeParams, Expression.Location))
					{
						return false;
					}
					std::unordered_map<std::string, std::string> FunctionSubst;
					for (std::size_t TypeIndex = 0;
					    TypeIndex < TemplateParameterNames(*FuncDecl).size();
					    ++TypeIndex)
					{
						FunctionSubst.emplace(
						    TemplateParameterNames(*FuncDecl)[TypeIndex],
						    ConcreteTypeParams[TypeIndex]);
					}
					SpecializedParamTypes.reserve(FuncDecl->Parameters.size());
					for (const FFunctionParam& Param : FuncDecl->Parameters)
					{
						SpecializedParamTypes.push_back(
						    QualifyTypeName(
						        SubstituteTypeNameWith(Param.TypeName, FunctionSubst),
						        FuncDecl->ModulePath));
					}
				}
				else if (FuncDecl && !Call.TypeArgs.empty())
				{
					ReportError(
					    "explicit type arguments require a generic function",
					    Expression.Location);
					return false;
				}

				// Field initializers are bound while type layouts are being built,
				// before the ordinary function reservation pass. Reserve a resolved
				// declaration here; its body is still compiled in the normal pass.
				if (FuncDecl && TemplateParameterNames(*FuncDecl).empty() && !FunctionLinkageName.empty() && FindFunctionIndex(FunctionLinkageName) == 0xFFFF)
				{
					if (!EnsureFunctionStub(FunctionLinkageName, FuncDecl->ReturnType,
					        static_cast<std::uint8_t>(FuncDecl->Parameters.size()), false, 0, false, FuncDecl->Location))
					{
						return false;
					}
					SetFunctionDeclarationSignatureMetadata(Module->Functions[FindFunctionIndex(FunctionLinkageName)],
					    FuncDecl->ReturnType, FuncDecl->Parameters, FuncDecl->ModulePath);
				}
				if (FuncDecl && TemplateParameterNames(*FuncDecl).empty() && IsVariadicParamList(FuncDecl->Parameters))
				{
					if (!ParamsAcceptArgCount(FuncDecl->Parameters, Call.GetArguments().size()))
					{
						ReportErrorId(
						    "diag.wrong_argument_count_for_function",
						    Expression.Location,
						    {Call.Callee});
						return false;
					}
					const std::size_t FixedCount = FixedParamCount(FuncDecl->Parameters);
					const std::size_t ArgCount = Call.GetArguments().size();
					const std::size_t FixedCompile = (std::min)(ArgCount, FixedCount);
					for (std::size_t ArgumentIndex = 0; ArgumentIndex < FixedCompile; ++ArgumentIndex)
					{
						const FExpr& RawArgument = *Call.GetArguments()[ArgumentIndex];
						const bool bParamIsRef = FuncDecl->Parameters[ArgumentIndex].bIsRef;
						const bool bArgIsRef = RawArgument.Kind == EExprKind::Ref || (Call.bImplicitReceiverRefBinding && ArgumentIndex == 0 && bParamIsRef);
						if (bArgIsRef != bParamIsRef)
						{
							ReportErrorId(
							    bParamIsRef
							        ? "diag.ref_parameter_requires_ref_argument"
							        : "diag.ref_argument_requires_ref_parameter",
							    Expression.Location);
							return false;
						}
						const FExpr& Argument = UnwrapRefOperand(RawArgument);
						const std::string ParamType = ParameterType(*FuncDecl, ArgumentIndex);
						if (!ValidateDynamicArrayAssignment(ParamType, Argument, Expression.Location))
						{
							return false;
						}
						if (bArgIsRef)
						{
							const bool bMutableRef = !FuncDecl->Parameters[ArgumentIndex].bIsConst;
							if (!ValidateRefArgumentType(ParamType, Argument, Expression.Location))
							{
								return false;
							}
							if (!CompileRefArgument(
							        Emitter,
							        Argument,
							        Expression.Location,
							        bMutableRef))
							{
								return false;
							}
							continue;
						}
						{
							const std::string PreviousExpectedType = ExpressionExpectedType;
							ExpressionExpectedType = ParamType;
							const bool bCompiledArg = CompileExpression(Argument);
							const bool bCoerced = bCompiledArg && EmitValueCoercionForTarget(Emitter, ParamType, Argument, Expression.Location);
							if (bCoerced && NeedsPassByValueCopy(ParamType) && !CanTransferCoercedValue(Argument, ParamType))
							{
								EmitValueCopy(Emitter, ParamType);
							}
							ExpressionExpectedType = PreviousExpectedType;
							if (!bCompiledArg || !bCoerced)
							{
								return false;
							}
						}
					}
					if (ArgCount > FixedCount)
					{
						if (!EmitVariadicRestArgument(
						        Emitter,
						        FuncDecl->Parameters,
						        Call.GetArguments(),
						        FixedCount,
						        FuncDecl->ModulePath,
						        {},
						        Expression.Location))
						{
							return false;
						}
					}
					else if (!EmitTrailingDefaultArguments(
					             Emitter,
					             FuncDecl->Parameters,
					             ArgCount,
					             FuncDecl->ModulePath,
					             {},
					             Expression.Location))
					{
						return false;
					}
					const std::uint16_t VariadicFnIndex = FunctionLinkageName.empty()
					    ? static_cast<std::uint16_t>(-1)
					    : FindFunctionIndex(FunctionLinkageName);
					if (VariadicFnIndex == static_cast<std::uint16_t>(-1))
					{
						ReportErrorId("diag.unknown_function", Expression.Location, {Call.Callee});
						return false;
					}
					Emitter.EmitCall(
					    VariadicFnIndex,
					    static_cast<std::uint8_t>(FuncDecl->Parameters.size()));
					return true;
				}

				// Emit free-function arguments only when this call resolves to a free
				// function. Type constructors fall through below and emit their own args;
				// compiling here first would leave stray values under NewObject/Dup.
				std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
				bool bEmitFreeFunctionCall = false;
				if (FuncDecl && !TemplateParameterNames(*FuncDecl).empty())
				{
					if (!EnsureGenericInstantiation(
					        *FuncDecl,
					        ConcreteTypeParams,
					        FunctionIndex,
					        Expression.Location))
					{
						return false;
					}
					bEmitFreeFunctionCall = true;
				}
				else if (FuncDecl)
				{
					if (TemplateParameterNames(*FuncDecl).empty() && !ParamsAcceptArgCount(FuncDecl->Parameters, Call.GetArguments().size()))
					{
						ReportErrorId(
						    "diag.wrong_argument_count_for_function",
						    Expression.Location,
						    {Call.Callee});
						return false;
					}
					FunctionIndex = FunctionLinkageName.empty()
					    ? static_cast<std::uint16_t>(-1)
					    : FindFunctionIndex(FunctionLinkageName);
					bEmitFreeFunctionCall = FunctionIndex != static_cast<std::uint16_t>(-1) && ParamsAcceptArgCount(FuncDecl->Parameters, Call.GetArguments().size());
				}

				if (bEmitFreeFunctionCall && FuncDecl)
				{
					const bool bGenericVariadic = !TemplateParameterNames(*FuncDecl).empty() && IsVariadicParamList(FuncDecl->Parameters);
					const std::size_t GenericFixedCount = bGenericVariadic
					    ? FixedParamCount(FuncDecl->Parameters)
					    : Call.GetArguments().size();
					const std::size_t ArgumentCompileCount = bGenericVariadic
					    ? (std::min)(Call.GetArguments().size(), GenericFixedCount)
					    : Call.GetArguments().size();
					for (std::size_t ArgumentIndex = 0;
					    ArgumentIndex < ArgumentCompileCount;
					    ++ArgumentIndex)
					{
						const FExpr& RawArgument = *Call.GetArguments()[ArgumentIndex];
						const bool bParamIsRef = ArgumentIndex < FuncDecl->Parameters.size() && FuncDecl->Parameters[ArgumentIndex].bIsRef;
						const bool bArgIsRef = RawArgument.Kind == EExprKind::Ref || (Call.bImplicitReceiverRefBinding && ArgumentIndex == 0 && bParamIsRef);
						if (bArgIsRef != bParamIsRef)
						{
							ReportErrorId(
							    bParamIsRef
							        ? "diag.ref_parameter_requires_ref_argument"
							        : "diag.ref_argument_requires_ref_parameter",
							    Expression.Location);
							return false;
						}
						const FExpr& Argument = UnwrapRefOperand(RawArgument);
						const std::string ParamType = ArgumentIndex < FuncDecl->Parameters.size()
						    ? (!TemplateParameterNames(*FuncDecl).empty()
						              ? SpecializedParamTypes[ArgumentIndex]
						              : ParameterType(*FuncDecl, ArgumentIndex))
						    : std::string{};
						if (!ParamType.empty() && !ValidateDynamicArrayAssignment(ParamType, Argument, Expression.Location))
						{
							return false;
						}
						if (bArgIsRef)
						{
							const bool bMutableRef = !(ArgumentIndex < FuncDecl->Parameters.size() && FuncDecl->Parameters[ArgumentIndex].bIsConst);
							if (!ParamType.empty() && !ValidateRefArgumentType(ParamType, Argument, Expression.Location))
							{
								return false;
							}
							if (!CompileRefArgument(
							        Emitter,
							        Argument,
							        Expression.Location,
							        bMutableRef))
							{
								return false;
							}
							continue;
						}
						{
							const std::string PreviousExpectedType = ExpressionExpectedType;
							if (!ParamType.empty())
							{
								ExpressionExpectedType = ParamType;
							}
							const bool bCompiledArg = CompileExpression(Argument);
							bool bCoerced = bCompiledArg;
							if (bCompiledArg && !ParamType.empty())
							{
								bCoerced = EmitValueCoercionForTarget(
								    Emitter,
								    ParamType,
								    Argument,
								    Expression.Location);
								if (bCoerced && NeedsPassByValueCopy(ParamType) && !CanTransferCoercedValue(Argument, ParamType))
								{
									EmitValueCopy(Emitter, ParamType);
								}
							}
							ExpressionExpectedType = PreviousExpectedType;
							if (!bCompiledArg || !bCoerced)
							{
								return false;
							}
						}
					}

					if (!TemplateParameterNames(*FuncDecl).empty())
					{
						std::unordered_map<std::string, std::string> FunctionSubst;
						for (std::size_t TypeIndex = 0;
						    TypeIndex < TemplateParameterNames(*FuncDecl).size();
						    ++TypeIndex)
						{
							FunctionSubst.emplace(
							    TemplateParameterNames(*FuncDecl)[TypeIndex],
							    ConcreteTypeParams[TypeIndex]);
						}
						const bool bArgumentsEmitted = bGenericVariadic && Call.GetArguments().size() > GenericFixedCount
						    ? EmitVariadicRestArgument(
						          Emitter,
						          FuncDecl->Parameters,
						          Call.GetArguments(),
						          GenericFixedCount,
						          FuncDecl->ModulePath,
						          FunctionSubst,
						          Expression.Location)
						    : EmitTrailingDefaultArguments(
						          Emitter,
						          FuncDecl->Parameters,
						          Call.GetArguments().size(),
						          FuncDecl->ModulePath,
						          FunctionSubst,
						          Expression.Location);
						if (!bArgumentsEmitted)
						{
							return false;
						}
						Emitter.EmitCall(
						    FunctionIndex,
						    static_cast<std::uint8_t>(FuncDecl->Parameters.size()));
						return true;
					}

					if (!EmitTrailingDefaultArguments(
					        Emitter,
					        FuncDecl->Parameters,
					        Call.GetArguments().size(),
					        FuncDecl->ModulePath,
					        {},
					        Expression.Location))
					{
						return false;
					}
					Emitter.EmitCall(
					    FunctionIndex,
					    static_cast<std::uint8_t>(FuncDecl->Parameters.size()));
					return true;
				}

				(void)TryInferBuiltinHandleConstructorType(Call, EffectiveCallee, EffectiveCallee);
				const std::string TypeLinkageName = ResolveTypeLinkageName(EffectiveCallee, CurrentModulePath);
				{
					std::string ResolvedLinkage;
					std::uint16_t GenericTypeIndex = static_cast<std::uint16_t>(-1);
					if (EnsureStructTypeResolved(
					        EffectiveCallee,
					        ResolvedLinkage,
					        &GenericTypeIndex,
					        Expression.Location))
					{
						if (Module->Types[GenericTypeIndex].Kind == EScriptObjectKind::Struct && Call.GetArguments().size() == 1 && Call.GetArguments()[0]->Kind != EExprKind::Ref && ExpressionYieldsUnaliasedByValue(*Call.GetArguments()[0], ResolvedLinkage))
						{
							return CompileExpression(*Call.GetArguments()[0]);
						}
						if (TypeHasUserConstructors(ResolvedLinkage))
						{
							std::vector<std::string> CtorArgTypes;
							CtorArgTypes.reserve(Call.GetArguments().size());
							for (const FExpr* ArgExpr : Call.GetArguments())
							{
								if (!ArgExpr)
								{
									CtorArgTypes.emplace_back();
									continue;
								}
								std::string ArgType;
								const std::string PreviousExpectedType = ExpressionExpectedType;
								ExpressionExpectedType.clear();
								const bool bResolvedArg = TryResolveExprTypeName(*ArgExpr, ArgType);
								const auto EncodedArgType = bResolvedArg ? EncodeOperatorArgType(*ArgExpr) : std::string{};
								ExpressionExpectedType = PreviousExpectedType;
								if (!bResolvedArg)
								{
									ArgType.clear();
								}
								CtorArgTypes.push_back(EncodedArgType);
							}
							const FConstructorDecl* MatchingCtor = nullptr;
							std::string CtorKey;
							if (!ResolveConstructorForCall(
							        ResolvedLinkage,
							        CtorArgTypes,
							        MatchingCtor,
							        CtorKey,
							        Expression.Location) ||
							    !MatchingCtor)
							{
								return false;
							}
							std::uint16_t CtorIndex = FindFunctionIndex(CtorKey);
							if (CtorIndex == static_cast<std::uint16_t>(-1) &&
							    EnsureFunctionStub(CtorKey, "void", static_cast<std::uint8_t>(MatchingCtor->Parameters.size()),
							        true, GenericTypeIndex, false, Expression.Location))
							{
								CtorIndex = FindFunctionIndex(CtorKey);
							}
							if (CtorIndex == static_cast<std::uint16_t>(-1))
							{
								ReportErrorId(
								    "diag.no_matching_constructor_for_type",
								    Expression.Location,
								    {Call.Callee});
								return false;
							}
							std::unordered_map<std::string, std::string> OwnerSubst;
							if (!TryBuildGenericOwnerSubst(
							        ResolvedLinkage,
							        OwnerSubst,
							        Expression.Location))
							{
								ReportError(
								    "failed to resolve type arguments for constructor: " + Call.Callee,
								    Expression.Location);
								return false;
							}
							if (!RequireConcreteType(ResolvedLinkage, Expression.Location))
							{
								return false;
							}
							Emitter.EmitU16Operand(EOpcode::NewUninitializedObject, GenericTypeIndex);
							// Preserve value construction copies; class results stay on the stack.
							const std::uint16_t CtorTemp = Module->Types[GenericTypeIndex].Kind == EScriptObjectKind::Class
							    ? static_cast<std::uint16_t>(-1)
							    : AllocateAssignTemp();
							Emitter.Emit(EOpcode::Dup);
							if (CtorTemp != static_cast<std::uint16_t>(-1))
							{
								Emitter.EmitU16Operand(EOpcode::StoreLocal, CtorTemp);
							}
							for (std::size_t ArgIndex = 0; ArgIndex < Call.GetArguments().size(); ++ArgIndex)
							{
								const FExpr& Argument = *Call.GetArguments()[ArgIndex];
								const std::string ParamType = OwnerSubst.empty()
								    ? SubstituteTypeName(
								          MatchingCtor->Parameters[ArgIndex].TypeName)
								    : SubstituteTypeNameWith(
								          MatchingCtor->Parameters[ArgIndex].TypeName,
								          OwnerSubst);
								const std::string PreviousExpectedType = ExpressionExpectedType;
								ExpressionExpectedType = ParamType;
								const auto& Parameter = MatchingCtor->Parameters[ArgIndex];
								if (Parameter.bIsRef && Argument.Kind != EExprKind::Ref && !(Module->Types[GenericTypeIndex].Kind == EScriptObjectKind::Struct && IsCopyConstructor(*MatchingCtor, ResolvedLinkage, ResolveOwnerModulePath(ResolvedLinkage))))
								{
									ReportError("constructor reference arguments require an explicit ref marker", Expression.Location);
									return false;
								}
								const std::string BoundType = Parameter.bIsRef
								    ? (Parameter.bIsConst ? "const ref " : "ref ") + ParamType
								    : ParamType;
								const bool bCoerced = CompileOperatorArgument(Emitter, Argument, BoundType, Expression.Location);
								ExpressionExpectedType = PreviousExpectedType;
								if (!bCoerced) return false;
							}
							if (!EmitTrailingDefaultArguments(Emitter, MatchingCtor->Parameters, Call.GetArguments().size(),
							        ResolveOwnerModulePath(ResolvedLinkage), OwnerSubst, Expression.Location))
							{
								return false;
							}
							Emitter.EmitCallBoundMethod(
							    CtorIndex,
							    static_cast<std::uint8_t>(MatchingCtor->Parameters.size()));
							Emitter.Emit(EOpcode::Pop);
							if (CtorTemp != static_cast<std::uint16_t>(-1))
							{
								Emitter.EmitU16Operand(EOpcode::TakeLocal, CtorTemp);
							}
							if (Call.GetArguments().empty())
							{
								EmitReflectionScopeBinding(Emitter, GenericTypeIndex);
							}
							return true;
						}
						if (!Call.GetArguments().empty())
						{
							ReportError("type constructor expects no arguments", Expression.Location);
							return false;
						}
						return EmitDefaultTypeConstruction(
						    Emitter,
						    ResolvedLinkage,
						    GenericTypeIndex,
						    Expression.Location);
					}
					if (bHasError)
					{
						return false;
					}
				}
				if (Call.TypeArgs.empty() && !ExpressionExpectedType.empty() && MatchesExpectedGenericCtor(Call.Callee, ExpressionExpectedType))
				{
					std::string ResolvedLinkage;
					std::uint16_t ExpectedTypeIndex = static_cast<std::uint16_t>(-1);
					if (EnsureStructTypeResolved(
					        ExpressionExpectedType,
					        ResolvedLinkage,
					        &ExpectedTypeIndex,
					        Expression.Location))
					{
						if (Module->Types[ExpectedTypeIndex].Kind == EScriptObjectKind::Struct && Call.GetArguments().size() == 1 && Call.GetArguments()[0]->Kind != EExprKind::Ref && ExpressionYieldsUnaliasedByValue(*Call.GetArguments()[0], ResolvedLinkage))
						{
							return CompileExpression(*Call.GetArguments()[0]);
						}
						if (TypeHasUserConstructors(ResolvedLinkage))
						{
							std::vector<std::string> CtorArgTypes;
							CtorArgTypes.reserve(Call.GetArguments().size());
							for (const FExpr* ArgExpr : Call.GetArguments())
							{
								if (!ArgExpr)
								{
									CtorArgTypes.emplace_back();
									continue;
								}
								std::string ArgType;
								const std::string PreviousExpectedType = ExpressionExpectedType;
								ExpressionExpectedType.clear();
								const bool bResolvedArg = TryResolveExprTypeName(*ArgExpr, ArgType);
								const auto EncodedArgType = bResolvedArg ? EncodeOperatorArgType(*ArgExpr) : std::string{};
								ExpressionExpectedType = PreviousExpectedType;
								if (!bResolvedArg)
								{
									ArgType.clear();
								}
								CtorArgTypes.push_back(EncodedArgType);
							}
							const FConstructorDecl* MatchingCtor = nullptr;
							std::string CtorKey;
							if (!ResolveConstructorForCall(
							        ResolvedLinkage,
							        CtorArgTypes,
							        MatchingCtor,
							        CtorKey,
							        Expression.Location) ||
							    !MatchingCtor)
							{
								return false;
							}
							std::uint16_t CtorIndex = FindFunctionIndex(CtorKey);
							if (CtorIndex == static_cast<std::uint16_t>(-1) &&
							    EnsureFunctionStub(CtorKey, "void", static_cast<std::uint8_t>(MatchingCtor->Parameters.size()),
							        true, ExpectedTypeIndex, false, Expression.Location))
							{
								CtorIndex = FindFunctionIndex(CtorKey);
							}
							if (CtorIndex == static_cast<std::uint16_t>(-1))
							{
								ReportErrorId(
								    "diag.no_matching_constructor_for_type",
								    Expression.Location,
								    {Call.Callee});
								return false;
							}
							std::unordered_map<std::string, std::string> OwnerSubst;
							if (!TryBuildGenericOwnerSubst(
							        ResolvedLinkage,
							        OwnerSubst,
							        Expression.Location))
							{
								ReportError(
								    "failed to resolve type arguments for constructor: " + Call.Callee,
								    Expression.Location);
								return false;
							}
							if (!RequireConcreteType(ResolvedLinkage, Expression.Location))
							{
								return false;
							}
							Emitter.EmitU16Operand(EOpcode::NewUninitializedObject, ExpectedTypeIndex);
							// Preserve value construction copies; class results stay on the stack.
							const std::uint16_t CtorTemp = Module->Types[ExpectedTypeIndex].Kind == EScriptObjectKind::Class
							    ? static_cast<std::uint16_t>(-1)
							    : AllocateAssignTemp();
							Emitter.Emit(EOpcode::Dup);
							if (CtorTemp != static_cast<std::uint16_t>(-1))
							{
								Emitter.EmitU16Operand(EOpcode::StoreLocal, CtorTemp);
							}
							for (std::size_t ArgIndex = 0; ArgIndex < Call.GetArguments().size(); ++ArgIndex)
							{
								const FExpr& Argument = *Call.GetArguments()[ArgIndex];
								const std::string ParamType = OwnerSubst.empty()
								    ? SubstituteTypeName(
								          MatchingCtor->Parameters[ArgIndex].TypeName)
								    : SubstituteTypeNameWith(
								          MatchingCtor->Parameters[ArgIndex].TypeName,
								          OwnerSubst);
								const std::string PreviousExpectedType = ExpressionExpectedType;
								ExpressionExpectedType = ParamType;
								const auto& Parameter = MatchingCtor->Parameters[ArgIndex];
								if (Parameter.bIsRef && Argument.Kind != EExprKind::Ref && !(Module->Types[ExpectedTypeIndex].Kind == EScriptObjectKind::Struct && IsCopyConstructor(*MatchingCtor, ResolvedLinkage, ResolveOwnerModulePath(ResolvedLinkage))))
								{
									ReportError("constructor reference arguments require an explicit ref marker", Expression.Location);
									return false;
								}
								const std::string BoundType = Parameter.bIsRef
								    ? (Parameter.bIsConst ? "const ref " : "ref ") + ParamType
								    : ParamType;
								const bool bCoerced = CompileOperatorArgument(Emitter, Argument, BoundType, Expression.Location);
								ExpressionExpectedType = PreviousExpectedType;
								if (!bCoerced) return false;
							}
							if (!EmitTrailingDefaultArguments(Emitter, MatchingCtor->Parameters, Call.GetArguments().size(),
							        ResolveOwnerModulePath(ResolvedLinkage), OwnerSubst, Expression.Location))
							{
								return false;
							}
							Emitter.EmitCallBoundMethod(
							    CtorIndex,
							    static_cast<std::uint8_t>(MatchingCtor->Parameters.size()));
							Emitter.Emit(EOpcode::Pop);
							if (CtorTemp != static_cast<std::uint16_t>(-1))
							{
								Emitter.EmitU16Operand(EOpcode::TakeLocal, CtorTemp);
							}
							return true;
						}
						if (!Call.GetArguments().empty())
						{
							ReportError("type constructor expects no arguments", Expression.Location);
							return false;
						}
						return EmitDefaultTypeConstruction(
						    Emitter,
						    ResolvedLinkage,
						    ExpectedTypeIndex,
						    Expression.Location);
					}
					if (bHasError)
					{
						return false;
					}
				}
				if (!TypeLinkageName.empty())
				{
					if (IsInterfaceTypeName(TypeLinkageName))
					{
						ReportError(
						    "cannot construct interface type: " + Call.Callee,
						    Expression.Location);
						return false;
					}
					const std::string ArityLookupName =
					    ExpandUserTypeAlias(Call.Callee, CurrentModulePath);
					const bool bHasArity0 =
					    FindGenericStructDecl(ArityLookupName, CurrentModulePath, 0) != nullptr || FindGenericClassDecl(ArityLookupName, CurrentModulePath, 0) != nullptr || (ArityLookupName != Call.Callee && (FindGenericStructDecl(Call.Callee, CurrentModulePath, 0) != nullptr || FindGenericClassDecl(Call.Callee, CurrentModulePath, 0) != nullptr));
					if (!bHasArity0)
					{
						// ResolveTypeLinkageName may return a generic template base; do not
						// materialize it as a concrete arity-0 type.
						ReportError(
						    "generic type requires type arguments: " + Call.Callee,
						    Expression.Location);
						return false;
					}
					const std::uint16_t TypeIndex = FindOrAddType(TypeLinkageName);
					if (TypeIndex == static_cast<std::uint16_t>(-1))
					{
						return false;
					}
					if (Module->Types[TypeIndex].Kind == EScriptObjectKind::Struct && Call.GetArguments().size() == 1 && Call.GetArguments()[0]->Kind != EExprKind::Ref && ExpressionYieldsUnaliasedByValue(*Call.GetArguments()[0], TypeLinkageName))
					{
						return CompileExpression(*Call.GetArguments()[0]);
					}
					if (TypeHasUserConstructors(TypeLinkageName))
					{
						std::vector<std::string> CtorArgTypes;
						CtorArgTypes.reserve(Call.GetArguments().size());
						for (const FExpr* ArgExpr : Call.GetArguments())
						{
							if (!ArgExpr)
							{
								CtorArgTypes.emplace_back();
								continue;
							}
							std::string ArgType;
							const std::string PreviousExpectedType = ExpressionExpectedType;
							ExpressionExpectedType.clear();
							const bool bResolvedArg = TryResolveExprTypeName(*ArgExpr, ArgType);
							const auto EncodedArgType = bResolvedArg ? EncodeOperatorArgType(*ArgExpr) : std::string{};
							ExpressionExpectedType = PreviousExpectedType;
							if (!bResolvedArg)
							{
								ArgType.clear();
							}
							CtorArgTypes.push_back(EncodedArgType);
						}
						const FConstructorDecl* MatchingCtor = nullptr;
						std::string CtorKey;
						if (!ResolveConstructorForCall(
						        TypeLinkageName,
						        CtorArgTypes,
						        MatchingCtor,
						        CtorKey,
						        Expression.Location) ||
						    !MatchingCtor)
						{
							return false;
						}
						std::uint16_t CtorIndex = FindFunctionIndex(CtorKey);
						if (CtorIndex == static_cast<std::uint16_t>(-1) &&
						    EnsureFunctionStub(CtorKey, "void", static_cast<std::uint8_t>(MatchingCtor->Parameters.size()),
						        true, TypeIndex, false, Expression.Location))
						{
							CtorIndex = FindFunctionIndex(CtorKey);
						}
						if (CtorIndex == static_cast<std::uint16_t>(-1))
						{
							ReportErrorId(
							    "diag.no_matching_constructor_for_type",
							    Expression.Location,
							    {Call.Callee});
							return false;
						}
						std::unordered_map<std::string, std::string> OwnerSubst;
						if (!TryBuildGenericOwnerSubst(
						        TypeLinkageName,
						        OwnerSubst,
						        Expression.Location))
						{
							ReportError(
							    "failed to resolve type arguments for constructor: " + Call.Callee,
							    Expression.Location);
							return false;
						}
						if (!RequireConcreteType(TypeLinkageName, Expression.Location))
						{
							return false;
						}
						Emitter.EmitU16Operand(EOpcode::NewUninitializedObject, TypeIndex);
						// Preserve value construction copies; class results stay on the stack.
						const std::uint16_t CtorTemp = Module->Types[TypeIndex].Kind == EScriptObjectKind::Class
						    ? static_cast<std::uint16_t>(-1)
						    : AllocateAssignTemp();
						Emitter.Emit(EOpcode::Dup);
						if (CtorTemp != static_cast<std::uint16_t>(-1))
						{
							Emitter.EmitU16Operand(EOpcode::StoreLocal, CtorTemp);
						}
						for (std::size_t ArgIndex = 0; ArgIndex < Call.GetArguments().size(); ++ArgIndex)
						{
							const FExpr& Argument = *Call.GetArguments()[ArgIndex];
							const std::string ParamType = OwnerSubst.empty()
							    ? SubstituteTypeName(
							          MatchingCtor->Parameters[ArgIndex].TypeName)
							    : SubstituteTypeNameWith(
							          MatchingCtor->Parameters[ArgIndex].TypeName,
							          OwnerSubst);
							const std::string PreviousExpectedType = ExpressionExpectedType;
							ExpressionExpectedType = ParamType;
							const auto& Parameter = MatchingCtor->Parameters[ArgIndex];
							if (Parameter.bIsRef && Argument.Kind != EExprKind::Ref && !(Module->Types[TypeIndex].Kind == EScriptObjectKind::Struct && IsCopyConstructor(*MatchingCtor, TypeLinkageName, ResolveOwnerModulePath(TypeLinkageName))))
							{
								ReportError("constructor reference arguments require an explicit ref marker", Expression.Location);
								return false;
							}
							const std::string BoundType = Parameter.bIsRef
							    ? (Parameter.bIsConst ? "const ref " : "ref ") + ParamType
							    : ParamType;
							const bool bCoerced = CompileOperatorArgument(Emitter, Argument, BoundType, Expression.Location);
							ExpressionExpectedType = PreviousExpectedType;
							if (!bCoerced) return false;
						}
						if (!EmitTrailingDefaultArguments(Emitter, MatchingCtor->Parameters, Call.GetArguments().size(),
						        ResolveOwnerModulePath(TypeLinkageName), OwnerSubst, Expression.Location))
						{
							return false;
						}
						Emitter.EmitCallBoundMethod(
						    CtorIndex,
						    static_cast<std::uint8_t>(MatchingCtor->Parameters.size()));
						Emitter.Emit(EOpcode::Pop);
						if (CtorTemp != static_cast<std::uint16_t>(-1))
						{
							Emitter.EmitU16Operand(EOpcode::TakeLocal, CtorTemp);
						}
						return true;
					}
					if (!Call.GetArguments().empty())
					{
						ReportError("type constructor expects no arguments", Expression.Location);
						return false;
					}
					return EmitDefaultTypeConstruction(
					    Emitter,
					    TypeLinkageName,
					    TypeIndex,
					    Expression.Location);
				}

				if (FunctionIndex != static_cast<std::uint16_t>(-1))
				{
					ReportErrorId(
					    "diag.wrong_argument_count_for_function",
					    Expression.Location,
					    {Call.Callee});
					return false;
				}
				// Ordinary call targets have all failed. Inside an instance body,
				// lower a matching script method to the exact same path as
				// `this.Method(...)`; this preserves overload, inheritance,
				// virtual dispatch, and instance-call interception semantics.
				{
					std::string ImplicitOwner;
					const FMethodDecl* ImplicitMethod = nullptr;
					if (ResolveImplicitThisMethodForCall(
					        Call,
					        ImplicitOwner,
					        ImplicitMethod,
					        true) &&
					    ImplicitMethod)
					{
						auto ThisExpr = std::make_unique<FThisExpr>();
						ThisExpr->Location = Expression.Location;
						FMethodCallExpr ImplicitCall(
						    std::move(ThisExpr),
						    Call.Callee,
						    {});
						ImplicitCall.Location = Expression.Location;
						ImplicitCall.TypeArgs = Call.TypeArgs;
						ImplicitCall.bExplicitTemplateArguments = Call.bExplicitTemplateArguments;
						ImplicitCall.BorrowArgumentsFrom(Call.GetArguments());
						bPreserveNextReferenceResult = bCurrentExpressionPreservesReference;
						return CompileExpression(ImplicitCall);
					}
					if (HasError())
					{
						return false;
					}
				}
				// Host-subclass instance method: a bare call that names nothing in script
				// may still be a method on the inherited host base.
				if (Call.TypeArgs.empty() && !Call.CalleeExpr)
				{
					if (const std::string HostBase = CurrentMethodHostBaseTypeName();
					    !HostBase.empty())
					{
						std::uint16_t HostNativeIndex = static_cast<std::uint16_t>(-1);
						if (const FNativeFunction* const HostNative = FindNativeForCall(
						        MakeHostMethodNativeName(HostBase, Call.Callee),
						        Call.GetArguments(), HostNativeIndex, Expression.Location, true, 0,
						        CurrentFunction && CurrentFunction->bIsConstMethod))
						{
							return EmitHostBaseMethodCall(
							    Emitter,
							    Call.Callee,
							    *HostNative,
							    HostNativeIndex,
							    Call.GetArguments(),
							    Expression.Location);
						}
					}
				}
				if (!Call.bStandardLibraryQualified && !Call.bImplementationBuiltinAccess)
				{
					if (const std::string_view Required =
					        StandardLibrary::RequiredNamespaceForFunction(Call.Callee);
					    !Required.empty())
					{
						ReportErrorId(
						    "diag.function_requires_its_standard_namespace",
						    Expression.Location,
						    {Call.Callee, std::string(Required)});
						return false;
					}
				}
				ReportErrorId("diag.unknown_function", Expression.Location, {Call.Callee});
				return false;
			}
