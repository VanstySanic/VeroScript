// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptParser.h"
#include "VeroScriptAnalysisControl.h"

#include "VeroScriptAst.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptLexer.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptHostBinding.h"
#include "Parser/VeroScriptParserBindings.h"
#include "TypeSupport/VeroScriptTypeMemberBinding.h"
#include "VeroScriptFloatParsing.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace VeroScript
{
	namespace ParserDetail
	{
		bool IsTypeKeyword(std::string_view Lexeme)
		{
			return Lexeme == "int64" || Lexeme == "uint64" || VeroScript::IsTypeKeyword(Lexeme);
		}

		bool IsUnsignedBaseTypeKeyword(std::string_view Lexeme)
		{
			return VeroScript::IsUnsignedBaseTypeKeyword(Lexeme);
		}

		bool IsContainerTypeKeyword(std::string_view Lexeme)
		{
			return VeroScript::IsContainerTypeKeyword(Lexeme);
		}

		template <typename TDeclaration>
		std::string GenericSignaturePattern(const TDeclaration& Declaration, const std::string& Type)
		{
			std::vector<std::string> Symbols;
			for (std::size_t Index = 0; Index < TemplateParameterNames(Declaration).size(); ++Index)
			{
				Symbols.push_back("__template_parameter_" + std::to_string(Index));
			}
			return SubstituteScopedTypeParameters(Type, TemplateParameterNames(Declaration), Symbols);
		}

		template <typename TDeclaration>
		bool SameGenericParameterSchema(const TDeclaration& Left, const TDeclaration& Right)
		{
			if (TemplateParameterNames(Left).size() != TemplateParameterNames(Right).size()) return false;
			for (std::size_t Index = 0; Index < TemplateParameterNames(Left).size(); ++Index)
			{
				const auto* A = Left.TemplateParameters.IsEmpty() ? nullptr : &Left.TemplateParameters.GetParameters()[Index];
				const auto* B = Right.TemplateParameters.IsEmpty() ? nullptr : &Right.TemplateParameters.GetParameters()[Index];
				const auto AK = A ? A->Kind : ETemplateParameterKind::Type;
				const auto BK = B ? B->Kind : ETemplateParameterKind::Type;
				if (AK != BK || (AK == ETemplateParameterKind::Value && GenericSignaturePattern(Left, A->ValueTypeName) != GenericSignaturePattern(Right, B->ValueTypeName)))
				{
					return false;
				}
			}
			return true;
		}

		bool HasMethodWithSignature(
		    const std::vector<FMethodDecl>& Methods,
		    const FMethodDecl& Candidate)
		{
			for (const FMethodDecl& Existing : Methods)
			{
				if (Existing.Name != Candidate.Name || Existing.Parameters.size() != Candidate.Parameters.size() || !SameGenericParameterSchema(Existing, Candidate) || Existing.bIsConst != Candidate.bIsConst)
				{
					continue;
				}
				bool bSame = true;
				for (std::size_t Index = 0; Index < Candidate.Parameters.size(); ++Index)
				{
					if (GenericSignaturePattern(Existing, Existing.Parameters[Index].TypeName) != GenericSignaturePattern(Candidate, Candidate.Parameters[Index].TypeName) || Existing.Parameters[Index].bIsRef != Candidate.Parameters[Index].bIsRef || Existing.Parameters[Index].bIsConst != Candidate.Parameters[Index].bIsConst || Existing.Parameters[Index].bIsVariadic != Candidate.Parameters[Index].bIsVariadic)
					{
						bSame = false;
						break;
					}
				}
				if (bSame)
				{
					return true;
				}
			}
			return false;
		}

		bool HasFunctionWithSignature(
		    const std::vector<FFunctionDecl>& Functions,
		    const FFunctionDecl& Candidate)
		{
			for (const FFunctionDecl& Existing : Functions)
			{
				if (Existing.ModulePath != Candidate.ModulePath || Existing.Name != Candidate.Name)
				{
					continue;
				}
				if (!SameGenericParameterSchema(Existing, Candidate))
				{
					continue;
				}
				if (Existing.Parameters.size() != Candidate.Parameters.size())
				{
					continue;
				}
				bool bSame = true;
				for (std::size_t Index = 0; Index < Candidate.Parameters.size(); ++Index)
				{
					if (GenericSignaturePattern(Existing, Existing.Parameters[Index].TypeName) != GenericSignaturePattern(Candidate, Candidate.Parameters[Index].TypeName) || Existing.Parameters[Index].bIsRef != Candidate.Parameters[Index].bIsRef || Existing.Parameters[Index].bIsConst != Candidate.Parameters[Index].bIsConst || Existing.Parameters[Index].bIsVariadic != Candidate.Parameters[Index].bIsVariadic)
					{
						bSame = false;
						break;
					}
				}
				if (bSame)
				{
					return true;
				}
			}
			return false;
		}

		bool HasConstructorWithSignature(
		    const std::vector<FConstructorDecl>& Constructors,
		    const FConstructorDecl& Candidate)
		{
			for (const FConstructorDecl& Existing : Constructors)
			{
				if (Existing.Parameters.size() != Candidate.Parameters.size())
				{
					continue;
				}
				bool bSame = true;
				for (std::size_t Index = 0; Index < Candidate.Parameters.size(); ++Index)
				{
					if (Existing.Parameters[Index].TypeName != Candidate.Parameters[Index].TypeName || Existing.Parameters[Index].bIsRef != Candidate.Parameters[Index].bIsRef || Existing.Parameters[Index].bIsConst != Candidate.Parameters[Index].bIsConst || Existing.Parameters[Index].bIsVariadic != Candidate.Parameters[Index].bIsVariadic)
					{
						bSame = false;
						break;
					}
				}
				if (bSame)
				{
					return true;
				}
			}
			return false;
		}

		void PreserveDeclaredHostSignature(
		    FDeclMeta& DeclMeta,
		    const std::string_view ReturnType,
		    const std::vector<FFunctionParam>& Parameters,
		    const std::unordered_set<std::string>& HostTypeNames)
		{
			const auto UsesHostType = [&HostTypeNames](const std::string_view TypeName)
			{
				if (HostTypeNames.find(std::string(TypeName)) != HostTypeNames.end())
				{
					return true;
				}
				if (TypeName.find('<') == std::string_view::npos)
				{
					return false;
				}
				for (const std::string& HostTypeName : HostTypeNames)
				{
					if (HostTypeName.empty())
					{
						continue;
					}
					const std::string Needle = HostTypeName + "<";
					std::size_t Position = TypeName.find(Needle);
					while (Position != std::string_view::npos)
					{
						if (Position == 0 || TypeName[Position - 1] == '<' || TypeName[Position - 1] == ',' || TypeName[Position - 1] == ' ')
						{
							return true;
						}
						Position = TypeName.find(Needle, Position + 1);
					}
				}
				return false;
			};

			if (UsesHostType(ReturnType))
			{
				DeclMeta.emplace_back(
				    std::string(DeclaredReturnTypeMetaKey),
				    std::string(ReturnType));
			}
			for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
			{
				if (UsesHostType(Parameters[Index].TypeName))
				{
					DeclMeta.emplace_back(
					    std::string(DeclaredParamTypeMetaPrefix) + std::to_string(Index),
					    Parameters[Index].TypeName);
				}
			}
		}

		/**
		 * Once any param has a default, all following params must also,
		 * except a trailing variadic (`T...`) which never takes a default.
		 * At most one variadic, and only as the last parameter.
		 */
		bool HasValidVariadicPlacement(const std::vector<FFunctionParam>& Parameters)
		{
			for (std::size_t Index = 0; Index < Parameters.size(); ++Index)
			{
				if (Parameters[Index].bIsVariadic && Index + 1 != Parameters.size())
				{
					return false;
				}
			}
			return true;
		}

		bool HasValidTrailingDefaults(const std::vector<FFunctionParam>& Parameters)
		{
			if (!HasValidVariadicPlacement(Parameters))
			{
				return false;
			}
			bool bSeenDefault = false;
			for (const FFunctionParam& Param : Parameters)
			{
				if (Param.DefaultValue)
				{
					bSeenDefault = true;
					if (Param.bIsVariadic)
					{
						return false;
					}
				}
				else if (bSeenDefault && !Param.bIsVariadic)
				{
					return false;
				}
			}
			return true;
		}

		bool IsOperatorConversionName(const std::string& Name)
		{
			return Name.rfind("operator conv ", 0) == 0;
		}

		// Leaf name for free-operator validation; keeps "operator conv A.B" intact.
		std::string OperatorNameForValidation(const std::string& Name)
		{
			const std::size_t DotOp = Name.rfind(".operator");
			if (DotOp != std::string::npos)
			{
				return Name.substr(DotOp + 1);
			}
			if (Name.rfind("operator", 0) == 0)
			{
				return Name;
			}
			const std::size_t Dot = Name.rfind('.');
			return Dot == std::string::npos ? Name : Name.substr(Dot + 1);
		}

		std::string OperatorConversionTarget(const std::string& Name)
		{
			return IsOperatorConversionName(Name) ? Name.substr(14) : "";
		}

		bool IsUnaryOperatorName(const std::string& Name, const bool bMember)
		{
			(void)bMember;
			if (IsOperatorConversionName(Name))
			{
				return true;
			}
			return Name == "operator!" || Name == "operator~" || Name == "operator++" || Name == "operator--" || Name == "operator-";
		}

		bool IsOperatorDeclarationName(const std::string_view Name)
		{
			return Name.rfind("operator", 0) == 0 && Name.size() > 8 && std::string_view(" +-*/%&|^!~<>=([,").find(Name[8]) != std::string_view::npos;
		}

		bool RequiresBoolOperatorReturn(const std::string& Name)
		{
			return Name == "operator==" || Name == "operator!=" || Name == "operator<" || Name == "operator<=" || Name == "operator>" || Name == "operator>=" || Name == "operator&&" || Name == "operator||" || Name == "operator!";
		}

		bool ValidateOperatorParamRestrictions(
		    const std::string& Name,
		    const std::vector<FFunctionParam>& Parameters,
		    std::string& OutError)
		{
			for (const FFunctionParam& Param : Parameters)
			{
				if (Param.bIsVariadic && Name != "operator()")
				{
					OutError = "operator overload cannot use variadic parameters";
					return false;
				}
				if (Param.DefaultValue && Name != "operator()")
				{
					OutError = "operator overload cannot use default parameters";
					return false;
				}
			}
			return true;
		}

		bool ValidateMemberOperatorSignature(
		    const std::string& Name,
		    const std::string& ReturnType,
		    const bool bIsStatic,
		    const std::vector<FFunctionParam>& Parameters,
		    const std::string& LastParameterType,
		    std::string& OutError)
		{
			if (!IsOperatorDeclarationName(Name)) return true;
			if (bIsStatic && Name != "operator()" && Name != "operator[]")
			{
				OutError = "only operator() and operator[] may be static";
				return false;
			}
			const std::size_t ParamCount = Parameters.size();
			if (Name == "operator()")
			{
				// any arity, including zero
			}
			else if (Name == "operator[]")
			{
				if (ParamCount != 1)
				{
					OutError = "operator[] overload must take exactly one parameter";
					return false;
				}
			}
			else if (Name == "operator-")
			{
				if (ParamCount > 1)
				{
					OutError = "operator- overload must take at most one parameter";
					return false;
				}
			}
			else if (Name == "operator++" || Name == "operator--")
			{
				if (ParamCount != 0 && ParamCount != 1)
				{
					OutError = "increment/decrement overload has invalid parameter count";
					return false;
				}
				if (ParamCount == 1 && (LastParameterType != "int" || Parameters.back().bIsRef))
				{
					OutError = "postfix increment/decrement tag must be a by-value int";
					return false;
				}
			}
			else if (IsUnaryOperatorName(Name, true))
			{
				if (ParamCount != 0)
				{
					OutError = "unary operator overload must take no parameters";
					return false;
				}
			}
			else if (ParamCount != 1)
			{
				OutError = "binary operator overload must take exactly one parameter";
				return false;
			}

			if (RequiresBoolOperatorReturn(Name) && ReturnType != "bool")
			{
				OutError = Name + " must return bool";
				return false;
			}
			if (IsOperatorConversionName(Name))
			{
				const std::string Target = OperatorConversionTarget(Name);
				if (ReturnType != Target)
				{
					OutError = Name + " return type must match conversion target";
					return false;
				}
			}
			return ValidateOperatorParamRestrictions(Name, Parameters, OutError);
		}

		bool ValidateFreeOperatorSignature(
		    const std::string& Name,
		    const std::string& ReturnType,
		    const std::vector<FFunctionParam>& Parameters,
		    const std::string& LastParameterType,
		    std::string& OutError)
		{
			if (!IsOperatorDeclarationName(Name)) return true;
			if (Name == "operator=")
			{
				OutError = "operator= must be a member operator";
				return false;
			}
			const std::size_t ParamCount = Parameters.size();
			if (Name == "operator()")
			{
				if (ParamCount < 1)
				{
					OutError = "operator() free-function overload must take at least one parameter";
					return false;
				}
			}
			else if (Name == "operator[]")
			{
				if (ParamCount != 2)
				{
					OutError = "operator[] free-function overload must take exactly two parameters";
					return false;
				}
			}
			else if (Name == "operator-")
			{
				if (ParamCount != 1 && ParamCount != 2)
				{
					OutError = "free-function operator- must take one (unary) or two (binary) parameters";
					return false;
				}
			}
			else if (Name == "operator++" || Name == "operator--")
			{
				if (ParamCount != 1 && ParamCount != 2)
				{
					OutError = "increment/decrement overload has invalid parameter count";
					return false;
				}
				if (ParamCount == 2 && (LastParameterType != "int" || Parameters.back().bIsRef))
				{
					OutError = "postfix increment/decrement tag must be a by-value int";
					return false;
				}
			}
			else if (IsUnaryOperatorName(Name, false))
			{
				if (ParamCount != 1)
				{
					OutError = "unary free-function operator must take exactly one parameter";
					return false;
				}
			}
			else if (ParamCount != 2)
			{
				OutError = "binary free-function operator must take exactly two parameters";
				return false;
			}

			if (RequiresBoolOperatorReturn(Name) && ReturnType != "bool")
			{
				OutError = Name + " must return bool";
				return false;
			}
			if (IsOperatorConversionName(Name))
			{
				const std::string Target = OperatorConversionTarget(Name);
				if (ReturnType != Target)
				{
					OutError = Name + " return type must match conversion target";
					return false;
				}
			}
			return ValidateOperatorParamRestrictions(Name, Parameters, OutError);
		}

		// Parameters can occur in type leaves, array extents, and value arguments.
		bool TypeMentionsParam(const std::string& TypeName, const std::string& Param)
		{
			if (Param.empty() || TypeName.empty()) return false;
			const auto IdentifierPart = [](const char C)
			{
				return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_';
			};
			for (std::size_t Pos = 0; Pos < TypeName.size(); ++Pos)
			{
				if ((Pos & 255u) == 0)
				{
					CheckAnalysisCancellation();
				}
				const auto C = TypeName[Pos];
				if (C == '\'' || C == '"')
				{
					// Character constants in value arguments do not name parameters.
					while (++Pos < TypeName.size())
					{
						if ((Pos & 255u) == 0)
						{
							CheckAnalysisCancellation();
						}
						if (TypeName[Pos] == '\\' && Pos + 1 < TypeName.size())
						{
							++Pos;
						}
						else if (TypeName[Pos] == C)
						{
							break;
						}
					}
					continue;
				}
				const bool Left = Pos == 0 || (!IdentifierPart(TypeName[Pos - 1]) && TypeName[Pos - 1] != '.' && TypeName[Pos - 1] != ':');
				if (!Left || TypeName.compare(Pos, Param.size(), Param) != 0)
				{
					continue;
				}
				const auto After = Pos + Param.size();
				if (After == TypeName.size() || !IdentifierPart(TypeName[After]))
				{
					return true;
				}
			}
			return false;
		}

		bool IsNameToken(const FToken& Token)
		{
			return Token.Kind == ETokenKind::Identifier || (Token.Kind == ETokenKind::Keyword && IsContainerTypeKeyword(Token.Lexeme));
		}

		std::size_t FindQualifiedDefinitionSeparator(const std::vector<FToken>& Tokens, std::size_t Begin)
		{
			for (std::size_t I = Begin; I < Tokens.size(); ++I)
			{
				const auto Text = Tokens[I].Lexeme;
				if (Text == ";" || Text == "{" || Text == "=" || Tokens[I].Kind == ETokenKind::EndOfFile)
				{
					break;
				}

				if (Text == "(")
				{
					// fn(...) is part of a return type, including nested callable
					// types. Other parentheses start the declaration's parameters;
					// do not search their defaults for a qualified function call.
					if (I == Begin || Tokens[I - 1].Lexeme != "fn")
					{
						break;
					}

					std::size_t Depth = 1;
					while (Depth && ++I < Tokens.size())
					{
						if (Tokens[I].Lexeme == "(")
						{
							++Depth;
						}

						else if (Tokens[I].Lexeme == ")")
						{
							--Depth;
						}
					}
					if (Depth)
					{
						break;
					}

					continue;
				}
				if (Text != "::" || I + 1 >= Tokens.size())
				{
					continue;
				}

				const auto Member = I + 1;
				// Select the owner separator before operator. A conversion target
				// may itself contain :: and operator() has name parentheses.
				if (Tokens[Member].Kind == ETokenKind::Keyword && Tokens[Member].Lexeme == "operator")
				{
					return I;
				}

				auto ParameterStart = Member + 1;
				if (ParameterStart < Tokens.size() && Tokens[ParameterStart].Lexeme == "<")
				{
					std::size_t Depth = 1;
					while (Depth && ++ParameterStart < Tokens.size())
					{
						const auto Part = Tokens[ParameterStart].Lexeme;
						if (Part == "<")
						{
							++Depth;
						}
						else if (Part == ">")
						{
							--Depth;
						}
						else if (Part == ">>")
						{
							Depth = Depth > 1 ? Depth - 2 : 0;
						}
						else if (Part == ";" || Part == "{" || Part == "}")
						{
							break;
						}
					}
					if (Depth)
					{
						return Tokens.size();
					}
					++ParameterStart;
				}
				if (IsNameToken(Tokens[Member]) && ParameterStart < Tokens.size() &&
				    Tokens[ParameterStart].Lexeme == "(")
				{
					return I;
				}

				if (Tokens[Member].Lexeme == "~" && Member + 2 < Tokens.size() &&
				    IsNameToken(Tokens[Member + 1]) && Tokens[Member + 2].Lexeme == "(")
				{
					return I;
				}
			}
			return Tokens.size();
		}

		bool IsDeclVisible(
		    const std::string_view DeclModulePath,
		    const bool bExported,
		    const std::string_view CurrentModulePath,
		    const std::unordered_set<std::string>* VisibleModulePaths)
		{
			if (!bExported)
			{
				if (DeclModulePath == CurrentModulePath)
				{
					return true;
				}
				return VisibleModulePaths != nullptr && VisibleModulePaths->find(ModuleVisibility::ProjectVisibilityKey(DeclModulePath)) != VisibleModulePaths->end() && VisibleModulePaths->find(ModuleVisibility::ProjectVisibilityKey(CurrentModulePath)) != VisibleModulePaths->end();
			}
			if (DeclModulePath == CurrentModulePath || DeclModulePath.empty() || CurrentModulePath.empty())
			{
				return true;
			}
			if (!VisibleModulePaths || VisibleModulePaths->empty())
			{
				return true;
			}
			return VisibleModulePaths->find(std::string(DeclModulePath)) != VisibleModulePaths->end();
		}

		bool StripIntegerSuffix(std::string_view& Lexeme, bool& bOutUnsigned, bool& bOutHadLongSuffix)
		{
			bOutUnsigned = false;
			bOutHadLongSuffix = false;
			if (Lexeme.empty())
			{
				return true;
			}

			const char Last = Lexeme.back();
			if (Last == 'u' || Last == 'U')
			{
				bOutUnsigned = true;
				Lexeme.remove_suffix(1);
				if (!Lexeme.empty() && (Lexeme.back() == 'l' || Lexeme.back() == 'L'))
				{
					bOutHadLongSuffix = true;
					Lexeme.remove_suffix(1);
				}
				return true;
			}
			if (Last == 'l' || Last == 'L')
			{
				bOutHadLongSuffix = true;
				Lexeme.remove_suffix(1);
				if (!Lexeme.empty() && (Lexeme.back() == 'u' || Lexeme.back() == 'U'))
				{
					bOutUnsigned = true;
					Lexeme.remove_suffix(1);
				}
				return true;
			}
			return true;
		}

		bool ParseIntegerLiteral(
		    std::string_view Lexeme,
		    std::int32_t& OutSignedValue,
		    std::uint32_t& OutUnsignedValue,
		    bool& bOutUnsigned,
		    std::string& OutError,
		    bool* bOutIs64 = nullptr,
		    std::int64_t* OutSignedValue64 = nullptr,
		    std::uint64_t* OutUnsignedValue64 = nullptr,
		    const bool bAllowSignedMinMagnitude = false,
		    bool* bOutParsedSignedMin = nullptr)
		{
			std::string NormalizedLexeme;
			if (Lexeme.find('_') != std::string_view::npos)
			{
				NormalizedLexeme.reserve(Lexeme.size());
				for (const char Character : Lexeme)
				{
					if (Character != '_')
					{
						NormalizedLexeme.push_back(Character);
					}
				}
				Lexeme = NormalizedLexeme;
			}
			bOutUnsigned = false;
			OutSignedValue = 0;
			OutUnsignedValue = 0;
			if (bOutIs64)
			{
				*bOutIs64 = false;
			}
			if (OutSignedValue64)
			{
				*OutSignedValue64 = 0;
			}
			if (OutUnsignedValue64)
			{
				*OutUnsignedValue64 = 0;
			}
			if (bOutParsedSignedMin)
			{
				*bOutParsedSignedMin = false;
			}

			bool bHadLongSuffix = false;
			bool bIs64 = false;
			if (Lexeme.size() >= 3)
			{
				const std::string_view Suffix = Lexeme.substr(Lexeme.size() - 3);
				if (Suffix == "i64" || Suffix == "I64")
				{
					bIs64 = true;
					Lexeme.remove_suffix(3);
				}
				else if (Suffix == "u64" || Suffix == "U64")
				{
					bIs64 = true;
					bOutUnsigned = true;
					Lexeme.remove_suffix(3);
				}
			}
			if (!bIs64 && !StripIntegerSuffix(Lexeme, bOutUnsigned, bHadLongSuffix))
			{
				OutError = "invalid integer literal";
				return false;
			}
			// `long` is a fixed 64-bit language primitive. Expression parsing supplies
			// the 64-bit outputs; size/count contexts intentionally keep their existing
			// bounded integer result and perform their own range validation.
			if (bHadLongSuffix && bOutIs64)
			{
				bIs64 = true;
			}
			if (bIs64 && !bOutIs64)
			{
				OutError = "64-bit integer literal is not valid in this context";
				return false;
			}
			if (bOutIs64)
			{
				*bOutIs64 = bIs64;
			}
			if (Lexeme.empty())
			{
				OutError = "invalid integer literal";
				return false;
			}

			const bool bHex = Lexeme.size() >= 2 && Lexeme[0] == '0' && (Lexeme[1] == 'x' || Lexeme[1] == 'X');
			const bool bBinary = Lexeme.size() >= 2 && Lexeme[0] == '0' && (Lexeme[1] == 'b' || Lexeme[1] == 'B');
			if (bHex || bBinary)
			{
				const std::string_view Digits = Lexeme.substr(2);
				if (Digits.empty())
				{
					OutError = "invalid integer literal";
					return false;
				}
				std::uint64_t Parsed = 0;
				const auto [Pointer, ErrorCode] = std::from_chars(
				    Digits.data(),
				    Digits.data() + Digits.size(),
				    Parsed,
				    bHex ? 16 : 2);
				if (ErrorCode != std::errc() || Pointer != Digits.data() + Digits.size())
				{
					OutError = "invalid integer literal";
					return false;
				}
				if (bOutUnsigned)
				{
					if (bIs64)
					{
						*OutUnsignedValue64 = Parsed;
						return true;
					}
					if (Parsed > std::numeric_limits<std::uint32_t>::max())
					{
						OutError = "unsigned integer literal out of range";
						return false;
					}
					OutUnsignedValue = static_cast<std::uint32_t>(Parsed);
					return true;
				}
				if (bIs64)
				{
					const std::uint64_t SignedMax = static_cast<std::uint64_t>(
					    std::numeric_limits<std::int64_t>::max());
					if (bAllowSignedMinMagnitude && Parsed == SignedMax + 1u)
					{
						*OutSignedValue64 = std::numeric_limits<std::int64_t>::min();
						if (bOutParsedSignedMin)
						{
							*bOutParsedSignedMin = true;
						}
						return true;
					}
					if (Parsed > SignedMax)
					{
						OutError = "signed int64 literal out of range";
						return false;
					}
					*OutSignedValue64 = static_cast<std::int64_t>(Parsed);
					return true;
				}
				const std::uint64_t SignedMax = static_cast<std::uint64_t>(
				    std::numeric_limits<std::int32_t>::max());
				if (bAllowSignedMinMagnitude && Parsed == SignedMax + 1u)
				{
					OutSignedValue = std::numeric_limits<std::int32_t>::min();
					if (bOutParsedSignedMin)
					{
						*bOutParsedSignedMin = true;
					}
					return true;
				}
				if (Parsed > SignedMax)
				{
					OutError = "integer literal out of range";
					return false;
				}
				OutSignedValue = static_cast<std::int32_t>(Parsed);
				return true;
			}

			std::uint64_t Parsed = 0;
			const auto [Pointer, ErrorCode] = std::from_chars(
			    Lexeme.data(),
			    Lexeme.data() + Lexeme.size(),
			    Parsed);
			if (ErrorCode != std::errc() || Pointer != Lexeme.data() + Lexeme.size())
			{
				OutError = "invalid integer literal";
				return false;
			}
			if (bOutUnsigned)
			{
				if (bIs64)
				{
					*OutUnsignedValue64 = Parsed;
					return true;
				}
				if (Parsed > std::numeric_limits<std::uint32_t>::max())
				{
					OutError = "unsigned integer literal out of range";
					return false;
				}
				OutUnsignedValue = static_cast<std::uint32_t>(Parsed);
				return true;
			}
			if (bIs64)
			{
				const std::uint64_t SignedMax = static_cast<std::uint64_t>(
				    std::numeric_limits<std::int64_t>::max());
				if (bAllowSignedMinMagnitude && Parsed == SignedMax + 1u)
				{
					*OutSignedValue64 = std::numeric_limits<std::int64_t>::min();
					if (bOutParsedSignedMin)
					{
						*bOutParsedSignedMin = true;
					}
					return true;
				}
				if (Parsed > SignedMax)
				{
					OutError = "signed int64 literal out of range";
					return false;
				}
				*OutSignedValue64 = static_cast<std::int64_t>(Parsed);
				return true;
			}
			const std::uint64_t SignedMax = static_cast<std::uint64_t>(
			    std::numeric_limits<std::int32_t>::max());
			if (bAllowSignedMinMagnitude && Parsed == SignedMax + 1u)
			{
				OutSignedValue = std::numeric_limits<std::int32_t>::min();
				if (bOutParsedSignedMin)
				{
					*bOutParsedSignedMin = true;
				}
				return true;
			}
			if (Parsed > SignedMax)
			{
				OutError = "integer literal out of range";
				return false;
			}
			OutSignedValue = static_cast<std::int32_t>(Parsed);
			return true;
		}

		bool ParseFloatLiteral(std::string_view Lexeme, double& OutValue, std::string& OutError)
		{
			std::string NormalizedLexeme;
			if (Lexeme.find('_') != std::string_view::npos)
			{
				NormalizedLexeme.reserve(Lexeme.size());
				for (const char Character : Lexeme)
				{
					if (Character != '_')
					{
						NormalizedLexeme.push_back(Character);
					}
				}
				Lexeme = NormalizedLexeme;
			}
			const bool bFloat32 = !Lexeme.empty() && (Lexeme.back() == 'f' || Lexeme.back() == 'F');
			if (bFloat32)
			{
				Lexeme.remove_suffix(1);
			}
			if (Lexeme.empty())
			{
				OutError = "invalid floating-point literal";
				return false;
			}

			std::from_chars_result Parsed;
			if (bFloat32)
			{
				float Value = 0.f;
				Parsed = FloatParsingDetail::FromCharsDecimal(Lexeme, Value);
				if (Parsed.ec == std::errc{})
				{
					OutValue = static_cast<double>(Value);
				}
				else if (Parsed.ec == std::errc::result_out_of_range)
				{
					// Preserve the existing literal range: a decimal representable as
					// double may round to float zero, while double underflow is rejected.
					double Wider = 0.;
					const auto WiderParsed = FloatParsingDetail::FromCharsDecimal(Lexeme, Wider);
					if (WiderParsed.ec == std::errc{} && WiderParsed.ptr == Lexeme.data() + Lexeme.size() && std::isfinite(Wider) && std::abs(Wider) < std::numeric_limits<float>::min())
					{
						OutValue = std::copysign(0., Wider);
						Parsed = WiderParsed;
					}
				}
			}
			else
			{
				Parsed = FloatParsingDetail::FromCharsDecimal(Lexeme, OutValue);
			}
			const auto [End, ErrorCode] = Parsed;
			if (ErrorCode == std::errc::result_out_of_range || !std::isfinite(OutValue))
			{
				OutError = "floating-point literal out of range";
				return false;
			}
			if (ErrorCode != std::errc() || End != Lexeme.data() + Lexeme.size())
			{
				OutError = "invalid floating-point literal";
				return false;
			}
			return true;
		}

		namespace
		{
			int HexDigitValue(const char Character)
			{
				if (Character >= '0' && Character <= '9')
				{
					return Character - '0';
				}
				if (Character >= 'a' && Character <= 'f')
				{
					return Character - 'a' + 10;
				}
				if (Character >= 'A' && Character <= 'F')
				{
					return Character - 'A' + 10;
				}
				return -1;
			}

			bool ParseFixedHexEscape(
			    const std::string_view Lexeme,
			    std::size_t& Index,
			    const std::size_t DigitCount,
			    std::uint32_t& OutValue,
			    std::string& OutError,
			    const char* Error)
			{
				const std::size_t ContentEnd = Lexeme.size() - 1;
				if (Index + DigitCount >= ContentEnd)
				{
					OutError = Error;
					return false;
				}
				std::uint32_t Value = 0;
				for (std::size_t Offset = 1; Offset <= DigitCount; ++Offset)
				{
					const int Digit = HexDigitValue(Lexeme[Index + Offset]);
					if (Digit < 0)
					{
						OutError = Error;
						return false;
					}
					Value = (Value << 4u) | static_cast<std::uint32_t>(Digit);
				}
				Index += DigitCount;
				OutValue = Value;
				return true;
			}

			bool IsUnicodeScalar(const std::uint32_t Value)
			{
				return Value <= 0x10FFFFu && (Value < 0xD800u || Value > 0xDFFFu);
			}

			void AppendUtf8CodePoint(std::string& Out, const std::uint32_t Value)
			{
				if (Value <= 0x7Fu)
				{
					Out.push_back(static_cast<char>(Value));
				}
				else if (Value <= 0x7FFu)
				{
					Out.push_back(static_cast<char>(0xC0u | (Value >> 6u)));
					Out.push_back(static_cast<char>(0x80u | (Value & 0x3Fu)));
				}
				else if (Value <= 0xFFFFu)
				{
					Out.push_back(static_cast<char>(0xE0u | (Value >> 12u)));
					Out.push_back(static_cast<char>(0x80u | ((Value >> 6u) & 0x3Fu)));
					Out.push_back(static_cast<char>(0x80u | (Value & 0x3Fu)));
				}
				else
				{
					Out.push_back(static_cast<char>(0xF0u | (Value >> 18u)));
					Out.push_back(static_cast<char>(0x80u | ((Value >> 12u) & 0x3Fu)));
					Out.push_back(static_cast<char>(0x80u | ((Value >> 6u) & 0x3Fu)));
					Out.push_back(static_cast<char>(0x80u | (Value & 0x3Fu)));
				}
			}

			bool DecodeSimpleEscape(const char Escaped, char& Out)
			{
				switch (Escaped)
				{
				case '0':
					{
						Out = '\0';
						return true;
					}
				case 'a':
					{
						Out = '\a';
						return true;
					}
				case 'b':
					{
						Out = '\b';
						return true;
					}
				case 'f':
					{
						Out = '\f';
						return true;
					}
				case 'n':
					{
						Out = '\n';
						return true;
					}
				case 'r':
					{
						Out = '\r';
						return true;
					}
				case 't':
					{
						Out = '\t';
						return true;
					}
				case 'v':
					{
						Out = '\v';
						return true;
					}
				case '\\':
					{
						Out = '\\';
						return true;
					}
				case '\'':
					{
						Out = '\'';
						return true;
					}
				case '"':
					{
						Out = '"';
						return true;
					}
				default:
					return false;
				}
			}

			bool DecodeUnicodeEscape(
			    const std::string_view Lexeme,
			    std::size_t& Index,
			    const char Escaped,
			    std::string& OutUtf8,
			    std::string& OutError,
			    const char* Error)
			{
				std::uint32_t Value = 0;
				if (!ParseFixedHexEscape(
				        Lexeme,
				        Index,
				        Escaped == 'u' ? 4u : 8u,
				        Value,
				        OutError,
				        Error))
				{
					return false;
				}
				if (!IsUnicodeScalar(Value))
				{
					OutError = Error;
					return false;
				}
				AppendUtf8CodePoint(OutUtf8, Value);
				return true;
			}
		}

		bool DecodeStringLiteral(
		    const std::string_view Lexeme,
		    std::string& OutValue,
		    std::string& OutError,
		    std::uint32_t* OutErrorOffset)
		{
			if (OutErrorOffset)
			{
				*OutErrorOffset = 0;
			}
			const bool bRaw = Lexeme.size() >= 3 && Lexeme[0] == 'r' && Lexeme[1] == '"';
			if ((!bRaw && (Lexeme.size() < 2 || Lexeme.front() != '"')) || Lexeme.back() != '"')
			{
				OutError = "invalid string literal";
				return false;
			}
			if (bRaw)
			{
				OutValue.assign(Lexeme.substr(2, Lexeme.size() - 3));
				// Raw string contents are source UTF-8, rather than an arbitrary
				// byte escape channel. Keep the original UTF-8 bytes after validation.
				std::u16string Validation;
				if (!Runtime::Utf8ToUtf16(OutValue, Validation, &OutError))
				{
					if (OutErrorOffset)
					{
						*OutErrorOffset = 2;
					}
					OutError = "invalid UTF-8 in raw string literal";
					return false;
				}
				return true;
			}

			OutValue.clear();
			for (std::size_t Index = 1; Index + 1 < Lexeme.size(); ++Index)
			{
				const char C = Lexeme[Index];
				if (C != '\\')
				{
					OutValue.push_back(C);
					continue;
				}
				if (OutErrorOffset)
				{
					*OutErrorOffset = static_cast<std::uint32_t>(Index);
				}

				if (Index + 1 >= Lexeme.size() - 1)
				{
					OutError = "invalid string escape";
					return false;
				}

				const char Escaped = Lexeme[++Index];
				char Simple = 0;
				if (DecodeSimpleEscape(Escaped, Simple))
				{
					OutValue.push_back(Simple);
				}
				else if (Escaped == 'x')
				{
					std::uint32_t Value = 0;
					if (!ParseFixedHexEscape(
					        Lexeme, Index, 2, Value, OutError, "invalid string hexadecimal escape"))
					{
						return false;
					}
					OutValue.push_back(static_cast<char>(Value));
				}
				else if (Escaped == 'u' || Escaped == 'U')
				{
					if (!DecodeUnicodeEscape(
					        Lexeme, Index, Escaped, OutValue, OutError, "invalid string Unicode escape"))
					{
						return false;
					}
				}
				else
				{
					OutError = "invalid string escape";
					return false;
				}
			}
			return true;
		}

		bool DecodeWStringLiteral(
		    const std::string_view Lexeme,
		    std::u16string& OutValue,
		    std::string& OutError,
		    std::uint32_t* OutErrorOffset)
		{
			if (OutErrorOffset)
			{
				*OutErrorOffset = 0;
			}
			if (Lexeme.size() < 3 || (Lexeme[0] != 'L' && Lexeme[0] != 'l') || Lexeme[1] != '"' || Lexeme.back() != '"')
			{
				OutError = "invalid wide string literal";
				return false;
			}

			std::string Utf8Content;
			for (std::size_t Index = 2; Index + 1 < Lexeme.size(); ++Index)
			{
				const char C = Lexeme[Index];
				if (C != '\\')
				{
					Utf8Content.push_back(C);
					continue;
				}
				if (OutErrorOffset)
				{
					*OutErrorOffset = static_cast<std::uint32_t>(Index);
				}

				if (Index + 1 >= Lexeme.size() - 1)
				{
					OutError = "invalid wide string escape";
					return false;
				}

				const char Escaped = Lexeme[++Index];
				char Simple = 0;
				if (DecodeSimpleEscape(Escaped, Simple))
				{
					Utf8Content.push_back(Simple);
				}
				else if (Escaped == 'x')
				{
					std::uint32_t Value = 0;
					if (!ParseFixedHexEscape(
					        Lexeme, Index, 2, Value, OutError, "invalid wide string hexadecimal escape"))
					{
						return false;
					}
					AppendUtf8CodePoint(Utf8Content, Value);
				}
				else if (Escaped == 'u' || Escaped == 'U')
				{
					if (!DecodeUnicodeEscape(
					        Lexeme, Index, Escaped, Utf8Content, OutError, "invalid wide string Unicode escape"))
					{
						return false;
					}
				}
				else
				{
					OutError = "invalid wide string escape";
					return false;
				}
			}
			if (OutErrorOffset)
			{
				*OutErrorOffset = 0;
			}
			if (!Runtime::Utf8ToUtf16(Utf8Content, OutValue, &OutError))
			{
				return false;
			}
			return true;
		}

		bool DecodeCharacterLiteral(
		    const std::string_view Lexeme,
		    std::int8_t& OutValue,
		    std::string& OutError,
		    std::uint32_t* OutErrorOffset)
		{
			if (OutErrorOffset)
			{
				*OutErrorOffset = 0;
			}
			if (Lexeme.size() < 2 || Lexeme.front() != '\'' || Lexeme.back() != '\'')
			{
				OutError = "invalid character literal";
				return false;
			}
			const std::string_view Content = Lexeme.substr(1, Lexeme.size() - 2);
			std::uint32_t Value = 0;
			if (Content.size() == 1 && Content.front() != '\\')
			{
				Value = static_cast<unsigned char>(Content.front());
				if (Value > 0x7Fu)
				{
					OutError = "character literal must contain one ASCII byte or an escape";
					if (OutErrorOffset)
					{
						*OutErrorOffset = 1;
					}
					return false;
				}
			}
			else if (!Content.empty() && Content.front() == '\\')
			{
				if (OutErrorOffset)
				{
					*OutErrorOffset = 1;
				}
				if (Content.size() < 2)
				{
					OutError = "invalid character escape";
					return false;
				}
				const char Escaped = Content[1];
				char Simple = 0;
				if (DecodeSimpleEscape(Escaped, Simple))
				{
					if (Content.size() != 2)
					{
						OutError = "character literal must contain exactly one value";
						return false;
					}
					Value = static_cast<unsigned char>(Simple);
				}
				else if (Escaped == 'x')
				{
					if (Content.size() != 4)
					{
						OutError = "character hexadecimal escape requires exactly two digits";
						return false;
					}
					const int High = HexDigitValue(Content[2]);
					const int Low = HexDigitValue(Content[3]);
					if (High < 0 || Low < 0)
					{
						OutError = "invalid character hexadecimal escape";
						return false;
					}
					Value = static_cast<std::uint32_t>((High << 4) | Low);
				}
				else if (Escaped == 'u' || Escaped == 'U')
				{
					OutError = "Unicode escapes are not supported in char literals";
					return false;
				}
				else
				{
					OutError = "invalid character escape";
					return false;
				}
			}
			else
			{
				const bool bContainsNonAscii = std::any_of(
				    Content.begin(), Content.end(),
				    [](const char Character)
				{
					return static_cast<unsigned char>(Character) > 0x7Fu;
				});
				OutError = bContainsNonAscii
				    ? "character literal must contain one ASCII byte or an escape"
				    : "character literal must contain exactly one value";
				if (OutErrorOffset)
				{
					*OutErrorOffset = 1;
				}
				return false;
			}

			OutValue = Value <= 0x7Fu
			    ? static_cast<std::int8_t>(Value)
			    : static_cast<std::int8_t>(static_cast<std::int32_t>(Value) - 0x100);
			return true;
		}

		std::unique_ptr<FExpr> CloneExpr(const FExpr& Expression)
		{
			switch (Expression.Kind)
			{
			case EExprKind::IntegerLiteral:
				{
					const FIntegerLiteralExpr& Literal = static_cast<const FIntegerLiteralExpr&>(Expression);
					std::unique_ptr<FIntegerLiteralExpr> Cloned;
					if (Literal.bIs64 && Literal.bUnsigned)
					{
						Cloned = std::make_unique<FIntegerLiteralExpr>(
						    Literal.UnsignedValue64, true);
					}
					else if (Literal.bIs64)
					{
						Cloned = std::make_unique<FIntegerLiteralExpr>(Literal.Value64);
					}
					else if (Literal.bUnsigned)
					{
						Cloned = std::make_unique<FIntegerLiteralExpr>(Literal.UnsignedValue, true);
					}
					else
					{
						Cloned = std::make_unique<FIntegerLiteralExpr>(Literal.Value);
					}
					Cloned->Location = Expression.Location;
					Cloned->SourceRange = Expression.SourceRange;
					Cloned->DeclMeta = Expression.DeclMeta;
					Cloned->DeferredSyntax = Expression.DeferredSyntax;
					return Cloned;
				}
			case EExprKind::CharacterLiteral:
				{
					auto Cloned = std::make_unique<FCharacterLiteralExpr>(
					    static_cast<const FCharacterLiteralExpr&>(Expression).Value);
					Cloned->Location = Expression.Location;
					Cloned->SourceRange = Expression.SourceRange;
					Cloned->DeclMeta = Expression.DeclMeta;
					Cloned->DeferredSyntax = Expression.DeferredSyntax;
					return Cloned;
				}
			case EExprKind::FloatLiteral:
				{
					const auto& Literal = static_cast<const FFloatLiteralExpr&>(Expression);
					auto Cloned = std::make_unique<FFloatLiteralExpr>(
					    Literal.Value, Literal.bFloat32);
					Cloned->Location = Expression.Location;
					Cloned->SourceRange = Expression.SourceRange;
					Cloned->DeclMeta = Expression.DeclMeta;
					Cloned->DeferredSyntax = Expression.DeferredSyntax;
					return Cloned;
				}
			case EExprKind::StringLiteral:
				{
					auto Cloned = std::make_unique<FStringLiteralExpr>(
					    static_cast<const FStringLiteralExpr&>(Expression).Value);
					Cloned->Location = Expression.Location;
					Cloned->SourceRange = Expression.SourceRange;
					Cloned->DeclMeta = Expression.DeclMeta;
					Cloned->DeferredSyntax = Expression.DeferredSyntax;
					return Cloned;
				}
			case EExprKind::WStringLiteral:
				{
					auto Cloned = std::make_unique<FWStringLiteralExpr>(
					    static_cast<const FWStringLiteralExpr&>(Expression).Value);
					Cloned->Location = Expression.Location;
					Cloned->SourceRange = Expression.SourceRange;
					Cloned->DeclMeta = Expression.DeclMeta;
					Cloned->DeferredSyntax = Expression.DeferredSyntax;
					return Cloned;
				}
			case EExprKind::BooleanLiteral:
				{
					auto Cloned = std::make_unique<FBooleanLiteralExpr>(
					    static_cast<const FBooleanLiteralExpr&>(Expression).Value);
					Cloned->Location = Expression.Location;
					Cloned->SourceRange = Expression.SourceRange;
					Cloned->DeclMeta = Expression.DeclMeta;
					Cloned->DeferredSyntax = Expression.DeferredSyntax;
					return Cloned;
				}
			case EExprKind::NullptrLiteral:
				{
					auto Cloned = std::make_unique<FNullptrLiteralExpr>();
					Cloned->Location = Expression.Location;
					Cloned->SourceRange = Expression.SourceRange;
					Cloned->DeclMeta = Expression.DeclMeta;
					Cloned->DeferredSyntax = Expression.DeferredSyntax;
					return Cloned;
				}
			case EExprKind::This:
				{
					auto Cloned = std::make_unique<FThisExpr>();
					Cloned->Location = Expression.Location;
					Cloned->SourceRange = Expression.SourceRange;
					Cloned->DeclMeta = Expression.DeclMeta;
					Cloned->DeferredSyntax = Expression.DeferredSyntax;
					return Cloned;
				}
			case EExprKind::Identifier:
				{
					const FIdentifierExpr& Identifier =
					    static_cast<const FIdentifierExpr&>(Expression);
					auto Cloned = std::make_unique<FIdentifierExpr>(
					    Identifier.Name);
					Cloned->bExplicitTemplateArguments = Identifier.bExplicitTemplateArguments;
					Cloned->TypeArgs = Identifier.TypeArgs;
					Cloned->Location = Expression.Location;
					Cloned->SourceRange = Expression.SourceRange;
					Cloned->DeclMeta = Expression.DeclMeta;
					Cloned->DeferredSyntax = Expression.DeferredSyntax;
					Cloned->bStandardLibraryQualified =
					    Identifier.bStandardLibraryQualified;
					return Cloned;
				}
			case EExprKind::Unary:
				{
					const FUnaryExpr& Unary = static_cast<const FUnaryExpr&>(Expression);
					auto Cloned = std::make_unique<FUnaryExpr>(Unary.Op, CloneExpr(*Unary.Operand));
					Cloned->Location = Expression.Location;
					Cloned->SourceRange = Expression.SourceRange;
					Cloned->DeclMeta = Expression.DeclMeta;
					Cloned->DeferredSyntax = Expression.DeferredSyntax;
					return Cloned;
				}
			case EExprKind::Binary:
				{
					const FBinaryExpr& Binary = static_cast<const FBinaryExpr&>(Expression);
					auto Cloned = std::make_unique<FBinaryExpr>(
					    Binary.Op,
					    CloneExpr(*Binary.Left),
					    CloneExpr(*Binary.Right));
					Cloned->Location = Expression.Location;
					Cloned->SourceRange = Expression.SourceRange;
					Cloned->DeclMeta = Expression.DeclMeta;
					Cloned->DeferredSyntax = Expression.DeferredSyntax;
					return Cloned;
				}
			case EExprKind::Call:
				{
					const FCallExpr& Call = static_cast<const FCallExpr&>(Expression);
					std::vector<std::unique_ptr<FExpr>> Args;
					Args.reserve(Call.Arguments.size());
					for (const std::unique_ptr<FExpr>& Arg : Call.Arguments)
					{
						Args.push_back(Arg ? CloneExpr(*Arg) : nullptr);
					}
					auto Cloned = std::make_unique<FCallExpr>(
					    Call.Callee,
					    std::move(Args),
					    Call.TypeArgs);
					Cloned->bStandardLibraryQualified = Call.bStandardLibraryQualified;
					Cloned->bExplicitTemplateArguments = Call.bExplicitTemplateArguments;
					Cloned->bImplementationBuiltinAccess = Call.bImplementationBuiltinAccess;
					if (Call.CalleeExpr)
					{
						Cloned->CalleeExpr = CloneExpr(*Call.CalleeExpr);
					}
					Cloned->Location = Expression.Location;
					Cloned->SourceRange = Expression.SourceRange;
					Cloned->DeclMeta = Expression.DeclMeta;
					Cloned->DeferredSyntax = Expression.DeferredSyntax;
					return Cloned;
				}
			case EExprKind::MethodCall:
				{
					const auto& Call = static_cast<const FMethodCallExpr&>(Expression);
					std::vector<std::unique_ptr<FExpr>> Arguments;
					for (const auto* Argument : Call.GetArguments())
					{
						Arguments.push_back(Argument ? CloneExpr(*Argument) : nullptr);
					}
					auto Cloned = std::make_unique<FMethodCallExpr>(Call.Object ? CloneExpr(*Call.Object) : nullptr,
					    Call.Method, std::move(Arguments));
					Cloned->TypeArgs = Call.TypeArgs;
					Cloned->bExplicitTemplateArguments = Call.bExplicitTemplateArguments;
					Cloned->Location = Expression.Location;
					Cloned->SourceRange = Expression.SourceRange;
					Cloned->DeclMeta = Expression.DeclMeta;
					Cloned->DeferredSyntax = Expression.DeferredSyntax;
					return Cloned;
				}
			case EExprKind::MemberAccess:
				{
					const FMemberAccessExpr& MemberAccess = static_cast<const FMemberAccessExpr&>(Expression);
					auto Cloned = std::make_unique<FMemberAccessExpr>(
					    CloneExpr(*MemberAccess.Object),
					    MemberAccess.Member);
					Cloned->bExplicitTemplateArguments = MemberAccess.bExplicitTemplateArguments;
					Cloned->TypeArgs = MemberAccess.TypeArgs;
					Cloned->Location = Expression.Location;
					Cloned->SourceRange = Expression.SourceRange;
					Cloned->DeclMeta = Expression.DeclMeta;
					Cloned->DeferredSyntax = Expression.DeferredSyntax;
					return Cloned;
				}
			case EExprKind::IndexAccess:
				{
					const FIndexAccessExpr& IndexAccess = static_cast<const FIndexAccessExpr&>(Expression);
					auto Cloned = std::make_unique<FIndexAccessExpr>(
					    CloneExpr(*IndexAccess.Object),
					    CloneExpr(*IndexAccess.Index));
					Cloned->Location = Expression.Location;
					Cloned->SourceRange = Expression.SourceRange;
					Cloned->DeclMeta = Expression.DeclMeta;
					Cloned->DeferredSyntax = Expression.DeferredSyntax;
					return Cloned;
				}
			case EExprKind::Lambda:
				{
					const FLambdaExpr& Lambda = static_cast<const FLambdaExpr&>(Expression);
					auto Cloned = std::make_unique<FLambdaExpr>();
					Cloned->Location = Expression.Location;
					Cloned->SourceRange = Expression.SourceRange;
					Cloned->DeclMeta = Expression.DeclMeta;
					Cloned->DeferredSyntax = Expression.DeferredSyntax;
					Cloned->CaptureKind = Lambda.CaptureKind;
					Cloned->bMutable = Lambda.bMutable;
					for (const auto& SourceCapture : Lambda.Captures)
					{
						FLambdaCapture Capture;
						Capture.Name = SourceCapture.Name;
						Capture.bByReference = SourceCapture.bByReference;
						Capture.Location = SourceCapture.Location;
						Capture.SourceRange = SourceCapture.SourceRange;
						Capture.NameRange = SourceCapture.NameRange;
						if (SourceCapture.Initializer)
						{
							Capture.Initializer = CloneExpr(*SourceCapture.Initializer);
						}
						Cloned->Captures.push_back(std::move(Capture));
					}
					Cloned->Parameters = Lambda.Parameters;
					Cloned->ReturnType = Lambda.ReturnType;
					// Semantic capture analysis annotates the AST for backends that emit
					// directly from it. Preserve that annotation when generic/module
					// instantiation clones a lambda after analysis; otherwise an AOT clone
					// can lose the retained class receiver and capture a dangling C++ this.
					Cloned->bCapturesThis.store(
					    Lambda.bCapturesThis.load(std::memory_order_acquire),
					    std::memory_order_release);
					if (Lambda.ExpressionBody)
					{
						Cloned->ExpressionBody = CloneExpr(*Lambda.ExpressionBody);
					}
					// Block bodies are not deep-cloned for generic instantiation MVP.
					if (Lambda.Body)
					{
						Cloned->Body = std::make_unique<FBlockStmt>();
						Cloned->Body->Location = Lambda.Body->Location;
						Cloned->Body->Statements.reserve(Lambda.Body->Statements.size());
						for (const std::unique_ptr<FStmt>& Statement : Lambda.Body->Statements)
						{
							if (!Statement)
							{
								continue;
							}
							if (Statement->Kind == EStmtKind::Return)
							{
								const FReturnStmt& Return =
								    static_cast<const FReturnStmt&>(*Statement);
								auto ClonedReturn = std::make_unique<FReturnStmt>(
								    Return.Value ? CloneExpr(*Return.Value) : nullptr);
								ClonedReturn->Location = Return.Location;
								Cloned->Body->Statements.push_back(std::move(ClonedReturn));
							}
							else if (Statement->Kind == EStmtKind::Expr)
							{
								const FExprStmt& ExprStmt =
								    static_cast<const FExprStmt&>(*Statement);
								auto ClonedExpr = std::make_unique<FExprStmt>(
								    ExprStmt.Expression ? CloneExpr(*ExprStmt.Expression) : nullptr);
								ClonedExpr->Location = ExprStmt.Location;
								Cloned->Body->Statements.push_back(std::move(ClonedExpr));
							}
						}
					}
					return Cloned;
				}
			default:
				return nullptr;
			}
		}

		bool IsValidAssignmentTarget(const FExpr& Expression)
		{
			return Expression.Kind == EExprKind::Identifier || Expression.Kind == EExprKind::MemberAccess || Expression.Kind == EExprKind::IndexAccess
			    // The compiler accepts these only when overload resolution proves a
			    // mutable reference return; ordinary calls still diagnose as non-lvalues.
			    || Expression.Kind == EExprKind::Call || Expression.Kind == EExprKind::MethodCall || Expression.Kind == EExprKind::Binary || Expression.Kind == EExprKind::Unary || Expression.Kind == EExprKind::Assign;
		}

		EBinaryOp CompoundAssignOp(const std::string_view Symbol)
		{
			if (Symbol == "+=")
			{
				return EBinaryOp::Add;
			}
			if (Symbol == "-=")
			{
				return EBinaryOp::Subtract;
			}
			if (Symbol == "*=")
			{
				return EBinaryOp::Multiply;
			}
			if (Symbol == "/=")
			{
				return EBinaryOp::Divide;
			}
			if (Symbol == "%=")
			{
				return EBinaryOp::Modulo;
			}
			if (Symbol == "&=")
			{
				return EBinaryOp::BitAnd;
			}
			if (Symbol == "|=")
			{
				return EBinaryOp::BitOr;
			}
			if (Symbol == "^=")
			{
				return EBinaryOp::BitXor;
			}
			if (Symbol == "<<=")
			{
				return EBinaryOp::ShiftLeft;
			}
			if (Symbol == ">>=")
			{
				return EBinaryOp::ShiftRight;
			}
			return EBinaryOp::Add;
		}
	}

	VeroScriptParser::VeroScriptParser(
	    const std::vector<FToken>& InTokens,
	    const std::string_view InFileLabel,
	    const FProgram* const InExternalTypeProgram,
	    std::unordered_set<std::string> InVisibleModulePaths)
	    : Tokens(InTokens), FileLabel(InFileLabel), ExternalTypeProgram(InExternalTypeProgram), VisibleModulePaths(std::move(InVisibleModulePaths))
	{
		SetHostTypeNames({});
	}

	void VeroScriptParser::SetAdditionalForwardTypeNames(
	    std::unordered_set<std::string> TypeNames,
	    std::unordered_set<std::string> InterfaceNames)
	{
		AdditionalForwardTypeNames = std::move(TypeNames);
		AdditionalForwardInterfaceNames = std::move(InterfaceNames);
	}

	std::unique_ptr<FProgram> VeroScriptParser::ParseProgram()
	{
		FAnalysisStage Stage("parse");
		CheckAnalysisCancellation();
		// Reject a pathological left-nested generic chain iteratively, before any
		// recursive declaration/type parsing starts.  The recursive depth guard in
		// ParseTypeName remains authoritative for general type grammar, while this
		// preflight ensures malformed input cannot consume the native stack first in
		// unoptimized builds (for example array<array<...<int>...>>).
		constexpr std::size_t MaxPreflightTypeDepth = 64;
		std::size_t SegmentEnd = 0;
		std::size_t RemainingClosings = 0;
		for (std::size_t Start = 0; Start + 1 < Tokens.size(); ++Start)
		{
			if ((Start & 1023u) == 0)
			{
				CheckAnalysisCancellation();
			}
			if (Start >= SegmentEnd)
			{
				// Count closers once per semicolon-delimited segment. Repeatedly
				// scanning the remaining input for every `a < b` used quadratic
				// time on long comparison chains, even though parsing rejects them.
				RemainingClosings = 0;
				SegmentEnd = Start;
				while (SegmentEnd < Tokens.size())
				{
					if ((SegmentEnd & 1023u) == 0)
					{
						CheckAnalysisCancellation();
					}
					const FToken& Token = Tokens[SegmentEnd++];
					if (Token.Kind == ETokenKind::Symbol)
					{
						if (Token.Lexeme == ";")
						{
							break;
						}
						if (Token.Lexeme == ">" || Token.Lexeme == ">>")
						{
							RemainingClosings += Token.Lexeme.size();
						}
					}
				}
			}
			if (Tokens[Start].Kind == ETokenKind::Symbol && (Tokens[Start].Lexeme == ">" || Tokens[Start].Lexeme == ">>"))
			{
				RemainingClosings -= Tokens[Start].Lexeme.size();
			}
			std::size_t Cursor = Start;
			std::size_t Depth = 0;
			while (Cursor + 1 < Tokens.size())
			{
				const FToken& Head = Tokens[Cursor];
				const bool bGenericHead = Head.Kind == ETokenKind::Identifier || (Head.Kind == ETokenKind::Keyword && ParserDetail::IsContainerTypeKeyword(Head.Lexeme));
				if (!bGenericHead || Tokens[Cursor + 1].Kind != ETokenKind::Symbol || Tokens[Cursor + 1].Lexeme != "<")
				{
					break;
				}
				++Depth;
				if (Depth > MaxPreflightTypeDepth)
				{
					// Avoid treating a long comparison chain (`a < b < c`) as a type.
					// A genuine nested generic chain has enough closing angle brackets.
					if (RemainingClosings >= Depth)
					{
						ReportErrorAt(Head, "type name nested too deeply");
						CommitCurrentError();
						return nullptr;
					}
					break;
				}
				Cursor += 2;
			}
		}
		Program = std::make_unique<FProgram>();
		ParsedTypeLookup = {};
		bFragmentForwardLookup = false;
		for (const FToken& Token : Tokens)
		{
			if (Token.Lexeme == "module_info" || Token.Lexeme == "function_info" || Token.Lexeme == "global_info")
			{
				Program->UsedCompilerBuiltinTypes.emplace(Token.Lexeme);
			}
		}
		PendingGenericCloseCount = 0;
		TypeNameParseDepth = 0;
		ExpressionTypeArgumentParseDepth = 0;
		bTypeNameDepthExceeded = false;
		ForwardTypeNames.clear();
		ForwardInterfaceNames.clear();
		ForwardTypeNames.insert(
		    AdditionalForwardTypeNames.begin(),
		    AdditionalForwardTypeNames.end());
		ForwardInterfaceNames.insert(
		    AdditionalForwardInterfaceNames.begin(),
		    AdditionalForwardInterfaceNames.end());
		std::vector<std::string> NamespaceStack;
		std::vector<int> NamespaceBraceDepths;
		std::string PendingNamespaceName;
		int BraceDepth = 0;
		for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
		{
			if (Tokens[Index].Kind == ETokenKind::Symbol && Tokens[Index].Lexeme == "{")
			{
				++BraceDepth;
				if (!PendingNamespaceName.empty())
				{
					NamespaceStack.push_back(std::move(PendingNamespaceName));
					NamespaceBraceDepths.push_back(BraceDepth);
					PendingNamespaceName.clear();
				}
				continue;
			}
			if (Tokens[Index].Kind == ETokenKind::Symbol && Tokens[Index].Lexeme == "}")
			{
				while (!NamespaceBraceDepths.empty() && NamespaceBraceDepths.back() == BraceDepth)
				{
					NamespaceBraceDepths.pop_back();
					NamespaceStack.pop_back();
				}
				if (BraceDepth > 0)
				{
					--BraceDepth;
				}
				PendingNamespaceName.clear();
				continue;
			}
			if (Tokens[Index].Kind == ETokenKind::Keyword && Tokens[Index].Lexeme == "namespace" && Index + 1 < Tokens.size() && Tokens[Index + 1].Kind == ETokenKind::Identifier)
			{
				// Match ParseNamespaceDecl: `namespace a::b::c` is one nested path (a.b.c).
				// Advance Index past the name so later tokens do not clear Pending before '{'.
				PendingNamespaceName.assign(Tokens[Index + 1].Lexeme);
				std::size_t NameIndex = Index + 2;
				while (NameIndex + 1 < Tokens.size() && Tokens[NameIndex].Kind == ETokenKind::Symbol && Tokens[NameIndex].Lexeme == "::" && Tokens[NameIndex + 1].Kind == ETokenKind::Identifier)
				{
					PendingNamespaceName += '.';
					PendingNamespaceName += Tokens[NameIndex + 1].Lexeme;
					NameIndex += 2;
				}
				Index = NameIndex - 1;
				continue;
			}
			PendingNamespaceName.clear();

			if (BraceDepth != (NamespaceBraceDepths.empty() ? 0 : NamespaceBraceDepths.back()))
			{
				continue;
			}
			if (Tokens[Index].Lexeme == "class" && Index > 0 && (Tokens[Index - 1].Lexeme == "<" || Tokens[Index - 1].Lexeme == ","))
			{
				// A template parameter introducer does not declare a forward type.
				continue;
			}
			std::size_t TypeTokenIndex = Index;
			if (Tokens[TypeTokenIndex].Kind == ETokenKind::Keyword && Tokens[TypeTokenIndex].Lexeme == "export")
			{
				++TypeTokenIndex;
			}
			if (TypeTokenIndex + 1 >= Tokens.size())
			{
				continue;
			}
			if (Tokens[TypeTokenIndex].Kind != ETokenKind::Keyword)
			{
				continue;
			}
			if (Tokens[TypeTokenIndex].Lexeme != "struct" && Tokens[TypeTokenIndex].Lexeme != "class" && Tokens[TypeTokenIndex].Lexeme != "interface" && Tokens[TypeTokenIndex].Lexeme != "enum" && Tokens[TypeTokenIndex].Lexeme != "using" && Tokens[TypeTokenIndex].Lexeme != "typedef")
			{
				continue;
			}
			if (Tokens[TypeTokenIndex + 1].Kind != ETokenKind::Identifier)
			{
				continue;
			}
			std::string TypeName(Tokens[TypeTokenIndex + 1].Lexeme);
			if (!NamespaceStack.empty())
			{
				std::string Qualified;
				for (const std::string& Ns : NamespaceStack)
				{
					if (!Qualified.empty())
					{
						Qualified += '.';
					}
					Qualified += Ns;
				}
				Qualified += '.';
				Qualified += TypeName;
				TypeName = std::move(Qualified);
			}
			ForwardTypeNames.insert(TypeName);
			if (Tokens[TypeTokenIndex].Lexeme == "interface")
			{
				ForwardInterfaceNames.insert(TypeName);
			}
		}
		if (ExternalTypeProgram)
		{
			for (const FStructDecl& StructDecl : ExternalTypeProgram->Structs)
			{
				if (StructDecl.bExported && ParserDetail::IsDeclVisible(StructDecl.ModulePath, StructDecl.bExported, FileLabel, &VisibleModulePaths))
				{
					ForwardTypeNames.insert(StructDecl.Name);
					if (!StructDecl.Scope.DisplayName.empty())
					{
						ForwardTypeNames.insert(StructDecl.Scope.DisplayName);
					}
				}
			}
			for (const FClassDecl& ClassDecl : ExternalTypeProgram->Classes)
			{
				if (ClassDecl.bExported && ParserDetail::IsDeclVisible(ClassDecl.ModulePath, ClassDecl.bExported, FileLabel, &VisibleModulePaths))
				{
					ForwardTypeNames.insert(ClassDecl.Name);
					if (!ClassDecl.Scope.DisplayName.empty())
					{
						ForwardTypeNames.insert(ClassDecl.Scope.DisplayName);
					}
				}
			}
			for (const FInterfaceDecl& InterfaceDecl : ExternalTypeProgram->Interfaces)
			{
				if (InterfaceDecl.bExported && ParserDetail::IsDeclVisible(InterfaceDecl.ModulePath, InterfaceDecl.bExported, FileLabel, &VisibleModulePaths))
				{
					ForwardTypeNames.insert(InterfaceDecl.Name);
					if (!InterfaceDecl.Scope.DisplayName.empty())
					{
						ForwardTypeNames.insert(InterfaceDecl.Scope.DisplayName);
					}
					ForwardInterfaceNames.insert(InterfaceDecl.Name);
				}
			}
			for (const FEnumDecl& EnumDecl : ExternalTypeProgram->Enums)
			{
				if (EnumDecl.bExported && ParserDetail::IsDeclVisible(EnumDecl.ModulePath, EnumDecl.bExported, FileLabel, &VisibleModulePaths))
				{
					ForwardTypeNames.insert(EnumDecl.Name);
					if (!EnumDecl.Scope.DisplayName.empty())
					{
						ForwardTypeNames.insert(EnumDecl.Scope.DisplayName);
					}
				}
			}
			for (const FTypeAliasDecl& AliasDecl : ExternalTypeProgram->TypeAliases)
			{
				if (AliasDecl.bExported && ParserDetail::IsDeclVisible(AliasDecl.ModulePath, AliasDecl.bExported, FileLabel, &VisibleModulePaths))
				{
					ForwardTypeNames.insert(AliasDecl.Name);
					if (!AliasDecl.Scope.DisplayName.empty())
					{
						ForwardTypeNames.insert(AliasDecl.Scope.DisplayName);
					}
				}
			}
		}

		while (!IsAtEnd())
		{
			const std::size_t DeclPrefixStart = Current;
			const int DeclPrefixPending = PendingGenericCloseCount;
			FDeclMeta PendingDeclMeta;
			bool bExported = false;
			std::vector<std::string> PrefixTypeParams;
			std::vector<std::string> PrefixTypeParamDefaults;
			FTemplateParameterList PrefixTemplateParameters;
			std::vector<FTemplateParameterList> TemplateHeaders;
			bool bHasTypeDefaultAccess = false;
			EMemberAccess TypeDefaultAccess = EMemberAccess::Private;
			bool bStaticClass = false;
			FSourceLocation TypeHeaderModifierLocation = {};
			TryParseAnnotations(PendingDeclMeta);
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "export")
			{
				Advance();
				bExported = true;
				TryParseAnnotations(PendingDeclMeta);
			}
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "template")
			{
				if (!ParseTemplatePrefix(PrefixTypeParams, PrefixTypeParamDefaults, &PrefixTemplateParameters))
				{
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
					continue;
				}
				TryParseAnnotations(PendingDeclMeta);
				// Also accept the natural `template<T> export class ...` spelling.
				if (!bExported && Check(ETokenKind::Keyword) && Peek().Lexeme == "export")
				{
					Advance();
					bExported = true;
					TryParseAnnotations(PendingDeclMeta);
				}
			}
			const auto RollbackDeclPrefix = [&]()
			{
				Current = DeclPrefixStart;
				PendingGenericCloseCount = DeclPrefixPending;
				PendingDeclMeta.clear();
				PrefixTypeParams.clear();
				PrefixTypeParamDefaults.clear();
				bExported = false;
				bHasTypeDefaultAccess = false;
				TypeDefaultAccess = EMemberAccess::Private;
				bStaticClass = false;
				TypeHeaderModifierLocation = {};
			};

			if (!ParseRemainingTemplateHeaders(PrefixTemplateParameters, TemplateHeaders))
			{
				if (!RecoverAfterDeclarationError())
				{
					return nullptr;
				}
				continue;
			}

			// Type-header modifiers intentionally live between `export`/`template`
			// prefixes and the type keyword: `export public static class Math {}`.
			// They are not general declaration modifiers, so a stray use is diagnosed
			// before it can be mistaken for a member access section.
			while (Check(ETokenKind::Keyword) && (Peek().Lexeme == "public" || Peek().Lexeme == "protected" || Peek().Lexeme == "private" || Peek().Lexeme == "static"))
			{
				const FToken Modifier = Advance();
				if (TypeHeaderModifierLocation.FileLabel.empty())
				{
					TypeHeaderModifierLocation = Modifier.Location;
				}
				if (Modifier.Lexeme == "static")
				{
					if (bStaticClass)
					{
						ReportErrorAt(Modifier, "duplicate 'static' in type declaration header");
						return nullptr;
					}
					bStaticClass = true;
					continue;
				}
				if (bHasTypeDefaultAccess)
				{
					ReportErrorAt(Modifier, "duplicate access modifier in type declaration header");
					return nullptr;
				}
				bHasTypeDefaultAccess = true;
				TypeDefaultAccess = Modifier.Lexeme == "public"
				    ? EMemberAccess::Public
				    : Modifier.Lexeme == "protected"
				    ? EMemberAccess::Protected
				    : EMemberAccess::Private;
			}
			if ((bHasTypeDefaultAccess || bStaticClass) && !(Check(ETokenKind::Keyword) && (Peek().Lexeme == "class" || Peek().Lexeme == "struct" || Peek().Lexeme == "interface")))
			{
				ReportErrorAt(
				    TypeHeaderModifierLocation,
				    "type declaration modifiers must be followed by 'class', 'struct', or 'interface'");
				if (!RecoverAfterDeclarationError())
				{
					return nullptr;
				}
				continue;
			}

			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "import")
			{
				if ((!PrefixTypeParams.empty() || !PrefixTemplateParameters.IsEmpty()))
				{
					ReportErrorAt(Peek(), "template prefix is not supported on import declarations");
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
					continue;
				}
				if (!PendingDeclMeta.empty())
				{
					ReportErrorAt(Peek(), "annotations are not supported on import");
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
					continue;
				}
				if (!ParseImportDecl(bExported))
				{
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
				}
				continue;
			}

			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "namespace")
			{
				if ((!PrefixTypeParams.empty() || !PrefixTemplateParameters.IsEmpty()))
				{
					ReportErrorAt(Peek(), "template prefix is not supported on namespace declarations");
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
					continue;
				}
				if (!PendingDeclMeta.empty())
				{
					ReportErrorAt(Peek(), "annotations are not supported on namespace");
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
					continue;
				}
				if (bExported)
				{
					ReportErrorAt(Peek(), "namespace cannot be exported; export members instead");
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
					continue;
				}
				if (!ParseNamespaceDecl())
				{
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
				}
				continue;
			}

			if (Check(ETokenKind::Keyword) && (Peek().Lexeme == "using" || Peek().Lexeme == "typedef"))
			{
				if ((!PrefixTypeParams.empty() || !PrefixTemplateParameters.IsEmpty()))
				{
					ReportErrorAt(Peek(), "template prefix is not supported on using/typedef declarations");
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
					continue;
				}
				if (!PendingDeclMeta.empty())
				{
					ReportErrorAt(Peek(), "annotations are not supported on using/typedef");
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
					continue;
				}
				const bool bUsingNamespace = Peek().Lexeme == "using" && Current + 1 < Tokens.size() && Tokens[Current + 1].Kind == ETokenKind::Keyword && Tokens[Current + 1].Lexeme == "namespace";
				if (bUsingNamespace && bExported)
				{
					ReportErrorAt(Peek(), "using namespace directive cannot be exported");
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
					continue;
				}
				if (!(bUsingNamespace ? ParseUsingNamespaceDecl() : ParseUsingDecl(bExported)))
				{
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
				}
				continue;
			}

			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "struct")
			{
				if (bStaticClass && bHasTypeDefaultAccess && TypeDefaultAccess == EMemberAccess::Protected)
				{
					ReportErrorAt(TypeHeaderModifierLocation, "a static struct cannot have protected default access");
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
					continue;
				}
				if (!ParseStructDecl(
				        bExported,
				        std::move(PendingDeclMeta),
				        std::move(PrefixTypeParams),
				        std::move(PrefixTypeParamDefaults),
				        bHasTypeDefaultAccess ? TypeDefaultAccess : EMemberAccess::Public,
				        bStaticClass, std::move(PrefixTemplateParameters)))
				{
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
				}
				continue;
			}

			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "class")
			{
				if (bStaticClass && bHasTypeDefaultAccess && TypeDefaultAccess == EMemberAccess::Protected)
				{
					ReportErrorAt(TypeHeaderModifierLocation, "a static class cannot have protected default access");
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
					continue;
				}
				if (!ParseClassDecl(
				        bExported,
				        std::move(PendingDeclMeta),
				        std::move(PrefixTypeParams),
				        std::move(PrefixTypeParamDefaults),
				        bHasTypeDefaultAccess ? TypeDefaultAccess : EMemberAccess::Private,
				        bStaticClass, std::move(PrefixTemplateParameters)))
				{
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
				}
				continue;
			}

			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "interface")
			{
				if (bStaticClass)
				{
					ReportErrorAt(TypeHeaderModifierLocation, "'static' is only supported on class or struct declarations");
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
					continue;
				}
				if (!ParseInterfaceDecl(
				        bExported,
				        std::move(PendingDeclMeta),
				        std::move(PrefixTypeParams),
				        std::move(PrefixTypeParamDefaults),
				        bHasTypeDefaultAccess ? TypeDefaultAccess : EMemberAccess::Public, std::move(PrefixTemplateParameters)))
				{
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
				}
				continue;
			}

			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "enum")
			{
				if ((!PrefixTypeParams.empty() || !PrefixTemplateParameters.IsEmpty()))
				{
					ReportErrorAt(Peek(), "template prefix is not supported on enum declarations");
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
					continue;
				}
				if (!ParseEnumDecl(bExported, std::move(PendingDeclMeta)))
				{
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
				}
				continue;
			}

			if (LooksLikeOutOfClassDefinition())
			{
				if (bExported || !PendingDeclMeta.empty())
				{
					ReportErrorAt(Peek(), "out-of-class definitions cannot repeat export or declaration annotations");
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
					continue;
				}
				if (!ParseOutOfClassDefinition(
				        std::move(PrefixTypeParams),
				        std::move(PrefixTypeParamDefaults), std::move(TemplateHeaders)) &&
				    !RecoverAfterDeclarationError())
				{
					return nullptr;
				}
				continue;
			}

			if ((!PrefixTypeParams.empty() || !PrefixTemplateParameters.IsEmpty()) || LooksLikeGenericFunctionDecl())
			{
				if (!ParseFunctionDecl(
				        bExported,
				        std::move(PendingDeclMeta),
				        std::move(PrefixTypeParams),
				        std::move(PrefixTypeParamDefaults), std::move(PrefixTemplateParameters)))
				{
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
				}
				continue;
			}

			if (CheckTypeName())
			{
				const bool bLeadingConst =
				    Check(ETokenKind::Keyword) && Peek().Lexeme == "const";
				const bool bSoftContainerType =
				    (!bLeadingConst && Check(ETokenKind::Keyword) && ParserDetail::IsContainerTypeKeyword(Peek().Lexeme)) || (bLeadingConst && Current + 1 < Tokens.size() && Tokens[Current + 1].Kind == ETokenKind::Keyword && ParserDetail::IsContainerTypeKeyword(Tokens[Current + 1].Lexeme));
				if (!(bSoftContainerType && !LooksLikeContainerTypeApplication()))
				{
					const std::size_t Checkpoint = Current;
					const int PendingCheckpoint = PendingGenericCloseCount;
					FDeclMeta FunctionMeta = PendingDeclMeta;
					const bool bLooksLikeVariable = LooksLikeTypedVarDecl() && !LooksLikeFunctionPrototype(bExported);
					if (!bLooksLikeVariable && ParseFunctionDecl(bExported, std::move(FunctionMeta), PrefixTypeParams, PrefixTypeParamDefaults))
					{
						continue;
					}
					if (bHasError && !bLooksLikeVariable)
					{
						const bool bAllowSoftFallback =
						    bSoftContainerType && (Error.Message.find("expected type name") != std::string::npos || Error.Message.find("expected '<' after") != std::string::npos);
						if (!bAllowSoftFallback)
						{
							if (!RecoverAfterDeclarationError())
							{
								return nullptr;
							}
							continue;
						}
					}
					Current = Checkpoint;
					PendingGenericCloseCount = PendingCheckpoint;
					bHasError = false;
					Error = {};
					std::unique_ptr<FStmt> Statement = ParseStatement();
					if (!Statement)
					{
						if (!RecoverAfterDeclarationError())
						{
							return nullptr;
						}
						continue;
					}
					if (Statement->Kind == EStmtKind::VarDecl)
					{
						FVarDeclStmt& VarDecl = static_cast<FVarDeclStmt&>(*Statement);
						VarDecl.bExported = bExported;
						VarDecl.DeclMeta = std::move(PendingDeclMeta);
					}
					else if (Statement->Kind == EStmtKind::VarDeclGroup)
					{
						// Top-level declarations retain their ordinary per-variable AST
						// representation. This keeps global symbol registration, package
						// metadata, and bytecode layout unchanged while preserving the
						// group's left-to-right source order.
						FVarDeclGroupStmt& Group = static_cast<FVarDeclGroupStmt&>(*Statement);
						for (std::unique_ptr<FVarDeclStmt>& VarDecl : Group.Declarations)
						{
							VarDecl->bExported = bExported;
							VarDecl->DeclMeta = PendingDeclMeta;
							VarDecl->ModulePath.assign(FileLabel);
							Program->TopLevelStatements.push_back(std::move(VarDecl));
						}
						continue;
					}
					Statement->ModulePath.assign(FileLabel);
					Program->TopLevelStatements.push_back(std::move(Statement));
					continue;
				}
			}

			if (!PendingDeclMeta.empty())
			{
				// e.g. print(x); peeked as annotation — roll back and parse as statement.
				RollbackDeclPrefix();
				std::unique_ptr<FStmt> Statement = ParseStatement();
				if (!Statement)
				{
					if (!RecoverAfterDeclarationError())
					{
						return nullptr;
					}
					continue;
				}
				Statement->ModulePath.assign(FileLabel);
				Program->TopLevelStatements.push_back(std::move(Statement));
				continue;
			}

			if ((!PrefixTypeParams.empty() || !PrefixTemplateParameters.IsEmpty()))
			{
				ReportErrorAt(Peek(), "template prefix must precede a class, struct, interface, function, or matching out-of-class definition");
				if (!RecoverAfterDeclarationError())
				{
					return nullptr;
				}
				continue;
			}

			if (bExported)
			{
				ReportErrorAt(Peek(), "export must precede a top-level declaration");
				if (!RecoverAfterDeclarationError())
				{
					return nullptr;
				}
				continue;
			}

			std::unique_ptr<FStmt> Statement = ParseStatement();
			if (!Statement)
			{
				if (!RecoverAfterDeclarationError())
				{
					return nullptr;
				}
				continue;
			}
			Statement->ModulePath.assign(FileLabel);
			Program->TopLevelStatements.push_back(std::move(Statement));
		}

		if (!Diagnostics.empty())
		{
			Error = Diagnostics.front();
			bHasError = true;
			return nullptr;
		}

		if (!ValidateOutOfClassDefinitions())
		{
			return nullptr;
		}
		return std::move(Program);
	}

	void VeroScriptParser::SetCollectMultipleErrors(const bool bEnabled, const std::size_t MaxErrors)
	{
		bCollectMultipleErrors = bEnabled;
		MaxCollectedErrors = MaxErrors == 0 ? 1 : MaxErrors;
	}

	void VeroScriptParser::SetHostTypeNames(std::unordered_set<std::string> TypeNames)
	{
		for (const auto& Type : StandardLibrary::GetBindingCatalog().GetTypes())
		{
			TypeNames.insert(Type.Name);
		}
		HostTypeNames = std::move(TypeNames);
	}

	const FToken& VeroScriptParser::Peek() const
	{
		return Tokens[Current];
	}

	const FToken& VeroScriptParser::PeekNext() const
	{
		if (Current + 1 >= Tokens.size())
		{
			return Tokens.back();
		}
		return Tokens[Current + 1];
	}

	const FToken& VeroScriptParser::Previous() const
	{
		return Tokens[Current - 1];
	}

	bool VeroScriptParser::IsAtEnd() const
	{
		return Peek().Kind == ETokenKind::EndOfFile;
	}

	bool VeroScriptParser::Check(const ETokenKind Kind) const
	{
		return !IsAtEnd() && Peek().Kind == Kind;
	}

	bool VeroScriptParser::CheckSymbol(const char* Lexeme) const
	{
		return Check(ETokenKind::Symbol) && Peek().Lexeme == Lexeme;
	}

	bool VeroScriptParser::MatchExplicitDiscardPrefix()
	{
		if (Current + 2 >= Tokens.size() || Tokens[Current].Kind != ETokenKind::Symbol || Tokens[Current].Lexeme != "(" || Tokens[Current + 1].Kind != ETokenKind::Keyword || Tokens[Current + 1].Lexeme != "void" || Tokens[Current + 2].Kind != ETokenKind::Symbol || Tokens[Current + 2].Lexeme != ")")
		{
			return false;
		}

		Current += 3;
		return true;
	}

	bool VeroScriptParser::TryMatchCStyleCastType(std::string& OutTypeName)
	{
		OutTypeName.clear();
		if (!CheckSymbol("(") || bHasError || Current + 1 >= Tokens.size() || !CheckTypeNameAt(Current + 1))
		{
			return false;
		}

		const std::size_t CurrentCheckpoint = Current;
		const int PendingCheckpoint = PendingGenericCloseCount;
		const bool bDepthExceededCheckpoint = bTypeNameDepthExceeded;
		const bool bHasErrorCheckpoint = bHasError;
		const FScriptError ErrorCheckpoint = Error;
		const std::size_t DiagnosticCountCheckpoint = Diagnostics.size();
		const auto RestoreSpeculation = [&]()
		{
			Current = CurrentCheckpoint;
			PendingGenericCloseCount = PendingCheckpoint;
			bTypeNameDepthExceeded = bDepthExceededCheckpoint;
			bHasError = bHasErrorCheckpoint;
			Error = ErrorCheckpoint;
			Diagnostics.resize(DiagnosticCountCheckpoint);
			OutTypeName.clear();
		};

		Advance();
		if (CheckTypeName())
		{
			OutTypeName = ParseExpressionTypeArgumentName();
			if (!bHasError && !OutTypeName.empty() && MatchSymbol(")"))
			{
				const FToken& OperandStart = Peek();
				const bool bCanStartUnary =
				    OperandStart.Kind == ETokenKind::Identifier || OperandStart.Kind == ETokenKind::IntegerLiteral || OperandStart.Kind == ETokenKind::CharacterLiteral || OperandStart.Kind == ETokenKind::FloatLiteral || OperandStart.Kind == ETokenKind::StringLiteral || OperandStart.Kind == ETokenKind::WStringLiteral || OperandStart.Kind == ETokenKind::BooleanLiteral || OperandStart.Kind == ETokenKind::NullptrLiteral || (OperandStart.Kind == ETokenKind::Keyword && (OperandStart.Lexeme == "this" || OperandStart.Lexeme == "new" || OperandStart.Lexeme == "fn" || OperandStart.Lexeme == "function" || ParserDetail::IsTypeKeyword(OperandStart.Lexeme) || ParserDetail::IsContainerTypeKeyword(OperandStart.Lexeme))) || (OperandStart.Kind == ETokenKind::Symbol && (OperandStart.Lexeme == "(" || OperandStart.Lexeme == "[" || OperandStart.Lexeme == "::" || OperandStart.Lexeme == "++" || OperandStart.Lexeme == "--" || OperandStart.Lexeme == "-" || OperandStart.Lexeme == "!" || OperandStart.Lexeme == "~"));

				// A simple user type spelling can also name a shadowing local value.
				// Preserve pre-existing postfix/binary expressions such as `(Box)(1)`,
				// `(Box)[0]`, and `(Box)-1`; unambiguous `(Box)value` remains a cast.
				const bool bSimpleIdentifierType =
				    CurrentCheckpoint + 3 == Current && Tokens[CurrentCheckpoint + 1].Kind == ETokenKind::Identifier;
				const bool bAmbiguousValueContinuation = bSimpleIdentifierType && OperandStart.Kind == ETokenKind::Symbol && (OperandStart.Lexeme == "(" || OperandStart.Lexeme == "[" || OperandStart.Lexeme == "::" || OperandStart.Lexeme == "++" || OperandStart.Lexeme == "--" || OperandStart.Lexeme == "-");
				if (bCanStartUnary && !bAmbiguousValueContinuation)
				{
					return true;
				}
			}
		}

		// A parenthesized value such as `(Color::Ready)` can begin with a known
		// type name. Failed type speculation must therefore be completely silent.
		RestoreSpeculation();
		return false;
	}

	bool VeroScriptParser::CheckTypeKeyword() const
	{
		return Check(ETokenKind::Keyword) && ParserDetail::IsTypeKeyword(Peek().Lexeme);
	}

	bool VeroScriptParser::CheckTypeName() const
	{
		std::size_t Index = Current;
		while (Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Keyword && (Tokens[Index].Lexeme == "const" || Tokens[Index].Lexeme == "constexpr"))
		{
			++Index;
		}
		if (Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Keyword && Tokens[Index].Lexeme == "ref")
		{
			++Index;
		}
		return CheckTypeNameAt(Index);
	}

	bool VeroScriptParser::CheckTypeNameAt(const std::size_t Index) const
	{
		if (Index >= Tokens.size())
		{
			return false;
		}
		const FToken& Token = Tokens[Index];
		const FToken& NextToken = (Index + 1 < Tokens.size()) ? Tokens[Index + 1] : Tokens.back();
		if (Token.Kind == ETokenKind::Keyword && (Token.Lexeme == "array" || Token.Lexeme == "map" || Token.Lexeme == "multimap" || Token.Lexeme == "set" || Token.Lexeme == "multiset" || Token.Lexeme == "ordered_multiset" || Token.Lexeme == "queue" || Token.Lexeme == "priority_queue" || Token.Lexeme == "stack" || Token.Lexeme == "ordered_map" || Token.Lexeme == "ordered_multimap" || Token.Lexeme == "deque" || Token.Lexeme == "channel"))
		{
			return NextToken.Kind == ETokenKind::Symbol && NextToken.Lexeme == "<";
		}
		if (Token.Kind == ETokenKind::Keyword && (Token.Lexeme == "fn" || Token.Lexeme == "function"))
		{
			return NextToken.Kind == ETokenKind::Symbol && NextToken.Lexeme == "(";
		}
		if (Token.Kind == ETokenKind::Keyword && ParserDetail::IsTypeKeyword(Token.Lexeme))
		{
			return true;
		}
		std::string QualifiedTypeName;
		std::size_t EndIndex = Index;
		if (TryBuildQualifiedTypeName(Index, QualifiedTypeName, EndIndex))
		{
			if (Token.Kind == ETokenKind::Identifier && BindingStore)
			{
				std::size_t TypeScopeOrder = 0;
				for (const auto& Frame : TypeFrames)
				{
					if ((QualifiedTypeName == Frame.Name || QualifiedTypeName.rfind(Frame.Name + ".", 0) == 0) &&
					    Frame.AliasBoundary < BindingStore->AliasOrder.size())
					{
						TypeScopeOrder = std::max(TypeScopeOrder, BindingStore->AliasOrder[Frame.AliasBoundary]);
					}
				}
				if (BindingStore->IsRuntimeName(std::string(Token.Lexeme), LocalTypeAliasScopes,
				        RuntimeBindingScopes, TypeScopeOrder) &&
				    StandardLibrary::ResolveTypeName(QualifiedTypeName).empty() &&
				    (QualifiedTypeName != Token.Lexeme ||
				        StandardLibrary::ResolveTypeName("std." + std::string(Token.Lexeme)).empty()))
				{
					return false;
				}
			}
			if (!bDeclarationOutline || IsUserTypeName(QualifiedTypeName) ||
			    ActiveTypeParams.find(QualifiedTypeName) != ActiveTypeParams.end())
			{
				return true;
			}
			// An unbound identifier is a type only in explicit declaration syntax.
			// Calls and top-level expressions remain ordinary deferred statements.
			auto End = Index;
			if (!TrySkipTypeNameTokens(End) || End >= Tokens.size()) return false;
			if (Tokens[End].Lexeme == "&")
			{
				++End;
			}

			return End < Tokens.size() && ParserDetail::IsNameToken(Tokens[End]);
		}
		return false;
	}

	bool VeroScriptParser::MatchOptionalConst()
	{
		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "const")
		{
			Advance();
			return true;
		}
		return false;
	}

	bool VeroScriptParser::MatchConstexprSpecifiers(bool& OutConstexpr)
	{
		bool IsConst = false;
		OutConstexpr = false;
		while (Check(ETokenKind::Keyword) && (Peek().Lexeme == "const" || Peek().Lexeme == "constexpr"))
		{
			const bool IsConstexpr = Peek().Lexeme == "constexpr";
			if (IsConstexpr ? OutConstexpr : IsConst)
			{
				ReportErrorAt(Peek(), "duplicate declaration specifier");
			}
			(IsConstexpr ? OutConstexpr : IsConst) = true;
			Advance();
		}
		return IsConst || OutConstexpr;
	}

	bool VeroScriptParser::MatchOptionalStatic()
	{
		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "static")
		{
			Advance();
			return true;
		}
		return false;
	}

	bool VeroScriptParser::MatchOptionalMutable()
	{
		// Keep this contextual: unlike C++, VeroScript previously allowed an
		// identifier named `mutable`, and this modifier is meaningful only for a
		// field declaration parsed by ParseFieldDecl. In particular, preserve a
		// pre-existing type declaration such as `struct mutable {}; mutable value;`.
		if ((Check(ETokenKind::Keyword) || Check(ETokenKind::Identifier)) && Peek().Lexeme == "mutable")
		{
			std::size_t TypeIndex = Current + 1;
			while (TypeIndex < Tokens.size() && Tokens[TypeIndex].Kind == ETokenKind::Keyword && (Tokens[TypeIndex].Lexeme == "static" || Tokens[TypeIndex].Lexeme == "const"))
			{
				++TypeIndex;
			}
			if (!CheckTypeNameAt(TypeIndex))
			{
				return false;
			}
			Advance();
			return true;
		}
		return false;
	}

	bool VeroScriptParser::MatchOptionalRef()
	{
		bool bMatched = false;
		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "ref")
		{
			Advance();
			bMatched = true;
		}
		else if (CheckSymbol("&"))
		{
			// Prefix `& int x` / call `&x`; `&&` and `&=` remain distinct longer symbols.

			Advance();
			bMatched = true;
		}
		if (bMatched && ((Check(ETokenKind::Keyword) && Peek().Lexeme == "ref") || CheckSymbol("&")))
		{
			ReportErrorAt(Peek(), "duplicate ref marker");
		}
		return bMatched;
	}

	bool VeroScriptParser::MatchOptionalTrailingRefAmpersand(bool& bInOutIsRef)
	{
		if (!CheckSymbol("&"))
		{
			return true;
		}
		if (bInOutIsRef)
		{
			ReportErrorAt(
			    Peek(),
			    "duplicate ref marker: cannot combine prefix ref/& with trailing &");
			return false;
		}
		Advance();
		bInOutIsRef = true;
		return true;
	}

	bool VeroScriptParser::LooksLikeGenericFunctionDecl() const
	{
		if (Check(ETokenKind::Identifier) && !IsUserTypeName(Peek().Lexeme) && !ParserDetail::IsTypeKeyword(Peek().Lexeme) && Current + 2 < Tokens.size() && ParserDetail::IsNameToken(Tokens[Current + 1]) && Tokens[Current + 2].Kind == ETokenKind::Symbol && Tokens[Current + 2].Lexeme == "(")
		{
			return true;
		}

		std::size_t Opening = 0;
		return FindFunctionTemplateHeader(Opening);
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseCallArgument()
	{
		if (ParserDetail::IsNameToken(Peek()) && PeekNext().Kind == ETokenKind::Symbol && PeekNext().Lexeme == ":")
		{
			ReportErrorAt(Peek(), "named arguments are not supported");
			return nullptr;
		}

		const bool bRefKeyword = Check(ETokenKind::Keyword) && Peek().Lexeme == "ref";
		const bool bRefAmp = CheckSymbol("&");
		if (bRefKeyword || bRefAmp)
		{
			const FSourceLocation Location = Peek().Location;
			Advance();
			if (!ParserDetail::IsNameToken(Peek()))
			{
				ReportErrorAt(
				    Peek(),
				    "ref argument expects an identifier, field, or index expression");
				return nullptr;
			}
			// Share generic type applications and interleaved member/index chains
			// with ordinary expressions, retaining the reference-target restriction.
			std::unique_ptr<FExpr> Operand = ParsePostfix();
			if (!Operand)
			{
				return nullptr;
			}
			if (Operand->Kind != EExprKind::Identifier && Operand->Kind != EExprKind::MemberAccess && Operand->Kind != EExprKind::IndexAccess && Operand->Kind != EExprKind::Call && Operand->Kind != EExprKind::MethodCall)
			{
				ReportErrorAt(
				    Previous(),
				    "ref argument expects an identifier, field, or index expression");
				return nullptr;
			}
			auto RefExpr = std::make_unique<FRefExpr>(std::move(Operand));
			RefExpr->Location = Location;
			return RefExpr;
		}
		return ParseExpression();
	}

	std::unique_ptr<FExpr> VeroScriptParser::ParseLambdaAfterOpenParen(
	    std::unique_ptr<FLambdaExpr> Lambda,
	    const FSourceLocation& Location)
	{
		if (!Lambda)
		{
			return nullptr;
		}
		Lambda->Location = Location;
		if (!CheckSymbol(")"))
		{
			do
			{
				const auto ParameterBegin = Current;
				FFunctionParam Param;
				Param.bIsConst = MatchOptionalConst();
				Param.bIsRef = MatchOptionalRef();
				// Context-typed lambda: bare `x` (ident then `,`/`)`) omits the type.
				const bool bBareName =
				    Check(ETokenKind::Identifier) && Current + 1 < Tokens.size() && Tokens[Current + 1].Kind == ETokenKind::Symbol && (Tokens[Current + 1].Lexeme == "," || Tokens[Current + 1].Lexeme == ")");
				if (bBareName)
				{
					Param.TypeName.clear();
					MatchName();
					Param.Name.assign(Previous().Lexeme);
				}
				else
				{
					Param.TypeName = ParseTypeName();
					if (bHasError)
					{
						return nullptr;
					}
					if (!ParseParamArraySuffix(Param.TypeName))
					{
						return nullptr;
					}
					if (!MatchOptionalTrailingRefAmpersand(Param.bIsRef))
					{
						return nullptr;
					}
					if (!MatchName())
					{
						ReportErrorAt(Peek(), "expected parameter name");
						return nullptr;
					}
					Param.Name.assign(Previous().Lexeme);
					if (!ParseArraySuffix(Param.TypeName))
					{
						return nullptr;
					}
				}
				if (!SourceText.empty() && Current > ParameterBegin)
				{
					Param.DeclMeta.SourceRange = CaptureSourceRange(ParameterBegin, Current);
				}

				Lambda->Parameters.push_back(std::move(Param));
			}
			while (MatchSymbol(","));
		}
		if (!MatchSymbol(")"))
		{
			ReportErrorAt(Peek(), "expected ')' after lambda parameters");
			return nullptr;
		}
		if (Peek().Lexeme == "mutable")
		{
			Advance();
			Lambda->bMutable = true;
		}
		if (MatchSymbol("->"))
		{
			Lambda->ReturnType = ParseReturnTypeName();
			if (bHasError || Lambda->ReturnType.empty())
			{
				return nullptr;
			}
		}
		else
		{
			// Empty = infer from assignment/argument expected `fn(...)->R`.
			Lambda->ReturnType.clear();
		}

		if (CheckSymbol("{"))
		{
			Lambda->Body = ParseCallableBlock("$lambda:" + std::to_string(Lambda->Location.Line) + ":" +
			        std::to_string(Lambda->Location.Column),
			    Lambda->Parameters);
			if (!Lambda->Body)
			{
				return nullptr;
			}
			return Lambda;
		}

		if (!MatchSymbol("=>"))
		{
			ReportErrorAt(Peek(), "expected '{' or '=>' after lambda parameters");
			return nullptr;
		}
		std::unique_ptr<FExpr> ExpressionBody = ParseExpression();
		if (!ExpressionBody)
		{
			return nullptr;
		}

		auto Body = std::make_unique<FBlockStmt>();
		Body->Location = ExpressionBody->Location;
		// Untyped return (`fn(x) => …`) treats the expression as a value return; void is
		// only used when the return type was written explicitly as `void`.
		if (Lambda->ReturnType == "void")
		{
			auto ExprStmt = std::make_unique<FExprStmt>(std::move(ExpressionBody));
			ExprStmt->Location = Body->Location;
			Body->Statements.push_back(std::move(ExprStmt));
		}
		else
		{
			auto ReturnStmt = std::make_unique<FReturnStmt>(std::move(ExpressionBody));
			ReturnStmt->Location = Body->Location;
			Body->Statements.push_back(std::move(ReturnStmt));
		}
		Lambda->Body = std::move(Body);
		return Lambda;
	}

	bool VeroScriptParser::LooksLikeContainerTypeApplication() const
	{
		// Current token is a container keyword and next is '<' (CheckTypeName already true).
		// Distinguish `set<int>` from comparison `set < queue` (RHS soft keyword without '<').
		std::size_t TypeStart = Current;
		if (TypeStart < Tokens.size() && Tokens[TypeStart].Kind == ETokenKind::Keyword && Tokens[TypeStart].Lexeme == "const")
		{
			++TypeStart;
		}
		if (TypeStart < Tokens.size() && Tokens[TypeStart].Kind == ETokenKind::Keyword && Tokens[TypeStart].Lexeme == "ref")
		{
			++TypeStart;
		}
		if (TypeStart + 2 >= Tokens.size())
		{
			return false;
		}
		std::size_t Index = TypeStart + 2;
		int PendingCloseCount = 0;
		return TrySkipTypeNameTokens(Index, PendingCloseCount);
	}

	bool VeroScriptParser::TrySkipTypeNameTokens(std::size_t& Index) const
	{
		int PendingCloseCount = 0;
		return TrySkipTypeNameTokens(Index, PendingCloseCount);
	}

	bool VeroScriptParser::LooksLikeTypedVarDecl() const
	{
		std::size_t Index = Current;
		while (Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Keyword && (Tokens[Index].Lexeme == "const" || Tokens[Index].Lexeme == "constexpr"))
		{
			++Index;
		}
		if (Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Keyword && Tokens[Index].Lexeme == "ref")
		{
			++Index;
		}
		if (!TrySkipTypeNameTokens(Index) || Index >= Tokens.size())
		{
			return false;
		}
		if (Tokens[Index].Kind == ETokenKind::Symbol && Tokens[Index].Lexeme == "&")
		{
			++Index;
		}
		if (Index >= Tokens.size() || !ParserDetail::IsNameToken(Tokens[Index]))
		{
			return false;
		}
		++Index;
		if (Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Symbol)
		{
			if (Tokens[Index].Lexeme == "=" || Tokens[Index].Lexeme == ";" || Tokens[Index].Lexeme == "[" || Tokens[Index].Lexeme == "{")
			{
				return true;
			}
		}
		if (Index >= Tokens.size() || Tokens[Index].Kind != ETokenKind::Symbol || Tokens[Index].Lexeme != "(")
		{
			return false;
		}

		int ParenDepth = 0;
		int BracketDepth = 0;
		int BraceDepth = 0;
		for (; Index < Tokens.size(); ++Index)
		{
			const FToken& Token = Tokens[Index];
			if (Token.Kind != ETokenKind::Symbol)
			{
				continue;
			}
			if (Token.Lexeme == "(")
			{
				++ParenDepth;
			}
			else if (Token.Lexeme == ")")
			{
				if (--ParenDepth < 0)
				{
					return false;
				}
				if (ParenDepth == 0 && BracketDepth == 0 && BraceDepth == 0)
				{
					++Index;
					return Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Symbol && Tokens[Index].Lexeme == ";";
				}
			}
			else if (Token.Lexeme == "[")
			{
				++BracketDepth;
			}
			else if (Token.Lexeme == "]")
			{
				if (--BracketDepth < 0)
				{
					return false;
				}
			}
			else if (Token.Lexeme == "{")
			{
				++BraceDepth;
			}
			else if (Token.Lexeme == "}")
			{
				if (--BraceDepth < 0)
				{
					return false;
				}
			}
		}
		return false;
	}

	bool VeroScriptParser::LooksLikeFunctionPrototype(bool bExplicitFunctionDeclaration) const
	{
		std::size_t I = Current;
		if (!TrySkipTypeNameTokens(I)) return false;
		if (I < Tokens.size() && Tokens[I].Lexeme == "&")
		{
			++I;
		}

		if (I >= Tokens.size() || !ParserDetail::IsNameToken(Tokens[I])) return false;
		if (++I >= Tokens.size() || Tokens[I++].Lexeme != "(") return false;
		if (I < Tokens.size() && Tokens[I].Lexeme == ")")
		{
			// Preserve the existing named-type zero-argument object initializer.
			// Explicit export disambiguates a bodyless function returning a user type.
			if (Tokens[Current].Kind == ETokenKind::Identifier && !bExplicitFunctionDeclaration) return false;
			return I + 1 < Tokens.size() && Tokens[I + 1].Lexeme == ";";
		}
		if (I < Tokens.size() && Tokens[I].Lexeme == "const")
		{
			++I;
		}

		if (I < Tokens.size() && Tokens[I].Lexeme == "ref")
		{
			++I;
		}

		if (!TrySkipTypeNameTokens(I)) return false;
		if (I < Tokens.size() && Tokens[I].Lexeme == "&")
		{
			++I;
		}

		if (I < Tokens.size() && Tokens[I].Lexeme == "...")
		{
			++I;
		}

		return I < Tokens.size() && ParserDetail::IsNameToken(Tokens[I]);
	}

	bool VeroScriptParser::LooksLikeMethodDecl() const
	{
		std::size_t HeaderOpening = 0;
		if (FindFunctionTemplateHeader(HeaderOpening))
		{
			return true;
		}
		std::size_t Index = Current;
		while (Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Keyword && (Tokens[Index].Lexeme == "static" || Tokens[Index].Lexeme == "const" || Tokens[Index].Lexeme == "constexpr"))
		{
			++Index;
		}
		if (Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Keyword && Tokens[Index].Lexeme == "virtual")
		{
			++Index;
		}
		if (!TrySkipTypeNameTokens(Index))
		{
			return false;
		}
		if (Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Symbol && Tokens[Index].Lexeme == "&")
		{
			++Index;
		}
		if (Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Keyword && Tokens[Index].Lexeme == "operator")
		{
			++Index;
			if (Index >= Tokens.size())
			{
				return false;
			}
			// operator[] ( … )
			if (Tokens[Index].Kind == ETokenKind::Symbol && Tokens[Index].Lexeme == "[")
			{
				++Index;
				if (Index >= Tokens.size() || Tokens[Index].Kind != ETokenKind::Symbol || Tokens[Index].Lexeme != "]")
				{
					return false;
				}
				++Index;
				return Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Symbol && Tokens[Index].Lexeme == "(";
			}
			// operator() ( … ) — declaration parameter list follows the empty () name.
			if (Tokens[Index].Kind == ETokenKind::Symbol && Tokens[Index].Lexeme == "(")
			{
				++Index;
				if (Index >= Tokens.size() || Tokens[Index].Kind != ETokenKind::Symbol || Tokens[Index].Lexeme != ")")
				{
					return false;
				}
				++Index;
				return Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Symbol && Tokens[Index].Lexeme == "(";
			}
			// operator + ( … ) / operator += ( … ) / …
			if (Tokens[Index].Kind == ETokenKind::Symbol)
			{
				++Index;
				return Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Symbol && Tokens[Index].Lexeme == "(";
			}
			// operator Type ( … ) conversion
			int PendingClose = 0;
			if (!TrySkipTypeNameTokens(Index, PendingClose))
			{
				return false;
			}
			return Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Symbol && Tokens[Index].Lexeme == "(";
		}
		if (Index >= Tokens.size() || !ParserDetail::IsNameToken(Tokens[Index]))
		{
			return false;
		}
		++Index;
		return Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Symbol && Tokens[Index].Lexeme == "(";
	}

	bool VeroScriptParser::LooksLikeConstructorDecl(const std::string_view TypeName) const
	{
		std::size_t Index = Current;
		if (Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Keyword && Tokens[Index].Lexeme == "explicit")
		{
			++Index;
		}
		if (Index >= Tokens.size() || Tokens[Index].Kind != ETokenKind::Identifier || Tokens[Index].Lexeme != TypeName)
		{
			return false;
		}
		++Index;
		return Index < Tokens.size() && Tokens[Index].Kind == ETokenKind::Symbol && Tokens[Index].Lexeme == "(";
	}

	const FToken& VeroScriptParser::Advance()
	{
		if ((Current & 127u) == 0)
		{
			CheckAnalysisCancellation();
		}
		if (!IsAtEnd())
		{
			++Current;
		}
		return Previous();
	}

	bool VeroScriptParser::Match(const ETokenKind Kind)
	{
		if (!Check(Kind))
		{
			return false;
		}
		Advance();
		return true;
	}

	bool VeroScriptParser::MatchName()
	{
		if (!Check(ETokenKind::Identifier) && !(Check(ETokenKind::Keyword) && ParserDetail::IsContainerTypeKeyword(Peek().Lexeme)))
		{
			return false;
		}
		Advance();
		return true;
	}

	bool VeroScriptParser::MatchSymbol(const char* Lexeme)
	{
		if (!CheckSymbol(Lexeme))
		{
			return false;
		}
		Advance();
		return true;
	}

	bool VeroScriptParser::MatchMemberAccessOp()
	{
		if (!CheckMemberAccessOp())
		{
			return false;
		}
		Advance();
		return true;
	}

	bool VeroScriptParser::CheckMemberAccessOp() const
	{
		return CheckSymbol(".") || CheckSymbol("::");
	}

	bool VeroScriptParser::MatchCloseGeneric()
	{
		if (PendingGenericCloseCount > 0)
		{
			--PendingGenericCloseCount;
			return true;
		}
		if (MatchSymbol(">"))
		{
			return true;
		}
		if (CheckSymbol(">>"))
		{
			Advance();
			++PendingGenericCloseCount;
			return true;
		}
		return false;
	}

	bool VeroScriptParser::TrySkipCloseGeneric(std::size_t& Index, int& PendingCloseCount) const
	{
		if (PendingCloseCount > 0)
		{
			--PendingCloseCount;
			return true;
		}
		if (Index >= Tokens.size() || Tokens[Index].Kind != ETokenKind::Symbol)
		{
			return false;
		}
		if (Tokens[Index].Lexeme == ">")
		{
			++Index;
			return true;
		}
		if (Tokens[Index].Lexeme == ">>")
		{
			++Index;
			PendingCloseCount = 1;
			return true;
		}
		return false;
	}

	bool VeroScriptParser::MatchOperatorMethodName(std::string& OutName)
	{
		if (!(Check(ETokenKind::Keyword) && Peek().Lexeme == "operator"))
		{
			return false;
		}
		Advance();
		if (Peek().Kind == ETokenKind::Symbol)
		{
			const std::string_view Symbol = Peek().Lexeme;
			const bool bAllowed = Symbol == "=" || Symbol == "+" || Symbol == "-" || Symbol == "*" || Symbol == "/" || Symbol == "%" || Symbol == "+=" || Symbol == "-=" || Symbol == "*=" || Symbol == "/=" || Symbol == "%=" || Symbol == "==" || Symbol == "!=" || Symbol == "<" || Symbol == "<=" || Symbol == ">" || Symbol == ">=" || Symbol == "!" || Symbol == "++" || Symbol == "--" || Symbol == "&" || Symbol == "|" || Symbol == "^" || Symbol == "<<" || Symbol == ">>" || Symbol == "~" || Symbol == "&&" || Symbol == "||";
			if (bAllowed)
			{
				OutName = "operator";
				OutName += Symbol;
				Advance();
				return true;
			}
		}
		if (MatchSymbol("["))
		{
			if (!MatchSymbol("]"))
			{
				ReportErrorAt(Peek(), "expected ']' after 'operator['");
				return false;
			}
			OutName = "operator[]";
			return true;
		}
		if (MatchSymbol("("))
		{
			if (!MatchSymbol(")"))
			{
				ReportErrorAt(Peek(), "expected ')' after 'operator('");
				return false;
			}
			OutName = "operator()";
			return true;
		}
		if (CheckTypeName())
		{
			const std::string ConvType = ParseTypeName();
			if (bHasError || ConvType.empty())
			{
				return false;
			}
			OutName = "operator conv ";
			OutName += ConvType;
			return true;
		}
		if (Peek().Kind == ETokenKind::Symbol)
		{
			ReportErrorAt(Peek(), "unsupported operator overload: " + std::string(Peek().Lexeme));
		}
		else
		{
			ReportErrorAt(Peek(), "expected operator symbol, '[]', '()', or type after 'operator'");
		}
		return false;
	}

	std::string VeroScriptParser::ParseTypeName()
	{
		const auto TypeBegin = Current;
		++TypeNameParseDepth;
		struct FDepthGuard
		{
			explicit FDepthGuard(int& InDepth) : Depth(InDepth)
			{
			}

			~FDepthGuard()
			{
				--Depth;
			}

			int& Depth;

		} DepthGuard{TypeNameParseDepth};
		// Type parsing recursively descends through generic and callable arguments.
		// Reject pathological input before it can exhaust the native thread stack.
		constexpr int MaxTypeNameParseDepth = 64;
		if (TypeNameParseDepth > MaxTypeNameParseDepth)
		{
			bTypeNameDepthExceeded = true;
			ReportErrorAt(Peek(), "type name nested too deeply");
			return {};
		}

		if (Check(ETokenKind::Keyword) && (Peek().Lexeme == "fn" || Peek().Lexeme == "function"))
		{
			const std::string FunctionKeyword(Peek().Lexeme);
			Advance();
			if (!MatchSymbol("("))
			{
				ReportErrorAt(Peek(), "expected '(' after '" + FunctionKeyword + "'");
				return {};
			}
			std::vector<std::string> ParamTypes;
			if (!CheckSymbol(")"))
			{
				do
				{
					const bool bParamConst = MatchOptionalConst();
					bool bParamRef = MatchOptionalRef();
					std::string ParamType = ParseTypeName();
					if (bHasError || ParamType.empty())
					{
						return {};
					}
					if (!MatchOptionalTrailingRefAmpersand(bParamRef))
					{
						return {};
					}
					if (bParamConst && !bParamRef)
					{
						ReportErrorAt(Peek(), "const function parameter type requires ref");
						return {};
					}
					if (bParamRef)
					{
						ParamType = bParamConst
						    ? "const ref " + ParamType
						    : "ref " + ParamType;
					}
					ParamTypes.push_back(std::move(ParamType));
				}
				while (MatchSymbol(","));
			}
			if (!MatchSymbol(")"))
			{
				ReportErrorAt(Peek(), "expected ')' after function type parameters");
				return {};
			}
			if (!MatchSymbol("->"))
			{
				ReportErrorAt(Peek(), "expected '->' after function type parameters");
				return {};
			}
			const std::string ReturnType = ParseReturnTypeName();
			if (bHasError || ReturnType.empty())
			{
				return {};
			}
			const std::string TypeName = Runtime::MakeFunctionTypeName(ParamTypes, ReturnType);
			std::vector<std::string> ParsedParams;
			std::string ParsedReturn;
			if (!Runtime::ParseFunctionType(TypeName, ParsedParams, ParsedReturn))
			{
				ReportErrorAt(Previous(), "invalid function type");
				return {};
			}
			return CompleteTypeName(TypeName, TypeBegin);
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "array")
		{
			Advance();
			if (!MatchSymbol("<"))
			{
				ReportErrorAt(Peek(), "expected '<' after 'array'");
				return {};
			}
			const std::string ElementType = ParseTypeName();
			if (bHasError)
			{
				return {};
			}
			if (!MatchCloseGeneric())
			{
				ReportErrorAt(Peek(), "expected '>' after dynamic array element type");
				return {};
			}
			const std::string TypeName = "array<" + ElementType + ">";
			std::string ParsedElementType;
			if (!Runtime::ParseDynamicArrayElementType(TypeName, ParsedElementType))
			{
				ReportErrorAt(Previous(), "invalid dynamic array element type");
				return {};
			}
			return CompleteTypeName(TypeName, TypeBegin);
		}

		if (Check(ETokenKind::Keyword) && (Peek().Lexeme == "map" || Peek().Lexeme == "multimap"))
		{
			const std::string ContainerName(Peek().Lexeme);
			Advance();
			if (!MatchSymbol("<"))
			{
				ReportErrorAt(Peek(), "expected '<' after '" + ContainerName + "'");
				return {};
			}
			const std::string KeyType = ParseTypeName();
			if (bHasError)
			{
				return {};
			}
			if (!MatchSymbol(","))
			{
				ReportErrorAt(Peek(), "expected ',' after " + ContainerName + " key type");
				return {};
			}
			const std::string ValueType = ParseTypeName();
			if (bHasError)
			{
				return {};
			}
			if (!MatchCloseGeneric())
			{
				ReportErrorAt(Peek(), "expected '>' after " + ContainerName + " value type");
				return {};
			}
			const std::string TypeName = ContainerName + "<" + KeyType + "," + ValueType + ">";
			std::string ParsedKeyType;
			std::string ParsedValueType;
			if (!Runtime::SplitMapType(TypeName, ParsedKeyType, ParsedValueType))
			{
				ReportErrorAt(Previous(), "invalid " + ContainerName + " type");
				return {};
			}
			// Host Class key eligibility depends on the binding catalog and is
			// validated by the compiler.
			return CompleteTypeName(TypeName, TypeBegin);
		}

		if (Check(ETokenKind::Keyword) && (Peek().Lexeme == "ordered_map" || Peek().Lexeme == "ordered_multimap"))
		{
			const std::string ContainerName(Peek().Lexeme);
			Advance();
			if (!MatchSymbol("<"))
			{
				ReportErrorAt(Peek(), "expected '<' after '" + ContainerName + "'");
				return {};
			}
			const std::string KeyType = ParseTypeName();
			if (bHasError)
			{
				return {};
			}
			if (!MatchSymbol(","))
			{
				ReportErrorAt(Peek(), "expected ',' after " + ContainerName + " key type");
				return {};
			}
			const std::string ValueType = ParseTypeName();
			if (bHasError)
			{
				return {};
			}
			if (!MatchCloseGeneric())
			{
				ReportErrorAt(Peek(), "expected '>' after " + ContainerName + " value type");
				return {};
			}
			const std::string TypeName = ContainerName + "<" + KeyType + "," + ValueType + ">";
			std::string ParsedKeyType;
			std::string ParsedValueType;
			if (!Runtime::SplitOrderedMapType(TypeName, ParsedKeyType, ParsedValueType))
			{
				ReportErrorAt(Previous(), "invalid " + ContainerName + " type");
				return {};
			}
			// Host Class key eligibility depends on the binding catalog.
			return CompleteTypeName(TypeName, TypeBegin);
		}

		if (Check(ETokenKind::Keyword) && (Peek().Lexeme == "set" || Peek().Lexeme == "multiset" || Peek().Lexeme == "ordered_multiset"))
		{
			const std::string ContainerName{Peek().Lexeme};
			Advance();
			if (!MatchSymbol("<"))
			{
				ReportErrorAt(Peek(), "expected '<' after '" + ContainerName + "'");
				return {};
			}
			const std::string ElementType = ParseTypeName();
			if (bHasError)
			{
				return {};
			}
			if (!MatchCloseGeneric())
			{
				ReportErrorAt(Peek(), "expected '>' after set element type");
				return {};
			}
			const std::string TypeName = ContainerName + "<" + ElementType + ">";
			std::string ParsedElementType;
			const bool bParsed = ContainerName == "set"
			    ? Runtime::SplitSetElementType(TypeName, ParsedElementType)
			    : Runtime::ParseMultiSetElementType(TypeName, ParsedElementType);
			if (!bParsed)
			{
				ReportErrorAt(Previous(), "invalid set element type");
				return {};
			}
			// Host Class element eligibility depends on the binding catalog.
			return CompleteTypeName(TypeName, TypeBegin);
		}

		if (Check(ETokenKind::Keyword) && (Peek().Lexeme == "queue" || Peek().Lexeme == "priority_queue"))
		{
			const std::string ContainerName{Peek().Lexeme};
			Advance();
			if (!MatchSymbol("<"))
			{
				ReportErrorAt(Peek(), "expected '<' after '" + ContainerName + "'");
				return {};
			}
			const std::string ElementType = ParseTypeName();
			if (bHasError)
			{
				return {};
			}
			if (!MatchCloseGeneric())
			{
				ReportErrorAt(Peek(), "expected '>' after queue element type");
				return {};
			}
			const std::string TypeName = ContainerName + "<" + ElementType + ">";
			std::string ParsedElementType;
			if (!Runtime::ParseQueueElementType(TypeName, ParsedElementType))
			{
				ReportErrorAt(Previous(), "invalid queue element type");
				return {};
			}
			return CompleteTypeName(TypeName, TypeBegin);
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "channel")
		{
			Advance();
			if (!MatchSymbol("<"))
			{
				ReportErrorAt(Peek(), "expected '<' after 'channel'");
				return {};
			}
			const std::string ElementType = ParseTypeName();
			if (bHasError)
			{
				return {};
			}
			if (!MatchCloseGeneric())
			{
				ReportErrorAt(Peek(), "expected '>' after channel element type");
				return {};
			}
			const std::string TypeName = "channel<" + ElementType + ">";
			std::string ParsedElementType;
			if (!Runtime::ParseChannelElementType(TypeName, ParsedElementType) && ActiveTypeParams.find(ElementType) == ActiveTypeParams.end())
			{
				ReportErrorAt(Previous(),
				    "invalid channel element type (expected scalar or channel)");
				return {};
			}
			return CompleteTypeName(TypeName, TypeBegin);
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "deque")
		{
			Advance();
			if (!MatchSymbol("<"))
			{
				ReportErrorAt(Peek(), "expected '<' after 'deque'");
				return {};
			}
			const std::string ElementType = ParseTypeName();
			if (bHasError)
			{
				return {};
			}
			if (!MatchCloseGeneric())
			{
				ReportErrorAt(Peek(), "expected '>' after deque element type");
				return {};
			}
			const std::string TypeName = "deque<" + ElementType + ">";
			std::string ParsedElementType;
			if (!Runtime::ParseDequeElementType(TypeName, ParsedElementType))
			{
				ReportErrorAt(Previous(), "invalid deque element type");
				return {};
			}
			return CompleteTypeName(TypeName, TypeBegin);
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "stack")
		{
			Advance();
			if (!MatchSymbol("<"))
			{
				ReportErrorAt(Peek(), "expected '<' after 'stack'");
				return {};
			}
			const std::string ElementType = ParseTypeName();
			if (bHasError)
			{
				return {};
			}
			if (!MatchCloseGeneric())
			{
				ReportErrorAt(Peek(), "expected '>' after stack element type");
				return {};
			}
			const std::string TypeName = "stack<" + ElementType + ">";
			std::string ParsedElementType;
			if (!Runtime::ParseStackElementType(TypeName, ParsedElementType))
			{
				ReportErrorAt(Previous(), "invalid stack element type");
				return {};
			}
			return CompleteTypeName(TypeName, TypeBegin);
		}

		if (CheckTypeKeyword())
		{
			if (Peek().Lexeme == "unsigned")
			{
				Advance();
				if (Check(ETokenKind::Keyword) && ParserDetail::IsUnsignedBaseTypeKeyword(Peek().Lexeme))
				{
					std::string TypeName = "unsigned ";
					TypeName.append(Peek().Lexeme);
					Advance();
					return CompleteTypeName(std::move(TypeName), TypeBegin);
				}
				return CompleteTypeName("unsigned int", TypeBegin);
			}

			const std::string TypeName(
			    CanonicalizePrimitiveTypeKeyword(Peek().Lexeme));
			Advance();
			return CompleteTypeName(TypeName, TypeBegin);
		}

		const bool bGlobalQualified = CheckSymbol("::");
		if (bGlobalQualified)
		{
			Advance();
		}
		if (Check(ETokenKind::Identifier))
		{
			std::string TypeName(Peek().Lexeme);
			const std::string SourceRoot = TypeName;
			Advance();
			std::string HostQualifiedTypeName = TypeName;
			// Script namespaces use '.', while a host may intentionally expose a
			// C++ namespace-enum type such as EEndPlayReason::Type. Preserve the
			// exact host spelling when the catalog declares it.
			while (MatchMemberAccessOp())
			{
				const std::string Separator(Previous().Lexeme);
				if (!(Check(ETokenKind::Identifier) || (Check(ETokenKind::Keyword) && (ParserDetail::IsTypeKeyword(Peek().Lexeme) || ParserDetail::IsContainerTypeKeyword(Peek().Lexeme)))))
				{
					ReportErrorAt(Peek(), "expected identifier after '.' or '::' in type name");
					return {};
				}
				Advance();
				TypeName += '.';
				TypeName += Previous().Lexeme;
				HostQualifiedTypeName += Separator;
				HostQualifiedTypeName += Previous().Lexeme;
			}
			const std::string_view StandardType =
			    StandardLibrary::ResolveTypeName(TypeName);
			if (!StandardType.empty())
			{
				TypeName.assign(StandardType);
			}
			else if (HostTypeNames.find(HostQualifiedTypeName) != HostTypeNames.end())
			{
				TypeName = std::move(HostQualifiedTypeName);
			}
			else if (bGlobalQualified)
			{
				// Keep the root marker on user types so later compiler passes cannot
				// accidentally reinterpret a bare `::T` relative to the active scope.
				TypeName = (bDeclarationOutline || IsUserTypeName(TypeName)) ? "::" + TypeName : std::string{};
			}
			else
			{
				TypeName = ResolveNamespacedTypeName(std::move(TypeName));
			}
			if (!bGlobalQualified && BindingStore)
			{
				std::size_t TypeScopeOrder = 0;
				for (const auto& Frame : TypeFrames)
				{
					if ((TypeName == Frame.Name || TypeName.rfind(Frame.Name + ".", 0) == 0) &&
					    Frame.AliasBoundary < BindingStore->AliasOrder.size())
					{
						TypeScopeOrder = std::max(TypeScopeOrder, BindingStore->AliasOrder[Frame.AliasBoundary]);
					}
				}
				if (BindingStore->IsRuntimeName(SourceRoot, LocalTypeAliasScopes, RuntimeBindingScopes,
				        TypeScopeOrder) &&
				    StandardType.empty() &&
				    (TypeName != SourceRoot || StandardLibrary::ResolveTypeName("std." + SourceRoot).empty()))
				{
					ReportErrorAt(Previous(), "a value name cannot be used as a type: " + SourceRoot);
					return {};
				}
			}
			const std::string_view LookupTypeName = TypeName.rfind("::", 0) == 0
			    ? std::string_view(TypeName).substr(2)
			    : std::string_view(TypeName);
			if (!bDeclarationOutline && !IsUserTypeName(LookupTypeName) && ActiveTypeParams.find(TypeName) == ActiveTypeParams.end() && !ParserDetail::IsTypeKeyword(TypeName) && !ParserDetail::IsContainerTypeKeyword(TypeName))
			{
				ReportErrorAt(Previous(), "expected type name");
				return {};
			}
			if ((bDeclarationOutline || IsUserTypeName(LookupTypeName) || ParserDetail::IsContainerTypeKeyword(TypeName)) && MatchSymbol("<"))
			{
				// Preserve the application until scoped lookup has chosen the declaration.
				if (MatchCloseGeneric())
				{
					return CompleteTypeName(TypeName + "<>", TypeBegin);
				}
				std::string GenericTypeName = TypeName + "<";
				bool bFirstArg = true;
				do
				{
					const std::string ArgType = ParseTemplateArgumentName();
					if (bHasError || ArgType.empty())
					{
						return {};
					}
					if (!bFirstArg)
					{
						GenericTypeName += ",";
					}
					GenericTypeName += ArgType;
					bFirstArg = false;
				}
				while (MatchSymbol(","));
				if (!MatchCloseGeneric())
				{
					ReportErrorAt(Peek(), "expected '>' after type argument");
					return {};
				}
				return CompleteTypeName(GenericTypeName + ">", TypeBegin);
			}
			return CompleteTypeName(TypeName, TypeBegin);
		}

		if (bGlobalQualified)
		{
			ReportErrorAt(Peek(), "expected type name after '::'");
		}
		else
		{
			ReportErrorAt(Peek(), "expected type name");
		}
		return {};
	}

	std::string VeroScriptParser::ParseExpressionTypeArgumentName(const bool bAllowReference)
	{
		++ExpressionTypeArgumentParseDepth;
		const std::string TypeName = bAllowReference ? ParseReturnTypeName() : ParseTemplateArgumentName();
		--ExpressionTypeArgumentParseDepth;
		return TypeName;
	}

	std::unique_ptr<FStmt> VeroScriptParser::ParseStatement()
	{
		const auto Begin = Current;
		const auto DeclarationContext = bDeclarationOutline ? CaptureDeclarationContext() : nullptr;
		auto Result = ParseStatementCore();
		if (Result && DeclarationContext)
		{
			Result->DeclarationContext = DeclarationContext;
		}
		if (Result && !RuntimeBindingScopes.empty())
		{
			if (Result->Kind == EStmtKind::VarDecl)
			{
				AddRuntimeBinding(static_cast<const FVarDeclStmt&>(*Result).Name);
			}
			else if (Result->Kind == EStmtKind::VarDeclGroup)
			{
				for (const auto& Variable : static_cast<const FVarDeclGroupStmt&>(*Result).Declarations)
				{
					AddRuntimeBinding(Variable->Name);
				}
			}
		}
		if (Result && Result->ScopeId.empty())
		{
			Result->ScopeId = BlockScopes.empty() ? FragmentScopeId : BlockScopes.back();
		}
		if (Result && !SourceText.empty() && Current > Begin)
		{
			Result->SourceRange = CaptureSourceRange(Begin, Current);
			if (DeclarationContext)
			{
				DeclarationContext->Range = Result->SourceRange;
			}
		}

		if (Result && bDeclarationOutline && Result->Kind != EStmtKind::VarDecl &&
		    Result->Kind != EStmtKind::VarDeclGroup && Current > Begin)
		{
			Result->DeferredSyntax = CaptureDeferredSyntax(Begin, Current);
		}

		return Result;
	}

	std::unique_ptr<FStmt> VeroScriptParser::ParseStatementCore()
	{
		const FSourceLocation Location = Peek().Location;
		if (!EnterStatementNesting(Location))
		{
			return nullptr;
		}
		struct FStatementDepthGuard
		{
			~FStatementDepthGuard()
			{
				Parser.LeaveStatementNesting();
			}

			VeroScriptParser& Parser;

		} DepthGuard{*this};

		if (MatchSymbol("{"))
		{
			--Current;
			std::unique_ptr<FBlockStmt> Block = ParseBlock();
			return Block;
		}

		if (CheckSymbol(";"))
		{
			ReportErrorAt(Peek(), "empty statements are not supported; use '{}' for an empty body");
			return nullptr;
		}

		if (LooksLikeAnnotationStart())
		{
			const auto Checkpoint = Current;
			const auto Pending = PendingGenericCloseCount;
			FDeclMeta Meta;
			TryParseAnnotations(Meta);
			if (bHasError)
			{
				return nullptr;
			}
			if (IsScopedTypeDeclaration() && Peek().Lexeme != "using" && Peek().Lexeme != "typedef")
			{
				return ParseLocalTypeDeclaration(std::move(Meta));
			}
			Current = Checkpoint;
			PendingGenericCloseCount = Pending;
		}
		if (IsScopedTypeDeclaration() && Peek().Lexeme != "using" && Peek().Lexeme != "typedef")
		{
			return ParseLocalTypeDeclaration();
		}

		if (Peek().Lexeme == "static" || ((Peek().Lexeme == "const" || Peek().Lexeme == "constexpr") && Current + 1 < Tokens.size() && Tokens[Current + 1].Lexeme == "static"))
		{
			const bool bConstPrefix = Peek().Lexeme != "static";
			const bool bConstexprPrefix = Peek().Lexeme == "constexpr";
			if (bConstPrefix)
			{
				Advance();
			}
			Advance();
			auto Declaration = ParseStatementCore();
			if (!Declaration) return nullptr;
			const auto Mark = [&](FVarDeclStmt& Variable)
			{
				Variable.bIsLocalStatic = true;
				Variable.bIsConst = Variable.bIsConst || bConstPrefix;
				Variable.bConstexpr = Variable.bConstexpr || bConstexprPrefix;
			};
			if (Declaration->Kind == EStmtKind::VarDecl)
			{
				Mark(static_cast<FVarDeclStmt&>(*Declaration));
			}
			else if (Declaration->Kind == EStmtKind::VarDeclGroup)
			{
				for (auto& Variable : static_cast<FVarDeclGroupStmt&>(*Declaration).Declarations)
				{
					Mark(*Variable);
				}
			}
			else
			{
				ReportErrorAt(Location, "static requires a local variable declaration");
				return nullptr;
			}
			return Declaration;
		}

		if (Check(ETokenKind::Keyword) && (Peek().Lexeme == "using" || Peek().Lexeme == "typedef"))
		{
			return ParseLocalTypeAliasStmt();
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "try")
		{
			Advance();
			if (!CheckSymbol("{"))
			{
				ReportErrorAt(Peek(), "expected '{' after 'try'");
				return nullptr;
			}
			auto Statement = std::make_unique<FTryStmt>();
			Statement->Location = Location;
			Statement->TryBlock = ParseBlock();
			if (!Statement->TryBlock)
			{
				return nullptr;
			}

			bool bSawCatchAll = false;
			while (Check(ETokenKind::Keyword) && Peek().Lexeme == "catch")
			{
				const FSourceLocation CatchLocation = Advance().Location;
				if (bSawCatchAll)
				{
					ReportErrorAt(Previous(), "catch-all handler must be last");
					return nullptr;
				}
				if (!MatchSymbol("("))
				{
					ReportErrorAt(Peek(), "expected '(' after 'catch'");
					return nullptr;
				}
				FCatchClause Clause;
				Clause.Location = CatchLocation;
				if (MatchSymbol("..."))
				{
					Clause.bCatchAll = true;
					bSawCatchAll = true;
				}
				else
				{
					Clause.bIsConst = MatchOptionalConst();
					Clause.bIsRef = MatchOptionalRef();
					Clause.TypeName = ParseTypeName();
					if (Clause.TypeName.empty())
					{
						ReportErrorAt(Peek(), "expected exception type in catch handler");
						return nullptr;
					}
					if (!MatchOptionalTrailingRefAmpersand(Clause.bIsRef))
					{
						return nullptr;
					}
					if (Check(ETokenKind::Identifier))
					{
						const FToken& NameToken = Advance();
						Clause.Name = NameToken.Lexeme;
						Clause.NameLocation = NameToken.Location;
					}
					else if (!CheckSymbol(")"))
					{
						ReportErrorAt(Peek(), "expected exception variable name");
						return nullptr;
					}
				}
				if (!MatchSymbol(")"))
				{
					ReportErrorAt(Peek(), "expected ')' after catch declaration");
					return nullptr;
				}
				if (!CheckSymbol("{"))
				{
					ReportErrorAt(Peek(), "expected '{' after catch declaration");
					return nullptr;
				}
				PushAliasScope();
				PushRuntimeScope();
				AddRuntimeBinding(Clause.Name);
				Clause.Body = ParseBlock();
				PopRuntimeScope();
				PopAliasScope();
				if (!Clause.Body)
				{
					return nullptr;
				}
				Statement->Catches.push_back(std::move(Clause));
			}

			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "finally")
			{
				Advance();
				if (!CheckSymbol("{"))
				{
					ReportErrorAt(Peek(), "expected '{' after 'finally'");
					return nullptr;
				}
				Statement->FinallyBlock = ParseBlock();
				if (!Statement->FinallyBlock)
				{
					return nullptr;
				}
			}
			if (Statement->Catches.empty() && !Statement->FinallyBlock)
			{
				ReportErrorAt(Peek(), "try statement requires catch or finally");
				return nullptr;
			}
			return Statement;
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "throw")
		{
			Advance();
			std::unique_ptr<FExpr> Value;
			if (!CheckSymbol(";"))
			{
				Value = ParseExpression();
				if (!Value)
				{
					return nullptr;
				}
			}
			if (!MatchSymbol(";"))
			{
				ReportErrorAt(Peek(), "expected ';' after throw");
				return nullptr;
			}
			auto Statement = std::make_unique<FThrowStmt>(std::move(Value));
			Statement->Location = Location;
			return Statement;
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "if")
		{
			Advance();
			const bool bConstexpr = Check(ETokenKind::Keyword) && Peek().Lexeme == "constexpr";
			if (bConstexpr)
			{
				Advance();
			}
			if (!MatchSymbol("("))
			{
				ReportErrorAt(Peek(), "expected '(' after 'if'");
				return nullptr;
			}
			std::unique_ptr<FExpr> Condition = ParseExpression();
			if (!Condition)
			{
				return nullptr;
			}
			if (!MatchSymbol(")"))
			{
				ReportErrorAt(Peek(), "expected ')' after condition");
				return nullptr;
			}

			std::unique_ptr<FBlockStmt> ThenBranch = ParseBlockOrStatement();
			if (!ThenBranch)
			{
				return nullptr;
			}

			std::unique_ptr<FBlockStmt> ElseBranch;
			if (Check(ETokenKind::Keyword) && Peek().Lexeme == "else")
			{
				Advance();
				ElseBranch = ParseBlockOrStatement();
				if (!ElseBranch)
				{
					return nullptr;
				}
			}

			auto Statement = std::make_unique<FIfStmt>(
			    std::move(Condition),
			    std::move(ThenBranch),
			    std::move(ElseBranch));
			Statement->bConstexpr = bConstexpr;
			Statement->Location = Location;
			return Statement;
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "while")
		{
			Advance();
			if (!MatchSymbol("("))
			{
				ReportErrorAt(Peek(), "expected '(' after 'while'");
				return nullptr;
			}
			std::unique_ptr<FExpr> Condition = ParseExpression();
			if (!Condition)
			{
				return nullptr;
			}
			if (!MatchSymbol(")"))
			{
				ReportErrorAt(Peek(), "expected ')' after condition");
				return nullptr;
			}

			std::unique_ptr<FBlockStmt> Body = ParseBlockOrStatement();
			if (!Body)
			{
				return nullptr;
			}

			auto Statement = std::make_unique<FWhileStmt>(std::move(Condition), std::move(Body));
			Statement->Location = Location;
			return Statement;
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "do")
		{
			return ParseDoWhileStatement(Location);
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "switch")
		{
			return ParseSwitchStatement(Location);
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "for")
		{
			Advance();
			PushAliasScope();
			PushRuntimeScope();
			struct FForScopeGuard
			{
				VeroScriptParser& Parser;
				~FForScopeGuard()
				{
					Parser.PopRuntimeScope();
					Parser.PopAliasScope();
				}
			} ForScopeGuard{*this};
			if (!MatchSymbol("("))
			{
				ReportErrorAt(Peek(), "expected '(' after 'for'");
				return nullptr;
			}

			const auto FinishDestructureForEach =
			    [&](std::string KeyType,
			        std::string KeyName,
			        const bool bKeyConst,
			        std::string ValueType,
			        std::string ValueName,
			        const bool bValueConst,
			        const bool bValueRef) -> std::unique_ptr<FStmt>
			{
				if (KeyName == ValueName)
				{
					ReportErrorAt(Previous(), "map destructure key and value names must differ");
					return nullptr;
				}
				std::unique_ptr<FExpr> Collection = ParseExpression();
				if (!Collection)
				{
					return nullptr;
				}
				if (!MatchSymbol(")"))
				{
					ReportErrorAt(Peek(), "expected ')' after range-for collection");
					return nullptr;
				}
				AddRuntimeBinding(KeyName);
				AddRuntimeBinding(ValueName);
				std::unique_ptr<FBlockStmt> Body = ParseBlockOrStatement();
				if (!Body)
				{
					return nullptr;
				}
				auto Statement = std::make_unique<FForEachStmt>(
				    std::move(ValueType),
				    std::move(ValueName),
				    std::move(Collection),
				    std::move(Body),
				    bValueConst,
				    bValueRef);
				Statement->bHasKeyBinding = true;
				Statement->KeyTypeName = std::move(KeyType);
				Statement->KeyName = std::move(KeyName);
				Statement->bKeyIsConst = bKeyConst;
				Statement->Location = Location;
				return Statement;
			};

			// for ((K k, V& v) : m); keys remain by-value copies. A leading
			// `(T)` can instead be a C-style cast in an ordinary for initializer,
			// so probe the complete type prefix before committing to destructuring.
			bool bStartsParenthesizedCast = false;
			if (CheckSymbol("("))
			{
				const std::size_t CastCheckpoint = Current;
				const int PendingCheckpoint = PendingGenericCloseCount;
				std::string CastTypeName;
				bStartsParenthesizedCast = TryMatchCStyleCastType(CastTypeName);
				Current = CastCheckpoint;
				PendingGenericCloseCount = PendingCheckpoint;
			}
			if (CheckSymbol("(") && !bStartsParenthesizedCast)
			{
				Advance(); // inner '('
				const bool bKeyConst = MatchOptionalConst();
				if (MatchOptionalRef())
				{
					ReportErrorAt(
					    Previous(),
					    "map destructure key cannot be ref (keys are by-value copies)");
					return nullptr;
				}
				std::string KeyType = ParseTypeName();
				if (bHasError || !MatchName())
				{
					if (!bHasError)
					{
						ReportErrorAt(Peek(), "expected map destructure key name");
					}
					return nullptr;
				}
				std::string KeyName(Previous().Lexeme);
				if (!ParseArraySuffix(KeyType))
				{
					return nullptr;
				}
				if (!MatchSymbol(","))
				{
					ReportErrorAt(Peek(), "expected ',' in map destructure binding");
					return nullptr;
				}
				const bool bValueConst = MatchOptionalConst();
				bool bValueRef = MatchOptionalRef();
				std::string ValueType = ParseTypeName();
				if (bHasError)
				{
					return nullptr;
				}
				if (!MatchOptionalTrailingRefAmpersand(bValueRef))
				{
					return nullptr;
				}
				if (!MatchName())
				{
					ReportErrorAt(Peek(), "expected map destructure value name");
					return nullptr;
				}
				std::string ValueName(Previous().Lexeme);
				if (!ParseArraySuffix(ValueType))
				{
					return nullptr;
				}
				if (!MatchSymbol(")"))
				{
					ReportErrorAt(Peek(), "expected ')' after map destructure bindings");
					return nullptr;
				}
				if (!MatchSymbol(":"))
				{
					ReportErrorAt(Peek(), "expected ':' after map destructure bindings");
					return nullptr;
				}
				return FinishDestructureForEach(
				    std::move(KeyType),
				    std::move(KeyName),
				    bKeyConst,
				    std::move(ValueType),
				    std::move(ValueName),
				    bValueConst,
				    bValueRef);
			}

			// for (auto& [k, v] : m) / for (auto [k, v] : m) / for (ref auto [k, v] : m)
			{
				std::size_t Probe = Current;
				if (Probe < Tokens.size() && Tokens[Probe].Kind == ETokenKind::Keyword && Tokens[Probe].Lexeme == "const")
				{
					++Probe;
				}
				if (Probe < Tokens.size() && ((Tokens[Probe].Kind == ETokenKind::Keyword && Tokens[Probe].Lexeme == "ref") || (Tokens[Probe].Kind == ETokenKind::Symbol && Tokens[Probe].Lexeme == "&")))
				{
					++Probe;
				}
				const bool bProbeAuto = Probe < Tokens.size() && Tokens[Probe].Kind == ETokenKind::Keyword && Tokens[Probe].Lexeme == "auto";
				if (bProbeAuto)
				{
					++Probe;
					// C++-style trailing &: auto& [k, v]
					if (Probe < Tokens.size() && Tokens[Probe].Kind == ETokenKind::Symbol && Tokens[Probe].Lexeme == "&")
					{
						++Probe;
					}
					const bool bProbeBracket = Probe < Tokens.size() && Tokens[Probe].Kind == ETokenKind::Symbol && Tokens[Probe].Lexeme == "[";
					if (bProbeBracket)
					{
						const bool bValueConst = MatchOptionalConst();
						bool bValueRef = MatchOptionalRef();
						Advance(); // auto
						if (!MatchOptionalTrailingRefAmpersand(bValueRef))
						{
							return nullptr;
						}
						if (!MatchSymbol("["))
						{
							ReportErrorAt(Peek(), "expected '[' in structured binding");
							return nullptr;
						}
						if (!MatchName())
						{
							ReportErrorAt(Peek(), "expected structured binding key name");
							return nullptr;
						}
						std::string KeyName(Previous().Lexeme);
						if (!MatchSymbol(","))
						{
							ReportErrorAt(Peek(), "expected ',' in structured binding");
							return nullptr;
						}
						if (!MatchName())
						{
							ReportErrorAt(Peek(), "expected structured binding value name");
							return nullptr;
						}
						std::string ValueName(Previous().Lexeme);
						if (MatchSymbol(","))
						{
							ReportErrorAt(Previous(), "map structured binding expects exactly two names");
							return nullptr;
						}
						if (!MatchSymbol("]"))
						{
							ReportErrorAt(Peek(), "expected ']' after structured binding names");
							return nullptr;
						}
						if (!MatchSymbol(":"))
						{
							ReportErrorAt(Peek(), "expected ':' after structured binding");
							return nullptr;
						}
						return FinishDestructureForEach(
						    "auto",
						    std::move(KeyName),
						    bValueConst,
						    "auto",
						    std::move(ValueName),
						    bValueConst,
						    bValueRef);
					}
				}
			}

			// Range-for / typed for-init: Type | Type& | const Type& |
			// ref/& Type | const ref/& Type.
			bool bLocalStatic = false;
			bool bStaticConstPrefix = false;
			bool bStaticConstexprPrefix = false;
			if (Peek().Lexeme == "static")
			{
				Advance();
				bLocalStatic = true;
			}
			else if ((Peek().Lexeme == "const" || Peek().Lexeme == "constexpr") && Current + 1u < Tokens.size() && Tokens[Current + 1u].Lexeme == "static")
			{
				bStaticConstexprPrefix = Peek().Lexeme == "constexpr";
				bStaticConstPrefix = true;
				Advance();
				Advance();
				bLocalStatic = true;
			}
			if (CheckTypeName() || (Check(ETokenKind::Keyword) && Peek().Lexeme == "const") || (Check(ETokenKind::Keyword) && Peek().Lexeme == "ref") || CheckSymbol("&"))
			{
				const bool bLeadingConst =
				    Check(ETokenKind::Keyword) && Peek().Lexeme == "const";
				std::size_t SoftTypeStart = Current;
				if (bLeadingConst)
				{
					++SoftTypeStart;
				}
				if (SoftTypeStart < Tokens.size() && ((Tokens[SoftTypeStart].Kind == ETokenKind::Keyword && Tokens[SoftTypeStart].Lexeme == "ref") || (Tokens[SoftTypeStart].Kind == ETokenKind::Symbol && Tokens[SoftTypeStart].Lexeme == "&")))
				{
					++SoftTypeStart;
				}
				const bool bSoftContainerType =
				    SoftTypeStart < Tokens.size() && Tokens[SoftTypeStart].Kind == ETokenKind::Keyword && ParserDetail::IsContainerTypeKeyword(Tokens[SoftTypeStart].Lexeme);
				if (!(bSoftContainerType && !LooksLikeContainerTypeApplication()))
				{
					const std::size_t Checkpoint = Current;
					const int PendingCheckpoint = PendingGenericCloseCount;
					bool bConstexpr = false;
					const bool bIsConst = MatchConstexprSpecifiers(bConstexpr) || bStaticConstPrefix;
					bConstexpr = bConstexpr || bStaticConstexprPrefix;
					bool bIsRef = MatchOptionalRef();
					std::string TypeName = ParseTypeName();
					if (bHasError)
					{
						const bool bAllowSoftFallback =
						    bSoftContainerType && (Error.Message.find("expected type name") != std::string::npos || Error.Message.find("expected '<' after") != std::string::npos);
						if (!bAllowSoftFallback)
						{
							return nullptr;
						}
						Current = Checkpoint;
						PendingGenericCloseCount = PendingCheckpoint;
						bHasError = false;
						Error = {};
					}
					else if (!MatchOptionalTrailingRefAmpersand(bIsRef))
					{
						return nullptr;
					}
					else if (!MatchName())
					{
						if (!bSoftContainerType)
						{
							ReportErrorAt(Peek(), "expected variable name");
							return nullptr;
						}
						Current = Checkpoint;
						PendingGenericCloseCount = PendingCheckpoint;
						bHasError = false;
						Error = {};
					}
					else
					{
						std::string Name(Previous().Lexeme);
						const std::string DeclaredTypeName = TypeName;
						if (!ParseArraySuffix(TypeName))
						{
							return nullptr;
						}
						const bool bFirstDeclaratorHasArraySuffix = TypeName != DeclaredTypeName;
						if (MatchSymbol(":"))
						{
							if (bLocalStatic)
							{
								ReportErrorAt(Peek(), "range-for bindings cannot be static");
								return nullptr;
							}
							std::unique_ptr<FExpr> Collection = ParseExpression();
							if (!Collection)
							{
								return nullptr;
							}
							if (!MatchSymbol(")"))
							{
								ReportErrorAt(Peek(), "expected ')' after range-for collection");
								return nullptr;
							}
							AddRuntimeBinding(Name);
							std::unique_ptr<FBlockStmt> Body = ParseBlockOrStatement();
							if (!Body)
							{
								return nullptr;
							}
							auto Statement = std::make_unique<FForEachStmt>(
							    std::move(TypeName),
							    std::move(Name),
							    std::move(Collection),
							    std::move(Body),
							    bIsConst,
							    bIsRef);
							Statement->Location = Location;
							return Statement;
						}

						// C-style for with typed init.
						std::unique_ptr<FExpr> Initializer =
						    (bConstexpr && !(CheckSymbol("=") || CheckSymbol("(") || CheckSymbol("{"))
						            ? (ReportErrorAt(Peek(), "constexpr variable requires an initializer"), std::unique_ptr<FExpr>{})
						            : ParseTypedVarInitializer(TypeName, Runtime::IsNativeArrayTypeName(TypeName)));
						AddRuntimeBinding(Name);
						if (bHasError)
						{
							return nullptr;
						}
						auto FirstDeclaration = std::make_unique<FVarDeclStmt>(
						    TypeName, std::move(Name), std::move(Initializer));
						FirstDeclaration->bIsConst = bIsConst;
						FirstDeclaration->bIsLocalStatic = bLocalStatic;
						FirstDeclaration->bConstexpr = bConstexpr;
						FirstDeclaration->bIsRef = bIsRef;
						FirstDeclaration->Location = Location;
						std::unique_ptr<FStmt> Init;
						if (!CheckSymbol(","))
						{
							if (!MatchSymbol(";"))
							{
								ReportErrorAt(Peek(), "expected ';' after for-init variable declaration");
								return nullptr;
							}
							Init = std::move(FirstDeclaration);
						}
						else
						{
							Advance(); // comma after the first declaration
							if (TypeName == "auto")
							{
								ReportErrorAt(
								    Previous(),
								    "auto declarations cannot declare multiple variables");
								return nullptr;
							}
							if (bFirstDeclaratorHasArraySuffix)
							{
								ReportErrorAt(
								    Previous(),
								    "multi-variable declarations require a shared array type prefix");
								return nullptr;
							}
							std::vector<std::unique_ptr<FVarDeclStmt>> Declarations;
							Declarations.push_back(std::move(FirstDeclaration));
							for (;;)
							{
								if (!MatchName())
								{
									ReportErrorAt(Peek(), "expected variable name after ','");
									return nullptr;
								}
								const FSourceLocation DeclarationLocation = Previous().Location;
								const std::string DeclarationName(Previous().Lexeme);
								if (CheckSymbol("["))
								{
									ReportErrorAt(
									    Peek(),
									    "multi-variable declarations require a shared array type prefix");
									return nullptr;
								}
								std::unique_ptr<FExpr> NextInitializer =
								    (bConstexpr && !(CheckSymbol("=") || CheckSymbol("(") || CheckSymbol("{"))
								            ? (ReportErrorAt(Peek(), "constexpr variable requires an initializer"), std::unique_ptr<FExpr>{})
								            : ParseTypedVarInitializer(TypeName, Runtime::IsNativeArrayTypeName(TypeName)));
								AddRuntimeBinding(DeclarationName);
								if (bHasError)
								{
									return nullptr;
								}
								auto Declaration = std::make_unique<FVarDeclStmt>(
								    TypeName, DeclarationName, std::move(NextInitializer));
								Declaration->bIsConst = bIsConst;
								Declaration->bIsLocalStatic = bLocalStatic;
								Declaration->bConstexpr = bConstexpr;
								Declaration->bIsRef = bIsRef;
								Declaration->Location = DeclarationLocation;
								Declarations.push_back(std::move(Declaration));
								if (!MatchSymbol(","))
								{
									break;
								}
							}
							if (!MatchSymbol(";"))
							{
								ReportErrorAt(Peek(), "expected ';' after for-init variable declaration");
								return nullptr;
							}
							auto Group = std::make_unique<FVarDeclGroupStmt>(std::move(Declarations));
							Group->Location = Location;
							Init = std::move(Group);
						}

						std::unique_ptr<FExpr> Condition;
						if (!CheckSymbol(";"))
						{
							Condition = ParseExpression();
							if (!Condition)
							{
								return nullptr;
							}
						}
						if (!MatchSymbol(";"))
						{
							ReportErrorAt(Peek(), "expected ';' after for condition");
							return nullptr;
						}

						std::vector<std::unique_ptr<FExpr>> StepExpressions;
						if (!CheckSymbol(")"))
						{
							StepExpressions = ParseForExpressionList();
							if (bHasError)
							{
								return nullptr;
							}
						}
						if (!MatchSymbol(")"))
						{
							ReportErrorAt(Peek(), "expected ')' after for clauses");
							return nullptr;
						}
						std::unique_ptr<FExpr> Step;
						if (!StepExpressions.empty())
						{
							Step = std::move(StepExpressions.front());
							StepExpressions.erase(StepExpressions.begin());
						}

						std::unique_ptr<FBlockStmt> Body = ParseBlockOrStatement();
						if (!Body)
						{
							return nullptr;
						}

						auto Statement = std::make_unique<FForStmt>(
						    std::move(Init),
						    std::vector<std::unique_ptr<FExpr>>{},
						    std::move(Condition),
						    std::move(Step),
						    std::move(StepExpressions),
						    std::move(Body));
						Statement->Location = Location;
						return Statement;
					}
				}
			}

			std::unique_ptr<FStmt> Init;
			std::vector<std::unique_ptr<FExpr>> InitExpressions;
			if (!CheckSymbol(";"))
			{
				InitExpressions = ParseForExpressionList();
				if (bHasError)
				{
					return nullptr;
				}
				if (!MatchSymbol(";"))
				{
					ReportErrorAt(Peek(), "expected ';' after for-init expression");
					return nullptr;
				}
			}
			else
			{
				Advance();
			}
			if (!InitExpressions.empty())
			{
				auto InitStatement = std::make_unique<FExprStmt>(
				    std::move(InitExpressions.front()));
				InitStatement->Location = Location;
				Init = std::move(InitStatement);
				InitExpressions.erase(InitExpressions.begin());
			}

			std::unique_ptr<FExpr> Condition;
			if (!CheckSymbol(";"))
			{
				Condition = ParseExpression();
				if (!Condition)
				{
					return nullptr;
				}
			}
			if (!MatchSymbol(";"))
			{
				ReportErrorAt(Peek(), "expected ';' after for condition");
				return nullptr;
			}

			std::vector<std::unique_ptr<FExpr>> StepExpressions;
			if (!CheckSymbol(")"))
			{
				StepExpressions = ParseForExpressionList();
				if (bHasError)
				{
					return nullptr;
				}
			}
			if (!MatchSymbol(")"))
			{
				ReportErrorAt(Peek(), "expected ')' after for clauses");
				return nullptr;
			}
			std::unique_ptr<FExpr> Step;
			if (!StepExpressions.empty())
			{
				Step = std::move(StepExpressions.front());
				StepExpressions.erase(StepExpressions.begin());
			}

			std::unique_ptr<FBlockStmt> Body = ParseBlockOrStatement();
			if (!Body)
			{
				return nullptr;
			}

			auto Statement = std::make_unique<FForStmt>(
			    std::move(Init),
			    std::move(InitExpressions),
			    std::move(Condition),
			    std::move(Step),
			    std::move(StepExpressions),
			    std::move(Body));
			Statement->Location = Location;
			return Statement;
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "break")
		{
			Advance();
			if (!MatchSymbol(";"))
			{
				ReportErrorAt(Peek(), "expected ';' after 'break'");
				return nullptr;
			}
			auto Statement = std::make_unique<FBreakStmt>();
			Statement->Location = Location;
			return Statement;
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "continue")
		{
			Advance();
			if (!MatchSymbol(";"))
			{
				ReportErrorAt(Peek(), "expected ';' after 'continue'");
				return nullptr;
			}
			auto Statement = std::make_unique<FContinueStmt>();
			Statement->Location = Location;
			return Statement;
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "fallthrough")
		{
			Advance();
			if (!MatchSymbol(";"))
			{
				ReportErrorAt(Peek(), "expected ';' after 'fallthrough'");
				return nullptr;
			}
			auto Statement = std::make_unique<FFallthroughStmt>();
			Statement->Location = Location;
			return Statement;
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "goto")
		{
			Advance();
			if (!Match(ETokenKind::Identifier))
			{
				ReportErrorAt(Peek(), "expected label name after 'goto'");
				return nullptr;
			}
			const std::string LabelName(Previous().Lexeme);
			if (!MatchSymbol(";"))
			{
				ReportErrorAt(Peek(), "expected ';' after goto");
				return nullptr;
			}
			auto Statement = std::make_unique<FGotoStmt>(LabelName);
			Statement->Location = Location;
			return Statement;
		}

		if (Check(ETokenKind::Identifier) && Current + 1 < Tokens.size() && Tokens[Current + 1].Kind == ETokenKind::Symbol && Tokens[Current + 1].Lexeme == ":")
		{
			const std::string LabelName(Peek().Lexeme);
			Advance();
			Advance(); // ':'
			auto Statement = std::make_unique<FLabelStmt>(LabelName);
			Statement->Location = Location;
			return Statement;
		}

		if (Check(ETokenKind::Keyword) && Peek().Lexeme == "return")
		{
			Advance();
			std::unique_ptr<FExpr> Value;
			if (!CheckSymbol(";"))
			{
				Value = ParseExpression();
				if (!Value)
				{
					return nullptr;
				}
			}
			if (!MatchSymbol(";"))
			{
				ReportErrorAt(Peek(), "expected ';' after return");
				return nullptr;
			}

			auto Statement = std::make_unique<FReturnStmt>(std::move(Value));
			Statement->Location = Location;
			return Statement;
		}

		if (CheckTypeName())
		{
			// User type names can also be values (locals / this-fields) or enum qualifiers.
			// Variable declaration form is only `TypeName Name ...` (Name may be a soft container keyword).
			// Soft container keywords may also start comparisons (`map < x`); fall back to expression.
			const bool bLeadingConst =
			    Check(ETokenKind::Keyword) && Peek().Lexeme == "const";
			const bool bUserTypeExpression =
			    !bLeadingConst && Check(ETokenKind::Identifier) && IsUserTypeName(Peek().Lexeme) && !LooksLikeTypedVarDecl() && !ParserDetail::IsNameToken(PeekNext()) && !(PeekNext().Kind == ETokenKind::Symbol && PeekNext().Lexeme == "<");
			if (!bUserTypeExpression)
			{
				const bool bSoftContainerType =
				    (!bLeadingConst && Check(ETokenKind::Keyword) && ParserDetail::IsContainerTypeKeyword(Peek().Lexeme)) || (bLeadingConst && Current + 1 < Tokens.size() && Tokens[Current + 1].Kind == ETokenKind::Keyword && ParserDetail::IsContainerTypeKeyword(Tokens[Current + 1].Lexeme));
				const bool bSoftFnType =
				    !bLeadingConst && Check(ETokenKind::Keyword) && (Peek().Lexeme == "fn" || Peek().Lexeme == "function");
				const bool bSoftTypeFallback = bSoftContainerType || bSoftFnType;
				if (bSoftContainerType && !LooksLikeContainerTypeApplication())
				{
					// Comparison / shift-like uses of soft names: `set < y`, `map < x`.
				}
				else
				{
					const std::size_t Checkpoint = Current;
					const int PendingCheckpoint = PendingGenericCloseCount;
					bool bConstexpr = false;
					const bool bIsConst = MatchConstexprSpecifiers(bConstexpr);
					bool bIsRef = MatchOptionalRef();
					std::string TypeName = ParseTypeName();
					if (bHasError)
					{
						const bool bAllowSoftFallback =
						    bSoftTypeFallback && (Error.Message.find("expected type name") != std::string::npos || Error.Message.find("expected '<' after") != std::string::npos || Error.Message.find("expected ')' after function type") != std::string::npos || Error.Message.find("expected '->' after function type") != std::string::npos);
						if (!bAllowSoftFallback)
						{
							return nullptr;
						}
						Current = Checkpoint;
						PendingGenericCloseCount = PendingCheckpoint;
						bHasError = false;
						Error = {};
					}
					else if (!MatchOptionalTrailingRefAmpersand(bIsRef))
					{
						return nullptr;
					}
					else if (!MatchName())
					{
						// Not `Type Name` — may be a constructor call / type expression (`N.C()`).
						Current = Checkpoint;
						PendingGenericCloseCount = PendingCheckpoint;
						bHasError = false;
						Error = {};
					}
					else
					{
						std::string Name(Previous().Lexeme);
						const std::string DeclaredTypeName = TypeName;
						if (!ParseArraySuffix(TypeName))
						{
							return nullptr;
						}
						const bool bFirstDeclaratorHasArraySuffix = TypeName != DeclaredTypeName;
						std::unique_ptr<FExpr> Initializer =
						    (bConstexpr && !(CheckSymbol("=") || CheckSymbol("(") || CheckSymbol("{"))
						            ? (ReportErrorAt(Peek(), "constexpr variable requires an initializer"), std::unique_ptr<FExpr>{})
						            : ParseTypedVarInitializer(TypeName, Runtime::IsNativeArrayTypeName(TypeName)));
						AddRuntimeBinding(Name);
						if (bHasError)
						{
							return nullptr;
						}
						auto FirstDeclaration = std::make_unique<FVarDeclStmt>(
						    TypeName,
						    std::move(Name),
						    std::move(Initializer));
						FirstDeclaration->bIsConst = bIsConst;
						FirstDeclaration->bConstexpr = bConstexpr;
						FirstDeclaration->bIsRef = bIsRef;
						FirstDeclaration->Location = Location;
						if (!CheckSymbol(","))
						{
							if (!MatchSymbol(";"))
							{
								ReportErrorAt(Peek(), "expected ';' after variable declaration");
								return nullptr;
							}
							return FirstDeclaration;
						}
						Advance(); // comma after the first declaration
						if (TypeName == "auto")
						{
							ReportErrorAt(
							    Previous(),
							    "auto declarations cannot declare multiple variables");
							return nullptr;
						}
						if (bFirstDeclaratorHasArraySuffix)
						{
							ReportErrorAt(
							    Previous(),
							    "multi-variable declarations require a shared array type prefix");
							return nullptr;
						}

						std::vector<std::unique_ptr<FVarDeclStmt>> Declarations;
						Declarations.push_back(std::move(FirstDeclaration));
						for (;;)
						{
							if (!MatchName())
							{
								ReportErrorAt(Peek(), "expected variable name after ','");
								return nullptr;
							}
							const FSourceLocation DeclarationLocation = Previous().Location;
							const std::string DeclarationName(Previous().Lexeme);
							if (CheckSymbol("["))
							{
								ReportErrorAt(
								    Peek(),
								    "multi-variable declarations require a shared array type prefix");
								return nullptr;
							}
							std::unique_ptr<FExpr> NextInitializer =
							    (bConstexpr && !(CheckSymbol("=") || CheckSymbol("(") || CheckSymbol("{"))
							            ? (ReportErrorAt(Peek(), "constexpr variable requires an initializer"), std::unique_ptr<FExpr>{})
							            : ParseTypedVarInitializer(TypeName, Runtime::IsNativeArrayTypeName(TypeName)));
							AddRuntimeBinding(DeclarationName);
							if (bHasError)
							{
								return nullptr;
							}
							auto Declaration = std::make_unique<FVarDeclStmt>(
							    TypeName,
							    DeclarationName,
							    std::move(NextInitializer));
							Declaration->bIsConst = bIsConst;
							Declaration->bConstexpr = bConstexpr;
							Declaration->bIsRef = bIsRef;
							Declaration->Location = DeclarationLocation;
							Declarations.push_back(std::move(Declaration));
							if (!MatchSymbol(","))
							{
								break;
							}
						}
						if (!MatchSymbol(";"))
						{
							ReportErrorAt(Peek(), "expected ';' after variable declaration");
							return nullptr;
						}
						auto Statement = std::make_unique<FVarDeclGroupStmt>(std::move(Declarations));
						Statement->Location = Location;
						return Statement;
					}
				}
			}
		}

		// `(void) expression;` is an explicit result-discard statement. The
		// expression still executes exactly once and uses the ordinary expression
		// statement lowering, so side effects and runtime failures are preserved.
		const bool bExplicitDiscard = MatchExplicitDiscardPrefix();
		std::unique_ptr<FExpr> Expression = bExplicitDiscard
		    ? ParseUnary()
		    : ParseExpression();
		if (!Expression)
		{
			return nullptr;
		}
		if (CheckSymbol(","))
		{
			ReportErrorAt(Peek(), "comma operator is not supported outside classic for clauses");
			return nullptr;
		}
		if (!MatchSymbol(";"))
		{
			ReportErrorAt(Peek(), "expected ';' after expression");
			return nullptr;
		}

		auto Statement = std::make_unique<FExprStmt>(std::move(Expression));
		Statement->Location = Location;
		return Statement;
	}

	std::unique_ptr<FBlockStmt> VeroScriptParser::ParseBlock()
	{
		const auto BlockBegin = Current;
		const FSourceLocation Location = Peek().Location;
		if (!MatchSymbol("{"))
		{
			ReportErrorAt(Peek(), "expected '{'");
			return nullptr;
		}

		auto Block = std::make_unique<FBlockStmt>();
		Block->Location = Location;

		const std::string ParentScope = FragmentScopeId.empty() ? std::string(FileLabel) : FragmentScopeId;
		const std::string Position = std::to_string(Location.Line) + ":" + std::to_string(Location.Column);
		const std::string Scope =
		    RestoredBlockScope.empty()
		    ? TypeScopeIdentity(BlockScopes.empty() ? ParentScope : BlockScopes.back(), "block", Position)
		    : std::exchange(RestoredBlockScope, {});
		Block->ScopeId = Scope;
		Block->ParentScopeId = BlockScopes.empty() ? ParentScope : BlockScopes.back();
		if (BlockScopes.empty())
		{
			Block->CallableOwner = FunctionOwner;
		}
		BlockScopes.push_back(Scope);
		PushRuntimeScope();
		struct FBlockScopeGuard
		{
			std::vector<std::string>& Scopes;
			VeroScriptParser& Parser;
			~FBlockScopeGuard()
			{
				Scopes.pop_back();
				Parser.PopRuntimeScope();
			}
		} ScopeGuard{BlockScopes, *this};
		const auto BodyContext = CaptureDeclarationContext();
		Block->DeclarationContext = BodyContext;
		Block->BodyContext = BodyContext;
		PushAliasScope();
		const auto& GeneratedBindings = GetGeneratedBlockBindings(Scope);
		std::unordered_map<std::size_t, std::vector<const FGeneratedBlockTypeBinding*>> BindingsAt;
		for (const auto* Binding : GeneratedBindings)
		{
			BindingsAt[Binding->bAtEnd ? std::numeric_limits<std::size_t>::max() : Binding->Anchor.Begin]
			    .push_back(Binding);
		}
		const auto Inject = [&](const bool AtEnd)
		{
			if (GeneratedBindings.empty())
			{
				return true;
			}
			const auto Position = CaptureSourceRange(Current, Current + 1);
			const auto Found = BindingsAt.find(AtEnd ? std::numeric_limits<std::size_t>::max() : Position.Begin);
			if (Found == BindingsAt.end())
			{
				return true;
			}
			for (const auto* Binding : Found->second)
			{
				if (Binding->bAtEnd != AtEnd || Binding->Anchor.SourceId != Position.SourceId ||
				    (!AtEnd && Binding->Anchor.Begin != Position.Begin))
				{
					continue;
				}
				if (!AddAliasBinding(Binding->Name, Binding->TypeName))
				{
					ReportErrorAt(Peek(),
					    "generated local type conflicts with an existing name: " + Binding->Name);
					return false;
				}
				auto Alias = std::make_unique<FTypeAliasStmt>(Binding->Name, Binding->TypeName);
				Alias->bTypeDefinition = true;
				Alias->Location = Peek().Location;
				Alias->ScopeId = Scope;
				Alias->SourceRange = Binding->Metadata.SourceRange;
				Alias->DeclMeta = Binding->Metadata;
				Block->Statements.push_back(std::move(Alias));
			}
			return true;
		};
		while (!IsAtEnd() && !CheckSymbol("}"))
		{
			if (!Inject(false))
			{
				PopAliasScope();
				return nullptr;
			}
			std::unique_ptr<FStmt> Statement = ParseStatement();
			if (!Statement)
			{
				PopAliasScope();
				return nullptr;
			}
			Block->Statements.push_back(std::move(Statement));
		}

		if (!IsAtEnd() && !Inject(true))
		{
			PopAliasScope();
			return nullptr;
		}
		if (bDeclarationOutline)
		{
			Block->EndContext = CaptureDeclarationContext();
		}
		if (!MatchSymbol("}"))
		{
			ReportErrorAt(Peek(), "expected '}'");
			PopAliasScope();
			return nullptr;
		}

		PopAliasScope();
		if (!SourceText.empty())
		{
			Block->SourceRange = CaptureSourceRange(BlockBegin, Current);
		}
		if (!SourceText.empty())
		{
			BodyContext->Range = Block->SourceRange;
			BodyContext->Source =
			    std::string(SourceText.substr(Block->SourceRange.Begin - SourceDocumentOffset,
			        Block->SourceRange.End - Block->SourceRange.Begin));
			if (bDeclarationOutline)
			{
				Block->DeferredSyntax = BodyContext;
			}
		}

		return Block;
	}

	std::unique_ptr<FBlockStmt> VeroScriptParser::ParseBlockOrStatement()
	{
		if (CheckSymbol("{"))
		{
			return ParseBlock();
		}
		if (IsScopedTypeDeclaration() && Peek().Lexeme != "using" && Peek().Lexeme != "typedef")
		{
			ReportErrorAt(Peek(), "a local type declaration requires a braced block");
			return nullptr;
		}

		PushAliasScope();
		PushRuntimeScope();
		const auto Location = Peek().Location;
		const std::string ParentScope = BlockScopes.empty() ? (FragmentScopeId.empty() ? std::string(FileLabel) : FragmentScopeId) : BlockScopes.back();
		const auto Scope = TypeScopeIdentity(ParentScope, "statement", std::to_string(Location.Line) + ":" + std::to_string(Location.Column));
		BlockScopes.push_back(Scope);
		std::unique_ptr<FStmt> Statement = ParseStatement();
		BlockScopes.pop_back();
		PopAliasScope();
		PopRuntimeScope();
		if (!Statement)
		{
			return nullptr;
		}

		auto Block = std::make_unique<FBlockStmt>();
		Block->Location = Location;
		Block->ScopeId = Scope;
		Block->ParentScopeId = ParentScope;
		Block->SourceRange = Statement->SourceRange;
		Block->Statements.push_back(std::move(Statement));
		return Block;
	}

	std::unique_ptr<FStmt> VeroScriptParser::ParseDoWhileStatement(
	    const FSourceLocation& Location)
	{
		Advance();
		std::unique_ptr<FBlockStmt> Body = ParseBlockOrStatement();
		if (!Body)
		{
			return nullptr;
		}
		if (!Check(ETokenKind::Keyword) || Peek().Lexeme != "while")
		{
			ReportErrorAt(Peek(), "expected 'while' after do-loop body");
			return nullptr;
		}
		Advance();
		if (!MatchSymbol("("))
		{
			ReportErrorAt(Peek(), "expected '(' after 'while' in do-loop");
			return nullptr;
		}
		std::unique_ptr<FExpr> Condition = ParseExpression();
		if (!Condition)
		{
			return nullptr;
		}
		if (!MatchSymbol(")"))
		{
			ReportErrorAt(Peek(), "expected ')' after do-loop condition");
			return nullptr;
		}
		if (!MatchSymbol(";"))
		{
			ReportErrorAt(Peek(), "expected ';' after do-loop");
			return nullptr;
		}
		auto Statement = std::make_unique<FDoWhileStmt>(
		    std::move(Body), std::move(Condition));
		Statement->Location = Location;
		return Statement;
	}

	std::vector<std::unique_ptr<FExpr>> VeroScriptParser::ParseForExpressionList()
	{
		std::vector<std::unique_ptr<FExpr>> Expressions;
		for (;;)
		{
			const bool bExplicitDiscard = MatchExplicitDiscardPrefix();
			std::unique_ptr<FExpr> Expression = bExplicitDiscard
			    ? ParseUnary()
			    : ParseExpression();
			if (!Expression)
			{
				Expressions.clear();
				return Expressions;
			}
			Expressions.push_back(std::move(Expression));
			if (!MatchSymbol(","))
			{
				return Expressions;
			}
			if (CheckSymbol(";") || CheckSymbol(")"))
			{
				ReportErrorAt(Peek(), "expected expression after ',' in for clause");
				Expressions.clear();
				return Expressions;
			}
		}
	}

	std::unique_ptr<FStmt> VeroScriptParser::ParseSwitchStatement(const FSourceLocation& Location)
	{
		Advance(); // switch
		if (!MatchSymbol("("))
		{
			ReportErrorAt(Peek(), "expected '(' after 'switch'");
			return nullptr;
		}
		std::unique_ptr<FExpr> Discriminant = ParseExpression();
		if (!Discriminant)
		{
			return nullptr;
		}
		if (!MatchSymbol(")"))
		{
			ReportErrorAt(Peek(), "expected ')' after switch discriminant");
			return nullptr;
		}
		if (!MatchSymbol("{"))
		{
			ReportErrorAt(Peek(), "expected '{' after switch");
			return nullptr;
		}
		const auto Brace = Previous().Location;
		const std::string Parent = BlockScopes.empty()
		    ? (FragmentScopeId.empty() ? std::string(FileLabel) : FragmentScopeId)
		    : BlockScopes.back();
		const std::string Scope =
		    TypeScopeIdentity(Parent, "block", std::to_string(Brace.Line) + ":" + std::to_string(Brace.Column));
		BlockScopes.push_back(Scope);
		PushAliasScope();
		PushRuntimeScope();
		struct FScopeGuard
		{
			std::vector<std::string>& Blocks;
			VeroScriptParser& Parser;
			~FScopeGuard()
			{
				Blocks.pop_back();
				Parser.PopAliasScope();
				Parser.PopRuntimeScope();
			}
		} Guard{BlockScopes, *this};

		std::vector<FSwitchCase> Cases;
		bool bSawDefault = false;
		while (!IsAtEnd() && !CheckSymbol("}"))
		{
			FSwitchCase Case;
			bool bGotLabel = false;
			while (Check(ETokenKind::Keyword) && (Peek().Lexeme == "case" || Peek().Lexeme == "default"))
			{
				bGotLabel = true;
				if (Peek().Lexeme == "default")
				{
					Advance();
					if (bSawDefault || Case.bIsDefault)
					{
						ReportErrorAt(Previous(), "duplicate default in switch");
						return nullptr;
					}
					Case.bIsDefault = true;
					bSawDefault = true;
					if (!MatchSymbol(":"))
					{
						ReportErrorAt(Peek(), "expected ':' after default");
						return nullptr;
					}
					continue;
				}

				Advance(); // case
				std::unique_ptr<FExpr> Label =
				    bDeclarationOutline ? ParseDeferredExpression(true) : ParseExpression();
				if (!Label)
				{
					return nullptr;
				}
				if (!MatchSymbol(":"))
				{
					ReportErrorAt(Peek(), "expected ':' after case label");
					return nullptr;
				}
				Case.Labels.push_back(std::move(Label));
			}

			if (!bGotLabel)
			{
				ReportErrorAt(Peek(), "expected 'case' or 'default' in switch");
				return nullptr;
			}

			auto Body = std::make_unique<FBlockStmt>();
			Body->Location = Peek().Location;
			while (!IsAtEnd() && !CheckSymbol("}") && !(Check(ETokenKind::Keyword) && (Peek().Lexeme == "case" || Peek().Lexeme == "default")))
			{
				std::unique_ptr<FStmt> Statement = ParseStatement();
				if (!Statement)
				{
					return nullptr;
				}
				Body->Statements.push_back(std::move(Statement));
			}
			Case.Body = std::move(Body);
			Cases.push_back(std::move(Case));
		}

		if (!MatchSymbol("}"))
		{
			ReportErrorAt(Peek(), "expected '}' after switch");
			return nullptr;
		}

		auto Statement = std::make_unique<FSwitchStmt>(std::move(Discriminant), std::move(Cases));
		Statement->ScopeId = Scope;
		Statement->Location = Location;
		return Statement;
	}

	std::string VeroScriptParser::CompleteTypeName(std::string TypeName, const std::size_t TypeBegin)
	{
		while ((CheckSymbol("::") || CheckSymbol(".")) && Current + 1 < Tokens.size() &&
		    Tokens[Current + 1].Kind == ETokenKind::Identifier)
		{
			std::vector<FTypePathSegment> Segments;
			std::string Candidate;
			if (!ParseTypePath(TypeName, Segments))
			{
				break;
			}
			for (const auto& Segment : Segments)
			{
				if (!Candidate.empty())
				{
					Candidate += '.';
				}
				Candidate += Segment.Name;
			}
			Candidate += '.';
			Candidate += Tokens[Current + 1].Lexeme;
			if (!bDeclarationOutline && !IsUserTypeName(Candidate))
			{
				break;
			}
			Advance();
			Advance();
			TypeName += '.';
			TypeName += Previous().Lexeme;
			if (MatchSymbol("<"))
			{
				TypeName += '<';
				bool First = true;
				if (PendingGenericCloseCount == 0 && !CheckSymbol(">") && !CheckSymbol(">>"))
				{
					do
					{
						const auto Argument = ParseTemplateArgumentName();
						if (bHasError || Argument.empty())
						{
							return {};
						}
						if (!First)
						{
							TypeName += ',';
						}
						First = false;
						TypeName += Argument;
					}
					while (MatchSymbol(","));
				}
				if (!MatchCloseGeneric())
				{
					ReportErrorAt(Peek(), "expected '>' after nested type argument");
					return {};
				}
				TypeName += '>';
			}
		}
		if (Program)
		{
			std::vector<FTypePathSegment> Path;
			if (ParseTypePath(TypeName, Path) &&
			    (Path.size() > 1 ||
			        (Path.size() == 1 && Path.front().bApplication && IsUserTypeName(Path.front().Name))))
			{
				std::string SourceName;
				for (auto Index = TypeBegin; Index < Current; ++Index)
				{
					SourceName += Tokens[Index].Lexeme;
				}
				Program->ScopedTypeUses.push_back(
				    {TypeName, TypeFrames.empty() ? std::string{} : TypeFrames.back().Name, FunctionOwner,
				        Previous().Location,
				        TypeFrames.empty() ? std::string(FileLabel) : TypeFrames.back().Scope.ModulePath,
				        CaptureDeclarationContext(), std::move(SourceName)});
			}
		}
		TypeName = ResolveScopedTypeApplication(std::move(TypeName));
		if (bHasError || TypeName.empty())
		{
			return {};
		}
		std::size_t DynamicDimensions = 0;
		while (CheckSymbol("[") && Current + 1 < Tokens.size() && Tokens[Current + 1].Kind == ETokenKind::Symbol &&
		    Tokens[Current + 1].Lexeme == "]")
		{
			Advance();
			Advance();
			++DynamicDimensions;
		}
		if (DynamicDimensions != 0)
		{
			if (TypeName == "void" || Runtime::IsScriptArgsArrayTypeName(TypeName))
			{
				ReportErrorAt(Previous(), "invalid dynamic array element type");
				return {};
			}
			// Retain the command-line entrypoint ABI for its historic single
			// `string[]`; every other empty suffix is sugar for array<T>.
			if (TypeName == "string" && DynamicDimensions == 1)
			{
				TypeName = "string[]";
			}
			else
			{
				for (std::size_t Index = 0; Index < DynamicDimensions; ++Index)
				{
					TypeName = "array<" + TypeName + ">";
				}
			}
		}
		if (TypeNameParseDepth == 1 && ExpressionTypeArgumentParseDepth == 0 && PendingGenericCloseCount > 0)
		{
			ReportErrorAt(Previous(), "unexpected '>' after type name");
			return {};
		}
		if (CheckSymbol("[") && !ParseParamArraySuffix(TypeName))
		{
			return {};
		}
		return TypeName;
	}

	bool VeroScriptParser::EnterExpressionNesting(const FSourceLocation& Location)
	{
		constexpr int MaxExpressionParseDepth = 256;
		if (++ExpressionParseDepth <= MaxExpressionParseDepth)
		{
			return true;
		}
		--ExpressionParseDepth;
		ReportErrorAt(Location, "expression nested too deeply");
		return false;
	}

	void VeroScriptParser::LeaveExpressionNesting()
	{
		if (ExpressionParseDepth > 0)
		{
			--ExpressionParseDepth;
		}
	}

	bool VeroScriptParser::EnterUnaryNesting(const FSourceLocation& Location)
	{
		constexpr int MaxUnaryParseDepth = 256;
		if (++UnaryParseDepth <= MaxUnaryParseDepth)
		{
			return true;
		}
		--UnaryParseDepth;
		ReportErrorAt(Location, "unary expression nested too deeply");
		return false;
	}

	void VeroScriptParser::LeaveUnaryNesting()
	{
		if (UnaryParseDepth > 0)
		{
			--UnaryParseDepth;
		}
	}

	bool VeroScriptParser::EnterStatementNesting(const FSourceLocation& Location)
	{
		// Keep this well below the host-stack budget: a statement frame may include
		// declaration and control-flow parser temporaries, and malformed nested blocks
		// must report a diagnostic rather than exhaust the process stack.
		// Alternating control-flow statements and blocks use substantially more
		// stack per level than blocks alone on the default Windows thread stack.
		constexpr int MaxStatementParseDepth = 32;
		if (++StatementParseDepth <= MaxStatementParseDepth)
		{
			return true;
		}
		--StatementParseDepth;
		ReportErrorAt(Location, "statement nested too deeply");
		return false;
	}

	void VeroScriptParser::LeaveStatementNesting()
	{
		if (StatementParseDepth > 0)
		{
			--StatementParseDepth;
		}
	}

	bool VeroScriptParser::ConsumeExpressionChainNode(const FSourceLocation& Location)
	{
		// Several expression forms are parsed iteratively but represented as a
		// left-nested AST. Keep that representation within the downstream
		// compiler/destructor recursion budget.
		constexpr std::size_t MaxExpressionChainNodes = 256;
		if (++ExpressionChainNodeCount <= MaxExpressionChainNodes)
		{
			return true;
		}
		ReportErrorAt(Location, "expression chain is too long");
		return false;
	}

}
