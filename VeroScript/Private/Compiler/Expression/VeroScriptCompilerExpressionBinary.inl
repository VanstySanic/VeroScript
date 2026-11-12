// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

	case EExprKind::Binary:
		{
			const FBinaryExpr& Binary = static_cast<const FBinaryExpr&>(Expression);
			if (Binary.Op == EBinaryOp::LogicalAnd
					|| Binary.Op == EBinaryOp::LogicalOr)
			{
				std::vector<const FExpr*> Operands;
				const FExpr* Cursor = &Expression;
				while (Cursor && Cursor->Kind == EExprKind::Binary)
				{
					const auto& Node = static_cast<const FBinaryExpr&>(*Cursor);
					if (Node.Op != Binary.Op)
					{
						break;
					}
					Operands.push_back(Node.Right.get());
					Cursor = Node.Left.get();
				}
				if (Cursor)
				{
					Operands.push_back(Cursor);
				}
				std::reverse(Operands.begin(), Operands.end());
				bool bAllBuiltinBool = Operands.size() >= 2;
				for (const FExpr* const Operand : Operands)
				{
					std::string OperandType;
					if (!Operand
							|| !TryResolveExprTypeName(*Operand, OperandType)
							|| OperandType != "bool")
					{
						bAllBuiltinBool = false;
						break;
					}
				}
				if (bAllBuiltinBool)
				{
					// Flatten same-operator chains so source expressions with many
					// validation terms do not consume the native compiler stack.
					if (Binary.Op == EBinaryOp::LogicalAnd)
					{
						std::vector<std::size_t> FalseJumps;
						FalseJumps.reserve(Operands.size() - 1);
						for (std::size_t Index = 0; Index < Operands.size(); ++Index)
						{
							if (!CompileExpression(*Operands[Index]))
							{
								return false;
							}
							if (Index + 1 < Operands.size())
							{
								FalseJumps.push_back(
										Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0));
							}
						}
						const std::size_t EndJump =
								Emitter.EmitI32Operand(EOpcode::Jmp, 0);
						for (const std::size_t FalseJump : FalseJumps)
						{
							PatchRelativeJump(Emitter, FalseJump);
						}
						Emitter.EmitU8Operand(EOpcode::ConstBool, 0);
						PatchRelativeJump(Emitter, EndJump);
						return true;
					}

					std::vector<std::size_t> EndJumps;
					EndJumps.reserve(Operands.size() - 1);
					for (std::size_t Index = 0; Index < Operands.size(); ++Index)
					{
						if (!CompileExpression(*Operands[Index]))
						{
							return false;
						}
						if (Index + 1 < Operands.size())
						{
							const std::size_t EvalNextJump =
									Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0);
							Emitter.EmitU8Operand(EOpcode::ConstBool, 1);
							EndJumps.push_back(Emitter.EmitI32Operand(EOpcode::Jmp, 0));
							PatchRelativeJump(Emitter, EvalNextJump);
						}
					}
					for (const std::size_t EndJump : EndJumps)
					{
						PatchRelativeJump(Emitter, EndJump);
					}
					return true;
				}
			}
			if (Binary.Op == EBinaryOp::Add)
			{
				std::vector<const FExpr*> Operands;
				const FExpr* Cursor = &Expression;
				while (Cursor && Cursor->Kind == EExprKind::Binary)
				{
					const auto& Node = static_cast<const FBinaryExpr&>(*Cursor);
					if (Node.Op != EBinaryOp::Add)
					{
						break;
					}
					Operands.push_back(Node.Right.get());
					Cursor = Node.Left.get();
				}
				if (Cursor)
				{
					Operands.push_back(Cursor);
				}
				std::reverse(Operands.begin(), Operands.end());
				if (Operands.size() >= 3)
				{
					// Preserve the ordinary binary-emission path unless every operand has
					// exactly the same runtime representation.  In particular,
					// string + scalar must retain its per-edge ToString coercion, and
					// mixed numeric chains must retain the usual left-associative
					// conversion rules.  The narrow homogeneous case still removes the
					// compiler-stack growth that motivated this optimization.
					auto IsHomogeneousAddType = [](const std::string& TypeName)
					{
						return TypeName == "int"
								|| TypeName == "unsigned int"
								|| TypeName == "int64"
								|| TypeName == "uint64"
								|| TypeName == "float"
								|| TypeName == "double"
								|| TypeName == "string"
								|| TypeName == "wstring";
					};
					bool bHomogeneousPrimitiveChain = true;
					std::string ChainOperandType;
					for (const FExpr* const Operand : Operands)
					{
						std::string OperandType;
						if (!Operand
								|| !TryResolveExprTypeName(*Operand, OperandType)
								|| TypeSupportsOperatorOverload(OperandType))
						{
							bHomogeneousPrimitiveChain = false;
							break;
						}
						OperandType = NormalizeTypeAlias(OperandType);
						if (!IsHomogeneousAddType(OperandType)
								|| (!ChainOperandType.empty()
									&& ChainOperandType != OperandType))
						{
							bHomogeneousPrimitiveChain = false;
							break;
						}
						ChainOperandType = std::move(OperandType);
					}
					if (bHomogeneousPrimitiveChain)
					{
						if (!CompileExpression(*Operands.front()))
						{
							return false;
						}
						for (std::size_t Index = 1; Index < Operands.size(); ++Index)
						{
							if (!CompileExpression(*Operands[Index]))
							{
								return false;
							}
							Emitter.Emit(EOpcode::Add);
						}
						return true;
					}
				}
			}
			{
				std::string LeftType;
				std::string RightType;
				const bool bLeftKnown = TryResolveExprTypeName(*Binary.Left, LeftType);
				const bool bRightKnown = TryResolveExprTypeName(*Binary.Right, RightType);
				if (LeftType == "variant" || RightType == "variant")
				{
					if (LeftType != "variant" || RightType != "variant"
						|| (Binary.Op != EBinaryOp::Equal && Binary.Op != EBinaryOp::NotEqual))
					{
						ReportError("variant supports only equality with another variant; extract a concrete value first", Expression.Location);
						return false;
					}
					if (!CompileExpression(*Binary.Left) || !CompileExpression(*Binary.Right)) return false;
					Emitter.Emit(Binary.Op == EBinaryOp::Equal ? EOpcode::Eq : EOpcode::Ne);
					return true;
				}
				if ((bLeftKnown && LeftType == "void") || (bRightKnown && RightType == "void"))
				{
					ReportError("cannot use void value in expression", Expression.Location);
					return false;
				}
			}
			if (TryCompileOverloadedBinary(Emitter, Binary, Expression.Location))
			{
				return true;
			}
			if (HasError())
			{
				return false;
			}
			switch (Binary.Op)
			{
			case EBinaryOp::LogicalAnd:
			case EBinaryOp::LogicalOr:
				{
					std::string LeftType;
					std::string RightType;
					const bool bLeftKnown = TryResolveExprTypeName(*Binary.Left, LeftType);
					const bool bRightKnown = TryResolveExprTypeName(*Binary.Right, RightType);
					if ((bLeftKnown && !CanAssignBetweenTypes("bool", LeftType))
							|| (bRightKnown && !CanAssignBetweenTypes("bool", RightType))
							|| !bLeftKnown
							|| !bRightKnown)
					{
						ReportError(
								"logical operators require bool operands",
								Expression.Location);
						return false;
					}
					if (Binary.Op == EBinaryOp::LogicalAnd)
					{
						if (!CompileExpression(*Binary.Left)
							|| !EmitValueCoercionForTarget(Emitter, "bool", *Binary.Left, Expression.Location))
						{
							return false;
						}
						const std::size_t FalseJump = Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0);
						if (!CompileExpression(*Binary.Right)
							|| !EmitValueCoercionForTarget(Emitter, "bool", *Binary.Right, Expression.Location))
						{
							return false;
						}
						const std::size_t EndJump = Emitter.EmitI32Operand(EOpcode::Jmp, 0);
						PatchRelativeJump(Emitter, FalseJump);
						Emitter.EmitU8Operand(EOpcode::ConstBool, 0);
						PatchRelativeJump(Emitter, EndJump);
						return true;
					}
					if (!CompileExpression(*Binary.Left)
							|| !EmitValueCoercionForTarget(Emitter, "bool", *Binary.Left, Expression.Location))
					{
						return false;
					}
					{
						const std::size_t EvalRightJump = Emitter.EmitI32Operand(EOpcode::JmpIfFalse, 0);
						Emitter.EmitU8Operand(EOpcode::ConstBool, 1);
						const std::size_t EndJump = Emitter.EmitI32Operand(EOpcode::Jmp, 0);
						PatchRelativeJump(Emitter, EvalRightJump);
						if (!CompileExpression(*Binary.Right)
							|| !EmitValueCoercionForTarget(Emitter, "bool", *Binary.Right, Expression.Location))
						{
							return false;
						}
						PatchRelativeJump(Emitter, EndJump);
					}
					return true;
				}
			case EBinaryOp::In:
				{
					std::string ElementType;
					std::string ContainerType;
					if (!TryResolveExprTypeName(*Binary.Left, ElementType)
							|| !TryResolveExprTypeName(*Binary.Right, ContainerType))
					{
						ReportError("could not resolve 'in' operand types", Expression.Location);
						return false;
					}
					std::string ExpectedKeyType;
					if (Runtime::IsSetTypeName(ContainerType))
					{
						if (!ParseSetElementTypeForCompile(ContainerType, ExpectedKeyType))
						{
							ReportError("invalid set type for 'in'", Expression.Location);
							return false;
						}
					}
					else if (Runtime::IsMultiSetTypeName(ContainerType)
							|| Runtime::IsOrderedMultiSetTypeName(ContainerType))
					{
						if (!Runtime::ParseMultiSetElementType(
									ContainerType,
									ExpectedKeyType))
						{
							ReportError("invalid multiset type for 'in'", Expression.Location);
							return false;
						}
					}
					else if (Runtime::IsMapTypeName(ContainerType))
					{
						std::string ValueType;
						if (!ParseMapTypeForCompile(ContainerType, ExpectedKeyType, ValueType))
						{
							ReportError("invalid map type for 'in'", Expression.Location);
							return false;
						}
					}
					else if (Runtime::IsOrderedMapTypeName(ContainerType))
					{
						std::string ValueType;
						if (!ParseOrderedMapTypeForCompile(ContainerType, ExpectedKeyType, ValueType))
						{
							ReportError("invalid ordered_map type for 'in'", Expression.Location);
							return false;
						}
					}
					else
					{
						ReportError(
								"'in' requires set, map, or ordered_map on the right",
								Expression.Location);
						return false;
					}
					// Left-to-right: evaluate the element first, then the container, then call has.
					if (!CompileExpression(*Binary.Left))
					{
						return false;
					}
					if (!EmitValueCoercionForTarget(
								Emitter,
								ExpectedKeyType,
									*Binary.Left,
								Expression.Location))
					{
						return false;
					}
					const std::uint16_t ElementTemp = AllocateAssignTemp();
					Emitter.EmitU16Operand(EOpcode::StoreLocal, ElementTemp);
					if (!CompileExpression(*Binary.Right))
					{
						return false;
					}
					Emitter.EmitU16Operand(EOpcode::LoadLocal, ElementTemp);
					Emitter.EmitCallMethod(AddString("has"), 1);
					return true;
				}
			default:
				{
					break;
				}
			}
			if (Binary.Op == EBinaryOp::Add)
			{
				std::string LeftType;
				std::string RightType;
				const bool bLeftKnown = TryResolveExprTypeName(*Binary.Left, LeftType);
				const bool bRightKnown = TryResolveExprTypeName(*Binary.Right, RightType);
				if (bLeftKnown && bRightKnown)
				{
					const bool bLeftWString = LeftType == "wstring";
					const bool bRightWString = RightType == "wstring";
					const bool bLeftString = LeftType == "string";
					const bool bRightString = RightType == "string";
					const bool bLeftText = bLeftString || bLeftWString;
					const bool bRightText = bRightString || bRightWString;
					if (bLeftText || bRightText)
					{
						if (bLeftWString && bRightWString)
						{
							// wstring concatenation
						}
						else if (bLeftString && bRightString)
						{
							// string concatenation
						}
						else if ((bLeftWString || bRightWString) && (bLeftString || bRightString))
						{
							ReportError("cannot mix string and wstring concatenation", Expression.Location);
							return false;
						}
						else if (bLeftWString || bRightWString)
						{
							ReportError("wstring concatenation requires wstring operands", Expression.Location);
							return false;
						}
						else
						{
							const auto IsConcatScalar = [](const std::string& TypeName) -> bool
							{
								return TypeName == "bool"
										|| TypeName == "float"
										|| TypeName == "double"
										|| TypeName == "int"
										|| TypeName == "char"
										|| TypeName == "short"
										|| TypeName == "long"
										|| TypeName == "int64"
										|| Runtime::IsUnsignedTypeName(TypeName);
							};
							const bool bLeftScalar = IsConcatScalar(LeftType);
							const bool bRightScalar = IsConcatScalar(RightType);
							if ((bLeftString && bRightScalar) || (bRightString && bLeftScalar))
							{
								// string + scalar → coerce scalar via to_string
							}
							else
							{
								ReportError(
										"string concatenation requires string operands",
										Expression.Location);
								return false;
							}
						}
					}
					const bool bLeftUnsigned = Runtime::IsUnsignedTypeName(LeftType);
					const bool bRightUnsigned = Runtime::IsUnsignedTypeName(RightType);
					const bool bLeftSignedInt = IsSignedIntegerTypeName(LeftType);
					const bool bRightSignedInt = IsSignedIntegerTypeName(RightType);
					if ((bLeftUnsigned && bRightSignedInt) || (bRightUnsigned && bLeftSignedInt))
					{
						ReportError("cannot mix signed and unsigned", Expression.Location);
						return false;
					}
				}
			}
			else if (Binary.Op == EBinaryOp::Subtract
					|| Binary.Op == EBinaryOp::Multiply
					|| Binary.Op == EBinaryOp::Divide
					|| Binary.Op == EBinaryOp::Modulo
					|| Binary.Op == EBinaryOp::Less
					|| Binary.Op == EBinaryOp::LessEqual
					|| Binary.Op == EBinaryOp::Greater
					|| Binary.Op == EBinaryOp::GreaterEqual
					|| Binary.Op == EBinaryOp::Equal
					|| Binary.Op == EBinaryOp::NotEqual)
			{
				std::string LeftType;
				std::string RightType;
				if (TryResolveExprTypeName(*Binary.Left, LeftType)
						&& TryResolveExprTypeName(*Binary.Right, RightType))
				{
					LeftType = NormalizeTypeAlias(LeftType);
					RightType = NormalizeTypeAlias(RightType);
					const bool bLeftScopedEnum = IsScopedEnumTypeName(LeftType);
					const bool bRightScopedEnum = IsScopedEnumTypeName(RightType);
					if (bLeftScopedEnum || bRightScopedEnum)
					{
						const bool bEquality = Binary.Op == EBinaryOp::Equal
								|| Binary.Op == EBinaryOp::NotEqual;
						if (!bEquality || !bLeftScopedEnum || !bRightScopedEnum
								|| CanonicalTypeNameForCoercion(LeftType) != CanonicalTypeNameForCoercion(RightType))
						{
							ReportErrorId(
									"diag.cannot_compare_and",
									Expression.Location,
								{LeftType, RightType});
							return false;
						}
					}
					const bool bLeftUnsigned = Runtime::IsUnsignedTypeName(LeftType);
					const bool bRightUnsigned = Runtime::IsUnsignedTypeName(RightType);
					const bool bLeftSignedInt = IsSignedIntegerTypeName(LeftType);
					const bool bRightSignedInt = IsSignedIntegerTypeName(RightType);
					if ((bLeftUnsigned && bRightSignedInt) || (bRightUnsigned && bLeftSignedInt))
					{
						ReportError("cannot mix signed and unsigned", Expression.Location);
						return false;
					}

					const bool bLeftString = LeftType == "string";
					const bool bRightString = RightType == "string";
					const bool bLeftWString = LeftType == "wstring";
					const bool bRightWString = RightType == "wstring";
					const bool bLeftText = bLeftString || bLeftWString;
					const bool bRightText = bRightString || bRightWString;
					const bool bEquality = Binary.Op == EBinaryOp::Equal
							|| Binary.Op == EBinaryOp::NotEqual;
					const bool bOrdering = Binary.Op == EBinaryOp::Less
							|| Binary.Op == EBinaryOp::LessEqual
							|| Binary.Op == EBinaryOp::Greater
							|| Binary.Op == EBinaryOp::GreaterEqual;
					const bool bArithmetic = Binary.Op == EBinaryOp::Subtract
							|| Binary.Op == EBinaryOp::Multiply
							|| Binary.Op == EBinaryOp::Divide
							|| Binary.Op == EBinaryOp::Modulo;

					if (bLeftText || bRightText)
					{
						if (bArithmetic)
						{
							ReportError("arithmetic operands must be numeric", Expression.Location);
							return false;
						}
						if (bLeftText && bRightText)
						{
							if (bLeftWString != bRightWString)
							{
								ReportError(
										"cannot mix string and wstring comparison",
										Expression.Location);
								return false;
							}
							if (bOrdering && bLeftString)
							{
								ReportError(
										"comparison operands must be numeric",
										Expression.Location);
								return false;
							}
						}
						else if (bEquality)
						{
							ReportErrorId(
									"diag.cannot_compare_and",
									Expression.Location,
								{LeftType, RightType});
							return false;
						}
						else if (bOrdering)
						{
							ReportError(
									"comparison operands must be numeric",
									Expression.Location);
							return false;
						}
					}
					else if (bEquality
							&& ((LeftType == "nullptr"
								&& (RightType == "bool"
									|| RightType == "float"
									|| RightType == "double"
									|| bRightSignedInt
									|| bRightUnsigned))
									|| (RightType == "nullptr"
										&& (LeftType == "bool"
											|| LeftType == "float"
											|| LeftType == "double"
											|| bLeftSignedInt
											|| bLeftUnsigned))))
					{
						ReportErrorId(
								"diag.cannot_compare_and",
								Expression.Location,
							{LeftType, RightType});
						return false;
					}
					else if ((bEquality || bOrdering)
							&& ((LeftType == "bool" && RightType != "bool")
								|| (RightType == "bool" && LeftType != "bool")))
					{
						ReportErrorId(
								"diag.cannot_compare_and",
								Expression.Location,
							{LeftType, RightType});
						return false;
					}
				}
			}
			else if (Binary.Op == EBinaryOp::BitAnd
					|| Binary.Op == EBinaryOp::BitOr
					|| Binary.Op == EBinaryOp::BitXor
					|| Binary.Op == EBinaryOp::ShiftLeft
					|| Binary.Op == EBinaryOp::ShiftRight)
			{
				std::string LeftType;
				std::string RightType;
				if (TryResolveExprTypeName(*Binary.Left, LeftType)
						&& TryResolveExprTypeName(*Binary.Right, RightType))
				{
					const bool bLeftUnsigned = Runtime::IsUnsignedTypeName(LeftType);
					const bool bRightUnsigned = Runtime::IsUnsignedTypeName(RightType);
					const bool bLeftSignedInt = IsSignedIntegerTypeName(LeftType);
					const bool bRightSignedInt = IsSignedIntegerTypeName(RightType);
					const bool bIsShift = Binary.Op == EBinaryOp::ShiftLeft
							|| Binary.Op == EBinaryOp::ShiftRight;
					if (!bIsShift
							&& ((bLeftUnsigned && bRightSignedInt) || (bRightUnsigned && bLeftSignedInt)))
					{
						ReportError("cannot mix signed and unsigned", Expression.Location);
						return false;
					}
					if (!(bLeftSignedInt || bLeftUnsigned) || !(bRightSignedInt || bRightUnsigned))
					{
						ReportError("bitwise operands must be integers", Expression.Location);
						return false;
					}
				}
			}
			if (Binary.Op == EBinaryOp::Add)
			{
				std::string LeftType;
				std::string RightType;
				if (TryResolveExprTypeName(*Binary.Left, LeftType)
						&& TryResolveExprTypeName(*Binary.Right, RightType))
				{
					const auto IsConcatScalar = [](const std::string& TypeName) -> bool
					{
						return TypeName == "bool"
								|| TypeName == "float"
								|| TypeName == "double"
								|| TypeName == "int"
							|| TypeName == "char"
							|| TypeName == "short"
							|| TypeName == "long"
							|| TypeName == "int64"
							|| Runtime::IsUnsignedTypeName(TypeName);
					};
					const bool bLeftString = LeftType == "string";
					const bool bRightString = RightType == "string";
					const bool bLeftScalar = IsConcatScalar(LeftType);
					const bool bRightScalar = IsConcatScalar(RightType);
					if ((bLeftString && bRightScalar) || (bRightString && bLeftScalar))
					{
						if (!CompileExpression(*Binary.Left))
						{
							return false;
						}
						if (bLeftScalar)
						{
							Emitter.EmitCallBuiltin(EBuiltinId::ToString, 1);
						}
						if (!CompileExpression(*Binary.Right))
						{
							return false;
						}
						if (bRightScalar)
						{
							Emitter.EmitCallBuiltin(EBuiltinId::ToString, 1);
						}
						Emitter.Emit(EOpcode::Add);
						return true;
					}
				}
			}
			if (!CompileExpression(*Binary.Left) || !CompileExpression(*Binary.Right))
			{
				return false;
			}
			switch (Binary.Op)
			{
			case EBinaryOp::Add:
				{
					Emitter.Emit(EOpcode::Add);
					return true;
				}
			case EBinaryOp::Subtract:
				{
					Emitter.Emit(EOpcode::Sub);
					return true;
				}
			case EBinaryOp::Multiply:
				{
					Emitter.Emit(EOpcode::Mul);
					return true;
				}
			case EBinaryOp::Divide:
				{
					Emitter.Emit(EOpcode::Div);
					return true;
				}
			case EBinaryOp::Modulo:
				{
					Emitter.Emit(EOpcode::Mod);
					return true;
				}
			case EBinaryOp::Equal:
				{
					Emitter.Emit(EOpcode::Eq);
					return true;
				}
			case EBinaryOp::NotEqual:
				{
					Emitter.Emit(EOpcode::Ne);
					return true;
				}
			case EBinaryOp::Less:
				{
					Emitter.Emit(EOpcode::Lt);
					return true;
				}
			case EBinaryOp::LessEqual:
				{
					Emitter.Emit(EOpcode::Le);
					return true;
				}
			case EBinaryOp::Greater:
				{
					Emitter.Emit(EOpcode::Gt);
					return true;
				}
			case EBinaryOp::GreaterEqual:
				{
					Emitter.Emit(EOpcode::Ge);
					return true;
				}
			case EBinaryOp::BitAnd:
				{
					Emitter.Emit(EOpcode::BitAnd);
					return true;
				}
			case EBinaryOp::BitOr:
				{
					Emitter.Emit(EOpcode::BitOr);
					return true;
				}
			case EBinaryOp::BitXor:
				{
					Emitter.Emit(EOpcode::BitXor);
					return true;
				}
			case EBinaryOp::ShiftLeft:
				{
					Emitter.Emit(EOpcode::Shl);
					return true;
				}
			case EBinaryOp::ShiftRight:
				{
					Emitter.Emit(EOpcode::Shr);
					return true;
				}
			default:
				{
					ReportError("unsupported binary operator", Expression.Location);
					return false;
				}
			}
		}
