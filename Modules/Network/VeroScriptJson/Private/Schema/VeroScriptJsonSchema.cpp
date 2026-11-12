// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJsonSchema.h"
#include "SchemaBackend.h"
#include <map>
#include <set>

namespace VeroScript::Json
{
	using namespace Detail;
	using J = jsoncons::json;
	using FEngine = jsoncons::jsonschema::json_schema<J>;
	static constexpr std::string_view Dialect = "https://json-schema.org/draft/2020-12/schema";

	struct FSchema::FImpl
	{
		explicit FImpl(FEngine InEngine) : Engine(std::move(InEngine))
		{
		}
		FEngine Engine;
	};

	struct FConversionBudget
	{
		FBudget& Work;
		std::uint64_t Nodes = 0;
		std::uint64_t Bytes = 0;
	};
	struct FSchemaResourceFailure
	{
		FError Value;
	};

	static J Convert(const FJsonValue& Value, FConversionBudget& State, std::uint64_t Depth = 1)
	{
		auto& Budget = State.Work;
		Budget.Check();
		if (State.Nodes >= Budget.Limits.Nodes)
		{
			Fail(EError::Limit, "Schema JSON node limit exceeded");
		}
		++State.Nodes;
		if (Depth > std::min<std::uint64_t>(Budget.Limits.Depth, 256))
		{
			Fail(EError::Limit, "Schema JSON depth exceeded");
		}
		const auto& Node = FAccess::Get(Value);
		auto Bytes = [&](std::size_t Count)
		{
			Budget.Check(Count);
			if (Count > Budget.Limits.InputBytes - std::min(State.Bytes, Budget.Limits.InputBytes))
			{
				Fail(EError::Limit, "Schema JSON byte limit exceeded");
			}
			State.Bytes += Count;
		};
		switch (Node.Kind)
		{
		case EKind::Null:
			return J::null();
		case EKind::Boolean:
			return J(Node.Boolean);
		case EKind::Number:
			{
				Bytes(Node.Text.size());
				Decimal(Node.Text, Budget.Limits);
				return J::parse(Node.Text, jsoncons::json_options{}.lossless_number(true));
			}
		case EKind::String:
			{
				Bytes(Node.Text.size());
				if (Node.Text.size() > Budget.Limits.TokenBytes)
				{
					Fail(EError::Limit, "Schema JSON string limit exceeded");
				}
				return J(Node.Text);
			}
		case EKind::Array:
			{
				if (Node.Array.size() > Budget.Limits.Nodes - State.Nodes)
				{
					Fail(EError::Limit, "Schema JSON node limit exceeded");
				}
				J Out(jsoncons::json_array_arg);
				Out.reserve(Node.Array.size());
				for (const auto& Item : Node.Array)
				{
					Out.emplace_back(Convert(Item, State, Depth + 1));
				}
				return Out;
			}
		case EKind::Object:
			{
				if (Node.Object.size() > Budget.Limits.Nodes - State.Nodes)
				{
					Fail(EError::Limit, "Schema JSON node limit exceeded");
				}
				J Out(jsoncons::json_object_arg);
				Out.reserve(Node.Object.size());
				std::vector<const std::pair<std::string, FJsonValue>*> Ordered;
				Ordered.reserve(Node.Object.size());
				for (const auto& Item : Node.Object)
				{
					Bytes(Item.first.size());
					if (Item.first.size() > Budget.Limits.TokenBytes)
					{
						Fail(EError::Limit, "Schema JSON key limit exceeded");
					}
					Ordered.push_back(&Item);
				}
				std::sort(Ordered.begin(),
				    Ordered.end(),
				    [&](const auto* A, const auto* B)
				{
					Budget.Check(1 + std::min(A->first.size(), B->first.size()));
					return A->first < B->first;
				});
				for (const auto* Item : Ordered)
				{
					Out.try_emplace(Item->first, Convert(Item->second, State, Depth + 1));
				}
				return Out;
			}

		default:
			{
				break;
			}
		}
		Fail(EError::Type, "Invalid JSON kind");
	}

	template <class F>
	static bool SchemaGuard(FError& Error, F&& Function)
	{
		try
		{
			return Guard(Error, std::forward<F>(Function));
		}
		catch (const FSchemaResourceFailure& Failure)
		{
			CopyError(Error, Failure.Value);
		}
		catch (const FSchemaRegexSyntax& Failure)
		{
			SetError(Error, EError::Schema, Failure.what());
		}
		catch (const jsoncons::jsonschema::schema_error& Failure)
		{
			SetError(Error, EError::Schema, Failure.what());
		}
		catch (const std::exception& Failure)
		{
			SetError(Error, EError::Schema, Failure.what());
		}
		catch (...)
		{
			SetError(Error, EError::Callback, "Schema callback failed");
		}
		return false;
	}

	static std::string ResourceUri(std::string_view Text)
	{
		if (Text.size() > 65536)
		{
			Fail(EError::Limit, "Schema resource URI exceeds its limit");
		}
		if (Text.empty() || Text.find('\0') != std::string_view::npos)
		{
			Fail(EError::Resource, "Invalid schema resource URI");
		}
		ValidateUtf8(Text);
		for (const unsigned char C : Text)
		{
			if (C <= 0x20 || C >= 0x7f)
			{
				Fail(EError::Resource, "Schema resource URI must use encoded ASCII");
			}
		}
		const jsoncons::uri Uri(Text);
		if (!Uri.is_absolute() || Uri.has_fragment())
		{
			Fail(EError::Resource, "Schema resource URI must be absolute without a fragment");
		}
		return Uri.string();
	}

	static void CheckDialect(const J& Schema, FBudget& Budget)
	{
		Budget.Check();
		if (!Schema.is_object() && !Schema.is_bool())
		{
			Fail(EError::Schema, "Schema must be a boolean or object");
		}
		if (!Schema.is_object())
		{
			return;
		}
		const auto Found = Schema.find("$schema");
		if (Found != Schema.object_range().end())
		{
			if (!FSchemaSemantics::String(Found->value()))
			{
				Fail(EError::Schema, "Schema dialect URI must be a string");
			}
			const auto Name = Found->value().template as<std::string>();
			const jsoncons::uri Uri(Name);
			if (!Uri.encoded_fragment().empty())
			{
				Fail(EError::Schema, "A schema dialect URI cannot have a nonempty fragment");
			}
			ResourceUri(Uri.base().string());
			FSchemaSemantics::Dialect(Name);
		}
	}

	FSchema::FSchema() noexcept = default;

	bool FSchema::TryCompile(const FJsonValue& Schema, FSchema& Out, FError& Error, const FSchemaOptions& Options,
	    const std::vector<FSchemaResource>& Resources, const FSchemaResolver& Resolver, FWork Work)
	{
		return SchemaGuard(Error,
		    [&]
		{
			FBudget Budget(Options.Limits, Work);
			FSchemaWorkScope Scope(Budget);
			FConversionBudget Conversion{Budget};
			const auto Uri =
			    ResourceUri(Options.RetrievalUri.empty() ? std::string_view("urn:veroscript:json-schema:root")
			                                             : std::string_view(Options.RetrievalUri));
			if (!Options.MaxResources || Resources.size() >= Options.MaxResources)
			{
				Fail(EError::Limit, "Schema resource limit exceeded");
			}
			std::map<std::string, J> Registry;
			auto Root = Convert(Schema, Conversion);
			CheckDialect(Root, Budget);
			Registry.emplace(Uri, Root);
			for (const auto& Resource : Resources)
			{
				Budget.Check();
				const auto Id = ResourceUri(Resource.Uri);
				if (!Registry.emplace(Id, Convert(Resource.Schema, Conversion)).second)
				{
					Fail(EError::Resource, "Duplicate schema resource URI");
				}
			}
			const auto BackendOptions =
			    jsoncons::jsonschema::evaluation_options{}.default_base_uri(Uri).require_format_validation(
			        Options.bAssertFormats);
			// The bundled meta-schema owns all seven standard vocabulary resources.
			const auto MetaSource = jsoncons::jsonschema::draft202012::schema_draft202012<J>::get_schema();
			std::map<std::string, J> Builtins;
			Builtins.emplace(std::string(Dialect), MetaSource);
			for (const auto& Part : MetaSource.at("allOf").array_range())
			{
				Builtins.emplace(Part.at("$id").template as<std::string>(), Part);
			}
			Builtins.emplace("https://json-schema.org/draft/2020-12/meta/format-assertion", J::parse(R"({
                    "$schema":"https://json-schema.org/draft/2020-12/schema",
                    "$id":"https://json-schema.org/draft/2020-12/meta/format-assertion",
                    "$dynamicAnchor":"meta","title":"Format vocabulary meta-schema for assertion results",
                    "type":["object","boolean"],"properties":{"format":{"type":"string"}}
                })"));
			const auto Meta = jsoncons::jsonschema::make_json_schema(MetaSource, BackendOptions);
			for (const auto& Entry : Registry)
			{
				const auto Standard = Builtins.find(Entry.first);
				if (Standard != Builtins.end() && !FSchemaSemantics::Equal(Entry.second, Standard->second))
				{
					Fail(EError::Resource, "Cannot replace a built-in schema resource");
				}
			}
			std::function<void(const J&)> Check;
			std::map<std::string, std::shared_ptr<FEngine>> MetaCache;
			auto Resolve = [&](const jsoncons::uri& Reference) -> J
			{
				Budget.Check();
				const auto Id = Reference.base().string();
				ResourceUri(Id);
				const auto Standard = Builtins.find(Id);
				if (Standard != Builtins.end())
				{
					return Standard->second;
				}
				const auto Found = Registry.find(Id);
				if (Found != Registry.end())
				{
					return Found->second;
				}
				if (!Resolver)
				{
					FError Missing;
					SetError(Missing, EError::Resource, "Schema reference is not registered: ");
					Missing.Message += Id;
					throw FSchemaResourceFailure{std::move(Missing)};
				}
				if (Registry.size() >= Options.MaxResources)
				{
					Fail(EError::Limit, "Schema resource limit exceeded");
				}
				FJsonValue Loaded;
				FError Failure;
				bool Success = false;
				try
				{
					Success = Resolver(Id, Loaded, Failure);
				}
				catch (const std::bad_alloc&)
				{
					throw;
				}
				catch (...)
				{
					Fail(EError::Callback, "Schema resolver callback threw");
				}
				if (!Success)
				{
					if (!Failure)
					{
						SetError(Failure, EError::Resource, "Schema resource resolver failed");
					}
					throw FSchemaResourceFailure{std::move(Failure)};
				}
				auto Value = Convert(Loaded, Conversion);
				Registry.emplace(Id, Value);
				Check(Value);
				return Value;
			};
			Check = [&](const J& Source)
			{
				FSchemaStep Step;
				CheckDialect(Source, Budget);
				std::string Version(Dialect);
				if (Source.is_object())
				{
					const auto It = Source.find("$schema");
					if (It != Source.object_range().end())
					{
						Version = jsoncons::uri(It->value().template as<std::string>()).base().string();
					}
				}
				const FEngine* Validator = &Meta;
				if (Version != Dialect)
				{
					auto Found = MetaCache.find(Version);
					if (Found == MetaCache.end())
					{
						const auto Definition = Resolve(jsoncons::uri(Version));
						Found = MetaCache.find(Version);
						if (Found == MetaCache.end())
						{
							// Publish the local compiled validator before checking the
							// definition itself, allowing self/cyclic meta-schema chains.
							auto Compiled = std::make_shared<FEngine>(jsoncons::jsonschema::make_json_schema(
							    Definition, Version, Resolve, BackendOptions));
							Found = MetaCache.emplace(Version, std::move(Compiled)).first;
							Check(Definition);
						}
					}
					Validator = Found->second.get();
				}
				if (!Validator->is_valid(Source))
				{
					Fail(EError::Schema, "Schema does not satisfy its meta-schema");
				}
			};
			for (const auto& Entry : Registry)
			{
				Check(Entry.second);
			}
			auto Result = std::make_shared<FImpl>(
			    jsoncons::jsonschema::make_json_schema(std::move(Root), Uri, Resolve, BackendOptions));
			Out.Impl = std::move(Result);
		});
	}

	bool FSchema::IsCompiled() const noexcept
	{
		return static_cast<bool>(Impl);
	}

	bool FSchema::TryIsValid(
	    const FJsonValue& Instance, bool& Out, FError& Error, const FLimits& Limits, FWork Work) const
	{
		return SchemaGuard(Error,
		    [&]
		{
			const auto Compiled = Impl;
			if (!Compiled)
			{
				Fail(EError::State, "Schema is not compiled");
			}
			FBudget Budget(Limits, Work);
			FSchemaWorkScope Scope(Budget);
			FConversionBudget Conversion{Budget};
			const bool Value = Compiled->Engine.is_valid(Convert(Instance, Conversion));
			Out = Value;
		});
	}

	static FSchemaIssue Issue(const jsoncons::jsonschema::validation_message& Message, FBudget& Budget,
	    std::uint64_t& Remaining, bool& Truncated)
	{
		FSchemaStep Step;
		Budget.Check();
		--Remaining;
		FSchemaIssue Out;
		Out.InstancePointer = Message.instance_location().string();
		Out.SchemaLocation = Message.schema_location().string();
		Out.Keyword = Message.keyword();
		Out.Message = Message.message();
		for (const auto& Detail : Message.details())
		{
			if (!Remaining)
			{
				Truncated = true;
				break;
			}
			Out.Details.push_back(Issue(Detail, Budget, Remaining, Truncated));
		}
		return Out;
	}

	bool FSchema::TryValidate(const FJsonValue& Instance, FSchemaReport& Out, FError& Error, std::uint64_t MaxIssues,
	    const FLimits& Limits, FWork Work) const
	{
		FSchemaReportOptions Options;
		Options.MaxIssues = MaxIssues;
		Options.Limits = Limits;
		return TryValidateWithOptions(Instance, Out, Error, Options, Work);
	}

	bool FSchema::TryValidateWithOptions(const FJsonValue& Instance, FSchemaReport& Out, FError& Error,
	    const FSchemaReportOptions& Options, FWork Work) const
	{
		return SchemaGuard(Error,
		    [&]
		{
			const auto Compiled = Impl;
			if (!Compiled)
			{
				Fail(EError::State, "Schema is not compiled");
			}
			auto MaxIssues = Options.MaxIssues;
			if (!MaxIssues || MaxIssues > 16384)
			{
				Fail(EError::Range, "Schema report limit must be between 1 and 16384");
			}
			if (Options.MaxAnnotations > 16384)
			{
				Fail(EError::Range, "Schema annotation limit must be between 0 and 16384");
			}
			FBudget Budget(Options.Limits, Work);
			FSchemaReport Result;
			FSchemaAnnotationCollector Collector{Result.Annotations, Budget, Options.MaxAnnotations};
			FSchemaWorkScope Scope(Budget, Options.bCollectAnnotations ? &Collector : nullptr);
			FConversionBudget Conversion{Budget};
			Compiled->Engine.validate(Convert(Instance, Conversion),
			    [&](const jsoncons::jsonschema::validation_message& Message)
			{
				if (!MaxIssues)
				{
					Result.bTruncated = true;
					return jsoncons::jsonschema::walk_state::abort;
				}
				Result.bValid = false;
				Result.Issues.push_back(Issue(Message, Budget, MaxIssues, Result.bTruncated));
				if (!MaxIssues)
				{
					Result.bTruncated = true;
					return jsoncons::jsonschema::walk_state::abort;
				}
				return jsoncons::jsonschema::walk_state::advance;
			});
			Result.bAnnotationsTruncated = Collector.Truncated;
			Result.bTruncated |= Collector.Truncated;
			Out = std::move(Result);
		});
	}
} // namespace VeroScript::Json
