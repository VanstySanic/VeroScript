// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "JsonBindingInternal.h"
#include "VeroScriptMemory.h"
#include <stdexcept>

namespace VeroScript::Json::Binding
{
	enum class ESchemaAction
	{
		Compile,
		CompileOptions,
		TryCompile,
		TryCompileOptions,
		IsCompiled,
		IsValid,
		Validate,
		ValidateLimits,
		TryValidate,
		TryValidateLimits,
		ValidateOptions,
		TryValidateOptions,
		ReportAnnotationSize,
		ReportAnnotation,
		ReportAnnotationsTruncated,
		ReportLimits,
		SetReportLimits,
		ReportMaxIssues,
		SetReportMaxIssues,
		ReportMaxAnnotations,
		SetReportMaxAnnotations,
		ReportCollectAnnotations,
		SetReportCollectAnnotations,
		ReportValid,
		ReportTruncated,
		ReportSize,
		ReportIssue,
		Uri,
		SetUri,
		Limits,
		SetLimits,
		Resources,
		SetResources,
		AssertFormats,
		SetAssertFormats
	};

	static void SchemaCheck(bool Success, const FError& Error)
	{
		if (!Success)
		{
			throw std::runtime_error(std::string(Error.Id()) + ": " + Error.Message);
		}
	}
	static std::int64_t SchemaCount(std::uint64_t Value)
	{
		if (Value > INT64_MAX)
		{
			throw std::range_error("Schema count exceeds the script int64 range");
		}
		return static_cast<std::int64_t>(Value);
	}

	static FJsonValue IssueValue(const FSchemaIssue& Issue, FWork Work)
	{
		if (Work.Check && !Work.Check(Work.UserData, 1))
		{
			throw std::runtime_error("JSON Schema report cancelled");
		}
		FError Error;
		FBuilder ObjectBuilder(EKind::Object);
		auto String = [&](const char* Key, const std::string& Text)
		{
			if (Work.Check && !Work.Check(Work.UserData, Text.size()))
			{
				throw std::runtime_error("JSON Schema report cancelled");
			}
			FJsonValue Value;
			SchemaCheck(FJsonValue::TryString(Text, Value, Error), Error);
			SchemaCheck(ObjectBuilder.TrySet(Key, Value, Error), Error);
		};
		String("instance_pointer", Issue.InstancePointer);
		String("schema_location", Issue.SchemaLocation);
		String("keyword", Issue.Keyword);
		String("message", Issue.Message);
		FBuilder Details(EKind::Array);
		for (const auto& Detail : Issue.Details)
		{
			SchemaCheck(Details.TryPush(IssueValue(Detail, Work), Error), Error);
		}
		FJsonValue Value;
		SchemaCheck(Details.TryFinish(Value, Error), Error);
		SchemaCheck(ObjectBuilder.TrySet("details", Value, Error), Error);
		SchemaCheck(ObjectBuilder.TryFinish(Value, Error), Error);
		return Value;
	}
	static FJsonValue AnnotationValue(const FSchemaReport::FAnnotation& Annotation, FWork Work)
	{
		FError Error;
		FBuilder Builder(EKind::Object);
		for (const auto& Field : {std::pair{"instance_pointer", &Annotation.InstancePointer},
				 std::pair{"schema_location", &Annotation.SchemaLocation},
				 std::pair{"evaluation_path", &Annotation.EvaluationPath},
				 std::pair{"keyword", &Annotation.Keyword}})
		{
			if (Work.Check && !Work.Check(Work.UserData, Field.second->size() + 1))
			{
				throw std::runtime_error("JSON Schema report cancelled");
			}
			FJsonValue Value;
			SchemaCheck(FJsonValue::TryString(*Field.second, Value, Error), Error);
			SchemaCheck(Builder.TrySet(Field.first, Value, Error), Error);
		}
		SchemaCheck(Builder.TrySet("value", Annotation.Value, Error), Error);
		FJsonValue Out;
		SchemaCheck(Builder.TryFinish(Out, Error), Error);
		return Out;
	}

	template <ESchemaAction Action>
	bool SchemaCall(const FNativeCallContext& Input, VeroScript::FValue& Out, std::string& Error)
	{
		try
		{
			auto C = Input;
			if (C.HostThis)
			{
				++C.Args;
				if (C.MutableArgs)
				{
					++C.MutableArgs;
				}
				--C.ArgCount;
			}
			auto& S = *static_cast<FState*>(C.UserData);
			const auto MemoryWork = C.MemoryServices ? C.MemoryServices->GetMemoryWork() : FMemoryWork{};
			const FWork Work{MemoryWork.Check, MemoryWork.UserData};
			FError Failure;
			if constexpr (Action == ESchemaAction::Compile || Action == ESchemaAction::CompileOptions ||
						  Action == ESchemaAction::TryCompile || Action == ESchemaAction::TryCompileOptions)
			{
				FSchema Schema;
				FSchemaOptions Options;
				std::vector<FSchemaResource> Resources;

				constexpr bool WithOptions =
					Action == ESchemaAction::CompileOptions || Action == ESchemaAction::TryCompileOptions;
				constexpr bool IsTry =
					Action == ESchemaAction::TryCompile || Action == ESchemaAction::TryCompileOptions;
				constexpr std::size_t OutputIndex = WithOptions ? 3 : 1;
				if constexpr (IsTry)
				{
					if (!C.MutableArg(OutputIndex) || !C.MutableArg(OutputIndex + 1))
					{
						throw std::invalid_argument("Schema try compilation requires ref outputs");
					}
				}
				bool Ready = true;
				if constexpr (WithOptions)
				{
					Options = Object<FSchemaOptions>(C, 1);
					const auto& Source = Object<FJsonValue>(C, 2);
					std::vector<std::string> Keys;
					Ready = Source.TryKeys(Keys, Failure);
					if (Ready && Keys.size() >= Options.MaxResources)
					{
						Failure.Code = EError::Limit;
						Failure.Message = "Schema resource limit exceeded";
						Ready = false;
					}
					for (auto& Key : Keys)
					{
						if (!Ready)
						{
							break;
						}
						if (Work.Check && !Work.Check(Work.UserData, 1))
						{
							Failure.Code = EError::Cancelled;
							Ready = false;
							break;
						}
						FJsonValue Resource;
						Ready = Source.TryGet(Key, Resource, Failure);
						if (Ready)
						{
							Resources.push_back({std::move(Key), std::move(Resource)});
						}
					}
				}
				const bool Success =
					Ready &&
					FSchema::TryCompile(Object<FJsonValue>(C, 0), Schema, Failure, Options, Resources, {}, Work);
				if constexpr (IsTry)
				{
					if (Failure.Code == EError::Cancelled)
					{
						Error = Failure.Message;
						Diagnostic(C, Failure.Id(), Failure.Message, Failure.InstancePointer);
						return false;
					}
					VeroScript::FValue Result, ErrorValue;
					if (!Wrap(S, std::move(Failure), ErrorValue, Error) ||
						(Success && !Wrap(S, std::move(Schema), Result, Error)))
					{
						return false;
					}
					if (Success)
					{
						C.AssignValue(*C.MutableArg(OutputIndex), std::move(Result));
					}
					C.AssignValue(*C.MutableArg(OutputIndex + 1), std::move(ErrorValue));
					Out = Success;
					return true;
				}

				if (!Success)
				{
					Diagnostic(C, Failure.Id(), Failure.Message, Failure.InstancePointer);
				}
				SchemaCheck(Success, Failure);
				return Wrap(S, std::move(Schema), Out, Error);
			}
			else if constexpr (Action == ESchemaAction::IsCompiled)
			{
				Out = static_cast<FSchema*>(C.HostThis)->IsCompiled();
			}
			else if constexpr (Action == ESchemaAction::IsValid)
			{
				bool Valid = false;
				const bool Success =
					static_cast<FSchema*>(C.HostThis)->TryIsValid(Object<FJsonValue>(C, 0), Valid, Failure, {}, Work);
				if (!Success)
				{
					Diagnostic(C, Failure.Id(), Failure.Message, Failure.InstancePointer);
				}
				SchemaCheck(Success, Failure);
				Out = Valid;
			}
			else if constexpr (Action == ESchemaAction::Validate || Action == ESchemaAction::ValidateLimits ||
							   Action == ESchemaAction::TryValidate || Action == ESchemaAction::TryValidateLimits ||
							   Action == ESchemaAction::ValidateOptions || Action == ESchemaAction::TryValidateOptions)
			{
				FSchemaReportOptions Options;
				if constexpr (Action == ESchemaAction::ValidateLimits || Action == ESchemaAction::TryValidateLimits)
				{
					constexpr std::size_t Index = Action == ESchemaAction::TryValidateLimits ? 2 : 1;
					const auto Count = std::get<std::int64_t>(C.Args[Index]);

					Options.MaxIssues = Count < 1 ? 0 : static_cast<std::uint64_t>(Count);
					Options.Limits = Object<FLimits>(C, Index + 1);
				}
				if constexpr (Action == ESchemaAction::ValidateOptions || Action == ESchemaAction::TryValidateOptions)
				{
					Options = Object<FSchemaReportOptions>(C, Action == ESchemaAction::TryValidateOptions ? 2 : 1);
				}
				FSchemaReport Report;
				bool Success;
				if constexpr (Action == ESchemaAction::TryValidate || Action == ESchemaAction::TryValidateLimits ||
							  Action == ESchemaAction::TryValidateOptions)
				{
					constexpr std::size_t OutputIndex = Action == ESchemaAction::TryValidateLimits	  ? 4
														: Action == ESchemaAction::TryValidateOptions ? 3
																									  : 2;
					Success = Object<FSchema>(C, 0).TryValidateWithOptions(
						Object<FJsonValue>(C, 1), Report, Failure, Options, Work);
					if (!C.MutableArg(OutputIndex) || !C.MutableArg(OutputIndex + 1))
					{
						throw std::invalid_argument("Schema try validation requires ref outputs");
					}
					if (Failure.Code == EError::Cancelled)
					{
						Error = Failure.Message;
						Diagnostic(C, Failure.Id(), Failure.Message, Failure.InstancePointer);
						return false;
					}
					VeroScript::FValue Result, ErrorValue;
					if (!Wrap(S, std::move(Failure), ErrorValue, Error) ||
						(Success && !Wrap(S, std::move(Report), Result, Error)))
					{
						return false;
					}
					if (Success)
					{
						C.AssignValue(*C.MutableArg(OutputIndex), std::move(Result));
					}
					C.AssignValue(*C.MutableArg(OutputIndex + 1), std::move(ErrorValue));
					Out = Success;
					return true;
				}
				else
				{
					Success = static_cast<FSchema*>(C.HostThis)
								  ->TryValidateWithOptions(Object<FJsonValue>(C, 0), Report, Failure, Options, Work);
				}
				if (!Success)
				{
					Diagnostic(C, Failure.Id(), Failure.Message, Failure.InstancePointer);
				}
				SchemaCheck(Success, Failure);
				return Wrap(S, std::move(Report), Out, Error);
			}
			else if constexpr (Action == ESchemaAction::ReportValid)
			{
				Out = static_cast<FSchemaReport*>(C.HostThis)->bValid;
			}
			else if constexpr (Action == ESchemaAction::ReportTruncated)
			{
				Out = static_cast<FSchemaReport*>(C.HostThis)->bTruncated;
			}
			else if constexpr (Action == ESchemaAction::ReportSize)
			{
				Out = SchemaCount(static_cast<FSchemaReport*>(C.HostThis)->Issues.size());
			}
			else if constexpr (Action == ESchemaAction::ReportIssue)
			{
				const auto Index = std::get<std::int64_t>(C.Args[0]);
				const auto& Issues = static_cast<FSchemaReport*>(C.HostThis)->Issues;
				if (Index < 0 || static_cast<std::uint64_t>(Index) >= Issues.size())
				{
					throw std::out_of_range("Schema issue index is out of range");
				}
				return Wrap(S, IssueValue(Issues[static_cast<std::size_t>(Index)], Work), Out, Error);
			}
			else if constexpr (Action == ESchemaAction::ReportAnnotationSize)
			{
				Out = SchemaCount(static_cast<FSchemaReport*>(C.HostThis)->Annotations.size());
			}
			else if constexpr (Action == ESchemaAction::ReportAnnotationsTruncated)
			{
				Out = static_cast<FSchemaReport*>(C.HostThis)->bAnnotationsTruncated;
			}
			else if constexpr (Action == ESchemaAction::ReportAnnotation)
			{
				const auto Index = std::get<std::int64_t>(C.Args[0]);
				const auto& Values = static_cast<FSchemaReport*>(C.HostThis)->Annotations;
				if (Index < 0 || static_cast<std::uint64_t>(Index) >= Values.size())
				{
					throw std::out_of_range("Schema annotation index is out of range");
				}
				return Wrap(S, AnnotationValue(Values[static_cast<std::size_t>(Index)], Work), Out, Error);
			}
			else if constexpr (Action == ESchemaAction::ReportLimits || Action == ESchemaAction::SetReportLimits ||
							   Action == ESchemaAction::ReportMaxIssues ||
							   Action == ESchemaAction::SetReportMaxIssues ||
							   Action == ESchemaAction::ReportMaxAnnotations ||
							   Action == ESchemaAction::SetReportMaxAnnotations ||
							   Action == ESchemaAction::ReportCollectAnnotations ||
							   Action == ESchemaAction::SetReportCollectAnnotations)
			{
				auto& Options = *static_cast<FSchemaReportOptions*>(C.HostThis);
				if constexpr (Action == ESchemaAction::ReportLimits)
				{
					return Wrap(S, Options.Limits, Out, Error);
				}
				else if constexpr (Action == ESchemaAction::SetReportLimits)
				{
					Options.Limits = Object<FLimits>(C, 0);
				}
				else if constexpr (Action == ESchemaAction::ReportMaxIssues)
				{
					Out = SchemaCount(Options.MaxIssues);
				}
				else if constexpr (Action == ESchemaAction::ReportMaxAnnotations)
				{
					Out = SchemaCount(Options.MaxAnnotations);
				}
				else if constexpr (Action == ESchemaAction::ReportCollectAnnotations)
				{
					Out = Options.bCollectAnnotations;
				}
				else if constexpr (Action == ESchemaAction::SetReportCollectAnnotations)
				{
					Options.bCollectAnnotations = std::get<bool>(C.Args[0]);
				}
				else
				{
					const auto Count = std::get<std::int64_t>(C.Args[0]);
					constexpr bool Issues = Action == ESchemaAction::SetReportMaxIssues;
					if (Count < (Issues ? 1 : 0) || Count > 16384)
					{
						throw std::range_error("Schema report boundary is out of range");
					}
					(Issues ? Options.MaxIssues : Options.MaxAnnotations) = static_cast<std::uint64_t>(Count);
				}
			}
			else
			{
				auto& Options = *static_cast<FSchemaOptions*>(C.HostThis);
				if constexpr (Action == ESchemaAction::Uri)
				{
					Out = Options.RetrievalUri;
				}
				else if constexpr (Action == ESchemaAction::SetUri)
				{
					Options.RetrievalUri = std::get<std::string>(C.Args[0]);
				}
				else if constexpr (Action == ESchemaAction::Limits)
				{
					return Wrap(S, Options.Limits, Out, Error);
				}
				else if constexpr (Action == ESchemaAction::SetLimits)
				{
					Options.Limits = Object<FLimits>(C, 0);
				}
				else if constexpr (Action == ESchemaAction::Resources)
				{
					Out = SchemaCount(Options.MaxResources);
				}
				else if constexpr (Action == ESchemaAction::AssertFormats)
				{
					Out = Options.bAssertFormats;
				}
				else if constexpr (Action == ESchemaAction::SetAssertFormats)
				{
					Options.bAssertFormats = std::get<bool>(C.Args[0]);
				}
				else if constexpr (Action == ESchemaAction::SetResources)
				{
					const auto Count = std::get<std::int64_t>(C.Args[0]);
					if (Count < 1)
					{
						throw std::range_error("Schema resource limit must be positive");
					}
					Options.MaxResources = static_cast<std::uint64_t>(Count);
				}
			}
			return true;
		}
		catch (const std::exception& Failure)
		{
			Error = Failure.what();
			return false;
		}
		catch (...)
		{
			Error = "JSON Schema binding failed";
			return false;
		}
	}

	bool RegisterSchema(FState& S, std::string& Error)
	{
		using T = ENativeType;
		FHostTypeId Schema, Report, Options, ReportOptions;
		return Type<FSchema>(S, Schema, Error) && HandleType<FSchemaReport>(S, Report, Error) &&
			   Type<FSchemaOptions>(S, Options, Error) && Type<FSchemaReportOptions>(S, ReportOptions, Error) &&
			   Method<SchemaCall<ESchemaAction::AssertFormats>>(S, Options, "assert_formats", T::Bool, {}, Error) &&
			   Method<SchemaCall<ESchemaAction::SetAssertFormats>>(
				   S, Options, "set_assert_formats", T::Void, {T::Bool}, Error, true) &&
			   Method<SchemaCall<ESchemaAction::ReportLimits>>(
				   S, ReportOptions, "limits", T::HostObject, {}, Error, false, "json::limits") &&
			   Method<SchemaCall<ESchemaAction::SetReportLimits>>(
				   S, ReportOptions, "set_limits", T::Void, {T::HostObject}, Error, true, "", {"json::limits"}) &&
			   Method<SchemaCall<ESchemaAction::ReportMaxIssues>>(
				   S, ReportOptions, "max_issues", T::Int64, {}, Error) &&
			   Method<SchemaCall<ESchemaAction::SetReportMaxIssues>>(
				   S, ReportOptions, "set_max_issues", T::Void, {T::Int64}, Error, true) &&
			   Method<SchemaCall<ESchemaAction::ReportMaxAnnotations>>(
				   S, ReportOptions, "max_annotations", T::Int64, {}, Error) &&
			   Method<SchemaCall<ESchemaAction::SetReportMaxAnnotations>>(
				   S, ReportOptions, "set_max_annotations", T::Void, {T::Int64}, Error, true) &&
			   Method<SchemaCall<ESchemaAction::ReportCollectAnnotations>>(
				   S, ReportOptions, "collect_annotations", T::Bool, {}, Error) &&
			   Method<SchemaCall<ESchemaAction::SetReportCollectAnnotations>>(
				   S, ReportOptions, "set_collect_annotations", T::Void, {T::Bool}, Error, true) &&
			   Method<SchemaCall<ESchemaAction::ReportAnnotationSize>>(
				   S, Report, "annotation_count", T::Int64, {}, Error) &&
			   Method<SchemaCall<ESchemaAction::ReportAnnotationsTruncated>>(
				   S, Report, "annotations_truncated", T::Bool, {}, Error) &&
			   Method<SchemaCall<ESchemaAction::ReportAnnotation>>(
				   S, Report, "annotation", T::HostObject, {T::Int64}, Error, false, "json::value") &&
			   Method<SchemaCall<ESchemaAction::ValidateOptions>>(S,
				   Schema,
				   "validate_with_options",
				   T::HostObject,
				   {T::HostObject, T::HostObject},
				   Error,
				   false,
				   "json::schema_report",
				   {"json::value", "json::schema_report_options"}) &&
			   Function<SchemaCall<ESchemaAction::TryValidateOptions>>(S,
				   "json::try_validate_schema_with_options",
				   T::Bool,
				   {T::HostObject, T::HostObject, T::HostObject, T::HostObject, T::HostObject},
				   Error,
				   {"json::schema", "json::value", "json::schema_report_options", "json::schema_report", "json::error"},
				   {0, 0, 0, NativeParamFlagRef, NativeParamFlagRef}) &&
			   Function<SchemaCall<ESchemaAction::Compile>>(S,
				   "json::compile_schema",
				   T::HostObject,
				   {T::HostObject},
				   Error,
				   {"json::value"},
				   {},
				   "json::schema") &&
			   Function<SchemaCall<ESchemaAction::CompileOptions>>(S,
				   "json::compile_schema_with_options",
				   T::HostObject,
				   {T::HostObject, T::HostObject, T::HostObject},
				   Error,
				   {"json::value", "json::schema_options", "json::value"},
				   {},
				   "json::schema") &&
			   Function<SchemaCall<ESchemaAction::TryCompile>>(S,
				   "json::try_compile_schema",
				   T::Bool,
				   {T::HostObject, T::HostObject, T::HostObject},
				   Error,
				   {"json::value", "json::schema", "json::error"},
				   {0, NativeParamFlagRef, NativeParamFlagRef}) &&

			   Function<SchemaCall<ESchemaAction::TryCompileOptions>>(S,
				   "json::try_compile_schema_with_options",
				   T::Bool,
				   {T::HostObject, T::HostObject, T::HostObject, T::HostObject, T::HostObject},
				   Error,
				   {"json::value", "json::schema_options", "json::value", "json::schema", "json::error"},
				   {0, 0, 0, NativeParamFlagRef, NativeParamFlagRef}) &&
			   Function<SchemaCall<ESchemaAction::TryValidateLimits>>(S,
				   "json::try_validate_schema_with_limits",
				   T::Bool,
				   {T::HostObject, T::HostObject, T::Int64, T::HostObject, T::HostObject, T::HostObject},
				   Error,
				   {"json::schema", "json::value", "", "json::limits", "json::schema_report", "json::error"},
				   {0, 0, 0, 0, NativeParamFlagRef, NativeParamFlagRef}) &&
			   Function<SchemaCall<ESchemaAction::TryValidate>>(S,
				   "json::try_validate_schema",
				   T::Bool,
				   {T::HostObject, T::HostObject, T::HostObject, T::HostObject},
				   Error,
				   {"json::schema", "json::value", "json::schema_report", "json::error"},
				   {0, 0, NativeParamFlagRef, NativeParamFlagRef}) &&
			   Method<SchemaCall<ESchemaAction::IsCompiled>>(S, Schema, "compiled", T::Bool, {}, Error) &&
			   Method<SchemaCall<ESchemaAction::IsValid>>(S, Schema, "is_valid", T::Bool, {T::HostObject}, Error) &&
			   Method<SchemaCall<ESchemaAction::Validate>>(
				   S, Schema, "validate", T::HostObject, {T::HostObject}, Error, false, "json::schema_report") &&
			   Method<SchemaCall<ESchemaAction::ValidateLimits>>(S,
				   Schema,
				   "validate_with_limits",
				   T::HostObject,
				   {T::HostObject, T::Int64, T::HostObject},
				   Error,
				   false,
				   "json::schema_report",
				   {"json::value", "", "json::limits"}) &&
			   Method<SchemaCall<ESchemaAction::ReportValid>>(S, Report, "valid", T::Bool, {}, Error) &&
			   Method<SchemaCall<ESchemaAction::ReportTruncated>>(S, Report, "truncated", T::Bool, {}, Error) &&
			   Method<SchemaCall<ESchemaAction::ReportSize>>(S, Report, "size", T::Int64, {}, Error) &&
			   Method<SchemaCall<ESchemaAction::ReportIssue>>(S, Report, "issue", T::HostObject, {T::Int64}, Error) &&
			   Method<SchemaCall<ESchemaAction::Uri>>(S, Options, "retrieval_uri", T::String, {}, Error) &&
			   Method<SchemaCall<ESchemaAction::SetUri>>(
				   S, Options, "set_retrieval_uri", T::Void, {T::String}, Error, true) &&
			   Method<SchemaCall<ESchemaAction::Limits>>(
				   S, Options, "limits", T::HostObject, {}, Error, false, "json::limits") &&
			   Method<SchemaCall<ESchemaAction::SetLimits>>(
				   S, Options, "set_limits", T::Void, {T::HostObject}, Error, true, "", {"json::limits"}) &&
			   Method<SchemaCall<ESchemaAction::Resources>>(S, Options, "max_resources", T::Int64, {}, Error) &&
			   Method<SchemaCall<ESchemaAction::SetResources>>(
				   S, Options, "set_max_resources", T::Void, {T::Int64}, Error, true);
	}
} // namespace VeroScript::Json::Binding
