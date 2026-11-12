// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

export import "Controls.vs";
export import "Widget.vs";
export import "Binding.vs";
import "delegate.vs";

namespace VS::Gui
{
	// Modal portal in the existing application loop, without a nested blocking event loop.
	@ GuiWidget(Mark = "dialog") export class Dialog : public VS::Gui::Popup
	{
	public:
		Dialog()
		{
			SetModal(true);
			SetDismissOnOutside(false);
			SetPlacement("center");
			SetFocusable(true);
			SetWidth(Length(400));
			SetBackground(Color(0.10, 0.11, 0.14, 1));
		}
		@ GuiMethod void Show()
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			result = "";
			RequestOpen(true);
		}
		@ GuiMethod void Close(string value = "")
		{
			assert(VS::Gui::PropertyEvaluation::Depth == 0, "GUI mutation is not allowed in a getter");
			result = value;
			RequestOpen(false);
		}
		@ GuiMethod string GetResult() const
		{
			return result;
		}
	protected:
		void VsguiOnFloatingSynchronized(bool value) override
		{
			if (value)
			{
				result = "";
			}
		}
		void VsguiNotifyFloatingChange(bool value) override
		{
			if (value)
			{
				result = "";
			}
			string completedResult = result;
			VsguiBroadcastOpenChanged(value);
			if (IsDisposed()) return;
			OnOpenStateChanged(value);
			if (!value && !IsDisposed())
			{
				Closed.broadcast(this, completedResult);
			}
		}
	private:
		string result;
		@ GuiEvent(Mark = "closed") std::multicast_delegate<VS::Gui::Dialog, string> Closed = std::multicast_delegate<VS::Gui::Dialog, string>();
	};
}
