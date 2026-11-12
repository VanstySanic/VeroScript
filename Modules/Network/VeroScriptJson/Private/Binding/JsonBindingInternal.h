// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptJson.h"
#include "VeroScriptJsonSchema.h"
#include "VeroScriptJsonModule.h"
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace VeroScript::Json::Binding
{
	using FBindingCall = std::function<bool(const FNativeCallContext&, VeroScript::FValue&, std::string&)>;
	struct FState
	{
		INativeExtensionHost* Host = nullptr;
		FHostTypeId Functions;
		std::vector<std::unique_ptr<FBindingCall>> Calls;
	};
	inline void Diagnostic(const FNativeCallContext& C, const char* Id, const std::string& Message,
		const std::string& Pointer = {}) noexcept
	{
		if (!C.DiagnosticOutput || !C.DiagnosticOutput->MessageId.empty())
		{
			return;
		}
		try
		{
			FScriptError Value;
			Value.MessageId = Id;
			Value.Message = Message;
			Value.MessageArgs = {Message, Pointer};
			*C.DiagnosticOutput = std::move(Value);
		}
		catch (...)
		{
		} // A display diagnostic must not replace the original failure.
	}
	template <FNativeCallback Callback>
	bool DiagnosticCall(const FNativeCallContext& C, VeroScript::FValue& Out, std::string& Error)
	{
		const char* Id = "json.binding";
		try
		{
			if (Callback(C, Out, Error))
			{
				return true;
			}
		}
		catch (const std::bad_alloc&)
		{
			Id = "json.allocation";
			Error = "JSON binding allocation failed";
		}
		catch (const std::length_error& Exception)
		{
			Id = "json.limit";
			Error = Exception.what();
		}
		catch (const std::exception& Exception)
		{
			Error = Exception.what();
		}
		catch (...)
		{
			Error = "JSON native callback failed";
		}
		Diagnostic(C, Id, Error);
		return false;
	}

	template <class T>
	const char* TypeName();
	template <>
	inline const char* TypeName<FJsonValue>()
	{
		return "json::value";
	}
	template <>
	inline const char* TypeName<FError>()
	{
		return "json::error";
	}
	template <>
	inline const char* TypeName<FDocument>()
	{
		return "json::document";
	}
	template <>
	inline const char* TypeName<FBuilder>()
	{
		return "json::builder";
	}
	template <>
	inline const char* TypeName<FEvent>()
	{
		return "json::event";
	}

	template <>
	inline const char* TypeName<FPointer>()
	{
		return "json::pointer";
	}
	template <>
	inline const char* TypeName<FLimits>()
	{
		return "json::limits";
	}
	template <>
	inline const char* TypeName<FParseOptions>()
	{
		return "json::parse_options";
	}
	template <>
	inline const char* TypeName<FWriteOptions>()
	{
		return "json::write_options";
	}
	template <>
	inline const char* TypeName<FSchema>()
	{
		return "json::schema";
	}
	template <>
	inline const char* TypeName<FSchemaReport>()
	{
		return "json::schema_report";
	}
	template <>
	inline const char* TypeName<FSchemaOptions>()
	{
		return "json::schema_options";
	}
	template <>
	inline const char* TypeName<FSchemaReportOptions>()
	{
		return "json::schema_report_options";
	}

	template <class T>
	bool Wrap(FState& S, T Value, VeroScript::FValue& Out, std::string& Error)
	{
		auto Payload = std::make_unique<T>(std::move(Value));
		auto Object = S.Host->MakeHostObjectNamed(
			TypeName<T>(),
			Payload.get(),
			[](void* P)
			{
				delete static_cast<T*>(P);
			},
			Error);
		if (!Object)
		{
			return false;
		}
		Payload.release();
		Out = std::move(Object);
		return true;
	}

	template <class T>
	T& Object(const FNativeCallContext& C, std::size_t Index)
	{
		if (!C.Args || Index >= C.ArgCount)
		{
			throw std::invalid_argument("Missing JSON object argument");
		}
		const auto* Handle = std::get_if<FHostObjectHandle>(&C.Args[Index]);
		if (!Handle || !*Handle || !(*Handle)->Payload)
		{
			throw std::invalid_argument("JSON object argument is null");
		}
		return *static_cast<T*>((*Handle)->Payload);
	}

	template <class T, bool bClone>
	bool Construct(const FNativeCallContext& C, VeroScript::FValue& Out, std::string& Error)
	{
		auto& S = *static_cast<FState*>(C.UserData);
		return Wrap(S, bClone ? Object<T>(C, 0) : T{}, Out, Error);
	}

	template <FNativeCallback Callback>
	bool Function(FState& S, const char* Name, ENativeType Return, std::vector<ENativeType> Parameters,
		std::string& Error, std::vector<std::string> ParameterNames = {}, std::vector<std::uint8_t> Flags = {},
		const char* ReturnName = "json::value")
	{
		FHostMemberDesc D;
		D.Kind = EHostMemberKind::StaticMethod;
		D.Name = std::string(Name).substr(6);
		D.ReturnType = Return;
		D.ReturnHostTypeName = Return == ENativeType::HostObject ? ReturnName : "";
		D.ParamTypes = std::move(Parameters);
		D.ParamHostTypeNames = std::move(ParameterNames);
		D.ParamFlags = std::move(Flags);
		D.Callback = DiagnosticCall<Callback>;
		D.UserData = &S;
		return S.Host->RegisterMember(S.Functions, std::move(D), Error);
	}

	template <FNativeCallback Callback>
	bool Method(FState& S, FHostTypeId Id, const char* Name, ENativeType Return, std::vector<ENativeType> Parameters,
		std::string& Error, bool bMutable = false, const char* ReturnName = "json::value",
		std::vector<std::string> ParameterNames = {}, std::vector<std::uint8_t> Flags = {})
	{
		FHostMemberDesc D;
		D.Name = Name;
		D.ReturnType = Return;
		D.ParamTypes = std::move(Parameters);
		D.ReturnHostTypeName = Return == ENativeType::HostObject ? ReturnName : "";
		for (auto P : D.ParamTypes)
		{
			D.ParamHostTypeNames.push_back(P == ENativeType::HostObject ? "json::value" : "");
		}
		if (!ParameterNames.empty())
		{
			D.ParamHostTypeNames = std::move(ParameterNames);
		}
		D.ParamFlags = std::move(Flags);
		D.bConstMethod = !bMutable;
		D.bMutableThis = bMutable;
		D.Callback = DiagnosticCall<Callback>;
		D.UserData = &S;
		return S.Host->RegisterMember(Id, std::move(D), Error);
	}

	template <class T>
	bool HandleType(FState& S, FHostTypeId& Id, std::string& Error)
	{
		FHostTypeDesc D;
		D.Name = TypeName<T>();
		D.Kind = EHostTypeKind::Class;
		return S.Host->RegisterType(std::move(D), Id, Error);
	}

	template <class T>
	bool Type(FState& S, FHostTypeId& Id, std::string& Error);

	template <class T>
	bool Type(FState& S, FHostTypeId& Id, std::string& Error)
	{
		FHostTypeDesc D;
		D.Name = TypeName<T>();
		D.Kind = EHostTypeKind::Struct;
		D.CloneCallback = Construct<T, true>;
		D.CloneUserData = &S;
		if (!S.Host->RegisterType(std::move(D), Id, Error))
		{
			return false;
		}
		FHostMemberDesc C;
		C.Kind = EHostMemberKind::Constructor;
		C.Name = std::string(TypeName<T>()).substr(6);
		C.ReturnType = ENativeType::HostObject;
		C.Callback = Construct<T, false>;
		C.UserData = &S;
		return S.Host->RegisterMember(Id, std::move(C), Error);
	}

	bool RegisterReferences(FState&, FHostTypeId, FHostTypeId, std::string&);
	bool RegisterBuilderTry(FState&, FHostTypeId, std::string&);
	bool RegisterStreams(FState& S, std::string& Error);
	bool RegisterOptions(FState& S, std::string& Error);
	bool RegisterSchema(FState& S, std::string& Error);
	bool RegisterMappingContext(FState& S, std::string& Error);
	bool RegisterMappingProcessor(FState& S, std::string& Error);
	bool RegisterValueQueries(FState& S, FHostTypeId Id, std::string& Error);
} // namespace VeroScript::Json::Binding
