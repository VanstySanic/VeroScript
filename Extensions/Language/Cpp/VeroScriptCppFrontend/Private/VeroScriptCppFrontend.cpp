// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCppFrontend.h"
#include "VeroScriptCppPath.h"
#include "VeroScriptCppToolOutput.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace VeroScript::CppFrontend
{
	namespace
	{
		bool IsIdentifierStart(const char C)
		{
			return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || C == '_';
		}

		bool IsIdentifierPart(const char C)
		{
			return IsIdentifierStart(C) || (C >= '0' && C <= '9');
		}

		std::string Trim(std::string Value)
		{
			const auto IsSpace = [](const unsigned char C)
			{
				return std::isspace(C) != 0;
			};
			Value.erase(Value.begin(), std::find_if(Value.begin(), Value.end(), [&](const char C)
			{
				return !IsSpace(static_cast<unsigned char>(C));
			}));
			Value.erase(std::find_if(Value.rbegin(), Value.rend(),
			                [&](const char C)
			{
				return !IsSpace(static_cast<unsigned char>(C));
			}).base(),
			    Value.end());
			return Value;
		}

		std::string LeafName(const std::string_view Name)
		{
			const std::size_t Separator = Name.rfind("::");
			return Separator == std::string_view::npos
			    ? std::string(Name)
			    : std::string(Name.substr(Separator + 2));
		}

		std::string Qualify(const std::string& Scope, const std::string& Name)
		{
			return Scope.empty() ? Name : Scope + "::" + Name;
		}

		class FLexer
		{
		public:
			FLexer(const std::string_view InSource, std::string InPath)
			    : Source(InSource), Path(std::move(InPath))
			{
				if (Source.size() >= 3 && static_cast<unsigned char>(Source[0]) == 0xef && static_cast<unsigned char>(Source[1]) == 0xbb && static_cast<unsigned char>(Source[2]) == 0xbf)
				{
					Offset = 3;
				}
			}

			std::vector<FToken> Run(std::vector<FDiagnostic>& OutDiagnostics)
			{
				std::vector<FToken> Result;
				while (!AtEnd())
				{
					if (SkipTrivia(OutDiagnostics))
					{
						continue;
					}
					if (AtEnd())
					{
						break;
					}

					const std::size_t Begin = Offset;
					const std::size_t TokenLine = Line;
					const std::size_t TokenColumn = Column;
					const char C = Peek();
					if (bAtLineStart && C == '#')
					{
						while (!AtEnd() && Peek() != '\n' && Peek() != '\r')
						{
							Advance();
						}
						Result.push_back(MakeToken(
						    ETokenKind::Preprocessor, Begin, TokenLine, TokenColumn));
						continue;
					}
					const std::size_t RawPrefixLength =
					    C == 'R' && Peek(1) == '"'                                               ? 1
					    : C == 'u' && Peek(1) == '8' && Peek(2) == 'R' && Peek(3) == '"'         ? 3
					    : (C == 'L' || C == 'u' || C == 'U') && Peek(1) == 'R' && Peek(2) == '"' ? 2
					                                                                             : 0;
					if (RawPrefixLength > 0)
					{
						std::string Normalized;
						ScanRawString(RawPrefixLength,
						    RawPrefixLength == 1 || RawPrefixLength == 3,
						    Normalized, OutDiagnostics, Begin, TokenLine, TokenColumn);
						FToken Token = MakeToken(ETokenKind::StringLiteral,
						    Begin, TokenLine, TokenColumn);
						if (!Normalized.empty())
						{
							Token.Text = std::move(Normalized);
						}
						Result.push_back(std::move(Token));
						continue;
					}
					const std::size_t LiteralPrefixLength =
					    ((C == 'L' || C == 'u' || C == 'U') && (Peek(1) == '"' || Peek(1) == '\'')) ? 1
					    : (C == 'u' && Peek(1) == '8' && (Peek(2) == '"' || Peek(2) == '\''))       ? 2
					                                                                                : 0;
					if (LiteralPrefixLength > 0)
					{
						for (std::size_t Index = 0; Index < LiteralPrefixLength; ++Index)
						{
							Advance();
						}
						const char Quote = Peek();
						Advance();
						bool bClosed = false;
						while (!AtEnd())
						{
							const char Value = Advance();
							if (Value == '\\' && !AtEnd())
							{
								Advance();
								continue;
							}
							if (Value == Quote)
							{
								bClosed = true;
								break;
							}
							if (Value == '\n' || Value == '\r')
							{
								break;
							}
						}
						if (!bClosed)
						{
							AddDiagnostic(OutDiagnostics, "cpp.lex.unterminated_literal",
							    "unterminated C++ literal", Begin, TokenLine, TokenColumn);
						}
						Result.push_back(MakeToken(
						    Quote == '"' ? ETokenKind::StringLiteral : ETokenKind::CharacterLiteral,
						    Begin, TokenLine, TokenColumn));
						continue;
					}
					if (IsIdentifierStart(C))
					{
						Advance();
						while (!AtEnd() && IsIdentifierPart(Peek()))
						{
							Advance();
						}
						Result.push_back(MakeToken(
						    ETokenKind::Identifier, Begin, TokenLine, TokenColumn));
						continue;
					}
					if (std::isdigit(static_cast<unsigned char>(C)) != 0 || (C == '.' && Offset + 1 < Source.size() && std::isdigit(static_cast<unsigned char>(Source[Offset + 1])) != 0))
					{
						ScanNumber();
						Result.push_back(MakeToken(
						    ETokenKind::Number, Begin, TokenLine, TokenColumn));
						continue;
					}
					if (C == '"' || C == '\'')
					{
						const char Quote = C;
						Advance();
						bool bClosed = false;
						while (!AtEnd())
						{
							const char Value = Advance();
							if (Value == '\\' && !AtEnd())
							{
								Advance();
								continue;
							}
							if (Value == Quote)
							{
								bClosed = true;
								break;
							}
							if (Value == '\n' || Value == '\r')
							{
								break;
							}
						}
						if (!bClosed)
						{
							AddDiagnostic(OutDiagnostics, "cpp.lex.unterminated_literal",
							    "unterminated C++ literal", Begin, TokenLine, TokenColumn);
						}
						Result.push_back(MakeToken(
						    Quote == '"' ? ETokenKind::StringLiteral : ETokenKind::CharacterLiteral,
						    Begin, TokenLine, TokenColumn));
						continue;
					}

					ScanSymbol();
					Result.push_back(MakeToken(ETokenKind::Symbol, Begin, TokenLine, TokenColumn));
				}

				FToken End;
				End.Kind = ETokenKind::End;
				End.Location = MakeLocation(Offset, Line, Column);
				End.EndOffset = Offset;
				Result.push_back(std::move(End));
				return Result;
			}

		private:
			void ScanRawString(
			    const std::size_t PrefixLength,
			    const bool bUtf8Compatible,
			    std::string& OutNormalized,
			    std::vector<FDiagnostic>& OutDiagnostics,
			    const std::size_t Begin,
			    const std::size_t TokenLine,
			    const std::size_t TokenColumn)
			{
				for (std::size_t Index = 0; Index < PrefixLength; ++Index)
				{
					Advance();
				}
				Advance(); // opening quote
				const std::size_t DelimiterBegin = Offset;
				bool bValidDelimiter = true;
				while (!AtEnd() && Peek() != '(' && Peek() != '\n' && Peek() != '\r')
				{
					const unsigned char C = static_cast<unsigned char>(Peek());
					if (Offset - DelimiterBegin >= 16 || C <= 0x20 || C == ')' || C == '\\')
					{
						bValidDelimiter = false;
					}
					Advance();
				}
				if (AtEnd() || Peek() != '(')
				{
					AddDiagnostic(OutDiagnostics, "cpp.lex.invalid_raw_string_delimiter",
					    "raw string literal requires a valid delimiter followed by '('",
					    Begin, TokenLine, TokenColumn);
					return;
				}
				if (!bValidDelimiter)
				{
					AddDiagnostic(OutDiagnostics, "cpp.lex.invalid_raw_string_delimiter",
					    "raw string delimiter must contain at most 16 valid d-char bytes",
					    Begin, TokenLine, TokenColumn);
					// Searching every content position with an unbounded malformed
					// delimiter can be quadratic. The invalid prefix was consumed;
					// let normal lexing resume at '(' after reporting the error.
					return;
				}
				const std::string Delimiter(Source.substr(
				    DelimiterBegin, Offset - DelimiterBegin));
				Advance(); // '('
				const std::size_t ContentBegin = Offset;
				const std::string Closing = ")" + Delimiter + "\"";
				while (!AtEnd() && Source.substr(Offset, Closing.size()) != Closing)
				{
					Advance();
				}
				if (AtEnd())
				{
					AddDiagnostic(OutDiagnostics, "cpp.lex.unterminated_raw_string",
					    "unterminated C++ raw string literal",
					    Begin, TokenLine, TokenColumn);
					return;
				}
				const std::string_view Content = Source.substr(
				    ContentBegin, Offset - ContentBegin);
				for (std::size_t Index = 0; Index < Closing.size(); ++Index)
				{
					Advance();
				}
				if (!bUtf8Compatible)
				{
					AddDiagnostic(OutDiagnostics, "cpp.lex.wide_raw_string_unsupported",
					    "wide raw strings have target-dependent code-unit semantics; use an ordinary or u8 raw string",
					    Begin, TokenLine, TokenColumn);
					return;
				}
				OutNormalized = "\"";
				for (const unsigned char C : Content)
				{
					switch (C)
					{
					case '\\':
						{
							OutNormalized += "\\\\";
							break;
						}
					case '"':
						{
							OutNormalized += "\\\"";
							break;
						}
					case '\n':
						{
							OutNormalized += "\\n";
							break;
						}
					case '\r':
						{
							OutNormalized += "\\r";
							break;
						}
					case '\t':
						{
							OutNormalized += "\\t";
							break;
						}
					default:
						{
							if (C < 0x20 || C == 0x7f)
							{
								AddDiagnostic(OutDiagnostics,
								    "cpp.lex.raw_string_control_character_unsupported",
								    "raw string contains a control byte that VeroScript string syntax cannot represent",
								    Begin, TokenLine, TokenColumn);
							}
							else
							{
								OutNormalized.push_back(static_cast<char>(C));
							}
							break;
						}
					}
				}
				OutNormalized.push_back('"');
			}

			bool AtEnd() const
			{
				return Offset >= Source.size();
			}
			char Peek(const std::size_t Lookahead = 0) const
			{
				return Offset + Lookahead < Source.size() ? Source[Offset + Lookahead] : '\0';
			}

			char Advance()
			{
				if (AtEnd())
				{
					return '\0';
				}
				const char C = Source[Offset++];
				if (C == '\n')
				{
					++Line;
					Column = 1;
					bAtLineStart = true;
				}
				else
				{
					++Column;
					if (C != ' ' && C != '\t' && C != '\r')
					{
						bAtLineStart = false;
					}
				}
				return C;
			}

			bool SkipTrivia(std::vector<FDiagnostic>& OutDiagnostics)
			{
				if (AtEnd())
				{
					return false;
				}
				const char C = Peek();
				if (std::isspace(static_cast<unsigned char>(C)) != 0)
				{
					Advance();
					return true;
				}
				if (C == '/' && Peek(1) == '/')
				{
					while (!AtEnd() && Peek() != '\n')
					{
						// C++ removes a backslash-newline before recognizing comments.
						// Keep the continued physical line inside this comment instead
						// of exposing declarations that the native compiler never sees.
						if (Peek() == '\\' && (Peek(1) == '\n' || (Peek(1) == '\r' && Peek(2) == '\n')))
						{
							Advance();
							if (Peek() == '\r')
							{
								Advance();
							}
							Advance();
							continue;
						}
						Advance();
					}
					return true;
				}
				if (C == '/' && Peek(1) == '*')
				{
					const std::size_t Begin = Offset;
					const std::size_t BeginLine = Line;
					const std::size_t BeginColumn = Column;
					Advance();
					Advance();
					while (!AtEnd() && !(Peek() == '*' && Peek(1) == '/'))
					{
						Advance();
					}
					if (AtEnd())
					{
						AddDiagnostic(OutDiagnostics, "cpp.lex.unterminated_comment",
						    "unterminated block comment", Begin, BeginLine, BeginColumn);
					}
					else
					{
						Advance();
						Advance();
					}
					return true;
				}
				return false;
			}

			void ScanNumber()
			{
				while (!AtEnd())
				{
					const char C = Peek();
					if (std::isalnum(static_cast<unsigned char>(C)) == 0 && C != '.' && C != '_' && C != '\'')
					{
						if ((C == '+' || C == '-') && Offset > 0 && (Source[Offset - 1] == 'e' || Source[Offset - 1] == 'E' || Source[Offset - 1] == 'p' || Source[Offset - 1] == 'P'))
						{
							Advance();
							continue;
						}
						break;
					}
					Advance();
				}
			}

			void ScanSymbol()
			{
				static const std::vector<std::string_view> Multi = {
				    "<=>", "<<=", ">>=", "->*", "...", "::", "->", ".*",
				    "++", "--", "<=", ">=", "==", "!=", "&&", "||",
				    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
				    // Keep `>>` as two `>` tokens. This is required for nested C++17
				    // template argument lists; JoinTokens still reconstructs the shift
				    // operator spelling in function bodies without inserting whitespace.
				    "<<", "[[", "]]"};
				for (const std::string_view Candidate : Multi)
				{
					if (Source.substr(Offset, Candidate.size()) == Candidate)
					{
						for (std::size_t Index = 0; Index < Candidate.size(); ++Index)
						{
							Advance();
						}
						return;
					}
				}
				Advance();
			}

			FSourceLocation MakeLocation(
			    const std::size_t At,
			    const std::size_t AtLine,
			    const std::size_t AtColumn) const
			{
				return {Path, AtLine, AtColumn, At};
			}

			FToken MakeToken(
			    const ETokenKind Kind,
			    const std::size_t Begin,
			    const std::size_t TokenLine,
			    const std::size_t TokenColumn) const
			{
				FToken Token;
				Token.Kind = Kind;
				Token.Text = std::string(Source.substr(Begin, Offset - Begin));
				Token.Location = MakeLocation(Begin, TokenLine, TokenColumn);
				Token.EndOffset = Offset;
				return Token;
			}

			void AddDiagnostic(
			    std::vector<FDiagnostic>& Out,
			    std::string Code,
			    std::string Message,
			    const std::size_t At,
			    const std::size_t AtLine,
			    const std::size_t AtColumn) const
			{
				Out.push_back({EDiagnosticSeverity::Error, std::move(Code), std::move(Message),
				    MakeLocation(At, AtLine, AtColumn)});
			}

			std::string_view Source;
			std::string Path;
			std::size_t Offset = 0;
			std::size_t Line = 1;
			std::size_t Column = 1;
			bool bAtLineStart = true;
		};

		std::string JoinTokenRange(
		    const std::vector<FToken>& Tokens,
		    const std::size_t Begin,
		    const std::size_t End)
		{
			std::vector<FToken> Slice;
			if (Begin < End && End <= Tokens.size())
			{
				Slice.assign(Tokens.begin() + static_cast<std::ptrdiff_t>(Begin),
				    Tokens.begin() + static_cast<std::ptrdiff_t>(End));
			}
			return JoinTokens(Slice);
		}

		bool IsIdentifierText(const std::string_view Text)
		{
			if (Text.empty() || (std::isalpha(static_cast<unsigned char>(Text.front())) == 0 && Text.front() != '_'))
			{
				return false;
			}
			return std::all_of(Text.begin() + 1, Text.end(), [](const char C)
			{
				return std::isalnum(static_cast<unsigned char>(C)) != 0 || C == '_';
			});
		}

		bool StripConventionalHeaderGuard(
		    std::vector<FToken>& Tokens,
		    std::vector<FDiagnostic>& Diagnostics)
		{
			if (Tokens.size() < 4)
			{
				return true;
			}
			std::size_t GuardAt = 0;
			if (Tokens[0].Kind == ETokenKind::Preprocessor && Trim(Tokens[0].Text) == "#pragma once")
			{
				GuardAt = 1;
			}
			if (GuardAt >= Tokens.size() - 1 || Tokens[GuardAt].Kind != ETokenKind::Preprocessor)
			{
				return true;
			}
			const std::string Ifndef = Trim(Tokens[GuardAt].Text);
			if (Ifndef.rfind("#ifndef", 0) != 0)
			{
				return true;
			}
			const std::string Macro = Trim(Ifndef.substr(7));
			const auto Fail = [&](const std::string& Message)
			{
				Diagnostics.push_back({EDiagnosticSeverity::Error,
				    "cpp.parse.invalid_header_guard", Message, Tokens[GuardAt].Location});
				return false;
			};
			if (!IsIdentifierText(Macro))
			{
				return Fail("#ifndef header guard requires one macro identifier");
			}
			if (GuardAt + 1 >= Tokens.size() - 1 || Tokens[GuardAt + 1].Kind != ETokenKind::Preprocessor)
			{
				return Fail("header guard #ifndef must be followed by its #define");
			}
			const std::string Define = Trim(Tokens[GuardAt + 1].Text);
			if (Define.rfind("#define", 0) != 0 || Trim(Define.substr(7)) != Macro)
			{
				return Fail("header guard #define must use the same macro and no replacement value");
			}
			const std::size_t EndAt = Tokens.size() - 2;
			if (Tokens[EndAt].Kind != ETokenKind::Preprocessor || Trim(Tokens[EndAt].Text).rfind("#endif", 0) != 0)
			{
				return Fail("header guard must end at the end of the source file");
			}
			for (std::size_t Index = GuardAt + 2; Index < EndAt; ++Index)
			{
				if (Tokens[Index].Kind != ETokenKind::Preprocessor)
				{
					continue;
				}
				const std::string Directive = Trim(Tokens[Index].Text);
				if (Directive.rfind("#if", 0) == 0 || Directive.rfind("#else", 0) == 0 || Directive.rfind("#elif", 0) == 0 || Directive.rfind("#endif", 0) == 0)
				{
					return Fail("nested conditional preprocessing is not supported by the strict frontend");
				}
			}
			Tokens.erase(Tokens.begin() + static_cast<std::ptrdiff_t>(EndAt));
			Tokens.erase(Tokens.begin() + static_cast<std::ptrdiff_t>(GuardAt),
			    Tokens.begin() + static_cast<std::ptrdiff_t>(GuardAt + 2));
			return true;
		}

		bool IsTypeQualifier(const std::string_view Text)
		{
			return Text == "const" || Text == "volatile" || Text == "typename" || Text == "class" || Text == "struct" || Text == "enum";
		}

		bool IsDeclModifier(const std::string_view Text)
		{
			return Text == "static" || Text == "inline" || Text == "virtual" || Text == "constexpr" || Text == "consteval" || Text == "constinit" || Text == "explicit" || Text == "friend" || Text == "extern" || Text == "register" || Text == "mutable";
		}

		bool IsBuiltinTypeWord(const std::string_view Text)
		{
			return Text == "void" || Text == "bool" || Text == "char" || Text == "wchar_t" || Text == "char8_t" || Text == "char16_t" || Text == "char32_t" || Text == "short" || Text == "int" || Text == "long" || Text == "signed" || Text == "unsigned" || Text == "float" || Text == "double" || Text == "auto";
		}

		bool IsSupportedEnumUnderlyingType(const std::string_view Type)
		{
			return Type == "char" || Type == "int8" || Type == "uint8" || Type == "short" || Type == "int16" || Type == "uint16" || Type == "int" || Type == "unsigned int" || Type == "int64";
		}

		FType ParseTypeTokens(const std::vector<FToken>& Input)
		{
			FType Result;
			if (!Input.empty())
			{
				Result.Location = Input.front().Location;
			}
			Result.Spelling = JoinTokens(Input);
			std::vector<FToken> Tokens;
			Tokens.reserve(Input.size());
			int QualifierAngleDepth = 0;
			bool bSawTopLevelPointer = false;
			for (const FToken& Token : Input)
			{
				if (Token.Text == "<")
				{
					++QualifierAngleDepth;
					Tokens.push_back(Token);
					continue;
				}
				if (Token.Text == ">")
				{
					QualifierAngleDepth = std::max(0, QualifierAngleDepth - 1);
					Tokens.push_back(Token);
					continue;
				}
				if (QualifierAngleDepth > 0)
				{
					// Qualifiers and indirections inside a template argument belong to
					// that argument and are handled by the recursive parse below.
					Tokens.push_back(Token);
					continue;
				}
				if (Token.Text == "const")
				{
					if (bSawTopLevelPointer)
					{
						Result.bPointerConst = true;
					}
					else
					{
						Result.bConst = true;
					}
					continue;
				}
				if (Token.Text == "volatile")
				{
					if (bSawTopLevelPointer)
					{
						Result.bPointerVolatile = true;
					}
					else
					{
						Result.bVolatile = true;
					}
					continue;
				}
				if (Token.Text == "*")
				{
					bSawTopLevelPointer = true;
					if (Result.PointerDepth != std::numeric_limits<std::uint8_t>::max())
					{
						++Result.PointerDepth;
					}
					continue;
				}
				if (Token.Text == "&")
				{
					Result.Reference = EReferenceKind::LValue;
					continue;
				}
				if (Token.Text == "&&")
				{
					Result.Reference = EReferenceKind::RValue;
					continue;
				}
				if (Token.Text == "typename" || Token.Text == "class" || Token.Text == "struct" || Token.Text == "enum")
				{
					continue;
				}
				Tokens.push_back(Token);
			}

			std::size_t Open = Tokens.size();
			int Depth = 0;
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				if (Tokens[Index].Text == "<" && Depth++ == 0)
				{
					Open = Index;
					break;
				}
			}
			if (Open == Tokens.size())
			{
				Result.BaseName = Trim(JoinTokens(Tokens));
				return Result;
			}

			Result.BaseName = Trim(JoinTokenRange(Tokens, 0, Open));
			std::size_t PartBegin = Open + 1;
			Depth = 0;
			for (std::size_t Index = Open + 1; Index < Tokens.size(); ++Index)
			{
				if (Tokens[Index].Text == "<")
				{
					++Depth;
				}
				else if (Tokens[Index].Text == ">")
				{
					if (Depth == 0)
					{
						if (PartBegin < Index)
						{
							Result.TemplateArguments.emplace_back(ParseTypeTokens(
							    std::vector<FToken>(Tokens.begin() + static_cast<std::ptrdiff_t>(PartBegin),
							        Tokens.begin() + static_cast<std::ptrdiff_t>(Index))));
						}
						break;
					}
					--Depth;
				}
				else if (Tokens[Index].Text == "," && Depth == 0)
				{
					Result.TemplateArguments.emplace_back(ParseTypeTokens(
					    std::vector<FToken>(Tokens.begin() + static_cast<std::ptrdiff_t>(PartBegin),
					        Tokens.begin() + static_cast<std::ptrdiff_t>(Index))));
					PartBegin = Index + 1;
				}
			}
			return Result;
		}

		std::string TypeIdentity(const FType& Type);
		std::string TemplateTypeIdentity(
		    const FType& Type,
		    const std::vector<FTemplateParameter>& Parameters);
		std::string FunctionIdentity(const FFunction& Function);
		bool TemplateParametersCompatible(
		    const std::vector<FTemplateParameter>& Left,
		    const std::vector<FTemplateParameter>& Right);
		FType RemapTemplateType(
		    FType Type,
		    const std::vector<FTemplateParameter>& Source,
		    const std::vector<FTemplateParameter>& Target);
		void MergeAttributes(
		    std::vector<FAttribute>& Target,
		    const std::vector<FAttribute>& Source);
		void ResolveOutOfClassMembers(
		    FTranslationUnit& Unit,
		    std::vector<FDiagnostic>& Diagnostics,
		    bool bDeferMissingOwner);
		void ResolveOutOfClassStaticFields(
		    FTranslationUnit& Unit,
		    std::vector<FDiagnostic>& Diagnostics,
		    bool bDeferMissingOwner);
		bool ValidateFunctionParameters(
		    const FFunction& Function,
		    std::vector<FDiagnostic>& Diagnostics);

		class FParser
		{
		public:
			FParser(
			    std::vector<FToken> InTokens,
			    FTranslationUnit& InUnit,
			    std::vector<FDiagnostic>& InDiagnostics,
			    const FParseOptions& InOptions)
			    : Tokens(std::move(InTokens)), Unit(InUnit), Diagnostics(InDiagnostics), Options(InOptions)
			{
			}

			void Run()
			{
				ParseScope({}, false);
				ResolveOutOfClassMembers(Unit, Diagnostics,
				    Options.bDeferOutOfClassMemberResolution);
				ResolveOutOfClassStaticFields(Unit, Diagnostics,
				    Options.bDeferOutOfClassMemberResolution);
				ValidateAliasConflicts();
			}

		private:
			struct FTemplateScope
			{
				explicit FTemplateScope(std::vector<std::string>& InNames,
				    const std::vector<FTemplateParameter>& Parameters = {})
				    : Names(InNames), PreviousSize(InNames.size())
				{
					for (const auto& Parameter : Parameters)
					{
						Names.push_back(Parameter.Name);
					}
				}

				~FTemplateScope()
				{
					Names.resize(PreviousSize);
				}

				std::vector<std::string>& Names;

				std::size_t PreviousSize;
			};

			const FToken& Peek(const std::size_t Ahead = 0) const
			{
				const std::size_t At = std::min(Current + Ahead, Tokens.size() - 1);
				return Tokens[At];
			}
			bool AtEnd() const
			{
				return Peek().Kind == ETokenKind::End;
			}
			bool Check(const std::string_view Text) const
			{
				return Peek().Text == Text;
			}
			bool Match(const std::string_view Text)
			{
				if (!Check(Text))
				{
					return false;
				}
				++Current;
				return true;
			}

			void Error(const FToken& Token, std::string Code, std::string Message)
			{
				ErrorAt(Token.Location, std::move(Code), std::move(Message));
			}

			void ErrorAt(
			    const FSourceLocation& Location,
			    std::string Code,
			    std::string Message)
			{
				Diagnostics.push_back({EDiagnosticSeverity::Error, std::move(Code),
				    std::move(Message), Location});
			}

			bool ParseAttributeSequence(
			    const std::vector<FToken>& Source,
			    std::size_t& At,
			    std::vector<FAttribute>& Out)
			{
				while (At < Source.size() && Source[At].Text == "[[")
				{
					const FToken Start = Source[At++];
					while (At < Source.size() && Source[At].Text != "]]")
					{
						if (Source[At].Kind != ETokenKind::Identifier)
						{
							Error(Source[At], "cpp.parse.invalid_attribute_name",
							    "C++ attribute requires an identifier name");
							return false;
						}
						FAttribute Attribute;
						Attribute.Location = Source[At].Location;
						std::vector<std::string> NameParts;
						NameParts.push_back(Source[At++].Text);
						while (At + 1 < Source.size() && Source[At].Text == "::" && Source[At + 1].Kind == ETokenKind::Identifier)
						{
							++At;
							NameParts.push_back(Source[At++].Text);
						}
						Attribute.Name = NameParts.back();
						for (std::size_t Index = 0; Index + 1 < NameParts.size(); ++Index)
						{
							if (Index > 0)
							{
								Attribute.NamespaceName += "::";
							}
							Attribute.NamespaceName += NameParts[Index];
						}
						if (At < Source.size() && Source[At].Text == "(")
						{
							++At;
							std::size_t ArgumentIndex = 0;
							while (At < Source.size() && Source[At].Text != ")")
							{
								const std::size_t ArgumentBegin = At;
								int Depth = 0;
								while (At < Source.size())
								{
									if (Source[At].Text == "(" || Source[At].Text == "[" || Source[At].Text == "{")
									{
										++Depth;
									}
									else if (Source[At].Text == ")")
									{
										if (Depth == 0)
										{
											break;
										}
										--Depth;
									}
									else if (Source[At].Text == "]" || Source[At].Text == "}")
									{
										Depth = std::max(0, Depth - 1);
									}
									if (Source[At].Text == "," && Depth == 0)
									{
										break;
									}
									++At;
								}
								if (ArgumentBegin == At)
								{
									Error(Source[At], "cpp.parse.empty_attribute_argument",
									    "attribute argument cannot be empty");
									return false;
								}
								std::size_t Equals = At;
								for (std::size_t Index = ArgumentBegin; Index < At; ++Index)
								{
									if (Source[Index].Text == "=")
									{
										Equals = Index;
										break;
									}
								}
								FAttributeArgument Argument;
								if (Equals < At && Equals == ArgumentBegin + 1 && Source[ArgumentBegin].Kind == ETokenKind::Identifier)
								{
									Argument.Name = Source[ArgumentBegin].Text;
									Argument.Value = JoinTokenRange(Source, Equals + 1, At);
									Argument.bHasValue = true;
								}
								else if (Equals == At && At == ArgumentBegin + 1 && Source[ArgumentBegin].Kind == ETokenKind::Identifier)
								{
									Argument.Name = Source[ArgumentBegin].Text;
								}
								else
								{
									Argument.Name = "Arg" + std::to_string(ArgumentIndex);
									Argument.Value = JoinTokenRange(Source, ArgumentBegin, At);
									Argument.bHasValue = true;
								}
								if (Argument.bHasValue && Argument.Value.empty())
								{
									Error(Source[ArgumentBegin], "cpp.parse.attribute_argument_value_required",
									    "named attribute argument requires a value");
									return false;
								}
								Attribute.Arguments.push_back(std::move(Argument));
								++ArgumentIndex;
								if (At < Source.size() && Source[At].Text == ",")
								{
									++At;
								}
							}
							if (At >= Source.size() || Source[At].Text != ")")
							{
								Error(Start, "cpp.parse.unterminated_attribute_arguments",
								    "expected ')' after C++ attribute arguments");
								return false;
							}
							++At;
						}
						Out.push_back(std::move(Attribute));
						if (At < Source.size() && Source[At].Text == ",")
						{
							++At;
						}
						else if (At < Source.size() && Source[At].Text != "]]")
						{
							Error(Source[At], "cpp.parse.invalid_attribute_list",
							    "expected ',' or ']]' after C++ attribute");
							return false;
						}
					}
					if (At >= Source.size() || Source[At].Text != "]]")
					{
						Error(Start, "cpp.parse.unterminated_attribute_list",
						    "expected ']]' after C++ attributes");
						return false;
					}
					++At;
				}
				return true;
			}

			bool ExtractAttributes(
			    std::vector<FToken>& Source,
			    std::vector<FAttribute>& Out)
			{
				for (std::size_t At = 0; At < Source.size();)
				{
					if (Source[At].Text != "[[")
					{
						++At;
						continue;
					}
					const std::size_t Begin = At;
					if (!ParseAttributeSequence(Source, At, Out))
					{
						return false;
					}
					Source.erase(
					    Source.begin() + static_cast<std::ptrdiff_t>(Begin),
					    Source.begin() + static_cast<std::ptrdiff_t>(At));
					At = Begin;
				}
				return true;
			}

			bool ParseTemplateParameters(
			    const std::string& Scope,
			    std::vector<FTemplateParameter>& Out)
			{
				const FTemplateScope ParameterScope(ActiveTemplateParameters);
				const FToken Start = Peek();
				if (!Match("template") || !Match("<"))
				{
					Error(Start, "cpp.parse.invalid_template",
					    "expected '<' after template");
					return false;
				}
				if (Check(">"))
				{
					Error(Peek(), "cpp.parse.template_specialization_unsupported",
					    "explicit C++ template specializations are not supported");
					++Current;
					return false;
				}
				std::unordered_set<std::string> Names;
				bool bSawDefault = false;
				while (!AtEnd())
				{
					const FToken ParameterStart = Peek();
					if (!Match("typename") && !Match("class"))
					{
						Error(ParameterStart, "cpp.parse.non_type_template_parameter_unsupported",
						    "only typename/class template parameters can be mapped to VeroScript generics");
						return false;
					}
					if (Match("...") || Peek().Kind != ETokenKind::Identifier)
					{
						Error(Peek(), "cpp.parse.template_parameter_pack_unsupported",
						    "template parameter packs are not supported by the VeroScript generic mapping");
						return false;
					}
					FTemplateParameter Parameter;
					Parameter.Name = Peek().Text;
					Parameter.Location = Peek().Location;
					++Current;
					if (Match("..."))
					{
						Error(Peek(), "cpp.parse.template_parameter_pack_unsupported",
						    "template parameter packs are not supported by the VeroScript generic mapping");
						return false;
					}
					if (!Names.insert(Parameter.Name).second)
					{
						ErrorAt(Parameter.Location, "cpp.parse.duplicate_template_parameter",
						    "duplicate template parameter: " + Parameter.Name);
						return false;
					}
					if (Match("="))
					{
						bSawDefault = true;
						const std::size_t DefaultBegin = Current;
						int Depth = 0;
						while (!AtEnd())
						{
							if (Check("<"))
							{
								++Depth;
							}
							else if (Check(">"))
							{
								if (Depth == 0)
								{
									break;
								}
								--Depth;
							}
							if (Check(",") && Depth == 0)
							{
								break;
							}
							++Current;
						}
						if (DefaultBegin == Current)
						{
							Error(Peek(), "cpp.parse.template_default_type_required",
							    "template type default requires a type");
							return false;
						}
						Parameter.DefaultType = ResolveAlias(
						    ParseTypeTokens(Slice(DefaultBegin, Current)), Scope);
						Parameter.bHasDefault = true;
					}
					else if (bSawDefault)
					{
						ErrorAt(Parameter.Location,
						    "cpp.parse.non_trailing_template_default",
						    "a non-defaulted template parameter cannot follow a defaulted parameter in the VeroScript generic mapping");
						return false;
					}
					ActiveTemplateParameters.push_back(Parameter.Name);
					Out.push_back(std::move(Parameter));
					if (Match(">"))
					{
						return true;
					}
					if (!Match(","))
					{
						Error(Peek(), "cpp.parse.invalid_template_parameter_list",
						    "expected ',' or '>' after template parameter");
						return false;
					}
				}
				Error(Start, "cpp.parse.unterminated_template_parameter_list",
				    "expected '>' after template parameters");
				return false;
			}

			void ParseScope(const std::string& Scope, const bool bExpectClose)
			{
				while (!AtEnd())
				{
					if (bExpectClose && Match("}"))
					{
						Match(";");
						return;
					}
					if (Peek().Kind == ETokenKind::Preprocessor)
					{
						ParsePreprocessor();
						continue;
					}
					if (Match(";"))
					{
						continue;
					}
					std::vector<FAttribute> Attributes;
					if (Check("[[") && !ParseAttributeSequence(Tokens, Current, Attributes))
					{
						Synchronize();
						continue;
					}
					if (Check("template"))
					{
						std::vector<FTemplateParameter> TemplateParameters;
						if (!ParseTemplateParameters(Scope, TemplateParameters))
						{
							Synchronize();
							continue;
						}
						if (Check("[[") && !ParseAttributeSequence(Tokens, Current, Attributes))
						{
							Synchronize();
							continue;
						}
						if (Check("struct") || Check("class"))
						{
							ParseRecord(Scope, std::move(TemplateParameters), std::move(Attributes));
						}
						else if (Check("using"))
						{
							if (!Attributes.empty())
							{
								ErrorAt(Attributes.front().Location,
								    "cpp.parse.using_attribute_unsupported",
								    "using-alias attributes are not represented by the strict frontend AST");
							}
							ParseUsing(Scope, std::move(TemplateParameters));
						}
						else if (Check("enum") || Check("namespace") || Check("typedef") || Check("union"))
						{
							RejectAndSynchronize("cpp.parse.templated_declaration_unsupported",
							    "this declaration kind cannot be mapped to a VeroScript generic");
						}
						else
						{
							ParseFreeDeclaration(Scope, std::move(TemplateParameters), std::move(Attributes));
						}
						continue;
					}
					if (Check("namespace"))
					{
						if (!Attributes.empty())
						{
							ErrorAt(Attributes.front().Location, "cpp.parse.namespace_attribute_unsupported",
							    "namespace attributes are not represented by the strict frontend AST");
						}
						ParseNamespace(Scope);
						continue;
					}
					if (Check("extern") && Peek(1).Kind == ETokenKind::StringLiteral && Peek(1).Text == "\"C\"")
					{
						if (Peek(2).Text == "{")
						{
							const std::size_t FunctionCount = Unit.Functions.size();
							const std::size_t VariableCount = Unit.Variables.size();
							Current += 3;
							ParseScope(Scope, true);
							for (std::size_t Index = FunctionCount; Index < Unit.Functions.size(); ++Index)
							{
								Unit.Functions[Index].bCLinkage = true;
							}
							for (std::size_t Index = VariableCount; Index < Unit.Variables.size(); ++Index)
							{
								Unit.Variables[Index].bCLinkage = true;
							}
						}
						else
						{
							Current += 2;
							const std::size_t FunctionCount = Unit.Functions.size();
							const std::size_t VariableCount = Unit.Variables.size();
							ParseFreeDeclaration(Scope, {}, std::move(Attributes));
							if (Unit.Functions.size() > FunctionCount)
							{
								Unit.Functions.back().bCLinkage = true;
							}
							if (Unit.Variables.size() > VariableCount)
							{
								Unit.Variables.back().bCLinkage = true;
							}
						}
						continue;
					}
					if (Check("enum"))
					{
						ParseEnum(Scope, std::move(Attributes));
						continue;
					}
					if (Check("struct") || Check("class"))
					{
						ParseRecord(Scope, {}, std::move(Attributes));
						continue;
					}
					if (Check("using"))
					{
						if (!Attributes.empty())
						{
							ErrorAt(Attributes.front().Location, "cpp.parse.using_attribute_unsupported",
							    "using-declaration attributes are not represented by the strict frontend AST");
						}
						ParseUsing(Scope);
						continue;
					}
					if (Check("typedef"))
					{
						if (!Attributes.empty())
						{
							ErrorAt(Attributes.front().Location,
							    "cpp.parse.typedef_attribute_unsupported",
							    "typedef attributes are not represented by the strict frontend AST");
						}
						ParseTypedef(Scope);
						continue;
					}
					if (Check("union"))
					{
						RejectAndSynchronize("cpp.parse.declaration_unsupported",
						    "union declarations are not supported; use a struct with explicit representation");
						continue;
					}
					ParseFreeDeclaration(Scope, {}, std::move(Attributes));
				}
				if (bExpectClose)
				{
					Error(Peek(), "cpp.parse.expected_close_brace", "expected '}' before end of file");
				}
			}

			void ParsePreprocessor()
			{
				const FToken Directive = Tokens[Current++];
				std::string Text = Trim(Directive.Text);
				const bool bAllowed = Text.rfind("#include", 0) == 0 || Text == "#pragma once";
				if ((!Options.bAllowIncludeAndPragma || !bAllowed) && Options.bRejectUnknownPreprocessorDirectives)
				{
					Error(Directive, "cpp.parse.preprocessor_unsupported",
					    "unsupported preprocessor directive in strict C++ source: " + Text);
				}
			}

			void ParseNamespace(const std::string& OuterScope)
			{
				const FToken Start = Tokens[Current++];
				std::string Name;
				if (Match("{"))
				{
					Error(Start, "cpp.parse.anonymous_namespace_unsupported",
					    "anonymous namespaces have internal-linkage semantics that the strict frontend AST cannot preserve");
					const std::size_t Close = FindMatching(Current - 1, "{", "}");
					Current = Close == Tokens.size() ? Tokens.size() - 1 : Close + 1;
					return;
				}
				while (Peek().Kind == ETokenKind::Identifier || Check("::"))
				{
					Name += Peek().Text;
					++Current;
				}
				if (Name.empty() || !Match("{"))
				{
					Error(Start, "cpp.parse.invalid_namespace", "expected namespace name and body");
					Synchronize();
					return;
				}
				const std::string QualifiedNamespace = Qualify(OuterScope, Name);
				std::size_t PrefixEnd = 0;
				do
				{
					PrefixEnd = QualifiedNamespace.find("::", PrefixEnd);
					const std::string Prefix = QualifiedNamespace.substr(0, PrefixEnd);
					if (std::find(Unit.Namespaces.begin(), Unit.Namespaces.end(), Prefix) == Unit.Namespaces.end())
					{
						Unit.Namespaces.push_back(Prefix);
					}
					if (PrefixEnd != std::string::npos)
					{
						PrefixEnd += 2;
					}
				}
				while (PrefixEnd != std::string::npos);
				ParseScope(QualifiedNamespace, true);
			}

			void ParseUsing(
			    const std::string& Scope,
			    std::vector<FTemplateParameter> TemplateParameters = {})
			{
				const FTemplateScope ParameterScope(ActiveTemplateParameters, TemplateParameters);
				const FToken Start = Tokens[Current++];
				if (Match("namespace"))
				{
					if (!TemplateParameters.empty())
					{
						Error(Start, "cpp.parse.templated_using_namespace",
						    "using namespace cannot have a template parameter list");
						Synchronize();
						return;
					}
					std::string NamespaceName;
					const FSourceLocation NamespaceLocation = Peek().Location;
					while (!AtEnd() && !Check(";"))
					{
						NamespaceName += Peek().Text;
						++Current;
					}
					if (!Match(";"))
					{
						Error(Start, "cpp.parse.invalid_using_namespace",
						    "expected ';' after using namespace declaration");
					}
					else if (NamespaceName.empty())
					{
						Error(Start, "cpp.parse.invalid_using_namespace",
						    "using namespace requires a namespace name");
					}
					else
					{
						Unit.UsingNamespaces.push_back(
						    {Scope, std::move(NamespaceName), NamespaceLocation});
					}
					return;
				}
				if (Peek().Kind != ETokenKind::Identifier)
				{
					Error(Start, "cpp.parse.invalid_using_alias", "expected alias name after using");
					Synchronize();
					return;
				}
				const std::string Alias = Peek().Text;
				++Current;
				if (!Match("="))
				{
					RejectAndSynchronize("cpp.parse.using_declaration_unsupported",
					    "using declarations are not supported; only using Name = Type is accepted");
					return;
				}
				const std::size_t Begin = Current;
				while (!AtEnd() && !Check(";"))
				{
					++Current;
				}
				if (!Match(";"))
				{
					Error(Start, "cpp.parse.invalid_using_alias", "expected ';' after using alias");
					return;
				}
				const std::string QualifiedAlias = Qualify(Scope, Alias);
				FType Resolved = ResolveAlias(
				    ParseTypeTokens(Slice(Begin, Current - 1)), Scope);
				if (!TemplateParameters.empty())
				{
					if (Aliases.find(QualifiedAlias) != Aliases.end())
					{
						Error(Start, "cpp.parse.conflicting_using_alias",
						    "alias template conflicts with a non-template alias: '" + QualifiedAlias + "'");
						return;
					}
					const auto Existing = std::find_if(Unit.TypeAliases.begin(), Unit.TypeAliases.end(),
					    [&](const FTypeAlias& Value)
					{
						return Value.QualifiedName == QualifiedAlias;
					});
					if (Existing != Unit.TypeAliases.end())
					{
						if (!TemplateParametersCompatible(
						        Existing->TemplateParameters, TemplateParameters) ||
						    TemplateTypeIdentity(Existing->TargetType,
						        Existing->TemplateParameters) != TemplateTypeIdentity(Resolved, TemplateParameters))
						{
							Error(Start, "cpp.parse.conflicting_using_alias",
							    "conflicting alias-template declaration for '" + QualifiedAlias + "'");
						}
						return;
					}
					FTypeAlias TypeAlias;
					TypeAlias.Name = Alias;
					TypeAlias.QualifiedName = QualifiedAlias;
					TypeAlias.TargetType = std::move(Resolved);
					TypeAlias.TemplateParameters = std::move(TemplateParameters);
					TypeAlias.Location = Start.Location;
					Unit.TypeAliases.push_back(std::move(TypeAlias));
					AliasLocations.emplace(QualifiedAlias, Start.Location);
					return;
				}
				if (std::any_of(Unit.TypeAliases.begin(), Unit.TypeAliases.end(),
				        [&](const FTypeAlias& Value)
				{
					return Value.QualifiedName == QualifiedAlias && !Value.TemplateParameters.empty();
				}))
				{
					Error(Start, "cpp.parse.conflicting_using_alias",
					    "non-template alias conflicts with an alias template: '" + QualifiedAlias + "'");
					return;
				}
				if (const auto Existing = Aliases.find(QualifiedAlias);
				    Existing != Aliases.end())
				{
					if (TypeIdentity(Existing->second) != TypeIdentity(Resolved))
					{
						Error(Start, "cpp.parse.conflicting_using_alias",
						    "conflicting using-alias declaration for '" + QualifiedAlias + "'");
					}
					return;
				}
				Aliases.emplace(QualifiedAlias, std::move(Resolved));
				AliasLocations.emplace(QualifiedAlias, Start.Location);
				FTypeAlias TypeAlias;
				TypeAlias.Name = Alias;
				TypeAlias.QualifiedName = QualifiedAlias;
				TypeAlias.TargetType = Aliases.at(QualifiedAlias);
				TypeAlias.TemplateParameters = std::move(TemplateParameters);
				TypeAlias.Location = Start.Location;
				Unit.TypeAliases.push_back(std::move(TypeAlias));
			}

			void ParseTypedef(const std::string& Scope)
			{
				const FToken Start = Tokens[Current++];
				const std::size_t Begin = Current;
				while (!AtEnd() && !Check(";"))
				{
					++Current;
				}
				const std::size_t End = Current;
				if (!Match(";"))
				{
					Error(Start, "cpp.parse.invalid_typedef", "expected ';' after typedef declaration");
					return;
				}
				if (End <= Begin || Tokens[End - 1].Kind != ETokenKind::Identifier)
				{
					Error(Start, "cpp.parse.invalid_typedef",
					    "typedef requires a type followed by an alias name");
					return;
				}
				for (std::size_t Index = Begin; Index + 1 < End; ++Index)
				{
					if (Tokens[Index].Text == "," || Tokens[Index].Text == "[" || Tokens[Index].Text == "]" || Tokens[Index].Text == "(" || Tokens[Index].Text == ")")
					{
						Error(Tokens[Index], "cpp.parse.typedef_declarator_unsupported",
						    "function, array, grouped, and multi-declarator typedefs require an explicit using alias");
						return;
					}
				}
				const std::string Alias = Tokens[End - 1].Text;
				if (End - 1 == Begin)
				{
					Error(Start, "cpp.parse.invalid_typedef", "typedef target type is missing");
					return;
				}
				const std::string QualifiedAlias = Qualify(Scope, Alias);
				FType Resolved = ResolveAlias(
				    ParseTypeTokens(Slice(Begin, End - 1)), Scope);
				if (std::any_of(Unit.TypeAliases.begin(), Unit.TypeAliases.end(),
				        [&](const FTypeAlias& Value)
				{
					return Value.QualifiedName == QualifiedAlias && !Value.TemplateParameters.empty();
				}))
				{
					Error(Start, "cpp.parse.conflicting_using_alias",
					    "typedef conflicts with an alias template: '" + QualifiedAlias + "'");
					return;
				}
				if (const auto Existing = Aliases.find(QualifiedAlias);
				    Existing != Aliases.end())
				{
					if (TypeIdentity(Existing->second) != TypeIdentity(Resolved))
					{
						Error(Start, "cpp.parse.conflicting_using_alias",
						    "conflicting typedef declaration for '" + QualifiedAlias + "'");
					}
					return;
				}
				Aliases.emplace(QualifiedAlias, std::move(Resolved));
				AliasLocations.emplace(QualifiedAlias, Start.Location);
				FTypeAlias TypeAlias;
				TypeAlias.Name = Alias;
				TypeAlias.QualifiedName = QualifiedAlias;
				TypeAlias.TargetType = Aliases.at(QualifiedAlias);
				TypeAlias.Location = Start.Location;
				Unit.TypeAliases.push_back(std::move(TypeAlias));
			}

			void ParseEnum(
			    const std::string& Scope,
			    std::vector<FAttribute> Attributes = {})
			{
				const FToken Start = Tokens[Current++];
				FEnum Enum;
				Enum.Location = Start.Location;
				Enum.bScoped = Match("class") || Match("struct");
				if (Check("[[") && !ParseAttributeSequence(Tokens, Current, Attributes))
				{
					Synchronize();
					return;
				}
				Enum.Attributes = std::move(Attributes);
				if (Peek().Kind != ETokenKind::Identifier)
				{
					Error(Peek(), "cpp.parse.enum_requires_name", "anonymous enums are not supported");
					Synchronize();
					return;
				}
				Enum.Name = Peek().Text;
				Enum.QualifiedName = Qualify(Scope, Enum.Name);
				NonAliasTypeNames.insert(Enum.QualifiedName);
				++Current;
				if (Check("[[") && !ParseAttributeSequence(Tokens, Current, Enum.Attributes))
				{
					Synchronize();
					return;
				}
				const bool bHasExplicitUnderlyingType = Match(":");
				Enum.bHasExplicitUnderlyingType = bHasExplicitUnderlyingType;
				if (bHasExplicitUnderlyingType)
				{
					const std::size_t TypeBegin = Current;
					while (!AtEnd() && !Check("{") && !Check(";"))
					{
						++Current;
					}
					const FType Underlying = ResolveAlias(
					    ParseTypeTokens(Slice(TypeBegin, Current)), Scope);
					const FTypeMapResult Mapped = MapTypeToVeroScript(Underlying);
					if (!Mapped.bSuccess)
					{
						Error(Start, Mapped.ErrorCode, Mapped.ErrorMessage);
					}
					else if (IsSupportedEnumUnderlyingType(Mapped.VeroType))
					{
						Enum.UnderlyingType = Mapped.VeroType;
					}
					else
					{
						Error(Start, "cpp.type.enum_underlying_unsupported",
						    "enum underlying type cannot be represented by VeroScript: " + Mapped.VeroType);
					}
				}
				if (Match(";"))
				{
					if (!Enum.bScoped && !bHasExplicitUnderlyingType)
					{
						Error(Start, "cpp.parse.unscoped_enum_forward_requires_underlying_type",
						    "an unscoped enum forward declaration requires an explicit underlying type");
						return;
					}
					Enum.bForwardDeclaration = true;
					CommitEnum(std::move(Enum));
					return;
				}
				if (!Match("{"))
				{
					Error(Peek(), "cpp.parse.enum_requires_body", "expected enum body or a valid forward declaration");
					Synchronize();
					return;
				}
				while (!AtEnd() && !Check("}"))
				{
					if (Peek().Kind != ETokenKind::Identifier)
					{
						Error(Peek(), "cpp.parse.invalid_enumerator", "expected enum member name");
						SynchronizeTo("}");
						break;
					}
					FEnumValue Value;
					Value.Name = Peek().Text;
					Value.Location = Peek().Location;
					++Current;
					if (Check("[[") && !ParseAttributeSequence(Tokens, Current, Value.Attributes))
					{
						SynchronizeTo("}");
						break;
					}
					if (Match("="))
					{
						const std::size_t Begin = Current;
						int Depth = 0;
						while (!AtEnd())
						{
							if ((Check(",") || Check("}")) && Depth == 0)
							{
								break;
							}
							// In an expression '<' and '>' may be relational or shift
							// operators. Only grouping delimiters determine where this
							// enumerator initializer ends.
							Depth += Peek().Text == "(" || Peek().Text == "[" || Peek().Text == "{" ? 1 : 0;
							Depth -= Peek().Text == ")" || Peek().Text == "]" || Peek().Text == "}" ? 1 : 0;
							++Current;
						}
						Value.Initializer = JoinTokenRange(Tokens, Begin, Current);
						Value.InitializerTokens = Slice(Begin, Current);
					}
					Enum.Values.push_back(std::move(Value));
					if (!Match(",") && !Check("}"))
					{
						Error(Peek(), "cpp.parse.expected_enum_separator", "expected ',' or '}' after enum member");
						SynchronizeTo("}");
					}
				}
				if (!Match("}"))
				{
					Error(Peek(), "cpp.parse.expected_close_brace", "expected '}' after enum body");
					return;
				}
				if (!Match(";"))
				{
					Error(Peek(), "cpp.parse.enum_requires_semicolon",
					    "a C++ enum definition must end with ';'");
				}
				CommitEnum(std::move(Enum));
			}

			void ParseRecord(
			    const std::string& Scope,
			    std::vector<FTemplateParameter> TemplateParameters = {},
			    std::vector<FAttribute> Attributes = {})
			{
				const FTemplateScope ParameterScope(ActiveTemplateParameters, TemplateParameters);
				const FToken Start = Tokens[Current];
				const bool bClass = Match("class");
				if (!bClass)
				{
					Match("struct");
				}
				if (Check("[[") && !ParseAttributeSequence(Tokens, Current, Attributes))
				{
					Synchronize();
					return;
				}
				if (Peek().Kind != ETokenKind::Identifier)
				{
					Error(Peek(), "cpp.parse.record_requires_name", "anonymous records are not supported");
					Synchronize();
					return;
				}
				FRecord Record;
				Record.Name = Peek().Text;
				Record.QualifiedName = Qualify(Scope, Record.Name);
				NonAliasTypeNames.insert(Record.QualifiedName);
				Record.Location = Start.Location;
				Record.Kind = bClass ? ERecordKind::Class : ERecordKind::Struct;
				Record.Attributes = std::move(Attributes);
				Record.TemplateParameters = std::move(TemplateParameters);
				++Current;
				if (Check("[[") && !ParseAttributeSequence(Tokens, Current, Record.Attributes))
				{
					Synchronize();
					return;
				}
				Record.bFinal = Match("final");
				if (Match(":"))
				{
					while (!AtEnd() && !Check("{"))
					{
						FBaseType ParsedBase;
						ParsedBase.Location = Peek().Location;
						ParsedBase.Access = bClass ? EAccess::Private : EAccess::Public;
						std::vector<FToken> Base;
						int Depth = 0;
						while (!AtEnd() && !(Check(",") && Depth == 0) && !Check("{"))
						{
							if (Peek().Text == "public")
							{
								ParsedBase.Access = EAccess::Public;
							}
							else if (Peek().Text == "protected")
							{
								ParsedBase.Access = EAccess::Protected;
							}
							else if (Peek().Text == "private")
							{
								ParsedBase.Access = EAccess::Private;
							}
							else if (Peek().Text == "virtual")
							{
								ParsedBase.bVirtual = true;
							}
							else
							{
								Base.push_back(Peek());
							}
							Depth += Peek().Text == "<" ? 1 : 0;
							Depth -= Peek().Text == ">" ? 1 : 0;
							++Current;
						}
						if (!Base.empty())
						{
							ParsedBase.Type = ResolveAlias(ParseTypeTokens(Base), Scope);
							ParsedBase.TypeName = TypeIdentity(ParsedBase.Type);
							Record.BaseTypes.push_back(std::move(ParsedBase));
						}
						Match(",");
					}
				}
				if (Match(";"))
				{
					if (Record.bFinal || !Record.BaseTypes.empty())
					{
						Error(Start, "cpp.parse.invalid_record_forward_declaration",
						    "a record forward declaration cannot be final or declare base classes");
						return;
					}
					Record.bForwardDeclaration = true;
					CommitRecord(std::move(Record));
					return;
				}
				if (!Match("{"))
				{
					Error(Peek(), "cpp.parse.record_requires_body", "expected record body or forward declaration");
					Synchronize();
					return;
				}
				EAccess Access = bClass ? EAccess::Private : EAccess::Public;
				while (!AtEnd() && !Check("}"))
				{
					if ((Check("public") || Check("protected") || Check("private")) && Peek(1).Text == ":")
					{
						Access = Check("public") ? EAccess::Public
						    : Check("protected") ? EAccess::Protected
						                         : EAccess::Private;
						Current += 2;
						continue;
					}
					std::vector<FAttribute> MemberAttributes;
					if (Check("[[") && !ParseAttributeSequence(Tokens, Current, MemberAttributes))
					{
						Synchronize();
						continue;
					}
					if (Check("template"))
					{
						std::vector<FTemplateParameter> MethodTemplateParameters;
						if (!ParseTemplateParameters(Record.QualifiedName, MethodTemplateParameters))
						{
							Synchronize();
							continue;
						}
						if (Check("class") || Check("struct") || Check("enum") || Check("using") || Check("typedef") || Check("union"))
						{
							RejectAndSynchronize("cpp.parse.nested_template_declaration_unsupported",
							    "nested template types and aliases are not supported");
							continue;
						}
						if (Check("[[") && !ParseAttributeSequence(Tokens, Current, MemberAttributes))
						{
							Synchronize();
							continue;
						}
						ParseMember(Record, Access, std::move(MethodTemplateParameters),
						    std::move(MemberAttributes));
						continue;
					}
					if (Check("using") || Check("typedef") || Check("class") || Check("struct") || Check("enum") || Check("union"))
					{
						RejectAndSynchronize("cpp.parse.nested_declaration_unsupported",
						    "nested types, aliases, and templates are not supported by the initial strict subset");
						continue;
					}
					ParseMember(Record, Access, {}, std::move(MemberAttributes));
				}
				if (!Match("}"))
				{
					Error(Peek(), "cpp.parse.expected_close_brace", "expected '}' after record body");
					return;
				}
				if (!Match(";"))
				{
					Error(Peek(), "cpp.parse.record_requires_semicolon",
					    "a C++ class or struct definition must end with ';'");
				}
				CommitRecord(std::move(Record));
			}

			void CommitEnum(FEnum Enum)
			{
				if (const auto Cross = std::find_if(Unit.Records.begin(), Unit.Records.end(),
				        [&](const FRecord& Record)
				{
					return Record.QualifiedName == Enum.QualifiedName;
				});
				    Cross != Unit.Records.end())
				{
					Error({ETokenKind::Identifier, Enum.Name, Enum.Location, Enum.Location.Offset},
					    "cpp.parse.conflicting_type_declaration",
					    "type is declared as both an enum and a record: " + Enum.QualifiedName);
					return;
				}
				auto Existing = std::find_if(Unit.Enums.begin(), Unit.Enums.end(),
				    [&](const FEnum& Value)
				{
					return Value.QualifiedName == Enum.QualifiedName;
				});
				if (Existing == Unit.Enums.end())
				{
					Unit.Enums.push_back(std::move(Enum));
					return;
				}
				if (Existing->bScoped != Enum.bScoped || Existing->UnderlyingType != Enum.UnderlyingType || (!Enum.bScoped && Existing->bHasExplicitUnderlyingType != Enum.bHasExplicitUnderlyingType))
				{
					Error({ETokenKind::Identifier, Enum.Name, Enum.Location, Enum.Location.Offset},
					    "cpp.parse.conflicting_enum_declaration",
					    "enum forward declaration and definition disagree: " + Enum.QualifiedName);
					return;
				}
				if (Existing->bForwardDeclaration && !Enum.bForwardDeclaration)
				{
					MergeAttributes(Enum.Attributes, Existing->Attributes);
					*Existing = std::move(Enum);
				}
				else if (Existing->bForwardDeclaration && Enum.bForwardDeclaration)
				{
					MergeAttributes(Existing->Attributes, Enum.Attributes);
				}
				else if (!Existing->bForwardDeclaration && !Enum.bForwardDeclaration)
				{
					Error({ETokenKind::Identifier, Enum.Name, Enum.Location, Enum.Location.Offset},
					    "cpp.parse.duplicate_enum_definition",
					    "duplicate enum definition: " + Enum.QualifiedName);
				}
			}

			void CommitRecord(FRecord Record)
			{
				if (const auto Cross = std::find_if(Unit.Enums.begin(), Unit.Enums.end(),
				        [&](const FEnum& Enum)
				{
					return Enum.QualifiedName == Record.QualifiedName;
				});
				    Cross != Unit.Enums.end())
				{
					Error({ETokenKind::Identifier, Record.Name, Record.Location, Record.Location.Offset},
					    "cpp.parse.conflicting_type_declaration",
					    "type is declared as both a record and an enum: " + Record.QualifiedName);
					return;
				}
				auto Existing = std::find_if(Unit.Records.begin(), Unit.Records.end(),
				    [&](const FRecord& Value)
				{
					return Value.QualifiedName == Record.QualifiedName;
				});
				if (Existing == Unit.Records.end())
				{
					Unit.Records.push_back(std::move(Record));
					return;
				}
				if (!TemplateParametersCompatible(
				        Existing->TemplateParameters, Record.TemplateParameters))
				{
					Error({ETokenKind::Identifier, Record.Name, Record.Location, Record.Location.Offset},
					    "cpp.parse.conflicting_record_template_parameters",
					    "record declarations have incompatible template parameter lists: " + Record.QualifiedName);
					return;
				}
				if (Existing->bForwardDeclaration && !Record.bForwardDeclaration)
				{
					for (std::size_t Index = 0; Index < Record.TemplateParameters.size(); ++Index)
					{
						FTemplateParameter& Target = Record.TemplateParameters[Index];
						const FTemplateParameter& Source = Existing->TemplateParameters[Index];
						if (Target.bHasDefault && Source.bHasDefault)
						{
							Error({ETokenKind::Identifier, Record.Name, Record.Location, Record.Location.Offset},
							    "cpp.parse.duplicate_template_default_argument",
							    "template default argument is repeated for record: " + Record.QualifiedName);
							return;
						}
						if (!Target.bHasDefault && Source.bHasDefault)
						{
							Target.DefaultType = RemapTemplateType(Source.DefaultType,
							    Existing->TemplateParameters, Record.TemplateParameters);
							Target.bHasDefault = true;
						}
					}
					MergeAttributes(Record.Attributes, Existing->Attributes);
					*Existing = std::move(Record);
				}
				else if (Existing->bForwardDeclaration && Record.bForwardDeclaration)
				{
					for (std::size_t Index = 0; Index < Existing->TemplateParameters.size(); ++Index)
					{
						FTemplateParameter& Target = Existing->TemplateParameters[Index];
						const FTemplateParameter& Source = Record.TemplateParameters[Index];
						if (Target.bHasDefault && Source.bHasDefault)
						{
							Error({ETokenKind::Identifier, Record.Name, Record.Location, Record.Location.Offset},
							    "cpp.parse.duplicate_template_default_argument",
							    "template default argument is repeated for record: " + Record.QualifiedName);
							return;
						}
						if (!Target.bHasDefault && Source.bHasDefault)
						{
							Target.DefaultType = RemapTemplateType(Source.DefaultType,
							    Record.TemplateParameters, Existing->TemplateParameters);
							Target.bHasDefault = true;
						}
					}
					MergeAttributes(Existing->Attributes, Record.Attributes);
				}
				else if (!Existing->bForwardDeclaration && !Record.bForwardDeclaration)
				{
					Error({ETokenKind::Identifier, Record.Name, Record.Location, Record.Location.Offset},
					    "cpp.parse.duplicate_record_definition",
					    "duplicate record definition: " + Record.QualifiedName);
				}
			}

			void ParseMember(
			    FRecord& Record,
			    const EAccess Access,
			    std::vector<FTemplateParameter> TemplateParameters = {},
			    std::vector<FAttribute> Attributes = {})
			{
				const FTemplateScope ParameterScope(ActiveTemplateParameters, TemplateParameters);
				const std::size_t Begin = Current;
				if (Check("friend"))
				{
					if (!Attributes.empty())
					{
						ErrorAt(Attributes.front().Location,
						    "cpp.parse.friend_attribute_unsupported",
						    "attributes on friend declarations are not represented by the strict frontend AST");
					}
					int ParenDepth = 0;
					int AngleDepth = 0;
					std::size_t End = Current;
					while (End < Tokens.size())
					{
						const std::string& Text = Tokens[End].Text;
						if (Text == "(")
						{
							++ParenDepth;
						}
						else if (Text == ")" && ParenDepth > 0)
						{
							--ParenDepth;
						}
						else if (Text == "<")
						{
							++AngleDepth;
						}
						else if (Text == ">" && AngleDepth > 0)
						{
							--AngleDepth;
						}
						if (ParenDepth == 0 && AngleDepth == 0 && (Text == ";" || Text == "{" || Text == "}"))
						{
							break;
						}
						++End;
					}
					if (End >= Tokens.size() || Tokens[End].Text != ";")
					{
						Error(Tokens[Begin], "cpp.parse.friend_definition_unsupported",
						    "friend declarations are supported, but an inline friend function definition cannot be moved safely to namespace scope");
						Synchronize();
						return;
					}
					Record.FriendDeclarations.push_back(Slice(Begin, End));
					Current = End + 1;
					return;
				}
				std::size_t Paren = Tokens.size();
				std::size_t Terminator = Tokens.size();
				int AngleDepth = 0;
				int ParenDepth = 0;
				for (std::size_t Index = Current; Index < Tokens.size(); ++Index)
				{
					const std::string& Text = Tokens[Index].Text;
					const bool bOperatorAngle = Index > Begin && Tokens[Index - 1].Text == "operator";
					if (Text == "<" && !bOperatorAngle)
					{
						++AngleDepth;
					}
					else if (Text == ">" && AngleDepth > 0)
					{
						--AngleDepth;
					}
					else if (Text == "(" && AngleDepth == 0)
					{
						// The empty pair in operator() belongs to the name, not its parameters.
						if (ParenDepth == 0 && Index > Begin && Tokens[Index - 1].Text == "operator" && Index + 1 < Tokens.size() && Tokens[Index + 1].Text == ")")
						{
							++Index;
							continue;
						}
						if (ParenDepth++ == 0 && Paren == Tokens.size())
						{
							Paren = Index;
						}
					}
					else if (Text == ")" && ParenDepth > 0)
					{
						--ParenDepth;
					}
					if (ParenDepth == 0 && AngleDepth == 0 && (Text == ";" || Text == "{" || Text == "}"))
					{
						Terminator = Index;
						break;
					}
				}
				if (Terminator == Tokens.size() || Terminator == Begin)
				{
					Error(Peek(), "cpp.parse.invalid_member", "could not parse record member declaration");
					Synchronize();
					return;
				}
				if (Paren < Terminator)
				{
					FFunction Function;
					if (!ParseFunction(Begin, Paren, Terminator, Record.Name,
					        Record.QualifiedName, Access, Function))
					{
						Synchronize();
						return;
					}
					Function.TemplateParameters = std::move(TemplateParameters);
					Function.Attributes.insert(Function.Attributes.begin(),
					    std::make_move_iterator(Attributes.begin()),
					    std::make_move_iterator(Attributes.end()));
					Record.Methods.push_back(std::move(Function));
					return;
				}
				if (!TemplateParameters.empty())
				{
					Error(Tokens[Begin], "cpp.parse.variable_template_unsupported",
					    "variable templates cannot be mapped to VeroScript fields");
					Synchronize();
					return;
				}
				std::vector<FField> Fields;
				if (TryParseSimpleFieldOrVariableList(Begin, Terminator, Access,
				        Record.QualifiedName, true, Fields))
				{
					for (FField& Field : Fields)
					{
						if (Field.bExtern)
						{
							Error(Tokens[Begin], "cpp.parse.extern_data_member",
							    "a C++ non-function data member cannot be declared extern");
							continue;
						}
						Field.Attributes.insert(Field.Attributes.begin(),
						    Attributes.begin(), Attributes.end());
						Record.Fields.push_back(std::move(Field));
					}
					Current = Terminator + 1;
					return;
				}
				FField Field;
				if (!ParseFieldOrVariable(Begin, Terminator, Access,
				        Record.QualifiedName, true, Field))
				{
					Synchronize();
					return;
				}
				if (Field.bExtern)
				{
					Error(Tokens[Begin], "cpp.parse.extern_data_member",
					    "a C++ non-function data member cannot be declared extern");
					Current = Terminator + 1;
					return;
				}
				Field.Attributes.insert(Field.Attributes.begin(),
				    std::make_move_iterator(Attributes.begin()),
				    std::make_move_iterator(Attributes.end()));
				Record.Fields.push_back(std::move(Field));
				Current = Terminator + 1;
			}

			void ParseFreeDeclaration(
			    const std::string& Scope,
			    std::vector<FTemplateParameter> TemplateParameters = {},
			    std::vector<FAttribute> Attributes = {})
			{
				const FTemplateScope ParameterScope(ActiveTemplateParameters, TemplateParameters);
				const std::size_t Begin = Current;
				std::size_t Paren = Tokens.size();
				std::size_t Terminator = Tokens.size();
				int AngleDepth = 0;
				int ParenDepth = 0;
				for (std::size_t Index = Current; Index < Tokens.size(); ++Index)
				{
					const std::string& Text = Tokens[Index].Text;
					const bool bOperatorAngle = Index > Begin && Tokens[Index - 1].Text == "operator";
					if (Text == "<" && !bOperatorAngle)
					{
						++AngleDepth;
					}
					else if (Text == ">" && AngleDepth > 0)
					{
						--AngleDepth;
					}
					else if (Text == "(" && AngleDepth == 0)
					{
						// Qualified out-of-class operator() definitions use the same name pair.
						if (ParenDepth == 0 && Index > Begin && Tokens[Index - 1].Text == "operator" && Index + 1 < Tokens.size() && Tokens[Index + 1].Text == ")")
						{
							++Index;
							continue;
						}
						if (ParenDepth++ == 0 && Paren == Tokens.size())
						{
							Paren = Index;
						}
					}
					else if (Text == ")" && ParenDepth > 0)
					{
						--ParenDepth;
					}
					if (ParenDepth == 0 && AngleDepth == 0 && (Text == ";" || Text == "{" || Text == "}"))
					{
						Terminator = Index;
						break;
					}
				}
				if (Terminator == Tokens.size() || Terminator == Begin || Tokens[Terminator].Text == "}")
				{
					Error(Peek(), "cpp.parse.invalid_declaration", "could not parse top-level declaration");
					Synchronize();
					return;
				}
				if (Paren < Terminator)
				{
					FFunction Function;
					if (ParseFunction(Begin, Paren, Terminator, {}, Scope, EAccess::Public, Function))
					{
						Function.TemplateParameters = std::move(TemplateParameters);
						Function.Attributes.insert(Function.Attributes.begin(),
						    std::make_move_iterator(Attributes.begin()),
						    std::make_move_iterator(Attributes.end()));
						Unit.Functions.push_back(std::move(Function));
					}
					else
					{
						Synchronize();
					}
					return;
				}

				if (TryParseOutOfClassStaticField(
				        Begin, Terminator, Scope, Attributes))
				{
					return;
				}
				if (!TemplateParameters.empty())
				{
					Error(Tokens[Begin], "cpp.parse.variable_template_unsupported",
					    "variable templates cannot be mapped to VeroScript globals");
					Synchronize();
					return;
				}
				std::vector<FField> Fields;
				if (TryParseSimpleFieldOrVariableList(Begin, Terminator,
				        EAccess::Public, Scope, false, Fields))
				{
					for (FField& Field : Fields)
					{
						FVariable Variable;
						Variable.Name = Field.Name;
						Variable.QualifiedName = Qualify(Scope, Field.Name);
						Variable.Type = std::move(Field.Type);
						Variable.InitializerTokens = std::move(Field.InitializerTokens);
						Variable.Attributes = Attributes;
						Variable.Attributes.insert(Variable.Attributes.end(),
						    std::make_move_iterator(Field.Attributes.begin()),
						    std::make_move_iterator(Field.Attributes.end()));
						Variable.Location = Field.Location;
						Variable.bStatic = Field.bStatic;
						Variable.bInline = Field.bInline;
						Variable.bExtern = Field.bExtern;
						Variable.bConstexpr = Field.bConstexpr;
						Variable.bConstinit = Field.bConstinit;
						Unit.Variables.push_back(std::move(Variable));
					}
					Current = Terminator + 1;
					return;
				}

				FField Field;
				if (!ParseFieldOrVariable(Begin, Terminator, EAccess::Public, Scope,
				        false, Field))
				{
					Synchronize();
					return;
				}
				FVariable Variable;
				Variable.Name = Field.Name;
				Variable.QualifiedName = Qualify(Scope, Field.Name);
				Variable.Type = std::move(Field.Type);
				Variable.InitializerTokens = std::move(Field.InitializerTokens);
				Variable.Attributes = std::move(Attributes);
				Variable.Attributes.insert(Variable.Attributes.end(),
				    std::make_move_iterator(Field.Attributes.begin()),
				    std::make_move_iterator(Field.Attributes.end()));
				Variable.Location = Field.Location;
				Variable.bStatic = Field.bStatic;
				Variable.bInline = Field.bInline;
				Variable.bExtern = Field.bExtern;
				Variable.bConstexpr = Field.bConstexpr;
				Variable.bConstinit = Field.bConstinit;
				Unit.Variables.push_back(std::move(Variable));
				Current = Terminator + 1;
			}

			bool TryParseOutOfClassStaticField(
			    const std::size_t Begin,
			    const std::size_t Terminator,
			    const std::string& Scope,
			    std::vector<FAttribute>& Attributes)
			{
				std::size_t DeclaratorEnd = Terminator;
				int Nested = 0;
				for (std::size_t Index = Begin; Index < Terminator; ++Index)
				{
					Nested += IsOpening(Tokens[Index].Text) ? 1 : 0;
					Nested -= IsClosing(Tokens[Index].Text) ? 1 : 0;
					if (Nested == 0 && Tokens[Index].Text == "=")
					{
						DeclaratorEnd = Index;
						break;
					}
				}
				if (DeclaratorEnd < Begin + 3 || Tokens[DeclaratorEnd - 1].Kind != ETokenKind::Identifier || Tokens[DeclaratorEnd - 2].Text != "::")
				{
					return false;
				}
				const std::size_t NameAt = DeclaratorEnd - 1;
				const std::size_t Separator = DeclaratorEnd - 2;
				if (Separator <= Begin || Tokens[Separator - 1].Kind != ETokenKind::Identifier)
				{
					return false;
				}
				std::size_t OwnerBegin = Separator - 1;
				while (OwnerBegin >= Begin + 2 && Tokens[OwnerBegin - 1].Text == "::" && Tokens[OwnerBegin - 2].Kind == ETokenKind::Identifier)
				{
					OwnerBegin -= 2;
				}
				if (OwnerBegin == Begin)
				{
					Error(Tokens[Begin], "cpp.parse.static_field_type_required",
					    "out-of-class static data-member definition requires a type");
					Current = Terminator + 1;
					return true;
				}
				std::vector<FToken> TypeTokens = Slice(Begin, OwnerBegin);
				FVariable Variable;
				Variable.Location = Tokens[Begin].Location;
				Variable.Name = Tokens[NameAt].Text;
				const std::string Owner = JoinTokenRange(Tokens, OwnerBegin, Separator);
				Variable.OwnerQualifiedName = Scope.empty() ? Owner : Qualify(Scope, Owner);
				Variable.QualifiedName = Variable.OwnerQualifiedName + "::" + Variable.Name;
				Variable.bOutOfClassMember = true;
				for (const FToken& Token : TypeTokens)
				{
					if (Token.Text == "static" || Token.Text == "extern" || Token.Text == "mutable" || Token.Text == "thread_local")
					{
						Error(Token, "cpp.parse.invalid_out_of_class_static_field_specifier",
						    "an out-of-class static data-member definition cannot use " + Token.Text);
						Current = Terminator + 1;
						return true;
					}
					Variable.bInline = Variable.bInline || Token.Text == "inline";
					Variable.bConstexpr = Variable.bConstexpr || Token.Text == "constexpr";
					Variable.bConstinit = Variable.bConstinit || Token.Text == "constinit";
				}
				TypeTokens.erase(std::remove_if(TypeTokens.begin(), TypeTokens.end(),
				                     [](const FToken& Token)
				{
					return IsDeclModifier(Token.Text);
				}),
				    TypeTokens.end());
				if (TypeTokens.empty())
				{
					Error(Tokens[Begin], "cpp.parse.static_field_type_required",
					    "out-of-class static data-member definition requires a type");
					Current = Terminator + 1;
					return true;
				}
				Variable.Type = ResolveAlias(ParseTypeTokens(TypeTokens),
				    Variable.OwnerQualifiedName);
				if (DeclaratorEnd < Terminator)
				{
					if (DeclaratorEnd + 1 == Terminator)
					{
						Error(Tokens[DeclaratorEnd], "cpp.parse.variable_initializer_required",
						    "'=' requires an initializer expression");
					}
					else
					{
						Variable.InitializerTokens = Slice(DeclaratorEnd + 1, Terminator);
					}
				}
				Variable.Attributes = std::move(Attributes);
				Unit.Variables.push_back(std::move(Variable));
				Current = Terminator + 1;
				return true;
			}

			bool ParseFunction(
			    const std::size_t Begin,
			    const std::size_t OpenParen,
			    const std::size_t Terminator,
			    const std::string& OwnerLeaf,
			    const std::string& Scope,
			    const EAccess Access,
			    FFunction& Out)
			{
				Out.Location = Tokens[Begin].Location;
				Out.Access = Access;
				std::string EffectiveOwnerLeaf = OwnerLeaf;
				std::string EffectiveScope = Scope;
				std::size_t NameBegin = OpenParen;
				if (NameBegin == Begin)
				{
					Error(Tokens[Begin], "cpp.parse.function_requires_name", "function declaration requires a name");
					return false;
				}
				--NameBegin;
				if (Tokens[NameBegin].Text == ">")
				{
					Error(Tokens[NameBegin], "cpp.parse.function_templates_unsupported",
					    "function template specializations are not supported");
					return false;
				}

				std::size_t OperatorAt = OpenParen;
				for (std::size_t Index = Begin; Index < OpenParen; ++Index)
				{
					if (Tokens[Index].Text == "operator")
					{
						OperatorAt = Index;
					}
				}
				std::size_t ReturnEnd = NameBegin;
				const auto ParseOutOfClassOwner = [&](const std::size_t Separator,
				                                      std::size_t& OutReturnEnd) -> bool
				{
					if (Separator <= Begin || Tokens[Separator].Text != "::")
					{
						return false;
					}
					if (Tokens[Separator - 1].Text == ">" || Tokens[Separator - 1].Text == ">>")
					{
						Error(Tokens[Separator - 1], "cpp.parse.out_of_class_template_owner_unsupported",
						    "out-of-class definitions for C++ class-template specializations are not yet supported");
						return false;
					}
					if (Tokens[Separator - 1].Kind != ETokenKind::Identifier)
					{
						Error(Tokens[Separator - 1], "cpp.parse.invalid_out_of_class_owner",
						    "qualified function definitions require a namespace or non-template record owner");
						return false;
					}
					std::size_t OwnerBegin = Separator - 1;
					while (OwnerBegin >= Begin + 2 && Tokens[OwnerBegin - 1].Text == "::")
					{
						if (Tokens[OwnerBegin - 2].Text == ">" || Tokens[OwnerBegin - 2].Text == ">>")
						{
							Error(Tokens[OwnerBegin - 2], "cpp.parse.out_of_class_template_owner_unsupported",
							    "out-of-class definitions for C++ class-template specializations are not yet supported");
							return false;
						}
						if (Tokens[OwnerBegin - 2].Kind != ETokenKind::Identifier)
						{
							break;
						}

						OwnerBegin -= 2;
					}
					for (std::size_t Index = OwnerBegin; Index < Separator; ++Index)
					{
						if (Tokens[Index].Text == "<" || Tokens[Index].Text == ">")
						{
							Error(Tokens[Index], "cpp.parse.out_of_class_template_owner_unsupported",
							    "out-of-class definitions for C++ class-template specializations are not yet supported");
							return false;
						}
					}
					std::string Owner = JoinTokenRange(Tokens, OwnerBegin, Separator);
					if (Owner.empty())
					{
						return false;
					}
					Out.OwnerQualifiedName = Scope.empty() ? Owner : Qualify(Scope, Owner);
					EffectiveOwnerLeaf = LeafName(Out.OwnerQualifiedName);
					EffectiveScope = Out.OwnerQualifiedName;
					Out.bOutOfClassMember = true;
					OutReturnEnd = OwnerBegin;
					return true;
				};
				if (OperatorAt != OpenParen)
				{
					if (OperatorAt > Begin && Tokens[OperatorAt - 1].Text == "::" && !ParseOutOfClassOwner(OperatorAt - 1, ReturnEnd))
					{
						return false;
					}
					NameBegin = OperatorAt;
					if (!Out.bOutOfClassMember)
					{
						ReturnEnd = OperatorAt;
					}
					Out.Name = "operator";
					for (std::size_t Index = OperatorAt + 1; Index < OpenParen; ++Index)
					{
						Out.Name += Tokens[Index].Text;
					}
				}
				else
				{
					const bool bQualifiedDestructor = NameBegin >= Begin + 2 && Tokens[NameBegin - 1].Text == "~" && Tokens[NameBegin - 2].Text == "::";
					if (NameBegin > Begin && Tokens[NameBegin - 1].Text == "::")
					{
						if (!ParseOutOfClassOwner(NameBegin - 1, ReturnEnd))
						{
							return false;
						}
					}
					else if (bQualifiedDestructor)
					{
						if (!ParseOutOfClassOwner(NameBegin - 2, ReturnEnd))
						{
							return false;
						}
					}
					if (Tokens[NameBegin].Kind != ETokenKind::Identifier)
					{
						Error(Tokens[NameBegin], "cpp.parse.function_requires_name", "function declaration requires a simple name");
						return false;
					}
					Out.Name = Tokens[NameBegin].Text;
					if (NameBegin > Begin && Tokens[NameBegin - 1].Text == "~")
					{
						if (EffectiveOwnerLeaf.empty() || Out.Name != EffectiveOwnerLeaf)
						{
							Error(Tokens[NameBegin - 1], "cpp.parse.invalid_destructor_name",
							    "a destructor name must match its owning record");
							return false;
						}
						Out.bDestructor = true;
						Out.Name.insert(Out.Name.begin(), '~');
						if (!Out.bOutOfClassMember)
						{
							ReturnEnd = NameBegin - 1;
						}
					}
				}

				std::vector<FToken> Prefix = Slice(Begin, ReturnEnd);
				for (const FToken& Token : Prefix)
				{
					if (Token.Text == "register" || Token.Text == "mutable" || Token.Text == "constinit" || Token.Text == "friend" || Token.Text == "thread_local" || Token.Text == "[[" || Token.Text == "]]")
					{
						Error(Token, "cpp.parse.invalid_function_specifier",
						    "invalid function declaration specifier: " + Token.Text);
						return false;
					}
					Out.bStatic = Out.bStatic || Token.Text == "static";
					Out.bInline = Out.bInline || Token.Text == "inline";
					Out.bExtern = Out.bExtern || Token.Text == "extern";
					Out.bVirtual = Out.bVirtual || Token.Text == "virtual";
					Out.bExplicit = Out.bExplicit || Token.Text == "explicit";
					Out.bConstexpr = Out.bConstexpr || Token.Text == "constexpr";
					Out.bConsteval = Out.bConsteval || Token.Text == "consteval";
				}
				Prefix.erase(std::remove_if(Prefix.begin(), Prefix.end(),
				                 [](const FToken& Token)
				{
					return IsDeclModifier(Token.Text);
				}),
				    Prefix.end());

				Out.bConstructor = !EffectiveOwnerLeaf.empty() && Out.Name == EffectiveOwnerLeaf;
				if (Out.bConstructor || Out.bDestructor)
				{
					if (!Prefix.empty())
					{
						Error(Tokens[Begin],
						    Out.bConstructor ? "cpp.parse.constructor_return_type"
						                     : "cpp.parse.destructor_return_type",
						    Out.bConstructor ? "constructor must not declare a return type"
						                     : "destructor must not declare a return type");
						return false;
					}
					Out.ReturnType.BaseName = "void";
					Out.ReturnType.Spelling = "void";
				}
				else
				{
					if (Prefix.empty())
					{
						Error(Tokens[Begin], "cpp.parse.function_return_type_required",
						    "function requires an explicit return type");
						return false;
					}
					Out.ReturnType = ResolveAlias(ParseTypeTokens(Prefix), EffectiveScope);
				}
				Out.QualifiedName = Qualify(EffectiveScope, Out.Name);

				std::size_t CloseParen = FindMatching(OpenParen, "(", ")");
				if (CloseParen == Tokens.size() || CloseParen > Terminator)
				{
					Error(Tokens[OpenParen], "cpp.parse.expected_close_paren", "expected ')' after function parameters");
					return false;
				}
				if (!ParseParameters(OpenParen + 1, CloseParen, EffectiveScope, Out.Parameters))
				{
					return false;
				}
				bool bHasSpecialFunctionSuffix = false;
				std::size_t Index = CloseParen + 1;
				while (Index < Terminator)
				{
					const std::string& Text = Tokens[Index].Text;
					if (Text == "[[")
					{
						if (!ParseAttributeSequence(Tokens, Index, Out.Attributes))
						{
							return false;
						}
						continue;
					}
					if (Text == "const")
					{
						Out.bConstMethod = true;
					}
					else if (Text == "override")
					{
						Out.bOverride = true;
					}
					else if (Text == "final")
					{
						Out.bFinal = true;
					}
					else if (Text == "noexcept")
					{
						Out.bNoexcept = true;
					}
					else if (Text == "=" && Index + 1 < Terminator)
					{
						if (bHasSpecialFunctionSuffix)
						{
							Error(Tokens[Index], "cpp.parse.duplicate_function_suffix",
							    "a function declaration may have only one '= delete', '= default', or '= 0' suffix");
							return false;
						}
						bHasSpecialFunctionSuffix = true;
						const std::string& Value = Tokens[++Index].Text;
						Out.bDeleted = Value == "delete";
						Out.bDefaulted = Value == "default";
						Out.bPureVirtual = Value == "0";
						if (!Out.bDeleted && !Out.bDefaulted && !Out.bPureVirtual)
						{
							Error(Tokens[Index], "cpp.parse.function_suffix_unsupported",
							    "unsupported function declaration suffix after '='");
							return false;
						}
					}
					else if (Text == "&" || Text == "&&")
					{
						Error(Tokens[Index], "cpp.parse.ref_qualified_method_unsupported",
						    "ref-qualified member functions are not supported");
						return false;
					}
					else
					{
						Error(Tokens[Index], "cpp.parse.function_suffix_unsupported",
						    "unsupported function qualifier: " + Text);
						return false;
					}
					++Index;
				}

				const bool bMember = !EffectiveOwnerLeaf.empty();
				if (Out.bConstexpr && Out.bConsteval)
				{
					Error(Tokens[Begin], "cpp.parse.conflicting_constexpr_consteval",
					    "a function cannot be declared both constexpr and consteval");
					return false;
				}
				if (!bMember && (Out.bVirtual || Out.bExplicit || Out.bConstMethod || Out.bOverride || Out.bFinal || Out.bPureVirtual || Out.bDefaulted))
				{
					Error(Tokens[Begin], "cpp.parse.invalid_free_function_specifier",
					    "member-only function specifier used on a free function");
					return false;
				}
				if (Out.bConstructor && (Out.bStatic || Out.bExtern || Out.bVirtual || Out.bConstMethod || Out.bOverride || Out.bFinal || Out.bPureVirtual))
				{
					Error(Tokens[Begin], "cpp.parse.invalid_constructor_specifier",
					    "constructor has an invalid static, virtual, cv, override, final, or pure specifier");
					return false;
				}
				if (Out.bOutOfClassMember && (Out.bStatic || Out.bVirtual || Out.bExplicit || Out.bOverride || Out.bFinal || Out.bPureVirtual || Out.bDeleted))
				{
					Error(Tokens[Begin], "cpp.parse.invalid_out_of_class_specifier",
					    "an out-of-class member definition cannot repeat static, virtual, explicit, override, final, pure, or deleted specifiers");
					return false;
				}
				if (Out.bDestructor && (Out.bStatic || Out.bExtern || Out.bConstMethod || Out.bExplicit || !Out.Parameters.empty()))
				{
					Error(Tokens[Begin], "cpp.parse.invalid_destructor_specifier",
					    "destructor cannot be static, extern, const, explicit, or take parameters");
					return false;
				}
				if (bMember && Out.bExtern)
				{
					Error(Tokens[Begin], "cpp.parse.extern_member_function",
					    "a C++ member function cannot use the extern storage-class specifier");
					return false;
				}
				if (!bMember && Out.bStatic && Out.bExtern)
				{
					Error(Tokens[Begin], "cpp.parse.conflicting_function_linkage",
					    "a free function cannot be declared both static and extern");
					return false;
				}
				if (!Out.bConstructor && Out.bExplicit)
				{
					Error(Tokens[Begin], "cpp.parse.explicit_non_constructor_unsupported",
					    "explicit is supported only for constructors in the strict frontend subset");
					return false;
				}
				if (Out.bStatic && (Out.bVirtual || Out.bConstMethod || Out.bOverride || Out.bFinal || Out.bPureVirtual))
				{
					Error(Tokens[Begin], "cpp.parse.invalid_static_method_specifier",
					    "static methods cannot be virtual, cv-qualified, override, final, or pure virtual");
					return false;
				}
				if (Out.bDefaulted)
				{
					const auto IsOwnerType = [&](const FType& Type)
					{
						std::string Base = Type.BaseName;
						while (Base.rfind("::", 0) == 0)
						{
							Base.erase(0, 2);
						}
						return Type.PointerDepth == 0 && Type.TemplateArguments.empty() && (Base == EffectiveOwnerLeaf || Base == EffectiveScope);
					};
					const bool bDefaultConstructor =
					    Out.bConstructor && Out.Parameters.empty();
					const bool bCopyOrMoveConstructor = Out.bConstructor && Out.Parameters.size() == 1 && IsOwnerType(Out.Parameters[0].Type) && Out.Parameters[0].Type.Reference != EReferenceKind::None;
					const bool bCopyOrMoveAssignment = Out.Name == "operator=" && Out.Parameters.size() == 1 && IsOwnerType(Out.Parameters[0].Type) && IsOwnerType(Out.ReturnType) && Out.ReturnType.Reference == EReferenceKind::LValue;
					const bool bDefaultDestructor = Out.bDestructor;
					if (!bDefaultConstructor && !bCopyOrMoveConstructor && !bDefaultDestructor && !bCopyOrMoveAssignment)
					{
						Error(Tokens[Begin],
						    "cpp.parse.invalid_defaulted_function",
						    "only a supported default, copy, or move special member function may be explicitly defaulted");
						return false;
					}
				}

				if (Tokens[Terminator].Text == "{" && bHasSpecialFunctionSuffix)
				{
					Error(Tokens[Terminator], "cpp.parse.special_function_suffix_with_body",
					    "a function declared '= delete', '= default', or '= 0' cannot also have an inline body");
					return false;
				}

				if (Tokens[Terminator].Text == "{")
				{
					if (!Options.bAllowFunctionBodies)
					{
						Error(Tokens[Terminator], "cpp.parse.function_body_not_allowed",
						    "function bodies are not allowed in this frontend mode");
						return false;
					}
					const std::size_t Close = FindMatching(Terminator, "{", "}");
					if (Close == Tokens.size())
					{
						Error(Tokens[Terminator], "cpp.parse.expected_close_brace", "expected '}' after function body");
						return false;
					}
					Out.BodyTokens = Slice(Terminator + 1, Close);
					Out.bHasBody = true;
					Current = Close + 1;
					Match(";");
				}
				else
				{
					Current = Terminator + 1;
				}
				return true;
			}

			bool ParseParameters(
			    const std::size_t Begin,
			    const std::size_t End,
			    const std::string& Scope,
			    std::vector<FParameter>& Out)
			{
				if (Begin == End)
				{
					return true;
				}
				if (End == Begin + 1 && Tokens[Begin].Text == "void")
				{
					return true;
				}
				std::size_t PartBegin = Begin;
				int Angle = 0;
				int Paren = 0;
				for (std::size_t Index = Begin; Index <= End; ++Index)
				{
					const bool bBoundary = Index == End || (Tokens[Index].Text == "," && Angle == 0 && Paren == 0);
					if (!bBoundary)
					{
						Angle += Tokens[Index].Text == "<" ? 1 : 0;
						Angle -= Tokens[Index].Text == ">" ? 1 : 0;
						Paren += Tokens[Index].Text == "(" ? 1 : 0;
						Paren -= Tokens[Index].Text == ")" ? 1 : 0;
						continue;
					}
					std::vector<FToken> ParamTokens = Slice(PartBegin, Index);
					PartBegin = Index + 1;
					if (ParamTokens.empty())
					{
						Error(Tokens[Begin], "cpp.parse.empty_parameter", "empty function parameter");
						return false;
					}
					std::vector<FAttribute> Attributes;
					if (!ExtractAttributes(ParamTokens, Attributes))
					{
						return false;
					}
					if (ParamTokens.empty())
					{
						Error(Tokens[Begin], "cpp.parse.parameter_type_required",
						    "parameter requires a type in addition to its attributes");
						return false;
					}
					std::size_t Equals = ParamTokens.size();
					int Nested = 0;
					for (std::size_t At = 0; At < ParamTokens.size(); ++At)
					{
						Nested += IsOpening(ParamTokens[At].Text) ? 1 : 0;
						Nested -= IsClosing(ParamTokens[At].Text) ? 1 : 0;
						if (ParamTokens[At].Text == "=" && Nested == 0)
						{
							Equals = At;
							break;
						}
					}
					FParameter Param;
					Param.Location = ParamTokens.front().Location;
					Param.Attributes = std::move(Attributes);
					if (Equals < ParamTokens.size())
					{
						if (Equals + 1 == ParamTokens.size())
						{
							Error(ParamTokens[Equals],
							    "cpp.parse.default_argument_requires_expression",
							    "default argument requires an expression after '='");
							return false;
						}
						Param.DefaultValue = JoinTokenRange(ParamTokens, Equals + 1, ParamTokens.size());
						Param.DefaultValueTokens.assign(
						    ParamTokens.begin() + static_cast<std::ptrdiff_t>(Equals + 1),
						    ParamTokens.end());
						ParamTokens.resize(Equals);
					}
					if (!ParamTokens.empty() && ParamTokens.back().Kind == ETokenKind::Identifier && !IsTypeQualifier(ParamTokens.back().Text) && !IsBuiltinTypeWord(ParamTokens.back().Text) && ParamTokens.size() > 1 && ParamTokens[ParamTokens.size() - 2].Text != "::" && !std::all_of(ParamTokens.begin(), ParamTokens.end() - 1, [](const FToken& Token)
					{
						return IsTypeQualifier(Token.Text) || IsDeclModifier(Token.Text);
					}))
					{
						Param.Name = ParamTokens.back().Text;
						Param.bNameExplicit = true;
						ParamTokens.pop_back();
					}
					else
					{
						Param.Name = "Arg" + std::to_string(Out.size());
					}
					if (ParamTokens.empty())
					{
						Error(Param.Location.Offset < Tokens.back().Location.Offset ? Tokens[Begin] : Tokens.back(),
						    "cpp.parse.parameter_type_required", "parameter requires a type");
						return false;
					}
					Param.Type = ResolveAlias(ParseTypeTokens(ParamTokens), Scope);
					Out.push_back(std::move(Param));
				}
				return true;
			}

			bool ParseFieldOrVariable(
			    const std::size_t Begin,
			    const std::size_t Terminator,
			    const EAccess Access,
			    const std::string& Scope,
			    const bool bMember,
			    FField& Out)
			{
				std::vector<FToken> Decl = Slice(Begin, Terminator);
				Out.Location = Tokens[Begin].Location;
				Out.Access = Access;
				if (!ExtractAttributes(Decl, Out.Attributes))
				{
					return false;
				}
				for (const FToken& Token : Decl)
				{
					if (Token.Text == "virtual" || Token.Text == "explicit" || Token.Text == "register" || Token.Text == "consteval" || Token.Text == "friend" || Token.Text == "thread_local" || Token.Text == "[[" || Token.Text == "]]")
					{
						Error(Token, "cpp.parse.invalid_variable_specifier",
						    "invalid field or variable declaration specifier: " + Token.Text);
						return false;
					}
					Out.bStatic = Out.bStatic || Token.Text == "static";
					Out.bExtern = Out.bExtern || Token.Text == "extern";
					Out.bConstexpr = Out.bConstexpr || Token.Text == "constexpr";
					Out.bConstinit = Out.bConstinit || Token.Text == "constinit";
					Out.bMutable = Out.bMutable || Token.Text == "mutable";
					if (Token.Text == "[")
					{
						Error(Token, "cpp.parse.c_array_unsupported",
						    "C arrays are not supported; use std::vector or VeroScript array<T>");
						return false;
					}
					if (Token.Text == ",")
					{
						Error(Token, "cpp.parse.multi_declarator_unsupported",
						    "declare one field or variable per statement");
						return false;
					}
				}
				if (Out.bMutable && Out.bStatic)
				{
					Error(Tokens[Begin], "cpp.parse.mutable_static_field",
					    "a C++ field cannot be both mutable and static");
					return false;
				}
				const bool bInline = std::any_of(Decl.begin(), Decl.end(),
				    [](const FToken& Token)
				{
					return Token.Text == "inline";
				});
				Out.bInline = bInline;
				if (bMember && bInline && !Out.bStatic)
				{
					Error(Tokens[Begin], "cpp.parse.inline_nonstatic_field",
					    "inline applies only to static data members");
					return false;
				}
				if (!bMember && Out.bMutable)
				{
					Error(Tokens[Begin], "cpp.parse.mutable_global_variable",
					    "mutable applies only to non-static data members");
					return false;
				}
				if (bMember && Out.bConstexpr && !Out.bStatic)
				{
					Error(Tokens[Begin], "cpp.parse.constexpr_nonstatic_field",
					    "a constexpr data member must be static");
					return false;
				}
				if (bMember && Out.bConstinit && !Out.bStatic)
				{
					Error(Tokens[Begin], "cpp.parse.constinit_nonstatic_field",
					    "constinit applies only to variables with static storage duration");
					return false;
				}
				if (Out.bConstexpr && Out.bConstinit)
				{
					Error(Tokens[Begin], "cpp.parse.conflicting_constexpr_constinit",
					    "a variable cannot be declared both constexpr and constinit");
					return false;
				}
				if (!bMember && Out.bStatic && Out.bExtern)
				{
					Error(Tokens[Begin], "cpp.parse.conflicting_variable_linkage",
					    "a namespace-scope variable cannot be declared both static and extern");
					return false;
				}
				std::size_t Equals = Decl.size();
				int Nested = 0;
				for (std::size_t Index = 0; Index < Decl.size(); ++Index)
				{
					Nested += IsOpening(Decl[Index].Text) ? 1 : 0;
					Nested -= IsClosing(Decl[Index].Text) ? 1 : 0;
					if (Decl[Index].Text == "=" && Nested == 0)
					{
						Equals = Index;
						break;
					}
				}
				if (Equals < Decl.size())
				{
					Out.InitializerTokens.assign(
					    Decl.begin() + static_cast<std::ptrdiff_t>(Equals + 1), Decl.end());
					Decl.resize(Equals);
				}
				if (Out.bConstexpr && Out.InitializerTokens.empty())
				{
					Error(Tokens[Begin], "cpp.parse.constexpr_variable_requires_initializer",
					    "a constexpr variable requires an initializer");
					return false;
				}
				Decl.erase(std::remove_if(Decl.begin(), Decl.end(),
				               [](const FToken& Token)
				{
					return IsDeclModifier(Token.Text);
				}),
				    Decl.end());
				if (Decl.size() < 2 || Decl.back().Kind != ETokenKind::Identifier)
				{
					Error(Tokens[Begin], "cpp.parse.invalid_variable", "field or variable requires a type and name");
					return false;
				}
				Out.Name = Decl.back().Text;
				Decl.pop_back();
				Out.Type = ResolveAlias(ParseTypeTokens(Decl), Scope);
				if (Out.bMutable && (Out.Type.bConst || Out.Type.bPointerConst))
				{
					Error(Tokens[Begin], "cpp.parse.mutable_const_field",
					    "a mutable data member cannot have a const-qualified type");
					return false;
				}
				if (Out.bMutable && Out.Type.Reference != EReferenceKind::None)
				{
					Error(Tokens[Begin], "cpp.parse.mutable_reference_field",
					    "a mutable data member cannot have reference type");
					return false;
				}
				return true;
			}

			bool TryParseSimpleFieldOrVariableList(
			    const std::size_t Begin,
			    const std::size_t Terminator,
			    const EAccess Access,
			    const std::string& Scope,
			    const bool bMember,
			    std::vector<FField>& Out)
			{
				std::vector<std::size_t> Separators;
				int Nested = 0;
				for (std::size_t Index = Begin; Index < Terminator; ++Index)
				{
					Nested += IsOpening(Tokens[Index].Text) ? 1 : 0;
					Nested -= IsClosing(Tokens[Index].Text) ? 1 : 0;
					if (Nested == 0 && Tokens[Index].Text == ",")
					{
						Separators.push_back(Index);
					}
				}
				if (Separators.empty())
				{
					return false;
				}
				FField First;
				if (!ParseFieldOrVariable(Begin, Separators.front(), Access,
				        Scope, bMember, First))
				{
					return true;
				}
				if (First.Type.PointerDepth != 0 || First.Type.Reference != EReferenceKind::None)
				{
					Error(Tokens[Separators.front()],
					    "cpp.parse.mixed_declarator_type_unsupported",
					    "multi-declarator declarations are supported only when every declarator has the same non-pointer, non-reference type");
					return true;
				}
				Out.push_back(First);
				for (std::size_t Part = 0; Part < Separators.size(); ++Part)
				{
					const std::size_t PartBegin = Separators[Part] + 1;
					const std::size_t PartEnd = Part + 1 < Separators.size()
					    ? Separators[Part + 1]
					    : Terminator;
					if (PartBegin >= PartEnd || Tokens[PartBegin].Kind != ETokenKind::Identifier)
					{
						Error(Tokens[Separators[Part]],
						    "cpp.parse.mixed_declarator_type_unsupported",
						    "each additional declarator must be a simple identifier with the shared type");
						Out.clear();
						return true;
					}
					FField Field = First;
					Field.Name = Tokens[PartBegin].Text;
					Field.Location = Tokens[PartBegin].Location;
					Field.InitializerTokens.clear();
					if (PartBegin + 1 < PartEnd)
					{
						if (Tokens[PartBegin + 1].Text != "=" || PartBegin + 2 == PartEnd)
						{
							Error(Tokens[PartBegin + 1],
							    "cpp.parse.mixed_declarator_type_unsupported",
							    "each additional declarator must use an optional '= expression' initializer");
							Out.clear();
							return true;
						}
						Field.InitializerTokens = Slice(PartBegin + 2, PartEnd);
					}
					if (Field.bConstexpr && Field.InitializerTokens.empty())
					{
						Error(Tokens[PartBegin],
						    "cpp.parse.constexpr_variable_requires_initializer",
						    "every constexpr declarator requires an initializer");
						Out.clear();
						return true;
					}
					Out.push_back(std::move(Field));
				}
				return true;
			}

			FType ResolveAlias(FType Type, std::string Scope) const
			{
				if (Type.DeclarationScope.empty())
				{
					Type.DeclarationScope = Scope;
				}
				for (FType& Argument : Type.TemplateArguments)
				{
					Argument = ResolveAlias(std::move(Argument), Scope);
				}
				if (Type.BaseName.find("::") == std::string::npos && std::find(ActiveTemplateParameters.begin(), ActiveTemplateParameters.end(), Type.BaseName) != ActiveTemplateParameters.end())
				{
					Type.bTemplateParameter = true;
					return Type;
				}
				auto It = Aliases.end();
				std::string Name = Type.BaseName;
				if (Name.rfind("::", 0) == 0)
				{
					Scope.clear();
					while (Name.rfind("::", 0) == 0)
					{
						Name.erase(0, 2);
					}
				}
				while (true)
				{
					const std::string Qualified = Qualify(Scope, Name);
					// A nearer class/enum declaration hides aliases in outer scopes.
					// Record names are registered before parsing their own members.
					if (NonAliasTypeNames.count(Qualified)) return Type;
					It = Aliases.find(Qualified);
					if (It != Aliases.end() || Scope.empty())
					{
						break;
					}
					const std::size_t Separator = Scope.rfind("::");
					Scope = Separator == std::string::npos
					    ? std::string{}
					    : Scope.substr(0, Separator);
				}
				if (It == Aliases.end() || !Type.TemplateArguments.empty())
				{
					return Type;
				}
				FType Resolved = It->second;
				// C++ ignores cv applied through a reference typedef/alias. The
				// referred-to type retains only the qualifiers in the alias target.
				if (Resolved.Reference == EReferenceKind::None)
				{
					if (Resolved.PointerDepth > 0 && Type.PointerDepth == 0)
					{
						// cv applied to a pointer alias qualifies the alias object, not
						// its pointee: `using P = T*; const P` is `T* const`.
						Resolved.bPointerConst = Resolved.bPointerConst || Type.bConst;
						Resolved.bPointerVolatile =
						    Resolved.bPointerVolatile || Type.bVolatile;
					}
					else
					{
						Resolved.bConst = Resolved.bConst || Type.bConst;
						Resolved.bVolatile = Resolved.bVolatile || Type.bVolatile;
					}
				}
				Resolved.bPointerConst = Resolved.bPointerConst || Type.bPointerConst;
				Resolved.bPointerVolatile =
				    Resolved.bPointerVolatile || Type.bPointerVolatile;
				Resolved.PointerDepth = static_cast<std::uint8_t>(
				    std::min<unsigned>(255u, Resolved.PointerDepth + Type.PointerDepth));
				if (Resolved.Reference == EReferenceKind::LValue || Type.Reference == EReferenceKind::LValue)
				{
					Resolved.Reference = EReferenceKind::LValue;
				}
				else if (Resolved.Reference == EReferenceKind::RValue || Type.Reference == EReferenceKind::RValue)
				{
					Resolved.Reference = EReferenceKind::RValue;
				}
				Resolved.Spelling = Type.Spelling;
				Resolved.Location = Type.Location;
				return Resolved;
			}

			std::size_t FindMatching(
			    const std::size_t OpenAt,
			    const std::string_view Open,
			    const std::string_view Close) const
			{
				int Depth = 0;
				for (std::size_t Index = OpenAt; Index < Tokens.size(); ++Index)
				{
					if (Tokens[Index].Text == Open)
					{
						++Depth;
					}
					else if (Tokens[Index].Text == Close && --Depth == 0)
					{
						return Index;
					}
				}
				return Tokens.size();
			}

			std::vector<FToken> Slice(const std::size_t Begin, const std::size_t End) const
			{
				if (Begin >= End || End > Tokens.size())
				{
					return {};
				}
				return {Tokens.begin() + static_cast<std::ptrdiff_t>(Begin),
				    Tokens.begin() + static_cast<std::ptrdiff_t>(End)};
			}

			static bool IsOpening(const std::string_view Text)
			{
				return Text == "(" || Text == "[" || Text == "{" || Text == "<";
			}
			static bool IsClosing(const std::string_view Text)
			{
				return Text == ")" || Text == "]" || Text == "}" || Text == ">";
			}

			void RejectAndSynchronize(std::string Code, std::string Message)
			{
				Error(Peek(), std::move(Code), std::move(Message));
				Synchronize();
			}

			void Synchronize()
			{
				int Brace = 0;
				while (!AtEnd())
				{
					if (Check("{"))
					{
						++Brace;
					}
					else if (Check("}"))
					{
						if (Brace == 0)
						{
							return;
						}
						--Brace;
					}
					if (Check(";") && Brace == 0)
					{
						++Current;
						return;
					}
					++Current;
				}
			}

			void SynchronizeTo(const std::string_view Text)
			{
				while (!AtEnd() && !Check(Text))
				{
					++Current;
				}
			}

			void ValidateAliasConflicts()
			{
				const auto Check = [&](
				                       const std::string& QualifiedName,
				                       const FSourceLocation& DeclarationLocation,
				                       const std::string_view DeclarationKind)
				{
					const auto Alias = AliasLocations.find(QualifiedName);
					if (Alias == AliasLocations.end())
					{
						return;
					}
					ErrorAt(DeclarationLocation, "cpp.parse.using_alias_name_conflict",
					    "using alias '" + QualifiedName + "' conflicts with a " + std::string(DeclarationKind) + " declaration; alias is in " + Alias->second.File);
				};
				for (const FEnum& Enum : Unit.Enums)
				{
					Check(Enum.QualifiedName, Enum.Location, "enum");
				}
				for (const FRecord& Record : Unit.Records)
				{
					Check(Record.QualifiedName, Record.Location, "record");
				}
				for (const FFunction& Function : Unit.Functions)
				{
					Check(Function.QualifiedName, Function.Location, "function");
				}
				for (const FVariable& Variable : Unit.Variables)
				{
					Check(Variable.QualifiedName, Variable.Location, "variable");
				}
			}

			std::vector<FToken> Tokens;
			FTranslationUnit& Unit;
			std::vector<FDiagnostic>& Diagnostics;
			FParseOptions Options;
			std::size_t Current = 0;
			std::unordered_map<std::string, FType> Aliases;
			std::unordered_map<std::string, FSourceLocation> AliasLocations;
			std::unordered_set<std::string> NonAliasTypeNames;
			std::vector<std::string> ActiveTemplateParameters;
		};

		std::string NormalizeBase(std::string Value)
		{
			Value = Trim(std::move(Value));
			while (Value.rfind("::", 0) == 0)
			{
				Value.erase(0, 2);
			}
			std::string Compact;
			Compact.reserve(Value.size());
			bool bSpace = false;
			for (const char C : Value)
			{
				if (std::isspace(static_cast<unsigned char>(C)) != 0)
				{
					bSpace = true;
					continue;
				}
				if (bSpace && !Compact.empty() && C != ':' && Compact.back() != ':')
				{
					Compact.push_back(' ');
				}
				Compact.push_back(C);
				bSpace = false;
			}
			return Compact;
		}

		std::string ResolveKnownName(
		    const std::vector<std::string>& Names,
		    const std::string_view Name,
		    std::string Scope,
		    const bool Absolute,
		    const std::vector<std::string>* OtherNames = nullptr)
		{
			const auto Find = [&](const std::string_view Candidate)
			{
				const auto I = std::find(Names.begin(), Names.end(), Candidate);
				if (I != Names.end()) return *I;
				if (OtherNames)
				{
					const auto J = std::find(OtherNames->begin(), OtherNames->end(), Candidate);
					if (J != OtherNames->end()) return *J;
				}
				return std::string{};
			};
			// Relative qualified names participate in lexical lookup too. An inner
			// declaration also takes precedence over the same name at global scope.
			if (!Absolute)
			{
				while (!Scope.empty())
				{
					const std::string Qualified = Scope + "::" + std::string(Name);
					if (const auto Scoped = Find(Qualified); !Scoped.empty())
					{
						return Scoped;
					}
					const std::size_t Separator = Scope.rfind("::");
					Scope = Separator == std::string::npos
					    ? std::string{}
					    : Scope.substr(0, Separator);
				}
			}
			if (const auto Exact = Find(Name); !Exact.empty())
			{
				return Exact;
			}
			if (Absolute || Name.find("::") != std::string_view::npos) return {};
			std::string Match;
			const std::string Leaf = LeafName(Name);
			const auto FindLeaf = [&](const std::vector<std::string>& Candidates)
			{
				for (const std::string& Candidate : Candidates)
				{
					if (LeafName(Candidate) != Leaf)
					{
						continue;
					}

					if (!Match.empty() && Match != Candidate) return false;
					Match = Candidate;
				}
				return true;
			};
			if (!FindLeaf(Names) || (OtherNames && !FindLeaf(*OtherNames))) return {};
			return Match;
		}
	}

	bool FParseResult::Succeeded() const
	{
		return std::none_of(Diagnostics.begin(), Diagnostics.end(),
		    [](const FDiagnostic& Diagnostic)
		{
			return Diagnostic.Severity == EDiagnosticSeverity::Error;
		});
	}

	namespace
	{
		template <typename T>
		std::string FundamentalTypeIdentity()
		{
			if constexpr (std::is_same_v<T, signed char>)
			{
				return "signed char";
			}
			else if constexpr (std::is_same_v<T, unsigned char>)
			{
				return "unsigned char";
			}
			else if constexpr (std::is_same_v<T, short>)
			{
				return "short";
			}
			else if constexpr (std::is_same_v<T, unsigned short>)
			{
				return "unsigned short";
			}
			else if constexpr (std::is_same_v<T, int>)
			{
				return "int";
			}
			else if constexpr (std::is_same_v<T, unsigned int>)
			{
				return "unsigned int";
			}
			else if constexpr (std::is_same_v<T, long>)
			{
				return "long";
			}
			else if constexpr (std::is_same_v<T, unsigned long>)
			{
				return "unsigned long";
			}
			else if constexpr (std::is_same_v<T, long long>)
			{
				return "long long";
			}
			else if constexpr (std::is_same_v<T, unsigned long long>)
			{
				return "unsigned long long";
			}
			else
			{
				return "<unsupported-fundamental>";
			}
		}

		std::string CanonicalBuiltinIdentity(std::string Base)
		{
			Base = NormalizeBase(std::move(Base));
			if (Base == "signed char")
			{
				return "signed char";
			}
			if (Base == "unsigned char")
			{
				return "unsigned char";
			}
			if (Base == "short" || Base == "short int" || Base == "signed short" || Base == "signed short int")
			{
				return "short";
			}
			if (Base == "unsigned short" || Base == "unsigned short int")
			{
				return "unsigned short";
			}
			if (Base == "int" || Base == "signed" || Base == "signed int")
			{
				return "int";
			}
			if (Base == "unsigned" || Base == "unsigned int")
			{
				return "unsigned int";
			}
			if (Base == "long" || Base == "long int" || Base == "signed long" || Base == "signed long int")
			{
				return "long";
			}
			if (Base == "unsigned long" || Base == "unsigned long int")
			{
				return "unsigned long";
			}
			if (Base == "long long" || Base == "long long int" || Base == "signed long long" || Base == "signed long long int")
			{
				return "long long";
			}
			if (Base == "unsigned long long" || Base == "unsigned long long int")
			{
				return "unsigned long long";
			}
			if (Base == "std::int8_t" || Base == "int8_t")
			{
				return FundamentalTypeIdentity<std::int8_t>();
			}
			if (Base == "std::uint8_t" || Base == "uint8_t")
			{
				return FundamentalTypeIdentity<std::uint8_t>();
			}
			if (Base == "std::int16_t" || Base == "int16_t")
			{
				return FundamentalTypeIdentity<std::int16_t>();
			}
			if (Base == "std::uint16_t" || Base == "uint16_t")
			{
				return FundamentalTypeIdentity<std::uint16_t>();
			}
			if (Base == "std::int32_t" || Base == "int32_t")
			{
				return FundamentalTypeIdentity<std::int32_t>();
			}
			if (Base == "std::uint32_t" || Base == "uint32_t")
			{
				return FundamentalTypeIdentity<std::uint32_t>();
			}
			if (Base == "std::int64_t" || Base == "int64_t")
			{
				return FundamentalTypeIdentity<std::int64_t>();
			}
			if (Base == "std::uint64_t" || Base == "uint64_t")
			{
				return FundamentalTypeIdentity<std::uint64_t>();
			}
			if (Base == "std::size_t" || Base == "size_t")
			{
				return FundamentalTypeIdentity<std::size_t>();
			}
			if (Base == "std::ptrdiff_t" || Base == "ptrdiff_t")
			{
				return FundamentalTypeIdentity<std::ptrdiff_t>();
			}
			if (Base == "std::intptr_t" || Base == "intptr_t")
			{
				return FundamentalTypeIdentity<std::intptr_t>();
			}
			if (Base == "std::uintptr_t" || Base == "uintptr_t")
			{
				return FundamentalTypeIdentity<std::uintptr_t>();
			}
			return Base;
		}

		std::string TypeIdentity(const FType& Type)
		{
			std::string Result = CanonicalBuiltinIdentity(Type.BaseName);
			if (!Type.TemplateArguments.empty())
			{
				Result.push_back('<');
				for (std::size_t Index = 0; Index < Type.TemplateArguments.size(); ++Index)
				{
					if (Index > 0)
					{
						Result.push_back(',');
					}
					Result += TypeIdentity(Type.TemplateArguments[Index]);
				}
				Result.push_back('>');
			}
			if (Type.bConst)
			{
				Result += " const";
			}
			if (Type.bVolatile)
			{
				Result += " volatile";
			}
			Result.append(Type.PointerDepth, '*');
			if (Type.bPointerConst)
			{
				Result += " const";
			}
			if (Type.bPointerVolatile)
			{
				Result += " volatile";
			}
			if (Type.Reference == EReferenceKind::LValue)
			{
				Result.push_back('&');
			}
			else if (Type.Reference == EReferenceKind::RValue)
			{
				Result += "&&";
			}
			return Result;
		}

		std::string TemplateTypeIdentity(
		    const FType& Type,
		    const std::vector<FTemplateParameter>& Parameters)
		{
			std::string Result;
			const auto Parameter = std::find_if(Parameters.begin(), Parameters.end(),
			    [&](const FTemplateParameter& Value)
			{
				return Value.Name == Type.BaseName;
			});
			if (Parameter == Parameters.end())
			{
				Result = CanonicalBuiltinIdentity(Type.BaseName);
			}
			else
			{
				Result = "$T" + std::to_string(static_cast<std::size_t>(Parameter - Parameters.begin()));
			}
			if (!Type.TemplateArguments.empty())
			{
				Result.push_back('<');
				for (std::size_t Index = 0; Index < Type.TemplateArguments.size(); ++Index)
				{
					if (Index > 0)
					{
						Result.push_back(',');
					}
					Result += TemplateTypeIdentity(Type.TemplateArguments[Index], Parameters);
				}
				Result.push_back('>');
			}
			if (Type.bConst)
			{
				Result += " const";
			}
			if (Type.bVolatile)
			{
				Result += " volatile";
			}
			Result.append(Type.PointerDepth, '*');
			if (Type.bPointerConst)
			{
				Result += " const";
			}
			if (Type.bPointerVolatile)
			{
				Result += " volatile";
			}
			if (Type.Reference == EReferenceKind::LValue)
			{
				Result.push_back('&');
			}
			else if (Type.Reference == EReferenceKind::RValue)
			{
				Result += "&&";
			}
			return Result;
		}

		bool TemplateParametersCompatible(
		    const std::vector<FTemplateParameter>& Left,
		    const std::vector<FTemplateParameter>& Right)
		{
			if (Left.size() != Right.size())
			{
				return false;
			}
			for (std::size_t Index = 0; Index < Left.size(); ++Index)
			{
				if (Left[Index].bHasDefault && Right[Index].bHasDefault && TemplateTypeIdentity(Left[Index].DefaultType, Left) != TemplateTypeIdentity(Right[Index].DefaultType, Right))
				{
					return false;
				}
			}
			return true;
		}

		FType RemapTemplateType(
		    FType Type,
		    const std::vector<FTemplateParameter>& Source,
		    const std::vector<FTemplateParameter>& Target)
		{
			for (std::size_t Index = 0;
			    Index < Source.size() && Index < Target.size(); ++Index)
			{
				if (Type.BaseName == Source[Index].Name)
				{
					Type.BaseName = Target[Index].Name;
					Type.Spelling = Target[Index].Name;
					break;
				}
			}
			for (FType& Argument : Type.TemplateArguments)
			{
				Argument = RemapTemplateType(std::move(Argument), Source, Target);
			}
			return Type;
		}

		bool AttributeEqual(const FAttribute& Left, const FAttribute& Right)
		{
			if (Left.NamespaceName != Right.NamespaceName || Left.Name != Right.Name || Left.Arguments.size() != Right.Arguments.size())
			{
				return false;
			}
			for (std::size_t Index = 0; Index < Left.Arguments.size(); ++Index)
			{
				const FAttributeArgument& A = Left.Arguments[Index];
				const FAttributeArgument& B = Right.Arguments[Index];
				if (A.Name != B.Name || A.Value != B.Value || A.bHasValue != B.bHasValue)
				{
					return false;
				}
			}
			return true;
		}

		void MergeAttributes(
		    std::vector<FAttribute>& Target,
		    const std::vector<FAttribute>& Source)
		{
			for (const FAttribute& Attribute : Source)
			{
				if (std::none_of(Target.begin(), Target.end(),
				        [&](const FAttribute& Existing)
				{
					return AttributeEqual(Existing, Attribute);
				}))
				{
					Target.push_back(Attribute);
				}
			}
		}

		FType ApplyAliasUseQualifiers(FType Target, const FType& Use)
		{
			if (Target.Reference == EReferenceKind::None)
			{
				if (Target.PointerDepth > 0 && Use.PointerDepth == 0)
				{
					Target.bPointerConst = Target.bPointerConst || Use.bConst;
					Target.bPointerVolatile = Target.bPointerVolatile || Use.bVolatile;
				}
				else
				{
					Target.bConst = Target.bConst || Use.bConst;
					Target.bVolatile = Target.bVolatile || Use.bVolatile;
				}
			}
			Target.bPointerConst = Target.bPointerConst || Use.bPointerConst;
			Target.bPointerVolatile = Target.bPointerVolatile || Use.bPointerVolatile;
			Target.PointerDepth = static_cast<std::uint8_t>(std::min<unsigned>(
			    255u, Target.PointerDepth + Use.PointerDepth));
			if (Target.Reference == EReferenceKind::LValue || Use.Reference == EReferenceKind::LValue)
			{
				Target.Reference = EReferenceKind::LValue;
			}
			else if (Target.Reference == EReferenceKind::RValue || Use.Reference == EReferenceKind::RValue)
			{
				Target.Reference = EReferenceKind::RValue;
			}
			Target.Location = Use.Location;
			// An alias target is looked up where the alias is declared, while a
			// substituted template argument keeps the argument's own lookup scope.
			// Empty is the global scope, not an unspecified scope. Replacing it
			// with the use-site scope can bind a different same-name declaration.
			return Target;
		}

		FType SubstituteAliasParameters(
		    FType Type,
		    const std::vector<FTemplateParameter>& Parameters,
		    const std::vector<FType>& Arguments)
		{
			for (std::size_t Index = 0;
			    Index < Parameters.size() && Index < Arguments.size(); ++Index)
			{
				if (Type.BaseName == Parameters[Index].Name)
				{
					return ApplyAliasUseQualifiers(Arguments[Index], Type);
				}
			}
			for (FType& Argument : Type.TemplateArguments)
			{
				Argument = SubstituteAliasParameters(
				    std::move(Argument), Parameters, Arguments);
			}
			return Type;
		}

		const FTypeAlias* FindTypeAlias(
		    const std::vector<FTypeAlias>& Aliases,
		    std::string Name,
		    std::string Scope,
		    const FTypeMapOptions* Lookup)
		{
			if (Name.rfind("::", 0) == 0)
			{
				Scope.clear();
			}
			while (Name.rfind("::", 0) == 0)
			{
				Name.erase(0, 2);
			}
			const std::string UseScope = Scope;
			while (true)
			{
				const std::string Qualified = Scope.empty()
				    ? Name
				    : Scope + "::" + Name;
				const auto It = std::find_if(Aliases.begin(), Aliases.end(),
				    [&](const FTypeAlias& Alias)
				{
					return Alias.QualifiedName == Qualified;
				});
				if (It != Aliases.end())
				{
					// Scan concrete declarations only after finding a possible alias.
					// Most types in a large project do not refer to an alias at all.
					if (Lookup)
					{
						std::string InnerScope = UseScope;
						while (true)
						{
							const std::string Candidate = Qualify(InnerScope, Name);
							if (std::find(Lookup->KnownHostTypes.begin(), Lookup->KnownHostTypes.end(),
							        Candidate) != Lookup->KnownHostTypes.end() ||
							    std::find(Lookup->KnownEnumTypes.begin(), Lookup->KnownEnumTypes.end(),
							        Candidate) != Lookup->KnownEnumTypes.end())
							{
								return nullptr;
							}

							if (InnerScope == Scope)
							{
								break;
							}

							const auto Separator = InnerScope.rfind("::");
							InnerScope = Separator == std::string::npos ? std::string{} : InnerScope.substr(0, Separator);
						}
					}
					return &*It;
				}
				if (Scope.empty())
				{
					break;
				}
				const std::size_t Separator = Scope.rfind("::");
				Scope = Separator == std::string::npos
				    ? std::string{}
				    : Scope.substr(0, Separator);
			}
			return nullptr;
		}

		void MarkTypeParameters(FType& Type, const std::vector<std::string>& Parameters)
		{
			if (Type.BaseName.find("::") == std::string::npos &&
			    std::find(Parameters.begin(), Parameters.end(), Type.BaseName) != Parameters.end())
			{
				Type.bTemplateParameter = true;
			}

			for (FType& Argument : Type.TemplateArguments)
			{
				MarkTypeParameters(Argument, Parameters);
			}
		}

		FType ResolveProjectAliases(
		    FType Type,
		    const std::vector<FTypeAlias>& Aliases,
		    std::unordered_set<std::string>& Active,
		    const FTypeMapOptions* Lookup)
		{
			for (FType& Argument : Type.TemplateArguments)
			{
				Argument = ResolveProjectAliases(std::move(Argument), Aliases, Active, Lookup);
			}
			if (Type.bTemplateParameter) return Type;
			const FTypeAlias* Alias = FindTypeAlias(
			    Aliases, Type.BaseName, Type.DeclarationScope, Lookup);
			if (!Alias || Type.TemplateArguments.size() > Alias->TemplateParameters.size())
			{
				return Type;
			}
			if (!Alias->TemplateParameters.empty() && Type.TemplateArguments.empty() && Type.Spelling.find('<') == std::string::npos)
			{
				return Type;
			}
			if (!Active.insert(Alias->QualifiedName).second)
			{
				return Type;
			}
			std::vector<FType> Arguments = Type.TemplateArguments;
			for (std::size_t Index = Arguments.size();
			    Index < Alias->TemplateParameters.size(); ++Index)
			{
				const FTemplateParameter& Parameter = Alias->TemplateParameters[Index];
				if (!Parameter.bHasDefault)
				{
					Active.erase(Alias->QualifiedName);
					return Type;
				}
				FType Default = SubstituteAliasParameters(
				    Parameter.DefaultType, Alias->TemplateParameters, Arguments);
				Arguments.push_back(ResolveProjectAliases(
				    std::move(Default), Aliases, Active, Lookup));
			}
			FType Target = SubstituteAliasParameters(
			    Alias->TargetType, Alias->TemplateParameters, Arguments);
			Target = ApplyAliasUseQualifiers(std::move(Target), Type);
			Target = ResolveProjectAliases(std::move(Target), Aliases, Active, Lookup);
			Active.erase(Alias->QualifiedName);
			return Target;
		}

		FType ResolveProjectAliases(
		    FType Type,
		    const std::vector<FTypeAlias>& Aliases,
		    const FTypeMapOptions* Lookup = nullptr)
		{
			if (Aliases.empty()) return Type;
			std::unordered_set<std::string> Active;
			return ResolveProjectAliases(std::move(Type), Aliases, Active, Lookup);
		}

		std::string FunctionIdentity(const FFunction& Function)
		{
			std::string Result = Function.QualifiedName;
			if (!Function.TemplateParameters.empty())
			{
				Result += "<template:" + std::to_string(Function.TemplateParameters.size()) + ">";
			}
			Result.push_back('(');
			for (std::size_t Index = 0; Index < Function.Parameters.size(); ++Index)
			{
				if (Index > 0)
				{
					Result.push_back(',');
				}
				FType ParameterType = Function.Parameters[Index].Type;
				if (ParameterType.Reference == EReferenceKind::None && ParameterType.PointerDepth == 0)
				{
					// Top-level cv qualifiers on by-value parameters are not part
					// of a C++ function signature.
					ParameterType.bConst = false;
					ParameterType.bVolatile = false;
				}
				else if (ParameterType.Reference == EReferenceKind::None && ParameterType.PointerDepth > 0)
				{
					// The outermost pointer is passed by value; its top-level cv
					// qualifiers do not participate in C++ function identity.
					ParameterType.bPointerConst = false;
					ParameterType.bPointerVolatile = false;
				}
				Result += TemplateTypeIdentity(
				    ParameterType, Function.TemplateParameters);
			}
			Result.push_back(')');
			if (Function.bConstMethod)
			{
				Result += " const";
			}
			return Result;
		}

		void AddMergeError(
		    std::vector<FDiagnostic>& Diagnostics,
		    const FSourceLocation& Location,
		    std::string Code,
		    std::string Message);

		bool ValidateFunctionParameters(
		    const FFunction& Function,
		    std::vector<FDiagnostic>& Diagnostics)
		{
			bool bValid = true;
			bool bSawDefault = false;
			std::unordered_map<std::string, FSourceLocation> Names;
			for (const FParameter& Parameter : Function.Parameters)
			{
				if (Parameter.bNameExplicit)
				{
					const auto Added = Names.emplace(Parameter.Name, Parameter.Location);
					if (!Added.second)
					{
						AddMergeError(Diagnostics, Parameter.Location,
						    "cpp.merge.duplicate_parameter_name",
						    "duplicate parameter name '" + Parameter.Name + "' in function '" + Function.QualifiedName + "'");
						bValid = false;
					}
				}
				const bool bHasDefault = !Parameter.DefaultValueTokens.empty();
				if (!bHasDefault && bSawDefault)
				{
					AddMergeError(Diagnostics, Parameter.Location,
					    "cpp.merge.missing_trailing_default_argument",
					    "parameter '" + Parameter.Name + "' of function '" + Function.QualifiedName + "' requires a default argument because an earlier parameter has one");
					bValid = false;
				}
				bSawDefault = bSawDefault || bHasDefault;
			}
			return bValid;
		}

		bool ValidateEnumMembers(
		    const FEnum& Enum,
		    std::vector<FDiagnostic>& Diagnostics)
		{
			bool bValid = true;
			std::unordered_map<std::string, FSourceLocation> Names;
			for (const FEnumValue& Value : Enum.Values)
			{
				const auto Added = Names.emplace(Value.Name, Value.Location);
				if (!Added.second)
				{
					AddMergeError(Diagnostics, Value.Location,
					    "cpp.merge.duplicate_enumerator",
					    "duplicate enumerator '" + Enum.QualifiedName + "::" + Value.Name + "'; previous declaration is in " + Added.first->second.File);
					bValid = false;
				}
			}
			return bValid;
		}

		std::string CompactQualifiedSpelling(std::string Value)
		{
			Value.erase(std::remove_if(Value.begin(), Value.end(), [](const unsigned char C)
			{
				return std::isspace(C) != 0;
			}),
			    Value.end());
			while (Value.rfind("::", 0) == 0)
			{
				Value.erase(0, 2);
			}
			return Value;
		}

		bool ValidateRecordMembers(
		    const FRecord& Record,
		    std::vector<FDiagnostic>& Diagnostics)
		{
			bool bValid = true;
			std::unordered_map<std::string, FSourceLocation> Fields;
			for (const FField& Field : Record.Fields)
			{
				if (Field.Name == Record.Name)
				{
					AddMergeError(Diagnostics, Field.Location,
					    "cpp.merge.field_matches_record_name",
					    "field name '" + Field.Name + "' cannot match its enclosing record name");
					bValid = false;
				}
				const auto Added = Fields.emplace(Field.Name, Field.Location);
				if (!Added.second)
				{
					AddMergeError(Diagnostics, Field.Location,
					    "cpp.merge.duplicate_record_field",
					    "duplicate field '" + Record.QualifiedName + "::" + Field.Name + "'; previous declaration is in " + Added.first->second.File);
					bValid = false;
				}
			}
			std::unordered_map<std::string, FSourceLocation> Bases;
			for (const FBaseType& Base : Record.BaseTypes)
			{
				const std::string Identity = CompactQualifiedSpelling(Base.TypeName);
				const auto Added = Bases.emplace(Identity, Base.Location);
				if (!Added.second)
				{
					AddMergeError(Diagnostics, Base.Location,
					    "cpp.merge.duplicate_base_type",
					    "record '" + Record.QualifiedName + "' declares duplicate base type '" + Base.TypeName + "'");
					bValid = false;
				}
				if (Identity == Record.Name || Identity == Record.QualifiedName)
				{
					AddMergeError(Diagnostics, Base.Location,
					    "cpp.merge.direct_self_inheritance",
					    "record '" + Record.QualifiedName + "' cannot derive from itself");
					bValid = false;
				}
			}
			std::unordered_map<std::string, FSourceLocation> Methods;
			for (const FFunction& Method : Record.Methods)
			{
				bValid = ValidateFunctionParameters(Method, Diagnostics) && bValid;
				if (!Method.bConstructor)
				{
					const auto Field = Fields.find(Method.Name);
					if (Field != Fields.end())
					{
						AddMergeError(Diagnostics, Method.Location,
						    "cpp.merge.record_field_method_name_conflict",
						    "member '" + Record.QualifiedName + "::" + Method.Name + "' is declared as both a field and method; field is in " + Field->second.File);
						bValid = false;
					}
				}
				const std::string Identity = FunctionIdentity(Method);
				const auto Added = Methods.emplace(Identity, Method.Location);
				if (!Added.second)
				{
					AddMergeError(Diagnostics, Method.Location,
					    "cpp.merge.duplicate_record_method",
					    "duplicate member-function declaration '" + Identity + "'; previous declaration is in " + Added.first->second.File);
					bValid = false;
				}
			}
			return bValid;
		}

		bool IsFunctionDefinition(const FFunction& Function)
		{
			return Function.bHasBody || Function.bDeleted || Function.bDefaulted || Function.bPureVirtual;
		}

		void AddMergeError(
		    std::vector<FDiagnostic>& Diagnostics,
		    const FSourceLocation& Location,
		    std::string Code,
		    std::string Message)
		{
			Diagnostics.push_back({EDiagnosticSeverity::Error, std::move(Code),
			    std::move(Message), Location});
		}

		void ResolveOutOfClassMembers(
		    FTranslationUnit& Unit,
		    std::vector<FDiagnostic>& Diagnostics,
		    const bool bDeferMissingOwner)
		{
			auto FunctionIt = Unit.Functions.begin();
			while (FunctionIt != Unit.Functions.end())
			{
				if (!FunctionIt->bOutOfClassMember)
				{
					++FunctionIt;
					continue;
				}
				auto RecordIt = std::find_if(Unit.Records.begin(), Unit.Records.end(),
				    [&](const FRecord& Record)
				{
					return Record.QualifiedName == FunctionIt->OwnerQualifiedName;
				});
				if (RecordIt == Unit.Records.end() || RecordIt->bForwardDeclaration)
				{
					if (std::find(Unit.Namespaces.begin(), Unit.Namespaces.end(),
					        FunctionIt->OwnerQualifiedName) != Unit.Namespaces.end())
					{
						FunctionIt->bOutOfClassMember = false;
						FunctionIt->OwnerQualifiedName.clear();
						++FunctionIt;
						continue;
					}
					if (bDeferMissingOwner)
					{
						++FunctionIt;
						continue;
					}
					AddMergeError(Diagnostics, FunctionIt->Location,
					    "cpp.parse.out_of_class_owner_not_defined",
					    "out-of-class definition requires its complete owning record among the parsed inputs: " + FunctionIt->OwnerQualifiedName);
					FunctionIt = Unit.Functions.erase(FunctionIt);
					continue;
				}
				const std::string Identity = FunctionIdentity(*FunctionIt);
				auto MethodIt = std::find_if(RecordIt->Methods.begin(), RecordIt->Methods.end(),
				    [&](const FFunction& Method)
				{
					return FunctionIdentity(Method) == Identity;
				});
				if (MethodIt == RecordIt->Methods.end())
				{
					AddMergeError(Diagnostics, FunctionIt->Location,
					    "cpp.parse.out_of_class_declaration_not_found",
					    "out-of-class definition has no matching member declaration: " + Identity);
					FunctionIt = Unit.Functions.erase(FunctionIt);
					continue;
				}
				if (MethodIt->bHasBody || MethodIt->bDeleted || MethodIt->bDefaulted)
				{
					AddMergeError(Diagnostics, FunctionIt->Location,
					    "cpp.parse.duplicate_member_definition",
					    "member already has a definition: " + Identity);
					FunctionIt = Unit.Functions.erase(FunctionIt);
					continue;
				}
				if (!MethodIt->bConstructor && !MethodIt->bDestructor && TypeIdentity(MethodIt->ReturnType) != TypeIdentity(FunctionIt->ReturnType))
				{
					AddMergeError(Diagnostics, FunctionIt->Location,
					    "cpp.parse.out_of_class_return_type_mismatch",
					    "out-of-class member return type does not match its declaration: " + Identity);
					FunctionIt = Unit.Functions.erase(FunctionIt);
					continue;
				}
				if (MethodIt->bConstexpr != FunctionIt->bConstexpr || MethodIt->bConsteval != FunctionIt->bConsteval || MethodIt->bNoexcept != FunctionIt->bNoexcept)
				{
					AddMergeError(Diagnostics, FunctionIt->Location,
					    "cpp.parse.out_of_class_specifier_mismatch",
					    "out-of-class member constexpr, consteval, or noexcept specifier does not match its declaration: " + Identity);
					FunctionIt = Unit.Functions.erase(FunctionIt);
					continue;
				}
				if (!FunctionIt->bHasBody && !FunctionIt->bDeleted && !FunctionIt->bDefaulted)
				{
					AddMergeError(Diagnostics, FunctionIt->Location,
					    "cpp.parse.out_of_class_definition_required",
					    "qualified member declaration outside its class must provide a definition");
					FunctionIt = Unit.Functions.erase(FunctionIt);
					continue;
				}
				for (std::size_t Index = 0;
				    Index < MethodIt->Parameters.size() && Index < FunctionIt->Parameters.size(); ++Index)
				{
					if (!MethodIt->Parameters[Index].DefaultValueTokens.empty())
					{
						if (!FunctionIt->Parameters[Index].DefaultValueTokens.empty())
						{
							AddMergeError(Diagnostics, FunctionIt->Parameters[Index].Location,
							    "cpp.parse.repeated_default_argument",
							    "a default argument may not be repeated on an out-of-class definition");
						}
						else
						{
							FunctionIt->Parameters[Index].DefaultValue =
							    MethodIt->Parameters[Index].DefaultValue;
							FunctionIt->Parameters[Index].DefaultValueTokens =
							    MethodIt->Parameters[Index].DefaultValueTokens;
						}
					}
					MergeAttributes(FunctionIt->Parameters[Index].Attributes,
					    MethodIt->Parameters[Index].Attributes);
					if (!FunctionIt->Parameters[Index].bNameExplicit && MethodIt->Parameters[Index].bNameExplicit)
					{
						FunctionIt->Parameters[Index].Name =
						    MethodIt->Parameters[Index].Name;
						FunctionIt->Parameters[Index].bNameExplicit = true;
					}
				}
				MergeAttributes(FunctionIt->Attributes, MethodIt->Attributes);
				MethodIt->Parameters = std::move(FunctionIt->Parameters);
				MethodIt->Attributes = std::move(FunctionIt->Attributes);
				MethodIt->BodyTokens = std::move(FunctionIt->BodyTokens);
				MethodIt->bHasBody = FunctionIt->bHasBody;
				MethodIt->bDeleted = FunctionIt->bDeleted;
				MethodIt->bDefaulted = FunctionIt->bDefaulted;
				MethodIt->bInline = MethodIt->bInline || FunctionIt->bInline;
				MethodIt->Location = FunctionIt->Location;
				if (RecordIt->Location.File != MethodIt->Location.File)
				{
					ValidateFunctionParameters(*MethodIt, Diagnostics);
				}
				FunctionIt = Unit.Functions.erase(FunctionIt);
			}
		}

		void ResolveOutOfClassStaticFields(
		    FTranslationUnit& Unit,
		    std::vector<FDiagnostic>& Diagnostics,
		    const bool bDeferMissingOwner)
		{
			auto VariableIt = Unit.Variables.begin();
			while (VariableIt != Unit.Variables.end())
			{
				if (!VariableIt->bOutOfClassMember)
				{
					++VariableIt;
					continue;
				}
				auto RecordIt = std::find_if(Unit.Records.begin(), Unit.Records.end(),
				    [&](const FRecord& Record)
				{
					return Record.QualifiedName == VariableIt->OwnerQualifiedName;
				});
				if (RecordIt == Unit.Records.end() || RecordIt->bForwardDeclaration)
				{
					if (std::find(Unit.Namespaces.begin(), Unit.Namespaces.end(),
					        VariableIt->OwnerQualifiedName) != Unit.Namespaces.end())
					{
						VariableIt->bOutOfClassMember = false;
						VariableIt->OwnerQualifiedName.clear();
						++VariableIt;
						continue;
					}
					if (bDeferMissingOwner)
					{
						++VariableIt;
						continue;
					}
					AddMergeError(Diagnostics, VariableIt->Location,
					    "cpp.parse.out_of_class_field_owner_not_defined",
					    "qualified variable definition requires a complete owning record or declared namespace among the parsed inputs: " + VariableIt->OwnerQualifiedName);
					VariableIt = Unit.Variables.erase(VariableIt);
					continue;
				}
				auto FieldIt = std::find_if(RecordIt->Fields.begin(), RecordIt->Fields.end(),
				    [&](const FField& Field)
				{
					return Field.Name == VariableIt->Name;
				});
				if (FieldIt == RecordIt->Fields.end())
				{
					AddMergeError(Diagnostics, VariableIt->Location,
					    "cpp.parse.out_of_class_field_declaration_not_found",
					    "out-of-class static data-member definition has no matching field declaration: " + VariableIt->QualifiedName);
					VariableIt = Unit.Variables.erase(VariableIt);
					continue;
				}
				if (!FieldIt->bStatic)
				{
					AddMergeError(Diagnostics, VariableIt->Location,
					    "cpp.parse.out_of_class_field_not_static",
					    "only a static data member may have an out-of-class definition: " + VariableIt->QualifiedName);
					VariableIt = Unit.Variables.erase(VariableIt);
					continue;
				}
				if (TypeIdentity(FieldIt->Type) != TypeIdentity(VariableIt->Type))
				{
					AddMergeError(Diagnostics, VariableIt->Location,
					    "cpp.parse.out_of_class_field_type_mismatch",
					    "out-of-class static data-member type does not match its declaration: " + VariableIt->QualifiedName);
					VariableIt = Unit.Variables.erase(VariableIt);
					continue;
				}
				if (FieldIt->bConstexpr != VariableIt->bConstexpr)
				{
					AddMergeError(Diagnostics, VariableIt->Location,
					    "cpp.parse.out_of_class_field_constexpr_mismatch",
					    "out-of-class static data-member constexpr specifier does not match its declaration: " + VariableIt->QualifiedName);
					VariableIt = Unit.Variables.erase(VariableIt);
					continue;
				}
				if (FieldIt->bDefinedOutOfClass)
				{
					AddMergeError(Diagnostics, VariableIt->Location,
					    "cpp.parse.duplicate_static_field_definition",
					    "static data member already has an out-of-class definition: " + VariableIt->QualifiedName);
					VariableIt = Unit.Variables.erase(VariableIt);
					continue;
				}
				if (!FieldIt->InitializerTokens.empty() && !VariableIt->InitializerTokens.empty())
				{
					AddMergeError(Diagnostics, VariableIt->Location,
					    "cpp.parse.duplicate_static_field_initializer",
					    "static data member has initializers both in-class and out-of-class: " + VariableIt->QualifiedName);
					VariableIt = Unit.Variables.erase(VariableIt);
					continue;
				}
				if (FieldIt->InitializerTokens.empty())
				{
					FieldIt->InitializerTokens = std::move(VariableIt->InitializerTokens);
				}
				MergeAttributes(FieldIt->Attributes, VariableIt->Attributes);
				FieldIt->bDefinedOutOfClass = true;
				FieldIt->bInline = FieldIt->bInline || VariableIt->bInline;
				FieldIt->bConstinit = FieldIt->bConstinit || VariableIt->bConstinit;
				VariableIt = Unit.Variables.erase(VariableIt);
			}
		}

		bool MergeFunctionDeclaration(
		    FFunction& Existing,
		    const FFunction& Incoming,
		    std::vector<FDiagnostic>& Diagnostics)
		{
			if (!TemplateParametersCompatible(
			        Existing.TemplateParameters, Incoming.TemplateParameters))
			{
				AddMergeError(Diagnostics, Incoming.Location,
				    "cpp.merge.conflicting_function_template_parameters",
				    "conflicting template parameter list for function '" + Incoming.QualifiedName + "'; previous declaration is in " + Existing.Location.File);
				return false;
			}
			if (TemplateTypeIdentity(Existing.ReturnType, Existing.TemplateParameters) != TemplateTypeIdentity(Incoming.ReturnType, Incoming.TemplateParameters))
			{
				AddMergeError(Diagnostics, Incoming.Location,
				    "cpp.merge.conflicting_function_return_type",
				    "conflicting return type for function '" + Incoming.QualifiedName + "'; previous declaration is in " + Existing.Location.File);
				return false;
			}
			if (Existing.bConstexpr != Incoming.bConstexpr || Existing.bConsteval != Incoming.bConsteval || Existing.bNoexcept != Incoming.bNoexcept)
			{
				AddMergeError(Diagnostics, Incoming.Location,
				    "cpp.merge.conflicting_function_specifier",
				    "conflicting constexpr, consteval, or noexcept declaration for function '" + Incoming.QualifiedName + "'; previous declaration is in " + Existing.Location.File);
				return false;
			}
			if ((Existing.bStatic || Incoming.bStatic) && Existing.Location.File != Incoming.Location.File)
			{
				AddMergeError(Diagnostics, Incoming.Location,
				    "cpp.merge.internal_linkage_collision",
				    "same-signature static functions from separate translation units are distinct C++ entities and cannot be merged: '" + Incoming.QualifiedName + "'");
				return false;
			}
			if (!Existing.bStatic && Incoming.bStatic)
			{
				AddMergeError(Diagnostics, Incoming.Location,
				    "cpp.merge.conflicting_function_linkage",
				    "function was given internal linkage after an external-linkage declaration: '" + Incoming.QualifiedName + "'");
				return false;
			}
			const bool ExistingDefinition = IsFunctionDefinition(Existing);
			const bool IncomingDefinition = IsFunctionDefinition(Incoming);
			if (Incoming.bDeleted && !Existing.bDeleted)
			{
				AddMergeError(Diagnostics, Incoming.Location,
				    "cpp.merge.deleted_definition_must_be_first_declaration",
				    "a deleted function definition must be the first declaration of '" + Incoming.QualifiedName + "'");
				return false;
			}
			if (Existing.bCLinkage != Incoming.bCLinkage)
			{
				AddMergeError(Diagnostics, Incoming.Location,
				    "cpp.merge.conflicting_function_language_linkage",
				    "conflicting C and C++ language linkage for function '" + Incoming.QualifiedName + "'");
				return false;
			}
			if (ExistingDefinition && IncomingDefinition)
			{
				AddMergeError(Diagnostics, Incoming.Location,
				    "cpp.merge.duplicate_function_definition",
				    "duplicate definition of function '" + Incoming.QualifiedName + "'; previous definition is in " + Existing.Location.File);
				return false;
			}

			FFunction Combined = IncomingDefinition ? Incoming : Existing;
			const FFunction& DefaultsFrom = IncomingDefinition ? Existing : Incoming;
			for (std::size_t Index = 0; Index < Combined.TemplateParameters.size(); ++Index)
			{
				FTemplateParameter& Target = Combined.TemplateParameters[Index];
				const FTemplateParameter& Source = DefaultsFrom.TemplateParameters[Index];
				if (Target.bHasDefault && Source.bHasDefault)
				{
					AddMergeError(Diagnostics, Source.Location,
					    "cpp.merge.duplicate_template_default_argument",
					    "template default argument is repeated for parameter " + std::to_string(Index) + " of function '" + Incoming.QualifiedName + "'");
					return false;
				}
				if (!Target.bHasDefault && Source.bHasDefault)
				{
					Target.DefaultType = RemapTemplateType(Source.DefaultType,
					    DefaultsFrom.TemplateParameters, Combined.TemplateParameters);
					Target.bHasDefault = true;
				}
			}
			for (std::size_t Index = 0; Index < Combined.Parameters.size(); ++Index)
			{
				FParameter& Target = Combined.Parameters[Index];
				const FParameter& Source = DefaultsFrom.Parameters[Index];
				if (!Target.DefaultValue.empty() && !Source.DefaultValue.empty())
				{
					AddMergeError(Diagnostics, Source.Location,
					    "cpp.merge.duplicate_default_argument",
					    "default argument is repeated for parameter " + std::to_string(Index) + " of function '" + Incoming.QualifiedName + "'");
					return false;
				}
				if (Target.DefaultValue.empty() && !Source.DefaultValue.empty())
				{
					Target.DefaultValue = Source.DefaultValue;
					Target.DefaultValueTokens = Source.DefaultValueTokens;
				}
				// A definition may legally omit an unused parameter name even when a
				// preceding declaration provides one. Keep that declaration metadata
				// for binding catalogs, hover, and generated API references.
				if (!Target.bNameExplicit && Source.bNameExplicit)
				{
					Target.Name = Source.Name;
					Target.bNameExplicit = true;
				}
				MergeAttributes(Target.Attributes, Source.Attributes);
			}
			MergeAttributes(Combined.Attributes, DefaultsFrom.Attributes);
			Combined.bStatic = Existing.bStatic || Incoming.bStatic;
			Combined.bInline = Existing.bInline || Incoming.bInline;
			Combined.bExtern = Existing.bExtern || Incoming.bExtern;
			Combined.bCLinkage = Existing.bCLinkage || Incoming.bCLinkage;
			Existing = std::move(Combined);
			return true;
		}

		bool IsVariableDefinition(const FVariable& Variable)
		{
			// A namespace-scope declaration without extern is a definition. An
			// extern declaration with an initializer is also a definition.
			return !Variable.bExtern || !Variable.InitializerTokens.empty();
		}

		bool MergeVariableDeclaration(
		    FVariable& Existing,
		    const FVariable& Incoming,
		    std::vector<FDiagnostic>& Diagnostics)
		{
			if (TypeIdentity(Existing.Type) != TypeIdentity(Incoming.Type))
			{
				AddMergeError(Diagnostics, Incoming.Location,
				    "cpp.merge.conflicting_variable_type",
				    "conflicting type for variable '" + Incoming.QualifiedName + "'; previous declaration is in " + Existing.Location.File);
				return false;
			}
			if (Existing.bCLinkage != Incoming.bCLinkage)
			{
				AddMergeError(Diagnostics, Incoming.Location,
				    "cpp.merge.conflicting_variable_language_linkage",
				    "conflicting C and C++ language linkage for variable '" + Incoming.QualifiedName + "'");
				return false;
			}
			if ((Existing.bStatic || Incoming.bStatic) && Existing.Location.File != Incoming.Location.File)
			{
				AddMergeError(Diagnostics, Incoming.Location,
				    "cpp.merge.internal_linkage_variable_collision",
				    "same-name static variables from separate translation units are distinct C++ entities and cannot be merged: '" + Incoming.QualifiedName + "'");
				return false;
			}
			if (!Existing.bStatic && Incoming.bStatic)
			{
				AddMergeError(Diagnostics, Incoming.Location,
				    "cpp.merge.conflicting_variable_linkage",
				    "variable was given internal linkage after an external-linkage declaration: '" + Incoming.QualifiedName + "'");
				return false;
			}

			const bool bExistingDefinition = IsVariableDefinition(Existing);
			const bool bIncomingDefinition = IsVariableDefinition(Incoming);
			if (bExistingDefinition && bIncomingDefinition)
			{
				AddMergeError(Diagnostics, Incoming.Location,
				    "cpp.merge.duplicate_variable_definition",
				    "duplicate definition of variable '" + Incoming.QualifiedName + "'; previous definition is in " + Existing.Location.File);
				return false;
			}

			FVariable Combined = bIncomingDefinition ? Incoming : Existing;
			const FVariable& MetadataFrom = bIncomingDefinition ? Existing : Incoming;
			MergeAttributes(Combined.Attributes, MetadataFrom.Attributes);
			Combined.bStatic = Existing.bStatic || Incoming.bStatic;
			Combined.bInline = Existing.bInline || Incoming.bInline;
			Combined.bCLinkage = Existing.bCLinkage || Incoming.bCLinkage;
			Combined.bConstinit = Existing.bConstinit || Incoming.bConstinit;
			Existing = std::move(Combined);
			return true;
		}

	}

	FParseResult MergeTranslationUnits(const std::vector<FTranslationUnit>& Units)
	{
		FParseResult Result;
		Result.Unit.SourcePath = Units.size() == 1
		    ? Units.front().SourcePath
		    : "<multiple-cpp-files>";
		if (Units.empty())
		{
			AddMergeError(Result.Diagnostics, {"<cpp-files>", 1, 1, 0},
			    "cpp.merge.no_input_files", "at least one C++ input file is required");
			return Result;
		}

		std::vector<FTranslationUnit> NormalizedUnits = Units;
		std::vector<FTypeAlias> ProjectAliases;
		FTypeMapOptions ProjectLookup;
		for (const FTranslationUnit& Unit : Units)
		{
			ProjectAliases.insert(ProjectAliases.end(),
			    Unit.TypeAliases.begin(), Unit.TypeAliases.end());
			for (const FRecord& Record : Unit.Records)
			{
				ProjectLookup.KnownHostTypes.push_back(Record.QualifiedName);
			}

			for (const FEnum& Enum : Unit.Enums)
			{
				ProjectLookup.KnownEnumTypes.push_back(Enum.QualifiedName);
			}
		}
		for (FTranslationUnit& Unit : NormalizedUnits)
		{
			for (FTypeAlias& Alias : Unit.TypeAliases)
			{
				Alias.TargetType = ResolveProjectAliases(
				    std::move(Alias.TargetType), ProjectAliases, &ProjectLookup);
			}
			for (FRecord& Record : Unit.Records)
			{
				for (FBaseType& Base : Record.BaseTypes)
				{
					if (Base.Type.BaseName.empty())
					{
						continue;
					}

					Base.Type = ResolveProjectAliases(std::move(Base.Type), ProjectAliases, &ProjectLookup);
					Base.TypeName = TypeIdentity(Base.Type);
				}
				for (FTemplateParameter& Parameter : Record.TemplateParameters)
				{
					if (Parameter.bHasDefault)
					{
						Parameter.DefaultType = ResolveProjectAliases(
						    std::move(Parameter.DefaultType), ProjectAliases, &ProjectLookup);
					}
				}
				for (FField& Field : Record.Fields)
				{
					Field.Type = ResolveProjectAliases(std::move(Field.Type), ProjectAliases, &ProjectLookup);
				}
				for (FFunction& Method : Record.Methods)
				{
					for (FTemplateParameter& Parameter : Method.TemplateParameters)
					{
						if (Parameter.bHasDefault)
						{
							Parameter.DefaultType = ResolveProjectAliases(
							    std::move(Parameter.DefaultType), ProjectAliases, &ProjectLookup);
						}
					}
					Method.ReturnType = ResolveProjectAliases(
					    std::move(Method.ReturnType), ProjectAliases, &ProjectLookup);
					for (FParameter& Parameter : Method.Parameters)
					{
						Parameter.Type = ResolveProjectAliases(
						    std::move(Parameter.Type), ProjectAliases, &ProjectLookup);
					}
				}
			}
			for (FFunction& Function : Unit.Functions)
			{
				for (FTemplateParameter& Parameter : Function.TemplateParameters)
				{
					if (Parameter.bHasDefault)
					{
						Parameter.DefaultType = ResolveProjectAliases(
						    std::move(Parameter.DefaultType), ProjectAliases, &ProjectLookup);
					}
				}
				Function.ReturnType = ResolveProjectAliases(
				    std::move(Function.ReturnType), ProjectAliases, &ProjectLookup);
				for (FParameter& Parameter : Function.Parameters)
				{
					Parameter.Type = ResolveProjectAliases(
					    std::move(Parameter.Type), ProjectAliases, &ProjectLookup);
				}
			}
			for (FVariable& Variable : Unit.Variables)
			{
				Variable.Type = ResolveProjectAliases(
				    std::move(Variable.Type), ProjectAliases, &ProjectLookup);
			}
		}

		std::unordered_map<std::string,
		    std::unordered_map<std::string, std::unordered_set<std::string>>>
		    UsingNamespacesByFile;
		for (const FTranslationUnit& Unit : NormalizedUnits)
		{
			for (const std::string& Namespace : Unit.Namespaces)
			{
				if (std::find(Result.Unit.Namespaces.begin(),
				        Result.Unit.Namespaces.end(), Namespace) == Result.Unit.Namespaces.end())
				{
					Result.Unit.Namespaces.push_back(Namespace);
				}
			}
			for (const FUsingNamespace& Using : Unit.UsingNamespaces)
			{
				if (UsingNamespacesByFile[Using.Location.File][Using.Scope]
				        .insert(Using.NamespaceName)
				        .second)
				{
					Result.Unit.UsingNamespaces.push_back(Using);
				}
			}
			for (const FTypeAlias& Alias : Unit.TypeAliases)
			{
				const bool bValueConflict = std::any_of(
				                                Result.Unit.Enums.begin(), Result.Unit.Enums.end(),
				                                [&](const FEnum& Value)
				{
					return Value.QualifiedName == Alias.QualifiedName;
				}) ||
				    std::any_of(Result.Unit.Records.begin(), Result.Unit.Records.end(),
				        [&](const FRecord& Value)
				{
					return Value.QualifiedName == Alias.QualifiedName;
				}) ||
				    std::any_of(Result.Unit.Functions.begin(), Result.Unit.Functions.end(),
				        [&](const FFunction& Value)
				{
					return Value.QualifiedName == Alias.QualifiedName;
				}) ||
				    std::any_of(Result.Unit.Variables.begin(), Result.Unit.Variables.end(),
				        [&](const FVariable& Value)
				{
					return Value.QualifiedName == Alias.QualifiedName;
				});
				if (bValueConflict)
				{
					AddMergeError(Result.Diagnostics, Alias.Location,
					    "cpp.merge.using_alias_name_conflict",
					    "using alias conflicts with another declaration: " + Alias.QualifiedName);
					continue;
				}
				auto Existing = std::find_if(Result.Unit.TypeAliases.begin(),
				    Result.Unit.TypeAliases.end(), [&](const FTypeAlias& Value)
				{
					return Value.QualifiedName == Alias.QualifiedName;
				});
				if (Existing == Result.Unit.TypeAliases.end())
				{
					Result.Unit.TypeAliases.push_back(Alias);
				}
				else if (!TemplateParametersCompatible(
				             Existing->TemplateParameters, Alias.TemplateParameters) ||
				    TemplateTypeIdentity(Existing->TargetType,
				        Existing->TemplateParameters) != TemplateTypeIdentity(Alias.TargetType, Alias.TemplateParameters))
				{
					AddMergeError(Result.Diagnostics, Alias.Location,
					    "cpp.merge.conflicting_using_alias",
					    "conflicting using-alias declaration for '" + Alias.QualifiedName + "'; previous declaration is in " + Existing->Location.File);
				}
			}
			for (const FEnum& Enum : Unit.Enums)
			{
				if (!ValidateEnumMembers(Enum, Result.Diagnostics))
				{
					continue;
				}
				if (std::any_of(Result.Unit.TypeAliases.begin(), Result.Unit.TypeAliases.end(),
				        [&](const FTypeAlias& Alias)
				{
					return Alias.QualifiedName == Enum.QualifiedName;
				}))
				{
					AddMergeError(Result.Diagnostics, Enum.Location,
					    "cpp.merge.using_alias_name_conflict",
					    "enum conflicts with a using alias: " + Enum.QualifiedName);
					continue;
				}
				if (std::any_of(Result.Unit.Records.begin(), Result.Unit.Records.end(),
				        [&](const FRecord& Record)
				{
					return Record.QualifiedName == Enum.QualifiedName;
				}))
				{
					AddMergeError(Result.Diagnostics, Enum.Location,
					    "cpp.merge.conflicting_type_declaration",
					    "type is declared as both an enum and record: " + Enum.QualifiedName);
					continue;
				}
				auto Existing = std::find_if(Result.Unit.Enums.begin(), Result.Unit.Enums.end(),
				    [&](const FEnum& Value)
				{
					return Value.QualifiedName == Enum.QualifiedName;
				});
				if (Existing == Result.Unit.Enums.end())
				{
					Result.Unit.Enums.push_back(Enum);
				}
				else if (Existing->bScoped != Enum.bScoped || Existing->UnderlyingType != Enum.UnderlyingType || (!Enum.bScoped && Existing->bHasExplicitUnderlyingType != Enum.bHasExplicitUnderlyingType))
				{
					AddMergeError(Result.Diagnostics, Enum.Location,
					    "cpp.merge.conflicting_enum_declaration",
					    "conflicting enum declaration for '" + Enum.QualifiedName + "'; previous declaration is in " + Existing->Location.File);
				}
				else if (Existing->bForwardDeclaration && !Enum.bForwardDeclaration)
				{
					FEnum Combined = Enum;
					MergeAttributes(Combined.Attributes, Existing->Attributes);
					*Existing = std::move(Combined);
				}
				else if (Existing->bForwardDeclaration && Enum.bForwardDeclaration)
				{
					MergeAttributes(Existing->Attributes, Enum.Attributes);
				}
				else if (!Existing->bForwardDeclaration && !Enum.bForwardDeclaration)
				{
					AddMergeError(Result.Diagnostics, Enum.Location,
					    "cpp.merge.duplicate_enum_definition",
					    "duplicate enum definition for '" + Enum.QualifiedName + "'; previous definition is in " + Existing->Location.File);
				}
			}

			for (const FRecord& Record : Unit.Records)
			{
				if (!ValidateRecordMembers(Record, Result.Diagnostics))
				{
					continue;
				}
				if (std::any_of(Result.Unit.TypeAliases.begin(), Result.Unit.TypeAliases.end(),
				        [&](const FTypeAlias& Alias)
				{
					return Alias.QualifiedName == Record.QualifiedName;
				}))
				{
					AddMergeError(Result.Diagnostics, Record.Location,
					    "cpp.merge.using_alias_name_conflict",
					    "record conflicts with a using alias: " + Record.QualifiedName);
					continue;
				}
				if (std::any_of(Result.Unit.Enums.begin(), Result.Unit.Enums.end(),
				        [&](const FEnum& Enum)
				{
					return Enum.QualifiedName == Record.QualifiedName;
				}))
				{
					AddMergeError(Result.Diagnostics, Record.Location,
					    "cpp.merge.conflicting_type_declaration",
					    "type is declared as both a record and enum: " + Record.QualifiedName);
					continue;
				}
				auto Existing = std::find_if(Result.Unit.Records.begin(), Result.Unit.Records.end(),
				    [&](const FRecord& Value)
				{
					return Value.QualifiedName == Record.QualifiedName;
				});
				if (Existing == Result.Unit.Records.end())
				{
					Result.Unit.Records.push_back(Record);
				}
				else if (!TemplateParametersCompatible(
				             Existing->TemplateParameters, Record.TemplateParameters))
				{
					AddMergeError(Result.Diagnostics, Record.Location,
					    "cpp.merge.conflicting_record_template_parameters",
					    "conflicting template parameter list for record '" + Record.QualifiedName + "'; previous declaration is in " + Existing->Location.File);
				}
				else if (Existing->bForwardDeclaration && !Record.bForwardDeclaration)
				{
					FRecord Combined = Record;
					for (std::size_t Index = 0; Index < Combined.TemplateParameters.size(); ++Index)
					{
						FTemplateParameter& Target = Combined.TemplateParameters[Index];
						const FTemplateParameter& Source = Existing->TemplateParameters[Index];
						if (Target.bHasDefault && Source.bHasDefault)
						{
							AddMergeError(Result.Diagnostics, Target.Location,
							    "cpp.merge.duplicate_template_default_argument",
							    "template default argument is repeated for record '" + Record.QualifiedName + "'");
							continue;
						}
						if (!Target.bHasDefault && Source.bHasDefault)
						{
							Target.DefaultType = RemapTemplateType(Source.DefaultType,
							    Existing->TemplateParameters, Combined.TemplateParameters);
							Target.bHasDefault = true;
						}
					}
					MergeAttributes(Combined.Attributes, Existing->Attributes);
					*Existing = std::move(Combined);
				}
				else if (Existing->bForwardDeclaration && Record.bForwardDeclaration)
				{
					for (std::size_t Index = 0; Index < Existing->TemplateParameters.size(); ++Index)
					{
						FTemplateParameter& Target = Existing->TemplateParameters[Index];
						const FTemplateParameter& Source = Record.TemplateParameters[Index];
						if (Target.bHasDefault && Source.bHasDefault)
						{
							AddMergeError(Result.Diagnostics, Source.Location,
							    "cpp.merge.duplicate_template_default_argument",
							    "template default argument is repeated for record '" + Record.QualifiedName + "'");
							continue;
						}
						if (!Target.bHasDefault && Source.bHasDefault)
						{
							Target.DefaultType = RemapTemplateType(Source.DefaultType,
							    Record.TemplateParameters, Existing->TemplateParameters);
							Target.bHasDefault = true;
						}
					}
					MergeAttributes(Existing->Attributes, Record.Attributes);
				}
				else if (!Existing->bForwardDeclaration && !Record.bForwardDeclaration)
				{
					AddMergeError(Result.Diagnostics, Record.Location,
					    "cpp.merge.duplicate_record_definition",
					    "duplicate record definition for '" + Record.QualifiedName + "'; previous definition is in " + Existing->Location.File);
				}
			}

			for (const FFunction& Function : Unit.Functions)
			{
				if (std::any_of(Result.Unit.TypeAliases.begin(), Result.Unit.TypeAliases.end(),
				        [&](const FTypeAlias& Alias)
				{
					return Alias.QualifiedName == Function.QualifiedName;
				}))
				{
					AddMergeError(Result.Diagnostics, Function.Location,
					    "cpp.merge.using_alias_name_conflict",
					    "function conflicts with a using alias: " + Function.QualifiedName);
					continue;
				}
				auto ConflictingVariable = std::find_if(
				    Result.Unit.Variables.begin(), Result.Unit.Variables.end(),
				    [&](const FVariable& Value)
				{
					return Value.QualifiedName == Function.QualifiedName;
				});
				if (ConflictingVariable != Result.Unit.Variables.end())
				{
					AddMergeError(Result.Diagnostics, Function.Location,
					    "cpp.merge.conflicting_value_declaration",
					    "'" + Function.QualifiedName + "' is declared as both a function and variable; variable declaration is in " + ConflictingVariable->Location.File);
					continue;
				}
				const std::string Identity = FunctionIdentity(Function);
				auto Existing = std::find_if(Result.Unit.Functions.begin(), Result.Unit.Functions.end(),
				    [&](const FFunction& Value)
				{
					return FunctionIdentity(Value) == Identity;
				});
				if (Existing == Result.Unit.Functions.end())
				{
					Result.Unit.Functions.push_back(Function);
				}
				else
				{
					MergeFunctionDeclaration(*Existing, Function, Result.Diagnostics);
				}
			}
			for (const FVariable& Variable : Unit.Variables)
			{
				if (std::any_of(Result.Unit.TypeAliases.begin(), Result.Unit.TypeAliases.end(),
				        [&](const FTypeAlias& Alias)
				{
					return Alias.QualifiedName == Variable.QualifiedName;
				}))
				{
					AddMergeError(Result.Diagnostics, Variable.Location,
					    "cpp.merge.using_alias_name_conflict",
					    "variable conflicts with a using alias: " + Variable.QualifiedName);
					continue;
				}
				auto ConflictingFunction = std::find_if(
				    Result.Unit.Functions.begin(), Result.Unit.Functions.end(),
				    [&](const FFunction& Value)
				{
					return Value.QualifiedName == Variable.QualifiedName;
				});
				if (ConflictingFunction != Result.Unit.Functions.end())
				{
					AddMergeError(Result.Diagnostics, Variable.Location,
					    "cpp.merge.conflicting_value_declaration",
					    "'" + Variable.QualifiedName + "' is declared as both a variable and function; function declaration is in " + ConflictingFunction->Location.File);
					continue;
				}
				auto Existing = std::find_if(
				    Result.Unit.Variables.begin(), Result.Unit.Variables.end(),
				    [&](const FVariable& Value)
				{
					return Value.QualifiedName == Variable.QualifiedName;
				});
				if (Existing == Result.Unit.Variables.end())
				{
					Result.Unit.Variables.push_back(Variable);
				}
				else
				{
					MergeVariableDeclaration(*Existing, Variable, Result.Diagnostics);
				}
			}
		}
		// Individual project inputs may contain a conventional .cpp member
		// definition while the complete class declaration lives in a sibling header.
		// Resolve only after every record and function has been collected.
		ResolveOutOfClassMembers(Result.Unit, Result.Diagnostics, false);
		ResolveOutOfClassStaticFields(Result.Unit, Result.Diagnostics, false);
		for (const FFunction& Function : Result.Unit.Functions)
		{
			ValidateFunctionParameters(Function, Result.Diagnostics);
		}
		return Result;
	}

	std::string GetFunctionIdentity(const FFunction& Function)
	{
		return FunctionIdentity(Function);
	}

	FParseResult ParseFiles(
	    const std::vector<std::string>& FilePaths,
	    const FParseOptions& Options)
	{
		FParseOptions FileOptions = Options;
		FileOptions.bDeferOutOfClassMemberResolution = true;
		std::vector<FTranslationUnit> Units;
		std::vector<FDiagnostic> Diagnostics;
		CppTools::FFilePathSet Seen;
		for (const std::string& FilePath : FilePaths)
		{
			if (!Seen.Insert(FilePath))
			{
				AddMergeError(Diagnostics, {FilePath, 1, 1, 0},
				    "cpp.merge.duplicate_input_file",
				    "the same C++ input file was supplied more than once");
				continue;
			}
			FParseResult Parsed = ParseFile(FilePath, FileOptions);
			const bool bParsedSuccessfully = Parsed.Succeeded();
			Diagnostics.insert(Diagnostics.end(),
			    std::make_move_iterator(Parsed.Diagnostics.begin()),
			    std::make_move_iterator(Parsed.Diagnostics.end()));
			if (bParsedSuccessfully)
			{
				Units.push_back(std::move(Parsed.Unit));
			}
		}
		if (Units.empty() && !FilePaths.empty())
		{
			FParseResult Result;
			Result.Unit.SourcePath = FilePaths.size() == 1
			    ? FilePaths.front()
			    : "<multiple-cpp-files>";
			Result.Diagnostics = std::move(Diagnostics);
			return Result;
		}
		FParseResult Result = MergeTranslationUnits(Units);
		Result.Diagnostics.insert(Result.Diagnostics.begin(),
		    std::make_move_iterator(Diagnostics.begin()),
		    std::make_move_iterator(Diagnostics.end()));
		return Result;
	}

	FParseResult ParseSource(
	    const std::string_view Source,
	    const std::string_view SourcePath,
	    const FParseOptions& Options)
	{
		FParseResult Result;
		Result.Unit.SourcePath = std::string(SourcePath);
		FLexer Lexer(Source, std::string(SourcePath));
		std::vector<FToken> Tokens = Lexer.Run(Result.Diagnostics);
		if (Result.Succeeded() && StripConventionalHeaderGuard(Tokens, Result.Diagnostics))
		{
			FParser Parser(std::move(Tokens), Result.Unit, Result.Diagnostics, Options);
			Parser.Run();
		}
		if (Result.Succeeded() && !Options.bDeferOutOfClassMemberResolution)
		{
			FParseResult Validated = MergeTranslationUnits({Result.Unit});
			Validated.Diagnostics.insert(
			    Validated.Diagnostics.begin(),
			    std::make_move_iterator(Result.Diagnostics.begin()),
			    std::make_move_iterator(Result.Diagnostics.end()));
			return Validated;
		}
		return Result;
	}

	FParseResult ParseFile(const std::string_view FilePath, const FParseOptions& Options)
	{
		// Native file APIs stop at NUL; never read a different prefix path while
		// reporting the caller's complete spelling as the parsed source.
		if (FilePath.find('\0') != std::string_view::npos)
		{
			FParseResult Result;
			Result.Unit.SourcePath = std::string(FilePath);
			Result.Diagnostics.push_back({EDiagnosticSeverity::Error, "cpp.io.read_failed",
			    "C++ source path must not contain NUL characters", {std::string(FilePath), 1, 1, 0}});
			return Result;
		}
		const auto SourcePath = Path::FromUtf8(FilePath);
		std::error_code FileError;
		const bool RegularFile = std::filesystem::is_regular_file(SourcePath, FileError);
		std::ifstream Input;
		if (RegularFile && !FileError)
		{
			Input.open(SourcePath, std::ios::binary);
		}
		if (!Input.is_open())
		{
			FParseResult Result;
			Result.Unit.SourcePath = std::string(FilePath);
			Result.Diagnostics.push_back({EDiagnosticSeverity::Error, "cpp.io.read_failed",
			    "could not open C++ source file", {std::string(FilePath), 1, 1, 0}});
			return Result;
		}
		// Reading through operator<<(streambuf*) reports extraction failures on
		// the destination stream, leaving Input apparently good. Read explicitly
		// so a failed source read cannot be accepted as empty or partial input.
		std::string Buffer;
		char Chunk[8192];
		while (Input.read(Chunk, sizeof(Chunk)) || Input.gcount() > 0)
		{
			Buffer.append(Chunk, static_cast<std::size_t>(Input.gcount()));
		}
		if (Input.bad() || !Input.eof())
		{
			FParseResult Result;
			Result.Unit.SourcePath = std::string(FilePath);
			Result.Diagnostics.push_back({EDiagnosticSeverity::Error, "cpp.io.read_failed",
			    "failed while reading C++ source file", {std::string(FilePath), 1, 1, 0}});
			return Result;
		}
		return ParseSource(Buffer, FilePath, Options);
	}

	FTypeMapResult MapTypeToVeroScript(const FType& Type, const FTypeMapOptions& Options)
	{
		const auto Failure = [](std::string Code, std::string Message)
		{
			FTypeMapResult Result;
			Result.ErrorCode = std::move(Code);
			Result.ErrorMessage = std::move(Message);
			return Result;
		};
		if (!Options.KnownTypeAliases.empty())
		{
			FType Resolved = Type;
			MarkTypeParameters(Resolved, Options.KnownTypeParameters);
			Resolved = ResolveProjectAliases(std::move(Resolved), Options.KnownTypeAliases, &Options);
			if (TypeIdentity(Resolved) != TypeIdentity(Type))
			{
				FTypeMapOptions ResolvedOptions = Options;
				ResolvedOptions.KnownTypeAliases.clear();
				return MapTypeToVeroScript(Resolved, ResolvedOptions);
			}
		}
		if (Type.bVolatile)
		{
			return Failure("cpp.type.volatile_unsupported",
			    "volatile has no semantics-preserving VeroScript mapping");
		}
		if (Type.Reference == EReferenceKind::RValue && !Options.bAllowRValueReferences)
		{
			return Failure("cpp.type.rvalue_reference_unsupported",
			    "rvalue references have no direct VeroScript mapping");
		}
		if (Type.PointerDepth > 0 && !Options.bAllowRawHostPointers)
		{
			return Failure("cpp.type.raw_pointer_requires_policy",
			    "raw pointer type requires an explicit host ownership/nullability policy: " + Type.Spelling);
		}
		if (Type.PointerDepth > 0 && Type.bConst)
		{
			return Failure("cpp.type.const_raw_pointer_unsupported",
			    "a pointer-to-const host object cannot be exposed as a mutable VeroScript host handle: " + Type.Spelling);
		}
		if (Type.PointerDepth > 0 && (Type.bPointerConst || Type.bPointerVolatile))
		{
			return Failure("cpp.type.pointer_cv_unsupported",
			    "cv-qualified pointer storage cannot be represented by the VeroScript host-object value ABI: " + Type.Spelling);
		}

		const std::string Base = NormalizeBase(Type.BaseName);
		if (Type.BaseName.find("::") == std::string::npos && std::find(Options.KnownTypeParameters.begin(), Options.KnownTypeParameters.end(), Base) != Options.KnownTypeParameters.end())
		{
			if (Type.PointerDepth != 0 || !Type.TemplateArguments.empty())
			{
				return Failure("cpp.type.template_parameter_shape_unsupported",
				    "a type template parameter cannot be used as a template or pointer type here: " + Type.Spelling);
			}
			FTypeMapResult Result;
			Result.bSuccess = true;
			Result.VeroType = Base;
			return Result;
		}
		if (Type.PointerDepth > 0)
		{
			if (Type.PointerDepth != 1)
			{
				return Failure("cpp.type.multilevel_pointer_unsupported",
				    "multi-level raw pointers cannot be represented as a VeroScript host object: " + Type.Spelling);
			}
			if (Type.Reference != EReferenceKind::None)
			{
				return Failure("cpp.type.pointer_reference_unsupported",
				    "references to raw pointers cannot be represented by the host-object value ABI: " + Type.Spelling);
			}
			const std::string KnownHost = ResolveKnownName(
			    Options.KnownHostTypes, Base, Type.DeclarationScope, Type.BaseName.rfind("::", 0) == 0, &Options.KnownEnumTypes);
			if (KnownHost.empty() || std::find(Options.KnownHostTypes.begin(), Options.KnownHostTypes.end(), KnownHost) == Options.KnownHostTypes.end())
			{
				return Failure("cpp.type.raw_pointer_pointee_unsupported",
				    "raw-pointer policy only permits a single pointer to a known host type: " + Type.Spelling);
			}
			FTypeMapResult Result;
			Result.bSuccess = true;
			Result.VeroType = Options.bQualifyNamedTypes ? "::" + KnownHost : KnownHost;
			return Result;
		}
		static const std::unordered_set<std::string> FundamentalNames = {"void", "bool", "char", "short", "int", "long", "signed", "unsigned", "float", "double"};
		if (!FundamentalNames.count(Base) && Base.find(' ') == std::string::npos)
		{
			const std::string KnownHost = ResolveKnownName(
			    Options.KnownHostTypes, Base, Type.DeclarationScope, Type.BaseName.rfind("::", 0) == 0, &Options.KnownEnumTypes);
			if (!KnownHost.empty())
			{
				const bool KnownEnum = std::find(Options.KnownEnumTypes.begin(), Options.KnownEnumTypes.end(), KnownHost) != Options.KnownEnumTypes.end();
				FTypeMapResult Result;
				Result.bSuccess = true;
				Result.VeroType = Options.bQualifyNamedTypes ? "::" + KnownHost : KnownHost;
				if (!Type.TemplateArguments.empty())
				{
					if (KnownEnum)
					{
						return Failure("cpp.type.enum_template_arguments",
						    "an enum type cannot have template arguments: " + Type.Spelling);
					}
					Result.VeroType.push_back('<');
					for (std::size_t Index = 0; Index < Type.TemplateArguments.size(); ++Index)
					{
						FTypeMapResult Argument = MapTypeToVeroScript(
						    Type.TemplateArguments[Index], Options);
						if (!Argument.bSuccess)
						{
							return Argument;
						}
						if (Index > 0)
						{
							Result.VeroType.push_back(',');
						}
						Result.VeroType += Type.TemplateArguments[Index].bConst
						    ? "readonly<" + Argument.VeroType + ">"
						    : Argument.VeroType;
					}
					Result.VeroType.push_back('>');
				}
				return Result;
			}
		}

		static const std::unordered_map<std::string, std::string> Scalars = {
		    {"void", "void"}, {"bool", "bool"},
		    {"signed char", "int8"}, {"std::int8_t", "int8"}, {"int8_t", "int8"},
		    {"unsigned char", "uint8"}, {"std::uint8_t", "uint8"}, {"uint8_t", "uint8"},
		    {"short", "short"}, {"short int", "short"}, {"signed short", "short"},
		    {"std::int16_t", "int16"}, {"int16_t", "int16"},
		    {"unsigned short", "uint16"}, {"unsigned short int", "uint16"},
		    {"std::uint16_t", "uint16"}, {"uint16_t", "uint16"},
		    {"int", "int"}, {"signed", "int"}, {"signed int", "int"},
		    {"std::int32_t", "int"}, {"int32_t", "int"},
		    {"unsigned", "unsigned int"}, {"unsigned int", "unsigned int"},
		    {"std::uint32_t", "unsigned int"}, {"uint32_t", "unsigned int"},
		    {"long long", "int64"}, {"long long int", "int64"},
		    {"signed long long", "int64"}, {"std::int64_t", "int64"}, {"int64_t", "int64"},
		    {"unsigned long long", "uint64"}, {"unsigned long long int", "uint64"},
		    {"std::uint64_t", "uint64"}, {"uint64_t", "uint64"},
		    {"float", "float"}, {"double", "double"},
		    {"std::string", "string"}, {"string", "string"},
		    {"std::wstring", "wstring"}, {"wstring", "wstring"}};
		if (const auto It = Scalars.find(Base); It != Scalars.end())
		{
			FTypeMapResult Result;
			Result.bSuccess = true;
			Result.VeroType = It->second;
			return Result;
		}
		if (Base == "char")
		{
			FTypeMapResult Result;
			Result.bSuccess = true;
			Result.VeroType = std::numeric_limits<char>::is_signed ? "char" : "uint8";
			return Result;
		}
		if (Base == "long" || Base == "long int" || Base == "signed long")
		{
			FTypeMapResult Result;
			Result.bSuccess = true;
			Result.VeroType = sizeof(long) == sizeof(std::int32_t) ? "int" : "int64";
			return Result;
		}
		if (Base == "std::size_t" || Base == "size_t" || Base == "std::uintptr_t" || Base == "uintptr_t")
		{
			FTypeMapResult Result;
			Result.bSuccess = true;
			Result.VeroType = sizeof(std::size_t) == sizeof(std::uint64_t)
			    ? "uint64"
			    : "unsigned int";
			return Result;
		}
		if (Base == "std::ptrdiff_t" || Base == "ptrdiff_t" || Base == "std::intptr_t" || Base == "intptr_t")
		{
			FTypeMapResult Result;
			Result.bSuccess = true;
			Result.VeroType = sizeof(std::ptrdiff_t) == sizeof(std::int64_t)
			    ? "int64"
			    : "int";
			return Result;
		}
		if (Base == "unsigned long" || Base == "unsigned long int")
		{
			FTypeMapResult Result;
			Result.bSuccess = true;
			Result.VeroType = sizeof(unsigned long) == sizeof(std::uint32_t)
			    ? "unsigned int"
			    : "uint64";
			return Result;
		}

		const auto Compose = [&](const std::string_view VeroBase, const std::size_t Arity)
		{
			if (Type.TemplateArguments.size() != Arity)
			{
				return Failure("cpp.type.template_arity_mismatch",
				    "template type has unsupported arity: " + Type.Spelling);
			}
			std::string Value(VeroBase);
			Value.push_back('<');
			for (std::size_t Index = 0; Index < Type.TemplateArguments.size(); ++Index)
			{
				if (Type.TemplateArguments[Index].Reference != EReferenceKind::None)
				{
					return Failure("cpp.type.reference_template_argument_unsupported",
					    "C++ reference template arguments have no value-container mapping: " + Type.TemplateArguments[Index].Spelling);
				}
				FTypeMapResult Argument = MapTypeToVeroScript(Type.TemplateArguments[Index], Options);
				if (!Argument.bSuccess)
				{
					return Argument;
				}
				if (Index > 0)
				{
					Value.push_back(',');
				}
				if (Type.TemplateArguments[Index].bConst)
				{
					Value += "readonly<" + Argument.VeroType + ">";
				}
				else
				{
					Value += Argument.VeroType;
				}
			}
			Value.push_back('>');
			FTypeMapResult Result;
			Result.bSuccess = true;
			Result.VeroType = std::move(Value);
			return Result;
		};

		if (Base == "std::vector" || Base == "vector")
		{
			return Compose("array", 1);
		}
		if (Base == "std::deque" || Base == "deque")
		{
			return Compose("deque", 1);
		}
		if (Base == "std::queue" || Base == "queue")
		{
			return Compose("queue", 1);
		}
		if (Base == "std::stack" || Base == "stack")
		{
			return Compose("stack", 1);
		}
		if (Base == "std::priority_queue" || Base == "priority_queue")
		{
			return Compose("priority_queue", 1);
		}
		if (Base == "std::unordered_map" || Base == "unordered_map")
		{
			return Compose("map", 2);
		}
		if (Base == "std::map" || Base == "map")
		{
			return Compose("ordered_map", 2);
		}
		if (Base == "std::unordered_multimap" || Base == "unordered_multimap")
		{
			return Compose("multimap", 2);
		}
		if (Base == "std::multimap" || Base == "multimap")
		{
			return Compose("ordered_multimap", 2);
		}
		if (Base == "std::unordered_set" || Base == "unordered_set")
		{
			return Compose("set", 1);
		}
		if (Base == "std::unordered_multiset" || Base == "unordered_multiset")
		{
			return Compose("multiset", 1);
		}
		if (Base == "std::multiset" || Base == "multiset")
		{
			return Compose("ordered_multiset", 1);
		}
		if (Base == "std::pair" || Base == "pair")
		{
			return Compose("pair", 2);
		}
		if (Base == "std::optional" || Base == "optional")
		{
			return Compose("optional", 1);
		}
		if (Base == "std::weak_ptr" || Base == "weak_ptr")
		{
			return Compose("weak", 1);
		}
		if (Base == "std::shared_ptr" || Base == "shared_ptr")
		{
			return Failure("cpp.type.shared_ownership_unsupported",
			    "std::shared_ptr ownership and nullability cannot be erased to its pointee type safely");
		}
		if (Base == "std::set" || Base == "set")
		{
			return Failure("cpp.type.ordered_set_unavailable",
			    "std::set ordering cannot be preserved because VeroScript has no ordered_set<T>");
		}

		if (!Base.empty() && Type.TemplateArguments.empty())
		{
			// User types declared in the same translation unit are added to KnownHostTypes
			// by clients. Unknown names fail closed so typos cannot become opaque host values.
			return Failure("cpp.type.unknown_type", "unknown or unsupported C++ type: " + Type.Spelling);
		}
		return Failure("cpp.type.template_unsupported", "unsupported C++ template type: " + Type.Spelling);
	}

	FType ResolveTypeAliases(
	    FType Type,
	    const std::vector<FTypeAlias>& Aliases)
	{
		return ResolveProjectAliases(std::move(Type), Aliases);
	}

	FType ResolveTypeAliases(
	    FType Type,
	    const std::vector<FTypeAlias>& Aliases,
	    const FTypeMapOptions& Lookup)
	{
		MarkTypeParameters(Type, Lookup.KnownTypeParameters);
		return ResolveProjectAliases(std::move(Type), Aliases, &Lookup);
	}

	std::string FormatDiagnostic(const FDiagnostic& Diagnostic)
	{
		const char* Severity = Diagnostic.Severity == EDiagnosticSeverity::Error ? "error"
		    : Diagnostic.Severity == EDiagnosticSeverity::Warning                ? "warning"
		                                                                         : "note";
		std::ostringstream Out;
		Out << Diagnostic.Location.File << ':' << Diagnostic.Location.Line << ':'
		    << Diagnostic.Location.Column << ": " << Severity << "[" << Diagnostic.Code
		    << "]: " << Diagnostic.Message;
		return Out.str();
	}

	std::string JoinTokens(const std::vector<FToken>& Tokens)
	{
		std::string Out;
		const auto IsWord = [](const FToken& Token)
		{
			return Token.Kind == ETokenKind::Identifier || Token.Kind == ETokenKind::Number || Token.Kind == ETokenKind::StringLiteral || Token.Kind == ETokenKind::CharacterLiteral;
		};
		for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
		{
			const FToken& Token = Tokens[Index];
			if (Token.Kind == ETokenKind::End)
			{
				continue;
			}
			if (!Out.empty())
			{
				const FToken& Previous = Tokens[Index - 1];
				const bool bSpace = (IsWord(Previous) && IsWord(Token)) || (Previous.Text == ">" && IsWord(Token)) || (IsWord(Previous) && Token.Text == "{") || (Previous.Text == ",") || (Previous.Text == ";");
				if (bSpace)
				{
					Out.push_back(' ');
				}
			}
			Out += Token.Text;
		}
		return Trim(std::move(Out));
	}
}
