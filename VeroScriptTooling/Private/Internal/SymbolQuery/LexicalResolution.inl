// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		bool IsLexTypeToken(const FToken& Token)
		{
			if (Token.Kind == ETokenKind::Identifier)
			{
				return true;
			}
			if (Token.Kind == ETokenKind::Keyword)
			{
				// Type-name fragments — shared classifiers + qualifiers (no `signed`).
				return VeroScript::IsTypeKeyword(Token.Lexeme) || VeroScript::IsContainerTypeKeyword(Token.Lexeme) || (Token.Lexeme == "const" || Token.Lexeme == "constexpr") || Token.Lexeme == "ref";
			}
			if (Token.Kind == ETokenKind::Symbol)
			{
				return Token.Lexeme == "<" || Token.Lexeme == ">" || Token.Lexeme == "," || Token.Lexeme == "[" || Token.Lexeme == "]";
			}
			return false;
		}

		void AppendLexTypeToken(std::string& Out, const FToken& Token)
		{
			Out.append(Token.Lexeme.data(), Token.Lexeme.size());
		}

		bool TryLexResolveBindingTypeName(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const std::string_view BindingName,
		    std::string& OutTypeName)
		{
			if (BindingName.empty())
			{
				return false;
			}
			FQueryLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			// Tolerate later lex errors so type fragments before the fault remain usable.

			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (Token.Kind != ETokenKind::Identifier || Token.Lexeme != BindingName)
				{
					continue;
				}

				if (Index == 0)
				{
					continue;
				}

				std::size_t TypeEnd = Index - 1;
				if (Tokens[TypeEnd].Kind == ETokenKind::Symbol && Tokens[TypeEnd].Lexeme == "&")
				{
					if (TypeEnd == 0)
					{
						continue;
					}
					--TypeEnd;
				}
				while (TypeEnd > 0 && Tokens[TypeEnd].Kind == ETokenKind::Keyword && (Tokens[TypeEnd].Lexeme == "ref" || Tokens[TypeEnd].Lexeme == "const" || Tokens[TypeEnd].Lexeme == "constexpr"))
				{
					--TypeEnd;
				}

				std::size_t TypeStart = TypeEnd;
				while (TypeStart > 0 && IsLexTypeToken(Tokens[TypeStart - 1]))
				{
					--TypeStart;
				}

				if (TypeStart > TypeEnd)
				{
					continue;
				}

				std::string TypeName;
				for (std::size_t TokenIndex = TypeStart; TokenIndex <= TypeEnd; ++TokenIndex)
				{
					if (Tokens[TokenIndex].Kind == ETokenKind::Keyword && (Tokens[TokenIndex].Lexeme == "const" || Tokens[TokenIndex].Lexeme == "constexpr" || Tokens[TokenIndex].Lexeme == "ref"))
					{
						continue;
					}
					AppendLexTypeToken(TypeName, Tokens[TokenIndex]);
				}
				if (TypeName.empty())
				{
					continue;
				}
				OutTypeName = std::move(TypeName);
				return true;
			}
			return false;
		}

		// Scan only through the caret. The lexer may stop at an unfinished literal or
		// comment, so its token list alone cannot decide whether completion is code.
		enum class ECompletionLexicalRegion : std::uint8_t
		{
			Code,
			Literal,
			String,
			RawString,
			Character,
			LineComment,
			BlockComment,
		};

		ECompletionLexicalRegion CompletionRegionAt(
		    const std::string_view Source,
		    const std::size_t Caret,
		    std::size_t* LiteralStart = nullptr)
		{
			if (LiteralStart)
			{
				*LiteralStart = std::string_view::npos;
			}

			ECompletionLexicalRegion Region = ECompletionLexicalRegion::Code;
			bool bAfterLiteral = false;
			for (std::size_t Index = 0; Index < Caret; ++Index)
			{
				if ((Index & 255u) == 0)
				{
					CheckAnalysisCancellation();
				}

				const char Ch = Source[Index];
				const char Next = Index + 1 < Caret ? Source[Index + 1] : '\0';
				switch (Region)
				{
				case ECompletionLexicalRegion::Code:
					{
						if (!std::isspace(static_cast<unsigned char>(Ch)) && !(Ch == '/' && (Next == '/' || Next == '*')))
						{
							bAfterLiteral = false;
						}
						if (Ch == 'r' && Next == '"' && (Index == 0 || (!std::isalnum(static_cast<unsigned char>(Source[Index - 1])) && Source[Index - 1] != '_')))
						{
							Region = ECompletionLexicalRegion::RawString;
							if (LiteralStart)
							{
								*LiteralStart = Index;
							}

							++Index;
						}
						else if (Ch == '/' && Next == '/')
						{
							Region = ECompletionLexicalRegion::LineComment;
							++Index;
						}
						else if (Ch == '/' && Next == '*')
						{
							Region = ECompletionLexicalRegion::BlockComment;
							++Index;
						}
						else if (Ch == '"')
						{
							Region = ECompletionLexicalRegion::String;
							if (LiteralStart)
							{
								*LiteralStart = Index;
							}
						}
						else if (Ch == '\'')
						{
							Region = ECompletionLexicalRegion::Character;
						}
						else if (std::isdigit(static_cast<unsigned char>(Ch)))
						{
							// Include unfinished bases, exponents and suffixes. A failed lexer
							// elsewhere must not turn the end of a number into a name site.
							const bool bBased = Ch == '0' && Index + 1 < Source.size() && (Source[Index + 1] == 'x' || Source[Index + 1] == 'X' || Source[Index + 1] == 'b' || Source[Index + 1] == 'B');
							bool bDot = false;
							std::size_t End = Index + 1;
							while (End < Source.size())
							{
								const char Part = Source[End];
								if (std::isalnum(static_cast<unsigned char>(Part)) || Part == '_')
								{
									++End;
								}
								else if (!bBased && !bDot && Part == '.')
								{
									bDot = true;
									++End;
								}
								else if (!bBased && (Part == '+' || Part == '-') && (Source[End - 1] == 'e' || Source[End - 1] == 'E'))
								{
									++End;
								}
								else
								{
									break;
								}
							}
							if (Caret <= End)
							{
								return ECompletionLexicalRegion::Literal;
							}
							bAfterLiteral = true;
							Index = End - 1;
						}
						else if (std::isalpha(static_cast<unsigned char>(Ch)) || Ch == '_')
						{
							// Digits in an identifier are not numeric literals.
							const std::size_t Start = Index;
							while (Index + 1 < Caret && (std::isalnum(static_cast<unsigned char>(Source[Index + 1])) || Source[Index + 1] == '_'))
							{
								++Index;
							}
							const std::string_view Word = Source.substr(Start, Index - Start + 1);
							bAfterLiteral = (Word == "true" || Word == "false" || Word == "nullptr") && (Index + 1 == Source.size() || (!std::isalnum(static_cast<unsigned char>(Source[Index + 1])) && Source[Index + 1] != '_'));
						}
						break;
					}
				case ECompletionLexicalRegion::RawString:
					{
						if (Ch == '"')
						{
							Region = ECompletionLexicalRegion::Code;
							bAfterLiteral = true;
						}
						break;
					}
				case ECompletionLexicalRegion::String:
				case ECompletionLexicalRegion::Character:
					{
						// Strings and character literals share escape and line-end handling.
						if (Ch == '\\' && Index + 1 < Caret)
						{
							++Index;
						}
						else if (Ch == (Region == ECompletionLexicalRegion::String ? '"' : '\'') || Ch == '\n')
						{
							bAfterLiteral = Ch != '\n';
							Region = ECompletionLexicalRegion::Code;
						}
						break;
					}
				case ECompletionLexicalRegion::LineComment:
					{
						if (Ch == '\n')
						{
							Region = ECompletionLexicalRegion::Code;
						}
						break;
					}
				case ECompletionLexicalRegion::BlockComment:
					{
						if (Ch == '*' && Next == '/')
						{
							Region = ECompletionLexicalRegion::Code;
							++Index;
						}
						break;
					}
				default:
					{
						Region = ECompletionLexicalRegion::Code;
						break;
					}
				}
			}
			return Region == ECompletionLexicalRegion::Code && bAfterLiteral
			    ? ECompletionLexicalRegion::Literal
			    : Region;
		}

		FCompletionResult EmptyCompletionAtCaret(
		    const std::string_view Source,
		    const FTextPosition Position,
		    const std::size_t Caret)
		{
			FCompletionResult Result;
			Result.bSuccess = true;
			std::size_t Start = Caret;
			while (Start > 0 && (std::isalnum(static_cast<unsigned char>(Source[Start - 1])) || Source[Start - 1] == '_'))
			{
				--Start;
			}
			if (Start == Caret)
			{
				return Result;
			}
			std::size_t End = Caret;
			while (End < Source.size() && (std::isalnum(static_cast<unsigned char>(Source[End])) || Source[End] == '_'))
			{
				++End;
			}
			Result.Prefix = std::string(Source.substr(Start, Caret - Start));
			Result.bHasReplaceRange = true;
			Result.ReplaceLine = Position.Line;
			Result.ReplaceStartColumn = Position.Column - static_cast<std::uint32_t>(Caret - Start);
			Result.ReplaceEndColumn = Position.Column + static_cast<std::uint32_t>(End - Caret);
			return Result;
		}

		bool HasImmediateMemberAccess(
		    const std::string_view Source,
		    const std::size_t Caret)
		{
			std::size_t Start = Caret;
			while (Start > 0 && (std::isalnum(static_cast<unsigned char>(Source[Start - 1])) || Source[Start - 1] == '_'))
			{
				--Start;
			}
			while (Start > 0 && (Source[Start - 1] == ' ' || Source[Start - 1] == '\t'))
			{
				--Start;
			}
			return (Start >= 2 && (Source.substr(Start - 2, 2) == "::" || Source.substr(Start - 2, 2) == "->")) || (Start >= 1 && Source[Start - 1] == '.');
		}

		// Recover only a for-header whose initializer is already terminated. Keep
		// its declarations in a real parser scope; never promote them to globals.
		bool TryRecoverForHeader(
		    const std::string_view Source,
		    const std::vector<FToken>& Tokens,
		    const FTextPosition Position,
		    const std::size_t Caret,
		    const bool bPrefixOnly,
		    std::string& OutRecovered)
		{
			std::vector<std::size_t> Opens;
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				if (Tokens[Index].Location.Line > Position.Line || (Tokens[Index].Location.Line == Position.Line && Tokens[Index].Location.Column >= Position.Column))
				{
					break;
				}
				const std::string_view Part = Tokens[Index].Lexeme;
				if (Part == "(" || Part == "[" || Part == "{")
				{
					Opens.push_back(Index);
				}
				else if (Part == ")" || Part == "]" || Part == "}")
				{
					if (Opens.empty())
					{
						return false;
					}
					const std::string_view Open = Tokens[Opens.back()].Lexeme;
					if ((Part == ")" && Open != "(") || (Part == "]" && Open != "[") || (Part == "}" && Open != "{"))
					{
						return false;
					}
					Opens.pop_back();
				}
			}
			if (Opens.empty())
			{
				return false;
			}
			std::size_t ForFrame = Opens.size();
			while (ForFrame > 0)
			{
				const std::size_t Candidate = Opens[--ForFrame];
				if (Tokens[Candidate].Lexeme == "{")
				{
					return false; // A loop body or lambda has its own lexical scope.
				}
				if (Tokens[Candidate].Lexeme == "(" && Candidate > 0 && Tokens[Candidate - 1].Lexeme == "for")
				{
					break;
				}
			}
			const std::size_t Open = Opens[ForFrame];
			if (Tokens[Open].Lexeme != "(" || Open == 0 || Tokens[Open - 1].Lexeme != "for")
			{
				return false;
			}
			std::size_t InitEnd = 0;
			int Depth = 0;
			for (std::size_t Index = Open + 1; Index < Tokens.size(); ++Index)
			{
				const std::string_view Part = Tokens[Index].Lexeme;
				if (Part == "(" || Part == "[" || Part == "{")
				{
					++Depth;
				}
				else if (Part == ")" || Part == "]" || Part == "}")
				{
					--Depth;
				}
				else if (Part == ";" && Depth == 0)
				{
					if (!FindSourceOffset(Source, Tokens[Index].Location.Line,
					        Tokens[Index].Location.Column + 1, InitEnd))
					{
						return false;
					}
					break;
				}
			}
			if (InitEnd == 0 || InitEnd > Caret)
			{
				return false;
			}
			std::size_t CloseOffset = Caret;
			bool bNeedsBody = true;
			if (!bPrefixOnly)
			{
				Depth = 0;
				bool bFound = false;
				for (std::size_t Index = Open + 1; Index < Tokens.size(); ++Index)
				{
					const std::string_view Part = Tokens[Index].Lexeme;
					if (Part == "(")
					{
						++Depth;
					}
					else if (Part == ")" && Depth == 0)
					{
						bFound = FindSourceOffset(Source, Tokens[Index].Location.Line,
						             Tokens[Index].Location.Column, CloseOffset) &&
						    CloseOffset >= Caret;
						bNeedsBody = Index + 1 == Tokens.size() || Tokens[Index + 1].Lexeme == "}" || Tokens[Index + 1].Kind == ETokenKind::EndOfFile;
						break;
					}
					else if (Part == ")")
					{
						--Depth;
					}
					else if (Depth == 0 && (Part == "{" || Part == "}"))
					{
						break;
					}
				}
				if (!bFound)
				{
					return false;
				}
			}
			else
			{
				// At EOF (or with unrelated errors after the caret), close only
				// known enclosing blocks. Do not guess expression delimiters.
				for (std::size_t Index = 0; Index < ForFrame; ++Index)
				{
					if (Tokens[Opens[Index]].Lexeme != "{")
					{
						return false;
					}
				}
			}
			OutRecovered.assign(Source.substr(0, CloseOffset));
			for (std::size_t Index = InitEnd; Index < CloseOffset; ++Index)
			{
				if (OutRecovered[Index] != '\n' && OutRecovered[Index] != '\r')
				{
					OutRecovered[Index] = ' ';
				}
			}
			OutRecovered += ";)";
			if (bNeedsBody)
			{
				OutRecovered += " {}";
			}
			if (bPrefixOnly)
			{
				OutRecovered.append(ForFrame, '}');
			}
			else
			{
				OutRecovered.append(Source.substr(CloseOffset + 1));
			}
			return true;
		}

		// Last-resort parsing of the executable prefix. Add syntax only after the
		// caret, so existing declarations and their block/lambda scopes stay intact.
		bool TryRecoverExecutablePrefix(
		    const std::string_view Source,
		    const std::vector<FToken>& Tokens,
		    const FTextPosition Position,
		    const std::size_t Caret,
		    std::string& OutRecovered)
		{
			if (ClassifyStructuralCompletionScope(Tokens, Position) != EStructuralCompletionScope::Executable)
			{
				return false;
			}
			std::vector<std::size_t> Opens;
			std::size_t Last = Tokens.size();
			std::size_t End = Caret;
			std::vector<std::size_t> InitializerBraces;
			std::vector<std::size_t> ConditionalDepths;
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (Token.Kind == ETokenKind::EndOfFile || Token.Location.Line > Position.Line || (Token.Location.Line == Position.Line && Token.Location.Column >= Position.Column))
				{
					break;
				}
				Last = Index;
				if (Token.Location.Line == Position.Line && Token.Location.Column + Token.Lexeme.size() > Position.Column)
				{
					// Preserve the suffix of a token being edited, and its replace range.
					End += Token.Location.Column + Token.Lexeme.size() - Position.Column;
				}
				const std::string_view Part = Token.Lexeme;
				if (Part == "?")
				{
					ConditionalDepths.push_back(Opens.size());
				}
				else if (Part == ":" && !ConditionalDepths.empty() &&
				    ConditionalDepths.back() == Opens.size())
				{
					ConditionalDepths.pop_back();
				}
				if (Part == "(" || Part == "[" || Part == "{")
				{
					if (Part == "{")
					{
						const std::string_view Head = Index > 0 ? Tokens[Index - 1].Lexeme : std::string_view{};
						const bool bNestedInitializer = !Opens.empty() && std::find(InitializerBraces.begin(), InitializerBraces.end(), Opens.back()) != InitializerBraces.end();
						if (Head == "=" || Head == "(" || Head == "[" || Head == "," || Head == "return" || Head == "throw" || Head == "=>" || (bNestedInitializer && (Head == "{" || Head == ":" || Head == "?")))
						{
							InitializerBraces.push_back(Index);
						}
					}
					Opens.push_back(Index);
					if (Opens.size() > 64)
					{
						return false;
					}
				}
				else if (Part == ")" || Part == "]" || Part == "}")
				{
					if (Opens.empty())
					{
						return false;
					}
					const std::string_view Open = Tokens[Opens.back()].Lexeme;
					if ((Part == ")" && Open != "(") || (Part == "]" && Open != "[") || (Part == "}" && Open != "{"))
					{
						return false;
					}
					Opens.pop_back();
				}
			}
			if (Opens.empty() || Last == Tokens.size())
			{
				return false;
			}
			OutRecovered.assign(Source.substr(0, End));
			const std::string_view LastPart = Tokens[Last].Lexeme;
			bool bStatementComplete = LastPart == ";" || LastPart == "{" || LastPart == "}";
			if ((Tokens[Last].Kind == ETokenKind::Symbol && !bStatementComplete && LastPart != ")" && LastPart != "]" && LastPart != "++" && LastPart != "--") || LastPart == "return" || LastPart == "throw")
			{
				OutRecovered += (LastPart == "." || LastPart == "::" || LastPart == "->")
				    ? "__vero_completion"
				    : "0";
			}
			std::size_t FrameDepth = Opens.size();
			for (auto It = Opens.rbegin(); It != Opens.rend(); ++It, --FrameDepth)
			{
				// Complete dangling ternaries at their own expression depth before
				// closing groups/for headers; preserve nested lambda scopes.
				while (!ConditionalDepths.empty() && ConditionalDepths.back() == FrameDepth)
				{
					OutRecovered += ":0";
					ConditionalDepths.pop_back();
				}
				const std::size_t Open = *It;
				const std::string_view Part = Tokens[Open].Lexeme;
				const std::string_view Head = Open > 0 ? Tokens[Open - 1].Lexeme : std::string_view{};
				if (Part == "[")
				{
					OutRecovered += ']';
					bStatementComplete = false;
				}
				else if (Part == "(")
				{
					if (Head == "for")
					{
						int Depth = 0;
						std::size_t Semicolons = 0;
						bool bForEach = false;
						bool bInitializer = false;
						std::size_t Conditionals = 0;
						for (std::size_t Index = Open + 1; Index <= Last; ++Index)
						{
							const std::string_view Item = Tokens[Index].Lexeme;
							if (Item == "(" || Item == "[" || Item == "{")
							{
								++Depth;
							}
							else if (Item == ")" || Item == "]" || Item == "}")
							{
								--Depth;
							}
							else if (Depth == 0 && Item == ";")
							{
								++Semicolons;
							}
							else if (Depth == 0 && Item == "=")
							{
								bInitializer = true;
							}
							else if (Depth == 0 && Item == "?")
							{
								++Conditionals;
							}
							else if (Depth == 0 && Item == ":" && Conditionals > 0)
							{
								--Conditionals;
							}
							else if (Depth == 0 && Item == ":" && !bInitializer && Semicolons == 0)
							{
								bForEach = true;
							}
						}
						if (Semicolons > 2)
						{
							return false;
						}
						if (!bForEach)
						{
							OutRecovered.append(2 - Semicolons, ';');
						}
					}
					OutRecovered += ')';
					bStatementComplete = Head == "for" || Head == "if" || Head == "while" || Head == "switch";
					if (bStatementComplete)
					{
						OutRecovered += " {}";
					}
				}
				else
				{
					if (std::find(InitializerBraces.begin(), InitializerBraces.end(), Open) != InitializerBraces.end())
					{
						// Array/map braces close an expression, not a statement block.
						OutRecovered += '}';
						bStatementComplete = false;
						continue;
					}
					const FTextPosition AfterOpen{Tokens[Open].Location.Line,
					    Tokens[Open].Location.Column + 1};
					const bool bExecutable = ClassifyStructuralCompletionScope(Tokens, AfterOpen) == EStructuralCompletionScope::Executable;
					if (bExecutable && !bStatementComplete)
					{
						OutRecovered += ';';
					}
					OutRecovered += '}';
					if (Head == "do")
					{
						OutRecovered += " while(false);";
					}
					if (Head == "try")
					{
						OutRecovered += " finally {}";
					}
					bStatementComplete = true;
				}
			}
			return true;
		}

		struct FCompletionAccess
		{
			static void CollectResolvedSourceImports(
			    const std::string_view Source,
			    const std::string_view FileLabel,
			    const FCompletionOptions& Options,
			    FProgram& Program)
			{
				Program.EntryModulePath = FileLabel;
				FQueryLexer Lexer(Source, FileLabel);
				const std::vector<FToken>& Tokens = Lexer.Tokenize();
				const std::filesystem::path SourceDirectory =
				    PathUtil::FromUtf8(FileLabel).parent_path();
				const std::string LocalSearchPath = SourceDirectory.empty()
				    ? std::string{}
				    : PathUtil::ToUtf8(SourceDirectory);
				std::vector<std::string_view> SearchPaths;
				if (!LocalSearchPath.empty())
				{
					SearchPaths.push_back(LocalSearchPath);
				}
				SearchPaths.insert(SearchPaths.end(),
				    Options.ImportPaths.begin(), Options.ImportPaths.end());
				SearchPaths.insert(SearchPaths.end(),
				    Options.StdlibSearchPaths.begin(), Options.StdlibSearchPaths.end());
				int BraceDepth = 0;
				for (std::size_t Index = 0; Index + 1 < Tokens.size(); ++Index)
				{
					const FToken& Token = Tokens[Index];
					if (Token.Kind == ETokenKind::Symbol)
					{
						if (Token.Lexeme == "{")
						{
							++BraceDepth;
						}
						else if (Token.Lexeme == "}" && BraceDepth > 0)
						{
							--BraceDepth;
						}
					}
					if (BraceDepth != 0 || Token.Kind != ETokenKind::Keyword || Token.Lexeme != "import" || Tokens[Index + 1].Kind != ETokenKind::StringLiteral)
					{
						continue;
					}
					FImportDecl Import;
					std::string DecodeError;
					if (!ParserDetail::DecodeStringLiteral(
					        Tokens[Index + 1].Lexeme, Import.Path, DecodeError) ||
					    Import.Path.empty())
					{
						continue;
					}
					Import.ImporterModulePath = Program.EntryModulePath;
					if (ResolveImportPathOnDisk(
					        Import.Path, FileLabel, SearchPaths, Import.ResolvedPath))
					{
						Program.Imports.push_back(std::move(Import));
					}
				}
			}

			// Retain only the original caret's preprocessor state across parser
			// recovery. Copy emitted source only when a lexical fallback is needed.
			struct FCompletionPreprocessorContext
			{
				bool bActiveCode = true;
				bool bHasMap = false;
				bool bHasMappedSource = false;
				FTextPosition AstPosition;
				std::string PreprocessedSource;

				void Capture(const FPreprocessLineMap* Map, const FTextPosition Position,
				    const bool bRetainSource)
				{
					if (!Map) return;
					bHasMap = true;
					bActiveCode = IsActiveCodeLine(*Map, Position.Line);
					if (bRetainSource && bActiveCode && MapCaretPhysicalToPreprocessed(*Map,
					        Position.Line, Position.Column, AstPosition.Line, AstPosition.Column))
					{
						// Core's token map chooses the preceding byte when no byte
						// starts at the caret (EOF or the end of a physical line).
						// A completion caret is the boundary after that byte.
						std::size_t Offset = 0;
						if (FindSourceOffset(Map->PreprocessedSource, AstPosition.Line, AstPosition.Column, Offset)
						    && Offset < Map->OutByteToPhysical.size() && Offset < Map->PreprocessedSource.size())
						{
							const auto& Origin = Map->OutByteToPhysical[Offset];
							if (Origin.Line == Position.Line && Position.Column > 1 && Origin.Column == Position.Column - 1
							    && Map->PreprocessedSource[Offset] != '\n' && Map->PreprocessedSource[Offset] != '\r')
							{
								++AstPosition.Column;
							}
						}
						PreprocessedSource = Map->PreprocessedSource;
						bHasMappedSource = true;
					}
				}
			};

			static FCompletionResult TryCompleteSoftNamespaceLexically(
			    const std::string_view Source,
			    const std::string_view FileLabel,
			    const FTextPosition Position,
			    const FCompletionOptions& Options,
			    const bool bUseCaretLine,
			    const FCompletionPreprocessorContext& PreprocessorContext)
			{
				std::string_view QuerySource = Source;
				FTextPosition QueryPosition = Position;
				if (bUseCaretLine)
				{
					std::size_t Caret = 0;
					if (!FindSourceOffset(Source, Position.Line, Position.Column, Caret))
					{
						return {};
					}
					std::size_t Begin = Caret;
					while (Begin > 0 && Source[Begin - 1] != '\n')
					{
						--Begin;
					}
					std::size_t End = Caret;
					while (End < Source.size() && Source[End] != '\n')
					{
						++End;
					}
					QuerySource = Source.substr(Begin, End - Begin);
					QueryPosition.Line = 1;
				}
				FMemberCompletionContext Context;
				if (!FindMemberCompletionContext(
				        QuerySource, FileLabel, QueryPosition, Context) ||
				    !Context.bNamespacePrefix || !FindStandardNamespaceMetadata(Context.ReceiverName))
				{
					return {};
				}
				if (!PreprocessorContext.bActiveCode ||
				    (PreprocessorContext.bHasMap && !PreprocessorContext.bHasMappedSource))
				{
					return {};
				}
				const FTextPosition LexicalPosition = PreprocessorContext.bHasMappedSource
				    ? PreprocessorContext.AstPosition : Position;
				// A malformed ordinary literal stops Core's lexer at the newline.
				// Blank only the failed token span, preserving every coordinate and
				// all earlier braces, before recovering namespace/type declarations.
				std::string LexicalSource(PreprocessorContext.bHasMappedSource
				    ? std::string_view(PreprocessorContext.PreprocessedSource) : Source);
				bool bRecovered = false;
				for (int Attempt = 0; Attempt < 4; ++Attempt)
				{
					CheckAnalysisCancellation();
					VeroScriptLexer Lexer(LexicalSource, FileLabel);
					const auto& Tokens = Lexer.Tokenize();
					if (!Lexer.HasError())
					{
						bRecovered = true;
						break;
					}
					if (Tokens.empty()) return {};
					std::size_t Begin = 0, End = 0;
					if (Tokens.size() > 1)
					{
						const auto& Last = Tokens[Tokens.size() - 2];
						if (!FindSourceOffset(LexicalSource, Last.Location.Line, Last.Location.Column, Begin)) return {};
						Begin += Last.Lexeme.size();
					}
					const auto& Error = Tokens.back().Location;
					if (!FindSourceOffset(LexicalSource, Error.Line, Error.Column, End) || End <= Begin) return {};
					for (auto Offset = Begin; Offset < End; ++Offset)
					{
						if ((Offset & 2047u) == 0)
						{
							CheckAnalysisCancellation();
						}
						if (LexicalSource[Offset] != '\n' && LexicalSource[Offset] != '\r')
						{
							LexicalSource[Offset] = ' ';
						}
					}
				}
				if (!bRecovered) return {};
				FQueryLexicalSnapshot LexicalSnapshot(LexicalSource, FileLabel);
				FMemberCompletionContext EmittedContext;
				if (!FindMemberCompletionContext(LexicalSource, FileLabel, LexicalPosition, EmittedContext)
				    || !EmittedContext.bNamespacePrefix || EmittedContext.ReceiverName != Context.ReceiverName
				    || EmittedContext.Prefix != Context.Prefix)
				{
					return {};
				}
				FProgram EmptyProgram;
				EmptyProgram.EntryModulePath = std::string(FileLabel);
				CollectResolvedSourceImports(LexicalSource, FileLabel, Options, EmptyProgram);
				CollectLexicalNamespaceDeclarations(LexicalSource, FileLabel, EmptyProgram);
				// Keep native standard types available even when no program can be
				// recovered. The core catalog is globally available; host declarations
				// still require a successfully recovered program to establish visibility.
				FDocumentSymbolOptions StandardOptions;
				StandardOptions.bIncludeSoftBuiltins = true;
				StandardOptions.bIncludeHostBindings = true;
				StandardOptions.Bindings = &StandardLibrary::GetBindingCatalog();
				const auto Symbols = VeroScriptDocumentSymbols::CollectFromProgram(EmptyProgram, StandardOptions);
				// A standard-looking path may instead name a user type. Without an
				// AST, retain declaration prefixes only to reject that unsafe fallback.
				const std::string First = Context.ReceiverName.substr(0, Context.ReceiverName.find('.'));
				std::unordered_set<std::string> TypePrefixes;
				const auto& Tokens = LexicalSnapshot.Lexer.Tokenize();
				for (std::size_t Index = 1; Index < Tokens.size(); ++Index)
				{
					const auto Before = Tokens[Index - 1].Lexeme;
					if (Tokens[Index].Lexeme != First || (Before != "class" && Before != "struct" && Before != "interface" && Before != "enum" && Before != "using"))
					{
						continue;
					}
					const auto Namespace = FindActiveNamespaceAt(LexicalSource, FileLabel, {Tokens[Index].Location.Line, Tokens[Index].Location.Column});
					TypePrefixes.insert(Namespace.empty() ? First : Namespace + "." + First);
				}
				const bool bRoot = EmittedContext.ReceiverExpression.rfind("::", 0) == 0;
				bool bTypePrefix = bRoot && TypePrefixes.count(First) != 0;
				FNamespaceLookup Names(EmptyProgram);
				const auto Targets = BindNamespacePath(bRoot ? "::" + Context.ReceiverName : Context.ReceiverName,
				    FindActiveNamespaceAt(LexicalSource, FileLabel, LexicalPosition), [&](const std::string& Candidate)
				{
					if (TypePrefixes.count(Candidate))
					{
						bTypePrefix = true;
						return true;
					}
					return StandardLibrary::IsNamespace(Candidate) || Names.Contains(EmptyProgram, Candidate, EmptyProgram.EntryModulePath, nullptr);
				});
				if (bTypePrefix || Targets.size() != 1 || !FindStandardNamespaceMetadata(Targets.front()))
				{
					return {};
				}
				FCompletionResult Result = CompleteLoaded(
				    EmptyProgram,
				    Symbols,
				    LexicalSource,
				    FileLabel,
				    LexicalPosition,
				    Options.Defines,
				    Options.PredefinedMacros,
				    StandardOptions.Bindings);
				// The lexical program uses emitted coordinates. Its edit still replaces
				// only the identifier in the user's original, unexpanded document.
				Result.Prefix = Context.Prefix;
				Result.bHasReplaceRange = Context.bHasReplaceRange;
				Result.ReplaceLine = Position.Line;
				Result.ReplaceStartColumn = Context.ReplaceStartColumn;
				Result.ReplaceEndColumn = Context.ReplaceEndColumn;
				return Result.Items.empty() ? FCompletionResult{} : Result;
			}

			static FCompletionResult TryCompleteSoftMembersLexically(
			    const std::string_view Source,
			    const std::string_view FileLabel,
			    const FTextPosition Position)
			{
				FMemberCompletionContext MemberContext;
				if (!FindMemberCompletionContext(Source, FileLabel, Position, MemberContext))
				{
					return {};
				}

				std::string TypeName = MemberContext.ResolvedTypeName;
				if (TypeName.empty() && !MemberContext.ReceiverName.empty())
				{
					(void)TryLexResolveBindingTypeName(
					    Source,
					    FileLabel,
					    MemberContext.ReceiverName,
					    TypeName);
				}
				if (TypeName.empty())
				{
					return {};
				}

				std::unordered_map<std::string, FInheritedMemberCompletion> MembersByLabel;
				AppendSoftContainerMembers(TypeName, MemberContext.Prefix, MembersByLabel);
				if (MembersByLabel.empty())
				{
					return {};
				}

				FCompletionResult Result;
				Result.bSuccess = true;
				Result.Prefix = MemberContext.Prefix;
				Result.bHasReplaceRange = MemberContext.bHasReplaceRange;
				Result.ReplaceLine = MemberContext.ReplaceLine;
				Result.ReplaceStartColumn = MemberContext.ReplaceStartColumn;
				Result.ReplaceEndColumn = MemberContext.ReplaceEndColumn;
				Result.Items.reserve(MembersByLabel.size());
				for (auto& Pair : MembersByLabel)
				{
					FCompletionItem Item;
					Item.Kind = Pair.second.Kind;
					Item.Label = Pair.second.Label;
					Item.Detail = std::move(Pair.second.Detail);
					Item.InsertText = Pair.second.Label;
					Item.bSoftBuiltin = Pair.second.bSoftBuiltin;
					Result.Items.push_back(std::move(Item));
				}
				FinalizeCompletionItems(
				    Result.Items, ECompletionRankContext::Member, Result.Prefix);
				return Result;
			}

			static FCompletionResult TryCompleteAnnotationsLexically(
			    const std::string_view Source,
			    const std::string_view FileLabel,
			    const FTextPosition Position,
			    const VeroScript::IHostBindingCatalog* const Bindings)
			{
				return CompleteAnnotations(Source, FileLabel, Position, Bindings, false);
			}

			/**
			 * Load-failed fallback for `goto` — mirrors CompleteLoaded's label-only path
			 * using lexical statement labels `name:` (not ternary `? a : b`).
			 * Tolerates a later lex error after the caret.
			 */
			static bool LooksLikeStatementLabelColon(
			    const std::vector<FToken>& Tokens,
			    const std::size_t NameIndex)
			{
				if (NameIndex + 1 >= Tokens.size())
				{
					return false;
				}
				const FToken& Name = Tokens[NameIndex];
				const FToken& Colon = Tokens[NameIndex + 1];
				if (Name.Kind != ETokenKind::Identifier || Colon.Kind != ETokenKind::Symbol || Colon.Lexeme != ":")
				{
					return false;
				}
				if (NameIndex == 0)
				{
					return true;
				}
				const FToken& Prev = Tokens[NameIndex - 1];
				// `case x:` / `default:` — not goto targets.
				if (Prev.Kind == ETokenKind::Keyword && (Prev.Lexeme == "case" || Prev.Lexeme == "default"))
				{
					return false;
				}
				// Ternary `? then : else` — `then` is Ident followed by `:`.
				if (Prev.Kind == ETokenKind::Symbol && Prev.Lexeme == "?")
				{
					return false;
				}
				// Statement boundary: only accept labels that can start a statement.
				return Prev.Kind == ETokenKind::Symbol && (Prev.Lexeme == "{" || Prev.Lexeme == "}" || Prev.Lexeme == ";");
			}

			static FCompletionResult TryCompleteGotoLabelsLexically(
			    const std::string_view Source,
			    const std::string_view FileLabel,
			    const FTextPosition Position)
			{
				FGotoCompletionContext GotoContext;
				if (!FindGotoCompletionContext(Source, FileLabel, Position, GotoContext))
				{
					return {};
				}

				FQueryLexer Lexer(Source, FileLabel);
				const std::vector<FToken>& Tokens = Lexer.Tokenize();
				std::unordered_map<std::string, FCompletionItem> Items;
				for (std::size_t Index = 0; Index + 1 < Tokens.size(); ++Index)
				{
					if (!LooksLikeStatementLabelColon(Tokens, Index))
					{
						continue;
					}
					const std::string Label(Tokens[Index].Lexeme);
					if (!PrefixMatches(Label, GotoContext.Prefix) || Label.empty())
					{
						continue;
					}
					FCompletionItem Item;
					Item.Kind = EDocumentSymbolKind::Label;
					Item.Label = Label;
					Item.Detail = "label";
					Item.InsertText = Label;
					Items.insert_or_assign(Label, std::move(Item));
				}
				if (Items.empty())
				{
					return {};
				}

				FCompletionResult Result;
				Result.bSuccess = true;
				Result.Prefix = GotoContext.Prefix;
				Result.bHasReplaceRange = GotoContext.bHasReplaceRange;
				Result.ReplaceLine = GotoContext.ReplaceLine;
				Result.ReplaceStartColumn = GotoContext.ReplaceStartColumn;
				Result.ReplaceEndColumn = GotoContext.ReplaceEndColumn;
				Result.Items.reserve(Items.size());
				for (auto& Pair : Items)
				{
					Result.Items.push_back(std::move(Pair.second));
				}
				FinalizeCompletionItems(
				    Result.Items, ECompletionRankContext::GotoLabel, Result.Prefix);
				return Result;
			}

			/**
			 * Load-failed fallback for `[capture` — mirrors CompleteLoaded's capture path
			 * with lexical bindings (`T name =|;|,|)` before the caret). Tolerates a later
			 * lex error after the caret.
			 */
			static bool LooksLikeLexicalBindingName(
			    const std::vector<FToken>& Tokens,
			    const std::size_t NameIndex)
			{
				if (NameIndex == 0 || NameIndex >= Tokens.size())
				{
					return false;
				}
				const FToken& Name = Tokens[NameIndex];
				if (Name.Kind != ETokenKind::Identifier)
				{
					return false;
				}
				if (NameIndex + 1 >= Tokens.size())
				{
					return false;
				}
				const FToken& Next = Tokens[NameIndex + 1];
				if (Next.Kind != ETokenKind::Symbol || (Next.Lexeme != "=" && Next.Lexeme != ";" && Next.Lexeme != "," && Next.Lexeme != ")"))
				{
					return false;
				}

				std::size_t TypeIndex = NameIndex - 1;
				if (Tokens[TypeIndex].Kind == ETokenKind::Symbol && Tokens[TypeIndex].Lexeme == "&")
				{
					if (TypeIndex == 0)
					{
						return false;
					}
					--TypeIndex;
				}
				while (TypeIndex > 0 && Tokens[TypeIndex].Kind == ETokenKind::Keyword && (Tokens[TypeIndex].Lexeme == "ref" || Tokens[TypeIndex].Lexeme == "const" || Tokens[TypeIndex].Lexeme == "constexpr"))
				{
					--TypeIndex;
				}
				const FToken& TypeTok = Tokens[TypeIndex];
				if (TypeTok.Kind == ETokenKind::Keyword)
				{
					return VeroScript::IsTypeKeyword(TypeTok.Lexeme) || VeroScript::IsContainerTypeKeyword(TypeTok.Lexeme);
				}
				if (TypeTok.Kind == ETokenKind::Identifier)
				{
					return true;
				}
				return TypeTok.Kind == ETokenKind::Symbol && (TypeTok.Lexeme == ">" || TypeTok.Lexeme == "]");
			}

			static FCompletionResult TryCompleteCaptureLexically(
			    const std::string_view Source,
			    const std::string_view FileLabel,
			    const FTextPosition Position)
			{
				FCaptureCompletionContext CaptureContext;
				if (!FindCaptureCompletionContext(Source, FileLabel, Position, CaptureContext))
				{
					return {};
				}

				std::unordered_map<std::string, FCompletionItem> Items;
				const bool bReferencePrefix = !CaptureContext.Prefix.empty() && CaptureContext.Prefix.front() == '&';
				const std::string_view NamePrefix = bReferencePrefix
				    ? std::string_view(CaptureContext.Prefix).substr(1)
				    : std::string_view(CaptureContext.Prefix);
				if (!bReferencePrefix && PrefixMatches("=", CaptureContext.Prefix))
				{
					FCompletionItem Item;
					Item.Kind = EDocumentSymbolKind::Constant;
					Item.Label = "=";
					Item.Detail = "capture all by value";
					Item.InsertText = "=";
					Items.insert_or_assign("=", std::move(Item));
				}
				if (CaptureContext.Prefix.empty() || CaptureContext.Prefix == "&")
				{
					FCompletionItem Item;
					Item.Kind = EDocumentSymbolKind::Constant;
					Item.Label = "&";
					Item.Detail = "capture all by reference";
					Item.InsertText = "&";
					Items.insert_or_assign("&", std::move(Item));
				}

				FQueryLexer Lexer(Source, FileLabel);
				const std::vector<FToken>& Tokens = Lexer.Tokenize();
				for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
				{
					const FToken& Token = Tokens[Index];
					if (Token.Location.Line > Position.Line || (Token.Location.Line == Position.Line && Token.Location.Column >= Position.Column))
					{
						break;
					}
					if (!LooksLikeLexicalBindingName(Tokens, Index))
					{
						continue;
					}
					const std::string Name(Token.Lexeme);
					if (!PrefixMatches(Name, NamePrefix) || Name.empty())
					{
						continue;
					}
					if (Items.find(Name) != Items.end())
					{
						continue;
					}
					const std::string Label = bReferencePrefix ? ("&" + Name) : Name;
					FCompletionItem Item;
					Item.Kind = EDocumentSymbolKind::Variable;
					Item.Label = Label;
					Item.Detail = bReferencePrefix ? "local, capture by reference" : "local";
					Item.InsertText = Label;
					Items.emplace(Label, std::move(Item));
				}

				if (Items.empty())
				{
					return {};
				}

				FCompletionResult Result;
				Result.bSuccess = true;
				Result.Prefix = CaptureContext.Prefix;
				Result.bHasReplaceRange = CaptureContext.bHasReplaceRange;
				Result.ReplaceLine = CaptureContext.ReplaceLine;
				Result.ReplaceStartColumn = CaptureContext.ReplaceStartColumn;
				Result.ReplaceEndColumn = CaptureContext.ReplaceEndColumn;
				Result.Items.reserve(Items.size());
				for (auto& Pair : Items)
				{
					Result.Items.push_back(std::move(Pair.second));
				}
				FinalizeCompletionItems(
				    Result.Items, ECompletionRankContext::Capture, Result.Prefix);
				return Result;
			}

			static FCompletionResult CompleteWithSource(
			    const std::string_view LoadSource,
			    const std::string_view OriginalSource,
			    const FTextPosition Position,
			    const std::string_view FileLabel,
			    const FCompletionOptions& Options,
			    FScriptError* OutLoadError,
			    FCompletionPreprocessorContext* OutPreprocessorContext = nullptr)
			{
				if (LoadSource == OriginalSource && HasMatchingSemanticSnapshot(OriginalSource, FileLabel, Options.ModuleEnvironment))
				{
					// Completion hides import directives; use the same parsed program with
					// the normal completion symbol options, rather than reloading modules.
					FDocumentSymbolOptions CachedOptions;
					CachedOptions.bIncludeImports = false;
					CachedOptions.bIncludeImportedExports = true;
					CachedOptions.bIncludeSoftBuiltins = true;
					CachedOptions.Bindings = Options.Bindings;
					CachedOptions.bIncludeHostBindings = Options.Bindings != nullptr;
					const auto Symbols = VeroScriptDocumentSymbols::CollectFromProgram(
					    *ActiveSemanticSnapshot->Program, CachedOptions);
					FCompletionResult Result = CompleteLoaded(*ActiveSemanticSnapshot->Program, Symbols,
					    OriginalSource, FileLabel, Position, Options.Defines,
					    Options.PredefinedMacros, Options.Bindings);
					if (OutPreprocessorContext)
					{
						OutPreprocessorContext->Capture(ProgramPreprocessorLineMap(
						    *ActiveSemanticSnapshot->Program, FileLabel), Position,
						    !Result.bSuccess || Result.Items.empty());
					}
					return Result;
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
				SymbolOptions.bIncludeImports = false;
				SymbolOptions.bIncludeImportedExports = true;
				SymbolOptions.bIncludeSoftBuiltins = true;
				SymbolOptions.Bindings = Options.Bindings;
				SymbolOptions.AnnotationProcessing = Options.AnnotationProcessing;
				SymbolOptions.CompileProcessing = Options.CompileProcessing;
				SymbolOptions.ScriptCompileExtensions = Options.ScriptCompileExtensions;
				SymbolOptions.ModuleEnvironment = Options.ModuleEnvironment;
				SymbolOptions.bIncludeHostBindings = Options.Bindings != nullptr;

				VeroScriptModuleLoader Loader("", Options.ModuleEnvironment);
				ApplyModuleLoadingOptions(Loader, Options);
				VeroScript::Tooling::ApplyHostTypeNames(Loader, Options.Bindings);
				ApplyDefines(Loader, Options.Defines);
				ApplyPredefinedMacros(Loader, Options.PredefinedMacros);
				for (const FSourceOverlay& Overlay : Options.SourceOverlays)
				{
					Loader.SetSourceOverlay(Overlay.Path, Overlay.Source);
				}

				std::unique_ptr<FProgram> Program = Loader.LoadSourceModule(LoadSource, FileLabel);
				const auto CapturePreprocessorContext = [&](const bool bRetainSource)
				{
					if (!OutPreprocessorContext) return;
					const auto* Map = Program ? ProgramPreprocessorLineMap(*Program, FileLabel) : nullptr;
					OutPreprocessorContext->Capture(Map ? Map : Loader.GetPreprocessLineMap(FileLabel),
					    Position, bRetainSource);
				};
				if (Program)
				{
					FScriptError Error;
					if (!Detail::PrepareQueryProgram(Program, Options, &Error))
					{
						CapturePreprocessorContext(true);
						if (OutLoadError)
						{
							*OutLoadError = Error;
						}
						return FailCompletion(std::move(Error));
					}
				}
				if (Loader.HasError() || !Program)
				{
					CapturePreprocessorContext(true);
					if (Loader.HasError())
					{
						if (OutLoadError)
						{
							*OutLoadError = Loader.GetError();
						}
						return FailCompletionFromLoader(Loader);
					}
					FScriptError Error;
					FillDiagnostic(
					    Error,
					    "diag.failed_to_load_source_module",
					    FSourceLocation{std::string(FileLabel), 1, 1});
					if (OutLoadError)
					{
						*OutLoadError = Error;
					}
					return FailCompletion(std::move(Error));
				}

				FDocumentSymbolResult SymbolsResult =
				    VeroScriptDocumentSymbols::CollectFromProgram(*Program, SymbolOptions);
				FCompletionResult Result = CompleteLoaded(
				    *Program,
				    SymbolsResult,
				    OriginalSource,
				    FileLabel,
				    Position,
				    Options.Defines,
				    Options.PredefinedMacros,
				    Options.Bindings);
				CapturePreprocessorContext(!Result.bSuccess || Result.Items.empty());
				return Result;
			}

			static FCompletionResult CompleteAtSource(
			    const std::string_view Source,
			    const FTextPosition Position,
			    const std::string_view FileLabel,
			    const FCompletionOptions& Options)
			{
				std::size_t Caret = 0;
				if (!FindSourceOffset(Source, Position.Line, Position.Column, Caret))
				{
					return {};
				}
				const ECompletionLexicalRegion Region = CompletionRegionAt(Source, Caret);
				FImportPathCompletionContext ImportContext;
				if ((Region == ECompletionLexicalRegion::String || Region == ECompletionLexicalRegion::RawString) && FindImportPathCompletionContext(Source, Position, ImportContext))
				{
					return CompleteImportPaths(ImportContext, FileLabel, Options);
				}
				if (Region != ECompletionLexicalRegion::Code)
				{
					return EmptyCompletionAtCaret(Source, Position, Caret);
				}

				FPreprocessorDirectiveCompletionContext DirectiveContext;
				if (FindPreprocessorDirectiveCompletionContext(Source, Position, DirectiveContext))
				{
					FPreprocessLineMap Map;
					std::string Error;
					BuildModulePreprocessorLineMap(Source, FileLabel, Options, Map, Error);
					return CompletePreprocessorDirectives(DirectiveContext, Source, Options.Defines,
					    Options.PredefinedMacros, &Map);
				}
				if (IsHashDirectiveLine(Source, Position))
				{
					FCompletionResult Empty;
					Empty.bSuccess = true;
					return Empty;
				}

				FUsingNamespaceTargetContext NamespaceContext;
				const bool bUsingNamespaceSite = FindUsingNamespaceTargetContext(
				    Source, FileLabel, Position, NamespaceContext);

				// The parser can recover a different statement after an error. Preserve
				// the caret's syntactic role before entering those recovery paths.
				FMemberCompletionContext LexicalMemberContext;
				const bool bTokenMemberSite = FindMemberCompletionContext(
				    Source, FileLabel, Position, LexicalMemberContext);
				const bool bMemberSite = bTokenMemberSite || HasImmediateMemberAccess(Source, Caret);
				if (!bUsingNamespaceSite && !bMemberSite)
				{
					EKeywordCompletionMode Mode =
					    ClassifyKeywordCompletionMode(Source, FileLabel, Position);
					if (Mode != EKeywordCompletionMode::DeclaratorName)
					{
						std::size_t LineStart = Caret;
						while (LineStart > 0 && Source[LineStart - 1] != '\n')
						{
							--LineStart;
						}
						std::size_t LineEnd = Caret;
						while (LineEnd < Source.size() && Source[LineEnd] != '\n')
						{
							++LineEnd;
						}
						const FTextPosition LinePosition{1, Position.Column};
						Mode = ClassifyKeywordCompletionMode(
						    Source.substr(LineStart, LineEnd - LineStart),
						    FileLabel,
						    LinePosition);
					}
					if (Mode == EKeywordCompletionMode::DeclaratorName)
					{
						return EmptyCompletionAtCaret(Source, Position, Caret);
					}
				}
				FQueryLexer ContextLexer(Source, FileLabel);
				const std::vector<FToken>& ContextTokens = ContextLexer.Tokenize();
				if (!bUsingNamespaceSite && (!bMemberSite || bTokenMemberSite)
				    && ContextLexer.HasError() && !ContextTokens.empty())
				{
					const FSourceLocation& ErrorAt = ContextTokens.back().Location;
					if (ErrorAt.Line < Position.Line || (ErrorAt.Line == Position.Line && ErrorAt.Column <= Position.Column))
					{
						return EmptyCompletionAtCaret(Source, Position, Caret);
					}
				}
				FCompletionPreprocessorContext PreprocessorContext;
				auto RestoreSoftNamespaceIfEmpty = [&](FCompletionResult& Candidate)
				{
					if (!bMemberSite || !Candidate.bSuccess || !Candidate.Items.empty())
					{
						return;
					}
					FCompletionResult Soft = TryCompleteSoftNamespaceLexically(
					    Source, FileLabel, Position, Options, false, PreprocessorContext);
					if (Soft.bSuccess)
					{
						Candidate = std::move(Soft);
					}
				};

				FScriptError LoadError;
				FCompletionResult Result =
				    CompleteWithSource(Source, Source, Position, FileLabel, Options, &LoadError, &PreprocessorContext);
				if (!PreprocessorContext.bActiveCode)
				{
					return EmptyCompletionAtCaret(Source, Position, Caret);
				}
				if (bUsingNamespaceSite && (!Result.bSuccess || Result.Items.empty()))
				{
					FProgram LexicalNamespaces;
					const std::string_view LexicalSource = PreprocessorContext.bHasMappedSource
					    ? std::string_view(PreprocessorContext.PreprocessedSource) : Source;
					const FTextPosition LexicalPosition = PreprocessorContext.bHasMappedSource
					    ? PreprocessorContext.AstPosition : Position;
					CollectLexicalNamespaceDeclarations(LexicalSource, FileLabel, LexicalNamespaces);
					for (const FSourceOverlay& Overlay : Options.SourceOverlays)
					{
						CollectLexicalNamespaceDeclarations(
						    Overlay.Source, Overlay.Path, LexicalNamespaces);
					}
					std::vector<FDocumentSymbol> StandardNamespaces;
					AppendStandardNamespaceSymbols(StandardNamespaces);
					FCompletionResult NamespaceResult = CompleteUsingNamespaceTarget(
					    LexicalNamespaces, StandardNamespaces, LexicalSource, FileLabel, LexicalPosition);
					if (!NamespaceResult.Items.empty())
					{
						NamespaceResult.Prefix = NamespaceContext.Prefix;
						NamespaceResult.bHasReplaceRange = NamespaceContext.bHasReplaceRange;
						NamespaceResult.ReplaceLine = NamespaceContext.ReplaceLine;
						NamespaceResult.ReplaceStartColumn = NamespaceContext.ReplaceStartColumn;
						NamespaceResult.ReplaceEndColumn = NamespaceContext.ReplaceEndColumn;
						return NamespaceResult;
					}
				}
				if (!bUsingNamespaceSite && bMemberSite && !bTokenMemberSite)
				{
					const FCompletionResult SoftNamespace = TryCompleteSoftNamespaceLexically(
					    Source, FileLabel, Position, Options, true, PreprocessorContext);
					return SoftNamespace.bSuccess ? SoftNamespace : EmptyCompletionAtCaret(Source, Position, Caret);
				}
				if (Result.bSuccess)
				{
					RestoreSoftNamespaceIfEmpty(Result);
					return Result;
				}
				const FCompletionResult PrimaryFailure = Result;

				std::string Recovered;
				auto TryCompleteRecovered = [&](const std::string& Candidate) -> bool
				{
					FScriptError RecoverError;
					Result = CompleteWithSource(
					    Candidate,
					    Source,
					    Position,
					    FileLabel,
					    Options,
					    &RecoverError);
					RestoreSoftNamespaceIfEmpty(Result);
					return Result.bSuccess;
				};

				bool bTriedSemicolonRecover = false;
				auto TrySemicolonRecover = [&]() -> bool
				{
					if (bTriedSemicolonRecover)
					{
						return false;
					}
					bTriedSemicolonRecover = true;
					return TryRecoverSourceAtExpectedSemicolon(
					           Source,
					           FileLabel,
					           PrimaryFailure.Diagnostics,
					           Recovered) &&
					    TryCompleteRecovered(Recovered);
				};

				// Prefer semicolon recovery when the primary failure looks like an
				// unfinished statement elsewhere — avoids several wasted reparses.
				if (DiagnosticsSuggestMissingSemicolon(FileLabel, PrimaryFailure.Diagnostics) && TrySemicolonRecover())
				{
					return Result;
				}

				// A method-name prefix directly in a type body is intentionally
				// incomplete syntax. Remove only that token for the parser, while
				// keeping the original caret/range for override completion.
				if (ClassifyKeywordCompletionMode(Source, FileLabel, Position) == EKeywordCompletionMode::TypeBody)
				{
					FToken PrefixToken;
					if (FindIdentifierAt(Source, FileLabel, Position, PrefixToken))
					{
						std::size_t Begin = 0;
						std::size_t End = 0;
						if (FindSourceOffset(Source, PrefixToken.Location.Line,
						        PrefixToken.Location.Column, Begin) &&
						    FindSourceOffset(Source, PrefixToken.Location.Line,
						        PrefixToken.Location.Column + static_cast<std::uint32_t>(PrefixToken.Lexeme.size()), End))
						{
							Recovered = std::string(Source);
							Recovered.replace(Begin, End - Begin, End - Begin, ' ');
							if (TryCompleteRecovered(Recovered))
							{
								return Result;
							}
						}
					}
				}

				if (TryRecoverSourceAfterIdentifier(Source, FileLabel, Position, Recovered) && TryCompleteRecovered(Recovered))
				{
					return Result;
				}
				if (TryRecoverSourceAfterMemberDot(Source, FileLabel, Position, Recovered) && TryCompleteRecovered(Recovered))
				{
					return Result;
				}
				if (TryRecoverSourceAfterGoto(Source, FileLabel, Position, Recovered) && TryCompleteRecovered(Recovered))
				{
					return Result;
				}
				if (TryRecoverSourceAfterCapture(Source, FileLabel, Position, Recovered) && TryCompleteRecovered(Recovered))
				{
					return Result;
				}

				if (TrySemicolonRecover())
				{
					return Result;
				}
				if (TryRecoverExecutablePrefix(Source, ContextTokens, Position, Caret, Recovered))
				{
					for (int Attempt = 0; Attempt < 3; ++Attempt)
					{
						if (TryCompleteRecovered(Recovered))
						{
							if (!bMemberSite || !Result.Items.empty())
							{
								return Result;
							}
							// Keep the existing scoped lexical receiver fallback available
							// when the parsed prefix cannot resolve a global/host receiver.
							break;
						}
						std::string Next;
						if (!TryRecoverSourceAtExpectedSemicolon(
						        Recovered, FileLabel, Result.Diagnostics, Next) ||
						    Next == Recovered)
						{
							break;
						}
						Recovered = std::move(Next);
					}
				}
				// Erasing the condition/step is a final parser recovery only. Earlier
				// repairs retain nested expression-lambda parameters and receiver types.
				for (const bool bPrefixOnly : {false, true})
				{
					if (!TryRecoverForHeader(Source, ContextTokens, Position, Caret, bPrefixOnly, Recovered))
					{
						continue;
					}
					for (int Attempt = 0; Attempt < 3; ++Attempt)
					{
						if (TryCompleteRecovered(Recovered))
						{
							if (!bMemberSite || !Result.Items.empty())
							{
								return Result;
							}
							break;
						}
						std::string Next;
						if (!TryRecoverSourceAtExpectedSemicolon(
						        Recovered, FileLabel, Result.Diagnostics, Next) ||
						    Next == Recovered)
						{
							break;
						}
						Recovered = std::move(Next);
					}
				}
				if (bUsingNamespaceSite)
				{
					return EmptyCompletionAtCaret(Source, Position, Caret);
				}

				{
					FGotoCompletionContext GotoContext;
					if (FindGotoCompletionContext(Source, FileLabel, Position, GotoContext))
					{
						const FCompletionResult SoftGoto =
						    TryCompleteGotoLabelsLexically(Source, FileLabel, Position);
						if (SoftGoto.bSuccess && !SoftGoto.Items.empty())
						{
							return SoftGoto;
						}
						// Stay label-only — do not fall through to keyword flood.
						FCompletionResult Empty;
						Empty.bSuccess = true;
						Empty.Prefix = GotoContext.Prefix;
						Empty.bHasReplaceRange = GotoContext.bHasReplaceRange;
						Empty.ReplaceLine = GotoContext.ReplaceLine;
						Empty.ReplaceStartColumn = GotoContext.ReplaceStartColumn;
						Empty.ReplaceEndColumn = GotoContext.ReplaceEndColumn;
						return Empty;
					}
				}

				{
					FCaptureCompletionContext CaptureContext;
					if (FindCaptureCompletionContext(Source, FileLabel, Position, CaptureContext))
					{
						const FCompletionResult SoftCapture =
						    TryCompleteCaptureLexically(Source, FileLabel, Position);
						if (SoftCapture.bSuccess && !SoftCapture.Items.empty())
						{
							return SoftCapture;
						}
						FCompletionResult Empty;
						Empty.bSuccess = true;
						Empty.Prefix = CaptureContext.Prefix;
						Empty.bHasReplaceRange = CaptureContext.bHasReplaceRange;
						Empty.ReplaceLine = CaptureContext.ReplaceLine;
						Empty.ReplaceStartColumn = CaptureContext.ReplaceStartColumn;
						Empty.ReplaceEndColumn = CaptureContext.ReplaceEndColumn;
						return Empty;
					}
				}

				{
					FMemberCompletionContext MemberContext;
					if (FindMemberCompletionContext(Source, FileLabel, Position, MemberContext))
					{
						if (MemberContext.bNamespacePrefix)
						{
							const FCompletionResult SoftNamespace =
							    TryCompleteSoftNamespaceLexically(
							        Source, FileLabel, Position, Options, false, PreprocessorContext);
							if (SoftNamespace.bSuccess)
							{
								return SoftNamespace;
							}
						}
						const FCompletionResult SoftLexical =
						    TryCompleteSoftMembersLexically(Source, FileLabel, Position);
						if (SoftLexical.bSuccess && !SoftLexical.Items.empty())
						{
							return SoftLexical;
						}
						FCompletionResult Empty;
						Empty.bSuccess = true;
						Empty.Prefix = MemberContext.Prefix;
						Empty.bHasReplaceRange = MemberContext.bHasReplaceRange;
						Empty.ReplaceLine = MemberContext.ReplaceLine;
						Empty.ReplaceStartColumn = MemberContext.ReplaceStartColumn;
						Empty.ReplaceEndColumn = MemberContext.ReplaceEndColumn;
						return Empty;
					}
				}

				{
					const FCompletionResult AnnotationArguments =
					    CompleteHostAnnotationArguments(
					        Source, FileLabel, Position, Options.Bindings);
					if (AnnotationArguments.bSuccess)
					{
						return AnnotationArguments;
					}

					const FCompletionResult SoftAnnotations =
					    TryCompleteAnnotationsLexically(
					        Source, FileLabel, Position, Options.Bindings);
					if (SoftAnnotations.bSuccess && !SoftAnnotations.Items.empty())
					{
						return SoftAnnotations;
					}

					// Empty lists are valid (e.g. declarator-name sites suppress keywords).
					FPreprocessLineMap Map;
					std::string Error;
					BuildModulePreprocessorLineMap(Source, FileLabel, Options, Map, Error);
					// Without a program, imports cannot reliably scope the host catalog.
					return MakeKeywordOnlyCompletion(Source, FileLabel, Position, Options.Defines,
					    Options.PredefinedMacros, Options.ModuleEnvironment ? nullptr : Options.Bindings,
					    &Map);
				}
			}

			static FCompletionResult CompleteAtFile(
			    const std::string_view FilePath,
			    const FTextPosition Position,
			    const FCompletionOptions& Options)
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
					return FailCompletion(std::move(Error));
				}
				std::ostringstream Buffer;
				Buffer << Input.rdbuf();
				return CompleteAtSource(Buffer.str(), Position, FilePath, Options);
			}
		};

		bool TokenCoversPosition(const FToken& Token, const FTextPosition Position)
		{
			if (Token.Location.Line != Position.Line)
			{
				return false;
			}
			const std::uint32_t Start = Token.Location.Column;
			const std::uint32_t EndExclusive =
			    Start + static_cast<std::uint32_t>(Token.Lexeme.size());
			return Position.Column >= Start && Position.Column < EndExclusive;
		}

		// Like TokenCoversPosition, plus caret immediately after the token (`mem|`).
		bool TokenCoversPositionOrEnd(const FToken& Token, const FTextPosition Position)
		{
			if (TokenCoversPosition(Token, Position))
			{
				return true;
			}
			if (Token.Location.Line != Position.Line)
			{
				return false;
			}
			const std::uint32_t EndExclusive =
			    Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size());
			return Position.Column == EndExclusive;
		}

		bool FindIdentifierAt(
		    std::string_view Source,
		    std::string_view FileLabel,
		    const FTextPosition Position,
		    FToken& OutToken)
		{
			FQueryLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			// Keep scanning even if the lexer reported a later error — mid-edit buffers
			// still need the identifier under the caret for soft recovery.

			const auto Range = QueryPointTokenRange(Tokens, Position);
			const auto TryToken = [&](const FToken& Token)
			{
				// Word literals also act as prefixes while editing inside their token
				// (`tr|ue`). Their completed ends are suppressed by lexical preflight.
				if (Token.Kind != ETokenKind::Identifier && Token.Kind != ETokenKind::Keyword && Token.Kind != ETokenKind::BooleanLiteral && Token.Kind != ETokenKind::NullptrLiteral)
				{
					return false;
				}
				if (TokenCoversPositionOrEnd(Token, Position))
				{
					OutToken = Token;
					return true;
				}
				return false;
			};
			// Preserve inclusive word ends before considering a token at the caret.
			for (auto Index = Range.first; Index < Range.second; ++Index)
			{
				if (TryToken(Tokens[Index])) return true;
			}
			return false;
		}

		// Identifier or `operator…` declaration span under the caret.
		bool FindQueryTokenAt(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FTextPosition Position,
		    std::string& OutTokenText,
		    std::uint32_t& OutLine,
		    std::uint32_t& OutStartColumn,
		    std::uint32_t& OutEndColumn)
		{
			FQueryLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			// Mid-edit: a later lex fault must not hide the token under the caret.
			const auto Direct = QueryTokenLowerBound(Tokens, Position);
			if (Direct < Tokens.size() && Tokens[Direct].Kind == ETokenKind::Identifier && Tokens[Direct].Location.Line == Position.Line && Tokens[Direct].Location.Column == Position.Column)
			{
				bool Composite = Direct > 0 && (Tokens[Direct - 1].Lexeme == "~" || Tokens[Direct - 1].Lexeme == "@");
				if (!Composite && (!ActiveQueryLexicalSnapshot || !ActiveQueryLexicalSnapshot->Matches(Source, FileLabel) || ActiveQueryLexicalSnapshot->HasOperatorKeyword))
				{
					for (auto I = Direct; I > 0;)
					{
						if ((I & 127u) == 0)
						{
							CheckAnalysisCancellation();
						}
						const auto Text = Tokens[--I].Lexeme;
						if (Text == ";" || Text == "{" || Text == "}")
						{
							break;
						}
						if (Text == "operator")
						{
							Composite = true;
							break;
						}
					}
				}
				if (!Composite)
				{
					const auto& Token = Tokens[Direct];
					OutTokenText.assign(Token.Lexeme);
					OutLine = Token.Location.Line;
					OutStartColumn = Token.Location.Column;
					OutEndColumn = Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size());
					return true;
				}
			}

			std::string OperatorName;
			std::uint32_t OpStart = 1;
			std::uint32_t OpEnd = 1;
			if (FindOperatorNameAt(
			        Tokens,
			        Position.Line,
			        Position.Column,
			        OperatorName,
			        OpStart,
			        OpEnd))
			{
				OutTokenText = std::move(OperatorName);
				OutLine = Position.Line;
				OutStartColumn = OpStart;
				OutEndColumn = OpEnd;
				return true;
			}
			// Before `~` use-site mapping (`operator~`), prefer destructor decls `~Type(`.
			if (FindDestructorNameAt(
			        Tokens,
			        Position.Line,
			        Position.Column,
			        OperatorName,
			        OpStart,
			        OpEnd))
			{
				OutTokenText = std::move(OperatorName);
				OutLine = Position.Line;
				OutStartColumn = OpStart;
				OutEndColumn = OpEnd;
				return true;
			}
			if (FindOperatorUseAt(
			        Tokens,
			        Position.Line,
			        Position.Column,
			        OperatorName,
			        OpStart,
			        OpEnd))
			{
				OutTokenText = std::move(OperatorName);
				OutLine = Position.Line;
				OutStartColumn = OpStart;
				OutEndColumn = OpEnd;
				return true;
			}

			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				const FToken& Token = Tokens[Index];
				const bool bTypeKeyword =
				    Token.Kind == ETokenKind::Keyword && (VeroScript::IsTypeKeyword(Token.Lexeme) || VeroScript::IsContainerTypeKeyword(Token.Lexeme));
				if (Token.Kind != ETokenKind::Identifier && !(Token.Kind == ETokenKind::Keyword && Token.Lexeme == "this") && !bTypeKeyword)
				{
					continue;
				}
				// Match completion: caret may sit on or immediately after the token.
				if (TokenCoversPositionOrEnd(Token, Position))
				{
					// `@name(...)` annotations: include the sigil so soft catalog names
					// (`@rpc`) match, and so the highlight range covers `@`.
					if (Index > 0 && Tokens[Index - 1].Kind == ETokenKind::Symbol && Tokens[Index - 1].Lexeme == "@" && Tokens[Index - 1].Location.Line == Token.Location.Line)
					{
						OutTokenText = "@";
						OutTokenText.append(Token.Lexeme.data(), Token.Lexeme.size());
						OutLine = Tokens[Index - 1].Location.Line;
						OutStartColumn = Tokens[Index - 1].Location.Column;
					}
					else
					{
						OutTokenText.assign(Token.Lexeme.data(), Token.Lexeme.size());
						OutLine = Token.Location.Line;
						OutStartColumn = Token.Location.Column;
					}
					OutEndColumn =
					    Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size());
					return true;
				}
			}
			return false;
		}

		// LHS of `recv +=/-= …` or `owner.member +=/-= …` when caret is on the op.
		bool FindCompoundAssignLeftReceiver(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FTextPosition Position,
		    std::string& OutReceiverName,
		    std::string& OutOwnerName)
		{
			OutReceiverName.clear();
			OutOwnerName.clear();
			FQueryLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			// Same mid-edit policy as FindIdentifierAt / FindMemberCompletionContext.

			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (Token.Kind != ETokenKind::Symbol || (Token.Lexeme != "+=" && Token.Lexeme != "-=") || !TokenCoversPosition(Token, Position) || Index == 0)
				{
					continue;
				}
				const FToken& Left = Tokens[Index - 1];
				const bool bLeftIdent = Left.Kind == ETokenKind::Identifier;
				const bool bLeftThis =
				    Left.Kind == ETokenKind::Keyword && Left.Lexeme == "this";
				if (!bLeftIdent && !bLeftThis)
				{
					continue;
				}
				if (Index >= 3 && Tokens[Index - 2].Kind == ETokenKind::Symbol && IsMemberAccessSymbol(Tokens[Index - 2].Lexeme) && (Tokens[Index - 3].Kind == ETokenKind::Identifier || (Tokens[Index - 3].Kind == ETokenKind::Keyword && Tokens[Index - 3].Lexeme == "this")))
				{
					OutOwnerName.assign(
					    Tokens[Index - 3].Lexeme.data(),
					    Tokens[Index - 3].Lexeme.size());
					OutReceiverName.assign(Left.Lexeme.data(), Left.Lexeme.size());
					return true;
				}
				OutReceiverName.assign(Left.Lexeme.data(), Left.Lexeme.size());
				return true;
			}
			return false;
		}

		bool MatchOpening(
		    const std::vector<FToken>& Tokens,
		    const std::size_t CloseIndex,
		    const char* OpenLexeme,
		    const char* CloseLexeme,
		    std::size_t& OutOpenIndex)
		{
			int Depth = 0;
			int ParenthesisDepth = 0, BracketDepth = 0;
			const bool bAngles = std::string_view(OpenLexeme) == "<";
			for (std::size_t Index = CloseIndex + 1; Index-- > 0;)
			{
				const FToken& Token = Tokens[Index];
				if (Token.Kind != ETokenKind::Symbol)
				{
					continue;
				}
				if (bAngles)
				{
					// Comparisons and shifts in parenthesized value arguments do not
					// close the surrounding generic application (`Buffer<(8 >> 1)>`).
					if (Token.Lexeme == ")")
					{
						++ParenthesisDepth;
						continue;
					}
					if (Token.Lexeme == "]")
					{
						++BracketDepth;
						continue;
					}
					if (Token.Lexeme == "(" && ParenthesisDepth)
					{
						--ParenthesisDepth;
						continue;
					}
					if (Token.Lexeme == "[" && BracketDepth)
					{
						--BracketDepth;
						continue;
					}
					if (ParenthesisDepth || BracketDepth)
					{
						continue;
					}
				}
				if (Token.Lexeme == CloseLexeme)
				{
					++Depth;
				}
				else if (std::string_view(CloseLexeme) == ">" && Token.Lexeme == ">>")
				{
					Depth += 2;
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

		std::string PostfixReceiverExpression(
		    const std::vector<FToken>& Tokens, const std::size_t DotIndex)
		{
			if (DotIndex == 0)
			{
				return {};
			}
			std::size_t Start = DotIndex - 1;
			for (unsigned Depth = 0; Depth < 32; ++Depth)
			{
				const std::string_view Part = Tokens[Start].Lexeme;
				if (Part == ")" || Part == "]")
				{
					std::size_t Open = 0;
					if (!MatchOpening(Tokens, Start, Part == ")" ? "(" : "[", Part == ")" ? ")" : "]", Open))
					{
						return {};
					}
					Start = Open;
					if (Start > 0 && (Tokens[Start - 1].Lexeme == ")" || Tokens[Start - 1].Lexeme == "]"))
					{
						--Start;
						continue;
					}
					if (Start > 0 && (Tokens[Start - 1].Lexeme == ">" || Tokens[Start - 1].Lexeme == ">>"))
					{
						if (!MatchOpening(Tokens, Start - 1, "<", ">", Open))
						{
							return {};
						}
						Start = Open;
					}
					if (Start > 0 && (Tokens[Start - 1].Kind == ETokenKind::Identifier || Tokens[Start - 1].Lexeme == "this"))
					{
						--Start;
					}
				}
				else if (Part == ">" || Part == ">>")
				{
					// A qualifier can itself be a type application (`Outer<T>::`).
					// Retain every balanced segment before parsing the receiver.
					std::size_t Open = 0;
					if (!MatchOpening(Tokens, Start, "<", ">", Open) || Open == 0 || Tokens[Open - 1].Kind != ETokenKind::Identifier)
					{
						return {};
					}
					Start = Open - 1;
				}
				else if (Tokens[Start].Kind != ETokenKind::Identifier && Part != "this")
				{
					return {};
				}
				if (Start > 0 && Tokens[Start - 1].Lexeme == "::" && (Start == 1 || (Tokens[Start - 2].Kind != ETokenKind::Identifier && Tokens[Start - 2].Lexeme != "this" && Tokens[Start - 2].Lexeme != ")" && Tokens[Start - 2].Lexeme != "]" && Tokens[Start - 2].Lexeme != "}" && Tokens[Start - 2].Lexeme != ">" && Tokens[Start - 2].Lexeme != ">>")))
				{
					--Start; // Retain a leading global namespace anchor.
				}
				else if (Start >= 2 && IsMemberAccessSymbol(Tokens[Start - 1].Lexeme))
				{
					Start -= 2;
					continue;
				}
				if (Start > 0 && Tokens[Start - 1].Lexeme == "new")
				{
					--Start;
				}
				const char* Begin = Tokens[Start].Lexeme.data();
				const char* End = Tokens[DotIndex - 1].Lexeme.data() + Tokens[DotIndex - 1].Lexeme.size();
				return End >= Begin && End - Begin <= 65536 ? std::string(Begin, End) : std::string{};
			}
			return {};
		}

		bool FillMemberContextFromReceiverToken(
		    const std::vector<FToken>& Tokens,
		    const std::size_t DotIndex,
		    FMemberCompletionContext& Out)
		{
			if (DotIndex == 0)
			{
				return false;
			}
			const FToken& Receiver = Tokens[DotIndex - 1];
			if (Receiver.Kind == ETokenKind::Symbol && (Receiver.Lexeme == ">" || Receiver.Lexeme == ">>"))
			{
				// Retain the complete type application before `::`, including outer
				// qualifiers and nested arguments. The shared receiver resolver owns
				// alias/arity checking; a closing angle is not an absent receiver.
				Out.ReceiverExpression = PostfixReceiverExpression(Tokens, DotIndex);
				Out.ReceiverName = Out.ReceiverExpression;
				Out.bTypeApplicationPath = true;
				return !Out.ReceiverName.empty();
			}
			if (Receiver.Kind == ETokenKind::Keyword && Receiver.Lexeme == "this")
			{
				Out.ReceiverName = "this";
				Out.ReceiverExpression = "this";
				return true;
			}
			if (Receiver.Kind == ETokenKind::Identifier)
			{
				// Walk `A.B.C.` / `A::B::C::` so namespace prefixes keep the full path.
				std::vector<std::string_view> Parts;
				Parts.push_back(Receiver.Lexeme);
				std::size_t Index = DotIndex - 1;
				while (Index >= 2 && Tokens[Index - 1].Kind == ETokenKind::Symbol && IsMemberAccessSymbol(Tokens[Index - 1].Lexeme) && Tokens[Index - 2].Kind == ETokenKind::Identifier)
				{
					Parts.push_back(Tokens[Index - 2].Lexeme);
					Index -= 2;
				}
				Out.ReceiverName.clear();
				Out.bPostfixReceiver = Index >= 2 && IsMemberAccessSymbol(Tokens[Index - 1].Lexeme) && (Tokens[Index - 2].Lexeme == ")" || Tokens[Index - 2].Lexeme == "]" || Tokens[Index - 2].Lexeme == "}" || Tokens[Index - 2].Lexeme == "this");
				Out.ReceiverExpression = PostfixReceiverExpression(Tokens, DotIndex);
				Out.bTypeApplicationPath = !Out.bPostfixReceiver && Out.ReceiverExpression.find('<') != std::string::npos;
				for (std::size_t PartIndex = Parts.size(); PartIndex > 0; --PartIndex)
				{
					if (!Out.ReceiverName.empty())
					{
						Out.ReceiverName.push_back('.');
					}
					Out.ReceiverName.append(
					    Parts[PartIndex - 1].data(),
					    Parts[PartIndex - 1].size());
				}
				return true;
			}
			if (Receiver.Kind == ETokenKind::Symbol && Receiver.Lexeme == "]")
			{
				Out.ReceiverExpression = PostfixReceiverExpression(Tokens, DotIndex);
				Out.bPostfixReceiver = true;
				std::size_t OpenIndex = 0;
				if (!MatchOpening(Tokens, DotIndex - 1, "[", "]", OpenIndex) || OpenIndex == 0)
				{
					return false;
				}
				const FToken& Collection = Tokens[OpenIndex - 1];
				if (Collection.Kind != ETokenKind::Identifier && !(Collection.Kind == ETokenKind::Keyword && Collection.Lexeme == "this"))
				{
					return !Out.ReceiverExpression.empty();
				}
				Out.ReceiverName.assign(Collection.Lexeme.data(), Collection.Lexeme.size());
				Out.bIndexElement = true;
				return true;
			}
			if (Receiver.Kind == ETokenKind::Symbol && Receiver.Lexeme == ")")
			{
				Out.ReceiverExpression = PostfixReceiverExpression(Tokens, DotIndex);
				std::size_t OpenIndex = 0;
				if (!MatchOpening(Tokens, DotIndex - 1, "(", ")", OpenIndex) || OpenIndex == 0)
				{
					return false;
				}
				const FToken& Before = Tokens[OpenIndex - 1];
				if (Before.Kind != ETokenKind::Identifier)
				{
					return !Out.ReceiverExpression.empty();
				}
				// Collect `A.B.C(` / `A::B::C(` so ctor vs method can be disambiguated later.
				std::vector<std::string_view> Parts;
				Parts.push_back(Before.Lexeme);
				std::size_t Index = OpenIndex - 1;
				while (Index >= 2 && Tokens[Index - 1].Kind == ETokenKind::Symbol && IsMemberAccessSymbol(Tokens[Index - 1].Lexeme) && Tokens[Index - 2].Kind == ETokenKind::Identifier)
				{
					Parts.push_back(Tokens[Index - 2].Lexeme);
					Index -= 2;
				}
				auto JoinParts = [](const std::vector<std::string_view>& Segs,
				                     const std::size_t Begin,
				                     const std::size_t EndExclusive)
				{
					std::string Joined;
					for (std::size_t PartIndex = EndExclusive; PartIndex > Begin; --PartIndex)
					{
						if (!Joined.empty())
						{
							Joined.push_back('.');
						}
						Joined.append(Segs[PartIndex - 1].data(), Segs[PartIndex - 1].size());
					}
					return Joined;
				};
				if (Parts.size() >= 2)
				{
					// `recv.method()` or `Ns.Type()` — prefer method-return; callers fall
					// back to treating the full qualifier as a constructed type.
					Out.MethodName.assign(Parts.front().data(), Parts.front().size());
					Out.ReceiverName = JoinParts(Parts, 1, Parts.size());
					Out.bMethodReturn = true;
					return true;
				}
				// Bare `Type()` / `func()` — treat return as the callee type name.
				Out.ResolvedTypeName.assign(Before.Lexeme.data(), Before.Lexeme.size());
				Out.ReceiverName = Out.ResolvedTypeName;
				return true;
			}
			if (Receiver.Kind == ETokenKind::Symbol && Receiver.Lexeme == "}")
			{
				std::size_t OpenIndex = 0;
				if (!MatchOpening(Tokens, DotIndex - 1, "{", "}", OpenIndex) || OpenIndex == 0)
				{
					return false;
				}

				std::size_t TypeStart = OpenIndex - 1;
				if (Tokens[TypeStart].Kind == ETokenKind::Symbol && (Tokens[TypeStart].Lexeme == ">" || Tokens[TypeStart].Lexeme == ">>"))
				{
					std::size_t GenericOpen = 0;
					if (!MatchOpening(Tokens, TypeStart, "<", ">", GenericOpen) || GenericOpen == 0)
					{
						return false;
					}
					TypeStart = GenericOpen - 1;
				}
				const FToken& BaseType = Tokens[TypeStart];
				if (BaseType.Kind != ETokenKind::Identifier && !(BaseType.Kind == ETokenKind::Keyword && (VeroScript::IsTypeKeyword(BaseType.Lexeme) || VeroScript::IsContainerTypeKeyword(BaseType.Lexeme))))
				{
					return false;
				}
				while (TypeStart >= 2 && Tokens[TypeStart - 1].Kind == ETokenKind::Symbol && IsMemberAccessSymbol(Tokens[TypeStart - 1].Lexeme) && Tokens[TypeStart - 2].Kind == ETokenKind::Identifier)
				{
					TypeStart -= 2;
				}

				for (std::size_t Index = TypeStart; Index < OpenIndex; ++Index)
				{
					if (Tokens[Index].Kind == ETokenKind::Symbol && IsMemberAccessSymbol(Tokens[Index].Lexeme))
					{
						Out.ResolvedTypeName.push_back('.');
					}
					else
					{
						Out.ResolvedTypeName.append(
						    Tokens[Index].Lexeme.data(),
						    Tokens[Index].Lexeme.size());
					}
				}
				Out.ReceiverName = Out.ResolvedTypeName;
				return !Out.ResolvedTypeName.empty();
			}
			return false;
		}

		bool FindMemberCompletionContext(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FTextPosition Position,
		    FMemberCompletionContext& Out)
		{
			Out = {};
			FQueryLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			// Keep using tokens before a later lex error — same policy as FindIdentifierAt
			// (mid-edit buffers often break after the caret).
			const auto Range = QueryPointTokenRange(Tokens, Position);

			for (std::size_t Index = Range.first; Index < Range.second; ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (Token.Kind != ETokenKind::Identifier || !TokenCoversPositionOrEnd(Token, Position))
				{
					continue;
				}
				if (Index < 2)
				{
					return false;
				}
				const FToken& Dot = Tokens[Index - 1];
				if (Dot.Kind != ETokenKind::Symbol || !IsMemberAccessSymbol(Dot.Lexeme))
				{
					return false;
				}
				if (!FillMemberContextFromReceiverToken(Tokens, Index - 1, Out))
				{
					return false;
				}

				Out.bActive = true;
				Out.bHasReplaceRange = true;
				Out.ReplaceLine = Token.Location.Line;
				Out.ReplaceStartColumn = Token.Location.Column;
				Out.ReplaceEndColumn =
				    Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size());
				const std::uint32_t Relative =
				    Position.Column >= Token.Location.Column
				    ? (Position.Column - Token.Location.Column)
				    : 0;
				const std::size_t PrefixLen =
				    Relative <= Token.Lexeme.size() ? Relative : Token.Lexeme.size();
				Out.Prefix = std::string(Token.Lexeme.substr(0, PrefixLen));
				Out.bNamespacePrefix = Dot.Lexeme == "::" && FindStandardNamespaceMetadata(Out.ReceiverName);
				Out.DotInsertLine = Dot.Location.Line;
				Out.DotInsertColumn =
				    Dot.Location.Column + static_cast<std::uint32_t>(Dot.Lexeme.size());
				return true;
			}

			for (std::size_t Index = Range.first; Index < Range.second; ++Index)
			{
				const FToken& Dot = Tokens[Index];
				if (Dot.Kind != ETokenKind::Symbol || !IsMemberAccessSymbol(Dot.Lexeme) || Dot.Location.Line > Position.Line || Index == 0)
				{
					continue;
				}
				const std::uint32_t DotEnd =
				    Dot.Location.Column + static_cast<std::uint32_t>(Dot.Lexeme.size());
				if (Dot.Location.Line == Position.Line && Position.Column < DotEnd)
				{
					continue;
				}

				if (Index + 1 < Tokens.size())
				{
					const FToken& Next = Tokens[Index + 1];
					if (Next.Kind == ETokenKind::Identifier && TokenCoversPosition(Next, Position))
					{
						continue;
					}
					if (Next.Kind != ETokenKind::EndOfFile && (Next.Location.Line < Position.Line || (Next.Location.Line == Position.Line && Position.Column > Next.Location.Column)))
					{
						continue;
					}
				}

				// Resolve only the access at the caret; unrelated call chains can
				// contain large argument lists and need no receiver analysis here.
				FMemberCompletionContext Candidate;
				if (!FillMemberContextFromReceiverToken(Tokens, Index, Candidate))
				{
					continue;
				}
				Out = std::move(Candidate);
				Out.bActive = true;
				Out.Prefix.clear();
				Out.bHasReplaceRange = false;
				Out.bNamespacePrefix = Dot.Lexeme == "::" && FindStandardNamespaceMetadata(Out.ReceiverName);
				Out.DotInsertLine = Dot.Location.Line;
				Out.DotInsertColumn = DotEnd;
				return true;
			}

			return false;
		}

		int CompareLocation(const FSourceLocation& Location, const FTextPosition Position)
		{
			if (Location.Line < Position.Line)
			{
				return -1;
			}
			if (Location.Line > Position.Line)
			{
				return 1;
			}
			if (Location.Column < Position.Column)
			{
				return -1;
			}
			if (Location.Column > Position.Column)
			{
				return 1;
			}
			return 0;
		}

		bool LocationAtOrBefore(const FSourceLocation& Location, const FTextPosition Position)
		{
			return CompareLocation(Location, Position) <= 0;
		}

		FSourceLocation MaxLocation()
		{
			FSourceLocation Location;
			Location.Line = std::numeric_limits<std::uint32_t>::max();
			Location.Column = std::numeric_limits<std::uint32_t>::max();
			return Location;
		}

		bool RangeContainsExclusiveEnd(
		    const FSourceLocation& Start,
		    const FSourceLocation& EndExclusive,
		    const FTextPosition Position)
		{
			if (CompareLocation(Start, Position) > 0)
			{
				return false;
			}
			if (EndExclusive.Line == std::numeric_limits<std::uint32_t>::max() && EndExclusive.Column == std::numeric_limits<std::uint32_t>::max())
			{
				return true;
			}
			return CompareLocation(EndExclusive, Position) > 0;
		}

		bool StatementCompletedBefore(
		    const FSourceLocation& EndExclusive,
		    const FTextPosition Position)
		{
			if (EndExclusive.Line == std::numeric_limits<std::uint32_t>::max() && EndExclusive.Column == std::numeric_limits<std::uint32_t>::max())
			{
				return false;
			}
			return CompareLocation(EndExclusive, Position) <= 0;
		}

		void AdvanceContentEnd(FSourceLocation& End, const FSourceLocation& Candidate)
		{
			if (Candidate.Line == 0)
			{
				return;
			}
			const FTextPosition AsPos{Candidate.Line, Candidate.Column};
			if (CompareLocation(End, AsPos) < 0)
			{
				End = Candidate;
			}
		}

		void ExtendContentEndByExpr(const FExpr& Expr, FSourceLocation& End);
		void ExtendContentEndByStmt(const FStmt& Statement, FSourceLocation& End);

		void ExtendContentEndByBlock(const FBlockStmt& Block, FSourceLocation& End)
		{
			AdvanceContentEnd(End, Block.Location);
			for (const std::unique_ptr<FStmt>& Statement : Block.Statements)
			{
				if (Statement)
				{
					ExtendContentEndByStmt(*Statement, End);
				}
			}
		}

		void ExtendContentEndByExpr(const FExpr& Expr, FSourceLocation& End)
		{
			AdvanceContentEnd(End, Expr.Location);
			switch (Expr.Kind)
			{
			case EExprKind::Identifier:
				{
					const auto& Identifier = static_cast<const FIdentifierExpr&>(Expr);
					FSourceLocation TokenEnd = Expr.Location;
					// Qualified identifiers are located at their final source name.
					// Canonical standard builtin names can differ from that source token.
					if (!Identifier.bStandardLibraryQualified)
					{
						const std::string_view Name = std::string_view(Identifier.Name).substr(0, Identifier.Name.find('<'));
						const std::size_t Scope = Name.rfind('.');
						TokenEnd.Column += static_cast<std::uint32_t>(
						    Scope == std::string_view::npos ? Name.size() : Name.size() - Scope - 1);
					}
					AdvanceContentEnd(End, TokenEnd);
					break;
				}
			case EExprKind::Unary:
				{
					if (static_cast<const FUnaryExpr&>(Expr).Operand)
					{
						ExtendContentEndByExpr(*static_cast<const FUnaryExpr&>(Expr).Operand, End);
					}
					break;
				}
			case EExprKind::Binary:
				{
					const auto& Binary = static_cast<const FBinaryExpr&>(Expr);
					if (Binary.Left)
					{
						ExtendContentEndByExpr(*Binary.Left, End);
					}
					if (Binary.Right)
					{
						ExtendContentEndByExpr(*Binary.Right, End);
					}
					break;
				}
			case EExprKind::Assign:
				{
					const auto& Assign = static_cast<const FAssignExpr&>(Expr);
					if (Assign.Target)
					{
						ExtendContentEndByExpr(*Assign.Target, End);
					}
					if (Assign.Value)
					{
						ExtendContentEndByExpr(*Assign.Value, End);
					}
					break;
				}
			case EExprKind::Conditional:
				{
					const auto& Cond = static_cast<const FConditionalExpr&>(Expr);
					if (Cond.Condition)
					{
						ExtendContentEndByExpr(*Cond.Condition, End);
					}
					if (Cond.ThenExpr)
					{
						ExtendContentEndByExpr(*Cond.ThenExpr, End);
					}
					if (Cond.ElseExpr)
					{
						ExtendContentEndByExpr(*Cond.ElseExpr, End);
					}
					break;
				}
			case EExprKind::Ref:
				{
					if (static_cast<const FRefExpr&>(Expr).Operand)
					{
						ExtendContentEndByExpr(*static_cast<const FRefExpr&>(Expr).Operand, End);
					}
					break;
				}
			case EExprKind::Call:
				{
					const auto& Call = static_cast<const FCallExpr&>(Expr);
					if (Call.CalleeExpr)
					{
						ExtendContentEndByExpr(*Call.CalleeExpr, End);
					}
					for (const std::unique_ptr<FExpr>& Arg : Call.Arguments)
					{
						if (Arg)
						{
							ExtendContentEndByExpr(*Arg, End);
						}
					}
					break;
				}
			case EExprKind::MemberAccess:
				{
					FSourceLocation MemberEnd = Expr.Location;
					MemberEnd.Column += static_cast<std::uint32_t>(
					    static_cast<const FMemberAccessExpr&>(Expr).Member.size());
					AdvanceContentEnd(End, MemberEnd);
					if (static_cast<const FMemberAccessExpr&>(Expr).Object)
					{
						ExtendContentEndByExpr(
						    *static_cast<const FMemberAccessExpr&>(Expr).Object,
						    End);
					}
					break;
				}
			case EExprKind::MethodCall:
				{
					const auto& MethodCall = static_cast<const FMethodCallExpr&>(Expr);
					FSourceLocation MethodEnd = Expr.Location;
					MethodEnd.Column += static_cast<std::uint32_t>(MethodCall.Method.size());
					AdvanceContentEnd(End, MethodEnd);
					if (MethodCall.Object)
					{
						ExtendContentEndByExpr(*MethodCall.Object, End);
					}
					for (const std::unique_ptr<FExpr>& Arg : MethodCall.Arguments)
					{
						if (Arg)
						{
							ExtendContentEndByExpr(*Arg, End);
						}
					}
					break;
				}
			case EExprKind::IndexAccess:
				{
					const auto& Index = static_cast<const FIndexAccessExpr&>(Expr);
					if (Index.Object)
					{
						ExtendContentEndByExpr(*Index.Object, End);
					}
					if (Index.Index)
					{
						ExtendContentEndByExpr(*Index.Index, End);
					}
					break;
				}
			case EExprKind::ArrayInit:
				{
					for (const std::unique_ptr<FExpr>& Elem :
					    static_cast<const FArrayInitExpr&>(Expr).Elements)
					{
						if (Elem)
						{
							ExtendContentEndByExpr(*Elem, End);
						}
					}
					break;
				}
			case EExprKind::MapInit:
				{
					for (const FMapInitEntry& Entry : static_cast<const FMapInitExpr&>(Expr).Entries)
					{
						if (Entry.Key)
						{
							ExtendContentEndByExpr(*Entry.Key, End);
						}
						if (Entry.Value)
						{
							ExtendContentEndByExpr(*Entry.Value, End);
						}
					}
					break;
				}
			case EExprKind::Lambda:
				{
					const auto& Lambda = static_cast<const FLambdaExpr&>(Expr);
					for (const auto& Capture : Lambda.Captures)
					{
						if (Capture.Initializer)
						{
							ExtendContentEndByExpr(*Capture.Initializer, End);
						}
					}
					if (Lambda.Body)
					{
						ExtendContentEndByBlock(*Lambda.Body, End);
					}
					if (Lambda.ExpressionBody)
					{
						ExtendContentEndByExpr(*Lambda.ExpressionBody, End);
					}
					break;
				}
			default:
				{
					break;
				}
			}
		}

		void ExtendContentEndByStmt(const FStmt& Statement, FSourceLocation& End)
		{
			AdvanceContentEnd(End, Statement.Location);
			switch (Statement.Kind)
			{
			case EStmtKind::VarDecl:
				{
					if (static_cast<const FVarDeclStmt&>(Statement).Initializer)
					{
						ExtendContentEndByExpr(
						    *static_cast<const FVarDeclStmt&>(Statement).Initializer,
						    End);
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
							ExtendContentEndByStmt(*Declaration, End);
						}
					}
					break;
				}
			case EStmtKind::Expr:
				{
					if (static_cast<const FExprStmt&>(Statement).Expression)
					{
						ExtendContentEndByExpr(
						    *static_cast<const FExprStmt&>(Statement).Expression,
						    End);
					}
					break;
				}
			case EStmtKind::Return:
				{
					if (static_cast<const FReturnStmt&>(Statement).Value)
					{
						ExtendContentEndByExpr(
						    *static_cast<const FReturnStmt&>(Statement).Value,
						    End);
					}
					break;
				}
			case EStmtKind::Throw:
				{
					if (static_cast<const FThrowStmt&>(Statement).Value)
					{
						ExtendContentEndByExpr(
						    *static_cast<const FThrowStmt&>(Statement).Value,
						    End);
					}
					break;
				}
			case EStmtKind::Try:
				{
					const auto& TryStmt = static_cast<const FTryStmt&>(Statement);
					if (TryStmt.TryBlock)
					{
						ExtendContentEndByBlock(*TryStmt.TryBlock, End);
					}
					for (const FCatchClause& Clause : TryStmt.Catches)
					{
						if (Clause.Body)
						{
							ExtendContentEndByBlock(*Clause.Body, End);
						}
					}
					if (TryStmt.FinallyBlock)
					{
						ExtendContentEndByBlock(*TryStmt.FinallyBlock, End);
					}
					break;
				}
			case EStmtKind::Block:
				{
					ExtendContentEndByBlock(static_cast<const FBlockStmt&>(Statement), End);
					break;
				}
			case EStmtKind::If:
				{
					const auto& IfStmt = static_cast<const FIfStmt&>(Statement);
					if (IfStmt.Condition)
					{
						ExtendContentEndByExpr(*IfStmt.Condition, End);
					}
					if (IfStmt.ThenBranch)
					{
						ExtendContentEndByBlock(*IfStmt.ThenBranch, End);
					}
					if (IfStmt.ElseBranch)
					{
						ExtendContentEndByBlock(*IfStmt.ElseBranch, End);
					}
					break;
				}
			case EStmtKind::While:
				{
					const auto& WhileStmt = static_cast<const FWhileStmt&>(Statement);
					if (WhileStmt.Condition)
					{
						ExtendContentEndByExpr(*WhileStmt.Condition, End);
					}
					if (WhileStmt.Body)
					{
						ExtendContentEndByBlock(*WhileStmt.Body, End);
					}
					break;
				}
			case EStmtKind::DoWhile:
				{
					const auto& DoWhileStmt = static_cast<const FDoWhileStmt&>(Statement);
					if (DoWhileStmt.Body)
					{
						ExtendContentEndByBlock(*DoWhileStmt.Body, End);
					}
					if (DoWhileStmt.Condition)
					{
						ExtendContentEndByExpr(*DoWhileStmt.Condition, End);
					}
					break;
				}
			case EStmtKind::For:
				{
					const auto& ForStmt = static_cast<const FForStmt&>(Statement);
					if (ForStmt.Init)
					{
						ExtendContentEndByStmt(*ForStmt.Init, End);
					}
					for (const std::unique_ptr<FExpr>& InitExpression :
					    ForStmt.InitExpressions)
					{
						if (InitExpression)
						{
							ExtendContentEndByExpr(*InitExpression, End);
						}
					}
					if (ForStmt.Condition)
					{
						ExtendContentEndByExpr(*ForStmt.Condition, End);
					}
					if (ForStmt.Step)
					{
						ExtendContentEndByExpr(*ForStmt.Step, End);
					}
					for (const std::unique_ptr<FExpr>& StepExpression :
					    ForStmt.StepExpressions)
					{
						if (StepExpression)
						{
							ExtendContentEndByExpr(*StepExpression, End);
						}
					}
					if (ForStmt.Body)
					{
						ExtendContentEndByBlock(*ForStmt.Body, End);
					}
					break;
				}
			case EStmtKind::ForEach:
				{
					const auto& ForEachStmt = static_cast<const FForEachStmt&>(Statement);
					if (ForEachStmt.Collection)
					{
						ExtendContentEndByExpr(*ForEachStmt.Collection, End);
					}
					if (ForEachStmt.Body)
					{
						ExtendContentEndByBlock(*ForEachStmt.Body, End);
					}
					break;
				}
			case EStmtKind::Switch:
				{
					const auto& SwitchStmt = static_cast<const FSwitchStmt&>(Statement);
					if (SwitchStmt.Discriminant)
					{
						ExtendContentEndByExpr(*SwitchStmt.Discriminant, End);
					}
					for (const FSwitchCase& Case : SwitchStmt.Cases)
					{
						if (Case.Body)
						{
							ExtendContentEndByBlock(*Case.Body, End);
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

		FSourceLocation SoftExclusiveEnd(
		    const FSourceLocation& ContentEnd,
		    const FSourceLocation& FallbackExclusive)
		{
			if (FallbackExclusive.Line != std::numeric_limits<std::uint32_t>::max() || FallbackExclusive.Column != std::numeric_limits<std::uint32_t>::max())
			{
				const FTextPosition ContentPos{ContentEnd.Line, ContentEnd.Column};
				if (CompareLocation(FallbackExclusive, ContentPos) > 0)
				{
					return FallbackExclusive;
				}
			}
			FSourceLocation Soft = ContentEnd;
			if (Soft.Column < std::numeric_limits<std::uint32_t>::max())
			{
				++Soft.Column;
			}
			else if (Soft.Line < std::numeric_limits<std::uint32_t>::max())
			{
				++Soft.Line;
				Soft.Column = 1;
			}
			return Soft;
		}

		FSourceLocation ExclusiveEndForStmt(
		    const FStmt& Statement,
		    const FSourceLocation& FallbackExclusive)
		{
			FSourceLocation Content = Statement.Location;
			ExtendContentEndByStmt(Statement, Content);
			return SoftExclusiveEnd(Content, FallbackExclusive);
		}

		FSourceLocation ExclusiveEndForExpr(
		    const FExpr& Expr,
		    const FSourceLocation& FallbackExclusive)
		{
			FSourceLocation Content = Expr.Location;
			ExtendContentEndByExpr(Expr, Content);
			return SoftExclusiveEnd(Content, FallbackExclusive);
		}

		FSourceLocation ExclusiveEndForBlock(
		    const FBlockStmt& Block,
		    const FSourceLocation& FallbackExclusive)
		{
			FSourceLocation Content = Block.Location;
			ExtendContentEndByBlock(Block, Content);
			return SoftExclusiveEnd(Content, FallbackExclusive);
		}
