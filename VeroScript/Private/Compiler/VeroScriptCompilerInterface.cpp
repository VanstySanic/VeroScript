// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptCompilerTemplateSelection.h"

#include "VeroScriptAst.h"
#include "TypeSupport/VeroScriptRuntimeTypeScope.h"
#include "VeroScriptBuiltins.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleVisibility.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <tuple>
#include <unordered_set>

namespace VeroScript
{
	bool VeroScriptCompiler::IsInterfaceTypeName(const std::string& TypeName) const
	{
		if (Runtime::IsPrimitiveValueTypeName(TypeName) || TypeName == "void")
		{
			return false;
		}
		if (!ActiveProgram || TypeName.empty())
		{
			return false;
		}
		if (Module)
		{
			for (const FBytecodeInterfaceDesc& InterfaceDesc : Module->Interfaces)
			{
				if (InterfaceDesc.Name == TypeName)
				{
					return true;
				}
			}
		}
		std::string LookupName = TypeName;
		std::string GenericBase;
		std::vector<std::string> GenericArgs;
		if (SplitUserGenericTypeApp(TypeName, GenericBase, GenericArgs))
		{
			LookupName = GenericBase;
		}
		else
		{
			const std::string TemplateBase = GenericTemplateBaseLinkage(TypeName);
			if (!TemplateBase.empty())
			{
				LookupName = TemplateBase;
			}
		}
		std::string LinkageName = ResolveTypeLinkageName(LookupName, CurrentModulePath);
		if (LinkageName.empty())
		{
			LinkageName = LookupName;
		}
		for (const FInterfaceDecl& InterfaceDecl : ActiveProgram->Interfaces)
		{
			if (GetSymbolLinkageName(InterfaceDecl.ModulePath, InterfaceDecl.bExported, InterfaceDecl.Name) == LinkageName || InterfaceDecl.Name == LookupName || InterfaceDecl.Name == TypeName)
			{
				return true;
			}
		}
		return false;
	}

	std::uint16_t VeroScriptCompiler::FindInterfaceIndex(const std::string& InterfaceLinkageName) const
	{
		if (!Module)
		{
			return static_cast<std::uint16_t>(-1);
		}
		for (std::size_t Index = 0; Index < Module->Interfaces.size(); ++Index)
		{
			if (Module->Interfaces[Index].Name == InterfaceLinkageName)
			{
				return static_cast<std::uint16_t>(Index);
			}
		}
		return static_cast<std::uint16_t>(-1);
	}

	bool VeroScriptCompiler::ResolveInheritanceInterfaces(
	    const std::vector<std::string>& InterfaceTypeNames,
	    const std::string& OwnerModulePath,
	    const FSourceLocation& Location,
	    std::vector<std::uint16_t>& OutInterfaceIndices)
	{
		OutInterfaceIndices.clear();
		const std::string PreviousModulePath = CurrentModulePath;
		CurrentModulePath = OwnerModulePath;
		for (const std::string& InterfaceName : InterfaceTypeNames)
		{
			std::string Linkage;
			std::uint16_t Index = static_cast<std::uint16_t>(-1);
			if (!EnsureInterfaceTypeResolved(
			        NormalizeInheritanceTypeName(InterfaceName, OwnerModulePath), Linkage, &Index, Location))
			{
				CurrentModulePath = PreviousModulePath;
				if (!bHasError)
				{
					ReportErrorId("diag.unknown_interface_type", Location, {InterfaceName});
				}
				return false;
			}
			if (Index == static_cast<std::uint16_t>(-1))
			{
				Index = FindInterfaceIndex(Linkage);
			}
			if (Index == static_cast<std::uint16_t>(-1))
			{
				CurrentModulePath = PreviousModulePath;
				ReportErrorId("diag.unknown_interface_type", Location, {InterfaceName});
				return false;
			}
			OutInterfaceIndices.push_back(Index);
		}
		CurrentModulePath = PreviousModulePath;
		return true;
	}

	bool VeroScriptCompiler::TypeProvidesMethod(
	    const std::string& TypeLinkageName,
	    const FMethodDecl& Required,
	    const std::string& RequiredModulePath) const
	{
		static const std::unordered_map<std::string, std::string> EmptySubst;
		return TypeProvidesMethod(TypeLinkageName, Required, RequiredModulePath, EmptySubst);
	}

	bool VeroScriptCompiler::TypeProvidesMethod(
	    const std::string& TypeLinkageName,
	    const FMethodDecl& Required,
	    const std::string& RequiredModulePath,
	    const std::unordered_map<std::string, std::string>& RequiredSubst) const
	{
		std::unordered_map<std::string, std::string> OwnerSubst;
		TryBuildGenericOwnerSubst(TypeLinkageName, OwnerSubst, FSourceLocation{});

		std::vector<std::string> Queue{TypeLinkageName};
		std::unordered_set<std::string> Visited;
		for (std::size_t Qi = 0; Qi < Queue.size(); ++Qi)
		{
			const std::string CurrentLinkage = Queue[Qi];
			if (CurrentLinkage.empty() || !Visited.insert(CurrentLinkage).second)
			{
				continue;
			}
			std::string LookupLinkage = CurrentLinkage;
			const std::string TemplateBase = GenericTemplateBaseLinkage(CurrentLinkage);
			if (!TemplateBase.empty())
			{
				LookupLinkage = TemplateBase;
			}
			const std::size_t WantedArity = TypeParamArityFromLinkage(CurrentLinkage);
			for (const FStructDecl& StructDecl : ActiveProgram->Structs)
			{
				if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) != LookupLinkage || TemplateParameterNames(StructDecl).size() != WantedArity)
				{
					continue;
				}
				for (const FMethodDecl& Method : StructDecl.Methods)
				{
					if (!Method.bDeleted && !Method.bPureVirtual && MethodSignaturesMatch(Method, Required, StructDecl.ModulePath, RequiredModulePath, OwnerSubst, RequiredSubst))
					{
						return true;
					}
				}
				// Walk mangled CurrentLinkage so B<T> becomes B#… under OwnerSubst /
				// Module BaseTypeIndices — not the open template LookupLinkage.
				const std::vector<std::string> Bases = GetAstBaseTypeNames(CurrentLinkage);
				Queue.insert(Queue.end(), Bases.begin(), Bases.end());
				goto NextProvide;
			}
			for (const FClassDecl& ClassDecl : ActiveProgram->Classes)
			{
				if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) != LookupLinkage || TemplateParameterNames(ClassDecl).size() != WantedArity)
				{
					continue;
				}
				for (const FMethodDecl& Method : ClassDecl.Methods)
				{
					if (!Method.bDeleted && !Method.bPureVirtual && MethodSignaturesMatch(Method, Required, ClassDecl.ModulePath, RequiredModulePath, OwnerSubst, RequiredSubst))
					{
						return true;
					}
				}
				const std::vector<std::string> Bases = GetAstBaseTypeNames(CurrentLinkage);
				Queue.insert(Queue.end(), Bases.begin(), Bases.end());
				goto NextProvide;
			}
		NextProvide:
			continue;
		}
		return false;
	}

	bool VeroScriptCompiler::ShadowsNonVirtualInterfaceDefault(
	    const std::string& TypeLinkageName,
	    const FMethodDecl& Required,
	    const std::string& RequiredModulePath,
	    const std::unordered_map<std::string, std::string>& RequiredSubst,
	    const std::string& IfaceLinkage) const
	{
		if (!ActiveProgram)
		{
			return false;
		}
		// Does type X (class/struct linkage) implement IfaceLinkage, directly or via a
		// base class / parent interface? Compare on template base so generic instances
		// (`IFoo#int`) match their template.
		const std::string WantedIfaceTemplate = [&]() -> std::string
		{
			const std::string Base = GenericTemplateBaseLinkage(IfaceLinkage);
			return Base.empty() ? IfaceLinkage : Base;
		}();
		const auto SameIface = [&](const std::string& Linkage) -> bool
		{
			const std::string Template = GenericTemplateBaseLinkage(Linkage);
			return (Template.empty() ? Linkage : Template) == WantedIfaceTemplate;
		};
		const auto TypeImplementsIface = [&](const std::string& XLinkage) -> bool
		{
			std::vector<std::string> IfaceQueue;
			std::unordered_set<std::string> IfaceVisited;
			// Seed with X's own + all base classes' implemented interfaces.
			std::vector<std::string> TypeQueue{XLinkage};
			std::unordered_set<std::string> TypeVisited;
			for (std::size_t Ti = 0; Ti < TypeQueue.size(); ++Ti)
			{
				const std::string Current = TypeQueue[Ti];
				if (Current.empty() || !TypeVisited.insert(Current).second)
				{
					continue;
				}
				const std::vector<std::string> Ifaces =
				    GetAstImplementedInterfaceLinkages(Current);
				IfaceQueue.insert(IfaceQueue.end(), Ifaces.begin(), Ifaces.end());
				const std::vector<std::string> Bases = GetAstBaseTypeNames(Current);
				TypeQueue.insert(TypeQueue.end(), Bases.begin(), Bases.end());
			}
			// Walk the interface closure (each interface + its parents).
			for (std::size_t Qi = 0; Qi < IfaceQueue.size(); ++Qi)
			{
				const std::string Current = IfaceQueue[Qi];
				if (Current.empty() || !IfaceVisited.insert(Current).second)
				{
					continue;
				}
				if (SameIface(Current))
				{
					return true;
				}
				const FInterfaceDecl* const Decl = FindInterfaceDeclByLinkage(Current);
				if (!Decl)
				{
					continue;
				}
				std::unordered_map<std::string, std::string> Subst;
				BuildInterfaceTypeSubst(Current, Subst);
				for (const std::string& ParentName : Decl->BaseInterfaceNames)
				{
					const std::string ParentLinkage = ResolveInheritanceBaseLinkage(
					    SubstituteTypeNameWith(ParentName, Subst),
					    Decl->ModulePath);
					IfaceQueue.push_back(ParentLinkage.empty() ? ParentName : ParentLinkage);
				}
			}
			return false;
		};

		std::unordered_map<std::string, std::string> OwnerSubst;
		TryBuildGenericOwnerSubst(TypeLinkageName, OwnerSubst, FSourceLocation{});

		bool bImplementerDeclares = false;
		bool bIndependentDeclares = false;
		std::vector<std::string> Queue{TypeLinkageName};
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
				const std::string Base = GenericTemplateBaseLinkage(CurrentLinkage);
				return Base.empty() ? CurrentLinkage : Base;
			}();
			const std::size_t WantedArity = TypeParamArityFromLinkage(CurrentLinkage);
			const auto ScanMethods = [&](const std::vector<FMethodDecl>& Methods,
			                             const std::string& DeclModulePath) -> bool
			{
				for (const FMethodDecl& Method : Methods)
				{
					if (MethodSignaturesMatch(
					        Method,
					        Required,
					        DeclModulePath,
					        RequiredModulePath,
					        OwnerSubst,
					        RequiredSubst))
					{
						return true;
					}
				}
				return false;
			};
			bool bMatched = false;
			for (const FStructDecl& StructDecl : ActiveProgram->Structs)
			{
				if (GetSymbolLinkageName(StructDecl.ModulePath, StructDecl.bExported, StructDecl.Name) != LookupLinkage || TemplateParameterNames(StructDecl).size() != WantedArity)
				{
					continue;
				}
				bMatched = ScanMethods(StructDecl.Methods, StructDecl.ModulePath);
				const std::vector<std::string> Bases = GetAstBaseTypeNames(CurrentLinkage);
				Queue.insert(Queue.end(), Bases.begin(), Bases.end());
				break;
			}
			if (!bMatched)
			{
				for (const FClassDecl& ClassDecl : ActiveProgram->Classes)
				{
					if (GetSymbolLinkageName(ClassDecl.ModulePath, ClassDecl.bExported, ClassDecl.Name) != LookupLinkage || TemplateParameterNames(ClassDecl).size() != WantedArity)
					{
						continue;
					}
					bMatched = ScanMethods(ClassDecl.Methods, ClassDecl.ModulePath);
					const std::vector<std::string> Bases = GetAstBaseTypeNames(CurrentLinkage);
					Queue.insert(Queue.end(), Bases.begin(), Bases.end());
					break;
				}
			}
			if (bMatched)
			{
				if (TypeImplementsIface(CurrentLinkage))
				{
					bImplementerDeclares = true;
				}
				else
				{
					bIndependentDeclares = true;
				}
			}
		}
		return bImplementerDeclares && !bIndependentDeclares;
	}

	bool VeroScriptCompiler::VerifyInterfaceConformance(
	    const std::string& TypeLinkageName,
	    const std::vector<std::string>& InterfaceTypeNames,
	    const std::vector<std::uint16_t>& ImplementedInterfaceIndices,
	    const std::string& OwnerModulePath,
	    const FSourceLocation& Location)
	{
		// Ambiguity across sibling interfaces (not parent/child of each other): same
		// method name/arity from two listed/inherited interfaces without a type override.
		{
			const auto IsIfaceSubtypeOf = [this](
			                                  const std::string& SourceLinkage,
			                                  const std::string& TargetLinkage) -> bool
			{
				if (SourceLinkage.empty() || TargetLinkage.empty())
				{
					return false;
				}
				if (SourceLinkage == TargetLinkage)
				{
					return true;
				}
				// Prefer bytecode parent links (correct for generic interface instances).
				if (Module)
				{
					const std::uint16_t SourceIndex = FindInterfaceIndex(SourceLinkage);
					const std::uint16_t TargetIndex = FindInterfaceIndex(TargetLinkage);
					if (SourceIndex != static_cast<std::uint16_t>(-1) && TargetIndex != static_cast<std::uint16_t>(-1))
					{
						std::vector<std::uint16_t> IfaceQueue{SourceIndex};
						std::unordered_set<std::uint16_t> Visited;
						for (std::size_t Qi = 0; Qi < IfaceQueue.size(); ++Qi)
						{
							const std::uint16_t Walk = IfaceQueue[Qi];
							if (Walk >= Module->Interfaces.size() || !Visited.insert(Walk).second)
							{
								continue;
							}
							if (Walk == TargetIndex)
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
				std::vector<std::string> AstQueue{SourceLinkage};
				std::unordered_set<std::string> Visited;
				for (std::size_t Qi = 0; Qi < AstQueue.size(); ++Qi)
				{
					const std::string Current = AstQueue[Qi];
					if (Current.empty() || !Visited.insert(Current).second)
					{
						continue;
					}
					if (Current == TargetLinkage)
					{
						return true;
					}
					const FInterfaceDecl* const Decl = FindInterfaceDeclByLinkage(Current);
					if (!Decl || Decl->BaseInterfaceNames.empty())
					{
						continue;
					}
					std::unordered_map<std::string, std::string> Subst;
					BuildInterfaceTypeSubst(Current, Subst);
					for (const std::string& ParentName : Decl->BaseInterfaceNames)
					{
						const std::string ParentLinkage = ResolveInheritanceBaseLinkage(
						    SubstituteTypeNameWith(ParentName, Subst),
						    Decl->ModulePath);
						if (!ParentLinkage.empty())
						{
							AstQueue.push_back(ParentLinkage);
						}
					}
				}
				return false;
			};

			struct FMethodKey
			{
				bool operator==(const FMethodKey& Other) const
				{
					return Name == Other.Name && ParamSuffix == Other.ParamSuffix && bIsConst == Other.bIsConst;
				}

				std::string Name;
				std::string ParamSuffix;
				bool bIsConst = false;
			};
			struct FMethodKeyHash
			{
				std::size_t operator()(const FMethodKey& Key) const
				{
					return std::hash<std::string>{}(Key.Name) ^ (std::hash<std::string>{}(Key.ParamSuffix) * 2654435761u) ^ (Key.bIsConst ? static_cast<std::size_t>(0x9e3779b97f4a7c15ull) : std::size_t{0});
				}
			};

			std::vector<std::string> ListedLinkages;
			std::vector<std::unordered_set<FMethodKey, FMethodKeyHash>> ListedMethods;
			std::vector<std::unordered_map<FMethodKey, std::pair<std::string, const FMethodDecl*>, FMethodKeyHash>>
			    ListedDecls;
			ListedLinkages.reserve(ImplementedInterfaceIndices.size());
			ListedMethods.reserve(ImplementedInterfaceIndices.size());
			ListedDecls.reserve(ImplementedInterfaceIndices.size());
			for (const std::uint16_t IfaceIndex : ImplementedInterfaceIndices)
			{
				if (!Module || IfaceIndex >= Module->Interfaces.size())
				{
					continue;
				}
				const std::string& IfaceLinkage = Module->Interfaces[IfaceIndex].Name;
				std::vector<std::pair<std::string, const FMethodDecl*>> Flattened;
				std::unordered_set<std::string> VisitedInterfaces;
				if (!CollectFlattenedInterfaceMethods(IfaceLinkage, Flattened, VisitedInterfaces))
				{
					if (!bHasError)
					{
						ReportErrorId(
						    "diag.unknown_interface_type",
						    Location,
						    {DisplayTypeName(IfaceLinkage)});
					}
					return false;
				}
				std::unordered_set<FMethodKey, FMethodKeyHash> Keys;
				std::unordered_map<FMethodKey, std::pair<std::string, const FMethodDecl*>, FMethodKeyHash>
				    Decls;
				for (const auto& Entry : Flattened)
				{
					std::unordered_map<std::string, std::string> EntrySubst;
					BuildInterfaceTypeSubst(Entry.first, EntrySubst);
					const FInterfaceDecl* const EntryIface = FindInterfaceDeclByLinkage(Entry.first);
					const std::string EntryModule =
					    EntryIface ? EntryIface->ModulePath : OwnerModulePath;
					// A non-virtual interface default must not be shadowed by an own method
					// of the implementing type (or a subclass): dispatch would then depend
					// on the static reference type (interface ref -> default body, concrete
					// ref -> own method). Require `virtual` on the interface method to allow a
					// real override, or rename. Covers directly-listed AND inherited interfaces
					// (ImplementedInterfaceIndices is the flattened implemented set). A method
					// inherited from an interface-independent base class (e.g. `class D : B1,
					// IFoo` where B1 supplies it) is a legitimate implementation, not a shadow.
					if (Entry.second && Entry.second->Body && !Entry.second->bVirtual && ShadowsNonVirtualInterfaceDefault(TypeLinkageName, *Entry.second, EntryModule, EntrySubst, Entry.first))
					{
						ReportErrorId(
						    "diag.cannot_shadow_non_virtual_interface_method",
						    Location,
						    {Entry.second->Name});
						return false;
					}
					if (Entry.second && Entry.second->Body && Entry.second->bVirtual && Entry.second->bFinal && TypeProvidesMethod(TypeLinkageName, *Entry.second, EntryModule, EntrySubst))
					{
						ReportErrorId(
						    "diag.cannot_override_final_method",
						    Location,
						    {Entry.second->Name});
						return false;
					}
					if (!Entry.second)
					{
						continue;
					}
					const FMethodKey Key{
					    Entry.second->Name,
					    EncodeMethodParamSuffix(
					        CanonicalMethodParamTypeNames(*Entry.second, EntryModule, EntrySubst)),
					    Entry.second->bIsConst};
					Keys.insert(Key);
					Decls.emplace(Key, Entry);
				}
				ListedLinkages.push_back(IfaceLinkage);
				ListedMethods.push_back(std::move(Keys));
				ListedDecls.push_back(std::move(Decls));
			}

			for (std::size_t Left = 0; Left < ListedLinkages.size(); ++Left)
			{
				for (std::size_t Right = Left + 1; Right < ListedLinkages.size(); ++Right)
				{
					if (IsIfaceSubtypeOf(ListedLinkages[Left], ListedLinkages[Right]) || IsIfaceSubtypeOf(ListedLinkages[Right], ListedLinkages[Left]))
					{
						continue;
					}
					for (const FMethodKey& Key : ListedMethods[Left])
					{
						if (ListedMethods[Right].count(Key) == 0)
						{
							continue;
						}
						const auto& SampleEntry = ListedDecls[Left][Key];
						const FMethodDecl* const Sample = SampleEntry.second;
						std::unordered_map<std::string, std::string> SampleSubst;
						BuildInterfaceTypeSubst(SampleEntry.first, SampleSubst);
						const FInterfaceDecl* const SampleIface =
						    FindInterfaceDeclByLinkage(SampleEntry.first);
						if (Sample && SampleIface && TypeProvidesMethod(TypeLinkageName, *Sample, SampleIface->ModulePath, SampleSubst))
						{
							continue;
						}
						ReportErrorId(
						    "diag.ambiguous_interface_method_on_type_from_and_override_it_to_disambiguate",
						    Location,
						    {
						        Key.Name,
						        DisplayTypeName(TypeLinkageName),
						        DisplayTypeName(ListedLinkages[Left]),
						        DisplayTypeName(ListedLinkages[Right]),
						    });
						return false;
					}
				}
			}
		}

		for (const std::string& InterfaceName : InterfaceTypeNames)
		{
			std::string IfaceLinkage;
			if (!EnsureInterfaceTypeResolved(
			        NormalizeInheritanceTypeName(InterfaceName, OwnerModulePath),
			        IfaceLinkage,
			        nullptr,
			        Location))
			{
				if (!bHasError)
				{
					ReportErrorId("diag.unknown_interface_type", Location, {InterfaceName});
				}
				return false;
			}
			std::vector<std::pair<std::string, const FMethodDecl*>> Flattened;
			std::unordered_set<std::string> VisitedInterfaces;
			if (!CollectFlattenedInterfaceMethods(IfaceLinkage, Flattened, VisitedInterfaces))
			{
				if (!bHasError)
				{
					ReportErrorId("diag.unknown_interface_type", Location, {InterfaceName});
				}
				return false;
			}
			for (const auto& Entry : Flattened)
			{
				const std::string& DeclaringLinkage = Entry.first;
				const FMethodDecl* const Method = Entry.second;
				const FInterfaceDecl* const DeclaringIface =
				    FindInterfaceDeclByLinkage(DeclaringLinkage);
				if (!DeclaringIface || !Method)
				{
					ReportErrorId("diag.unknown_interface_type", Location, {InterfaceName});
					return false;
				}
				if (Method->Body)
				{
					// Default methods (virtual or non-virtual) need no implementation.
					// Non-virtual-default shadowing is rejected in the ImplementedInterfaceIndices
					// pass above (covers directly-listed AND inherited interfaces).
					continue;
				}
				std::unordered_map<std::string, std::string> RequiredSubst;
				BuildInterfaceTypeSubst(DeclaringLinkage, RequiredSubst);
				if (!TypeProvidesMethod(
				        TypeLinkageName,
				        *Method,
				        DeclaringIface->ModulePath,
				        RequiredSubst))
				{
					// The implementing type remains abstract. Construction is rejected by
					// RequireConcreteType and by the VM's type-level abstract flag.
					continue;
				}
			}
		}
		return true;
	}

	bool VeroScriptCompiler::CollectFlattenedInterfaceMethods(
	    const std::string& InterfaceLinkageName,
	    std::vector<std::pair<std::string, const FMethodDecl*>>& OutMethods,
	    std::unordered_set<std::string>& VisitedInterfaces,
	    const FSourceLocation* ConflictLocation,
	    std::vector<FDeferredIfaceDefaultConflict>* DeferredDefaultConflicts) const
	{
		// Already flattened via another parent path (interface diamond): skip, not an error.
		if (!VisitedInterfaces.insert(InterfaceLinkageName).second)
		{
			return true;
		}
		const FInterfaceDecl* const InterfaceDecl = FindInterfaceDeclByLinkage(InterfaceLinkageName);
		if (!InterfaceDecl)
		{
			return false;
		}
		const auto IsIfaceAncestorOf = [&](const std::string& Ancestor, const std::string& Descendant) -> bool
		{
			if (Ancestor.empty() || Descendant.empty())
			{
				return false;
			}
			if (Ancestor == Descendant)
			{
				return true;
			}
			std::vector<std::string> Queue{Descendant};
			std::unordered_set<std::string> Seen;
			for (std::size_t Qi = 0; Qi < Queue.size(); ++Qi)
			{
				const std::string Current = Queue[Qi];
				if (!Seen.insert(Current).second)
				{
					continue;
				}
				if (Current == Ancestor)
				{
					return true;
				}
				if (Module)
				{
					const std::uint16_t Idx = FindInterfaceIndex(Current);
					if (Idx != static_cast<std::uint16_t>(-1) && Idx < Module->Interfaces.size())
					{
						for (const std::uint16_t ParentIdx :
						    Module->Interfaces[Idx].BaseInterfaceIndices)
						{
							if (ParentIdx < Module->Interfaces.size())
							{
								Queue.push_back(Module->Interfaces[ParentIdx].Name);
							}
						}
						continue;
					}
				}
				const FInterfaceDecl* const Decl = FindInterfaceDeclByLinkage(Current);
				if (!Decl)
				{
					continue;
				}
				std::unordered_map<std::string, std::string> LocalSubst;
				BuildInterfaceTypeSubst(Current, LocalSubst);
				for (const std::string& ParentName : Decl->BaseInterfaceNames)
				{
					const std::string ParentLinkage = ResolveInheritanceBaseLinkage(
					    SubstituteTypeNameWith(ParentName, LocalSubst),
					    Decl->ModulePath);
					if (!ParentLinkage.empty())
					{
						Queue.push_back(ParentLinkage);
					}
				}
			}
			return false;
		};
		std::vector<FDeferredIfaceDefaultConflict> LocalDeferred;
		std::vector<FDeferredIfaceDefaultConflict>* const ConflictSink =
		    DeferredDefaultConflicts ? DeferredDefaultConflicts : &LocalDeferred;
		const FSourceLocation& DiagLocation =
		    ConflictLocation ? *ConflictLocation : InterfaceDecl->Location;
		std::unordered_map<std::string, std::string> Subst;
		BuildInterfaceTypeSubst(InterfaceLinkageName, Subst);
		for (const std::string& ParentName : InterfaceDecl->BaseInterfaceNames)
		{
			const std::string ParentLinkage = ResolveInheritanceBaseLinkage(
			    SubstituteTypeNameWith(ParentName, Subst),
			    InterfaceDecl->ModulePath);
			if (ParentLinkage.empty())
			{
				ReportErrorId(
				    "diag.unknown_interface_type",
				    DiagLocation,
				    {ParentName});
				return false;
			}
			if (!CollectFlattenedInterfaceMethods(
			        ParentLinkage,
			        OutMethods,
			        VisitedInterfaces,
			        &InterfaceDecl->Location,
			        ConflictSink))
			{
				return false;
			}
		}
		for (const FMethodDecl& Method : InterfaceDecl->Methods)
		{
			// Static interface member functions are free-function-like; they do not
			// participate in interface conformance / virtual-default flattening.
			if (Method.bIsStatic || !TemplateParameterNames(Method).empty())
			{
				continue;
			}
			bool bSkipAdd = false;
			for (auto It = OutMethods.begin(); It != OutMethods.end();)
			{
				const FInterfaceDecl* const PrevIface =
				    FindInterfaceDeclByLinkage(It->first);
				const std::string PrevModule =
				    PrevIface ? PrevIface->ModulePath : InterfaceDecl->ModulePath;
				std::unordered_map<std::string, std::string> PrevSubst;
				BuildInterfaceTypeSubst(It->first, PrevSubst);
				if (MethodSignaturesMatch(
				        *It->second,
				        Method,
				        PrevModule,
				        InterfaceDecl->ModulePath,
				        PrevSubst,
				        Subst))
				{
					const bool bOverridesAncestor =
					    IsIfaceAncestorOf(It->first, InterfaceLinkageName);
					if (!bOverridesAncestor)
					{
						if (Method.Body && It->second->Body)
						{
							// Keep the first default; require a descendant to redeclare.
							ConflictSink->emplace_back(
							    Method.Name,
							    It->first,
							    InterfaceLinkageName,
							    It->second);
							bSkipAdd = true;
							break;
						}
						if (!Method.Body && It->second->Body)
						{
							// Sibling abstract loses to existing default.
							bSkipAdd = true;
							break;
						}
					}
					It = OutMethods.erase(It);
				}
				else if (
				    It->second->Name == Method.Name && It->second->bIsConst == Method.bIsConst && CanonicalMethodParamTypeNames(*It->second, PrevModule, PrevSubst) == CanonicalMethodParamTypeNames(Method, InterfaceDecl->ModulePath, Subst))
				{
					// Same name + same parameter signature but MethodSignaturesMatch failed:
					// the methods differ only by return type (or another non-overloadable
					// aspect), which is a genuine clash. Different parameter types are a
					// legitimate overload (as with classes) and fall through to keep both.
					ReportErrorId(
					    "diag.conflicting_interface_method_signatures",
					    DiagLocation,
					    {Method.Name, It->first, InterfaceLinkageName});
					return false;
				}
				else
				{
					++It;
				}
			}
			if (!bSkipAdd)
			{
				OutMethods.emplace_back(InterfaceLinkageName, &Method);
			}
		}
		// Drop deferred clashes this interface redeclares with a matching signature.
		for (std::size_t Ci = 0; Ci < ConflictSink->size();)
		{
			const auto& [MethodName, FirstIface, SecondIface, LeftMethod] = (*ConflictSink)[Ci];
			bool bResolvedBySelf = false;
			if (LeftMethod)
			{
				const FInterfaceDecl* const LeftDecl = FindInterfaceDeclByLinkage(FirstIface);
				const std::string LeftModule =
				    LeftDecl ? LeftDecl->ModulePath : InterfaceDecl->ModulePath;
				std::unordered_map<std::string, std::string> LeftSubst;
				BuildInterfaceTypeSubst(FirstIface, LeftSubst);
				for (const auto& Entry : OutMethods)
				{
					if (Entry.first != InterfaceLinkageName || !Entry.second)
					{
						continue;
					}
					if (MethodSignaturesMatch(
					        *LeftMethod,
					        *Entry.second,
					        LeftModule,
					        InterfaceDecl->ModulePath,
					        LeftSubst,
					        Subst))
					{
						bResolvedBySelf = true;
						break;
					}
				}
			}
			if (bResolvedBySelf)
			{
				ConflictSink->erase(ConflictSink->begin() + static_cast<std::ptrdiff_t>(Ci));
			}
			else
			{
				++Ci;
			}
		}
		if (!DeferredDefaultConflicts && !LocalDeferred.empty())
		{
			const auto& [MethodName, FirstIface, SecondIface, LeftMethod] = LocalDeferred.front();
			(void)LeftMethod;
			ReportErrorId(
			    "diag.ambiguous_interface_default_method_from_and_redeclare_to_disambiguate",
			    InterfaceDecl->Location,
			    {MethodName, FirstIface, SecondIface});
			return false;
		}
		return true;
	}

	bool VeroScriptCompiler::CollectFlattenedInterfaceFields(
	    const std::string& InterfaceLinkageName,
	    std::vector<std::pair<std::string, const FFieldDecl*>>& OutFields,
	    std::unordered_set<std::string>& VisitedInterfaces,
	    const FSourceLocation* ConflictLocation) const
	{
		if (!VisitedInterfaces.insert(InterfaceLinkageName).second)
		{
			return true;
		}
		const FInterfaceDecl* const InterfaceDecl = FindInterfaceDeclByLinkage(InterfaceLinkageName);
		if (!InterfaceDecl)
		{
			return false;
		}
		const FSourceLocation& DiagLocation =
		    ConflictLocation ? *ConflictLocation : InterfaceDecl->Location;
		std::unordered_map<std::string, std::string> Subst;
		BuildInterfaceTypeSubst(InterfaceLinkageName, Subst);
		for (const std::string& ParentName : InterfaceDecl->BaseInterfaceNames)
		{
			const std::string ParentLinkage = ResolveInheritanceBaseLinkage(
			    SubstituteTypeNameWith(ParentName, Subst),
			    InterfaceDecl->ModulePath);
			if (ParentLinkage.empty())
			{
				ReportErrorId(
				    "diag.unknown_interface_type",
				    DiagLocation,
				    {ParentName});
				return false;
			}
			if (!CollectFlattenedInterfaceFields(
			        ParentLinkage,
			        OutFields,
			        VisitedInterfaces,
			        &InterfaceDecl->Location))
			{
				return false;
			}
		}
		for (const FFieldDecl& Field : InterfaceDecl->Fields)
		{
			// Static interface data members are shared globals, not layout state.
			if (Field.bIsStatic)
			{
				continue;
			}
			for (const auto& Existing : OutFields)
			{
				if (Existing.second && Existing.second->Name == Field.Name)
				{
					ReportErrorId(
					    "diag.conflicting_interface_field",
					    DiagLocation,
					    {Field.Name, Existing.first, InterfaceLinkageName});
					return false;
				}
			}
			OutFields.emplace_back(InterfaceLinkageName, &Field);
		}
		return true;
	}

	bool VeroScriptCompiler::AppendFlattenedInterfaceFields(
	    FBytecodeTypeDesc& TypeDesc,
	    const std::vector<std::string>& InterfaceTypeNames,
	    const std::string& OwnerModulePath,
	    const FSourceLocation& Location)
	{
		std::vector<std::pair<std::string, const FFieldDecl*>> Flattened;
		std::unordered_set<std::string> Visited;
		for (const std::string& InterfaceName : InterfaceTypeNames)
		{
			std::string Linkage;
			if (!EnsureInterfaceTypeResolved(
			        NormalizeInheritanceTypeName(InterfaceName, OwnerModulePath), Linkage, nullptr, Location))
			{
				return false;
			}
			if (!CollectFlattenedInterfaceFields(Linkage, Flattened, Visited, &Location))
			{
				return false;
			}
		}
		for (const auto& [IfaceLinkage, Field] : Flattened)
		{
			if (!Field)
			{
				continue;
			}
			const FInterfaceDecl* const IfaceDecl = FindInterfaceDeclByLinkage(IfaceLinkage);
			const std::string FieldModule =
			    IfaceDecl ? IfaceDecl->ModulePath : OwnerModulePath;
			std::unordered_map<std::string, std::string> Subst;
			BuildInterfaceTypeSubst(IfaceLinkage, Subst);
			const std::string DeclaredType = SubstituteTypeNameWith(Field->TypeName, Subst);
			const std::string QualifiedDeclaredType = QualifyTypeName(
			    DeclaredType, FieldModule);
			const std::string QualifiedType = QualifyTypeName(
			    NormalizeTypeAlias(QualifiedDeclaredType, FieldModule), FieldModule);
			bool bAlreadyPresent = false;
			for (FBytecodeFieldDesc& Existing : TypeDesc.Fields)
			{
				if (Existing.Name != Field->Name)
				{
					continue;
				}
				if (Existing.TypeName != QualifiedType)
				{
					ReportErrorId(
					    "diag.field_name_collision_with_interface_field",
					    Field->Location.Line != 0 ? Field->Location : Location,
					    {Field->Name});
					return false;
				}
				// Base/class field won layout; inherit interface DeclMeta when absent.
				if (Existing.DeclMeta.empty() && !Field->DeclMeta.empty())
				{
					Existing.DeclMeta = Field->DeclMeta;
				}
				bAlreadyPresent = true;
				break;
			}
			if (bAlreadyPresent)
			{
				continue;
			}
			FBytecodeFieldDesc FieldDesc;
			FieldDesc.Name = Field->Name;
			FieldDesc.DeclaredTypeName = QualifiedDeclaredType;
			FieldDesc.TypeName = QualifiedType;
			// Inherited fields and their defaults belong to the declaring interface,
			// whose generic parameter names can differ from the implementing type.
			const std::string PreviousModulePath = CurrentModulePath;
			const auto PreviousSubst = ActiveTypeSubst;
			CurrentModulePath = FieldModule;
			ActiveTypeSubst = std::move(Subst);
			const bool bCompiled = ValidateFieldTypeName(QualifiedType, Field->Location) && CompileFieldDefault(*Field, FieldDesc);
			ActiveTypeSubst = PreviousSubst;
			CurrentModulePath = PreviousModulePath;
			if (!bCompiled)
			{
				return false;
			}
			FieldDesc.DeclMeta = Field->DeclMeta;
			TypeDesc.Fields.push_back(std::move(FieldDesc));
		}
		return true;
	}

	bool VeroScriptCompiler::CheckInterfaceInheritanceCycles()
	{
		if (!Module)
		{
			return true;
		}
		enum class EIfaceVisit : std::uint8_t
		{
			White = 0,
			Gray = 1,
			Black = 2
		};
		std::vector<EIfaceVisit> Colors(Module->Interfaces.size(), EIfaceVisit::White);
		std::function<bool(std::uint16_t)> Visit = [&](const std::uint16_t Current) -> bool
		{
			if (Current >= Module->Interfaces.size())
			{
				return true;
			}
			if (Colors[Current] == EIfaceVisit::Gray)
			{
				ReportErrorId(
				    "diag.interface_inheritance_cycle_involving",
				    FSourceLocation{},
				    {Module->Interfaces[Current].Name});
				return false;
			}
			if (Colors[Current] == EIfaceVisit::Black)
			{
				return true;
			}
			Colors[Current] = EIfaceVisit::Gray;
			for (const std::uint16_t Parent : Module->Interfaces[Current].BaseInterfaceIndices)
			{
				if (!Visit(Parent))
				{
					return false;
				}
			}
			Colors[Current] = EIfaceVisit::Black;
			return true;
		};
		for (std::size_t Index = 0; Index < Module->Interfaces.size(); ++Index)
		{
			if (Colors[Index] == EIfaceVisit::White && !Visit(static_cast<std::uint16_t>(Index)))
			{
				return false;
			}
		}
		return true;
	}

	const FInterfaceDecl* VeroScriptCompiler::FindInterfaceDeclByLinkage(
	    const std::string& LinkageName) const
	{
		if (!ActiveProgram)
		{
			return nullptr;
		}
		std::string TemplateLinkage = GenericTemplateBaseLinkage(LinkageName);
		if (TemplateLinkage.empty())
		{
			TemplateLinkage = LinkageName;
		}
		const std::size_t WantedArity =
		    GenericTemplateBaseLinkage(LinkageName).empty()
		    ? 0
		    : DecodeGenericConcreteTypes(LinkageName).size();
		const FInterfaceDecl* Selected = nullptr;
		for (const FInterfaceDecl& InterfaceDecl : ActiveProgram->Interfaces)
		{
			if (GetSymbolLinkageName(InterfaceDecl.ModulePath, InterfaceDecl.bExported, InterfaceDecl.Name) != TemplateLinkage || TemplateParameterNames(InterfaceDecl).size() != WantedArity)
			{
				continue;
			}
			if (!Selected)
			{
				Selected = &InterfaceDecl;
			}
		}
		return Selected;
	}

	const FInterfaceDecl* VeroScriptCompiler::FindGenericInterfaceDecl(
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

		if (Selection.Ambiguous)
		{
			ReportErrorId("diag.template.type_arguments_ambiguous", CurrentEmitLocation, {SimpleName});
			return nullptr;
		}
		return Selection.Interface;
	}

	bool VeroScriptCompiler::EnsureInterfaceInstantiation(
	    const FInterfaceDecl& InterfaceDecl,
	    const std::vector<std::string>& ConcreteTypes,
	    std::uint16_t& OutInterfaceIndex,
	    const FSourceLocation& Location)
	{
		const std::string BaseLinkageName = GetSymbolLinkageName(
		    InterfaceDecl.ModulePath,
		    InterfaceDecl.bExported,
		    InterfaceDecl.Name);
		const std::string MangledName = MangleGenericInstanceName(BaseLinkageName, ConcreteTypes);
		for (std::size_t Index = 0; Index < Module->Interfaces.size(); ++Index)
		{
			if (Module->Interfaces[Index].Name == MangledName)
			{
				OutInterfaceIndex = static_cast<std::uint16_t>(Index);
				return true;
			}
		}

		if (InstantiatingGenerics.find(MangledName) != InstantiatingGenerics.end())
		{
			ReportError("recursive generic instantiation: " + MangledName, Location);
			return false;
		}

		if (!CheckTypeParamBounds(
		        TemplateParameterNames(InterfaceDecl),
		        InterfaceDecl.Bounds,
		        ConcreteTypes,
		        InterfaceDecl.ModulePath,
		        Location,
		        "type"))
		{
			return false;
		}

		InstantiatingGenerics.insert(MangledName);
		const std::unordered_map<std::string, std::string> PreviousSubst = ActiveTypeSubst;
		if (!ApplyTypeParamSubst(
		        TemplateParameterNames(InterfaceDecl),
		        ConcreteTypes,
		        ActiveTypeSubst,
		        Location))
		{
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}

		if (!InterfaceDecl.Scope.Id.empty() && !CheckScopedTypeRequirements(InterfaceDecl.Scope.Id, {}, ActiveTypeSubst))
		{
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}
		const std::string PreviousModulePath = CurrentModulePath;
		CurrentModulePath = InterfaceDecl.ModulePath;

		std::vector<std::uint16_t> BaseInterfaceIndices;
		for (const std::string& ParentName : InterfaceDecl.BaseInterfaceNames)
		{
			const std::string ParentAsWritten = NormalizeInheritanceTypeName(
			    SubstituteTypeName(ParentName), InterfaceDecl.ModulePath);
			std::string InstantiatedParent;
			std::uint16_t BaseInterfaceIndex = 0xFFFF;
			if (!EnsureInterfaceTypeResolved(
			        ParentAsWritten,
			        InstantiatedParent,
			        &BaseInterfaceIndex,
			        Location))
			{
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			if (BaseInterfaceIndex == static_cast<std::uint16_t>(-1))
			{
				BaseInterfaceIndex = FindInterfaceIndex(InstantiatedParent);
			}
			if (BaseInterfaceIndex == static_cast<std::uint16_t>(-1))
			{
				ReportErrorId("diag.unknown_parent_interface", Location, {ParentAsWritten});
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
			BaseInterfaceIndices.push_back(BaseInterfaceIndex);
		}

		FBytecodeInterfaceDesc Desc;
		Desc.Name = MangledName;
		PopulateSourceTypeScope(InterfaceDecl.Scope, ConcreteTypes, Desc.Scope, Desc.NestedTypes);
		ApplyTemplateInstanceMetadata(Desc.TemplateInstance, &InterfaceDecl.DeclMeta);
		Desc.DeclMeta = InterfaceDecl.DeclMeta;
		Desc.BaseInterfaceIndices = std::move(BaseInterfaceIndices);
		if (!RegisterStaticFieldsForTypeLinkage(MangledName, InterfaceDecl.ModulePath, InterfaceDecl.Fields))
		{
			CurrentModulePath = PreviousModulePath;
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}
		for (const FFieldDecl& Field : InterfaceDecl.Fields)
		{
			// Static interface data members are shared globals, not layout state —
			// keep DeclMeta on StaticFields for embed reflection (mirrors Types).
			if (Field.bIsStatic)
			{
				FBytecodeFieldDesc StaticDesc;
				StaticDesc.Name = Field.Name;
				const std::string DeclaredType = SubstituteTypeName(Field.TypeName);
				StaticDesc.DeclaredTypeName = QualifyTypeName(
				    DeclaredType, InterfaceDecl.ModulePath);
				StaticDesc.TypeName = QualifyTypeName(
				    NormalizeTypeAlias(
				        StaticDesc.DeclaredTypeName, InterfaceDecl.ModulePath),
				    InterfaceDecl.ModulePath);
				StaticDesc.bIsConst = Field.bIsConst;
				StaticDesc.bConstexpr = Field.bConstexpr;
				StaticDesc.DeclMeta = Field.DeclMeta;
				Desc.StaticFields.push_back(std::move(StaticDesc));
				continue;
			}
			FBytecodeFieldDesc FieldDesc;
			FieldDesc.Name = Field.Name;
			const std::string DeclaredType = SubstituteTypeName(Field.TypeName);
			FieldDesc.DeclaredTypeName = QualifyTypeName(
			    DeclaredType, InterfaceDecl.ModulePath);
			FieldDesc.TypeName = QualifyTypeName(
			    NormalizeTypeAlias(
			        FieldDesc.DeclaredTypeName, InterfaceDecl.ModulePath),
			    InterfaceDecl.ModulePath);
			FieldDesc.bIsMutable = Field.bIsMutable;
			FieldDesc.bIsConst = Field.bIsConst;
			FieldDesc.bConstexpr = Field.bConstexpr;
			FieldDesc.Access = Field.Access;
			FieldDesc.DeclMeta = Field.DeclMeta;
			Desc.Fields.push_back(std::move(FieldDesc));
		}
		Module->Interfaces.push_back(std::move(Desc));
		OutInterfaceIndex = static_cast<std::uint16_t>(Module->Interfaces.size() - 1);

		if (!InterfaceDecl.BaseInterfaceNames.empty())
		{
			std::vector<std::pair<std::string, const FMethodDecl*>> Flattened;
			std::unordered_set<std::string> VisitedInterfaces;
			if (!CollectFlattenedInterfaceMethods(MangledName, Flattened, VisitedInterfaces))
			{
				Module->Interfaces.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
		}
		if (!CheckInterfaceInheritanceCycles())
		{
			Module->Interfaces.pop_back();
			CurrentModulePath = PreviousModulePath;
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}

		const std::size_t FunctionCountBeforeMethods = Module->Functions.size();
		// Default bodies can bind later methods of the same closed interface.
		for (const FMethodDecl& Method : InterfaceDecl.Methods)
		{
			if (!TemplateParameterNames(Method).empty())
			{
				continue;
			}
			const auto MethodName = MakeMethodFunctionName(MangledName, Method.Name,
			    CanonicalMethodParamTypeNames(Method, InterfaceDecl.ModulePath, ActiveTypeSubst), Method.bIsConst);
			std::string BaseLinkage;
			const FMethodDecl* BaseMethod = nullptr;
			const bool bIsVirtual = !Method.bIsStatic && (Method.bVirtual || Method.bFinal || FindVirtualBaseMethod(MangledName, Method, InterfaceDecl.ModulePath, BaseLinkage, BaseMethod));
			if (!EnsureMethodDeclarationStub(MethodName, Method, 0xFFFF, bIsVirtual, InterfaceDecl.ModulePath))
			{
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Interfaces.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
		}
		if (!CompileOwnFieldDefaults(MangledName, OutInterfaceIndex, InterfaceDecl.Fields, true) ||
		    !QueueOrCompileStaticFieldInitializersForTypeLinkage(
		        MangledName, InterfaceDecl.ModulePath, InterfaceDecl.Fields))
		{
			CurrentModulePath = PreviousModulePath;
			ActiveTypeSubst = PreviousSubst;
			InstantiatingGenerics.erase(MangledName);
			return false;
		}
		for (const FMethodDecl& Method : InterfaceDecl.Methods)
		{
			if (!TemplateParameterNames(Method).empty())
			{
				continue;
			}

			if (Method.bDeleted)
			{
				continue;
			}
			// Static member functions compile like free functions; no virtual base check.
			if (!Method.bIsStatic)
			{
				std::string BaseLinkage;
				const FMethodDecl* BaseMethod = nullptr;
				const bool bFoundVirtualBase = FindVirtualBaseMethod(
				    MangledName,
				    Method,
				    InterfaceDecl.ModulePath,
				    BaseLinkage,
				    BaseMethod);
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
					Module->Interfaces.pop_back();
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
					Module->Interfaces.pop_back();
					CurrentModulePath = PreviousModulePath;
					ActiveTypeSubst = PreviousSubst;
					InstantiatingGenerics.erase(MangledName);
					return false;
				}
			}
			if (Method.bPureVirtual)
			{
				const std::string MethodName = MakeMethodFunctionName(
				    MangledName,
				    Method.Name,
				    CanonicalMethodParamTypeNames(Method, InterfaceDecl.ModulePath, ActiveTypeSubst),
				    Method.bIsConst);
				if (!EnsureFunctionStub(
				        MethodName,
				        SubstituteTypeName(Method.ReturnType),
				        static_cast<std::uint8_t>(Method.Parameters.size()),
				        !Method.bIsStatic,
				        Method.bIsStatic ? 0 : 0xFFFF,
				        Method.bIsStatic ? false : Method.bVirtual,
				        Method.Location,
				        Method.bIsConst))
				{
					while (Module->Functions.size() > FunctionCountBeforeMethods)
					{
						Module->FunctionIndices.erase(Module->Functions.back().Name);
						Module->Functions.pop_back();
					}
					Module->Interfaces.pop_back();
					CurrentModulePath = PreviousModulePath;
					ActiveTypeSubst = PreviousSubst;
					InstantiatingGenerics.erase(MangledName);
					return false;
				}
				const auto StubIt = Module->FunctionIndices.find(MethodName);
				if (StubIt != Module->FunctionIndices.end() && StubIt->second < Module->Functions.size())
				{
					FBytecodeFunction& Stub = Module->Functions[StubIt->second];
					std::vector<FFunctionParam> SubstitutedParams;
					SubstitutedParams.reserve(Method.Parameters.size());
					for (const FFunctionParam& Param : Method.Parameters)
					{
						FFunctionParam Copy = Param;
						Copy.TypeName = SubstituteTypeName(Param.TypeName);
						SubstitutedParams.push_back(std::move(Copy));
					}
					SetFunctionDeclarationSignatureMetadata(
					    Stub,
					    SubstituteTypeName(Method.ReturnType),
					    SubstitutedParams,
					    InterfaceDecl.ModulePath);
					Stub.DeclMeta = Method.DeclMeta;
					Stub.Access = Method.Access;
					Stub.bIsAbstract = true;
				}
				continue;
			}
			const std::string MethodName = MakeMethodFunctionName(
			    MangledName,
			    Method.Name,
			    CanonicalMethodParamTypeNames(Method, InterfaceDecl.ModulePath, ActiveTypeSubst), Method.bIsConst);
			std::vector<FFunctionParam> SubstitutedParams;
			SubstitutedParams.reserve(Method.Parameters.size());
			for (const FFunctionParam& Param : Method.Parameters)
			{
				FFunctionParam Copy = Param;
				Copy.TypeName = SubstituteTypeName(Param.TypeName);
				SubstitutedParams.push_back(std::move(Copy));
			}
			CurrentInterfaceOwnerLinkage = Method.bIsStatic ? std::string{} : MangledName;
			const bool bCompiled = CompileFunction(
			    MethodName,
			    SubstituteTypeName(Method.ReturnType),
			    SubstitutedParams,
			    *Method.Body,
			    !Method.bIsStatic,
			    Method.bIsStatic ? 0 : 0xFFFF,
			    InterfaceDecl.ModulePath,
			    Method.Location,
			    Method.bIsStatic ? false : Method.bVirtual,
			    nullptr,
			    Method.bIsConst,
			    &Method.DeclMeta);
			CurrentInterfaceOwnerLinkage.clear();
			if (!bCompiled)
			{
				while (Module->Functions.size() > FunctionCountBeforeMethods)
				{
					Module->FunctionIndices.erase(Module->Functions.back().Name);
					Module->Functions.pop_back();
				}
				Module->Interfaces.pop_back();
				CurrentModulePath = PreviousModulePath;
				ActiveTypeSubst = PreviousSubst;
				InstantiatingGenerics.erase(MangledName);
				return false;
			}
		}

		CurrentModulePath = PreviousModulePath;
		ActiveTypeSubst = PreviousSubst;
		InstantiatingGenerics.erase(MangledName);
		return true;
	}

	bool VeroScriptCompiler::EnsureInterfaceTypeResolved(
	    const std::string& TypeName,
	    std::string& OutLinkageName,
	    std::uint16_t* OutInterfaceIndex,
	    const FSourceLocation& Location)
	{
		if (!ActiveProgram || !Module || TypeName.empty())
		{
			return false;
		}
		{
			const std::string Expanded = ExpandUserTypeAlias(TypeName, CurrentModulePath);
			if (Expanded != TypeName)
			{
				return EnsureInterfaceTypeResolved(
				    Expanded,
				    OutLinkageName,
				    OutInterfaceIndex,
				    Location);
			}
		}

		for (std::size_t Index = 0; Index < Module->Interfaces.size(); ++Index)
		{
			if (Module->Interfaces[Index].Name == TypeName)
			{
				OutLinkageName = TypeName;
				if (OutInterfaceIndex)
				{
					*OutInterfaceIndex = static_cast<std::uint16_t>(Index);
				}
				return true;
			}
		}

		const std::string TemplateBase = GenericTemplateBaseLinkage(TypeName);
		if (!TemplateBase.empty())
		{
			const std::vector<std::string> DecodedConcreteTypes = DecodeGenericConcreteTypes(TypeName);
			for (const FInterfaceDecl& Candidate : ActiveProgram->Interfaces)
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
				std::uint16_t InterfaceIndex = static_cast<std::uint16_t>(-1);
				if (!EnsureInterfaceInstantiation(Candidate, ConcreteTypes, InterfaceIndex, Location))
				{
					return false;
				}
				OutLinkageName = Module->Interfaces[InterfaceIndex].Name;
				if (OutInterfaceIndex)
				{
					*OutInterfaceIndex = InterfaceIndex;
				}
				return true;
			}
			return false;
		}

		std::string BaseName;
		std::vector<std::string> ArgTypes;
		if (SplitUserGenericTypeApp(TypeName, BaseName, ArgTypes))
		{
			const FInterfaceDecl* const InterfaceDecl =
			    FindGenericInterfaceDecl(BaseName, CurrentModulePath, ArgTypes.size());
			if (!InterfaceDecl)
			{
				return false;
			}
			if (TemplateParameterNames(*InterfaceDecl).empty())
			{
				ReportErrorId("diag.type_is_not_generic", Location, {BaseName});
				return false;
			}
			std::vector<std::string> ConcreteTypes = ArgTypes;
			if (InterfaceDecl->TemplateParameters.IsEmpty())
			{
				for (auto& ConcreteType : ConcreteTypes)
				{
					ConcreteType = CanonicalTypeNameForCoercion(
					    QualifyTypeName(SubstituteTypeName(ConcreteType), CurrentModulePath));
				}
			}
			if (!CompleteGenericTypeArguments(
			        TemplateParameterNames(*InterfaceDecl),
			        TemplateParameterDefaults(*InterfaceDecl),
			        InterfaceDecl->ModulePath,
			        ConcreteTypes,
			        Location,
			        BaseName, &InterfaceDecl->TemplateParameters))
			{
				return false;
			}
			// Captured defaults must be completed in their declaration environment
			// before they are validated as concrete field types.
			for (std::size_t Index = 0; Index < ConcreteTypes.size(); ++Index)
			{
				if (!InterfaceDecl->TemplateParameters.IsEmpty() && InterfaceDecl->TemplateParameters.GetParameters()[Index].Kind == ETemplateParameterKind::Value)
				{
					continue;
				}
				const auto& ConcreteType = ConcreteTypes[Index];
				if (ConcreteType != "void" && !ValidateFieldTypeName(ConcreteType, Location))
				{
					return false;
				}
			}
			std::uint16_t InterfaceIndex = static_cast<std::uint16_t>(-1);
			if (!EnsureInterfaceInstantiation(*InterfaceDecl, ConcreteTypes, InterfaceIndex, Location))
			{
				return false;
			}
			OutLinkageName = Module->Interfaces[InterfaceIndex].Name;
			if (OutInterfaceIndex)
			{
				*OutInterfaceIndex = InterfaceIndex;
			}
			return true;
		}

		const std::string LinkageName = ResolveTypeLinkageName(TypeName, CurrentModulePath);
		if (LinkageName.empty())
		{
			return false;
		}
		const FInterfaceDecl* const InterfaceDecl = FindInterfaceDeclByLinkage(LinkageName);
		if (!InterfaceDecl)
		{
			return false;
		}
		if (!TemplateParameterNames(*InterfaceDecl).empty())
		{
			ReportErrorId(
			    "diag.generic_interface_requires_type_arguments",
			    Location,
			    {TypeName});
			return false;
		}
		const std::uint16_t Index = FindInterfaceIndex(LinkageName);
		if (Index == static_cast<std::uint16_t>(-1))
		{
			return false;
		}
		OutLinkageName = LinkageName;
		if (OutInterfaceIndex)
		{
			*OutInterfaceIndex = Index;
		}
		return true;
	}

	bool VeroScriptCompiler::BuildInterfaceTypeSubst(
	    const std::string& InterfaceLinkageOrApp,
	    std::unordered_map<std::string, std::string>& OutSubst) const
	{
		OutSubst.clear();
		if (!ActiveProgram || InterfaceLinkageOrApp.empty())
		{
			return true;
		}
		std::string Instantiated = InterfaceLinkageOrApp;
		std::string GenericBase;
		std::vector<std::string> GenericArgs;
		if (SplitUserGenericTypeApp(InterfaceLinkageOrApp, GenericBase, GenericArgs))
		{
			Instantiated = QualifyTypeName(InterfaceLinkageOrApp, CurrentModulePath);
		}
		const std::string TemplateBase = GenericTemplateBaseLinkage(Instantiated);
		if (TemplateBase.empty())
		{
			return true;
		}
		const std::vector<std::string> ConcreteTypes = DecodeGenericConcreteTypes(Instantiated);
		// Pass Instantiated (with #) so arity-overloaded interface templates resolve.
		const FInterfaceDecl* const InterfaceDecl = FindInterfaceDeclByLinkage(Instantiated);
		if (!InterfaceDecl || TemplateParameterNames(*InterfaceDecl).size() != ConcreteTypes.size())
		{
			return false;
		}
		for (std::size_t Index = 0; Index < TemplateParameterNames(*InterfaceDecl).size(); ++Index)
		{
			OutSubst[TemplateParameterNames(*InterfaceDecl)[Index]] = ConcreteTypes[Index];
		}
		return true;
	}

	std::string VeroScriptCompiler::NormalizeInheritanceTypeName(
	    const std::string& TypeAsWritten,
	    const std::string& ReferenceModulePath) const
	{
		const auto Expanded = ExpandUserTypeAlias(TypeAsWritten, ReferenceModulePath);
		if (Expanded.size() < 2 || Expanded.compare(Expanded.size() - 2, 2, "<>") != 0) return Expanded;
		std::string Base;
		std::vector<std::string> Arguments;
		if (!SplitUserGenericTypeApp(Expanded, Base, Arguments) || !Arguments.empty()) return Expanded;
		// Empty inheritance lists historically also name ordinary, arity-zero
		// types. Preserve that spelling while retaining <> for real defaults.
		const auto AliasedBase = ExpandHostTypeAlias(ExpandUserTypeAlias(Base, ReferenceModulePath));
		if (AliasedBase != Base) return AliasedBase;
		const auto IsOrdinary = [](const auto* Declaration)
		{
			return Declaration && TemplateParameterNames(*Declaration).empty();
		};
		if (IsOrdinary(FindGenericStructDecl(Base, ReferenceModulePath, 0)) || IsOrdinary(FindGenericClassDecl(Base, ReferenceModulePath, 0)) || IsOrdinary(FindGenericInterfaceDecl(Base, ReferenceModulePath, 0)) || (ActiveBindings && ActiveBindings->FindType(Base)))
		{
			return Base;
		}
		return Expanded;
	}

	std::string VeroScriptCompiler::ResolveInheritanceBaseLinkage(
	    const std::string& TypeAsWritten,
	    const std::string& ReferenceModulePath) const
	{
		if (TypeAsWritten.empty())
		{
			return {};
		}
		const auto Normalized = NormalizeInheritanceTypeName(TypeAsWritten, ReferenceModulePath);
		if (Normalized != TypeAsWritten) return ResolveInheritanceBaseLinkage(Normalized, ReferenceModulePath);
		std::string GenericBase;
		std::vector<std::string> GenericArgs;
		if (SplitUserGenericTypeApp(TypeAsWritten, GenericBase, GenericArgs))
		{
			// Use the common declaration selection and typed default binding.
			return QualifyTypeName(TypeAsWritten, ReferenceModulePath);
		}
		if (FindGenericTypeMarker(TypeAsWritten) != std::string::npos || TypeAsWritten.find('\x1F') != std::string::npos)
		{
			// A closed identity already fixes its schema and argument count.
			// Reapplying defaults could select a different arity overload.
			return TypeAsWritten;
		}
		return ResolveTypeLinkageName(TypeAsWritten, ReferenceModulePath);
	}

}
