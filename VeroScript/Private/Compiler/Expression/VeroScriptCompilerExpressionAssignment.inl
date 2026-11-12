// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		case EExprKind::Ref:
			{
				// Standalone `ref` expressions are only valid as call arguments; callers that know
				// parameter constness invoke CompileRefArgument directly. Default to mutable so a
				// leaked `ref` still cannot silently bind a const storage location.
				const FRefExpr& RefExpr = static_cast<const FRefExpr&>(Expression);
				return CompileRefArgument(Emitter, *RefExpr.Operand, Expression.Location, true);
			}
		case EExprKind::Assign:
			{
				const FAssignExpr& Assign = static_cast<const FAssignExpr&>(Expression);
				if (!Assign.bIsCompound && CurrentFunction && CurrentFunction->bIsMethod && !CurrentFunction->bIsConstMethod && CurrentFunction->OwnerTypeIndex < Module->Types.size() && Assign.Target->Kind == EExprKind::Identifier && Assign.Value->Kind == EExprKind::Identifier)
				{
					const auto& TargetName = static_cast<const FIdentifierExpr&>(*Assign.Target).Name;
					const auto& SourceName = static_cast<const FIdentifierExpr&>(*Assign.Value).Name;
					std::uint16_t Local = 0, TargetField = 0, SourceField = 0;
					if (!ResolveLocal(TargetName, Local) && !ResolveLocal(SourceName, Local) && ResolveThisField(TargetName, TargetField) && ResolveThisField(SourceName, SourceField))
					{
						const auto& Fields = Module->Types[CurrentFunction->OwnerTypeIndex].Fields;
						const auto InactiveStorage = [&](const std::uint16_t Index)
						{
							return Index < Fields.size() && std::any_of(Fields[Index].DeclMeta.begin(), Fields[Index].DeclMeta.end(), [](const auto& Entry)
							{
								return Entry.first == UninitializedPayloadMetaKey;
							});
						};
						if (InactiveStorage(TargetField) && InactiveStorage(SourceField))
						{
							// Resetting compiler-owned optional storage must release its active
							// payload without treating the inactive sentinel as a T value.
							Emitter.Emit(EOpcode::LoadThis);
							Emitter.EmitU16Operand(EOpcode::LoadThisField, AddString(SourceName));
							Emitter.Emit(EOpcode::CloneStruct);
							Emitter.EmitU16Operand(EOpcode::StoreField, AddString(TargetName));
							std::string ResultType;
							if (TryResolveExprRawTypeName(Expression, ResultType) && Runtime::IsReferenceTypeName(ResultType))
							{
								Emitter.Emit(EOpcode::Pop);
								Emitter.EmitU16Operand(EOpcode::MakeThisFieldRef, AddString(TargetName));
							}
							return true;
						}
					}
				}
				if (TryCompileOverloadedAssignment(Emitter, Assign, Expression.Location)) return true;
				if (HasError()) return false;
				if (IsConstAssignTarget(*Assign.Target))
				{
					ReportError("cannot assign to const", Expression.Location);
					return false;
				}
				std::string TargetRawType;
				const bool bReferenceTarget = TryResolveExprRawTypeName(*Assign.Target, TargetRawType) && Runtime::IsReferenceTypeName(TargetRawType);
				bool bIndexPathTarget = false;
				if (Assign.Target->Kind == EExprKind::IndexAccess && CanEmitGetIndexRefChain(*Assign.Target, false))
				{
					const auto& IndexAccess = static_cast<const FIndexAccessExpr&>(*Assign.Target);
					std::string ContainerType;
					// Native arrays are values. Copy-and-replace writeback would end the
					// original storage lifetime even for an ordinary element assignment.
					bIndexPathTarget = IndexAccess.Object
					    && TryResolveExprTypeName(*IndexAccess.Object, ContainerType)
					    && (Runtime::IsNativeArrayTypeName(ContainerType) || Runtime::IsScriptArgsArrayTypeName(ContainerType));
					const FExpr* Root = Assign.Target.get();
					while (Root && Root->Kind == EExprKind::IndexAccess)
					{
						Root = static_cast<const FIndexAccessExpr&>(*Root).Object.get();
					}
					if (Root && Root->Kind == EExprKind::MemberAccess)
					{
						const auto& Member = static_cast<const FMemberAccessExpr&>(*Root);
						std::string GlobalName, FieldType, AccessOwner;
						bool bFieldConst = false, bTypeReceiver = false;
						bIndexPathTarget |= Member.Object && (TryResolveStaticField(*Member.Object, Member.Member, GlobalName, FieldType, bFieldConst, AccessOwner, bTypeReceiver) || TryResolveNamespaceQualifiedGlobal(*Member.Object, Member.Member, GlobalName));
					}
				}
				if (bReferenceTarget || bIndexPathTarget || Assign.Target->Kind == EExprKind::Call || Assign.Target->Kind == EExprKind::MethodCall)
				{
					std::string RefType;
					if (bIndexPathTarget && TryResolveExprTypeName(*Assign.Target, RefType))
					{
						RefType = "ref " + RefType;
					}
					else if (!TryResolveExprRawTypeName(*Assign.Target, RefType) || !Runtime::IsReferenceTypeName(RefType))
					{
						ReportError("call expression is not an assignable reference", Expression.Location);
						return false;
					}
					if (Runtime::IsConstReferenceTypeName(RefType))
					{
						ReportError("cannot assign through const reference return", Expression.Location);
						return false;
					}
					const std::string ValueType(Runtime::StripReferenceTypeName(RefType));
					const std::uint16_t RefSlot = AllocateAssignTemp();
					const bool bBound = bIndexPathTarget
					    ? EmitGetIndexRefChain(Emitter, *Assign.Target, Expression.Location, false)
					    : CompileRefArgument(Emitter, *Assign.Target, Expression.Location, true);
					if (!bBound)
					{
						return false;
					}
					Emitter.EmitU16Operand(EOpcode::BindLocal, RefSlot);
					std::uint16_t OldValueSlot = 0;
					if (Assign.bIsCompound)
					{
						Emitter.EmitU16Operand(EOpcode::LoadLocal, RefSlot);
						if (Assign.bFromPostfixIncDec)
						{
							OldValueSlot = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::StoreLocal, OldValueSlot);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, OldValueSlot);
						}
						std::string RightType;
						if (!TryResolveExprTypeName(*Assign.Value, RightType) || (!TryEmitIncDecUnitOne(Emitter, Assign, ValueType, RightType) && !CompileExpression(*Assign.Value)))
						{
							return false;
						}
						if (!EmitCompoundAssignOp(
						        Emitter,
						        Assign.CompoundOp,
						        ValueType,
						        RightType,
						        Expression.Location, nullptr, Assign.Value.get()))
						{
							return false;
						}
					}
					else
					{
						const std::string PreviousExpectedType = ExpressionExpectedType;
						ExpressionExpectedType = ValueType;
						const bool bCompiled = CompileExpression(*Assign.Value);
						const bool bCoerced = bCompiled && EmitValueCoercionForTarget(Emitter, ValueType, *Assign.Value, Expression.Location);
						ExpressionExpectedType = PreviousExpectedType;
						if (!bCoerced)
						{
							return false;
						}
						if (NeedsPassByValueCopy(ValueType) && !CanTransferCoercedValue(*Assign.Value, ValueType))
						{
							EmitValueCopy(Emitter, ValueType);
						}
					}
					Emitter.EmitU16Operand(EOpcode::StoreLocal, RefSlot);
					Emitter.EmitU16Operand(
					    EOpcode::LoadLocal,
					    Assign.bFromPostfixIncDec ? OldValueSlot : RefSlot);
					return true;
				}
				if (Assign.Target->Kind == EExprKind::Identifier)
				{
					const std::string& TargetName =
					    static_cast<const FIdentifierExpr&>(*Assign.Target).Name;
					std::string BoundTypeName;
					std::uint16_t ThisFieldIndex = 0;
					const bool bValueBinding = TryResolveVariableTypeName(TargetName, BoundTypeName) || ResolveThisField(TargetName, ThisFieldIndex);
					if (!ResolveLocalEnumConstantLinkageName(TargetName).empty() || (!bValueBinding && IsEnumConstantName(TargetName)))
					{
						ReportError("cannot modify enum constant: " + TargetName, Expression.Location);
						return false;
					}
					if (!bValueBinding && IsTypeNameWithoutValueBinding(TargetName))
					{
						ReportError("cannot assign to type name: " + TargetName, Expression.Location);
						return false;
					}
				}
				if (Assign.Target->Kind == EExprKind::MemberAccess)
				{
					const FMemberAccessExpr& MemberAccess =
					    static_cast<const FMemberAccessExpr&>(*Assign.Target);
					std::int64_t EnumValue = 0;
					std::string DisplayName;
					if (TryResolveEnumQualifiedConstant(
					        *MemberAccess.Object,
					        MemberAccess.Member,
					        EnumValue,
					        DisplayName))
					{
						ReportError("cannot modify enum constant: " + DisplayName, Expression.Location);
						return false;
					}
				}
				if (Assign.bIsCompound)
				{
					std::string TargetTypeName;
					std::string RightTypeName;
					if (!TryResolveExprTypeName(*Assign.Target, TargetTypeName))
					{
						ReportError("compound assignment target type could not be resolved", Expression.Location);
						return false;
					}
					if (!TryResolveExprTypeName(*Assign.Value, RightTypeName))
					{
						ReportError("compound assignment value type could not be resolved", Expression.Location);
						return false;
					}
					if (TargetTypeName == "void" || RightTypeName == "void")
					{
						ReportError("cannot use void value in expression", Expression.Location);
						return false;
					}
					if (Assign.Target->Kind == EExprKind::MemberAccess)
					{
						// C++-style static data member compound assign (`Type.Field op= v`,
						// `Type.Field++`, `instance.Field op= v`, …) → load/modify/store
						// the backing global.
						const FMemberAccessExpr& StaticMember =
						    static_cast<const FMemberAccessExpr&>(*Assign.Target);
						std::string StaticGlobalName;
						std::string StaticTypeName;
						bool bStaticConst = false;
						std::string StaticAccessOwner;
						bool bStaticTypeNameReceiver = false;
						if (TryResolveStaticField(
						        *StaticMember.Object,
						        StaticMember.Member,
						        StaticGlobalName,
						        StaticTypeName,
						        bStaticConst,
						        StaticAccessOwner,
						        bStaticTypeNameReceiver))
						{
							if (!ValidateMemberAccess(
							        Expression.Location,
							        StaticAccessOwner,
							        StaticMember.Member,
							        false))
							{
								return false;
							}
							EnsureStaticFieldGlobalRegistered(
							    StaticAccessOwner, StaticGlobalName, Expression.Location);
							std::uint16_t StaticSlot = 0;
							if (!ResolveGlobal(StaticGlobalName, StaticSlot))
							{
								ReportError(
								    "undeclared static member: " + StaticMember.Member,
								    Expression.Location);
								return false;
							}
							if (!bStaticTypeNameReceiver)
							{
								// `instance.staticField op= v`: evaluate the object for
								// side effects, then discard it before touching the global.
								if (!CompileExpression(*StaticMember.Object))
								{
									return false;
								}
								Emitter.Emit(EOpcode::Pop);
							}
							Emitter.EmitU16Operand(EOpcode::LoadGlobal, StaticSlot);
							std::uint16_t OldTemp = 0;
							if (Assign.bFromPostfixIncDec)
							{
								OldTemp = AllocateAssignTemp();
								Emitter.EmitU16Operand(EOpcode::StoreLocal, OldTemp);
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							if (!TryEmitIncDecUnitOne(Emitter, Assign, TargetTypeName, RightTypeName) && !CompileExpression(*Assign.Value))
							{
								return false;
							}
							std::string OverloadedResultType;
							if (!EmitCompoundAssignOp(
							        Emitter,
							        Assign.CompoundOp,
							        TargetTypeName,
							        RightTypeName,
							        Expression.Location,
							        &OverloadedResultType, Assign.Value.get()))
							{
								return false;
							}
							if (OverloadedResultType == "void")
							{
								Emitter.Emit(EOpcode::Pop);
								if (Assign.bFromPostfixIncDec)
								{
									Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
								}
								else
								{
									Emitter.EmitU16Operand(EOpcode::LoadGlobal, StaticSlot);
								}
								return true;
							}
							const std::string ResultTypeName = ResolveCompoundAssignResultType(
							    Assign.CompoundOp,
							    TargetTypeName,
							    RightTypeName,
							    OverloadedResultType);
							if (!EmitValueCoercionBetweenTypes(
							        Emitter,
							        TargetTypeName,
							        ResultTypeName,
							        Expression.Location))
							{
								return false;
							}
							if (NeedsPassByValueCopy(TargetTypeName))
							{
								EmitValueCopy(Emitter, TargetTypeName);
							}
							const std::uint16_t ValueTemp = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::StoreLocal, ValueTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
							Emitter.EmitU16Operand(EOpcode::StoreGlobal, StaticSlot);
							if (Assign.bFromPostfixIncDec)
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							else
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
							}
							return true;
						}
						// Namespace-qualified global compound assign (`Ns.x op= v`,
						// `Ns.x++`, …) → load/modify/store the backing global.
						std::string NsGlobalLinkageName;
						if (TryResolveNamespaceQualifiedGlobal(
						        *StaticMember.Object,
						        StaticMember.Member,
						        NsGlobalLinkageName))
						{
							if (GlobalVarConsts.find(NsGlobalLinkageName) != GlobalVarConsts.end())
							{
								ReportError("cannot assign to const", Expression.Location);
								return false;
							}
							std::uint16_t NsSlot = 0;
							if (!ResolveGlobal(NsGlobalLinkageName, NsSlot))
							{
								ReportError(
								    "undeclared identifier: " + StaticMember.Member,
								    Expression.Location);
								return false;
							}
							Emitter.EmitU16Operand(EOpcode::LoadGlobal, NsSlot);
							std::uint16_t OldTemp = 0;
							if (Assign.bFromPostfixIncDec)
							{
								OldTemp = AllocateAssignTemp();
								Emitter.EmitU16Operand(EOpcode::StoreLocal, OldTemp);
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							if (!TryEmitIncDecUnitOne(Emitter, Assign, TargetTypeName, RightTypeName) && !CompileExpression(*Assign.Value))
							{
								return false;
							}
							std::string OverloadedResultType;
							if (!EmitCompoundAssignOp(
							        Emitter,
							        Assign.CompoundOp,
							        TargetTypeName,
							        RightTypeName,
							        Expression.Location,
							        &OverloadedResultType, Assign.Value.get()))
							{
								return false;
							}
							if (OverloadedResultType == "void")
							{
								Emitter.Emit(EOpcode::Pop);
								if (Assign.bFromPostfixIncDec)
								{
									Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
								}
								else
								{
									Emitter.EmitU16Operand(EOpcode::LoadGlobal, NsSlot);
								}
								return true;
							}
							const std::string ResultTypeName = ResolveCompoundAssignResultType(
							    Assign.CompoundOp,
							    TargetTypeName,
							    RightTypeName,
							    OverloadedResultType);
							if (!EmitValueCoercionBetweenTypes(
							        Emitter,
							        TargetTypeName,
							        ResultTypeName,
							        Expression.Location))
							{
								return false;
							}
							if (NeedsPassByValueCopy(TargetTypeName))
							{
								EmitValueCopy(Emitter, TargetTypeName);
							}
							const std::uint16_t ValueTemp = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::StoreLocal, ValueTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
							Emitter.EmitU16Operand(EOpcode::StoreGlobal, NsSlot);
							if (Assign.bFromPostfixIncDec)
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							else
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
							}
							return true;
						}
					}
					if (Assign.bFromPrefixIncDec || Assign.bFromPostfixIncDec)
					{
						if (TryEmitOverloadedIncDec(
						        Emitter,
						        Assign,
						        TargetTypeName,
						        Expression.Location))
						{
							return true;
						}
						if (HasError())
						{
							return false;
						}
						const bool bNumericTarget = IsSignedIntegerTypeName(TargetTypeName) || Runtime::IsUnsignedTypeName(TargetTypeName) || TargetTypeName == "float" || TargetTypeName == "double";
						if (!bNumericTarget)
						{
							ReportError("increment/decrement requires numeric operand", Expression.Location);
							return false;
						}
					}

					// multicast_delegate += / -= → Add / Remove (UE-style sugar; mutates class handle).
					if (!Assign.bFromPrefixIncDec && !Assign.bFromPostfixIncDec && Runtime::IsMulticastDelegateTypeName(TargetTypeName) && (Assign.CompoundOp == EBinaryOp::Add || Assign.CompoundOp == EBinaryOp::Subtract))
					{
						if (!Runtime::IsFunctionTypeName(RightTypeName))
						{
							ReportError(
							    "multicast_delegate +=/-= expects a function value",
							    Expression.Location);
							return false;
						}
						const std::string MethodName =
						    Assign.CompoundOp == EBinaryOp::Add ? "add" : "remove";
						std::string DeclOwnerLinkage;
						const FMethodDecl* MethodDecl = nullptr;
						const std::vector<std::string> ArgTypeNames{RightTypeName};
						if (!ResolveMethodForCall(
						        TargetTypeName,
						        MethodName,
						        ArgTypeNames,
						        DeclOwnerLinkage,
						        MethodDecl,
						        Expression.Location) ||
						    !MethodDecl)
						{
							ReportError(
							    "multicast_delegate has no method: " + MethodName,
							    Expression.Location);
							return false;
						}
						std::unordered_map<std::string, std::string> OwnerSubst;
						if (!TryBuildGenericOwnerSubst(
						        TargetTypeName,
						        OwnerSubst,
						        Expression.Location))
						{
							return false;
						}
						const std::string MethodOwnerModulePath =
						    ResolveOwnerModulePath(TargetTypeName);
						const std::string MethodModulePath = MethodOwnerModulePath.empty()
						    ? CurrentModulePath
						    : MethodOwnerModulePath;
						const std::vector<std::string> ParamTypes =
						    CanonicalMethodParamTypeNames(
						        *MethodDecl,
						        MethodModulePath,
						        OwnerSubst);
						const std::string MethodKey = MakeMethodFunctionName(
						    DeclOwnerLinkage,
						    MethodName,
						    ParamTypes, MethodDecl->bIsConst);
						const std::uint16_t FunctionIndex = FindFunctionIndex(MethodKey);
						if (FunctionIndex == static_cast<std::uint16_t>(-1))
						{
							ReportError(
							    "multicast_delegate method not found: " + MethodName,
							    Expression.Location);
							return false;
						}
						if (!CompileExpression(*Assign.Target))
						{
							return false;
						}
						if (!CompileExpression(*Assign.Value))
						{
							return false;
						}
						if (!ParamTypes.empty() && !EmitValueCoercionForTarget(Emitter, ParamTypes.front(), *Assign.Value, Expression.Location))
						{
							return false;
						}
						Emitter.EmitCallBoundMethod(FunctionIndex, 1);
						return true;
					}

					if (Assign.Target->Kind == EExprKind::IndexAccess)
					{
						const FIndexAccessExpr& IndexAccess =
						    static_cast<const FIndexAccessExpr&>(*Assign.Target);
						std::string IndexType;
						if (TryResolveExprTypeName(*IndexAccess.Index, IndexType) && IndexType == "void")
						{
							ReportError("cannot use void value in expression", Expression.Location);
							return false;
						}
						{
							std::string ObjectTypeName;
							if (TryResolveExprTypeName(*IndexAccess.Object, ObjectTypeName) && TypeSupportsOperatorOverload(ObjectTypeName))
							{
								ReportError(
								    "cannot assign through index on type " + DisplayTypeName(ObjectTypeName) + " (use methods; operator[] is not an assignable lvalue)",
								    Expression.Location);
								return false;
							}
						}
						const auto EmitCompoundIndexTail =
						    [&](const std::function<void()>& EmitLoadArray,
						        const std::function<void()>& EmitStoreArray) -> bool
						{
							EmitLoadArray();
							if (!CompileExpression(*IndexAccess.Index))
							{
								return false;
							}
							const std::uint16_t IndexTemp = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::StoreLocal, IndexTemp);
							const std::uint16_t ArrayTemp = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::StoreLocal, ArrayTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ArrayTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, IndexTemp);
							Emitter.Emit(EOpcode::GetIndexCopy);
							std::uint16_t OldTemp = 0;
							if (Assign.bFromPostfixIncDec)
							{
								OldTemp = AllocateAssignTemp();
								Emitter.EmitU16Operand(EOpcode::StoreLocal, OldTemp);
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							if (!TryEmitIncDecUnitOne(Emitter, Assign, TargetTypeName, RightTypeName) && !CompileExpression(*Assign.Value))
							{
								return false;
							}
							std::string OverloadedResultType;
							if (!EmitCompoundAssignOp(
							        Emitter,
							        Assign.CompoundOp,
							        TargetTypeName,
							        RightTypeName,
							        Expression.Location,
							        &OverloadedResultType, Assign.Value.get()))
							{
								return false;
							}
							if (OverloadedResultType == "void")
							{
								// In-place compound: drop void sentinel; reload element value.
								Emitter.Emit(EOpcode::Pop);
								if (Assign.bFromPostfixIncDec)
								{
									Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
								}
								else
								{
									Emitter.EmitU16Operand(EOpcode::LoadLocal, ArrayTemp);
									Emitter.EmitU16Operand(EOpcode::LoadLocal, IndexTemp);
									Emitter.Emit(EOpcode::GetIndexCopy);
								}
								return true;
							}
							const std::string ResultTypeName = ResolveCompoundAssignResultType(
							    Assign.CompoundOp,
							    TargetTypeName,
							    RightTypeName,
							    OverloadedResultType);
							if (!EmitValueCoercionBetweenTypes(
							        Emitter,
							        TargetTypeName,
							        ResultTypeName,
							        Expression.Location))
							{
								return false;
							}
							const std::uint16_t ValueTemp = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::StoreLocal, ValueTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ArrayTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, IndexTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
							Emitter.Emit(EOpcode::SetIndex);
							EmitStoreArray();
							if (Assign.bFromPostfixIncDec)
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							else
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
							}
							return true;
						};

						if (IndexAccess.Object->Kind == EExprKind::Identifier)
						{
							const std::string& Name =
							    static_cast<const FIdentifierExpr&>(*IndexAccess.Object).Name;
							std::uint16_t ArraySlot = 0;
							std::uint16_t ThisFieldIndex = 0;
							enum class EArrayStorage : std::uint8_t
							{
								Local,
								ThisField,
								Global,
							} Storage{};
							if (ResolveLocal(Name, ArraySlot))
							{
								Storage = EArrayStorage::Local;
							}
							else if (ResolveThisField(Name, ThisFieldIndex))
							{
								if (!ValidateThisFieldAccess(Expression.Location, Name))
								{
									return false;
								}
								Storage = EArrayStorage::ThisField;
							}
							else
							{
								const std::string GlobalLinkageName =
								    ResolveGlobalLinkageName(Name, CurrentModulePath);
								if (GlobalLinkageName.empty() || !ResolveGlobal(GlobalLinkageName, ArraySlot))
								{
									ReportError("undeclared identifier: " + Name, Expression.Location);
									return false;
								}
								Storage = EArrayStorage::Global;
							}
							const auto EmitLoadArray = [&]()
							{
								if (Storage == EArrayStorage::Local)
								{
									Emitter.EmitU16Operand(EOpcode::LoadLocal, ArraySlot);
								}
								else if (Storage == EArrayStorage::ThisField)
								{
									Emitter.EmitU16Operand(EOpcode::LoadThisField, AddString(Name));
								}
								else
								{
									Emitter.EmitU16Operand(EOpcode::LoadGlobal, ArraySlot);
								}
							};
							const auto EmitStoreArray = [&]()
							{
								if (Storage == EArrayStorage::Local)
								{
									Emitter.EmitU16Operand(EOpcode::StoreLocal, ArraySlot);
								}
								else if (Storage == EArrayStorage::ThisField)
								{
									Emitter.EmitU16Operand(EOpcode::StoreThisField, AddString(Name));
								}
								else
								{
									Emitter.EmitU16Operand(EOpcode::StoreGlobal, ArraySlot);
								}
							};
							if (!EmitCompoundIndexTail(EmitLoadArray, EmitStoreArray))
							{
								return false;
							}
							return true;
						}
						if (IndexAccess.Object->Kind == EExprKind::MemberAccess)
						{
							const FMemberAccessExpr& MemberAccess =
							    static_cast<const FMemberAccessExpr&>(*IndexAccess.Object);
							// Entry-view p.second[i] op=: write through MapEntryRef.
							if (MemberAccess.Object && MemberAccess.Object->Kind == EExprKind::Identifier && MemberAccess.Member == "second")
							{
								const std::string& OwnerName =
								    static_cast<const FIdentifierExpr&>(*MemberAccess.Object).Name;
								FScope::FMapEntryViewBinding EntryView;
								if (TryGetMapEntryViewBinding(OwnerName, EntryView))
								{
									if (!CanEmitGetIndexRefChain(*Assign.Target, false))
									{
										ReportErrorId(
										    "diag.map_entry_view_nested_index_requires_array_or_deque",
										    Expression.Location);
										return false;
									}
									if (!EmitGetIndexRefChain(Emitter, *Assign.Target, Expression.Location, false))
									{
										return false;
									}
									const std::uint16_t RefSlot = AllocateAssignTemp();
									Emitter.EmitU16Operand(EOpcode::BindLocal, RefSlot);
									Emitter.EmitU16Operand(EOpcode::LoadLocal, RefSlot);
									std::uint16_t OldTemp = 0;
									if (Assign.bFromPostfixIncDec)
									{
										OldTemp = AllocateAssignTemp();
										Emitter.EmitU16Operand(EOpcode::StoreLocal, OldTemp);
										Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
									}
									if (!TryEmitIncDecUnitOne(Emitter, Assign, TargetTypeName, RightTypeName) && !CompileExpression(*Assign.Value))
									{
										return false;
									}
									std::string OverloadedResultType;
									if (!EmitCompoundAssignOp(
									        Emitter,
									        Assign.CompoundOp,
									        TargetTypeName,
									        RightTypeName,
									        Expression.Location,
									        &OverloadedResultType, Assign.Value.get()))
									{
										return false;
									}
									if (OverloadedResultType == "void")
									{
										Emitter.Emit(EOpcode::Pop);
										if (Assign.bFromPostfixIncDec)
										{
											Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
										}
										else
										{
											Emitter.EmitU16Operand(EOpcode::LoadLocal, RefSlot);
										}
										return true;
									}
									const std::string ResultTypeName = ResolveCompoundAssignResultType(
									    Assign.CompoundOp,
									    TargetTypeName,
									    RightTypeName,
									    OverloadedResultType);
									if (!EmitValueCoercionBetweenTypes(
									        Emitter,
									        TargetTypeName,
									        ResultTypeName,
									        Expression.Location))
									{
										return false;
									}
									Emitter.EmitU16Operand(EOpcode::StoreLocal, RefSlot);
									if (Assign.bFromPostfixIncDec)
									{
										Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
									}
									else
									{
										Emitter.EmitU16Operand(EOpcode::LoadLocal, RefSlot);
									}
									return true;
								}
							}
							std::string IndexFieldOwner;
							if (TryResolveExprTypeName(*MemberAccess.Object, IndexFieldOwner) && !ValidateMemberAccess(Expression.Location, IndexFieldOwner, MemberAccess.Member, false))
							{
								return false;
							}
							if (!CompileBorrowedReceiver(Emitter, *MemberAccess.Object))
							{
								return false;
							}
							Emitter.EmitU16Operand(EOpcode::LoadFieldKeep, AddString(MemberAccess.Member));
							if (!CompileExpression(*IndexAccess.Index))
							{
								return false;
							}
							const std::uint16_t IndexTemp = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::StoreLocal, IndexTemp);
							const std::uint16_t ArrayTemp = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::StoreLocal, ArrayTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ArrayTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, IndexTemp);
							Emitter.Emit(EOpcode::GetIndexCopy);
							std::uint16_t OldTemp = 0;
							if (Assign.bFromPostfixIncDec)
							{
								OldTemp = AllocateAssignTemp();
								Emitter.EmitU16Operand(EOpcode::StoreLocal, OldTemp);
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							if (!TryEmitIncDecUnitOne(Emitter, Assign, TargetTypeName, RightTypeName) && !CompileExpression(*Assign.Value))
							{
								return false;
							}
							std::string OverloadedResultType;
							if (!EmitCompoundAssignOp(
							        Emitter,
							        Assign.CompoundOp,
							        TargetTypeName,
							        RightTypeName,
							        Expression.Location,
							        &OverloadedResultType, Assign.Value.get()))
							{
								return false;
							}
							if (OverloadedResultType == "void")
							{
								Emitter.Emit(EOpcode::Pop);
								Emitter.Emit(EOpcode::Pop); // drop kept object under in-place update
								if (Assign.bFromPostfixIncDec)
								{
									Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
								}
								else
								{
									Emitter.EmitU16Operand(EOpcode::LoadLocal, ArrayTemp);
									Emitter.EmitU16Operand(EOpcode::LoadLocal, IndexTemp);
									Emitter.Emit(EOpcode::GetIndexCopy);
								}
								return true;
							}
							const std::string ResultTypeName = ResolveCompoundAssignResultType(
							    Assign.CompoundOp,
							    TargetTypeName,
							    RightTypeName,
							    OverloadedResultType);
							if (!EmitValueCoercionBetweenTypes(
							        Emitter,
							        TargetTypeName,
							        ResultTypeName,
							        Expression.Location))
							{
								return false;
							}
							const std::uint16_t ValueTemp = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::StoreLocal, ValueTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ArrayTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, IndexTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
							Emitter.Emit(EOpcode::SetIndex);
							Emitter.EmitU16Operand(EOpcode::StoreField, AddString(MemberAccess.Member));
							Emitter.Emit(EOpcode::Pop);
							if (Assign.bFromPostfixIncDec)
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							else
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
							}
							return true;
						}
						if (IndexAccess.Object->Kind == EExprKind::IndexAccess && CanEmitGetIndexRefChain(*Assign.Target, false))
						{
							if (!EmitGetIndexRefChain(Emitter, *Assign.Target, Expression.Location, false))
							{
								return false;
							}
							const std::uint16_t RefSlot = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::BindLocal, RefSlot);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, RefSlot);
							std::uint16_t OldTemp = 0;
							if (Assign.bFromPostfixIncDec)
							{
								OldTemp = AllocateAssignTemp();
								Emitter.EmitU16Operand(EOpcode::StoreLocal, OldTemp);
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							if (!TryEmitIncDecUnitOne(Emitter, Assign, TargetTypeName, RightTypeName) && !CompileExpression(*Assign.Value))
							{
								return false;
							}
							std::string OverloadedResultType;
							if (!EmitCompoundAssignOp(
							        Emitter,
							        Assign.CompoundOp,
							        TargetTypeName,
							        RightTypeName,
							        Expression.Location,
							        &OverloadedResultType, Assign.Value.get()))
							{
								return false;
							}
							if (OverloadedResultType == "void")
							{
								Emitter.Emit(EOpcode::Pop);
								if (Assign.bFromPostfixIncDec)
								{
									Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
								}
								else
								{
									Emitter.EmitU16Operand(EOpcode::LoadLocal, RefSlot);
								}
								return true;
							}
							const std::string ResultTypeName = ResolveCompoundAssignResultType(
							    Assign.CompoundOp,
							    TargetTypeName,
							    RightTypeName,
							    OverloadedResultType);
							if (!EmitValueCoercionBetweenTypes(
							        Emitter,
							        TargetTypeName,
							        ResultTypeName,
							        Expression.Location))
							{
								return false;
							}
							Emitter.EmitU16Operand(EOpcode::StoreLocal, RefSlot);
							if (Assign.bFromPostfixIncDec)
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							else
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, RefSlot);
							}
							return true;
						}
						if (IndexAccess.Object->Kind == EExprKind::IndexAccess)
						{
							// Shared map/array nesting: GetIndexCopy + SetIndex in place.
							// Native T[N] / string[] intermediates are value copies — refuse
							// rather than silently dropping the write (use a local IndexRef chain).
							{
								std::string ObjectTypeName;
								if (TryResolveExprTypeName(*IndexAccess.Object, ObjectTypeName) && (Runtime::IsNativeArrayTypeName(ObjectTypeName) || Runtime::IsScriptArgsArrayTypeName(ObjectTypeName)))
								{
									ReportErrorId(
									    "diag.cannot_assign_through_temporary_native_array_element",
									    Expression.Location);
									return false;
								}
							}
							if (!CompileExpression(*IndexAccess.Object))
							{
								return false;
							}
							if (!CompileExpression(*IndexAccess.Index))
							{
								return false;
							}
							const std::uint16_t IndexTemp = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::StoreLocal, IndexTemp);
							const std::uint16_t ArrayTemp = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::StoreLocal, ArrayTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ArrayTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, IndexTemp);
							Emitter.Emit(EOpcode::GetIndexCopy);
							std::uint16_t OldTemp = 0;
							if (Assign.bFromPostfixIncDec)
							{
								OldTemp = AllocateAssignTemp();
								Emitter.EmitU16Operand(EOpcode::StoreLocal, OldTemp);
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							if (!TryEmitIncDecUnitOne(Emitter, Assign, TargetTypeName, RightTypeName) && !CompileExpression(*Assign.Value))
							{
								return false;
							}
							std::string OverloadedResultType;
							if (!EmitCompoundAssignOp(
							        Emitter,
							        Assign.CompoundOp,
							        TargetTypeName,
							        RightTypeName,
							        Expression.Location,
							        &OverloadedResultType, Assign.Value.get()))
							{
								return false;
							}
							if (OverloadedResultType == "void")
							{
								Emitter.Emit(EOpcode::Pop);
								if (Assign.bFromPostfixIncDec)
								{
									Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
								}
								else
								{
									Emitter.EmitU16Operand(EOpcode::LoadLocal, ArrayTemp);
									Emitter.EmitU16Operand(EOpcode::LoadLocal, IndexTemp);
									Emitter.Emit(EOpcode::GetIndexCopy);
								}
								return true;
							}
							const std::string ResultTypeName = ResolveCompoundAssignResultType(
							    Assign.CompoundOp,
							    TargetTypeName,
							    RightTypeName,
							    OverloadedResultType);
							if (!EmitValueCoercionBetweenTypes(
							        Emitter,
							        TargetTypeName,
							        ResultTypeName,
							        Expression.Location))
							{
								return false;
							}
							const std::uint16_t ValueTemp = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::StoreLocal, ValueTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ArrayTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, IndexTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
							Emitter.Emit(EOpcode::SetIndex);
							Emitter.Emit(EOpcode::Pop);
							if (Assign.bFromPostfixIncDec)
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							else
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
							}
							return true;
						}
						ReportError("invalid indexed compound assignment target", Expression.Location);
						return false;
					}

					if (Assign.Target->Kind == EExprKind::MemberAccess)
					{
						const FMemberAccessExpr& MemberAccess =
						    static_cast<const FMemberAccessExpr&>(*Assign.Target);
						if (MemberAccess.Object && IsSuperReceiver(*MemberAccess.Object))
						{
							if (!CurrentFunction || !CurrentFunction->bIsMethod)
							{
								ReportError(
								    "'Super' is only valid inside instance methods",
								    Expression.Location);
								return false;
							}
							if (const std::string HostBase = CurrentMethodHostBaseTypeName();
							    !HostBase.empty())
							{
								std::uint16_t SuperGetter = static_cast<std::uint16_t>(-1);
								if (FindNative(
								        MakeHostPropertyGetterNativeName(
								            HostBase, MemberAccess.Member),
								        SuperGetter))
								{
									std::uint16_t SuperSetter = static_cast<std::uint16_t>(-1);
									if (!FindNative(
									        MakeHostPropertySetterNativeName(
									            HostBase, MemberAccess.Member),
									        SuperSetter))
									{
										ReportErrorId(
										    "diag.cannot_assign_host_property",
										    Expression.Location,
										    {MemberAccess.Member, DisplayTypeName(HostBase)});
										return false;
									}
									return EmitImplicitReceiverCompoundAssign(
									    Emitter,
									    Assign,
									    SuperGetter,
									    SuperSetter,
									    {},
									    TargetTypeName,
									    RightTypeName,
									    Expression.Location);
								}
							}
							// Script base fields are flattened into the derived layout.
							std::uint16_t BaseFieldIndex = 0;
							if (!ResolveThisField(MemberAccess.Member, BaseFieldIndex))
							{
								ReportError(
								    "unknown base member: " + MemberAccess.Member,
								    Expression.Location);
								return false;
							}
							if (!ValidateThisFieldAccess(Expression.Location, MemberAccess.Member))
							{
								return false;
							}
							return EmitImplicitReceiverCompoundAssign(
							    Emitter,
							    Assign,
							    static_cast<std::uint16_t>(-1),
							    static_cast<std::uint16_t>(-1),
							    MemberAccess.Member,
							    TargetTypeName,
							    RightTypeName,
							    Expression.Location);
						}
						FScope::FMapEntryViewBinding EntryView;
						std::vector<std::string> EntryMembers;
						if (TryGetMapEntryViewMemberPath(*Assign.Target, EntryView, EntryMembers))
						{
							if (EntryMembers.empty() || EntryMembers[0] == "first")
							{
								ReportErrorId(
								    "diag.cannot_assign_map_entry_view_key",
								    Expression.Location);
								return false;
							}
							if (EntryMembers[0] != "second")
							{
								ReportErrorId(
								    "diag.unknown_field_on_type",
								    Expression.Location,
								    {EntryMembers[0], "pair"});
								return false;
							}
							std::uint16_t WriteSlot = EntryView.RefSlot;
							if (EntryMembers.size() > 1)
							{
								std::vector<std::string> NestedFields(
								    EntryMembers.begin() + 1,
								    EntryMembers.end());
								if (!EmitMapEntryViewValueFieldRef(Emitter, EntryView, NestedFields))
								{
									return false;
								}
								WriteSlot = AllocateAssignTemp();
								Emitter.EmitU16Operand(EOpcode::BindLocal, WriteSlot);
							}
							Emitter.EmitU16Operand(EOpcode::LoadLocal, WriteSlot);
							std::uint16_t OldTemp = 0;
							if (Assign.bFromPostfixIncDec)
							{
								OldTemp = AllocateAssignTemp();
								Emitter.EmitU16Operand(EOpcode::StoreLocal, OldTemp);
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							if (!TryEmitIncDecUnitOne(Emitter, Assign, TargetTypeName, RightTypeName) && !CompileExpression(*Assign.Value))
							{
								return false;
							}
							std::string OverloadedResultType;
							if (!EmitCompoundAssignOp(
							        Emitter,
							        Assign.CompoundOp,
							        TargetTypeName,
							        RightTypeName,
							        Expression.Location,
							        &OverloadedResultType, Assign.Value.get()))
							{
								return false;
							}
							if (OverloadedResultType == "void")
							{
								Emitter.Emit(EOpcode::Pop);
								if (Assign.bFromPostfixIncDec)
								{
									Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
								}
								else
								{
									Emitter.EmitU16Operand(EOpcode::LoadLocal, WriteSlot);
								}
								return true;
							}
							const std::string ResultTypeName = ResolveCompoundAssignResultType(
							    Assign.CompoundOp,
							    TargetTypeName,
							    RightTypeName,
							    OverloadedResultType);
							if (!EmitValueCoercionBetweenTypes(
							        Emitter,
							        TargetTypeName,
							        ResultTypeName,
							        Expression.Location))
							{
								return false;
							}
							if (NeedsPassByValueCopy(TargetTypeName))
							{
								EmitValueCopy(Emitter, TargetTypeName);
							}
							Emitter.EmitU16Operand(EOpcode::StoreLocal, WriteSlot);
							if (Assign.bFromPostfixIncDec)
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							else
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, WriteSlot);
							}
							return true;
						}
						std::string FieldOwnerType;
						if (TryResolveExprTypeName(*MemberAccess.Object, FieldOwnerType) && !ValidateMemberAccess(Expression.Location, FieldOwnerType, MemberAccess.Member, false))
						{
							return false;
						}
						std::uint16_t HostGetterIndex = static_cast<std::uint16_t>(-1);
						std::uint16_t HostSetterIndex = static_cast<std::uint16_t>(-1);
						const std::string HostPropOwner = FieldOwnerType.empty()
						    ? std::string{}
						    : ResolveHostDispatchTypeName(FieldOwnerType);
						const std::string HostPropType =
						    HostPropOwner.empty() ? FieldOwnerType : HostPropOwner;
						const bool bHostProperty = !HostPropType.empty() && FindNative(MakeHostPropertyGetterNativeName(HostPropType, MemberAccess.Member), HostGetterIndex) && FindNative(MakeHostPropertySetterNativeName(HostPropType, MemberAccess.Member), HostSetterIndex);
						if (bHostProperty)
						{
							if (!CompileBorrowedReceiver(Emitter, *MemberAccess.Object))
							{
								return false;
							}
							const std::uint16_t ObjTemp = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::StoreLocal, ObjTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ObjTemp);
							Emitter.EmitCallNative(HostGetterIndex, 1);
							std::uint16_t OldTemp = 0;
							if (Assign.bFromPostfixIncDec)
							{
								OldTemp = AllocateAssignTemp();
								Emitter.EmitU16Operand(EOpcode::StoreLocal, OldTemp);
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							if (!TryEmitIncDecUnitOne(Emitter, Assign, TargetTypeName, RightTypeName) && !CompileExpression(*Assign.Value))
							{
								return false;
							}
							std::string OverloadedResultType;
							if (!EmitCompoundAssignOp(
							        Emitter,
							        Assign.CompoundOp,
							        TargetTypeName,
							        RightTypeName,
							        Expression.Location,
							        &OverloadedResultType, Assign.Value.get()))
							{
								return false;
							}
							if (OverloadedResultType == "void")
							{
								Emitter.Emit(EOpcode::Pop);
								if (Assign.bFromPostfixIncDec)
								{
									Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
								}
								else
								{
									Emitter.EmitU16Operand(EOpcode::LoadLocal, ObjTemp);
									Emitter.EmitCallNative(HostGetterIndex, 1);
								}
								return true;
							}
							const std::string ResultTypeName = ResolveCompoundAssignResultType(
							    Assign.CompoundOp,
							    TargetTypeName,
							    RightTypeName,
							    OverloadedResultType);
							if (!EmitValueCoercionBetweenTypes(
							        Emitter,
							        TargetTypeName,
							        ResultTypeName,
							        Expression.Location))
							{
								return false;
							}
							if (NeedsPassByValueCopy(TargetTypeName))
							{
								EmitValueCopy(Emitter, TargetTypeName);
							}
							const std::uint16_t ValueTemp = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::StoreLocal, ValueTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ObjTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
							Emitter.EmitCallNative(HostSetterIndex, 2);
							if (Assign.bFromPostfixIncDec)
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							else
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
							}
							return true;
						}
						if (!HostPropType.empty())
						{
							std::uint16_t HostGetterOnly = static_cast<std::uint16_t>(-1);
							if (FindNative(
							        MakeHostPropertyGetterNativeName(
							            HostPropType, MemberAccess.Member),
							        HostGetterOnly))
							{
								ReportErrorId(
								    "diag.cannot_assign_host_property",
								    Expression.Location,
								    {MemberAccess.Member, DisplayTypeName(HostPropType)});
								return false;
							}
						}
						if (!CompileBorrowedReceiver(Emitter, *MemberAccess.Object))
						{
							return false;
						}
						Emitter.EmitU16Operand(EOpcode::LoadFieldKeep, AddString(MemberAccess.Member));
						std::uint16_t OldTemp = 0;
						if (Assign.bFromPostfixIncDec)
						{
							OldTemp = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::StoreLocal, OldTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
						}
						if (!TryEmitIncDecUnitOne(Emitter, Assign, TargetTypeName, RightTypeName) && !CompileExpression(*Assign.Value))
						{
							return false;
						}
						std::string OverloadedResultType;
						if (!EmitCompoundAssignOp(
						        Emitter,
						        Assign.CompoundOp,
						        TargetTypeName,
						        RightTypeName,
						        Expression.Location,
						        &OverloadedResultType, Assign.Value.get()))
						{
							return false;
						}
						if (OverloadedResultType == "void")
						{
							// [object, void_sentinel] → Pop → [object]; LoadField for value (no StoreField).
							Emitter.Emit(EOpcode::Pop);
							if (Assign.bFromPostfixIncDec)
							{
								Emitter.Emit(EOpcode::Pop); // drop object
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							else
							{
								Emitter.EmitU16Operand(EOpcode::LoadField, AddString(MemberAccess.Member));
							}
							return true;
						}
						const std::string ResultTypeName = ResolveCompoundAssignResultType(
						    Assign.CompoundOp,
						    TargetTypeName,
						    RightTypeName,
						    OverloadedResultType);
						if (!EmitValueCoercionBetweenTypes(
						        Emitter,
						        TargetTypeName,
						        ResultTypeName,
						        Expression.Location))
						{
							return false;
						}
						if (NeedsPassByValueCopy(TargetTypeName))
						{
							EmitValueCopy(Emitter, TargetTypeName);
						}
						const std::uint16_t ValueTemp = AllocateAssignTemp();
						Emitter.EmitU16Operand(EOpcode::StoreLocal, ValueTemp);
						Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
						Emitter.EmitU16Operand(EOpcode::StoreField, AddString(MemberAccess.Member));
						Emitter.Emit(EOpcode::Pop);
						if (Assign.bFromPostfixIncDec)
						{
							Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
						}
						else
						{
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
						}
						return true;
					}

					if (Assign.Target->Kind == EExprKind::Identifier)
					{
						const std::string& WholeName =
						    static_cast<const FIdentifierExpr&>(*Assign.Target).Name;
						FScope::FMapEntryViewBinding WholeEntryView;
						if (TryGetMapEntryViewBinding(WholeName, WholeEntryView))
						{
							ReportErrorId(
							    "diag.cannot_assign_map_entry_view",
							    Expression.Location);
							return false;
						}
						// Bare host-base property (script bindings already won above).
						std::uint16_t UnusedSlot = 0;
						std::uint16_t UnusedFieldIndex = 0;
						if (!ResolveLocal(WholeName, UnusedSlot) && !ResolveThisField(WholeName, UnusedFieldIndex) && ResolveGlobalLinkageName(WholeName, CurrentModulePath).empty())
						{
							if (const std::string HostBase = CurrentMethodHostBaseTypeName();
							    !HostBase.empty())
							{
								std::uint16_t ImplicitGetter = static_cast<std::uint16_t>(-1);
								if (FindNative(
								        MakeHostPropertyGetterNativeName(HostBase, WholeName),
								        ImplicitGetter))
								{
									std::uint16_t ImplicitSetter = static_cast<std::uint16_t>(-1);
									if (!FindNative(
									        MakeHostPropertySetterNativeName(HostBase, WholeName),
									        ImplicitSetter))
									{
										ReportErrorId(
										    "diag.cannot_assign_host_property",
										    Expression.Location,
										    {WholeName, DisplayTypeName(HostBase)});
										return false;
									}
									return EmitImplicitReceiverCompoundAssign(
									    Emitter,
									    Assign,
									    ImplicitGetter,
									    ImplicitSetter,
									    {},
									    TargetTypeName,
									    RightTypeName,
									    Expression.Location);
								}
							}
						}
					}

					if (!CompileExpression(*Assign.Target))
					{
						return false;
					}
					std::uint16_t OldTemp = 0;
					if (Assign.bFromPostfixIncDec)
					{
						OldTemp = AllocateAssignTemp();
						Emitter.EmitU16Operand(EOpcode::StoreLocal, OldTemp);
						Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
					}
					if (!TryEmitIncDecUnitOne(Emitter, Assign, TargetTypeName, RightTypeName) && !CompileExpression(*Assign.Value))
					{
						return false;
					}
					std::string OverloadedResultType;
					if (!EmitCompoundAssignOp(
					        Emitter,
					        Assign.CompoundOp,
					        TargetTypeName,
					        RightTypeName,
					        Expression.Location,
					        &OverloadedResultType, Assign.Value.get()))
					{
						return false;
					}
					if (Assign.Target->Kind == EExprKind::Identifier)
					{
						const std::string& Name =
						    static_cast<const FIdentifierExpr&>(*Assign.Target).Name;
						FScope::FMapEntryViewBinding EntryView;
						if (TryGetMapEntryViewBinding(Name, EntryView))
						{
							ReportErrorId(
							    "diag.cannot_assign_map_entry_view",
							    Expression.Location);
							return false;
						}
						std::uint16_t Slot = 0;
						const bool bLocal = ResolveLocal(Name, Slot);
						std::uint16_t FieldIndex = 0;
						const bool bThisField = !bLocal && ResolveThisField(Name, FieldIndex);
						std::uint16_t GlobalSlot = 0;
						bool bGlobal = false;
						if (!bLocal && !bThisField)
						{
							const std::string GlobalLinkageName =
							    ResolveGlobalLinkageName(Name, CurrentModulePath);
							if (GlobalLinkageName.empty() || !ResolveGlobal(GlobalLinkageName, GlobalSlot))
							{
								ReportError("undeclared identifier: " + Name, Expression.Location);
								return false;
							}
							bGlobal = true;
						}
						if (bThisField && !ValidateThisFieldAccess(Expression.Location, Name))
						{
							return false;
						}
						if (OverloadedResultType == "void")
						{
							Emitter.Emit(EOpcode::Pop);
							if (Assign.bFromPostfixIncDec)
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							else if (bLocal)
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, Slot);
							}
							else if (bThisField)
							{
								Emitter.EmitU16Operand(EOpcode::LoadThisField, AddString(Name));
							}
							else
							{
								(void)bGlobal;
								Emitter.EmitU16Operand(EOpcode::LoadGlobal, GlobalSlot);
							}
							return true;
						}
						{
							const std::string ResultTypeName = ResolveCompoundAssignResultType(
							    Assign.CompoundOp,
							    TargetTypeName,
							    RightTypeName,
							    OverloadedResultType);
							if (!EmitValueCoercionBetweenTypes(
							        Emitter,
							        TargetTypeName,
							        ResultTypeName,
							        Expression.Location))
							{
								return false;
							}
						}
						if (NeedsPassByValueCopy(TargetTypeName))
						{
							EmitValueCopy(Emitter, TargetTypeName);
						}
						if (bLocal)
						{
							Emitter.EmitU16Operand(EOpcode::StoreLocal, Slot);
							if (Assign.bFromPostfixIncDec)
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							else
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, Slot);
							}
							return true;
						}
						if (bThisField)
						{
							Emitter.EmitU16Operand(EOpcode::StoreThisField, AddString(Name));
							if (Assign.bFromPostfixIncDec)
							{
								Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
							}
							else
							{
								Emitter.EmitU16Operand(EOpcode::LoadThisField, AddString(Name));
							}
							return true;
						}
						Emitter.EmitU16Operand(EOpcode::StoreGlobal, GlobalSlot);
						if (Assign.bFromPostfixIncDec)
						{
							Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
						}
						else
						{
							Emitter.EmitU16Operand(EOpcode::LoadGlobal, GlobalSlot);
						}
						return true;
					}
					ReportError("invalid compound assignment target", Expression.Location);
					return false;
				}
				if (Assign.Target->Kind == EExprKind::Identifier || Assign.Target->Kind == EExprKind::MemberAccess)
				{
					std::string TargetTypeName;
					if (TryResolveExprTypeName(*Assign.Target, TargetTypeName) && !ValidateDynamicArrayAssignment(TargetTypeName, *Assign.Value, Expression.Location))
					{
						return false;
					}
				}
				if (Assign.Target->Kind == EExprKind::IndexAccess)
				{
					const FIndexAccessExpr& IndexAccess = static_cast<const FIndexAccessExpr&>(*Assign.Target);
					std::string IndexType;
					if (TryResolveExprTypeName(*IndexAccess.Index, IndexType) && IndexType == "void")
					{
						ReportError("cannot use void value in expression", Expression.Location);
						return false;
					}
					{
						std::string ObjectTypeName;
						if (TryResolveExprTypeName(*IndexAccess.Object, ObjectTypeName) && TypeSupportsOperatorOverload(ObjectTypeName))
						{
							ReportError(
							    "cannot assign through index on type " + DisplayTypeName(ObjectTypeName) + " (use methods; operator[] is not an assignable lvalue)",
							    Expression.Location);
							return false;
						}
					}
					if (IndexAccess.Object->Kind == EExprKind::Identifier)
					{
						const std::string& Name = static_cast<const FIdentifierExpr&>(*IndexAccess.Object).Name;
						std::uint16_t Slot = 0;
						if (ResolveLocal(Name, Slot))
						{
							Emitter.EmitU16Operand(EOpcode::LoadLocal, Slot);
						}
						else
						{
							std::uint16_t FieldIndex = 0;
							if (ResolveThisField(Name, FieldIndex))
							{
								if (!ValidateThisFieldAccess(Expression.Location, Name))
								{
									return false;
								}
								Emitter.EmitU16Operand(EOpcode::LoadThisField, AddString(Name));
								if (!CompileExpression(*IndexAccess.Index) || !CompileExpression(*Assign.Value))
								{
									return false;
								}
								{
									std::string ElementTypeName;
									if (TryResolveExprTypeName(*Assign.Target, ElementTypeName) && !EmitValueCoercionForTarget(Emitter, ElementTypeName, *Assign.Value, Expression.Location))
									{
										return false;
									}
								}
								const std::uint16_t TempSlot = AllocateAssignTemp();
								Emitter.EmitU16Operand(EOpcode::StoreLocal, TempSlot);
								Emitter.EmitU16Operand(EOpcode::LoadLocal, TempSlot);
								Emitter.Emit(EOpcode::SetIndex);
								Emitter.EmitU16Operand(EOpcode::StoreThisField, AddString(Name));
								Emitter.EmitU16Operand(EOpcode::LoadLocal, TempSlot);
								return true;
							}
							const std::string GlobalLinkageName =
							    ResolveGlobalLinkageName(Name, CurrentModulePath);
							if (GlobalLinkageName.empty())
							{
								ReportError("undeclared identifier: " + Name, Expression.Location);
								return false;
							}
							if (!ResolveGlobal(GlobalLinkageName, Slot))
							{
								ReportError("undeclared identifier: " + Name, Expression.Location);
								return false;
							}
							Emitter.EmitU16Operand(EOpcode::LoadGlobal, Slot);
						}
						if (!CompileExpression(*IndexAccess.Index) || !CompileExpression(*Assign.Value))
						{
							return false;
						}
						{
							std::string ElementTypeName;
							if (TryResolveExprTypeName(*Assign.Target, ElementTypeName) && !EmitValueCoercionForTarget(Emitter, ElementTypeName, *Assign.Value, Expression.Location))
							{
								return false;
							}
						}
						const std::uint16_t TempSlot = AllocateAssignTemp();
						Emitter.EmitU16Operand(EOpcode::StoreLocal, TempSlot);
						Emitter.EmitU16Operand(EOpcode::LoadLocal, TempSlot);
						Emitter.Emit(EOpcode::SetIndex);
						if (ResolveLocal(Name, Slot))
						{
							Emitter.EmitU16Operand(EOpcode::StoreLocal, Slot);
						}
						else
						{
							Emitter.EmitU16Operand(EOpcode::StoreGlobal, Slot);
						}
						Emitter.EmitU16Operand(EOpcode::LoadLocal, TempSlot);
						return true;
					}
					if (IndexAccess.Object->Kind == EExprKind::MemberAccess)
					{
						const FMemberAccessExpr& MemberAccess =
						    static_cast<const FMemberAccessExpr&>(*IndexAccess.Object);
						// Entry-view p.second[i]: write through MapEntryRef, not a materialized pair.
						if (MemberAccess.Object && MemberAccess.Object->Kind == EExprKind::Identifier && MemberAccess.Member == "second")
						{
							const std::string& OwnerName =
							    static_cast<const FIdentifierExpr&>(*MemberAccess.Object).Name;
							FScope::FMapEntryViewBinding EntryView;
							if (TryGetMapEntryViewBinding(OwnerName, EntryView))
							{
								if (!CanEmitGetIndexRefChain(*Assign.Target, false))
								{
									ReportErrorId(
									    "diag.map_entry_view_nested_index_requires_array_or_deque",
									    Expression.Location);
									return false;
								}
								if (!EmitGetIndexRefChain(Emitter, *Assign.Target, Expression.Location, false))
								{
									return false;
								}
								const std::uint16_t RefSlot = AllocateAssignTemp();
								Emitter.EmitU16Operand(EOpcode::BindLocal, RefSlot);
								{
									std::string ElementTypeName;
									const bool bHasElementType =
									    TryResolveExprTypeName(*Assign.Target, ElementTypeName);
									const std::string PreviousExpectedType = ExpressionExpectedType;
									if (bHasElementType)
									{
										ExpressionExpectedType = ElementTypeName;
									}
									const bool bCompiled = CompileExpression(*Assign.Value);
									const bool bCoerced = !bHasElementType || (bCompiled && EmitValueCoercionForTarget(Emitter, ElementTypeName, *Assign.Value, Expression.Location));
									ExpressionExpectedType = PreviousExpectedType;
									if (!bCompiled || !bCoerced)
									{
										return false;
									}
									if (!bHasElementType || NeedsPassByValueCopy(ElementTypeName))
									{
										EmitValueCopy(Emitter, ElementTypeName);
									}
								}
								Emitter.EmitU16Operand(EOpcode::StoreLocal, RefSlot);
								Emitter.EmitU16Operand(EOpcode::LoadLocal, RefSlot);
								return true;
							}
						}
						std::string AssignIndexFieldOwner;
						if (TryResolveExprTypeName(*MemberAccess.Object, AssignIndexFieldOwner) && !ValidateMemberAccess(Expression.Location, AssignIndexFieldOwner, MemberAccess.Member, false))
						{
							return false;
						}
						if (!CompileBorrowedReceiver(Emitter, *MemberAccess.Object))
						{
							return false;
						}
						Emitter.EmitU16Operand(EOpcode::LoadFieldKeep, AddString(MemberAccess.Member));
						if (!CompileExpression(*IndexAccess.Index) || !CompileExpression(*Assign.Value))
						{
							return false;
						}
						{
							std::string ElementTypeName;
							if (TryResolveExprTypeName(*Assign.Target, ElementTypeName) && !EmitValueCoercionForTarget(Emitter, ElementTypeName, *Assign.Value, Expression.Location))
							{
								return false;
							}
						}
						const std::uint16_t TempSlot = AllocateAssignTemp();
						Emitter.EmitU16Operand(EOpcode::StoreLocal, TempSlot);
						Emitter.EmitU16Operand(EOpcode::LoadLocal, TempSlot);
						Emitter.Emit(EOpcode::SetIndex);
						Emitter.EmitU16Operand(EOpcode::StoreField, AddString(MemberAccess.Member));
						Emitter.Emit(EOpcode::Pop);
						Emitter.EmitU16Operand(EOpcode::LoadLocal, TempSlot);
						return true;
					}
					if (IndexAccess.Object->Kind == EExprKind::IndexAccess)
					{
						// Prefer GetIndexRef write-through for local array/deque chains (native
						// T[N] rows are value copies under GetIndexCopy). Map/ordered_map nesting
						// stays on SetIndex so missing keys can still insert.
						if (CanEmitGetIndexRefChain(*Assign.Target, false))
						{
							if (!EmitGetIndexRefChain(Emitter, *Assign.Target, Expression.Location, false))
							{
								return false;
							}
							const std::uint16_t RefSlot = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::BindLocal, RefSlot);
							{
								std::string ElementTypeName;
								const bool bHasElementType =
								    TryResolveExprTypeName(*Assign.Target, ElementTypeName);
								const std::string PreviousExpectedType = ExpressionExpectedType;
								if (bHasElementType)
								{
									ExpressionExpectedType = ElementTypeName;
								}
								const bool bCompiled = CompileExpression(*Assign.Value);
								const bool bCoerced = !bHasElementType || (bCompiled && EmitValueCoercionForTarget(Emitter, ElementTypeName, *Assign.Value, Expression.Location));
								ExpressionExpectedType = PreviousExpectedType;
								if (!bCompiled || !bCoerced)
								{
									return false;
								}
								if (!bHasElementType || NeedsPassByValueCopy(ElementTypeName))
								{
									EmitValueCopy(Emitter, ElementTypeName);
								}
							}
							Emitter.EmitU16Operand(EOpcode::StoreLocal, RefSlot);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, RefSlot);
							return true;
						}
						{
							std::string ObjectTypeName;
							if (TryResolveExprTypeName(*IndexAccess.Object, ObjectTypeName) && (Runtime::IsNativeArrayTypeName(ObjectTypeName) || Runtime::IsScriptArgsArrayTypeName(ObjectTypeName)))
							{
								ReportErrorId(
								    "diag.cannot_assign_through_temporary_native_array_element",
								    Expression.Location);
								return false;
							}
						}
						if (!CompileExpression(*IndexAccess.Object) || !CompileExpression(*IndexAccess.Index) || !CompileExpression(*Assign.Value))
						{
							return false;
						}
						{
							std::string ElementTypeName;
							if (TryResolveExprTypeName(*Assign.Target, ElementTypeName) && !EmitValueCoercionForTarget(Emitter, ElementTypeName, *Assign.Value, Expression.Location))
							{
								return false;
							}
						}
						const std::uint16_t TempSlot = AllocateAssignTemp();
						Emitter.EmitU16Operand(EOpcode::StoreLocal, TempSlot);
						Emitter.EmitU16Operand(EOpcode::LoadLocal, TempSlot);
						Emitter.Emit(EOpcode::SetIndex);
						Emitter.Emit(EOpcode::Pop);
						Emitter.EmitU16Operand(EOpcode::LoadLocal, TempSlot);
						return true;
					}
					ReportError("invalid indexed assignment target", Expression.Location);
					return false;
				}
				if (Assign.Target->Kind == EExprKind::MemberAccess)
				{
					const FMemberAccessExpr& MemberAccess = static_cast<const FMemberAccessExpr&>(*Assign.Target);
					if (MemberAccess.Object && IsSuperReceiver(*MemberAccess.Object))
					{
						if (!CurrentFunction || !CurrentFunction->bIsMethod)
						{
							ReportError(
							    "'Super' is only valid inside instance methods",
							    Expression.Location);
							return false;
						}
						std::string FieldTypeName;
						const bool bHasFieldType =
						    TryResolveExprTypeName(*Assign.Target, FieldTypeName);
						const std::string PreviousExpectedType = ExpressionExpectedType;
						if (bHasFieldType)
						{
							ExpressionExpectedType = FieldTypeName;
						}
						const bool bCompiled = CompileExpression(*Assign.Value);
						const bool bCoerced = !bHasFieldType || (bCompiled && EmitValueCoercionForTarget(Emitter, FieldTypeName, *Assign.Value, Expression.Location));
						ExpressionExpectedType = PreviousExpectedType;
						if (!bCompiled || !bCoerced)
						{
							return false;
						}
						if (!bHasFieldType || (NeedsPassByValueCopy(FieldTypeName) && !CanTransferCoercedValue(*Assign.Value, FieldTypeName)))
						{
							EmitValueCopy(Emitter, FieldTypeName);
						}
						if (const std::string HostBase = CurrentMethodHostBaseTypeName();
						    !HostBase.empty())
						{
							std::uint16_t SetterIndex = static_cast<std::uint16_t>(-1);
							if (FindNative(
							        MakeHostPropertySetterNativeName(
							            HostBase, MemberAccess.Member),
							        SetterIndex))
							{
								const std::uint16_t ValueTemp = AllocateAssignTemp();
								Emitter.EmitU16Operand(EOpcode::StoreLocal, ValueTemp);
								Emitter.Emit(EOpcode::LoadThis);
								Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
								Emitter.EmitCallNative(SetterIndex, 2);
								Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
								return true;
							}
							std::uint16_t GetterIndex = static_cast<std::uint16_t>(-1);
							if (FindNative(
							        MakeHostPropertyGetterNativeName(
							            HostBase, MemberAccess.Member),
							        GetterIndex))
							{
								ReportErrorId(
								    "diag.cannot_assign_host_property",
								    Expression.Location,
								    {MemberAccess.Member, DisplayTypeName(HostBase)});
								return false;
							}
						}
						std::uint16_t BaseFieldIndex = 0;
						if (ResolveThisField(MemberAccess.Member, BaseFieldIndex))
						{
							if (!ValidateThisFieldAccess(Expression.Location, MemberAccess.Member))
							{
								return false;
							}
							Emitter.EmitU16Operand(
							    EOpcode::StoreThisField,
							    AddString(MemberAccess.Member));
							Emitter.EmitU16Operand(
							    EOpcode::LoadThisField,
							    AddString(MemberAccess.Member));
							return true;
						}
						ReportError(
						    "unknown base member: " + MemberAccess.Member,
						    Expression.Location);
						return false;
					}
					{
						// C++-style static data member write: `Type.Field = value` or
						// `instance.Field = value` → StoreGlobal.
						std::string StaticGlobalName;
						std::string StaticTypeName;
						bool bStaticConst = false;
						std::string StaticAccessOwner;
						bool bStaticTypeNameReceiver = false;
						if (TryResolveStaticField(
						        *MemberAccess.Object,
						        MemberAccess.Member,
						        StaticGlobalName,
						        StaticTypeName,
						        bStaticConst,
						        StaticAccessOwner,
						        bStaticTypeNameReceiver))
						{
							if (!ValidateMemberAccess(
							        Expression.Location,
							        StaticAccessOwner,
							        MemberAccess.Member,
							        false))
							{
								return false;
							}
							EnsureStaticFieldGlobalRegistered(
							    StaticAccessOwner, StaticGlobalName, Expression.Location);
							std::uint16_t StaticSlot = 0;
							if (!ResolveGlobal(StaticGlobalName, StaticSlot))
							{
								ReportError(
								    "undeclared static member: " + MemberAccess.Member,
								    Expression.Location);
								return false;
							}
							if (!bStaticTypeNameReceiver)
							{
								// `instance.staticField = v`: evaluate the object for
								// side effects, then discard it before StoreGlobal.
								if (!CompileBorrowedReceiver(Emitter, *MemberAccess.Object))
								{
									return false;
								}
								Emitter.Emit(EOpcode::Pop);
							}
							const std::string PreviousExpectedType = ExpressionExpectedType;
							ExpressionExpectedType = StaticTypeName;
							const bool bCompiled = CompileExpression(*Assign.Value);
							const bool bCoerced = bCompiled && EmitValueCoercionForTarget(Emitter, StaticTypeName, *Assign.Value, Expression.Location);
							ExpressionExpectedType = PreviousExpectedType;
							if (!bCompiled || !bCoerced)
							{
								return false;
							}
							if (NeedsPassByValueCopy(StaticTypeName) && !CanTransferCoercedValue(*Assign.Value, StaticTypeName))
							{
								EmitValueCopy(Emitter, StaticTypeName);
							}
							Emitter.EmitU16Operand(EOpcode::StoreGlobal, StaticSlot);
							Emitter.EmitU16Operand(EOpcode::LoadGlobal, StaticSlot);
							return true;
						}
					}
					// Namespace-qualified global write.
					{
						std::string GlobalLinkageName;
						if (TryResolveNamespaceQualifiedGlobal(
						        *MemberAccess.Object,
						        MemberAccess.Member,
						        GlobalLinkageName))
						{
							std::uint16_t GlobalSlot = 0;
							if (!GlobalLinkageName.empty() && ResolveGlobal(GlobalLinkageName, GlobalSlot))
							{
								if (GlobalVarConsts.find(GlobalLinkageName) != GlobalVarConsts.end())
								{
									ReportError("cannot assign to const", Expression.Location);
									return false;
								}
								std::string TargetTypeName;
								const auto TypeIt = GlobalVarTypes.find(GlobalLinkageName);
								if (TypeIt != GlobalVarTypes.end())
								{
									TargetTypeName = TypeIt->second;
								}
								const std::string PreviousExpectedType = ExpressionExpectedType;
								ExpressionExpectedType = TargetTypeName;
								const bool bCompiled = CompileExpression(*Assign.Value);
								const bool bCoerced = bCompiled && (TargetTypeName.empty() || EmitValueCoercionForTarget(Emitter, TargetTypeName, *Assign.Value, Expression.Location));
								ExpressionExpectedType = PreviousExpectedType;
								if (!bCompiled || !bCoerced)
								{
									return false;
								}
								if (!TargetTypeName.empty() && NeedsPassByValueCopy(TargetTypeName) && !CanTransferCoercedValue(*Assign.Value, TargetTypeName))
								{
									EmitValueCopy(Emitter, TargetTypeName);
								}
								Emitter.EmitU16Operand(EOpcode::StoreGlobal, GlobalSlot);
								Emitter.EmitU16Operand(EOpcode::LoadGlobal, GlobalSlot);
								return true;
							}
						}
					}
					FScope::FMapEntryViewBinding EntryView;
					std::vector<std::string> EntryMembers;
					if (TryGetMapEntryViewMemberPath(*Assign.Target, EntryView, EntryMembers))
					{
						if (EntryMembers.empty() || EntryMembers[0] == "first")
						{
							ReportErrorId(
							    "diag.cannot_assign_map_entry_view_key",
							    Expression.Location);
							return false;
						}
						if (EntryMembers[0] != "second")
						{
							ReportErrorId(
							    "diag.unknown_field_on_type",
							    Expression.Location,
							    {EntryMembers[0], "pair"});
							return false;
						}
						std::uint16_t WriteSlot = EntryView.RefSlot;
						if (EntryMembers.size() > 1)
						{
							std::vector<std::string> NestedFields(
							    EntryMembers.begin() + 1,
							    EntryMembers.end());
							if (!EmitMapEntryViewValueFieldRef(Emitter, EntryView, NestedFields))
							{
								return false;
							}
							WriteSlot = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::BindLocal, WriteSlot);
						}
						std::string FieldTypeName;
						const bool bHasFieldType =
						    TryResolveExprTypeName(*Assign.Target, FieldTypeName);
						const std::string PreviousExpectedType = ExpressionExpectedType;
						if (bHasFieldType)
						{
							ExpressionExpectedType = FieldTypeName;
						}
						const bool bCompiled = CompileExpression(*Assign.Value);
						const bool bCoerced = !bHasFieldType || (bCompiled && EmitValueCoercionForTarget(Emitter, FieldTypeName, *Assign.Value, Expression.Location));
						ExpressionExpectedType = PreviousExpectedType;
						if (!bCompiled || !bCoerced)
						{
							return false;
						}
						if (!bHasFieldType || (NeedsPassByValueCopy(FieldTypeName) && !CanTransferCoercedValue(*Assign.Value, FieldTypeName)))
						{
							EmitValueCopy(Emitter, FieldTypeName);
						}
						Emitter.EmitU16Operand(EOpcode::StoreLocal, WriteSlot);
						Emitter.EmitU16Operand(EOpcode::LoadLocal, WriteSlot);
						return true;
					}
					std::string OwnerTypeName;
					if (TryResolveExprTypeName(*MemberAccess.Object, OwnerTypeName) && !ValidateMemberAccess(Expression.Location, OwnerTypeName, MemberAccess.Member, false))
					{
						return false;
					}
					if (!CompileBorrowedReceiver(Emitter, *MemberAccess.Object))
					{
						return false;
					}
					std::string FieldTypeName;
					const bool bHasFieldType = TryResolveExprTypeName(*Assign.Target, FieldTypeName);
					const std::string PreviousExpectedType = ExpressionExpectedType;
					if (bHasFieldType)
					{
						ExpressionExpectedType = FieldTypeName;
					}
					const bool bCompiled = CompileExpression(*Assign.Value);
					const bool bCoerced = !bHasFieldType || (bCompiled && EmitValueCoercionForTarget(Emitter, FieldTypeName, *Assign.Value, Expression.Location));
					ExpressionExpectedType = PreviousExpectedType;
					if (!bCompiled || !bCoerced)
					{
						return false;
					}
					if (!bHasFieldType || (NeedsPassByValueCopy(FieldTypeName) && !CanTransferCoercedValue(*Assign.Value, FieldTypeName)))
					{
						EmitValueCopy(Emitter, FieldTypeName);
					}
					if (!OwnerTypeName.empty())
					{
						const std::string HostPropOwner =
						    ResolveHostDispatchTypeName(OwnerTypeName);
						const std::string HostPropType =
						    HostPropOwner.empty() ? OwnerTypeName : HostPropOwner;
						const std::string SetterName =
						    MakeHostPropertySetterNativeName(HostPropType, MemberAccess.Member);
						std::uint16_t SetterIndex = static_cast<std::uint16_t>(-1);
						if (FindNative(SetterName, SetterIndex))
						{
							const std::uint16_t ValueTemp = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::StoreLocal, ValueTemp);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
							Emitter.EmitCallNative(SetterIndex, 2);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
							return true;
						}
						std::uint16_t GetterIndex = static_cast<std::uint16_t>(-1);
						if (FindNative(
						        MakeHostPropertyGetterNativeName(
						            HostPropType, MemberAccess.Member),
						        GetterIndex))
						{
							ReportErrorId(
							    "diag.cannot_assign_host_property",
							    Expression.Location,
							    {MemberAccess.Member, DisplayTypeName(HostPropType)});
							return false;
						}
					}
					Emitter.EmitU16Operand(EOpcode::StoreField, AddString(MemberAccess.Member));
					return true;
				}
				std::string TargetTypeName;
				const bool bHasTargetType = TryResolveExprTypeName(*Assign.Target, TargetTypeName);
				if (Assign.Target->Kind == EExprKind::Identifier)
				{
					const std::string& WholeName =
					    static_cast<const FIdentifierExpr&>(*Assign.Target).Name;
					FScope::FMapEntryViewBinding WholeEntryView;
					if (TryGetMapEntryViewBinding(WholeName, WholeEntryView))
					{
						ReportErrorId(
						    "diag.cannot_assign_map_entry_view",
						    Expression.Location);
						return false;
					}
				}
				{
					const std::string PreviousExpectedType = ExpressionExpectedType;
					if (bHasTargetType)
					{
						ExpressionExpectedType = TargetTypeName;
					}
					const bool bCompiled = CompileExpression(*Assign.Value);
					const bool bCoerced = !bHasTargetType || (bCompiled && EmitValueCoercionForTarget(Emitter, TargetTypeName, *Assign.Value, Expression.Location));
					ExpressionExpectedType = PreviousExpectedType;
					if (!bCompiled || !bCoerced)
					{
						return false;
					}
				}
				if (!bHasTargetType || (NeedsPassByValueCopy(TargetTypeName) && !CanTransferCoercedValue(*Assign.Value, TargetTypeName)))
				{
					EmitValueCopy(Emitter, TargetTypeName);
				}
				if (Assign.Target->Kind == EExprKind::Identifier)
				{
					const std::string& Name = static_cast<const FIdentifierExpr&>(*Assign.Target).Name;
					FScope::FMapEntryViewBinding EntryView;
					if (TryGetMapEntryViewBinding(Name, EntryView))
					{
						ReportErrorId(
						    "diag.cannot_assign_map_entry_view",
						    Expression.Location);
						return false;
					}
					std::uint16_t Slot = 0;
					if (ResolveLocal(Name, Slot))
					{
						Emitter.EmitU16Operand(EOpcode::StoreLocal, Slot);
						Emitter.EmitU16Operand(EOpcode::LoadLocal, Slot);
						return true;
					}
					std::uint16_t FieldIndex = 0;
					if (ResolveThisField(Name, FieldIndex))
					{
						if (!ValidateThisFieldAccess(Expression.Location, Name))
						{
							return false;
						}
						Emitter.EmitU16Operand(EOpcode::StoreThisField, AddString(Name));
						Emitter.EmitU16Operand(EOpcode::LoadThisField, AddString(Name));
						return true;
					}
					// Host-subclass instance method: bare name may be a host-base property.
					if (const std::string HostBase = CurrentMethodHostBaseTypeName();
					    !HostBase.empty())
					{
						std::uint16_t SetterIndex = static_cast<std::uint16_t>(-1);
						if (FindNative(
						        MakeHostPropertySetterNativeName(HostBase, Name),
						        SetterIndex))
						{
							const std::uint16_t ValueTemp = AllocateAssignTemp();
							Emitter.EmitU16Operand(EOpcode::StoreLocal, ValueTemp);
							Emitter.Emit(EOpcode::LoadThis);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
							Emitter.EmitCallNative(SetterIndex, 2);
							Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
							return true;
						}
						std::uint16_t GetterIndex = static_cast<std::uint16_t>(-1);
						if (FindNative(
						        MakeHostPropertyGetterNativeName(HostBase, Name),
						        GetterIndex))
						{
							ReportErrorId(
							    "diag.cannot_assign_host_property",
							    Expression.Location,
							    {Name, DisplayTypeName(HostBase)});
							return false;
						}
					}
					std::uint16_t GlobalSlot = 0;
					const std::string GlobalLinkageName =
					    ResolveGlobalLinkageName(Name, CurrentModulePath);
					if (GlobalLinkageName.empty())
					{
						ReportError("undeclared identifier: " + Name, Expression.Location);
						return false;
					}
					if (!ResolveGlobal(GlobalLinkageName, GlobalSlot))
					{
						ReportError("undeclared identifier: " + Name, Expression.Location);
						return false;
					}
					Emitter.EmitU16Operand(EOpcode::StoreGlobal, GlobalSlot);
					Emitter.EmitU16Operand(EOpcode::LoadGlobal, GlobalSlot);
					return true;
				}
				ReportError("invalid assignment target", Expression.Location);
				return false;
			}
