// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptAstClone.h"
#include "VeroScriptAstTraversal.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptConstantBranches.h"
#include "VeroScriptMemberAccess.h"

#include <functional>
#include <limits>
#include <unordered_set>

namespace VeroScript
{
	FConstantBranchState::FConstantBranchState(const FProgram &Program)
	{
		VisitProgramNodes(Program, [](const FExpr &)
		{
		}, [&](const FStmt &Node)
		{
			CheckAnalysisCancellation();
			if (Node.Kind != EStmtKind::If || !static_cast<const FIfStmt &>(Node).bConstexpr) return;
			const auto &If = static_cast<const FIfStmt &>(Node);
			if (If.ThenBranch && !If.ThenBranch->ScopeId.empty())
			{
				Guards.emplace(If.ThenBranch->ScopeId, FGuard{Key(If), true});
			}
			if (If.ElseBranch && !If.ElseBranch->ScopeId.empty())
			{
				Guards.emplace(If.ElseBranch->ScopeId, FGuard{Key(If), false});
			}
		});
	}

	std::string FConstantBranchState::Key(const FIfStmt &If)
	{
		return If.SourceRange.SourceId + ':' + std::to_string(If.SourceRange.Begin) + ':' + std::to_string(If.SourceRange.End) + ':' + If.ScopeId + ':' + std::string(If.Location.FileLabel) + ':' + std::to_string(If.Location.Line) + ':' + std::to_string(If.Location.Column);
	}
	bool FConstantBranchState::Includes(std::string_view Scope) const
	{
		std::string Path(Scope);
		while (!Path.empty())
		{
			if (const auto Guard = Guards.find(Path); Guard != Guards.end())
			{
				const auto Chosen = Selections.find(Guard->second.Key);
				if (Chosen == Selections.end() || Chosen->second != Guard->second.bThen) return false;
			}
			const auto Parent = Path.rfind('/');
			if (Parent == std::string::npos)
			{
				break;
			}
			Path.resize(Parent);
		}
		return true;
	}
	bool FConstantBranchState::Guarded(std::string_view Scope) const
	{
		std::string Path(Scope);
		while (!Path.empty())
		{
			if (Guards.count(Path)) return true;
			const auto Parent = Path.rfind('/');
			if (Parent == std::string::npos)
			{
				break;
			}
			Path.resize(Parent);
		}
		return false;
	}
	void FConstantBranchState::SelectEnclosing(std::string_view Scope)
	{
		// This body is reached only through a selected declaration/expression.
		// Restore its lexical guards when a deferred initializer compiles later.
		std::string Path(Scope);
		while (!Path.empty())
		{
			if (const auto Guard = Guards.find(Path); Guard != Guards.end())
			{
				Selections[Guard->second.Key] = Guard->second.bThen;
			}
			const auto Parent = Path.rfind('/');
			if (Parent == std::string::npos)
			{
				break;
			}
			Path.resize(Parent);
		}
	}
	bool VeroScriptCompiler::LowerConstantBranches(const FBlockStmt &Source,
	    std::unique_ptr<FBlockStmt> &Output, const std::vector<FFunctionParam> *Parameters)
	{
		const auto ValidateSelectedTypes = [&]()
		{
			if (!ActiveProgram || !ConstantBranchState || ConstantBranchState->Guards.empty()) return true;
			std::vector<FScopedTypeUse> Uses;
			if (!Source.CallableOwner.empty())
			{
				if (const auto Found = ScopedUsesByFunction.find(Source.CallableOwner); Found != ScopedUsesByFunction.end())
				{
					for (const auto *Use : Found->second)
					{
						if (Use->Context && ConstantBranchState->Guarded(Use->Context->ScopeId) && ConstantBranchState->Includes(*Use))
						{
							Uses.push_back(*Use);
						}
					}
				}
			}
			else if (!Source.ScopeId.empty())
			{
				for (const auto &Use : ActiveProgram->ScopedTypeUses)
				{
					if (Use.Context && Use.ContextFunction.empty() &&
					    (Use.Context->ScopeId == Source.ScopeId || Use.Context->ScopeId.rfind(Source.ScopeId + '/', 0) == 0) &&
					    ConstantBranchState->Guarded(Use.Context->ScopeId) && ConstantBranchState->Includes(Use))
					{
						Uses.push_back(Use);
					}
				}
			}
			if (Uses.empty()) return true;
			FScriptError Error;
			if (!Uses.empty() && !ValidateScopedTypeDeclarations(*ActiveProgram, ActiveImportIndex, Error, [&](const auto &Info, const auto &)
			{
				return IsCurrentFunctionFriendOf(Info, CurrentOwnerTypeLinkage);
			}, &Uses, [&](std::string_view Scope)
			{
				return ConstantBranchState->Includes(Scope);
			}))
			{
				ReportError(Error.Message, {Error.Location.FileLabel, Error.Location.Line, Error.Location.Column});
				return false;
			}
			return CheckScopedTypeRequirements(Source.CallableOwner.empty() ? Source.ScopeId : std::string_view{}, Source.CallableOwner, ActiveTypeSubst);
		};
		bool Found = false;
		VisitBlockNodes(Source, [](const FExpr &)
		{
		}, [&](const FStmt &Node)
		{
			CheckAnalysisCancellation();
			Found = Found || (Node.Kind == EStmtKind::If && static_cast<const FIfStmt &>(Node).bConstexpr);
		});
		if (!Found) return ValidateSelectedTypes();
		Output = CloneBlockStatement(Source);

		// A read-only lexical binding pass. These temporary scopes never allocate
		// runtime slots, emit cleanup, or change the enclosing function's locals.
		const auto ScopeCount = Scopes.size(), AliasCount = LocalTypeAliasScopes.size();
		const auto PreviousSlot = NextLocalSlot;
		struct FRestore
		{
			VeroScriptCompiler &C;
			std::size_t Scopes, Aliases;
			std::uint16_t Slot;
			~FRestore()
			{
				C.Scopes.resize(Scopes);
				C.LocalTypeAliasScopes.resize(Aliases);
				C.NextLocalSlot = Slot;
			}
		} Restore{*this, ScopeCount, AliasCount, PreviousSlot};
		const auto Push = [&]()
		{
			Scopes.emplace_back();
			LocalTypeAliasScopes.emplace_back();
		};
		const auto Pop = [&]()
		{
			Scopes.pop_back();
			LocalTypeAliasScopes.pop_back();
		};
		const auto Declare = [&](const std::string &Name, const std::string &Type,
		                         const FExpr *Initializer = nullptr, bool Const = false) -> bool
		{
			if (NextLocalSlot == std::numeric_limits<std::uint16_t>::max())
			{
				ReportError("too many locals during if constexpr binding", Source.Location);
				return false;
			}
			const auto Slot = NextLocalSlot++;
			// Ordinary compilation owns duplicate-declaration diagnostics.
			if (!Scopes.back().Locals.emplace(Name, Slot).second) return true;
			Scopes.back().LocalTypes.emplace(Name, Type);
			if (Const && Initializer)
			{
				Scopes.back().LocalConsts.insert(Name);
				FLocalConstantInitializer Pending;
				Pending.Expression = Initializer;
				Pending.TypeName = Type == "auto" ? std::string{} : Type;
				Pending.ScopeCount = Scopes.size();
				Pending.SlotBound = Slot;
				Pending.GlobalSlotBound = Module ? Module->GlobalNames.size() : 0;
				Pending.FunctionName = CurrentFunction ? CurrentFunction->Name : std::string{};
				Pending.ModulePath = CurrentModulePath;
				Pending.Namespace = CurrentNamespacePrefix;
				Pending.Owner = CurrentOwnerTypeLinkage;
				Pending.Access = CurrentAccessFunctionLinkage;
				Pending.Identity = CurrentAccessFunctionLinkage + "$if-constexpr-local-" + std::to_string(Slot);
				Pending.Bindings = ActiveTypeSubst;
				for (const auto &Scope : Scopes)
				{
					Pending.EnumScopes.push_back(Scope.EnumConstants);
				}
				for (const auto &Aliases : LocalTypeAliasScopes)
				{
					for (const auto &Alias : Aliases)
					{
						Pending.Bindings[Alias.first] = CanonicalTypeNameForCoercion(QualifyTypeName(SubstituteTypeName(Alias.second), CurrentModulePath));
					}
				}
				Scopes.back().ConstantInitializers.emplace(Name, std::move(Pending));
			}
			return true;
		};
		std::function<bool(FBlockStmt &)> Block;
		std::function<bool(std::unique_ptr<FStmt> &)> Statement;
		std::unordered_set<const FExpr *> LoweredLambdas;
		const auto Expression = [&](FExpr *Root) -> bool
		{
			if (!Root) return true;
			bool Ok = true;
			VisitExpressionNodes(*Root, [&](FExpr &Node)
			{
				if (!Ok || Node.Kind != EExprKind::Lambda || !LoweredLambdas.insert(&Node).second) return;
				auto &Lambda = static_cast<FLambdaExpr &>(Node);
				Push();
				for (const auto &Parameter : Lambda.Parameters)
				{
					if (!Declare(Parameter.Name, Parameter.TypeName))
					{
						Ok = false;
						break;
					}
				}
				if (Ok && Lambda.Body)
				{
					Ok = Block(*Lambda.Body);
				}
				Pop();
			});
			return Ok;
		};
		unsigned Depth = 0;
		Block = [&](FBlockStmt &Body)
		{
			Push();
			bool Ok = true;
			for (auto &Child : Body.Statements)
			{
				if (!Statement(Child))
				{
					Ok = false;
					break;
				}
			}
			Pop();
			return Ok;
		};
		Statement = [&](std::unique_ptr<FStmt> &Node) -> bool
		{
			if (!Node) return true;
			CheckAnalysisCancellation();
			if (++Depth > 256)
			{
				--Depth;
				ReportError("if constexpr binding nesting limit exceeded", Node->Location);
				return false;
			}
			struct FDepth
			{
				unsigned &Value;
				~FDepth()
				{
					--Value;
				}
			} Guard{Depth};
			switch (Node->Kind)
			{
			case EStmtKind::Block:
				return Block(static_cast<FBlockStmt &>(*Node));
			case EStmtKind::If:
				{
					auto &If = static_cast<FIfStmt &>(*Node);
					if (If.bConstexpr)
					{
						const auto Value = EvaluateTemplateExpression(*If.Condition, CurrentModulePath, ActiveTypeSubst, CurrentNamespacePrefix, true);
						if (!Value.Succeeded() || Value.Value.Type.bScopedEnum)
						{
							ReportError("if constexpr requires an integral or bool constant condition: " +
							        (Value.Succeeded() ? std::string("scoped enum requires an explicit conversion") : Value.Reason),
							    If.Condition->Location);
							return false;
						}
						const bool Then = Value.Value.Bits != 0;
						if (ConstantBranchState)
						{
							ConstantBranchState->Selections[FConstantBranchState::Key(If)] = Then;
						}
						Module->CompiledConstantBranches.push_back({FConstantBranchState::Key(If), Then});
						auto Selected = Then ? std::move(If.ThenBranch) : std::move(If.ElseBranch);
						if (!Selected)
						{
							Selected = std::make_unique<FBlockStmt>();
							Selected->Location = If.Location;
						}
						Node = std::move(Selected);
						return Statement(Node);
					}
					return Expression(If.Condition.get()) && (!If.ThenBranch || Block(*If.ThenBranch)) && (!If.ElseBranch || Block(*If.ElseBranch));
				}
			case EStmtKind::VarDecl:
				{
					auto &Var = static_cast<FVarDeclStmt &>(*Node);
					return Expression(Var.Initializer.get()) && Declare(Var.Name, Var.TypeName, Var.Initializer.get(), Var.bIsConst && !Var.bIsRef);
				}
			case EStmtKind::VarDeclGroup:
				{
					for (auto &Var : static_cast<FVarDeclGroupStmt &>(*Node).Declarations)
					{
						if (Var && (!Expression(Var->Initializer.get()) || !Declare(Var->Name, Var->TypeName, Var->Initializer.get(), Var->bIsConst && !Var->bIsRef))) return false;
					}
					return true;
				}
			case EStmtKind::TypeAlias:
				{
					const auto &Alias = static_cast<const FTypeAliasStmt &>(*Node);
					LocalTypeAliasScopes.back().emplace(Alias.Name, SubstituteTypeName(Alias.TypeName));
					if (Alias.bTypeDefinition)
					{
						if (const auto *Enum = FindEnumDeclaration(Alias.TypeName); Enum && !Enum->bScoped)
						{
							for (const auto &Member : Enum->Members)
							{
								Scopes.back().EnumConstants.emplace(Member.Name,
								    GetSymbolLinkageName(Enum->ModulePath, Enum->bExported, Enum->Name + "." + Member.Name));
							}
						}
					}
					return true;
				}
			case EStmtKind::Expr:
				return Expression(static_cast<FExprStmt &>(*Node).Expression.get());
			case EStmtKind::Return:
				return Expression(static_cast<FReturnStmt &>(*Node).Value.get());
			case EStmtKind::Throw:
				return Expression(static_cast<FThrowStmt &>(*Node).Value.get());
			case EStmtKind::While:
				{
					auto &Loop = static_cast<FWhileStmt &>(*Node);
					return Expression(Loop.Condition.get()) && (!Loop.Body || Block(*Loop.Body));
				}
			case EStmtKind::DoWhile:
				{
					auto &Loop = static_cast<FDoWhileStmt &>(*Node);
					return (!Loop.Body || Block(*Loop.Body)) && Expression(Loop.Condition.get());
				}
			case EStmtKind::For:
				{
					auto &Loop = static_cast<FForStmt &>(*Node);
					Push();
					bool Ok = Statement(Loop.Init) && Expression(Loop.Condition.get()) && Expression(Loop.Step.get());
					for (auto &Expr : Loop.InitExpressions)
					{
						Ok = Ok && Expression(Expr.get());
					}
					for (auto &Expr : Loop.StepExpressions)
					{
						Ok = Ok && Expression(Expr.get());
					}
					Ok = Ok && (!Loop.Body || Block(*Loop.Body));
					Pop();
					return Ok;
				}
			case EStmtKind::ForEach:
				{
					auto &Loop = static_cast<FForEachStmt &>(*Node);
					if (!Expression(Loop.Collection.get())) return false;
					Push();
					bool Ok = Declare(Loop.ElementName, Loop.ElementTypeName) && (!Loop.bHasKeyBinding || Declare(Loop.KeyName, Loop.KeyTypeName));
					Ok = Ok && (!Loop.Body || Block(*Loop.Body));
					Pop();
					return Ok;
				}
			case EStmtKind::Switch:
				{
					auto &Switch = static_cast<FSwitchStmt &>(*Node);
					if (!Expression(Switch.Discriminant.get())) return false;
					for (auto &Case : Switch.Cases)
					{
						if (Case.Body && !Block(*Case.Body)) return false;
					}
					return true;
				}
			case EStmtKind::Try:
				{
					auto &Try = static_cast<FTryStmt &>(*Node);
					if (Try.TryBlock && !Block(*Try.TryBlock)) return false;
					for (auto &Catch : Try.Catches)
					{
						Push();
						bool Ok = Catch.Name.empty() || Declare(Catch.Name, Catch.TypeName);
						Ok = Ok && (!Catch.Body || Block(*Catch.Body));
						Pop();
						if (!Ok) return false;
					}
					return !Try.FinallyBlock || Block(*Try.FinallyBlock);
				}
			default:
				return true;
			}
		};
		Push();
		if (Parameters)
		{
			for (const auto &Parameter : *Parameters)
			{
				if (!Declare(Parameter.Name, Parameter.TypeName)) return false;
			}
		}
		return Block(*Output) && ValidateSelectedTypes();
	}
}
