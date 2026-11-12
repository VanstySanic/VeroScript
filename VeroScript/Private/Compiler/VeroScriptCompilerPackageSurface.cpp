// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"

#include "VeroScriptAst.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptPackageInterface.h"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace VeroScript
{

	void VeroScriptCompiler::FinalizePackageSurface(const FProgram& Program)
	{
		Module->PublicMacros = Program.PublicMacros;
		// Select the binary-library surface from the root's export graph. Clear
		// dependency flags first: an ordinary import must not accidentally become
		// a transitive re-export merely because its implementation was linked.
		for (FBytecodeFunction& Function : Module->Functions)
		{
			Function.bPackageExported = false;
		}
		std::unordered_set<std::string> PackageSurfaceModules;
		PackageSurfaceModules.insert(Program.EntryModulePath);
		std::vector<std::string> ExportQueue{Program.EntryModulePath};
		while (!ExportQueue.empty())
		{
			const std::string Current = std::move(ExportQueue.back());
			ExportQueue.pop_back();
			for (const FImportDecl& ImportDecl : Program.Imports)
			{
				if (!ImportDecl.bReExport || ImportDecl.ImporterModulePath != Current || ImportDecl.ResolvedPath.empty())
				{
					continue;
				}
				if (PackageSurfaceModules.insert(ImportDecl.ResolvedPath).second)
				{
					ExportQueue.push_back(ImportDecl.ResolvedPath);
				}
			}
		}
		for (const FFunctionDecl& Function : Program.Functions)
		{
			if (!Function.bExported || !TemplateParameterNames(Function).empty() || PackageSurfaceModules.find(Function.ModulePath) == PackageSurfaceModules.end())
			{
				continue;
			}
			const auto Found = Module->FunctionIndices.find(
			    MakeFreeFunctionLinkageName(Function));
			if (Found != Module->FunctionIndices.end() && Found->second < Module->Functions.size())
			{
				Module->Functions[Found->second].bPackageExported = true;
			}
		}
		const auto IsSurfaceGeneric = [&](const auto& Declaration)
		{
			return Declaration.bExported && !TemplateParameterNames(Declaration).empty() && PackageSurfaceModules.find(Declaration.ModulePath) != PackageSurfaceModules.end();
		};
		const auto HasSurfaceMethodTemplates = [&](const auto& Declarations)
		{
			return std::any_of(Declarations.begin(), Declarations.end(), [&](const auto& Declaration)
			{
				return Declaration.bExported && PackageSurfaceModules.count(Declaration.ModulePath) != 0 && std::any_of(Declaration.Methods.begin(), Declaration.Methods.end(), [](const auto& Method)
				{
					return !TemplateParameterNames(Method).empty();
				});
			});
		};
		const bool HasMethodTemplates = HasSurfaceMethodTemplates(Program.Classes) || HasSurfaceMethodTemplates(Program.Structs) || HasSurfaceMethodTemplates(Program.Interfaces);
		bool bNeedsPackageTemplates = HasMethodTemplates;
		bool bHasScopedDeclarations = false;
		const auto InspectScopes = [&](const auto& Declarations)
		{
			for (const auto& Declaration : Declarations)
			{
				bHasScopedDeclarations = bHasScopedDeclarations || Declaration.Scope.bLocal || !Declaration.Scope.OwnerTypeName.empty();
			}
		};
		InspectScopes(Program.Classes);
		InspectScopes(Program.Structs);
		InspectScopes(Program.Interfaces);
		InspectScopes(Program.Enums);
		InspectScopes(Program.TypeAliases);
		bNeedsPackageTemplates = bNeedsPackageTemplates || bHasScopedDeclarations;
		for (const FStructDecl& Decl : Program.Structs)
		{
			bNeedsPackageTemplates = bNeedsPackageTemplates || IsSurfaceGeneric(Decl);
		}
		for (const FClassDecl& Decl : Program.Classes)
		{
			bNeedsPackageTemplates = bNeedsPackageTemplates || IsSurfaceGeneric(Decl);
		}
		for (const FInterfaceDecl& Decl : Program.Interfaces)
		{
			bNeedsPackageTemplates = bNeedsPackageTemplates || IsSurfaceGeneric(Decl);
		}
		for (const FFunctionDecl& Decl : Program.Functions)
		{
			bNeedsPackageTemplates = bNeedsPackageTemplates || IsSurfaceGeneric(Decl) || (Decl.bConstexpr && Decl.bExported && PackageSurfaceModules.count(Decl.ModulePath) != 0);
		}
		const auto HasConstants = [&](const auto& Declarations)
		{
			for (const auto& Declaration : Declarations)
			{
				if (PackageSurfaceModules.count(Declaration.ModulePath) == 0)
				{
					continue;
				}
				for (const auto& Field : Declaration.Fields)
				{
					if (Field.bConstexpr) return true;
				}
				for (const auto& Method : Declaration.Methods)
				{
					if (Method.bConstexpr) return true;
				}
			}
			return false;
		};
		bNeedsPackageTemplates = bNeedsPackageTemplates || HasConstants(Program.Structs) || HasConstants(Program.Classes) || HasConstants(Program.Interfaces);
		// A binary re-export must retain its original declaration facade even
		// without open generics. Flattening it into the wrapper interface changes
		// declaration identity and duplicates exports when both packages are used.
		for (const FPackageSourceModule& Source : Program.PackageSourceModules)
		{
			if (Source.bFromPackage && Source.bPackageSurface &&
			    PackageSurfaceModules.count(Source.ModulePath) != 0)
			{
				bNeedsPackageTemplates = true;
				break;
			}
		}
		if (bNeedsPackageTemplates)
		{
			std::unordered_set<std::string> AddedModules;
			for (const FBytecodePackageTemplateModule& Existing :
			    Module->PackageTemplateModules)
			{
				AddedModules.insert(Existing.ModulePath);
			}
			for (const FPackageSourceModule& Source : Program.PackageSourceModules)
			{
				if (Source.ModulePath.empty() || Source.Source.empty() || !AddedModules.insert(Source.ModulePath).second)
				{
					continue;
				}
				FBytecodePackageTemplateModule Template;
				Template.ModulePath = Source.ModulePath;
				Template.Source = Source.Source;
				Template.FacadePath = Source.FacadePath;
				Template.OwnerComponentId = Source.OwnerComponentId;
				Template.SourceOwnerId = Program.ModuleEnvironment && !Source.bFromPackage && Source.SourceOwnerId == Program.ModuleEnvironment->GetDescription().EntryOwnerId ? std::string{} : Source.SourceOwnerId;
				Template.GeneratedOperations = Source.GeneratedOperations;
				Template.CompileGeneratedOperations = Source.CompileGeneratedOperations;
				Template.InitialSourceMetadata = Source.InitialSourceMetadata;
				Template.bDeclarationOnly = Source.bDeclarationOnly;
				Template.bFromPackage = Source.bFromPackage;
				Template.bPackageSurface = Source.bPackageSurface || PackageSurfaceModules.find(Source.ModulePath) != PackageSurfaceModules.end();
				for (const FImportDecl& Import : Source.Imports)
				{
					if (Import.ResolvedPath.empty())
					{
						continue;
					}
					Template.Imports.push_back(FBytecodePackageTemplateImport{
					    Import.ResolvedPath,
					    Import.bReExport});
				}
				Module->PackageTemplateModules.push_back(std::move(Template));
			}
			if (Module->PackageTemplateModules.empty())
			{
				Module->PackageInterfaceError =
				    "open generic package surface has no retained template modules";
			}
		}
		for (FBytecodeTypeDesc& Type : Module->Types)
		{
			// A pure virtual destructor makes its declaring type abstract even though
			// derived implicit destructors can provide the final overrider. Preserve
			// that local fact when recomputing inherited virtual slots.
			Type.bIsAbstract = Type.bDestructorPureVirtual || TypeHasUnimplementedPureVirtual(Type.Name);
		}
		std::string PackageInterfaceError;
		// Retained source preserves declaration ownership and hidden dependencies.
		// A flat namespace facade cannot represent nested or local declarations.
		if (bHasScopedDeclarations || HasMethodTemplates)
		{
			Module->PackageInterfaceSource.clear();
		}
		else if (!BuildPackageInterfaceSource(
		             Program,
		             PackageSurfaceModules,
		             Module->PackageInterfaceSource,
		             PackageInterfaceError, Module.get()))
		{
			Module->PackageInterfaceSource.clear();
			Module->PackageInterfaceError =
			    "package interface failed: " + PackageInterfaceError;
		}
	}

}
