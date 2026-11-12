// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		struct FMemberCompletionContext
		{
			bool bActive = false;
			std::string ReceiverName;
			// Complete postfix receiver, retained for standard memory call chains.
			std::string ReceiverExpression;
			// A generic path can end in either a nested type or a static value.
			bool bTypeApplicationPath = false;
			// True when a name path is preceded by a call/index/other value expression.
			bool bPostfixReceiver = false;
			// When set, skip ResolveReceiverTypeName and use this type directly.
			std::string ResolvedTypeName;
			std::string Prefix;
			bool bHasReplaceRange = false;
			// True when completing after `expr].` (index element type).
			bool bIndexElement = false;
			// True when completing after `recv.method(...).` / soft method return.
			bool bMethodReturn = false;
			std::string MethodName;
			bool bNamespacePrefix = false;
			std::uint32_t ReplaceLine = 1;
			std::uint32_t ReplaceStartColumn = 1;
			std::uint32_t ReplaceEndColumn = 1;
			std::uint32_t DotInsertLine = 1;
			std::uint32_t DotInsertColumn = 1;
		};

		enum class EInitializedCaptureRole
		{
			None,
			Declaration,
			Initializer,
			ValueInitializer
		};
		EInitializedCaptureRole FindInitializedCaptureRole(const FProgram& Program, FTextPosition Position);

		bool FindIdentifierAt(
		    std::string_view Source,
		    std::string_view FileLabel,
		    FTextPosition Position,
		    FToken& OutToken);
		bool FindMemberCompletionContext(
		    std::string_view Source,
		    std::string_view FileLabel,
		    FTextPosition Position,
		    FMemberCompletionContext& Out);

		void CollectVisibleLocals(
		    const FProgram& Program,
		    const FTextPosition Position,
		    std::vector<FSymbolQueryHit>& OutHits);
		std::string ResolveReceiverTypeApplication(const FProgram& Program, FTextPosition Position,
		    std::string_view Expression, const VeroScript::IHostBindingCatalog* Bindings,
		    std::string_view ActiveNamespace, const std::vector<FSymbolQueryHit>& Locals);
		std::string ResolveReceiverExpressionType(const FProgram& Program,
		    const FDocumentSymbolResult& Symbols, FTextPosition Position, std::string_view Expression,
		    const VeroScript::IHostBindingCatalog* Bindings, std::string_view ActiveNamespace,
		    const std::vector<FSymbolQueryHit>* PreLocals = nullptr);
		bool IsKnownValueReceiver(const FProgram& Program, FTextPosition Position,
		    std::string_view Name, const std::vector<FSymbolQueryHit>& Locals,
		    const FDocumentSymbolResult& Symbols, std::string_view ActiveNamespace);
		void RefineCallableLocalType(const FProgram& Program, const FDocumentSymbolResult& Symbols,
		    const IHostBindingCatalog* Bindings, FSymbolQueryHit& Hit);
		FSymbolQueryResult QueryLoaded(
		    const FProgram& Program,
		    const FDocumentSymbolResult& SymbolsResult,
		    std::string_view Source,
		    std::string_view FileLabel,
		    FTextPosition Position,
		    const std::vector<std::string_view>& ImportPaths,
		    const std::vector<std::string_view>& Defines,
		    const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros,
		    const VeroScript::IHostBindingCatalog* Bindings,
		    const VeroScript::FPreprocessLineMap* ExistingLineMap,
		    bool bResolveGenericTypeParameters);
		std::string ResolveReceiverTypeName(
		    const FProgram& Program,
		    const FDocumentSymbolResult& SymbolsResult,
		    const FTextPosition Position,
		    const std::string& ReceiverName,
		    const std::vector<FSymbolQueryHit>* PreLocals = nullptr);
		// When `Ns.Type()` was parsed as method-return, recover the constructed type.
		std::string ResolveQualifiedCtorTypeName(
		    const FProgram& Program,
		    const FDocumentSymbolResult& SymbolsResult,
		    const std::string& ReceiverName,
		    const std::string& CalleeName);

		bool FindSourceOffset(
		    const std::string_view Source,
		    const std::uint32_t Line,
		    const std::uint32_t Column,
		    std::size_t& OutOffset)
		{
			if (ActiveQueryLexicalSnapshot && ActiveQueryLexicalSnapshot->MatchesSource(Source))
			{
				return ActiveQueryLexicalSnapshot->SourceOffset({Line, Column}, OutOffset);
			}
			std::uint32_t CurrentLine = 1;
			std::uint32_t CurrentColumn = 1;
			for (std::size_t Index = 0; Index <= Source.size(); ++Index)
			{
				if (CurrentLine == Line && CurrentColumn == Column)
				{
					OutOffset = Index;
					return true;
				}
				if (Index == Source.size())
				{
					break;
				}
				if (Source[Index] == '\n')
				{
					++CurrentLine;
					CurrentColumn = 1;
				}
				else
				{
					++CurrentColumn;
				}
			}
			return false;
		}

		const VeroScript::FHostAnnotationDesc* FindEnclosingHostAnnotationAtOffset(
		    const std::string_view Source,
		    const std::size_t Offset,
		    const VeroScript::IHostBindingCatalog* const Bindings,
		    std::string* const OutArgumentPath = nullptr)
		{
			if (OutArgumentPath)
			{
				OutArgumentPath->clear();
			}
			if (!Bindings || Offset > Source.size())
			{
				return nullptr;
			}
			const auto FindAnnotation =
			    [Bindings](const std::string_view Name)
			    -> const VeroScript::FHostAnnotationDesc*
			{
				for (const VeroScript::FHostAnnotationDesc& Annotation :
				    Bindings->GetAnnotations())
				{
					if (VeroScript::AnnotationNamesEqual(Annotation.Name, Name, Annotation.CaseSensitive))
					{
						return &Annotation;
					}
				}
				return nullptr;
			};
			struct FAnnotationParenContext
			{
				const VeroScript::FHostAnnotationDesc* Annotation = nullptr;
				std::string ArgumentPath;
			};
			std::vector<FAnnotationParenContext> ParenContexts;
			bool bInLineComment = false;
			bool bInBlockComment = false;
			bool bInString = false;
			bool bInChar = false;
			bool bEscape = false;
			for (std::size_t Index = 0; Index < Offset; ++Index)
			{
				const char C = Source[Index];
				const char Next =
				    Index + 1 < Offset ? Source[Index + 1] : '\0';
				if (bInLineComment)
				{
					if (C == '\n')
					{
						bInLineComment = false;
					}
					continue;
				}
				if (bInBlockComment)
				{
					if (C == '*' && Next == '/')
					{
						bInBlockComment = false;
						++Index;
					}
					continue;
				}
				if (bInString || bInChar)
				{
					if (bEscape)
					{
						bEscape = false;
					}
					else if (C == '\\')
					{
						bEscape = true;
					}
					else if ((bInString && C == '"') || (bInChar && C == '\''))
					{
						bInString = false;
						bInChar = false;
					}
					continue;
				}
				if (C == '/' && Next == '/')
				{
					bInLineComment = true;
					++Index;
					continue;
				}
				if (C == '/' && Next == '*')
				{
					bInBlockComment = true;
					++Index;
					continue;
				}
				if (C == '"')
				{
					bInString = true;
					continue;
				}
				if (C == '\'')
				{
					bInChar = true;
					continue;
				}
				if (C == '(')
				{
					std::size_t End = Index;
					while (End > 0 && std::isspace(static_cast<unsigned char>(Source[End - 1])))
					{
						--End;
					}
					std::size_t Start = End;
					while (Start > 0)
					{
						const unsigned char Previous =
						    static_cast<unsigned char>(Source[Start - 1]);
						if (!std::isalnum(Previous) && Previous != '_')
						{
							break;
						}
						--Start;
					}
					if (Start > 0 && Source[Start - 1] == '@')
					{
						--Start;
					}
					const VeroScript::FHostAnnotationDesc* Annotation =
					    FindAnnotation(Source.substr(Start, End - Start));
					std::string ArgumentPath;
					if (!Annotation && !ParenContexts.empty() && ParenContexts.back().Annotation && End > 0 && Source[End - 1] == '=')
					{
						std::size_t ArgumentEnd = End - 1;
						while (ArgumentEnd > 0 && std::isspace(static_cast<unsigned char>(Source[ArgumentEnd - 1])))
						{
							--ArgumentEnd;
						}
						std::size_t ArgumentStart = ArgumentEnd;
						while (ArgumentStart > 0)
						{
							const unsigned char Previous =
							    static_cast<unsigned char>(Source[ArgumentStart - 1]);
							if (!std::isalnum(Previous) && Previous != '_')
							{
								break;
							}
							--ArgumentStart;
						}
						const std::string_view ArgumentName =
						    Source.substr(ArgumentStart, ArgumentEnd - ArgumentStart);
						for (const VeroScript::FHostAnnotationArgumentDesc& Argument :
						    ParenContexts.back().Annotation->Arguments)
						{
							if (Argument.Name == ArgumentName)
							{
								// A known annotation argument may own a nested
								// key/value list (for example metadata). Keep the
								// parent catalog context without hard-coding the
								// argument or host vocabulary.
								Annotation = ParenContexts.back().Annotation;
								ArgumentPath = ParenContexts.back().ArgumentPath;
								if (!ArgumentPath.empty())
								{
									ArgumentPath.push_back('.');
								}
								ArgumentPath.append(ArgumentName);
								break;
							}
						}
					}
					ParenContexts.push_back(
					    FAnnotationParenContext{Annotation, std::move(ArgumentPath)});
				}
				else if (C == ')' && !ParenContexts.empty())
				{
					ParenContexts.pop_back();
				}
			}
			// Only direct list keys are accepted by FindHostAnnotationArgumentAt.
			// Nested lists inherit the annotation only when introduced by a known
			// catalog argument; ordinary calls inside argument values remain null.
			if (bInLineComment || bInBlockComment || bInString || bInChar || ParenContexts.empty() || !ParenContexts.back().Annotation)
			{
				return nullptr;
			}
			if (OutArgumentPath)
			{
				*OutArgumentPath = ParenContexts.back().ArgumentPath;
			}
			return ParenContexts.back().Annotation;
		}

		const VeroScript::FHostAnnotationArgumentDesc* FindHostAnnotationArgumentAt(
		    const std::string_view Source,
		    const std::uint32_t Line,
		    const std::uint32_t Column,
		    const std::string_view TokenText,
		    const VeroScript::IHostBindingCatalog* const Bindings,
		    const VeroScript::FHostAnnotationDesc*& OutAnnotation,
		    std::string* const OutArgumentPath = nullptr)
		{
			OutAnnotation = nullptr;
			if (OutArgumentPath)
			{
				OutArgumentPath->clear();
			}
			if (!Bindings || TokenText.empty())
			{
				return nullptr;
			}
			bool bKnownArgumentName = false;
			for (const VeroScript::FHostAnnotationDesc& Annotation :
			    Bindings->GetAnnotations())
			{
				for (const VeroScript::FHostAnnotationArgumentDesc& Argument :
				    Annotation.Arguments)
				{
					if (Argument.Name == TokenText)
					{
						bKnownArgumentName = true;
						break;
					}
				}
				if (bKnownArgumentName)
				{
					break;
				}
			}
			if (!bKnownArgumentName)
			{
				return nullptr;
			}
			std::size_t TokenOffset = 0;
			if (!FindSourceOffset(Source, Line, Column, TokenOffset))
			{
				return nullptr;
			}
			OutAnnotation =
			    FindEnclosingHostAnnotationAtOffset(
			        Source, TokenOffset, Bindings, OutArgumentPath);
			if (!OutAnnotation)
			{
				return nullptr;
			}
			std::size_t Previous = TokenOffset;
			while (Previous > 0 && std::isspace(static_cast<unsigned char>(Source[Previous - 1])))
			{
				--Previous;
			}
			if (Previous == 0 || (Source[Previous - 1] != '(' && Source[Previous - 1] != ','))
			{
				return nullptr;
			}
			for (const VeroScript::FHostAnnotationArgumentDesc& Argument :
			    OutAnnotation->Arguments)
			{
				if (Argument.Name == TokenText)
				{
					return &Argument;
				}
			}
			return nullptr;
		}

		/**
		 * Soft recovery for mid-edit completion: insert ';' after the identifier under the
		 * caret so expression / decl statements can parse (e.g. typing `Hel` before ';').
		 * Bare type keywords (`int` / `array` / …) need a dummy declarator — `int;` is not
		 * a valid statement and would leave completion empty.
		 */
		bool TryRecoverSourceAfterIdentifier(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FTextPosition Position,
		    std::string& OutRecovered)
		{
			FToken Token;
			if (!FindIdentifierAt(Source, FileLabel, Position, Token))
			{
				return false;
			}

			const std::uint32_t InsertColumn =
			    Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size());
			std::size_t InsertOffset = 0;
			if (!FindSourceOffset(Source, Token.Location.Line, InsertColumn, InsertOffset))
			{
				return false;
			}

			// Shared with parser via VeroScriptLexer.h classifiers.
			std::string Tail = ";";
			if (Token.Kind == ETokenKind::Keyword)
			{
				if (VeroScript::IsContainerTypeKeyword(Token.Lexeme))
				{
					Tail = "<int> _;";
				}
				else if (Token.Lexeme == "unsigned")
				{
					Tail = " int _;";
				}
				else if (VeroScript::IsPrimitiveDeclarableTypeKeyword(Token.Lexeme))
				{
					Tail = " _;";
				}
			}

			OutRecovered.clear();
			OutRecovered.reserve(Source.size() + Tail.size());
			OutRecovered.append(Source.substr(0, InsertOffset));
			OutRecovered.append(Tail);
			OutRecovered.append(Source.substr(InsertOffset));
			return true;
		}

		bool AppendConstructorInitializerTargetCompletions(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FTextPosition Position,
		    const std::string_view Prefix,
		    std::unordered_map<std::string, FCompletionItem>& OutItems,
		    const VeroScript::IHostBindingCatalog* const Bindings)
		{
			FQueryLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			if (Lexer.HasError() || Tokens.empty())
			{
				return false;
			}

			const auto StartsBeforeCaret = [&](const FToken& Token)
			{
				return Token.Location.Line < Position.Line || (Token.Location.Line == Position.Line && Token.Location.Column < Position.Column);
			};
			std::size_t BeforeCaret = Tokens.size();
			while (BeforeCaret > 0 && !StartsBeforeCaret(Tokens[BeforeCaret - 1]))
			{
				--BeforeCaret;
			}
			if (BeforeCaret == 0)
			{
				return false;
			}

			std::size_t InitializerColon = Tokens.size();
			int ParenDepth = 0;
			for (std::size_t Index = BeforeCaret; Index > 0;)
			{
				const std::string_view Lexeme = Tokens[--Index].Lexeme;
				if (Lexeme == ")")
				{
					++ParenDepth;
				}
				else if (Lexeme == "(" && ParenDepth > 0)
				{
					--ParenDepth;
				}
				else if (ParenDepth == 0)
				{
					if (Lexeme == ":")
					{
						InitializerColon = Index;
						break;
					}
					if (Lexeme == ";" || Lexeme == "{" || Lexeme == "}")
					{
						return false;
					}
				}
			}
			if (InitializerColon == Tokens.size() || InitializerColon == 0 || Tokens[InitializerColon - 1].Lexeme != ")")
			{
				return false;
			}

			std::size_t ConstructorOpen = InitializerColon - 1;
			ParenDepth = 1;
			while (ConstructorOpen > 0 && ParenDepth > 0)
			{
				const std::string_view Lexeme = Tokens[--ConstructorOpen].Lexeme;
				if (Lexeme == ")")
				{
					++ParenDepth;
				}
				else if (Lexeme == "(")
				{
					--ParenDepth;
				}
			}
			if (ParenDepth != 0 || ConstructorOpen == 0 || Tokens[ConstructorOpen - 1].Kind != ETokenKind::Identifier)
			{
				return false;
			}
			const std::size_t ConstructorNameIndex = ConstructorOpen - 1;
			const std::string ConstructorName(Tokens[ConstructorNameIndex].Lexeme);
			const bool bOutOfClass = ConstructorNameIndex > 0 && Tokens[ConstructorNameIndex - 1].Lexeme == "::";
			std::string OwnerName = ConstructorName;
			std::string WrittenOwnerName = ConstructorName;
			if (ConstructorNameIndex >= 2 && bOutOfClass)
			{
				std::size_t OwnerEnd = ConstructorNameIndex - 1;
				std::size_t OwnerSimple = OwnerEnd - 1;
				if (Tokens[OwnerSimple].Lexeme == ">")
				{
					int AngleDepth = 0;
					std::size_t GenericOpen = OwnerEnd;
					for (std::size_t Index = OwnerEnd; Index > 0;)
					{
						const std::string_view Part = Tokens[--Index].Lexeme;
						if (Part == ">")
						{
							++AngleDepth;
						}
						else if (Part == "<" && AngleDepth > 0 && --AngleDepth == 0)
						{
							GenericOpen = Index;
							break;
						}
					}
					if (GenericOpen > 0 && GenericOpen < OwnerEnd && Tokens[GenericOpen - 1].Kind == ETokenKind::Identifier)
					{
						OwnerEnd = GenericOpen;
						OwnerSimple = GenericOpen - 1;
					}
				}
				if (Tokens[OwnerSimple].Kind == ETokenKind::Identifier)
				{
					std::size_t OwnerStart = OwnerSimple;
					OwnerName.assign(Tokens[OwnerSimple].Lexeme);
					while (OwnerStart >= 2 && Tokens[OwnerStart - 1].Lexeme == "::" && Tokens[OwnerStart - 2].Kind == ETokenKind::Identifier)
					{
						OwnerStart -= 2;
					}
					WrittenOwnerName.clear();
					for (std::size_t Index = OwnerStart; Index < OwnerEnd; ++Index)
					{
						if (Tokens[Index].Lexeme == "::")
						{
							WrittenOwnerName.push_back('.');
						}
						else
						{
							WrittenOwnerName.append(Tokens[Index].Lexeme);
						}
					}
				}
			}

			std::size_t TypeOpen = Tokens.size();
			int BraceDepth = 0;
			for (std::size_t Index = ConstructorNameIndex; !bOutOfClass && Index > 0;)
			{
				const std::string_view Lexeme = Tokens[--Index].Lexeme;
				if (Lexeme == "}")
				{
					++BraceDepth;
				}
				else if (Lexeme == "{")
				{
					if (BraceDepth == 0)
					{
						TypeOpen = Index;
						break;
					}
					--BraceDepth;
				}
			}
			if (TypeOpen == Tokens.size())
			{
				struct FTypeDeclarationCandidate
				{
					std::size_t Open = 0;
					std::string QualifiedName;
				};
				struct FBraceFrame
				{
					bool bNamespace = false;
					std::string PreviousNamespace;
				};
				std::vector<FTypeDeclarationCandidate> Candidates;
				std::vector<FBraceFrame> ScopeStack;
				std::string ActiveNamespace;
				std::size_t SegmentStart = 0;
				for (std::size_t Index = 0; Index < ConstructorNameIndex; ++Index)
				{
					if (Index + 1 < ConstructorNameIndex && (Tokens[Index].Lexeme == "class" || Tokens[Index].Lexeme == "struct") && Tokens[Index + 1].Kind == ETokenKind::Identifier && Tokens[Index + 1].Lexeme == OwnerName)
					{
						for (std::size_t Probe = Index + 2; Probe < ConstructorNameIndex; ++Probe)
						{
							if (Tokens[Probe].Lexeme == "{")
							{
								Candidates.push_back(FTypeDeclarationCandidate{
								    Probe,
								    ActiveNamespace.empty()
								        ? OwnerName
								        : ActiveNamespace + "." + OwnerName});
								break;
							}
							if (Tokens[Probe].Lexeme == ";")
							{
								break;
							}
						}
					}

					const std::string_view Lexeme = Tokens[Index].Lexeme;
					if (Lexeme == "{")
					{
						std::size_t NamespaceKeyword = Index;
						for (std::size_t Probe = SegmentStart; Probe < Index; ++Probe)
						{
							if (Tokens[Probe].Lexeme == "namespace")
							{
								NamespaceKeyword = Probe;
								break;
							}
						}
						FBraceFrame Frame;
						Frame.bNamespace = NamespaceKeyword != Index;
						Frame.PreviousNamespace = ActiveNamespace;
						if (Frame.bNamespace)
						{
							std::string Declared;
							for (std::size_t Probe = NamespaceKeyword + 1; Probe < Index; ++Probe)
							{
								if (Tokens[Probe].Kind == ETokenKind::Identifier)
								{
									if (!Declared.empty())
									{
										Declared.push_back('.');
									}
									Declared.append(Tokens[Probe].Lexeme);
								}
							}
							if (!Declared.empty())
							{
								ActiveNamespace = ActiveNamespace.empty()
								    ? std::move(Declared)
								    : ActiveNamespace + "." + Declared;
							}
						}
						ScopeStack.push_back(std::move(Frame));
						SegmentStart = Index + 1;
					}
					else if (Lexeme == "}")
					{
						if (!ScopeStack.empty())
						{
							if (ScopeStack.back().bNamespace)
							{
								ActiveNamespace =
								    std::move(ScopeStack.back().PreviousNamespace);
							}
							ScopeStack.pop_back();
						}
						SegmentStart = Index + 1;
					}
					else if (Lexeme == ";")
					{
						SegmentStart = Index + 1;
					}
				}

				const auto SelectQualified = [&](const std::string_view Qualified) -> bool
				{
					const auto Match = std::find_if(
					    Candidates.begin(),
					    Candidates.end(),
					    [&](const FTypeDeclarationCandidate& Candidate)
					{
						return Candidate.QualifiedName == Qualified;
					});
					if (Match == Candidates.end())
					{
						return false;
					}
					TypeOpen = Match->Open;
					return true;
				};

				if (WrittenOwnerName.find('.') != std::string::npos)
				{
					(void)SelectQualified(WrittenOwnerName);
				}
				else
				{
					std::string Namespace = ActiveNamespace;
					while (!Namespace.empty() && TypeOpen == Tokens.size())
					{
						if (SelectQualified(Namespace + "." + OwnerName))
						{
							break;
						}
						const std::size_t Dot = Namespace.rfind('.');
						Namespace = Dot == std::string::npos
						    ? std::string{}
						    : Namespace.substr(0, Dot);
					}
					if (TypeOpen == Tokens.size())
					{
						(void)SelectQualified(OwnerName);
					}
					// A using-namespace owner has no lexical qualifier. It is safe to
					// recover only when the leaf identifies exactly one declaration.
					if (TypeOpen == Tokens.size() && Candidates.size() == 1)
					{
						TypeOpen = Candidates.front().Open;
					}
				}
			}
			if (TypeOpen == Tokens.size())
			{
				return false;
			}

			std::size_t TypeKeyword = Tokens.size();
			std::size_t TypeNameIndex = Tokens.size();
			for (std::size_t Index = TypeOpen; Index > 0;)
			{
				--Index;
				if (Tokens[Index].Lexeme == ";" || Tokens[Index].Lexeme == "}" || Tokens[Index].Lexeme == "{")
				{
					break;
				}
				if ((Tokens[Index].Lexeme == "class" || Tokens[Index].Lexeme == "struct") && Index + 1 < TypeOpen && Tokens[Index + 1].Kind == ETokenKind::Identifier && Tokens[Index + 1].Lexeme == OwnerName)
				{
					TypeKeyword = Index;
					TypeNameIndex = Index + 1;
					break;
				}
			}
			if (TypeKeyword == Tokens.size())
			{
				return false;
			}

			// This fallback is target-only: it must not take over ordinary expression
			// completion while the caret is inside an initializer argument. Track only
			// top-level calls as used targets; nested calls are argument expressions.
			std::unordered_set<std::string> UsedTargets;
			int InitializerArgumentDepth = 0;
			bool bCurrentSegmentContainsCall = false;
			std::size_t CurrentSegmentTokenCount = 0;
			for (std::size_t Index = InitializerColon + 1; Index < BeforeCaret; ++Index)
			{
				const std::string_view Lexeme = Tokens[Index].Lexeme;
				if (Lexeme == "(")
				{
					if (InitializerArgumentDepth == 0)
					{
						bCurrentSegmentContainsCall = true;
						if (Index > InitializerColon + 1 && Tokens[Index - 1].Kind == ETokenKind::Identifier)
						{
							UsedTargets.emplace(Tokens[Index - 1].Lexeme);
						}
					}
					++InitializerArgumentDepth;
				}
				else if (Lexeme == ")")
				{
					if (InitializerArgumentDepth == 0)
					{
						return false;
					}
					--InitializerArgumentDepth;
				}
				else if (InitializerArgumentDepth == 0 && Lexeme == ",")
				{
					bCurrentSegmentContainsCall = false;
					CurrentSegmentTokenCount = 0;
				}
				else if (InitializerArgumentDepth == 0)
				{
					++CurrentSegmentTokenCount;
				}
			}
			if (InitializerArgumentDepth != 0 || bCurrentSegmentContainsCall || CurrentSegmentTokenCount > 1)
			{
				return false;
			}

			const auto AddTarget = [&](const std::string& Name,
			                           const EDocumentSymbolKind Kind, std::string Detail)
			{
				if (UsedTargets.find(Name) != UsedTargets.end() || !PrefixMatches(Name, Prefix))
				{
					return;
				}
				FCompletionItem Item;
				Item.Kind = Kind;
				Item.Label = Name;
				Item.Detail = std::move(Detail);
				Item.InsertText = Name;
				OutItems.insert_or_assign(Name, std::move(Item));
			};

			std::size_t InheritanceColon = Tokens.size();
			std::unordered_set<std::string> DeclaredInterfaces;
			for (std::size_t Index = 0; Index + 1 < Tokens.size(); ++Index)
			{
				if (Tokens[Index].Lexeme == "interface" && Tokens[Index + 1].Kind == ETokenKind::Identifier)
				{
					DeclaredInterfaces.emplace(Tokens[Index + 1].Lexeme);
				}
			}
			for (std::size_t Index = TypeNameIndex + 1; Index < TypeOpen; ++Index)
			{
				if (Tokens[Index].Lexeme == ":")
				{
					InheritanceColon = Index;
					break;
				}
			}
			if (InheritanceColon != Tokens.size())
			{
				int AngleDepth = 0;
				bool bNeedTarget = true;
				for (std::size_t Index = InheritanceColon + 1; Index < TypeOpen; ++Index)
				{
					const std::string_view Lexeme = Tokens[Index].Lexeme;
					if (Lexeme == "<")
					{
						++AngleDepth;
					}
					else if (Lexeme == ">" && AngleDepth > 0)
					{
						--AngleDepth;
					}
					else if (Lexeme == "," && AngleDepth == 0)
					{
						bNeedTarget = true;
					}
					else if (bNeedTarget && AngleDepth == 0 && Tokens[Index].Kind == ETokenKind::Identifier)
					{
						const std::string Candidate(Lexeme);
						// An in-class constructor initializer currently accepts a single
						// identifier, while inheritance permits qualified type names. Do
						// not truncate `N::Base` to the invalid target `N` in this lexical
						// fallback. A generic argument may itself be qualified, so inspect
						// only the declared base name before its opening '<'.
						bool bQualifiedBase = false;
						for (std::size_t Probe = Index + 1; Probe < TypeOpen; ++Probe)
						{
							const std::string_view Following = Tokens[Probe].Lexeme;
							if (Following == "<" || Following == ",")
							{
								break;
							}
							if (Following == "." || Following == "::")
							{
								bQualifiedBase = true;
								break;
							}
						}
						if (!bQualifiedBase && DeclaredInterfaces.find(Candidate) == DeclaredInterfaces.end() && (!Bindings || Bindings->FindType(Candidate) == nullptr))
						{
							AddTarget(Candidate, EDocumentSymbolKind::Class,
							    "direct base constructor");
						}
						bNeedTarget = false;
					}
				}
			}

			std::size_t TypeClose = Tokens.size();
			BraceDepth = 1;
			for (std::size_t Index = TypeOpen + 1; Index < Tokens.size(); ++Index)
			{
				if (Tokens[Index].Lexeme == "{")
				{
					++BraceDepth;
				}
				else if (Tokens[Index].Lexeme == "}" && --BraceDepth == 0)
				{
					TypeClose = Index;
					break;
				}
			}

			std::size_t SegmentStart = TypeOpen + 1;
			BraceDepth = 1;
			for (std::size_t Index = TypeOpen + 1; Index < TypeClose; ++Index)
			{
				const std::string_view Lexeme = Tokens[Index].Lexeme;
				if (Lexeme == "{")
				{
					++BraceDepth;
					continue;
				}
				if (Lexeme == "}")
				{
					if (--BraceDepth == 1)
					{
						SegmentStart = Index + 1;
					}
					continue;
				}
				if (BraceDepth != 1)
				{
					continue;
				}
				if (Lexeme == ":" && Index > SegmentStart && (Tokens[Index - 1].Lexeme == "public" || Tokens[Index - 1].Lexeme == "protected" || Tokens[Index - 1].Lexeme == "private"))
				{
					SegmentStart = Index + 1;
					continue;
				}
				if (Lexeme != ";")
				{
					continue;
				}

				bool bCallable = false;
				bool bStatic = false;
				bool bConst = false;
				bool bHasDeclarationInitializer = false;
				std::size_t NameIndex = Tokens.size();
				std::size_t DeclarationStart = SegmentStart;
				while (DeclarationStart < Index && Tokens[DeclarationStart].Lexeme == "@")
				{
					++DeclarationStart;
					if (DeclarationStart >= Index || Tokens[DeclarationStart].Kind != ETokenKind::Identifier)
					{
						break;
					}
					++DeclarationStart;
					if (DeclarationStart < Index && Tokens[DeclarationStart].Lexeme == "(")
					{
						int AnnotationDepth = 0;
						do
						{
							const std::string_view AnnotationToken =
							    Tokens[DeclarationStart++].Lexeme;
							if (AnnotationToken == "(")
							{
								++AnnotationDepth;
							}
							else if (AnnotationToken == ")")
							{
								--AnnotationDepth;
							}
						}
						while (DeclarationStart < Index && AnnotationDepth > 0);
					}
				}
				for (std::size_t Probe = DeclarationStart; Probe < Index; ++Probe)
				{
					const std::string_view Part = Tokens[Probe].Lexeme;
					bCallable = bCallable || (Part == "(" && Probe > DeclarationStart && Tokens[Probe - 1].Kind == ETokenKind::Identifier);
					bStatic = bStatic || Part == "static";
					bConst = bConst || Part == "const" || Part == "constexpr";
					if (Part == "=")
					{
						bHasDeclarationInitializer = true;
						break;
					}
					if (Tokens[Probe].Kind == ETokenKind::Identifier)
					{
						NameIndex = Probe;
					}
				}
				if (!bCallable && !bStatic && !bHasDeclarationInitializer && NameIndex != Tokens.size())
				{
					std::string Detail = "constructor field";
					if (NameIndex > DeclarationStart)
					{
						Detail += ": ";
						for (std::size_t Probe = DeclarationStart; Probe < NameIndex; ++Probe)
						{
							if (Probe > DeclarationStart)
							{
								Detail.push_back(' ');
							}
							Detail.append(Tokens[Probe].Lexeme);
						}
					}
					AddTarget(
					    std::string(Tokens[NameIndex].Lexeme),
					    bConst ? EDocumentSymbolKind::Constant : EDocumentSymbolKind::Field,
					    std::move(Detail));
				}
				SegmentStart = Index + 1;
			}
			return true;
		}

		/**
		 * Last-resort completion when the buffer cannot be recovered into a program:
		 * still offer language keywords (and active macros) for the caret prefix.
		 */
		FCompletionResult MakeKeywordOnlyCompletion(
		    const std::string_view Source, const std::string_view FileLabel,
		    const FTextPosition Position, const std::vector<std::string_view>& Defines,
		    const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros,
		    const VeroScript::IHostBindingCatalog* const Bindings,
		    const FPreprocessLineMap* ExistingLineMap = nullptr)
		{
			FCompletionResult Result;
			Result.bSuccess = true;

			FToken Token;
			if (FindIdentifierAt(Source, FileLabel, Position, Token))
			{
				Result.bHasReplaceRange = true;
				Result.ReplaceLine = Token.Location.Line;
				Result.ReplaceStartColumn = Token.Location.Column;
				Result.ReplaceEndColumn =
				    Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size());
				const std::uint32_t Relative =
				    Position.Column >= Token.Location.Column
				    ? (Position.Column - Token.Location.Column)
				    : 0;
				const std::size_t PrefixLen =
				    Relative <= Token.Lexeme.size() ? Relative : Token.Lexeme.size();
				Result.Prefix = std::string(Token.Lexeme.substr(0, PrefixLen));
			}

			// When lexing stopped before the caret, the preceding tokens cannot tell
			// whether this is a statement, declaration, comment or literal. Keep the
			// known prefix/range, but do not fill that uncertainty with global words.
			FQueryLexer ContextLexer(Source, FileLabel);
			const std::vector<FToken>& ContextTokens = ContextLexer.Tokenize();
			if (ContextLexer.HasError() && !ContextTokens.empty())
			{
				const FSourceLocation& ErrorAt = ContextTokens.back().Location;
				if (ErrorAt.Line < Position.Line || (ErrorAt.Line == Position.Line && ErrorAt.Column <= Position.Column))
				{
					return Result;
				}
			}

			std::unordered_map<std::string, FCompletionItem> Items;
			const EKeywordCompletionMode KeywordMode =
			    ClassifyKeywordCompletionMode(Source, FileLabel, Position, true);
			const bool bConstructorInitializerTargets =
			    AppendConstructorInitializerTargetCompletions(
			        Source, FileLabel, Position, Result.Prefix, Items, Bindings);
			if (!bConstructorInitializerTargets)
			{
				AppendLanguageKeywordCompletions(Result.Prefix, Items, KeywordMode);
			}
			VeroScript::FPreprocessLineMap LineMap;
			std::string PreprocessError;
			// Partial map is still useful for ActiveMacros (e.g. open `#ifdef`).
			if (ExistingLineMap)
			{
				LineMap = *ExistingLineMap;
			}

			else
			{
				(void)BuildPreprocessorLineMap(Source, Defines, PredefinedMacros, LineMap, PreprocessError);
			}

			if (!bConstructorInitializerTargets)
			{
				AppendMacroCompletions(LineMap, Source, Result.Prefix, Position.Line, Items, true,
				    &Defines, Position.Column);
			}

			// Keep host functions/types on the same fuzzy filtering and contextual
			// ranking path as language symbols. This fallback is used specifically
			// for incomplete buffers, where completion must remain useful without
			// assigning every host candidate a fixed, artificially-low rank.
			if (Bindings && !bConstructorInitializerTargets)
			{
				for (const VeroScript::FHostFunctionDesc& Function : Bindings->GetFunctions())
				{
					if (!PrefixMatches(Function.Name, Result.Prefix))
					{
						continue;
					}
					FCompletionItem Item;
					Item.Kind = EDocumentSymbolKind::Function;
					Item.Label = Function.Name;
					Item.Detail = VeroScript::FormatHostAnnotations(Function.Annotations);
					if (!Item.Detail.empty())
					{
						Item.Detail.push_back(' ');
					}
					Item.Detail += VeroScript::FormatHostFunctionSignature(Function);
					Item.InsertText = Function.Name;
					Item.bSoftBuiltin = true;
					const auto Existing = Items.find(Item.Label);
					const bool bDistinctOverload = Existing != Items.end() && IsCallableCompletionKind(Existing->second.Kind) && Existing->second.Detail != Item.Detail;
					const std::string Key = bDistinctOverload
					    ? Item.Label + '\x1f' + Item.Detail
					    : Item.Label;
					Items.emplace(Key, std::move(Item));
				}

				for (const VeroScript::FHostTypeDesc& Type : Bindings->GetTypes())
				{
					if (!PrefixMatches(Type.Name, Result.Prefix) || Items.find(Type.Name) != Items.end())
					{
						continue;
					}
					FCompletionItem Item;
					switch (Type.Kind)
					{
					case VeroScript::EHostTypeKind::Enum:
						{
							Item.Kind = EDocumentSymbolKind::Enum;
							break;
						}
					case VeroScript::EHostTypeKind::Struct:
						{
							Item.Kind = EDocumentSymbolKind::Struct;
							break;
						}
					default:
						{
							Item.Kind = EDocumentSymbolKind::Class;
							break;
						}
					}
					Item.Label = Type.Name;
					Item.Detail = VeroScript::FormatHostAnnotations(Type.Annotations);
					if (!Item.Detail.empty())
					{
						Item.Detail.push_back(' ');
					}
					Item.Detail += std::string("host ") + VeroScript::HostTypeKindName(Type.Kind);
					if (Type.Kind == VeroScript::EHostTypeKind::Enum && Type.bScopedEnum)
					{
						Item.Detail += " scoped";
					}
					Item.InsertText = Type.Name;
					Item.bSoftBuiltin = true;
					Items.emplace(Item.Label, std::move(Item));
				}
				AppendGenericHostTemplateCompletions(Bindings, Result.Prefix, Items);
			}
			SuppressGenericSnippetBeforeExistingAngle(Source, Result, Items);
			ApplyCallableSnippets(Source, Result, KeywordMode, Position, Items);
			MergeCallableOverloadItems(Items);
			Result.Items.reserve(Items.size());
			for (auto& Pair : Items)
			{
				Result.Items.push_back(std::move(Pair.second));
			}
			FinalizeCompletionItems(
			    Result.Items,
			    bConstructorInitializerTargets
			        ? ECompletionRankContext::General
			        : RankContextFromKeywordMode(KeywordMode),
			    Result.Prefix);
			return Result;
		}

		// Soft recovery for `recv.` mid-edit: insert `_;` after the dot.
		bool TryRecoverSourceAfterMemberDot(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FTextPosition Position,
		    std::string& OutRecovered)
		{
			FMemberCompletionContext Context;
			if (!FindMemberCompletionContext(Source, FileLabel, Position, Context) || Context.bHasReplaceRange)
			{
				return false;
			}

			std::size_t InsertOffset = 0;
			if (!FindSourceOffset(
			        Source,
			        Context.DotInsertLine,
			        Context.DotInsertColumn,
			        InsertOffset))
			{
				return false;
			}

			OutRecovered.clear();
			OutRecovered.reserve(Source.size() + 2);
			OutRecovered.append(Source.substr(0, InsertOffset));
			OutRecovered += "_;";
			OutRecovered.append(Source.substr(InsertOffset));
			return true;
		}

		// Preserve the confirmed left-hand side of an unfinished qualified path
		// (`std::chrono::`) for query/semantic-token recovery.  The recovery is
		// deliberately narrow: only a final scope separator immediately before a
		// closing brace, semicolon, or EOF is rewritten as a statement terminator.
		// This never changes the physical source used to produce token ranges.
		bool TryRecoverSourceAfterTrailingQualifiedScope(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    std::string& OutRecovered)
		{
			FQueryLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			for (std::size_t Index = Tokens.size(); Index-- > 0;)
			{
				if (Tokens[Index].Lexeme != "::" || Index == 0 || Tokens[Index - 1].Kind != ETokenKind::Identifier)
				{
					continue;
				}
				if (Index + 1 < Tokens.size() && Tokens[Index + 1].Lexeme != "}" && Tokens[Index + 1].Lexeme != ";")
				{
					return false;
				}

				std::size_t ScopeOffset = 0;
				if (!FindSourceOffset(
				        Source,
				        Tokens[Index].Location.Line,
				        Tokens[Index].Location.Column,
				        ScopeOffset))
				{
					return false;
				}
				OutRecovered.assign(Source.data(), Source.size());
				OutRecovered.replace(ScopeOffset, 2, ";");
				return true;
			}
			return false;
		}

		// Soft recovery for `goto ` with no label yet: insert `_;` at the caret.
		bool TryRecoverSourceAfterGoto(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FTextPosition Position,
		    std::string& OutRecovered)
		{
			FGotoCompletionContext Context;
			if (!FindGotoCompletionContext(Source, FileLabel, Position, Context) || Context.bHasReplaceRange)
			{
				return false;
			}

			std::size_t InsertOffset = 0;
			if (!FindSourceOffset(Source, Position.Line, Position.Column, InsertOffset))
			{
				return false;
			}

			OutRecovered.clear();
			OutRecovered.reserve(Source.size() + 2);
			OutRecovered.append(Source.substr(0, InsertOffset));
			OutRecovered += "_;";
			OutRecovered.append(Source.substr(InsertOffset));
			return true;
		}

		// Soft recovery for `[capture|` mid-edit: close as a tiny lambda.
		bool TryRecoverSourceAfterCapture(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FTextPosition Position,
		    std::string& OutRecovered)
		{
			FCaptureCompletionContext Context;
			if (!FindCaptureCompletionContext(Source, FileLabel, Position, Context))
			{
				return false;
			}

			std::size_t InsertOffset = 0;
			const std::uint32_t InsertLine =
			    Context.bHasReplaceRange ? Context.ReplaceLine : Context.InsertLine;
			const std::uint32_t InsertColumn = Context.bHasReplaceRange
			    ? Context.ReplaceEndColumn
			    : Context.InsertColumn;
			if (!FindSourceOffset(Source, InsertLine, InsertColumn, InsertOffset))
			{
				return false;
			}

			constexpr const char* Tail = "]() -> int => 0;";
			OutRecovered.clear();
			OutRecovered.reserve(Source.size() + 16);
			OutRecovered.append(Source.substr(0, InsertOffset));
			OutRecovered += Tail;
			OutRecovered.append(Source.substr(InsertOffset));
			return true;
		}

		bool LooksLikeExpectedSemicolonDiagnostic(const FScriptError& Error)
		{
			// Prefer stable ids — Message text is locale-dependent and may mention
			// ';' in unrelated diagnostics later.
			if (Error.MessageId == "diag.expected_after_expression" || Error.MessageId == "diag.expected_after_variable_declaration" || Error.MessageId == "diag.expected_after_fallthrough" || Error.MessageId == "diag.expected_or_after_interface_method_signature" || Error.MessageId == "diag.parser_expected_after_return")
			{
				return true;
			}
			// Fallback for paths that only stamp an English Message.
			return Error.MessageId.empty() && Error.Message.find("';'") != std::string::npos && (Error.Message.find("expected") != std::string::npos || Error.Message.find("after") != std::string::npos);
		}

		/**
		 * Soft recovery for unfinished statements elsewhere in the buffer
		 * (e.g. a lone `to` / `no` before `}` → "expected ';' after expression").
		 * Insert ';' at diagnostic sites so Hover/Complete can still resolve other symbols.
		 */
		bool DiagnosticBelongsToFile(
		    const FScriptError& Diagnostic,
		    const std::string_view FileLabel)
		{
			if (Diagnostic.Location.FileLabel.empty() || FileLabel.empty())
			{
				return true;
			}
			return VeroScript::PathUtil::Equal(
			    Diagnostic.Location.FileLabel,
			    std::string(FileLabel));
		}

		bool TryRecoverSourceAtExpectedSemicolon(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const std::vector<FScriptError>& Diagnostics,
		    std::string& OutRecovered)
		{
			std::vector<std::size_t> InsertOffsets;
			std::vector<std::size_t> ReplaceOffsets;
			InsertOffsets.reserve(Diagnostics.size());
			ReplaceOffsets.reserve(Diagnostics.size());
			FQueryLexer RecoveryLexer(Source, FileLabel);
			const auto& Tokens = RecoveryLexer.Tokenize();
			for (const FScriptError& Diagnostic : Diagnostics)
			{
				if (!LooksLikeExpectedSemicolonDiagnostic(Diagnostic) || !DiagnosticBelongsToFile(Diagnostic, FileLabel))
				{
					continue;
				}
				std::size_t Offset = 0;
				if (!FindSourceOffset(
				        Source,
				        Diagnostic.Location.Line,
				        Diagnostic.Location.Column,
				        Offset))
				{
					continue;
				}
				if (Offset < Source.size() && Source[Offset] == ';')
				{
					continue;
				}
				// Preserve physical columns when repairing the private parse copy.
				// Inserting at the diagnostic token shifts every later declaration
				// on that line, so navigation would point into the wrong character.
				if (Offset > 0 && Source[Offset - 1] != '\r' && Source[Offset - 1] != '\n' && std::isspace(static_cast<unsigned char>(Source[Offset - 1])))
				{
					ReplaceOffsets.push_back(Offset - 1);
					continue;
				}
				if (Source.substr(Offset).find_first_not_of(" \t\r\n})];>") == std::string_view::npos)
				{
					// EOF and a closing-only recovery suffix contain no later symbol
					// sites to shift (for example the synthetic end of an open lambda).
					InsertOffsets.push_back(Offset);
					continue;
				}
				const auto Next = QueryTokenLowerBound(Tokens,
				    {Diagnostic.Location.Line, Diagnostic.Location.Column});
				if (Next > 0)
				{
					const auto& Previous = Tokens[Next - 1];
					std::size_t PreviousOffset = 0;
					if (FindSourceOffset(Source, Previous.Location.Line, Previous.Location.Column, PreviousOffset) && Previous.Lexeme.size() <= Source.size() - PreviousOffset)
					{
						const auto End = PreviousOffset + Previous.Lexeme.size();
						if (End <= Offset && Source.substr(End, Offset - End).find_first_of("\r\n") != std::string_view::npos)
						{
							InsertOffsets.push_back(End);
						} // Before trailing comments and the line break.
					}
				}
				// With no safe trivia or suffix, keep the failed parse rather than
				// shift declaration sites on the same physical line.
			}
			if (InsertOffsets.empty() && ReplaceOffsets.empty())
			{
				return false;
			}

			std::sort(
			    InsertOffsets.begin(),
			    InsertOffsets.end(),
			    [](const std::size_t Left, const std::size_t Right)
			{
				return Left > Right;
			});
			InsertOffsets.erase(
			    std::unique(InsertOffsets.begin(), InsertOffsets.end()),
			    InsertOffsets.end());

			OutRecovered.clear();
			OutRecovered.reserve(Source.size() + InsertOffsets.size());
			OutRecovered.assign(Source.begin(), Source.end());
			for (const std::size_t Offset : ReplaceOffsets)
			{
				OutRecovered[Offset] = ';';
			}
			for (const std::size_t Offset : InsertOffsets)
			{
				OutRecovered.insert(OutRecovered.begin() + static_cast<std::ptrdiff_t>(Offset), ';');
			}
			return true;
		}

		bool DiagnosticsSuggestMissingSemicolon(
		    const std::string_view FileLabel,
		    const std::vector<FScriptError>& Diagnostics)
		{
			for (const FScriptError& Diagnostic : Diagnostics)
			{
				if (LooksLikeExpectedSemicolonDiagnostic(Diagnostic) && DiagnosticBelongsToFile(Diagnostic, FileLabel))
				{
					return true;
				}
			}
			return false;
		}

		FCompletionResult CompleteImportPaths(
		    const FImportPathCompletionContext& Context,
		    const std::string_view FileLabel,
		    const FCompletionOptions& Options)
		{
			FCompletionResult Result;
			Result.bSuccess = true;
			Result.Prefix = Context.Prefix;
			Result.bHasReplaceRange = Context.bHasReplaceRange;
			Result.ReplaceLine = Context.ReplaceLine;
			Result.ReplaceStartColumn = Context.ReplaceStartColumn;
			Result.ReplaceEndColumn = Context.ReplaceEndColumn;

			std::vector<std::filesystem::path> Roots;
			auto AddRoot = [&Roots](const std::filesystem::path& Root)
			{
				if (Root.empty())
				{
					return;
				}
				std::error_code Error;
				if (std::filesystem::is_directory(Root, Error))
				{
					Roots.push_back(Root);
				}
			};

			for (const std::string_view ImportPath : Options.ImportPaths)
			{
				AddRoot(PathUtil::FromUtf8(ImportPath));
			}
			for (const std::string_view StdlibPath : Options.StdlibSearchPaths)
			{
				AddRoot(PathUtil::FromUtf8(StdlibPath));
			}
			{
				const std::filesystem::path LabelPath = PathUtil::FromUtf8(FileLabel);
				if (LabelPath.has_parent_path())
				{
					AddRoot(LabelPath.parent_path());
				}
			}
			if (!Options.ModuleEnvironment || Options.ModuleEnvironment->GetDescription().bAllowAmbientSearch)
			{
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

			// Relative prefixes like `lib/` limit enumeration to that subdirectory.
			std::string RelativeDir;
			std::string FilePrefix = Context.Prefix;
			const std::size_t Slash = Context.Prefix.find_last_of("/\\");
			if (Slash != std::string::npos)
			{
				RelativeDir = Context.Prefix.substr(0, Slash + 1);
				FilePrefix = Context.Prefix.substr(Slash + 1);
			}

			std::unordered_map<std::string, FCompletionItem> Items;
			std::unique_ptr<VeroScriptModuleLoader> Resolver;
			if (Options.ModuleEnvironment)
			{
				Resolver = std::make_unique<VeroScriptModuleLoader>("", Options.ModuleEnvironment);
				ApplyModuleLoadingOptions(*Resolver, Options);
				for (const auto& S : Options.SourceOverlays)
				{
					Resolver->SetSourceOverlay(S.Path, {});
				}
			}
			const auto AddItem = [&](const std::string& Label, const std::string& Extension)
			{
				if (Resolver)
				{
					std::string Resolved;
					const auto Directory = PathUtil::ToUtf8(PathUtil::FromUtf8(FileLabel).parent_path());
					try
					{
						if (!Resolver->ResolveImportPath(Label, Directory, Resolved, FileLabel)) return;
					}
					catch (const FAnalysisCancelled&)
					{
						throw;
					}
					catch (const std::exception&)
					{
						return;
					}
				}
				FCompletionItem Item;
				Item.Kind = EDocumentSymbolKind::Import;
				Item.Label = Label;
				if (Context.SourcePrefix != Context.Prefix)
				{
					// Keep the typed escapes through the last matched character,
					// including a fuzzy filename match, then retain its real suffix.
					std::size_t Matched = 0, Suffix = 0;
					for (std::size_t I = 0; I < Label.size() && Matched < Context.Prefix.size(); ++I)
					{
						if (FoldCompletionCharacter(Label[I]) ==
						    FoldCompletionCharacter(Context.Prefix[Matched]))
						{
							++Matched;
							Suffix = I + 1;
						}
					}

					if (Matched == Context.Prefix.size())
					{
						Item.FilterText = Context.SourcePrefix + Label.substr(Suffix);
					}
				}
				if (Extension == ".vslib")
				{
					Item.Detail = Context.bReExport
					    ? "re-exported binary package"
					    : "binary package";
				}
				else
				{
					Item.Detail = Context.bReExport
					    ? "re-exported module"
					    : "module";
				}
				bool Representable = true;
				for (const char Ch : Label)
				{
					if (Context.bRawString)
					{
						if (Ch == '"' || Ch == '\n' || Ch == '\r')
						{
							Representable = false;
							break;
						}
						Item.InsertText += Ch;
					}
					else if (Ch == '\\' || Ch == '"')
					{
						Item.InsertText += '\\';
						Item.InsertText += Ch;
					}
					else if (Ch == '\n' || Ch == '\r' || Ch == '\t')
					{
						if (Context.bCompilerExtension)
						{
							Representable = false;
							break;
						}
						Item.InsertText += Ch == '\n' ? "\\n" : Ch == '\r' ? "\\r"
						                                                   : "\\t";
					}
					else
					{
						Item.InsertText += Ch;
					}
				}
				if (Representable)
				{
					Items.insert_or_assign(Label, std::move(Item));
				}
			};
			if (Options.ModuleEnvironment)
			{
				for (const auto& M : Options.ModuleEnvironment->GetDescription().Modules)
				{
					if (PrefixMatches(M.Name, Context.Prefix))
					{
						AddItem(M.Name, {});
					}
				}
			}

			for (const std::filesystem::path& Root : Roots)
			{
				CheckAnalysisCancellation();
				const std::filesystem::path SearchRoot =
				    RelativeDir.empty()
				    ? Root
				    : (Root / PathUtil::FromUtf8(RelativeDir));
				std::error_code Error;
				if (!std::filesystem::is_directory(SearchRoot, Error))
				{
					continue;
				}
				std::filesystem::directory_iterator Iterator(SearchRoot, Error);
				const std::filesystem::directory_iterator End;
				while (!Error && Iterator != End)
				{
					CheckAnalysisCancellation();
					const std::filesystem::directory_entry& Entry = *Iterator;
					if (Entry.is_regular_file(Error))
					{
						const std::filesystem::path& Path = Entry.path();
						std::string Extension = PathUtil::ToUtf8(Path.extension());
						std::transform(
						    Extension.begin(),
						    Extension.end(),
						    Extension.begin(),
						    [](const unsigned char Character)
						{
							return static_cast<char>(std::tolower(Character));
						});
						if (Extension == ".vs" || Extension == ".vslib")
						{
							const std::string FileName = PathUtil::ToUtf8(Path.filename());
							if (PrefixMatches(FileName, FilePrefix))
							{
								const std::string Label = RelativeDir + FileName;
								AddItem(Label, Extension);
							}
						}
					}
					if (Error)
					{
						break;
					}
					Iterator.increment(Error);
				}
			}

			Result.Items.reserve(Items.size());
			for (auto& Pair : Items)
			{
				Result.Items.push_back(std::move(Pair.second));
			}
			FinalizeCompletionItems(
			    Result.Items, ECompletionRankContext::ImportPath, Result.Prefix);
			return Result;
		}

		FCompletionResult CompletePreprocessorDirectives(
		    const FPreprocessorDirectiveCompletionContext& Context, const std::string_view Source,
		    const std::vector<std::string_view>& Defines,
		    const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros,
		    const FPreprocessLineMap* ExistingLineMap = nullptr)
		{
			FCompletionResult Result;
			Result.bSuccess = true;
			Result.Prefix = Context.Prefix;
			Result.bHasReplaceRange = Context.bHasReplaceRange;
			Result.ReplaceLine = Context.ReplaceLine;
			Result.ReplaceStartColumn = Context.ReplaceStartColumn;
			Result.ReplaceEndColumn = Context.ReplaceEndColumn;

			std::unordered_map<std::string, FCompletionItem> Items;

			if (Context.Kind == FPreprocessorDirectiveCompletionContext::EKind::NoCompletions)
			{
				return Result;
			}

			if (Context.Kind == FPreprocessorDirectiveCompletionContext::EKind::DirectiveName)
			{
				static constexpr const char* Directives[] = {
				    "define",
				    "define_export",
				    "undef",
				    "include",
				    "ifdef",
				    "ifndef",
				    "if",
				    "elif",
				    "else",
				    "endif",
				    "error",
				};
				for (const char* Directive : Directives)
				{
					const std::string Label(Directive);
					if (!PrefixMatches(Label, Context.Prefix))
					{
						continue;
					}
					FCompletionItem Item;
					Item.Kind = EDocumentSymbolKind::Keyword;
					Item.Label = Label;
					Item.Detail = "preprocessor directive";
					Item.InsertText = Label;
					Items.emplace(Label, std::move(Item));
				}
			}
			else
			{
				if (Context.Kind == FPreprocessorDirectiveCompletionContext::EKind::IfExpr && !Context.bInsideDefined && PrefixMatches("defined", Context.Prefix))
				{
					FCompletionItem Item;
					Item.Kind = EDocumentSymbolKind::Keyword;
					Item.Label = "defined";
					Item.Detail = "preprocessor defined";
					Item.InsertText = "defined(";
					Items.emplace("defined", std::move(Item));
				}

				VeroScript::FPreprocessLineMap LineMap;
				std::string PreprocessError;
				// OutMap may be partial on error (e.g. missing #endif while typing
				// `#ifdef FO|`); still use any ActiveMacros collected so far.
				if (ExistingLineMap)
				{
					LineMap = *ExistingLineMap;
				}

				else
				{
					(void)BuildPreprocessorLineMap(
					    Source, Defines, PredefinedMacros, LineMap, PreprocessError);
				}

				const std::uint32_t AtLine = Context.ReplaceLine != 0 ? Context.ReplaceLine : 1;
				// PP contexts need bare macro names (`#ifdef MAX`, `#if defined(MAX)`).
				AppendMacroCompletions(LineMap, Source, Context.Prefix, AtLine, Items, false,
				    &Defines, Context.ReplaceEndColumn);
			}

			Result.Items.reserve(Items.size());
			for (auto& Pair : Items)
			{
				Result.Items.push_back(std::move(Pair.second));
			}
			FinalizeCompletionItems(
			    Result.Items, ECompletionRankContext::Preprocessor, Result.Prefix);
			return Result;
		}

		bool ResolveHostMemberReturnTypeName(
		    const VeroScript::IHostBindingCatalog* Bindings,
		    const std::string& OwnerTypeName,
		    const std::string& MemberName,
		    std::string& OutReturnType,
		    const FProgram* Program = nullptr);

		bool CollectHostMemberHits(
		    const VeroScript::IHostBindingCatalog* Bindings,
		    const std::string_view OwnerTypeName,
		    const std::string_view MemberName,
		    std::vector<FSymbolQueryHit>& OutHits,
		    const FProgram* Program = nullptr,
		    bool bTypeReceiver = false);

		/**
		 * Completion for host-registered declaration annotations. Both spellings the
		 * parser accepts are covered: a bare `Name(` and `@name(`, where the sigil is
		 * its own token and so must be matched and replaced separately.
		 *
		 * Returns an unsuccessful result when the caret is not on an annotation-shaped
		 * prefix or the host registered no annotations.
		 */
		FCompletionResult CompleteAnnotations(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FTextPosition Position,
		    const VeroScript::IHostBindingCatalog* const Bindings,
		    const bool bSigilOnly)
		{
			std::vector<FDocumentSymbol> AnnotationSymbols;
			AppendHostAnnotationSymbols(Bindings, AnnotationSymbols);
			if (AnnotationSymbols.empty())
			{
				return {};
			}

			FToken Token;
			const bool bHasIdentifier = FindIdentifierAt(Source, FileLabel, Position, Token);
			bool bAtSigil = false;
			bool bSigilWithoutName = false;
			std::size_t AnnotationStartOffset = 0;
			if (bHasIdentifier)
			{
				if (!FindSourceOffset(
				        Source, Token.Location.Line, Token.Location.Column, AnnotationStartOffset))
				{
					return {};
				}
				if (Token.Location.Column > 1)
				{
					std::size_t SigilOffset = 0;
					bAtSigil = FindSourceOffset(
					               Source, Token.Location.Line, Token.Location.Column - 1, SigilOffset) &&
					    SigilOffset < Source.size() && Source[SigilOffset] == '@';
					if (bAtSigil)
					{
						AnnotationStartOffset = SigilOffset;
					}
				}
			}
			else if (Position.Column > 1)
			{
				bAtSigil = FindSourceOffset(
				               Source, Position.Line, Position.Column - 1, AnnotationStartOffset) &&
				    AnnotationStartOffset < Source.size() && Source[AnnotationStartOffset] == '@';
				bSigilWithoutName = bAtSigil;
			}
			if (!bHasIdentifier && !bSigilWithoutName)
			{
				return {};
			}
			if (bSigilOnly && !bAtSigil)
			{
				return {};
			}

			// Declaration annotations begin a logical line (after indentation). Without
			// this guard, a broken expression such as `value = UCL|` could incorrectly
			// offer UCLASS through the load-failed lexical fallback.
			if (!HasOnlyIndentBeforeOffset(Source, AnnotationStartOffset))
			{
				return {};
			}
			if (IsInsideExecutableBrace(Source, Position))
			{
				return {};
			}

			std::string Prefix;
			if (bHasIdentifier)
			{
				const std::uint32_t PrefixLength =
				    Position.Column > Token.Location.Column
				    ? Position.Column - Token.Location.Column
				    : 0;
				Prefix.assign(
				    Token.Lexeme.substr(
				        0, std::min<std::size_t>(PrefixLength, Token.Lexeme.size())));
				if (Prefix.empty())
				{
					return {};
				}
			}

			FCompletionResult Result;
			Result.bSuccess = true;
			Result.Prefix = Prefix;
			Result.bHasReplaceRange = true;
			Result.ReplaceLine = bHasIdentifier ? Token.Location.Line : Position.Line;
			Result.ReplaceStartColumn = bHasIdentifier
			    ? Token.Location.Column - (bAtSigil ? 1u : 0u)
			    : Position.Column - 1;
			Result.ReplaceEndColumn = bHasIdentifier
			    ? Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size())
			    : Position.Column;
			for (const FDocumentSymbol& Symbol : AnnotationSymbols)
			{
				const bool bSigilName = !Symbol.Name.empty() && Symbol.Name.front() == '@';
				if (bSigilName != bAtSigil)
				{
					continue;
				}
				const std::string MatchName =
				    bSigilName ? Symbol.Name.substr(1) : Symbol.Name;
				if (!PrefixMatches(MatchName, Prefix))
				{
					continue;
				}
				FCompletionItem Item;
				Item.Kind = EDocumentSymbolKind::Annotation;
				Item.Label = Symbol.Name;
				Item.Detail = Symbol.Detail;
				Item.InsertText = Symbol.Name;
				Item.bSoftBuiltin = true;
				Result.Items.push_back(std::move(Item));
			}
			if (Result.Items.empty())
			{
				return {};
			}
			FinalizeCompletionItems(
			    Result.Items, ECompletionRankContext::General, Result.Prefix);
			return Result;
		}

		FCompletionResult CompleteHostAnnotationArguments(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FTextPosition Position,
		    const VeroScript::IHostBindingCatalog* const Bindings)
		{
			if (!Bindings)
			{
				return {};
			}

			FToken Token;
			const bool bHasIdentifier = FindIdentifierAt(Source, FileLabel, Position, Token);
			std::size_t StartOffset = 0;
			FCompletionResult Result;
			Result.bSuccess = true;
			Result.bHasReplaceRange = true;
			if (bHasIdentifier)
			{
				if (!FindSourceOffset(
				        Source, Token.Location.Line, Token.Location.Column, StartOffset))
				{
					return {};
				}
				Result.ReplaceLine = Token.Location.Line;
				Result.ReplaceStartColumn = Token.Location.Column;
				Result.ReplaceEndColumn =
				    Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size());
				const std::uint32_t Relative =
				    Position.Column >= Token.Location.Column
				    ? Position.Column - Token.Location.Column
				    : 0;
				Result.Prefix.assign(
				    Token.Lexeme.substr(
				        0, std::min<std::size_t>(Relative, Token.Lexeme.size())));
			}
			else
			{
				if (!FindSourceOffset(Source, Position.Line, Position.Column, StartOffset))
				{
					return {};
				}
				Result.ReplaceLine = Position.Line;
				Result.ReplaceStartColumn = Position.Column;
				Result.ReplaceEndColumn = Position.Column;
			}

			const VeroScript::FHostAnnotationDesc* const Annotation =
			    FindEnclosingHostAnnotationAtOffset(Source, StartOffset, Bindings);
			if (!Annotation)
			{
				return {};
			}
			std::size_t Previous = StartOffset;
			while (Previous > 0 && std::isspace(static_cast<unsigned char>(Source[Previous - 1])))
			{
				--Previous;
			}
			if (Previous == 0 || (Source[Previous - 1] != '(' && Source[Previous - 1] != ','))
			{
				return {};
			}

			for (const VeroScript::FHostAnnotationArgumentDesc& Argument :
			    Annotation->Arguments)
			{
				if (!PrefixMatches(Argument.Name, Result.Prefix))
				{
					continue;
				}
				FCompletionItem Item;
				Item.Kind = EDocumentSymbolKind::AnnotationArgument;
				Item.Label = Argument.Name;
				Item.Detail = Argument.Detail.empty()
				    ? std::string("annotation argument")
				    : Argument.Detail;
				Item.InsertText = Argument.Name;
				Item.bSoftBuiltin = true;
				Result.Items.push_back(std::move(Item));
			}
			FinalizeCompletionItems(
			    Result.Items, ECompletionRankContext::General, Result.Prefix);
			return Result;
		}

		std::string FindOverrideOwnerType(
		    const FProgram& Program,
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FTextPosition Position,
		    const std::string_view Prefix)
		{
			FQueryLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			if (ClassifyStructuralCompletionScope(Tokens, Position) != EStructuralCompletionScope::TypeBody)
			{
				return {};
			}
			std::vector<std::size_t> OpenBraces;
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				if (!TokenEndsAtOrBefore(Tokens[Index], Position))
				{
					break;
				}
				if (Tokens[Index].Lexeme == "{")
				{
					OpenBraces.push_back(Index);
				}
				else if (Tokens[Index].Lexeme == "}" && !OpenBraces.empty())
				{
					OpenBraces.pop_back();
				}
			}
			if (OpenBraces.empty())
			{
				return {};
			}
			const std::size_t Open = OpenBraces.back();
			std::size_t HeaderStart = Open;
			while (HeaderStart > 0)
			{
				const std::string_view Lexeme = Tokens[HeaderStart - 1].Lexeme;
				if (Lexeme == "{" || Lexeme == "}" || Lexeme == ";")
				{
					break;
				}
				--HeaderStart;
			}
			std::size_t DeclIndex = Tokens.size();
			for (std::size_t Index = HeaderStart; Index + 1 < Open; ++Index)
			{
				if (Tokens[Index].Kind == ETokenKind::Keyword && (Tokens[Index].Lexeme == "class" || Tokens[Index].Lexeme == "struct"))
				{
					DeclIndex = Index;
					break;
				}
			}
			if (DeclIndex >= Open || Tokens[DeclIndex + 1].Kind != ETokenKind::Identifier)
			{
				return {};
			}
			// A method header, field initializer or nested block is not a legal
			// insertion site. Only an optional identifier prefix may follow the
			// last completed member or access label at this brace depth.
			int Depth = 1;
			std::size_t SegmentStart = Open + 1;
			for (std::size_t Index = Open + 1; Index < Tokens.size(); ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (!TokenEndsAtOrBefore(Token, Position))
				{
					break;
				}
				if (Token.Lexeme == "{")
				{
					++Depth;
					continue;
				}
				if (Token.Lexeme == "}")
				{
					--Depth;
					if (Depth == 1)
					{
						SegmentStart = Index + 1;
					}
					continue;
				}
				if (Depth != 1)
				{
					continue;
				}
				if (Token.Lexeme == ";")
				{
					SegmentStart = Index + 1;
				}
				if (Token.Lexeme == ":" && Index > 0 && (Tokens[Index - 1].Lexeme == "public" || Tokens[Index - 1].Lexeme == "protected" || Tokens[Index - 1].Lexeme == "private"))
				{
					SegmentStart = Index + 1;
				}
			}
			std::size_t SegmentTokens = 0;
			for (std::size_t Index = SegmentStart; Index < Tokens.size(); ++Index)
			{
				if (!TokenEndsAtOrBefore(Tokens[Index], Position))
				{
					break;
				}
				if (Tokens[Index].Kind == ETokenKind::EndOfFile)
				{
					break;
				}
				if (++SegmentTokens > 1 || Prefix.empty() || Tokens[Index].Kind != ETokenKind::Identifier || Tokens[Index].Lexeme.substr(0, Prefix.size()) != Prefix)
				{
					return {};
				}
			}
			std::size_t CursorOffset = 0;
			if (!FindSourceOffset(Source, Position.Line, Position.Column, CursorOffset))
			{
				return {};
			}
			while (CursorOffset < Source.size() && (Source[CursorOffset] == ' ' || Source[CursorOffset] == '\t'))
			{
				++CursorOffset;
			}
			// Completing an existing method header must never insert a second
			// declaration in front of its parameter list or body.
			if (CursorOffset < Source.size() && (Source[CursorOffset] == '(' || Source[CursorOffset] == '{' || Source[CursorOffset] == ';' || Source[CursorOffset] == '='))
			{
				return {};
			}
			const FToken& DeclToken = Tokens[DeclIndex];
			const auto MatchOwner = [&](const auto& Decls) -> std::string
			{
				for (const auto& Decl : Decls)
				{
					if (Decl.Location.Line == DeclToken.Location.Line && Decl.Location.Column == DeclToken.Location.Column)
					{
						std::string Name = Decl.Name;
						const auto& Parameters = TemplateParameterNames(Decl);
						if (!Parameters.empty())
						{
							Name.push_back('<');
							for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
							{
								if (Index > 0)
								{
									Name += ", ";
								}
								Name += Parameters[Index];
							}
							Name.push_back('>');
						}
						return Name;
					}
				}
				return {};
			};
			return DeclToken.Lexeme == "class"
			    ? MatchOwner(Program.Classes)
			    : MatchOwner(Program.Structs);
		}

		FCompletionResult CompleteLoaded(
		    const FProgram& Program,
		    const FDocumentSymbolResult& SymbolsResult,
		    std::string_view OriginalSource,
		    std::string_view FileLabel,
		    const FTextPosition Position,
		    const std::vector<std::string_view>& Defines = {},
		    const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros = {},
		    const VeroScript::IHostBindingCatalog* Bindings = nullptr)
		{
			const auto QueryBindings = SourceMetadataQueryBindings(Bindings, Program.SourceMetadata.get());
			const auto ScopedBindings = ScopedQueryBindings(Program, QueryBindings.get(), FileLabel);
			Bindings = ScopedBindings.get();
			// Preprocessor completion does not need a successful program load.
			FPreprocessorDirectiveCompletionContext DirectiveContext;
			if (FindPreprocessorDirectiveCompletionContext(
			        OriginalSource,
			        Position,
			        DirectiveContext))
			{
				return CompletePreprocessorDirectives(
				    DirectiveContext,
				    OriginalSource,
				    Defines,
				    PredefinedMacros);
			}
			if (IsHashDirectiveLine(OriginalSource, Position))
			{
				FCompletionResult Empty;
				Empty.bSuccess = true;
				return Empty;
			}

			if (!SymbolsResult.bSuccess)
			{
				FCompletionResult Failed = FailCompletion(SymbolsResult.Error);
				if (!SymbolsResult.Diagnostics.empty())
				{
					Failed.Diagnostics = SymbolsResult.Diagnostics;
				}
				return Failed;
			}

			VeroScript::FPreprocessLineMap LineMap;
			std::string PreprocessError;
			if (const auto Snapshot = ProgramPreprocessorLineMap(Program, FileLabel))
			{
				LineMap = *Snapshot;
			}

			else
			{
				BuildPreprocessorLineMap(
				    OriginalSource, Defines, PredefinedMacros, FileLabel, LineMap, PreprocessError);
			}

			if (!IsActiveCodeLine(LineMap, Position.Line))
			{
				FCompletionResult Empty;
				Empty.bSuccess = true;
				return Empty;
			}

			FTextPosition AstPosition = Position;
			std::uint32_t AstLine = 0;
			std::uint32_t AstColumn = 0;
			if (MapCaretPhysicalToPreprocessed(
			        LineMap,
			        Position.Line,
			        Position.Column,
			        AstLine,
			        AstColumn))
			{
				AstPosition.Line = AstLine;
				AstPosition.Column = AstColumn;
			}

			const std::string ActiveNamespace =
			    VeroScriptSymbolQuery::ResolveEnclosingNamespaceName(Program, AstPosition,
			        FindActiveNamespaceAt(OriginalSource, FileLabel, Position));
			FNamespaceQueryScope NamespaceScope(Program, SymbolsResult.Symbols);
			FCompletionResult Result;
			Result.bSuccess = true;
			std::optional<EInitializedCaptureRole> InitializedRole;
			const auto GetInitializedRole = [&]()
			{
				if (!InitializedRole)
				{
					InitializedRole = FindInitializedCaptureRole(Program, AstPosition);
				}
				return *InitializedRole;
			};
			const auto ApplyContextualSnippets = [&](const EKeywordCompletionMode Mode, auto& Items)
			{
				// A parsed identifier/member initializer stores a callable as a value.
				// Defer the AST walk until this value boundary can actually matter.
				if (((InitializedRole && *InitializedRole == EInitializedCaptureRole::ValueInitializer) || HasFollowingTokenAfterCompletion(OriginalSource, Result, Position, "]") || HasFollowingTokenAfterCompletion(OriginalSource, Result, Position, ")") || HasFollowingTokenAfterCompletion(OriginalSource, Result, Position, "<") || HasFollowingTokenAfterCompletion(OriginalSource, Result, Position, ",")) && GetInitializedRole() == EInitializedCaptureRole::ValueInitializer)
				{
					return;
				}
				ApplyCallableSnippets(OriginalSource, Result, Mode, Position, Items);
			};

			std::optional<FSourceLocation> ValueArgumentHead;
			EKeywordCompletionMode KeywordMode =
			    ClassifyKeywordCompletionMode(OriginalSource, FileLabel, Position, true, &ValueArgumentHead);
			if (KeywordMode == EKeywordCompletionMode::DeclaratorName && ValueArgumentHead)
			{
				// `Buffer<2> name` declares a variable; `local<2> name` compares values.
				// Use the resolved head, including local shadowing and qualified receivers.
				const auto& Head = *ValueArgumentHead;
				const auto Target = QueryLoaded(Program, SymbolsResult, OriginalSource, FileLabel,
				    {Head.Line, Head.Column}, {}, Defines, PredefinedMacros, Bindings, &LineMap, false);
				bool bValueHead = false;
				FQueryLexer HeadLexer(OriginalSource, FileLabel);
				const auto& HeadTokens = HeadLexer.Tokenize();
				for (std::size_t Index = 0; Index < HeadTokens.size(); ++Index)
				{
					const auto& Token = HeadTokens[Index];
					if (Token.Location.Line != Head.Line || Token.Location.Column != Head.Column)
					{
						continue;
					}
					const bool bQualified = Index > 0 && (HeadTokens[Index - 1].Lexeme == "::" || HeadTokens[Index - 1].Lexeme == "." || HeadTokens[Index - 1].Lexeme == "->");
					if (!bQualified && Token.Kind == ETokenKind::Identifier)
					{
						std::vector<FSymbolQueryHit> Values;
						CollectVisibleLocals(Program, AstPosition, Values);
						bValueHead = IsKnownValueReceiver(Program, AstPosition, Token.Lexeme,
						    Values, SymbolsResult, ActiveNamespace);
					}
					break;
				}
				if (bValueHead || std::any_of(Target.Definitions.begin(), Target.Definitions.end(), [](const auto& Hit)
				{
					return Hit.Kind == EDocumentSymbolKind::Variable || Hit.Kind == EDocumentSymbolKind::Constant || Hit.Kind == EDocumentSymbolKind::Field || Hit.Kind == EDocumentSymbolKind::EnumMember;
				}))
				{
					KeywordMode = EKeywordCompletionMode::Expression;
				}
			}
			if (KeywordMode == EKeywordCompletionMode::TypeName)
			{
				// Lexically `value<pre` and `Generic<pre` are ambiguous. A visible
				// value binding confirms a comparison; preserve expression candidates.
				FQueryLexer Lexer(OriginalSource, FileLabel);
				const std::vector<FToken>& Tokens = Lexer.Tokenize();
				std::vector<std::size_t> Angles;
				for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
				{
					const FToken& Token = Tokens[Index];
					if (Token.Location.Line > Position.Line || (Token.Location.Line == Position.Line && Token.Location.Column >= Position.Column))
					{
						break;
					}
					if (Token.Lexeme == ";" || Token.Lexeme == "{" || Token.Lexeme == "}")
					{
						Angles.clear();
					}
					else if (Token.Lexeme == "<" && IsGenericTypeHeadBeforeAngle(Tokens, Index))
					{
						Angles.push_back(Index - 1);
					}
					else if (const int Levels = AngleCloseLevels(Token.Lexeme); Levels > 0)
					{
						for (int Level = 0; Level < Levels && !Angles.empty(); ++Level)
						{
							Angles.pop_back();
						}
					}
				}
				if (!Angles.empty())
				{
					const std::size_t Head = Angles.back();
					const bool bQualified = Head > 0 && (Tokens[Head - 1].Lexeme == "." || Tokens[Head - 1].Lexeme == "::" || Tokens[Head - 1].Lexeme == "->");
					if (bQualified && Tokens[Head].Kind == ETokenKind::Identifier)
					{
						const auto& Location = Tokens[Head].Location;
						const FSymbolQueryResult Receiver = QueryLoaded(Program, SymbolsResult,
						    OriginalSource, FileLabel, {Location.Line, Location.Column}, {}, Defines,
						    PredefinedMacros, Bindings, &LineMap, false);
						if (std::any_of(Receiver.Definitions.begin(), Receiver.Definitions.end(),
						        [](const FSymbolQueryHit& Hit)
						{
							return Hit.Kind == EDocumentSymbolKind::Variable || Hit.Kind == EDocumentSymbolKind::Constant || Hit.Kind == EDocumentSymbolKind::Field || Hit.Kind == EDocumentSymbolKind::EnumMember;
						}))
						{
							KeywordMode = EKeywordCompletionMode::Expression;
						}
					}
					if (!bQualified && Tokens[Head].Kind == ETokenKind::Identifier)
					{
						std::vector<FSymbolQueryHit> Values;
						CollectVisibleLocals(Program, AstPosition, Values);
						const auto Local = std::find_if(Values.begin(), Values.end(), [&](const FSymbolQueryHit& Value)
						{
							return Value.Name == Tokens[Head].Lexeme;
						});
						bool bValue = Local != Values.end() && !IsTypeSymbolKind(Local->Kind);
						if (Local == Values.end())
						{
							std::unordered_map<std::string, FCompletionItem> VisibleSymbols;
							AppendCompletionFromSymbols(SymbolsResult.Symbols, Tokens[Head].Lexeme,
							    false, ActiveNamespace, VisibleSymbols);
							AppendUsingNamespaceCompletions(Program, SymbolsResult.Symbols, ActiveNamespace,
							    AstPosition, Tokens[Head].Lexeme, VisibleSymbols);
							const auto Global = VisibleSymbols.find(std::string(Tokens[Head].Lexeme));
							bValue = Global != VisibleSymbols.end() && (Global->second.Kind == EDocumentSymbolKind::Variable || Global->second.Kind == EDocumentSymbolKind::Constant);
						}
						if (bValue)
						{
							KeywordMode = EKeywordCompletionMode::Expression;
						}
					}
				}
			}

			if (FCompletionResult NamespaceTarget = CompleteUsingNamespaceTarget(
			        Program,
			        SymbolsResult.Symbols,
			        OriginalSource,
			        FileLabel,
			        Position);
			    NamespaceTarget.bSuccess)
			{
				return NamespaceTarget;
			}

			FMemberCompletionContext MemberContext;
			if (FindMemberCompletionContext(OriginalSource, FileLabel, Position, MemberContext))
			{
				Result.Prefix = MemberContext.Prefix;
				Result.bHasReplaceRange = MemberContext.bHasReplaceRange;
				Result.ReplaceLine = MemberContext.ReplaceLine;
				Result.ReplaceStartColumn = MemberContext.ReplaceStartColumn;
				Result.ReplaceEndColumn = MemberContext.ReplaceEndColumn;

				auto FinishSortedItems =
				    [&Result](
				        std::unordered_map<std::string, FCompletionItem>& Items,
				        const ECompletionRankContext Context)
				{
					Result.Items.reserve(Items.size());
					for (auto& Pair : Items)
					{
						Result.Items.push_back(std::move(Pair.second));
					}
					FinalizeCompletionItems(Result.Items, Context, Result.Prefix);
				};

				auto CompleteNamespacePrefix = [&]()
				{
					if (MemberContext.bPostfixReceiver)
					{
						return false;
					}
					const bool bExistingScope = HasFollowingTokenAfterCompletion(
					    OriginalSource, Result, Position, "::");
					const auto IsImportedStdlibModule = [&](const std::string_view Module)
					{
						for (const FImportDecl& Import : Program.Imports)
						{
							if (!Import.bImplicit && Import.ImporterModulePath != Program.EntryModulePath)
							{
								continue;
							}
							const std::string Resolved = PathUtil::Normalize(Import.ResolvedPath);
							if (Resolved.size() >= Module.size() && Resolved.compare(Resolved.size() - Module.size(), Module.size(), Module) == 0 && (Resolved.size() == Module.size() || Resolved[Resolved.size() - Module.size() - 1] == '/'))
							{
								return true;
							}
						}
						return false;
					};
					std::unordered_map<std::string, FCompletionItem> Items;
					const auto Targets = BindQueryQualifiedPath(Program, SymbolsResult.Symbols,
					    MemberContext.ReceiverExpression.rfind("::", 0) == 0 ? "::" + MemberContext.ReceiverName : MemberContext.ReceiverName,
					    ActiveNamespace, AstPosition);
					for (const auto& Target : Targets)
					{
						const std::string NsPrefix = Target + ".";
						const auto* NamespaceMetadata = FindStandardNamespaceMetadata(Target);
						const bool bRequiresStdlibImport = NamespaceMetadata && NamespaceMetadata->Module.rfind("stdlib/", 0) == 0 && !IsImportedStdlibModule(NamespaceMetadata->Module);
						for (const FDocumentSymbol& Symbol : SymbolsResult.Symbols)
						{
							if (Symbol.Name.size() <= NsPrefix.size() || Symbol.Name.compare(0, NsPrefix.size(), NsPrefix) != 0)
							{
								continue;
							}
							// Generic declaration parameters are metadata, not a namespace
							// path segment or literal text to insert into the user's source.
							const std::string LeafFull = StripGenericArgs(Symbol.Name.substr(NsPrefix.size()));
							// Offer the next path segment only (`A.` → `B` for `A.B.C`).
							const std::size_t Seg = LeafFull.find('.');
							const std::string Leaf = Seg == std::string::npos
							    ? LeafFull
							    : LeafFull.substr(0, Seg);
							if (Leaf.empty() || !PrefixMatches(Leaf, Result.Prefix))
							{
								continue;
							}
							FCompletionItem Item;
							Item.Kind = Seg == std::string::npos
							    ? Symbol.Kind
							    : EDocumentSymbolKind::Namespace;
							Item.Label = Leaf;
							Item.LabelDescription = Target;
							for (std::size_t Dot = 0;
							    (Dot = Item.LabelDescription.find('.', Dot)) != std::string::npos;
							    Dot += 2)
							{
								Item.LabelDescription.replace(Dot, 1, "::");
							}
							Item.Detail = Seg == std::string::npos
							    ? Symbol.Detail
							    : std::string("namespace");
							if (Seg == std::string::npos)
							{
								Item.Documentation = Symbol.Documentation;
							}
							if (Seg == std::string::npos && bRequiresStdlibImport && Symbol.bSoftBuiltin)
							{
								Item.Detail += "\nRequires import \"";
								Item.Detail += NamespaceMetadata->Module;
								Item.Detail += "\" to compile.";
							}
							Item.InsertText = Item.Kind == EDocumentSymbolKind::Namespace && !bExistingScope ? Leaf + "::" : Leaf;
							Item.bSoftBuiltin = Symbol.bSoftBuiltin;
							const auto Existing = Items.find(Leaf);
							// A path inferred from a child does not carry its parent's
							// declaration or documentation. Preserve an existing namespace;
							// a direct namespace symbol can still replace an inferred one.
							if (Seg != std::string::npos && Existing != Items.end() && Existing->second.Kind == EDocumentSymbolKind::Namespace)
							{
								continue;
							}
							const bool bDistinctOverload = Existing != Items.end() && IsCallableCompletionKind(Existing->second.Kind) && IsCallableCompletionKind(Item.Kind) && Existing->second.Detail != Item.Detail;
							const std::string Key = bDistinctOverload
							    ? Leaf + '\x1f' + Item.Detail
							    : Leaf;
							Items.insert_or_assign(Key, std::move(Item));
						}
					}
					ApplyContextualSnippets(KeywordMode, Items);
					MergeCallableOverloadItems(Items);
					FinishSortedItems(Items, ECompletionRankContext::NamespaceLeaf);
					return !Result.Items.empty();
				};

				std::vector<FSymbolQueryHit> ReceiverLocals;
				CollectVisibleLocals(Program, AstPosition, ReceiverLocals);
				const bool bKnownValueReceiver = IsKnownValueReceiver(
				    Program, AstPosition, MemberContext.ReceiverName, ReceiverLocals, SymbolsResult, ActiveNamespace);
				std::string TypeName = ResolveReceiverExpressionType(Program, SymbolsResult, AstPosition,
				    MemberContext.ReceiverExpression, Bindings, ActiveNamespace, &ReceiverLocals);
				const bool bResolvedExpression = !TypeName.empty();
				const bool bTypeApplicationReceiver = MemberContext.bTypeApplicationPath && !bResolvedExpression;
				if (bTypeApplicationReceiver)
				{
					TypeName = ResolveReceiverTypeApplication(Program, AstPosition, MemberContext.ReceiverExpression,
					    Bindings, ActiveNamespace, ReceiverLocals);
					if (TypeName.empty()) return Result;
				}
				if (TypeName == "void") return Result;
				if (!bResolvedExpression && (MemberContext.bPostfixReceiver || bKnownValueReceiver))
				{
					return Result;
				}
				if (TypeName.empty())
				{
					TypeName = MemberContext.ResolvedTypeName;
				}
				if (TypeName.empty())
				{
					TypeName = ResolveReceiverTypeName(
					    Program,
					    SymbolsResult,
					    AstPosition,
					    MemberContext.ReceiverName, &ReceiverLocals);
				}
				if (!bResolvedExpression && MemberContext.bIndexElement)
				{
					std::string ElementType;
					if (ResolveContainerIndexElementType(TypeName, ElementType))
					{
						TypeName = std::move(ElementType);
					}
				}
				FCallableAccessContext CallableAccessContext;
				const FCallableAccessContext* const CallableAccess =
				    VeroScriptSymbolQuery::ResolveEnclosingCallableAccessContext(
				        Program, AstPosition, CallableAccessContext)
				    ? &CallableAccessContext
				    : nullptr;
				if (!bResolvedExpression && MemberContext.bMethodReturn && !MemberContext.MethodName.empty())
				{
					const std::string OwnerType = ResolveReceiverTypeName(
					    Program,
					    SymbolsResult,
					    AstPosition,
					    MemberContext.ReceiverName);
					// Recognized type receivers are handled by expression inference.
					// A failed static call must not fall back to an instance signature.
					if (!bKnownValueReceiver && IsKnownTypeName(StripGenericArgs(OwnerType), SymbolsResult.Symbols, Bindings))
					{
						return Result;
					}
					std::string ReturnType;
					bool bResolvedReturn = false;
					if (ResolveSoftContainerMethodReturnType(
					        OwnerType,
					        MemberContext.MethodName,
					        ReturnType, &Program))
					{
						TypeName = std::move(ReturnType);
						bResolvedReturn = true;
					}
					else if (ResolveHostMemberReturnTypeName(
					             Bindings,
					             OwnerType,
					             MemberContext.MethodName,
					             ReturnType,
					             &Program))
					{
						TypeName = std::move(ReturnType);
						bResolvedReturn = true;
					}
					else
					{
						std::unordered_map<std::string, FInheritedMemberCompletion> OwnerMembers;
						const std::string AccessContext =
						    VeroScriptSymbolQuery::ResolveEnclosingTypeName(Program, AstPosition);
						AppendCompletionMembersAlongHierarchy(
						    Program,
						    SymbolsResult.Symbols,
						    OwnerType,
						    MemberContext.MethodName,
						    OwnerMembers,
						    AccessContext,
						    CallableAccess);
						const auto Found = OwnerMembers.find(MemberContext.MethodName);
						if (Found != OwnerMembers.end())
						{
							const std::string Candidate = TypeNameFromDetail(Found->second.Detail);
							if (!Candidate.empty() && Candidate != "void")
							{
								TypeName = Candidate;
								bResolvedReturn = true;
							}
						}
					}
					if (!bResolvedReturn)
					{
						std::string ExtensionReturn;
						bool bAmbiguousReturn = false;
						for (const FFunctionDecl& Function : Program.Functions)
						{
							std::string Candidate;
							if (!bKnownValueReceiver || Function.bDeleted || ExtensionMethodLeafName(Function.Name) != MemberContext.MethodName || !ExtensionFunctionIsVisible(Program, Function, FileLabel, ActiveNamespace, AstPosition, Bindings) || !ResolveExtensionMethodReturnType(Function, OwnerType, Candidate) || Candidate == "void")
							{
								continue;
							}
							if (ExtensionReturn.empty())
							{
								ExtensionReturn = std::move(Candidate);
							}
							else if (ExtensionReturn != Candidate)
							{
								bAmbiguousReturn = true;
							}
						}
						if (!ExtensionReturn.empty() && !bAmbiguousReturn)
						{
							TypeName = std::move(ExtensionReturn);
							bResolvedReturn = true;
						}
					}
					if (!bResolvedReturn)
					{
						// `N.Actor()` — method-return parse with no matching method.
						const std::string CtorType = ResolveQualifiedCtorTypeName(
						    Program,
						    SymbolsResult,
						    MemberContext.ReceiverName,
						    MemberContext.MethodName);
						if (!CtorType.empty())
						{
							TypeName = CtorType;
						}
					}
				}

				std::unordered_map<std::string, FInheritedMemberCompletion> MembersByLabel;
				std::string AccessContext =
				    VeroScriptSymbolQuery::ResolveEnclosingTypeName(Program, AstPosition);
				// `this.` / `Super.` completions use the enclosing type's access domain.
				if (AccessContext.empty() && (MemberContext.ReceiverName == "this" || MemberContext.ReceiverName == "Super") && !TypeName.empty())
				{
					AccessContext = MemberContext.ReceiverName == "this"
					    ? TypeName
					    : VeroScriptSymbolQuery::ResolveEnclosingTypeName(Program, AstPosition);
					if (AccessContext.empty())
					{
						AccessContext = TypeName;
					}
				}
				AppendCompletionMembersAlongHierarchy(
				    Program,
				    SymbolsResult.Symbols,
				    TypeName,
				    Result.Prefix,
				    MembersByLabel,
				    AccessContext,
				    CallableAccess);
				AppendSoftContainerMembers(TypeName, Result.Prefix, MembersByLabel, &Program);
				if (bTypeApplicationReceiver)
				{
					for (auto It = MembersByLabel.begin(); It != MembersByLabel.end();)
					{
						const auto& Member = It->second;
						if ((Member.Kind == EDocumentSymbolKind::Field || Member.Kind == EDocumentSymbolKind::Method) && !Member.bIsStatic)
						{
							It = MembersByLabel.erase(It);
						}
						else
						{
							++It;
						}
					}
				}

				// Match the compiler's uniform-call fallback. Real members already in
				// MembersByLabel keep priority over equally named free functions.
				for (const FFunctionDecl& Function : Program.Functions)
				{
					if ((!bResolvedExpression && !bKnownValueReceiver) || Function.Parameters.empty() || Function.bDeleted || !ExtensionFunctionIsVisible(Program, Function, FileLabel, ActiveNamespace, AstPosition, Bindings) || !ExtensionReceiverTypeMatches(Function, TypeName))
					{
						continue;
					}
					const std::string Leaf = ExtensionMethodLeafName(Function.Name);
					if (Leaf.empty() || !PrefixMatches(Leaf, Result.Prefix) || MembersByLabel.find(Leaf) != MembersByLabel.end())
					{
						continue;
					}
					FInheritedMemberCompletion Item;
					Item.Kind = EDocumentSymbolKind::Method;
					Item.Label = Leaf;
					Item.Detail = FormatExtensionMethodDetail(Function);
					MembersByLabel.emplace(Leaf, std::move(Item));
				}

				if (Bindings)
				{
					bool bTypeReceiver = bTypeApplicationReceiver;
					if (!bTypeReceiver && !bResolvedExpression && !MemberContext.bIndexElement && !MemberContext.bMethodReturn)
					{
						std::vector<FSymbolQueryHit> VisibleLocals;
						CollectVisibleLocals(Program, AstPosition, VisibleLocals);
						const bool bShadowedByValue = std::any_of(
						    VisibleLocals.begin(),
						    VisibleLocals.end(),
						    [&MemberContext](const FSymbolQueryHit& Hit)
						{
							return Hit.Name == MemberContext.ReceiverName;
						});
						bTypeReceiver = !bShadowedByValue && IsKnownTypeName(MemberContext.ReceiverName, SymbolsResult.Symbols, Bindings);
					}
					std::string HostTypeName = TypeName;
					if (HostTypeName.empty() && !MemberContext.ReceiverName.empty())
					{
						HostTypeName = MemberContext.ReceiverName;
					}
					const VeroScript::FHostTypeDesc* HostType =
					    FindHostTypeAlongHierarchy(Bindings, Program, HostTypeName);
					if (HostType)
					{
						std::unordered_set<std::string> AllowedHostMemberNames;
						for (const VeroScript::FHostMemberDesc& Member : HostType->Members)
						{
							if (HostMemberCanBeNamedFromReceiver(Member, bTypeReceiver))
							{
								AllowedHostMemberNames.insert(Member.Name);
							}
						}
						for (const VeroScript::FHostMemberDesc& Member : HostType->Members)
						{
							if (!HostMemberCanBeNamedFromReceiver(Member, bTypeReceiver))
							{
								const auto Existing = MembersByLabel.find(Member.Name);
								if (Existing != MembersByLabel.end() && AllowedHostMemberNames.find(Member.Name) == AllowedHostMemberNames.end() && Existing->second.bSoftBuiltin)
								{
									MembersByLabel.erase(Existing);
								}
								continue;
							}
							if (!PrefixMatches(Member.Name, Result.Prefix) || MembersByLabel.find(Member.Name) != MembersByLabel.end())
							{
								continue;
							}
							FInheritedMemberCompletion Item;
							Item.Label = Member.Name;
							Item.Detail = VeroScript::FormatHostAnnotations(
							    Member.Annotations);
							if (!Item.Detail.empty())
							{
								Item.Detail.push_back(' ');
							}
							Item.Detail += FormatHostMemberDetail(
							    Member, HostType->Name);
							Item.bSoftBuiltin = true;
							switch (Member.Kind)
							{
							case VeroScript::EHostMemberKind::EnumMember:
								{
									Item.Kind = EDocumentSymbolKind::EnumMember;
									break;
								}
							case VeroScript::EHostMemberKind::StaticConstant:
								{
									Item.Kind = EDocumentSymbolKind::Constant;
									break;
								}
							case VeroScript::EHostMemberKind::PropertyGetter:
							case VeroScript::EHostMemberKind::Field:
								{
									Item.Kind = EDocumentSymbolKind::Field;
									break;
								}
							default:
								{
									Item.Kind = EDocumentSymbolKind::Method;
									break;
								}
							}
							MembersByLabel.insert_or_assign(Member.Name, std::move(Item));
						}
					}
				}

				if (MembersByLabel.empty() && !MemberContext.bIndexElement && !MemberContext.bMethodReturn && MemberContext.ResolvedTypeName.empty() && CompleteNamespacePrefix())
				{
					return Result;
				}

				std::unordered_map<std::string, FCompletionItem> Items;
				for (auto& Pair : MembersByLabel)
				{
					FCompletionItem Item;
					Item.Kind = Pair.second.Kind;
					Item.Label = Pair.second.Label;
					Item.Detail = std::move(Pair.second.Detail);
					Item.InsertText = Pair.second.Label;
					Item.bSoftBuiltin = Pair.second.bSoftBuiltin;
					Items.insert_or_assign(Pair.first, std::move(Item));
				}
				ApplyContextualSnippets(KeywordMode, Items);
				MergeCallableOverloadItems(Items);
				FinishSortedItems(Items, ECompletionRankContext::Member);
				return Result;
			}

			FGotoCompletionContext GotoContext;
			if (FindGotoCompletionContext(OriginalSource, FileLabel, Position, GotoContext))
			{
				Result.Prefix = GotoContext.Prefix;
				Result.bHasReplaceRange = GotoContext.bHasReplaceRange;
				Result.ReplaceLine = GotoContext.ReplaceLine;
				Result.ReplaceStartColumn = GotoContext.ReplaceStartColumn;
				Result.ReplaceEndColumn = GotoContext.ReplaceEndColumn;

				std::unordered_map<std::string, FCompletionItem> Items;
				AppendLabelCompletions(SymbolsResult.Symbols, Result.Prefix, Items);
				Result.Items.reserve(Items.size());
				for (auto& Pair : Items)
				{
					Result.Items.push_back(std::move(Pair.second));
				}
				FinalizeCompletionItems(
				    Result.Items, ECompletionRankContext::GotoLabel, Result.Prefix);
				return Result;
			}

			FCaptureCompletionContext CaptureContext;
			const bool bCaptureContext = FindCaptureCompletionContext(
			    OriginalSource,
			    FileLabel,
			    Position,
			    CaptureContext);
			const auto CaptureRole = bCaptureContext
			    ? GetInitializedRole()
			    : EInitializedCaptureRole::None;
			if (CaptureRole == EInitializedCaptureRole::Declaration) return Result;
			if (bCaptureContext && CaptureRole != EInitializedCaptureRole::Initializer && CaptureRole != EInitializedCaptureRole::ValueInitializer)
			{
				Result.Prefix = CaptureContext.Prefix;
				Result.bHasReplaceRange = CaptureContext.bHasReplaceRange;
				Result.ReplaceLine = CaptureContext.ReplaceLine;
				Result.ReplaceStartColumn = CaptureContext.ReplaceStartColumn;
				Result.ReplaceEndColumn = CaptureContext.ReplaceEndColumn;

				std::unordered_map<std::string, FCompletionItem> Items;
				const bool bReferencePrefix = !Result.Prefix.empty() && Result.Prefix.front() == '&';
				const std::string_view NamePrefix = bReferencePrefix
				    ? std::string_view(Result.Prefix).substr(1)
				    : std::string_view(Result.Prefix);
				if (!bReferencePrefix && PrefixMatches("=", Result.Prefix))
				{
					FCompletionItem Item;
					Item.Kind = EDocumentSymbolKind::Constant;
					Item.Label = "=";
					Item.Detail = "capture all by value";
					Item.InsertText = "=";
					Items.insert_or_assign("=", std::move(Item));
				}
				if (Result.Prefix.empty() || Result.Prefix == "&")
				{
					FCompletionItem Item;
					Item.Kind = EDocumentSymbolKind::Constant;
					Item.Label = "&";
					Item.Detail = "capture all by reference";
					Item.InsertText = "&";
					Items.insert_or_assign("&", std::move(Item));
				}

				std::vector<FSymbolQueryHit> Locals;
				VeroScriptSymbolQuery::CollectVisibleLocalsAt(
				    Program,
				    OriginalSource,
				    Defines,
				    PredefinedMacros,
				    Position,
				    Locals);
				for (const FSymbolQueryHit& Local : Locals)
				{
					if (Local.Kind == EDocumentSymbolKind::TypeAlias || !PrefixMatches(Local.Name, NamePrefix))
					{
						continue;
					}
					FCompletionItem Item;
					Item.Kind = Local.Kind;
					Item.Label = bReferencePrefix ? ("&" + Local.Name) : Local.Name;
					Item.Detail = Local.Detail;
					if (bReferencePrefix)
					{
						Item.Detail += " (capture by reference)";
					}
					Item.InsertText = Item.Label;
					Item.bSoftBuiltin = Local.bSoftBuiltin;
					Item.ScopeDistance = Local.CompletionScopeDistance;
					const std::string Label = Item.Label;
					Items.insert_or_assign(Label, std::move(Item));
				}
				for (const FDocumentSymbol& Symbol : SymbolsResult.Symbols)
				{
					if (Symbol.Kind != EDocumentSymbolKind::Variable && Symbol.Kind != EDocumentSymbolKind::Constant)
					{
						continue;
					}
					if (!PrefixMatches(Symbol.Name, NamePrefix))
					{
						continue;
					}
					// Locals already inserted win over module-level same-name symbols.
					const std::string Label = bReferencePrefix ? ("&" + Symbol.Name) : Symbol.Name;
					if (Items.find(Label) != Items.end())
					{
						continue;
					}
					FCompletionItem Item;
					Item.Kind = Symbol.Kind;
					Item.Label = Label;
					Item.Detail = Symbol.Detail;
					if (bReferencePrefix)
					{
						Item.Detail += " (capture by reference)";
					}
					Item.InsertText = Label;
					Item.bSoftBuiltin = Symbol.bSoftBuiltin;
					Items.emplace(Label, std::move(Item));
				}

				Result.Items.reserve(Items.size());
				for (auto& Pair : Items)
				{
					Result.Items.push_back(std::move(Pair.second));
				}
				FinalizeCompletionItems(
				    Result.Items, ECompletionRankContext::Capture, Result.Prefix);
				return Result;
			}

			if (FCompletionResult AnnotationArguments =
			        CompleteHostAnnotationArguments(
			            OriginalSource, FileLabel, Position, Bindings);
			    AnnotationArguments.bSuccess)
			{
				return AnnotationArguments;
			}

			// `@name` is a distinct spelling, not an identifier: no local, keyword, or
			// bare symbol can complete there.
			if (FCompletionResult SigilAnnotations = CompleteAnnotations(
			        OriginalSource, FileLabel, Position, Bindings, true);
			    SigilAnnotations.bSuccess)
			{
				return SigilAnnotations;
			}

			FToken Token;
			if (FindIdentifierAt(OriginalSource, FileLabel, Position, Token))
			{
				Result.bHasReplaceRange = true;
				Result.ReplaceLine = Token.Location.Line;
				Result.ReplaceStartColumn = Token.Location.Column;
				Result.ReplaceEndColumn =
				    Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size());
				const std::uint32_t Relative =
				    Position.Column >= Token.Location.Column
				    ? (Position.Column - Token.Location.Column)
				    : 0;
				const std::size_t PrefixLen =
				    Relative <= Token.Lexeme.size() ? Relative : Token.Lexeme.size();
				Result.Prefix = std::string(Token.Lexeme.substr(0, PrefixLen));
			}

			std::unordered_map<std::string, FCompletionItem> Items;
			if (const std::string Owner = FindOverrideOwnerType(
			        Program, OriginalSource, FileLabel, Position, Result.Prefix);
			    !Owner.empty())
			{
				std::vector<FOverrideMemberCandidate> Candidates;
				CollectOverrideMemberCandidates(Program, Owner, Candidates);
				std::size_t CaretOffset = 0;
				std::string Indent;
				if (FindSourceOffset(OriginalSource, Position.Line, Position.Column, CaretOffset))
				{
					const std::size_t LineStart = OriginalSource.rfind('\n', CaretOffset);
					const std::size_t Start = LineStart == std::string_view::npos ? 0 : LineStart + 1;
					for (std::size_t Index = Start; Index < CaretOffset && (OriginalSource[Index] == ' ' || OriginalSource[Index] == '\t'); ++Index)
					{
						Indent.push_back(OriginalSource[Index]);
					}
				}
				for (const FOverrideMemberCandidate& Candidate : Candidates)
				{
					if (!PrefixMatches(Candidate.Name, Result.Prefix))
					{
						continue;
					}
					FCompletionItem Item;
					Item.Kind = EDocumentSymbolKind::Method;
					Item.Label = Candidate.Name;
					Item.Detail = Candidate.Signature;
					Item.Documentation = Candidate.bInterfaceImplementation
					    ? VeroScript::Locale::TranslateId(
					          "lsp.completion.implement_interface_member")
					    : VeroScript::Locale::TranslateId(
					          "lsp.completion.override_inherited_member");
					Item.bImplementsInterfaceMember = Candidate.bInterfaceImplementation;
					Item.InsertText = Candidate.Declaration;
					std::size_t Newline = 0;
					while ((Newline = Item.InsertText.find('\n', Newline)) != std::string::npos)
					{
						Item.InsertText.insert(Newline + 1, Indent);
						Newline += Indent.size() + 1;
					}
					if (OriginalSource.find("\r\n") != std::string_view::npos)
					{
						for (std::size_t Index = 0; Index < Item.InsertText.size(); ++Index)
						{
							if (Item.InsertText[Index] == '\n')
							{
								Item.InsertText.insert(Index++, 1, '\r');
							}
						}
					}
					const std::size_t Stub = Item.InsertText.find("throw 0;");
					if (Stub != std::string::npos)
					{
						Item.InsertText.replace(Stub, 8, "${0:throw 0;}");
					}
					Item.bInsertTextSnippet = true;
					Item.bOverrideMember = true;
					Items.emplace("override\x1f" + Candidate.Signature, std::move(Item));
				}
			}
			AppendCompletionFromSymbols(
			    SymbolsResult.Symbols,
			    Result.Prefix,
			    IsDeclarationAnnotationCompletionSite(OriginalSource, Position),
			    ActiveNamespace,
			    Items,
			    KeywordMode == EKeywordCompletionMode::TypeName || KeywordMode == EKeywordCompletionMode::Statement);
			AppendNamespaceRootCompletions(
			    Program,
			    SymbolsResult.Symbols,
			    Result.Prefix,
			    Items,
			    KeywordMode == EKeywordCompletionMode::Statement || KeywordMode == EKeywordCompletionMode::Expression,
			    HasFollowingTokenAfterCompletion(OriginalSource, Result, Position, "::"));
			AppendUsingNamespaceCompletions(
			    Program,
			    SymbolsResult.Symbols,
			    ActiveNamespace,
			    AstPosition,
			    Result.Prefix,
			    Items);
			AppendGenericHostTemplateCompletions(Bindings, Result.Prefix, Items);

			// Inside an instance method, bare calls may resolve through an implicit
			// `this`. Offer the same accessible method set without making fields look
			// like bare variables. Top-level functions were inserted first and keep
			// their language-defined priority over an equally named member.
			const std::string ImplicitThisType =
			    VeroScriptSymbolQuery::ResolveImplicitThisTypeName(Program, AstPosition);
			if (!ImplicitThisType.empty())
			{
				std::unordered_map<std::string, FInheritedMemberCompletion> Members;
				AppendCompletionMembersAlongHierarchy(
				    Program,
				    SymbolsResult.Symbols,
				    ImplicitThisType,
				    Result.Prefix,
				    Members,
				    ImplicitThisType);
				for (const auto& Pair : Members)
				{
					const FInheritedMemberCompletion& Member = Pair.second;
					const bool bAlreadyVisible = std::any_of(
					    Items.begin(), Items.end(), [&Member](const auto& Existing)
					{
						return Existing.second.Label == Member.Label;
					});
					if (Member.Kind != EDocumentSymbolKind::Method || bAlreadyVisible)
					{
						continue;
					}
					FCompletionItem Item;
					Item.Kind = Member.Kind;
					Item.Label = Member.Label;
					Item.Detail = Member.Detail;
					Item.InsertText = Member.Label;
					Item.bSoftBuiltin = Member.bSoftBuiltin;
					Items.emplace(Item.Label, std::move(Item));
				}
			}

			std::vector<FSymbolQueryHit> Locals;
			CollectVisibleLocals(Program, AstPosition, Locals);
			for (auto& Local : Locals)
			{
				if (!PrefixMatches(Local.Name, Result.Prefix))
				{
					continue;
				}
				RefineCallableLocalType(Program, SymbolsResult, Bindings, Local);
				FCompletionItem Item;
				Item.Kind = Local.Kind;
				Item.Label = Local.Name;
				Item.Detail = Local.Detail;
				Item.InsertText = Local.Name;
				Item.bSoftBuiltin = Local.bSoftBuiltin;
				Item.ScopeDistance = Local.CompletionScopeDistance;
				// A local/parameter shadows every same-name unqualified candidate,
				// including overloads stored under the internal label+signature key.
				const std::string OverloadPrefix = Local.Name + '\x1f';
				for (auto Existing = Items.begin(); Existing != Items.end();)
				{
					if (Existing->first == Local.Name || Existing->first.rfind(OverloadPrefix, 0) == 0)
					{
						Existing = Items.erase(Existing);
					}
					else
					{
						++Existing;
					}
				}
				Items.emplace(Local.Name, std::move(Item));
			}

			// After symbols/locals so same-name user identifiers keep priority.
			AppendLanguageKeywordCompletions(Result.Prefix, Items, KeywordMode);
			// Macros fill remaining gaps (object- and function-like `#define`).
			AppendMacroCompletions(LineMap, OriginalSource, Result.Prefix, Position.Line, Items,
			    true, &Defines, Position.Column);
			SuppressGenericSnippetBeforeExistingAngle(OriginalSource, Result, Items);
			ApplyContextualSnippets(KeywordMode, Items);
			MergeCallableOverloadItems(Items);

			Result.Items.reserve(Items.size());
			for (auto& Pair : Items)
			{
				Result.Items.push_back(std::move(Pair.second));
			}
			FinalizeCompletionItems(
			    Result.Items, RankContextFromKeywordMode(KeywordMode), Result.Prefix);
			return Result;
		}
