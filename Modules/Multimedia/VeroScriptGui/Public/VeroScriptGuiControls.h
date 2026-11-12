// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiTypes.h"
#include "VeroScriptPlatformApi.h"

namespace VeroScript::Gui
{
	enum class EControlKind : std::uint8_t
	{
		None,
		Button,
		CheckBox,
		ToggleSwitch,
		ProgressBar,
		Slider,
		ScrollBar,
		ScrollView,
		TextBox,
		TextArea,
		Grid,
		Wrap,
		Splitter,
		RadioButton,
		RadioGroup,
		NumberBox,
		PasswordBox,
		Popup,
		ToolTip
	};

	enum class EControlAction : std::uint8_t
	{
		Clicked,
		CheckedChanged,
		ValueChanged,
		TextChanged,
		Submitted,
		Scrolled,
		SelectionChanged,
		OpenChanged
	};
	/** Values cross the backend boundary as snapshots; callbacks run only on the GUI owner thread. */
	struct FControlOptions
	{
		enum class EPlacement : std::uint8_t
		{
			Below,
			Right,
			Center
		};

		bool operator==(const FControlOptions &Other) const noexcept;
		EControlKind Kind = EControlKind::None;
		bool bChecked = false, bVertical = false, bReadOnly = false;
		double Value = 0, Minimum = 0, Maximum = 1, Step = 0.01;
		double ScrollX = 0, ScrollY = 0, PageSize = 0.1;
		/** Editable text, or the selected key for RadioGroup. */
		std::string Text, Placeholder;
		/** Grid owns child widths; Splitter owns both pane dimensions. */
		std::int64_t Columns = 2;
		double DividerSize = 6;
		std::string ChoiceKey;
		/** Floating children anchor to their logical parent while escaping its render clip. */
		bool bOpen = false, bDismissOnOutside = true, bAutoOpen = true;
		double OffsetX = 0, OffsetY = 0, Delay = 0.5;
		/** Modal input is enforced by Gui; providers keep its portal above ordinary content. */
		bool bModal = false, bDismissOnEscape = true, bMatchAnchorWidth = false;
		EPlacement Placement = EPlacement::Below;
		/** Collection rows share button input and native painting. */
		bool bSelected = false, bExpandable = false, bExpanded = true, bItemIndicator = false;
		Graphics::FColor SelectionColor{.08f, .22f, .45f, .85f};
		/** Assigned by Context in backend snapshots; declaration order remains the non-modal default. */
		std::uint64_t ModalOrder = 0;
	};

	struct FControlChange
	{
		EControlAction Action = EControlAction::Clicked;
		bool bChecked = false;
		double Value = 0, X = 0, Y = 0;
		std::string Text;
	};
	using FControlCallback = std::function<bool(const FControlChange &, std::string &)>;

	/** Optional host integration for an embedded context. Application installs its owned window services. */
	struct FTextInputServices
	{
		std::function<bool(std::string &, std::string &)> ReadClipboard;
		std::function<bool(const std::string &, std::string &)> WriteClipboard;
		// Rectangle and horizontal cursor offset use GUI logical pixels, not character indices.
		std::function<bool(bool, const FRect &, std::int64_t, std::string &)> SetInputArea;
	};
	struct FTextEditResult
	{
		std::string Text;
		std::int64_t SelectionStart = 0, SelectionEnd = 0;
		bool bChanged = false, bSubmitted = false, bHandled = false;
	};

	bool ValidateControlOptions(const FControlOptions &Value, std::string &Error);
	double NormalizeControlValue(const FControlOptions &Options, double Value) noexcept;
	bool IsTextControl(EControlKind Kind) noexcept;
	bool IsFloatingControl(EControlKind Kind) noexcept;
	bool ParseControlNumber(const std::string &Text, double &Value) noexcept;
	std::string FormatControlNumber(double Value);
} // namespace VeroScript::Gui
