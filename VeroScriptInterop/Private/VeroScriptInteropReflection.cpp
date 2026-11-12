// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropInternal.h"
#include "VeroScriptJsonString.h"

namespace VeroScript::Interop
{
	namespace
	{
		std::string EscapeJson(const std::string_view Text)
		{
			return JsonStringDetail::Escape(Text, JsonStringDetail::EControlEscapeStyle::Short);
		}

		void AppendQuoted(std::string& Out, const std::string_view Value)
		{
			Out += '"';
			Out += EscapeJson(Value);
			Out += '"';
		}

		void AppendStringArray(std::string& Out, const std::vector<std::string>& Values)
		{
			Out += '[';
			for (std::size_t Index = 0; Index < Values.size(); ++Index)
			{
				if (Index)
				{
					Out += ',';
				}
				AppendQuoted(Out, Values[Index]);
			}
			Out += ']';
		}

		void AppendU8Array(std::string& Out, const std::vector<std::uint8_t>& Values)
		{
			Out += '[';
			for (std::size_t Index = 0; Index < Values.size(); ++Index)
			{
				if (Index)
				{
					Out += ',';
				}
				Out += std::to_string(Values[Index]);
			}
			Out += ']';
		}

		void AppendU16Array(std::string& Out, const std::vector<std::uint16_t>& Values)
		{
			Out += '[';
			for (std::size_t Index = 0; Index < Values.size(); ++Index)
			{
				if (Index)
				{
					Out += ',';
				}
				Out += std::to_string(Values[Index]);
			}
			Out += ']';
		}

		void AppendDeclMeta(std::string& Out, const FDeclMeta& Meta)
		{
			Out += '[';
			for (std::size_t Index = 0; Index < Meta.size(); ++Index)
			{
				if (Index)
				{
					Out += ',';
				}
				Out += "{\"name\":";
				AppendQuoted(Out, Meta[Index].first);
				Out += ",\"value\":";
				AppendQuoted(Out, Meta[Index].second);
				Out += '}';
			}
			Out += ']';
		}

		template <typename TMethod>
		void AppendMethod(std::string& Out, const TMethod& Method)
		{
			Out += "{\"name\":";
			AppendQuoted(Out, Method.Name);
			Out += ",\"parameterCount\":" + std::to_string(Method.ParamCount);
			Out += ",\"parameterTypes\":";
			AppendStringArray(Out, Method.ParamTypeNames);
			Out += ",\"parameterNames\":";
			AppendStringArray(Out, Method.ParamNames);
			Out += ",\"parameterFlags\":";
			AppendU8Array(Out, Method.ParamFlags);
			Out += ",\"returnType\":";
			AppendQuoted(Out, Method.ReturnTypeName);
			Out += ",\"returnsVoid\":";
			Out += Method.bReturnsVoid ? "true" : "false";
			Out += ",\"returnsRef\":";
			Out += Method.bReturnsRef ? "true" : "false";
			Out += ",\"returnsConstRef\":";
			Out += Method.bReturnsConstRef ? "true" : "false";
			Out += ",\"deleted\":";
			Out += Method.bDeleted ? "true" : "false";
			Out += ",\"constexpr\":";
			Out += Method.bConstexpr ? "true" : "false";
			Out += ",\"functionIndex\":" + std::to_string(Method.FunctionIndex);
			Out += ",\"linkageName\":";
			AppendQuoted(Out, Method.LinkageName);
			Out += ",\"metadata\":";
			AppendDeclMeta(Out, Method.DeclMeta);
			Out += '}';
		}

		void AppendFreeFunction(std::string& Out, const FFreeFunctionInfo& Function)
		{
			Out += "{\"name\":";
			AppendQuoted(Out, Function.Name);
			Out += ",\"parameterCount\":" + std::to_string(Function.ParamCount);
			Out += ",\"parameterTypes\":";
			AppendStringArray(Out, Function.ParamTypeNames);
			Out += ",\"parameterNames\":";
			AppendStringArray(Out, Function.ParamNames);
			Out += ",\"parameterFlags\":";
			AppendU8Array(Out, Function.ParamFlags);
			Out += ",\"returnType\":";
			AppendQuoted(Out, Function.ReturnTypeName);
			Out += ",\"returnsVoid\":";
			Out += Function.bReturnsVoid ? "true" : "false";
			Out += ",\"returnsRef\":";
			Out += Function.bReturnsRef ? "true" : "false";
			Out += ",\"returnsConstRef\":";
			Out += Function.bReturnsConstRef ? "true" : "false";
			Out += ",\"deleted\":";
			Out += Function.bDeleted ? "true" : "false";
			Out += ",\"constexpr\":";
			Out += Function.bConstexpr ? "true" : "false";
			Out += ",\"functionIndex\":" + std::to_string(Function.FunctionIndex);
			Out += ",\"linkageName\":";
			AppendQuoted(Out, Function.LinkageName);
			Out += ",\"modulePath\":";
			AppendQuoted(Out, Function.ModulePath);
			Out += ",\"exported\":";
			Out += Function.bExported ? "true" : "false";
			Out += ",\"metadata\":";
			AppendDeclMeta(Out, Function.DeclMeta);
			Out += '}';
		}

		void AppendInstanceMethod(std::string& Out, const FInstanceMethodInfo& Method)
		{
			Out += "{\"name\":";
			AppendQuoted(Out, Method.Name);
			Out += ",\"parameterCount\":" + std::to_string(Method.ParamCount);
			Out += ",\"parameterTypes\":";
			AppendStringArray(Out, Method.ParamTypeNames);
			Out += ",\"parameterNames\":";
			AppendStringArray(Out, Method.ParamNames);
			Out += ",\"parameterFlags\":";
			AppendU8Array(Out, Method.ParamFlags);
			Out += ",\"returnType\":";
			AppendQuoted(Out, Method.ReturnTypeName);
			Out += ",\"returnsVoid\":";
			Out += Method.bReturnsVoid ? "true" : "false";
			Out += ",\"returnsRef\":";
			Out += Method.bReturnsRef ? "true" : "false";
			Out += ",\"returnsConstRef\":";
			Out += Method.bReturnsConstRef ? "true" : "false";
			Out += ",\"virtual\":";
			Out += Method.bIsVirtual ? "true" : "false";
			Out += ",\"abstract\":";
			Out += Method.bIsAbstract ? "true" : "false";
			Out += ",\"const\":";
			Out += Method.bIsConst ? "true" : "false";
			Out += ",\"constructor\":";
			Out += Method.bIsConstructor ? "true" : "false";
			Out += ",\"destructor\":";
			Out += Method.bIsDestructor ? "true" : "false";
			Out += ",\"deleted\":";
			Out += Method.bDeleted ? "true" : "false";
			Out += ",\"constexpr\":";
			Out += Method.bConstexpr ? "true" : "false";
			Out += ",\"functionIndex\":" + std::to_string(Method.FunctionIndex);
			Out += ",\"declaringTypeIndex\":" + std::to_string(Method.DeclaringTypeIndex);
			Out += ",\"declaringTypeName\":";
			AppendQuoted(Out, Method.DeclaringTypeName);
			Out += ",\"metadata\":";
			AppendDeclMeta(Out, Method.DeclMeta);
			Out += '}';
		}

		void AppendGlobal(std::string& Out, const FLoadedGlobalInfo& Global)
		{
			Out += "{\"name\":";
			AppendQuoted(Out, Global.Name);
			Out += ",\"linkageName\":";
			AppendQuoted(Out, Global.LinkageName);
			Out += ",\"modulePath\":";
			AppendQuoted(Out, Global.ModulePath);
			Out += ",\"declaredType\":";
			AppendQuoted(Out, Global.DeclaredTypeName);
			Out += ",\"runtimeType\":";
			AppendQuoted(Out, Global.RuntimeTypeName);
			Out += ",\"globalSlot\":" + std::to_string(Global.GlobalSlot);
			Out += ",\"const\":";
			Out += Global.bConst ? "true" : "false";
			Out += ",\"constexpr\":";
			Out += Global.bConstexpr ? "true" : "false";
			Out += ",\"exported\":";
			Out += Global.bExported ? "true" : "false";
			Out += ",\"staticMember\":";
			Out += Global.bStaticMember ? "true" : "false";
			Out += ",\"metadata\":";
			AppendDeclMeta(Out, Global.DeclMeta);
			Out += '}';
		}

		void AppendField(std::string& Out, const FLoadedFieldInfo& Field)
		{
			Out += "{\"name\":";
			AppendQuoted(Out, Field.Name);
			Out += ",\"declaredType\":";
			AppendQuoted(Out, Field.TypeName);
			Out += ",\"runtimeType\":";
			AppendQuoted(Out, Field.RuntimeTypeName);
			Out += ",\"fieldIndex\":" + std::to_string(Field.FieldIndex);
			Out += ",\"declaredOnType\":";
			Out += Field.bDeclaredOnType ? "true" : "false";
			Out += ",\"static\":";
			Out += Field.bIsStatic ? "true" : "false";
			Out += ",\"mutable\":";
			Out += Field.bIsMutable ? "true" : "false";
			Out += ",\"constexpr\":";
			Out += Field.bConstexpr ? "true" : "false";
			Out += ",\"metadata\":";
			AppendDeclMeta(Out, Field.DeclMeta);
			Out += '}';
		}

		bool BuildReflectionJson(VeroScriptSession& Session, std::string& Out)
		{
			std::vector<FLoadedTypeInfo> Types;
			std::vector<FFreeFunctionInfo> Functions;
			std::vector<FLoadedGlobalInfo> Globals;
			std::vector<FLoadedInterfaceInfo> Interfaces;
			std::vector<FLoadedEnumInfo> Enums;
			if (!Session.EnumerateTypes(Types)
				|| !Session.EnumerateFreeFunctions(Functions)
				|| !Session.EnumerateGlobals(Globals)
				|| !Session.EnumerateInterfaces(Interfaces)
				|| !Session.EnumerateEnums(Enums))
			{
				return false;
			}

			Out = "{\"version\":1,\"types\":[";
			for (std::size_t TypeIndex = 0; TypeIndex < Types.size(); ++TypeIndex)
			{
				if (TypeIndex)
				{
					Out += ',';
				}
				const auto& Type = Types[TypeIndex];
				std::vector<FLoadedFieldInfo> Fields;
				std::vector<FInstanceMethodInfo> Methods;
				std::vector<FStaticMethodInfo> StaticMethods;
				if (!Session.EnumerateFields(Type.TypeName, Fields)
					|| !Session.EnumerateInstanceMethods(Type.TypeName, Methods, true, true, true)
					|| !Session.EnumerateStaticMethods(Type.TypeName, StaticMethods))
				{
					return false;
				}
				Out += "{\"typeIndex\":" + std::to_string(Type.TypeIndex) + ",\"name\":";
				AppendQuoted(Out, Type.TypeName);
				Out += ",\"simpleName\":";
				AppendQuoted(Out, Type.SimpleName);
				Out += ",\"kind\":";
				AppendQuoted(Out, Type.Kind == EScriptObjectKind::Class ? "class" : "struct");
				Out += ",\"hostBaseTypeName\":";
				AppendQuoted(Out, Type.HostBaseTypeName);
				Out += ",\"hostBaseTypeId\":" + std::to_string(Type.HostBaseTypeId);
				Out += ",\"abstract\":";
				Out += Type.bIsAbstract ? "true" : "false";
				Out += ",\"static\":";
				Out += Type.bIsStatic ? "true" : "false";
				Out += ",\"pureVirtualDestructor\":";
				Out += Type.bDestructorPureVirtual ? "true" : "false";
				Out += ",\"baseTypeIndices\":";
				AppendU16Array(Out, Type.BaseTypeIndices);
				Out += ",\"ownFieldStartIndex\":" + std::to_string(Type.OwnFieldStartIndex);
				Out += ",\"metadata\":";
				AppendDeclMeta(Out, Type.DeclMeta);
				Out += ",\"fields\":[";
				for (std::size_t Index = 0; Index < Fields.size(); ++Index)
				{
					if (Index)
					{
						Out += ',';
					}
					AppendField(Out, Fields[Index]);
				}
				Out += "],\"methods\":[";
				for (std::size_t Index = 0; Index < Methods.size(); ++Index)
				{
					if (Index)
					{
						Out += ',';
					}
					AppendInstanceMethod(Out, Methods[Index]);
				}
				Out += "],\"staticMethods\":[";
				for (std::size_t Index = 0; Index < StaticMethods.size(); ++Index)
				{
					if (Index)
					{
						Out += ',';
					}
					AppendMethod(Out, StaticMethods[Index]);
				}
				Out += "]}";
			}
			Out += "],\"freeFunctions\":[";
			for (std::size_t Index = 0; Index < Functions.size(); ++Index)
			{
				if (Index)
				{
					Out += ',';
				}
				AppendFreeFunction(Out, Functions[Index]);
			}
			Out += "],\"globals\":[";
			for (std::size_t Index = 0; Index < Globals.size(); ++Index)
			{
				if (Index)
				{
					Out += ',';
				}
				AppendGlobal(Out, Globals[Index]);
			}
			Out += "],\"interfaces\":[";
			for (std::size_t Index = 0; Index < Interfaces.size(); ++Index)
			{
				if (Index)
				{
					Out += ',';
				}
				const auto& Interface = Interfaces[Index];
				std::vector<FInstanceMethodInfo> Methods;
				std::vector<FStaticMethodInfo> StaticMethods;
				if (!Session.EnumerateInstanceMethods(
						Interface.Name, Methods, true, true, true)
					|| !Session.EnumerateStaticMethods(Interface.Name, StaticMethods))
				{
					return false;
				}
				Out += "{\"interfaceIndex\":" + std::to_string(Interface.InterfaceIndex)
					+ ",\"name\":";
				AppendQuoted(Out, Interface.Name);
				Out += ",\"simpleName\":";
				AppendQuoted(Out, Interface.SimpleName);
				Out += ",\"baseInterfaceIndices\":";
				AppendU16Array(Out, Interface.BaseInterfaceIndices);
				Out += ",\"metadata\":";
				AppendDeclMeta(Out, Interface.DeclMeta);
				Out += ",\"fields\":[";
				for (std::size_t FieldIndex = 0; FieldIndex < Interface.Fields.size(); ++FieldIndex)
				{
					if (FieldIndex)
					{
						Out += ',';
					}
					AppendField(Out, Interface.Fields[FieldIndex]);
				}
				Out += "],\"methods\":[";
				for (std::size_t MethodIndex = 0; MethodIndex < Methods.size(); ++MethodIndex)
				{
					if (MethodIndex)
					{
						Out += ',';
					}
					AppendInstanceMethod(Out, Methods[MethodIndex]);
				}
				Out += "],\"staticMethods\":[";
				for (std::size_t MethodIndex = 0; MethodIndex < StaticMethods.size(); ++MethodIndex)
				{
					if (MethodIndex)
					{
						Out += ',';
					}
					AppendMethod(Out, StaticMethods[MethodIndex]);
				}
				Out += "]}";
			}
			Out += "],\"enums\":[";
			for (std::size_t Index = 0; Index < Enums.size(); ++Index)
			{
				if (Index)
				{
					Out += ',';
				}
				const auto& Enum = Enums[Index];
				Out += "{\"name\":";
				AppendQuoted(Out, Enum.Name);
				Out += ",\"simpleName\":";
				AppendQuoted(Out, Enum.SimpleName);
				Out += ",\"scoped\":";
				Out += Enum.bScoped ? "true" : "false";
				Out += ",\"underlyingType\":";
				AppendQuoted(Out, Enum.UnderlyingTypeName);
				Out += ",\"metadata\":";
				AppendDeclMeta(Out, Enum.DeclMeta);
				Out += ",\"members\":[";
				for (std::size_t MemberIndex = 0; MemberIndex < Enum.Members.size(); ++MemberIndex)
				{
					if (MemberIndex)
					{
						Out += ',';
					}
					const auto& Member = Enum.Members[MemberIndex];
					Out += "{\"name\":";
					AppendQuoted(Out, Member.Name);
					Out += ",\"value\":";
					AppendQuoted(Out, std::to_string(Member.Value));
					Out += ",\"metadata\":";
					AppendDeclMeta(Out, Member.DeclMeta);
					Out += '}';
				}
				Out += "]}";
			}
			Out += "]}";
			return true;
		}
	}
}

extern "C"
{
	vs_interop_status VS_INTEROP_CALL vs_interop_session_export_reflection_json(
		vs_interop_session* session,
		vs_interop_buffer** out_json)
	{
		if (!session || !session->State || !out_json)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_json = nullptr;
		const auto StateHandle = session->State;
		auto& State = *StateHandle;
		if (const auto Status = VeroScript::Interop::RequireOwnerThread(State);
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		if (const auto Status = VeroScript::Interop::RequireIdleSession(State, "ExportReflectionJson");
			Status != VS_INTEROP_STATUS_OK)
		{
			return Status;
		}
		try
		{
			if (!State.Session.IsLoaded())
			{
				VeroScript::Interop::SetError(State, "no VeroScript module is loaded");
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			std::string Json;
			const VeroScript::Interop::FScopedSessionActivity Activity(State);
			if (!VeroScript::Interop::BuildReflectionJson(State.Session, Json))
			{
				VeroScript::Interop::SetError(State, "unable to enumerate loaded module metadata");
				return VS_INTEROP_STATUS_ENGINE_ERROR;
			}
			return VeroScript::Interop::MakeTextBuffer(std::move(Json), out_json);
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(&State);
		}
	}
}
