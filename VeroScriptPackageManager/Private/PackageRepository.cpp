// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInternal.h"
#include "PackageSignatures.h"
#include "PackageSources.h"
#include <algorithm>
#include <fstream>
#include <thread>

namespace VeroScript::Packages::Detail
{
	namespace
	{
		void Compatible(const J& M, const FEnvironmentLayout& L, const FClientOptions& O)
		{
			if (!MatchesTarget(M.value("target", J::object()), L.Target))
			{
				Fail("target-unavailable", "package target or ABI does not match selected environment");
			}
			const auto R = M.value("requires", J::object());
			if (R.contains("core") && !Satisfies(L.Target.Core, R["core"]))
			{
				Fail("core-version", "package requires a different core version");
			}
			for (const auto& F : R.value("features", J::array()))
			{
				std::string Name = F["id"], Version = F["version"];
				auto I = O.Features.find(Name);
				if (Name == "veroscript.package-manager")
				{
					if (!Satisfies(FormatVersion, Version))
					{
						Fail("installer-feature", "unsupported package manager version");
					}
				}
				else if (I == O.Features.end() || !I->second ||
				    !Satisfies(I->second->GetVersion(), Version))
				{
					Fail("installer-feature", "required installer feature is unavailable", Name);
				}
			}
		}
		J ReadJson(const fs::path& P, const FWork& W, std::uint64_t Limit)
		{
			auto L = W.Options.Limits;
			L.MaxManifestBytes = Limit;
			auto Bytes = Read(P, Limit, &W);
			W.ChargeMetadata(Bytes.size());
			return Parse(Bytes, L);
		}
	} // namespace
	FRepository::FRepository(FEnvironmentLayout L, J P, const FClientOptions& O, FWork& W)
	    : Layout(std::move(L)), Settings(std::move(P)), Options(O), Work(W),
	      Http(O.Transport ? O.Transport : CreateHttpTransport())
	{
	}
	std::vector<FCandidate> FRepository::Candidates(const std::string& Name, const J& Spec, const J& Previous)
	{
		Work.Check();
		auto Src = Source(Spec);
		auto Key = Name + ":" + Id(Src) + ":" + Spec.at("version").get<std::string>();
		auto Found = Cache.find(Key);
		if (Found != Cache.end())
		{
			return Found->second;
		}
		std::vector<FCandidate> Result;
		if (Src["kind"] != "registry")
		{
			Result.push_back(Local(Name, Spec));
			Cache[Key] = Result;
			return Result;
		}
		const auto AddPrevious = [&]()
		{
			if (Previous.is_object() && Previous.value("name", "") == Name &&
			    Src["id"] == Previous.value("source", J::object()).value("id", "") &&
			    Previous.contains("manifest"))
			{
				FCandidate C;
				C.Spec = Spec;
				C.Source = Src;
				C.Manifest = Manifest(Previous["manifest"]);
				C.Artifact = Previous.at("artifact");
				C.OwnerId = Previous.at("ownerId");
				try
				{
					Compatible(C.Manifest, Layout, Options);
				}
				catch (const FFailure& E)
				{
					if (!Options.Locked &&
					    (E.Diagnostic.Code == "core-version" || E.Diagnostic.Code == "installer-feature" ||
					        E.Diagnostic.Code == "target-unavailable"))
					{
						return;
					}
					throw;
				}
				Result.push_back(std::move(C));
			}
		};
		AddPrevious();
		if (Options.Locked)
		{
			if (Result.empty())
			{
				Fail("locked-incomplete", "dependency/source is missing from the exact lock", Name);
			}
			Cache[Key] = Result;
			return Result;
		}
		bool Upgrade = Options.Upgrade &&
		    (!Settings.contains("_upgradeNames") ||
		        std::find(Settings["_upgradeNames"].begin(), Settings["_upgradeNames"].end(), Name) !=
		            Settings["_upgradeNames"].end());
		if (Options.Offline && !Result.empty() && !Upgrade)
		{
			Cache[Key] = Result;
			return Result;
		}
		auto Alias = Src["alias"].get<std::string>();
		auto Package = RequestJson(Alias, "/packages/" + EscapeUrl(Name));
		if (Package.value("format", "") != "veroscript-registry-package" ||
		    Package.value("formatVersion", "") != FormatVersion || Package.value("name", "") != Name ||
		    !Package.contains("versions") || !Package["versions"].is_array())
		{
			Fail("registry-format", "invalid registry package response", Name);
		}
		for (const auto& Summary : Package["versions"])
		{
			Work.Check();
			if (++Work.Candidates > Options.Limits.MaxCandidates)
			{
				Fail("candidate-limit", "version candidate budget exhausted");
			}
			std::string V = Summary.at("version");
			CompareVersions(V, V);
			if (!Satisfies(V, Spec.at("version")))
			{
				continue;
			}
			if (V.find('-') != std::string::npos && !Options.AllowPrerelease &&
			    Spec.at("version").get<std::string>().find('-') == std::string::npos)
			{
				continue;
			}
			if (Summary.value("blocked", false) || Summary.value("withdrawn", false))
			{
				Result.erase(std::remove_if(Result.begin(), Result.end(),
				                 [&](const FCandidate& C)
				{
					return C.Manifest.at("version") == V;
				}),
				    Result.end());
				continue;
			}
			auto Meta = RequestJson(Alias, "/packages/" + EscapeUrl(Name) + "/versions/" + EscapeUrl(V));
			if (Meta.value("format", "") != "veroscript-registry-version" ||
			    Meta.value("formatVersion", "") != FormatVersion || Meta.value("name", "") != Name ||
			    Meta.value("version", "") != V)
			{
				Fail("registry-format", "invalid registry version response", Name);
			}
			if (Meta.value("blocked", false) || Meta.value("withdrawn", false))
			{
				Result.erase(std::remove_if(Result.begin(), Result.end(),
				                 [&](const FCandidate& C)
				{
					return C.Manifest.at("version") == V;
				}),
				    Result.end());
				continue;
			}
			std::vector<J> Artifacts;
			for (const auto& A : Meta.at("artifacts"))
			{
				Keys(A, {"id", "size", "sha256", "manifestSha256", "target", "requires", "blocked", "signatures"},
				    "artifact");
				String(A.at("id"), "artifact.id");
				auto ArtifactId = A["id"].get<std::string>();
				if (ArtifactId.empty() || ArtifactId == "." || ArtifactId == ".." ||
				    !std::all_of(ArtifactId.begin(), ArtifactId.end(),
				        [](char X)
				{
					return (X >= 'a' && X <= 'z') || (X >= 'A' && X <= 'Z') ||
					    (X >= '0' && X <= '9') || X == '.' || X == '_' || X == '-';
				}))
				{
					Fail("artifact-id", "invalid opaque artifact ID");
				}
				if (!A.at("size").is_number_unsigned() ||
				    A["size"].get<std::uint64_t>() > Options.Limits.MaxArchiveBytes ||
				    !Digest(A.at("sha256").get<std::string>()) ||
				    !Digest(A.at("manifestSha256").get<std::string>()))
				{
					Fail("artifact-metadata", "invalid artifact size or digest");
				}
				if (A.contains("target"))
				{
					ValidateTarget(A["target"], "artifact.target");
				}
				if (A.contains("signatures"))
				{
					ValidateDistributionSignatures(A.at("signatures"), A.at("sha256"), A.at("size"), Work);
				}
				if (A.value("blocked", false))
				{
					Result.erase(std::remove_if(Result.begin(), Result.end(),
					                 [&](const FCandidate& C)
					{
						return C.Manifest.at("version") == V &&
						    C.Artifact.at("id") == A.at("id");
					}),
					    Result.end());
				}
				if (!A.value("blocked", false) &&
				    MatchesTarget(A.value("target", J::object()), Layout.Target))
				{
					Artifacts.push_back(A);
				}
			}
			if (Artifacts.size() > 1)
			{
				Fail("artifact-ambiguity", "multiple artifacts match the same target", Name);
			}
			if (Artifacts.empty())
			{
				continue;
			}
			FCandidate C;
			C.MetadataOnly = true;
			C.Spec = Spec;
			C.Source = Src;
			C.Artifact = Artifacts.front();
			C.Manifest = {{"format", "veroscript-package"},
			    {"formatVersion", FormatVersion},
			    {"name", Name},
			    {"version", V},
			    {"dependencies", Meta.value("dependencies", J::object())},
			    {"conflicts", Meta.value("conflicts", J::object())},
			    {"requires", C.Artifact.value("requires", J::object())},
			    {"target", C.Artifact.value("target", J::object())}};
			C.Manifest = Manifest(C.Manifest);
			try
			{
				Compatible(C.Manifest, Layout, Options);
			}
			catch (const FFailure& E)
			{
				if (E.Diagnostic.Code == "core-version" || E.Diagnostic.Code == "installer-feature" ||
				    E.Diagnostic.Code == "target-unavailable")
				{
					continue;
				}
				throw;
			}
			C.OwnerId =
			    "package." +
			    Id(J{
			        {"source", Src["id"]}, {"name", Name}, {"version", V}, {"sha256", C.Artifact["sha256"]}});
			if (!Result.empty() && Result.front().Manifest["version"] == V)
			{
				for (auto Field : {"id", "size", "sha256", "manifestSha256", "target", "requires", "signatures"})
				{
					if (Result.front().Artifact.value(Field, J{}) != C.Artifact.value(Field, J{}))
					{
						Fail("registry-mutated", "locked immutable artifact metadata changed", Name);
					}
				}
				for (auto Field : {"dependencies", "conflicts"})
				{
					if (Result.front().Manifest.value(Field, J::object()) !=
					    C.Manifest.value(Field, J::object()))
					{
						Fail("registry-mutated", "locked immutable dependency metadata changed", Name);
					}
				}
				continue;
			}
			Result.push_back(std::move(C));
		}
		std::stable_sort(Result.begin(), Result.end(),
		    [&](const FCandidate& A, const FCandidate& B)
		{
			if (!Upgrade && !Previous.is_null())
			{
				bool X = A.Manifest["version"] == Previous.value("version", "");
				bool Y = B.Manifest["version"] == Previous.value("version", "");
				if (X != Y)
				{
					return X;
				}
			}
			return CompareVersions(A.Manifest["version"], B.Manifest["version"]) > 0;
		});
		Cache[Key] = Result;
		return Result;
	}
	FCandidate FRepository::Local(const std::string& Expected, J Spec)
	{
		Work.Check();
		FCandidate C;
		C.Spec = Spec;
		C.Source = Source(Spec);
		auto Kind = C.Source["kind"].get<std::string>();
		auto Temp = std::make_unique<FTempDirectory>(U8Path(Layout.Cache) / "staging");
		auto Content = Temp->Path / "content";
		if (Kind == "directory")
		{
			auto From = Absolute(C.Source["location"], Layout.Root);
			auto ManifestBytes = Read(From / "veroscript-package.json", Options.Limits.MaxManifestBytes, &Work);
			Work.ChargeMetadata(ManifestBytes.size());
			auto M = Manifest(Parse(ManifestBytes, Options.Limits));
			if (Under(U8Path(Layout.Cache), From) || Under(U8Path(Layout.Packages), From))
			{
				Fail("recursive-source", "package source must not contain its installation or cache root");
			}
			auto Files = Inventory(From, Work, true, M);
			auto ManifestFile = std::find_if(Files.begin(), Files.end(),
			    [](const J& F)
			{
				return F.at("path") == "veroscript-package.json";
			});
			if (ManifestFile == Files.end() || ManifestFile->at("sha256") != Hash(ManifestBytes))
			{
				Fail("source-changed", "directory manifest changed while selecting snapshot files");
			}
			CopyInventory(From, Content, Files, Work);
			if (Inventory(From, Work, true, M) != Files)
			{
				Fail("source-changed", "directory changed while creating immutable snapshot");
			}
			C.Inventory = Files;
			C.Artifact = {{"id", "directory-" + InventoryHash(Files)},
			    {"sha256", InventoryHash(Files)},
			    {"size", 0},
			    {"kind", "directory"}};
		}
		else
		{
			fs::path Archive = Kind == "url" ? Download(C.Source["location"], Spec.value("sha256", ""), 0)
			                                 : Absolute(C.Source["location"], Layout.Root);
			J Signatures;
			if (Spec.contains("signatures"))
			{
				Signatures = Spec.at("signatures");
			}
			else if (Kind != "url" && fs::exists(U8Path(NativePathText(Archive) + ".sig.json")))
			{
				Signatures = ReadJson(U8Path(NativePathText(Archive) + ".sig.json"), Work, Options.Limits.MaxManifestBytes);
			}
			if (Signatures.is_null() && (Spec.contains("signatures") || (Kind != "url" && fs::exists(U8Path(NativePathText(Archive) + ".sig.json")))))
			{
				Fail("artifact-signatures", "signature sidecar cannot be null");
			}
			auto Snapshot = Temp->Path / "archive.zip";
			SnapshotArchive(Archive, Snapshot, Work, Spec.value("sha256", std::string{}));
			Archive = Snapshot;
			auto Sha = HashFile(Archive, Work, Options.Limits.MaxArchiveBytes);
			if (Spec.contains("sha256") && Spec["sha256"] != Sha)
			{
				Fail("artifact-digest", "local or direct artifact digest changed");
			}
			VerifyDistributionArchive(Archive, Signatures.is_null() ? nullptr : &Signatures, Work);
			Extract(Archive, Content, Work);
			C.Artifact = {{"id", "archive-" + Sha},
			    {"sha256", Sha},
			    {"size", fs::file_size(Archive)},
			    {"kind", "archive"}};
			if (!Signatures.is_null())
			{
				C.Artifact["signatures"] = std::move(Signatures);
			}
		}
		C.Manifest = Manifest(ReadJson(Content / "veroscript-package.json", Work, Options.Limits.MaxManifestBytes));
		if (!Expected.empty() && C.Manifest["name"] != Expected)
		{
			Fail("package-identity", "requested package name differs from archive manifest");
		}
		Compatible(C.Manifest, Layout, Options);
		C.Content = Content;
		if (C.Inventory.is_null())
		{
			C.Inventory = Inventory(Content, Work, false, C.Manifest);
		}
		else
		{
			Inventory(Content, Work, false, C.Manifest);
		}
		C.Artifact["manifestSha256"] =
		    HashFile(C.Content / "veroscript-package.json", Work, Options.Limits.MaxManifestBytes);
		C.Artifact["target"] = C.Manifest.value("target", J::object());
		C.Artifact["requires"] = C.Manifest.value("requires", J::object());
		C.OwnerId = "package." + Id(J{{"source", C.Source["id"]}, {"name", C.Manifest["name"]}, {"version", C.Manifest["version"]}, {"sha256", C.Artifact["sha256"]}});
		Temps.push_back(std::move(Temp));
		return C;
	}
	void FRepository::Materialize(FCandidate& C)
	{
		if (!C.Content.empty())
		{
			return;
		}
		if (C.Source.at("kind") != "registry")
		{
			auto Kind = C.Source.at("kind").get<std::string>();
			J Spec = {{"version", C.Manifest.at("version")}, {Kind, C.Source.at("location")}};
			if (Kind != "directory")
			{
				Spec["sha256"] = C.Artifact.at("sha256");
			}
			if (C.Artifact.contains("signatures"))
			{
				Spec["signatures"] = C.Artifact.at("signatures");
			}
			auto Actual = Local(C.Manifest.at("name"), Spec);
			if (Actual.Artifact.at("sha256") != C.Artifact.at("sha256"))
			{
				Fail("locked-local-change", "local content differs from the exact lock");
			}
			C = std::move(Actual);
			return;
		}
		Work.Check();
		auto Sources = Registries(), S = Sources.at(C.Source.at("alias").get<std::string>());
		std::string Api = S.value("mirror", S.at("api").get<std::string>());
		if (C.Source["alias"] == "official" && !Options.Mirror.empty())
		{
			Api = Options.Mirror;
		}
		while (!Api.empty() && Api.back() == '/')
		{
			Api.pop_back();
		}
		auto Route = "/artifacts/" + EscapeUrl(C.Artifact["id"]) + "/download";
		fs::path Archive;
		try
		{
			Archive = Download(Api + Route, C.Artifact.at("sha256"), C.Artifact.at("size"), C.Source["id"]);
		}
		catch (const FFailure& E)
		{
			std::string Origin = S.at("api");
			while (!Origin.empty() && Origin.back() == '/')
			{
				Origin.pop_back();
			}
			if (Api == Origin || !S.value("allowOriginFallback", false) ||
			    E.Diagnostic.Code != "http-transfer")
			{
				throw;
			}
			Archive =
			    Download(Origin + Route, C.Artifact.at("sha256"), C.Artifact.at("size"), C.Source["id"]);
		}
		auto Temp = std::make_unique<FTempDirectory>(U8Path(Layout.Cache) / "staging");
		auto Root = Temp->Path / "content";
		auto Snapshot = Temp->Path / "archive.zip";
		SnapshotArchive(Archive, Snapshot, Work, C.Artifact.at("sha256"), C.Artifact.at("size"));
		VerifyDistributionArchive(Snapshot, C.Artifact.contains("signatures") ? &C.Artifact.at("signatures") : nullptr, Work);
		Extract(Snapshot, Root, Work);
		auto M = Manifest(ReadJson(Root / "veroscript-package.json", Work, Options.Limits.MaxManifestBytes));
		if (C.Artifact.at("manifestSha256") !=
		        HashFile(Root / "veroscript-package.json", Work, Options.Limits.MaxManifestBytes) ||
		    M["name"] != C.Manifest["name"] || M["version"] != C.Manifest["version"] ||
		    M.value("dependencies", J::object()) != C.Manifest.value("dependencies", J::object()) ||
		    M.value("conflicts", J::object()) != C.Manifest.value("conflicts", J::object()) ||
		    M.value("requires", J::object()) != C.Artifact.value("requires", J::object()) ||
		    M.value("target", J::object()) != C.Artifact.value("target", J::object()))
		{
			Fail("metadata-mismatch", "archive manifest disagrees with locked registry metadata");
		}
		C.MetadataOnly = false;
		C.Manifest = std::move(M);
		Compatible(C.Manifest, Layout, Options);
		C.Content = Root;
		C.Inventory = Inventory(Root, Work, false, C.Manifest);
		Temps.push_back(std::move(Temp));
	}
	J FRepository::RequestJson(const std::string& Alias, const std::string& Route)
	{
		Work.Check();
		auto Sources = Registries();
		if (!Sources.contains(Alias))
		{
			Fail("source-unconfigured", "registry source is not configured", Alias);
		}
		auto S = Sources[Alias];
		std::string Api = S["api"], Transport = S.value("mirror", Api);
		bool ExplicitMirror = Alias == "official" && !Options.Mirror.empty();
		if (ExplicitMirror)
		{
			Transport = Options.Mirror;
		}
		if (Transport != Api && !ExplicitMirror && !S.value("trustedMirror", false))
		{
			Fail("untrusted-mirror", "first metadata requires an explicitly trusted mirror");
		}
		while (!Api.empty() && Api.back() == '/')
		{
			Api.pop_back();
		}
		while (!Transport.empty() && Transport.back() == '/')
		{
			Transport.pop_back();
		}
		StableUrl(Transport);
		auto CacheFile = U8Path(Layout.Cache) / "metadata" / Id(J{{"id", S["id"]}, {"api", Api}}) /
		    (Hash(Route) + ".json");
		J Saved;
		bool PublicCache = !Options.Credential && Http->AllowsPublicMetadataCache() &&
		    Options.Proxy.find('@') == std::string::npos;
		if (PublicCache && fs::exists(CacheFile))
		{
			Saved = ReadJson(CacheFile, Work, Options.Limits.MaxResponseBytes);
		}
		if (Options.Offline)
		{
			if (Saved.is_null())
			{
				Fail("offline-missing", "registry metadata is not available offline", Alias + ":" + Route);
			}
			return Saved.at("data");
		}
		FHttpResponse Response;
		FDiagnostic Error;
		for (unsigned Attempt = 0;; ++Attempt)
		{
			Work.Check();
			FHttpRequest Q;
			Q.Url = Transport + Route;
			Q.Proxy = Options.Proxy;
			Q.CaFile = Options.CaFile;
			Q.AllowHttp = Options.AllowHttp;
			Q.MaxBytes = Options.Limits.MaxResponseBytes;
			Q.CheckCancelled = [&]()
			{
				Work.Check();
			};
			if (!Saved.is_null() && Saved.contains("etag"))
			{
				Q.Headers["If-None-Match"] = Saved["etag"];
			}
			if (Options.Credential)
			{
				auto C = Options.Credential(Q.Url);
				if (!C.empty())
				{
					Q.Headers["Authorization"] = C;
					Q.Credential = Options.Credential;
				}
			}
			bool Ok = Http->Send(Q, Response, Error);
			if (Ok && Response.Status == 304 && !Saved.is_null())
			{
				return Saved.at("data");
			}
			if (Ok && Response.Status == 200)
			{
				break;
			}
			bool Retry =
			    (!Ok && Error.Code == "http-transfer") || (Response.Status == 429 || Response.Status == 502 || Response.Status == 503 || Response.Status == 504);
			if (!Retry || Attempt == 2)
			{
				if (Retry && Transport != Api && S.value("allowOriginFallback", false))
				{
					Transport = Api;
					Attempt = 0;
					continue;
				}
				if (!Ok)
				{
					FFailure F(Error.Code, Error.Message);
					F.Diagnostic = Error;
					throw F;
				}
				auto L = Options.Limits;
				L.MaxManifestBytes = Options.Limits.MaxResponseBytes;
				try
				{
					auto B = Parse(Response.Body, L);
					FFailure F(B.value("code", "http-status"), B.value("message", "registry request failed"));
					F.Diagnostic.DetailsJson =
					    J{{"status", Response.Status}, {"requestId", B.value("requestId", "")}}.dump();
					throw F;
				}
				catch (const FFailure&)
				{
					throw;
				}
				catch (...)
				{
					Fail("http-status", "registry returned an invalid error response");
				}
			}
			unsigned Delay = 100 * (1u << Attempt);
			auto H = Response.Headers.find("retry-after");
			if (H != Response.Headers.end())
			{
				try
				{
					auto N = std::stoull(H->second);
					if (N > 10)
					{
						Fail("retry-after", "server retry interval exceeds operation retry budget");
					}
					Delay = static_cast<unsigned>(N * 1000);
				}
				catch (const std::invalid_argument&)
				{
				}
			}
			for (unsigned I = 0; I < Delay; I += 20)
			{
				Work.Check();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
		}
		auto Limits = Options.Limits;
		Limits.MaxManifestBytes = Options.Limits.MaxResponseBytes;
		Work.ChargeMetadata(Response.Body.size());
		auto Data = Parse(Response.Body, Limits);
		if (S["id"] != Data.value("sourceId", ""))
		{
			Fail("source-identity", "registry or mirror returned a different logical source");
		}
		if (PublicCache)
		{
			AtomicWrite(CacheFile, J{{"etag", Response.Headers.count("etag") ? Response.Headers["etag"] : ""}, {"data", Data}}.dump());
		}
		return Data;
	}
	J FRepository::Registries() const
	{
		return RegistrySettings(Settings, Options);
	}

	J FRepository::Source(const J& D) const
	{
		for (const auto* K : {"directory", "archive", "url"})
		{
			if (D.contains(K))
			{
				std::string Value = D[K];
				if (std::string(K) == "url")
				{
					StableUrl(Value);
				}
				else
				{
					Value = PathReference(Absolute(Value, Layout.Root), U8Path(Layout.Root));
				}
				J S = {{"kind", K}, {"location", Value}};
				S["id"] = "local." + Id(S);
				return S;
			}
		}
		auto Alias = D.value("source", "official");
		auto R = Registries();
		if (!R.contains(Alias))
		{
			Fail("source-unconfigured", "registry source must be configured before use", Alias);
		}
		auto V = R[Alias];
		return {{"kind", "registry"}, {"alias", Alias}, {"id", V.at("id")}, {"api", V.at("api")}};
	}
	fs::path FRepository::Download(const std::string& Url, const std::string& Sha, std::uint64_t Size,
	    const std::string& SourceId)
	{
		Work.Check();
		if (!Sha.empty() && !Digest(Sha))
		{
			Fail("invalid-digest", "invalid archive content digest");
		}
		auto Root = U8Path(Layout.Cache) / "artifacts" / (SourceId.empty() ? "direct" : Hash(SourceId));
		auto Cached = Root / (Sha + ".zip");
		if (!Sha.empty() && fs::exists(Cached))
		{
			if ((Size && fs::file_size(Cached) != Size) ||
			    HashFile(Cached, Work, Options.Limits.MaxArchiveBytes) != Sha)
			{
				Fail("cache-corrupt", "cached artifact failed integrity verification");
			}
			return Cached;
		}
		if (Options.Offline)
		{
			Fail("offline-missing", "artifact is missing from verified cache", Sha);
		}
		if (Size > Options.Limits.MaxArchiveBytes)
		{
			Fail("archive-limit", "remote archive exceeds byte budget");
		}
		auto Temp = std::make_unique<FTempDirectory>(Root / "staging");
		auto File = Temp->Path / "download.zip";
		std::ofstream Out(File, std::ios::binary);
		if (!Out)
		{
			Fail("download-write", "cannot open download staging file");
		}
		std::uint64_t Bytes = 0;
		FHttpRequest Q;
		Q.Url = Url;
		Q.Proxy = Options.Proxy;
		Q.CaFile = Options.CaFile;
		Q.AllowHttp = Options.AllowHttp;
		Q.MaxBytes = Size ? Size : Options.Limits.MaxArchiveBytes;
		Q.CheckCancelled = [&]()
		{
			Work.Check();
		};
		Q.DataSink = [&](const char* P, std::size_t N)
		{
			if (N > Q.MaxBytes - Bytes)
			{
				Fail("download-limit", "download exceeds artifact size");
			}
			if (!Out.write(P, static_cast<std::streamsize>(N)))
			{
				Fail("download-write", "failed writing download");
			}
			Bytes += N;
		};
		Q.Progress = [&](std::uint64_t N, std::uint64_t Total)
		{
			if (Options.Progress)
			{
				Options.Progress("download", N, Total);
			}
		};
		if (Options.Credential)
		{
			auto A = Options.Credential(Url);
			if (!A.empty())
			{
				Q.Headers["Authorization"] = A;
				Q.Credential = Options.Credential;
			}
		}
		FHttpResponse R;
		FDiagnostic E;
		if (!Http->Send(Q, R, E))
		{
			FFailure X(E.Code, E.Message);
			X.Diagnostic = E;
			throw X;
		}
		if (R.Status != 200)
		{
			Fail("artifact-http", "artifact download returned HTTP " + std::to_string(R.Status));
		}
		// A custom transport may return owned bytes instead of invoking the sink.
		if (!R.Body.empty())
		{
			if (Bytes)
			{
				Fail("transport-contract", "transport returned both streamed and owned artifact bytes");
			}
			Q.DataSink(R.Body.data(), R.Body.size());
		}
		Out.close();
		if (Size && Bytes != Size)
		{
			Fail("artifact-size", "downloaded archive size does not match metadata");
		}
		auto Actual = HashFile(File, Work, Options.Limits.MaxArchiveBytes);
		if (!Sha.empty() && Actual != Sha)
		{
			Fail("artifact-digest", "downloaded archive SHA-256 does not match metadata");
		}
		Cached = Root / (Actual + ".zip");
		FFileLock Guard(Root / (Actual + ".lock"), true, true, true, &Work);
		if (fs::exists(Cached))
		{
			if (HashFile(Cached, Work, Options.Limits.MaxArchiveBytes) != Actual)
			{
				Fail("cache-corrupt", "cache race produced inconsistent content");
			}
		}
		else
		{
			fs::rename(File, Cached);
		}
		Temps.push_back(std::move(Temp));
		return Cached;
	}
} // namespace VeroScript::Packages::Detail
