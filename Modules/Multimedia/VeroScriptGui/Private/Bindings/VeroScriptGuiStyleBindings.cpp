// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiStyleBindings.h"
#include "VeroScriptGuiMarkup.h"
#include "Tools/VeroScriptGuiArtifact.h"
#include <sstream>

namespace VeroScript::Gui::ScriptBindings
{
	bool RegisterStyleBindings(FHostBindingRegistry& Registry, const std::shared_ptr<FBridge>& Bridge,
	    std::string& Error)
	{
		const auto Void = FNativeTypeDesc::Scalar(ENativeType::Void);
		const auto String = FNativeTypeDesc::Scalar(ENativeType::String);
		const auto Int64 = FNativeTypeDesc::Scalar(ENativeType::Int64);
		const auto Int = FNativeTypeDesc::Scalar(ENativeType::Int);
		const auto Bool = FNativeTypeDesc::Scalar(ENativeType::Bool);
		if (!Member(Registry, Bridge->Types[0], "SetClass", EHostMemberKind::Method, Void, {String, Bool},
		        [](const auto& Call, auto&, auto& Error)
		{
			const auto Widget = static_cast<const FWidgetView*>(Call.HostThis)->lock();
			if (!Widget)
			{
				Error = "gui.widget_expired";
				return false;
			}
			return Widget->SetClass(std::get<std::string>(Call.Args[1]), std::get<bool>(Call.Args[2]), Error);
		}, Error) ||
		    !Member(Registry, Bridge->Types[0], "SetContentVisible", EHostMemberKind::Method, Void, {Bool},
		        [](const auto& Call, auto&, auto& Error)
		{
			const auto Widget = static_cast<const FWidgetView*>(Call.HostThis)->lock();
			if (!Widget)
			{
				Error = "gui.widget_expired";
				return false;
			}
			return Widget->SetContentVisible(std::get<bool>(Call.Args[1]), Error);
		}, Error) ||
		    !Member(Registry, Bridge->Types[0], "IsContentVisible", EHostMemberKind::Method, Bool, {},
		        [](const auto& Call, auto& Out, auto& Error)
		{
			const auto Widget = static_cast<const FWidgetView*>(Call.HostThis)->lock();
			if (!Widget)
			{
				Error = "gui.widget_expired";
				return false;
			}
			Out = Widget->IsContentVisible();
			return true;
		}, Error))
		{
			return false;
		}
		for (const bool Style : {false, true})
		{
			if (!Member(
			        Registry, Bridge->Types[0], Style ? "GetStyleMutationToken" : "GetMutationToken",
			        EHostMemberKind::Method, FNativeTypeDesc::Scalar(ENativeType::UInt64), {},
			        [Style](const auto& Call, auto& Out, auto& Error)
			{
				const auto Widget = static_cast<const FWidgetView*>(Call.HostThis)->lock();
				if (!Widget)
				{
					Error = "gui.widget_expired";
					return false;
				}
				Out = Style ? Widget->GetStyleMutationToken() : Widget->GetMutationToken();
				return true;
			},
			        Error))
			{
				return false;
			}
		}
		if (!Member(
		        Registry, Bridge->Types[0], "Invalidate", EHostMemberKind::Method, Void, {},
		        [](const auto& Call, auto&, auto& Error)
		{
			const auto Widget = static_cast<const FWidgetView*>(Call.HostThis)->lock();
			return Widget && Widget->Invalidate(Error);
		},
		        Error) ||
		    !Member(
		        Registry, Bridge->Types[0], "GetSynchronizationToken", EHostMemberKind::Method, Int64, {},
		        [](const auto& Call, auto& Out, auto& Error)
		{
			const auto Widget = static_cast<const FWidgetView*>(Call.HostThis)->lock();
			if (!Widget)
			{
				Error = "gui.widget_expired";
				return false;
			}
			Out = static_cast<std::int64_t>(Widget->GetSynchronizationToken());
			return true;
		},
		        Error) ||
		    !Member(
		        Registry, Bridge->Types[0], "SetStyleType", EHostMemberKind::Method, Void, {Int64, String, String},
		        [](const auto& Call, auto&, auto& Error)
		{
			const auto Widget = static_cast<const FWidgetView*>(Call.HostThis)->lock();
			const auto Scope = std::get<std::int64_t>(Call.Args[1]);
			if (!Widget || Scope <= 0)
			{
				Error = "gui.invalid_style_scope";
				return false;
			}
			return Widget->SetStyleType(static_cast<std::uint64_t>(Scope), std::get<std::string>(Call.Args[2]),
			    std::get<std::string>(Call.Args[3]), Error);
		},
		        Error) ||
		    !Member(
		        Registry, Bridge->Types[0], "SetClassesText", EHostMemberKind::Method, Void, {String},
		        [](const auto& Call, auto&, auto& Error)
		{
			const auto Widget = static_cast<const FWidgetView*>(Call.HostThis)->lock();
			if (!Widget)
			{
				Error = "gui.widget_expired";
				return false;
			}
			std::istringstream Stream(std::get<std::string>(Call.Args[1]));
			std::vector<std::string> Values;
			for (std::string Value; Stream >> Value;)
			{
				Values.push_back(std::move(Value));
			}
			return Widget->SetClasses(std::move(Values), Error);
		},
		        Error))
		{
			return false;
		}
		auto Sheets =
		    std::make_shared<std::map<std::pair<std::string, std::string>, std::weak_ptr<const FStyleSheet>>>();
		if (!Member(
		        Registry, Bridge->Types[0], "AttachStyleSheet", EHostMemberKind::Method, Void, {Int64, String, String},
		        [Sheets, Bridge](const auto& Call, auto&, auto& Error)
		{
			const auto Widget = static_cast<const FWidgetView*>(Call.HostThis)->lock();
			const auto Scope = std::get<std::int64_t>(Call.Args[1]);
			if (!Widget || Scope <= 0)
			{
				Error = "gui.invalid_style_scope";
				return false;
			}
			const auto Key =
			    std::make_pair(std::get<std::string>(Call.Args[2]), std::get<std::string>(Call.Args[3]));
			auto Sheet = (*Sheets)[Key].lock();
			if (!Sheet)
			{
				FMarkupDiagnostic Diagnostic;
				auto Label = Key.first;
				if (Label.rfind(GuiSourceMarker, 0) == 0)
				{
					Label.erase(0, std::char_traits<char>::length(GuiSourceMarker));
				}
				Sheet = FStyleSheet::CompileTyped({Label, Key.second}, Diagnostic,
				    Bridge->Services.ResolveResourceReference);
				if (!Sheet)
				{
					Error = Diagnostic.Format();
					return false;
				}
				for (auto It = Sheets->begin(); It != Sheets->end();)
				{
					if (It->second.expired())
					{
						It = Sheets->erase(It);
					}
					else
					{
						++It;
					}
				}
				(*Sheets)[Key] = Sheet;
			}
			return Widget->AttachStyleSheet(static_cast<std::uint64_t>(Scope), std::move(Sheet), Error);
		},
		        Error) ||
		    !Member(
		        Registry, Bridge->Types[0], "RegisterStyleProperty", EHostMemberKind::Method, Void,
		        {String, String, Int, FNativeTypeDesc::ArrayOf("string"), FNativeTypeDesc::ArrayOf("int64")},
		        [](const auto& Call, auto&, auto& Error)
		{
			const auto Widget = static_cast<const FWidgetView*>(Call.HostThis)->lock();
			const auto Kind = std::get<std::int32_t>(Call.Args[3]);
			const auto& Names = std::get<FDynamicArrayHandle>(Call.Args[4]);
			const auto& Values = std::get<FDynamicArrayHandle>(Call.Args[5]);
			if (!Widget || Kind < 0 || Kind > static_cast<int>(EStyleValueType::Raw) || !Names || !Values ||
			    Names->Elements.size() != Values->Elements.size())
			{
				Error = "gui.invalid_style_codec";
				return false;
			}
			std::map<std::string, std::int64_t> Enums;
			for (std::size_t Index = 0; Index < Names->Elements.size(); ++Index)
			{
				Enums.emplace(std::get<std::string>(Names->Elements[Index]),
				    std::get<std::int64_t>(Values->Elements[Index]));
			}
			return Widget->RegisterStyleProperty(
			    MakeStyleProperty(std::get<std::string>(Call.Args[1]), std::get<std::string>(Call.Args[2]),
			        static_cast<EStyleValueType>(Kind), std::move(Enums)),
			    Error);
		},
		        Error))
		{
			return false;
		}
		if (!Member(
		        Registry, Bridge->Types[0], "HasStyleValue", EHostMemberKind::Method, Bool, {String},
		        [](const auto& Call, auto& Out, auto& Error)
		{
			const auto Widget = static_cast<const FWidgetView*>(Call.HostThis)->lock();
			std::optional<FStyleValue> Value;
			if (!Widget || !Widget->GetStyleValue(std::get<std::string>(Call.Args[1]), Value, Error))
			{
				return false;
			}
			Out = Value.has_value();
			return true;
		},
		        Error, true))
		{
			return false;
		}
		const char* Names[]{"String", "Bool", "Int", "Int64", "UInt64", "Float",
		    "Double", "Length", "Insets", "Color", "Enum"};
		const FNativeTypeDesc Types[]{String,
		    Bool,
		    Int,
		    Int64,
		    FNativeTypeDesc::Scalar(ENativeType::UInt64),
		    FNativeTypeDesc::Scalar(ENativeType::Float),
		    FNativeTypeDesc::Scalar(ENativeType::Double),
		    FNativeTypeDesc::Host(TypeNames[1]),
		    FNativeTypeDesc::Host(TypeNames[2]),
		    FNativeTypeDesc::Host(TypeNames[3]),
		    Int64};
		for (int Kind = 0; Kind <= static_cast<int>(EStyleValueType::Enum); ++Kind)
		{
			if (!Member(
			        Registry, Bridge->Types[0], std::string("GetStyle") + Names[Kind], EHostMemberKind::Method,
			        Types[Kind], {String},
			        [Bridge, Kind](const auto& Call, auto& Out, auto& Error)
			{
				const auto Widget = static_cast<const FWidgetView*>(Call.HostThis)->lock();
				std::optional<FStyleValue> Value;
				if (!Widget || !Widget->GetStyleValue(std::get<std::string>(Call.Args[1]), Value, Error))
				{
					return false;
				}
				if (!Value)
				{
					Error = "gui.style_value_unset";
					return false;
				}
				if (Kind == 0)
				{
					Out = std::get<std::string>(*Value);
				}
				else if (Kind == 2)
				{
					Out = static_cast<std::int32_t>(std::get<std::int64_t>(*Value));
				}
				else if (Kind == 3 || Kind == 10)
				{
					Out = std::get<std::int64_t>(*Value);
				}
				else if (Kind == 4)
				{
					Out = std::get<std::uint64_t>(*Value);
				}
				else
				{
					const auto& Native = std::get<FPropertyValue>(*Value);
					if (Kind == 1)
					{
						Out = std::get<bool>(Native);
					}
					else if (Kind == 5)
					{
						Out = static_cast<float>(std::get<double>(Native));
					}
					else if (Kind == 6)
					{
						Out = std::get<double>(Native);
					}
					else if (Kind == 7)
					{
						Out = Make(Bridge, 1, std::get<FLength>(Native));
					}
					else if (Kind == 8)
					{
						Out = Make(Bridge, 2, std::get<FInsets>(Native));
					}
					else
					{
						Out = Make(Bridge, 3, std::get<Graphics::FColor>(Native));
					}
				}
				return true;
			},
			        Error))
			{
				return false;
			}
		}
		return true;
	}
} // namespace VeroScript::Gui::ScriptBindings
