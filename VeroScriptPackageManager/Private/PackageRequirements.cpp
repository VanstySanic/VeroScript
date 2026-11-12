// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInternal.h"
#include <algorithm>
#include <sstream>

namespace VeroScript::Packages::Detail
{
	namespace
	{
		bool Space(char C)
		{
			return C == ' ' || C == '\t' || C == '\r' || C == '\n';
		}
		std::string Trim(std::string S)
		{
			auto Begin = S.find_first_not_of(" \t\r\n");
			return Begin == std::string::npos ? std::string{}
											  : S.substr(Begin, S.find_last_not_of(" \t\r\n") - Begin + 1);
		}
		std::string Unquote(std::string S)
		{
			S = Trim(std::move(S));
			if (!S.empty() && (S.front() == '\'' || S.front() == '"'))
			{
				if (S.size() < 2 || S.back() != S.front() || S.find(S.front(), 1) != S.size() - 1)
				{
					Fail("requirements-syntax", "expected one quoted path or URL");
				}
				S = S.substr(1, S.size() - 2);
			}
			if (S.empty())
			{
				Fail("requirements-syntax", "expected a nonempty file path or dependency");
			}
			return S;
		}
		std::string StripComment(const std::string& S)
		{
			char Quote = 0;
			for (std::size_t I = 0; I < S.size(); ++I)
			{
				if (Quote)
				{
					if (S[I] == Quote)
					{
						Quote = 0;
					}
				}
				else if ((S[I] == '\'' || S[I] == '"') && (I == 0 || Space(S[I - 1]) || S[I - 1] == '=' ||
															 (I == 2 && S.rfind("-r", 0) == 0)))
				{
					Quote = S[I];
				}
				else if (S[I] == '#' && (I == 0 || Space(S[I - 1])))
				{
					return Trim(S.substr(0, I));
				}
			}
			return Trim(S);
		}
		bool Url(const std::string& S)
		{
			return S.rfind("https://", 0) == 0 || S.rfind("http://", 0) == 0;
		}
		bool Local(const std::string& S)
		{
			return S.find_first_of("/\\") != std::string::npos || IsPackageArchive(U8Path(S)) || S == "." ||
				   S == "..";
		}
		std::string VersionRequirement(std::string S, const FWork& Work)
		{
			S = Trim(std::move(S));
			std::string Result;
			std::size_t I = 0;
			while (I < S.size())
			{
				Work.Check();
				while (I < S.size() && Space(S[I]))
				{
					++I;
				}
				auto Begin = I;
				while (I < S.size() && (S[I] == '<' || S[I] == '>' || S[I] == '='))
				{
					++I;
				}
				auto Operator = S.substr(Begin, I - Begin);
				if (Operator == "==")
				{
					Operator = "=";
				}
				while (I < S.size() && Space(S[I]))
				{
					++I;
				}
				Begin = I;
				while (I < S.size() && !Space(S[I]) && S[I] != ',')
				{
					++I;
				}
				if (Begin == I)
				{
					Fail("requirements-syntax", "version comparison requires a version");
				}
				if (!Result.empty())
				{
					Result += ' ';
				}
				Result += Operator + S.substr(Begin, I - Begin);
				while (I < S.size() && Space(S[I]))
				{
					++I;
				}
				if (I < S.size() && S[I] == ',')
				{
					++I;
					if (S.find_first_not_of(" \t\r\n", I) == std::string::npos)
					{
						Fail("requirements-syntax", "trailing comma in version requirement");
					}
				}
			}
			ValidateRequirements(Result);
			return Result;
		}
		FInstallInput ParseInput(std::string Item, const std::string& Base, bool FromFile, const FWork& Work)
		{
			Work.Check();
			FInstallInput Input;
			bool Quoted = FromFile && !Item.empty() && (Item.front() == '\'' || Item.front() == '"');
			if (FromFile)
			{
				Item = Unquote(std::move(Item));
			}
			auto At = Item.find('@');
			auto Prefix = At == std::string::npos ? std::string{} : Trim(Item.substr(0, At));
			bool Reference = FromFile && !Quoted && Name(Prefix) && At > 0 &&
							 (Space(Item[At - 1]) || (At + 1 < Item.size() && Space(Item[At + 1])));
			if (Reference)
			{
				Input.Name = Prefix;
				Item = Unquote(Item.substr(At + 1));
			}
			if (Reference || Quoted || Url(Item) || Local(Item))
			{
				Input.Location = Url(Item) ? Item : NativePathText(Absolute(Item, Base));
				return Input;
			}
			if (FromFile)
			{
				auto End = Item.find_first_of(" \t@<>=");
				Input.Name = Item.substr(0, End);
				auto Requirement = End == std::string::npos ? std::string{} : Trim(Item.substr(End));
				if (!Requirement.empty() && Requirement.front() == '@')
				{
					Requirement = Trim(Requirement.substr(1));
				}
				if (!Requirement.empty())
				{
					Input.Version = VersionRequirement(Requirement, Work);
				}
				else if (End != std::string::npos)
				{
					Fail("requirements-syntax", "dependency has an empty version requirement");
				}
			}
			else
			{
				Input.Name = Item.substr(0, At);
				if (At != std::string::npos)
				{
					Input.Version = Item.substr(At + 1);
					ValidateRequirements(Input.Version);
				}
			}
			if (!Name(Input.Name))
			{
				Fail("package-name", "invalid package name", Input.Name);
			}
			return Input;
		}
		class FRequirementReader
		{
		public:
			explicit FRequirementReader(const FWork& InWork);
			FInstallInputs ReadRequest(const FRequest&);

		private:
			void ReadFile(const fs::path&);
			void ReadLine(const std::string&, const FInstallInput&);
			void Add(FInstallInput);

			const FWork& Work;
			FInstallInputs Inputs;
			std::vector<fs::path> Active;
			std::uint64_t FileCount = 0;
		};
		FRequirementReader::FRequirementReader(const FWork& InWork) : Work(InWork)
		{
		}
		FInstallInputs FRequirementReader::ReadRequest(const FRequest& Request)
		{
			for (const auto& Item : Request.Arguments)
			{
				Add(ParseInput(Item, Request.Environment.WorkingDirectory, false, Work));
			}
			for (const auto& File : Request.RequirementFiles)
			{
				if (Url(File))
				{
					Fail("requirements-option", "requirements includes must use local files", File);
				}
				ReadFile(Absolute(File, Request.Environment.WorkingDirectory));
			}
			return std::move(Inputs);
		}
		void FRequirementReader::Add(FInstallInput Input)
		{
			Work.Check();
			if (Inputs.Items.size() >= Work.Options.Limits.MaxEdges)
			{
				Fail("requirements-limit", "requirements exceed the dependency entry budget");
			}
			Inputs.Items.push_back(std::move(Input));
		}
		void FRequirementReader::ReadFile(const fs::path& Path)
		{
			FInstallInput Origin;
			Origin.Source = NativePathText(Path);
			try
			{
				Work.Check();
				if (std::any_of(Active.begin(), Active.end(), [&](const fs::path& P)
				{
					return PathUtil::Equal(NativePathText(P), NativePathText(Path));
				}))
				{
					Fail("requirements-cycle", "requirements files include each other recursively");
				}
				if (Active.size() >= std::min<std::size_t>(Work.Options.Limits.MaxDependencyDepth, 256) ||
					++FileCount > Work.Options.Limits.MaxFiles)
				{
					Fail("requirements-limit", "requirements include depth or file budget exhausted");
				}
				Active.push_back(Path);
				auto Text = Detail::Read(Path, Work.Options.Limits.MaxManifestBytes, &Work);
				Work.ChargeMetadata(Text.size());
				auto Identity = Hash(Text);
				auto Saved = Inputs.Files.emplace(Path, Identity);
				if (!Saved.second && Saved.first->second != Identity)
				{
					Fail("requirements-changed", "requirements file changed during the operation");
				}
				try
				{
					(void)J(Text).dump();
				}
				catch (const J::exception&)
				{
					Fail("requirements-encoding", "requirements files must use UTF-8");
				}
				if (Text.find('\0') != std::string::npos)
				{
					Fail("requirements-encoding", "requirements files cannot contain NUL bytes");
				}
				if (Text.compare(0, 3, "\xef\xbb\xbf") == 0)
				{
					Text.erase(0, 3);
				}
				std::istringstream Stream(Text);
				std::string Physical, Logical;
				std::size_t Line = 0;
				bool Continued = false;
				while (std::getline(Stream, Physical))
				{
					Work.Check();
					++Line;
					if (!Continued)
					{
						Origin.Line = Line;
					}
					auto End = Physical.find_last_not_of(" \t\r");
					Physical.resize(End == std::string::npos ? 0 : End + 1);
					auto Slashes = Physical.find_last_not_of('\\');
					auto Count =
						Slashes == std::string::npos ? Physical.size() : Physical.size() - Slashes - 1;
					Continued = Count % 2 == 1;
					if (Continued)
					{
						Physical.pop_back();
					}
					Logical += Physical;
					if (Continued)
					{
						Logical += ' ';
						continue;
					}
					ReadLine(StripComment(Logical), Origin);
					Logical.clear();
				}
				if (Continued)
				{
					Fail("requirements-syntax", "unterminated requirements line continuation");
				}
				Active.pop_back();
			}
			catch (FFailure& Failure)
			{
				LocateInstallFailure(Failure, Origin);
				throw;
			}
			catch (const fs::filesystem_error& Failure)
			{
				FFailure Error("requirements-file", Failure.what());
				LocateInstallFailure(Error, Origin);
				throw Error;
			}
		}
		void FRequirementReader::ReadLine(const std::string& Line, const FInstallInput& Origin)
		{
			if (Line.empty())
			{
				return;
			}
			if (Line.front() == '-')
			{
				std::string File;
				if (Line.rfind("-r", 0) == 0)
				{
					File = Line.substr(2);
				}
				else if (Line.rfind("--requirement=", 0) == 0)
				{
					File = Line.substr(14);
				}
				else if (Line.rfind("--requirement", 0) == 0 && (Line.size() == 13 || Space(Line[13])))
				{
					File = Line.substr(13);
				}
				else
				{
					Fail("requirements-option",
						 "unsupported option in requirements file; only -r/--requirement is allowed");
				}
				File = Unquote(std::move(File));
				if (Url(File))
				{
					Fail("requirements-option", "requirements includes must use local files", File);
				}
				auto Path = Absolute(File, NativePathText(U8Path(Origin.Source).parent_path()));
				try
				{
					ReadFile(Path);
				}
				catch (FFailure& Failure)
				{
					// A file that cannot be opened has no source line of its own.
					// Locate that failure at the include while retaining the failing path.
					if (Failure.Diagnostic.Field.rfind(NativePathText(Path) + ":0", 0) == 0)
					{
						Failure.Diagnostic.Source.clear();
						LocateInstallFailure(Failure, Origin);
					}
					throw;
				}
				return;
			}
			auto Input = ParseInput(Line, NativePathText(U8Path(Origin.Source).parent_path()), true, Work);
			Input.Source = Origin.Source;
			Input.Line = Origin.Line;
			Add(std::move(Input));
		}
	} // namespace
	FInstallInputs ReadInstallInputs(const FRequest& Request, const FWork& Work)
	{
		return FRequirementReader(Work).ReadRequest(Request);
	}
	std::string IntersectInstallVersions(const std::string& A, const std::string& B)
	{
		std::set<std::string> Seen;
		std::string Result, Term;
		for (const auto* Requirement : {&A, &B})
		{
			std::istringstream Stream(*Requirement);
			while (Stream >> Term)
			{
				if (Term != "*" && Seen.insert(Term).second)
				{
					Result += (Result.empty() ? "" : " ") + Term;
				}
			}
		}
		return Result.empty() ? "*" : Result;
	}
	void VerifyInstallInputs(const FInstallInputs& Inputs, const FWork& Work)
	{
		for (const auto& File : Inputs.Files)
		{
			if (!fs::exists(File.first) ||
				HashFile(File.first, Work, Work.Options.Limits.MaxManifestBytes) != File.second)
			{
				Fail("requirements-changed", "requirements file changed during the operation; retry",
					 NativePathText(File.first));
			}
		}
	}
	void LocateInstallFailure(FFailure& Failure, const FInstallInput& Origin)
	{
		if (!Origin.Source.empty() && Failure.Diagnostic.Source.empty())
		{
			Failure.Diagnostic.Source = Origin.Source;
			Failure.Diagnostic.Field =
				Origin.Source + ":" + std::to_string(Origin.Line) +
				(Failure.Diagnostic.Field.empty() ? "" : ": " + Failure.Diagnostic.Field);
		}
	}
} // namespace VeroScript::Packages::Detail
