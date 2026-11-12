// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptFont.h"

namespace VeroScript::Font
{
	struct FTextRun
	{
		std::int64_t ByteOffset = 0, ByteLength = 0;
		std::uint8_t Level = 0;
		std::string Script;
	};
	inline constexpr const char *TextLayoutServiceId = "text-layout";
	inline constexpr const char *TextLayoutContractId = "veroscript.text-layout";
	inline constexpr const char *TextLayoutContractVersion = "0.1.0";

	// Immutable backend analysis of owned logical UTF-8. No third-party types cross this boundary.
	class ITextLayoutAnalysis
	{
	public:
		virtual ~ITextLayoutAnalysis() = default;
		virtual const std::string &GetText() const noexcept = 0;
		virtual EDirection GetParagraphDirection(std::int64_t ByteOffset) const noexcept = 0;
		// Resolve the complete soft-wrapped line within its original paragraph, including L1/L2.
		// Runs cover the requested range, in visual order, with absolute logical byte positions.
		virtual bool GetVisualRuns(std::int64_t ByteOffset, std::int64_t ByteLength,
								   std::vector<FTextRun> &Result, std::string &Error) const = 0;
	};
	class ITextLayoutService
	{
	public:
		virtual ~ITextLayoutService() = default;
		virtual const char *GetBackendId() const noexcept = 0;
		// Service configuration is immutable. Use a new instance when configuration changes.
		// Reject invalid UTF-8/direction (ValidateLayoutText); own the unmodified logical text.
		virtual std::shared_ptr<const ITextLayoutAnalysis> Analyze(std::string Text, EDirection BaseDirection,
																	std::string &Error) const = 0;
	};
	using FTextLayoutServiceFactory =
		std::function<std::shared_ptr<ITextLayoutService>(const std::string &, std::string &)>;
	// Also usable by providers when Analyze is called directly, outside the facade.
	bool ValidateLayoutText(std::string_view Text, EDirection BaseDirection, std::string &Error);
	struct FTextCaretStop
	{
		std::int64_t ByteOffset = 0;
		// Logical pen endpoint; negative spacing can place it inside a cluster's visual envelope.
		double X = 0;
		// Distinguishes the two visual locations of a logical boundary between opposite-direction runs.
		bool bTrailing = false;
	};
	struct FVisualCluster
	{
		std::int64_t ByteOffset = 0, ByteLength = 0;
		double Left = 0, Right = 0;
		std::uint8_t Level = 0;
	};
	struct FTextLine
	{
		// Glyphs are in physical left-to-right visual order. Clusters retain original UTF-8 byte offsets.
		FResolvedText Text;
		double Width = 0;
		std::vector<FTextCaretStop> Carets;
		std::vector<FVisualCluster> Clusters;
	};

	// Immutable paragraph analysis. Keep the original logical text; resolve visual order after wrapping.
	// No platform or font-backend types are exposed by this interface.
	class FTextLayout : public std::enable_shared_from_this<FTextLayout>
	{
	public:
		static std::shared_ptr<const FTextLayout> Create(const std::shared_ptr<ITextLayoutService> &Service,
													  std::string Text, EDirection BaseDirection,
													  std::string &Error);
		~FTextLayout();
		FTextLayout(const FTextLayout &) = delete;
		FTextLayout &operator=(const FTextLayout &) = delete;
		const std::string &GetText() const noexcept;
		bool Owns(std::string_view Text) const noexcept;
		// Include this instance identity in external cache keys; backend ID alone is insufficient.
		const std::shared_ptr<ITextLayoutService> &GetService() const noexcept;
		EDirection GetParagraphDirection(std::int64_t ByteOffset) const noexcept;
		// A line must be within one original paragraph and start/end at UTF-8 boundaries.
		// Returned script runs have already undergone the line-specific UAX #9 L1/L2 reordering.
		bool GetVisualRuns(std::int64_t ByteOffset, std::int64_t ByteLength, std::vector<FTextRun> &Result,
						   std::string &Error) const;
		// Shapes each script/direction run with its original surrounding context, then builds visual carets.
		// Offsets are absolute in GetText(). Options.Script is a fallback for common/inherited-only runs.
		// LetterSpacing and ReplacementAdvance are pixel units; the latter is the visible missing-glyph advance.
		// Spacing is applied once at each visible cluster's last glyph; returned glyph metrics remain unmodified.
		// Clusters containing only zero-width format controls/selectors keep carets but consume no spacing.
		// A styled fragment can restrict shaping while retaining the entire line's L1/L2 levels.
		bool ShapeLine(const std::shared_ptr<IFace> &Primary, const std::vector<std::shared_ptr<IFace>> &Fallbacks,
					   const FShapeOptions &Options, std::int64_t ByteOffset, std::int64_t ByteLength,
					   double LetterSpacing, double ReplacementAdvance, FTextLine &Result, std::string &Error,
					   std::int64_t FragmentOffset = -1, std::int64_t FragmentLength = -1) const;

	private:
		struct FImpl;
		explicit FTextLayout(std::shared_ptr<const FImpl> Value);
		std::shared_ptr<const FImpl> Impl;
	};
} // namespace VeroScript::Font
