// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTooling.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
	int Fail(const char* Message)
	{
		std::cerr << "VeroScriptDocumentHighlightTests: " << Message << '\n';
		return EXIT_FAILURE;
	}

	bool Expect(const bool Condition, const char* Message)
	{
		return Condition ? true : (Fail(Message), false);
	}

	bool FindPosition(
		const std::string_view Source,
		const std::string_view Needle,
		VeroScript::Tooling::FTextPosition& OutPosition)
	{
		const std::size_t Offset = Source.find(Needle);
		if (Offset == std::string_view::npos)
		{
			return false;
		}
		std::uint32_t Line = 1;
		std::uint32_t Column = 1;
		for (std::size_t Index = 0; Index < Offset; ++Index)
		{
			if (Source[Index] == '\n')
			{
				++Line;
				Column = 1;
			}
			else
			{
				++Column;
			}
		}
		OutPosition.Line = Line;
		OutPosition.Column = Column;
		return true;
	}
}

int main()
{
	using namespace VeroScript::Tooling;

	constexpr const char* Source = R"(
void Helper() {}

void main() {
	Helper();
	Helper();
}
)";

	FTextPosition Position;
	if (!Expect(FindPosition(Source, "Helper();", Position), "locate Helper call"))
	{
		return EXIT_FAILURE;
	}

	const FDocumentHighlightResult Result =
		VeroScriptDocumentHighlight::HighlightsAtSource(Source, Position, "<highlight>");
	if (!Expect(Result.bSuccess && Result.bHasToken, "document highlight"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(Result.TokenText == "Helper", "token Helper"))
	{
		return EXIT_FAILURE;
	}
	if (!Expect(Result.Highlights.size() == 3, "same-file decl + two calls"))
	{
		std::cerr << "count=" << Result.Highlights.size() << '\n';
		return EXIT_FAILURE;
	}

	int Definitions = 0;
	int Reads = 0;
	int Texts = 0;
	for (const FDocumentHighlight& Highlight : Result.Highlights)
	{
		if (Highlight.Kind == EDocumentHighlightKind::Write)
		{
			++Definitions;
		}
		if (Highlight.Kind == EDocumentHighlightKind::Read)
		{
			++Reads;
		}
		if (Highlight.Kind == EDocumentHighlightKind::Text)
		{
			++Texts;
		}
	}
	if (!Expect(Texts == 1 && Reads == 2 && Definitions == 0, "text def + read kinds"))
	{
		return EXIT_FAILURE;
	}

	{
		VeroScriptWorkspace Workspace;
		Workspace.OpenDocument("app.vs", Source);
		const FDocumentHighlightResult WorkspaceResult =
			Workspace.HighlightDocument("app.vs", Position);
		if (!Expect(
				WorkspaceResult.bSuccess && WorkspaceResult.Highlights.size() == 3,
				"workspace highlight"))
		{
			return EXIT_FAILURE;
		}
	}

	constexpr const char* WriteSource = R"(
void main() {
	int n = 1;
	n = n + 1;
	print(n);
}
)";

	{
		FTextPosition WritePos;
		if (!Expect(FindPosition(WriteSource, "print(n)", WritePos), "locate print(n)"))
		{
			return EXIT_FAILURE;
		}
		WritePos.Column += static_cast<std::uint32_t>(std::string_view("print(").size());
		const FDocumentHighlightResult WriteResult =
			VeroScriptDocumentHighlight::HighlightsAtSource(WriteSource, WritePos, "<hl-write>");
		if (!Expect(WriteResult.bSuccess, "write highlight query"))
		{
			return EXIT_FAILURE;
		}
		int WriteCount = 0;
		int ReadCount = 0;
		for (const FDocumentHighlight& Highlight : WriteResult.Highlights)
		{
			if (Highlight.Kind == EDocumentHighlightKind::Write)
			{
				++WriteCount;
			}
			else if (Highlight.Kind == EDocumentHighlightKind::Read)
			{
				++ReadCount;
			}
		}
		// decl init + assignment LHS are writes; RHS + print are reads
		// decl init + assignment LHS are writes; RHS + print are reads
		if (!Expect(WriteCount >= 2 && ReadCount >= 2, "assignment LHS marked write"))
		{
			std::cerr << "writes=" << WriteCount << " reads=" << ReadCount << '\n';
			return EXIT_FAILURE;
		}
	}

	constexpr const char* BitWriteSource = R"(
void main() {
	int flags = 1;
	flags &= 2;
	print(flags);
}
)";

	{
		FTextPosition BitPos;
		if (!Expect(FindPosition(BitWriteSource, "flags &=", BitPos), "locate flags &="))
		{
			return EXIT_FAILURE;
		}
		const FDocumentHighlightResult BitResult =
			VeroScriptDocumentHighlight::HighlightsAtSource(BitWriteSource, BitPos, "<hl-bit>");
		if (!Expect(BitResult.bSuccess, "bit write highlight query"))
		{
			return EXIT_FAILURE;
		}
		bool bSawWrite = false;
		for (const FDocumentHighlight& Highlight : BitResult.Highlights)
		{
			if (Highlight.Kind == EDocumentHighlightKind::Write
				&& Highlight.Range.StartColumn == BitPos.Column)
			{
				bSawWrite = true;
			}
		}
		if (!Expect(bSawWrite, "&= LHS marked write"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		constexpr const char* EnumClassSource = R"(
enum class Color { Red, Green }
void main() {
	Color c = Color.Green;
	Color d = Color::Green;
	Green;
}
)";
		FTextPosition DeclPos;
		if (!Expect(
				FindPosition(EnumClassSource, "Green }", DeclPos),
				"locate enum class Green for highlight"))
		{
			return EXIT_FAILURE;
		}
		const FDocumentHighlightResult DeclHl =
			VeroScriptDocumentHighlight::HighlightsAtSource(
				EnumClassSource, DeclPos, "<hl-enum-class-decl>");
		if (!Expect(
				DeclHl.bSuccess && DeclHl.Highlights.size() == 3,
				"enum class member highlight: decl + . and :: uses"))
		{
			std::cerr << "decl highlights=" << DeclHl.Highlights.size() << '\n';
			return EXIT_FAILURE;
		}

		FTextPosition ScopePos;
		if (!Expect(
				FindPosition(EnumClassSource, "Color::Green", ScopePos),
				"locate Color::Green highlight"))
		{
			return EXIT_FAILURE;
		}
		ScopePos.Column += static_cast<std::uint32_t>(std::string_view("Color::").size());
		const FDocumentHighlightResult ScopeHl =
			VeroScriptDocumentHighlight::HighlightsAtSource(
				EnumClassSource, ScopePos, "<hl-enum-class-scope>");
		if (!Expect(
				ScopeHl.bSuccess && ScopeHl.Highlights.size() == 3,
				":: qualified enum class member highlights all uses"))
		{
			std::cerr << "scope highlights=" << ScopeHl.Highlights.size() << '\n';
			return EXIT_FAILURE;
		}

		FTextPosition BarePos;
		if (!Expect(FindPosition(EnumClassSource, "\tGreen;", BarePos), "locate bare Green hl"))
		{
			return EXIT_FAILURE;
		}
		BarePos.Column += 1;
		const FDocumentHighlightResult BareHl =
			VeroScriptDocumentHighlight::HighlightsAtSource(
				EnumClassSource, BarePos, "<hl-enum-class-bare>");
		if (!Expect(
				BareHl.bSuccess
					&& BareHl.bHasToken
					&& BareHl.TokenText == "Green"
					&& BareHl.Highlights.empty(),
				"bare enum class member must not highlight lexical Green"))
		{
			std::cerr << "bare highlights=" << BareHl.Highlights.size() << '\n';
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}
