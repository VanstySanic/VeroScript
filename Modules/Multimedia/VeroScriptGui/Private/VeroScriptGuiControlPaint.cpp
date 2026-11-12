// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiControlPaint.h"
#include <algorithm>
#include <cmath>

namespace VeroScript::Gui
{
	double ControlThumbSize(const FControlOptions &O, double Extent) noexcept
	{
		if (O.Kind != EControlKind::ScrollBar)
		{
			return std::min(10.0, Extent);
		}
		const double Range = O.Maximum - O.Minimum;
		// Divide before adding so large finite ranges and page sizes cannot overflow.
		const double Fraction = O.PageSize > Range ? 1 / (1 + Range / O.PageSize)
												   : (Range > 0 ? (O.PageSize / Range) / (1 + O.PageSize / Range) : 1);
		return std::min(Extent, std::max(8.0, Extent * Fraction));
	}

	bool PaintControl(const FControlOptions &O, const Graphics::FColor &Color, double LineHeight, FPaintContext &Canvas,
					  std::string &Error)
	{
		const auto B = Canvas.GetBounds();
		const auto W = B.Width, H = B.Height;
		const double Header = std::min(H, LineHeight);
		if (O.bSelected && !Canvas.FillRect({0, 0, W, O.bItemIndicator ? Header : H}, O.SelectionColor, Error))
		{
			return false;
		}
		if (O.bExpandable)
		{
			if (Header <= 0) return true;
			const double Y = Header * .5, Size = std::min(5.0, Header * .25);
			return O.bExpanded
				? Canvas.DrawLine(-11 - Size, Y - Size * .5, -11, Y + Size * .5, 1.5, Color, Error) &&
				  Canvas.DrawLine(-11, Y + Size * .5, -11 + Size, Y - Size * .5, 1.5, Color, Error)
				: Canvas.DrawLine(-13, Y - Size, -13 + Size, Y, 1.5, Color, Error) &&
				  Canvas.DrawLine(-13 + Size, Y, -13, Y + Size, 1.5, Color, Error);
		}
		if (O.Kind == EControlKind::Splitter)
		{
			const auto Size = std::min(O.DividerSize, O.bVertical ? H : W);
			const auto At = std::max(0.0, (O.bVertical ? H : W) - Size) * O.Value;
			return Canvas.FillRect(O.bVertical ? FRect{0, At, W, Size} : FRect{At, 0, Size, H}, Color, Error);
		}
		if (O.Kind == EControlKind::RadioButton)
		{
			const double Radius = std::max(0.0, std::min(7.0, H * .5));
			if (Radius == 0)
			{
				return true;
			}
			for (int I = 0; I < 24; ++I)
			{
				const double A = I * 6.283185307179586 / 24, C = (I + 1) * 6.283185307179586 / 24;
				if (!Canvas.DrawLine(-13 + Radius * std::cos(A), Radius + Radius * std::sin(A),
									 -13 + Radius * std::cos(C), Radius + Radius * std::sin(C), O.bChecked ? 3 : 1,
									 Color, Error))
				{
					return false;
				}
			}
			return true;
		}
		if (O.Kind == EControlKind::ToggleSwitch)
		{
			const double Width = 32, Height = std::min(16.0, H);
			auto Track = Color;
			Track.A *= .3f;
			return Canvas.FillRect({-40, 0, Width, Height}, Track, Error) &&
				   Canvas.FillRect({O.bChecked ? -22.0 : -38.0, 2, 12, std::max(0.0, Height - 4)}, Color, Error);
		}
		if (O.Kind == EControlKind::CheckBox)
		{
			const double Size = std::max(0.0, std::min(14.0, H));
			if (Size == 0)
			{
				return true;
			}
			if (!Canvas.DrawLine(-20, 0, Size - 20, 0, 1, Color, Error) ||
				!Canvas.DrawLine(Size - 20, 0, Size - 20, Size, 1, Color, Error) ||
				!Canvas.DrawLine(Size - 20, Size, -20, Size, 1, Color, Error) ||
				!Canvas.DrawLine(-20, Size, -20, 0, 1, Color, Error))
			{
				return false;
			}
			return !O.bChecked ||
				   Canvas.FillRect({-17, 3, std::max(0.0, Size - 6), std::max(0.0, Size - 6)}, Color, Error);
		}
		if (O.Kind == EControlKind::ProgressBar || O.Kind == EControlKind::Slider || O.Kind == EControlKind::ScrollBar)
		{
			const auto Ratio = O.Maximum > O.Minimum ? (O.Value - O.Minimum) / (O.Maximum - O.Minimum) : 0;
			if (O.Kind == EControlKind::ProgressBar)
			{
				return Canvas.FillRect(
					O.bVertical ? FRect{0, H * (1 - Ratio), W, H * Ratio} : FRect{0, 0, W * Ratio, H}, Color, Error);
			}
			const double Size = O.bVertical ? H : W;
			const double Thumb = ControlThumbSize(O, Size);
			const double DisplayRatio = O.bVertical && O.Kind == EControlKind::Slider ? 1 - Ratio : Ratio;
			const double At = DisplayRatio * std::max(0.0, Size - Thumb);
			return Canvas.FillRect(O.bVertical ? FRect{0, At, W, Thumb} : FRect{At, 0, Thumb, H}, Color, Error);
		}
		return true;
	}
} // namespace VeroScript::Gui
