// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptPlatformBindings.h"
#include <array>

namespace VeroScript::Platform::BindingDetail
{
	inline constexpr std::array<const char*, 8> Names{
		"VS.Platform.Service",	   "VS.Platform.Window",	 "VS.Platform.Event",	   "VS.Platform.InputDevices",
		"VS.Platform.InputDevice", "VS.Platform.InputState", "VS.Platform.PressedKey", "VS.Platform.PointerState"};
	struct FState
	{
		std::array<FHostTypeId, 8> Ids{};
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

	using FCall = std::function<bool(const FNativeCallContext&, FValue&, std::string&)>;

	bool Invoke(const FNativeCallContext& Context, FValue& Result, std::string& Error);
	bool RegisterWindowBindings(FHostBindingRegistry& Registry, const std::shared_ptr<FState>& State,
								std::string& Error);
	bool RegisterControllerBindings(FHostBindingRegistry& Registry, const std::shared_ptr<FState>& State,
									std::string& Error);
	class FRegistrar
	{
	public:
		FRegistrar(FHostBindingRegistry& Registry, std::shared_ptr<FState> State, std::string& Error,
				   bool bInConstMethod = false);
		bool operator()(unsigned Type, const char* Name, EHostMemberKind Kind, ENativeType ReturnType,
						std::vector<ENativeType> Params, FCall Call, const char* HostReturn = "",
						std::vector<std::string> HostParams = {}) const;

	private:
		FHostBindingRegistry& Registry;
		std::shared_ptr<FState> State;
		std::string& Error;
		bool bConstMethod;
	};
}
