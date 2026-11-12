// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

@ GuiWidget(Mark = "drawing") export class Drawing : public VS::Gui::Widget
{
protected:
	void OnPaint(VS::Gui::PaintContext canvas) override
	{
		canvas.PushClip(0, 0, canvas.GetWidth(), canvas.GetHeight());
		canvas.FillRect(8, 8, 72, 28, VS::Gui::Color(0.1, 0.5, 1, 1));
		canvas.DrawLine(8, 48, canvas.GetWidth() - 8, 48, 2, VS::Gui::Color(1, 1, 1, 1));
		canvas.PopClip();
	}
};
