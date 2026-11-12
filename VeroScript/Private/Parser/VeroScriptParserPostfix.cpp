// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptParser.h"
#include "VeroScriptParserLookup.h"

#include "VeroScriptAst.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParserDetail.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptParserBindings.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <string>

namespace VeroScript
{
	namespace
	{
		template <typename TTypeHead>
		bool HasTemplateApplication(const std::vector<FToken>& Tokens, const std::size_t Opening, const TTypeHead& IsTypeHead)
		{
			std::size_t Angles = 1, Parentheses = 0, Brackets = 0;
			for (std::size_t Index = Opening + 1; Index + 1 < Tokens.size(); ++Index)
			{
				if ((Index & 255u) == 0)
				{
					CheckAnalysisCancellation();
				}
				const auto Part = Tokens[Index].Lexeme;
				if (Part == "(")
				{
					++Parentheses;
				}
				else if (Part == ")")
				{
					if (Parentheses == 0) return false;
					--Parentheses;
				}
				else if (Part == "[")
				{
					++Brackets;
				}
				else if (Part == "]")
				{
					if (Brackets == 0) return false;
					--Brackets;
				}
				else if (Parentheses == 0 && Brackets == 0)
				{
					if (Part == "<" && (Tokens[Index - 1].Lexeme == "cast" || IsTypeHead(Index - 1)))
					{
						++Angles;
					}
					else if (Part == ">" || Part == ">>")
					{
						const std::size_t Count = Part.size();
						if (Angles < Count) return false;
						Angles -= Count;
						if (Angles == 0)
						{
							const auto Next = Tokens[Index + 1].Lexeme;
							return Next == "(" || Next == "." || Next == "::" || Next == ";" || Next == "," || Next == ")" || Next == "]" || Next == "}";
						}
					}
					else if (Part == ";" || Part == "{" || Part == "}")
					{
						return false;
					}
				}
			}
			return false;
		}
	}
	bool VeroScriptParser::HasDependentTypeRoot(const std::string& TypeName)
	{
		if (ActiveTypeParams.empty()) return false;
		if (ActiveTypeParams.count(TypeName.substr(0, TypeName.find_first_of(".<")))) return true;
		// Local aliases can name a dependent owner. Resolve them before deciding
		// whether a qualified expression is a type or an ordinary member access.
		const std::string Resolved = ResolveScopedTypeApplication(ResolveNamespacedTypeName(TypeName));
		return ActiveTypeParams.count(Resolved.substr(0, Resolved.find_first_of(".<"))) != 0;
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParsePostfix()
	{
		const auto Begin = Current;
		std::unique_ptr<FExpr> Expression;
		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "new")
		{
			const FSourceLocation Location = Peek().Location;
			Advance();
			const std::string TypeName = ParseTypeName();
			if (bHasError || TypeName.empty())
			{
				ReportErrorAt(Peek(), "expected type name after new");
				return nullptr;
			}
			for (const FStructDecl& StructDecl : Program->Structs)
			{
				if (StructDecl.Name == TypeName)
				{
					ReportErrorAt(Previous(), "new requires class type");
					return nullptr;
				}
			}

			if (!CheckSymbol("("))
			{
				ReportErrorAt(Peek(), "expected '(' after new expression");
				return nullptr;
			}

			Expression = FinishCall(TypeName, Location);
			if (!Expression)
			{
				return nullptr;
			}
		}

		const bool bDependentMember = !Expression && Check(ETokenKind::Identifier) && Current + 1 < Tokens.size() && (Tokens[Current + 1].Lexeme == "." || Tokens[Current + 1].Lexeme == "::") && HasDependentTypeRoot(std::string(Peek().Lexeme));
		// Soft type aliases can also name globals, which are not recorded in the
		// local runtime binding scopes. Parse their brackets as value indices first;
		// actual user types/aliases and explicit `shared<T>` keep their type grammar.
		const std::size_t IndexedTypeCheckpoint = Current;
		const int IndexedTypePendingCheckpoint = PendingGenericCloseCount;
		bool bIndexedSoftType = false;
		const bool bRuntimeIndex = [&]()
		{
			if (Expression || !Check(ETokenKind::Identifier) || Current + 1 >= Tokens.size() || Tokens[Current + 1].Lexeme != "[")
			{
				return false;
			}
			const std::string Name(Peek().Lexeme);
			if (BindingStore && BindingStore->IsRuntimeName(Name, LocalTypeAliasScopes, RuntimeBindingScopes)) return true;
			std::size_t AliasCount = 0;
			const auto* Aliases = StandardLibrary::GetTypeAliases(AliasCount);
			if (!std::any_of(Aliases, Aliases + AliasCount, [&](const auto& Alias)
			{
				return Name == Alias.QualifiedName || Name == Alias.CanonicalName;
			}))
			{
				return false;
			}
			if (ActiveTypeParams.count(Name) || HostTypeNames.count(Name)) return false;
			for (const auto& Scope : LocalTypeAliasScopes)
			{
				if (Scope.count(Name)) return false;
			}
			const std::string Qualified = ResolveNamespacedTypeName(Name);
			const auto HasDeclaredType = [&](const FProgram& Target)
			{
				const auto& Lookup = GetParsedTypeLookup(Target);
				const auto Contains = [&](const auto& Declarations, const auto& Index, const auto& IsConcrete)
				{
					for (const auto& Candidate : {Name, Qualified})
					{
						const auto Found = Index.find(Candidate);
						if (Found == Index.end())
						{
							continue;
						}
						for (const auto Position : Found->second)
						{
							const auto& Declaration = Declarations[Position];
							if (IsConcrete(Declaration) && ParserDetail::IsDeclVisible(Declaration.ModulePath, Declaration.bExported, FileLabel, &VisibleModulePaths))
							{
								return true;
							}
						}
					}
					return false;
				};
				const auto Concrete = [](const auto& Declaration)
				{
					return TemplateParameterNames(Declaration).empty();
				};
				const auto Named = [](const auto&)
				{
					return true;
				};
				return Contains(Target.Classes, Lookup.Classes, Concrete) || Contains(Target.Structs, Lookup.Structs, Concrete) || Contains(Target.Interfaces, Lookup.Interfaces, Concrete) || Contains(Target.Enums, Lookup.Enums, Named) || Contains(Target.TypeAliases, Lookup.Aliases, Named);
			};
			bIndexedSoftType = !(Program && HasDeclaredType(*Program)) && !(ExternalTypeProgram && HasDeclaredType(*ExternalTypeProgram));
			return bIndexedSoftType;
		}();
		if (!Expression && !bDependentMember && !bRuntimeIndex && CheckTypeName())
		{
			const bool bSoftContainerType =
			    Check(ETokenKind::Keyword) && ParserDetail::IsContainerTypeKeyword(Peek().Lexeme);
			const bool bSoftFnType =
			    Check(ETokenKind::Keyword) && (Peek().Lexeme == "fn" || Peek().Lexeme == "function");
			// Function-type spellings may also introduce lambdas; never treat them as constructors.
			if (!(bSoftContainerType && !LooksLikeContainerTypeApplication()) && !bSoftFnType)
			{
				const std::size_t Checkpoint = Current;
				const int PendingCheckpoint = PendingGenericCloseCount;
				const FSourceLocation Location = Peek().Location;
				const std::string TypeName = ParseTypeName();
				if (!bHasError && CheckSymbol("("))
				{
					Expression = FinishCall(TypeName, Location);
				}
				else if (!bHasError && CheckSymbol("{"))
				{
					if (TypeSupportsBraceInitializer(TypeName))
					{
						Expression = ParseTypedBraceInitializer(TypeName, true);
					}
					else
					{
						// User/host type braces are constructor-call syntax, not aggregate
						// field initialization. The compiler remains the authority for
						// constructibility, overload resolution and interface rejection.
						Expression = FinishBraceCall(TypeName, Location);
					}
				}
				else if (!bHasError && CheckMemberAccessOp())
				{
					Expression = std::make_unique<FIdentifierExpr>(TypeName);
					Expression->Location = Location;
				}
				// Continue through the shared postfix loop when construction succeeded so
				// member access, indexing and immediate calls behave like other expressions.
				if (!Expression)
				{
					// Hard type-parse errors must not be swallowed; only backtrack when the
					// token sequence was a valid type name that is not a constructor call.
					if (bHasError)
					{
						return nullptr;
					}
					Current = Checkpoint;
					PendingGenericCloseCount = PendingCheckpoint;
				}
			}
		}

		if (!Expression)
		{
			Expression = ParsePrimary();
			if (!Expression)
			{
				return nullptr;
			}
		}

		while (true)
		{
			if (bIndexedSoftType && Expression->Kind == EExprKind::IndexAccess && CheckSymbol("{"))
			{
				// Builtins such as module_info have no parsed declaration. Preserve
				// native-array construction without scanning nested value indices twice.
				Current = IndexedTypeCheckpoint;
				PendingGenericCloseCount = IndexedTypePendingCheckpoint;
				const std::string TypeName = ParseTypeName();
				if (bHasError) return nullptr;
				Expression = ParseTypedBraceInitializer(TypeName, true);
				if (!Expression) return nullptr;
				bIndexedSoftType = false;
			}
			if (!CheckSymbol("["))
			{
				bIndexedSoftType = false;
			}
			Expression = FinishExpressionRange(std::move(Expression), Begin);
			if (MatchMemberAccessOp())
			{
				if (!MatchName())
				{
					ReportErrorAt(Peek(), "expected member name after '.' or '::'");
					return nullptr;
				}

				const std::string Member(Previous().Lexeme);
				const FSourceLocation Location = Previous().Location;

				if (TryParseQualifiedTypeMember(Expression, Member, Location))
				{
					if (!Expression || bHasError)
					{
						return nullptr;
					}
					continue;
				}

				std::vector<std::string> QualifiedCallTypeArgs;
				bool HasExplicitArguments = false;
				const bool bReflectionReturnType = Member == "invoke" || Member == "invoke_static";
				if (CheckSymbol("<") && HasTemplateApplication(Tokens, Current, [&](std::size_t Index)
				{
					while (Index >= 2 && Tokens[Index - 1].Lexeme == "::")
					{
						Index -= 2;
					}
					return CheckTypeNameAt(Index);
				}))
				{
					const std::size_t Checkpoint = Current;
					const int PendingCheckpoint = PendingGenericCloseCount;
					Advance();
					do
					{
						if (CheckSymbol(">") || CheckSymbol(">>"))
						{
							break;
						}
						QualifiedCallTypeArgs.push_back(
						    ParseExpressionTypeArgumentName(bReflectionReturnType));
					}
					while (!bHasError && MatchSymbol(","));
					if (!bHasError && MatchCloseGeneric() && (CheckSymbol("(") || CheckSymbol(";") || CheckSymbol(",") || CheckSymbol(")") || CheckSymbol("]")))
					{
						HasExplicitArguments = true;
						// Keep Current at '('; the common call parser below consumes it.
					}
					else
					{
						if (bHasError)
						{
							return nullptr;
						}
						Current = Checkpoint;
						PendingGenericCloseCount = PendingCheckpoint;
						QualifiedCallTypeArgs.clear();
					}
				}

				if (MatchSymbol("("))
				{
					std::vector<std::unique_ptr<FExpr>> Arguments;
					if (!CheckSymbol(")"))
					{
						do
						{
							std::unique_ptr<FExpr> Argument = ParseCallArgument();
							if (!Argument)
							{
								return nullptr;
							}
							Arguments.push_back(std::move(Argument));
						}
						while (MatchSymbol(","));
					}
					if (!MatchSymbol(")"))
					{
						ReportErrorAt(Peek(), "expected ')' after arguments");
						return nullptr;
					}

					if (!ConsumeExpressionChainNode(Location))
					{
						return nullptr;
					}
					auto MethodCall = std::make_unique<FMethodCallExpr>(
					    std::move(Expression),
					    Member,
					    std::move(Arguments));
					MethodCall->TypeArgs = std::move(QualifiedCallTypeArgs);
					MethodCall->bExplicitTemplateArguments = HasExplicitArguments;
					MethodCall->Location = Location;
					Expression = std::move(MethodCall);
				}
				else
				{
					if (!ConsumeExpressionChainNode(Location))
					{
						return nullptr;
					}
					auto MemberAccess = std::make_unique<FMemberAccessExpr>(std::move(Expression), Member);
					MemberAccess->TypeArgs = std::move(QualifiedCallTypeArgs);
					MemberAccess->bExplicitTemplateArguments = HasExplicitArguments;
					MemberAccess->Location = Location;
					Expression = std::move(MemberAccess);
				}
				continue;
			}

			if (Expression->Kind == EExprKind::Identifier && CheckSymbol("("))
			{
				const FIdentifierExpr& Identifier =
				    static_cast<const FIdentifierExpr&>(*Expression);
				std::string Callee = Identifier.Name;
				const bool bStandardLibraryQualified =
				    Identifier.bStandardLibraryQualified;
				Expression = FinishCall(
				    std::move(Callee),
				    Expression->Location,
				    {},
				    bStandardLibraryQualified);
				if (!Expression)
				{
					return nullptr;
				}
				continue;
			}

			if (CheckSymbol("(") && Expression->Kind != EExprKind::Identifier)
			{
				// Do not read Expression in the same call expression that moves it:
				// function-argument evaluation order must not determine correctness.
				const FSourceLocation Location = Expression->Location;
				Expression = FinishCallExpr(std::move(Expression), Location);
				if (!Expression)
				{
					return nullptr;
				}
				continue;
			}

			if (Expression->Kind == EExprKind::Identifier && CheckSymbol("<") && HasTemplateApplication(Tokens, Current, [&](std::size_t Index)
			{
				while (Index >= 2 && Tokens[Index - 1].Lexeme == "::")
				{
					Index -= 2;
				}
				return CheckTypeNameAt(Index);
			}))
			{
				const std::size_t Checkpoint = Current;
				const int PendingCheckpoint = PendingGenericCloseCount;
				Advance();
				std::vector<std::string> TypeArgs;
				do
				{
					if (CheckSymbol(">") || CheckSymbol(">>"))
					{
						break;
					}
					TypeArgs.push_back(ParseExpressionTypeArgumentName());
				}
				while (!bHasError && MatchSymbol(","));

				// Empty <> ≡ arity-0 type application for constructors.
				if (!bHasError && MatchCloseGeneric())
				{
					if (CheckSymbol("("))
					{
						const FIdentifierExpr& Identifier =
						    static_cast<const FIdentifierExpr&>(*Expression);
						std::string Callee = Identifier.Name;
						const bool bStandardLibraryQualified =
						    Identifier.bStandardLibraryQualified;
						Expression = FinishCall(
						    std::move(Callee),
						    Expression->Location,
						    std::move(TypeArgs),
						    bStandardLibraryQualified);
						if (!Expression)
						{
							return nullptr;
						}
						static_cast<FCallExpr&>(*Expression).bExplicitTemplateArguments = true;
						continue;
					}
					if (CheckMemberAccessOp())
					{
						// `Name<Args>.` / `Name<Args>::` : rewrite to an identifier naming
						// the type application so trailing member access attaches
						// (C++-style static member access on a generic type).
						std::string AppName = static_cast<FIdentifierExpr&>(*Expression).Name;
						AppName.push_back('<');
						for (std::size_t Index = 0; Index < TypeArgs.size(); ++Index)
						{
							if (Index > 0)
							{
								AppName.push_back(',');
							}
							AppName += TypeArgs[Index];
						}
						AppName.push_back('>');
						const FSourceLocation IdentLocation = Expression->Location;
						auto Ident = std::make_unique<FIdentifierExpr>(std::move(AppName));
						Ident->Location = IdentLocation;
						Expression = std::move(Ident);
						continue;
					}
					auto& Identifier = static_cast<FIdentifierExpr&>(*Expression);
					Identifier.TypeArgs = std::move(TypeArgs);
					Identifier.bExplicitTemplateArguments = true;
					continue;
				}
				// Incomplete / failed type-arg list: keep the diagnostic.
				// Successful type name without '(' (e.g. comparison) backtracks cleanly.
				if (bHasError)
				{
					return nullptr;
				}
				Current = Checkpoint;
				PendingGenericCloseCount = PendingCheckpoint;
			}

			if (MatchSymbol("["))
			{
				std::unique_ptr<FExpr> Index = ParseExpression();
				if (!Index)
				{
					return nullptr;
				}
				if (!MatchSymbol("]"))
				{
					ReportErrorAt(Peek(), "expected ']' after index expression");
					return nullptr;
				}
				const FSourceLocation IndexLocation = Previous().Location;
				if (!ConsumeExpressionChainNode(IndexLocation))
				{
					return nullptr;
				}
				auto IndexAccess = std::make_unique<FIndexAccessExpr>(std::move(Expression), std::move(Index));
				IndexAccess->Location = IndexLocation;
				Expression = std::move(IndexAccess);
				continue;
			}

			if (CheckSymbol("++") || CheckSymbol("--"))
			{
				const bool bDecrement = Peek().Lexeme == "--";
				const FSourceLocation OpLocation = Peek().Location;
				Advance();
				if (!ParserDetail::IsValidAssignmentTarget(*Expression))
				{
					ReportErrorAt(Previous(), "invalid increment/decrement target");
					return nullptr;
				}

				auto One = std::make_unique<FIntegerLiteralExpr>(1);
				One->Location = OpLocation;
				if (!ConsumeExpressionChainNode(OpLocation))
				{
					return nullptr;
				}
				auto Assign = std::make_unique<FAssignExpr>(
				    std::move(Expression),
				    std::move(One),
				    bDecrement ? EBinaryOp::Subtract : EBinaryOp::Add,
				    false,
				    true);
				Assign->Location = OpLocation;
				Expression = std::move(Assign);
				continue;
			}

			break;
		}

		return FinishExpressionRange(std::move(Expression), Begin);
	}

	bool VeroScriptParser::TryParseQualifiedTypeMember(std::unique_ptr<FExpr>& Expression,
	    const std::string& Member, const FSourceLocation& Location)
	{
		const auto TryGetTypePath = [](auto&& Self, const FExpr& Expr) -> std::string
		{
			if (Expr.Kind == EExprKind::Identifier)
			{
				return static_cast<const FIdentifierExpr&>(Expr).Name;
			}
			if (Expr.Kind == EExprKind::MemberAccess)
			{
				const FMemberAccessExpr& MemberAccess = static_cast<const FMemberAccessExpr&>(Expr);
				const std::string Base = Self(Self, *MemberAccess.Object);
				if (Base.empty())
				{
					return {};
				}
				return Base + "." + MemberAccess.Member;
			}
			return {};
		};
		const std::string BasePath = TryGetTypePath(TryGetTypePath, *Expression);
		if (!BasePath.empty())
		{
			const std::string StandardPath = BasePath + "." + Member;
			if (const std::string_view StandardFunction = StandardLibrary::ResolveFunctionName(StandardPath);
			    !StandardFunction.empty())
			{
				auto StandardIdentifier = std::make_unique<FIdentifierExpr>(std::string(StandardFunction));
				StandardIdentifier->Location = Location;
				StandardIdentifier->bStandardLibraryQualified = true;
				Expression = std::move(StandardIdentifier);
				return true;
			}
			std::string QualPath = ResolveNamespacedTypeName(BasePath + "." + Member);
			const auto Root = BasePath.substr(0, BasePath.find_first_of(".<"));
			const bool ValueRoot =
			    BindingStore && BindingStore->IsRuntimeName(Root, LocalTypeAliasScopes, RuntimeBindingScopes);
			if (!ValueRoot && Expression->Kind == EExprKind::Identifier &&
			    std::any_of(TypeFrames.begin(), TypeFrames.end(), [&](const auto& Frame)
			{
				return BasePath == Frame.Name || BasePath == Frame.Scope.DisplayName ||
				    BasePath == Frame.Scope.SimpleName;
			}))
			{
				// A data-member tail bypasses type parsing; retain the lexical self type.
				static_cast<FIdentifierExpr&>(*Expression).Name =
				    ResolveScopedTypeApplication(ResolveNamespacedTypeName(BasePath));
			}
			// A dependent member may be a field or method. Keep its expression
			// structure until the compiler can bind the actual owner type.
			if (!ValueRoot && !HasDependentTypeRoot(BasePath) && IsUserTypeName(QualPath))
			{
				Program->ScopedTypeUses.push_back(
				    {QualPath, TypeFrames.empty() ? std::string{} : TypeFrames.back().Name, FunctionOwner,
				        Location, TypeFrames.empty() ? std::string(FileLabel) : TypeFrames.back().Scope.ModulePath,
				        CaptureDeclarationContext()});
				if (CheckSymbol("("))
				{
					Expression = FinishCall(ResolveScopedTypeApplication(std::move(QualPath)), Location);
					if (!Expression)
					{
						return true;
					}
					return true;
				}
				if (CheckSymbol("<") && HasTemplateApplication(Tokens, Current, [&](std::size_t Index)
				{
					while (Index >= 2 && Tokens[Index - 1].Lexeme == "::")
					{
						Index -= 2;
					}
					return CheckTypeNameAt(Index);
				}))
				{
					const std::size_t Checkpoint = Current;
					const int PendingCheckpoint = PendingGenericCloseCount;
					Advance();
					std::vector<std::string> TypeArgs;
					do
					{
						if (CheckSymbol(">") || CheckSymbol(">>"))
						{
							break;
						}
						TypeArgs.push_back(ParseExpressionTypeArgumentName());
					}
					while (!bHasError && MatchSymbol(","));
					// Empty <> ≡ arity-0 constructor (same as bare Name()).
					if (!bHasError && MatchCloseGeneric())
					{
						if (CheckSymbol("("))
						{
							Expression = FinishCall(ResolveScopedTypeApplication(std::move(QualPath)), Location,
							    std::move(TypeArgs));
							if (!Expression)
							{
								return true;
							}
							return true;
						}
						if (CheckMemberAccessOp())
						{
							// `Ns.Name<Args>.` / `Ns::Name<Args>::` : rewrite to a
							// type-application identifier so `.Field` / `::Method(`
							// attaches.
							std::string AppName = QualPath;
							AppName.push_back('<');
							for (std::size_t Index = 0; Index < TypeArgs.size(); ++Index)
							{
								if (Index > 0)
								{
									AppName.push_back(',');
								}
								AppName += TypeArgs[Index];
							}
							AppName.push_back('>');
							auto Ident = std::make_unique<FIdentifierExpr>(std::move(AppName));
							Ident->Location = Location;
							Expression = std::move(Ident);
							return true;
						}
					}
					if (bHasError)
					{
						return true;
					}
					Current = Checkpoint;
					PendingGenericCloseCount = PendingCheckpoint;
				}
				// Qualified type used as enum qualifier / value path: keep as Identifier.
				auto Ident = std::make_unique<FIdentifierExpr>(ResolveScopedTypeApplication(std::move(QualPath)));
				Ident->Location = Location;
				Expression = std::move(Ident);
				return true;
			}
		}
		return false;
	}
}
