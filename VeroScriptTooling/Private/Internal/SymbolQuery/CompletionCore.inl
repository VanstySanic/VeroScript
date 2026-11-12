// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

		bool NameMatchesSymbol(const std::string& SymbolName, const std::string_view Ident)
		{
			return SymbolNameMatchesIdent(SymbolName, Ident);
		}

		void CollectHits(
		    const std::vector<FDocumentSymbol>& Symbols,
		    const std::string_view Ident,
		    const std::string& ContainerName,
		    std::vector<FSymbolQueryHit>& OutHits,
		    const FTextPosition* const QueryPosition = nullptr,
		    const std::string_view QueryFileLabel = {},
		    const bool bDeclarationPositionOnly = false)
		{
			const auto AppendHit = [&](const FDocumentSymbol& Symbol, const std::string& Container,
			                           const bool bDeclarationOnly)
			{
				// Inclusive end matches FindQueryTokenAt / TokenCoversPositionOrEnd
				// (`Green|` still resolves the declaration).
				const auto NameSize = Symbol.TypeScope.SimpleName.empty() ? Symbol.Name.size() : Symbol.TypeScope.SimpleName.size();
				const std::uint32_t NameEndExclusive = Symbol.StartColumn + static_cast<std::uint32_t>(NameSize);
				const bool bSourceDeclaration = QueryPosition && Symbol.FileLabel == QueryFileLabel && Symbol.StartLine == QueryPosition->Line && QueryPosition->Column >= Symbol.StartColumn && QueryPosition->Column <= NameEndExclusive;
				const bool bAtDeclaration = !bDeclarationOnly || bSourceDeclaration;
				const bool ScopedName = Symbol.TypeScope.bLocal || !Symbol.TypeScope.OwnerTypeName.empty();
				const bool ExactType = ScopedName && (Ident == Symbol.TypeScope.QualifiedName || Ident.substr(0, Symbol.TypeScope.QualifiedName.size() + 1) == Symbol.TypeScope.QualifiedName + "<");
				if (bAtDeclaration && (!ScopedName || ExactType || bSourceDeclaration) && NameMatchesSymbol(Symbol.Name, Ident))
				{
					FSymbolQueryHit Hit;
					Hit.Kind = Symbol.Kind;
					Hit.bTypeDefinition = Symbol.TypeScope.bLocal && Symbol.Kind != EDocumentSymbolKind::TypeAlias;
					Hit.Name = Symbol.Name;
					Hit.Detail = Symbol.Detail;
					Hit.Documentation = Symbol.Documentation;
					Hit.bHasSourceDocumentation = Symbol.bHasSourceDocumentation;
					Hit.ContainerName = Container;
					Hit.FileLabel = Symbol.FileLabel;
					Hit.DefLine = Symbol.StartLine;
					Hit.DefColumn = Symbol.StartColumn;
					Hit.bExported = Symbol.bExported;
					Hit.bSoftBuiltin = Symbol.bSoftBuiltin;
					Hit.bReadOnly = Symbol.bReadOnly;
					Hit.SourceImplementation = Symbol.SourceImplementation;
					OutHits.push_back(std::move(Hit));
				}
			};
			if (ActiveSymbolLookupIndex && ContainerName.empty() && ActiveSymbolLookupIndex->Matches(Symbols) && FSymbolLookupIndex::Supports(Ident))
			{
				if (const auto* Entries = ActiveSymbolLookupIndex->Find(Ident))
				{
					std::size_t Work = 0;
					for (const auto& Entry : *Entries)
					{
						if ((++Work & 127u) == 0)
						{
							CheckAnalysisCancellation();
						}
						AppendHit(*Entry.Symbol, Entry.Container ? *Entry.Container : ContainerName,
						    bDeclarationPositionOnly || Entry.bDeclarationPositionOnly);
					}
				}
				return;
			}
			for (const FDocumentSymbol& Symbol : Symbols)
			{
				AppendHit(Symbol, ContainerName, bDeclarationPositionOnly);

				if (Symbol.Children.empty())
				{
					continue;
				}
				// `enum class` members only participate in bare lookup at their own
				// declarations. Qualified lookup/completion uses the type receiver.
				const bool bScopedEnum =
				    Symbol.Kind == EDocumentSymbolKind::Enum && Symbol.bScopedEnum;
				CollectHits(
				    Symbol.Children,
				    Ident,
				    Symbol.Name,
				    OutHits,
				    QueryPosition,
				    QueryFileLabel,
				    bDeclarationPositionOnly || bScopedEnum);
			}
		}

		std::string CompletionLabel(const std::string& SymbolName)
		{
			const std::size_t Generic = SymbolName.find('<');
			if (Generic == std::string::npos)
			{
				return SymbolName;
			}
			return SymbolName.substr(0, Generic);
		}

		std::size_t GenericTypeArityFromSymbolName(const std::string_view SymbolName)
		{
			const std::size_t Open = SymbolName.find('<');
			if (Open == std::string_view::npos)
			{
				return 0;
			}
			std::size_t Depth = 0;
			std::size_t Count = 0;
			bool bHasArgument = false;
			for (std::size_t Index = Open + 1; Index < SymbolName.size(); ++Index)
			{
				const char Character = SymbolName[Index];
				if (Character == '<')
				{
					++Depth;
					bHasArgument = true;
				}
				else if (Character == '>')
				{
					if (Depth == 0)
					{
						return bHasArgument ? Count + 1 : 0;
					}
					--Depth;
				}
				else if (Character == ',' && Depth == 0)
				{
					++Count;
				}
				else if (!std::isspace(static_cast<unsigned char>(Character)))
				{
					bHasArgument = true;
				}
			}
			return 0;
		}

		std::string MakeGenericTypeSnippet(
		    const std::string_view Name,
		    const std::size_t Arity)
		{
			std::string Result(Name);
			Result.push_back('<');
			for (std::size_t Index = 0; Index < Arity; ++Index)
			{
				if (Index > 0)
				{
					Result += ", ";
				}
				Result += "${" + std::to_string(Index + 1) + "}";
			}
			Result.push_back('>');
			return Result;
		}

		bool IsInstantiableGenericTypeSymbol(const EDocumentSymbolKind Kind)
		{
			return Kind == EDocumentSymbolKind::Class || Kind == EDocumentSymbolKind::Struct || Kind == EDocumentSymbolKind::Interface || Kind == EDocumentSymbolKind::TypeAlias;
		}

		enum class ECompletionMatchQuality : std::uint8_t
		{
			Exact = 0,
			ExactIgnoreCase = 1,
			Prefix = 2,
			WordBoundary = 3,
			Contains = 4,
			Subsequence = 5,
			NoQuery = 6,
			NoMatch = 7,
		};

		char FoldCompletionCharacter(const char Character)
		{
			return static_cast<char>(
			    std::tolower(static_cast<unsigned char>(Character)));
		}

		bool CompletionEqualIgnoreCase(
		    const std::string_view Left,
		    const std::string_view Right)
		{
			return Left.size() == Right.size() && std::equal(Left.begin(), Left.end(), Right.begin(), [](const char L, const char R)
			{
				return FoldCompletionCharacter(L) == FoldCompletionCharacter(R);
			});
		}

		bool CompletionStartsWithIgnoreCase(
		    const std::string_view Text,
		    const std::string_view Prefix)
		{
			return Text.size() >= Prefix.size() && CompletionEqualIgnoreCase(Text.substr(0, Prefix.size()), Prefix);
		}

		bool CompletionContainsIgnoreCase(
		    const std::string_view Text,
		    const std::string_view Needle)
		{
			if (Needle.size() > Text.size())
			{
				return false;
			}
			for (std::size_t Index = 0; Index + Needle.size() <= Text.size(); ++Index)
			{
				if (CompletionEqualIgnoreCase(Text.substr(Index, Needle.size()), Needle))
				{
					return true;
				}
			}
			return false;
		}

		bool CompletionWordBoundaryMatches(
		    const std::string_view Text,
		    const std::string_view Prefix)
		{
			std::size_t PrefixIndex = 0;
			for (std::size_t Index = 0; Index < Text.size(); ++Index)
			{
				const unsigned char Character = static_cast<unsigned char>(Text[Index]);
				const bool bBoundary = Index == 0 || std::isalnum(static_cast<unsigned char>(Text[Index - 1])) == 0 || (std::islower(static_cast<unsigned char>(Text[Index - 1])) != 0 && std::isupper(Character) != 0);
				if (bBoundary && std::isalnum(Character) != 0)
				{
					if (CompletionStartsWithIgnoreCase(Text.substr(Index), Prefix))
					{
						return true;
					}
					if (PrefixIndex < Prefix.size() && FoldCompletionCharacter(Text[Index]) == FoldCompletionCharacter(Prefix[PrefixIndex]))
					{
						++PrefixIndex;
					}
				}
			}
			return !Prefix.empty() && PrefixIndex == Prefix.size();
		}

		ECompletionMatchQuality CompletionMatchQualityForText(
		    const std::string_view Text,
		    const std::string_view Prefix)
		{
			if (Prefix.empty())
			{
				return ECompletionMatchQuality::NoQuery;
			}
			if (Text == Prefix)
			{
				return ECompletionMatchQuality::Exact;
			}
			if (CompletionEqualIgnoreCase(Text, Prefix))
			{
				return ECompletionMatchQuality::ExactIgnoreCase;
			}
			if (CompletionStartsWithIgnoreCase(Text, Prefix))
			{
				return ECompletionMatchQuality::Prefix;
			}
			if (CompletionWordBoundaryMatches(Text, Prefix) || FuzzyIdentifierPrefixMatches(Text, Prefix))
			{
				return ECompletionMatchQuality::WordBoundary;
			}
			if (CompletionContainsIgnoreCase(Text, Prefix))
			{
				return ECompletionMatchQuality::Contains;
			}
			std::size_t PrefixIndex = 0;
			for (const char Character : Text)
			{
				if (PrefixIndex < Prefix.size() && FoldCompletionCharacter(Character) == FoldCompletionCharacter(Prefix[PrefixIndex]))
				{
					++PrefixIndex;
				}
			}
			return PrefixIndex == Prefix.size()
			    ? ECompletionMatchQuality::Subsequence
			    : ECompletionMatchQuality::NoMatch;
		}

		ECompletionMatchQuality CompletionMatchQualityForLabel(
		    const std::string_view Label,
		    const std::string_view Prefix)
		{
			const std::size_t Generic = Label.find('<');
			const std::string_view Simple = Label.substr(0, Generic);
			ECompletionMatchQuality Best = CompletionMatchQualityForText(Simple, Prefix);
			const std::size_t Dot = Simple.rfind('.');
			if (Dot != std::string_view::npos && Dot + 1 < Simple.size())
			{
				Best = (std::min)(Best,
				    CompletionMatchQualityForText(Simple.substr(Dot + 1), Prefix));
			}
			return Best;
		}

		bool PrefixMatches(const std::string& Label, const std::string_view Prefix)
		{
			return CompletionMatchQualityForLabel(Label, Prefix) != ECompletionMatchQuality::NoMatch;
		}

		/**
		 * Completion list ranking context. SortPriority is derived from kind + context
		 * so expression sites prefer locals over types/keywords, member sites prefer
		 * fields/methods, etc.
		 */
		enum class ECompletionRankContext : std::uint8_t
		{
			General = 0,
			TopLevel,
			TypeBody,
			Statement,
			TypeName,
			Expression,
			DeclaratorName,
			Member,
			NamespaceLeaf,
			GotoLabel,
			Capture,
			ImportPath,
			Preprocessor,
		};

		bool IsTypeLikeCompletionKind(const EDocumentSymbolKind Kind)
		{
			return Kind == EDocumentSymbolKind::Class || Kind == EDocumentSymbolKind::Struct || Kind == EDocumentSymbolKind::Interface || Kind == EDocumentSymbolKind::Enum || Kind == EDocumentSymbolKind::TypeAlias;
		}

		bool IsNamespaceCompletionItem(const FCompletionItem& Item)
		{
			return Item.Kind == EDocumentSymbolKind::Namespace;
		}

		bool CompletionItemShouldOffer(
		    const FCompletionItem& Item,
		    const ECompletionRankContext Context)
		{
			if (Item.bOverrideMember)
			{
				return Context == ECompletionRankContext::TypeBody;
			}
			if (Item.Kind == EDocumentSymbolKind::Destructor)
			{
				return false;
			}
			if (Context == ECompletionRankContext::Expression && ((Item.Kind == EDocumentSymbolKind::Import && !IsNamespaceCompletionItem(Item)) || Item.Kind == EDocumentSymbolKind::Annotation || Item.Kind == EDocumentSymbolKind::AnnotationArgument))
			{
				return false;
			}
			if (Context == ECompletionRankContext::TopLevel || Context == ECompletionRankContext::TypeBody)
			{
				// A bare callable/value cannot begin a declaration. Keep declaration
				// keywords, type names, annotations and macros; a macro may expand to
				// a declaration and therefore remains valid in either scope.
				return IsTypeLikeCompletionKind(Item.Kind) || IsNamespaceCompletionItem(Item) || Item.Kind == EDocumentSymbolKind::Keyword || Item.Kind == EDocumentSymbolKind::Annotation || Item.Kind == EDocumentSymbolKind::Macro;
			}
			if (Context == ECompletionRankContext::Statement)
			{
				return (Item.Kind != EDocumentSymbolKind::Import || IsNamespaceCompletionItem(Item)) && Item.Kind != EDocumentSymbolKind::Annotation && Item.Kind != EDocumentSymbolKind::AnnotationArgument && Item.Kind != EDocumentSymbolKind::Label;
			}
			if (Context == ECompletionRankContext::TypeName)
			{
				return IsTypeLikeCompletionKind(Item.Kind) || IsNamespaceCompletionItem(Item) || Item.Kind == EDocumentSymbolKind::Keyword || Item.Kind == EDocumentSymbolKind::Macro;
			}
			if (Context == ECompletionRankContext::DeclaratorName)
			{
				// Existing symbols are not useful while the user is naming a new
				// declaration (`FVector Ta|`). Do not offer a callable that would
				// turn the declaration into invalid syntax.
				return false;
			}
			if (Context == ECompletionRankContext::Member && Item.Kind == EDocumentSymbolKind::Keyword)
			{
				return false;
			}
			if (Context == ECompletionRankContext::GotoLabel && Item.Kind != EDocumentSymbolKind::Label)
			{
				return false;
			}
			return true;
		}

		std::uint16_t ComputeCompletionSortPriority(
		    const FCompletionItem& Item,
		    const ECompletionRankContext Context)
		{
			if (Item.bOverrideMember)
			{
				return 0;
			}
			const std::uint16_t Soft = Item.bSoftBuiltin ? 5u : 0u;
			// Macros remain available in expression contexts, but rank below
			// real constants, locals, and functions.
			const std::uint16_t Macro =
			    (Item.Kind == EDocumentSymbolKind::Macro || Item.Detail.rfind("#define ", 0) == 0)
			    ? 15u
			    : 0u;
			const auto TypeLikePriority = [&]() -> std::uint16_t
			{
				switch (Item.Kind)
				{
				case EDocumentSymbolKind::Class:
				case EDocumentSymbolKind::Struct:
				case EDocumentSymbolKind::Interface:
				case EDocumentSymbolKind::Enum:
					return 70u + Soft;
				case EDocumentSymbolKind::TypeAlias:
					return 75u + Soft;
				default:
					return 80u + Soft;
				}
			};

			switch (Context)
			{
			case ECompletionRankContext::TopLevel:
			case ECompletionRankContext::TypeBody:
				{
					switch (Item.Kind)
					{
					case EDocumentSymbolKind::Annotation:
					case EDocumentSymbolKind::Macro:
						return 10u + Soft + Macro;
					case EDocumentSymbolKind::Keyword:
						return 18u + Soft;
					case EDocumentSymbolKind::Class:
					case EDocumentSymbolKind::Struct:
					case EDocumentSymbolKind::Interface:
					case EDocumentSymbolKind::Enum:
					case EDocumentSymbolKind::TypeAlias:
						return 30u + Soft;
					case EDocumentSymbolKind::Import:
					case EDocumentSymbolKind::Namespace:
						return 34u + Soft;
					default:
						return 80u + Soft + Macro;
					}
				}
			case ECompletionRankContext::Statement:
				{
					switch (Item.Kind)
					{
					case EDocumentSymbolKind::Variable:
						return 10u + Soft + Macro;
					case EDocumentSymbolKind::Keyword:
						return 14u + Soft;
					case EDocumentSymbolKind::Field:
						return 18u + Soft + Macro;
					case EDocumentSymbolKind::Constant:
					case EDocumentSymbolKind::EnumMember:
					case EDocumentSymbolKind::Macro:
						return 22u + Soft + Macro;
					case EDocumentSymbolKind::Function:
					case EDocumentSymbolKind::Method:
						return 28u + Soft + Macro;
					case EDocumentSymbolKind::Import:
					case EDocumentSymbolKind::Namespace:
						return 32u + Soft;
					case EDocumentSymbolKind::Class:
					case EDocumentSymbolKind::Struct:
					case EDocumentSymbolKind::Interface:
					case EDocumentSymbolKind::Enum:
					case EDocumentSymbolKind::TypeAlias:
						return 35u + Soft;
					default:
						return 55u + Soft + Macro;
					}
				}
			case ECompletionRankContext::TypeName:
				{
					if (IsTypeLikeCompletionKind(Item.Kind))
					{
						return 10u + Soft;
					}
					if (Item.Kind == EDocumentSymbolKind::Keyword)
					{
						return 15u + Soft;
					}
					if (Item.Kind == EDocumentSymbolKind::Import || Item.Kind == EDocumentSymbolKind::Namespace)
					{
						return 12u + Soft;
					}
					return 25u + Soft + Macro;
				}
			case ECompletionRankContext::Member:
				{
					switch (Item.Kind)
					{
					case EDocumentSymbolKind::Field:
						return 10u + Soft + Macro;
					case EDocumentSymbolKind::Constant:
					case EDocumentSymbolKind::EnumMember:
					case EDocumentSymbolKind::Macro:
						return 15u + Soft + Macro;
					case EDocumentSymbolKind::Method:
						return 20u + Soft + Macro;
					case EDocumentSymbolKind::Constructor:
						return 25u + Soft + Macro;
					default:
						return 40u + Soft + Macro;
					}
				}
			case ECompletionRankContext::NamespaceLeaf:
				{
					switch (Item.Kind)
					{
					case EDocumentSymbolKind::Import:
					case EDocumentSymbolKind::Namespace:
						return 5u + Soft;
					case EDocumentSymbolKind::Class:
					case EDocumentSymbolKind::Struct:
					case EDocumentSymbolKind::Interface:
					case EDocumentSymbolKind::Enum:
					case EDocumentSymbolKind::TypeAlias:
						return 10u + Soft + Macro;
					case EDocumentSymbolKind::Function:
					case EDocumentSymbolKind::Annotation:
					case EDocumentSymbolKind::Constant:
					case EDocumentSymbolKind::Variable:
					case EDocumentSymbolKind::Macro:
						return 20u + Soft + Macro;
					default:
						return 40u + Soft + Macro;
					}
				}
			case ECompletionRankContext::Capture:
				{
					if (Item.Label == "=")
					{
						return 5u;
					}
					if (Item.Kind == EDocumentSymbolKind::Variable)
					{
						return 10u + Soft + Macro;
					}
					if (Item.Kind == EDocumentSymbolKind::Constant)
					{
						return 20u + Soft + Macro;
					}
					return 50u + Soft + Macro;
				}
			case ECompletionRankContext::GotoLabel:
				return Item.Kind == EDocumentSymbolKind::Label ? 10u : 50u;
			case ECompletionRankContext::ImportPath:
				return Item.Kind == EDocumentSymbolKind::Import ? 10u : 20u;
			case ECompletionRankContext::Preprocessor:
				// Directives first; macros (#define …) next.
				return Item.Kind == EDocumentSymbolKind::Keyword ? 10u : 20u + Soft + Macro;
			case ECompletionRankContext::Expression:
				{
					switch (Item.Kind)
					{
					case EDocumentSymbolKind::Variable:
						return 10u + Soft + Macro;
					case EDocumentSymbolKind::Field:
						return 16u + Soft + Macro;
					case EDocumentSymbolKind::Constant:
					case EDocumentSymbolKind::EnumMember:
					case EDocumentSymbolKind::Macro:
						return 22u + Soft + Macro;
					case EDocumentSymbolKind::Function:
					case EDocumentSymbolKind::Method:
					case EDocumentSymbolKind::Annotation:
						return 30u + Soft + Macro;
					case EDocumentSymbolKind::Constructor:
						return 35u + Soft + Macro;
					case EDocumentSymbolKind::Import:
					case EDocumentSymbolKind::Namespace:
						return 38u + Soft;
					case EDocumentSymbolKind::Keyword:
						return 48u + Soft;
					case EDocumentSymbolKind::Class:
					case EDocumentSymbolKind::Struct:
					case EDocumentSymbolKind::Interface:
					case EDocumentSymbolKind::Enum:
					case EDocumentSymbolKind::TypeAlias:
						return TypeLikePriority() + 10u;
					default:
						return 55u + Soft + Macro;
					}
				}
			case ECompletionRankContext::DeclaratorName:
				// Keywords already suppressed; caret is naming a binding (`int foo|`),
				// so prefer value-like identifiers — same band as General, not types-first.
				{
					switch (Item.Kind)
					{
					case EDocumentSymbolKind::Variable:
						return 10u + Soft + Macro;
					case EDocumentSymbolKind::Field:
						return 18u + Soft + Macro;
					case EDocumentSymbolKind::Constant:
					case EDocumentSymbolKind::EnumMember:
					case EDocumentSymbolKind::Macro:
						return 24u + Soft + Macro;
					case EDocumentSymbolKind::Function:
					case EDocumentSymbolKind::Method:
					case EDocumentSymbolKind::Annotation:
						return 35u + Soft + Macro;
					case EDocumentSymbolKind::Class:
					case EDocumentSymbolKind::Struct:
					case EDocumentSymbolKind::Interface:
					case EDocumentSymbolKind::Enum:
					case EDocumentSymbolKind::TypeAlias:
						return TypeLikePriority();
					default:
						return 60u + Soft + Macro;
					}
				}
			case ECompletionRankContext::General:
			default:
				{
					switch (Item.Kind)
					{
					case EDocumentSymbolKind::Variable:
						return 10u + Soft + Macro;
					case EDocumentSymbolKind::Field:
						return 18u + Soft + Macro;
					case EDocumentSymbolKind::Constant:
					case EDocumentSymbolKind::EnumMember:
					case EDocumentSymbolKind::Macro:
						return 24u + Soft + Macro;
					case EDocumentSymbolKind::Label:
						return 28u;
					case EDocumentSymbolKind::Function:
					case EDocumentSymbolKind::Method:
					case EDocumentSymbolKind::Annotation:
						return 35u + Soft + Macro;
					case EDocumentSymbolKind::Constructor:
						return 40u + Soft + Macro;
					case EDocumentSymbolKind::Class:
					case EDocumentSymbolKind::Struct:
					case EDocumentSymbolKind::Interface:
					case EDocumentSymbolKind::Enum:
					case EDocumentSymbolKind::TypeAlias:
						return TypeLikePriority();
					case EDocumentSymbolKind::Keyword:
						return 85u;
					case EDocumentSymbolKind::Import:
					case EDocumentSymbolKind::Namespace:
						return 50u + Soft;
					default:
						return 60u + Soft + Macro;
					}
				}
			}
		}

		void FinalizeCompletionItems(
		    std::vector<FCompletionItem>& Items,
		    const ECompletionRankContext Context,
		    const std::string_view Prefix)
		{
			std::size_t Kept = 0;
			for (std::size_t Index = 0; Index < Items.size(); ++Index)
			{
				if ((Index & 127u) == 0)
				{
					CheckAnalysisCancellation();
				}
				auto& Item = Items[Index];
				if (!CompletionItemShouldOffer(Item, Context))
				{
					continue;
				}
				const auto Quality = CompletionMatchQualityForLabel(Item.Label, Prefix);
				if (Quality == ECompletionMatchQuality::NoMatch)
				{
					continue;
				}
				Item.MatchQuality = static_cast<std::uint8_t>(Quality);
				Item.SortPriority = ComputeCompletionSortPriority(Item, Context);
				if (Item.ScopeDistance != 255)
				{
					const std::uint16_t Distance =
					    std::min<std::uint16_t>(Item.ScopeDistance, 6u);
					if (Item.Kind == EDocumentSymbolKind::Variable || Item.Kind == EDocumentSymbolKind::Constant)
					{
						// Locals and parameters are value candidates regardless of
						// constness. Prefer the innermost active lexical scope while
						// keeping every visible outer binding available.
						Item.SortPriority = 10u + Distance;
					}
					else if (Item.Kind == EDocumentSymbolKind::TypeAlias)
					{
						// Preserve the rank band selected by the syntactic context, then
						// place visible local aliases just ahead of non-local types in that
						// band. Distance still orders nested aliases deterministically.
						constexpr std::uint16_t LocalTypeBias = 7u;
						Item.SortPriority = Item.SortPriority > LocalTypeBias
						    ? Item.SortPriority - LocalTypeBias + Distance
						    : Distance;
					}
				}
				if (Kept != Index)
				{
					Items[Kept] = std::move(Item);
				}
				++Kept;
			}
			Items.resize(Kept);
			std::size_t SortWork = 0;
			std::sort(
			    Items.begin(),
			    Items.end(),
			    [&SortWork](const FCompletionItem& Left, const FCompletionItem& Right)
			{
				if ((++SortWork & 127u) == 0)
				{
					CheckAnalysisCancellation();
				}
				if (Left.MatchQuality != Right.MatchQuality)
				{
					return Left.MatchQuality < Right.MatchQuality;
				}
				if (Left.SortPriority != Right.SortPriority)
				{
					return Left.SortPriority < Right.SortPriority;
				}
				if (Left.Label != Right.Label)
				{
					return Left.Label < Right.Label;
				}
				if (Left.Detail != Right.Detail)
				{
					return Left.Detail < Right.Detail;
				}
				return Left.InsertText < Right.InsertText;
			});
		}

		bool IsHostFormatFamilyName(const std::string_view Name)
		{
			return Name == "print" || Name == "printf" || Name == "format" || Name == "formatf";
		}

		bool IsCallableCompletionKind(const EDocumentSymbolKind Kind)
		{
			return Kind == EDocumentSymbolKind::Function || Kind == EDocumentSymbolKind::Method || Kind == EDocumentSymbolKind::Constructor;
		}

		bool FindSourceOffset(
		    std::string_view Source,
		    std::uint32_t Line,
		    std::uint32_t Column,
		    std::size_t& OutOffset);

		/**
		 * A completion text-edit replaces the identifier but leaves an already-typed
		 * '<' immediately after it in place.  Suppress the generic snippet in that
		 * case so `pair<` never becomes `pair<...><`.
		 */
		void SuppressGenericSnippetBeforeExistingAngle(
		    const std::string_view Source,
		    const FCompletionResult& Result,
		    std::unordered_map<std::string, FCompletionItem>& Items)
		{
			if (!Result.bHasReplaceRange)
			{
				return;
			}
			std::size_t Offset = 0;
			if (!FindSourceOffset(
			        Source,
			        Result.ReplaceLine,
			        Result.ReplaceEndColumn,
			        Offset))
			{
				return;
			}
			while (Offset < Source.size() && (Source[Offset] == ' ' || Source[Offset] == '\t'))
			{
				++Offset;
			}
			if (Offset >= Source.size() || Source[Offset] != '<')
			{
				return;
			}
			for (auto& Pair : Items)
			{
				FCompletionItem& Item = Pair.second;
				if (!Item.bInsertTextSnippet || Item.InsertText.rfind(Item.Label + "<", 0) != 0)
				{
					continue;
				}
				Item.InsertText = Item.Label;
				Item.bInsertTextSnippet = false;
			}
		}

		bool HasOnlyIndentBeforeOffset(
		    const std::string_view Source,
		    const std::size_t Offset)
		{
			for (std::size_t Index = std::min(Offset, Source.size()); Index > 0;)
			{
				const char Ch = Source[--Index];
				if (Ch == '\n' || Ch == '\r')
				{
					break;
				}
				if (Ch != ' ' && Ch != '\t')
				{
					return false;
				}
			}
			return true;
		}

		bool IsInsideExecutableBrace(
		    const std::string_view Source,
		    const FTextPosition Position)
		{
			FQueryLexer Lexer(Source, "<annotation-context>");
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			std::vector<bool> ExecutableBraceStack;
			std::size_t SegmentStart = 0;
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (Token.Location.Line > Position.Line || (Token.Location.Line == Position.Line && Token.Location.Column >= Position.Column))
				{
					break;
				}
				if (Token.Kind != ETokenKind::Symbol)
				{
					continue;
				}
				if (Token.Lexeme == "{")
				{
					bool bDeclarationContainer = false;
					for (std::size_t Probe = SegmentStart; Probe < Index; ++Probe)
					{
						if (Tokens[Probe].Kind == ETokenKind::Keyword && (Tokens[Probe].Lexeme == "class" || Tokens[Probe].Lexeme == "struct" || Tokens[Probe].Lexeme == "interface" || Tokens[Probe].Lexeme == "namespace" || Tokens[Probe].Lexeme == "enum"))
						{
							bDeclarationContainer = true;
							break;
						}
					}
					const bool bParentExecutable =
					    !ExecutableBraceStack.empty() && ExecutableBraceStack.back();
					ExecutableBraceStack.push_back(
					    bParentExecutable || !bDeclarationContainer);
					SegmentStart = Index + 1;
				}
				else if (Token.Lexeme == "}")
				{
					if (!ExecutableBraceStack.empty())
					{
						ExecutableBraceStack.pop_back();
					}
					SegmentStart = Index + 1;
				}
				else if (Token.Lexeme == ";" && (ExecutableBraceStack.empty() || !ExecutableBraceStack.back()))
				{
					SegmentStart = Index + 1;
				}
			}
			return !ExecutableBraceStack.empty() && ExecutableBraceStack.back();
		}

		bool IsDeclarationAnnotationCompletionSite(
		    const std::string_view Source,
		    const FTextPosition Position)
		{
			if (IsInsideExecutableBrace(Source, Position))
			{
				return false;
			}
			std::size_t CaretOffset = 0;
			if (!FindSourceOffset(Source, Position.Line, Position.Column, CaretOffset))
			{
				return false;
			}
			std::size_t Start = CaretOffset;
			while (Start > 0)
			{
				const unsigned char Ch = static_cast<unsigned char>(Source[Start - 1]);
				if (!std::isalnum(Ch) && Ch != '_')
				{
					break;
				}
				--Start;
			}
			if (Start > 0 && Source[Start - 1] == '@')
			{
				--Start;
			}
			return HasOnlyIndentBeforeOffset(Source, Start);
		}

		void AppendCompletionFromSymbols(
		    const std::vector<FDocumentSymbol>& Symbols,
		    const std::string_view Prefix,
		    const bool bIncludeAnnotations,
		    const std::string_view ActiveNamespace,
		    std::unordered_map<std::string, FCompletionItem>& OutItems,
		    const bool bTypeNameContext = false)
		{
			const auto EmplaceSymbol =
			    [&OutItems, Prefix, bIncludeAnnotations, ActiveNamespace, bTypeNameContext](
			        const FDocumentSymbol& Symbol)
			{
				std::string Label = CompletionLabel(Symbol.Name);
				// Qualified declarations are reached through namespace-member
				// completion. Offering `std.abs` for a bare `std` prefix hides the
				// actual namespace item and inserts a spelling that bypasses the
				// normal segment-by-segment completion flow.
				if (Label.find('.') != std::string::npos || Label.find("::") != std::string::npos)
				{
					const auto NormalizeSeparators = [](std::string Value)
					{
						std::size_t Scope = 0;
						while ((Scope = Value.find("::", Scope)) != std::string::npos)
						{
							Value.replace(Scope, 2, ".");
							++Scope;
						}
						return Value;
					};
					const std::string Normalized = NormalizeSeparators(Label);
					const std::string ScopePrefix = ActiveNamespace.empty()
					    ? std::string{}
					    : NormalizeSeparators(std::string(ActiveNamespace)) + ".";
					if (ScopePrefix.empty() || Normalized.rfind(ScopePrefix, 0) != 0)
					{
						return;
					}
					Label = Normalized.substr(ScopePrefix.size());
					if (Label.empty() || Label.find('.') != std::string::npos)
					{
						return;
					}
				}
				// `@name` annotations are never reachable from a bare identifier prefix;
				// CompleteAnnotationsAtSigil owns that spelling.
				if (!Label.empty() && Label.front() == '@')
				{
					return;
				}
				if (!bIncludeAnnotations && IsAnnotationSymbol(Symbol))
				{
					return;
				}
				if (!PrefixMatches(Label, Prefix) || Label.empty())
				{
					return;
				}
				const std::size_t GenericArity =
				    bTypeNameContext && IsInstantiableGenericTypeSymbol(Symbol.Kind)
				    ? GenericTypeArityFromSymbolName(Symbol.Name)
				    : 0;
				const auto ApplyGenericSnippet = [&Label, GenericArity](FCompletionItem& Item)
				{
					if (GenericArity > 0)
					{
						Item.InsertText = MakeGenericTypeSnippet(Label, GenericArity);
						Item.bInsertTextSnippet = true;
					}
				};
				// Use the complete signature as a callable key from the first item.
				// This also deduplicates the third and later identical overloads;
				// relying on a name-only first key lost that case.
				const std::string ItemKey = IsCallableCompletionKind(Symbol.Kind)
				    ? Label + '\x1f' + Symbol.Detail
				    : Label;
				auto Existing = OutItems.find(ItemKey);
				if (Existing != OutItems.end())
				{
					// One item per signature. Prefer the source declaration's
					// documentation when host and source expose the same callable.
					if (!Symbol.bSoftBuiltin && Existing->second.bSoftBuiltin)
					{
						FCompletionItem Item;
						Item.Kind = Symbol.Kind;
						Item.Label = Label;
						Item.Detail = Symbol.Detail;
						Item.Documentation = Symbol.Documentation;
						Item.InsertText = Label;
						ApplyGenericSnippet(Item);
						Item.bSoftBuiltin = Symbol.bSoftBuiltin;
						Existing->second = std::move(Item);
					}
				}
				else
				{
					FCompletionItem Item;
					Item.Kind = Symbol.Kind;
					Item.Label = Label;
					Item.Detail = Symbol.Detail;
					Item.Documentation = Symbol.Documentation;
					Item.InsertText = Label;
					ApplyGenericSnippet(Item);
					Item.bSoftBuiltin = Symbol.bSoftBuiltin;
					OutItems.emplace(ItemKey, std::move(Item));
				}
			};

			// Top-level only (plus unscoped enum members). Qualified members belong
			// after '.'; labels after `goto`; nested typedefs from CollectVisibleLocals.
			// Recursing into Children used to flood identifier completion with soft-type
			// methods/fields and bury entry `main` / user functions under Take(100).
			for (const FDocumentSymbol& Symbol : Symbols)
			{
				if (Symbol.TypeScope.bLocal || !Symbol.TypeScope.OwnerTypeName.empty())
				{
					continue;
				}
				EmplaceSymbol(Symbol);
				if (Symbol.Kind == EDocumentSymbolKind::Enum && !Symbol.bScopedEnum)
				{
					for (const FDocumentSymbol& Child : Symbol.Children)
					{
						if (Child.Kind == EDocumentSymbolKind::EnumMember)
						{
							EmplaceSymbol(Child);
						}
					}
				}
			}
		}

		void AppendLabelCompletions(
		    const std::vector<FDocumentSymbol>& Symbols,
		    const std::string_view Prefix,
		    std::unordered_map<std::string, FCompletionItem>& OutItems)
		{
			for (const FDocumentSymbol& Symbol : Symbols)
			{
				if (Symbol.Kind == EDocumentSymbolKind::Label && PrefixMatches(Symbol.Name, Prefix) && !Symbol.Name.empty())
				{
					FCompletionItem Item;
					Item.Kind = EDocumentSymbolKind::Label;
					Item.Label = Symbol.Name;
					Item.Detail = Symbol.Detail.empty() ? "label" : Symbol.Detail;
					Item.InsertText = Symbol.Name;
					Item.bSoftBuiltin = Symbol.bSoftBuiltin;
					OutItems.insert_or_assign(Symbol.Name, std::move(Item));
				}
				if (!Symbol.Children.empty())
				{
					AppendLabelCompletions(Symbol.Children, Prefix, OutItems);
				}
			}
		}

		/**
		 * Language words from GetLanguageKeywords(). Primitive type spellings are
		 * keywords in both completion and semantic tokens; container spellings are
		 * named types. Reserved primitive words also replace any stale soft type
		 * candidate, including its insert text and snippet state.
		 *
		 * Mode filters statement/type keywords out of expression and declarator-name
		 * sites (e.g. `for (int i = 0; i|)` / `for (int i|)`).
		 */
		enum class EKeywordCompletionMode : std::uint8_t
		{
			// Compatibility/default mode used when the surrounding syntactic
			// context cannot be classified more narrowly.
			All,
			TopLevel,
			TypeBody,
			Statement,
			TypeName,
			Expression,
			DeclaratorName,
			// Function-like declarations may end in `= default` / `= delete`.
			DeclarationAssignment,
		};

		bool IsTypeNameKeyword(const std::string_view Lexeme)
		{
			return VeroScript::IsTypeKeyword(Lexeme) || VeroScript::IsContainerTypeKeyword(Lexeme) || Lexeme == "const" || Lexeme == "constexpr" || Lexeme == "ref" || Lexeme == "fn" || Lexeme == "function";
		}

		bool IsKeywordAllowedForMode(
		    const std::string_view Lexeme,
		    const EKeywordCompletionMode Mode)
		{
			if (Mode == EKeywordCompletionMode::All)
			{
				return true;
			}
			if (Mode == EKeywordCompletionMode::DeclaratorName)
			{
				return false;
			}
			if (Mode == EKeywordCompletionMode::DeclarationAssignment)
			{
				return Lexeme == "default" || Lexeme == "delete";
			}
			if (Mode == EKeywordCompletionMode::TypeName)
			{
				return IsTypeNameKeyword(Lexeme);
			}
			if (Mode == EKeywordCompletionMode::TopLevel)
			{
				return IsTypeNameKeyword(Lexeme) || Lexeme == "class" || Lexeme == "struct" || Lexeme == "interface" || Lexeme == "enum" || Lexeme == "import" || Lexeme == "export" || Lexeme == "using" || Lexeme == "typedef" || Lexeme == "namespace" || Lexeme == "template" || Lexeme == "static";
			}
			if (Mode == EKeywordCompletionMode::TypeBody)
			{
				return IsTypeNameKeyword(Lexeme) || Lexeme == "class" || Lexeme == "struct" || Lexeme == "interface" || Lexeme == "enum" || Lexeme == "using" || Lexeme == "typedef" || Lexeme == "public" || Lexeme == "protected" || Lexeme == "private" || Lexeme == "static" || Lexeme == "virtual" || Lexeme == "explicit" || Lexeme == "override" || Lexeme == "final" || Lexeme == "default" || Lexeme == "operator";
			}
			if (Mode == EKeywordCompletionMode::Statement)
			{
				// Statement starts may also begin declarations or expressions.
				return IsTypeNameKeyword(Lexeme) || Lexeme == "if" || Lexeme == "else" || Lexeme == "while" || Lexeme == "do" || Lexeme == "for" || Lexeme == "switch" || Lexeme == "case" || Lexeme == "default" || Lexeme == "return" || Lexeme == "break" || Lexeme == "continue" || Lexeme == "fallthrough" || Lexeme == "goto" || Lexeme == "new" || Lexeme == "this" || Lexeme == "static" || Lexeme == "using" || Lexeme == "typedef" || Lexeme == "true" || Lexeme == "false" || Lexeme == "nullptr";
			}

			// Expression: value / allocation keywords only.
			// `in` stays on Statement (and for-init foreach after a binding name).
			return Lexeme == "true" || Lexeme == "false" || Lexeme == "nullptr" || Lexeme == "this" || Lexeme == "new";
		}

		bool IsDeclaratorTypeKeyword(const std::string_view Lexeme)
		{
			return VeroScript::IsTypeKeyword(Lexeme) || VeroScript::IsContainerTypeKeyword(Lexeme);
		}

		bool TokenEndsAtOrBefore(
		    const FToken& Token,
		    const FTextPosition Position)
		{
			if (Token.Location.Line < Position.Line)
			{
				return true;
			}
			if (Token.Location.Line > Position.Line)
			{
				return false;
			}
			const std::uint32_t EndExclusive =
			    Token.Location.Column + static_cast<std::uint32_t>(Token.Lexeme.size());
			return EndExclusive <= Position.Column;
		}

		/**
		 * If caret sits inside `for ( ... )`, return how many top-level `;` appear
		 * between the opening `(` and the caret (0 = init, 1 = condition, 2 = increment).
		 */
		bool TryGetForHeaderSemicolonCountBefore(
		    const std::vector<FToken>& Tokens,
		    const FTextPosition Position,
		    std::size_t& OutSemicolons)
		{
			OutSemicolons = 0;
			std::size_t LastBefore = Tokens.size();
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				if (Tokens[Index].Kind == ETokenKind::EndOfFile)
				{
					break;
				}
				if (TokenEndsAtOrBefore(Tokens[Index], Position))
				{
					LastBefore = Index;
				}
				else
				{
					break;
				}
			}
			if (LastBefore >= Tokens.size())
			{
				return false;
			}

			int Depth = 0;
			std::size_t ForOpen = Tokens.size();
			for (std::size_t Index = LastBefore + 1; Index > 0; --Index)
			{
				const std::size_t At = Index - 1;
				const FToken& Token = Tokens[At];
				if (Token.Kind != ETokenKind::Symbol)
				{
					continue;
				}
				if (Token.Lexeme == ")")
				{
					++Depth;
					continue;
				}
				if (Token.Lexeme == "(")
				{
					if (Depth > 0)
					{
						--Depth;
						continue;
					}
					if (At > 0 && Tokens[At - 1].Kind == ETokenKind::Keyword && Tokens[At - 1].Lexeme == "for")
					{
						ForOpen = At;
					}
					break;
				}
			}
			if (ForOpen >= Tokens.size())
			{
				return false;
			}

			int InnerDepth = 0;
			std::size_t Semicolons = 0;
			for (std::size_t Index = ForOpen + 1; Index <= LastBefore; ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (Token.Kind != ETokenKind::Symbol)
				{
					continue;
				}
				if (Token.Lexeme == "(" || Token.Lexeme == "[" || Token.Lexeme == "{")
				{
					++InnerDepth;
					continue;
				}
				if (Token.Lexeme == ")" || Token.Lexeme == "]" || Token.Lexeme == "}")
				{
					if (InnerDepth > 0)
					{
						--InnerDepth;
					}
					continue;
				}
				if (InnerDepth == 0 && Token.Lexeme == ";")
				{
					++Semicolons;
				}
			}
			OutSemicolons = Semicolons;
			return true;
		}

		int AngleCloseLevels(const std::string_view Lexeme)
		{
			if (Lexeme == ">")
			{
				return 1;
			}
			if (Lexeme == ">>")
			{
				return 2;
			}
			return 0;
		}

		/**
		 * Type/container keywords always head a generic. Identifiers only when `<`
		 * is glued (`Foo<` not `x <`) so comparisons stay Expression.
		 */
		bool IsGenericTypeHeadBeforeAngle(
		    const std::vector<FToken>& Tokens,
		    const std::size_t AngleIndex)
		{
			if (AngleIndex == 0 || AngleIndex >= Tokens.size())
			{
				return false;
			}
			const FToken& Head = Tokens[AngleIndex - 1];
			const FToken& Angle = Tokens[AngleIndex];
			if (Angle.Kind != ETokenKind::Symbol || Angle.Lexeme != "<")
			{
				return false;
			}
			if (Head.Kind == ETokenKind::Keyword)
			{
				return VeroScript::IsTypeKeyword(Head.Lexeme) || VeroScript::IsContainerTypeKeyword(Head.Lexeme);
			}
			if (Head.Kind != ETokenKind::Identifier)
			{
				return false;
			}
			return Head.Location.Line == Angle.Location.Line && Head.Location.Column + static_cast<std::uint32_t>(Head.Lexeme.size()) == Angle.Location.Column;
		}

		// True when Tokens[CloseIndex] (`>` / `>>`) closes a `TypeHead<...>` list.
		bool ClosesTypeArgumentList(
		    const std::vector<FToken>& Tokens,
		    const std::size_t CloseIndex,
		    const bool bAllowValueArguments = false,
		    std::optional<FSourceLocation>* ValueHead = nullptr)
		{
			if (CloseIndex >= Tokens.size() || Tokens[CloseIndex].Kind != ETokenKind::Symbol)
			{
				return false;
			}
			int Depth = AngleCloseLevels(Tokens[CloseIndex].Lexeme);
			if (Depth <= 0)
			{
				return false;
			}
			int ParenDepth = 0, BracketDepth = 0;

			for (std::size_t Index = CloseIndex; Index > 0; --Index)
			{
				const std::size_t At = Index - 1;
				const FToken& Token = Tokens[At];
				// Literal arguments also occur in comparisons. Only the loaded-program
				// path (or a fail-closed fallback) may classify these as a declaration;
				// the loaded path resolves the head before suppressing expression items.
				if (Token.Kind == ETokenKind::IntegerLiteral || Token.Kind == ETokenKind::FloatLiteral || Token.Kind == ETokenKind::BooleanLiteral || Token.Kind == ETokenKind::NullptrLiteral || Token.Kind == ETokenKind::StringLiteral || Token.Kind == ETokenKind::WStringLiteral || Token.Kind == ETokenKind::CharacterLiteral)
				{
					if (!bAllowValueArguments) return false;
				}
				if (Token.Kind != ETokenKind::Symbol)
				{
					continue;
				}
				if (bAllowValueArguments)
				{
					if (Token.Lexeme == ")")
					{
						++ParenDepth;
						continue;
					}
					if (Token.Lexeme == "]")
					{
						++BracketDepth;
						continue;
					}
					if (Token.Lexeme == "(" && ParenDepth > 0)
					{
						--ParenDepth;
						continue;
					}
					if (Token.Lexeme == "[" && BracketDepth > 0)
					{
						--BracketDepth;
						continue;
					}
					if (ParenDepth > 0 || BracketDepth > 0)
					{
						continue;
					}
				}
				if (Token.Lexeme == ";" || Token.Lexeme == "{" || Token.Lexeme == "}") return false;
				if (const int Close = AngleCloseLevels(Token.Lexeme); Close > 0)
				{
					Depth += Close;
					continue;
				}
				if (Token.Lexeme == "<")
				{
					--Depth;
					if (Depth == 0)
					{
						if (!IsGenericTypeHeadBeforeAngle(Tokens, At) && (!bAllowValueArguments || At == 0 || Tokens[At - 1].Kind != ETokenKind::Identifier))
						{
							return false;
						}
						if (ValueHead)
						{
							*ValueHead = Tokens[At - 1].Location;
						}
						return true;
					}
				}
			}
			return false;
		}

		/**
		 * Angle-depth counting only `<` that follow a real generic head
		 * (keyword type/container, or glued ident), so `a < b` stays a comparison.
		 */
		bool IsInsideTypeArgumentList(
		    const std::vector<FToken>& Tokens,
		    const std::size_t LastIndexInclusive)
		{
			if (LastIndexInclusive >= Tokens.size())
			{
				return false;
			}

			int Depth = 0, ParenDepth = 0, BracketDepth = 0;
			for (std::size_t Index = 0; Index <= LastIndexInclusive; ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (Token.Kind != ETokenKind::Symbol)
				{
					continue;
				}
				// A type argument list cannot cross a statement or block boundary.
				// Glued comparisons (`i<2`) otherwise leave stale angle depth in
				// the following loop body and suppress its local variables.
				if (Token.Lexeme == ";" || Token.Lexeme == "{" || Token.Lexeme == "}")
				{
					Depth = ParenDepth = BracketDepth = 0;
					continue;
				}
				if (Depth > 0)
				{
					// Parenthesized value arguments may contain comparisons or shifts.
					// Their angles do not open or close the enclosing type application.
					if (Token.Lexeme == "(")
					{
						++ParenDepth;
						continue;
					}
					if (Token.Lexeme == "[")
					{
						++BracketDepth;
						continue;
					}
					if (Token.Lexeme == ")" && ParenDepth > 0)
					{
						--ParenDepth;
						continue;
					}
					if (Token.Lexeme == "]" && BracketDepth > 0)
					{
						--BracketDepth;
						continue;
					}
					if (ParenDepth > 0 || BracketDepth > 0)
					{
						continue;
					}
				}
				if (Token.Lexeme == "<" && IsGenericTypeHeadBeforeAngle(Tokens, Index))
				{
					++Depth;
				}
				else if (const int Close = AngleCloseLevels(Token.Lexeme); Close > 0 && Depth > 0)
				{
					Depth = Depth > Close ? Depth - Close : 0;
				}
			}
			return Depth > 0;
		}

		enum class EStructuralCompletionScope : std::uint8_t
		{
			TopLevel,
			TypeBody,
			Executable,
		};

		EStructuralCompletionScope ClassifyStructuralCompletionScope(
		    const std::vector<FToken>& Tokens,
		    const FTextPosition Position)
		{
			enum class EBraceScope : std::uint8_t
			{
				Namespace,
				Type,
				Executable,
			};
			std::vector<EBraceScope> Stack;
			std::size_t SegmentStart = 0;
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				const FToken& Token = Tokens[Index];
				if (!TokenEndsAtOrBefore(Token, Position))
				{
					break;
				}
				if (Token.Kind != ETokenKind::Symbol)
				{
					continue;
				}
				if (Token.Lexeme == "{")
				{
					bool bNamespace = false;
					bool bType = false;
					for (std::size_t Probe = SegmentStart; Probe < Index; ++Probe)
					{
						if (Tokens[Probe].Kind != ETokenKind::Keyword)
						{
							continue;
						}
						bNamespace = bNamespace || Tokens[Probe].Lexeme == "namespace";
						bType = bType || Tokens[Probe].Lexeme == "class" || Tokens[Probe].Lexeme == "struct" || Tokens[Probe].Lexeme == "interface" || Tokens[Probe].Lexeme == "enum";
					}
					Stack.push_back(
					    bNamespace
					        ? EBraceScope::Namespace
					        : (bType ? EBraceScope::Type : EBraceScope::Executable));
					SegmentStart = Index + 1;
				}
				else if (Token.Lexeme == "}")
				{
					if (!Stack.empty())
					{
						Stack.pop_back();
					}
					SegmentStart = Index + 1;
				}
				else if (Token.Lexeme == ";")
				{
					SegmentStart = Index + 1;
				}
			}

			if (Stack.empty() || Stack.back() == EBraceScope::Namespace)
			{
				return EStructuralCompletionScope::TopLevel;
			}
			return Stack.back() == EBraceScope::Type
			    ? EStructuralCompletionScope::TypeBody
			    : EStructuralCompletionScope::Executable;
		}

		EKeywordCompletionMode ModeForStructuralScope(
		    const EStructuralCompletionScope Scope)
		{
			switch (Scope)
			{
			case EStructuralCompletionScope::TopLevel:
				return EKeywordCompletionMode::TopLevel;
			case EStructuralCompletionScope::TypeBody:
				return EKeywordCompletionMode::TypeBody;
			case EStructuralCompletionScope::Executable:
			default:
				return EKeywordCompletionMode::Statement;
			}
		}

		bool SegmentContainsKeywordBefore(
		    const std::vector<FToken>& Tokens,
		    const std::size_t EndExclusive,
		    const std::initializer_list<std::string_view> Keywords)
		{
			for (std::size_t Index = std::min(EndExclusive, Tokens.size()); Index > 0; --Index)
			{
				const FToken& Token = Tokens[Index - 1];
				if (Token.Kind == ETokenKind::Symbol && (Token.Lexeme == "{" || Token.Lexeme == "}" || Token.Lexeme == ";"))
				{
					break;
				}
				if (Token.Kind != ETokenKind::Keyword)
				{
					continue;
				}
				for (const std::string_view Keyword : Keywords)
				{
					if (Token.Lexeme == Keyword)
					{
						return true;
					}
				}
			}
			return false;
		}

		bool LooksLikeIdentifierDeclarator(
		    const std::vector<FToken>& Tokens,
		    const std::size_t TypeIndex)
		{
			if (TypeIndex >= Tokens.size() || (Tokens[TypeIndex].Kind != ETokenKind::Identifier && Tokens[TypeIndex].Kind != ETokenKind::Keyword))
			{
				return false;
			}
			if (Tokens[TypeIndex].Kind == ETokenKind::Keyword && !IsDeclaratorTypeKeyword(Tokens[TypeIndex].Lexeme))
			{
				return false;
			}
			if (TypeIndex == 0)
			{
				return true;
			}
			const FToken& BeforeType = Tokens[TypeIndex - 1];
			return (BeforeType.Kind == ETokenKind::Symbol && (BeforeType.Lexeme == "{" || BeforeType.Lexeme == "}" || BeforeType.Lexeme == ";" || BeforeType.Lexeme == ":" || BeforeType.Lexeme == "(" || BeforeType.Lexeme == ",")) || (BeforeType.Kind == ETokenKind::Keyword && (BeforeType.Lexeme == "const" || BeforeType.Lexeme == "constexpr" || BeforeType.Lexeme == "ref" || BeforeType.Lexeme == "static" || BeforeType.Lexeme == "virtual"));
		}

		bool IsPreviouslyDeclaredTypeName(
		    const std::vector<FToken>& Tokens,
		    const std::size_t TypeIndex)
		{
			if (TypeIndex >= Tokens.size() || Tokens[TypeIndex].Kind != ETokenKind::Identifier)
			{
				return false;
			}
			const std::string_view Name = Tokens[TypeIndex].Lexeme;
			for (std::size_t Index = 1; Index < TypeIndex; ++Index)
			{
				if (Tokens[Index].Kind != ETokenKind::Identifier || Tokens[Index].Lexeme != Name || Tokens[Index - 1].Kind != ETokenKind::Keyword)
				{
					continue;
				}
				const std::string_view Head = Tokens[Index - 1].Lexeme;
				if (Head == "class" || Head == "struct" || Head == "interface" || Head == "enum" || Head == "using" || Head == "typedef")
				{
					return true;
				}
			}
			return false;
		}

		EKeywordCompletionMode ClassifyAfterPreviousToken(
		    const std::vector<FToken>& Tokens,
		    const std::size_t PrevIndex,
		    const EKeywordCompletionMode StructuralMode,
		    const bool bAllowValueArguments = false,
		    std::optional<FSourceLocation>* ValueHead = nullptr)
		{
			if (PrevIndex >= Tokens.size())
			{
				return StructuralMode;
			}

			const FToken& Prev = Tokens[PrevIndex];
			if (Prev.Kind == ETokenKind::Keyword)
			{
				if (IsDeclaratorTypeKeyword(Prev.Lexeme))
				{
					return EKeywordCompletionMode::DeclaratorName;
				}
				if (Prev.Lexeme == "const" || Prev.Lexeme == "constexpr" || Prev.Lexeme == "ref" || Prev.Lexeme == "static" || Prev.Lexeme == "fn" || Prev.Lexeme == "function" || Prev.Lexeme == "new" || Prev.Lexeme == "typedef")
				{
					return EKeywordCompletionMode::TypeName;
				}
				if (Prev.Lexeme == "class" || Prev.Lexeme == "struct" || Prev.Lexeme == "interface" || Prev.Lexeme == "enum" || Prev.Lexeme == "namespace")
				{
					return EKeywordCompletionMode::DeclaratorName;
				}
				if (Prev.Lexeme == "return" || Prev.Lexeme == "case" || Prev.Lexeme == "in")
				{
					return EKeywordCompletionMode::Expression;
				}
				return StructuralMode;
			}
			if (Prev.Kind == ETokenKind::Identifier && LooksLikeIdentifierDeclarator(Tokens, PrevIndex))
			{
				return EKeywordCompletionMode::DeclaratorName;
			}
			if (Prev.Kind == ETokenKind::Symbol && AngleCloseLevels(Prev.Lexeme) > 0)
			{
				// `array<int> |` / `Foo<T> n|` — not `x > t|`.
				return ClosesTypeArgumentList(Tokens, PrevIndex, bAllowValueArguments, ValueHead)
				    ? EKeywordCompletionMode::DeclaratorName
				    : EKeywordCompletionMode::Expression;
			}
			if (Prev.Kind != ETokenKind::Symbol)
			{
				return StructuralMode;
			}

			const std::string_view Op = Prev.Lexeme;
			if (bAllowValueArguments && Op == "&")
			{
				std::size_t TypeEnd = PrevIndex;
				while (TypeEnd > 0 && Tokens[TypeEnd].Lexeme == "&")
				{
					--TypeEnd;
				}
				if (ClosesTypeArgumentList(Tokens, TypeEnd, true, ValueHead))
				{
					return EKeywordCompletionMode::DeclaratorName;
				}
			}
			if (Op == "<")
			{
				// `array<|` / `Foo<i|` need type keywords; `x <|` is comparison.
				return IsGenericTypeHeadBeforeAngle(Tokens, PrevIndex)
				    ? EKeywordCompletionMode::TypeName
				    : EKeywordCompletionMode::Expression;
			}
			if (Op == ",")
			{
				// `map<int, |` → types; call/arg `,` → expression.
				if (IsInsideTypeArgumentList(Tokens, PrevIndex) || SegmentContainsKeywordBefore(Tokens, PrevIndex, {"class", "struct", "interface"}))
				{
					return EKeywordCompletionMode::TypeName;
				}
				return EKeywordCompletionMode::Expression;
			}
			if (Op == "(")
			{
				// `for (|` needs type keywords; call `( |` is expression.
				if (PrevIndex > 0 && Tokens[PrevIndex - 1].Kind == ETokenKind::Keyword && Tokens[PrevIndex - 1].Lexeme == "for")
				{
					return EKeywordCompletionMode::Statement;
				}
				return EKeywordCompletionMode::Expression;
			}
			if (Op == ":" && PrevIndex > 0 && Tokens[PrevIndex - 1].Kind == ETokenKind::Keyword && (Tokens[PrevIndex - 1].Lexeme == "public" || Tokens[PrevIndex - 1].Lexeme == "protected" || Tokens[PrevIndex - 1].Lexeme == "private"))
			{
				return StructuralMode;
			}
			if (Op == ":" && SegmentContainsKeywordBefore(Tokens, PrevIndex, {"class", "struct", "interface"}))
			{
				return EKeywordCompletionMode::TypeName;
			}
			if (Op == "=" && SegmentContainsKeywordBefore(Tokens, PrevIndex, {"using"}))
			{
				return EKeywordCompletionMode::TypeName;
			}
			if (Op == "=" && PrevIndex > 0 && Tokens[PrevIndex - 1].Kind == ETokenKind::Symbol && Tokens[PrevIndex - 1].Lexeme == ")")
			{
				return EKeywordCompletionMode::DeclarationAssignment;
			}
			if (Op == "=" || Op == "+=" || Op == "-=" || Op == "*=" || Op == "/=" || Op == "%=" || Op == "==" || Op == "!=" || Op == "<=" || Op == ">=" || Op == "+" || Op == "-" || Op == "*" || Op == "/" || Op == "%" || Op == "&&" || Op == "||" || Op == "!" || Op == "?" || Op == ":" || Op == "[")
			{
				return EKeywordCompletionMode::Expression;
			}
			if (Op == "{" || Op == "}" || Op == ";")
			{
				return StructuralMode;
			}
			return StructuralMode;
		}

		EKeywordCompletionMode ClassifyKeywordCompletionMode(
		    const std::string_view Source,
		    const std::string_view FileLabel,
		    const FTextPosition Position,
		    const bool bAllowValueArguments = false,
		    std::optional<FSourceLocation>* ValueHead = nullptr)
		{
			FQueryLexer Lexer(Source, FileLabel);
			const std::vector<FToken>& Tokens = Lexer.Tokenize();
			const EKeywordCompletionMode StructuralMode = ModeForStructuralScope(
			    ClassifyStructuralCompletionScope(Tokens, Position));
			auto FindLastBefore = [&]() -> std::size_t
			{
				std::size_t LastBefore = Tokens.size();
				for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
				{
					if (Tokens[Index].Kind == ETokenKind::EndOfFile)
					{
						break;
					}
					if (TokenEndsAtOrBefore(Tokens[Index], Position))
					{
						LastBefore = Index;
					}
					else
					{
						break;
					}
				}
				return LastBefore;
			};
			// A declaration can continue on the next line. Treat an identifier at
			// that line's start as the name after `class`, `int`, `Foo<T>`, etc.
			auto ModeAtLineStart = [&]() -> EKeywordCompletionMode
			{
				const std::size_t LastBefore = FindLastBefore();
				if (LastBefore >= Tokens.size())
				{
					return StructuralMode;
				}
				std::size_t PrevIndex = LastBefore;
				const FToken& Last = Tokens[LastBefore];
				if ((Last.Kind == ETokenKind::Identifier || Last.Kind == ETokenKind::Keyword) && Last.Location.Line == Position.Line && Position.Column >= Last.Location.Column && Position.Column <= Last.Location.Column + static_cast<std::uint32_t>(Last.Lexeme.size()))
				{
					if (LastBefore == 0)
					{
						return StructuralMode;
					}
					PrevIndex = LastBefore - 1;
				}
				// A bare identifier can also be an expression or a macro invocation.
				// Continue a declarator only when that identifier names a type already
				// declared in this document and sits at a possible type position.
				if (Tokens[PrevIndex].Kind == ETokenKind::Identifier && (!LooksLikeIdentifierDeclarator(Tokens, PrevIndex) || !IsPreviouslyDeclaredTypeName(Tokens, PrevIndex)))
				{
					return StructuralMode;
				}
				const EKeywordCompletionMode Continued =
				    ClassifyAfterPreviousToken(Tokens, PrevIndex, StructuralMode, bAllowValueArguments, ValueHead);
				return Continued == EKeywordCompletionMode::DeclaratorName
				    ? Continued
				    : StructuralMode;
			};
			std::size_t CaretLineOffset = 0;
			if (FindSourceOffset(Source, Position.Line, Position.Column, CaretLineOffset) && HasOnlyIndentBeforeOffset(Source, CaretLineOffset))
			{
				return ModeAtLineStart();
			}

			std::size_t ForSemicolons = 0;
			if (TryGetForHeaderSemicolonCountBefore(Tokens, Position, ForSemicolons))
			{
				// for-init: type/declarator or expression RHS; condition/increment: expression.
				if (ForSemicolons == 0)
				{
					// `for (int i|` / `for (int |` — name after a type keyword.
					const std::size_t LastBefore = FindLastBefore();
					if (LastBefore < Tokens.size())
					{
						const FToken& Prev = Tokens[LastBefore];
						const bool bPrevIsCaretIdent =
						    (Prev.Kind == ETokenKind::Identifier || Prev.Kind == ETokenKind::Keyword) && Prev.Location.Line == Position.Line && Position.Column >= Prev.Location.Column && Position.Column <= Prev.Location.Column + static_cast<std::uint32_t>(Prev.Lexeme.size());
						if (bPrevIsCaretIdent)
						{
							if (LastBefore > 0)
							{
								const FToken& BeforeIdent = Tokens[LastBefore - 1];
								if (BeforeIdent.Kind == ETokenKind::Keyword && IsDeclaratorTypeKeyword(BeforeIdent.Lexeme))
								{
									return EKeywordCompletionMode::DeclaratorName;
								}
								if (BeforeIdent.Kind == ETokenKind::Symbol && AngleCloseLevels(BeforeIdent.Lexeme) > 0 && ClosesTypeArgumentList(Tokens, LastBefore - 1, bAllowValueArguments, ValueHead))
								{
									return EKeywordCompletionMode::DeclaratorName;
								}
							}
							// `for (i|` — still decl-ish; keep All.
							return EKeywordCompletionMode::Statement;
						}
						if (Prev.Kind == ETokenKind::Keyword && IsDeclaratorTypeKeyword(Prev.Lexeme))
						{
							return EKeywordCompletionMode::DeclaratorName;
						}
						if (Prev.Kind == ETokenKind::Symbol && AngleCloseLevels(Prev.Lexeme) > 0 && ClosesTypeArgumentList(Tokens, LastBefore, bAllowValueArguments, ValueHead))
						{
							return EKeywordCompletionMode::DeclaratorName;
						}
						// `for (int i = |` / `for (int i = t|` — expression RHS.
						return ClassifyAfterPreviousToken(
						    Tokens, LastBefore, EKeywordCompletionMode::Statement, bAllowValueArguments, ValueHead);
					}
					return EKeywordCompletionMode::Statement;
				}
				return EKeywordCompletionMode::Expression;
			}

			const std::size_t LastBefore = FindLastBefore();
			if (LastBefore >= Tokens.size())
			{
				return StructuralMode;
			}

			std::size_t PrevIndex = LastBefore;
			const FToken& Last = Tokens[LastBefore];
			const bool bLastIsCaretIdent =
			    (Last.Kind == ETokenKind::Identifier || Last.Kind == ETokenKind::Keyword) && Last.Location.Line == Position.Line && Position.Column >= Last.Location.Column && Position.Column <= Last.Location.Column + static_cast<std::uint32_t>(Last.Lexeme.size());
			if (bLastIsCaretIdent)
			{
				std::size_t CaretOffset = 0;
				if (FindSourceOffset(Source, Position.Line, Position.Column, CaretOffset))
				{
					std::size_t PrefixStart = CaretOffset;
					while (PrefixStart > 0)
					{
						const unsigned char Ch =
						    static_cast<unsigned char>(Source[PrefixStart - 1]);
						if (!std::isalnum(Ch) && Ch != '_')
						{
							break;
						}
						--PrefixStart;
					}
					if (HasOnlyIndentBeforeOffset(Source, PrefixStart))
					{
						return ModeAtLineStart();
					}
				}
				if (LastBefore == 0)
				{
					return StructuralMode;
				}
				if (IsInsideTypeArgumentList(Tokens, LastBefore - 1))
				{
					return EKeywordCompletionMode::TypeName;
				}
				PrevIndex = LastBefore - 1;
			}

			return ClassifyAfterPreviousToken(Tokens, PrevIndex, StructuralMode, bAllowValueArguments, ValueHead);
		}

		ECompletionRankContext RankContextFromKeywordMode(const EKeywordCompletionMode Mode)
		{
			switch (Mode)
			{
			case EKeywordCompletionMode::TopLevel:
				return ECompletionRankContext::TopLevel;
			case EKeywordCompletionMode::TypeBody:
				return ECompletionRankContext::TypeBody;
			case EKeywordCompletionMode::Statement:
				return ECompletionRankContext::Statement;
			case EKeywordCompletionMode::TypeName:
				return ECompletionRankContext::TypeName;
			case EKeywordCompletionMode::Expression:
				return ECompletionRankContext::Expression;
			case EKeywordCompletionMode::DeclaratorName:
				return ECompletionRankContext::DeclaratorName;
			case EKeywordCompletionMode::DeclarationAssignment:
				return ECompletionRankContext::TypeBody;
			default:
				return ECompletionRankContext::General;
			}
		}

		bool IsCallableSnippetContext(
		    const std::string_view Source,
		    const FCompletionResult& Result,
		    const EKeywordCompletionMode Mode,
		    const FTextPosition Position)
		{
			if (Mode != EKeywordCompletionMode::Statement && Mode != EKeywordCompletionMode::Expression)
			{
				return false;
			}

			std::size_t Start = 0;
			if (!FindSourceOffset(
			        Source,
			        Result.bHasReplaceRange ? Result.ReplaceLine : Position.Line,
			        Result.bHasReplaceRange ? Result.ReplaceStartColumn : Position.Column,
			        Start))
			{
				return false;
			}
			std::size_t SegmentStart = Start;
			while (SegmentStart > 0)
			{
				const char Character = Source[SegmentStart - 1];
				if (Character == ';' || Character == '{' || Character == '}')
				{
					break;
				}
				--SegmentStart;
			}
			const std::string_view Segment = Source.substr(SegmentStart, Start - SegmentStart);
			std::size_t Before = Segment.size();
			while (Before > 0 && std::isspace(static_cast<unsigned char>(Segment[Before - 1])))
			{
				--Before;
			}
			if (Before > 0 && Segment[Before - 1] == '&')
			{
				return false;
			}
			const auto IsStandaloneAssignment = [&Segment](const std::size_t Index)
			{
				return Segment[Index] == '=' && (Index == 0 || (Segment[Index - 1] != '=' && Segment[Index - 1] != '!' && Segment[Index - 1] != '<' && Segment[Index - 1] != '>')) && (Index + 1 >= Segment.size() || Segment[Index + 1] != '=');
			};
			const std::size_t Assignment = Segment.find_last_of('=');
			if (Assignment != std::string_view::npos && IsStandaloneAssignment(Assignment))
			{
				std::string_view Left = Segment.substr(0, Assignment);
				while (!Left.empty() && std::isspace(static_cast<unsigned char>(Left.front())))
				{
					Left.remove_prefix(1);
				}
				while (!Left.empty() && std::isspace(static_cast<unsigned char>(Left.back())))
				{
					Left.remove_suffix(1);
				}
				// `Type name = Func` is a declaration initializer, where a callable can
				// genuinely be used as a value. A normal assignment (`object.value =
				// Func`, `if (ok) value = Func`) remains a call-site. Do not use
				// whitespace as a proxy for the distinction: formatting is not syntax.
				// This deliberately accepts the declaration-only spelling alphabet
				// (qualified/generic/reference type plus declarator) and rejects any
				// expression punctuation before the assignment.
				const std::size_t DeclaratorStart = Left.find_last_of(" \t\r\n");
				if (DeclaratorStart != std::string_view::npos)
				{
					const std::string_view TypePart = Left.substr(0, DeclaratorStart);
					const std::string_view NamePart = Left.substr(DeclaratorStart + 1);
					const auto IsIdentifierPart = [](const unsigned char Character)
					{
						return std::isalnum(Character) != 0 || Character == '_';
					};
					const bool bDeclarator = !NamePart.empty() && std::all_of(NamePart.begin(), NamePart.end(), [&IsIdentifierPart](const char Character)
					{
						return IsIdentifierPart(static_cast<unsigned char>(Character));
					});
					const bool bTypeSpelling = !TypePart.empty() && std::all_of(TypePart.begin(), TypePart.end(), [&IsIdentifierPart](const char Character)
					{
						return IsIdentifierPart(static_cast<unsigned char>(Character)) || Character == ':' || Character == '<' || Character == '>' || Character == ',' || Character == '&' || Character == '*' || std::isspace(static_cast<unsigned char>(Character)) != 0;
					});
					if (bDeclarator && bTypeSpelling)
					{
						return false;
					}
				}
			}
			// `function` / `fn` declarations are parsed as declaration sites above
			// in well-formed code.  Do not use a substring check here: it would
			// incorrectly suppress a call merely because an unrelated identifier
			// contains those letters (for example, `config`).
			return true;
		}

		bool FindTrailingCallableParameterList(
		    const std::string_view Detail,
		    std::size_t& OutOpen,
		    std::size_t& OutClose)
		{
			OutClose = Detail.rfind(')');
			if (OutClose == std::string_view::npos)
			{
				return false;
			}
			std::size_t Depth = 0;
			for (std::size_t Index = OutClose + 1; Index > 0;)
			{
				const char Character = Detail[--Index];
				if (Character == ')')
				{
					++Depth;
				}
				else if (Character == '(' && Depth > 0 && --Depth == 0)
				{
					OutOpen = Index;
					return true;
				}
			}
			return false;
		}

		std::string CallableSnippetParameterName(
		    const std::string_view Parameter,
		    const std::size_t Index)
		{
			std::size_t End = Parameter.size();
			while (End > 0 && std::isspace(static_cast<unsigned char>(Parameter[End - 1])))
			{
				--End;
			}
			std::size_t Begin = End;
			while (Begin > 0)
			{
				const unsigned char Character = static_cast<unsigned char>(Parameter[Begin - 1]);
				if (!std::isalnum(Character) && Character != '_')
				{
					break;
				}
				--Begin;
			}
			// A trailing type name such as `int` or `array<int>` has no parameter
			// name.  Only accept the trailing identifier after an unambiguous
			// declarator separator.
			if (Begin < End && Begin > 0 && (std::isspace(static_cast<unsigned char>(Parameter[Begin - 1])) || Parameter[Begin - 1] == '&' || Parameter[Begin - 1] == '*'))
			{
				return std::string(Parameter.substr(Begin, End - Begin));
			}
			return "arg" + std::to_string(Index + 1);
		}

		std::size_t FindTopLevelCallableParameterComma(
		    const std::string_view Parameters,
		    const std::size_t Start)
		{
			std::size_t GenericDepth = 0;
			std::size_t ParenDepth = 0;
			std::size_t BracketDepth = 0;
			for (std::size_t Index = Start; Index < Parameters.size(); ++Index)
			{
				switch (Parameters[Index])
				{
				case '<':
					{
						++GenericDepth;
						break;
					}
				case '>':
					{
						if (GenericDepth > 0)
						{
							--GenericDepth;
						}
						break;
					}
				case '(':
					{
						++ParenDepth;
						break;
					}
				case ')':
					{
						if (ParenDepth > 0)
						{
							--ParenDepth;
						}
						break;
					}
				case '[':
					{
						++BracketDepth;
						break;
					}
				case ']':
					{
						if (BracketDepth > 0)
						{
							--BracketDepth;
						}
						break;
					}
				case ',':
					{
						if (GenericDepth == 0 && ParenDepth == 0 && BracketDepth == 0)
						{
							return Index;
						}
						break;
					}
				default:
					{
						break;
					}
				}
			}
			return std::string_view::npos;
		}

		bool HasFollowingTokenAfterCompletion(
		    const std::string_view Source,
		    const FCompletionResult& Result,
		    const FTextPosition Position,
		    const std::string_view Token)
		{
			std::size_t Offset = 0;
			if (!FindSourceOffset(Source,
			        Result.bHasReplaceRange ? Result.ReplaceLine : Position.Line,
			        Result.bHasReplaceRange ? Result.ReplaceEndColumn : Position.Column,
			        Offset))
			{
				return false;
			}
			while (Offset < Source.size())
			{
				if (std::isspace(static_cast<unsigned char>(Source[Offset])))
				{
					++Offset;
					continue;
				}
				if (Source.substr(Offset, 2) == "//")
				{
					const std::size_t End = Source.find('\n', Offset + 2);
					Offset = End == std::string_view::npos ? Source.size() : End + 1;
					continue;
				}
				if (Source.substr(Offset, 2) == "/*")
				{
					const std::size_t End = Source.find("*/", Offset + 2);
					if (End == std::string_view::npos)
					{
						return false;
					}
					Offset = End + 2;
					continue;
				}
				return Source.substr(Offset, Token.size()) == Token;
			}
			return false;
		}

		void ApplyCallableSnippets(
		    const std::string_view Source,
		    const FCompletionResult& Result,
		    const EKeywordCompletionMode Mode,
		    const FTextPosition Position,
		    std::unordered_map<std::string, FCompletionItem>& Items)
		{
			if (!IsCallableSnippetContext(Source, Result, Mode, Position) || HasFollowingTokenAfterCompletion(Source, Result, Position, "("))
			{
				return;
			}
			for (auto& Pair : Items)
			{
				FCompletionItem& Item = Pair.second;
				if (Item.bOverrideMember)
				{
					continue;
				}
				if (Item.Kind != EDocumentSymbolKind::Function && Item.Kind != EDocumentSymbolKind::Method)
				{
					continue;
				}
				const std::string Callee = Item.InsertText.empty() ? Item.Label : Item.InsertText;
				// Empty-prefix completion has no replacement range. Keep the existing
				// behavior for ordinary callables, while the variadic host family can
				// still insert its argument-free call skeleton at the caret.
				const bool bSpecialSoftCall = Item.bSoftBuiltin && (IsHostFormatFamilyName(Item.Label) || Item.Detail.find(" get<V>(") != std::string::npos || Item.Detail.find(" set<V>(") != std::string::npos || Item.Detail.find(" invoke<R>(") != std::string::npos || Item.Detail.find(" invoke_static<R>(") != std::string::npos || Item.Detail.find("std::concurrent::spawn(") != std::string::npos || Item.Detail.find("std::concurrent::spawn_task<T>(") != std::string::npos || Item.Detail.find("std::memory::read<T>(") != std::string::npos || Item.Detail.find("std::memory::write<T>(") != std::string::npos);
				if (!Result.bHasReplaceRange && !bSpecialSoftCall)
				{
					continue;
				}
				// Executable reflection is a compiler intrinsic. Its four methods
				// require an explicit result/value type even though the generated
				// descriptor declarations do not contain ordinary generic methods.
				const bool bReflectionValueMethod = Item.bSoftBuiltin && Item.Kind == EDocumentSymbolKind::Method && ((Item.Label == "get" && Item.Detail.find(" get<V>(") != std::string::npos) || (Item.Label == "set" && Item.Detail.find(" set<V>(") != std::string::npos));
				const bool bReflectionResultMethod = Item.bSoftBuiltin && Item.Kind == EDocumentSymbolKind::Method && ((Item.Label == "invoke" && Item.Detail.find(" invoke<R>(") != std::string::npos) || (Item.Label == "invoke_static" && Item.Detail.find(" invoke_static<R>(") != std::string::npos));
				if (bReflectionValueMethod || bReflectionResultMethod)
				{
					if (!HasFollowingTokenAfterCompletion(Source, Result, Position, "<"))
					{
						Item.InsertText = Callee + (bReflectionValueMethod ? "<${1:ValueType}>(${0})" : "<${1:ResultType}>(${0})");
						Item.bInsertTextSnippet = true;
					}
					continue;
				}
				// The standard concurrency entry points have a string form and a
				// callable form. A parameter-name snippet would choose the wrong form
				// before the user has supplied the first argument.
				if (Item.bSoftBuiltin && Item.Kind == EDocumentSymbolKind::Function && Item.Detail.find("std::concurrent::spawn(") != std::string::npos && Item.Label == "spawn")
				{
					Item.InsertText = Callee + "(${0})";
					Item.bInsertTextSnippet = true;
					continue;
				}
				if (Item.bSoftBuiltin && Item.Kind == EDocumentSymbolKind::Function && Item.Detail.find("std::concurrent::spawn_task<T>(") != std::string::npos && Item.Label == "spawn_task")
				{
					Item.InsertText = HasFollowingTokenAfterCompletion(Source, Result, Position, "<")
					    ? Callee
					    : Callee + "<${1:ResultType}>(${0})";
					Item.bInsertTextSnippet = Item.InsertText != Callee;
					continue;
				}
				if (Item.bSoftBuiltin && Item.Kind == EDocumentSymbolKind::Function && ((Item.Label == "read" && Item.Detail.find("std::memory::read<T>(") != std::string::npos) || (Item.Label == "write" && Item.Detail.find("std::memory::write<T>(") != std::string::npos)))
				{
					if (!HasFollowingTokenAfterCompletion(Source, Result, Position, "<"))
					{
						Item.InsertText = Callee + "<${1:ValueType}>(${0})";
						Item.bInsertTextSnippet = true;
					}
					continue;
				}
				// Compiler-provided formatting functions are variadic, and print's
				// display detail combines overloads. Parameter names in that text are
				// documentation, not arguments to insert into the source.
				if (Item.bSoftBuiltin && IsHostFormatFamilyName(Item.Label))
				{
					Item.InsertText = Callee + "(${0})";
					Item.bInsertTextSnippet = true;
					continue;
				}
				// Host annotations may themselves carry parenthesized arguments.
				// The callable parameter list is the final balanced pair in the
				// display signature, not the first pair in its documentation prefix.
				std::size_t Open = std::string::npos;
				std::size_t Close = std::string::npos;
				if (!FindTrailingCallableParameterList(Item.Detail, Open, Close))
				{
					continue;
				}
				const std::string_view Parameters(
				    Item.Detail.data() + Open + 1, Close - Open - 1);
				std::string Snippet = Callee + "(";
				std::size_t ParameterStart = 0;
				std::size_t ParameterIndex = 0;
				for (;;)
				{
					const std::size_t Comma =
					    FindTopLevelCallableParameterComma(Parameters, ParameterStart);
					const std::string_view Parameter = Parameters.substr(
					    ParameterStart,
					    Comma == std::string::npos
					        ? std::string::npos
					        : Comma - ParameterStart);
					if (!Parameter.empty())
					{
						if (ParameterIndex > 0)
						{
							Snippet += ", ";
						}
						Snippet += "${" + std::to_string(ParameterIndex + 1) + ":" + CallableSnippetParameterName(Parameter, ParameterIndex) + "}";
						++ParameterIndex;
					}
					if (Comma == std::string::npos)
					{
						break;
					}
					ParameterStart = Comma + 1;
				}
				if (ParameterIndex == 0)
				{
					Snippet += "${0}";
				}
				Snippet.push_back(')');
				Item.InsertText = std::move(Snippet);
				Item.bInsertTextSnippet = true;
			}
		}

		void MergeCallableOverloadItems(
		    std::unordered_map<std::string, FCompletionItem>& Items)
		{
			const auto DetailContainsLine = [](const std::string_view Detail,
			                                    const std::string_view Signature)
			{
				std::size_t Start = 0;
				while (Start <= Detail.size())
				{
					const std::size_t End = Detail.find('\n', Start);
					if (Detail.substr(Start, End == std::string_view::npos ? Detail.size() - Start : End - Start) == Signature)
					{
						return true;
					}
					if (End == std::string_view::npos)
					{
						break;
					}
					Start = End + 1;
				}
				return false;
			};
			const auto SourceFacingSignature = [](const FCompletionItem& Item)
			{
				if (Item.Detail.find('\n') != std::string::npos)
				{
					return Item.Detail;
				}
				std::size_t Open = std::string::npos;
				std::size_t Close = std::string::npos;
				if (!FindTrailingCallableParameterList(Item.Detail, Open, Close))
				{
					return Item.Detail;
				}
				std::string Written = Item.Detail;
				std::size_t PrefixEnd = Open;
				while (PrefixEnd > 0 && Written[PrefixEnd - 1] == ' ')
				{
					--PrefixEnd;
				}
				const bool bAlreadyNamed = PrefixEnd >= Item.Label.size() && Written.compare(PrefixEnd - Item.Label.size(), Item.Label.size(), Item.Label) == 0;
				if (!bAlreadyNamed)
				{
					Written.insert(Open,
					    (Open > 0 && Written[Open - 1] != ' ' ? " " : "") + Item.Label);
				}
				if (Written.empty() || Written.back() != ';')
				{
					Written.push_back(';');
				}
				return Written;
			};
			std::vector<std::pair<std::string, FCompletionItem>> Ordered;
			Ordered.reserve(Items.size());
			for (auto& Pair : Items)
			{
				Ordered.emplace_back(Pair.first, std::move(Pair.second));
			}
			std::sort(Ordered.begin(), Ordered.end(), [](const auto& A, const auto& B)
			{
				return A.first < B.first;
			});
			std::unordered_map<std::string, FCompletionItem> Merged;
			Merged.reserve(Items.size());
			for (auto& Pair : Ordered)
			{
				FCompletionItem Item = std::move(Pair.second);
				if (Item.bOverrideMember)
				{
					Merged.emplace(Pair.first, std::move(Item));
					continue;
				}
				auto Existing = Merged.find(Item.Label);
				if (Existing == Merged.end())
				{
					Merged.emplace(Item.Label, std::move(Item));
					continue;
				}
				FCompletionItem& Target = Existing->second;
				if (!IsCallableCompletionKind(Target.Kind) || !IsCallableCompletionKind(Item.Kind))
				{
					continue;
				}
				const std::string NextSignature = SourceFacingSignature(Item);
				if (Target.Detail.find('\n') == std::string::npos)
				{
					Target.Detail = SourceFacingSignature(Target);
				}
				if (!DetailContainsLine(Target.Detail, NextSignature))
				{
					Target.Detail += "\n" + NextSignature;
				}
				if (Target.bInsertTextSnippet || Item.bInsertTextSnippet)
				{
					const std::size_t Parameters = Target.InsertText.find('(');
					const std::string Callee = Target.InsertText.empty()
					    ? Target.Label
					    : Target.InsertText.substr(0, Parameters);
					Target.InsertText = Callee + "(${0})";
					Target.bInsertTextSnippet = true;
				}
			}
			Items = std::move(Merged);
		}

		void AppendLanguageKeywordCompletions(
		    const std::string_view Prefix,
		    std::unordered_map<std::string, FCompletionItem>& OutItems,
		    const EKeywordCompletionMode Mode = EKeywordCompletionMode::Statement)
		{
			if (Mode == EKeywordCompletionMode::DeclaratorName)
			{
				return;
			}

			std::size_t Count = 0;
			const char* const* Keywords = GetLanguageKeywords(Count);
			for (std::size_t Index = 0; Index < Count; ++Index)
			{
				const std::string Label(Keywords[Index]);
				if (!IsKeywordAllowedForMode(Label, Mode) || !PrefixMatches(Label, Prefix))
				{
					continue;
				}

				const bool bPrimitiveType = VeroScript::IsTypeKeyword(Label);
				const bool bContainerType = VeroScript::IsContainerTypeKeyword(Label);
				const bool bBuiltinType = bPrimitiveType || bContainerType;
				FCompletionItem Item;
				Item.Kind = bContainerType
				    ? EDocumentSymbolKind::TypeAlias
				    : EDocumentSymbolKind::Keyword;
				Item.Label = Label;
				Item.Detail = bBuiltinType ? "builtin type" : "keyword";
				Item.InsertText = Label;
				if ((Mode == EKeywordCompletionMode::TypeName || Mode == EKeywordCompletionMode::Statement) && bContainerType)
				{
					const bool bTwoArguments = Label == "map" || Label == "multimap" || Label == "ordered_map" || Label == "ordered_multimap";
					Item.InsertText = MakeGenericTypeSnippet(Label, bTwoArguments ? 2 : 1);
					Item.bInsertTextSnippet = true;
				}
				const auto Existing = OutItems.find(Label);
				if (Existing != OutItems.end())
				{
					// A primitive spelling is reserved: no declaration can validly
					// shadow it. Keep the canonical keyword kind and commit behavior.
					if (bPrimitiveType)
					{
						Existing->second = std::move(Item);
					}
					else if (Existing->second.bSoftBuiltin && (!bContainerType || !IsTypeLikeCompletionKind(Existing->second.Kind)))
					{
						Existing->second.Kind = bContainerType
						    ? EDocumentSymbolKind::TypeAlias
						    : EDocumentSymbolKind::Keyword;
					}
					continue;
				}
				OutItems.emplace(Label, std::move(Item));
			}

			// `mutable` is intentionally contextual so existing variables named
			// mutable remain valid. Offer it only where a type member may begin.
			if (Mode == EKeywordCompletionMode::TypeBody && PrefixMatches("mutable", Prefix) && OutItems.find("mutable") == OutItems.end())
			{
				FCompletionItem Item;
				Item.Kind = EDocumentSymbolKind::Keyword;
				Item.Label = "mutable";
				Item.Detail = "contextual field modifier";
				Item.InsertText = "mutable";
				OutItems.emplace(Item.Label, std::move(Item));
			}
		}

		std::string FormatMacroCompletionDetail(const VeroScript::FPreprocessMacroDef& Macro)
		{
			// Imported definitions originated from #define_export even when the
			// current module does not re-export them (bPublic describes that).
			std::string Detail = ((Macro.bPublic || Macro.bImported) ? "#define_export " : "#define ") + Macro.Name;
			if (Macro.bFunctionLike)
			{
				Detail.push_back('(');
				for (std::size_t ParamIndex = 0; ParamIndex < Macro.Parameters.size(); ++ParamIndex)
				{
					if (ParamIndex > 0)
					{
						Detail += ", ";
					}
					Detail += Macro.Parameters[ParamIndex];
				}
				if (Macro.bVariadic)
				{
					if (!Macro.Parameters.empty())
					{
						Detail += ", ";
					}
					Detail += "...";
				}
				Detail.push_back(')');
			}
			if (!Macro.Body.empty())
			{
				Detail += " " + Macro.Body;
			}
			return Detail;
		}
