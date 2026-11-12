// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCppFrontend.h"
#include "VeroScriptCppFrontendInteger.h"

#include <algorithm>
#include <limits>
#include <unordered_map>

namespace VeroScript::CppFrontend
{
	namespace
	{
		using FInteger = Detail::FIntegerLiteral;

		FInteger CommonInteger(FInteger Left, FInteger Right)
		{
			const unsigned Width = std::max(Left.Width, Right.Width);
			const bool Unsigned = Left.bUnsigned == Right.bUnsigned ? Left.bUnsigned
				: (Left.bUnsigned ? Left.Width >= Right.Width : Right.Width >= Left.Width);
			return {0, Width, Unsigned};
		}
		FInteger ConvertInteger(FInteger Value, const FInteger Type)
		{
			const auto Bits = Value.bUnsigned ? Value.Value : static_cast<std::uint64_t>(Value.SignedValue());
			return {Bits & Type.Mask(), Type.Width, Type.bUnsigned};
		}
		bool UnaryInteger(std::string_view Op, FInteger& Out, std::string& Error)
		{
			if (Op == "!")
			{
				Out = FInteger::Signed(Out.Value == 0);
				return true;
			}
			if (Op == "~")
			{
				Out.Value = ~Out.Value & Out.Mask();
				return true;
			}
			if (!Out.bUnsigned &&
				Out.SignedValue() ==
					(Out.Width == 64 ? std::numeric_limits<std::int64_t>::min()
									 : std::numeric_limits<std::int32_t>::min()))
			{
				Error = "integer overflow in unary '-'";
				return false;
			}
			Out.Value = (std::uint64_t{0} - Out.Value) & Out.Mask();
			return true;
		}
		bool BinaryInteger(std::string_view Op, FInteger Left, FInteger Right, FInteger& Out, std::string& Error)
		{
			const auto Type = CommonInteger(Left, Right);
			Left = ConvertInteger(Left, Type);
			Right = ConvertInteger(Right, Type);
			std::uint64_t Bits = 0;
			if (Op == "&" || Op == "|" || Op == "^")
			{
				Bits = Op == "&" ? Left.Value & Right.Value
					: Op == "|"	 ? Left.Value | Right.Value
								 : Left.Value ^ Right.Value;
			}

			else if (Type.bUnsigned)
			{
				if ((Op == "/" || Op == "%") && Right.Value == 0)
				{
					Error = "division by zero";
					return false;
				}
				if (Op == "+")
				{
					Bits = Left.Value + Right.Value;
				}

				else if (Op == "-")
				{
					Bits = Left.Value - Right.Value;
				}

				else if (Op == "*")
				{
					Bits = Left.Value * Right.Value;
				}

				else
				{
					Bits = Op == "/" ? Left.Value / Right.Value : Left.Value % Right.Value;
				}

			}
			else
			{
				const auto L = Left.SignedValue(), R = Right.SignedValue();
				const std::int64_t Min = Type.Width == 64 ? std::numeric_limits<std::int64_t>::min() : std::numeric_limits<std::int32_t>::min();
				const std::int64_t Max = Type.Width == 64 ? std::numeric_limits<std::int64_t>::max() : std::numeric_limits<std::int32_t>::max();
				std::int64_t Value = 0;
				if (Op == "+")
				{
					if ((R > 0 && L > Max - R) || (R < 0 && L < Min - R))
					{
						Error = "integer overflow in '+'";
						return false;
					}
					Value = L + R;
				}
				else if (Op == "-")
				{
					if ((R < 0 && L > Max + R) || (R > 0 && L < Min + R))
					{
						Error = "integer overflow in '-'";
						return false;
					}
					Value = L - R;
				}
				else if (Op == "*")
				{
					if (L != 0 && R != 0 &&
						((L == -1 && R == Min) || (R == -1 && L == Min) || (L > 0 && R > 0 && L > Max / R) ||
							(L > 0 && R < 0 && R < Min / L) || (L < 0 && R > 0 && L < Min / R) ||
							(L < 0 && R < 0 && L < Max / R)))
					{
						Error = "integer overflow in '*'";
						return false;
					}
					Value = L * R;
				}
				else
				{
					if (R == 0)
					{
						Error = "division by zero";
						return false;
					}
					if (L == Min && R == -1)
					{
						Error = "integer overflow in division";
						return false;
					}
					Value = Op == "/" ? L / R : L % R;
				}
				Bits = static_cast<std::uint64_t>(Value);
			}
			Out = {Bits & Type.Mask(), Type.Width, Type.bUnsigned};
			return true;
		}
		void CompareInteger(std::string_view Op, FInteger Left, FInteger Right, FInteger& Out)
		{
			const auto Type = CommonInteger(Left, Right);
			Left = ConvertInteger(Left, Type);
			Right = ConvertInteger(Right, Type);
			const auto Compare = [&](auto L, auto R)
			{
				return Op == "<" ? L < R
					: Op == "<=" ? L <= R
					: Op == ">"	 ? L > R
					: Op == ">=" ? L >= R
					: Op == "==" ? L == R
								 : L != R;
			};
			Out = FInteger::Signed(Type.bUnsigned ? Compare(Left.Value, Right.Value) : Compare(Left.SignedValue(), Right.SignedValue()));
		}
		bool ShiftInteger(bool LeftShift, FInteger Left, FInteger Count, FInteger& Out, std::string& Error)
		{
			if ((!Count.bUnsigned && Count.SignedValue() < 0) || Count.Value >= Left.Width)
			{
				Error = "shift count must be non-negative and below the operand width";
				return false;
			}
			if (!Left.bUnsigned && Left.SignedValue() < 0)
			{
				Error = "shift of a negative signed value is unsupported";
				return false;
			}
			if (LeftShift && !Left.bUnsigned && Left.Value > (Left.Mask() >> Count.Value))
			{
				Error = "integer overflow in left shift";
				return false;
			}
			Out = Left;
			Out.Value = (LeftShift ? Left.Value << Count.Value : Left.Value >> Count.Value) & Left.Mask();
			return true;
		}
		FInteger PromotedEnumerator(std::int64_t Value, std::string_view Underlying)
		{
			if (Underlying == "unsigned" || Underlying == "unsigned int" || Underlying == "uint32")
			{
				return {static_cast<std::uint64_t>(Value) & 0xffffffffu, 32, true};
			}

			return FInteger::Signed(Value, Underlying == "long" || Underlying == "int64" ? 64 : 32);
		}

		bool TryParseEnumCharacter(const std::string_view Text, std::int64_t& Out)
		{
			if (Text.size() < 3 || Text.front() != '\'' || Text.back() != '\'')
			{
				return false;
			}
			const std::string_view Body = Text.substr(1, Text.size() - 2);
			if (Body.size() == 1)
			{
				const unsigned char Character = static_cast<unsigned char>(Body.front());
				if (Character >= 0x80 || Character == '\\')
				{
					return false;
				}
				Out = Character;
				return true;
			}
			if (Body.size() < 2 || Body.front() != '\\')
			{
				return false;
			}
			if (Body.size() == 2)
			{
				switch (Body[1])
				{
				case 'a':
					{
						Out = 7;
						return true;
					}
				case 'b':
					{
						Out = 8;
						return true;
					}
				case 'f':
					{
						Out = 12;
						return true;
					}
				case 'n':
					{
						Out = 10;
						return true;
					}
				case 'r':
					{
						Out = 13;
						return true;
					}
				case 't':
					{
						Out = 9;
						return true;
					}
				case 'v':
					{
						Out = 11;
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
				case '?':
					{
						Out = '?';
						return true;
					}
				default:
					{
						break;
					}
				}
			}
			std::uint32_t Value = 0;
			if (Body[1] == 'x')
			{
				if (Body.size() == 2)
				{
					return false;
				}
				for (std::size_t Index = 2; Index < Body.size(); ++Index)
				{
					const char Character = Body[Index];
					const int Digit = Character >= '0' && Character <= '9' ? Character - '0'
						: Character >= 'a' && Character <= 'f' ? Character - 'a' + 10
						: Character >= 'A' && Character <= 'F' ? Character - 'A' + 10
																		   : -1;
					if (Digit < 0 || Value > (127u - static_cast<std::uint32_t>(Digit)) / 16u)
					{
						return false;
					}
					Value = Value * 16u + static_cast<std::uint32_t>(Digit);
				}
			}
			else
			{
				if (Body.size() > 4)
				{
					return false;
				}
				for (std::size_t Index = 1; Index < Body.size(); ++Index)
				{
					if (Body[Index] < '0' || Body[Index] > '7')
					{
						return false;
					}
					const std::uint32_t Digit = static_cast<std::uint32_t>(Body[Index] - '0');
					if (Value > (127u - Digit) / 8u)
					{
						return false;
					}
					Value = Value * 8u + Digit;
				}
			}
			Out = static_cast<std::int64_t>(Value);
			return true;
		}

		class FEnumConstantExpressionParser
		{
		public:
			FEnumConstantExpressionParser(
				const std::vector<FToken>& InTokens,
				const std::unordered_map<std::string, FInteger>& InValues)
				: Tokens(InTokens), Values(InValues)
			{
			}

			bool Parse(FInteger& Out, std::string& OutError)
			{
				if (Tokens.empty())
				{
					return OutError = "empty initializer", false;
				}
				if (!ParseBitOr(Out, OutError))
				{
					return false;
				}
				if (Current != Tokens.size())
				{
					return OutError = "unsupported token '" + Tokens[Current].Text + "'", false;
				}
				return true;
			}

		private:
			bool Match(const std::string_view Text)
			{
				if (Current >= Tokens.size() || Tokens[Current].Text != Text)
				{
					return false;
				}
				++Current;
				return true;
			}

			bool ParsePrimary(FInteger& Out, std::string& Error)
			{
				if (Match("("))
				{
					if (!ParseBitOr(Out, Error))
					{
						return false;
					}
					if (!Match(")"))
					{
						return Error = "expected ')'", false;
					}
					return true;
				}
				if (Current >= Tokens.size())
				{
					return Error = "expected integer literal or earlier enumerator", false;
				}
				if (Tokens[Current].Kind == ETokenKind::Number)
				{
					if (!Detail::ParseIntegerLiteral(Tokens[Current++].Text, Out, Error))
					{
						return false;
					}
					return true;
				}
				if (Tokens[Current].Kind == ETokenKind::CharacterLiteral)
				{
					std::int64_t Character = 0;
					if (!TryParseEnumCharacter(Tokens[Current++].Text, Character))
					{
						return Error = "unsupported, multi-character, or target-dependent character literal", false;
					}
					Out = FInteger::Signed(Character);
					return true;
				}
				if (Tokens[Current].Text == "true" || Tokens[Current].Text == "false")
				{
					Out = FInteger::Signed(Tokens[Current++].Text == "true");
					return true;
				}
				const bool bGlobalQualified = Match("::");
				if (Current < Tokens.size()
					&& Tokens[Current].Kind == ETokenKind::Identifier)
				{
					std::string Name = Tokens[Current++].Text;
					while (Current + 1 < Tokens.size() && Tokens[Current].Text == "::"
						&& Tokens[Current + 1].Kind == ETokenKind::Identifier)
					{
						Name += "::" + Tokens[Current + 1].Text;
						Current += 2;
					}
					auto It = Values.find(bGlobalQualified ? "::" + Name : Name);
					if (It == Values.end())
					{
						return Error = "unknown or forward enum constant '" + Name + "'", false;
					}
					Out = It->second;
					return true;
				}
				if (bGlobalQualified)
				{
					return Error = "expected an enumerator after global '::'", false;
				}
				return Error = "unsupported token '" + Tokens[Current].Text + "'", false;
			}

			bool ParseUnary(FInteger& Out, std::string& Error)
			{
				if (Match("+"))
				{
					return ParseUnary(Out, Error);
				}
				if (Match("-"))
				{
					if (!ParseUnary(Out, Error))
					{
						return false;
					}
					return UnaryInteger("-", Out, Error);
				}
				if (Match("~"))
				{
					if (!ParseUnary(Out, Error))
					{
						return false;
					}
					return UnaryInteger("~", Out, Error);
				}
				if (Match("!"))
				{
					if (!ParseUnary(Out, Error))
					{
						return false;
					}
					return UnaryInteger("!", Out, Error);
				}
				return ParsePrimary(Out, Error);
			}

			bool ParseMultiply(FInteger& Out, std::string& Error)
			{
				if (!ParseUnary(Out, Error))
				{
					return false;
				}
				while (Current < Tokens.size() && (Tokens[Current].Text == "*"
					|| Tokens[Current].Text == "/" || Tokens[Current].Text == "%"))
				{
					const std::string Op = Tokens[Current++].Text;
					FInteger Right;
					if (!ParseUnary(Right, Error)
						|| !BinaryInteger(Op, Out, Right, Out, Error))
					{
						return false;
					}
				}
				return true;
			}

			bool ParseAdd(FInteger& Out, std::string& Error)
			{
				if (!ParseMultiply(Out, Error))
				{
					return false;
				}
				while (Current < Tokens.size() && (Tokens[Current].Text == "+"
					|| Tokens[Current].Text == "-"))
				{
					const std::string Op = Tokens[Current++].Text;
					FInteger Right;
					if (!ParseMultiply(Right, Error)
						|| !BinaryInteger(Op, Out, Right, Out, Error))
					{
						return false;
					}
				}
				return true;
			}

			bool ParseShift(FInteger& Out, std::string& Error)
			{
				if (!ParseAdd(Out, Error))
				{
					return false;
				}
				while (Current < Tokens.size())
				{
					bool bLeft = false;
					if (Tokens[Current].Text == "<<")
					{
						bLeft = true;
						++Current;
					}
					else if (Current + 1 < Tokens.size() && Tokens[Current].Text == ">"
						&& Tokens[Current + 1].Text == ">")
					{
						Current += 2;
					}
					else
					{
						break;
					}
					FInteger Shift;
					if (!ParseAdd(Shift, Error))
					{
						return false;
					}
					if (!ShiftInteger(bLeft, Out, Shift, Out, Error)) return false;
				}
				return true;
			}

			bool ParseRelational(FInteger& Out, std::string& Error)
			{
				if (!ParseShift(Out, Error))
				{
					return false;
				}
				while (Current < Tokens.size())
				{
					const std::string Op = Tokens[Current].Text;
					if (Op != "<" && Op != "<=" && Op != ">" && Op != ">=")
					{
						break;
					}
					++Current;
					FInteger Right;
					if (!ParseShift(Right, Error))
					{
						return false;
					}
					CompareInteger(Op, Out, Right, Out);
				}
				return true;
			}

			bool ParseEquality(FInteger& Out, std::string& Error)
			{
				if (!ParseRelational(Out, Error))
				{
					return false;
				}
				while (Current < Tokens.size()
					&& (Tokens[Current].Text == "==" || Tokens[Current].Text == "!="))
				{
					const bool bEqual = Tokens[Current++].Text == "==";
					FInteger Right;
					if (!ParseRelational(Right, Error))
					{
						return false;
					}
					CompareInteger(bEqual ? "==" : "!=", Out, Right, Out);
				}
				return true;
			}

			template <typename TNext>
			bool ParseBitwise(FInteger& Out, std::string& Error,
				const std::string_view Operator, TNext ParseNext)
			{
				if (!(this->*ParseNext)(Out, Error))
				{
					return false;
				}
				while (Match(Operator))
				{
					FInteger Right;
					if (!(this->*ParseNext)(Right, Error))
					{
						return false;
					}
					if (!BinaryInteger(Operator, Out, Right, Out, Error)) return false;
				}
				return true;
			}

			bool ParseBitAnd(FInteger& Out, std::string& Error)
			{
				return ParseBitwise(Out, Error, "&", &FEnumConstantExpressionParser::ParseEquality);
			}
			bool ParseBitXor(FInteger& Out, std::string& Error)
			{
				return ParseBitwise(Out, Error, "^", &FEnumConstantExpressionParser::ParseBitAnd);
			}
			bool ParseBitOr(FInteger& Out, std::string& Error)
			{
				return ParseBitwise(Out, Error, "|", &FEnumConstantExpressionParser::ParseBitXor);
			}

			const std::vector<FToken>& Tokens;
			const std::unordered_map<std::string, FInteger>& Values;
			std::size_t Current = 0;
		};

	}

	bool EvaluateEnumValues(
		const FEnum& Enum,
		std::vector<std::int64_t>& OutValues,
		std::size_t& OutFailedIndex,
		std::string& OutError)
	{
		OutValues.clear();
		OutFailedIndex = 0;
		OutError.clear();
		OutValues.reserve(Enum.Values.size());
		std::unordered_map<std::string, FInteger> KnownValues;
		std::int64_t NextValue = 0;
		FInteger NextExpression;
		const bool FixedUnderlying = Enum.bScoped || Enum.bHasExplicitUnderlyingType || Enum.UnderlyingType != "int";
		for (std::size_t Index = 0; Index < Enum.Values.size(); ++Index)
		{
			const FEnumValue& Value = Enum.Values[Index];
			std::int64_t Numeric = NextValue;
			FInteger Expression = NextExpression;
			if (!Value.InitializerTokens.empty())
			{
				if (!FEnumConstantExpressionParser(Value.InitializerTokens, KnownValues).Parse(Expression, OutError)
					|| (Expression.bUnsigned && Expression.Value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())))
				{
					if (OutError.empty())
					{
						OutError = "enum value exceeds the supported int64 representation";
					}

					OutFailedIndex = Index;
					OutValues.clear();
					return false;
				}
				Numeric = Expression.bUnsigned ? static_cast<std::int64_t>(Expression.Value) : Expression.SignedValue();
			}
			const auto Typed = FixedUnderlying ? PromotedEnumerator(Numeric, Enum.UnderlyingType) : Expression;
			OutValues.push_back(Numeric);
			KnownValues.emplace(Value.Name, Typed);
			KnownValues.emplace(Enum.Name + "::" + Value.Name, Typed);
			KnownValues.emplace(Enum.QualifiedName + "::" + Value.Name, Typed);
			KnownValues.emplace("::" + Enum.QualifiedName + "::" + Value.Name, Typed);
			if (!Enum.bScoped)
			{
				const std::size_t Separator = Enum.QualifiedName.rfind("::");
				if (Separator != std::string::npos)
				{
					KnownValues.emplace(Enum.QualifiedName.substr(0, Separator)
							+ "::" + Value.Name, Typed);
					KnownValues.emplace("::" + Enum.QualifiedName.substr(0, Separator) + "::" + Value.Name, Typed);
				}
				else
				{
					KnownValues.emplace("::" + Value.Name, Typed);
				}
			}
			if (Numeric == std::numeric_limits<std::int64_t>::max()
				&& Index + 1 < Enum.Values.size()
				&& Enum.Values[Index + 1].InitializerTokens.empty())
			{
				OutFailedIndex = Index + 1;
				OutError = "implicit value overflows int64 after '" + Value.Name + "'";
				OutValues.clear();
				return false;
			}
			if (Numeric != std::numeric_limits<std::int64_t>::max())
			{
				NextValue = Numeric + 1;
				NextExpression = Typed;
				// Implicit enumerators retain the prior type while it can represent
				// the successor. Keep the numeric successor separately for range checks.
				if (Typed.Width == 32 &&
					(NextValue > (Typed.bUnsigned ? static_cast<std::int64_t>(
														std::numeric_limits<std::uint32_t>::max())
												  : std::numeric_limits<std::int32_t>::max()) ||
						NextValue < std::numeric_limits<std::int32_t>::min()))
				{
					NextExpression.Width = 64;
				}

				NextExpression.Value = static_cast<std::uint64_t>(NextValue) & NextExpression.Mask();
			}
		}
		return true;
	}

	bool TryGetEnumValueRange(
		const std::string_view UnderlyingType,
		std::int64_t& OutMinimum,
		std::int64_t& OutMaximum)
	{
		if (UnderlyingType == "char")
		{
			OutMinimum = static_cast<std::int64_t>(std::numeric_limits<char>::min());
			OutMaximum = static_cast<std::int64_t>(std::numeric_limits<char>::max());
		}
		else if (UnderlyingType == "int8")
		{
			OutMinimum = std::numeric_limits<std::int8_t>::min();
			OutMaximum = std::numeric_limits<std::int8_t>::max();
		}
		else if (UnderlyingType == "unsigned char" || UnderlyingType == "uint8")
		{
			OutMinimum = 0;
			OutMaximum = std::numeric_limits<std::uint8_t>::max();
		}
		else if (UnderlyingType == "short" || UnderlyingType == "int16")
		{
			OutMinimum = std::numeric_limits<std::int16_t>::min();
			OutMaximum = std::numeric_limits<std::int16_t>::max();
		}
		else if (UnderlyingType == "unsigned short" || UnderlyingType == "uint16")
		{
			OutMinimum = 0;
			OutMaximum = std::numeric_limits<std::uint16_t>::max();
		}
		else if (UnderlyingType == "int" || UnderlyingType == "int32")
		{
			OutMinimum = std::numeric_limits<std::int32_t>::min();
			OutMaximum = std::numeric_limits<std::int32_t>::max();
		}
		else if (UnderlyingType == "unsigned" || UnderlyingType == "unsigned int"
			|| UnderlyingType == "uint32")
		{
			OutMinimum = 0;
			OutMaximum = std::numeric_limits<std::uint32_t>::max();
		}
		else if (UnderlyingType == "long" || UnderlyingType == "int64")
		{
			OutMinimum = std::numeric_limits<std::int64_t>::min();
			OutMaximum = std::numeric_limits<std::int64_t>::max();
		}
		else
		{
			return false;
		}
		return true;
	}

}
