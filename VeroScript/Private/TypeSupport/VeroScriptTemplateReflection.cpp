// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTemplateReflection.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptModuleMeta.h"

namespace VeroScript
{
	bool QueryOpenTemplateMetadata(const FBytecodeModule &Module, const std::string &ModulePath,
								   const ETemplateReflectionQuery Query, const std::size_t Index,
								   const std::size_t Detail, FValue &Result, std::string &Error)
	{
		const FTemplateDeclarationInfo *Declaration = nullptr;
		std::size_t Count = 0;
		for (const auto &Candidate : Module.TemplateDeclarations)
		{
			if (!ModulePath.empty() && Candidate.ModulePath != ModulePath)
			{
				continue;
			}
			if (Count++ == Index)
			{
				Declaration = &Candidate;
			}
		}
		if (Query == ETemplateReflectionQuery::DeclarationCount)
		{
			Result = static_cast<std::int32_t>(Count);
			return true;
		}
		if (!Declaration)
		{
			Error = "template declaration index out of range";
			return false;
		}
		const auto &Own = Declaration->Parameters.GetParameters();
		const auto &Enclosing = Declaration->EnclosingParameters.GetParameters();
		const auto *Parameter = Detail < Enclosing.size()				 ? &Enclosing[Detail]
								: Detail - Enclosing.size() < Own.size() ? &Own[Detail - Enclosing.size()]
																		 : nullptr;
		switch (Query)
		{
			case ETemplateReflectionQuery::DeclarationId:
			{
				Result = Declaration->Id;
				return true;
			}
			case ETemplateReflectionQuery::DeclarationName:
			{
				Result = Declaration->Name;
				return true;
			}
			case ETemplateReflectionQuery::DeclarationKind:
			{
				Result = Declaration->Kind;
				return true;
			}
			case ETemplateReflectionQuery::DeclarationOwner:
			{
				Result = Declaration->Owner;
				return true;
			}
			case ETemplateReflectionQuery::ParameterCount:
			case ETemplateReflectionQuery::OwnParameterCount:
			case ETemplateReflectionQuery::EnclosingParameterCount:
			{
				Result = static_cast<std::int32_t>(
					Query == ETemplateReflectionQuery::ParameterCount	   ? Own.size() + Enclosing.size()
					: Query == ETemplateReflectionQuery::OwnParameterCount ? Own.size()
																		   : Enclosing.size());
				return true;
			}
			default:
			{
				break;
			}
		}
		if (!Parameter)
		{
			Error = "template parameter index out of range";
			return false;
		}
		switch (Query)
		{
			case ETemplateReflectionQuery::ParameterId:
			{
				Result = Parameter->Id;
				return true;
			}
			case ETemplateReflectionQuery::ParameterName:
			{
				Result = Parameter->Name;
				return true;
			}
			case ETemplateReflectionQuery::ParameterKind:
			{
				Result = std::string(Parameter->Kind == ETemplateParameterKind::Type ? "type" : "value");
				return true;
			}
			case ETemplateReflectionQuery::ParameterValueType:
			{
				Result = Parameter->ValueTypeName;
				return true;
			}
			case ETemplateReflectionQuery::ParameterDefault:
			{
				Result = Parameter->DefaultSource;
				return true;
			}
			case ETemplateReflectionQuery::ParameterIsEnclosing:
			{
				Result = Detail < Enclosing.size();
				return true;
			}
			default:
			{
				Error = "invalid template declaration query";
				return false;
			}
		}
	}

	bool QueryTemplateInstanceMetadata(const FBytecodeModule &Module, const FScriptObject &Descriptor,
									   const ETemplateReflectionQuery Query, const std::int32_t Index, FValue &Result,
									   std::string &Error)
	{
		const auto Field = [&](const char *Name) -> const FValue *
		{
			std::uint16_t Slot = 0;
			return ModuleMeta::FindFieldIndex(Module, Descriptor.GetTypeName(), Name, Slot) &&
						   Slot < Descriptor.Fields.size()
					   ? &Descriptor.Fields[Slot]
					   : nullptr;
		};
		const auto String = [&](const char *Name)
		{
			const auto *Value = Field(Name);
			const auto *Text = Value ? std::get_if<std::string>(Value) : nullptr;
			return Text ? *Text : std::string{};
		};
		const auto *RawIndex = Field("index");
		const auto *MemberIndex = RawIndex ? std::get_if<std::int32_t>(RawIndex) : nullptr;
		auto Kind = ModuleMeta::TypeSimpleName(Descriptor.GetTypeName());
		Kind.resize(Kind.find('<') == std::string::npos ? Kind.size() : Kind.find('<'));
		FTemplateInstanceInfo Instance;
		bool Found = false;
		if (Kind == "type")
		{
			const auto Owner = String("qualified_name");
			const auto Find = [&](const auto &Declarations)
			{
				for (const auto &Declaration : Declarations)
				{
					if (Declaration.Name == Owner)
					{
						Instance = Declaration.TemplateInstance;
						Found = true;
					}
				}
			};
			Find(Module.Types);
			Find(Module.Interfaces);
			Find(Module.Enums);
		}
		else if (Kind == "method_info" && MemberIndex && *MemberIndex >= 0)
		{
			const auto Owner = String("owner_name");
			std::vector<FInstanceMethodInfo> Methods;
			std::vector<FStaticMethodInfo> Statics;
			ModuleMeta::EnumerateInstanceMethods(Module, Owner, Methods, true, true, true);
			ModuleMeta::EnumerateStaticMethods(Module, Owner, Statics);
			const auto Position = static_cast<std::size_t>(*MemberIndex);
			if (Position < Methods.size())
			{
				Instance = Methods[Position].TemplateInstance;
				Found = true;
			}
			else if (Position - Methods.size() < Statics.size())
			{
				Instance = Statics[Position - Methods.size()].TemplateInstance;
				Found = true;
			}
		}
		else if (Kind == "function_info" && MemberIndex && *MemberIndex >= 0)
		{
			std::vector<FFreeFunctionInfo> Functions;
			ModuleMeta::EnumerateFreeFunctions(Module, Functions, true);
			const auto Position = static_cast<std::size_t>(*MemberIndex);
			if (Position < Functions.size())
			{
				Instance = Functions[Position].TemplateInstance;
				Found = true;
			}
		}
		if (!Found)
		{
			// Primitive/container type descriptors have no template declaration.
			if (Kind != "type")
			{
				Error = "invalid template instance descriptor";
				return false;
			}
		}
		if (Query == ETemplateReflectionQuery::InstanceDeclarationId)
		{
			Result = Instance.DeclarationId;
			return true;
		}
		const bool Enclosing = Query >= ETemplateReflectionQuery::EnclosingArgumentCount;
		const auto &Arguments = Enclosing ? Instance.EnclosingArguments : Instance.Arguments;
		if (Query == ETemplateReflectionQuery::ArgumentCount ||
			Query == ETemplateReflectionQuery::EnclosingArgumentCount)
		{
			Result = static_cast<std::int32_t>(Arguments.size());
			return true;
		}
		if (Index < 0 || static_cast<std::size_t>(Index) >= Arguments.size())
		{
			Error = "template argument index out of range";
			return false;
		}
		const auto &Argument = Arguments[static_cast<std::size_t>(Index)];
		switch (Query)
		{
			case ETemplateReflectionQuery::ArgumentEncoding:
			case ETemplateReflectionQuery::EnclosingArgumentEncoding:
			{
				Result = Argument;
				return true;
			}
			case ETemplateReflectionQuery::ArgumentName:
			case ETemplateReflectionQuery::EnclosingArgumentName:
			{
				Result = FormatTemplateArgumentName(Argument);
				return true;
			}
			case ETemplateReflectionQuery::ArgumentKind:
			{
				Result = std::string(Argument.rfind("@value:", 0) == 0 ? "value" : "type");
				return true;
			}
			case ETemplateReflectionQuery::ArgumentValueType:
			{
				FTemplateArgument Value;
				Result = Argument.rfind("@value:", 0) == 0 &&
								 DecodeTemplateArgument(std::string_view(Argument).substr(7), Value)
							 ? Value.Value.Type.Name
							 : std::string{};
				return true;
			}
			default:
			{
				Error = "invalid template instance query";
				return false;
			}
		}
	}
} // namespace VeroScript
