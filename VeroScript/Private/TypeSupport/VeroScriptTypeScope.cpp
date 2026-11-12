// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTypeScope.h"
#include "VeroScriptAst.h"

#include <utility>

namespace VeroScript
{
	std::string MakeLocalTypePrefix(std::string_view Identity, const std::string_view Module)
	{
		// Private symbol linkage already carries the module. Keep the suffix
		// unchanged when a package relocates the same lexical declaration.
		if (Identity.substr(0, Module.size()) == Module)
		{
			Identity.remove_prefix(Module.size());
		}
		std::uint64_t Hash = 14695981039346656037ull;
		for (const unsigned char Character : Identity)
		{
			Hash = (Hash ^ Character) * 1099511628211ull;
		}
		return "$local" + std::to_string(Hash);
	}

	std::string StripTypeScopeMarkers(std::string Name)
	{
		for (auto At = Name.find("$arity"); At != std::string::npos; At = Name.find("$arity", At))
		{
			auto End = At + 6;
			while (End < Name.size() && Name[End] >= '0' && Name[End] <= '9')
			{
				++End;
			}
			if (End > At + 6 && End < Name.size() && Name[End] == '.')
			{
				Name.erase(At, End - At);
			}
			else
			{
				At = End;
			}
		}
		return Name;
	}

	bool ParseTypePath(const std::string_view Name, std::vector<FTypePathSegment> &Segments)
	{
		Segments.clear();
		std::size_t Start = Name.rfind("::", 0) == 0 ? 2 : 0;
		std::size_t AngleDepth = 0;
		std::size_t ParenthesisDepth = 0;
		std::size_t ArgumentStart = std::string_view::npos;
		FTypePathSegment Segment;
		for (std::size_t Index = Start; Index <= Name.size(); ++Index)
		{
			const char Character = Index == Name.size() ? '\0' : Name[Index];
			if (Character == '(')
			{
				++ParenthesisDepth;
			}
			else if (Character == ')')
			{
				if (ParenthesisDepth == 0)
				{
					return false;
				}
				--ParenthesisDepth;
			}
			else if (Character == '<')
			{
				if (++AngleDepth > 64)
				{
					return false;
				}
				if (AngleDepth == 1 && ParenthesisDepth == 0)
				{
					if (Segment.bApplication)
					{
						return false;
					}
					Segment.Name.assign(Name.substr(Start, Index - Start));
					Segment.bApplication = true;
					ArgumentStart = Index + 1;
				}
			}
			else if (Character == '>' && (Index == 0 || Name[Index - 1] != '-'))
			{
				if (AngleDepth == 0)
				{
					return false;
				}
				--AngleDepth;
				if (AngleDepth == 0 && ParenthesisDepth == 0)
				{
					if (ArgumentStart == Index && !Segment.Arguments.empty())
					{
						return false;
					}
					if (ArgumentStart < Index)
					{
						Segment.Arguments.emplace_back(Name.substr(ArgumentStart, Index - ArgumentStart));
					}
					ArgumentStart = std::string_view::npos;
					if (Index + 1 < Name.size() && Name[Index + 1] != '.' && Name[Index + 1] != ':')
					{
						return false;
					}
				}
			}
			else if (Character == ',' && AngleDepth == 1 && ParenthesisDepth == 0)
			{
				if (ArgumentStart == Index)
				{
					return false;
				}
				Segment.Arguments.emplace_back(Name.substr(ArgumentStart, Index - ArgumentStart));
				ArgumentStart = Index + 1;
			}
			else if ((Character == '.' || Character == ':' || Character == '\0') && AngleDepth == 0 &&
					 ParenthesisDepth == 0)
			{
				if (!Segment.bApplication)
				{
					Segment.Name.assign(Name.substr(Start, Index - Start));
				}
				if (Segment.Name.empty())
				{
					return false;
				}
				Segments.push_back(std::move(Segment));
				Segment = {};
				if (Character == ':')
				{
					if (Index + 1 >= Name.size() || Name[Index + 1] != ':')
					{
						return false;
					}
					++Index;
				}
				Start = Index + 1;
			}
		}
		return AngleDepth == 0 && ParenthesisDepth == 0 && !Segments.empty();
	}

	std::string TypeScopeIdentity(const std::string_view Parent, const std::string_view Kind,
								  const std::string_view Name)
	{
		std::string Result(Parent);
		Result += '/' + std::to_string(Kind.size()) + ':';
		Result.append(Kind);
		Result += std::to_string(Name.size()) + ':';
		Result.append(Name);
		return Result;
	}

	bool HasScopedTypeDeclarations(const FProgram &Program)
	{
		const auto Has = [](const auto &Declarations, const bool IncludeLocalAliases = true)
		{
			for (const auto &Declaration : Declarations)
			{
				if ((IncludeLocalAliases && Declaration.Scope.bLocal) ||
					!Declaration.Scope.OwnerTypeName.empty())
				{
					return true;
				}
			}
			return false;
		};
		// Block aliases predate scoped type definitions and are emitted from their
		// statements. Their ownership catalog does not require runtime type support.
		return Has(Program.Classes) || Has(Program.Structs) || Has(Program.Interfaces) ||
			   Has(Program.Enums) || Has(Program.TypeAliases, false);
	}
} // namespace VeroScript
