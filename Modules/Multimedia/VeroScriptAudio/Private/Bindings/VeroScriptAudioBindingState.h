// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAudioBindings.h"
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>

namespace VeroScript::Audio::BindingDetail
{
	inline constexpr std::array<const char*, 7> Names{"VS.Audio.Service",	 "VS.Audio.Stream", "VS.Audio.Buffer",
													  "VS.Audio.Devices",	 "VS.Audio.Device", "VS.Audio.Event",
													  "VS.Audio.StreamState"};
	struct FState
	{
		std::array<FHostTypeId, 7> Ids{};
		FServiceFactory Factory;
	};
	template <class T> struct FDelete
	{
		void operator()(FHostObject* Object) const noexcept
		{
			delete static_cast<T*>(Object->Payload);
			delete Object;
		}
	};

	template <class T> FValue Make(const std::shared_ptr<FState>& State, unsigned Type, T Value)
	{
		auto Payload = std::make_unique<T>(std::move(Value));
		auto Object = std::make_unique<FHostObject>();
		Object->TypeId = State->Ids[Type].Value;
		Object->TypeName = Names[Type];
		Object->ModuleOwner = State;
		Object->Payload = Payload.release();
		return FHostObjectHandle(Object.release(), FDelete<T>{});
	}

	template <class T> const T& This(const FNativeCallContext& Context)
	{
		return *static_cast<const T*>(Context.HostThis);
	}

	template <class T> const T* Get(const FValue& Value, std::string& Error)
	{
		const auto* Object = std::get_if<FHostObjectHandle>(&Value);
		if (!Object || !*Object || !std::get_deleter<FDelete<T>>(*Object))
		{
			Error = "audio.invalid_resource: expected an Audio binding resource";
			return nullptr;
		}
		return static_cast<const T*>((*Object)->Payload);
	}

	template <class T> T Element(const FDynamicArray& Array, std::size_t Index)
	{
		if (const auto* Values = std::get_if<std::vector<T>>(&Array.ScalarElements))
		{
			return (*Values)[Index];
		}
		if (Index >= Array.Elements.size())
		{
			throw std::invalid_argument("array element type does not match the binding");
		}
		return std::get<T>(Array.Elements[Index]);
	}
	inline const FDynamicArray& Array(const FValue& Value)
	{
		const auto& Handle = std::get<FDynamicArrayHandle>(Value);
		if (!Handle)
		{
			throw std::invalid_argument("null array");
		}
		return *Handle;
	}
	using FCall = std::function<bool(const FNativeCallContext&, FValue&, std::string&)>;
	inline bool Invoke(const FNativeCallContext& Context, FValue& Result, std::string& Error)
	{
		try
		{
			FNativeCallContext Explicit = Context;
			// Common handlers consume explicit parameters, excluding Core's receiver slot.
			if (Context.HostThis)
			{
				++Explicit.Args;
				if (Explicit.MutableArgs)
				{
					++Explicit.MutableArgs;
				}
				--Explicit.ArgCount;
			}
			return (*static_cast<const FCall*>(Context.UserData))(Explicit, Result, Error);
		}
		catch (const std::exception& Exception)
		{
			Error = std::string("audio.native_exception: ") + Exception.what();
			return false;
		}
		catch (...)
		{
			Error = "audio.native_exception: unknown exception";
			return false;
		}
	}

	class FRegistrar
	{
	public:
		FRegistrar(FHostBindingRegistry& InRegistry, std::shared_ptr<FState> InState, std::string& InError,
				   bool bInConstMethod = false)
			: Registry(InRegistry), State(std::move(InState)), Error(InError), bConstMethod(bInConstMethod)
		{
		}
		bool operator()(unsigned Type, const char* Name, EHostMemberKind Kind, ENativeType ReturnType,
						std::vector<ENativeType> Params, FCall Call, const char* HostReturn = "",
						std::vector<std::string> HostParams = {}, std::vector<std::string> Elements = {},
						const char* ReturnElement = "") const
		{
			auto Owner = std::make_shared<FCall>(std::move(Call));
			FHostMemberDesc Member;
			Member.Name = Name;
			Member.Kind = Kind;
			Member.bConstMethod = bConstMethod;
			Member.ReturnType = ReturnType;
			Member.ParamTypes = std::move(Params);
			Member.ReturnHostTypeName = HostReturn;
			Member.ReturnElementTypeName = ReturnElement;
			if (!HostParams.empty() && HostParams.size() < Member.ParamTypes.size())
			{
				HostParams.resize(Member.ParamTypes.size());
			}
			if (!Elements.empty() && Elements.size() < Member.ParamTypes.size())
			{
				Elements.resize(Member.ParamTypes.size());
			}
			Member.ParamHostTypeNames = std::move(HostParams);
			Member.ParamElementTypeNames = std::move(Elements);
			Member.Callback = Invoke;
			Member.UserData = Owner.get();
			Member.CallbackOwner = Owner;
			return Registry.RegisterMember(State->Ids[Type], std::move(Member), Error);
		}

	private:
		FHostBindingRegistry& Registry;
		std::shared_ptr<FState> State;
		std::string& Error;
		bool bConstMethod;
	};
}
