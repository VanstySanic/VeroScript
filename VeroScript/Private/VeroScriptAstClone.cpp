// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptAstClone.h"
#include "VeroScriptAnalysisControl.h"
#include "Processing/VeroScriptProcessingSession.h"
#include <deque>
#include <stdexcept>
namespace VeroScript
{
	namespace
	{
		thread_local std::deque<std::string>* CloneStrings = nullptr;
		thread_local std::size_t CloneDepth = 0;
		struct FCloneDepthGuard
		{
			FCloneDepthGuard()
			{
				CheckAnalysisCancellation();
				if (CloneDepth >= 512)
				{
					throw std::runtime_error("AST clone nesting limit exceeded");
				}

				++CloneDepth;
			}
			~FCloneDepthGuard()
			{
				--CloneDepth;
			}
		};
		FSourceLocation Copy(const FSourceLocation& V)
		{
			if (!CloneStrings)
			{
				return V;
			}

			CloneStrings->emplace_back(V.FileLabel);
			return {CloneStrings->back(), V.Line, V.Column};
		}
		FScopedTypeUse Copy(const FScopedTypeUse& V)
		{
			auto R = V;
			R.Location = Copy(V.Location);
			return R;
		}
		FToken Copy(const FToken& V)
		{
			auto R = V;
			R.Location = Copy(V.Location);
			if (CloneStrings)
			{
				CloneStrings->emplace_back(V.Lexeme);
				R.Lexeme = CloneStrings->back();
			}
			return R;
		}

		FLambdaCapture Copy(const FLambdaCapture&);
		FMapInitEntry Copy(const FMapInitEntry&);
		FCatchClause Copy(const FCatchClause&);
		FSwitchCase Copy(const FSwitchCase&);
		FFunctionParam Copy(const FFunctionParam&);
		FUsingMemberDecl Copy(const FUsingMemberDecl&);
		FFriendDecl Copy(const FFriendDecl&);
		FFieldDecl Copy(const FFieldDecl&);
		FMethodDecl Copy(const FMethodDecl&);
		FBaseCtorInit Copy(const FBaseCtorInit&);
		FFieldCtorInit Copy(const FFieldCtorInit&);
		FConstructorDecl Copy(const FConstructorDecl&);
		FDestructorDecl Copy(const FDestructorDecl&);
		FEnumMemberDecl Copy(const FEnumMemberDecl&);
		FEnumDecl Copy(const FEnumDecl&);
		FTypeParamBound Copy(const FTypeParamBound&);
		FInterfaceDecl Copy(const FInterfaceDecl&);
		FStructDecl Copy(const FStructDecl&);
		FClassDecl Copy(const FClassDecl&);
		FImportDecl Copy(const FImportDecl&);
		FUsingNamespaceDecl Copy(const FUsingNamespaceDecl&);
		FNamespaceDecl Copy(const FNamespaceDecl&);
		FPackageSourceModule Copy(const FPackageSourceModule&);
		FTypeAliasDecl Copy(const FTypeAliasDecl&);
		FFunctionDecl Copy(const FFunctionDecl&);
		FSourceDefinition Copy(const FSourceDefinition&);
		FProgram Copy(const FProgram&);
		std::unique_ptr<FExpr> CopyNode(const FExpr&);
		std::unique_ptr<FStmt> CopyNode(const FStmt&);
		std::shared_ptr<FExpr> Copy(const std::shared_ptr<FExpr>&);
		template <class T>
		T Copy(const T& V)
		{
			return V;
		}
		template <class T>
		std::unique_ptr<T> Copy(const std::unique_ptr<T>& V)
		{
			if (!V)
			{
				return {};
			}

			if constexpr (std::is_base_of_v<FExpr, T> || std::is_base_of_v<FStmt, T>)
			{
				auto N = CopyNode(*V);
				return std::unique_ptr<T>(static_cast<T*>(N.release()));
			}
			else
			{
				return std::make_unique<T>(Copy(*V));
			}
		}
		template <class T>
		std::vector<T> Copy(const std::vector<T>& V)
		{
			std::vector<T> R;
			R.reserve(V.size());
			for (const auto& E : V)
			{
				CheckAnalysisCancellation();
				R.push_back(Copy(E));
			}
			return R;
		}
		std::shared_ptr<FExpr> Copy(const std::shared_ptr<FExpr>& V)
		{
			return V ? std::shared_ptr<FExpr>(CopyNode(*V)) : nullptr;
		}
		FLambdaCapture Copy(const FLambdaCapture& Source)
		{
			FLambdaCapture Result;
			Result.Name = Source.Name;
			Result.bByReference = Source.bByReference;
			Result.Initializer = Copy(Source.Initializer);
			Result.Location = Copy(Source.Location);
			Result.SourceRange = Source.SourceRange;
			Result.NameRange = Source.NameRange;
			return Result;
		}
		FTemplateParameterList Copy(const FTemplateParameterList& Source)
		{
			auto Parameters = Source.GetParameters();
			for (auto& Parameter : Parameters)
			{
				Parameter.Location = Copy(Parameter.Location);
				if (Parameter.DefaultValue)
				{
					Parameter.DefaultValue = std::shared_ptr<const FExpr>(CopyNode(*Parameter.DefaultValue));
				}
			}
			FTemplateParameterList Result;
			Result.Assign(std::move(Parameters), Source.GetEnclosingCount());
			return Result;
		}

		std::unique_ptr<FExpr> CopyNode(const FExpr& V)
		{
			FCloneDepthGuard Depth;
			switch (V.Kind)
			{
			case EExprKind::IntegerLiteral:
				{
					const auto& S = static_cast<const FIntegerLiteralExpr&>(V);
					auto R = std::make_unique<FIntegerLiteralExpr>(std::int32_t{});
					R->Value = Copy(S.Value);
					R->UnsignedValue = Copy(S.UnsignedValue);
					R->Value64 = Copy(S.Value64);
					R->UnsignedValue64 = Copy(S.UnsignedValue64);
					R->bUnsigned = Copy(S.bUnsigned);
					R->bIs64 = Copy(S.bIs64);
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::CharacterLiteral:
				{
					const auto& S = static_cast<const FCharacterLiteralExpr&>(V);
					auto R = std::make_unique<FCharacterLiteralExpr>(std::int8_t{});
					R->Value = Copy(S.Value);
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::FloatLiteral:
				{
					const auto& S = static_cast<const FFloatLiteralExpr&>(V);
					auto R = std::make_unique<FFloatLiteralExpr>(0.0);
					R->Value = Copy(S.Value);
					R->bFloat32 = Copy(S.bFloat32);
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::StringLiteral:
				{
					const auto& S = static_cast<const FStringLiteralExpr&>(V);
					auto R = std::make_unique<FStringLiteralExpr>(std::string{});
					R->Value = Copy(S.Value);
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::WStringLiteral:
				{
					const auto& S = static_cast<const FWStringLiteralExpr&>(V);
					auto R = std::make_unique<FWStringLiteralExpr>(std::u16string{});
					R->Value = Copy(S.Value);
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::BooleanLiteral:
				{
					const auto& S = static_cast<const FBooleanLiteralExpr&>(V);
					auto R = std::make_unique<FBooleanLiteralExpr>(false);
					R->Value = Copy(S.Value);
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::NullptrLiteral:
				{
					auto R = std::make_unique<FNullptrLiteralExpr>();
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::This:
				{
					auto R = std::make_unique<FThisExpr>();
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::Identifier:
				{
					const auto& S = static_cast<const FIdentifierExpr&>(V);
					auto R = std::make_unique<FIdentifierExpr>(std::string{});
					R->Name = Copy(S.Name);
					R->bStandardLibraryQualified = Copy(S.bStandardLibraryQualified);
					R->TypeArgs = S.TypeArgs;
					R->bExplicitTemplateArguments = S.bExplicitTemplateArguments;
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::Unary:
				{
					const auto& S = static_cast<const FUnaryExpr&>(V);
					auto R = std::make_unique<FUnaryExpr>(EUnaryOp::Negate, nullptr);
					R->Op = Copy(S.Op);
					R->Operand = Copy(S.Operand);
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::Binary:
				{
					const auto& S = static_cast<const FBinaryExpr&>(V);
					auto R = std::make_unique<FBinaryExpr>(EBinaryOp::Add, nullptr, nullptr);
					R->Op = Copy(S.Op);
					R->Left = Copy(S.Left);
					R->Right = Copy(S.Right);
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::Assign:
				{
					const auto& S = static_cast<const FAssignExpr&>(V);
					auto R = std::make_unique<FAssignExpr>(nullptr, nullptr);
					R->Target = Copy(S.Target);
					R->Value = Copy(S.Value);
					R->bIsCompound = Copy(S.bIsCompound);
					R->CompoundOp = Copy(S.CompoundOp);
					R->bFromPrefixIncDec = Copy(S.bFromPrefixIncDec);
					R->bFromPostfixIncDec = Copy(S.bFromPostfixIncDec);
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::Conditional:
				{
					const auto& S = static_cast<const FConditionalExpr&>(V);
					auto R = std::make_unique<FConditionalExpr>(nullptr, nullptr, nullptr);
					R->Condition = Copy(S.Condition);
					R->ThenExpr = Copy(S.ThenExpr);
					R->ElseExpr = Copy(S.ElseExpr);
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::Ref:
				{
					const auto& S = static_cast<const FRefExpr&>(V);
					auto R = std::make_unique<FRefExpr>(nullptr);
					R->Operand = Copy(S.Operand);
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::Call:
				{
					const auto& S = static_cast<const FCallExpr&>(V);
					auto R = std::make_unique<FCallExpr>(std::string{}, std::vector<std::unique_ptr<FExpr>>{});
					R->Callee = Copy(S.Callee);
					R->CalleeExpr = Copy(S.CalleeExpr);
					for (const auto* A : S.GetArguments())
					{
						R->Arguments.push_back(A ? CopyNode(*A) : nullptr);
					}

					R->TypeArgs = Copy(S.TypeArgs);
					R->bExplicitTemplateArguments = S.bExplicitTemplateArguments;
					R->bStandardLibraryQualified = Copy(S.bStandardLibraryQualified);
					R->bImplementationBuiltinAccess = Copy(S.bImplementationBuiltinAccess);
					R->bImplicitReceiverArgument = Copy(S.bImplicitReceiverArgument);
					R->bImplicitReceiverRefBinding = Copy(S.bImplicitReceiverRefBinding);
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::MemberAccess:
				{
					const auto& S = static_cast<const FMemberAccessExpr&>(V);
					auto R = std::make_unique<FMemberAccessExpr>(nullptr, std::string{});
					R->TypeArgs = S.TypeArgs;
					R->bExplicitTemplateArguments = S.bExplicitTemplateArguments;
					R->Object = Copy(S.Object);
					R->Member = Copy(S.Member);
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::MethodCall:
				{
					const auto& S = static_cast<const FMethodCallExpr&>(V);
					auto R =
					    std::make_unique<FMethodCallExpr>(nullptr, std::string{}, std::vector<std::unique_ptr<FExpr>>{});
					R->bExplicitTemplateArguments = S.bExplicitTemplateArguments;
					R->Object = Copy(S.Object);
					R->Method = Copy(S.Method);
					for (const auto* A : S.GetArguments())
					{
						R->Arguments.push_back(A ? CopyNode(*A) : nullptr);
					}

					R->TypeArgs = Copy(S.TypeArgs);
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::IndexAccess:
				{
					const auto& S = static_cast<const FIndexAccessExpr&>(V);
					auto R = std::make_unique<FIndexAccessExpr>(nullptr, nullptr);
					R->Object = Copy(S.Object);
					R->Index = Copy(S.Index);
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::ArrayInit:
				{
					const auto& S = static_cast<const FArrayInitExpr&>(V);
					auto R = std::make_unique<FArrayInitExpr>(std::vector<std::unique_ptr<FExpr>>{});
					R->Elements = Copy(S.Elements);
					R->TypeName = Copy(S.TypeName);
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::MapInit:
				{
					const auto& S = static_cast<const FMapInitExpr&>(V);
					auto R = std::make_unique<FMapInitExpr>(std::vector<FMapInitEntry>{});
					R->Entries = Copy(S.Entries);
					R->TypeName = Copy(S.TypeName);
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}
			case EExprKind::Lambda:
				{
					const auto& S = static_cast<const FLambdaExpr&>(V);
					auto R = std::make_unique<FLambdaExpr>();
					R->CaptureKind = Copy(S.CaptureKind);
					R->Captures = Copy(S.Captures);
					R->bMutable = S.bMutable;
					R->Parameters = Copy(S.Parameters);
					R->ReturnType = Copy(S.ReturnType);
					R->Body = Copy(S.Body);
					R->ExpressionBody = Copy(S.ExpressionBody);
					R->bCapturesThis.store(S.bCapturesThis.load());
					R->Location = Copy(V.Location);
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					return R;
				}

			default:
				{
					break;
				}
			}
			throw std::invalid_argument("unknown AST node during preparation");
		}
		std::unique_ptr<FStmt> CopyNode(const FStmt& V)
		{
			FCloneDepthGuard Depth;
			switch (V.Kind)
			{
			case EStmtKind::Expr:
				{
					const auto& S = static_cast<const FExprStmt&>(V);
					auto R = std::make_unique<FExprStmt>(nullptr);
					R->Expression = Copy(S.Expression);
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					return R;
				}
			case EStmtKind::Block:
				{
					const auto& S = static_cast<const FBlockStmt&>(V);
					auto R = std::make_unique<FBlockStmt>();
					R->Statements = Copy(S.Statements);
					R->DefinitionModulePath = Copy(S.DefinitionModulePath);
					R->DefinitionNamespace = Copy(S.DefinitionNamespace);
					R->DefinitionParameterNames = Copy(S.DefinitionParameterNames);
					R->DefinitionTemplateAliases = S.DefinitionTemplateAliases;
					R->CallableOwner = S.CallableOwner;
					R->ParentScopeId = S.ParentScopeId;
					R->BodyContext = S.BodyContext;
					R->EndContext = S.EndContext;
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					return R;
				}
			case EStmtKind::VarDecl:
				{
					const auto& S = static_cast<const FVarDeclStmt&>(V);
					auto R = std::make_unique<FVarDeclStmt>(std::string{}, std::string{}, nullptr);
					R->TypeName = Copy(S.TypeName);
					R->Name = Copy(S.Name);
					R->bExported = Copy(S.bExported);
					R->bIsConst = Copy(S.bIsConst);
					R->bConstexpr = S.bConstexpr;
					R->bIsRef = Copy(S.bIsRef);
					R->bIsLocalStatic = S.bIsLocalStatic;
					R->bPrecompiledPackage = Copy(S.bPrecompiledPackage);
					R->DeclMeta = Copy(S.DeclMeta);
					R->Initializer = Copy(S.Initializer);
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					R->InitializerSource = S.InitializerSource;
					return R;
				}
			case EStmtKind::VarDeclGroup:
				{
					const auto& S = static_cast<const FVarDeclGroupStmt&>(V);
					auto R = std::make_unique<FVarDeclGroupStmt>(std::vector<std::unique_ptr<FVarDeclStmt>>{});
					R->Declarations = Copy(S.Declarations);
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					return R;
				}
			case EStmtKind::TypeAlias:
				{
					const auto& S = static_cast<const FTypeAliasStmt&>(V);
					auto R = std::make_unique<FTypeAliasStmt>(std::string{}, std::string{});
					R->Name = Copy(S.Name);
					R->bTypeDefinition = S.bTypeDefinition;
					R->TypeName = Copy(S.TypeName);
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					return R;
				}
			case EStmtKind::Return:
				{
					const auto& S = static_cast<const FReturnStmt&>(V);
					auto R = std::make_unique<FReturnStmt>(nullptr);
					R->Value = Copy(S.Value);
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					return R;
				}
			case EStmtKind::Try:
				{
					const auto& S = static_cast<const FTryStmt&>(V);
					auto R = std::make_unique<FTryStmt>();
					R->TryBlock = Copy(S.TryBlock);
					R->Catches = Copy(S.Catches);
					R->FinallyBlock = Copy(S.FinallyBlock);
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					return R;
				}
			case EStmtKind::Throw:
				{
					const auto& S = static_cast<const FThrowStmt&>(V);
					auto R = std::make_unique<FThrowStmt>(nullptr);
					R->Value = Copy(S.Value);
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					return R;
				}
			case EStmtKind::If:
				{
					const auto& S = static_cast<const FIfStmt&>(V);
					auto R = std::make_unique<FIfStmt>(nullptr, nullptr, nullptr);
					R->bConstexpr = S.bConstexpr;
					R->Condition = Copy(S.Condition);
					R->ThenBranch = Copy(S.ThenBranch);
					R->ElseBranch = Copy(S.ElseBranch);
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					return R;
				}
			case EStmtKind::While:
				{
					const auto& S = static_cast<const FWhileStmt&>(V);
					auto R = std::make_unique<FWhileStmt>(nullptr, nullptr);
					R->Condition = Copy(S.Condition);
					R->Body = Copy(S.Body);
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					return R;
				}
			case EStmtKind::DoWhile:
				{
					const auto& S = static_cast<const FDoWhileStmt&>(V);
					auto R = std::make_unique<FDoWhileStmt>(nullptr, nullptr);
					R->Body = Copy(S.Body);
					R->Condition = Copy(S.Condition);
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					return R;
				}
			case EStmtKind::For:
				{
					const auto& S = static_cast<const FForStmt&>(V);
					auto R = std::make_unique<FForStmt>(nullptr, nullptr, nullptr, nullptr);
					R->Init = Copy(S.Init);
					R->Condition = Copy(S.Condition);
					R->Step = Copy(S.Step);
					R->Body = Copy(S.Body);
					R->InitExpressions = Copy(S.InitExpressions);
					R->StepExpressions = Copy(S.StepExpressions);
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					return R;
				}
			case EStmtKind::ForEach:
				{
					const auto& S = static_cast<const FForEachStmt&>(V);
					auto R = std::make_unique<FForEachStmt>(std::string{}, std::string{}, nullptr, nullptr);
					R->ElementTypeName = Copy(S.ElementTypeName);
					R->ElementName = Copy(S.ElementName);
					R->bIsConst = Copy(S.bIsConst);
					R->bIsRef = Copy(S.bIsRef);
					R->bHasKeyBinding = Copy(S.bHasKeyBinding);
					R->KeyTypeName = Copy(S.KeyTypeName);
					R->KeyName = Copy(S.KeyName);
					R->bKeyIsConst = Copy(S.bKeyIsConst);
					R->Collection = Copy(S.Collection);
					R->Body = Copy(S.Body);
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					return R;
				}
			case EStmtKind::Switch:
				{
					const auto& S = static_cast<const FSwitchStmt&>(V);
					auto R = std::make_unique<FSwitchStmt>(nullptr, std::vector<FSwitchCase>{});
					R->Discriminant = Copy(S.Discriminant);
					R->Cases = Copy(S.Cases);
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					return R;
				}
			case EStmtKind::Break:
				{
					auto R = std::make_unique<FBreakStmt>();
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					return R;
				}
			case EStmtKind::Continue:
				{
					auto R = std::make_unique<FContinueStmt>();
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					return R;
				}
			case EStmtKind::Fallthrough:
				{
					auto R = std::make_unique<FFallthroughStmt>();
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					return R;
				}
			case EStmtKind::Goto:
				{
					const auto& S = static_cast<const FGotoStmt&>(V);
					auto R = std::make_unique<FGotoStmt>(std::string{});
					R->Name = Copy(S.Name);
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					return R;
				}
			case EStmtKind::Label:
				{
					const auto& S = static_cast<const FLabelStmt&>(V);
					auto R = std::make_unique<FLabelStmt>(std::string{});
					R->Name = Copy(S.Name);
					R->Location = Copy(V.Location);
					R->ScopeId = V.ScopeId;
					R->SourceRange = V.SourceRange;
					R->DeclMeta = V.DeclMeta;
					R->DeferredSyntax = V.DeferredSyntax;
					R->DeclarationContext = V.DeclarationContext;
					R->ModulePath = V.ModulePath;
					return R;
				}

			default:
				{
					break;
				}
			}
			throw std::invalid_argument("unknown AST node during preparation");
		}
		FMapInitEntry Copy(const FMapInitEntry& S)
		{
			FMapInitEntry R;
			R.Key = Copy(S.Key);
			R.Value = Copy(S.Value);
			return R;
		}
		FCatchClause Copy(const FCatchClause& S)
		{
			FCatchClause R;
			R.TypeName = Copy(S.TypeName);
			R.Name = Copy(S.Name);
			R.bCatchAll = Copy(S.bCatchAll);
			R.bIsConst = Copy(S.bIsConst);
			R.bIsRef = Copy(S.bIsRef);
			R.Location = Copy(S.Location);
			R.NameLocation = Copy(S.NameLocation);
			R.Body = Copy(S.Body);
			return R;
		}
		FSwitchCase Copy(const FSwitchCase& S)
		{
			FSwitchCase R;
			R.Labels = Copy(S.Labels);
			R.bIsDefault = Copy(S.bIsDefault);
			R.Body = Copy(S.Body);
			return R;
		}
		FFunctionParam Copy(const FFunctionParam& S)
		{
			FFunctionParam R;
			R.DeclMeta = S.DeclMeta;
			R.TypeName = Copy(S.TypeName);
			R.Name = Copy(S.Name);
			R.bIsConst = Copy(S.bIsConst);
			R.bIsRef = Copy(S.bIsRef);
			R.bIsVariadic = Copy(S.bIsVariadic);
			R.DefaultValue = Copy(S.DefaultValue);
			R.DefaultValueSource = S.DefaultValueSource;
			return R;
		}
		FUsingMemberDecl Copy(const FUsingMemberDecl& S)
		{
			FUsingMemberDecl R;
			R.BaseTypeName = Copy(S.BaseTypeName);
			R.MemberName = Copy(S.MemberName);
			R.Access = Copy(S.Access);
			R.Location = Copy(S.Location);
			return R;
		}
		FFriendDecl Copy(const FFriendDecl& S)
		{
			FFriendDecl R;
			R.Kind = Copy(S.Kind);
			R.TypeName = Copy(S.TypeName);
			R.ReturnType = Copy(S.ReturnType);
			R.FunctionName = Copy(S.FunctionName);
			R.Parameters = Copy(S.Parameters);
			R.bIsConstMethod = Copy(S.bIsConstMethod);
			R.Location = Copy(S.Location);
			return R;
		}
		FFieldDecl Copy(const FFieldDecl& S)
		{
			FFieldDecl R;
			R.bConstexpr = S.bConstexpr;
			R.TypeName = Copy(S.TypeName);
			R.Name = Copy(S.Name);
			R.bIsConst = Copy(S.bIsConst);
			R.bIsMutable = Copy(S.bIsMutable);
			R.bIsStatic = Copy(S.bIsStatic);
			R.bAllowUninitializedHostStruct = Copy(S.bAllowUninitializedHostStruct);
			R.Access = Copy(S.Access);
			R.DefaultValue = Copy(S.DefaultValue);
			R.DefaultValueSource = S.DefaultValueSource;
			R.DeclMeta = Copy(S.DeclMeta);
			R.Location = Copy(S.Location);
			return R;
		}
		FMethodDecl Copy(const FMethodDecl& S)
		{
			FMethodDecl R;
			R.bConstexpr = S.bConstexpr;
			R.TemplateParameters = Copy(S.TemplateParameters);
			R.TypeParams = S.TypeParams;
			R.TypeParamDefaults = S.TypeParamDefaults;
			R.Bounds = Copy(S.Bounds);
			R.ReturnType = Copy(S.ReturnType);
			R.Name = Copy(S.Name);
			R.Parameters = Copy(S.Parameters);
			R.Body = Copy(S.Body);
			R.bVirtual = Copy(S.bVirtual);
			R.bOverride = Copy(S.bOverride);
			R.bFinal = Copy(S.bFinal);
			R.bIsConst = Copy(S.bIsConst);
			R.bIsStatic = Copy(S.bIsStatic);
			R.bDeleted = Copy(S.bDeleted);
			R.bPureVirtual = Copy(S.bPureVirtual);
			R.bDefaulted = S.bDefaulted;
			R.Access = Copy(S.Access);
			R.DeclMeta = Copy(S.DeclMeta);
			R.Location = Copy(S.Location);
			return R;
		}
		FBaseCtorInit Copy(const FBaseCtorInit& S)
		{
			FBaseCtorInit R;
			R.BaseName = Copy(S.BaseName);
			R.Args = Copy(S.Args);
			R.Location = Copy(S.Location);
			return R;
		}
		FFieldCtorInit Copy(const FFieldCtorInit& S)
		{
			FFieldCtorInit R;
			R.FieldName = Copy(S.FieldName);
			R.Args = Copy(S.Args);
			R.Location = Copy(S.Location);
			return R;
		}
		FConstructorDecl Copy(const FConstructorDecl& S)
		{
			FConstructorDecl R;
			R.Access = S.Access;
			R.Parameters = Copy(S.Parameters);
			R.bExplicit = Copy(S.bExplicit);
			R.BaseInits = Copy(S.BaseInits);
			R.FieldInits = Copy(S.FieldInits);
			R.Body = Copy(S.Body);
			R.bDefaulted = Copy(S.bDefaulted);
			R.bDeleted = Copy(S.bDeleted);
			R.DeclMeta = Copy(S.DeclMeta);
			R.Location = Copy(S.Location);
			return R;
		}
		FDestructorDecl Copy(const FDestructorDecl& S)
		{
			FDestructorDecl R;
			R.Body = Copy(S.Body);
			R.bVirtual = Copy(S.bVirtual);
			R.bPureVirtual = Copy(S.bPureVirtual);
			R.bDefaulted = Copy(S.bDefaulted);
			R.Access = Copy(S.Access);
			R.DeclMeta = Copy(S.DeclMeta);
			R.Location = Copy(S.Location);
			return R;
		}
		FEnumMemberDecl Copy(const FEnumMemberDecl& S)
		{
			FEnumMemberDecl R;
			R.Initializer = Copy(S.Initializer);
			R.InitializerSource = S.InitializerSource;
			R.Name = Copy(S.Name);
			R.Value = Copy(S.Value);
			R.bHasExplicitValue = Copy(S.bHasExplicitValue);
			R.DeclMeta = Copy(S.DeclMeta);
			R.Location = Copy(S.Location);
			return R;
		}
		FEnumDecl Copy(const FEnumDecl& S)
		{
			FEnumDecl R;
			R.Scope = S.Scope;
			R.Name = Copy(S.Name);
			R.ModulePath = Copy(S.ModulePath);
			R.bExported = Copy(S.bExported);
			R.bPrecompiledPackage = Copy(S.bPrecompiledPackage);
			R.bScoped = Copy(S.bScoped);
			R.UnderlyingTypeName = Copy(S.UnderlyingTypeName);
			R.Members = Copy(S.Members);
			R.DeclMeta = Copy(S.DeclMeta);
			R.Location = Copy(S.Location);
			return R;
		}
		FTypeParamBound Copy(const FTypeParamBound& S)
		{
			FTypeParamBound R;
			R.TypeParam = Copy(S.TypeParam);
			R.InterfaceTypeName = Copy(S.InterfaceTypeName);
			return R;
		}
		FInterfaceDecl Copy(const FInterfaceDecl& S)
		{
			FInterfaceDecl R;
			R.TemplateParameters = Copy(S.TemplateParameters);
			R.Scope = S.Scope;
			R.Name = Copy(S.Name);
			R.ModulePath = Copy(S.ModulePath);
			R.bExported = Copy(S.bExported);
			R.bPrecompiledPackage = Copy(S.bPrecompiledPackage);
			R.bFinal = Copy(S.bFinal);
			R.TypeParams = Copy(S.TypeParams);
			R.TypeParamDefaults = Copy(S.TypeParamDefaults);
			R.BaseInterfaceNames = Copy(S.BaseInterfaceNames);
			R.Bounds = Copy(S.Bounds);
			R.Methods = Copy(S.Methods);
			R.Fields = Copy(S.Fields);
			R.Friends = Copy(S.Friends);
			R.DeclMeta = Copy(S.DeclMeta);
			R.Location = Copy(S.Location);
			return R;
		}
		FStructDecl Copy(const FStructDecl& S)
		{
			FStructDecl R;
			R.TemplateParameters = Copy(S.TemplateParameters);
			R.Scope = S.Scope;
			R.Name = Copy(S.Name);
			R.ModulePath = Copy(S.ModulePath);
			R.bExported = Copy(S.bExported);
			R.bPrecompiledPackage = Copy(S.bPrecompiledPackage);
			R.bIsStatic = Copy(S.bIsStatic);
			R.bFinal = Copy(S.bFinal);
			R.TypeParams = Copy(S.TypeParams);
			R.TypeParamDefaults = Copy(S.TypeParamDefaults);
			R.Bounds = Copy(S.Bounds);
			R.BaseTypeName = Copy(S.BaseTypeName);
			R.BaseInheritanceAccess = Copy(S.BaseInheritanceAccess);
			R.InterfaceTypeNames = Copy(S.InterfaceTypeNames);
			R.Fields = Copy(S.Fields);
			R.Methods = Copy(S.Methods);
			R.Constructors = Copy(S.Constructors);
			R.Destructor = Copy(S.Destructor);
			R.UsingMembers = Copy(S.UsingMembers);
			R.Friends = Copy(S.Friends);
			R.DeclMeta = Copy(S.DeclMeta);
			R.Location = Copy(S.Location);
			return R;
		}
		FClassDecl Copy(const FClassDecl& S)
		{
			FClassDecl R;
			R.TemplateParameters = Copy(S.TemplateParameters);
			R.Scope = S.Scope;
			R.Name = Copy(S.Name);
			R.ModulePath = Copy(S.ModulePath);
			R.bExported = Copy(S.bExported);
			R.bPrecompiledPackage = Copy(S.bPrecompiledPackage);
			R.bIsStatic = Copy(S.bIsStatic);
			R.bFinal = Copy(S.bFinal);
			R.TypeParams = Copy(S.TypeParams);
			R.TypeParamDefaults = Copy(S.TypeParamDefaults);
			R.Bounds = Copy(S.Bounds);
			R.BaseTypeNames = Copy(S.BaseTypeNames);
			R.BaseInheritanceAccess = Copy(S.BaseInheritanceAccess);
			R.InterfaceTypeNames = Copy(S.InterfaceTypeNames);
			R.Fields = Copy(S.Fields);
			R.Methods = Copy(S.Methods);
			R.Constructors = Copy(S.Constructors);
			R.Destructor = Copy(S.Destructor);
			R.ClassDefaults = Copy(S.ClassDefaults);
			R.UsingMembers = Copy(S.UsingMembers);
			R.Friends = Copy(S.Friends);
			R.DeclMeta = Copy(S.DeclMeta);
			R.Location = Copy(S.Location);
			return R;
		}
		FImportDecl Copy(const FImportDecl& S)
		{
			FImportDecl R;
			R.Path = Copy(S.Path);
			R.ImporterModulePath = Copy(S.ImporterModulePath);
			R.ResolvedPath = Copy(S.ResolvedPath);
			R.bReExport = Copy(S.bReExport);
			R.bImplicit = Copy(S.bImplicit);
			R.Location = Copy(S.Location);
			return R;
		}
		FUsingNamespaceDecl Copy(const FUsingNamespaceDecl& S)
		{
			FUsingNamespaceDecl R;
			R.NamespaceName = Copy(S.NamespaceName);
			R.ScopeNamespace = Copy(S.ScopeNamespace);
			R.ModulePath = Copy(S.ModulePath);
			R.Location = Copy(S.Location);
			return R;
		}
		FNamespaceDecl Copy(const FNamespaceDecl& S)
		{
			FNamespaceDecl R;
			R.DeclMeta = S.DeclMeta;
			R.Name = Copy(S.Name);
			R.ModulePath = Copy(S.ModulePath);
			R.Location = Copy(S.Location);
			return R;
		}
		FPackageSourceModule Copy(const FPackageSourceModule& S)
		{
			FPackageSourceModule R;
			R.ModulePath = Copy(S.ModulePath);
			R.Source = Copy(S.Source);
			R.Imports = Copy(S.Imports);
			R.bDeclarationOnly = Copy(S.bDeclarationOnly);
			R.bFromPackage = Copy(S.bFromPackage);
			R.bPackageSurface = Copy(S.bPackageSurface);
			R.FacadePath = Copy(S.FacadePath);
			R.OwnerComponentId = Copy(S.OwnerComponentId);
			R.SourceOwnerId = Copy(S.SourceOwnerId);
			R.GeneratedOperations = Copy(S.GeneratedOperations);
			R.CompileGeneratedOperations = S.CompileGeneratedOperations;
			R.InitialSourceMetadata = S.InitialSourceMetadata;
			return R;
		}
		FTypeAliasDecl Copy(const FTypeAliasDecl& S)
		{
			FTypeAliasDecl R;
			R.Scope = S.Scope;
			R.DeclMeta = S.DeclMeta;
			R.Name = Copy(S.Name);
			R.TypeName = Copy(S.TypeName);
			R.ModulePath = Copy(S.ModulePath);
			R.bExported = Copy(S.bExported);
			R.bPrecompiledPackage = Copy(S.bPrecompiledPackage);
			R.Location = Copy(S.Location);
			return R;
		}
		FFunctionDecl Copy(const FFunctionDecl& S)
		{
			FFunctionDecl R;
			R.bConstexpr = S.bConstexpr;
			R.TemplateParameters = Copy(S.TemplateParameters);
			R.ReturnType = Copy(S.ReturnType);
			R.Name = Copy(S.Name);
			R.ModulePath = Copy(S.ModulePath);
			R.TypeParams = Copy(S.TypeParams);
			R.TypeParamDefaults = Copy(S.TypeParamDefaults);
			R.Bounds = Copy(S.Bounds);
			R.bExported = Copy(S.bExported);
			R.bPrecompiledPackage = Copy(S.bPrecompiledPackage);
			R.Parameters = Copy(S.Parameters);
			R.Body = Copy(S.Body);
			R.bDeleted = Copy(S.bDeleted);
			R.DeclMeta = Copy(S.DeclMeta);
			R.Location = Copy(S.Location);
			return R;
		}
		FSourceDefinition Copy(const FSourceDefinition& S)
		{
			FSourceDefinition R;
			R.ModulePath = Copy(S.ModulePath);
			R.Namespace = Copy(S.Namespace);
			R.PrefixTypeParams = Copy(S.PrefixTypeParams);
			R.PrefixTypeParamDefaults = Copy(S.PrefixTypeParamDefaults);
			for (const auto& Header : S.TemplateHeaders)
			{
				R.TemplateHeaders.push_back(Copy(Header));
			}
			R.Tokens = Copy(S.Tokens);
			R.bBound = Copy(S.bBound);
			return R;
		}
		FProgram Copy(const FProgram& S)
		{
			FProgram R;
			R.ConstantEvaluationLimits = S.ConstantEvaluationLimits;
			R.ScopedTypeUses = Copy(S.ScopedTypeUses);
			R.GeneratedBlockTypes = S.GeneratedBlockTypes;
			R.ModuleEnvironment = S.ModuleEnvironment;
			R.ModuleSourceOwners = S.ModuleSourceOwners;
			R.RequiredExternalDependencies = S.RequiredExternalDependencies;
			R.PreparationSeal = S.PreparationSeal;
			if (S.CompileProcessingSession)
			{
				const auto& O = S.CompileProcessingSession->Options;
				if (O.Registry &&
				    (O.Registry->HasPhase(ECompileProcessingPhase::RawSource) ||
				        O.Registry->HasPhase(ECompileProcessingPhase::ActiveSource)))
				{
					throw std::runtime_error(
					    "an active source processing session must be prepared through its owning program");
				}

				R.CompileProcessingSession = std::make_shared<FCompileProcessingSession>(O);
				R.CompileProcessingSession->Metadata = std::make_shared<FSourceMetadataSnapshot>(*S.CompileProcessingSession->Metadata);
				R.CompileProcessingSession->ReadOnlyModules = S.CompileProcessingSession->ReadOnlyModules;
			}
			R.SourceMetadata = R.CompileProcessingSession ? R.CompileProcessingSession->Metadata : S.SourceMetadata;
			R.CompileGeneratedOperations = S.CompileGeneratedOperations;
			R.CompileProcessingFingerprint = S.CompileProcessingFingerprint;
			R.SourceDefinitions = Copy(S.SourceDefinitions);
			R.bDeclarationsPrepared = Copy(S.bDeclarationsPrepared);
			R.PreparationHostFingerprint = S.PreparationHostFingerprint;
			R.GeneratedDocuments = S.GeneratedDocuments;
			R.AnnotationDependencies = S.AnnotationDependencies;
			R.DeclarationPreparationFingerprint = S.DeclarationPreparationFingerprint;
			R.bDeclarationPreparationCacheable = S.bDeclarationPreparationCacheable;
			R.DeclarationGeneratedOperations = S.DeclarationGeneratedOperations;
			R.bPackageGenerationReplayed = Copy(S.bPackageGenerationReplayed);
			R.PublicMacros = Copy(S.PublicMacros);
			R.PreprocessLineMaps = Copy(S.PreprocessLineMaps);
			R.LifetimeAnchors = Copy(S.LifetimeAnchors);
			R.UsedCompilerBuiltinTypes = Copy(S.UsedCompilerBuiltinTypes);
			R.EntryModulePath = Copy(S.EntryModulePath);
			R.ProjectVisibleModulePaths = Copy(S.ProjectVisibleModulePaths);
			R.Imports = Copy(S.Imports);
			R.Namespaces = Copy(S.Namespaces);
			R.UsingNamespaces = Copy(S.UsingNamespaces);
			R.TypeAliases = Copy(S.TypeAliases);
			R.Enums = Copy(S.Enums);
			R.Structs = Copy(S.Structs);
			R.Classes = Copy(S.Classes);
			R.Interfaces = Copy(S.Interfaces);
			R.Functions = Copy(S.Functions);
			R.PackageSourceModules = Copy(S.PackageSourceModules);
			R.PrecompiledPackageModules = Copy(S.PrecompiledPackageModules);
			R.TopLevelStatements = Copy(S.TopLevelStatements);
			return R;
		}
	} // namespace
	FMethodDecl CloneMethodDeclaration(const FMethodDecl& Method)
	{
		return Copy(Method);
	}
	FFunctionParam CloneFunctionParameter(const FFunctionParam& Parameter)
	{
		return Copy(Parameter);
	}
	std::unique_ptr<FBlockStmt> CloneBlockStatement(const FBlockStmt& Block)
	{
		return std::unique_ptr<FBlockStmt>(static_cast<FBlockStmt*>(CopyNode(Block).release()));
	}
	std::unique_ptr<FExpr> CloneExpression(const FExpr& Expression)
	{
		return CopyNode(Expression);
	}
	std::unique_ptr<FProgram> CloneProgram(const FProgram& Program)
	{
		if (Program.ScriptCompileExtensionTask)
		{
			throw std::invalid_argument(
			    "a script compilation extension task requires uniquely owned AST preparation");
		}

		auto Strings = std::make_shared<std::deque<std::string>>();
		struct FScope
		{
			~FScope()
			{
				CloneStrings = Previous;
			}

			std::deque<std::string>* Previous;
		} Scope{CloneStrings};
		CloneStrings = Strings.get();
		auto Result = std::make_unique<FProgram>(Copy(Program));
		Result->LifetimeAnchors.push_back(std::move(Strings));
		return Result;
	}
} // namespace VeroScript
