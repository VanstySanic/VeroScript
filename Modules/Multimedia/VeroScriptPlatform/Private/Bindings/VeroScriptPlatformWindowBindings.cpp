// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptPlatformBindingState.h"

namespace VeroScript::Platform::BindingDetail
{
	bool RegisterWindowBindings(FHostBindingRegistry& Registry, const std::shared_ptr<FState>& State,
								std::string& Error)
	{
		constexpr const char* CursorName = "VS.Platform.Cursor";
		FHostTypeId Cursor;
		if (!Registry.RegisterType(CursorName, EHostTypeKind::Enum, Cursor, Error) ||
			!Registry.SetEnumScoped(Cursor, true, Error))
		{
			return false;
		}
		const char* CursorNames[]{"Default",	"Text",		"Wait",		"Crosshair", "Progress",   "ResizeNWSE",
								  "ResizeNESW", "ResizeEW", "ResizeNS", "Move",		 "NotAllowed", "Pointer"};
		for (unsigned I = 0; I < std::size(CursorNames); ++I)
		{
			FHostMemberDesc Member;
			Member.Kind = EHostMemberKind::EnumMember;
			Member.Name = CursorNames[I];
			Member.EnumValue = I;
			if (!Registry.RegisterMember(Cursor, std::move(Member), Error))
			{
				return false;
			}
		}
		const FRegistrar Add(Registry, State, Error);
		const FRegistrar AddConst(Registry, State, Error, true);
		const auto Method = EHostMemberKind::Method;
		if (!Add(1, "SetCursor", Method, ENativeType::Void, {ENativeType::Enum, ENativeType::Bool},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 const auto Shape = std::get<FEnumValue>(C.Args[0]).Value;
					 if (Shape < 0 || Shape > static_cast<std::int64_t>(ECursor::Pointer))
					 {
						 E = "platform.invalid_cursor: unknown system cursor";
						 return false;
					 }
					 return This<std::shared_ptr<IWindow>>(C)->SetCursor(static_cast<ECursor>(Shape),
																		 std::get<bool>(C.Args[1]), E);
				 },
				 "", {CursorName, ""}) ||
			!Add(1, "SetMouseCapture", Method, ENativeType::Void, {ENativeType::Bool},
				 [](const FNativeCallContext& C, FValue&, std::string& E)
				 {
					 return This<std::shared_ptr<IWindow>>(C)->SetMouseCapture(std::get<bool>(C.Args[0]), E);
				 }))
		{
			return false;
		}
		for (const bool bBorrowed : {false, true})
		{
			if (!AddConst(1, bBorrowed ? "IsBorrowed" : "HasMouseCapture", Method, ENativeType::Bool, {},
					 [bBorrowed](const FNativeCallContext& C, FValue& Out, std::string&)
					 {
						 const auto& Window = This<std::shared_ptr<IWindow>>(C);
						 Out = bBorrowed ? Window->IsBorrowed() : Window->HasMouseCapture();
						 return true;
					 }))
			{
				return false;
			}
		}
		const char* Capabilities[]{"SupportsSystemCursor", "SupportsMouseCapture", "SupportsBorrowedWindows"};
		for (unsigned I = 0; I < std::size(Capabilities); ++I)
		{
			if (!AddConst(0, Capabilities[I], Method, ENativeType::Bool, {},
					 [I](const FNativeCallContext& C, FValue& Out, std::string&)
					 {
						 const auto Caps = This<std::shared_ptr<IService>>(C)->GetCapabilities();
						 Out = I == 0 ? Caps.bSystemCursor : I == 1 ? Caps.bMouseCapture : Caps.bBorrowedWindows;
						 return true;
					 }))
			{
				return false;
			}
		}
		return true;
	}
}
