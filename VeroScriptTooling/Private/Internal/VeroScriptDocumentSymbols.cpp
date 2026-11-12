// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPreparedQuery.h"

#include "VeroScriptDocumentSymbols.h"
#include "VeroScriptAnalysisControl.h"

#include "VeroScriptAst.h"
#include "VeroScriptMemory.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptSoftBuiltins.h"
#include "VeroScriptSourceMetadataQuery.h"
#include "VeroScriptOverlayUtil.h"
#include "VeroScriptBindingVisibility.h"

#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace VeroScript::Tooling::Detail
{
	std::string FormatHostMemberDetail(const FHostMemberDesc& Member, const std::string_view OwnerType)
	{
		if (Member.Kind == EHostMemberKind::EnumMember)
		{
			return std::string(OwnerType) + " = " + std::to_string(Member.EnumValue);
		}
		return FormatHostMemberSignature(Member, OwnerType);
	}

	namespace
	{
		void AppendHostBindingSymbols(
		    const VeroScript::IHostBindingCatalog& Bindings,
		    const FProgram& Program,
		    std::vector<FDocumentSymbol>& OutSymbols);

		void ApplyLocation(FDocumentSymbol& Symbol, const FSourceLocation& Location)
		{
			Symbol.FileLabel.assign(Location.FileLabel.data(), Location.FileLabel.size());
			Symbol.StartLine = Location.Line;
			Symbol.StartColumn = Location.Column;
			Symbol.EndLine = Location.Line;
			Symbol.EndColumn = Location.Column;
		}

		void ApplyDeclarationRange(FDocumentSymbol& Symbol, const FSourceRange& Range,
		    const std::string_view FileLabel)
		{
			const auto& Begin = Range.Position.Begin;
			const auto& End = Range.Position.End;
			if (Range.End > Range.Begin && Begin.Line != 0 && Begin.FileLabel == FileLabel && End.FileLabel == FileLabel && (End.Line > Begin.Line || (End.Line == Begin.Line && End.Column > Begin.Column)))
			{
				Symbol.DeclarationRange = {Begin.Line, Begin.Column, End.Line, End.Column};
			}
		}

		void RemapImportedDeclarationSites(const FProgram& Program, const std::string_view EntryKey,
		    std::vector<FDocumentSymbol>& Symbols,
		    std::unordered_map<std::string, FPreprocessedRangeMapper>& RangeMappers)
		{
			for (auto& Symbol : Symbols)
			{
				CheckAnalysisCancellation();
				if (!Symbol.bSoftBuiltin && !Symbol.FileLabel.empty() && Symbol.StartLine != 0 &&
				    PathUtil::IdentityKey(Symbol.FileLabel) != EntryKey)
				{
					if (const auto* Map = ProgramPreprocessorLineMap(Program, Symbol.FileLabel))
					{
						std::uint32_t Line = 0, Column = 0;
						if (TryRemapPreprocessedToPhysical(*Map, Symbol.StartLine, Symbol.StartColumn, Line, Column))
						{
							Symbol.StartLine = Line;
							Symbol.StartColumn = Column;
						}
						if (TryRemapPreprocessedToPhysical(*Map, Symbol.EndLine, Symbol.EndColumn, Line, Column))
						{
							Symbol.EndLine = Line;
							Symbol.EndColumn = Column;
						}
						if (Symbol.DeclarationRange.StartLine != 0)
						{
							FTextRange Physical;
							auto Mapper = RangeMappers.find(Symbol.FileLabel);
							if (Mapper == RangeMappers.end() && Program.SourceMetadata)
							{
								if (const auto* Raw = Program.SourceMetadata->FindDocument(Symbol.FileLabel + ":raw"))
								{
									Mapper = RangeMappers.try_emplace(Symbol.FileLabel, *Map, Raw->Text).first;
								}
							}
							Symbol.DeclarationRange = Mapper != RangeMappers.end() && Mapper->second.TryRemap(Symbol.DeclarationRange, Physical)
							    ? Physical
							    : FTextRange{0, 0, 0, 0};
						}
					}
					else
					{
						Symbol.DeclarationRange = {0, 0, 0, 0};
					}
				}
				if (!Symbol.Children.empty())
				{
					RemapImportedDeclarationSites(Program, EntryKey, Symbol.Children, RangeMappers);
				}
			}
		}

		void RemapImportedDeclarationSites(const FProgram& Program, const std::string_view EntryKey,
		    std::vector<FDocumentSymbol>& Symbols)
		{
			std::unordered_map<std::string, FPreprocessedRangeMapper> RangeMappers;
			RemapImportedDeclarationSites(Program, EntryKey, Symbols, RangeMappers);
		}

		std::string FormatParams(const std::vector<FFunctionParam>& Parameters)
		{
			std::ostringstream Stream;
			Stream << '(';
			for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
			{
				if (Index > 0)
				{
					Stream << ", ";
				}
				const FFunctionParam& Param = Parameters[Index];
				if (Param.bIsConst)
				{
					Stream << "const ";
				}
				if (Param.bIsVariadic)
				{
					Stream << Param.TypeName;
					if (Param.bIsRef)
					{
						Stream << '&';
					}
					Stream << "... " << Param.Name;
				}
				else
				{
					Stream << Param.TypeName;
					if (Param.bIsRef)
					{
						Stream << '&';
					}
					Stream << ' ' << Param.Name;
				}
				if (Param.DefaultValue)
				{
					Stream << " = …";
				}
			}
			Stream << ')';
			return Stream.str();
		}

		std::string FormatReturnType(const std::string_view TypeName)
		{
			if (TypeName.rfind("const ref ", 0) == 0)
			{
				return "const " + std::string(TypeName.substr(10)) + "&";
			}
			if (TypeName.rfind("ref ", 0) == 0)
			{
				return std::string(TypeName.substr(4)) + "&";
			}
			return std::string(TypeName);
		}

		std::string FormatTypeParams(
		    const std::vector<std::string>& TypeParams,
		    const std::vector<std::string>& TypeParamDefaults)
		{
			if (TypeParams.empty())
			{
				return {};
			}
			std::ostringstream Stream;
			Stream << '<';
			for (std::size_t Index = 0; Index < TypeParams.size(); ++Index)
			{
				if (Index > 0)
				{
					Stream << ", ";
				}
				Stream << TypeParams[Index];
				if (Index < TypeParamDefaults.size() && !TypeParamDefaults[Index].empty())
				{
					Stream << " = " << TypeParamDefaults[Index];
				}
			}
			Stream << '>';
			return Stream.str();
		}

		template <typename TDeclaration>
		std::string FormatDeclaredTemplateParams(const TDeclaration& Declaration, const bool OwnOnly = false)
		{
			const auto& Schema = Declaration.TemplateParameters;
			if (Schema.IsEmpty()) return FormatTypeParams(Declaration.TypeParams, Declaration.TypeParamDefaults);
			const auto Begin = OwnOnly ? Schema.GetEnclosingCount() : 0;
			if (Begin == Schema.GetParameters().size()) return {};
			std::ostringstream Stream;
			Stream << '<';
			for (std::size_t Index = Begin; Index < Schema.GetParameters().size(); ++Index)
			{
				if (Index != Begin)
				{
					Stream << ", ";
				}
				const auto& Parameter = Schema.GetParameters()[Index];
				if (Parameter.Kind == ETemplateParameterKind::Value)
				{
					Stream << Parameter.ValueTypeName << ' ';
				}
				Stream << Parameter.Name;
				if (Parameter.HasDefault())
				{
					Stream << " = " << (Parameter.Kind == ETemplateParameterKind::Type ? Parameter.DefaultTypeName : Parameter.DefaultSource);
				}
			}
			Stream << '>';
			return Stream.str();
		}

		std::string FormatGenericHostTemplateName(
		    const std::string_view Name,
		    const std::size_t Arity)
		{
			std::ostringstream Stream;
			Stream << Name << '<';
			for (std::size_t Index = 0; Index < Arity; ++Index)
			{
				if (Index > 0)
				{
					Stream << ", ";
				}
				Stream << 'T' << (Index + 1);
			}
			Stream << '>';
			return Stream.str();
		}

		std::string FormatBounds(const std::vector<FTypeParamBound>& Bounds)
		{
			if (Bounds.empty())
			{
				return {};
			}
			std::ostringstream Stream;
			Stream << "where ";
			for (std::size_t Index = 0; Index < Bounds.size(); ++Index)
			{
				if (Index > 0)
				{
					Stream << ", ";
				}
				const FTypeParamBound& Bound = Bounds[Index];
				Stream << Bound.TypeParam << " : " << Bound.InterfaceTypeName;
			}
			return Stream.str();
		}

		std::string FormatInheritance(
		    const std::string& BaseTypeName,
		    const std::vector<std::string>& BaseTypeNames,
		    const std::vector<std::string>& InterfaceTypeNames)
		{
			std::vector<std::string> Parts;
			if (!BaseTypeName.empty())
			{
				Parts.push_back(BaseTypeName);
			}
			for (const std::string& Name : BaseTypeNames)
			{
				if (!Name.empty())
				{
					Parts.push_back(Name);
				}
			}
			for (const std::string& Name : InterfaceTypeNames)
			{
				if (!Name.empty())
				{
					Parts.push_back(Name);
				}
			}
			if (Parts.empty())
			{
				return {};
			}
			std::ostringstream Stream;
			Stream << ": ";
			for (std::size_t Index = 0; Index < Parts.size(); ++Index)
			{
				if (Index > 0)
				{
					Stream << ", ";
				}
				Stream << Parts[Index];
			}
			return Stream.str();
		}

		std::string JoinDetails(const std::string& Left, const std::string& Right)
		{
			if (Left.empty())
			{
				return Right;
			}
			if (Right.empty())
			{
				return Left;
			}
			return Left + ' ' + Right;
		}

		/**
		 * Reconstruct a compact annotation summary from flattened DeclMeta
		 * (`__annot` markers + args), e.g. `[UCLASS()] [UPROPERTY(EditAnywhere, meta=(ClampMin=0))]`.
		 * Nested group open markers (`key` / `"__annot"`) are rebuilt as `key=(...)`,
		 * not rendered as `key=__annot`.
		 */
		std::string FormatDeclMeta(const VeroScript::FDeclMeta& Meta)
		{
			if (Meta.empty())
			{
				return {};
			}

			struct FArgNode
			{
				std::string Name;
				bool bGroup = false;
				bool bHasValue = false;
				std::string Value;
				std::vector<FArgNode> Children;
			};

			const auto FindChild = [](FArgNode& Parent, const std::string& Name) -> FArgNode*
			{
				for (auto& Child : Parent.Children)
				{
					if (Child.Name == Name)
					{
						return &Child;
					}
				}
				return nullptr;
			};
			const auto EnsureChild = [&FindChild](FArgNode& Parent, const std::string& Name) -> FArgNode&
			{
				if (FArgNode* Existing = FindChild(Parent, Name))
				{
					return *Existing;
				}
				Parent.Children.emplace_back();
				Parent.Children.back().Name = Name;
				return Parent.Children.back();
			};
			const auto WalkPath = [&EnsureChild](FArgNode& Root, const std::string& Path) -> FArgNode&
			{
				FArgNode* Current = &Root;
				std::size_t Start = 0;
				while (Start <= Path.size())
				{
					const std::size_t Dot = Path.find('.', Start);
					const std::string Segment = Dot == std::string::npos
					    ? Path.substr(Start)
					    : Path.substr(Start, Dot - Start);
					if (!Segment.empty())
					{
						Current = &EnsureChild(*Current, Segment);
					}
					if (Dot == std::string::npos)
					{
						break;
					}
					Start = Dot + 1;
				}
				return *Current;
			};
			const auto FormatNode = [](const FArgNode& Node, std::ostringstream& Out, auto&& Self) -> void
			{
				bool bFirst = true;
				for (const auto& Child : Node.Children)
				{
					// Double-underscore arguments are compiler/tooling implementation
					// markers. They remain in DeclMeta for consumers but must never
					// leak into user-facing outline, completion, or hover details.
					if (Child.Name.rfind("__", 0) == 0)
					{
						continue;
					}
					if (!bFirst)
					{
						Out << ", ";
					}
					bFirst = false;
					Out << Child.Name;
					if (Child.bGroup)
					{
						Out << "=(";
						Self(Child, Out, Self);
						Out << ')';
					}
					else if (Child.bHasValue && !Child.Value.empty())
					{
						Out << '=' << Child.Value;
					}
				}
			};

			std::ostringstream Out;
			bool bWroteAnnot = false;
			std::string CurrentName;
			FArgNode Root;
			const auto Flush = [&]()
			{
				if (CurrentName.empty())
				{
					return;
				}
				if (bWroteAnnot)
				{
					Out << ' ';
				}
				bWroteAnnot = true;
				Out << '[' << CurrentName << '(';
				FormatNode(Root, Out, FormatNode);
				Out << ")]";
				CurrentName.clear();
				Root = FArgNode{};
			};

			for (const auto& Pair : Meta)
			{
				if (Pair.first == "__annot")
				{
					Flush();
					CurrentName = Pair.second;
					continue;
				}
				if (CurrentName.empty())
				{
					continue;
				}
				// Nested group open marker (`meta` / `"__annot"`) — not a leaf value.
				if (Pair.second == "__annot")
				{
					FArgNode& Node = WalkPath(Root, Pair.first);
					Node.bGroup = true;
					continue;
				}
				FArgNode& Node = WalkPath(Root, Pair.first);
				Node.bHasValue = true;
				Node.Value = Pair.second;
			}
			Flush();
			return Out.str();
		}

		void ApplyDeclMetaDetail(FDocumentSymbol& Symbol, const VeroScript::FDeclMeta& Meta,
		    const std::string_view FileLabel)
		{
			// Processors may retain metadata from another source document. Such an
			// extent cannot be interpreted in the declaration's current document.
			ApplyDeclarationRange(Symbol, Meta.SourceRange, FileLabel);
			// Append after type/signature so TypeNameFromDetail / CallHelp keep working.
			Symbol.Detail = JoinDetails(Symbol.Detail, FormatDeclMeta(Meta));
			if (!Meta.StableId.empty() || !Meta.Documentation.empty() || Meta.CommentRetention != ECommentRetention::Full)
			{
				Symbol.Documentation = Meta.Documentation;
				Symbol.bHasSourceDocumentation = true;
			}
		}

		bool IsEntryModule(const std::string& ModulePath, const std::string& EntryModulePath)
		{
			return ModulePath == EntryModulePath;
		}

		bool ShouldCollectDecl(
		    const std::string& ModulePath,
		    const std::string& EntryModulePath,
		    const bool bExported,
		    const FDocumentSymbolOptions& Options,
		    const FProgram& Program)
		{
			if (IsEntryModule(ModulePath, EntryModulePath))
			{
				return true;
			}
			if (!Options.bIncludeImportedExports)
			{
				return false;
			}
			return ModuleVisibility::CanAccessSymbol(
			    Program,
			    ModulePath,
			    bExported,
			    EntryModulePath);
		}

		FDocumentSymbol MakeLabelSymbol(const FLabelStmt& Label)
		{
			FDocumentSymbol Symbol;
			Symbol.Kind = EDocumentSymbolKind::Label;
			Symbol.Name = Label.Name;
			Symbol.Detail = "label";
			ApplyDeclarationRange(Symbol, Label.SourceRange, Label.Location.FileLabel);
			ApplyLocation(Symbol, Label.Location);
			return Symbol;
		}

		void AppendLabelsFromExpr(const FExpr& Expr, std::vector<FDocumentSymbol>& OutChildren);

		void AppendLabelsFromStmt(const FStmt& Statement, std::vector<FDocumentSymbol>& OutChildren)
		{
			CheckAnalysisCancellation();
			switch (Statement.Kind)
			{
			case EStmtKind::Label:
				{
					OutChildren.push_back(MakeLabelSymbol(static_cast<const FLabelStmt&>(Statement)));
					break;
				}
			case EStmtKind::TypeAlias:
				{
					const auto& Alias = static_cast<const FTypeAliasStmt&>(Statement);
					if (Alias.bTypeDefinition)
					{
						break;
					}
					FDocumentSymbol Symbol;
					Symbol.Kind = EDocumentSymbolKind::TypeAlias;
					Symbol.Name = Alias.Name;
					Symbol.Detail = Alias.TypeName;
					ApplyDeclMetaDetail(Symbol, Alias.DeclMeta, Alias.Location.FileLabel);
					ApplyLocation(Symbol, Alias.Location);
					OutChildren.push_back(std::move(Symbol));
					break;
				}
			case EStmtKind::Block:
				{
					for (const std::unique_ptr<FStmt>& Child :
					    static_cast<const FBlockStmt&>(Statement).Statements)
					{
						if (Child)
						{
							AppendLabelsFromStmt(*Child, OutChildren);
						}
					}
					break;
				}
			case EStmtKind::Expr:
				{
					if (static_cast<const FExprStmt&>(Statement).Expression)
					{
						AppendLabelsFromExpr(
						    *static_cast<const FExprStmt&>(Statement).Expression,
						    OutChildren);
					}
					break;
				}
			case EStmtKind::VarDecl:
				{
					if (static_cast<const FVarDeclStmt&>(Statement).Initializer)
					{
						AppendLabelsFromExpr(
						    *static_cast<const FVarDeclStmt&>(Statement).Initializer,
						    OutChildren);
					}
					break;
				}
			case EStmtKind::VarDeclGroup:
				{
					for (const std::unique_ptr<FVarDeclStmt>& Declaration :
					    static_cast<const FVarDeclGroupStmt&>(Statement).Declarations)
					{
						if (Declaration)
						{
							AppendLabelsFromStmt(*Declaration, OutChildren);
						}
					}
					break;
				}
			case EStmtKind::Return:
				{
					if (static_cast<const FReturnStmt&>(Statement).Value)
					{
						AppendLabelsFromExpr(
						    *static_cast<const FReturnStmt&>(Statement).Value,
						    OutChildren);
					}
					break;
				}
			case EStmtKind::Throw:
				{
					if (static_cast<const FThrowStmt&>(Statement).Value)
					{
						AppendLabelsFromExpr(
						    *static_cast<const FThrowStmt&>(Statement).Value,
						    OutChildren);
					}
					break;
				}
			case EStmtKind::Try:
				{
					const auto& TryStmt = static_cast<const FTryStmt&>(Statement);
					if (TryStmt.TryBlock)
					{
						AppendLabelsFromStmt(*TryStmt.TryBlock, OutChildren);
					}
					for (const FCatchClause& Clause : TryStmt.Catches)
					{
						if (Clause.Body)
						{
							AppendLabelsFromStmt(*Clause.Body, OutChildren);
						}
					}
					if (TryStmt.FinallyBlock)
					{
						AppendLabelsFromStmt(*TryStmt.FinallyBlock, OutChildren);
					}
					break;
				}
			case EStmtKind::If:
				{
					const auto& IfStmt = static_cast<const FIfStmt&>(Statement);
					if (IfStmt.Condition)
					{
						AppendLabelsFromExpr(*IfStmt.Condition, OutChildren);
					}
					if (IfStmt.ThenBranch)
					{
						AppendLabelsFromStmt(*IfStmt.ThenBranch, OutChildren);
					}
					if (IfStmt.ElseBranch)
					{
						AppendLabelsFromStmt(*IfStmt.ElseBranch, OutChildren);
					}
					break;
				}
			case EStmtKind::While:
				{
					const auto& WhileStmt = static_cast<const FWhileStmt&>(Statement);
					if (WhileStmt.Condition)
					{
						AppendLabelsFromExpr(*WhileStmt.Condition, OutChildren);
					}
					if (WhileStmt.Body)
					{
						AppendLabelsFromStmt(*WhileStmt.Body, OutChildren);
					}
					break;
				}
			case EStmtKind::DoWhile:
				{
					const auto& DoWhileStmt = static_cast<const FDoWhileStmt&>(Statement);
					if (DoWhileStmt.Body)
					{
						AppendLabelsFromStmt(*DoWhileStmt.Body, OutChildren);
					}
					if (DoWhileStmt.Condition)
					{
						AppendLabelsFromExpr(*DoWhileStmt.Condition, OutChildren);
					}
					break;
				}
			case EStmtKind::For:
				{
					const auto& ForStmt = static_cast<const FForStmt&>(Statement);
					if (ForStmt.Init)
					{
						AppendLabelsFromStmt(*ForStmt.Init, OutChildren);
					}
					for (const std::unique_ptr<FExpr>& InitExpression : ForStmt.InitExpressions)
					{
						if (InitExpression)
						{
							AppendLabelsFromExpr(*InitExpression, OutChildren);
						}
					}
					if (ForStmt.Condition)
					{
						AppendLabelsFromExpr(*ForStmt.Condition, OutChildren);
					}
					if (ForStmt.Step)
					{
						AppendLabelsFromExpr(*ForStmt.Step, OutChildren);
					}
					for (const std::unique_ptr<FExpr>& StepExpression : ForStmt.StepExpressions)
					{
						if (StepExpression)
						{
							AppendLabelsFromExpr(*StepExpression, OutChildren);
						}
					}
					if (ForStmt.Body)
					{
						AppendLabelsFromStmt(*ForStmt.Body, OutChildren);
					}
					break;
				}
			case EStmtKind::ForEach:
				{
					const auto& ForEachStmt = static_cast<const FForEachStmt&>(Statement);
					if (ForEachStmt.Collection)
					{
						AppendLabelsFromExpr(*ForEachStmt.Collection, OutChildren);
					}
					if (ForEachStmt.Body)
					{
						AppendLabelsFromStmt(*ForEachStmt.Body, OutChildren);
					}
					break;
				}
			case EStmtKind::Switch:
				{
					for (const FSwitchCase& Case : static_cast<const FSwitchStmt&>(Statement).Cases)
					{
						if (Case.Body)
						{
							AppendLabelsFromStmt(*Case.Body, OutChildren);
						}
					}
					break;
				}
			default:
				{
					break;
				}
			}
		}

		void AppendLabelsFromExpr(const FExpr& Expr, std::vector<FDocumentSymbol>& OutChildren)
		{
			CheckAnalysisCancellation();
			switch (Expr.Kind)
			{
			case EExprKind::Lambda:
				{
					const auto& Lambda = static_cast<const FLambdaExpr&>(Expr);
					for (const auto& Capture : Lambda.Captures)
					{
						if (Capture.Initializer)
						{
							AppendLabelsFromExpr(*Capture.Initializer, OutChildren);
						}
					}
					if (Lambda.Body)
					{
						AppendLabelsFromStmt(*Lambda.Body, OutChildren);
					}
					if (Lambda.ExpressionBody)
					{
						AppendLabelsFromExpr(*Lambda.ExpressionBody, OutChildren);
					}
					break;
				}
			case EExprKind::Call:
				{
					const auto& Call = static_cast<const FCallExpr&>(Expr);
					if (Call.CalleeExpr)
					{
						AppendLabelsFromExpr(*Call.CalleeExpr, OutChildren);
					}
					for (const std::unique_ptr<FExpr>& Arg : Call.Arguments)
					{
						if (Arg)
						{
							AppendLabelsFromExpr(*Arg, OutChildren);
						}
					}
					break;
				}
			case EExprKind::Unary:
				{
					if (static_cast<const FUnaryExpr&>(Expr).Operand)
					{
						AppendLabelsFromExpr(
						    *static_cast<const FUnaryExpr&>(Expr).Operand,
						    OutChildren);
					}
					break;
				}
			case EExprKind::Binary:
				{
					const auto& Binary = static_cast<const FBinaryExpr&>(Expr);
					if (Binary.Left)
					{
						AppendLabelsFromExpr(*Binary.Left, OutChildren);
					}
					if (Binary.Right)
					{
						AppendLabelsFromExpr(*Binary.Right, OutChildren);
					}
					break;
				}
			case EExprKind::Assign:
				{
					const auto& Assign = static_cast<const FAssignExpr&>(Expr);
					if (Assign.Target)
					{
						AppendLabelsFromExpr(*Assign.Target, OutChildren);
					}
					if (Assign.Value)
					{
						AppendLabelsFromExpr(*Assign.Value, OutChildren);
					}
					break;
				}
			case EExprKind::Conditional:
				{
					const auto& Cond = static_cast<const FConditionalExpr&>(Expr);
					if (Cond.Condition)
					{
						AppendLabelsFromExpr(*Cond.Condition, OutChildren);
					}
					if (Cond.ThenExpr)
					{
						AppendLabelsFromExpr(*Cond.ThenExpr, OutChildren);
					}
					if (Cond.ElseExpr)
					{
						AppendLabelsFromExpr(*Cond.ElseExpr, OutChildren);
					}
					break;
				}
			case EExprKind::Ref:
				{
					if (static_cast<const FRefExpr&>(Expr).Operand)
					{
						AppendLabelsFromExpr(
						    *static_cast<const FRefExpr&>(Expr).Operand,
						    OutChildren);
					}
					break;
				}
			case EExprKind::MemberAccess:
				{
					if (static_cast<const FMemberAccessExpr&>(Expr).Object)
					{
						AppendLabelsFromExpr(
						    *static_cast<const FMemberAccessExpr&>(Expr).Object,
						    OutChildren);
					}
					break;
				}
			case EExprKind::MethodCall:
				{
					const auto& Method = static_cast<const FMethodCallExpr&>(Expr);
					if (Method.Object)
					{
						AppendLabelsFromExpr(*Method.Object, OutChildren);
					}
					for (const std::unique_ptr<FExpr>& Arg : Method.Arguments)
					{
						if (Arg)
						{
							AppendLabelsFromExpr(*Arg, OutChildren);
						}
					}
					break;
				}
			case EExprKind::IndexAccess:
				{
					const auto& Index = static_cast<const FIndexAccessExpr&>(Expr);
					if (Index.Object)
					{
						AppendLabelsFromExpr(*Index.Object, OutChildren);
					}
					if (Index.Index)
					{
						AppendLabelsFromExpr(*Index.Index, OutChildren);
					}
					break;
				}
			case EExprKind::ArrayInit:
				{
					for (const std::unique_ptr<FExpr>& Element :
					    static_cast<const FArrayInitExpr&>(Expr).Elements)
					{
						if (Element)
						{
							AppendLabelsFromExpr(*Element, OutChildren);
						}
					}
					break;
				}
			case EExprKind::MapInit:
				{
					for (const auto& Entry : static_cast<const FMapInitExpr&>(Expr).Entries)
					{
						if (Entry.Key)
						{
							AppendLabelsFromExpr(*Entry.Key, OutChildren);
						}
						if (Entry.Value)
						{
							AppendLabelsFromExpr(*Entry.Value, OutChildren);
						}
					}
					break;
				}
			default:
				{
					break;
				}
			}
		}

		void AppendLabelsFromBlock(
		    const FBlockStmt* Body,
		    std::vector<FDocumentSymbol>& OutChildren)
		{
			if (Body)
			{
				AppendLabelsFromStmt(*Body, OutChildren);
			}
		}

		FDocumentSymbol MakeMethodSymbol(
		    const FProgram& Program,
		    const FMethodDecl& Method,
		    const bool bInterfaceMember = false)
		{
			FDocumentSymbol Symbol;
			Symbol.Kind = EDocumentSymbolKind::Method;
			Symbol.Name = Method.Name;
			Symbol.bIsStatic = Method.bIsStatic;
			Symbol.CallableOwner = Method.Body ? Method.Body->CallableOwner : std::string{};
			if (std::string_view(Method.Location.FileLabel).rfind("veroscript-generated:", 0) == 0)
			{
				Symbol.Documentation = "Generated method (read-only).";
				Symbol.bReadOnly = true;
			}
			std::string Detail = FormatReturnType(Method.ReturnType) + FormatParams(Method.Parameters);
			if (Method.bConstexpr)
			{
				Detail = "constexpr " + Detail;
			}
			if (Method.bIsStatic)
			{
				Detail = "static " + Detail;
			}
			if (Method.bIsConst)
			{
				Detail += " const";
			}
			if (Method.bVirtual)
			{
				Detail = "virtual " + Detail;
			}
			if (Method.bOverride)
			{
				Detail = JoinDetails(Detail, "override");
			}
			if (Method.bFinal)
			{
				Detail = JoinDetails(Detail, "final");
			}
			// Preserve explicit default/delete state for special members. Interface
			// methods are abstract only for `= 0`; nonvirtual implementations use
			// the existing default marker.
			if (Method.bDeleted)
			{
				Detail = JoinDetails(Detail, "delete");
			}
			else if (Method.bDefaulted)
			{
				Detail = JoinDetails(Detail, "default");
			}
			else if (Method.bPureVirtual)
			{
				Detail = JoinDetails(Detail, "abstract");
			}
			else if (bInterfaceMember && !Method.bVirtual)
			{
				Detail = JoinDetails(Detail, "default");
			}
			if (!TemplateParameterNames(Method).empty())
			{
				Detail = "template" + FormatDeclaredTemplateParams(Method) + " " + Detail;
			}
			Detail = JoinDetails(Detail, FormatBounds(Method.Bounds));
			Symbol.Detail = std::move(Detail);
			ApplyDeclMetaDetail(Symbol, Method.DeclMeta, Method.Location.FileLabel);
			Symbol.Access = Method.Access;
			ApplyLocation(Symbol, Method.Location);
			Symbol.SourceImplementation = SourceImplementationLocation(Program, Method.Body.get());
			Symbol.GeneratedOrigin = GeneratedOriginLocation(Program, Method.Location.FileLabel);
			AppendLabelsFromBlock(Method.Body.get(), Symbol.Children);
			return Symbol;
		}

		FDocumentSymbol MakeConstructorSymbol(
		    const FConstructorDecl& Constructor,
		    const std::string& TypeName)
		{
			FDocumentSymbol Symbol;
			Symbol.Kind = EDocumentSymbolKind::Constructor;
			Symbol.Name = TypeName;
			Symbol.CallableOwner = Constructor.Body ? Constructor.Body->CallableOwner : std::string{};
			Symbol.Detail = FormatParams(Constructor.Parameters);
			if (Constructor.bDeleted)
			{
				Symbol.Detail = JoinDetails(Symbol.Detail, "delete");
			}
			else if (Constructor.bDefaulted)
			{
				Symbol.Detail = JoinDetails(Symbol.Detail, "default");
			}
			ApplyDeclMetaDetail(Symbol, Constructor.DeclMeta, Constructor.Location.FileLabel);
			Symbol.Access = Constructor.Access;
			ApplyLocation(Symbol, Constructor.Location);
			AppendLabelsFromBlock(Constructor.Body.get(), Symbol.Children);
			return Symbol;
		}

		FDocumentSymbol MakeDestructorSymbol(
		    const FDestructorDecl& Destructor,
		    const std::string& TypeName)
		{
			FDocumentSymbol Symbol;
			Symbol.Kind = EDocumentSymbolKind::Destructor;
			Symbol.Name = "~" + TypeName;
			Symbol.CallableOwner = Destructor.Body ? Destructor.Body->CallableOwner : std::string{};
			std::string Detail = "()";
			if (Destructor.bVirtual)
			{
				Detail = "virtual " + Detail;
			}
			if (Destructor.bDefaulted)
			{
				Detail = JoinDetails(Detail, "default");
			}
			Symbol.Detail = std::move(Detail);
			ApplyDeclMetaDetail(Symbol, Destructor.DeclMeta, Destructor.Location.FileLabel);
			Symbol.Access = Destructor.Access;
			ApplyLocation(Symbol, Destructor.Location);
			AppendLabelsFromBlock(Destructor.Body.get(), Symbol.Children);
			return Symbol;
		}

		FDocumentSymbol MakeFieldSymbol(const FFieldDecl& Field)
		{
			FDocumentSymbol Symbol;
			Symbol.Kind = EDocumentSymbolKind::Field;
			Symbol.Name = Field.Name;
			Symbol.bReadOnly = Field.bIsConst;
			Symbol.bIsStatic = Field.bIsStatic;
			Symbol.Detail = Field.bConstexpr ? ("constexpr " + Field.TypeName) : Field.bIsConst ? ("const " + Field.TypeName)
			                                                                                    : Field.TypeName;
			if (Field.bIsMutable)
			{
				Symbol.Detail = "mutable " + Symbol.Detail;
			}
			if (Field.bIsStatic)
			{
				Symbol.Detail = "static " + Symbol.Detail;
			}
			ApplyDeclMetaDetail(Symbol, Field.DeclMeta, Field.Location.FileLabel);
			Symbol.Access = Field.Access;
			ApplyLocation(Symbol, Field.Location);
			return Symbol;
		}

		void CollectSymbolsInto(
		    const FProgram& Program,
		    const FDocumentSymbolOptions& Options,
		    std::vector<FDocumentSymbol>& OutSymbols)
		{
			const std::string& Entry = Program.EntryModulePath;
			if (Options.bIncludeImports)
			{
				for (const FImportDecl& Import : Program.Imports)
				{
					CheckAnalysisCancellation();
					if (!IsEntryModule(Import.ImporterModulePath, Entry))
					{
						continue;
					}
					// Configuration-level implicit imports affect semantic visibility,
					// but are not declarations in the current source document.
					if (Import.bImplicit)
					{
						continue;
					}
					FDocumentSymbol Symbol;
					Symbol.Kind = EDocumentSymbolKind::Import;
					Symbol.Name = Import.Path;
					if (Import.bReExport)
					{
						Symbol.Detail = "export import";
					}
					ApplyLocation(Symbol, Import.Location);
					OutSymbols.push_back(std::move(Symbol));
				}
				for (const FUsingNamespaceDecl& Directive : Program.UsingNamespaces)
				{
					CheckAnalysisCancellation();
					if (!IsEntryModule(Directive.ModulePath, Entry))
					{
						continue;
					}
					FDocumentSymbol Symbol;
					Symbol.Kind = EDocumentSymbolKind::Import;
					Symbol.Name = Directive.NamespaceName;
					Symbol.Detail = "using namespace";
					ApplyLocation(Symbol, Directive.Location);
					OutSymbols.push_back(std::move(Symbol));
				}
			}

			// Namespaces are declarations in their own right. Emit every explicit
			// path and its implicit parents so `namespace A::B {}` exposes both A
			// and A::B to all document-symbol consumers. Keep import directives
			// first: that preserves the outline's historical source-action ordering
			// when `using namespace A;` and `namespace A {}` coexist.
			std::unordered_set<std::string> EmittedNamespaces;
			for (const FNamespaceDecl& NamespaceDecl : Program.Namespaces)
			{
				CheckAnalysisCancellation();
				if (!IsEntryModule(NamespaceDecl.ModulePath, Entry) || NamespaceDecl.Name.empty())
				{
					continue;
				}
				std::size_t SegmentStart = 0;
				for (;;)
				{
					const std::size_t SegmentEnd = NamespaceDecl.Name.find('.', SegmentStart);
					const std::string Qualified = NamespaceDecl.Name.substr(
					    0,
					    SegmentEnd == std::string::npos ? std::string::npos : SegmentEnd);
					if (!Qualified.empty() && EmittedNamespaces.insert(Qualified).second)
					{
						FDocumentSymbol Symbol;
						Symbol.Kind = EDocumentSymbolKind::Namespace;
						Symbol.Name = Qualified;
						Symbol.Detail = "namespace";
						ApplyDeclarationRange(Symbol, NamespaceDecl.DeclMeta.SourceRange, NamespaceDecl.Location.FileLabel);
						ApplyLocation(Symbol, NamespaceDecl.Location);
						OutSymbols.push_back(std::move(Symbol));
					}
					if (SegmentEnd == std::string::npos)
					{
						break;
					}
					SegmentStart = SegmentEnd + 1;
				}
			}

			for (const FTypeAliasDecl& Alias : Program.TypeAliases)
			{
				CheckAnalysisCancellation();
				// Block aliases already occur at their exact statement position.
				if (Alias.Scope.bLocal && Alias.Scope.OwnerTypeName.empty())
				{
					continue;
				}
				if (!ShouldCollectDecl(Alias.ModulePath, Entry, Alias.bExported, Options, Program))
				{
					continue;
				}
				FDocumentSymbol Symbol;
				Symbol.Kind = EDocumentSymbolKind::TypeAlias;
				Symbol.Name = Alias.Name;
				Symbol.TypeScope = Alias.Scope;
				Symbol.OutlineName = Alias.Scope.SimpleName;
				Symbol.Access = Alias.Scope.Access;
				Symbol.Detail = Alias.TypeName;
				ApplyDeclMetaDetail(Symbol, Alias.DeclMeta, Alias.Location.FileLabel);
				Symbol.bExported = Alias.bExported;
				ApplyLocation(Symbol, Alias.Location);
				OutSymbols.push_back(std::move(Symbol));
			}

			for (const FEnumDecl& EnumDecl : Program.Enums)
			{
				CheckAnalysisCancellation();
				if (!ShouldCollectDecl(EnumDecl.ModulePath, Entry, EnumDecl.bExported, Options, Program))
				{
					continue;
				}
				FDocumentSymbol Symbol;
				Symbol.Kind = EDocumentSymbolKind::Enum;
				Symbol.Name = EnumDecl.Name;
				Symbol.TypeScope = EnumDecl.Scope;
				Symbol.OutlineName = EnumDecl.Scope.SimpleName;
				Symbol.Access = EnumDecl.Scope.Access;
				Symbol.Detail = EnumDecl.bScoped ? "enum class" : "enum";
				Symbol.bScopedEnum = EnumDecl.bScoped;
				Symbol.bExported = EnumDecl.bExported;
				ApplyDeclMetaDetail(Symbol, EnumDecl.DeclMeta, EnumDecl.Location.FileLabel);
				ApplyLocation(Symbol, EnumDecl.Location);
				for (const FEnumMemberDecl& Member : EnumDecl.Members)
				{
					CheckAnalysisCancellation();
					FDocumentSymbol Child;
					Child.Kind = EDocumentSymbolKind::EnumMember;
					Child.Name = Member.Name;
					Child.Detail = Member.InitializerSource.empty() ? std::to_string(Member.Value) : Member.InitializerSource;
					ApplyDeclMetaDetail(Child, Member.DeclMeta, Member.Location.FileLabel);
					ApplyLocation(Child, Member.Location);
					Symbol.Children.push_back(std::move(Child));
				}
				OutSymbols.push_back(std::move(Symbol));
			}

			for (const FStructDecl& StructDecl : Program.Structs)
			{
				CheckAnalysisCancellation();
				if (!ShouldCollectDecl(StructDecl.ModulePath, Entry, StructDecl.bExported, Options, Program))
				{
					continue;
				}
				FDocumentSymbol Symbol;
				Symbol.Kind = EDocumentSymbolKind::Struct;
				Symbol.Name = StructDecl.Name + FormatDeclaredTemplateParams(StructDecl);
				Symbol.TypeScope = StructDecl.Scope;
				Symbol.Access = StructDecl.Scope.Access;
				Symbol.OutlineName = StructDecl.Scope.SimpleName + FormatDeclaredTemplateParams(StructDecl, true);
				Symbol.Detail = JoinDetails(
				    StructDecl.bIsStatic ? "static struct" : (StructDecl.bFinal ? "final" : ""),
				    JoinDetails(
				        FormatInheritance(
				            StructDecl.BaseTypeName,
				            {},
				            StructDecl.InterfaceTypeNames),
				        FormatBounds(StructDecl.Bounds)));
				ApplyDeclMetaDetail(Symbol, StructDecl.DeclMeta, StructDecl.Location.FileLabel);
				Symbol.bExported = StructDecl.bExported;
				ApplyLocation(Symbol, StructDecl.Location);
				for (const FFieldDecl& Field : StructDecl.Fields)
				{
					CheckAnalysisCancellation();
					Symbol.Children.push_back(MakeFieldSymbol(Field));
				}
				for (const FConstructorDecl& Constructor : StructDecl.Constructors)
				{
					CheckAnalysisCancellation();
					Symbol.Children.push_back(MakeConstructorSymbol(Constructor, StructDecl.Name));
				}
				if (StructDecl.Destructor)
				{
					Symbol.Children.push_back(
					    MakeDestructorSymbol(*StructDecl.Destructor, StructDecl.Name));
				}
				for (const FMethodDecl& Method : StructDecl.Methods)
				{
					CheckAnalysisCancellation();
					Symbol.Children.push_back(MakeMethodSymbol(Program, Method));
				}
				OutSymbols.push_back(std::move(Symbol));
			}

			for (const FClassDecl& ClassDecl : Program.Classes)
			{
				CheckAnalysisCancellation();
				if (!ShouldCollectDecl(ClassDecl.ModulePath, Entry, ClassDecl.bExported, Options, Program))
				{
					continue;
				}
				FDocumentSymbol Symbol;
				Symbol.Kind = EDocumentSymbolKind::Class;
				Symbol.Name = ClassDecl.Name + FormatDeclaredTemplateParams(ClassDecl);
				Symbol.TypeScope = ClassDecl.Scope;
				Symbol.Access = ClassDecl.Scope.Access;
				Symbol.OutlineName = ClassDecl.Scope.SimpleName + FormatDeclaredTemplateParams(ClassDecl, true);
				Symbol.Detail = JoinDetails(
				    ClassDecl.bIsStatic ? "static class" : (ClassDecl.bFinal ? "final" : ""),
				    JoinDetails(
				        FormatInheritance({}, ClassDecl.BaseTypeNames, ClassDecl.InterfaceTypeNames),
				        FormatBounds(ClassDecl.Bounds)));
				ApplyDeclMetaDetail(Symbol, ClassDecl.DeclMeta, ClassDecl.Location.FileLabel);
				Symbol.bExported = ClassDecl.bExported;
				ApplyLocation(Symbol, ClassDecl.Location);
				for (const FFieldDecl& Field : ClassDecl.Fields)
				{
					CheckAnalysisCancellation();
					Symbol.Children.push_back(MakeFieldSymbol(Field));
				}
				for (const FConstructorDecl& Constructor : ClassDecl.Constructors)
				{
					CheckAnalysisCancellation();
					Symbol.Children.push_back(MakeConstructorSymbol(Constructor, ClassDecl.Name));
				}
				if (ClassDecl.Destructor)
				{
					Symbol.Children.push_back(
					    MakeDestructorSymbol(*ClassDecl.Destructor, ClassDecl.Name));
				}
				for (const FMethodDecl& Method : ClassDecl.Methods)
				{
					CheckAnalysisCancellation();
					Symbol.Children.push_back(MakeMethodSymbol(Program, Method));
				}
				OutSymbols.push_back(std::move(Symbol));
			}

			for (const FInterfaceDecl& InterfaceDecl : Program.Interfaces)
			{
				CheckAnalysisCancellation();
				if (!ShouldCollectDecl(InterfaceDecl.ModulePath, Entry, InterfaceDecl.bExported, Options, Program))
				{
					continue;
				}
				FDocumentSymbol Symbol;
				Symbol.Kind = EDocumentSymbolKind::Interface;
				Symbol.Name = InterfaceDecl.Name + FormatDeclaredTemplateParams(InterfaceDecl);
				Symbol.TypeScope = InterfaceDecl.Scope;
				Symbol.Access = InterfaceDecl.Scope.Access;
				Symbol.OutlineName = InterfaceDecl.Scope.SimpleName + FormatDeclaredTemplateParams(InterfaceDecl, true);
				Symbol.Detail = JoinDetails(
				    InterfaceDecl.bFinal ? "final" : "",
				    JoinDetails(
				        FormatInheritance({}, InterfaceDecl.BaseInterfaceNames, {}),
				        FormatBounds(InterfaceDecl.Bounds)));
				ApplyDeclMetaDetail(Symbol, InterfaceDecl.DeclMeta, InterfaceDecl.Location.FileLabel);
				Symbol.bExported = InterfaceDecl.bExported;
				ApplyLocation(Symbol, InterfaceDecl.Location);
				for (const FFieldDecl& Field : InterfaceDecl.Fields)
				{
					CheckAnalysisCancellation();
					Symbol.Children.push_back(MakeFieldSymbol(Field));
				}
				for (const FMethodDecl& Method : InterfaceDecl.Methods)
				{
					CheckAnalysisCancellation();
					Symbol.Children.push_back(MakeMethodSymbol(Program, Method, true));
				}
				OutSymbols.push_back(std::move(Symbol));
			}

			for (const FFunctionDecl& Function : Program.Functions)
			{
				CheckAnalysisCancellation();
				if (!ShouldCollectDecl(Function.ModulePath, Entry, Function.bExported, Options, Program))
				{
					continue;
				}
				FDocumentSymbol Symbol;
				Symbol.Kind = EDocumentSymbolKind::Function;
				Symbol.CallableOwner = Function.Body ? Function.Body->CallableOwner : std::string{};
				Symbol.Name = Function.Name + FormatDeclaredTemplateParams(Function);
				Symbol.Detail = JoinDetails(
				    (Function.bConstexpr ? "constexpr " : "") + FormatReturnType(Function.ReturnType) + FormatParams(Function.Parameters),
				    FormatBounds(Function.Bounds));
				if (Function.bDeleted)
				{
					Symbol.Detail = JoinDetails(Symbol.Detail, "delete");
				}
				ApplyDeclMetaDetail(Symbol, Function.DeclMeta, Function.Location.FileLabel);
				Symbol.bExported = Function.bExported;
				ApplyLocation(Symbol, Function.Location);
				Symbol.SourceImplementation = SourceImplementationLocation(Program, Function.Body.get());
				AppendLabelsFromBlock(Function.Body.get(), Symbol.Children);
				OutSymbols.push_back(std::move(Symbol));
			}

			for (const std::unique_ptr<FStmt>& Statement : Program.TopLevelStatements)
			{
				CheckAnalysisCancellation();
				if (!Statement || Statement->Kind != EStmtKind::VarDecl)
				{
					continue;
				}
				const auto& VarDecl = static_cast<const FVarDeclStmt&>(*Statement);
				if (!ShouldCollectDecl(Statement->ModulePath, Entry, VarDecl.bExported, Options, Program))
				{
					continue;
				}
				FDocumentSymbol Symbol;
				Symbol.Kind = VarDecl.bIsConst
				    ? EDocumentSymbolKind::Constant
				    : EDocumentSymbolKind::Variable;
				Symbol.Name = VarDecl.Name;
				Symbol.Detail = VarDecl.bConstexpr ? "constexpr " : VarDecl.bIsConst ? "const "
				                                                                     : "";
				Symbol.Detail += VarDecl.TypeName;
				if (VarDecl.bIsRef)
				{
					Symbol.Detail += '&';
				}
				Symbol.bExported = VarDecl.bExported;
				ApplyDeclMetaDetail(Symbol, VarDecl.DeclMeta, VarDecl.Location.FileLabel);
				ApplyLocation(Symbol, VarDecl.Location);
				OutSymbols.push_back(std::move(Symbol));
			}

			if (Options.bIncludeSoftBuiltins)
			{
				AppendSoftBuiltinSymbols(Program, OutSymbols);
			}
			if (Options.bIncludeHostBindings && Options.Bindings)
			{
				AppendHostBindingSymbols(*Options.Bindings, Program, OutSymbols);
				// Annotation vocabulary is host-declared (bindings.json / RegisterAnnotation).
				AppendHostAnnotationSymbols(Options.Bindings, OutSymbols);
			}
		}

		void ApplyHostTypeNames(
		    VeroScriptModuleLoader& Loader,
		    const VeroScript::IHostBindingCatalog* Bindings)
		{
			VeroScript::Tooling::ApplyHostTypeNames(Loader, Bindings);
		}

		void MarkSoftSymbol(FDocumentSymbol& Symbol)
		{
			Symbol.bSoftBuiltin = true;
			Symbol.StartLine = 0;
			Symbol.StartColumn = 0;
			Symbol.EndLine = 0;
			Symbol.EndColumn = 0;
			for (FDocumentSymbol& Child : Symbol.Children)
			{
				MarkSoftSymbol(Child);
			}
		}

		bool HasNamedSymbol(
		    const std::vector<FDocumentSymbol>& Symbols,
		    const EDocumentSymbolKind Kind,
		    const std::string_view Name)
		{
			for (const FDocumentSymbol& Symbol : Symbols)
			{
				if (Symbol.Kind == Kind && Symbol.Name == Name)
				{
					return true;
				}
			}
			return false;
		}

		void AppendHostBindingSymbols(
		    const VeroScript::IHostBindingCatalog& Bindings,
		    const FProgram& Program,
		    std::vector<FDocumentSymbol>& OutSymbols)
		{
			FBindingVisibility Visibility(Program, Bindings);
			for (const VeroScript::FHostFunctionDesc& Function : Bindings.GetFunctions())
			{
				if (!Visibility.Allows(Function.Origin))
				{
					continue;
				}

				FDocumentSymbol Symbol;
				Symbol.Kind = EDocumentSymbolKind::Function;
				Symbol.Name = Function.Name;
				Symbol.Detail = VeroScript::FormatHostAnnotations(Function.Annotations);
				if (!Symbol.Detail.empty())
				{
					Symbol.Detail.push_back(' ');
				}
				Symbol.Detail += VeroScript::FormatHostFunctionSignature(Function);
				const bool bDuplicateSignature = std::any_of(
				    OutSymbols.begin(), OutSymbols.end(), [&Symbol](const FDocumentSymbol& Existing)
				{
					return Existing.Kind == EDocumentSymbolKind::Function && Existing.Name == Symbol.Name && Existing.Detail == Symbol.Detail;
				});
				if (bDuplicateSignature)
				{
					continue;
				}
				Symbol.Documentation = VeroScript::FormatHostDocumentation(
				    Function.Documentation,
				    Function.ParamNames,
				    Function.ParamDocumentation,
				    Function.ReturnDocumentation,
				    Function.Annotations,
				    Function.ParamAnnotations);
				Symbol.bExported = true;
				MarkSoftSymbol(Symbol);
				OutSymbols.push_back(std::move(Symbol));
			}

			for (const VeroScript::FHostTypeDesc& Type : Bindings.GetTypes())
			{
				if (!Visibility.Allows(Type.Origin, Type.Name))
				{
					continue;
				}

				EDocumentSymbolKind Kind = EDocumentSymbolKind::Class;
				switch (Type.Kind)
				{
				case VeroScript::EHostTypeKind::Enum:
					{
						Kind = EDocumentSymbolKind::Enum;
						break;
					}
				case VeroScript::EHostTypeKind::Struct:
					{
						Kind = EDocumentSymbolKind::Struct;
						break;
					}
				default:
					{
						Kind = EDocumentSymbolKind::Class;
						break;
					}
				}
				if (HasNamedSymbol(OutSymbols, Kind, Type.Name))
				{
					continue;
				}

				FDocumentSymbol Symbol;
				Symbol.Kind = Kind;
				Symbol.Name = Type.Name;
				Symbol.bScopedEnum =
				    Type.Kind == VeroScript::EHostTypeKind::Enum && Type.bScopedEnum;
				Symbol.Documentation = VeroScript::FormatHostDocumentation(
				    Type.Documentation, {}, {}, {}, Type.Annotations, {});
				Symbol.Detail = VeroScript::FormatHostAnnotations(Type.Annotations);
				if (!Symbol.Detail.empty())
				{
					Symbol.Detail.push_back(' ');
				}
				Symbol.Detail += std::string("host ") + VeroScript::HostTypeKindName(Type.Kind);
				if (Symbol.bScopedEnum)
				{
					Symbol.Detail += " scoped";
				}
				if (Type.bInheritable)
				{
					Symbol.Detail += " inheritable";
				}
				Symbol.bExported = true;
				for (const VeroScript::FHostMemberDesc& Member : Type.Members)
				{
					if (Member.Kind == VeroScript::EHostMemberKind::PropertySetter)
					{
						// Getter/Field already represents the property name; skip setter twin.
						bool bHasPropertyName = false;
						for (const FDocumentSymbol& Existing : Symbol.Children)
						{
							if (Existing.Name == Member.Name && Existing.Kind == EDocumentSymbolKind::Field)
							{
								bHasPropertyName = true;
								break;
							}
						}
						if (bHasPropertyName)
						{
							continue;
						}
					}

					FDocumentSymbol Child;
					Child.bIsStatic = Member.Kind == EHostMemberKind::StaticMethod || Member.Kind == EHostMemberKind::StaticConstant;
					switch (Member.Kind)
					{
					case VeroScript::EHostMemberKind::Constructor:
						{
							Child.Kind = EDocumentSymbolKind::Constructor;
							Child.Name = Type.Name;
							break;
						}
					case VeroScript::EHostMemberKind::EnumMember:
						{
							Child.Kind = EDocumentSymbolKind::EnumMember;
							Child.Name = Member.Name;
							break;
						}
					case VeroScript::EHostMemberKind::StaticConstant:
						{
							Child.Kind = EDocumentSymbolKind::Constant;
							Child.Name = Member.Name;
							break;
						}
					case VeroScript::EHostMemberKind::Field:
					case VeroScript::EHostMemberKind::PropertyGetter:
					case VeroScript::EHostMemberKind::PropertySetter:
						{
							Child.Kind = EDocumentSymbolKind::Field;
							Child.Name = Member.Name;
							break;
						}
					case VeroScript::EHostMemberKind::StaticMethod:
					case VeroScript::EHostMemberKind::Method:
					default:
						{
							Child.Kind = EDocumentSymbolKind::Method;
							Child.Name = Member.Name.empty() ? Type.Name : Member.Name;
							break;
						}
					}
					Child.bReadOnly = Member.Kind == EHostMemberKind::PropertyGetter &&
					    std::none_of(Type.Members.begin(), Type.Members.end(),
					        [&](const auto& Other)
					{
						return Other.Kind == EHostMemberKind::PropertySetter &&
						    Other.Name == Member.Name;
					});
					Child.Detail = VeroScript::FormatHostAnnotations(Member.Annotations);
					if (!Child.Detail.empty())
					{
						Child.Detail.push_back(' ');
					}
					Child.Detail += FormatHostMemberDetail(Member, Type.Name);
					Child.Documentation = VeroScript::FormatHostDocumentation(
					    Member.Documentation,
					    Member.ParamNames,
					    Member.ParamDocumentation,
					    Member.ReturnDocumentation,
					    Member.Annotations,
					    Member.ParamAnnotations);
					Child.bExported = true;
					Symbol.Children.push_back(std::move(Child));
				}
				MarkSoftSymbol(Symbol);
				OutSymbols.push_back(std::move(Symbol));
			}

			for (const VeroScript::FGenericHostTypeTemplateDesc& Template :
			    Bindings.GetGenericTypeTemplates())
			{
				if (!Visibility.Allows(Template.Origin, Template.Name))
				{
					continue;
				}

				const std::string DisplayName = FormatGenericHostTemplateName(Template.Name, Template.Arity);
				if (HasNamedSymbol(
				        OutSymbols, EDocumentSymbolKind::TypeAlias, DisplayName))
				{
					continue;
				}
				FDocumentSymbol Symbol;
				Symbol.Kind = EDocumentSymbolKind::TypeAlias;
				Symbol.Name = DisplayName;
				Symbol.Detail = "host generic type template (arity " + std::to_string(Template.Arity) + ')';
				Symbol.Documentation = Template.Documentation;
				Symbol.bExported = true;
				MarkSoftSymbol(Symbol);
				OutSymbols.push_back(std::move(Symbol));
			}

			for (const VeroScript::FHostTypeAliasDesc& Alias : Bindings.GetTypeAliases())
			{
				if (!Visibility.Allows(Alias.Origin, Alias.Name))
				{
					continue;
				}

				if (HasNamedSymbol(OutSymbols, EDocumentSymbolKind::TypeAlias, Alias.Name))
				{
					continue;
				}
				FDocumentSymbol Symbol;
				Symbol.Kind = EDocumentSymbolKind::TypeAlias;
				Symbol.Name = Alias.Name;
				Symbol.Detail = Alias.TargetTypeName;
				Symbol.Documentation = Alias.Documentation;
				Symbol.bExported = true;
				MarkSoftSymbol(Symbol);
				OutSymbols.push_back(std::move(Symbol));
			}
		}

		FDocumentSymbolResult Fail(FScriptError Error)
		{
			FDocumentSymbolResult Result;
			Result.Error = std::move(Error);
			Result.Diagnostics.push_back(Result.Error);
			return Result;
		}

		FDocumentSymbolResult FailFromLoader(const VeroScriptModuleLoader& Loader)
		{
			FDocumentSymbolResult Result;
			Result.Error = Loader.GetError();
			Result.Diagnostics = Loader.GetDiagnostics();
			if (Result.Diagnostics.empty())
			{
				Result.Diagnostics.push_back(Result.Error);
			}
			return Result;
		}
	}

	namespace
	{
		void OrganizeScopedOutline(std::vector<FDocumentSymbol>& Symbols)
		{
			std::vector<FDocumentSymbol> Pending, Roots;
			for (auto& Symbol : Symbols)
			{
				(Symbol.TypeScope.bLocal || !Symbol.TypeScope.OwnerTypeName.empty() ? Pending : Roots)
				    .push_back(std::move(Symbol));
			}
			const auto FindOwner = [](auto&& Self, std::vector<FDocumentSymbol>& Items,
			                           const FTypeDeclarationScope& Scope) -> FDocumentSymbol*
			{
				for (auto& Item : Items)
				{
					if (!Scope.OwnerTypeName.empty()
					        ? Item.TypeScope.Id == Scope.ScopeId && Item.TypeScope.ModulePath == Scope.ModulePath
					        : !Scope.OwnerFunctionName.empty() && Item.CallableOwner == Scope.OwnerFunctionName &&
					            Item.FileLabel == Scope.ModulePath)
					{
						return &Item;
					}
					if (auto* Found = Self(Self, Item.Children, Scope))
					{
						return Found;
					}
				}
				return nullptr;
			};
			for (std::size_t Depth = 0; Depth < 64 && !Pending.empty(); ++Depth)
			{
				bool Progress = false;
				for (auto I = Pending.begin(); I != Pending.end();)
				{
					if (auto* Owner = FindOwner(FindOwner, Roots, I->TypeScope))
					{
						Owner->Children.push_back(std::move(*I));
						I = Pending.erase(I);
						Progress = true;
					}
					else
					{
						++I;
					}
				}
				if (!Progress)
				{
					break;
				}
			}
			for (auto& Item : Pending)
			{
				Roots.push_back(std::move(Item));
			}
			const auto Display = [](auto&& Self, std::vector<FDocumentSymbol>& Items) -> void
			{
				for (auto& Item : Items)
				{
					if ((Item.TypeScope.bLocal || !Item.TypeScope.OwnerTypeName.empty()) &&
					    !Item.OutlineName.empty())
					{
						Item.Name = Item.OutlineName;
					}
					Self(Self, Item.Children);
				}
			};
			Display(Display, Roots);
			Symbols = std::move(Roots);
		}
	} // namespace

	FDocumentSymbolResult VeroScriptDocumentSymbols::CollectFromProgram(
	    const FProgram& Program, const FDocumentSymbolOptions& RequestedOptions)
	{
		auto Options = RequestedOptions;
		std::string EnvironmentError;
		if (!ResolveToolingModuleEnvironment(Options, EnvironmentError))
		{
			return ToolingModuleEnvironmentFailure<FDocumentSymbolResult>(EnvironmentError);
		}

		const auto StandardBindings = StandardLibrary::MergeBindingCatalog(Options.Bindings, nullptr);
		const auto QueryBindings = SourceMetadataQueryBindings(StandardBindings.get(),
		    Program.SourceMetadata ? Program.SourceMetadata->Definitions : Options.CompileProcessing.AnnotationDefinitions);
		Options.Bindings = QueryBindings.get();
		FDocumentSymbolResult Result;
		Result.bSuccess = true;
		CollectSymbolsInto(Program, Options, Result.Symbols);
		if (!Options.bIncludeImportedExports && !Options.bIncludeSoftBuiltins && !Options.bIncludeHostBindings)
		{
			OrganizeScopedOutline(Result.Symbols);
		}
		return Result;
	}

	FDocumentSymbolResult VeroScriptDocumentSymbols::CollectFromSource(
	    const std::string_view Source,
	    const std::string_view FileLabel,
	    const FDocumentSymbolOptions& RequestedOptions)
	{
		auto Options = RequestedOptions;
		std::string EnvironmentError;
		if (!ResolveToolingModuleEnvironment(Options, EnvironmentError))
		{
			return ToolingModuleEnvironmentFailure<FDocumentSymbolResult>(EnvironmentError);
		}

		VeroScriptModuleLoader Loader("", Options.ModuleEnvironment);
		ApplyModuleLoadingOptions(Loader, Options);
		ApplyDefines(Loader, Options.Defines);
		ApplyPredefinedMacros(Loader, Options.PredefinedMacros);
		ApplyHostTypeNames(Loader, Options.Bindings);
		for (const FSourceOverlay& Overlay : Options.SourceOverlays)
		{
			Loader.SetSourceOverlay(Overlay.Path, Overlay.Source);
		}

		std::unique_ptr<FProgram> Program = Loader.LoadSourceModule(Source, FileLabel);
		if (Program)
		{
			FScriptError Error;
			if (!Detail::PrepareQueryProgram(Program, Options, &Error)) return Fail(std::move(Error));
		}
		if (Loader.HasError() || !Program)
		{
			if (Loader.HasError())
			{
				return FailFromLoader(Loader);
			}
			FScriptError Error;
			FillDiagnostic(
			    Error,
			    "diag.failed_to_load_source_module",
			    FSourceLocation{std::string(FileLabel), 1, 1});
			return Fail(std::move(Error));
		}

		auto Result = CollectFromProgram(*Program, Options);
		if (Options.bIncludeImportedExports)
		{
			RemapImportedDeclarationSites(
			    *Program, PathUtil::IdentityKey(std::string(FileLabel)), Result.Symbols);
		}

		if (const auto Map = ProgramPreprocessorLineMap(*Program, FileLabel))
		{
			Result.PreprocessLineMap = *Map;
		}

		return Result;
	}

	FDocumentSymbolResult VeroScriptDocumentSymbols::CollectFromFile(
	    const std::string_view FilePath,
	    const FDocumentSymbolOptions& RequestedOptions)
	{
		auto Options = RequestedOptions;
		std::string EnvironmentError;
		if (!ResolveToolingModuleEnvironment(Options, EnvironmentError))
		{
			return ToolingModuleEnvironmentFailure<FDocumentSymbolResult>(EnvironmentError);
		}

		VeroScriptModuleLoader Loader(FilePath, Options.ModuleEnvironment);
		ApplyModuleLoadingOptions(Loader, Options);
		ApplyDefines(Loader, Options.Defines);
		ApplyPredefinedMacros(Loader, Options.PredefinedMacros);
		ApplyHostTypeNames(Loader, Options.Bindings);
		for (const FSourceOverlay& Overlay : Options.SourceOverlays)
		{
			Loader.SetSourceOverlay(Overlay.Path, Overlay.Source);
		}

		std::unique_ptr<FProgram> Program = Loader.LoadMainModule();
		if (Program)
		{
			FScriptError Error;
			if (!Detail::PrepareQueryProgram(Program, Options, &Error)) return Fail(std::move(Error));
		}
		if (Loader.HasError() || !Program)
		{
			if (Loader.HasError())
			{
				return FailFromLoader(Loader);
			}
			FScriptError Error;
			FillDiagnostic(
			    Error,
			    "diag.failed_to_load_module",
			    FSourceLocation{std::string(FilePath), 1, 1});
			return Fail(std::move(Error));
		}

		auto Result = CollectFromProgram(*Program, Options);
		if (Options.bIncludeImportedExports)
		{
			RemapImportedDeclarationSites(
			    *Program, PathUtil::IdentityKey(std::string(FilePath)), Result.Symbols);
		}

		if (const auto Map = ProgramPreprocessorLineMap(*Program, FilePath))
		{
			Result.PreprocessLineMap = *Map;
		}

		return Result;
	}
}
