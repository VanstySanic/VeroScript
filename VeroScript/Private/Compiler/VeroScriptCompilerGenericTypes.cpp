// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptCompilerTemplateSelection.h"

#include "VeroScriptAst.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptRuntime.h"
#include "Parser/VeroScriptParserDetail.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <tuple>
#include <unordered_set>

namespace VeroScript
{
	bool VeroScriptCompiler::TryScoreArgParamTypes(
	    const std::string& ArgTypeName,
	    const std::string& ParamTypeName,
	    int& OutScoreContribution,
	    const bool bAllowUserDefinedConversions) const
	{
		OutScoreContribution = 0;
		if (ArgTypeName.rfind("lvalue ", 0) == 0 || ArgTypeName.rfind("const lvalue ", 0) == 0 || ArgTypeName.rfind("rvalue ", 0) == 0)
		{
			const bool bConst = ArgTypeName.rfind("const lvalue ", 0) == 0;
			const bool bLvalue = ArgTypeName.rfind("rvalue ", 0) != 0;
			const std::string Base = ArgTypeName.substr(bConst ? 13 : 7);
			if (!Runtime::IsReferenceTypeName(ParamTypeName))
			{
				return TryScoreArgParamTypes(Base, ParamTypeName, OutScoreContribution,
				    bAllowUserDefinedConversions);
			}
			const bool bConstParam = Runtime::IsConstReferenceTypeName(ParamTypeName);
			if (!bConstParam && (!bLvalue || bConst)) return false;
			const std::string ParamBase(Runtime::StripReferenceTypeName(ParamTypeName));
			if (!bConstParam && CanonicalTypeNameForCoercion(Base) != CanonicalTypeNameForCoercion(ParamBase)) return false;
			int DirectScore = 0;
			if (bLvalue && TryScoreArgParamTypes((bConst ? "const ref " : "ref ") + Base, ParamTypeName, DirectScore, false))
			{
				OutScoreContribution = DirectScore == 15 ? 16 : DirectScore;
				return true;
			}
			// Only const references can bind a converted/temporary value.
			return bConstParam && TryScoreArgParamTypes(Base, ParamBase, OutScoreContribution, bAllowUserDefinedConversions);
		}
		const auto SplitRefPrefix =
		    [](std::string TypeName, bool& bIsRef, bool& bIsConstRef) -> std::string
		{
			bIsRef = false;
			bIsConstRef = false;
			if (TypeName.rfind("const ref ", 0) == 0)
			{
				bIsRef = true;
				bIsConstRef = true;
				return TypeName.substr(10);
			}
			if (TypeName.rfind("ref ", 0) == 0)
			{
				bIsRef = true;
				return TypeName.substr(4);
			}
			return TypeName;
		};

		bool bArgIsRef = false;
		bool bArgIsConstRef = false;
		bool bParamIsRef = false;
		bool bParamIsConstRef = false;
		std::string ArgBase = SplitRefPrefix(ArgTypeName, bArgIsRef, bArgIsConstRef);
		std::string ParamBase = SplitRefPrefix(ParamTypeName, bParamIsRef, bParamIsConstRef);
		if (bArgIsRef != bParamIsRef)
		{
			return false;
		}
		// const-ref argument cannot bind a mutable-ref parameter.
		if (bArgIsRef && bArgIsConstRef && !bParamIsConstRef)
		{
			return false;
		}

		ArgBase = CanonicalTypeNameForCoercion(NormalizeTypeAlias(ArgBase));
		ParamBase = CanonicalTypeNameForCoercion(NormalizeTypeAlias(ParamBase));
		if (ArgBase == ParamBase)
		{
			if (bArgIsRef && !bArgIsConstRef && bParamIsConstRef)
			{
				// Mutable `ref T` → `const ref T` (const-widening).
				OutScoreContribution = 15;
			}
			else
			{
				OutScoreContribution = 16;
			}
			return true;
		}
		if (IsSubtypeOf(ArgBase, ParamBase))
		{
			OutScoreContribution = 12;
			return true;
		}
		// References must bind existing storage. Numeric and user-defined
		// conversions create temporaries and are therefore never viable here.
		if (bParamIsRef)
		{
			return false;
		}
		if (ParamBase == "variant" && ArgBase != "nullptr" && ArgBase != "void" && !ArgBase.empty())
		{
			OutScoreContribution = 1;
			return true;
		}

		const auto IntegerInfo = [](const std::string& Type, bool& bUnsigned, int& Bits)
		{
			bUnsigned = false;
			Bits = 0;
			if (Type == "char")
			{
				Bits = 8;
			}
			else if (Type == "short")
			{
				Bits = 16;
			}
			else if (Type == "int")
			{
				Bits = 32;
			}
			else if (Type == "long" || Type == "int64")
			{
				Bits = 64;
			}
			else if (Type == "unsigned char")
			{
				bUnsigned = true;
				Bits = 8;
			}
			else if (Type == "unsigned short")
			{
				bUnsigned = true;
				Bits = 16;
			}
			else if (Type == "unsigned int")
			{
				bUnsigned = true;
				Bits = 32;
			}
			else if (Type == "unsigned long" || Type == "uint64")
			{
				bUnsigned = true;
				Bits = 64;
			}
			return Bits != 0;
		};
		const bool bArgNumeric = IsNumericTypeName(ArgBase);
		const bool bParamNumeric = IsNumericTypeName(ParamBase);
		if (bArgNumeric && bParamNumeric)
		{
			bool bArgUnsigned = false;
			bool bParamUnsigned = false;
			int ArgBits = 0;
			int ParamBits = 0;
			const bool bArgInteger = IntegerInfo(ArgBase, bArgUnsigned, ArgBits);
			const bool bParamInteger = IntegerInfo(ParamBase, bParamUnsigned, ParamBits);
			const bool bSafeInteger = bArgInteger && bParamInteger && ((bArgUnsigned == bParamUnsigned && ArgBits <= ParamBits) || (bArgUnsigned && !bParamUnsigned && ArgBits < ParamBits));
			const bool bSafeFloat = ArgBase == "float" && ParamBase == "double";
			const bool bSafeIntegerToFloat = bArgInteger && ((ParamBase == "float" && ArgBits <= 16) || (ParamBase == "double" && ArgBits <= 32));
			OutScoreContribution =
			    (bSafeInteger || bSafeFloat || bSafeIntegerToFloat) ? 10 : 6;
			return true;
		}
		if (CanAssignBetweenTypes(
		        ParamBase, ArgBase, bAllowUserDefinedConversions))
		{
			OutScoreContribution = 3;
			return true;
		}
		return false;
	}

	std::string VeroScriptCompiler::EncodeRefCallArgType(
	    const FExpr& Operand,
	    std::string BaseType) const
	{
		// Match CanonicalParamTypeNames: const/const-ref operands → "const ref T".
		if (IsConstAssignTarget(Operand))
		{
			return "const ref " + std::move(BaseType);
		}
		return "ref " + std::move(BaseType);
	}

	std::string VeroScriptCompiler::DisplayTypeName(std::string TypeName)
	{
		if (TypeName.rfind("@value:", 0) == 0) return FormatTemplateArgumentName(TypeName);
		const std::size_t GenericMarker = FindGenericTypeMarker(TypeName);
		if (GenericMarker != std::string::npos)
		{
			const std::vector<std::string> ConcreteTypes =
			    DecodeGenericConcreteTypes(TypeName);
			if (!ConcreteTypes.empty())
			{
				std::string Result = DisplayTypeName(TypeName.substr(0, GenericMarker)) + "<";
				for (std::size_t Index = 0; Index < ConcreteTypes.size(); ++Index)
				{
					if (Index > 0)
					{
						Result += ",";
					}
					Result += DisplayTypeName(ConcreteTypes[Index]);
				}
				return Result + ">";
			}
		}
		for (char& Character : TypeName)
		{
			if (Character == '\x1F')
			{
				Character = '/';
			}
		}
		return StripTypeScopeMarkers(std::move(TypeName));
	}

	std::string VeroScriptCompiler::SubstituteTypeName(const std::string& TypeName) const
	{
		return SubstituteTypeNameWith(TypeName, ActiveTypeSubst);
	}

	std::string VeroScriptCompiler::SubstituteTypeNameWith(
	    const std::string& TypeName,
	    const std::unordered_map<std::string, std::string>& Subst) const
	{
		// Extents use the same pure scalar evaluator as value arguments. An open
		// signature retains unresolved syntax until its binding environment closes.
		std::size_t ExtentOpening = std::string::npos, Angles = 0;
		for (std::size_t Index = 0; Index < TypeName.size(); ++Index)
		{
			if (TypeName[Index] == '<')
			{
				++Angles;
			}
			else if (TypeName[Index] == '>' && Angles != 0)
			{
				--Angles;
			}
			else if (TypeName[Index] == '[' && Angles == 0)
			{
				ExtentOpening = Index;
				break;
			}
		}
		if (ExtentOpening != std::string::npos && TypeName.back() == ']' && TypeName[ExtentOpening + 1] != ']')
		{
			std::string Result = SubstituteTypeNameWith(TypeName.substr(0, ExtentOpening), Subst);
			std::size_t Cursor = ExtentOpening;
			while (Cursor < TypeName.size() && TypeName[Cursor] == '[')
			{
				const auto End = TypeName.find(']', Cursor + 1);
				if (End == std::string::npos) return TypeName;
				const auto Expression = TypeName.substr(Cursor + 1, End - Cursor - 1);
				std::uint32_t CanonicalSize = 0;
				const auto Parsed = std::from_chars(Expression.data(), Expression.data() + Expression.size(), CanonicalSize);
				if (Parsed.ec == std::errc{} && Parsed.ptr == Expression.data() + Expression.size() && CanonicalSize != 0 && CanonicalSize <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
				{
					Result += '[' + std::to_string(CanonicalSize) + ']';
					Cursor = End + 1;
					continue;
				}
				const auto Value = EvaluateTemplateSource(Expression, CurrentModulePath, Subst, CurrentEmitLocation);
				std::string Extent = Expression;
				if (Value.Succeeded())
				{
					FTemplateValueType Integer;
					TryGetTemplateValueType("int", Integer);
					const auto Size = ConvertTemplateConstant(Value.Value, Integer, false, CurrentEmitLocation);
					if (!Size.Succeeded() || Value.Value.Type.bBoolean || Value.Value.Type.bEnum || Size.Value.Bits == 0 || Size.Value.Bits > std::numeric_limits<std::int32_t>::max())
					{
						ReportError("array extent is outside its positive integral range", CurrentEmitLocation);
						return {};
					}
					Extent = std::to_string(Size.Value.Bits);
				}
				Result += '[' + Extent + ']';
				Cursor = End + 1;
			}
			return Cursor == TypeName.size() ? Result : TypeName;
		}
		if (Subst.empty())
		{
			return TypeName;
		}
		const auto Exact = Subst.find(TypeName);
		if (Exact != Subst.end())
		{
			return Exact->second;
		}
		if (Runtime::IsReferenceTypeName(TypeName))
		{
			const std::string Prefix = Runtime::IsConstReferenceTypeName(TypeName)
			    ? "const ref "
			    : "ref ";
			return Prefix + SubstituteTypeNameWith(std::string(Runtime::StripReferenceTypeName(TypeName)), Subst);
		}

		std::string ElementType;
		if (Runtime::ParseDynamicArrayElementType(TypeName, ElementType))
		{
			return "array<" + SubstituteTypeNameWith(ElementType, Subst) + ">";
		}
		std::string KeyType;
		std::string ValueType;
		if (Runtime::SplitMapType(TypeName, KeyType, ValueType))
		{
			const std::string Prefix =
			    Runtime::IsMultiMapTypeName(TypeName) ? "multimap<" : "map<";
			return Prefix + SubstituteTypeNameWith(KeyType, Subst) + "," + SubstituteTypeNameWith(ValueType, Subst) + ">";
		}
		if (Runtime::SplitOrderedMapType(TypeName, KeyType, ValueType))
		{
			const std::string Prefix = Runtime::IsOrderedMultiMapTypeName(TypeName)
			    ? "ordered_multimap<"
			    : "ordered_map<";
			return Prefix + SubstituteTypeNameWith(KeyType, Subst) + "," + SubstituteTypeNameWith(ValueType, Subst) + ">";
		}
		if (Runtime::SplitSetElementType(TypeName, ElementType))
		{
			return "set<" + SubstituteTypeNameWith(ElementType, Subst) + ">";
		}
		if (Runtime::ParseMultiSetElementType(TypeName, ElementType))
		{
			const std::string Prefix = Runtime::IsOrderedMultiSetTypeName(TypeName)
			    ? "ordered_multiset<"
			    : "multiset<";
			return Prefix + SubstituteTypeNameWith(ElementType, Subst) + ">";
		}
		if (Runtime::ParseQueueElementType(TypeName, ElementType))
		{
			const std::string Prefix =
			    Runtime::IsPriorityQueueTypeName(TypeName) ? "priority_queue<" : "queue<";
			return Prefix + SubstituteTypeNameWith(ElementType, Subst) + ">";
		}
		if (Runtime::ParseStackElementType(TypeName, ElementType))
		{
			return "stack<" + SubstituteTypeNameWith(ElementType, Subst) + ">";
		}
		if (Runtime::ParseDequeElementType(TypeName, ElementType))
		{
			return "deque<" + SubstituteTypeNameWith(ElementType, Subst) + ">";
		}
		if (Runtime::IsChannelTypeName(TypeName))
		{
			const std::string ChannelElementType =
			    TypeName.substr(8, TypeName.size() - 9);
			return "channel<" + SubstituteTypeNameWith(ChannelElementType, Subst) + ">";
		}
		{
			std::vector<std::uint32_t> Lengths;
			std::string Current = TypeName;
			std::uint32_t Length = 0;
			while (Runtime::ParseNativeArrayType(Current, ElementType, Length))
			{
				Lengths.push_back(Length);
				Current = std::move(ElementType);
			}
			if (!Lengths.empty())
			{
				std::string Result = SubstituteTypeNameWith(Current, Subst);
				for (const std::uint32_t Dim : Lengths)
				{
					Result.push_back('[');
					Result.append(std::to_string(Dim));
					Result.push_back(']');
				}
				return Result;
			}
		}
		if (Runtime::IsFunctionTypeName(TypeName))
		{
			std::vector<std::string> ParamTypes;
			std::string ReturnType;
			if (Runtime::ParseFunctionType(TypeName, ParamTypes, ReturnType))
			{
				for (std::string& ParamType : ParamTypes)
				{
					ParamType = SubstituteTypeNameWith(ParamType, Subst);
				}
				return Runtime::MakeFunctionTypeName(
				    ParamTypes,
				    SubstituteTypeNameWith(ReturnType, Subst));
			}
		}
		std::string GenericBase;
		std::vector<std::string> GenericArgs;
		if (SplitUserGenericTypeApp(TypeName, GenericBase, GenericArgs))
		{
			std::string Result = GenericBase + "<";
			for (std::size_t Index = 0; Index < GenericArgs.size(); ++Index)
			{
				if (Index > 0)
				{
					Result += ",";
				}
				Result += SubstituteTypeNameWith(GenericArgs[Index], Subst);
			}
			return Result + ">";
		}
		return TypeName;
	}

	std::size_t VeroScriptCompiler::RequiredGenericTypeArgumentCount(
	    const std::vector<std::string>& TypeParams,
	    const std::vector<std::string>& TypeParamDefaults)
	{
		if (TypeParamDefaults.size() != TypeParams.size())
		{
			return TypeParams.size();
		}
		std::size_t Required = TypeParams.size();
		while (Required > 0 && !TypeParamDefaults[Required - 1].empty())
		{
			--Required;
		}
		return Required;
	}

	bool VeroScriptCompiler::AcceptsGenericTypeArgumentCount(
	    const std::vector<std::string>& TypeParams,
	    const std::vector<std::string>& TypeParamDefaults,
	    const std::size_t ProvidedCount)
	{
		return ProvidedCount >= RequiredGenericTypeArgumentCount(TypeParams, TypeParamDefaults) && ProvidedCount <= TypeParams.size();
	}

	bool VeroScriptCompiler::CompleteGenericTypeArguments(
	    const std::vector<std::string>& TypeParams,
	    const std::vector<std::string>& TypeParamDefaults,
	    const std::string& DeclarationModulePath,
	    std::vector<std::string>& InOutConcreteTypes,
	    const FSourceLocation& Location,
	    const std::string& DisplayName,
	    const FTemplateParameterList* Parameters, const bool TypesAreCanonical)
	{
		if (Parameters && !Parameters->IsEmpty())
		{
			return CompleteTemplateArguments(*Parameters, DeclarationModulePath, InOutConcreteTypes, Location, true, nullptr, TypesAreCanonical);
		}
		if (!AcceptsGenericTypeArgumentCount(
		        TypeParams,
		        TypeParamDefaults,
		        InOutConcreteTypes.size()))
		{
			const std::size_t Required =
			    RequiredGenericTypeArgumentCount(TypeParams, TypeParamDefaults);
			const std::string Expected = Required == TypeParams.size()
			    ? std::to_string(TypeParams.size())
			    : std::to_string(Required) + " to " + std::to_string(TypeParams.size());
			ReportError(
			    "wrong number of type arguments for '" + DisplayName + "': expected " + Expected + ", got " + std::to_string(InOutConcreteTypes.size()),
			    Location);
			return false;
		}
		if (InOutConcreteTypes.size() == TypeParams.size())
		{
			return true;
		}
		if (TypeParamDefaults.size() != TypeParams.size())
		{
			ReportError("invalid default type-argument metadata for '" + DisplayName + "'", Location);
			return false;
		}

		std::unordered_map<std::string, std::string> Subst;
		for (std::size_t Index = 0; Index < InOutConcreteTypes.size(); ++Index)
		{
			Subst.emplace(TypeParams[Index], InOutConcreteTypes[Index]);
		}
		const std::string PreviousModulePath = CurrentModulePath;
		CurrentModulePath = DeclarationModulePath;
		while (InOutConcreteTypes.size() < TypeParams.size())
		{
			const std::size_t Index = InOutConcreteTypes.size();
			if (TypeParamDefaults[Index].empty())
			{
				CurrentModulePath = PreviousModulePath;
				ReportError("missing required type argument for '" + DisplayName + "'", Location);
				return false;
			}
			std::string ConcreteType = SubstituteTypeNameWith(TypeParamDefaults[Index], Subst);
			ConcreteType = CanonicalTypeNameForCoercion(
			    QualifyTypeName(ConcreteType, DeclarationModulePath));
			if (ConcreteType.empty() || (ConcreteType != "void" && !ValidateFieldTypeName(ConcreteType, Location)))
			{
				CurrentModulePath = PreviousModulePath;
				return false;
			}
			InOutConcreteTypes.push_back(ConcreteType);
			Subst.emplace(TypeParams[Index], std::move(ConcreteType));
		}
		CurrentModulePath = PreviousModulePath;
		return true;
	}

	bool VeroScriptCompiler::CompleteGenericFunctionTypeArguments(
	    const FFunctionDecl& Function,
	    std::vector<std::string>& InOutConcreteTypes,
	    const FSourceLocation& Location)
	{
		if (!Function.TemplateParameters.IsEmpty())
		{
			return CompleteTemplateArguments(Function.TemplateParameters, Function.ModulePath,
			    InOutConcreteTypes, Location, true);
		}
		if (TemplateParameterNames(Function).empty() || InOutConcreteTypes.size() != TemplateParameterNames(Function).size() || (!TemplateParameterDefaults(Function).empty() && TemplateParameterDefaults(Function).size() != TemplateParameterNames(Function).size()))
		{
			ReportErrorId(
			    "diag.internal_error_generic_function_type_arity_mismatch",
			    Location);
			return false;
		}

		std::unordered_map<std::string, std::string> Subst;
		Subst.reserve(TemplateParameterNames(Function).size());
		const std::string PreviousModulePath = CurrentModulePath;
		CurrentModulePath = Function.ModulePath;
		for (std::size_t Index = 0; Index < TemplateParameterNames(Function).size(); ++Index)
		{
			std::string& ConcreteType = InOutConcreteTypes[Index];
			if (ConcreteType.empty())
			{
				const std::string DefaultType =
				    Index < TemplateParameterDefaults(Function).size()
				    ? TemplateParameterDefaults(Function)[Index]
				    : std::string{};
				if (DefaultType.empty())
				{
					CurrentModulePath = PreviousModulePath;
					ReportError(
					    "cannot infer generic parameter " + TemplateParameterNames(Function)[Index],
					    Location);
					return false;
				}
				ConcreteType = CanonicalTypeNameForCoercion(
				    QualifyTypeName(
				        SubstituteTypeNameWith(DefaultType, Subst),
				        Function.ModulePath));
				if (ConcreteType.empty() || (ConcreteType != "void" && !ValidateFieldTypeName(ConcreteType, Location)))
				{
					CurrentModulePath = PreviousModulePath;
					return false;
				}
			}
			Subst.emplace(TemplateParameterNames(Function)[Index], ConcreteType);
		}
		CurrentModulePath = PreviousModulePath;
		return true;
	}

	bool VeroScriptCompiler::EnsureGenericInstantiation(
	    const FFunctionDecl& Function,
	    const std::vector<std::string>& ConcreteTypes,
	    std::uint16_t& OutFunctionIndex,
	    const FSourceLocation& Location)
	{
		if (Function.bDeleted)
		{
			ReportErrorId(
			    "diag.use_of_deleted_declaration",
			    Location,
			    {TypeNameLeaf(Function.Name)});
			return false;
		}
		if (!Function.Body)
		{
			ReportError("template function has no definition: " + Function.Name, Location);
			return false;
		}

		const std::string BaseLinkageName =
		    GetSymbolLinkageName(Function.ModulePath, Function.bExported, Function.Name);
		// A specialization must retain the declaration's value-parameter identity;
		// type arguments alone collide for f<T>(string) / f<T>(string, int).
		std::unordered_map<std::string, std::string> Parameters;
		for (std::size_t I = 0; I < TemplateParameterNames(Function).size(); ++I)
		{
			Parameters.emplace(TemplateParameterNames(Function)[I], "@type_parameter_" + std::to_string(I));
		}
		const std::string MangledName = MangleGenericInstanceName(BaseLinkageName, ConcreteTypes) + EncodeMethodParamSuffix(CanonicalParamTypeNames(Function.Parameters, Function.ModulePath, Parameters));
		const auto Existing = Module->FunctionIndices.find(MangledName);
		if (Existing != Module->FunctionIndices.end())
		{
			OutFunctionIndex = Existing->second;
			return true;
		}

		if (InstantiatingGenerics.size() >= 64)
		{
			ReportError("template instantiation nesting limit exceeded", Location);
			return false;
		}
		if (InstantiatingGenerics.find(MangledName) != InstantiatingGenerics.end())
		{
			ReportError("recursive generic instantiation: " + MangledName, Location);
			return false;
		}

		if (TemplateParameterNames(Function).empty() || TemplateParameterNames(Function).size() != ConcreteTypes.size())
		{
			ReportErrorId("diag.internal_error_generic_function_type_arity_mismatch", Location);
			return false;
		}
		if (!CheckGenericFunctionBounds(Function, ConcreteTypes, Location))
		{
			return false;
		}

		InstantiatingGenerics.insert(MangledName);
		const std::unordered_map<std::string, std::string> PreviousSubst = ActiveTypeSubst;
		// A free function's body cannot capture the caller's template parameters.
		ActiveTypeSubst.clear();
		for (std::size_t Index = 0; Index < TemplateParameterNames(Function).size(); ++Index)
		{
			ActiveTypeSubst[TemplateParameterNames(Function)[Index]] = ConcreteTypes[Index];
		}

		const bool bCompiled = CompileFunction(
		    MangledName,
		    Function.ReturnType,
		    Function.Parameters,
		    *Function.Body,
		    false,
		    0,
		    Function.ModulePath,
		    Location,
		    false,
		    nullptr,
		    false,
		    &Function.DeclMeta, nullptr, false, Function.bConstexpr);

		ActiveTypeSubst = PreviousSubst;
		InstantiatingGenerics.erase(MangledName);

		if (!bCompiled)
		{
			return false;
		}

		const auto Instantiated = Module->FunctionIndices.find(MangledName);
		if (Instantiated == Module->FunctionIndices.end())
		{
			ReportError("failed to instantiate generic: " + MangledName, Location);
			return false;
		}
		OutFunctionIndex = Instantiated->second;
		return true;
	}

	bool VeroScriptCompiler::ValidateTypeParamBoundDeclarations(
	    const std::vector<std::string>& TypeParams,
	    const std::vector<FTypeParamBound>& Bounds,
	    const std::string& ModulePath,
	    const FSourceLocation& Location,
	    const char* GenericKindNoun, const FTemplateParameterList* Parameters)
	{
		if (Bounds.empty())
		{
			return true;
		}
		if (TypeParams.empty())
		{
			ReportError(
			    std::string("'where' clause requires a generic ") + GenericKindNoun,
			    Location);
			return false;
		}

		auto MentionsDeclTypeParam = [&TypeParams, Parameters](const std::string& TypeName) -> bool
		{
			for (const std::string& TypeParam : Parameters && !Parameters->IsEmpty() ? Parameters->GetNames() : TypeParams)
			{
				if (ParserDetail::TypeMentionsParam(TypeName, TypeParam))
				{
					return true;
				}
			}
			return false;
		};

		const std::string PreviousModulePath = CurrentModulePath;
		CurrentModulePath = ModulePath;
		for (const FTypeParamBound& Bound : Bounds)
		{
			if (std::find(TypeParams.begin(), TypeParams.end(), Bound.TypeParam) == TypeParams.end())
			{
				CurrentModulePath = PreviousModulePath;
				ReportError(
				    "unknown type parameter in 'where' clause: " + Bound.TypeParam,
				    Location);
				return false;
			}

			// Resolve aliases in the declaration module before looking up the
			// interface. Instantiation already performs this normalization; using
			// the written alias here incorrectly rejected the same valid bound.
			const std::string NormalizedBound = NormalizeTypeAlias(Bound.InterfaceTypeName, ModulePath);
			if (bHasError)
			{
				CurrentModulePath = PreviousModulePath;
				return false;
			}
			std::string BaseName;
			std::vector<std::string> ArgTypes;
			const bool bTypeApp = SplitUserGenericTypeApp(NormalizedBound, BaseName, ArgTypes);
			const std::string LookupName = bTypeApp ? BaseName : NormalizedBound;
			const FInterfaceDecl* const InterfaceDecl =
			    FindGenericInterfaceDecl(LookupName, ModulePath, ArgTypes.size());
			if (!InterfaceDecl)
			{
				CurrentModulePath = PreviousModulePath;
				ReportError(
				    "type bound must be an interface: " + DisplayTypeName(Bound.InterfaceTypeName),
				    Location);
				return false;
			}
			if (!TemplateParameterNames(*InterfaceDecl).empty())
			{
				if (!bTypeApp)
				{
					CurrentModulePath = PreviousModulePath;
					ReportError(
					    "generic interface requires type arguments: " + DisplayTypeName(Bound.InterfaceTypeName),
					    Location);
					return false;
				}
				bool Dependent = false;
				for (std::size_t Index = 0; Index < ArgTypes.size(); ++Index)
				{
					const auto& ArgType = ArgTypes[Index];
					if (MentionsDeclTypeParam(ArgType))
					{
						Dependent = true;
						continue;
					}
					const bool Value = !InterfaceDecl->TemplateParameters.IsEmpty() && Index < InterfaceDecl->TemplateParameters.GetParameters().size() && InterfaceDecl->TemplateParameters.GetParameters()[Index].Kind == ETemplateParameterKind::Value;
					if (Value)
					{
						const auto Result = EvaluateTemplateSource(ArgType, ModulePath, ActiveTypeSubst, Location);
						if (!Result.Succeeded())
						{
							CurrentModulePath = PreviousModulePath;
							ReportError("invalid value argument in interface bound: " + Result.Reason, Location);
							return false;
						}
					}
					else if (ArgType != "void" && !ValidateFieldTypeName(ArgType, Location))
					{
						CurrentModulePath = PreviousModulePath;
						return false;
					}
				}
				if (!Dependent && !InterfaceDecl->TemplateParameters.IsEmpty())
				{
					auto Arguments = ArgTypes;
					if (!CompleteTemplateArguments(InterfaceDecl->TemplateParameters, InterfaceDecl->ModulePath, Arguments, Location, true))
					{
						CurrentModulePath = PreviousModulePath;
						return false;
					}
				}
			}
			else if (bTypeApp)
			{
				CurrentModulePath = PreviousModulePath;
				ReportErrorId("diag.type_is_not_generic", Location, {BaseName});
				return false;
			}
		}
		CurrentModulePath = PreviousModulePath;
		return true;
	}

	bool VeroScriptCompiler::CheckTypeParamBounds(
	    const std::vector<std::string>& TypeParams,
	    const std::vector<FTypeParamBound>& Bounds,
	    const std::vector<std::string>& ConcreteTypes,
	    const std::string& ModulePath,
	    const FSourceLocation& Location,
	    const char* GenericKindNoun)
	{
		if (Bounds.empty())
		{
			return true;
		}
		if (!ValidateTypeParamBoundDeclarations(
		        TypeParams,
		        Bounds,
		        ModulePath,
		        Location,
		        GenericKindNoun))
		{
			return false;
		}
		if (TypeParams.size() != ConcreteTypes.size())
		{
			ReportErrorId("diag.internal_error_generic_type_arity_mismatch", Location, {GenericKindNoun});
			return false;
		}

		std::unordered_map<std::string, std::string> LocalSubst;
		LocalSubst.reserve(TypeParams.size());
		for (std::size_t Index = 0; Index < TypeParams.size(); ++Index)
		{
			LocalSubst[TypeParams[Index]] = ConcreteTypes[Index];
		}

		const std::string PreviousModulePath = CurrentModulePath;
		CurrentModulePath = ModulePath;
		for (const FTypeParamBound& Bound : Bounds)
		{
			std::size_t ParamIndex = static_cast<std::size_t>(-1);
			for (std::size_t Index = 0; Index < TypeParams.size(); ++Index)
			{
				if (TypeParams[Index] == Bound.TypeParam)
				{
					ParamIndex = Index;
					break;
				}
			}
			if (ParamIndex == static_cast<std::size_t>(-1))
			{
				CurrentModulePath = PreviousModulePath;
				ReportError(
				    "unknown type parameter in 'where' clause: " + Bound.TypeParam,
				    Location);
				return false;
			}
			const std::string& Concrete = ConcreteTypes[ParamIndex];
			const std::string BoundAsWritten = SubstituteTypeNameWith(
			    Bound.InterfaceTypeName,
			    LocalSubst);
			std::string ResolvedIface;
			if (!EnsureInterfaceTypeResolved(
			        QualifyTypeName(
			            NormalizeTypeAlias(BoundAsWritten, ModulePath),
			            ModulePath),
			        ResolvedIface,
			        nullptr,
			        Location))
			{
				CurrentModulePath = PreviousModulePath;
				if (!bHasError)
				{
					ReportError(
					    "type bound must be an interface: " + DisplayTypeName(Bound.InterfaceTypeName),
					    Location);
				}
				return false;
			}
			const std::string ConcreteCanonical = CanonicalTypeNameForCoercion(
			    QualifyTypeName(
			        NormalizeTypeAlias(Concrete, PreviousModulePath),
			        PreviousModulePath));
			const std::string InterfaceCanonical = CanonicalTypeNameForCoercion(ResolvedIface);
			if (!IsSubtypeOf(ConcreteCanonical, InterfaceCanonical))
			{
				std::string WhereClause = "where " + Bound.TypeParam + " : ";
				bool bFirstBoundForParam = true;
				for (const FTypeParamBound& Sibling : Bounds)
				{
					if (Sibling.TypeParam != Bound.TypeParam)
					{
						continue;
					}
					if (!bFirstBoundForParam)
					{
						WhereClause += ", ";
					}
					bFirstBoundForParam = false;
					WhereClause += DisplayTypeName(Sibling.InterfaceTypeName);
				}
				CurrentModulePath = PreviousModulePath;
				ReportErrorId(
				    "diag.type_argument_does_not_satisfy_failed_bound",
				    Location,
				    {
				        DisplayTypeName(Concrete),
				        WhereClause,
				        DisplayTypeName(Bound.InterfaceTypeName),
				    });
				return false;
			}
		}
		CurrentModulePath = PreviousModulePath;
		return true;
	}

	bool VeroScriptCompiler::CheckGenericFunctionBounds(
	    const FFunctionDecl& Function,
	    const std::vector<std::string>& ConcreteTypes,
	    const FSourceLocation& Location)
	{
		std::unordered_map<std::string, std::string> Bindings;
		for (std::size_t I = 0; I < TemplateParameterNames(Function).size() && I < ConcreteTypes.size(); ++I)
		{
			Bindings.emplace(TemplateParameterNames(Function)[I], ConcreteTypes[I]);
		}
		if (Function.Body && !Function.Body->CallableOwner.empty() && !CheckScopedTypeRequirements({}, Function.Body->CallableOwner, Bindings))
		{
			return false;
		}
		return CheckTypeParamBounds(
		    TemplateParameterNames(Function),
		    Function.Bounds,
		    ConcreteTypes,
		    Function.ModulePath,
		    Location,
		    "function");
	}

	bool VeroScriptCompiler::ValidateGenericFunctionBoundDeclarations(
	    const FFunctionDecl& Function,
	    const FSourceLocation& Location)
	{
		return ValidateTypeParamBoundDeclarations(
		    TemplateParameterNames(Function),
		    Function.Bounds,
		    Function.ModulePath,
		    Location,
		    "function", &Function.TemplateParameters);
	}

	bool VeroScriptCompiler::SplitUserGenericTypeApp(
	    const std::string& TypeName,
	    std::string& OutBaseName,
	    std::vector<std::string>& OutArgTypes) const
	{
		if (TypeName.empty() || Runtime::IsDynamicArrayTypeName(TypeName) || Runtime::IsCommonContainerTypeName(TypeName) || Runtime::IsNativeArrayTypeName(TypeName) || Runtime::IsScriptArgsArrayTypeName(TypeName))
		{
			return false;
		}
		const std::size_t Open = TypeName.find('<');
		if (Open == std::string::npos || Open == 0 || TypeName.back() != '>')
		{
			return false;
		}
		// A mangled instance may itself contain a source-spelled generic argument,
		// e.g. `Box#10:array<int>`. Its `<...>` belongs to the length-encoded
		// argument, not to a new outer type application.
		const std::size_t Marker = TypeName.find('#');
		if (Marker != std::string::npos && Marker < Open)
		{
			return false;
		}
		int Depth = 0, Parentheses = 0, Brackets = 0;
		char Quote = 0;
		for (std::size_t Index = Open; Index < TypeName.size(); ++Index)
		{
			const char Character = TypeName[Index];
			if (Quote)
			{
				if (Character == '\\')
				{
					++Index;
				}
				else if (Character == Quote)
				{
					Quote = 0;
				}
				continue;
			}
			if (Character == '\'' || Character == '"')
			{
				Quote = Character;
				continue;
			}
			if (Character == '(')
			{
				++Parentheses;
				continue;
			}
			if (Character == ')')
			{
				if (Parentheses-- == 0) return false;
				continue;
			}
			if (Character == '[')
			{
				++Brackets;
				continue;
			}
			if (Character == ']')
			{
				if (Brackets-- == 0) return false;
				continue;
			}
			if (Parentheses || Brackets)
			{
				continue;
			}
			if (Character == '<')
			{
				++Depth;
			}
			else if (TypeName[Index] == '>' && (Index == 0 || TypeName[Index - 1] != '-'))
			{
				--Depth;
				if (Depth == 0 && Index + 1 != TypeName.size())
				{
					return false;
				}
			}
		}
		if (Depth != 0 || Parentheses != 0 || Brackets != 0 || Quote != 0)
		{
			return false;
		}
		OutBaseName = TrimNativeTypeName(std::string_view(TypeName).substr(0, Open));
		OutArgTypes.clear();
		if (Open + 2 < TypeName.size())
		{
			OutArgTypes = SplitTopLevelDelimited(
			    TypeName.substr(Open + 1, TypeName.size() - Open - 2), ',');
			for (auto& Argument : OutArgTypes)
			{
				const auto First = Argument.find_first_not_of(" \t\r\n");
				const auto Last = Argument.find_last_not_of(" \t\r\n");
				if (First == std::string::npos)
				{
					Argument.clear();
				}
				else
				{
					if (Last + 1 != Argument.size())
					{
						Argument.erase(Last + 1);
					}
					if (First != 0)
					{
						Argument.erase(0, First);
					}
				}
			}
		}
		return !OutBaseName.empty() && std::all_of(OutArgTypes.begin(), OutArgTypes.end(), [](const std::string& Arg)
		{
			return !Arg.empty();
		});
	}

	std::vector<std::string> VeroScriptCompiler::SplitTopLevelDelimited(
	    const std::string& Text,
	    const char Delimiter)
	{
		std::vector<std::string> Parts;
		std::size_t PartStart = 0;
		int Depth = 0;
		int ParenthesisDepth = 0;
		int BracketDepth = 0;
		char Quote = 0;
		for (std::size_t Index = 0; Index < Text.size(); ++Index)
		{
			const char Character = Text[Index];
			if (Quote)
			{
				if (Character == '\\')
				{
					++Index;
				}
				else if (Character == Quote)
				{
					Quote = 0;
				}
				continue;
			}
			if (Character == '\'' || Character == '"')
			{
				Quote = Character;
				continue;
			}
			if (Character == '(')
			{
				++ParenthesisDepth;
				continue;
			}
			if (Character == ')')
			{
				if (ParenthesisDepth > 0)
				{
					--ParenthesisDepth;
				}
				continue;
			}
			if (Character == '[')
			{
				++BracketDepth;
				continue;
			}
			if (Character == ']')
			{
				if (BracketDepth > 0)
				{
					--BracketDepth;
				}
				continue;
			}
			if (ParenthesisDepth || BracketDepth)
			{
				continue;
			}
			if (Character == '<')
			{
				++Depth;
			}
			else if (Text[Index] == '>' && (Index == 0 || Text[Index - 1] != '-'))
			{
				--Depth;
			}
			else if (Text[Index] == Delimiter && Depth == 0)
			{
				Parts.push_back(Text.substr(PartStart, Index - PartStart));
				PartStart = Index + 1;
			}
		}
		Parts.push_back(Text.substr(PartStart));
		return Parts;
	}

	std::vector<std::string> VeroScriptCompiler::DecodeGenericConcreteTypes(
	    const std::string& InstantiatedLinkageName) const
	{
		const std::size_t Marker = FindGenericTypeMarker(InstantiatedLinkageName);
		if (Marker == std::string::npos || Marker + 1 >= InstantiatedLinkageName.size())
		{
			return {};
		}
		std::vector<std::string> ConcreteTypes;
		std::size_t Position = Marker + 1;
		while (Position < InstantiatedLinkageName.size())
		{
			const std::size_t LengthStart = Position;
			while (Position < InstantiatedLinkageName.size() && InstantiatedLinkageName[Position] >= '0' && InstantiatedLinkageName[Position] <= '9')
			{
				++Position;
			}
			if (Position == LengthStart || Position >= InstantiatedLinkageName.size() || InstantiatedLinkageName[Position] != ':')
			{
				return {};
			}
			std::size_t Length = 0;
			const char* const Begin = InstantiatedLinkageName.data() + LengthStart;
			const char* const End = InstantiatedLinkageName.data() + Position;
			const auto Parsed = std::from_chars(Begin, End, Length);
			if (Parsed.ec != std::errc{} || Parsed.ptr != End)
			{
				return {};
			}
			++Position;
			if (Length > InstantiatedLinkageName.size() - Position)
			{
				return {};
			}
			ConcreteTypes.push_back(InstantiatedLinkageName.substr(Position, Length));
			Position += Length;
		}
		return ConcreteTypes;
	}

	std::string VeroScriptCompiler::MangleGenericInstanceName(
	    const std::string& BaseLinkageName,
	    const std::vector<std::string>& ConcreteTypes) const
	{
		std::string Result = BaseLinkageName;
		if (!ConcreteTypes.empty())
		{
			Result += "#";
		}
		for (const std::string& ConcreteType : ConcreteTypes)
		{
			Result += std::to_string(ConcreteType.size());
			Result += ":";
			Result += ConcreteType;
		}
		return Result;
	}

	bool VeroScriptCompiler::TryBuildGenericOwnerSubst(
	    const std::string& OwnerTypeName,
	    std::unordered_map<std::string, std::string>& OutSubst,
	    const FSourceLocation&) const
	{
		OutSubst.clear();
		const std::string InstantiatedOwner =
		    FindGenericTypeMarker(OwnerTypeName) != std::string::npos
		    ? OwnerTypeName
		    : QualifyTypeName(OwnerTypeName, CurrentModulePath);
		const std::string TemplateBase = GenericTemplateBaseLinkage(InstantiatedOwner);
		if (TemplateBase.empty())
		{
			return true;
		}

		// Closed identity fixes declaration arity; defaults cannot widen it again.
		std::vector<std::string> ConcreteTypes =
		    DecodeGenericConcreteTypes(InstantiatedOwner);
		const std::vector<std::string>* TypeParams = nullptr;
		const std::vector<std::string>* TypeParamDefaults = nullptr;
		const std::string* DeclarationModulePath = nullptr;
		for (const FStructDecl& StructDecl : ActiveProgram->Structs)
		{
			if (GetSymbolLinkageName(
			        StructDecl.ModulePath,
			        StructDecl.bExported,
			        StructDecl.Name) == TemplateBase &&
			    (TemplateParameterNames(StructDecl).size() == ConcreteTypes.size()))
			{
				TypeParams = &TemplateParameterNames(StructDecl);
				TypeParamDefaults = &TemplateParameterDefaults(StructDecl);
				DeclarationModulePath = &StructDecl.ModulePath;
				break;
			}
		}
		if (!TypeParams)
		{
			for (const FClassDecl& ClassDecl : ActiveProgram->Classes)
			{
				if (GetSymbolLinkageName(
				        ClassDecl.ModulePath,
				        ClassDecl.bExported,
				        ClassDecl.Name) == TemplateBase &&
				    (TemplateParameterNames(ClassDecl).size() == ConcreteTypes.size()))
				{
					TypeParams = &TemplateParameterNames(ClassDecl);
					TypeParamDefaults = &TemplateParameterDefaults(ClassDecl);
					DeclarationModulePath = &ClassDecl.ModulePath;
					break;
				}
			}
		}
		if (!TypeParams)
		{
			for (const FInterfaceDecl& InterfaceDecl : ActiveProgram->Interfaces)
			{
				if (GetSymbolLinkageName(
				        InterfaceDecl.ModulePath,
				        InterfaceDecl.bExported,
				        InterfaceDecl.Name) == TemplateBase &&
				    (TemplateParameterNames(InterfaceDecl).size() == ConcreteTypes.size()))
				{
					TypeParams = &TemplateParameterNames(InterfaceDecl);
					TypeParamDefaults = &TemplateParameterDefaults(InterfaceDecl);
					DeclarationModulePath = &InterfaceDecl.ModulePath;
					break;
				}
			}
		}
		if (!TypeParams)
		{
			for (const FStructDecl& StructDecl : BuiltinStructTemplates)
			{
				if (GetSymbolLinkageName(
				        StructDecl.ModulePath,
				        StructDecl.bExported,
				        StructDecl.Name) == TemplateBase &&
				    (TemplateParameterNames(StructDecl).size() == ConcreteTypes.size()))
				{
					TypeParams = &TemplateParameterNames(StructDecl);
					TypeParamDefaults = &TemplateParameterDefaults(StructDecl);
					DeclarationModulePath = &StructDecl.ModulePath;
					break;
				}
			}
		}
		if (!TypeParams)
		{
			for (const FClassDecl& ClassDecl : BuiltinClassTemplates)
			{
				if (GetSymbolLinkageName(
				        ClassDecl.ModulePath,
				        ClassDecl.bExported,
				        ClassDecl.Name) == TemplateBase &&
				    (TemplateParameterNames(ClassDecl).size() == ConcreteTypes.size()))
				{
					TypeParams = &TemplateParameterNames(ClassDecl);
					TypeParamDefaults = &TemplateParameterDefaults(ClassDecl);
					DeclarationModulePath = &ClassDecl.ModulePath;
					break;
				}
			}
		}
		if (!TypeParams || !TypeParamDefaults || !DeclarationModulePath)
		{
			return false;
		}
		if (ConcreteTypes.size() < TypeParams->size())
		{
			if (TypeParamDefaults->size() != TypeParams->size())
			{
				return false;
			}
			std::unordered_map<std::string, std::string> DefaultSubst;
			for (std::size_t Index = 0; Index < ConcreteTypes.size(); ++Index)
			{
				DefaultSubst.emplace((*TypeParams)[Index], ConcreteTypes[Index]);
			}
			while (ConcreteTypes.size() < TypeParams->size())
			{
				const std::size_t Index = ConcreteTypes.size();
				std::string ConcreteType = SubstituteTypeNameWith(
				    (*TypeParamDefaults)[Index],
				    DefaultSubst);
				ConcreteType = CanonicalTypeNameForCoercion(
				    QualifyTypeName(ConcreteType, *DeclarationModulePath));
				ConcreteTypes.push_back(ConcreteType);
				DefaultSubst.emplace((*TypeParams)[Index], std::move(ConcreteType));
			}
		}
		for (std::size_t Index = 0; Index < TypeParams->size(); ++Index)
		{
			OutSubst[(*TypeParams)[Index]] = ConcreteTypes[Index];
		}
		return true;
	}

	bool VeroScriptCompiler::ApplyTypeParamSubst(
	    const std::vector<std::string>& TypeParams,
	    const std::vector<std::string>& ConcreteTypes,
	    std::unordered_map<std::string, std::string>& InOutSubst,
	    const FSourceLocation& Location)
	{
		if (TypeParams.size() != ConcreteTypes.size())
		{
			ReportErrorId(
			    "diag.wrong_number_of_type_arguments_expected_got",
			    Location,
			    {
			        std::to_string(TypeParams.size()),
			        std::to_string(ConcreteTypes.size()),
			    });
			return false;
		}
		for (std::size_t Index = 0; Index < TypeParams.size(); ++Index)
		{
			InOutSubst[TypeParams[Index]] = ConcreteTypes[Index];
		}
		return true;
	}

	std::string VeroScriptCompiler::GenericTemplateBaseLinkage(const std::string& TypeName) const
	{
		const std::size_t HashPos = FindGenericTypeMarker(TypeName);
		if (HashPos == std::string::npos || HashPos == 0)
		{
			return {};
		}
		return TypeName.substr(0, HashPos);
	}

	const FStructDecl* VeroScriptCompiler::FindGenericStructDecl(
	    const std::string& SimpleName, const std::string& ReferenceModulePath, const std::size_t TypeParamArity) const
	{
		if (!ActiveProgram || SimpleName.empty()) return nullptr;
		const std::string_view Lookup = SimpleName.rfind("::", 0) == 0
		    ? std::string_view(SimpleName).substr(2)
		    : std::string_view(SimpleName);
		const auto Selection = CompilerDetail::SelectTemplateTypeDeclaration(*ActiveProgram, Lookup,
		    ReferenceModulePath, TypeParamArity, [&](const auto& Module, const bool Exported)
		{
			return CanAccessSymbol(Module, Exported, ReferenceModulePath);
		}, &FindDeclarationCandidates(SourceStructNames, std::string(Lookup)), &FindDeclarationCandidates(SourceClassNames, std::string(Lookup)), &FindDeclarationCandidates(SourceInterfaceNames, std::string(Lookup)), &FindDeclarationCandidates(SourceEnumNames, std::string(Lookup)));

		if (!Selection.Local)
		{
			for (const auto& Declaration : BuiltinStructTemplates)
			{
				if (Declaration.Name == Lookup && AcceptsGenericTypeArgumentCount(TemplateParameterNames(Declaration), TemplateParameterDefaults(Declaration), TypeParamArity))
				{
					return &Declaration;
				}
			}
		}

		if (Selection.Ambiguous)
		{
			ReportErrorId("diag.template.type_arguments_ambiguous", CurrentEmitLocation, {SimpleName});
			return nullptr;
		}
		return Selection.Struct;
	}

	const FClassDecl* VeroScriptCompiler::FindGenericClassDecl(
	    const std::string& SimpleName, const std::string& ReferenceModulePath, const std::size_t TypeParamArity) const
	{
		if (!ActiveProgram || SimpleName.empty()) return nullptr;
		const std::string_view Lookup = SimpleName.rfind("::", 0) == 0
		    ? std::string_view(SimpleName).substr(2)
		    : std::string_view(SimpleName);
		const auto Selection = CompilerDetail::SelectTemplateTypeDeclaration(*ActiveProgram, Lookup,
		    ReferenceModulePath, TypeParamArity, [&](const auto& Module, const bool Exported)
		{
			return CanAccessSymbol(Module, Exported, ReferenceModulePath);
		}, &FindDeclarationCandidates(SourceStructNames, std::string(Lookup)), &FindDeclarationCandidates(SourceClassNames, std::string(Lookup)), &FindDeclarationCandidates(SourceInterfaceNames, std::string(Lookup)), &FindDeclarationCandidates(SourceEnumNames, std::string(Lookup)));

		if (!Selection.Local)
		{
			for (const auto& Declaration : BuiltinClassTemplates)
			{
				if (Declaration.Name == Lookup && AcceptsGenericTypeArgumentCount(TemplateParameterNames(Declaration), TemplateParameterDefaults(Declaration), TypeParamArity))
				{
					return &Declaration;
				}
			}
		}

		if (Selection.Ambiguous)
		{
			ReportErrorId("diag.template.type_arguments_ambiguous", CurrentEmitLocation, {SimpleName});
			return nullptr;
		}
		return Selection.Class;
	}

}
