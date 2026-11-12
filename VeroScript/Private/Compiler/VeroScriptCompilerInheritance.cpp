// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptCompilerStackGuard.h"

#include "VeroScriptAst.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptEnumValueIdentity.h"
#include "VeroScriptMemberAccess.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <tuple>
#include <type_traits>
#include <unordered_set>

namespace VeroScript
{
	bool VeroScriptCompiler::FindVirtualBaseMethod(
	    const std::string& OwnerLinkageName,
	    const FMethodDecl& Method,
	    const std::string& OwnerModulePath,
	    std::string& OutBaseLinkageName,
	    const FMethodDecl*& OutBaseMethod) const
	{
		OutBaseMethod = nullptr;
		OutBaseLinkageName.clear();
		if (!ActiveProgram)
		{
			return false;
		}
		std::unordered_map<std::string, std::string> OwnerSubst;
		TryBuildGenericOwnerSubst(OwnerLinkageName, OwnerSubst, Method.Location);
		std::vector<std::string> Queue = GetAstBaseTypeNames(OwnerLinkageName);
		{
			const std::vector<std::string> Implemented =
			    GetAstImplementedInterfaceLinkages(OwnerLinkageName);
			Queue.insert(Queue.end(), Implemented.begin(), Implemented.end());
		}
		{
			const std::string OwnerTemplate = [&]() -> std::string
			{
				const std::string TemplateBase = GenericTemplateBaseLinkage(OwnerLinkageName);
				return TemplateBase.empty() ? OwnerLinkageName : TemplateBase;
			}();
			const std::size_t OwnerArity = TypeParamArityFromLinkage(OwnerLinkageName);
			for (const FInterfaceDecl& InterfaceDecl : ActiveProgram->Interfaces)
			{
				if (GetSymbolLinkageName(
				        InterfaceDecl.ModulePath,
				        InterfaceDecl.bExported,
				        InterfaceDecl.Name) != OwnerTemplate ||
				    TemplateParameterNames(InterfaceDecl).size() != OwnerArity)
				{
					continue;
				}
				for (const std::string& ParentName : InterfaceDecl.BaseInterfaceNames)
				{
					const std::string ParentLinkage =
					    ResolveInheritanceBaseLinkage(ParentName, InterfaceDecl.ModulePath);
					Queue.push_back(ParentLinkage.empty() ? ParentName : ParentLinkage);
				}
				break;
			}
		}
		std::unordered_set<std::string> Visited;
		for (std::size_t Qi = 0; Qi < Queue.size(); ++Qi)
		{
			const std::string CurrentLinkage = Queue[Qi];
			if (CurrentLinkage.empty() || !Visited.insert(CurrentLinkage).second)
			{
				continue;
			}
			const std::string LookupLinkage = [&]() -> std::string
			{
				const std::string TemplateBase = GenericTemplateBaseLinkage(CurrentLinkage);
				return TemplateBase.empty() ? CurrentLinkage : TemplateBase;
			}();
			std::unordered_map<std::string, std::string> BaseSubst;
			TryBuildGenericOwnerSubst(CurrentLinkage, BaseSubst, Method.Location);
			for (const FStructDecl& StructDecl : ActiveProgram->Structs)
			{
				if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) != LookupLinkage)
				{
					continue;
				}
				for (const FMethodDecl& Candidate : StructDecl.Methods)
				{
					if (!(Candidate.bVirtual || Candidate.bOverride) || !MethodSignaturesMatch(Method, Candidate, OwnerModulePath, StructDecl.ModulePath, OwnerSubst, BaseSubst))
					{
						continue;
					}
					OutBaseLinkageName = CurrentLinkage;
					OutBaseMethod = &Candidate;
					return true;
				}
				const std::vector<std::string> Bases = GetAstBaseTypeNames(CurrentLinkage);
				Queue.insert(Queue.end(), Bases.begin(), Bases.end());
				goto NextVirtual;
			}
			for (const FClassDecl& ClassDecl : ActiveProgram->Classes)
			{
				if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) != LookupLinkage)
				{
					continue;
				}
				for (const FMethodDecl& Candidate : ClassDecl.Methods)
				{
					if (!(Candidate.bVirtual || Candidate.bOverride) || !MethodSignaturesMatch(Method, Candidate, OwnerModulePath, ClassDecl.ModulePath, OwnerSubst, BaseSubst))
					{
						continue;
					}
					OutBaseLinkageName = CurrentLinkage;
					OutBaseMethod = &Candidate;
					return true;
				}
				const std::vector<std::string> Bases = GetAstBaseTypeNames(CurrentLinkage);
				Queue.insert(Queue.end(), Bases.begin(), Bases.end());
				goto NextVirtual;
			}
			for (const FInterfaceDecl& InterfaceDecl : ActiveProgram->Interfaces)
			{
				if (GetSymbolLinkageName(
				        InterfaceDecl.ModulePath,
				        InterfaceDecl.bExported,
				        InterfaceDecl.Name) != LookupLinkage)
				{
					continue;
				}
				for (const FMethodDecl& Candidate : InterfaceDecl.Methods)
				{
					// Only virtual/override interface slots participate in override/final
					// matching (abstract `;` methods are implemented, not overridden).
					if (!(Candidate.bVirtual || Candidate.bOverride) || !MethodSignaturesMatch(Method, Candidate, OwnerModulePath, InterfaceDecl.ModulePath, OwnerSubst, BaseSubst))
					{
						continue;
					}
					OutBaseLinkageName = CurrentLinkage;
					OutBaseMethod = &Candidate;
					return true;
				}
				for (const std::string& ParentName : InterfaceDecl.BaseInterfaceNames)
				{
					const std::string ParentLinkage =
					    ResolveInheritanceBaseLinkage(ParentName, InterfaceDecl.ModulePath);
					Queue.push_back(ParentLinkage.empty() ? ParentName : ParentLinkage);
				}
				goto NextVirtual;
			}
		NextVirtual:
			continue;
		}
		return false;
	}

	bool VeroScriptCompiler::ReportIfCannotOverrideFinalMethod(
	    const bool bFoundVirtualBase,
	    const FMethodDecl* BaseMethod,
	    const FMethodDecl& Method,
	    const FSourceLocation& Location)
	{
		if (bFoundVirtualBase && BaseMethod && BaseMethod->bFinal)
		{
			ReportErrorId("diag.cannot_override_final_method", Location, {Method.Name});
			return true;
		}
		return false;
	}

	bool VeroScriptCompiler::ReportIfDeletedMethodOverridesVirtual(
	    const std::string& OwnerLinkageName,
	    const FMethodDecl& Method,
	    const std::string& OwnerModulePath)
	{
		if (!Method.bDeleted || Method.bIsStatic)
		{
			return false;
		}
		std::string BaseLinkage;
		const FMethodDecl* BaseMethod = nullptr;
		if (!FindVirtualBaseMethod(
		        OwnerLinkageName,
		        Method,
		        OwnerModulePath,
		        BaseLinkage,
		        BaseMethod))
		{
			return false;
		}
		ReportErrorId(
		    "diag.deleted_method_cannot_override_virtual_base_method",
		    Method.Location,
		    {Method.Name});
		return true;
	}

	bool VeroScriptCompiler::IsVirtualOrOverridesVirtual(
	    const std::string& OwnerTypeName,
	    const FMethodDecl& Method) const
	{
		if (Method.bVirtual)
		{
			return true;
		}
		std::string OwnerLinkage = ResolveTypeLinkageName(OwnerTypeName, CurrentModulePath);
		if (OwnerLinkage.empty())
		{
			OwnerLinkage = OwnerTypeName;
		}
		std::string BaseLinkage;
		const FMethodDecl* BaseMethod = nullptr;
		return FindVirtualBaseMethod(
		    OwnerLinkage,
		    Method,
		    ResolveOwnerModulePath(OwnerLinkage),
		    BaseLinkage,
		    BaseMethod);
	}

	bool VeroScriptCompiler::IsSubtypeOf(
	    const std::string& SourceTypeName,
	    const std::string& TargetTypeName) const
	{
		if (!ActiveProgram || SourceTypeName.empty() || TargetTypeName.empty())
		{
			return false;
		}
		if (SourceTypeName == TargetTypeName)
		{
			return true;
		}

		// Interface upcast: interface-to-parent-interface, or class/struct that implements I
		// (including via base / inherited ImplementedInterfaceIndices).
		// Struct→interface assignment still emits CloneStruct (value-copy box) at the call site.
		if (IsInterfaceTypeName(TargetTypeName))
		{
			const auto ResolveIfaceLinkage = [this](const std::string& TypeName) -> std::string
			{
				if (FindGenericTypeMarker(TypeName) != std::string::npos)
				{
					return TypeName;
				}
				std::string GenericBase;
				std::vector<std::string> GenericArgs;
				if (SplitUserGenericTypeApp(TypeName, GenericBase, GenericArgs))
				{
					return QualifyTypeName(TypeName, CurrentModulePath);
				}
				if (TypeName.find('\x1F') != std::string::npos)
				{
					return TypeName;
				}
				const std::string Resolved = ResolveTypeLinkageName(TypeName, CurrentModulePath);
				return Resolved.empty() ? TypeName : Resolved;
			};
			const std::string TargetLinkage = ResolveIfaceLinkage(TargetTypeName);
			const std::uint16_t TargetIfaceIndex = FindInterfaceIndex(TargetLinkage);

			// IChild -> IParent (multi-parent interface graph).
			if (IsInterfaceTypeName(SourceTypeName))
			{
				const std::string SourceLinkage = ResolveIfaceLinkage(SourceTypeName);
				if (SourceLinkage == TargetLinkage)
				{
					return true;
				}
				if (Module && TargetIfaceIndex != static_cast<std::uint16_t>(-1))
				{
					const std::uint16_t SourceIfaceIndex = FindInterfaceIndex(SourceLinkage);
					if (SourceIfaceIndex != static_cast<std::uint16_t>(-1))
					{
						std::vector<std::uint16_t> IfaceQueue{SourceIfaceIndex};
						std::unordered_set<std::uint16_t> IfaceVisited;
						for (std::size_t Qi = 0; Qi < IfaceQueue.size(); ++Qi)
						{
							const std::uint16_t Walk = IfaceQueue[Qi];
							if (Walk >= Module->Interfaces.size() || !IfaceVisited.insert(Walk).second)
							{
								continue;
							}
							if (Walk == TargetIfaceIndex)
							{
								return true;
							}
							for (const std::uint16_t Parent :
							    Module->Interfaces[Walk].BaseInterfaceIndices)
							{
								IfaceQueue.push_back(Parent);
							}
						}
					}
				}
				// AST fallback (e.g. before Interfaces table is fully wired).
				std::vector<std::string> AstQueue{SourceLinkage};
				std::unordered_set<std::string> VisitedIfaces;
				for (std::size_t Qi = 0; Qi < AstQueue.size(); ++Qi)
				{
					const std::string CurrentLinkage = AstQueue[Qi];
					if (CurrentLinkage.empty() || !VisitedIfaces.insert(CurrentLinkage).second)
					{
						continue;
					}
					if (CurrentLinkage == TargetLinkage)
					{
						return true;
					}
					const FInterfaceDecl* const Decl = FindInterfaceDeclByLinkage(CurrentLinkage);
					if (!Decl)
					{
						continue;
					}
					std::unordered_map<std::string, std::string> Subst;
					BuildInterfaceTypeSubst(CurrentLinkage, Subst);
					for (const std::string& ParentName : Decl->BaseInterfaceNames)
					{
						const std::string ParentWritten =
						    SubstituteTypeNameWith(ParentName, Subst);
						const std::string Next =
						    ResolveInheritanceBaseLinkage(ParentWritten, Decl->ModulePath);
						if (!Next.empty())
						{
							AstQueue.push_back(Next);
						}
					}
				}
				return false;
			}

			if (TargetIfaceIndex == static_cast<std::uint16_t>(-1))
			{
				return false;
			}
			std::vector<std::string> Queue{SourceTypeName};
			std::unordered_set<std::string> Visited;
			for (std::size_t Qi = 0; Qi < Queue.size(); ++Qi)
			{
				const std::string Current = Queue[Qi];
				if (Current.empty() || !Visited.insert(Current).second)
				{
					continue;
				}
				if (Module)
				{
					for (const FBytecodeTypeDesc& TypeDesc : Module->Types)
					{
						if (TypeDesc.Name != Current)
						{
							continue;
						}
						for (const std::uint16_t IfaceIndex : TypeDesc.ImplementedInterfaceIndices)
						{
							std::vector<std::uint16_t> IfaceQueue{IfaceIndex};
							std::unordered_set<std::uint16_t> IfaceVisited;
							for (std::size_t Ii = 0; Ii < IfaceQueue.size(); ++Ii)
							{
								const std::uint16_t Walk = IfaceQueue[Ii];
								if (Walk >= Module->Interfaces.size() || !IfaceVisited.insert(Walk).second)
								{
									continue;
								}
								if (Walk == TargetIfaceIndex)
								{
									return true;
								}
								for (const std::uint16_t Parent :
								    Module->Interfaces[Walk].BaseInterfaceIndices)
								{
									IfaceQueue.push_back(Parent);
								}
							}
						}
						for (const std::uint16_t BaseIdx : TypeDesc.BaseTypeIndices)
						{
							if (BaseIdx < Module->Types.size())
							{
								Queue.push_back(Module->Types[BaseIdx].Name);
							}
						}
						if (!TypeDesc.HostBaseTypeName.empty())
						{
							Queue.push_back(TypeDesc.HostBaseTypeName);
						}
						goto ContinueIfaceSubtype;
					}
				}
				{
					// AST fallback: Module->Types may not yet list open-generic instances
					// (or may use a different spelling); still honor `: I` / inherited bases.
					const std::vector<std::string> AstIfaces =
					    GetAstImplementedInterfaceLinkages(Current);
					for (const std::string& IfaceLinkage : AstIfaces)
					{
						std::vector<std::string> WalkQueue{IfaceLinkage};
						std::unordered_set<std::string> IfaceVisited;
						for (std::size_t Wi = 0; Wi < WalkQueue.size(); ++Wi)
						{
							std::string Walk = WalkQueue[Wi];
							if (Walk.empty() || !IfaceVisited.insert(Walk).second)
							{
								continue;
							}
							if (Walk == TargetLinkage)
							{
								return true;
							}
							if (Module && TargetIfaceIndex != static_cast<std::uint16_t>(-1))
							{
								const std::uint16_t WalkIndex = FindInterfaceIndex(Walk);
								if (WalkIndex == TargetIfaceIndex)
								{
									return true;
								}
								if (WalkIndex != static_cast<std::uint16_t>(-1) && WalkIndex < Module->Interfaces.size())
								{
									for (const std::uint16_t Parent :
									    Module->Interfaces[WalkIndex].BaseInterfaceIndices)
									{
										if (Parent < Module->Interfaces.size())
										{
											WalkQueue.push_back(Module->Interfaces[Parent].Name);
										}
									}
									continue;
								}
							}
							const FInterfaceDecl* const Decl = FindInterfaceDeclByLinkage(Walk);
							if (!Decl)
							{
								continue;
							}
							std::unordered_map<std::string, std::string> Subst;
							BuildInterfaceTypeSubst(Walk, Subst);
							for (const std::string& ParentName : Decl->BaseInterfaceNames)
							{
								const std::string ParentWritten =
								    SubstituteTypeNameWith(ParentName, Subst);
								const std::string Next =
								    ResolveInheritanceBaseLinkage(
								        ParentWritten,
								        Decl->ModulePath);
								if (!Next.empty())
								{
									WalkQueue.push_back(Next);
								}
							}
						}
					}
					const std::vector<std::string> Bases = GetAstBaseTypeNames(Current);
					Queue.insert(Queue.end(), Bases.begin(), Bases.end());
				}
			ContinueIfaceSubtype:
				continue;
			}
			return false;
		}

		// Do not CanonicalTypeNameForCoercion / ResolveTypeLinkageName here: that can
		// collapse an exported simple name onto a shadowed private type in CurrentModulePath.
		std::vector<std::string> Queue{SourceTypeName};
		std::unordered_set<std::string> Visited;
		for (std::size_t Qi = 0; Qi < Queue.size(); ++Qi)
		{
			const std::string Current = Queue[Qi];
			if (Current.empty() || !Visited.insert(Current).second)
			{
				continue;
			}
			if (Current == TargetTypeName)
			{
				return true;
			}
			bool bFoundInModule = false;
			if (Module)
			{
				for (const FBytecodeTypeDesc& TypeDesc : Module->Types)
				{
					if (TypeDesc.Name != Current)
					{
						continue;
					}
					bFoundInModule = true;
					for (const std::uint16_t BaseIdx : TypeDesc.BaseTypeIndices)
					{
						if (BaseIdx < Module->Types.size())
						{
							Queue.push_back(Module->Types[BaseIdx].Name);
						}
					}
					if (!TypeDesc.HostBaseTypeName.empty())
					{
						Queue.push_back(TypeDesc.HostBaseTypeName);
					}
					break;
				}
			}
			if (bFoundInModule)
			{
				continue;
			}
			const std::vector<std::string> Bases = GetAstBaseTypeNames(Current);
			Queue.insert(Queue.end(), Bases.begin(), Bases.end());
		}
		return false;
	}

	std::vector<std::string> VeroScriptCompiler::GetAstBaseTypeNames(
	    const std::string& TypeLinkageName) const
	{
		std::vector<std::string> Result;
		if (Module)
		{
			const auto TypeIndex = FindRegisteredTypeIndex(TypeLinkageName);
			if (TypeIndex != static_cast<std::uint16_t>(-1))
			{
				const auto& TypeDesc = Module->Types[TypeIndex];
				for (const std::uint16_t BaseIndex : TypeDesc.BaseTypeIndices)
				{
					if (BaseIndex < Module->Types.size())
					{
						Result.push_back(Module->Types[BaseIndex].Name);
					}
				}
				if (!TypeDesc.HostBaseTypeName.empty())
				{
					Result.push_back(TypeDesc.HostBaseTypeName);
				}
				return Result;
			}
		}
		if (!ActiveProgram)
		{
			return Result;
		}
		const std::string TemplateLinkage = [&]() -> std::string
		{
			const std::string Base = GenericTemplateBaseLinkage(TypeLinkageName);
			return Base.empty() ? TypeLinkageName : Base;
		}();
		const std::vector<std::string> ConcreteTypes = DecodeGenericConcreteTypes(TypeLinkageName);
		const std::size_t WantedArity = TypeParamArityFromLinkage(TypeLinkageName);
		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceStructIndices, TemplateLinkage))
		{
			const auto& StructDecl = ActiveProgram->Structs[DeclarationIndex];
			if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) != TemplateLinkage || TemplateParameterNames(StructDecl).size() != WantedArity)
			{
				continue;
			}
			if (StructDecl.BaseTypeName.empty())
			{
				return Result;
			}
			std::string BaseAsWritten = StructDecl.BaseTypeName;
			if (!TemplateParameterNames(StructDecl).empty() && ConcreteTypes.size() == TemplateParameterNames(StructDecl).size())
			{
				std::unordered_map<std::string, std::string> Subst;
				for (std::size_t Index = 0; Index < TemplateParameterNames(StructDecl).size(); ++Index)
				{
					Subst.emplace(TemplateParameterNames(StructDecl)[Index], ConcreteTypes[Index]);
				}
				BaseAsWritten = SubstituteTypeNameWith(StructDecl.BaseTypeName, Subst);
			}
			if (ActiveBindings && ActiveBindings->FindType(BaseAsWritten) != nullptr)
			{
				Result.push_back(BaseAsWritten);
				return Result;
			}
			const std::string BaseLinkage =
			    ResolveInheritanceBaseLinkage(BaseAsWritten, StructDecl.ModulePath);
			Result.push_back(BaseLinkage.empty() ? BaseAsWritten : BaseLinkage);
			return Result;
		}
		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceClassIndices, TemplateLinkage))
		{
			const auto& ClassDecl = ActiveProgram->Classes[DeclarationIndex];
			if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) != TemplateLinkage || TemplateParameterNames(ClassDecl).size() != WantedArity)
			{
				continue;
			}
			std::unordered_map<std::string, std::string> Subst;
			if (!TemplateParameterNames(ClassDecl).empty() && ConcreteTypes.size() == TemplateParameterNames(ClassDecl).size())
			{
				for (std::size_t Index = 0; Index < TemplateParameterNames(ClassDecl).size(); ++Index)
				{
					Subst.emplace(TemplateParameterNames(ClassDecl)[Index], ConcreteTypes[Index]);
				}
			}
			for (const std::string& BaseSimple : ClassDecl.BaseTypeNames)
			{
				const std::string BaseAsWritten = Subst.empty()
				    ? BaseSimple
				    : SubstituteTypeNameWith(BaseSimple, Subst);
				if (ActiveBindings && ActiveBindings->FindType(BaseAsWritten) != nullptr)
				{
					Result.push_back(BaseAsWritten);
					continue;
				}
				const std::string BaseLinkage =
				    ResolveInheritanceBaseLinkage(BaseAsWritten, ClassDecl.ModulePath);
				Result.push_back(BaseLinkage.empty() ? BaseAsWritten : BaseLinkage);
			}
			return Result;
		}
		return Result;
	}

	std::string VeroScriptCompiler::ResolveHostDispatchTypeName(
	    const std::string& OwnerTypeName) const
	{
		if (OwnerTypeName.empty())
		{
			return {};
		}
		if (ActiveBindings && ActiveBindings->FindType(OwnerTypeName) != nullptr)
		{
			return OwnerTypeName;
		}
		if (!Module)
		{
			return {};
		}
		std::string OwnerLinkage = OwnerTypeName;
		if (FindGenericTypeMarker(OwnerTypeName) == std::string::npos && OwnerTypeName.find('\x1F') == std::string::npos)
		{
			const std::string Resolved = ResolveTypeLinkageName(OwnerTypeName, CurrentModulePath);
			if (!Resolved.empty())
			{
				OwnerLinkage = Resolved;
			}
		}
		std::vector<std::string> Queue{OwnerLinkage, OwnerTypeName};
		std::unordered_set<std::string> Visited;
		for (std::size_t Qi = 0; Qi < Queue.size(); ++Qi)
		{
			const std::string Current = Queue[Qi];
			if (Current.empty() || !Visited.insert(Current).second)
			{
				continue;
			}
			if (ActiveBindings && ActiveBindings->FindType(Current) != nullptr)
			{
				return Current;
			}
			const auto TypeIndex = FindRegisteredTypeIndex(Current);
			if (TypeIndex != static_cast<std::uint16_t>(-1))
			{
				const auto& TypeDesc = Module->Types[TypeIndex];
				if (!TypeDesc.HostBaseTypeName.empty())
				{
					// Only dispatch to a registered host type (stale BC / missing bindings).
					if (ActiveBindings && ActiveBindings->FindType(TypeDesc.HostBaseTypeName) != nullptr)
					{
						return TypeDesc.HostBaseTypeName;
					}
					return {};
				}
				for (const std::uint16_t BaseIdx : TypeDesc.BaseTypeIndices)
				{
					if (BaseIdx < Module->Types.size())
					{
						Queue.push_back(Module->Types[BaseIdx].Name);
					}
				}
			}
		}
		return {};
	}

	std::string VeroScriptCompiler::CurrentMethodHostBaseTypeName() const
	{
		if (!Module || !CurrentFunction || !CurrentFunction->bIsMethod)
		{
			return {};
		}
		if (CurrentFunction->OwnerTypeIndex >= Module->Types.size())
		{
			return {};
		}
		return ResolveHostDispatchTypeName(Module->Types[CurrentFunction->OwnerTypeIndex].Name);
	}

	bool VeroScriptCompiler::IsSuperReceiver(const FExpr& Object) const
	{
		if (Object.Kind != EExprKind::Identifier)
		{
			return false;
		}
		const std::string& Name = static_cast<const FIdentifierExpr&>(Object).Name;
		if (Name != "Super")
		{
			return false;
		}
		std::string BoundTypeName;
		std::uint16_t ThisFieldIndex = 0;
		return !TryResolveVariableTypeName(Name, BoundTypeName) && !ResolveThisField(Name, ThisFieldIndex) && !IsTypeNameWithoutValueBinding(Name);
	}

	bool VeroScriptCompiler::EmitHostBaseMethodCall(
	    FEmitter& Emitter,
	    const std::string& MethodName,
	    const FNativeFunction& HostNative,
	    const std::uint16_t HostNativeIndex,
	    const FExprArgumentView Arguments,
	    const FSourceLocation& Location)
	{
		const bool bHostStatic = HostNative.bHostStaticMethod || HostNative.ParamTypes.empty() || HostNative.ParamTypes[0] != ENativeType::HostObject;
		const std::size_t ExpectedArgs = bHostStatic
		    ? HostNative.ParamTypes.size()
		    : HostNative.ParamTypes.size() - 1;
		if (Arguments.size() != ExpectedArgs)
		{
			ReportError("wrong argument count for method: " + MethodName, Location);
			return false;
		}
		if (!bHostStatic)
		{
			Emitter.Emit(EOpcode::LoadThis);
		}
		for (std::size_t ArgumentIndex = 0; ArgumentIndex < Arguments.size(); ++ArgumentIndex)
		{
			if (!Arguments[ArgumentIndex])
			{
				ReportError("invalid argument for method: " + MethodName, Location);
				return false;
			}
			const std::size_t ParamIndex = bHostStatic ? ArgumentIndex : ArgumentIndex + 1;
			if (Arguments[ArgumentIndex]->Kind == EExprKind::Ref && !NativeParamFlagIsRef(NativeParamFlagAt(HostNative, ParamIndex)))
			{
				ReportErrorId("diag.native_does_not_accept_ref_arguments", Location);
				return false;
			}
			const std::string ParamTypeName = ResolveNativeParamTypeName(HostNative, ParamIndex);
			if (Arguments[ArgumentIndex]->Kind == EExprKind::Ref)
			{
				const auto Flag = NativeParamFlagAt(HostNative, ParamIndex);
				if (!CompileOperatorArgument(Emitter, *Arguments[ArgumentIndex],
				        ((Flag & NativeParamFlagConstRef) ? "const ref " : "ref ") + ParamTypeName, Location))
				{
					return false;
				}
				continue;
			}
			const std::string PreviousExpectedType = ExpressionExpectedType;
			ExpressionExpectedType = ParamTypeName;
			const bool bCompiled = CompileExpression(*Arguments[ArgumentIndex]);
			const bool bCoerced = bCompiled && EmitValueCoercionForTarget(Emitter, ParamTypeName, *Arguments[ArgumentIndex], Location);
			ExpressionExpectedType = PreviousExpectedType;
			if (!bCompiled || !bCoerced)
			{
				return false;
			}
		}
		Emitter.EmitCallNative(
		    HostNativeIndex,
		    static_cast<std::uint8_t>(bHostStatic ? Arguments.size() : Arguments.size() + 1));
		if (HostNative.ReturnType == ENativeType::Void)
		{
			FBytecodeConstant Zero;
			Zero.Kind = EConstantKind::I32;
			Emitter.EmitU16Operand(EOpcode::ConstI32, AddConstant(Zero));
		}
		return true;
	}

	bool VeroScriptCompiler::EmitImplicitReceiverCompoundAssign(
	    FEmitter& Emitter,
	    const FAssignExpr& Assign,
	    const std::uint16_t HostGetterIndex,
	    const std::uint16_t HostSetterIndex,
	    const std::string& ThisFieldName,
	    const std::string& TargetTypeName,
	    const std::string& RightTypeName,
	    const FSourceLocation& Location)
	{
		const bool bThisField = !ThisFieldName.empty();
		// LoadThis is side-effect free, so the receiver needs no temp (unlike `obj.prop`).
		const auto EmitLoadCurrent = [&]()
		{
			if (bThisField)
			{
				Emitter.EmitU16Operand(EOpcode::LoadThisField, AddString(ThisFieldName));
			}
			else
			{
				Emitter.Emit(EOpcode::LoadThis);
				Emitter.EmitCallNative(HostGetterIndex, 1);
			}
		};

		EmitLoadCurrent();
		std::uint16_t OldTemp = 0;
		if (Assign.bFromPostfixIncDec)
		{
			OldTemp = AllocateAssignTemp();
			Emitter.EmitU16Operand(EOpcode::StoreLocal, OldTemp);
			Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
		}
		std::string MutableRightTypeName = RightTypeName;
		if (!TryEmitIncDecUnitOne(Emitter, Assign, TargetTypeName, MutableRightTypeName) && !CompileExpression(*Assign.Value))
		{
			return false;
		}
		std::string OverloadedResultType;
		if (!EmitCompoundAssignOp(
		        Emitter,
		        Assign.CompoundOp,
		        TargetTypeName,
		        MutableRightTypeName,
		        Location,
		        &OverloadedResultType, Assign.Value.get()))
		{
			return false;
		}
		if (OverloadedResultType == "void")
		{
			Emitter.Emit(EOpcode::Pop);
			if (Assign.bFromPostfixIncDec)
			{
				Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
			}
			else
			{
				EmitLoadCurrent();
			}
			return true;
		}
		const std::string ResultTypeName = ResolveCompoundAssignResultType(
		    Assign.CompoundOp,
		    TargetTypeName,
		    MutableRightTypeName,
		    OverloadedResultType);
		if (!EmitValueCoercionBetweenTypes(
		        Emitter,
		        TargetTypeName,
		        ResultTypeName,
		        Location))
		{
			return false;
		}
		if (NeedsPassByValueCopy(TargetTypeName))
		{
			EmitValueCopy(Emitter, TargetTypeName);
		}
		if (bThisField)
		{
			Emitter.EmitU16Operand(EOpcode::StoreThisField, AddString(ThisFieldName));
			if (Assign.bFromPostfixIncDec)
			{
				Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
			}
			else
			{
				Emitter.EmitU16Operand(EOpcode::LoadThisField, AddString(ThisFieldName));
			}
			return true;
		}
		const std::uint16_t ValueTemp = AllocateAssignTemp();
		Emitter.EmitU16Operand(EOpcode::StoreLocal, ValueTemp);
		Emitter.Emit(EOpcode::LoadThis);
		Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
		Emitter.EmitCallNative(HostSetterIndex, 2);
		if (Assign.bFromPostfixIncDec)
		{
			Emitter.EmitU16Operand(EOpcode::LoadLocal, OldTemp);
		}
		else
		{
			Emitter.EmitU16Operand(EOpcode::LoadLocal, ValueTemp);
		}
		return true;
	}

	bool VeroScriptCompiler::CompileSuperMethodCall(
	    const FMethodCallExpr& MethodCall,
	    FEmitter& Emitter)
	{
		if (!CurrentFunction || !CurrentFunction->bIsMethod)
		{
			ReportError("'Super' is only valid inside instance methods", MethodCall.Location);
			return false;
		}
		std::vector<std::string> ArgTypeNames;
		const auto& MethodArguments = MethodCall.GetArguments();
		ArgTypeNames.reserve(MethodArguments.size());
		for (const FExpr* ArgExpr : MethodArguments)
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

		std::string BaseOwnerLinkage;
		const FMethodDecl* BaseMethod = nullptr;
		if (TryResolveSuperScriptMethod(
		        MethodCall.Method,
		        ArgTypeNames,
		        MethodCall.Location,
		        BaseOwnerLinkage,
		        BaseMethod) &&
		    BaseMethod)
		{
			if (BaseMethod->bDeleted)
			{
				ReportErrorId("diag.use_of_deleted_declaration", MethodCall.Location,
				    {MethodCall.Method});
				return false;
			}
			if (BaseMethod->bIsStatic)
			{
				ReportError(
				    "cannot call static method through 'Super': " + MethodCall.Method,
				    MethodCall.Location);
				return false;
			}
			if (CurrentFunction->bIsConstMethod && !BaseMethod->bIsConst)
			{
				ReportErrorId(
				    "diag.cannot_call_mutating_method_on_const",
				    MethodCall.Location,
				    {MethodCall.Method});
				return false;
			}
			if (!ValidateMemberAccess(
			        MethodCall.Location,
			        BaseOwnerLinkage,
			        MethodCall.Method,
			        true,
			        ArgTypeNames.size(),
			        BaseMethod))
			{
				return false;
			}
			if (!ParamsAcceptArgCount(BaseMethod->Parameters, MethodArguments.size()))
			{
				ReportError(
				    "wrong argument count for method: " + MethodCall.Method,
				    MethodCall.Location);
				return false;
			}
			const std::string BaseModulePath = [&]() -> std::string
			{
				const std::string Resolved = ResolveOwnerModulePath(BaseOwnerLinkage);
				return Resolved.empty() ? CurrentModulePath : Resolved;
			}();
			std::unordered_map<std::string, std::string> OwnerSubst;
			if (!GenericTemplateBaseLinkage(BaseOwnerLinkage).empty() && !TryBuildGenericOwnerSubst(BaseOwnerLinkage, OwnerSubst, MethodCall.Location))
			{
				ReportError(
				    "could not decode generic base type: " + DisplayTypeName(BaseOwnerLinkage),
				    MethodCall.Location);
				return false;
			}
			// Static bind: the base body must run even when the derived type overrides it.
			const std::string MethodKey = MakeMethodFunctionName(
			    BaseOwnerLinkage,
			    MethodCall.Method,
			    CanonicalMethodParamTypeNames(*BaseMethod, BaseModulePath, OwnerSubst),
			    BaseMethod->bIsConst);
			const std::uint16_t FunctionIndex = FindFunctionIndex(MethodKey);
			if (FunctionIndex == static_cast<std::uint16_t>(-1))
			{
				ReportError(
				    "base method has no body: " + MethodCall.Method,
				    MethodCall.Location);
				return false;
			}
			Emitter.Emit(EOpcode::LoadThis);
			const bool bVariadic = IsVariadicParamList(BaseMethod->Parameters);
			const std::size_t FixedCount = FixedParamCount(BaseMethod->Parameters);
			const std::size_t ArgCount = MethodArguments.size();
			const std::size_t FixedCompile = bVariadic
			    ? (std::min)(ArgCount, FixedCount)
			    : ArgCount;
			for (std::size_t ArgumentIndex = 0; ArgumentIndex < FixedCompile; ++ArgumentIndex)
			{
				const FExpr& RawArgument = *MethodArguments[ArgumentIndex];
				const bool bArgIsRef = RawArgument.Kind == EExprKind::Ref;
				const bool bParamIsRef = ArgumentIndex < BaseMethod->Parameters.size() && BaseMethod->Parameters[ArgumentIndex].bIsRef;
				if (bArgIsRef != bParamIsRef)
				{
					ReportErrorId(
					    bParamIsRef
					        ? "diag.ref_parameter_requires_ref_argument"
					        : "diag.ref_argument_requires_ref_parameter",
					    MethodCall.Location);
					return false;
				}
				const FExpr& Argument = UnwrapRefOperand(RawArgument);
				const std::string ParamType = ArgumentIndex < BaseMethod->Parameters.size()
				    ? QualifyTypeName(
				          SubstituteTypeNameWith(
				              BaseMethod->Parameters[ArgumentIndex].TypeName,
				              OwnerSubst),
				          BaseModulePath)
				    : std::string{};
				if (!ParamType.empty() && !ValidateDynamicArrayAssignment(ParamType, Argument, MethodCall.Location))
				{
					return false;
				}
				if (bArgIsRef)
				{
					const bool bMutableRef = !(ArgumentIndex < BaseMethod->Parameters.size() && BaseMethod->Parameters[ArgumentIndex].bIsConst);
					if (!ParamType.empty() && !ValidateRefArgumentType(ParamType, Argument, MethodCall.Location))
					{
						return false;
					}
					if (!CompileRefArgument(Emitter, Argument, MethodCall.Location, bMutableRef))
					{
						return false;
					}
					continue;
				}
				const std::string PreviousExpectedType = ExpressionExpectedType;
				if (!ParamType.empty())
				{
					ExpressionExpectedType = ParamType;
				}
				const bool bCompiledArg = CompileExpression(Argument);
				bool bCoerced = bCompiledArg;
				if (bCompiledArg && !ParamType.empty())
				{
					bCoerced = EmitValueCoercionForTarget(
					    Emitter,
					    ParamType,
					    Argument,
					    MethodCall.Location);
				}
				ExpressionExpectedType = PreviousExpectedType;
				if (!bCompiledArg || !bCoerced)
				{
					return false;
				}
				if (!ParamType.empty() && NeedsPassByValueCopy(ParamType) && !CanTransferCoercedValue(Argument, ParamType))
				{
					EmitValueCopy(Emitter, ParamType);
				}
			}
			if (bVariadic && ArgCount > FixedCount)
			{
				if (!EmitVariadicRestArgument(
				        Emitter,
				        BaseMethod->Parameters,
				        MethodArguments,
				        FixedCount,
				        BaseModulePath,
				        OwnerSubst,
				        MethodCall.Location))
				{
					return false;
				}
			}
			else if (!EmitTrailingDefaultArguments(
			             Emitter,
			             BaseMethod->Parameters,
			             bVariadic ? FixedCompile : ArgCount,
			             BaseModulePath,
			             OwnerSubst,
			             MethodCall.Location))
			{
				return false;
			}
			Emitter.EmitCallBoundMethod(
			    FunctionIndex,
			    static_cast<std::uint8_t>(BaseMethod->Parameters.size()));
			return true;
		}
		if (HasError())
		{
			return false;
		}

		const std::string HostBase = CurrentMethodHostBaseTypeName();
		if (!HostBase.empty())
		{
			std::uint16_t HostNativeIndex = static_cast<std::uint16_t>(-1);
			if (const FNativeFunction* const HostNative = FindNativeForCall(
			        MakeHostMethodNativeName(HostBase, MethodCall.Method),
			        MethodArguments, HostNativeIndex, MethodCall.Location, true, 0,
			        CurrentFunction && CurrentFunction->bIsConstMethod))
			{
				return EmitHostBaseMethodCall(
				    Emitter,
				    MethodCall.Method,
				    *HostNative,
				    HostNativeIndex,
				    MethodArguments,
				    MethodCall.Location);
			}
		}
		ReportError("unknown base method: " + MethodCall.Method, MethodCall.Location);
		return false;
	}

	bool VeroScriptCompiler::TryResolveSuperScriptMethod(
	    const std::string& MethodName,
	    const std::vector<std::string>& ArgTypeNames,
	    const FSourceLocation& Location,
	    std::string& OutOwnerLinkageName,
	    const FMethodDecl*& OutMethod) const
	{
		OutOwnerLinkageName.clear();
		OutMethod = nullptr;
		if (!Module || !CurrentFunction || !CurrentFunction->bIsMethod || CurrentFunction->OwnerTypeIndex >= Module->Types.size())
		{
			return false;
		}
		const std::string OwnerLinkage = Module->Types[CurrentFunction->OwnerTypeIndex].Name;
		std::vector<std::pair<std::string, const FMethodDecl*>> Candidates;
		for (const std::string& BaseLinkage : GetAstBaseTypeNames(OwnerLinkage))
		{
			if (BaseLinkage.empty() || (ActiveBindings && ActiveBindings->FindType(BaseLinkage) != nullptr))
			{
				// Host bases have no script method table; the caller falls back to natives.
				continue;
			}
			std::string ResolvedOwner;
			const FMethodDecl* Method = nullptr;
			if (ResolveMethodForCall(
			        BaseLinkage,
			        MethodName,
			        ArgTypeNames,
			        ResolvedOwner,
			        Method,
			        Location,
			        true,
			        false,
			        nullptr,
			        CurrentFunction && CurrentFunction->bIsConstMethod) &&
			    Method)
			{
				const std::string Owner = ResolvedOwner.empty() ? BaseLinkage : ResolvedOwner;
				bool bDuplicate = false;
				for (const auto& Existing : Candidates)
				{
					if (Existing.first == Owner && Existing.second == Method)
					{
						bDuplicate = true;
						break;
					}
				}
				if (!bDuplicate)
				{
					Candidates.emplace_back(Owner, Method);
				}
			}
		}
		if (Candidates.size() > 1)
		{
			ReportErrorId(
			    "diag.ambiguous_method_reachable_from_multiple_bases_override_it_in",
			    Location,
			    {MethodName, OwnerLinkage});
			return false;
		}
		if (Candidates.size() == 1)
		{
			OutOwnerLinkageName = Candidates[0].first;
			OutMethod = Candidates[0].second;
			return true;
		}
		return false;
	}

	std::vector<std::string> VeroScriptCompiler::GetAstImplementedInterfaceLinkages(
	    const std::string& TypeLinkageName) const
	{
		std::vector<std::string> Result;
		if (!ActiveProgram)
		{
			return Result;
		}
		const std::string TemplateLinkage = [&]() -> std::string
		{
			const std::string Base = GenericTemplateBaseLinkage(TypeLinkageName);
			return Base.empty() ? TypeLinkageName : Base;
		}();
		const std::vector<std::string> ConcreteTypes = DecodeGenericConcreteTypes(TypeLinkageName);
		const std::size_t WantedArity = TypeParamArityFromLinkage(TypeLinkageName);
		auto ResolveIfaceList =
		    [&](const std::vector<std::string>& InterfaceTypeNames,
		        const std::string& OwnerModulePath,
		        const std::vector<std::string>& TypeParams)
		{
			std::unordered_map<std::string, std::string> Subst;
			if (!TypeParams.empty() && !ConcreteTypes.empty() && TypeParams.size() == ConcreteTypes.size())
			{
				for (std::size_t Index = 0; Index < TypeParams.size(); ++Index)
				{
					Subst.emplace(TypeParams[Index], ConcreteTypes[Index]);
				}
			}
			for (const std::string& InterfaceName : InterfaceTypeNames)
			{
				const std::string AsWritten = Subst.empty()
				    ? InterfaceName
				    : SubstituteTypeNameWith(InterfaceName, Subst);
				const std::string Linkage =
				    ResolveInheritanceBaseLinkage(AsWritten, OwnerModulePath);
				Result.push_back(Linkage.empty() ? AsWritten : Linkage);
			}
		};
		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceStructIndices, TemplateLinkage))
		{
			const auto& StructDecl = ActiveProgram->Structs[DeclarationIndex];
			if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) != TemplateLinkage || TemplateParameterNames(StructDecl).size() != WantedArity)
			{
				continue;
			}
			ResolveIfaceList(
			    StructDecl.InterfaceTypeNames,
			    StructDecl.ModulePath,
			    TemplateParameterNames(StructDecl));
			return Result;
		}
		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceClassIndices, TemplateLinkage))
		{
			const auto& ClassDecl = ActiveProgram->Classes[DeclarationIndex];
			if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) != TemplateLinkage || TemplateParameterNames(ClassDecl).size() != WantedArity)
			{
				continue;
			}
			ResolveIfaceList(
			    ClassDecl.InterfaceTypeNames,
			    ClassDecl.ModulePath,
			    TemplateParameterNames(ClassDecl));
			return Result;
		}
		return Result;
	}

	void VeroScriptCompiler::CollectAncestorLinkageSet(
	    const std::string& TypeLinkageName,
	    std::unordered_set<std::string>& OutAncestors) const
	{
		std::vector<std::string> Queue{TypeLinkageName};
		for (std::size_t Qi = 0; Qi < Queue.size(); ++Qi)
		{
			const std::string Current = Queue[Qi];
			if (Current.empty() || !OutAncestors.insert(Current).second)
			{
				continue;
			}
			const std::vector<std::string> Bases = GetAstBaseTypeNames(Current);
			Queue.insert(Queue.end(), Bases.begin(), Bases.end());
		}
	}

	bool VeroScriptCompiler::CheckDiamondInheritance(
	    const std::vector<std::string>& BaseTypeNamesSimple,
	    const std::string& OwnerModulePath,
	    const FSourceLocation& Location)
	{
		if (BaseTypeNamesSimple.size() < 2)
		{
			return true;
		}
		std::vector<std::unordered_set<std::string>> AncestorSets;
		AncestorSets.reserve(BaseTypeNamesSimple.size());
		for (const std::string& BaseSimple : BaseTypeNamesSimple)
		{
			const std::string BaseLinkage =
			    ResolveInheritanceBaseLinkage(BaseSimple, OwnerModulePath);
			if (BaseLinkage.empty())
			{
				ReportErrorId("diag.unknown_base_type", Location, {BaseSimple});
				return false;
			}
			std::unordered_set<std::string> Ancestors;
			CollectAncestorLinkageSet(BaseLinkage, Ancestors);
			AncestorSets.push_back(std::move(Ancestors));
		}
		for (std::size_t I = 0; I < AncestorSets.size(); ++I)
		{
			for (std::size_t J = I + 1; J < AncestorSets.size(); ++J)
			{
				for (const std::string& Ancestor : AncestorSets[I])
				{
					if (AncestorSets[J].count(Ancestor) > 0)
					{
						ReportErrorId(
						    "diag.diamond_inheritance_is_not_allowed_common_ancestor_reachable_via_and",
						    Location,
						    {
						        DisplayTypeName(Ancestor),
						        BaseTypeNamesSimple[I],
						        BaseTypeNamesSimple[J],
						    });
						return false;
					}
				}
			}
		}
		return true;
	}

	bool VeroScriptCompiler::TypeHasUserConstructors(const std::string& TypeLinkageName) const
	{
		if (!ActiveProgram)
		{
			return false;
		}
		const std::string TemplateBase = GenericTemplateBaseLinkage(TypeLinkageName);
		const std::string LookupName = TemplateBase.empty() ? TypeLinkageName : TemplateBase;
		const std::size_t WantedArity = TypeParamArityFromLinkage(TypeLinkageName);
		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceStructIndices, LookupName))
		{
			const auto& StructDecl = ActiveProgram->Structs[DeclarationIndex];
			if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) == LookupName && TemplateParameterNames(StructDecl).size() == WantedArity)
			{
				return !StructDecl.Constructors.empty();
			}
		}
		for (const auto DeclarationIndex : FindDeclarationCandidates(SourceClassIndices, LookupName))
		{
			const auto& ClassDecl = ActiveProgram->Classes[DeclarationIndex];
			if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) == LookupName && TemplateParameterNames(ClassDecl).size() == WantedArity)
			{
				return !ClassDecl.Constructors.empty();
			}
		}
		for (const FStructDecl& StructDecl : BuiltinStructTemplates)
		{
			if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) == LookupName && TemplateParameterNames(StructDecl).size() == WantedArity)
			{
				return !StructDecl.Constructors.empty();
			}
		}
		for (const FClassDecl& ClassDecl : BuiltinClassTemplates)
		{
			if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) == LookupName && TemplateParameterNames(ClassDecl).size() == WantedArity)
			{
				return !ClassDecl.Constructors.empty();
			}
		}
		return false;
	}

	bool VeroScriptCompiler::ResolveConstructorForCall(
	    const std::string& TypeLinkageName,
	    const std::vector<std::string>& ArgTypeNames,
	    const FConstructorDecl*& OutConstructor,
	    std::string& OutCtorKey,
	    const FSourceLocation& Location,
	    const bool bReportErrors,
	    bool* OutAmbiguous,
	    const bool bAllowExplicit,
	    const bool bAllowParamUserDefinedConversions) const
	{
		OutConstructor = nullptr;
		OutCtorKey.clear();
		if (OutAmbiguous)
		{
			*OutAmbiguous = false;
		}
		if (!ActiveProgram)
		{
			return false;
		}

		const std::string TemplateBase = GenericTemplateBaseLinkage(TypeLinkageName);
		const std::string LookupName = TemplateBase.empty() ? TypeLinkageName : TemplateBase;
		const std::size_t WantedArity = TypeParamArityFromLinkage(TypeLinkageName);
		const std::size_t ArgCount = ArgTypeNames.size();
		const std::string SimpleName = GetTypeSimpleName(TypeLinkageName);

		std::unordered_map<std::string, std::string> OwnerSubst;
		TryBuildGenericOwnerSubst(TypeLinkageName, OwnerSubst, Location);

		struct FCtorCandidate
		{
			int Score = -1;
			const FConstructorDecl* Constructor = nullptr;
			std::string DeclModulePath;
		};

		const auto ScoreConstructor =
		    [&](const FConstructorDecl& Constructor, const std::string& DeclModulePath) -> int
		{
			if (!ParamsAcceptArgCount(Constructor.Parameters, ArgCount))
			{
				return -1;
			}
			const std::vector<std::string> ParamTypes =
			    CanonicalParamTypeNames(Constructor.Parameters, DeclModulePath, OwnerSubst);
			const bool bVariadic = IsVariadicParamList(Constructor.Parameters);
			const std::size_t FixedCount = FixedParamCount(Constructor.Parameters);
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
				if (!TryScoreArgParamTypes(
				        ArgTypeName,
				        ParamCanonical,
				        Contribution,
				        bAllowParamUserDefinedConversions))
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
				std::string ElementType = Constructor.Parameters.back().TypeName;
				ElementType = CanonicalTypeNameForCoercion(
				    QualifyTypeName(
				        NormalizeTypeAlias(
				            SubstituteTypeNameWith(ElementType, OwnerSubst),
				            DeclModulePath),
				        DeclModulePath));
				for (std::size_t Index = FixedCount; Index < ArgCount; ++Index)
				{
					ScorePair(ArgTypeNames[Index], ElementType);
				}
			}
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

		std::vector<FCtorCandidate> Scored;
		const auto ConsiderDecl =
		    [&](const std::vector<FConstructorDecl>& Constructors,
		        const std::string& DeclModulePath,
		        const std::size_t TypeParamCount,
		        const std::string& DeclLinkage)
		{
			if (DeclLinkage != LookupName || TypeParamCount != WantedArity)
			{
				return;
			}
			for (const FConstructorDecl& Constructor : Constructors)
			{
				if (Constructor.bExplicit && !bAllowExplicit)
				{
					continue;
				}
				const int Score = ScoreConstructor(Constructor, DeclModulePath);
				if (Score < -2)
				{
					continue;
				}
				if (Score == -1)
				{
					continue;
				}
				Scored.push_back(FCtorCandidate{Score, &Constructor, DeclModulePath});
			}
		};

		for (const FStructDecl& StructDecl : ActiveProgram->Structs)
		{
			ConsiderDecl(
			    StructDecl.Constructors,
			    StructDecl.ModulePath,
			    TemplateParameterNames(StructDecl).size(),
			    GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name));
		}
		for (const FClassDecl& ClassDecl : ActiveProgram->Classes)
		{
			ConsiderDecl(
			    ClassDecl.Constructors,
			    ClassDecl.ModulePath,
			    TemplateParameterNames(ClassDecl).size(),
			    GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name));
		}
		for (const FStructDecl& StructDecl : BuiltinStructTemplates)
		{
			ConsiderDecl(
			    StructDecl.Constructors,
			    StructDecl.ModulePath,
			    TemplateParameterNames(StructDecl).size(),
			    GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name));
		}
		for (const FClassDecl& ClassDecl : BuiltinClassTemplates)
		{
			ConsiderDecl(
			    ClassDecl.Constructors,
			    ClassDecl.ModulePath,
			    TemplateParameterNames(ClassDecl).size(),
			    GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name));
		}

		if (Scored.empty())
		{
			if (bReportErrors)
			{
				ReportErrorId(
				    "diag.no_matching_constructor_for_type",
				    Location,
				    {SimpleName.empty() ? TypeLinkageName : SimpleName});
			}
			return false;
		}

		int Best = std::numeric_limits<int>::min();
		for (const FCtorCandidate& Entry : Scored)
		{
			Best = std::max(Best, Entry.Score);
		}
		// Prefer viable scores (>= 0) over weak type-mismatch (-2).
		if (Best >= 0)
		{
			std::vector<const FCtorCandidate*> Ties;
			for (const FCtorCandidate& Entry : Scored)
			{
				if (Entry.Score == Best)
				{
					Ties.push_back(&Entry);
				}
			}
			if (Ties.size() > 1)
			{
				if (OutAmbiguous)
				{
					*OutAmbiguous = true;
				}
				if (bReportErrors)
				{
					ReportErrorId(
					    "diag.ambiguous_overload_for_method",
					    Location,
					    {SimpleName.empty() ? TypeLinkageName : SimpleName});
				}
				return false;
			}
			OutConstructor = Ties.front()->Constructor;
			if (bReportErrors && OutConstructor->Access != EMemberAccess::Public)
			{
				FMemberAccessInfo Info;
				Info.bFound = true;
				Info.EffectiveAccess = OutConstructor->Access;
				Info.DeclaringOwnerLinkage = LookupName;
				Info.DeclaringArity = WantedArity;
				const auto Separator = FindMethodLinkageSeparator(CurrentAccessFunctionLinkage);
				const auto Context = Separator == std::string::npos ? CurrentOwnerTypeLinkage
				                                                    : CurrentAccessFunctionLinkage.substr(0, Separator);
				if (!IsCurrentFunctionFriendOf(Info, TypeLinkageName) &&
				    !CanAccessMemberFromContext(Info, Context, *ActiveProgram, TypeLinkageName, false, ActiveImportIndex))
				{
					ReportError("constructor is inaccessible: " + SimpleName, Location);
					OutConstructor = nullptr;
					return false;
				}
			}
			if (OutConstructor->bDeleted)
			{
				if (bReportErrors)
				{
					ReportErrorId(
					    "diag.use_of_deleted_declaration",
					    Location,
					    {SimpleName.empty() ? TypeLinkageName : SimpleName});
				}
				OutConstructor = nullptr;
				return false;
			}
			OutCtorKey = MakeConstructorKey(
			    TypeLinkageName,
			    SimpleName.empty() ? TypeNameLeaf(LookupName) : SimpleName,
			    *OutConstructor,
			    Ties.front()->DeclModulePath,
			    OwnerSubst);
			return true;
		}

		// Only weak (-2) candidates: treat as no match for reporting.
		if (bReportErrors)
		{
			ReportErrorId(
			    "diag.no_matching_constructor_for_type",
			    Location,
			    {SimpleName.empty() ? TypeLinkageName : SimpleName});
		}
		return false;
	}

	bool VeroScriptCompiler::TypeHasZeroArgConstructor(const std::string& TypeLinkageName) const
	{
		const FConstructorDecl* Constructor = nullptr;
		std::string CtorKey;
		return ResolveConstructorForCall(
		    TypeLinkageName,
		    {},
		    Constructor,
		    CtorKey,
		    {},
		    false,
		    nullptr);
	}

	bool VeroScriptCompiler::TypeHasUnimplementedPureVirtual(
	    const std::string& TypeLinkageName,
	    std::string* const OutMethodName) const
	{
		if (OutMethodName)
		{
			OutMethodName->clear();
		}
		if (!ActiveProgram || TypeLinkageName.empty())
		{
			return false;
		}

		struct FVirtualSlot
		{
			std::string RootIdentity;
			std::string Signature;
			std::string MethodName;
			bool bPure = false;
		};
		std::unordered_set<std::string> Active;
		std::unordered_map<std::string, std::vector<FVirtualSlot>> Completed;
		std::function<bool(const std::string&, std::vector<FVirtualSlot>&)> Collect;
		Collect = [&](const std::string& Linkage, std::vector<FVirtualSlot>& OutSlots) -> bool
		{
			if (Linkage.empty())
			{
				return true;
			}
			const auto Cached = Completed.find(Linkage);
			if (Cached != Completed.end())
			{
				OutSlots = Cached->second;
				return true;
			}
			if (!Active.insert(Linkage).second)
			{
				return true;
			}
			const auto Leave = [&](const bool bComplete = false)
			{
				Active.erase(Linkage);
				// A diamond reuses the same owner's final slots on each path.
				// The root is visited once, so ordinary types need no cache entry.
				if (bComplete && Linkage != TypeLinkageName)
				{
					Completed.emplace(Linkage, OutSlots);
				}
			};
			const std::string TemplateBase = GenericTemplateBaseLinkage(Linkage);
			const std::string Lookup = TemplateBase.empty() ? Linkage : TemplateBase;
			const std::vector<std::string> ConcreteArgs = DecodeGenericConcreteTypes(Linkage);
			const std::size_t WantedArity = TypeParamArityFromLinkage(Linkage);

			const auto ApplyMethods = [&](const std::vector<FMethodDecl>& Methods,
			                              const std::string& ModulePath,
			                              const std::vector<std::string>& TypeParams)
			{
				std::unordered_map<std::string, std::string> Subst;
				if (TypeParams.size() == ConcreteArgs.size())
				{
					for (std::size_t Index = 0; Index < TypeParams.size(); ++Index)
					{
						Subst.emplace(TypeParams[Index], ConcreteArgs[Index]);
					}
				}
				for (const FMethodDecl& Method : Methods)
				{
					if (Method.bIsStatic || Method.bDeleted)
					{
						continue;
					}
					const std::vector<std::string> Params =
					    CanonicalMethodParamTypeNames(Method, ModulePath, Subst);
					std::string Signature = Method.Name.rfind("operator conv ", 0) == 0
					    ? ConversionOperatorMethodName(CanonicalTypeNameForCoercion(QualifyTypeName(
					          NormalizeTypeAlias(SubstituteTypeNameWith(Method.ReturnType, Subst), ModulePath),
					          ModulePath)))
					    : Method.Name;
					Signature.push_back('#');
					for (const std::string& Param : Params)
					{
						Signature += std::to_string(Param.size());
						Signature.push_back(':');
						Signature += Param;
					}
					std::string ReturnType = QualifyTypeName(
					    NormalizeTypeAlias(
					        SubstituteTypeNameWith(Method.ReturnType, Subst),
					        ModulePath),
					    ModulePath);
					if (Method.Name.rfind("operator conv ", 0) == 0)
					{
						ReturnType = CanonicalTypeNameForCoercion(ReturnType);
					}

					Signature += "#return";
					Signature += std::to_string(ReturnType.size());
					Signature.push_back(':');
					Signature += ReturnType;
					Signature += Method.bIsConst ? "#const" : "#mutable";
					bool bOverrode = false;
					for (FVirtualSlot& Slot : OutSlots)
					{
						if (Slot.Signature == Signature)
						{
							Slot.bPure = Method.bPureVirtual;
							Slot.MethodName = Method.Name;
							bOverrode = true;
						}
					}
					if (!bOverrode && (Method.bVirtual || Method.bOverride))
					{
						OutSlots.push_back(FVirtualSlot{
						    Linkage + "::" + Signature,
						    std::move(Signature),
						    Method.Name,
						    Method.bPureVirtual});
					}
				}
			};
			const auto ApplyDestructor = [&](const FDestructorDecl* const Destructor)
			{
				bool bOverrode = false;
				for (FVirtualSlot& Slot : OutSlots)
				{
					if (Slot.Signature == "@destructor")
					{
						// Every derived type has an implicit destructor, which is the final
						// overrider of all inherited virtual destructors.
						Slot.bPure = Destructor && Destructor->bPureVirtual;
						Slot.MethodName = "~" + GetTypeSimpleName(Linkage);
						bOverrode = true;
					}
				}
				if (Destructor && Destructor->bPureVirtual && !bOverrode)
				{
					OutSlots.push_back(FVirtualSlot{
					    Linkage + "::@destructor",
					    "@destructor",
					    "~" + GetTypeSimpleName(Linkage),
					    true});
				}
			};

			const auto CollectParents = [&](const std::vector<std::string>& Parents) -> bool
			{
				for (const std::string& Parent : Parents)
				{
					std::vector<FVirtualSlot> ParentSlots;
					if (!Collect(Parent, ParentSlots))
					{
						return false;
					}
					for (FVirtualSlot& Slot : ParentSlots)
					{
						const auto Duplicate = std::find_if(
						    OutSlots.begin(), OutSlots.end(),
						    [&](const FVirtualSlot& Existing)
						{
							return Existing.RootIdentity == Slot.RootIdentity;
						});
						if (Duplicate == OutSlots.end())
						{
							OutSlots.push_back(std::move(Slot));
						}
					}
				}
				return true;
			};

			for (const auto DeclarationIndex : FindDeclarationCandidates(SourceStructIndices, Lookup))
			{
				const FStructDecl& Decl = ActiveProgram->Structs[DeclarationIndex];
				if (GetSymbolLinkageName(Decl.ModulePath, Decl.bExported, Decl.Name) != Lookup || TemplateParameterNames(Decl).size() != WantedArity)
				{
					continue;
				}
				std::vector<std::string> Parents = GetAstBaseTypeNames(Linkage);
				const std::vector<std::string> Interfaces = GetAstImplementedInterfaceLinkages(Linkage);
				Parents.insert(Parents.end(), Interfaces.begin(), Interfaces.end());
				if (!CollectParents(Parents))
				{
					Leave();
					return false;
				}
				ApplyDestructor(Decl.Destructor.get());
				ApplyMethods(Decl.Methods, Decl.ModulePath, TemplateParameterNames(Decl));
				Leave(true);
				return true;
			}
			for (const auto DeclarationIndex : FindDeclarationCandidates(SourceClassIndices, Lookup))
			{
				const FClassDecl& Decl = ActiveProgram->Classes[DeclarationIndex];
				if (GetSymbolLinkageName(Decl.ModulePath, Decl.bExported, Decl.Name) != Lookup || TemplateParameterNames(Decl).size() != WantedArity)
				{
					continue;
				}
				std::vector<std::string> Parents = GetAstBaseTypeNames(Linkage);
				const std::vector<std::string> Interfaces = GetAstImplementedInterfaceLinkages(Linkage);
				Parents.insert(Parents.end(), Interfaces.begin(), Interfaces.end());
				if (!CollectParents(Parents))
				{
					Leave();
					return false;
				}
				ApplyDestructor(Decl.Destructor.get());
				ApplyMethods(Decl.Methods, Decl.ModulePath, TemplateParameterNames(Decl));
				Leave(true);
				return true;
			}
			for (const auto DeclarationIndex : FindDeclarationCandidates(SourceInterfaceIndices, Lookup))
			{
				const FInterfaceDecl& Decl = ActiveProgram->Interfaces[DeclarationIndex];
				if (GetSymbolLinkageName(Decl.ModulePath, Decl.bExported, Decl.Name) != Lookup || TemplateParameterNames(Decl).size() != WantedArity)
				{
					continue;
				}
				std::unordered_map<std::string, std::string> Subst;
				if (TemplateParameterNames(Decl).size() == ConcreteArgs.size())
				{
					for (std::size_t Index = 0; Index < TemplateParameterNames(Decl).size(); ++Index)
					{
						Subst.emplace(TemplateParameterNames(Decl)[Index], ConcreteArgs[Index]);
					}
				}
				std::vector<std::string> Parents;
				for (const std::string& ParentAsWritten : Decl.BaseInterfaceNames)
				{
					const std::string Substituted = SubstituteTypeNameWith(ParentAsWritten, Subst);
					const std::string Resolved = ResolveInheritanceBaseLinkage(Substituted, Decl.ModulePath);
					Parents.push_back(Resolved.empty() ? Substituted : Resolved);
				}
				if (!CollectParents(Parents))
				{
					Leave();
					return false;
				}
				ApplyMethods(Decl.Methods, Decl.ModulePath, TemplateParameterNames(Decl));
				Leave(true);
				return true;
			}
			Leave(true);
			return true;
		};

		std::vector<FVirtualSlot> Slots;
		Collect(TypeLinkageName, Slots);
		for (const FVirtualSlot& Slot : Slots)
		{
			if (Slot.bPure)
			{
				if (OutMethodName)
				{
					*OutMethodName = Slot.MethodName;
				}
				return true;
			}
		}
		if (Module)
		{
			const auto TypeIndex = FindRegisteredTypeIndex(TypeLinkageName);
			if (TypeIndex != static_cast<std::uint16_t>(-1) && Module->Types[TypeIndex].bIsAbstract) return true;
		}
		return false;
	}

	bool VeroScriptCompiler::RequireConcreteType(
	    const std::string& TypeLinkageName,
	    const FSourceLocation& Location)
	{
		const std::string ResolvedTypeLinkage =
		    ResolveTypeLinkageName(TypeLinkageName, CurrentModulePath);
		if (ActiveProgram)
		{
			for (const FStructDecl& StructDecl : ActiveProgram->Structs)
			{
				if (!StructDecl.bIsStatic)
				{
					continue;
				}
				const std::string StructLinkage = GetSymbolLinkageName(
				    StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name);
				if (TypeLinkageName == StructDecl.Name || TypeLinkageName == StructLinkage || (!ResolvedTypeLinkage.empty() && ResolvedTypeLinkage == StructLinkage))
				{
					ReportError(
					    "cannot instantiate static struct '" + DisplayTypeName(TypeLinkageName) + "'",
					    Location);
					return false;
				}
			}
			for (const FClassDecl& ClassDecl : ActiveProgram->Classes)
			{
				if (!ClassDecl.bIsStatic)
				{
					continue;
				}
				const std::string ClassLinkage = GetSymbolLinkageName(
				    ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name);
				if (TypeLinkageName == ClassDecl.Name || TypeLinkageName == ClassLinkage || (!ResolvedTypeLinkage.empty() && ResolvedTypeLinkage == ClassLinkage))
				{
					ReportError(
					    "cannot instantiate static class '" + DisplayTypeName(TypeLinkageName) + "'",
					    Location);
					return false;
				}
			}
		}
		if (Module)
		{
			for (const FBytecodeTypeDesc& Type : Module->Types)
			{
				if ((Type.Name == TypeLinkageName || (!ResolvedTypeLinkage.empty() && Type.Name == ResolvedTypeLinkage)) && Type.bIsStatic)
				{
					ReportError(
					    "cannot instantiate static " + std::string(Type.Kind == EScriptObjectKind::Struct ? "struct '" : "class '") + DisplayTypeName(TypeLinkageName) + "'",
					    Location);
					return false;
				}
			}
		}
		std::string MethodName;
		if (!TypeHasUnimplementedPureVirtual(TypeLinkageName, &MethodName))
		{
			return true;
		}
		const std::string TypeName = DisplayTypeName(TypeLinkageName);
		ReportError(
		    "cannot instantiate abstract type '" + TypeName + "'" + (MethodName.empty() ? std::string{} : "; pure virtual method remains: " + MethodName),
		    Location);
		return false;
	}

	std::string VeroScriptCompiler::GetTypeSimpleName(const std::string& TypeLinkageName) const
	{
		if (!ActiveProgram)
		{
			return {};
		}
		const std::string TemplateBase = GenericTemplateBaseLinkage(TypeLinkageName);
		const std::string LookupName = TemplateBase.empty() ? TypeLinkageName : TemplateBase;
		for (const auto Index : FindDeclarationCandidates(SourceStructIndices, LookupName))
		{
			const FStructDecl& StructDecl = ActiveProgram->Structs[Index];
			if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) == LookupName)
			{
				return TypeNameLeaf(StructDecl.Name);
			}
		}
		for (const auto Index : FindDeclarationCandidates(SourceClassIndices, LookupName))
		{
			const FClassDecl& ClassDecl = ActiveProgram->Classes[Index];
			if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) == LookupName)
			{
				return TypeNameLeaf(ClassDecl.Name);
			}
		}
		const std::size_t Sep = LookupName.find('\x1F');
		if (Sep != std::string::npos)
		{
			return TypeNameLeaf(LookupName.substr(Sep + 1));
		}
		return TypeNameLeaf(LookupName);
	}

	std::string VeroScriptCompiler::MakeConstructorKey(
	    const std::string& TypeLinkageName,
	    const std::string& TypeSimpleName,
	    const FConstructorDecl& Constructor,
	    const std::string& DeclModulePath,
	    const std::unordered_map<std::string, std::string>& Subst) const
	{
		return MakeConstructorFunctionName(
		    TypeLinkageName,
		    TypeNameLeaf(TypeSimpleName),
		    CanonicalParamTypeNames(Constructor.Parameters, DeclModulePath, Subst));
	}

	bool VeroScriptCompiler::UsesTrivialDefaultConstruction(const std::uint16_t TypeIndex) const
	{
		if (TypeIndex >= Module->Types.size()) return false;
		const auto& Type = Module->Types[TypeIndex];
		if (!Type.BaseTypeIndices.empty() || !Type.HostBaseTypeName.empty() || TypeHasUserConstructors(Type.Name))
		{
			return false;
		}
		for (const auto& Entry : Type.DeclMeta)
		{
			if (Entry.first == ClassDefaultsLinkageMetaKey) return false;
		}
		for (const auto& Field : Type.Fields)
		{
			if (Field.bHasInitCode || Runtime::IsReferenceTypeName(Field.DeclaredTypeName)) return false;
			if (Field.TypeName != "bool" && Field.TypeName != "string" && Field.TypeName != "wstring" && !IsNumericTypeName(Field.TypeName) && !IsEnumTypeName(Field.TypeName))
			{
				return false;
			}
		}
		return true;
	}

	bool VeroScriptCompiler::EnsureImplicitDefaultConstructor(
	    const std::string& TypeLinkageName, const std::uint16_t TypeIndex,
	    const FSourceLocation& Location, std::uint16_t& OutFunctionIndex)
	{
		const std::string TypeName = TypeLinkageName;
		const std::string TemplateBase = GenericTemplateBaseLinkage(TypeName);
		const std::string Lookup = TemplateBase.empty() ? TypeName : TemplateBase;
		const std::string SimpleName = TypeNameLeaf(GetTypeSimpleName(Lookup));
		const std::string Key = MakeConstructorFunctionName(TypeName, SimpleName, {});
		OutFunctionIndex = FindFunctionIndex(Key);
		if (OutFunctionIndex != static_cast<std::uint16_t>(-1)) return true;

		const std::vector<FFieldDecl>* Fields = nullptr;
		std::vector<std::string> Bases;
		std::string OwnerModule;
		const auto Arity = TypeParamArityFromLinkage(TypeName);
		const auto Find = [&](const auto& Declarations, const FDeclarationIndex* const Indices = nullptr)
		{
			const auto* Candidates = Indices ? &FindDeclarationCandidates(*Indices, Lookup) : nullptr;
			const auto Count = Candidates ? Candidates->size() : Declarations.size();
			for (std::size_t Index = 0; Index < Count; ++Index)
			{
				const auto& Declaration = Declarations[Candidates ? (*Candidates)[Index] : Index];
				if (GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, Declaration.Name) != Lookup || TemplateParameterNames(Declaration).size() != Arity)
				{
					continue;
				}
				Fields = &Declaration.Fields;
				OwnerModule = Declaration.ModulePath;
				using FDeclaration = std::decay_t<decltype(Declaration)>;
				if constexpr (std::is_same_v<FDeclaration, FClassDecl>)
				{
					Bases = Declaration.BaseTypeNames;
				}
				else if (!Declaration.BaseTypeName.empty())
				{
					Bases.push_back(Declaration.BaseTypeName);
				}
				return true;
			}
			return false;
		};
		if (!ActiveProgram || !(Find(ActiveProgram->Structs, &SourceStructIndices) || Find(ActiveProgram->Classes, &SourceClassIndices) || Find(BuiltinStructTemplates) || Find(BuiltinClassTemplates)))
		{
			ReportError("implicit constructor declaration is unavailable: " + TypeName, Location);
			return false;
		}
		std::unordered_map<std::string, std::string> Bindings;
		if (!TryBuildGenericOwnerSubst(TypeName, Bindings, Location)) return false;
		auto PreviousBindings = ActiveTypeSubst;
		struct FRestoreBindings
		{
			std::unordered_map<std::string, std::string>& Target;
			std::unordered_map<std::string, std::string>& Previous;
			~FRestoreBindings()
			{
				Target = std::move(Previous);
			}
		} Restore{ActiveTypeSubst, PreviousBindings};
		ActiveTypeSubst = std::move(Bindings);
		Bases.clear();
		for (const auto Base : Module->Types[TypeIndex].BaseTypeIndices)
		{
			Bases.push_back(Module->Types[Base].Name);
		}
		FConstructorDecl Constructor;
		Constructor.Access = EMemberAccess::Public;
		Constructor.bDefaulted = true;
		Constructor.Location = Location;
		Constructor.Body = std::make_unique<FBlockStmt>();
		Constructor.Body->Location = Location;
		// Publish a signature before descending into its subobjects.
		if (!EnsureFunctionStub(Key, "void", 0, true, TypeIndex, false, Location) || !CompileConstructor(TypeName, SimpleName, Constructor, *Fields, Bases, TypeIndex, OwnerModule, Location))
		{
			return false;
		}
		OutFunctionIndex = FindFunctionIndex(Key);
		return OutFunctionIndex != static_cast<std::uint16_t>(-1);
	}

	bool VeroScriptCompiler::EnsureDefaultValueConstruction(
	    const std::string& TypeName, const FSourceLocation& Location)
	{
		const auto Type = CanonicalTypeNameForCoercion(QualifyTypeName(
		    NormalizeTypeAlias(SubstituteTypeName(TypeName)), CurrentModulePath));
		std::string Element;
		std::uint32_t Length = 0;
		if (Runtime::ParseNativeArrayType(Type, Element, Length))
		{
			return EnsureDefaultValueConstruction(Element, Location);
		}
		if (!IsStructType(Type) || (ActiveBindings && ActiveBindings->FindType(Type))) return true;
		std::string Linkage;
		std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
		if (!EnsureStructTypeResolved(Type, Linkage, &TypeIndex, Location) || !RequireConcreteType(Linkage, Location))
		{
			return false;
		}
		if (UsesTrivialDefaultConstruction(TypeIndex)) return true;
		if (TypeHasUserConstructors(Linkage))
		{
			const FConstructorDecl* Constructor = nullptr;
			std::string Key;
			// Check at every use, including when another permitted use created the factory.
			if (!ResolveConstructorForCall(Linkage, {}, Constructor, Key, Location) || !Constructor) return false;
		}
		else
		{
			std::uint16_t ConstructorIndex = static_cast<std::uint16_t>(-1);
			if (!EnsureImplicitDefaultConstructor(Linkage, TypeIndex, Location, ConstructorIndex)) return false;
		}
		const std::string FactoryName = Linkage + std::string(DefaultConstructionFactorySuffix);
		if (Module->FunctionIndices.count(FactoryName) != 0) return true;
		const std::string OwnerModule = ResolveOwnerModulePath(Linkage);
		// Shared type descriptors carry public grants. Lexical array construction
		// binds any additional visibility at its own source location.
		const std::string Access = GenericTemplateBaseLinkage(Linkage) == "type"
		    ? std::string{}
		    : CurrentAccessFunctionLinkage;
		auto Call = std::make_unique<FCallExpr>(Linkage, std::vector<std::unique_ptr<FExpr>>{});
		Call->Location = Location;
		FBlockStmt Body;
		Body.Location = Location;
		auto Return = std::make_unique<FReturnStmt>(std::move(Call));
		Return->Location = Location;
		Body.Statements.push_back(std::move(Return));
		const std::vector<FFunctionParam> Parameters;
		auto PreviousBindings = ActiveTypeSubst;
		struct FRestoreBindings
		{
			std::unordered_map<std::string, std::string>& Target;
			std::unordered_map<std::string, std::string>& Previous;
			~FRestoreBindings()
			{
				Target = std::move(Previous);
			}
		} Restore{ActiveTypeSubst, PreviousBindings};
		ActiveTypeSubst.clear();
		return CompileFunction(FactoryName, Linkage, Parameters, Body, false, 0,
		    OwnerModule, Location, false, nullptr, false, nullptr, &Access);
	}

	bool VeroScriptCompiler::EmitDefaultInitializedValue(
	    FEmitter& Emitter, const std::string& TypeName, const FSourceLocation& Location)
	{
		const auto Type = CanonicalTypeNameForCoercion(QualifyTypeName(
		    NormalizeTypeAlias(SubstituteTypeName(TypeName)), CurrentModulePath));
		if (Runtime::IsNativeArrayTypeName(Type))
		{
			FArrayInitExpr Initializer({});
			Initializer.Location = Location;
			return CompileNativeArrayInit(Type, Initializer, Emitter, Location);
		}
		if (Runtime::IsDynamicArrayTypeName(Type) || Runtime::IsCommonContainerTypeName(Type) || IsStructType(Type) || Type == "std.memory.buffer" || Type == "std.memory.view" || Type == "std.memory.readonly_view")
		{
			FCallExpr Call(Type, {});
			Call.Location = Location;
			return CompileExpression(Call);
		}
		if (Type == "variant")
		{
			Emitter.EmitCallBuiltin(EBuiltinId::VariantEmpty, 0);
			return true;
		}
		if (Type == "string" || Type == "wstring")
		{
			FBytecodeConstant Value;
			Value.Kind = Type == "string" ? EConstantKind::Str : EConstantKind::WStr;
			Value.StringIndex = Type == "string" ? AddString("") : AddWString(u"");
			Emitter.EmitU16Operand(EOpcode::ConstStr, AddConstant(Value));
			return true;
		}
		if (Type == "bool")
		{
			Emitter.EmitU8Operand(EOpcode::ConstBool, 0);
			return true;
		}
		if (IsNumericTypeName(Type) || IsEnumTypeName(Type))
		{
			FBytecodeConstant Value;
			if (IsScopedEnumTypeName(Type))
			{
				Value.Kind = EConstantKind::Enum;
				Value.StringIndex = AddString(Runtime::EnumValueTypeName(*Module, Type));
			}
			else if (Type == "float")
			{
				Value.Kind = EConstantKind::F32;
			}
			else if (Type == "double")
			{
				Value.Kind = EConstantKind::F64;
			}
			else if (Type == "int64" || Type == "long")
			{
				Value.Kind = EConstantKind::I64;
			}
			else if (Type == "char")
			{
				Value.Kind = EConstantKind::I8;
			}
			else if (Type == "short")
			{
				Value.Kind = EConstantKind::I16;
			}
			else if (Type == "uint64" || Type == "unsigned long")
			{
				Value.Kind = EConstantKind::U64;
			}
			else if (Type == "unsigned char")
			{
				Value.Kind = EConstantKind::U8;
			}
			else if (Type == "unsigned short")
			{
				Value.Kind = EConstantKind::U16;
			}
			else if (Runtime::IsUnsignedTypeName(Type))
			{
				Value.Kind = EConstantKind::U32;
			}
			Emitter.EmitU16Operand(Type == "float" || Type == "double" ? EOpcode::ConstF64 : EOpcode::ConstI32,
			    AddConstant(Value));
			return true;
		}
		Emitter.Emit(EOpcode::ConstNull);
		return true;
	}

	bool VeroScriptCompiler::EmitImplicitAncestorDefaultCtors(
	    FEmitter& Emitter,
	    const std::string& TypeLinkageName,
	    const FSourceLocation& Location,
	    const bool bObjectOnStack)
	{
		const std::string Type = TypeLinkageName;
		const auto TypeIndex = FindRegisteredTypeIndex(Type);
		std::uint16_t ConstructorIndex = static_cast<std::uint16_t>(-1);
		if (TypeIndex == static_cast<std::uint16_t>(-1) || !EnsureImplicitDefaultConstructor(Type, TypeIndex, Location, ConstructorIndex))
		{
			return false;
		}
		Emitter.Emit(bObjectOnStack ? EOpcode::Dup : EOpcode::LoadThis);
		Emitter.EmitCallBoundMethod(ConstructorIndex, 0);
		Emitter.Emit(EOpcode::Pop);
		return true;
	}

	bool VeroScriptCompiler::ReportIncompatibleVirtualOverride(
	    const std::string& OwnerLinkageName,
	    const FMethodDecl& Method,
	    const std::string& OwnerModulePath,
	    const FSourceLocation& Location)
	{
		// A conversion's target is part of its name. Equal private alias spellings
		// in different declaration modules can denote different targets, which
		// introduce separate conversions rather than incompatible return types.
		if (Method.Name.rfind("operator conv ", 0) == 0) return false;
		std::vector<std::string> Queue = GetAstBaseTypeNames(OwnerLinkageName);
		std::unordered_set<std::string> Visited;
		for (std::size_t Qi = 0; Qi < Queue.size(); ++Qi)
		{
			const std::string CurrentLinkage = Queue[Qi];
			if (CurrentLinkage.empty() || !Visited.insert(CurrentLinkage).second)
			{
				continue;
			}
			const std::string LookupLinkage = [&]() -> std::string
			{
				const std::string TemplateBase = GenericTemplateBaseLinkage(CurrentLinkage);
				return TemplateBase.empty() ? CurrentLinkage : TemplateBase;
			}();
			for (const FStructDecl& StructDecl : ActiveProgram->Structs)
			{
				if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) != LookupLinkage)
				{
					continue;
				}
				for (const FMethodDecl& Candidate : StructDecl.Methods)
				{
					if (!Candidate.bVirtual || Candidate.Name != Method.Name || Candidate.Parameters.size() != Method.Parameters.size() || Candidate.bIsConst != Method.bIsConst)
					{
						continue;
					}
					bool bParamsMatch = true;
					for (std::size_t Index = 0; Index < Method.Parameters.size(); ++Index)
					{
						if (QualifyTypeName(
						        NormalizeTypeAlias(
						            Method.Parameters[Index].TypeName,
						            OwnerModulePath),
						        OwnerModulePath) != QualifyTypeName(NormalizeTypeAlias(Candidate.Parameters[Index].TypeName, StructDecl.ModulePath), StructDecl.ModulePath))
						{
							bParamsMatch = false;
							break;
						}
					}
					if (!bParamsMatch)
					{
						continue;
					}
					if (!MethodSignaturesMatch(
					        Method,
					        Candidate,
					        OwnerModulePath,
					        StructDecl.ModulePath))
					{
						ReportErrorId("diag.method_overrides_virtual_base_but_return_type_differs", Location, {Method.Name});
						return true;
					}
				}
				const std::vector<std::string> Bases = GetAstBaseTypeNames(CurrentLinkage);
				Queue.insert(Queue.end(), Bases.begin(), Bases.end());
				goto NextIncompatible;
			}
			for (const FClassDecl& ClassDecl : ActiveProgram->Classes)
			{
				if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) != LookupLinkage)
				{
					continue;
				}
				for (const FMethodDecl& Candidate : ClassDecl.Methods)
				{
					if (!Candidate.bVirtual || Candidate.Name != Method.Name || Candidate.Parameters.size() != Method.Parameters.size() || Candidate.bIsConst != Method.bIsConst)
					{
						continue;
					}
					bool bParamsMatch = true;
					for (std::size_t Index = 0; Index < Method.Parameters.size(); ++Index)
					{
						if (QualifyTypeName(
						        NormalizeTypeAlias(
						            Method.Parameters[Index].TypeName,
						            OwnerModulePath),
						        OwnerModulePath) != QualifyTypeName(NormalizeTypeAlias(Candidate.Parameters[Index].TypeName, ClassDecl.ModulePath), ClassDecl.ModulePath))
						{
							bParamsMatch = false;
							break;
						}
					}
					if (!bParamsMatch)
					{
						continue;
					}
					if (!MethodSignaturesMatch(
					        Method,
					        Candidate,
					        OwnerModulePath,
					        ClassDecl.ModulePath))
					{
						ReportErrorId("diag.method_overrides_virtual_base_but_return_type_differs", Location, {Method.Name});
						return true;
					}
				}
				const std::vector<std::string> Bases = GetAstBaseTypeNames(CurrentLinkage);
				Queue.insert(Queue.end(), Bases.begin(), Bases.end());
				goto NextIncompatible;
			}
		NextIncompatible:
			continue;
		}
		return false;
	}

	bool VeroScriptCompiler::EmitDefaultTypeConstruction(
	    FEmitter& Emitter,
	    const std::string& TypeLinkageName,
	    const std::uint16_t TypeIndex,
	    const FSourceLocation& Location)
	{
		if (!RequireConcreteType(TypeLinkageName, Location))
		{
			return false;
		}
		if (UsesTrivialDefaultConstruction(TypeIndex))
		{
			Emitter.EmitU16Operand(EOpcode::NewObject, TypeIndex);
			EmitReflectionScopeBinding(Emitter, TypeIndex);
			return true;
		}
		Emitter.EmitU16Operand(EOpcode::NewUninitializedObject, TypeIndex);
		if (!EmitImplicitAncestorDefaultCtors(Emitter, TypeLinkageName, Location)) return false;
		EmitReflectionScopeBinding(Emitter, TypeIndex);
		return true;
	}

	bool VeroScriptCompiler::AppendInheritedAndOwnFields(
	    FBytecodeTypeDesc& TypeDesc,
	    const std::string& OwnTypeName,
	    const std::vector<std::string>& BaseTypeNamesSimple,
	    const EScriptObjectKind ExpectedKind,
	    const std::vector<FFieldDecl>& OwnFields,
	    const std::string& OwnerModulePath,
	    const FSourceLocation& Location,
	    const std::vector<std::string>& InterfaceTypeNames,
	    const std::vector<EMemberAccess>& BaseAccess,
	    const bool bCompileFieldDefaults)
	{
		// Every layout builder registers its owner before resolving bases and
		// fields. Distinct types can exhaust the native stack without a cycle.
		if (TypesInProgress.size() > 64 || !HasCompilerTypeStackHeadroom())
		{
			ReportError("type resolution nesting limit exceeded", Location);
			return false;
		}
		struct FNamespaceRestore
		{
			std::string& Target;
			std::string Previous;
			~FNamespaceRestore()
			{
				Target = std::move(Previous);
			}
		} NamespaceRestore{CurrentNamespacePrefix, CurrentNamespacePrefix};
		// Field type expressions belong to their declaring type, including when
		// its layout is first requested from another namespace. Local declarations
		// retain the lexical context restored by scoped type instantiation.
		if (!TypeDesc.Scope.bLocal)
		{
			CurrentNamespacePrefix = NamespaceFromFunctionLinkage(OwnTypeName);
		}
		std::size_t BaseOrdinal = 0;
		for (const std::string& BaseAsWritten : BaseTypeNamesSimple)
		{
			const std::string BaseTypeNameSimple = NormalizeInheritanceTypeName(BaseAsWritten, OwnerModulePath);
			const EMemberAccess InheritanceAccess = BaseOrdinal < BaseAccess.size()
			    ? BaseAccess[BaseOrdinal]
			    : EMemberAccess::Private;
			++BaseOrdinal;
			if (BaseTypeNameSimple.empty())
			{
				continue;
			}

			if (ActiveBindings)
			{
				if (const FHostTypeDesc* const HostType = ActiveBindings->FindType(BaseTypeNameSimple))
				{
					if (ExpectedKind == EScriptObjectKind::Struct && HostType->Kind != EHostTypeKind::Struct)
					{
						ReportErrorId(
						    "diag.host_base_kind_mismatch",
						    Location,
						    {BaseTypeNameSimple, "struct"});
						return false;
					}
					if (ExpectedKind == EScriptObjectKind::Class && HostType->Kind != EHostTypeKind::Class)
					{
						ReportErrorId(
						    "diag.host_base_kind_mismatch",
						    Location,
						    {BaseTypeNameSimple, "class"});
						return false;
					}
					if (HostType->Kind != EHostTypeKind::Class && HostType->Kind != EHostTypeKind::Struct)
					{
						ReportErrorId(
						    "diag.host_base_must_be_class_or_struct",
						    Location,
						    {BaseTypeNameSimple});
						return false;
					}
					if (!HostType->bInheritable)
					{
						ReportErrorId(
						    "diag.cannot_inherit_from_non_inheritable_host_type",
						    Location,
						    {BaseTypeNameSimple});
						return false;
					}
					if (!TypeDesc.HostBaseTypeName.empty())
					{
						ReportErrorId(
						    "diag.cannot_mix_host_and_script_bases",
						    Location,
						    {BaseTypeNameSimple});
						return false;
					}
					if (!TypeDesc.BaseTypeIndices.empty())
					{
						ReportErrorId(
						    "diag.cannot_mix_host_and_script_bases",
						    Location,
						    {BaseTypeNameSimple});
						return false;
					}
					TypeDesc.HostBaseTypeName = HostType->Name;
					TypeDesc.HostBaseTypeId = HostType->Id.Value;
					continue;
				}
			}

			std::string GenericBase;
			std::vector<std::string> GenericArgs;
			if (SplitUserGenericTypeApp(BaseTypeNameSimple, GenericBase, GenericArgs))
			{
				const FStructDecl* const GenericStruct =
				    FindGenericStructDecl(GenericBase, OwnerModulePath, GenericArgs.size());
				const FClassDecl* const GenericClass =
				    FindGenericClassDecl(GenericBase, OwnerModulePath, GenericArgs.size());
				if (ExpectedKind == EScriptObjectKind::Struct)
				{
					if (!GenericStruct)
					{
						if (GenericClass)
						{
							ReportErrorId(
							    "diag.struct_can_only_inherit_from_struct_got_class",
							    Location,
							    {GenericBase});
						}
						else
						{
							ReportErrorId("diag.unknown_base_type", Location, {BaseTypeNameSimple});
						}
						return false;
					}
					if (GenericStruct->bFinal)
					{
						ReportErrorId(
						    "diag.cannot_inherit_from_final_type",
						    Location,
						    {BaseTypeNameSimple});
						return false;
					}
				}
				else
				{
					if (!GenericClass)
					{
						if (GenericStruct)
						{
							ReportErrorId(
							    "diag.class_can_only_inherit_from_class_got_struct",
							    Location,
							    {GenericBase});
						}
						else
						{
							ReportErrorId("diag.unknown_base_type", Location, {BaseTypeNameSimple});
						}
						return false;
					}
					if (GenericClass->bFinal)
					{
						ReportErrorId(
						    "diag.cannot_inherit_from_final_type",
						    Location,
						    {BaseTypeNameSimple});
						return false;
					}
				}
				if (GenericStruct && GenericStruct->bIsStatic)
				{
					ReportError("cannot inherit from static struct '" + GenericBase + "'", Location);
					return false;
				}
				if (GenericClass && GenericClass->bIsStatic)
				{
					ReportError("cannot inherit from static class '" + GenericBase + "'", Location);
					return false;
				}

				std::string ResolvedBaseLinkage;
				std::uint16_t BaseTypeIndex = static_cast<std::uint16_t>(-1);
				if (!EnsureStructTypeResolved(
				        BaseTypeNameSimple,
				        ResolvedBaseLinkage,
				        &BaseTypeIndex,
				        Location))
				{
					if (!HasError())
					{
						ReportErrorId("diag.unknown_base_type", Location, {BaseTypeNameSimple});
					}
					return false;
				}
				TypeDesc.BaseTypeIndices.push_back(BaseTypeIndex);
				TypeDesc.BaseInheritanceAccess.push_back(InheritanceAccess);
				const FBytecodeTypeDesc& BaseDesc = Module->Types[BaseTypeIndex];
				if (BaseDesc.Kind != ExpectedKind)
				{
					ReportErrorId("diag.base_type_kind_mismatch_for", Location, {BaseTypeNameSimple});
					return false;
				}
				if (!TypeDesc.HostBaseTypeName.empty())
				{
					ReportErrorId(
					    "diag.cannot_mix_host_and_script_bases",
					    Location,
					    {BaseTypeNameSimple});
					return false;
				}
				if (!BaseDesc.HostBaseTypeName.empty())
				{
					TypeDesc.HostBaseTypeName = BaseDesc.HostBaseTypeName;
					TypeDesc.HostBaseTypeId = BaseDesc.HostBaseTypeId;
				}
				for (const FBytecodeFieldDesc& BaseField : BaseDesc.Fields)
				{
					for (const FBytecodeFieldDesc& Existing : TypeDesc.Fields)
					{
						if (Existing.Name == BaseField.Name)
						{
							ReportError(
							    "field name collision with base type: " + BaseField.Name,
							    Location);
							return false;
						}
					}
					TypeDesc.Fields.push_back(BaseField);
				}
				continue;
			}

			const FStructDecl* BaseStruct = nullptr;
			const FClassDecl* BaseClass = nullptr;
			const FStructDecl* LocalStruct = nullptr;
			const FClassDecl* LocalClass = nullptr;
			for (const FStructDecl& StructDecl : ActiveProgram->Structs)
			{
				if (StructDecl.Name != BaseTypeNameSimple || !CanAccessSymbol(StructDecl.ModulePath, StructDecl.bExported, OwnerModulePath))
				{
					continue;
				}
				if (StructDecl.ModulePath == OwnerModulePath)
				{
					LocalStruct = &StructDecl;
					break;
				}
				if (!BaseStruct)
				{
					BaseStruct = &StructDecl;
				}
			}
			if (LocalStruct)
			{
				BaseStruct = LocalStruct;
			}
			for (const FClassDecl& ClassDecl : ActiveProgram->Classes)
			{
				if (ClassDecl.Name != BaseTypeNameSimple || !CanAccessSymbol(ClassDecl.ModulePath, ClassDecl.bExported, OwnerModulePath))
				{
					continue;
				}
				if (ClassDecl.ModulePath == OwnerModulePath)
				{
					LocalClass = &ClassDecl;
					break;
				}
				if (!BaseClass)
				{
					BaseClass = &ClassDecl;
				}
			}
			if (LocalClass)
			{
				BaseClass = LocalClass;
			}
			if (!BaseStruct && !BaseClass)
			{
				ReportErrorId("diag.unknown_base_type", Location, {BaseTypeNameSimple});
				return false;
			}
			if (!TypeDesc.HostBaseTypeName.empty())
			{
				ReportErrorId(
				    "diag.cannot_mix_host_and_script_bases",
				    Location,
				    {BaseTypeNameSimple});
				return false;
			}
			if (ExpectedKind == EScriptObjectKind::Struct)
			{
				if (!BaseStruct)
				{
					ReportErrorId(
					    "diag.struct_can_only_inherit_from_struct_got_class",
					    Location,
					    {BaseTypeNameSimple});
					return false;
				}
				if (BaseStruct->bFinal)
				{
					ReportErrorId(
					    "diag.cannot_inherit_from_final_type",
					    Location,
					    {BaseTypeNameSimple});
					return false;
				}
				if (BaseStruct->bIsStatic)
				{
					ReportError("cannot inherit from static struct '" + BaseTypeNameSimple + "'", Location);
					return false;
				}
				if (!TemplateParameterNames(*BaseStruct).empty())
				{
					ReportErrorId(
					    "diag.cannot_inherit_from_generic_type_provide_type_arguments",
					    Location,
					    {BaseTypeNameSimple});
					return false;
				}
			}
			else
			{
				if (!BaseClass)
				{
					ReportErrorId(
					    "diag.class_can_only_inherit_from_class_got_struct",
					    Location,
					    {BaseTypeNameSimple});
					return false;
				}
				if (BaseClass->bFinal)
				{
					ReportErrorId(
					    "diag.cannot_inherit_from_final_type",
					    Location,
					    {BaseTypeNameSimple});
					return false;
				}
				if (BaseClass->bIsStatic)
				{
					ReportError("cannot inherit from static class '" + BaseTypeNameSimple + "'", Location);
					return false;
				}
				if (!TemplateParameterNames(*BaseClass).empty())
				{
					ReportErrorId(
					    "diag.cannot_inherit_from_generic_type_provide_type_arguments",
					    Location,
					    {BaseTypeNameSimple});
					return false;
				}
			}

			const std::string BaseLinkageName = BaseStruct
			    ? GetSymbolLinkageName(BaseStruct->ModulePath, BaseStruct->bExported, BaseStruct->Name)
			    : GetSymbolLinkageName(BaseClass->ModulePath, BaseClass->bExported, BaseClass->Name);
			const std::uint16_t BaseTypeIndex = FindOrAddType(BaseLinkageName);
			if (BaseTypeIndex == static_cast<std::uint16_t>(-1))
			{
				return false;
			}
			TypeDesc.BaseTypeIndices.push_back(BaseTypeIndex);
			TypeDesc.BaseInheritanceAccess.push_back(InheritanceAccess);
			const FBytecodeTypeDesc& BaseDesc = Module->Types[BaseTypeIndex];
			if (BaseDesc.bIsStatic)
			{
				ReportError(
				    "cannot inherit from static " + std::string(BaseDesc.Kind == EScriptObjectKind::Struct ? "struct '" : "class '") + BaseTypeNameSimple + "'",
				    Location);
				return false;
			}
			if (BaseDesc.Kind != ExpectedKind)
			{
				ReportErrorId("diag.base_type_kind_mismatch_for", Location, {BaseTypeNameSimple});
				return false;
			}
			if (!BaseDesc.HostBaseTypeName.empty())
			{
				if (!TypeDesc.HostBaseTypeName.empty() && TypeDesc.HostBaseTypeName != BaseDesc.HostBaseTypeName)
				{
					ReportErrorId(
					    "diag.cannot_mix_host_and_script_bases",
					    Location,
					    {BaseDesc.HostBaseTypeName});
					return false;
				}
				TypeDesc.HostBaseTypeName = BaseDesc.HostBaseTypeName;
				TypeDesc.HostBaseTypeId = BaseDesc.HostBaseTypeId;
			}
			for (const FBytecodeFieldDesc& BaseField : BaseDesc.Fields)
			{
				for (const FBytecodeFieldDesc& Existing : TypeDesc.Fields)
				{
					if (Existing.Name == BaseField.Name)
					{
						ReportError(
						    "field name collision with base type: " + BaseField.Name,
						    Location);
						return false;
					}
				}
				TypeDesc.Fields.push_back(BaseField);
			}
		}

		TypeDesc.OwnFieldStartIndex = static_cast<std::uint16_t>(TypeDesc.Fields.size());

		if (!InterfaceTypeNames.empty() && !AppendFlattenedInterfaceFields(TypeDesc, InterfaceTypeNames, OwnerModulePath, Location))
		{
			return false;
		}

		for (const FFieldDecl& Field : OwnFields)
		{
			// C++-style static data members are shared globals (Type.Name), never part
			// of the per-instance object layout — but keep DeclMeta for embed reflection.
			if (Field.bIsStatic)
			{
				if (!ValidateFieldTypeName(Field.TypeName, Field.Location))
				{
					return false;
				}
				FBytecodeFieldDesc StaticDesc;
				StaticDesc.Name = Field.Name;
				const std::string DeclaredType = SubstituteTypeName(Field.TypeName);
				StaticDesc.DeclaredTypeName = QualifyTypeName(
				    DeclaredType, OwnerModulePath);
				StaticDesc.TypeName = QualifyTypeName(
				    NormalizeTypeAlias(StaticDesc.DeclaredTypeName, OwnerModulePath),
				    OwnerModulePath);
				StaticDesc.bIsConst = Field.bIsConst;
				StaticDesc.bConstexpr = Field.bConstexpr;
				StaticDesc.DeclMeta = Field.DeclMeta;
				TypeDesc.StaticFields.push_back(std::move(StaticDesc));
				continue;
			}
			if (!ValidateFieldTypeName(Field.TypeName, Field.Location))
			{
				return false;
			}
			if (!TypeDesc.HostBaseTypeName.empty() && ActiveBindings)
			{
				if (const FHostTypeDesc* const HostType =
				        ActiveBindings->FindType(TypeDesc.HostBaseTypeName))
				{
					for (const FHostMemberDesc& Member : HostType->Members)
					{
						if (Member.Name != Field.Name)
						{
							continue;
						}
						if (Member.Kind == EHostMemberKind::Field || Member.Kind == EHostMemberKind::PropertyGetter || Member.Kind == EHostMemberKind::PropertySetter || Member.Kind == EHostMemberKind::Method)
						{
							ReportErrorId(
							    "diag.field_collides_with_host_member",
							    Field.Location,
							    {Field.Name, TypeDesc.HostBaseTypeName});
							return false;
						}
					}
				}
			}
			for (const FBytecodeFieldDesc& Existing : TypeDesc.Fields)
			{
				if (Existing.Name == Field.Name)
				{
					ReportError(
					    "field name collision with base type: " + Field.Name,
					    Field.Location);
					return false;
				}
			}
			FBytecodeFieldDesc FieldDesc;
			FieldDesc.Name = Field.Name;
			const std::string DeclaredType = SubstituteTypeName(Field.TypeName);
			FieldDesc.DeclaredTypeName = QualifyTypeName(
			    DeclaredType, OwnerModulePath);
			FieldDesc.TypeName = QualifyTypeName(
			    NormalizeTypeAlias(FieldDesc.DeclaredTypeName, OwnerModulePath),
			    OwnerModulePath);
			if (!Field.DefaultValue && !Field.bAllowUninitializedHostStruct && ActiveBindings)
			{
				const auto RequireHostStructInit = [&](const std::string& HostTypeName) -> bool
				{
					const FHostTypeDesc* const HostType = ActiveBindings->FindType(HostTypeName);
					if (!HostType || HostType->Kind != EHostTypeKind::Struct ||
					    Memory::IsTypeName(HostTypeName))
					{
						return true;
					}
					ReportErrorId(
					    "diag.host_struct_field_requires_initializer",
					    Field.Location,
					    {Field.Name, HostTypeName});
					return false;
				};
				std::string ElementType;
				std::uint32_t ArrayLength = 0;
				const std::string FieldTypeSimple = SubstituteTypeName(Field.TypeName);
				if (Runtime::IsNativeArrayTypeName(FieldTypeSimple) && Runtime::ParseNativeArrayType(FieldTypeSimple, ElementType, ArrayLength))
				{
					if (!RequireHostStructInit(ElementType))
					{
						return false;
					}
				}
				else if (!RequireHostStructInit(FieldTypeSimple))
				{
					return false;
				}
			}
			const auto PreviousOwner = CurrentOwnerTypeLinkage;
			const auto PreviousAccess = CurrentAccessFunctionLinkage;
			CurrentOwnerTypeLinkage = OwnTypeName;
			CurrentAccessFunctionLinkage = OwnTypeName + "::@initializer";
			FieldDesc.bIsMutable = Field.bIsMutable;
			FieldDesc.bIsConst = Field.bIsConst;
			FieldDesc.bConstexpr = Field.bConstexpr;
			FieldDesc.Access = Field.Access;
			const bool FieldCompiled = !bCompileFieldDefaults || CompileFieldDefault(Field, FieldDesc);
			CurrentOwnerTypeLinkage = PreviousOwner;
			CurrentAccessFunctionLinkage = PreviousAccess;
			if (!FieldCompiled)
			{
				return false;
			}
			FieldDesc.DeclMeta = Field.DeclMeta;
			TypeDesc.Fields.push_back(std::move(FieldDesc));
		}
		return true;
	}
}
