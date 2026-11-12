// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJsonSchema.h"
#include "../Internal/JsonInternal.h"
#include <jsoncons/json.hpp>

namespace VeroScript::Json::Detail
{
	struct FSchemaAnnotationTemplate
	{
		std::string Keyword, SchemaLocation;
		// External resources can be temporary backend factory inputs. Own the
		// annotation snapshot rather than borrowing their storage.
		FJsonValue Value;
		bool bStringOnly = false;
	};
	using FSchemaAnnotationPlan = std::vector<FSchemaAnnotationTemplate>;
	std::string SchemaAnnotationLocation(std::string_view Base, std::string_view Keyword);
	struct FSchemaAnnotationCollector
	{
		std::vector<FSchemaReport::FAnnotation>& Values;
		FBudget& Budget;
		std::uint64_t Maximum;
		std::uint64_t Bytes = 0, Nodes = 0;
		bool Truncated = false;
		bool Available();
		void Charge(std::uint64_t Size);
		void Node(std::uint64_t Depth);
	};
	class FSchemaAnnotationScope
	{
	public:
		explicit FSchemaAnnotationScope(FSchemaAnnotationCollector*);
		~FSchemaAnnotationScope();
		FSchemaAnnotationScope(const FSchemaAnnotationScope&) = delete;
		FSchemaAnnotationScope& operator=(const FSchemaAnnotationScope&) = delete;
		void Commit() noexcept
		{
			bCommitted = true;
		}

	private:
		FSchemaAnnotationCollector* Collector;
		std::size_t Mark = 0;
		std::uint64_t Bytes = 0, Nodes = 0;
		bool Truncated = false, bCommitted = false;
	};

	FSchemaAnnotationPlan MakeSchemaAnnotationPlan(const jsoncons::json&, std::string_view Location,
		bool Validation = true, bool Applicator = true, bool Unevaluated = true, bool Content = true);
	void CollectSchemaAnnotations(const FSchemaAnnotationPlan&, const jsoncons::json& Instance,
		std::string_view InstancePointer, std::string_view EvaluationPath, FSchemaAnnotationCollector*);
	void CollectEvaluationAnnotation(std::string_view Keyword, std::string_view SchemaLocation,
		std::string_view EvaluationPath, std::string_view InstancePointer, FJsonValue Value,
		FSchemaAnnotationCollector&);
} // namespace VeroScript::Json::Detail
