// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTemplateMetadataValidation.h"
#include "VeroScriptBytecode.h"
#include "VeroScriptAst.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace VeroScript
{
	bool CaptureTemplateInstanceValueTypes(FTemplateInstanceInfo &Instance)
	{
		std::vector<std::string> Types;
		for (const auto *Arguments : {&Instance.EnclosingArguments, &Instance.Arguments})
		{
			for (const auto &Argument : *Arguments)
			{
				if (Argument.rfind("@value:", 0) == 0)
				{
					FTemplateArgument Value;
					if (!DecodeTemplateArgument(std::string_view(Argument).substr(7), Value) ||
						Value.Kind != ETemplateParameterKind::Value)
					{
						return false;
					}
					Types.push_back(Value.Value.Type.Name);
				}
			}
		}
		Instance.ValueTypeNames = std::move(Types);
		return true;
	}

	bool ValidateTemplateMetadata(const FBytecodeModule &Module, std::string &Error)
	{
		std::unordered_map<std::string, const FTemplateDeclarationInfo *> Declarations;
		std::size_t Bytes = 0;
		const auto Text = [&](const std::string &Value)
		{
			if (Value.size() > 64u * 1024u * 1024u - Bytes || Value.find('\0') != std::string::npos)
			{
				Error = "invalid or excessive template metadata text";
				return false;
			}
			Bytes += Value.size();
			return true;
		};
		const auto Parameters = [&](const FTemplateParameterList &List, const bool CheckDefaults)
		{
			if (List.GetParameters().size() > 65536)
			{
				return false;
			}
			std::unordered_set<std::string> Names, Ids;
			bool Defaulted = false;
			for (const auto &Parameter : List.GetParameters())
			{
				if (Parameter.Id.empty() || !Ids.insert(Parameter.Id).second || Parameter.Name.empty() ||
					!Names.insert(Parameter.Name).second ||
					static_cast<unsigned>(Parameter.Kind) > static_cast<unsigned>(ETemplateParameterKind::Value) ||
					(Parameter.Kind == ETemplateParameterKind::Type && !Parameter.ValueTypeName.empty()) ||
					(Parameter.Kind == ETemplateParameterKind::Value &&
					 (Parameter.ValueTypeName.empty() || !Parameter.DefaultTypeName.empty())))
				{
					return false;
				}
				if (CheckDefaults && Defaulted && !Parameter.HasDefault())
				{
					return false;
				}
				Defaulted = Defaulted || Parameter.HasDefault();
				for (const auto *Value :
					 {&Parameter.Id, &Parameter.Name, &Parameter.Namespace, &Parameter.ValueTypeName,
					  &Parameter.DefaultTypeName, &Parameter.DefaultSource})
				{
					if (!Text(*Value))
					{
						return false;
					}
				}
			}
			return true;
		};
		if (Module.TemplateDeclarations.size() > 65536)
		{
			Error = "template declaration metadata exceeds safety limit";
			return false;
		}
		for (const auto &Declaration : Module.TemplateDeclarations)
		{
			if (Declaration.Id.empty() || Declaration.Name.empty() ||
				!Declarations.emplace(Declaration.Id, &Declaration).second ||
				(Declaration.Kind != "class" && Declaration.Kind != "struct" && Declaration.Kind != "interface" &&
				 Declaration.Kind != "function" && Declaration.Kind != "method" && Declaration.Kind != "enum") ||
				static_cast<unsigned>(Declaration.Access) > static_cast<unsigned>(EMemberAccess::Private) ||
				!Parameters(Declaration.Parameters, true) || !Parameters(Declaration.EnclosingParameters, false) ||
				(Declaration.Parameters.IsEmpty() && Declaration.EnclosingParameters.IsEmpty()))
			{
				Error = "invalid open template declaration metadata";
				return false;
			}
			for (const auto *Value : {&Declaration.Id, &Declaration.Kind, &Declaration.Name, &Declaration.Owner,
									  &Declaration.ModulePath, &Declaration.ReturnType, &Declaration.SourceFile})
			{
				if (!Text(*Value))
				{
					Error = "template declaration metadata exceeds safety limit";
					return false;
				}
			}
			std::unordered_set<std::string> Names, Ids;
			for (const auto *Schema : {&Declaration.EnclosingParameters, &Declaration.Parameters})
			{
				for (const auto &Parameter : Schema->GetParameters())
				{
					if (!Names.insert(Parameter.Name).second || !Ids.insert(Parameter.Id).second)
					{
						Error = "duplicate enclosing or own template parameter metadata";
						return false;
					}
				}
			}
			for (const auto &Parameter : Declaration.ParameterTypeNames)
			{
				if (!Text(Parameter))
				{
					return false;
				}
			}
		}
		// A nested enum's source domain contains the enclosing argument expressions.
		// Match against the complete closed enum, rather than accepting any enum
		// with the same storage width. Evaluation uses only encoded constant bindings.
		const auto EnumDomainMatches = [&](const std::string &Domain, const FBytecodeEnumDesc &Enum,
										   const std::unordered_map<std::string, std::string> &Bindings)
		{
			std::vector<FTypePathSegment> Path;
			if (!ParseTypePath(Domain, Path))
			{
				return false;
			}
			std::string Name;
			std::vector<std::string> Arguments;
			for (const auto &Segment : Path)
			{
				if (!Name.empty())
				{
					Name += '.';
				}
				Name += Segment.Name;
				Arguments.insert(Arguments.end(), Segment.Arguments.begin(), Segment.Arguments.end());
			}
			if (Name != Enum.Scope.QualifiedName && Name != StripTypeScopeMarkers(Enum.Scope.QualifiedName))
			{
				return false;
			}
			const auto &Concrete = Enum.TemplateInstance.EnclosingArguments;
			if (Arguments.size() != Concrete.size())
			{
				return false;
			}
			for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
			{
				auto Argument = Arguments[Index];
				if (const auto Binding = Bindings.find(Argument); Binding != Bindings.end())
				{
					Argument = Binding->second;
				}
				if (Argument == Concrete[Index])
				{
					continue;
				}
				FTemplateArgument Expected;
				if (Concrete[Index].rfind("@value:", 0) != 0 ||
					!DecodeTemplateArgument(std::string_view(Concrete[Index]).substr(7), Expected) ||
					Argument.size() > 65536)
				{
					return false;
				}
				VeroScriptLexer Lexer(Argument);
				VeroScriptParser Parser(Lexer.Tokenize(), "<template-metadata>");
				auto Expression = Parser.ParseExpressionFragment({}, {});
				if (!Expression || Lexer.HasError() || Parser.HasError())
				{
					return false;
				}
				FTemplateEvaluationContext Context;
				bool ExternalConstant = false;
				Context.ResolveName = [&](std::string_view Name, const FSourceLocation &)
				{
					FTemplateConstantSymbol Constant;
					const auto Found = Bindings.find(std::string(Name));
					FTemplateArgument Value;
					if (Found == Bindings.end())
					{
						// The closed type was captured after compiler binding. A loader
						// has no source environment for unrelated const dependencies.
						ExternalConstant = true;
						Constant.Status = ETemplateEvaluationStatus::Dependent;
						Constant.Value.Type = Expected.Value.Type;
						return Constant;
					}
					if (Found->second.rfind("@value:", 0) != 0 ||
						!DecodeTemplateArgument(std::string_view(Found->second).substr(7), Value))
					{
						return Constant;
					}
					Constant.Value = Value.Value;
					Constant.Status = ETemplateEvaluationStatus::Constant;
					return Constant;
				};
				auto Result = EvaluateTemplateConstant(*Expression, Context);
				if (Result.Status == ETemplateEvaluationStatus::Dependent && ExternalConstant)
				{
					continue;
				}
				if (!Result.Succeeded())
				{
					return false;
				}
				Result = ConvertTemplateConstant(Result.Value, Expected.Value.Type, false, {});
				if (!Result.Succeeded() || Result.Value.Bits != Expected.Value.Bits)
				{
					return false;
				}
			}
			return true;
		};
		const auto Instance = [&](const FTemplateInstanceInfo &Value, const std::string_view ExpectedKind)
		{
			if (Value.DeclarationId.empty())
			{
				return Value.Arguments.empty() && Value.EnclosingArguments.empty() && Value.ValueTypeNames.empty();
			}
			const auto Found = Declarations.find(Value.DeclarationId);
			if (Found == Declarations.end() || Found->second->Kind != ExpectedKind)
			{
				return false;
			}
			std::unordered_map<std::string, std::string> Bindings;
			std::size_t ValueTypeIndex = 0;
			const auto Check = [&](const FTemplateParameterList &Schema, const std::vector<std::string> &Arguments)
			{
				if (Arguments.size() != Schema.GetParameters().size())
				{
					return false;
				}
				for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
				{
					if (Schema.GetParameters()[Index].Kind == ETemplateParameterKind::Type)
					{
						Bindings.emplace(Schema.GetParameters()[Index].Name, Arguments[Index]);
					}
				}
				for (std::size_t Index = 0; Index < Arguments.size(); ++Index)
				{
					const auto &Argument = Arguments[Index];
					if (Argument.empty() || !Text(Argument) || Argument.rfind("@default:", 0) == 0 ||
						Argument.find("@type_parameter_") != std::string::npos)
					{
						return false;
					}
					const auto &Parameter = Schema.GetParameters()[Index];
					if (Parameter.Kind == ETemplateParameterKind::Type)
					{
						if (Argument.rfind("@value:", 0) == 0)
						{
							return false;
						}
						Bindings.emplace(Parameter.Name, Argument);
						continue;
					}
					FTemplateArgument Decoded;
					if (Argument.rfind("@value:", 0) != 0 ||
						!DecodeTemplateArgument(std::string_view(Argument).substr(7), Decoded) ||
						Decoded.Kind != ETemplateParameterKind::Value)
					{
						return false;
					}
					if (ValueTypeIndex >= Value.ValueTypeNames.size() || !Text(Value.ValueTypeNames[ValueTypeIndex]) ||
						Value.ValueTypeNames[ValueTypeIndex++] != Decoded.Value.Type.Name)
					{
						return false;
					}
					auto Domain = Parameter.ValueTypeName;
					bool DependentDomain = false;
					if (const auto Binding = Bindings.find(Domain); Binding != Bindings.end())
					{
						Domain = Binding->second;
						DependentDomain = true;
					}
					FTemplateValueType Type;
					if (TryGetTemplateValueType(Domain, Type) &&
						(Decoded.Value.Type.Name != Type.Name || Decoded.Value.Type.bEnum))
					{
						return false;
					}
					const auto ExpectedEnum =
						std::find_if(Module.Enums.begin(), Module.Enums.end(),
									 [&](const auto &Candidate)
									 {
										 return Candidate.Name == Domain ||
												Candidate.Name == Found->second->ModulePath + '\x1F' + Domain;
									 });
					if (ExpectedEnum != Module.Enums.end() &&
						(!Decoded.Value.Type.bEnum || Decoded.Value.Type.Name != ExpectedEnum->Name))
					{
						return false;
					}
					if (DependentDomain && ExpectedEnum == Module.Enums.end() && !TryGetTemplateValueType(Domain, Type))
					{
						return false;
					}
					if (!TryGetTemplateValueType(Domain, Type) && ExpectedEnum == Module.Enums.end() &&
						!Decoded.Value.Type.bEnum)
					{
						return false;
					}
					if (Decoded.Value.Type.bEnum)
					{
						const auto Enum = std::find_if(Module.Enums.begin(), Module.Enums.end(),
													   [&](const auto &Candidate)
													   {
														   return Candidate.Name == Decoded.Value.Type.Name;
													   });
						if (Enum == Module.Enums.end() ||
							(ExpectedEnum == Module.Enums.end() && !EnumDomainMatches(Domain, *Enum, Bindings)) ||
							!TryGetTemplateValueType(Enum->UnderlyingTypeName, Type) ||
							Decoded.Value.Type.Width != Type.Width || Decoded.Value.Type.bUnsigned != Type.bUnsigned ||
							Decoded.Value.Type.bScopedEnum != Enum->bScoped)
						{
							return false;
						}
					}
					Bindings.emplace(Parameter.Name, Argument);
				}
				return true;
			};
			return Check(Found->second->EnclosingParameters, Value.EnclosingArguments) &&
				   Check(Found->second->Parameters, Value.Arguments) && ValueTypeIndex == Value.ValueTypeNames.size();
		};
		const auto Invalid = [&]()
		{
			Error = "invalid closed template instance metadata";
			return false;
		};
		for (const auto &Function : Module.Functions)
		{
			if (!Instance(Function.TemplateInstance,
						  Function.bIsMethod || Function.bIsStaticMethod ? "method" : "function"))
			{
				return Invalid();
			}
		}
		for (const auto &Type : Module.Types)
		{
			if (!Instance(Type.TemplateInstance, Type.Kind == EScriptObjectKind::Class ? "class" : "struct"))
			{
				return Invalid();
			}
		}
		for (const auto &Interface : Module.Interfaces)
		{
			if (!Instance(Interface.TemplateInstance, "interface"))
			{
				return Invalid();
			}
		}
		for (const auto &Enum : Module.Enums)
		{
			if (!Instance(Enum.TemplateInstance, "enum"))
			{
				return Invalid();
			}
		}
		return true;
	}
} // namespace VeroScript
