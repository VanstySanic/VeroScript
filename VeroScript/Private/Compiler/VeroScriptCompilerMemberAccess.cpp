// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"

#include "VeroScriptAst.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
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
	void VeroScriptCompiler::EmitReflectionScopeBinding(FEmitter& Emitter, const std::uint16_t TypeIndex)
	{
		if (!ActiveProgram || !Module || TypeIndex >= Module->Types.size() || GenericTemplateBaseLinkage(Module->Types[TypeIndex].Name) != "type")
		{
			return;
		}
		const auto Arguments = DecodeGenericConcreteTypes(Module->Types[TypeIndex].Name);
		if (Arguments.size() != 1) return;
		const auto FindChildren = [&](const auto& Declarations) -> const std::vector<FNestedTypeDeclaration>*
		{
			for (const auto& Declaration : Declarations)
			{
				if (Declaration.Name == Arguments[0]) return &Declaration.NestedTypes;
			}
			return nullptr;
		};
		const auto* Children = FindChildren(Module->Types);
		if (!Children)
		{
			Children = FindChildren(Module->Interfaces);
		}
		std::string Grants;
		if (Children)
		{
			std::string Context = CurrentOwnerTypeLinkage;
			const auto MethodSeparator = FindMethodLinkageSeparator(CurrentAccessFunctionLinkage);
			if (MethodSeparator != std::string::npos)
			{
				Context = CurrentAccessFunctionLinkage.substr(0, MethodSeparator);
			}
			for (const auto& Child : *Children)
			{
				FMemberAccessInfo Access;
				Access.bFound = true;
				Access.DeclaringOwnerLinkage = Arguments[0];
				Access.DeclaringArity = DecodeGenericConcreteTypes(Arguments[0]).size();
				Access.EffectiveAccess = Child.Scope.Access;
				if (CanAccessMemberFromContext(Access, Context, *ActiveProgram, {}, true, ActiveImportIndex) ||
				    IsCurrentFunctionFriendOf(Access, Arguments[0]))
				{
					Grants += std::to_string(Child.Scope.SimpleName.size()) + ':' + Child.Scope.SimpleName;
				}
			}
		}
		Emitter.Emit(EOpcode::Dup);
		FBytecodeConstant Constant;
		Constant.Kind = EConstantKind::Str;
		Constant.bIsLinkageString = true;
		Constant.StringIndex = AddString(Grants);
		Emitter.EmitU16Operand(EOpcode::ConstStr, AddConstant(Constant));
		Emitter.EmitCallBuiltin(EBuiltinId::ReflectionBind, 2);
	}

	bool VeroScriptCompiler::FindFieldTypeOnOwnerType(
	    const std::string& OwnerTypeName,
	    const std::string& FieldName,
	    std::string& OutFieldTypeName) const
	{
		auto TryHostField = [&](const std::string& HostTypeName) -> bool
		{
			if (!ActiveBindings || HostTypeName.empty())
			{
				return false;
			}
			const FHostTypeDesc* const HostType = ActiveBindings->FindType(HostTypeName);
			if (!HostType || (HostType->Kind != EHostTypeKind::Class && HostType->Kind != EHostTypeKind::Struct))
			{
				return false;
			}
			for (const FHostMemberDesc& Member : HostType->Members)
			{
				if (Member.Kind != EHostMemberKind::PropertyGetter && Member.Kind != EHostMemberKind::Field)
				{
					continue;
				}
				if (Member.Name != FieldName)
				{
					continue;
				}
				if (Member.ReturnType == ENativeType::HostObject)
				{
					OutFieldTypeName = Member.ReturnHostTypeName.empty()
					    ? HostType->Name
					    : Member.ReturnHostTypeName;
				}
				else if ((Member.ReturnType == ENativeType::Enum || Member.ReturnType == ENativeType::Function) && !Member.ReturnHostTypeName.empty())
				{
					OutFieldTypeName = Member.ReturnHostTypeName;
				}
				else if (Member.ReturnType == ENativeType::Array || Member.ReturnType == ENativeType::Set || Member.ReturnType == ENativeType::MultiSet || Member.ReturnType == ENativeType::OrderedMultiSet || Member.ReturnType == ENativeType::Queue || Member.ReturnType == ENativeType::PriorityQueue || Member.ReturnType == ENativeType::Stack || Member.ReturnType == ENativeType::Deque || Member.ReturnType == ENativeType::Channel)
				{
					OutFieldTypeName = Member.ReturnElementTypeName.empty()
					    ? std::string{NativeTypeName(Member.ReturnType)}
					    : MakeNativeUnaryContainerTypeName(
					          NativeTypeName(Member.ReturnType),
					          CanonicalizeNativeElementTypeName(Member.ReturnElementTypeName));
				}
				else if (NativeTypeIsBinaryMap(Member.ReturnType))
				{
					OutFieldTypeName =
					    Member.ReturnKeyTypeName.empty() || Member.ReturnValueTypeName.empty()
					    ? std::string{NativeTypeName(Member.ReturnType)}
					    : MakeNativeBinaryContainerTypeName(
					          NativeTypeName(Member.ReturnType),
					          CanonicalizeNativeElementTypeName(Member.ReturnKeyTypeName),
					          CanonicalizeNativeElementTypeName(Member.ReturnValueTypeName));
				}
				else
				{
					OutFieldTypeName = NativeTypeName(Member.ReturnType);
				}
				return true;
			}
			return false;
		};

		if (TryHostField(OwnerTypeName))
		{
			return true;
		}
		if (TryHostField(ResolveHostDispatchTypeName(OwnerTypeName)))
		{
			return true;
		}

		if (!ActiveProgram || !Module)
		{
			return false;
		}
		// pair<K,V> is a soft builtin. A non-generic user `pair` has a distinct
		// arity and must not hide the builtin fields of map/ordered_map entries.
		std::string PairFirstType;
		std::string PairSecondType;
		bool bSoftPairApplication = Runtime::ParsePairType(
		    OwnerTypeName, PairFirstType, PairSecondType);
		if (!bSoftPairApplication)
		{
			const std::string PairTemplateBase = GenericTemplateBaseLinkage(OwnerTypeName);
			const std::vector<std::string> PairTypes =
			    DecodeGenericConcreteTypes(OwnerTypeName);
			if (!PairTemplateBase.empty() && PairTypes.size() == 2u && GetTypeSimpleName(PairTemplateBase) == "pair")
			{
				PairFirstType = PairTypes[0];
				PairSecondType = PairTypes[1];
				bSoftPairApplication = true;
			}
		}
		if (bSoftPairApplication)
		{
			const auto HasUserPairTemplate = [](const auto& Declarations) -> bool
			{
				return std::any_of(
				    Declarations.begin(), Declarations.end(), [](const auto& Decl)
				{
					return Decl.Name == "pair" && TemplateParameterNames(Decl).size() == 2u;
				});
			};
			if (!HasUserPairTemplate(ActiveProgram->Structs) && !HasUserPairTemplate(ActiveProgram->Classes))
			{
				if (FieldName == "first")
				{
					OutFieldTypeName = CanonicalTypeNameForCoercion(
					    QualifyTypeName(PairFirstType, CurrentModulePath));
					return true;
				}
				if (FieldName == "second")
				{
					OutFieldTypeName = CanonicalTypeNameForCoercion(
					    QualifyTypeName(PairSecondType, CurrentModulePath));
					return true;
				}
				return false;
			}
		}

		std::string OwnerLinkageName = OwnerTypeName;
		std::string GenericBase;
		std::vector<std::string> GenericArgs;
		if (SplitUserGenericTypeApp(OwnerTypeName, GenericBase, GenericArgs))
		{
			OwnerLinkageName = QualifyTypeName(OwnerTypeName, CurrentModulePath);
		}
		else if (FindGenericTypeMarker(OwnerTypeName) == std::string::npos && OwnerTypeName.find('\x1F') == std::string::npos)
		{
			const std::string Resolved = ResolveTypeLinkageName(OwnerTypeName, CurrentModulePath);
			if (!Resolved.empty())
			{
				OwnerLinkageName = Resolved;
			}
		}

		// Retain the first matching layout when both owner spellings are registered.
		const auto OwnerIndex = std::min(
		    FindRegisteredTypeIndex(OwnerLinkageName), FindRegisteredTypeIndex(OwnerTypeName));
		if (OwnerIndex != static_cast<std::uint16_t>(-1))
		{
			const auto& TypeDesc = Module->Types[OwnerIndex];
			for (const FBytecodeFieldDesc& Field : TypeDesc.Fields)
			{
				if (Field.Name == FieldName)
				{
					OutFieldTypeName = Field.TypeName;
					return true;
				}
			}
			return false;
		}

		std::string AstOwnerLinkage = OwnerLinkageName;
		const std::string TemplateBase = GenericTemplateBaseLinkage(OwnerLinkageName);
		if (!TemplateBase.empty())
		{
			AstOwnerLinkage = TemplateBase;
		}
		else if (AstOwnerLinkage.find('\x1F') == std::string::npos && FindGenericTypeMarker(AstOwnerLinkage) == std::string::npos)
		{
			AstOwnerLinkage = ResolveTypeLinkageName(OwnerTypeName, CurrentModulePath);
		}
		if (AstOwnerLinkage.empty())
		{
			return false;
		}
		const std::size_t WantedArity = TypeParamArityFromLinkage(
		    FindGenericTypeMarker(OwnerLinkageName) != std::string::npos
		        ? OwnerLinkageName
		        : OwnerTypeName);
		// Cold expression queries must use the same owner arguments as a compiled
		// layout, without instantiating the type merely to inspect a declaration.
		std::unordered_map<std::string, std::string> OwnerSubst;
		if (!TemplateBase.empty())
		{
			(void)TryBuildGenericOwnerSubst(OwnerLinkageName, OwnerSubst, {});
		}
		const auto ResolveOwnerFieldType =
		    [&](const std::string& FieldTypeName, const std::string& DeclModulePath) -> std::string
		{
			return QualifyTypeName(
			    SubstituteTypeNameWith(FieldTypeName, OwnerSubst), DeclModulePath);
		};
		const auto FindInheritedFieldType =
		    [&](const std::string& BaseTypeName, const std::string& DeclModulePath) -> bool
		{
			const std::string BaseLinkage = ResolveInheritanceBaseLinkage(
			    SubstituteTypeNameWith(BaseTypeName, OwnerSubst), DeclModulePath);
			return FindFieldTypeOnOwnerType(BaseLinkage, FieldName, OutFieldTypeName);
		};
		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceStructIndices, AstOwnerLinkage))
		{
			const auto& StructDecl = ActiveProgram->Structs[DeclarationIndex];
			if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) != AstOwnerLinkage || TemplateParameterNames(StructDecl).size() != WantedArity)
			{
				continue;
			}
			for (const FFieldDecl& Field : StructDecl.Fields)
			{
				if (Field.Name == FieldName)
				{
					OutFieldTypeName = ResolveOwnerFieldType(Field.TypeName, StructDecl.ModulePath);
					return true;
				}
			}
			if (!StructDecl.BaseTypeName.empty() && FindInheritedFieldType(StructDecl.BaseTypeName, StructDecl.ModulePath))
			{
				return true;
			}
			for (const std::string& InterfaceName : StructDecl.InterfaceTypeNames)
			{
				if (FindInheritedFieldType(InterfaceName, StructDecl.ModulePath))
				{
					return true;
				}
			}
			return false;
		}
		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceClassIndices, AstOwnerLinkage))
		{
			const auto& ClassDecl = ActiveProgram->Classes[DeclarationIndex];
			if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) != AstOwnerLinkage || TemplateParameterNames(ClassDecl).size() != WantedArity)
			{
				continue;
			}
			for (const FFieldDecl& Field : ClassDecl.Fields)
			{
				if (Field.Name == FieldName)
				{
					OutFieldTypeName = ResolveOwnerFieldType(Field.TypeName, ClassDecl.ModulePath);
					return true;
				}
			}
			for (const std::string& BaseSimple : ClassDecl.BaseTypeNames)
			{
				if (FindInheritedFieldType(BaseSimple, ClassDecl.ModulePath))
				{
					return true;
				}
			}
			for (const std::string& InterfaceName : ClassDecl.InterfaceTypeNames)
			{
				if (FindInheritedFieldType(InterfaceName, ClassDecl.ModulePath))
				{
					return true;
				}
			}
			return false;
		}

		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceInterfaceIndices, AstOwnerLinkage))
		{
			const auto& InterfaceDecl = ActiveProgram->Interfaces[DeclarationIndex];
			if (GetSymbolLinkageName(InterfaceDecl.ModulePath, InterfaceDecl.bExported, InterfaceDecl.Name) != AstOwnerLinkage || TemplateParameterNames(InterfaceDecl).size() != WantedArity)
			{
				continue;
			}
			std::unordered_map<std::string, std::string> InterfaceSubst;
			BuildInterfaceTypeSubst(OwnerLinkageName, InterfaceSubst);
			for (const FFieldDecl& Field : InterfaceDecl.Fields)
			{
				if (Field.Name == FieldName)
				{
					OutFieldTypeName = QualifyTypeName(
					    SubstituteTypeNameWith(Field.TypeName, InterfaceSubst), InterfaceDecl.ModulePath);
					return true;
				}
			}
			for (const std::string& ParentName : InterfaceDecl.BaseInterfaceNames)
			{
				const std::string ParentLinkage = ResolveInheritanceBaseLinkage(
				    SubstituteTypeNameWith(ParentName, InterfaceSubst), InterfaceDecl.ModulePath);
				if (FindFieldTypeOnOwnerType(ParentLinkage, FieldName, OutFieldTypeName))
				{
					return true;
				}
			}
			return false;
		}

		for (const FStructDecl& StructDecl : BuiltinStructTemplates)
		{
			if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) != AstOwnerLinkage || TemplateParameterNames(StructDecl).size() != WantedArity)
			{
				continue;
			}
			for (const FFieldDecl& Field : StructDecl.Fields)
			{
				if (Field.Name == FieldName)
				{
					OutFieldTypeName = ResolveOwnerFieldType(Field.TypeName, StructDecl.ModulePath);
					return true;
				}
			}
			return false;
		}
		for (const FClassDecl& ClassDecl : BuiltinClassTemplates)
		{
			if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) != AstOwnerLinkage || TemplateParameterNames(ClassDecl).size() != WantedArity)
			{
				continue;
			}
			for (const FFieldDecl& Field : ClassDecl.Fields)
			{
				if (Field.Name == FieldName)
				{
					OutFieldTypeName = ResolveOwnerFieldType(Field.TypeName, ClassDecl.ModulePath);
					return true;
				}
			}
			return false;
		}
		return false;
	}

	bool VeroScriptCompiler::LookupMemberAccess(
	    const std::string& OwnerTypeLinkage,
	    const std::string& MemberName,
	    const bool bIsMethod,
	    FMemberAccessInfo& Out,
	    const int Depth,
	    const std::size_t ArgCount,
	    const FMethodDecl* const PreferMethod) const
	{
		if (!ActiveProgram || Depth > 64 || OwnerTypeLinkage.empty())
		{
			return false;
		}

		const std::string OwnerKey = [&]() -> std::string
		{
			const std::string TemplateBase = GenericTemplateBaseLinkage(OwnerTypeLinkage);
			return TemplateBase.empty() ? OwnerTypeLinkage : TemplateBase;
		}();
		const std::size_t WantedArity = TypeParamArityFromLinkage(OwnerTypeLinkage);

		const auto LinkageMatchesOwner =
		    [&](const std::string& DeclLinkage, const std::size_t DeclArity) -> bool
		{
			if (DeclArity != WantedArity)
			{
				return false;
			}
			const std::string DeclKey = GenericTemplateBaseLinkage(DeclLinkage);
			return (DeclKey.empty() ? DeclLinkage : DeclKey) == OwnerKey || DeclLinkage == OwnerTypeLinkage;
		};

		const auto MethodArityOk =
		    [ArgCount, PreferMethod](const FMethodDecl& Method) -> bool
		{
			if (PreferMethod)
			{
				if (Method.Name != PreferMethod->Name || Method.bIsConst != PreferMethod->bIsConst || Method.Parameters.size() != PreferMethod->Parameters.size())
				{
					return false;
				}
				for (std::size_t Index = 0; Index < Method.Parameters.size(); ++Index)
				{
					if (Method.Parameters[Index].TypeName != PreferMethod->Parameters[Index].TypeName || Method.Parameters[Index].bIsRef != PreferMethod->Parameters[Index].bIsRef || Method.Parameters[Index].bIsConst != PreferMethod->Parameters[Index].bIsConst)
					{
						return false;
					}
				}
				return true;
			}
			if (ArgCount == static_cast<std::size_t>(-1))
			{
				return true;
			}
			return ParamsAcceptArgCount(Method.Parameters, ArgCount);
		};

		const auto ApplyUsingPromotion = [&](
		                                     const FUsingMemberDecl& UsingDecl,
		                                     const FMemberAccessInfo& BaseInfo,
		                                     const std::string& TypeLinkage,
		                                     const std::size_t TypeArity,
		                                     const std::string& UsedBaseLinkage) -> bool
		{
			// Qualified `using Base::m` requires Base to be an accessible base of this type.
			if (!UsedBaseLinkage.empty() && !IsAccessibleBase(*ActiveProgram, OwnerTypeLinkage, WantedArity, UsedBaseLinkage, TypeParamArityFromLinkage(UsedBaseLinkage)))
			{
				return false;
			}
			// `using` may only name a member already accessible in this class (C++).
			// Use OwnerTypeLinkage (may be `Type#args`) so generic arity matches IsDerivedFrom.
			if (!CanAccessMemberFromContext(BaseInfo, OwnerTypeLinkage, *ActiveProgram, {}, false, ActiveImportIndex))
			{
				return false;
			}
			Out.bFound = true;
			Out.EffectiveAccess = UsingDecl.Access;
			Out.DeclaringOwnerLinkage = TypeLinkage;
			Out.DeclaringArity = TypeArity;
			return true;
		};

		const auto TryOwnMembers = [&](
		                               const std::vector<FFieldDecl>& Fields,
		                               const std::vector<FMethodDecl>& Methods,
		                               const std::vector<FUsingMemberDecl>& UsingMembers,
		                               const std::string& TypeLinkage,
		                               const std::size_t TypeArity,
		                               const std::string& OwnerModulePath) -> bool
		{
			if (!bIsMethod)
			{
				for (const FFieldDecl& Field : Fields)
				{
					if (Field.Name == MemberName)
					{
						Out.bFound = true;
						Out.EffectiveAccess = Field.Access;
						Out.DeclaringOwnerLinkage = TypeLinkage;
						Out.DeclaringArity = TypeArity;
						return true;
					}
				}
			}
			else
			{
				for (const FMethodDecl& Method : Methods)
				{
					if (Method.Name == MemberName && MethodArityOk(Method))
					{
						Out.bFound = true;
						Out.EffectiveAccess = Method.Access;
						Out.DeclaringOwnerLinkage = TypeLinkage;
						Out.DeclaringArity = TypeArity;
						return true;
					}
				}
			}
			for (const FUsingMemberDecl& UsingDecl : UsingMembers)
			{
				if (UsingDecl.MemberName != MemberName || UsingDecl.BaseTypeName.empty())
				{
					continue;
				}
				const std::string BaseLinkage =
				    ResolveTypeLinkageName(UsingDecl.BaseTypeName, OwnerModulePath);
				FMemberAccessInfo BaseInfo;
				if (!BaseLinkage.empty() && LookupMemberAccess(BaseLinkage, MemberName, bIsMethod, BaseInfo, Depth + 1, ArgCount, PreferMethod) && ApplyUsingPromotion(UsingDecl, BaseInfo, TypeLinkage, TypeArity, BaseLinkage))
				{
					return true;
				}
			}
			return false;
		};

		const auto InheritFromBase = [&](
		                                 const FMemberAccessInfo& BaseInfo,
		                                 const EMemberAccess InheritAccess,
		                                 const std::string& CurrentTypeLinkage,
		                                 const std::size_t CurrentTypeArity)
		{
			Out.bFound = true;
			Out.EffectiveAccess = ApplyInheritanceAccess(BaseInfo.EffectiveAccess, InheritAccess);
			// Private/protected inheritance re-homes the name on the derived class for access checks
			// so that class can still use the member, while outsiders / further derived see the
			// tightened access.
			if (IsAccessMoreRestricted(Out.EffectiveAccess, BaseInfo.EffectiveAccess))
			{
				Out.DeclaringOwnerLinkage = CurrentTypeLinkage;
				Out.DeclaringArity = CurrentTypeArity;
			}
			else
			{
				Out.DeclaringOwnerLinkage = BaseInfo.DeclaringOwnerLinkage;
				Out.DeclaringArity = BaseInfo.DeclaringArity;
			}
		};

		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceStructIndices, OwnerKey))
		{
			const auto& StructDecl = ActiveProgram->Structs[DeclarationIndex];
			const std::string Linkage =
			    GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name);
			if (!LinkageMatchesOwner(Linkage, TemplateParameterNames(StructDecl).size()))
			{
				continue;
			}
			const std::size_t TypeArity = TemplateParameterNames(StructDecl).size();
			if (TryOwnMembers(
			        StructDecl.Fields,
			        StructDecl.Methods,
			        StructDecl.UsingMembers,
			        Linkage,
			        TypeArity,
			        StructDecl.ModulePath))
			{
				return true;
			}
			for (const FUsingMemberDecl& UsingDecl : StructDecl.UsingMembers)
			{
				if (UsingDecl.MemberName != MemberName || !UsingDecl.BaseTypeName.empty())
				{
					continue;
				}
				if (StructDecl.BaseTypeName.empty())
				{
					continue;
				}
				const std::string BaseLinkage =
				    ResolveTypeLinkageName(StructDecl.BaseTypeName, StructDecl.ModulePath);
				FMemberAccessInfo BaseInfo;
				if (!BaseLinkage.empty() && LookupMemberAccess(BaseLinkage, MemberName, bIsMethod, BaseInfo, Depth + 1, ArgCount, PreferMethod) && ApplyUsingPromotion(UsingDecl, BaseInfo, Linkage, TypeArity, BaseLinkage))
				{
					return true;
				}
			}
			if (!StructDecl.BaseTypeName.empty())
			{
				const std::string BaseLinkage =
				    ResolveTypeLinkageName(StructDecl.BaseTypeName, StructDecl.ModulePath);
				FMemberAccessInfo BaseInfo;
				if (!BaseLinkage.empty() && LookupMemberAccess(BaseLinkage, MemberName, bIsMethod, BaseInfo, Depth + 1, ArgCount, PreferMethod))
				{
					InheritFromBase(
					    BaseInfo, StructDecl.BaseInheritanceAccess, Linkage, TypeArity);
					return true;
				}
			}
			for (const std::string& InterfaceName : StructDecl.InterfaceTypeNames)
			{
				const std::string InterfaceLinkage =
				    ResolveTypeLinkageName(InterfaceName, StructDecl.ModulePath);
				FMemberAccessInfo InterfaceInfo;
				if (!InterfaceLinkage.empty() && LookupMemberAccess(InterfaceLinkage, MemberName, bIsMethod, InterfaceInfo, Depth + 1, ArgCount, PreferMethod))
				{
					InheritFromBase(InterfaceInfo, EMemberAccess::Public, Linkage, TypeArity);
					return true;
				}
			}
			return false;
		}

		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceClassIndices, OwnerKey))
		{
			const auto& ClassDecl = ActiveProgram->Classes[DeclarationIndex];
			const std::string Linkage =
			    GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name);
			if (!LinkageMatchesOwner(Linkage, TemplateParameterNames(ClassDecl).size()))
			{
				continue;
			}
			const std::size_t TypeArity = TemplateParameterNames(ClassDecl).size();
			if (TryOwnMembers(
			        ClassDecl.Fields,
			        ClassDecl.Methods,
			        ClassDecl.UsingMembers,
			        Linkage,
			        TypeArity,
			        ClassDecl.ModulePath))
			{
				return true;
			}
			for (const FUsingMemberDecl& UsingDecl : ClassDecl.UsingMembers)
			{
				if (UsingDecl.MemberName != MemberName || !UsingDecl.BaseTypeName.empty())
				{
					continue;
				}
				for (const std::string& BaseName : ClassDecl.BaseTypeNames)
				{
					const std::string BaseLinkage =
					    ResolveTypeLinkageName(BaseName, ClassDecl.ModulePath);
					FMemberAccessInfo BaseInfo;
					if (!BaseLinkage.empty() && LookupMemberAccess(BaseLinkage, MemberName, bIsMethod, BaseInfo, Depth + 1, ArgCount, PreferMethod) && ApplyUsingPromotion(UsingDecl, BaseInfo, Linkage, TypeArity, BaseLinkage))
					{
						return true;
					}
				}
			}
			for (std::size_t BaseIndex = 0; BaseIndex < ClassDecl.BaseTypeNames.size(); ++BaseIndex)
			{
				const std::string BaseLinkage = ResolveTypeLinkageName(
				    ClassDecl.BaseTypeNames[BaseIndex],
				    ClassDecl.ModulePath);
				const EMemberAccess InheritAccess =
				    BaseIndex < ClassDecl.BaseInheritanceAccess.size()
				    ? ClassDecl.BaseInheritanceAccess[BaseIndex]
				    : EMemberAccess::Private;
				FMemberAccessInfo BaseInfo;
				if (!BaseLinkage.empty() && LookupMemberAccess(BaseLinkage, MemberName, bIsMethod, BaseInfo, Depth + 1, ArgCount, PreferMethod))
				{
					InheritFromBase(BaseInfo, InheritAccess, Linkage, TypeArity);
					return true;
				}
			}
			// Interface members inherited through `class C : public I`.
			for (const std::string& InterfaceName : ClassDecl.InterfaceTypeNames)
			{
				const std::string InterfaceLinkage =
				    ResolveTypeLinkageName(InterfaceName, ClassDecl.ModulePath);
				FMemberAccessInfo InterfaceInfo;
				if (!InterfaceLinkage.empty() && LookupMemberAccess(InterfaceLinkage, MemberName, bIsMethod, InterfaceInfo, Depth + 1, ArgCount, PreferMethod))
				{
					InheritFromBase(InterfaceInfo, EMemberAccess::Public, Linkage, TypeArity);
					return true;
				}
			}
			return false;
		}

		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceInterfaceIndices, OwnerKey))
		{
			const auto& InterfaceDecl = ActiveProgram->Interfaces[DeclarationIndex];
			const std::string Linkage = GetSymbolLinkageName(
			    InterfaceDecl.ModulePath,
			    InterfaceDecl.bExported,
			    InterfaceDecl.Name);
			if (!LinkageMatchesOwner(Linkage, TemplateParameterNames(InterfaceDecl).size()))
			{
				continue;
			}
			const std::size_t TypeArity = TemplateParameterNames(InterfaceDecl).size();
			static const std::vector<FUsingMemberDecl> kNoUsing;
			if (TryOwnMembers(
			        InterfaceDecl.Fields,
			        InterfaceDecl.Methods,
			        kNoUsing,
			        Linkage,
			        TypeArity,
			        InterfaceDecl.ModulePath))
			{
				return true;
			}
			for (const std::string& ParentName : InterfaceDecl.BaseInterfaceNames)
			{
				const std::string ParentLinkage =
				    ResolveTypeLinkageName(ParentName, InterfaceDecl.ModulePath);
				FMemberAccessInfo ParentInfo;
				if (!ParentLinkage.empty() && LookupMemberAccess(ParentLinkage, MemberName, bIsMethod, ParentInfo, Depth + 1, ArgCount, PreferMethod))
				{
					InheritFromBase(ParentInfo, EMemberAccess::Public, Linkage, TypeArity);
					return true;
				}
			}
			return false;
		}
		return false;
	}

	bool VeroScriptCompiler::ValidateMemberAccess(
	    const FSourceLocation& Location,
	    const std::string& OwnerTypeName,
	    const std::string& MemberName,
	    const bool bIsMethod,
	    const std::size_t ArgCount,
	    const FMethodDecl* const PreferMethod) const
	{
		if (!ActiveProgram)
		{
			return true;
		}
		std::string OwnerLinkage = ResolveTypeLinkageName(OwnerTypeName, CurrentModulePath);
		if (OwnerLinkage.empty())
		{
			OwnerLinkage = QualifyTypeName(OwnerTypeName, CurrentModulePath);
		}
		// Keep `Type#args` so arity-overloaded templates resolve to the correct decl.
		const FMethodDecl* SourceMethod = PreferMethod;
		if (const auto Key = MethodSpecializationKeys.find(PreferMethod); Key != MethodSpecializationKeys.end())
		{
			SourceMethod = MethodSpecializations.at(Key->second).Declaration;
		}
		FMemberAccessInfo Info;
		if (!LookupMemberAccess(
		        OwnerLinkage, MemberName, bIsMethod, Info, 0, ArgCount, SourceMethod))
		{
			// PreferMethod may live on an interface (Lookup only walks class/struct).
			// Allow only when the resolved method is public — never fail-open private/protected.
			if (PreferMethod && PreferMethod->Access == EMemberAccess::Public)
			{
				Info.bFound = true;
				Info.EffectiveAccess = EMemberAccess::Public;
				Info.DeclaringOwnerLinkage = [&]() -> std::string
				{
					const std::string TemplateBase = GenericTemplateBaseLinkage(OwnerLinkage);
					return TemplateBase.empty() ? OwnerLinkage : TemplateBase;
				}();
				Info.DeclaringArity = TypeParamArityFromLinkage(OwnerLinkage);
			}
			else if (PreferMethod)
			{
				ReportErrorId(
				    "diag.inaccessible_member",
				    Location,
				    {MemberName, OwnerTypeName});
				return false;
			}
			else
			{
				// Field / unbound Lookup miss: soft builtins (handle.value) and some
				// monomorph fields are not in access tables — unknown-member diagnostics
				// own those paths. PreferMethod path above is already fail-closed.
				return true;
			}
		}
		// Public access is unconditional; friendship cannot change this result.
		if (Info.EffectiveAccess == EMemberAccess::Public) return true;
		std::string AccessContextOwner = CurrentOwnerTypeLinkage;
		const std::size_t AccessMethodSeparator =
		    FindMethodLinkageSeparator(CurrentAccessFunctionLinkage);
		if (AccessMethodSeparator != std::string::npos)
		{
			// Generic method bodies and their nested lambdas must use the concrete
			// owner identity. CurrentOwnerTypeLinkage can still name the open template.
			AccessContextOwner =
			    CurrentAccessFunctionLinkage.substr(0, AccessMethodSeparator);
		}
		const std::string ReceiverBase = [&]() -> std::string
		{
			const std::string Base = GenericTemplateBaseLinkage(OwnerLinkage);
			return Base.empty() ? OwnerLinkage : Base;
		}();
		const std::string DeclaringBase = [&]() -> std::string
		{
			const std::string Base = GenericTemplateBaseLinkage(Info.DeclaringOwnerLinkage);
			return Base.empty() ? Info.DeclaringOwnerLinkage : Base;
		}();
		// A declaring type's ordinary access does not require a friendship search.
		const std::string ContextBase = GenericTemplateBaseLinkage(AccessContextOwner);
		if (DeclaringBase == (ContextBase.empty() ? AccessContextOwner : ContextBase) &&
		    CanAccessMemberFromContext(Info, AccessContextOwner, *ActiveProgram, OwnerLinkage, false, ActiveImportIndex))
		{
			return true;
		}
		bool bFunctionFriend = IsCurrentFunctionFriendOf(Info, OwnerLinkage);
		if (!bFunctionFriend && Info.EffectiveAccess == EMemberAccess::Protected && ReceiverBase != DeclaringBase)
		{
			FMemberAccessInfo ReceiverInfo = Info;
			ReceiverInfo.DeclaringOwnerLinkage = ReceiverBase;
			ReceiverInfo.DeclaringArity = TypeParamArityFromLinkage(OwnerLinkage);
			bFunctionFriend = IsCurrentFunctionFriendOf(ReceiverInfo, OwnerLinkage);
		}
		if (!bFunctionFriend &&
		    !CanAccessMemberFromContext(Info, AccessContextOwner, *ActiveProgram, OwnerLinkage, false, ActiveImportIndex))
		{
			ReportErrorId(
			    "diag.inaccessible_member",
			    Location,
			    {MemberName, OwnerTypeName});
			return false;
		}
		return true;
	}

	bool VeroScriptCompiler::IsCurrentFunctionFriendOf(
	    const FMemberAccessInfo& Info,
	    const std::string& ReceiverTypeLinkage) const
	{
		const auto CheckFunction = [&](const std::string& AccessFunctionLinkage)
		{
			if (!ActiveProgram || AccessFunctionLinkage.empty() || !Info.bFound)
			{
				return false;
			}
			const std::string DeclaringBase = [&]()
			{
				const std::string Base = GenericTemplateBaseLinkage(Info.DeclaringOwnerLinkage);
				return Base.empty() ? Info.DeclaringOwnerLinkage : Base;
			}();
			const std::vector<FFriendDecl>* Friends = nullptr;
			std::string DeclaringModule;
			std::string DeclaringName;
			const auto FindDeclaring = [&](const auto& Decls)
			{
				for (const auto& Decl : Decls)
				{
					const std::string Linkage = GetSymbolLinkageName(
					    Decl.ModulePath, Decl.bExported, Decl.Name);
					if (Linkage == DeclaringBase && TemplateParameterNames(Decl).size() == Info.DeclaringArity)
					{
						Friends = &Decl.Friends;
						DeclaringModule = Decl.ModulePath;
						DeclaringName = Decl.Name;
						return;
					}
				}
			};
			FindDeclaring(ActiveProgram->Structs);
			if (!Friends)
			{
				FindDeclaring(ActiveProgram->Classes);
			}
			if (!Friends)
			{
				FindDeclaring(ActiveProgram->Interfaces);
			}
			if (!Friends)
			{
				return false;
			}

			std::unordered_map<std::string, std::string> DeclaringSubst;
			const std::string DeclaringConcreteOwner =
			    GenericTemplateBaseLinkage(ReceiverTypeLinkage) == DeclaringBase
			    ? ReceiverTypeLinkage
			    : Info.DeclaringOwnerLinkage;
			(void)TryBuildGenericOwnerSubst(
			    DeclaringConcreteOwner, DeclaringSubst, FSourceLocation{});
			const std::size_t DeclaringSeparator = DeclaringName.rfind('.');
			const std::string DeclaringNamespace = DeclaringSeparator == std::string::npos
			    ? std::string{}
			    : DeclaringName.substr(0, DeclaringSeparator);
			const auto QualifiedCandidates = [&](const std::string& Name)
			{
				std::vector<std::string> Result;
				if (!DeclaringNamespace.empty() && Name.rfind(DeclaringNamespace + ".", 0) != 0)
				{
					Result.push_back(DeclaringNamespace + "." + Name);
				}
				Result.push_back(Name);
				return Result;
			};
			const auto CanonicalReturn = [&](
			                                 const std::string& TypeName,
			                                 const std::string& ModulePath,
			                                 const std::unordered_map<std::string, std::string>& Subst)
			{
				return QualifyTypeName(
				    NormalizeTypeAlias(SubstituteTypeNameWith(TypeName, Subst), ModulePath),
				    ModulePath);
			};
			const auto SplitQualifiedFunction = [](
			                                        const std::string& Name,
			                                        std::string& OutOwner,
			                                        std::string& OutLeaf)
			{
				int Depth = 0;
				std::size_t LastDot = std::string::npos;
				for (std::size_t Index = 0; Index < Name.size(); ++Index)
				{
					if (Name[Index] == '<')
					{
						++Depth;
					}
					else if (Name[Index] == '>' && (Index == 0 || Name[Index - 1] != '-'))
					{
						--Depth;
					}
					else if (Name[Index] == '.' && Depth == 0)
					{
						LastDot = Index;
					}
				}
				if (LastDot == std::string::npos)
				{
					OutOwner.clear();
					OutLeaf = Name;
				}
				else
				{
					OutOwner = Name.substr(0, LastDot);
					OutLeaf = Name.substr(LastDot + 1);
				}
			};

			const std::size_t MethodSeparator =
			    FindMethodLinkageSeparator(AccessFunctionLinkage);
			if (MethodSeparator == std::string::npos)
			{
				const FFunctionDecl* CurrentDecl = nullptr;
				for (const FFunctionDecl& Function : ActiveProgram->Functions)
				{
					if (TemplateParameterNames(Function).empty() &&
					    CanAccessSymbol(Function.ModulePath, Function.bExported, DeclaringModule) &&
					    MakeFreeFunctionLinkageName(Function) == AccessFunctionLinkage)
					{
						CurrentDecl = &Function;
						break;
					}
				}
				if (!CurrentDecl)
				{
					return false;
				}
				for (const FFriendDecl& Friend : *Friends)
				{
					if (Friend.Kind != FFriendDecl::EKind::Function || Friend.bIsConstMethod || CanonicalParamTypeNames(Friend.Parameters, DeclaringModule, DeclaringSubst) != CanonicalParamTypeNames(CurrentDecl->Parameters, CurrentDecl->ModulePath, {}) || CanonicalReturn(Friend.ReturnType, DeclaringModule, DeclaringSubst) != CanonicalReturn(CurrentDecl->ReturnType, CurrentDecl->ModulePath, {}))
					{
						continue;
					}
					for (const std::string& Candidate : QualifiedCandidates(Friend.FunctionName))
					{
						if (Candidate == CurrentDecl->Name)
						{
							return true;
						}
					}
				}
				return false;
			}

			const std::string CurrentOwner =
			    AccessFunctionLinkage.substr(0, MethodSeparator);
			const std::string CurrentMethodLeaf(
			    MethodLinkageBareLeaf(AccessFunctionLinkage));
			const FMethodDecl* CurrentMethod = nullptr;
			std::string CurrentMethodModule;
			std::unordered_map<std::string, std::string> CurrentOwnerSubst;
			(void)TryBuildGenericOwnerSubst(
			    CurrentOwner, CurrentOwnerSubst, FSourceLocation{});
			const auto FindCurrentMethod = [&](const auto& Decls)
			{
				for (const auto& Decl : Decls)
				{
					const std::string DeclLinkage = GetSymbolLinkageName(
					    Decl.ModulePath, Decl.bExported, Decl.Name);
					const std::string CurrentBase = GenericTemplateBaseLinkage(CurrentOwner);
					if (DeclLinkage != (CurrentBase.empty() ? CurrentOwner : CurrentBase))
					{
						continue;
					}
					if (!CanAccessSymbol(Decl.ModulePath, Decl.bExported, DeclaringModule))
					{
						continue;
					}
					for (const FMethodDecl& Method : Decl.Methods)
					{
						if (Method.Name != CurrentMethodLeaf)
						{
							continue;
						}
						const std::string Key = MakeMethodFunctionName(
						    CurrentOwner,
						    Method.Name,
						    CanonicalMethodParamTypeNames(
						        Method, Decl.ModulePath, CurrentOwnerSubst),
						    Method.bIsConst);
						if (Key == AccessFunctionLinkage)
						{
							CurrentMethod = &Method;
							CurrentMethodModule = Decl.ModulePath;
							return;
						}
					}
				}
			};
			FindCurrentMethod(ActiveProgram->Structs);
			if (!CurrentMethod)
			{
				FindCurrentMethod(ActiveProgram->Classes);
			}
			if (!CurrentMethod)
			{
				FindCurrentMethod(ActiveProgram->Interfaces);
			}
			if (!CurrentMethod)
			{
				return false;
			}

			for (const FFriendDecl& Friend : *Friends)
			{
				if (Friend.Kind != FFriendDecl::EKind::Function || Friend.bIsConstMethod != CurrentMethod->bIsConst || CanonicalParamTypeNames(Friend.Parameters, DeclaringModule, DeclaringSubst) != CanonicalMethodParamTypeNames(*CurrentMethod, CurrentMethodModule, CurrentOwnerSubst) || CanonicalReturn(Friend.ReturnType, DeclaringModule, DeclaringSubst) != CanonicalReturn(CurrentMethod->ReturnType, CurrentMethodModule, CurrentOwnerSubst))
				{
					continue;
				}
				std::string FriendOwner;
				std::string FriendLeaf;
				SplitQualifiedFunction(Friend.FunctionName, FriendOwner, FriendLeaf);
				if (FriendOwner.empty() || FriendLeaf != CurrentMethod->Name)
				{
					continue;
				}
				FriendOwner = NormalizeTypeAlias(
				    SubstituteTypeNameWith(FriendOwner, DeclaringSubst),
				    DeclaringModule);
				for (const std::string& Candidate : QualifiedCandidates(FriendOwner))
				{
					if (QualifyTypeName(Candidate, DeclaringModule) == CurrentOwner)
					{
						return true;
					}
				}
			}
			return false;
		};
		if (CheckFunction(CurrentAccessFunctionLinkage)) return true;
		if (!ActiveProgram || CurrentOwnerTypeLinkage.empty()) return false;
		std::unordered_map<std::string, std::string> Substitution;
		(void)TryBuildGenericOwnerSubst(CurrentOwnerTypeLinkage, Substitution, {});
		const auto Base = GenericTemplateBaseLinkage(CurrentOwnerTypeLinkage);
		std::string Owner = Base.empty() ? CurrentOwnerTypeLinkage : Base;
		std::unordered_set<std::string> Seen;
		for (std::size_t Depth = 0; !Owner.empty() && Depth < 64 && Seen.insert(Owner).second; ++Depth)
		{
			const FTypeDeclarationScope* Scope = nullptr;
			const FDeferredSyntax* Context = nullptr;
			const auto FindScope = [&](const auto& Declarations)
			{
				for (const auto& Declaration : Declarations)
				{
					if (GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, Declaration.Name) == Owner)
					{
						Scope = &Declaration.Scope;
						Context = Declaration.DeclarationContext.get();
					}
				}
			};
			FindScope(ActiveProgram->Classes);
			FindScope(ActiveProgram->Structs);
			FindScope(ActiveProgram->Interfaces);
			if (!Scope || !Context) return false;
			const auto Contains = [&](const FBlockStmt* Body)
			{
				if (!Body || Body->ScopeId.empty()) return false;
				const auto& Name = Scope->OwnerFunctionName;
				return (Name == Body->CallableOwner || Name.rfind(Body->CallableOwner + "/6:lambda", 0) == 0) && (Context->ScopeId == Body->ScopeId || Context->ScopeId.rfind(Body->ScopeId + "/", 0) == 0);
			};
			for (const auto& Function : ActiveProgram->Functions)
			{
				if (Contains(Function.Body.get()) && CheckFunction(MakeFreeFunctionLinkageName(Function))) return true;
			}
			std::string Parent;
			const auto CheckMethods = [&](const auto& Declarations)
			{
				for (const auto& Declaration : Declarations)
				{
					const auto Linkage = GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, Declaration.Name);
					if (Declaration.Name == Scope->AccessTypeName && Declaration.ModulePath == Scope->AccessModulePath)
					{
						Parent = Linkage;
					}
					for (const auto& Method : Declaration.Methods)
					{
						if (!Contains(Method.Body.get()))
						{
							continue;
						}
						std::vector<std::string> Arguments;
						for (const auto& Parameter : TemplateParameterNames(Declaration))
						{
							const auto Found = Substitution.find(Parameter);
							Arguments.push_back(Found == Substitution.end() ? Parameter : Found->second);
						}
						const auto Concrete = Arguments.empty() ? Linkage : MangleGenericInstanceName(Linkage, Arguments);
						const auto MethodName = MakeMethodFunctionName(Concrete, Method.Name,
						    CanonicalMethodParamTypeNames(Method, Declaration.ModulePath, Substitution), Method.bIsConst);
						if (CheckFunction(MethodName)) return true;
					}
				}
				return false;
			};
			if (CheckMethods(ActiveProgram->Classes) || CheckMethods(ActiveProgram->Structs) || CheckMethods(ActiveProgram->Interfaces))
			{
				return true;
			}
			Owner = std::move(Parent);
		}
		return false;
	}

	bool VeroScriptCompiler::ValidateThisFieldAccess(
	    const FSourceLocation& Location,
	    const std::string& FieldName) const
	{
		if (!CurrentFunction || !Module || CurrentFunction->OwnerTypeIndex >= Module->Types.size())
		{
			return true;
		}
		return ValidateMemberAccess(
		    Location,
		    Module->Types[CurrentFunction->OwnerTypeIndex].Name,
		    FieldName,
		    false);
	}

}
