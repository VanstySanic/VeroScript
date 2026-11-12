// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

	case EExprKind::Lambda:
		{
			return CompileLambdaExpression(static_cast<const FLambdaExpr&>(Expression), Emitter);
		}
	case EExprKind::Identifier:
		{
			const auto& Identifier = static_cast<const FIdentifierExpr&>(Expression);
			if (Identifier.bExplicitTemplateArguments) return CompileTemplateFunctionReference(Identifier, Emitter);
			const std::string& Name = Identifier.Name;
			const auto TemplateBinding = ActiveTypeSubst.find(Name);
			FTemplateArgument TemplateArgument;
			if (TemplateBinding != ActiveTypeSubst.end() && TemplateBinding->second.rfind("@value:", 0) == 0 && DecodeTemplateArgument(std::string_view(TemplateBinding->second).substr(7), TemplateArgument) && TemplateArgument.Kind == ETemplateParameterKind::Value)
			{
				const auto& Value = TemplateArgument.Value;
				if (Value.Type.bBoolean)
				{
					Emitter.EmitU8Operand(EOpcode::ConstBool, Value.Bits != 0 ? 1 : 0);
					return true;
				}
				FBytecodeConstant Constant;
				const auto Mask = Value.Type.Width == 64 ? std::numeric_limits<std::uint64_t>::max()
				                                         : (std::uint64_t{1} << Value.Type.Width) - 1;
				const auto Signed = (!Value.Type.bUnsigned || Value.Type.Width == 64) && (Value.Bits & (std::uint64_t{1} << (Value.Type.Width - 1))) != 0
				    ? -1 - static_cast<std::int64_t>((~Value.Bits) & Mask)
				    : static_cast<std::int64_t>(Value.Bits);
				if (Value.Type.bEnum && Value.Type.bScopedEnum)
				{
					Constant.Kind = EConstantKind::Enum;
					Constant.I64 = Signed;
					Constant.U64 = Value.Bits;
					const auto* Enum = FindEnumDeclaration(Value.Type.Name);
					// Runtime enum values retain their declaration spelling; the exact
					// module identity stays in the separate template argument encoding.
					Constant.StringIndex = AddString(Enum && EnclosingTemplateParameterNames(Enum->Scope).empty()
					        ? Enum->Name
					        : Value.Type.Name);
				}
				else if (Value.Type.bUnsigned)
				{
					Constant.Kind = Value.Type.Width == 64 ? EConstantKind::U64 : Value.Type.Width == 32 ? EConstantKind::U32
					    : Value.Type.Width == 16                                                         ? EConstantKind::U16
					                                                                                     : EConstantKind::U8;
					Constant.U64 = Value.Bits;
					Constant.U32 = static_cast<std::uint32_t>(Value.Bits);
					Constant.U16 = static_cast<std::uint16_t>(Value.Bits);
					Constant.U8 = static_cast<std::uint8_t>(Value.Bits);
				}
				else
				{
					Constant.Kind = Value.Type.Width == 64 ? EConstantKind::I64 : Value.Type.Width == 32 ? EConstantKind::I32
					    : Value.Type.Width == 16                                                         ? EConstantKind::I16
					                                                                                     : EConstantKind::I8;
					Constant.I64 = Signed;
					Constant.I32 = static_cast<std::int32_t>(Signed);
					Constant.I16 = static_cast<std::int16_t>(Signed);
					Constant.I8 = static_cast<std::int8_t>(Signed);
				}
				Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Constant));
				return true;
			}
			FScope::FMapEntryViewBinding EntryView;
			if (TryGetMapEntryViewBinding(Name, EntryView))
			{
				std::string PairTypeName;
				if (!TryResolveVariableTypeName(Name, PairTypeName))
				{
					ReportError("cannot resolve map entry view type: " + Name, Expression.Location);
					return false;
				}
				return EmitMaterializeMapEntryPair(
				    Emitter,
				    EntryView,
				    PairTypeName,
				    Expression.Location);
			}
			std::uint16_t Slot = 0;
			if (ResolveLocal(Name, Slot))
			{
				Emitter.EmitU16Operand(EOpcode::LoadLocal, Slot);
				return true;
			}
			if (const auto Static = ResolveLocalStaticLinkageName(Name); !Static.empty())
			{
				if (!ResolveGlobal(Static, Slot)) return false;
				if (CurrentFunction)
				{
					CurrentFunction->bUsesLocalStaticStorage = true;
				}
				Emitter.EmitU16Operand(EOpcode::LoadGlobal, Slot);
				return true;
			}
			std::uint16_t FieldIndex = 0;
			if (ResolveLocalEnumConstantLinkageName(Name).empty() && ResolveThisField(Name, FieldIndex))
			{
				if (!ValidateThisFieldAccess(Expression.Location, Name))
				{
					return false;
				}
				Emitter.EmitU16Operand(EOpcode::LoadThisField, AddString(Name));
				return true;
			}
			if (IsForbiddenRuntimeCapture(Name))
			{
				ReportError("a local type cannot capture an enclosing runtime binding: " + Name, Expression.Location);
				return false;
			}
			const std::string EnumConstantLinkage =
			    ResolveEnumConstantLinkageName(Name, CurrentModulePath);
			if (!EnumConstantLinkage.empty())
			{
				const auto EnumIt = EnumConstants.find(EnumConstantLinkage);
				if (EnumIt != EnumConstants.end())
				{
					FTemplateConstant Constant;
					const auto Type = EnumConstantTypeNames.find(EnumConstantLinkage);
					if (!TryGetTemplateValueType(Type == EnumConstantTypeNames.end() ? "int" : Type->second, Constant.Type))
					{
						ReportError("invalid enum constant underlying type", Expression.Location);
						return false;
					}
					Constant.Bits = static_cast<std::uint64_t>(EnumIt->second);
					if (Constant.Type.Width < 64)
					{
						Constant.Bits &= (std::uint64_t{1} << Constant.Type.Width) - 1;
					}
					return CompileExpression(*MakeConstantExpression(Constant, Expression.Location));
				}
			}
			if (const FFunctionDecl* const FreeFunction =
			        FindUniqueFreeFunction(Name, CurrentModulePath))
			{
				if (IsMethodBindingFactoryDeclaration(*FreeFunction))
				{
					ReportError("method binding factory requires a direct call", Expression.Location);
					return false;
				}
				if (FreeFunction->bDeleted)
				{
					ReportErrorId(
					    "diag.use_of_deleted_declaration",
					    Expression.Location,
					    {Name});
					return false;
				}
				if (TemplateParameterNames(*FreeFunction).empty())
				{
					const std::string LinkageName = MakeFreeFunctionLinkageName(*FreeFunction);
					const std::uint16_t FunctionIndex = FindFunctionIndex(LinkageName);
					if (FunctionIndex != static_cast<std::uint16_t>(-1))
					{
						if (Module->Functions[FunctionIndex].ClosureTypeName.empty())
						{
							Module->Functions[FunctionIndex].ClosureTypeName =
							    MakeFunctionTypeFromDecl(*FreeFunction);
						}
						Emitter.EmitMakeClosure(FunctionIndex, 0);
						return true;
					}
				}
			}
			const std::string GlobalLinkageName = ResolveGlobalLinkageName(Name, CurrentModulePath);
			if (!GlobalLinkageName.empty())
			{
				std::uint16_t GlobalSlot = 0;
				if (ResolveGlobal(GlobalLinkageName, GlobalSlot))
				{
					Emitter.EmitU16Operand(EOpcode::LoadGlobal, GlobalSlot);
					return true;
				}
			}
			// Host-subclass instance method: a name that binds to nothing in script
			// may still be a property on the inherited host base.
			if (const std::string HostBase = CurrentMethodHostBaseTypeName();
			    !HostBase.empty())
			{
				std::uint16_t HostGetterIndex = static_cast<std::uint16_t>(-1);
				if (FindNative(
				        MakeHostPropertyGetterNativeName(HostBase, Name),
				        HostGetterIndex))
				{
					Emitter.Emit(EOpcode::LoadThis);
					Emitter.EmitCallNative(HostGetterIndex, 1);
					return true;
				}
			}
			ReportError("undeclared identifier: " + Name, Expression.Location);
			return false;
		}
	case EExprKind::This:
		{
			if (!CurrentFunction || !CurrentFunction->bIsMethod)
			{
				ReportError("'this' is only valid inside methods", Expression.Location);
				return false;
			}
			Emitter.Emit(EOpcode::LoadThis);
			return true;
		}
	case EExprKind::Unary:
		{
			const FUnaryExpr& Unary = static_cast<const FUnaryExpr&>(Expression);
			if (TryCompileOverloadedUnary(Emitter, Unary, Expression.Location))
			{
				return true;
			}
			if (HasError())
			{
				return false;
			}
			std::string OperandType;
			if (TryResolveExprTypeName(*Unary.Operand, OperandType))
			{
				if (OperandType == "void")
				{
					ReportError("cannot use void value in expression", Expression.Location);
					return false;
				}
				if (Unary.Op == EUnaryOp::Not)
				{
					if (!CanAssignBetweenTypes("bool", OperandType))
					{
						ReportError("logical not requires bool operand", Expression.Location);
						return false;
					}
				}
				else if (Unary.Op == EUnaryOp::Negate)
				{
					const bool bNumeric = IsSignedIntegerTypeName(OperandType) || Runtime::IsUnsignedTypeName(OperandType) || OperandType == "float" || OperandType == "double";
					if (!bNumeric)
					{
						ReportError("unary minus requires numeric operand", Expression.Location);
						return false;
					}
				}
				else if (Unary.Op == EUnaryOp::BitNot)
				{
					const bool bInteger = IsSignedIntegerTypeName(OperandType) || Runtime::IsUnsignedTypeName(OperandType);
					if (!bInteger)
					{
						ReportError("bitwise not requires integer operand", Expression.Location);
						return false;
					}
				}
			}
			if (!CompileExpression(*Unary.Operand))
			{
				return false;
			}
			if (Unary.Op == EUnaryOp::Negate)
			{
				Emitter.Emit(EOpcode::Neg);
			}
			else if (Unary.Op == EUnaryOp::Not)
			{
				if (!EmitValueCoercionForTarget(Emitter, "bool", *Unary.Operand, Expression.Location))
				{
					return false;
				}
				Emitter.Emit(EOpcode::Not);
			}
			else if (Unary.Op == EUnaryOp::BitNot)
			{
				Emitter.Emit(EOpcode::BitNot);
			}
			return true;
		}
