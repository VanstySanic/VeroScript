// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPlatformBindingState.h"

namespace VeroScript::Platform::BindingDetail
{
	bool RegisterControllerBindings(FHostBindingRegistry& Registry, const std::shared_ptr<FState>& State,
									std::string& Error)
	{
		const auto Enum = [&](const char* Name, const std::vector<std::pair<const char*, std::int64_t>>& Values)
		{
			FHostTypeId Id;
			if (!Registry.RegisterType(Name, EHostTypeKind::Enum, Id, Error) ||
				!Registry.SetEnumScoped(Id, true, Error))
			{
				return false;
			}
			for (const auto& Value : Values)
			{
				FHostMemberDesc M;
				M.Kind = EHostMemberKind::EnumMember;
				M.Name = Value.first;
				M.EnumValue = Value.second;
				if (!Registry.RegisterMember(Id, std::move(M), Error))
				{
					return false;
				}
			}
			return true;
		};
		if (!Enum(
				"VS.Platform.GamepadAxis",
				{{"LeftX", 0}, {"LeftY", 1}, {"RightX", 2}, {"RightY", 3}, {"LeftTrigger", 4}, {"RightTrigger", 5}}) ||
			!Enum("VS.Platform.GamepadButton",
				  {{"South", 0},		 {"East", 1},		  {"West", 2},			 {"North", 3},
				   {"Back", 4},			 {"Guide", 5},		  {"Start", 6},			 {"LeftStick", 7},
				   {"RightStick", 8},	 {"LeftShoulder", 9}, {"RightShoulder", 10}, {"DpadUp", 11},
				   {"DpadDown", 12},	 {"DpadLeft", 13},	  {"DpadRight", 14},	 {"Misc1", 15},
				   {"RightPaddle1", 16}, {"LeftPaddle1", 17}, {"RightPaddle2", 18},	 {"LeftPaddle2", 19},
				   {"Touchpad", 20},	 {"Misc2", 21},		  {"Misc3", 22},		 {"Misc4", 23},
				   {"Misc5", 24},		 {"Misc6", 25}}) ||
			!Enum("VS.Platform.Hat", {{"Centered", 0},
									  {"Up", 1},
									  {"Right", 2},
									  {"Down", 4},
									  {"Left", 8},
									  {"RightUp", 3},
									  {"RightDown", 6},
									  {"LeftUp", 9},
									  {"LeftDown", 12}}))
		{
			return false;
		}
		const FRegistrar Add(Registry, State, Error);
		const FRegistrar AddConst(Registry, State, Error, true);
		const auto Method = EHostMemberKind::Method, Getter = EHostMemberKind::PropertyGetter;
		for (bool Rumble : {false, true})
		{
			if (!AddConst(0, Rumble ? "SupportsRumble" : "SupportsControllers", Method, ENativeType::Bool, {},
					 [Rumble](const FNativeCallContext& C, FValue& Out, std::string&)
					 {
						 const auto Caps = This<std::shared_ptr<IService>>(C)->GetCapabilities();
						 Out = Rumble ? Caps.bRumble : Caps.bControllers;
						 return true;
					 }))
			{
				return false;
			}
		}
		if (!Add(0, "Rumble", Method, ENativeType::Void,
				 {ENativeType::Enum, ENativeType::UInt64, ENativeType::Double, ENativeType::Double, ENativeType::Int64},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 const auto Kind = std::get<FEnumValue>(C.Args[0]).Value;
					 if (Kind != static_cast<std::int64_t>(EInputDeviceKind::Joystick) &&
						 Kind != static_cast<std::int64_t>(EInputDeviceKind::Gamepad))
					 {
						 E = "platform.invalid_controller: rumble requires a controller kind";
						 return false;
					 }
					 return This<std::shared_ptr<IService>>(C)->Rumble(
						 static_cast<EInputDeviceKind>(Kind), std::get<std::uint64_t>(C.Args[1]),
						 std::get<double>(C.Args[2]), std::get<double>(C.Args[3]), std::get<std::int64_t>(C.Args[4]),
						 E);
				 },
				 "", {"VS.Platform.InputDeviceKind", "", "", "", ""}))
		{
			return false;
		}
		for (unsigned Type : {2u, 4u, 5u})
		{
			for (unsigned Category = 0; Category < 3; ++Category)
			{
				const char* Name = Category == 0 ? "AxisCount" : Category == 1 ? "ButtonCount" : "HatCount";
				if (!Add(Type, Name, Getter, ENativeType::Int64, {},
						 [Type, Category](const FNativeCallContext& C, FValue& Out, std::string&)
						 {
							 if (Type == 2)
							 {
								 const auto& V = This<FEvent>(C);
								 Out = static_cast<std::int64_t>(Category == 0	 ? V.AxisCount
																 : Category == 1 ? V.ButtonCount
																				 : V.HatCount);
							 }
							 else
							 {
								 const auto& V = Type == 4 ? This<FInputDevice>(C) : This<FInputState>(C).Device;
								 Out = static_cast<std::int64_t>(Category == 0	 ? V.AxisCount
																 : Category == 1 ? V.ButtonCount
																				 : V.HatCount);
							 }
							 return true;
						 }))
				{
					return false;
				}
			}
		}
		for (unsigned Type : {2u, 4u})
		{
			if (!Add(Type, "SupportsRumble", Getter, ENativeType::Bool, {},
					 [Type](const FNativeCallContext& C, FValue& Out, std::string&)
					 {
						 Out = Type == 2 ? This<FEvent>(C).bRumble : This<FInputDevice>(C).bRumble;
						 return true;
					 }))
			{
				return false;
			}
		}
		if (!Add(2, "Control", Getter, ENativeType::Int, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string&)
				 {
					 Out = This<FEvent>(C).Control;
					 return true;
				 }) ||
			!Add(2, "Value", Getter, ENativeType::Double, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string&)
				 {
					 Out = This<FEvent>(C).Value;
					 return true;
				 }))
		{
			return false;
		}
		for (unsigned Category = 0; Category < 3; ++Category)
		{
			if (!AddConst(
					5,
					Category == 0	? "GetAxis"
					: Category == 1 ? "IsButtonDown"
									: "GetHat",
					Method,
					Category == 0	? ENativeType::Double
					: Category == 1 ? ENativeType::Bool
									: ENativeType::Enum,
					{ENativeType::Int64},
					[Category](const FNativeCallContext& C, FValue& Out, std::string& E)
					{
						const auto& V = This<FInputState>(C);
						const auto Index = std::get<std::int64_t>(C.Args[0]);
						const auto Size = Category == 0	  ? V.Axes.size()
										  : Category == 1 ? V.ControllerButtons.size()
														  : V.Hats.size();
						if (Index < 0 || static_cast<std::uint64_t>(Index) >= Size)
						{
							E = "platform.invalid_index: controller snapshot index is out of range";
							return false;
						}
						const auto I = static_cast<std::size_t>(Index);
						if (Category == 0)
						{
							Out = V.Axes[I];
						}
						else if (Category == 1)
						{
							Out = V.ControllerButtons[I] != 0;
						}
						else
						{
							Out = FEnumValue{"VS.Platform.Hat", V.Hats[I]};
						}
						return true;
					},
					Category == 2 ? "VS.Platform.Hat" : ""))
			{
				return false;
			}
		}
		// Distinct names make numeric indices and typed gamepad queries explicit.
		for (bool Button : {false, true})
		{
			if (!AddConst(5, Button ? "IsGamepadButtonDown" : "GetGamepadAxis", Method,
					 Button ? ENativeType::Bool : ENativeType::Double, {ENativeType::Enum},
					 [Button](const FNativeCallContext& C, FValue& Out, std::string& E)
					 {
						 const auto& V = This<FInputState>(C);
						 const auto Index = std::get<FEnumValue>(C.Args[0]).Value;
						 const auto Size = Button ? V.ControllerButtons.size() : V.Axes.size();
						 if (V.Device.Kind != EInputDeviceKind::Gamepad || Index < 0 ||
							 static_cast<std::uint64_t>(Index) >= Size)
						 {
							 E = "platform.invalid_index: gamepad control is unavailable";
							 return false;
						 }
						 if (Button)
						 {
							 Out = V.ControllerButtons[static_cast<std::size_t>(Index)] != 0;
						 }
						 else
						 {
							 Out = V.Axes[static_cast<std::size_t>(Index)];
						 }
						 return true;
					 },
					 "", {Button ? "VS.Platform.GamepadButton" : "VS.Platform.GamepadAxis"}))
			{
				return false;
			}
		}
		return true;
	}
}
