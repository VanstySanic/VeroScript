// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "SchemaSemantics.h"

namespace VeroScript::Json::Detail
{
	static thread_local FSchemaWorkState* ActiveWork = nullptr;

	FSchemaWorkScope::FSchemaWorkScope(FBudget& Budget, FSchemaAnnotationCollector* Annotations)
		: State{Budget}, Previous(ActiveWork)
	{
		State.Annotations = Annotations;
		ActiveWork = &State;
	}
	FSchemaAnnotationCollector* FSchemaSemantics::Annotations() noexcept
	{
		return ActiveWork ? ActiveWork->Annotations : nullptr;
	}
	std::uint64_t FSchemaSemantics::DiagnosticBytes() noexcept
	{
		return ActiveWork ? ActiveWork->ErrorBytes : 0;
	}
	bool FSchemaSemantics::EvaluationKeyword(std::string_view Name) noexcept
	{
		return Name == "unevaluatedProperties" || Name == "items" || Name == "contains" || Name == "unevaluatedItems";
	}
	FSchemaWorkScope::~FSchemaWorkScope()
	{
		ActiveWork = Previous;
	}
	FSchemaStep::FSchemaStep() : State(ActiveWork)
	{
		FSchemaSemantics::Check();
		char Marker;
		const auto Current = reinterpret_cast<std::uintptr_t>(&Marker);
		const auto Base = reinterpret_cast<std::uintptr_t>(State);
		const auto Distance = Current > Base ? Current - Base : Base - Current;
		if (State->Depth >= 512 || Distance >= 128 * 1024)
		{
			Fail(EError::Limit, "Schema evaluation depth exceeded");
		}
		++State->Depth;
	}
	FSchemaStep::~FSchemaStep()
	{
		--State->Depth;
	}
	FBudget& FSchemaSemantics::Budget()
	{
		if (!ActiveWork)
		{
			Fail(EError::Type, "Schema backend requires an active work scope");
		}
		return ActiveWork->Budget;
	}
	void FSchemaSemantics::Check(std::uint64_t Units)
	{
		Budget().Check(Units);
	}
	bool FSchemaSemantics::RegexValid(std::string_view Pattern)
	{
		try
		{
			FSchemaRegex Compiled(Pattern);
			return true;
		}
		catch (const FSchemaRegexSyntax&)
		{
			return false;
		}
	}
	void FSchemaSemantics::Report(std::size_t Bytes)
	{
		Check(Bytes + 1);
		const auto Limit = Budget().Limits.OutputBytes;
		const auto Used = std::min(ActiveWork->ErrorBytes, Limit);
		const auto Annotations = ActiveWork->Annotations ? ActiveWork->Annotations->Bytes : 0;
		if (++ActiveWork->Errors > 16384 || Bytes > Limit - Used || Annotations > Limit - Used - Bytes)
		{
			Fail(EError::Limit, "Schema diagnostic budget exceeded");
		}
		ActiveWork->ErrorBytes += Bytes;
	}
	void FSchemaSemantics::Dialect(std::string_view Uri)
	{
		Check(Uri.size() + 1);
		if (!Uri.empty() && Uri.back() == '#')
		{
			Uri.remove_suffix(1);
		}
		static constexpr std::string_view Older[] = {"http://json-schema.org/draft-04/schema",
			"https://json-schema.org/draft-04/schema",
			"http://json-schema.org/draft-06/schema",
			"https://json-schema.org/draft-06/schema",
			"http://json-schema.org/draft-07/schema",
			"https://json-schema.org/draft-07/schema",
			"https://json-schema.org/draft/2019-09/schema"};
		for (const auto Version : Older)
		{
			if (Uri == Version)
			{
				Fail(EError::Unsupported, "Only JSON Schema 2020-12 and derived dialects are supported");
			}
		}
	}
	void FSchemaSemantics::Vocabulary(const std::unordered_map<std::string, bool>& Values)
	{
		static constexpr std::string_view Known[] = {"https://json-schema.org/draft/2020-12/vocab/core",
			"https://json-schema.org/draft/2020-12/vocab/applicator",
			"https://json-schema.org/draft/2020-12/vocab/unevaluated",
			"https://json-schema.org/draft/2020-12/vocab/validation",
			"https://json-schema.org/draft/2020-12/vocab/meta-data",
			"https://json-schema.org/draft/2020-12/vocab/format-annotation",
			"https://json-schema.org/draft/2020-12/vocab/format-assertion",
			"https://json-schema.org/draft/2020-12/vocab/content"};
		for (const auto& Value : Values)
		{
			Check(Value.first.size() + 1);
			if (!Formats::Validate("uri", Value.first))
			{
				Fail(EError::Schema, "Vocabulary identifier must be an absolute URI");
			}
			if (Value.second && std::find(std::begin(Known), std::end(Known), Value.first) == std::end(Known))
			{
				Fail(EError::Unsupported, "Schema requires an unsupported vocabulary");
			}
		}
	}
} // namespace VeroScript::Json::Detail
