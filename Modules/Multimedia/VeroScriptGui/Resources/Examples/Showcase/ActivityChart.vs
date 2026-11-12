// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace Showcase
{
	@ GuiWidget(Mark = "activity-chart") export class ActivityChart : public VS::Gui::Widget
	{
	protected:
		//~ Begin VS::Gui::Widget Interface
		void OnPaint(VS::Gui::PaintContext canvas) override
		{
			double width = canvas.GetWidth();
			double height = canvas.GetHeight();
			if (width <= 0 || height <= 0)
			{
				return;
			}

			// Read the synchronized attribute; paint never mutates the tree or model.
			double scale = GetLevel() / 100;
			if (scale < 0)
			{
				scale = 0;
			}
			if (scale > 1)
			{
				scale = 1;
			}
			canvas.PushClip(0, 0, width, height);
			for (int row = 1; row < 4; row++)
			{
				double y = height * row / 4;
				canvas.DrawLine(0, y, width, y, 1, VS::Gui::Color(0.15, 0.22, 0.31, 0.5));
			}
			double cell = width / len(samples);
			for (int64 index = 0; index < len(samples); index++)
			{
				double extent = height * samples[index] * (0.15 + scale * 0.85);
				canvas.FillRect(cell * index + cell * 0.15, height - extent, cell * 0.7, extent, VS::Gui::Color(0.04, 0.52 + index * 0.025, 0.8, 1));
			}
			canvas.PopClip();
		}
		//~ End VS::Gui::Widget Interface

	private:
		@ GuiProperty(Mark = "level") std::attribute<double> Level = std::attribute<double>(65);
		array<double> samples = {0.28, 0.46, 0.36, 0.64, 0.52, 0.82, 0.68, 0.94, 0.74, 0.86, 0.62, 0.91};
	};
}
