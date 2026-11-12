// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPreparedQuery.h"

#include "VeroScriptReferences.h"

#include "VeroScriptAst.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptLexer.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptPath.h"
#include "VeroScriptOperatorLex.h"
#include "VeroScriptOverlayUtil.h"
#include "VeroScriptPreprocessorQuery.h"
#include "VeroScriptDocumentSymbols.h"
#include "VeroScriptSoftBuiltins.h"
#include "VeroScriptTypeHierarchy.h"
#include "VeroScriptSourceQuerySession.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace VeroScript::Tooling::Detail
{
	namespace
	{
		void ApplyHostBindingsToLoader(
			VeroScriptModuleLoader& Loader,
			const VeroScript::IHostBindingCatalog* Bindings)
		{
			VeroScript::Tooling::ApplyHostTypeNames(Loader, Bindings);
		}

		void ApplyHostBindingsToSymbolOptions(
			FDocumentSymbolOptions& SymbolOptions,
			const VeroScript::IHostBindingCatalog* Bindings)
		{
			SymbolOptions.Bindings = Bindings;
			SymbolOptions.bIncludeHostBindings = Bindings != nullptr;
		}

		std::string NormalizePathKey(std::string Path)
		{
			return VeroScript::PathUtil::IdentityKey(std::move(Path));
		}

		FReferencesResult Fail(FScriptError Error)
		{
			FReferencesResult Result;
			Result.Error = std::move(Error);
			Result.Diagnostics.push_back(Result.Error);
			return Result;
		}

		bool SameDocumentPath(const std::string& Left, const std::string& Right)
		{
			return Left == Right || NormalizePathKey(Left) == NormalizePathKey(Right);
		}

		bool MatchOpeningToken(
			const std::vector<FToken>& Tokens,
			const std::size_t CloseIndex,
			const char* const OpenLexeme,
			const char* const CloseLexeme,
			std::size_t& OutOpenIndex)
		{
			int Depth = 0;
			for (std::size_t Index = CloseIndex + 1; Index-- > 0;)
			{
				const FToken& Token = Tokens[Index];
				if (Token.Kind != ETokenKind::Symbol)
				{
					continue;
				}
				if (Token.Lexeme == CloseLexeme)
				{
					++Depth;
				}
				else if (Token.Lexeme == OpenLexeme)
				{
					if (Depth == 0)
					{
						OutOpenIndex = Index;
						return true;
					}
					--Depth;
					if (Depth == 0)
					{
						OutOpenIndex = Index;
						return true;
					}
				}
			}
			return false;
		}

		bool IsReceiverToken(const FToken& Token)
		{
			return Token.Kind == ETokenKind::Identifier
				|| (Token.Kind == ETokenKind::Keyword && Token.Lexeme == "this");
		}

		bool TryReceiverBeforeBracket(
			const std::vector<FToken>& Tokens,
			const std::size_t BracketIndex,
			std::string& OutReceiver,
			FTextPosition& OutReceiverPosition)
		{
			if (BracketIndex == 0 || BracketIndex >= Tokens.size())
			{
				return false;
			}
			const FToken& Previous = Tokens[BracketIndex - 1];
			if (IsReceiverToken(Previous))
			{
				OutReceiver = Previous.Lexeme;
				OutReceiverPosition.Line = Previous.Location.Line;
				OutReceiverPosition.Column = Previous.Location.Column;
				return true;
			}
			if (Previous.Kind == ETokenKind::Symbol && Previous.Lexeme == ")")
			{
				std::size_t OpenIndex = 0;
				if (MatchOpeningToken(Tokens, BracketIndex - 1, "(", ")", OpenIndex)
					&& OpenIndex > 0
					&& IsReceiverToken(Tokens[OpenIndex - 1]))
				{
					const FToken& Receiver = Tokens[OpenIndex - 1];
					OutReceiver = Receiver.Lexeme;
					OutReceiverPosition.Line = Receiver.Location.Line;
					OutReceiverPosition.Column = Receiver.Location.Column;
					return true;
				}
			}
			return false;
		}

		bool TryReceiverBeforeCall(
			const std::vector<FToken>& Tokens,
			const std::size_t OpenParenIndex,
			std::string& OutReceiver,
			FTextPosition& OutReceiverPosition)
		{
			if (OpenParenIndex == 0
				|| OpenParenIndex >= Tokens.size()
				|| Tokens[OpenParenIndex].Kind != ETokenKind::Symbol
				|| Tokens[OpenParenIndex].Lexeme != "(")
			{
				return false;
			}
			const FToken& Previous = Tokens[OpenParenIndex - 1];
			if (IsReceiverToken(Previous))
			{
				OutReceiver = Previous.Lexeme;
				OutReceiverPosition.Line = Previous.Location.Line;
				OutReceiverPosition.Column = Previous.Location.Column;
				return true;
			}
			if (Previous.Kind == ETokenKind::Symbol && Previous.Lexeme == "]")
			{
				std::size_t OpenIndex = 0;
				if (MatchOpeningToken(Tokens, OpenParenIndex - 1, "[", "]", OpenIndex)
					&& OpenIndex > 0
					&& IsReceiverToken(Tokens[OpenIndex - 1]))
				{
					const FToken& Receiver = Tokens[OpenIndex - 1];
					OutReceiver = Receiver.Lexeme;
					OutReceiverPosition.Line = Receiver.Location.Line;
					OutReceiverPosition.Column = Receiver.Location.Column;
					return true;
				}
			}
			return false;
		}

		bool AppendLexicalHits(
			const std::string_view Source,
			const std::string_view FileLabel,
			const std::string_view Name,
			std::vector<FReferenceHit>& OutHits,
			FScriptError& OutError)
		{
			VeroScriptLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			if (Lexer.HasError())
			{
				FillFromEnglishMessage(
					OutError,
					Lexer.GetErrorMessage(),
					FSourceLocation{std::string(FileLabel), 1, 1});
				return false;
			}

			const std::string Label(FileLabel);
			// Resolve each spelling once, then deduplicate by token position. A
			// pairwise scan used to repeat filesystem identity checks for every hit.
			const auto PositionKey = [](const std::uint32_t Line, const std::uint32_t Column)
			{
				return (static_cast<std::uint64_t>(Line) << 32) | Column;
			};
			std::unordered_set<std::uint64_t> SeenPositions;
			std::unordered_map<std::string, bool> SameFiles;
			for (const FReferenceHit& Hit : OutHits)
			{
				const auto [Match, bInserted] = SameFiles.try_emplace(Hit.FileLabel, false);
				if (bInserted)
				{
					Match->second = SameDocumentPath(Hit.FileLabel, Label);
				}
				if (Match->second)
				{
					SeenPositions.insert(PositionKey(Hit.Line, Hit.StartColumn));
				}
			}
			const auto HasHitAt = [&](const std::uint32_t Line, const std::uint32_t Column)
			{
				return !SeenPositions.insert(PositionKey(Line, Column)).second;
			};
			const bool bDestructorQuery = Name.size() >= 2 && Name.front() == '~';
			const bool bOperatorQuery = IsOperatorMethodName(Name);

			if (bDestructorQuery)
			{
				for (std::size_t Index = 0; Index + 2 < Tokens.size(); ++Index)
				{
					const FToken& Tilde = Tokens[Index];
					if (Tilde.Kind != ETokenKind::Symbol || Tilde.Lexeme != "~")
					{
						continue;
					}
					const FToken& TypeToken = Tokens[Index + 1];
					if (TypeToken.Kind != ETokenKind::Identifier)
					{
						continue;
					}
					const FToken& Open = Tokens[Index + 2];
					if (Open.Kind != ETokenKind::Symbol || Open.Lexeme != "(")
					{
						continue;
					}
					std::string DeclName = "~";
					DeclName.append(TypeToken.Lexeme.data(), TypeToken.Lexeme.size());
					if (DeclName != Name)
					{
						continue;
					}
					const std::uint32_t StartColumn = Tilde.Location.Column;
					if (HasHitAt(Tilde.Location.Line, StartColumn))
					{
						continue;
					}
					FReferenceHit Hit;
					Hit.FileLabel = Label;
					Hit.Line = Tilde.Location.Line;
					Hit.StartColumn = StartColumn;
					Hit.EndColumn =
						TypeToken.Location.Column
							+ static_cast<std::uint32_t>(TypeToken.Lexeme.size());
					OutHits.push_back(std::move(Hit));
				}
				return true;
			}

			if (bOperatorQuery)
			{
				for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
				{
					std::string OpName;
					std::size_t EndExclusive = Index;
					if (!TryParseOperatorName(Tokens, Index, OpName, EndExclusive))
					{
						continue;
					}
					if (!OperatorNameLeafMatches(OpName, Name))
					{
						continue;
					}
					const FToken& StartTok = Tokens[Index];
					const FToken& EndTok = Tokens[EndExclusive - 1];
					const std::uint32_t StartColumn = StartTok.Location.Column;
					const std::uint32_t EndColumn =
						EndTok.Location.Column
							+ static_cast<std::uint32_t>(EndTok.Lexeme.size());
					if (HasHitAt(StartTok.Location.Line, StartColumn))
					{
						continue;
					}
					FReferenceHit Hit;
					Hit.FileLabel = Label;
					Hit.Line = StartTok.Location.Line;
					Hit.StartColumn = StartColumn;
					Hit.EndColumn = EndColumn;
					OutHits.push_back(std::move(Hit));
				}

				// Bare use-sites for binary/unary ops (`a + b`). `()` / `[]` are typed-filtered below.
				for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
				{
					const FToken& Token = Tokens[Index];
					if (Token.Kind != ETokenKind::Symbol)
					{
						continue;
					}
					if (Index > 0
						&& Tokens[Index - 1].Kind == ETokenKind::Keyword
						&& Tokens[Index - 1].Lexeme == "operator")
					{
						continue;
					}
					std::string UseName;
					if (!TryMapUseSymbolToOperatorName(Token.Lexeme, UseName)
						|| !OperatorUseMatchesQuery(UseName, Name))
					{
						continue;
					}
					// Skip generic Foo<T> angles when querying comparison operators.
					if (Token.Lexeme == "<")
					{
						std::size_t CloseIndex = 0;
						if (FindMatchingGenericAngleClose(Tokens, Index, CloseIndex))
						{
							continue;
						}
					}
					else if (Token.Lexeme == ">")
					{
						std::size_t OpenIndex = 0;
						if (FindMatchingGenericAngleOpen(Tokens, Index, OpenIndex))
						{
							continue;
						}
					}
					if (UseName == "operator()")
					{
						if (Token.Lexeme != "(")
						{
							continue;
						}
						std::string Receiver;
						FTextPosition ReceiverPosition;
						if (!TryReceiverBeforeCall(
								Tokens,
								Index,
								Receiver,
								ReceiverPosition))
						{
							continue;
						}
					}
					else if (UseName == "operator[]")
					{
						if (Token.Lexeme != "[")
						{
							continue;
						}
						std::string Receiver;
						FTextPosition ReceiverPosition;
						if (!TryReceiverBeforeBracket(
								Tokens,
								Index,
								Receiver,
								ReceiverPosition))
						{
							continue;
						}
					}
					if (HasHitAt(Token.Location.Line, Token.Location.Column))
					{
						continue;
					}
					FReferenceHit Hit;
					Hit.FileLabel = Label;
					Hit.Line = Token.Location.Line;
					Hit.StartColumn = Token.Location.Column;
					Hit.EndColumn =
						Token.Location.Column
							+ static_cast<std::uint32_t>(Token.Lexeme.size());
					OutHits.push_back(std::move(Hit));
				}
				return true;
			}

			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (Token.Kind != ETokenKind::Identifier || Token.Lexeme != Name)
				{
					continue;
				}
				if (HasHitAt(Token.Location.Line, Token.Location.Column))
				{
					continue;
				}
				FReferenceHit Hit;
				Hit.FileLabel = Label;
				Hit.Line = Token.Location.Line;
				Hit.StartColumn = Token.Location.Column;
				Hit.EndColumn =
					Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size());
				// Lexical write: next token is `=` / compound / ++/-- suffix,
				// or previous token is prefix ++/--.
				if (Index + 1 < Tokens.size()
					&& Tokens[Index + 1].Kind == ETokenKind::Symbol)
				{
					const std::string_view Sym = Tokens[Index + 1].Lexeme;
					if (Sym == "="
						|| Sym == "+="
						|| Sym == "-="
						|| Sym == "*="
						|| Sym == "/="
						|| Sym == "%="
						|| Sym == "&="
						|| Sym == "|="
						|| Sym == "^="
						|| Sym == "<<="
						|| Sym == ">>="
						|| Sym == "++"
						|| Sym == "--")
					{
						Hit.bIsWrite = true;
					}
				}
				if (!Hit.bIsWrite
					&& Index > 0
					&& Tokens[Index - 1].Kind == ETokenKind::Symbol)
				{
					const std::string_view Sym = Tokens[Index - 1].Lexeme;
					if (Sym == "++" || Sym == "--")
					{
						Hit.bIsWrite = true;
					}
				}
				OutHits.push_back(std::move(Hit));
			}
			return true;
		}

		void MarkDefinitions(
			std::vector<FReferenceHit>& References,
			const std::vector<FSymbolQueryHit>& Definitions)
		{
			for (const FSymbolQueryHit& Definition : Definitions)
			{
				FReferenceHit* Exact = nullptr;
				FReferenceHit* Nearest = nullptr;
				for (FReferenceHit& Hit : References)
				{
					if (Hit.Line != Definition.DefLine)
					{
						continue;
					}
					if (!Definition.FileLabel.empty()
						&& !SameDocumentPath(Definition.FileLabel, Hit.FileLabel))
					{
						continue;
					}
					if (Hit.StartColumn == Definition.DefColumn)
					{
						Exact = &Hit;
						break;
					}
					if (Hit.StartColumn < Definition.DefColumn)
					{
						continue;
					}
					if (!Nearest || Hit.StartColumn < Nearest->StartColumn)
					{
						Nearest = &Hit;
					}
				}
				FReferenceHit* Best = Exact ? Exact : Nearest;
				if (Best)
				{
					Best->bIsDefinition = true;
				}
			}
		}

		bool TryLoadSourceForPath(
			const std::string& Path,
			const FReferencesOptions& Options,
			std::string& OutSource);

		/**
		 * Drop occurrences in inactive `#if` / `#ifdef` bodies. Keep `#define Name`
		 * sites for the queried macro name (directive lines are not "active code").
		 */
		void FilterInactivePreprocessorHits(
			const std::string_view CaretSource,
			const std::string_view CaretFileLabel,
			const FReferencesOptions& Options,
			const std::string& TokenText,
			std::vector<FReferenceHit>& Hits)
		{
			std::unordered_map<std::string, VeroScript::FPreprocessLineMap> MapsByFile;
			const auto LoadMap = [&](const std::string& Path) -> const VeroScript::FPreprocessLineMap*
			{
				const std::string Key = NormalizePathKey(Path);
				const auto Existing = MapsByFile.find(Key);
				if (Existing != MapsByFile.end())
				{
					return &Existing->second;
				}
				std::string FileSource;
				if (SameDocumentPath(Path, std::string(CaretFileLabel)))
				{
					FileSource.assign(CaretSource.data(), CaretSource.size());
				}
				else if (!TryLoadSourceForPath(Path, Options, FileSource))
				{
					return nullptr;
				}
				VeroScript::FPreprocessLineMap Map;
				std::string Error;
				if (!BuildModulePreprocessorLineMap(FileSource, Path, Options, Map, Error))
				{
					return nullptr;
				}
				const auto Inserted = MapsByFile.emplace(Key, std::move(Map));
				return &Inserted.first->second;
			};

			std::vector<FReferenceHit> Filtered;
			Filtered.reserve(Hits.size());
			for (FReferenceHit& Hit : Hits)
			{
				const std::string HitFile = Hit.FileLabel.empty()
					? std::string(CaretFileLabel)
					: Hit.FileLabel;
				const VeroScript::FPreprocessLineMap* Map = LoadMap(HitFile);
				if (!Map)
				{
					Filtered.push_back(std::move(Hit));
					continue;
				}
				if (IsActiveCodeLine(*Map, Hit.Line))
				{
					Filtered.push_back(std::move(Hit));
					continue;
				}
				bool bMacroDef = false;
				for (const VeroScript::FPreprocessMacroDef& Macro : Map->ActiveMacros)
				{
					if (Macro.bActive && !Macro.bImported && Macro.Name == TokenText &&
						Hit.Line >= Macro.DirectiveStartLine && Hit.Line <= Macro.DirectiveEndLine)
					{
						bMacroDef = true;
						break;
					}
				}
				if (bMacroDef)
				{
					Filtered.push_back(std::move(Hit));
				}
			}
			Hits = std::move(Filtered);
		}

		bool TryLoadSourceForPath(
			const std::string& Path,
			const FReferencesOptions& Options,
			std::string& OutSource)
		{
			const std::string Key = NormalizePathKey(Path);
			for (auto I = Options.SourceOverlays.rbegin(); I != Options.SourceOverlays.rend(); ++I)
			{
				const auto& Overlay = *I;
				if (SameDocumentPath(Overlay.Path, Key))
				{
					OutSource = Overlay.Source;
					return true;
				}
			}
			std::ifstream Input(PathUtil::FromUtf8(Key), std::ios::binary);
			if (!Input)
			{
				return false;
			}
			std::ostringstream Buffer;
			Buffer << Input.rdbuf();
			OutSource = Buffer.str();
			return true;
		}

		bool SameDefinition(const FSymbolQueryHit& Left, const FSymbolQueryHit& Right)
		{
			if (StripGenericArgs(Left.Name) != StripGenericArgs(Right.Name))
			{
				return false;
			}
			if (Left.DefLine != Right.DefLine
				|| Left.DefColumn != Right.DefColumn
				|| Left.Kind != Right.Kind)
			{
				return false;
			}
			if (StripGenericArgs(Left.ContainerName) != StripGenericArgs(Right.ContainerName))
			{
				return false;
			}
			if (!Left.FileLabel.empty() && !Right.FileLabel.empty()
				&& !SameDocumentPath(Left.FileLabel, Right.FileLabel))
			{
				return false;
			}
			return true;
		}

		bool ResolvesToAnyTarget(
			const FSymbolQueryResult& Query,
			const std::vector<FSymbolQueryHit>& Targets)
		{
			if (!Query.bSuccess || !Query.bHasToken || Query.Definitions.empty())
			{
				return false;
			}
			for (const FSymbolQueryHit& Hit : Query.Definitions)
			{
				for (const FSymbolQueryHit& Target : Targets)
				{
					if (SameDefinition(Hit, Target))
					{
						return true;
					}
				}
			}
			return false;
		}

		bool IsTypeSymbolKind(const EDocumentSymbolKind Kind)
		{
			return Kind == EDocumentSymbolKind::Class
				|| Kind == EDocumentSymbolKind::Struct
				|| Kind == EDocumentSymbolKind::Interface;
		}

		/**
		 * Type-name queries treat the embedded identifier in `~Type(` as a use of the type.
		 * SymbolQuery reports TokenText=`~Type` there, so plain token equality would drop it.
		 */
		bool IsDestructorNameOfTypeTarget(
			const FSymbolQueryResult& Query,
			const std::vector<FSymbolQueryHit>& Targets,
			const std::string_view TypeName)
		{
			if (!Query.bSuccess || !Query.bHasToken || Query.Definitions.empty()
				|| !IsDestructorTokenForTypeName(Query.TokenText, TypeName))
			{
				return false;
			}
			for (const FSymbolQueryHit& Target : Targets)
			{
				if (!IsTypeSymbolKind(Target.Kind)
					|| StripGenericArgs(Target.Name) != TypeName)
				{
					continue;
				}
				for (const FSymbolQueryHit& Hit : Query.Definitions)
				{
					if (Hit.Name != Query.TokenText)
					{
						continue;
					}
					const std::string Container = StripGenericArgs(Hit.ContainerName);
					if (!Container.empty() && Container != TypeName)
					{
						continue;
					}
					if (!Hit.FileLabel.empty() && !Target.FileLabel.empty()
						&& !SameDocumentPath(Hit.FileLabel, Target.FileLabel))
					{
						continue;
					}
					return true;
				}
			}
			return false;
		}

		// Type-name queries: constructor decls resolve as Constructor after type+ctor collapse.
		bool IsConstructorOfTypeTarget(
			const FSymbolQueryResult& Query,
			const std::vector<FSymbolQueryHit>& Targets,
			const std::string_view TypeName)
		{
			if (!Query.bSuccess || !Query.bHasToken || Query.Definitions.empty()
				|| Query.TokenText != TypeName)
			{
				return false;
			}
			for (const FSymbolQueryHit& Target : Targets)
			{
				if (!IsTypeSymbolKind(Target.Kind)
					|| StripGenericArgs(Target.Name) != TypeName)
				{
					continue;
				}
				for (const FSymbolQueryHit& Hit : Query.Definitions)
				{
					if (Hit.Kind != EDocumentSymbolKind::Constructor
						|| StripGenericArgs(Hit.Name) != TypeName)
					{
						continue;
					}
					const std::string Container = StripGenericArgs(Hit.ContainerName);
					if (!Container.empty() && Container != TypeName)
					{
						continue;
					}
					if (!Hit.FileLabel.empty() && !Target.FileLabel.empty()
						&& !SameDocumentPath(Hit.FileLabel, Target.FileLabel))
					{
						continue;
					}
					return true;
				}
			}
			return false;
		}

		/**
		 * Drop homonyms that share spelling but not the caret definition
		 * (same approach as Rename). Operator queries stay lexical.
		 */
		void FilterResolvedIdentifierHits(
			const std::string_view CaretSource,
			const std::string_view CaretFileLabel,
			const FReferencesOptions& Options,
			const std::string& TokenText,
			const std::vector<FSymbolQueryHit>& Definitions,
			std::vector<FReferenceHit>& Hits)
		{
			// Operators stay lexical (mirror symbols / use-site spellings).
			if (IsOperatorMethodName(TokenText))
			{
				return;
			}
			// Unresolved identifiers (e.g. illegal bare `enum class` members) must not
			// keep every same-spelling token as a reference. Hover/Rename already refuse.
			if (Definitions.empty())
			{
				Hits.clear();
				return;
			}

			FSymbolQueryOptions QueryOptions;
			QueryOptions.ImportPaths = Options.ImportPaths;
			QueryOptions.StdlibSearchPaths = Options.StdlibSearchPaths;
			QueryOptions.ImplicitImportPaths = Options.ImplicitImportPaths;
			QueryOptions.bAllowCircularImports = Options.bAllowCircularImports;
			QueryOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
			QueryOptions.Defines = Options.Defines;
			QueryOptions.PredefinedMacros = Options.PredefinedMacros;
			QueryOptions.SourceOverlays = Options.SourceOverlays;
			QueryOptions.Bindings = Options.Bindings;
			QueryOptions.AnnotationProcessing = Options.AnnotationProcessing;
			QueryOptions.CompileProcessing = Options.CompileProcessing;
			QueryOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
			QueryOptions.ModuleEnvironment = Options.ModuleEnvironment;
			std::vector<FReferenceHit> Filtered;
			Filtered.reserve(Hits.size());
			FSourceQuerySession Queries(QueryOptions);
			for (FReferenceHit& Hit : Hits)
			{
				const std::string HitFile = Hit.FileLabel.empty()
					? std::string(CaretFileLabel)
					: Hit.FileLabel;
				std::string HitSource;
				if (SameDocumentPath(HitFile, std::string(CaretFileLabel)))
				{
					HitSource.assign(CaretSource.data(), CaretSource.size());
				}
				else if (!TryLoadSourceForPath(HitFile, Options, HitSource))
				{
					continue;
				}

				const FSymbolQueryResult HitQuery = Queries.QueryAtSource(
					HitSource,
					FTextPosition{Hit.Line, Hit.StartColumn},
					HitFile);
				if (HitQuery.TokenText == TokenText)
				{
					if (!ResolvesToAnyTarget(HitQuery, Definitions)
						&& !IsConstructorOfTypeTarget(HitQuery, Definitions, TokenText))
					{
						continue;
					}
				}
				else if (!IsDestructorNameOfTypeTarget(HitQuery, Definitions, TokenText))
				{
					continue;
				}
				Filtered.push_back(std::move(Hit));
			}
			Hits = std::move(Filtered);
		}

		void EnsureImportedDefinitionsPhysical(
			const std::string_view CaretFileLabel,
			const FReferencesOptions& Options,
			std::vector<FSymbolQueryHit>& Definitions)
		{
			std::unordered_map<std::string, VeroScript::FPreprocessLineMap> MapsByFile;
			const auto LoadMap = [&](const std::string& Path) -> const VeroScript::FPreprocessLineMap*
			{
				const std::string Key = NormalizePathKey(Path);
				const auto Existing = MapsByFile.find(Key);
				if (Existing != MapsByFile.end())
				{
					return &Existing->second;
				}
				std::string FileSource;
				if (!TryLoadSourceForPath(Path, Options, FileSource))
				{
					return nullptr;
				}
				VeroScript::FPreprocessLineMap Map;
				std::string Error;
				if (!BuildModulePreprocessorLineMap(FileSource, Path, Options, Map, Error))
				{
					return nullptr;
				}
				const auto Inserted = MapsByFile.emplace(Key, std::move(Map));
				return &Inserted.first->second;
			};

			for (FSymbolQueryHit& Definition : Definitions)
			{
				if (Definition.bPhysicalLocation || Definition.Kind == EDocumentSymbolKind::Macro
					|| Definition.Detail.rfind("#define", 0) == 0)
				{
					continue;
				}
				const std::string DefFile = Definition.FileLabel.empty()
					? std::string(CaretFileLabel)
					: Definition.FileLabel;
				if (SameDocumentPath(DefFile, std::string(CaretFileLabel)))
				{
					continue;
				}
				const VeroScript::FPreprocessLineMap* Map = LoadMap(DefFile);
				if (!Map)
				{
					continue;
				}
				std::uint32_t PhysicalLine = 0;
				std::uint32_t PhysicalColumn = 0;
				if (TryRemapPreprocessedToPhysical(
						*Map,
						Definition.DefLine,
						Definition.DefColumn,
						PhysicalLine,
						PhysicalColumn))
				{
					Definition.DefLine = PhysicalLine;
					Definition.DefColumn = PhysicalColumn;
					Definition.bPhysicalLocation = true;
				}
			}
		}

		bool OperatorDeclarationMatches(
			const FSymbolQueryHit& Candidate,
			const FSymbolQueryHit& Definition)
		{
			if (StripGenericArgs(Candidate.Name) != StripGenericArgs(Definition.Name))
			{
				return false;
			}
			if (!Candidate.ContainerName.empty() && !Definition.ContainerName.empty()
				&& StripGenericArgs(Candidate.ContainerName)
				!= StripGenericArgs(Definition.ContainerName))
			{
				return false;
			}
			if (!Candidate.FileLabel.empty() && !Definition.FileLabel.empty()
				&& !SameDocumentPath(Candidate.FileLabel, Definition.FileLabel))
			{
				return false;
			}
			return true;
		}

		// Token lexemes and source labels borrow their storage. Keep it stable
		// when contexts move into the per-query map, including short strings.
		struct FReferenceFileContext
		{
			std::unique_ptr<std::string> Source;
			std::unique_ptr<std::string> FileLabel;
			std::unique_ptr<FProgram> Program;
			FDocumentSymbolResult Symbols;
			std::vector<FToken> Tokens;
			FPreprocessLineMap LineMap;
			bool bReady = false;
		};

		/**
		 * Keep typed `()` / `[]` use-sites only when the receiver resolves to a
		 * definition container that declares the queried operator.
		 */
		void FilterResolvedOperatorCallHits(
			const std::string_view CaretSource,
			const std::string_view CaretFileLabel,
			const FReferencesOptions& Options,
			const std::string& TokenText,
			const std::vector<FSymbolQueryHit>& Definitions,
			const FProgram* CaretProgram,
			const FDocumentSymbolResult* CaretSymbols,
			std::vector<FReferenceHit>& Hits)
		{
			if (TokenText != "operator()" && TokenText != "operator[]")
			{
				return;
			}
			if (Definitions.empty())
			{
				return;
			}


			std::unordered_map<std::string, FReferenceFileContext> ContextByFile;

			const auto LoadContext = [&](const std::string& Path,
				const std::string_view Source) -> FReferenceFileContext*
			{
				const std::string Key = NormalizePathKey(Path);
				const auto Existing = ContextByFile.find(Key);
				if (Existing != ContextByFile.end())
				{
					return &Existing->second;
				}
				FReferenceFileContext Context;
				Context.Source = std::make_unique<std::string>(Source);
				Context.FileLabel = std::make_unique<std::string>(Path);
				if (CaretProgram
					&& CaretSymbols
					&& SameDocumentPath(Path, std::string(CaretFileLabel)))
				{
					Context.Program.reset();
					Context.Symbols = *CaretSymbols;
				}
				else
				{
					VeroScriptModuleLoader Loader("", Options.ModuleEnvironment);
					ApplyModuleLoadingOptions(Loader, Options);
					ApplyHostBindingsToLoader(Loader, Options.Bindings);
					ApplyDefines(Loader, Options.Defines);
					ApplyPredefinedMacros(Loader, Options.PredefinedMacros);
					for (const FSourceOverlay& Overlay : Options.SourceOverlays)
					{
						Loader.SetSourceOverlay(Overlay.Path, Overlay.Source);
					}
					Context.Program = Loader.LoadSourceModule(Source, Path);
					if (Context.Program)
					{
						Detail::PrepareQueryProgram(Context.Program, Options);
					}

					if (Loader.HasError() || !Context.Program)
					{
						ContextByFile.emplace(Key, std::move(Context));
						return &ContextByFile.find(Key)->second;
					}
					FDocumentSymbolOptions SymbolOptions;
					SymbolOptions.ImportPaths = Options.ImportPaths;
					SymbolOptions.StdlibSearchPaths = Options.StdlibSearchPaths;
					SymbolOptions.ImplicitImportPaths = Options.ImplicitImportPaths;
					SymbolOptions.bAllowCircularImports = Options.bAllowCircularImports;
					SymbolOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
					SymbolOptions.Defines = Options.Defines;
					SymbolOptions.PredefinedMacros = Options.PredefinedMacros;
					SymbolOptions.SourceOverlays = Options.SourceOverlays;
					SymbolOptions.bIncludeImports = true;
					SymbolOptions.bIncludeImportedExports = true;
					SymbolOptions.bIncludeSoftBuiltins = true;
					ApplyHostBindingsToSymbolOptions(SymbolOptions, Options.Bindings);
					Context.Symbols =
						VeroScriptDocumentSymbols::CollectFromProgram(
							*Context.Program,
							SymbolOptions);
				}
				VeroScriptLexer Lexer(*Context.Source, *Context.FileLabel);
				Context.Tokens = Lexer.Tokenize();
				std::string PreprocessError;
				const bool bMapped = BuildModulePreprocessorLineMap(
					*Context.Source, *Context.FileLabel, Options, Context.LineMap, PreprocessError);
				Context.bReady = !Lexer.HasError()
					&& bMapped
					&& Context.Symbols.bSuccess
					&& (Context.Program || (CaretProgram && SameDocumentPath(Path, std::string(CaretFileLabel))));
				ContextByFile.emplace(Key, std::move(Context));
				return &ContextByFile.find(Key)->second;
			};

			std::vector<FReferenceHit> Filtered;
			Filtered.reserve(Hits.size());
			for (FReferenceHit& Hit : Hits)
			{
				if (Hit.bIsDefinition)
				{
					Filtered.push_back(std::move(Hit));
					continue;
				}

				const std::string HitFile = Hit.FileLabel.empty()
					? std::string(CaretFileLabel)
					: Hit.FileLabel;
				std::string_view HitSource = CaretSource;
				std::string LoadedSource;
				if (!SameDocumentPath(HitFile, std::string(CaretFileLabel)))
				{
					if (!TryLoadSourceForPath(HitFile, Options, LoadedSource))
					{
						continue;
					}
					HitSource = LoadedSource;
				}

				FReferenceFileContext* Context = LoadContext(HitFile, HitSource);
				if (!Context || !Context->bReady)
				{
					continue;
				}
				HitSource = *Context->Source;
				const FProgram& Program = Context->Program
					? *Context->Program
					: *CaretProgram;
				const std::vector<FToken>& Tokens = Context->Tokens;

				std::size_t TokenIndex = Tokens.size();
				for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
				{
					const FToken& Token = Tokens[Index];
					if (Token.Location.Line == Hit.Line
						&& Token.Location.Column == Hit.StartColumn)
					{
						TokenIndex = Index;
						break;
					}
				}
				if (TokenIndex >= Tokens.size())
				{
					continue;
				}

				std::string Receiver;
				FTextPosition ReceiverPosition;
				if (TokenText == "operator[]")
				{
					if (!TryReceiverBeforeBracket(
							Tokens,
							TokenIndex,
							Receiver,
							ReceiverPosition))
					{
						continue;
					}
				}
				else if (!TryReceiverBeforeCall(
						Tokens,
						TokenIndex,
						Receiver,
						ReceiverPosition))
				{
					continue;
				}

				if (TokenText == "operator()")
				{
					const auto ReceiverQuery = VeroScriptSymbolQuery::QueryInProgram(
						Program, Context->Symbols, HitSource, *Context->FileLabel,
						ReceiverPosition, Options.Bindings, Context->LineMap);
					const bool bConstructsType = std::any_of(ReceiverQuery.Definitions.begin(),
						ReceiverQuery.Definitions.end(), [](const FSymbolQueryHit& Definition)
						{
							return Definition.Kind == EDocumentSymbolKind::Class
								|| Definition.Kind == EDocumentSymbolKind::Struct
								|| Definition.Kind == EDocumentSymbolKind::Interface
								|| Definition.Kind == EDocumentSymbolKind::Constructor
								|| Definition.Kind == EDocumentSymbolKind::TypeAlias;
							});
					if (bConstructsType)
					{
						continue;
					}
				}

				const std::string ReceiverType =
					VeroScriptSymbolQuery::ResolveReceiverTypeAtPhysical(
						Program,
						Context->Symbols,
						HitSource,
						Options.Defines,
						Options.PredefinedMacros,
						ReceiverPosition,
						Receiver);
				if (ReceiverType.empty())
				{
					continue;
				}

				bool bMatches = false;
				for (const FSymbolQueryHit& Definition : Definitions)
				{
					if (StripGenericArgs(Definition.Name) != StripGenericArgs(TokenText))
					{
						continue;
					}
					if (!Definition.ContainerName.empty()
						&& StripGenericArgs(Definition.ContainerName)
						== StripGenericArgs(ReceiverType))
					{
						bMatches = true;
						break;
					}
				}

				std::vector<FSymbolQueryHit> OperatorMembers;
				if (!bMatches
					&& !CollectMemberHitsAlongHierarchy(
						Program,
						Context->Symbols.Symbols,
						ReceiverType,
						TokenText,
						OperatorMembers))
				{
					continue;
				}

				if (!bMatches)
				{
					for (const FSymbolQueryHit& Member : OperatorMembers)
					{
						for (const FSymbolQueryHit& Definition : Definitions)
						{
							if (OperatorDeclarationMatches(Member, Definition))
							{
								bMatches = true;
								break;
							}
						}
						if (bMatches)
						{
							break;
						}
					}
				}
				if (bMatches)
				{
					Filtered.push_back(std::move(Hit));
				}
			}
			Hits = std::move(Filtered);
		}

		/**
		 * multicast_delegate +=/-= sugar resolves to Add/Remove. Keep only compound-assign
		 * use-sites whose LHS is a multicast_delegate (drop unrelated `x += 1` hits).
		 */
		void FilterResolvedMulticastAssignHits(
			const std::string_view CaretSource,
			const std::string_view CaretFileLabel,
			const FReferencesOptions& Options,
			const std::string& TokenText,
			const std::vector<FSymbolQueryHit>& Definitions,
			const FProgram* CaretProgram,
			const FDocumentSymbolResult* CaretSymbols,
			std::vector<FReferenceHit>& Hits)
		{
			if (TokenText != "operator+=" && TokenText != "operator-=")
			{
				return;
			}
			bool bSugar = false;
			for (const FSymbolQueryHit& Definition : Definitions)
			{
				if ((TokenText == "operator+=" && Definition.Name == "add")
					|| (TokenText == "operator-=" && Definition.Name == "remove"))
				{
					bSugar = true;
					break;
				}
			}
			if (!bSugar || Definitions.empty())
			{
				return;
			}


			std::unordered_map<std::string, FReferenceFileContext> ContextByFile;

			const auto LoadContext = [&](const std::string& Path,
				const std::string_view Source) -> FReferenceFileContext*
			{
				const std::string Key = NormalizePathKey(Path);
				const auto Existing = ContextByFile.find(Key);
				if (Existing != ContextByFile.end())
				{
					return &Existing->second;
				}
				FReferenceFileContext Context;
				Context.Source = std::make_unique<std::string>(Source);
				Context.FileLabel = std::make_unique<std::string>(Path);
				if (CaretProgram
					&& CaretSymbols
					&& SameDocumentPath(Path, std::string(CaretFileLabel)))
				{
					Context.Symbols = *CaretSymbols;
				}
				else
				{
					VeroScriptModuleLoader Loader("", Options.ModuleEnvironment);
					ApplyModuleLoadingOptions(Loader, Options);
					ApplyHostBindingsToLoader(Loader, Options.Bindings);
					ApplyDefines(Loader, Options.Defines);
					ApplyPredefinedMacros(Loader, Options.PredefinedMacros);
					for (const FSourceOverlay& Overlay : Options.SourceOverlays)
					{
						Loader.SetSourceOverlay(Overlay.Path, Overlay.Source);
					}
					Context.Program = Loader.LoadSourceModule(Source, Path);
					if (Context.Program)
					{
						Detail::PrepareQueryProgram(Context.Program, Options);
					}

					if (Loader.HasError() || !Context.Program)
					{
						ContextByFile.emplace(Key, std::move(Context));
						return &ContextByFile.find(Key)->second;
					}
					FDocumentSymbolOptions SymbolOptions;
					SymbolOptions.ImportPaths = Options.ImportPaths;
					SymbolOptions.StdlibSearchPaths = Options.StdlibSearchPaths;
					SymbolOptions.ImplicitImportPaths = Options.ImplicitImportPaths;
					SymbolOptions.bAllowCircularImports = Options.bAllowCircularImports;
					SymbolOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
					SymbolOptions.Defines = Options.Defines;
					SymbolOptions.PredefinedMacros = Options.PredefinedMacros;
					SymbolOptions.SourceOverlays = Options.SourceOverlays;
					SymbolOptions.bIncludeImports = true;
					SymbolOptions.bIncludeImportedExports = true;
					SymbolOptions.bIncludeSoftBuiltins = true;
					ApplyHostBindingsToSymbolOptions(SymbolOptions, Options.Bindings);
					Context.Symbols = VeroScriptDocumentSymbols::CollectFromProgram(
						*Context.Program,
						SymbolOptions);
				}
				VeroScriptLexer Lexer(*Context.Source, *Context.FileLabel);
				Context.Tokens = Lexer.Tokenize();
				Context.bReady = !Lexer.HasError()
					&& Context.Symbols.bSuccess
					&& (Context.Program
						|| (CaretProgram && SameDocumentPath(Path, std::string(CaretFileLabel))));
				ContextByFile.emplace(Key, std::move(Context));
				return &ContextByFile.find(Key)->second;
			};

			std::vector<FReferenceHit> Filtered;
			Filtered.reserve(Hits.size());
			for (FReferenceHit& Hit : Hits)
			{
				if (Hit.bIsDefinition)
				{
					Filtered.push_back(std::move(Hit));
					continue;
				}

				const std::string HitFile = Hit.FileLabel.empty()
					? std::string(CaretFileLabel)
					: Hit.FileLabel;
				std::string_view HitSource = CaretSource;
				std::string LoadedSource;
				if (!SameDocumentPath(HitFile, std::string(CaretFileLabel)))
				{
					if (!TryLoadSourceForPath(HitFile, Options, LoadedSource))
					{
						continue;
					}
					HitSource = LoadedSource;
				}

				FReferenceFileContext* Context = LoadContext(HitFile, HitSource);
				if (!Context || !Context->bReady)
				{
					continue;
				}
				HitSource = *Context->Source;
				const FProgram& Program = Context->Program
					? *Context->Program
					: *CaretProgram;

				std::size_t TokenIndex = Context->Tokens.size();
				for (std::size_t Index = 0; Index < Context->Tokens.size(); ++Index)
				{
					const FToken& Token = Context->Tokens[Index];
					if (Token.Location.Line == Hit.Line
						&& Token.Location.Column == Hit.StartColumn
						&& Token.Kind == ETokenKind::Symbol
						&& (Token.Lexeme == "+=" || Token.Lexeme == "-="))
					{
						TokenIndex = Index;
						break;
					}
				}
				if (TokenIndex == 0 || TokenIndex >= Context->Tokens.size())
				{
					continue;
				}

				std::string ReceiverName;
				std::string OwnerName;
				const FToken& Left = Context->Tokens[TokenIndex - 1];
				const bool bLeftIdent = Left.Kind == ETokenKind::Identifier;
				const bool bLeftThis =
					Left.Kind == ETokenKind::Keyword && Left.Lexeme == "this";
				if (!bLeftIdent && !bLeftThis)
				{
					continue;
				}
				if (TokenIndex >= 3
					&& Context->Tokens[TokenIndex - 2].Kind == ETokenKind::Symbol
					&& IsMemberAccessSymbol(Context->Tokens[TokenIndex - 2].Lexeme)
					&& (Context->Tokens[TokenIndex - 3].Kind == ETokenKind::Identifier
						|| (Context->Tokens[TokenIndex - 3].Kind == ETokenKind::Keyword
							&& Context->Tokens[TokenIndex - 3].Lexeme == "this")))
				{
					OwnerName.assign(
						Context->Tokens[TokenIndex - 3].Lexeme.data(),
						Context->Tokens[TokenIndex - 3].Lexeme.size());
					ReceiverName.assign(Left.Lexeme.data(), Left.Lexeme.size());
				}
				else
				{
					ReceiverName.assign(Left.Lexeme.data(), Left.Lexeme.size());
				}

				std::string TypeName;
				const FTextPosition ReceiverPos{
					Left.Location.Line,
					Left.Location.Column};
				if (!OwnerName.empty())
				{
					const FTextPosition OwnerPos{
						Context->Tokens[TokenIndex - 3].Location.Line,
						Context->Tokens[TokenIndex - 3].Location.Column};
					const std::string OwnerType =
						VeroScriptSymbolQuery::ResolveReceiverTypeAtPhysical(
							Program,
							Context->Symbols,
							HitSource,
							Options.Defines,
							Options.PredefinedMacros,
							OwnerPos,
							OwnerName);
					std::unordered_map<std::string, FInheritedMemberCompletion> Members;
					AppendCompletionMembersAlongHierarchy(
						Program,
						Context->Symbols.Symbols,
						OwnerType,
						ReceiverName,
						Members);
					const auto Found = Members.find(ReceiverName);
					if (Found != Members.end())
					{
						TypeName = TypeNameFromDetail(Found->second.Detail);
					}
				}
				else
				{
					TypeName = VeroScriptSymbolQuery::ResolveReceiverTypeAtPhysical(
						Program,
						Context->Symbols,
						HitSource,
						Options.Defines,
						Options.PredefinedMacros,
						ReceiverPos,
						ReceiverName);
				}

				if (IsSoftMulticastDelegateTypeName(TypeName))
				{
					Filtered.push_back(std::move(Hit));
				}
			}
			Hits = std::move(Filtered);
		}

		/**
		 * Scan a document only if it is the caret file, a known definition file,
		 * or (with Program) can see a definition module via import visibility.
		 * Avoids reporting unrelated open buffers that merely share an identifier spelling.
		 */
		bool ShouldScanDocument(
			const std::string& Path,
			const std::string& CaretFileLabel,
			const std::vector<FSymbolQueryHit>& Definitions,
			const FProgram* Program,
			const FReferencesOptions* Options = nullptr,
			const std::string_view CandidateSource = {},
			const bool bAllowUnparsedCandidate = false)
		{
			if (SameDocumentPath(Path, CaretFileLabel))
			{
				return true;
			}
			for (const FSymbolQueryHit& Definition : Definitions)
			{
				if (!Definition.FileLabel.empty()
					&& SameDocumentPath(Path, Definition.FileLabel))
				{
					return true;
				}
			}
			if (Definitions.empty())
			{
				return false;
			}
			if (Program)
			{
				for (const FSymbolQueryHit& Definition : Definitions)
				{
					if (Definition.FileLabel.empty())
					{
						continue;
					}
					if (ModuleVisibility::IsExportedModuleVisible(
							Program->Imports,
							Definition.FileLabel,
							Path)
						|| ModuleVisibility::IsProjectInternalVisible(
							*Program,
							Definition.FileLabel,
							Path))
					{
						return true;
					}
				}
			}
			// A declaration's program does not contain the reverse import edges of
			// other open entry modules. Check the candidate's own import graph before
			// discarding it, while retaining the same visibility rules.
			if (Options)
			{
				VeroScriptModuleLoader Loader("", Options->ModuleEnvironment);
				ApplyModuleLoadingOptions(Loader, *Options);
				ApplyHostBindingsToLoader(Loader, Options->Bindings);
				ApplyDefines(Loader, Options->Defines);
				ApplyPredefinedMacros(Loader, Options->PredefinedMacros);
				for (const FSourceOverlay& Overlay : Options->SourceOverlays)
				{
					Loader.SetSourceOverlay(Overlay.Path, Overlay.Source);
				}
				auto Candidate = Loader.LoadSourceModule(CandidateSource, Path);
				if (Candidate && Options)
				{
					Detail::PrepareQueryProgram(Candidate, *Options);
				}

				if (Candidate && !Loader.HasError())
				{
					return ShouldScanDocument(Path, CaretFileLabel, Definitions, Candidate.get());
				}
				// Ordinary identifier hits are subsequently resolved with QueryAtSource,
				// including its parse recovery and exact definition identity check. Let
				// that step decide when a missing semicolon prevents loading the graph.
				// Operator sugar lacks that recovery path and remains conservative.
				if (bAllowUnparsedCandidate)
				{
					VeroScriptLexer Lexer(CandidateSource, Path);
					(void)Lexer.Tokenize();
					return !Lexer.HasError();
				}
			}
			return false;
		}

		FReferencesResult FindLoaded(
			const std::string_view Source,
			const std::string_view FileLabel,
			const FTextPosition Position,
			const FReferencesOptions& Options)
		{
			FSymbolQueryOptions QueryOptions;
			QueryOptions.ImportPaths = Options.ImportPaths;
			QueryOptions.StdlibSearchPaths = Options.StdlibSearchPaths;
			QueryOptions.ImplicitImportPaths = Options.ImplicitImportPaths;
			QueryOptions.bAllowCircularImports = Options.bAllowCircularImports;
			QueryOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
			QueryOptions.Defines = Options.Defines;
			QueryOptions.PredefinedMacros = Options.PredefinedMacros;
			QueryOptions.SourceOverlays = Options.SourceOverlays;
			QueryOptions.Bindings = Options.Bindings;
			QueryOptions.AnnotationProcessing = Options.AnnotationProcessing;
			QueryOptions.CompileProcessing = Options.CompileProcessing;
			QueryOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
			QueryOptions.ModuleEnvironment = Options.ModuleEnvironment;
			const FSymbolQueryResult Query =
				VeroScriptSymbolQuery::QueryAtSource(Source, Position, FileLabel, QueryOptions);
			if (!Query.bSuccess)
			{
				FReferencesResult Failed = Fail(Query.Error);
				if (!Query.Diagnostics.empty())
				{
					Failed.Diagnostics = Query.Diagnostics;
				}
				return Failed;
			}

			FReferencesResult Result;
			Result.bSuccess = true;
			Result.bHasToken = Query.bHasToken;
			Result.TokenText = Query.TokenText;
			if (!Query.bHasToken || Query.TokenText.empty())
			{
				return Result;
			}

			// Re-load entry for import graph (visibility filter for open overlays).
			std::unique_ptr<FProgram> Program;
			FDocumentSymbolResult CaretSymbols;
			{
				VeroScriptModuleLoader Loader("", Options.ModuleEnvironment);
				ApplyModuleLoadingOptions(Loader, Options);
				ApplyHostBindingsToLoader(Loader, Options.Bindings);
				ApplyDefines(Loader, Options.Defines);
				ApplyPredefinedMacros(Loader, Options.PredefinedMacros);
				for (const FSourceOverlay& Overlay : Options.SourceOverlays)
				{
					Loader.SetSourceOverlay(Overlay.Path, Overlay.Source);
				}
				Program = Loader.LoadSourceModule(Source, FileLabel);
				if (Program)
				{
					Detail::PrepareQueryProgram(Program, Options);
				}

				if (!Loader.HasError() && Program)
				{
					FDocumentSymbolOptions SymbolOptions;
					SymbolOptions.ImportPaths = Options.ImportPaths;
					SymbolOptions.StdlibSearchPaths = Options.StdlibSearchPaths;
					SymbolOptions.ImplicitImportPaths = Options.ImplicitImportPaths;
					SymbolOptions.bAllowCircularImports = Options.bAllowCircularImports;
					SymbolOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
					SymbolOptions.Defines = Options.Defines;
					SymbolOptions.PredefinedMacros = Options.PredefinedMacros;
					SymbolOptions.SourceOverlays = Options.SourceOverlays;
					SymbolOptions.bIncludeImports = true;
					SymbolOptions.bIncludeImportedExports = true;
					SymbolOptions.bIncludeSoftBuiltins = true;
					ApplyHostBindingsToSymbolOptions(SymbolOptions, Options.Bindings);
					CaretSymbols =
						VeroScriptDocumentSymbols::CollectFromProgram(*Program, SymbolOptions);
				}
				else
				{
					Program.reset();
				}
			}

			std::vector<FSymbolQueryHit> PhysicalDefinitions = Query.Definitions;
			EnsureImportedDefinitionsPhysical(FileLabel, Options, PhysicalDefinitions);

			// No definition at the caret → no reference set for ordinary identifiers.
			// (Operators still scan lexically below; see FilterResolvedIdentifierHits.)
			const bool bOperatorToken = IsOperatorMethodName(Query.TokenText);
			if (PhysicalDefinitions.empty() && !bOperatorToken)
			{
				return Result;
			}

			FScriptError LexError;
			const std::string CaretLabel(FileLabel);
			if (!AppendLexicalHits(Source, FileLabel, Query.TokenText, Result.References, LexError))
			{
				return Fail(std::move(LexError));
			}

			// Block/callable bindings cannot be referenced from another module.
			// Projected definitions in another source retain the full scan.
			const bool bFileLocal = Query.bLocalSymbol && std::all_of(
				Query.Definitions.begin(), Query.Definitions.end(), [&](const FSymbolQueryHit& Definition)
				{
					return Definition.FileLabel.empty() || SameDocumentPath(Definition.FileLabel, CaretLabel);
				});
			for (const FSourceOverlay& Overlay : Options.SourceOverlays)
			{
				if (bFileLocal)
				{
					break;
				}
				if (SameDocumentPath(Overlay.Path, CaretLabel))
				{
					continue;
				}
				// Ordinary references are lexical candidates before resolution. Avoid
				// loading unrelated entry modules that cannot contain such a candidate.
				// Operator sugar (`f()`, `f[]`, +=) need not spell its declaration name.
				if (!bOperatorToken && Overlay.Source.find(Query.TokenText) == std::string::npos)
				{
					continue;
				}
				if (!ShouldScanDocument(Overlay.Path, CaretLabel, Query.Definitions, Program.get(),
					&Options, Overlay.Source, !bOperatorToken))
				{
					continue;
				}
				if (!AppendLexicalHits(
						Overlay.Source,
						Overlay.Path,
						Query.TokenText,
						Result.References,
						LexError))
				{
					return Fail(std::move(LexError));
				}
			}

			for (const FSymbolQueryHit& Definition : Query.Definitions)
			{
				if (Definition.FileLabel.empty()
					|| SameDocumentPath(Definition.FileLabel, CaretLabel))
				{
					continue;
				}
				bool bAlreadyScanned = false;
				for (const FSourceOverlay& Overlay : Options.SourceOverlays)
				{
					if (SameDocumentPath(Overlay.Path, Definition.FileLabel)
						&& ShouldScanDocument(Overlay.Path, CaretLabel, Query.Definitions, Program.get(),
							&Options, Overlay.Source))
					{
						bAlreadyScanned = true;
						break;
					}
				}
				if (bAlreadyScanned)
				{
					continue;
				}
				// Still load definition file even if filter would skip an unrelated overlay path.
				std::string DefSource;
				if (!TryLoadSourceForPath(Definition.FileLabel, Options, DefSource))
				{
					continue;
				}
				if (!AppendLexicalHits(
						DefSource,
						Definition.FileLabel,
						Query.TokenText,
						Result.References,
						LexError))
				{
					return Fail(std::move(LexError));
				}
			}

			FilterInactivePreprocessorHits(
				Source,
				FileLabel,
				Options,
				Result.TokenText,
				Result.References);
			MarkDefinitions(Result.References, PhysicalDefinitions);
			FilterResolvedOperatorCallHits(
				Source,
				FileLabel,
				Options,
				Result.TokenText,
				PhysicalDefinitions,
				Program.get(),
				CaretSymbols.bSuccess ? &CaretSymbols : nullptr,
				Result.References);
			FilterResolvedMulticastAssignHits(
				Source,
				FileLabel,
				Options,
				Result.TokenText,
				PhysicalDefinitions,
				Program.get(),
				CaretSymbols.bSuccess ? &CaretSymbols : nullptr,
				Result.References);
			FilterResolvedIdentifierHits(
				Source,
				FileLabel,
				Options,
				Result.TokenText,
				PhysicalDefinitions,
				Result.References);
			return Result;
		}
	}

	FReferencesResult VeroScriptReferences::FindAtSource(
		const std::string_view Source,
		const FTextPosition Position,
		const std::string_view FileLabel,
		const FReferencesOptions& Options)
	{
		return FindLoaded(Source, FileLabel, Position, Options);
	}

	FReferencesResult VeroScriptReferences::FindAtFile(
		const std::string_view FilePath,
		const FTextPosition Position,
		const FReferencesOptions& Options)
	{
		std::ifstream Input(PathUtil::FromUtf8(FilePath), std::ios::binary);
		if (!Input)
		{
			FScriptError Error;
			FillDiagnostic(
				Error,
				"diag.cannot_open_script",
				FSourceLocation{std::string(FilePath), 1, 1},
				{FilePath});
			return Fail(std::move(Error));
		}
		std::ostringstream Buffer;
		Buffer << Input.rdbuf();
		return FindAtSource(Buffer.str(), Position, FilePath, Options);
	}
}
