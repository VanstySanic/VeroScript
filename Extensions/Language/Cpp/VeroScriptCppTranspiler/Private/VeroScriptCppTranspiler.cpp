// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCppTranspiler.h"
#include "VeroScriptJsonString.h"

#include "VeroScriptCppToolOutput.h"
#include "VeroScriptCppTranspilerNumeric.h"
#include "VeroScriptCompile.h"
#include "VeroScriptLexer.h"
#include "VeroScriptVersion.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace VeroScript::CppTranspiler
{
	using namespace CppFrontend;

	namespace
	{
		struct FMapEntry
		{
			std::uint32_t GeneratedLine = 1;
			CppFrontend::FSourceLocation Original;
		};

		void AddError(
		    std::vector<FDiagnostic>& Diagnostics,
		    const CppFrontend::FSourceLocation& Location,
		    std::string Code,
		    std::string Message)
		{
			Diagnostics.push_back({EDiagnosticSeverity::Error, std::move(Code),
			    std::move(Message), Location});
		}

		std::string LeafName(const std::string_view Name)
		{
			const std::size_t At = Name.rfind("::");
			return At == std::string_view::npos ? std::string(Name) : std::string(Name.substr(At + 2));
		}

		std::string ScopeName(const std::string_view Name)
		{
			const std::size_t At = Name.rfind("::");
			return At == std::string_view::npos ? std::string{} : std::string(Name.substr(0, At));
		}

		std::vector<std::string> SplitScope(const std::string& Scope)
		{
			std::vector<std::string> Result;
			std::size_t Begin = 0;
			while (Begin < Scope.size())
			{
				const std::size_t At = Scope.find("::", Begin);
				Result.push_back(Scope.substr(Begin,
				    At == std::string::npos ? std::string::npos : At - Begin));
				if (At == std::string::npos)
				{
					break;
				}
				Begin = At + 2;
			}
			return Result;
		}

		std::string JsonEscape(const std::string_view Value)
		{
			return JsonStringDetail::Escape(Value, JsonStringDetail::EControlEscapeStyle::Unicode);
		}

		class FEmitter
		{
		public:
			void Line(const std::string& Text = {})
			{
				Output += Text;
				Output.push_back('\n');
				CurrentLine += 1u + static_cast<std::uint32_t>(std::count(Text.begin(), Text.end(), '\n'));
			}

			void Mark(const CppFrontend::FSourceLocation& Location)
			{
				Mappings.push_back({CurrentLine, Location});
			}

			void OpenNamespaces(const std::string& Scope)
			{
				for (const std::string& Part : SplitScope(Scope))
				{
					Line("namespace " + Part);
					Line("{");
					++NamespaceDepth;
				}
			}

			void CloseNamespaces()
			{
				while (NamespaceDepth > 0)
				{
					Line("}");
					--NamespaceDepth;
				}
			}

			std::string Output;
			std::vector<FMapEntry> Mappings;

		private:
			std::uint32_t CurrentLine = 1;
			std::size_t NamespaceDepth = 0;
		};

		bool IsSimpleAnnotationValueToken(const VeroScript::FToken& Token)
		{
			return Token.Kind == VeroScript::ETokenKind::StringLiteral || Token.Kind == VeroScript::ETokenKind::BooleanLiteral || Token.Kind == VeroScript::ETokenKind::IntegerLiteral || Token.Kind == VeroScript::ETokenKind::FloatLiteral || Token.Kind == VeroScript::ETokenKind::Identifier;
		}

		bool ParseAnnotationGroupValue(
		    const std::vector<VeroScript::FToken>& Tokens,
		    std::size_t& Index);

		bool ParseAnnotationValueAt(
		    const std::vector<VeroScript::FToken>& Tokens,
		    std::size_t& Index)
		{
			if (Index >= Tokens.size())
			{
				return false;
			}
			if (Tokens[Index].Lexeme == "(")
			{
				return ParseAnnotationGroupValue(Tokens, Index);
			}
			if (Tokens[Index].Lexeme == "-" && Index + 1 < Tokens.size() && (Tokens[Index + 1].Kind == VeroScript::ETokenKind::IntegerLiteral || Tokens[Index + 1].Kind == VeroScript::ETokenKind::FloatLiteral))
			{
				Index += 2;
				return true;
			}
			if (!IsSimpleAnnotationValueToken(Tokens[Index]))
			{
				return false;
			}
			++Index;
			return true;
		}

		bool ParseAnnotationGroupValue(
		    const std::vector<VeroScript::FToken>& Tokens,
		    std::size_t& Index)
		{
			if (Index >= Tokens.size() || Tokens[Index].Lexeme != "(")
			{
				return false;
			}
			++Index;
			if (Index < Tokens.size() && Tokens[Index].Lexeme == ")")
			{
				++Index;
				return true;
			}
			for (;;)
			{
				if (Index >= Tokens.size() || Tokens[Index].Kind != VeroScript::ETokenKind::Identifier)
				{
					return false;
				}
				++Index;
				if (Index < Tokens.size() && Tokens[Index].Lexeme == "=")
				{
					++Index;
					if (!ParseAnnotationValueAt(Tokens, Index))
					{
						return false;
					}
				}
				if (Index >= Tokens.size())
				{
					return false;
				}
				if (Tokens[Index].Lexeme == ")")
				{
					++Index;
					return true;
				}
				if (Tokens[Index].Lexeme != ",")
				{
					return false;
				}
				++Index;
			}
		}

		bool IsValidAnnotationValue(const std::string& Value)
		{
			VeroScript::VeroScriptLexer Lexer(Value, "<cpp-attribute-value>");
			const std::vector<VeroScript::FToken>& AllTokens = Lexer.Tokenize();
			if (Lexer.HasError() || AllTokens.empty())
			{
				return false;
			}
			std::vector<VeroScript::FToken> Tokens;
			for (const VeroScript::FToken& Token : AllTokens)
			{
				if (Token.Kind != VeroScript::ETokenKind::EndOfFile)
				{
					Tokens.push_back(Token);
				}
			}
			if (Tokens.size() == 1)
			{
				return IsSimpleAnnotationValueToken(Tokens.front());
			}
			if (Tokens.size() == 2 && Tokens[0].Lexeme == "-" && (Tokens[1].Kind == VeroScript::ETokenKind::IntegerLiteral || Tokens[1].Kind == VeroScript::ETokenKind::FloatLiteral))
			{
				return true;
			}
			std::size_t Index = 0;
			return ParseAnnotationGroupValue(Tokens, Index) && Index == Tokens.size();
		}

		std::string FormatType(
		    const FType& Type,
		    const FTypeMapOptions& MapOptions,
		    std::vector<FDiagnostic>& Diagnostics)
		{
			const FTypeMapResult Mapped = MapTypeToVeroScript(Type, MapOptions);
			if (!Mapped.bSuccess)
			{
				AddError(Diagnostics, Type.Location, Mapped.ErrorCode, Mapped.ErrorMessage);
				return {};
			}
			if (Type.Reference == EReferenceKind::RValue)
			{
				AddError(Diagnostics, Type.Location, "cpp.transpile.rvalue_reference_unsupported",
				    "rvalue-reference semantics cannot be preserved by source transpilation");
				return {};
			}
			if (Type.Reference == EReferenceKind::LValue)
			{
				// Trailing & is valid for both parameters and function return types.
				return (Type.bConst ? "const " : "") + Mapped.VeroType + "&";
			}
			return Type.bConst ? "const " + Mapped.VeroType : Mapped.VeroType;
		}

		std::string FormatBodyTokens(
		    const std::vector<CppFrontend::FToken>& Tokens,
		    const std::size_t BaseIndent)
		{
			std::string Out;
			std::vector<CppFrontend::FToken> LineTokens;
			std::size_t Indent = BaseIndent;
			int ParenDepth = 0;
			const auto Flush = [&]()
			{
				if (LineTokens.empty())
				{
					return;
				}
				Out.append(Indent, '\t');
				Out += JoinTokens(LineTokens);
				Out.push_back('\n');
				LineTokens.clear();
			};
			for (const CppFrontend::FToken& Token : Tokens)
			{
				if (Token.Text == "(")
				{
					++ParenDepth;
				}
				else if (Token.Text == ")" && ParenDepth > 0)
				{
					--ParenDepth;
				}
				if (ParenDepth == 0 && Token.Text == "{")
				{
					LineTokens.push_back(Token);
					Flush();
					++Indent;
				}
				else if (ParenDepth == 0 && Token.Text == "}")
				{
					Flush();
					if (Indent > BaseIndent)
					{
						--Indent;
					}
					LineTokens.push_back(Token);
					Flush();
				}
				else if (ParenDepth == 0 && Token.Text == ";")
				{
					LineTokens.push_back(Token);
					Flush();
				}
				else
				{
					LineTokens.push_back(Token);
				}
			}
			Flush();
			if (!Out.empty())
			{
				Out.pop_back();
			}
			return Out;
		}

		std::string RewriteTokens(
		    const std::vector<CppFrontend::FToken>& Tokens,
		    std::vector<FDiagnostic>& Diagnostics,
		    const std::size_t BodyIndent = 0)
		{
			static const std::unordered_set<std::string> Forbidden = {
			    "reinterpret_cast", "dynamic_cast", "sizeof", "alignof", "typeid",
			    "decltype", "delete", "asm", "__asm", "co_await", "co_return",
			    "co_yield", "goto", "new", "static", "thread_local",
			    "extern", "register", "constexpr", "consteval", "constinit",
			    "volatile", "typedef", "union"};
			static const std::unordered_map<std::string, std::string> StdNames = {
			    {"string", "string"}, {"wstring", "wstring"},
			    {"vector", "array"}, {"deque", "deque"}, {"queue", "queue"},
			    {"stack", "stack"}, {"priority_queue", "priority_queue"},
			    {"unordered_map", "map"}, {"map", "ordered_map"},
			    {"unordered_multimap", "multimap"}, {"multimap", "ordered_multimap"},
			    {"unordered_set", "set"}, {"unordered_multiset", "multiset"},
			    {"multiset", "ordered_multiset"}, {"pair", "pair"},
			    {"optional", "optional"}, {"int8_t", "int8"}, {"uint8_t", "uint8"},
			    {"int16_t", "int16"}, {"uint16_t", "uint16"},
			    {"int32_t", "int"}, {"uint32_t", "unsigned int"},
			    {"int64_t", "int64"}, {"uint64_t", "uint64"},
			    {"size_t", sizeof(std::size_t) == 8 ? "uint64" : "unsigned int"},
			    {"uintptr_t", sizeof(std::uintptr_t) == 8 ? "uint64" : "unsigned int"},
			    {"intptr_t", sizeof(std::intptr_t) == 8 ? "int64" : "int"},
			    {"ptrdiff_t", sizeof(std::ptrdiff_t) == 8 ? "int64" : "int"}};
			static const std::unordered_set<std::string> ScalarSpecifiers = {
			    "void", "bool", "char", "short", "int", "long", "signed", "unsigned", "float", "double"};
			std::vector<CppFrontend::FToken> Rewritten;
			Rewritten.reserve(Tokens.size());
			for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
			{
				const CppFrontend::FToken& Token = Tokens[Index];
				if (Token.Kind == CppFrontend::ETokenKind::Number)
				{
					CppFrontend::FToken Replacement = Token;
					std::string Error;
					if (!Detail::NormalizeNumberLiteral(Token.Text, Replacement.Text, Error))
					{
						AddError(Diagnostics, Token.Location, "cpp.transpile.numeric_literal_unsupported",
						    std::move(Error));
					}

					else
					{
						Rewritten.push_back(std::move(Replacement));
					}

					continue;
				}
				if (ScalarSpecifiers.count(Token.Text))
				{
					// Body declarations and cast targets must use the same platform
					// mapping as structured function signatures and fields.
					FType Type;
					Type.Location = Token.Location;
					Type.BaseName = Token.Text;
					while (Index + 1 < Tokens.size() && ScalarSpecifiers.count(Tokens[Index + 1].Text))
					{
						Type.BaseName += " " + Tokens[++Index].Text;
					}

					Type.Spelling = Type.BaseName;
					CppFrontend::FToken Replacement = Token;
					Replacement.Text = FormatType(Type, {}, Diagnostics);
					Rewritten.push_back(std::move(Replacement));
					continue;
				}
				if (Forbidden.find(Token.Text) != Forbidden.end())
				{
					AddError(Diagnostics, Token.Location, "cpp.transpile.expression_unsupported",
					    "unsupported C++ expression/statement construct: " + Token.Text);
					continue;
				}
				if (Token.Text == "std" && Index + 2 < Tokens.size() && Tokens[Index + 1].Text == "::")
				{
					const std::string& Name = Tokens[Index + 2].Text;
					if (Name == "move" || Name == "forward" || Name == "unique_ptr" || Name == "shared_ptr" || Name == "weak_ptr")
					{
						AddError(Diagnostics, Token.Location, "cpp.transpile.std_construct_unsupported",
						    "C++ ownership/move construct cannot be source-transpiled safely: std::" + Name);
						Index += 2;
						continue;
					}
					if (const auto It = StdNames.find(Name); It != StdNames.end())
					{
						CppFrontend::FToken Replacement = Token;
						Replacement.Text = It->second;
						Rewritten.push_back(std::move(Replacement));
						Index += 2;
						continue;
					}
					AddError(Diagnostics, Token.Location, "cpp.transpile.std_symbol_unsupported",
					    "unsupported std symbol in transpiled body: std::" + Name);
					Index += 2;
					continue;
				}
				if (Token.Text == "static_cast")
				{
					CppFrontend::FToken Replacement = Token;
					Replacement.Text = "cast";
					Rewritten.push_back(std::move(Replacement));
					continue;
				}
				if (Token.Text == "->")
				{
					if (!Rewritten.empty() && Rewritten.back().Text == "this")
					{
						CppFrontend::FToken Replacement = Token;
						Replacement.Text = ".";
						Rewritten.push_back(std::move(Replacement));
					}
					else
					{
						AddError(Diagnostics, Token.Location, "cpp.transpile.pointer_member_access_unsupported",
						    "pointer member access cannot be preserved; only this->member is lowered");
					}
					continue;
				}
				Rewritten.push_back(Token);
			}
			return BodyIndent == 0
			    ? JoinTokens(Rewritten)
			    : FormatBodyTokens(Rewritten, BodyIndent);
		}

		bool IsVeroDefaultLiteral(const std::vector<CppFrontend::FToken>& Tokens)
		{
			if (Tokens.size() != 1)
			{
				return false;
			}
			const CppFrontend::FToken& Token = Tokens.front();
			return Token.Kind == CppFrontend::ETokenKind::Number || Token.Kind == CppFrontend::ETokenKind::StringLiteral || Token.Kind == CppFrontend::ETokenKind::CharacterLiteral || Token.Text == "true" || Token.Text == "false";
		}

		std::string FormatParameters(
		    const std::vector<FParameter>& Parameters,
		    const FTypeMapOptions& MapOptions,
		    std::vector<FDiagnostic>& Diagnostics)
		{
			std::string Out;
			for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
			{
				if (Index > 0)
				{
					Out += ", ";
				}
				Out += FormatType(Parameters[Index].Type, MapOptions, Diagnostics);
				Out += ' ';
				Out += Parameters[Index].Name;
				if (!Parameters[Index].DefaultValueTokens.empty())
				{
					if (!IsVeroDefaultLiteral(Parameters[Index].DefaultValueTokens))
					{
						AddError(Diagnostics, Parameters[Index].DefaultValueTokens.front().Location,
						    "cpp.transpile.default_argument_expression_unsupported",
						    "VeroScript parameter defaults are literal-only; this C++ default expression cannot be preserved");
						continue;
					}
					Out += " = ";
					Out += RewriteTokens(Parameters[Index].DefaultValueTokens, Diagnostics);
				}
			}
			return Out;
		}

		bool AppendSourceAnnotations(
		    FEmitter& Emitter,
		    const std::vector<FAttribute>& Attributes,
		    const std::string_view Indent,
		    const std::vector<FAttributeMapping>& Mappings,
		    std::vector<FDiagnostic>& Diagnostics)
		{
			for (const FAttribute& Attribute : Attributes)
			{
				const std::string CppName = Attribute.NamespaceName.empty()
				    ? Attribute.Name
				    : Attribute.NamespaceName + "::" + Attribute.Name;
				std::string AnnotationName;
				if (Attribute.NamespaceName == "veroscript" || Attribute.NamespaceName == "vero")
				{
					AnnotationName = Attribute.Name;
				}
				else
				{
					const auto It = std::find_if(Mappings.begin(), Mappings.end(),
					    [&](const FAttributeMapping& Mapping)
					{
						return Mapping.CppName == CppName;
					});
					if (It != Mappings.end())
					{
						AnnotationName = It->AnnotationName;
					}
				}
				if (AnnotationName.empty())
				{
					AddError(Diagnostics, Attribute.Location,
					    "cpp.transpile.attribute_requires_annotation_policy",
					    "attribute requires an explicit C++-to-VeroScript annotation mapping: " + CppName);
					return false;
				}
				if (VeroScript::IsLanguageKeyword(AnnotationName))
				{
					AddError(Diagnostics, Attribute.Location,
					    "cpp.transpile.invalid_annotation_name",
					    "VeroScript annotation name cannot be a language keyword: " + AnnotationName);
					return false;
				}
				std::string Line(Indent);
				Line += '@';
				Line += AnnotationName;
				Line += '(';
				for (std::size_t Index = 0; Index < Attribute.Arguments.size(); ++Index)
				{
					if (Index > 0)
					{
						Line += ", ";
					}
					const FAttributeArgument& Argument = Attribute.Arguments[Index];
					Line += Argument.Name;
					if (Argument.bHasValue)
					{
						if (!IsValidAnnotationValue(Argument.Value))
						{
							AddError(Diagnostics, Attribute.Location,
							    "cpp.transpile.annotation_argument_unsupported",
							    "attribute argument cannot be represented by VeroScript annotation syntax: " + Argument.Name + " = " + Argument.Value);
							return false;
						}
						Line += " = ";
						Line += Argument.Value;
					}
				}
				Line += ')';
				Emitter.Line(Line);
			}
			return true;
		}

		bool ValidateAttributeMappings(
		    const std::vector<FAttributeMapping>& Mappings,
		    std::vector<FDiagnostic>& Diagnostics,
		    const CppFrontend::FSourceLocation& Location)
		{
			std::unordered_set<std::string> Seen;
			for (const FAttributeMapping& Mapping : Mappings)
			{
				const auto IsIdentifier = [](const std::string_view Value)
				{
					const auto IsAlpha = [](const char Character)
					{
						return (Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z');
					};
					if (Value.empty() || !(IsAlpha(Value.front()) || Value.front() == '_'))
					{
						return false;
					}
					return std::all_of(Value.begin() + 1, Value.end(),
					    [&](const char Character)
					{
						return IsAlpha(Character) || (Character >= '0' && Character <= '9') || Character == '_';
					});
				};
				const auto IsCppAttributeName = [&](const std::string_view Value)
				{
					if (Value.empty())
					{
						return false;
					}
					std::size_t Begin = 0;
					while (Begin < Value.size())
					{
						const std::size_t Separator = Value.find("::", Begin);
						const std::string_view Part = Value.substr(Begin,
						    Separator == std::string_view::npos
						        ? std::string_view::npos
						        : Separator - Begin);
						if (!IsIdentifier(Part))
						{
							return false;
						}
						if (Separator == std::string_view::npos)
						{
							return true;
						}
						Begin = Separator + 2;
					}
					return false;
				};
				if (!IsCppAttributeName(Mapping.CppName) || Mapping.CppName.rfind("veroscript::", 0) == 0 || Mapping.CppName.rfind("vero::", 0) == 0 || !IsIdentifier(Mapping.AnnotationName) || VeroScript::IsLanguageKeyword(Mapping.AnnotationName))
				{
					AddError(Diagnostics, Location,
					    "cpp.transpile.invalid_attribute_mapping",
					    "attribute mappings require an identifier or namespace-qualified C++ attribute name outside the reserved veroscript::/vero:: namespaces and a valid VeroScript annotation identifier");
					return false;
				}
				if (!Seen.insert(Mapping.CppName).second)
				{
					AddError(Diagnostics, Location,
					    "cpp.transpile.duplicate_attribute_mapping",
					    "duplicate attribute mapping for: " + Mapping.CppName);
					return false;
				}
			}
			return true;
		}

		std::string FormatTemplateParameters(
		    const std::vector<CppFrontend::FTemplateParameter>& Parameters,
		    const FTypeMapOptions& BaseOptions,
		    std::vector<FDiagnostic>& Diagnostics)
		{
			if (Parameters.empty())
			{
				return {};
			}
			FTypeMapOptions Options = BaseOptions;
			std::string Out = "template<";
			for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
			{
				if (Index > 0)
				{
					Out += ", ";
				}
				Out += Parameters[Index].Name;
				if (Parameters[Index].bHasDefault)
				{
					Out += " = ";
					Out += FormatType(Parameters[Index].DefaultType, Options, Diagnostics);
				}
				Options.KnownTypeParameters.push_back(Parameters[Index].Name);
			}
			Out += "> ";
			return Out;
		}

		void EmitFunction(
		    FEmitter& Emitter,
		    const FFunction& Function,
		    const FTypeMapOptions& MapOptions,
		    const std::vector<FAttributeMapping>& AttributeMappings,
		    std::vector<FDiagnostic>& Diagnostics,
		    const bool bMember,
		    const bool bAllowMethodDeclaration = false)
		{
			if (bMember && !Function.TemplateParameters.empty())
			{
				AddError(Diagnostics, Function.Location,
				    "cpp.transpile.member_function_template_unsupported",
				    "VeroScript does not currently support a method with its own template parameter list");
				return;
			}
			FTypeMapOptions FunctionMapOptions = MapOptions;
			for (const CppFrontend::FTemplateParameter& Parameter : Function.TemplateParameters)
			{
				FunctionMapOptions.KnownTypeParameters.push_back(Parameter.Name);
			}
			if (Function.bConstexpr)
			{
				AddError(Diagnostics, Function.Location,
				    "cpp.transpile.constexpr_function_unsupported",
				    "constexpr function semantics cannot be preserved because VeroScript has no constexpr evaluation model");
				return;
			}
			if (Function.bConsteval)
			{
				AddError(Diagnostics, Function.Location,
				    "cpp.transpile.consteval_function_unsupported",
				    "consteval requires compile-time-only invocation semantics that VeroScript does not provide");
				return;
			}
			if (Function.bNoexcept)
			{
				AddError(Diagnostics, Function.Location,
				    "cpp.transpile.noexcept_function_unsupported",
				    "noexcept cannot be preserved by the VeroScript source transpiler");
				return;
			}
			AppendSourceAnnotations(Emitter, Function.Attributes,
			    bMember ? "    " : "", AttributeMappings, Diagnostics);
			Emitter.Mark(Function.Location);
			std::string Header = FormatTemplateParameters(
			    Function.TemplateParameters, MapOptions, Diagnostics);
			if (Function.bExplicit)
			{
				Header += "explicit ";
			}
			if (bMember && Function.bStatic)
			{
				Header += "static ";
			}
			if (Function.bVirtual)
			{
				Header += "virtual ";
			}
			if (!Function.bConstructor && !Function.bDestructor)
			{
				Header += FormatType(Function.ReturnType, FunctionMapOptions, Diagnostics);
				Header += ' ';
			}
			Header += bMember ? Function.Name : LeafName(Function.QualifiedName);
			Header += '(';
			Header += FormatParameters(Function.Parameters, FunctionMapOptions, Diagnostics);
			Header += ')';
			if (Function.bConstMethod)
			{
				Header += " const";
			}
			if (Function.bOverride)
			{
				Header += " override";
			}
			if (Function.bFinal)
			{
				Header += " final";
			}
			if (Function.bDeleted)
			{
				Header += " = delete";
			}
			else if (Function.bPureVirtual)
			{
				Header += " = 0";
			}
			else if (Function.bDefaulted)
			{
				Header += " = default";
			}

			if (Function.bPureVirtual)
			{
				// A legal C++ pure virtual may also have a separate out-of-class
				// definition. Keep the abstract declaration here; its body is emitted
				// separately after the owning record.
				Emitter.Line((bMember ? "    " : std::string{}) + Header + ";");
			}
			else if (Function.bHasBody)
			{
				const std::string Prefix = bMember ? "    " : std::string{};
				const std::string Body = RewriteTokens(
				    Function.BodyTokens, Diagnostics, bMember ? 2u : 1u);
				Emitter.Line(Prefix + Header);
				Emitter.Line(Prefix + "{");
				Emitter.Line(Body);
				Emitter.Line(Prefix + "}");
			}
			else if (Function.bDeleted || Function.bDefaulted)
			{
				Emitter.Line((bMember ? "    " : std::string{}) + Header + ";");
			}
			else if (bMember && bAllowMethodDeclaration && Function.bOutOfClassMember && !Function.bConstructor && !Function.bDestructor)
			{
				Emitter.Line("    " + Header + ";");
			}
			else
			{
				AddError(Diagnostics, Function.Location, "cpp.transpile.function_definition_required",
				    "source transpilation requires a function body (or = default, = delete, or = 0): " + Function.QualifiedName);
			}
		}

		void EmitEnum(
		    FEmitter& Emitter,
		    const FEnum& Enum,
		    const std::vector<FAttributeMapping>& AttributeMappings,
		    std::vector<FDiagnostic>& Diagnostics)
		{
			std::vector<std::int64_t> NumericValues;
			std::size_t FailedIndex = 0;
			std::string ExpressionError;
			if (!EvaluateEnumValues(
			        Enum, NumericValues, FailedIndex, ExpressionError))
			{
				const CppFrontend::FSourceLocation& Location = FailedIndex < Enum.Values.size()
				    ? Enum.Values[FailedIndex].Location
				    : Enum.Location;
				AddError(Diagnostics, Location,
				    "cpp.transpile.enum_initializer_not_constant_integer",
				    "unsupported enum integer constant expression: " + ExpressionError);
				return;
			}
			std::int64_t EnumMinimum = 0;
			std::int64_t EnumMaximum = 0;
			if (!TryGetEnumValueRange(Enum.UnderlyingType, EnumMinimum, EnumMaximum))
			{
				AddError(Diagnostics, Enum.Location,
				    "cpp.transpile.enum_underlying_unsupported",
				    "cannot validate enum value range for underlying type: " + Enum.UnderlyingType);
				return;
			}
			for (std::size_t Index = 0; Index < NumericValues.size(); ++Index)
			{
				if (NumericValues[Index] < EnumMinimum || NumericValues[Index] > EnumMaximum)
				{
					AddError(Diagnostics, Enum.Values[Index].Location,
					    "cpp.transpile.enum_value_out_of_range",
					    "enum value is outside the range of " + Enum.UnderlyingType + ": " + std::to_string(NumericValues[Index]));
					return;
				}
			}
			Emitter.OpenNamespaces(ScopeName(Enum.QualifiedName));
			AppendSourceAnnotations(Emitter, Enum.Attributes, "",
			    AttributeMappings, Diagnostics);
			Emitter.Mark(Enum.Location);
			Emitter.Line(std::string(Enum.bScoped ? "enum class " : "enum ") + LeafName(Enum.QualifiedName) + " : " + Enum.UnderlyingType);
			Emitter.Line("{");
			for (std::size_t Index = 0; Index < Enum.Values.size(); ++Index)
			{
				AppendSourceAnnotations(Emitter, Enum.Values[Index].Attributes,
				    "    ", AttributeMappings, Diagnostics);
				std::string Line = "    " + Enum.Values[Index].Name;
				Line += " = " + std::to_string(NumericValues[Index]);
				// Core enum declarations require a literal. Preserve the range with
				// a suffix, including its special handling of signed minimum values.
				if (EnumMinimum == 0)
				{
					Line += 'U';
				}
				else if (NumericValues[Index] < std::numeric_limits<std::int32_t>::min() ||
				    NumericValues[Index] > std::numeric_limits<std::int32_t>::max())
				{
					Line += 'L';
				}
				if (Index + 1 < Enum.Values.size())
				{
					Line += ',';
				}
				Emitter.Line(Line);
			}
			Emitter.Line("};");
			Emitter.CloseNamespaces();
			Emitter.Line();
		}

		std::string AccessName(const EAccess Access)
		{
			switch (Access)
			{
			case EAccess::Public:
				return "public:";
			case EAccess::Protected:
				return "protected:";
			case EAccess::Private:
				return "private:";
			default:
				{
					break;
				}
			}
			return "private:";
		}

		std::string SourceMapJson(
		    const std::string& GeneratedFile,
		    const std::vector<FMapEntry>& Mappings)
		{
			std::ostringstream Out;
			Out << "{\"version\":1,\"generatedFile\":\"" << JsonEscape(GeneratedFile)
			    << "\",\"mappings\":[";
			for (std::size_t Index = 0; Index < Mappings.size(); ++Index)
			{
				if (Index > 0)
				{
					Out << ',';
				}
				const FMapEntry& Entry = Mappings[Index];
				Out << "{\"generatedLine\":" << Entry.GeneratedLine
				    << ",\"source\":\"" << JsonEscape(Entry.Original.File)
				    << "\",\"line\":" << Entry.Original.Line
				    << ",\"column\":" << Entry.Original.Column << '}';
			}
			Out << "]}";
			return Out.str();
		}

		std::string OutputStem(const std::string& Path)
		{
			const std::size_t Separator = Path.find_last_of("/\\");
			std::string Name = Path.substr(Separator == std::string::npos ? 0 : Separator + 1);
			const std::size_t Dot = Name.find_last_of('.');
			if (Dot != std::string::npos && Dot != 0)
			{
				Name.resize(Dot);
			}
			return Name.empty() ? "generated" : Name;
		}

		std::uint64_t StablePathHash(const std::string& Path)
		{
			std::uint64_t Value = UINT64_C(1469598103934665603);
			for (const unsigned char Character : Path)
			{
				Value ^= Character;
				Value *= UINT64_C(1099511628211);
			}
			return Value;
		}

		std::string HexSuffix(const std::uint64_t Value)
		{
			static constexpr char Hex[] = "0123456789abcdef";
			std::string Result(16, '0');
			for (std::size_t Index = 0; Index < Result.size(); ++Index)
			{
				const std::size_t Shift = (Result.size() - Index - 1) * 4;
				Result[Index] = Hex[(Value >> Shift) & 0xf];
			}
			return Result;
		}

		bool IsFunctionDefinition(const FFunction& Function)
		{
			return Function.bHasBody || Function.bDeleted || Function.bDefaulted || Function.bPureVirtual;
		}

		void EmitOutOfClassDefinition(
		    FEmitter& Emitter,
		    const FRecord& Record,
		    const FFunction& Function,
		    const FTypeMapOptions& MapOptions,
		    std::vector<FDiagnostic>& Diagnostics)
		{
			FTypeMapOptions FunctionMapOptions = MapOptions;
			for (const CppFrontend::FTemplateParameter& Parameter : Record.TemplateParameters)
			{
				FunctionMapOptions.KnownTypeParameters.push_back(Parameter.Name);
			}

			Emitter.OpenNamespaces(ScopeName(Record.QualifiedName));
			Emitter.Mark(Function.Location);
			std::string Header;
			if (!Record.TemplateParameters.empty())
			{
				Header = "template<";
				for (std::size_t Index = 0; Index < Record.TemplateParameters.size(); ++Index)
				{
					if (Index > 0)
					{
						Header += ", ";
					}
					Header += Record.TemplateParameters[Index].Name;
				}
				Header += "> ";
			}
			if (!Function.bConstructor && !Function.bDestructor)
			{
				Header += FormatType(Function.ReturnType, FunctionMapOptions, Diagnostics);
				Header += ' ';
			}
			Header += LeafName(Record.QualifiedName);
			if (!Record.TemplateParameters.empty())
			{
				Header += '<';
				for (std::size_t Index = 0; Index < Record.TemplateParameters.size(); ++Index)
				{
					if (Index > 0)
					{
						Header += ", ";
					}
					Header += Record.TemplateParameters[Index].Name;
				}
				Header += '>';
			}
			Header += "::" + Function.Name + '(';
			std::vector<FParameter> DefinitionParameters = Function.Parameters;
			for (FParameter& Parameter : DefinitionParameters)
			{
				Parameter.DefaultValue.clear();
				Parameter.DefaultValueTokens.clear();
			}
			Header += FormatParameters(
			    DefinitionParameters, FunctionMapOptions, Diagnostics);
			Header += ')';
			if (Function.bConstMethod)
			{
				Header += " const";
			}
			Emitter.Line(Header);
			Emitter.Line("{");
			if (Function.bHasBody)
			{
				Emitter.Line(RewriteTokens(Function.BodyTokens, Diagnostics, 1u));
			}
			Emitter.Line("}");
			Emitter.CloseNamespaces();
			Emitter.Line();
		}

		std::string FunctionIdentityWithAliases(
		    FFunction Function,
		    const std::vector<FTypeAlias>& Aliases,
		    const FTypeMapOptions& Lookup)
		{
			Function.ReturnType = ResolveTypeAliases(
			    std::move(Function.ReturnType), Aliases, Lookup);
			for (FParameter& Parameter : Function.Parameters)
			{
				Parameter.Type = ResolveTypeAliases(
				    std::move(Parameter.Type), Aliases, Lookup);
			}
			return GetFunctionIdentity(Function);
		}

		bool IsVariableDefinition(const FVariable& Variable)
		{
			return !Variable.bExtern || !Variable.InitializerTokens.empty();
		}

		void RejectGenericAliasUseInOpaqueTokens(
		    const std::vector<CppFrontend::FToken>& Tokens,
		    const std::vector<FTypeAlias>& Aliases,
		    std::vector<FDiagnostic>& Diagnostics)
		{
			for (std::size_t TokenIndex = 0; TokenIndex < Tokens.size(); ++TokenIndex)
			{
				const CppFrontend::FToken& Token = Tokens[TokenIndex];
				if (Token.Kind != CppFrontend::ETokenKind::Identifier)
				{
					continue;
				}
				if (TokenIndex + 1 >= Tokens.size() || Tokens[TokenIndex + 1].Text != "<")
				{
					continue;
				}
				const auto It = std::find_if(Aliases.begin(), Aliases.end(),
				    [&](const FTypeAlias& Alias)
				{
					return !Alias.TemplateParameters.empty() && (Token.Text == Alias.Name || Token.Text == LeafName(Alias.QualifiedName));
				});
				if (It == Aliases.end())
				{
					continue;
				}
				AddError(Diagnostics, Token.Location,
				    "cpp.transpile.generic_alias_in_opaque_tokens",
				    "generic alias '" + It->QualifiedName + "' can be expanded in structured declaration types, but not inside an opaque C++ expression or function body");
				return;
			}
		}

		void RejectParameterAttributes(
		    const std::vector<FAttribute>& Attributes,
		    std::vector<FDiagnostic>& Diagnostics)
		{
			if (Attributes.empty())
			{
				return;
			}
			AddError(Diagnostics, Attributes.front().Location,
			    "cpp.transpile.parameter_annotation_unsupported",
			    "VeroScript source declarations do not currently carry parameter-level annotations; use BindGen when parameter metadata must be preserved");
		}
	}

	bool FTranspileResult::Succeeded() const
	{
		return std::none_of(Diagnostics.begin(), Diagnostics.end(),
		    [](const FDiagnostic& Diagnostic)
		{
			return Diagnostic.Severity == EDiagnosticSeverity::Error;
		});
	}

	using FSeparatedMethod = std::pair<const FRecord*, const FFunction*>;

	static bool RejectTemplateParameterShadows(const FTranslationUnit& Unit,
	    const FTranslationUnit& CompleteUnit, const FTranspileOptions& Options,
	    std::vector<FDiagnostic>& Diagnostics)
	{
		std::unordered_set<std::string> TypeNames;
		for (const auto& Record : CompleteUnit.Records)
		{
			TypeNames.insert(Record.QualifiedName);
		}

		for (const auto& Enum : CompleteUnit.Enums)
		{
			TypeNames.insert(Enum.QualifiedName);
		}

		for (const auto& Alias : CompleteUnit.TypeAliases)
		{
			if (Alias.TemplateParameters.empty())
			{
				TypeNames.insert(Alias.QualifiedName);
			}
		}

		TypeNames.insert(Options.AdditionalKnownHostTypes.begin(), Options.AdditionalKnownHostTypes.end());
		TypeNames.insert(Options.AdditionalKnownEnumTypes.begin(), Options.AdditionalKnownEnumTypes.end());
		for (const auto& Alias : Options.AdditionalKnownTypeAliases)
		{
			if (Alias.TemplateParameters.empty())
			{
				TypeNames.insert(Alias.QualifiedName);
			}
		}

		const auto Check = [&](const auto& Parameters)
		{
			// Core rejects an exact globally declared name as a type parameter.
			// Qualified names in other namespaces do not reserve their leaf name.
			for (const auto& Parameter : Parameters)
			{
				if (TypeNames.count(Parameter.Name))
				{
					AddError(Diagnostics, Parameter.Location, "cpp.transpile.template_parameter_shadow_unsupported",
					    "VeroScript template parameters cannot shadow a global type or alias; rename the C++ parameter '" + Parameter.Name + "' before transpiling");
					return false;
				}
			}

			return true;
		};
		for (const auto& Record : Unit.Records)
		{
			if (!Check(Record.TemplateParameters)) return false;
			for (const auto& Method : Record.Methods)
			{
				if (!Check(Method.TemplateParameters)) return false;
			}
		}
		for (const auto& Function : Unit.Functions)
		{
			if (!Check(Function.TemplateParameters)) return false;
		}

		return true;
	}

	// Standard spellings in opaque tokens can only lower to builtins when their
	// namespace is not rebound. VeroScript's soft builtin pair also needs a check
	// for a same-name, same-arity user template after lowering.
	static bool RejectShadowedStandardNames(const FTranslationUnit& Unit,
	    const FTranslationUnit& CompleteUnit, const FTranspileOptions& Options,
	    std::vector<FDiagnostic>& Diagnostics)
	{
		std::unordered_set<std::string> StandardNamespaceScopes;
		for (const auto& Name : CompleteUnit.Namespaces)
		{
			const auto Parent = ScopeName(Name);
			if (LeafName(Name) == "std" && !Parent.empty())
			{
				StandardNamespaceScopes.insert(Parent);
			}
		}
		std::unordered_set<std::string> Namespaces;
		if (!StandardNamespaceScopes.empty())
		{
			Namespaces.insert(CompleteUnit.Namespaces.begin(), CompleteUnit.Namespaces.end());
		}
		const auto ResolveNamespace = [&](std::string Name, std::string Scope)
		{
			if (Name.rfind("::", 0) == 0)
			{
				Name.erase(0, 2);
				Scope.clear();
			}
			while (true)
			{
				const auto Candidate = Scope.empty() ? Name : Scope + "::" + Name;
				if (Namespaces.count(Candidate)) return Candidate;
				if (Scope.empty()) return std::string{};
				Scope = ScopeName(Scope);
			}
		};
		std::unordered_map<std::string, std::vector<const FUsingNamespace*>> UsingNamespacesByFile;
		if (!StandardNamespaceScopes.empty())
		{
			for (const auto& Using : CompleteUnit.UsingNamespaces)
			{
				const auto& File = Using.Location.File.empty() ? Unit.SourcePath : Using.Location.File;
				UsingNamespacesByFile[File].push_back(&Using);
			}
		}
		std::unordered_map<std::string, std::unordered_set<std::string>> ImportedScopesByFile;
		const auto ImportedScopes = [&](const std::string& File) -> const std::unordered_set<std::string>&
		{
			const auto Cached = ImportedScopesByFile.try_emplace(File);
			if (!Cached.second) return Cached.first->second;
			auto& Scopes = Cached.first->second;
			const auto UsingDeclarations = UsingNamespacesByFile.find(File);
			if (UsingDeclarations == UsingNamespacesByFile.end()) return Scopes;
			// Declarations are shared by the project, but namespace imports retain
			// their source file's lookup context. Store only this file's additions.
			std::unordered_map<std::string, std::vector<std::string>> Importers;
			for (const auto* Using : UsingDeclarations->second)
			{
				const auto Target = ResolveNamespace(Using->NamespaceName, Using->Scope);
				if (!Target.empty())
				{
					Importers[Target].push_back(Using->Scope);
				}
			}
			std::vector<std::string> Pending;
			for (const auto& Importer : Importers)
			{
				if (StandardNamespaceScopes.count(Importer.first))
				{
					Pending.push_back(Importer.first);
				}
			}
			for (std::size_t Index = 0; Index < Pending.size(); ++Index)
			{
				const auto Found = Importers.find(Pending[Index]);
				if (Found == Importers.end())
				{
					continue;
				}
				for (const auto& Scope : Found->second)
				{
					if (!StandardNamespaceScopes.count(Scope) && Scopes.insert(Scope).second)
					{
						Pending.push_back(Scope);
					}
				}
			}
			return Scopes;
		};
		std::vector<std::string> ShadowScopes;
		for (const auto& Record : CompleteUnit.Records)
		{
			if (Record.Name == "pair" && Record.TemplateParameters.size() == 2)
			{
				ShadowScopes.push_back(ScopeName(Record.QualifiedName));
			}
		}

		for (const auto& Name : Options.AdditionalKnownHostTypes)
		{
			if (LeafName(Name) == "pair" &&
			    std::none_of(CompleteUnit.Records.begin(), CompleteUnit.Records.end(),
			        [&](const auto& Record)
			{
				return Record.QualifiedName == Name;
			}))
			{
				ShadowScopes.push_back(ScopeName(Name));
			}
		}

		if (ShadowScopes.empty() && StandardNamespaceScopes.empty()) return true;
		FTypeMapOptions AliasLookup;
		AliasLookup.KnownHostTypes = Options.AdditionalKnownHostTypes;
		AliasLookup.KnownEnumTypes = Options.AdditionalKnownEnumTypes;
		for (const auto& Record : CompleteUnit.Records)
		{
			AliasLookup.KnownHostTypes.push_back(Record.QualifiedName);
		}

		for (const auto& Enum : CompleteUnit.Enums)
		{
			AliasLookup.KnownEnumTypes.push_back(Enum.QualifiedName);
		}

		const auto Shadowed = [&](const std::string& Scope)
		{
			return std::any_of(ShadowScopes.begin(), ShadowScopes.end(),
			    [&](const auto& Candidate)
			{
				return Candidate.empty() || Scope == Candidate || Scope.rfind(Candidate + "::", 0) == 0;
			});
		};
		auto Aliases = Unit.TypeAliases;
		Aliases.insert(Aliases.end(), Options.AdditionalKnownTypeAliases.begin(),
		    Options.AdditionalKnownTypeAliases.end());
		const auto Fail = [&](const CppFrontend::FSourceLocation& Location)
		{
			AddError(Diagnostics, Location, "cpp.transpile.standard_pair_shadowed",
			    "std::pair cannot be preserved beside a visible user pair<T,U> template; rename the user template or adapt the standard type explicitly");
			return false;
		};
		const auto ResolvedType = [&](const auto& Self, const FType& Resolved,
		                              const std::string& Scope) -> bool
		{
			if ((Resolved.BaseName == "std::pair" || Resolved.BaseName == "::std::pair" ||
			        Resolved.BaseName.rfind("std::pair<", 0) == 0 ||
			        Resolved.BaseName.rfind("::std::pair<", 0) == 0) &&
			    Shadowed(Scope))
			{
				return Fail(Resolved.Location);
			}

			for (const auto& Argument : Resolved.TemplateArguments)
			{
				if (!Self(Self, Argument, Scope)) return false;
			}

			return true;
		};
		const auto Type = [&](const FType& Original, const std::string& Scope)
		{
			if (!Shadowed(Scope)) return true;
			// The resolver already walks arguments with an active-alias set. Never
			// restart it on its leaves: a stopped recursive alias could expand again.
			const auto Resolved = ResolveTypeAliases(Original, Aliases, AliasLookup);
			return ResolvedType(ResolvedType, Resolved, Scope);
		};
		const auto Tokens = [&](const auto& Values, const std::string& Scope)
		{
			const bool bPairShadowed = Shadowed(Scope);
			if (!bPairShadowed && StandardNamespaceScopes.empty()) return true;
			struct FFileContext
			{
				const std::unordered_set<std::string>* Imports = nullptr;
				bool bLexicalShadowed = false;
			};
			std::unordered_map<std::string, FFileContext> FileContexts;
			for (std::size_t I = 0; I + 2 < Values.size(); ++I)
			{
				if (Values[I].Text != "std" || Values[I + 1].Text != "::")
				{
					continue;
				}
				FFileContext Context;
				if (!StandardNamespaceScopes.empty())
				{
					const auto& File = Values[I].Location.File.empty() ? Unit.SourcePath : Values[I].Location.File;
					const auto Cached = FileContexts.try_emplace(File);
					if (Cached.second)
					{
						auto& NewContext = Cached.first->second;
						NewContext.Imports = &ImportedScopes(File);
						for (auto Candidate = Scope;; Candidate = ScopeName(Candidate))
						{
							if (StandardNamespaceScopes.count(Candidate) || NewContext.Imports->count(Candidate))
							{
								NewContext.bLexicalShadowed = true;
								break;
							}
							if (Candidate.empty())
							{
								break;
							}
						}
					}
					Context = Cached.first->second;
				}
				std::size_t QualifierBegin = I;
				while (QualifierBegin >= 2 && Values[QualifierBegin - 1].Text == "::" && Values[QualifierBegin - 2].Kind == CppFrontend::ETokenKind::Identifier && Values[QualifierBegin - 2].Text != "return" && Values[QualifierBegin - 2].Text != "throw" && Values[QualifierBegin - 2].Text != "const" && Values[QualifierBegin - 2].Text != "volatile" && Values[QualifierBegin - 2].Text != "typename")
				{
					QualifierBegin -= 2;
				}
				bool bNamespaceShadowed = false;
				if (QualifierBegin < I)
				{
					std::string Qualifier;
					for (std::size_t At = QualifierBegin; At + 1 < I; ++At)
					{
						Qualifier += Values[At].Text;
					}
					if (QualifierBegin > 0 && Values[QualifierBegin - 1].Text == "::")
					{
						Qualifier.insert(0, "::");
					}
					const auto Target = ResolveNamespace(Qualifier, Scope);
					bNamespaceShadowed = !Target.empty() && (StandardNamespaceScopes.count(Target) || (Context.Imports && Context.Imports->count(Target)));
				}
				else if (I == 0 || Values[I - 1].Text != "::")
				{
					bNamespaceShadowed = Context.bLexicalShadowed;
				}
				if (bNamespaceShadowed)
				{
					AddError(Diagnostics, Values[I].Location, "cpp.transpile.standard_namespace_shadowed",
					    "a local std namespace cannot be lowered as the standard library; rename it or adapt its types explicitly");
					return false;
				}
				if (bPairShadowed && Values[I + 2].Text == "pair")
				{
					return Fail(Values[I].Location);
				}
			}

			return true;
		};
		const auto TemplateDefaults = [&](const auto& Parameters, const std::string& Scope)
		{
			for (const auto& Parameter : Parameters)
			{
				if (Parameter.bHasDefault && !Type(Parameter.DefaultType, Scope)) return false;
			}

			return true;
		};
		const auto Function = [&](const FFunction& Value)
		{
			const auto Scope = ScopeName(Value.QualifiedName);
			if (!TemplateDefaults(Value.TemplateParameters, Scope) || !Type(Value.ReturnType, Scope) ||
			    !Tokens(Value.BodyTokens, Scope))
			{
				return false;
			}

			for (const auto& Parameter : Value.Parameters)
			{
				if (!Type(Parameter.Type, Scope) || !Tokens(Parameter.DefaultValueTokens, Scope))
				{
					return false;
				}
			}

			return true;
		};
		for (const auto& Alias : Unit.TypeAliases)
		{
			if (!TemplateDefaults(Alias.TemplateParameters, ScopeName(Alias.QualifiedName)) ||
			    !Type(Alias.TargetType, ScopeName(Alias.QualifiedName)))
			{
				return false;
			}
		}

		for (const auto& Value : Unit.Variables)
		{
			if (!Type(Value.Type, ScopeName(Value.QualifiedName)) ||
			    !Tokens(Value.InitializerTokens, ScopeName(Value.QualifiedName)))
			{
				return false;
			}
		}

		for (const auto& Value : Unit.Functions)
		{
			if (!Function(Value)) return false;
		}

		for (const auto& Record : Unit.Records)
		{
			if (!TemplateDefaults(Record.TemplateParameters, ScopeName(Record.QualifiedName))) return false;
			for (const auto& Friend : Record.FriendDeclarations)
			{
				if (!Tokens(Friend, Record.QualifiedName)) return false;
			}

			for (const auto& Base : Record.BaseTypes)
			{
				FType BaseType = Base.Type;
				if (BaseType.BaseName.empty())
				{
					BaseType.BaseName = Base.TypeName;
				}

				BaseType.DeclarationScope = Record.QualifiedName;
				BaseType.Location = Base.Location;
				if (!Type(BaseType, Record.QualifiedName)) return false;
			}
			for (const auto& Field : Record.Fields)
			{
				if (!Type(Field.Type, Record.QualifiedName) ||
				    !Tokens(Field.InitializerTokens, Record.QualifiedName))
				{
					return false;
				}
			}

			for (const auto& Method : Record.Methods)
			{
				if (!Function(Method)) return false;
			}
		}
		return true;
	}

	static FTranspileResult TranspileUnit(
	    const FTranslationUnit& Unit,
	    const FTranspileOptions& Options,
	    const bool bAllowMethodDeclarations = false,
	    const std::vector<FSeparatedMethod>& Definitions = {},
	    std::vector<FMapEntry>* const OutMappings = nullptr,
	    const FTranslationUnit* CompleteUnit = nullptr)
	{
		FTranspileResult Result;
		if (!CompleteUnit && !RejectTemplateParameterShadows(Unit, Unit, Options, Result.Diagnostics))
		{
			return Result;
		}

		if (!CompleteUnit && !RejectShadowedStandardNames(Unit, Unit, Options, Result.Diagnostics))
		{
			return Result;
		}

		CppFrontend::FSourceLocation OptionsLocation;
		OptionsLocation.File = Unit.SourcePath;
		if (!ValidateAttributeMappings(
		        Options.AttributeMappings, Result.Diagnostics, OptionsLocation))
		{
			return Result;
		}
		FTypeMapOptions MapOptions;
		MapOptions.bQualifyNamedTypes = true;
		MapOptions.KnownTypeAliases = Unit.TypeAliases;
		for (const FRecord& Record : Unit.Records)
		{
			for (const FFunction& Method : Record.Methods)
			{
				for (const FParameter& Parameter : Method.Parameters)
				{
					RejectParameterAttributes(Parameter.Attributes, Result.Diagnostics);
				}
			}
		}
		for (const FFunction& Function : Unit.Functions)
		{
			for (const FParameter& Parameter : Function.Parameters)
			{
				RejectParameterAttributes(Parameter.Attributes, Result.Diagnostics);
			}
		}
		for (const FRecord& Record : Unit.Records)
		{
			MapOptions.KnownHostTypes.push_back(Record.QualifiedName);
		}
		for (const FEnum& Enum : Unit.Enums)
		{
			MapOptions.KnownEnumTypes.push_back(Enum.QualifiedName);
		}
		MapOptions.KnownHostTypes.insert(MapOptions.KnownHostTypes.end(),
		    Options.AdditionalKnownHostTypes.begin(), Options.AdditionalKnownHostTypes.end());
		MapOptions.KnownEnumTypes.insert(MapOptions.KnownEnumTypes.end(),
		    Options.AdditionalKnownEnumTypes.begin(), Options.AdditionalKnownEnumTypes.end());
		MapOptions.KnownTypeAliases.insert(MapOptions.KnownTypeAliases.end(),
		    Options.AdditionalKnownTypeAliases.begin(),
		    Options.AdditionalKnownTypeAliases.end());
		for (const FRecord& Record : Unit.Records)
		{
			for (const std::vector<CppFrontend::FToken>& Friend : Record.FriendDeclarations)
			{
				RejectGenericAliasUseInOpaqueTokens(
				    Friend, MapOptions.KnownTypeAliases, Result.Diagnostics);
			}
			for (const FField& Field : Record.Fields)
			{
				RejectGenericAliasUseInOpaqueTokens(
				    Field.InitializerTokens, MapOptions.KnownTypeAliases,
				    Result.Diagnostics);
			}
			for (const FFunction& Method : Record.Methods)
			{
				RejectGenericAliasUseInOpaqueTokens(
				    Method.BodyTokens, MapOptions.KnownTypeAliases,
				    Result.Diagnostics);
			}
		}
		for (const FVariable& Variable : Unit.Variables)
		{
			RejectGenericAliasUseInOpaqueTokens(
			    Variable.InitializerTokens, MapOptions.KnownTypeAliases,
			    Result.Diagnostics);
		}
		for (const FFunction& Function : Unit.Functions)
		{
			RejectGenericAliasUseInOpaqueTokens(
			    Function.BodyTokens, MapOptions.KnownTypeAliases,
			    Result.Diagnostics);
		}
		for (const auto& Definition : Definitions)
		{
			for (const auto& Parameter : Definition.second->Parameters)
			{
				RejectParameterAttributes(Parameter.Attributes, Result.Diagnostics);
			}

			RejectGenericAliasUseInOpaqueTokens(
			    Definition.second->BodyTokens, MapOptions.KnownTypeAliases, Result.Diagnostics);
		}

		FEmitter Emitter;
		Emitter.Line("// SPDX-FileCopyrightText: VanstySanic");
		Emitter.Line("// SPDX-License-Identifier: MIT");
		Emitter.Line();
		Emitter.Line("// Generated by VeroScriptCppTranspiler " + FormatVersion(VEROSCRIPT_VERSION) + ".");
		Emitter.Line("// Unsupported C++ constructs are rejected rather than approximated.");
		Emitter.Line();
		for (const FTypeAlias& Alias : Unit.TypeAliases)
		{
			if (!Alias.TemplateParameters.empty())
			{
				// VeroScript has no generic using declaration. Structured C++ types are
				// resolved through MapOptions.KnownTypeAliases and emitted as their
				// concrete target types, so no declaration is needed here.
				continue;
			}
			Emitter.OpenNamespaces(ScopeName(Alias.QualifiedName));
			Emitter.Mark(Alias.Location);
			Emitter.Line("using " + LeafName(Alias.QualifiedName) + " = " + FormatType(Alias.TargetType, MapOptions, Result.Diagnostics) + ";");
			Emitter.CloseNamespaces();
			Emitter.Line();
		}
		for (const FUsingNamespace& Using : Unit.UsingNamespaces)
		{
			Emitter.OpenNamespaces(Using.Scope);
			Emitter.Mark(Using.Location);
			Emitter.Line("using namespace " + Using.NamespaceName + ";");
			Emitter.CloseNamespaces();
			Emitter.Line();
		}
		for (const FEnum& Enum : Unit.Enums)
		{
			if (Enum.bForwardDeclaration)
			{
				AddError(Result.Diagnostics, Enum.Location,
				    "cpp.transpile.incomplete_enum_unsupported",
				    "a standalone enum forward declaration has no VeroScript source equivalent: " + Enum.QualifiedName);
				continue;
			}
			EmitEnum(Emitter, Enum, Options.AttributeMappings, Result.Diagnostics);
		}
		for (const FRecord& Record : Unit.Records)
		{
			if (Record.bForwardDeclaration)
			{
				AddError(Result.Diagnostics, Record.Location,
				    "cpp.transpile.incomplete_record_unsupported",
				    "a standalone record forward declaration has no VeroScript source equivalent: " + Record.QualifiedName);
				continue;
			}
			FTypeMapOptions RecordMapOptions = MapOptions;
			for (const CppFrontend::FTemplateParameter& Parameter : Record.TemplateParameters)
			{
				RecordMapOptions.KnownTypeParameters.push_back(Parameter.Name);
			}
			Emitter.OpenNamespaces(ScopeName(Record.QualifiedName));
			AppendSourceAnnotations(Emitter, Record.Attributes, "",
			    Options.AttributeMappings, Result.Diagnostics);
			Emitter.Mark(Record.Location);
			std::string Header = FormatTemplateParameters(
			    Record.TemplateParameters, MapOptions, Result.Diagnostics);
			Header += Record.Kind == ERecordKind::Class ? "class " : "struct ";
			Header += LeafName(Record.QualifiedName);
			if (Record.bFinal)
			{
				Header += " final";
			}
			if (!Record.BaseTypes.empty())
			{
				Header += " : ";
				for (std::size_t Index = 0; Index < Record.BaseTypes.size(); ++Index)
				{
					if (Index > 0)
					{
						Header += ", ";
					}
					const FBaseType& BaseInfo = Record.BaseTypes[Index];
					if (BaseInfo.bVirtual)
					{
						AddError(Result.Diagnostics, BaseInfo.Location,
						    "cpp.transpile.virtual_inheritance_unsupported",
						    "VeroScript has no semantics-preserving virtual-base inheritance");
					}
					Header += BaseInfo.Access == EAccess::Public ? "public "
					    : BaseInfo.Access == EAccess::Protected  ? "protected "
					                                             : "private ";
					FType Base = BaseInfo.Type;
					if (Base.BaseName.empty())
					{
						Base.Spelling = BaseInfo.TypeName;
						Base.BaseName = BaseInfo.TypeName;
						Base.DeclarationScope = ScopeName(Record.QualifiedName);
					}
					Base.Location = BaseInfo.Location;
					std::string BaseName = FormatType(Base, RecordMapOptions, Result.Diagnostics);
					if (BaseName.rfind("::", 0) != 0)
					{
						if (!BaseName.empty())
						{
							AddError(Result.Diagnostics, BaseInfo.Location,
							    "cpp.transpile.base_type_unsupported",
							    "inheritance requires a known record type; builtin containers and dependent type parameters cannot be script bases");
						}

						return Result;
					}
					// Core's base-list grammar accepts qualified names, but no leading
					// root operator. Removing it is safe unless a lexical namesake exists.
					BaseName.erase(0, 2);
					const std::string BaseHead = BaseName.substr(0, BaseName.find('<'));
					if (BaseHead.find("::") == std::string::npos)
					{
						std::string Scope = ScopeName(Record.QualifiedName);
						while (!Scope.empty())
						{
							const std::string Candidate = Scope + "::" + BaseHead;
							if (std::find(MapOptions.KnownHostTypes.begin(), MapOptions.KnownHostTypes.end(),
							        Candidate) != MapOptions.KnownHostTypes.end() ||
							    std::find(MapOptions.KnownEnumTypes.begin(), MapOptions.KnownEnumTypes.end(),
							        Candidate) != MapOptions.KnownEnumTypes.end() ||
							    std::any_of(MapOptions.KnownTypeAliases.begin(),
							        MapOptions.KnownTypeAliases.end(),
							        [&](const auto& Alias)
							{
								return Alias.TemplateParameters.empty() &&
								    Alias.QualifiedName == Candidate;
							}))
							{
								AddError(Result.Diagnostics, BaseInfo.Location, "cpp.transpile.global_base_shadowed",
								    "a global base hidden by a lexical type cannot be expressed by VeroScript's inheritance syntax; rename the conflicting type");
								return Result;
							}
							Scope = ScopeName(Scope);
						}
					}
					Header += BaseName;
				}
			}
			Emitter.Line(Header);
			Emitter.Line("{");
			EAccess CurrentAccess = Record.Kind == ERecordKind::Class
			    ? EAccess::Private
			    : EAccess::Public;
			bool bAccessWritten = false;
			const auto EnsureAccess = [&](const EAccess Desired)
			{
				if (!bAccessWritten || Desired != CurrentAccess)
				{
					Emitter.Line(AccessName(Desired));
					CurrentAccess = Desired;
					bAccessWritten = true;
				}
			};
			for (const std::vector<CppFrontend::FToken>& Friend : Record.FriendDeclarations)
			{
				Emitter.Line("    " + RewriteTokens(Friend, Result.Diagnostics) + ";");
			}
			for (const FField& Field : Record.Fields)
			{
				if (Field.bStatic && !Field.bInline && !Field.bConstexpr && !Field.bDefinedOutOfClass)
				{
					AddError(Result.Diagnostics, Field.Location,
					    "cpp.transpile.noninline_static_field_unsupported",
					    "a non-inline C++ static data member is only a declaration; source transpilation cannot turn it into VeroScript static storage");
					continue;
				}
				if (Field.bConstexpr)
				{
					AddError(Result.Diagnostics, Field.Location,
					    "cpp.transpile.constexpr_variable_unsupported",
					    "constexpr variable semantics cannot be preserved by source transpilation");
					continue;
				}
				if (Field.Type.Reference != EReferenceKind::None)
				{
					AddError(Result.Diagnostics, Field.Location,
					    "cpp.transpile.reference_field_unsupported",
					    "C++ reference-member alias and lifetime semantics cannot be preserved by source transpilation: " + Record.QualifiedName + "::" + Field.Name);
					continue;
				}
				if (Field.bConstinit)
				{
					AddError(Result.Diagnostics, Field.Location,
					    "cpp.transpile.constinit_unsupported",
					    "constinit has no semantics-preserving VeroScript source equivalent");
					continue;
				}
				if (!Field.bStatic && !Field.InitializerTokens.empty())
				{
					if (!IsVeroDefaultLiteral(Field.InitializerTokens))
					{
						AddError(Result.Diagnostics, Field.Location,
						    "cpp.transpile.field_default_expression_unsupported",
						    "VeroScript instance-field defaults are literal-only; this C++ field initializer cannot be preserved");
						continue;
					}
				}
				EnsureAccess(Field.Access);
				AppendSourceAnnotations(Emitter, Field.Attributes, "    ",
				    Options.AttributeMappings, Result.Diagnostics);
				Emitter.Mark(Field.Location);
				std::string Line = "    ";
				if (Field.bStatic)
				{
					Line += "static ";
				}
				if (Field.bMutable)
				{
					Line += "mutable ";
				}
				Line += FormatType(Field.Type, RecordMapOptions, Result.Diagnostics);
				Line += ' ';
				Line += Field.Name;
				if (!Field.InitializerTokens.empty())
				{
					Line += " = ";
					Line += RewriteTokens(Field.InitializerTokens, Result.Diagnostics);
				}
				Line += ';';
				Emitter.Line(Line);
			}
			for (const FFunction& Method : Record.Methods)
			{
				EnsureAccess(Method.Access);
				EmitFunction(Emitter, Method, RecordMapOptions,
				    Options.AttributeMappings, Result.Diagnostics, true, bAllowMethodDeclarations);
			}
			Emitter.Line("};");
			Emitter.CloseNamespaces();
			Emitter.Line();
			for (const FFunction& Method : Record.Methods)
			{
				if (!Method.bPureVirtual || (!Method.bHasBody && !Method.bDefaulted))
				{
					continue;
				}

				EmitOutOfClassDefinition(Emitter, Record, Method, RecordMapOptions, Result.Diagnostics);
			}
		}
		for (const FVariable& Variable : Unit.Variables)
		{
			if (Variable.bConstexpr)
			{
				AddError(Result.Diagnostics, Variable.Location,
				    "cpp.transpile.constexpr_variable_unsupported",
				    "constexpr variable semantics cannot be preserved by source transpilation");
				continue;
			}
			if (Variable.Type.Reference != EReferenceKind::None)
			{
				AddError(Result.Diagnostics, Variable.Location,
				    "cpp.transpile.reference_global_unsupported",
				    "C++ reference-global alias and lifetime semantics cannot be preserved by source transpilation: " + Variable.QualifiedName);
				continue;
			}
			if (Variable.bExtern)
			{
				AddError(Result.Diagnostics, Variable.Location,
				    "cpp.transpile.extern_variable_unsupported",
				    "an extern C++ variable declaration cannot become a VeroScript global definition: " + Variable.QualifiedName);
				continue;
			}
			if (Variable.bConstinit)
			{
				AddError(Result.Diagnostics, Variable.Location,
				    "cpp.transpile.constinit_unsupported",
				    "constinit has no semantics-preserving VeroScript source equivalent");
				continue;
			}
			Emitter.OpenNamespaces(ScopeName(Variable.QualifiedName));
			AppendSourceAnnotations(Emitter, Variable.Attributes, "",
			    Options.AttributeMappings, Result.Diagnostics);
			Emitter.Mark(Variable.Location);
			std::string Line;
			Line += FormatType(Variable.Type, MapOptions, Result.Diagnostics);
			Line += ' ';
			Line += LeafName(Variable.QualifiedName);
			if (!Variable.InitializerTokens.empty())
			{
				Line += " = ";
				Line += RewriteTokens(Variable.InitializerTokens, Result.Diagnostics);
			}
			Line += ';';
			Emitter.Line(Line);
			Emitter.CloseNamespaces();
			Emitter.Line();
		}
		for (const FFunction& Function : Unit.Functions)
		{
			Emitter.OpenNamespaces(ScopeName(Function.QualifiedName));
			EmitFunction(Emitter, Function, MapOptions,
			    Options.AttributeMappings, Result.Diagnostics, false);
			Emitter.CloseNamespaces();
			Emitter.Line();
		}

		for (const auto& Definition : Definitions)
		{
			EmitOutOfClassDefinition(Emitter, *Definition.first, *Definition.second, MapOptions, Result.Diagnostics);
		}
		if (OutMappings)
		{
			*OutMappings = Emitter.Mappings;
		}

		Result.Source = std::move(Emitter.Output);
		const std::string GeneratedSourcePath = Options.GeneratedSourcePath.empty()
		    ? Unit.SourcePath + ".vs"
		    : Options.GeneratedSourcePath;
		Result.SourceMapJson = SourceMapJson(GeneratedSourcePath, Emitter.Mappings);
		if (Result.Succeeded() && Options.bValidateGeneratedSource)
		{
			const FCompileResult Compiled = VeroScriptCompile::CompileSource(
			    Result.Source, GeneratedSourcePath);
			if (!Compiled.bSuccess)
			{
				const auto AppendCompileError = [&](const FScriptError& Error)
				{
					CppFrontend::FSourceLocation Location;
					Location.File = Unit.SourcePath;
					Location.Line = Error.Location.Line;
					Location.Column = Error.Location.Column;
					for (auto It = Emitter.Mappings.rbegin(); It != Emitter.Mappings.rend(); ++It)
					{
						if (It->GeneratedLine > Error.Location.Line)
						{
							continue;
						}
						Location = It->Original;
						Location.Line += Error.Location.Line - It->GeneratedLine;
						Location.Column = Error.Location.Column;
						break;
					}
					AddError(Result.Diagnostics, Location,
					    "cpp.transpile.generated_source_invalid",
					    Error.Message.empty() ? Error.MessageId : Error.Message);
				};
				if (Compiled.Diagnostics.empty())
				{
					AppendCompileError(Compiled.Error);
				}
				else
				{
					for (const FScriptError& Error : Compiled.Diagnostics)
					{
						AppendCompileError(Error);
					}
				}
			}
		}
		return Result;
	}

	FTranspileResult Transpile(const FTranslationUnit& Unit, const FTranspileOptions& Options)
	{
		return TranspileUnit(Unit, Options);
	}

	FTranspileResult TranspileSource(
	    const std::string_view Source,
	    const std::string_view SourcePath,
	    const FTranspileOptions& Options)
	{
		FParseResult Parsed = ParseSource(Source, SourcePath);
		if (!Parsed.Succeeded())
		{
			FTranspileResult Result;
			Result.Diagnostics = std::move(Parsed.Diagnostics);
			return Result;
		}
		FTranspileResult Result = Transpile(Parsed.Unit, Options);
		Result.Diagnostics.insert(Result.Diagnostics.begin(),
		    std::make_move_iterator(Parsed.Diagnostics.begin()),
		    std::make_move_iterator(Parsed.Diagnostics.end()));
		return Result;
	}

	FTranspileResult TranspileFile(
	    const std::string_view FilePath,
	    const FTranspileOptions& Options)
	{
		FParseResult Parsed = ParseFile(FilePath);
		if (!Parsed.Succeeded())
		{
			FTranspileResult Result;
			Result.Diagnostics = std::move(Parsed.Diagnostics);
			return Result;
		}
		FTranspileResult Result = Transpile(Parsed.Unit, Options);
		Result.Diagnostics.insert(Result.Diagnostics.begin(),
		    std::make_move_iterator(Parsed.Diagnostics.begin()),
		    std::make_move_iterator(Parsed.Diagnostics.end()));
		return Result;
	}

	bool FTranspileProjectResult::Succeeded() const
	{
		return std::none_of(Diagnostics.begin(), Diagnostics.end(),
		    [](const FDiagnostic& Diagnostic)
		{
			return Diagnostic.Severity == EDiagnosticSeverity::Error;
		});
	}

	FTranspileProjectResult TranspileFiles(
	    const std::vector<std::string>& FilePaths,
	    const FTranspileProjectOptions& Options)
	{
		FTranspileProjectResult Result;
		FParseOptions ProjectParseOptions;
		ProjectParseOptions.bDeferOutOfClassMemberResolution = true;
		std::vector<FTranslationUnit> Units;
		Units.reserve(FilePaths.size());
		CppTools::FFilePathSet SeenFiles;
		for (std::size_t FileIndex = 0; FileIndex < FilePaths.size(); ++FileIndex)
		{
			const std::string& FilePath = FilePaths[FileIndex];
			if (!SeenFiles.Insert(FilePath))
			{
				AddError(Result.Diagnostics, {FilePath, 1, 1, 0},
				    "cpp.transpile.duplicate_input_file",
				    "the same C++ input file was supplied more than once");
				continue;
			}
			FParseResult Parsed = ParseFile(FilePath, ProjectParseOptions);
			const bool bParsed = Parsed.Succeeded();
			Result.Diagnostics.insert(Result.Diagnostics.end(),
			    std::make_move_iterator(Parsed.Diagnostics.begin()),
			    std::make_move_iterator(Parsed.Diagnostics.end()));
			if (bParsed)
			{
				Units.push_back(std::move(Parsed.Unit));
			}
		}
		if (Units.empty())
		{
			if (FilePaths.empty())
			{
				AddError(Result.Diagnostics, {"<cpp-files>", 1, 1, 0},
				    "cpp.transpile.no_input_files", "at least one C++ input file is required");
			}
			return Result;
		}

		FParseResult Merged = MergeTranslationUnits(Units);
		Result.Diagnostics.insert(Result.Diagnostics.end(),
		    std::make_move_iterator(Merged.Diagnostics.begin()),
		    std::make_move_iterator(Merged.Diagnostics.end()));
		if (!Result.Succeeded())
		{
			return Result;
		}
		// Check the complete declaration/body graph once, before module emission.
		// This also covers bodies separated from their record's declaration file.
		if (!RejectTemplateParameterShadows(Merged.Unit, Merged.Unit, {}, Result.Diagnostics)) return Result;
		if (!RejectShadowedStandardNames(Merged.Unit, Merged.Unit, {}, Result.Diagnostics)) return Result;

		std::vector<std::string> KnownRecords;
		std::vector<std::string> KnownEnums;
		std::unordered_set<std::string> DefinedFunctions;
		std::unordered_set<std::string> DefinedVariables;
		std::unordered_map<std::string, const FFunction*> MergedFunctions;
		for (const FRecord& Record : Merged.Unit.Records)
		{
			KnownRecords.push_back(Record.QualifiedName);
		}
		for (const FEnum& Enum : Merged.Unit.Enums)
		{
			KnownEnums.push_back(Enum.QualifiedName);
		}
		FTypeMapOptions ProjectLookup;
		ProjectLookup.KnownHostTypes = KnownRecords;
		ProjectLookup.KnownEnumTypes = KnownEnums;
		for (const FFunction& Function : Merged.Unit.Functions)
		{
			const std::string Identity = FunctionIdentityWithAliases(
			    Function, Merged.Unit.TypeAliases, ProjectLookup);
			MergedFunctions.emplace(Identity, &Function);
			if (IsFunctionDefinition(Function))
			{
				DefinedFunctions.insert(Identity);
			}
		}
		for (const FVariable& Variable : Merged.Unit.Variables)
		{
			if (IsVariableDefinition(Variable))
			{
				DefinedVariables.insert(Variable.QualifiedName);
			}
		}

		// Keep ordinary method bodies in their defining module. Moving a body to
		// its owner's declaration module changes using directives and alias lookup.
		const auto IsSeparatedMethod = [](const FRecord& Record, const FFunction& Method)
		{
			return Method.bHasBody && !Method.bConstructor && !Method.bDestructor && Method.Location.File != Record.Location.File;
		};
		std::unordered_map<std::string, std::vector<FSeparatedMethod>> MethodDefinitions;
		for (const auto& Record : Merged.Unit.Records)
		{
			for (const auto& Method : Record.Methods)
			{
				if (IsSeparatedMethod(Record, Method))
				{
					MethodDefinitions[Method.Location.File].emplace_back(&Record, &Method);
				}
			}
		}
		std::unordered_map<std::string, std::vector<FMapEntry>> GeneratedMappings;

		std::map<std::string, std::size_t, CppTools::FOutputFileNameLess> StemCounts;
		for (const FTranslationUnit& Unit : Units)
		{
			++StemCounts[OutputStem(Unit.SourcePath)];
		}
		for (const FTranslationUnit& Unit : Units)
		{
			const std::string Stem = OutputStem(Unit.SourcePath);
			const std::string OutputName = Stem + (StemCounts[Stem] > 1 ? "." + HexSuffix(StablePathHash(Unit.SourcePath)) : "") + ".vs";
			FTranspileOptions FileOptions;
			FileOptions.bValidateGeneratedSource = false;
			FileOptions.GeneratedSourcePath = OutputName;
			FileOptions.AdditionalKnownHostTypes = KnownRecords;
			FileOptions.AdditionalKnownEnumTypes = KnownEnums;
			FileOptions.AdditionalKnownTypeAliases = Merged.Unit.TypeAliases;
			FileOptions.AttributeMappings = Options.AttributeMappings;
			FTranslationUnit EmissionUnit = Unit;
			EmissionUnit.Records.erase(std::remove_if(
			                               EmissionUnit.Records.begin(), EmissionUnit.Records.end(),
			                               [&](const FRecord& Record)
			{
				if (!Record.bForwardDeclaration)
				{
					return false;
				}
				return std::any_of(Merged.Unit.Records.begin(), Merged.Unit.Records.end(),
				    [&](const FRecord& Candidate)
				{
					return Candidate.QualifiedName == Record.QualifiedName && !Candidate.bForwardDeclaration;
				});
			}),
			    EmissionUnit.Records.end());
			// Merge declaration metadata and special-member/storage definitions, then
			// leave prototypes for ordinary methods supplied by sibling modules.
			for (FRecord& Record : EmissionUnit.Records)
			{
				if (Record.bForwardDeclaration)
				{
					continue;
				}
				const auto MergedRecord = std::find_if(
				    Merged.Unit.Records.begin(), Merged.Unit.Records.end(),
				    [&](const FRecord& Candidate)
				{
					return Candidate.QualifiedName == Record.QualifiedName && !Candidate.bForwardDeclaration;
				});
				if (MergedRecord != Merged.Unit.Records.end())
				{
					auto Declarations = std::move(Record.Methods);
					Record = *MergedRecord;
					for (auto& Method : Record.Methods)
					{
						if (!IsSeparatedMethod(Record, Method))
						{
							continue;
						}

						const auto Identity =
						    FunctionIdentityWithAliases(Method, Merged.Unit.TypeAliases, ProjectLookup);
						const auto Declaration = std::find_if(Declarations.begin(), Declarations.end(),
						    [&](const auto& Candidate)
						{
							return FunctionIdentityWithAliases(
							           Candidate, Merged.Unit.TypeAliases, ProjectLookup) == Identity;
						});
						if (Declaration != Declarations.end())
						{
							Method.Location = Declaration->Location;
							auto Parameters = Declaration->Parameters;
							for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
							{
								// The matched signature can differ in top-level parameter
								// cv-qualifiers, which govern the definition's local values.
								// Keep its canonical types and accumulated defaults while
								// restoring the declaration's parameter names.
								Parameters[Index].Type = Method.Parameters[Index].Type;
								Parameters[Index].DefaultValue = Method.Parameters[Index].DefaultValue;
								Parameters[Index].DefaultValueTokens = Method.Parameters[Index].DefaultValueTokens;
							}
							Method.Parameters = std::move(Parameters);
						}
						Method.bHasBody = false;
						Method.BodyTokens.clear();
						// Private emission copy: this prototype has a resolved qualified body.
						Method.bOutOfClassMember = true;
					}
				}
			}
			EmissionUnit.Enums.erase(std::remove_if(
			                             EmissionUnit.Enums.begin(), EmissionUnit.Enums.end(),
			                             [&](const FEnum& Enum)
			{
				if (!Enum.bForwardDeclaration)
				{
					return false;
				}
				return std::any_of(Merged.Unit.Enums.begin(), Merged.Unit.Enums.end(),
				    [&](const FEnum& Candidate)
				{
					return Candidate.QualifiedName == Enum.QualifiedName && !Candidate.bForwardDeclaration;
				});
			}),
			    EmissionUnit.Enums.end());
			EmissionUnit.Functions.erase(std::remove_if(
			                                 EmissionUnit.Functions.begin(), EmissionUnit.Functions.end(),
			                                 [&](const FFunction& Function)
			{
				if (!Function.bOutOfClassMember)
				{
					return false;
				}
				return std::any_of(Merged.Unit.Records.begin(),
				    Merged.Unit.Records.end(), [&](const FRecord& Record)
				{
					return Record.QualifiedName == Function.OwnerQualifiedName;
				});
			}),
			    EmissionUnit.Functions.end());
			for (FFunction& Function : EmissionUnit.Functions)
			{
				if (!Function.bOutOfClassMember)
				{
					continue;
				}
				Function.bOutOfClassMember = false;
				Function.OwnerQualifiedName.clear();
			}
			for (FFunction& Function : EmissionUnit.Functions)
			{
				if (!IsFunctionDefinition(Function))
				{
					continue;
				}
				const auto MergedIt = MergedFunctions.find(FunctionIdentityWithAliases(
				    Function, Merged.Unit.TypeAliases, ProjectLookup));
				if (MergedIt != MergedFunctions.end())
				{
					Function = *MergedIt->second;
				}
			}
			EmissionUnit.Functions.erase(std::remove_if(
			                                 EmissionUnit.Functions.begin(), EmissionUnit.Functions.end(),
			                                 [&](const FFunction& Function)
			{
				if (IsFunctionDefinition(Function))
				{
					return false;
				}
				return DefinedFunctions.find(FunctionIdentityWithAliases(
				           Function, Merged.Unit.TypeAliases, ProjectLookup)) != DefinedFunctions.end();
			}),
			    EmissionUnit.Functions.end());
			EmissionUnit.Variables.erase(std::remove_if(
			                                 EmissionUnit.Variables.begin(), EmissionUnit.Variables.end(),
			                                 [&](const FVariable& Variable)
			{
				if (Variable.bOutOfClassMember)
				{
					return std::any_of(Merged.Unit.Records.begin(),
					    Merged.Unit.Records.end(), [&](const FRecord& Record)
					{
						return Record.QualifiedName == Variable.OwnerQualifiedName;
					});
				}
				if (IsVariableDefinition(Variable))
				{
					return false;
				}
				return DefinedVariables.find(Variable.QualifiedName) != DefinedVariables.end();
			}),
			    EmissionUnit.Variables.end());
			for (FVariable& Variable : EmissionUnit.Variables)
			{
				if (!Variable.bOutOfClassMember)
				{
					continue;
				}
				Variable.bOutOfClassMember = false;
				Variable.OwnerQualifiedName.clear();
			}
			FTranspileResult Transpiled = TranspileUnit(EmissionUnit, FileOptions, true,
			    MethodDefinitions[Unit.SourcePath], &GeneratedMappings[OutputName], &Merged.Unit);
			Result.Diagnostics.insert(Result.Diagnostics.end(),
			    std::make_move_iterator(Transpiled.Diagnostics.begin()),
			    std::make_move_iterator(Transpiled.Diagnostics.end()));
			Result.Files.push_back({Unit.SourcePath, OutputName,
			    std::move(Transpiled.Source), std::move(Transpiled.SourceMapJson)});
		}
		if (!Result.Succeeded())
		{
			Result.Files.clear();
			return Result;
		}
		if (Options.bValidateGeneratedSources)
		{
			FCompileOptions ValidationOptions;
			ValidationOptions.bAllowProjectWideAccess = true;
			for (const auto& File : Result.Files)
			{
				ValidationOptions.SourceOverlays.push_back({File.OutputName, File.Source});
				ValidationOptions.ImplicitImportPaths.push_back(File.OutputName);
			}
			const auto& Entry = Result.Files.back();
			const auto Compiled = VeroScriptCompile::CompileSource(Entry.Source, Entry.OutputName, ValidationOptions);
			if (!Compiled.bSuccess)
			{
				const auto AppendError = [&](const FScriptError& Error)
				{
					CppFrontend::FSourceLocation Location{
					    Error.Location.FileLabel, Error.Location.Line, Error.Location.Column, 0};
					const auto File = std::find_if(Result.Files.begin(), Result.Files.end(),
					    [&](const auto& Candidate)
					{
						return CppTools::PathsReferToSameFile(
						    Candidate.OutputName, Error.Location.FileLabel);
					});
					if (File != Result.Files.end())
					{
						Location.File = File->InputPath;
						const auto& Mappings = GeneratedMappings.at(File->OutputName);
						for (auto It = Mappings.rbegin(); It != Mappings.rend(); ++It)
						{
							if (It->GeneratedLine > Error.Location.Line)
							{
								continue;
							}

							Location = It->Original;
							Location.Line += Error.Location.Line - It->GeneratedLine;
							Location.Column = Error.Location.Column;
							break;
						}
					}
					AddError(Result.Diagnostics, Location, "cpp.transpile.generated_source_invalid",
					    Error.Message.empty() ? Error.MessageId : Error.Message);
				};
				if (Compiled.Diagnostics.empty())
				{
					AppendError(Compiled.Error);
				}

				else
				{
					for (const auto& Error : Compiled.Diagnostics)
					{
						AppendError(Error);
					}
				}

				Result.Files.clear();
				return Result;
			}
		}

		std::ostringstream Manifest;
		Manifest << "{\"version\":1,\"kind\":\"veroscript.transpiled-project\","
		            "\"allowProjectWideAccess\":true,\"modulePaths\":[";
		for (std::size_t Index = 0; Index < Result.Files.size(); ++Index)
		{
			if (Index > 0)
			{
				Manifest << ',';
			}
			Manifest << "\"" << JsonEscape(Result.Files[Index].OutputName) << "\"";
		}
		Manifest << "],\"implicitImportPaths\":[";
		for (std::size_t Index = 0; Index < Result.Files.size(); ++Index)
		{
			if (Index > 0)
			{
				Manifest << ',';
			}
			Manifest << "\"" << JsonEscape(Result.Files[Index].OutputName) << "\"";
		}
		Manifest << "],\"files\":[";
		for (std::size_t Index = 0; Index < Result.Files.size(); ++Index)
		{
			if (Index > 0)
			{
				Manifest << ',';
			}
			const FTranspiledFile& File = Result.Files[Index];
			Manifest << "{\"input\":\"" << JsonEscape(File.InputPath)
			         << "\",\"source\":\"" << JsonEscape(File.OutputName)
			         << "\",\"sourceMap\":\"" << JsonEscape(File.OutputName + ".map.json")
			         << "\"}";
		}
		Manifest << "]}";
		Result.ManifestJson = Manifest.str();
		return Result;
	}
}
