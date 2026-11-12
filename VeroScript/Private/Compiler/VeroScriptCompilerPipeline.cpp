// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "VeroScriptAnalysisControl.h"
#include "ConstantEvaluation/VeroScriptConstantRequest.h"
#include "ConstantEvaluation/VeroScriptConstantBranches.h"

#include "VeroScriptAst.h"
#include "VeroScriptStandardLibrary.h"
#include "ModuleSupport/VeroScriptScopedBindingCatalog.h"
#include "TypeSupport/VeroScriptRuntimeTypeScope.h"

#include <type_traits>

namespace VeroScript
{

	void VeroScriptCompiler::IndexSourceDeclarations(const FProgram& Program)
	{
		SourceTypeContexts.clear();
		SourceStructIndices.clear();
		SourceClassIndices.clear();
		SourceInterfaceIndices.clear();
		SourceStructNames.clear();
		SourceClassNames.clear();
		SourceInterfaceNames.clear();
		SourceEnumNames.clear();
		SourceMethodContexts.clear();
		for (auto& Index : SourceTypeScopeIndices)
		{
			Index.clear();
		}
		for (auto& Index : SourceTypeChildIndices)
		{
			Index.clear();
		}
		const auto Index = [&](const auto& Declarations)
		{
			std::size_t Position = 0;
			for (const auto& Declaration : Declarations)
			{
				if ((Position & 255u) == 0u)
				{
					CheckAnalysisCancellation();
				}
				const auto Linkage = GetSymbolLinkageName(Declaration.ModulePath, Declaration.bExported, Declaration.Name);
				const auto Key = Linkage + '\x1E' + std::to_string(TemplateParameterNames(Declaration).size());
				if (Program.bDeclarationsPrepared)
				{
					SourceTypeContexts.emplace(Key, FSourceTypeContext{&Declaration.Scope, Declaration.DeclarationContext});
				}
				using FDeclaration = std::decay_t<decltype(Declaration)>;
				constexpr auto Kind = std::is_same_v<FDeclaration, FClassDecl>
				    ? ERuntimeTypeDeclarationKind::Class
				    : std::is_same_v<FDeclaration, FStructDecl>
				    ? ERuntimeTypeDeclarationKind::Struct
				    : ERuntimeTypeDeclarationKind::Interface;
				SourceTypeScopeIndices[static_cast<unsigned>(Kind)][Declaration.Scope.Id].push_back(Position);
				SourceTypeChildIndices[static_cast<unsigned>(Kind)][Declaration.Scope.ScopeId].push_back(Position);
				if constexpr (std::is_same_v<FDeclaration, FStructDecl>)
				{
					SourceStructIndices[Linkage].push_back(Position);
					SourceStructNames[Declaration.Name].push_back(Position);
				}
				else if constexpr (std::is_same_v<FDeclaration, FClassDecl>)
				{
					SourceClassIndices[Linkage].push_back(Position);
					SourceClassNames[Declaration.Name].push_back(Position);
				}
				else
				{
					SourceInterfaceIndices[Linkage].push_back(Position);
					SourceInterfaceNames[Declaration.Name].push_back(Position);
				}
				for (const auto& Method : Declaration.Methods)
				{
					SourceMethodContexts.emplace(&Method.DeclMeta, FSourceMethodContext{&Method, Linkage, TemplateParameterNames(Declaration)});
				}
				++Position;
			}
		};
		Index(Program.Classes);
		Index(Program.Structs);
		Index(Program.Interfaces);
		for (std::size_t Position = 0; Position < Program.Enums.size(); ++Position)
		{
			if ((Position & 255u) == 0u)
			{
				CheckAnalysisCancellation();
			}
			SourceEnumNames[Program.Enums[Position].Name].push_back(Position);
			SourceTypeChildIndices[static_cast<unsigned>(ERuntimeTypeDeclarationKind::Enum)]
			                      [Program.Enums[Position].Scope.ScopeId]
			                          .push_back(Position);
		}
	}

	void VeroScriptCompiler::ResetCompilationState(
	    const FProgram& Program,
	    const std::vector<FNativeFunction>* const Natives,
	    const IHostBindingCatalog* const Bindings,
	    const ModuleVisibility::FImportIndex& ImportIndex)
	{
		// Reset per-compilation state before any validation can return early.
		// Error-collection policy belongs to the compiler instance and is preserved.
		if (!bConstantEvaluationLimitsExplicit)
		{
			ConstantEvaluationLimits = Program.ConstantEvaluationLimits;
		}
		ConstantEvaluationState = CurrentConstantEvaluationRequest();
		if (!ConstantEvaluationState)
		{
			ConstantEvaluationState = std::make_shared<FConstantEvaluationState>();
			ConstantEvaluationState->Limits = ConstantEvaluationLimits;
		}
		bHasError = false;
		Error = {};
		Diagnostics.clear();
		CopyRequirements.clear();
		bUsesRuntimeMemoryTypes = false;
		// Synthetic linkage is module-local: reusing the compiler must not change
		// bytecode identity or prevent an unchanged retained closure from migrating.
		NextLambdaId = 0;
		LambdaOrdinals.clear();
		LambdaOwnerIdentities.clear();
		CurrentFunctionSourceBody = nullptr;
		CurrentInitializerStatement = nullptr;
		CurrentInitializerExpression = nullptr;
		CurrentInitializerField = nullptr;
		MethodBindingCodegenInfo.clear();
		// A compile unit normally restores its outer context, but Compile() also
		// has validation exits outside those scopes. Establish the complete
		// top-level invariant here before replacing Module, whose functions may
		// still be referenced by CurrentFunction after an interrupted compilation.
		CurrentFunction = nullptr;
		Scopes.clear();
		LoopStack.clear();
		LabelTable.clear();
		GotoPatches.clear();
		LocalInitMarkers.clear();
		ActiveCatchSlots.clear();
		ActiveExceptionScopePath.clear();
		ActiveFinallyDepth = 0;
		NextLocalSlot = 0;
		NextAssignTempId = 0;
		NextExceptionScopeId = 1;
		CurrentOwnerTypeIndex = 0;
		CurrentOwnerTypeLinkage.clear();
		CurrentModulePath.clear();
		CurrentInterfaceOwnerLinkage.clear();
		ExpressionExpectedType.clear();
		bPreserveNextReferenceResult = false;
		bCurrentExpressionPreservesReference = false;
		ActiveTypeSubst.clear();
		InstantiatingGenerics.clear();
		TemplateMetadataBindings.clear();
		MethodSpecializations.clear();
		MethodSpecializationKeys.clear();
		TypesInProgress.clear();
		GlobalVarTypes.clear();
		GlobalVarConsts.clear();
		LocalStaticConstants.clear();
		EnclosingConstantScopes.clear();
		EnumTypeNames.clear();
		EnumConstants.clear();
		EnumConstantTypeNames.clear();
		UserTypeAliases.clear();
		LocalTypeAliasScopes.clear();
		CurrentFunctionReturnType.clear();
		CurrentNamespacePrefix.clear();
		CurrentAccessFunctionLinkage.clear();
		CurrentEmitLocation = {};
		ActiveExpressionCompileStackCost = 0;
		Module = std::make_unique<FBytecodeModule>();
		Module->SourceMetadata = Program.SourceMetadata;
		Module->LifetimeAnchors = Program.LifetimeAnchors;
		Module->RequiredExternalDependencies = Program.RequiredExternalDependencies;
		Module->EntryModulePath = Program.EntryModulePath;
		ActiveProgram = &Program;
		NamespaceLookup.reset();
		ActiveImportIndex = &ImportIndex;
		ConstantBranchState = std::make_shared<FConstantBranchState>(Program);
		IndexSourceDeclarations(Program);
		RegisteredTypeIndices.clear();
		IndexedTypeModule = nullptr;
		IndexedTypeCount = 0;
		ScopedUsesByType.clear();
		ScopedUsesByFunction.clear();
		if (Program.bDeclarationsPrepared)
		{
			for (const auto& Use : Program.ScopedTypeUses)
			{
				if (Use.Context && Use.Context->TypeScope)
				{
					ScopedUsesByType[Use.Context->TypeScope->Id].push_back(&Use);
				}
				if (!Use.ContextFunction.empty())
				{
					ScopedUsesByFunction[Use.ContextFunction].push_back(&Use);
				}
			}
		}
		StandardBindings = StandardLibrary::MergeBindingCatalog(Bindings, Natives);
		ActiveNatives = &StandardBindings->GetNativeFunctions();
		ActiveBindings = StandardBindings.get();
		ScopedBindings.reset();
		NativeExposure.clear();
		if (Program.ModuleEnvironment)
		{
			ScopedBindings = std::make_shared<FScopedBindingCatalog>(StandardBindings,
			    [this](const auto& Origin, const auto& Type)
			{
				return CanAccessNativeBinding(Origin, Type);
			});
			ActiveBindings = ScopedBindings.get();
		}
		InitializedStaticFieldGlobals.clear();
		bStaticFieldInitializationStarted = false;
		PendingGenericStaticFieldInits.clear();
		BuiltinStructTemplates.clear();
		BuiltinClassTemplates.clear();
	}

	void VeroScriptCompiler::PopulateSourceTypeScope(const FTypeDeclarationScope& Declaration,
	    const std::vector<std::string>& Arguments, FTypeDeclarationScope& Scope,
	    std::vector<FNestedTypeDeclaration>& NestedTypes) const
	{
		const FRuntimeTypeScopeLookupView Lookup{this,
		    [](const void* Context, const ERuntimeTypeDeclarationKind Kind,
		        const ERuntimeTypeLookupKind Query, const std::string& Key) -> const std::vector<std::size_t>&
		{
			const auto& Compiler = *static_cast<const VeroScriptCompiler*>(Context);
			if (Query == ERuntimeTypeLookupKind::Children)
			{
				return FindDeclarationCandidates(Compiler.SourceTypeChildIndices[static_cast<unsigned>(Kind)], Key);
			}
			if (Query == ERuntimeTypeLookupKind::Scope && Kind != ERuntimeTypeDeclarationKind::Enum)
			{
				return FindDeclarationCandidates(Compiler.SourceTypeScopeIndices[static_cast<unsigned>(Kind)], Key);
			}
			switch (Kind)
			{
			case ERuntimeTypeDeclarationKind::Class:
				return FindDeclarationCandidates(Compiler.SourceClassNames, Key);
			case ERuntimeTypeDeclarationKind::Struct:
				return FindDeclarationCandidates(Compiler.SourceStructNames, Key);
			case ERuntimeTypeDeclarationKind::Interface:
				return FindDeclarationCandidates(Compiler.SourceInterfaceNames, Key);
			case ERuntimeTypeDeclarationKind::Enum:
				return FindDeclarationCandidates(Compiler.SourceEnumNames, Key);

			default:
				{
					break;
				}
			}
			static const std::vector<std::size_t> Empty;
			return Empty;
		}};
		PopulateRuntimeTypeScope(*ActiveProgram, Declaration, Arguments, Scope, NestedTypes, &Lookup);
	}

}
