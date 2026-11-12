// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptCompilerVariantInternal.h"
#include "VeroScriptCompilerTemplateOrdering.h"
#include "VeroScriptCompilerTemplateBinding.h"

#include "VeroScriptAst.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptStandardLibrary.h"

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
	bool VeroScriptCompiler::TryMatchTypeParam(
	    const std::string& ParamType,
	    const std::string& ArgType,
	    const std::string& TypeParam,
	    std::string& OutConcrete) const
	{
		OutConcrete.clear();
		if (ParamType == TypeParam)
		{
			OutConcrete = ArgType;
			return !OutConcrete.empty();
		}

		const auto MatchNested = [&](const std::string& NestedParam, const std::string& NestedArg)
		{
			return TryMatchTypeParam(NestedParam, NestedArg, TypeParam, OutConcrete);
		};

		std::string ParamElement;
		std::string ArgElement;
		if (Runtime::ParseDynamicArrayElementType(ParamType, ParamElement))
		{
			return Runtime::ParseDynamicArrayElementType(ArgType, ArgElement) && MatchNested(ParamElement, ArgElement);
		}
		if (Runtime::ParseQueueElementType(ParamType, ParamElement))
		{
			return Runtime::ParseQueueElementType(ArgType, ArgElement) && MatchNested(ParamElement, ArgElement);
		}
		if (Runtime::ParseStackElementType(ParamType, ParamElement))
		{
			return Runtime::ParseStackElementType(ArgType, ArgElement) && MatchNested(ParamElement, ArgElement);
		}
		if (Runtime::SplitSetElementType(ParamType, ParamElement))
		{
			return Runtime::SplitSetElementType(ArgType, ArgElement) && MatchNested(ParamElement, ArgElement);
		}
		if (Runtime::ParseDequeElementType(ParamType, ParamElement))
		{
			return Runtime::ParseDequeElementType(ArgType, ArgElement) && MatchNested(ParamElement, ArgElement);
		}

		std::string ParamKey;
		std::string ParamValue;
		std::string ArgKey;
		std::string ArgValue;
		const auto MatchMapParts = [&](const bool bOrdered)
		{
			const bool bParamParsed = bOrdered
			    ? Runtime::SplitOrderedMapType(ParamType, ParamKey, ParamValue)
			    : Runtime::SplitMapType(ParamType, ParamKey, ParamValue);
			if (!bParamParsed)
			{
				return false;
			}
			const bool bArgParsed = bOrdered
			    ? Runtime::SplitOrderedMapType(ArgType, ArgKey, ArgValue)
			    : Runtime::SplitMapType(ArgType, ArgKey, ArgValue);
			if (!bArgParsed)
			{
				return false;
			}
			std::string KeyConcrete;
			std::string ValueConcrete;
			const bool bKeyMatched =
			    TryMatchTypeParam(ParamKey, ArgKey, TypeParam, KeyConcrete);
			const bool bValueMatched =
			    TryMatchTypeParam(ParamValue, ArgValue, TypeParam, ValueConcrete);
			if (!bKeyMatched && !bValueMatched)
			{
				return false;
			}
			if (bKeyMatched && bValueMatched && KeyConcrete != ValueConcrete)
			{
				return false;
			}
			OutConcrete = bKeyMatched ? KeyConcrete : ValueConcrete;
			return true;
		};
		if (Runtime::IsMapTypeName(ParamType))
		{
			return MatchMapParts(false);
		}
		if (Runtime::IsOrderedMapTypeName(ParamType))
		{
			return MatchMapParts(true);
		}

		const auto SplitExtents = [](const std::string& Name, std::string& Element, std::vector<std::string>& Extents)
		{
			std::size_t Angles = 0, Begin = std::string::npos;
			for (std::size_t Index = 0; Index < Name.size(); ++Index)
			{
				if (Name[Index] == '<')
				{
					++Angles;
				}
				else if (Name[Index] == '>' && Angles != 0)
				{
					--Angles;
				}
				else if (Name[Index] == '[' && Angles == 0)
				{
					Begin = Index;
					break;
				}
			}
			if (Begin == std::string::npos || Name.back() != ']') return false;
			Element = Name.substr(0, Begin);
			while (Begin < Name.size() && Name[Begin] == '[')
			{
				const auto End = Name.find(']', Begin + 1);
				if (End == std::string::npos || End == Begin + 1) return false;
				Extents.push_back(Name.substr(Begin + 1, End - Begin - 1));
				Begin = End + 1;
			}
			return Begin == Name.size();
		};
		std::vector<std::string> ParamExtents, ArgExtents;
		if (SplitExtents(ParamType, ParamElement, ParamExtents))
		{
			if (!SplitExtents(ArgType, ArgElement, ArgExtents) || ParamExtents.size() != ArgExtents.size()) return false;
			std::string Candidate;
			if (TryMatchTypeParam(ParamElement, ArgElement, TypeParam, Candidate))
			{
				OutConcrete = Candidate;
			}
			for (std::size_t Index = 0; Index < ParamExtents.size(); ++Index)
			{
				if (ParamExtents[Index] == TypeParam)
				{
					if (!OutConcrete.empty() && OutConcrete != ArgExtents[Index]) return false;
					OutConcrete = ArgExtents[Index];
				}
			}
			return !OutConcrete.empty();
		}
		std::uint32_t ParamLength = 0;
		std::uint32_t ArgLength = 0;
		if (Runtime::ParseNativeArrayType(ParamType, ParamElement, ParamLength))
		{
			return Runtime::ParseNativeArrayType(ArgType, ArgElement, ArgLength) && ParamLength == ArgLength && MatchNested(ParamElement, ArgElement);
		}

		std::string ParamBase;
		std::vector<std::string> ParamArgs;
		if (!SplitUserGenericTypeApp(ParamType, ParamBase, ParamArgs))
		{
			return false;
		}
		std::string ArgBase;
		std::vector<std::string> ArgArgs;
		if (!SplitUserGenericTypeApp(ArgType, ArgBase, ArgArgs))
		{
			const std::string MangledBase = GenericTemplateBaseLinkage(ArgType);
			if (MangledBase.empty())
			{
				return false;
			}
			ArgBase = MangledBase;
			ArgArgs = DecodeGenericConcreteTypes(ArgType);
		}
		const auto SimpleBase = [](const std::string& Base)
		{
			const std::size_t Separator = Base.rfind('\x1F');
			std::string_view Name(Base);
			if (Separator != std::string::npos)
			{
				Name.remove_prefix(Separator + 1);
			}
			if (Name.rfind("::", 0) == 0)
			{
				Name.remove_prefix(2);
			}
			return Name;
		};
		if (SimpleBase(ParamBase) != SimpleBase(ArgBase) || ParamArgs.size() != ArgArgs.size())
		{
			return false;
		}
		std::string MatchedConcrete;
		for (std::size_t Index = 0; Index < ParamArgs.size(); ++Index)
		{
			std::string Candidate;
			if (!TryMatchTypeParam(ParamArgs[Index], ArgArgs[Index], TypeParam, Candidate))
			{
				continue;
			}
			if (!MatchedConcrete.empty() && MatchedConcrete != Candidate)
			{
				return false;
			}
			MatchedConcrete = std::move(Candidate);
		}
		OutConcrete = std::move(MatchedConcrete);
		return !OutConcrete.empty();
	}

	bool VeroScriptCompiler::TryResolveIndexElementTypeName(
	    const FExpr& ContainerExpr,
	    std::string& OutElementTypeName) const
	{
		std::string ContainerTypeName;
		if (!TryResolveExprTypeName(ContainerExpr, ContainerTypeName))
		{
			return false;
		}
		if (Runtime::IsDynamicArrayTypeName(ContainerTypeName))
		{
			return Runtime::ParseDynamicArrayElementType(ContainerTypeName, OutElementTypeName);
		}
		if (Runtime::IsMapTypeName(ContainerTypeName))
		{
			std::string KeyType;
			return ParseMapTypeForCompile(ContainerTypeName, KeyType, OutElementTypeName);
		}
		if (Runtime::IsOrderedMapTypeName(ContainerTypeName))
		{
			std::string KeyType;
			return ParseOrderedMapTypeForCompile(ContainerTypeName, KeyType, OutElementTypeName);
		}
		if (Runtime::IsDequeTypeName(ContainerTypeName))
		{
			return Runtime::ParseDequeElementType(ContainerTypeName, OutElementTypeName);
		}
		if (Runtime::IsScriptArgsArrayTypeName(ContainerTypeName))
		{
			OutElementTypeName = "string";
			return true;
		}
		std::uint32_t Length = 0;
		return Runtime::ParseNativeArrayType(ContainerTypeName, OutElementTypeName, Length);
	}

	const FMethodDecl* VeroScriptCompiler::FindMethodDeclForReference(
	    const std::string& OwnerTypeName,
	    const std::string& MethodName,
	    const std::size_t ArgCount) const
	{
		std::string OwnerLinkage;
		const FMethodDecl* Method = nullptr;
		if (!FindMethodDeclOwnerLinkage(OwnerTypeName, MethodName, OwnerLinkage, Method, ArgCount))
		{
			return nullptr;
		}
		return Method;
	}

	bool VeroScriptCompiler::FindMethodDeclOwnerLinkage(
	    const std::string& OwnerTypeName,
	    const std::string& MethodName,
	    std::string& OutOwnerLinkageName,
	    const FMethodDecl*& OutMethod,
	    const std::size_t ArgCount) const
	{
		OutMethod = nullptr;
		OutOwnerLinkageName.clear();
		if (!ActiveProgram)
		{
			return false;
		}
		std::string InstantiatedForSubst = OwnerTypeName;
		std::string OwnerLinkageName = ResolveTypeLinkageName(OwnerTypeName, CurrentModulePath);
		std::string GenericBase;
		std::vector<std::string> GenericArgs;
		if (OwnerLinkageName.empty() && SplitUserGenericTypeApp(OwnerTypeName, GenericBase, GenericArgs))
		{
			OwnerLinkageName = ResolveTypeLinkageName(GenericBase, CurrentModulePath);
			InstantiatedForSubst = QualifyTypeName(OwnerTypeName, CurrentModulePath);
		}
		if (OwnerLinkageName.empty())
		{
			const std::string TemplateBase = GenericTemplateBaseLinkage(OwnerTypeName);
			if (!TemplateBase.empty())
			{
				InstantiatedForSubst =
				    FindGenericTypeMarker(OwnerTypeName) != std::string::npos
				    ? OwnerTypeName
				    : InstantiatedForSubst;
				OwnerLinkageName = TemplateBase.find('\x1F') != std::string::npos
				    ? TemplateBase
				    : ResolveTypeLinkageName(TemplateBase, CurrentModulePath);
			}
		}
		else
		{
			const std::string TemplateBase = GenericTemplateBaseLinkage(OwnerLinkageName);
			if (!TemplateBase.empty())
			{
				InstantiatedForSubst =
				    FindGenericTypeMarker(OwnerLinkageName) != std::string::npos
				    ? OwnerLinkageName
				    : (FindGenericTypeMarker(OwnerTypeName) != std::string::npos
				              ? OwnerTypeName
				              : InstantiatedForSubst);
				OwnerLinkageName = TemplateBase;
			}
		}
		if (OwnerLinkageName.empty())
		{
			return false;
		}

		std::unordered_map<std::string, std::string> OwnerSubst;
		TryBuildGenericOwnerSubst(InstantiatedForSubst, OwnerSubst, FSourceLocation{});

		const auto MethodArityOk = [ArgCount](const FMethodDecl& Method) -> bool
		{
			if (ArgCount == static_cast<std::size_t>(-1))
			{
				return true;
			}
			return ParamsAcceptArgCount(Method.Parameters, ArgCount);
		};

		if (IsInterfaceTypeName(OwnerLinkageName))
		{
			std::string FlattenLinkage = OwnerLinkageName;
			if (GenericTemplateBaseLinkage(OwnerTypeName).size() > 0 || FindGenericTypeMarker(OwnerTypeName) != std::string::npos)
			{
				FlattenLinkage =
				    FindGenericTypeMarker(OwnerTypeName) != std::string::npos
				    ? OwnerTypeName
				    : QualifyTypeName(OwnerTypeName, CurrentModulePath);
			}
			else if (SplitUserGenericTypeApp(OwnerTypeName, GenericBase, GenericArgs))
			{
				FlattenLinkage = QualifyTypeName(OwnerTypeName, CurrentModulePath);
			}
			std::vector<std::pair<std::string, const FMethodDecl*>> Flattened;
			std::unordered_set<std::string> VisitedInterfaces;
			if (!CollectFlattenedInterfaceMethods(FlattenLinkage, Flattened, VisitedInterfaces))
			{
				return false;
			}
			for (const auto& Entry : Flattened)
			{
				if (Entry.second->Name == MethodName && MethodArityOk(*Entry.second))
				{
					OutOwnerLinkageName = Entry.first;
					OutMethod = Entry.second;
					return true;
				}
			}
			return false;
		}

		const auto TryInterfaces =
		    [&](const std::vector<std::string>& InterfaceTypeNames,
		        const std::string& DeclModulePath) -> bool
		{
			for (const std::string& InterfaceName : InterfaceTypeNames)
			{
				const std::string SubstIface =
				    SubstituteTypeNameWith(InterfaceName, OwnerSubst);
				std::string IfaceLinkage = QualifyTypeName(SubstIface, DeclModulePath);
				if (IfaceLinkage.empty() || IfaceLinkage == SubstIface)
				{
					IfaceLinkage = ResolveTypeLinkageName(SubstIface, DeclModulePath);
				}
				if (IfaceLinkage.empty())
				{
					continue;
				}
				std::vector<std::pair<std::string, const FMethodDecl*>> Flattened;
				std::unordered_set<std::string> VisitedInterfaces;
				if (!CollectFlattenedInterfaceMethods(IfaceLinkage, Flattened, VisitedInterfaces))
				{
					continue;
				}
				for (const auto& Entry : Flattened)
				{
					if (Entry.second->Name == MethodName && MethodArityOk(*Entry.second))
					{
						OutOwnerLinkageName = Entry.first;
						OutMethod = Entry.second;
						return true;
					}
				}
			}
			return false;
		};

		const auto BindOwner = [&](const std::string& AstLinkage)
		{
			OutOwnerLinkageName =
			    FindGenericTypeMarker(InstantiatedForSubst) != std::string::npos
			    ? InstantiatedForSubst
			    : AstLinkage;
		};

		const auto AstLookup = [this](const std::string& Linkage) -> std::string
		{
			const std::string TemplateBase = GenericTemplateBaseLinkage(Linkage);
			return TemplateBase.empty() ? Linkage : TemplateBase;
		};
		// Arity-overloaded types share linkage SimpleName; match TypeParams to instance args.
		const auto DeclArityMatches =
		    [this](const std::size_t DeclArity, const std::string& TypeLinkage) -> bool
		{
			if (GenericTemplateBaseLinkage(TypeLinkage).empty())
			{
				return DeclArity == 0;
			}
			return DeclArity == DecodeGenericConcreteTypes(TypeLinkage).size();
		};

		const auto ResolveSubstitutedBase =
		    [&](const std::string& BaseAsWritten, const std::string& DeclModulePath) -> std::string
		{
			const std::string Substituted = OwnerSubst.empty()
			    ? BaseAsWritten
			    : SubstituteTypeNameWith(BaseAsWritten, OwnerSubst);
			std::string BaseLinkage = ResolveTypeLinkageName(Substituted, DeclModulePath);
			if (BaseLinkage.empty())
			{
				BaseLinkage = QualifyTypeName(Substituted, DeclModulePath);
			}
			return BaseLinkage;
		};

		// Own methods → base class/struct subtrees (declaration order) → own interfaces.
		std::function<bool(const std::string&)> WalkType;
		WalkType = [&](const std::string& TypeLinkage) -> bool
		{
			if (TypeLinkage.empty())
			{
				return false;
			}
			const std::string LookupLinkage = AstLookup(TypeLinkage);
			for (const auto Index : FindDeclarationCandidates(SourceStructIndices, LookupLinkage))
			{
				const FStructDecl& StructDecl = ActiveProgram->Structs[Index];
				if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) != LookupLinkage || !DeclArityMatches(TemplateParameterNames(StructDecl).size(), TypeLinkage))
				{
					continue;
				}
				for (const FMethodDecl& Method : StructDecl.Methods)
				{
					if (Method.Name == MethodName && MethodArityOk(Method))
					{
						BindOwner(TypeLinkage);
						OutMethod = &Method;
						return true;
					}
				}
				if (!StructDecl.BaseTypeName.empty())
				{
					const std::string BaseLinkage = ResolveSubstitutedBase(
					    StructDecl.BaseTypeName,
					    StructDecl.ModulePath);
					if (WalkType(BaseLinkage))
					{
						return true;
					}
				}
				return TryInterfaces(StructDecl.InterfaceTypeNames, StructDecl.ModulePath);
			}
			for (const auto Index : FindDeclarationCandidates(SourceClassIndices, LookupLinkage))
			{
				const FClassDecl& ClassDecl = ActiveProgram->Classes[Index];
				if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) != LookupLinkage || !DeclArityMatches(TemplateParameterNames(ClassDecl).size(), TypeLinkage))
				{
					continue;
				}
				for (const FMethodDecl& Method : ClassDecl.Methods)
				{
					if (Method.Name == MethodName && MethodArityOk(Method))
					{
						BindOwner(TypeLinkage);
						OutMethod = &Method;
						return true;
					}
				}
				std::vector<std::string> BaseLinkages;
				if (FindGenericTypeMarker(TypeLinkage) != std::string::npos || FindGenericTypeMarker(InstantiatedForSubst) != std::string::npos)
				{
					const std::string ForBases =
					    FindGenericTypeMarker(TypeLinkage) != std::string::npos
					    ? TypeLinkage
					    : InstantiatedForSubst;
					BaseLinkages = GetAstBaseTypeNames(ForBases);
				}
				if (BaseLinkages.empty())
				{
					for (const std::string& BaseSimple : ClassDecl.BaseTypeNames)
					{
						const std::string BaseLinkage =
						    ResolveSubstitutedBase(BaseSimple, ClassDecl.ModulePath);
						if (!BaseLinkage.empty())
						{
							BaseLinkages.push_back(BaseLinkage);
						}
					}
				}
				for (const std::string& BaseLinkage : BaseLinkages)
				{
					if (WalkType(BaseLinkage))
					{
						return true;
					}
				}
				return TryInterfaces(ClassDecl.InterfaceTypeNames, ClassDecl.ModulePath);
			}
			for (const FStructDecl& StructDecl : BuiltinStructTemplates)
			{
				if (GetSymbolLinkageName(
				        StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) != LookupLinkage ||
				    !DeclArityMatches(TemplateParameterNames(StructDecl).size(), TypeLinkage))
				{
					continue;
				}
				for (const FMethodDecl& Method : StructDecl.Methods)
				{
					if (Method.Name == MethodName && MethodArityOk(Method))
					{
						BindOwner(TypeLinkage);
						OutMethod = &Method;
						return true;
					}
				}
				return false;
			}
			for (const FClassDecl& ClassDecl : BuiltinClassTemplates)
			{
				if (GetSymbolLinkageName(
				        ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) != LookupLinkage ||
				    !DeclArityMatches(TemplateParameterNames(ClassDecl).size(), TypeLinkage))
				{
					continue;
				}
				for (const FMethodDecl& Method : ClassDecl.Methods)
				{
					if (Method.Name == MethodName && MethodArityOk(Method))
					{
						BindOwner(TypeLinkage);
						OutMethod = &Method;
						return true;
					}
				}
				return false;
			}
			return false;
		};
		return WalkType(
		    FindGenericTypeMarker(InstantiatedForSubst) != std::string::npos
		        ? InstantiatedForSubst
		        : OwnerLinkageName);
	}

	bool VeroScriptCompiler::ResolveMethodForCall(
	    const std::string& OwnerTypeName,
	    const std::string& MethodName,
	    const std::vector<std::string>& ArgTypeNames,
	    std::string& OutOwnerLinkageName,
	    const FMethodDecl*& OutMethod,
	    const FSourceLocation& Location,
	    const bool bAllowTypeMismatchCandidate,
	    const bool bReportErrors,
	    bool* OutAmbiguous,
	    const bool bReceiverIsConst,
	    const std::vector<std::string>* ExplicitTemplateArguments, const std::string* ExpectedReturnType) const
	{
		OutMethod = nullptr;
		OutOwnerLinkageName.clear();
		if (OutAmbiguous)
		{
			*OutAmbiguous = false;
		}
		if (!ActiveProgram)
		{
			return false;
		}
		const std::size_t ArgCount = ArgTypeNames.size();
		std::string InstantiatedOwner = OwnerTypeName;
		std::string OwnerLinkageName = ResolveTypeLinkageName(OwnerTypeName, CurrentModulePath);
		if (OwnerLinkageName.empty())
		{
			OwnerLinkageName = OwnerTypeName;
		}
		{
			std::string GenericBase;
			std::vector<std::string> GenericArgs;
			if (FindGenericTypeMarker(OwnerTypeName) != std::string::npos)
			{
				InstantiatedOwner = OwnerTypeName;
			}
			else if (FindGenericTypeMarker(OwnerLinkageName) != std::string::npos)
			{
				InstantiatedOwner = OwnerLinkageName;
			}
			else if (SplitUserGenericTypeApp(OwnerTypeName, GenericBase, GenericArgs))
			{
				InstantiatedOwner = QualifyTypeName(OwnerTypeName, CurrentModulePath);
				if (OwnerLinkageName.find('<') != std::string::npos)
				{
					OwnerLinkageName = ResolveTypeLinkageName(
					    GenericBase, CurrentModulePath);
					if (OwnerLinkageName.empty())
					{
						OwnerLinkageName = GenericBase;
					}
				}
			}
			const std::string TemplateBase = GenericTemplateBaseLinkage(
			    FindGenericTypeMarker(InstantiatedOwner) != std::string::npos
			        ? InstantiatedOwner
			        : OwnerLinkageName);
			if (!TemplateBase.empty())
			{
				OwnerLinkageName = TemplateBase;
			}
		}

		std::unordered_map<std::string, std::string> OwnerSubst;
		TryBuildGenericOwnerSubst(InstantiatedOwner, OwnerSubst, Location);

		constexpr std::string_view ConversionPrefix = "operator conv ";
		const bool bConversionLookup = MethodName.rfind(ConversionPrefix, 0) == 0;
		const std::string ConversionTarget = bConversionLookup
		    ? CanonicalTypeNameForCoercion(QualifyTypeName(
		          NormalizeTypeAlias(MethodName.substr(ConversionPrefix.size())), CurrentModulePath))
		    : std::string{};
		const auto ScoreCandidate =
		    [&](const FMethodDecl& Method, const std::string& DeclModulePath,
		        const std::unordered_map<std::string, std::string>& TypeSubst) -> int
		{
			const bool bNameMatches = bConversionLookup
			    ? Method.Name.rfind(ConversionPrefix, 0) == 0
			    : Method.Name == MethodName || MethodSpecializationKeys.count(&Method) != 0;
			if (!bNameMatches || !ParamsAcceptArgCount(Method.Parameters, ArgCount))
			{
				return -1;
			}
			if (bConversionLookup)
			{
				// The parser requires a conversion's return type to match its
				// target. Compare that type in the declaration's scope, after
				// owner substitution, rather than the target's written alias.
				if (CanonicalTypeNameForCoercion(QualifyTypeName(
				        NormalizeTypeAlias(
				            SubstituteTypeNameWith(Method.ReturnType, TypeSubst), DeclModulePath),
				        DeclModulePath)) != ConversionTarget)
				{
					return -1;
				}
			}
			// Static member functions have no `this`, so receiver constness never applies.
			if (bReceiverIsConst && !Method.bIsConst && !Method.bIsStatic)
			{
				return -1;
			}
			const std::vector<std::string> ParamTypes =
			    CanonicalMethodParamTypeNames(Method, DeclModulePath, TypeSubst);
			const bool bVariadic = IsVariadicParamList(Method.Parameters);
			const std::size_t FixedCount = FixedParamCount(Method.Parameters);
			int Score = 0;
			bool bAnyTypedArg = false;
			bool bTypeMismatch = false;
			const auto ScorePair =
			    [&](const std::string& ArgTypeName, const std::string& ParamCanonical)
			{
				if (ArgTypeName.empty())
				{
					return;
				}
				bAnyTypedArg = true;
				int Contribution = 0;
				if (!TryScoreArgParamTypes(ArgTypeName, ParamCanonical, Contribution))
				{
					bTypeMismatch = true;
					return;
				}
				Score += Contribution;
			};
			const std::size_t FixedCompare = (std::min)(ArgCount, FixedCount);
			for (std::size_t Index = 0; Index < FixedCompare; ++Index)
			{
				ScorePair(ArgTypeNames[Index], ParamTypes[Index]);
			}
			if (bVariadic && ArgCount > FixedCount)
			{
				std::string ElementType = Method.Parameters.back().TypeName;
				ElementType = CanonicalTypeNameForCoercion(
				    QualifyTypeName(
				        NormalizeTypeAlias(
				            SubstituteTypeNameWith(ElementType, TypeSubst),
				            DeclModulePath),
				        DeclModulePath));
				const std::string ArrayType = MakeDynamicArrayTypeName(ElementType);
				if (ArgCount == FixedCount + 1)
				{
					const std::string& RestArg = ArgTypeNames[FixedCount];
					if (!RestArg.empty())
					{
						bAnyTypedArg = true;
						int Contribution = 0;
						if (TryScoreArgParamTypes(RestArg, ArrayType, Contribution) || TryScoreArgParamTypes(RestArg, ElementType, Contribution))
						{
							Score += 2;
						}
						else
						{
							bTypeMismatch = true;
						}
					}
				}
				else
				{
					for (std::size_t Index = FixedCount; Index < ArgCount; ++Index)
					{
						ScorePair(ArgTypeNames[Index], ElementType);
					}
				}
			}
			// Arity matches but types do not: keep as weak candidate so call-site
			// coercion / array checks can emit a specific error (not "unknown method").
			if (bTypeMismatch)
			{
				return -2;
			}
			if (!bAnyTypedArg)
			{
				Score = 0;
			}
			return Score;
		};

		const auto CallParamTypes = [&](const FMethodDecl&, std::vector<std::string> Parameters)
		{
			return CompilerDetail::CallParameterTypes(ArgTypeNames, std::move(Parameters),
			    [&](const auto& Argument, const auto& Parameter, int& Score)
			{
				return TryScoreArgParamTypes(Argument, Parameter, Score);
			},
			    [&](const auto& Element)
			{
				return MakeDynamicArrayTypeName(Element);
			});
		};
		const auto PreferMethod = [&](const FMethodDecl& Left, std::vector<std::string> LeftParams,
		                              const FMethodDecl& Right, std::vector<std::string> RightParams)
		{
			LeftParams = CallParamTypes(Left, std::move(LeftParams));
			RightParams = CallParamTypes(Right, std::move(RightParams));
			auto Arguments = ArgTypeNames;
			if (!Left.bIsStatic && !Right.bIsStatic)
			{
				// A better receiver binding cannot compensate for a worse argument.
				Arguments.push_back((bReceiverIsConst ? "const lvalue " : "lvalue ") + InstantiatedOwner);
				LeftParams.push_back((Left.bIsConst ? "const ref " : "ref ") + InstantiatedOwner);
				RightParams.push_back((Right.bIsConst ? "const ref " : "ref ") + InstantiatedOwner);
			}
			if (PreferOperatorReferenceOverload(Arguments, LeftParams, RightParams)) return true;
			if (PreferOperatorReferenceOverload(Arguments, RightParams, LeftParams)) return false;
			for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
			{
				int LeftScore = 0, RightScore = 0;
				if (!TryScoreArgParamTypes(Arguments[Index], LeftParams[Index], LeftScore) || !TryScoreArgParamTypes(Arguments[Index], RightParams[Index], RightScore) || LeftScore != RightScore)
				{
					return false;
				}
			}
			const bool LeftTemplate = MethodSpecializationKeys.count(&Left) != 0;
			const bool RightTemplate = MethodSpecializationKeys.count(&Right) != 0;
			if (LeftTemplate != RightTemplate) return !LeftTemplate;
			if (!LeftTemplate) return false;
			const auto& LeftDeclaration = *MethodSpecializations.at(MethodSpecializationKeys.at(&Left)).Declaration;
			const auto& RightDeclaration = *MethodSpecializations.at(MethodSpecializationKeys.at(&Right)).Declaration;
			const auto Patterns = [&](const FMethodDecl& Declaration, const FMethodDecl& Closed)
			{
				auto Bindings = MethodSpecializations.at(MethodSpecializationKeys.at(&Closed)).Bindings;
				for (const auto& Name : TemplateParameterNames(Declaration))
				{
					Bindings.erase(Name);
				}
				std::vector<std::string> Result;
				for (const auto& Parameter : Declaration.Parameters)
				{
					Result.push_back((Parameter.bIsRef ? (Parameter.bIsConst ? "const ref " : "ref ") : "") + SubstituteTypeNameWith(Parameter.TypeName, Bindings) + (Parameter.bIsVariadic ? "..." : ""));
				}
				return Result;
			};
			return CompilerDetail::MoreSpecializedTemplatePattern(TemplateParameterNames(LeftDeclaration),
			    Patterns(LeftDeclaration, Left), TemplateParameterNames(RightDeclaration), Patterns(RightDeclaration, Right),
			    [&](const auto& Pattern, const auto& Argument, const auto& Parameter, auto& Binding)
			{
				return TryMatchTypeParam(Pattern, Argument, Parameter, Binding);
			});
		};

		const auto PickBest =
		    [&](std::vector<std::pair<int, const FMethodDecl*>>& Scored,
		        const std::string& BindLinkage) -> bool
		{
			if (Scored.empty())
			{
				return false;
			}
			CompilerDetail::DiscardBoxingFallbacks(Scored, [](const auto& C)
			{
				return C.first;
			}, [&](const auto& C)
			{
				return CompilerDetail::NeedsVariantBoxing(ArgTypeNames, CanonicalMethodParamTypeNames(*C.second, ResolveOwnerModulePath(OwnerTypeName), OwnerSubst));
			});
			int Best = std::numeric_limits<int>::min();
			for (const auto& Entry : Scored)
			{
				Best = std::max(Best, Entry.first);
			}
			// Prefer viable scores (>= 0) over weak type-mismatch (-2).
			if (Best >= 0)
			{

				std::vector<const FMethodDecl*> Ties;
				for (const auto& Entry : Scored)
				{
					// Viability is separate from per-argument conversion ordering.
					if (Entry.first >= 0)
					{
						Ties.push_back(Entry.second);
					}
				}
				const auto OriginalTies = Ties;
				Ties.erase(std::remove_if(Ties.begin(), Ties.end(), [&](const FMethodDecl* Candidate)
				{
					const auto Params = CanonicalMethodParamTypeNames(*Candidate,
					    ResolveOwnerModulePath(OwnerTypeName), OwnerSubst);
					return std::any_of(OriginalTies.begin(), OriginalTies.end(), [&](const FMethodDecl* Other)
					{
						return Other != Candidate && PreferMethod(*Other, CanonicalMethodParamTypeNames(*Other, ResolveOwnerModulePath(OwnerTypeName), OwnerSubst), *Candidate, Params);
					});
				}),
				    Ties.end());
				if (Ties.size() != 1)
				{
					if (OutAmbiguous)
					{
						*OutAmbiguous = true;
					}
					if (bReportErrors)
					{
						ReportErrorId("diag.ambiguous_overload_for_method", Location, {MethodName});
					}
					return false;
				}
				OutOwnerLinkageName =
				    FindGenericTypeMarker(InstantiatedOwner) != std::string::npos
				    ? InstantiatedOwner
				    : BindLinkage;
				OutMethod = Ties.front();
				if (OutMethod->bDeleted && bReportErrors)
				{
					ReportErrorId(
					    "diag.use_of_deleted_declaration",
					    Location,
					    {MethodName});
					OutMethod = nullptr;
					OutOwnerLinkageName.clear();
					return false;
				}
				return true;
			}
			if (bAllowTypeMismatchCandidate && Best == -2)
			{
				std::vector<const FMethodDecl*> Weak;
				for (const auto& Entry : Scored)
				{
					if (Entry.first == -2)
					{
						Weak.push_back(Entry.second);
					}
				}
				if (Weak.size() == 1)
				{
					OutOwnerLinkageName =
					    FindGenericTypeMarker(InstantiatedOwner) != std::string::npos
					    ? InstantiatedOwner
					    : BindLinkage;
					OutMethod = Weak.front();
					return true;
				}
				if (bReportErrors)
				{
					ReportErrorId("diag.no_matching_overload_for_method", Location, {MethodName});
				}
				return false;
			}
			return false;
		};

		struct FInterfaceCandidate
		{
			int Score;
			std::string Owner;
			const FMethodDecl* Method;
			std::vector<std::string> Params;
		};
		const auto AppendInterfaceMethod = [&](const std::string& Owner, const FMethodDecl& Declaration,
		                                       std::vector<FInterfaceCandidate>& Scored)
		{
			if (Declaration.Name != MethodName) return;
			const auto DeclModule = ResolveOwnerModulePath(Owner);
			std::unordered_map<std::string, std::string> Bindings;
			BuildInterfaceTypeSubst(Owner, Bindings);
			const auto* Method = SpecializeMethodSignature(Declaration, Owner, DeclModule, Bindings,
			    ArgTypeNames, ExplicitTemplateArguments, ExpectedReturnType);
			if (!Method) return;
			const int Score = ScoreCandidate(*Method, DeclModule, Bindings);
			if (Score < 0 && !(bAllowTypeMismatchCandidate && Score == -2)) return;
			if (std::any_of(Scored.begin(), Scored.end(), [&](const auto& Candidate)
			{
				return Candidate.Method == Method && Candidate.Owner == Owner;
			}))
			{
				return;
			}
			Scored.push_back({Score, Owner, Method, CanonicalMethodParamTypeNames(*Method, DeclModule, Bindings)});
		};
		const auto AppendInterfaceHelpers = [&](const std::string& Root,
		                                        std::vector<FInterfaceCandidate>& Scored, const bool Static)
		{
			std::vector<std::string> Pending{Root};
			std::unordered_set<std::string> Visited;
			for (std::size_t Index = 0; Index < Pending.size(); ++Index)
			{
				const auto Owner = Pending[Index];
				if (!Visited.insert(Owner).second)
				{
					continue;
				}
				const auto* Declaration = FindInterfaceDeclByLinkage(Owner);
				if (!Declaration)
				{
					continue;
				}
				for (const auto& Method : Declaration->Methods)
				{
					if ((!TemplateParameterNames(Method).empty() && (!Method.bIsStatic || Static)) || (Static && Method.bIsStatic))
					{
						AppendInterfaceMethod(Owner, Method, Scored);
					}
				}
				std::unordered_map<std::string, std::string> Bindings;
				BuildInterfaceTypeSubst(Owner, Bindings);
				for (const auto& Base : Declaration->BaseInterfaceNames)
				{
					const auto Linkage = ResolveInheritanceBaseLinkage(SubstituteTypeNameWith(Base, Bindings), Declaration->ModulePath);
					if (!Linkage.empty())
					{
						Pending.push_back(Linkage);
					}
				}
			}
		};

		const auto PickInterfaceBest = [&](std::vector<FInterfaceCandidate>& Scored) -> bool
		{
			if (Scored.empty()) return false;
			CompilerDetail::DiscardBoxingFallbacks(Scored, [](const auto& C)
			{
				return C.Score;
			}, [&](const auto& C)
			{
				return CompilerDetail::NeedsVariantBoxing(ArgTypeNames, C.Params);
			});
			const int BestScore = std::max_element(Scored.begin(), Scored.end(), [](const auto& Left, const auto& Right)
			{
				return Left.Score < Right.Score;
			})->Score;
			if (BestScore < 0)
			{
				if (bAllowTypeMismatchCandidate && BestScore == -2 && Scored.size() == 1)
				{
					OutOwnerLinkageName = Scored.front().Owner;
					OutMethod = Scored.front().Method;
					return true;
				}
				if (bReportErrors)
				{
					ReportErrorId("diag.no_matching_overload_for_method", Location, {MethodName});
				}
				return false;
			}

			Scored.erase(std::remove_if(Scored.begin(), Scored.end(), [&](const auto& Entry)
			{
				return Entry.Score < 0;
			}),
			    Scored.end());
			const auto OriginalCandidates = Scored;
			Scored.erase(std::remove_if(Scored.begin(), Scored.end(), [&](const auto& Candidate)
			{
				return std::any_of(OriginalCandidates.begin(), OriginalCandidates.end(), [&](const auto& Other)
				{
					return (Other.Method != Candidate.Method || Other.Owner != Candidate.Owner) && PreferMethod(*Other.Method, Other.Params, *Candidate.Method, Candidate.Params);
				});
			}),
			    Scored.end());
			if (Scored.size() != 1)
			{
				if (OutAmbiguous)
				{
					*OutAmbiguous = true;
				}
				if (bReportErrors)
				{
					ReportErrorId("diag.ambiguous_overload_for_method", Location, {MethodName});
				}
				return false;
			}
			if (Scored.front().Method->bDeleted && bReportErrors)
			{
				ReportErrorId("diag.use_of_deleted_declaration", Location, {MethodName});
				return false;
			}
			OutOwnerLinkageName = Scored.front().Owner;
			OutMethod = Scored.front().Method;
			return true;
		};

		const auto TryClassInterfaces =
		    [&](const std::vector<std::string>& InterfaceTypeNames,
		        const std::string& DeclModulePath) -> bool
		{
			std::vector<FInterfaceCandidate> Scored;
			for (const std::string& InterfaceName : InterfaceTypeNames)
			{
				const std::string SubstIface =
				    SubstituteTypeNameWith(InterfaceName, OwnerSubst);
				std::string IfaceLinkage = QualifyTypeName(SubstIface, DeclModulePath);
				if (IfaceLinkage.empty() || IfaceLinkage == SubstIface)
				{
					IfaceLinkage = ResolveTypeLinkageName(SubstIface, DeclModulePath);
				}
				if (IfaceLinkage.empty())
				{
					continue;
				}
				AppendInterfaceHelpers(IfaceLinkage, Scored, false);
				std::vector<std::pair<std::string, const FMethodDecl*>> Flattened;
				std::unordered_set<std::string> VisitedInterfaces;
				if (!CollectFlattenedInterfaceMethods(IfaceLinkage, Flattened, VisitedInterfaces))
				{
					continue;
				}
				for (const auto& Entry : Flattened)
				{
					const FInterfaceDecl* const IfaceDecl =
					    FindInterfaceDeclByLinkage(Entry.first);
					const std::string IfaceModule =
					    IfaceDecl ? IfaceDecl->ModulePath : DeclModulePath;
					std::unordered_map<std::string, std::string> IfaceSubst;
					BuildInterfaceTypeSubst(Entry.first, IfaceSubst);
					const auto* Method = SpecializeMethodSignature(*Entry.second, Entry.first, IfaceModule,
					    IfaceSubst, ArgTypeNames, ExplicitTemplateArguments, ExpectedReturnType);
					if (!Method)
					{
						continue;
					}
					const int SavedScore = ScoreCandidate(*Method, IfaceModule, IfaceSubst);
					if (SavedScore < 0)
					{
						continue;
					}
					const bool bDuplicate = std::any_of(Scored.begin(), Scored.end(), [&](const auto& Existing)
					{
						return Existing.Method == Method && Existing.Owner == Entry.first;
					});
					if (!bDuplicate)
					{
						Scored.push_back({SavedScore, Entry.first, Method,
						    CanonicalMethodParamTypeNames(*Method, IfaceModule, IfaceSubst)});
					}
				}
			}
			return PickInterfaceBest(Scored);
		};

		const std::size_t WantedTypeParamArity = [&]() -> std::size_t
		{
			if (!GenericTemplateBaseLinkage(InstantiatedOwner).empty())
			{
				return DecodeGenericConcreteTypes(InstantiatedOwner).size();
			}
			std::string GenericBase;
			std::vector<std::string> GenericArgs;
			return SplitUserGenericTypeApp(InstantiatedOwner, GenericBase, GenericArgs)
			    ? GenericArgs.size()
			    : 0;
		}();
		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceClassIndices, OwnerLinkageName))
		{
			const auto& ClassDecl = ActiveProgram->Classes[DeclarationIndex];
			if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) != OwnerLinkageName || TemplateParameterNames(ClassDecl).size() != WantedTypeParamArity)
			{
				continue;
			}
			{
				std::vector<std::pair<int, const FMethodDecl*>> Scored;
				for (const FMethodDecl& Declaration : ClassDecl.Methods)
				{
					if (!bConversionLookup && Declaration.Name != MethodName)
					{
						continue;
					}
					const FMethodDecl* Method = SpecializeMethodSignature(Declaration,
					    InstantiatedOwner, ClassDecl.ModulePath, OwnerSubst,
					    ArgTypeNames, ExplicitTemplateArguments, ExpectedReturnType);
					if (!Method)
					{
						continue;
					}
					const int Score = ScoreCandidate(*Method, ClassDecl.ModulePath, OwnerSubst);
					if (Score >= 0 || (bAllowTypeMismatchCandidate && Score == -2))
					{
						Scored.emplace_back(Score, Method);
					}
				}
				if (PickBest(Scored, OwnerLinkageName))
				{
					return true;
				}
				if (HasError())
				{
					return false;
				}
				for (const auto& Entry : Scored)
				{
					if (Entry.first >= 0)
					{
						// Viable overloads existed but were ambiguous (possibly silent).
						return false;
					}
				}
			}
			std::vector<std::string> BaseLinkages;
			if (FindGenericTypeMarker(InstantiatedOwner) != std::string::npos)
			{
				BaseLinkages = GetAstBaseTypeNames(InstantiatedOwner);
			}
			if (BaseLinkages.empty())
			{
				for (const std::string& BaseSimple : ClassDecl.BaseTypeNames)
				{
					const std::string BaseAsWritten = OwnerSubst.empty()
					    ? BaseSimple
					    : SubstituteTypeNameWith(BaseSimple, OwnerSubst);
					std::string BaseLinkage =
					    ResolveTypeLinkageName(BaseAsWritten, ClassDecl.ModulePath);
					if (BaseLinkage.empty())
					{
						BaseLinkage = QualifyTypeName(BaseAsWritten, ClassDecl.ModulePath);
					}
					if (!BaseLinkage.empty())
					{
						BaseLinkages.push_back(BaseLinkage);
					}
				}
			}
			std::vector<std::pair<std::string, const FMethodDecl*>> Candidates;
			for (const std::string& BaseLinkage : BaseLinkages)
			{
				std::string NestedOwner;
				const FMethodDecl* NestedMethod = nullptr;
				if (!ResolveMethodForCall(
				        BaseLinkage,
				        MethodName,
				        ArgTypeNames,
				        NestedOwner,
				        NestedMethod,
				        Location,
				        bAllowTypeMismatchCandidate,
				        bReportErrors,
				        OutAmbiguous,
				        bReceiverIsConst, ExplicitTemplateArguments))
				{
					if (HasError())
					{
						return false;
					}
					continue;
				}
				if (!NestedMethod)
				{
					continue;
				}
				bool bDuplicate = false;
				for (const auto& Existing : Candidates)
				{
					if (Existing.first == NestedOwner && Existing.second == NestedMethod)
					{
						bDuplicate = true;
						break;
					}
				}
				if (!bDuplicate)
				{
					Candidates.emplace_back(NestedOwner, NestedMethod);
				}
			}
			if (Candidates.size() > 1)
			{
				if (OutAmbiguous)
				{
					*OutAmbiguous = true;
				}
				if (bReportErrors)
				{
					ReportErrorId(
					    "diag.ambiguous_method_reachable_from_multiple_bases_override_it_in",
					    Location,
					    {MethodName, ClassDecl.Name});
				}
				return false;
			}
			if (Candidates.size() == 1)
			{
				OutOwnerLinkageName = Candidates[0].first;
				OutMethod = Candidates[0].second;
				return true;
			}
			if (TryClassInterfaces(ClassDecl.InterfaceTypeNames, ClassDecl.ModulePath))
			{
				return true;
			}
			return false;
		}
		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceStructIndices, OwnerLinkageName))
		{
			const auto& StructDecl = ActiveProgram->Structs[DeclarationIndex];
			if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) != OwnerLinkageName || TemplateParameterNames(StructDecl).size() != WantedTypeParamArity)
			{
				continue;
			}
			{
				std::vector<std::pair<int, const FMethodDecl*>> Scored;
				for (const FMethodDecl& Declaration : StructDecl.Methods)
				{
					if (!bConversionLookup && Declaration.Name != MethodName)
					{
						continue;
					}
					const FMethodDecl* Method = SpecializeMethodSignature(Declaration,
					    InstantiatedOwner, StructDecl.ModulePath, OwnerSubst,
					    ArgTypeNames, ExplicitTemplateArguments, ExpectedReturnType);
					if (!Method)
					{
						continue;
					}
					const int Score = ScoreCandidate(*Method, StructDecl.ModulePath, OwnerSubst);
					if (Score >= 0 || (bAllowTypeMismatchCandidate && Score == -2))
					{
						Scored.emplace_back(Score, Method);
					}
				}
				if (PickBest(Scored, OwnerLinkageName))
				{
					return true;
				}
				if (HasError())
				{
					return false;
				}
				for (const auto& Entry : Scored)
				{
					if (Entry.first >= 0)
					{
						return false;
					}
				}
			}
			if (!StructDecl.BaseTypeName.empty())
			{
				const std::string BaseAsWritten = OwnerSubst.empty()
				    ? StructDecl.BaseTypeName
				    : SubstituteTypeNameWith(StructDecl.BaseTypeName, OwnerSubst);
				std::string BaseLinkage =
				    ResolveTypeLinkageName(BaseAsWritten, StructDecl.ModulePath);
				if (BaseLinkage.empty())
				{
					BaseLinkage = QualifyTypeName(BaseAsWritten, StructDecl.ModulePath);
				}
				std::string NestedOwner;
				const FMethodDecl* NestedMethod = nullptr;
				if (!BaseLinkage.empty() && ResolveMethodForCall(BaseLinkage, MethodName, ArgTypeNames, NestedOwner, NestedMethod, Location, bAllowTypeMismatchCandidate, bReportErrors, OutAmbiguous, bReceiverIsConst, ExplicitTemplateArguments) && NestedMethod)
				{
					OutOwnerLinkageName = NestedOwner;
					OutMethod = NestedMethod;
					return true;
				}
				if (HasError())
				{
					return false;
				}
			}
			if (TryClassInterfaces(StructDecl.InterfaceTypeNames, StructDecl.ModulePath))
			{
				return true;
			}
			return false;
		}
		const auto TryBuiltinMethods =
		    [&](const auto& Declarations) -> bool
		{
			for (const auto& Decl : Declarations)
			{
				if (GetSymbolLinkageName(
				        Decl.ModulePath, Decl.bExported, Decl.Name) != OwnerLinkageName ||
				    TemplateParameterNames(Decl).size() != WantedTypeParamArity)
				{
					continue;
				}
				std::vector<std::pair<int, const FMethodDecl*>> Scored;
				for (const FMethodDecl& Method : Decl.Methods)
				{
					const int Score = ScoreCandidate(Method, Decl.ModulePath, OwnerSubst);
					if (Score >= 0 || (bAllowTypeMismatchCandidate && Score == -2))
					{
						Scored.emplace_back(Score, &Method);
					}
				}
				return PickBest(Scored, OwnerLinkageName);
			}
			return false;
		};
		if (TryBuiltinMethods(BuiltinStructTemplates) || TryBuiltinMethods(BuiltinClassTemplates))
		{
			return true;
		}
		// Fallback: interface-typed receivers skip class/struct loops — score overloads
		// (const preference included), do not take the first arity match.
		{
			std::string IfaceOwnerLinkage = ResolveTypeLinkageName(OwnerTypeName, CurrentModulePath);
			std::string GenericBase;
			std::vector<std::string> GenericArgs;
			if (IfaceOwnerLinkage.empty() && SplitUserGenericTypeApp(OwnerTypeName, GenericBase, GenericArgs))
			{
				IfaceOwnerLinkage = ResolveTypeLinkageName(GenericBase, CurrentModulePath);
			}
			if (IfaceOwnerLinkage.empty())
			{
				const std::string TemplateBase = GenericTemplateBaseLinkage(OwnerTypeName);
				if (!TemplateBase.empty())
				{
					IfaceOwnerLinkage = TemplateBase.find('\x1F') != std::string::npos
					    ? TemplateBase
					    : ResolveTypeLinkageName(TemplateBase, CurrentModulePath);
				}
			}
			else
			{
				const std::string TemplateBase = GenericTemplateBaseLinkage(IfaceOwnerLinkage);
				if (!TemplateBase.empty())
				{
					IfaceOwnerLinkage = TemplateBase;
				}
			}
			if (!IfaceOwnerLinkage.empty() && IsInterfaceTypeName(IfaceOwnerLinkage))
			{
				std::string FlattenLinkage = IfaceOwnerLinkage;
				if (GenericTemplateBaseLinkage(OwnerTypeName).size() > 0 || FindGenericTypeMarker(OwnerTypeName) != std::string::npos)
				{
					FlattenLinkage =
					    FindGenericTypeMarker(OwnerTypeName) != std::string::npos
					    ? OwnerTypeName
					    : QualifyTypeName(OwnerTypeName, CurrentModulePath);
				}
				else if (SplitUserGenericTypeApp(OwnerTypeName, GenericBase, GenericArgs))
				{
					FlattenLinkage = QualifyTypeName(OwnerTypeName, CurrentModulePath);
				}
				std::vector<std::pair<std::string, const FMethodDecl*>> Flattened;
				std::unordered_set<std::string> VisitedInterfaces;
				if (!CollectFlattenedInterfaceMethods(FlattenLinkage, Flattened, VisitedInterfaces))
				{
					return false;
				}
				std::vector<FInterfaceCandidate> Scored;
				const auto AddInterfaceCandidate = [&](const std::string& Owner, const FMethodDecl& Method)
				{
					AppendInterfaceMethod(Owner, Method, Scored);
				};
				for (const auto& Entry : Flattened)
				{
					AddInterfaceCandidate(Entry.first, *Entry.second);
				}
				AppendInterfaceHelpers(FlattenLinkage, Scored, true);
				return PickInterfaceBest(Scored);
			}
		}
		if (!FindMethodDeclOwnerLinkage(
		        OwnerTypeName,
		        MethodName,
		        OutOwnerLinkageName,
		        OutMethod,
		        ArgCount) ||
		    !OutMethod || !ParamsAcceptArgCount(OutMethod->Parameters, ArgCount))
		{
			return false;
		}
		{
			const std::string DeclModule = ResolveOwnerModulePath(
			    OutOwnerLinkageName.empty() ? OwnerTypeName : OutOwnerLinkageName);
			const int Score = ScoreCandidate(*OutMethod, DeclModule, OwnerSubst);
			if (Score < 0 && !(bAllowTypeMismatchCandidate && Score == -2))
			{
				OutMethod = nullptr;
				OutOwnerLinkageName.clear();
				return false;
			}
		}
		return true;
	}

	bool VeroScriptCompiler::ResolveImplicitThisMethodForCall(
	    const FCallExpr& Call,
	    std::string& OutOwnerLinkageName,
	    const FMethodDecl*& OutMethod,
	    const bool bReportErrors) const
	{
		OutOwnerLinkageName.clear();
		OutMethod = nullptr;
		if (Call.CalleeExpr || !CurrentFunction || !CurrentFunction->bIsMethod || !Module)
		{
			return false;
		}

		const std::string OwnerTypeName = CurrentImplicitThisOwnerTypeName();
		if (OwnerTypeName.empty())
		{
			return false;
		}

		std::vector<std::string> ArgTypeNames;
		ArgTypeNames.reserve(Call.GetArguments().size());
		for (const FExpr* ArgExpr : Call.GetArguments())
		{
			if (!ArgExpr)
			{
				ArgTypeNames.emplace_back();
				continue;
			}
			const bool bArgIsRef = ArgExpr->Kind == EExprKind::Ref;
			const FExpr& Arg = UnwrapRefOperand(*ArgExpr);
			std::string ArgType;
			if (!TryResolveExprTypeName(Arg, ArgType))
			{
				ArgType.clear();
			}
			else if (bArgIsRef && !ArgType.empty())
			{
				ArgType = EncodeRefCallArgType(Arg, std::move(ArgType));
			}
			ArgTypeNames.push_back(std::move(ArgType));
		}

		return ResolveMethodForCall(
		    OwnerTypeName,
		    Call.Callee,
		    ArgTypeNames,
		    OutOwnerLinkageName,
		    OutMethod,
		    Call.Location,
		    true,
		    bReportErrors,
		    nullptr,
		    CurrentFunction->bIsConstMethod,
		    Call.bExplicitTemplateArguments || !Call.TypeArgs.empty() ? &Call.TypeArgs : nullptr);
	}

	std::string VeroScriptCompiler::CurrentImplicitThisOwnerTypeName() const
	{
		if (!CurrentFunction || !CurrentFunction->bIsMethod || !Module)
		{
			return {};
		}
		if (CurrentFunction->OwnerTypeIndex < Module->Types.size())
		{
			return Module->Types[CurrentFunction->OwnerTypeIndex].Name;
		}
		// Interface default methods intentionally use the sentinel owner index;
		// their instantiated owner is tracked separately.
		return CurrentInterfaceOwnerLinkage;
	}

	bool VeroScriptCompiler::BareCallNeedsImplicitThisCapture(const FCallExpr& Call) const
	{
		if (Call.CalleeExpr || CurrentImplicitThisOwnerTypeName().empty())
		{
			return false;
		}

		// Match the ordinary-call shadowing rules. Presence is sufficient here:
		// an invalid call to a shadowing value/function must keep its own diagnostic
		// rather than silently becoming a member call.
		std::uint16_t Slot = 0;
		if (ResolveLocal(Call.Callee, Slot))
		{
			return false;
		}
		const std::string GlobalLinkage =
		    ResolveGlobalLinkageName(Call.Callee, CurrentModulePath);
		const bool bBuiltinAccessible =
		    Call.bStandardLibraryQualified || Call.bImplementationBuiltinAccess || StandardLibrary::RequiredNamespaceForFunction(Call.Callee).empty();
		if ((!GlobalLinkage.empty() && ResolveGlobal(GlobalLinkage, Slot)) || (bBuiltinAccessible && Builtins::Lookup(Call.Callee)) || FindFunctionDeclForReference(Call.Callee, CurrentModulePath) || IsTypeNameWithoutValueBinding(Call.Callee) || Call.Callee == "format" || Call.Callee == "__format" || Call.Callee == "printf" || Call.Callee == "formatf" || Call.Callee == "__format_printf")
		{
			return false;
		}
		std::uint16_t NativeIndex = static_cast<std::uint16_t>(-1);
		if (FindNative(Call.Callee, NativeIndex))
		{
			return false;
		}

		const std::string OwnerTypeName = CurrentImplicitThisOwnerTypeName();
		if (FindMethodDeclForReference(
		        OwnerTypeName,
		        Call.Callee,
		        Call.GetArguments().size()))
		{
			return true;
		}

		// Preserve the pre-existing implicit host-base method fallback in lambdas.
		const std::string HostBase = CurrentMethodHostBaseTypeName();
		return !HostBase.empty() && FindNative(MakeHostMethodNativeName(HostBase, Call.Callee), NativeIndex) != nullptr;
	}

	std::string VeroScriptCompiler::QualifyMethodTypeName(
	    const FMethodDecl& Method, const std::string& TypeName, const std::string& DeclModulePath,
	    const std::unordered_map<std::string, std::string>& Subst) const
	{
		const FMethodDecl* Declaration = &Method;
		CompilerDetail::FTemplateBindingScope Bindings(
		    const_cast<VeroScriptCompiler*>(this)->ActiveTypeSubst, Subst);
		if (const auto Key = MethodSpecializationKeys.find(Declaration); Key != MethodSpecializationKeys.end())
		{
			Declaration = MethodSpecializations.at(Key->second).Declaration;
		}
		auto& Namespace = const_cast<VeroScriptCompiler*>(this)->CurrentNamespacePrefix;
		struct FNamespaceRestore
		{
			std::string& Target;
			std::string Previous;
			~FNamespaceRestore()
			{
				Target = std::move(Previous);
			}
		} Restore{Namespace, Namespace};
		if (const auto Context = SourceMethodContexts.find(&Declaration->DeclMeta);
		    Context != SourceMethodContexts.end())
		{
			Namespace = NamespaceFromFunctionLinkage(Context->second.OwnerLinkage);
		}
		return QualifyTypeName(
		    NormalizeTypeAlias(SubstituteTypeNameWith(TypeName, Subst), DeclModulePath), DeclModulePath);
	}

	std::vector<std::string> VeroScriptCompiler::CanonicalMethodParamTypeNames(
	    const FMethodDecl& Method,
	    const std::string& DeclModulePath,
	    const std::unordered_map<std::string, std::string>& Subst) const
	{
		std::vector<std::string> Out;
		Out.reserve(Method.Parameters.size());
		for (const auto& Param : Method.Parameters)
		{
			auto Type = QualifyMethodTypeName(Method, Param.TypeName, DeclModulePath, Subst);
			if (Param.bIsVariadic)
			{
				Type += "...";
			}
			else if (Param.bIsRef)
			{
				Type = (Param.bIsConst ? "const ref " : "ref ") + Type;
			}
			Out.push_back(std::move(Type));
		}
		return Out;
	}

	std::vector<std::string> VeroScriptCompiler::CanonicalParamTypeNames(
	    const std::vector<FFunctionParam>& Parameters,
	    const std::string& DeclModulePath,
	    const std::unordered_map<std::string, std::string>& Subst) const
	{
		std::vector<std::string> Out;
		Out.reserve(Parameters.size());
		for (const FFunctionParam& Param : Parameters)
		{
			std::string TypeName = QualifyTypeName(
			    NormalizeTypeAlias(
			        SubstituteTypeNameWith(Param.TypeName, Subst),
			        DeclModulePath),
			    DeclModulePath);
			if (Param.bIsVariadic)
			{
				TypeName += "...";
			}
			else if (Param.bIsRef)
			{
				TypeName = Param.bIsConst ? ("const ref " + TypeName) : ("ref " + TypeName);
			}
			Out.push_back(std::move(TypeName));
		}
		return Out;
	}

	bool VeroScriptCompiler::ValidateConversionMethods(const std::vector<FMethodDecl>& Methods,
	    const std::string& DeclModulePath, const std::unordered_map<std::string, std::string>& Subst) const
	{
		std::unordered_set<std::string> Targets;
		for (const auto& Method : Methods)
		{
			if (Method.Name.rfind("operator conv ", 0) != 0)
			{
				continue;
			}

			auto Target = CanonicalTypeNameForCoercion(QualifyTypeName(
			    NormalizeTypeAlias(SubstituteTypeNameWith(Method.ReturnType, Subst), DeclModulePath),
			    DeclModulePath));
			Target += Method.bIsConst ? "#const" : "#mutable";
			if (!Targets.insert(Target).second)
			{
				ReportError("ambiguous conversion methods to the same target type", Method.Location);
				return false;
			}
		}
		return !HasError();
	}

	std::string VeroScriptCompiler::MakeMethodCallLeafName(
	    const std::string& MethodName,
	    const FMethodDecl& Method,
	    const std::string& DeclModulePath,
	    const std::unordered_map<std::string, std::string>& Subst) const
	{
		std::string DispatchName = MethodSpecializationKeys.count(&Method) != 0 ? Method.Name : MethodName;
		if (MethodName.rfind("operator conv ", 0) == 0)
		{
			DispatchName = ConversionOperatorMethodName(CanonicalTypeNameForCoercion(QualifyTypeName(
			    NormalizeTypeAlias(SubstituteTypeNameWith(Method.ReturnType, Subst), DeclModulePath),
			    DeclModulePath)));
		}

		std::string Leaf = DispatchName +
		    EncodeMethodParamSuffix(CanonicalMethodParamTypeNames(Method, DeclModulePath, Subst));
		if (Method.bIsConst)
		{
			Leaf.push_back('c');
		}
		return Leaf;
	}

	bool VeroScriptCompiler::MethodSignaturesMatch(
	    const FMethodDecl& Left,
	    const FMethodDecl& Right,
	    const std::string& LeftModulePath,
	    const std::string& RightModulePath) const
	{
		static const std::unordered_map<std::string, std::string> EmptySubst;
		return MethodSignaturesMatch(Left, Right, LeftModulePath, RightModulePath, EmptySubst);
	}

}
