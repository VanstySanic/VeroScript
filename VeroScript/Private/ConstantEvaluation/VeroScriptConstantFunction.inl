// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

			// Included inside the exact scalar evaluator: arithmetic has a single implementation.
			struct FConstantLocal
			{
				FResult Value;
				bool bConst = false, bConstantExpression = false;
				const FExpr *Initializer = nullptr;
				std::uint64_t Order = 0;
			};
			struct FConstantFrame
			{
				std::vector<std::unordered_map<std::string, FConstantLocal>> Scopes;
				FTemplateValueType ReturnType;
				const FTemplateEvaluationContext *DeclarationContext = nullptr;
				bool bValidation = false, bRequiredInitializer = false, bDefaultArguments = false;
				std::uint32_t LoopDepth = 0, SwitchDepth = 0;
				std::uint64_t MemoryBytes = 0;
				std::uint64_t NextOrder = 1, LookupOrderLimit = std::numeric_limits<std::uint64_t>::max();
			};
			enum class EFlow
			{
				Next,
				Return,
				Break,
				Continue,
				Fallthrough,
				Error
			};
			struct FFlow
			{
				EFlow Kind = EFlow::Next;
				FResult Result;
			};

			void PopScope(FConstantFrame &Frame)
			{
				for (const auto &Entry : Frame.Scopes.back())
				{
					const auto Bytes = 512 + Entry.first.size() + Entry.second.Value.Value.Type.Name.size();
					EvaluationState->MemoryBytes -= Bytes;
					Frame.MemoryBytes -= Bytes;
				}
				Frame.Scopes.pop_back();
			}
			bool AddLocal(FConstantFrame &Frame, const std::string &Name, FConstantLocal Local)
			{
				const auto Bytes = 512 + Name.size() + Local.Value.Value.Type.Name.size();
				if (Bytes > EvaluationState->Limits.MaxMemoryBytes || EvaluationState->MemoryBytes > EvaluationState->Limits.MaxMemoryBytes - Bytes)
				{
					Stop = EStatus::LimitExceeded;
					return false;
				}
				if (!Frame.Scopes.back().emplace(Name, std::move(Local)).second) return false;
				Frame.MemoryBytes += Bytes;
				EvaluationState->MemoryBytes += Bytes;
				return true;
			}

			FConstantLocal *FindLocal(const std::string &Name)
			{
				if (Frames.empty() || Frames.back().bDefaultArguments || Frames.back().DeclarationContext != Context) return nullptr;
				for (auto Scope = Frames.back().Scopes.rbegin(); Scope != Frames.back().Scopes.rend(); ++Scope)
				{
					if (auto Found = Scope->find(Name); Found != Scope->end() && Found->second.Order <= Frames.back().LookupOrderLimit) return &Found->second;
				}
				return nullptr;
			}

			static bool Usable(const FResult &Value)
			{
				return Value.Succeeded() || Value.Status == EStatus::Dependent;
			}

			FResult Assign(const FAssignExpr &Expression, bool Shape, std::uint32_t Depth)
			{
				if (!Expression.Target || Expression.Target->Kind != EExprKind::Identifier || !Expression.Value)
				{
					return Failure(EStatus::Invalid, "constexpr assignment requires a local scalar", Expression.Location);
				}
				auto *Local = FindLocal(static_cast<const FIdentifierExpr &>(*Expression.Target).Name);
				if (!Local || Local->bConst || Frames.back().bRequiredInitializer)
				{
					return Failure(EStatus::Invalid, "constexpr cannot modify const or external state", Expression.Location);
				}
				const auto Previous = Local->Value;
				auto Value = Visit(*Expression.Value, Shape, Depth + 1);
				if (!Usable(Value)) return Value;
				const bool Dependent = Previous.Status == EStatus::Dependent || Value.Status == EStatus::Dependent;
				if (Expression.bIsCompound)
				{
					FBinaryExpr BinaryExpression(Expression.CompoundOp, MakeConstantExpression(Previous.Value),
					    MakeConstantExpression(Value.Value));
					BinaryExpression.Location = Expression.Location;
					Value = Binary(BinaryExpression, Shape, Depth + 1);
				}
				if (!Usable(Value)) return Value;
				if (Shape)
				{
					// Type probes inspect both conditional branches without executing either.
					// Validation frames still propagate their unknown parameter/local values.
					auto Result = Constant({Previous.Value.Type, 0}, Expression.Location);
					if (Dependent)
					{
						Result.Status = EStatus::Dependent;
					}
					return Result;
				}
				Value = ConvertTemplateConstant(Value.Value, Previous.Value.Type, false, Expression.Location);
				if (!Usable(Value)) return Value;
				Local->Value = Value;
				return Expression.bFromPostfixIncDec ? Previous : Value;
			}

			FResult Call(const FExpr &Expression, bool Shape, std::uint32_t Depth)
			{
				const auto ArgumentCount = Expression.Kind == EExprKind::Call
				    ? static_cast<const FCallExpr &>(Expression).GetArguments().size()
				    : static_cast<const FMethodCallExpr &>(Expression).Arguments.size();
				if (ArgumentCount > std::numeric_limits<std::uint8_t>::max())
				{
					return Failure(EStatus::Invalid, "constexpr call supports at most 255 arguments", Expression.Location);
				}
				if (!Context->ResolveCall)
				{
					return Failure(EStatus::NotConstant, "call is not bound to a constexpr function", Expression.Location);
				}
				std::vector<FResult> Arguments;
				if (Expression.Kind == EExprKind::Call)
				{
					const auto &Call = static_cast<const FCallExpr &>(Expression);
					if (Call.CalleeExpr || FindLocal(Call.Callee)) return Failure(EStatus::NotConstant, "indirect constexpr call is unsupported", Expression.Location);
					for (const auto &Argument : Call.GetArguments())
					{
						if (!Argument) return Failure(EStatus::Invalid, "missing constexpr argument", Expression.Location);
						Arguments.push_back(Visit(*Argument, Shape, Depth + 1));
						if (!Usable(Arguments.back())) return Arguments.back();
					}
				}
				else
				{
					const auto &MethodCall = static_cast<const FMethodCallExpr &>(Expression);
					if (MethodCall.Object && MethodCall.Object->Kind == EExprKind::Identifier && FindLocal(static_cast<const FIdentifierExpr &>(*MethodCall.Object).Name))
					{
						return Failure(EStatus::NotConstant, "constexpr requires a type or namespace qualifier", Expression.Location);
					}
					for (const auto &Argument : static_cast<const FMethodCallExpr &>(Expression).Arguments)
					{
						if (!Argument) return Failure(EStatus::Invalid, "missing constexpr argument", Expression.Location);
						Arguments.push_back(Visit(*Argument, Shape, Depth + 1));
						if (!Usable(Arguments.back())) return Arguments.back();
					}
				}
				auto Function = Context->ResolveCall(Expression, Arguments);
				if (Function.Status != EStatus::Constant)
				{
					return Failure(Function.Status, Function.Reason.empty() ? "selected function is not constexpr" : Function.Reason, Expression.Location);
				}
				if (!Function.Body || !ValidType(Function.ReturnType))
				{
					return Failure(EStatus::Invalid, "constexpr function has no scalar return type or body", Expression.Location);
				}
				for (std::size_t I = 0; I < Arguments.size(); ++I)
				{
					if (I >= Function.ParameterTypes.size()) return Failure(EStatus::Invalid, "too many constexpr arguments", Expression.Location);
					const auto Conversion = ConvertTemplateConstant({Arguments[I].Value.Type, 0}, Function.ParameterTypes[I], false, Expression.Location);
					if (!Conversion.Succeeded()) return Conversion;
				}
				if (Shape)
				{
					auto Result = Constant({Function.ReturnType, 0}, Expression.Location);
					for (const auto &Argument : Arguments)
					{
						if (Argument.Status == EStatus::Dependent)
						{
							Result.Status = EStatus::Dependent;
						}
					}
					return Result;
				}
				if (ValidatedFunctions.find(Function.Identity) == ValidatedFunctions.end())
				{
					auto Validation = ExecuteFunction(Function, {}, true, Depth + 1);
					if (!Validation.Succeeded())
					{
						Validation.Reason += " [in " + Function.Identity + "]";
						return Validation;
					}
					if (CanCache(Function.Identity.size() + 64))
					{
						ValidatedFunctions.insert(Function.Identity);
					}
				}
				auto Result = ExecuteFunction(Function, Arguments, false, Depth + 1);
				if (!Result.Succeeded())
				{
					Result.Reason += " [in " + Function.Identity + "]";
				}
				return Result;
			}

			FResult ExecuteFunction(const FConstantFunction &Function, const std::vector<FResult> &Arguments,
			    bool Validation, std::uint32_t Depth)
			{
				if (!Function.Body || Function.ParameterNames.size() != Function.ParameterTypes.size() ||
				    Function.Identity.empty() || !ValidType(Function.ReturnType))
				{
					return Failure(EStatus::Invalid, "invalid constexpr function view", {});
				}
				if (EvaluationState->CallDepth >= EvaluationState->Limits.MaxCallDepth || EvaluationState->NativeDepth >= 256 || Depth > 256)
				{
					return Failure(EStatus::LimitExceeded, "constexpr call depth exceeded", Function.Body->Location);
				}
				std::string Key = Function.Identity;
				for (const auto &Argument : Arguments)
				{
					Key += EncodeTemplateArgument({ETemplateParameterKind::Value, {}, Argument.Value});
				}
				if (!Validation)
				{
					if (const auto Found = CallCache.find(Key); Found != CallCache.end()) return Found->second;
				}
				if (!Validation && !ActiveCalls.insert(Key).second)
				{
					return Failure(EStatus::Invalid, "cyclic constexpr call with identical arguments", Function.Body->Location);
				}
				const auto *PreviousContext = Context;
				Context = Function.DeclarationContext ? Function.DeclarationContext : Context;
				Frames.emplace_back();
				auto &Frame = Frames.back();
				Frame.ReturnType = Function.ReturnType;
				Frame.DeclarationContext = Context;
				Frame.bValidation = Validation;
				Frame.Scopes.emplace_back();
				++EvaluationState->CallDepth;
				struct FRestore
				{
					FEvaluator &Evaluator;
					const FTemplateEvaluationContext *Previous;
					const std::string &Key;
					bool Validation;
					~FRestore()
					{
						--Evaluator.EvaluationState->CallDepth;
						Evaluator.EvaluationState->MemoryBytes -= Evaluator.Frames.back().MemoryBytes;
						Evaluator.Frames.pop_back();
						Evaluator.Context = Previous;
						if (!Validation)
						{
							Evaluator.ActiveCalls.erase(Key);
						}
					}
				} Restore{*this, PreviousContext, Key, Validation};
				for (std::size_t I = 0; I < Function.ParameterNames.size(); ++I)
				{
					FResult Value;
					if (Validation)
					{
						Value.Status = EStatus::Dependent;
						Value.Value.Type = Function.ParameterTypes[I];
					}
					else if (I < Arguments.size())
					{
						Value = Arguments[I];
					}
					else if (I < Function.Defaults.size() && Function.Defaults[I])
					{
						const auto *BodyContext = Context;
						Context = Function.DefaultContext ? Function.DefaultContext : Context;
						Frame.bDefaultArguments = true;
						struct FRestoreDefaultContext
						{
							FEvaluator &Evaluator;
							const FTemplateEvaluationContext *Previous;
							FConstantFrame &Frame;
							~FRestoreDefaultContext()
							{
								Evaluator.Context = Previous;
								Frame.bDefaultArguments = false;
							}
						} DefaultScope{*this, BodyContext, Frame};
						Value = Visit(*Function.Defaults[I], false, Depth + 1);
					}
					else
					{
						return Failure(EStatus::Invalid, "missing constexpr argument", Function.Body->Location);
					}
					if (!Usable(Value)) return Value;
					if (!Validation)
					{
						Value = ConvertTemplateConstant(Value.Value, Function.ParameterTypes[I], false, Function.Body->Location);
					}
					if (!Usable(Value)) return Value;
					if (!AddLocal(Frame, Function.ParameterNames[I], FConstantLocal{Value, I < Function.ParameterConsts.size() && Function.ParameterConsts[I], false}))
					{
						return Failure(EStatus::LimitExceeded, "constexpr frame memory budget exceeded", Function.Body->Location);
					}
				}
				auto Flow = Statement(*Function.Body, Validation, Depth + 1);
				if (Flow.Kind == EFlow::Error) return Flow.Result;
				if (Validation) return Constant({Function.ReturnType, 0}, Function.Body->Location);
				if (Flow.Kind != EFlow::Return) return Failure(EStatus::Invalid, "constexpr call reached the end without returning", Function.Body->Location);
				auto Result = ConvertTemplateConstant(Flow.Result.Value, Function.ReturnType, false, Function.Body->Location);
				if (Result.Succeeded() && CanCache(Key.size() + 256))
				{
					CallCache.emplace(Key, Result);
				}
				return Result;
			}

			FFlow Statement(const FStmt &Node, bool Shape, std::uint32_t Depth)
			{
				if (!Check(1) || Depth > 256)
				{
					return {EFlow::Error, Failure(Stop == EStatus::Constant ? EStatus::LimitExceeded : Stop, "constexpr statement budget exceeded", Node.Location)};
				}
				auto &Frame = Frames.back();
				const auto Error = [&](const std::string &Reason)
				{
					return FFlow{EFlow::Error, Failure(EStatus::Invalid, Reason, Node.Location)};
				};
				const auto Eval = [&](const FExpr *Expression) -> FFlow
				{
					if (!Expression) return {};
					auto Result = Visit(*Expression, Shape, Depth + 1);
					return Usable(Result) ? FFlow{} : FFlow{EFlow::Error, std::move(Result)};
				};
				switch (Node.Kind)
				{
				case EStmtKind::Block:
					{
						Frame.Scopes.emplace_back();
						struct FPop
						{
							FEvaluator &Evaluator;
							FConstantFrame &Frame;
							~FPop()
							{
								Evaluator.PopScope(Frame);
							}
						} Pop{*this, Frame};
						for (const auto &Child : static_cast<const FBlockStmt &>(Node).Statements)
						{
							auto Flow = Statement(*Child, Shape, Depth + 1);
							if (Flow.Kind == EFlow::Error || (!Shape && Flow.Kind != EFlow::Next)) return Flow;
						}
						return {};
					}
				case EStmtKind::VarDeclGroup:
					{
						for (const auto &Declaration : static_cast<const FVarDeclGroupStmt &>(Node).Declarations)
						{
							auto Flow = Statement(*Declaration, Shape, Depth + 1);
							if (Flow.Kind == EFlow::Error) return Flow;
						}
						return {};
					}
				case EStmtKind::VarDecl:
					{
						const auto &Declaration = static_cast<const FVarDeclStmt &>(Node);
						if (Declaration.bIsLocalStatic && !Declaration.bConstexpr)
						{
							return Error("runtime local static storage is not permitted in a constexpr function");
						}
						if (Declaration.bIsRef || !Declaration.Initializer) return Error("constexpr local requires an initialized scalar value");
						if (Frame.Scopes.back().count(Declaration.Name)) return Error("duplicate constexpr local");
						const auto OldRequired = Frame.bRequiredInitializer;
						Frame.bRequiredInitializer = Declaration.bConstexpr;
						auto Value = Visit(*Declaration.Initializer, Shape && !Declaration.bConstexpr, Depth + 1);
						Frame.bRequiredInitializer = OldRequired;
						if (!Usable(Value) || (Declaration.bConstexpr && !Value.Succeeded())) return {EFlow::Error, Value};
						FTemplateValueType Type = Value.Value.Type;
						if (Declaration.TypeName != "auto" && ResolveType(Declaration.TypeName, Type) != EStatus::Constant)
						{
							return Error("unsupported constexpr local type");
						}
						if (Shape && !Declaration.bConstexpr)
						{
							Value.Value.Type = Type;
							Value.Status = EStatus::Dependent;
						}
						else
						{
							Value = ConvertTemplateConstant(Value.Value, Type, false, Node.Location);
						}
						if (!Usable(Value)) return {EFlow::Error, Value};
						FConstantLocal Local{Value, Declaration.bIsConst, Declaration.bIsConst};
						Local.Order = Frame.NextOrder++;
						if (Declaration.bIsConst && !Declaration.bConstexpr)
						{
							Local.Initializer = Declaration.Initializer.get();
						}
						if (!AddLocal(Frame, Declaration.Name, std::move(Local)))
						{
							return {EFlow::Error, Failure(EStatus::LimitExceeded, "constexpr local memory budget exceeded", Node.Location)};
						}
						return {};
					}
				case EStmtKind::Expr:
					return Eval(static_cast<const FExprStmt &>(Node).Expression.get());
				case EStmtKind::Return:
					{
						const auto &Return = static_cast<const FReturnStmt &>(Node);
						if (!Return.Value) return Error("constexpr return requires a scalar value");
						auto Value = Visit(*Return.Value, Shape, Depth + 1);
						if (!Usable(Value)) return {EFlow::Error, Value};
						if (!Shape)
						{
							Value = ConvertTemplateConstant(Value.Value, Frame.ReturnType, false, Node.Location);
						}
						return {Usable(Value) ? EFlow::Return : EFlow::Error, Value};
					}
				case EStmtKind::If:
					{
						const auto &If = static_cast<const FIfStmt &>(Node);
						if (If.bConstexpr)
						{
							const bool Required = Frame.bRequiredInitializer;
							Frame.bRequiredInitializer = true;
							struct FRestoreRequired
							{
								FConstantFrame &Frame;
								bool Previous;
								~FRestoreRequired()
								{
									Frame.bRequiredInitializer = Previous;
								}
							} Restore{Frame, Required};
							const auto Condition = Visit(*If.Condition, false, Depth + 1);
							if (!Condition.Succeeded()) return {EFlow::Error, Condition};
							if (Condition.Value.Type.bScopedEnum) return Error("scoped enum cannot be an if constexpr condition");
							Frame.bRequiredInitializer = Required;
							const auto *Branch = Condition.Value.Bits ? If.ThenBranch.get() : If.ElseBranch.get();
							return Branch ? Statement(*Branch, Shape, Depth + 1) : FFlow{};
						}
						auto Condition = Visit(*If.Condition, Shape, Depth + 1);
						if (!Usable(Condition)) return {EFlow::Error, Condition};
						if (Condition.Value.Type.bScopedEnum) return Error("scoped enum cannot be a condition");
						if (Shape)
						{
							auto Then = Statement(*If.ThenBranch, true, Depth + 1);
							if (Then.Kind == EFlow::Error) return Then;
							return If.ElseBranch ? Statement(*If.ElseBranch, true, Depth + 1) : FFlow{};
						}
						return Condition.Value.Bits ? Statement(*If.ThenBranch, false, Depth + 1)
						    : If.ElseBranch         ? Statement(*If.ElseBranch, false, Depth + 1)
						                            : FFlow{};
					}
				case EStmtKind::For:
				case EStmtKind::While:
				case EStmtKind::DoWhile:
					{
						Frame.Scopes.emplace_back();
						++Frame.LoopDepth;
						struct FPop
						{
							FEvaluator &Evaluator;
							FConstantFrame &Frame;
							~FPop()
							{
								--Frame.LoopDepth;
								Evaluator.PopScope(Frame);
							}
						} Pop{*this, Frame};
						const FExpr *Condition = nullptr, *Step = nullptr;
						const FBlockStmt *Body = nullptr;
						const FForStmt *For = nullptr;
						if (Node.Kind == EStmtKind::For)
						{
							For = &static_cast<const FForStmt &>(Node);
							Condition = For->Condition.get();
							Step = For->Step.get();
							Body = For->Body.get();
							if (For->Init)
							{
								auto Flow = Statement(*For->Init, Shape, Depth + 1);
								if (Flow.Kind == EFlow::Error) return Flow;
							}
							for (const auto &Init : For->InitExpressions)
							{
								auto Flow = Eval(Init.get());
								if (Flow.Kind == EFlow::Error) return Flow;
							}
						}
						else if (Node.Kind == EStmtKind::While)
						{
							const auto &While = static_cast<const FWhileStmt &>(Node);
							Condition = While.Condition.get();
							Body = While.Body.get();
						}
						else
						{
							const auto &Do = static_cast<const FDoWhileStmt &>(Node);
							Condition = Do.Condition.get();
							Body = Do.Body.get();
						}
						bool First = true;
						for (;;)
						{
							if (!Check(1)) return {EFlow::Error, Failure(Stop, "constexpr loop budget exceeded", Node.Location)};
							if (Condition && (Shape || !First || Node.Kind != EStmtKind::DoWhile))
							{
								auto Test = Visit(*Condition, Shape, Depth + 1);
								if (!Usable(Test)) return {EFlow::Error, Test};
								if (Test.Value.Type.bScopedEnum) return Error("scoped enum cannot be a loop condition");
								if (!Shape && !Test.Value.Bits) return {};
							}
							First = false;
							auto Flow = Statement(*Body, Shape, Depth + 1);
							if (Flow.Kind == EFlow::Error || (!Shape && Flow.Kind == EFlow::Return)) return Flow;
							if (!Shape && Flow.Kind == EFlow::Break) return {};
							if (Step)
							{
								Flow = Eval(Step);
								if (Flow.Kind == EFlow::Error) return Flow;
							}
							if (For)
							{
								for (const auto &Expression : For->StepExpressions)
								{
									Flow = Eval(Expression.get());
									if (Flow.Kind == EFlow::Error) return Flow;
								}
							}
							if (Shape) return {};
						}
					}
				case EStmtKind::Switch:
					{
						const auto &Switch = static_cast<const FSwitchStmt &>(Node);
						auto Value = Visit(*Switch.Discriminant, Shape, Depth + 1);
						if (!Usable(Value)) return {EFlow::Error, Value};
						std::size_t Selected = Switch.Cases.size(), Default = Selected;
						++Frame.SwitchDepth;
						struct FPop
						{
							FConstantFrame &Frame;
							~FPop()
							{
								--Frame.SwitchDepth;
							}
						} Pop{Frame};
						for (std::size_t I = 0; I < Switch.Cases.size(); ++I)
						{
							const auto &Case = Switch.Cases[I];
							if (Case.bIsDefault)
							{
								Default = I;
							}
							for (const auto &Label : Case.Labels)
							{
								const bool Previous = Frame.bRequiredInitializer;
								Frame.bRequiredInitializer = true;
								auto ConstantValue = Visit(*Label, false, Depth + 1);
								Frame.bRequiredInitializer = Previous;
								if (!ConstantValue.Succeeded()) return {EFlow::Error, ConstantValue};
								auto Converted = ConvertTemplateConstant(ConstantValue.Value, Value.Value.Type, false, Label->Location);
								if (!Converted.Succeeded()) return {EFlow::Error, Converted};
								if (!Shape && Converted.Value.Bits == Value.Value.Bits)
								{
									Selected = I;
								}
							}
							if (Shape)
							{
								auto Flow = Statement(*Case.Body, true, Depth + 1);
								if (Flow.Kind == EFlow::Error) return Flow;
							}
						}
						if (Shape) return {};
						if (Selected == Switch.Cases.size())
						{
							Selected = Default;
						}
						for (; Selected < Switch.Cases.size(); ++Selected)
						{
							auto Flow = Statement(*Switch.Cases[Selected].Body, false, Depth + 1);
							if (Flow.Kind == EFlow::Break || Flow.Kind == EFlow::Next) return {};
							if (Flow.Kind != EFlow::Fallthrough) return Flow;
						}
						return {};
					}
				case EStmtKind::Break:
					return Frame.LoopDepth || Frame.SwitchDepth ? FFlow{EFlow::Break, {}} : Error("break outside constexpr loop or switch");
				case EStmtKind::Continue:
					return Frame.LoopDepth ? FFlow{EFlow::Continue, {}} : Error("continue outside constexpr loop");
				case EStmtKind::Fallthrough:
					return Frame.SwitchDepth ? FFlow{EFlow::Fallthrough, {}} : Error("fallthrough outside constexpr switch");
				default:
					return Error("statement is outside the pure scalar constexpr subset");
				}
			}

			std::deque<FConstantFrame> Frames;
			std::unordered_set<std::string> ValidatedFunctions, ActiveCalls;
			std::unordered_map<std::string, FResult> CallCache;
			std::uint64_t CacheBytes = 0;
			bool CanCache(std::uint64_t Bytes)
			{
				if (Bytes > EvaluationState->Limits.MaxCacheBytes || CacheBytes > EvaluationState->Limits.MaxCacheBytes - Bytes) return false;
				CacheBytes += Bytes;
				return true;
			}
