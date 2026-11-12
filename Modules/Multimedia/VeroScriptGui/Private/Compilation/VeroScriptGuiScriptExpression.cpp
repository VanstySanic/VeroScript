// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiScriptExpression.h"
#include "VeroScriptAst.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include <algorithm>
#include <map>

namespace VeroScript::Gui
{
	namespace
	{
		class FQualifier
		{
		public:
			FQualifier(const std::string& InSource, const std::vector<FToken>& InTokens,
					   const std::unordered_set<std::string>& InMembers, bool InExternal)
				: Source(InSource), Tokens(InTokens), Members(InMembers), bExternal(InExternal)
			{
			}

			void Expression(const FExpr* Value, const std::unordered_set<std::string>& Locals)
			{
				if (!Value)
				{
					return;
				}
				switch (Value->Kind)
				{
				case EExprKind::Identifier:
					{
						const auto& Item = static_cast<const FIdentifierExpr&>(*Value);
						Name(Item.Name, Item.Location, Locals);
						break;
					}
				case EExprKind::This:
					{
						if (bExternal)
						{
							Error = "gui.external_context_has_no_this: use context";
						}
						break;
					}
				case EExprKind::Unary:
					{
						Expression(static_cast<const FUnaryExpr&>(*Value).Operand.get(), Locals);
						break;
					}
				case EExprKind::Binary:
					{
						const auto& Item = static_cast<const FBinaryExpr&>(*Value);
						Expression(Item.Left.get(), Locals);
						Expression(Item.Right.get(), Locals);
						break;
					}
				case EExprKind::Assign:
					{
						const auto& Item = static_cast<const FAssignExpr&>(*Value);
						Expression(Item.Target.get(), Locals);
						Expression(Item.Value.get(), Locals);
						break;
					}
				case EExprKind::Conditional:
					{
						const auto& Item = static_cast<const FConditionalExpr&>(*Value);
						Expression(Item.Condition.get(), Locals);
						Expression(Item.ThenExpr.get(), Locals);
						Expression(Item.ElseExpr.get(), Locals);
						break;
					}
				case EExprKind::Ref:
					{
						Expression(static_cast<const FRefExpr&>(*Value).Operand.get(), Locals);
						break;
					}
				case EExprKind::Call:
					{
						const auto& Item = static_cast<const FCallExpr&>(*Value);
						if (!Item.Callee.empty())
						{
							Name(Item.Callee, Item.Location, Locals);
						}
						Expression(Item.CalleeExpr.get(), Locals);
						for (const auto& Argument : Item.Arguments)
						{
							Expression(Argument.get(), Locals);
						}
						break;
					}
				case EExprKind::MemberAccess:
					{
						Expression(static_cast<const FMemberAccessExpr&>(*Value).Object.get(), Locals);
						break;
					}
				case EExprKind::MethodCall:
					{
						const auto& Item = static_cast<const FMethodCallExpr&>(*Value);
						Expression(Item.Object.get(), Locals);
						for (const auto& Argument : Item.Arguments)
						{
							Expression(Argument.get(), Locals);
						}
						break;
					}
				case EExprKind::IndexAccess:
					{
						const auto& Item = static_cast<const FIndexAccessExpr&>(*Value);
						Expression(Item.Object.get(), Locals);
						Expression(Item.Index.get(), Locals);
						break;
					}
				case EExprKind::ArrayInit:
					{
						for (const auto& Item : static_cast<const FArrayInitExpr&>(*Value).Elements)
						{
							Expression(Item.get(), Locals);
						}
						break;
					}
				case EExprKind::MapInit:
					{
						for (const auto& Item : static_cast<const FMapInitExpr&>(*Value).Entries)
						{
							Expression(Item.Key.get(), Locals);
							Expression(Item.Value.get(), Locals);
						}
						break;
					}
				case EExprKind::Lambda:
					{
						const auto& Item = static_cast<const FLambdaExpr&>(*Value);
						auto Nested = Locals;
						// Initializers see the enclosing scope, not earlier capture aliases.
						for (const auto& Capture : Item.Captures)
						{
							if (Capture.Initializer)
							{
								Expression(Capture.Initializer.get(), Locals);
								Declare(Capture.Name, Nested);
							}
						}
						for (const auto& Parameter : Item.Parameters)
						{
							Declare(Parameter.Name, Nested);
						}
						Statement(Item.Body.get(), Nested);
						Expression(Item.ExpressionBody.get(), Nested);
						break;
					}
				default:
					{
						break;
					}
				}
			}

			void Statement(const FStmt* Value, std::unordered_set<std::string>& Locals)
			{
				if (!Value)
				{
					return;
				}
				switch (Value->Kind)
				{
				case EStmtKind::Expr:
					{
						Expression(static_cast<const FExprStmt&>(*Value).Expression.get(), Locals);
						break;
					}
				case EStmtKind::Block:
					{
						auto Nested = Locals;
						for (const auto& Item : static_cast<const FBlockStmt&>(*Value).Statements)
						{
							Statement(Item.get(), Nested);
						}
						break;
					}
				case EStmtKind::VarDecl:
					{
						const auto& Item = static_cast<const FVarDeclStmt&>(*Value);
						Declare(Item.Name, Locals);
						Expression(Item.Initializer.get(), Locals);
						break;
					}
				case EStmtKind::VarDeclGroup:
					{
						for (const auto& Item : static_cast<const FVarDeclGroupStmt&>(*Value).Declarations)
						{
							Statement(Item.get(), Locals);
						}
						break;
					}
				case EStmtKind::TypeAlias:
					{
						Declare(static_cast<const FTypeAliasStmt&>(*Value).Name, Locals);
						break;
					}
				case EStmtKind::Return:
					{
						Expression(static_cast<const FReturnStmt&>(*Value).Value.get(), Locals);
						break;
					}
				case EStmtKind::Throw:
					{
						Expression(static_cast<const FThrowStmt&>(*Value).Value.get(), Locals);
						break;
					}
				case EStmtKind::If:
					{
						const auto& Item = static_cast<const FIfStmt&>(*Value);
						Expression(Item.Condition.get(), Locals);
						Statement(Item.ThenBranch.get(), Locals);
						Statement(Item.ElseBranch.get(), Locals);
						break;
					}
				case EStmtKind::While:
					{
						const auto& Item = static_cast<const FWhileStmt&>(*Value);
						Expression(Item.Condition.get(), Locals);
						Statement(Item.Body.get(), Locals);
						break;
					}
				case EStmtKind::DoWhile:
					{
						const auto& Item = static_cast<const FDoWhileStmt&>(*Value);
						Statement(Item.Body.get(), Locals);
						Expression(Item.Condition.get(), Locals);
						break;
					}
				case EStmtKind::For:
					{
						const auto& Item = static_cast<const FForStmt&>(*Value);
						auto Nested = Locals;
						Statement(Item.Init.get(), Nested);
						for (const auto& Expr : Item.InitExpressions)
						{
							Expression(Expr.get(), Nested);
						}
						Expression(Item.Condition.get(), Nested);
						Expression(Item.Step.get(), Nested);
						for (const auto& Expr : Item.StepExpressions)
						{
							Expression(Expr.get(), Nested);
						}
						Statement(Item.Body.get(), Nested);
						break;
					}
				case EStmtKind::ForEach:
					{
						const auto& Item = static_cast<const FForEachStmt&>(*Value);
						Expression(Item.Collection.get(), Locals);
						auto Nested = Locals;
						Declare(Item.ElementName, Nested);
						Declare(Item.KeyName, Nested);
						Statement(Item.Body.get(), Nested);
						break;
					}
				case EStmtKind::Switch:
					{
						const auto& Item = static_cast<const FSwitchStmt&>(*Value);
						Expression(Item.Discriminant.get(), Locals);
						for (const auto& Case : Item.Cases)
						{
							for (const auto& Label : Case.Labels)
							{
								Expression(Label.get(), Locals);
							}
							Statement(Case.Body.get(), Locals);
						}
						break;
					}
				case EStmtKind::Try:
					{
						const auto& Item = static_cast<const FTryStmt&>(*Value);
						Statement(Item.TryBlock.get(), Locals);
						for (const auto& Catch : Item.Catches)
						{
							auto Nested = Locals;
							Declare(Catch.Name, Nested);
							Statement(Catch.Body.get(), Nested);
						}
						Statement(Item.FinallyBlock.get(), Locals);
						break;
					}
				default:
					{
						break;
					}
				}
			}

			std::string Finish() const
			{
				std::string Result = Source;
				for (auto Edit = Edits.rbegin(); Edit != Edits.rend(); ++Edit)
				{
					Result.replace(Edit->first, Edit->second.first, Edit->second.second);
				}
				return Result;
			}

			std::string Error;

			std::vector<FMarkupTextSpan> CopiedSpans() const
			{
				std::vector<FMarkupTextSpan> Result;
				std::size_t Input = 0, Output = 0;
				for (const auto& Edit : Edits)
				{
					if (Input < Edit.first)
					{
						const auto Length = Edit.first - Input;
						Result.push_back({Output, Output + Length, Input});
						Output += Length;
					}
					Output += Edit.second.second.size();
					Input = Edit.first + Edit.second.first;
				}
				if (Input < Source.size())
				{
					Result.push_back({Output, Output + Source.size() - Input, Input});
				}
				return Result;
			}

		private:
			void Declare(const std::string& Name, std::unordered_set<std::string>& Locals)
			{
				if (Name.empty())
				{
					return;
				}
				if (Reserved(Name))
				{
					Error = "gui.reserved_binding_name: " + Name;
				}
				Locals.insert(Name);
			}

			bool Reserved(const std::string& Name) const
			{
				return Name == "self" || Name == "context" || Name == "elements" || Name == "sender" ||
					   Name == "event" || Name == "arg0" || Name == "arg1" || Name == "arg2";
			}

			void Name(const std::string& Name, const FSourceLocation& Location,
					  const std::unordered_set<std::string>& Locals)
			{
				if ((!bExternal && Name != "context") || (bExternal && (!Members.count(Name) || Locals.count(Name))))
				{
					return;
				}
				const auto Token = std::lower_bound(Tokens.begin(), Tokens.end(), Location,
													[](const auto& Item, const auto& Where)
													{
														return Item.Location.Line < Where.Line ||
															   (Item.Location.Line == Where.Line &&
																Item.Location.Column < Where.Column);
													});
				if (Token != Tokens.end() && Token->Lexeme == Name && Token->Location.Line == Location.Line &&
					Token->Location.Column == Location.Column)
				{
					const auto Offset = static_cast<std::size_t>(Token->Lexeme.data() - Source.data());
					Edits[Offset] = bExternal ? std::make_pair(std::size_t{0}, std::string("context."))
											  : std::make_pair(Name.size(), std::string("this"));
					return;
				}
				Error = "gui.binding_source_location_unavailable";
			}

			const std::string& Source;
			const std::vector<FToken>& Tokens;
			const std::unordered_set<std::string>& Members;
			bool bExternal;
			std::map<std::size_t, std::pair<std::size_t, std::string>> Edits;
		};
	} // namespace

	bool RewriteMarkupExpression(const FMarkupExpression& Input, bool bExternal,
								 const std::unordered_set<std::string>& Members, std::string& Code, std::string& Error,
								 std::vector<FMarkupTextSpan>* CopiedSpans)
	{
		const bool bEvent = Input.Event != Platform::EEventType::None;
		const std::string Source = bEvent ? "{" + Input.Text + "}" : Input.Text;
		VeroScriptLexer Lexer(Source, Input.Location.Source);
		const auto& Tokens = Lexer.Tokenize();
		VeroScriptParser Parser(Tokens, Input.Location.Source);
		Parser.SetSourceText(Source);
		FQualifier Qualifier(Source, Tokens, Members, bExternal);
		std::unordered_set<std::string> Locals{"self", "sender", "event",	 "arg0",
											   "arg1", "arg2",	 "elements", "context"};
		if (bEvent)
		{
			auto Body = Parser.ParseBodyFragment({}, {});
			if (Body)
			{
				Qualifier.Statement(Body.get(), Locals);
			}
		}
		else
		{
			auto Expression = Parser.ParseExpressionFragment({}, {});
			if (Expression)
			{
				Qualifier.Expression(Expression.get(), Locals);
			}
		}
		if (Lexer.HasError() || Parser.HasError() || !Qualifier.Error.empty())
		{
			Error = !Qualifier.Error.empty() ? Qualifier.Error
					: Lexer.HasError()		 ? Lexer.GetErrorMessage()
											 : Parser.GetError().Message;
			return false;
		}
		Code = Qualifier.Finish();
		if (CopiedSpans)
		{
			CopiedSpans->clear();
			for (const auto& Span : Qualifier.CopiedSpans())
			{
				const auto Begin = bEvent ? std::max<std::size_t>(1, Span.Begin) : Span.Begin;
				const auto End = bEvent ? std::min(Code.size() - 1, Span.End) : Span.End;
				if (Begin < End)
				{
					CopiedSpans->push_back({Begin - (bEvent ? 1 : 0), End - (bEvent ? 1 : 0),
											Span.SourceBegin + Begin - Span.Begin - (bEvent ? 1 : 0)});
				}
			}
		}
		if (bEvent)
		{
			Code = Code.substr(1, Code.size() - 2);
		}
		return true;
	}
} // namespace VeroScript::Gui
