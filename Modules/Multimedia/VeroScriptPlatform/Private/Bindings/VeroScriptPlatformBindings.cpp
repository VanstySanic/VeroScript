// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPlatformBindings.h"
#include "VeroScriptPlatformBindingState.h"
#include "VeroScriptPlatformInputBindings.h"
#include <array>
#include <exception>

namespace VeroScript::Platform
{
	using namespace BindingDetail;

	bool RegisterScriptBindings(FHostBindingRegistry& Registry, FServiceFactory Factory, std::string& Error)
	{
		if (!Factory)
		{
			Error = "platform.missing_factory: host must provide a backend resolver";
			return false;
		}
		FHostBindingRegistry Candidate = Registry;
		auto State = std::make_shared<FState>();
		State->Factory = std::move(Factory);
		for (unsigned Index = 0; Index < Names.size(); ++Index)
		{
			if (!Candidate.RegisterType(Names[Index], EHostTypeKind::Class, State->Ids[Index], Error))
			{
				return false;
			}
		}
		FHostTypeId EventType;
		if (!Candidate.RegisterType("VS.Platform.EventType", EHostTypeKind::Enum, EventType, Error) ||
			!Candidate.SetEnumScoped(EventType, true, Error))
		{
			return false;
		}
		const char *EventNames[] = {"None",
									"Quit",
									"CloseRequested",
									"Resized",
									"PixelSizeChanged",
									"ScaleChanged",
									"Shown",
									"Hidden",
									"Minimized",
									"Restored",
									"FocusGained",
									"FocusLost",
									"KeyDown",
									"KeyUp",
									"PointerMove",
									"PointerDown",
									"PointerUp",
									"Wheel",
									"TextInput",
									"TextEditing",
									"PointerCancelled",
									"Background",
									"Foreground",
									"InputDeviceAdded",
									"InputDeviceRemoved",
									"InputStateReset",
									"ControllerAxis",
									"ControllerButtonDown",
									"ControllerButtonUp",
									"ControllerHat",
									"MouseEntered",
									"MouseLeft"};
		for (unsigned Index = 0; Index < std::size(EventNames); ++Index)
		{
			FHostMemberDesc Member;
			Member.Kind = EHostMemberKind::EnumMember;
			Member.Name = EventNames[Index];
			Member.EnumValue = Index;
			if (!Candidate.RegisterMember(EventType, std::move(Member), Error))
			{
				return false;
			}
		}
		FHostTypeId KeyType, ModifierType;
		if (!Candidate.RegisterType("VS.Platform.Key", EHostTypeKind::Enum, KeyType, Error) ||
			!Candidate.SetEnumScoped(KeyType, true, Error) ||
			!Candidate.RegisterType("VS.Platform.Modifier", EHostTypeKind::Enum, ModifierType, Error))
		{
			return false;
		}
		const auto AddEnum = [&](FHostTypeId Type, const std::string& Name, std::int64_t Value)
		{
			FHostMemberDesc Member;
			Member.Kind = EHostMemberKind::EnumMember;
			Member.Name = Name;
			Member.EnumValue = Value;
			return Candidate.RegisterMember(Type, std::move(Member), Error);
		};
		const std::pair<const char*, EKey> Keys[] = {{"Unknown", EKey::Unknown},
													 {"Backspace", EKey::Backspace},
													 {"Tab", EKey::Tab},
													 {"Enter", EKey::Enter},
													 {"Escape", EKey::Escape},
													 {"Space", EKey::Space},
													 {"Left", EKey::Left},
													 {"Right", EKey::Right},
													 {"Up", EKey::Up},
													 {"Down", EKey::Down},
													 {"Home", EKey::Home},
													 {"End", EKey::End},
													 {"PageUp", EKey::PageUp},
													 {"PageDown", EKey::PageDown},
													 {"Insert", EKey::Insert},
													 {"Delete", EKey::Delete},
													 {"LeftShift", EKey::LeftShift},
													 {"RightShift", EKey::RightShift},
													 {"LeftControl", EKey::LeftControl},
													 {"RightControl", EKey::RightControl},
													 {"LeftAlt", EKey::LeftAlt},
													 {"RightAlt", EKey::RightAlt},
													 {"LeftSuper", EKey::LeftSuper},
													 {"RightSuper", EKey::RightSuper}};
		for (const auto& Key : Keys)
		{
			if (!AddEnum(KeyType, Key.first, static_cast<std::int64_t>(Key.second)))
			{
				return false;
			}
		}
		for (unsigned Letter = 0; Letter < 26; ++Letter)
		{
			if (!AddEnum(KeyType, std::string(1, static_cast<char>('A' + Letter)),
						 static_cast<unsigned>(EKey::A) + Letter))
			{
				return false;
			}
		}
		for (unsigned Digit = 0; Digit < 10; ++Digit)
		{
			if (!AddEnum(KeyType, "Digit" + std::to_string(Digit), static_cast<unsigned>(EKey::Digit0) + Digit))
			{
				return false;
			}
		}
		for (unsigned Function = 0; Function < 24; ++Function)
		{
			if (!AddEnum(KeyType, "F" + std::to_string(Function + 1), static_cast<unsigned>(EKey::F1) + Function))
			{
				return false;
			}
		}
		const char* ModifierNames[]{"None", "Shift", "Control", "Alt", "Super", "CapsLock", "NumLock"};
		for (unsigned Index = 0; Index < std::size(ModifierNames); ++Index)
		{
			if (!AddEnum(ModifierType, ModifierNames[Index], Index ? 1u << (Index - 1) : 0))
			{
				return false;
			}
		}
		const FRegistrar Add(Candidate, State, Error);
		const FRegistrar AddConst(Candidate, State, Error, true);
		const auto Static = EHostMemberKind::StaticMethod, Method = EHostMemberKind::Method,
				   Property = EHostMemberKind::PropertyGetter;
		for (const bool bExternal : {false, true})
		{
			if (!Add(
					0, bExternal ? "CreateExternal" : "Create", Static, ENativeType::HostObject, {ENativeType::String},
					[State, bExternal](const FNativeCallContext& C, FValue& Out, std::string& E)
					{
						auto Service = State->Factory(std::get<std::string>(C.Args[0]),
													  bExternal ? EEventPumpMode::External : EEventPumpMode::Owned, E);
						if (!Service)
						{
							return false;
						}
						Out = Make(State, 0, std::move(Service));
						return true;
					},
					Names[0]))
			{
				return false;
			}
		}
		for (const bool bGraphics : {false, true})
		{
			std::vector<ENativeType> WindowParams{ENativeType::String, ENativeType::Int64, ENativeType::Int64,
												  ENativeType::Bool};
			if (bGraphics)
			{
				WindowParams.push_back(ENativeType::String);
			}
			if (!Add(
					0, bGraphics ? "CreateWindowForGraphics" : "CreateWindow", Method, ENativeType::HostObject,
					WindowParams,
					[State, bGraphics](const FNativeCallContext& C, FValue& Out, std::string& E)
					{
						FWindowOptions Options;
						Options.Title = std::get<std::string>(C.Args[0]);
						Options.Width = std::get<std::int64_t>(C.Args[1]);
						Options.Height = std::get<std::int64_t>(C.Args[2]);
						Options.bVisible = std::get<bool>(C.Args[3]);
						if (bGraphics)
						{
							Options.GraphicsApi = std::get<std::string>(C.Args[4]);
						}
						auto Window = This<std::shared_ptr<IService>>(C)->CreateWindow(Options, E);
						if (!Window)
						{
							return false;
						}
						Out = Make(State, 1, std::move(Window));
						return true;
					},
					Names[1]))
			{
				return false;
			}
		}
		for (const bool bWait : {false, true})
		{
			if (!Add(
					0, bWait ? "WaitEvent" : "PollEvent", Method, ENativeType::HostObject,
					bWait ? std::vector<ENativeType>{ENativeType::Int64} : std::vector<ENativeType>{},
					[State, bWait](const FNativeCallContext& C, FValue& Out, std::string& E)
					{
						FEvent Event;
						bool bAvailable = false;
						const auto& Service = This<std::shared_ptr<IService>>(C);
						if (!(bWait ? Service->WaitEvent(std::get<std::int64_t>(C.Args[0]), Event, bAvailable, E)
									: Service->PollEvent(Event, bAvailable, E)))
						{
							return false;
						}
						Out = Make(State, 2, std::move(Event));
						return true;
					},
					Names[2]))
			{
				return false;
			}
		}
		if (!Add(0, "CollectRetired", Method, ENativeType::Void, {},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 return This<std::shared_ptr<IService>>(C)->CollectRetired(E);
				 }))
		{
			return false;
		}
		if (!AddConst(0, "HasPendingRetirements", Method, ENativeType::Bool, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string& E)
				 {
					 bool Pending = false;
					 if (!This<std::shared_ptr<IService>>(C)->HasPendingRetirements(Pending, E))
					 {
						 return false;
					 }
					 Out = Pending;
					 return true;
				 }))
		{
			return false;
		}
		if (!Add(0, "SetClipboardText", Method, ENativeType::Void, {ENativeType::String},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 return This<std::shared_ptr<IService>>(C)->SetClipboardText(std::get<std::string>(C.Args[0]), E);
				 }))
		{
			return false;
		}
		if (!AddConst(0, "GetClipboardText", Method, ENativeType::String, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string& E)
				 {
					 std::string Text;
					 if (!This<std::shared_ptr<IService>>(C)->GetClipboardText(Text, E))
					 {
						 return false;
					 }
					 Out = std::move(Text);
					 return true;
				 }))
		{
			return false;
		}
		if (!AddConst(1, "GetId", Method, ENativeType::UInt64, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string&)
				 {
					 Out = This<std::shared_ptr<IWindow>>(C)->GetId();
					 return true;
				 }))
		{
			return false;
		}
		if (!AddConst(1, "IsOpen", Method, ENativeType::Bool, {},
				 [](const FNativeCallContext& C, FValue& Out, std::string&)
				 {
					 Out = This<std::shared_ptr<IWindow>>(C)->IsOpen();
					 return true;
				 }))
		{
			return false;
		}
		if (!Add(1, "Close", Method, ENativeType::Void, {},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 return This<std::shared_ptr<IWindow>>(C)->Close(E);
				 }))
		{
			return false;
		}
		if (!Add(1, "SetTitle", Method, ENativeType::Void, {ENativeType::String},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 return This<std::shared_ptr<IWindow>>(C)->SetTitle(std::get<std::string>(C.Args[0]), E);
				 }))
		{
			return false;
		}
		if (!Add(1, "SetSize", Method, ENativeType::Void, {ENativeType::Int64, ENativeType::Int64},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 return This<std::shared_ptr<IWindow>>(C)->SetSize(std::get<std::int64_t>(C.Args[0]),
																	   std::get<std::int64_t>(C.Args[1]), E);
				 }))
		{
			return false;
		}
		for (const bool bTextInput : {false, true})
		{
			if (!Add(1, bTextInput ? "SetTextInputEnabled" : "SetVisible", Method, ENativeType::Void,
					 {ENativeType::Bool},
					 [bTextInput](const FNativeCallContext& C, FValue&, std::string& E)
					 {
						 const auto& Window = This<std::shared_ptr<IWindow>>(C);
						 return bTextInput ? Window->SetTextInputEnabled(std::get<bool>(C.Args[0]), E)
										   : Window->SetVisible(std::get<bool>(C.Args[0]), E);
					 }))
			{
				return false;
			}
		}
		if (!Add(1, "SetTextInputArea", Method, ENativeType::Void,
				 {ENativeType::Double, ENativeType::Double, ENativeType::Double, ENativeType::Double,
				  ENativeType::Int64},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 return This<std::shared_ptr<IWindow>>(C)->SetTextInputArea(
						 std::get<double>(C.Args[0]), std::get<double>(C.Args[1]), std::get<double>(C.Args[2]),
						 std::get<double>(C.Args[3]), std::get<std::int64_t>(C.Args[4]), E);
				 }))
		{
			return false;
		}
		const char* Metrics[] = {"GetWidth",  "GetHeight", "GetPixelWidth",	 "GetPixelHeight",
								 "GetScaleX", "GetScaleY", "GetDisplayScale"};
		for (unsigned Index = 0; Index < std::size(Metrics); ++Index)
		{
			if (!AddConst(1, Metrics[Index], Method, Index < 4 ? ENativeType::Int64 : ENativeType::Double, {},
					 [Index](const FNativeCallContext& C, FValue& Out, std::string& E)
					 {
						 FWindowMetrics Value;
						 if (!This<std::shared_ptr<IWindow>>(C)->GetMetrics(Value, E))
						 {
							 return false;
						 }
						 if (Index < 4)
						 {
							 const std::int64_t Values[]{Value.Width, Value.Height, Value.PixelWidth,
														 Value.PixelHeight};
							 Out = Values[Index];
						 }
						 else
						 {
							 Out = Index == 4 ? Value.ScaleX : Index == 5 ? Value.ScaleY : Value.DisplayScale;
						 }
						 return true;
					 }))
			{
				return false;
			}
		}
		if (!Add(
				2, "Type", Property, ENativeType::Enum, {},
				[](const FNativeCallContext& C, FValue& Out, std::string&)
				{
					Out = FEnumValue{"VS.Platform.EventType", static_cast<std::int64_t>(This<FEvent>(C).Type)};
					return true;
				},
				"VS.Platform.EventType"))
		{
			return false;
		}
		const auto EventProperty = [&](const char* Name, ENativeType Type, FCall Call)
		{
			return Add(2, Name, Property, Type, {}, std::move(Call));
		};
		if (!EventProperty("WindowId", ENativeType::UInt64,
						   [](const FNativeCallContext& C, FValue& O, std::string&)
						   {
							   O = This<FEvent>(C).WindowId;
							   return true;
						   }) ||
			!EventProperty("TimestampNanoseconds", ENativeType::UInt64,
						   [](const FNativeCallContext& C, FValue& O, std::string&)
						   {
							   O = This<FEvent>(C).TimestampNanoseconds;
							   return true;
						   }) ||
			!EventProperty("PointerId", ENativeType::UInt64,
						   [](const FNativeCallContext& C, FValue& O, std::string&)
						   {
							   O = This<FEvent>(C).PointerId;
							   return true;
						   }) ||
			!EventProperty("Text", ENativeType::String,
						   [](const FNativeCallContext& C, FValue& O, std::string&)
						   {
							   O = This<FEvent>(C).Text;
							   return true;
						   }) ||
			!EventProperty("X", ENativeType::Double,
						   [](const FNativeCallContext& C, FValue& O, std::string&)
						   {
							   O = This<FEvent>(C).X;
							   return true;
						   }) ||
			!EventProperty("Y", ENativeType::Double,
						   [](const FNativeCallContext& C, FValue& O, std::string&)
						   {
							   O = This<FEvent>(C).Y;
							   return true;
						   }) ||
			!EventProperty("DeltaX", ENativeType::Double,
						   [](const FNativeCallContext& C, FValue& O, std::string&)
						   {
							   O = This<FEvent>(C).DeltaX;
							   return true;
						   }) ||
			!EventProperty("DeltaY", ENativeType::Double,
						   [](const FNativeCallContext& C, FValue& O, std::string&)
						   {
							   O = This<FEvent>(C).DeltaY;
							   return true;
						   }) ||
			!EventProperty("DeviceId", ENativeType::UInt64,
						   [](const FNativeCallContext& C, FValue& O, std::string&)
						   {
							   O = This<FEvent>(C).DeviceId;
							   return true;
						   }) ||
			!EventProperty("Modifiers", ENativeType::Unsigned,
						   [](const FNativeCallContext& C, FValue& O, std::string&)
						   {
							   O = This<FEvent>(C).Modifiers;
							   return true;
						   }) ||
			!EventProperty("Buttons", ENativeType::Unsigned,
						   [](const FNativeCallContext& C, FValue& O, std::string&)
						   {
							   O = This<FEvent>(C).Buttons;
							   return true;
						   }) ||
			!EventProperty("Button", ENativeType::Int,
						   [](const FNativeCallContext& C, FValue& O, std::string&)
						   {
							   O = This<FEvent>(C).Button;
							   return true;
						   }) ||
			!EventProperty("Pressure", ENativeType::Double,
						   [](const FNativeCallContext& C, FValue& O, std::string&)
						   {
							   O = This<FEvent>(C).Pressure;
							   return true;
						   }) ||
			!EventProperty("PointerKind", ENativeType::Int,
						   [](const FNativeCallContext& C, FValue& O, std::string&)
						   {
							   O = static_cast<std::int32_t>(This<FEvent>(C).PointerKind);
							   return true;
						   }) ||
			!EventProperty("ScanCode", ENativeType::Int,
						   [](const FNativeCallContext& C, FValue& O, std::string&)
						   {
							   O = This<FEvent>(C).ScanCode;
							   return true;
						   }) ||
			!EventProperty("Repeat", ENativeType::Bool,
						   [](const FNativeCallContext& C, FValue& O, std::string&)
						   {
							   O = This<FEvent>(C).bRepeat;
							   return true;
						   }) ||
			!EventProperty("SelectionStart", ENativeType::Int64,
						   [](const FNativeCallContext& C, FValue& O, std::string&)
						   {
							   O = This<FEvent>(C).SelectionStart;
							   return true;
						   }) ||
			!EventProperty("SelectionLength", ENativeType::Int64,
						   [](const FNativeCallContext& C, FValue& O, std::string&)
						   {
							   O = This<FEvent>(C).SelectionLength;
							   return true;
						   }))
		{
			return false;
		}
		if (!Add(
				2, "Key", Property, ENativeType::Enum, {},
				[](const FNativeCallContext& C, FValue& Out, std::string&)
				{
					Out = FEnumValue{"VS.Platform.Key", static_cast<std::int64_t>(This<FEvent>(C).Key)};
					return true;
				},
				"VS.Platform.Key"))
		{
			return false;
		}
		if (!RegisterInputBindings(Candidate, State, Error) || !RegisterControllerBindings(Candidate, State, Error) ||
			!RegisterWindowBindings(Candidate, State, Error))
		{
			return false;
		}
		Registry = std::move(Candidate);
		Error.clear();
		return true;
	}

	std::shared_ptr<IWindow> GetScriptWindow(const FValue& Value, std::string& Error)
	{
		const auto* Object = std::get_if<FHostObjectHandle>(&Value);
		if (!Object || !*Object || !std::get_deleter<FDelete<std::shared_ptr<IWindow>>>(*Object))
		{
			Error = "platform.invalid_window: expected a Platform binding window";
			return {};
		}
		return *static_cast<const std::shared_ptr<IWindow>*>((*Object)->Payload);
	}
}
