// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

			bool TryEmitMemberOperatorCall(
			    const FUserTypeInfo& OwnerType,
			    const std::string& OwnerTypeName,
			    const char* MethodName,
			    const std::string& ArgType,
			    const FExpr& SelfExpr,
			    const FExpr& ArgExpr,
			    const bool bReceiverFirst,
			    const bool bNegate = false)
			{
				const FUserTypeInfo* DeclaringType = nullptr;
				const FMethodDecl* Method = ArgType.empty() ? nullptr
				                                            : ResolveMethod(OwnerType, MethodName, {ArgType}, &DeclaringType);
				if (!Method || Method->Parameters.size() != 1)
				{
					return false;
				}
				// Convert using the selected declaration's instantiated owner, including
				// inherited generic operators. Capture before evaluating the next operand.
				const std::string ParamType = SubstForUserType(
				    *DeclaringType, Method->Parameters.front().TypeName);
				const auto MakeOperandName = [this](std::string Name)
				{
					while (LocalTypes.find(Name) != LocalTypes.end())
					{
						Name += '_';
					}
					return Name;
				};
				const std::string LeftOperand = MakeOperandName("__vs_op_left");
				const std::string RightOperand = MakeOperandName("__vs_op_right");
				if (bNegate)
				{
					Out << "!(";
				}
				const std::string& Receiver = bReceiverFirst ? LeftOperand : RightOperand;
				const std::string& Argument = bReceiverFirst ? RightOperand : LeftOperand;
				const bool bSelfIsThis = SelfExpr.Kind == EExprKind::This || (SelfExpr.Kind == EExprKind::Identifier && static_cast<const FIdentifierExpr&>(SelfExpr).Name == "this");
				const bool bValueReceiver = (OwnerType.bIsClass || OwnerType.bIsInterface) && !IsDirectObjectReceiver(SelfExpr, bSelfIsThis);
				Out << "([&]() { ";
				if (bReceiverFirst)
				{
					Out << (bValueReceiver ? "auto " : "auto&& ") << Receiver << " = (";
					EmitExpr(SelfExpr);
					Out << "); auto " << Argument << " = ";
					EmitCoercedValue(ParamType, ArgExpr);
				}
				else
				{
					Out << "auto " << Argument << " = ";
					EmitCoercedValue(ParamType, ArgExpr);
					Out << (bValueReceiver ? "; auto " : "; auto&& ") << Receiver << " = (";
					EmitExpr(SelfExpr);
					Out << ')';
				}
				Out << "; return ";
				if (TypeNeedsOopLayout(OwnerType))
				{
					const bool bArrow = OwnerType.bIsClass || OwnerType.bIsInterface;
					if (bArrow && !IsDirectObjectReceiver(SelfExpr, bSelfIsThis))
					{
						bNeedsMemory = true;
						Out << "VsRequireObject(" << Receiver << ").";
					}
					else
					{
						Out << '(' << Receiver << ").";
					}
					Out << SanitizeSymbolName(MethodName) << '(';
					Out << Argument;
					Out << ')';
				}
				else
				{
					Out << MangleMethod(OwnerTypeName, MethodName) << '(';
					Out << Receiver << ", " << Argument;
					Out << ')';
				}
				Out << "; }())";
				if (bNegate)
				{
					Out << ')';
				}
				return true;
			}

			// operator+/==/...: left-member, right-member, free; != falls back to !(==); relational mirrors.
			bool TryEmitOperatorOverloadBinary(
			    const FBinaryExpr& Binary,
			    const std::string& LeftType,
			    const std::string& RightType)
			{
				const auto LeftIt = UserTypes.find(LeftType);
				const auto RightIt = UserTypes.find(RightType);
				const bool bLeftUser = LeftIt != UserTypes.end();
				const bool bRightUser = RightIt != UserTypes.end();
				if (!bLeftUser && !bRightUser)
				{
					return false;
				}
				const char* const OpName = BinaryOpOperatorName(Binary.Op);
				if (!OpName)
				{
					return false;
				}

				const auto MakeOperandName = [this](std::string Name)
				{
					while (LocalTypes.find(Name) != LocalTypes.end())
					{
						Name += '_';
					}
					return Name;
				};
				const std::string LeftOperand = MakeOperandName("__vs_op_left");
				const std::string RightOperand = MakeOperandName("__vs_op_right");
				auto EmitFreeCall = [this, &Binary, &LeftOperand, &RightOperand](
				                        const std::string& Callee,
				                        const bool bSwapArgs)
				{
					// C++ function argument order is unspecified. Capture each value
					// in script order before the next operand can mutate its storage.
					Out << "([&]() { auto " << LeftOperand << " = ";
					EmitExpr(*Binary.Left);
					Out << "; auto " << RightOperand << " = ";
					EmitExpr(*Binary.Right);
					Out << "; return " << Callee << '(' << (bSwapArgs ? RightOperand : LeftOperand)
					    << ", " << (bSwapArgs ? LeftOperand : RightOperand) << "); }())";
				};

				auto EmitMemberCall = [this, &Binary](
				                          const FUserTypeInfo& OwnerType,
				                          const std::string& OwnerTypeName,
				                          const char* MethodName,
				                          const std::string& ArgType,
				                          const FExpr& SelfExpr,
				                          const FExpr& ArgExpr,
				                          const bool bNegate) -> bool
				{
					return TryEmitMemberOperatorCall(OwnerType, OwnerTypeName, MethodName,
					    ArgType, SelfExpr, ArgExpr, &SelfExpr == Binary.Left.get(), bNegate);
				};

				const auto TryOp = [&](const char* const Name, const bool bNegate) -> bool
				{
					if (bLeftUser && EmitMemberCall(LeftIt->second, LeftType, Name, RightType, *Binary.Left, *Binary.Right, bNegate))
					{
						return true;
					}
					if (bRightUser && EmitMemberCall(RightIt->second, RightType, Name, LeftType, *Binary.Right, *Binary.Left, bNegate))
					{
						return true;
					}
					{
						const std::vector<std::string> FreeArgTypes{LeftType, RightType};
						if (const FFunctionDecl* Function =
						        ResolveConcreteOverload(Name, FreeArgTypes, &Binary.Location))
						{
							if (bNegate)
							{
								Out << "!(";
							}
							EmitFreeCall(EmitFunctionCppName(*Function), false);
							if (bNegate)
							{
								Out << ')';
							}
							return true;
						}
					}
					{
						const std::vector<std::string> FreeArgTypes{RightType, LeftType};
						if (const FFunctionDecl* Function =
						        ResolveConcreteOverload(Name, FreeArgTypes, &Binary.Location))
						{
							if (bNegate)
							{
								Out << "!(";
							}
							EmitFreeCall(EmitFunctionCppName(*Function), true);
							if (bNegate)
							{
								Out << ')';
							}
							return true;
						}
					}
					return false;
				};

				if (TryOp(OpName, false))
				{
					return true;
				}
				if (Binary.Op == EBinaryOp::NotEqual && TryOp("operator==", true))
				{
					return true;
				}
				if (const std::optional<EBinaryOp> Mirror = MirrorBinaryOp(Binary.Op))
				{
					if (const char* const MirrorName = BinaryOpOperatorName(*Mirror))
					{
						// a > b via b.operator<(a) / free(b, a), matching VM right-first lookup.
						if (bRightUser && EmitMemberCall(RightIt->second, RightType, MirrorName, LeftType, *Binary.Right, *Binary.Left, false))
						{
							return true;
						}
						{
							const std::vector<std::string> FreeArgTypes{RightType, LeftType};
							if (const FFunctionDecl* Function =
							        ResolveConcreteOverload(MirrorName, FreeArgTypes, &Binary.Location))
							{
								EmitFreeCall(EmitFunctionCppName(*Function), true);
								return true;
							}
						}
					}
				}
				return false;
			}

			bool TryEmitOperatorOverloadUnary(
			    const FUnaryExpr& Unary,
			    const std::string& OperandType)
			{
				const auto TypeIt = UserTypes.find(OperandType);
				if (TypeIt == UserTypes.end())
				{
					return false;
				}
				const char* const OpName = UnaryOpOperatorName(Unary.Op);
				if (!OpName)
				{
					return false;
				}
				const std::vector<std::string> NoArgs;
				if (ResolveMethod(TypeIt->second, OpName, NoArgs))
				{
					if (TypeNeedsOopLayout(TypeIt->second))
					{
						const bool bArrow =
						    TypeIt->second.bIsClass || TypeIt->second.bIsInterface;
						const bool bOperandIsThis = Unary.Operand->Kind == EExprKind::This || (Unary.Operand->Kind == EExprKind::Identifier && static_cast<const FIdentifierExpr&>(*Unary.Operand).Name == "this");
						if (bArrow && !IsDirectObjectReceiver(*Unary.Operand, bOperandIsThis))
						{
							bNeedsMemory = true;
							Out << "VsRequireObject(";
							EmitExpr(*Unary.Operand);
							Out << ").";
						}
						else
						{
							Out << '(';
							EmitExpr(*Unary.Operand);
							Out << ").";
						}
						Out << SanitizeSymbolName(OpName) << "()";
						return true;
					}
					Out << MangleMethod(OperandType, OpName) << '(';
					EmitExpr(*Unary.Operand);
					Out << ')';
					return true;
				}
				if (const FFunctionDecl* Function = ResolveConcreteOverload(
				        OpName, {OperandType}, &Unary.Location))
				{
					Out << EmitFunctionCppName(*Function) << '(';
					EmitExpr(*Unary.Operand);
					Out << ')';
					return true;
				}
				return false;
			}

			void EmitBinaryOp(const EBinaryOp Op)
			{
				switch (Op)
				{
				case EBinaryOp::Add:
					{
						Out << " + ";
						return;
					}
				case EBinaryOp::Subtract:
					{
						Out << " - ";
						return;
					}
				case EBinaryOp::Multiply:
					{
						Out << " * ";
						return;
					}
				case EBinaryOp::Divide:
					{
						Out << " / ";
						return;
					}
				case EBinaryOp::Modulo:
					{
						Out << " % ";
						return;
					}
				case EBinaryOp::Equal:
					{
						Out << " == ";
						return;
					}
				case EBinaryOp::NotEqual:
					{
						Out << " != ";
						return;
					}
				case EBinaryOp::Less:
					{
						Out << " < ";
						return;
					}
				case EBinaryOp::LessEqual:
					{
						Out << " <= ";
						return;
					}
				case EBinaryOp::Greater:
					{
						Out << " > ";
						return;
					}
				case EBinaryOp::GreaterEqual:
					{
						Out << " >= ";
						return;
					}
				case EBinaryOp::LogicalAnd:
					{
						Out << " && ";
						return;
					}
				case EBinaryOp::LogicalOr:
					{
						Out << " || ";
						return;
					}
				case EBinaryOp::BitAnd:
					{
						Out << " & ";
						return;
					}
				case EBinaryOp::BitOr:
					{
						Out << " | ";
						return;
					}
				case EBinaryOp::BitXor:
					{
						Out << " ^ ";
						return;
					}
				case EBinaryOp::ShiftLeft:
					{
						Out << " << ";
						return;
					}
				case EBinaryOp::ShiftRight:
					{
						Out << " >> ";
						return;
					}
				case EBinaryOp::In:
					return;
				default:
					{
						break;
					}
				}
				Fail("AOT: unknown binary operator");
			}

			bool IsSignedIntScriptType(const std::string& RawTypeName) const
			{
				const std::string TypeName = ResolveValueType(RawTypeName);
				return TypeName == "int" || TypeName == "char" || TypeName == "short" || TypeName == "long" || TypeName == "int64";
			}

			bool IsUnsignedIntScriptType(const std::string& RawTypeName) const
			{
				const std::string TypeName = ResolveValueType(RawTypeName);
				return Runtime::IsUnsignedTypeName(TypeName);
			}

			std::string PromoteNumericScriptTypes(
			    const std::string& RawLeftType,
			    const std::string& RawRightType) const
			{
				const std::string LeftType =
				    ResolveValueType(RawLeftType);
				const std::string RightType =
				    ResolveValueType(RawRightType);
				if (LeftType == "double" || RightType == "double")
				{
					return "double";
				}
				if (LeftType == "float" || RightType == "float")
				{
					return "float";
				}
				const bool bLeftSigned = IsSignedIntScriptType(LeftType);
				const bool bRightSigned = IsSignedIntScriptType(RightType);
				const bool bLeftUnsigned = IsUnsignedIntScriptType(LeftType);
				const bool bRightUnsigned = IsUnsignedIntScriptType(RightType);
				if ((bLeftSigned && bRightUnsigned) || (bLeftUnsigned && bRightSigned))
				{
					return {};
				}
				if (bLeftUnsigned && bRightUnsigned)
				{
					return LeftType == "uint64" || RightType == "uint64"
					    ? "uint64"
					    : "unsigned int";
				}
				if (bLeftSigned && bRightSigned)
				{
					return LeftType == "int64" || RightType == "int64"
					    ? "int64"
					    : "int";
				}
				return {};
			}

			bool TryEmitCheckedIntegerBinary(
			    const FBinaryExpr& Binary,
			    const std::string& LeftType,
			    const std::string& RightType)
			{
				if (Binary.Op == EBinaryOp::ShiftLeft || Binary.Op == EBinaryOp::ShiftRight)
				{
					const std::string EffectiveLeft =
					    ResolveAliasChain(ApplyTypeSubst(LeftType));
					if (!(IsSignedIntScriptType(EffectiveLeft) || IsUnsignedIntScriptType(EffectiveLeft)) || !(IsSignedIntScriptType(RightType) || IsUnsignedIntScriptType(RightType)))
					{
						return false;
					}
					const std::string PromotedLeft =
					    EffectiveLeft == "int64" || EffectiveLeft == "uint64"
					    ? EffectiveLeft
					    : IsUnsignedIntScriptType(EffectiveLeft)
					    ? "unsigned int"
					    : "int";
					Out << (Binary.Op == EBinaryOp::ShiftLeft
					        ? "(VsCheckedShiftLeft("
					        : "(VsCheckedShiftRight(");
					Out << "static_cast<" << MapType(PromotedLeft) << ">(";
					EmitExpr(*Binary.Left);
					Out << "), ";
					EmitExpr(*Binary.Right);
					Out << "))";
					return true;
				}
				const bool bBothSigned =
				    IsSignedIntScriptType(LeftType) && IsSignedIntScriptType(RightType);
				const bool bBothUnsigned =
				    IsUnsignedIntScriptType(LeftType) && IsUnsignedIntScriptType(RightType);
				const std::string PromotedType =
				    PromoteNumericScriptTypes(LeftType, RightType);
				const bool bFloating = PromotedType == "float" || PromotedType == "double";
				if (!bBothSigned && !bBothUnsigned && !(bFloating && Binary.Op == EBinaryOp::Divide))
				{
					return false;
				}
				const char* CheckedFn = nullptr;
				switch (Binary.Op)
				{
				case EBinaryOp::Add:
					{
						CheckedFn = "VsCheckedAdd";
						break;
					}
				case EBinaryOp::Subtract:
					{
						CheckedFn = "VsCheckedSub";
						break;
					}
				case EBinaryOp::Multiply:
					{
						CheckedFn = "VsCheckedMul";
						break;
					}
				case EBinaryOp::Divide:
					{
						CheckedFn = "VsCheckedDiv";
						break;
					}
				case EBinaryOp::Modulo:
					{
						CheckedFn = "VsCheckedMod";
						break;
					}
				default:
					return false;
				}
				if (!bBothSigned && Binary.Op != EBinaryOp::Divide && Binary.Op != EBinaryOp::Modulo)
				{
					return false;
				}
				if (PromotedType.empty())
				{
					return false;
				}
				const std::string CppType = MapType(PromotedType);
				Out << '(' << CheckedFn << '(';
				Out << "static_cast<" << CppType << ">(";
				EmitExpr(*Binary.Left);
				Out << "), static_cast<" << CppType << ">(";
				EmitExpr(*Binary.Right);
				Out << ")))";
				return true;
			}

			void EmitResolvedCallArgs(
			    const std::vector<std::unique_ptr<FExpr>>& Arguments,
			    const std::vector<std::string>* ParamTypeHints = nullptr)
			{
				EmitResolvedCallArgs(
				    FExprArgumentView(nullptr, &Arguments), ParamTypeHints);
			}

			void EmitResolvedCallArgs(
			    const FExprArgumentView Arguments,
			    const std::vector<std::string>* ParamTypeHints = nullptr)
			{
				for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
				{
					if (Index > 0)
					{
						Out << ", ";
					}
					if (ParamTypeHints && Index < ParamTypeHints->size() && !(*ParamTypeHints)[Index].empty())
					{
						EmitCoercedValue((*ParamTypeHints)[Index], *Arguments[Index]);
					}
					else
					{
						EmitExpr(*Arguments[Index]);
					}
				}
			}

			// Fill default params / pack trailing variadic into std::vector at the call site.
			void EmitCallArgsForParams(
			    const std::vector<FFunctionParam>& Parameters,
			    const std::vector<std::unique_ptr<FExpr>>& Arguments)
			{
				EmitCallArgsForParams(
				    Parameters, FExprArgumentView(nullptr, &Arguments));
			}

			void EmitCallArgsForParams(
			    const std::vector<FFunctionParam>& Parameters,
			    const FExprArgumentView Arguments,
			    const std::vector<std::string>* ParamTypeHints = nullptr)
			{
				const auto ParamType = [&](const std::size_t Index) -> const std::string&
				{
					return ParamTypeHints && Index < ParamTypeHints->size()
					    ? (*ParamTypeHints)[Index]
					    : Parameters[Index].TypeName;
				};
				std::size_t FixedCount = Parameters.size();
				bool bVariadic = false;
				if (!Parameters.empty() && Parameters.back().bIsVariadic)
				{
					bVariadic = true;
					FixedCount = Parameters.size() - 1;
				}
				for (std::size_t Index = 0; Index < FixedCount; ++Index)
				{
					if (Index > 0)
					{
						Out << ", ";
					}
					if (Index < Arguments.size())
					{
						if (Parameters[Index].bIsRef)
						{
							EmitCoercedReferenceArgument(ParamType(Index), *Arguments[Index]);
						}

						else
						{
							SetSequencedValueArgument(*Arguments[Index]);
							EmitCoercedValue(ParamType(Index), *Arguments[Index]);
						}
					}
					else if (Parameters[Index].DefaultValue)
					{
						EmitCoercedValue(ParamType(Index), *Parameters[Index].DefaultValue);
					}
					else
					{
						Fail("AOT: missing argument for parameter '" + Parameters[Index].Name + "'");
						return;
					}
				}
				if (bVariadic)
				{
					if (FixedCount > 0)
					{
						Out << ", ";
					}
					bNeedsVector = true;
					Out << "std::vector<" << MapType(ParamType(Parameters.size() - 1)) << ">{";
					for (std::size_t Index = FixedCount; Index < Arguments.size(); ++Index)
					{
						if (Index > FixedCount)
						{
							Out << ", ";
						}
						SetSequencedValueArgument(*Arguments[Index]);
						EmitCoercedValue(ParamType(Parameters.size() - 1), *Arguments[Index]);
					}
					Out << '}';
				}
			}

			void EmitCallArgsForGenericParams(
			    const FFunctionDecl& Generic,
			    const std::vector<std::string>& TypeArgs,
			    const FExprArgumentView Arguments)
			{
				std::unordered_map<std::string, std::string> Bindings;
				for (std::size_t Index = 0; Index < Generic.TypeParams.size(); ++Index)
				{
					Bindings.emplace(Generic.TypeParams[Index], ApplyTypeSubst(TypeArgs[Index]));
				}
				std::vector<std::string> ParamTypes;
				for (const FFunctionParam& Param : Generic.Parameters)
				{
					ParamTypes.push_back(SubstituteWithMap(Param.TypeName, Bindings));
				}
				// Keep argument expressions in their caller's substitution scope.
				EmitCallArgsForParams(Generic.Parameters, Arguments, &ParamTypes);
			}

			void EmitCallArgsForUserTypeParams(
			    const FUserTypeInfo& Type,
			    const std::vector<FFunctionParam>& Parameters,
			    const std::vector<std::unique_ptr<FExpr>>& Arguments)
			{
				TValueScopeRestore<std::unordered_map<std::string, std::string>> SubstScope(
				    TypeSubst);
				PushTypeSubstForUserType(Type);
				EmitCallArgsForParams(Parameters, Arguments);
			}

			void EmitCallArgsForUserTypeParams(
			    const FUserTypeInfo& Type,
			    const std::vector<FFunctionParam>& Parameters,
			    const FExprArgumentView Arguments)
			{
				TValueScopeRestore<std::unordered_map<std::string, std::string>> SubstScope(
				    TypeSubst);
				PushTypeSubstForUserType(Type);
				EmitCallArgsForParams(Parameters, Arguments);
			}

			bool TryEmitExtensionMethodCall(
			    const FMethodCallExpr& Call,
			    const std::string& ObjectType)
			{
				std::vector<std::string> TypeArgs;
				bool bGeneric = false;
				const FFunctionDecl* Function = ResolveExtensionOverload(
				    Call, ObjectType, TypeArgs, bGeneric);
				if (!Function)
				{
					return false;
				}
				Out << EmitFunctionCppName(*Function);
				if (bGeneric)
				{
					if (!HasCollectedGenericCall(*Function, TypeArgs))
					{
						Fail("AOT: missing specialization for extension method '" + Call.Method + "' (internal collect)");
						return true;
					}
					Out << '<';
					for (std::size_t Index = 0; Index < TypeArgs.size(); ++Index)
					{
						if (Index > 0)
						{
							Out << ", ";
						}
						Out << MapType(TypeArgs[Index]);
					}
					Out << '>';
				}
				Out << '(';
				const FExprArgumentView Arguments(Call.Object.get(), &Call.Arguments);
				if (bGeneric)
				{
					EmitCallArgsForGenericParams(*Function, TypeArgs, Arguments);
				}
				else
				{
					EmitCallArgsForParams(Function->Parameters, Arguments);
				}
				Out << ')';
				return true;
			}

			void EmitCastIntrinsic(const FCallExpr& Call)
			{
				const bool bTryCast = Call.Callee == "try_cast";
				const bool bConstCast = Call.Callee == "const_cast";
				if (Call.TypeArgs.size() != 1 || Call.Arguments.size() != 1)
				{
					Fail("AOT: invalid " + Call.Callee + " intrinsic");
					return;
				}

				const std::string TargetType =
				    ResolveAliasChain(ApplyTypeSubst(Call.TypeArgs.front()));
				const FExpr& Argument = *Call.Arguments.front();
				const bool bNullSource = Argument.Kind == EExprKind::NullptrLiteral;
				const std::string SourceType = bNullSource
				    ? std::string{}
				    : ResolveAliasChain(ApplyTypeSubst(InferExprType(Argument)));
				if (bConstCast)
				{
					const std::string SourceValueType = Runtime::IsReferenceTypeName(SourceType)
					    ? std::string(Runtime::StripReferenceTypeName(SourceType))
					    : SourceType;
					if (MapType(SourceValueType) != MapType(TargetType))
					{
						Fail(
						    "AOT: const_cast cannot change type from '" + SourceType + "' to '" + TargetType + "'");
						return;
					}
					if (!IsConstExtensionReceiver(Argument))
					{
						Fail("AOT: const_cast source is not const");
						return;
					}
					const bool bThisOperand = Argument.Kind == EExprKind::This || (Argument.Kind == EExprKind::Identifier && static_cast<const FIdentifierExpr&>(Argument).Name == "this");
					const std::string CastType = bThisOperand
					    ? MapBaseCppType(TargetType)
					    : MapType(TargetType);
					Out << "const_cast<" << CastType << "&>(";
					EmitExpr(Argument);
					Out << ')';
					return;
				}
				const auto IsNumericType = [](const std::string& Type)
				{
					return Type == "char" || Type == "int8" || Type == "unsigned char" || Type == "uint8" || Type == "short" || Type == "int16" || Type == "unsigned short" || Type == "uint16" || Type == "int" || Type == "int32" || Type == "unsigned" || Type == "unsigned int" || Type == "uint32" || Type == "long" || Type == "int64" || Type == "unsigned long" || Type == "uint64" || Type == "float" || Type == "double";
				};
				const FUserTypeInfo* const TargetInfo = ResolveUserTypeForLookup(TargetType);
				const FUserTypeInfo* const SourceInfo = SourceType.empty()
				    ? nullptr
				    : ResolveUserTypeForLookup(SourceType);
				const bool bTargetReference = TargetInfo && (TargetInfo->bIsClass || TargetInfo->bIsInterface);
				const bool bSourceReference = SourceInfo && (SourceInfo->bIsClass || SourceInfo->bIsInterface);
				const bool bTargetEnum = ScopedEnumTypeNames.find(TargetType) != ScopedEnumTypeNames.end();
				const bool bSourceEnum = ScopedEnumTypeNames.find(SourceType) != ScopedEnumTypeNames.end();

				// A checked cast is only required for a down/cross cast. Exact values,
				// implicit numeric conversions, up-casts, and struct-to-interface boxing
				// already use the ordinary AOT coercion path.
				if (!bTryCast && !bTargetEnum && !bSourceEnum && !(bSourceReference && bTargetReference && !IsAotSubtypeOf(SourceType, TargetType)))
				{
					EmitCoercedValue(TargetType, Argument, true);
					return;
				}

				const std::string TargetCppType = MapType(TargetType);
				const std::string ResultCppType = bTryCast
				    ? MapType("optional<" + TargetType + ">")
				    : TargetCppType;
				Out << "([&]() -> " << ResultCppType << " { auto _vs_value = ";
				EmitExpr(Argument);
				Out << "; ";

				if (bTryCast && bNullSource && bTargetReference)
				{
					Out << TargetCppType << " _vs_cast{}; return " << ResultCppType
					    << "(std::move(_vs_cast)); }())";
					return;
				}

				if (!SourceType.empty() && SourceType == TargetType)
				{
					if (bTryCast)
					{
						Out << "return " << ResultCppType
						    << "(std::move(_vs_value)); }())";
					}
					else
					{
						Out << "return _vs_value; }())";
					}
					return;
				}

				if (bTryCast && bSourceEnum && bTargetEnum)
				{
					// Strong enums keep their full type identity. The VM rejects a direct
					// conversion between distinct enum types; callers must explicitly cast
					// through an underlying integer when that is intended.
					Out << "(void)_vs_value; return " << ResultCppType << "{}; }())";
					return;
				}

				if (bSourceReference && bTargetReference)
				{
					const std::string TargetBaseCpp = MapBaseCppType(TargetType);
					if (IsAotSubtypeOf(SourceType, TargetType))
					{
						Out << TargetCppType << " _vs_cast = _vs_value; ";
					}
					else
					{
						Out << "if (!_vs_value) return ";
						if (bTryCast)
						{
							// Null is a valid value of every script reference type.
							Out << ResultCppType << '(' << TargetCppType << "{}); ";
						}
						else
						{
							Out << TargetCppType << "{}; ";
						}
						Out << "auto _vs_cast = std::dynamic_pointer_cast<"
						    << TargetBaseCpp << ">(_vs_value); ";
						if (bTryCast)
						{
							Out << "if (!_vs_cast) return " << ResultCppType << "{}; ";
						}
						else
						{
							Out << "if (!_vs_cast) VsScriptFail(\"cast failed\"); ";
						}
					}
					Out << "return ";
					if (bTryCast)
					{
						Out << ResultCppType << "(std::move(_vs_cast))";
					}
					else
					{
						Out << "_vs_cast";
					}
					Out << "; }())";
					return;
				}

				if (bTargetEnum)
				{
					const auto UnderlyingIt = EnumUnderlyingTypeNames.find(TargetType);
					const std::string UnderlyingType = UnderlyingIt == EnumUnderlyingTypeNames.end()
					    ? std::string{"int"}
					    : ResolveAliasChain(ApplyTypeSubst(UnderlyingIt->second));
					const std::string UnderlyingCppType = MapType(UnderlyingType);
					Out << UnderlyingCppType << " _vs_underlying{}; ";
					if (bTryCast)
					{
						Out << "if (!VsTryNumericCast<" << UnderlyingCppType
						    << ">(_vs_value, _vs_underlying)) return " << ResultCppType << "{}; ";
					}
					else
					{
						Out << "_vs_underlying = VsCheckedNumericCast<" << UnderlyingCppType
						    << ">(_vs_value); ";
					}
					if (UnderlyingType == "uint64" || UnderlyingType == "unsigned long")
					{
						Out << "if (_vs_underlying > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) ";
						if (bTryCast)
						{
							Out << "return " << ResultCppType << "{}; ";
						}
						else
						{
							Out << "VsScriptFail(\"numeric conversion overflow\"); ";
						}
					}
					Out << TargetCppType << " _vs_cast = static_cast<" << TargetCppType
					    << ">(_vs_underlying); return ";
					if (bTryCast)
					{
						Out << ResultCppType << "(std::move(_vs_cast))";
					}
					else
					{
						Out << "_vs_cast";
					}
					Out << "; }())";
					return;
				}

				if ((IsNumericType(TargetType) && (IsNumericType(SourceType) || bSourceEnum || SourceType == "bool")))
				{
					Out << TargetCppType << " _vs_cast{}; ";
					if (bTryCast)
					{
						Out << "if (!VsTryNumericCast<" << TargetCppType
						    << ">(_vs_value, _vs_cast)) return " << ResultCppType
						    << "{}; return " << ResultCppType << "(std::move(_vs_cast)); }())";
					}
					else
					{
						Out << "_vs_cast = VsCheckedNumericCast<" << TargetCppType
						    << ">(_vs_value); return _vs_cast; }())";
					}
					return;
				}

				// The compiler has already established that try_cast is a legal primitive
				// conversion. Different non-numeric primitive domains simply do not match.
				if (bTryCast)
				{
					Out << "(void)_vs_value; return " << ResultCppType << "{}; }())";
					return;
				}
				Fail("AOT: unsupported cast conversion from '" + SourceType + "' to '" + TargetType + "'");
				Out << "return " << TargetCppType << "{}; }())";
			}

			void EmitCallExpr(const FCallExpr& Call)
			{
				const FExprArgumentView EffectiveArguments = Call.GetArguments();
				if (Call.Callee == "__numeric_type" && Call.TypeArgs.size() == 1)
				{
					Out << "VsNumericType<" << MapType(ApplyTypeSubst(Call.TypeArgs[0])) << ">()";
					return;
				}
				static constexpr std::pair<std::string_view, std::string_view> FoundationCalls[] = {
				    {"__string_starts_with", "VsStringStarts"}, {"__string_ends_with", "VsStringEnds"},
				    {"__string_last_find", "VsStringLastFind"}, {"__string_repeat", "VsStringRepeat"},
				    {"__string_replace", "VsStringReplace"}, {"__string_trim", "VsStringTrim"},
				    {"__array_reverse", "VsArrayReverse"}, {"__array_rotate", "VsArrayRotate"},
				    {"__array_shuffle", "VsArrayShuffle"}, {"__random_next_uint", "VsRandomNext"},
				    {"__random_index", "VsRandomIndex"}};
				for (const auto& Entry : FoundationCalls)
				{
					if (Call.Callee == Entry.first)
					{
						if (Entry.first.substr(0, 8) == "__array_" || Entry.first.substr(0, 9) == "__random_")
						{
							bNeedsVector = true;
						}
						Out << Entry.second << '(';
						EmitResolvedCallArgs(EffectiveArguments);
						Out << ')';
						return;
					}
				}
				if (EmitAttributeFactory(Call)) return;
				if ((Call.Callee == "__std_spawn" || Call.Callee == "__std_spawn_task") &&
				    !EffectiveArguments.empty() &&
				    ResolveAliasChain(ApplyTypeSubst(InferExprType(*EffectiveArguments[0]))) != "string")
				{
					Fail("AOT: callable task entry is not supported by the C++ backend");
					return;
				}
				if (Call.Callee.empty() && Call.CalleeExpr)
				{
					Out << '(';
					EmitExpr(*Call.CalleeExpr);
					Out << ")(";
					std::vector<std::string> FnParamTypes;
					std::string FnReturnType;
					const std::string CalleeType =
					    ResolveAliasChain(ApplyTypeSubst(InferExprType(*Call.CalleeExpr)));
					if (Runtime::ParseFunctionType(CalleeType, FnParamTypes, FnReturnType))
					{
						EmitResolvedCallArgs(Call.Arguments, &FnParamTypes);
					}
					else
					{
						EmitResolvedCallArgs(Call.Arguments);
					}
					Out << ')';
					return;
				}
				if ((Call.Callee == "cast" || Call.Callee == "try_cast" || Call.Callee == "const_cast") && !Call.TypeArgs.empty())
				{
					EmitCastIntrinsic(Call);
					return;
				}
				if (!IsFormatFamilyIntrinsicCall(Call))
				{
					const auto LocalIt = LocalTypes.find(Call.Callee);
					const FVarDeclStmt* Global = nullptr;
					std::string VariableType;
					std::string VariableCppName;
					if (LocalIt != LocalTypes.end())
					{
						VariableType = LocalIt->second;
						VariableCppName = Call.Callee;
					}
					else if ((Global = ResolveGlobalVariable(Call.Callee, &Call.Location)) != nullptr)
					{
						VariableType = Global->TypeName;
						VariableCppName = MangleGlobal(*Global) + "()";
					}
					if (!VariableType.empty())
					{
						const std::string VarType =
						    ResolveAliasChain(ApplyTypeSubst(VariableType));
						std::vector<std::string> AttributeArgs;
						if (bHasStandardAttribute &&
						    TryParseSoftBuiltinType(VarType, "attribute", AttributeArgs))
						{
							Out << VariableCppName << "()";
							return;
						}
						std::vector<std::string> FnParamTypes;
						std::string FnReturnType;
						if (!Runtime::ParseFunctionType(
						        VarType,
						        FnParamTypes,
						        FnReturnType))
						{
							Fail("AOT: '" + Call.Callee + "' is not a function value (type '" + VarType + "')");
							return;
						}
						EmitNamedCallable(Call, VariableCppName);
						Out << '(';
						EmitResolvedCallArgs(Call.Arguments, &FnParamTypes);
						Out << ')';
						return;
					}
				}
				const bool bStandardSpawn = Call.bStandardLibraryQualified && Call.Callee == "__std_spawn";
				if (bStandardSpawn || Call.Callee == "__spawn" || Call.Callee == "__spawn_result")
				{
					if (Call.Arguments.size() != 2)
					{
						Fail("AOT: spawn expects name and channel");
						return;
					}
					bNeedsThread = true;
					bNeedsChannel = true;
					bNeedsMemory = true;
					const bool bResultTask = Call.Callee == "__spawn_result";
					const std::string ChType =
					    ResolveAliasChain(ApplyTypeSubst(InferExprType(*Call.Arguments[1])));
					std::string Elem;
					if (!TryParseChannelType(ChType, Elem))
					{
						Fail("AOT: spawn second argument must be channel<T>");
						return;
					}
					const auto EmitSpawnWorkerCall = [&](const FFunctionDecl& Worker)
					{
						Out << (bResultTask
						        ? "VsSpawnResultWithChannelCloser([=]() { "
						        : "VsSpawnWithChannelCloser([=]() { ");
						EmitValueExpr(*Call.Arguments[1]);
						Out << "->close(); }, [=]() { " << EmitFunctionCppName(Worker) << '(';
						EmitValueExpr(*Call.Arguments[1]);
						Out << "); })";
					};
					if (const auto* NameLit =
					        dynamic_cast<const FStringLiteralExpr*>(Call.Arguments[0].get()))
					{
						const FFunctionDecl* Worker =
						    ResolveSpawnWorker(NameLit->Value, Elem);
						if (!Worker)
						{
							Fail(
							    "AOT: spawn target not found or ambiguous: " + NameLit->Value);
							return;
						}
						EmitSpawnWorkerCall(*Worker);
						return;
					}
					if (ActiveOpenTypeParams.find(Elem) != ActiveOpenTypeParams.end())
					{
						// The concrete channel element is supplied when the surrounding
						// generic function is instantiated.  Let C++ overload resolution
						// select the matching concrete dispatcher.
						Out << "VsSpawnByNameOrFail(";
					}
					else
					{
						Out << "VsSpawnByNameOrFail_" << SanitizeForMangle(Elem) << "(";
					}
					EmitValueExpr(*Call.Arguments[0]);
					Out << ", \""
					    << EscapeCppString(SpawnCallerModulePath())
					    << "\", ";
					EmitValueExpr(*Call.Arguments[1]);
					Out << ", " << (bResultTask ? "true" : "false") << ')';
					return;
				}
				if ((Call.bStandardLibraryQualified && Call.Callee == "__std_join_task") || Call.Callee == "__join_task")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: join_task expects task id");
						return;
					}
					bNeedsThread = true;
					Out << "VsJoinTaskOrFail(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__join_task_for")
				{
					if (Call.Arguments.size() != 2)
					{
						Fail("AOT: __join_task_for expects task id and timeout");
						return;
					}
					bNeedsThread = true;
					Out << "VsJoinTaskForOrFail(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ", ";
					EmitValueExpr(*Call.Arguments[1]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__task_done")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: __task_done expects task id");
						return;
					}
					bNeedsThread = true;
					Out << "VsTaskDone(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__cancel_requested")
				{
					if (!Call.Arguments.empty())
					{
						Fail("AOT: __cancel_requested expects no arguments");
						return;
					}
					bNeedsThread = true;
					Out << "VsCancelRequested()";
					return;
				}
				if (Call.Callee == "__cancel_task")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: __cancel_task expects task id");
						return;
					}
					bNeedsThread = true;
					Out << "VsCancelTask(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "print")
				{
					if (Call.Arguments.empty())
					{
						Fail("AOT: print expects at least one argument");
						return;
					}
					if (Call.Arguments.size() > 1)
					{
						bNeedsVector = true;
						const bool bPassArrayDirect =
						    Call.Arguments.size() == 2 && ResolveAliasChain(ApplyTypeSubst(InferExprType(*Call.Arguments[1]))) == "array<string>";
						if (bPassArrayDirect)
						{
							Out << "VsPrint(VsFormatArray(";
							EmitValueExpr(*Call.Arguments[0]);
							Out << ", ";
							EmitValueExpr(*Call.Arguments[1]);
							Out << "))";
							return;
						}
						Out << "VsPrintFormat(";
						EmitValueExpr(*Call.Arguments[0]);
						for (std::size_t Index = 1; Index < Call.Arguments.size(); ++Index)
						{
							Out << ", ";
							EmitValueExpr(*Call.Arguments[Index]);
						}
						Out << ')';
						return;
					}
					const std::string ArgType = InferExprType(*Call.Arguments[0]);
					if (!ArgType.empty() && UserTypes.find(ArgType) != UserTypes.end())
					{
						// MVP: print TypeName{...} for user struct/class/interface values.
						Out << "(static_cast<void>(";
						EmitValueExpr(*Call.Arguments[0]);
						Out << "), VsPrintTypeName(\"" << EscapeCppString(ArgType) << "\"))";
						return;
					}
					Out << "VsPrint(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "eprint")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: eprint expects exactly one argument");
						return;
					}
					Out << "VsEprint(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "abs")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: abs expects exactly one argument");
						return;
					}
					Out << "VsAbs(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "min" || Call.Callee == "max")
				{
					if (Call.Arguments.size() != 2)
					{
						Fail("AOT: min/max expects exactly two arguments");
						return;
					}
					Out << (Call.Callee == "min" ? "VsMin(" : "VsMax(");
					const std::string PromotedType = PromoteNumericScriptTypes(
					    InferExprType(*Call.Arguments[0]),
					    InferExprType(*Call.Arguments[1]));
					const std::string CppType = MapType(PromotedType);
					if (PromotedType.empty() || CppType.empty())
					{
						Fail("AOT: min/max arguments do not have a common numeric type");
						return;
					}
					Out << "static_cast<" << CppType << ">(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << "), static_cast<" << CppType << ">(";
					EmitValueExpr(*Call.Arguments[1]);
					Out << "))";
					return;
				}
				if (Call.Callee == "exit")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: exit expects exactly one argument");
						return;
					}
					Out << "throw VsExitException(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "range")
				{
					bNeedsVector = true;
					if (Call.Arguments.size() == 1)
					{
						Out << "VsRange(0, ";
						EmitCoercedValue("int64", *Call.Arguments[0]);
						Out << ", 1)";
						return;
					}
					if (Call.Arguments.size() == 2)
					{
						Out << "VsRange(";
						EmitValueExpr(*Call.Arguments[0]);
						Out << ", ";
						EmitValueExpr(*Call.Arguments[1]);
						Out << ", 1)";
						return;
					}
					if (Call.Arguments.size() == 3)
					{
						Out << "VsRange(";
						EmitValueExpr(*Call.Arguments[0]);
						Out << ", ";
						EmitValueExpr(*Call.Arguments[1]);
						Out << ", ";
						EmitValueExpr(*Call.Arguments[2]);
						Out << ')';
						return;
					}
					Fail("AOT: range expects 1..3 arguments");
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "now")
				{
					bNeedsChrono = true;
					Out << "VsNowSeconds()";
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "steady_now")
				{
					bNeedsChrono = true;
					Out << "VsSteadyNowSeconds()";
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "console_write")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: console_write expects exactly one argument");
						return;
					}
					bNeedsConsole = true;
					bNeedsVector = true;
					Out << "VsConsoleWrite(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "console_error")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: console_error expects exactly one argument");
						return;
					}
					bNeedsConsole = true;
					bNeedsVector = true;
					Out << "VsConsoleError(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "console_flush")
				{
					if (!Call.Arguments.empty())
					{
						Fail("AOT: console_flush expects no arguments");
						return;
					}
					bNeedsConsole = true;
					Out << "VsConsoleFlush()";
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "console_clear")
				{
					if (!Call.Arguments.empty())
					{
						Fail("AOT: console_clear expects no arguments");
						return;
					}
					bNeedsConsole = true;
					Out << "VsConsoleClear()";
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "console_is_interactive")
				{
					if (!Call.Arguments.empty())
					{
						Fail("AOT: console_is_interactive expects no arguments");
						return;
					}
					bNeedsConsole = true;
					Out << "VsConsoleIsInteractive()";
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "console_supports_color")
				{
					bNeedsConsole = true;
					Out << "VsConsoleRuntime::Query(std::cout).bColor";
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "console_supports_cursor")
				{
					bNeedsConsole = true;
					Out << "VsConsoleRuntime::Query(std::cout).bCursor";
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "console_supports_clear")
				{
					bNeedsConsole = true;
					Out << "VsConsoleRuntime::Query(std::cout).bClear";
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "console_is_output_terminal")
				{
					bNeedsConsole = true;
					Out << "VsConsoleRuntime::Query(std::cout).bOutputTerminal";
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "console_width")
				{
					bNeedsConsole = true;
					Out << "VsConsoleRuntime::Query(std::cout).Width";
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "console_height")
				{
					bNeedsConsole = true;
					Out << "VsConsoleRuntime::Query(std::cout).Height";
					return;
				}
				if (Call.bStandardLibraryQualified &&
				    (Call.Callee == "console_set_color" || Call.Callee == "console_move_cursor"))
				{
					bNeedsConsole = true;
					const bool bColor = Call.Callee == "console_set_color";
					const auto MakeArgumentName = [this](std::string Name)
					{
						while (LocalTypes.find(Name) != LocalTypes.end())
						{
							Name += '_';
						}

						return Name;
					};
					const std::string FirstArgument = MakeArgumentName("__vs_console_arg0");
					const std::string SecondArgument = MakeArgumentName("__vs_console_arg1");
					const std::string FirstValue = MakeArgumentName("__vs_console_first");
					const std::string SecondValue = MakeArgumentName("__vs_console_second");
					// The VM evaluates both arguments left to right before validating them.
					// Separate statements also preserve side effects on validation failure.
					Out << "([&]() { auto " << FirstArgument << " = std::move(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << "); auto " << SecondArgument << " = std::move(";
					EmitValueExpr(*Call.Arguments[1]);
					const char* Conversion = bColor ? "VsConsoleRuntime::ColorIndex"
					                                : "VsCheckedNumericCast<std::int32_t>";
					Out << "); const auto " << FirstValue << " = " << Conversion << '(' << FirstArgument
					    << "); const auto " << SecondValue << " = " << Conversion << '(' << SecondArgument
					    << "); return VsConsoleRuntime::Execute(std::cout, VsConsoleRuntime::EOperation::"
					    << (bColor ? "SetColor" : "MoveCursor") << ", " << FirstValue << ", " << SecondValue
					    << "); }())";
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "console_reset_style")
				{
					bNeedsConsole = true;
					Out << "VsConsoleRuntime::Execute(std::cout, VsConsoleRuntime::EOperation::ResetStyle";
					Out << ")";
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "console_clear_line")
				{
					bNeedsConsole = true;
					Out << "VsConsoleRuntime::Execute(std::cout, VsConsoleRuntime::EOperation::ClearLine";
					Out << ", VsConsoleRuntime::LineMode(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ")";
					Out << ")";
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "console_set_cursor_visible")
				{
					bNeedsConsole = true;
					Out << "VsConsoleRuntime::Execute(std::cout, VsConsoleRuntime::EOperation::SetCursorVisible";
					Out << ", ";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ")";
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "getenv")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: getenv expects exactly one argument");
						return;
					}
					bNeedsCstdlib = true;
					Out << "VsGetenv(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "setenv")
				{
					if (Call.Arguments.size() != 2)
					{
						Fail("AOT: setenv expects exactly two arguments");
						return;
					}
					bNeedsCstdlib = true;
					Out << "VsSetenv(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ", ";
					EmitValueExpr(*Call.Arguments[1]);
					Out << ')';
					return;
				}
				if (Call.bStandardLibraryQualified && Call.Callee == "sleep")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: sleep expects exactly one argument");
						return;
					}
					bNeedsThread = true;
					bNeedsChrono = true;
					Out << "VsSleepMs(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "to_wstring")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: to_wstring expects exactly one argument");
						return;
					}
					Out << "VsToWString(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__channel_make" || Call.Callee == "channel")
				{
					std::string Elem = "int";
					if (!Call.TypeArgs.empty())
					{
						Elem = Call.TypeArgs[0];
					}
					else if (Call.Callee != "channel" && !Call.Arguments.empty())
					{
						Fail("AOT: __channel_make expects type arg");
						return;
					}
					{
						std::string Parsed;
						if (TryParseChannelType(Call.Callee, Parsed))
						{
							Elem = Parsed;
						}
						std::vector<std::string> ChArgs;
						if (TryParseTypeApplication(Call.Callee, "channel", ChArgs) && ChArgs.size() == 1)
						{
							Elem = ChArgs[0];
						}
					}
					bNeedsChannel = true;
					bNeedsMemory = true;
					Out << "VsMakeChannel<" << MapType(Elem) << ">()";
					return;
				}
				if (Call.Callee == "__channel_send")
				{
					if (Call.Arguments.size() != 2)
					{
						Fail("AOT: __channel_send expects channel and value");
						return;
					}
					bNeedsChannel = true;
					const std::string ChType =
					    ResolveAliasChain(ApplyTypeSubst(InferExprType(*Call.Arguments[0])));
					std::string Elem;
					if (!TryParseChannelType(ChType, Elem))
					{
						Fail("AOT: __channel_send expects channel<T>");
						return;
					}
					Out << "VsChannelSend(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ", ";
					EmitValueExpr(*Call.Arguments[1]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__channel_try_send" || Call.Callee == "__channel_send_for")
				{
					const std::size_t ExpectedArgs =
					    Call.Callee == "__channel_try_send" ? 2 : 3;
					if (Call.Arguments.size() != ExpectedArgs)
					{
						Fail("AOT: channel send helper has invalid arguments");
						return;
					}
					bNeedsChannel = true;
					const std::string ChType =
					    ResolveAliasChain(ApplyTypeSubst(InferExprType(*Call.Arguments[0])));
					std::string Elem;
					if (!TryParseChannelType(ChType, Elem))
					{
						Fail("AOT: channel send helper expects channel<T>");
						return;
					}
					Out << (Call.Callee == "__channel_try_send"
					        ? "VsChannelTrySend("
					        : "VsChannelSendFor(");
					EmitValueExpr(*Call.Arguments[0]);
					Out << ", ";
					EmitValueExpr(*Call.Arguments[1]);
					if (ExpectedArgs == 3)
					{
						Out << ", ";
						EmitValueExpr(*Call.Arguments[2]);
					}
					Out << ')';
					return;
				}
				if (Call.Callee == "__channel_recv")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: __channel_recv expects channel");
						return;
					}
					bNeedsChannel = true;
					const std::string ChType =
					    ResolveAliasChain(ApplyTypeSubst(InferExprType(*Call.Arguments[0])));
					std::string Elem;
					if (!TryParseChannelType(ChType, Elem))
					{
						Fail("AOT: __channel_recv expects channel<T>");
						return;
					}
					Out << "VsChannelRecv<" << MapType(Elem) << ">(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__channel_try_recv" || Call.Callee == "__channel_recv_for")
				{
					const std::size_t ExpectedArgs =
					    Call.Callee == "__channel_try_recv" ? 1 : 2;
					if (Call.Arguments.size() != ExpectedArgs)
					{
						Fail("AOT: channel receive helper has invalid arguments");
						return;
					}
					bNeedsChannel = true;
					bNeedsVector = true;
					const std::string ChType =
					    ResolveAliasChain(ApplyTypeSubst(InferExprType(*Call.Arguments[0])));
					std::string Elem;
					if (!TryParseChannelType(ChType, Elem))
					{
						Fail("AOT: channel receive helper expects channel<T>");
						return;
					}
					Out << (Call.Callee == "__channel_try_recv"
					               ? "VsChannelTryRecv<"
					               : "VsChannelRecvFor<")
					    << MapType(Elem) << ">(";
					EmitValueExpr(*Call.Arguments[0]);
					if (ExpectedArgs == 2)
					{
						Out << ", ";
						EmitValueExpr(*Call.Arguments[1]);
					}
					Out << ')';
					return;
				}
				if (Call.Callee == "__channel_is_closed" || Call.Callee == "__channel_size" || Call.Callee == "__channel_capacity" || Call.Callee == "__channel_set_capacity")
				{
					const std::size_t ExpectedArgs =
					    Call.Callee == "__channel_set_capacity" ? 2 : 1;
					if (Call.Arguments.size() != ExpectedArgs)
					{
						Fail("AOT: channel query/configuration helper has invalid arguments");
						return;
					}
					bNeedsChannel = true;
					bNeedsMemory = true;
					Out << "VsRequireObject(";
					EmitValueExpr(*Call.Arguments[0]);
					if (Call.Callee == "__channel_is_closed")
					{
						Out << ").is_closed()";
					}
					else if (Call.Callee == "__channel_size")
					{
						Out << ").size()";
					}
					else if (Call.Callee == "__channel_capacity")
					{
						Out << ").capacity()";
					}
					else
					{
						Out << ").configure_capacity(";
						EmitValueExpr(*Call.Arguments[1]);
						Out << ')';
					}
					return;
				}
				if (Call.Callee == "__channel_close")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: __channel_close expects channel");
						return;
					}
					bNeedsChannel = true;
					bNeedsMemory = true;
					Out << "VsRequireObject(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ").close()";
					return;
				}
				if (Call.Callee == "len" || Call.Callee == "__array_prepare_sort")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: len expects exactly one argument");
						return;
					}
					Out << "VsCheckedLength((";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ").size())";
					return;
				}
				if (Call.Callee == "to_string")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: to_string expects exactly one argument");
						return;
					}
					Out << "VsStr(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "to_int")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: to_int expects exactly one argument");
						return;
					}
					Out << "VsToInt(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "to_int64")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: to_int64 expects exactly one argument");
						return;
					}
					Out << "VsToInt64(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "to_uint64")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: to_uint64 expects exactly one argument");
						return;
					}
					Out << "VsToUInt64(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "to_double")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: to_double expects exactly one argument");
						return;
					}
					Out << "VsToDouble(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "to_float")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: to_float expects exactly one argument");
						return;
					}
					Out << "VsToFloat(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "to_unsigned")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: to_unsigned expects exactly one argument");
						return;
					}
					Out << "VsToUnsigned(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				// ---- stdlib soft builtins (math.vs / string.vs / io.vs / path.vs)
				if (Call.Callee == "__sqrt" || Call.Callee == "__sin" || Call.Callee == "__cos" || Call.Callee == "__tan" || Call.Callee == "__floor" || Call.Callee == "__ceil" || Call.Callee == "__round" || Call.Callee == "__trunc" || Call.Callee == "__exp" || Call.Callee == "__log" || Call.Callee == "__log2" || Call.Callee == "__asin" || Call.Callee == "__acos")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: " + Call.Callee + " expects exactly one argument");
						return;
					}
					bNeedsCmath = true;
					const char* StdFn = "sqrt";
					if (Call.Callee == "__sin")
					{
						StdFn = "sin";
					}
					else if (Call.Callee == "__cos")
					{
						StdFn = "cos";
					}
					else if (Call.Callee == "__tan")
					{
						StdFn = "tan";
					}
					else if (Call.Callee == "__floor")
					{
						StdFn = "floor";
					}
					else if (Call.Callee == "__ceil")
					{
						StdFn = "ceil";
					}
					else if (Call.Callee == "__round")
					{
						StdFn = "round";
					}
					else if (Call.Callee == "__trunc")
					{
						StdFn = "trunc";
					}
					else if (Call.Callee == "__exp")
					{
						StdFn = "exp";
					}
					else if (Call.Callee == "__log")
					{
						StdFn = "log";
					}
					else if (Call.Callee == "__log2")
					{
						StdFn = "log2";
					}
					else if (Call.Callee == "__asin")
					{
						StdFn = "asin";
					}
					else if (Call.Callee == "__acos")
					{
						StdFn = "acos";
					}
					Out << "std::" << StdFn << "(static_cast<double>(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << "))";
					return;
				}
				if (Call.Callee == "__pow" || Call.Callee == "__atan2")
				{
					if (Call.Arguments.size() != 2)
					{
						Fail("AOT: " + Call.Callee + " expects exactly two arguments");
						return;
					}
					bNeedsCmath = true;
					Out << "std::" << (Call.Callee == "__pow" ? "pow" : "atan2")
					    << "(static_cast<double>(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << "), static_cast<double>(";
					EmitValueExpr(*Call.Arguments[1]);
					Out << "))";
					return;
				}
				if (Call.Callee == "__isfinite" || Call.Callee == "__isnan")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: " + Call.Callee + " expects exactly one argument");
						return;
					}
					bNeedsCmath = true;
					Out << "std::" << (Call.Callee == "__isfinite" ? "isfinite" : "isnan")
					    << "(static_cast<double>(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << "))";
					return;
				}
				if (Call.Callee == "__substr")
				{
					if (Call.Arguments.size() != 3)
					{
						Fail("AOT: __substr expects (string, int64, int64)");
						return;
					}
					Out << "VsSubstr(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ", ";
					EmitValueExpr(*Call.Arguments[1]);
					Out << ", ";
					EmitValueExpr(*Call.Arguments[2]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__find")
				{
					if (Call.Arguments.size() != 2)
					{
						Fail("AOT: __find expects (string, string)");
						return;
					}
					Out << "VsFind(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ", ";
					EmitValueExpr(*Call.Arguments[1]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__char_code")
				{
					if (Call.Arguments.size() != 2)
					{
						Fail("AOT: __char_code expects (string, int64)");
						return;
					}
					Out << "VsCharCode(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ", ";
					EmitValueExpr(*Call.Arguments[1]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__split")
				{
					if (Call.Arguments.size() != 2)
					{
						Fail("AOT: __split expects (string, string)");
						return;
					}
					bNeedsVector = true;
					Out << "VsSplit(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ", ";
					EmitValueExpr(*Call.Arguments[1]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__join")
				{
					if (Call.Arguments.size() != 2)
					{
						Fail("AOT: __join expects (array<string>, string)");
						return;
					}
					bNeedsVector = true;
					Out << "VsJoin(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ", ";
					EmitValueExpr(*Call.Arguments[1]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__to_lower" || Call.Callee == "__to_upper")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: " + Call.Callee + " expects exactly one argument");
						return;
					}
					bNeedsCctype = true;
					Out << (Call.Callee == "__to_lower" ? "VsToLower(" : "VsToUpper(");
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__format")
				{
					if (Call.Arguments.size() != 2)
					{
						Fail("AOT: __format expects (string, array<string>)");
						return;
					}
					bNeedsVector = true;
					Out << "VsFormatArray(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ", ";
					EmitValueExpr(*Call.Arguments[1]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__format_printf")
				{
					if (Call.Arguments.size() != 2)
					{
						Fail("AOT: __format_printf expects (string, array<string>)");
						return;
					}
					bNeedsVector = true;
					Out << "VsFormatPrintfArray(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ", ";
					EmitValueExpr(*Call.Arguments[1]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__read_file")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: __read_file expects exactly one argument");
						return;
					}
					bNeedsFstream = true;
					Out << "VsReadFile(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__write_file")
				{
					if (Call.Arguments.size() != 2)
					{
						Fail("AOT: __write_file expects (string, string)");
						return;
					}
					bNeedsFstream = true;
					Out << "VsWriteFile(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ", ";
					EmitValueExpr(*Call.Arguments[1]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__append_file")
				{
					if (Call.Arguments.size() != 2)
					{
						Fail("AOT: __append_file expects (string, string)");
						return;
					}
					bNeedsFstream = true;
					Out << "VsAppendFile(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ", ";
					EmitValueExpr(*Call.Arguments[1]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__exists")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: __exists expects exactly one argument");
						return;
					}
					bNeedsFilesystem = true;
					Out << "VsExists(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__is_file" || Call.Callee == "__is_directory" || Call.Callee == "__remove_dir")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: filesystem operation expects exactly one argument");
						return;
					}
					bNeedsFilesystem = true;
					Out << (Call.Callee == "__is_file"        ? "VsIsFile("
					        : Call.Callee == "__is_directory" ? "VsIsDirectory("
					                                          : "VsRemoveDir(");
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__delete_file")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: __delete_file expects exactly one argument");
						return;
					}
					bNeedsFilesystem = true;
					Out << "VsDeleteFile(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__mkdir")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: __mkdir expects exactly one argument");
						return;
					}
					bNeedsFilesystem = true;
					Out << "VsMkDir(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__list_dir")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: __list_dir expects exactly one argument");
						return;
					}
					bNeedsFilesystem = true;
					bNeedsVector = true;
					Out << "VsListDir(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__cwd")
				{
					if (!Call.Arguments.empty())
					{
						Fail("AOT: __cwd expects no arguments");
						return;
					}
					bNeedsFilesystem = true;
					Out << "VsCwd()";
					return;
				}
				if (Call.Callee == "__chdir")
				{
					if (Call.Arguments.size() != 1)
					{
						Fail("AOT: __chdir expects exactly one argument");
						return;
					}
					bNeedsFilesystem = true;
					Out << "VsChdir(";
					EmitValueExpr(*Call.Arguments[0]);
					Out << ')';
					return;
				}
				if (Call.Callee == "__read_line" || Call.Callee == "read_line")
				{
					if (!Call.Arguments.empty())
					{
						Fail("AOT: read_line expects no arguments");
						return;
					}
					Out << "VsReadLine()";
					return;
				}
				if (Call.Callee == "__read_token" || Call.Callee == "read_token" || Call.Callee == "__read_string" || Call.Callee == "read_string")
				{
					if (!Call.Arguments.empty())
					{
						Fail("AOT: " + Call.Callee + " expects no arguments");
						return;
					}
					Out << "VsReadToken()";
					return;
				}
				if (Call.Callee == "__read_int" || Call.Callee == "read_int")
				{
					if (!Call.Arguments.empty())
					{
						Fail("AOT: read_int expects no arguments");
						return;
					}
					Out << "VsReadInt()";
					return;
				}
				if (Call.Callee == "__read_double" || Call.Callee == "read_double")
				{
					if (!Call.Arguments.empty())
					{
						Fail("AOT: read_double expects no arguments");
						return;
					}
					Out << "VsReadDouble()";
					return;
				}
				if (Call.Callee == "__read_bool" || Call.Callee == "read_bool")
				{
					if (!Call.Arguments.empty())
					{
						Fail("AOT: read_bool expects no arguments");
						return;
					}
					Out << "VsReadBool()";
					return;
				}
				if (Call.Callee == "assert")
				{
					if (Call.Arguments.size() < 1 || Call.Arguments.size() > 2)
					{
						Fail("AOT: assert expects one or two arguments");
						return;
					}
					bNeedsAssert = true;
					Out << "VsAssert(";
					EmitValueExpr(*Call.Arguments[0]);
					if (Call.Arguments.size() == 2)
					{
						Out << ", ";
						EmitValueExpr(*Call.Arguments[1]);
					}
					Out << ')';
					return;
				}
				if (Call.Callee == "format")
				{
					if (Call.Arguments.empty())
					{
						Fail("AOT: format expects at least a format string");
						return;
					}
					const bool bPassArrayDirect =
					    Call.Arguments.size() == 2 && ResolveAliasChain(ApplyTypeSubst(InferExprType(*Call.Arguments[1]))) == "array<string>";
					if (bPassArrayDirect)
					{
						bNeedsVector = true;
						Out << "VsFormatArray(";
						EmitValueExpr(*Call.Arguments[0]);
						Out << ", ";
						EmitValueExpr(*Call.Arguments[1]);
						Out << ')';
						return;
					}
					Out << "VsFormatBrace(";
					EmitValueExpr(*Call.Arguments[0]);
					for (std::size_t Index = 1; Index < Call.Arguments.size(); ++Index)
					{
						Out << ", ";
						EmitValueExpr(*Call.Arguments[Index]);
					}
					Out << ')';
					return;
				}
				if (Call.Callee == "formatf" || Call.Callee == "printf")
				{
					if (Call.Arguments.empty())
					{
						Fail("AOT: printf format call expects at least a format string");
						return;
					}
					bNeedsVector = true;
					const bool bPassArrayDirect =
					    Call.Arguments.size() == 2 && ResolveAliasChain(ApplyTypeSubst(InferExprType(*Call.Arguments[1]))) == "array<string>";
					if (bPassArrayDirect)
					{
						if (Call.Callee == "printf")
						{
							Out << "VsPrint(";
						}
						Out << "VsFormatPrintfArray(";
						EmitValueExpr(*Call.Arguments[0]);
						Out << ", ";
						EmitValueExpr(*Call.Arguments[1]);
						Out << ')';
						if (Call.Callee == "printf")
						{
							Out << ')';
						}
						return;
					}
					if (Call.Callee == "printf")
					{
						Out << "VsPrintfFormat(";
					}
					else
					{
						Out << "VsFormatPrintf(";
					}
					EmitValueExpr(*Call.Arguments[0]);
					for (std::size_t Index = 1; Index < Call.Arguments.size(); ++Index)
					{
						Out << ", ";
						EmitValueExpr(*Call.Arguments[Index]);
					}
					Out << ')';
					return;
				}
				{
					// `handle<Point>()`/`multicast_delegate<int>()`/... parse as a single
					// primary type-call: Callee holds the *full* "Base<Args>" text and
					// Call.TypeArgs stays empty (unlike generic *function* calls).
					std::string HandleInnerCheck;
					std::vector<std::string> ScratchArgs;
					const bool bBareHandle = Call.Callee == "handle" && !HasNonGenericHandleType() && OpenGenericTypes.find("handle") == OpenGenericTypes.end();
					const bool bGenericHandle = TryParseHandleType(Call.Callee, HandleInnerCheck);
					const bool bEventCtor = IsMulticastEventTypeName(Call.Callee) || TryParseTypeApplication(Call.Callee, "delegate", ScratchArgs);
					if (bBareHandle || bGenericHandle || bEventCtor)
					{
						std::string TypeApp = Call.Callee;
						if (bBareHandle)
						{
							if (EffectiveArguments.empty())
							{
								Fail("AOT: handle<T>() requires a type argument or one argument");
								return;
							}
							const std::string InnerType = InferExprType(*EffectiveArguments[0]);
							if (InnerType.empty())
							{
								Fail("AOT: cannot infer handle<T> element type");
								return;
							}
							TypeApp = "handle<" + InnerType + ">";
						}
						const std::string Mapped = MapType(TypeApp);
						if (bFailed)
						{
							return;
						}
						std::string Inner = Mapped;
						static const std::string SharedPtrPrefix = "std::shared_ptr<";
						if (Inner.compare(0, SharedPtrPrefix.size(), SharedPtrPrefix) == 0 && !Inner.empty() && Inner.back() == '>')
						{
							Inner = Inner.substr(
							    SharedPtrPrefix.size(),
							    Inner.size() - SharedPtrPrefix.size() - 1);
						}
						Out << "std::make_shared<" << Inner << ">(";
						std::string PayloadType;
						if (OpenGenericTypes.find("handle") == OpenGenericTypes.end() && TryParseHandleType(TypeApp, PayloadType) && EffectiveArguments.size() == 1)
						{
							EmitCoercedValue(PayloadType, *EffectiveArguments[0]);
						}
						else
						{
							EmitResolvedCallArgs(EffectiveArguments);
						}
						Out << ')';
						return;
					}
				}
				{
					std::vector<std::string> SoftArgs;
					if (bHasStandardAttribute && TryParseSoftBuiltinType(Call.Callee, "attribute", SoftArgs) && SoftArgs.size() == 1)
					{
						Out << MapType(Call.Callee) << '(';
						EmitResolvedCallArgs(EffectiveArguments, &SoftArgs);
						Out << ')';
						return;
					}
					if (TryParseSoftBuiltinType(Call.Callee, "optional", SoftArgs) && SoftArgs.size() == 1)
					{
						Out << "vs_Optional<" << MapType(SoftArgs[0]) << ">(";
						EmitResolvedCallArgs(EffectiveArguments);
						Out << ')';
						return;
					}
					if (TryParseSoftBuiltinType(Call.Callee, "readonly", SoftArgs) && SoftArgs.size() == 1)
					{
						Out << "vs_Readonly<" << MapType(SoftArgs[0]) << ">(";
						EmitResolvedCallArgs(EffectiveArguments);
						Out << ')';
						return;
					}
					if (TryParseSoftBuiltinType(Call.Callee, "type", SoftArgs) && SoftArgs.size() == 1)
					{
						if (!EffectiveArguments.empty())
						{
							Fail("AOT: type<T>() expects no arguments");
							return;
						}
						const std::string TokenType =
						    ResolveAliasChain(ApplyTypeSubst(SoftArgs[0]));
						Out << "vs_TypeToken<" << MapType(TokenType) << ">{\""
						    << EscapeCppString(ModuleMeta::TypeSimpleName(TokenType))
						    << "\", \"" << EscapeCppString(TokenType) << "\"}";
						return;
					}
					if (TryParseSoftBuiltinType(Call.Callee, "weak", SoftArgs) && SoftArgs.size() == 1)
					{
						Out << "vs_Weak<" << MapType(SoftArgs[0]) << ">(";
						EmitResolvedCallArgs(EffectiveArguments);
						Out << ')';
						return;
					}
				}
				if (!Call.TypeArgs.empty())
				{
					const std::string TypeApp = ReconstructTypeFromCall(Call);
					if (!TypeApp.empty() && EffectiveArguments.empty() && EmitEmptyContainerCtor(TypeApp))
					{
						return;
					}
					if (Call.Callee == "array" && Call.TypeArgs.size() == 1 && EffectiveArguments.empty())
					{
						bNeedsVector = true;
						Out << MapType(TypeApp) << "{}";
						return;
					}
				}
				{
					const std::string EffectiveCallee =
					    ResolveAliasChain(ApplyTypeSubst(Call.Callee));
					if (EffectiveArguments.empty() && EmitEmptyContainerCtor(EffectiveCallee))
					{
						return;
					}
				}
				const std::string TypeLookupCallee =
				    ResolveAliasChain(ApplyTypeSubst(Call.Callee));
				if (!Call.TypeArgs.empty() && UserTypes.find(TypeLookupCallee) == UserTypes.end() && FindGenericByName(TypeLookupCallee, &Call.Location) == nullptr)
				{
					const std::string TypeApp = ReconstructTypeFromCall(Call);
					if (TypeApp.empty() || !EmitEmptyContainerCtor(TypeApp))
					{
						Fail("AOT: unexpected type arguments on call '" + Call.Callee + "'");
						return;
					}
					return;
				}

				std::vector<std::string> ArgTypes;
				ArgTypes.reserve(EffectiveArguments.size());
				for (const FExpr* Arg : EffectiveArguments)
				{
					const std::string ArgType = InferExprType(*Arg);
					if (ArgType.empty() && UserTypes.find(Call.Callee) == UserTypes.end())
					{
						// Still allow if ctor/type args of user type and literal array etc.
					}
					ArgTypes.push_back(ArgType);
				}

				{
					std::string CtorTypeName = TypeLookupCallee;
					bool bOpenTemplateCtor = false;
					std::vector<std::string> OpenCtorArgs;
					if (!Call.TypeArgs.empty() && OpenGenericTypes.find(TypeLookupCallee) != OpenGenericTypes.end())
					{
						std::vector<std::string> MonoArgs = Call.TypeArgs;
						for (std::string& Arg : MonoArgs)
						{
							Arg = ResolveAliasChain(ApplyTypeSubst(Arg));
						}
						CtorTypeName = EnsureTypeMonomorph(TypeLookupCallee, MonoArgs);
						if (CtorTypeName.empty())
						{
							return;
						}
						for (const std::string& Arg : MonoArgs)
						{
							if (TypeMentionsActiveOpenParam(Arg))
							{
								bOpenTemplateCtor = true;
								break;
							}
						}
						OpenCtorArgs = std::move(MonoArgs);
					}
					else
					{
						std::vector<std::string> ParsedArgs;
						const std::string::size_type Lt = CtorTypeName.find('<');
						if (Lt != std::string::npos && CtorTypeName.back() == '>')
						{
							const std::string Base = CtorTypeName.substr(0, Lt);
							if (OpenGenericTypes.find(Base) != OpenGenericTypes.end())
							{
								SplitTopLevelTypeArgs(
								    CtorTypeName.substr(Lt + 1, CtorTypeName.size() - Lt - 2),
								    ParsedArgs);
								for (std::string& Arg : ParsedArgs)
								{
									Arg = ResolveAliasChain(ApplyTypeSubst(Arg));
									if (TypeMentionsActiveOpenParam(Arg))
									{
										bOpenTemplateCtor = true;
									}
								}
								OpenCtorArgs = ParsedArgs;
								CtorTypeName = EnsureTypeMonomorph(Base, ParsedArgs);
								if (CtorTypeName.empty())
								{
									return;
								}
							}
						}
					}
					const auto TypeIt = UserTypes.find(CtorTypeName);
					if (TypeIt != UserTypes.end() && !TypeIt->second.bIsInterface)
					{
						for (std::size_t Index = 0; Index < ArgTypes.size(); ++Index)
						{
							if (ArgTypes[Index].empty())
							{
								Fail(
								    "AOT: cannot infer constructor argument type for '" + CtorTypeName + "'");
								return;
							}
						}
						PushTypeSubstForUserType(TypeIt->second);
						const FConstructorDecl* Ctor = ResolveCtor(TypeIt->second, ArgTypes);
						TypeSubst.clear();
						if (!TypeIt->second.Constructors->empty() && !Ctor && !bOpenTemplateCtor)
						{
							Fail("AOT: no matching constructor for '" + CtorTypeName + "'");
							return;
						}
						if (bOpenTemplateCtor)
						{
							std::string OpenCtorName = TypeLookupCallee;
							const std::string::size_type Lt = OpenCtorName.find('<');
							if (Lt != std::string::npos)
							{
								OpenCtorName.resize(Lt);
							}
							Out << MangleCtor(OpenCtorName) << '<';
							for (std::size_t Index = 0; Index < OpenCtorArgs.size(); ++Index)
							{
								if (Index > 0)
								{
									Out << ", ";
								}
								Out << MapType(OpenCtorArgs[Index]);
							}
							Out << ">(";
						}
						else
						{
							Out << MangleCtor(CtorTypeName) << '(';
						}
						if (Ctor)
						{
							EmitCallArgsForUserTypeParams(
							    TypeIt->second, Ctor->Parameters, EffectiveArguments);
						}
						else
						{
							EmitResolvedCallArgs(EffectiveArguments);
						}
						Out << ')';
						return;
					}
				}

				if (!Call.TypeArgs.empty())
				{
					std::vector<std::string> TypeArgs;
					if (const FFunctionDecl* Generic =
					        ResolveGenericOverload(Call.Callee, Call, TypeArgs))
					{
						if (!HasCollectedGenericCall(*Generic, TypeArgs))
						{
							Fail(
							    "AOT: missing specialization for generic '" + Call.Callee + "' (internal collect)");
							return;
						}
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
						EmitCallArgsForGenericParams(*Generic, TypeArgs, Call.GetArguments());
						Out << ')';
						return;
					}
					Fail(
					    "AOT: unresolved generic call '" + Call.Callee + "' with explicit type arguments");
					return;
				}

				// Empty ArgTypes entries are OK (contextual bare lambdas): TypesCompatible
				// treats them as wildcards against the candidate parameter types.
				if (const FFunctionDecl* Concrete = ResolveConcreteCallOverload(
				        Call.Callee, Call, ArgTypes, &Call.Location))
				{
					Out << EmitFunctionCppName(*Concrete) << '(';
					EmitCallArgsForParams(Concrete->Parameters, Call.Arguments);
					Out << ')';
					return;
				}

				{
					std::vector<std::string> TypeArgs;
					if (const FFunctionDecl* Generic =
					        ResolveGenericOverload(Call.Callee, Call, TypeArgs))
					{
						if (!HasCollectedGenericCall(*Generic, TypeArgs))
						{
							Fail(
							    "AOT: missing specialization for generic '" + Call.Callee + "' (internal collect)");
							return;
						}
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
						EmitCallArgsForGenericParams(*Generic, TypeArgs, Call.GetArguments());
						Out << ')';
						return;
					}
				}

				if (HostBindings && HostBindings->FindFunction(Call.Callee))
				{
					Out << SanitizeForMangle(Call.Callee) << '(';
					for (std::size_t Index = 0; Index < Call.Arguments.size(); ++Index)
					{
						if (Index > 0)
						{
							Out << ", ";
						}
						EmitValueExpr(*Call.Arguments[Index]);
					}
					Out << ')';
					return;
				}

				if (bInSelfContext && !SelfTypeName.empty())
				{
					const auto SelfIt = UserTypes.find(SelfTypeName);
					if (SelfIt != UserTypes.end())
					{
						const FMethodDecl* Method =
						    ResolveMethod(SelfIt->second, Call.Callee, ArgTypes);
						if (Method)
						{
							if (TypeNeedsOopLayout(SelfIt->second))
							{
								if (bMemberMethodBody || CapturedClassThisLambdaDepth != 0u || CapturedStructThisLambdaDepth != 0u)
								{
									if (CapturedClassThisLambdaDepth != 0u)
									{
										Out << "VsRequireObject(" << CapturedThisName << ").";
									}
									else if (CapturedStructThisLambdaDepth != 0u)
									{
										Out << CapturedThisName << '.';
									}
									else
									{
										Out << "this->";
									}
								}
								else if (SelfIt->second.bIsClass && !bCtorInitDirectSelf)
								{
									bNeedsMemory = true;
									Out << "VsRequireObject(" << SelfReceiverName << ").";
								}
								else
								{
									Out << SelfReceiverName << '.';
								}
								Out << SanitizeSymbolName(Call.Callee) << '(';
								EmitCallArgsForUserTypeParams(
								    SelfIt->second, Method->Parameters, Call.Arguments);
								Out << ')';
								return;
							}
							Out << MangleMethod(SelfTypeName, Call.Callee) << '('
							    << (CapturedStructThisLambdaDepth != 0u ? CapturedThisName : SelfReceiverName);
							if (!Method->Parameters.empty())
							{
								Out << ", ";
								EmitCallArgsForUserTypeParams(
								    SelfIt->second, Method->Parameters, Call.Arguments);
							}
							Out << ')';
							return;
						}
					}
				}

				if (IsRuntimeSourceMetadataType(ResolveAliasChain(ApplyTypeSubst(Call.Callee))))
				{
					Fail("AOT: runtime source metadata requires the VeroScript runtime; use the VM backend");
					return;
				}
				Fail("AOT: unresolved call '" + Call.Callee + "'");
			}
