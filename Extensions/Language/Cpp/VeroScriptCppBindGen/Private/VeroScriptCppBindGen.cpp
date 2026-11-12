// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCppBindGen.h"
#include "VeroScriptJsonString.h"

#include "VeroScriptCppToolOutput.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptHostBindingCatalog.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace VeroScript::CppBindGen
{
	using namespace CppFrontend;

	namespace
	{
		struct FNativeInfo
		{
			ENativeType Type = ENativeType::Void;
			std::string VeroType;
			std::string HostType;
			std::string ElementType;
			std::string KeyType;
			std::string ValueType;
		};

		void AddError(
			std::vector<FDiagnostic>& Diagnostics,
			const CppFrontend::FSourceLocation& Location,
			std::string Code,
			std::string Message)
		{
			Diagnostics.push_back({EDiagnosticSeverity::Error, std::move(Code),
				std::move(Message), Location});
		}

		std::string Trim(std::string Value)
		{
			const auto IsSpace = [](const unsigned char C)
			{
				return std::isspace(C) != 0;
			};
			Value.erase(Value.begin(), std::find_if(Value.begin(), Value.end(),
				[&](const char C)
				{
					return !IsSpace(static_cast<unsigned char>(C));
				}));
			Value.erase(std::find_if(Value.rbegin(), Value.rend(),
				[&](const char C)
				{
					return !IsSpace(static_cast<unsigned char>(C));
				}).base(),
				Value.end());
			return Value;
		}

		std::string LeafName(const std::string_view Name)
		{
			const std::size_t At = Name.rfind("::");
			return At == std::string_view::npos ? std::string(Name) : std::string(Name.substr(At + 2));
		}

		bool SplitGeneric(
			const std::string& Type,
			std::string& OutBase,
			std::vector<std::string>& OutArguments)
		{
			OutBase.clear();
			OutArguments.clear();
			const std::size_t Open = Type.find('<');
			if (Open == std::string::npos || Type.empty() || Type.back() != '>')
			{
				return false;
			}
			OutBase = Trim(Type.substr(0, Open));
			std::size_t Begin = Open + 1;
			int Depth = 0;
			for (std::size_t Index = Begin; Index + 1 < Type.size(); ++Index)
			{
				if (Type[Index] == '<')
				{
					++Depth;
				}
				else if (Type[Index] == '>')
				{
					--Depth;
				}
				else if (Type[Index] == ',' && Depth == 0)
				{
					OutArguments.push_back(Trim(Type.substr(Begin, Index - Begin)));
					Begin = Index + 1;
				}
			}
			OutArguments.push_back(Trim(Type.substr(Begin, Type.size() - Begin - 1)));
			return std::none_of(OutArguments.begin(), OutArguments.end(),
				[](const std::string& Argument)
				{
					return Argument.empty();
				});
		}

		bool IsKnown(const std::vector<std::string>& Names, const std::string_view Name)
		{
			return std::find(Names.begin(), Names.end(), Name) != Names.end();
		}

		std::string NormalizeAttributeValue(const std::string& Value)
		{
			std::size_t Quote = Value.find('"');
			if (Quote == std::string::npos || Value.back() != '"')
			{
				return Value;
			}
			if (Quote > 2 || Value.substr(0, Quote).find('R') != std::string::npos)
			{
				return Value;
			}
			std::string Result;
			for (std::size_t Index = Quote + 1; Index + 1 < Value.size(); ++Index)
			{
				if (Value[Index] != '\\' || Index + 2 >= Value.size())
				{
					Result.push_back(Value[Index]);
					continue;
				}
				const char Escaped = Value[++Index];
				switch (Escaped)
				{
				case 'n':
					{
						Result.push_back('\n');
						break;
					}
				case 'r':
					{
						Result.push_back('\r');
						break;
					}
				case 't':
					{
						Result.push_back('\t');
						break;
					}
				case '\\':
					{
						Result.push_back('\\');
						break;
					}
				case '"':
					{
						Result.push_back('"');
						break;
					}
				default:
					{
						Result.push_back('\\');
						Result.push_back(Escaped);
						break;
					}
				}
			}
			return Result;
		}

		bool ConvertAttributes(
			const std::vector<FAttribute>& Attributes,
			std::vector<FHostAnnotationInstance>& Out,
			std::vector<FDiagnostic>& Diagnostics)
		{
			for (const FAttribute& Attribute : Attributes)
			{
				if (!Attribute.NamespaceName.empty()
					&& Attribute.NamespaceName != "veroscript"
					&& Attribute.NamespaceName != "vero")
				{
					AddError(Diagnostics, Attribute.Location,
						"cpp.bindgen.attribute_namespace_unsupported",
						"only unscoped, [[veroscript::...]], and [[vero::...]] attributes can map unambiguously to VeroScript annotations: "
						+ Attribute.NamespaceName + "::" + Attribute.Name);
					return false;
				}
				FHostAnnotationInstance Instance;
				Instance.Name = Attribute.Name;
				for (const FAttributeArgument& Argument : Attribute.Arguments)
				{
					FHostAnnotationArgument Converted;
					Converted.Name = Argument.Name;
					if (Argument.bHasValue)
					{
						Converted.Value = NormalizeAttributeValue(Argument.Value);
					}
					Instance.Arguments.push_back(std::move(Converted));
				}
				Out.push_back(std::move(Instance));
			}
			return true;
		}

		bool TryNativeInfo(
			const FType& Type,
			const FTypeMapOptions& MapOptions,
			FNativeInfo& Out,
			std::string& OutCode,
			std::string& OutMessage)
		{
			Out = {};
			const FTypeMapResult Mapped = MapTypeToVeroScript(Type, MapOptions);
			if (!Mapped.bSuccess)
			{
				OutCode = Mapped.ErrorCode;
				OutMessage = Mapped.ErrorMessage;
				return false;
			}
			Out.VeroType = Mapped.VeroType;
			static const std::unordered_map<std::string, ENativeType> Scalars = {
				{"void", ENativeType::Void}, {"bool", ENativeType::Bool},
				{"char", ENativeType::Int8}, {"int8", ENativeType::Int8},
				{"unsigned char", ENativeType::UInt8}, {"uint8", ENativeType::UInt8},
				{"short", ENativeType::Int16}, {"int16", ENativeType::Int16},
				{"unsigned short", ENativeType::UInt16}, {"uint16", ENativeType::UInt16},
				{"int", ENativeType::Int}, {"int32", ENativeType::Int},
				{"unsigned int", ENativeType::Unsigned}, {"uint32", ENativeType::Unsigned},
				{"long", ENativeType::Int64}, {"int64", ENativeType::Int64},
				{"unsigned long", ENativeType::UInt64}, {"uint64", ENativeType::UInt64},
				{"float", ENativeType::Float}, {"double", ENativeType::Double},
				{"string", ENativeType::String}};
			if (const auto It = Scalars.find(Out.VeroType); It != Scalars.end())
			{
				Out.Type = It->second;
				return true;
			}
			if (IsKnown(MapOptions.KnownEnumTypes, Out.VeroType))
			{
				Out.Type = ENativeType::Enum;
				Out.HostType = Out.VeroType;
				return true;
			}
			if (IsKnown(MapOptions.KnownHostTypes, Out.VeroType))
			{
				Out.Type = ENativeType::HostObject;
				Out.HostType = Out.VeroType;
				return true;
			}

			std::string Base;
			std::vector<std::string> Arguments;
			if (!SplitGeneric(Out.VeroType, Base, Arguments))
			{
				OutCode = "cpp.bindgen.unsupported_native_abi_type";
				OutMessage = "type cannot be represented by the current native binding ABI: " + Out.VeroType;
				return false;
			}
			const auto Unary = [&](const ENativeType NativeType) -> bool
			{
				if (Arguments.size() != 1)
				{
					return false;
				}
				Out.Type = NativeType;
				Out.ElementType = Arguments[0];
				return true;
			};
			const auto Binary = [&](const ENativeType NativeType) -> bool
			{
				if (Arguments.size() != 2)
				{
					return false;
				}
				Out.Type = NativeType;
				Out.KeyType = Arguments[0];
				Out.ValueType = Arguments[1];
				return true;
			};
			if (Base == "array")
			{
				return Unary(ENativeType::Array);
			}
			if (Base == "set")
			{
				return Unary(ENativeType::Set);
			}
			if (Base == "multiset")
			{
				return Unary(ENativeType::MultiSet);
			}
			if (Base == "ordered_multiset")
			{
				return Unary(ENativeType::OrderedMultiSet);
			}
			if (Base == "queue")
			{
				return Unary(ENativeType::Queue);
			}
			if (Base == "stack")
			{
				return Unary(ENativeType::Stack);
			}
			if (Base == "deque")
			{
				return Unary(ENativeType::Deque);
			}
			if (Base == "channel")
			{
				return Unary(ENativeType::Channel);
			}
			if (Base == "priority_queue")
			{
				return Unary(ENativeType::PriorityQueue);
			}
			if (Base == "map")
			{
				return Binary(ENativeType::Map);
			}
			if (Base == "ordered_map")
			{
				return Binary(ENativeType::OrderedMap);
			}
			if (Base == "multimap")
			{
				return Binary(ENativeType::MultiMap);
			}
			if (Base == "ordered_multimap")
			{
				return Binary(ENativeType::OrderedMultiMap);
			}

			OutCode = "cpp.bindgen.unsupported_native_abi_type";
			OutMessage = "type has source-level syntax but no native binding ABI representation: " + Out.VeroType;
			return false;
		}

		void ApplyReturn(const FNativeInfo& Info, FHostFunctionDesc& Out)
		{
			Out.ReturnType = Info.Type;
			Out.ReturnHostTypeName = Info.HostType;
			Out.ReturnElementTypeName = Info.ElementType;
			Out.ReturnKeyTypeName = Info.KeyType;
			Out.ReturnValueTypeName = Info.ValueType;
		}

		void ApplyReturn(const FNativeInfo& Info, FHostMemberDesc& Out)
		{
			Out.ReturnType = Info.Type;
			Out.ReturnHostTypeName = Info.HostType;
			Out.ReturnElementTypeName = Info.ElementType;
			Out.ReturnKeyTypeName = Info.KeyType;
			Out.ReturnValueTypeName = Info.ValueType;
		}

		template <typename TDesc>
		void AppendParam(const FNativeInfo& Info, const FParameter& Param, TDesc& Out)
		{
			Out.ParamTypes.push_back(Info.Type);
			Out.ParamHostTypeNames.push_back(Info.HostType);
			Out.ParamElementTypeNames.push_back(Info.ElementType);
			Out.ParamKeyTypeNames.push_back(Info.KeyType);
			Out.ParamValueTypeNames.push_back(Info.ValueType);
			std::uint8_t Flags = 0;
			if (Param.Type.Reference == EReferenceKind::LValue)
			{
				Flags = NativeParamFlagRef;
				if (Param.Type.bConst)
				{
					Flags |= NativeParamFlagConstRef;
				}
			}
			Out.ParamFlags.push_back(Flags);
			Out.ParamNames.push_back(Param.Name);
		}

		bool BuildSignature(
			const FFunction& Function,
			const FTypeMapOptions& MapOptions,
			FHostFunctionDesc& Out,
			std::vector<FDiagnostic>& Diagnostics)
		{
			for (const FParameter& Param : Function.Parameters)
			{
				if (!Param.DefaultValue.empty())
				{
					AddError(Diagnostics, Param.Location,
						"cpp.bindgen.default_argument_metadata_unsupported",
						"the binding catalog cannot preserve C++ default arguments");
					return false;
				}
			}
			if (Function.ReturnType.Reference != EReferenceKind::None)
			{
				AddError(Diagnostics, Function.ReturnType.Location,
					"cpp.bindgen.reference_return_unsupported",
					"native binding metadata cannot represent a C++ reference return");
				return false;
			}
			FNativeInfo Return;
			std::string Code;
			std::string Message;
			if (!TryNativeInfo(Function.ReturnType, MapOptions, Return, Code, Message))
			{
				AddError(Diagnostics, Function.ReturnType.Location, std::move(Code), std::move(Message));
				return false;
			}
			ApplyReturn(Return, Out);
			for (const FParameter& Param : Function.Parameters)
			{
				FNativeInfo Info;
				if (!TryNativeInfo(Param.Type, MapOptions, Info, Code, Message))
				{
					AddError(Diagnostics, Param.Location, std::move(Code), std::move(Message));
					return false;
				}
				if (Info.Type == ENativeType::Void)
				{
					AddError(Diagnostics, Param.Location, "cpp.bindgen.void_parameter",
						"a binding parameter cannot have type void");
					return false;
				}
				AppendParam(Info, Param, Out);
				Out.ParamAnnotations.emplace_back();
				if (!ConvertAttributes(Param.Attributes,
					Out.ParamAnnotations.back(), Diagnostics))
				{
					return false;
				}
			}
			return true;
		}

		bool BuildSignature(
			const FFunction& Function,
			const FTypeMapOptions& MapOptions,
			FHostMemberDesc& Out,
			std::vector<FDiagnostic>& Diagnostics)
		{
			for (const FParameter& Param : Function.Parameters)
			{
				if (!Param.DefaultValue.empty())
				{
					AddError(Diagnostics, Param.Location,
						"cpp.bindgen.default_argument_metadata_unsupported",
						"the binding catalog cannot preserve C++ default arguments");
					return false;
				}
			}
			if (Function.bConstructor)
			{
				Out.ReturnType = ENativeType::HostObject;
			}
			else
			{
				if (Function.ReturnType.Reference != EReferenceKind::None)
				{
					AddError(Diagnostics, Function.ReturnType.Location,
						"cpp.bindgen.reference_return_unsupported",
						"native binding metadata cannot represent a C++ reference return");
					return false;
				}
				FNativeInfo Return;
				std::string Code;
				std::string Message;
				if (!TryNativeInfo(Function.ReturnType, MapOptions, Return, Code, Message))
				{
					AddError(Diagnostics, Function.ReturnType.Location, std::move(Code), std::move(Message));
					return false;
				}
				ApplyReturn(Return, Out);
			}
			for (const FParameter& Param : Function.Parameters)
			{
				FNativeInfo Info;
				std::string Code;
				std::string Message;
				if (!TryNativeInfo(Param.Type, MapOptions, Info, Code, Message))
				{
					AddError(Diagnostics, Param.Location, std::move(Code), std::move(Message));
					return false;
				}
				if (Info.Type == ENativeType::Void)
				{
					AddError(Diagnostics, Param.Location, "cpp.bindgen.void_parameter",
						"a binding parameter cannot have type void");
					return false;
				}
				AppendParam(Info, Param, Out);
				Out.ParamAnnotations.emplace_back();
				if (!ConvertAttributes(Param.Attributes,
					Out.ParamAnnotations.back(), Diagnostics))
				{
					return false;
				}
			}
			return true;
		}

		std::string FormatType(const FType& Type, const FTypeMapOptions& Options)
		{
			const FTypeMapResult Mapped = MapTypeToVeroScript(Type, Options);
			if (!Mapped.bSuccess)
			{
				return "<unsupported>";
			}
			if (Type.Reference == EReferenceKind::LValue)
			{
				return Type.bConst ? "const ref " + Mapped.VeroType : "ref " + Mapped.VeroType;
			}
			return Mapped.VeroType;
		}

		std::string BuildApiReference(
			const FTranslationUnit& Unit,
			const FTypeMapOptions& Options)
		{
			std::ostringstream Out;
			Out << "// SPDX-FileCopyrightText: VanstySanic\n// SPDX-License-Identifier: MIT\n\n"
			    << "// Generated C++ binding API reference.\n"
				"// Load the JSON catalog for compilation; do not compile this file as an implementation.\n\n";
			for (const FTypeAlias& Alias : Unit.TypeAliases)
			{
				FTypeMapOptions AliasOptions = Options;
				if (!Alias.TemplateParameters.empty())
				{
					Out << "template<";
					for (std::size_t Index = 0; Index < Alias.TemplateParameters.size(); ++Index)
					{
						if (Index > 0)
						{
							Out << ", ";
						}
						Out << "typename " << Alias.TemplateParameters[Index].Name;
						AliasOptions.KnownTypeParameters.push_back(
							Alias.TemplateParameters[Index].Name);
					}
					Out << "> ";
				}
				Out << "using " << Alias.QualifiedName << " = "
					<< FormatType(Alias.TargetType, AliasOptions) << ";\n";
			}
			for (const FEnum& Enum : Unit.Enums)
			{
				if (Enum.bForwardDeclaration)
				{
					Out << (Enum.bScoped ? "enum class " : "enum ") << Enum.QualifiedName
						<< " : " << Enum.UnderlyingType << ";\n";
					continue;
				}
				Out << (Enum.bScoped ? "enum class " : "enum ") << Enum.QualifiedName
					<< " : " << Enum.UnderlyingType << " { ";
				for (std::size_t Index = 0; Index < Enum.Values.size(); ++Index)
				{
					if (Index > 0)
					{
						Out << ", ";
					}
					Out << Enum.Values[Index].Name;
					if (!Enum.Values[Index].Initializer.empty())
					{
						Out << " = " << Enum.Values[Index].Initializer;
					}
				}
				Out << " };\n";
			}
			for (const FRecord& Record : Unit.Records)
			{
				if (Record.bForwardDeclaration)
				{
					Out << (Record.Kind == ERecordKind::Class ? "class " : "struct ")
						<< Record.QualifiedName << ";\n";
					continue;
				}
				Out << (Record.Kind == ERecordKind::Class ? "class " : "struct ")
					<< Record.QualifiedName << "\n{\n";
				for (const FField& Field : Record.Fields)
				{
					if (Field.Access != EAccess::Public)
					{
						continue;
					}
					Out << "\t" << (Field.bStatic ? "static " : "")
						<< FormatType(Field.Type, Options) << ' ' << Field.Name << ";\n";
				}
				for (const FFunction& Method : Record.Methods)
				{
					if (Method.Access != EAccess::Public)
					{
						continue;
					}
					Out << "\t";
					if (Method.bStatic)
					{
						Out << "static ";
					}
					if (!Method.bConstructor && !Method.bDestructor)
					{
						Out << FormatType(Method.ReturnType, Options) << ' ';
					}
					Out << Method.Name << '(';
					for (std::size_t Index = 0; Index < Method.Parameters.size(); ++Index)
					{
						if (Index > 0)
						{
							Out << ", ";
						}
						Out << FormatType(Method.Parameters[Index].Type, Options)
							<< ' ' << Method.Parameters[Index].Name;
					}
					Out << ')';
					if (Method.bConstMethod)
					{
						Out << " const";
					}
					if (Method.bDeleted)
					{
						Out << " = delete";
					}
					else if (Method.bPureVirtual)
					{
						Out << " = 0";
					}
					else if (Method.bDefaulted)
					{
						Out << " = default";
					}
					Out << ";\n";
				}
				Out << "};\n";
			}
			for (const FFunction& Function : Unit.Functions)
			{
				Out << FormatType(Function.ReturnType, Options) << ' ' << Function.QualifiedName << '(';
				for (std::size_t Index = 0; Index < Function.Parameters.size(); ++Index)
				{
					if (Index > 0)
					{
						Out << ", ";
					}
					Out << FormatType(Function.Parameters[Index].Type, Options)
						<< ' ' << Function.Parameters[Index].Name;
				}
				Out << ");\n";
			}
			return Out.str();
		}

		std::string JsonEscape(const std::string& Value)
		{
			return JsonStringDetail::Escape(Value, JsonStringDetail::EControlEscapeStyle::Short);
		}

		std::string OutputStem(const std::string& Path)
		{
			const std::size_t Separator = Path.find_last_of("/\\");
			std::string Name = Path.substr(Separator == std::string::npos ? 0 : Separator + 1);
			const std::size_t Dot = Name.find_last_of('.');
			if (Dot != std::string::npos && Dot != 0)
			{
				Name.resize(Dot);
			}
			return Name.empty() ? "bindings" : Name;
		}

		std::uint64_t StablePathHash(const std::string& Path)
		{
			std::uint64_t Value = UINT64_C(1469598103934665603);
			for (const unsigned char Character : Path)
			{
				Value ^= Character;
				Value *= UINT64_C(1099511628211);
			}
			return Value;
		}

		std::string HexSuffix(const std::uint64_t Value)
		{
			static constexpr char Hex[] = "0123456789abcdef";
			std::string Result(16, '0');
			for (std::size_t Index = 0; Index < Result.size(); ++Index)
			{
				const std::size_t Shift = (Result.size() - Index - 1) * 4;
				Result[Index] = Hex[(Value >> Shift) & 0xf];
			}
			return Result;
		}
	}

	bool FBindGenResult::Succeeded() const
	{
		return std::none_of(Diagnostics.begin(), Diagnostics.end(),
			[](const FDiagnostic& Diagnostic)
			{
				return Diagnostic.Severity == EDiagnosticSeverity::Error;
			});
	}

	FBindGenResult GenerateBindings(
		const FTranslationUnit& Unit,
		const FBindGenOptions& Options)
	{
		FBindGenResult Result;
		for (const FVariable& Variable : Unit.Variables)
		{
			AddError(Result.Diagnostics, Variable.Location,
				"cpp.bindgen.global_variable_unsupported",
				"the binding catalog has no free global-variable declaration kind: "
				+ Variable.QualifiedName);
		}
		FTypeMapOptions MapOptions;
		MapOptions.KnownTypeAliases = Unit.TypeAliases;
		for (const FRecord& Record : Unit.Records)
		{
			MapOptions.KnownHostTypes.push_back(Record.QualifiedName);
		}
		for (const FEnum& Enum : Unit.Enums)
		{
			MapOptions.KnownEnumTypes.push_back(Enum.QualifiedName);
		}

		FHostBindingRegistry Registry;
		std::unordered_map<std::string, FHostTypeId> TypeIds;
		std::string Error;
		for (const FEnum& Enum : Unit.Enums)
		{
			FHostTypeId Id;
			std::vector<FHostAnnotationInstance> TypeAnnotations;
			if (!ConvertAttributes(Enum.Attributes, TypeAnnotations, Result.Diagnostics))
			{
				continue;
			}
			if (!Registry.RegisterType(Enum.QualifiedName, EHostTypeKind::Enum, Id, Error,
					std::move(TypeAnnotations))
				|| !Registry.SetEnumScoped(Id, Enum.bScoped, Error)
				|| !Registry.SetEnumUnderlyingType(Id, Enum.UnderlyingType, Error))
			{
				AddError(Result.Diagnostics, Enum.Location, "cpp.bindgen.registry_rejected_enum", Error);
				continue;
			}
			TypeIds.emplace(Enum.QualifiedName, Id);
		}
		for (const FRecord& Record : Unit.Records)
		{
			if (!Record.TemplateParameters.empty())
			{
				AddError(Result.Diagnostics, Record.Location,
					"cpp.bindgen.generic_host_member_protocol_required",
					"generic C++ host types require an instantiation callback/member prototype protocol; refusing to emit an incomplete catalog type: "
					+ Record.QualifiedName);
				continue;
			}
			if (!Record.BaseTypes.empty())
			{
				AddError(Result.Diagnostics, Record.Location, "cpp.bindgen.host_inheritance_unsupported",
					"the current binding catalog cannot preserve C++ host-to-host base classes: " + Record.QualifiedName);
				continue;
			}
			FHostTypeId Id;
			const EHostTypeKind Kind = Record.Kind == ERecordKind::Class
				? EHostTypeKind::Class
				: EHostTypeKind::Struct;
			std::vector<FHostAnnotationInstance> TypeAnnotations;
			if (!ConvertAttributes(Record.Attributes, TypeAnnotations, Result.Diagnostics))
			{
				continue;
			}
			if (!Registry.RegisterType(Record.QualifiedName, Kind, Id, Error,
				std::move(TypeAnnotations)))
			{
				AddError(Result.Diagnostics, Record.Location, "cpp.bindgen.registry_rejected_type", Error);
				continue;
			}
			TypeIds.emplace(Record.QualifiedName, Id);
		}
		for (const FTypeAlias& Alias : Unit.TypeAliases)
		{
			FTypeMapOptions AliasOptions = MapOptions;
			std::vector<std::string> TypeParams;
			for (const FTemplateParameter& Parameter : Alias.TemplateParameters)
			{
				if (Parameter.bHasDefault)
				{
					AddError(Result.Diagnostics, Parameter.Location,
						"cpp.bindgen.type_alias_template_default_unsupported",
						"the binding catalog cannot preserve a C++ alias-template default argument");
					TypeParams.clear();
					break;
				}
				TypeParams.push_back(Parameter.Name);
				AliasOptions.KnownTypeParameters.push_back(Parameter.Name);
			}
			if (TypeParams.size() != Alias.TemplateParameters.size())
			{
				continue;
			}
			const FTypeMapResult Target = MapTypeToVeroScript(
				Alias.TargetType, AliasOptions);
			if (!Target.bSuccess)
			{
				AddError(Result.Diagnostics, Alias.Location,
					Target.ErrorCode, Target.ErrorMessage);
				continue;
			}
			if (!Registry.RegisterTypeAlias(Alias.QualifiedName,
				std::move(TypeParams), Target.VeroType, Error))
			{
				AddError(Result.Diagnostics, Alias.Location,
					"cpp.bindgen.registry_rejected_type_alias", Error);
			}
		}

		for (const FEnum& Enum : Unit.Enums)
		{
			const auto IdIt = TypeIds.find(Enum.QualifiedName);
			if (IdIt == TypeIds.end())
			{
				continue;
			}
			std::vector<std::int64_t> NumericValues;
			std::size_t FailedIndex = 0;
			std::string ExpressionError;
			if (!EvaluateEnumValues(
				Enum, NumericValues, FailedIndex, ExpressionError))
			{
				const CppFrontend::FSourceLocation& Location = FailedIndex < Enum.Values.size()
					? Enum.Values[FailedIndex].Location
					: Enum.Location;
				AddError(Result.Diagnostics, Location,
					"cpp.bindgen.enum_initializer_not_constant_integer",
					"unsupported enum integer constant expression: " + ExpressionError);
				continue;
			}
			std::int64_t EnumMinimum = 0;
			std::int64_t EnumMaximum = 0;
			if (!TryGetEnumValueRange(Enum.UnderlyingType, EnumMinimum, EnumMaximum))
			{
				AddError(Result.Diagnostics, Enum.Location,
					"cpp.bindgen.enum_underlying_unsupported",
					"cannot validate enum value range for underlying type: "
					+ Enum.UnderlyingType);
				continue;
			}
			for (std::size_t ValueIndex = 0;
				ValueIndex < Enum.Values.size(); ++ValueIndex)
			{
				const CppFrontend::FEnumValue& Value = Enum.Values[ValueIndex];
				const std::int64_t Numeric = NumericValues[ValueIndex];
				if (Numeric < EnumMinimum || Numeric > EnumMaximum)
				{
					AddError(Result.Diagnostics, Value.Location,
						"cpp.bindgen.enum_value_out_of_range",
						"enum value is outside the range of " + Enum.UnderlyingType
						+ ": " + std::to_string(Numeric));
					continue;
				}
				FHostMemberDesc Member;
				Member.Kind = EHostMemberKind::EnumMember;
				Member.Name = Value.Name;
				Member.EnumValue = Numeric;
				if (!ConvertAttributes(Value.Attributes, Member.Annotations,
					Result.Diagnostics))
				{
					continue;
				}
				if (!Registry.RegisterMember(IdIt->second, std::move(Member), Error))
				{
					AddError(Result.Diagnostics, Value.Location, "cpp.bindgen.registry_rejected_enum_member", Error);
					continue;
				}
			}
		}

		for (const FRecord& Record : Unit.Records)
		{
			const auto IdIt = TypeIds.find(Record.QualifiedName);
			if (IdIt == TypeIds.end())
			{
				continue;
			}
			for (const FField& Field : Record.Fields)
			{
				if (Field.Access != EAccess::Public)
				{
					continue;
				}
				if (Field.Type.Reference != EReferenceKind::None)
				{
					AddError(Result.Diagnostics, Field.Location,
						"cpp.bindgen.reference_field_unsupported",
						"C++ reference-member alias and lifetime semantics cannot be represented by a binding catalog field: "
						+ Record.QualifiedName + "::" + Field.Name);
					continue;
				}
				if (Field.bStatic && !Field.bConstexpr && !Field.Type.bConst)
				{
					AddError(Result.Diagnostics, Field.Location, "cpp.bindgen.mutable_static_field_unsupported",
						"mutable C++ static fields cannot be represented as binding catalog constants");
					continue;
				}
				FNativeInfo Info;
				std::string Code;
				std::string Message;
				if (!TryNativeInfo(Field.Type, MapOptions, Info, Code, Message))
				{
					AddError(Result.Diagnostics, Field.Location, std::move(Code), std::move(Message));
					continue;
				}
				FHostMemberDesc Member;
				Member.Kind = Field.bStatic ? EHostMemberKind::StaticConstant : EHostMemberKind::Field;
				Member.Name = Field.Name;
				ApplyReturn(Info, Member);
				if (!ConvertAttributes(Field.Attributes, Member.Annotations,
					Result.Diagnostics))
				{
					continue;
				}
				if (!Registry.RegisterMember(IdIt->second, std::move(Member), Error))
				{
					AddError(Result.Diagnostics, Field.Location, "cpp.bindgen.registry_rejected_field", Error);
				}
			}
			for (const FFunction& Method : Record.Methods)
			{
				if (Method.Access != EAccess::Public)
				{
					continue;
				}
				// Destruction is represented by the host value/object lifetime protocol,
				// not as an explicitly callable binding-catalog member.
				if (Method.bDestructor)
				{
					continue;
				}
				if (!Method.TemplateParameters.empty())
				{
					AddError(Result.Diagnostics, Method.Location,
						"cpp.bindgen.generic_method_protocol_required",
						"generic C++ methods require a generic native invocation protocol; refusing to erase template parameters from: "
						+ Method.QualifiedName);
					continue;
				}
				if (Method.bConsteval)
				{
					AddError(Result.Diagnostics, Method.Location,
						"cpp.bindgen.consteval_function_unsupported",
						"a consteval C++ function cannot be exposed as a runtime host binding");
					continue;
				}
				if (Method.bDeleted)
				{
					AddError(Result.Diagnostics, Method.Location, "cpp.bindgen.deleted_member_metadata_unsupported",
						"the host binding catalog has no deleted-member flag; refusing to expose a deleted C++ overload");
					continue;
				}
				FHostMemberDesc Member;
				Member.Kind = Method.bConstructor ? EHostMemberKind::Constructor
					: (Method.bStatic ? EHostMemberKind::StaticMethod : EHostMemberKind::Method);
				Member.Name = Method.bConstructor ? std::string{} : Method.Name;
				Member.bConstMethod = Method.bConstMethod;
				Member.bMutableThis = Record.Kind == ERecordKind::Struct
					&& !Method.bStatic && !Method.bConstMethod;
				Member.bOperator = Method.Name.rfind("operator", 0) == 0;
				if (!ConvertAttributes(Method.Attributes, Member.Annotations,
					Result.Diagnostics))
				{
					continue;
				}
				if (!BuildSignature(Method, MapOptions, Member, Result.Diagnostics))
				{
					continue;
				}
				Member.Callback = BindingCatalogStubNative;
				const bool Registered = Member.bOperator
					? Registry.RegisterOperator(IdIt->second, std::move(Member), Error)
					: Registry.RegisterMember(IdIt->second, std::move(Member), Error);
				if (!Registered)
				{
					AddError(Result.Diagnostics, Method.Location, "cpp.bindgen.registry_rejected_method", Error);
				}
			}
		}

		std::unordered_set<std::string> FreeNames;
		for (const FFunction& Function : Unit.Functions)
		{
			if (!Function.TemplateParameters.empty())
			{
				AddError(Result.Diagnostics, Function.Location,
					"cpp.bindgen.generic_function_protocol_required",
					"generic C++ functions require a generic native invocation protocol; refusing to erase template parameters from: "
					+ Function.QualifiedName);
				continue;
			}
			if (Function.bStatic)
			{
				AddError(Result.Diagnostics, Function.Location,
					"cpp.bindgen.internal_linkage_function_unsupported",
					"a static free function has internal linkage and must not be exported as a host binding: "
					+ Function.QualifiedName);
				continue;
			}
			if (Function.bConsteval)
			{
				AddError(Result.Diagnostics, Function.Location,
					"cpp.bindgen.consteval_function_unsupported",
					"a consteval C++ function cannot be exposed as a runtime host binding");
				continue;
			}
			if (!FreeNames.insert(Function.QualifiedName).second)
			{
				AddError(Result.Diagnostics, Function.Location, "cpp.bindgen.free_function_overload_unsupported",
					"the native free-function catalog requires unique names; overloaded C++ free function: "
					+ Function.QualifiedName);
				continue;
			}
			if (Function.bDeleted)
			{
				AddError(Result.Diagnostics, Function.Location, "cpp.bindgen.deleted_function_metadata_unsupported",
					"the host binding catalog has no deleted free-function flag");
				continue;
			}
			FHostFunctionDesc Desc;
			Desc.Name = Function.QualifiedName;
			if (!ConvertAttributes(Function.Attributes, Desc.Annotations,
				Result.Diagnostics))
			{
				continue;
			}
			if (!BuildSignature(Function, MapOptions, Desc, Result.Diagnostics))
			{
				continue;
			}
			if (!Registry.RegisterNative(std::move(Desc), BindingCatalogStubNative, nullptr, Error))
			{
				AddError(Result.Diagnostics, Function.Location, "cpp.bindgen.registry_rejected_function", Error);
			}
		}

		if (Result.Succeeded() && !ExportBindingCatalogJson(Registry, Result.CatalogJson))
		{
			AddError(Result.Diagnostics, {Unit.SourcePath, 1, 1, 0},
				"cpp.bindgen.catalog_export_failed", "failed to serialize the generated binding catalog");
		}
		if (Options.bEmitApiReference)
		{
			Result.ApiReference = BuildApiReference(Unit, MapOptions);
		}
		return Result;
	}

	FBindGenResult GenerateBindingsFromSource(
		const std::string_view Source,
		const std::string_view SourcePath,
		const FBindGenOptions& Options)
	{
		FParseOptions ParseOptions;
		ParseOptions.bAllowFunctionBodies = true;
		FParseResult Parsed = ParseSource(Source, SourcePath, ParseOptions);
		if (!Parsed.Succeeded())
		{
			FBindGenResult Result;
			Result.Diagnostics = std::move(Parsed.Diagnostics);
			return Result;
		}
		FBindGenResult Result = GenerateBindings(Parsed.Unit, Options);
		Result.Diagnostics.insert(Result.Diagnostics.begin(),
			std::make_move_iterator(Parsed.Diagnostics.begin()),
			std::make_move_iterator(Parsed.Diagnostics.end()));
		return Result;
	}

	FBindGenResult GenerateBindingsFromFile(
		const std::string_view FilePath,
		const FBindGenOptions& Options)
	{
		FParseResult Parsed = ParseFile(FilePath);
		if (!Parsed.Succeeded())
		{
			FBindGenResult Result;
			Result.Diagnostics = std::move(Parsed.Diagnostics);
			return Result;
		}
		FBindGenResult Result = GenerateBindings(Parsed.Unit, Options);
		Result.Diagnostics.insert(Result.Diagnostics.begin(),
			std::make_move_iterator(Parsed.Diagnostics.begin()),
			std::make_move_iterator(Parsed.Diagnostics.end()));
		return Result;
	}

	FBindGenResult GenerateBindingsFromFiles(
		const std::vector<std::string>& FilePaths,
		const FBindGenOptions& Options)
	{
		FParseResult Parsed = ParseFiles(FilePaths);
		if (!Parsed.Succeeded())
		{
			FBindGenResult Result;
			Result.Diagnostics = std::move(Parsed.Diagnostics);
			return Result;
		}
		FBindGenResult Result = GenerateBindings(Parsed.Unit, Options);
		Result.Diagnostics.insert(Result.Diagnostics.begin(),
			std::make_move_iterator(Parsed.Diagnostics.begin()),
			std::make_move_iterator(Parsed.Diagnostics.end()));
		return Result;
	}

	bool FBindGenProjectResult::Succeeded() const
	{
		return std::none_of(Diagnostics.begin(), Diagnostics.end(),
			[](const FDiagnostic& Diagnostic)
			{
				return Diagnostic.Severity == EDiagnosticSeverity::Error;
			});
	}

	FBindGenProjectResult GenerateBindingProject(
		const std::vector<std::string>& FilePaths,
		const FBindGenOptions& Options,
		const FBindGenProjectLayout& Layout)
	{
		FBindGenProjectResult Result;
		FParseOptions ProjectParseOptions;
		ProjectParseOptions.bDeferOutOfClassMemberResolution = true;
		std::vector<FTranslationUnit> Units;
		Units.reserve(FilePaths.size());
		CppTools::FFilePathSet SeenFiles;
		for (std::size_t FileIndex = 0; FileIndex < FilePaths.size(); ++FileIndex)
		{
			const std::string& FilePath = FilePaths[FileIndex];
			if (!SeenFiles.Insert(FilePath))
			{
				AddError(Result.Diagnostics, {FilePath, 1, 1, 0},
					"cpp.bindgen.duplicate_input_file",
					"the same C++ input file was supplied more than once");
				continue;
			}
			FParseResult Parsed = ParseFile(FilePath, ProjectParseOptions);
			const bool bParsed = Parsed.Succeeded();
			Result.Diagnostics.insert(Result.Diagnostics.end(),
				std::make_move_iterator(Parsed.Diagnostics.begin()),
				std::make_move_iterator(Parsed.Diagnostics.end()));
			if (bParsed)
			{
				Units.push_back(std::move(Parsed.Unit));
			}
		}
		if (Units.empty())
		{
			if (FilePaths.empty())
			{
				AddError(Result.Diagnostics, {"<cpp-files>", 1, 1, 0},
					"cpp.bindgen.no_input_files", "at least one C++ input file is required");
			}
			return Result;
		}
		FParseResult Merged = MergeTranslationUnits(Units);
		Result.Diagnostics.insert(Result.Diagnostics.end(),
			std::make_move_iterator(Merged.Diagnostics.begin()),
			std::make_move_iterator(Merged.Diagnostics.end()));
		if (!Result.Succeeded())
		{
			return Result;
		}

		FBindGenOptions CatalogOptions = Options;
		CatalogOptions.bEmitApiReference = false;
		FBindGenResult Catalog = GenerateBindings(Merged.Unit, CatalogOptions);
		Result.Diagnostics.insert(Result.Diagnostics.end(),
			std::make_move_iterator(Catalog.Diagnostics.begin()),
			std::make_move_iterator(Catalog.Diagnostics.end()));
		if (!Result.Succeeded())
		{
			return Result;
		}
		Result.CatalogJson = std::move(Catalog.CatalogJson);

		if (Options.bEmitApiReference)
		{
			FTypeMapOptions MapOptions;
			for (const FRecord& Record : Merged.Unit.Records)
			{
				MapOptions.KnownHostTypes.push_back(Record.QualifiedName);
			}
			for (const FEnum& Enum : Merged.Unit.Enums)
			{
				MapOptions.KnownEnumTypes.push_back(Enum.QualifiedName);
			}
			MapOptions.KnownTypeAliases = Merged.Unit.TypeAliases;
			std::map<std::string, std::size_t, CppTools::FOutputFileNameLess> StemCounts;
			for (const FTranslationUnit& Unit : Units)
			{
				++StemCounts[OutputStem(Unit.SourcePath)];
			}
			for (const FTranslationUnit& Unit : Units)
			{
				const std::string Stem = OutputStem(Unit.SourcePath);
				const std::string OutputName = Stem
					+ (StemCounts[Stem] > 1
						? "." + HexSuffix(StablePathHash(Unit.SourcePath)) : "")
					+ ".api.vs.txt";
				FTranslationUnit ReferenceUnit = Unit;
				for (FRecord& Record : ReferenceUnit.Records)
				{
					if (Record.bForwardDeclaration)
					{
						continue;
					}
					const auto MergedRecord = std::find_if(
						Merged.Unit.Records.begin(), Merged.Unit.Records.end(),
						[&](const FRecord& Candidate)
						{
							return Candidate.QualifiedName == Record.QualifiedName
									&& !Candidate.bForwardDeclaration;
						});
					if (MergedRecord != Merged.Unit.Records.end())
					{
						Record = *MergedRecord;
					}
				}
				const auto HasRecordOwner = [&](const std::string& Owner)
				{
					return std::any_of(Merged.Unit.Records.begin(),
						Merged.Unit.Records.end(), [&](const FRecord& Record)
						{
							return Record.QualifiedName == Owner;
						});
				};
				ReferenceUnit.Functions.erase(std::remove_if(
					ReferenceUnit.Functions.begin(), ReferenceUnit.Functions.end(),
					[&](const FFunction& Function)
					{
						return Function.bOutOfClassMember
								&& HasRecordOwner(Function.OwnerQualifiedName);
					}),
					ReferenceUnit.Functions.end());
				ReferenceUnit.Variables.erase(std::remove_if(
					ReferenceUnit.Variables.begin(), ReferenceUnit.Variables.end(),
					[&](const FVariable& Variable)
					{
						return Variable.bOutOfClassMember
								&& HasRecordOwner(Variable.OwnerQualifiedName);
					}),
					ReferenceUnit.Variables.end());
				Result.ApiReferences.push_back({Unit.SourcePath, OutputName,
					BuildApiReference(ReferenceUnit, MapOptions)});
			}
		}

		std::ostringstream Manifest;
		Manifest << "{\"version\":1,\"kind\":\"veroscript.cpp-binding-project\","
			"\"catalogRole\":\"merged\",\"catalog\":\""
			<< JsonEscape(Layout.CatalogPath) << "\",\"inputs\":[";
		for (std::size_t Index = 0; Index < Units.size(); ++Index)
		{
			if (Index > 0)
			{
				Manifest << ',';
			}
			Manifest << "\"" << JsonEscape(Units[Index].SourcePath) << "\"";
		}
		Manifest << "],\"apiReferences\":[";
		for (std::size_t Index = 0; Index < Result.ApiReferences.size(); ++Index)
		{
			if (Index > 0)
			{
				Manifest << ',';
			}
			const FBindGenApiReference& Reference = Result.ApiReferences[Index];
			Manifest << "{\"input\":\"" << JsonEscape(Reference.InputPath)
				<< "\",\"path\":\"";
			if (!Layout.ApiReferenceDirectory.empty())
			{
				Manifest << JsonEscape(Layout.ApiReferenceDirectory);
				const char Last = Layout.ApiReferenceDirectory.back();
				if (Last != '/' && Last != '\\')
				{
					Manifest << '/';
				}
			}
			Manifest << JsonEscape(Reference.OutputName) << "\"}";
		}
		Manifest << "]}";
		Result.ManifestJson = Manifest.str();
		return Result;
	}
}
