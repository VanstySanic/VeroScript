// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJson.h"

namespace VeroScript::Json
{
	struct FSchemaResource
	{
		std::string Uri;
		FJsonValue Schema;
	};

	struct FSchemaOptions
	{
		FLimits Limits;
		// Empty selects urn:veroscript:json-schema:root without allocating a default argument.
		std::string RetrievalUri;
		std::uint64_t MaxResources = 256;
		// Explicitly assert all standard formats even in an annotation-only dialect.
		bool bAssertFormats = false;
	};

	struct FSchemaIssue
	{
		std::string InstancePointer;
		std::string SchemaLocation;
		std::string Keyword;
		std::string Message;
		std::vector<FSchemaIssue> Details;
	};

	struct FSchemaReport
	{
		struct FAnnotation
		{
			std::string InstancePointer, SchemaLocation, EvaluationPath, Keyword;
			FJsonValue Value;
		};

		bool bValid = true;
		// The requested report boundary was reached; further issues may exist.
		bool bTruncated = false;
		std::vector<FSchemaIssue> Issues;
		std::vector<FAnnotation> Annotations;
		bool bAnnotationsTruncated = false;
	};
	struct FSchemaReportOptions
	{
		FLimits Limits;
		std::uint64_t MaxIssues = 256, MaxAnnotations = 256;
		bool bCollectAnnotations = true;
	};

	// Borrowed only during compilation. All resolved schemas are owned by the compiled result.
	using FSchemaResolver = std::function<bool(std::string_view, FJsonValue&, FError&)>;

	class FSchema
	{
	public:
		FSchema() noexcept;
		static bool TryCompile(const FJsonValue& Schema, FSchema& Out, FError& Error,
			const FSchemaOptions& Options = {}, const std::vector<FSchemaResource>& Resources = {},
			const FSchemaResolver& Resolver = {}, FWork Work = {});
		bool IsCompiled() const noexcept;
		bool TryIsValid(
			const FJsonValue& Instance, bool& Out, FError& Error, const FLimits& Limits = {}, FWork Work = {}) const;
		bool TryValidate(const FJsonValue& Instance, FSchemaReport& Out, FError& Error, std::uint64_t MaxIssues = 256,
			const FLimits& Limits = {}, FWork Work = {}) const;
		bool TryValidateWithOptions(const FJsonValue& Instance, FSchemaReport& Out, FError& Error,
			const FSchemaReportOptions& Options = {}, FWork Work = {}) const;

	private:
		struct FImpl;
		std::shared_ptr<const FImpl> Impl;
	};
} // namespace VeroScript::Json
