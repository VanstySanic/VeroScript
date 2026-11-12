// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSourceDocumentation.h"
#include "VeroScriptLocale.h"

#include <cctype>
#include <utility>
#include <vector>

namespace VeroScript::Tooling::Detail
{
	namespace
	{
		std::string_view TrimLeft(std::string_view Text)
		{
			while (!Text.empty()
				&& std::isspace(static_cast<unsigned char>(Text.front())) != 0)
			{
				Text.remove_prefix(1);
			}
			return Text;
		}

		std::string_view TrimRight(std::string_view Text)
		{
			while (!Text.empty()
				&& std::isspace(static_cast<unsigned char>(Text.back())) != 0)
			{
				Text.remove_suffix(1);
			}
			return Text;
		}

		std::string_view Trim(std::string_view Text)
		{
			return TrimRight(TrimLeft(Text));
		}

		std::vector<std::string_view> SourceLines(const std::string_view Source)
		{
			std::vector<std::string_view> Lines;
			std::size_t Start = 0;
			while (Start <= Source.size())
			{
				std::size_t End = Source.find('\n', Start);
				if (End == std::string_view::npos)
				{
					End = Source.size();
				}
				std::string_view Line = Source.substr(Start, End - Start);
				if (!Line.empty() && Line.back() == '\r')
				{
					Line.remove_suffix(1);
				}
				Lines.push_back(Line);
				if (End == Source.size())
				{
					break;
				}
				Start = End + 1;
			}
			return Lines;
		}

		std::string JoinDocumentationLines(
			const std::vector<std::string>& Lines)
		{
			std::size_t Begin = 0;
			std::size_t End = Lines.size();
			while (Begin < End && Lines[Begin].empty())
			{
				++Begin;
			}
			while (End > Begin && Lines[End - 1].empty())
			{
				--End;
			}

			std::string Result;
			for (std::size_t Index = Begin; Index < End; ++Index)
			{
				if (!Result.empty())
				{
					Result.push_back('\n');
				}
				Result += Lines[Index];
			}
			return Result;
		}

		bool ConsumeDocumentationTag(
			const std::string_view Line,
			const std::string_view Tag,
			std::string_view& OutValue)
		{
			if (Line.rfind(Tag, 0) != 0
				|| (Line.size() > Tag.size()
					&& std::isspace(
						static_cast<unsigned char>(Line[Tag.size()])) == 0))
			{
				return false;
			}
			OutValue = Trim(Line.substr(Tag.size()));
			return true;
		}

		void AppendDocumentationContinuation(
			std::string& Target,
			const std::string_view Text)
		{
			if (!Target.empty())
			{
				Target.push_back('\n');
			}
			Target.append(Text.data(), Text.size());
		}

	}

	std::vector<std::string_view> SplitSourceLines(const std::string_view Source)
	{
		return SourceLines(Source);
	}

	/**
	 * Parse source-level documentation tags without involving the compiler.
	 * Unknown/malformed tags remain ordinary body text.
	 */
	std::string FormatDocumentationTags(const std::string_view Documentation)
	{
		struct FParameterDocumentation
		{
			std::string Name;
			std::string Text;
		};
		enum class ESection : std::uint8_t
		{
			Body,
			Parameter,
			Return,
		};

		const std::vector<std::string_view> Lines = SourceLines(Documentation);
		std::vector<std::string> BodyLines;
		std::vector<FParameterDocumentation> Parameters;
		std::string ReturnText;
		ESection Section = ESection::Body;
		std::size_t ParameterIndex = 0;
		bool bFoundTag = false;

		for (const std::string_view RawLine : Lines)
		{
			const std::string_view Line = Trim(RawLine);
			std::string_view Value;
			if (ConsumeDocumentationTag(Line, "@param", Value))
			{
				const std::size_t Separator = Value.find_first_of(" \t");
				const std::string_view Name = Value.substr(0, Separator);
				if (Name.empty())
				{
					BodyLines.emplace_back(RawLine);
					Section = ESection::Body;
					continue;
				}

				FParameterDocumentation Parameter;
				Parameter.Name.assign(Name.data(), Name.size());
				if (Separator != std::string_view::npos)
				{
					const std::string_view Text = Trim(Value.substr(Separator + 1));
					Parameter.Text.assign(Text.data(), Text.size());
				}
				Parameters.push_back(std::move(Parameter));
				ParameterIndex = Parameters.size() - 1;
				Section = ESection::Parameter;
				bFoundTag = true;
				continue;
			}
			if (ConsumeDocumentationTag(Line, "@return", Value)
				|| ConsumeDocumentationTag(Line, "@returns", Value))
			{
				AppendDocumentationContinuation(ReturnText, Value);
				Section = ESection::Return;
				bFoundTag = true;
				continue;
			}

			// Do not attach an unsupported tag to the preceding supported tag.
			// Preserve it verbatim so future/custom documentation extensions
			// remain visible instead of being silently reinterpreted.
			if (!Line.empty() && Line.front() == '@')
			{
				BodyLines.emplace_back(RawLine);
				Section = ESection::Body;
				continue;
			}

			switch (Section)
			{
			case ESection::Parameter:
				{
					AppendDocumentationContinuation(
						Parameters[ParameterIndex].Text,
						Line);
					break;
				}
			case ESection::Return:
				{
					AppendDocumentationContinuation(ReturnText, Line);
					break;
				}
			case ESection::Body:
				{
					BodyLines.emplace_back(RawLine);
					break;
				}
			default:
				{
					break;
				}
			}
		}

		if (!bFoundTag)
		{
			return std::string(Documentation);
		}

		Locale::InitializeModuleFromEnvironment("VeroScriptTooling");
		std::string Result = JoinDocumentationLines(BodyLines);
		for (const FParameterDocumentation& Parameter : Parameters)
		{
			if (!Result.empty())
			{
				Result += "\n\n";
			}
			Result += Locale::TranslateId(
				"tooling.source_documentation.parameter",
				{Parameter.Name});
			if (!Parameter.Text.empty())
			{
				Result.push_back(' ');
				Result += Parameter.Text;
			}
		}
		if (!ReturnText.empty())
		{
			if (!Result.empty())
			{
				Result += "\n\n";
			}
			Result += Locale::TranslateId(
				"tooling.source_documentation.returns");
			Result.push_back(' ');
			Result += ReturnText;
		}
		return Result;
	}

	namespace
	{
		std::string ExtractLineDocumentation(
			const std::vector<std::string_view>& Lines,
			const std::size_t LastLine)
		{
			std::size_t FirstLine = LastLine;
			while (FirstLine > 0)
			{
				const std::string_view Candidate = TrimLeft(Lines[FirstLine - 1]);
				if (Candidate.rfind("//", 0) != 0)
				{
					break;
				}
				--FirstLine;
			}

			std::vector<std::string> DocumentationLines;
			DocumentationLines.reserve(LastLine - FirstLine + 1);
			for (std::size_t Index = FirstLine; Index <= LastLine; ++Index)
			{
				std::string_view Text = TrimLeft(Lines[Index]);
				Text.remove_prefix(Text.rfind("///", 0) == 0 ? 3 : 2);
				if (!Text.empty() && Text.front() == ' ')
				{
					Text.remove_prefix(1);
				}
				DocumentationLines.emplace_back(TrimRight(Text));
			}
			return JoinDocumentationLines(DocumentationLines);
		}

		std::string ExtractBlockDocumentation(
			const std::vector<std::string_view>& Lines,
			const std::size_t LastLine)
		{
			const std::string_view Last = TrimRight(Lines[LastLine]);
			if (Last.size() < 2 || Last.substr(Last.size() - 2) != "*/")
			{
				return {};
			}

			std::size_t FirstLine = LastLine;
			std::size_t OpenOffset = std::string_view::npos;
			for (;;)
			{
				OpenOffset = Lines[FirstLine].find("/*");
				if (OpenOffset != std::string_view::npos)
				{
					if (!Trim(Lines[FirstLine].substr(0, OpenOffset)).empty())
					{
						return {};
					}
					break;
				}
				if (FirstLine == 0)
				{
					return {};
				}
				--FirstLine;
			}

			const std::size_t CloseOffset = Lines[LastLine].rfind("*/");
			if (CloseOffset == std::string_view::npos
				|| !Trim(Lines[LastLine].substr(CloseOffset + 2)).empty())
			{
				return {};
			}
			// Degenerate `/*/` opens and closes with overlapping markers — no body.
			if (FirstLine == LastLine && CloseOffset < OpenOffset + 2)
			{
				return {};
			}

			// Reject when the comment body contains an earlier `*/`: the block actually
			// closed before LastLine, so the lines above the declaration mix comment + code.
			const auto BodyHasEarlyClose = [&]() -> bool
			{
				for (std::size_t Index = FirstLine; Index <= LastLine; ++Index)
				{
					const std::string_view Line = Lines[Index];
					std::size_t Begin = 0;
					std::size_t End = Line.size();
					if (Index == FirstLine)
					{
						Begin = OpenOffset + 2;
					}
					if (Index == LastLine)
					{
						End = CloseOffset;
					}
					if (Begin < End
						&& Line.substr(Begin, End - Begin).find("*/") != std::string_view::npos)
					{
						return true;
					}
				}
				return false;
			};
			if (BodyHasEarlyClose())
			{
				return {};
			}

			std::vector<std::string> DocumentationLines;
			DocumentationLines.reserve(LastLine - FirstLine + 1);
			for (std::size_t Index = FirstLine; Index <= LastLine; ++Index)
			{
				std::string_view Text = Lines[Index];
				if (Index == FirstLine && Index == LastLine)
				{
					Text = Text.substr(
						OpenOffset + 2,
						CloseOffset - OpenOffset - 2);
				}
				else if (Index == FirstLine)
				{
					Text.remove_prefix(OpenOffset + 2);
				}
				else if (Index == LastLine)
				{
					Text = Text.substr(0, CloseOffset);
				}

				Text = Trim(Text);
				if (!Text.empty() && Text.front() == '*')
				{
					Text.remove_prefix(1);
					if (!Text.empty() && Text.front() == ' ')
					{
						Text.remove_prefix(1);
					}
					Text = TrimRight(Text);
				}
				DocumentationLines.emplace_back(Text);
			}
			return JoinDocumentationLines(DocumentationLines);
		}

	}

	std::string ExtractLeadingDocumentation(
		const std::vector<std::string_view>& Lines,
		const std::uint32_t OneBasedDefinitionLine)
	{
		if (OneBasedDefinitionLine <= 1)
		{
			return {};
		}
		const std::size_t PreviousLine =
			static_cast<std::size_t>(OneBasedDefinitionLine - 2);
		if (PreviousLine >= Lines.size() || Trim(Lines[PreviousLine]).empty())
		{
			return {};
		}

		if (TrimLeft(Lines[PreviousLine]).rfind("//", 0) == 0)
		{
			return ExtractLineDocumentation(Lines, PreviousLine);
		}
		return ExtractBlockDocumentation(Lines, PreviousLine);
	}

	std::string ExtractLeadingDocumentation(
		const std::string_view Source,
		const std::uint32_t OneBasedDefinitionLine)
	{
		return ExtractLeadingDocumentation(SourceLines(Source), OneBasedDefinitionLine);
	}

}
