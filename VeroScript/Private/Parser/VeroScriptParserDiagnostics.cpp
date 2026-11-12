// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptParser.h"

#include "VeroScriptAst.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParserDetail.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <string>

namespace VeroScript
{
	void VeroScriptParser::ReportError(std::string Message)
	{
		PendingGenericCloseCount = 0;
		if (!bHasError)
		{
			bHasError = true;
			// Keep default line/column (1,1); only the file label is known here.
			FSourceLocation Location;
			Location.FileLabel = FileLabel;
			FillFromEnglishMessage(Error, std::move(Message), Location);
		}
	}

	void VeroScriptParser::ReportErrorAt(const FToken& Token, std::string Message)
	{
		ReportErrorAt(Token.Location, std::move(Message));
	}

	void VeroScriptParser::ReportErrorAt(
		const FSourceLocation& Location,
		std::string Message)
	{
		PendingGenericCloseCount = 0;
		if (!bHasError)
		{
			bHasError = true;
			FillFromEnglishMessage(Error, std::move(Message), Location);
			if (Error.Location.FileLabel.empty())
			{
				Error.Location.FileLabel = FileLabel;
			}
		}
	}

	void VeroScriptParser::ReportErrorId(const FToken& Token, const std::string_view MessageId)
	{
		ReportErrorId(Token.Location, MessageId);
	}

	void VeroScriptParser::ReportErrorId(
		const FSourceLocation& Location,
		const std::string_view MessageId)
	{
		ReportErrorId(Location, MessageId, {});
	}

	void VeroScriptParser::ReportErrorId(
		const FSourceLocation& Location,
		const std::string_view MessageId,
		const std::initializer_list<FDiagnosticArg> Args)
	{
		PendingGenericCloseCount = 0;
		if (!bHasError)
		{
			bHasError = true;
			FillDiagnostic(Error, MessageId, Location, Args);
			if (Error.Location.FileLabel.empty())
			{
				Error.Location.FileLabel = FileLabel;
			}
		}
	}

	void VeroScriptParser::CommitCurrentError()
	{
		if (!bHasError || Error.Message.empty())
		{
			return;
		}
		if (Diagnostics.size() >= MaxCollectedErrors)
		{
			return;
		}
		for (const FScriptError& Existing : Diagnostics)
		{
			if (Existing.Message == Error.Message
				&& Existing.MessageId == Error.MessageId
				&& Existing.Location.Line == Error.Location.Line
				&& Existing.Location.Column == Error.Location.Column
				&& Existing.Location.FileLabel == Error.Location.FileLabel)
			{
				return;
			}
		}
		Diagnostics.push_back(Error);
	}

	void VeroScriptParser::SynchronizeToNextDeclaration()
	{
		PendingGenericCloseCount = 0;
		if (bTypeNameDepthExceeded)
		{
			// A nested generic tail is made entirely from tokens which each look like
			// the start of another declaration (`array<...`).  Normal recovery would
			// therefore retry the same pathological tail repeatedly.  Consume the
			// failed declaration once, up to its terminator, before resuming.
			bTypeNameDepthExceeded = false;
			int NestedBraceDepth = 0;
			while (!IsAtEnd())
			{
				if (CheckSymbol("{"))
				{
					++NestedBraceDepth;
				}
				else if (CheckSymbol("}") && NestedBraceDepth > 0)
				{
					--NestedBraceDepth;
				}
				else if (NestedBraceDepth == 0 && CheckSymbol(";"))
				{
					Advance();
					return;
				}
				Advance();
			}
			return;
		}
		int BraceDepth = 0;
		if (!IsAtEnd())
		{
			Advance();
		}
		while (!IsAtEnd())
		{
			if (CheckSymbol("{"))
			{
				Advance();
				++BraceDepth;
				continue;
			}
			if (CheckSymbol("}"))
			{
				Advance();
				if (BraceDepth > 0)
				{
					--BraceDepth;
				}
				continue;
			}
			if (BraceDepth == 0 && Check(ETokenKind::Keyword))
			{
				const std::string_view Lexeme = Peek().Lexeme;
				if (Lexeme == "import"
					|| Lexeme == "struct"
					|| Lexeme == "class"
					|| Lexeme == "interface"
					|| Lexeme == "enum"
					|| Lexeme == "export"
					|| Lexeme == "using"
					|| Lexeme == "typedef"
					|| Lexeme == "namespace"
					|| Lexeme == "template")
				{
					return;
				}
			}
			if (BraceDepth == 0 && CheckTypeName())
			{
				return;
			}
			Advance();
		}
	}

	bool VeroScriptParser::RecoverAfterDeclarationError()
	{
		CommitCurrentError();
		if (!bCollectMultipleErrors || Diagnostics.size() >= MaxCollectedErrors)
		{
			if (!Diagnostics.empty())
			{
				Error = Diagnostics.front();
			}
			bHasError = true;
			return false;
		}
		SynchronizeToNextDeclaration();
		bHasError = false;
		Error = {};
		return true;
	}
}
