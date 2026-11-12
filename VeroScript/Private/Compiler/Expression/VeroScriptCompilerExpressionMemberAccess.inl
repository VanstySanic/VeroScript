// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		case EExprKind::Conditional:
			{
				const FConditionalExpr& Conditional = static_cast<const FConditionalExpr&>(Expression);
				if (!RequireBoolCondition(*Conditional.Condition, Expression.Location))
				{
					return false;
				}
				std::string ThenType;
				std::string ElseType;
				const bool bThenKnown = TryResolveExprTypeName(*Conditional.ThenExpr, ThenType);
				const bool bElseKnown = TryResolveExprTypeName(*Conditional.ElseExpr, ElseType);
				if (bThenKnown && bElseKnown && ThenType != ElseType)
				{
					ReportErrorId(
					    "diag.conditional_branches_have_mismatched_types_vs",
					    Expression.Location,
					    {ThenType, ElseType});
					return false;
				}
				if (!CompileExpression(*Conditional.Condition) || !EmitValueCoercionForTarget(Emitter, "bool", *Conditional.Condition, Expression.Location))
				{
					return false;
				}
				const std::size_t JumpIfFalseOperand = Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0);
				if (!CompileExpression(*Conditional.ThenExpr))
				{
					return false;
				}
				if (bThenKnown && NeedsPassByValueCopy(ThenType) && !CanTransferCoercedValue(*Conditional.ThenExpr, ThenType))
				{
					EmitValueCopy(Emitter, ThenType);
				}
				const std::size_t JumpEndOperand = Emitter.EmitI32Operand(EOpcode::Jmp, 0);
				Emitter.PatchI32(
				    JumpIfFalseOperand,
				    Emitter.CurrentOffset() - static_cast<std::int32_t>(JumpIfFalseOperand + 4));
				if (!CompileExpression(*Conditional.ElseExpr))
				{
					return false;
				}
				if (bElseKnown && NeedsPassByValueCopy(ElseType) && !CanTransferCoercedValue(*Conditional.ElseExpr, ElseType))
				{
					EmitValueCopy(Emitter, ElseType);
				}
				Emitter.PatchI32(
				    JumpEndOperand,
				    Emitter.CurrentOffset() - static_cast<std::int32_t>(JumpEndOperand + 4));
				return true;
			}
		case EExprKind::MemberAccess:
			{
				const FMemberAccessExpr& MemberAccess = static_cast<const FMemberAccessExpr&>(Expression);
				std::string QualifiedFreeName;
				std::string QualifiedGlobalName;
				if (MemberAccess.Object && !IsSuperReceiver(*MemberAccess.Object) && TryBuildNamespaceQualifiedName(*MemberAccess.Object, MemberAccess.Member, QualifiedFreeName) && FindFunctionDeclForReference(QualifiedFreeName, CurrentModulePath) && (MemberAccess.bExplicitTemplateArguments || (ResolveEnumConstantLinkageName(QualifiedFreeName, CurrentModulePath).empty() && !TryResolveNamespaceQualifiedGlobal(*MemberAccess.Object, MemberAccess.Member, QualifiedGlobalName))))
				{
					FIdentifierExpr Reference(std::move(QualifiedFreeName));
					Reference.Location = Expression.Location;
					Reference.TypeArgs = MemberAccess.TypeArgs;
					Reference.bExplicitTemplateArguments = MemberAccess.bExplicitTemplateArguments;
					return CompileExpression(Reference);
				}
				if (MemberAccess.bExplicitTemplateArguments) return CompileTemplateMethodReference(MemberAccess, Emitter);
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
						std::uint16_t HostGetterIndex = static_cast<std::uint16_t>(-1);
						if (FindNative(
						        MakeHostPropertyGetterNativeName(HostBase, MemberAccess.Member),
						        HostGetterIndex))
						{
							Emitter.Emit(EOpcode::LoadThis);
							Emitter.EmitCallNative(HostGetterIndex, 1);
							return true;
						}
					}
					// Script base fields are flattened into the derived layout.
					std::uint16_t BaseFieldIndex = 0;
					if (ResolveThisField(MemberAccess.Member, BaseFieldIndex))
					{
						if (!ValidateThisFieldAccess(Expression.Location, MemberAccess.Member))
						{
							return false;
						}
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
				std::int64_t EnumValue = 0;
				std::string DisplayName;
				std::string ScopedEnumType;
				if (TryResolveEnumQualifiedConstant(
				        *MemberAccess.Object,
				        MemberAccess.Member,
				        EnumValue,
				        DisplayName,
				        &ScopedEnumType))
				{
					FBytecodeConstant Constant;
					std::string UnderlyingType = "int";
					if (ScopedEnumType.empty() && MemberAccess.Object->Kind == EExprKind::Identifier && ActiveProgram)
					{
						const std::string& EnumName =
						    static_cast<const FIdentifierExpr&>(
						        *MemberAccess.Object)
						        .Name;
						for (const FEnumDecl& EnumDecl : ActiveProgram->Enums)
						{
							if (EnumDecl.Name == EnumName)
							{
								UnderlyingType = EnumDecl.UnderlyingTypeName;
								break;
							}
						}
					}
					if (!ScopedEnumType.empty())
					{
						Constant.Kind = EConstantKind::Enum;
					}
					else if (UnderlyingType == "char")
					{
						Constant.Kind = EConstantKind::I8;
					}
					else if (UnderlyingType == "unsigned char")
					{
						Constant.Kind = EConstantKind::U8;
					}
					else if (UnderlyingType == "short")
					{
						Constant.Kind = EConstantKind::I16;
					}
					else if (UnderlyingType == "unsigned short")
					{
						Constant.Kind = EConstantKind::U16;
					}
					else if (UnderlyingType == "unsigned int")
					{
						Constant.Kind = EConstantKind::U32;
					}
					else if (UnderlyingType == "long" || UnderlyingType == "int64")
					{
						Constant.Kind = EConstantKind::I64;
					}
					else
					{
						Constant.Kind = EConstantKind::I32;
					}
					if (Constant.Kind == EConstantKind::I8)
					{
						Constant.I8 = static_cast<std::int8_t>(EnumValue);
					}
					else if (Constant.Kind == EConstantKind::U8)
					{
						Constant.U8 = static_cast<std::uint8_t>(EnumValue);
					}
					else if (Constant.Kind == EConstantKind::I16)
					{
						Constant.I16 = static_cast<std::int16_t>(EnumValue);
					}
					else if (Constant.Kind == EConstantKind::U16)
					{
						Constant.U16 = static_cast<std::uint16_t>(EnumValue);
					}
					else if (Constant.Kind == EConstantKind::I32)
					{
						Constant.I32 = static_cast<std::int32_t>(EnumValue);
					}
					else if (Constant.Kind == EConstantKind::U32)
					{
						Constant.U32 = static_cast<std::uint32_t>(EnumValue);
					}
					else
					{
						Constant.I64 = EnumValue;
					}
					if (!ScopedEnumType.empty())
					{
						Constant.StringIndex = AddString(Runtime::EnumValueTypeName(*Module, ScopedEnumType));
					}
					Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Constant));
					return true;
				}
				{
					// C++-style static data member read: `Type.Field` or
					// `instance.Field` → LoadGlobal (object evaluated + discarded).
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
							// `instance.staticField`: evaluate the object for side
							// effects, then discard it — the field lives in a global.
							if (!CompileBorrowedReceiver(Emitter, *MemberAccess.Object))
							{
								return false;
							}
							Emitter.Emit(EOpcode::Pop);
						}
						Emitter.EmitU16Operand(EOpcode::LoadGlobal, StaticSlot);
						return true;
					}
				}
				{
					std::uint16_t StaticConstantIndex = static_cast<std::uint16_t>(-1);
					if (TryResolveHostStaticConstant(
					        *MemberAccess.Object,
					        MemberAccess.Member,
					        StaticConstantIndex))
					{
						Emitter.EmitCallNative(StaticConstantIndex, 0);
						return true;
					}
				}
				// Namespace-qualified global.
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
							Emitter.EmitU16Operand(EOpcode::LoadGlobal, GlobalSlot);
							return true;
						}
					}
				}
				if (MemberAccess.Object->Kind == EExprKind::Identifier)
				{
					const std::string& TypeName =
					    static_cast<const FIdentifierExpr&>(*MemberAccess.Object).Name;
					std::string BoundTypeName;
					std::uint16_t ThisFieldIndex = 0;
					const bool bValueBinding = TryResolveVariableTypeName(TypeName, BoundTypeName) || ResolveThisField(TypeName, ThisFieldIndex);
					if (!bValueBinding && IsEnumTypeName(TypeName))
					{
						ReportErrorId(
						    "diag.unknown_enum_member",
						    Expression.Location,
						    {TypeName, MemberAccess.Member});
						return false;
					}
				}
				std::string OwnerTypeName;
				if (TryResolveExprTypeName(*MemberAccess.Object, OwnerTypeName))
				{
					std::string FieldTypeName;
					if (!FindFieldTypeOnOwnerType(OwnerTypeName, MemberAccess.Member, FieldTypeName))
					{
						ReportErrorId(
						    "diag.unknown_field_on_type",
						    Expression.Location,
						    {MemberAccess.Member, DisplayTypeName(OwnerTypeName)});
						return false;
					}
					if (!ValidateMemberAccess(
					        Expression.Location,
					        OwnerTypeName,
					        MemberAccess.Member,
					        false))
					{
						return false;
					}
				}
				if (MemberAccess.Object->Kind == EExprKind::Identifier)
				{
					const std::string& OwnerName =
					    static_cast<const FIdentifierExpr&>(*MemberAccess.Object).Name;
					FScope::FMapEntryViewBinding EntryView;
					if (TryGetMapEntryViewBinding(OwnerName, EntryView))
					{
						if (MemberAccess.Member == "first")
						{
							Emitter.EmitU16Operand(EOpcode::LoadLocal, EntryView.KeySlot);
							Emitter.Emit(EOpcode::CloneStruct);
							return true;
						}
						if (MemberAccess.Member == "second")
						{
							Emitter.EmitU16Operand(EOpcode::LoadLocal, EntryView.RefSlot);
							return true;
						}
						ReportErrorId(
						    "diag.unknown_field_on_type",
						    Expression.Location,
						    {MemberAccess.Member, DisplayTypeName(OwnerTypeName)});
						return false;
					}
				}
				if (!CompileBorrowedReceiver(Emitter, *MemberAccess.Object))
				{
					return false;
				}
				if (!CanBindImplicitRefArgument(*MemberAccess.Object, false))
				{
					Emitter.Emit(EOpcode::Dup);
					Emitter.EmitU16Operand(EOpcode::BindLocal, AllocateAssignTemp());
				}
				if (!OwnerTypeName.empty())
				{
					const std::string HostPropOwner =
					    ResolveHostDispatchTypeName(OwnerTypeName);
					const std::string HostPropType =
					    HostPropOwner.empty() ? OwnerTypeName : HostPropOwner;
					const std::string GetterName =
					    MakeHostPropertyGetterNativeName(HostPropType, MemberAccess.Member);
					std::uint16_t GetterIndex = static_cast<std::uint16_t>(-1);
					if (FindNative(GetterName, GetterIndex))
					{
						Emitter.EmitCallNative(GetterIndex, 1);
						return true;
					}
				}
				Emitter.EmitU16Operand(EOpcode::LoadField, AddString(MemberAccess.Member));
				return true;
			}
		case EExprKind::IndexAccess:
			{
				// GetIndexCopy: element by value. GetIndexRef: element alias (ref a[i] / nested).
				const FIndexAccessExpr& IndexAccess = static_cast<const FIndexAccessExpr&>(Expression);
				std::string IndexType;
				if (TryResolveExprTypeName(*IndexAccess.Index, IndexType) && IndexType == "void")
				{
					ReportError("cannot use void value in expression", Expression.Location);
					return false;
				}
				std::string ObjectType;
				if (TryResolveExprTypeName(*IndexAccess.Object, ObjectType) && ObjectType == "void")
				{
					ReportError("cannot use void value in expression", Expression.Location);
					return false;
				}
				if (TypeSupportsOperatorOverload(ObjectType))
				{
					FResolvedScriptOperator Resolved;
					bool bAmbiguous = false;
					if (ResolveScriptOperator("operator[]", *IndexAccess.Object,
					        {IndexAccess.Index.get()}, Resolved, bAmbiguous))
					{
						return CompileScriptOperator(Emitter, "operator[]", *IndexAccess.Object,
						    {IndexAccess.Index.get()}, Resolved, Expression.Location);
					}
					ReportError(bAmbiguous ? "ambiguous operator[]" : "no matching operator[] for type " + DisplayTypeName(ObjectType), Expression.Location);
					return false;
				}
				if (!CompileExpression(*IndexAccess.Object)) return false;
				if (!CanBindImplicitRefArgument(*IndexAccess.Object, false))
				{
					Emitter.Emit(EOpcode::Dup);
					Emitter.EmitU16Operand(EOpcode::BindLocal, AllocateAssignTemp());
				}
				if (!CompileExpression(*IndexAccess.Index))
				{
					return false;
				}
				Emitter.Emit(EOpcode::GetIndexCopy);
				std::string ElementType;
				if (TryResolveExprTypeName(Expression, ElementType) && NeedsPassByValueCopy(ElementType))
				{
					CopyRequirements.push_back({ElementType, CurrentFunction ? CurrentFunction->Name : CurrentOwnerTypeLinkage.empty() ? ""
					                                                                                                                   : CurrentOwnerTypeLinkage + "::@initializer",
					    Expression.Location, false, CurrentAccessFunctionLinkage});
				}
				return true;
			}
