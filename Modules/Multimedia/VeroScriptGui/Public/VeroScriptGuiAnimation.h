// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiTypes.h"

namespace VeroScript::Gui
{
	enum class EEasing : std::uint8_t
	{
		Linear,
		EaseIn,
		EaseOut,
		EaseInOut
	};
	struct FAnimationOptions
	{
		double Duration = 0.25, Delay = 0, MaxStep = 0.25;
		EEasing Easing = EEasing::Linear;
		// Otherwise completion removes the presentation layer and reveals the current base value.
		bool bHoldEnd = false;
	};

	bool InterpolateProperty(const FPropertyValue& From, const FPropertyValue& To, double Progress,
							 FPropertyValue& Value, std::string& Error);

	class FAnimationTrack final
	{
	public:
		static std::optional<FAnimationTrack> Create(EProperty Property, FPropertyValue From, FPropertyValue To,
													 const FAnimationOptions& Options, std::string& Error);
		bool Sample(double ElapsedSeconds, std::optional<FPropertyValue>& Presentation, std::string& Error);
		void Pause() noexcept;
		bool IsFinished() const noexcept;

	private:
		FPropertyValue From, To;
		FAnimationOptions Options;
		std::optional<double> LastTime;
		double Age = 0;
		bool bFinished = false;
	};
} // namespace VeroScript::Gui
