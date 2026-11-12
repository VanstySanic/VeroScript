// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiContext.h"
#include "VeroScriptGuiScriptInternal.h"
#include <string_view>

namespace VeroScript::Gui::ScriptBindings
{
	bool RegisterControlBindings(FHostBindingRegistry &Registry, const std::shared_ptr<FBridge> &Bridge,
	    std::string &Error)
	{
		const auto Void = FNativeTypeDesc::Scalar(ENativeType::Void);
		const auto Int = FNativeTypeDesc::Scalar(ENativeType::Int);
		const auto Double = FNativeTypeDesc::Scalar(ENativeType::Double);
		const auto Bool = FNativeTypeDesc::Scalar(ENativeType::Bool);
		const auto String = FNativeTypeDesc::Scalar(ENativeType::String);
		if (!Member(Registry, Bridge->Types[0], "CompareText", EHostMemberKind::StaticMethod, Int, {String, String},
		        [](const auto &Call, auto &Out, auto &)
		{
			const int Order = std::get<std::string>(Call.Args[0]).compare(std::get<std::string>(Call.Args[1]));
			Out = std::int32_t(Order < 0 ? -1 : Order > 0 ? 1
			                                              : 0);
			return true;
		}, Error, true))
		{
			return false;
		}
		if (!Member(Registry, Bridge->Types[0], "GetControlKind", EHostMemberKind::Method, Int, {},
		        [](const auto &Call, auto &Out, auto &Failure)
		{
			const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
			if (!Widget)
			{
				Failure = "gui.widget_expired";
				return false;
			}
			Out = static_cast<std::int32_t>(Widget->GetControlOptions().Kind);
			return true;
		}, Error))
		{
			return false;
		}
		if (!Member(Registry, Bridge->Types[0], "ConfigureItem", EHostMemberKind::Method, Void,
		        {Bool, Bool, Bool, FNativeTypeDesc::Host(TypeNames[3]), Bool},
		        [](const auto &Call, auto &, auto &Failure)
		{
			const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
			if (!Widget)
			{
				Failure = "gui.widget_expired";
				return false;
			}
			auto Options = Widget->GetControlOptions();
			Options.bSelected = std::get<bool>(Call.Args[1]);
			Options.bExpandable = std::get<bool>(Call.Args[2]);
			Options.bExpanded = std::get<bool>(Call.Args[3]);
			Options.SelectionColor = Payload<Graphics::FColor>(Call.Args[4]);
			Options.bItemIndicator = std::get<bool>(Call.Args[5]);
			return Widget->SetControlOptions(std::move(Options), Failure);
		}, Error) ||
		    !Member(Registry, Bridge->Types[0], "ConfigurePopup", EHostMemberKind::Method, Void,
		        {Bool, Bool, Bool, String},
		        [](const auto &Call, auto &, auto &Failure)
		{
			const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
			if (!Widget)
			{
				Failure = "gui.widget_expired";
				return false;
			}
			auto Options = Widget->GetControlOptions();
			Options.bModal = std::get<bool>(Call.Args[1]);
			Options.bDismissOnEscape = std::get<bool>(Call.Args[2]);
			Options.bMatchAnchorWidth = std::get<bool>(Call.Args[3]);
			const auto &Placement = std::get<std::string>(Call.Args[4]);
			if (Placement == "below" || Placement == "Below")
			{
				Options.Placement = FControlOptions::EPlacement::Below;
			}
			else if (Placement == "right" || Placement == "Right")
			{
				Options.Placement = FControlOptions::EPlacement::Right;
			}
			else if (Placement == "center" || Placement == "Center")
			{
				Options.Placement = FControlOptions::EPlacement::Center;
			}
			else
			{
				Failure = "gui.invalid_control_options";
				return false;
			}
			return Widget->SetControlOptions(std::move(Options), Failure);
		}, Error) ||
		    !Member(Registry, Bridge->Types[0], "SetFloatingOpen", EHostMemberKind::Method, Void, {Bool},
		        [](const auto &Call, auto &, auto &Failure)
		{
			const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
			if (!Widget)
			{
				Failure = "gui.widget_expired";
				return false;
			}
			return Widget->SetFloatingOpen(std::get<bool>(Call.Args[1]), Failure);
		}, Error))
		{
			return false;
		}
		if (!Member(
		        Registry, Bridge->Types[0], "InitializeControl", EHostMemberKind::Method, Void, {Int},
		        [](const auto &Call, auto &, auto &Failure)
		{
			const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
			const auto Kind = std::get<std::int32_t>(Call.Args[1]);
			if (!Widget || Kind < 0 || Kind > static_cast<int>(EControlKind::ToolTip))
			{
				Failure = "gui.invalid_control_kind";
				return false;
			}
			FControlOptions Options;
			Options.Kind = static_cast<EControlKind>(Kind);
			if (!Widget->SetControlOptions(Options, Failure))
			{
				return false;
			}
			const auto Set = [&](EProperty P, FPropertyValue V)
			{
				return Widget->SetStyle(P, std::move(V), Failure);
			};
			const bool bInteractive =
			    Options.Kind != EControlKind::None && Options.Kind != EControlKind::ProgressBar &&
			    Options.Kind != EControlKind::Grid && Options.Kind != EControlKind::Wrap &&
			    Options.Kind != EControlKind::RadioGroup && !IsFloatingControl(Options.Kind);
			if (!Widget->Set(EProperty::Focusable, bInteractive, Failure))
			{
				return false;
			}
			if (Options.Kind == EControlKind::Button || IsTextControl(Options.Kind))
			{
				if (!Set(EProperty::Padding, FInsets{6, 8, 6, 8}) ||
				    !Set(EProperty::Background, Graphics::FColor{.06f, .07f, .09f, 1}))
				{
					return false;
				}
			}
			if (Options.Kind == EControlKind::CheckBox || Options.Kind == EControlKind::ToggleSwitch ||
			    Options.Kind == EControlKind::RadioButton)
			{
				if (!Set(EProperty::Padding, FInsets{4, 4, 4, 4}) ||
				    !Set(EProperty::Height, FLength{ELengthUnit::Pixel, 28}))
				{
					return false;
				}
			}
			if (Options.Kind == EControlKind::ProgressBar || Options.Kind == EControlKind::Slider ||
			    Options.Kind == EControlKind::ScrollBar)
			{
				if (!Set(EProperty::Height, FLength{ELengthUnit::Pixel, 16}) ||
				    !Set(EProperty::Background, Graphics::FColor{.04f, .05f, .06f, 1}))
				{
					return false;
				}
			}
			if (Options.Kind == EControlKind::ScrollView || IsTextControl(Options.Kind))
			{
				if (!Set(EProperty::ClipContent, true) ||
				    !Set(EProperty::Height,
				        FLength{ELengthUnit::Pixel, Options.Kind == EControlKind::TextArea || Options.Kind == EControlKind::ScrollView ? 120.0 : 36.0}))
				{
					return false;
				}
			}
			if (Options.Kind == EControlKind::Wrap && !Set(EProperty::Layout, ELayout::Horizontal))
			{
				return false;
			}
			if (Options.Kind == EControlKind::ToolTip &&
			    !Widget->Set(EProperty::HitTest, EHitTestMode::None, Failure))
			{
				return false;
			}
			if (Options.Kind == EControlKind::Splitter &&
			    !Set(EProperty::Height, FLength{ELengthUnit::Pixel, 120}))
			{
				return false;
			}
			return true;
		},
		        Error))
		{
			return false;
		}
		if (!Member(
		        Registry, Bridge->Types[0], "InitializeLayout", EHostMemberKind::Method, Void, {Int},
		        [](const auto &Call, auto &, auto &Failure)
		{
			const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
			const auto Preset = std::get<std::int32_t>(Call.Args[1]);
			if (!Widget || Preset < 0 || Preset > 3)
			{
				Failure = "gui.invalid_layout_preset";
				return false;
			}
			if (Preset == 3)
			{
				return Widget->SetStyle(EProperty::BorderWidth, 1.0, Failure);
			}
			if (Preset == 0)
			{
				return Widget->SetStyle(EProperty::Layout, ELayout::Overlay, Failure);
			}
			return Widget->SetStyle(EProperty::Height, FLength{ELengthUnit::Pixel, Preset == 1 ? 8.0 : 1.0},
			           Failure) &&
			    Widget->Set(EProperty::HitTest, EHitTestMode::None, Failure) &&
			    (Preset != 2 ||
			        Widget->SetStyle(EProperty::Background, Graphics::FColor{.15f, .15f, .15f, 1}, Failure));
		},
		        Error))
		{
			return false;
		}
		for (const auto *Name : {"ConfigureControl", "ConfigureControlAndGetValue"})
		{
			const bool bReturnValue = std::string_view(Name) == "ConfigureControlAndGetValue";
			if (!Member(
			        Registry, Bridge->Types[0], Name, EHostMemberKind::Method, bReturnValue ? Double : Void,
			        {Bool, Double, Double, Double, Double, Bool, Bool, String, String, Double, Double, Double},
			        [bReturnValue](const auto &Call, auto &Out, auto &Failure)
			{
				const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
				if (!Widget)
				{
					Failure = "gui.widget_expired";
					return false;
				}
				auto Options = Widget->GetControlOptions();
				Options.bChecked = std::get<bool>(Call.Args[1]);
				Options.Value = std::get<double>(Call.Args[2]);
				Options.Minimum = std::get<double>(Call.Args[3]);
				Options.Maximum = std::get<double>(Call.Args[4]);
				Options.Step = std::get<double>(Call.Args[5]);
				Options.bVertical = std::get<bool>(Call.Args[6]);
				Options.bReadOnly = std::get<bool>(Call.Args[7]);
				Options.Text = std::get<std::string>(Call.Args[8]);
				Options.Placeholder = std::get<std::string>(Call.Args[9]);
				Options.ScrollX = std::get<double>(Call.Args[10]);
				Options.ScrollY = std::get<double>(Call.Args[11]);
				Options.PageSize = std::get<double>(Call.Args[12]);
				if (!Widget->SetControlOptions(std::move(Options), Failure))
				{
					return false;
				}
				if (bReturnValue)
				{
					Out = Widget->GetControlOptions().Value;
				}
				return true;
			},
			        Error))
			{
				return false;
			}
		}
		if (!Member(
		        Registry, Bridge->Types[0], "ConfigureLayout", EHostMemberKind::Method, Void,
		        {FNativeTypeDesc::Scalar(ENativeType::Int64), Double},
		        [](const auto &Call, auto &, auto &Failure)
		{
			const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
			if (!Widget)
			{
				Failure = "gui.widget_expired";
				return false;
			}
			auto O = Widget->GetControlOptions();
			O.Columns = std::get<std::int64_t>(Call.Args[1]);
			O.DividerSize = std::get<double>(Call.Args[2]);
			return Widget->SetControlOptions(std::move(O), Failure);
		},
		        Error) ||
		    !Member(
		        Registry, Bridge->Types[0], "ConfigureChoice", EHostMemberKind::Method, Void, {String},
		        [](const auto &Call, auto &, auto &Failure)
		{
			const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
			if (!Widget)
			{
				Failure = "gui.widget_expired";
				return false;
			}
			auto O = Widget->GetControlOptions();
			O.ChoiceKey = std::get<std::string>(Call.Args[1]);
			return Widget->SetControlOptions(std::move(O), Failure);
		},
		        Error) ||
		    !Member(
		        Registry, Bridge->Types[0], "ConfigureFloating", EHostMemberKind::Method, Void,
		        {Bool, Bool, Bool, Double, Double, Double},
		        [](const auto &Call, auto &, auto &Failure)
		{
			const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
			if (!Widget)
			{
				Failure = "gui.widget_expired";
				return false;
			}
			auto O = Widget->GetControlOptions();
			O.bOpen = std::get<bool>(Call.Args[1]);
			O.bDismissOnOutside = std::get<bool>(Call.Args[2]);
			O.bAutoOpen = std::get<bool>(Call.Args[3]);
			O.OffsetX = std::get<double>(Call.Args[4]);
			O.OffsetY = std::get<double>(Call.Args[5]);
			O.Delay = std::get<double>(Call.Args[6]);
			return Widget->SetControlOptions(std::move(O), Failure);
		},
		        Error))
		{
			return false;
		}
		for (const auto *Name : {"GetControlChecked", "GetControlOpen"})
		{
			if (!Member(
			        Registry, Bridge->Types[0], Name, EHostMemberKind::Method, Bool, {},
			        [Name = std::string(Name)](const auto &Call, auto &Out, auto &Failure)
			{
				const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
				if (!Widget || Widget->IsDisposed())
				{
					Failure = "gui.widget_expired";
					return false;
				}
				const auto &Options = Widget->GetControlOptions();
				Out = Name == "GetControlChecked" ? Options.bChecked : Options.bOpen;
				return true;
			},
			        Error, true))
			{
				return false;
			}
		}
		for (const auto *Name : {"Activate", "FocusControl"})
		{
			if (!Member(
			        Registry, Bridge->Types[0], Name, EHostMemberKind::Method, Void, {},
			        [Bridge, Name = std::string(Name)](const auto &Call, auto &, auto &Failure)
			{
				FInvokerScope Guard(*Bridge, Call.ScriptInvoker);
				const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
				if (!Widget)
				{
					Failure = "gui.widget_expired";
					return false;
				}
				return Name == "Activate" ? Widget->Activate(Failure) : Widget->FocusControl(Failure);
			},
			        Error))
			{
				return false;
			}
		}
		if (!Member(
		        Registry, Bridge->Types[0], "GetControlValue", EHostMemberKind::Method, Double, {},
		        [](const auto &Call, auto &Out, auto &Failure)
		{
			const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
			if (!Widget || Widget->IsDisposed())
			{
				Failure = "gui.widget_expired";
				return false;
			}
			Out = Widget->GetControlOptions().Value;
			return true;
		},
		        Error, true))
		{
			return false;
		}
		if (!Member(
		        Registry, Bridge->Types[0], "GetControlText", EHostMemberKind::Method, String, {},
		        [](const auto &Call, auto &Out, auto &Failure)
		{
			const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
			if (!Widget || Widget->IsDisposed())
			{
				Failure = "gui.widget_expired";
				return false;
			}
			Out = Widget->GetControlOptions().Text;
			return true;
		},
		        Error, true))
		{
			return false;
		}
		const auto Int64 = FNativeTypeDesc::Scalar(ENativeType::Int64);
		if (!Member(
		        Registry, Bridge->Types[0], "SetTextSelection", EHostMemberKind::Method, Void, {Int64, Int64},
		        [](const auto &Call, auto &, auto &Failure)
		{
			const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
			if (!Widget)
			{
				Failure = "gui.widget_expired";
				return false;
			}
			return Widget->SetTextSelection(std::get<std::int64_t>(Call.Args[1]),
			    std::get<std::int64_t>(Call.Args[2]), Failure);
		},
		        Error))
		{
			return false;
		}
		return Member(
		    Registry, Bridge->Types[0], "GetScrollGeometry", EHostMemberKind::Method, Double, {Int},
		    [](const auto &Call, auto &Out, auto &Failure)
		{
			const auto Widget = static_cast<const FWidgetView *>(Call.HostThis)->lock();
			FLayoutBox Box;
			if (!Widget || !Widget->GetLayout(Box, Failure))
			{
				if (Failure.empty())
				{
					Failure = "gui.widget_expired";
				}
				return false;
			}
			const auto Index = std::get<std::int32_t>(Call.Args[1]);
			if (Index < 0 || Index > 3)
			{
				Failure = "gui.invalid_geometry_member";
				return false;
			}
			Out = Index == 0 ? Box.ScrollX
			    : Index == 1 ? Box.ScrollY
			    : Index == 2 ? Box.ScrollWidth
			                 : Box.ScrollHeight;
			return true;
		},
		    Error, true);
	}
} // namespace VeroScript::Gui::ScriptBindings
