// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPlatformInputBindings.h"
#include <algorithm>
#include <type_traits>

namespace VeroScript::Platform::BindingDetail
{
	namespace
	{
		template <class T, class V>
		bool Property(const FRegistrar& Add, unsigned Type, const char* Name, ENativeType Native, V T::* Member,
					  const char* Enum = "")
		{
			return Add(
				Type, Name, EHostMemberKind::PropertyGetter, Native, {},
				[Member, Enum](const FNativeCallContext& C, FValue& Out, std::string&)
				{
					if constexpr (std::is_enum_v<V>)
					{
						Out = FEnumValue{Enum, static_cast<std::int64_t>(This<T>(C).*Member)};
					}
					else
					{
						Out = This<T>(C).*Member;
					}
					return true;
				},
				Enum);
		}
		template <class T>
		bool At(const std::shared_ptr<FState>& State, unsigned Type, const std::vector<T>& Items,
				const FNativeCallContext& C, FValue& Out, std::string& Error)
		{
			const auto Index = std::get<std::int64_t>(C.Args[0]);
			if (Index < 0 || static_cast<std::uint64_t>(Index) >= Items.size())
			{
				Error = "platform.invalid_index: input snapshot index is out of range";
				return false;
			}
			Out = Make(State, Type, Items[static_cast<std::size_t>(Index)]);
			return true;
		}
	}

	bool RegisterInputBindings(FHostBindingRegistry& Registry, const std::shared_ptr<FState>& State, std::string& Error)
	{
		constexpr const char* DeviceEnum = "VS.Platform.InputDeviceKind";
		FHostTypeId Kind;
		if (!Registry.RegisterType(DeviceEnum, EHostTypeKind::Enum, Kind, Error) ||
			!Registry.SetEnumScoped(Kind, true, Error))
		{
			return false;
		}
		const char* Kinds[]{"Unknown", "Keyboard", "Mouse", "Touch", "Joystick", "Gamepad"};
		for (unsigned I = 0; I < std::size(Kinds); ++I)
		{
			FHostMemberDesc Member;
			Member.Kind = EHostMemberKind::EnumMember;
			Member.Name = Kinds[I];
			Member.EnumValue = I;
			if (!Registry.RegisterMember(Kind, std::move(Member), Error))
			{
				return false;
			}
		}
		const FRegistrar Add(Registry, State, Error);
		const FRegistrar AddConst(Registry, State, Error, true);
		const auto Method = EHostMemberKind::Method, Getter = EHostMemberKind::PropertyGetter;
		if (!AddConst(
				0, "GetInputDevices", Method, ENativeType::HostObject, {},
				[State](const FNativeCallContext& C, FValue& Out, std::string& E)
				{
					std::vector<FInputDevice> Devices;
					if (!This<std::shared_ptr<IService>>(C)->GetInputDevices(Devices, E))
					{
						return false;
					}
					Out = Make(State, 3, std::move(Devices));
					return true;
				},
				Names[3]) ||
			!AddConst(0, "GetInputState", Method, ENativeType::HostObject, {ENativeType::Enum, ENativeType::UInt64},
				 [State](const FNativeCallContext& C, FValue& Out, std::string& E)
				 {
					 FInputState Snapshot;
					 const auto Value = std::get<FEnumValue>(C.Args[0]).Value;
					 if (Value < 1 || Value > static_cast<std::int64_t>(EInputDeviceKind::Gamepad))
					 {
						 E = "platform.unknown_input_device: invalid device kind";
						 return false;
					 }
					 if (!This<std::shared_ptr<IService>>(C)->GetInputState(
							 static_cast<EInputDeviceKind>(Value), std::get<std::uint64_t>(C.Args[1]), Snapshot, E))
					 {
						 return false;
					 }
					 Out = Make(State, 5, std::move(Snapshot));
					 return true;
				 },
				 Names[5], {DeviceEnum, ""}) ||
			!Add(0, "ClearInputState", Method, ENativeType::Void, {ENativeType::UInt64},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 return This<std::shared_ptr<IService>>(C)->ClearInputState(std::get<std::uint64_t>(C.Args[0]), E);
				 }))
		{
			return false;
		}
		const char* Capabilities[]{"SupportsInputDevices", "SupportsInputState", "SupportsDeviceEvents"};
		for (unsigned I = 0; I < std::size(Capabilities); ++I)
		{
			if (!AddConst(0, Capabilities[I], Method, ENativeType::Bool, {},
					 [I](const FNativeCallContext& C, FValue& Out, std::string&)
					 {
						 const auto Caps = This<std::shared_ptr<IService>>(C)->GetCapabilities();
						 Out = I == 0 ? Caps.bInputDevices : I == 1 ? Caps.bInputState : Caps.bDeviceEvents;
						 return true;
					 }))
			{
				return false;
			}
		}
		if (!Property(Add, 2, "DeviceKind", ENativeType::Enum, &FEvent::DeviceKind, DeviceEnum) ||
			!Property(Add, 2, "DeviceName", ENativeType::String, &FEvent::DeviceName) ||
			!Property(Add, 2, "AggregateDevice", ENativeType::Bool, &FEvent::bAggregateDevice) ||
			!Property(Add, 2, "Synthetic", ENativeType::Bool, &FEvent::bSynthetic) ||
			!Property(Add, 4, "Kind", ENativeType::Enum, &FInputDevice::Kind, DeviceEnum) ||
			!Property(Add, 4, "DeviceId", ENativeType::UInt64, &FInputDevice::DeviceId) ||
			!Property(Add, 4, "Name", ENativeType::String, &FInputDevice::Name) ||
			!Property(Add, 4, "Aggregate", ENativeType::Bool, &FInputDevice::bAggregate) ||
			!Property(Add, 4, "Connected", ENativeType::Bool, &FInputDevice::bConnected) ||
			!Property(Add, 5, "Modifiers", ENativeType::Unsigned, &FInputState::Modifiers) ||
			!Property(Add, 6, "Key", ENativeType::Enum, &FPressedKey::Key, "VS.Platform.Key") ||
			!Property(Add, 6, "ScanCode", ENativeType::Int, &FPressedKey::ScanCode) ||
			!Property(Add, 6, "WindowId", ENativeType::UInt64, &FPressedKey::WindowId) ||
			!Property(Add, 7, "PointerId", ENativeType::UInt64, &FPointerState::PointerId) ||
			!Property(Add, 7, "WindowId", ENativeType::UInt64, &FPointerState::WindowId) ||
			!Property(Add, 7, "X", ENativeType::Double, &FPointerState::X) ||
			!Property(Add, 7, "Y", ENativeType::Double, &FPointerState::Y) ||
			!Property(Add, 7, "Pressure", ENativeType::Double, &FPointerState::Pressure) ||
			!Property(Add, 7, "Buttons", ENativeType::Unsigned, &FPointerState::Buttons))
		{
			return false;
		}
		if (!Add(3, "Count", Getter, ENativeType::Int64, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string&)
				 {
					 Out = static_cast<std::int64_t>(This<std::vector<FInputDevice>>(C).size());
					 return true;
				 }) ||
			!AddConst(
				3, "Get", Method, ENativeType::HostObject, {ENativeType::Int64},
				[State](const FNativeCallContext& C, FValue& Out, std::string& E)
				{
					return At(State, 4, This<std::vector<FInputDevice>>(C), C, Out, E);
				},
				Names[4]) ||
			!Add(
				5, "Device", Getter, ENativeType::HostObject, {},
				[State](const FNativeCallContext& C, FValue& Out, std::string&)
				{
					Out = Make(State, 4, This<FInputState>(C).Device);
					return true;
				},
				Names[4]))
		{
			return false;
		}
		for (const bool bKeys : {true, false})
		{
			if (!Add(5, bKeys ? "KeyCount" : "PointerCount", Getter, ENativeType::Int64, {},
					 [bKeys](const FNativeCallContext& C, FValue& Out, std::string&)
					 {
						 const auto& Snapshot = This<FInputState>(C);
						 Out = static_cast<std::int64_t>(bKeys ? Snapshot.Keys.size() : Snapshot.Pointers.size());
						 return true;
					 }) ||
				!AddConst(
					5, bKeys ? "GetKey" : "GetPointer", Method, ENativeType::HostObject, {ENativeType::Int64},
					[State, bKeys](const FNativeCallContext& C, FValue& Out, std::string& E)
					{
						const auto& Snapshot = This<FInputState>(C);
						return bKeys ? At(State, 6, Snapshot.Keys, C, Out, E)
									 : At(State, 7, Snapshot.Pointers, C, Out, E);
					},
					Names[bKeys ? 6 : 7]))
			{
				return false;
			}
		}
		if (!AddConst(5, "IsKeyDown", Method, ENativeType::Bool, {ENativeType::Enum},
				 [](const FNativeCallContext& C, FValue& Out, std::string&)
				 {
					 const auto& Keys = This<FInputState>(C).Keys;
					 const auto Key = std::get<FEnumValue>(C.Args[0]).Value;
					 Out = std::any_of(Keys.begin(), Keys.end(),
									   [Key](const auto& K)
									   {
										   return static_cast<std::int64_t>(K.Key) == Key;
									   });
					 return true;
				 },
				 "", {"VS.Platform.Key"}) ||
			!AddConst(5, "IsScanCodeDown", Method, ENativeType::Bool, {ENativeType::Int},
				 [](const FNativeCallContext& C, FValue& Out, std::string&)
				 {
					 const auto& Keys = This<FInputState>(C).Keys;
					 const auto Scan = std::get<std::int32_t>(C.Args[0]);
					 Out = Scan > 0 && std::any_of(Keys.begin(), Keys.end(),
												   [Scan](const auto& K)
												   {
													   return K.ScanCode == Scan;
												   });
					 return true;
				 }))
		{
			return false;
		}
		return true;
	}
}
