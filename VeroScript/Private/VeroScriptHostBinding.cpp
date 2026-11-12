// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHostBinding.h"
#include "VeroScriptAnnotation.h"

#include "VeroScriptRuntime.h"
#include "VeroScriptNumericBindings.h"

#include <cctype>
#include <exception>
#include <charconv>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace VeroScript
{
	namespace
	{
		const FHostTypeDesc* FindSignatureType(
		    const IHostBindingCatalog& Catalog, const std::string_view Name)
		{
			if (const FHostTypeDesc* const Type = Catalog.FindType(Name))
			{
				return Type;
			}
			// Numeric types are immutable Core bindings, outside the mutable user
			// registry. During their catalog construction all three types are already
			// registered locally, so those lookups never reenter catalog initialization.
			if (Numerics::IsTypeName(Name) || Name == "std.numeric.rounding_mode")
			{
				return Numerics::GetBindingCatalog().FindType(Name);
			}
			return nullptr;
		}

		bool HostTypeHasStrongEnumSemantics(const FHostTypeDesc* const HostType)
		{
			if (!HostType || HostType->Kind != EHostTypeKind::Enum)
			{
				return false;
			}
			if (HostType->bScopedEnum)
			{
				return true;
			}
			for (const FHostMemberDesc& Member : HostType->Members)
			{
				if (Member.Kind == EHostMemberKind::EnumMember && Member.ReturnHostTypeName == HostType->Name)
				{
					return true;
				}
			}
			return false;
		}

		/**
		 * Annotation spelling accepted by the parser: `Name(` or `@name(`.
		 * Mirrors VeroScriptParser::LooksLikeAnnotationStart.
		 */
		bool IsValidAnnotationName(const std::string_view Name)
		{
			std::string_view Ident = Name;
			if (!Ident.empty() && Ident.front() == '@')
			{
				Ident.remove_prefix(1);
			}
			if (Ident.empty())
			{
				return false;
			}
			const char First = Ident.front();
			if ((First < 'A' || First > 'Z') && (First < 'a' || First > 'z') && First != '_')
			{
				return false;
			}
			for (const char Char : Ident)
			{
				const bool bWord =
				    (Char >= 'A' && Char <= 'Z') || (Char >= 'a' && Char <= 'z') || (Char >= '0' && Char <= '9') || Char == '_';
				if (!bWord)
				{
					return false;
				}
			}
			return true;
		}

		bool ValidateDeclarationAnnotations(
		    const std::vector<FHostAnnotationInstance>& Annotations,
		    std::string& OutErrorMessageId)
		{
			for (const FHostAnnotationInstance& Annotation : Annotations)
			{
				if (!IsValidAnnotationName(Annotation.Name))
				{
					OutErrorMessageId =
					    "diag.hostbinding_invalid_declaration_annotation_name";
					return false;
				}
				std::unordered_set<std::string> SeenArguments;
				for (const FHostAnnotationArgument& Argument : Annotation.Arguments)
				{
					if (Argument.Name.empty() || Argument.Name.front() == '@' || !IsValidAnnotationName(Argument.Name))
					{
						OutErrorMessageId =
						    "diag.hostbinding_invalid_declaration_annotation_argument";
						return false;
					}
					if (!SeenArguments.insert(Argument.Name).second)
					{
						OutErrorMessageId =
						    "diag.hostbinding_duplicate_declaration_annotation_argument";
						return false;
					}
				}
			}
			return true;
		}

		bool ValidateParameterAnnotations(
		    const std::vector<std::vector<FHostAnnotationInstance>>& ParamAnnotations,
		    const std::size_t ParamCount,
		    std::string& OutErrorMessageId)
		{
			if (!ParamAnnotations.empty() && ParamAnnotations.size() != ParamCount)
			{
				OutErrorMessageId =
				    "diag.hostbinding_param_annotation_count_mismatch";
				return false;
			}
			for (const std::vector<FHostAnnotationInstance>& Annotations : ParamAnnotations)
			{
				if (!ValidateDeclarationAnnotations(Annotations, OutErrorMessageId))
				{
					return false;
				}
			}
			return true;
		}

		std::string HostOrNativeTypeName(
		    const ENativeType Type,
		    const std::string& HostName,
		    const std::string& ElementTypeName = {},
		    const std::string& KeyTypeName = {},
		    const std::string& ValueTypeName = {})
		{
			if (Type == ENativeType::HostObject || Type == ENativeType::Enum || Type == ENativeType::Function)
			{
				return HostName.empty() ? std::string(NativeTypeName(Type)) : HostName;
			}
			if (Type == ENativeType::Array || Type == ENativeType::Set || Type == ENativeType::MultiSet || Type == ENativeType::OrderedMultiSet || Type == ENativeType::Queue || Type == ENativeType::PriorityQueue || Type == ENativeType::Stack || Type == ENativeType::Deque || Type == ENativeType::Channel)
			{
				if (!ElementTypeName.empty())
				{
					return MakeNativeUnaryContainerTypeName(
					    NativeTypeName(Type),
					    CanonicalizeNativeElementTypeName(ElementTypeName));
				}
				return NativeTypeName(Type);
			}
			if (NativeTypeIsBinaryMap(Type))
			{
				if (!KeyTypeName.empty() && !ValueTypeName.empty())
				{
					return MakeNativeBinaryContainerTypeName(
					    NativeTypeName(Type),
					    CanonicalizeNativeElementTypeName(KeyTypeName),
					    CanonicalizeNativeElementTypeName(ValueTypeName));
				}
				return NativeTypeName(Type);
			}
			return NativeTypeName(Type);
		}

		std::string JoinParamTypes(
		    const std::vector<ENativeType>& ParamTypes,
		    const std::vector<std::string>& ParamHostTypeNames,
		    const std::vector<std::string>& ParamElementTypeNames = {},
		    const std::vector<std::uint8_t>& ParamFlags = {},
		    const std::vector<std::string>& ParamKeyTypeNames = {},
		    const std::vector<std::string>& ParamValueTypeNames = {},
		    const std::vector<std::string>& ParamNames = {})
		{
			std::string Text = "(";
			for (std::size_t Index = 0; Index < ParamTypes.size(); ++Index)
			{
				if (Index > 0)
				{
					Text += ", ";
				}
				const std::uint8_t Flag =
				    Index < ParamFlags.size() ? ParamFlags[Index] : std::uint8_t{0};
				const bool bIsRef = NativeParamFlagIsRef(Flag);
				if (bIsRef && (Flag & NativeParamFlagConstRef) != 0)
				{
					Text += "const ";
				}
				const std::string HostName =
				    Index < ParamHostTypeNames.size() ? ParamHostTypeNames[Index] : std::string{};
				const std::string ElementName =
				    Index < ParamElementTypeNames.size()
				    ? ParamElementTypeNames[Index]
				    : std::string{};
				const std::string KeyName =
				    Index < ParamKeyTypeNames.size() ? ParamKeyTypeNames[Index] : std::string{};
				const std::string ValueName =
				    Index < ParamValueTypeNames.size()
				    ? ParamValueTypeNames[Index]
				    : std::string{};
				Text += HostOrNativeTypeName(
				    ParamTypes[Index], HostName, ElementName, KeyName, ValueName);
				if (bIsRef)
				{
					Text.push_back('&');
				}
				if (Index < ParamNames.size() && !ParamNames[Index].empty())
				{
					Text.push_back(' ');
					Text += ParamNames[Index];
				}
			}
			Text += ')';
			return Text;
		}

		bool IsPrimitiveNativeElementTypeName(const std::string_view Name)
		{
			return Name == "int" || Name == "int64" || Name == "uint64" || Name == "bool" || Name == "double" || Name == "string" || Name == "float" || Name == "unsigned int" || Name == "unsigned" || Name == "char" || Name == "short" || Name == "long" || Name == "unsigned char" || Name == "unsigned short" || Name == "unsigned long" || Name == "wstring";
		}

		bool ValidateNativeParamFlags(
		    const std::vector<ENativeType>& ParamTypes,
		    const std::vector<std::uint8_t>& ParamFlags,
		    std::string& OutErrorMessageId)
		{
			if (ParamFlags.empty())
			{
				return true;
			}
			if (ParamFlags.size() != ParamTypes.size())
			{
				OutErrorMessageId = "diag.hostbinding_param_flags_size_mismatch";
				return false;
			}
			for (std::size_t Index = 0; Index < ParamFlags.size(); ++Index)
			{
				const std::uint8_t Flag = ParamFlags[Index];
				if (Flag == 0)
				{
					continue;
				}
				constexpr std::uint8_t KnownFlags =
				    NativeParamFlagRef | NativeParamFlagConstRef;
				if ((Flag & static_cast<std::uint8_t>(~KnownFlags)) != 0)
				{
					OutErrorMessageId = "diag.hostbinding_param_flags_unknown_bits";
					return false;
				}
				if ((Flag & NativeParamFlagConstRef) != 0 && (Flag & NativeParamFlagRef) == 0)
				{
					OutErrorMessageId = "diag.hostbinding_param_flags_const_ref_requires_ref";
					return false;
				}
				if (!NativeTypeSupportsRef(ParamTypes[Index]))
				{
					OutErrorMessageId = "diag.hostbinding_param_flags_ref_requires_non_void";
					return false;
				}
			}
			return true;
		}

		void FillParamHostTypeNames(
		    FNativeFunction& Entry,
		    const std::string& DefaultHostType,
		    const std::vector<std::string>& MemberParamHostNames,
		    const bool bHasImplicitReceiver)
		{
			Entry.ParamHostTypeNames.assign(Entry.ParamTypes.size(), {});
			for (std::size_t Index = 0; Index < Entry.ParamTypes.size(); ++Index)
			{
				if (Entry.ParamTypes[Index] != ENativeType::HostObject && Entry.ParamTypes[Index] != ENativeType::Enum && Entry.ParamTypes[Index] != ENativeType::Function)
				{
					continue;
				}
				if (bHasImplicitReceiver && Index == 0)
				{
					Entry.ParamHostTypeNames[Index] = DefaultHostType;
					continue;
				}
				const std::size_t MemberIndex = bHasImplicitReceiver ? Index - 1 : Index;
				if (MemberIndex < MemberParamHostNames.size() && !MemberParamHostNames[MemberIndex].empty())
				{
					Entry.ParamHostTypeNames[Index] = MemberParamHostNames[MemberIndex];
				}
				else
				{
					Entry.ParamHostTypeNames[Index] = DefaultHostType;
				}
			}
		}

		void FillParamElementTypeNames(
		    FNativeFunction& Entry,
		    const std::vector<std::string>& MemberParamElementNames,
		    const bool bHasImplicitReceiver)
		{
			Entry.ParamElementTypeNames.assign(Entry.ParamTypes.size(), {});
			for (std::size_t Index = 0; Index < Entry.ParamTypes.size(); ++Index)
			{
				if (Entry.ParamTypes[Index] != ENativeType::Array && Entry.ParamTypes[Index] != ENativeType::Set && Entry.ParamTypes[Index] != ENativeType::MultiSet && Entry.ParamTypes[Index] != ENativeType::OrderedMultiSet && Entry.ParamTypes[Index] != ENativeType::Queue && Entry.ParamTypes[Index] != ENativeType::PriorityQueue && Entry.ParamTypes[Index] != ENativeType::Stack && Entry.ParamTypes[Index] != ENativeType::Deque && Entry.ParamTypes[Index] != ENativeType::Channel)
				{
					continue;
				}
				if (bHasImplicitReceiver && Index == 0)
				{
					continue;
				}
				const std::size_t MemberIndex = bHasImplicitReceiver ? Index - 1 : Index;
				if (MemberIndex < MemberParamElementNames.size())
				{
					Entry.ParamElementTypeNames[Index] =
					    CanonicalizeNativeElementTypeName(MemberParamElementNames[MemberIndex]);
				}
			}
		}

		void FillParamKeyValueTypeNames(
		    FNativeFunction& Entry,
		    const std::vector<std::string>& MemberParamKeyNames,
		    const std::vector<std::string>& MemberParamValueNames,
		    const bool bHasImplicitReceiver)
		{
			Entry.ParamKeyTypeNames.assign(Entry.ParamTypes.size(), {});
			Entry.ParamValueTypeNames.assign(Entry.ParamTypes.size(), {});
			for (std::size_t Index = 0; Index < Entry.ParamTypes.size(); ++Index)
			{
				if (!NativeTypeIsBinaryMap(Entry.ParamTypes[Index]))
				{
					continue;
				}
				if (bHasImplicitReceiver && Index == 0)
				{
					continue;
				}
				const std::size_t MemberIndex = bHasImplicitReceiver ? Index - 1 : Index;
				if (MemberIndex < MemberParamKeyNames.size())
				{
					Entry.ParamKeyTypeNames[Index] =
					    CanonicalizeNativeElementTypeName(MemberParamKeyNames[MemberIndex]);
				}
				if (MemberIndex < MemberParamValueNames.size())
				{
					Entry.ParamValueTypeNames[Index] =
					    CanonicalizeNativeElementTypeName(MemberParamValueNames[MemberIndex]);
				}
			}
		}

		void FillParamFlags(
		    FNativeFunction& Entry,
		    const std::vector<std::uint8_t>& MemberParamFlags,
		    const bool bHasImplicitReceiver)
		{
			Entry.ParamFlags.assign(Entry.ParamTypes.size(), 0);
			for (std::size_t Index = 0; Index < Entry.ParamTypes.size(); ++Index)
			{
				if (bHasImplicitReceiver && Index == 0)
				{
					continue;
				}
				const std::size_t MemberIndex = bHasImplicitReceiver ? Index - 1 : Index;
				if (MemberIndex < MemberParamFlags.size())
				{
					Entry.ParamFlags[Index] = MemberParamFlags[MemberIndex];
				}
			}
		}

		std::string TrimTypeName(const std::string_view Text)
		{
			std::size_t Begin = 0;
			std::size_t End = Text.size();
			while (Begin < End && std::isspace(static_cast<unsigned char>(Text[Begin])))
			{
				++Begin;
			}
			while (End > Begin && std::isspace(static_cast<unsigned char>(Text[End - 1])))
			{
				--End;
			}
			return std::string(Text.substr(Begin, End - Begin));
		}

		bool SplitGenericTypeName(
		    const std::string_view Text,
		    std::string& OutBase,
		    std::vector<std::string>& OutArguments)
		{
			OutBase.clear();
			OutArguments.clear();
			const std::string Trimmed = TrimTypeName(Text);
			const std::size_t Open = Trimmed.find('<');
			if (Open == std::string::npos || Trimmed.empty() || Trimmed.back() != '>')
			{
				return false;
			}
			int Depth = 0;
			int ParenthesisDepth = 0;
			std::size_t Start = Open + 1;
			for (std::size_t Index = Open; Index < Trimmed.size(); ++Index)
			{
				const char Char = Trimmed[Index];
				if (Char == '<')
				{
					++Depth;
				}
				else if (Char == '>' && (Index == 0 || Trimmed[Index - 1] != '-'))
				{
					--Depth;
					if (Depth < 0 || (Depth == 0 && Index + 1 != Trimmed.size()))
					{
						return false;
					}
				}
				else if (Char == '(')
				{
					++ParenthesisDepth;
				}
				else if (Char == ')')
				{
					if (--ParenthesisDepth < 0)
					{
						return false;
					}
				}
				else if (Char == ',' && Depth == 1 && ParenthesisDepth == 0)
				{
					std::string Argument = TrimTypeName(
					    std::string_view(Trimmed).substr(Start, Index - Start));
					if (Argument.empty())
					{
						return false;
					}
					OutArguments.push_back(std::move(Argument));
					Start = Index + 1;
				}
			}
			if (Depth != 0 || ParenthesisDepth != 0)
			{
				return false;
			}
			std::string Last = TrimTypeName(
			    std::string_view(Trimmed).substr(Start, Trimmed.size() - Start - 1));
			if (Last.empty())
			{
				return false;
			}
			OutArguments.push_back(std::move(Last));
			OutBase = TrimTypeName(std::string_view(Trimmed).substr(0, Open));
			return !OutBase.empty();
		}

		std::string SubstituteAliasParameters(
		    const std::string_view Target,
		    const std::unordered_map<std::string, std::string>& Substitution)
		{
			std::string Result;
			Result.reserve(Target.size());
			for (std::size_t Index = 0; Index < Target.size();)
			{
				const unsigned char First = static_cast<unsigned char>(Target[Index]);
				if (std::isalpha(First) || Target[Index] == '_')
				{
					std::size_t End = Index + 1;
					while (End < Target.size())
					{
						const unsigned char Char = static_cast<unsigned char>(Target[End]);
						if (!std::isalnum(Char) && Target[End] != '_')
						{
							break;
						}
						++End;
					}
					const std::string Token(Target.substr(Index, End - Index));
					const auto It = Substitution.find(Token);
					Result += It == Substitution.end() ? Token : It->second;
					Index = End;
					continue;
				}
				Result.push_back(Target[Index++]);
			}
			return Result;
		}

		std::size_t FindHostArrayDimension(const std::string_view TypeName)
		{
			std::size_t AngleDepth = 0;
			std::size_t ParenthesisDepth = 0;
			for (std::size_t Index = 0; Index < TypeName.size(); ++Index)
			{
				const char Character = TypeName[Index];
				if (Character == '<')
				{
					++AngleDepth;
				}
				else if (Character == '>' && AngleDepth > 0 && (Index == 0 || TypeName[Index - 1] != '-'))
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
				else if (Character == '[' && AngleDepth == 0 && ParenthesisDepth == 0)
				{
					return Index;
				}
			}
			return std::string_view::npos;
		}

		bool ResolveHostRuntimeTypeNameImpl(
		    const IHostBindingCatalog* Catalog,
		    const std::string_view DeclaredTypeName,
		    std::string& OutRuntimeTypeName,
		    std::string& OutErrorMessageId,
		    std::unordered_set<std::string>* Resolving,
		    const std::size_t Depth)
		{
			if (Depth > 64)
			{
				OutErrorMessageId = "diag.hostbinding_type_alias_too_deep";
				return false;
			}
			std::string TypeName = TrimTypeName(DeclaredTypeName);
			if (TypeName.empty())
			{
				OutRuntimeTypeName.clear();
				return true;
			}

			std::string RefPrefix;
			if (TypeName.rfind("const ref ", 0) == 0)
			{
				RefPrefix = "const ref ";
				TypeName.erase(0, 10);
			}
			else if (TypeName.rfind("ref ", 0) == 0)
			{
				RefPrefix = "ref ";
				TypeName.erase(0, 4);
			}

			std::vector<std::string> FunctionParams;
			std::string FunctionReturn;
			if (Runtime::ParseFunctionType(TypeName, FunctionParams, FunctionReturn))
			{
				for (std::string& Param : FunctionParams)
				{
					std::string Resolved;
					if (!ResolveHostRuntimeTypeNameImpl(
					        Catalog, Param, Resolved, OutErrorMessageId, Resolving, Depth + 1))
					{
						return false;
					}
					Param = std::move(Resolved);
				}
				std::string ResolvedReturn;
				if (!ResolveHostRuntimeTypeNameImpl(
				        Catalog, FunctionReturn, ResolvedReturn, OutErrorMessageId, Resolving, Depth + 1))
				{
					return false;
				}
				OutRuntimeTypeName = RefPrefix + Runtime::MakeFunctionTypeName(FunctionParams, ResolvedReturn);
				return true;
			}

			// Only peel dimensions belonging to this type. A dimension inside a
			// generic argument or callable belongs to that nested type instead.
			const std::size_t ArrayDimension = FindHostArrayDimension(TypeName);
			const std::size_t ArrayClose = TypeName.find(']', ArrayDimension);
			std::uint32_t ArrayLength = 0;
			bool bArray = false;
			if (ArrayDimension != std::string::npos && ArrayDimension > 0 && ArrayClose != std::string::npos && ArrayClose > ArrayDimension + 1)
			{
				const auto Parsed = std::from_chars(TypeName.data() + ArrayDimension + 1,
				    TypeName.data() + ArrayClose, ArrayLength);
				bArray = Parsed.ec == std::errc{} && Parsed.ptr == TypeName.data() + ArrayClose && ArrayLength > 0;
			}
			if (bArray)
			{
				const std::string ArrayElement = TypeName.substr(0, ArrayDimension) + TypeName.substr(ArrayClose + 1);
				std::string ResolvedElement;
				if (!ResolveHostRuntimeTypeNameImpl(
				        Catalog, ArrayElement, ResolvedElement, OutErrorMessageId, Resolving, Depth + 1))
				{
					return false;
				}
				// Restore the leftmost outer dimension before any other outer
				// dimensions, including dimensions introduced by an alias.
				const std::size_t Dimension = FindHostArrayDimension(ResolvedElement);
				ResolvedElement.insert(Dimension == std::string::npos ? ResolvedElement.size() : Dimension,
				    "[" + std::to_string(ArrayLength) + "]");
				OutRuntimeTypeName = RefPrefix + ResolvedElement;
				return true;
			}

			std::string GenericBase;
			std::vector<std::string> GenericArguments;
			const bool bGeneric = SplitGenericTypeName(TypeName, GenericBase, GenericArguments);
			const std::string& AliasName = bGeneric ? GenericBase : TypeName;
			if (const FHostTypeAliasDesc* const Alias = Catalog ? Catalog->FindTypeAlias(AliasName) : nullptr)
			{
				if (Alias->TypeParams.size() != GenericArguments.size())
				{
					OutErrorMessageId = "diag.hostbinding_type_alias_arity_mismatch";
					return false;
				}
				const std::string AliasIdentity = TypeName;
				if (!Resolving->insert(AliasIdentity).second)
				{
					OutErrorMessageId = "diag.hostbinding_type_alias_cycle";
					return false;
				}
				std::unordered_map<std::string, std::string> Substitution;
				for (std::size_t Index = 0; Index < GenericArguments.size(); ++Index)
				{
					Substitution.emplace(Alias->TypeParams[Index], GenericArguments[Index]);
				}
				const std::string Target = SubstituteAliasParameters(
				    Alias->TargetTypeName, Substitution);
				const bool bResolved = ResolveHostRuntimeTypeNameImpl(
				    Catalog, Target, OutRuntimeTypeName, OutErrorMessageId, Resolving, Depth + 1);
				Resolving->erase(AliasIdentity);
				if (bResolved)
				{
					OutRuntimeTypeName = RefPrefix + OutRuntimeTypeName;
				}
				return bResolved;
			}

			if (bGeneric)
			{
				for (std::string& Argument : GenericArguments)
				{
					std::string Resolved;
					if (!ResolveHostRuntimeTypeNameImpl(
					        Catalog, Argument, Resolved, OutErrorMessageId, Resolving, Depth + 1))
					{
						return false;
					}
					Argument = std::move(Resolved);
				}
				OutRuntimeTypeName = RefPrefix + GenericBase + "<";
				for (std::size_t Index = 0; Index < GenericArguments.size(); ++Index)
				{
					if (Index > 0)
					{
						OutRuntimeTypeName.push_back(',');
					}
					OutRuntimeTypeName += GenericArguments[Index];
				}
				OutRuntimeTypeName.push_back('>');
				return true;
			}

			const std::string Canonical = CanonicalizeNativeElementTypeName(TypeName);
			if (Canonical.empty())
			{
				OutErrorMessageId = "diag.hostbinding_type_alias_too_deep";
				return false;
			}
			OutRuntimeTypeName = RefPrefix + Canonical;
			return true;
		}
	}

	bool ResolveHostRuntimeTypeName(
	    const IHostBindingCatalog& Catalog,
	    const std::string_view DeclaredTypeName,
	    std::string& OutRuntimeTypeName,
	    std::string& OutErrorMessageId)
	{
		OutRuntimeTypeName.clear();
		OutErrorMessageId.clear();
		std::unordered_set<std::string> Resolving;
		return ResolveHostRuntimeTypeNameImpl(
		    &Catalog,
		    DeclaredTypeName,
		    OutRuntimeTypeName,
		    OutErrorMessageId,
		    &Resolving,
		    0);
	}

	std::string NativeParameterSignature(const FNativeFunction& Native)
	{
		std::string Result = "(";
		for (std::size_t Index = 0; Index < Native.ParamTypes.size(); ++Index)
		{
			const std::string Declared = ResolveNativeParamTypeName(Native, Index);
			std::string Type, Ignored;
			// Reuse recursive type resolution for primitive aliases inside ref,
			// function, fixed-array and generic-container identities. Host aliases
			// have already been expanded by the registry, so no catalog is needed.
			if (!ResolveHostRuntimeTypeNameImpl(nullptr, Declared, Type, Ignored, nullptr, 0))
			{
				Type = Declared;
			}
			const auto Flags = NativeParamFlagAt(Native, Index);
			if (NativeParamFlagIsRef(Flags))
			{
				Type = ((Flags & NativeParamFlagConstRef) ? "const ref " : "ref ") + Type;
			}
			Result += std::to_string(Type.size()) + ':' + Type;
		}
		return Result + ')';
	}

	const FHostFunctionDesc* FHostBindingRegistry::FindFunction(const std::string_view Name) const
	{
		const FHostFunctionDesc* Result = nullptr;
		for (const FHostFunctionDesc& Function : Functions)
		{
			if (Function.Name == Name)
			{
				if (Result) return nullptr;
				Result = &Function;
			}
		}
		return Result;
	}

	const FHostTypeDesc* FHostBindingRegistry::FindType(const std::string_view Name) const
	{
		for (const FHostTypeDesc& Type : Types)
		{
			if (Type.Name == Name)
			{
				return &Type;
			}
		}
		return nullptr;
	}

	const FHostTypeDesc* FHostBindingRegistry::FindType(const FHostTypeId Id) const
	{
		if (!Id.IsValid())
		{
			return nullptr;
		}
		for (const FHostTypeDesc& Type : Types)
		{
			if (Type.Id == Id)
			{
				return &Type;
			}
		}
		return nullptr;
	}

	const FHostTypeAliasDesc* FHostBindingRegistry::FindTypeAlias(
	    const std::string_view Name) const
	{
		for (const FHostTypeAliasDesc& Alias : TypeAliases)
		{
			if (Alias.Name == Name)
			{
				return &Alias;
			}
		}
		return nullptr;
	}

	const FGenericHostTypeTemplateDesc* FHostBindingRegistry::FindGenericTypeTemplate(
	    const std::string_view Name) const
	{
		for (const FGenericHostTypeTemplateDesc& Desc : GenericTypeTemplates)
		{
			if (Desc.Name == Name)
			{
				return &Desc;
			}
		}
		return nullptr;
	}

	bool FHostBindingRegistry::RegisterNative(
	    std::string Name,
	    const ENativeType ReturnType,
	    std::vector<ENativeType> ParamTypes,
	    const FNativeCallback Callback,
	    void* const UserData,
	    std::string& OutErrorMessageId,
	    std::string ReturnHostTypeName,
	    std::vector<std::string> ParamHostTypeNames,
	    std::string ReturnElementTypeName,
	    std::vector<std::string> ParamElementTypeNames,
	    std::vector<std::uint8_t> ParamFlags,
	    std::string ReturnKeyTypeName,
	    std::string ReturnValueTypeName,
	    std::vector<std::string> ParamKeyTypeNames,
	    std::vector<std::string> ParamValueTypeNames)
	{
		InvalidateRuntimeBindings();
		const auto RuntimeTypeForValidation =
		    [this](const std::string& Declared) -> std::string
		{
			std::string RuntimeName;
			std::string Error;
			return ResolveHostRuntimeTypeName(*this, Declared, RuntimeName, Error)
			    ? RuntimeName
			    : Declared;
		};
		const auto ValidateMapOrSetKeyTypeName =
		    [this, &RuntimeTypeForValidation](
		        const std::string& KeyName, std::string& OutError) -> bool
		{
			if (KeyName.empty())
			{
				OutError = "diag.hostbinding_unsupported_map_or_set_key_type";
				return false;
			}
			const std::string Canonical = CanonicalizeNativeElementTypeName(
			    RuntimeTypeForValidation(KeyName));
			const FHostTypeDesc* const HostType = FindSignatureType(*this, Canonical);
			if (!Runtime::IsAllowedMapOrSetKeyType(Canonical) && (!HostType || (!HostTypeHasStrongEnumSemantics(HostType) && HostType->Kind != EHostTypeKind::Class && (HostType->Kind != EHostTypeKind::Struct || !HostType->CloneCallback || !HostType->EqualsCallback || !HostType->HashCallback))))
			{
				OutError = "diag.hostbinding_unsupported_map_or_set_key_type";
				return false;
			}
			return true;
		};

		const auto ValidateNestedValueTypeName =
		    [this, &ValidateMapOrSetKeyTypeName, &RuntimeTypeForValidation](
		        const std::string& TypeName,
		        std::string& OutError,
		        auto& ValidateNestedValueTypeNameRef) -> bool
		{
			if (TypeName.empty())
			{
				OutError = "diag.hostbinding_array_param_requires_element_type_name";
				return false;
			}
			const std::string Canonical = CanonicalizeNativeElementTypeName(
			    RuntimeTypeForValidation(TypeName));
			if (Canonical == "variant" || IsPrimitiveNativeElementTypeName(Canonical) || Runtime::IsFunctionTypeName(Canonical))
			{
				return true;
			}
			if (FindSignatureType(*this, Canonical) != nullptr)
			{
				return true;
			}
			std::string Nested;
			if (Runtime::ParseDynamicArrayElementType(Canonical, Nested) || Runtime::ParseQueueElementType(Canonical, Nested) || Runtime::ParseStackElementType(Canonical, Nested) || Runtime::ParseDequeElementType(Canonical, Nested) || Runtime::ParseChannelElementType(Canonical, Nested))
			{
				return ValidateNestedValueTypeNameRef(
				    Nested, OutError, ValidateNestedValueTypeNameRef);
			}
			std::string Key;
			std::string Value;
			if (Runtime::SplitMapType(Canonical, Key, Value) || Runtime::ParseOrderedMapType(Canonical, Key, Value))
			{
				if (!ValidateMapOrSetKeyTypeName(Key, OutError))
				{
					return false;
				}
				return ValidateNestedValueTypeNameRef(
				    Value, OutError, ValidateNestedValueTypeNameRef);
			}
			if (Runtime::SplitSetElementType(Canonical, Nested) || Runtime::ParseMultiSetElementType(Canonical, Nested))
			{
				return ValidateMapOrSetKeyTypeName(Nested, OutError);
			}
			OutError = "diag.hostbinding_unknown_array_element_type_name";
			return false;
		};

		const auto ValidateArrayOrMapValueTypeName =
		    [&ValidateNestedValueTypeName](const std::string& ElementName, std::string& OutError) -> bool
		{
			return ValidateNestedValueTypeName(
			    ElementName, OutError, ValidateNestedValueTypeName);
		};

		if (ReturnType == ENativeType::HostObject)
		{
			if (ReturnHostTypeName.empty())
			{
				// Legacy: use the function name as the opaque type label when no
				// RegisterType name is supplied.
				ReturnHostTypeName = Name;
			}
			else if (ReturnHostTypeName != "object" && FindSignatureType(*this, RuntimeTypeForValidation(ReturnHostTypeName)) == nullptr)
			{
				OutErrorMessageId = "diag.hostbinding_unknown_host_type_name";
				return false;
			}
		}
		if (ReturnType == ENativeType::Enum && ReturnHostTypeName.empty())
		{
			OutErrorMessageId = "diag.hostbinding_enum_requires_type_name";
			return false;
		}
		if (ReturnType == ENativeType::Function && !Runtime::IsFunctionTypeName(RuntimeTypeForValidation(ReturnHostTypeName)))
		{
			OutErrorMessageId = "diag.hostbinding_function_requires_signature";
			return false;
		}
		if (ReturnType == ENativeType::Array || ReturnType == ENativeType::Queue || ReturnType == ENativeType::PriorityQueue || ReturnType == ENativeType::Stack || ReturnType == ENativeType::Deque)
		{
			if (!ValidateArrayOrMapValueTypeName(ReturnElementTypeName, OutErrorMessageId))
			{
				if (OutErrorMessageId == "diag.hostbinding_array_param_requires_element_type_name")
				{
					OutErrorMessageId = "diag.hostbinding_array_return_requires_element_type_name";
				}
				return false;
			}
		}
		if (ReturnType == ENativeType::Channel && !Runtime::IsAllowedChannelElementType(CanonicalizeNativeElementTypeName(RuntimeTypeForValidation(ReturnElementTypeName))))
		{
			OutErrorMessageId =
			    "diag.hostbinding_channel_requires_scalar_element_type";
			return false;
		}
		if (ReturnType == ENativeType::Set || ReturnType == ENativeType::MultiSet || ReturnType == ENativeType::OrderedMultiSet)
		{
			if (ReturnElementTypeName.empty())
			{
				OutErrorMessageId = "diag.hostbinding_set_return_requires_element_type_name";
				return false;
			}
			if (!ValidateMapOrSetKeyTypeName(ReturnElementTypeName, OutErrorMessageId))
			{
				return false;
			}
		}
		if (NativeTypeIsBinaryMap(ReturnType))
		{
			if (ReturnKeyTypeName.empty() || ReturnValueTypeName.empty())
			{
				OutErrorMessageId = "diag.hostbinding_map_return_requires_key_value_type_names";
				return false;
			}
			if (!ValidateMapOrSetKeyTypeName(ReturnKeyTypeName, OutErrorMessageId))
			{
				return false;
			}
			if (!ValidateArrayOrMapValueTypeName(ReturnValueTypeName, OutErrorMessageId))
			{
				return false;
			}
		}
		for (std::size_t Index = 0; Index < ParamTypes.size(); ++Index)
		{
			if (ParamTypes[Index] == ENativeType::HostObject)
			{
				if (Index >= ParamHostTypeNames.size() || ParamHostTypeNames[Index].empty())
				{
					OutErrorMessageId = "diag.hostbinding_hostobject_param_requires_type_name";
					return false;
				}
				if (ParamHostTypeNames[Index] != "object" && FindSignatureType(*this, RuntimeTypeForValidation(ParamHostTypeNames[Index])) == nullptr)
				{
					OutErrorMessageId = "diag.hostbinding_unknown_host_type_name";
					return false;
				}
			}
			else if (ParamTypes[Index] == ENativeType::Enum)
			{
				if (Index >= ParamHostTypeNames.size() || ParamHostTypeNames[Index].empty())
				{
					OutErrorMessageId = "diag.hostbinding_enum_requires_type_name";
					return false;
				}
			}
			else if (ParamTypes[Index] == ENativeType::Function)
			{
				if (Index >= ParamHostTypeNames.size() || !Runtime::IsFunctionTypeName(RuntimeTypeForValidation(ParamHostTypeNames[Index])))
				{
					OutErrorMessageId = "diag.hostbinding_function_requires_signature";
					return false;
				}
			}
			else if (ParamTypes[Index] == ENativeType::Array || ParamTypes[Index] == ENativeType::Queue || ParamTypes[Index] == ENativeType::PriorityQueue || ParamTypes[Index] == ENativeType::Stack || ParamTypes[Index] == ENativeType::Deque)
			{
				const std::string ElementName =
				    Index < ParamElementTypeNames.size()
				    ? ParamElementTypeNames[Index]
				    : std::string{};
				if (!ValidateArrayOrMapValueTypeName(ElementName, OutErrorMessageId))
				{
					return false;
				}
			}
			else if (ParamTypes[Index] == ENativeType::Channel)
			{
				const std::string ElementName =
				    Index < ParamElementTypeNames.size()
				    ? ParamElementTypeNames[Index]
				    : std::string{};
				if (!Runtime::IsAllowedChannelElementType(
				        CanonicalizeNativeElementTypeName(
				            RuntimeTypeForValidation(ElementName))))
				{
					OutErrorMessageId =
					    "diag.hostbinding_channel_requires_scalar_element_type";
					return false;
				}
			}
			else if (ParamTypes[Index] == ENativeType::Set || ParamTypes[Index] == ENativeType::MultiSet || ParamTypes[Index] == ENativeType::OrderedMultiSet)
			{
				const std::string ElementName =
				    Index < ParamElementTypeNames.size()
				    ? ParamElementTypeNames[Index]
				    : std::string{};
				if (ElementName.empty())
				{
					OutErrorMessageId = "diag.hostbinding_set_param_requires_element_type_name";
					return false;
				}
				if (!ValidateMapOrSetKeyTypeName(ElementName, OutErrorMessageId))
				{
					return false;
				}
			}
			else if (NativeTypeIsBinaryMap(ParamTypes[Index]))
			{
				const std::string KeyName =
				    Index < ParamKeyTypeNames.size() ? ParamKeyTypeNames[Index] : std::string{};
				const std::string ValueName =
				    Index < ParamValueTypeNames.size()
				    ? ParamValueTypeNames[Index]
				    : std::string{};
				if (KeyName.empty() || ValueName.empty())
				{
					OutErrorMessageId = "diag.hostbinding_map_param_requires_key_value_type_names";
					return false;
				}
				if (!ValidateMapOrSetKeyTypeName(KeyName, OutErrorMessageId))
				{
					return false;
				}
				if (!ValidateArrayOrMapValueTypeName(ValueName, OutErrorMessageId))
				{
					return false;
				}
			}
		}
		if (!ValidateNativeParamFlags(ParamTypes, ParamFlags, OutErrorMessageId))
		{
			return false;
		}

		FNativeFunction Entry;
		Entry.Name = std::move(Name);
		Entry.ReturnType = ReturnType;
		Entry.ParamTypes = std::move(ParamTypes);
		Entry.Callback = Callback;
		Entry.UserData = UserData;
		Entry.ReturnHostTypeName = std::move(ReturnHostTypeName);
		Entry.HostTypeName = Entry.ReturnHostTypeName;
		Entry.ParamHostTypeNames = std::move(ParamHostTypeNames);
		Entry.ReturnElementTypeName =
		    CanonicalizeNativeElementTypeName(ReturnElementTypeName);
		Entry.ParamElementTypeNames = std::move(ParamElementTypeNames);
		Entry.ParamFlags = std::move(ParamFlags);
		Entry.ReturnKeyTypeName = CanonicalizeNativeElementTypeName(ReturnKeyTypeName);
		Entry.ReturnValueTypeName = CanonicalizeNativeElementTypeName(ReturnValueTypeName);
		Entry.ParamKeyTypeNames = std::move(ParamKeyTypeNames);
		Entry.ParamValueTypeNames = std::move(ParamValueTypeNames);
		if (Entry.ParamHostTypeNames.size() < Entry.ParamTypes.size())
		{
			Entry.ParamHostTypeNames.resize(Entry.ParamTypes.size());
		}
		if (Entry.ParamElementTypeNames.size() < Entry.ParamTypes.size())
		{
			Entry.ParamElementTypeNames.resize(Entry.ParamTypes.size());
		}
		if (Entry.ParamFlags.size() < Entry.ParamTypes.size())
		{
			Entry.ParamFlags.resize(Entry.ParamTypes.size(), 0);
		}
		if (Entry.ParamKeyTypeNames.size() < Entry.ParamTypes.size())
		{
			Entry.ParamKeyTypeNames.resize(Entry.ParamTypes.size());
		}
		if (Entry.ParamValueTypeNames.size() < Entry.ParamTypes.size())
		{
			Entry.ParamValueTypeNames.resize(Entry.ParamTypes.size());
		}
		for (std::size_t Index = 0; Index < Entry.ParamElementTypeNames.size(); ++Index)
		{
			if (!Entry.ParamElementTypeNames[Index].empty())
			{
				Entry.ParamElementTypeNames[Index] =
				    CanonicalizeNativeElementTypeName(Entry.ParamElementTypeNames[Index]);
			}
		}
		for (std::size_t Index = 0; Index < Entry.ParamKeyTypeNames.size(); ++Index)
		{
			if (!Entry.ParamKeyTypeNames[Index].empty())
			{
				Entry.ParamKeyTypeNames[Index] =
				    CanonicalizeNativeElementTypeName(Entry.ParamKeyTypeNames[Index]);
			}
			if (!Entry.ParamValueTypeNames[Index].empty())
			{
				Entry.ParamValueTypeNames[Index] =
				    CanonicalizeNativeElementTypeName(Entry.ParamValueTypeNames[Index]);
			}
		}
		return TryAddNative(std::move(Entry), OutErrorMessageId);
	}

	bool FHostBindingRegistry::RegisterNative(
	    std::string Name,
	    FNativeTypeDesc ReturnType,
	    std::vector<FNativeParamDesc> Params,
	    const FNativeCallback Callback,
	    void* const UserData,
	    std::string& OutErrorMessageId)
	{
		InvalidateRuntimeBindings();
		const auto ValidateDesc =
		    [](const FNativeTypeDesc& Desc, auto& Self) -> bool
		{
			const bool bBinary = NativeTypeIsBinaryMap(Desc.Type);
			const bool bUnary =
			    NativeTypeIsContainer(Desc.Type) && !bBinary;
			if (!Desc.TypeArguments.empty())
			{
				if ((bBinary && Desc.TypeArguments.size() != 2) || (bUnary && Desc.TypeArguments.size() != 1) || (!bBinary && !bUnary))
				{
					return false;
				}
				if (!Desc.ElementTypeName.empty() || !Desc.KeyTypeName.empty() || !Desc.ValueTypeName.empty())
				{
					return false;
				}
				for (const FNativeTypeDesc& Argument : Desc.TypeArguments)
				{
					if (!Self(Argument, Self))
					{
						return false;
					}
				}
			}
			return true;
		};
		if (!ValidateDesc(ReturnType, ValidateDesc))
		{
			OutErrorMessageId = "diag.hostbinding_invalid_native_type_desc";
			return false;
		}
		for (const FNativeParamDesc& Param : Params)
		{
			if (!ValidateDesc(Param.Type, ValidateDesc))
			{
				OutErrorMessageId = "diag.hostbinding_invalid_native_type_desc";
				return false;
			}
		}
		const std::string RegisteredName = Name;
		FNativeTypeDesc StructuredReturnType = ReturnType;
		std::vector<FNativeTypeDesc> StructuredParamTypes;
		StructuredParamTypes.reserve(Params.size());
		for (const FNativeParamDesc& Param : Params)
		{
			StructuredParamTypes.push_back(Param.Type);
		}

		std::vector<ENativeType> ParamTypes;
		std::vector<std::string> ParamHostTypeNames;
		std::vector<std::string> ParamElementTypeNames;
		std::vector<std::uint8_t> ParamFlags;
		std::vector<std::string> ParamKeyTypeNames;
		std::vector<std::string> ParamValueTypeNames;
		ParamTypes.reserve(Params.size());
		ParamHostTypeNames.reserve(Params.size());
		ParamElementTypeNames.reserve(Params.size());
		ParamFlags.reserve(Params.size());
		ParamKeyTypeNames.reserve(Params.size());
		ParamValueTypeNames.reserve(Params.size());
		for (FNativeParamDesc& Param : Params)
		{
			ParamTypes.push_back(Param.Type.Type);
			ParamHostTypeNames.push_back(std::move(Param.Type.HostTypeName));
			if (Param.Type.TypeArguments.size() == 1)
			{
				ParamElementTypeNames.push_back(
				    ResolveNativeTypeDescName(Param.Type.TypeArguments[0]));
			}
			else
			{
				ParamElementTypeNames.push_back(std::move(Param.Type.ElementTypeName));
			}
			ParamFlags.push_back(Param.Flags);
			if (Param.Type.TypeArguments.size() == 2)
			{
				ParamKeyTypeNames.push_back(
				    ResolveNativeTypeDescName(Param.Type.TypeArguments[0]));
				ParamValueTypeNames.push_back(
				    ResolveNativeTypeDescName(Param.Type.TypeArguments[1]));
			}
			else
			{
				ParamKeyTypeNames.push_back(std::move(Param.Type.KeyTypeName));
				ParamValueTypeNames.push_back(std::move(Param.Type.ValueTypeName));
			}
		}
		std::string ReturnElementTypeName = std::move(ReturnType.ElementTypeName);
		std::string ReturnKeyTypeName = std::move(ReturnType.KeyTypeName);
		std::string ReturnValueTypeName = std::move(ReturnType.ValueTypeName);
		if (ReturnType.TypeArguments.size() == 1)
		{
			ReturnElementTypeName =
			    ResolveNativeTypeDescName(ReturnType.TypeArguments[0]);
		}
		else if (ReturnType.TypeArguments.size() == 2)
		{
			ReturnKeyTypeName = ResolveNativeTypeDescName(ReturnType.TypeArguments[0]);
			ReturnValueTypeName = ResolveNativeTypeDescName(ReturnType.TypeArguments[1]);
		}
		if (!RegisterNative(
		        std::move(Name),
		        ReturnType.Type,
		        std::move(ParamTypes),
		        Callback,
		        UserData,
		        OutErrorMessageId,
		        std::move(ReturnType.HostTypeName),
		        std::move(ParamHostTypeNames),
		        std::move(ReturnElementTypeName),
		        std::move(ParamElementTypeNames),
		        std::move(ParamFlags),
		        std::move(ReturnKeyTypeName),
		        std::move(ReturnValueTypeName),
		        std::move(ParamKeyTypeNames),
		        std::move(ParamValueTypeNames)))
		{
			return false;
		}
		for (auto It = Natives.rbegin(); It != Natives.rend(); ++It)
		{
			auto& Native = *It;
			if (Native.Name == RegisteredName)
			{
				// All allocations must precede publication of the callback.
				Native.StructuredReturnType = std::move(StructuredReturnType);
				Native.StructuredParamTypes = std::move(StructuredParamTypes);
				Native.bHasStructuredTypeDescs = true;
				break;
			}
		}
		return true;
	}

	bool FHostBindingRegistry::RegisterNative(
	    FHostFunctionDesc Function,
	    const FNativeCallback Callback,
	    void* const UserData,
	    std::string& OutErrorMessageId)
	{
		InvalidateRuntimeBindings();
		OutErrorMessageId.clear();
		if (!Function.Name.empty() && Function.Name.front() == '\x1F')
		{
			OutErrorMessageId =
			    "diag.hostbinding_function_metadata_requires_a_public_function";
			return false;
		}
		if (Function.bGenericHostObjectCast && (Function.ReturnType != ENativeType::HostObject || Function.ParamTypes.size() != 1 || Function.ParamTypes.front() != ENativeType::HostObject))
		{
			OutErrorMessageId =
			    "diag.hostbinding_generic_cast_has_invalid_signature";
			return false;
		}
		if ((!Function.ParamNames.empty() && Function.ParamNames.size() != Function.ParamTypes.size()) || (!Function.ParamDocumentation.empty() && Function.ParamDocumentation.size() != Function.ParamTypes.size()))
		{
			OutErrorMessageId = "diag.hostbinding_param_metadata_count_mismatch";
			return false;
		}
		if (!ValidateDeclarationAnnotations(Function.Annotations, OutErrorMessageId) || !ValidateParameterAnnotations(Function.ParamAnnotations, Function.ParamTypes.size(), OutErrorMessageId))
		{
			return false;
		}

		const std::size_t FunctionCountBefore = Functions.size();
		const bool bRegistered = Function.bGenericHostObjectCast
		    ? RegisterGenericHostObjectCast(
		          Function.Name,
		          Function.TypeParams,
		          Function.ScriptReturnTypeName,
		          Callback,
		          UserData,
		          OutErrorMessageId)
		    : RegisterNative(
		          Function.Name,
		          Function.ReturnType,
		          Function.ParamTypes,
		          Callback,
		          UserData,
		          OutErrorMessageId,
		          Function.ReturnHostTypeName,
		          Function.ParamHostTypeNames,
		          Function.ReturnElementTypeName,
		          Function.ParamElementTypeNames,
		          Function.ParamFlags,
		          Function.ReturnKeyTypeName,
		          Function.ReturnValueTypeName,
		          Function.ParamKeyTypeNames,
		          Function.ParamValueTypeNames);
		if (!bRegistered)
		{
			return false;
		}
		if (Functions.size() != FunctionCountBefore + 1)
		{
			OutErrorMessageId = "diag.hostbinding_function_metadata_registration_failed";
			return false;
		}
		FHostFunctionDesc& Registered = Functions.back();
		Registered.Documentation = std::move(Function.Documentation);
		Registered.ParamNames = std::move(Function.ParamNames);
		Registered.ParamDocumentation = std::move(Function.ParamDocumentation);
		Registered.ReturnDocumentation = std::move(Function.ReturnDocumentation);
		Registered.Annotations = std::move(Function.Annotations);
		Registered.ParamAnnotations = std::move(Function.ParamAnnotations);
		return true;
	}

	bool FHostBindingRegistry::RegisterGenericHostObjectCast(
	    std::string Name,
	    std::vector<std::string> TypeParams,
	    std::string ScriptReturnTypeName,
	    const FNativeCallback Callback,
	    void* const UserData,
	    std::string& OutErrorMessageId)
	{
		InvalidateRuntimeBindings();
		for (const auto& Native : Natives)
		{
			if (Native.Name == Name)
			{
				OutErrorMessageId = "diag.registernative_duplicate_name";
				return false;
			}
		}
		if (TypeParams.empty() || ScriptReturnTypeName.empty())
		{
			OutErrorMessageId = "diag.hostbinding_generic_cast_requires_type_parameters";
			return false;
		}
		std::unordered_set<std::string> SeenTypeParams;
		for (const std::string& TypeParam : TypeParams)
		{
			if (TypeParam.empty() || TypeParam.front() == '@' || TypeParam == "variant" || !IsValidAnnotationName(TypeParam) || !SeenTypeParams.insert(TypeParam).second)
			{
				OutErrorMessageId =
				    "diag.hostbinding_generic_cast_has_invalid_type_parameters";
				return false;
			}
		}
		if (SeenTypeParams.find(ScriptReturnTypeName) == SeenTypeParams.end())
		{
			OutErrorMessageId =
			    "diag.hostbinding_generic_cast_return_must_be_a_type_parameter";
			return false;
		}

		const std::string RegisteredName = Name;
		if (!RegisterNative(
		        std::move(Name),
		        ENativeType::HostObject,
		        {ENativeType::HostObject},
		        Callback,
		        UserData,
		        OutErrorMessageId,
		        "object",
		        {"object"}))
		{
			return false;
		}

		for (auto It = Natives.rbegin(); It != Natives.rend(); ++It)
		{
			auto& Native = *It;
			if (Native.Name == RegisteredName)
			{
				Native.TypeParams = TypeParams;
				Native.ScriptReturnTypeName = ScriptReturnTypeName;
				Native.bGenericHostObjectCast = true;
				Native.bHostStaticMethod = true;
				break;
			}
		}
		for (auto It = Functions.rbegin(); It != Functions.rend(); ++It)
		{
			auto& Function = *It;
			if (Function.Name == RegisteredName)
			{
				Function.TypeParams = std::move(TypeParams);
				Function.ScriptReturnTypeName = std::move(ScriptReturnTypeName);
				Function.bGenericHostObjectCast = true;
				break;
			}
		}
		OutErrorMessageId.clear();
		return true;
	}

	bool FHostBindingRegistry::RegisterType(
	    std::string Name,
	    const EHostTypeKind Kind,
	    FHostTypeId& OutTypeId,
	    std::string& OutErrorMessageId)
	{
		InvalidateRuntimeBindings();
		return RegisterType(
		    std::move(Name), Kind, OutTypeId, OutErrorMessageId, {});
	}

	bool FHostBindingRegistry::RegisterType(
	    std::string Name,
	    const EHostTypeKind Kind,
	    FHostTypeId& OutTypeId,
	    std::string& OutErrorMessageId,
	    std::vector<FHostAnnotationInstance> TypeAnnotations)
	{
		InvalidateRuntimeBindings();
		OutErrorMessageId.clear();
		OutTypeId = {};
		if (Name == "variant" || Name == "std.variant" || Name == "std::variant")
		{
			OutErrorMessageId = "diag.variant_reserved_name";
			return false;
		}
		if (Name.empty())
		{
			OutErrorMessageId = "diag.hostbinding_type_requires_a_non_empty_name";
			return false;
		}
		if (FindType(Name) != nullptr || FindTypeAlias(Name) != nullptr || FindGenericTypeTemplate(Name) != nullptr)
		{
			OutErrorMessageId = "diag.hostbinding_duplicate_type_name";
			return false;
		}
		if (NextTypeId == 0)
		{
			OutErrorMessageId = "diag.hostbinding_type_table_is_full";
			return false;
		}
		if (!ValidateDeclarationAnnotations(TypeAnnotations, OutErrorMessageId))
		{
			return false;
		}

		FHostTypeDesc Type;
		Type.ModuleOwner = RegistrationOwner;
		Type.ProviderIdentity = RegistrationProviderIdentity;
		Type.Origin = RegistrationOrigin;
		Type.Id.Value = NextTypeId++;
		Type.Name = std::move(Name);
		Type.Kind = Kind;
		Type.Annotations = std::move(TypeAnnotations);
		OutTypeId = Type.Id;
		Types.push_back(std::move(Type));
		return true;
	}

	bool FHostBindingRegistry::RegisterGenericHostTypeTemplate(
	    std::string Name,
	    const std::size_t Arity,
	    const FGenericHostTypeInstantiateCallback Instantiate,
	    void* const UserData,
	    FGenericHostTypeTemplateId& OutTemplateId,
	    std::string& OutErrorMessageId,
	    std::string Documentation)
	{
		InvalidateRuntimeBindings();
		OutErrorMessageId.clear();
		OutTemplateId = {};
		if (Name == "variant" || Name == "std.variant" || Name == "std::variant")
		{
			OutErrorMessageId = "diag.variant_reserved_name";
			return false;
		}
		if (Name.empty() || Arity == 0 || Arity > 255)
		{
			OutErrorMessageId = "diag.hostbinding_generic_type_template_requires_name_and_arity";
			return false;
		}
		if (!Instantiate)
		{
			OutErrorMessageId = "diag.hostbinding_generic_type_template_requires_callback";
			return false;
		}
		if (FindType(Name) || FindTypeAlias(Name) || FindGenericTypeTemplate(Name))
		{
			OutErrorMessageId = "diag.hostbinding_duplicate_type_name";
			return false;
		}
		if (NextGenericTemplateId == 0)
		{
			OutErrorMessageId = "diag.hostbinding_generic_type_template_table_is_full";
			return false;
		}
		FGenericHostTypeTemplateDesc Desc;
		Desc.ModuleOwner = RegistrationOwner;
		Desc.ProviderIdentity = RegistrationProviderIdentity;
		Desc.Origin = RegistrationOrigin;
		Desc.Id.Value = NextGenericTemplateId++;
		Desc.Name = std::move(Name);
		Desc.Arity = Arity;
		Desc.Documentation = std::move(Documentation);
		Desc.Instantiate = Instantiate;
		Desc.UserData = UserData;
		OutTemplateId = Desc.Id;
		GenericTypeTemplates.push_back(std::move(Desc));
		return true;
	}

	bool FHostBindingRegistry::RegisterGenericHostTypeTemplateMetadata(
	    std::string Name,
	    const std::size_t Arity,
	    FGenericHostTypeTemplateId& OutTemplateId,
	    std::string& OutErrorMessageId,
	    std::string Documentation)
	{
		InvalidateRuntimeBindings();
		OutErrorMessageId.clear();
		OutTemplateId = {};
		if (Name.empty() || Arity == 0 || Arity > 255)
		{
			OutErrorMessageId = "diag.hostbinding_generic_type_template_requires_name_and_arity";
			return false;
		}
		if (FindType(Name) || FindTypeAlias(Name) || FindGenericTypeTemplate(Name))
		{
			OutErrorMessageId = "diag.hostbinding_duplicate_type_name";
			return false;
		}
		if (NextGenericTemplateId == 0)
		{
			OutErrorMessageId = "diag.hostbinding_generic_type_template_table_is_full";
			return false;
		}
		FGenericHostTypeTemplateDesc Desc;
		Desc.ModuleOwner = RegistrationOwner;
		Desc.ProviderIdentity = RegistrationProviderIdentity;
		Desc.Origin = RegistrationOrigin;
		Desc.Id.Value = NextGenericTemplateId++;
		Desc.Name = std::move(Name);
		Desc.Arity = Arity;
		Desc.Documentation = std::move(Documentation);
		OutTemplateId = Desc.Id;
		GenericTypeTemplates.push_back(std::move(Desc));
		return true;
	}

	bool FHostBindingRegistry::InstantiateGenericHostType(
	    const FGenericHostTypeTemplateId TemplateId,
	    std::vector<std::string> TypeArguments,
	    FHostTypeId& OutTypeId,
	    std::string& OutErrorMessageId)
	{
		InvalidateRuntimeBindings();
		OutTypeId = {};
		OutErrorMessageId.clear();
		const FGenericHostTypeTemplateDesc* Template = nullptr;
		for (const FGenericHostTypeTemplateDesc& Candidate : GenericTypeTemplates)
		{
			if (Candidate.Id == TemplateId)
			{
				Template = &Candidate;
				break;
			}
		}
		if (!Template)
		{
			OutErrorMessageId = "diag.hostbinding_unknown_generic_type_template_id";
			return false;
		}
		if (!Template->Instantiate)
		{
			OutErrorMessageId = "diag.hostbinding_generic_type_template_is_catalog_only";
			return false;
		}
		if (TypeArguments.size() != Template->Arity)
		{
			OutErrorMessageId = "diag.hostbinding_generic_type_argument_count_mismatch";
			return false;
		}
		for (std::string& Argument : TypeArguments)
		{
			Argument = CanonicalizeNativeElementTypeName(Argument);
			if (Argument.empty())
			{
				OutErrorMessageId = "diag.hostbinding_generic_type_argument_must_not_be_empty";
				return false;
			}
			// This public factory has a type-only schema. Never pass scalar value
			// arguments to a provider that expects canonical type names.
			const auto First = static_cast<unsigned char>(Argument.front());
			if (std::isdigit(First) || Argument.front() == '-' || Argument.front() == '+' || Argument.front() == '@' || Argument.front() == '(' || Argument.front() == '\'' || Argument.front() == '"' || Argument == "true" || Argument == "false")
			{
				OutErrorMessageId = "diag.hostbinding_generic_type_instantiation_failed";
				return false;
			}
		}
		std::string ConcreteName = Template->Name;
		ConcreteName.push_back('<');
		for (std::size_t Index = 0; Index < TypeArguments.size(); ++Index)
		{
			if (Index > 0)
			{
				ConcreteName.push_back(',');
			}
			ConcreteName += TypeArguments[Index];
		}
		ConcreteName.push_back('>');
		if (const FHostTypeDesc* Existing = FindType(ConcreteName))
		{
			if (Existing->GenericTemplateId == TemplateId && Existing->GenericTypeArguments == TypeArguments)
			{
				OutTypeId = Existing->Id;
				return true;
			}
			OutErrorMessageId = "diag.hostbinding_duplicate_type_name";
			return false;
		}

		const auto TemplateOwner = Template->ModuleOwner;
		const auto TemplateIdentity = Template->ProviderIdentity;
		const auto TemplateOrigin = Template->Origin;
		FHostTypeDesc Prototype;
		std::string CallbackError;
		if (!Template->Instantiate(
		        TypeArguments, Prototype, CallbackError, Template->UserData))
		{
			OutErrorMessageId = CallbackError.empty()
			    ? "diag.hostbinding_generic_type_instantiation_failed"
			    : std::move(CallbackError);
			return false;
		}
		if (Prototype.Kind == EHostTypeKind::Enum)
		{
			OutErrorMessageId = "diag.hostbinding_generic_enum_templates_are_not_supported";
			return false;
		}

		// Registering members synthesizes native entries. Roll back the complete
		// registry on any validation failure so an instantiation is all-or-nothing.
		FHostBindingRegistry Before = *this;
		struct FRestoreOwner
		{
			~FRestoreOwner()
			{
				Registry.SetRegistrationOwner(std::move(Owner), std::move(Origin), std::move(Identity));
			}

			FHostBindingRegistry& Registry;

			std::shared_ptr<void> Owner;

			FBindingOrigin Origin;

			std::shared_ptr<const void> Identity;
		} Restore{*this, RegistrationOwner, RegistrationOrigin, RegistrationProviderIdentity};
		SetRegistrationOwner(JoinCallbackOwners(TemplateOwner, Prototype.ModuleOwner), TemplateOrigin, TemplateIdentity);
		FHostTypeId ConcreteId;
		if (!RegisterType(
		        ConcreteName,
		        Prototype.Kind,
		        ConcreteId,
		        OutErrorMessageId,
		        std::move(Prototype.Annotations)))
		{
			return false;
		}
		FHostTypeDesc* Concrete = nullptr;
		for (FHostTypeDesc& Candidate : Types)
		{
			if (Candidate.Id == ConcreteId)
			{
				Concrete = &Candidate;
				break;
			}
		}
		if (!Concrete)
		{
			Swap(Before);
			OutErrorMessageId = "diag.hostbinding_generic_type_instantiation_failed";
			return false;
		}
		Concrete->Documentation = std::move(Prototype.Documentation);
		Concrete->GenericTemplateId = TemplateId;
		Concrete->GenericTypeArguments = TypeArguments;

		const auto RollBack = [&]()
		{
			Swap(Before);
			OutTypeId = {};
		};
		if (Prototype.bInheritable && !SetTypeInheritable(ConcreteId, true, OutErrorMessageId))
		{
			RollBack();
			return false;
		}
		if (Prototype.CloneCallback && !RegisterTypeClone(ConcreteId, Prototype.CloneCallback, Prototype.CloneUserData, OutErrorMessageId))
		{
			RollBack();
			return false;
		}
		if (Prototype.EqualsCallback && !RegisterTypeEquals(ConcreteId, Prototype.EqualsCallback, Prototype.EqualsUserData, OutErrorMessageId))
		{
			RollBack();
			return false;
		}
		if (Prototype.HashCallback && !RegisterTypeHash(ConcreteId, Prototype.HashCallback, Prototype.HashUserData, OutErrorMessageId))
		{
			RollBack();
			return false;
		}
		if (Prototype.SubclassFactory && !RegisterTypeSubclassFactory(ConcreteId, Prototype.SubclassFactory, Prototype.SubclassFactoryUserData, OutErrorMessageId))
		{
			RollBack();
			return false;
		}
		for (FHostMemberDesc& Member : Prototype.Members)
		{
			if (!RegisterMember(ConcreteId, std::move(Member), OutErrorMessageId))
			{
				RollBack();
				return false;
			}
		}
		OutTypeId = ConcreteId;
		for (const auto& Lifetime : Prototype.CallbackLifetimes)
		{
			RetainNativeCallback(Lifetime.first, Lifetime.second);
		}
		return true;
	}

	bool FHostBindingRegistry::SetTypeGenericInstanceInfo(
	    const FHostTypeId TypeId,
	    const FGenericHostTypeTemplateId TemplateId,
	    std::vector<std::string> TypeArguments,
	    std::string& OutErrorMessageId)
	{
		InvalidateRuntimeBindings();
		OutErrorMessageId.clear();
		const FGenericHostTypeTemplateDesc* Template = nullptr;
		for (const FGenericHostTypeTemplateDesc& Candidate : GenericTypeTemplates)
		{
			if (Candidate.Id == TemplateId)
			{
				Template = &Candidate;
				break;
			}
		}
		if (!Template || TypeArguments.size() != Template->Arity)
		{
			OutErrorMessageId = Template
			    ? "diag.hostbinding_generic_type_argument_count_mismatch"
			    : "diag.hostbinding_unknown_generic_type_template_id";
			return false;
		}
		for (std::string& Argument : TypeArguments)
		{
			Argument = CanonicalizeNativeElementTypeName(Argument);
		}
		std::string ExpectedName = Template->Name + '<';
		for (std::size_t Index = 0; Index < TypeArguments.size(); ++Index)
		{
			if (TypeArguments[Index].empty())
			{
				OutErrorMessageId = "diag.hostbinding_generic_type_argument_must_not_be_empty";
				return false;
			}
			if (Index > 0)
			{
				ExpectedName.push_back(',');
			}
			ExpectedName += TypeArguments[Index];
		}
		ExpectedName.push_back('>');
		for (FHostTypeDesc& Type : Types)
		{
			if (Type.Id != TypeId)
			{
				continue;
			}
			if (Type.Name != ExpectedName)
			{
				OutErrorMessageId = "diag.hostbinding_generic_concrete_name_mismatch";
				return false;
			}
			Type.GenericTemplateId = TemplateId;
			Type.GenericTypeArguments = std::move(TypeArguments);
			return true;
		}
		OutErrorMessageId = "diag.hostbinding_unknown_type_id";
		return false;
	}

	bool FHostBindingRegistry::RegisterTypeAlias(
	    std::string Name,
	    std::vector<std::string> TypeParams,
	    std::string TargetTypeName,
	    std::string& OutErrorMessageId,
	    std::string Documentation)
	{
		InvalidateRuntimeBindings();
		OutErrorMessageId.clear();
		if (Name == "variant" || Name == "std.variant" || Name == "std::variant")
		{
			OutErrorMessageId = "diag.variant_reserved_name";
			return false;
		}
		if (Name.empty() || TargetTypeName.empty())
		{
			OutErrorMessageId = "diag.hostbinding_type_alias_requires_name_and_target";
			return false;
		}
		if (FindType(Name) || FindTypeAlias(Name) || FindGenericTypeTemplate(Name))
		{
			OutErrorMessageId = "diag.hostbinding_duplicate_type_name";
			return false;
		}
		std::unordered_set<std::string> Seen;
		for (const std::string& TypeParam : TypeParams)
		{
			if (TypeParam.empty() || TypeParam.front() == '@' || TypeParam == "variant" || !IsValidAnnotationName(TypeParam) || !Seen.insert(TypeParam).second)
			{
				OutErrorMessageId = "diag.hostbinding_type_alias_has_invalid_type_parameters";
				return false;
			}
		}
		FHostTypeAliasDesc Alias;
		Alias.Origin = RegistrationOrigin;
		Alias.Name = std::move(Name);
		Alias.TypeParams = std::move(TypeParams);
		Alias.TargetTypeName = std::move(TargetTypeName);
		Alias.Documentation = std::move(Documentation);
		TypeAliases.push_back(std::move(Alias));
		// Adding an alias can make an earlier forward reference resolvable. Recheck
		// the complete graph so newly exposed cycles and arity mismatches are
		// rejected transactionally instead of surfacing only when a script uses it.
		for (const FHostTypeAliasDesc& RegisteredAlias : TypeAliases)
		{
			std::string Probe = RegisteredAlias.Name;
			if (!RegisteredAlias.TypeParams.empty())
			{
				Probe.push_back('<');
				for (std::size_t Index = 0;
				    Index < RegisteredAlias.TypeParams.size();
				    ++Index)
				{
					if (Index > 0)
					{
						Probe.push_back(',');
					}
					Probe += RegisteredAlias.TypeParams[Index];
				}
				Probe.push_back('>');
			}
			std::string RuntimeProbe;
			if (!ResolveHostRuntimeTypeName(
			        *this, Probe, RuntimeProbe, OutErrorMessageId))
			{
				TypeAliases.pop_back();
				return false;
			}
		}
		return true;
	}

	bool FHostBindingRegistry::RegisterMember(
	    const FHostTypeId TypeId,
	    FHostMemberDesc Member,
	    std::string& OutErrorMessageId)
	{
		InvalidateRuntimeBindings();
		OutErrorMessageId.clear();
		const auto RuntimeTypeForValidation =
		    [this](const std::string& Declared) -> std::string
		{
			std::string RuntimeName;
			std::string Error;
			return ResolveHostRuntimeTypeName(*this, Declared, RuntimeName, Error)
			    ? RuntimeName
			    : Declared;
		};
		FHostTypeDesc* Type = nullptr;
		for (FHostTypeDesc& Candidate : Types)
		{
			if (Candidate.Id == TypeId)
			{
				Type = &Candidate;
				break;
			}
		}
		if (!Type)
		{
			OutErrorMessageId = "diag.hostbinding_unknown_type_id";
			return false;
		}
		Member.CallbackOwner = JoinCallbackOwners(RegistrationOwner ? RegistrationOwner : Type->ModuleOwner, Member.CallbackOwner);
		if (!ValidateDeclarationAnnotations(Member.Annotations, OutErrorMessageId) || !ValidateParameterAnnotations(Member.ParamAnnotations, Member.ParamTypes.size(), OutErrorMessageId))
		{
			return false;
		}
		if (Member.bOperator)
		{
			if (Member.Kind != EHostMemberKind::Method || Member.Name.rfind("operator", 0) != 0)
			{
				OutErrorMessageId = "diag.hostbinding_invalid_operator";
				return false;
			}
		}
		if (!Member.bOperator && Member.Kind != EHostMemberKind::Constructor)
		{
			Member.NativeName.clear();
		}
		const auto IsSupportedMapOrSetKeyType =
		    [this, &RuntimeTypeForValidation](const std::string& RawTypeName) -> bool
		{
			const std::string Canonical =
			    CanonicalizeNativeElementTypeName(
			        RuntimeTypeForValidation(RawTypeName));
			if (Runtime::IsAllowedMapOrSetKeyType(Canonical))
			{
				return true;
			}
			const FHostTypeDesc* const HostType = FindSignatureType(*this, Canonical);
			return HostType && (HostTypeHasStrongEnumSemantics(HostType) || HostType->Kind == EHostTypeKind::Class || (HostType->Kind == EHostTypeKind::Struct && HostType->CloneCallback && HostType->EqualsCallback && HostType->HashCallback));
		};

		if (Member.Kind != EHostMemberKind::EnumMember && Member.Kind != EHostMemberKind::Constructor && Member.Name.empty())
		{
			OutErrorMessageId = "diag.hostbinding_member_requires_a_non_empty_name";
			return false;
		}
		if (Member.Kind == EHostMemberKind::EnumMember)
		{
			if (Type->Kind != EHostTypeKind::Enum)
			{
				OutErrorMessageId = "diag.hostbinding_enum_member_requires_enum_type";
				return false;
			}
			if (Member.Name.empty())
			{
				OutErrorMessageId = "diag.hostbinding_member_requires_a_non_empty_name";
				return false;
			}
			if (Type->bScopedEnum)
			{
				if (!Member.ReturnHostTypeName.empty() && Member.ReturnHostTypeName != Type->Name)
				{
					OutErrorMessageId = "diag.hostbinding_enum_member_type_mismatch";
					return false;
				}
				Member.ReturnHostTypeName = Type->Name;
			}
		}
		if (Member.Kind == EHostMemberKind::Constructor && Member.Name.empty())
		{
			Member.Name = Type->Name;
		}
		if (Member.Kind == EHostMemberKind::StaticConstant && (Member.ReturnType == ENativeType::Void || !Member.ParamTypes.empty()))
		{
			OutErrorMessageId = "diag.hostbinding_static_constant_requires_value_signature";
			return false;
		}
		if (Member.Kind != EHostMemberKind::EnumMember)
		{
			if (!Member.ParamNames.empty() && Member.ParamNames.size() != Member.ParamTypes.size())
			{
				OutErrorMessageId = "diag.hostbinding_param_name_count_mismatch";
				return false;
			}
			if (!Member.ParamDocumentation.empty() && Member.ParamDocumentation.size() != Member.ParamTypes.size())
			{
				OutErrorMessageId =
				    "diag.hostbinding_param_documentation_count_mismatch";
				return false;
			}
			if (!IsValidReturnType(Member.ReturnType))
			{
				OutErrorMessageId = "diag.hostbinding_member_has_invalid_return_type";
				return false;
			}
			for (std::size_t Index = 0; Index < Member.ParamTypes.size(); ++Index)
			{
				const ENativeType ParamType = Member.ParamTypes[Index];
				if (!IsValidParamType(ParamType))
				{
					OutErrorMessageId = "diag.hostbinding_member_has_invalid_parameter_type";
					return false;
				}
				if (ParamType == ENativeType::HostObject)
				{
					const std::string HostName =
					    Index < Member.ParamHostTypeNames.size()
					    ? Member.ParamHostTypeNames[Index]
					    : std::string{};
					if (HostName.empty() || FindSignatureType(*this, RuntimeTypeForValidation(HostName)) == nullptr)
					{
						OutErrorMessageId = HostName.empty()
						    ? "diag.hostbinding_hostobject_param_requires_type_name"
						    : "diag.hostbinding_unknown_host_type_name";
						return false;
					}
				}
				else if (ParamType == ENativeType::Enum)
				{
					const std::string EnumName =
					    Index < Member.ParamHostTypeNames.size()
					    ? Member.ParamHostTypeNames[Index]
					    : std::string{};
					const FHostTypeDesc* const EnumType = FindSignatureType(*this,
					    RuntimeTypeForValidation(EnumName));
					if (EnumName.empty() || !EnumType || EnumType->Kind != EHostTypeKind::Enum)
					{
						OutErrorMessageId = "diag.hostbinding_enum_requires_type_name";
						return false;
					}
				}
				else if (ParamType == ENativeType::Function)
				{
					const std::string Signature =
					    Index < Member.ParamHostTypeNames.size()
					    ? Member.ParamHostTypeNames[Index]
					    : std::string{};
					if (!Runtime::IsFunctionTypeName(
					        RuntimeTypeForValidation(Signature)))
					{
						OutErrorMessageId = "diag.hostbinding_function_requires_signature";
						return false;
					}
				}
				else if (ParamType == ENativeType::Array || ParamType == ENativeType::Queue || ParamType == ENativeType::PriorityQueue || ParamType == ENativeType::Stack || ParamType == ENativeType::Deque || ParamType == ENativeType::Channel)
				{
					const std::string ElementName =
					    Index < Member.ParamElementTypeNames.size()
					    ? Member.ParamElementTypeNames[Index]
					    : std::string{};
					if (ElementName.empty())
					{
						OutErrorMessageId = "diag.hostbinding_array_param_requires_element_type_name";
						return false;
					}
					if (ParamType == ENativeType::Channel && !Runtime::IsAllowedChannelElementType(CanonicalizeNativeElementTypeName(RuntimeTypeForValidation(ElementName))))
					{
						OutErrorMessageId =
						    "diag.hostbinding_channel_requires_scalar_element_type";
						return false;
					}
					std::string NestedError;
					const auto ValidateMapOrSetKeyTypeName =
					    [&IsSupportedMapOrSetKeyType](
					        const std::string& KeyName,
					        std::string& OutError) -> bool
					{
						if (!IsSupportedMapOrSetKeyType(KeyName))
						{
							OutError = "diag.hostbinding_unsupported_map_or_set_key_type";
							return false;
						}
						return true;
					};
					const auto ValidateNested =
					    [this, &ValidateMapOrSetKeyTypeName, &RuntimeTypeForValidation](
					        const std::string& TypeName,
					        std::string& OutError,
					        auto& Self) -> bool
					{
						const std::string C = CanonicalizeNativeElementTypeName(
						    RuntimeTypeForValidation(TypeName));
						if (IsPrimitiveNativeElementTypeName(C) || Runtime::IsFunctionTypeName(C) || FindSignatureType(*this, C) != nullptr)
						{
							return true;
						}
						std::string Inner;
						if (Runtime::ParseDynamicArrayElementType(C, Inner) || Runtime::ParseQueueElementType(C, Inner) || Runtime::ParseStackElementType(C, Inner) || Runtime::ParseDequeElementType(C, Inner) || Runtime::ParseChannelElementType(C, Inner))
						{
							return Self(Inner, OutError, Self);
						}
						std::string Key;
						std::string Value;
						if (Runtime::SplitMapType(C, Key, Value) || Runtime::SplitOrderedMapType(C, Key, Value))
						{
							return ValidateMapOrSetKeyTypeName(Key, OutError) && Self(Value, OutError, Self);
						}
						if (Runtime::SplitSetElementType(C, Inner) || Runtime::ParseMultiSetElementType(C, Inner))
						{
							return ValidateMapOrSetKeyTypeName(Inner, OutError);
						}
						OutError = "diag.hostbinding_unknown_array_element_type_name";
						return false;
					};
					if (!ValidateNested(ElementName, NestedError, ValidateNested))
					{
						OutErrorMessageId = NestedError;
						return false;
					}
				}
				else if (ParamType == ENativeType::Set || ParamType == ENativeType::MultiSet || ParamType == ENativeType::OrderedMultiSet)
				{
					const std::string ElementName =
					    Index < Member.ParamElementTypeNames.size()
					    ? Member.ParamElementTypeNames[Index]
					    : std::string{};
					if (ElementName.empty())
					{
						OutErrorMessageId = "diag.hostbinding_set_param_requires_element_type_name";
						return false;
					}
					if (!IsSupportedMapOrSetKeyType(ElementName))
					{
						OutErrorMessageId = "diag.hostbinding_unsupported_map_or_set_key_type";
						return false;
					}
				}
				else if (NativeTypeIsBinaryMap(ParamType))
				{
					const std::string KeyName =
					    Index < Member.ParamKeyTypeNames.size()
					    ? Member.ParamKeyTypeNames[Index]
					    : std::string{};
					const std::string ValueName =
					    Index < Member.ParamValueTypeNames.size()
					    ? Member.ParamValueTypeNames[Index]
					    : std::string{};
					if (KeyName.empty() || ValueName.empty())
					{
						OutErrorMessageId = "diag.hostbinding_map_param_requires_key_value_type_names";
						return false;
					}
					if (!IsSupportedMapOrSetKeyType(KeyName))
					{
						OutErrorMessageId = "diag.hostbinding_unsupported_map_or_set_key_type";
						return false;
					}
					const std::string ValueCanonical =
					    CanonicalizeNativeElementTypeName(
					        RuntimeTypeForValidation(ValueName));
					std::string NestedError;
					const auto ValidateMapOrSetKeyTypeName =
					    [&IsSupportedMapOrSetKeyType](
					        const std::string& Key,
					        std::string& OutError) -> bool
					{
						if (!IsSupportedMapOrSetKeyType(Key))
						{
							OutError = "diag.hostbinding_unsupported_map_or_set_key_type";
							return false;
						}
						return true;
					};
					const auto ValidateNested =
					    [this, &ValidateMapOrSetKeyTypeName, &RuntimeTypeForValidation](
					        const std::string& TypeName,
					        std::string& OutError,
					        auto& Self) -> bool
					{
						const std::string C = CanonicalizeNativeElementTypeName(
						    RuntimeTypeForValidation(TypeName));
						if (IsPrimitiveNativeElementTypeName(C) || Runtime::IsFunctionTypeName(C) || FindSignatureType(*this, C) != nullptr)
						{
							return true;
						}
						std::string Inner;
						if (Runtime::ParseDynamicArrayElementType(C, Inner) || Runtime::ParseQueueElementType(C, Inner) || Runtime::ParseStackElementType(C, Inner) || Runtime::ParseDequeElementType(C, Inner) || Runtime::ParseChannelElementType(C, Inner))
						{
							return Self(Inner, OutError, Self);
						}
						std::string Key;
						std::string Value;
						if (Runtime::SplitMapType(C, Key, Value) || Runtime::SplitOrderedMapType(C, Key, Value))
						{
							return ValidateMapOrSetKeyTypeName(Key, OutError) && Self(Value, OutError, Self);
						}
						if (Runtime::SplitSetElementType(C, Inner) || Runtime::ParseMultiSetElementType(C, Inner))
						{
							return ValidateMapOrSetKeyTypeName(Inner, OutError);
						}
						OutError = "diag.hostbinding_unknown_array_element_type_name";
						return false;
					};
					if (!ValidateNested(ValueCanonical, NestedError, ValidateNested))
					{
						OutErrorMessageId = NestedError;
						return false;
					}
				}
			}
			if (Member.ReturnType == ENativeType::HostObject && !Member.ReturnHostTypeName.empty() && FindSignatureType(*this, RuntimeTypeForValidation(Member.ReturnHostTypeName)) == nullptr)
			{
				OutErrorMessageId = "diag.hostbinding_unknown_host_type_name";
				return false;
			}
			if (Member.ReturnType == ENativeType::Enum)
			{
				const FHostTypeDesc* const EnumType = FindSignatureType(*this,
				    RuntimeTypeForValidation(Member.ReturnHostTypeName));
				if (!EnumType || EnumType->Kind != EHostTypeKind::Enum)
				{
					OutErrorMessageId = "diag.hostbinding_enum_requires_type_name";
					return false;
				}
			}
			if (Member.ReturnType == ENativeType::Function && !Runtime::IsFunctionTypeName(RuntimeTypeForValidation(Member.ReturnHostTypeName)))
			{
				OutErrorMessageId = "diag.hostbinding_function_requires_signature";
				return false;
			}
			if (Member.ReturnType == ENativeType::Array || Member.ReturnType == ENativeType::Queue || Member.ReturnType == ENativeType::PriorityQueue || Member.ReturnType == ENativeType::Stack || Member.ReturnType == ENativeType::Deque || Member.ReturnType == ENativeType::Channel)
			{
				if (Member.ReturnElementTypeName.empty())
				{
					OutErrorMessageId = "diag.hostbinding_array_return_requires_element_type_name";
					return false;
				}
				if (Member.ReturnType == ENativeType::Channel && !Runtime::IsAllowedChannelElementType(CanonicalizeNativeElementTypeName(RuntimeTypeForValidation(Member.ReturnElementTypeName))))
				{
					OutErrorMessageId =
					    "diag.hostbinding_channel_requires_scalar_element_type";
					return false;
				}
				std::string NestedError;
				const auto ValidateMapOrSetKeyTypeName =
				    [&IsSupportedMapOrSetKeyType](
				        const std::string& KeyName,
				        std::string& OutError) -> bool
				{
					if (!IsSupportedMapOrSetKeyType(KeyName))
					{
						OutError = "diag.hostbinding_unsupported_map_or_set_key_type";
						return false;
					}
					return true;
				};
				const auto ValidateNested =
				    [this, &ValidateMapOrSetKeyTypeName, &RuntimeTypeForValidation](
				        const std::string& TypeName,
				        std::string& OutError,
				        auto& Self) -> bool
				{
					const std::string C = CanonicalizeNativeElementTypeName(
					    RuntimeTypeForValidation(TypeName));
					if (IsPrimitiveNativeElementTypeName(C) || Runtime::IsFunctionTypeName(C) || FindSignatureType(*this, C) != nullptr)
					{
						return true;
					}
					std::string Inner;
					if (Runtime::ParseDynamicArrayElementType(C, Inner) || Runtime::ParseQueueElementType(C, Inner) || Runtime::ParseStackElementType(C, Inner) || Runtime::ParseDequeElementType(C, Inner) || Runtime::ParseChannelElementType(C, Inner))
					{
						return Self(Inner, OutError, Self);
					}
					std::string Key;
					std::string Value;
					if (Runtime::SplitMapType(C, Key, Value) || Runtime::SplitOrderedMapType(C, Key, Value))
					{
						return ValidateMapOrSetKeyTypeName(Key, OutError) && Self(Value, OutError, Self);
					}
					if (Runtime::SplitSetElementType(C, Inner) || Runtime::ParseMultiSetElementType(C, Inner))
					{
						return ValidateMapOrSetKeyTypeName(Inner, OutError);
					}
					OutError = "diag.hostbinding_unknown_array_element_type_name";
					return false;
				};
				if (!ValidateNested(Member.ReturnElementTypeName, NestedError, ValidateNested))
				{
					OutErrorMessageId = NestedError;
					return false;
				}
			}
			if (Member.ReturnType == ENativeType::Set || Member.ReturnType == ENativeType::MultiSet || Member.ReturnType == ENativeType::OrderedMultiSet)
			{
				if (Member.ReturnElementTypeName.empty())
				{
					OutErrorMessageId = "diag.hostbinding_set_return_requires_element_type_name";
					return false;
				}
				if (!IsSupportedMapOrSetKeyType(Member.ReturnElementTypeName))
				{
					OutErrorMessageId = "diag.hostbinding_unsupported_map_or_set_key_type";
					return false;
				}
			}
			if (NativeTypeIsBinaryMap(Member.ReturnType))
			{
				if (Member.ReturnKeyTypeName.empty() || Member.ReturnValueTypeName.empty())
				{
					OutErrorMessageId = "diag.hostbinding_map_return_requires_key_value_type_names";
					return false;
				}
				if (!IsSupportedMapOrSetKeyType(Member.ReturnKeyTypeName))
				{
					OutErrorMessageId = "diag.hostbinding_unsupported_map_or_set_key_type";
					return false;
				}
			}
			// Keep catalog / FormatHost aligned with RegisterNative (`unsigned` -> `unsigned int`).
			if (!Member.ReturnElementTypeName.empty())
			{
				Member.ReturnElementTypeName =
				    CanonicalizeNativeElementTypeName(Member.ReturnElementTypeName);
			}
			if (!Member.ReturnKeyTypeName.empty())
			{
				Member.ReturnKeyTypeName =
				    CanonicalizeNativeElementTypeName(Member.ReturnKeyTypeName);
			}
			if (!Member.ReturnValueTypeName.empty())
			{
				Member.ReturnValueTypeName =
				    CanonicalizeNativeElementTypeName(Member.ReturnValueTypeName);
			}
			for (std::string& ElementName : Member.ParamElementTypeNames)
			{
				if (!ElementName.empty())
				{
					ElementName = CanonicalizeNativeElementTypeName(ElementName);
				}
			}
			for (std::string& KeyName : Member.ParamKeyTypeNames)
			{
				if (!KeyName.empty())
				{
					KeyName = CanonicalizeNativeElementTypeName(KeyName);
				}
			}
			for (std::string& ValueName : Member.ParamValueTypeNames)
			{
				if (!ValueName.empty())
				{
					ValueName = CanonicalizeNativeElementTypeName(ValueName);
				}
			}
			if (!ValidateNativeParamFlags(Member.ParamTypes, Member.ParamFlags, OutErrorMessageId))
			{
				return false;
			}
		}

		const std::string CanonicalParams = JoinParamTypes(
		    Member.ParamTypes,
		    Member.ParamHostTypeNames,
		    Member.ParamElementTypeNames,
		    Member.ParamFlags,
		    Member.ParamKeyTypeNames,
		    Member.ParamValueTypeNames);
		if (Member.bOperator)
		{
			Member.NativeName = MakeHostOperatorNativeName(
			    Type->Name, Member.Name + CanonicalParams);
		}
		else if (Member.Kind == EHostMemberKind::Constructor)
		{
			Member.NativeName = MakeHostConstructorNativeName(
			    Type->Name, CanonicalParams);
		}
		const auto ParameterKey = [&](const FHostMemberDesc& M)
		{
			FNativeFunction N;
			N.HostTypeName = Type->Name;
			N.ParamTypes = M.ParamTypes;
			N.ParamHostTypeNames = M.ParamHostTypeNames;
			N.ParamElementTypeNames = M.ParamElementTypeNames;
			N.ParamKeyTypeNames = M.ParamKeyTypeNames;
			N.ParamValueTypeNames = M.ParamValueTypeNames;
			N.ParamFlags = M.ParamFlags;
			for (std::size_t I = 0; I < N.ParamTypes.size(); ++I)
			{
				std::string RuntimeType, Ignored;
				const auto Declared = ResolveNativeParamTypeName(N, I);
				if (!ResolveHostRuntimeTypeName(*this, Declared, RuntimeType, Ignored))
				{
					RuntimeType = Declared;
				}
				N.RuntimeParamTypeNames.push_back(std::move(RuntimeType));
			}
			return NativeParameterSignature(N);
		};
		const auto MemberParameterKey = ParameterKey(Member);
		if (!Member.bOperator && (Member.Kind == EHostMemberKind::Method || Member.Kind == EHostMemberKind::StaticMethod))
		{
			Member.NativeName = MakeHostMethodOverloadNativeName(Type->Name, Member.Name,
			    MemberParameterKey + (Member.bConstMethod ? " const" : ""));
		}

		for (const FHostMemberDesc& Existing : Type->Members)
		{
			const bool NamedMethods = !Existing.bOperator && !Member.bOperator && (Existing.Kind == EHostMemberKind::Method || Existing.Kind == EHostMemberKind::StaticMethod) && (Member.Kind == EHostMemberKind::Method || Member.Kind == EHostMemberKind::StaticMethod);
			if ((Existing.Kind == Member.Kind || NamedMethods) && Existing.Name == Member.Name)
			{
				const bool bDistinctOperatorOverload = Existing.bOperator && Member.bOperator && ParameterKey(Existing) != MemberParameterKey;
				const bool bDistinctConstructorOverload =
				    Existing.Kind == EHostMemberKind::Constructor && Member.Kind == EHostMemberKind::Constructor && ParameterKey(Existing) != MemberParameterKey;
				const bool bDistinctNamedOverload = NamedMethods && (ParameterKey(Existing) != MemberParameterKey || (Existing.Kind == EHostMemberKind::Method && Member.Kind == EHostMemberKind::Method && Existing.bConstMethod != Member.bConstMethod));
				if (!bDistinctOperatorOverload && !bDistinctConstructorOverload && !bDistinctNamedOverload)
				{
					OutErrorMessageId = "diag.hostbinding_duplicate_member";
					return false;
				}
			}
			// Field and PropertyGetter share the same property name namespace.
			if ((Existing.Kind == EHostMemberKind::Field || Existing.Kind == EHostMemberKind::PropertyGetter) && (Member.Kind == EHostMemberKind::Field || Member.Kind == EHostMemberKind::PropertyGetter) && Existing.Name == Member.Name)
			{
				OutErrorMessageId = "diag.hostbinding_duplicate_member";
				return false;
			}
		}

		const bool bObjectType =
		    Type->Kind == EHostTypeKind::Class || Type->Kind == EHostTypeKind::Struct;

		const bool bDispatchable =
		    Member.Callback && bObjectType && (Member.Kind == EHostMemberKind::Constructor || Member.Kind == EHostMemberKind::Method || Member.Kind == EHostMemberKind::StaticMethod || Member.Kind == EHostMemberKind::StaticConstant || Member.Kind == EHostMemberKind::PropertyGetter || Member.Kind == EHostMemberKind::PropertySetter || Member.Kind == EHostMemberKind::Field);

		if (bDispatchable)
		{
			FNativeFunction Entry;
			Entry.Callback = Member.Callback;
			Entry.CallbackOwner = JoinCallbackOwners(Type->ModuleOwner, Member.CallbackOwner);
			Entry.Origin = Type->Origin;
			Entry.ProviderIdentity = Type->ProviderIdentity;
			Entry.UserData = Member.UserData;
			Entry.HostTypeName = Type->Name;
			const std::string ResolvedReturnHost = Member.ReturnHostTypeName.empty()
			    ? Type->Name
			    : Member.ReturnHostTypeName;

			if (Member.Kind == EHostMemberKind::Constructor)
			{
				Entry.Name = Member.NativeName;
				Entry.ReturnType = ENativeType::HostObject;
				Entry.ParamTypes = Member.ParamTypes;
				Entry.ReturnHostTypeName = Type->Name;
				Entry.HostTypeName = Type->Name;
				FillParamHostTypeNames(Entry, Type->Name, Member.ParamHostTypeNames, false);
				FillParamElementTypeNames(Entry, Member.ParamElementTypeNames, false);
				FillParamKeyValueTypeNames(
				    Entry, Member.ParamKeyTypeNames, Member.ParamValueTypeNames, false);
				FillParamFlags(Entry, Member.ParamFlags, false);
				if (!TryAddNative(std::move(Entry), OutErrorMessageId, false))
				{
					return false;
				}
			}
			else if (Member.Kind == EHostMemberKind::StaticMethod || Member.Kind == EHostMemberKind::StaticConstant)
			{
				Entry.Name = Member.Kind == EHostMemberKind::StaticConstant
				    ? MakeHostStaticConstantNativeName(Type->Name, Member.Name)
				    : Member.NativeName;
				Entry.ReturnType = Member.ReturnType;
				Entry.ParamTypes = Member.ParamTypes;
				Entry.bHostStaticMethod = true;
				if (Entry.ReturnType == ENativeType::HostObject || Entry.ReturnType == ENativeType::Enum || Entry.ReturnType == ENativeType::Function)
				{
					Entry.ReturnHostTypeName = ResolvedReturnHost;
					if (Entry.ReturnType == ENativeType::HostObject)
					{
						Entry.HostTypeName = ResolvedReturnHost;
					}
				}
				if (Entry.ReturnType == ENativeType::Array || Entry.ReturnType == ENativeType::Set || Entry.ReturnType == ENativeType::MultiSet || Entry.ReturnType == ENativeType::OrderedMultiSet || Entry.ReturnType == ENativeType::Queue || Entry.ReturnType == ENativeType::PriorityQueue || Entry.ReturnType == ENativeType::Stack || Entry.ReturnType == ENativeType::Deque || Entry.ReturnType == ENativeType::Channel)
				{
					Entry.ReturnElementTypeName =
					    CanonicalizeNativeElementTypeName(Member.ReturnElementTypeName);
				}
				if (NativeTypeIsBinaryMap(Entry.ReturnType))
				{
					Entry.ReturnKeyTypeName =
					    CanonicalizeNativeElementTypeName(Member.ReturnKeyTypeName);
					Entry.ReturnValueTypeName =
					    CanonicalizeNativeElementTypeName(Member.ReturnValueTypeName);
				}
				FillParamHostTypeNames(Entry, Type->Name, Member.ParamHostTypeNames, false);
				FillParamElementTypeNames(Entry, Member.ParamElementTypeNames, false);
				FillParamKeyValueTypeNames(
				    Entry, Member.ParamKeyTypeNames, Member.ParamValueTypeNames, false);
				FillParamFlags(Entry, Member.ParamFlags, false);
				if (!TryAddNative(std::move(Entry), OutErrorMessageId, false))
				{
					return false;
				}
			}
			else if (Member.Kind == EHostMemberKind::PropertyGetter || Member.Kind == EHostMemberKind::Field)
			{
				Entry.Name = MakeHostPropertyGetterNativeName(Type->Name, Member.Name);
				Entry.ReturnType = Member.ReturnType;
				Entry.ParamTypes = {ENativeType::HostObject};
				if (Entry.ReturnType == ENativeType::HostObject || Entry.ReturnType == ENativeType::Enum || Entry.ReturnType == ENativeType::Function)
				{
					Entry.ReturnHostTypeName = ResolvedReturnHost;
				}
				if (Entry.ReturnType == ENativeType::Array || Entry.ReturnType == ENativeType::Set || Entry.ReturnType == ENativeType::MultiSet || Entry.ReturnType == ENativeType::OrderedMultiSet || Entry.ReturnType == ENativeType::Queue || Entry.ReturnType == ENativeType::PriorityQueue || Entry.ReturnType == ENativeType::Stack || Entry.ReturnType == ENativeType::Deque || Entry.ReturnType == ENativeType::Channel)
				{
					Entry.ReturnElementTypeName =
					    CanonicalizeNativeElementTypeName(Member.ReturnElementTypeName);
				}
				if (NativeTypeIsBinaryMap(Entry.ReturnType))
				{
					Entry.ReturnKeyTypeName =
					    CanonicalizeNativeElementTypeName(Member.ReturnKeyTypeName);
					Entry.ReturnValueTypeName =
					    CanonicalizeNativeElementTypeName(Member.ReturnValueTypeName);
				}
				FillParamHostTypeNames(Entry, Type->Name, {}, true);
				FillParamElementTypeNames(Entry, {}, true);
				FillParamKeyValueTypeNames(Entry, {}, {}, true);
				FillParamFlags(Entry, {}, true);
				if (!TryAddNative(std::move(Entry), OutErrorMessageId, false))
				{
					return false;
				}
			}
			else if (Member.Kind == EHostMemberKind::PropertySetter)
			{
				if (Member.ParamTypes.size() != 1)
				{
					OutErrorMessageId = "diag.hostbinding_property_setter_expects_one_parameter";
					return false;
				}
				Entry.Name = MakeHostPropertySetterNativeName(Type->Name, Member.Name);
				Entry.ReturnType = ENativeType::Void;
				Entry.ParamTypes = {ENativeType::HostObject, Member.ParamTypes[0]};
				FillParamHostTypeNames(Entry, Type->Name, Member.ParamHostTypeNames, true);
				FillParamElementTypeNames(Entry, Member.ParamElementTypeNames, true);
				FillParamKeyValueTypeNames(
				    Entry, Member.ParamKeyTypeNames, Member.ParamValueTypeNames, true);
				FillParamFlags(Entry, Member.ParamFlags, true);
				if (Type->Kind == EHostTypeKind::Struct)
				{
					Member.bMutableThis = true;
					Entry.ParamFlags[0] = NativeParamFlagRef;
				}
				if (!TryAddNative(std::move(Entry), OutErrorMessageId, false))
				{
					return false;
				}
			}
			else
			{
				Entry.Name = Member.NativeName;
				Entry.ReturnType = Member.ReturnType;
				Entry.ParamTypes.reserve(Member.ParamTypes.size() + 1);
				Entry.ParamTypes.push_back(ENativeType::HostObject);
				Entry.ParamTypes.insert(
				    Entry.ParamTypes.end(),
				    Member.ParamTypes.begin(),
				    Member.ParamTypes.end());
				if (Entry.ReturnType == ENativeType::HostObject || Entry.ReturnType == ENativeType::Enum || Entry.ReturnType == ENativeType::Function)
				{
					Entry.ReturnHostTypeName = ResolvedReturnHost;
				}
				if (Entry.ReturnType == ENativeType::Array || Entry.ReturnType == ENativeType::Set || Entry.ReturnType == ENativeType::MultiSet || Entry.ReturnType == ENativeType::OrderedMultiSet || Entry.ReturnType == ENativeType::Queue || Entry.ReturnType == ENativeType::PriorityQueue || Entry.ReturnType == ENativeType::Stack || Entry.ReturnType == ENativeType::Deque || Entry.ReturnType == ENativeType::Channel)
				{
					Entry.ReturnElementTypeName =
					    CanonicalizeNativeElementTypeName(Member.ReturnElementTypeName);
				}
				if (NativeTypeIsBinaryMap(Entry.ReturnType))
				{
					Entry.ReturnKeyTypeName =
					    CanonicalizeNativeElementTypeName(Member.ReturnKeyTypeName);
					Entry.ReturnValueTypeName =
					    CanonicalizeNativeElementTypeName(Member.ReturnValueTypeName);
				}
				FillParamHostTypeNames(Entry, Type->Name, Member.ParamHostTypeNames, true);
				FillParamElementTypeNames(Entry, Member.ParamElementTypeNames, true);
				FillParamKeyValueTypeNames(
				    Entry, Member.ParamKeyTypeNames, Member.ParamValueTypeNames, true);
				FillParamFlags(Entry, Member.ParamFlags, true);
				if (Type->Kind == EHostTypeKind::Struct && Member.bMutableThis)
				{
					Entry.ParamFlags[0] = NativeParamFlagRef;
				}

				if (!TryAddNative(std::move(Entry), OutErrorMessageId, false))
				{
					return false;
				}
			}
		}

		Type->Members.push_back(std::move(Member));
		return true;
	}

	bool FHostBindingRegistry::RegisterOperator(
	    const FHostTypeId TypeId,
	    FHostMemberDesc Operator,
	    std::string& OutErrorMessageId)
	{
		InvalidateRuntimeBindings();
		Operator.Kind = EHostMemberKind::Method;
		Operator.bOperator = true;
		return RegisterMember(TypeId, std::move(Operator), OutErrorMessageId);
	}

	bool FHostBindingRegistry::RegisterTypeClone(
	    const FHostTypeId TypeId,
	    const FNativeCallback Callback,
	    void* const UserData,
	    std::string& OutErrorMessageId)
	{
		InvalidateRuntimeBindings();
		OutErrorMessageId.clear();
		FHostTypeDesc* Type = nullptr;
		for (FHostTypeDesc& Candidate : Types)
		{
			if (Candidate.Id == TypeId)
			{
				Type = &Candidate;
				break;
			}
		}
		if (!Type)
		{
			OutErrorMessageId = "diag.hostbinding_unknown_type_id";
			return false;
		}
		if (Type->Kind != EHostTypeKind::Struct)
		{
			OutErrorMessageId = "diag.hostbinding_clone_requires_struct_type";
			return false;
		}
		if (!Callback)
		{
			OutErrorMessageId = "diag.registernative_requires_a_non_null_callback";
			return false;
		}

		FNativeFunction Entry;
		Entry.Name = MakeHostCloneNativeName(Type->Name);
		Entry.ReturnType = ENativeType::HostObject;
		Entry.ParamTypes = {ENativeType::HostObject};
		Entry.Callback = Callback;
		Entry.UserData = UserData;
		Entry.HostTypeName = Type->Name;
		Entry.ReturnHostTypeName = Type->Name;
		Entry.ParamHostTypeNames = {Type->Name};
		// Keep @clone off GetFunctions() (same as @subclass / member natives).
		if (!TryAddNative(std::move(Entry), OutErrorMessageId, false))
		{
			return false;
		}
		Type->CloneCallback = Callback;
		Type->CloneUserData = UserData;
		return true;
	}

	bool FHostBindingRegistry::RegisterTypeEquals(
	    const FHostTypeId TypeId,
	    const FNativeCallback Callback,
	    void* const UserData,
	    std::string& OutErrorMessageId)
	{
		InvalidateRuntimeBindings();
		OutErrorMessageId.clear();
		FHostTypeDesc* Type = nullptr;
		for (FHostTypeDesc& Candidate : Types)
		{
			if (Candidate.Id == TypeId)
			{
				Type = &Candidate;
				break;
			}
		}
		if (!Type)
		{
			OutErrorMessageId = "diag.hostbinding_unknown_type_id";
			return false;
		}
		if (Type->Kind != EHostTypeKind::Struct)
		{
			OutErrorMessageId = "diag.hostbinding_equals_requires_struct_type";
			return false;
		}
		if (!Callback)
		{
			OutErrorMessageId = "diag.registernative_requires_a_non_null_callback";
			return false;
		}

		FNativeFunction Entry;
		Entry.Name = MakeHostEqualsNativeName(Type->Name);
		Entry.ReturnType = ENativeType::Bool;
		Entry.ParamTypes = {ENativeType::HostObject, ENativeType::HostObject};
		Entry.Callback = Callback;
		Entry.UserData = UserData;
		Entry.HostTypeName = Type->Name;
		Entry.ParamHostTypeNames = {Type->Name, Type->Name};
		if (!TryAddNative(std::move(Entry), OutErrorMessageId, false))
		{
			return false;
		}
		Type->EqualsCallback = Callback;
		Type->EqualsUserData = UserData;
		return true;
	}

	bool FHostBindingRegistry::RegisterTypeHash(
	    const FHostTypeId TypeId,
	    const FNativeCallback Callback,
	    void* const UserData,
	    std::string& OutErrorMessageId)
	{
		InvalidateRuntimeBindings();
		OutErrorMessageId.clear();
		FHostTypeDesc* Type = nullptr;
		for (FHostTypeDesc& Candidate : Types)
		{
			if (Candidate.Id == TypeId)
			{
				Type = &Candidate;
				break;
			}
		}
		if (!Type)
		{
			OutErrorMessageId = "diag.hostbinding_unknown_type_id";
			return false;
		}
		if (Type->Kind != EHostTypeKind::Struct)
		{
			OutErrorMessageId = "diag.hostbinding_hash_requires_struct_type";
			return false;
		}
		if (!Callback)
		{
			OutErrorMessageId = "diag.registernative_requires_a_non_null_callback";
			return false;
		}

		FNativeFunction Entry;
		Entry.Name = MakeHostHashNativeName(Type->Name);
		Entry.ReturnType = ENativeType::UInt64;
		Entry.ParamTypes = {ENativeType::HostObject};
		Entry.Callback = Callback;
		Entry.UserData = UserData;
		Entry.HostTypeName = Type->Name;
		Entry.ParamHostTypeNames = {Type->Name};
		if (!TryAddNative(std::move(Entry), OutErrorMessageId, false))
		{
			return false;
		}
		Type->HashCallback = Callback;
		Type->HashUserData = UserData;
		return true;
	}

	bool FHostBindingRegistry::SetTypeInheritable(
	    const FHostTypeId TypeId,
	    const bool bInheritable,
	    std::string& OutErrorMessageId)
	{
		InvalidateRuntimeBindings();
		OutErrorMessageId.clear();
		FHostTypeDesc* Type = nullptr;
		for (FHostTypeDesc& Candidate : Types)
		{
			if (Candidate.Id == TypeId)
			{
				Type = &Candidate;
				break;
			}
		}
		if (!Type)
		{
			OutErrorMessageId = "diag.hostbinding_unknown_type_id";
			return false;
		}
		if (bInheritable && Type->Kind != EHostTypeKind::Class && Type->Kind != EHostTypeKind::Struct)
		{
			OutErrorMessageId = "diag.hostbinding_only_class_or_struct_can_be_inheritable";
			return false;
		}
		Type->bInheritable = bInheritable;
		return true;
	}

	bool FHostBindingRegistry::SetEnumScoped(
	    const FHostTypeId TypeId,
	    const bool bScoped,
	    std::string& OutErrorMessageId)
	{
		InvalidateRuntimeBindings();
		OutErrorMessageId.clear();
		FHostTypeDesc* Type = nullptr;
		for (FHostTypeDesc& Candidate : Types)
		{
			if (Candidate.Id == TypeId)
			{
				Type = &Candidate;
				break;
			}
		}
		if (!Type)
		{
			OutErrorMessageId = "diag.hostbinding_unknown_type_id";
			return false;
		}
		if (Type->Kind != EHostTypeKind::Enum)
		{
			OutErrorMessageId = "diag.hostbinding_scoped_requires_enum_type";
			return false;
		}
		if (!bScoped)
		{
			for (const FHostMemberDesc& Member : Type->Members)
			{
				if (Member.Kind == EHostMemberKind::EnumMember && !Member.ReturnHostTypeName.empty())
				{
					OutErrorMessageId =
					    "diag.hostbinding_cannot_unset_scoped_enum_with_typed_members";
					return false;
				}
			}
		}
		Type->bScopedEnum = bScoped;
		if (bScoped)
		{
			for (FHostMemberDesc& Member : Type->Members)
			{
				if (Member.Kind == EHostMemberKind::EnumMember)
				{
					Member.ReturnHostTypeName = Type->Name;
				}
			}
		}
		return true;
	}

	bool FHostBindingRegistry::SetEnumUnderlyingType(
	    const FHostTypeId TypeId,
	    std::string UnderlyingTypeName,
	    std::string& OutErrorMessageId)
	{
		InvalidateRuntimeBindings();
		OutErrorMessageId.clear();
		FHostTypeDesc* Type = nullptr;
		for (FHostTypeDesc& Candidate : Types)
		{
			if (Candidate.Id == TypeId)
			{
				Type = &Candidate;
				break;
			}
		}
		if (!Type)
		{
			OutErrorMessageId = "diag.hostbinding_unknown_type_id";
			return false;
		}
		if (Type->Kind != EHostTypeKind::Enum)
		{
			OutErrorMessageId = "diag.hostbinding_enum_underlying_requires_enum_type";
			return false;
		}
		// FEnumValue stores its payload as int64. Reject the only integral spelling
		// whose domain cannot be represented losslessly instead of silently narrowing.
		if (UnderlyingTypeName == "uint64" || UnderlyingTypeName == "unsigned long")
		{
			OutErrorMessageId = "diag.hostbinding_enum_underlying_exceeds_int64_domain";
			return false;
		}
		if (UnderlyingTypeName == "int8")
		{
			UnderlyingTypeName = "char";
		}
		else if (UnderlyingTypeName == "uint8")
		{
			UnderlyingTypeName = "unsigned char";
		}
		else if (UnderlyingTypeName == "int16")
		{
			UnderlyingTypeName = "short";
		}
		else if (UnderlyingTypeName == "uint16")
		{
			UnderlyingTypeName = "unsigned short";
		}
		else if (UnderlyingTypeName == "int32")
		{
			UnderlyingTypeName = "int";
		}
		else if (UnderlyingTypeName == "uint32" || UnderlyingTypeName == "unsigned")
		{
			UnderlyingTypeName = "unsigned int";
		}
		else if (UnderlyingTypeName == "int64")
		{
			UnderlyingTypeName = "long";
		}
		if (UnderlyingTypeName != "char" && UnderlyingTypeName != "short" && UnderlyingTypeName != "int" && UnderlyingTypeName != "long" && UnderlyingTypeName != "unsigned char" && UnderlyingTypeName != "unsigned short" && UnderlyingTypeName != "unsigned int")
		{
			OutErrorMessageId = "diag.hostbinding_enum_underlying_must_be_integral";
			return false;
		}
		Type->EnumUnderlyingTypeName = std::move(UnderlyingTypeName);
		return true;
	}

	bool FHostBindingRegistry::RegisterTypeSubclassFactory(
	    const FHostTypeId TypeId,
	    const FNativeCallback Callback,
	    void* const UserData,
	    std::string& OutErrorMessageId)
	{
		InvalidateRuntimeBindings();
		OutErrorMessageId.clear();
		FHostTypeDesc* Type = nullptr;
		for (FHostTypeDesc& Candidate : Types)
		{
			if (Candidate.Id == TypeId)
			{
				Type = &Candidate;
				break;
			}
		}
		if (!Type)
		{
			OutErrorMessageId = "diag.hostbinding_unknown_type_id";
			return false;
		}
		if (Type->Kind != EHostTypeKind::Class && Type->Kind != EHostTypeKind::Struct)
		{
			OutErrorMessageId = "diag.hostbinding_subclass_requires_class_or_struct_type";
			return false;
		}
		if (!Callback)
		{
			OutErrorMessageId = "diag.registernative_requires_a_non_null_callback";
			return false;
		}

		FNativeFunction Entry;
		Entry.Name = MakeHostSubclassNativeName(Type->Name);
		Entry.ReturnType = ENativeType::HostObject;
		Entry.ParamTypes = {ENativeType::String};
		Entry.Callback = Callback;
		Entry.UserData = UserData;
		Entry.HostTypeName = Type->Name;
		Entry.ReturnHostTypeName = Type->Name;
		Entry.ParamHostTypeNames = {};
		if (!TryAddNative(std::move(Entry), OutErrorMessageId, false))
		{
			return false;
		}
		Type->SubclassFactory = Callback;
		Type->SubclassFactoryUserData = UserData;
		return true;
	}

	bool FHostBindingRegistry::RegisterAnnotation(
	    std::string Name,
	    std::string Detail,
	    std::string& OutErrorMessageId,
	    std::string Documentation,
	    std::vector<FHostAnnotationArgumentDesc> Arguments,
	    std::uint32_t Targets,
	    bool CaseSensitive)
	{
		InvalidateRuntimeBindings();
		OutErrorMessageId.clear();
		if (!Targets || Targets > 131071 || !IsValidAnnotationName(Name))
		{
			OutErrorMessageId = "diag.hostbinding_invalid_annotation_name";
			return false;
		}
		for (const FHostAnnotationDesc& Existing : Annotations)
		{
			if (AnnotationNamesEqual(Existing.Name, Name, Existing.CaseSensitive && CaseSensitive))
			{
				OutErrorMessageId = "diag.hostbinding_duplicate_annotation_name";
				return false;
			}
		}
		std::size_t DescriptionNodes = 0;
		std::function<bool(const std::vector<FHostAnnotationArgumentDesc>&, std::size_t)> ValidateDescriptions;
		ValidateDescriptions = [&](const auto& Items, std::size_t Depth)
		{
			if (Depth > 64 || Items.size() > 65536 - DescriptionNodes)
			{
				OutErrorMessageId = "diag.hostbinding_invalid_annotation_argument_name";
				return false;
			}
			DescriptionNodes += Items.size();
			for (std::size_t Index = 0; Index < Items.size(); ++Index)
			{
				if (!IsValidAnnotationName(Items[Index].Name) || Items[Index].Name.front() == '@')
				{
					OutErrorMessageId = "diag.hostbinding_invalid_annotation_argument_name";
					return false;
				}
				for (std::size_t Previous = 0; Previous < Index; ++Previous)
				{
					if (Items[Previous].Name == Items[Index].Name)
					{
						OutErrorMessageId = "diag.hostbinding_duplicate_annotation_argument_name";
						return false;
					}
				}

				if (!ValidateDescriptions(Items[Index].Children, Depth + 1)) return false;
			}
			return true;
		};
		if (!ValidateDescriptions(Arguments, 0)) return false;

		FHostAnnotationDesc Annotation;
		Annotation.Name = std::move(Name);
		Annotation.Detail = std::move(Detail);
		Annotation.Documentation = std::move(Documentation);
		Annotation.Arguments = std::move(Arguments);
		Annotation.Targets = Targets;
		Annotation.CaseSensitive = CaseSensitive;
		Annotations.push_back(std::move(Annotation));
		return true;
	}

	bool FHostBindingRegistry::SetFunctionDocumentation(
	    const std::string_view Name,
	    std::string Documentation,
	    std::vector<std::string> ParamNames,
	    std::vector<std::string> ParamDocumentation,
	    std::string ReturnDocumentation,
	    std::string& OutErrorMessageId)
	{
		InvalidateRuntimeBindings();
		OutErrorMessageId.clear();
		for (FHostFunctionDesc& Function : Functions)
		{
			if (Function.Name != Name)
			{
				continue;
			}
			if (!ParamNames.empty() && ParamNames.size() != Function.ParamTypes.size())
			{
				OutErrorMessageId = "diag.hostbinding_param_name_count_mismatch";
				return false;
			}
			if (!ParamDocumentation.empty() && ParamDocumentation.size() != Function.ParamTypes.size())
			{
				OutErrorMessageId = "diag.hostbinding_param_documentation_count_mismatch";
				return false;
			}
			Function.Documentation = std::move(Documentation);
			Function.ParamNames = std::move(ParamNames);
			Function.ParamDocumentation = std::move(ParamDocumentation);
			Function.ReturnDocumentation = std::move(ReturnDocumentation);
			return true;
		}
		OutErrorMessageId = "diag.hostbinding_unknown_function";
		return false;
	}

	bool FHostBindingRegistry::SetTypeDocumentation(
	    const FHostTypeId TypeId,
	    std::string Documentation,
	    std::string& OutErrorMessageId)
	{
		InvalidateRuntimeBindings();
		OutErrorMessageId.clear();
		for (FHostTypeDesc& Type : Types)
		{
			if (Type.Id != TypeId)
			{
				continue;
			}
			Type.Documentation = std::move(Documentation);
			return true;
		}
		OutErrorMessageId = "diag.hostbinding_unknown_type_id";
		return false;
	}

	void FHostBindingRegistry::Clear()
	{
		InvalidateRuntimeBindings();
		Natives.clear();
		Functions.clear();
		Types.clear();
		TypeAliases.clear();
		GenericTypeTemplates.clear();
		Annotations.clear();
		NextTypeId = 1;
		NextGenericTemplateId = 1;
		RegistrationOwner.reset();
		RegistrationProviderIdentity.reset();
		RegistrationOrigin = {};
	}

	FHostBindingRegistry::FHostBindingRegistry(FHostBindingRegistry&& Other) noexcept
	{
		Swap(Other);
	}

	FHostBindingRegistry& FHostBindingRegistry::operator=(FHostBindingRegistry Other) noexcept
	{
		Swap(Other);
		return *this;
	}

	void FHostBindingRegistry::InvalidateRuntimeBindings() noexcept
	{
		// Keep the counter on the registry: host and module copies of Core may
		// both mutate this instance. Assignment/swap must not copy its revision.
		if (++Revision == 0)
		{
			std::terminate();
		}
	}

	void FHostBindingRegistry::Swap(FHostBindingRegistry& Other) noexcept
	{
		InvalidateRuntimeBindings();
		RegistrationOwner.swap(Other.RegistrationOwner);
		RegistrationProviderIdentity.swap(Other.RegistrationProviderIdentity);
		std::swap(RegistrationOrigin, Other.RegistrationOrigin);
		Natives.swap(Other.Natives);
		Functions.swap(Other.Functions);
		Types.swap(Other.Types);
		TypeAliases.swap(Other.TypeAliases);
		GenericTypeTemplates.swap(Other.GenericTypeTemplates);
		Annotations.swap(Other.Annotations);
		std::swap(NextTypeId, Other.NextTypeId);
		std::swap(NextGenericTemplateId, Other.NextGenericTemplateId);
		Other.InvalidateRuntimeBindings();
	}

	bool FHostBindingRegistry::IsValidParamType(const ENativeType Type)
	{
		switch (Type)
		{
		case ENativeType::Variant:
		case ENativeType::Int:
		case ENativeType::Bool:
		case ENativeType::Double:
		case ENativeType::Float:
		case ENativeType::Int8:
		case ENativeType::UInt8:
		case ENativeType::Int16:
		case ENativeType::UInt16:
		case ENativeType::String:
		case ENativeType::Unsigned:
		case ENativeType::Int64:
		case ENativeType::UInt64:
		case ENativeType::Enum:
		case ENativeType::Function:
		case ENativeType::HostObject:
		case ENativeType::Array:
		case ENativeType::Map:
		case ENativeType::MultiMap:
		case ENativeType::Set:
		case ENativeType::MultiSet:
		case ENativeType::OrderedMultiSet:
		case ENativeType::Queue:
		case ENativeType::PriorityQueue:
		case ENativeType::Stack:
		case ENativeType::OrderedMap:
		case ENativeType::OrderedMultiMap:
		case ENativeType::Deque:
		case ENativeType::Channel:
			return true;
		default:
			return false;
		}
	}

	bool FHostBindingRegistry::IsValidReturnType(const ENativeType Type)
	{
		return Type == ENativeType::Void || IsValidParamType(Type);
	}

	bool FHostBindingRegistry::TryAddNative(
	    FNativeFunction Entry,
	    std::string& OutErrorMessageId,
	    const bool bExposeAsFreeFunction)
	{
		InvalidateRuntimeBindings();
		OutErrorMessageId.clear();
		std::string_view IntrinsicName = Entry.Name;
		if (IntrinsicName.substr(0, 5) == "std::")
		{
			IntrinsicName.remove_prefix(5);
		}
		else if (IntrinsicName.substr(0, 4) == "std.")
		{
			IntrinsicName.remove_prefix(4);
		}
		if (bExposeAsFreeFunction && (IntrinsicName == "variant" || IntrinsicName == "variant_is" || IntrinsicName == "variant_has_value" || IntrinsicName == "variant_type_name" || IntrinsicName == "__variant_empty"))
		{
			OutErrorMessageId = "diag.variant_reserved_name";
			return false;
		}
		Entry.CallbackOwner = JoinCallbackOwners(RegistrationOwner, Entry.CallbackOwner);
		if (!Entry.ProviderIdentity)
		{
			Entry.ProviderIdentity = RegistrationProviderIdentity;
		}

		if (Entry.Origin.ProviderId.empty())
		{
			Entry.Origin = RegistrationOrigin;
		}

		const auto ResolveRuntime = [this, &OutErrorMessageId](
		                                const std::string& Declared, std::string& Out) -> bool
		{
			return ResolveHostRuntimeTypeName(*this, Declared, Out, OutErrorMessageId);
		};
		const std::string DeclaredReturnType = HostOrNativeTypeName(
		    Entry.ReturnType,
		    Entry.ReturnHostTypeName.empty() ? Entry.HostTypeName : Entry.ReturnHostTypeName,
		    Entry.ReturnElementTypeName,
		    Entry.ReturnKeyTypeName,
		    Entry.ReturnValueTypeName);
		if (!ResolveRuntime(DeclaredReturnType, Entry.RuntimeReturnTypeName))
		{
			return false;
		}
		Entry.RuntimeParamTypeNames.resize(Entry.ParamTypes.size());
		for (std::size_t Index = 0; Index < Entry.ParamTypes.size(); ++Index)
		{
			const std::string DeclaredParamType = HostOrNativeTypeName(
			    Entry.ParamTypes[Index],
			    Index < Entry.ParamHostTypeNames.size()
			        ? Entry.ParamHostTypeNames[Index]
			        : std::string{},
			    Index < Entry.ParamElementTypeNames.size()
			        ? Entry.ParamElementTypeNames[Index]
			        : std::string{},
			    Index < Entry.ParamKeyTypeNames.size()
			        ? Entry.ParamKeyTypeNames[Index]
			        : std::string{},
			    Index < Entry.ParamValueTypeNames.size()
			        ? Entry.ParamValueTypeNames[Index]
			        : std::string{});
			if (!ResolveRuntime(DeclaredParamType, Entry.RuntimeParamTypeNames[Index]))
			{
				return false;
			}
		}
		if (Entry.ReturnType == ENativeType::HostObject)
		{
			const std::string& ReturnName = !Entry.RuntimeReturnTypeName.empty()
			    ? Entry.RuntimeReturnTypeName
			    : (Entry.ReturnHostTypeName.empty()
			              ? Entry.HostTypeName
			              : Entry.ReturnHostTypeName);
			if (const FHostTypeDesc* ReturnHost = FindType(ReturnName))
			{
				Entry.ReturnGenericTemplateId = ReturnHost->GenericTemplateId.Value;
				Entry.ReturnGenericTypeArguments = ReturnHost->GenericTypeArguments;
			}
		}
		if (Entry.Name.empty())
		{
			OutErrorMessageId = "diag.registernative_requires_a_non_empty_name";
			return false;
		}
		if (!Entry.Callback)
		{
			OutErrorMessageId = "diag.registernative_requires_a_non_null_callback";
			return false;
		}
		if (!IsValidReturnType(Entry.ReturnType))
		{
			OutErrorMessageId = "diag.registernative_has_invalid_return_type";
			return false;
		}
		if (Entry.ParamTypes.size() > 255)
		{
			OutErrorMessageId = "diag.registernative_supports_at_most_255_parameters";
			return false;
		}
		for (const ENativeType ParamType : Entry.ParamTypes)
		{
			if (!IsValidParamType(ParamType))
			{
				OutErrorMessageId = ParamType == ENativeType::Void
				    ? "diag.registernative_parameter_type_cannot_be_void"
				    : "diag.registernative_has_invalid_parameter_type";
				return false;
			}
		}
		for (const FNativeFunction& Existing : Natives)
		{
			if (Existing.Name == Entry.Name && (!bExposeAsFreeFunction || Existing.bGenericHostObjectCast || (!Entry.Name.empty() && Entry.Name.front() == '\x1f') || NativeParameterSignature(Existing) == NativeParameterSignature(Entry)))
			{
				OutErrorMessageId = "diag.registernative_duplicate_name";
				return false;
			}
		}
		if (Natives.size() >= 0xFFFFu)
		{
			OutErrorMessageId = "diag.registernative_table_is_full";
			return false;
		}

		// Constructors share the type name as the native callable; they must not
		// appear in GetFunctions() or catalog export treats them as free functions.
		const bool bScriptVisible =
		    bExposeAsFreeFunction && !Entry.Name.empty() && Entry.Name[0] != '\x1F';
		if (bScriptVisible)
		{
			FHostFunctionDesc Desc;
			Desc.Origin = RegistrationOrigin;
			Desc.Name = Entry.Name;
			Desc.ReturnType = Entry.ReturnType;
			Desc.ParamTypes = Entry.ParamTypes;
			Desc.ReturnHostTypeName = Entry.ReturnHostTypeName.empty()
			    ? Entry.HostTypeName
			    : Entry.ReturnHostTypeName;
			Desc.ParamHostTypeNames = Entry.ParamHostTypeNames;
			Desc.ReturnElementTypeName = Entry.ReturnElementTypeName;
			Desc.ParamElementTypeNames = Entry.ParamElementTypeNames;
			Desc.ReturnKeyTypeName = Entry.ReturnKeyTypeName;
			Desc.ReturnValueTypeName = Entry.ReturnValueTypeName;
			Desc.ParamKeyTypeNames = Entry.ParamKeyTypeNames;
			Desc.ParamValueTypeNames = Entry.ParamValueTypeNames;
			Desc.ParamFlags = Entry.ParamFlags;
			Desc.TypeParams = Entry.TypeParams;
			Desc.ScriptReturnTypeName = Entry.ScriptReturnTypeName;
			Desc.bGenericHostObjectCast = Entry.bGenericHostObjectCast;
			Functions.push_back(std::move(Desc));
		}

		try
		{
			Natives.push_back(std::move(Entry));
		}
		catch (...)
		{
			// Metadata and its executable native form one registration. Restore
			// the metadata table if allocating the native entry fails.
			if (bScriptVisible)
			{
				Functions.pop_back();
			}
			throw;
		}
		return true;
	}

	std::string FormatHostFunctionSignature(const FHostFunctionDesc& Function)
	{
		const std::string ReturnName = HostOrNativeTypeName(
		    Function.ReturnType,
		    Function.ReturnHostTypeName,
		    Function.ReturnElementTypeName,
		    Function.ReturnKeyTypeName,
		    Function.ReturnValueTypeName);
		return ReturnName + ' ' + JoinParamTypes(Function.ParamTypes, Function.ParamHostTypeNames, Function.ParamElementTypeNames, Function.ParamFlags, Function.ParamKeyTypeNames, Function.ParamValueTypeNames, Function.ParamNames);
	}

	std::string FormatHostMemberSignature(
	    const FHostMemberDesc& Member,
	    const std::string_view OwnerTypeName)
	{
		const std::string Owner(OwnerTypeName);
		const std::string ReturnHost = Member.ReturnHostTypeName.empty()
		    ? Owner
		    : Member.ReturnHostTypeName;
		if (Member.Kind == EHostMemberKind::EnumMember || Member.Kind == EHostMemberKind::Field || Member.Kind == EHostMemberKind::StaticConstant || Member.Kind == EHostMemberKind::PropertyGetter)
		{
			return HostOrNativeTypeName(
			    Member.ReturnType,
			    ReturnHost,
			    Member.ReturnElementTypeName,
			    Member.ReturnKeyTypeName,
			    Member.ReturnValueTypeName);
		}
		if (Member.Kind == EHostMemberKind::PropertySetter)
		{
			return std::string("void ") + JoinParamTypes(Member.ParamTypes, Member.ParamHostTypeNames, Member.ParamElementTypeNames, Member.ParamFlags, Member.ParamKeyTypeNames, Member.ParamValueTypeNames, Member.ParamNames);
		}
		std::string Signature = Member.Kind == EHostMemberKind::StaticMethod
		    ? "static "
		    : "";
		Signature += HostOrNativeTypeName(
		                 Member.ReturnType,
		                 ReturnHost,
		                 Member.ReturnElementTypeName,
		                 Member.ReturnKeyTypeName,
		                 Member.ReturnValueTypeName) +
		    ' ' + JoinParamTypes(Member.ParamTypes, Member.ParamHostTypeNames, Member.ParamElementTypeNames, Member.ParamFlags, Member.ParamKeyTypeNames, Member.ParamValueTypeNames, Member.ParamNames);
		if (Member.Kind == EHostMemberKind::Method && Member.bConstMethod)
		{
			Signature += " const";
		}
		return Signature;
	}

	std::string FormatHostAnnotations(
	    const std::vector<FHostAnnotationInstance>& Annotations)
	{
		std::string Result;
		for (std::size_t AnnotationIndex = 0;
		    AnnotationIndex < Annotations.size();
		    ++AnnotationIndex)
		{
			if (AnnotationIndex > 0)
			{
				Result.push_back(' ');
			}
			const FHostAnnotationInstance& Annotation = Annotations[AnnotationIndex];
			Result += Annotation.Name;
			if (Annotation.Arguments.empty())
			{
				continue;
			}
			Result.push_back('(');
			for (std::size_t ArgumentIndex = 0;
			    ArgumentIndex < Annotation.Arguments.size();
			    ++ArgumentIndex)
			{
				if (ArgumentIndex > 0)
				{
					Result += ", ";
				}
				const FHostAnnotationArgument& Argument =
				    Annotation.Arguments[ArgumentIndex];
				Result += Argument.Name;
				if (Argument.Value.has_value())
				{
					Result += "=\"";
					for (const char Ch : *Argument.Value)
					{
						switch (Ch)
						{
						case '\\':
							{
								Result += "\\\\";
								break;
							}
						case '"':
							{
								Result.push_back('\\');
								Result.push_back('"');
								break;
							}
						case '\n':
							{
								Result += "\\n";
								break;
							}
						case '\r':
							{
								Result += "\\r";
								break;
							}
						case '\t':
							{
								Result += "\\t";
								break;
							}
						case '\b':
							{
								Result += "\\b";
								break;
							}
						case '\f':
							{
								Result += "\\f";
								break;
							}
						case '`':
							{
								// Keep the result safe when embedded in a Markdown code span.
								Result += "\\x60";
								break;
							}
						default:
							{
								if (static_cast<unsigned char>(Ch) < 0x20u)
								{
									constexpr char Hex[] = "0123456789abcdef";
									const unsigned char Byte =
									    static_cast<unsigned char>(Ch);
									Result += "\\x";
									Result.push_back(Hex[(Byte >> 4) & 0x0fu]);
									Result.push_back(Hex[Byte & 0x0fu]);
								}
								else
								{
									Result.push_back(Ch);
								}
								break;
							}
						}
					}
					Result.push_back('"');
				}
			}
			Result.push_back(')');
		}
		return Result;
	}

	std::string FormatHostDocumentation(
	    const std::string_view Documentation,
	    const std::vector<std::string>& ParamNames,
	    const std::vector<std::string>& ParamDocumentation,
	    const std::string_view ReturnDocumentation)
	{
		return FormatHostDocumentation(
		    Documentation,
		    ParamNames,
		    ParamDocumentation,
		    ReturnDocumentation,
		    {},
		    {});
	}

	std::string FormatHostDocumentation(
	    const std::string_view Documentation,
	    const std::vector<std::string>& ParamNames,
	    const std::vector<std::string>& ParamDocumentation,
	    const std::string_view ReturnDocumentation,
	    const std::vector<FHostAnnotationInstance>& Annotations,
	    const std::vector<std::vector<FHostAnnotationInstance>>& ParamAnnotations)
	{
		std::string Result(Documentation);
		const std::string AnnotationText = FormatHostAnnotations(Annotations);
		if (!AnnotationText.empty())
		{
			if (!Result.empty())
			{
				Result += "\n\n";
			}
			Result += "Annotations: `";
			Result += AnnotationText;
			Result.push_back('`');
		}
		for (std::size_t Index = 0; Index < ParamDocumentation.size(); ++Index)
		{
			if (ParamDocumentation[Index].empty())
			{
				continue;
			}
			if (!Result.empty())
			{
				Result += "\n\n";
			}
			Result += "Parameter `";
			Result += Index >= ParamNames.size() || ParamNames[Index].empty()
			    ? ("#" + std::to_string(Index + 1))
			    : ParamNames[Index];
			Result += "`: ";
			Result += ParamDocumentation[Index];
		}
		for (std::size_t Index = 0; Index < ParamAnnotations.size(); ++Index)
		{
			const std::string ParamAnnotationText =
			    FormatHostAnnotations(ParamAnnotations[Index]);
			if (ParamAnnotationText.empty())
			{
				continue;
			}
			if (!Result.empty())
			{
				Result += "\n\n";
			}
			Result += "Parameter `";
			Result += Index >= ParamNames.size() || ParamNames[Index].empty()
			    ? ("#" + std::to_string(Index + 1))
			    : ParamNames[Index];
			Result += "` annotations: `";
			Result += ParamAnnotationText;
			Result.push_back('`');
		}
		if (!ReturnDocumentation.empty())
		{
			if (!Result.empty())
			{
				Result += "\n\n";
			}
			Result += "Returns: ";
			Result += ReturnDocumentation;
		}
		return Result;
	}
}
