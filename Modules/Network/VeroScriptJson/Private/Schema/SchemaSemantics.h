// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "SchemaArithmetic.h"
#include "SchemaRegex.h"
#include "SchemaDestruction.h"
#define VEROSCRIPT_JSON_BOUNDED_CLEAR ::VeroScript::Json::Detail::ClearSchemaChildren
#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonpointer/jsonpointer.hpp>
#include "SchemaAnnotations.h"
#include "SchemaFormats.h"

namespace VeroScript::Json::Detail
{
	struct FSchemaWorkState
	{
		FBudget& Budget;
		std::uint64_t Depth = 0;
		std::uint64_t Errors = 0;
		std::uint64_t ErrorBytes = 0;
		FSchemaAnnotationCollector* Annotations = nullptr;
	};

	class FSchemaWorkScope
	{
	public:
		explicit FSchemaWorkScope(FBudget& Budget, FSchemaAnnotationCollector* Annotations = nullptr);
		~FSchemaWorkScope();
		FSchemaWorkScope(const FSchemaWorkScope&) = delete;
		FSchemaWorkScope& operator=(const FSchemaWorkScope&) = delete;

	private:
		FSchemaWorkState State;
		FSchemaWorkState* Previous;
	};

	class FSchemaStep
	{
	public:
		FSchemaStep();
		~FSchemaStep();
		FSchemaStep(const FSchemaStep&) = delete;
		FSchemaStep& operator=(const FSchemaStep&) = delete;

	private:
		FSchemaWorkState* State;
	};

	struct FSchemaSemantics
	{
		using FRegex = FSchemaRegex;
		using FAnnotationPlan = FSchemaAnnotationPlan;
		using FAnnotationScope = FSchemaAnnotationScope;
		static FSchemaAnnotationCollector* Annotations() noexcept;
		static std::uint64_t DiagnosticBytes() noexcept;
		static FAnnotationPlan AnnotationPlan(const jsoncons::json& Schema, std::string_view Location,
			bool Validation = true, bool Applicator = true, bool Unevaluated = true, bool Content = true)
		{
			return MakeSchemaAnnotationPlan(Schema, Location, Validation, Applicator, Unevaluated, Content);
		}
		static bool EvaluationKeyword(std::string_view Name) noexcept;
		static std::string AnnotationLocation(std::string_view Base, std::string_view Keyword)
		{
			return SchemaAnnotationLocation(Base, Keyword);
		}
		static void Collect(const FAnnotationPlan& Plan, const jsoncons::json& Instance, std::string_view Pointer,
			std::string_view Path)
		{
			CollectSchemaAnnotations(Plan, Instance, Pointer, Path, Annotations());
		}
		template <class R>
		static void Evaluated(std::string_view Keyword, std::string_view Location, std::string_view Path,
			std::string_view Pointer, const jsoncons::json& Instance, const R& Results, const R* Before)
		{
			auto* C = Annotations();
			const bool Properties = Keyword == "properties" || Keyword == "patternProperties" ||
									Keyword == "additionalProperties" || Keyword == "unevaluatedProperties";
			if (!C || (Properties ? !Instance.is_object() : !Instance.is_array()) || !C->Available())
			{
				return;
			}
			FNode Node;
			Node.Kind = EKind::Array;
			if (Properties)
			{
				for (const auto& Entry : Instance.object_range())
				{
					C->Budget.Check(Entry.key().size() + 1);
					if (!Results.evaluated_properties.count(Entry.key()) ||
						(Before && Before->evaluated_properties.count(Entry.key())))
					{
						continue;
					}
					C->Node(2);
					C->Charge(Entry.key().size());
					FNode Name;
					Name.Kind = EKind::String;
					Name.Text = Entry.key();
					Node.Array.push_back(FAccess::Make(std::move(Name)));
				}
			}
			else
			{
				// Normalize intervals once, avoiding a scan of all ranges for every item.
				auto Ranges = [&](const R& Input)
				{
					std::vector<std::pair<std::size_t, std::size_t>> Out;
					for (const auto& Range : Input.evaluated_items)
					{
						C->Budget.Check();
						if (Range.start() < Range.end())
						{
							Out.emplace_back(Range.start(), Range.end());
						}
					}
					std::sort(Out.begin(),
						Out.end(),
						[&](const auto& A, const auto& B)
						{
							C->Budget.Check();
							return A < B;
						});
					std::size_t Count = 0;
					for (const auto& Range : Out)
					{
						C->Budget.Check();
						if (Count && Range.first <= Out[Count - 1].second)
						{
							Out[Count - 1].second = std::max(Out[Count - 1].second, Range.second);
						}
						else
						{
							Out[Count++] = Range;
						}
					}
					Out.resize(Count);
					return Out;
				};
				const auto Intervals = Ranges(Results);
				std::size_t Total = 0;
				for (const auto& Range : Intervals)
				{
					Total += Range.second - Range.first;
				}
				if (Keyword == "items" || Keyword == "unevaluatedItems")
				{
					std::size_t Previous = 0;
					if (Before)
					{
						for (const auto& Range : Ranges(*Before))
						{
							Previous += Range.second - Range.first;
						}
					}
					if (Total <= Previous)
					{
						return;
					}
					Node.Kind = EKind::Boolean;
					Node.Boolean = true;
				}
				else if (Total == Instance.size())
				{
					Node.Kind = EKind::Boolean;
					Node.Boolean = true;
				}
				else if (Keyword == "prefixItems")
				{
					if (!Total)
					{
						return;
					}
					Node.Kind = EKind::Number;
					Node.Text = std::to_string(Intervals.back().second - 1);
					Node.Decimal = Decimal(Node.Text, C->Budget.Limits);
					C->Charge(Node.Text.size());
				}
				else
				{
					for (const auto& Range : Intervals)
					{
						for (auto I = Range.first; I < Range.second; ++I)
						{
							C->Node(2);
							FNode Index;
							Index.Kind = EKind::Number;
							Index.Text = std::to_string(I);
							Index.Decimal = Decimal(Index.Text, C->Budget.Limits);
							C->Charge(Index.Text.size());
							Node.Array.push_back(FAccess::Make(std::move(Index)));
						}
					}
				}
			}
			C->Node(1);
			Node.Depth = TreeDepth(Node);
			auto Eval = jsoncons::jsonpointer::json_pointer(std::string(Path));
			Eval /= std::string(Keyword);
			CollectEvaluationAnnotation(Keyword, Location, Eval.string(), Pointer, FAccess::Make(std::move(Node)), *C);
		}
		static FBudget& Budget();
		static void Check(std::uint64_t Units = 1);
		static bool RegexValid(std::string_view Pattern);
		static void Report(std::size_t Bytes);
		static void Dialect(std::string_view Uri);
		static bool FormatKnown(std::string_view Name)
		{
			return Formats::Known(Name);
		}
		static bool Format(std::string_view Name, std::string_view Text)
		{
			return Formats::Validate(Name, Text);
		}
		static void Vocabulary(const std::unordered_map<std::string, bool>&);

		template <class J>
		static FDecimal Number(const J& Value)
		{
			Check();
			if (!Value.is_number())
			{
				Fail(EError::Type, "Schema expected a number");
			}
			const auto Text = Value.template as<std::string>();
			Check(Text.size());
			return Decimal(Text, Budget().Limits);
		}

		template <class J>
		static int CompareNumber(const J& A, const J& B)
		{
			return Compare(Number(A), Number(B));
		}

		template <class J>
		static bool Integer(const J& Value)
		{
			return Value.is_number() && IsInteger(Number(Value));
		}

		template <class J>
		static bool String(const J& Value)
		{
			return Value.is_string() && !Value.is_number();
		}

		template <class J>
		static bool Multiple(const J& Value, const J& Divisor)
		{
			return IsMultiple(Number(Value), Number(Divisor), Budget());
		}

		template <class J>
		static std::size_t Size(const J& Value)
		{
			const auto N = Number(Value);
			if (N.bNegative || !IsInteger(N))
			{
				Fail(EError::Range, "Schema cardinality must be a nonnegative integer");
			}
			const FDecimal Maximum{false, std::to_string(SIZE_MAX), 0};
			if (Compare(N, Maximum) >= 0)
			{
				return SIZE_MAX;
			}
			std::size_t Result = 0;
			for (const char Digit : N.Digits)
			{
				Result = Result * 10 + static_cast<unsigned>(Digit - '0');
			}
			if (Result)
			{
				for (std::int64_t I = 0; I < N.Exponent; ++I)
				{
					Result *= 10;
				}
			}
			return Result;
		}

		template <class J>
		static bool Equal(const J& A, const J& B)
		{
			FSchemaStep Step;
			if (A.is_number() || B.is_number())
			{
				return A.is_number() && B.is_number() && CompareNumber(A, B) == 0;
			}
			if (A.is_object() || B.is_object())
			{
				if (!A.is_object() || !B.is_object() || A.size() != B.size())
				{
					return false;
				}
				for (const auto& Item : A.object_range())
				{
					const auto Found = B.find(Item.key());
					if (Found == B.object_range().end() || !Equal(Item.value(), Found->value()))
					{
						return false;
					}
				}
				return true;
			}
			if (A.is_array() || B.is_array())
			{
				if (!A.is_array() || !B.is_array() || A.size() != B.size())
				{
					return false;
				}
				for (std::size_t I = 0; I < A.size(); ++I)
				{
					if (!Equal(A[I], B[I]))
					{
						return false;
					}
				}
				return true;
			}
			if (String(A) && String(B))
			{
				Check(std::max(A.as_string_view().size(), B.as_string_view().size()));
			}
			return A == B;
		}
	};
} // namespace VeroScript::Json::Detail
