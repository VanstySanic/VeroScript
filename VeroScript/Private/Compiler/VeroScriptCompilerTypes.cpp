// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"

#include "VeroScriptAst.h"
#include "TypeSupport/VeroScriptRuntimeTypeScope.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptRuntime.h"

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
	bool VeroScriptCompiler::EnsureStructInstantiation(
	    const FStructDecl& StructDecl,
	    const std::vector<std::string>& ConcreteTypes,
	    std::uint16_t& OutTypeIndex,
	    const FSourceLocation& Location)
	{
		const std::string BaseLinkageName =
		    GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name);
		const std::string MangledName = MangleGenericInstanceName(BaseLinkageName, ConcreteTypes);
		bool bBuiltinWeak = false;
		for (const FStructDecl& Candidate : BuiltinStructTemplates)
		{
			if (&Candidate == &StructDecl && Candidate.Name == "weak")
			{
				bBuiltinWeak = true;
				break;
			}
		}
		if (bBuiltinWeak && ConcreteTypes.size() == 1)
		{
			const std::string& TargetType = ConcreteTypes[0];
			const FHostTypeDesc* const HostType =
			    ActiveBindings ? ActiveBindings->FindType(TargetType) : nullptr;
			const bool bScriptReference =
			    (IsClassOrStructType(TargetType) && !IsStructType(TargetType)) || IsInterfaceTypeName(TargetType);
			const bool bHostReference =
			    HostType && HostType->Kind == EHostTypeKind::Class;
			if (!bScriptReference && !bHostReference)
			{
				ReportError(
				    "weak<T> requires a class, interface, or host class type",
				    Location);
				return false;
			}
		}
		const auto Registered = FindRegisteredTypeIndex(MangledName);
		if (Registered != static_cast<std::uint16_t>(-1))
		{
			OutTypeIndex = Registered;
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

		if (!CheckTypeParamBounds(
		        TemplateParameterNames(StructDecl),
		        StructDecl.Bounds,
		        ConcreteTypes,
		        StructDecl.ModulePath,
		        Location,
		        "type"))
		{
			return false;
		}

		InstantiatingGenerics.insert(MangledName);
		const std::unordered_map<std::string, std::string> PreviousSubst = ActiveTypeSubst;
		if (!ApplyTypeParamSubst(
		        TemplateParameterNames(StructDecl),
		        ConcreteTypes,
		        ActiveTypeSubst,
		        Location))
		{
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}

		if (!StructDecl.Scope.Id.empty() && !CheckScopedTypeRequirements(StructDecl.Scope.Id, {}, ActiveTypeSubst))
		{
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}
		const std::string PreviousModulePath = CurrentModulePath;
		CurrentModulePath = StructDecl.ModulePath;
		if (!ValidateConversionMethods(StructDecl.Methods, StructDecl.ModulePath, ActiveTypeSubst))
		{
			CurrentModulePath = PreviousModulePath;
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}
		TypesInProgress.insert(MangledName);

		FBytecodeTypeDesc TypeDesc;
		TypeDesc.Name = MangledName;
		TypeDesc.Kind = EScriptObjectKind::Struct;
		TypeDesc.bIsStatic = StructDecl.bIsStatic;
		TypeDesc.DeclMeta = StructDecl.DeclMeta;
		PopulateSourceTypeScope(StructDecl.Scope, ConcreteTypes, TypeDesc.Scope, TypeDesc.NestedTypes);
		ApplyTemplateInstanceMetadata(TypeDesc.TemplateInstance, &StructDecl.DeclMeta);
		for (const FUsingMemberDecl& Using : StructDecl.UsingMembers)
		{
			TypeDesc.UsingMembers.push_back({Using.BaseTypeName,
			    Using.MemberName, Using.Access});
		}

		std::vector<std::string> SubstitutedBases;
		if (!StructDecl.BaseTypeName.empty())
		{
			SubstitutedBases.push_back(SubstituteTypeName(StructDecl.BaseTypeName));
		}
		std::vector<std::string> SubstitutedInterfaces;
		SubstitutedInterfaces.reserve(StructDecl.InterfaceTypeNames.size());
		for (const std::string& InterfaceName : StructDecl.InterfaceTypeNames)
		{
			SubstitutedInterfaces.push_back(SubstituteTypeName(InterfaceName));
		}
		// Field defaults may read this owner's statics while its layout is incomplete.
		// Publish their storage with the closed substitution, without running initializers.
		if (!RegisterStaticFieldsForTypeLinkage(MangledName, StructDecl.ModulePath, StructDecl.Fields) || !AppendInheritedAndOwnFields(TypeDesc, MangledName, SubstitutedBases, EScriptObjectKind::Struct, StructDecl.Fields, StructDecl.ModulePath, Location, SubstitutedInterfaces, std::vector<EMemberAccess>{StructDecl.BaseInheritanceAccess}, false))
		{
			TypesInProgress.erase(MangledName);
			CurrentModulePath = PreviousModulePath;
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}
		if (StructDecl.Name == "type" && StructDecl.ModulePath.empty() && ConcreteTypes.size() == 1 && TypeDesc.Fields.size() >= 2 && TypeDesc.Fields[0].Name == "name" && TypeDesc.Fields[1].Name == "qualified_name")
		{
			TypeDesc.Fields[0].DefaultValue.Kind = EConstantKind::Str;
			TypeDesc.Fields[0].DefaultValue.StringIndex =
			    AddString(ModuleMeta::TypeSimpleName(ConcreteTypes[0]));
			TypeDesc.Fields[0].bHasDefault = true;
			TypeDesc.Fields[1].DefaultValue.Kind = EConstantKind::Str;
			TypeDesc.Fields[1].DefaultValue.bIsLinkageString = true;
			TypeDesc.Fields[1].DefaultValue.StringIndex = AddString(ConcreteTypes[0]);
			TypeDesc.Fields[1].bHasDefault = true;
		}
		for (const std::uint16_t BaseTypeIndex : TypeDesc.BaseTypeIndices)
		{
			if (BaseTypeIndex >= Module->Types.size())
			{
				continue;
			}
			for (const std::uint16_t InheritedIface :
			    Module->Types[BaseTypeIndex].ImplementedInterfaceIndices)
			{
				bool bAlready = false;
				for (const std::uint16_t Existing : TypeDesc.ImplementedInterfaceIndices)
				{
					if (Existing == InheritedIface)
					{
						bAlready = true;
						break;
					}
				}
				if (!bAlready)
				{
					TypeDesc.ImplementedInterfaceIndices.push_back(InheritedIface);
				}
			}
		}

		TypesInProgress.erase(MangledName);
		{
			std::vector<std::uint16_t> OwnInterfaces;
			if (!ResolveInheritanceInterfaces(
			        SubstitutedInterfaces,
			        StructDecl.ModulePath,
			        Location,
			        OwnInterfaces))
			{
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			for (const std::uint16_t InterfaceIndex : OwnInterfaces)
			{
				bool bAlready = false;
				for (const std::uint16_t Existing : TypeDesc.ImplementedInterfaceIndices)
				{
					if (Existing == InterfaceIndex)
					{
						bAlready = true;
						break;
					}
				}
				if (!bAlready)
				{
					TypeDesc.ImplementedInterfaceIndices.push_back(InterfaceIndex);
				}
			}
		}
		if (!VerifyInterfaceConformance(
		        MangledName,
		        SubstitutedInterfaces,
		        TypeDesc.ImplementedInterfaceIndices,
		        StructDecl.ModulePath,
		        Location))
		{
			CurrentModulePath = PreviousModulePath;
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}
		const std::size_t FunctionCountBeforeMethods = Module->Functions.size();
		Module->Types.push_back(std::move(TypeDesc));
		OutTypeIndex = static_cast<std::uint16_t>(Module->Types.size() - 1);

		// Publish complete method declarations before constructors or earlier
		// methods can inspect this closed owner's callable signatures.
		for (const FMethodDecl& Method : StructDecl.Methods)
		{
			if (!TemplateParameterNames(Method).empty())
			{
				continue;
			}
			const auto MethodName = MakeMethodFunctionName(MangledName, Method.Name,
			    CanonicalMethodParamTypeNames(Method, StructDecl.ModulePath, ActiveTypeSubst), Method.bIsConst);
			std::string BaseLinkage;
			const FMethodDecl* BaseMethod = nullptr;
			const bool bIsVirtual = !Method.bIsStatic && (Method.bVirtual || Method.bFinal || FindVirtualBaseMethod(MangledName, Method, StructDecl.ModulePath, BaseLinkage, BaseMethod));
			if (!EnsureMethodDeclarationStub(MethodName, Method, OutTypeIndex, bIsVirtual, StructDecl.ModulePath))
			{
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
		}

		// Defaults can call this closed owner's methods. Keep dependency discovery
		// before static initialization, while publishing only complete layouts.
		if (!CompileOwnFieldDefaults(MangledName, OutTypeIndex, StructDecl.Fields) ||
		    !QueueOrCompileStaticFieldInitializersForTypeLinkage(
		        MangledName, StructDecl.ModulePath, StructDecl.Fields))
		{
			CurrentModulePath = PreviousModulePath;
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}

		for (const FConstructorDecl& Constructor : StructDecl.Constructors)
		{
			if (Constructor.bDeleted)
			{
				const std::string CtorName = MakeConstructorKey(
				    MangledName,
				    StructDecl.Name,
				    Constructor,
				    StructDecl.ModulePath,
				    ActiveTypeSubst);
				if (EnsureDeletedFunctionDeclaration(
				        CtorName, "void", Constructor.Parameters, true, OutTypeIndex,
				        StructDecl.ModulePath, Constructor.Location, false,
				        Constructor.DeclMeta))
				{
					continue;
				}
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			if (!CompileConstructor(
			        MangledName,
			        StructDecl.Name,
			        Constructor,
			        StructDecl.Fields,
			        SubstitutedBases,
			        OutTypeIndex,
			        StructDecl.ModulePath,
			        Constructor.Location))
			{
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
		}

		for (const FMethodDecl& Method : StructDecl.Methods)
		{
			if (!TemplateParameterNames(Method).empty())
			{
				continue;
			}
			if (ReportIfDeletedMethodOverridesVirtual(
			        MangledName, Method, StructDecl.ModulePath))
			{
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			if (Method.bDeleted)
			{
				const std::string MethodName = MakeMethodFunctionName(
				    MangledName,
				    Method.Name,
				    CanonicalMethodParamTypeNames(
				        Method, StructDecl.ModulePath, ActiveTypeSubst),
				    Method.bIsConst);
				if (EnsureDeletedFunctionDeclaration(
				        MethodName,
				        Method.ReturnType,
				        Method.Parameters,
				        !Method.bIsStatic,
				        Method.bIsStatic ? 0 : OutTypeIndex,
				        StructDecl.ModulePath,
				        Method.Location,
				        Method.bIsConst,
				        Method.DeclMeta))
				{
					continue;
				}
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			if (!Method.Body && !Method.bPureVirtual)
			{
				ReportError("generic method declared without an in-class definition: " + Method.Name, Method.Location);
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			const std::string MethodName = MakeMethodFunctionName(
			    MangledName,
			    Method.Name,
			    CanonicalMethodParamTypeNames(Method, StructDecl.ModulePath, ActiveTypeSubst), Method.bIsConst);
			// Static member functions compile like free functions; no virtual checks.
			if (Method.bIsStatic)
			{
				if (!CompileFunction(
				        MethodName,
				        Method.ReturnType,
				        Method.Parameters,
				        *Method.Body,
				        false,
				        0,
				        StructDecl.ModulePath,
				        Method.Location,
				        false, nullptr, Method.bIsConst, &Method.DeclMeta, nullptr, false, Method.bConstexpr))
				{
					while (Module->Functions.size() > FunctionCountBeforeMethods)
					{
						Module->FunctionIndices.erase(Module->Functions.back().Name);
						Module->Functions.pop_back();
					}
					Module->Types.pop_back();
					CurrentModulePath = PreviousModulePath;
					ActiveTypeSubst = PreviousSubst;
					InstantiatingGenerics.erase(MangledName);
					return false;
				}
				continue;
			}
			std::string BaseLinkage;
			const FMethodDecl* BaseMethod = nullptr;
			const bool bFoundVirtualBase = FindVirtualBaseMethod(
			    MangledName,
			    Method,
			    StructDecl.ModulePath,
			    BaseLinkage,
			    BaseMethod);
			if (!bFoundVirtualBase && ReportIncompatibleVirtualOverride(MangledName, Method, StructDecl.ModulePath, Method.Location))
			{
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			if (Method.bOverride && !bFoundVirtualBase)
			{
				ReportErrorId(
				    "diag.override_method_has_no_matching_virtual_base_method",
				    Method.Location,
				    {Method.Name});
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			if (ReportIfCannotOverrideFinalMethod(
			        bFoundVirtualBase,
			        BaseMethod,
			        Method,
			        Method.Location))
			{
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			const bool bIsVirtual = Method.bVirtual || Method.bFinal || bFoundVirtualBase;
			if (Method.bPureVirtual)
			{
				if (EnsurePureVirtualFunctionDeclaration(
				        MethodName,
				        Method.ReturnType,
				        Method.Parameters,
				        OutTypeIndex,
				        StructDecl.ModulePath,
				        Method.Location,
				        Method.bIsConst,
				        Method.Access,
				        Method.DeclMeta))
				{
					continue;
				}
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			if (!CompileFunction(
			        MethodName,
			        Method.ReturnType,
			        Method.Parameters,
			        *Method.Body,
			        true,
			        OutTypeIndex,
			        StructDecl.ModulePath,
			        Method.Location,
			        bIsVirtual, nullptr, Method.bIsConst, &Method.DeclMeta, nullptr, false, Method.bConstexpr))
			{
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
		}
		if (StructDecl.Destructor && !CompileDestructor(MangledName, StructDecl.Name, *StructDecl.Destructor, OutTypeIndex, StructDecl.ModulePath))
		{
			while (Module->Functions.size() > FunctionCountBeforeMethods)
			{
				Module->FunctionIndices.erase(Module->Functions.back().Name);
				Module->Functions.pop_back();
			}
			Module->Types.pop_back();
			CurrentModulePath = PreviousModulePath;
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}

		Module->Types[OutTypeIndex].bIsAbstract =
		    Module->Types[OutTypeIndex].bDestructorPureVirtual || TypeHasUnimplementedPureVirtual(MangledName);
		CurrentModulePath = PreviousModulePath;
		ActiveTypeSubst = PreviousSubst;
		InstantiatingGenerics.erase(MangledName);
		return true;
	}

	bool VeroScriptCompiler::EnsureClassInstantiation(
	    const FClassDecl& ClassDecl,
	    const std::vector<std::string>& ConcreteTypes,
	    std::uint16_t& OutTypeIndex,
	    const FSourceLocation& Location)
	{
		const std::string BaseLinkageName =
		    GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name);
		const std::string MangledName = MangleGenericInstanceName(BaseLinkageName, ConcreteTypes);
		const auto Registered = FindRegisteredTypeIndex(MangledName);
		if (Registered != static_cast<std::uint16_t>(-1))
		{
			OutTypeIndex = Registered;
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

		if (!CheckTypeParamBounds(
		        TemplateParameterNames(ClassDecl),
		        ClassDecl.Bounds,
		        ConcreteTypes,
		        ClassDecl.ModulePath,
		        Location,
		        "type"))
		{
			return false;
		}

		InstantiatingGenerics.insert(MangledName);
		const std::unordered_map<std::string, std::string> PreviousSubst = ActiveTypeSubst;
		if (!ApplyTypeParamSubst(
		        TemplateParameterNames(ClassDecl),
		        ConcreteTypes,
		        ActiveTypeSubst,
		        Location))
		{
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}

		if (!ClassDecl.Scope.Id.empty() && !CheckScopedTypeRequirements(ClassDecl.Scope.Id, {}, ActiveTypeSubst))
		{
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}
		const std::string PreviousModulePath = CurrentModulePath;
		CurrentModulePath = ClassDecl.ModulePath;
		if (!ValidateConversionMethods(ClassDecl.Methods, ClassDecl.ModulePath, ActiveTypeSubst))
		{
			CurrentModulePath = PreviousModulePath;
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}
		TypesInProgress.insert(MangledName);

		FBytecodeTypeDesc TypeDesc;
		TypeDesc.Name = MangledName;
		TypeDesc.Kind = EScriptObjectKind::Class;
		TypeDesc.DeclMeta = ClassDecl.DeclMeta;
		PopulateSourceTypeScope(ClassDecl.Scope, ConcreteTypes, TypeDesc.Scope, TypeDesc.NestedTypes);
		ApplyTemplateInstanceMetadata(TypeDesc.TemplateInstance, &ClassDecl.DeclMeta);
		for (const FUsingMemberDecl& Using : ClassDecl.UsingMembers)
		{
			TypeDesc.UsingMembers.push_back({Using.BaseTypeName,
			    Using.MemberName, Using.Access});
		}
		TypeDesc.bIsStatic = ClassDecl.bIsStatic;

		std::vector<std::string> SubstitutedBases;
		SubstitutedBases.reserve(ClassDecl.BaseTypeNames.size());
		for (const std::string& BaseName : ClassDecl.BaseTypeNames)
		{
			SubstitutedBases.push_back(SubstituteTypeName(BaseName));
		}
		if (!CheckDiamondInheritance(
		        SubstitutedBases,
		        ClassDecl.ModulePath,
		        Location))
		{
			TypesInProgress.erase(MangledName);
			CurrentModulePath = PreviousModulePath;
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}
		std::vector<std::string> SubstitutedInterfaces;
		SubstitutedInterfaces.reserve(ClassDecl.InterfaceTypeNames.size());
		for (const std::string& InterfaceName : ClassDecl.InterfaceTypeNames)
		{
			SubstitutedInterfaces.push_back(SubstituteTypeName(InterfaceName));
		}
		// Field defaults may read this owner's statics while its layout is incomplete.
		// Publish their storage with the closed substitution, without running initializers.
		if (!RegisterStaticFieldsForTypeLinkage(MangledName, ClassDecl.ModulePath, ClassDecl.Fields) || !AppendInheritedAndOwnFields(TypeDesc, MangledName, SubstitutedBases, EScriptObjectKind::Class, ClassDecl.Fields, ClassDecl.ModulePath, Location, SubstitutedInterfaces, ClassDecl.BaseInheritanceAccess, false))
		{
			TypesInProgress.erase(MangledName);
			CurrentModulePath = PreviousModulePath;
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}
		for (const std::uint16_t BaseTypeIndex : TypeDesc.BaseTypeIndices)
		{
			if (BaseTypeIndex >= Module->Types.size())
			{
				continue;
			}
			for (const std::uint16_t InheritedIface :
			    Module->Types[BaseTypeIndex].ImplementedInterfaceIndices)
			{
				bool bAlready = false;
				for (const std::uint16_t Existing : TypeDesc.ImplementedInterfaceIndices)
				{
					if (Existing == InheritedIface)
					{
						bAlready = true;
						break;
					}
				}
				if (!bAlready)
				{
					TypeDesc.ImplementedInterfaceIndices.push_back(InheritedIface);
				}
			}
		}

		TypesInProgress.erase(MangledName);
		{
			std::vector<std::uint16_t> OwnInterfaces;
			if (!ResolveInheritanceInterfaces(
			        SubstitutedInterfaces,
			        ClassDecl.ModulePath,
			        Location,
			        OwnInterfaces))
			{
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			for (const std::uint16_t InterfaceIndex : OwnInterfaces)
			{
				bool bAlready = false;
				for (const std::uint16_t Existing : TypeDesc.ImplementedInterfaceIndices)
				{
					if (Existing == InterfaceIndex)
					{
						bAlready = true;
						break;
					}
				}
				if (!bAlready)
				{
					TypeDesc.ImplementedInterfaceIndices.push_back(InterfaceIndex);
				}
			}
		}
		if (!VerifyInterfaceConformance(
		        MangledName,
		        SubstitutedInterfaces,
		        TypeDesc.ImplementedInterfaceIndices,
		        ClassDecl.ModulePath,
		        Location))
		{
			CurrentModulePath = PreviousModulePath;
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}
		const std::size_t FunctionCountBeforeMethods = Module->Functions.size();
		Module->Types.push_back(std::move(TypeDesc));
		OutTypeIndex = static_cast<std::uint16_t>(Module->Types.size() - 1);

		// Publish complete method declarations before constructors or earlier
		// methods can inspect this closed owner's callable signatures.
		for (const FMethodDecl& Method : ClassDecl.Methods)
		{
			if (!TemplateParameterNames(Method).empty())
			{
				continue;
			}
			const auto MethodName = MakeMethodFunctionName(MangledName, Method.Name,
			    CanonicalMethodParamTypeNames(Method, ClassDecl.ModulePath, ActiveTypeSubst), Method.bIsConst);
			std::string BaseLinkage;
			const FMethodDecl* BaseMethod = nullptr;
			const bool bIsVirtual = !Method.bIsStatic && (Method.bVirtual || Method.bFinal || FindVirtualBaseMethod(MangledName, Method, ClassDecl.ModulePath, BaseLinkage, BaseMethod));
			if (!EnsureMethodDeclarationStub(MethodName, Method, OutTypeIndex, bIsVirtual, ClassDecl.ModulePath))
			{
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
		}

		if (!CompileOwnFieldDefaults(MangledName, OutTypeIndex, ClassDecl.Fields) ||
		    !QueueOrCompileStaticFieldInitializersForTypeLinkage(
		        MangledName, ClassDecl.ModulePath, ClassDecl.Fields))
		{
			CurrentModulePath = PreviousModulePath;
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}

		for (const FConstructorDecl& Constructor : ClassDecl.Constructors)
		{
			if (Constructor.bDeleted)
			{
				const std::string CtorName = MakeConstructorKey(
				    MangledName,
				    ClassDecl.Name,
				    Constructor,
				    ClassDecl.ModulePath,
				    ActiveTypeSubst);
				if (EnsureDeletedFunctionDeclaration(
				        CtorName, "void", Constructor.Parameters, true, OutTypeIndex,
				        ClassDecl.ModulePath, Constructor.Location, false,
				        Constructor.DeclMeta))
				{
					continue;
				}
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			if (!CompileConstructor(
			        MangledName,
			        ClassDecl.Name,
			        Constructor,
			        ClassDecl.Fields,
			        SubstitutedBases,
			        OutTypeIndex,
			        ClassDecl.ModulePath,
			        Constructor.Location))
			{
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
		}

		if (ClassDecl.ClassDefaults)
		{
			const std::string DefaultsName = MakeMethodFunctionName(
			    MangledName, "@defaults", {}, false);
			const std::vector<FFunctionParam> NoParameters;
			if (!CompileFunction(
			        DefaultsName,
			        "void",
			        NoParameters,
			        *ClassDecl.ClassDefaults,
			        true,
			        OutTypeIndex,
			        ClassDecl.ModulePath,
			        ClassDecl.ClassDefaults->Location,
			        false))
			{
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			auto& Meta = Module->Types[OutTypeIndex].DeclMeta;
			Meta.erase(
			    std::remove_if(
			        Meta.begin(), Meta.end(),
			        [](const auto& Entry)
			{
				return Entry.first == ClassDefaultsLinkageMetaKey;
			}),
			    Meta.end());
			Meta.emplace_back(std::string(ClassDefaultsLinkageMetaKey), DefaultsName);
		}

		for (const FMethodDecl& Method : ClassDecl.Methods)
		{
			if (!TemplateParameterNames(Method).empty())
			{
				continue;
			}
			if (ReportIfDeletedMethodOverridesVirtual(
			        MangledName, Method, ClassDecl.ModulePath))
			{
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			if (Method.bDeleted)
			{
				const std::string MethodName = MakeMethodFunctionName(
				    MangledName,
				    Method.Name,
				    CanonicalMethodParamTypeNames(
				        Method, ClassDecl.ModulePath, ActiveTypeSubst),
				    Method.bIsConst);
				if (EnsureDeletedFunctionDeclaration(
				        MethodName,
				        Method.ReturnType,
				        Method.Parameters,
				        !Method.bIsStatic,
				        Method.bIsStatic ? 0 : OutTypeIndex,
				        ClassDecl.ModulePath,
				        Method.Location,
				        Method.bIsConst,
				        Method.DeclMeta))
				{
					continue;
				}
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			if (!Method.Body && !Method.bPureVirtual)
			{
				ReportError("generic method declared without an in-class definition: " + Method.Name, Method.Location);
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			const std::string MethodName = MakeMethodFunctionName(
			    MangledName,
			    Method.Name,
			    CanonicalMethodParamTypeNames(Method, ClassDecl.ModulePath, ActiveTypeSubst), Method.bIsConst);
			// Static member functions compile like free functions; no virtual checks.
			if (Method.bIsStatic)
			{
				if (!CompileFunction(
				        MethodName,
				        Method.ReturnType,
				        Method.Parameters,
				        *Method.Body,
				        false,
				        0,
				        ClassDecl.ModulePath,
				        Method.Location,
				        false, nullptr, Method.bIsConst, &Method.DeclMeta, nullptr, false, Method.bConstexpr))
				{
					while (Module->Functions.size() > FunctionCountBeforeMethods)
					{
						Module->FunctionIndices.erase(Module->Functions.back().Name);
						Module->Functions.pop_back();
					}
					Module->Types.pop_back();
					CurrentModulePath = PreviousModulePath;
					ActiveTypeSubst = PreviousSubst;
					InstantiatingGenerics.erase(MangledName);
					return false;
				}
				continue;
			}
			std::string BaseLinkage;
			const FMethodDecl* BaseMethod = nullptr;
			const bool bFoundVirtualBase = FindVirtualBaseMethod(
			    MangledName,
			    Method,
			    ClassDecl.ModulePath,
			    BaseLinkage,
			    BaseMethod);
			if (!bFoundVirtualBase && ReportIncompatibleVirtualOverride(MangledName, Method, ClassDecl.ModulePath, Method.Location))
			{
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			if (Method.bOverride && !bFoundVirtualBase)
			{
				ReportErrorId(
				    "diag.override_method_has_no_matching_virtual_base_method",
				    Method.Location,
				    {Method.Name});
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			if (ReportIfCannotOverrideFinalMethod(
			        bFoundVirtualBase,
			        BaseMethod,
			        Method,
			        Method.Location))
			{
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			const bool bIsVirtual = Method.bVirtual || Method.bFinal || bFoundVirtualBase;
			if (Method.bPureVirtual)
			{
				if (EnsurePureVirtualFunctionDeclaration(
				        MethodName,
				        Method.ReturnType,
				        Method.Parameters,
				        OutTypeIndex,
				        ClassDecl.ModulePath,
				        Method.Location,
				        Method.bIsConst,
				        Method.Access,
				        Method.DeclMeta))
				{
					continue;
				}
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			if (!CompileFunction(
			        MethodName,
			        Method.ReturnType,
			        Method.Parameters,
			        *Method.Body,
			        true,
			        OutTypeIndex,
			        ClassDecl.ModulePath,
			        Method.Location,
			        bIsVirtual, nullptr, Method.bIsConst, &Method.DeclMeta, nullptr, false, Method.bConstexpr))
			{
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Types.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
		}
		if (ClassDecl.Destructor && !CompileDestructor(MangledName, ClassDecl.Name, *ClassDecl.Destructor, OutTypeIndex, ClassDecl.ModulePath))
		{
			while (Module->Functions.size() > FunctionCountBeforeMethods)
			{
				Module->FunctionIndices.erase(Module->Functions.back().Name);
				Module->Functions.pop_back();
			}
			Module->Types.pop_back();
			CurrentModulePath = PreviousModulePath;
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}

		Module->Types[OutTypeIndex].bIsAbstract =
		    Module->Types[OutTypeIndex].bDestructorPureVirtual || TypeHasUnimplementedPureVirtual(MangledName);
		CurrentModulePath = PreviousModulePath;
		ActiveTypeSubst = PreviousSubst;
		InstantiatingGenerics.erase(MangledName);
		return true;
	}

	bool VeroScriptCompiler::EnsureStructTypeResolved(
	    const std::string& TypeName,
	    std::string& OutLinkageName,
	    std::uint16_t* OutTypeIndex,
	    const FSourceLocation& Location)
	{
		if (!ActiveProgram || !Module || TypeName.empty())
		{
			return false;
		}

		const auto Registered = FindRegisteredTypeIndex(TypeName);
		if (Registered != static_cast<std::uint16_t>(-1))
		{
			const bool bCanonical = FindGenericTypeMarker(TypeName) != std::string::npos || TypeName.find('\x1F') != std::string::npos;
			if (bCanonical || ResolveTypeLinkageName(TypeName, CurrentModulePath) == TypeName)
			{
				OutLinkageName = TypeName;
				if (OutTypeIndex)
				{
					*OutTypeIndex = Registered;
				}
				return true;
			}
		}

		const std::string TemplateBase = GenericTemplateBaseLinkage(TypeName);
		if (!TemplateBase.empty())
		{
			const std::vector<std::string> DecodedConcreteTypes = DecodeGenericConcreteTypes(TypeName);
			for (const FStructDecl& Candidate : ActiveProgram->Structs)
			{
				if (GetSymbolLinkageName(Candidate.ModulePath, Candidate.bExported, Candidate.Name) != TemplateBase || TemplateParameterNames(Candidate).empty())
				{
					continue;
				}
				if (!(TemplateParameterNames(Candidate).size() == DecodedConcreteTypes.size()))
				{
					continue;
				}
				std::vector<std::string> ConcreteTypes = DecodedConcreteTypes;
				if (!CompleteGenericTypeArguments(
				        TemplateParameterNames(Candidate),
				        TemplateParameterDefaults(Candidate),
				        Candidate.ModulePath,
				        ConcreteTypes,
				        Location,
				        Candidate.Name, &Candidate.TemplateParameters, true))
				{
					return false;
				}
				std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
				if (!EnsureStructInstantiation(Candidate, ConcreteTypes, TypeIndex, Location))
				{
					return false;
				}
				OutLinkageName = Module->Types[TypeIndex].Name;
				if (OutTypeIndex)
				{
					*OutTypeIndex = TypeIndex;
				}
				return true;
			}
			for (const FClassDecl& Candidate : ActiveProgram->Classes)
			{
				if (GetSymbolLinkageName(Candidate.ModulePath, Candidate.bExported, Candidate.Name) != TemplateBase || TemplateParameterNames(Candidate).empty())
				{
					continue;
				}
				if (!(TemplateParameterNames(Candidate).size() == DecodedConcreteTypes.size()))
				{
					continue;
				}
				std::vector<std::string> ConcreteTypes = DecodedConcreteTypes;
				if (!CompleteGenericTypeArguments(
				        TemplateParameterNames(Candidate),
				        TemplateParameterDefaults(Candidate),
				        Candidate.ModulePath,
				        ConcreteTypes,
				        Location,
				        Candidate.Name, &Candidate.TemplateParameters, true))
				{
					return false;
				}
				std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
				if (!EnsureClassInstantiation(Candidate, ConcreteTypes, TypeIndex, Location))
				{
					return false;
				}
				OutLinkageName = Module->Types[TypeIndex].Name;
				if (OutTypeIndex)
				{
					*OutTypeIndex = TypeIndex;
				}
				return true;
			}
			for (const FClassDecl& Candidate : BuiltinClassTemplates)
			{
				if (GetSymbolLinkageName(Candidate.ModulePath, Candidate.bExported, Candidate.Name) != TemplateBase || TemplateParameterNames(Candidate).empty())
				{
					continue;
				}
				if (!(TemplateParameterNames(Candidate).size() == DecodedConcreteTypes.size()))
				{
					continue;
				}
				std::vector<std::string> ConcreteTypes = DecodedConcreteTypes;
				if (!CompleteGenericTypeArguments(
				        TemplateParameterNames(Candidate),
				        TemplateParameterDefaults(Candidate),
				        Candidate.ModulePath,
				        ConcreteTypes,
				        Location,
				        Candidate.Name, &Candidate.TemplateParameters, true))
				{
					return false;
				}
				std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
				if (!EnsureClassInstantiation(Candidate, ConcreteTypes, TypeIndex, Location))
				{
					return false;
				}
				OutLinkageName = Module->Types[TypeIndex].Name;
				if (OutTypeIndex)
				{
					*OutTypeIndex = TypeIndex;
				}
				return true;
			}
			for (const FStructDecl& Candidate : BuiltinStructTemplates)
			{
				if (GetSymbolLinkageName(Candidate.ModulePath, Candidate.bExported, Candidate.Name) != TemplateBase || TemplateParameterNames(Candidate).empty())
				{
					continue;
				}
				if (!(TemplateParameterNames(Candidate).size() == DecodedConcreteTypes.size()))
				{
					continue;
				}
				std::vector<std::string> ConcreteTypes = DecodedConcreteTypes;
				if (!CompleteGenericTypeArguments(
				        TemplateParameterNames(Candidate),
				        TemplateParameterDefaults(Candidate),
				        Candidate.ModulePath,
				        ConcreteTypes,
				        Location,
				        Candidate.Name, &Candidate.TemplateParameters, true))
				{
					return false;
				}
				std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
				if (!EnsureStructInstantiation(Candidate, ConcreteTypes, TypeIndex, Location))
				{
					return false;
				}
				OutLinkageName = Module->Types[TypeIndex].Name;
				if (OutTypeIndex)
				{
					*OutTypeIndex = TypeIndex;
				}
				return true;
			}
			return false;
		}

		std::string BaseName;
		std::vector<std::string> ArgTypes;
		if (!SplitUserGenericTypeApp(TypeName, BaseName, ArgTypes))
		{
			return false;
		}

		if (const FStructDecl* const StructDecl = FindGenericStructDecl(BaseName, CurrentModulePath, ArgTypes.size()))
		{
			if (TemplateParameterNames(*StructDecl).empty())
			{
				ReportErrorId("diag.type_is_not_generic", Location, {BaseName});
				return false;
			}
			std::vector<std::string> ConcreteTypes = ArgTypes;
			if (StructDecl->TemplateParameters.IsEmpty())
			{
				for (auto& ConcreteType : ConcreteTypes)
				{
					ConcreteType = CanonicalTypeNameForCoercion(
					    QualifyTypeName(ConcreteType, CurrentModulePath));
				}
			}
			if (!CompleteGenericTypeArguments(
			        TemplateParameterNames(*StructDecl),
			        TemplateParameterDefaults(*StructDecl),
			        StructDecl->ModulePath,
			        ConcreteTypes,
			        Location,
			        BaseName, &StructDecl->TemplateParameters))
			{
				return false;
			}
			// Captured defaults must be completed in their declaration environment
			// before they are validated as concrete field types.
			for (std::size_t Index = 0; Index < ConcreteTypes.size(); ++Index)
			{
				if (!StructDecl->TemplateParameters.IsEmpty() && StructDecl->TemplateParameters.GetParameters()[Index].Kind == ETemplateParameterKind::Value)
				{
					continue;
				}
				const auto& ConcreteType = ConcreteTypes[Index];
				if (ConcreteType != "void" && !ValidateFieldTypeName(ConcreteType, Location))
				{
					return false;
				}
			}
			std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
			if (!EnsureStructInstantiation(*StructDecl, ConcreteTypes, TypeIndex, Location))
			{
				return false;
			}
			OutLinkageName = Module->Types[TypeIndex].Name;
			if (OutTypeIndex)
			{
				*OutTypeIndex = TypeIndex;
			}
			return true;
		}

		if (const FClassDecl* const ClassDecl = FindGenericClassDecl(BaseName, CurrentModulePath, ArgTypes.size()))
		{
			if (TemplateParameterNames(*ClassDecl).empty())
			{
				ReportErrorId("diag.type_is_not_generic", Location, {BaseName});
				return false;
			}
			std::vector<std::string> ConcreteTypes = ArgTypes;
			if (ClassDecl->TemplateParameters.IsEmpty())
			{
				for (auto& ConcreteType : ConcreteTypes)
				{
					ConcreteType = CanonicalTypeNameForCoercion(
					    QualifyTypeName(ConcreteType, CurrentModulePath));
				}
			}
			if (!CompleteGenericTypeArguments(
			        TemplateParameterNames(*ClassDecl),
			        TemplateParameterDefaults(*ClassDecl),
			        ClassDecl->ModulePath,
			        ConcreteTypes,
			        Location,
			        BaseName, &ClassDecl->TemplateParameters))
			{
				return false;
			}
			// Captured defaults must be completed in their declaration environment
			// before they are validated as concrete field types.
			for (std::size_t Index = 0; Index < ConcreteTypes.size(); ++Index)
			{
				if (!ClassDecl->TemplateParameters.IsEmpty() && ClassDecl->TemplateParameters.GetParameters()[Index].Kind == ETemplateParameterKind::Value)
				{
					continue;
				}
				const auto& ConcreteType = ConcreteTypes[Index];
				if (ConcreteType != "void" && !ValidateFieldTypeName(ConcreteType, Location))
				{
					return false;
				}
			}
			std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
			if (!EnsureClassInstantiation(*ClassDecl, ConcreteTypes, TypeIndex, Location))
			{
				return false;
			}
			OutLinkageName = Module->Types[TypeIndex].Name;
			if (OutTypeIndex)
			{
				*OutTypeIndex = TypeIndex;
			}
			return true;
		}

		if (FindGenericInterfaceDecl(BaseName, CurrentModulePath, ArgTypes.size()))
		{
			return false;
		}

		// Name exists at another arity — report wrong argument count instead of unknown type.
		for (const FStructDecl& StructDecl : ActiveProgram->Structs)
		{
			if (StructDecl.Name == BaseName && CanAccessSymbol(StructDecl.ModulePath, StructDecl.bExported, CurrentModulePath) && TemplateParameterNames(StructDecl).size() != ArgTypes.size())
			{
				ReportErrorId(
				    "diag.wrong_number_of_type_arguments",
				    Location,
				    {BaseName,
				        std::to_string(TemplateParameterNames(StructDecl).size()),
				        std::to_string(ArgTypes.size())});
				return false;
			}
		}
		for (const FClassDecl& ClassDecl : ActiveProgram->Classes)
		{
			if (ClassDecl.Name == BaseName && CanAccessSymbol(ClassDecl.ModulePath, ClassDecl.bExported, CurrentModulePath) && TemplateParameterNames(ClassDecl).size() != ArgTypes.size())
			{
				ReportErrorId(
				    "diag.wrong_number_of_type_arguments",
				    Location,
				    {BaseName,
				        std::to_string(TemplateParameterNames(ClassDecl).size()),
				        std::to_string(ArgTypes.size())});
				return false;
			}
		}
		for (const FStructDecl& StructDecl : BuiltinStructTemplates)
		{
			if (StructDecl.Name == BaseName && TemplateParameterNames(StructDecl).size() != ArgTypes.size())
			{
				ReportErrorId(
				    "diag.wrong_number_of_type_arguments",
				    Location,
				    {BaseName,
				        std::to_string(TemplateParameterNames(StructDecl).size()),
				        std::to_string(ArgTypes.size())});
				return false;
			}
		}
		for (const FClassDecl& ClassDecl : BuiltinClassTemplates)
		{
			if (ClassDecl.Name == BaseName && TemplateParameterNames(ClassDecl).size() != ArgTypes.size())
			{
				ReportErrorId(
				    "diag.wrong_number_of_type_arguments",
				    Location,
				    {BaseName,
				        std::to_string(TemplateParameterNames(ClassDecl).size()),
				        std::to_string(ArgTypes.size())});
				return false;
			}
		}

		ReportErrorId("diag.unknown_type", Location, {BaseName});
		return false;
	}

	std::string VeroScriptCompiler::GenericCtorSimpleName(const std::string& TypeName) const
	{
		std::string BaseName;
		std::vector<std::string> ArgTypes;
		if (SplitUserGenericTypeApp(TypeName, BaseName, ArgTypes))
		{
			return BaseName;
		}
		const std::string TemplateBase = GenericTemplateBaseLinkage(TypeName);
		const std::string& Linkage = TemplateBase.empty() ? TypeName : TemplateBase;
		const std::size_t Sep = Linkage.rfind('\x1F');
		return Sep == std::string::npos ? Linkage : Linkage.substr(Sep + 1);
	}

	bool VeroScriptCompiler::MatchesExpectedGenericCtor(
	    const std::string& CalleeName,
	    const std::string& ExpectedTypeName) const
	{
		if (CalleeName.empty() || ExpectedTypeName.empty())
		{
			return false;
		}
		const std::string ExpectedSimple = GenericCtorSimpleName(ExpectedTypeName);
		if (ExpectedSimple != CalleeName)
		{
			// using Alias = Generic<T>; allow Alias() when the expected type is that alias expansion.
			const std::string Expanded = ExpandUserTypeAlias(CalleeName, CurrentModulePath);
			if (Expanded != CalleeName)
			{
				return Expanded == ExpectedTypeName || QualifyTypeName(Expanded, CurrentModulePath) == ExpectedTypeName;
			}
			return false;
		}
		std::string BaseName;
		std::vector<std::string> ArgTypes;
		if (SplitUserGenericTypeApp(ExpectedTypeName, BaseName, ArgTypes))
		{
			return !ArgTypes.empty();
		}
		return !GenericTemplateBaseLinkage(ExpectedTypeName).empty();
	}

	bool VeroScriptCompiler::TryInferBuiltinHandleConstructorType(
	    const FCallExpr& Call, const std::string& CalleeName, std::string& OutTypeName) const
	{
		if (CalleeName != "handle" || !Call.TypeArgs.empty() || Call.GetArguments().size() != 1 || FindGenericClassDecl(CalleeName, CurrentModulePath, 0) || FindGenericStructDecl(CalleeName, CurrentModulePath, 0))
		{
			return false;
		}
		const bool bBuiltin = std::any_of(BuiltinClassTemplates.begin(), BuiltinClassTemplates.end(),
		    [](const FClassDecl& Decl)
		{
			return Decl.Name == "handle";
		});
		if (!bBuiltin) return false;
		if (MatchesExpectedGenericCtor(CalleeName, ExpressionExpectedType))
		{
			// Preserve contextual construction, including already instantiated linkage names.
			return false;
		}
		std::string PayloadType;
		if (!TryResolveExprTypeName(*Call.GetArguments().front(), PayloadType) || PayloadType.empty() || PayloadType == "nullptr" || PayloadType == "void")
		{
			return false;
		}
		OutTypeName = MangleGenericInstanceName("handle", {PayloadType});
		return true;
	}

	std::string VeroScriptCompiler::ResolveGlobalLinkageName(
	    const std::string& Name,
	    const std::string& ReferenceModulePath) const
	{
		for (auto Scope = Scopes.rbegin(); Scope != Scopes.rend(); ++Scope)
		{
			if (!Scope->StaticLocals.empty())
			{
				const auto Static = Scope->StaticLocals.find(Name);
				if (Static != Scope->StaticLocals.end())
				{
					if (CurrentFunction)
					{
						CurrentFunction->bUsesLocalStaticStorage = true;
					}
					return Static->second;
				}
			}
			if (Scope->Locals.count(Name) || Scope->EnumConstants.count(Name)) return {};
		}
		bool Forbidden = false;
		const auto Owner = FindLexicalMemberOwner(Name, Forbidden);
		if (Forbidden) return {};
		if (!Owner.empty())
		{
			std::string Global, Type;
			bool Const = false;
			if (FindStaticFieldOnTypeLinkage(Owner, Name, Global, Type, Const))
			{
				if (!ValidateMemberAccess(CurrentEmitLocation, Owner, Name, false) || !const_cast<VeroScriptCompiler*>(this)->EnsureStaticFieldGlobalRegistered(Owner, Global, CurrentEmitLocation))
				{
					return {};
				}
				return Global;
			}
			return {};
		}
		for (const std::string& Candidate : BuildLexicalNamespaceLookupNames(Name))
		{
			if (const std::string Resolved =
			        ResolveGlobalLinkageNameExact(Candidate, ReferenceModulePath);
			    !Resolved.empty())
			{
				return Resolved;
			}
		}

		std::vector<std::string> Matches;
		for (const std::string& Candidate :
		    BuildUsingNamespaceLookupNames(Name, ReferenceModulePath))
		{
			const std::string Resolved =
			    ResolveGlobalLinkageNameExact(Candidate, ReferenceModulePath);
			if (!Resolved.empty() && std::find(Matches.begin(), Matches.end(), Resolved) == Matches.end())
			{
				Matches.push_back(Resolved);
			}
		}
		if (Matches.size() == 1)
		{
			return Matches.front();
		}
		if (Matches.size() > 1)
		{
			ReportError("ambiguous global name from using namespace: " + Name,
			    CurrentEmitLocation);
		}
		return {};
	}

	std::string VeroScriptCompiler::ResolveGlobalLinkageNameExact(
	    const std::string& Name,
	    const std::string& ReferenceModulePath) const
	{
		if (!ActiveProgram)
		{
			return {};
		}

		const FVarDeclStmt* SelectedGlobal = nullptr;
		for (const std::unique_ptr<FStmt>& Statement : ActiveProgram->TopLevelStatements)
		{
			if (!Statement || Statement->Kind != EStmtKind::VarDecl)
			{
				continue;
			}
			const FVarDeclStmt& VarDecl = static_cast<const FVarDeclStmt&>(*Statement);
			if (VarDecl.Name != Name || !CanAccessSymbol(VarDecl.ModulePath, VarDecl.bExported, ReferenceModulePath))
			{
				continue;
			}
			if (VarDecl.ModulePath == ReferenceModulePath)
			{
				return GetSymbolLinkageName(VarDecl.ModulePath, VarDecl.bExported, VarDecl.Name);
			}
			if (!SelectedGlobal)
			{
				SelectedGlobal = &VarDecl;
			}
		}
		if (!SelectedGlobal)
		{
			return {};
		}
		return GetSymbolLinkageName(
		    SelectedGlobal->ModulePath,
		    SelectedGlobal->bExported,
		    SelectedGlobal->Name);
	}
}
