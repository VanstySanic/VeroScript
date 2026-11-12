// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInstalledInternal.h"
#include <fstream>

namespace VeroScript::Packages::Detail
{
	namespace
	{
		std::pair<std::string, J> Library(const FGraph& G, const std::string& Package, const std::string& Ref)
		{
			auto Colon = Ref.find(':');
			auto Name = Colon == std::string::npos ? Package : Ref.substr(0, Colon);
			auto Id = Colon == std::string::npos ? Ref : Ref.substr(Colon + 1);
			if (Name != Package && (!G.Edges.count(Package) || !G.Edges.at(Package).count(Name)))
			{
				Fail("library-dependency", "native library reference has no direct dependency edge", Ref);
			}
			auto N = G.Nodes.find(Name);
			if (N == G.Nodes.end())
			{
				Fail("library-package", "native library dependency is not installed", Ref);
			}
			for (const auto& L :
				 N->second.Manifest.value("entries", J::object()).value("libraries", J::array()))
			{
				if (L.at("id") == Id)
				{
					return {Name, L};
				}
			}
			Fail("library-missing", "native library entry does not exist", Ref);
		}
	} // namespace
	void ValidateLibraries(const FGraph& G)
	{
		for (const auto& Pair : G.Nodes)
		{
			auto E = Pair.second.Manifest.value("entries", J::object());
			for (const auto* K : {"native", "backends"})
			{
				for (const auto& N : E.value(K, J::array()))
				{
					for (const auto& Ref : N.value("libraries", std::vector<std::string>{}))
					{
						Library(G, Pair.first, Ref);
					}
				}
			}
		}
	}
	J PrepareLoaders(const FGraph& G, const std::string& Name, const fs::path& Generated, FWork& W)
	{
		J Result = J::object();
		const auto& C = G.Nodes.at(Name);
		auto E = C.Manifest.value("entries", J::object());
		for (const auto* K : {"native", "backends"})
		{
			for (const auto& N : E.value(K, J::array()))
			{
				if (!N.contains("path") || N.value("linkage", "") == "static")
				{
					continue;
				}
				std::string Key = std::string(K) + ":"
					+ (std::string_view(K) == "backends" ? N.at("service").get<std::string>() + ":" : "")
					+ N.at("id").get<std::string>();
				std::string Stem = Hash(Key);
				auto Base = Generated / "loaders" / Stem;
				fs::create_directories(Base);
				std::map<std::string, std::string> Names;
				J Libraries = J::array(), Hashes = J::object();
				const auto Copy = [&](const fs::path& Src)
				{
					W.Check();
					NoLinks(Src);
					auto Name = NativePathText(Src.filename());
					CheckPath(Name);
					auto Hash = HashFile(Src, W, W.Options.Limits.MaxFileBytes);
					auto I = Names.find(Name);
					if (I != Names.end())
					{
						if (I->second != Hash)
						{
							Fail("library-conflict", "same loader filename has different content", Name);
						}
						return;
					}
					Names[Name] = Hash;
					auto Dest = Base / U8Path(Name);
					std::ifstream In(Src, std::ios::binary);
					std::ofstream Out(Dest, std::ios::binary);
					char Buffer[65536];
					while (In)
					{
						W.Check();
						In.read(Buffer, sizeof Buffer);
						if (!Out.write(Buffer, In.gcount()))
						{
							Fail("library-copy", "failed preparing immutable loader directory",
								 NativePathText(Dest));
						}
					}
					Out.close();
					Hashes[PathUtil::ToUtf8(fs::path("loaders") / Stem / U8Path(Name))] = Hash;
					if (!In.eof() || HashFile(Dest, W, W.Options.Limits.MaxFileBytes) != Hash)
					{
						Fail("source-changed", "native file changed while preparing loader view");
					}
				};
				Copy(C.Content / U8Path(N.at("path").get<std::string>()));
				for (const auto& Ref : N.value("libraries", std::vector<std::string>{}))
				{
					auto L = Library(G, Name, Ref);
					if (L.second.value("linkage", "") != "dynamic")
					{
						continue;
					}
					auto Src = G.Nodes.at(L.first).Content / U8Path(L.second.at("path").get<std::string>());
					Copy(Src);
					Libraries.push_back(PathUtil::ToUtf8(fs::path("loaders") / Stem / Src.filename()));
				}
				Result[Key] = {
					{"path", PathUtil::ToUtf8(fs::path("loaders") / Stem / U8Path(N.at("path").get<std::string>()).filename())},
					{"libraries", Libraries},
					{"files", Hashes}};
			}
		}
		return Result;
	}
	void VerifyLoaders(const fs::path& Root, const J& Loaders, const FWork& W)
	{
		for (const auto& L : Loaders)
		{
			auto Files = L.at("files");
			if (!Files.is_object())
			{
				Fail("loader-receipt", "loader file identities are missing");
			}
			for (auto I = Files.begin(); I != Files.end(); ++I)
			{
				CheckPath(I.key());
				auto P = Root / U8Path(I.key());
				if (!Under(P, Root) || !Digest(I.value().get<std::string>()) ||
					I.value() != HashFile(P, W, W.Options.Limits.MaxFileBytes))
				{
					Fail("loader-integrity", "generated loader file differs from its source identity",
						 I.key());
				}
			}
			auto Paths = L.at("libraries").get<std::vector<std::string>>();
			Paths.push_back(L.at("path"));
			for (const auto& P : Paths)
			{
				if (!Files.contains(P))
				{
					Fail("loader-receipt", "loader entry has no expected content identity", P);
				}
			}
		}
	}
} // namespace VeroScript::Packages::Detail
