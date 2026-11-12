// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

bool FindMacroDefOrInitialDefine(const VeroScript::FPreprocessLineMap& LineMap,
    const std::string_view Name, const std::uint32_t AtPhysicalLine,
    const std::vector<std::string_view>& Defines,
    VeroScript::FPreprocessMacroDef& OutMacro,
    const std::uint32_t AtPhysicalColumn = 0)
{
	if (FindActiveMacroDef(LineMap, Name, OutMacro, AtPhysicalLine, AtPhysicalColumn))
	{
		return true;
	}
	for (const VeroScript::FPreprocessMacroDef& Event : LineMap.ActiveMacros)
	{
		if (Event.Name == Name &&
		    (AtPhysicalLine == 0 || Event.ActivationLine < AtPhysicalLine ||
		        (Event.ActivationLine == AtPhysicalLine &&
		            (AtPhysicalColumn == 0 || Event.ActivationColumn <= AtPhysicalColumn))))
		{
			// The source timeline contains an #undef as its last applicable
			// event, so it intentionally suppresses the initial -D value.
			return false;
		}
	}
	std::unordered_map<std::string, std::string> InitialDefines;
	std::string DefineError;
	if (!VeroScript::BuildDefineMap(Defines, InitialDefines, DefineError))
	{
		return false;
	}
	const auto Found = InitialDefines.find(std::string(Name));
	if (Found == InitialDefines.end())
	{
		return false;
	}
	OutMacro = {};
	OutMacro.Name = Found->first;
	OutMacro.Body = Found->second;
	OutMacro.bActive = true;
	OutMacro.bPredefined = true;
	OutMacro.SourceName = "command line";
	OutMacro.Line = 0;
	OutMacro.Column = 0;
	return true;
}

/**
 * Active `#define` names at AtPhysicalLine (object- and function-like).
 * Fills gaps only — symbols / locals / keywords keep priority. Timeline
 * matches Hover: last define/undef event at or before the line wins.
 * CLI `-D` names (not in ActiveMacros) are gap-filled when Defines is set,
 * unless a later `#undef` tombstone covers them at this line.
 * bCallSiteInsertText: code sites use `NAME(` for function-like; PP
 * MacroName / `#if` / `defined()` use bare `NAME`.
 */
void AppendMacroCompletions(const VeroScript::FPreprocessLineMap& LineMap,
    const std::string_view Source, const std::string_view Prefix,
    const std::uint32_t AtPhysicalLine,
    std::unordered_map<std::string, FCompletionItem>& OutItems,
    const bool bCallSiteInsertText = true,
    const std::vector<std::string_view>* Defines = nullptr,
    const std::uint32_t AtPhysicalColumn = 0)
{
	std::unordered_map<std::string, const VeroScript::FPreprocessMacroDef*> LatestByName;
	for (const VeroScript::FPreprocessMacroDef& Macro : LineMap.ActiveMacros)
	{
		if (Macro.Name.empty())
		{
			continue;
		}
		if (AtPhysicalLine != 0 &&
		    (Macro.ActivationLine > AtPhysicalLine ||
		        (Macro.ActivationLine == AtPhysicalLine && AtPhysicalColumn != 0 &&
		            Macro.ActivationColumn > AtPhysicalColumn)))
		{
			continue;
		}
		LatestByName[Macro.Name] = &Macro;
	}

	std::vector<std::string_view> Lines;
	for (const auto& Pair : LatestByName)
	{
		const VeroScript::FPreprocessMacroDef& Macro = *Pair.second;
		if (!Macro.bActive || !PrefixMatches(Macro.Name, Prefix) ||
		    OutItems.find(Macro.Name) != OutItems.end())
		{
			continue;
		}

		FCompletionItem Item;
		Item.Kind = EDocumentSymbolKind::Macro;
		Item.Label = Macro.Name;
		Item.Detail = FormatMacroCompletionDetail(Macro);
		if (!Macro.bPredefined && !Macro.bImported && Macro.DirectiveStartLine != 0)
		{
			if (Lines.empty())
			{
				Lines = SplitSourceLines(Source);
			}
			Item.Documentation = ExtractLeadingDocumentation(Lines, Macro.DirectiveStartLine);
		}
		Item.InsertText =
		    (bCallSiteInsertText && Macro.bFunctionLike) ? (Macro.Name + "(") : Macro.Name;
		OutItems.emplace(Macro.Name, std::move(Item));
	}

	if (Defines == nullptr || Defines->empty())
	{
		return;
	}
	std::unordered_map<std::string, std::string> CliDefines;
	std::string DefineError;
	if (!VeroScript::BuildDefineMap(*Defines, CliDefines, DefineError))
	{
		return;
	}
	for (const auto& Entry : CliDefines)
	{
		if (LatestByName.find(Entry.first) != LatestByName.end())
		{
			// Source timeline already decided (active define or #undef).
			continue;
		}
		if (!PrefixMatches(Entry.first, Prefix) || OutItems.find(Entry.first) != OutItems.end())
		{
			continue;
		}
		VeroScript::FPreprocessMacroDef Synthetic;
		Synthetic.Name = Entry.first;
		Synthetic.Body = Entry.second;
		Synthetic.bActive = true;
		FCompletionItem Item;
		Item.Kind = EDocumentSymbolKind::Macro;
		Item.Label = Entry.first;
		Item.Detail = FormatMacroCompletionDetail(Synthetic);
		Item.InsertText = Entry.first;
		OutItems.emplace(Entry.first, std::move(Item));
	}
}

struct FGotoCompletionContext
{
	std::string Prefix;
	bool bHasReplaceRange = false;
	std::uint32_t ReplaceLine = 1;
	std::uint32_t ReplaceStartColumn = 1;
	std::uint32_t ReplaceEndColumn = 1;
};

struct FCaptureCompletionContext
{
	std::string Prefix;
	bool bHasReplaceRange = false;
	std::uint32_t ReplaceLine = 1;
	std::uint32_t ReplaceStartColumn = 1;
	std::uint32_t ReplaceEndColumn = 1;
	std::uint32_t InsertLine = 1;
	std::uint32_t InsertColumn = 1;
};

struct FImportPathCompletionContext
{
	std::string Prefix;
	// Source spelling before escape decoding, used by client filtering.
	std::string SourcePrefix;
	// Full contents between quotes (closed string) or Prefix when unterminated.
	std::string PathText;
	// `export import` and `#include` both forward the target's exported API.
	bool bReExport = false;
	bool bCompilerExtension = false;
	bool bRawString = false;
	bool bHasReplaceRange = false;
	std::uint32_t ReplaceLine = 1;
	std::uint32_t ReplaceStartColumn = 1;
	std::uint32_t ReplaceEndColumn = 1;
};

struct FPreprocessorDirectiveCompletionContext
{
	enum class EKind : std::uint8_t
	{
		// Completing `define` / `ifdef` / … right after `#`.
		DirectiveName,
		// Completing the macro name after `#define` / `#ifdef` / `#ifndef` / `#undef`.
		MacroName,
		// Completing inside `#if` / `#elif` expression (`defined`, macros).
		IfExpr,
		// Stay in PP mode with no items (`#define` params/body, `#else`, …).
		NoCompletions,
	};

	EKind Kind = EKind::DirectiveName;
	std::string Prefix;
	bool bHasReplaceRange = false;
	std::uint32_t ReplaceLine = 1;
	std::uint32_t ReplaceStartColumn = 1;
	std::uint32_t ReplaceEndColumn = 1;
	// True when caret is inside `defined(...)` — do not offer nested `defined`.
	bool bInsideDefined = false;
};

bool FindGotoCompletionContext(
    std::string_view Source,
    std::string_view FileLabel,
    FTextPosition Position,
    FGotoCompletionContext& Out);
bool FindCaptureCompletionContext(
    std::string_view Source,
    std::string_view FileLabel,
    FTextPosition Position,
    FCaptureCompletionContext& Out);
bool FindImportPathCompletionContext(
    std::string_view Source,
    FTextPosition Position,
    FImportPathCompletionContext& Out);
bool ResolveImportPathOnDisk(
    std::string_view PathText,
    std::string_view FileLabel,
    const std::vector<std::string_view>& ImportPaths,
    std::string& OutResolved);
bool FindPreprocessorDirectiveCompletionContext(
    std::string_view Source,
    FTextPosition Position,
    FPreprocessorDirectiveCompletionContext& Out);
bool FindIdentifierAt(
    std::string_view Source,
    std::string_view FileLabel,
    FTextPosition Position,
    FToken& OutToken);
bool IsHashDirectiveLine(std::string_view Source, FTextPosition Position);

bool IsTypeSymbolKind(const EDocumentSymbolKind Kind)
{
	return Kind == EDocumentSymbolKind::Struct || Kind == EDocumentSymbolKind::Class || Kind == EDocumentSymbolKind::Interface || Kind == EDocumentSymbolKind::Enum || Kind == EDocumentSymbolKind::TypeAlias;
}

const FDocumentSymbol* FindTypeSymbol(
    const std::vector<FDocumentSymbol>& Symbols,
    const std::string_view TypeName)
{
	if (ActiveSymbolLookupIndex && ActiveSymbolLookupIndex->Matches(Symbols) && FSymbolLookupIndex::Supports(TypeName))
	{
		if (const auto* Entries = ActiveSymbolLookupIndex->Find(TypeName))
		{
			for (const auto& Entry : *Entries)
			{
				if (Entry.bTopLevel && IsTypeSymbolKind(Entry.Symbol->Kind) && NameMatchesSymbol(Entry.Symbol->Name, TypeName))
				{
					return Entry.Symbol;
				}
			}
		}
		return nullptr;
	}
	for (const FDocumentSymbol& Symbol : Symbols)
	{
		if (IsTypeSymbolKind(Symbol.Kind) && NameMatchesSymbol(Symbol.Name, TypeName))
		{
			return &Symbol;
		}
	}
	return nullptr;
}

bool IsKnownTypeName(
    const std::string_view TypeName,
    const std::vector<FDocumentSymbol>& Symbols,
    const VeroScript::IHostBindingCatalog* const Bindings)
{
	if (TypeName.empty())
	{
		return false;
	}
	if (VeroScript::IsTypeKeyword(TypeName) || VeroScript::IsContainerTypeKeyword(TypeName) || FindTypeSymbol(Symbols, TypeName))
	{
		return true;
	}
	return Bindings && (Bindings->FindType(TypeName) || Bindings->FindTypeAlias(TypeName) || Bindings->FindGenericTypeTemplate(TypeName));
}

struct FUsingNamespaceTargetContext
{
	std::string ParentPath;
	std::string Prefix;
	FTextPosition DirectivePosition;
	bool bRoot = false;
	bool bHasReplaceRange = false;
	std::uint32_t ReplaceLine = 1;
	std::uint32_t ReplaceStartColumn = 1;
	std::uint32_t ReplaceEndColumn = 1;
};

std::string NormalizeNamespaceSpelling(std::string_view Name);
std::string QualifiedIdentifierAt(std::string_view Source, std::string_view FileLabel,
    std::uint32_t TokenLine, std::uint32_t TokenColumn);

void AppendNamespaceRootCompletions(
    const FProgram& Program,
    const std::vector<FDocumentSymbol>& Symbols,
    const std::string_view Prefix,
    std::unordered_map<std::string, FCompletionItem>& OutItems,
    const bool bAppendScopeSeparator = false,
    const bool bHasFollowingScopeSeparator = false)
{
	std::unordered_map<std::string, bool> Roots;
	for (const FNamespaceDecl& NamespaceDecl : Program.Namespaces)
	{
		const std::string Name = NormalizeNamespaceSpelling(NamespaceDecl.Name);
		const std::size_t Dot = Name.find('.');
		const std::string Root = Name.substr(0, Dot);
		if (!Root.empty())
		{
			Roots.insert_or_assign(Root, false);
		}
	}
	for (const FDocumentSymbol& Symbol : Symbols)
	{
		if (Symbol.Kind == EDocumentSymbolKind::Namespace)
		{
			const std::string Name = NormalizeNamespaceSpelling(Symbol.Name);
			const std::string Root = Name.substr(0, Name.find('.'));
			if (!Root.empty())
			{
				bool& bSoftBuiltin = Roots[Root];
				bSoftBuiltin = bSoftBuiltin || Symbol.bSoftBuiltin;
			}
		}
	}
	for (const auto& Pair : Roots)
	{
		if (!PrefixMatches(Pair.first, Prefix))
		{
			continue;
		}
		if (const auto Existing = OutItems.find(Pair.first);
		    Existing != OutItems.end())
		{
			// Symbol completion may have already inserted the root namespace.
			// Preserve its richer source detail, but apply the contextual edit.
			if (Existing->second.Kind == EDocumentSymbolKind::Namespace)
			{
				Existing->second.InsertText = bAppendScopeSeparator && !bHasFollowingScopeSeparator
				    ? Pair.first + "::"
				    : Pair.first;
			}
			continue;
		}
		FCompletionItem Item;
		Item.Kind = EDocumentSymbolKind::Namespace;
		Item.Label = Pair.first;
		Item.Detail = "namespace";
		// At an expression/statement site a namespace is necessarily the
		// beginning of a qualified access.  Insert the separator there, but
		// never at a `using namespace` / declaration site. Once `::` has
		// been typed, member completion handles the next path segment.
		Item.InsertText = bAppendScopeSeparator && !bHasFollowingScopeSeparator
		    ? Pair.first + "::"
		    : Pair.first;
		Item.bSoftBuiltin = Pair.second;
		OutItems.emplace(Item.Label, std::move(Item));
	}
}

bool FindUsingNamespaceTargetContext(
    const std::string_view Source,
    const std::string_view FileLabel,
    const FTextPosition Position,
    FUsingNamespaceTargetContext& Out)
{
	FQueryLexer Lexer(Source, FileLabel);
	const std::vector<FToken>& Tokens = Lexer.Tokenize();
	std::size_t NamespaceIndex = Tokens.size();
	if (ActiveQueryLexicalSnapshot && ActiveQueryLexicalSnapshot->Matches(Source, FileLabel))
	{
		NamespaceIndex = ActiveQueryLexicalSnapshot->LastUsingNamespaceBefore(Position);
	}
	else
	{
		for (std::size_t Index = 1; Index < Tokens.size(); ++Index)
		{
			if (Tokens[Index - 1].Lexeme == "using" && Tokens[Index].Lexeme == "namespace" && TokenEndsAtOrBefore(Tokens[Index], Position))
			{
				NamespaceIndex = Index;
			}
		}
	}
	if (NamespaceIndex >= Tokens.size())
	{
		return false;
	}

	Out = FUsingNamespaceTargetContext{};
	Out.DirectivePosition = {Tokens[NamespaceIndex - 1].Location.Line,
	    Tokens[NamespaceIndex - 1].Location.Column};
	Out.ReplaceLine = Position.Line;
	Out.ReplaceStartColumn = Position.Column;
	Out.ReplaceEndColumn = Position.Column;
	bool bExpectIdentifier = true;
	for (std::size_t Index = NamespaceIndex + 1; Index < Tokens.size(); ++Index)
	{
		const FToken& Token = Tokens[Index];
		if (Token.Kind == ETokenKind::EndOfFile)
		{
			break;
		}
		if (Token.Location.Line > Position.Line || (Token.Location.Line == Position.Line && Token.Location.Column > Position.Column))
		{
			break;
		}
		if (Token.Lexeme == ";" || Token.Lexeme == "{" || Token.Lexeme == "}")
		{
			// The caret at a terminator's start is still before that token.
			// `using namespace |;` needs an empty namespace-name prefix.
			return bExpectIdentifier && Token.Location.Line == Position.Line && Token.Location.Column == Position.Column;
		}
		if (bExpectIdentifier)
		{
			if (Index == NamespaceIndex + 1 && Token.Lexeme == "::")
			{
				Out.bRoot = true;
				continue;
			}
			if (Token.Kind != ETokenKind::Identifier)
			{
				return false;
			}
			const std::uint32_t TokenEnd = Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size());
			if (Token.Location.Line == Position.Line && Position.Column >= Token.Location.Column && Position.Column <= TokenEnd)
			{
				const std::uint32_t Relative = Position.Column >= Token.Location.Column
				    ? Position.Column - Token.Location.Column
				    : 0;
				Out.Prefix.assign(Token.Lexeme.substr(
				    0, std::min<std::size_t>(Relative, Token.Lexeme.size())));
				Out.bHasReplaceRange = true;
				Out.ReplaceLine = Token.Location.Line;
				Out.ReplaceStartColumn = Token.Location.Column;
				Out.ReplaceEndColumn = Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size());
				return true;
			}
			if (!Out.ParentPath.empty())
			{
				Out.ParentPath.push_back('.');
			}
			Out.ParentPath.append(Token.Lexeme.data(), Token.Lexeme.size());
			bExpectIdentifier = false;
		}
		else
		{
			if (Token.Kind != ETokenKind::Symbol || (Token.Lexeme != "." && Token.Lexeme != "::"))
			{
				return false;
			}
			bExpectIdentifier = true;
		}
	}
	return bExpectIdentifier;
}

void CollectLexicalNamespaceDeclarations(
    const std::string_view Source,
    const std::string_view FileLabel,
    FProgram& OutProgram)
{
	FQueryLexer Lexer(Source, FileLabel);
	const std::vector<FToken>& Tokens = Lexer.Tokenize();
	for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
	{
		if (Tokens[Index].Lexeme != "namespace" || (Index > 0 && Tokens[Index - 1].Lexeme == "using"))
		{
			continue;
		}
		std::string Declared;
		std::size_t Cursor = Index + 1;
		bool bExpectIdentifier = true;
		for (; Cursor < Tokens.size(); ++Cursor)
		{
			const FToken& Token = Tokens[Cursor];
			if (bExpectIdentifier)
			{
				if (Token.Kind != ETokenKind::Identifier)
				{
					break;
				}
				if (!Declared.empty())
				{
					Declared.push_back('.');
				}
				Declared.append(Token.Lexeme.data(), Token.Lexeme.size());
				bExpectIdentifier = false;
			}
			else if (Token.Lexeme == "." || Token.Lexeme == "::")
			{
				bExpectIdentifier = true;
			}
			else
			{
				break;
			}
		}
		if (Declared.empty() || Cursor >= Tokens.size() || Tokens[Cursor].Lexeme != "{")
		{
			continue;
		}
		const std::string Active = FindActiveNamespaceAt(
		    Source,
		    FileLabel,
		    FTextPosition{
		        Tokens[Index].Location.Line,
		        Tokens[Index].Location.Column});
		FNamespaceDecl NamespaceDecl;
		NamespaceDecl.Name = Active.empty() ? Declared : Active + "." + Declared;
		NamespaceDecl.ModulePath = std::string(FileLabel);
		NamespaceDecl.Location = Tokens[Index].Location;
		OutProgram.Namespaces.push_back(std::move(NamespaceDecl));
	}
}

FCompletionResult CompleteUsingNamespaceTarget(
    const FProgram& Program,
    const std::vector<FDocumentSymbol>& Symbols,
    const std::string_view Source,
    const std::string_view FileLabel,
    const FTextPosition Position)
{
	FUsingNamespaceTargetContext Context;
	if (!FindUsingNamespaceTargetContext(Source, FileLabel, Position, Context))
	{
		return {};
	}

	std::unordered_set<std::string> NamespaceNames;
	for (const FNamespaceDecl& NamespaceDecl : Program.Namespaces)
	{
		NamespaceNames.insert(NormalizeNamespaceSpelling(NamespaceDecl.Name));
	}
	for (const FDocumentSymbol& Symbol : Symbols)
	{
		const std::string Name = NormalizeNamespaceSpelling(Symbol.Name);
		if (Symbol.Kind == EDocumentSymbolKind::Namespace)
		{
			NamespaceNames.insert(Name);
		}
		std::size_t Dot = Name.find('.');
		while (Dot != std::string::npos)
		{
			NamespaceNames.insert(Name.substr(0, Dot));
			Dot = Name.find('.', Dot + 1);
		}
	}

	FCompletionResult Result;
	Result.bSuccess = true;
	Result.Prefix = Context.Prefix;
	Result.bHasReplaceRange = Context.bHasReplaceRange;
	Result.ReplaceLine = Context.ReplaceLine;
	Result.ReplaceStartColumn = Context.ReplaceStartColumn;
	Result.ReplaceEndColumn = Context.ReplaceEndColumn;
	std::unordered_map<std::string, FCompletionItem> Items;
	std::string Scope = Context.bRoot ? std::string{} : FindActiveNamespaceAt(Source, FileLabel, Position);
	std::vector<std::string> Parents;
	if (!Context.ParentPath.empty())
	{
		Parents = BindNamespacePath(Context.bRoot ? "::" + Context.ParentPath : Context.ParentPath,
		    Scope, [&](const std::string& Name) { return NamespaceNames.count(Name) != 0; });
	}
	else
	{
		for (;;)
		{
			Parents.push_back(Scope);
			if (Scope.empty())
			{
				break;
			}
			const std::size_t Dot = Scope.rfind('.');
			Scope = Dot == std::string::npos ? std::string{} : Scope.substr(0, Dot);
		}
	}
	for (const std::string& Parent : Parents)
	{
		const std::string CandidatePrefix = Parent.empty() ? std::string{} : Parent + ".";
		for (const std::string& NamespaceName : NamespaceNames)
		{
			if (NamespaceName.rfind(CandidatePrefix, 0) != 0)
			{
				continue;
			}
			const std::string Remainder = NamespaceName.substr(CandidatePrefix.size());
			const std::size_t Dot = Remainder.find('.');
			const std::string Leaf = Remainder.substr(0, Dot);
			if (Leaf.empty() || !PrefixMatches(Leaf, Context.Prefix))
			{
				continue;
			}
			FCompletionItem Item;
			Item.Kind = EDocumentSymbolKind::Namespace;
			Item.Label = Leaf;
			Item.Detail = "namespace";
			Item.InsertText = Leaf;
			Items.insert_or_assign(Leaf, std::move(Item));
		}
	}
	Result.Items.reserve(Items.size());
	for (auto& Pair : Items)
	{
		Result.Items.push_back(std::move(Pair.second));
	}
	FinalizeCompletionItems(
	    Result.Items, ECompletionRankContext::NamespaceLeaf, Result.Prefix);
	return Result;
}

bool CollectUsingNamespaceTargetHits(
    const FProgram& Program,
    const std::vector<FDocumentSymbol>& Symbols,
    const std::string_view Source,
    const std::string_view FileLabel,
    const FTextPosition Position,
    const FPreprocessLineMap& LineMap,
    std::vector<FSymbolQueryHit>& OutHits)
{
	if (ActiveQueryLexicalSnapshot && ActiveQueryLexicalSnapshot->Matches(Source, FileLabel) && !ActiveQueryLexicalSnapshot->HasUsingKeyword)
	{
		return false;
	}
	FUsingNamespaceTargetContext Context;
	if (!FindUsingNamespaceTargetContext(Source, FileLabel, Position, Context))
	{
		return false;
	}
	// The raw token identifies this directive even when several share a
	// line. Match its keyword in AST space, not the physical caret line:
	// continued directives and multi-line using declarations shift both.
	FTextPosition DirectivePosition = Context.DirectivePosition;
	MapCaretPhysicalToPreprocessed(LineMap,
	    Context.DirectivePosition.Line, Context.DirectivePosition.Column,
	    DirectivePosition.Line, DirectivePosition.Column);
	for (const FUsingNamespaceDecl& Directive : Program.UsingNamespaces)
	{
		if (Directive.ModulePath != Program.EntryModulePath || Directive.Location.Line != DirectivePosition.Line || Directive.Location.Column != DirectivePosition.Column)
		{
			continue;
		}
		const std::string Name = Context.bHasReplaceRange
		    ? QualifiedIdentifierAt(Source, FileLabel, Context.ReplaceLine, Context.ReplaceStartColumn)
		    : Directive.NamespaceName;
		const auto Targets = BindQueryNamespacePath(Program, Symbols, Name, Directive.ScopeNamespace);
		for (const auto& Target : Targets)
		{
			bool bFoundSymbol = false;
			for (const auto& Symbol : Symbols)
			{
				if (Symbol.Kind != EDocumentSymbolKind::Namespace || NormalizeNamespaceSpelling(Symbol.Name) != Target)
				{
					continue;
				}
				FSymbolQueryHit Hit;
				Hit.Kind = EDocumentSymbolKind::Namespace;
				Hit.Name = Target;
				Hit.Detail = Symbol.Detail;
				Hit.Documentation = Symbol.Documentation;
				Hit.FileLabel = Symbol.FileLabel;
				Hit.DefLine = Symbol.StartLine;
				Hit.DefColumn = Symbol.StartColumn;
				Hit.bSoftBuiltin = Symbol.bSoftBuiltin;
				OutHits.push_back(std::move(Hit));
				bFoundSymbol = true;
			}
			if (bFoundSymbol)
			{
				continue;
			}
			for (const FNamespaceDecl& NamespaceDecl : Program.Namespaces)
			{
				if (NormalizeNamespaceSpelling(NamespaceDecl.Name) != Target)
				{
					continue;
				}
				FSymbolQueryHit Hit;
				Hit.Kind = EDocumentSymbolKind::Namespace;
				Hit.Name = Target;
				Hit.Detail = "namespace";
				Hit.FileLabel = NamespaceDecl.ModulePath;
				Hit.DefLine = NamespaceDecl.Location.Line;
				Hit.DefColumn = NamespaceDecl.Location.Column;
				OutHits.push_back(std::move(Hit));
			}
		}
		return true;
	}
	return false;
}

std::string NormalizeNamespaceSpelling(const std::string_view Name)
{
	std::string Normalized(Name);
	for (std::size_t Separator = 0;
	    (Separator = Normalized.find("::", Separator)) != std::string::npos;)
	{
		Normalized.replace(Separator, 2, ".");
		++Separator;
	}
	return Normalized;
}

std::string QualifiedIdentifierAt(
    const std::string_view Source,
    const std::string_view FileLabel,
    const std::uint32_t Line,
    const std::uint32_t Column)
{
	FQueryLexer Lexer(Source, FileLabel);
	const std::vector<FToken>& Tokens = Lexer.Tokenize();
	const auto IsQualifiedComponent = [](const FToken& Token)
	{
		return Token.Kind == ETokenKind::Identifier || (Token.Kind == ETokenKind::Keyword && (VeroScript::IsTypeKeyword(Token.Lexeme) || VeroScript::IsContainerTypeKeyword(Token.Lexeme)));
	};
	for (std::size_t Index = QueryTokenLowerBound(Tokens, {Line, Column});
	    Index < Tokens.size(); ++Index)
	{
		if (!IsQualifiedComponent(Tokens[Index]) || Tokens[Index].Location.Line != Line || Tokens[Index].Location.Column != Column)
		{
			break;
		}
		std::size_t Begin = Index;
		while (Begin >= 2 && Tokens[Begin - 1].Kind == ETokenKind::Symbol && (Tokens[Begin - 1].Lexeme == "." || Tokens[Begin - 1].Lexeme == "::") && IsQualifiedComponent(Tokens[Begin - 2]))
		{
			Begin -= 2;
		}
		std::string Qualified;
		for (std::size_t Component = Begin; Component <= Index; Component += 2)
		{
			if (!Qualified.empty())
			{
				Qualified.push_back('.');
			}
			Qualified.append(Tokens[Component].Lexeme.data(), Tokens[Component].Lexeme.size());
		}
		if (Begin > 0 && Tokens[Begin - 1].Lexeme == "::")
		{
			Qualified.insert(Qualified.begin(), '.');
		}
		return Qualified;
	}
	return {};
}

/**
 * Resolve the namespace segment under the caret as a namespace symbol, not
 * merely as the left side of a qualified function/type name.  Candidate
 * paths are expanded lexically so `namespace A::B`, `A::B::Run`, and a
 * nested `namespace B` all land on the same fully-qualified declaration.
 */
bool CollectNamespaceQueryHits(
    const FProgram& Program,
    const std::vector<FDocumentSymbol>& Symbols,
    const std::string_view Source,
    const std::string_view FileLabel,
    const FTextPosition Position,
    const std::string_view ActiveNamespace,
    const std::string_view TokenText,
    const std::uint32_t TokenLine,
    const std::uint32_t TokenColumn,
    std::vector<FSymbolQueryHit>& OutHits)
{
	std::string Qualified = QualifiedIdentifierAt(
	    Source, FileLabel, TokenLine, TokenColumn);
	if (Qualified.empty())
	{
		Qualified.assign(TokenText.data(), TokenText.size());
	}
	if (Qualified.empty())
	{
		return false;
	}

	const auto Candidates = BindQueryQualifiedPath(Program, Symbols, NormalizeNamespaceSpelling(Qualified),
	    std::string(ActiveNamespace), Position);

	const auto AppendHit = [&OutHits](
	                           const std::string_view Name,
	                           const std::string_view Detail,
	                           const std::string_view Documentation,
	                           const std::string_view FileLabelValue,
	                           const std::uint32_t DefLine,
	                           const std::uint32_t DefColumn,
	                           const bool bSoftBuiltin)
	{
		FSymbolQueryHit Hit;
		Hit.Kind = EDocumentSymbolKind::Namespace;
		Hit.Name.assign(Name.data(), Name.size());
		const std::size_t Dot = Hit.Name.rfind('.');
		Hit.ContainerName = Dot == std::string::npos ? std::string{} : Hit.Name.substr(0, Dot);
		Hit.Detail.assign(Detail.data(), Detail.size());
		Hit.Documentation.assign(Documentation.data(), Documentation.size());
		Hit.FileLabel.assign(FileLabelValue.data(), FileLabelValue.size());
		Hit.DefLine = DefLine;
		Hit.DefColumn = DefColumn;
		Hit.bExported = true;
		Hit.bSoftBuiltin = bSoftBuiltin;
		OutHits.push_back(std::move(Hit));
	};

	for (const std::string& Candidate : Candidates)
	{
		for (const FDocumentSymbol& Symbol : Symbols)
		{
			if (Symbol.Kind != EDocumentSymbolKind::Namespace || NormalizeNamespaceSpelling(Symbol.Name) != Candidate)
			{
				continue;
			}
			AppendHit(
			    Candidate,
			    Symbol.Detail,
			    Symbol.Documentation,
			    Symbol.FileLabel,
			    Symbol.StartLine,
			    Symbol.StartColumn,
			    Symbol.bSoftBuiltin);
			return true;
		}

		for (const FNamespaceDecl& NamespaceDecl : Program.Namespaces)
		{
			const std::string Declared = NormalizeNamespaceSpelling(NamespaceDecl.Name);
			if (Declared != Candidate && (Declared.size() <= Candidate.size() || Declared.compare(0, Candidate.size(), Candidate) != 0 || Declared[Candidate.size()] != '.'))
			{
				continue;
			}
			AppendHit(
			    Candidate,
			    "namespace",
			    {},
			    NamespaceDecl.ModulePath,
			    NamespaceDecl.Location.Line,
			    NamespaceDecl.Location.Column,
			    false);
			return true;
		}
	}
	return false;
}

void AppendUsingNamespaceCompletions(
    const FProgram& Program,
    const std::vector<FDocumentSymbol>& Symbols,
    const std::string_view ActiveNamespace,
    const FTextPosition ReferencePosition,
    const std::string_view Prefix,
    std::unordered_map<std::string, FCompletionItem>& OutItems)
{
	for (const FUsingNamespaceDecl& Directive : Program.UsingNamespaces)
	{
		if (Directive.ModulePath != Program.EntryModulePath || (!Directive.ScopeNamespace.empty() && ActiveNamespace != Directive.ScopeNamespace && ActiveNamespace.rfind(Directive.ScopeNamespace + ".", 0) != 0) || ReferencePosition.Line < Directive.Location.Line || (ReferencePosition.Line == Directive.Location.Line && ReferencePosition.Column < Directive.Location.Column))
		{
			continue;
		}
		const auto NamespaceTargets = BindQueryNamespacePath(Program, Symbols, Directive.NamespaceName, Directive.ScopeNamespace);
		const std::string NamespacePrefix = NamespaceTargets.empty() ? std::string{} : NamespaceTargets.front() + ".";
		if (NamespacePrefix.empty())
		{
			continue;
		}
		for (const FDocumentSymbol& Symbol : Symbols)
		{
			const std::string NormalizedName =
			    NormalizeNamespaceSpelling(Symbol.Name);
			if (NormalizedName.rfind(NamespacePrefix, 0) != 0)
			{
				continue;
			}
			const std::string Remainder =
			    NormalizedName.substr(NamespacePrefix.size());
			const std::size_t Generic = Remainder.find('<');
			const std::string Leaf = Remainder.substr(0, Generic);
			if (Leaf.empty() || Leaf.find('.') != std::string::npos || !PrefixMatches(Leaf, Prefix))
			{
				continue;
			}
			FCompletionItem Item;
			Item.Kind = Symbol.Kind;
			Item.Label = Leaf;
			Item.Detail = Symbol.Detail;
			Item.Documentation = Symbol.Documentation;
			Item.InsertText = Leaf;
			Item.bSoftBuiltin = Symbol.bSoftBuiltin;
			if (Symbol.bSoftBuiltin && Symbol.Kind == EDocumentSymbolKind::Function)
			{
				const std::string_view Canonical = StandardLibrary::ResolveFunctionName(NormalizedName);
				if (!Canonical.empty() && !StandardLibrary::RequiredNamespaceForFunction(Canonical).empty())
				{
					// Namespace-only intrinsics require explicit qualification,
					// even in a scope with a using-directive.
					Item.InsertText = FormatNamespaceQualifiedName(NormalizedName);
				}
			}
			OutItems.insert_or_assign(Leaf, std::move(Item));

			if (Symbol.Kind == EDocumentSymbolKind::Enum && !Symbol.bScopedEnum)
			{
				for (const FDocumentSymbol& Child : Symbol.Children)
				{
					if (Child.Kind != EDocumentSymbolKind::EnumMember || !PrefixMatches(Child.Name, Prefix))
					{
						continue;
					}
					FCompletionItem ChildItem;
					ChildItem.Kind = Child.Kind;
					ChildItem.Label = Child.Name;
					ChildItem.Detail = Child.Detail;
					ChildItem.Documentation = Child.Documentation;
					ChildItem.InsertText = Child.Name;
					OutItems.insert_or_assign(Child.Name, std::move(ChildItem));
				}
			}
		}
	}
}

EDocumentSymbolKind HostTypeKindToDocumentKind(
    const VeroScript::EHostTypeKind Kind)
{
	switch (Kind)
	{
	case VeroScript::EHostTypeKind::Enum:
		return EDocumentSymbolKind::Enum;
	case VeroScript::EHostTypeKind::Struct:
		return EDocumentSymbolKind::Struct;
	case VeroScript::EHostTypeKind::Class:
		return EDocumentSymbolKind::Class;
	default:
		{
			break;
		}
	}
	return EDocumentSymbolKind::TypeAlias;
}

std::string FormatGenericHostTemplateDetail(
    const VeroScript::FGenericHostTypeTemplateDesc& Template)
{
	std::string Detail = "host generic type template ";
	Detail += Template.Name;
	Detail.push_back('<');
	for (std::size_t Index = 0; Index < Template.Arity; ++Index)
	{
		if (Index > 0)
		{
			Detail += ", ";
		}
		Detail.push_back('T');
		Detail += std::to_string(Index + 1);
	}
	Detail.push_back('>');
	return Detail;
}

void AppendGenericHostTemplateCompletions(
    const VeroScript::IHostBindingCatalog* const Bindings,
    const std::string_view Prefix,
    std::unordered_map<std::string, FCompletionItem>& OutItems)
{
	if (!Bindings)
	{
		return;
	}
	for (const VeroScript::FGenericHostTypeTemplateDesc& Template :
	    Bindings->GetGenericTypeTemplates())
	{
		if (!PrefixMatches(Template.Name, Prefix) || !Bindings->FindGenericTypeTemplate(Template.Name))
		{
			continue;
		}
		const auto Existing = OutItems.find(Template.Name);
		if (Existing != OutItems.end() && !Existing->second.bSoftBuiltin)
		{
			continue;
		}
		FCompletionItem Item;
		Item.Kind = EDocumentSymbolKind::TypeAlias;
		Item.Label = Template.Name;
		Item.Detail = FormatGenericHostTemplateDetail(Template);
		Item.InsertText = MakeGenericTypeSnippet(Template.Name, Template.Arity);
		Item.bInsertTextSnippet = true;
		Item.bSoftBuiltin = true;
		OutItems.insert_or_assign(Template.Name, std::move(Item));
	}
}

bool HostMemberCanBeNamedFromReceiver(
    const VeroScript::FHostMemberDesc& Member,
    const bool bTypeReceiver)
{
	if (Member.bOperator || Member.Kind == VeroScript::EHostMemberKind::Constructor || Member.Kind == VeroScript::EHostMemberKind::PropertySetter)
	{
		return false;
	}
	switch (Member.Kind)
	{
	case VeroScript::EHostMemberKind::StaticMethod:
		// The language permits static methods through either a type or instance.
		return true;
	case VeroScript::EHostMemberKind::StaticConstant:
	case VeroScript::EHostMemberKind::EnumMember:
		return bTypeReceiver;
	case VeroScript::EHostMemberKind::Method:
	case VeroScript::EHostMemberKind::Field:
	case VeroScript::EHostMemberKind::PropertyGetter:
		return !bTypeReceiver;
	default:
		return false;
	}
}

const VeroScript::FHostTypeDesc* FindHostTypeAlongHierarchy(
    const VeroScript::IHostBindingCatalog* const Bindings,
    const FProgram& Program,
    const std::string_view TypeName)
{
	if (!Bindings || TypeName.empty())
	{
		return nullptr;
	}
	if (const VeroScript::FHostTypeDesc* Direct = Bindings->FindType(TypeName))
	{
		return Direct;
	}
	std::vector<std::string> Hierarchy;
	CollectTypeHierarchy(Program, TypeName, Hierarchy);
	for (const std::string& Candidate : Hierarchy)
	{
		if (const VeroScript::FHostTypeDesc* Found = Bindings->FindType(Candidate))
		{
			return Found;
		}
	}
	return nullptr;
}

/**
 * Build physical-token classifications that a one-token symbol query cannot
 * recover on its own:
 * - namespace declaration path components (`namespace a::b`)
 * - every identifier component of a catalog/source qualified type
 *   (`EEndPlayReason::Type`, `game.math.Vector`)
 *
 * Classification is based on declarations/catalog entries, never spelling
 * or capitalization.
 */
void CollectCompositeSemanticKinds(
    const FProgram& Program,
    const std::vector<FToken>& Tokens,
    const std::vector<FDocumentSymbol>& Symbols,
    const std::string_view FileLabel,
    const VeroScript::IHostBindingCatalog* const Bindings,
    std::unordered_map<std::size_t, EDocumentSymbolKind>& OutKinds)
{
	// Use parser-confirmed namespace declarations rather than guessing from a
	// dotted symbol: `EEndPlayReason.Type` is a nested type, not a namespace.
	// The reserved `std` surface is generated as qualified soft symbols, so its
	// intermediate prefixes are safe to recover from `std.*` names.
	std::unordered_set<std::string> NamespaceNames;
	const auto AddNamespaceAndParents = [&NamespaceNames](std::string Name)
	{
		Name = NormalizeNamespaceSpelling(Name);
		for (;;)
		{
			if (!Name.empty())
			{
				NamespaceNames.insert(Name);
			}
			const std::size_t Dot = Name.rfind('.');
			if (Dot == std::string::npos)
			{
				break;
			}
			Name.resize(Dot);
		}
	};
	for (const FNamespaceDecl& NamespaceDecl : Program.Namespaces)
	{
		AddNamespaceAndParents(NamespaceDecl.Name);
	}
	for (const FDocumentSymbol& Symbol : Symbols)
	{
		const std::string Name = NormalizeNamespaceSpelling(
		    StripGenericArgs(Symbol.Name));
		if (Symbol.Kind == EDocumentSymbolKind::Namespace)
		{
			AddNamespaceAndParents(Name);
		}
		if (Name.rfind("std.", 0) == 0)
		{
			const std::size_t Leaf = Name.rfind('.');
			AddNamespaceAndParents(Name.substr(0, Leaf));
		}
	}

	std::unordered_set<std::string> LabelNames;
	std::unordered_set<std::uint64_t> LabelDeclarationPositions;
	const auto PositionKey = [](const std::uint32_t Line, const std::uint32_t Column)
	{
		return (static_cast<std::uint64_t>(Line) << 32u) | Column;
	};
	std::function<void(const std::vector<FDocumentSymbol>&)> CollectLabels;
	CollectLabels = [&](const std::vector<FDocumentSymbol>& Items)
	{
		for (const FDocumentSymbol& Symbol : Items)
		{
			if (Symbol.Kind == EDocumentSymbolKind::Label && (Symbol.FileLabel.empty() || Symbol.FileLabel == FileLabel))
			{
				LabelNames.insert(Symbol.Name);
				LabelDeclarationPositions.insert(
				    PositionKey(Symbol.StartLine, Symbol.StartColumn));
			}
			CollectLabels(Symbol.Children);
		}
	};
	CollectLabels(Symbols);

	for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
	{
		if (Tokens[Index].Kind == ETokenKind::Identifier && LabelNames.find(std::string(Tokens[Index].Lexeme)) != LabelNames.end() && (LabelDeclarationPositions.find(PositionKey(Tokens[Index].Location.Line, Tokens[Index].Location.Column)) != LabelDeclarationPositions.end() || (Index > 0 && Tokens[Index - 1].Lexeme == "goto")))
		{
			OutKinds[Index] = EDocumentSymbolKind::Label;
		}

		if (Tokens[Index].Lexeme != "namespace")
		{
			continue;
		}
		for (std::size_t NameIndex = Index + 1; NameIndex < Tokens.size();)
		{
			if (Tokens[NameIndex].Kind != ETokenKind::Identifier)
			{
				break;
			}
			OutKinds[NameIndex] = EDocumentSymbolKind::Namespace;
			if (NameIndex + 2 >= Tokens.size() || (Tokens[NameIndex + 1].Lexeme != "::" && Tokens[NameIndex + 1].Lexeme != ".") || Tokens[NameIndex + 2].Kind != ETokenKind::Identifier)
			{
				break;
			}
			NameIndex += 2;
		}
	}

	for (std::size_t Start = 0; Start < Tokens.size(); ++Start)
	{
		if (Tokens[Start].Kind != ETokenKind::Identifier)
		{
			continue;
		}

		std::string Qualified(Tokens[Start].Lexeme);
		std::string SourceQualified = Qualified;
		std::vector<std::size_t> Components{Start};
		if (NamespaceNames.find(Qualified) != NamespaceNames.end())
		{
			OutKinds[Start] = EDocumentSymbolKind::Namespace;
		}
		const auto IsQualifiedTypeComponent = [](const FToken& Token)
		{
			return Token.Kind == ETokenKind::Identifier || (Token.Kind == ETokenKind::Keyword && (VeroScript::IsTypeKeyword(Token.Lexeme) || VeroScript::IsContainerTypeKeyword(Token.Lexeme)));
		};
		for (std::size_t Cursor = Start + 1;
		    Cursor + 1 < Tokens.size() && (Tokens[Cursor].Lexeme == "::" || Tokens[Cursor].Lexeme == ".") && IsQualifiedTypeComponent(Tokens[Cursor + 1]);
		    Cursor += 2)
		{
			SourceQualified.append(Tokens[Cursor].Lexeme);
			SourceQualified.append(Tokens[Cursor + 1].Lexeme);
			Qualified.push_back('.');
			Qualified.append(Tokens[Cursor + 1].Lexeme);
			Components.push_back(Cursor + 1);
			if (NamespaceNames.find(Qualified) != NamespaceNames.end())
			{
				OutKinds[Cursor + 1] = EDocumentSymbolKind::Namespace;
			}

			EDocumentSymbolKind Kind = EDocumentSymbolKind::TypeAlias;
			bool bKnownType = false;
			if (const FDocumentSymbol* Symbol = FindTypeSymbol(Symbols, Qualified))
			{
				Kind = Symbol->Kind;
				bKnownType = true;
			}
			else if (Bindings)
			{
				const VeroScript::FHostTypeDesc* HostType =
				    Bindings->FindType(SourceQualified);
				if (!HostType)
				{
					HostType = Bindings->FindType(Qualified);
				}
				if (HostType)
				{
					Kind = HostTypeKindToDocumentKind(HostType->Kind);
					bKnownType = true;
				}
			}
			if (bKnownType)
			{
				for (const std::size_t Component : Components)
				{
					// A namespace declaration path remains a namespace even
					// when a same-spelled type exists in the visible catalog.
					if (const auto Existing = OutKinds.find(Component);
					    Existing == OutKinds.end() || Existing->second != EDocumentSymbolKind::Namespace)
					{
						OutKinds[Component] = Kind;
					}
				}
			}
		}
	}
}

bool MacroExpandsToKnownType(
    const VeroScript::FPreprocessMacroDef& Macro,
    const std::vector<FDocumentSymbol>& Symbols,
    const VeroScript::IHostBindingCatalog* const Bindings)
{
	if (Macro.bFunctionLike)
	{
		return false;
	}
	std::string_view Replacement = Macro.Body;
	while (!Replacement.empty() && std::isspace(static_cast<unsigned char>(Replacement.front())))
	{
		Replacement.remove_prefix(1);
	}
	const std::size_t End = Replacement.find_first_of(" \t\r\n<*&");
	return IsKnownTypeName(Replacement.substr(0, End), Symbols, Bindings);
}

FSymbolQueryResult Fail(FScriptError Error)
{
	FSymbolQueryResult Result;
	Result.Error = std::move(Error);
	Result.Diagnostics.push_back(Result.Error);
	return Result;
}

FSymbolQueryResult FailFromLoader(const VeroScriptModuleLoader& Loader)
{
	FSymbolQueryResult Result;
	Result.Error = Loader.GetError();
	Result.Diagnostics = Loader.GetDiagnostics();
	if (Result.Diagnostics.empty())
	{
		Result.Diagnostics.push_back(Result.Error);
	}
	return Result;
}

FCompletionResult FailCompletion(FScriptError Error)
{
	FCompletionResult Result;
	Result.Error = std::move(Error);
	Result.Diagnostics.push_back(Result.Error);
	return Result;
}

FCompletionResult FailCompletionFromLoader(const VeroScriptModuleLoader& Loader)
{
	FCompletionResult Result;
	Result.Error = Loader.GetError();
	Result.Diagnostics = Loader.GetDiagnostics();
	if (Result.Diagnostics.empty())
	{
		Result.Diagnostics.push_back(Result.Error);
	}
	return Result;
}
