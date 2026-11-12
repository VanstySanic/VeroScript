// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptRename.h"

#include "Internal/VeroScriptOperatorLex.h"
#include "Internal/VeroScriptDocumentSymbols.h"
#include "Internal/VeroScriptReferences.h"
#include "Internal/VeroScriptStandardNamespaceMetadata.h"
#include "Internal/VeroScriptSymbolQuery.h"
#include "Internal/VeroScriptSourceQuerySession.h"
#include "Internal/VeroScriptTypeHierarchy.h"
#include "VeroScriptDiagnosticUtil.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptLexer.h"
#include "VeroScriptModuleLoader.h"
#include "VeroScriptOverlayUtil.h"
#include "VeroScriptPath.h"
#include "VeroScriptTypes.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>

namespace VeroScript::Tooling
{
	namespace
	{
		std::string NormalizePathKey(std::string Path)
		{
			return VeroScript::PathUtil::Normalize(std::move(Path));
		}

		bool SameDocumentPath(const std::string& Left, const std::string& Right)
		{
			return VeroScript::PathUtil::Equal(Left, Right);
		}

		FDiagnostic MakeDiagnostic(
			const VeroScript::FScriptError& Error,
			const std::string_view FallbackFileLabel)
		{
			return ToToolingDiagnostic(Error, FallbackFileLabel, "rename failed");
		}

		bool IsValidIdentifierToken(const std::string_view Name)
		{
			if (Name.empty())
			{
				return false;
			}
			VeroScript::VeroScriptLexer Lexer(Name, "<rename-name>");
			const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
			if (Lexer.HasError() || Tokens.size() < 2)
			{
				return false;
			}
			if (Tokens[0].Kind == VeroScript::ETokenKind::Keyword
				|| Tokens[0].Kind == VeroScript::ETokenKind::BooleanLiteral
				|| Tokens[0].Kind == VeroScript::ETokenKind::NullptrLiteral)
			{
				return false;
			}
			if (Tokens[0].Kind != VeroScript::ETokenKind::Identifier
				|| Tokens[0].Lexeme != Name)
			{
				return false;
			}
			return Tokens[1].Kind == VeroScript::ETokenKind::EndOfFile;
		}

		std::string NormalizeContainer(const std::string& ContainerName)
		{
			return Detail::StripGenericArgs(ContainerName);
		}

		bool SameDefinition(
			const Detail::FSymbolQueryHit& Left,
			const Detail::FSymbolQueryHit& Right)
		{
			if (Detail::StripGenericArgs(Left.Name) != Detail::StripGenericArgs(Right.Name))
			{
				return false;
			}
			if (Left.DefLine != Right.DefLine
				|| Left.DefColumn != Right.DefColumn
				|| Left.Kind != Right.Kind)
			{
				return false;
			}
			if (NormalizeContainer(Left.ContainerName) != NormalizeContainer(Right.ContainerName))
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

		bool ResolvesToTarget(
			const Detail::FSymbolQueryResult& Query,
			const Detail::FSymbolQueryHit& Target)
		{
			if (!Query.bSuccess || !Query.bHasToken || Query.Definitions.empty())
			{
				return false;
			}
			for (const Detail::FSymbolQueryHit& Hit : Query.Definitions)
			{
				if (SameDefinition(Hit, Target))
				{
					return true;
				}
			}
			return false;
		}

		bool HasEntryMemberHomonym(
			const std::string_view Source,
			const std::string_view FileLabel,
			const std::string_view Name,
			const FRenameOptions& Options)
		{
			Detail::FDocumentSymbolOptions SymbolOptions;
			SymbolOptions.ImportPaths = Options.ImportPaths;
			SymbolOptions.StdlibSearchPaths = Options.StdlibSearchPaths;
			SymbolOptions.ImplicitImportPaths = Options.ImplicitImportPaths;
			SymbolOptions.bAllowCircularImports = Options.bAllowCircularImports;
			SymbolOptions.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
			SymbolOptions.Defines = Options.Defines;
			SymbolOptions.PredefinedMacros = Options.PredefinedMacros;
			SymbolOptions.SourceOverlays = ToEngineOverlays(Options.SourceOverlays);
			SymbolOptions.Bindings = Options.Bindings;
			SymbolOptions.AnnotationProcessing = Options.AnnotationProcessing;
			SymbolOptions.CompileProcessing = Options.CompileProcessing;
			SymbolOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
			SymbolOptions.ModuleEnvironment = Options.ModuleEnvironment;
			const Detail::FDocumentSymbolResult Symbols =
				Detail::VeroScriptDocumentSymbols::CollectFromSource(
					Source, FileLabel, SymbolOptions);
			if (!Symbols.bSuccess)
			{
				return false;
			}
			for (const Detail::FDocumentSymbol& Type : Symbols.Symbols)
			{
				if (Type.Kind != Detail::EDocumentSymbolKind::Class
					&& Type.Kind != Detail::EDocumentSymbolKind::Struct
					&& Type.Kind != Detail::EDocumentSymbolKind::Interface)
				{
					continue;
				}
				for (const Detail::FDocumentSymbol& Member : Type.Children)
				{
					if (Member.Kind == Detail::EDocumentSymbolKind::Field
						&& Detail::StripGenericArgs(Member.Name) == Name)
					{
						return true;
					}
				}
			}
			return false;
		}

		bool IsTypeSymbolKind(const Detail::EDocumentSymbolKind Kind)
		{
			return Kind == Detail::EDocumentSymbolKind::Class
				|| Kind == Detail::EDocumentSymbolKind::Struct
				|| Kind == Detail::EDocumentSymbolKind::Interface;
		}

		/**
		 * Type queries also match same-named Constructor children in the outline tree.
		 * Collapse those so Rename can target the type (and rewrite ctor / dtor name tokens).
		 */
		bool TryPickTypeRenameTarget(
			const std::vector<Detail::FSymbolQueryHit>& Definitions,
			Detail::FSymbolQueryHit& OutTarget)
		{
			const Detail::FSymbolQueryHit* TypeHit = nullptr;
			for (const Detail::FSymbolQueryHit& Hit : Definitions)
			{
				if (!IsTypeSymbolKind(Hit.Kind))
				{
					continue;
				}
				if (TypeHit && !SameDefinition(*TypeHit, Hit))
				{
					return false;
				}
				TypeHit = &Hit;
			}
			if (!TypeHit)
			{
				return false;
			}
			const std::string TypeName = Detail::StripGenericArgs(TypeHit->Name);
			for (const Detail::FSymbolQueryHit& Hit : Definitions)
			{
				if (IsTypeSymbolKind(Hit.Kind))
				{
					continue;
				}
				if (Hit.Kind == Detail::EDocumentSymbolKind::Constructor
					&& Detail::StripGenericArgs(Hit.Name) == TypeName
					&& (NormalizeContainer(Hit.ContainerName) == TypeName
						|| Hit.ContainerName.empty()))
				{
					continue;
				}
				return false;
			}
			OutTarget = *TypeHit;
			return true;
		}

		// Type rename: accept `~Type` destructor name tokens that embed the type identifier.
		bool IsDestructorNameOfTypeTarget(
			const Detail::FSymbolQueryResult& Query,
			const Detail::FSymbolQueryHit& Target,
			const std::string_view TypeName)
		{
			if (!Query.bSuccess || !Query.bHasToken || Query.Definitions.empty()
				|| !IsTypeSymbolKind(Target.Kind)
				|| Detail::StripGenericArgs(Target.Name) != TypeName
				|| !Detail::IsDestructorTokenForTypeName(Query.TokenText, TypeName))
			{
				return false;
			}
			for (const Detail::FSymbolQueryHit& Hit : Query.Definitions)
			{
				if (Hit.Name != Query.TokenText)
				{
					continue;
				}
				const std::string Container = NormalizeContainer(Hit.ContainerName);
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
			return false;
		}

		// Type rename: accept constructor-decl tokens that resolve to `Type.Type(...)`.
		bool IsConstructorOfTypeTarget(
			const Detail::FSymbolQueryResult& Query,
			const Detail::FSymbolQueryHit& Target,
			const std::string_view TypeName)
		{
			if (!Query.bSuccess || !Query.bHasToken || Query.Definitions.empty()
				|| !IsTypeSymbolKind(Target.Kind)
				|| Detail::StripGenericArgs(Target.Name) != TypeName
				|| Query.TokenText != TypeName)
			{
				return false;
			}
			for (const Detail::FSymbolQueryHit& Hit : Query.Definitions)
			{
				if (Hit.Kind != Detail::EDocumentSymbolKind::Constructor
					|| Detail::StripGenericArgs(Hit.Name) != TypeName)
				{
					continue;
				}
				const std::string Container = NormalizeContainer(Hit.ContainerName);
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
			return false;
		}

		std::string EditKey(
			const std::string& FileLabel,
			const std::uint32_t Line,
			const std::uint32_t Column)
		{
			return NormalizePathKey(FileLabel) + '#' + std::to_string(Line) + '#'
				+ std::to_string(Column);
		}

		bool TryGetSourceForPath(
			const std::string& Path,
			const std::string_view CaretSource,
			const std::string_view CaretFileLabel,
			const FRenameOptions& Options,
			std::string& OutSource)
		{
			if (SameDocumentPath(Path, std::string(CaretFileLabel)))
			{
				OutSource.assign(CaretSource.data(), CaretSource.size());
				return true;
			}
			for (auto I = Options.SourceOverlays.rbegin(); I != Options.SourceOverlays.rend(); ++I)
			{
				const auto& Overlay = *I;
				if (SameDocumentPath(Overlay.Path, Path))
				{
					OutSource = Overlay.Source;
					return true;
				}
			}
			std::ifstream Input(
				PathUtil::FromUtf8(NormalizePathKey(Path)),
				std::ios::binary);
			if (!Input)
			{
				return false;
			}
			std::ostringstream Buffer;
			Buffer << Input.rdbuf();
			OutSource = Buffer.str();
			return true;
		}

		FRenameResult Refuse(const std::string& Reason, const std::string& OldName = {})
		{
			FRenameResult Result;
			Result.bSuccess = true;
			Result.bCanRename = false;
			Result.Reason = Reason;
			Result.OldName = OldName;
			return Result;
		}

		Detail::FSymbolQueryOptions ToQueryOptions(const FRenameOptions& Options)
		{
			Detail::FSymbolQueryOptions Out;
			Out.ImportPaths = Options.ImportPaths;
			Out.StdlibSearchPaths = Options.StdlibSearchPaths;
			Out.ImplicitImportPaths = Options.ImplicitImportPaths;
			Out.bAllowCircularImports = Options.bAllowCircularImports;
			Out.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
			Out.Defines = Options.Defines;
			Out.PredefinedMacros = Options.PredefinedMacros;
			Out.SourceOverlays = ToEngineOverlays(Options.SourceOverlays);
			Out.Bindings = Options.Bindings;
			Out.AnnotationProcessing = Options.AnnotationProcessing;
			Out.CompileProcessing = Options.CompileProcessing;
			Out.ScriptCompileExtensions = Options.ScriptCompileExtensions;
			Out.ModuleEnvironment = Options.ModuleEnvironment;
			return Out;
		}

		Detail::FReferencesOptions ToRefsOptions(const FRenameOptions& Options)
		{
			Detail::FReferencesOptions Out;
			Out.ImportPaths = Options.ImportPaths;
			Out.StdlibSearchPaths = Options.StdlibSearchPaths;
			Out.ImplicitImportPaths = Options.ImplicitImportPaths;
			Out.bAllowCircularImports = Options.bAllowCircularImports;
			Out.bAllowProjectWideAccess = Options.bAllowProjectWideAccess;
			Out.Defines = Options.Defines;
			Out.PredefinedMacros = Options.PredefinedMacros;
			Out.SourceOverlays = ToEngineOverlays(Options.SourceOverlays);
			Out.Bindings = Options.Bindings;
			Out.AnnotationProcessing = Options.AnnotationProcessing;
			Out.CompileProcessing = Options.CompileProcessing;
			Out.ScriptCompileExtensions = Options.ScriptCompileExtensions;
			Out.ModuleEnvironment = Options.ModuleEnvironment;
			return Out;
		}

		struct FResolvedRenameTarget
		{
			Detail::FSymbolQueryResult Query;
			Detail::FSymbolQueryHit Target;
		};

		bool TryResolveRenameTarget(
			const std::string_view Source,
			const FTextPosition Position,
			const std::string_view FileLabel,
			const FRenameOptions& Options,
			FResolvedRenameTarget& OutResolved,
			FRenameResult& OutFailure)
		{
			const Detail::FTextPosition EnginePosition{Position.Line, Position.Column};
			OutResolved.Query = Detail::VeroScriptSymbolQuery::QueryAtSource(
				Source,
				EnginePosition,
				FileLabel,
				ToQueryOptions(Options));
			if (!OutResolved.Query.bSuccess)
			{
				AppendToolingDiagnostics(
					OutFailure.Diagnostics,
					OutResolved.Query.Error,
					OutResolved.Query.Diagnostics,
					FileLabel,
					"rename failed");
				return false;
			}
			if (!OutResolved.Query.bHasToken || OutResolved.Query.TokenText.empty())
			{
				OutFailure = Refuse("no identifier under caret");
				return false;
			}
			if (Detail::IsOperatorMethodName(OutResolved.Query.TokenText))
			{
				OutFailure = Refuse("operator overloads cannot be renamed", OutResolved.Query.TokenText);
				return false;
			}
			if (OutResolved.Query.TokenText.size() >= 2
				&& OutResolved.Query.TokenText.front() == '~')
			{
				OutFailure = Refuse(
					"destructors are renamed by renaming their type",
					OutResolved.Query.TokenText);
				return false;
			}
			if (OutResolved.Query.Definitions.empty())
			{
				OutFailure = Refuse("could not resolve a definition", OutResolved.Query.TokenText);
				return false;
			}
			if (OutResolved.Query.Definitions.size() == 1)
			{
				OutResolved.Target = OutResolved.Query.Definitions.front();
			}
			else if (!TryPickTypeRenameTarget(OutResolved.Query.Definitions, OutResolved.Target))
			{
				OutFailure = Refuse(
					"definition is ambiguous; move the caret to a unique use or declaration",
					OutResolved.Query.TokenText);
				return false;
			}
			if (OutResolved.Target.Kind == Detail::EDocumentSymbolKind::Constructor)
			{
				OutFailure = Refuse(
					"constructors are renamed by renaming their type",
					OutResolved.Query.TokenText);
				return false;
			}
			if (OutResolved.Target.Kind == Detail::EDocumentSymbolKind::Namespace
				&& Detail::FindStandardNamespaceMetadata(OutResolved.Target.Name))
			{
				OutFailure = Refuse("standard-library namespaces cannot be renamed", OutResolved.Query.TokenText);
				return false;
			}
			if (OutResolved.Target.DefLine == 0)
			{
				OutFailure = Refuse("virtual / builtin symbols cannot be renamed", OutResolved.Query.TokenText);
				return false;
			}
			if (OutResolved.Target.FileLabel.rfind("veroscript-generated:", 0) == 0)
			{
				OutFailure = Refuse("generated methods are read-only; change their annotation or processor",
					OutResolved.Query.TokenText);
				return false;
			}
			if (OutResolved.Target.FileLabel.rfind("veroscript-module:/", 0) == 0)
			{
				OutFailure = Refuse("host module sources are read-only; update the host module snapshot", OutResolved.Query.TokenText);
				return false;
			}
			if (OutResolved.Target.bSoftBuiltin)
			{
				OutFailure = Refuse("host / soft builtins cannot be renamed", OutResolved.Query.TokenText);
				return false;
			}
			if (OutResolved.Target.Kind == Detail::EDocumentSymbolKind::Macro
				|| OutResolved.Target.Detail.rfind("#define", 0) == 0)
			{
				OutFailure = Refuse("preprocessor macros cannot be renamed", OutResolved.Query.TokenText);
				return false;
			}
			if (OutResolved.Target.ContainerName.empty()
				&& OutResolved.Target.Detail.rfind("template value parameter", 0) != 0
				&& OutResolved.Target.Detail != "generic type parameter"
				&& (OutResolved.Target.Kind == Detail::EDocumentSymbolKind::Variable
					|| OutResolved.Target.Kind == Detail::EDocumentSymbolKind::Constant)
				&& HasEntryMemberHomonym(
					Source, FileLabel, OutResolved.Query.TokenText, Options))
			{
				OutFailure = Refuse(
					"definition is ambiguous between a global and a member; "
					"rename from an explicit declaration or qualified use",
					OutResolved.Query.TokenText);
				return false;
			}
			return true;
		}
	}

	FRenameResult VeroScriptRename::CanRenameAtSource(
		const std::string_view Source,
		const FTextPosition Position,
		const std::string_view FileLabel,
		const FRenameOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FRenameResult>(Failure);
			}

			return CanRenameAtSource(Source, Position, FileLabel, Effective);
		}

		FResolvedRenameTarget Resolved;
		FRenameResult Failure;
		if (!TryResolveRenameTarget(Source, Position, FileLabel, Options, Resolved, Failure))
		{
			return Failure;
		}
		FRenameResult Result;
		Result.bSuccess = true;
		Result.bCanRename = true;
		Result.OldName = Resolved.Query.TokenText;
		return Result;
	}

	FRenameResult VeroScriptRename::PrepareAtSource(
		const std::string_view Source,
		const FTextPosition Position,
		const std::string_view NewName,
		const std::string_view FileLabel,
		const FRenameOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FRenameResult>(Failure);
			}

			return PrepareAtSource(Source, Position, NewName, FileLabel, Effective);
		}

		if (!IsValidIdentifierToken(NewName))
		{
			return Refuse("new name is not a valid rename identifier");
		}

		FResolvedRenameTarget Resolved;
		FRenameResult Failure;
		if (!TryResolveRenameTarget(Source, Position, FileLabel, Options, Resolved, Failure))
		{
			return Failure;
		}
		const Detail::FSymbolQueryResult& Query = Resolved.Query;
		const Detail::FSymbolQueryHit& Target = Resolved.Target;
		if (Query.TokenText == NewName)
		{
			return Refuse("new name equals old name", Query.TokenText);
		}
		const Detail::FTextPosition EnginePosition{Position.Line, Position.Column};
		const Detail::FSymbolQueryOptions QueryOptions = ToQueryOptions(Options);

		const Detail::FReferencesResult Refs = Detail::VeroScriptReferences::FindAtSource(
			Source,
			EnginePosition,
			FileLabel,
			ToRefsOptions(Options));
		if (!Refs.bSuccess)
		{
			FRenameResult Result;
			AppendToolingDiagnostics(
				Result.Diagnostics,
				Refs.Error,
				Refs.Diagnostics,
				FileLabel,
				"rename failed");
			return Result;
		}

		/**
		 * Lexical FindReferences is a candidate set only. Re-resolve each hit with
		 * SymbolQuery so homonyms (locals/fields with the same spelling) are skipped.
		 */
		FRenameResult Result;
		Result.bSuccess = true;
		Result.bCanRename = true;
		Result.OldName = Query.TokenText;
		Result.Edits.reserve(Refs.References.size());
		std::unordered_set<std::string> SeenEdits;
		Detail::FSourceQuerySession Queries(QueryOptions);
		for (const Detail::FReferenceHit& Hit : Refs.References)
		{
			const std::string HitFile = Hit.FileLabel.empty()
				? std::string(FileLabel)
				: Hit.FileLabel;
			const std::string Key = EditKey(HitFile, Hit.Line, Hit.StartColumn);
			if (!SeenEdits.insert(Key).second)
			{
				continue;
			}

			std::string HitSource;
			if (!TryGetSourceForPath(HitFile, Source, FileLabel, Options, HitSource))
			{
				continue;
			}

			const Detail::FSymbolQueryResult HitQuery = Queries.QueryAtSource(
				HitSource,
				Detail::FTextPosition{Hit.Line, Hit.StartColumn},
				HitFile);
			if (HitQuery.TokenText == Query.TokenText)
			{
				if (!ResolvesToTarget(HitQuery, Target)
					&& !IsConstructorOfTypeTarget(HitQuery, Target, Query.TokenText))
				{
					continue;
				}
			}
			else if (!IsDestructorNameOfTypeTarget(HitQuery, Target, Query.TokenText))
			{
				continue;
			}

			if (HitFile.rfind("veroscript-module:/", 0) == 0)
			{
				return Refuse("rename would modify a read-only host module source", Query.TokenText);
			}

			FTextEdit Edit;
			Edit.FileLabel = HitFile;
			Edit.Range.StartLine = Hit.Line;
			Edit.Range.StartColumn = Hit.StartColumn;
			Edit.Range.EndLine = Hit.Line;
			Edit.Range.EndColumn = Hit.EndColumn;
			Edit.NewText = std::string(NewName);
			Result.Edits.push_back(std::move(Edit));
		}

		if (Result.Edits.empty())
		{
			return Refuse("no editable occurrences resolve to the same definition", Query.TokenText);
		}

		// Apply from bottom of each file so earlier offsets stay valid.
		std::sort(
			Result.Edits.begin(),
			Result.Edits.end(),
			[](const FTextEdit& Left, const FTextEdit& Right)
			{
				const std::string LeftPath = NormalizePathKey(Left.FileLabel);
				const std::string RightPath = NormalizePathKey(Right.FileLabel);
				const int PathCmp = LeftPath.compare(RightPath);
				if (PathCmp != 0)
				{
					return PathCmp < 0;
				}
				if (Left.Range.StartLine != Right.Range.StartLine)
				{
					return Left.Range.StartLine > Right.Range.StartLine;
				}
				return Left.Range.StartColumn > Right.Range.StartColumn;
			});
		return Result;
	}

	FRenameResult VeroScriptRename::PrepareAtFile(
		const std::string_view FilePath,
		const FTextPosition Position,
		const std::string_view NewName,
		const FRenameOptions& Options)
	{
		if (Options.ModuleEnvironment && Options.CompileProcessing.ModuleEnvironment != Options.ModuleEnvironment)
		{
			auto Effective = Options;
			std::string Failure;
			if (!ResolveToolingModuleEnvironment(Effective, Failure))
			{
				return ToolingModuleEnvironmentFailure<FRenameResult>(Failure);
			}

			return PrepareAtFile(FilePath, Position, NewName, Effective);
		}

		std::ifstream Input(PathUtil::FromUtf8(FilePath), std::ios::binary);
		if (!Input)
		{
			FRenameResult Result;
			FScriptError Error;
			FillDiagnostic(
				Error,
				"diag.cannot_open_script",
				FSourceLocation{std::string(FilePath), 1, 1},
				{FilePath});
			Result.Diagnostics.push_back(MakeDiagnostic(Error, FilePath));
			return Result;
		}
		std::ostringstream Buffer;
		Buffer << Input.rdbuf();
		return PrepareAtSource(Buffer.str(), Position, NewName, FilePath, Options);
	}
}
