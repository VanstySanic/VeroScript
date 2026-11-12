// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "BuildInternal.h"
#include "VeroScriptPath.h"
#include <algorithm>
#include <fstream>
#include <regex>

namespace VeroScript::Build::Detail
{
	FFailure::FFailure(std::string Code, std::string Message, std::string Path)
	    : std::runtime_error(Message), Diagnostic{std::move(Code), std::move(Message), {}, std::move(Path), {}}
	{
	}
	[[noreturn]] void Fail(const std::string& Code, const std::string& Message, const std::string& Path)
	{
		throw FFailure(Code, Message, Path);
	}
	void Require(bool Ok, const Application::FDiagnostic& D)
	{
		if (!Ok)
		{
			Fail(D.Code, D.Message, D.Path);
		}
	}
	J Parse(std::string_view S, const FLimits& L)
	{
		if (S.size() > L.Content.MaxMetadataBytes)
		{
			Fail("metadata-budget", "build metadata exceeds byte budget");
		}
		std::vector<std::set<std::string>> Stack;
		try
		{
			return J::parse(S, [&](int Depth, J::parse_event_t Event, J& V)
			{
				if (L.Content.CheckCancelled)
				{
					L.Content.CheckCancelled();
				}
				if (Depth < 0 || static_cast<std::size_t>(Depth) > L.Content.MaxJsonDepth)
				{
					Fail("json-depth", "build metadata exceeds nesting budget");
				}
				if (Event == J::parse_event_t::object_start)
				{
					Stack.emplace_back();
				}
				else if (Event == J::parse_event_t::key && !Stack.back().insert(V.get<std::string>()).second)
				{
					Fail("json-key", "duplicate JSON key");
				}
				else if (Event == J::parse_event_t::object_end)
				{
					Stack.pop_back();
				}
				return true;
			});
		}
		catch (const J::exception& E)
		{
			Fail("json", E.what());
		}
	}
	void Keys(const J& V, std::initializer_list<const char*> Allowed, const std::string& Field)
	{
		if (!V.is_object())
		{
			Fail("object", "expected an object", Field);
		}
		for (auto I = V.begin(); I != V.end(); ++I)
		{
			if (std::none_of(Allowed.begin(), Allowed.end(), [&](const auto* K)
			{
				return K == I.key();
			}))
			{
				Fail("unknown-field", "unknown build field", Field + "." + I.key());
			}
		}
	}
	void Id(const std::string& S)
	{
		static const std::regex Pattern("[A-Za-z_][A-Za-z0-9_.-]{0,127}");
		if (!std::regex_match(S, Pattern))
		{
			Fail("identifier", "invalid build identifier", S);
		}
	}
	void Safe(const std::string& S)
	{
		std::string E;
		if (!Application::ValidateContentPath(S, E))
		{
			Fail("build-path", E, S);
		}
	}
	std::string PhysicalIdentity(const fs::path& P)
	{
		auto S = PathUtil::IdentityKey(NativePathText(P.lexically_normal()));
		#if defined(_WIN32)
		if (S.rfind("//?/unc/", 0) == 0)
		{
			S = "//" + S.substr(8);
		}
		else if (S.rfind("//?/", 0) == 0)
		{
			S.erase(0, 4);
		}
		#endif
		return S;
	}
	bool Under(const fs::path& A, const fs::path& B)
	{
		const auto Child = PhysicalIdentity(A), Parent = PhysicalIdentity(B);
		return Child == Parent || (!Parent.empty() && Child.rfind(Parent + (Parent.back() == '/' ? "" : "/"), 0) == 0);
	}
	fs::path TreePath(const fs::path& P)
	{
		auto Result = PathUtil::FromUtf8(NativePathText(P.lexically_normal()));
		#if defined(_WIN32)
		const auto Native = Result.make_preferred().native();
		if (Result.is_absolute() && Native.rfind(L"\\\\?\\", 0) != 0)
		{
			return fs::path(Native.rfind(L"\\\\", 0) == 0 ? L"\\\\?\\UNC\\" + Native.substr(2) : L"\\\\?\\" + Native);
		}
		#endif
		return Result;
	}
	void RemoveOwnedTree(const fs::path& P, const fs::path& OwnedRoot)
	{
		if (!P.is_absolute() || !OwnedRoot.is_absolute() || !Under(P, OwnedRoot))
		{
			Fail("cleanup-path", "cleanup escaped the owned staging or publication directory", NativePathText(P));
		}
		const auto Native = TreePath(P);
		if (!fs::exists(Native)) return;
		Physical(Native);
		for (const auto& Entry : fs::recursive_directory_iterator(Native))
		{
			Physical(Entry.path());
		}
		fs::remove_all(Native);
	}
	fs::path Absolute(const std::string& S, const fs::path& Base)
	{
		if (S.empty() || S.find('\0') != std::string::npos)
		{
			Fail("path", "empty or invalid physical path");
		}
		const auto P = PathUtil::FromUtf8(S);
		return PathUtil::FromUtf8(NativePathText((P.is_absolute() ? P : Base / P).lexically_normal()));
	}
	void Physical(const fs::path& P, bool Missing)
	{
		Application::FDiagnostic E;
		Require(Application::CheckPhysicalPath(NativePathText(P), Missing, E), E);
	}
	std::string Read(const fs::path& P, const FLimits& L)
	{
		Physical(P);
		FArtifactData D;
		std::string E;
		if (!ReadArtifactFile({NativePathText(P), "generic", {}}, {L.Content.MaxFileBytes, L.Content.CheckCancelled}, D, E))
		{
			Fail("read", E, NativePathText(P));
		}
		return {D.Bytes.begin(), D.Bytes.end()};
	}
	void Write(const fs::path& P, std::string_view S)
	{
		Physical(P, true);
		if (fs::exists(P))
		{
			Fail("output-exists", "build will not overwrite an existing staging file", NativePathText(P));
		}
		fs::create_directories(P.parent_path());
		std::ofstream O(P, std::ios::binary);
		if (!O || !O.write(S.data(), static_cast<std::streamsize>(S.size())) || !O.flush())
		{
			Fail("write", "cannot write build output", NativePathText(P));
		}
	}
	std::vector<Application::FContentFile> Tree(const fs::path& P, const FLimits& L)
	{
		Application::FDiagnostic E;
		std::vector<Application::FContentFile> F;
		Require(Application::ReadContentTree(NativePathText(P), F, E, L.Content), E);
		return F;
	}
	std::string Fingerprint(const fs::path& P, const FLimits& L)
	{
		Physical(P);
		if (fs::is_regular_file(P))
		{
			std::string Hash;
			Application::FDiagnostic E;
			Require(Application::FileSha256(NativePathText(P), Hash, E, L.Content), E);
			return Hash;
		}
		if (!fs::is_directory(P))
		{
			Fail("input-type", "unsupported build input type", NativePathText(P));
		}
		J Files = J::array();
		for (const auto& F : Tree(P, L))
		{
			Files.push_back({F.Path, Application::ContentSha256(F.Bytes), F.Executable});
		}
		return Application::ContentSha256(Files.dump());
	}
	void Snapshot(const fs::path& P, std::map<std::string, std::string>& Inputs, const FLimits& L)
	{
		const auto Hash = Fingerprint(P, L);
		const auto Existing = Inputs.find(NativePathText(P));
		if (Existing != Inputs.end() && Existing->second != Hash)
		{
			Fail("plan-stale", "build input changed after it was frozen", NativePathText(P));
		}
		Inputs.emplace(NativePathText(P), Hash);
		if (Inputs.size() > L.Content.MaxFiles)
		{
			Fail("input-budget", "too many build inputs");
		}
	}
	void Copy(const fs::path& Source, const fs::path& Destination, const FLimits& L)
	{
		Physical(Source);
		Physical(Destination, true);
		if (fs::exists(Destination))
		{
			Fail("output-collision", "two inputs map to the same output", NativePathText(Destination));
		}
		if (fs::is_directory(Source))
		{
			if (Under(Destination, Source))
			{
				Fail("recursive-copy", "copy input contains its destination", NativePathText(Source));
			}
			Application::FDiagnostic E;
			Require(Application::WriteContentTree(Tree(Source, L), NativePathText(Destination), E, L.Content), E);
		}
		else
		{
			const auto S = Read(Source, L);
			Write(Destination, S);
			#if !defined(_WIN32)
			fs::permissions(Destination, fs::status(Source).permissions());
			#endif
		}
	}
}
