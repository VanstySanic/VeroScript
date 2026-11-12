// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

			// Installed on each immutable declaration context by EvaluateTemplateExpression.
			Context->State = ConstantEvaluationState;
			Context->MaxNodes = ConstantEvaluationLimits.MaxWork;
			Context->ResolveCall = [&, ReferenceModule, ReferenceNamespace, ReferenceBindings, ReferenceOwner, ReferenceAccess, LocalEnvironment, WithLocals](const FExpr &Expression, const std::vector<FTemplateEvaluationResult> &Arguments)
			{
				FConstantFunction Result;
				if (!AllowFunctionCalls)
				{
					Result.Reason = "ordinary const calls are evaluated only when a constant is required";
					return Result;
				}
				auto &Compiler = *const_cast<VeroScriptCompiler *>(this);
				FConstantLexicalScope LexicalScope(Compiler, WithLocals, LocalEnvironment);
				if (!LexicalScope.bValid)
				{
					Result.Status = ETemplateEvaluationStatus::LimitExceeded;
					Result.Reason = "constexpr lexical scope budget exceeded";
					return Result;
				}
				struct FGuard
				{
					VeroScriptCompiler &Compiler;
					std::string Module, Namespace, Owner, Access;
					FBindings Bindings;
					~FGuard()
					{
						Compiler.CurrentModulePath = std::move(Module);
						Compiler.CurrentNamespacePrefix = std::move(Namespace);
						Compiler.CurrentOwnerTypeLinkage = std::move(Owner);
						Compiler.CurrentAccessFunctionLinkage = std::move(Access);
						Compiler.ActiveTypeSubst = std::move(Bindings);
					}
				} Guard{Compiler, Compiler.CurrentModulePath, Compiler.CurrentNamespacePrefix,
				    Compiler.CurrentOwnerTypeLinkage, Compiler.CurrentAccessFunctionLinkage, Compiler.ActiveTypeSubst};
				Compiler.CurrentModulePath = ReferenceModule;
				Compiler.CurrentNamespacePrefix = ReferenceNamespace;
				Compiler.CurrentOwnerTypeLinkage = ReferenceOwner;
				Compiler.ActiveTypeSubst = ReferenceBindings;
				Compiler.CurrentAccessFunctionLinkage = ReferenceAccess;
				std::vector<std::string> ArgumentTypes;
				for (const auto &Argument : Arguments)
				{
					if (Argument.Value.Type.Name.empty())
					{
						Result.Status = ETemplateEvaluationStatus::Dependent;
						return Result;
					}
					ArgumentTypes.push_back(Argument.Value.Type.Name);
				}
				const FMethodCallExpr *MethodCall = nullptr;
				std::unique_ptr<FMethodCallExpr> LexicalCall;
				std::unique_ptr<FCallExpr> QualifiedCall;
				const FCallExpr *FreeCall = nullptr;
				if (Expression.Kind == EExprKind::MethodCall)
				{
					MethodCall = &static_cast<const FMethodCallExpr &>(Expression);
				}
				else
				{
					FreeCall = &static_cast<const FCallExpr &>(Expression);
					if (!FreeCall->CalleeExpr && !FreeCall->bStandardLibraryQualified && !FreeCall->bImplementationBuiltinAccess &&
					    !Compiler.ResolveLocalEnumConstantLinkageName(FreeCall->Callee).empty())
					{
						Result.Reason = "enum constant is not callable: " + FreeCall->Callee;
						return Result;
					}
					LexicalCall = Compiler.MakeLexicalStaticCall(*FreeCall);
					if (LexicalCall)
					{
						MethodCall = LexicalCall.get();
					}
				}
				if (MethodCall && MethodCall->Object)
				{
					std::string QualifiedName;
					if (Compiler.TryBuildNamespaceQualifiedName(*MethodCall->Object, MethodCall->Method, QualifiedName) &&
					    Compiler.FindFunctionDeclForReference(QualifiedName, ReferenceModule))
					{
						QualifiedCall = std::make_unique<FCallExpr>(QualifiedName, std::vector<std::unique_ptr<FExpr>>{});
						QualifiedCall->TypeArgs = MethodCall->TypeArgs;
						QualifiedCall->bExplicitTemplateArguments = MethodCall->bExplicitTemplateArguments;
						FreeCall = QualifiedCall.get();
						MethodCall = nullptr;
					}
				}
				const std::vector<FFunctionParam> *Parameters = nullptr;
				const FBlockStmt *Body = nullptr;
				std::string ReturnType, Module, Namespace, Owner;
				FBindings Bindings;
				if (MethodCall)
				{
					std::string Receiver;
					if (!MethodCall->Object || !Compiler.TryBuildQualifiedExpressionName(*MethodCall->Object, Receiver))
					{
						Result.Reason = "constexpr requires a type-qualified static method";
						return Result;
					}
					Receiver = Compiler.QualifyTypeName(Compiler.SubstituteTypeName(Receiver), ReferenceModule);
					const FMethodDecl *Method = nullptr;
					if (!Compiler.ResolveMethodForCall(Receiver, MethodCall->Method, ArgumentTypes, Owner, Method,
					        Expression.Location, false, false, nullptr, false,
					        MethodCall->bExplicitTemplateArguments || !MethodCall->TypeArgs.empty() ? &MethodCall->TypeArgs : nullptr) ||
					    !Method)
					{
						Result.Reason = "no matching constexpr static method";
						return Result;
					}
					if (!Method->bConstexpr || !Method->bIsStatic || Method->bVirtual || Method->bOverride || Method->bFinal || Method->bDeleted || Method->bDefaulted)
					{
						Result.Reason = "selected method is outside the constexpr subset";
						return Result;
					}
					if (!Compiler.ValidateMemberAccess(Expression.Location, Owner, MethodCall->Method, true, Arguments.size(), Method))
					{
						Result.Reason = "constexpr method is inaccessible";
						return Result;
					}
					Module = Compiler.ResolveOwnerModulePath(Owner);
					Compiler.TryBuildGenericOwnerSubst(Owner, Bindings, Expression.Location);
					Parameters = &Method->Parameters;
					Body = Method->Body.get();
					ReturnType = Method->ReturnType;
					if (const auto Found = MethodSpecializationKeys.find(Method); Found != MethodSpecializationKeys.end())
					{
						const auto &Instance = MethodSpecializations.at(Found->second);
						Bindings = Instance.Bindings;
						Body = Instance.Declaration->Body.get();
					}
					Namespace = Compiler.NamespaceFromFunctionLinkage(Owner + "::" + Method->Name);
					Result.Identity = Owner + "::" + Method->Name;
				}
				else
				{
					if (!FreeCall || FreeCall->CalleeExpr) return Result;
					if (WithLocals)
					{
						const auto Count = LocalEnvironment ? (std::min)(LocalEnvironment->ScopeCount, Scopes.size()) : Scopes.size();
						for (std::size_t I = Count; I-- > 0;)
						{
							if (Scopes[I].StaticLocals.count(FreeCall->Callee))
							{
								Result.Reason = "indirect calls are not constexpr";
								return Result;
							}
							if (const auto Slot = Scopes[I].Locals.find(FreeCall->Callee); Slot != Scopes[I].Locals.end() && (!LocalEnvironment || Slot->second < LocalEnvironment->SlotBound))
							{
								Result.Reason = "indirect calls are not constexpr";
								return Result;
							}
						}
					}
					const FFunctionDecl *Function = nullptr;
					Compiler.ResolveGenericFreeFunctionForCall(FreeCall->Callee, FreeCall->TypeArgs, ArgumentTypes, Function,
					    Expression.Location, FreeCall->bExplicitTemplateArguments);
					if (!Function && FreeCall->TypeArgs.empty() && !FreeCall->bExplicitTemplateArguments && !Compiler.HasError())
					{
						Compiler.ResolveFunctionForCall(FreeCall->Callee, ArgumentTypes, Function, Expression.Location);
					}
					if (!Function || !Function->bConstexpr || Function->bDeleted)
					{
						Result.Reason = "selected function is not constexpr";
						return Result;
					}
					std::vector<std::string> Concrete;
					if (!TemplateParameterNames(*Function).empty() && !Compiler.TryInferGenericFunctionTypeParams(*Function, ArgumentTypes, FreeCall->TypeArgs, Concrete))
					{
						Result.Status = ETemplateEvaluationStatus::Invalid;
						Result.Reason = "constexpr template deduction failed";
						return Result;
					}
					const auto &Names = TemplateParameterNames(*Function);
					for (std::size_t I = 0; I < Names.size(); ++I)
					{
						Bindings[Names[I]] = Concrete[I];
					}
					Module = Function->ModulePath;
					const auto Dot = Function->Name.rfind('.');
					Namespace = Dot == std::string::npos ? "" : Function->Name.substr(0, Dot);
					Parameters = &Function->Parameters;
					Body = Function->Body.get();
					ReturnType = Function->ReturnType;
					Result.Identity = Compiler.MakeFreeFunctionLinkageName(*Function);
					for (const auto &Argument : Concrete)
					{
						Result.Identity += "$" + Argument;
					}
				}
				if (!Body)
				{
					Result.Status = ETemplateEvaluationStatus::Invalid;
					Result.Reason = "constexpr definition is unavailable";
					return Result;
				}
				Compiler.CurrentModulePath = Module;
				Compiler.CurrentNamespacePrefix = Namespace;
				Compiler.CurrentOwnerTypeLinkage = Owner;
				Compiler.ActiveTypeSubst = Bindings;
				if (!Body->DefinitionParameterNames.empty() && Body->DefinitionParameterNames.size() != Parameters->size())
				{
					Result.Status = ETemplateEvaluationStatus::Invalid;
					Result.Reason = "invalid constexpr definition parameter mapping";
					return Result;
				}
				if (!ResolveTemplateValueType(ReturnType, Module, Bindings, Result.ReturnType))
				{
					Result.Status = ETemplateEvaluationStatus::Invalid;
					Result.Reason = "unsupported constexpr return type";
					return Result;
				}
				for (std::size_t I = 0; I < Parameters->size(); ++I)
				{
					const auto &Parameter = (*Parameters)[I];
					FTemplateValueType Type;
					if (Parameter.bIsRef || Parameter.bIsVariadic || !ResolveTemplateValueType(Parameter.TypeName, Module, Bindings, Type))
					{
						Result.Status = ETemplateEvaluationStatus::Invalid;
						Result.Reason = "constexpr parameters must be scalar values";
						return Result;
					}
					Result.ParameterTypes.push_back(Type);
					Result.ParameterNames.push_back(Body->DefinitionParameterNames.empty() ? Parameter.Name : Body->DefinitionParameterNames[I]);
					Result.ParameterConsts.push_back(Parameter.bIsConst);
					Result.Defaults.push_back(Parameter.DefaultValue.get());
				}
				Result.DefaultContext = ContextFor(Module, Namespace, Bindings, Owner, false, Result.Identity, nullptr);
				for (const auto &Alias : Body->DefinitionTemplateAliases)
				{
					if (const auto Found = Bindings.find(Alias.second); Found != Bindings.end())
					{
						Bindings[Alias.first] = Found->second;
					}
				}
				if (!Body->DefinitionModulePath.empty())
				{
					Module = Body->DefinitionModulePath;
					Namespace = Body->DefinitionNamespace;
				}
				Result.DeclarationContext = ContextFor(Module, Namespace, Bindings, Owner, false, Result.Identity, nullptr);
				Result.Body = Body;
				Result.Status = ETemplateEvaluationStatus::Constant;
				return Result;
			};
