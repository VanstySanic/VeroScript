// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

			void CollectLambdaCaptureNames(
				const FExpr& Expr,
				const std::unordered_set<std::string>& Candidates,
				std::unordered_set<std::string>& OutNames,
				std::unordered_set<std::string>* const OutBindingChanges = nullptr) const
			{
				const auto Visit = [&](const std::unique_ptr<FExpr>& Value)
				{
					if (Value)
					{
						CollectLambdaCaptureNames(*Value, Candidates, OutNames, OutBindingChanges);
					}
				};
				switch (Expr.Kind)
				{
				case EExprKind::CharacterLiteral:
					return;
				case EExprKind::Identifier:
					{
						const std::string& Name = static_cast<const FIdentifierExpr&>(Expr).Name;
						if (Candidates.find(Name) != Candidates.end())
						{
							OutNames.insert(Name);
						}
						return;
					}
				case EExprKind::Unary:
					{
						Visit(static_cast<const FUnaryExpr&>(Expr).Operand);
						return;
					}
				case EExprKind::Binary:
					{
						Visit(static_cast<const FBinaryExpr&>(Expr).Left);
						Visit(static_cast<const FBinaryExpr&>(Expr).Right);
						return;
					}
				case EExprKind::Assign:
					{
						const auto& Assign = static_cast<const FAssignExpr&>(Expr);
						if (OutBindingChanges && !Assign.bIsCompound && Assign.Target
							&& Assign.Target->Kind == EExprKind::Identifier)
						{
							const auto& Name = static_cast<const FIdentifierExpr&>(*Assign.Target).Name;
							if (Candidates.count(Name))
							{
								OutBindingChanges->insert(Name);
							}
						}
						Visit(Assign.Target);
						Visit(static_cast<const FAssignExpr&>(Expr).Value);
						return;
					}
				case EExprKind::Conditional:
					{
						Visit(static_cast<const FConditionalExpr&>(Expr).Condition);
						Visit(static_cast<const FConditionalExpr&>(Expr).ThenExpr);
						Visit(static_cast<const FConditionalExpr&>(Expr).ElseExpr);
						return;
					}
				case EExprKind::Call:
					{
						const auto& Call = static_cast<const FCallExpr&>(Expr);
						if (Candidates.find(Call.Callee) != Candidates.end())
						{
							OutNames.insert(Call.Callee);
						}
						Visit(Call.CalleeExpr);
						for (const FExpr* Argument : Call.GetArguments())
						{
							if (Argument)
							{
								CollectLambdaCaptureNames(*Argument, Candidates, OutNames, OutBindingChanges);
							}
						}
						return;
					}
				case EExprKind::MemberAccess:
					{
						Visit(static_cast<const FMemberAccessExpr&>(Expr).Object);
						return;
					}
				case EExprKind::MethodCall:
					{
						const auto& Call = static_cast<const FMethodCallExpr&>(Expr);
						Visit(Call.Object);
						for (const FExpr* Argument : Call.GetArguments())
						{
							if (Argument)
							{
								CollectLambdaCaptureNames(*Argument, Candidates, OutNames, OutBindingChanges);
							}
						}
						return;
					}
				case EExprKind::IndexAccess:
					{
						Visit(static_cast<const FIndexAccessExpr&>(Expr).Object);
						Visit(static_cast<const FIndexAccessExpr&>(Expr).Index);
						return;
					}
				case EExprKind::ArrayInit:
					{
						for (const auto& Element : static_cast<const FArrayInitExpr&>(Expr).Elements)
						{
							Visit(Element);
						}
						return;
					}
				case EExprKind::MapInit:
					{
						for (const FMapInitEntry& Entry : static_cast<const FMapInitExpr&>(Expr).Entries)
						{
							Visit(Entry.Key);
							Visit(Entry.Value);
						}
						return;
					}
				case EExprKind::Ref:
					{
						const auto& Ref = static_cast<const FRefExpr&>(Expr);
						if (OutBindingChanges && Ref.Operand && Ref.Operand->Kind == EExprKind::Identifier)
						{
							const auto& Name = static_cast<const FIdentifierExpr&>(*Ref.Operand).Name;
							if (Candidates.count(Name))
							{
								OutBindingChanges->insert(Name);
							}
						}
						Visit(Ref.Operand);
						return;
					}
				case EExprKind::Lambda:
					{
						const auto& Nested = static_cast<const FLambdaExpr&>(Expr);
						for (const auto& Capture : Nested.Captures)
						{
							if (Capture.Initializer)
							{
								Visit(Capture.Initializer);
							}
							else if (Candidates.count(Capture.Name))
							{
								OutNames.insert(Capture.Name);
							}
						}
						auto NestedCandidates = Candidates;
						for (const auto& Capture : Nested.Captures)
						{
							if (Capture.Initializer)
							{
								NestedCandidates.erase(Capture.Name);
							}
						}
						for (const FFunctionParam& Parameter : Nested.Parameters)
						{
							NestedCandidates.erase(Parameter.Name);
						}
						std::unordered_set<std::string> NestedChanges;
						auto* const NestedBindingChanges = OutBindingChanges ? &NestedChanges : nullptr;
						if (Nested.Body)
						{
							CollectLambdaCaptureNames(*Nested.Body, NestedCandidates, OutNames, NestedBindingChanges);
						}
						if (Nested.ExpressionBody)
						{
							CollectLambdaCaptureNames(*Nested.ExpressionBody, NestedCandidates, OutNames, NestedBindingChanges);
						}
						for (const auto& Name : NestedChanges)
						{
							const bool bReferenceDefault = Nested.CaptureKind == ELambdaCaptureKind::Reference
								&& !Nested.HasCapture(Name, false);
							if (bReferenceDefault ||
								Nested.HasCapture(Name, true))
							{
								OutBindingChanges->insert(Name);
							}
						}
						return;
					}
				default:
					return;
				}
			}

			void CollectLambdaCaptureNames(
				const FStmt& Statement,
				const std::unordered_set<std::string>& Candidates,
				std::unordered_set<std::string>& OutNames,
				std::unordered_set<std::string>* const OutBindingChanges = nullptr) const
			{
				const auto VisitExpr = [&](const std::unique_ptr<FExpr>& Value)
				{
					if (Value)
					{
						CollectLambdaCaptureNames(*Value, Candidates, OutNames, OutBindingChanges);
					}
				};
				const auto VisitBlock = [&](const std::unique_ptr<FBlockStmt>& Value)
				{
					if (Value)
					{
						CollectLambdaCaptureNames(*Value, Candidates, OutNames, OutBindingChanges);
					}
				};
				switch (Statement.Kind)
				{
				case EStmtKind::Block:
					{
						auto BlockCandidates = Candidates;
						for (const auto& Child : static_cast<const FBlockStmt&>(Statement).Statements)
						{
							if (Child)
							{
								CollectLambdaCaptureNames(*Child, BlockCandidates, OutNames, OutBindingChanges);
							}
							if (Child && Child->Kind == EStmtKind::VarDecl)
							{
								BlockCandidates.erase(static_cast<const FVarDeclStmt&>(*Child).Name);
							}
							else if (Child && Child->Kind == EStmtKind::VarDeclGroup)
							{
								for (const auto& Declaration
										: static_cast<const FVarDeclGroupStmt&>(*Child).Declarations)
								{
									if (Declaration)
									{
										BlockCandidates.erase(Declaration->Name);
									}
								}
							}
						}
						return;
					}
				case EStmtKind::Expr:
					{
						VisitExpr(static_cast<const FExprStmt&>(Statement).Expression);
						return;
					}
				case EStmtKind::TypeAlias:
					{
						return;
					}
				case EStmtKind::VarDecl:
					{
						VisitExpr(static_cast<const FVarDeclStmt&>(Statement).Initializer);
						return;
					}
				case EStmtKind::VarDeclGroup:
					{
						auto GroupCandidates = Candidates;
						for (const auto& Declaration
							: static_cast<const FVarDeclGroupStmt&>(Statement).Declarations)
						{
							if (!Declaration)
							{
								continue;
							}
							if (Declaration->Initializer)
							{
								CollectLambdaCaptureNames(
										*Declaration->Initializer, GroupCandidates, OutNames, OutBindingChanges);
							}
							GroupCandidates.erase(Declaration->Name);
						}
						return;
					}
				case EStmtKind::Return:
					{
						VisitExpr(static_cast<const FReturnStmt&>(Statement).Value);
						return;
					}
				case EStmtKind::Throw:
					{
						VisitExpr(static_cast<const FThrowStmt&>(Statement).Value);
						return;
					}
				case EStmtKind::If:
					{
						VisitExpr(static_cast<const FIfStmt&>(Statement).Condition);
						VisitBlock(static_cast<const FIfStmt&>(Statement).ThenBranch);
						VisitBlock(static_cast<const FIfStmt&>(Statement).ElseBranch);
						return;
					}
				case EStmtKind::While:
					{
						VisitExpr(static_cast<const FWhileStmt&>(Statement).Condition);
						VisitBlock(static_cast<const FWhileStmt&>(Statement).Body);
						return;
					}
				case EStmtKind::DoWhile:
					{
						VisitBlock(static_cast<const FDoWhileStmt&>(Statement).Body);
						VisitExpr(static_cast<const FDoWhileStmt&>(Statement).Condition);
						return;
					}
				case EStmtKind::For:
					{
						const auto& For = static_cast<const FForStmt&>(Statement);
						auto LoopCandidates = Candidates;
						if (For.Init)
						{
							CollectLambdaCaptureNames(*For.Init, LoopCandidates, OutNames, OutBindingChanges);
						}
						if (For.Init && For.Init->Kind == EStmtKind::VarDecl)
						{
							LoopCandidates.erase(static_cast<const FVarDeclStmt&>(*For.Init).Name);
						}
						else if (For.Init && For.Init->Kind == EStmtKind::VarDeclGroup)
						{
							for (const auto& Declaration
									: static_cast<const FVarDeclGroupStmt&>(*For.Init).Declarations)
							{
								if (Declaration)
								{
									LoopCandidates.erase(Declaration->Name);
								}
							}
						}
						for (const auto& InitExpression : For.InitExpressions)
						{
							if (InitExpression)
							{
								CollectLambdaCaptureNames(
										*InitExpression, LoopCandidates, OutNames, OutBindingChanges);
							}
						}
						if (For.Condition)
						{
							CollectLambdaCaptureNames(*For.Condition, LoopCandidates, OutNames, OutBindingChanges);
						}
						if (For.Step)
						{
							CollectLambdaCaptureNames(*For.Step, LoopCandidates, OutNames, OutBindingChanges);
						}
						for (const auto& StepExpression : For.StepExpressions)
						{
							if (StepExpression)
							{
								CollectLambdaCaptureNames(
										*StepExpression, LoopCandidates, OutNames, OutBindingChanges);
							}
						}
						if (For.Body)
						{
							CollectLambdaCaptureNames(*For.Body, LoopCandidates, OutNames, OutBindingChanges);
						}
						return;
					}
				case EStmtKind::ForEach:
					{
						const auto& For = static_cast<const FForEachStmt&>(Statement);
						VisitExpr(For.Collection);
						auto LoopCandidates = Candidates;
						LoopCandidates.erase(For.ElementName);
						LoopCandidates.erase(For.KeyName);
						if (For.Body)
						{
							CollectLambdaCaptureNames(*For.Body, LoopCandidates, OutNames, OutBindingChanges);
						}
						return;
					}
				case EStmtKind::Switch:
					{
						const auto& Switch = static_cast<const FSwitchStmt&>(Statement);
						VisitExpr(Switch.Discriminant);
						for (const FSwitchCase& Case : Switch.Cases)
						{
							for (const auto& Label : Case.Labels)
							{
								VisitExpr(Label);
							}
							VisitBlock(Case.Body);
						}
						return;
					}
				case EStmtKind::Try:
					{
						const auto& Try = static_cast<const FTryStmt&>(Statement);
						VisitBlock(Try.TryBlock);
						for (const FCatchClause& Clause : Try.Catches)
						{
							auto CatchCandidates = Candidates;
							if (!Clause.Name.empty())
							{
								CatchCandidates.erase(Clause.Name);
							}
							if (Clause.Body)
							{
								CollectLambdaCaptureNames(*Clause.Body, CatchCandidates, OutNames, OutBindingChanges);
							}
						}
						VisitBlock(Try.FinallyBlock);
						return;
					}
				default:
					return;
				}
			}
