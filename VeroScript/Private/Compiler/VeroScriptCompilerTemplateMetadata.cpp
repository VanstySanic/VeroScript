// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCompiler.h"
#include "TypeSupport/VeroScriptTemplateMetadataValidation.h"

#include <algorithm>
#include "VeroScriptAnalysisControl.h"
#include "VeroScriptMethodKey.h"
#include "TypeSupport/VeroScriptTypeMemberBinding.h"

namespace VeroScript
{
	void VeroScriptCompiler::CollectTemplateDeclarationMetadata(const FProgram &Program)
	{
		const auto Schema = [](const auto &Declaration)
		{
			auto Result = Declaration.TemplateParameters;
			if (Result.IsEmpty())
			{
				std::vector<FTemplateParameter> Parameters;
				for (std::size_t Index = 0; Index < Declaration.TypeParams.size(); ++Index)
				{
					FTemplateParameter Parameter;
					Parameter.Name = Declaration.TypeParams[Index];
					if (Index < Declaration.TypeParamDefaults.size())
					{
						Parameter.DefaultTypeName = Declaration.TypeParamDefaults[Index];
						Parameter.DefaultSource = Parameter.DefaultTypeName;
					}
					Parameters.push_back(std::move(Parameter));
				}
				Result.Assign(std::move(Parameters));
			}
			return Result;
		};
		const auto Snapshot = [](const FTemplateParameterList &Source, const std::size_t Begin, const std::size_t End)
		{
			std::vector<FTemplateParameter> Parameters;
			for (std::size_t Index = Begin; Index < End; ++Index)
			{
				auto Parameter = Source.GetParameters()[Index];
				Parameter.DefaultValue.reset();
				Parameter.Location.FileLabel = {};
				Parameters.push_back(std::move(Parameter));
			}
			FTemplateParameterList Result;
			Result.Assign(std::move(Parameters));
			return Result;
		};
		const auto Add = [&](FTemplateDeclarationInfo Info, const FDeclMeta &Declaration,
		                     const FSourceLocation &Location, const FTemplateParameterList &Parameters,
		                     const FTemplateParameterList &Owner, const std::string &OwnerIdentity)
		{
			CheckAnalysisCancellation();
			const auto Captured = Parameters.GetEnclosingCount();
			Info.Parameters = Snapshot(Parameters, Captured, Parameters.GetParameters().size());
			Info.EnclosingParameters =
			    Captured != 0 ? Snapshot(Parameters, 0, Captured) : Snapshot(Owner, 0, Owner.GetParameters().size());
			if (Info.Parameters.IsEmpty() && Info.EnclosingParameters.IsEmpty())
			{
				return;
			}
			auto FullParameters = Info.EnclosingParameters.GetParameters();
			const auto &Own = Info.Parameters.GetParameters();
			FullParameters.insert(FullParameters.end(), Own.begin(), Own.end());
			std::vector<std::string> Names, Symbols;
			for (std::size_t Index = 0; Index < FullParameters.size(); ++Index)
			{
				Names.push_back(FullParameters[Index].Name);
				Symbols.push_back("__template_parameter_" + std::to_string(Index));
			}
			Info.Id =
			    Info.ModulePath + '\x1F' + OwnerIdentity + '\x1F' + Info.Owner + "::" + Info.Name + "$" + Info.Kind;
			Info.Id += ":owner" + std::to_string(Info.EnclosingParameters.GetParameters().size());
			for (const auto &Parameter : FullParameters)
			{
				Info.Id += Parameter.Kind == ETemplateParameterKind::Type ? ":T" : ":V";
				if (Parameter.Kind == ETemplateParameterKind::Value)
				{
					const auto Type = SubstituteScopedTypeParameters(Parameter.ValueTypeName, Names, Symbols);
					Info.Id += std::to_string(Type.size()) + ':' + Type;
				}
			}
			auto Patterns = Info.ParameterTypeNames;
			for (auto &Pattern : Patterns)
			{
				Pattern = SubstituteScopedTypeParameters(Pattern, Names, Symbols);
			}
			Info.Id += EncodeMethodParamSuffix(Patterns) + (Info.bConst ? "c" : "");
			const auto IdentifyParameters = [&](FTemplateParameterList &Schema, const char *Group)
			{
				auto Records = Schema.GetParameters();
				for (std::size_t Index = 0; Index < Records.size(); ++Index)
				{
					if (Records[Index].Id.empty())
					{
						Records[Index].Id = Info.Id + Group + std::to_string(Index);
					}
				}
				Schema.Assign(std::move(Records));
			};
			IdentifyParameters(Info.Parameters, ":parameter:");
			IdentifyParameters(Info.EnclosingParameters, ":enclosing:");
			Info.SourceFile = std::string(Location.FileLabel);
			Info.Line = Location.Line;
			Info.Column = Location.Column;
			FTemplateMetadataBinding Binding;
			Binding.Id = Info.Id;
			Binding.Names = Info.Parameters.GetNames();
			Binding.EnclosingNames = Info.EnclosingParameters.GetNames();
			TemplateMetadataBindings.emplace(&Declaration, std::move(Binding));
			if (std::none_of(Module->TemplateDeclarations.begin(), Module->TemplateDeclarations.end(),
			        [&](const auto &Existing)
			{
				return Existing.Id == Info.Id;
			}))
			{
				Module->TemplateDeclarations.push_back(std::move(Info));
			}
		};
		const auto Parameters = [](const auto &Declaration)
		{
			std::vector<std::string> Result;
			for (const auto &Parameter : Declaration.Parameters)
			{
				Result.push_back((Parameter.bIsRef ? (Parameter.bIsConst ? "const ref " : "ref ") : "") +
				    Parameter.TypeName + (Parameter.bIsVariadic ? "..." : ""));
			}
			return Result;
		};
		const auto Types = [&](const auto &Declarations, const std::string &Kind)
		{
			for (const auto &Declaration : Declarations)
			{
				const auto TypeSchema = Schema(Declaration);
				FTemplateDeclarationInfo Info;
				Info.Kind = Kind;
				Info.Name = Declaration.Name;
				Info.Owner = Declaration.Scope.OwnerTypeName;
				Info.ModulePath = Declaration.ModulePath;
				Info.bExported = Declaration.bExported;
				Info.Access = Declaration.Scope.Access;
				Add(std::move(Info), Declaration.DeclMeta, Declaration.Location, TypeSchema, {}, Declaration.Scope.Id);
				for (const auto &Method : Declaration.Methods)
				{
					if (TemplateParameterNames(Method).empty() && TypeSchema.IsEmpty())
					{
						continue;
					}
					FTemplateDeclarationInfo Member;
					Member.Kind = "method";
					Member.Name = Method.Name;
					Member.Owner = Declaration.Name;
					Member.ModulePath = Declaration.ModulePath;
					Member.ReturnType = Method.ReturnType;
					Member.ParameterTypeNames = Parameters(Method);
					Member.bExported = Declaration.bExported && Method.Access == EMemberAccess::Public;
					Member.Access = Method.Access;
					Member.bStatic = Method.bIsStatic;
					Member.bConst = Method.bIsConst;
					Add(std::move(Member), Method.DeclMeta, Method.Location, Schema(Method), TypeSchema,
					    Declaration.Scope.Id);
				}
			}
		};
		Types(Program.Classes, "class");
		Types(Program.Structs, "struct");
		Types(Program.Interfaces, "interface");
		for (const auto &Declaration : Program.Enums)
		{
			if (Declaration.Scope.EnclosingTemplateParameters.IsEmpty())
			{
				continue;
			}
			FTemplateDeclarationInfo Info;
			Info.Kind = "enum";
			Info.Name = Declaration.Name;
			Info.Owner = Declaration.Scope.OwnerTypeName;
			Info.ModulePath = Declaration.ModulePath;
			Info.bExported = Declaration.bExported;
			Info.Access = Declaration.Scope.Access;
			FTemplateParameterList EnumSchema;
			EnumSchema.Assign(Declaration.Scope.EnclosingTemplateParameters.GetParameters(),
			    Declaration.Scope.EnclosingTemplateParameters.GetParameters().size());
			Add(std::move(Info), Declaration.DeclMeta, Declaration.Location, EnumSchema, {}, Declaration.Scope.Id);
		}
		for (const auto &Function : Program.Functions)
		{
			if (TemplateParameterNames(Function).empty())
			{
				continue;
			}
			FTemplateDeclarationInfo Info;
			Info.Kind = "function";
			Info.Name = Function.Name;
			Info.ModulePath = Function.ModulePath;
			Info.ReturnType = Function.ReturnType;
			Info.ParameterTypeNames = Parameters(Function);
			Info.bExported = Function.bExported;
			Add(std::move(Info), Function.DeclMeta, Function.Location, Schema(Function), {}, {});
		}
	}

	void VeroScriptCompiler::ApplyTemplateInstanceMetadata(FTemplateInstanceInfo &Instance,
	    const FDeclMeta *Declaration)
	{
		const auto Found = TemplateMetadataBindings.find(Declaration);
		if (Found == TemplateMetadataBindings.end())
		{
			return;
		}
		const auto &Binding = Found->second;
		FTemplateInstanceInfo Completed;
		Completed.DeclarationId = Binding.Id;
		for (const auto &Name : Binding.Names)
		{
			const auto Value = ActiveTypeSubst.find(Name);
			if (Value == ActiveTypeSubst.end())
			{
				return;
			}
			Completed.Arguments.push_back(Value->second);
		}
		for (const auto &Name : Binding.EnclosingNames)
		{
			const auto Value = ActiveTypeSubst.find(Name);
			if (Value == ActiveTypeSubst.end())
			{
				return;
			}
			Completed.EnclosingArguments.push_back(Value->second);
		}
		if (!CaptureTemplateInstanceValueTypes(Completed))
		{
			ReportError("invalid closed template instance metadata", CurrentEmitLocation);
			return;
		}
		Instance = std::move(Completed);
	}
} // namespace VeroScript
