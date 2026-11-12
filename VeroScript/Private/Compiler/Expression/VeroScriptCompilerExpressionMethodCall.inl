// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		case EExprKind::MethodCall:
			{
				const FMethodCallExpr& MethodCall = static_cast<const FMethodCallExpr&>(Expression);
				const auto& MethodArguments = MethodCall.GetArguments();
				if (MethodArguments.size() > std::numeric_limits<std::uint8_t>::max())
				{
					ReportError("method call supports at most 255 arguments", Expression.Location);
					return false;
				}
				if (MethodCall.Object && IsSuperReceiver(*MethodCall.Object))
				{
					if (!MethodCall.TypeArgs.empty())
					{
						ReportError(
						    "script methods do not support explicit type arguments",
						    Expression.Location);
						return false;
					}
					return CompileSuperMethodCall(MethodCall, Emitter);
				}
				std::string OwnerTypeName;
				// `Type.method(...)` / `Ns.Type.method(...)`: a qualified type-name
				// receiver (whose root is not a value binding) selects a static member
				// function; no object is evaluated.
				bool bStaticTypeReceiver = false;
				std::string StaticObjName;
				if (TryResolveTypeNameReceiver(*MethodCall.Object, StaticObjName) && !IsEnumTypeName(StaticObjName))
				{
					bStaticTypeReceiver = true;
					OwnerTypeName = StaticObjName;
				}
				// Namespace-qualified free function: `Ns.Func(...)` / `Ns.Sub.Func(...)`.
				// Lower to the ordinary call path so overloads, defaults, ref arguments,
				// variadics, generics and package linkage all share one implementation.
				if (!bStaticTypeReceiver)
				{
					std::string QualName;
					std::uint16_t QualifiedNativeIndex = 0xFFFF;
					if (TryBuildNamespaceQualifiedName(
					        *MethodCall.Object,
					        MethodCall.Method,
					        QualName) &&
					    (FindFunctionDeclForReference(QualName, CurrentModulePath) || FindNative(QualName, QualifiedNativeIndex)))
					{
						FCallExpr QualifiedCall(QualName, {});
						QualifiedCall.Location = Expression.Location;
						QualifiedCall.TypeArgs = MethodCall.TypeArgs;
						QualifiedCall.bExplicitTemplateArguments = MethodCall.bExplicitTemplateArguments;
						QualifiedCall.BorrowArgumentsFrom(MethodArguments);
						bPreserveNextReferenceResult = bCurrentExpressionPreservesReference;
						return CompileExpression(QualifiedCall);
					}
				}
				bool bOwnerTypeKnown = bStaticTypeReceiver || TryResolveExprTypeName(*MethodCall.Object, OwnerTypeName);
				if (bOwnerTypeKnown && OwnerTypeName == "void")
				{
					ReportError("cannot use void value in expression", Expression.Location);
					return false;
				}
				const bool bBuiltinContainerOwner = bOwnerTypeKnown && (Runtime::IsDynamicArrayTypeName(OwnerTypeName) || Runtime::IsMapTypeName(OwnerTypeName) || Runtime::IsOrderedMapTypeName(OwnerTypeName) || Runtime::IsSetTypeName(OwnerTypeName) || Runtime::IsMultiSetTypeName(OwnerTypeName) || Runtime::IsOrderedMultiSetTypeName(OwnerTypeName) || Runtime::IsQueueTypeName(OwnerTypeName) || Runtime::IsStackTypeName(OwnerTypeName) || Runtime::IsDequeTypeName(OwnerTypeName));
				if (bBuiltinContainerOwner && IsKnownContainerMutator(MethodCall.Method) && IsConstAssignTarget(*MethodCall.Object))
				{
					ReportError(
					    "cannot call mutating method on const: " + MethodCall.Method,
					    Expression.Location);
					return false;
				}
				const bool bReceiverIsConst = IsConstAssignTarget(*MethodCall.Object);
				if (bOwnerTypeKnown && !bStaticTypeReceiver)
				{
					std::string DescriptorBase = GenericTemplateBaseLinkage(
					    QualifyTypeName(OwnerTypeName, CurrentModulePath));
					std::vector<std::string> UnusedArgs;
					if (DescriptorBase.empty())
					{
						(void)SplitUserGenericTypeApp(
						    OwnerTypeName, DescriptorBase, UnusedArgs);
					}
					const bool bFieldOperation = DescriptorBase == "field_info" && (MethodCall.Method == "get" || MethodCall.Method == "set");
					const bool bMethodOperation = DescriptorBase == "method_info" && (MethodCall.Method == "invoke" || MethodCall.Method == "invoke_static");
					if (bFieldOperation || bMethodOperation)
					{
						const bool bStaticInvoke = MethodCall.Method == "invoke_static";
						const bool bSet = MethodCall.Method == "set";
						const bool bGet = MethodCall.Method == "get";
						const std::size_t Minimum = bStaticInvoke ? 0u : (bSet ? 2u : 1u);
						const std::size_t Overhead = bStaticInvoke ? 2u : (bSet ? 4u : (bGet ? 3u : 4u));
						if (MethodCall.TypeArgs.size() != 1 || MethodArguments.size() < Minimum || ((bGet || bSet) && MethodArguments.size() != Minimum) || MethodArguments.size() - (bStaticInvoke ? 0u : 1u) + Overhead > std::numeric_limits<std::uint8_t>::max())
						{
							ReportError("invalid executable reflection type arguments or arity", Expression.Location);
							return false;
						}
						std::unordered_map<std::string, std::string> DescriptorSubst;
						if (!TryBuildGenericOwnerSubst(
						        OwnerTypeName, DescriptorSubst, Expression.Location) ||
						    DescriptorSubst.find("T") == DescriptorSubst.end())
						{
							ReportError("invalid executable reflection descriptor type", Expression.Location);
							return false;
						}
						const std::string ValueType = QualifyTypeName(
						    NormalizeTypeAlias(SubstituteTypeName(MethodCall.TypeArgs[0])),
						    CurrentModulePath);
						if (ValueType.empty() || (bFieldOperation && (ValueType == "void" || Runtime::IsReferenceTypeName(ValueType))))
						{
							ReportError("invalid executable reflection value type", Expression.Location);
							return false;
						}
						if (bSet)
						{
							std::string SourceType;
							if (!TryResolveExprTypeName(*MethodArguments[1], SourceType) || !CanAssignBetweenTypes(ValueType, SourceType, false))
							{
								ReportError("reflected field value has an incompatible source type", Expression.Location);
								return false;
							}
						}
						if (!bStaticInvoke)
						{
							std::string ReceiverType;
							if (!TryResolveExprTypeName(*MethodArguments[0], ReceiverType) || !CanAssignBetweenTypes(DescriptorSubst["T"], ReceiverType, false))
							{
								ReportError("reflected receiver has the wrong type", Expression.Location);
								return false;
							}
						}
						if (!CompileExpression(*MethodCall.Object))
						{
							return false;
						}
						std::size_t FirstArgument = 0;
						if (!bStaticInvoke)
						{
							const FExpr& Receiver = *MethodArguments[0];
							std::uint16_t ReceiverTypeIndex = 0;
							const bool bStructReceiver = Module && ModuleMeta::FindTypeIndex(*Module, DescriptorSubst["T"], ReceiverTypeIndex) && ReceiverTypeIndex < Module->Types.size() && Module->Types[ReceiverTypeIndex].Kind == EScriptObjectKind::Struct;
							if (bStructReceiver && !bGet)
							{
								if (!CompileRefArgument(Emitter, Receiver, Expression.Location, false))
								{
									return false;
								}
							}
							else if (!CompileExpression(Receiver))
							{
								return false;
							}
							FirstArgument = 1;
						}
						std::vector<std::string> ReflectionArgumentTypes;
						if (bMethodOperation)
						{
							for (std::size_t Index = FirstArgument; Index < MethodArguments.size(); ++Index)
							{
								const FExpr& Argument = *MethodArguments[Index];
								std::string ArgumentType;
								if (!TryResolveExprTypeName(UnwrapRefOperand(Argument), ArgumentType))
								{
									ReportError("reflected argument type is unknown", Argument.Location);
									return false;
								}
								if (ArgumentType == "void")
								{
									ReportError("reflected argument must have a value type", Argument.Location);
									return false;
								}
								if (Argument.Kind == EExprKind::Ref)
								{
									if (!CanBindImplicitRefArgument(UnwrapRefOperand(Argument), false))
									{
										ReportError("reflected reference argument must have addressable storage", Argument.Location);
										return false;
									}
									ArgumentType = EncodeRefCallArgType(
									    UnwrapRefOperand(Argument), std::move(ArgumentType));
								}
								ReflectionArgumentTypes.push_back(std::move(ArgumentType));
							}
						}
						FBytecodeConstant TypeConstant;
						TypeConstant.Kind = EConstantKind::Str;
						TypeConstant.bIsLinkageString = true;
						TypeConstant.StringIndex = AddString(bMethodOperation
						        ? std::string(Runtime::ReflectionCallSignaturePrefix) + Runtime::MakeFunctionTypeName(ReflectionArgumentTypes, ValueType)
						        : ValueType);
						Emitter.EmitU16Operand(
						    EOpcode::ConstStr, AddConstant(TypeConstant));
						if (bSet || (bMethodOperation && !bStaticInvoke))
						{
							Emitter.EmitU8Operand(EOpcode::ConstBool,
							    !bStaticInvoke && IsConstAssignTarget(*MethodArguments[0]) ? 1u : 0u);
						}
						for (std::size_t Index = FirstArgument; Index < MethodArguments.size(); ++Index)
						{
							const FExpr& Argument = *MethodArguments[Index];
							if (bMethodOperation && Argument.Kind == EExprKind::Ref)
							{
								// The target descriptor is dynamic. Preserve the binding and
								// validate its ref/const direction before entering the method.
								if (!CompileRefArgument(Emitter, UnwrapRefOperand(Argument),
								        Argument.Location, false))
								{
									return false;
								}
							}
							else if (Argument.Kind == EExprKind::Ref)
							{
								ReportError("reflected field operations require value arguments", Expression.Location);
								return false;
							}
							else
							{
								if (!CompileExpression(Argument)) return false;
								if (bSet)
								{
									// Materialize the checked assignment conversion before the
									// runtime validates the field's exact storage type. This also
									// performs the ordinary struct-to-interface boxing copy.
									if (!EmitValueCoercionForTarget(
									        Emitter, ValueType, Argument, Argument.Location))
									{
										return false;
									}
								}
								// Snapshot each value argument at its evaluation point, before
								// subsequent arguments can mutate the source. Ref arguments retain
								// their original binding; class/interface handles remain shared.
								if (bMethodOperation && NeedsPassByValueCopy(ReflectionArgumentTypes[Index - FirstArgument]) && !CanTransferCoercedValue(Argument, ReflectionArgumentTypes[Index - FirstArgument]))
								{
									EmitValueCopy(Emitter, ReflectionArgumentTypes[Index - FirstArgument]);
								}
							}
						}
						const EBuiltinId Builtin = bGet ? EBuiltinId::ReflectionGet
						    : bSet                      ? EBuiltinId::ReflectionSet
						    : bStaticInvoke             ? EBuiltinId::ReflectionInvokeStatic
						                                : EBuiltinId::ReflectionInvoke;
						Emitter.EmitCallBuiltin(
						    Builtin,
						    static_cast<std::uint8_t>(Overhead + MethodArguments.size() - (bStaticInvoke ? 0u : 1u)));
						if (bSet)
						{
							FBytecodeConstant Zero;
							Zero.Kind = EConstantKind::I32;
							Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Zero));
						}
						return true;
					}
				}
				const FMethodDecl* MethodDecl = nullptr;
				std::string ResolvedMethodOwner;
				std::vector<std::string> ArgTypeNames;
				ArgTypeNames.reserve(MethodArguments.size());
				for (const FExpr* ArgExpr : MethodArguments)
				{
					if (!ArgExpr)
					{
						ArgTypeNames.emplace_back();
						continue;
					}
					const bool bArgIsRef = ArgExpr->Kind == EExprKind::Ref;
					const FExpr& Arg = UnwrapRefOperand(*ArgExpr);
					std::string ArgType;
					if (!TryResolveExprTypeName(Arg, ArgType))
					{
						ArgType.clear();
					}
					else if (bArgIsRef && !ArgType.empty())
					{
						ArgType = EncodeRefCallArgType(Arg, std::move(ArgType));
					}
					ArgTypeNames.push_back(std::move(ArgType));
				}
				if (bOwnerTypeKnown)
				{
					const std::string HostDispatchType =
					    ResolveHostDispatchTypeName(OwnerTypeName);
					bool PreferScriptMethod = false;
					if (!HostDispatchType.empty() && HostDispatchType != OwnerTypeName)
					{
						bool Ambiguous = false;
						PreferScriptMethod = ResolveMethodForCall(OwnerTypeName, MethodCall.Method, ArgTypeNames,
						                         ResolvedMethodOwner, MethodDecl, Expression.Location, true, false, &Ambiguous, bReceiverIsConst,
						                         (MethodCall.bExplicitTemplateArguments || !MethodCall.TypeArgs.empty()) ? &MethodCall.TypeArgs : nullptr) ||
						    Ambiguous;
					}
					const std::string HostNativeName = HostDispatchType.empty()
					    ? MakeHostMethodNativeName(
					          OwnerTypeName.substr(FindLinkageIdentityStart(OwnerTypeName)), MethodCall.Method)
					    : MakeHostMethodNativeName(HostDispatchType, MethodCall.Method);
					std::uint16_t HostNativeIndex = static_cast<std::uint16_t>(-1);
					if (const FNativeFunction* const HostNative =
					        PreferScriptMethod ? nullptr : FindNativeForCall(HostNativeName, MethodArguments, HostNativeIndex, Expression.Location, true, bStaticTypeReceiver ? 1 : 0, bReceiverIsConst))
					{
						if (!MethodCall.TypeArgs.empty())
						{
							ReportError(
							    "host methods do not support explicit type arguments",
							    Expression.Location);
							return false;
						}
						const bool bHostStatic =
						    HostNative->bHostStaticMethod || HostNative->ParamTypes.empty() || HostNative->ParamTypes[0] != ENativeType::HostObject;
						if (bStaticTypeReceiver && !bHostStatic)
						{
							ReportError(
							    "cannot call non-static host method '" + MethodCall.Method + "' without an instance",
							    Expression.Location);
							return false;
						}
						if (!bStaticTypeReceiver && bHostStatic)
						{
							// instance.staticHostMethod — evaluate object for side effects then drop.
						}
						const std::size_t ExpectedArgs = bHostStatic
						    ? HostNative->ParamTypes.size()
						    : (HostNative->ParamTypes.empty()
						              ? 0
						              : HostNative->ParamTypes.size() - 1);
						if (MethodArguments.size() != ExpectedArgs)
						{
							ReportError(
							    "wrong argument count for method: " + MethodCall.Method,
							    Expression.Location);
							return false;
						}
						if (bStaticTypeReceiver)
						{
							// no receiver
						}
						else if (!bHostStatic && NativeParamFlagIsMutableRef(NativeParamFlagAt(*HostNative, 0)) && CanBindImplicitRefArgument(*MethodCall.Object, true))
						{
							if (!CompileRefArgument(Emitter, *MethodCall.Object, Expression.Location, true))
							{
								return false;
							}
						}
						else if (!CompileExpression(*MethodCall.Object))
						{
							return false;
						}
						else if (bHostStatic)
						{
							Emitter.Emit(EOpcode::Pop);
						}
						for (std::size_t ArgumentIndex = 0;
						    ArgumentIndex < MethodArguments.size();
						    ++ArgumentIndex)
						{
							const std::size_t ParamIndex =
							    bHostStatic ? ArgumentIndex : (ArgumentIndex + 1);
							if (MethodArguments[ArgumentIndex]->Kind == EExprKind::Ref && !NativeParamFlagIsRef(NativeParamFlagAt(*HostNative, ParamIndex)))
							{
								ReportErrorId("diag.native_does_not_accept_ref_arguments",
								    Expression.Location);
								return false;
							}
							const std::string ParamTypeName =
							    ResolveNativeParamTypeName(*HostNative, ParamIndex);
							if (MethodArguments[ArgumentIndex]->Kind == EExprKind::Ref)
							{
								const auto Flag = NativeParamFlagAt(*HostNative, ParamIndex);
								if (!CompileOperatorArgument(Emitter, *MethodArguments[ArgumentIndex],
								        ((Flag & NativeParamFlagConstRef) ? "const ref " : "ref ") + ParamTypeName, Expression.Location))
								{
									return false;
								}
								continue;
							}
							std::string NumericArgumentType;
							if (Numerics::ArgumentRole(*HostNative, ParamIndex) !=
							        Numerics::EArgumentRole::Ordinary &&
							    (!TryResolveExprTypeName(*MethodArguments[ArgumentIndex],
							         NumericArgumentType) ||
							        !Numerics::ValidateArgumentType(
							            *HostNative, ParamIndex,
							            CanonicalTypeNameForCoercion(NumericArgumentType))))
							{
								ReportErrorId("diag.numeric_type_mismatch", Expression.Location);
								return false;
							}
							const bool bNumericInteger = Numerics::ArgumentRole(*HostNative, ParamIndex) ==
							    Numerics::EArgumentRole::Integer;
							const std::string PreviousExpectedType = ExpressionExpectedType;
							ExpressionExpectedType = bNumericInteger ? std::string{} : ParamTypeName;
							const bool bCompiled = CompileExpression(*MethodArguments[ArgumentIndex]);
							const bool bCoerced =
							    bCompiled && (bNumericInteger || EmitValueCoercionForTarget(Emitter, ParamTypeName, *MethodArguments[ArgumentIndex], Expression.Location));
							ExpressionExpectedType = PreviousExpectedType;
							if (!bCompiled || !bCoerced)
							{
								return false;
							}
						}
						const std::uint8_t EmitArgCount = static_cast<std::uint8_t>(
						    bHostStatic ? MethodArguments.size() : MethodArguments.size() + 1);
						Emitter.EmitCallNative(HostNativeIndex, EmitArgCount);
						if (HostNative->ReturnType == ENativeType::Void)
						{
							FBytecodeConstant Zero;
							Zero.Kind = EConstantKind::I32;
							Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Zero));
						}
						return true;
					}
				}
				if (HasError()) return false;
				if (bOwnerTypeKnown)
				{
					if (!ResolveMethodForCall(
					        OwnerTypeName,
					        MethodCall.Method,
					        ArgTypeNames,
					        ResolvedMethodOwner,
					        MethodDecl,
					        Expression.Location,
					        true,
					        true,
					        nullptr,
					        bReceiverIsConst,
					        (MethodCall.bExplicitTemplateArguments || !MethodCall.TypeArgs.empty()) ? &MethodCall.TypeArgs : nullptr) &&
					    HasError())
					{
						return false;
					}
					if (MethodDecl && !ValidateMemberAccess(Expression.Location, OwnerTypeName, MethodCall.Method, true, ArgTypeNames.size(), MethodDecl))
					{
						return false;
					}
				}
				// C++-style static member function call (Type.m(...) or instance.m(...)).
				const bool bStaticCall = MethodDecl && MethodDecl->bIsStatic;
				if (bStaticTypeReceiver && MethodDecl && !MethodDecl->bIsStatic)
				{
					ReportError(
					    "cannot call non-static method '" + MethodCall.Method + "' without an instance",
					    Expression.Location);
					return false;
				}
				if (bReceiverIsConst && MethodDecl && !MethodDecl->bIsConst && !MethodDecl->bIsStatic)
				{
					ReportErrorId(
					    "diag.cannot_call_mutating_method_on_const",
					    Expression.Location,
					    {MethodCall.Method});
					return false;
				}
				std::string DynamicArrayElementType;
				const bool bDynamicArrayPush = bOwnerTypeKnown && MethodCall.Method == "push" && Runtime::ParseDynamicArrayElementType(OwnerTypeName, DynamicArrayElementType);
				const bool bDynamicArrayClear = bOwnerTypeKnown && MethodCall.Method == "clear" && Runtime::IsDynamicArrayTypeName(OwnerTypeName);
				const bool bDynamicArrayRemove = bOwnerTypeKnown && MethodCall.Method == "remove" && Runtime::IsDynamicArrayTypeName(OwnerTypeName);
				const bool bDynamicArrayRemoveAt = bOwnerTypeKnown && MethodCall.Method == "remove_at" && Runtime::IsDynamicArrayTypeName(OwnerTypeName);
				const bool bDynamicArrayReserve = bOwnerTypeKnown && MethodCall.Method == "reserve" && Runtime::IsDynamicArrayTypeName(OwnerTypeName);
				const bool bDynamicArrayCapacity = bOwnerTypeKnown && MethodCall.Method == "capacity" && Runtime::IsDynamicArrayTypeName(OwnerTypeName);
				std::string ContainerArgType;
				std::string ContainerSecondArgType;
				bool bContainerTypedArg = false;
				bool bContainerSecondTypedArg = false;
				const bool bMultiMapAdd = bOwnerTypeKnown && MethodCall.Method == "add" && (Runtime::IsMultiMapTypeName(OwnerTypeName) || Runtime::IsOrderedMultiMapTypeName(OwnerTypeName));
				bool bKnownContainerMethod = bDynamicArrayPush || bDynamicArrayClear || bDynamicArrayRemove || bDynamicArrayRemoveAt || bDynamicArrayReserve || bDynamicArrayCapacity;
				if (bDynamicArrayRemove)
				{
					bContainerTypedArg = Runtime::ParseDynamicArrayElementType(
					    OwnerTypeName,
					    ContainerArgType);
				}
				else if (bDynamicArrayRemoveAt || bDynamicArrayReserve)
				{
					ContainerArgType = "int64";
					bContainerTypedArg = true;
				}
				if (bOwnerTypeKnown)
				{
					if (Runtime::IsMapTypeName(OwnerTypeName) && (MethodCall.Method == "has" || (Runtime::IsMultiMapTypeName(OwnerTypeName) && MethodCall.Method == "add") || MethodCall.Method == "remove" || MethodCall.Method == "keys" || MethodCall.Method == "values" || MethodCall.Method == "entries" || MethodCall.Method == "reserve" || MethodCall.Method == "clear"))
					{
						bKnownContainerMethod = true;
						if (MethodCall.Method == "reserve")
						{
							ContainerArgType = "int64";
							bContainerTypedArg = true;
						}
						else if (MethodCall.Method == "has" || MethodCall.Method == "remove" || MethodCall.Method == "add")
						{
							std::string ValueType;
							bContainerTypedArg = ParseMapTypeForCompile(
							    OwnerTypeName,
							    ContainerArgType,
							    ValueType);
							if (MethodCall.Method == "add")
							{
								std::string RawKeyType;
								bContainerSecondTypedArg = Runtime::SplitMapType(
								    OwnerTypeName,
								    RawKeyType,
								    ContainerSecondArgType);
							}
						}
					}
					else if (Runtime::IsSetTypeName(OwnerTypeName) && (MethodCall.Method == "add" || MethodCall.Method == "has" || MethodCall.Method == "remove" || MethodCall.Method == "values" || MethodCall.Method == "reserve" || MethodCall.Method == "clear"))
					{
						bKnownContainerMethod = true;
						if (MethodCall.Method == "reserve")
						{
							ContainerArgType = "int64";
							bContainerTypedArg = true;
						}
						else if (MethodCall.Method != "values" && MethodCall.Method != "clear")
						{
							bContainerTypedArg = ParseSetElementTypeForCompile(
							    OwnerTypeName,
							    ContainerArgType);
						}
					}
					else if ((Runtime::IsMultiSetTypeName(OwnerTypeName) || Runtime::IsOrderedMultiSetTypeName(OwnerTypeName)) && (MethodCall.Method == "add" || MethodCall.Method == "has" || MethodCall.Method == "count" || MethodCall.Method == "remove" || MethodCall.Method == "values" || MethodCall.Method == "reserve" || MethodCall.Method == "clear"))
					{
						bKnownContainerMethod = true;
						if (MethodCall.Method == "reserve")
						{
							ContainerArgType = "int64";
							bContainerTypedArg = true;
						}
						else if (MethodCall.Method != "values" && MethodCall.Method != "clear")
						{
							bContainerTypedArg = Runtime::ParseMultiSetElementType(
							    OwnerTypeName,
							    ContainerArgType);
						}
					}
					else if (Runtime::IsQueueTypeName(OwnerTypeName) && (MethodCall.Method == "push" || MethodCall.Method == "pop" || (MethodCall.Method == "front" && !Runtime::IsPriorityQueueTypeName(OwnerTypeName)) || (MethodCall.Method == "top" && Runtime::IsPriorityQueueTypeName(OwnerTypeName)) || MethodCall.Method == "values" || MethodCall.Method == "clear"))
					{
						bKnownContainerMethod = true;
						if (MethodCall.Method == "push")
						{
							bContainerTypedArg = Runtime::ParseQueueElementType(
							    OwnerTypeName,
							    ContainerArgType);
						}
					}
					else if (Runtime::IsStackTypeName(OwnerTypeName) && (MethodCall.Method == "push" || MethodCall.Method == "pop" || MethodCall.Method == "top" || MethodCall.Method == "values" || MethodCall.Method == "clear"))
					{
						bKnownContainerMethod = true;
						if (MethodCall.Method == "push")
						{
							bContainerTypedArg = Runtime::ParseStackElementType(
							    OwnerTypeName,
							    ContainerArgType);
						}
					}
					else if (Runtime::IsOrderedMapTypeName(OwnerTypeName) && (MethodCall.Method == "has" || (Runtime::IsOrderedMultiMapTypeName(OwnerTypeName) && MethodCall.Method == "add") || MethodCall.Method == "remove" || MethodCall.Method == "keys" || MethodCall.Method == "values" || MethodCall.Method == "entries" || MethodCall.Method == "reserve" || MethodCall.Method == "clear"))
					{
						bKnownContainerMethod = true;
						if (MethodCall.Method == "reserve")
						{
							ContainerArgType = "int64";
							bContainerTypedArg = true;
						}
						else if (MethodCall.Method == "has" || MethodCall.Method == "remove" || MethodCall.Method == "add")
						{
							std::string ValueType;
							bContainerTypedArg = ParseOrderedMapTypeForCompile(
							    OwnerTypeName,
							    ContainerArgType,
							    ValueType);
							if (MethodCall.Method == "add")
							{
								std::string RawKeyType;
								bContainerSecondTypedArg = Runtime::SplitOrderedMapType(
								    OwnerTypeName,
								    RawKeyType,
								    ContainerSecondArgType);
							}
						}
					}
					else if (Runtime::IsDequeTypeName(OwnerTypeName) && (MethodCall.Method == "push_back" || MethodCall.Method == "push_front" || MethodCall.Method == "pop_back" || MethodCall.Method == "pop_front" || MethodCall.Method == "front" || MethodCall.Method == "back" || MethodCall.Method == "values" || MethodCall.Method == "clear"))
					{
						bKnownContainerMethod = true;
						if (MethodCall.Method == "push_back" || MethodCall.Method == "push_front")
						{
							bContainerTypedArg = Runtime::ParseDequeElementType(
							    OwnerTypeName,
							    ContainerArgType);
						}
					}
				}
				if (bOwnerTypeKnown && !MethodDecl)
				{
					std::string BuiltinBase = GenericTemplateBaseLinkage(OwnerTypeName);
					std::vector<std::string> BuiltinArgs;
					if (BuiltinBase.empty())
					{
						(void)SplitUserGenericTypeApp(
						    OwnerTypeName, BuiltinBase, BuiltinArgs);
					}
					const auto FindBuiltinMethod =
					    [&](const auto& Declarations) -> const FMethodDecl*
					{
						for (const auto& Decl : Declarations)
						{
							if (Decl.Name != BuiltinBase)
							{
								continue;
							}
							for (const FMethodDecl& Method : Decl.Methods)
							{
								if (Method.Name == MethodCall.Method && ParamsAcceptArgCount(Method.Parameters, MethodArguments.size()))
								{
									return &Method;
								}
							}
						}
						return nullptr;
					};
					MethodDecl = FindBuiltinMethod(BuiltinStructTemplates);
					if (!MethodDecl)
					{
						MethodDecl = FindBuiltinMethod(BuiltinClassTemplates);
					}
					if (MethodDecl)
					{
						ResolvedMethodOwner = OwnerTypeName;
					}
				}
				if (bOwnerTypeKnown && !bStaticTypeReceiver && !MethodDecl && !bKnownContainerMethod)
				{
					std::vector<std::string> ExtensionArgTypes;
					ExtensionArgTypes.reserve(ArgTypeNames.size() + 1);
					ExtensionArgTypes.push_back(OwnerTypeName);
					ExtensionArgTypes.insert(
					    ExtensionArgTypes.end(),
					    ArgTypeNames.begin(),
					    ArgTypeNames.end());
					std::vector<std::string> RefExtensionArgTypes = ExtensionArgTypes;
					RefExtensionArgTypes[0] = EncodeRefCallArgType(
					    *MethodCall.Object,
					    OwnerTypeName);
					const bool bReceiverCanBindRef = CanBindImplicitRefArgument(
					    *MethodCall.Object,
					    !bReceiverIsConst);
					const bool bBindReceiverByRef = bReceiverCanBindRef && HasExtensionMethodCandidate(MethodCall.Method, MethodCall.TypeArgs, RefExtensionArgTypes);
					if (bBindReceiverByRef || HasExtensionMethodCandidate(MethodCall.Method, MethodCall.TypeArgs, ExtensionArgTypes))
					{
						FCallExpr ExtensionCall(MethodCall.Method, {});
						ExtensionCall.Location = Expression.Location;
						ExtensionCall.TypeArgs = MethodCall.TypeArgs;
						ExtensionCall.bExplicitTemplateArguments = MethodCall.bExplicitTemplateArguments;
						ExtensionCall.BorrowArgumentsFrom(MethodArguments);
						ExtensionCall.BorrowLeadingArgument(*MethodCall.Object);
						ExtensionCall.bImplicitReceiverArgument = true;
						ExtensionCall.bImplicitReceiverRefBinding = bBindReceiverByRef;
						bPreserveNextReferenceResult = bCurrentExpressionPreservesReference;
						return CompileExpression(ExtensionCall);
					}
				}
				if (MethodDecl && !EnsureMethodSpecialization(*MethodDecl, ResolvedMethodOwner.empty() ? OwnerTypeName : ResolvedMethodOwner, Expression.Location))
				{
					return false;
				}
				if (bOwnerTypeKnown && !MethodDecl && !bKnownContainerMethod)
				{
					ReportError("unknown method: " + MethodCall.Method, Expression.Location);
					return false;
				}
				// Inherited signatures belong to the instantiated declaring base.
				const std::string& SignatureOwner = ResolvedMethodOwner.empty()
				    ? OwnerTypeName
				    : ResolvedMethodOwner;
				const std::string MethodOwnerModulePath = bOwnerTypeKnown
				    ? ResolveOwnerModulePath(SignatureOwner)
				    : std::string{};
				const std::string MethodTypeModulePath = MethodOwnerModulePath.empty()
				    ? CurrentModulePath
				    : MethodOwnerModulePath;
				std::unordered_map<std::string, std::string> OwnerSubst;
				const bool bGenericOwner = bOwnerTypeKnown && GenericTemplateBaseLinkage(FindGenericTypeMarker(SignatureOwner) != std::string::npos ? SignatureOwner : QualifyTypeName(SignatureOwner, MethodTypeModulePath)).size() > 0;
				if (MethodDecl && bGenericOwner && !TryBuildGenericOwnerSubst(SignatureOwner, OwnerSubst, Expression.Location))
				{
					ReportError(
					    "could not decode generic owner type: " + DisplayTypeName(SignatureOwner),
					    Expression.Location);
					return false;
				}
				if (MethodDecl && !ParamsAcceptArgCount(MethodDecl->Parameters, MethodArguments.size()))
				{
					ReportError(
					    "wrong argument count for method: " + MethodCall.Method,
					    Expression.Location);
					return false;
				}
				if (bKnownContainerMethod)
				{
					const int ExpectedArgs =
					    bMultiMapAdd ? 2 : ExpectedContainerMethodArgCount(MethodCall.Method);
					if (ExpectedArgs >= 0 && static_cast<int>(MethodArguments.size()) != ExpectedArgs)
					{
						ReportError(
						    "wrong argument count for method: " + MethodCall.Method,
						    Expression.Location);
						return false;
					}
				}
				if (bStaticTypeReceiver)
				{
					// No receiver is evaluated for `Type.staticMethod(...)`.
				}
				else if (!CompileBorrowedReceiver(Emitter, *MethodCall.Object))
				{
					return false;
				}
				else if (bStaticCall)
				{
					// `instance.staticMethod(...)`: evaluate the object for side effects,
					// then discard it — a static call takes no receiver.
					Emitter.Emit(EOpcode::Pop);
				}
				const bool bMethodVariadic = MethodDecl && IsVariadicParamList(MethodDecl->Parameters);
				const std::size_t MethodFixedCount = MethodDecl
				    ? FixedParamCount(MethodDecl->Parameters)
				    : 0;
				const std::size_t MethodArgCount = MethodArguments.size();
				const std::size_t MethodFixedCompile = bMethodVariadic
				    ? (std::min)(MethodArgCount, MethodFixedCount)
				    : MethodArgCount;
				bool bTaskTimeout = false;
				if (MethodDecl && MethodDecl->Body && MethodDecl->Body->Statements.size() == 1 && MethodDecl->Body->Statements.front()->Kind == EStmtKind::Return)
				{
					const auto& Value = static_cast<const FReturnStmt&>(*MethodDecl->Body->Statements.front()).Value;
					if (Value && Value->Kind == EExprKind::Call)
					{
						const auto& Implementation = static_cast<const FCallExpr&>(*Value);
						bTaskTimeout = Implementation.bImplementationBuiltinAccess && Implementation.Callee == "__typed_task_await_for";
					}
				}
				for (std::size_t ArgumentIndex = 0; ArgumentIndex < MethodFixedCompile; ++ArgumentIndex)
				{
					const FExpr& RawArgument = *MethodArguments[ArgumentIndex];
					if (bTaskTimeout && ArgumentIndex == 0)
					{
						std::string SourceType;
						if (!TryResolveExprTypeName(RawArgument, SourceType) || !StandardLibrary::IsScalarParseType(CanonicalTypeNameForCoercion(SourceType), true))
						{
							ReportErrorId("diag.stdlib_arguments", RawArgument.Location);
							return false;
						}
					}
					const bool bArgIsRef = RawArgument.Kind == EExprKind::Ref;
					const bool bParamIsRef = MethodDecl && ArgumentIndex < MethodDecl->Parameters.size() && MethodDecl->Parameters[ArgumentIndex].bIsRef;
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
					const std::string ParamType = MethodDecl && ArgumentIndex < MethodDecl->Parameters.size()
					    ? QualifyMethodTypeName(*MethodDecl,
					          MethodDecl->Parameters[ArgumentIndex].TypeName, MethodTypeModulePath, OwnerSubst)
					    : std::string{};
					if (MethodDecl && ArgumentIndex < MethodDecl->Parameters.size() && !ValidateDynamicArrayAssignment(ParamType, Argument, Expression.Location))
					{
						return false;
					}
					if (bDynamicArrayPush && ArgumentIndex == 0 && !ValidateDynamicArrayAssignment(DynamicArrayElementType, Argument, Expression.Location))
					{
						return false;
					}
					if (bContainerTypedArg && ArgumentIndex == 0 && !ValidateDynamicArrayAssignment(ContainerArgType, Argument, Expression.Location))
					{
						return false;
					}
					if (bContainerSecondTypedArg && ArgumentIndex == 1 && !ValidateDynamicArrayAssignment(ContainerSecondArgType, Argument, Expression.Location))
					{
						return false;
					}
					if (bArgIsRef)
					{
						const bool bMutableRef = !(MethodDecl && ArgumentIndex < MethodDecl->Parameters.size() && MethodDecl->Parameters[ArgumentIndex].bIsConst);
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
						std::string ExpectedForArg;
						if (MethodDecl && ArgumentIndex < MethodDecl->Parameters.size())
						{
							ExpectedForArg = ParamType;
						}
						else if (bDynamicArrayPush && ArgumentIndex == 0)
						{
							ExpectedForArg = DynamicArrayElementType;
						}
						else if (bContainerTypedArg && ArgumentIndex == 0)
						{
							ExpectedForArg = ContainerArgType;
						}
						else if (bContainerSecondTypedArg && ArgumentIndex == 1)
						{
							ExpectedForArg = ContainerSecondArgType;
						}
						if (!ExpectedForArg.empty())
						{
							ExpressionExpectedType = ExpectedForArg;
						}
						const bool bCompiledArg = CompileExpression(Argument);
						bool bCoerced = bCompiledArg;
						if (bCompiledArg && !ExpectedForArg.empty())
						{
							bCoerced = EmitValueCoercionForTarget(
							    Emitter,
							    ExpectedForArg,
							    Argument,
							    Expression.Location);
						}
						ExpressionExpectedType = PreviousExpectedType;
						if (!bCompiledArg || !bCoerced)
						{
							return false;
						}
						const std::string ArgCopyType = ExpectedForArg;
						if (!ArgCopyType.empty() && NeedsPassByValueCopy(ArgCopyType) && !CanTransferCoercedValue(Argument, ArgCopyType))
						{
							EmitValueCopy(Emitter, ArgCopyType);
						}
					}
				}
				std::uint8_t MethodArgEmitCount = static_cast<std::uint8_t>(MethodArguments.size());
				if (MethodDecl)
				{
					if (bMethodVariadic)
					{
						if (MethodArgCount > MethodFixedCount)
						{
							if (!EmitVariadicRestArgument(
							        Emitter,
							        MethodDecl->Parameters,
							        MethodArguments,
							        MethodFixedCount,
							        MethodTypeModulePath,
							        OwnerSubst,
							        Expression.Location))
							{
								return false;
							}
						}
						else if (!EmitTrailingDefaultArguments(
						             Emitter,
						             MethodDecl->Parameters,
						             MethodArgCount,
						             MethodTypeModulePath,
						             OwnerSubst,
						             Expression.Location))
						{
							return false;
						}
					}
					else if (!EmitTrailingDefaultArguments(
					             Emitter,
					             MethodDecl->Parameters,
					             MethodArguments.size(),
					             MethodTypeModulePath,
					             OwnerSubst,
					             Expression.Location))
					{
						return false;
					}
					MethodArgEmitCount = static_cast<std::uint8_t>(MethodDecl->Parameters.size());
				}
				if (bStaticCall)
				{
					// Static member functions are compiled as free functions; invoke by
					// index with no receiver on the stack.
					std::string StaticOwnerLinkage = ResolvedMethodOwner;
					if (StaticOwnerLinkage.empty())
					{
						StaticOwnerLinkage = ResolveTypeLinkageName(OwnerTypeName, CurrentModulePath);
					}
					// Materialize the declaring instantiation, including inherited
					// static methods reached through a different generic receiver.
					{
						const bool bNeedsEnsure =
						    SignatureOwner.find('<') != std::string::npos || FindGenericTypeMarker(SignatureOwner) != std::string::npos || FindGenericTypeMarker(StaticOwnerLinkage) != std::string::npos;
						if (bNeedsEnsure)
						{
							std::string EnsuredLinkage;
							if (EnsureStructTypeResolved(
							        SignatureOwner, EnsuredLinkage, nullptr, Expression.Location) ||
							    EnsureInterfaceTypeResolved(
							        SignatureOwner, EnsuredLinkage, nullptr, Expression.Location))
							{
								if (!EnsuredLinkage.empty())
								{
									StaticOwnerLinkage = EnsuredLinkage;
								}
							}
						}
					}
					// Type layout can materialize this owner before its statics are initialized.
					// Initialize that queued dependency before the call, preserving other owners.
					if (bStaticFieldInitializationStarted && CurrentFunction == nullptr &&
					    std::find(PendingGenericStaticFieldInits.begin(), PendingGenericStaticFieldInits.end(), StaticOwnerLinkage) != PendingGenericStaticFieldInits.end() &&
					    !FlushPendingGenericStaticFieldInitializers(StaticOwnerLinkage))
					{
						return false;
					}
					const std::vector<std::string> StaticParamTypes =
					    CanonicalMethodParamTypeNames(*MethodDecl, MethodTypeModulePath, OwnerSubst);
					const std::string StaticMethodKey = MakeMethodFunctionName(
					    StaticOwnerLinkage,
					    MethodDecl->Name,
					    StaticParamTypes,
					    MethodDecl->bIsConst);
					const std::uint16_t StaticFunctionIndex = FindFunctionIndex(StaticMethodKey);
					if (StaticFunctionIndex == static_cast<std::uint16_t>(-1))
					{
						ReportError(
						    "static method not found: " + MethodCall.Method,
						    Expression.Location);
						return false;
					}
					Emitter.EmitCall(StaticFunctionIndex, MethodArgEmitCount);
					return true;
				}
				const bool bOwnerIsInterface =
				    IsInterfaceTypeName(OwnerTypeName) || IsInterfaceTypeName(QualifyTypeName(OwnerTypeName, CurrentModulePath));
				const auto EmitInterfaceStaticDefault = [&]() -> bool
				{
					std::string DeclOwnerLinkage = ResolvedMethodOwner;
					if (DeclOwnerLinkage.empty())
					{
						const FMethodDecl* DeclMethod = nullptr;
						// Non-fatal lookup: a miss falls back to dynamic dispatch below.
						if (!ResolveMethodForCall(
						        OwnerTypeName,
						        MethodCall.Method,
						        ArgTypeNames,
						        DeclOwnerLinkage,
						        DeclMethod,
						        Expression.Location,
						        true,
						        false,
						        nullptr,
						        bReceiverIsConst,
						        (MethodCall.bExplicitTemplateArguments || !MethodCall.TypeArgs.empty()) ? &MethodCall.TypeArgs : nullptr))
						{
							DeclOwnerLinkage.clear();
						}
					}
					if (DeclOwnerLinkage.empty() || !IsInterfaceTypeName(DeclOwnerLinkage))
					{
						return false;
					}
					const std::vector<std::string> ParamTypes = CanonicalMethodParamTypeNames(
					    *MethodDecl,
					    MethodTypeModulePath,
					    OwnerSubst);
					const std::string MethodKey = MakeMethodFunctionName(
					    DeclOwnerLinkage,
					    MethodDecl->Name,
					    ParamTypes,
					    MethodDecl->bIsConst);
					const std::uint16_t FunctionIndex = FindFunctionIndex(MethodKey);
					if (FunctionIndex == static_cast<std::uint16_t>(-1))
					{
						return false;
					}
					Emitter.EmitCallBoundMethod(FunctionIndex, MethodArgEmitCount);
					return true;
				};
				// Interface receiver: abstract / virtual default → CallMethod;
				// non-virtual default → CallBoundMethod (static bind to the declaring
				// interface's compiled default). Generic interface defaults are not emitted
				// as standalone functions, so when the static bind is unavailable fall back
				// to dynamic dispatch — safe because a non-virtual default cannot be shadowed
				// (VerifyInterfaceConformance rejects it), leaving one reachable body.
				if (MethodDecl && bOwnerIsInterface)
				{
					if ((MethodDecl->Body || MethodSpecializationKeys.count(MethodDecl) != 0) && !MethodDecl->bVirtual)
					{
						if (!EmitInterfaceStaticDefault())
						{
							Emitter.EmitCallMethod(
							    AddString(MakeMethodCallLeafName(
							        MethodCall.Method,
							        *MethodDecl,
							        MethodTypeModulePath,
							        OwnerSubst)),
							    MethodArgEmitCount);
						}
					}
					else
					{
						Emitter.EmitCallMethod(
						    AddString(MakeMethodCallLeafName(
						        MethodCall.Method,
						        *MethodDecl,
						        MethodTypeModulePath,
						        OwnerSubst)),
						    MethodArgEmitCount);
					}
				}
				else if (MethodDecl && IsVirtualOrOverridesVirtual(OwnerTypeName, *MethodDecl))
				{
					Emitter.EmitCallMethod(
					    AddString(MakeMethodCallLeafName(
					        MethodCall.Method,
					        *MethodDecl,
					        MethodTypeModulePath,
					        OwnerSubst)),
					    MethodArgEmitCount);
				}
				else if (MethodDecl)
				{
					// Prefer owner resolved by ResolveMethodForCall (signature-aware, bases before
					// interfaces). Re-walking FindMethodDeclOwnerLinkage can prefer an interface
					// default and incorrectly emit CallMethod for a non-virtual base method.
					std::string DeclOwnerLinkage = ResolvedMethodOwner;
					std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
					if (DeclOwnerLinkage.empty())
					{
						const FMethodDecl* DeclMethod = nullptr;
						if (!ResolveMethodForCall(
						        OwnerTypeName,
						        MethodCall.Method,
						        ArgTypeNames,
						        DeclOwnerLinkage,
						        DeclMethod,
						        Expression.Location,
						        true,
						        true,
						        nullptr,
						        bReceiverIsConst,
						        (MethodCall.bExplicitTemplateArguments || !MethodCall.TypeArgs.empty()) ? &MethodCall.TypeArgs : nullptr))
						{
							DeclOwnerLinkage.clear();
						}
					}
					if (!DeclOwnerLinkage.empty())
					{
						const std::vector<std::string> ParamTypes =
						    CanonicalMethodParamTypeNames(
						        *MethodDecl,
						        MethodTypeModulePath,
						        OwnerSubst);
						// Interface default dispatch on concrete receivers:
						// - non-virtual default → CallBoundMethod to Iface::Method
						// - virtual / abstract → CallMethod (dynamic)
						if (IsInterfaceTypeName(DeclOwnerLinkage))
						{
							if ((MethodDecl->Body || MethodSpecializationKeys.count(MethodDecl) != 0) && !MethodDecl->bVirtual)
							{
								const std::string MethodKey = MakeMethodFunctionName(
								    DeclOwnerLinkage,
								    MethodDecl->Name,
								    ParamTypes, MethodDecl->bIsConst);
								FunctionIndex = FindFunctionIndex(MethodKey);
								if (FunctionIndex == static_cast<std::uint16_t>(-1))
								{
									// Generic interface defaults have no standalone function;
									// fall back to dynamic dispatch (shadowing is rejected, so a
									// single body is reachable and the result is identical).
									Emitter.EmitCallMethod(
									    AddString(MakeMethodCallLeafName(
									        MethodCall.Method,
									        *MethodDecl,
									        MethodTypeModulePath,
									        OwnerSubst)),
									    MethodArgEmitCount);
									return true;
								}
								Emitter.EmitCallBoundMethod(FunctionIndex, MethodArgEmitCount);
								return true;
							}
							Emitter.EmitCallMethod(
							    AddString(MakeMethodCallLeafName(
							        MethodCall.Method,
							        *MethodDecl,
							        MethodTypeModulePath,
							        OwnerSubst)),
							    MethodArgEmitCount);
							return true;
						}
						const std::string MethodKey = MakeMethodFunctionName(
						    DeclOwnerLinkage,
						    MethodDecl->Name,
						    ParamTypes, MethodDecl->bIsConst);
						FunctionIndex = FindFunctionIndex(MethodKey);
					}
					if (FunctionIndex != static_cast<std::uint16_t>(-1))
					{
						Emitter.EmitCallBoundMethod(FunctionIndex, MethodArgEmitCount);
					}
					else
					{
						Emitter.EmitCallMethod(
						    AddString(MakeMethodCallLeafName(
						        MethodCall.Method,
						        *MethodDecl,
						        MethodTypeModulePath,
						        OwnerSubst)),
						    MethodArgEmitCount);
					}
				}
				else
				{
					if (MethodCall.Method == "entries" && bOwnerTypeKnown && (Runtime::IsMapTypeName(OwnerTypeName) || Runtime::IsOrderedMapTypeName(OwnerTypeName)))
					{
						std::string KeyType;
						std::string ValueType;
						const bool bParsed = Runtime::IsMapTypeName(OwnerTypeName)
						    ? ParseMapTypeForCompile(OwnerTypeName, KeyType, ValueType)
						    : ParseOrderedMapTypeForCompile(OwnerTypeName, KeyType, ValueType);
						if (bParsed)
						{
							std::string ResolvedLinkage;
							if (!EnsureStructTypeResolved(
							        Runtime::MakePairTypeName(KeyType, ValueType),
							        ResolvedLinkage,
							        nullptr,
							        Expression.Location))
							{
								return false;
							}
						}
					}
					Emitter.EmitCallMethod(
					    AddString(MethodCall.Method),
					    MethodArgEmitCount);
				}
				return true;
			}
