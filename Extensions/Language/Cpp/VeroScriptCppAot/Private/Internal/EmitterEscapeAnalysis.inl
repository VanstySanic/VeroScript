// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

			bool ExprContainsIdentifier(
				const FExpr& Expr,
				const std::string_view Name) const
			{
				switch (Expr.Kind)
				{
				case EExprKind::Identifier:
					return static_cast<const FIdentifierExpr&>(Expr).Name == Name;
				case EExprKind::Unary:
					return ExprContainsIdentifier(
						*static_cast<const FUnaryExpr&>(Expr).Operand, Name);
				case EExprKind::Binary:
					{
						const auto& E = static_cast<const FBinaryExpr&>(Expr);
						return ExprContainsIdentifier(*E.Left, Name)
							|| ExprContainsIdentifier(*E.Right, Name);
					}
				case EExprKind::Assign:
					{
						const auto& E = static_cast<const FAssignExpr&>(Expr);
						return ExprContainsIdentifier(*E.Target, Name)
							|| ExprContainsIdentifier(*E.Value, Name);
					}
				case EExprKind::Call:
					{
						const auto& E = static_cast<const FCallExpr&>(Expr);
						if (E.CalleeExpr && ExprContainsIdentifier(*E.CalleeExpr, Name))
						{
							return true;
						}
						for (const FExpr* Arg : E.GetArguments())
						{
							if (Arg && ExprContainsIdentifier(*Arg, Name))
							{
								return true;
							}
						}
						return false;
					}
				case EExprKind::MemberAccess:
					return ExprContainsIdentifier(
						*static_cast<const FMemberAccessExpr&>(Expr).Object, Name);
				case EExprKind::MethodCall:
					{
						const auto& E = static_cast<const FMethodCallExpr&>(Expr);
						if (ExprContainsIdentifier(*E.Object, Name))
						{
							return true;
						}
						for (const FExpr* Arg : E.GetArguments())
						{
							if (Arg && ExprContainsIdentifier(*Arg, Name))
							{
								return true;
							}
						}
						return false;
					}
				case EExprKind::IndexAccess:
					{
						const auto& E = static_cast<const FIndexAccessExpr&>(Expr);
						return ExprContainsIdentifier(*E.Object, Name)
							|| ExprContainsIdentifier(*E.Index, Name);
					}
				case EExprKind::ArrayInit:
					{
						for (const auto& Element : static_cast<const FArrayInitExpr&>(Expr).Elements)
						{
							if (Element && ExprContainsIdentifier(*Element, Name))
							{
								return true;
							}
						}
						return false;
					}
				case EExprKind::MapInit:
					{
						for (const FMapInitEntry& Entry : static_cast<const FMapInitExpr&>(Expr).Entries)
						{
							if ((Entry.Key && ExprContainsIdentifier(*Entry.Key, Name))
							|| (Entry.Value && ExprContainsIdentifier(*Entry.Value, Name)))
							{
								return true;
							}
						}
						return false;
					}
				case EExprKind::Conditional:
					{
						const auto& E = static_cast<const FConditionalExpr&>(Expr);
						return ExprContainsIdentifier(*E.Condition, Name)
							|| ExprContainsIdentifier(*E.ThenExpr, Name)
							|| ExprContainsIdentifier(*E.ElseExpr, Name);
					}
				case EExprKind::Ref:
					return ExprContainsIdentifier(
						*static_cast<const FRefExpr&>(Expr).Operand, Name);
				case EExprKind::Lambda:
					{
						const auto& E = static_cast<const FLambdaExpr&>(Expr);
						return (E.ExpressionBody
								&& ExprContainsIdentifier(*E.ExpressionBody, Name))
							|| (E.Body && StmtContainsIdentifier(*E.Body, Name));
					}
				default:
					return false;
				}
			}

			bool IsDirectStackReceiver(
				const FExpr& Expr,
				const std::string_view Name) const
			{
				return Expr.Kind == EExprKind::Identifier
					&& static_cast<const FIdentifierExpr&>(Expr).Name == Name;
			}

			bool IsStackAllocatedClassReceiver(const FExpr& Expr) const
			{
				if (Expr.Kind != EExprKind::Identifier)
				{
					return false;
				}
				return StackClassLocals.find(
					static_cast<const FIdentifierExpr&>(Expr).Name)
					!= StackClassLocals.end();
			}

			bool IsDirectObjectReceiver(
				const FExpr& Expr,
				const bool bIsThis) const
			{
				if (RetainedReferenceReceivers.count(&Expr)) return false;
				if (bIsThis && CapturedClassThisLambdaDepth != 0u)
				{
					// EmitExpr(This) is the retained shared handle inside such a lambda.
					return false;
				}
				if (bIsThis && CapturedStructThisLambdaDepth != 0u)
				{
					return true;
				}
				return ((bMemberMethodBody || bCtorInitDirectSelf) && bIsThis)
					|| IsStackAllocatedClassReceiver(Expr);
			}

			bool IsNonEscapingStackLocalExpr(
				const FExpr& Expr,
				const std::string_view Name) const
			{
				switch (Expr.Kind)
				{
				case EExprKind::Identifier:
					return static_cast<const FIdentifierExpr&>(Expr).Name != Name;
				case EExprKind::MemberAccess:
					{
						const auto& E = static_cast<const FMemberAccessExpr&>(Expr);
						return IsDirectStackReceiver(*E.Object, Name)
							|| IsNonEscapingStackLocalExpr(*E.Object, Name);
					}
				case EExprKind::MethodCall:
					{
						const auto& E = static_cast<const FMethodCallExpr&>(Expr);
						if (!IsDirectStackReceiver(*E.Object, Name)
							&& !IsNonEscapingStackLocalExpr(*E.Object, Name))
						{
							return false;
						}
						for (const FExpr* Arg : E.GetArguments())
						{
							// A callee may bind a ref/out parameter or retain a reference to a
							// field. Without an interprocedural effect summary, any occurrence
							// outside the direct receiver is an escape.
							if (Arg && ExprContainsIdentifier(*Arg, Name))
							{
								return false;
							}
						}
						return true;
					}
				case EExprKind::IndexAccess:
					{
						const auto& E = static_cast<const FIndexAccessExpr&>(Expr);
						const bool bSafeObject = IsDirectStackReceiver(*E.Object, Name)
							|| IsNonEscapingStackLocalExpr(*E.Object, Name);
						return bSafeObject && IsNonEscapingStackLocalExpr(*E.Index, Name);
					}
				case EExprKind::Ref:
					// A reference to the object or one of its fields may outlive the local.
					return !ExprContainsIdentifier(Expr, Name);
				case EExprKind::Unary:
					return IsNonEscapingStackLocalExpr(
						*static_cast<const FUnaryExpr&>(Expr).Operand, Name);
				case EExprKind::Binary:
					{
						const auto& E = static_cast<const FBinaryExpr&>(Expr);
						return IsNonEscapingStackLocalExpr(*E.Left, Name)
							&& IsNonEscapingStackLocalExpr(*E.Right, Name);
					}
				case EExprKind::Assign:
					{
						const auto& E = static_cast<const FAssignExpr&>(Expr);
						return IsNonEscapingStackLocalExpr(*E.Target, Name)
							&& IsNonEscapingStackLocalExpr(*E.Value, Name);
					}
				case EExprKind::Call:
					{
						const auto& E = static_cast<const FCallExpr&>(Expr);
						if (E.CalleeExpr && ExprContainsIdentifier(*E.CalleeExpr, Name))
						{
							return false;
						}
						for (const FExpr* Arg : E.GetArguments())
						{
							if (Arg && ExprContainsIdentifier(*Arg, Name))
							{
								return false;
							}
						}
						return true;
					}
				case EExprKind::ArrayInit:
					{
						for (const auto& Element : static_cast<const FArrayInitExpr&>(Expr).Elements)
						{
							if (Element && !IsNonEscapingStackLocalExpr(*Element, Name))
							{
								return false;
							}
						}
						return true;
					}
				case EExprKind::MapInit:
					{
						for (const FMapInitEntry& Entry : static_cast<const FMapInitExpr&>(Expr).Entries)
						{
							if ((Entry.Key && !IsNonEscapingStackLocalExpr(*Entry.Key, Name))
							|| (Entry.Value && !IsNonEscapingStackLocalExpr(*Entry.Value, Name)))
							{
								return false;
							}
						}
						return true;
					}
				case EExprKind::Conditional:
					{
						const auto& E = static_cast<const FConditionalExpr&>(Expr);
						return IsNonEscapingStackLocalExpr(*E.Condition, Name)
							&& IsNonEscapingStackLocalExpr(*E.ThenExpr, Name)
							&& IsNonEscapingStackLocalExpr(*E.ElseExpr, Name);
					}
				case EExprKind::Lambda:
					return !ExprContainsIdentifier(Expr, Name);
				default:
					return true;
				}
			}

			bool StmtContainsIdentifier(
				const FStmt& Stmt,
				const std::string_view Name) const
			{
				return !IsNonEscapingStackLocalStmt(Stmt, Name, false);
			}

			// Stack allocation is only valid when the generated C++ object cannot make
			// its script identity observable outside the local scope.  Merely checking
			// call sites is not sufficient: an apparently scalar method can return a
			// closure that captures implicit `this`, or pass `this` through another
			// method.  Keep this proof deliberately structural and conservative.  A
			// future effect/purity annotation can widen it without weakening lifetime
			// semantics.
			bool IsStackSafeClassExpr(const FExpr& Expr) const
			{
				switch (Expr.Kind)
				{
				case EExprKind::IntegerLiteral:
				case EExprKind::CharacterLiteral:
				case EExprKind::FloatLiteral:
				case EExprKind::StringLiteral:
				case EExprKind::WStringLiteral:
				case EExprKind::BooleanLiteral:
				case EExprKind::NullptrLiteral:
					return true;
				case EExprKind::Identifier:
					return static_cast<const FIdentifierExpr&>(Expr).Name != "this";
				case EExprKind::This:
				case EExprKind::Ref:
				case EExprKind::Call:
				case EExprKind::MethodCall:
				case EExprKind::Lambda:
					return false;
				case EExprKind::Unary:
					return IsStackSafeClassExpr(
						*static_cast<const FUnaryExpr&>(Expr).Operand);
				case EExprKind::Binary:
					{
						const auto& E = static_cast<const FBinaryExpr&>(Expr);
						return IsStackSafeClassExpr(*E.Left)
							&& IsStackSafeClassExpr(*E.Right);
					}
				case EExprKind::Assign:
					{
						const auto& E = static_cast<const FAssignExpr&>(Expr);
						return IsStackSafeClassExpr(*E.Target)
							&& IsStackSafeClassExpr(*E.Value);
					}
				case EExprKind::MemberAccess:
					return IsStackSafeClassExpr(
						*static_cast<const FMemberAccessExpr&>(Expr).Object);
				case EExprKind::IndexAccess:
					{
						const auto& E = static_cast<const FIndexAccessExpr&>(Expr);
						return IsStackSafeClassExpr(*E.Object)
							&& IsStackSafeClassExpr(*E.Index);
					}
				case EExprKind::ArrayInit:
					{
						for (const auto& Element : static_cast<const FArrayInitExpr&>(Expr).Elements)
						{
							if (Element && !IsStackSafeClassExpr(*Element))
							{
								return false;
							}
						}
						return true;
					}
				case EExprKind::MapInit:
					{
						for (const FMapInitEntry& Entry : static_cast<const FMapInitExpr&>(Expr).Entries)
						{
							if ((Entry.Key && !IsStackSafeClassExpr(*Entry.Key))
							|| (Entry.Value && !IsStackSafeClassExpr(*Entry.Value)))
							{
								return false;
							}
						}
						return true;
					}
				case EExprKind::Conditional:
					{
						const auto& E = static_cast<const FConditionalExpr&>(Expr);
						return IsStackSafeClassExpr(*E.Condition)
							&& IsStackSafeClassExpr(*E.ThenExpr)
							&& IsStackSafeClassExpr(*E.ElseExpr);
					}
				default:
					{
						break;
					}
				}
				return false;
			}

			bool IsStackSafeClassStmt(const FStmt& Stmt) const
			{
				const auto SafeExpr = [&](const std::unique_ptr<FExpr>& Expr)
				{
					return !Expr || IsStackSafeClassExpr(*Expr);
				};
				switch (Stmt.Kind)
				{
				case EStmtKind::Expr:
					return SafeExpr(static_cast<const FExprStmt&>(Stmt).Expression);
				case EStmtKind::Block:
					{
						for (const auto& Child : static_cast<const FBlockStmt&>(Stmt).Statements)
						{
							if (Child && !IsStackSafeClassStmt(*Child))
							{
								return false;
							}
						}
						return true;
					}
				case EStmtKind::VarDecl:
					return !static_cast<const FVarDeclStmt&>(Stmt).bIsRef
						&& SafeExpr(static_cast<const FVarDeclStmt&>(Stmt).Initializer);
				case EStmtKind::VarDeclGroup:
					{
						for (const std::unique_ptr<FVarDeclStmt>& Declaration :
						static_cast<const FVarDeclGroupStmt&>(Stmt).Declarations)
						{
							if (!Declaration || !IsStackSafeClassStmt(*Declaration))
							{
								return false;
							}
						}
						return true;
					}
				case EStmtKind::TypeAlias:
				case EStmtKind::Break:
				case EStmtKind::Continue:
				case EStmtKind::Fallthrough:
				case EStmtKind::Goto:
				case EStmtKind::Label:
					return true;
				case EStmtKind::Return:
					return SafeExpr(static_cast<const FReturnStmt&>(Stmt).Value);
				case EStmtKind::Throw:
					return SafeExpr(static_cast<const FThrowStmt&>(Stmt).Value);
				case EStmtKind::If:
					{
						const auto& S = static_cast<const FIfStmt&>(Stmt);
						return SafeExpr(S.Condition)
							&& (!S.ThenBranch || IsStackSafeClassStmt(*S.ThenBranch))
							&& (!S.ElseBranch || IsStackSafeClassStmt(*S.ElseBranch));
					}
				case EStmtKind::While:
					{
						const auto& S = static_cast<const FWhileStmt&>(Stmt);
						return SafeExpr(S.Condition)
							&& (!S.Body || IsStackSafeClassStmt(*S.Body));
					}
				case EStmtKind::DoWhile:
					{
						const auto& S = static_cast<const FDoWhileStmt&>(Stmt);
						return (!S.Body || IsStackSafeClassStmt(*S.Body))
							&& SafeExpr(S.Condition);
					}
				case EStmtKind::For:
					{
						const auto& S = static_cast<const FForStmt&>(Stmt);
						if (S.Init && !IsStackSafeClassStmt(*S.Init))
						{
							return false;
						}
						for (const std::unique_ptr<FExpr>& Expr : S.InitExpressions)
						{
							if (!SafeExpr(Expr))
							{
								return false;
							}
						}
						if (!SafeExpr(S.Condition) || !SafeExpr(S.Step))
						{
							return false;
						}
						for (const std::unique_ptr<FExpr>& Expr : S.StepExpressions)
						{
							if (!SafeExpr(Expr))
							{
								return false;
							}
						}
						return !S.Body || IsStackSafeClassStmt(*S.Body);
					}
				case EStmtKind::ForEach:
					{
						const auto& S = static_cast<const FForEachStmt&>(Stmt);
						return SafeExpr(S.Collection)
							&& (!S.Body || IsStackSafeClassStmt(*S.Body));
					}
				case EStmtKind::Switch:
					{
						const auto& S = static_cast<const FSwitchStmt&>(Stmt);
						if (!SafeExpr(S.Discriminant))
						{
							return false;
						}
						for (const FSwitchCase& Case : S.Cases)
						{
							for (const auto& Label : Case.Labels)
							{
								if (!SafeExpr(Label))
								{
									return false;
								}
							}
							if (Case.Body && !IsStackSafeClassStmt(*Case.Body))
							{
								return false;
							}
						}
						return true;
					}
				case EStmtKind::Try:
					{
						const auto& S = static_cast<const FTryStmt&>(Stmt);
						if (S.TryBlock && !IsStackSafeClassStmt(*S.TryBlock))
						{
							return false;
						}
						for (const FCatchClause& Catch : S.Catches)
						{
							if (Catch.Body && !IsStackSafeClassStmt(*Catch.Body))
							{
								return false;
							}
						}
						return !S.FinallyBlock || IsStackSafeClassStmt(*S.FinallyBlock);
					}
				default:
					{
						break;
					}
				}
				return false;
			}

			bool IsStackSafeClassType(
				const FUserTypeInfo& Type,
				std::unordered_set<std::string>& Visiting) const
			{
				if (!Visiting.insert(Type.Name).second)
				{
					return true;
				}
				if (Type.Fields)
				{
					for (const FFieldDecl& Field : *Type.Fields)
					{
						if (!Field.bIsStatic && Field.DefaultValue
							&& !IsStackSafeClassExpr(*Field.DefaultValue))
						{
							return false;
						}
					}
				}
				if (Type.Methods)
				{
					for (const FMethodDecl& Method : *Type.Methods)
					{
						if (Method.bVirtual || Method.bOverride || Method.bPureVirtual
							|| Method.ReturnType.rfind("ref ", 0) == 0
							|| Method.ReturnType.rfind("const ref ", 0) == 0
							|| (!Method.bIsStatic && Method.Body
								&& !IsStackSafeClassStmt(*Method.Body)))
						{
							return false;
						}
					}
				}
				if (Type.Constructors)
				{
					for (const FConstructorDecl& Constructor : *Type.Constructors)
					{
						for (const FBaseCtorInit& Base : Constructor.BaseInits)
						{
							for (const auto& Arg : Base.Args)
							{
								if (Arg && !IsStackSafeClassExpr(*Arg))
								{
									return false;
								}
							}
						}
						for (const FFieldCtorInit& Field : Constructor.FieldInits)
						{
							for (const auto& Arg : Field.Args)
							{
								if (Arg && !IsStackSafeClassExpr(*Arg))
								{
									return false;
								}
							}
						}
						if (Constructor.Body && !IsStackSafeClassStmt(*Constructor.Body))
						{
							return false;
						}
					}
				}
				if (Type.Destructor && Type.Destructor->Body
					&& !IsStackSafeClassStmt(*Type.Destructor->Body))
				{
					return false;
				}
				for (const std::string& BaseName : Type.BaseTypeNames)
				{
					const auto Base = UserTypes.find(
						ResolveAliasChain(ApplyTypeSubst(BaseName)));
					if (Base == UserTypes.end()
						|| !IsStackSafeClassType(Base->second, Visiting))
					{
						return false;
					}
				}
				return true;
			}

			bool IsNonEscapingStackLocalStmt(
				const FStmt& Stmt,
				const std::string_view Name,
				const bool bAllowMemberReceivers = true) const
			{
				const auto SafeExpr = [&](const std::unique_ptr<FExpr>& Expr)
				{
					if (!Expr)
					{
						return true;
					}
					return bAllowMemberReceivers
						? IsNonEscapingStackLocalExpr(*Expr, Name)
						: !ExprContainsIdentifier(*Expr, Name);
				};
				switch (Stmt.Kind)
				{
				case EStmtKind::Expr:
					return SafeExpr(static_cast<const FExprStmt&>(Stmt).Expression);
				case EStmtKind::Block:
					{
						for (const auto& Child : static_cast<const FBlockStmt&>(Stmt).Statements)
						{
							if (Child && !IsNonEscapingStackLocalStmt(
								*Child, Name, bAllowMemberReceivers))
							{
								return false;
							}
						}
						return true;
					}
				case EStmtKind::VarDecl:
					{
						const auto& Var = static_cast<const FVarDeclStmt&>(Stmt);
						// A reference alias can hide the object's field from all later
						// name-based checks and then escape it independently.
						return !(Var.bIsRef && Var.Initializer
								&& ExprContainsIdentifier(*Var.Initializer, Name))
							&& SafeExpr(Var.Initializer);
					}
				case EStmtKind::VarDeclGroup:
					{
						for (const std::unique_ptr<FVarDeclStmt>& Declaration :
						static_cast<const FVarDeclGroupStmt&>(Stmt).Declarations)
						{
							if (!Declaration || !IsNonEscapingStackLocalStmt(
								*Declaration, Name, bAllowMemberReceivers))
							{
								return false;
							}
						}
						return true;
					}
				case EStmtKind::Return:
					{
						const auto& Return = static_cast<const FReturnStmt&>(Stmt);
						const bool bReferenceReturn =
							ActiveReturnType.rfind("ref ", 0) == 0
							|| ActiveReturnType.rfind("const ref ", 0) == 0;
						// A by-reference return may outlive the object. Ordinary by-value
						// scalar/member results remain safe and are handled by the normal
						// expression escape proof.
						return !(bReferenceReturn && Return.Value
								&& ExprContainsIdentifier(*Return.Value, Name))
							&& SafeExpr(Return.Value);
					}
				case EStmtKind::Throw:
					return SafeExpr(static_cast<const FThrowStmt&>(Stmt).Value);
				case EStmtKind::If:
					{
						const auto& S = static_cast<const FIfStmt&>(Stmt);
						return SafeExpr(S.Condition)
							&& (!S.ThenBranch || IsNonEscapingStackLocalStmt(
								*S.ThenBranch, Name, bAllowMemberReceivers))
							&& (!S.ElseBranch || IsNonEscapingStackLocalStmt(
								*S.ElseBranch, Name, bAllowMemberReceivers));
					}
				case EStmtKind::While:
					{
						const auto& S = static_cast<const FWhileStmt&>(Stmt);
						return SafeExpr(S.Condition) && (!S.Body
							|| IsNonEscapingStackLocalStmt(*S.Body, Name, bAllowMemberReceivers));
					}
				case EStmtKind::DoWhile:
					{
						const auto& S = static_cast<const FDoWhileStmt&>(Stmt);
						return (!S.Body || IsNonEscapingStackLocalStmt(
								*S.Body, Name, bAllowMemberReceivers))
							&& SafeExpr(S.Condition);
					}
				case EStmtKind::For:
					{
						const auto& S = static_cast<const FForStmt&>(Stmt);
						if (S.Init && !IsNonEscapingStackLocalStmt(
								*S.Init, Name, bAllowMemberReceivers))
						{
							return false;
						}
						for (const std::unique_ptr<FExpr>& Expr : S.InitExpressions)
						{
							if (!SafeExpr(Expr))
							{
								return false;
							}
						}
						if (!SafeExpr(S.Condition) || !SafeExpr(S.Step))
						{
							return false;
						}
						for (const std::unique_ptr<FExpr>& Expr : S.StepExpressions)
						{
							if (!SafeExpr(Expr))
							{
								return false;
							}
						}
						return !S.Body || IsNonEscapingStackLocalStmt(
							*S.Body, Name, bAllowMemberReceivers);
					}
				case EStmtKind::ForEach:
					{
						const auto& S = static_cast<const FForEachStmt&>(Stmt);
						return SafeExpr(S.Collection) && (!S.Body
							|| IsNonEscapingStackLocalStmt(*S.Body, Name, bAllowMemberReceivers));
					}
				case EStmtKind::Switch:
					{
						const auto& S = static_cast<const FSwitchStmt&>(Stmt);
						if (!SafeExpr(S.Discriminant))
						{
							return false;
						}
						for (const FSwitchCase& Case : S.Cases)
						{
							for (const auto& Label : Case.Labels)
							{
								if (!SafeExpr(Label))
								{
									return false;
								}
							}
							if (Case.Body && !IsNonEscapingStackLocalStmt(
									*Case.Body, Name, bAllowMemberReceivers))
							{
								return false;
							}
						}
						return true;
					}
				case EStmtKind::Try:
					{
						const auto& S = static_cast<const FTryStmt&>(Stmt);
						if (S.TryBlock && !IsNonEscapingStackLocalStmt(
								*S.TryBlock, Name, bAllowMemberReceivers))
						{
							return false;
						}
						for (const FCatchClause& Catch : S.Catches)
						{
							if (Catch.Body && !IsNonEscapingStackLocalStmt(
									*Catch.Body, Name, bAllowMemberReceivers))
							{
								return false;
							}
						}
						return !S.FinallyBlock || IsNonEscapingStackLocalStmt(
							*S.FinallyBlock, Name, bAllowMemberReceivers);
					}
				default:
					return true;
				}
			}

			bool CanUseStackClassLocal(
				const FVarDeclStmt& Var,
				const FBlockStmt& Block,
				const std::size_t StatementIndex,
				const FVarDeclGroupStmt* const Group = nullptr,
				const std::size_t DeclarationIndex = 0u)
			{
				if (Var.bIsRef || Var.bIsConst || !Var.Initializer
					|| Var.Initializer->Kind != EExprKind::Call
					|| ExprContainsIdentifier(*Var.Initializer, Var.Name))
				{
					return false;
				}
				const std::string TypeName = ResolveAliasChain(ApplyTypeSubst(Var.TypeName));
				const auto TypeIt = UserTypes.find(TypeName);
				if (TypeIt == UserTypes.end() || !TypeIt->second.bIsClass
					|| TypeIt->second.bIsInterface || TypeIt->second.bIsAbstract
					|| !TypeIt->second.OpenGenericName.empty()
					|| TypeName.find('<') != std::string::npos)
				{
					return false;
				}
				std::unordered_set<std::string> Visiting;
				if (!IsStackSafeClassType(TypeIt->second, Visiting))
				{
					return false;
				}
				const auto& Call = static_cast<const FCallExpr&>(*Var.Initializer);
				if (Call.CalleeExpr || !Call.TypeArgs.empty()
					|| ResolveAliasChain(ApplyTypeSubst(Call.Callee)) != TypeName)
				{
					return false;
				}
				// A declaration group is one block statement, but its declarators execute
				// left-to-right. Later initializers can therefore make an earlier class
				// local escape before the next block statement is reached.
				if (Group)
				{
					for (std::size_t Index = DeclarationIndex + 1u;
						Index < Group->Declarations.size(); ++Index)
					{
						if (Group->Declarations[Index]
							&& !IsNonEscapingStackLocalStmt(
								*Group->Declarations[Index], Var.Name))
						{
							return false;
						}
					}
				}
				for (std::size_t Index = StatementIndex + 1u;
					Index < Block.Statements.size(); ++Index)
				{
					if (Block.Statements[Index]
						&& !IsNonEscapingStackLocalStmt(
							*Block.Statements[Index], Var.Name))
					{
						return false;
					}
				}
				return true;
			}
