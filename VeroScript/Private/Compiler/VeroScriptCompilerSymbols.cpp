// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "ConstantEvaluation/VeroScriptConstantBranches.h"
#include "VeroScriptCompilerHostLookup.h"
#include "VeroScriptStaticInitializationDependencies.h"

#include "VeroScriptAst.h"
#include "VeroScriptAstTraversal.h"
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptNamespaceLookup.h"
#include "VeroScriptRuntime.h"
#include "Parser/VeroScriptParserBindings.h"
#include "TypeSupport/VeroScriptTypeMemberBinding.h"
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
	VeroScriptCompiler::FEmitter VeroScriptCompiler::MakeEmitter(
	    std::vector<std::uint8_t>& Code,
	    std::vector<FDebugLine>* const DebugLines)
	{
		return FEmitter(
		    Code, DebugLines, &CurrentEmitLocation, Module ? &Module->DebugFiles : nullptr, ActiveNatives,
		    Module ? &Module->NativeReferences : nullptr);
	}

	bool VeroScriptCompiler::MethodSignaturesMatch(
	    const FMethodDecl& Left,
	    const FMethodDecl& Right,
	    const std::string& LeftModulePath,
	    const std::string& RightModulePath,
	    const std::unordered_map<std::string, std::string>& RightSubst) const
	{
		static const std::unordered_map<std::string, std::string> EmptySubst;
		return MethodSignaturesMatch(
		    Left,
		    Right,
		    LeftModulePath,
		    RightModulePath,
		    EmptySubst,
		    RightSubst);
	}
	bool VeroScriptCompiler::MethodSignaturesMatch(
	    const FMethodDecl& Left,
	    const FMethodDecl& Right,
	    const std::string& LeftModulePath,
	    const std::string& RightModulePath,
	    const std::unordered_map<std::string, std::string>& LeftSubst,
	    const std::unordered_map<std::string, std::string>& RightSubst) const
	{
		const bool bBothConversions = Left.Name.rfind("operator conv ", 0) == 0 &&
		    Right.Name.rfind("operator conv ", 0) == 0;
		if ((!bBothConversions && Left.Name != Right.Name) || Left.Parameters.size() != Right.Parameters.size() || Left.bIsConst != Right.bIsConst)
		{
			return false;
		}
		// Conversion targets use the same identity as conversion lookup.
		const auto ReturnIdentity = [&](const FMethodDecl& Method, const std::string& ModulePath,
		                                const std::unordered_map<std::string, std::string>& Subst)
		{
			const auto Type = QualifyTypeName(NormalizeTypeAlias(
			                                      SubstituteTypeNameWith(Method.ReturnType, Subst), ModulePath),
			    ModulePath);
			return bBothConversions ? CanonicalTypeNameForCoercion(Type) : Type;
		};
		if (ReturnIdentity(Left, LeftModulePath, LeftSubst) != ReturnIdentity(Right, RightModulePath, RightSubst))
		{
			return false;
		}
		for (std::size_t Index = 0; Index < Left.Parameters.size(); ++Index)
		{
			const FFunctionParam& LeftParam = Left.Parameters[Index];
			const FFunctionParam& RightParam = Right.Parameters[Index];
			// Keep in sync with CanonicalParamTypeNames, which encodes only "ref" /
			// "const ref" / "..." — by-value `const` is NOT part of method identity, so
			// only compare const-ness when the parameter is a ref.
			if (LeftParam.bIsVariadic != RightParam.bIsVariadic || LeftParam.bIsRef != RightParam.bIsRef || (LeftParam.bIsRef && (LeftParam.bIsConst != RightParam.bIsConst)))
			{
				return false;
			}
			if (QualifyTypeName(
			        NormalizeTypeAlias(
			            SubstituteTypeNameWith(LeftParam.TypeName, LeftSubst),
			            LeftModulePath),
			        LeftModulePath) != QualifyTypeName(NormalizeTypeAlias(SubstituteTypeNameWith(RightParam.TypeName, RightSubst), RightModulePath), RightModulePath))
			{
				return false;
			}
		}
		return true;
	}

	std::vector<std::string> VeroScriptCompiler::BuildLexicalNamespaceLookupNames(
	    const std::string& Name) const
	{
		if (Name.rfind("::", 0) == 0)
		{
			return Name.size() == 2
			    ? std::vector<std::string>{}
			    : std::vector<std::string>{Name.substr(2)};
		}
		if (Name.empty() || Name.find("::") != std::string::npos || Name.find('\x1F') != std::string::npos)
		{
			return {Name};
		}

		if (Name.find('.') != std::string::npos)
		{
			return BindNamespacePath(Name, CurrentNamespacePrefix, [&](const std::string& Prefix)
			{
				return NamespaceExists(Prefix) || !ResolveTypeLinkageNameExact(Prefix, CurrentModulePath).empty();
			});
		}

		std::vector<std::string> Names;
		std::string Prefix = CurrentNamespacePrefix;
		while (!Prefix.empty())
		{
			Names.push_back(Prefix + "." + Name);
			const std::size_t Dot = Prefix.rfind('.');
			Prefix = Dot == std::string::npos ? std::string{} : Prefix.substr(0, Dot);
		}
		Names.push_back(Name);
		return Names;
	}

	std::vector<std::string> VeroScriptCompiler::BuildUsingNamespaceLookupNames(
	    const std::string& Name,
	    const std::string& ReferenceModulePath) const
	{
		std::vector<std::string> Names;
		if (!ActiveProgram || Name.empty() || Name.rfind("::", 0) == 0 || Name.find("::") != std::string::npos || Name.find('\x1F') != std::string::npos)
		{
			return Names;
		}
		// A qualified name keeps its selected lexical prefix even when that
		// namespace/type has no matching member. Using-directives cannot rebind it.
		if (Name.find('.') != std::string::npos && !BuildLexicalNamespaceLookupNames(Name).empty())
		{
			return Names;
		}
		for (const FUsingNamespaceDecl& Directive : ActiveProgram->UsingNamespaces)
		{
			if (!IsUsingNamespaceDirectiveVisible(Directive, ReferenceModulePath))
			{
				continue;
			}
			for (const std::string& Target : BuildUsingNamespaceTargets(Directive))
			{
				if (!NamespaceExists(Target, Directive.ModulePath))
				{
					continue;
				}
				const std::string Candidate = Target + "." + Name;
				if (std::find(Names.begin(), Names.end(), Candidate) == Names.end())
				{
					Names.push_back(Candidate);
				}
				break;
			}
		}
		return Names;
	}

	std::vector<std::string> VeroScriptCompiler::BuildUsingNamespaceTargets(
	    const FUsingNamespaceDecl& Directive) const
	{
		return BindNamespacePath(Directive.NamespaceName, Directive.ScopeNamespace, [&](const std::string& Name)
		{
			return NamespaceExists(Name, Directive.ModulePath);
		});
	}

	bool VeroScriptCompiler::IsUsingNamespaceDirectiveVisible(
	    const FUsingNamespaceDecl& Directive,
	    const std::string& ReferenceModulePath) const
	{
		if (Directive.ModulePath != ReferenceModulePath)
		{
			return false;
		}
		if (!Directive.ScopeNamespace.empty() && CurrentNamespacePrefix != Directive.ScopeNamespace && CurrentNamespacePrefix.rfind(Directive.ScopeNamespace + ".", 0) != 0)
		{
			return false;
		}
		// Like other lexical declarations, a using-directive starts at its point
		// of declaration. An unavailable location only occurs in compiler-wide
		// metadata passes; parsed type names are already qualified in those passes.
		const auto LookupLocation = ResolveProgramLookupLocation(ActiveProgram, CurrentEmitLocation);
		if (!LookupLocation.FileLabel.empty())
		{
			if (LookupLocation.Line < Directive.Location.Line)
			{
				return false;
			}
			if (LookupLocation.Line == Directive.Location.Line && LookupLocation.Column < Directive.Location.Column)
			{
				return false;
			}
		}
		return true;
	}

	bool VeroScriptCompiler::NamespaceExists(const std::string& Name, const std::string& ReferenceModulePath) const
	{
		if (StandardLibrary::IsNamespace(Name)) return true;
		if (!ActiveProgram || Name.empty()) return false;
		const auto Count = ActiveProgram->Namespaces.size() + ActiveProgram->Structs.size()
		    + ActiveProgram->Classes.size() + ActiveProgram->Interfaces.size() + ActiveProgram->Enums.size()
		    + ActiveProgram->TypeAliases.size() + ActiveProgram->Functions.size() + ActiveProgram->TopLevelStatements.size();
		if (!NamespaceLookup || NamespaceDeclarationCount != Count)
		{
			NamespaceLookup = std::make_shared<FNamespaceLookup>(*ActiveProgram);
			NamespaceDeclarationCount = Count;
			if (ActiveNatives)
			{
				for (const auto& Native : *ActiveNatives)
				{
					NamespaceLookup->Add(Native.Name, Native.Origin.ModulePath, Native.Origin.bExported, false);
				}
			}
			if (ActiveBindings)
			{
				const auto Add = [&](const auto &Values)
				{
					for (const auto &Value : Values)
					{
						NamespaceLookup->Add(Value.Name, Value.Origin.ModulePath, Value.Origin.bExported, false);
					}
				};
				Add(ActiveBindings->GetTypes());
				Add(ActiveBindings->GetTypeAliases());
				Add(ActiveBindings->GetGenericTypeTemplates());
			}
		}
		return NamespaceLookup->Contains(*ActiveProgram, Name,
		    ReferenceModulePath.empty() ? CurrentModulePath : ReferenceModulePath, ActiveImportIndex);
	}

	std::string VeroScriptCompiler::NamespaceFromFunctionLinkage(const std::string& Name) const
	{
		std::string Surface = Name;
		// Generic concrete types may themselves contain private-linkage separators.
		// Trim length-encoded generic/signature data before inspecting the base name.
		if (const std::size_t Hash = Surface.find('#'); Hash != std::string::npos)
		{
			Surface.resize(Hash);
		}
		if (const std::size_t PrivateSeparator = Surface.rfind('\x1F');
		    PrivateSeparator != std::string::npos)
		{
			Surface.erase(0, PrivateSeparator + 1);
		}
		if (const std::size_t MethodSeparator = Surface.rfind("::");
		    MethodSeparator != std::string::npos)
		{
			Surface.resize(MethodSeparator);
		}
		const std::size_t Dot = Surface.rfind('.');
		return Dot == std::string::npos ? std::string{} : Surface.substr(0, Dot);
	}

	bool VeroScriptCompiler::ValidateUsingNamespaceDirectives() const
	{
		if (!ActiveProgram)
		{
			return true;
		}
		for (const FUsingNamespaceDecl& Directive : ActiveProgram->UsingNamespaces)
		{
			bool bFound = false;
			for (const std::string& Target : BuildUsingNamespaceTargets(Directive))
			{
				if (NamespaceExists(Target, Directive.ModulePath))
				{
					bFound = true;
					break;
				}
			}
			if (!bFound)
			{
				ReportError(
				    "unknown namespace in using directive: " + Directive.NamespaceName,
				    Directive.Location);
				return false;
			}
		}
		return true;
	}

	bool VeroScriptCompiler::IsStructType(const std::string& TypeName) const
	{
		if (Runtime::IsPrimitiveValueTypeName(TypeName) || TypeName == "void")
		{
			return false;
		}
		if (ActiveBindings)
		{
			if (const FHostTypeDesc* const HostType = ActiveBindings->FindType(TypeName);
			    HostType && HostType->Kind == EHostTypeKind::Struct)
			{
				return true;
			}
		}
		if (!ActiveProgram)
		{
			return false;
		}
		if (Module)
		{
			std::string GenericBase;
			std::vector<std::string> GenericArgs;
			const std::string Qualified = SplitUserGenericTypeApp(TypeName, GenericBase, GenericArgs)
			    ? QualifyTypeName(TypeName, CurrentModulePath)
			    : TypeName;
			for (const auto Index : {FindRegisteredTypeIndex(TypeName), FindRegisteredTypeIndex(Qualified)})
			{
				if (Index < Module->Types.size() && Module->Types[Index].Kind == EScriptObjectKind::Struct)
				{
					return true;
				}
			}
		}
		std::string LinkageName = ResolveTypeLinkageName(TypeName, CurrentModulePath);
		std::string OwnerGenericBase;
		std::vector<std::string> OwnerGenericArgs;
		if (LinkageName.empty() && SplitUserGenericTypeApp(TypeName, OwnerGenericBase, OwnerGenericArgs))
		{
			LinkageName = ResolveTypeLinkageName(OwnerGenericBase, CurrentModulePath);
		}
		if (LinkageName.empty())
		{
			const std::string TemplateBase = GenericTemplateBaseLinkage(TypeName);
			if (!TemplateBase.empty())
			{
				LinkageName = TemplateBase.find('\x1F') != std::string::npos
				    ? TemplateBase
				    : ResolveTypeLinkageName(TemplateBase, CurrentModulePath);
			}
		}
		if (LinkageName.empty())
		{
			return false;
		}
		// A closed generic identity may be used before its layout is instantiated.
		// Decode the whole identity so a fixed array suffix is not a struct owner.
		const auto ConcreteTypes = DecodeGenericConcreteTypes(LinkageName);
		if (!ConcreteTypes.empty())
		{
			LinkageName = GenericTemplateBaseLinkage(LinkageName);
		}
		for (const auto Index : FindDeclarationCandidates(SourceStructIndices, LinkageName))
		{
			const FStructDecl& StructDecl = ActiveProgram->Structs[Index];
			if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) == LinkageName && (ConcreteTypes.empty() || TemplateParameterNames(StructDecl).size() == ConcreteTypes.size()))
			{
				return true;
			}
		}
		for (const FStructDecl& StructDecl : BuiltinStructTemplates)
		{
			if ((GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) == LinkageName || StructDecl.Name == TypeName) && (ConcreteTypes.empty() || TemplateParameterNames(StructDecl).size() == ConcreteTypes.size()))
			{
				return true;
			}
			std::string GenericBase;
			std::vector<std::string> BuiltinArgs;
			if (SplitUserGenericTypeApp(TypeName, GenericBase, BuiltinArgs) && GenericBase == StructDecl.Name && BuiltinArgs.size() == TemplateParameterNames(StructDecl).size())
			{
				return true;
			}
		}
		return false;
	}

	bool VeroScriptCompiler::IsAllowedMapOrSetKeyTypeForCompile(
	    const std::string_view TypeName) const
	{
		if (TypeName == "variant") return false;
		if (Runtime::IsAllowedMapOrSetKeyType(TypeName))
		{
			return true;
		}
		if (IsScopedEnumTypeName(TypeName))
		{
			return true;
		}
		if (!ActiveBindings)
		{
			return false;
		}
		const FHostTypeDesc* const HostType = ActiveBindings->FindType(TypeName);
		return HostType && (HostType->Kind == EHostTypeKind::Class || (HostType->Kind == EHostTypeKind::Struct && HostType->CloneCallback && HostType->EqualsCallback && HostType->HashCallback));
	}

	bool VeroScriptCompiler::ParseMapTypeForCompile(
	    const std::string_view TypeName,
	    std::string& OutKeyType,
	    std::string& OutValueType) const
	{
		if (!Runtime::SplitMapType(TypeName, OutKeyType, OutValueType) || !IsAllowedMapOrSetKeyTypeForCompile(OutKeyType))
		{
			return false;
		}
		if (Runtime::IsMultiMapTypeName(TypeName))
		{
			OutValueType = "array<" + OutValueType + ">";
		}
		return true;
	}

	bool VeroScriptCompiler::ParseSetElementTypeForCompile(
	    const std::string_view TypeName,
	    std::string& OutElementType) const
	{
		return Runtime::SplitSetElementType(TypeName, OutElementType) && IsAllowedMapOrSetKeyTypeForCompile(OutElementType);
	}

	bool VeroScriptCompiler::ParseOrderedMapTypeForCompile(
	    const std::string_view TypeName,
	    std::string& OutKeyType,
	    std::string& OutValueType) const
	{
		if (!Runtime::SplitOrderedMapType(TypeName, OutKeyType, OutValueType) || !IsAllowedMapOrSetKeyTypeForCompile(OutKeyType))
		{
			return false;
		}
		if (Runtime::IsOrderedMultiMapTypeName(TypeName))
		{
			OutValueType = "array<" + OutValueType + ">";
		}
		return true;
	}

	bool VeroScriptCompiler::NeedsPassByValueCopy(const std::string& TypeName) const
	{
		// Ref bindings are aliases — never deep-copy (`EmitClonePassByValueArgs` contract).
		if (TypeName.rfind("ref ", 0) == 0 || TypeName.rfind("const ref ", 0) == 0)
		{
			return false;
		}
		return IsStructType(TypeName) || Runtime::IsNativeArrayTypeName(TypeName);
	}

	bool VeroScriptCompiler::ExpressionYieldsUnaliasedByValue(const FExpr& Expression, const std::string& TargetType) const
	{
		bool Owned = [&]()
		{
			// Conditional branches materialize their selected value. Constructor
			// temporaries transfer their sole lifetime root with TakeLocal.
			if (Expression.Kind == EExprKind::Conditional) return true;
			if (Expression.Kind == EExprKind::ArrayInit || Expression.Kind == EExprKind::MapInit) return true;
			if (Expression.Kind == EExprKind::Identifier)
			{
				FScope::FMapEntryViewBinding View;
				if (TryGetMapEntryViewBinding(static_cast<const FIdentifierExpr&>(Expression).Name, View)) return true;
			}
			if (Expression.Kind == EExprKind::MemberAccess)
			{
				const auto& Member = static_cast<const FMemberAccessExpr&>(Expression);
				FScope::FMapEntryViewBinding View;
				if (Member.Member == "first" && Member.Object && Member.Object->Kind == EExprKind::Identifier && TryGetMapEntryViewBinding(static_cast<const FIdentifierExpr&>(*Member.Object).Name, View))
				{
					return true;
				}
			}
			if (Expression.Kind == EExprKind::IndexAccess)
			{
				std::string RawReturn;
				return TryResolveExprRawTypeName(Expression, RawReturn) && !Runtime::IsReferenceTypeName(RawReturn) && NeedsPassByValueCopy(RawReturn);
			}
			if (Expression.Kind == EExprKind::MethodCall)
			{
				const auto& Call = static_cast<const FMethodCallExpr&>(Expression);
				std::string Type, RawReturn;
				if (Call.Object && TryResolveExprTypeName(*Call.Object, Type) && ActiveBindings && ActiveBindings->FindType(Type))
				{
					return false;
				}
				return TryResolveExprRawTypeName(Expression, RawReturn) && !Runtime::IsReferenceTypeName(RawReturn) && NeedsPassByValueCopy(RawReturn);
			}
			if (Expression.Kind != EExprKind::Call)
			{
				return false;
			}
			const auto& Call = static_cast<const FCallExpr&>(Expression);
			if ((Call.Callee == "cast" || Call.Callee == "try_cast") && Call.TypeArgs.size() == 1)
			{
				std::string Return;
				return TryResolveExprTypeName(Expression, Return) && IsStructType(Return);
			}
			if (Call.CalleeExpr)
			{
				std::string Return;
				return TryResolveExprRawTypeName(Expression, Return) && !Runtime::IsReferenceTypeName(Return) && NeedsPassByValueCopy(Return);
			}
			const bool bBuiltinAccessible =
			    Call.bStandardLibraryQualified || Call.bImplementationBuiltinAccess || StandardLibrary::RequiredNamespaceForFunction(Call.Callee).empty();
			if (Call.CalleeExpr || Call.Callee.empty() || (bBuiltinAccessible && Builtins::Lookup(Call.Callee)))
			{
				return false;
			}
			// Native (host-registered) calls do not pass through the script return-site
			// CloneStruct, so their result ownership/aliasing is unknown — never skip the
			// consumer clone for them.
			std::uint16_t NativeIndex = 0;
			if (FindNative(Call.Callee, NativeIndex))
			{
				return false;
			}
			const std::string EffectiveCallee = SubstituteTypeName(Call.Callee);
			if (IsStructType(EffectiveCallee))
			{
				return true;
			}
			if (IsInterfaceTypeName(EffectiveCallee))
			{
				return false;
			}
			if (Module)
			{
				std::string GenericBase;
				std::vector<std::string> GenericArgs;
				const std::string Qualified = SplitUserGenericTypeApp(EffectiveCallee, GenericBase, GenericArgs)
				    ? QualifyTypeName(EffectiveCallee, CurrentModulePath)
				    : EffectiveCallee;
				const std::string Linkage = ResolveTypeLinkageName(EffectiveCallee, CurrentModulePath);
				for (const FBytecodeTypeDesc& TypeDesc : Module->Types)
				{
					if (TypeDesc.Kind == EScriptObjectKind::Class && (TypeDesc.Name == EffectiveCallee || TypeDesc.Name == Qualified || (!Linkage.empty() && TypeDesc.Name == Linkage)))
					{
						return false;
					}
				}
			}
			if (ActiveProgram)
			{
				const std::string Linkage = ResolveTypeLinkageName(EffectiveCallee, CurrentModulePath);
				std::string GenericBase;
				std::vector<std::string> GenericArgs;
				const bool bGenericApp = SplitUserGenericTypeApp(EffectiveCallee, GenericBase, GenericArgs);
				for (const FClassDecl& ClassDecl : ActiveProgram->Classes)
				{
					const std::string ClassLinkage = GetSymbolLinkageName(
					    ClassDecl.ModulePath,
					    ClassDecl.bExported,
					    ClassDecl.Name);
					if (ClassDecl.Name == EffectiveCallee || ClassLinkage == Linkage || (bGenericApp && ClassDecl.Name == GenericBase))
					{
						return false;
					}
				}
			}
			// A reference return is borrowed storage even when its underlying type
			// is a struct. Inspect the raw signature before considering copy elision.
			std::string ReturnTypeName;
			if (!TryResolveExprRawTypeName(Expression, ReturnTypeName) || Runtime::IsReferenceTypeName(ReturnTypeName) || !NeedsPassByValueCopy(ReturnTypeName))
			{
				return false;
			}
			return true;
		}();
		if (Owned && !TargetType.empty())
		{
			if (Expression.Kind == EExprKind::ArrayInit && static_cast<const FArrayInitExpr&>(Expression).TypeName.empty()) return true;
			std::string Type;
			Owned = TryResolveExprTypeName(Expression, Type) && CanonicalTypeNameForCoercion(Type) == CanonicalTypeNameForCoercion(TargetType);
		}
		return Owned;
	}

	std::uint16_t VeroScriptCompiler::AllocateAssignTemp()
	{
		return AllocateLocal("$a" + std::to_string(NextAssignTempId++));
	}

	const VeroScriptCompiler::FUserTypeAlias* VeroScriptCompiler::FindUserTypeAlias(
	    const std::string& SimpleName,
	    const std::string& ReferenceModulePath) const
	{
		const FUserTypeAlias* Selected = nullptr;
		const auto Range = UserTypeAliases.equal_range(SimpleName);
		for (auto It = Range.first; It != Range.second; ++It)
		{
			const FUserTypeAlias& Alias = It->second;
			if (!CanAccessSymbol(Alias.ModulePath, Alias.bExported, ReferenceModulePath))
			{
				continue;
			}
			if (Alias.ModulePath == ReferenceModulePath)
			{
				return &Alias;
			}
			// Preserve historic last-wins among imported aliases; prefer exported over private.
			if (!Selected || (Alias.bExported && !Selected->bExported) || (Alias.bExported == Selected->bExported))
			{
				Selected = &Alias;
			}
		}
		return Selected;
	}

	std::size_t VeroScriptCompiler::TypeParamArityFromLinkage(
	    const std::string& TypeLinkageName) const
	{
		if (GenericTemplateBaseLinkage(TypeLinkageName).empty())
		{
			return 0;
		}
		return DecodeGenericConcreteTypes(TypeLinkageName).size();
	}

	bool VeroScriptCompiler::IsEnumTypeName(const std::string_view TypeName) const
	{
		if (Runtime::IsPrimitiveValueTypeName(TypeName) || TypeName == "void")
		{
			return false;
		}
		if (FindEnumDeclaration(std::string(TypeName))) return true;
		if (EnumTypeNames.find(std::string(TypeName)) != EnumTypeNames.end())
		{
			return true;
		}
		if (ActiveBindings)
		{
			if (const FHostTypeDesc* const HostType =
			        CompilerDetail::FindHostTypeWithNamespaceSpelling(ActiveBindings, TypeName);
			    HostType && HostType->Kind == EHostTypeKind::Enum)
			{
				return true;
			}
			// UE-style namespace enum: `namespace EReason { enum Type {...}; }`.
			// The type is `EReason::Type`, while enumerators are written
			// `EReason::Value`.
			const std::string NamespaceType =
			    std::string(TypeName) + "::Type";
			if (const FHostTypeDesc* const HostType =
			        ActiveBindings->FindType(NamespaceType);
			    HostType && HostType->Kind == EHostTypeKind::Enum)
			{
				return true;
			}
		}
		if (!ActiveProgram)
		{
			return false;
		}
		for (const FEnumDecl& EnumDecl : ActiveProgram->Enums)
		{
			if (EnumDecl.Name == TypeName && CanAccessSymbol(EnumDecl.ModulePath, EnumDecl.bExported, CurrentModulePath))
			{
				return true;
			}
		}
		return false;
	}

	bool VeroScriptCompiler::IsScopedEnumTypeName(const std::string_view TypeName) const
	{
		if (const auto* Declaration = FindEnumDeclaration(std::string(TypeName))) return Declaration->bScoped;
		if (ActiveBindings)
		{
			const auto IsStrongHostEnum = [](const FHostTypeDesc* const HostType) -> bool
			{
				if (!HostType || HostType->Kind != EHostTypeKind::Enum)
				{
					return false;
				}
				if (HostType->bScopedEnum)
				{
					return true;
				}
				// Backward-compatible inference for catalogs which already emit typed
				// enum members but predate the explicit scoped-enum flag.
				for (const FHostMemberDesc& Member : HostType->Members)
				{
					if (Member.Kind == EHostMemberKind::EnumMember && Member.ReturnHostTypeName == HostType->Name)
					{
						return true;
					}
				}
				return false;
			};
			if (IsStrongHostEnum(ActiveBindings->FindType(TypeName)))
			{
				return true;
			}
			const std::string NamespaceType = std::string(TypeName) + "::Type";
			if (IsStrongHostEnum(ActiveBindings->FindType(NamespaceType)))
			{
				return true;
			}
		}
		if (!ActiveProgram)
		{
			return false;
		}
		for (const FEnumDecl& EnumDecl : ActiveProgram->Enums)
		{
			if (!EnumDecl.bScoped || !CanAccessSymbol(EnumDecl.ModulePath, EnumDecl.bExported, CurrentModulePath))
			{
				continue;
			}
			if (EnumDecl.Name == TypeName || GetSymbolLinkageName(EnumDecl.ModulePath, EnumDecl.bExported, EnumDecl.Name) == TypeName)
			{
				return true;
			}
		}
		return false;
	}

	std::string VeroScriptCompiler::ExpandUserTypeAlias(
	    const std::string& TypeName,
	    const std::string& ReferenceModulePath,
	    const int Depth) const
	{
		if (TypeName.empty() || Depth > 32)
		{
			return TypeName;
		}
		const bool bGlobalQualified = TypeName.rfind("::", 0) == 0;
		const std::string LookupName = bGlobalQualified ? TypeName.substr(2) : TypeName;
		if (!bGlobalQualified)
		{
			for (auto ScopeIt = LocalTypeAliasScopes.rbegin();
			    ScopeIt != LocalTypeAliasScopes.rend();
			    ++ScopeIt)
			{
				const auto LocalIt = ScopeIt->find(TypeName);
				if (LocalIt != ScopeIt->end())
				{
					return ExpandUserTypeAlias(
					    LocalIt->second, ReferenceModulePath, Depth + 1);
				}
			}
		}
		const FUserTypeAlias* Alias =
		    FindUserTypeAlias(LookupName, ReferenceModulePath);
		std::vector<std::string> Arguments;
		if (!Alias)
		{
			std::string Base;
			if (SplitUserGenericTypeApp(LookupName, Base, Arguments))
			{
				Alias = FindUserTypeAlias(Base, ReferenceModulePath);
				if (Alias && (Alias->EnclosingParameterNames.empty() || Alias->EnclosingParameterNames.size() != Arguments.size()))
				{
					Alias = nullptr;
				}
			}
		}
		if (!Alias)
		{
			return TypeName;
		}
		std::string TargetTypeName = Alias->TargetTypeName;
		if (!Arguments.empty())
		{
			TargetTypeName = SubstituteScopedTypeParameters(TargetTypeName, Alias->EnclosingParameterNames, Arguments);
		}
		if (bGlobalQualified && TargetTypeName.rfind("::", 0) != 0 && TargetTypeName.find('.') == std::string::npos && TargetTypeName.find("::") == std::string::npos && !Runtime::IsCommonContainerTypeName(TargetTypeName) && !Runtime::IsFunctionTypeName(TargetTypeName))
		{
			// The target of a root-selected alias is resolved in that alias's root
			// declaration scope. Keep a bare user-type target anchored while the
			// recursive expansion proceeds; otherwise `::Alias` inside namespace N
			// could accidentally become N.Target.
			const bool bLooksLikeUserType =
			    FindUserTypeAlias(TargetTypeName, ReferenceModulePath) != nullptr || !ResolveTypeLinkageNameExact(TargetTypeName, ReferenceModulePath).empty() || IsEnumTypeName(TargetTypeName) || TargetTypeName.find('<') != std::string::npos;
			if (bLooksLikeUserType)
			{
				TargetTypeName.insert(0, "::");
			}
		}
		return ExpandUserTypeAlias(TargetTypeName, ReferenceModulePath, Depth + 1);
	}

	std::string VeroScriptCompiler::ExpandHostTypeAlias(
	    const std::string& TypeName,
	    const int /*Depth*/) const
	{
		if (!ActiveBindings || TypeName.empty())
		{
			return TypeName;
		}
		std::string RuntimeTypeName;
		std::string AliasError;
		return ResolveHostRuntimeTypeName(
		           *ActiveBindings, TypeName, RuntimeTypeName, AliasError)
		    ? RuntimeTypeName
		    : TypeName;
	}

	std::string VeroScriptCompiler::NormalizeTypeAlias(std::string TypeName) const
	{
		return NormalizeTypeAlias(std::move(TypeName), CurrentModulePath);
	}

	std::string VeroScriptCompiler::NormalizeTypeAlias(
	    std::string TypeName,
	    const std::string& ReferenceModulePath) const
	{
		if (Runtime::IsReferenceTypeName(TypeName))
		{
			const std::string Prefix = Runtime::IsConstReferenceTypeName(TypeName)
			    ? "const ref "
			    : "ref ";
			return Prefix + NormalizeTypeAlias(std::string(Runtime::StripReferenceTypeName(TypeName)), ReferenceModulePath);
		}
		// Reserved primitive spellings cannot name user/host aliases or enums.
		// Preserve their canonical ABI aliases, including the referenced spelling.
		if (Runtime::IsPrimitiveValueTypeName(TypeName) || TypeName == "void")
		{
			return std::string(CanonicalizeNativeScalarTypeName(TrimNativeTypeName(TypeName)));
		}
		TypeName = ExpandUserTypeAlias(TypeName, ReferenceModulePath);
		TypeName = ExpandHostTypeAlias(TypeName);
		if (ActiveProgram)
		{
			const FEnumDecl* SelectedEnum = FindEnumDeclaration(SubstituteTypeName(TypeName));
			for (const FEnumDecl& EnumDecl : ActiveProgram->Enums)
			{
				if (!CanAccessSymbol(
				        EnumDecl.ModulePath,
				        EnumDecl.bExported,
				        ReferenceModulePath))
				{
					continue;
				}
				const std::string LinkageName = GetSymbolLinkageName(
				    EnumDecl.ModulePath,
				    EnumDecl.bExported,
				    EnumDecl.Name);
				if (EnumDecl.Name != TypeName && LinkageName != TypeName)
				{
					continue;
				}
				if (LinkageName == TypeName || EnumDecl.ModulePath == ReferenceModulePath)
				{
					SelectedEnum = &EnumDecl;
					break;
				}
				if (!SelectedEnum)
				{
					SelectedEnum = &EnumDecl;
				}
			}
			if (SelectedEnum)
			{
				// Strong enums retain their source/runtime type spelling here.
				// Callers that need linkage identity qualify it separately.
				return SelectedEnum->bScoped
				    ? TypeName
				    : SelectedEnum->UnderlyingTypeName;
			}
		}
		if (ActiveBindings)
		{
			if (const FHostTypeDesc* const HostType = ActiveBindings->FindType(TypeName);
			    HostType && HostType->Kind == EHostTypeKind::Enum)
			{
				// Strong host enums retain their complete runtime identity; legacy
				// unscoped bindings keep the established integer ABI.
				return IsScopedEnumTypeName(TypeName) ? TypeName : "int";
			}
		}
		if (IsEnumTypeName(TypeName) && !IsScopedEnumTypeName(TypeName))
		{
			return "int";
		}
		return TypeName;
	}

	std::string VeroScriptCompiler::ResolveEnumConstantLinkageName(
	    const std::string& Name,
	    const std::string& ReferenceModulePath) const
	{
		for (auto Scope = Scopes.rbegin(); Scope != Scopes.rend(); ++Scope)
		{
			if (Scope->Locals.count(Name) || Scope->StaticLocals.count(Name)) return {};
			const auto Entry = Scope->EnumConstants.find(Name);
			if (Entry != Scope->EnumConstants.end()) return Entry->second;
		}
		if (!ActiveProgram)
		{
			return {};
		}

		const auto* Lexical = FindSourceTypeContext(CurrentOwnerTypeLinkage);
		for (std::size_t Depth = 0; Lexical && Depth < 64; ++Depth)
		{
			for (const auto& Enum : ActiveProgram->Enums)
			{
				if (!Enum.bScoped && Enum.Scope.ScopeId == Lexical->Scope->Id && std::any_of(Enum.Members.begin(), Enum.Members.end(), [&](const auto& Member)
				{
					return Member.Name == Name;
				}))
				{
					return GetSymbolLinkageName(Enum.ModulePath, Enum.bExported, Enum.Name + "." + Name);
				}
			}
			const auto ParentId = Lexical->Context && Lexical->Context->TypeScope ? Lexical->Context->TypeScope->Id : std::string{};
			Lexical = nullptr;
			for (const auto& Entry : SourceTypeContexts)
			{
				if (Entry.second.Scope->Id == ParentId)
				{
					Lexical = &Entry.second;
					break;
				}
			}
		}

		const auto LocalEnumVisible = [&](const FEnumDecl& Enum)
		{
			if (!Enum.Scope.bLocal) return true;
			const auto* Owner = FindSourceTypeContext(CurrentOwnerTypeLinkage);
			if (!Owner || !Owner->Context || !Owner->Context->Bindings) return false;
			const auto& Bindings = *Owner->Context->Bindings;
			for (auto I = Bindings.Aliases.rbegin(); I != Bindings.Aliases.rend(); ++I)
			{
				for (auto Head = *I; Head != EmptyBindingHead && Head < Bindings.RecordCount;)
				{
					const auto& Binding = Bindings.Arena->Get(Head);
					if (Binding.Name == Enum.Scope.SimpleName)
					{
						return Binding.Type.substr(0, Binding.Type.find('<')) == Enum.Name;
					}
					if (Binding.Previous != EmptyBindingHead && Binding.Previous >= Head) return false;
					Head = Binding.Previous;
				}
			}
			return false;
		};
		const auto ResolveExact = [&](const std::string& Candidate) -> std::string
		{
			const FEnumDecl* SelectedEnum = nullptr;
			std::string SelectedConstant;
			for (const FEnumDecl& EnumDecl : ActiveProgram->Enums)
			{
				if (EnumDecl.bScoped || !LocalEnumVisible(EnumDecl) || !CanAccessSymbol(EnumDecl.ModulePath, EnumDecl.bExported, ReferenceModulePath))
				{
					continue;
				}
				for (const FEnumMemberDecl& Member : EnumDecl.Members)
				{
					std::string InjectedName = Member.Name;
					if (const std::size_t Dot = EnumDecl.Name.rfind('.');
					    Dot != std::string::npos)
					{
						InjectedName = (EnumDecl.Scope.OwnerTypeName.empty() ? EnumDecl.Name.substr(0, Dot + 1)
						                                                     : EnumDecl.Scope.OwnerTypeName + ".") +
						    Member.Name;
					}
					if (InjectedName != Candidate)
					{
						continue;
					}
					if (EnumDecl.ModulePath == ReferenceModulePath)
					{
						return GetSymbolLinkageName(
						    EnumDecl.ModulePath, EnumDecl.bExported, EnumDecl.Name + "." + Member.Name);
					}
					if (!SelectedEnum)
					{
						SelectedEnum = &EnumDecl;
						SelectedConstant = GetSymbolLinkageName(EnumDecl.ModulePath, EnumDecl.bExported, EnumDecl.Name + "." + Member.Name);
					}
				}
			}
			return SelectedConstant;
		};

		for (const std::string& Candidate : BuildLexicalNamespaceLookupNames(Name))
		{
			if (std::string Resolved = ResolveExact(Candidate); !Resolved.empty())
			{
				return Resolved;
			}
		}
		std::vector<std::string> Matches;
		for (const std::string& Candidate :
		    BuildUsingNamespaceLookupNames(Name, ReferenceModulePath))
		{
			if (std::string Resolved = ResolveExact(Candidate); !Resolved.empty() && std::find(Matches.begin(), Matches.end(), Resolved) == Matches.end())
			{
				Matches.push_back(std::move(Resolved));
			}
		}
		if (Matches.size() == 1)
		{
			return Matches.front();
		}
		if (Matches.size() > 1)
		{
			ReportError("ambiguous enum member from using namespace: " + Name,
			    CurrentEmitLocation);
		}
		return {};
	}

	bool VeroScriptCompiler::IsEnumConstantName(const std::string& Name) const
	{
		return !ResolveEnumConstantLinkageName(Name, CurrentModulePath).empty();
	}

	bool VeroScriptCompiler::TryResolveEnumQualifiedConstant(
	    const FExpr& ObjectExpr,
	    const std::string& MemberName,
	    std::int64_t& OutValue,
	    std::string& OutDisplayName,
	    std::string* const OutScopedEnumTypeName) const
	{
		if (OutScopedEnumTypeName)
		{
			OutScopedEnumTypeName->clear();
		}
		if (ObjectExpr.Kind != EExprKind::Identifier)
		{
			return false;
		}

		const std::string& WrittenTypeName =
		    static_cast<const FIdentifierExpr&>(ObjectExpr).Name;
		// A value binding (local/global/this-field) wins over the type name for `Name.Member`.
		std::string BoundTypeName;
		if (TryResolveVariableTypeName(WrittenTypeName, BoundTypeName))
		{
			return false;
		}
		std::uint16_t ThisFieldIndex = 0;
		if (ResolveThisField(WrittenTypeName, ThisFieldIndex))
		{
			return false;
		}
		std::string TypeName = SubstituteTypeName(NormalizeTypeAlias(WrittenTypeName));
		if (!IsEnumTypeName(TypeName))
		{
			bool bFoundLexical = false;
			for (const std::string& Candidate :
			    BuildLexicalNamespaceLookupNames(WrittenTypeName))
			{
				if (IsEnumTypeName(Candidate))
				{
					TypeName = Candidate;
					bFoundLexical = true;
					break;
				}
			}
			if (!bFoundLexical)
			{
				std::vector<std::string> Matches;
				for (const std::string& Candidate :
				    BuildUsingNamespaceLookupNames(WrittenTypeName, CurrentModulePath))
				{
					if (IsEnumTypeName(Candidate))
					{
						Matches.push_back(Candidate);
					}
				}
				if (Matches.size() != 1)
				{
					if (Matches.size() > 1)
					{
						ReportError(
						    "ambiguous enum type from using namespace: " + WrittenTypeName,
						    ObjectExpr.Location);
					}
					return false;
				}
				TypeName = Matches.front();
			}
		}

		if (ActiveProgram)
		{
			const FEnumDecl* SelectedEnum = FindEnumDeclaration(SubstituteTypeName(TypeName));
			for (const FEnumDecl& EnumDecl : ActiveProgram->Enums)
			{
				if (!CanAccessSymbol(EnumDecl.ModulePath, EnumDecl.bExported, CurrentModulePath))
				{
					continue;
				}
				const bool bNameMatch = EnumDecl.Name == TypeName || GetSymbolLinkageName(EnumDecl.ModulePath, EnumDecl.bExported, EnumDecl.Name) == TypeName;
				if (!bNameMatch)
				{
					continue;
				}
				if (EnumDecl.ModulePath == CurrentModulePath)
				{
					SelectedEnum = &EnumDecl;
					break;
				}
				if (!SelectedEnum)
				{
					SelectedEnum = &EnumDecl;
				}
			}
			if (SelectedEnum)
			{
				for (const FEnumMemberDecl& Member : SelectedEnum->Members)
				{
					if (Member.Name != MemberName)
					{
						continue;
					}
					OutValue = Member.Value;
					if (Member.Initializer)
					{
						FIdentifierExpr Reference(SubstituteTypeName(TypeName) + "." + Member.Name);
						const auto Value = EvaluateTemplateExpression(Reference, CurrentModulePath, ActiveTypeSubst, CurrentNamespacePrefix, false);
						if (!Value.Succeeded())
						{
							ReportError("invalid enum constant: " + Value.Reason, Member.Location);
							return false;
						}
						auto Bits = Value.Value.Bits;
						const auto& Type = Value.Value.Type;
						if (!Type.bUnsigned && Type.Width < 64 && (Bits & (std::uint64_t{1} << (Type.Width - 1))))
						{
							Bits |= ~((std::uint64_t{1} << Type.Width) - 1);
						}
						OutValue = static_cast<std::int64_t>(Bits);
					}
					OutDisplayName = (SelectedEnum->Scope.DisplayName.empty() ? SelectedEnum->Name : SelectedEnum->Scope.DisplayName) + "." + MemberName;
					if (OutScopedEnumTypeName && SelectedEnum->bScoped)
					{
						*OutScopedEnumTypeName = QualifyTypeName(SubstituteTypeName(TypeName), CurrentModulePath);
					}
					return true;
				}
			}
		}

		if (ActiveBindings)
		{
			const FHostTypeDesc* HostEnum =
			    CompilerDetail::FindHostTypeWithNamespaceSpelling(ActiveBindings, TypeName);
			if ((!HostEnum || HostEnum->Kind != EHostTypeKind::Enum) && TypeName.find("::") == std::string::npos)
			{
				HostEnum = CompilerDetail::FindHostTypeWithNamespaceSpelling(
				    ActiveBindings, TypeName + "::Type");
			}
			if (HostEnum && HostEnum->Kind == EHostTypeKind::Enum)
			{
				for (const FHostMemberDesc& HostMember : HostEnum->Members)
				{
					if (HostMember.Kind != EHostMemberKind::EnumMember || HostMember.Name != MemberName)
					{
						continue;
					}
					OutValue = HostMember.EnumValue;
					OutDisplayName = HostEnum->Name + "." + MemberName;
					if (OutScopedEnumTypeName && !HostMember.ReturnHostTypeName.empty())
					{
						// A declared host type marks this as a typed host enum member.
						// Legacy host enums without that marker remain integer-compatible.
						*OutScopedEnumTypeName =
						    HostMember.ReturnHostTypeName;
					}
					return true;
				}
			}
		}

		{
			const auto It = EnumConstants.find(TypeName + "." + MemberName);
			if (It != EnumConstants.end())
			{
				OutValue = It->second;
				OutDisplayName = TypeName + "." + MemberName;
				return true;
			}
		}
		return false;
	}

	bool VeroScriptCompiler::IsTypeNameWithoutValueBinding(const std::string& Name) const
	{
		std::string BoundTypeName;
		if (TryResolveVariableTypeName(Name, BoundTypeName))
		{
			return false;
		}
		std::uint16_t ThisFieldIndex = 0;
		if (ResolveThisField(Name, ThisFieldIndex))
		{
			return false;
		}
		if (IsEnumTypeName(Name) || IsStructType(Name) || IsInterfaceTypeName(Name))
		{
			return true;
		}
		if (ActiveBindings && ActiveBindings->FindType(Name) != nullptr)
		{
			return true;
		}
		{
			std::uint16_t NativeIndex = static_cast<std::uint16_t>(-1);
			if (const FNativeFunction* const Native = FindNative(Name, NativeIndex);
			    Native && Native->ReturnType == ENativeType::HostObject)
			{
				return true;
			}
		}
		for (auto ScopeIt = LocalTypeAliasScopes.rbegin();
		    ScopeIt != LocalTypeAliasScopes.rend();
		    ++ScopeIt)
		{
			if (ScopeIt->find(Name) != ScopeIt->end())
			{
				return true;
			}
		}
		if (FindUserTypeAlias(Name, CurrentModulePath))
		{
			return true;
		}
		if (!ActiveProgram)
		{
			return false;
		}
		const std::string LinkageName = ResolveTypeLinkageName(Name, CurrentModulePath);
		if (LinkageName.empty())
		{
			return false;
		}
		for (const FClassDecl& ClassDecl : ActiveProgram->Classes)
		{
			if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) == LinkageName)
			{
				return true;
			}
		}
		for (const FClassDecl& ClassDecl : BuiltinClassTemplates)
		{
			if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) == LinkageName)
			{
				return true;
			}
		}
		return false;
	}

	bool VeroScriptCompiler::TryResolveTypeNameReceiver(
	    const FExpr& Expression, std::string& OutTypeName) const
	{
		if (!TryBuildQualifiedExpressionName(Expression, OutTypeName))
		{
			return false;
		}
		const std::string RootName = OutTypeName.substr(0, OutTypeName.find('.'));
		std::string BoundTypeName;
		std::uint16_t ThisFieldIndex = 0;
		if (TryResolveVariableTypeName(RootName, BoundTypeName) || ResolveThisField(RootName, ThisFieldIndex))
		{
			return false;
		}
		OutTypeName = CanonicalTypeNameForCoercion(
		    QualifyTypeName(SubstituteTypeName(OutTypeName), CurrentModulePath));
		// A generic marker in a prefix is not enough: Cache<int>.value is a
		// value receiver. Both parsers require the entire application/identity,
		// including length-encoded nested arguments, with no member suffix.
		if (FindGenericTypeMarker(OutTypeName) != std::string::npos)
		{
			return !DecodeGenericConcreteTypes(OutTypeName).empty();
		}
		std::string GenericBase;
		std::vector<std::string> GenericArguments;
		return SplitUserGenericTypeApp(OutTypeName, GenericBase, GenericArguments) ||
		    IsTypeNameWithoutValueBinding(OutTypeName);
	}

	bool VeroScriptCompiler::TryResolveStaticField(
	    const FExpr& ObjectExpr,
	    const std::string& MemberName,
	    std::string& OutGlobalName,
	    std::string& OutTypeName,
	    bool& OutIsConst,
	    std::string& OutAccessOwnerTypeName,
	    bool& OutIsTypeNameReceiver) const
	{
		OutAccessOwnerTypeName.clear();
		OutIsTypeNameReceiver = false;
		if (!ActiveProgram)
		{
			return false;
		}
		// Prefer registered global metadata for exact type / const parity.
		const auto ApplyGlobalOverlays = [&]()
		{
			const auto TypeIt = GlobalVarTypes.find(OutGlobalName);
			if (TypeIt != GlobalVarTypes.end() && !TypeIt->second.empty())
			{
				OutTypeName = TypeIt->second;
			}
			if (GlobalVarConsts.find(OutGlobalName) != GlobalVarConsts.end())
			{
				OutIsConst = true;
			}
		};
		// Type-name path: `Type.Field` / `Ns.Type.Field` where the root is not a
		// value binding. No object is evaluated. A qualified type name is never an
		// instance, so on a miss we do not fall through to the instance path.
		std::string TypeName;
		if (TryResolveTypeNameReceiver(ObjectExpr, TypeName))
		{
			const bool bLooksLikeApp = TypeName.find('<') != std::string::npos || FindGenericTypeMarker(TypeName) != std::string::npos;
			std::string Linkage = ResolveTypeLinkageName(TypeName, CurrentModulePath);
			if (Linkage.empty() || bLooksLikeApp)
			{
				// Prefer the mangled instantiation linkage for type applications.
				const std::string Qualified = QualifyTypeName(TypeName, CurrentModulePath);
				if (!Qualified.empty() && (FindGenericTypeMarker(Qualified) != std::string::npos || Qualified.find('\x1F') != std::string::npos))
				{
					Linkage = Qualified;
				}
			}
			if (!Linkage.empty() && FindStaticFieldOnTypeLinkage(Linkage, MemberName, OutGlobalName, OutTypeName, OutIsConst))
			{
				OutIsTypeNameReceiver = true;
				OutAccessOwnerTypeName = TypeName;
				ApplyGlobalOverlays();
				return true;
			}
			return false;
		}
		// Instance path (C++-style `instance.staticField`): resolve the object's static
		// type and search it (and its bases / interfaces) for the static field.
		std::string OwnerType;
		if (!TryResolveExprTypeName(ObjectExpr, OwnerType) || OwnerType.empty())
		{
			return false;
		}
		std::string OwnerLinkage = ResolveTypeLinkageName(OwnerType, CurrentModulePath);
		if (OwnerLinkage.empty() || OwnerType.find('<') != std::string::npos || FindGenericTypeMarker(OwnerType) != std::string::npos)
		{
			// Generic instance (`Box<int> a`): resolve to the mangled instantiation.
			const std::string Qualified = QualifyTypeName(OwnerType, CurrentModulePath);
			if (!Qualified.empty() && (FindGenericTypeMarker(Qualified) != std::string::npos || Qualified.find('\x1F') != std::string::npos))
			{
				OwnerLinkage = Qualified;
			}
		}
		if (OwnerLinkage.empty() || !FindStaticFieldOnTypeLinkage(OwnerLinkage, MemberName, OutGlobalName, OutTypeName, OutIsConst))
		{
			return false;
		}
		OutIsTypeNameReceiver = false;
		OutAccessOwnerTypeName = OwnerType;
		ApplyGlobalOverlays();
		return true;
	}

	const FNativeFunction* VeroScriptCompiler::TryResolveHostStaticConstant(
	    const FExpr& ObjectExpr,
	    const std::string& MemberName,
	    std::uint16_t& OutNativeIndex) const
	{
		OutNativeIndex = static_cast<std::uint16_t>(-1);
		std::string TypeName;
		if (!ActiveBindings || !TryResolveTypeNameReceiver(ObjectExpr, TypeName))
		{
			return nullptr;
		}
		const std::string DispatchType = ResolveHostDispatchTypeName(TypeName);
		const std::string& NativeOwner = DispatchType.empty() ? TypeName : DispatchType;
		const FNativeFunction* const Native = FindNative(
		    MakeHostStaticConstantNativeName(NativeOwner, MemberName),
		    OutNativeIndex);
		if (!Native || !Native->bHostStaticMethod || !Native->ParamTypes.empty())
		{
			OutNativeIndex = static_cast<std::uint16_t>(-1);
			return nullptr;
		}
		return Native;
	}

	bool VeroScriptCompiler::TryResolveNamespaceQualifiedGlobal(
	    const FExpr& ObjectExpr,
	    const std::string& MemberName,
	    std::string& OutGlobalLinkageName) const
	{
		OutGlobalLinkageName.clear();
		std::string QualName;
		if (!TryBuildNamespaceQualifiedName(ObjectExpr, MemberName, QualName))
		{
			return false;
		}
		const std::string GlobalLinkageName = ResolveGlobalLinkageName(QualName, CurrentModulePath);
		std::uint16_t GlobalSlot = 0;
		if (GlobalLinkageName.empty() || !ResolveGlobal(GlobalLinkageName, GlobalSlot))
		{
			return false;
		}
		OutGlobalLinkageName = GlobalLinkageName;
		return true;
	}

	bool VeroScriptCompiler::TryBuildQualifiedExpressionName(
	    const FExpr& ObjectExpr,
	    std::string& OutQualifiedName) const
	{
		OutQualifiedName.clear();
		const auto AppendPath =
		    [](auto&& Self, const FExpr& Expression, std::string& OutPath) -> bool
		{
			if (Expression.Kind == EExprKind::Identifier)
			{
				OutPath =
				    static_cast<const FIdentifierExpr&>(Expression).Name;
				return !OutPath.empty();
			}
			if (Expression.Kind != EExprKind::MemberAccess)
			{
				return false;
			}
			const FMemberAccessExpr& Access =
			    static_cast<const FMemberAccessExpr&>(Expression);
			if (!Access.Object || !Self(Self, *Access.Object, OutPath))
			{
				return false;
			}
			OutPath += ".";
			OutPath += Access.Member;
			return true;
		};
		return AppendPath(AppendPath, ObjectExpr, OutQualifiedName);
	}

	bool VeroScriptCompiler::TryBuildNamespaceQualifiedName(
	    const FExpr& ObjectExpr,
	    const std::string& MemberName,
	    std::string& OutQualifiedName) const
	{
		OutQualifiedName.clear();
		std::string QualName;
		if (!TryBuildQualifiedExpressionName(ObjectExpr, QualName))
		{
			return false;
		}
		const std::size_t FirstSeparator = QualName.find('.');
		const std::string Prefix = QualName.substr(0, FirstSeparator);
		const auto PrefixBinding = ActiveTypeSubst.find(Prefix);
		const bool bTypeParameter = PrefixBinding != ActiveTypeSubst.end() && PrefixBinding->second.rfind("@value:", 0) != 0;
		std::string BoundTypeName;
		std::uint16_t ThisFieldIndex = 0;
		const bool bValueBinding = TryResolveVariableTypeName(Prefix, BoundTypeName) || ResolveThisField(Prefix, ThisFieldIndex);
		if (bValueBinding || bTypeParameter || IsEnumTypeName(Prefix) || IsTypeNameWithoutValueBinding(Prefix) || IsEnumTypeName(QualName) || IsTypeNameWithoutValueBinding(QualName))
		{
			return false;
		}
		// Inspect the receiver from its root. Asking for the complete expression's
		// type here recursively re-enters namespace classification at every member.
		std::vector<const FMemberAccessExpr*> Members;
		const FExpr* Root = &ObjectExpr;
		while (Root->Kind == EExprKind::MemberAccess)
		{
			const auto& Access = static_cast<const FMemberAccessExpr&>(*Root);
			if (!Access.Object || Access.bExplicitTemplateArguments) return false;
			Members.push_back(&Access);
			Root = Access.Object.get();
		}
		std::string ReceiverType;
		if (Root->Kind != EExprKind::Identifier || IsSuperReceiver(*Root) || TryResolveExprTypeName(*Root, ReceiverType))
		{
			return false;
		}
		std::string ReceiverName = static_cast<const FIdentifierExpr&>(*Root).Name;
		for (auto Member = Members.rbegin(); Member != Members.rend(); ++Member)
		{
			CheckAnalysisCancellation();
			const auto& Access = **Member;
			FIdentifierExpr Parent(ReceiverName);
			Parent.Location = Access.Object->Location;
			std::int64_t EnumValue = 0;
			std::string EnumDisplayName;
			std::string StaticGlobalName;
			std::string StaticTypeName;
			std::string StaticOwner;
			bool bStaticConst = false;
			bool bStaticTypeReceiver = false;
			std::uint16_t NativeIndex = static_cast<std::uint16_t>(-1);
			if (TryResolveEnumQualifiedConstant(Parent, Access.Member, EnumValue, EnumDisplayName) || TryResolveStaticField(Parent, Access.Member, StaticGlobalName, StaticTypeName, bStaticConst, StaticOwner, bStaticTypeReceiver) || TryResolveHostStaticConstant(Parent, Access.Member, NativeIndex))
			{
				return false;
			}
			ReceiverName += ".";
			ReceiverName += Access.Member;
			const auto GlobalName = ResolveGlobalLinkageName(ReceiverName, CurrentModulePath);
			std::uint16_t GlobalSlot = 0;
			if (!GlobalName.empty() && ResolveGlobal(GlobalName, GlobalSlot)) return false;
		}
		QualName += ".";
		QualName += MemberName;
		OutQualifiedName = std::move(QualName);
		return true;
	}

	bool VeroScriptCompiler::FindStaticFieldOnTypeLinkage(
	    const std::string& TypeLinkageName,
	    const std::string& FieldName,
	    std::string& OutGlobalName,
	    std::string& OutTypeName,
	    bool& OutIsConst,
	    const int Depth) const
	{
		if (!ActiveProgram || Depth > 64 || TypeLinkageName.empty())
		{
			return false;
		}
		// Generic instantiations share one open template (matched by base linkage +
		// type-param arity) but keep per-instantiation static storage keyed by the
		// full mangled `TypeLinkageName`. Field types must substitute against the
		// *looked-up* linkage's concrete args (`LocalSubst`), never the ambient
		// `ActiveTypeSubst` of whatever body is compiling — otherwise
		// `Box<string>.Value` inside a `Box<int>` method wrongly yields `int`.
		const std::string TemplateBase = GenericTemplateBaseLinkage(TypeLinkageName);
		const std::string MatchKey = TemplateBase.empty() ? TypeLinkageName : TemplateBase;
		const std::size_t WantedArity = TypeParamArityFromLinkage(TypeLinkageName);
		const std::vector<std::string> ConcreteTypes =
		    WantedArity > 0 ? DecodeGenericConcreteTypes(TypeLinkageName)
		                    : std::vector<std::string>{};
		const auto BuildLocalSubst = [&](const std::vector<std::string>& TypeParams)
		    -> std::unordered_map<std::string, std::string>
		{
			std::unordered_map<std::string, std::string> LocalSubst;
			if (WantedArity > 0 && TypeParams.size() == WantedArity)
			{
				for (std::size_t Index = 0; Index < WantedArity && Index < ConcreteTypes.size(); ++Index)
				{
					LocalSubst[TypeParams[Index]] = ConcreteTypes[Index];
				}
			}
			return LocalSubst;
		};
		const auto TryFields = [&](
		                           const std::vector<FFieldDecl>& Fields,
		                           const std::string& ModulePath,
		                           const std::unordered_map<std::string, std::string>& LocalSubst) -> bool
		{
			for (const FFieldDecl& Field : Fields)
			{
				if (Field.Name != FieldName || !Field.bIsStatic)
				{
					continue;
				}
				OutGlobalName = MakeStaticFieldGlobalName(TypeLinkageName, FieldName);
				const std::string PreviousModulePath = CurrentModulePath;
				CurrentModulePath = ModulePath;
				std::string EffectiveType = Field.TypeName;
				if (!LocalSubst.empty())
				{
					EffectiveType = SubstituteTypeNameWith(EffectiveType, LocalSubst);
				}
				else if (!ActiveTypeSubst.empty())
				{
					// Non-generic owners: ambient subst is harmless for concrete
					// field types; keeps open-template edge cases consistent.
					EffectiveType = SubstituteTypeName(EffectiveType);
				}
				OutTypeName = NormalizeTypeAlias(QualifyTypeName(EffectiveType, ModulePath));
				CurrentModulePath = PreviousModulePath;
				OutIsConst = Field.bIsConst;
				return true;
			}
			return false;
		};
		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceStructIndices, MatchKey))
		{
			const auto& StructDecl = ActiveProgram->Structs[DeclarationIndex];
			if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) != MatchKey || TemplateParameterNames(StructDecl).size() != WantedArity)
			{
				continue;
			}
			if (TryFields(StructDecl.Fields, StructDecl.ModulePath, BuildLocalSubst(TemplateParameterNames(StructDecl))))
			{
				return true;
			}
			break;
		}
		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceClassIndices, MatchKey))
		{
			const auto& ClassDecl = ActiveProgram->Classes[DeclarationIndex];
			if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) != MatchKey || TemplateParameterNames(ClassDecl).size() != WantedArity)
			{
				continue;
			}
			if (TryFields(ClassDecl.Fields, ClassDecl.ModulePath, BuildLocalSubst(TemplateParameterNames(ClassDecl))))
			{
				return true;
			}
			break;
		}
		if (const FInterfaceDecl* const InterfaceDecl =
		        FindInterfaceDeclByLinkage(TypeLinkageName))
		{
			const std::unordered_map<std::string, std::string> LocalSubst =
			    BuildLocalSubst(TemplateParameterNames(*InterfaceDecl));
			if (TryFields(
			        InterfaceDecl->Fields,
			        InterfaceDecl->ModulePath,
			        LocalSubst))
			{
				return true;
			}
			for (const std::string& ParentName : InterfaceDecl->BaseInterfaceNames)
			{
				// Substitute this interface's concrete type args into the parent
				// reference (`IBase<T>` -> `IBase<int>`) before resolving linkage,
				// mirroring GetAstBaseTypeNames, so the recursed lookup targets the
				// correct per-instantiation static storage.
				const std::string ParentAsWritten = LocalSubst.empty()
				    ? ParentName
				    : SubstituteTypeNameWith(ParentName, LocalSubst);
				const std::string ParentLinkage = ResolveInheritanceBaseLinkage(
				    ParentAsWritten,
				    InterfaceDecl->ModulePath);
				if (!ParentLinkage.empty() && FindStaticFieldOnTypeLinkage(ParentLinkage, FieldName, OutGlobalName, OutTypeName, OutIsConst, Depth + 1))
				{
					return true;
				}
			}
		}
		for (const std::string& BaseLinkage : GetAstBaseTypeNames(TypeLinkageName))
		{
			if (FindStaticFieldOnTypeLinkage(
			        BaseLinkage,
			        FieldName,
			        OutGlobalName,
			        OutTypeName,
			        OutIsConst,
			        Depth + 1))
			{
				return true;
			}
		}
		for (const std::string& IfaceLinkage : GetAstImplementedInterfaceLinkages(TypeLinkageName))
		{
			if (FindStaticFieldOnTypeLinkage(
			        IfaceLinkage,
			        FieldName,
			        OutGlobalName,
			        OutTypeName,
			        OutIsConst,
			        Depth + 1))
			{
				return true;
			}
		}
		return false;
	}

	bool VeroScriptCompiler::RegisterStaticFieldGlobals()
	{
		if (!ActiveProgram)
		{
			return true;
		}
		// Open templates (TypeParams non-empty) register per-instantiation later.
		for (const FStructDecl& StructDecl : ActiveProgram->Structs)
		{
			if (ConstantBranchState && !ConstantBranchState->Includes(StructDecl.Scope.ScopeId))
			{
				continue;
			}
			if (TemplateParameterNames(StructDecl).empty() && !RegisterStaticFieldsForTypeLinkage(GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name), StructDecl.ModulePath, StructDecl.Fields))
			{
				return false;
			}
		}
		for (const FClassDecl& ClassDecl : ActiveProgram->Classes)
		{
			if (ConstantBranchState && !ConstantBranchState->Includes(ClassDecl.Scope.ScopeId))
			{
				continue;
			}
			if (TemplateParameterNames(ClassDecl).empty() && !RegisterStaticFieldsForTypeLinkage(GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name), ClassDecl.ModulePath, ClassDecl.Fields))
			{
				return false;
			}
		}
		for (const FInterfaceDecl& InterfaceDecl : ActiveProgram->Interfaces)
		{
			if (ConstantBranchState && !ConstantBranchState->Includes(InterfaceDecl.Scope.ScopeId))
			{
				continue;
			}
			if (TemplateParameterNames(InterfaceDecl).empty() && !RegisterStaticFieldsForTypeLinkage(GetSymbolLinkageName(InterfaceDecl.ModulePath, InterfaceDecl.bExported, InterfaceDecl.Name), InterfaceDecl.ModulePath, InterfaceDecl.Fields))
			{
				return false;
			}
		}
		return true;
	}

	bool VeroScriptCompiler::CompileStaticFieldInitializers()
	{
		if (!ActiveProgram)
		{
			return true;
		}
		bStaticFieldInitializationStarted = true;
		const auto Discarded = [&](const auto& Declaration)
		{
			if (!ConstantBranchState || !ConstantBranchState->Guarded(Declaration.Scope.ScopeId)) return false;
			const auto Name = GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, Declaration.Name);
			const auto Matches = [&](const auto& Type)
			{
				return Type.Name == Name;
			};
			return std::none_of(Module->Types.begin(), Module->Types.end(), Matches) &&
			    std::none_of(Module->Interfaces.begin(), Module->Interfaces.end(), Matches);
		};
		// Open templates (TypeParams non-empty) init per-instantiation later.
		for (const FStructDecl& StructDecl : ActiveProgram->Structs)
		{
			if (Discarded(StructDecl))
			{
				continue;
			}
			if (!StructDecl.bPrecompiledPackage && TemplateParameterNames(StructDecl).empty() && !CompileStaticFieldInitializersForTypeLinkage(GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name), StructDecl.ModulePath, StructDecl.Fields))
			{
				return false;
			}
		}
		for (const FClassDecl& ClassDecl : ActiveProgram->Classes)
		{
			if (Discarded(ClassDecl))
			{
				continue;
			}
			if (!ClassDecl.bPrecompiledPackage && TemplateParameterNames(ClassDecl).empty() && !CompileStaticFieldInitializersForTypeLinkage(GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name), ClassDecl.ModulePath, ClassDecl.Fields))
			{
				return false;
			}
		}
		for (const FInterfaceDecl& InterfaceDecl : ActiveProgram->Interfaces)
		{
			if (Discarded(InterfaceDecl))
			{
				continue;
			}
			if (!InterfaceDecl.bPrecompiledPackage && TemplateParameterNames(InterfaceDecl).empty() && !CompileStaticFieldInitializersForTypeLinkage(GetSymbolLinkageName(InterfaceDecl.ModulePath, InterfaceDecl.bExported, InterfaceDecl.Name), InterfaceDecl.ModulePath, InterfaceDecl.Fields))
			{
				return false;
			}
		}
		return FlushPendingGenericStaticFieldInitializers();
	}

	bool VeroScriptCompiler::RegisterStaticFieldsForTypeLinkage(
	    const std::string& TypeLinkage,
	    const std::string& ModulePath,
	    const std::vector<FFieldDecl>& Fields)
	{
		if (!Module)
		{
			return true;
		}
		for (const FFieldDecl& Field : Fields)
		{
			if (!Field.bIsStatic)
			{
				continue;
			}
			const std::string GlobalName = MakeStaticFieldGlobalName(TypeLinkage, Field.Name);
			// Idempotent: an instantiation is only ensured once, but guard anyway.
			if (Module->GlobalSlots.find(GlobalName) != Module->GlobalSlots.end())
			{
				continue;
			}
			const std::uint16_t Slot = AddGlobal(GlobalName);
			const std::string PreviousModulePath = CurrentModulePath;
			CurrentModulePath = ModulePath;
			// `ActiveTypeSubst` (live during a generic instantiation) maps `static T`
			// field types to the concrete instantiation type.
			const std::string StoredTypeName =
			    NormalizeTypeAlias(QualifyTypeName(SubstituteTypeName(Field.TypeName), ModulePath));
			CurrentModulePath = PreviousModulePath;
			GlobalVarTypes.emplace(GlobalName, StoredTypeName);
			if (Slot < Module->GlobalTypeNames.size())
			{
				Module->GlobalTypeNames[Slot] = StoredTypeName;
			}
			if (Slot < Module->GlobalMetadata.size())
			{
				FBytecodeGlobalMeta& Meta = Module->GlobalMetadata[Slot];
				Meta.ModulePath = ModulePath;
				Meta.DeclaredTypeName = QualifyTypeName(
				    SubstituteTypeName(Field.TypeName), ModulePath);
				Meta.bConst = Field.bIsConst;
				Meta.bConstexpr = Field.bConstexpr;
				Meta.bExported = false;
				Meta.bStaticMember = true;
				Meta.DeclMeta = Field.DeclMeta;
			}
			if (Field.bIsConst)
			{
				GlobalVarConsts.insert(GlobalName);
			}
		}
		return true;
	}

	bool VeroScriptCompiler::CompileStaticFieldInitializersForTypeLinkage(
	    const std::string& TypeLinkage,
	    const std::string& ModulePath,
	    const std::vector<FFieldDecl>& Fields)
	{
		if (!ActiveProgram || !Module)
		{
			return true;
		}
		bool bHasStatic = false;
		for (const FFieldDecl& Field : Fields)
		{
			if (Field.bIsStatic)
			{
				bHasStatic = true;
				break;
			}
		}
		if (!bHasStatic)
		{
			return true;
		}
		// Initializers land in Module->InitCode and run at module startup. This
		// helper may be called mid-body (first generic instantiation), so save and
		// neutralize the active function/scope state; `MakeEmitter` then targets
		// InitCode. `ActiveTypeSubst` stays live so `static T` initializers work.
		const std::string InitializerOwner = TypeLinkage;
		struct FStaticOwnerScope
		{
			~FStaticOwnerScope()
			{
				Owner = std::move(PreviousOwner);
				Access = std::move(PreviousAccess);
			}
			std::string& Owner;
			std::string& Access;
			std::string PreviousOwner, PreviousAccess;
		} OwnerScope{CurrentOwnerTypeLinkage, CurrentAccessFunctionLinkage,
		    std::move(CurrentOwnerTypeLinkage), std::move(CurrentAccessFunctionLinkage)};
		CurrentOwnerTypeLinkage = InitializerOwner;
		CurrentAccessFunctionLinkage.clear();
		FBytecodeFunction* const SavedFunction = CurrentFunction;
		std::vector<FScope> SavedScopes = std::move(Scopes);
		std::vector<FLoopContext> SavedLoopStack = std::move(LoopStack);
		std::unordered_map<std::string, FLabelInfo> SavedLabelTable = std::move(LabelTable);
		std::vector<FGotoPatch> SavedGotoPatches = std::move(GotoPatches);
		std::vector<FLocalInitMarker> SavedLocalInitMarkers = std::move(LocalInitMarkers);
		const std::uint16_t SavedNextLocalSlot = NextLocalSlot;
		const std::string SavedModulePath = CurrentModulePath;
		const std::string SavedNamespacePrefix = CurrentNamespacePrefix;
		const std::string SavedReturnType = CurrentFunctionReturnType;
		const FSourceLocation SavedEmitLocation = CurrentEmitLocation;
		CurrentFunction = nullptr;
		Scopes.clear();
		LoopStack.clear();
		LabelTable.clear();
		GotoPatches.clear();
		LocalInitMarkers.clear();
		NextLocalSlot = 0;
		CurrentFunctionReturnType.clear();

		// Emit initializers into the module init code (like top-level globals) reusing the
		// VarDecl statement path. The synthetic statement borrows the AST default-value
		// expression (never mutated / freed by the compiler) or supplies a zero literal.
		const auto InitOne = [&](
		                         const std::string& TypeLinkage,
		                         const std::string& ModulePath,
		                         const FFieldDecl& Field) -> bool
		{
			const std::string GlobalName = MakeStaticFieldGlobalName(TypeLinkage, Field.Name);
			CurrentModulePath = ModulePath;
			CurrentNamespacePrefix = NamespaceFromFunctionLinkage(
			    TypeLinkage + "::@static#");

			std::unique_ptr<FExpr> SyntheticDefault;
			FExpr* BorrowedInitializer = nullptr;
			if (Field.DefaultValue)
			{
				BorrowedInitializer = const_cast<FExpr*>(Field.DefaultValue.get());
			}
			else
			{
				// No initializer: containers self-construct; primitives get a zero literal;
				// struct/class/other require an explicit default (VarDecl "must be initialized").
				const std::string Effective =
				    NormalizeTypeAlias(SubstituteTypeName(Field.TypeName));
				if (Effective == "bool")
				{
					SyntheticDefault = std::make_unique<FBooleanLiteralExpr>(false);
				}
				else if (Effective == "float" || Effective == "double")
				{
					SyntheticDefault = std::make_unique<FFloatLiteralExpr>(
					    0.0, Effective == "float");
				}
				else if (Effective == "string")
				{
					SyntheticDefault = std::make_unique<FStringLiteralExpr>(std::string{});
				}
				else if (Effective == "wstring")
				{
					SyntheticDefault = std::make_unique<FWStringLiteralExpr>(std::u16string{});
				}
				else if (Runtime::IsUnsignedTypeName(Effective))
				{
					SyntheticDefault = std::make_unique<FIntegerLiteralExpr>(
					    static_cast<std::uint32_t>(0), true);
				}
				else if (IsNumericTypeName(Effective) || IsEnumTypeName(Effective) || Effective == "int" || Effective == "char")
				{
					SyntheticDefault = std::make_unique<FIntegerLiteralExpr>(0);
				}
				if (SyntheticDefault)
				{
					SyntheticDefault->Location = Field.Location;
				}
			}

			FVarDeclStmt Synth(Field.TypeName, GlobalName, nullptr);
			Synth.ModulePath.clear();
			Synth.bExported = false;
			Synth.bIsConst = Field.bIsConst;
			Synth.bConstexpr = Field.bConstexpr;
			if (Field.bConstexpr && !Field.DefaultValue)
			{
				ReportError("constexpr field requires an initializer", Field.Location);
				return false;
			}
			Synth.Location = Field.Location;
			if (BorrowedInitializer)
			{
				Synth.Initializer.reset(BorrowedInitializer);
			}
			else
			{
				Synth.Initializer = std::move(SyntheticDefault);
			}

			struct FDetachBorrowedInitializer
			{
				FVarDeclStmt& Statement;
				bool bBorrowed;
				~FDetachBorrowedInitializer()
				{
					if (bBorrowed)
					{
						(void)Statement.Initializer.release();
					}
				}
			} Detach{Synth, BorrowedInitializer != nullptr};
			if (BorrowedInitializer && !PendingGenericStaticFieldInits.empty())
			{
				// Emit queued type dependencies before this expression, including both
				// branches of short-circuit/conditional expressions. Their initialization
				// must not become conditional when the expression is lowered.
				std::unordered_set<const FExpr*> DeferredBodies;
				std::unordered_set<std::string> PreparedTypes;
				bool bPrepared = true;
				std::function<void(const std::string&)> PrepareType = [&](const std::string& Name)
				{
					if (!bPrepared || Name.empty()) return;
					const auto Owner = ResolveTypeLinkageName(CanonicalTypeNameForCoercion(
					                                              QualifyTypeName(SubstituteTypeName(Name), ModulePath)),
					    ModulePath);
					if (!PreparedTypes.insert(Owner).second ||
					    (FindRegisteredTypeIndex(Owner) == static_cast<std::uint16_t>(-1) && !IsInterfaceTypeName(Owner)))
					{
						return;
					}
					for (const auto& Base : GetAstBaseTypeNames(Owner))
					{
						PrepareType(Base);
					}
					for (const auto& Base : GetAstImplementedInterfaceLinkages(Owner))
					{
						PrepareType(Base);
					}
					if (bPrepared && std::find(PendingGenericStaticFieldInits.begin(), PendingGenericStaticFieldInits.end(), Owner) != PendingGenericStaticFieldInits.end())
					{
						bPrepared = FlushPendingGenericStaticFieldInitializers(Owner);
					}
				};
				VisitExpressionNodes(static_cast<const FExpr&>(*BorrowedInitializer), [&](const FExpr& Expression)
				{
					CheckAnalysisCancellation();
					if (!bPrepared || DeferredBodies.count(&Expression)) return;
					if (Expression.Kind == EExprKind::Lambda)
					{
						const auto& Lambda = static_cast<const FLambdaExpr&>(Expression);
						const auto Ignore = [&](const FExpr& Child)
						{
							DeferredBodies.insert(&Child);
						};
						if (Lambda.Body)
						{
							VisitBlockNodes(*Lambda.Body, Ignore, [](const FStmt&)
							{
							});
						}
						if (Lambda.ExpressionBody)
						{
							VisitExpressionNodes(static_cast<const FExpr&>(*Lambda.ExpressionBody), Ignore);
						}
						return;
					}
					if (Expression.Kind == EExprKind::MethodCall)
					{
						const auto& Call = static_cast<const FMethodCallExpr&>(Expression);
						std::string ReceiverType;
						// A static method can be reached through a field or another value receiver.
						if (Call.Object && TryResolveExprTypeName(*Call.Object, ReceiverType))
						{
							PrepareType(ReceiverType);
						}
					}
					std::string Name;
					if (TryBuildQualifiedExpressionName(Expression, Name))
					{
						std::string BoundType;
						std::uint16_t FieldIndex = 0;
						const auto Root = Name.substr(0, Name.find('.'));
						if (!TryResolveVariableTypeName(Root, BoundType) && !ResolveThisField(Root, FieldIndex))
						{
							PrepareType(Name);
						}
						else
						{
							PrepareType(BoundType);
						}
					}
					else if (Expression.Kind == EExprKind::Call)
					{
						const auto& Call = static_cast<const FCallExpr&>(Expression);
						Name = Call.Callee;
						if (!Call.TypeArgs.empty())
						{
							Name += '<';
							for (std::size_t I = 0; I < Call.TypeArgs.size(); ++I)
							{
								if (I)
								{
									Name += ',';
								}
								Name += Call.TypeArgs[I];
							}
							Name += '>';
						}
						PrepareType(Name);
					}
				});
				if (!bPrepared) return false;
			}
			const std::size_t ExpressionBegin = Module->InitCode.size();
			bool bCompiled = false;
			{
				// Lower the selected constructor/call first, without emitting queued
				// globals into a branch or a field-owned scratch expression.
				struct FStaticEmissionScope
				{
					~FStaticEmissionScope()
					{
						Flag = bPrevious;
					}

					bool& Flag;
					bool bPrevious;
				} Restore{bStaticFieldInitializationStarted, bStaticFieldInitializationStarted};
				bStaticFieldInitializationStarted = false;
				bCompiled = CompileStatement(Synth);
			}
			if (!bCompiled)
			{
				return false;
			}
			Module->InitLocalCount = std::max(Module->InitLocalCount, NextLocalSlot);
			std::vector<std::uint8_t> ExpressionCode(Module->InitCode.begin() + ExpressionBegin,
			    Module->InitCode.end());
			const auto DebugBegin = std::min(ExpressionBegin, Module->InitDebugLines.size());
			std::vector<FDebugLine> ExpressionLines(Module->InitDebugLines.begin() + DebugBegin,
			    Module->InitDebugLines.end());
			Module->InitCode.resize(ExpressionBegin);
			Module->InitDebugLines.resize(DebugBegin);
			const auto ExpressionLocation = CurrentEmitLocation;
			if (!PendingGenericStaticFieldInits.empty() &&
			    !CompilerDetail::PrepareStaticInitializationDependencies(*Module, ExpressionCode,
			        [&](const std::string& Owner)
			{
				return std::find(PendingGenericStaticFieldInits.begin(), PendingGenericStaticFieldInits.end(), Owner) == PendingGenericStaticFieldInits.end() || FlushPendingGenericStaticFieldInitializers(Owner);
			}, [this]()
			{
				CheckAnalysisCancellation();
			}))
			{
				return false;
			}
			CurrentEmitLocation = ExpressionLocation;
			const std::size_t InitBeginOffset = Module->InitCode.size();
			MakeEmitter(Module->InitCode, &Module->InitDebugLines).AppendCode(ExpressionCode, &ExpressionLines);
			const auto Slot = Module->GlobalSlots.find(GlobalName);
			if (Slot != Module->GlobalSlots.end() && Module->InitCode.size() > InitBeginOffset)
			{
				Module->GlobalInitRanges.push_back(
				    {Slot->second, InitBeginOffset, Module->InitCode.size(), TypeParamArityFromLinkage(TypeLinkage) != 0});
			}
			return true;
		};

		bool bOk = true;
		for (const FFieldDecl& Field : Fields)
		{
			if (!Field.bIsStatic)
			{
				continue;
			}
			const std::string GlobalName = MakeStaticFieldGlobalName(TypeLinkage, Field.Name);
			// Idempotent: only emit an initializer once per backing global.
			if (!InitializedStaticFieldGlobals.insert(GlobalName).second)
			{
				continue;
			}
			if (!InitOne(TypeLinkage, ModulePath, Field))
			{
				if (!RecoverAfterCompileUnitError())
				{
					bOk = false;
					break;
				}
			}
		}

		CurrentFunction = SavedFunction;
		Scopes = std::move(SavedScopes);
		LoopStack = std::move(SavedLoopStack);
		LabelTable = std::move(SavedLabelTable);
		GotoPatches = std::move(SavedGotoPatches);
		LocalInitMarkers = std::move(SavedLocalInitMarkers);
		NextLocalSlot = SavedNextLocalSlot;
		CurrentModulePath = SavedModulePath;
		CurrentNamespacePrefix = SavedNamespacePrefix;
		CurrentFunctionReturnType = SavedReturnType;
		CurrentEmitLocation = SavedEmitLocation;
		return bOk;
	}

	bool VeroScriptCompiler::QueueOrCompileStaticFieldInitializersForTypeLinkage(
	    const std::string& TypeLinkage,
	    const std::string& ModulePath,
	    const std::vector<FFieldDecl>& Fields)
	{
		bool bHasStatic = false;
		for (const FFieldDecl& Field : Fields)
		{
			if (Field.bIsStatic)
			{
				bHasStatic = true;
				break;
			}
		}
		if (!bHasStatic)
		{
			return true;
		}
		// Type layout can also instantiate generics outside a function. Defer until
		// static initialization starts, and while compiling bodies.
		if (!bStaticFieldInitializationStarted || CurrentFunction != nullptr)
		{
			for (const std::string& Pending : PendingGenericStaticFieldInits)
			{
				if (Pending == TypeLinkage)
				{
					return true;
				}
			}
			PendingGenericStaticFieldInits.push_back(TypeLinkage);
			(void)ModulePath;
			return true;
		}
		return CompileStaticFieldInitializersForTypeLinkage(TypeLinkage, ModulePath, Fields);
	}

	bool VeroScriptCompiler::FlushPendingGenericStaticFieldInitializers(const std::string& RequiredType)
	{
		if (!ActiveProgram || !Module)
		{
			PendingGenericStaticFieldInits.clear();
			return true;
		}
		// Nested Ensure during init may queue more entries — drain until empty.
		while (!PendingGenericStaticFieldInits.empty())
		{
			// Keep unvisited owners visible to dependency reads in another initializer.
			const std::vector<std::string> Batch = RequiredType.empty()
			    ? PendingGenericStaticFieldInits
			    : std::vector<std::string>{RequiredType};
			for (const std::string& TypeLinkage : Batch)
			{
				const auto Pending = std::find(PendingGenericStaticFieldInits.begin(), PendingGenericStaticFieldInits.end(), TypeLinkage);
				if (Pending == PendingGenericStaticFieldInits.end())
				{
					continue;
				}
				PendingGenericStaticFieldInits.erase(Pending);
				const std::string TemplateBase = GenericTemplateBaseLinkage(TypeLinkage);
				const std::string MatchKey = TemplateBase.empty() ? TypeLinkage : TemplateBase;
				const std::size_t WantedArity = TypeParamArityFromLinkage(TypeLinkage);
				const std::vector<std::string> ConcreteTypes =
				    WantedArity > 0 ? DecodeGenericConcreteTypes(TypeLinkage)
				                    : std::vector<std::string>{};
				const std::vector<FFieldDecl>* Fields = nullptr;
				std::string ModulePath;
				const std::vector<std::string>* TypeParams = nullptr;
				for (const FStructDecl& StructDecl : ActiveProgram->Structs)
				{
					if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) == MatchKey && TemplateParameterNames(StructDecl).size() == WantedArity)
					{
						Fields = &StructDecl.Fields;
						ModulePath = StructDecl.ModulePath;
						TypeParams = &TemplateParameterNames(StructDecl);
						break;
					}
				}
				if (!Fields)
				{
					for (const FClassDecl& ClassDecl : ActiveProgram->Classes)
					{
						if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) == MatchKey && TemplateParameterNames(ClassDecl).size() == WantedArity)
						{
							Fields = &ClassDecl.Fields;
							ModulePath = ClassDecl.ModulePath;
							TypeParams = &TemplateParameterNames(ClassDecl);
							break;
						}
					}
				}
				if (!Fields)
				{
					if (const FInterfaceDecl* const InterfaceDecl =
					        FindInterfaceDeclByLinkage(TypeLinkage))
					{
						Fields = &InterfaceDecl->Fields;
						ModulePath = InterfaceDecl->ModulePath;
						TypeParams = &TemplateParameterNames(*InterfaceDecl);
					}
				}
				// Generic builtin templates (e.g. pair<K,V>, handle<T>) live outside
				// ActiveProgram; search them too so their static initializers flush.
				if (!Fields)
				{
					for (const FStructDecl& StructDecl : BuiltinStructTemplates)
					{
						if (GetSymbolLinkageName(
						        StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) == MatchKey &&
						    TemplateParameterNames(StructDecl).size() == WantedArity)
						{
							Fields = &StructDecl.Fields;
							ModulePath = StructDecl.ModulePath;
							TypeParams = &TemplateParameterNames(StructDecl);
							break;
						}
					}
				}
				if (!Fields)
				{
					for (const FClassDecl& ClassDecl : BuiltinClassTemplates)
					{
						if (GetSymbolLinkageName(
						        ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) == MatchKey &&
						    TemplateParameterNames(ClassDecl).size() == WantedArity)
						{
							Fields = &ClassDecl.Fields;
							ModulePath = ClassDecl.ModulePath;
							TypeParams = &TemplateParameterNames(ClassDecl);
							break;
						}
					}
				}
				if (!Fields || !TypeParams)
				{
					continue;
				}
				const std::unordered_map<std::string, std::string> PreviousSubst = ActiveTypeSubst;
				ActiveTypeSubst.clear();
				if (WantedArity > 0 && !ApplyTypeParamSubst(*TypeParams, ConcreteTypes, ActiveTypeSubst, FSourceLocation{}))
				{
					ActiveTypeSubst = PreviousSubst;
					return false;
				}
				const bool bOk = CompileStaticFieldInitializersForTypeLinkage(
				    TypeLinkage, ModulePath, *Fields);
				ActiveTypeSubst = PreviousSubst;
				if (!bOk)
				{
					return false;
				}
			}
			if (!RequiredType.empty()) return true;
		}
		return true;
	}

	bool VeroScriptCompiler::EnsureStaticFieldGlobalRegistered(
	    const std::string& OwnerTypeName,
	    const std::string& GlobalName,
	    const FSourceLocation& Location)
	{
		std::uint16_t Slot = 0;
		const bool bRegistered = ResolveGlobal(GlobalName, Slot);
		if (bRegistered && (!bStaticFieldInitializationStarted || CurrentFunction != nullptr || InitializedStaticFieldGlobals.count(GlobalName) != 0))
		{
			return true;
		}
		const auto Separator = GlobalName.rfind('.');
		const auto StorageOwner = Separator == std::string::npos
		    ? OwnerTypeName
		    : GlobalName.substr(0, Separator);
		const bool bGeneric = StorageOwner.find('<') != std::string::npos ||
		    FindGenericTypeMarker(StorageOwner) != std::string::npos;
		if (bRegistered)
		{
			// An ordinary/static initializer may read an already resolved generic owner.
			// Initialize only that queued dependency; unrelated owners retain their order.
			return !bGeneric || std::find(PendingGenericStaticFieldInits.begin(), PendingGenericStaticFieldInits.end(), StorageOwner) == PendingGenericStaticFieldInits.end() ||
			    FlushPendingGenericStaticFieldInitializers(StorageOwner);
		}
		// A member initializer can instantiate a generic body before Compile has
		// registered ordinary statics. Register only the referenced declaration;
		// its initialization still runs in the normal module-startup order.
		if (!bGeneric)
		{
			if (!ActiveProgram) return false;
			struct FRestoreSubstitution
			{
				std::unordered_map<std::string, std::string>& Target;
				std::unordered_map<std::string, std::string> Previous;
				~FRestoreSubstitution()
				{
					Target = std::move(Previous);
				}
			} Restore{ActiveTypeSubst, std::move(ActiveTypeSubst)};
			ActiveTypeSubst.clear();
			const auto Register = [&](const auto& Declarations, const auto& Indices)
			{
				for (const auto Index : FindDeclarationCandidates(Indices, StorageOwner))
				{
					const auto& Declaration = Declarations[Index];
					if (!TemplateParameterNames(Declaration).empty() ||
					    GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, Declaration.Name) != StorageOwner ||
					    (ConstantBranchState && !ConstantBranchState->Includes(Declaration.Scope.ScopeId)))
					{
						continue;
					}
					return RegisterStaticFieldsForTypeLinkage(StorageOwner, Declaration.ModulePath, Declaration.Fields) && ResolveGlobal(GlobalName, Slot);
				}
				return false;
			};
			return Register(ActiveProgram->Structs, SourceStructIndices) ||
			    Register(ActiveProgram->Classes, SourceClassIndices) ||
			    Register(ActiveProgram->Interfaces, SourceInterfaceIndices);
		}
		std::string Linkage;
		if (EnsureStructTypeResolved(StorageOwner, Linkage, nullptr, Location))
		{
			return ResolveGlobal(GlobalName, Slot);
		}
		std::uint16_t InterfaceIndex = 0xFFFF;
		if (EnsureInterfaceTypeResolved(StorageOwner, Linkage, &InterfaceIndex, Location))
		{
			return ResolveGlobal(GlobalName, Slot);
		}
		return false;
	}
}
