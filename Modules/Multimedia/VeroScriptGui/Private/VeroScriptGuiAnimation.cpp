// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiAnimation.h"
#include <algorithm>
#include <cmath>

namespace VeroScript::Gui
{
	bool InterpolateProperty(const FPropertyValue& From, const FPropertyValue& To, double Progress,
	    FPropertyValue& Value, std::string& Error)
	{
		Error.clear();
		if (From.index() != To.index() || !std::isfinite(Progress) || Progress < 0 || Progress > 1)
		{
			Error = "gui.invalid_animation_values";
			return false;
		}
		const auto Mix = [&](double A, double B)
		{
			return A * (1 - Progress) + B * Progress;
		};
		if (const auto* Number = std::get_if<double>(&From))
		{
			Value = Mix(*Number, std::get<double>(To));
		}
		else if (const auto* Length = std::get_if<FLength>(&From))
		{
			const auto& B = std::get<FLength>(To);
			if (Length->Unit != B.Unit || Length->Unit == ELengthUnit::Auto)
			{
				Error = "gui.animation_length_units";
				return false;
			}
			Value = FLength{Length->Unit, Mix(Length->Value, B.Value)};
		}
		else if (const auto* Insets = std::get_if<FInsets>(&From))
		{
			const auto& B = std::get<FInsets>(To);
			Value = FInsets{Mix(Insets->Top, B.Top), Mix(Insets->Right, B.Right), Mix(Insets->Bottom, B.Bottom),
			    Mix(Insets->Left, B.Left)};
		}
		else if (const auto* Color = std::get_if<Graphics::FColor>(&From))
		{
			const auto& B = std::get<Graphics::FColor>(To);
			Value = Graphics::FColor{static_cast<float>(Mix(Color->R, B.R)), static_cast<float>(Mix(Color->G, B.G)),
			    static_cast<float>(Mix(Color->B, B.B)), static_cast<float>(Mix(Color->A, B.A))};
		}
		else
		{
			Error = "gui.property_not_animatable";
			return false;
		}
		return true;
	}

	std::optional<FAnimationTrack> FAnimationTrack::Create(EProperty Property, FPropertyValue From, FPropertyValue To,
	    const FAnimationOptions& Options, std::string& Error)
	{
		if (!ValidateProperty(Property, From, Error) || !ValidateProperty(Property, To, Error))
		{
			return {};
		}
		if (!std::isfinite(Options.Duration) || Options.Duration <= 0 || !std::isfinite(Options.Delay) ||
		    Options.Delay < 0 || !std::isfinite(Options.Delay + Options.Duration) || !std::isfinite(Options.MaxStep) ||
		    Options.MaxStep <= 0 || Options.Easing > EEasing::EaseInOut)
		{
			Error = "gui.invalid_animation_options";
			return {};
		}
		FPropertyValue Checked;
		if (!InterpolateProperty(From, To, 0, Checked, Error))
		{
			return {};
		}
		FAnimationTrack Result;
		Result.From = std::move(From);
		Result.To = std::move(To);
		Result.Options = Options;
		return Result;
	}

	bool FAnimationTrack::Sample(double ElapsedSeconds, std::optional<FPropertyValue>& Presentation, std::string& Error)
	{
		Error.clear();
		if (!std::isfinite(ElapsedSeconds) || ElapsedSeconds < 0 || (LastTime && ElapsedSeconds < *LastTime))
		{
			Error = "gui.invalid_time";
			return false;
		}
		if (LastTime && !bFinished)
		{
			// A foreground resume advances by a bounded step. Detached widgets pause.
			Age =
			    std::min(Options.Delay + Options.Duration, Age + std::min(ElapsedSeconds - *LastTime, Options.MaxStep));
		}
		LastTime = ElapsedSeconds;
		bFinished = Age >= Options.Delay + Options.Duration;
		if (bFinished && !Options.bHoldEnd)
		{
			Presentation.reset();
			return true;
		}
		double T = std::clamp((Age - Options.Delay) / Options.Duration, 0.0, 1.0);
		if (Options.Easing == EEasing::EaseIn)
		{
			T *= T;
		}
		else if (Options.Easing == EEasing::EaseOut)
		{
			T = 1 - (1 - T) * (1 - T);
		}
		else if (Options.Easing == EEasing::EaseInOut)
		{
			T = T * T * (3 - 2 * T);
		}
		FPropertyValue Value;
		if (!InterpolateProperty(From, To, T, Value, Error))
		{
			return false;
		}
		Presentation = std::move(Value);
		return true;
	}

	void FAnimationTrack::Pause() noexcept
	{
		LastTime.reset();
	}
	bool FAnimationTrack::IsFinished() const noexcept
	{
		return bFinished;
	}
} // namespace VeroScript::Gui
