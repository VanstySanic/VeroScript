// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAnnotationProcessing.h"
#include "VeroScriptSourceMetadata.h"
#include "VeroScriptResources.h"
#include <optional>

namespace VeroScript
{
	struct FProgram;
	class FModuleEnvironment;
	class ICompileProcessor;
	class FCompileProcessorRegistry;
	class FCompileProcessingSession;

	enum class ECompileProcessingPhase : std::uint8_t
	{
		RawSource,
		ActiveSource,
		Types,
		Members,
		Bodies
	};
	constexpr std::uint32_t CompilePhaseMask(ECompileProcessingPhase P)
	{
		return 1u << static_cast<unsigned>(P);
	}
	enum class ECompileProcessingEvent : std::uint8_t
	{
		Module,
		Declaration,
		Annotation,
		Comment,
		Syntax
	};
	enum class ECompileOperation : std::uint8_t
	{
		EditSource = 1, AddDeclaration = 2, ReplaceDeclaration = 4,
		CompleteBody = 8, ReplaceBody = 16, SetMetadata = 32
	};
	constexpr std::uint32_t CompileOperationMask(ECompileOperation O)
	{
		return static_cast<std::uint32_t>(O);
	}
	struct FCompileSubscription
	{
		ECompileProcessingEvent Event = ECompileProcessingEvent::Declaration;
		std::uint32_t Targets = AnnotationAllTargets;
		std::string AnnotationName, SyntaxKind;
		std::optional<ESourceCommentKind> CommentKind;
		// Zero selects the phases in which this event exists.
		std::uint32_t Phases = 0;
	};
	struct FCompileScope
	{
		std::string ModuleId, NamespacePath, OwnerId;
		std::uint64_t Snapshot = 0;
		std::string ScopeId, InsertionBeforeId;
	};
	struct FCompileMetadata
	{
		// A fragment-local range selects exactly one parsed descendant of Kind.
		struct FChild
		{
			std::size_t Begin = 0, End = 0;
			std::string Kind;
			FSourceNodeMetadata Metadata;
		};

		std::optional<std::vector<FDeclarationAnnotation>> Annotations;
		std::optional<std::vector<FSourceComment>> Comments;

		std::vector<FChild> Children;
	};
	struct FCompileStatementAnchor
	{
		std::string Id;
		FSourceRange Range;
	};
	struct FCompileBlockView
	{
		std::string ModuleId, ScopeId, ParentScopeId, OwnerId, EndAnchorId;
		std::vector<FCompileStatementAnchor> Statements;
	};
	struct FCompileGenerationOperation
	{
		ECompileOperation Kind = ECompileOperation::AddDeclaration;
		ECompileProcessingPhase Phase = ECompileProcessingPhase::Members;
		FCompileScope Scope;
		std::string TargetId, Source, ProcessorId, GenerationId, InputDigest;
		std::uint64_t Snapshot = 0;
		FSourceRange EditRange;
		FSourceOrigin Origin;
		std::vector<FSourceDocument::FMapping> SourceMappings;
		FCompileMetadata Metadata;
	};
	struct FCompileProcessingContext
	{
		const FAnnotationDeclarationView* FindDeclaration(std::string_view Id) const;

		ECompileProcessingPhase Phase;
		ECompileProcessingEvent Event;
		std::uint64_t Snapshot = 0;
		std::string ModuleId, EventId;
		const FAnnotationDeclarationView* Declaration = nullptr;
		const FDeclarationAnnotation* Annotation = nullptr;
		const FSourceComment* Comment = nullptr;
		const FSourceNodeMetadata* Syntax = nullptr;
		const FSourceDocument* Source = nullptr;
		const std::vector<FAnnotationDeclarationView>& Declarations;
		const FSourceMetadataSnapshot& SourceMetadata;
		std::function<void()> CheckCancelled;
		std::function<bool(std::string_view, std::string&)> ReadDependency;
		// Borrowed for this callback; null in source phases. Deferred bodies are
		// identified explicitly in Types/Members and resolved before Bodies.
		const FProgram* Program = nullptr;
		// Reads are recorded in the same dependency ledger as ReadDependency.
		std::function<bool(const FResourceRequest&, FResourceData&)> ReadResource;
		std::string ResourceRequester;
		// Borrowed for this callback. Only explicit source blocks are insertion scopes.
		const std::vector<FCompileBlockView>* Blocks = nullptr;

	};
	struct FCompileProcessorDesc
	{
		std::shared_ptr<void> ModuleOwner;
		FBindingOrigin Origin;
		std::string ProcessorId, Version, ConfigurationKey;
		std::uint32_t Phases = 0, Operations = 0;
		std::vector<FCompileSubscription> Subscriptions;
		std::int32_t Order = 0;
		std::function<std::unique_ptr<ICompileProcessor>()> Factory;
		bool Cacheable = true;
	};

	struct FCompileProcessingOptions
	{
		std::shared_ptr<const FCompileProcessorRegistry> Registry;
		std::function<void()> CheckCancelled;
		std::function<bool(std::string_view, std::string&)> ReadDependency;
		std::vector<std::string> WritableModules;
		std::vector<FAnnotationDefinition> AnnotationDefinitions;
		ECommentRetention CommentRetention = ECommentRetention::Full;
		std::size_t MaxOperations = 4096, MaxSourceBytes = 16 * 1024 * 1024;
		std::size_t MaxInputBytes = 64 * 1024 * 1024, MaxEvents = 262144, MaxDependencies = 4096;
		// Immutable implementation/profile identity, including script extensions.
		std::string ImplementationFingerprint;
		std::vector<FAnnotationDependency> InitialDependencies;
		bool bCacheable = true;
		// Source bootstrap returns protected byte intervals after masking directives.
		std::function<std::vector<FSourceRange>(std::string_view, std::string&)> PrepareSource;
		std::shared_ptr<const FModuleEnvironment> ModuleEnvironment;
	};

	class FCompileProcessingOutput
	{
	public:
		void EditSource(FSourceRange Range, std::string Replacement, FSourceOrigin Origin = {},
			std::vector<FSourceDocument::FMapping> Mappings = {});
		void AddDeclaration(FCompileScope Scope, std::string Source, FCompileMetadata Metadata = {});
		void ReplaceDeclaration(std::string Id, std::uint64_t Snapshot, std::string Source, FCompileMetadata Metadata = {});
		void CompleteBody(std::string Id, std::uint64_t Snapshot, std::string Source, FCompileMetadata Metadata = {});
		void ReplaceBody(std::string Id, std::uint64_t Snapshot, std::string Source, FCompileMetadata Metadata = {});
		void SetMetadata(std::string Id, std::uint64_t Snapshot, FCompileMetadata Metadata);
		void SubmitOperation(FCompileGenerationOperation Operation);
		void ReportDiagnostic(FScriptError Error);
		const std::vector<FCompileGenerationOperation>& GetOperations() const
		{
			return Operations;
		}
		const std::vector<FScriptError>& GetDiagnostics() const
		{
			return Diagnostics;
		}

	private:
		std::vector<FCompileGenerationOperation> Operations;
		std::vector<FScriptError> Diagnostics;
	};
	class ICompileProcessor
	{
	public:
		virtual ~ICompileProcessor() = default;
		virtual void Process(const FCompileProcessingContext&, FCompileProcessingOutput&) = 0;
	};
	class FCompileProcessorRegistry
	{
	public:
		bool Register(FCompileProcessorDesc, std::string& Error);
		void Freeze();
		bool IsFrozen() const
		{
			return bFrozen;
		}
		const std::vector<FCompileProcessorDesc>& GetProcessors() const
		{
			return Processors;
		}
		std::string GetFingerprint() const;
		bool HasPhase(ECompileProcessingPhase) const;
	private:
		bool bFrozen = false;
		std::vector<FCompileProcessorDesc> Processors;
	};
}
