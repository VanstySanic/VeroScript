// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGraphicsBindingState.h"

namespace VeroScript::Graphics::BindingDetail
{
	bool RegisterAsyncBindings(FHostBindingRegistry& Registry, const std::shared_ptr<FState>& State, std::string& Error)
	{
		FHostTypeId StatusType;
		if (!Registry.RegisterType("VS.Graphics.RequestStatus", EHostTypeKind::Enum, StatusType, Error) ||
			!Registry.SetEnumScoped(StatusType, true, Error))
		{
			return false;
		}
		const char* StatusNames[]{"Pending", "Succeeded", "Failed", "Cancelled"};
		for (unsigned I = 0; I < 4; ++I)
		{
			FHostMemberDesc M;
			M.Name = StatusNames[I];
			M.Kind = EHostMemberKind::EnumMember;
			M.EnumValue = I;
			if (!Registry.RegisterMember(StatusType, std::move(M), Error))
			{
				return false;
			}
		}
		FRegistrar Add(Registry, State, Error);
		const FRegistrar AddConst(Registry, State, Error, true);
		const auto Method = EHostMemberKind::Method;
		for (bool Recovery : {false, true})
		{
			const auto Params = Recovery ? std::vector<ENativeType>{ENativeType::HostObject, ENativeType::String}
										 : std::vector<ENativeType>{ENativeType::String};
			if (!Add(
					0, Recovery ? "RecoverDeviceAsync" : "CreateDeviceAsync", Method, ENativeType::HostObject, Params,
					[State, Recovery](const FNativeCallContext& C, FValue& Out, std::string& E)
					{
						FDeviceOptions O;
						O.Driver = std::get<std::string>(C.Args[Recovery ? 1 : 0]);
						const auto& Service = This<std::shared_ptr<IService>>(C);
						std::shared_ptr<IDeviceRequest> Request;
						if (Recovery)
						{
							const auto* Previous = Get<std::shared_ptr<IDevice>>(C.Args[0], E);
							if (!Previous)
							{
								return false;
							}
							Request = Service->RecoverDeviceAsync(*Previous, O, E);
						}
						else
						{
							Request = Service->CreateDeviceAsync(O, E);
						}
						if (!Request)
						{
							return false;
						}
						Out = Make(State, 10, std::move(Request));
						return true;
					},
					Names[10], Recovery ? std::vector<std::string>{Names[1], ""} : std::vector<std::string>{}))
			{
				return false;
			}
		}
		for (unsigned Type : {10u, 11u})
		{
			for (bool bError : {false, true})
			{
				if (!Add(
						Type, bError ? "GetError" : "Poll", Method, bError ? ENativeType::String : ENativeType::Enum,
						{},
						[Type, bError](const FNativeCallContext& C, FValue& Out, std::string&)
						{
							std::string Detail;
							const auto Status = Type == 10 ? This<std::shared_ptr<IDeviceRequest>>(C)->Poll(Detail)
														   : This<std::shared_ptr<IReadbackRequest>>(C)->Poll(Detail);
							if (bError)
							{
								Out = std::move(Detail);
							}
							else
							{
								Out = FEnumValue{"VS.Graphics.RequestStatus", static_cast<std::int64_t>(Status)};
							}
							return true;
						},
						bError ? "" : "VS.Graphics.RequestStatus"))
				{
					return false;
				}
			}
			if (!Add(Type, "Cancel", Method, ENativeType::Void, {},
					 [Type](const FNativeCallContext& C, FValue&, std::string& E)
					 {
						 return Type == 10 ? This<std::shared_ptr<IDeviceRequest>>(C)->Cancel(E)
										   : This<std::shared_ptr<IReadbackRequest>>(C)->Cancel(E);
					 }))
			{
				return false;
			}
		}
		if (!AddConst(
				10, "GetDevice", Method, ENativeType::HostObject, {},
				[State](const FNativeCallContext& C, FValue& Out, std::string& E)
				{
					auto D = This<std::shared_ptr<IDeviceRequest>>(C)->GetDevice(E);
					if (!D)
					{
						return false;
					}
					Out = Make(State, 1, std::move(D));
					return true;
				},
				Names[1]))
		{
			return false;
		}
		if (!Add(1, "ReadTextureAsync", Method, ENativeType::HostObject, {ENativeType::HostObject},
				 [State](const FNativeCallContext& C, FValue& Out, std::string& E)
				 {
					 const auto* Texture = Get<std::shared_ptr<ITexture>>(C.Args[0], E);
					 if (!Texture)
					 {
						 return false;
					 }
					 auto R = This<std::shared_ptr<IDevice>>(C)->ReadTextureAsync(*Texture, E);
					 if (!R)
					 {
						 return false;
					 }
					 Out = Make(State, 11, std::move(R));
					 return true;
				 },
				 Names[11], {Names[2]}))
		{
			return false;
		}
		if (!AddConst(
				11, "GetPixels", Method, ENativeType::Array, {},
				[](const FNativeCallContext& C, FValue& Out, std::string& E)
				{
					std::vector<std::uint8_t> Pixels;
					if (!This<std::shared_ptr<IReadbackRequest>>(C)->GetPixels(Pixels, E))
					{
						return false;
					}
					auto Result = std::make_shared<FDynamicArray>();
					Result->ElementTypeName = "uint8";
					Result->TypeName = "array<uint8>";
					Result->ScalarElements = std::move(Pixels);
					Out = std::move(Result);
					return true;
				},
				"", {}, {}, "uint8"))
		{
			return false;
		}
		return Add(1, "Poll", Method, ENativeType::Void, {},
				   [](const FNativeCallContext& C, FValue&, std::string& E)
				   {
					   return This<std::shared_ptr<IDevice>>(C)->Poll(E);
				   });
	}
}
