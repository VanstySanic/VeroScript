// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export import "Widget.vs";
import "delegate.vs";

namespace VS::Gui
{
	namespace Internal
	{
		double ControlFraction(double value, double low, double high)
		{
			if (high <= low || value <= low)
			{
				return 0;
			}
			if (value >= high)
			{
				return 1;
			}
			return (value - low) / (high - low);
		}
	}

	@ GuiWidget(Mark = "text") export class Text : public VS::Gui::Widget
	{
	public:
		Text()
		{
		}
	};

	@ GuiWidget(Mark = "image") export class Image : public VS::Gui::Widget
	{
	public:
		Image()
		{
		}
	};

	@ GuiWidget(Mark = "border") export class Border : public VS::Gui::Widget
	{
	public:
		Border()
		{
			this.__GetNativeHandle().InitializeLayout(3);
		}
	};

	@ GuiWidget(Mark = "stack") export class Stack : public VS::Gui::Widget
	{
	public:
		Stack()
		{
		}
	};

	@ GuiWidget(Mark = "overlay") export class Overlay : public VS::Gui::Widget
	{
	public:
		Overlay()
		{
			this.__GetNativeHandle().InitializeLayout(0);
		}
	};

	@ GuiWidget(Mark = "spacer") export class Spacer : public VS::Gui::Widget
	{
	public:
		Spacer()
		{
			this.__GetNativeHandle().InitializeLayout(1);
		}
	};

	@ GuiWidget(Mark = "separator") export class Separator : public VS::Gui::Widget
	{
	public:
		Separator()
		{
			this.__GetNativeHandle().InitializeLayout(2);
		}
	};

	@ GuiWidget(Mark = "button") export class Button : public VS::Gui::Widget
	{
	public:
		void VsguiControlAction(int action, bool checkedValue, double number, string text, double x, double y) override
		{
			if (action == 0)
			{
				Clicked.broadcast(this);
			}
		}

		Button()
		{
			this.__GetNativeHandle().InitializeControl(1);
			VsguiRegisterControlAction();
		}
		@ GuiMethod void Click()
		{
			this.__GetNativeHandle().Activate();
		}
		@ GuiMethod void Focus()
		{
			this.__GetNativeHandle().FocusControl();
		}

	private:
		@ GuiEvent(Mark = "clicked") std::multicast_delegate<VS::Gui::Button> Clicked = std::multicast_delegate<VS::Gui::Button>();
	};

	@ GuiWidget(Mark = "check-box") export class CheckBox : public VS::Gui::Widget
	{
	public:
		//~ Begin VS::Gui::Widget Interface
		void VsguiSynchronizeControl() override
		{
			if (!VsguiNeedsControlConfiguration()) return;
			this.__GetNativeHandle().ConfigureControl(GetChecked(), 0, 0, 1, 0, false, false, "", "", 0, 0, 0);
			VsguiCommitControlConfiguration();
		}
		void VsguiControlAction(int action, bool checkedValue, double number, string text, double x, double y) override
		{
			if (action == 1)
			{
				if (!IsCheckedBound())
				{
					SetChecked(checkedValue);
				}
				CheckedChanged.broadcast(this, checkedValue);
			}
		}
		//~ End VS::Gui::Widget Interface

		CheckBox()
		{
			this.__GetNativeHandle().InitializeControl(2);
			VsguiRegisterControlAction();
		}
		@ GuiMethod void Toggle()
		{
			this.__GetNativeHandle().Activate();
		}
		@ GuiMethod void Focus()
		{
			this.__GetNativeHandle().FocusControl();
		}

	private:
		@ GuiProperty(Mark = "checked") std::attribute<bool> Checked = std::attribute<bool>(false);
		@ GuiEvent(Mark = "checked-changed") std::multicast_delegate<VS::Gui::CheckBox, bool> CheckedChanged = std::multicast_delegate<VS::Gui::CheckBox, bool>();
	};

	@ GuiWidget(Mark = "toggle-switch") export class ToggleSwitch : public VS::Gui::CheckBox
	{
	public:
		ToggleSwitch()
		{
			this.__GetNativeHandle().InitializeControl(3);
			VsguiRegisterControlAction();
		}
	};

	@ GuiWidget(Mark = "progress-bar") export class ProgressBar : public VS::Gui::Widget
	{
	public:
		void VsguiSynchronizeControl() override
		{
			if (!VsguiNeedsControlConfiguration()) return;
			bool snapshot = VsguiCanUseControlSnapshot();
			double actual = this.__GetNativeHandle().ConfigureControlAndGetValue(false, snapshot ? VsguiPropertyValueReadValue : GetValue(), snapshot ? VsguiPropertyMinimumReadValue : GetMinimum(), snapshot ? VsguiPropertyMaximumReadValue : GetMaximum(), snapshot ? VsguiPropertyStepReadValue : GetStep(), snapshot ? VsguiPropertyVerticalReadValue : GetVertical(), false, "", "", 0, 0, snapshot ? VsguiPropertyPageSizeReadValue : GetPageSize());
			if (!IsValueBound() && GetValue() != actual)
			{
				SetValue(actual);
			}
			VsguiCommitControlConfiguration();
		}

		ProgressBar()
		{
			this.__GetNativeHandle().InitializeControl(4);
			VsguiRegisterControlAction();
		}
		@ GuiMethod double GetActualValue() const
		{
			return this.__GetNativeHandle().GetControlValue();
		}
		@ GuiMethod double GetNormalizedValue() const
		{
			return VS::Gui::Internal::ControlFraction(GetActualValue(), GetMinimum(), GetMaximum());
		}

	private:
		@ GuiProperty(Mark = "value") std::attribute<double> Value = std::attribute<double>(0);
		@ GuiProperty(Mark = "minimum") std::attribute<double> Minimum = std::attribute<double>(0);
		@ GuiProperty(Mark = "maximum") std::attribute<double> Maximum = std::attribute<double>(1);
		@ GuiProperty(Mark = "step") std::attribute<double> Step = std::attribute<double>(0);
		@ GuiProperty(Mark = "page-size") std::attribute<double> PageSize = std::attribute<double>(0.1);
		@ GuiProperty(Style, Mark = "vertical") std::attribute<bool> Vertical = std::attribute<bool>(false);
	};

	@ GuiWidget(Mark = "slider") export class Slider : public VS::Gui::Widget
	{
	public:
		//~ Begin VS::Gui::Widget Interface
		void VsguiSynchronizeControl() override
		{
			if (!VsguiNeedsControlConfiguration()) return;
			bool snapshot = VsguiCanUseControlSnapshot();
			double actual = this.__GetNativeHandle().ConfigureControlAndGetValue(false, snapshot ? VsguiPropertyValueReadValue : GetValue(), snapshot ? VsguiPropertyMinimumReadValue : GetMinimum(), snapshot ? VsguiPropertyMaximumReadValue : GetMaximum(), snapshot ? VsguiPropertyStepReadValue : GetStep(), snapshot ? VsguiPropertyVerticalReadValue : GetVertical(), false, "", "", 0, 0, snapshot ? VsguiPropertyPageSizeReadValue : GetPageSize());
			if (!IsValueBound() && GetValue() != actual)
			{
				SetValue(actual);
			}
			VsguiCommitControlConfiguration();
		}
		void VsguiControlAction(int action, bool checkedValue, double number, string text, double x, double y) override
		{
			if (action == 2)
			{
				if (!IsValueBound())
				{
					SetValue(number);
				}
				ValueChanged.broadcast(this, number);
			}
		}
		//~ End VS::Gui::Widget Interface

		Slider()
		{
			this.__GetNativeHandle().InitializeControl(5);
			VsguiRegisterControlAction();
		}
		@ GuiMethod double GetActualValue() const
		{
			return this.__GetNativeHandle().GetControlValue();
		}
		@ GuiMethod double GetNormalizedValue() const
		{
			return VS::Gui::Internal::ControlFraction(GetActualValue(), GetMinimum(), GetMaximum());
		}
		@ GuiMethod void Focus()
		{
			this.__GetNativeHandle().FocusControl();
		}

	private:
		@ GuiProperty(Mark = "value") std::attribute<double> Value = std::attribute<double>(0);
		@ GuiProperty(Mark = "minimum") std::attribute<double> Minimum = std::attribute<double>(0);
		@ GuiProperty(Mark = "maximum") std::attribute<double> Maximum = std::attribute<double>(1);
		@ GuiProperty(Mark = "step") std::attribute<double> Step = std::attribute<double>(0.01);
		@ GuiProperty(Mark = "page-size") std::attribute<double> PageSize = std::attribute<double>(0.1);
		@ GuiProperty(Style, Mark = "vertical") std::attribute<bool> Vertical = std::attribute<bool>(false);
		@ GuiEvent(Mark = "value-changed") std::multicast_delegate<VS::Gui::Slider, double> ValueChanged = std::multicast_delegate<VS::Gui::Slider, double>();
	};

	@ GuiWidget(Mark = "scroll-bar") export class ScrollBar : public VS::Gui::Widget
	{
	public:
		//~ Begin VS::Gui::Widget Interface
		void VsguiSynchronizeControl() override
		{
			if (!VsguiNeedsControlConfiguration()) return;
			bool snapshot = VsguiCanUseControlSnapshot();
			double actual = this.__GetNativeHandle().ConfigureControlAndGetValue(false, snapshot ? VsguiPropertyValueReadValue : GetValue(), snapshot ? VsguiPropertyMinimumReadValue : GetMinimum(), snapshot ? VsguiPropertyMaximumReadValue : GetMaximum(), snapshot ? VsguiPropertyStepReadValue : GetStep(), snapshot ? VsguiPropertyVerticalReadValue : GetVertical(), false, "", "", 0, 0, snapshot ? VsguiPropertyPageSizeReadValue : GetPageSize());
			if (!IsValueBound() && GetValue() != actual)
			{
				SetValue(actual);
			}
			VsguiCommitControlConfiguration();
		}
		void VsguiControlAction(int action, bool checkedValue, double number, string text, double x, double y) override
		{
			if (action == 2)
			{
				if (!IsValueBound())
				{
					SetValue(number);
				}
				ValueChanged.broadcast(this, number);
			}
		}
		//~ End VS::Gui::Widget Interface

		ScrollBar()
		{
			this.__GetNativeHandle().InitializeControl(6);
			VsguiRegisterControlAction();
		}
		@ GuiMethod double GetActualValue() const
		{
			return this.__GetNativeHandle().GetControlValue();
		}
		@ GuiMethod double GetNormalizedValue() const
		{
			return VS::Gui::Internal::ControlFraction(GetActualValue(), GetMinimum(), GetMaximum());
		}
		@ GuiMethod void Focus()
		{
			this.__GetNativeHandle().FocusControl();
		}

	private:
		@ GuiProperty(Mark = "value") std::attribute<double> Value = std::attribute<double>(0);
		@ GuiProperty(Mark = "minimum") std::attribute<double> Minimum = std::attribute<double>(0);
		@ GuiProperty(Mark = "maximum") std::attribute<double> Maximum = std::attribute<double>(1);
		@ GuiProperty(Mark = "step") std::attribute<double> Step = std::attribute<double>(0.01);
		@ GuiProperty(Mark = "page-size") std::attribute<double> PageSize = std::attribute<double>(0.1);
		@ GuiProperty(Style, Mark = "vertical") std::attribute<bool> Vertical = std::attribute<bool>(false);
		@ GuiEvent(Mark = "value-changed") std::multicast_delegate<VS::Gui::ScrollBar, double> ValueChanged = std::multicast_delegate<VS::Gui::ScrollBar, double>();
	};

	@ GuiWidget(Mark = "scroll-view") export class ScrollView : public VS::Gui::Widget
	{
	public:
		//~ Begin VS::Gui::Widget Interface
		void VsguiSynchronizeControl() override
		{
			VsguiSynchronizeScrolling();
		}
	protected:
		void VsguiSynchronizeScrolling()
		{
			if (!VsguiNeedsControlConfiguration()) return;
			this.__GetNativeHandle().ConfigureControl(false, 0, 0, 1, 0, false, false, "", "", GetScrollX(), GetScrollY(), 0);
			VsguiCommitControlConfiguration();
		}
	public:
		void VsguiControlAction(int action, bool checkedValue, double number, string text, double x, double y) override
		{
			if (action == 5)
			{
				if (!IsScrollXBound())
				{
					SetScrollX(x);
				}
				if (!IsScrollYBound())
				{
					SetScrollY(y);
				}
				Scrolled.broadcast(this, x, y);
			}
		}
		//~ End VS::Gui::Widget Interface

		ScrollView()
		{
			this.__GetNativeHandle().InitializeControl(7);
			VsguiRegisterControlAction();
		}
		@ GuiMethod void ScrollTo(double x, double y)
		{
			SetScrollX(x);
			SetScrollY(y);
		}
		@ GuiMethod double GetActualScrollX() const
		{
			return this.__GetNativeHandle().GetScrollGeometry(0);
		}
		@ GuiMethod double GetActualScrollY() const
		{
			return this.__GetNativeHandle().GetScrollGeometry(1);
		}
		@ GuiMethod double GetScrollWidth() const
		{
			return this.__GetNativeHandle().GetScrollGeometry(2);
		}
		@ GuiMethod double GetScrollHeight() const
		{
			return this.__GetNativeHandle().GetScrollGeometry(3);
		}

	private:
		@ GuiProperty(Mark = "scroll-x") std::attribute<double> ScrollX = std::attribute<double>(0);
		@ GuiProperty(Mark = "scroll-y") std::attribute<double> ScrollY = std::attribute<double>(0);
		@ GuiEvent(Mark = "scrolled") std::multicast_delegate<VS::Gui::ScrollView, double, double> Scrolled = std::multicast_delegate<VS::Gui::ScrollView, double, double>();
	};

	@ GuiWidget(Mark = "text-box") export class TextBox : public VS::Gui::Widget
	{
	public:
		//~ Begin VS::Gui::Widget Interface
		void VsguiSynchronizeControl() override
		{
			if (!VsguiNeedsControlConfiguration()) return;
			this.__GetNativeHandle().ConfigureControl(false, 0, 0, 1, 0, false, GetReadOnly(), GetValue(), GetPlaceholder(), 0, 0, 0);
			string actual = this.__GetNativeHandle().GetControlText();
			if (!IsValueBound() && GetValue() != actual)
			{
				SetValue(actual);
			}
			VsguiCommitControlConfiguration();
		}
		void VsguiControlAction(int action, bool checkedValue, double number, string text, double x, double y) override
		{
			if (action == 3)
			{
				if (!IsValueBound())
				{
					SetValue(text);
				}
				TextChanged.broadcast(this, text);
			}
			else if (action == 4)
			{
				Submitted.broadcast(this, text);
			}
		}
		//~ End VS::Gui::Widget Interface

		TextBox()
		{
			this.__GetNativeHandle().InitializeControl(8);
			VsguiRegisterControlAction();
		}
		@ GuiMethod string GetActualValue() const
		{
			return this.__GetNativeHandle().GetControlText();
		}
		@ GuiMethod void Focus()
		{
			this.__GetNativeHandle().FocusControl();
		}
		@ GuiMethod void Select(int64 start, int64 end)
		{
			this.__GetNativeHandle().SetTextSelection(start, end);
		}

	private:
		@ GuiProperty(Mark = "value") std::attribute<string> Value = std::attribute<string>("");
		@ GuiProperty(Mark = "read-only") std::attribute<bool> ReadOnly = std::attribute<bool>(false);
		@ GuiProperty(Mark = "placeholder") std::attribute<string> Placeholder = std::attribute<string>("");
		@ GuiEvent(Mark = "text-changed") std::multicast_delegate<VS::Gui::TextBox, string> TextChanged = std::multicast_delegate<VS::Gui::TextBox, string>();
		@ GuiEvent(Mark = "submitted") std::multicast_delegate<VS::Gui::TextBox, string> Submitted = std::multicast_delegate<VS::Gui::TextBox, string>();
	};

	@ GuiWidget(Mark = "text-area") export class TextArea : public VS::Gui::TextBox
	{
	public:
		TextArea()
		{
			this.__GetNativeHandle().InitializeControl(9);
			VsguiRegisterControlAction();
		}
	};

	@ GuiWidget(Mark = "grid") export class Grid : public VS::Gui::Widget
	{
	public:
		//~ Begin VS::Gui::Widget Interface
		void VsguiSynchronizeControl() override
		{
			if (!VsguiNeedsControlConfiguration()) return;
			this.__GetNativeHandle().ConfigureLayout(GetColumns(), 6);
			VsguiCommitControlConfiguration();
		}
		//~ End VS::Gui::Widget Interface
		Grid()
		{
			this.__GetNativeHandle().InitializeControl(10);
			VsguiRegisterControlAction();
		}
	private:
		@ GuiProperty(Style, Mark = "columns") std::attribute<int64> Columns = std::attribute<int64>(2);
	};

	@ GuiWidget(Mark = "wrap") export class Wrap : public VS::Gui::Widget
	{
	public:
		Wrap()
		{
			this.__GetNativeHandle().InitializeControl(11);
			VsguiRegisterControlAction();
		}
	};

	@ GuiWidget(Mark = "splitter") export class Splitter : public VS::Gui::Widget
	{
	public:
		//~ Begin VS::Gui::Widget Interface
		void VsguiSynchronizeControl() override
		{
			if (!VsguiNeedsControlConfiguration()) return;
			this.__GetNativeHandle().ConfigureControl(false, GetValue(), GetMinimum(), GetMaximum(), GetStep(), GetVertical(), GetReadOnly(), "", "", 0, 0, 0);
			this.__GetNativeHandle().ConfigureLayout(2, GetDividerSize());
			double actual = GetActualValue();
			if (!IsValueBound() && GetValue() != actual)
			{
				SetValue(actual);
			}
			VsguiCommitControlConfiguration();
		}
		void VsguiControlAction(int action, bool checkedValue, double number, string text, double x, double y) override
		{
			if (action == 2)
			{
				if (!IsValueBound())
				{
					SetValue(number);
				}
				ValueChanged.broadcast(this, number);
			}
		}
		//~ End VS::Gui::Widget Interface
		Splitter()
		{
			this.__GetNativeHandle().InitializeControl(12);
			VsguiRegisterControlAction();
		}
		@ GuiMethod double GetActualValue() const
		{
			return this.__GetNativeHandle().GetControlValue();
		}
		@ GuiMethod void Focus()
		{
			this.__GetNativeHandle().FocusControl();
		}
	private:
		@ GuiProperty(Mark = "value") std::attribute<double> Value = std::attribute<double>(0.5);
		@ GuiProperty(Mark = "minimum") std::attribute<double> Minimum = std::attribute<double>(0);
		@ GuiProperty(Mark = "maximum") std::attribute<double> Maximum = std::attribute<double>(1);
		@ GuiProperty(Mark = "step") std::attribute<double> Step = std::attribute<double>(0.01);
		@ GuiProperty(Mark = "read-only") std::attribute<bool> ReadOnly = std::attribute<bool>(false);
		@ GuiProperty(Style, Mark = "vertical") std::attribute<bool> Vertical = std::attribute<bool>(false);
		@ GuiProperty(Style, Mark = "divider-size") std::attribute<double> DividerSize = std::attribute<double>(6);
		@ GuiEvent(Mark = "value-changed") std::multicast_delegate<VS::Gui::Splitter, double> ValueChanged = std::multicast_delegate<VS::Gui::Splitter, double>();
	};

	@ GuiWidget(Mark = "radio-group") export class RadioGroup : public VS::Gui::Widget
	{
	public:
		//~ Begin VS::Gui::Widget Interface
		void VsguiSynchronizeControl() override
		{
			if (!VsguiNeedsControlConfiguration()) return;
			this.__GetNativeHandle().ConfigureControl(false, 0, 0, 1, 0, false, false, GetSelectedKey(), "", 0, 0, 0);
			VsguiCommitControlConfiguration();
		}
		void VsguiControlAction(int action, bool checkedValue, double number, string text, double x, double y) override
		{
			if (action == 6)
			{
				if (!IsSelectedKeyBound())
				{
					SetSelectedKey(text);
				}
				SelectionChanged.broadcast(this, text);
			}
		}
		//~ End VS::Gui::Widget Interface
		RadioGroup()
		{
			this.__GetNativeHandle().InitializeControl(14);
			VsguiRegisterControlAction();
		}
	private:
		@ GuiProperty(Mark = "selected-key") std::attribute<string> SelectedKey = std::attribute<string>("");
		@ GuiEvent(Mark = "selection-changed") std::multicast_delegate<VS::Gui::RadioGroup, string> SelectionChanged = std::multicast_delegate<VS::Gui::RadioGroup, string>();
	};

	@ GuiWidget(Mark = "radio-button") export class RadioButton : public VS::Gui::Widget
	{
	public:
		//~ Begin VS::Gui::Widget Interface
		void VsguiSynchronizeControl() override
		{
			if (!VsguiNeedsControlConfiguration()) return;
			this.__GetNativeHandle().ConfigureChoice(GetKey());
			VsguiCommitControlConfiguration();
		}
		void VsguiControlAction(int action, bool checkedValue, double number, string text, double x, double y) override
		{
			if (action == 1 && checkedValue)
			{
				Selected.broadcast(this);
			}
		}
		//~ End VS::Gui::Widget Interface
		RadioButton()
		{
			this.__GetNativeHandle().InitializeControl(13);
			VsguiRegisterControlAction();
		}
		@ GuiMethod bool IsChecked() const
		{
			return this.__GetNativeHandle().GetControlChecked();
		}
		@ GuiMethod void Select()
		{
			this.__GetNativeHandle().Activate();
		}
		@ GuiMethod void Focus()
		{
			this.__GetNativeHandle().FocusControl();
		}
	private:
		@ GuiProperty(Mark = "key") std::attribute<string> Key = std::attribute<string>("");
		@ GuiEvent(Mark = "selected") std::multicast_delegate<VS::Gui::RadioButton> Selected = std::multicast_delegate<VS::Gui::RadioButton>();
	};

	@ GuiWidget(Mark = "number-box") export class NumberBox : public VS::Gui::Widget
	{
	public:
		//~ Begin VS::Gui::Widget Interface
		void VsguiSynchronizeControl() override
		{
			if (!VsguiNeedsControlConfiguration()) return;
			this.__GetNativeHandle().ConfigureControl(false, GetValue(), GetMinimum(), GetMaximum(), GetStep(), false, GetReadOnly(), "", GetPlaceholder(), 0, 0, 0);
			double actual = GetActualValue();
			if (!IsValueBound() && GetValue() != actual)
			{
				SetValue(actual);
			}
			VsguiCommitControlConfiguration();
		}
		void VsguiControlAction(int action, bool checkedValue, double number, string text, double x, double y) override
		{
			if (action == 2)
			{
				if (!IsValueBound())
				{
					SetValue(number);
				}
				ValueChanged.broadcast(this, number);
			}
			else if (action == 4)
			{
				Submitted.broadcast(this, number);
			}
		}
		//~ End VS::Gui::Widget Interface
		NumberBox()
		{
			this.__GetNativeHandle().InitializeControl(15);
			VsguiRegisterControlAction();
		}
		@ GuiMethod double GetActualValue() const
		{
			return this.__GetNativeHandle().GetControlValue();
		}
		@ GuiMethod void Focus()
		{
			this.__GetNativeHandle().FocusControl();
		}
		@ GuiMethod void Select(int64 start, int64 end)
		{
			this.__GetNativeHandle().SetTextSelection(start, end);
		}
	private:
		@ GuiProperty(Mark = "value") std::attribute<double> Value = std::attribute<double>(0);
		@ GuiProperty(Mark = "minimum") std::attribute<double> Minimum = std::attribute<double>(0);
		@ GuiProperty(Mark = "maximum") std::attribute<double> Maximum = std::attribute<double>(100);
		@ GuiProperty(Mark = "step") std::attribute<double> Step = std::attribute<double>(1);
		@ GuiProperty(Mark = "read-only") std::attribute<bool> ReadOnly = std::attribute<bool>(false);
		@ GuiProperty(Mark = "placeholder") std::attribute<string> Placeholder = std::attribute<string>("");
		@ GuiEvent(Mark = "value-changed") std::multicast_delegate<VS::Gui::NumberBox, double> ValueChanged = std::multicast_delegate<VS::Gui::NumberBox, double>();
		@ GuiEvent(Mark = "submitted") std::multicast_delegate<VS::Gui::NumberBox, double> Submitted = std::multicast_delegate<VS::Gui::NumberBox, double>();
	};

	@ GuiWidget(Mark = "password-box") export class PasswordBox : public VS::Gui::TextBox
	{
	public:
		PasswordBox()
		{
			this.__GetNativeHandle().InitializeControl(16);
			VsguiRegisterControlAction();
		}
	};

	@ GuiWidget(Mark = "popup") export class Popup : public VS::Gui::Widget
	{
	public:
		//~ Begin VS::Gui::Widget Interface
		void VsguiSynchronizeControl() override
		{
			if (!VsguiNeedsControlConfiguration()) return;
			bool previous = IsActuallyOpen();
			this.__GetNativeHandle().ConfigureFloating(GetOpen(), GetDismissOnOutside(), false, GetOffsetX(), GetOffsetY(), 0);
			this.__GetNativeHandle().ConfigurePopup(GetModal(), GetDismissOnEscape(), GetMatchAnchorWidth(), GetPlacement());
			if (previous != GetOpen())
			{
				VsguiOnFloatingSynchronized(GetOpen());
			}
			VsguiCommitControlConfiguration();
		}
		void VsguiControlAction(int action, bool checkedValue, double number, string text, double x, double y) override
		{
			if (action == 7)
			{
				dismissalReason = text;
				if (!IsOpenBound())
				{
					SetOpen(checkedValue);
				}
				VsguiNotifyFloatingChange(checkedValue);
			}
		}
		//~ End VS::Gui::Widget Interface
		Popup()
		{
			this.__GetNativeHandle().InitializeControl(17);
			VsguiRegisterControlAction();
		}
		@ GuiMethod bool IsActuallyOpen() const
		{
			return this.__GetNativeHandle().GetControlOpen();
		}
		@ GuiMethod string GetDismissReason() const
		{
			return dismissalReason;
		}
		@ GuiMethod void RequestOpen(bool value)
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			bool previous = IsActuallyOpen();
			if (!IsOpenBound())
			{
				SetOpen(value);
			}
			if (previous == value) return;
			dismissalReason = "";
			this.__GetNativeHandle().SetFloatingOpen(value);
			VsguiNotifyFloatingChange(value);
		}
	protected:
		void VsguiBroadcastOpenChanged(bool value)
		{
			OpenChanged.broadcast(this, value);
		}
		virtual void VsguiNotifyFloatingChange(bool value)
		{
			VsguiBroadcastOpenChanged(value);
			if (!IsDisposed())
			{
				OnOpenStateChanged(value);
			}
		}
		virtual void OnOpenStateChanged(bool value)
		{
		}
		virtual void VsguiOnFloatingSynchronized(bool value)
		{
		}
	private:
		string dismissalReason;
		@ GuiProperty(Mark = "open") std::attribute<bool> Open = std::attribute<bool>(false);
		@ GuiProperty(Mark = "dismiss-on-outside") std::attribute<bool> DismissOnOutside = std::attribute<bool>(true);
		@ GuiProperty(Mark = "dismiss-on-escape") std::attribute<bool> DismissOnEscape = std::attribute<bool>(true);
		@ GuiProperty(Mark = "modal") std::attribute<bool> Modal = std::attribute<bool>(false);
		@ GuiProperty(Mark = "match-anchor-width") std::attribute<bool> MatchAnchorWidth = std::attribute<bool>(false);
		@ GuiProperty(Mark = "placement") std::attribute<string> Placement = std::attribute<string>("below");
		@ GuiProperty(Style, Mark = "offset-x") std::attribute<double> OffsetX = std::attribute<double>(0);
		@ GuiProperty(Style, Mark = "offset-y") std::attribute<double> OffsetY = std::attribute<double>(0);
		@ GuiEvent(Mark = "open-changed") std::multicast_delegate<VS::Gui::Popup, bool> OpenChanged = std::multicast_delegate<VS::Gui::Popup, bool>();
	};

	@ GuiWidget(Mark = "tool-tip") export class ToolTip : public VS::Gui::Popup
	{
	public:
		//~ Begin VS::Gui::Widget Interface
		void VsguiSynchronizeControl() override
		{
			if (!VsguiNeedsControlConfiguration()) return;
			assert(!GetModal(), "ToolTip cannot be modal");
			this.__GetNativeHandle().ConfigureFloating(GetOpen(), false, GetAutoOpen(), GetOffsetX(), GetOffsetY(), GetDelay());
			this.__GetNativeHandle().ConfigurePopup(false, false, GetMatchAnchorWidth(), GetPlacement());
			VsguiCommitControlConfiguration();
		}
		//~ End VS::Gui::Widget Interface
		ToolTip()
		{
			this.__GetNativeHandle().InitializeControl(18);
			VsguiRegisterControlAction();
		}
	private:
		@ GuiProperty(Mark = "auto-open") std::attribute<bool> AutoOpen = std::attribute<bool>(true);
		@ GuiProperty(Style, Mark = "delay") std::attribute<double> Delay = std::attribute<double>(0.5);
	};

} // namespace VS::Gui
