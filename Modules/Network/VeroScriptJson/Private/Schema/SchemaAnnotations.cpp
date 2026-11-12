// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "SchemaSemantics.h"
#include <set>

namespace VeroScript::Json::Detail
{
	static std::string CanonicalLocation(std::string_view Location, std::size_t LiteralStart)
	{
		std::string Out;
		const auto Hash = Location.find('#');
		if (Hash == std::string_view::npos)
		{
			return std::string(Location);
		}
		Out.append(Location.data(), Hash + 1);
		static constexpr char Hex[] = "0123456789ABCDEF";
		for (std::size_t I = Hash + 1; I < Location.size(); ++I)
		{
			FSchemaSemantics::Check();
			const auto C = static_cast<unsigned char>(Location[I]);
			const bool Allowed =
				(C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') ||
				std::string_view("-._~!$&'()*+,;=:@/?").find(static_cast<char>(C)) != std::string_view::npos ||
				(C == '%' && I < LiteralStart);
			if (Allowed)
			{
				Out.push_back(static_cast<char>(C));
			}
			else
			{
				Out.push_back('%');
				Out.push_back(Hex[C >> 4]);
				Out.push_back(Hex[C & 15]);
			}
		}
		return Out;
	}
	std::string SchemaAnnotationLocation(std::string_view Base, std::string_view Keyword)
	{
		std::string Location(Base);
		if (Location.find('#') == std::string::npos)
		{
			Location.push_back('#');
		}
		Location.push_back('/');
		const auto TokenStart = Location.size();
		for (const char C : Keyword)
		{
			if (C == '~')
			{
				Location += "~0";
			}
			else if (C == '/')
			{
				Location += "~1";
			}
			else
			{
				Location.push_back(C);
			}
		}
		return CanonicalLocation(Location, TokenStart);
	}

	bool FSchemaAnnotationCollector::Available()
	{
		Budget.Check();
		if (Values.size() < Maximum)
		{
			return true;
		}
		Truncated = true;
		return false;
	}
	void FSchemaAnnotationCollector::Charge(std::uint64_t Size)
	{
		Budget.Check(Size + 1);
		const auto Remaining = Budget.Limits.OutputBytes - std::min(Bytes, Budget.Limits.OutputBytes);
		if (Size > Remaining || FSchemaSemantics::DiagnosticBytes() > Remaining - Size)
		{
			Fail(EError::Limit, "Schema annotation byte budget exceeded");
		}
		Bytes += Size;
	}
	void FSchemaAnnotationCollector::Node(std::uint64_t Depth)
	{
		Budget.Check();
		if (Nodes >= Budget.Limits.Nodes || Depth > std::min<std::uint64_t>(Budget.Limits.Depth, 256))
		{
			Fail(EError::Limit, "Schema annotation node/depth budget exceeded");
		}
		++Nodes;
		Charge(1);
	}
	FSchemaAnnotationScope::FSchemaAnnotationScope(FSchemaAnnotationCollector* Value) : Collector(Value)
	{
		if (!Collector)
		{
			return;
		}
		Mark = Collector->Values.size();
		Bytes = Collector->Bytes;
		Nodes = Collector->Nodes;
		Truncated = Collector->Truncated;
	}
	FSchemaAnnotationScope::~FSchemaAnnotationScope()
	{
		if (!Collector || bCommitted)
		{
			return;
		}
		Collector->Values.resize(Mark);
		Collector->Bytes = Bytes;
		Collector->Nodes = Nodes;
		Collector->Truncated = Truncated;
	}

	static FJsonValue Clone(const jsoncons::json&, FSchemaAnnotationCollector&, std::uint64_t Depth = 1);
	FSchemaAnnotationPlan MakeSchemaAnnotationPlan(const jsoncons::json& Schema, std::string_view Location,
		bool Validation, bool Applicator, bool Unevaluated, bool ContentVocabulary)
	{
		// Standard assertions/applicators/identifiers are not ordinary metadata.
		// Unrecognized keywords retain their values as annotations. $comment is
		// explicitly excluded by JSON Schema Core 8.3.
		static const std::set<std::string, std::less<>> Core = {
			"$id", "$schema", "$ref", "$anchor", "$dynamicRef", "$dynamicAnchor", "$defs", "$vocabulary", "$comment"};
		static const std::set<std::string, std::less<>> ValidationKeys = {"type",
			"enum",
			"const",
			"multipleOf",
			"minimum",
			"maximum",
			"exclusiveMinimum",
			"exclusiveMaximum",
			"minLength",
			"maxLength",
			"pattern",
			"minItems",
			"maxItems",
			"uniqueItems",
			"minContains",
			"maxContains",
			"minProperties",
			"maxProperties",
			"required",
			"dependentRequired"};
		static const std::set<std::string, std::less<>> ApplicatorKeys = {"allOf",
			"anyOf",
			"oneOf",
			"not",
			"if",
			"then",
			"else",
			"dependentSchemas",
			"properties",
			"patternProperties",
			"additionalProperties",
			"propertyNames",
			"prefixItems",
			"items",
			"contains"};

		FSchemaAnnotationPlan Plan;
		if (!Schema.is_object())
		{
			return Plan;
		}
		std::vector<FSchemaReport::FAnnotation> Unused;
		FSchemaAnnotationCollector Storage{Unused, FSchemaSemantics::Budget(), 0};
		for (const auto& Entry : Schema.object_range())
		{
			FSchemaSemantics::Check(Entry.key().size() + 1);
			if (Core.count(Entry.key()) || (Validation && ValidationKeys.count(Entry.key())) ||
				(Applicator && ApplicatorKeys.count(Entry.key())) ||
				(Unevaluated && (Entry.key() == "unevaluatedProperties" || Entry.key() == "unevaluatedItems")))
			{
				continue;
			}
			const bool Content =
				ContentVocabulary && (Entry.key() == "contentEncoding" || Entry.key() == "contentMediaType" ||
										 Entry.key() == "contentSchema");
			if (Content && Entry.key() == "contentSchema" && !Schema.contains("contentMediaType"))
			{
				continue;
			}
			Plan.push_back({std::string(Entry.key()),
				SchemaAnnotationLocation(Location, Entry.key()),
				Clone(Entry.value(), Storage),
				Content});
		}
		return Plan;
	}

	static FJsonValue Clone(const jsoncons::json& Value, FSchemaAnnotationCollector& C, std::uint64_t Depth)
	{
		FSchemaStep Step;
		C.Node(Depth);
		FNode Node;
		if (Value.is_null())
		{
			return {};
		}
		if (Value.is_bool())
		{
			Node.Kind = EKind::Boolean;
			Node.Boolean = Value.as<bool>();
		}
		else if (Value.is_number())
		{
			Node.Kind = EKind::Number;
			Node.Text = Value.as<std::string>();
			C.Charge(Node.Text.size());
			Node.Decimal = Decimal(Node.Text, C.Budget.Limits);
		}
		else if (Value.is_string())
		{
			Node.Kind = EKind::String;
			C.Charge(Value.as_string_view().size());
			Node.Text = Value.as<std::string>();
		}
		else if (Value.is_array())
		{
			Node.Kind = EKind::Array;
			for (const auto& Item : Value.array_range())
			{
				Node.Array.push_back(Clone(Item, C, Depth + 1));
			}
		}
		else if (Value.is_object())
		{
			Node.Kind = EKind::Object;
			for (const auto& Item : Value.object_range())
			{
				C.Charge(Item.key().size());
				Node.Object.emplace_back(std::string(Item.key()), Clone(Item.value(), C, Depth + 1));
			}
		}
		else
		{
			Fail(EError::Schema, "Unsupported schema annotation value");
		}
		Node.Depth = TreeDepth(Node);
		return FAccess::Make(std::move(Node));
	}
	static void Account(const FJsonValue& Value, FSchemaAnnotationCollector& C, std::uint64_t Depth = 1)
	{
		C.Node(Depth);
		const auto& N = FAccess::Get(Value);
		C.Charge(N.Text.size());
		for (const auto& Item : N.Array)
		{
			Account(Item, C, Depth + 1);
		}
		for (const auto& Item : N.Object)
		{
			C.Charge(Item.first.size());
			Account(Item.second, C, Depth + 1);
		}
	}
	void CollectEvaluationAnnotation(std::string_view Keyword, std::string_view Location, std::string_view Path,
		std::string_view Instance, FJsonValue Value, FSchemaAnnotationCollector& C)
	{
		auto Canonical = CanonicalLocation(Location, Location.size());
		C.Charge(Keyword.size() + Canonical.size() + Path.size() + Instance.size() + 32);
		C.Values.push_back(
			{std::string(Instance), std::move(Canonical), std::string(Path), std::string(Keyword), std::move(Value)});
	}
	void CollectSchemaAnnotations(const FSchemaAnnotationPlan& Plan, const jsoncons::json& Instance,
		std::string_view InstancePointer, std::string_view EvaluationPath, FSchemaAnnotationCollector* C)
	{
		if (!C)
		{
			return;
		}
		for (const auto& Entry : Plan)
		{
			if (Entry.bStringOnly && !FSchemaSemantics::String(Instance))
			{
				continue;
			}
			if (!C->Available())
			{
				continue;
			}
			auto Path = jsoncons::jsonpointer::json_pointer(std::string(EvaluationPath));
			Path /= Entry.Keyword;
			Account(Entry.Value, *C);
			CollectEvaluationAnnotation(
				Entry.Keyword, Entry.SchemaLocation, Path.string(), InstancePointer, Entry.Value, *C);
		}
	}
} // namespace VeroScript::Json::Detail
