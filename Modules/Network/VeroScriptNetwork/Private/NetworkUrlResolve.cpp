// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNetwork.h"
#include "NetworkUrlInternal.h"

namespace VeroScript::Network
{
	static bool StartsWith(std::string_view Text, std::string_view Prefix)
	{
		return Text.substr(0, Prefix.size()) == Prefix;
	}

	static void RemoveLastSegment(std::string& Text)
	{
		const auto Slash = Text.rfind('/');
		Text.resize(Slash == std::string::npos ? 0 : Slash);
	}

	static std::string RemoveDotSegments(std::string_view Input)
	{
		std::string Output;
		Output.reserve(Input.size());
		while (!Input.empty())
		{
			if (StartsWith(Input, "../"))
			{
				Input.remove_prefix(3);
			}
			else if (StartsWith(Input, "./"))
			{
				Input.remove_prefix(2);
			}
			else if (StartsWith(Input, "/./"))
			{
				Input.remove_prefix(2);
			}
			else if (Input == "/.")
			{
				Input = "/";
			}
			else if (StartsWith(Input, "/../"))
			{
				Input.remove_prefix(3);
				RemoveLastSegment(Output);
			}
			else if (Input == "/..")
			{
				Input = "/";
				RemoveLastSegment(Output);
			}
			else if (Input == "." || Input == "..")
			{
				break;
			}
			else
			{
				const auto Slash = Input.find('/', Input.front() == '/' ? 1 : 0);
				const auto Segment = Input.substr(0, Slash);
				Output.append(Segment.data(), Segment.size());
				Input.remove_prefix(Segment.size());
			}
		}
		return Output;
	}

	bool FUrl::TryResolve(std::string_view BaseText, std::string_view Reference, FUrl& Out, FError& Error)
	{
		FUrl Value;
		if (!TryParse(BaseText, Value, Error))
		{
			return false;
		}
		if (!UrlDetail::ValidText(Reference))
		{
			Error = {EError::InvalidArgument, "Invalid network URL reference", 0};
			return false;
		}
		const auto Colon = Reference.find(':');
		const auto FirstDelimiter = Reference.find_first_of("/?#");
		if ((Colon != std::string_view::npos && (FirstDelimiter == std::string_view::npos || Colon < FirstDelimiter)) ||
			StartsWith(Reference, "//"))
		{
			const auto Absolute =
				StartsWith(Reference, "//") ? Value.Scheme + ':' + std::string(Reference) : std::string(Reference);
			if (!TryParse(Absolute, Value, Error))
			{
				return false;
			}
			Value.Path = RemoveDotSegments(Value.Path);
		}
		else
		{
			const auto Fragment = Reference.find('#');
			Value.bHasFragment = Fragment != std::string_view::npos;
			Value.Fragment = Value.bHasFragment ? std::string(Reference.substr(Fragment + 1)) : std::string{};
			Reference = Reference.substr(0, Fragment);
			const auto Query = Reference.find('?');
			const auto Path = Reference.substr(0, Query);
			if (!Path.empty() || Query != std::string_view::npos)
			{
				Value.bHasQuery = Query != std::string_view::npos;
				Value.Query = Value.bHasQuery ? std::string(Reference.substr(Query + 1)) : std::string{};
			}
			if (!Path.empty())
			{
				const auto Parent = Value.Path.substr(0, Value.Path.rfind('/') + 1);
				Value.Path = RemoveDotSegments(Path.front() == '/' ? std::string(Path) : Parent + std::string(Path));
			}
		}
		// Parsing the recomposed URL also validates escapes in the original reference.
		return TryParse(Value.String(), Out, Error);
	}
} // namespace VeroScript::Network
