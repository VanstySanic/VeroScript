// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptCompileProcessing.h"
#include "VeroScriptPreprocessor.h"
#include <map>
#include <set>

namespace VeroScript
{
	bool NormalizeActiveAnnotationDefinitions(std::vector<FAnnotationDefinition>&, std::string& Error);
	class FCompileProcessingSession
	{
	public:
		struct FPendingFragmentMetadata
		{
			std::string SourceId;
			std::size_t Offset = 0;
			FCompileMetadata Metadata;
		};

		explicit FCompileProcessingSession(FCompileProcessingOptions Options);
		void Check() const;
		bool HasErrors() const;
		std::string Fingerprint() const;
		bool ReplaceRegistry(FCompileProcessingOptions, std::string& Error);
		bool ProcessSource(ECompileProcessingPhase Phase, const std::string& Module, std::string& Source);
		void RecordPreprocessedSource(const std::string& Module, const std::string& Source, const FPreprocessLineMap& Map);
		FPreprocessSourcePos MapSourcePosition(const std::string& Module,FPreprocessSourcePos) const;
		std::vector<FCompileGenerationOperation> Dispatch(ECompileProcessingPhase Phase,
			const std::vector<FAnnotationDeclarationView>& Declarations,
			const FSourceDocument* Source = nullptr);

		FCompileProcessingOptions Options;
		std::shared_ptr<FSourceMetadataSnapshot> Metadata = std::make_shared<FSourceMetadataSnapshot>();
		std::vector<FCompileGenerationOperation> Operations;
		std::vector<FAnnotationDependency> Dependencies;
		std::vector<FScriptError> Diagnostics;
		std::set<std::string> OriginalDeclarations;
		std::set<std::string> OriginalAnnotations, OriginalComments;
		std::set<std::string> ReadOnlyModules;
		std::set<std::string> ExplicitCommentTargets;
		std::map<std::string,FDeclMeta> ModuleMetadata;
		const FProgram* Program = nullptr;
		std::function<void(std::vector<FAnnotationDeclarationView>&)> BindDeclarationViews;
		bool Cacheable = true;
		std::uint64_t Snapshot = 0;
		std::size_t SourceBytes = 0, InputBytes = 0, EventCount = 0, DependencyBytes = 0;
		std::size_t GeneratedMetadataBytes = 0;
		std::vector<FCompileGenerationOperation> ReplayOperations;
		// Reserve package operations before live processors can consume the budget.
		std::size_t ReservedReplayOperations = 0;

		std::vector<FPendingFragmentMetadata> PendingFragmentMetadata;
		bool bReplaying = false;

	private:
		std::vector<std::unique_ptr<ICompileProcessor>> Instances;
		bool Active = false;
		std::set<std::pair<std::string, ECompileProcessingPhase>> ProcessedSources;
		std::map<std::string,std::vector<std::size_t>> SourceLines;
	};
	struct FProgram;
	class IHostBindingCatalog;
	bool ProcessCompileDeclarations(FProgram&, ECompileProcessingPhase, const IHostBindingCatalog*, std::vector<FScriptError>&);
	bool RestoreCompileProcessing(FProgram&, std::vector<FScriptError>&);
	void FinalizeCompileProcessing(FProgram&);
	bool ResolveDeferredProgram(FProgram&, const IHostBindingCatalog*, std::vector<FScriptError>&, bool OutlineOnly = false);
	std::vector<FCompileBlockView> GetCompileBlockViews(const FProgram&);
	bool ResolveOutlineDeclarationTypes(FProgram&, const IHostBindingCatalog*, std::vector<FScriptError>&);
	void CollectProgramSourceMetadata(FProgram&);
	std::vector<FAnnotationDeclarationView> GetCompileDeclarationViews(FProgram&);
	bool AttachAnnotationProcessors(FCompileProcessingSession&, const FAnnotationProcessingOptions&, std::string& Error);
}
