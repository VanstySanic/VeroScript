// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

			struct FSequencedOperand
			{
				const FExpr* Expression = nullptr;
				std::string Name;
				std::string Initializer;
				bool bReference = false;
				bool bBuilding = false;
				bool bResolveAfterArguments = false;
				bool bIndex = false;
			};

			struct FSequencedExpression
			{
				const FExpr* Root = nullptr;
				FSequencedExpression* Parent = nullptr;
				std::vector<FSequencedOperand> Operands;
				FSequencedOperand Callable;
			};

			FSequencedExpression* ActiveSequence = nullptr;
			const FExpr* ValueExpression = nullptr;
			const FExpr* ReferenceExpression = nullptr;
			std::size_t NextSequence = 0;
			bool bEmittingNamespaceInitializer = false;
			// Rendering an operand must not change the surrounding output, including
			// when a contextual conversion renders another call recursively.
			struct FOperandOutputScope
			{
				explicit FOperandOutputScope(std::ostringstream& InOutput)
					: Output(InOutput), Previous(std::move(InOutput))
				{
					Output = std::ostringstream{};
				}
				~FOperandOutputScope()
				{
					Output = std::move(Previous);
				}
				std::ostringstream& Output;
				std::ostringstream Previous;
			};

			std::string SequencedOperandName(const std::size_t Sequence, const std::size_t Index) const
			{
				std::string Name = "__vs_eval_" + std::to_string(Sequence) + '_' + std::to_string(Index);
				while (LocalTypes.count(Name))
				{
					Name += '_';
				}

				return Name;
			}

			void EmitValueExpr(const FExpr& Expr, const std::string* Hint = nullptr)
			{
				TValueScopeRestore<const FExpr*> ValueScope(ValueExpression);
				ValueExpression = &Expr;
				EmitExpr(Expr, Hint);
			}

			std::string ResolveValueType(const std::string& RawType) const
			{
				const std::string Type = ResolveAliasChain(ApplyTypeSubst(RawType));
				return Runtime::IsReferenceTypeName(Type) ? std::string(Runtime::StripReferenceTypeName(Type)) : Type;
			}

			// Capture lazily at the existing emission site so parameter coercions
			// and contextual lambda types remain authoritative. Declarations are
			// emitted in script order; the rendered call uses only saved operands.
			template <typename FInitializer>
			void EmitSequencedOperand(FSequencedOperand& Operand, FInitializer&& Initializer, const bool Move = false)
			{
				if (Operand.Initializer.empty())
				{
					FOperandOutputScope OutputScope(Out);
					TValueScopeRestore<bool> BuildingScope(Operand.bBuilding);
					TValueScopeRestore<const FExpr*> ValueScope(ValueExpression);
					Operand.bBuilding = true;
					ValueExpression = Operand.bReference ? nullptr : Operand.Expression;
					Initializer();
					Operand.Initializer = Out.str();
					if (Operand.bIndex)
					{
						Operand.Initializer = "VsCheckedNativeSize(" + Operand.Initializer + ')';
					}
				}
				if (Operand.bResolveAfterArguments)
				{
					Out << Operand.Initializer;
				}

				else if (Move && !Operand.bReference)
				{
					Out << "std::move(" << Operand.Name << ')';
				}

				else
				{
					Out << Operand.Name;
				}
			}

			template <typename FInitializer>
			bool TryEmitSequencedOperand(const FExpr& Expr, FInitializer&& Initializer, const bool Move = false)
			{
				for (auto* Sequence = ActiveSequence; Sequence; Sequence = Sequence->Parent)
				{
					for (auto& Operand : Sequence->Operands)
					{
						if (Operand.Expression == &Expr && !Operand.bBuilding)
						{
							EmitSequencedOperand(Operand, std::forward<FInitializer>(Initializer), Move);
							return true;
						}
					}
				}
				return false;
			}

			void EmitNamedCallable(const FExpr& Call, const std::string& CppName)
			{
				if (ActiveSequence && ActiveSequence->Root == &Call)
				{
					EmitSequencedOperand(ActiveSequence->Callable,
						[&]
						{
							Out << CppName;
						});
				}
				else
				{
					Out << CppName;
				}
			}

			void SetSequencedValueArgument(const FExpr& Expr)
			{
				for (auto* Sequence = ActiveSequence; Sequence; Sequence = Sequence->Parent)
				{
					for (auto& Operand : Sequence->Operands)
					{
						if (Operand.Expression == &Expr && !Operand.bResolveAfterArguments)
						{
							// A struct method uses a reference receiver; an extension
							// with a value first parameter must instead take a snapshot.
							Operand.bReference = false;
							return;
						}
					}
				}
			}

			void EmitCoercedReferenceArgument(const std::string& Type, const FExpr& Expr)
			{
				for (auto* Sequence = ActiveSequence; Sequence; Sequence = Sequence->Parent)
				{
					for (auto& Operand : Sequence->Operands)
					{
						if (Operand.Expression == &Expr)
						{
							Operand.bReference = true;
						}
					}
				}

				TValueScopeRestore<const FExpr*> ReferenceScope(ReferenceExpression);
				ReferenceExpression = &Expr;
				EmitCoercedValue(Type, Expr);
			}

			bool SequenceReceiverByReference(const FExpr& Receiver)
			{
				const bool bThis = Receiver.Kind == EExprKind::This
					|| (Receiver.Kind == EExprKind::Identifier
						&& static_cast<const FIdentifierExpr&>(Receiver).Name == "this") || IsConstCastOfThis(Receiver);
				if (IsDirectObjectReceiver(Receiver, bThis)) return true;
				const std::string Type = ResolveAliasChain(ApplyTypeSubst(InferExprType(Receiver)));
				// Emission also supports receiver forms that have no standalone
				// inferred type (for example some namespace/property paths).
				if (Type.empty()) return true;
				if (const auto* UserType = ResolveUserTypeForLookup(Type))
				{
					return !UserType->bIsClass && !UserType->bIsInterface;
				}

				// The generated dynamic containers carry shared storage. Retain the
				// receiver's current handle if a later argument rebinds its variable.
				const std::string CppType = MapType(Type);
				if (CppType.rfind("std::shared_ptr<", 0) == 0) return false;
				for (const char* Prefix :
					{"vs_Array<", "vs_Map<", "vs_OrderedMap<", "vs_MultiSet<", "vs_OrderedMultiSet<",
						"vs_Set<", "vs_Deque<", "vs_Queue<", "vs_PriorityQueue<", "vs_Stack<"})
				{
					if (CppType.rfind(Prefix, 0) == 0) return false;
				}

				return true;
			}

			bool TryEmitSequencedExpression(const FExpr& Expr, const std::string* InitTypeHint = nullptr,
				const std::function<void()>& EmitBody = {})
			{
				if (ActiveSequence && ActiveSequence->Root == &Expr) return false;
				const bool bValueResult = Expr.Kind == EExprKind::Assign || ValueExpression == &Expr;
				FSequencedExpression Sequence;
				Sequence.Root = &Expr;
				Sequence.Parent = ActiveSequence;
				const FExpr* IndexWriteback = nullptr;
				std::size_t IndexOwnerOperand = 0;
				std::function<void(const FExpr*, bool)> Add = [&](const FExpr* Operand, const bool Reference)
				{
					if (!Operand) return;
					const bool bReference = Reference || Operand->Kind == EExprKind::Ref;
					const FExpr& Target = Operand->Kind == EExprKind::Ref
						? *static_cast<const FRefExpr&>(*Operand).Operand : *Operand;
					if (bReference && Target.Kind == EExprKind::IndexAccess)
					{
						if (Expr.Kind != EExprKind::Assign || Operand->Kind == EExprKind::Ref)
						{
							// A native T& cannot validate the original borrowed container and
							// element identity after a later argument or the callee mutates it.
							// Core-backed AOT supplies the same managed binding as VM/JIT.
							Fail("AOT: indexed reference argument requires managed storage paths; use the Core runtime");
							return;
						}
						// Ordinary index assignment retains its existing writeback path.
						const auto& Index = static_cast<const FIndexAccessExpr&>(Target);
						Add(Index.Object.get(), true);
						Add(Index.Index.get(), false);
						const std::string ContainerType = ResolveAliasChain(ApplyTypeSubst(InferExprType(*Index.Object)));
						// A plain write evaluates the RHS before validating the element;
						// a compound write reads and validates its old value first.
						Sequence.Operands.back().bIndex = static_cast<const FAssignExpr&>(Expr).bIsCompound
							&& (Runtime::IsDynamicArrayTypeName(ContainerType)
							    || Runtime::IsNativeArrayTypeName(ContainerType) || Runtime::IsScriptArgsArrayTypeName(ContainerType)
							    || Runtime::IsDequeTypeName(ContainerType));
						Sequence.Operands.push_back({Operand, {}, {}, true, false, true});
					}
					else if (bReference && Target.Kind == EExprKind::MemberAccess)
					{
						const auto& Member = static_cast<const FMemberAccessExpr&>(Target);
						Add(Member.Object.get(), SequenceReceiverByReference(*Member.Object));
						Sequence.Operands.push_back({Operand, {}, {}, true, false, true});
					}
					else
					{
						Sequence.Operands.push_back({Operand, {}, {}, bReference});
					}
				};
				if (Expr.Kind == EExprKind::Binary)
				{
					const auto& Binary = static_cast<const FBinaryExpr&>(Expr);
					if (Binary.Op == EBinaryOp::LogicalAnd || Binary.Op == EBinaryOp::LogicalOr) return false;
					// Reading scalar locals/literals has no observable ordering effect.
					// Keep ordinary arithmetic and loop conditions as direct expressions.
					const auto Stable = [](const FExpr& Value)
					{
						return Value.Kind == EExprKind::Identifier || Value.Kind == EExprKind::This ||
							Value.Kind == EExprKind::CharacterLiteral ||
							Value.Kind == EExprKind::IntegerLiteral ||
							Value.Kind == EExprKind::FloatLiteral ||
							Value.Kind == EExprKind::BooleanLiteral ||
							Value.Kind == EExprKind::StringLiteral ||
							Value.Kind == EExprKind::WStringLiteral ||
							Value.Kind == EExprKind::NullptrLiteral;
					};
					if (Stable(*Binary.Left) && Stable(*Binary.Right)) return false;
					Add(Binary.Left.get(), false);
					Add(Binary.Right.get(), false);
				}
				else if (Expr.Kind == EExprKind::Call)
				{
					const auto& Call = static_cast<const FCallExpr&>(Expr);
					if (Call.Callee == "exit") return false;
					for (const FExpr* Argument : Call.GetArguments())
					{
						Add(Argument, false);
					}

					// The VM evaluates a function value after its arguments. Method
					// receivers above/below have their separate receiver-first rule.
					Add(Call.CalleeExpr.get(), false);
					if (Sequence.Operands.size() < 2)
					{
						if (Sequence.Operands.empty() || IsFormatFamilyIntrinsicCall(Call)) return false;
						const auto Local = LocalTypes.find(Call.Callee);
						const auto* Global = Local == LocalTypes.end() ? ResolveGlobalVariable(Call.Callee, &Call.Location) : nullptr;
						const std::string Type = Local != LocalTypes.end() ? Local->second : Global ? Global->TypeName : "";
						if (!Runtime::IsFunctionTypeName(ResolveAliasChain(ApplyTypeSubst(Type)))) return false;
					}
				}
				else if (Expr.Kind == EExprKind::MethodCall)
				{
					const auto& Call = static_cast<const FMethodCallExpr&>(Expr);
					std::string StaticQual, NamespaceName;
					const std::vector<FFieldDecl>* Fields = nullptr;
					const std::vector<FMethodDecl>* Methods = nullptr;
					const bool bStatic = Call.Object && (TryResolveStaticTypeReceiver(*Call.Object, StaticQual, Fields, Methods)
						|| TryBuildNamespaceCallName(*Call.Object, Call.Method, NamespaceName));
					const bool bRetainedReceiver = Call.Object && RetainedReferenceReceivers.count(Call.Object.get());
					const bool bManagedReceiver = !bStatic && Call.Object && !SequenceReceiverByReference(*Call.Object);
					if (bManagedReceiver && Runtime::IsReferenceTypeName(InferExprType(Expr)))
					{
						if (const auto* Type =
								ResolveUserTypeForLookup(ResolveValueType(InferExprType(*Call.Object)));
							Type && ReferenceOwnerHasCleanup(*Type))
						{
							Fail("AOT: field reference owner requires managed reference cleanup; use the VM backend");
							return true;
						}
					}

					if (Call.GetArguments().empty() && !bManagedReceiver && !bRetainedReceiver) return false;
					if (!bStatic)
					{
						Add(Call.Object.get(),
							bRetainedReceiver || (Call.Object && SequenceReceiverByReference(*Call.Object)));
					}

					for (const FExpr* Argument : Call.GetArguments())
					{
						Add(Argument, false);
					}

					if (Sequence.Operands.size() < 2 && !bManagedReceiver && !bRetainedReceiver &&
						!ResolveGlobalVariable(NamespaceName, &Call.Location))
					{
						return false;
					}

				}
				else if (Expr.Kind == EExprKind::Assign)
				{
					const auto& Assign = static_cast<const FAssignExpr&>(Expr);
					if ((Assign.bFromPrefixIncDec || Assign.bFromPostfixIncDec) &&
						Assign.Target->Kind != EExprKind::MethodCall)
					{
						return false;
					}

					if (!Assign.bIsCompound && Assign.Target->Kind == EExprKind::MemberAccess)
					{
						const auto& Target = static_cast<const FMemberAccessExpr&>(*Assign.Target);
						const auto Simple = [](const FExpr& Value)
						{
							return Value.Kind == EExprKind::Identifier || Value.Kind == EExprKind::This ||
								Value.Kind == EExprKind::IntegerLiteral ||
								Value.Kind == EExprKind::FloatLiteral ||
								Value.Kind == EExprKind::BooleanLiteral ||
								Value.Kind == EExprKind::CharacterLiteral;
						};
						if (Simple(*Target.Object) && Simple(*Assign.Value) &&
							InferExprType(*Assign.Target) == InferExprType(*Assign.Value))
						{
							return false;
						}
					}
					if (Assign.Target->Kind == EExprKind::IndexAccess)
					{
						const auto& Target = static_cast<const FIndexAccessExpr&>(*Assign.Target);
						const auto ScalarRead = [](const FExpr& Value)
						{
							return Value.Kind == EExprKind::Identifier ||
								Value.Kind == EExprKind::IntegerLiteral ||
								Value.Kind == EExprKind::FloatLiteral ||
								Value.Kind == EExprKind::BooleanLiteral ||
								Value.Kind == EExprKind::CharacterLiteral;
						};
						// No storage can be rebound by these reads or conversions. In
						// particular, do not copy a whole native array for a[i] = value.
						if (Target.Object->Kind == EExprKind::Identifier && ScalarRead(*Target.Index) &&
							ScalarRead(*Assign.Value) &&
							InferExprType(*Assign.Target) == InferExprType(*Assign.Value))
						{
							return false;
						}

						const FExpr* Root = Assign.Target.get();
						bool bLinearPath = true;
						while (Root->Kind == EExprKind::IndexAccess)
						{
							const auto& Index = static_cast<const FIndexAccessExpr&>(*Root);
							const std::string Type = ResolveAliasChain(ApplyTypeSubst(InferExprType(*Index.Object)));
							bLinearPath &= Runtime::IsDynamicArrayTypeName(Type)
								|| Runtime::IsNativeArrayTypeName(Type) || Runtime::IsScriptArgsArrayTypeName(Type)
								|| Runtime::IsDequeTypeName(Type);
							Root = Index.Object.get();
						}
						bool bQualifiedRoot = false;
						if (Root->Kind == EExprKind::MemberAccess)
						{
							const auto& Member = static_cast<const FMemberAccessExpr&>(*Root);
							std::string Qual, NamespaceName;
							const std::vector<FFieldDecl>* Fields = nullptr;
							const std::vector<FMethodDecl>* Methods = nullptr;
							bQualifiedRoot = TryResolveStaticTypeReceiver(*Member.Object, Qual, Fields, Methods)
								|| TryBuildNamespaceCallName(*Member.Object, Member.Member, NamespaceName);
						}
						const auto ContainerType = ResolveAliasChain(ApplyTypeSubst(InferExprType(*Target.Object)));
						if (bLinearPath && (Target.Object->Kind == EExprKind::IndexAccess || bQualifiedRoot
						    || Runtime::IsNativeArrayTypeName(ContainerType) || Runtime::IsScriptArgsArrayTypeName(ContainerType)))
						{
							// Match the VM's indexed storage path, including fixed arrays:
							// copying a whole array would discard mutations made by the RHS.
							Add(Assign.Target.get(), true);
						}
						else
						{
							// SetIndex instead snapshots the container, then writes that
							// container back to its original variable/field after the RHS.
							if (Target.Object->Kind == EExprKind::MemberAccess && !bQualifiedRoot)
							{
								const auto& Member = static_cast<const FMemberAccessExpr&>(*Target.Object);
								Add(Member.Object.get(), SequenceReceiverByReference(*Member.Object));
							}
							IndexOwnerOperand = Sequence.Operands.size();
							Add(Target.Object.get(), false);
							Add(Target.Index.get(), false);
							if (Target.Object->Kind == EExprKind::Identifier ||
								Target.Object->Kind == EExprKind::MemberAccess)
							{
								IndexWriteback = Target.Object.get();
							}
						}
					}
					else if (Assign.Target->Kind == EExprKind::MemberAccess)
					{
						const auto& Target = static_cast<const FMemberAccessExpr&>(*Assign.Target);
						std::string StaticQual, NamespaceName;
						const std::vector<FFieldDecl>* Fields = nullptr;
						const std::vector<FMethodDecl>* Methods = nullptr;
						if (!TryResolveStaticTypeReceiver(*Target.Object, StaticQual, Fields, Methods) &&
							!TryBuildNamespaceCallName(*Target.Object, Target.Member, NamespaceName))
						{
							Add(Target.Object.get(), SequenceReceiverByReference(*Target.Object));
						}

					}
					else if (Assign.Target->Kind == EExprKind::MethodCall)
					{
						const auto& Target = static_cast<const FMethodCallExpr&>(*Assign.Target);
						if (Target.Object)
						{
							Add(Target.Object.get(), SequenceReceiverByReference(*Target.Object));
						}

						for (const auto* Arg : Target.GetArguments())
						{
							Add(Arg, false);
						}

						// Bind the returned storage once before the RHS. The saved
						// receiver keeps the reference alive through the whole assignment.
						Add(Assign.Target.get(), true);
					}
					else
					{
						return false;
					}

					if (Sequence.Operands.empty()) return false;
					if (!Assign.bIsCompound)
					{
						Add(Assign.Value.get(), false);
					}

				}
				else
				{
					return false;
				}

				if (bFailed) return true;
				const std::size_t Id = NextSequence++;
				Sequence.Callable.Name = SequencedOperandName(Id, 0);
				for (std::size_t Index = 0; Index < Sequence.Operands.size(); ++Index)
				{
					Sequence.Operands[Index].Name = SequencedOperandName(Id, Index + 1);
				}

				std::string Body, Writeback;
				{
					TValueScopeRestore<FSequencedExpression*> SequenceScope(ActiveSequence);
					ActiveSequence = &Sequence;
					FOperandOutputScope OutputScope(Out);
					if (EmitBody)
					{
						EmitBody();
					}

					else
					{
						EmitExpr(Expr, InitTypeHint);
					}

					Body = Out.str();
					if (IndexWriteback)
					{
						FOperandOutputScope WritebackOutput(Out);
						TValueScopeRestore<bool> BuildingScope(Sequence.Operands[IndexOwnerOperand].bBuilding);
						Sequence.Operands[IndexOwnerOperand].bBuilding = true;
						EmitExpr(*IndexWriteback);
						Writeback = Out.str();
					}
				}
				if (!bValueResult && Runtime::IsReferenceTypeName(ResolveAliasChain(ApplyTypeSubst(InferExprType(Expr)))))
				{
					const bool bRetainedOwner = !Sequence.Callable.Initializer.empty() ||
						std::any_of(Sequence.Operands.begin(), Sequence.Operands.end(),
							[&](const auto& Operand)
							{
								if (Operand.bReference || SequenceReceiverByReference(*Operand.Expression))
								{
									return false;
								}
								for (auto* Parent = Sequence.Parent; Parent; Parent = Parent->Parent)
								{
									for (const auto& Saved : Parent->Operands)
									{
										if (Saved.Expression == Operand.Expression && !Saved.bBuilding &&
											!Saved.Initializer.empty())
										{
											return false;
										}
									}
								}

								return true;
							});
					if (bRetainedOwner)
					{
						Fail("AOT: reference escaping a sequenced call requires managed owner retention; use the VM backend");
						return true;
					}
				}
				Out << (bEmittingNamespaceInitializer && !Sequence.Parent ? "([]()" : "([&]()")
					// Assignment is a value in the VM. Returning an lvalue reference
					// could outlive a saved receiver destroyed at this lambda's exit.
					<< (bValueResult ? " -> auto { " : " -> decltype(auto) { ");
				const auto Declare = [&](const FSequencedOperand& Operand)
				{
					if (!Operand.Initializer.empty() && !Operand.bResolveAfterArguments)
					{
						Out << (Operand.bReference ? "auto&& " : "auto ") << Operand.Name << " = ("
							<< Operand.Initializer << "); ";
					}
				};
				for (const auto& Operand : Sequence.Operands)
				{
					Declare(Operand);
				}

				Declare(Sequence.Callable);
				if (IndexWriteback)
				{
					const std::string Result = SequencedOperandName(Id, Sequence.Operands.size() + 1);
					Out << "auto " << Result << " = (" << Body << "); (" << Writeback << ") = std::move("
						<< Sequence.Operands[IndexOwnerOperand].Name << "); return " << Result << "; }())";
				}
				else
				{
					Out << "return " << Body << "; }())";
				}

				return true;
			}

			template <typename FOperation>
			void EmitCompoundSnapshot(const FAssignExpr& Assign, const std::string& CppType, FOperation&& Operation)
			{
				const std::size_t Id = NextSequence++;
				const std::string Old = SequencedOperandName(Id, 0), Right = SequencedOperandName(Id, 1);
				Out << "([&]() -> " << CppType << " { auto " << Old << " = (";
				EmitExpr(*Assign.Target);
				Out << "); auto " << Right << " = (";
				EmitExpr(*Assign.Value);
				Out << "); return (";
				// The outer assignment sequence retains the owner and index. Resolve
				// its storage again after the RHS, which may have grown an array.
				EmitExpr(*Assign.Target);
				Out << " = ";
				Operation(Old, Right);
				Out << "); }())";
			}
