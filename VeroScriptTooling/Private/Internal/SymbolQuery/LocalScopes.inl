// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		struct FCallableScope
		{
			std::string Name;
			std::string ContainerName;
			std::string ReturnType;
			std::string FileLabel;
			bool bHasImplicitThis = false;
			bool bIsConstMethod = false;
			std::uint32_t StartLine = 1;
			std::uint32_t StartColumn = 1;
			std::uint32_t EndLine = std::numeric_limits<std::uint32_t>::max();
			std::uint32_t EndColumn = std::numeric_limits<std::uint32_t>::max();
			const std::vector<FFunctionParam>* Parameters = nullptr;
			std::vector<FFunctionParam> DefinitionParameters;
			std::vector<FTextPosition> DefinitionParameterPositions;
			const FBlockStmt* Body = nullptr;
			std::string DeclarationModulePath;
			bool bExported = false;
		};

		void BindDefinitionParameterPositions(FCallableScope& Scope, const std::vector<FToken>& Tokens,
		    const FSourceLocation* InitializerStart = nullptr)
		{
			const auto Body = std::lower_bound(Tokens.begin(), Tokens.end(), Scope.Body->Location,
			    [](const FToken& Token, const FSourceLocation& Location)
			{
				return Token.Location.Line < Location.Line ||
				    (Token.Location.Line == Location.Line && Token.Location.Column < Location.Column);
			});
			if (Body == Tokens.end() || Body->Lexeme != "{" ||
			    Body->Location.Line != Scope.Body->Location.Line ||
			    Body->Location.Column != Scope.Body->Location.Column)
			{
				return;
			}

			// Constructor initializers precede the body and have their own argument
			// lists. Search before the first target so its ')' cannot be mistaken
			// for the constructor's parameter-list terminator.
			auto ParameterEnd = Body;
			if (InitializerStart && InitializerStart->FileLabel == Scope.FileLabel)
			{
				ParameterEnd = std::lower_bound(Tokens.begin(), Body, *InitializerStart,
				    [](const FToken& Token, const FSourceLocation& Location)
				{
					return Token.Location.Line < Location.Line ||
					    (Token.Location.Line == Location.Line && Token.Location.Column < Location.Column);
				});
			}
			// A trailing where-clause may itself contain function types such as
			// Guard<fn()->int>. Its ')' belongs to the bound, not this callable.
			for (auto Probe = ParameterEnd; Probe != Tokens.begin();)
			{
				--Probe;
				if (Probe->Kind == ETokenKind::Keyword && Probe->Lexeme == "where")
				{
					ParameterEnd = Probe;
					break;
				}
				if (Probe->Kind == ETokenKind::Symbol && (Probe->Lexeme == ";" || Probe->Lexeme == "{" || Probe->Lexeme == "}"))
				{
					break;
				}
			}
			std::size_t Close = static_cast<std::size_t>(ParameterEnd - Tokens.begin());
			while (Close > 0 && Tokens[Close - 1].Lexeme != ")")
			{
				--Close;
			}

			if (Close == 0) return;
			--Close;
			std::size_t Open = Close;
			int Depth = 1;
			while (Open > 0 && Depth != 0)
			{
				--Open;
				if (Tokens[Open].Lexeme == ")")
				{
					++Depth;
				}

				else if (Tokens[Open].Lexeme == "(")
				{
					--Depth;
				}
			}
			if (Depth != 0) return;
			Scope.StartLine = Tokens[Open].Location.Line;
			Scope.StartColumn = Tokens[Open].Location.Column;
			Scope.DefinitionParameterPositions.assign(Scope.DefinitionParameters.size(), {0, 0});
			int Angles = 0, Parens = 0, Brackets = 0;
			std::size_t Parameter = 0;
			for (std::size_t I = Open + 1; I < Close && Parameter < Scope.DefinitionParameters.size(); ++I)
			{
				const auto Spelling = Tokens[I].Lexeme;
				if (Spelling == "<")
				{
					++Angles;
				}

				else if (Spelling == ">" && Angles > 0)
				{
					--Angles;
				}

				else if (Spelling == ">>" && Angles > 0)
				{
					Angles = (std::max)(0, Angles - 2);
				}

				else if (Spelling == "(")
				{
					++Parens;
				}

				else if (Spelling == ")" && Parens > 0)
				{
					--Parens;
				}

				else if (Spelling == "[")
				{
					++Brackets;
				}

				else if (Spelling == "]" && Brackets > 0)
				{
					--Brackets;
				}

				else if (Angles == 0 && Parens == 0 && Brackets == 0 &&
				    Spelling == Scope.DefinitionParameters[Parameter].Name &&
				    (Tokens[I + 1].Lexeme == "," || Tokens[I + 1].Lexeme == ")" || Tokens[I + 1].Lexeme == "=" || Tokens[I + 1].Lexeme == "["))
				{
					const auto& ParameterRange = Scope.DefinitionParameters[Parameter].DeclMeta.SourceRange;
					Scope.DefinitionParameterPositions[Parameter++] = {Tokens[I].Location.Line, Tokens[I].Location.Column};
					bool bHasDefault = Tokens[I + 1].Lexeme == "=";
					if (Tokens[I + 1].Lexeme == "[")
					{
						// Definition parameter metadata can still belong to its earlier
						// declaration. Array suffixes have explicit, locally balanced bounds.
						std::size_t Next = I + 1;
						int Depth = 0;
						for (; Next < Close; ++Next)
						{
							if (Tokens[Next].Kind != ETokenKind::Symbol)
							{
								continue;
							}
							if (Tokens[Next].Lexeme == "[")
							{
								++Depth;
							}
							else if (Tokens[Next].Lexeme == "]" && Depth > 0 && --Depth == 0 && (Next + 1 == Close || Tokens[Next + 1].Lexeme != "["))
							{
								++Next;
								break;
							}
						}
						if (Depth != 0 || (Next != Close && Tokens[Next].Lexeme != "," && Tokens[Next].Lexeme != "=")) return;
						bHasDefault = Next != Close && Tokens[Next].Lexeme == "=";
						if (!bHasDefault)
						{
							I = Next;
						}
					}
					if (bHasDefault)
					{
						// Defaults can contain commas, comparisons and nested type
						// applications. Their parsed declaration range gives the exact
						// next parameter boundary without reinterpreting expression tokens.
						const auto Before = [](const auto& Left, const auto& Right)
						{
							return Left.Line < Right.Line || (Left.Line == Right.Line && Left.Column < Right.Column);
						};
						const auto& Begin = ParameterRange.Position.Begin;
						const auto& End = ParameterRange.Position.End;
						if (ParameterRange.End <= ParameterRange.Begin || Begin.FileLabel != Scope.FileLabel || End.FileLabel != Scope.FileLabel || !Before(Tokens[Open].Location, Begin) || Before(Tokens[I].Location, Begin) || !Before(Tokens[I].Location, End) || Before(Tokens[Close].Location, End))
						{
							return; // No reliable bound: do not bind names in the default expression.
						}
						const auto Next = std::lower_bound(Tokens.begin() + I + 1, Tokens.begin() + Close, End,
						    [&](const FToken& Token, const auto& Location)
						{
							return Before(Token.Location, Location);
						});
						if (Next != Tokens.begin() + Close && Next->Lexeme != ",") return;
						I = static_cast<std::size_t>(Next - Tokens.begin());
					}
				}
			}
		}

		FCallableScope* AppendCallable(
		    std::vector<FCallableScope>& Out,
		    std::string Name,
		    std::string ContainerName,
		    const FSourceLocation& Location,
		    const std::vector<FFunctionParam>& Parameters,
		    const FBlockStmt* Body,
		    const bool bHasImplicitThis = false,
		    std::string ReturnType = "void",
		    const bool bIsConstMethod = false,
		    const std::vector<FToken>* DefinitionTokens = nullptr,
		    const FSourceRange* DeclarationRange = nullptr)
		{
			if (Body && std::string_view(Body->Location.FileLabel).rfind("veroscript-generated:", 0) == 0) return nullptr;
			FCallableScope Scope;
			Scope.Name = std::move(Name);
			Scope.ContainerName = std::move(ContainerName);
			Scope.ReturnType = std::move(ReturnType);
			Scope.FileLabel.assign(Location.FileLabel.data(), Location.FileLabel.size());
			Scope.DeclarationModulePath = Scope.FileLabel;
			Scope.bHasImplicitThis = bHasImplicitThis;
			Scope.bIsConstMethod = bIsConstMethod;
			Scope.StartLine = Location.Line;
			Scope.StartColumn = Location.Column;
			Scope.Parameters = &Parameters;
			if (Body && !Body->DefinitionModulePath.empty())
			{
				Scope.FileLabel = Body->DefinitionModulePath;
				Scope.StartLine = Body->Location.Line;
				Scope.StartColumn = Body->Location.Column;
				Scope.DefinitionParameters = Parameters;
				for (std::size_t I = 0;
				    I < Scope.DefinitionParameters.size() && I < Body->DefinitionParameterNames.size(); ++I)
				{
					Scope.DefinitionParameters[I].Name = Body->DefinitionParameterNames[I];
				}
			}
			Scope.Body = Body;
			if (DefinitionTokens && Body)
			{
				if (Scope.DefinitionParameters.empty())
				{
					Scope.DefinitionParameters = Parameters;
				}
				BindDefinitionParameterPositions(Scope, *DefinitionTokens);
			}
			// Source ranges include the closing brace (or the declaration's
			// terminator). A following function is only a fallback for AST builders
			// without ranges; it must not extend access or locals into global code.
			const FSourceRange* Range = Body && Body->SourceRange.End > Body->SourceRange.Begin
			    ? &Body->SourceRange
			    : DeclarationRange;
			if (Range && Range->End > Range->Begin)
			{
				const auto& End = Range->Position.End;
				if (End.FileLabel == Scope.FileLabel && (End.Line > Scope.StartLine || (End.Line == Scope.StartLine && End.Column > Scope.StartColumn)))
				{
					Scope.EndLine = End.Line;
					Scope.EndColumn = End.Column;
				}
			}

			Out.push_back(std::move(Scope));
			return &Out.back();
		}

		void CollectCallables(const FProgram& Program, std::vector<FCallableScope>& Out)
		{
			const std::string& Entry = Program.EntryModulePath;
			const FPreprocessLineMap* EntryMap = nullptr;
			bool bLookedUpEntryMap = false;
			std::unique_ptr<FQueryLexer> DefinitionLexer;
			const auto DefinitionTokens = [&](const FBlockStmt* Body,
			                                  const bool bIncludeInlineBody = true) -> const std::vector<FToken>*
			{
				if (!Body || (Body->DefinitionModulePath != Entry && (!bIncludeInlineBody || Body->Location.FileLabel != Entry)) ||
				    Body->Location.FileLabel.rfind("veroscript-generated:", 0) == 0)
				{
					return nullptr;
				}

				if (!bLookedUpEntryMap)
				{
					EntryMap = ProgramPreprocessorLineMap(Program, Entry);
					bLookedUpEntryMap = true;
				}
				if (!EntryMap) return nullptr;
				if (!DefinitionLexer)
				{
					DefinitionLexer = std::make_unique<FQueryLexer>(EntryMap->PreprocessedSource, Entry);
				}

				return &DefinitionLexer->Tokenize();
			};
			for (const FFunctionDecl& Function : Program.Functions)
			{
				if (Function.ModulePath != Entry && (!Function.Body || Function.Body->DefinitionModulePath != Entry))
				{
					continue;
				}
				if (auto* Scope = AppendCallable(
				        Out,
				        Function.Name,
				        {},
				        Function.Location,
				        Function.Parameters,
				        Function.Body.get(),
				        false,
				        Function.ReturnType, false, DefinitionTokens(Function.Body.get()), &Function.DeclMeta.SourceRange))
				{
					Scope->DeclarationModulePath = Function.ModulePath;
					Scope->bExported = Function.bExported;
				}
			}

			static const std::vector<FFunctionParam> EmptyDestructorParams;
			auto AppendTypeCallables = [&Out, &Entry, &DefinitionTokens](const auto& TypeDecl)
			{
				for (const FConstructorDecl& Constructor : TypeDecl.Constructors)
				{
					if (TypeDecl.ModulePath != Entry)
					{
						continue;
					}

					auto* Scope = AppendCallable(Out, TypeDecl.Name, TypeDecl.Name, Constructor.Location,
					    Constructor.Parameters, Constructor.Body.get(), true, "void", false, nullptr,
					    &Constructor.DeclMeta.SourceRange);
					if (const auto* Tokens = Scope ? DefinitionTokens(Constructor.Body.get(), true) : nullptr)
					{
						if (Scope->DefinitionParameters.empty())
						{
							Scope->DefinitionParameters = Constructor.Parameters;
						}
						const FSourceLocation* FirstInitializer = nullptr;
						const auto FindFirst = [&](const auto& Initializers)
						{
							for (const auto& Initializer : Initializers)
							{
								const auto& Location = Initializer.Location;
								if (Location.FileLabel == Scope->FileLabel && (!FirstInitializer || Location.Line < FirstInitializer->Line || (Location.Line == FirstInitializer->Line && Location.Column < FirstInitializer->Column)))
								{
									FirstInitializer = &Location;
								}
							}
						};
						FindFirst(Constructor.BaseInits);
						FindFirst(Constructor.FieldInits);
						BindDefinitionParameterPositions(*Scope, *Tokens, FirstInitializer);
					}
				}
				if (TypeDecl.ModulePath == Entry && TypeDecl.Destructor && TypeDecl.Destructor->Body)
				{
					AppendCallable(
					    Out,
					    "~" + TypeDecl.Name,
					    TypeDecl.Name,
					    TypeDecl.Destructor->Location,
					    EmptyDestructorParams,
					    TypeDecl.Destructor->Body.get(),
					    true, "void", false, nullptr, &TypeDecl.Destructor->DeclMeta.SourceRange);
				}
				for (const FMethodDecl& Method : TypeDecl.Methods)
				{
					if (TypeDecl.ModulePath != Entry &&
					    (!Method.Body || Method.Body->DefinitionModulePath != Entry))
					{
						continue;
					}

					if (Method.bPureVirtual)
					{
						continue;
					}
					AppendCallable(
					    Out,
					    Method.Name,
					    TypeDecl.Name,
					    Method.Location,
					    Method.Parameters,
					    Method.Body.get(),
					    !Method.bIsStatic,
					    Method.ReturnType,
					    Method.bIsConst, DefinitionTokens(Method.Body.get()), &Method.DeclMeta.SourceRange);
				}
			};

			for (const FStructDecl& StructDecl : Program.Structs)
			{
				AppendTypeCallables(StructDecl);
			}
			for (const FClassDecl& ClassDecl : Program.Classes)
			{
				AppendTypeCallables(ClassDecl);
			}
			for (const FInterfaceDecl& InterfaceDecl : Program.Interfaces)
			{

				for (const FMethodDecl& Method : InterfaceDecl.Methods)
				{
					if (InterfaceDecl.ModulePath != Entry &&
					    (!Method.Body || Method.Body->DefinitionModulePath != Entry))
					{
						continue;
					}

					if (Method.bPureVirtual)
					{
						continue;
					}
					AppendCallable(
					    Out,
					    Method.Name,
					    InterfaceDecl.Name,
					    Method.Location,
					    Method.Parameters,
					    Method.Body.get(),
					    !Method.bIsStatic,
					    Method.ReturnType,
					    Method.bIsConst, DefinitionTokens(Method.Body.get()), &Method.DeclMeta.SourceRange);
				}
			}
			Out.erase(std::remove_if(Out.begin(), Out.end(),
			              [&](const auto& C)
			{
				return C.FileLabel != Entry;
			}),
			    Out.end());
		}

		void AssignCallableEndLines(std::vector<FCallableScope>& Callables)
		{
			std::sort(
			    Callables.begin(),
			    Callables.end(),
			    [](const FCallableScope& Left, const FCallableScope& Right)
			{
				if (Left.StartLine != Right.StartLine)
				{
					return Left.StartLine < Right.StartLine;
				}
				return Left.StartColumn < Right.StartColumn;
			});

			for (std::size_t Index = 0; Index + 1 < Callables.size(); ++Index)
			{
				if (Callables[Index].EndLine == std::numeric_limits<std::uint32_t>::max())
				{
					Callables[Index].EndLine = Callables[Index + 1].StartLine;
					Callables[Index].EndColumn = Callables[Index + 1].StartColumn;
				}
			}
		}

		const FCallableScope* FindEnclosingCallable(
		    const std::vector<FCallableScope>& Callables,
		    const FTextPosition Position)
		{
			const FCallableScope* Best = nullptr;
			std::uint32_t BestSpan = std::numeric_limits<std::uint32_t>::max();
			for (const FCallableScope& Scope : Callables)
			{
				if (Position.Line < Scope.StartLine || Position.Line > Scope.EndLine || (Position.Line == Scope.EndLine && Position.Column >= Scope.EndColumn))
				{
					continue;
				}
				if (Position.Line == Scope.StartLine && Position.Column < Scope.StartColumn)
				{
					continue;
				}
				const std::uint32_t Span = Scope.EndLine - Scope.StartLine;
				if (!Best || Span < BestSpan || (Span == BestSpan && (Scope.StartLine > Best->StartLine || (Scope.StartLine == Best->StartLine && Scope.StartColumn > Best->StartColumn))))
				{
					Best = &Scope;
					BestSpan = Span;
				}
			}
			return Best;
		}

		class FCallableQueryScope;
		thread_local FCallableQueryScope* ActiveCallableQueryScope = nullptr;

		// An immutable program can serve many semantic-token positions in one
		// request. Reuse its parameter bindings and source extents only in that
		// request's lifetime; never retain AST pointers across document revisions.
		class FCallableQueryScope
		{
		public:
			explicit FCallableQueryScope(const FProgram& InProgram)
			    : Program(&InProgram), Previous(ActiveCallableQueryScope)
			{
				ActiveCallableQueryScope = this;
			}
			~FCallableQueryScope()
			{
				ActiveCallableQueryScope = Previous;
			}
			FCallableQueryScope(const FCallableQueryScope&) = delete;
			FCallableQueryScope& operator=(const FCallableQueryScope&) = delete;
			bool Matches(const FProgram& Candidate) const
			{
				return Program == &Candidate;
			}
			std::string CallableInitializerType(const FSourceRange& Range, const IHostBindingCatalog* Bindings,
			    const std::function<std::string()>& Compute)
			{
				const auto Key = Range.SourceId + '\x1f' + std::to_string(Range.Begin) + ':' + std::to_string(Range.End) + ':' + std::to_string(reinterpret_cast<std::uintptr_t>(Bindings));
				const auto Found = CallableInitializerTypes.find(Key);
				if (Found != CallableInitializerTypes.end()) return Found->second;
				auto Type = Compute();
				CallableInitializerTypes.emplace(Key, Type);
				return Type;
			}
			std::string LiteralType(const FExpr& Expr)
			{
				const auto Found = LiteralTypes.find(&Expr);
				if (Found != LiteralTypes.end()) return Found->second;
				const FProgram EmptyProgram;
				FTemplateCallTypeQuery Query(EmptyProgram, nullptr, {}, {});
				auto Type = Query.ResolveExpressionType(Expr);
				LiteralTypes.emplace(&Expr, Type);
				return Type;
			}
			const std::vector<FCallableScope>& Get()
			{
				if (!bReady)
				{
					std::vector<FCallableScope> Prepared;
					CollectCallables(*Program, Prepared);
					AssignCallableEndLines(Prepared);
					std::vector<FTextRange> Ranges;
					Ranges.reserve(Prepared.size());
					for (const auto& Scope : Prepared)
					{
						Ranges.push_back({Scope.StartLine, Scope.StartColumn, Scope.EndLine, Scope.EndColumn});
					}
					auto PreparedIndex = FSourceRangeIndex(Ranges);
					Callables = std::move(Prepared);
					RangeIndex = std::move(PreparedIndex);
					bReady = true;
				}
				return Callables;
			}
			const FCallableScope* Find(const FTextPosition Position)
			{
				Get();
				const auto Index = RangeIndex.Find(Position);
				return Index == FSourceRangeIndex::NotFound ? nullptr : &Callables[Index];
			}

		private:
			const FProgram* Program;
			FCallableQueryScope* Previous;
			std::unordered_map<const FExpr*, std::string> LiteralTypes;
			std::unordered_map<std::string, std::string> CallableInitializerTypes;
			std::vector<FCallableScope> Callables;
			FSourceRangeIndex RangeIndex;
			bool bReady = false;
		};

		const FCallableScope* FindEnclosingCallable(const FProgram& Program,
		    const FTextPosition Position, std::vector<FCallableScope>& Storage)
		{
			if (ActiveCallableQueryScope && ActiveCallableQueryScope->Matches(Program))
			{
				return ActiveCallableQueryScope->Find(Position);
			}
			CollectCallables(Program, Storage);
			AssignCallableEndLines(Storage);
			return FindEnclosingCallable(Storage, Position);
		}

		std::string ResolveImplicitThisOwnerName(
		    const FProgram& Program,
		    const FTextPosition Position)
		{
			std::vector<FCallableScope> Callables;
			const FCallableScope* const Scope = FindEnclosingCallable(Program, Position, Callables);
			return Scope && Scope->bHasImplicitThis ? Scope->ContainerName : std::string{};
		}

		bool SourceRangeContainsPositionOrEnd(const FSourceRange& Range, const FTextPosition Position)
		{
			const auto& Begin = Range.Position.Begin;
			const auto& End = Range.Position.End;
			return Begin.Line != 0 && Begin.Column != 0 && (Position.Line > Begin.Line || (Position.Line == Begin.Line && Position.Column >= Begin.Column)) && (Position.Line < End.Line || (Position.Line == End.Line && Position.Column <= End.Column));
		}

		EInitializedCaptureRole FindInitializedCaptureRole(const FProgram& Program, const FTextPosition Position)
		{
			std::vector<FCallableScope> Callables;
			const auto* Callable = FindEnclosingCallable(Program, Position, Callables);
			if (!Callable || !Callable->Body) return EInitializedCaptureRole::None;
			auto Role = EInitializedCaptureRole::None;
			// The prepared AST distinguishes capture names from arbitrary initializer
			// expressions, including nested calls, templates, lambdas and commas.
			VisitBlockNodes(*Callable->Body, [&](const FExpr& Expr)
			{
				CheckAnalysisCancellation();
				if (Expr.Kind != EExprKind::Lambda) return;
				for (const auto& Capture : static_cast<const FLambdaExpr&>(Expr).Captures)
				{
					if (!Capture.Initializer)
					{
						continue;
					}
					if (SourceRangeContainsPositionOrEnd(Capture.NameRange, Position))
					{
						Role = EInitializedCaptureRole::Declaration;
					}
					else if (SourceRangeContainsPositionOrEnd(Capture.Initializer->SourceRange, Position))
					{
						Role = Capture.Initializer->Kind == EExprKind::Identifier || Capture.Initializer->Kind == EExprKind::MemberAccess
						    ? EInitializedCaptureRole::ValueInitializer
						    : EInitializedCaptureRole::Initializer;
					}
				}
			}, {});
			return Role;
		}

		using FScopeStack = std::vector<std::unordered_map<std::string, FSymbolQueryHit>>;
		const FSymbolQueryHit* LookupLocal(const FScopeStack& Stack, std::string_view Name);

		std::string InferMemoryInitializerType(
		    const FExpr& Expr, const FScopeStack& Stack, const unsigned Depth = 0)
		{
			if (Depth >= 32)
			{
				return {};
			}
			if (Expr.Kind == EExprKind::Identifier)
			{
				const auto* Local = LookupLocal(Stack, static_cast<const FIdentifierExpr&>(Expr).Name);
				const std::string Type = Local ? TypeNameFromDetail(Local->Detail) : std::string{};
				return Memory::IsTypeName(Type) || Type.rfind("array<", 0) == 0 ? Type : std::string{};
			}
			if (Expr.Kind == EExprKind::Call)
			{
				const auto& Call = static_cast<const FCallExpr&>(Expr);
				if (Memory::IsTypeName(Call.Callee) && Call.Callee != "std.memory.byte_order")
				{
					return Call.Callee;
				}
				// Namespace-only intrinsics require parser provenance; a leaf name
				// imported by a using-directive is not a valid intrinsic call.
				if (!Call.bStandardLibraryQualified || LookupLocal(Stack, Call.Callee))
				{
					return {};
				}
				const auto Resolved = StandardLibrary::ResolveFunctionName(Call.Callee);
				const std::string_view Canonical = Resolved.empty()
				    ? std::string_view(Call.Callee)
				    : Resolved;
				if (Canonical == "__memory_clone" || Canonical == "__memory_from_array")
				{
					return "std.memory.buffer";
				}
				if (Canonical == "__memory_to_array")
				{
					return "array<uint8>";
				}
				if (Canonical == "__memory_read" && Call.TypeArgs.size() == 1)
				{
					return Call.TypeArgs.front();
				}
				return {};
			}
			if (Expr.Kind == EExprKind::MethodCall)
			{
				const auto& Call = static_cast<const FMethodCallExpr&>(Expr);
				const std::string Owner = Call.Object
				    ? InferMemoryInitializerType(*Call.Object, Stack, Depth + 1)
				    : std::string{};
				const auto* Type = Memory::GetBindingCatalog().FindType(Owner);
				if (!Type)
				{
					return {};
				}
				std::string Result;
				for (const FHostMemberDesc& Member : Type->Members)
				{
					if (Member.Kind != EHostMemberKind::Method || Member.Name != Call.Method)
					{
						continue;
					}
					const std::string ReturnType = TypeNameFromDetail(FormatHostMemberSignature(Member, Type->Name));
					if (!Result.empty() && Result != ReturnType)
					{
						return {};
					}
					Result = ReturnType;
				}
				return Result;
			}
			return {};
		}

		FSymbolQueryHit MakeVarHit(const FVarDeclStmt& VarDecl, const FScopeStack& Stack)
		{
			FSymbolQueryHit Hit;
			Hit.bLocalStatic = VarDecl.bIsLocalStatic;
			Hit.Kind = VarDecl.bIsConst
			    ? EDocumentSymbolKind::Constant
			    : EDocumentSymbolKind::Variable;
			Hit.Name = VarDecl.Name;
			Hit.DeclarationRange = VarDecl.SourceRange;
			if (VarDecl.Initializer && VarDecl.Initializer->SourceRange.SourceId == Hit.DeclarationRange.SourceId && VarDecl.Initializer->SourceRange.Begin > Hit.DeclarationRange.Begin && VarDecl.Initializer->SourceRange.Begin <= Hit.DeclarationRange.End)
			{
				Hit.DeclarationRange.End = VarDecl.Initializer->SourceRange.Begin;
			}
			else if (VarDecl.Initializer)
			{
				// Synthetic initializers without source bounds cannot delimit a
				// declaration header reliably; retain the original AST location.
				Hit.DeclarationRange = {};
			}
			Hit.Detail = VarDecl.bIsConst ? "const " : "";
			std::string TypeName = VarDecl.TypeName;
			if (TypeName == "auto" && VarDecl.Initializer)
			{
				Hit.InitializerRange = VarDecl.Initializer->SourceRange;
				const std::string Inferred = InferMemoryInitializerType(*VarDecl.Initializer, Stack);
				if (!Inferred.empty())
				{
					TypeName = Inferred;
				}
			}
			Hit.Detail += TypeName;
			if (VarDecl.bIsRef)
			{
				Hit.Detail += '&';
			}
			Hit.FileLabel.assign(
			    VarDecl.Location.FileLabel.data(),
			    VarDecl.Location.FileLabel.size());
			Hit.DefLine = VarDecl.Location.Line;
			Hit.DefColumn = VarDecl.Location.Column;
			Hit.Documentation = VarDecl.DeclMeta.Documentation;
			Hit.bHasSourceDocumentation = !VarDecl.DeclMeta.StableId.empty() ||
			    VarDecl.DeclMeta.CommentRetention != ECommentRetention::Full;
			return Hit;
		}

		FSymbolQueryHit MakeTypeAliasHit(const FTypeAliasStmt& Alias)
		{
			FSymbolQueryHit Hit;
			Hit.bTypeDefinition = Alias.bTypeDefinition;
			Hit.Kind = EDocumentSymbolKind::TypeAlias;
			Hit.Name = Alias.Name;
			Hit.Detail = Alias.TypeName;
			Hit.FileLabel.assign(
			    Alias.Location.FileLabel.data(),
			    Alias.Location.FileLabel.size());
			Hit.DefLine = Alias.Location.Line;
			Hit.DefColumn = Alias.Location.Column;
			Hit.Documentation = Alias.DeclMeta.Documentation;
			Hit.bHasSourceDocumentation = !Alias.DeclMeta.StableId.empty() ||
			    Alias.DeclMeta.CommentRetention != ECommentRetention::Full;
			return Hit;
		}

		FSymbolQueryHit MakeParamHit(
		    const FFunctionParam& Param,
		    const FSourceLocation& Location,
		    const std::string_view TypeOverride = {})
		{
			FSymbolQueryHit Hit;
			Hit.Kind = EDocumentSymbolKind::Variable;
			Hit.Name = Param.Name;
			const std::string_view TypeName =
			    !TypeOverride.empty() ? TypeOverride : std::string_view(Param.TypeName);
			std::ostringstream Detail;
			if (Param.bIsConst)
			{
				Detail << "const ";
			}
			if (Param.bIsVariadic)
			{
				Detail << TypeName;
				if (Param.bIsRef)
				{
					Detail << '&';
				}
				Detail << "...";
			}
			else
			{
				Detail << TypeName;
				if (Param.bIsRef)
				{
					Detail << '&';
				}
			}
			Hit.Detail = Detail.str();
			Hit.FileLabel.assign(Location.FileLabel.data(), Location.FileLabel.size());
			Hit.DefLine = Location.Line;
			Hit.DefColumn = Location.Column;
			return Hit;
		}

		// Parse `fn(T0, T1)->R` parameter types (sealed contextual-lambda surface).
		bool TryParseFnTypeParams(
		    const std::string_view TypeName,
		    std::vector<std::string>& OutParams)
		{
			OutParams.clear();
			if (TypeName.size() < 4 || TypeName.compare(0, 3, "fn(") != 0)
			{
				return false;
			}
			int Depth = 0;
			std::size_t ArgStart = 3;
			std::size_t Close = std::string_view::npos;
			for (std::size_t Index = 3; Index < TypeName.size(); ++Index)
			{
				const char Char = TypeName[Index];
				if (Char == '<' || Char == '(')
				{
					++Depth;
				}
				else if ((Char == '>' && (Index == 0 || TypeName[Index - 1] != '-')) || Char == ')')
				{
					if (Char == ')' && Depth == 0)
					{
						Close = Index;
						break;
					}
					--Depth;
				}
				else if (Char == ',' && Depth == 0)
				{
					std::string Arg(TypeName.substr(ArgStart, Index - ArgStart));
					while (!Arg.empty() && std::isspace(static_cast<unsigned char>(Arg.front())))
					{
						Arg.erase(Arg.begin());
					}
					while (!Arg.empty() && std::isspace(static_cast<unsigned char>(Arg.back())))
					{
						Arg.pop_back();
					}
					if (!Arg.empty())
					{
						OutParams.push_back(std::move(Arg));
					}
					ArgStart = Index + 1;
				}
			}
			if (Close == std::string_view::npos)
			{
				OutParams.clear();
				return false;
			}
			std::string Last(TypeName.substr(ArgStart, Close - ArgStart));
			while (!Last.empty() && std::isspace(static_cast<unsigned char>(Last.front())))
			{
				Last.erase(Last.begin());
			}
			while (!Last.empty() && std::isspace(static_cast<unsigned char>(Last.back())))
			{
				Last.pop_back();
			}
			if (!Last.empty())
			{
				OutParams.push_back(std::move(Last));
			}
			return true;
		}

		void BindLocal(FScopeStack& Stack, FSymbolQueryHit Hit)
		{
			if (Stack.empty())
			{
				Stack.emplace_back();
			}
			Stack.back()[Hit.Name] = std::move(Hit);
		}

		const FSymbolQueryHit* LookupLocal(const FScopeStack& Stack, const std::string_view Name)
		{
			for (auto Frame = Stack.rbegin(); Frame != Stack.rend(); ++Frame)
			{
				const auto Found = Frame->find(std::string(Name));
				if (Found != Frame->end())
				{
					return &Found->second;
				}
			}
			return nullptr;
		}

		bool TryResolveCollectionExprType(
		    const FScopeStack& Stack,
		    const FExpr& Expr,
		    std::string& OutTypeName)
		{
			switch (Expr.Kind)
			{
			case EExprKind::Identifier:
				{
					const auto& Ident = static_cast<const FIdentifierExpr&>(Expr);
					if (const FSymbolQueryHit* Local = LookupLocal(Stack, Ident.Name))
					{
						OutTypeName = TypeNameFromDetail(Local->Detail);
						return !OutTypeName.empty() && OutTypeName != "auto";
					}
					return false;
				}
			case EExprKind::MethodCall:
				{
					const auto& MethodCall = static_cast<const FMethodCallExpr&>(Expr);
					if (!MethodCall.Object)
					{
						return false;
					}
					std::string OwnerType;
					if (!TryResolveCollectionExprType(Stack, *MethodCall.Object, OwnerType))
					{
						return false;
					}
					return ResolveSoftContainerMethodReturnType(
					    OwnerType,
					    MethodCall.Method,
					    OutTypeName);
				}
			case EExprKind::IndexAccess:
				{
					const auto& Index = static_cast<const FIndexAccessExpr&>(Expr);
					if (!Index.Object)
					{
						return false;
					}
					std::string OwnerType;
					if (!TryResolveCollectionExprType(Stack, *Index.Object, OwnerType))
					{
						return false;
					}
					return ResolveContainerIndexElementType(OwnerType, OutTypeName);
				}
			case EExprKind::MemberAccess:
				{
					// Soft Pair fields only when receiver type is known as pair<…>.
					const auto& Member = static_cast<const FMemberAccessExpr&>(Expr);
					if (!Member.Object)
					{
						return false;
					}
					std::string OwnerType;
					if (!TryResolveCollectionExprType(Stack, *Member.Object, OwnerType))
					{
						return false;
					}
					std::string First;
					std::string Second;
					if (!Runtime::ParsePairType(OwnerType, First, Second))
					{
						return false;
					}
					if (Member.Member == "first")
					{
						OutTypeName = std::move(First);
						return true;
					}
					if (Member.Member == "second")
					{
						OutTypeName = std::move(Second);
						return true;
					}
					return false;
				}
			default:
				return false;
			}
		}

		void BindLambdaParams(
		    FScopeStack& Stack,
		    const FLambdaExpr& Lambda,
		    const FSourceLocation& Location,
		    const std::string_view ExpectedFnType)
		{
			std::vector<std::string> ExpectedParams;
			(void)TryParseFnTypeParams(ExpectedFnType, ExpectedParams);
			for (std::size_t Index = 0; Index < Lambda.Parameters.size(); ++Index)
			{
				const FFunctionParam& Param = Lambda.Parameters[Index];
				if (Param.Name.empty())
				{
					continue;
				}
				std::string_view Override;
				if (Param.TypeName.empty() && Index < ExpectedParams.size())
				{
					Override = ExpectedParams[Index];
				}
				BindLocal(Stack, MakeParamHit(Param, Location, Override));
			}
		}

		void BindInitializedCaptures(FScopeStack& Stack, const FLambdaExpr& Lambda)
		{
			std::vector<FSymbolQueryHit> Bindings;
			std::unordered_set<std::string> Seen;
			for (auto Scope = Stack.rbegin(); Scope != Stack.rend(); ++Scope)
			{
				for (const auto& Binding : *Scope)
				{
					const auto& Name = Binding.first;
					if (!Seen.insert(Name).second || Binding.second.Kind == EDocumentSymbolKind::Label)
					{
						continue;
					}
					auto Hit = Binding.second;
					const bool bDirectLookup = Hit.bLocalStatic || Hit.Kind == EDocumentSymbolKind::TypeAlias;
					if (!bDirectLookup && (Lambda.CaptureKind == ELambdaCaptureKind::None || (Lambda.CaptureKind == ELambdaCaptureKind::Explicit && !Lambda.HasCapture(Name, false) && !Lambda.HasCapture(Name, true))))
					{
						continue;
					}
					const bool bReference = Lambda.CaptureKind == ELambdaCaptureKind::Reference || Lambda.HasCapture(Name, true);
					if (!bDirectLookup && !bReference && !Lambda.bMutable && Hit.Kind == EDocumentSymbolKind::Variable)
					{
						Hit.Kind = EDocumentSymbolKind::Constant;
						if (Hit.Detail.rfind("const ", 0) != 0)
						{
							Hit.Detail.insert(0, "const ");
						}
					}
					Bindings.push_back(std::move(Hit));
				}
			}
			for (const auto& Capture : Lambda.Captures)
			{
				if (!Capture.Initializer)
				{
					continue;
				}
				FSymbolQueryHit Hit;
				Hit.Name = Capture.Name;
				const auto* Source = Capture.Initializer->Kind == EExprKind::Identifier
				    ? LookupLocal(Stack, static_cast<const FIdentifierExpr&>(*Capture.Initializer).Name)
				    : nullptr;
				const bool bConstReference = Capture.bByReference && Source && (Source->Kind == EDocumentSymbolKind::Constant || Source->bReadOnly || Source->Detail.rfind("const ", 0) == 0);
				Hit.Kind = (!Lambda.bMutable && !Capture.bByReference) || bConstReference
				    ? EDocumentSymbolKind::Constant
				    : EDocumentSymbolKind::Variable;
				std::string Type;
				if (!TryResolveCollectionExprType(Stack, *Capture.Initializer, Type))
				{
					Type = InferMemoryInitializerType(*Capture.Initializer, Stack);
				}
				if (Type.empty() && Capture.Initializer->Kind <= EExprKind::NullptrLiteral)
				{
					// Let Core name literal types; no caller bindings or user code run.
					if (ActiveCallableQueryScope)
					{
						Type = ActiveCallableQueryScope->LiteralType(*Capture.Initializer);
					}
					else
					{
						const FProgram EmptyProgram;
						FTemplateCallTypeQuery Query(EmptyProgram, nullptr, {}, {});
						Type = Query.ResolveExpressionType(*Capture.Initializer);
					}
				}
				Hit.Detail = Hit.Kind == EDocumentSymbolKind::Constant ? "const " : "";
				Hit.Detail += Type.empty() ? "auto" : Type;
				if (Capture.bByReference)
				{
					Hit.Detail += '&';
				}
				Hit.InitializerRange = Capture.Initializer->SourceRange;
				Hit.FileLabel = std::string(Capture.Location.FileLabel);
				Hit.DefLine = Capture.NameRange.Position.Begin.Line;
				Hit.DefColumn = Capture.NameRange.Position.Begin.Column;
				Bindings.push_back(std::move(Hit));
			}
			// The lambda has its own value scope. Unlisted outer locals and labels
			// are unavailable; aliases and local statics remain direct lookups.
			Stack.clear();
			Stack.emplace_back();
			for (auto& Binding : Bindings)
			{
				BindLocal(Stack, std::move(Binding));
			}
		}

		void WalkBlockForLocals(
		    const FBlockStmt& Block,
		    const FTextPosition Position,
		    FScopeStack& Stack,
		    const FSourceLocation& BlockEndExclusive);

		bool WalkExprForLocals(
		    const FExpr& Expr,
		    const FSourceLocation& EndExclusive,
		    const FTextPosition Position,
		    FScopeStack& Stack,
		    const std::string_view ExpectedType = {})
		{
			CheckAnalysisCancellation();
			switch (Expr.Kind)
			{
			case EExprKind::Lambda:
				{
					const auto& Lambda = static_cast<const FLambdaExpr&>(Expr);
					const FSourceLocation BodyStart = Lambda.Body ? Lambda.Body->Location
					                                              : (Lambda.ExpressionBody ? Lambda.ExpressionBody->Location : EndExclusive);
					if (RangeContainsExclusiveEnd(Expr.Location, BodyStart, Position))
					{
						bool bInitializedDeclaration = false;
						for (std::size_t Index = 0; Index < Lambda.Captures.size(); ++Index)
						{
							const auto& Capture = Lambda.Captures[Index];
							if (!Capture.Initializer)
							{
								continue;
							}
							bInitializedDeclaration |= SourceRangeContainsPositionOrEnd(Capture.NameRange, Position);
							const auto End = Index + 1u < Lambda.Captures.size() ? Lambda.Captures[Index + 1u].Location : BodyStart;
							if (SourceRangeContainsPositionOrEnd(Capture.Initializer->SourceRange, Position))
							{
								(void)WalkExprForLocals(*Capture.Initializer, End, Position, Stack);
								return true;
							}
						}
						const bool bParameter = std::any_of(Lambda.Parameters.begin(), Lambda.Parameters.end(),
						    [Position](const auto& Param)
						{
							return SourceRangeContainsPositionOrEnd(Param.DeclMeta.SourceRange, Position);
						});
						// Capture-list names are looked up in the enclosing value scope.
						// Only a new capture declaration or a lambda parameter uses the
						// closure's own bindings before its body starts.
						if (!bInitializedDeclaration && !bParameter) return true;
					}
					if (Lambda.Body)
					{
						const FSourceLocation LambdaEnd =
						    ExclusiveEndForBlock(*Lambda.Body, EndExclusive);
						// Include `fn(params)` — not only the body — so Hover on contextual params works.
						if (!RangeContainsExclusiveEnd(Expr.Location, LambdaEnd, Position))
						{
							return false;
						}
						BindInitializedCaptures(Stack, Lambda);
						BindLambdaParams(Stack, Lambda, Expr.Location, ExpectedType);
						WalkBlockForLocals(*Lambda.Body, Position, Stack, LambdaEnd);
						return true;
					}
					if (!Lambda.ExpressionBody)
					{
						return false;
					}
					const FSourceLocation LambdaEnd =
					    ExclusiveEndForExpr(*Lambda.ExpressionBody, EndExclusive);
					if (!RangeContainsExclusiveEnd(Expr.Location, LambdaEnd, Position))
					{
						return false;
					}
					BindInitializedCaptures(Stack, Lambda);
					BindLambdaParams(Stack, Lambda, Expr.Location, ExpectedType);
					WalkExprForLocals(*Lambda.ExpressionBody, LambdaEnd, Position, Stack);
					return true;
				}
			case EExprKind::Call:
				{
					const auto& Call = static_cast<const FCallExpr&>(Expr);
					if (Call.CalleeExpr)
					{
						const FSourceLocation CalleeEnd = !Call.Arguments.empty() && Call.Arguments.front()
						    ? Call.Arguments.front()->Location
						    : EndExclusive;
						if (WalkExprForLocals(*Call.CalleeExpr, CalleeEnd, Position, Stack))
						{
							return true;
						}
					}
					for (std::size_t Index = 0; Index < Call.Arguments.size(); ++Index)
					{
						const FExpr* Arg = Call.Arguments[Index].get();
						if (!Arg)
						{
							continue;
						}
						const FSourceLocation ArgEnd =
						    (Index + 1 < Call.Arguments.size() && Call.Arguments[Index + 1])
						    ? Call.Arguments[Index + 1]->Location
						    : EndExclusive;
						if (WalkExprForLocals(*Arg, ArgEnd, Position, Stack))
						{
							return true;
						}
					}
					return false;
				}
			case EExprKind::Assign:
				{
					const auto& Assign = static_cast<const FAssignExpr&>(Expr);
					if (Assign.Target)
					{
						const FSourceLocation TargetEnd = Assign.Value
						    ? Assign.Value->Location
						    : EndExclusive;
						if (WalkExprForLocals(*Assign.Target, TargetEnd, Position, Stack))
						{
							return true;
						}
					}
					return Assign.Value && WalkExprForLocals(*Assign.Value, EndExclusive, Position, Stack);
				}
			case EExprKind::Binary:
				{
					const auto& Binary = static_cast<const FBinaryExpr&>(Expr);
					if (Binary.Left)
					{
						const FSourceLocation LeftEnd = Binary.Right
						    ? Binary.Right->Location
						    : EndExclusive;
						if (WalkExprForLocals(*Binary.Left, LeftEnd, Position, Stack))
						{
							return true;
						}
					}
					return Binary.Right && WalkExprForLocals(*Binary.Right, EndExclusive, Position, Stack);
				}
			case EExprKind::Unary:
				return static_cast<const FUnaryExpr&>(Expr).Operand && WalkExprForLocals(*static_cast<const FUnaryExpr&>(Expr).Operand, EndExclusive, Position, Stack);
			case EExprKind::Conditional:
				{
					const auto& Cond = static_cast<const FConditionalExpr&>(Expr);
					if (Cond.Condition)
					{
						const FSourceLocation CondEnd = Cond.ThenExpr
						    ? Cond.ThenExpr->Location
						    : EndExclusive;
						if (WalkExprForLocals(*Cond.Condition, CondEnd, Position, Stack))
						{
							return true;
						}
					}
					if (Cond.ThenExpr)
					{
						const FSourceLocation ThenEnd = Cond.ElseExpr
						    ? Cond.ElseExpr->Location
						    : EndExclusive;
						if (WalkExprForLocals(*Cond.ThenExpr, ThenEnd, Position, Stack))
						{
							return true;
						}
					}
					return Cond.ElseExpr && WalkExprForLocals(*Cond.ElseExpr, EndExclusive, Position, Stack);
				}
			case EExprKind::MemberAccess:
				return static_cast<const FMemberAccessExpr&>(Expr).Object && WalkExprForLocals(*static_cast<const FMemberAccessExpr&>(Expr).Object, EndExclusive, Position, Stack);
			case EExprKind::MethodCall:
				{
					const auto& MethodCall = static_cast<const FMethodCallExpr&>(Expr);
					if (MethodCall.Object)
					{
						const FSourceLocation ObjectEnd =
						    !MethodCall.Arguments.empty() && MethodCall.Arguments.front()
						    ? MethodCall.Arguments.front()->Location
						    : EndExclusive;
						if (WalkExprForLocals(*MethodCall.Object, ObjectEnd, Position, Stack))
						{
							return true;
						}
					}
					for (std::size_t Index = 0; Index < MethodCall.Arguments.size(); ++Index)
					{
						const FExpr* Arg = MethodCall.Arguments[Index].get();
						if (!Arg)
						{
							continue;
						}
						const FSourceLocation ArgEnd =
						    (Index + 1 < MethodCall.Arguments.size() && MethodCall.Arguments[Index + 1])
						    ? MethodCall.Arguments[Index + 1]->Location
						    : EndExclusive;
						if (WalkExprForLocals(*Arg, ArgEnd, Position, Stack))
						{
							return true;
						}
					}
					return false;
				}
			case EExprKind::IndexAccess:
				{
					const auto& Index = static_cast<const FIndexAccessExpr&>(Expr);
					if (Index.Object)
					{
						const FSourceLocation ObjectEnd = Index.Index
						    ? Index.Index->Location
						    : EndExclusive;
						if (WalkExprForLocals(*Index.Object, ObjectEnd, Position, Stack))
						{
							return true;
						}
					}
					return Index.Index && WalkExprForLocals(*Index.Index, EndExclusive, Position, Stack);
				}
			case EExprKind::ArrayInit:
				{
					const auto& ArrayInit = static_cast<const FArrayInitExpr&>(Expr);
					for (std::size_t Index = 0; Index < ArrayInit.Elements.size(); ++Index)
					{
						const FExpr* Elem = ArrayInit.Elements[Index].get();
						if (!Elem)
						{
							continue;
						}
						const FSourceLocation ElemEnd =
						    (Index + 1 < ArrayInit.Elements.size() && ArrayInit.Elements[Index + 1])
						    ? ArrayInit.Elements[Index + 1]->Location
						    : EndExclusive;
						if (WalkExprForLocals(*Elem, ElemEnd, Position, Stack))
						{
							return true;
						}
					}
					return false;
				}
			case EExprKind::MapInit:
				{
					const auto& MapInit = static_cast<const FMapInitExpr&>(Expr);
					for (std::size_t Index = 0; Index < MapInit.Entries.size(); ++Index)
					{
						const FMapInitEntry& Entry = MapInit.Entries[Index];
						const FSourceLocation EntryEnd =
						    (Index + 1 < MapInit.Entries.size() && MapInit.Entries[Index + 1].Key)
						    ? MapInit.Entries[Index + 1].Key->Location
						    : EndExclusive;
						if (Entry.Key)
						{
							const FSourceLocation KeyEnd = Entry.Value
							    ? Entry.Value->Location
							    : EntryEnd;
							if (WalkExprForLocals(*Entry.Key, KeyEnd, Position, Stack))
							{
								return true;
							}
						}
						if (Entry.Value && WalkExprForLocals(*Entry.Value, EntryEnd, Position, Stack))
						{
							return true;
						}
					}
					return false;
				}
			case EExprKind::Ref:
				return static_cast<const FRefExpr&>(Expr).Operand && WalkExprForLocals(*static_cast<const FRefExpr&>(Expr).Operand, EndExclusive, Position, Stack);
			default:
				return false;
			}
		}

		void WalkStmtForLocals(
		    const FStmt& Statement,
		    const FSourceLocation& EndExclusive,
		    const FTextPosition Position,
		    FScopeStack& Stack)
		{
			CheckAnalysisCancellation();
			switch (Statement.Kind)
			{
			case EStmtKind::VarDecl:
				{
					const auto& VarDecl = static_cast<const FVarDeclStmt&>(Statement);
					if (LocationAtOrBefore(Statement.Location, Position))
					{
						BindLocal(Stack, MakeVarHit(VarDecl, Stack));
					}
					if (VarDecl.Initializer)
					{
						WalkExprForLocals(
						    *VarDecl.Initializer,
						    EndExclusive,
						    Position,
						    Stack,
						    VarDecl.TypeName);
					}
					break;
				}
			case EStmtKind::VarDeclGroup:
				{
					const auto& Declarations =
					    static_cast<const FVarDeclGroupStmt&>(Statement).Declarations;
					for (std::size_t Index = 0; Index < Declarations.size(); ++Index)
					{
						const std::unique_ptr<FVarDeclStmt>& Declaration = Declarations[Index];
						if (Declaration)
						{
							FSourceLocation DeclarationEnd = EndExclusive;
							for (std::size_t Next = Index + 1; Next < Declarations.size(); ++Next)
							{
								if (Declarations[Next])
								{
									DeclarationEnd = Declarations[Next]->Location;
									break;
								}
							}
							WalkStmtForLocals(*Declaration, DeclarationEnd, Position, Stack);
						}
					}
					break;
				}
			case EStmtKind::TypeAlias:
				{
					if (LocationAtOrBefore(Statement.Location, Position))
					{
						BindLocal(
						    Stack,
						    MakeTypeAliasHit(static_cast<const FTypeAliasStmt&>(Statement)));
					}
					break;
				}
			case EStmtKind::Label:
				{
					if (LocationAtOrBefore(Statement.Location, Position))
					{
						FSymbolQueryHit Hit;
						Hit.Kind = EDocumentSymbolKind::Label;
						Hit.Name = static_cast<const FLabelStmt&>(Statement).Name;
						Hit.Detail = "label";
						Hit.FileLabel.assign(
						    Statement.Location.FileLabel.data(),
						    Statement.Location.FileLabel.size());
						Hit.DefLine = Statement.Location.Line;
						Hit.DefColumn = Statement.Location.Column;
						BindLocal(Stack, std::move(Hit));
					}
					break;
				}
			case EStmtKind::Expr:
				{
					const auto& ExprStmt = static_cast<const FExprStmt&>(Statement);
					if (ExprStmt.Expression && RangeContainsExclusiveEnd(Statement.Location, EndExclusive, Position))
					{
						WalkExprForLocals(*ExprStmt.Expression, EndExclusive, Position, Stack);
					}
					break;
				}
			case EStmtKind::Return:
				{
					const auto& ReturnStmt = static_cast<const FReturnStmt&>(Statement);
					if (ReturnStmt.Value && RangeContainsExclusiveEnd(Statement.Location, EndExclusive, Position))
					{
						WalkExprForLocals(*ReturnStmt.Value, EndExclusive, Position, Stack);
					}
					break;
				}
			case EStmtKind::Throw:
				{
					const auto& ThrowStmt = static_cast<const FThrowStmt&>(Statement);
					if (ThrowStmt.Value && RangeContainsExclusiveEnd(Statement.Location, EndExclusive, Position))
					{
						WalkExprForLocals(*ThrowStmt.Value, EndExclusive, Position, Stack);
					}
					break;
				}
			case EStmtKind::Try:
				{
					const FSourceLocation StmtEnd = ExclusiveEndForStmt(Statement, EndExclusive);
					if (!RangeContainsExclusiveEnd(Statement.Location, StmtEnd, Position))
					{
						break;
					}
					const auto& TryStmt = static_cast<const FTryStmt&>(Statement);
					if (TryStmt.TryBlock)
					{
						const FSourceLocation TryEnd = !TryStmt.Catches.empty()
						    ? TryStmt.Catches.front().Location
						    : (TryStmt.FinallyBlock ? TryStmt.FinallyBlock->Location : StmtEnd);
						if (RangeContainsExclusiveEnd(TryStmt.TryBlock->Location, TryEnd, Position))
						{
							WalkBlockForLocals(*TryStmt.TryBlock, Position, Stack, TryEnd);
							break;
						}
					}
					for (std::size_t Index = 0; Index < TryStmt.Catches.size(); ++Index)
					{
						const FCatchClause& Clause = TryStmt.Catches[Index];
						if (!Clause.Body)
						{
							continue;
						}
						const FSourceLocation CatchEnd = Index + 1 < TryStmt.Catches.size()
						    ? TryStmt.Catches[Index + 1].Location
						    : (TryStmt.FinallyBlock ? TryStmt.FinallyBlock->Location : StmtEnd);
						if (!RangeContainsExclusiveEnd(Clause.Location, CatchEnd, Position))
						{
							continue;
						}
						Stack.emplace_back();
						if (!Clause.bCatchAll && !Clause.Name.empty())
						{
							FSymbolQueryHit Hit;
							Hit.Kind = Clause.bIsConst
							    ? EDocumentSymbolKind::Constant
							    : EDocumentSymbolKind::Variable;
							Hit.Name = Clause.Name;
							Hit.Detail = (Clause.bIsConst ? "const " : "") + Clause.TypeName + (Clause.bIsRef ? "&" : "");
							const FSourceLocation& NameLocation =
							    Clause.NameLocation.Line != 0
							    ? Clause.NameLocation
							    : Clause.Location;
							Hit.FileLabel.assign(
							    NameLocation.FileLabel.data(),
							    NameLocation.FileLabel.size());
							Hit.DefLine = NameLocation.Line;
							Hit.DefColumn = NameLocation.Column;
							BindLocal(Stack, std::move(Hit));
						}
						WalkBlockForLocals(*Clause.Body, Position, Stack, CatchEnd);
						break;
					}
					if (TryStmt.FinallyBlock && RangeContainsExclusiveEnd(TryStmt.FinallyBlock->Location, StmtEnd, Position))
					{
						WalkBlockForLocals(*TryStmt.FinallyBlock, Position, Stack, StmtEnd);
					}
					break;
				}
			case EStmtKind::Block:
				{
					const FSourceLocation StmtEnd = ExclusiveEndForStmt(Statement, EndExclusive);
					if (RangeContainsExclusiveEnd(Statement.Location, StmtEnd, Position))
					{
						WalkBlockForLocals(
						    static_cast<const FBlockStmt&>(Statement),
						    Position,
						    Stack,
						    StmtEnd);
					}
					break;
				}
			case EStmtKind::If:
				{
					const FSourceLocation StmtEnd = ExclusiveEndForStmt(Statement, EndExclusive);
					if (!RangeContainsExclusiveEnd(Statement.Location, StmtEnd, Position))
					{
						break;
					}
					const auto& IfStmt = static_cast<const FIfStmt&>(Statement);
					if (IfStmt.Condition)
					{
						const FSourceLocation CondEnd = IfStmt.ThenBranch
						    ? IfStmt.ThenBranch->Location
						    : StmtEnd;
						if (WalkExprForLocals(*IfStmt.Condition, CondEnd, Position, Stack))
						{
							break;
						}
					}
					if (IfStmt.ElseBranch && LocationAtOrBefore(IfStmt.ElseBranch->Location, Position) && RangeContainsExclusiveEnd(IfStmt.ElseBranch->Location, StmtEnd, Position))
					{
						WalkBlockForLocals(*IfStmt.ElseBranch, Position, Stack, StmtEnd);
					}
					else if (IfStmt.ThenBranch && RangeContainsExclusiveEnd(IfStmt.ThenBranch->Location, IfStmt.ElseBranch ? IfStmt.ElseBranch->Location : StmtEnd, Position))
					{
						const FSourceLocation ThenEnd = IfStmt.ElseBranch
						    ? IfStmt.ElseBranch->Location
						    : StmtEnd;
						WalkBlockForLocals(*IfStmt.ThenBranch, Position, Stack, ThenEnd);
					}
					break;
				}
			case EStmtKind::While:
				{
					const FSourceLocation StmtEnd = ExclusiveEndForStmt(Statement, EndExclusive);
					if (!RangeContainsExclusiveEnd(Statement.Location, StmtEnd, Position))
					{
						break;
					}
					const auto& WhileStmt = static_cast<const FWhileStmt&>(Statement);
					if (WhileStmt.Condition)
					{
						const FSourceLocation CondEnd = WhileStmt.Body
						    ? WhileStmt.Body->Location
						    : StmtEnd;
						if (WalkExprForLocals(*WhileStmt.Condition, CondEnd, Position, Stack))
						{
							break;
						}
					}
					if (WhileStmt.Body && RangeContainsExclusiveEnd(WhileStmt.Body->Location, StmtEnd, Position))
					{
						WalkBlockForLocals(*WhileStmt.Body, Position, Stack, StmtEnd);
					}
					break;
				}
			case EStmtKind::DoWhile:
				{
					const FSourceLocation StmtEnd = ExclusiveEndForStmt(Statement, EndExclusive);
					if (!RangeContainsExclusiveEnd(Statement.Location, StmtEnd, Position))
					{
						break;
					}
					const auto& DoWhileStmt = static_cast<const FDoWhileStmt&>(Statement);
					if (DoWhileStmt.Body && DoWhileStmt.Condition && RangeContainsExclusiveEnd(DoWhileStmt.Body->Location, DoWhileStmt.Condition->Location, Position))
					{
						WalkBlockForLocals(
						    *DoWhileStmt.Body, Position, Stack, DoWhileStmt.Condition->Location);
						break;
					}
					if (DoWhileStmt.Condition)
					{
						WalkExprForLocals(*DoWhileStmt.Condition, StmtEnd, Position, Stack);
					}
					else if (DoWhileStmt.Body)
					{
						WalkBlockForLocals(*DoWhileStmt.Body, Position, Stack, StmtEnd);
					}
					break;
				}
			case EStmtKind::For:
				{
					const FSourceLocation StmtEnd = ExclusiveEndForStmt(Statement, EndExclusive);
					if (!RangeContainsExclusiveEnd(Statement.Location, StmtEnd, Position))
					{
						break;
					}
					const auto& ForStmt = static_cast<const FForStmt&>(Statement);
					Stack.emplace_back();
					const auto FirstLocation = [](const auto& Values) -> FSourceLocation
					{
						for (const auto& Value : Values)
						{
							if (Value)
							{
								return Value->Location;
							}
						}
						return {};
					};
					const FSourceLocation FirstInitExtra = FirstLocation(ForStmt.InitExpressions);
					const FSourceLocation FirstStepExtra = FirstLocation(ForStmt.StepExpressions);
					const FSourceLocation InitEnd = FirstInitExtra.Line != 0
					    ? FirstInitExtra
					    : (ForStmt.Condition
					              ? ForStmt.Condition->Location
					              : (ForStmt.Step
					                        ? ForStmt.Step->Location
					                        : (FirstStepExtra.Line != 0
					                                  ? FirstStepExtra
					                                  : (ForStmt.Body ? ForStmt.Body->Location : StmtEnd))));
					if (ForStmt.Init && LocationAtOrBefore(ForStmt.Init->Location, Position))
					{
						WalkStmtForLocals(*ForStmt.Init, InitEnd, Position, Stack);
					}
					for (std::size_t Index = 0; Index < ForStmt.InitExpressions.size(); ++Index)
					{
						const std::unique_ptr<FExpr>& InitExpression =
						    ForStmt.InitExpressions[Index];
						if (!InitExpression)
						{
							continue;
						}
						FSourceLocation ExpressionEnd = ForStmt.Condition
						    ? ForStmt.Condition->Location
						    : (ForStmt.Step
						              ? ForStmt.Step->Location
						              : (FirstStepExtra.Line != 0
						                        ? FirstStepExtra
						                        : (ForStmt.Body ? ForStmt.Body->Location : StmtEnd)));
						for (std::size_t Next = Index + 1;
						    Next < ForStmt.InitExpressions.size(); ++Next)
						{
							if (ForStmt.InitExpressions[Next])
							{
								ExpressionEnd = ForStmt.InitExpressions[Next]->Location;
								break;
							}
						}
						if (WalkExprForLocals(
						        *InitExpression, ExpressionEnd, Position, Stack))
						{
							return;
						}
					}
					if (ForStmt.Condition)
					{
						const FSourceLocation CondEnd = ForStmt.Step
						    ? ForStmt.Step->Location
						    : (FirstStepExtra.Line != 0
						              ? FirstStepExtra
						              : (ForStmt.Body ? ForStmt.Body->Location : StmtEnd));
						if (WalkExprForLocals(*ForStmt.Condition, CondEnd, Position, Stack))
						{
							break;
						}
					}
					if (ForStmt.Step)
					{
						const FSourceLocation StepEnd = FirstStepExtra.Line != 0
						    ? FirstStepExtra
						    : (ForStmt.Body ? ForStmt.Body->Location : StmtEnd);
						if (WalkExprForLocals(*ForStmt.Step, StepEnd, Position, Stack))
						{
							break;
						}
					}
					for (std::size_t Index = 0; Index < ForStmt.StepExpressions.size(); ++Index)
					{
						const std::unique_ptr<FExpr>& StepExpression =
						    ForStmt.StepExpressions[Index];
						if (!StepExpression)
						{
							continue;
						}
						FSourceLocation ExpressionEnd =
						    ForStmt.Body ? ForStmt.Body->Location : StmtEnd;
						for (std::size_t Next = Index + 1;
						    Next < ForStmt.StepExpressions.size(); ++Next)
						{
							if (ForStmt.StepExpressions[Next])
							{
								ExpressionEnd = ForStmt.StepExpressions[Next]->Location;
								break;
							}
						}
						if (WalkExprForLocals(
						        *StepExpression, ExpressionEnd, Position, Stack))
						{
							return;
						}
					}
					if (ForStmt.Body && RangeContainsExclusiveEnd(ForStmt.Body->Location, StmtEnd, Position))
					{
						WalkBlockForLocals(*ForStmt.Body, Position, Stack, StmtEnd);
					}
					break;
				}
			case EStmtKind::ForEach:
				{
					const FSourceLocation StmtEnd = ExclusiveEndForStmt(Statement, EndExclusive);
					if (!RangeContainsExclusiveEnd(Statement.Location, StmtEnd, Position))
					{
						break;
					}
					const auto& ForEachStmt = static_cast<const FForEachStmt&>(Statement);
					Stack.emplace_back();
					// The range expression is evaluated before the iteration bindings
					// exist. Keep outer same-name variables visible in that expression.
					if (ForEachStmt.Body && LocationAtOrBefore(ForEachStmt.Body->Location, Position))
					{
						std::string KeyTypeName = ForEachStmt.KeyTypeName;
						std::string ElementTypeName = ForEachStmt.ElementTypeName;
						if ((KeyTypeName == "auto" || ElementTypeName == "auto") && ForEachStmt.Collection)
						{
							std::string CollectionType;
							if (TryResolveCollectionExprType(
							        Stack,
							        *ForEachStmt.Collection,
							        CollectionType))
							{
								(void)InferForeachBindingTypes(
								    CollectionType,
								    ForEachStmt.bHasKeyBinding,
								    KeyTypeName,
								    ElementTypeName);
							}
						}
						if (ForEachStmt.bHasKeyBinding && !ForEachStmt.KeyName.empty())
						{
							FSymbolQueryHit KeyHit;
							KeyHit.Kind = ForEachStmt.bKeyIsConst
							    ? EDocumentSymbolKind::Constant
							    : EDocumentSymbolKind::Variable;
							KeyHit.Name = ForEachStmt.KeyName;
							std::ostringstream KeyDetail;
							if (ForEachStmt.bKeyIsConst)
							{
								KeyDetail << "const ";
							}
							KeyDetail << KeyTypeName;
							KeyHit.Detail = KeyDetail.str();
							KeyHit.FileLabel.assign(
							    ForEachStmt.Location.FileLabel.data(),
							    ForEachStmt.Location.FileLabel.size());
							KeyHit.DefLine = ForEachStmt.Location.Line;
							KeyHit.DefColumn = ForEachStmt.Location.Column;
							BindLocal(Stack, std::move(KeyHit));
						}
						if (!ForEachStmt.ElementName.empty())
						{
							FSymbolQueryHit Hit;
							Hit.Kind = ForEachStmt.bIsConst
							    ? EDocumentSymbolKind::Constant
							    : EDocumentSymbolKind::Variable;
							Hit.Name = ForEachStmt.ElementName;
							std::ostringstream Detail;
							if (ForEachStmt.bIsConst)
							{
								Detail << "const ";
							}
							Detail << ElementTypeName;
							if (ForEachStmt.bIsRef)
							{
								Detail << '&';
							}
							Hit.Detail = Detail.str();
							Hit.FileLabel.assign(
							    ForEachStmt.Location.FileLabel.data(),
							    ForEachStmt.Location.FileLabel.size());
							Hit.DefLine = ForEachStmt.Location.Line;
							Hit.DefColumn = ForEachStmt.Location.Column;
							BindLocal(Stack, std::move(Hit));
						}
					}
					if (ForEachStmt.Collection)
					{
						const FSourceLocation CollEnd = ForEachStmt.Body
						    ? ForEachStmt.Body->Location
						    : StmtEnd;
						if (WalkExprForLocals(*ForEachStmt.Collection, CollEnd, Position, Stack))
						{
							break;
						}
					}
					if (ForEachStmt.Body && RangeContainsExclusiveEnd(ForEachStmt.Body->Location, StmtEnd, Position))
					{
						WalkBlockForLocals(*ForEachStmt.Body, Position, Stack, StmtEnd);
					}
					break;
				}
			case EStmtKind::Switch:
				{
					const FSourceLocation StmtEnd = ExclusiveEndForStmt(Statement, EndExclusive);
					if (!RangeContainsExclusiveEnd(Statement.Location, StmtEnd, Position))
					{
						break;
					}
					const auto& SwitchStmt = static_cast<const FSwitchStmt&>(Statement);
					if (SwitchStmt.Discriminant)
					{
						const FSourceLocation DiscEnd = (!SwitchStmt.Cases.empty() && SwitchStmt.Cases.front().Body)
						    ? SwitchStmt.Cases.front().Body->Location
						    : StmtEnd;
						if (WalkExprForLocals(*SwitchStmt.Discriminant, DiscEnd, Position, Stack))
						{
							break;
						}
					}
					for (std::size_t Index = 0; Index < SwitchStmt.Cases.size(); ++Index)
					{
						const FSwitchCase& Case = SwitchStmt.Cases[Index];
						if (!Case.Body)
						{
							continue;
						}
						const FSourceLocation CaseEnd = (Index + 1 < SwitchStmt.Cases.size() && SwitchStmt.Cases[Index + 1].Body)
						    ? SwitchStmt.Cases[Index + 1].Body->Location
						    : StmtEnd;
						if (RangeContainsExclusiveEnd(Case.Body->Location, CaseEnd, Position))
						{
							WalkBlockForLocals(*Case.Body, Position, Stack, CaseEnd);
							break;
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

		void WalkBlockForLocals(
		    const FBlockStmt& Block,
		    const FTextPosition Position,
		    FScopeStack& Stack,
		    const FSourceLocation& BlockEndExclusive)
		{
			Stack.emplace_back();
			const std::vector<std::unique_ptr<FStmt>>& Statements = Block.Statements;
			for (std::size_t Index = 0; Index < Statements.size(); ++Index)
			{
				const FStmt* Statement = Statements[Index].get();
				if (!Statement)
				{
					continue;
				}

				if (CompareLocation(Statement->Location, Position) > 0)
				{
					break;
				}

				const FSourceLocation SiblingEnd = (Index + 1 < Statements.size() && Statements[Index + 1])
				    ? Statements[Index + 1]->Location
				    : BlockEndExclusive;
				const FSourceLocation StmtEnd = ExclusiveEndForStmt(*Statement, SiblingEnd);

				if (StatementCompletedBefore(StmtEnd, Position))
				{
					if (Statement->Kind == EStmtKind::VarDecl)
					{
						BindLocal(Stack, MakeVarHit(static_cast<const FVarDeclStmt&>(*Statement), Stack));
					}
					else if (Statement->Kind == EStmtKind::VarDeclGroup)
					{
						for (const std::unique_ptr<FVarDeclStmt>& Declaration :
						    static_cast<const FVarDeclGroupStmt&>(*Statement).Declarations)
						{
							if (Declaration)
							{
								BindLocal(Stack, MakeVarHit(*Declaration, Stack));
							}
						}
					}
					else if (Statement->Kind == EStmtKind::TypeAlias)
					{
						BindLocal(
						    Stack,
						    MakeTypeAliasHit(static_cast<const FTypeAliasStmt&>(*Statement)));
					}
					else if (Statement->Kind == EStmtKind::Label)
					{
						FSymbolQueryHit Hit;
						Hit.Kind = EDocumentSymbolKind::Label;
						Hit.Name = static_cast<const FLabelStmt&>(*Statement).Name;
						Hit.Detail = "label";
						Hit.FileLabel.assign(
						    Statement->Location.FileLabel.data(),
						    Statement->Location.FileLabel.size());
						Hit.DefLine = Statement->Location.Line;
						Hit.DefColumn = Statement->Location.Column;
						BindLocal(Stack, std::move(Hit));
					}
					continue;
				}

				if (Statement->Kind == EStmtKind::VarDecl)
				{
					const auto& VarDecl = static_cast<const FVarDeclStmt&>(*Statement);
					if (LocationAtOrBefore(Statement->Location, Position))
					{
						BindLocal(Stack, MakeVarHit(VarDecl, Stack));
					}
					if (VarDecl.Initializer && WalkExprForLocals(*VarDecl.Initializer, StmtEnd, Position, Stack, VarDecl.TypeName))
					{
						break;
					}
					continue;
				}
				if (Statement->Kind == EStmtKind::TypeAlias)
				{
					if (LocationAtOrBefore(Statement->Location, Position))
					{
						BindLocal(
						    Stack,
						    MakeTypeAliasHit(static_cast<const FTypeAliasStmt&>(*Statement)));
					}
					continue;
				}
				if (Statement->Kind == EStmtKind::Label)
				{
					if (LocationAtOrBefore(Statement->Location, Position))
					{
						FSymbolQueryHit Hit;
						Hit.Kind = EDocumentSymbolKind::Label;
						Hit.Name = static_cast<const FLabelStmt&>(*Statement).Name;
						Hit.Detail = "label";
						Hit.FileLabel.assign(
						    Statement->Location.FileLabel.data(),
						    Statement->Location.FileLabel.size());
						Hit.DefLine = Statement->Location.Line;
						Hit.DefColumn = Statement->Location.Column;
						BindLocal(Stack, std::move(Hit));
					}
					continue;
				}

				if (RangeContainsExclusiveEnd(Statement->Location, StmtEnd, Position))
				{
					WalkStmtForLocals(*Statement, SiblingEnd, Position, Stack);
					break;
				}
			}
		}

		bool BuildLocalScopeStack(
		    const FProgram& Program,
		    const FTextPosition Position,
		    FScopeStack& OutStack,
		    std::string& OutContainerName,
		    std::string& OutFileLabel)
		{
			std::vector<FCallableScope> Callables;
			const FCallableScope* Scope = FindEnclosingCallable(Program, Position, Callables);
			if (!Scope)
			{
				return false;
			}
			// Semantic-token requests already own their callable/range index. A
			// single-position query only needs a temporary literal cache while
			// walking its locals, without building another range index.
			std::optional<FCallableQueryScope> QueryScope;
			if (!ActiveCallableQueryScope || !ActiveCallableQueryScope->Matches(Program))
			{
				QueryScope.emplace(Program);
			}

			OutContainerName = Scope->ContainerName.empty()
			    ? Scope->Name
			    : (Scope->ContainerName + "." + Scope->Name);
			OutFileLabel = Scope->FileLabel;

			OutStack.clear();
			OutStack.emplace_back();
			if (Scope->Parameters)
			{
				std::size_t ParameterIndex = 0;
				for (const FFunctionParam& Param : (Scope->DefinitionParameters.empty() ? *Scope->Parameters : Scope->DefinitionParameters))
				{
					FSymbolQueryHit Hit;
					Hit.Kind = Param.bIsConst
					    ? EDocumentSymbolKind::Constant
					    : EDocumentSymbolKind::Variable;
					Hit.Name = Param.Name;
					std::ostringstream Detail;
					if (Param.bIsConst)
					{
						Detail << "const ";
					}
					if (Param.bIsVariadic)
					{
						Detail << Param.TypeName;
						if (Param.bIsRef)
						{
							Detail << '&';
						}
						Detail << "... (parameter)";
					}
					else
					{
						Detail << Param.TypeName;
						if (Param.bIsRef)
						{
							Detail << '&';
						}
						Detail << " (parameter)";
					}
					Hit.Detail = Detail.str();
					Hit.FileLabel = Scope->FileLabel;
					Hit.DefLine = Scope->StartLine;
					Hit.DefColumn = Scope->StartColumn;
					if (ParameterIndex < Scope->DefinitionParameterPositions.size() &&
					    Scope->DefinitionParameterPositions[ParameterIndex].Line != 0)
					{
						Hit.DefLine = Scope->DefinitionParameterPositions[ParameterIndex].Line;
						Hit.DefColumn = Scope->DefinitionParameterPositions[ParameterIndex].Column;
					}
					++ParameterIndex;
					BindLocal(OutStack, std::move(Hit));
				}
			}
			if (Scope->Body)
			{
				FSourceLocation CallableEnd = MaxLocation();
				if (Scope->EndLine != std::numeric_limits<std::uint32_t>::max())
				{
					CallableEnd.Line = Scope->EndLine;
					CallableEnd.Column = Scope->EndColumn;
				}
				const FSourceLocation BodyEnd =
				    ExclusiveEndForBlock(*Scope->Body, CallableEnd);
				WalkBlockForLocals(*Scope->Body, Position, OutStack, BodyEnd);
			}
			return true;
		}

		void RefineLocalDeclarationPosition(const FProgram& Program, FSymbolQueryHit& Hit)
		{
			const auto& Range = Hit.DeclarationRange;
			if (Range.SourceId.empty() || Range.Begin >= Range.End || Range.End - Range.Begin > 65536 || !Range.Position.Begin.Line || !Range.Position.Begin.Column)
			{
				return;
			}
			const auto* Map = ProgramPreprocessorLineMap(Program, Range.SourceId);
			if (!Map || Range.End > Map->PreprocessedSource.size()) return;
			const auto Header = std::string_view(Map->PreprocessedSource).substr(Range.Begin, Range.End - Range.Begin);
			FQueryLexer Lexer(Header, Range.SourceId);
			const auto& Tokens = Lexer.Tokenize();
			unsigned Brackets = 0;
			for (std::size_t Index = Tokens.size(); Index-- > 0;)
			{
				const auto& Token = Tokens[Index];
				if (Token.Lexeme == "]")
				{
					++Brackets;
					continue;
				}
				if (Token.Lexeme == "[" && Brackets)
				{
					--Brackets;
					continue;
				}
				if (Brackets || Token.Kind != ETokenKind::Identifier || Token.Lexeme != Hit.Name)
				{
					continue;
				}
				Hit.DefLine = Range.Position.Begin.Line + Token.Location.Line - 1;
				Hit.DefColumn = Token.Location.Column + (Token.Location.Line == 1 ? Range.Position.Begin.Column - 1 : 0);
				return;
			}
		}

		void CollectLocalHits(
		    const FProgram& Program,
		    const FTextPosition Position,
		    const std::string_view Ident,
		    std::vector<FSymbolQueryHit>& OutHits)
		{
			FScopeStack Stack;
			std::string ContainerName;
			std::string FileLabel;
			if (!BuildLocalScopeStack(Program, Position, Stack, ContainerName, FileLabel))
			{
				return;
			}

			const FSymbolQueryHit* Found = LookupLocal(Stack, Ident);
			if (!Found)
			{
				return;
			}

			FSymbolQueryHit Hit = *Found;
			RefineLocalDeclarationPosition(Program, Hit);
			Hit.ContainerName = ContainerName;
			if (Hit.FileLabel.empty())
			{
				Hit.FileLabel = FileLabel;
			}
			OutHits.push_back(std::move(Hit));
		}

		void CollectVisibleLocals(
		    const FProgram& Program,
		    const FTextPosition Position,
		    std::vector<FSymbolQueryHit>& OutHits)
		{
			FScopeStack Stack;
			std::string ContainerName;
			std::string FileLabel;
			if (!BuildLocalScopeStack(Program, Position, Stack, ContainerName, FileLabel))
			{
				return;
			}

			std::unordered_map<std::string, FSymbolQueryHit> Merged;
			for (std::size_t FrameIndex = 0; FrameIndex < Stack.size(); ++FrameIndex)
			{
				const auto& Frame = Stack[FrameIndex];
				for (const auto& Pair : Frame)
				{
					FSymbolQueryHit Hit = Pair.second;
					Hit.CompletionScopeDistance = static_cast<std::uint8_t>(
					    std::min<std::size_t>(Stack.size() - 1 - FrameIndex, 254u));
					Merged[Pair.first] = std::move(Hit);
				}
			}
			OutHits.reserve(OutHits.size() + Merged.size());
			for (auto& Pair : Merged)
			{
				FSymbolQueryHit Hit = std::move(Pair.second);
				Hit.ContainerName = ContainerName;
				if (Hit.FileLabel.empty())
				{
					Hit.FileLabel = FileLabel;
				}
				OutHits.push_back(std::move(Hit));
			}
		}

		std::string ResolveTypeNameWithLocals(
		    const FProgram& Program,
		    const std::vector<FSymbolQueryHit>& Locals,
		    const std::string_view TypeName)
		{
			const std::string Original(TypeName);
			std::string Current = StripGenericArgs(TypeName);
			std::string Expanded;
			bool bEverResolved = false;
			for (int Depth = 0; Depth < 8; ++Depth)
			{
				bool bStepped = false;
				for (const FSymbolQueryHit& Local : Locals)
				{
					if (Local.Kind != EDocumentSymbolKind::TypeAlias || Local.Name != Current)
					{
						continue;
					}
					// Keep full Detail spelling (`multicast_delegate<int>`) — do not strip args.
					Expanded = Local.Detail;
					Current = StripGenericArgs(Local.Detail);
					bStepped = true;
					bEverResolved = true;
					break;
				}
				if (!bStepped)
				{
					break;
				}
			}
			if (!bEverResolved)
			{
				return ResolveTypeAlias(Program, Original);
			}
			return ResolveTypeAlias(Program, Expanded);
		}
