// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAnnotation.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript
{
	enum class ESourceMetadataQuery : std::int32_t
	{
		Name, RawText, Text, Documentation, Detail, Provider, SourceId,
		NodeId, OwnerId, TypeName, Kind, Activity, Association,
		IsValid, AnnotationCount, ArgumentCount, CommentCount, SourceNodeCount,
		ParameterCount, CommentsAvailable, CommentRetention,
		ScalarKind, SignedValue, UnsignedValue, FloatValue, BoolValue, Value,
		BeginLine, BeginColumn, EndLine, EndColumn, ParentId,
		OriginSourceId, OriginProcessorId, OriginGenerationId, OriginLine, OriginColumn, TargetMask,
		MatchesName, CaseSensitive
	};
	enum class ESourceCommentKind : std::uint8_t
	{
		Line,
		Block,
		DocumentationLine,
		DocumentationBlock
	};
	enum class ESourceActivity : std::uint8_t
	{
		Unknown,
		Active,
		Inactive,
		Removed
	};
	enum class ECommentRetention : std::uint8_t
	{
		Full,
		Documentation,
		NoComments,
		Unavailable
	};
	enum class ECommentAssociation : std::uint8_t
	{
		Module,
		Scope,
		Leading,
		Trailing,
		Body,
		Explicit
	};
	struct FSourceRange
	{
		std::string SourceId;
		std::size_t Begin = 0, End = 0;
		FAnnotationSourceRange Position;
	};
	struct FSourceOrigin
	{
		FSourceRange Range;
		std::string ProcessorId, GenerationId;
	};
	struct FSourceComment
	{
		std::string Id, ModuleId, OwnerId, RawText, Text;
		ESourceCommentKind Kind = ESourceCommentKind::Line;
		ESourceActivity Activity = ESourceActivity::Unknown;
		ECommentAssociation Association = ECommentAssociation::Module;
		FSourceRange Range;
		FSourceOrigin Origin;
	};
	struct FAnnotationArgumentDefinition
	{
		std::string Name, Detail, Documentation;
		std::vector<FAnnotationArgumentDefinition> Children;
	};
	struct FAnnotationDefinition
	{
		std::string Id, ProviderId, Name, Detail, Documentation;
		std::vector<FAnnotationArgumentDefinition> Arguments;
		std::uint32_t Targets = 131071;
		// A single definition owns all accepted ASCII spelling variants.
		bool CaseSensitive = true;
	};
	struct FSourceNodeMetadata
	{
		std::string Id, OwnerId, ParentId, Kind;
		std::string Name, ModuleId, TypeName;
		std::uint32_t Target = 0;
		ECommentRetention CommentRetention = ECommentRetention::Full;
		FSourceRange Range;
		FSourceOrigin Origin;
		std::vector<FDeclarationAnnotation> Annotations;
		std::vector<FSourceComment> Comments;
		std::string Documentation;
	};
	struct FSourceDocument
	{
		// Output intervals map to owned input intervals; unmodified text retains identity.
		struct FMapping
		{
			std::size_t Begin = 0, End = 0;
			FSourceOrigin Origin;
		};

		std::string Id, ModuleId, Text;
		std::vector<std::size_t> LineOffsets;
		std::vector<FSourceComment> Comments;

		std::vector<FMapping> Mappings;
	};
	struct FSourceMetadataSnapshot
	{
		const FSourceDocument* FindDocument(const std::string& Id) const;
		const FSourceNodeMetadata* FindNode(const std::string& Id) const;
		const FAnnotationDefinition* FindDefinition(const std::string& Id) const;
		// Matches the resolved definition policy, retaining the annotation's source
		// spelling. Missing/unregistered definitions always use exact matching.
		bool MatchesAnnotationName(const FDeclarationAnnotation&, std::string_view Name,
			bool AllowOptionalAt = false) const;

		std::vector<FSourceDocument> Documents;
		std::vector<FSourceNodeMetadata> Nodes;
		std::vector<FAnnotationDefinition> Definitions;
		ECommentRetention Retention = ECommentRetention::Full;
	};
	// Recognizes VeroScript literals/comments without requiring otherwise valid syntax.
	std::vector<FSourceComment> CollectSourceComments(std::string_view Source, std::string_view SourceId,
		ESourceActivity Activity = ESourceActivity::Unknown);
	std::string FormatSourceDocumentation(const std::vector<FSourceComment>& Comments);
	// Follows owned source mappings; unknown mappings end the chain. Snapshots
	// retain line offsets so this also works after source text is discarded.
	std::vector<FSourceOrigin> ResolveSourceOrigins(const FSourceMetadataSnapshot&, FSourceRange Range);
	bool AnnotationArgumentInt64(const FAnnotationArgument&, std::int64_t& Value);
	bool AnnotationArgumentUInt64(const FAnnotationArgument&, std::uint64_t& Value);
	class IHostBindingCatalog;
	std::vector<FAnnotationDefinition> GetAnnotationDefinitions(const IHostBindingCatalog&);
}
