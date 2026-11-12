// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptGuiResources.h"
#include "VeroScriptGuiTypes.h"
#include <filesystem>
#include <fstream>

namespace VeroScript::Gui
{
	bool ResolveGuiFileReference(const std::string& Requester, const std::string& Reference, std::string& Identity,
								 std::string& Error)
	try
	{
		Error.clear();
		if (Reference.empty() || Reference.find('\0') != std::string::npos)
		{
			Error = "gui.invalid_resource_reference";
			return false;
		}
		const auto Colon = Reference.find(':');
		if (Colon != std::string::npos && Colon != 1)
		{
			Identity = Reference;
			return true;
		}
		auto Path = std::filesystem::u8path(Reference);
		if (!Path.is_absolute())
		{
			if (Requester.empty() || Requester.front() == '<' ||
				(Requester.find(':') != std::string::npos && Requester.find(':') != 1))
			{
				Error = "gui.resource_resolver_required";
				return false;
			}
			Path = std::filesystem::u8path(Requester).parent_path() / Path;
		}
		const auto Utf8 = Path.lexically_normal().generic_u8string();
		Identity.assign(Utf8.begin(), Utf8.end());
		return true;
	}
	catch (const std::exception& Exception)
	{
		Error = Exception.what();
		return false;
	}

	bool ResolveGuiStyleImages(const std::string& Requester, const std::string& Value, const FResourceResolver& Resolve,
							   std::string& Result, std::string& Error)
	{
		std::string Candidate;
		std::size_t At = 0;
		while (At < Value.size())
		{
			std::size_t Start = At;
			char Literal = 0;
			for (; Start < Value.size(); ++Start)
			{
				const char C = Value[Start];
				if (Literal)
				{
					if (C == '\\' && Start + 1 < Value.size())
					{
						++Start;
					}
					else if (C == Literal)
					{
						Literal = 0;
					}
					continue;
				}
				if (C == '\'' || C == '"')
				{
					Literal = C;
					continue;
				}
				if ((C == 'u' || C == 'U') && Start + 3 < Value.size() &&
					(Value[Start + 1] == 'r' || Value[Start + 1] == 'R') &&
					(Value[Start + 2] == 'l' || Value[Start + 2] == 'L') && Value[Start + 3] == '(')
				{
					break;
				}
			}
			if (Start == Value.size())
			{
				Candidate += Value.substr(At);
				break;
			}
			Candidate += Value.substr(At, Start - At);
			std::size_t End = Start + 4;
			while (End < Value.size() && (Value[End] == ' ' || Value[End] == '\t'))
			{
				++End;
			}
			const char Quote = End < Value.size() && (Value[End] == '\'' || Value[End] == '"') ? Value[End++] : 0;
			std::string Reference;
			bool Closed = false;
			while (End < Value.size())
			{
				const char C = Value[End++];
				if ((Quote && C == Quote) || (!Quote && C == ')'))
				{
					Closed = true;
					break;
				}
				if (C == '\\' && End < Value.size())
				{
					Reference += Value[End++];
				}
				else
				{
					Reference += C;
				}
			}
			if (Quote)
			{
				while (End < Value.size() && (Value[End] == ' ' || Value[End] == '\t'))
				{
					++End;
				}
				if (End >= Value.size() || Value[End++] != ')')
				{
					Closed = false;
				}
			}
			if (!Quote)
			{
				while (!Reference.empty() && (Reference.back() == ' ' || Reference.back() == '\t'))
				{
					Reference.pop_back();
				}
			}
			std::string Identity;
			if (!Closed || Reference.empty())
			{
				Error = "gui.invalid_image_url";
				return false;
			}
			if (!(Resolve ? Resolve(Requester, Reference, Identity, Error)
						  : ResolveGuiFileReference(Requester, Reference, Identity, Error)))
			{
				return false;
			}
			Candidate += "url(\"";
			if (Identity.empty() || Identity.find('\0') != std::string::npos)
			{
				Error = "gui.invalid_image_identity";
				return false;
			}
			for (char C : Identity)
			{
				if (C == '\\' || C == '"')
				{
					Candidate += '\\';
				}
				Candidate += C;
			}
			Candidate += "\")";
			At = End;
		}
		Result = std::move(Candidate);
		Error.clear();
		return true;
	}

	bool ValidateFontFace(const FFontFace& Font, std::string& Error)
	{
		if (!VeroScript::Font::ValidateFaceDescription(Font, Error))
		{
			Error = "gui.invalid_font_face";
			return false;
		}
		return true;
	}

	bool ReadGuiResourceFile(const std::string& Identity, std::vector<std::uint8_t>& Bytes, std::string& Error)
	try
	{
		Error.clear();
		if (Identity.empty() || Identity.find('\0') != std::string::npos)
		{
			Error = "gui.invalid_resource_identity";
			return false;
		}
		std::ifstream File(std::filesystem::u8path(Identity), std::ios::binary | std::ios::ate);
		const auto Size = File.tellg();
		if (!File || Size < 0 || Size > 64 * 1024 * 1024)
		{
			Error = "gui.resource_read_failed: " + Identity;
			return false;
		}
		std::vector<std::uint8_t> Candidate(static_cast<std::size_t>(Size));
		File.seekg(0);
		if (!Candidate.empty() && !File.read(reinterpret_cast<char*>(Candidate.data()), Size))
		{
			Error = "gui.resource_read_failed: " + Identity;
			return false;
		}
		Bytes = std::move(Candidate);
		return true;
	}
	catch (const std::exception& Exception)
	{
		Error = std::string("gui.resource_read_failed: ") + Exception.what();
		return false;
	}
} // namespace VeroScript::Gui
