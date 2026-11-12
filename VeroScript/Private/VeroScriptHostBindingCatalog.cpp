// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHostBindingCatalog.h"
#include "VeroScriptJsonString.h"
#include "VeroScriptFloatParsing.h"

#include "VeroScriptPath.h"
#include <charconv>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace VeroScript
{
	namespace
	{
		constexpr std::size_t MaxBindingCatalogBytes = 128u * 1024u * 1024u;
		constexpr std::size_t MaxBindingCatalogJsonValues = 4u * 1024u * 1024u;

		std::string EscapeJsonString(const std::string_view Text)
		{
			return JsonStringDetail::Escape(Text, JsonStringDetail::EControlEscapeStyle::Unicode);
		}

		void AppendOrigin(std::string& Out, const FBindingOrigin& Origin)
		{
			Out += "{\"providerId\":\"" + EscapeJsonString(Origin.ProviderId) + "\",\"modulePath\":\"" + EscapeJsonString(Origin.ModulePath) + "\",\"exported\":";
			Out += Origin.bExported ? "true}" : "false}";
		}

		void AppendStringArray(
		    std::string& Out,
		    const std::vector<std::string>& Values)
		{
			Out.push_back('[');
			for (std::size_t Index = 0; Index < Values.size(); ++Index)
			{
				if (Index > 0)
				{
					Out += ", ";
				}
				Out.push_back('"');
				Out += EscapeJsonString(Values[Index]);
				Out.push_back('"');
			}
			Out.push_back(']');
		}

		void AppendNativeTypeArray(
		    std::string& Out,
		    const std::vector<ENativeType>& Types)
		{
			Out.push_back('[');
			for (std::size_t Index = 0; Index < Types.size(); ++Index)
			{
				if (Index > 0)
				{
					Out += ", ";
				}
				Out.push_back('"');
				Out += NativeTypeName(Types[Index]);
				Out.push_back('"');
			}
			Out.push_back(']');
		}

		void AppendUInt8Array(std::string& Out, const std::vector<std::uint8_t>& Values)
		{
			Out.push_back('[');
			for (std::size_t Index = 0; Index < Values.size(); ++Index)
			{
				if (Index > 0)
				{
					Out += ", ";
				}
				Out += std::to_string(Values[Index]);
			}
			Out.push_back(']');
		}

		void AppendAnnotationInstances(
		    std::string& Out,
		    const std::vector<FHostAnnotationInstance>& Annotations)
		{
			Out.push_back('[');
			for (std::size_t AnnotationIndex = 0;
			    AnnotationIndex < Annotations.size();
			    ++AnnotationIndex)
			{
				if (AnnotationIndex > 0)
				{
					Out += ", ";
				}
				const FHostAnnotationInstance& Annotation = Annotations[AnnotationIndex];
				Out += "{\"name\": \"";
				Out += EscapeJsonString(Annotation.Name);
				Out += "\", \"arguments\": [";
				for (std::size_t ArgumentIndex = 0;
				    ArgumentIndex < Annotation.Arguments.size();
				    ++ArgumentIndex)
				{
					if (ArgumentIndex > 0)
					{
						Out += ", ";
					}
					const FHostAnnotationArgument& Argument =
					    Annotation.Arguments[ArgumentIndex];
					Out += "{\"name\": \"";
					Out += EscapeJsonString(Argument.Name);
					Out.push_back('"');
					if (Argument.Value.has_value())
					{
						Out += ", \"value\": \"";
						Out += EscapeJsonString(*Argument.Value);
						Out.push_back('"');
					}
					Out.push_back('}');
				}
				Out += "]}";
			}
			Out.push_back(']');
		}

		void AppendParameterAnnotationInstances(
		    std::string& Out,
		    const std::vector<std::vector<FHostAnnotationInstance>>& ParamAnnotations)
		{
			Out.push_back('[');
			for (std::size_t Index = 0; Index < ParamAnnotations.size(); ++Index)
			{
				if (Index > 0)
				{
					Out += ", ";
				}
				AppendAnnotationInstances(Out, ParamAnnotations[Index]);
			}
			Out.push_back(']');
		}

		const char* MemberKindName(const EHostMemberKind Kind)
		{
			switch (Kind)
			{
			case EHostMemberKind::Constructor:
				return "constructor";
			case EHostMemberKind::Method:
				return "method";
			case EHostMemberKind::StaticMethod:
				return "staticMethod";
			case EHostMemberKind::StaticConstant:
				return "staticConstant";
			case EHostMemberKind::PropertyGetter:
				return "propertyGetter";
			case EHostMemberKind::PropertySetter:
				return "propertySetter";
			case EHostMemberKind::EnumMember:
				return "enumMember";
			case EHostMemberKind::Field:
				return "field";
			default:
				return "method";
			}
		}

		bool ParseMemberKind(const std::string_view Text, EHostMemberKind& OutKind)
		{
			if (Text == "constructor")
			{
				OutKind = EHostMemberKind::Constructor;
				return true;
			}
			if (Text == "method")
			{
				OutKind = EHostMemberKind::Method;
				return true;
			}
			if (Text == "staticMethod")
			{
				OutKind = EHostMemberKind::StaticMethod;
				return true;
			}
			if (Text == "staticConstant")
			{
				OutKind = EHostMemberKind::StaticConstant;
				return true;
			}
			if (Text == "propertyGetter")
			{
				OutKind = EHostMemberKind::PropertyGetter;
				return true;
			}
			if (Text == "propertySetter")
			{
				OutKind = EHostMemberKind::PropertySetter;
				return true;
			}
			if (Text == "enumMember")
			{
				OutKind = EHostMemberKind::EnumMember;
				return true;
			}
			if (Text == "field")
			{
				OutKind = EHostMemberKind::Field;
				return true;
			}
			return false;
		}

		bool ParseTypeKind(const std::string_view Text, EHostTypeKind& OutKind)
		{
			if (Text == "enum")
			{
				OutKind = EHostTypeKind::Enum;
				return true;
			}
			if (Text == "struct")
			{
				OutKind = EHostTypeKind::Struct;
				return true;
			}
			if (Text == "class")
			{
				OutKind = EHostTypeKind::Class;
				return true;
			}
			return false;
		}

		bool ParseNativeType(const std::string_view Text, ENativeType& OutType)
		{
			if (Text == "variant")
			{
				OutType = ENativeType::Variant;
				return true;
			}
			if (Text == "void")
			{
				OutType = ENativeType::Void;
				return true;
			}
			if (Text == "int")
			{
				OutType = ENativeType::Int;
				return true;
			}
			if (Text == "bool")
			{
				OutType = ENativeType::Bool;
				return true;
			}
			if (Text == "double")
			{
				OutType = ENativeType::Double;
				return true;
			}
			if (Text == "float")
			{
				OutType = ENativeType::Float;
				return true;
			}
			if (Text == "char" || Text == "int8")
			{
				OutType = ENativeType::Int8;
				return true;
			}
			if (Text == "unsigned char" || Text == "uint8")
			{
				OutType = ENativeType::UInt8;
				return true;
			}
			if (Text == "short" || Text == "int16")
			{
				OutType = ENativeType::Int16;
				return true;
			}
			if (Text == "unsigned short" || Text == "uint16")
			{
				OutType = ENativeType::UInt16;
				return true;
			}
			if (Text == "string")
			{
				OutType = ENativeType::String;
				return true;
			}
			if (Text == "unsigned int" || Text == "unsigned")
			{
				OutType = ENativeType::Unsigned;
				return true;
			}
			if (Text == "int64")
			{
				OutType = ENativeType::Int64;
				return true;
			}
			if (Text == "uint64")
			{
				OutType = ENativeType::UInt64;
				return true;
			}
			if (Text == "enum")
			{
				OutType = ENativeType::Enum;
				return true;
			}
			if (Text == "function" || Text == "fn")
			{
				OutType = ENativeType::Function;
				return true;
			}
			if (Text == "object" || Text == "HostObject")
			{
				OutType = ENativeType::HostObject;
				return true;
			}
			if (Text == "array" || Text == "DynamicArray")
			{
				OutType = ENativeType::Array;
				return true;
			}
			if (Text == "map" || Text == "DynamicMap")
			{
				OutType = ENativeType::Map;
				return true;
			}
			if (Text == "multimap" || Text == "MultiMap")
			{
				OutType = ENativeType::MultiMap;
				return true;
			}
			if (Text == "set" || Text == "DynamicSet")
			{
				OutType = ENativeType::Set;
				return true;
			}
			if (Text == "multiset" || Text == "MultiSet")
			{
				OutType = ENativeType::MultiSet;
				return true;
			}
			if (Text == "ordered_multiset" || Text == "OrderedMultiSet")
			{
				OutType = ENativeType::OrderedMultiSet;
				return true;
			}
			if (Text == "queue")
			{
				OutType = ENativeType::Queue;
				return true;
			}
			if (Text == "stack")
			{
				OutType = ENativeType::Stack;
				return true;
			}
			if (Text == "ordered_map" || Text == "OrderedMap")
			{
				OutType = ENativeType::OrderedMap;
				return true;
			}
			if (Text == "priority_queue" || Text == "PriorityQueue")
			{
				OutType = ENativeType::PriorityQueue;
				return true;
			}
			if (Text == "ordered_multimap" || Text == "OrderedMultiMap")
			{
				OutType = ENativeType::OrderedMultiMap;
				return true;
			}
			if (Text == "deque")
			{
				OutType = ENativeType::Deque;
				return true;
			}
			if (Text == "channel")
			{
				OutType = ENativeType::Channel;
				return true;
			}
			return false;
		}

		struct FJsonValue
		{
			enum class EKind : std::uint8_t
			{
				Null,
				Bool,
				Number,
				String,
				Object,
				Array,
			};

			// Define special members after the recursive pair element is complete.
			FJsonValue();

			~FJsonValue();

			FJsonValue(const FJsonValue&);

			FJsonValue(FJsonValue&&) noexcept;

			FJsonValue& operator=(const FJsonValue&);

			FJsonValue& operator=(FJsonValue&&) noexcept;

			const FJsonValue* Find(std::string_view Key) const;

			EKind Kind = EKind::Null;
			bool BoolValue = false;
			double NumberValue = 0.0;
			std::string StringValue;
			std::vector<std::pair<std::string, FJsonValue>> ObjectValue;
			std::vector<FJsonValue> ArrayValue;
		};

		FJsonValue::FJsonValue() = default;
		FJsonValue::~FJsonValue() = default;
		FJsonValue::FJsonValue(const FJsonValue&) = default;
		FJsonValue::FJsonValue(FJsonValue&&) noexcept = default;
		FJsonValue& FJsonValue::operator=(const FJsonValue&) = default;
		FJsonValue& FJsonValue::operator=(FJsonValue&&) noexcept = default;

		const FJsonValue* FJsonValue::Find(const std::string_view Key) const
		{
			if (Kind != EKind::Object)
			{
				return nullptr;
			}
			for (const auto& Pair : ObjectValue)
			{
				if (Pair.first == Key)
				{
					return &Pair.second;
				}
			}
			return nullptr;
		}

		class FMiniJsonParser
		{
		public:
			explicit FMiniJsonParser(const std::string_view Text)
			    : Text(Text)
			{
			}

			bool Parse(FJsonValue& Out, std::string& OutError)
			{
				SkipWs();
				if (!ParseValue(Out, OutError))
				{
					return false;
				}
				SkipWs();
				if (Pos != Text.size())
				{
					OutError = "trailing junk after JSON value";
					return false;
				}
				return true;
			}

		private:
			void SkipWs()
			{
				while (Pos < Text.size() && std::isspace(static_cast<unsigned char>(Text[Pos])) != 0)
				{
					++Pos;
				}
			}

			bool Match(const char Ch)
			{
				SkipWs();
				if (Pos < Text.size() && Text[Pos] == Ch)
				{
					++Pos;
					return true;
				}
				return false;
			}

			bool ParseValue(FJsonValue& Out, std::string& OutError)
			{
				if (ValueCount >= MaxBindingCatalogJsonValues)
				{
					OutError = "binding catalog JSON contains too many values";
					return false;
				}
				++ValueCount;
				if (Depth >= MaxJsonDepth)
				{
					OutError = "binding catalog JSON nested too deeply";
					return false;
				}
				++Depth;
				struct FDepthGuard
				{
					~FDepthGuard()
					{
						--Value;
					}

					std::size_t& Value;

				} DepthGuard{Depth};
				SkipWs();
				if (Pos >= Text.size())
				{
					OutError = "unexpected end of JSON";
					return false;
				}
				const char Ch = Text[Pos];
				if (Ch == '{')
				{
					return ParseObject(Out, OutError);
				}
				if (Ch == '[')
				{
					return ParseArray(Out, OutError);
				}
				if (Ch == '"')
				{
					Out.Kind = FJsonValue::EKind::String;
					return ParseString(Out.StringValue, OutError);
				}
				if (Ch == 't' || Ch == 'f')
				{
					return ParseBool(Out, OutError);
				}
				if (Ch == 'n')
				{
					return ParseNull(Out, OutError);
				}
				if (Ch == '-' || (Ch >= '0' && Ch <= '9'))
				{
					return ParseNumber(Out, OutError);
				}
				OutError = "unexpected JSON token";
				return false;
			}

			bool ParseObject(FJsonValue& Out, std::string& OutError)
			{
				if (!Match('{'))
				{
					OutError = "expected '{'";
					return false;
				}
				Out.Kind = FJsonValue::EKind::Object;
				SkipWs();
				if (Match('}'))
				{
					return true;
				}
				std::unordered_set<std::string> Keys;
				for (;;)
				{
					std::string Key;
					if (!ParseString(Key, OutError))
					{
						return false;
					}
					if (!Match(':'))
					{
						OutError = "expected ':' after object key";
						return false;
					}
					FJsonValue Value;
					if (!ParseValue(Value, OutError))
					{
						return false;
					}
					if (!Keys.emplace(Key).second)
					{
						OutError = "duplicate JSON object key: " + Key;
						return false;
					}
					Out.ObjectValue.emplace_back(std::move(Key), std::move(Value));
					SkipWs();
					if (Match('}'))
					{
						return true;
					}
					if (!Match(','))
					{
						OutError = "expected ',' or '}' in object";
						return false;
					}
				}
			}

			bool ParseArray(FJsonValue& Out, std::string& OutError)
			{
				if (!Match('['))
				{
					OutError = "expected '['";
					return false;
				}
				Out.Kind = FJsonValue::EKind::Array;
				SkipWs();
				if (Match(']'))
				{
					return true;
				}
				for (;;)
				{
					FJsonValue Value;
					if (!ParseValue(Value, OutError))
					{
						return false;
					}
					Out.ArrayValue.push_back(std::move(Value));
					SkipWs();
					if (Match(']'))
					{
						return true;
					}
					if (!Match(','))
					{
						OutError = "expected ',' or ']' in array";
						return false;
					}
				}
			}

			bool ParseString(std::string& Out, std::string& OutError)
			{
				SkipWs();
				if (Pos >= Text.size() || Text[Pos] != '"')
				{
					OutError = "expected string";
					return false;
				}
				++Pos;
				Out.clear();
				const auto AppendUtf8 = [&Out](const std::uint32_t Code)
				{
					if (Code <= 0x7fu)
					{
						Out.push_back(static_cast<char>(Code));
					}
					else if (Code <= 0x7ffu)
					{
						Out.push_back(static_cast<char>(0xc0u | (Code >> 6)));
						Out.push_back(static_cast<char>(0x80u | (Code & 0x3fu)));
					}
					else if (Code <= 0xffffu)
					{
						Out.push_back(static_cast<char>(0xe0u | (Code >> 12)));
						Out.push_back(static_cast<char>(0x80u | ((Code >> 6) & 0x3fu)));
						Out.push_back(static_cast<char>(0x80u | (Code & 0x3fu)));
					}
					else
					{
						Out.push_back(static_cast<char>(0xf0u | (Code >> 18)));
						Out.push_back(static_cast<char>(0x80u | ((Code >> 12) & 0x3fu)));
						Out.push_back(static_cast<char>(0x80u | ((Code >> 6) & 0x3fu)));
						Out.push_back(static_cast<char>(0x80u | (Code & 0x3fu)));
					}
				};
				const auto ParseHex4 = [this, &OutError](std::uint32_t& OutCode)
				{
					OutCode = 0;
					for (int Index = 0; Index < 4; ++Index)
					{
						if (Pos >= Text.size())
						{
							OutError = "truncated unicode escape";
							return false;
						}
						const char Hex = Text[Pos++];
						OutCode <<= 4;
						if (Hex >= '0' && Hex <= '9')
						{
							OutCode |= Hex - '0';
						}
						else if (Hex >= 'a' && Hex <= 'f')
						{
							OutCode |= Hex - 'a' + 10;
						}
						else if (Hex >= 'A' && Hex <= 'F')
						{
							OutCode |= Hex - 'A' + 10;
						}
						else
						{
							OutError = "invalid unicode escape";
							return false;
						}
					}
					return true;
				};
				while (Pos < Text.size())
				{
					const char Ch = Text[Pos++];
					if (Ch == '"')
					{
						return true;
					}
					if (Ch == '\\')
					{
						if (Pos >= Text.size())
						{
							OutError = "unterminated escape in string";
							return false;
						}
						const char Esc = Text[Pos++];
						switch (Esc)
						{
						case '"':
						case '\\':
						case '/':
							{
								Out.push_back(Esc);
								break;
							}
						case 'b':
							{
								Out.push_back('\b');
								break;
							}
						case 'f':
							{
								Out.push_back('\f');
								break;
							}
						case 'n':
							{
								Out.push_back('\n');
								break;
							}
						case 'r':
							{
								Out.push_back('\r');
								break;
							}
						case 't':
							{
								Out.push_back('\t');
								break;
							}
						case 'u':
							{
								std::uint32_t Code = 0;
								if (!ParseHex4(Code))
								{
									return false;
								}
								if (Code >= 0xd800u && Code <= 0xdbffu)
								{
									if (Pos + 2 > Text.size() || Text[Pos] != '\\' || Text[Pos + 1] != 'u')
									{
										OutError = "incomplete unicode surrogate pair";
										return false;
									}
									Pos += 2;
									std::uint32_t Low = 0;
									if (!ParseHex4(Low))
									{
										return false;
									}
									if (Low < 0xdc00u || Low > 0xdfffu)
									{
										OutError = "invalid unicode low surrogate";
										return false;
									}
									Code = 0x10000u + ((Code - 0xd800u) << 10) + (Low - 0xdc00u);
								}
								else if (Code >= 0xdc00u && Code <= 0xdfffu)
								{
									OutError = "lone unicode low surrogate";
									return false;
								}
								AppendUtf8(Code);
								break;
							}
						default:
							{
								OutError = "unsupported escape in string";
								return false;
							}
						}
						continue;
					}
					if (static_cast<unsigned char>(Ch) < 0x20u)
					{
						OutError = "unescaped control character in string";
						return false;
					}
					Out.push_back(Ch);
				}
				OutError = "unterminated string";
				return false;
			}

			bool ParseBool(FJsonValue& Out, std::string& OutError)
			{
				if (Text.compare(Pos, 4, "true") == 0)
				{
					Pos += 4;
					Out.Kind = FJsonValue::EKind::Bool;
					Out.BoolValue = true;
					return true;
				}
				if (Text.compare(Pos, 5, "false") == 0)
				{
					Pos += 5;
					Out.Kind = FJsonValue::EKind::Bool;
					Out.BoolValue = false;
					return true;
				}
				OutError = "invalid boolean";
				return false;
			}

			bool ParseNull(FJsonValue& Out, std::string& OutError)
			{
				if (Text.compare(Pos, 4, "null") == 0)
				{
					Pos += 4;
					Out.Kind = FJsonValue::EKind::Null;
					return true;
				}
				OutError = "invalid null";
				return false;
			}

			bool ParseNumber(FJsonValue& Out, std::string& OutError)
			{
				const std::size_t Begin = Pos;
				if (Text[Pos] == '-')
				{
					++Pos;
				}
				if (Pos >= Text.size())
				{
					OutError = "invalid number";
					return false;
				}
				if (Text[Pos] == '0')
				{
					++Pos;
					if (Pos < Text.size() && Text[Pos] >= '0' && Text[Pos] <= '9')
					{
						OutError = "invalid leading zero in number";
						return false;
					}
				}
				else if (Text[Pos] >= '1' && Text[Pos] <= '9')
				{
					do
					{
						++Pos;
					}
					while (Pos < Text.size() && Text[Pos] >= '0' && Text[Pos] <= '9');
				}
				else
				{
					OutError = "invalid number";
					return false;
				}
				if (Pos < Text.size() && Text[Pos] == '.')
				{
					++Pos;
					if (Pos >= Text.size() || Text[Pos] < '0' || Text[Pos] > '9')
					{
						OutError = "invalid number fraction";
						return false;
					}
					while (Pos < Text.size() && Text[Pos] >= '0' && Text[Pos] <= '9')
					{
						++Pos;
					}
				}
				if (Pos < Text.size() && (Text[Pos] == 'e' || Text[Pos] == 'E'))
				{
					++Pos;
					if (Pos < Text.size() && (Text[Pos] == '+' || Text[Pos] == '-'))
					{
						++Pos;
					}
					if (Pos >= Text.size() || Text[Pos] < '0' || Text[Pos] > '9')
					{
						OutError = "invalid number exponent";
						return false;
					}
					while (Pos < Text.size() && Text[Pos] >= '0' && Text[Pos] <= '9')
					{
						++Pos;
					}
				}
				double Number = 0.0;
				const char* const First = Text.data() + Begin;
				const char* const Last = Text.data() + Pos;
				const auto Parsed = FloatParsingDetail::FromCharsDecimal(std::string_view(First, Last - First), Number);
				if (Parsed.ec != std::errc{} || Parsed.ptr != Last)
				{
					OutError = "invalid number";
					return false;
				}
				Out.Kind = FJsonValue::EKind::Number;
				Out.NumberValue = Number;
				return true;
			}

			static constexpr std::size_t MaxJsonDepth = 256;
			std::string_view Text;
			std::size_t Pos = 0;
			std::size_t Depth = 0;
			std::size_t ValueCount = 0;
		};

		bool ReadStringField(
		    const FJsonValue& Object,
		    const char* Key,
		    std::string& Out,
		    const bool bRequired,
		    std::string& OutError)
		{
			const FJsonValue* Field = Object.Find(Key);
			if (!Field)
			{
				if (bRequired)
				{
					OutError = std::string("missing field: ") + Key;
					return false;
				}
				Out.clear();
				return true;
			}
			if (Field->Kind != FJsonValue::EKind::String)
			{
				OutError = std::string("field is not a string: ") + Key;
				return false;
			}
			Out = Field->StringValue;
			return true;
		}

		bool ReadNativeTypeArray(
		    const FJsonValue& Object,
		    const char* Key,
		    std::vector<ENativeType>& Out,
		    std::string& OutError)
		{
			Out.clear();
			const FJsonValue* Field = Object.Find(Key);
			if (!Field)
			{
				return true;
			}
			if (Field->Kind != FJsonValue::EKind::Array)
			{
				OutError = std::string("field is not an array: ") + Key;
				return false;
			}
			for (const FJsonValue& Entry : Field->ArrayValue)
			{
				if (Entry.Kind != FJsonValue::EKind::String)
				{
					OutError = std::string("non-string entry in ") + Key;
					return false;
				}
				ENativeType Type = ENativeType::Void;
				if (!ParseNativeType(Entry.StringValue, Type))
				{
					OutError = "unknown native type: " + Entry.StringValue;
					return false;
				}
				Out.push_back(Type);
			}
			return true;
		}

		bool ReadAnnotationInstanceArrayValue(
		    const FJsonValue& Field,
		    std::vector<FHostAnnotationInstance>& Out,
		    std::string& OutError)
		{
			Out.clear();
			if (Field.Kind != FJsonValue::EKind::Array)
			{
				OutError = "declaration annotations must be an array";
				return false;
			}
			for (const FJsonValue& AnnotationJson : Field.ArrayValue)
			{
				if (AnnotationJson.Kind != FJsonValue::EKind::Object)
				{
					OutError = "declaration annotation entry must be an object";
					return false;
				}
				FHostAnnotationInstance Annotation;
				if (!ReadStringField(
				        AnnotationJson, "name", Annotation.Name, true, OutError))
				{
					return false;
				}
				if (const FJsonValue* Arguments = AnnotationJson.Find("arguments"))
				{
					if (Arguments->Kind != FJsonValue::EKind::Array)
					{
						OutError = "declaration annotation arguments must be an array";
						return false;
					}
					for (const FJsonValue& ArgumentJson : Arguments->ArrayValue)
					{
						if (ArgumentJson.Kind != FJsonValue::EKind::Object)
						{
							OutError =
							    "declaration annotation argument must be an object";
							return false;
						}
						FHostAnnotationArgument Argument;
						if (!ReadStringField(
						        ArgumentJson, "name", Argument.Name, true, OutError))
						{
							return false;
						}
						if (const FJsonValue* Value = ArgumentJson.Find("value"))
						{
							if (Value->Kind != FJsonValue::EKind::String)
							{
								OutError =
								    "declaration annotation argument value must be a string";
								return false;
							}
							Argument.Value = Value->StringValue;
						}
						Annotation.Arguments.push_back(std::move(Argument));
					}
				}
				Out.push_back(std::move(Annotation));
			}
			return true;
		}

		bool ReadAnnotationInstances(
		    const FJsonValue& Object,
		    const char* Key,
		    std::vector<FHostAnnotationInstance>& Out,
		    std::string& OutError)
		{
			const FJsonValue* Field = Object.Find(Key);
			if (!Field)
			{
				Out.clear();
				return true;
			}
			return ReadAnnotationInstanceArrayValue(*Field, Out, OutError);
		}

		bool ReadParameterAnnotationInstances(
		    const FJsonValue& Object,
		    const char* Key,
		    std::vector<std::vector<FHostAnnotationInstance>>& Out,
		    std::string& OutError)
		{
			Out.clear();
			const FJsonValue* Field = Object.Find(Key);
			if (!Field)
			{
				return true;
			}
			if (Field->Kind != FJsonValue::EKind::Array)
			{
				OutError = std::string("field is not an array: ") + Key;
				return false;
			}
			Out.reserve(Field->ArrayValue.size());
			for (const FJsonValue& Entry : Field->ArrayValue)
			{
				std::vector<FHostAnnotationInstance> Annotations;
				if (!ReadAnnotationInstanceArrayValue(Entry, Annotations, OutError))
				{
					return false;
				}
				Out.push_back(std::move(Annotations));
			}
			return true;
		}

		bool ReadStringArray(
		    const FJsonValue& Object,
		    const char* Key,
		    std::vector<std::string>& Out,
		    std::string& OutError)
		{
			Out.clear();
			const FJsonValue* Field = Object.Find(Key);
			if (!Field)
			{
				return true;
			}
			if (Field->Kind != FJsonValue::EKind::Array)
			{
				OutError = std::string("field is not an array: ") + Key;
				return false;
			}
			for (const FJsonValue& Entry : Field->ArrayValue)
			{
				if (Entry.Kind != FJsonValue::EKind::String)
				{
					OutError = std::string("non-string entry in ") + Key;
					return false;
				}
				Out.push_back(Entry.StringValue);
			}
			return true;
		}

		bool ReadUInt8Array(
		    const FJsonValue& Object,
		    const char* Key,
		    std::vector<std::uint8_t>& Out,
		    std::string& OutError)
		{
			Out.clear();
			const FJsonValue* Field = Object.Find(Key);
			if (!Field)
			{
				return true;
			}
			if (Field->Kind != FJsonValue::EKind::Array)
			{
				OutError = std::string("field is not an array: ") + Key;
				return false;
			}
			for (const FJsonValue& Entry : Field->ArrayValue)
			{
				if (Entry.Kind != FJsonValue::EKind::Number)
				{
					OutError = std::string("non-number entry in ") + Key;
					return false;
				}
				if (Entry.NumberValue < 0.0 || Entry.NumberValue > 255.0 || std::trunc(Entry.NumberValue) != Entry.NumberValue)
				{
					OutError = std::string("non-integer or out-of-range entry in ") + Key;
					return false;
				}
				Out.push_back(static_cast<std::uint8_t>(Entry.NumberValue));
			}
			return true;
		}

		// Optional JSON bool; missing -> *OutValue unchanged (caller sets default).
		bool ReadOptionalBoolField(
		    const FJsonValue& Object,
		    const char* Key,
		    bool& OutValue,
		    std::string& OutError)
		{
			const FJsonValue* Field = Object.Find(Key);
			if (!Field)
			{
				return true;
			}
			if (Field->Kind != FJsonValue::EKind::Bool)
			{
				OutError = std::string("field is not a boolean: ") + Key;
				return false;
			}
			OutValue = Field->BoolValue;
			return true;
		}

		bool ReadOptionalArrayField(
		    const FJsonValue& Object,
		    const char* Key,
		    const FJsonValue*& OutValue,
		    std::string& OutError)
		{
			OutValue = Object.Find(Key);
			if (!OutValue)
			{
				return true;
			}
			if (OutValue->Kind != FJsonValue::EKind::Array)
			{
				OutError = std::string("field is not an array: ") + Key;
				return false;
			}
			return true;
		}

		bool ReadOrigin(const FJsonValue& Object, FBindingOrigin& Origin, std::string& Error)
		{
			Origin = {};
			const auto* Value = Object.Find("origin");
			if (!Value) return true;
			if (Value->Kind != FJsonValue::EKind::Object)
			{
				Error = "binding origin must be an object";
				return false;
			}
			return ReadStringField(*Value, "providerId", Origin.ProviderId, false, Error) &&
			    ReadStringField(*Value, "modulePath", Origin.ModulePath, false, Error) &&
			    ReadOptionalBoolField(*Value, "exported", Origin.bExported, Error);
		}

		bool ApplyImportedTypeFlags(
		    FHostBindingRegistry& Registry,
		    const FHostTypeId TypeId,
		    const bool bInheritable,
		    const bool bScopedEnum,
		    const std::string& EnumUnderlyingTypeName,
		    const bool bHasClone,
		    const bool bHasEquals,
		    const bool bHasHash,
		    const bool bHasSubclass,
		    std::string& OutErrorMessage)
		{
			std::string ErrorId;
			if (!EnumUnderlyingTypeName.empty() && !Registry.SetEnumUnderlyingType(TypeId, EnumUnderlyingTypeName, ErrorId))
			{
				OutErrorMessage = ErrorId.empty()
				    ? "SetEnumUnderlyingType failed"
				    : ErrorId;
				return false;
			}
			if (bScopedEnum && !Registry.SetEnumScoped(TypeId, true, ErrorId))
			{
				OutErrorMessage = ErrorId.empty() ? "SetEnumScoped failed" : ErrorId;
				return false;
			}
			if (bInheritable && !Registry.SetTypeInheritable(TypeId, true, ErrorId))
			{
				OutErrorMessage = ErrorId.empty() ? "SetTypeInheritable failed" : ErrorId;
				return false;
			}
			if (bHasClone && !Registry.RegisterTypeClone(TypeId, BindingCatalogStubNative, nullptr, ErrorId))
			{
				OutErrorMessage = ErrorId.empty() ? "RegisterTypeClone failed" : ErrorId;
				return false;
			}
			if (bHasEquals && !Registry.RegisterTypeEquals(TypeId, BindingCatalogStubNative, nullptr, ErrorId))
			{
				OutErrorMessage = ErrorId.empty() ? "RegisterTypeEquals failed" : ErrorId;
				return false;
			}
			if (bHasHash && !Registry.RegisterTypeHash(TypeId, BindingCatalogStubNative, nullptr, ErrorId))
			{
				OutErrorMessage = ErrorId.empty() ? "RegisterTypeHash failed" : ErrorId;
				return false;
			}
			if (bHasSubclass && !Registry.RegisterTypeSubclassFactory(TypeId, BindingCatalogStubNative, nullptr, ErrorId))
			{
				OutErrorMessage =
				    ErrorId.empty() ? "RegisterTypeSubclassFactory failed" : ErrorId;
				return false;
			}
			return true;
		}
	}

	bool BindingCatalogStubNative(
	    const FNativeCallContext& Context,
	    FValue& OutReturn,
	    std::string& OutError)
	{
		(void)Context;
		(void)OutReturn;
		OutError = "host native from binding catalog is not linked for execution"
		           " (provide a live host registry to run host APIs)";
		return false;
	}

	bool ExportBindingCatalogJson(
	    const IHostBindingCatalog& Catalog,
	    std::string& OutJson)
	try
	{
		OutJson = "{\n  \"version\": 1,\n  \"functions\": [\n";
		const auto& Functions = Catalog.GetFunctions();
		const auto& Types = Catalog.GetTypes();
		const auto& TypeAliases = Catalog.GetTypeAliases();
		const auto& GenericTemplates = Catalog.GetGenericTypeTemplates();
		bool bEmittedFunction = false;
		for (std::size_t Index = 0; Index < Functions.size(); ++Index)
		{
			const FHostFunctionDesc& Function = Functions[Index];
			// Never export ctor-shaped entries that collide with a host type name.
			bool bIsHostTypeName = false;
			for (const FHostTypeDesc& Type : Types)
			{
				if (Type.Name == Function.Name)
				{
					bIsHostTypeName = true;
					break;
				}
			}
			if (bIsHostTypeName)
			{
				continue;
			}
			if (bEmittedFunction)
			{
				OutJson += ",\n";
			}
			bEmittedFunction = true;
			OutJson += "    {\n      \"name\": \"";
			OutJson += EscapeJsonString(Function.Name);
			OutJson += "\",\n      \"origin\": ";
			AppendOrigin(OutJson, Function.Origin);
			OutJson += ",\n      \"";
			OutJson += "documentation\": \"";
			OutJson += EscapeJsonString(Function.Documentation);
			OutJson += "\",\n      \"annotations\": ";
			AppendAnnotationInstances(OutJson, Function.Annotations);
			OutJson += ",\n      \"paramAnnotations\": ";
			AppendParameterAnnotationInstances(OutJson, Function.ParamAnnotations);
			OutJson += ",\n      \"paramNames\": ";
			AppendStringArray(OutJson, Function.ParamNames);
			OutJson += ",\n      \"paramDocumentation\": ";
			AppendStringArray(OutJson, Function.ParamDocumentation);
			OutJson += ",\n      \"returnDocumentation\": \"";
			OutJson += EscapeJsonString(Function.ReturnDocumentation);
			OutJson += "\",\n      \"returnType\": \"";
			OutJson += NativeTypeName(Function.ReturnType);
			OutJson += "\",\n      \"paramTypes\": ";
			AppendNativeTypeArray(OutJson, Function.ParamTypes);
			OutJson += ",\n      \"returnHostTypeName\": \"";
			OutJson += EscapeJsonString(Function.ReturnHostTypeName);
			OutJson += "\",\n      \"paramHostTypeNames\": ";
			AppendStringArray(OutJson, Function.ParamHostTypeNames);
			OutJson += ",\n      \"returnElementTypeName\": \"";
			OutJson += EscapeJsonString(Function.ReturnElementTypeName);
			OutJson += "\",\n      \"paramElementTypeNames\": ";
			AppendStringArray(OutJson, Function.ParamElementTypeNames);
			OutJson += ",\n      \"returnKeyTypeName\": \"";
			OutJson += EscapeJsonString(Function.ReturnKeyTypeName);
			OutJson += "\",\n      \"returnValueTypeName\": \"";
			OutJson += EscapeJsonString(Function.ReturnValueTypeName);
			OutJson += "\",\n      \"paramKeyTypeNames\": ";
			AppendStringArray(OutJson, Function.ParamKeyTypeNames);
			OutJson += ",\n      \"paramValueTypeNames\": ";
			AppendStringArray(OutJson, Function.ParamValueTypeNames);
			OutJson += ",\n      \"paramFlags\": ";
			AppendUInt8Array(OutJson, Function.ParamFlags);
			OutJson += ",\n      \"typeParams\": ";
			AppendStringArray(OutJson, Function.TypeParams);
			OutJson += ",\n      \"scriptReturnTypeName\": \"";
			OutJson += EscapeJsonString(Function.ScriptReturnTypeName);
			OutJson += "\",\n      \"genericHostObjectCast\": ";
			OutJson += Function.bGenericHostObjectCast ? "true" : "false";
			OutJson += "\n    }";
		}
		OutJson += "\n  ],\n  \"genericTypeTemplates\": [\n";
		for (std::size_t Index = 0; Index < GenericTemplates.size(); ++Index)
		{
			if (Index > 0)
			{
				OutJson += ",\n";
			}
			const FGenericHostTypeTemplateDesc& Template = GenericTemplates[Index];
			OutJson += "    {\n      \"name\": \"";
			OutJson += EscapeJsonString(Template.Name);
			OutJson += "\",\n      \"origin\": ";
			AppendOrigin(OutJson, Template.Origin);
			OutJson += ",\n      \"";
			OutJson += "arity\": ";
			OutJson += std::to_string(Template.Arity);
			OutJson += ",\n      \"documentation\": \"";
			OutJson += EscapeJsonString(Template.Documentation);
			OutJson += "\"\n    }";
		}
		OutJson += "\n  ],\n  \"types\": [\n";
		for (std::size_t TypeIndex = 0; TypeIndex < Types.size(); ++TypeIndex)
		{
			const FHostTypeDesc& Type = Types[TypeIndex];
			if (TypeIndex > 0)
			{
				OutJson += ",\n";
			}
			OutJson += "    {\n      \"name\": \"";
			OutJson += EscapeJsonString(Type.Name);
			OutJson += "\",\n      \"origin\": ";
			AppendOrigin(OutJson, Type.Origin);
			OutJson += ",\n      \"";
			OutJson += "documentation\": \"";
			OutJson += EscapeJsonString(Type.Documentation);
			OutJson += "\",\n      \"annotations\": ";
			AppendAnnotationInstances(OutJson, Type.Annotations);
			OutJson += ",\n      \"kind\": \"";
			OutJson += HostTypeKindName(Type.Kind);
			OutJson += "\",\n      \"inheritable\": ";
			OutJson += Type.bInheritable ? "true" : "false";
			OutJson += ",\n      \"scopedEnum\": ";
			OutJson += Type.bScopedEnum ? "true" : "false";
			OutJson += ",\n      \"enumUnderlyingType\": \"";
			OutJson += EscapeJsonString(
			    Type.Kind == EHostTypeKind::Enum
			        ? Type.EnumUnderlyingTypeName
			        : std::string{});
			OutJson += "\"";
			OutJson += ",\n      \"genericTemplateName\": \"";
			if (Type.GenericTemplateId.IsValid())
			{
				for (const FGenericHostTypeTemplateDesc& Template : GenericTemplates)
				{
					if (Template.Id == Type.GenericTemplateId)
					{
						OutJson += EscapeJsonString(Template.Name);
						break;
					}
				}
			}
			OutJson += "\",\n      \"genericTypeArguments\": ";
			AppendStringArray(OutJson, Type.GenericTypeArguments);
			OutJson += ",\n      \"hasClone\": ";
			OutJson += Type.CloneCallback != nullptr ? "true" : "false";
			OutJson += ",\n      \"hasEquals\": ";
			OutJson += Type.EqualsCallback != nullptr ? "true" : "false";
			OutJson += ",\n      \"hasHash\": ";
			OutJson += Type.HashCallback != nullptr ? "true" : "false";
			OutJson += ",\n      \"hasSubclass\": ";
			OutJson += Type.SubclassFactory != nullptr ? "true" : "false";
			OutJson += ",\n      \"members\": [\n";
			for (std::size_t MemberIndex = 0; MemberIndex < Type.Members.size(); ++MemberIndex)
			{
				const FHostMemberDesc& Member = Type.Members[MemberIndex];
				if (MemberIndex > 0)
				{
					OutJson += ",\n";
				}
				OutJson += "        {\n          \"kind\": \"";
				OutJson += MemberKindName(Member.Kind);
				OutJson += "\",\n          \"name\": \"";
				OutJson += EscapeJsonString(Member.Name);
				OutJson += "\",\n          \"documentation\": \"";
				OutJson += EscapeJsonString(Member.Documentation);
				OutJson += "\",\n          \"annotations\": ";
				AppendAnnotationInstances(OutJson, Member.Annotations);
				OutJson += ",\n          \"paramAnnotations\": ";
				AppendParameterAnnotationInstances(OutJson, Member.ParamAnnotations);
				OutJson += ",\n          \"paramNames\": ";
				AppendStringArray(OutJson, Member.ParamNames);
				OutJson += ",\n          \"paramDocumentation\": ";
				AppendStringArray(OutJson, Member.ParamDocumentation);
				OutJson += ",\n          \"returnDocumentation\": \"";
				OutJson += EscapeJsonString(Member.ReturnDocumentation);
				OutJson += "\",\n          \"returnType\": \"";
				OutJson += NativeTypeName(Member.ReturnType);
				OutJson += "\",\n          \"paramTypes\": ";
				AppendNativeTypeArray(OutJson, Member.ParamTypes);
				OutJson += ",\n          \"enumValue\": ";
				OutJson += std::to_string(Member.EnumValue);
				OutJson += ",\n          \"enumValueInt64\": \"";
				OutJson += std::to_string(Member.EnumValue);
				OutJson += "\"";
				OutJson += ",\n          \"returnHostTypeName\": \"";
				OutJson += EscapeJsonString(Member.ReturnHostTypeName);
				OutJson += "\",\n          \"paramHostTypeNames\": ";
				AppendStringArray(OutJson, Member.ParamHostTypeNames);
				OutJson += ",\n          \"returnElementTypeName\": \"";
				OutJson += EscapeJsonString(Member.ReturnElementTypeName);
				OutJson += "\",\n          \"paramElementTypeNames\": ";
				AppendStringArray(OutJson, Member.ParamElementTypeNames);
				OutJson += ",\n          \"returnKeyTypeName\": \"";
				OutJson += EscapeJsonString(Member.ReturnKeyTypeName);
				OutJson += "\",\n          \"returnValueTypeName\": \"";
				OutJson += EscapeJsonString(Member.ReturnValueTypeName);
				OutJson += "\",\n          \"paramKeyTypeNames\": ";
				AppendStringArray(OutJson, Member.ParamKeyTypeNames);
				OutJson += ",\n          \"paramValueTypeNames\": ";
				AppendStringArray(OutJson, Member.ParamValueTypeNames);
				OutJson += ",\n          \"paramFlags\": ";
				AppendUInt8Array(OutJson, Member.ParamFlags);
				OutJson += ",\n          \"operator\": ";
				OutJson += Member.bOperator ? "true" : "false";
				OutJson += ",\n          \"constMethod\": ";
				OutJson += Member.bConstMethod ? "true" : "false";
				OutJson += ",\n          \"mutableThis\": ";
				OutJson += Member.bMutableThis ? "true" : "false";
				OutJson += ",\n          \"nativeName\": \"";
				OutJson += EscapeJsonString(Member.NativeName);
				OutJson += "\"";
				OutJson += "\n        }";
			}
			OutJson += "\n      ]\n    }";
		}
		OutJson += "\n  ],\n  \"typeAliases\": [\n";
		for (std::size_t Index = 0; Index < TypeAliases.size(); ++Index)
		{
			if (Index > 0)
			{
				OutJson += ",\n";
			}
			const FHostTypeAliasDesc& Alias = TypeAliases[Index];
			OutJson += "    {\n      \"name\": \"";
			OutJson += EscapeJsonString(Alias.Name);
			OutJson += "\",\n      \"origin\": ";
			AppendOrigin(OutJson, Alias.Origin);
			OutJson += ",\n      \"";
			OutJson += "typeParams\": ";
			AppendStringArray(OutJson, Alias.TypeParams);
			OutJson += ",\n      \"targetTypeName\": \"";
			OutJson += EscapeJsonString(Alias.TargetTypeName);
			OutJson += "\",\n      \"documentation\": \"";
			OutJson += EscapeJsonString(Alias.Documentation);
			OutJson += "\"\n    }";
		}
		OutJson += "\n  ],\n  \"annotations\": [\n";
		const auto& Annotations = Catalog.GetAnnotations();
		for (std::size_t Index = 0; Index < Annotations.size(); ++Index)
		{
			if (Index > 0)
			{
				OutJson += ",\n";
			}
			OutJson += "    {\n      \"name\": \"";
			OutJson += EscapeJsonString(Annotations[Index].Name);
			OutJson += "\",\n      \"detail\": \"";
			OutJson += EscapeJsonString(Annotations[Index].Detail);
			OutJson += "\",\n      \"documentation\": \"";
			OutJson += EscapeJsonString(Annotations[Index].Documentation);
			OutJson += "\",\n      \"targets\": " + std::to_string(Annotations[Index].Targets);
			OutJson += ",\n      \"caseSensitive\": " + std::string(Annotations[Index].CaseSensitive ? "true" : "false");
			OutJson += ",\n      \"arguments\": [";
			std::size_t DescriptionCount = 0;
			std::function<bool(const std::vector<FHostAnnotationArgumentDesc>&, std::size_t)> WriteArguments;
			WriteArguments = [&](const auto& Args, std::size_t Depth)
			{
				if (Depth > 64 || Args.size() > 65536 - DescriptionCount) return false;
				DescriptionCount += Args.size();
				for (std::size_t I = 0; I < Args.size(); ++I)
				{
					if (I)
					{
						OutJson += ",";
					}

					const auto& A = Args[I];
					OutJson += "{\"name\":\"" + EscapeJsonString(A.Name) + "\",\"detail\":\"" + EscapeJsonString(A.Detail) +
					    "\",\"documentation\":\"" + EscapeJsonString(A.Documentation) + "\",\"children\":[";
					if (!WriteArguments(A.Children, Depth + 1)) return false;
					OutJson += "]}";
				}
				return true;
			};
			if (!WriteArguments(Annotations[Index].Arguments, 0))
			{
				OutJson.clear();
				return false;
			}
			if (!Annotations[Index].Arguments.empty())
			{
				OutJson += "\n      ";
			}
			OutJson += "]\n    }";
		}
		OutJson += "\n  ]\n}\n";
		return true;
	}
	catch (const std::bad_alloc&)
	{
		OutJson.clear();
		return false;
	}
	catch (const std::length_error&)
	{
		OutJson.clear();
		return false;
	}

	bool ImportBindingCatalogJson(
	    const std::string_view Json,
	    FHostBindingRegistry& Registry,
	    std::string& OutErrorMessage,
	    const bool bClearFirst)
	try
	{
		OutErrorMessage.clear();
		if (Json.size() > MaxBindingCatalogBytes)
		{
			OutErrorMessage = "binding catalog exceeds safety limit";
			return false;
		}
		FMiniJsonParser Parser(Json);
		FJsonValue Root;
		if (!Parser.Parse(Root, OutErrorMessage))
		{
			return false;
		}
		if (Root.Kind != FJsonValue::EKind::Object)
		{
			OutErrorMessage = "catalog root must be an object";
			return false;
		}
		if (const FJsonValue* Version = Root.Find("version"))
		{
			if (Version->Kind != FJsonValue::EKind::Number || Version->NumberValue != 1.0)
			{
				OutErrorMessage = "binding catalog version must be the integer 1";
				return false;
			}
		}

		// Build transactionally. Append mode must resolve signatures against existing
		// dependency types while retaining their callbacks and registration owners.
		FHostBindingRegistry Staging = bClearFirst ? FHostBindingRegistry{} : Registry;
		FHostBindingRegistry& Dest = Staging;

		struct FPendingMember
		{
			std::string TypeName;
			FHostMemberDesc Member;
		};
		std::vector<FPendingMember> PendingMembers;

		const FJsonValue* Templates = nullptr;
		if (!ReadOptionalArrayField(
		        Root, "genericTypeTemplates", Templates, OutErrorMessage))
		{
			return false;
		}
		if (Templates)
		{
			for (const FJsonValue& TemplateJson : Templates->ArrayValue)
			{
				if (TemplateJson.Kind != FJsonValue::EKind::Object)
				{
					OutErrorMessage = "generic type template entry must be an object";
					return false;
				}
				FBindingOrigin Origin;
				if (!ReadOrigin(TemplateJson, Origin, OutErrorMessage)) return false;
				Dest.SetRegistrationOwner({}, std::move(Origin));
				std::string Name;
				std::string Documentation;
				if (!ReadStringField(TemplateJson, "name", Name, true, OutErrorMessage) || !ReadStringField(TemplateJson, "documentation", Documentation, false, OutErrorMessage))
				{
					return false;
				}
				const FJsonValue* ArityJson = TemplateJson.Find("arity");
				if (!ArityJson || ArityJson->Kind != FJsonValue::EKind::Number || ArityJson->NumberValue < 1.0 || ArityJson->NumberValue > 255.0 || static_cast<double>(static_cast<std::size_t>(ArityJson->NumberValue)) != ArityJson->NumberValue)
				{
					OutErrorMessage = "generic type template arity must be an integer from 1 to 255";
					return false;
				}
				FGenericHostTypeTemplateId TemplateId;
				std::string ErrorId;
				if (!Dest.RegisterGenericHostTypeTemplateMetadata(
				        std::move(Name),
				        static_cast<std::size_t>(ArityJson->NumberValue),
				        TemplateId,
				        ErrorId,
				        std::move(Documentation)))
				{
					OutErrorMessage = ErrorId.empty()
					    ? "RegisterGenericHostTypeTemplateMetadata failed"
					    : ErrorId;
					return false;
				}
			}
		}

		const FJsonValue* TypeAliases = nullptr;
		if (!ReadOptionalArrayField(Root, "typeAliases", TypeAliases, OutErrorMessage))
		{
			return false;
		}
		if (TypeAliases)
		{
			for (const FJsonValue& AliasJson : TypeAliases->ArrayValue)
			{
				if (AliasJson.Kind != FJsonValue::EKind::Object)
				{
					OutErrorMessage = "type alias entry must be an object";
					return false;
				}
				FBindingOrigin Origin;
				if (!ReadOrigin(AliasJson, Origin, OutErrorMessage)) return false;
				Dest.SetRegistrationOwner({}, std::move(Origin));
				std::string Name;
				std::vector<std::string> TypeParams;
				std::string TargetTypeName;
				std::string Documentation;
				if (!ReadStringField(AliasJson, "name", Name, true, OutErrorMessage) || !ReadStringArray(AliasJson, "typeParams", TypeParams, OutErrorMessage) || !ReadStringField(AliasJson, "targetTypeName", TargetTypeName, true, OutErrorMessage) || !ReadStringField(AliasJson, "documentation", Documentation, false, OutErrorMessage))
				{
					return false;
				}
				std::string ErrorId;
				if (!Dest.RegisterTypeAlias(
				        std::move(Name),
				        std::move(TypeParams),
				        std::move(TargetTypeName),
				        ErrorId,
				        std::move(Documentation)))
				{
					OutErrorMessage =
					    ErrorId.empty() ? "RegisterTypeAlias failed" : ErrorId;
					return false;
				}
			}
		}

		const FJsonValue* Types = nullptr;
		if (!ReadOptionalArrayField(Root, "types", Types, OutErrorMessage))
		{
			return false;
		}
		if (Types)
		{
			for (const FJsonValue& TypeJson : Types->ArrayValue)
			{
				if (TypeJson.Kind != FJsonValue::EKind::Object)
				{
					OutErrorMessage = "type entry must be an object";
					return false;
				}
				FBindingOrigin Origin;
				if (!ReadOrigin(TypeJson, Origin, OutErrorMessage)) return false;
				Dest.SetRegistrationOwner({}, std::move(Origin));
				std::string Name;
				std::string KindText;
				std::string Documentation;
				std::string GenericTemplateName;
				std::string EnumUnderlyingTypeName;
				std::vector<std::string> GenericTypeArguments;
				std::vector<FHostAnnotationInstance> DeclarationAnnotations;
				if (!ReadStringField(TypeJson, "name", Name, true, OutErrorMessage) || !ReadStringField(TypeJson, "kind", KindText, true, OutErrorMessage) || !ReadStringField(TypeJson, "documentation", Documentation, false, OutErrorMessage) || !ReadStringField(TypeJson, "genericTemplateName", GenericTemplateName, false, OutErrorMessage) || !ReadStringField(TypeJson, "enumUnderlyingType", EnumUnderlyingTypeName, false, OutErrorMessage) || !ReadStringArray(TypeJson, "genericTypeArguments", GenericTypeArguments, OutErrorMessage) || !ReadAnnotationInstances(TypeJson, "annotations", DeclarationAnnotations, OutErrorMessage))
				{
					return false;
				}
				EHostTypeKind Kind = EHostTypeKind::Class;
				if (!ParseTypeKind(KindText, Kind))
				{
					OutErrorMessage = "unknown host type kind: " + KindText;
					return false;
				}
				FHostTypeId TypeId;
				std::string ErrorId;
				if (!Dest.RegisterType(
				        Name,
				        Kind,
				        TypeId,
				        ErrorId,
				        std::move(DeclarationAnnotations)))
				{
					OutErrorMessage = ErrorId.empty() ? "RegisterType failed" : ErrorId;
					return false;
				}
				if (!Dest.SetTypeDocumentation(
				        TypeId, std::move(Documentation), ErrorId))
				{
					OutErrorMessage =
					    ErrorId.empty() ? "SetTypeDocumentation failed" : ErrorId;
					return false;
				}
				if (!GenericTemplateName.empty())
				{
					const FGenericHostTypeTemplateDesc* Template =
					    Dest.FindGenericTypeTemplate(GenericTemplateName);
					if (!Template || !Dest.SetTypeGenericInstanceInfo(TypeId, Template->Id, std::move(GenericTypeArguments), ErrorId))
					{
						OutErrorMessage = ErrorId.empty()
						    ? "unknown genericTemplateName: " + GenericTemplateName
						    : ErrorId;
						return false;
					}
				}
				bool bInheritable = false;
				bool bScopedEnum = false;
				bool bHasClone = false;
				bool bHasEquals = false;
				bool bHasHash = false;
				bool bHasSubclass = false;
				if (!ReadOptionalBoolField(
				        TypeJson, "inheritable", bInheritable, OutErrorMessage) ||
				    !ReadOptionalBoolField(
				        TypeJson, "scopedEnum", bScopedEnum, OutErrorMessage) ||
				    !ReadOptionalBoolField(TypeJson, "hasClone", bHasClone, OutErrorMessage) || !ReadOptionalBoolField(TypeJson, "hasEquals", bHasEquals, OutErrorMessage) || !ReadOptionalBoolField(TypeJson, "hasHash", bHasHash, OutErrorMessage) || !ReadOptionalBoolField(TypeJson, "hasSubclass", bHasSubclass, OutErrorMessage) || !ApplyImportedTypeFlags(Dest, TypeId, bInheritable, bScopedEnum, EnumUnderlyingTypeName, bHasClone, bHasEquals, bHasHash, bHasSubclass, OutErrorMessage))
				{
					return false;
				}

				const FJsonValue* Members = TypeJson.Find("members");
				if (!Members)
				{
					continue;
				}
				if (Members->Kind != FJsonValue::EKind::Array)
				{
					OutErrorMessage = "members must be an array";
					return false;
				}
				for (const FJsonValue& MemberJson : Members->ArrayValue)
				{
					if (MemberJson.Kind != FJsonValue::EKind::Object)
					{
						OutErrorMessage = "member entry must be an object";
						return false;
					}
					FPendingMember Pending;
					Pending.TypeName = Name;
					std::string KindMemberText;
					std::string ReturnTypeText;
					if (!ReadStringField(MemberJson, "kind", KindMemberText, true, OutErrorMessage) || !ReadStringField(MemberJson, "name", Pending.Member.Name, false, OutErrorMessage) || !ReadStringField(MemberJson, "documentation", Pending.Member.Documentation, false, OutErrorMessage) || !ReadStringArray(MemberJson, "paramNames", Pending.Member.ParamNames, OutErrorMessage) || !ReadStringArray(MemberJson, "paramDocumentation", Pending.Member.ParamDocumentation, OutErrorMessage) || !ReadStringField(MemberJson, "returnDocumentation", Pending.Member.ReturnDocumentation, false, OutErrorMessage) || !ReadAnnotationInstances(MemberJson, "annotations", Pending.Member.Annotations, OutErrorMessage) || !ReadParameterAnnotationInstances(MemberJson, "paramAnnotations", Pending.Member.ParamAnnotations, OutErrorMessage) || !ReadStringField(MemberJson, "returnType", ReturnTypeText, false, OutErrorMessage))
					{
						return false;
					}
					if (!ParseMemberKind(KindMemberText, Pending.Member.Kind))
					{
						OutErrorMessage = "unknown member kind: " + KindMemberText;
						return false;
					}
					if (!ReturnTypeText.empty() && !ParseNativeType(ReturnTypeText, Pending.Member.ReturnType))
					{
						OutErrorMessage = "unknown return type: " + ReturnTypeText;
						return false;
					}
					if (!ReadNativeTypeArray(
					        MemberJson, "paramTypes", Pending.Member.ParamTypes, OutErrorMessage) ||
					    !ReadStringField(
					        MemberJson,
					        "returnHostTypeName",
					        Pending.Member.ReturnHostTypeName,
					        false,
					        OutErrorMessage) ||
					    !ReadStringArray(
					        MemberJson,
					        "paramHostTypeNames",
					        Pending.Member.ParamHostTypeNames,
					        OutErrorMessage) ||
					    !ReadStringField(
					        MemberJson,
					        "returnElementTypeName",
					        Pending.Member.ReturnElementTypeName,
					        false,
					        OutErrorMessage) ||
					    !ReadStringArray(
					        MemberJson,
					        "paramElementTypeNames",
					        Pending.Member.ParamElementTypeNames,
					        OutErrorMessage) ||
					    !ReadStringField(
					        MemberJson,
					        "returnKeyTypeName",
					        Pending.Member.ReturnKeyTypeName,
					        false,
					        OutErrorMessage) ||
					    !ReadStringField(
					        MemberJson,
					        "returnValueTypeName",
					        Pending.Member.ReturnValueTypeName,
					        false,
					        OutErrorMessage) ||
					    !ReadStringArray(
					        MemberJson,
					        "paramKeyTypeNames",
					        Pending.Member.ParamKeyTypeNames,
					        OutErrorMessage) ||
					    !ReadStringArray(
					        MemberJson,
					        "paramValueTypeNames",
					        Pending.Member.ParamValueTypeNames,
					        OutErrorMessage) ||
					    !ReadUInt8Array(
					        MemberJson,
					        "paramFlags",
					        Pending.Member.ParamFlags,
					        OutErrorMessage) ||
					    !ReadOptionalBoolField(
					        MemberJson,
					        "operator",
					        Pending.Member.bOperator,
					        OutErrorMessage) ||
					    !ReadOptionalBoolField(
					        MemberJson,
					        "constMethod",
					        Pending.Member.bConstMethod,
					        OutErrorMessage) ||
					    !ReadOptionalBoolField(
					        MemberJson,
					        "mutableThis",
					        Pending.Member.bMutableThis,
					        OutErrorMessage) ||
					    !ReadStringField(
					        MemberJson,
					        "nativeName",
					        Pending.Member.NativeName,
					        false,
					        OutErrorMessage))
					{
						return false;
					}
					if (const FJsonValue* EnumValue64 =
					        MemberJson.Find("enumValueInt64"))
					{
						if (EnumValue64->Kind != FJsonValue::EKind::String)
						{
							OutErrorMessage = "enumValueInt64 must be a string";
							return false;
						}
						const std::string& Text = EnumValue64->StringValue;
						const auto [End, Error] = std::from_chars(
						    Text.data(),
						    Text.data() + Text.size(),
						    Pending.Member.EnumValue);
						if (Error != std::errc{} || End != Text.data() + Text.size())
						{
							OutErrorMessage = "invalid enumValueInt64";
							return false;
						}
					}
					else if (const FJsonValue* EnumValue = MemberJson.Find("enumValue"))
					{
						if (EnumValue->Kind != FJsonValue::EKind::Number)
						{
							OutErrorMessage = "enumValue must be a number";
							return false;
						}
						// JSON numbers are parsed as double. Values outside the exact integer
						// range must use enumValueInt64 so they cannot be silently rounded.
						constexpr double MaxExactJsonInteger = 9007199254740991.0;
						if (std::trunc(EnumValue->NumberValue) != EnumValue->NumberValue || EnumValue->NumberValue < -MaxExactJsonInteger || EnumValue->NumberValue > MaxExactJsonInteger)
						{
							OutErrorMessage =
							    "enumValue must be an exact JSON integer; use enumValueInt64"
							    " for full-width values";
							return false;
						}
						Pending.Member.EnumValue =
						    static_cast<std::int64_t>(EnumValue->NumberValue);
					}
					const bool bNeedsStub =
					    Pending.Member.Kind != EHostMemberKind::EnumMember && (Pending.Member.Kind == EHostMemberKind::Constructor || Pending.Member.Kind == EHostMemberKind::Method || Pending.Member.Kind == EHostMemberKind::StaticMethod || Pending.Member.Kind == EHostMemberKind::StaticConstant || Pending.Member.Kind == EHostMemberKind::PropertyGetter || Pending.Member.Kind == EHostMemberKind::PropertySetter || Pending.Member.Kind == EHostMemberKind::Field);
					if (bNeedsStub)
					{
						Pending.Member.Callback = BindingCatalogStubNative;
					}
					PendingMembers.push_back(std::move(Pending));
				}
			}
		}

		// Pass 2: members after all types exist (cross-type HostObject refs).
		for (FPendingMember& Pending : PendingMembers)
		{
			const FHostTypeDesc* Type = Dest.FindType(Pending.TypeName);
			if (!Type)
			{
				OutErrorMessage = "internal: missing type for member: " + Pending.TypeName;
				return false;
			}
			std::string ErrorId;
			Dest.SetRegistrationOwner({}, Type->Origin);
			if (!Dest.RegisterMember(Type->Id, std::move(Pending.Member), ErrorId))
			{
				OutErrorMessage = ErrorId.empty() ? "RegisterMember failed" : ErrorId;
				return false;
			}
		}

		const FJsonValue* Functions = nullptr;
		if (!ReadOptionalArrayField(Root, "functions", Functions, OutErrorMessage))
		{
			return false;
		}
		if (Functions)
		{
			for (const FJsonValue& FunctionJson : Functions->ArrayValue)
			{
				if (FunctionJson.Kind != FJsonValue::EKind::Object)
				{
					OutErrorMessage = "function entry must be an object";
					return false;
				}
				FBindingOrigin Origin;
				if (!ReadOrigin(FunctionJson, Origin, OutErrorMessage)) return false;
				Dest.SetRegistrationOwner({}, std::move(Origin));
				std::string Name;
				std::string Documentation;
				std::vector<std::string> ParamNames;
				std::vector<std::string> ParamDocumentation;
				std::string ReturnDocumentation;
				std::string ReturnTypeText;
				std::vector<ENativeType> ParamTypes;
				std::string ReturnHostTypeName;
				std::vector<std::string> ParamHostTypeNames;
				std::string ReturnElementTypeName;
				std::vector<std::string> ParamElementTypeNames;
				std::string ReturnKeyTypeName;
				std::string ReturnValueTypeName;
				std::vector<std::string> ParamKeyTypeNames;
				std::vector<std::string> ParamValueTypeNames;
				std::vector<std::uint8_t> ParamFlags;
				std::vector<std::string> TypeParams;
				std::string ScriptReturnTypeName;
				std::vector<FHostAnnotationInstance> DeclarationAnnotations;
				std::vector<std::vector<FHostAnnotationInstance>> ParamAnnotations;
				bool bGenericHostObjectCast = false;
				if (!ReadStringField(FunctionJson, "name", Name, true, OutErrorMessage) || !ReadStringField(FunctionJson, "documentation", Documentation, false, OutErrorMessage) || !ReadStringArray(FunctionJson, "paramNames", ParamNames, OutErrorMessage) || !ReadStringArray(FunctionJson, "paramDocumentation", ParamDocumentation, OutErrorMessage) || !ReadStringField(FunctionJson, "returnDocumentation", ReturnDocumentation, false, OutErrorMessage) || !ReadStringField(FunctionJson, "returnType", ReturnTypeText, true, OutErrorMessage) || !ReadNativeTypeArray(FunctionJson, "paramTypes", ParamTypes, OutErrorMessage) || !ReadStringField(FunctionJson, "returnHostTypeName", ReturnHostTypeName, false, OutErrorMessage) || !ReadStringArray(FunctionJson, "paramHostTypeNames", ParamHostTypeNames, OutErrorMessage) || !ReadStringField(FunctionJson, "returnElementTypeName", ReturnElementTypeName, false, OutErrorMessage) || !ReadStringArray(FunctionJson, "paramElementTypeNames", ParamElementTypeNames, OutErrorMessage) || !ReadStringField(FunctionJson, "returnKeyTypeName", ReturnKeyTypeName, false, OutErrorMessage) || !ReadStringField(FunctionJson, "returnValueTypeName", ReturnValueTypeName, false, OutErrorMessage) || !ReadStringArray(FunctionJson, "paramKeyTypeNames", ParamKeyTypeNames, OutErrorMessage) || !ReadStringArray(FunctionJson, "paramValueTypeNames", ParamValueTypeNames, OutErrorMessage) || !ReadUInt8Array(FunctionJson, "paramFlags", ParamFlags, OutErrorMessage) || !ReadStringArray(FunctionJson, "typeParams", TypeParams, OutErrorMessage) || !ReadStringField(FunctionJson, "scriptReturnTypeName", ScriptReturnTypeName, false, OutErrorMessage) || !ReadOptionalBoolField(FunctionJson, "genericHostObjectCast", bGenericHostObjectCast, OutErrorMessage) || !ReadAnnotationInstances(FunctionJson, "annotations", DeclarationAnnotations, OutErrorMessage) || !ReadParameterAnnotationInstances(FunctionJson, "paramAnnotations", ParamAnnotations, OutErrorMessage))
				{
					return false;
				}
				// Skip ctor-shaped free functions that duplicate a host type name
				// (legacy catalogs may still emit them).
				if (Dest.FindType(Name) != nullptr)
				{
					continue;
				}
				ENativeType ReturnType = ENativeType::Void;
				if (!ParseNativeType(ReturnTypeText, ReturnType))
				{
					OutErrorMessage = "unknown return type: " + ReturnTypeText;
					return false;
				}
				std::string ErrorId;
				FHostFunctionDesc Function;
				Function.Name = std::move(Name);
				Function.Documentation = std::move(Documentation);
				Function.ParamNames = std::move(ParamNames);
				Function.ParamDocumentation = std::move(ParamDocumentation);
				Function.ReturnDocumentation = std::move(ReturnDocumentation);
				Function.ReturnType = ReturnType;
				Function.ParamTypes = std::move(ParamTypes);
				Function.ReturnHostTypeName = std::move(ReturnHostTypeName);
				Function.ParamHostTypeNames = std::move(ParamHostTypeNames);
				Function.ReturnElementTypeName = std::move(ReturnElementTypeName);
				Function.ParamElementTypeNames = std::move(ParamElementTypeNames);
				Function.ReturnKeyTypeName = std::move(ReturnKeyTypeName);
				Function.ReturnValueTypeName = std::move(ReturnValueTypeName);
				Function.ParamKeyTypeNames = std::move(ParamKeyTypeNames);
				Function.ParamValueTypeNames = std::move(ParamValueTypeNames);
				Function.ParamFlags = std::move(ParamFlags);
				Function.TypeParams = std::move(TypeParams);
				Function.ScriptReturnTypeName = std::move(ScriptReturnTypeName);
				Function.bGenericHostObjectCast = bGenericHostObjectCast;
				Function.Annotations = std::move(DeclarationAnnotations);
				Function.ParamAnnotations = std::move(ParamAnnotations);
				if (!Dest.RegisterNative(
				        std::move(Function),
				        BindingCatalogStubNative,
				        nullptr,
				        ErrorId))
				{
					OutErrorMessage = ErrorId.empty() ? "RegisterNative failed" : ErrorId;
					return false;
				}
			}
		}

		const FJsonValue* Annotations = nullptr;
		if (!ReadOptionalArrayField(Root, "annotations", Annotations, OutErrorMessage))
		{
			return false;
		}
		if (Annotations)
		{
			for (const FJsonValue& AnnotationJson : Annotations->ArrayValue)
			{
				if (AnnotationJson.Kind != FJsonValue::EKind::Object)
				{
					OutErrorMessage = "annotation entry must be an object";
					return false;
				}
				std::string Name;
				std::string Detail;
				std::string Documentation;
				std::vector<FHostAnnotationArgumentDesc> Arguments;
				if (!ReadStringField(AnnotationJson, "name", Name, true, OutErrorMessage) || !ReadStringField(AnnotationJson, "detail", Detail, false, OutErrorMessage) || !ReadStringField(AnnotationJson, "documentation", Documentation, false, OutErrorMessage))
				{
					return false;
				}
				if (const FJsonValue* ArgumentValues = AnnotationJson.Find("arguments"))
				{
					if (ArgumentValues->Kind != FJsonValue::EKind::Array)
					{
						OutErrorMessage = "annotation arguments must be an array";
						return false;
					}
					std::size_t DescriptionCount = 0;
					std::function<bool(
					    const FJsonValue&, std::vector<FHostAnnotationArgumentDesc>&, std::size_t)>
					    ReadArguments;
					ReadArguments = [&](const auto& Values, auto& Into, std::size_t Depth)
					{
						if (Values.Kind != FJsonValue::EKind::Array || Depth > 64 ||
						    Values.ArrayValue.size() > 65536 - DescriptionCount)
						{
							OutErrorMessage = "invalid annotation description children or budget";
							return false;
						}
						DescriptionCount += Values.ArrayValue.size();
						for (const auto& V : Values.ArrayValue)
						{
							FHostAnnotationArgumentDesc A;
							if (V.Kind != FJsonValue::EKind::Object ||
							    !ReadStringField(V, "name", A.Name, true, OutErrorMessage) ||
							    !ReadStringField(V, "detail", A.Detail, false, OutErrorMessage) ||
							    !ReadStringField(V, "documentation", A.Documentation, false, OutErrorMessage))
							{
								return false;
							}

							if (const auto* Children = V.Find("children"))
							{
								if (!ReadArguments(*Children, A.Children, Depth + 1)) return false;
							}

							Into.push_back(std::move(A));
						}
						return true;
					};
					if (!ReadArguments(*ArgumentValues, Arguments, 0)) return false;
				}
				std::string ErrorId;
				std::uint32_t Targets = 131071;
				bool CaseSensitive = true;
				if (!ReadOptionalBoolField(AnnotationJson, "caseSensitive", CaseSensitive, OutErrorMessage))
				{
					return false;
				}
				if (const auto T = AnnotationJson.Find("targets"))
				{
					if (T->Kind != FJsonValue::EKind::Number || T->NumberValue < 1 ||
					    T->NumberValue > 131071 || std::trunc(T->NumberValue) != T->NumberValue)
					{
						OutErrorMessage = "invalid annotation targets";
						return false;
					}
					Targets = static_cast<std::uint32_t>(T->NumberValue);
				}
				if (!Dest.RegisterAnnotation(std::move(Name), std::move(Detail), ErrorId,
				        std::move(Documentation), std::move(Arguments), Targets, CaseSensitive))
				{
					OutErrorMessage = ErrorId.empty() ? "RegisterAnnotation failed" : ErrorId;
					return false;
				}
			}
		}

		Dest.SetRegistrationOwner({});
		Registry.Swap(Staging);
		return true;
	}
	catch (const std::bad_alloc&)
	{
		OutErrorMessage = "out of memory while importing binding catalog";
		return false;
	}
	catch (const std::length_error&)
	{
		OutErrorMessage = "binding catalog size overflow";
		return false;
	}

	bool SaveBindingCatalogFile(
	    const IHostBindingCatalog& Catalog,
	    const std::string_view FilePath,
	    std::string& OutErrorMessage)
	try
	{
		OutErrorMessage.clear();
		std::string Json;
		if (!ExportBindingCatalogJson(Catalog, Json))
		{
			OutErrorMessage = "failed to serialize binding catalog";
			return false;
		}
		if (Json.size() > MaxBindingCatalogBytes)
		{
			OutErrorMessage = "binding catalog exceeds safety limit";
			return false;
		}
		const std::filesystem::path FinalPath = PathUtil::FromUtf8(FilePath);
		const std::filesystem::path TempPath =
		    PathUtil::MakeTemporarySiblingPath(FinalPath);
		{
			std::ofstream Output(TempPath, std::ios::binary | std::ios::trunc);
			if (!Output)
			{
				OutErrorMessage = "cannot write temporary binding catalog: " + std::string(FilePath);
				return false;
			}
			Output.write(Json.data(), static_cast<std::streamsize>(Json.size()));
			if (!Output)
			{
				Output.close();
				std::error_code RemoveError;
				std::filesystem::remove(TempPath, RemoveError);
				OutErrorMessage = "failed while writing binding catalog: " + std::string(FilePath);
				return false;
			}
			Output.close();
			if (!Output)
			{
				std::error_code RemoveError;
				std::filesystem::remove(TempPath, RemoveError);
				OutErrorMessage = "failed while flushing binding catalog: " + std::string(FilePath);
				return false;
			}
		}

		std::error_code ReplaceError;
		if (!PathUtil::ReplaceFileAtomically(TempPath, FinalPath, ReplaceError))
		{
			std::error_code RemoveError;
			std::filesystem::remove(TempPath, RemoveError);
			OutErrorMessage = "failed to replace binding catalog: " + ReplaceError.message();
			return false;
		}
		return true;
	}
	catch (const std::exception& Exception)
	{
		OutErrorMessage = "failed to save binding catalog: ";
		OutErrorMessage += Exception.what();
		return false;
	}
	catch (...)
	{
		OutErrorMessage = "failed to save binding catalog";
		return false;
	}

	bool LoadBindingCatalogFile(
	    const std::string_view FilePath,
	    FHostBindingRegistry& Registry,
	    std::string& OutErrorMessage,
	    const bool bClearFirst)
	try
	{
		std::ifstream Input(PathUtil::FromUtf8(FilePath), std::ios::binary);
		if (!Input)
		{
			OutErrorMessage = "cannot open binding catalog: " + std::string(FilePath);
			return false;
		}
		Input.seekg(0, std::ios::end);
		const std::streampos EndPosition = Input.tellg();
		if (EndPosition < std::streampos{0} || static_cast<std::uint64_t>(EndPosition) > MaxBindingCatalogBytes || static_cast<std::uint64_t>(EndPosition) > static_cast<std::uint64_t>((std::numeric_limits<std::streamsize>::max)()))
		{
			OutErrorMessage = "binding catalog exceeds safety limit: " + std::string(FilePath);
			return false;
		}
		const std::size_t Size = static_cast<std::size_t>(EndPosition);
		std::string Buffer(Size, '\0');
		Input.seekg(0, std::ios::beg);
		if (Size != 0)
		{
			Input.read(Buffer.data(), static_cast<std::streamsize>(Size));
		}
		if (!Input || static_cast<std::size_t>(Input.gcount()) != Size || Input.peek() != std::char_traits<char>::eof())
		{
			OutErrorMessage = "failed while reading binding catalog: " + std::string(FilePath);
			return false;
		}
		return ImportBindingCatalogJson(Buffer, Registry, OutErrorMessage, bClearFirst);
	}
	catch (const std::exception& Exception)
	{
		OutErrorMessage = "failed to load binding catalog: ";
		OutErrorMessage += Exception.what();
		return false;
	}
	catch (...)
	{
		OutErrorMessage = "failed to load binding catalog";
		return false;
	}
}
