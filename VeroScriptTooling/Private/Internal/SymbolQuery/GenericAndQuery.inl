// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		void MapAstDefinitionSitesToPhysical(
		    const FProgram& Program,
		    const VeroScript::FPreprocessLineMap& Map,
		    const std::string_view CurrentFile,
		    std::vector<FSymbolQueryHit>& Hits)
		{
			for (FSymbolQueryHit& Hit : Hits)
			{
				Hit.GeneratedOrigin = GeneratedOriginLocation(Program, Hit.FileLabel);
				if (Hit.bPhysicalLocation)
				{
					continue;
				}

				if (Hit.Kind == EDocumentSymbolKind::Macro || Hit.Detail.rfind("#define", 0) == 0)
				{
					// Type-expanding macros also carry physical definition sites.
					Hit.bPhysicalLocation = true;
					continue;
				}
				const auto* DefinitionMap = &Map;
				if (!Hit.FileLabel.empty() && Hit.FileLabel != CurrentFile && PathUtil::IdentityKey(Hit.FileLabel) != PathUtil::IdentityKey(std::string(CurrentFile)))
				{
					// Imported AST sites belong to their own immutable module map.
					DefinitionMap = ProgramPreprocessorLineMap(Program, Hit.FileLabel);
					if (!DefinitionMap)
					{
						continue;
					}
				}
				std::uint32_t PhysicalLine = 0;
				std::uint32_t PhysicalColumn = 0;
				if (TryRemapPreprocessedToPhysical(
				        *DefinitionMap,
				        Hit.DefLine,
				        Hit.DefColumn,
				        PhysicalLine,
				        PhysicalColumn))
				{
					Hit.DefLine = PhysicalLine;
					Hit.DefColumn = PhysicalColumn;
					Hit.bPhysicalLocation = true;
				}
			}
		}

		bool IsBareCallTokenContext(
		    const std::string_view Source,
		    const std::uint32_t Line,
		    const std::uint32_t StartColumn,
		    const std::uint32_t EndColumn)
		{
			std::size_t StartOffset = 0;
			std::size_t EndOffset = 0;
			if (!FindSourceOffset(Source, Line, StartColumn, StartOffset) || !FindSourceOffset(Source, Line, EndColumn, EndOffset))
			{
				return false;
			}
			std::size_t Next = EndOffset;
			while (Next < Source.size() && std::isspace(static_cast<unsigned char>(Source[Next])))
			{
				++Next;
			}
			if (Next >= Source.size() || Source[Next] != '(')
			{
				return false;
			}
			std::size_t Previous = StartOffset;
			while (Previous > 0 && std::isspace(static_cast<unsigned char>(Source[Previous - 1])))
			{
				--Previous;
			}
			if (Previous == 0)
			{
				return true;
			}
			const char Prefix = Source[Previous - 1];
			return Prefix != '.' && Prefix != ':' && Prefix != '>';
		}

		bool IsCallableDeclarationToken(
		    const FSymbolQueryHit& Hit,
		    const std::string_view Name,
		    const FTextPosition TokenStart,
		    const std::string_view FileLabel,
		    const std::vector<FToken>& Tokens,
		    const FPreprocessLineMap& LineMap)
		{
			if ((Hit.Kind != EDocumentSymbolKind::Function && Hit.Kind != EDocumentSymbolKind::Method && Hit.Kind != EDocumentSymbolKind::Constructor) || Hit.DefLine == 0 || Hit.DefColumn == 0 || (Hit.FileLabel != FileLabel && PathUtil::IdentityKey(Hit.FileLabel) != PathUtil::IdentityKey(std::string(FileLabel))))
			{
				return false;
			}
			FTextPosition HeaderStart{Hit.DefLine, Hit.DefColumn};
			std::uint32_t PhysicalLine = 0;
			std::uint32_t PhysicalColumn = 0;
			if (TryRemapPreprocessedToPhysical(LineMap, Hit.DefLine, Hit.DefColumn,
			        PhysicalLine, PhysicalColumn))
			{
				HeaderStart = {PhysicalLine, PhysicalColumn};
			}
			const auto Begin = std::lower_bound(Tokens.begin(), Tokens.end(), HeaderStart,
			    [](const FToken& Token, const FTextPosition Start)
			{
				return CompareLocation(Token.Location, Start) < 0;
			});
			for (std::size_t Index = static_cast<std::size_t>(Begin - Tokens.begin()); Index + 1 < Tokens.size(); ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (CompareLocation(Token.Location, {TokenStart.Line, TokenStart.Column}) > 0 || Token.Lexeme == ";" || Token.Lexeme == "{" || Token.Lexeme == "}")
				{
					break;
				}
				std::size_t End = Index + 1;
				bool bNameMatches = Token.Kind == ETokenKind::Identifier && Token.Lexeme == Name;
				if (Token.Kind == ETokenKind::Keyword && Token.Lexeme == "operator")
				{
					std::string OperatorName;
					bNameMatches = TryParseOperatorName(Tokens, Index, OperatorName, End) && OperatorNameLeafMatches(OperatorName, Name);
				}
				if (!bNameMatches)
				{
					continue;
				}
				if (End < Tokens.size() && Tokens[End].Lexeme == "<")
				{
					std::size_t Close = 0;
					if (!FindMatchingGenericAngleClose(Tokens, End, Close))
					{
						return false;
					}
					End = Close + 1;
				}
				if (End < Tokens.size() && Tokens[End].Lexeme == "(")
				{
					// The first matching name before this declaration's parameters is
					// its name; subsequent calls in parameters/body are not declarations.
					return Token.Location.Line == TokenStart.Line && Token.Location.Column == TokenStart.Column;
				}
			}
			return false;
		}

		struct FGenericTypeParameterToken
		{
			std::string Name, ValueType;
			bool bValue = false;
			std::uint32_t DefinitionLine = 1;
			std::uint32_t DefinitionColumn = 1;
		};

		using FGenericTypeParameterMap =
		    std::unordered_map<std::string, FGenericTypeParameterToken>;

		bool ParseGenericTypeParameterList(
		    const std::vector<FToken>& Tokens,
		    const std::size_t OpenIndex,
		    std::size_t& OutCloseIndex,
		    FGenericTypeParameterMap& OutParameters)
		{
			if (OpenIndex >= Tokens.size() || Tokens[OpenIndex].Kind != ETokenKind::Symbol || Tokens[OpenIndex].Lexeme != "<")
			{
				return false;
			}
			int AngleDepth = 1, Parentheses = 0, Brackets = 0;
			std::size_t HeadStart = OpenIndex + 1;
			bool HasDefault = false, HasHead = false;
			FGenericTypeParameterToken Parameter;
			const auto Head = [&](const std::size_t End)
			{
				std::size_t NameIndex = Tokens.size();
				int Angles = 0;
				for (std::size_t Index = HeadStart; Index < End; ++Index)
				{
					if (Tokens[Index].Lexeme == "<")
					{
						++Angles;
					}
					else if (Tokens[Index].Lexeme == ">" || Tokens[Index].Lexeme == ">>")
					{
						Angles -= static_cast<int>(Tokens[Index].Lexeme.size());
					}
					else if (Angles == 0 && Tokens[Index].Kind == ETokenKind::Identifier && Tokens[Index].Lexeme != "typename")
					{
						NameIndex = Index;
					}
				}
				if (NameIndex == Tokens.size()) return false;
				Parameter = {};
				Parameter.Name = std::string(Tokens[NameIndex].Lexeme);
				Parameter.DefinitionLine = Tokens[NameIndex].Location.Line;
				Parameter.DefinitionColumn = Tokens[NameIndex].Location.Column;
				Parameter.bValue = NameIndex != HeadStart && Tokens[HeadStart].Lexeme != "typename" && Tokens[HeadStart].Lexeme != "class";
				if (Parameter.bValue)
				{
					for (std::size_t Index = HeadStart; Index < NameIndex; ++Index)
					{
						if (!Parameter.ValueType.empty())
						{
							Parameter.ValueType += ' ';
						}
						Parameter.ValueType += Tokens[Index].Lexeme;
					}
				}
				HasHead = true;
				return true;
			};
			for (std::size_t Index = OpenIndex + 1; Index < Tokens.size(); ++Index)
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
				if (Parentheses != 0 || Brackets != 0)
				{
					continue;
				}
				if (Part == "<" && (!HasDefault || !Parameter.bValue || Tokens[Index - 1].Lexeme == "cast"))
				{
					++AngleDepth;
				}
				else if (Part == ">" || Part == ">>")
				{
					AngleDepth -= static_cast<int>(Part.size());
					if (AngleDepth < 0) return false;
					if (AngleDepth == 0)
					{
						if (!HasHead && !Head(Index)) return false;
						OutParameters.insert_or_assign(Parameter.Name, Parameter);
						OutCloseIndex = Index;
						return true;
					}
				}
				else if (AngleDepth == 1 && Part == "=" && !HasDefault)
				{
					if (!Head(Index)) return false;
					HasDefault = true;
				}
				else if (AngleDepth == 1 && Part == ",")
				{
					if (!HasHead && !Head(Index)) return false;
					OutParameters.insert_or_assign(Parameter.Name, Parameter);
					HeadStart = Index + 1;
					HasDefault = HasHead = false;
				}
			}
			return false;
		}

		std::size_t FindGenericDeclarationEnd(
		    const std::vector<FToken>& Tokens,
		    const std::size_t CloseIndex)
		{
			std::size_t EndIndex = CloseIndex;
			int ParenDepth = 0;
			int AngleDepth = 0;
			for (std::size_t Index = CloseIndex + 1; Index < Tokens.size(); ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (Token.Kind == ETokenKind::Symbol && Token.Lexeme == "(")
				{
					++ParenDepth;
				}
				else if (Token.Kind == ETokenKind::Symbol && Token.Lexeme == ")")
				{
					ParenDepth = std::max(0, ParenDepth - 1);
				}
				else if (Token.Kind == ETokenKind::Symbol && Token.Lexeme == "<")
				{
					++AngleDepth;
				}
				else if (Token.Kind == ETokenKind::Symbol && Token.Lexeme == ">")
				{
					AngleDepth = std::max(0, AngleDepth - 1);
				}
				else if (ParenDepth == 0 && AngleDepth == 0 && Token.Kind == ETokenKind::Symbol && Token.Lexeme == ";")
				{
					return Index;
				}
				else if (ParenDepth == 0 && AngleDepth == 0 && Token.Kind == ETokenKind::Symbol && Token.Lexeme == "{")
				{
					int BraceDepth = 1;
					EndIndex = Index;
					for (std::size_t BodyIndex = Index + 1;
					    BodyIndex < Tokens.size(); ++BodyIndex)
					{
						if (Tokens[BodyIndex].Kind == ETokenKind::Symbol && Tokens[BodyIndex].Lexeme == "{")
						{
							++BraceDepth;
						}
						else if (Tokens[BodyIndex].Kind == ETokenKind::Symbol && Tokens[BodyIndex].Lexeme == "}" && --BraceDepth == 0)
						{
							return BodyIndex;
						}
					}
					return EndIndex;
				}
			}
			return EndIndex;
		}

		void MapGenericTypeParameterRange(
		    const std::vector<FToken>& Tokens,
		    const std::size_t StartIndex,
		    const std::size_t EndIndex,
		    const FGenericTypeParameterMap& Parameters,
		    std::unordered_map<std::size_t, FGenericTypeParameterToken>& Out)
		{
			for (std::size_t Index = StartIndex;
			    Index <= EndIndex && Index < Tokens.size(); ++Index)
			{
				if (Tokens[Index].Kind != ETokenKind::Identifier)
				{
					continue;
				}
				const auto Parameter = Parameters.find(std::string(Tokens[Index].Lexeme));
				if (Parameter != Parameters.end())
				{
					Out.insert_or_assign(Index, Parameter->second);
				}
			}
		}

		bool IsSuffixGenericDeclaration(
		    const std::vector<FToken>& Tokens,
		    const std::size_t NameIndex,
		    const std::size_t CloseIndex,
		    std::size_t& OutStartIndex)
		{
			if (NameIndex > 0 && Tokens[NameIndex - 1].Kind == ETokenKind::Keyword && (Tokens[NameIndex - 1].Lexeme == "class" || Tokens[NameIndex - 1].Lexeme == "struct" || Tokens[NameIndex - 1].Lexeme == "interface"))
			{
				OutStartIndex = NameIndex + 2;
				return true;
			}
			if (CloseIndex + 1 >= Tokens.size() || Tokens[CloseIndex + 1].Kind != ETokenKind::Symbol || Tokens[CloseIndex + 1].Lexeme != "(")
			{
				return false;
			}
			if (NameIndex == 0 || (Tokens[NameIndex - 1].Kind == ETokenKind::Symbol && (Tokens[NameIndex - 1].Lexeme == "." || Tokens[NameIndex - 1].Lexeme == "::")))
			{
				// Qualified generic invocation (`Namespace.Function<T>(...)`), not
				// a declaration. A qualified return type has an identifier here.
				return false;
			}

			std::size_t HeaderStart = NameIndex;
			while (HeaderStart > 0)
			{
				const FToken& Previous = Tokens[HeaderStart - 1];
				if (Previous.Kind == ETokenKind::Symbol && (Previous.Lexeme == ";" || Previous.Lexeme == "{" || Previous.Lexeme == "}" || Previous.Lexeme == ":"))
				{
					break;
				}
				--HeaderStart;
			}
			if (HeaderStart == NameIndex)
			{
				return false;
			}
			for (std::size_t Index = HeaderStart; Index < NameIndex; ++Index)
			{
				const FToken& Token = Tokens[Index];
				if ((Token.Kind == ETokenKind::Keyword && (Token.Lexeme == "return" || Token.Lexeme == "if" || Token.Lexeme == "while" || Token.Lexeme == "do" || Token.Lexeme == "for")) || (Token.Kind == ETokenKind::Symbol && (Token.Lexeme == "=" || Token.Lexeme == "(" || Token.Lexeme == ")" || Token.Lexeme == ",")))
				{
					return false;
				}
			}
			OutStartIndex = HeaderStart;
			return true;
		}

		void CollectGenericTypeParameterTokens(
		    const std::vector<FToken>& Tokens,
		    std::unordered_map<std::size_t, FGenericTypeParameterToken>& Out)
		{
			for (std::size_t Index = 0; Index + 1 < Tokens.size(); ++Index)
			{
				std::size_t OpenIndex = Tokens.size();
				std::size_t StartIndex = Tokens.size();
				if (Tokens[Index].Kind == ETokenKind::Keyword && Tokens[Index].Lexeme == "template")
				{
					OpenIndex = Index + 1;
					StartIndex = OpenIndex + 1;
				}
				else if (Tokens[Index].Kind == ETokenKind::Identifier && Tokens[Index + 1].Kind == ETokenKind::Symbol && Tokens[Index + 1].Lexeme == "<")
				{
					OpenIndex = Index + 1;
				}
				else
				{
					continue;
				}

				std::size_t CloseIndex = Tokens.size();
				FGenericTypeParameterMap Parameters;
				if (!ParseGenericTypeParameterList(
				        Tokens, OpenIndex, CloseIndex, Parameters))
				{
					continue;
				}
				if (StartIndex == Tokens.size() && !IsSuffixGenericDeclaration(Tokens, Index, CloseIndex, StartIndex))
				{
					continue;
				}
				MapGenericTypeParameterRange(
				    Tokens,
				    StartIndex,
				    FindGenericDeclarationEnd(Tokens, CloseIndex),
				    Parameters,
				    Out);
				Index = CloseIndex;
			}
		}

		FSymbolQueryResult QueryLoaded(
		    const FProgram& Program,
		    const FDocumentSymbolResult& SymbolsResult,
		    std::string_view Source,
		    std::string_view FileLabel,
		    const FTextPosition Position,
		    const std::vector<std::string_view>& ImportPaths = {},
		    const std::vector<std::string_view>& Defines = {},
		    const std::vector<VeroScript::FPredefinedMacro>& PredefinedMacros = {},
		    const VeroScript::IHostBindingCatalog* Bindings = nullptr,
		    const VeroScript::FPreprocessLineMap* ExistingLineMap = nullptr,
		    const bool bResolveGenericTypeParameters = true)
		{
			const auto QueryBindings = SourceMetadataQueryBindings(Bindings, Program.SourceMetadata.get());
			const auto ScopedBindings = ScopedQueryBindings(Program, QueryBindings.get(), FileLabel);
			Bindings = ScopedBindings.get();
			CheckAnalysisCancellation();
			if (!ExistingLineMap)
			{
				ExistingLineMap = ProgramPreprocessorLineMap(Program, FileLabel);
			}

			if (!SymbolsResult.bSuccess)
			{
				FSymbolQueryResult Failed = Fail(SymbolsResult.Error);
				if (!SymbolsResult.Diagnostics.empty())
				{
					Failed.Diagnostics = SymbolsResult.Diagnostics;
				}
				return Failed;
			}

			FSymbolQueryResult Result;
			Result.bSuccess = true;

			VeroScript::FPreprocessLineMap OwnedLineMap;
			bool bHasLineMap = ExistingLineMap != nullptr;
			if (!ExistingLineMap)
			{
				std::string PreprocessError;
				bHasLineMap = BuildPreprocessorLineMap(
				    Source,
				    Defines,
				    PredefinedMacros,
				    OwnedLineMap,
				    PreprocessError);
			}
			const VeroScript::FPreprocessLineMap& LineMap =
			    ExistingLineMap ? *ExistingLineMap : OwnedLineMap;
			const FTextPosition AstPosition = PhysicalToAstPosition(LineMap, Position);
			const std::string ActiveNamespace =
			    VeroScriptSymbolQuery::ResolveEnclosingNamespaceName(Program, AstPosition,
			        FindActiveNamespaceAt(Source, FileLabel, Position));
			const auto Finalize = [&](FSymbolQueryResult Out) -> FSymbolQueryResult
			{
				for (auto& Hit : Out.Definitions)
				{
					RefineCallableLocalType(Program, SymbolsResult, Bindings, Hit);
					const auto* Type = Hit.bTypeDefinition ? FindTypeSymbolInTree(SymbolsResult.Symbols, Hit.Detail) : nullptr;
					if (!Type && IsTypeSymbolKind(Hit.Kind))
					{
						Type = FindTypeSymbolInTree(SymbolsResult.Symbols, Hit.Name);
					}
					if (Type && (Type->TypeScope.bLocal || !Type->TypeScope.OwnerTypeName.empty()))
					{
						Hit.Kind = Type->Kind;
						Hit.Name = Type->Name;
						Hit.Detail = Type->Detail;
						Hit.DefLine = Type->StartLine;
						Hit.DefColumn = Type->StartColumn;
						Hit.ContainerName = !Type->TypeScope.OwnerTypeName.empty()
						    ? Type->TypeScope.OwnerTypeName
						    : Type->TypeScope.OwnerFunctionName;
					}
				}
				MapAstDefinitionSitesToPhysical(Program, LineMap, FileLabel, Out.Definitions);
				if (Out.bHasToken)
				{
					CollapseSameNamedTypeConstructors(
					    Out.Definitions,
					    Out.TokenLine,
					    Out.TokenColumn);
				}
				return Out;
			};

			FImportPathCompletionContext ImportContext;
			if (FindImportPathCompletionContext(Source, Position, ImportContext))
			{
				Result.bHasToken = true;
				Result.TokenText = ImportContext.PathText;
				Result.TokenLine = ImportContext.ReplaceLine;
				Result.TokenColumn = ImportContext.ReplaceStartColumn;
				Result.TokenEndColumn = ImportContext.ReplaceEndColumn;
				std::string Resolved;
				std::string Decoded;
				if (!DecodeImportPathLiteral(
				        (ImportContext.bRawString ? "r\"" : "\"") + ImportContext.PathText + "\"",
				        ImportContext.bCompilerExtension, Decoded))
				{
					return Result;
				}

				if (ResolveImportPathOnDisk(Decoded, FileLabel, ImportPaths, Resolved))
				{
					FSymbolQueryHit Hit;
					Hit.Kind = EDocumentSymbolKind::Import;
					Hit.Name = ImportContext.PathText;
					Hit.Detail = ImportContext.bCompilerExtension ? "compilation extension" : ImportContext.bReExport ? "re-exported module"
					                                                                                                  : "module";
					if (ImportContext.bReExport)
					{
						Hit.Documentation =
						    "Forwards declarations exported by the target module. "
						    "Private declarations remain hidden.";
					}
					Hit.FileLabel = std::move(Resolved);
					Hit.DefLine = 1;
					Hit.DefColumn = 1;
					Hit.bExported = false;
					Result.Definitions.push_back(std::move(Hit));
				}
				return Result;
			}

			if (!FindQueryTokenAt(
			        Source,
			        FileLabel,
			        Position,
			        Result.TokenText,
			        Result.TokenLine,
			        Result.TokenColumn,
			        Result.TokenEndColumn))
			{
				return Result;
			}
			Result.bHasToken = true;

			{
				FQueryLexer DeclarationLexer(Source, FileLabel);
				const auto& Tokens = DeclarationLexer.Tokenize();
				for (std::size_t Index = std::max<std::size_t>(1,
				         QueryTokenLowerBound(Tokens, {Result.TokenLine, Result.TokenColumn}));
				    Index < Tokens.size(); ++Index)
				{
					if (Tokens[Index].Location.Line != Result.TokenLine || Tokens[Index].Location.Column != Result.TokenColumn)
					{
						break;
					}
					const auto Kind = Tokens[Index - 1].Lexeme;
					if (Kind == "class" || Kind == "struct" || Kind == "interface" || Kind == "enum" ||
					    Kind == "using" || Kind == "typedef")
					{
						auto Begin = Index - 1;
						if (Begin > 0 && Tokens[Begin - 1].Lexeme == "enum")
						{
							--Begin;
						}
						const auto DeclarationPosition = PhysicalToAstPosition(LineMap,
						    {Tokens[Begin].Location.Line, Tokens[Begin].Location.Column});
						CollectHits(SymbolsResult.Symbols, Result.TokenText, {}, Result.Definitions,
						    &DeclarationPosition, FileLabel, true);
						Result.Definitions.erase(std::remove_if(Result.Definitions.begin(), Result.Definitions.end(),
						                             [](const auto& Hit)
						{
							return !IsTypeSymbolKind(Hit.Kind);
						}),
						    Result.Definitions.end());
						if (!Result.Definitions.empty())
						{
							Result.bLocalSymbol = std::any_of(Result.Definitions.begin(), Result.Definitions.end(),
							    [](const auto& Hit)
							{
								return Hit.bTypeDefinition;
							});
							return Finalize(Result);
						}
					}
					break;
				}
			}

			if (CollectUsingNamespaceTargetHits(
			        Program,
			        SymbolsResult.Symbols,
			        Source,
			        FileLabel,
			        Position,
			        LineMap,
			        Result.Definitions))
			{
				return Finalize(Result);
			}

			// Resolve the physical spelling as a macro before consulting symbols in
			// the expanded AST. Otherwise a host macro such as UE_LOG can inherit the
			// kind of the function in its replacement list.
			if (bHasLineMap)
			{
				VeroScript::FPreprocessMacroDef Macro;
				if (FindMacroDefOrInitialDefine(LineMap, Result.TokenText, Position.Line, Defines,
				        Macro, Position.Column))
				{
					FSymbolQueryHit Hit;
					Hit.Kind = MacroExpandsToKnownType(Macro, SymbolsResult.Symbols, Bindings)
					    ? EDocumentSymbolKind::TypeAlias
					    : EDocumentSymbolKind::Macro;
					Hit.Name = Macro.Name;
					Hit.Detail = FormatMacroCompletionDetail(Macro);
					Hit.bSoftBuiltin = Macro.bPredefined;
					Hit.bExported = Macro.bPublic || Macro.bImported;
					if (Macro.bPredefined)
					{
						Hit.Documentation = "Predefined macro";
						if (!Macro.SourceName.empty())
						{
							Hit.Documentation += " from ";
							Hit.Documentation += Macro.SourceName;
						}
						Hit.DefLine = 0;
						Hit.DefColumn = 0;
					}
					else
					{
						Hit.FileLabel = Macro.bImported ? Macro.SourceName : std::string(FileLabel);
						Hit.DefLine = Macro.Line;
						Hit.DefColumn = Macro.Column;
					}
					Result.Definitions.push_back(std::move(Hit));
					return Finalize(Result);
				}
			}

			// Generic parameters are lexical type declarations. They are not
			// emitted as ordinary document symbols, so resolve their physical tokens
			// before falling back to the expanded AST and global symbol tables.
			if (bResolveGenericTypeParameters)
			{
				FQueryLexer Lexer(Source, FileLabel);
				const std::vector<FToken>& Tokens = Lexer.Tokenize();
				std::unordered_map<std::size_t, FGenericTypeParameterToken> GenericTokens;
				CollectGenericTypeParameterTokens(Tokens, GenericTokens);
				for (std::size_t TokenIndex = 0; TokenIndex < Tokens.size(); ++TokenIndex)
				{
					const FToken& Token = Tokens[TokenIndex];
					if (Token.Location.Line != Result.TokenLine || Token.Location.Column != Result.TokenColumn)
					{
						continue;
					}
					const auto Generic = GenericTokens.find(TokenIndex);
					if (Generic == GenericTokens.end())
					{
						break;
					}
					FSymbolQueryHit Hit;
					Hit.Kind = Generic->second.bValue ? EDocumentSymbolKind::Constant : EDocumentSymbolKind::TypeAlias;
					Hit.Name = Generic->second.Name;
					Hit.Detail = Generic->second.bValue ? "template value parameter (" + Generic->second.ValueType + ")" : "generic type parameter";
					Hit.FileLabel = std::string(FileLabel);
					Hit.DefLine = Generic->second.DefinitionLine;
					Hit.DefColumn = Generic->second.DefinitionColumn;
					Hit.bPhysicalLocation = true;
					Hit.bExported = false;
					Result.Definitions.push_back(std::move(Hit));
					return Finalize(Result);
				}
			}

			// multicast_delegate += / -= sugar → resolve Add / Remove (mirrors compiler).
			const VeroScript::FHostAnnotationDesc* EnclosingAnnotation = nullptr;
			std::string AnnotationArgumentPath;
			if (const VeroScript::FHostAnnotationArgumentDesc* const Argument =
			        FindHostAnnotationArgumentAt(
			            Source,
			            Result.TokenLine,
			            Result.TokenColumn,
			            Result.TokenText,
			            Bindings,
			            EnclosingAnnotation,
			            &AnnotationArgumentPath))
			{
				FSymbolQueryHit Hit;
				Hit.Kind = EDocumentSymbolKind::AnnotationArgument;
				Hit.Name = Argument->Name;
				Hit.Detail = Argument->Detail.empty()
				    ? std::string("annotation argument")
				    : Argument->Detail;
				Hit.Documentation = Argument->Documentation;
				if (EnclosingAnnotation)
				{
					Hit.ContainerName = EnclosingAnnotation->Name;
					if (!AnnotationArgumentPath.empty())
					{
						Hit.ContainerName.push_back('.');
						Hit.ContainerName += AnnotationArgumentPath;
					}
				}
				Hit.bSoftBuiltin = true;
				Hit.bExported = true;
				Hit.DefLine = 0;
				Hit.DefColumn = 0;
				Result.Definitions.push_back(std::move(Hit));
				return Finalize(Result);
			}

			// Keep TokenText as operator+=/-= so Find References still finds use-sites.
			if (Result.TokenText == "operator+=" || Result.TokenText == "operator-=")
			{
				std::string ReceiverName;
				std::string OwnerName;
				if (FindCompoundAssignLeftReceiver(
				        Source,
				        FileLabel,
				        Position,
				        ReceiverName,
				        OwnerName))
				{
					std::string TypeName;
					if (!OwnerName.empty())
					{
						const std::string OwnerType = ResolveReceiverTypeName(
						    Program,
						    SymbolsResult,
						    AstPosition,
						    OwnerName);
						std::unordered_map<std::string, FInheritedMemberCompletion> Members;
						AppendCompletionMembersAlongHierarchy(
						    Program,
						    SymbolsResult.Symbols,
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
						TypeName = ResolveReceiverTypeName(
						    Program,
						    SymbolsResult,
						    AstPosition,
						    ReceiverName);
					}
					if (IsSoftMulticastDelegateTypeName(TypeName))
					{
						const char* MethodName =
						    Result.TokenText == "operator+=" ? "add" : "remove";
						if (CollectMemberHitsAlongHierarchy(
						        Program,
						        SymbolsResult.Symbols,
						        TypeName,
						        MethodName,
						        Result.Definitions) ||
						    CollectSoftMemberHits(
						        TypeName,
						        MethodName,
						        Result.Definitions, Program))
						{
							return Finalize(Result);
						}
					}
				}
			}

			// Field declarations and constructor initializer targets name the field
			// itself, even when a constructor parameter has the same spelling.
			const FTextPosition FieldTokenStart = PhysicalToAstPosition(
			    LineMap, FTextPosition{Result.TokenLine, Result.TokenColumn});
			std::string FieldInitializerOwner;
			const FExpr* FieldInitializerExpression = nullptr;
			std::unique_ptr<FQueryLexer> FieldLexer;
			const auto FieldTokens = [&]() -> const std::vector<FToken>&
			{
				if (!FieldLexer)
				{
					FieldLexer = std::make_unique<FQueryLexer>(
					    bHasLineMap ? std::string_view(LineMap.PreprocessedSource) : Source, FileLabel);
				}
				return FieldLexer->Tokenize();
			};
			const auto FindFieldDeclarationOrInitializer = [&](const auto& Types)
			{
				for (const auto& Type : Types)
				{
					if (Type.ModulePath != Program.EntryModulePath)
					{
						continue;
					}
					for (const FFieldDecl& Field : Type.Fields)
					{
						if (CompareLocation(Field.Location, FieldTokenStart) > 0)
						{
							continue;
						}
						FSourceLocation InitializerEnd = Field.Location;
						if (Field.DefaultValue)
						{
							ExtendContentEndByExpr(*Field.DefaultValue, InitializerEnd);
						}
						const bool bMayBeInitializer = Field.DefaultValue && CompareLocation(InitializerEnd, FieldTokenStart) >= 0;
						if (Field.Name != Result.TokenText && !bMayBeInitializer)
						{
							continue;
						}
						// Field.Location points to the declaration specifier/type, not
						// the name. Verify the identifier inside that parsed declaration
						// rather than guessing a column from the field name's length.
						const auto& Tokens = FieldTokens();
						bool bInitializer = false;
						int BraceDepth = 0;
						for (std::size_t Index = 0; Index + 1 < Tokens.size(); ++Index)
						{
							const FToken& Token = Tokens[Index];
							if (CompareLocation(Token.Location, {Field.Location.Line, Field.Location.Column}) < 0)
							{
								continue;
							}
							const int Relative = CompareLocation(Token.Location, FieldTokenStart);
							if (Relative > 0)
							{
								break;
							}
							if ((Token.Lexeme == ";" || Token.Lexeme == "}") && BraceDepth == 0)
							{
								break;
							}
							if (Token.Lexeme == "=" && BraceDepth == 0)
							{
								bInitializer = true;
							}
							if (Relative == 0)
							{
								if (bInitializer && bMayBeInitializer)
								{
									FieldInitializerOwner = Type.Name;
									FieldInitializerExpression = Field.DefaultValue.get();
									break;
								}
								const auto Next = Tokens[Index + 1].Lexeme;
								if (!bInitializer && Field.Name == Result.TokenText && (Next == ";" || Next == "=" || Next == "["))
								{
									return CollectMemberHitsAlongHierarchy(Program, SymbolsResult.Symbols,
									    Type.Name, Field.Name, Result.Definitions);
								}
								break;
							}
							if (Token.Lexeme == "{")
							{
								++BraceDepth;
							}
							else if (Token.Lexeme == "}")
							{
								--BraceDepth;
							}
						}
					}
					if constexpr (!std::is_same_v<std::decay_t<decltype(Type)>, FInterfaceDecl>)
					{
						for (const FConstructorDecl& Constructor : Type.Constructors)
						{
							for (const FFieldCtorInit& Init : Constructor.FieldInits)
							{
								if (Init.FieldName == Result.TokenText && Init.Location.Line == FieldTokenStart.Line && Init.Location.Column == FieldTokenStart.Column)
								{
									return CollectMemberHitsAlongHierarchy(Program, SymbolsResult.Symbols,
									    Type.Name, Init.FieldName, Result.Definitions);
								}
							}
						}
					}
				}
				return false;
			};
			if (FindFieldDeclarationOrInitializer(Program.Structs) || FindFieldDeclarationOrInitializer(Program.Classes) || FindFieldDeclarationOrInitializer(Program.Interfaces))
			{
				return Finalize(Result);
			}

			// Prefer receiver.member over a same-named local / global hit.
			// Use bActive (not only bHasReplaceRange) so Hover on `recv.member` still
			// walks the specialized hierarchy (sealed multi-parent / open-generic parents).
			FMemberCompletionContext MemberContext;
			if (FindMemberCompletionContext(Source, FileLabel, Position, MemberContext) && MemberContext.bActive)
			{
				const auto CollectQualifiedNamespaceHits = [&]()
				{
					const std::string Qualified = QualifiedIdentifierAt(
					    Source, FileLabel, Result.TokenLine, Result.TokenColumn);
					if (MemberContext.bPostfixReceiver || Qualified.find('.') == std::string::npos) return;
					if (CollectNamespaceQueryHits(Program, SymbolsResult.Symbols, Source,
							FileLabel, AstPosition, ActiveNamespace, Result.TokenText, Result.TokenLine,
							Result.TokenColumn, Result.Definitions))
					{
						return;
					}
					CollectHits(SymbolsResult.Symbols, Result.TokenText, {},
					    Result.Definitions, &AstPosition, FileLabel);
					const auto Targets = BindQueryQualifiedPath(Program, SymbolsResult.Symbols, Qualified, ActiveNamespace, AstPosition);
					Result.Definitions.erase(std::remove_if(Result.Definitions.begin(),
					                             Result.Definitions.end(), [&](const FSymbolQueryHit& Hit)
					{
						return !Hit.ContainerName.empty() || (Targets.empty()
						    ? NormalizeNamespaceSpelling(StripGenericArgs(Hit.Name)) != Qualified
						    : std::find(Targets.begin(), Targets.end(), NormalizeNamespaceSpelling(StripGenericArgs(Hit.Name))) == Targets.end());
					}), Result.Definitions.end());
				};
				if (MemberContext.bNamespacePrefix && !MemberContext.bTypeApplicationPath
				    && !MemberContext.bPostfixReceiver && MemberContext.ReceiverExpression.rfind("::", 0) == 0)
				{
					// Root qualification bypasses local types/values with the same leaf.
					// Do not let bare receiver inference select one of those shadows.
					const auto Targets = BindQueryQualifiedPath(Program, SymbolsResult.Symbols,
					    "::" + MemberContext.ReceiverName, ActiveNamespace, AstPosition);
					if (Targets.size() == 1 && std::none_of(SymbolsResult.Symbols.begin(), SymbolsResult.Symbols.end(), [&](const auto& Symbol)
					{
						return IsTypeSymbolKind(Symbol.Kind) && NormalizeNamespaceSpelling(StripGenericArgs(Symbol.Name)) == Targets.front();
					}) && std::any_of(SymbolsResult.Symbols.begin(), SymbolsResult.Symbols.end(), [&](const auto& Symbol)
					{
						return Symbol.Kind == EDocumentSymbolKind::Namespace && NormalizeNamespaceSpelling(Symbol.Name) == Targets.front();
					}))
					{
						CollectQualifiedNamespaceHits();
						return Finalize(Result);
					}
				}
				std::vector<FSymbolQueryHit> VisibleLocals;
				if (FieldInitializerExpression)
				{
					FScopeStack Stack(1);
					WalkExprForLocals(*FieldInitializerExpression, MaxLocation(), AstPosition, Stack);
					if (const FSymbolQueryHit* Local = LookupLocal(Stack, MemberContext.ReceiverName))
					{
						VisibleLocals.push_back(*Local);
					}
					else
					{
						CollectMemberHitsAlongHierarchy(Program, SymbolsResult.Symbols,
						    FieldInitializerOwner, MemberContext.ReceiverName, VisibleLocals);
					}
				}
				else
				{
					CollectVisibleLocals(Program, AstPosition, VisibleLocals);
				}
				const auto ResolveQueryReceiverType = [&]()
				{
					if (FieldInitializerExpression && MemberContext.ReceiverName == "this")
					{
						return FieldInitializerOwner;
					}
					return ResolveReceiverTypeName(Program, SymbolsResult, AstPosition,
					    MemberContext.ReceiverName, &VisibleLocals);
				};
				bool bTypeReceiver = false;
				if (!MemberContext.bIndexElement && !MemberContext.bMethodReturn)
				{
					const bool bShadowedByValue = std::any_of(
					    VisibleLocals.begin(),
					    VisibleLocals.end(),
					    [&MemberContext](const FSymbolQueryHit& Hit)
					{
						return Hit.Name == MemberContext.ReceiverName;
					});
					bTypeReceiver = !bShadowedByValue && IsKnownTypeName(MemberContext.ReceiverName, SymbolsResult.Symbols, Bindings);
				}
				const bool bKnownValueReceiver = IsKnownValueReceiver(
				    Program, AstPosition, MemberContext.ReceiverName, VisibleLocals, SymbolsResult, ActiveNamespace);
				if (bKnownValueReceiver)
				{
					bTypeReceiver = false;
				}

				std::string TypeName = ResolveReceiverExpressionType(Program, SymbolsResult, AstPosition,
				    MemberContext.ReceiverExpression, Bindings, ActiveNamespace, &VisibleLocals);
				const bool bResolvedExpression = !TypeName.empty();
				if (bResolvedExpression)
				{
					bTypeReceiver = false;
				}
				if (MemberContext.bTypeApplicationPath && !bResolvedExpression)
				{
					TypeName = ResolveReceiverTypeApplication(Program, AstPosition, MemberContext.ReceiverExpression,
					    Bindings, ActiveNamespace, VisibleLocals);
					if (TypeName.empty()) return Finalize(Result);
					bTypeReceiver = true;
				}
				if (TypeName == "void") return Finalize(Result);
				if (!bResolvedExpression && (MemberContext.bPostfixReceiver || bKnownValueReceiver))
				{
					return Finalize(Result);
				}
				if (TypeName.empty())
				{
					TypeName = MemberContext.ResolvedTypeName;
				}
				if (!FieldInitializerOwner.empty() && MemberContext.ReceiverName == "this")
				{
					TypeName = FieldInitializerOwner;
				}
				if (TypeName.empty())
				{
					TypeName = ResolveQueryReceiverType();
				}
				if (!bResolvedExpression && MemberContext.bIndexElement)
				{
					std::string ElementType;
					if (ResolveContainerIndexElementType(TypeName, ElementType))
					{
						TypeName = std::move(ElementType);
					}
				}
				if (!bResolvedExpression && MemberContext.bMethodReturn && !MemberContext.MethodName.empty())
				{
					std::string ReturnType;
					bool bResolvedReturn = false;
					const std::string OwnerType = ResolveQueryReceiverType();
					// Keep hover/navigation on the same failed-static-call boundary
					// as completion instead of reviving an instance return type.
					if (!bKnownValueReceiver && IsKnownTypeName(StripGenericArgs(OwnerType), SymbolsResult.Symbols, Bindings))
					{
						return Finalize(Result);
					}
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
						AppendCompletionMembersAlongHierarchy(
						    Program,
						    SymbolsResult.Symbols,
						    OwnerType,
						    MemberContext.MethodName,
						    OwnerMembers);
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
				if (CollectMemberHitsAlongHierarchy(
				        Program,
				        SymbolsResult.Symbols,
				        TypeName,
				        Result.TokenText,
				        Result.Definitions))
				{
					if (const VeroScript::FHostTypeDesc* HostType =
					        FindHostTypeAlongHierarchy(Bindings, Program, TypeName))
					{
						bool bHasAllowedHostMember = false;
						for (const VeroScript::FHostMemberDesc& Member : HostType->Members)
						{
							if (Member.Name == Result.TokenText && HostMemberCanBeNamedFromReceiver(Member, bTypeReceiver))
							{
								bHasAllowedHostMember = true;
								break;
							}
						}
						if (!bHasAllowedHostMember)
						{
							Result.Definitions.erase(
							    std::remove_if(
							        Result.Definitions.begin(),
							        Result.Definitions.end(),
							        [](const FSymbolQueryHit& Hit)
							{
								return Hit.bSoftBuiltin;
							}),
							    Result.Definitions.end());
						}
					}
					if (!Result.Definitions.empty())
					{
						return Finalize(Result);
					}
				}
				if (CollectSoftMemberHits(TypeName, Result.TokenText, Result.Definitions, Program))
				{
					return Finalize(Result);
				}
				if (CollectHostMemberHits(
				        Bindings,
				        TypeName,
				        Result.TokenText,
				        Result.Definitions,
				        &Program,
				        bTypeReceiver))
				{
					return Finalize(Result);
				}
				for (const FFunctionDecl& Function : Program.Functions)
				{
					if ((!bResolvedExpression && !bKnownValueReceiver) || Function.Parameters.empty() || Function.bDeleted || ExtensionMethodLeafName(Function.Name) != Result.TokenText || !ExtensionFunctionIsVisible(Program, Function, FileLabel, ActiveNamespace, AstPosition, Bindings) || !ExtensionReceiverTypeMatches(Function, TypeName))
					{
						continue;
					}
					FSymbolQueryHit Hit;
					Hit.Kind = EDocumentSymbolKind::Function;
					Hit.Name = Result.TokenText;
					Hit.Detail = FormatExtensionMethodDetail(Function);
					Hit.ContainerName = TypeName;
					Hit.FileLabel = Function.ModulePath;
					Hit.DefLine = Function.Location.Line;
					Hit.DefColumn = Function.Location.Column;
					Hit.SourceImplementation = SourceImplementationLocation(Program, Function.Body.get());
					Hit.bExported = Function.bExported;
					Result.Definitions.push_back(std::move(Hit));
				}
				if (!Result.Definitions.empty())
				{
					return Finalize(Result);
				}
				// A known value/type receiver makes this lookup authoritative. A
				// missing member must not fall through to unrelated global symbols
				// or members of other types. Namespace paths are resolved below.
				if (bKnownValueReceiver || FindTypeSymbolInTree(SymbolsResult.Symbols, TypeName) || FindHostTypeAlongHierarchy(Bindings, Program, TypeName))
				{
					return Finalize(Result);
				}
				// An unresolved receiver still constrains the query. Only a matching
				// namespace segment or fully qualified top-level symbol can resolve
				// it; bare locals and globals must never fill a missing member slot.
				CollectQualifiedNamespaceHits();
				return Finalize(Result);
			}

			const bool bGotoTarget = IsGotoNameContext(Source, FileLabel, Position);
			if (bGotoTarget)
			{
				CollectLocalHits(Program, AstPosition, Result.TokenText, Result.Definitions);
				PreferLabelHits(Result.Definitions);
				// A label already resolved in the local scope is authoritative. The
				// outline also contains it; collecting both duplicates hover/navigation.
				if (!Result.Definitions.empty())
				{
					return Finalize(Result);
				}
				CollectHits(
				    SymbolsResult.Symbols,
				    Result.TokenText,
				    {},
				    Result.Definitions,
				    &AstPosition,
				    FileLabel);
				PreferLabelHits(Result.Definitions);
				PreferExactOrQualifiedHits(Result.TokenText, Result.Definitions);
				return Finalize(Result);
			}

			// A field can share a type's spelling. Keep parsed declaration/type
			// positions out of value lookup (`Value v`, `new Value`, type arguments).
			const std::string QualifiedToken = QualifiedIdentifierAt(
			    Source, FileLabel, Result.TokenLine, Result.TokenColumn);
			const bool bRootQualifiedToken = !QualifiedToken.empty() && QualifiedToken.front() == '.';
			if (!VeroScript::IsTypeKeyword(Result.TokenText) && !VeroScript::IsContainerTypeKeyword(Result.TokenText) && FindTypeSymbol(SymbolsResult.Symbols, Result.TokenText))
			{
				FQueryLexer Lexer(Source, FileLabel);
				const auto& Tokens = Lexer.Tokenize();
				const EKeywordCompletionMode Mode = ClassifyKeywordCompletionMode(
				    Source, FileLabel, {Result.TokenLine, Result.TokenColumn});
				bool bTypeUse = Mode == EKeywordCompletionMode::TypeName;
				for (std::size_t Index = 0; !bTypeUse && Index + 1 < Tokens.size(); ++Index)
				{
					if (Tokens[Index].Location.Line != Result.TokenLine || Tokens[Index].Location.Column != Result.TokenColumn)
					{
						continue;
					}
					std::size_t Next = Index + 1;
					int AngleDepth = 0;
					int BracketDepth = 0;
					for (; Next < Tokens.size(); ++Next)
					{
						const auto Text = Tokens[Next].Lexeme;
						if (Text == "<")
						{
							++AngleDepth;
						}
						else if (AngleDepth > 0 && AngleCloseLevels(Text) > 0)
						{
							AngleDepth -= AngleCloseLevels(Text);
						}
						else if (Text == "[")
						{
							++BracketDepth;
						}
						else if (Text == "]" && BracketDepth > 0)
						{
							--BracketDepth;
						}
						else if (AngleDepth == 0 && BracketDepth == 0 && Text != "&" && Text != "*")
						{
							break;
						}
					}
					bool bDeclarationContext = Mode != EKeywordCompletionMode::Expression || Next == Index + 1;
					if (!bDeclarationContext)
					{
						std::vector<FCallableScope> Callables;
						CollectCallables(Program, Callables);
						AssignCallableEndLines(Callables);
						const FCallableScope* Scope = FindEnclosingCallable(Callables, AstPosition);
						bDeclarationContext = Scope && Scope->Body && CompareLocation(Scope->Body->Location, AstPosition) > 0;
					}
					bTypeUse = bDeclarationContext && LooksLikeIdentifierDeclarator(Tokens, Index) && AngleDepth == 0 && BracketDepth == 0 && Next < Tokens.size() && Tokens[Next].Kind == ETokenKind::Identifier;
					break;
				}
				if (bTypeUse)
				{
					std::vector<FSymbolQueryHit> Types;
					CollectHits(SymbolsResult.Symbols, Result.TokenText, {}, Types, &AstPosition, FileLabel);
					for (const FSymbolQueryHit& Hit : Types)
					{
						if (IsTypeSymbolKind(Hit.Kind) && (!bRootQualifiedToken ||
						        NormalizeNamespaceSpelling(StripGenericArgs(Hit.Name)) == QualifiedToken.substr(1)))
						{
							Result.Definitions.push_back(Hit);
						}
					}
					if (!Result.Definitions.empty())
					{
						PreferVisibleNamespaceHits(Program, SymbolsResult.Symbols, ActiveNamespace,
							    AstPosition, bRootQualifiedToken ? QualifiedToken : Result.TokenText, Result.Definitions);
						if (!Result.Definitions.empty())
						{
							return Finalize(Result);
						}
					}
				}
			}

			if (!bRootQualifiedToken && FieldInitializerExpression)
			{
				FScopeStack Stack(1);
				WalkExprForLocals(*FieldInitializerExpression, MaxLocation(), AstPosition, Stack);
				if (const FSymbolQueryHit* Local = LookupLocal(Stack, Result.TokenText))
				{
					Result.Definitions.push_back(*Local);
				}
			}
			else if (!bRootQualifiedToken)
			{
				CollectLocalHits(Program, AstPosition, Result.TokenText, Result.Definitions);
			}
			if (!Result.Definitions.empty())
			{
				Result.bLocalSymbol = true;
				return Finalize(Result);
			}
			// Bare field reads use the enclosing type's nearest declaration,
			// after locals/parameters have had the opportunity to shadow it.
			const std::string FieldOwner = !FieldInitializerOwner.empty() ? FieldInitializerOwner
			                                                              : VeroScriptSymbolQuery::ResolveEnclosingTypeName(Program, AstPosition);
			if (!FieldOwner.empty() && QualifiedIdentifierAt(Source, FileLabel, Result.TokenLine, Result.TokenColumn).find('.') == std::string::npos && !IsBareCallTokenContext(Source, Result.TokenLine, Result.TokenColumn, Result.TokenEndColumn))
			{
				std::vector<FSymbolQueryHit> Fields;
				CollectMemberHitsAlongHierarchy(Program, SymbolsResult.Symbols,
				    FieldOwner, Result.TokenText, Fields);
				for (const FSymbolQueryHit& Hit : Fields)
				{
					if (Hit.Kind == EDocumentSymbolKind::Field)
					{
						Result.Definitions.push_back(Hit);
					}
				}
				if (!Result.Definitions.empty())
				{
					return Finalize(Result);
				}
			}

			// A namespace path is resolved segment-by-segment.  Do this before the
			// ordinary leaf-name lookup so hovering `chrono` in `std::chrono::now`
			// resolves the namespace rather than attempting to resolve `chrono` as a
			// standalone function/type identifier.
			if (CollectNamespaceQueryHits(
			        Program,
			        SymbolsResult.Symbols,
			        Source,
			        FileLabel,
			        AstPosition,
			        ActiveNamespace,
			        Result.TokenText,
			        Result.TokenLine,
			        Result.TokenColumn,
			        Result.Definitions))
			{
				return Finalize(Result);
			}

			// Primitive type words retain their keyword kind in hover, completion,
			// and semantic tokens; container words remain named types. Both still
			// describe a builtin type. DefLine remains zero so go-to-
			// definition correctly reports no source location for a language builtin.
			// A qualified spelling such as std::array must first resolve its namespace
			// alias so its richer standard-library hover remains available.
			if (VeroScript::IsTypeKeyword(Result.TokenText) || VeroScript::IsContainerTypeKeyword(Result.TokenText))
			{
				if (QualifiedToken.find('.') == std::string::npos)
				{
					FSymbolQueryHit Hit;
					Hit.Kind = VeroScript::IsTypeKeyword(Result.TokenText)
					    ? EDocumentSymbolKind::Keyword
					    : EDocumentSymbolKind::TypeAlias;
					Hit.Name = Result.TokenText;
					Hit.Detail = VeroScript::Locale::TranslateId("tooling.builtin_type.detail");
					Hit.Documentation = VeroScript::Locale::TranslateId(
					    "tooling.builtin_type.documentation");
					Hit.DefLine = 0;
					Hit.DefColumn = 0;
					Result.Definitions.push_back(std::move(Hit));
					return Finalize(Result);
				}
			}

			bool bOperatorUse = false;
			std::string LeftOperatorType, RightOperatorType;
			if (IsOperatorMethodName(Result.TokenText))
			{
				FQueryLexer OperatorLexer(Source, FileLabel);
				const auto& Tokens = OperatorLexer.Tokenize();
				std::string Name;
				std::uint32_t Begin = 0, End = 0;
				bOperatorUse = FindOperatorUseAt(Tokens, Result.TokenLine, Result.TokenColumn, Name, Begin, End) && Name == Result.TokenText;
				if (bOperatorUse)
				{
					for (std::size_t I = 0; I < Tokens.size(); ++I)
					{
						if (Tokens[I].Location.Line == Result.TokenLine && Tokens[I].Location.Column == Begin)
						{
							LeftOperatorType = ResolveReceiverExpressionType(Program, SymbolsResult, AstPosition,
							    PostfixReceiverExpression(Tokens, I), Bindings, ActiveNamespace);
							// Read one bounded postfix operand. Nested calls/indexes stay together;
							// an unknown/unfinished operand must not select another type globally.
							std::size_t Start = I + 1, Stop = Start;
							int Depth = 0;
							for (; Stop < Tokens.size() && Stop - Start < 4096; ++Stop)
							{
								const auto& T = Tokens[Stop];
								if (Depth == 0 && T.Kind != ETokenKind::Identifier && T.Lexeme != "this" &&
								    T.Lexeme != "." && T.Lexeme != "::" && T.Lexeme != "(" && T.Lexeme != "[")
								{
									break;
								}

								if (T.Lexeme == "(" || T.Lexeme == "[")
								{
									++Depth;
								}

								else if (T.Lexeme == ")" || T.Lexeme == "]")
								{
									--Depth;
								}
							}
							if (Depth == 0 && Stop > Start)
							{
								RightOperatorType =
								    ResolveReceiverExpressionType(Program, SymbolsResult, AstPosition,
								        PostfixReceiverExpression(Tokens, Stop), Bindings, ActiveNamespace);
							}

							break;
						}
					}
				}
			}
			const auto ScopeOperatorHits = [&](const std::string& Name, const bool bMirror)
			{
				if (!bOperatorUse) return;
				std::vector<FSymbolQueryHit> Members;
				const auto Collect = [&](const std::string& Type)
				{
					if (Type.empty() || !Members.empty()) return;
					CollectMemberHitsAlongHierarchy(Program, SymbolsResult.Symbols, Type, Name, Members);
					if (Members.empty())
					{
						CollectHostMemberHits(Bindings, Type, Name, Members, &Program);
					}
				};
				// Relational mirrors swap operands; != is synthesized from ==.
				if (!bMirror || Result.TokenText == "operator!=")
				{
					Collect(LeftOperatorType);
				}

				if (Result.TokenText != "operator[]" && Result.TokenText != "operator()" &&
				    Result.TokenText != "operator=")
				{
					Collect(RightOperatorType);
				}

				if (!Members.empty())
				{
					Result.Definitions = std::move(Members);
				}

				else
				{
					Result.Definitions.erase(
					    std::remove_if(Result.Definitions.begin(), Result.Definitions.end(),
					        [](const auto& H)
					{
						return H.Kind != EDocumentSymbolKind::Function || !H.ContainerName.empty();
					}),
					    Result.Definitions.end());
				}
			};

			CollectHits(
			    SymbolsResult.Symbols,
			    Result.TokenText,
			    {},
			    Result.Definitions,
			    &AstPosition,
			    FileLabel);
			if (!Result.Definitions.empty())
			{
				FQueryLexer DeclarationLexer(Source, FileLabel);
				const auto& Tokens = DeclarationLexer.Tokenize();
				std::vector<FSymbolQueryHit> DeclarationHits;
				for (const FSymbolQueryHit& Hit : Result.Definitions)
				{
					if (IsCallableDeclarationToken(Hit, Result.TokenText,
					        {Result.TokenLine, Result.TokenColumn}, FileLabel, Tokens, LineMap))
					{
						DeclarationHits.push_back(Hit);
					}
				}
				if (!DeclarationHits.empty())
				{
					// The exact declaration chooses its owner, not a single overload.
					// Keep the owner's whole name group so references remain complete
					// and rename cannot silently change only one overload declaration.
					std::vector<FSymbolQueryHit> OwnerHits;
					for (const FSymbolQueryHit& Hit : Result.Definitions)
					{
						for (const FSymbolQueryHit& Declaration : DeclarationHits)
						{
							if (Hit.Kind == Declaration.Kind && Hit.DefLine != 0 && Hit.bSoftBuiltin == Declaration.bSoftBuiltin && Hit.ContainerName == Declaration.ContainerName && StripGenericArgs(Hit.Name) == StripGenericArgs(Declaration.Name))
							{
								OwnerHits.push_back(Hit);
								break;
							}
						}
					}
					Result.Definitions = std::move(OwnerHits);
					return Finalize(Result);
				}
			}
			ScopeOperatorHits(Result.TokenText, false);
			if (QualifiedToken.find('.') != std::string::npos)
			{
				const auto Targets = BindQueryQualifiedPath(Program, SymbolsResult.Symbols, QualifiedToken,
				    ActiveNamespace, AstPosition);
				std::vector<FSymbolQueryHit> QualifiedHits;
				for (const FSymbolQueryHit& Hit : Result.Definitions)
				{
					if (Hit.ContainerName.empty() &&
					    (Targets.empty() ? NormalizeNamespaceSpelling(StripGenericArgs(Hit.Name)) == QualifiedToken
					        : std::find(Targets.begin(), Targets.end(), NormalizeNamespaceSpelling(StripGenericArgs(Hit.Name))) != Targets.end()))
					{
						QualifiedHits.push_back(Hit);
					}
				}
				if (!QualifiedHits.empty() || !Targets.empty())
				{
					Result.Definitions = std::move(QualifiedHits);
					return Finalize(Result);
				}
			}
			PreferVisibleNamespaceHits(
			    Program,
			    SymbolsResult.Symbols,
			    ActiveNamespace,
			    AstPosition,
			    Result.TokenText,
			    Result.Definitions);
			if (!Result.TokenText.empty() && Result.TokenText.front() != '~' && IsBareCallTokenContext(Source, Result.TokenLine, Result.TokenColumn, Result.TokenEndColumn))
			{
				// Match compiler priority: any ordinary call target wins. Only
				// when none exists may a method of the enclosing instance act as
				// the implicit `this` target.
				std::vector<FSymbolQueryHit> OrdinaryHits;
				for (const FSymbolQueryHit& Hit : Result.Definitions)
				{
					if (Hit.ContainerName.empty())
					{
						OrdinaryHits.push_back(Hit);
					}
				}
				if (!OrdinaryHits.empty())
				{
					Result.Definitions = std::move(OrdinaryHits);
				}
				else
				{
					const std::string OwnerType =
					    ResolveImplicitThisOwnerName(Program, AstPosition);
					std::vector<FSymbolQueryHit> ImplicitMethodHits;
					if (!OwnerType.empty())
					{
						CollectMemberHitsAlongHierarchy(
						    Program,
						    SymbolsResult.Symbols,
						    OwnerType,
						    Result.TokenText,
						    ImplicitMethodHits);
						ImplicitMethodHits.erase(
						    std::remove_if(
						        ImplicitMethodHits.begin(),
						        ImplicitMethodHits.end(),
						        [](const FSymbolQueryHit& Hit)
						{
							return Hit.Kind != EDocumentSymbolKind::Method;
						}),
						    ImplicitMethodHits.end());
						if (ImplicitMethodHits.empty())
						{
							CollectHostMemberHits(
							    Bindings,
							    OwnerType,
							    Result.TokenText,
							    ImplicitMethodHits,
							    &Program);
							ImplicitMethodHits.erase(
							    std::remove_if(
							        ImplicitMethodHits.begin(),
							        ImplicitMethodHits.end(),
							        [](const FSymbolQueryHit& Hit)
							{
								return Hit.Kind != EDocumentSymbolKind::Method;
							}),
							    ImplicitMethodHits.end());
						}
					}
					Result.Definitions = std::move(ImplicitMethodHits);
				}
			}
			PreferExactOrQualifiedHits(Result.TokenText, Result.Definitions);
			PreferHostFormatVarargsHits(Result.TokenText, Result.Definitions);
			if (Result.Definitions.empty())
			{
				std::string MirrorName;
				if (TryOperatorMirrorDefinitionName(Result.TokenText, MirrorName))
				{
					CollectHits(
					    SymbolsResult.Symbols,
					    MirrorName,
					    {},
					    Result.Definitions,
					    &AstPosition,
					    FileLabel);
					ScopeOperatorHits(MirrorName, true);
					PreferExactOrQualifiedHits(MirrorName, Result.Definitions);
				}
			}
			if (!Result.Definitions.empty())
			{
				return Finalize(Result);
			}

			if (Bindings)
			{
				for (const auto& Annotation : Bindings->GetAnnotations())
				{
					if (!VeroScript::AnnotationNamesEqual(
					        Annotation.Name, Result.TokenText, Annotation.CaseSensitive))
					{
						continue;
					}

					FSymbolQueryHit Hit;
					Hit.Kind = EDocumentSymbolKind::Annotation;
					Hit.Name = Annotation.Name;
					Hit.Detail = Annotation.Detail.empty() ? "annotation" : Annotation.Detail;
					Hit.Documentation = Annotation.Documentation;
					Hit.bSoftBuiltin = true;
					Hit.bExported = true;
					Result.Definitions.push_back(std::move(Hit));
					return Finalize(Result);
				}
			}

			if (bHasLineMap)
			{
				VeroScript::FPreprocessMacroDef Macro;
				if (FindMacroDefOrInitialDefine(LineMap, Result.TokenText, Position.Line, Defines,
				        Macro, Position.Column))
				{
					FSymbolQueryHit Hit;
					Hit.Kind = MacroExpandsToKnownType(
					               Macro, SymbolsResult.Symbols, Bindings)
					    ? EDocumentSymbolKind::TypeAlias
					    : EDocumentSymbolKind::Macro;
					Hit.Name = Macro.Name;
					Hit.Detail = FormatMacroCompletionDetail(Macro);
					if (Macro.bPredefined)
					{
						Hit.Documentation = "Predefined macro";
						if (!Macro.SourceName.empty())
						{
							Hit.Documentation += " from ";
							Hit.Documentation += Macro.SourceName;
						}
					}
					else
					{
						Hit.FileLabel = Macro.bImported ? Macro.SourceName : std::string(FileLabel);
						Hit.DefLine = Macro.Line;
						Hit.DefColumn = Macro.Column;
					}
					Hit.bExported = Macro.bPublic || Macro.bImported;
					Result.Definitions.push_back(std::move(Hit));
				}
			}
			return Finalize(Result);
		}
