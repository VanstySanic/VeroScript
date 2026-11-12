// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiControls.h"
#include "VeroScriptGuiPaint.h"

namespace VeroScript::Gui
{
	double ControlThumbSize(const FControlOptions &Options, double Extent) noexcept;
	bool PaintControl(const FControlOptions &Options, const Graphics::FColor &Color, double LineHeight, FPaintContext &Canvas,
					  std::string &Error);
} // namespace VeroScript::Gui
