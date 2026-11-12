// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInternal.h"
#include "VeroScriptVersion.h"
#include "sha256/picosha2.h"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>

namespace VeroScript::Packages::Detail
{
	FFailure::FFailure(std::string Code, std::string Message, std::string Field) : std::runtime_error(Message)
	{
		Diagnostic.Code = std::move(Code);
		Diagnostic.Message = std::move(Message);
		Diagnostic.Field = std::move(Field);
	}
	void Fail(const std::string& C, const std::string& M, const std::string& F)
	{
		throw FFailure(C, M, F);
	}
	void FWork::Check() const
	{
		if (Options.CheckCancelled)
		{
			Options.CheckCancelled();
		}
		if (Options.Limits.TimeoutMilliseconds &&
			std::chrono::steady_clock::now() - Started >
				std::chrono::milliseconds(Options.Limits.TimeoutMilliseconds))
		{
			Fail("timeout", "package operation exceeded its deadline");
		}
	}
	void FWork::ChargeMetadata(std::size_t Bytes) const
	{
		Check();
		if (Bytes > Options.Limits.MaxMetadataBytes - MetadataBytes)
		{
			Fail("metadata-limit", "aggregate metadata budget exhausted");
		}
		MetadataBytes += Bytes;
	}
	J Parse(std::string_view Text, const FLimits& L)
	{
		if (Text.size() > L.MaxManifestBytes)
		{
			Fail("json-limit", "JSON input exceeds its byte budget");
		}
		// The JSON library otherwise converts overflowing integer literals to
		// double. Reject that fallback before typed installation inputs are read.
		bool Quoted = false, Escaped = false;
		for (std::size_t I = 0; I < Text.size(); ++I)
		{
			char C = Text[I];
			if (Quoted)
			{
				if (Escaped)
				{
					Escaped = false;
				}
				else if (C == '\\')
				{
					Escaped = true;
				}
				else if (C == '"')
				{
					Quoted = false;
				}
				continue;
			}
			if (C == '"')
			{
				Quoted = true;
				continue;
			}
			if (C != '-' && (C < '0' || C > '9'))
			{
				continue;
			}
			auto End = I + 1;
			while (End < Text.size() &&
				   ((Text[End] >= '0' && Text[End] <= '9') || Text[End] == '.' || Text[End] == 'e' ||
					Text[End] == 'E' || Text[End] == '+' || Text[End] == '-'))
			{
				++End;
			}
			auto Token = Text.substr(I, End - I);
			if (Token.find_first_of(".eE") == std::string_view::npos)
			{
				std::errc Error{};
				if (C == '-')
				{
					std::int64_t N = 0;
					Error = std::from_chars(Token.data(), Token.data() + Token.size(), N).ec;
				}
				else
				{
					std::uint64_t N = 0;
					Error = std::from_chars(Token.data(), Token.data() + Token.size(), N).ec;
				}
				if (Error == std::errc::result_out_of_range)
				{
					Fail("integer-range", "JSON integer exceeds signed/unsigned 64-bit range");
				}
			}
			I = End - 1;
		}
		std::vector<std::set<std::string>> Objects;
		try
		{
			return J::parse(Text.begin(), Text.end(),
				[&](int Depth, J::parse_event_t Event, J& V)
				{
					if (static_cast<std::size_t>(Depth) > L.MaxJsonDepth)
					{
						Fail("json-depth", "JSON nesting exceeds its budget");
					}
					if (Event == J::parse_event_t::object_start)
					{
						Objects.resize(std::max(Objects.size(), std::size_t(Depth + 1)));
						Objects[Depth].clear();
					}
					if (Event == J::parse_event_t::key &&
						!Objects[Depth - 1].insert(V.get<std::string>()).second)
					{
						Fail("json-duplicate", "duplicate JSON key", V.get<std::string>());
					}
					return true;
				});
		}
		catch (const J::exception& E)
		{
			Fail("invalid-json", E.what());
		}
	}
	void Keys(const J& V, std::initializer_list<const char*> Allowed, const std::string& Field)
	{
		if (!V.is_object())
		{
			Fail("manifest-type", "expected an object", Field);
		}
		for (auto I = V.begin(); I != V.end(); ++I)
		{
			if (std::find_if(Allowed.begin(), Allowed.end(),
					[&](const char* A)
					{
						return I.key() == A;
					}) == Allowed.end())
			{
				Fail("unknown-field", "unknown standard field", Field + "." + I.key());
			}
		}
	}
	void String(const J& V, const std::string& F)
	{
		if (!V.is_string())
		{
			Fail("manifest-type", "expected a UTF-8 string", F);
		}
		(void)V.dump();
	}
	void Strings(const J& V, const std::string& F)
	{
		if (!V.is_array())
		{
			Fail("manifest-type", "expected a string array", F);
		}
		std::set<std::string> Seen;
		for (const auto& S : V)
		{
			String(S, F);
			if (!Seen.insert(S.get<std::string>()).second)
			{
				Fail("duplicate-value", "duplicate array value", F);
			}
		}
	}
	bool Name(std::string_view N)
	{
		if (N.size() < 2 || N.size() > 64 || N.front() == '-' || N.back() == '-')
		{
			return false;
		}
		bool Dash = false;
		for (char C : N)
		{
			if (C == '-')
			{
				if (Dash)
				{
					return false;
				}
				Dash = true;
			}
			else
			{
				if (!((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9')))
				{
					return false;
				}
				Dash = false;
			}
		}
		const auto Dot = N.find('.');
		const auto Base = N.substr(0, Dot);
		if (Base == "con" || Base == "prn" || Base == "aux" || Base == "nul" || Base == "clock$")
		{
			return false;
		}
		if (Base.size() == 4 && (Base.substr(0, 3) == "com" || Base.substr(0, 3) == "lpt") &&
			Base[3] >= '1' && Base[3] <= '9')
		{
			return false;
		}
		return true;
	}
	bool Digest(std::string_view S)
	{
		return S.size() == 64 &&
			std::all_of(S.begin(), S.end(),
				[](char C)
				{
					return (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f');
				});
	}
	std::string Hash(std::string_view S)
	{
		return picosha2::hash256_hex_string(S.begin(), S.end());
	}
	std::string Id(const J& V)
	{
		return Hash(V.dump());
	}

	namespace
	{
		struct V
		{
			std::uint64_t N[3]{};
			std::vector<std::string> Pre;
		};
		bool Digits(const std::string& S)
		{
			return !S.empty() &&
				std::all_of(S.begin(), S.end(),
					[](char C)
					{
						return C >= '0' && C <= '9';
					});
		}
		V Version(const std::string& S)
		{
			if (S.size() > 190)
			{
				Fail("invalid-version", "version is too long for portable artifact paths");
			}
			V R;
			auto Dash = S.find('-');
			auto Base = S.substr(0, Dash);
			std::size_t Start = 0;
			for (unsigned I = 0; I < 3; ++I)
			{
				auto End = Base.find('.', Start);
				auto T = Base.substr(Start, End == std::string::npos ? End : End - Start);
				if (!Digits(T) || (T.size() > 1 && T[0] == '0') || (I < 2 && End == std::string::npos) ||
					(I == 2 && End != std::string::npos))
				{
					Fail("invalid-version", "expected SemVer without build metadata", S);
				}
				auto P = std::from_chars(T.data(), T.data() + T.size(), R.N[I]);
				if (P.ec != std::errc{})
				{
					Fail("invalid-version", "version component is out of range", S);
				}
				Start = End + 1;
			}
			if (Dash != std::string::npos)
			{
				std::string P = S.substr(Dash + 1);
				Start = 0;
				for (;;)
				{
					auto End = P.find('.', Start);
					auto T = P.substr(Start, End == std::string::npos ? End : End - Start);
					if (T.empty() ||
						!std::all_of(T.begin(), T.end(),
							[](char C)
							{
								return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
									(C >= '0' && C <= '9') || C == '-';
							}) ||
						(Digits(T) && T.size() > 1 && T[0] == '0'))
					{
						Fail("invalid-version", "invalid prerelease identifier", S);
					}
					R.Pre.push_back(T);
					if (End == std::string::npos)
					{
						break;
					}
					Start = End + 1;
				}
			}
			return R;
		}
		void PathField(const J& V, const std::string& F, bool Dir = false)
		{
			String(V, F);
			CheckPath(V.get<std::string>(), Dir);
		}
		void EntryArray(const J& V, const std::string& F)
		{
			if (!V.is_array())
			{
				Fail("manifest-type", "expected an entry array", F);
			}
		}
	} // namespace
	int CompareVersions(const std::string& A, const std::string& B)
	{
		auto X = Version(A), Y = Version(B);
		for (unsigned I = 0; I < 3; ++I)
		{
			if (X.N[I] != Y.N[I])
			{
				return X.N[I] < Y.N[I] ? -1 : 1;
			}
		}
		if (X.Pre.empty() != Y.Pre.empty())
		{
			return X.Pre.empty() ? 1 : -1;
		}
		for (std::size_t I = 0; I < std::min(X.Pre.size(), Y.Pre.size()); ++I)
		{
			if (X.Pre[I] != Y.Pre[I])
			{
				bool D = Digits(X.Pre[I]), E = Digits(Y.Pre[I]);
				if (D != E)
				{
					return D ? -1 : 1;
				}
				if (D && X.Pre[I].size() != Y.Pre[I].size())
				{
					return X.Pre[I].size() < Y.Pre[I].size() ? -1 : 1;
				}
				return X.Pre[I] < Y.Pre[I] ? -1 : 1;
			}
		}
		return X.Pre.size() == Y.Pre.size() ? 0 : X.Pre.size() < Y.Pre.size() ? -1 : 1;
	}
	bool Satisfies(const std::string& Value, const std::string& Requirement)
	{
		Version(Value);
		if (Requirement == "*")
		{
			return true;
		}
		if (Requirement.empty())
		{
			Fail("invalid-requirement", "empty version requirement");
		}
		std::istringstream Stream(Requirement);
		std::string Term;
		bool Result = true, Any = false;
		while (Stream >> Term)
		{
			Any = true;
			std::size_t N = 0;
			while (N < Term.size() && (Term[N] == '<' || Term[N] == '>' || Term[N] == '='))
			{
				++N;
			}
			auto Op = Term.substr(0, N);
			if (Op != "" && Op != "=" && Op != "<" && Op != ">" && Op != "<=" && Op != ">=")
			{
				Fail("invalid-requirement", "unsupported version comparison", Requirement);
			}
			int C = CompareVersions(Value, Term.substr(N));
			Result = Result && (Op == "" || Op == "=" ? C == 0
								: Op == "<"			  ? C < 0
								: Op == ">"			  ? C > 0
								: Op == "<="		  ? C <= 0
													  : C >= 0);
		}
		if (!Any)
		{
			Fail("invalid-requirement", "empty version requirement");
		}
		return Result;
	}
	void ValidateRequirements(const std::string& R)
	{
		(void)Satisfies("0.1.1", R);
	}
	J TargetJson(const FTargetContext& T)
	{
		return {{"core", T.Core},
				{"coreBuild", T.CoreBuild},
				{"os", T.Os},
				{"arch", T.Architecture},
				{"environment", T.Environment},
				{"abis", T.Abis},
				{"minimumVersions", T.MinimumVersions},
				{"capabilities", T.Capabilities}};
	}
	FTargetContext ReadTarget(const J& V)
	{
		FTargetContext T;
		T.Core = V.at("core");
		T.CoreBuild = V.at("coreBuild");
		T.Os = V.at("os");
		T.Architecture = V.at("arch");
		T.Environment = V.at("environment");
		T.Abis = V.value("abis", std::map<std::string, std::string>{});
		T.MinimumVersions = V.value("minimumVersions", std::map<std::string, std::string>{});
		T.Capabilities = V.value("capabilities", std::vector<std::string>{});
		return T;
	}
	void ValidateTarget(const J& T, const std::string& F)
	{
		Keys(T, {"os", "arch", "environment", "abis", "coreBuild", "minimumVersions", "capabilities"}, F);
		for (const auto* K : {"os", "arch", "environment", "coreBuild"})
		{
			if (T.contains(K))
			{
				String(T[K], F + "." + K);
			}
		}
		for (const auto* K : {"abis", "minimumVersions"})
		{
			if (T.contains(K))
			{
				if (!T[K].is_object())
				{
					Fail("manifest-type", "expected string map", F + "." + K);
				}
				for (const auto& S : T[K])
				{
					String(S, F + "." + K);
				}
			}
		}
		if (T.contains("capabilities"))
		{
			Strings(T["capabilities"], F + ".capabilities");
		}
	}
	bool MatchesTarget(const J& T, const FTargetContext& C)
	{
		if (T.is_null() || T.empty())
		{
			return true;
		}
		for (auto Pair : {std::pair<const char*, std::string>{"os", C.Os},
						  {"arch", C.Architecture},
						  {"environment", C.Environment},
						  {"coreBuild", C.CoreBuild}})
		{
			if (T.contains(Pair.first) && T[Pair.first] != Pair.second)
			{
				return false;
			}
		}
		if (T.contains("abis"))
		{
			for (auto I = T["abis"].begin(); I != T["abis"].end(); ++I)
			{
				auto X = C.Abis.find(I.key());
				if (X == C.Abis.end() || I.value() != X->second)
				{
					return false;
				}
			}
		}
		if (T.contains("minimumVersions"))
		{
			for (auto I = T["minimumVersions"].begin(); I != T["minimumVersions"].end(); ++I)
			{
				auto X = C.MinimumVersions.find(I.key());
				if (X == C.MinimumVersions.end() || CompareVersions(X->second, I.value()) < 0)
				{
					return false;
				}
			}
		}
		if (T.contains("capabilities"))
		{
			for (const auto& V : T["capabilities"])
			{
				if (std::find(C.Capabilities.begin(), C.Capabilities.end(), V.get<std::string>()) ==
					C.Capabilities.end())
				{
					return false;
				}
			}
		}
		return true;
	}
	void ValidateDependency(const J& D, const std::string& F, bool Distribution)
	{
		Keys(D, {"version", "source", "usage", "optional", "when", "archive", "directory", "url", "sha256"},
			 F);
		if (!D.contains("version"))
		{
			Fail("missing-field", "dependency version is required", F + ".version");
		}
		String(D["version"], F + ".version");
		ValidateRequirements(D["version"]);
		unsigned Sources = 0;
		for (const auto* K : {"source", "archive", "directory", "url"})
		{
			if (D.contains(K))
			{
				String(D[K], F + "." + K);
				if (D[K].get<std::string>().empty())
				{
					Fail("invalid-source", "empty dependency source", F);
				}
				++Sources;
			}
		}
		if (Sources > 1)
		{
			Fail("source-conflict", "dependency has multiple sources", F);
		}
		if (Distribution && (D.contains("directory") || D.contains("archive")))
		{
			Fail("nonportable-source", "distribution dependencies cannot use local paths", F);
		}
		if (D.contains("url"))
		{
			if (!D.contains("sha256") || !Digest(D["sha256"].get<std::string>()))
			{
				Fail("missing-digest", "URL dependency requires sha256", F);
			}
		}
		if (D.contains("sha256") && (!D["sha256"].is_string() || !Digest(D["sha256"].get<std::string>())))
		{
			Fail("invalid-digest", "expected lowercase SHA-256", F + ".sha256");
		}
		if (D.contains("optional") && !D["optional"].is_boolean())
		{
			Fail("manifest-type", "optional must be boolean", F);
		}
		if (D.contains("when"))
		{
			Keys(D["when"], {"os", "arch", "environment"}, F + ".when");
			for (const auto& X : D["when"])
			{
				String(X, F + ".when");
			}
		}
		if (D.contains("usage"))
		{
			Strings(D["usage"], F + ".usage");
			if (D["usage"].empty())
			{
				Fail("invalid-usage", "usage cannot be empty", F);
			}
			for (const auto& U : D["usage"])
			{
				if (U != "runtime" && U != "compile" && U != "development")
				{
					Fail("invalid-usage", "unknown dependency execution domain", F);
				}
			}
		}
	}
	bool NeedsInstallationProcessing(const J& M)
	{
		auto Install = M.value("install", J::object());
		if (!Install.value("actions", J::array()).empty() || !Install.value("hooks", J::array()).empty())
		{
			return true;
		}
		for (const auto& F : M.value("requires", J::object()).value("features", J::array()))
		{
			if (F.at("id") != "veroscript.package-manager")
			{
				return true;
			}
		}
		return false;
	}
	J Manifest(const J& M)
	{
		Keys(M, {"format",	  "formatVersion", "name",		 "version",		 "displayName", "description",
				 "authors",	  "homepage",	   "repository", "license",		 "licenseFile", "readme",
				 "icon",	  "categories",	   "tags",		 "requires",	 "target",		"dependencies",
				 "conflicts", "entries",	   "resources",	 "localization", "files",		"exclude",
				 "install",	  "extensions"},
			 "package");
		if (M.value("format", "") != "veroscript-package" || M.value("formatVersion", "") != FormatVersion)
		{
			Fail("manifest-format", "expected veroscript-package 0.1.1");
		}
		if (!M.contains("name") || !M["name"].is_string() || !Name(M["name"].get<std::string>()))
		{
			Fail("package-name", "invalid canonical package name", "name");
		}
		if (!M.contains("version"))
		{
			Fail("missing-field", "version is required", "version");
		}
		String(M["version"], "version");
		Version(M["version"]);
		for (const auto* K : {"displayName", "description", "homepage", "repository", "license"})
		{
			if (M.contains(K))
			{
				String(M[K], K);
			}
		}
		for (const auto* K : {"licenseFile", "readme", "icon"})
		{
			if (M.contains(K))
			{
				PathField(M[K], K);
			}
		}
		for (const auto* K : {"categories", "tags", "files", "exclude"})
		{
			if (M.contains(K))
			{
				Strings(M[K], K);
			}
		}
		if (M.contains("authors"))
		{
			EntryArray(M["authors"], "authors");
			for (const auto& A : M["authors"])
			{
				Keys(A, {"name", "email", "url"}, "authors");
				if (!A.contains("name"))
				{
					Fail("missing-field", "author name is required");
				}
				for (const auto& X : A)
				{
					String(X, "authors");
				}
			}
		}
		if (M.contains("requires"))
		{
			const auto& R = M["requires"];
			Keys(R, {"core", "features"}, "requires");
			if (R.contains("core"))
			{
				String(R["core"], "requires.core");
				ValidateRequirements(R["core"]);
			}
			if (R.contains("features"))
			{
				EntryArray(R["features"], "requires.features");
				std::set<std::string> S;
				for (const auto& F : R["features"])
				{
					Keys(F, {"id", "version"}, "requires.features");
					String(F.at("id"), "feature.id");
					String(F.at("version"), "feature.version");
					ValidateRequirements(F["version"]);
					if (!S.insert(F["id"]).second)
					{
						Fail("duplicate-feature", "duplicate required feature");
					}
				}
			}
		}
		if (M.contains("target"))
		{
			ValidateTarget(M["target"], "target");
		}
		for (const auto* K : {"dependencies", "conflicts"})
		{
			if (M.contains(K))
			{
				if (!M[K].is_object())
				{
					Fail("manifest-type", "expected a named map", K);
				}
				for (auto I = M[K].begin(); I != M[K].end(); ++I)
				{
					if (!Name(I.key()))
					{
						Fail("package-name", "invalid dependency package name", I.key());
					}
					if (std::string(K) == "dependencies")
					{
						ValidateDependency(I.value(), "dependencies." + I.key(), true);
					}
					else
					{
						String(I.value(), "conflicts." + I.key());
						ValidateRequirements(I.value());
					}
				}
			}
		}
		if (M.contains("resources"))
		{
			if (!M["resources"].is_object())
			{
				Fail("manifest-type", "expected resource map");
			}
			for (auto I = M["resources"].begin(); I != M["resources"].end(); ++I)
			{
				if (I.key() == "generated" || !Name(I.key()))
				{
					Fail("resource-id", "invalid resource group", I.key());
				}
				PathField(I.value(), "resources." + I.key(), true);
			}
		}
		if (M.contains("localization"))
		{
			const auto& L = M["localization"];
			Keys(L, {"root", "baseCulture"}, "localization");
			PathField(L.at("root"), "localization.root", true);
			if (L.value("baseCulture", "") != "en-US")
			{
				Fail("localization-baseline", "localization requires en-US baseline");
			}
		}
		if (M.contains("extensions"))
		{
			if (!M["extensions"].is_object())
			{
				Fail("manifest-type", "extensions must be an object");
			}
			for (auto I = M["extensions"].begin(); I != M["extensions"].end(); ++I)
			{
				if (I.key().find('.') == std::string::npos || !I.value().is_object())
				{
					Fail("extension-namespace", "extension data requires a namespace and object", I.key());
				}
			}
		}
		if (M.contains("entries"))
		{
			const auto& E = M["entries"];
			Keys(E, {"modules", "native", "compiler", "backends", "libraries"}, "entries");
			if (E.contains("modules"))
			{
				if (!E["modules"].is_object())
				{
					Fail("manifest-type", "modules must be an object");
				}
				std::set<std::string> Names;
				for (auto I = E["modules"].begin(); I != E["modules"].end(); ++I)
				{
					CheckPath(I.key());
					PathField(I.value(), "entries.modules");
					std::string N = I.key();
					std::transform(N.begin(), N.end(), N.begin(),
						[](unsigned char C)
						{
							return char(std::tolower(C));
						});
					if (!Names.insert(N).second)
					{
						Fail("module-conflict", "case-ambiguous module names");
					}
					auto S = U8Path(I.value().get<std::string>()).extension();
					if (S != ".vs" && S != ".vslib")
					{
						Fail("module-format", "module entry must be .vs or .vslib");
					}
				}
			}
			for (const auto* K : {"native", "libraries", "compiler", "backends"})
			{
				if (E.contains(K))
				{
					EntryArray(E[K], K);
					std::set<std::string> Seen;
					for (const auto& X : E[K])
					{
						std::string Kind = K;
						if (Kind == "native" || Kind == "libraries")
						{
							if (Kind == "native")
							{
								Keys(X,
									 {"id", "path", "linkage", "roles", "catalog", "componentVersion",
									  "libraries"},
									 K);
							}
							else
							{
								Keys(X, {"id", "path", "linkage"}, K);
							}
							String(X.at("id"), "entry.id");
							if (!Name(X["id"].get<std::string>()) || !Seen.insert(X["id"]).second)
							{
								Fail("entry-id", "invalid or duplicate entry ID");
							}
							PathField(X.at("path"), "entry.path");
							if (X.value("linkage", "") != "dynamic" && X.value("linkage", "") != "static")
							{
								Fail("entry-linkage", "entry linkage must be dynamic or static");
							}
							if (Kind == "native")
							{
								Strings(X.at("roles"), "entry.roles");
								if (X["roles"].empty())
								{
									Fail("entry-role", "native roles cannot be empty");
								}
								for (const auto& R : X["roles"])
								{
									if (R != "bindings" && R != "compiler" && R != "provider" && R != "cli")
									{
										Fail("entry-role", "unknown native role");
									}
								}
								if (X.contains("catalog"))
								{
									PathField(X["catalog"], "entry.catalog");
								}
								if (X.contains("componentVersion"))
								{
									String(X["componentVersion"], "entry.componentVersion");
									Version(X["componentVersion"]);
								}
								if (X.contains("libraries"))
								{
									Strings(X["libraries"], "entry.libraries");
								}
							}
						}
						else if (Kind == "compiler")
						{
							Keys(X, {"path", "registrationEntry"}, K);
							PathField(X.at("path"), "compiler.path");
							if (X.contains("registrationEntry"))
							{
								String(X["registrationEntry"], "compiler.registrationEntry");
							}
						}
						else
						{
							Keys(X, {"service", "id", "module", "path", "contract", "libraries"}, K);
							String(X.at("service"), "backend.service");
							String(X.at("id"), "backend.id");
							if (!Name(X["service"].get<std::string>()) || !Name(X["id"].get<std::string>())
								|| X["id"] == "auto" || X["id"] == "none" ||
								!Seen.insert(X["service"].get<std::string>() + ":" +
											 X["id"].get<std::string>())
									 .second)
							{
								Fail("backend-id", "invalid or duplicate backend ID");
							}
							if (X.contains("module"))
							{
								String(X["module"], "backend.module");
								if (X.contains("path") || X.contains("contract"))
								{
									Fail("backend-form", "backend forms are mutually exclusive");
								}
								bool Found = false;
								for (const auto& N : E.value("native", J::array()))
								{
									if (N["id"] == X["module"] &&
										std::find(N["roles"].begin(), N["roles"].end(), "provider") !=
											N["roles"].end())
									{
										Found = true;
									}
								}
								if (!Found)
								{
									Fail("backend-module", "backend references no provider module");
								}
							}
							else
							{
								PathField(X.at("path"), "backend.path");
								if ((X["service"] != "jit" && X["service"] != "vm")
									|| X["service"] != X.value("contract", ""))
								{
									Fail("backend-contract", "unsupported dedicated backend protocol");
								}
							}
							if (X.contains("libraries"))
							{
								Strings(X["libraries"], "backend.libraries");
							}
						}
					}
				}
			}
			bool Native = !E.value("native", J::array()).empty() || !E.value("libraries", J::array()).empty();
			for (const auto& B : E.value("backends", J::array()))
			{
				Native = Native || B.contains("path");
			}
			if (Native)
			{
				if (!M.contains("target") || !M["target"].contains("os") || !M["target"].contains("arch") ||
					!M["target"].contains("environment"))
				{
					Fail("native-target",
						 "native packages require explicit OS, architecture and environment");
				}
				if (!E.value("native", J::array()).empty() &&
					(!M["target"].contains("coreBuild") ||
					 !M["target"].value("abis", J::object()).contains("native-extension")))
				{
					Fail("native-abi", "native extensions require coreBuild and native-extension ABI");
				}
				for (const auto& B : E.value("backends", J::array()))
				{
					if (B.contains("path") && !M["target"].value("abis", J::object()).contains(B.at("contract").get<std::string>()))
					{
						Fail("native-abi", "backend requires its dedicated protocol ABI");
					}
				}
			}
		}
		if (M.contains("install"))
		{
			const auto& I = M["install"];
			Keys(I, {"messages", "inputs", "actions", "hooks"}, "install");
			if (I.contains("messages"))
			{
				Keys(I["messages"], {"before", "after", "readme", "license"}, "install.messages");
				for (auto X = I["messages"].begin(); X != I["messages"].end(); ++X)
				{
					if (X.key() == "readme" || X.key() == "license")
					{
						PathField(X.value(), "install.messages." + X.key());
					}
					else
					{
						String(X.value(), "install.messages." + X.key());
					}
				}
			}
			if (I.contains("inputs"))
			{
				if (!I["inputs"].is_object())
				{
					Fail("manifest-type", "inputs must be object");
				}
				for (auto X = I["inputs"].begin(); X != I["inputs"].end(); ++X)
				{
					if (X.key().empty() || X.key().size() > 64 ||
						!std::all_of(X.key().begin(), X.key().end(),
							[](char C)
							{
								return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
									(C >= '0' && C <= '9') || C == '_' || C == '-';
							}))
					{
						Fail("input-name", "invalid input name");
					}
					const auto& D = X.value();
					Keys(D, {"type", "required", "default", "description", "sensitive", "choices"}, "input");
					auto T = D.value("type", "");
					if (T != "string" && T != "boolean" && T != "integer" && T != "enum")
					{
						Fail("input-type", "unsupported input type");
					}
					for (auto K : {"required", "sensitive"})
					{
						if (D.contains(K) && !D[K].is_boolean())
						{
							Fail("manifest-type", "input flags must be booleans");
						}
					}
					if (D.contains("description"))
					{
						String(D["description"], "input.description");
					}
					if (D.value("sensitive", false) && D.contains("default"))
					{
						Fail("secret-default", "sensitive defaults cannot be published");
					}
					if (T == "enum")
					{
						Strings(D.at("choices"), "input.choices");
						if (D["choices"].empty())
						{
							Fail("input-type", "enum choices cannot be empty");
						}
					}
					if (D.contains("default"))
					{
						const auto& V = D["default"];
						bool Valid = T == "string"	  ? V.is_string()
									 : T == "boolean" ? V.is_boolean()
									 : T == "integer" ? V.is_number_integer()
													  : V.is_string() && std::find(D["choices"].begin(),
																				   D["choices"].end(),
																				   V) != D["choices"].end();
						if (!Valid)
						{
							Fail("input-default", "default input has the wrong type or enum value", X.key());
						}
					}
				}
			}
			if (I.contains("actions"))
			{
				EntryArray(I["actions"], "install.actions");
				std::set<std::string> Dests;
				for (const auto& A : I["actions"])
				{
					Keys(A, {"type", "source", "destination"}, "action");
					if (A.value("type", "") != "copy" && A.value("type", "") != "template")
					{
						Fail("action-type", "unsupported install action");
					}
					PathField(A.at("source"), "action.source");
					PathField(A.at("destination"), "action.destination");
					auto Destination = A.at("destination").get<std::string>();
					if (Destination == "loaders" || Destination.rfind("loaders/", 0) == 0 ||
						Destination.rfind(".vero-", 0) == 0)
					{
						Fail("generated-reserved", "action destination uses installer-owned paths");
					}
					if (!Dests.insert(A["destination"]).second)
					{
						Fail("action-conflict", "duplicate action output");
					}
				}
			}
			if (I.contains("hooks"))
			{
				EntryArray(I["hooks"], "install.hooks");
				for (const auto& H : I["hooks"])
				{
					Keys(H, {"phase", "path", "entry", "permissions"}, "hook");
					auto P = H.value("phase", "");
					if (P != "pre-install" && P != "post-install" && P != "pre-uninstall" &&
						P != "post-uninstall")
					{
						Fail("hook-phase", "invalid hook phase");
					}
					PathField(H.at("path"), "hook.path");
					String(H.at("entry"), "hook.entry");
					if (H.contains("permissions"))
					{
						Strings(H["permissions"], "hook.permissions");
					}
				}
			}
		}
		return M;
	}
	J Project(const J& P)
	{
		Keys(P,
			 {"format", "formatVersion", "name", "version", "dependencies", "packages", "services",
			  "packageManager", "extensions"},
			 "project");
		if (P.value("format", "") != "veroscript-project" || P.value("formatVersion", "") != FormatVersion)
		{
			Fail("project-format", "expected veroscript-project 0.1.1");
		}
		if (!P.contains("name") || !P["name"].is_string() || !Name(P["name"].get<std::string>()))
		{
			Fail("package-name", "invalid project name");
		}
		Version(P.at("version"));
		if (P.contains("dependencies"))
		{
			if (!P["dependencies"].is_object())
			{
				Fail("manifest-type", "dependencies must be object");
			}
			for (auto I = P["dependencies"].begin(); I != P["dependencies"].end(); ++I)
			{
				if (!Name(I.key()))
				{
					Fail("package-name", "invalid dependency name");
				}
				ValidateDependency(I.value(), "dependencies." + I.key(), false);
			}
		}
		if (P.contains("packages"))
		{
			if (!P["packages"].is_object())
			{
				Fail("manifest-type", "packages must be object");
			}
			for (auto I = P["packages"].begin(); I != P["packages"].end(); ++I)
			{
				if (!Name(I.key()))
				{
					Fail("package-name", "invalid package state name");
				}
				Keys(I.value(), {"enabled", "configuration"}, "packages." + I.key());
				if (I.value().contains("enabled") && !I.value()["enabled"].is_boolean())
				{
					Fail("manifest-type", "enabled must be boolean");
				}
				if (I.value().contains("configuration") && !I.value()["configuration"].is_object())
				{
					Fail("manifest-type", "configuration must be object");
				}
			}
		}
		if (P.contains("services"))
		{
			if (!P["services"].is_object())
			{
				Fail("manifest-type", "services must be object");
			}
			for (auto I = P["services"].begin(); I != P["services"].end(); ++I)
			{
				Keys(I.value(), {"backend", "configuration"}, "services." + I.key());
				if (I.value().contains("backend"))
				{
					String(I.value()["backend"], "backend");
				}
			}
		}
		if (P.contains("packageManager"))
		{
			const auto& X = P["packageManager"];
			Keys(X, {"directory", "registries", "distribution"}, "packageManager");
			if (X.contains("directory"))
			{
				String(X["directory"], "packageManager.directory");
			}
			if (X.contains("registries"))
			{
				if (!X["registries"].is_object())
				{
					Fail("manifest-type", "registries must be object");
				}
				for (auto I = X["registries"].begin(); I != X["registries"].end(); ++I)
				{
					Keys(I.value(), {"id", "api", "mirror", "allowOriginFallback", "trustedMirror"},
						 "registry");
					String(I.value().at("id"), "registry.id");
					String(I.value().at("api"), "registry.api");
					if (I.value().contains("mirror"))
					{
						String(I.value()["mirror"], "registry.mirror");
					}
					for (auto K : {"allowOriginFallback", "trustedMirror"})
					{
						if (I.value().contains(K) && !I.value()[K].is_boolean())
						{
							Fail("manifest-type", "registry flags must be boolean");
						}
					}
				}
			}
		}
		return P;
	}
} // namespace VeroScript::Packages::Detail

namespace VeroScript::Packages
{
	bool VersionSatisfies(std::string_view V, std::string_view R, bool& Out, FDiagnostic& E)
	{
		try
		{
			E = {};
			Out = Detail::Satisfies(std::string(V), std::string(R));
			return true;
		}
		catch (const Detail::FFailure& X)
		{
			E = X.Diagnostic;
			Out = false;
			return false;
		}
		catch (const std::exception& X)
		{
			E.Code = "invalid-version";
			E.Message = X.what();
			Out = false;
			return false;
		}
	}
	bool ValidateDistribution(std::string_view S, std::string& Out, FDiagnostic& E, const FLimits& L)
	{
		try
		{
			E = {};
			Out = Detail::Manifest(Detail::Parse(S, L)).dump();
			return true;
		}
		catch (const Detail::FFailure& X)
		{
			E = X.Diagnostic;
			Out.clear();
			return false;
		}
		catch (const std::exception& X)
		{
			E.Code = "invalid-manifest";
			E.Message = X.what();
			Out.clear();
			return false;
		}
	}
} // namespace VeroScript::Packages
