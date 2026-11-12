// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptModuleMetaInternal.h"

#include "VeroScriptMethodKey.h"
#include "VeroScriptNative.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace VeroScript
{
	namespace
	{
		std::string SourceTemplateName(const FBytecodeModule& Module,
		    const FBytecodeFunction& Function, std::string Fallback)
		{
			if (Function.TemplateInstance.Arguments.empty()) return Fallback;
			const auto Declaration = std::find_if(Module.TemplateDeclarations.begin(), Module.TemplateDeclarations.end(),
			    [&](const auto& Candidate)
			{
				return Candidate.Id == Function.TemplateInstance.DeclarationId;
			});
			return Declaration == Module.TemplateDeclarations.end() ? Fallback : Declaration->Name;
		}

		bool IsInternalSignatureMeta(const std::string_view Key)
		{
			return Key == DeclaredReturnTypeMetaKey || Key.rfind(DeclaredParamTypeMetaPrefix, 0) == 0;
		}

		FDeclMeta PublicDeclMeta(const FDeclMeta& Meta)
		{
			FDeclMeta Result = Meta;
			Result.erase(std::remove_if(Result.begin(), Result.end(),
			                 [](const auto& Pair)
			{
				return IsInternalSignatureMeta(Pair.first);
			}),
			    Result.end());
			return Result;
		}

		std::string LinkageModulePath(const std::string_view Linkage)
		{
			const std::size_t IdentityStart = FindLinkageIdentityStart(Linkage);
			return IdentityStart == 0
			    ? std::string{}
			    : std::string(Linkage.substr(0, IdentityStart - 1));
		}

		bool IsVisibleFromEntry(
		    const FBytecodeModule& Module,
		    const std::string_view Linkage)
		{
			const std::size_t IdentityStart = FindLinkageIdentityStart(Linkage);
			return IdentityStart == 0 || Module.EntryModulePath.empty() || Linkage.substr(0, IdentityStart - 1) == Module.EntryModulePath;
		}

		std::string MakeGenericTypeEncodingUserFacing(
		    std::string_view TypeName,
		    std::size_t Nesting = 0);

		std::string MakeDelimitedTypeListUserFacing(
		    const std::string_view TypeList,
		    const std::size_t Nesting)
		{
			std::string Result;
			std::size_t PartStart = 0;
			int AngleDepth = 0;
			int ParenthesisDepth = 0;
			for (std::size_t Index = 0; Index <= TypeList.size(); ++Index)
			{
				const bool bAtEnd = Index == TypeList.size();
				const char Character = bAtEnd ? '\0' : TypeList[Index];
				if (!bAtEnd)
				{
					if (Character == '<')
					{
						++AngleDepth;
					}
					else if (Character == '>' && AngleDepth > 0)
					{
						--AngleDepth;
					}
					else if (Character == '(')
					{
						++ParenthesisDepth;
					}
					else if (Character == ')' && ParenthesisDepth > 0)
					{
						--ParenthesisDepth;
					}
				}
				if (bAtEnd || (Character == ',' && AngleDepth == 0 && ParenthesisDepth == 0))
				{
					if (!Result.empty())
					{
						Result.push_back(',');
					}
					Result += MakeGenericTypeEncodingUserFacing(
					    TypeList.substr(PartStart, Index - PartStart), Nesting + 1);
					PartStart = Index + 1;
				}
			}
			return Result;
		}

		std::string MakeGenericTypeEncodingUserFacing(
		    const std::string_view TypeName,
		    const std::size_t Nesting)
		{
			if (TypeName.substr(0, 7) == "@value:") return FormatTemplateArgumentName(TypeName);
			// Generic script instances use `Open#len:Arg...` as their stable runtime
			// identity. Reflection signatures are user-facing and must decode that
			// representation without changing the stable identity stored on the type.
			constexpr std::size_t MaxDisplayTypeNesting = 64;
			if (Nesting > MaxDisplayTypeNesting)
			{
				return std::string(TypeName);
			}
			int AngleDepth = 0;
			int ParenthesisDepth = 0;
			std::size_t Marker = std::string_view::npos;
			for (std::size_t Index = 0; Index < TypeName.size(); ++Index)
			{
				const char Character = TypeName[Index];
				if (Character == '<')
				{
					++AngleDepth;
				}
				else if (Character == '>' && AngleDepth > 0)
				{
					--AngleDepth;
				}
				else if (Character == '(')
				{
					++ParenthesisDepth;
				}
				else if (Character == ')' && ParenthesisDepth > 0)
				{
					--ParenthesisDepth;
				}
				else if (Character == '#' && AngleDepth == 0 && ParenthesisDepth == 0)
				{
					Marker = Index;
					break;
				}
			}

			if (Marker != std::string_view::npos && Marker + 1 < TypeName.size())
			{
				std::vector<std::string_view> Arguments;
				std::size_t Position = Marker + 1;
				bool bValid = true;
				while (Position < TypeName.size())
				{
					if (!Arguments.empty() && (TypeName[Position] < '0' || TypeName[Position] > '9'))
					{
						break;
					}
					const std::size_t LengthStart = Position;
					std::size_t Length = 0;
					while (Position < TypeName.size() && TypeName[Position] >= '0' && TypeName[Position] <= '9')
					{
						const std::size_t Digit = static_cast<std::size_t>(TypeName[Position] - '0');
						if (Length > TypeName.size() / 10u || (Length == TypeName.size() / 10u && Digit > TypeName.size() % 10u))
						{
							bValid = false;
							break;
						}
						Length = Length * 10u + Digit;
						++Position;
					}
					if (!bValid || Position == LengthStart || Position >= TypeName.size() || TypeName[Position++] != ':' || Length > TypeName.size() - Position)
					{
						bValid = false;
						break;
					}
					Arguments.push_back(TypeName.substr(Position, Length));
					Position += Length;
				}
				std::size_t SuffixPosition = Position;
				while (bValid && SuffixPosition < TypeName.size())
				{
					if (TypeName[SuffixPosition++] != '[')
					{
						bValid = false;
						break;
					}
					while (SuffixPosition < TypeName.size() && TypeName[SuffixPosition] >= '0' && TypeName[SuffixPosition] <= '9')
					{
						++SuffixPosition;
					}
					if (SuffixPosition >= TypeName.size() || TypeName[SuffixPosition++] != ']')
					{
						bValid = false;
						break;
					}
				}
				if (bValid && !Arguments.empty())
				{
					std::string Result = MakeGenericTypeEncodingUserFacing(
					    TypeName.substr(0, Marker), Nesting + 1);
					Result.push_back('<');
					for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
					{
						if (Index > 0)
						{
							Result.push_back(',');
						}
						Result += MakeGenericTypeEncodingUserFacing(
						    Arguments[Index], Nesting + 1);
					}
					Result.push_back('>');
					Result.append(TypeName.substr(Position));
					return Result;
				}
			}

			std::string Result;
			for (std::size_t Index = 0; Index < TypeName.size(); ++Index)
			{
				const char Open = TypeName[Index];
				const char Close = Open == '<' ? '>' : (Open == '(' ? ')' : '\0');
				if (Close == '\0')
				{
					Result.push_back(Open);
					continue;
				}
				int Depth = 1;
				std::size_t End = Index + 1;
				for (; End < TypeName.size() && Depth > 0; ++End)
				{
					if (TypeName[End] == Open)
					{
						++Depth;
					}
					else if (TypeName[End] == Close)
					{
						--Depth;
					}
				}
				if (Depth != 0)
				{
					Result.append(TypeName.substr(Index));
					break;
				}
				Result.push_back(Open);
				Result += MakeDelimitedTypeListUserFacing(
				    TypeName.substr(Index + 1, End - Index - 2), Nesting + 1);
				Result.push_back(Close);
				Index = End - 1;
			}
			return Result;
		}

		bool HasMethodOwnerSeparator(const std::string_view Linkage)
		{
			if (FindMethodLinkageSeparator(Linkage) != std::string_view::npos)
			{
				return true;
			}
			// Non-generic method keys use the first signature marker after the
			// module prefix. Keep this direct form as a compatibility fallback for
			// manually supplied/static-member tables.
			const std::size_t IdentityStart = FindLinkageIdentityStart(Linkage);
			const std::size_t Hash = Linkage.find('#', IdentityStart);
			return Linkage.rfind("::", Hash) != std::string_view::npos;
		}

		void RestoreDeclaredSignatureTypes(
		    const FDeclMeta& Meta,
		    std::string& InOutReturnTypeName,
		    std::vector<std::string>& InOutParamTypeNames)
		{
			for (const auto& Pair : Meta)
			{
				if (Pair.first == DeclaredReturnTypeMetaKey)
				{
					InOutReturnTypeName = Pair.second;
					continue;
				}
				for (std::size_t Index = 0;
				    Index < InOutParamTypeNames.size();
				    ++Index)
				{
					if (Pair.first == std::string(DeclaredParamTypeMetaPrefix) + std::to_string(Index))
					{
						InOutParamTypeNames[Index] = Pair.second;
						break;
					}
				}
			}
		}

		enum class EResolveKind : std::uint8_t
		{
			Instance = 0,
			Constructor,
		};

		std::string_view TrimAscii(std::string_view Text)
		{
			while (!Text.empty() && std::isspace(static_cast<unsigned char>(Text.front())))
			{
				Text.remove_prefix(1);
			}
			while (!Text.empty() && std::isspace(static_cast<unsigned char>(Text.back())))
			{
				Text.remove_suffix(1);
			}
			return Text;
		}

		std::string NormalizeDeclaredParamType(std::string_view Declared)
		{
			Declared = TrimAscii(Declared);
			for (;;)
			{
				if (Declared.size() >= 6 && Declared.compare(0, 6, "const ") == 0)
				{
					Declared.remove_prefix(6);
					Declared = TrimAscii(Declared);
					continue;
				}
				if (Declared.size() >= 4 && Declared.compare(0, 4, "ref ") == 0)
				{
					Declared.remove_prefix(4);
					Declared = TrimAscii(Declared);
					continue;
				}
				break;
			}
			return std::string(Declared);
		}

		std::string RuntimeValueTypeName(const FValue& Value)
		{
			if (std::holds_alternative<FScriptVariantHandle>(Value))
			{
				return "variant";
			}
			if (std::holds_alternative<std::int8_t>(Value))
			{
				return "char";
			}
			if (std::holds_alternative<std::uint8_t>(Value))
			{
				return "unsigned char";
			}
			if (std::holds_alternative<std::int16_t>(Value))
			{
				return "short";
			}
			if (std::holds_alternative<std::uint16_t>(Value))
			{
				return "unsigned short";
			}
			if (std::holds_alternative<std::int32_t>(Value))
			{
				return "int";
			}
			if (std::holds_alternative<std::int64_t>(Value))
			{
				return "int64";
			}
			if (std::holds_alternative<std::uint32_t>(Value))
			{
				return "unsigned int";
			}
			if (std::holds_alternative<std::uint64_t>(Value))
			{
				return "uint64";
			}
			if (std::holds_alternative<double>(Value))
			{
				return "double";
			}
			if (std::holds_alternative<float>(Value))
			{
				return "float";
			}
			if (std::holds_alternative<bool>(Value))
			{
				return "bool";
			}
			if (std::holds_alternative<std::string>(Value))
			{
				return "string";
			}
			if (std::holds_alternative<std::u16string>(Value))
			{
				return "wstring";
			}
			if (const auto* Array = std::get_if<FDynamicArrayHandle>(&Value))
			{
				return *Array && !(*Array)->TypeName.empty() ? (*Array)->TypeName : "array<int>";
			}
			if (const auto* Arr = std::get_if<FNativeArray>(&Value))
			{
				return Arr->TypeName().empty() ? "int[]" : Arr->TypeName();
			}
			if (const auto* Map = std::get_if<FMapHandle>(&Value))
			{
				return *Map && !(*Map)->TypeName.empty() ? (*Map)->TypeName : "map<int,int>";
			}
			if (const auto* Set = std::get_if<FSetHandle>(&Value))
			{
				return *Set && !(*Set)->TypeName.empty() ? (*Set)->TypeName : "set<int>";
			}
			if (const auto* Queue = std::get_if<FQueueHandle>(&Value))
			{
				return *Queue && !(*Queue)->TypeName.empty() ? (*Queue)->TypeName : "queue<int>";
			}
			if (const auto* Stack = std::get_if<FStackHandle>(&Value))
			{
				return *Stack && !(*Stack)->TypeName.empty() ? (*Stack)->TypeName : "stack<int>";
			}
			if (const auto* Ordered = std::get_if<FOrderedMapHandle>(&Value))
			{
				return *Ordered && !(*Ordered)->TypeName.empty()
				    ? (*Ordered)->TypeName
				    : "ordered_map<int,int>";
			}
			if (const auto* Deque = std::get_if<FDequeHandle>(&Value))
			{
				return *Deque && !(*Deque)->TypeName.empty() ? (*Deque)->TypeName : "deque<int>";
			}
			if (const auto* Object = std::get_if<FObjectHandle>(&Value))
			{
				if (*Object && !(*Object)->GetTypeName().empty())
				{
					return (*Object)->GetTypeName();
				}
				return "object";
			}
			if (const auto* Function = std::get_if<FFunctionHandle>(&Value))
			{
				return *Function && !(*Function)->TypeName.empty() ? (*Function)->TypeName : "fn";
			}
			if (const auto* Channel = std::get_if<FChannelHandle>(&Value))
			{
				if (!*Channel)
				{
					return "channel";
				}
				std::string TypeName;
				std::string ElementTypeName;
				(*Channel)->GetTypeNames(TypeName, ElementTypeName);
				return TypeName.empty() ? "channel" : TypeName;
			}
			if (const auto* Host = std::get_if<FHostObjectHandle>(&Value))
			{
				return *Host && !(*Host)->TypeName.empty() ? (*Host)->TypeName : "object";
			}
			if (const auto* Enum = std::get_if<FEnumValue>(&Value))
			{
				return Enum->TypeName;
			}
			return {};
		}

		bool PrimitiveNumericInfo(
		    std::string_view TypeName,
		    bool& bOutInteger,
		    bool& bOutUnsigned,
		    int& OutBits)
		{
			TypeName = TrimAscii(TypeName);
			bOutInteger = true;
			bOutUnsigned = false;
			OutBits = 0;
			if (TypeName == "char" || TypeName == "int8")
			{
				OutBits = 8;
			}
			else if (TypeName == "unsigned char" || TypeName == "uint8")
			{
				bOutUnsigned = true;
				OutBits = 8;
			}
			else if (TypeName == "short" || TypeName == "int16")
			{
				OutBits = 16;
			}
			else if (TypeName == "unsigned short" || TypeName == "uint16")
			{
				bOutUnsigned = true;
				OutBits = 16;
			}
			else if (TypeName == "int" || TypeName == "int32")
			{
				OutBits = 32;
			}
			else if (TypeName == "unsigned" || TypeName == "unsigned int" || TypeName == "uint32")
			{
				bOutUnsigned = true;
				OutBits = 32;
			}
			else if (TypeName == "long" || TypeName == "int64")
			{
				OutBits = 64;
			}
			else if (TypeName == "unsigned long" || TypeName == "uint64")
			{
				bOutUnsigned = true;
				OutBits = 64;
			}
			else if (TypeName == "float")
			{
				bOutInteger = false;
				OutBits = 32;
			}
			else if (TypeName == "double")
			{
				bOutInteger = false;
				OutBits = 64;
			}
			return OutBits != 0;
		}

		int RuntimeParamMatchScore(
		    const std::string_view Declared,
		    const FValue& Arg,
		    const bool bAllowConversion)
		{
			const std::string Normalized = NormalizeDeclaredParamType(Declared);
			if (Normalized.empty())
			{
				return -1;
			}
			const std::string RuntimeType = RuntimeValueTypeName(Arg);
			if (RuntimeType.empty())
			{
				return -1;
			}
			if (Runtime::IsPrimitiveValueTypeName(Normalized))
			{
				if (Runtime::ValueMatchesPrimitiveTypeName(Arg, Normalized))
				{
					return 32;
				}
				if (!bAllowConversion)
				{
					return -1;
				}
				FValue Converted = Arg;
				std::string ConversionError;
				if (!Runtime::CoerceValueToPrimitiveTypeName(
				        Normalized, Converted, ConversionError))
				{
					return -1;
				}
				bool bSourceInteger = false;
				bool bSourceUnsigned = false;
				int SourceBits = 0;
				bool bTargetInteger = false;
				bool bTargetUnsigned = false;
				int TargetBits = 0;
				if (!PrimitiveNumericInfo(
				        RuntimeType, bSourceInteger, bSourceUnsigned, SourceBits) ||
				    !PrimitiveNumericInfo(
				        Normalized, bTargetInteger, bTargetUnsigned, TargetBits))
				{
					// bool-to-number and other permitted scalar conversions rank below
					// ordinary numeric conversions.
					return 6;
				}
				const bool bSafeInteger = bSourceInteger && bTargetInteger && ((bSourceUnsigned == bTargetUnsigned && SourceBits <= TargetBits) || (bSourceUnsigned && !bTargetUnsigned && SourceBits < TargetBits));
				const bool bSafeFloat = !bSourceInteger && !bTargetInteger && SourceBits <= TargetBits;
				const bool bSafeIntegerToFloat = bSourceInteger && !bTargetInteger && ((TargetBits == 32 && SourceBits <= 16) || (TargetBits == 64 && SourceBits <= 32));
				return bSafeInteger || bSafeFloat || bSafeIntegerToFloat ? 20 : 10;
			}
			if (const auto* const Host = std::get_if<FHostObjectHandle>(&Arg);
			    Host && HostObjectMatchesType(*Host, Normalized))
			{
				return *Host && (*Host)->TypeName == Normalized ? 32 : 20;
			}
			if (ModuleMeta::TypeNameMatches(RuntimeType, Normalized) || ModuleMeta::TypeSimpleName(RuntimeType) == Normalized || ModuleMeta::TypeSimpleName(RuntimeType) == ModuleMeta::TypeSimpleName(Normalized))
			{
				return RuntimeType == Normalized ? 32 : 20;
			}
			return -1;
		}

		EMethodResolveStatus NarrowByRuntimeArgs(
		    const FBytecodeModule& Module,
		    const std::vector<std::uint16_t>& Candidates,
		    const FValue* const Arguments,
		    const std::uint8_t ArgumentCount,
		    std::uint16_t& OutFunctionIndex)
		{
			if (Candidates.empty())
			{
				return EMethodResolveStatus::NotFound;
			}
			if (Arguments == nullptr)
			{
				if (Candidates.size() == 1)
				{
					OutFunctionIndex = Candidates[0];
					return EMethodResolveStatus::Found;
				}
				return EMethodResolveStatus::Ambiguous;
			}
			int BestScore = -1;
			std::uint16_t BestCandidate = static_cast<std::uint16_t>(-1);
			bool bAmbiguous = false;
			const auto ConsiderCandidate = [&](std::uint16_t FunctionIndex, int Score)
			{
				if (Score > BestScore)
				{
					BestScore = Score;
					BestCandidate = FunctionIndex;
					bAmbiguous = false;
				}
				else if (Score == BestScore)
				{
					bAmbiguous = true;
				}
			};
			for (const std::uint16_t FunctionIndex : Candidates)
			{
				if (FunctionIndex >= Module.Functions.size() || Module.Functions[FunctionIndex].ParamCount != ArgumentCount)
				{
					continue;
				}
				std::vector<std::string> ParamTypes;
				if (!ModuleMeta::DecodeMethodParamTypeNames(
				        Module.Functions[FunctionIndex].Name,
				        ParamTypes) ||
				    ParamTypes.size() != ArgumentCount)
				{
					// Arity-only / undecodable mangling: keep as untyped candidate.
					if (ParamTypes.empty())
					{
						ConsiderCandidate(FunctionIndex, 0);
					}
					continue;
				}
				bool bMatch = true;
				int CandidateScore = 0;
				for (std::uint8_t Index = 0; Index < ArgumentCount; ++Index)
				{
					const bool bReferenceParam =
					    Index < Module.Functions[FunctionIndex].ParamFlags.size() && (Module.Functions[FunctionIndex].ParamFlags[Index] & (BytecodeParamFlagRef | BytecodeParamFlagConstRef)) != 0;
					const int ParamScore = RuntimeParamMatchScore(
					    ParamTypes[Index], Arguments[Index], !bReferenceParam);
					if (ParamScore < 0)
					{
						bMatch = false;
						break;
					}
					CandidateScore += ParamScore;
				}
				if (bMatch)
				{
					ConsiderCandidate(FunctionIndex, CandidateScore);
				}
			}
			if (BestScore < 0) return EMethodResolveStatus::NotFound;
			if (bAmbiguous) return EMethodResolveStatus::Ambiguous;
			OutFunctionIndex = BestCandidate;
			return EMethodResolveStatus::Found;
		}

		const std::uint16_t* FindExactFreeFunction(const FBytecodeModule& Module,
		    const std::string& Name, std::uint8_t ArgumentCount)
		{
			const auto It = Module.FunctionIndices.find(Name);
			if (It == Module.FunctionIndices.end() || It->second >= Module.Functions.size()) return nullptr;
			const auto& Function = Module.Functions[It->second];
			return !Function.bIsMethod && Function.ParamCount == ArgumentCount ? &It->second : nullptr;
		}

		void CollectFreeFunctionCandidates(const FBytecodeModule& Module, std::string_view Name,
		    std::uint8_t ArgumentCount, std::vector<std::uint16_t>& Candidates)
		{
			const bool bExplicitLinkage = Name.find('#') != std::string_view::npos;
			std::unordered_set<std::uint16_t> Seen;
			const auto LeafMatches = [&](std::string_view Key) -> bool
			{
				// Only inspect the linkage head for an owner. Parameter types can
				// themselves contain namespace or unit separators.
				const auto Leaf = Key.substr(FindLinkageIdentityStart(Key));
				if (Leaf.substr(0, Leaf.find('#')).find("::") != std::string_view::npos) return false;
				if (Leaf == Name) return true;
				const auto FnTag = Leaf.find("#fn");
				if (FnTag != std::string_view::npos) return Leaf.substr(0, FnTag) == Name;
				return !bExplicitLinkage && Leaf.size() > Name.size() &&
				    Leaf.compare(0, Name.size(), Name) == 0 && Leaf[Name.size()] == '#';
			};
			for (const auto& Entry : Module.FunctionIndices)
			{
				if (Entry.second >= Module.Functions.size())
				{
					continue;
				}
				const auto& Function = Module.Functions[Entry.second];
				if (!Function.bIsMethod && Function.ParamCount == ArgumentCount &&
				    (bExplicitLinkage || Function.TemplateInstance.Arguments.empty()) &&
				    LeafMatches(Entry.first) && Seen.insert(Entry.second).second)
				{
					Candidates.push_back(Entry.second);
				}
			}
		}

		std::string ResolveCallMethodKey(
		    const std::string& OwnerLinkage,
		    const std::string& MethodNameStr,
		    const std::size_t ArgumentCount)
		{
			if (MethodNameStr.find('#') != std::string::npos)
			{
				return OwnerLinkage + "::" + MethodNameStr;
			}
			return MakeArityMethodFunctionName(OwnerLinkage, MethodNameStr, ArgumentCount);
		}

		bool MatchesMethodLeaf(const std::string& Key, const std::string& MethodNameStr)
		{
			return MethodLinkageLeafMatches(Key, MethodNameStr);
		}

		bool TryParseMethodLeaf(
		    const std::string& FunctionName,
		    std::string& OutBareName,
		    bool& bOutIsDestructor)
		{
			OutBareName.clear();
			bOutIsDestructor = false;
			std::string_view Leaf = MethodLinkageBareLeaf(FunctionName);
			if (Leaf.empty())
			{
				return false;
			}
			if (Leaf.front() == '@') return false;
			if (Leaf.front() == '~')
			{
				bOutIsDestructor = true;
				Leaf.remove_prefix(1);
			}
			OutBareName = std::string(Leaf);
			return !OutBareName.empty();
		}

		bool AcceptCandidate(
		    const EResolveKind Kind,
		    const std::string& OwnerSimpleName,
		    const std::string& BareName,
		    const bool bIsDestructor)
		{
			const bool bIsConstructor = !bIsDestructor && BareName == OwnerSimpleName;
			if (BareName == "@defaults")
			{
				return false;
			}
			if (Kind == EResolveKind::Constructor)
			{
				return bIsConstructor;
			}
			return !bIsConstructor && !bIsDestructor;
		}

		void MakeSignatureTypeNamesUserFacing(
		    const FBytecodeModule& Module,
		    std::vector<std::string>& TypeNames);

		std::string MethodCandidateSignatureKey(
		    const FBytecodeModule& Module,
		    const FBytecodeFunction& Function,
		    const std::string& BareName,
		    const bool bIsDestructor)
		{
			std::vector<std::string> ParamTypes;
			(void)ModuleMeta::DecodeMethodParamTypeNames(Function.Name, ParamTypes);
			MakeSignatureTypeNamesUserFacing(Module, ParamTypes);
			std::string Key = bIsDestructor ? "~" + BareName : BareName;
			if (ParamTypes.size() == Function.ParamCount)
			{
				Key += EncodeMethodParamSuffix(ParamTypes);
			}
			else
			{
				Key += "#" + std::to_string(Function.ParamCount);
			}
			if (Function.bIsConstMethod)
			{
				Key.push_back('c');
			}
			return Key;
		}

		void CollectOwnerCandidates(
		    const FBytecodeModule& Module,
		    const std::string& OwnerLinkage,
		    const std::string& OwnerSimpleName,
		    const std::string& MethodNameStr,
		    const std::uint8_t ArgumentCount,
		    const EResolveKind Kind,
		    std::vector<std::uint16_t>& OutCandidates,
		    std::unordered_set<std::uint16_t>& SeenIndices,
		    std::unordered_set<std::string>& SeenSignatures)
		{
			const auto AddIndex = [&](const std::uint16_t FunctionIndex)
			{
				if (FunctionIndex >= Module.Functions.size() || Module.Functions[FunctionIndex].ParamCount != ArgumentCount || !SeenIndices.insert(FunctionIndex).second)
				{
					return;
				}
				std::string BareName;
				bool bIsDestructor = false;
				if (!TryParseMethodLeaf(Module.Functions[FunctionIndex].Name, BareName, bIsDestructor) || !AcceptCandidate(Kind, OwnerSimpleName, BareName, bIsDestructor) || !MatchesMethodLeaf(Module.Functions[FunctionIndex].Name, MethodNameStr))
				{
					return;
				}
				const std::string Signature = MethodCandidateSignatureKey(
				    Module,
				    Module.Functions[FunctionIndex],
				    BareName,
				    bIsDestructor);
				if (!SeenSignatures.insert(Signature).second)
				{
					return;
				}
				OutCandidates.push_back(FunctionIndex);
			};

			{
				const std::string MethodKey = ResolveCallMethodKey(
				    OwnerLinkage,
				    MethodNameStr,
				    ArgumentCount);
				const auto FunctionIt = Module.FunctionIndices.find(MethodKey);
				if (FunctionIt != Module.FunctionIndices.end())
				{
					AddIndex(FunctionIt->second);
				}
			}
			const std::string Prefix = OwnerLinkage + "::";
			for (const auto& Entry : Module.FunctionIndices)
			{
				if (Entry.first.compare(0, Prefix.size(), Prefix) != 0)
				{
					continue;
				}
				AddIndex(Entry.second);
			}
		}

		EMethodResolveStatus TryResolveOnTypeLinkage(
		    const FBytecodeModule& Module,
		    const std::string& TypeLinkageName,
		    const std::string& MethodNameStr,
		    const std::uint8_t ArgumentCount,
		    const EResolveKind Kind,
		    std::uint16_t& OutFunctionIndex,
		    const FValue* const Arguments)
		{
			OutFunctionIndex = static_cast<std::uint16_t>(-1);
			if (TypeLinkageName.empty() || MethodNameStr.empty())
			{
				return EMethodResolveStatus::NotFound;
			}

			std::vector<std::string> TypeQueue{TypeLinkageName};
			std::unordered_set<std::string> VisitedTypes;
			std::vector<std::uint16_t> InterfaceFallbackCandidates;
			std::unordered_set<std::uint16_t> InterfaceFallbackIndices;
			std::unordered_set<std::string> InterfaceFallbackSignatures;
			for (std::size_t QueueIndex = 0; QueueIndex < TypeQueue.size(); ++QueueIndex)
			{
				const std::string& LookupTypeName = TypeQueue[QueueIndex];
				if (LookupTypeName.empty() || !VisitedTypes.insert(LookupTypeName).second)
				{
					continue;
				}

				std::vector<std::uint16_t> Candidates;
				std::unordered_set<std::uint16_t> SeenIndices;
				std::unordered_set<std::string> SeenSignatures;
				const std::string OwnerSimple = ModuleMeta::TypeSimpleName(LookupTypeName);
				CollectOwnerCandidates(
				    Module,
				    LookupTypeName,
				    OwnerSimple,
				    MethodNameStr,
				    ArgumentCount,
				    Kind,
				    Candidates,
				    SeenIndices,
				    SeenSignatures);

				std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
				for (std::size_t Index = 0; Index < Module.Types.size(); ++Index)
				{
					if (Module.Types[Index].Name == LookupTypeName)
					{
						TypeIndex = static_cast<std::uint16_t>(Index);
						break;
					}
				}
				const auto CollectInterfaces = [&Module,
				                                   &MethodNameStr,
				                                   ArgumentCount,
				                                   Kind](
				                                   const std::uint16_t OwnerTypeIndex,
				                                   std::vector<std::uint16_t>& OutCandidates,
				                                   std::unordered_set<std::uint16_t>& OutIndices,
				                                   std::unordered_set<std::string>& OutSignatures)
				{
					if (OwnerTypeIndex >= Module.Types.size() || Kind != EResolveKind::Instance)
					{
						return;
					}
					std::unordered_set<std::uint16_t> VisitedInterfaces;
					std::vector<std::uint16_t> InterfaceQueue =
					    Module.Types[OwnerTypeIndex].ImplementedInterfaceIndices;
					for (std::size_t IfaceQueueIndex = 0;
					    IfaceQueueIndex < InterfaceQueue.size();
					    ++IfaceQueueIndex)
					{
						const std::uint16_t IfaceIndex = InterfaceQueue[IfaceQueueIndex];
						if (IfaceIndex >= Module.Interfaces.size() || !VisitedInterfaces.insert(IfaceIndex).second)
						{
							continue;
						}
						const std::string& IfaceName = Module.Interfaces[IfaceIndex].Name;
						CollectOwnerCandidates(
						    Module,
						    IfaceName,
						    ModuleMeta::TypeSimpleName(IfaceName),
						    MethodNameStr,
						    ArgumentCount,
						    Kind,
						    OutCandidates,
						    OutIndices,
						    OutSignatures);
						for (const std::uint16_t BaseIface :
						    Module.Interfaces[IfaceIndex].BaseInterfaceIndices)
						{
							if (BaseIface < Module.Interfaces.size())
							{
								InterfaceQueue.push_back(BaseIface);
							}
						}
					}
				};

				if (!Candidates.empty())
				{
					// Concrete class implementations are collected first. Merge interface
					// declarations only after their normalized signatures have been seen,
					// so an override wins while unrelated interface overloads remain.
					for (const std::uint16_t InterfaceFunction : InterfaceFallbackCandidates)
					{
						if (InterfaceFunction >= Module.Functions.size())
						{
							continue;
						}
						std::string BareName;
						bool bIsDestructor = false;
						if (!TryParseMethodLeaf(
						        Module.Functions[InterfaceFunction].Name,
						        BareName,
						        bIsDestructor))
						{
							continue;
						}
						const std::string Signature = MethodCandidateSignatureKey(
						    Module,
						    Module.Functions[InterfaceFunction],
						    BareName,
						    bIsDestructor);
						if (SeenSignatures.insert(Signature).second && SeenIndices.insert(InterfaceFunction).second)
						{
							Candidates.push_back(InterfaceFunction);
						}
					}
					CollectInterfaces(
					    TypeIndex, Candidates, SeenIndices, SeenSignatures);
					return NarrowByRuntimeArgs(
					    Module,
					    Candidates,
					    Arguments,
					    ArgumentCount,
					    OutFunctionIndex);
				}

				if (TypeIndex != static_cast<std::uint16_t>(-1))
				{
					CollectInterfaces(
					    TypeIndex,
					    InterfaceFallbackCandidates,
					    InterfaceFallbackIndices,
					    InterfaceFallbackSignatures);
				}

				if (TypeIndex == static_cast<std::uint16_t>(-1) || TypeIndex >= Module.Types.size())
				{
					continue;
				}
				for (const std::uint16_t BaseIdx : Module.Types[TypeIndex].BaseTypeIndices)
				{
					if (BaseIdx < Module.Types.size())
					{
						TypeQueue.push_back(Module.Types[BaseIdx].Name);
					}
				}
			}
			if (!InterfaceFallbackCandidates.empty())
			{
				return NarrowByRuntimeArgs(
				    Module,
				    InterfaceFallbackCandidates,
				    Arguments,
				    ArgumentCount,
				    OutFunctionIndex);
			}
			return EMethodResolveStatus::NotFound;
		}

		FLoadedTypeInfo MakeTypeInfo(const FBytecodeModule& Module, const std::uint16_t TypeIndex)
		{
			FLoadedTypeInfo Info;
			Info.TypeIndex = TypeIndex;
			const FBytecodeTypeDesc& Type = Module.Types[TypeIndex];
			Info.TemplateInstance = Type.TemplateInstance;
			Info.Kind = Type.Kind;
			Info.Scope = Type.Scope;
			Info.NestedTypes = Type.NestedTypes;
			Info.TypeName = Type.Name;
			Info.SimpleName = ModuleMeta::TypeSimpleName(Type.Name);
			Info.HostBaseTypeName = Type.HostBaseTypeName;
			Info.HostBaseTypeId = Type.HostBaseTypeId;
			Info.bIsAbstract = Type.bIsAbstract;
			Info.bIsStatic = Type.bIsStatic;
			Info.bDestructorPureVirtual = Type.bDestructorPureVirtual;
			Info.BaseTypeIndices = Type.BaseTypeIndices;
			Info.OwnFieldStartIndex = Type.OwnFieldStartIndex;
			Info.DeclMeta = Type.DeclMeta;
			return Info;
		}

		/**
		 * Top-level return type from a canonical `fn(...)->T` signature.
		 * Nested `fn(...)->fn(...)->T` yields the full RHS (via ParseFunctionType).
		 * Empty when the signature is missing or not in canonical form.
		 */
		std::string DecodeClosureReturnTypeName(const std::string& ClosureTypeName)
		{
			std::vector<std::string> ParamTypes;
			std::string ReturnType;
			if (!Runtime::ParseFunctionType(ClosureTypeName, ParamTypes, ReturnType))
			{
				return {};
			}
			return ReturnType;
		}

		// Fill ReturnTypeName / ParamNames / ParamFlags from existing bytecode fields.
		void FillFunctionSignatureExtras(
		    const FBytecodeFunction& Function,
		    std::string& OutReturnTypeName,
		    std::vector<std::string>& OutParamNames,
		    std::vector<std::uint8_t>& OutParamFlags)
		{
			OutReturnTypeName = Function.bReturnsVoid
			    ? std::string("void")
			    : DecodeClosureReturnTypeName(Function.ClosureTypeName);
			OutParamNames.assign(Function.ParamCount, std::string{});
			const std::size_t NamedCount = std::min<std::size_t>(
			    Function.ParamCount,
			    Function.LocalNames.size());
			for (std::size_t Index = 0; Index < NamedCount; ++Index)
			{
				OutParamNames[Index] = Function.LocalNames[Index];
			}
			OutParamFlags = Function.ParamFlags;
		}

		void MakeSignatureTypeNamesUserFacing(
		    const FBytecodeModule& Module,
		    std::vector<std::string>& TypeNames)
		{
			std::vector<std::pair<std::string_view, std::string_view>> Replacements;
			const auto AddReplacement =
			    [&Replacements](const std::string& LinkageName)
			{
				const std::size_t IdentityStart =
				    FindLinkageIdentityStart(LinkageName);
				if (IdentityStart > 0 && IdentityStart < LinkageName.size())
				{
					Replacements.emplace_back(
					    std::string_view(LinkageName),
					    std::string_view(LinkageName).substr(IdentityStart));
				}
			};
			for (const FBytecodeTypeDesc& Type : Module.Types)
			{
				AddReplacement(Type.Name);
			}
			for (const FBytecodeInterfaceDesc& Interface : Module.Interfaces)
			{
				AddReplacement(Interface.Name);
			}
			for (const FBytecodeEnumDesc& Enum : Module.Enums)
			{
				AddReplacement(Enum.Name);
			}
			std::sort(
			    Replacements.begin(),
			    Replacements.end(),
			    [](const auto& Left, const auto& Right)
			{
				return Left.first.size() > Right.first.size();
			});
			for (std::string& TypeName : TypeNames)
			{
				for (const auto& [LinkageName, DisplayName] : Replacements)
				{
					std::size_t Position = 0;
					while ((Position = TypeName.find(LinkageName, Position)) != std::string::npos)
					{
						TypeName.replace(Position, LinkageName.size(), DisplayName);
						Position += DisplayName.size();
					}
				}
				TypeName = MakeGenericTypeEncodingUserFacing(TypeName);
			}
		}

		void MakeSignatureTypeNameUserFacing(
		    const FBytecodeModule& Module,
		    std::string& TypeName)
		{
			std::vector<std::string> TypeNames;
			TypeNames.push_back(std::move(TypeName));
			MakeSignatureTypeNamesUserFacing(Module, TypeNames);
			TypeName = std::move(TypeNames.front());
		}

		void AppendMethodInfo(
		    const FBytecodeModule& Module,
		    const std::uint16_t FunctionIndex,
		    const std::uint16_t DeclaringTypeIndex,
		    const std::string& DeclaringTypeName,
		    const std::string& OwnerSimpleName,
		    const bool bIncludeConstructors,
		    const bool bIncludeDestructors,
		    std::unordered_map<std::string, std::size_t>& SeenIndices,
		    std::vector<FInstanceMethodInfo>& Out)
		{
			if (FunctionIndex >= Module.Functions.size())
			{
				return;
			}
			const FBytecodeFunction& Function = Module.Functions[FunctionIndex];
			if (!Function.bIsMethod)
			{
				return;
			}
			std::string BareName;
			bool bIsDestructor = false;
			if (!TryParseMethodLeaf(Function.Name, BareName, bIsDestructor))
			{
				return;
			}
			const bool bIsConstructor = !bIsDestructor && BareName == OwnerSimpleName;
			if (bIsDestructor && !bIncludeDestructors)
			{
				return;
			}
			if (bIsConstructor && !bIncludeConstructors)
			{
				return;
			}
			std::vector<std::string> ParamTypes;
			(void)ModuleMeta::DecodeMethodParamTypeNames(Function.Name, ParamTypes);
			std::vector<std::string> DedupParams;
			DedupParams = ParamTypes;
			MakeSignatureTypeNamesUserFacing(Module, DedupParams);
			std::string DedupKey = BareName;
			if (bIsDestructor)
			{
				DedupKey.insert(DedupKey.begin(), '~');
			}
			if (DedupParams.empty())
			{
				DedupKey += "#" + std::to_string(Function.ParamCount);
			}
			else
			{
				DedupKey += EncodeMethodParamSuffix(DedupParams);
			}
			if (Function.bIsConstMethod)
			{
				DedupKey += 'c';
			}
			const auto Seen = SeenIndices.find(DedupKey);
			if (Seen != SeenIndices.end())
			{
				// Most-derived override already won; inherit DeclMeta from iface/base
				// stubs when the override itself has none.
				const FDeclMeta InheritedPublicMeta =
				    PublicDeclMeta(Function.DeclMeta);
				if (Seen->second < Out.size() && Out[Seen->second].DeclMeta.empty() && !InheritedPublicMeta.empty())
				{
					Out[Seen->second].DeclMeta = InheritedPublicMeta;
				}
				return;
			}
			SeenIndices.emplace(std::move(DedupKey), Out.size());
			FInstanceMethodInfo Info;
			Info.TemplateInstance = Function.TemplateInstance;
			Info.Name = SourceTemplateName(Module, Function, std::move(BareName));
			Info.ParamCount = Function.ParamCount;
			MakeSignatureTypeNamesUserFacing(Module, ParamTypes);
			Info.ParamTypeNames = std::move(ParamTypes);
			Info.bReturnsVoid = Function.bReturnsVoid;
			Info.bReturnsRef = Function.bReturnsRef;
			Info.bIsVirtual = Function.bIsVirtual;
			Info.bIsAbstract = Function.bIsAbstract;
			Info.bIsConst = Function.bIsConstMethod;
			Info.bConstexpr = Function.bConstexpr;
			Info.bIsConstructor = bIsConstructor;
			Info.bIsDestructor = bIsDestructor;
			Info.bDeleted = Function.bDeleted;
			Info.FunctionIndex = FunctionIndex;
			Info.DeclaringTypeIndex = DeclaringTypeIndex;
			Info.DeclaringTypeName = DeclaringTypeName;
			FillFunctionSignatureExtras(
			    Function,
			    Info.ReturnTypeName,
			    Info.ParamNames,
			    Info.ParamFlags);
			RestoreDeclaredSignatureTypes(
			    Function.DeclMeta,
			    Info.ReturnTypeName,
			    Info.ParamTypeNames);
			MakeSignatureTypeNameUserFacing(Module, Info.ReturnTypeName);
			Info.bReturnsConstRef =
			    Runtime::IsConstReferenceTypeName(Info.ReturnTypeName);
			Info.DeclMeta = PublicDeclMeta(Function.DeclMeta);
			Out.push_back(std::move(Info));
		}

		void AppendMethodsDeclaredOnType(
		    const FBytecodeModule& Module,
		    const std::uint16_t TypeIndex,
		    const bool bIncludeConstructors,
		    const bool bIncludeDestructors,
		    std::unordered_map<std::string, std::size_t>& SeenIndices,
		    std::vector<FInstanceMethodInfo>& Out)
		{
			if (TypeIndex >= Module.Types.size())
			{
				return;
			}
			const FBytecodeTypeDesc& Type = Module.Types[TypeIndex];
			const std::string SimpleName = ModuleMeta::TypeSimpleName(Type.Name);
			for (std::size_t FunctionIndex = 0; FunctionIndex < Module.Functions.size(); ++FunctionIndex)
			{
				const FBytecodeFunction& Function = Module.Functions[FunctionIndex];
				if (!Function.bIsMethod || Function.OwnerTypeIndex != TypeIndex)
				{
					continue;
				}
				AppendMethodInfo(
				    Module,
				    static_cast<std::uint16_t>(FunctionIndex),
				    TypeIndex,
				    Type.Name,
				    SimpleName,
				    bIncludeConstructors,
				    bIncludeDestructors,
				    SeenIndices,
				    Out);
			}
		}

		void AppendMethodsDeclaredOnInterface(
		    const FBytecodeModule& Module,
		    const std::uint16_t InterfaceIndex,
		    const bool bIncludeConstructors,
		    const bool bIncludeDestructors,
		    std::unordered_map<std::string, std::size_t>& SeenIndices,
		    std::vector<FInstanceMethodInfo>& Out)
		{
			if (InterfaceIndex >= Module.Interfaces.size())
			{
				return;
			}
			const FBytecodeInterfaceDesc& Iface = Module.Interfaces[InterfaceIndex];
			const std::string SimpleName = ModuleMeta::TypeSimpleName(Iface.Name);
			const std::string Prefix = Iface.Name + "::";
			for (const auto& Entry : Module.FunctionIndices)
			{
				if (Entry.first.compare(0, Prefix.size(), Prefix) != 0 || Entry.second >= Module.Functions.size() || !Module.Functions[Entry.second].bIsMethod)
				{
					continue;
				}
				AppendMethodInfo(
				    Module,
				    Entry.second,
				    static_cast<std::uint16_t>(-1),
				    Iface.Name,
				    SimpleName,
				    bIncludeConstructors,
				    bIncludeDestructors,
				    SeenIndices,
				    Out);
			}
		}
	}

	namespace ModuleMeta
	{
		void EnumerateTemplateDeclarations(const FBytecodeModule& Module,
		    std::vector<FTemplateDeclarationInfo>& Out, const bool ExportedOnly)
		{
			Out.clear();
			for (const auto& Declaration : Module.TemplateDeclarations)
			{
				if (!ExportedOnly || Declaration.bExported)
				{
					Out.push_back(Declaration);
				}
			}
		}

		std::string TypeSimpleName(const std::string_view TypeLinkageName)
		{
			if (TypeLinkageName.find('#') == std::string_view::npos && TypeLinkageName.find('<') != std::string_view::npos)
			{
				std::vector<FTypePathSegment> Segments;
				if (ParseTypePath(TypeLinkageName.substr(FindLinkageIdentityStart(TypeLinkageName)), Segments))
				{
					return Segments.back().Name;
				}
			}
			std::string_view Leaf = TypeLinkageName.substr(
			    FindLinkageIdentityStart(TypeLinkageName));
			const std::size_t Hash = Leaf.find('#');
			const std::size_t Angle = Leaf.find('<');
			const std::size_t Suffix = std::min(Hash, Angle);
			if (Suffix != std::string_view::npos)
			{
				Leaf = Leaf.substr(0, Suffix);
			}
			const std::size_t Colon = Leaf.rfind("::");
			if (Colon != std::string_view::npos)
			{
				Leaf = Leaf.substr(Colon + 2);
			}
			const std::size_t Dot = Leaf.rfind('.');
			if (Dot != std::string_view::npos)
			{
				Leaf = Leaf.substr(Dot + 1);
			}
			return std::string(Leaf);
		}

		bool TypeNameMatches(const std::string_view TypeLinkageName, const std::string_view Wanted)
		{
			if (Wanted.empty())
			{
				return false;
			}
			if (TypeLinkageName == Wanted)
			{
				return true;
			}
			const std::size_t IdentityStart =
			    FindLinkageIdentityStart(TypeLinkageName);
			if (IdentityStart > 0)
			{
				const std::string_view AfterUnit =
				    TypeLinkageName.substr(IdentityStart);
				if (AfterUnit == Wanted)
				{
					return true;
				}
			}
			return TypeSimpleName(TypeLinkageName) == Wanted;
		}

		bool FindTypeIndex(
		    const FBytecodeModule& Module,
		    const std::string_view TypeName,
		    std::uint16_t& OutTypeIndex,
		    bool* const OutAmbiguous)
		{
			OutTypeIndex = static_cast<std::uint16_t>(-1);
			if (OutAmbiguous)
			{
				*OutAmbiguous = false;
			}
			if (TypeName.empty())
			{
				return false;
			}
			// Canonical runtime names normally match exactly. Resolve them before
			// parsing unrelated linkage/generic names for the alias fallback.
			bool HasExact = false;
			for (std::size_t Index = 0; Index < Module.Types.size(); ++Index)
			{
				if (Module.Types[Index].Name != TypeName)
				{
					continue;
				}
				if (HasExact)
				{
					OutTypeIndex = static_cast<std::uint16_t>(-1);
					if (OutAmbiguous)
					{
						*OutAmbiguous = true;
					}
					return false;
				}
				HasExact = true;
				OutTypeIndex = static_cast<std::uint16_t>(Index);
			}
			if (HasExact) return true;
			std::vector<std::uint16_t> Leaf;
			for (std::size_t Index = 0; Index < Module.Types.size(); ++Index)
			{
				const std::string& Name = Module.Types[Index].Name;
				if (TypeNameMatches(Name, TypeName) &&
				    (!(Module.Types[Index].Scope.bLocal || !Module.Types[Index].Scope.OwnerTypeName.empty()) ||
				        std::string_view(Name).substr(FindLinkageIdentityStart(Name)) == TypeName))
				{
					Leaf.push_back(static_cast<std::uint16_t>(Index));
				}
			}
			if (Leaf.size() == 1)
			{
				OutTypeIndex = Leaf[0];
				return true;
			}
			if (Leaf.size() > 1)
			{
				if (OutAmbiguous)
				{
					*OutAmbiguous = true;
				}
			}
			return false;
		}

		bool FindInterfaceIndex(
		    const FBytecodeModule& Module,
		    const std::string_view TypeName,
		    std::uint16_t& OutInterfaceIndex,
		    bool* const OutAmbiguous)
		{
			OutInterfaceIndex = static_cast<std::uint16_t>(-1);
			if (OutAmbiguous)
			{
				*OutAmbiguous = false;
			}
			if (TypeName.empty())
			{
				return false;
			}
			bool HasExact = false;
			for (std::size_t Index = 0; Index < Module.Interfaces.size(); ++Index)
			{
				if (Module.Interfaces[Index].Name != TypeName)
				{
					continue;
				}
				if (HasExact)
				{
					OutInterfaceIndex = static_cast<std::uint16_t>(-1);
					if (OutAmbiguous)
					{
						*OutAmbiguous = true;
					}
					return false;
				}
				HasExact = true;
				OutInterfaceIndex = static_cast<std::uint16_t>(Index);
			}
			if (HasExact) return true;
			std::vector<std::uint16_t> Leaf;
			for (std::size_t Index = 0; Index < Module.Interfaces.size(); ++Index)
			{
				const std::string& Name = Module.Interfaces[Index].Name;
				if (TypeNameMatches(Name, TypeName) &&
				    (!(Module.Interfaces[Index].Scope.bLocal || !Module.Interfaces[Index].Scope.OwnerTypeName.empty()) ||
				        std::string_view(Name).substr(FindLinkageIdentityStart(Name)) == TypeName))
				{
					Leaf.push_back(static_cast<std::uint16_t>(Index));
				}
			}
			if (Leaf.size() == 1)
			{
				OutInterfaceIndex = Leaf[0];
				return true;
			}
			if (Leaf.size() > 1)
			{
				if (OutAmbiguous)
				{
					*OutAmbiguous = true;
				}
			}
			return false;
		}

		bool IsObjectTypeAssignableTo(
		    const FBytecodeModule& Module,
		    const std::string_view ActualTypeName,
		    const std::string_view ExpectedTypeName)
		{
			std::uint16_t ActualTypeIndex = static_cast<std::uint16_t>(-1);
			if (!FindTypeIndex(Module, ActualTypeName, ActualTypeIndex))
			{
				return false;
			}

			std::uint16_t ExpectedTypeIndex = static_cast<std::uint16_t>(-1);
			if (FindTypeIndex(Module, ExpectedTypeName, ExpectedTypeIndex))
			{
				std::vector<std::uint16_t> TypeQueue{ActualTypeIndex};
				std::unordered_set<std::uint16_t> VisitedTypes;
				for (std::size_t QueueIndex = 0; QueueIndex < TypeQueue.size(); ++QueueIndex)
				{
					const std::uint16_t TypeIndex = TypeQueue[QueueIndex];
					if (TypeIndex >= Module.Types.size() || !VisitedTypes.insert(TypeIndex).second)
					{
						continue;
					}
					if (TypeIndex == ExpectedTypeIndex)
					{
						return true;
					}
					for (const std::uint16_t BaseIndex : Module.Types[TypeIndex].BaseTypeIndices)
					{
						if (BaseIndex < Module.Types.size())
						{
							TypeQueue.push_back(BaseIndex);
						}
					}
				}
				return false;
			}

			std::uint16_t ExpectedInterfaceIndex = static_cast<std::uint16_t>(-1);
			if (!FindInterfaceIndex(Module, ExpectedTypeName, ExpectedInterfaceIndex))
			{
				return false;
			}

			std::vector<std::uint16_t> InterfaceQueue;
			std::vector<std::uint16_t> TypeQueue{ActualTypeIndex};
			std::unordered_set<std::uint16_t> VisitedTypes;
			for (std::size_t QueueIndex = 0; QueueIndex < TypeQueue.size(); ++QueueIndex)
			{
				const std::uint16_t TypeIndex = TypeQueue[QueueIndex];
				if (TypeIndex >= Module.Types.size() || !VisitedTypes.insert(TypeIndex).second)
				{
					continue;
				}
				for (const std::uint16_t InterfaceIndex :
				    Module.Types[TypeIndex].ImplementedInterfaceIndices)
				{
					if (InterfaceIndex < Module.Interfaces.size())
					{
						InterfaceQueue.push_back(InterfaceIndex);
					}
				}
				for (const std::uint16_t BaseIndex : Module.Types[TypeIndex].BaseTypeIndices)
				{
					if (BaseIndex < Module.Types.size())
					{
						TypeQueue.push_back(BaseIndex);
					}
				}
			}
			std::unordered_set<std::uint16_t> VisitedInterfaces;
			for (std::size_t QueueIndex = 0; QueueIndex < InterfaceQueue.size(); ++QueueIndex)
			{
				const std::uint16_t InterfaceIndex = InterfaceQueue[QueueIndex];
				if (InterfaceIndex >= Module.Interfaces.size() || !VisitedInterfaces.insert(InterfaceIndex).second)
				{
					continue;
				}
				if (InterfaceIndex == ExpectedInterfaceIndex)
				{
					return true;
				}
				for (const std::uint16_t BaseIndex :
				    Module.Interfaces[InterfaceIndex].BaseInterfaceIndices)
				{
					if (BaseIndex < Module.Interfaces.size())
					{
						InterfaceQueue.push_back(BaseIndex);
					}
				}
			}
			return false;
		}

		bool DecodeMethodParamTypeNames(
		    const std::string_view MangledFunctionName,
		    std::vector<std::string>& OutParamTypeNames)
		{
			OutParamTypeNames.clear();
			const std::size_t Hash = FindFunctionSignatureMarker(MangledFunctionName);
			if (Hash == std::string_view::npos || Hash + 1 >= MangledFunctionName.size())
			{
				return true;
			}
			std::string_view Encoded = MangledFunctionName.substr(Hash + 1);
			// Free-function keys use `#fn` before the param suffix (`Name#fn#3:int`).
			if (Encoded.size() >= 2 && Encoded.compare(0, 2, "fn") == 0)
			{
				Encoded.remove_prefix(2);
				if (!Encoded.empty() && Encoded.front() == '#')
				{
					Encoded.remove_prefix(1);
				}
			}
			if (Encoded.find(':') == std::string_view::npos)
			{
				return true;
			}
			while (!Encoded.empty())
			{
				// Const methods append a trailing `c` after the param suffix — not part
				// of a type name (e.g. `#5:Music` must keep the final `c` of Music).
				if (Encoded == "c")
				{
					return true;
				}
				std::size_t Digits = 0;
				while (Digits < Encoded.size() && Encoded[Digits] >= '0' && Encoded[Digits] <= '9')
				{
					++Digits;
				}
				if (Digits == 0 || Digits >= Encoded.size() || Encoded[Digits] != ':')
				{
					OutParamTypeNames.clear();
					return false;
				}
				std::size_t TypeLen = 0;
				for (std::size_t I = 0; I < Digits; ++I)
				{
					const std::size_t Digit = static_cast<std::size_t>(Encoded[I] - '0');
					if (TypeLen > (static_cast<std::size_t>(-1) - Digit) / 10)
					{
						OutParamTypeNames.clear();
						return false;
					}
					TypeLen = TypeLen * 10 + Digit;
				}
				Encoded.remove_prefix(Digits + 1);
				if (TypeLen > Encoded.size())
				{
					OutParamTypeNames.clear();
					return false;
				}
				OutParamTypeNames.emplace_back(Encoded.substr(0, TypeLen));
				Encoded.remove_prefix(TypeLen);
			}
			return true;
		}

		void EnumerateTypes(
		    const FBytecodeModule& Module,
		    std::vector<FLoadedTypeInfo>& Out,
		    const bool bHostSubclassesOnly)
		{
			Out.clear();
			Out.reserve(Module.Types.size());
			for (std::size_t Index = 0; Index < Module.Types.size(); ++Index)
			{
				if (bHostSubclassesOnly && Module.Types[Index].HostBaseTypeName.empty())
				{
					continue;
				}
				Out.push_back(MakeTypeInfo(Module, static_cast<std::uint16_t>(Index)));
			}
		}

		bool EnumerateFields(
		    const FBytecodeModule& Module,
		    const std::string_view TypeName,
		    std::vector<FLoadedFieldInfo>& Out,
		    const bool bDeclaredOnly)
		{
			Out.clear();
			std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
			if (!FindTypeIndex(Module, TypeName, TypeIndex))
			{
				return false;
			}
			const FBytecodeTypeDesc& Type = Module.Types[TypeIndex];
			const std::size_t Start =
			    bDeclaredOnly ? static_cast<std::size_t>(Type.OwnFieldStartIndex) : 0;
			for (std::size_t Index = Start; Index < Type.Fields.size(); ++Index)
			{
				FLoadedFieldInfo Field;
				Field.Name = Type.Fields[Index].Name;
				Field.TypeName = Type.Fields[Index].DeclaredTypeName.empty()
				    ? Type.Fields[Index].TypeName
				    : Type.Fields[Index].DeclaredTypeName;
				Field.RuntimeTypeName = Type.Fields[Index].TypeName;
				Field.FieldIndex = static_cast<std::uint16_t>(Index);
				Field.bDeclaredOnType = Index >= Type.OwnFieldStartIndex;
				Field.bIsStatic = false;
				Field.bIsMutable = Type.Fields[Index].bIsMutable;
				Field.bConstexpr = Type.Fields[Index].bConstexpr;
				Field.DeclMeta = Type.Fields[Index].DeclMeta;
				Out.push_back(std::move(Field));
			}
			for (const FBytecodeFieldDesc& StaticField : Type.StaticFields)
			{
				FLoadedFieldInfo Field;
				Field.Name = StaticField.Name;
				Field.TypeName = StaticField.DeclaredTypeName.empty()
				    ? StaticField.TypeName
				    : StaticField.DeclaredTypeName;
				Field.RuntimeTypeName = StaticField.TypeName;
				Field.FieldIndex = static_cast<std::uint16_t>(-1);
				Field.bDeclaredOnType = true;
				Field.bIsStatic = true;
				Field.bIsMutable = false;
				Field.bConstexpr = StaticField.bConstexpr;
				Field.DeclMeta = StaticField.DeclMeta;
				Out.push_back(std::move(Field));
			}
			return true;
		}

		EMethodResolveStatus TryResolveInstanceMethodOnType(
		    const FBytecodeModule& Module,
		    const std::string_view TypeName,
		    const std::string_view MethodName,
		    const std::uint8_t ArgumentCount,
		    std::uint16_t& OutFunctionIndex,
		    const FValue* const Arguments)
		{
			OutFunctionIndex = static_cast<std::uint16_t>(-1);
			std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
			if (!FindTypeIndex(Module, TypeName, TypeIndex) || MethodName.empty())
			{
				return EMethodResolveStatus::NotFound;
			}
			return TryResolveOnTypeLinkage(
			    Module,
			    Module.Types[TypeIndex].Name,
			    std::string(MethodName),
			    ArgumentCount,
			    EResolveKind::Instance,
			    OutFunctionIndex,
			    Arguments);
		}

		EMethodResolveStatus TryResolveConstructorOnType(
		    const FBytecodeModule& Module,
		    const std::string_view TypeName,
		    const std::uint8_t ArgumentCount,
		    std::uint16_t& OutFunctionIndex,
		    const FValue* const Arguments)
		{
			OutFunctionIndex = static_cast<std::uint16_t>(-1);
			std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
			if (!FindTypeIndex(Module, TypeName, TypeIndex))
			{
				return EMethodResolveStatus::NotFound;
			}
			const std::string SimpleName = TypeSimpleName(Module.Types[TypeIndex].Name);
			return TryResolveOnTypeLinkage(
			    Module,
			    Module.Types[TypeIndex].Name,
			    SimpleName,
			    ArgumentCount,
			    EResolveKind::Constructor,
			    OutFunctionIndex,
			    Arguments);
		}

		bool HasInstanceMethodOnType(
		    const FBytecodeModule& Module,
		    const std::string_view TypeName,
		    const std::string_view MethodName,
		    const std::uint8_t ArgumentCount)
		{
			std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
			const EMethodResolveStatus Status = TryResolveInstanceMethodOnType(
			    Module,
			    TypeName,
			    MethodName,
			    ArgumentCount,
			    FunctionIndex);
			return Status == EMethodResolveStatus::Found || Status == EMethodResolveStatus::Ambiguous;
		}

		EMethodResolveStatus TryResolveFreeFunction(
		    const FBytecodeModule& Module,
		    const std::string_view FunctionName,
		    const std::uint8_t ArgumentCount,
		    std::uint16_t& OutFunctionIndex,
		    const FValue* const Arguments)
		{
			OutFunctionIndex = static_cast<std::uint16_t>(-1);
			if (FunctionName.empty())
			{
				return EMethodResolveStatus::NotFound;
			}
			const std::string Wanted(FunctionName);

			std::vector<std::uint16_t> Candidates;
			if (const auto* Exact = FindExactFreeFunction(Module, Wanted, ArgumentCount))
			{
				if (Arguments == nullptr)
				{
					OutFunctionIndex = *Exact;
					return EMethodResolveStatus::Found;
				}
				Candidates.push_back(*Exact);
			}
			else
			{
				CollectFreeFunctionCandidates(Module, Wanted, ArgumentCount, Candidates);
			}
			return NarrowByRuntimeArgs(
			    Module,
			    Candidates,
			    Arguments,
			    ArgumentCount,
			    OutFunctionIndex);
		}

		EMethodResolveStatus FFreeFunctionLookupCache::Resolve(
		    const std::shared_ptr<const FBytecodeModule>& Owner,
		    const std::uint64_t Generation, const std::string_view Name, const std::uint8_t ArgumentCount,
		    std::uint16_t& OutFunctionIndex, const FValue* const Arguments)
		{
			const auto& Module = *Owner;
			if (Name.empty() || Name.size() > 256)
			{
				return TryResolveFreeFunction(Module, Name, ArgumentCount, OutFunctionIndex, Arguments);
			}
			if (CachedModule != &Module || CachedGeneration != Generation ||
			    Owner.owner_before(CachedOwner) || CachedOwner.owner_before(Owner))
			{
				for (auto& Entry : Entries)
				{
					Entry.Name.clear();
				}
				CachedOwner = Owner;
				CachedModule = &Module;
				CachedGeneration = Generation;
				NextEntry = 0;
			}
			OutFunctionIndex = static_cast<std::uint16_t>(-1);
			for (const auto& Entry : Entries)
			{
				if (Entry.Name == Name && Entry.ArgumentCount == ArgumentCount)
				{
					return NarrowByRuntimeArgs(Module, Entry.Candidates, Arguments, ArgumentCount, OutFunctionIndex);
				}
			}
			std::vector<std::uint16_t> Candidates;
			if (const auto* Exact = FindExactFreeFunction(Module, std::string(Name), ArgumentCount))
			{
				Candidates.push_back(*Exact);
			}
			else
			{
				CollectFreeFunctionCandidates(Module, Name, ArgumentCount, Candidates);
			}
			const auto Status = NarrowByRuntimeArgs(Module, Candidates, Arguments, ArgumentCount, OutFunctionIndex);
			if (Candidates.size() <= 64)
			{
				auto& Entry = Entries[NextEntry];
				Entry.Name.assign(Name);
				Entry.ArgumentCount = ArgumentCount;
				Entry.Candidates = std::move(Candidates);
				NextEntry = (NextEntry + 1) % Entries.size();
			}
			return Status;
		}

		bool HasFreeFunction(
		    const FBytecodeModule& Module,
		    const std::string_view FunctionName,
		    const std::uint8_t ArgumentCount)
		{
			std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
			const EMethodResolveStatus Status = TryResolveFreeFunction(
			    Module,
			    FunctionName,
			    ArgumentCount,
			    FunctionIndex);
			return Status == EMethodResolveStatus::Found || Status == EMethodResolveStatus::Ambiguous;
		}

		bool FindFieldIndex(
		    const FBytecodeModule& Module,
		    const std::string_view TypeName,
		    const std::string_view FieldName,
		    std::uint16_t& OutFieldIndex)
		{
			OutFieldIndex = static_cast<std::uint16_t>(-1);
			std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
			if (!FindTypeIndex(Module, TypeName, TypeIndex) || FieldName.empty())
			{
				return false;
			}
			const FBytecodeTypeDesc& Type = Module.Types[TypeIndex];
			for (std::size_t Index = 0; Index < Type.Fields.size(); ++Index)
			{
				if (Type.Fields[Index].Name == FieldName)
				{
					OutFieldIndex = static_cast<std::uint16_t>(Index);
					return true;
				}
			}
			return false;
		}

		bool EnumerateInstanceMethods(
		    const FBytecodeModule& Module,
		    const std::string_view TypeName,
		    std::vector<FInstanceMethodInfo>& Out,
		    const bool bIncludeInherited,
		    const bool bIncludeConstructors,
		    const bool bIncludeDestructors)
		{
			Out.clear();
			std::uint16_t RootTypeIndex = static_cast<std::uint16_t>(-1);
			if (FindTypeIndex(Module, TypeName, RootTypeIndex))
			{
				std::vector<std::uint16_t> TypeQueue{RootTypeIndex};
				std::unordered_set<std::uint16_t> VisitedTypes;
				std::unordered_set<std::uint16_t> VisitedInterfaces;
				std::unordered_map<std::string, std::size_t> SeenIndices;
				for (std::size_t QueueIndex = 0; QueueIndex < TypeQueue.size(); ++QueueIndex)
				{
					const std::uint16_t TypeIndex = TypeQueue[QueueIndex];
					if (TypeIndex >= Module.Types.size() || !VisitedTypes.insert(TypeIndex).second)
					{
						continue;
					}
					AppendMethodsDeclaredOnType(
					    Module,
					    TypeIndex,
					    bIncludeConstructors,
					    bIncludeDestructors,
					    SeenIndices,
					    Out);
					if (!bIncludeInherited)
					{
						break;
					}
					for (const std::uint16_t IfaceIndex :
					    Module.Types[TypeIndex].ImplementedInterfaceIndices)
					{
						if (IfaceIndex < Module.Interfaces.size() && VisitedInterfaces.insert(IfaceIndex).second)
						{
							AppendMethodsDeclaredOnInterface(
							    Module,
							    IfaceIndex,
							    bIncludeConstructors,
							    bIncludeDestructors,
							    SeenIndices,
							    Out);
							std::vector<std::uint16_t> BaseQueue =
							    Module.Interfaces[IfaceIndex].BaseInterfaceIndices;
							for (std::size_t BaseQi = 0; BaseQi < BaseQueue.size(); ++BaseQi)
							{
								const std::uint16_t BaseIface = BaseQueue[BaseQi];
								if (BaseIface >= Module.Interfaces.size() || !VisitedInterfaces.insert(BaseIface).second)
								{
									continue;
								}
								AppendMethodsDeclaredOnInterface(
								    Module,
								    BaseIface,
								    bIncludeConstructors,
								    bIncludeDestructors,
								    SeenIndices,
								    Out);
								for (const std::uint16_t Nested :
								    Module.Interfaces[BaseIface].BaseInterfaceIndices)
								{
									BaseQueue.push_back(Nested);
								}
							}
						}
					}
					for (const std::uint16_t BaseIdx : Module.Types[TypeIndex].BaseTypeIndices)
					{
						if (BaseIdx < Module.Types.size())
						{
							TypeQueue.push_back(BaseIdx);
						}
					}
				}
				return true;
			}

			// Interface type name: abstract stubs + default method bodies.
			std::uint16_t RootIfaceIndex = static_cast<std::uint16_t>(-1);
			if (!FindInterfaceIndex(Module, TypeName, RootIfaceIndex))
			{
				return false;
			}
			std::unordered_set<std::uint16_t> VisitedInterfaces;
			std::unordered_map<std::string, std::size_t> SeenIndices;
			std::vector<std::uint16_t> IfaceQueue{RootIfaceIndex};
			for (std::size_t QueueIndex = 0; QueueIndex < IfaceQueue.size(); ++QueueIndex)
			{
				const std::uint16_t IfaceIndex = IfaceQueue[QueueIndex];
				if (IfaceIndex >= Module.Interfaces.size() || !VisitedInterfaces.insert(IfaceIndex).second)
				{
					continue;
				}
				AppendMethodsDeclaredOnInterface(
				    Module,
				    IfaceIndex,
				    bIncludeConstructors,
				    bIncludeDestructors,
				    SeenIndices,
				    Out);
				if (!bIncludeInherited)
				{
					break;
				}
				for (const std::uint16_t BaseIface :
				    Module.Interfaces[IfaceIndex].BaseInterfaceIndices)
				{
					if (BaseIface < Module.Interfaces.size())
					{
						IfaceQueue.push_back(BaseIface);
					}
				}
			}
			return true;
		}

		bool EnumerateStaticMethods(
		    const FBytecodeModule& Module,
		    const std::string_view TypeName,
		    std::vector<FStaticMethodInfo>& Out)
		{
			Out.clear();
			std::string OwnerLinkage;
			std::uint16_t DeclaringTypeIndex = static_cast<std::uint16_t>(-1);
			std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
			std::uint16_t IfaceIndex = static_cast<std::uint16_t>(-1);
			if (FindTypeIndex(Module, TypeName, TypeIndex))
			{
				OwnerLinkage = Module.Types[TypeIndex].Name;
				DeclaringTypeIndex = TypeIndex;
			}
			else if (FindInterfaceIndex(Module, TypeName, IfaceIndex))
			{
				OwnerLinkage = Module.Interfaces[IfaceIndex].Name;
			}
			else
			{
				return false;
			}

			const std::string OwnerSimple = TypeSimpleName(OwnerLinkage);
			const std::string Prefix = OwnerLinkage + "::";
			std::unordered_set<std::uint16_t> SeenIndices;
			for (const auto& Entry : Module.FunctionIndices)
			{
				if (Entry.first.compare(0, Prefix.size(), Prefix) != 0 || Entry.second >= Module.Functions.size() || !SeenIndices.insert(Entry.second).second)
				{
					continue;
				}
				const FBytecodeFunction& Function = Module.Functions[Entry.second];
				// Static members compile as non-methods (no `this`).
				if (Function.bIsMethod)
				{
					continue;
				}
				std::string BareName;
				bool bIsDestructor = false;
				if (!TryParseMethodLeaf(Function.Name, BareName, bIsDestructor) || bIsDestructor || BareName == OwnerSimple)
				{
					continue;
				}
				FStaticMethodInfo Info;
				Info.TemplateInstance = Function.TemplateInstance;
				Info.Name = SourceTemplateName(Module, Function, std::move(BareName));
				Info.ParamCount = Function.ParamCount;
				(void)DecodeMethodParamTypeNames(Function.Name, Info.ParamTypeNames);
				MakeSignatureTypeNamesUserFacing(Module, Info.ParamTypeNames);
				Info.bReturnsVoid = Function.bReturnsVoid;
				Info.bReturnsRef = Function.bReturnsRef;
				Info.bIsConst = Function.bIsConstMethod;
				Info.bConstexpr = Function.bConstexpr;
				Info.bDeleted = Function.bDeleted;
				Info.FunctionIndex = Entry.second;
				Info.DeclaringTypeIndex = DeclaringTypeIndex;
				Info.DeclaringTypeName = OwnerLinkage;
				Info.LinkageName = Entry.first;
				FillFunctionSignatureExtras(
				    Function,
				    Info.ReturnTypeName,
				    Info.ParamNames,
				    Info.ParamFlags);
				RestoreDeclaredSignatureTypes(
				    Function.DeclMeta,
				    Info.ReturnTypeName,
				    Info.ParamTypeNames);
				MakeSignatureTypeNameUserFacing(Module, Info.ReturnTypeName);
				Info.bReturnsConstRef =
				    Runtime::IsConstReferenceTypeName(Info.ReturnTypeName);
				Info.DeclMeta = PublicDeclMeta(Function.DeclMeta);
				Out.push_back(std::move(Info));
			}
			std::sort(
			    Out.begin(),
			    Out.end(),
			    [](const FStaticMethodInfo& Left, const FStaticMethodInfo& Right)
			{
				return Left.FunctionIndex < Right.FunctionIndex;
			});
			return true;
		}

		void EnumerateFreeFunctions(
		    const FBytecodeModule& Module,
		    std::vector<FFreeFunctionInfo>& Out,
		    const bool bVisibleFromEntryOnly)
		{
			Out.clear();
			std::unordered_set<std::uint16_t> SeenIndices;
			Out.reserve(Module.Functions.size());
			for (const auto& Entry : Module.FunctionIndices)
			{
				const std::uint16_t FunctionIndex = Entry.second;
				if (FunctionIndex >= Module.Functions.size() || Module.Functions[FunctionIndex].bIsMethod
				    // Static members have no `this`, so bIsMethod is false; their
				    // canonical linkage still has an owner/method separator. Namespace
				    // qualification is canonicalized to '.', and separators inside
				    // encoded parameter types are ignored by this helper.
				    || HasMethodOwnerSeparator(Entry.first) || (bVisibleFromEntryOnly && !IsVisibleFromEntry(Module, Entry.first)) || !SeenIndices.insert(FunctionIndex).second)
				{
					continue;
				}
				// Inspect only the non-generic linkage base. Both generic arguments and
				// encoded parameter types may contain unit / `::` separators.
				const std::string_view FullLinkage = Entry.first;
				const std::string_view Linkage = FullLinkage.substr(
				    FindLinkageIdentityStart(FullLinkage));
				const std::size_t Signature = FindFunctionSignatureMarker(Linkage);
				const std::string_view Identity = Linkage.substr(
				    0,
				    Signature == std::string_view::npos ? Linkage.size() : Signature);
				const std::size_t Generic = Identity.find('#');
				const std::string_view Base = Identity.substr(
				    0,
				    Generic == std::string_view::npos ? Identity.size() : Generic);
				// bIsMethod is the authoritative method/free-function distinction.
				// Namespace qualification uses '.' internally and may also arrive as
				// source-facing '::', so reuse the canonical leaf-name helper.
				std::string BareName = TypeSimpleName(Base);
				if (BareName.empty())
				{
					continue;
				}
				const FBytecodeFunction& Function = Module.Functions[FunctionIndex];
				FFreeFunctionInfo Info;
				Info.TemplateInstance = Function.TemplateInstance;
				Info.Name = SourceTemplateName(Module, Function, std::move(BareName));
				Info.ParamCount = Function.ParamCount;
				// Generic linkage carries type arguments and overload identity, which
				// are not the instantiated value parameters. The callable signature
				// already contains their substituted types (including ref qualifiers).
				std::string SignatureReturnType;
				if (!Runtime::ParseFunctionType(Function.ClosureTypeName, Info.ParamTypeNames, SignatureReturnType) || Info.ParamTypeNames.size() != Function.ParamCount)
				{
					(void)DecodeMethodParamTypeNames(Function.Name, Info.ParamTypeNames);
				}
				MakeSignatureTypeNamesUserFacing(Module, Info.ParamTypeNames);
				Info.bReturnsVoid = Function.bReturnsVoid;
				Info.bReturnsRef = Function.bReturnsRef;
				Info.bDeleted = Function.bDeleted;
				Info.FunctionIndex = FunctionIndex;
				Info.bConstexpr = Function.bConstexpr;
				Info.LinkageName = Entry.first;
				Info.ModulePath = !Function.ModulePath.empty()
				    ? Function.ModulePath
				    : (Function.Location.FileLabel.empty()
				              ? LinkageModulePath(Entry.first)
				              : std::string(Function.Location.FileLabel));
				Info.bExported = FindLinkageIdentityStart(Entry.first) == 0;
				FillFunctionSignatureExtras(
				    Function,
				    Info.ReturnTypeName,
				    Info.ParamNames,
				    Info.ParamFlags);
				RestoreDeclaredSignatureTypes(
				    Function.DeclMeta,
				    Info.ReturnTypeName,
				    Info.ParamTypeNames);
				MakeSignatureTypeNameUserFacing(Module, Info.ReturnTypeName);
				Info.bReturnsConstRef =
				    Runtime::IsConstReferenceTypeName(Info.ReturnTypeName);
				Info.DeclMeta = PublicDeclMeta(Function.DeclMeta);
				Out.push_back(std::move(Info));
			}
			std::sort(
			    Out.begin(),
			    Out.end(),
			    [](const FFreeFunctionInfo& Left, const FFreeFunctionInfo& Right)
			{
				return Left.FunctionIndex < Right.FunctionIndex;
			});
		}

		void EnumerateGlobals(
		    const FBytecodeModule& Module,
		    std::vector<FLoadedGlobalInfo>& Out,
		    const bool bVisibleFromEntryOnly,
		    const bool bIncludeStaticMembers)
		{
			Out.clear();
			Out.reserve(Module.GlobalNames.size());
			for (std::size_t Index = 0; Index < Module.GlobalNames.size(); ++Index)
			{
				const std::string& LinkageName = Module.GlobalNames[Index];
				const FBytecodeGlobalMeta* const Meta =
				    Index < Module.GlobalMetadata.size()
				    ? &Module.GlobalMetadata[Index]
				    : nullptr;
				if ((Meta && (Meta->bLocalStatic || (!bIncludeStaticMembers && Meta->bStaticMember))) || (bVisibleFromEntryOnly && !IsVisibleFromEntry(Module, LinkageName)))
				{
					continue;
				}
				FLoadedGlobalInfo Info;
				Info.Name = TypeSimpleName(LinkageName);
				Info.LinkageName = LinkageName;
				Info.ModulePath = Meta && !Meta->ModulePath.empty()
				    ? Meta->ModulePath
				    : LinkageModulePath(LinkageName);
				Info.RuntimeTypeName = Index < Module.GlobalTypeNames.size()
				    ? Module.GlobalTypeNames[Index]
				    : std::string{};
				Info.DeclaredTypeName = Meta && !Meta->DeclaredTypeName.empty()
				    ? Meta->DeclaredTypeName
				    : Info.RuntimeTypeName;
				Info.GlobalSlot = static_cast<std::uint16_t>(Index);
				Info.bConst = Meta && Meta->bConst;
				Info.bConstexpr = Meta && Meta->bConstexpr;
				Info.bExported = Meta
				    ? Meta->bExported
				    : FindLinkageIdentityStart(LinkageName) == 0;
				Info.bStaticMember = Meta && Meta->bStaticMember;
				if (Meta)
				{
					Info.DeclMeta = PublicDeclMeta(Meta->DeclMeta);
				}
				Out.push_back(std::move(Info));
			}
		}

		void EnumerateInterfaces(
		    const FBytecodeModule& Module,
		    std::vector<FLoadedInterfaceInfo>& Out)
		{
			Out.clear();
			Out.reserve(Module.Interfaces.size());
			for (std::size_t Index = 0; Index < Module.Interfaces.size(); ++Index)
			{
				const FBytecodeInterfaceDesc& Iface = Module.Interfaces[Index];
				FLoadedInterfaceInfo Info;
				Info.InterfaceIndex = static_cast<std::uint16_t>(Index);
				Info.Name = Iface.Name;
				Info.SimpleName = ModuleMeta::TypeSimpleName(Iface.Name);
				Info.Scope = Iface.Scope;
				Info.NestedTypes = Iface.NestedTypes;
				Info.BaseInterfaceIndices = Iface.BaseInterfaceIndices;
				Info.DeclMeta = Iface.DeclMeta;
				Info.Fields.reserve(Iface.Fields.size() + Iface.StaticFields.size());
				for (std::size_t FieldIndex = 0; FieldIndex < Iface.Fields.size(); ++FieldIndex)
				{
					const FBytecodeFieldDesc& Field = Iface.Fields[FieldIndex];
					FLoadedFieldInfo FieldInfo;
					FieldInfo.Name = Field.Name;
					FieldInfo.TypeName = Field.DeclaredTypeName.empty()
					    ? Field.TypeName
					    : Field.DeclaredTypeName;
					FieldInfo.RuntimeTypeName = Field.TypeName;
					FieldInfo.FieldIndex = static_cast<std::uint16_t>(FieldIndex);
					FieldInfo.bDeclaredOnType = true;
					FieldInfo.bIsStatic = false;
					FieldInfo.bIsMutable = Field.bIsMutable;
					FieldInfo.bConstexpr = Field.bConstexpr;
					FieldInfo.DeclMeta = Field.DeclMeta;
					Info.Fields.push_back(std::move(FieldInfo));
				}
				for (const FBytecodeFieldDesc& StaticField : Iface.StaticFields)
				{
					FLoadedFieldInfo FieldInfo;
					FieldInfo.Name = StaticField.Name;
					FieldInfo.TypeName = StaticField.DeclaredTypeName.empty()
					    ? StaticField.TypeName
					    : StaticField.DeclaredTypeName;
					FieldInfo.RuntimeTypeName = StaticField.TypeName;
					FieldInfo.FieldIndex = static_cast<std::uint16_t>(-1);
					FieldInfo.bDeclaredOnType = true;
					FieldInfo.bIsStatic = true;
					FieldInfo.bIsMutable = false;
					FieldInfo.bConstexpr = StaticField.bConstexpr;
					FieldInfo.DeclMeta = StaticField.DeclMeta;
					Info.Fields.push_back(std::move(FieldInfo));
				}
				Out.push_back(std::move(Info));
			}
		}

		void EnumerateEnums(
		    const FBytecodeModule& Module,
		    std::vector<FLoadedEnumInfo>& Out)
		{
			Out.clear();
			Out.reserve(Module.Enums.size());
			for (const FBytecodeEnumDesc& EnumDesc : Module.Enums)
			{
				FLoadedEnumInfo Info;
				Info.Name = EnumDesc.Name;
				Info.SimpleName = ModuleMeta::TypeSimpleName(EnumDesc.Name);
				Info.Scope = EnumDesc.Scope;
				Info.bScoped = EnumDesc.bScoped;
				Info.UnderlyingTypeName = EnumDesc.UnderlyingTypeName;
				Info.DeclMeta = EnumDesc.DeclMeta;
				Info.Members.reserve(EnumDesc.Members.size());
				for (const FBytecodeEnumMemberDesc& Member : EnumDesc.Members)
				{
					FLoadedEnumMemberInfo MemberInfo;
					MemberInfo.Name = Member.Name;
					MemberInfo.Value = Member.Value;
					MemberInfo.DeclMeta = Member.DeclMeta;
					Info.Members.push_back(std::move(MemberInfo));
				}
				Out.push_back(std::move(Info));
			}
		}
	}
}
