// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		void PreferExactOrQualifiedHits(
			const std::string_view Ident,
			std::vector<FSymbolQueryHit>& Hits)
		{
			if (Hits.size() <= 1 || Ident.empty())
			{
				return;
			}
			std::vector<FSymbolQueryHit> Exact;
			for (const FSymbolQueryHit& Hit : Hits)
			{
				if (StripGenericArgs(Hit.Name) == Ident)
				{
					Exact.push_back(Hit);
				}
			}
			if (!Exact.empty())
			{
				Hits = std::move(Exact);
			}
		}

		void PreferVisibleNamespaceHits(
			const FProgram& Program,
			const std::vector<FDocumentSymbol>& Symbols,
			const std::string_view ActiveNamespace,
			const FTextPosition Position,
			const std::string_view Ident,
			std::vector<FSymbolQueryHit>& Hits)
		{
			if (Ident.empty() || Hits.empty())
			{
				return;
			}
			if (Ident.find('.') != std::string_view::npos || Ident.find("::") != std::string_view::npos)
			{
				const auto Normalized = NormalizeNamespaceSpelling(Ident);
				const auto Targets = BindQueryQualifiedPath(Program, Symbols,
				    Ident.rfind("::", 0) == 0 ? "::" + Normalized.substr(1) : Normalized, std::string(ActiveNamespace), Position);
				if (!Targets.empty())
				{
					Hits.erase(std::remove_if(Hits.begin(), Hits.end(), [&](const FSymbolQueryHit& Hit)
					{
						return Hit.ContainerName.empty() && std::find(Targets.begin(), Targets.end(),
						    NormalizeNamespaceSpelling(StripGenericArgs(Hit.Name))) == Targets.end();
					}), Hits.end());
				}
				return;
			}
			const auto MatchTopLevel = [&](const std::string& Candidate)
			{
				std::vector<FSymbolQueryHit> Matches;
				for (const FSymbolQueryHit& Hit : Hits)
				{
					if (Hit.ContainerName.empty()
						&& NormalizeNamespaceSpelling(StripGenericArgs(Hit.Name)) == Candidate)
					{
						Matches.push_back(Hit);
					}
				}
				return Matches;
			};

			std::string Scope(ActiveNamespace);
			for (;;)
			{
				const std::string Candidate = Scope.empty()
					? std::string(Ident)
					: Scope + "." + std::string(Ident);
				std::vector<FSymbolQueryHit> Matches = MatchTopLevel(Candidate);
				if (!Matches.empty())
				{
					Hits = std::move(Matches);
					return;
				}
				if (Scope.empty())
				{
					break;
				}
				const std::size_t Dot = Scope.rfind('.');
				Scope = Dot == std::string::npos ? std::string{} : Scope.substr(0, Dot);
			}

			std::vector<FSymbolQueryHit> UsingMatches;
			for (const FUsingNamespaceDecl& Directive : Program.UsingNamespaces)
			{
				if (Directive.ModulePath != Program.EntryModulePath
					|| (!Directive.ScopeNamespace.empty()
						&& ActiveNamespace != Directive.ScopeNamespace
						&& ActiveNamespace.rfind(Directive.ScopeNamespace + ".", 0) != 0)
					|| Position.Line < Directive.Location.Line
					|| (Position.Line == Directive.Location.Line
						&& Position.Column < Directive.Location.Column))
				{
					continue;
				}
				for (const auto& Target : BindQueryNamespacePath(Program, Symbols, Directive.NamespaceName, Directive.ScopeNamespace))
				{
					auto Matches = MatchTopLevel(Target + "." + std::string(Ident));
					UsingMatches.insert(UsingMatches.end(), Matches.begin(), Matches.end());
				}
			}
			if (!UsingMatches.empty())
			{
				Hits = std::move(UsingMatches);
				return;
			}

			// No lexical or using-directive target owns this bare name. Remove only
			// namespace-qualified top-level false positives; member lookup is handled
			// independently below for implicit `this` and explicit receivers.
			Hits.erase(
				std::remove_if(Hits.begin(), Hits.end(), [](const FSymbolQueryHit& Hit)
				{
					if (!Hit.ContainerName.empty())
					{
						return false;
					}
					const std::string Name = StripGenericArgs(Hit.Name);
					return Name.find('.') != std::string::npos
							|| Name.find("::") != std::string::npos;
				}),
				Hits.end());
		}

		/**
		 * Outline constructors reuse the type name. When both the type and its constructors
		 * appear in a query, keep the caret's intended symbol:
		 * - caret on a constructor decl → constructors only
		 * - otherwise (type decl / use-site) → type only
		 */
		void CollapseSameNamedTypeConstructors(
			std::vector<FSymbolQueryHit>& Hits,
			const std::uint32_t TokenLine,
			const std::uint32_t TokenColumn)
		{
			if (Hits.size() <= 1 || TokenLine == 0)
			{
				return;
			}
			bool bHasType = false;
			bool bHasConstructor = false;
			bool bCaretOnConstructor = false;
			for (const FSymbolQueryHit& Hit : Hits)
			{
				if (IsTypeSymbolKind(Hit.Kind))
				{
					bHasType = true;
				}
				else if (Hit.Kind == EDocumentSymbolKind::Constructor)
				{
					bHasConstructor = true;
					if (Hit.DefLine == TokenLine && Hit.DefColumn == TokenColumn)
					{
						bCaretOnConstructor = true;
					}
				}
			}
			if (!bHasType || !bHasConstructor)
			{
				return;
			}

			std::unordered_set<std::string> TypeNames;
			for (const FSymbolQueryHit& Hit : Hits)
			{
				if (IsTypeSymbolKind(Hit.Kind))
				{
					TypeNames.insert(StripGenericArgs(Hit.Name));
				}
			}

			std::vector<FSymbolQueryHit> Filtered;
			Filtered.reserve(Hits.size());
			for (FSymbolQueryHit& Hit : Hits)
			{
				const std::string Name = StripGenericArgs(Hit.Name);
				const std::string Container = StripGenericArgs(Hit.ContainerName);
				const bool bCtorOfListedType =
					Hit.Kind == EDocumentSymbolKind::Constructor
						&& TypeNames.find(Name) != TypeNames.end()
						&& (Container.empty() || Container == Name
							|| TypeNames.find(Container) != TypeNames.end());
				const bool bTypeOfListedCtor =
					IsTypeSymbolKind(Hit.Kind) && TypeNames.find(Name) != TypeNames.end();
				if (bCaretOnConstructor)
				{
					if (bTypeOfListedCtor)
					{
						continue;
					}
				}
				else if (bCtorOfListedType)
				{
					continue;
				}
				Filtered.push_back(std::move(Hit));
			}
			if (!Filtered.empty())
			{
				Hits = std::move(Filtered);
			}
		}

		void PreferHostFormatVarargsHits(
			const std::string_view Ident,
			std::vector<FSymbolQueryHit>& Hits)
		{
			if (Hits.size() <= 1
				|| !IsHostFormatFamilyName(Ident))
			{
				return;
			}
			std::vector<FSymbolQueryHit> SoftVarargs;
			std::vector<FSymbolQueryHit> Others;
			for (FSymbolQueryHit& Hit : Hits)
			{
				if (Hit.bSoftBuiltin && Hit.Detail.find("...") != std::string::npos)
				{
					SoftVarargs.push_back(std::move(Hit));
				}
				else
				{
					Others.push_back(std::move(Hit));
				}
			}
			if (SoftVarargs.empty())
			{
				Hits = std::move(Others);
				return;
			}
			Hits.clear();
			Hits.reserve(SoftVarargs.size() + Others.size());
			for (FSymbolQueryHit& Hit : SoftVarargs)
			{
				Hits.push_back(std::move(Hit));
			}
			for (FSymbolQueryHit& Hit : Others)
			{
				Hits.push_back(std::move(Hit));
			}
		}

		std::string ResolveReceiverTypeName(
			const FProgram& Program,
			const FDocumentSymbolResult& SymbolsResult,
			const FTextPosition Position,
			const std::string& ReceiverName,
			const std::vector<FSymbolQueryHit>* PreLocals)
		{
			if (ReceiverName == "this")
			{
				std::vector<FCallableScope> Callables;
				CollectCallables(Program, Callables);
				AssignCallableEndLines(Callables);
				if (const FCallableScope* Scope = FindEnclosingCallable(Callables, Position))
				{
					if (!Scope->ContainerName.empty())
					{
						return Scope->ContainerName;
					}
				}
				return {};
			}

			std::vector<FSymbolQueryHit> OwnedLocals;
			const std::vector<FSymbolQueryHit>* LocalsPtr = PreLocals;
			if (!LocalsPtr)
			{
				CollectVisibleLocals(Program, Position, OwnedLocals);
				LocalsPtr = &OwnedLocals;
			}
			for (const FSymbolQueryHit& Local : *LocalsPtr)
			{
				if (Local.Name != ReceiverName)
				{
					continue;
				}
				if (Local.Kind == EDocumentSymbolKind::TypeAlias)
				{
					return ResolveTypeNameWithLocals(Program, *LocalsPtr, Local.Detail);
				}
				if (TypeNameFromDetail(Local.Detail) == "auto" && !Local.InitializerRange.SourceId.empty())
				{
					const auto* Map = ProgramPreprocessorLineMap(Program, Local.InitializerRange.SourceId);
					if (Map && Local.InitializerRange.Begin < Local.InitializerRange.End &&
						Local.InitializerRange.End <= Map->PreprocessedSource.size())
					{
						const auto Expression = std::string_view(Map->PreprocessedSource).substr(Local.InitializerRange.Begin,
							Local.InitializerRange.End - Local.InitializerRange.Begin);
						const FTextPosition InitializerPosition{Local.InitializerRange.Position.Begin.Line,
							Local.InitializerRange.Position.Begin.Column};
						const auto Inferred = ResolveReceiverExpressionType(Program, SymbolsResult, InitializerPosition, Expression, nullptr,
							VeroScriptSymbolQuery::ResolveEnclosingNamespaceName(Program, InitializerPosition,
								FindActiveNamespaceAt(Map->PreprocessedSource, Local.InitializerRange.SourceId, InitializerPosition)));
						if (!Inferred.empty()) return Inferred;
					}
				}
				return ResolveTypeNameWithLocals(
					Program,
						*LocalsPtr,
					TypeNameFromDetail(Local.Detail));
			}
			// Bare `Super` (not shadowed by a local/field) → first script/host base.
			if (ReceiverName == "Super")
			{
				std::vector<FCallableScope> Callables;
				CollectCallables(Program, Callables);
				AssignCallableEndLines(Callables);
				const FCallableScope* Scope = FindEnclosingCallable(Callables, Position);
				if (!Scope || Scope->ContainerName.empty())
				{
					return {};
				}
				std::vector<std::string> Hierarchy;
				CollectTypeHierarchy(Program, Scope->ContainerName, Hierarchy);
				for (std::size_t Index = 1; Index < Hierarchy.size(); ++Index)
				{
					if (!Hierarchy[Index].empty()
						&& Hierarchy[Index] != Scope->ContainerName)
					{
						return Hierarchy[Index];
					}
				}
				return {};
			}
			const auto Owner = ResolveImplicitThisOwnerName(Program, Position);
			if (!Owner.empty())
			{
				FCallableAccessContext CallableContext;
				const auto* CallableAccess = VeroScriptSymbolQuery::ResolveEnclosingCallableAccessContext(
					Program, Position, CallableContext) ? &CallableContext : nullptr;
				std::unordered_map<std::string, FInheritedMemberCompletion> Fields;
				AppendCompletionMembersAlongHierarchy(Program, SymbolsResult.Symbols, Owner, ReceiverName,
					Fields, Owner, CallableAccess, true);
				for (const auto& [Key, Field] : Fields)
				{
					(void)Key;
					if (Field.Label == ReceiverName && Field.Kind == EDocumentSymbolKind::Field)
					{
						return TypeNameFromDetail(Field.ValueType);
					}
				}
			}
			if (const FDocumentSymbol* TypeSymbol =
				FindTypeSymbol(SymbolsResult.Symbols, ReceiverName))
			{
				if (TypeSymbol->Kind == EDocumentSymbolKind::TypeAlias)
				{
					return ResolveTypeNameWithLocals(Program, *LocalsPtr, TypeSymbol->Detail);
				}
				return CompletionLabel(TypeSymbol->Name);
			}
			return ResolveTypeNameWithLocals(Program, *LocalsPtr, ReceiverName);
		}

		std::string ResolveQualifiedCtorTypeName(
			const FProgram& Program,
			const FDocumentSymbolResult& SymbolsResult,
			const std::string& ReceiverName,
			const std::string& CalleeName)
		{
			if (CalleeName.empty())
			{
				return {};
			}
			const std::string CtorType = ReceiverName.empty()
				? CalleeName
				: ReceiverName + "." + CalleeName;
			if (const FDocumentSymbol* TypeSymbol =
				FindTypeSymbol(SymbolsResult.Symbols, CtorType))
			{
				if (TypeSymbol->Kind == EDocumentSymbolKind::TypeAlias)
				{
					std::vector<FSymbolQueryHit> EmptyLocals;
					return ResolveTypeNameWithLocals(Program, EmptyLocals, TypeSymbol->Detail);
				}
				return CompletionLabel(TypeSymbol->Name);
			}
			return {};
		}

		bool LooksLikeLambdaCaptureOpen(
			const std::vector<FToken>& Tokens,
			const std::size_t OpenIndex)
		{
			if (OpenIndex >= Tokens.size()
				|| Tokens[OpenIndex].Kind != ETokenKind::Symbol
				|| Tokens[OpenIndex].Lexeme != "[")
			{
				return false;
			}
			if (OpenIndex == 0)
			{
				return true;
			}
			const FToken& Previous = Tokens[OpenIndex - 1];
			// `xs[i]` index access — not a capture list.
			if (Previous.Kind == ETokenKind::Identifier
				|| (Previous.Kind == ETokenKind::Keyword && Previous.Lexeme == "this")
				|| (Previous.Kind == ETokenKind::Symbol
					&& (Previous.Lexeme == ")" || Previous.Lexeme == "]" || Previous.Lexeme == ">")))
			{
				return false;
			}
			return true;
		}

		bool FindCaptureCompletionContext(
			const std::string_view Source,
			const std::string_view FileLabel,
			const FTextPosition Position,
			FCaptureCompletionContext& Out)
		{
			Out = {};
			FQueryLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			// Tolerate later lex errors (tokens before the fault remain valid).

			for (std::size_t OpenIndex = 0; OpenIndex < Tokens.size(); ++OpenIndex)
			{
				if (!LooksLikeLambdaCaptureOpen(Tokens, OpenIndex))
				{
					continue;
				}

				const FToken& Open = Tokens[OpenIndex];
				const std::uint32_t AfterOpen =
					Open.Location.Column + static_cast<std::uint32_t>(Open.Lexeme.size());
				if (Position.Line < Open.Location.Line
					|| (Position.Line == Open.Location.Line && Position.Column < AfterOpen))
				{
					continue;
				}

				std::size_t CloseIndex = Tokens.size();
				int Depth = 0;
				bool bFoundClose = false;
				for (std::size_t Index = OpenIndex; Index < Tokens.size(); ++Index)
				{
					const FToken& Token = Tokens[Index];
					if (Token.Kind != ETokenKind::Symbol)
					{
						continue;
					}
					if (Token.Lexeme == "[")
					{
						++Depth;
					}
					else if (Token.Lexeme == "]")
					{
						--Depth;
						if (Depth == 0)
						{
							CloseIndex = Index;
							bFoundClose = true;
							break;
						}
					}
				}

				if (bFoundClose)
				{
					const FToken& Close = Tokens[CloseIndex];
					if (Position.Line > Close.Location.Line
						|| (Position.Line == Close.Location.Line
							&& Position.Column > Close.Location.Column))
					{
						continue;
					}
					if (CloseIndex + 1 >= Tokens.size()
						|| Tokens[CloseIndex + 1].Kind != ETokenKind::Symbol
						|| Tokens[CloseIndex + 1].Lexeme != "(")
					{
						continue;
					}
				}
				else
				{
					// Incomplete capture list — stop if caret walked into a new statement.
					bool bPastTerminator = false;
					for (std::size_t Index = OpenIndex + 1; Index < Tokens.size(); ++Index)
					{
						const FToken& Token = Tokens[Index];
						if (Token.Location.Line > Position.Line
							|| (Token.Location.Line == Position.Line
								&& Token.Location.Column >= Position.Column))
						{
							break;
						}
						if (Token.Kind == ETokenKind::Symbol
							&& (Token.Lexeme == ";" || Token.Lexeme == "{" || Token.Lexeme == "}"))
						{
							bPastTerminator = true;
							break;
						}
					}
					if (bPastTerminator)
					{
						continue;
					}
				}

				for (std::size_t Index = OpenIndex + 1;
					Index < (bFoundClose ? CloseIndex : Tokens.size());
					++Index)
				{
					const FToken& Token = Tokens[Index];
					if ((Token.Kind == ETokenKind::Identifier
							|| (Token.Kind == ETokenKind::Symbol
								&& (Token.Lexeme == "=" || Token.Lexeme == "&")))
							&& TokenCoversPositionOrEnd(Token, Position))
					{
						const bool bReferenceName =
							Token.Kind == ETokenKind::Identifier
							&& Index > OpenIndex + 1
							&& Tokens[Index - 1].Kind == ETokenKind::Symbol
							&& Tokens[Index - 1].Lexeme == "&";
						const FToken& ReplaceStart = bReferenceName ? Tokens[Index - 1] : Token;
						Out.bHasReplaceRange = true;
						Out.ReplaceLine = Token.Location.Line;
						Out.ReplaceStartColumn = ReplaceStart.Location.Column;
						Out.ReplaceEndColumn =
							Token.Location.Column
								+ static_cast<std::uint32_t>(Token.Lexeme.size());
						const std::uint32_t Relative =
							Position.Column >= Token.Location.Column
							? (Position.Column - Token.Location.Column)
							: 0;
						const std::size_t PrefixLen =
							Relative <= Token.Lexeme.size() ? Relative : Token.Lexeme.size();
						Out.Prefix = bReferenceName ? "&" : "";
						Out.Prefix += std::string(Token.Lexeme.substr(0, PrefixLen));
						Out.InsertLine = Out.ReplaceLine;
						Out.InsertColumn = Out.ReplaceEndColumn;
						return true;
					}
				}

				Out.Prefix.clear();
				Out.bHasReplaceRange = false;
				Out.InsertLine = Position.Line;
				Out.InsertColumn = Position.Column;
				return true;
			}
			return false;
		}

		bool FindImportPathCompletionContext(
			const std::string_view Source,
			const FTextPosition Position,
			FImportPathCompletionContext& Out)
		{
			Out = {};
			if (ActiveQueryLexicalSnapshot && ActiveQueryLexicalSnapshot->MatchesSource(Source))
			{
				const auto& Tokens = ActiveQueryLexicalSnapshot->Lexer.Tokenize();
				const auto Index = QueryTokenLowerBound(Tokens, Position);
				if (Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Identifier
					&& Tokens[Index].Location.Line == Position.Line && Tokens[Index].Location.Column == Position.Column)
				{
					return false;
				}
			}
			std::size_t Caret = 0, LiteralStart = std::string_view::npos;
			if (!FindSourceOffset(Source, Position.Line, Position.Column, Caret)) return false;
			std::size_t LineStart = Caret;
			while (LineStart && Source[LineStart - 1] != '\n')
			{
				--LineStart;
			}

			// Most point queries (including each semantic-token query) are names.
			// Avoid rescanning the entire source for those single-line code sites.
			if (Source.substr(LineStart, Caret - LineStart).find('"') == std::string_view::npos) return false;
			const auto Region = CompletionRegionAt(Source, Caret, &LiteralStart);
			if ((Region != ECompletionLexicalRegion::String &&
					Region != ECompletionLexicalRegion::RawString) ||
				LiteralStart == std::string_view::npos)
			{
				return false;
			}

			// Lex the complete header, so comments, newlines and earlier imports
			// cannot be mistaken for the path at the caret. The literal itself is
			// excluded to support a path whose closing quote has not been typed.
			VeroScriptLexer HeaderLexer(Source.substr(0, LiteralStart));
			const auto& Header = HeaderLexer.Tokenize();
			if (HeaderLexer.HasError()) return false;
			std::size_t Count = Header.size();
			if (Count && Header.back().Kind == ETokenKind::EndOfFile)
			{
				--Count;
			}

			if (!Count) return false;
			std::size_t Import = Count - 1;
			if (Header[Import].Lexeme == "compiler")
			{
				if (!Import) return false;
				--Import;
				Out.bCompilerExtension = true;
			}
			if (Header[Import].Kind == ETokenKind::Keyword && Header[Import].Lexeme == "import")
			{
				Out.bReExport = Import && Header[Import - 1].Lexeme == "export";
				if (Out.bCompilerExtension)
				{
					// Only the initial sequence of compiler imports is legal in the
					// core. A compiler directive after business code is not a path site.
					for (std::size_t I = 0; I < Import; I += 4)
					{
						std::string Decoded;
						if (I + 4 > Import || Header[I].Lexeme != "import" ||
							Header[I + 1].Lexeme != "compiler" ||
							Header[I + 2].Kind != ETokenKind::StringLiteral || Header[I + 3].Lexeme != ";" ||
							!DecodeImportPathLiteral(Header[I + 2].Lexeme, true, Decoded))
						{
							return false;
						}
					}
					if (Out.bReExport || Region == ECompletionLexicalRegion::RawString) return false;
				}
			}
			else if (!Out.bCompilerExtension && Import && Header[Import].Lexeme == "include" &&
				Header[Import - 1].Lexeme == "#" && Header[Import - 1].Location.Line == Position.Line)
			{
				Out.bReExport = true;
			}
			else
			{
				return false;
			}

			Out.bRawString = Region == ECompletionLexicalRegion::RawString;
			const std::size_t ContentStart = LiteralStart + (Out.bRawString ? 2 : 1);
			// Completion edits use a single line range. Do not flatten a multiline
			// raw path; document links retain its complete multiline range instead.
			if (ContentStart < LineStart || Caret < ContentStart) return false;
			std::size_t ContentEnd = ContentStart;
			bool Closed = false;
			while (ContentEnd < Source.size())
			{
				if ((ContentEnd & 255u) == 0)
				{
					CheckAnalysisCancellation();
				}

				const char Ch = Source[ContentEnd];
				if (Ch == '"')
				{
					Closed = true;
					break;
				}
				if (Ch == '\n' || Ch == '\r')
				{
					if (Out.bRawString ||
						(Out.bCompilerExtension && Ch == '\r' &&
							(ContentEnd + 1 == Source.size() || Source[ContentEnd + 1] != '\n')))
					{
						return false;
					}

					break;
				}
				if (!Out.bRawString && Ch == '\\' && ContentEnd + 1 < Source.size())
				{
					ContentEnd += 2;
					continue;
				}
				++ContentEnd;
			}
			if (Caret > ContentEnd) return false;
			const std::string Prefix(Source.substr(ContentStart, Caret - ContentStart));
			Out.SourcePrefix = Prefix;
			if (!Prefix.empty() &&
				!DecodeImportPathLiteral(
					(Out.bRawString ? "r\"" : "\"") + Prefix + "\"", Out.bCompilerExtension, Out.Prefix))
			{
				return false;
			}

			Out.PathText =
				Closed ? std::string(Source.substr(ContentStart, ContentEnd - ContentStart)) : Prefix;
			Out.bHasReplaceRange = true;
			Out.ReplaceLine = Position.Line;
			Out.ReplaceStartColumn = static_cast<std::uint32_t>(ContentStart - LineStart + 1);
			Out.ReplaceEndColumn = static_cast<std::uint32_t>(ContentEnd - LineStart + 1);
			return true;
		}

		// Physical line ends with `\` continuation (CRLF-safe), matching the preprocessor.
		bool PhysicalLineEndsWithContinuation(
			const std::string_view Source,
			const std::size_t LineBegin,
			const std::size_t LineEnd)
		{
			std::size_t End = LineEnd;
			if (End > LineBegin && Source[End - 1] == '\r')
			{
				--End;
			}
			return End > LineBegin && Source[End - 1] == '\\';
		}

		struct FPreprocessorLogicalPiece
		{
			std::uint32_t PhysLine = 1;
			std::size_t PhysBegin = 0;
			std::size_t LogicalBegin = 0;
			std::size_t LogicalEnd = 0;
		};

		/**
		 * Join `\`-continued physical lines that form one preprocessor directive.
		 * Returns false when the caret is not inside a `#` directive span.
		 */
		bool TryBuildPreprocessorLogicalLine(
			const std::string_view Source,
			const FTextPosition Position,
			std::string& OutLogical,
			std::size_t& OutLogicalCaret,
			std::vector<FPreprocessorLogicalPiece>& OutPieces)
		{
			OutLogical.clear();
			OutPieces.clear();
			OutLogicalCaret = 0;

			std::size_t Caret = 0;
			if (!FindSourceOffset(Source, Position.Line, Position.Column, Caret))
			{
				return false;
			}

			std::size_t LineStart = Caret;
			while (LineStart > 0 && Source[LineStart - 1] != '\n')
			{
				--LineStart;
			}

			// Walk back across continued predecessors.
			for (;;)
			{
				if (LineStart == 0)
				{
					break;
				}
				const std::size_t PrevEnd = LineStart - 1; // '\n'
				std::size_t PrevStart = PrevEnd;
				while (PrevStart > 0 && Source[PrevStart - 1] != '\n')
				{
					--PrevStart;
				}
				if (!PhysicalLineEndsWithContinuation(Source, PrevStart, PrevEnd))
				{
					break;
				}
				LineStart = PrevStart;
			}

			// Emit the joined group starting at LineStart.
			std::size_t Index = LineStart;
			std::uint32_t PhysLine = 1;
			{
				std::uint32_t Line = 1;
				for (std::size_t Scan = 0; Scan < LineStart; ++Scan)
				{
					if (Source[Scan] == '\n')
					{
						++Line;
					}
				}
				PhysLine = Line;
			}

			bool bSawCaretLine = false;
			for (;;)
			{
				const std::size_t PhysBegin = Index;
				while (Index < Source.size() && Source[Index] != '\n')
				{
					++Index;
				}
				const std::size_t PhysEnd = Index;
				const bool bHasNewline = Index < Source.size() && Source[Index] == '\n';
				std::size_t ContentEnd = PhysEnd;
				if (ContentEnd > PhysBegin && Source[ContentEnd - 1] == '\r')
				{
					--ContentEnd;
				}
				const bool bContinues =
					ContentEnd > PhysBegin && Source[ContentEnd - 1] == '\\';
				if (bContinues)
				{
					--ContentEnd;
				}

				FPreprocessorLogicalPiece Piece;
				Piece.PhysLine = PhysLine;
				Piece.PhysBegin = PhysBegin;
				Piece.LogicalBegin = OutLogical.size();
				OutLogical.append(Source.data() + PhysBegin, ContentEnd - PhysBegin);
				Piece.LogicalEnd = OutLogical.size();
				OutPieces.push_back(Piece);

				if (PhysLine == Position.Line)
				{
					bSawCaretLine = true;
					const std::uint32_t Col = Position.Column;
					const std::size_t ContentLen = ContentEnd - PhysBegin;
					const std::size_t Rel =
						Col == 0 ? 0
						: (static_cast<std::size_t>(Col - 1) < ContentLen
							? static_cast<std::size_t>(Col - 1)
							: ContentLen);
					OutLogicalCaret = Piece.LogicalBegin + Rel;
				}

				if (bHasNewline)
				{
					++Index;
				}
				++PhysLine;
				if (!bContinues)
				{
					break;
				}
				if (!bHasNewline)
				{
					break;
				}
			}

			if (!bSawCaretLine || OutPieces.empty())
			{
				return false;
			}

			std::size_t Probe = 0;
			while (Probe < OutLogical.size()
				&& (OutLogical[Probe] == ' ' || OutLogical[Probe] == '\t'))
			{
				++Probe;
			}
			return Probe < OutLogical.size() && OutLogical[Probe] == '#';
		}

		bool MapLogicalOffsetToPhysical(
			const std::vector<FPreprocessorLogicalPiece>& Pieces,
			const std::size_t LogicalOffset,
			const bool bEndExclusive,
			std::uint32_t& OutLine,
			std::uint32_t& OutColumn)
		{
			if (Pieces.empty())
			{
				return false;
			}
			for (const FPreprocessorLogicalPiece& Piece : Pieces)
			{
				if (bEndExclusive)
				{
					if (LogicalOffset > Piece.LogicalBegin && LogicalOffset <= Piece.LogicalEnd)
					{
						OutLine = Piece.PhysLine;
						OutColumn = static_cast<std::uint32_t>(
							LogicalOffset - Piece.LogicalBegin)
								+
							1;
						return true;
					}
				}
				else if (LogicalOffset >= Piece.LogicalBegin && LogicalOffset < Piece.LogicalEnd)
				{
					OutLine = Piece.PhysLine;
					OutColumn = static_cast<std::uint32_t>(
						LogicalOffset - Piece.LogicalBegin)
							+
						1;
					return true;
				}
			}
			if (!bEndExclusive && LogicalOffset == Pieces.back().LogicalEnd)
			{
				OutLine = Pieces.back().PhysLine;
				OutColumn = static_cast<std::uint32_t>(
					Pieces.back().LogicalEnd - Pieces.back().LogicalBegin)
						+
					1;
				return true;
			}
			if (bEndExclusive && LogicalOffset == 0)
			{
				OutLine = Pieces.front().PhysLine;
				OutColumn = 1;
				return true;
			}
			return false;
		}

		// True when the caret sits on a `#` directive line or its `\` continuation.
		bool IsHashDirectiveLine(const std::string_view Source, const FTextPosition Position)
		{
			std::string Logical;
			std::size_t LogicalCaret = 0;
			std::vector<FPreprocessorLogicalPiece> Pieces;
			return TryBuildPreprocessorLogicalLine(
				Source,
				Position,
				Logical,
				LogicalCaret,
				Pieces);
		}

		bool FindPreprocessorDirectiveCompletionContext(
			const std::string_view Source,
			const FTextPosition Position,
			FPreprocessorDirectiveCompletionContext& Out)
		{
			Out = {};
			std::string LogicalStorage;
			std::size_t Caret = 0;
			std::vector<FPreprocessorLogicalPiece> Pieces;
			if (!TryBuildPreprocessorLogicalLine(
					Source,
					Position,
					LogicalStorage,
					Caret,
					Pieces))
			{
				return false;
			}
			const std::string_view Logical = LogicalStorage;
			Out.ReplaceLine = Position.Line;

			auto IsIdentStart = [](const char Char) -> bool
			{
				return (std::isalpha(static_cast<unsigned char>(Char)) != 0) || Char == '_';
			};
			auto IsIdentPart = [&](const char Char) -> bool
			{
				return IsIdentStart(Char) || (std::isdigit(static_cast<unsigned char>(Char)) != 0);
			};
			auto FillIdentAtCaret = [&](const std::size_t NameStart, const std::size_t NameEnd) -> bool
			{
				// Completion replace ranges are single-line. A token split across `\`
				// continuations cannot be replaced safely (insert would duplicate).
				std::uint32_t StartLine = Position.Line;
				std::uint32_t StartColumn = 1;
				std::uint32_t EndLine = Position.Line;
				std::uint32_t EndColumn = 1;
				if (!MapLogicalOffsetToPhysical(Pieces, NameStart, false, StartLine, StartColumn)
					|| !MapLogicalOffsetToPhysical(Pieces, NameEnd, true, EndLine, EndColumn)
					|| StartLine != EndLine)
				{
					Out.Prefix.clear();
					Out.bHasReplaceRange = false;
					return false;
				}
				Out.Prefix = std::string(Logical.substr(NameStart, Caret - NameStart));
				Out.bHasReplaceRange = true;
				Out.ReplaceLine = StartLine;
				Out.ReplaceStartColumn = StartColumn;
				Out.ReplaceEndColumn = EndColumn;
				return true;
			};
			auto SkipCommentOrString = [&](std::size_t& Scan) -> bool
			{
				if (Scan >= Logical.size())
				{
					return false;
				}
				if (Scan + 1 < Logical.size() && Logical[Scan] == '/' && Logical[Scan + 1] == '*')
				{
					Scan += 2;
					while (Scan + 1 < Logical.size()
						&& !(Logical[Scan] == '*' && Logical[Scan + 1] == '/'))
					{
						++Scan;
					}
					if (Scan + 1 < Logical.size())
					{
						Scan += 2;
					}
					else
					{
						Scan = Logical.size();
					}
					return true;
				}
				if (Scan + 1 < Logical.size() && Logical[Scan] == '/' && Logical[Scan + 1] == '/')
				{
					// `//` comments out the rest of the logical (joined) line.
					Scan = Logical.size();
					return true;
				}
				if (Logical[Scan] == '"')
				{
					++Scan;
					while (Scan < Logical.size())
					{
						if (Logical[Scan] == '\\' && Scan + 1 < Logical.size())
						{
							Scan += 2;
							continue;
						}
						if (Logical[Scan] == '"')
						{
							++Scan;
							break;
						}
						++Scan;
					}
					return true;
				}
				return false;
			};
			auto MarkInsideDefined = [&](const std::size_t ExprStart)
			{
				// Match preprocessor RewriteDefinedOperators: `defined(NAME)` and
				// bare `defined NAME`. Skip comments/strings like CollectDirectivePayload
				// so `#if /* defined( */ def|` can still offer `defined`.
				Out.bInsideDefined = false;
				std::size_t Scan = ExprStart;
				while (Scan < Caret)
				{
					const char Char = Logical[Scan];
					if (Char == ' ' || Char == '\t')
					{
						++Scan;
						continue;
					}
					if (SkipCommentOrString(Scan))
					{
						continue;
					}
					if (!IsIdentStart(Char))
					{
						++Scan;
						continue;
					}
					const std::size_t IdentStart = Scan;
					++Scan;
					while (Scan < Logical.size() && IsIdentPart(Logical[Scan]))
					{
						++Scan;
					}
					const std::string_view Ident =
						Logical.substr(IdentStart, Scan - IdentStart);
					if (Ident != "defined")
					{
						continue;
					}

					std::size_t After = Scan;
					while (After < Logical.size()
						&& (Logical[After] == ' ' || Logical[After] == '\t'))
					{
						++After;
					}

					// Parenthesized: defined( ... )
					if (After < Logical.size() && Logical[After] == '(')
					{
						if (After >= Caret)
						{
							continue;
						}
						int Depth = 1;
						std::size_t Cursor = After + 1;
						while (Cursor < Caret && Depth > 0)
						{
							if (SkipCommentOrString(Cursor))
							{
								continue;
							}
							if (Logical[Cursor] == '(')
							{
								++Depth;
							}
							else if (Logical[Cursor] == ')')
							{
								--Depth;
							}
							++Cursor;
						}
						if (Depth > 0)
						{
							Out.bInsideDefined = true;
							return;
						}
						Scan = Cursor;
						continue;
					}

					// Bare: defined NAME
					if (Caret <= Scan)
					{
						continue;
					}
					if (After >= Caret)
					{
						Out.bInsideDefined = true;
						return;
					}
					if (After < Logical.size() && IsIdentStart(Logical[After]))
					{
						std::size_t TargetEnd = After + 1;
						while (TargetEnd < Logical.size() && IsIdentPart(Logical[TargetEnd]))
						{
							++TargetEnd;
						}
						if (Caret <= TargetEnd)
						{
							Out.bInsideDefined = true;
							return;
						}
						Scan = TargetEnd;
						continue;
					}
				}
			};

			std::size_t Index = 0;
			while (Index < Logical.size()
				&& (Logical[Index] == ' ' || Logical[Index] == '\t'))
			{
				++Index;
			}
			if (Index >= Logical.size() || Logical[Index] != '#')
			{
				return false;
			}
			const std::size_t HashPos = Index;
			if (Caret < HashPos)
			{
				Out.Kind = FPreprocessorDirectiveCompletionContext::EKind::NoCompletions;
				return true;
			}
			++Index;

			while (Index < Logical.size()
				&& (Logical[Index] == ' ' || Logical[Index] == '\t'))
			{
				if (Index >= Caret)
				{
					Out.Kind = FPreprocessorDirectiveCompletionContext::EKind::DirectiveName;
					Out.Prefix.clear();
					Out.bHasReplaceRange = false;
					return true;
				}
				++Index;
			}

			if (Index >= Caret)
			{
				Out.Kind = FPreprocessorDirectiveCompletionContext::EKind::DirectiveName;
				Out.Prefix.clear();
				Out.bHasReplaceRange = false;
				return true;
			}

			if (Index >= Logical.size() || !IsIdentStart(Logical[Index]))
			{
				Out.Kind = FPreprocessorDirectiveCompletionContext::EKind::NoCompletions;
				return true;
			}

			const std::size_t DirStart = Index;
			std::size_t DirEnd = DirStart;
			while (DirEnd < Logical.size() && IsIdentPart(Logical[DirEnd]))
			{
				++DirEnd;
			}
			const std::string Directive(Logical.substr(DirStart, DirEnd - DirStart));

			if (Caret >= DirStart && Caret <= DirEnd)
			{
				if (!FillIdentAtCaret(DirStart, DirEnd))
				{
					Out.Kind = FPreprocessorDirectiveCompletionContext::EKind::NoCompletions;
					return true;
				}
				Out.Kind = FPreprocessorDirectiveCompletionContext::EKind::DirectiveName;
				return true;
			}

			const bool bMacroNameDirective = Directive == "define" ||
											 Directive == "define_export" || Directive == "undef" ||
											 Directive == "ifdef" || Directive == "ifndef";
			const bool bIfExprDirective = Directive == "if" || Directive == "elif";
			if (!bMacroNameDirective && !bIfExprDirective)
			{
				Out.Kind = FPreprocessorDirectiveCompletionContext::EKind::NoCompletions;
				return true;
			}

			Index = DirEnd;
			while (Index < Logical.size()
				&& (Logical[Index] == ' ' || Logical[Index] == '\t'))
			{
				++Index;
			}

			if (bMacroNameDirective)
			{
				if (Caret < Index)
				{
					Out.Kind = FPreprocessorDirectiveCompletionContext::EKind::MacroName;
					Out.Prefix.clear();
					Out.bHasReplaceRange = false;
					return true;
				}
				if (Index >= Logical.size())
				{
					Out.Kind = FPreprocessorDirectiveCompletionContext::EKind::MacroName;
					Out.Prefix.clear();
					Out.bHasReplaceRange = false;
					return true;
				}
				if (!IsIdentStart(Logical[Index]))
				{
					Out.Kind = FPreprocessorDirectiveCompletionContext::EKind::NoCompletions;
					return true;
				}
				const std::size_t NameStart = Index;
				std::size_t NameEnd = NameStart;
				while (NameEnd < Logical.size() && IsIdentPart(Logical[NameEnd]))
				{
					++NameEnd;
				}
				if (Caret < NameStart || Caret > NameEnd)
				{
					// Params/body (including `\` continuation body lines).
					Out.Kind = FPreprocessorDirectiveCompletionContext::EKind::NoCompletions;
					return true;
				}
				if (!FillIdentAtCaret(NameStart, NameEnd))
				{
					Out.Kind = FPreprocessorDirectiveCompletionContext::EKind::NoCompletions;
					return true;
				}
				Out.Kind = FPreprocessorDirectiveCompletionContext::EKind::MacroName;
				return true;
			}

			Out.Kind = FPreprocessorDirectiveCompletionContext::EKind::IfExpr;
			MarkInsideDefined(Index);
			if (Caret < Index)
			{
				Out.Prefix.clear();
				Out.bHasReplaceRange = false;
				return true;
			}

			std::size_t NameStart = Caret;
			while (NameStart > Index && IsIdentPart(Logical[NameStart - 1]))
			{
				--NameStart;
			}
			if (NameStart < Logical.size() && NameStart >= Index && IsIdentStart(Logical[NameStart]))
			{
				std::size_t NameEnd = NameStart;
				while (NameEnd < Logical.size() && IsIdentPart(Logical[NameEnd]))
				{
					++NameEnd;
				}
				if (Caret >= NameStart && Caret <= NameEnd)
				{
					if (!FillIdentAtCaret(NameStart, NameEnd))
					{
						Out.Kind = FPreprocessorDirectiveCompletionContext::EKind::NoCompletions;
						Out.Prefix.clear();
						Out.bHasReplaceRange = false;
						return true;
					}
					return true;
				}
			}

			Out.Prefix.clear();
			Out.bHasReplaceRange = false;
			return true;
		}

		bool ResolveImportPathOnDisk(
			const std::string_view PathText,
			const std::string_view FileLabel,
			const std::vector<std::string_view>& ImportPaths,
			std::string& OutResolved)
		{
			if (PathText.empty())
			{
				return false;
			}
			{
				const std::filesystem::path AsPath = PathUtil::FromUtf8(PathText);
				if (AsPath.is_absolute())
				{
					std::error_code Error;
					if (std::filesystem::is_regular_file(AsPath, Error))
					{
						OutResolved = VeroScript::PathUtil::Normalize(
							PathUtil::ToUtf8(AsPath.lexically_normal()));
						return true;
					}
				}
			}
			std::vector<std::filesystem::path> Roots;
			auto AddRoot = [&Roots](const std::filesystem::path& Root)
			{
				std::error_code Error;
				if (!Root.empty() && std::filesystem::is_directory(Root, Error))
				{
					Roots.push_back(Root);
				}
			};
			for (const std::string_view ImportPath : ImportPaths)
			{
				AddRoot(PathUtil::FromUtf8(ImportPath));
			}
			{
				const std::filesystem::path LabelPath = PathUtil::FromUtf8(FileLabel);
				if (LabelPath.has_parent_path())
				{
					AddRoot(LabelPath.parent_path());
				}
			}
			// Align with ModuleLoader search roots (installed/SDK roots, then cwd,
			// checkout and VERO_PATH).
			{
				const std::string ExeDir = VeroScript::PathUtil::ExecutableDirectory();
				if (!ExeDir.empty())
				{
					AddRoot(PathUtil::FromUtf8(ExeDir) / "stdlib");
					AddRoot(PathUtil::FromUtf8(ExeDir) / ".." / "assets" / "stdlib");
				}
			}
			AddRoot(std::filesystem::path("stdlib"));
			const auto SourceRoot = PathUtil::FindSourceModuleRoot(".");
			if (!SourceRoot.empty())
			{
				AddRoot(SourceRoot / "VeroScript/Resources/Scripts/stdlib");
			}
			AddRoot(std::filesystem::path("Resources/Scripts/stdlib"));
			{
				if (const auto EnvPath = Environment::GetUtf8("VERO_PATH");
					EnvPath && !EnvPath->empty())
				{
					std::string Current;
					for (const char Character : *EnvPath)
					{
						#if defined(_WIN32)
						const bool bSeparator = Character == ';';
						#else
						const bool bSeparator = Character == ':';
						#endif
						if (bSeparator)
						{
							if (!Current.empty())
							{
								AddRoot(PathUtil::FromUtf8(Current));
								Current.clear();
							}
							continue;
						}
						Current.push_back(Character);
					}
					if (!Current.empty())
					{
						AddRoot(PathUtil::FromUtf8(Current));
					}
				}
			}
			for (const std::filesystem::path& Root : Roots)
			{
				const std::filesystem::path Candidate =
					Root / PathUtil::FromUtf8(PathText);
				std::error_code Error;
				if (std::filesystem::is_regular_file(Candidate, Error))
				{
					OutResolved = VeroScript::PathUtil::Normalize(
						PathUtil::ToUtf8(Candidate.lexically_normal()));
					return true;
				}
			}
			return false;
		}

		bool CollectSoftMemberHits(
			const std::string_view OwnerTypeName,
			const std::string_view MemberName,
			std::vector<FSymbolQueryHit>& OutHits,
			const FProgram& Program)
		{
			if (OwnerTypeName.empty() || MemberName.empty())
			{
				return false;
			}
			std::unordered_map<std::string, FInheritedMemberCompletion> Members;
			AppendSoftContainerMembers(OwnerTypeName, MemberName, Members, &Program);
			const auto Found = Members.find(std::string(MemberName));
			if (Found == Members.end())
			{
				return false;
			}
			FSymbolQueryHit Hit;
			Hit.Kind = Found->second.Kind;
			Hit.Name = Found->second.Label;
			Hit.Detail = Found->second.Detail;
			Hit.ContainerName = std::string(OwnerTypeName);
			Hit.bSoftBuiltin = true;
			Hit.bExported = true;
			Hit.DefLine = 0;
			Hit.DefColumn = 0;
			OutHits.push_back(std::move(Hit));
			return true;
		}

		bool ResolveHostMemberReturnTypeName(
			const VeroScript::IHostBindingCatalog* Bindings,
			const std::string& OwnerTypeName,
			const std::string& MemberName,
			std::string& OutReturnType,
			const FProgram* const Program)
		{
			OutReturnType.clear();
			if (!Bindings || OwnerTypeName.empty() || MemberName.empty())
			{
				return false;
			}
			const VeroScript::FHostTypeDesc* Type = Bindings->FindType(OwnerTypeName);
			if (!Type && Program)
			{
				std::vector<std::string> Hierarchy;
				CollectTypeHierarchy(*Program, OwnerTypeName, Hierarchy);
				for (const std::string& Candidate : Hierarchy)
				{
					Type = Bindings->FindType(Candidate);
					if (Type)
					{
						break;
					}
				}
			}
			if (!Type)
			{
				return false;
			}
			for (const VeroScript::FHostMemberDesc& Member : Type->Members)
			{
				if (Member.Name != MemberName)
				{
					continue;
				}
				if (Member.bOperator
					|| Member.Kind == VeroScript::EHostMemberKind::Constructor
					|| Member.Kind == VeroScript::EHostMemberKind::PropertySetter
					|| Member.Kind == VeroScript::EHostMemberKind::EnumMember
					|| Member.Kind == VeroScript::EHostMemberKind::StaticConstant)
				{
					continue;
				}
				if (Member.ReturnType == VeroScript::ENativeType::Void)
				{
					return false;
				}
				if (Member.ReturnType == VeroScript::ENativeType::HostObject
					|| Member.ReturnType == VeroScript::ENativeType::Enum
					|| Member.ReturnType == VeroScript::ENativeType::Function)
				{
					OutReturnType = Member.ReturnHostTypeName.empty()
						? (Member.ReturnType == VeroScript::ENativeType::HostObject
							? Type->Name
							: std::string(VeroScript::NativeTypeName(Member.ReturnType)))
						: Member.ReturnHostTypeName;
					return !OutReturnType.empty();
				}
				if (Member.ReturnType == VeroScript::ENativeType::Array
					|| Member.ReturnType == VeroScript::ENativeType::Set
					|| Member.ReturnType == VeroScript::ENativeType::MultiSet
					|| Member.ReturnType == VeroScript::ENativeType::OrderedMultiSet
					|| Member.ReturnType == VeroScript::ENativeType::Queue
					|| Member.ReturnType == VeroScript::ENativeType::PriorityQueue
					|| Member.ReturnType == VeroScript::ENativeType::Stack
					|| Member.ReturnType == VeroScript::ENativeType::Deque
					|| Member.ReturnType == VeroScript::ENativeType::Channel)
				{
					OutReturnType = Member.ReturnElementTypeName.empty()
						? std::string(VeroScript::NativeTypeName(Member.ReturnType))
						: VeroScript::MakeNativeUnaryContainerTypeName(
							VeroScript::NativeTypeName(Member.ReturnType),
							VeroScript::CanonicalizeNativeElementTypeName(
								Member.ReturnElementTypeName));
					return true;
				}
				if (VeroScript::NativeTypeIsBinaryMap(Member.ReturnType))
				{
					OutReturnType =
						Member.ReturnKeyTypeName.empty() || Member.ReturnValueTypeName.empty()
						? std::string(VeroScript::NativeTypeName(Member.ReturnType))
						: VeroScript::MakeNativeBinaryContainerTypeName(
							VeroScript::NativeTypeName(Member.ReturnType),
							VeroScript::CanonicalizeNativeElementTypeName(
								Member.ReturnKeyTypeName),
							VeroScript::CanonicalizeNativeElementTypeName(
								Member.ReturnValueTypeName));
					return true;
				}
				OutReturnType = VeroScript::NativeTypeName(Member.ReturnType);
				return !OutReturnType.empty();
			}
			return false;
		}

		bool CollectHostMemberHits(
			const VeroScript::IHostBindingCatalog* Bindings,
			const std::string_view OwnerTypeName,
			const std::string_view MemberName,
			std::vector<FSymbolQueryHit>& OutHits,
			const FProgram* const Program,
			const bool bTypeReceiver)
		{
			if (!Bindings || OwnerTypeName.empty() || MemberName.empty())
			{
				return false;
			}
			const VeroScript::FHostTypeDesc* Type = Bindings->FindType(OwnerTypeName);
			if (!Type && Program)
			{
				std::vector<std::string> Hierarchy;
				CollectTypeHierarchy(*Program, OwnerTypeName, Hierarchy);
				for (const std::string& Candidate : Hierarchy)
				{
					Type = Bindings->FindType(Candidate);
					if (Type)
					{
						break;
					}
				}
			}
			if (!Type)
			{
				return false;
			}
			bool bFound = false;
			for (const VeroScript::FHostMemberDesc& Member : Type->Members)
			{
				if (Member.Name != MemberName)
				{
					continue;
				}
				if (!HostMemberCanBeNamedFromReceiver(Member, bTypeReceiver))
				{
					continue;
				}
				FSymbolQueryHit Hit;
				Hit.Name = Member.Name;
				Hit.Detail = VeroScript::FormatHostAnnotations(Member.Annotations);
				if (!Hit.Detail.empty())
				{
					Hit.Detail.push_back(' ');
				}
				Hit.Detail += FormatHostMemberDetail(Member, Type->Name);
				Hit.Documentation = VeroScript::FormatHostDocumentation(
					Member.Documentation,
					Member.ParamNames,
					Member.ParamDocumentation,
					Member.ReturnDocumentation,
					Member.Annotations,
					Member.ParamAnnotations);
				Hit.ContainerName = Type->Name;
				Hit.bSoftBuiltin = true;
				Hit.bExported = true;
				Hit.DefLine = 0;
				Hit.DefColumn = 0;
				switch (Member.Kind)
				{
				case VeroScript::EHostMemberKind::EnumMember:
					{
						Hit.Kind = EDocumentSymbolKind::EnumMember;
						break;
					}
				case VeroScript::EHostMemberKind::Constructor:
					{
						Hit.Kind = EDocumentSymbolKind::Constructor;
						Hit.Name = Type->Name;
						break;
					}
				case VeroScript::EHostMemberKind::StaticConstant:
					{
						Hit.Kind = EDocumentSymbolKind::Constant;
						break;
					}
				case VeroScript::EHostMemberKind::Field:
				case VeroScript::EHostMemberKind::PropertyGetter:
					{
						Hit.Kind = EDocumentSymbolKind::Field;
						break;
					}
				default:
					{
						Hit.Kind = EDocumentSymbolKind::Method;
						break;
					}
				}
				OutHits.push_back(std::move(Hit));
				bFound = true;
			}
			return bFound;
		}

		bool FindGotoCompletionContext(
			const std::string_view Source,
			const std::string_view FileLabel,
			const FTextPosition Position,
			FGotoCompletionContext& Out)
		{
			Out = FGotoCompletionContext{};
			if (ActiveQueryLexicalSnapshot && ActiveQueryLexicalSnapshot->Matches(Source, FileLabel)
				&& !ActiveQueryLexicalSnapshot->HasGotoKeyword)
			{
				return false;
			}
			FQueryLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			// Tolerate later lex errors (tokens before the fault remain valid).

			for (std::size_t Index = QueryTokenLowerBound(Tokens, {Position.Line, 1});
				Index < Tokens.size(); ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (Token.Location.Line != Position.Line || Token.Location.Column > Position.Column)
				{
					break;
				}
				if (Token.Kind != ETokenKind::Keyword || Token.Lexeme != "goto")
				{
					continue;
				}
				const std::uint32_t AfterGoto =
					Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size());
				if (Position.Column < AfterGoto)
				{
					continue;
				}

				if (Index + 1 < Tokens.size())
				{
					const FToken& Next = Tokens[Index + 1];
					if (Next.Kind == ETokenKind::Identifier
						&& TokenCoversPositionOrEnd(Next, Position))
					{
						Out.bHasReplaceRange = true;
						Out.ReplaceLine = Next.Location.Line;
						Out.ReplaceStartColumn = Next.Location.Column;
						Out.ReplaceEndColumn =
							Next.Location.Column
								+ static_cast<std::uint32_t>(Next.Lexeme.size());
						const std::uint32_t Relative =
							Position.Column >= Next.Location.Column
							? (Position.Column - Next.Location.Column)
							: 0;
						const std::size_t PrefixLen =
							Relative <= Next.Lexeme.size() ? Relative : Next.Lexeme.size();
						Out.Prefix = std::string(Next.Lexeme.substr(0, PrefixLen));
						return true;
					}
					const std::uint32_t NextEnd =
						Next.Location.Column
							+ static_cast<std::uint32_t>(Next.Lexeme.size());
					if (Next.Location.Line < Position.Line
						|| (Next.Location.Line == Position.Line
							&& Position.Column > NextEnd))
					{
						// Caret is past the goto target token.
						continue;
					}
				}

				Out.Prefix.clear();
				Out.bHasReplaceRange = false;
				return true;
			}
			return false;
		}

		bool IsGotoNameContext(
			const std::string_view Source,
			const std::string_view FileLabel,
			const FTextPosition Position)
		{
			FGotoCompletionContext Context;
			return FindGotoCompletionContext(Source, FileLabel, Position, Context)
				&& Context.bHasReplaceRange;
		}

		void PreferLabelHits(std::vector<FSymbolQueryHit>& Hits)
		{
			std::vector<FSymbolQueryHit> Labels;
			for (const FSymbolQueryHit& Hit : Hits)
			{
				if (Hit.Kind == EDocumentSymbolKind::Label)
				{
					Labels.push_back(Hit);
				}
			}
			Hits = std::move(Labels);
		}

		FTextPosition PhysicalToAstPosition(
			const VeroScript::FPreprocessLineMap& Map,
			const FTextPosition Physical)
		{
			FTextPosition Ast = Physical;
			std::uint32_t PreprocessedLine = 0;
			std::uint32_t PreprocessedColumn = 0;
			if (MapCaretPhysicalToPreprocessed(
					Map,
					Physical.Line,
					Physical.Column,
					PreprocessedLine,
					PreprocessedColumn))
			{
				Ast.Line = PreprocessedLine;
				Ast.Column = PreprocessedColumn;
			}
			return Ast;
		}
