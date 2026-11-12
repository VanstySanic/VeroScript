// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageInstalledInternal.h"
#include "PackageSignatures.h"
#include <algorithm>
#include <random>

namespace VeroScript::Packages::Detail
{
	namespace
	{
		bool Overlap(const J& A, const J& B)
		{
			for (const auto* K : {"os", "arch", "environment", "coreBuild"})
			{
				if (A.contains(K) && B.contains(K) && A[K] != B[K])
				{
					return false;
				}
			}
			auto Abis = A.value("abis", J::object());
			for (auto I = Abis.begin(); I != Abis.end(); ++I)
			{
				if (B.value("abis", J::object()).contains(I.key()) && B["abis"][I.key()] != I.value())
				{
					return false;
				}
			}
			// Minimum versions and required capabilities have no upper bounds or
			// exclusions, so different values cannot establish disjoint targets.
			return true;
		}
		J ArtifactData(const J& A)
		{
			J Result = {{"sha256", A.at("sha256")},
			    {"size", A.at("size")},
			    {"manifestSha256", A.at("manifestSha256")},
			    {"target", A.value("target", J::object())},
			    {"requires", A.value("requires", J::object())}};
			if (A.contains("signatures"))
			{
				Result["signatures"] = A.at("signatures");
			}
			return Result;
		}
		J PublishedArtifacts(const J& Values, const FWork& W)
		{
			if (!Values.is_array() || Values.size() > W.Options.Limits.MaxCandidates)
			{
				Fail("publish-contract", "invalid published artifact list");
			}
			J Result = J::array();
			std::set<std::string> Ids, Digests;
			for (const auto& A : Values)
			{
				W.Check();
				Keys(A, {"id", "sha256", "size", "manifestSha256", "target", "requires", "blocked", "signatures"},
				    "published artifact");
				if (!A.contains("id") || !A["id"].is_string() || !A.contains("size") ||
				    !A["size"].is_number_unsigned() ||
				    A["size"].get<std::uint64_t>() > W.Options.Limits.MaxArchiveBytes ||
				    !A.contains("sha256") || !A["sha256"].is_string() ||
				    !Digest(A["sha256"].get<std::string>()) || !A.contains("manifestSha256") ||
				    !A["manifestSha256"].is_string() || !Digest(A["manifestSha256"].get<std::string>()) ||
				    (A.contains("blocked") && !A["blocked"].is_boolean()))
				{
					Fail("publish-contract", "invalid published artifact identity");
				}
				auto Identity = A["id"].get<std::string>();
				if (Identity.empty() || Identity == "." || Identity == ".." ||
				    !std::all_of(Identity.begin(), Identity.end(),
				        [](unsigned char C)
				{
					return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
					    (C >= '0' && C <= '9') || C == '.' || C == '_' || C == '-';
				}) ||
				    !Ids.insert(Identity).second || !Digests.insert(A["sha256"].get<std::string>()).second)
				{
					Fail("publish-contract", "invalid or duplicate published artifact ID or digest");
				}
				auto Copy = ArtifactData(A);
				if (A.contains("signatures"))
				{
					ValidateDistributionSignatures(A.at("signatures"), A.at("sha256"), A.at("size"), W);
				}
				Manifest(J{{"format", "veroscript-package"},
				    {"formatVersion", FormatVersion},
				    {"name", "publication-validation"},
				    {"version", "0.1.1"},
				    {"target", Copy["target"]},
				    {"requires", Copy["requires"]}});
				for (const auto& Previous : Result)
				{
					W.Check();
					if (Overlap(Copy["target"], Previous["target"]))
					{
						Fail("publish-target", "published artifact targets overlap");
					}
				}
				Copy["id"] = Identity;
				Result.push_back(std::move(Copy));
			}
			return Result;
		}
		J VerifyVersion(const J& Result, const J& Expected, const FWork& W)
		{
			if (!Result.is_object() || Result.value("format", J{}) != "veroscript-registry-version" ||
			    Result.value("formatVersion", J{}) != FormatVersion ||
			    Result.value("sourceId", J{}) != Expected.at("sourceId") || !Result.contains("artifacts") ||
			    !Result["artifacts"].is_array() || Result["artifacts"].empty())
			{
				Fail("publish-contract", "invalid registry publication confirmation");
			}
			for (const auto* K : {"name", "version", "dependencies", "conflicts"})
			{
				auto Default = std::string(K) == "name" || std::string(K) == "version" ? J{} : J::object();
				if (Result.value(K, Default) != Expected.at(K))
				{
					Fail("publish-conflict", "published version has different immutable metadata");
				}
			}
			for (const auto* K : {"blocked", "withdrawn"})
			{
				if (Result.contains(K) && !Result[K].is_boolean())
				{
					Fail("publish-contract", "invalid publication status");
				}
				if (Result.value(K, false))
				{
					Fail("publish-state", "cannot publish a blocked or withdrawn version");
				}
			}
			return PublishedArtifacts(Result["artifacts"], W);
		}
		void VerifyPreserved(const J& Actual, const J& Baseline, const FWork& W)
		{
			for (const auto& A : Baseline)
			{
				W.Check();
				auto Found = std::find_if(Actual.begin(), Actual.end(),
				    [&](const J& V)
				{
					return V["id"] == A["id"];
				});
				if (Found == Actual.end() || *Found != A)
				{
					Fail("publish-conflict", "an existing artifact was removed or changed");
				}
			}
		}
		bool ContainsRequested(const J& Actual, const J& Requested, const FWork& W)
		{
			bool Complete = true;
			for (const auto& A : Requested)
			{
				W.Check();
				bool Found = false;
				for (const auto& V : Actual)
				{
					W.Check();
					if (ArtifactData(V) == A)
					{
						Found = true;
					}
					else if (Overlap(V["target"], A["target"]))
					{
						Fail("publish-conflict", "a requested artifact overlaps an existing target");
					}
				}
				Complete = Complete && Found;
			}
			return Complete;
		}
		J Http(const FHttpRequest& Request, const FClientOptions& O, const FWork& W,
		    bool AllowMissing = false)
		{
			W.Check();
			auto Q = Request;
			Q.Proxy = O.Proxy;
			Q.CaFile = O.CaFile;
			Q.AllowHttp = O.AllowHttp;
			Q.MaxBytes = O.Limits.MaxResponseBytes;
			Q.CheckCancelled = [&]()
			{
				W.Check();
			};
			if (O.Credential)
			{
				auto Token = O.Credential(Q.Url);
				if (!Token.empty())
				{
					Q.Headers["Authorization"] = Token;
					Q.Credential = O.Credential;
				}
			}
			FHttpResponse Response;
			FDiagnostic E;
			auto Transport = O.Transport ? O.Transport : CreateHttpTransport();
			if (!Transport->Send(Q, Response, E))
			{
				FFailure X(E.Code, E.Message);
				X.Diagnostic = E;
				throw X;
			}
			auto Limits = O.Limits;
			Limits.MaxManifestBytes = O.Limits.MaxResponseBytes;
			if (AllowMissing && Response.Status == 404)
			{
				return {};
			}
			if (Q.Method == "PUT" && Response.Status >= 200 && Response.Status < 300)
			{
				// Object stores need not return a registry JSON document for uploads.
				return J::object();
			}
			W.ChargeMetadata(Response.Body.size());
			auto Data = Response.Body.empty() ? J::object() : Parse(Response.Body, Limits);
			if (Response.Status < 200 || Response.Status >= 300)
			{
				// Gateways may return non-object JSON or incorrectly typed fields
				// after a successful commit. Keep this a normal HTTP failure so the
				// caller can still reconcile the authoritative version.
				const auto ErrorText = [&](const char* Key, const char* Fallback)
				{
					if (Data.is_object())
					{
						auto Value = Data.find(Key);
						if (Value != Data.end() && Value->is_string())
						{
							return Value->get<std::string>();
						}
					}
					return std::string(Fallback);
				};
				FFailure X(ErrorText("code", "publish-http"),
				    ErrorText("message", "publication request failed"));
				X.Diagnostic.DetailsJson =
				    J{{"status", Response.Status}, {"requestId", ErrorText("requestId", "")}}.dump();
				throw X;
			}
			if (AllowMissing && Data.is_null())
			{
				Fail("publish-contract", "only HTTP 404 may indicate an absent version");
			}
			return Data;
		}
	} // namespace
	J Publication(const FRequest& Request, const FEnvironmentLayout& Layout, FWork& W)
	{
		if (Request.Options.Offline)
		{
			Fail("offline-publish", "publication requires an online registry");
		}
		if (Request.Arguments.empty())
		{
			Fail("publish-input", "publish requires one or more explicit ZIP artifacts");
		}
		// Distribution metadata is separate from the selected project's registry configuration.
		auto P = NewProject("publication");
		if (fs::exists(U8Path(Layout.Requirements)))
		{
			P = Project(ReadRecord(U8Path(Layout.Requirements), W));
		}
		FRepository Repository(Layout, P, Request.Options, W);
		auto Registries = Repository.Registries();
		if (!Registries.contains("official"))
		{
			Fail("source-unconfigured", "publication requires a configured official registry");
		}
		auto Source = Registries.at("official");
		std::string Api = Source.at("api");
		while (!Api.empty() && Api.back() == '/')
		{
			Api.pop_back();
		}
		J Base, Artifacts = J::array();
		std::vector<fs::path> Paths;
		std::set<std::string> Digests;
		std::vector<std::unique_ptr<FTempDirectory>> Temps;
		for (const auto& Input : Request.Arguments)
		{
			W.Check();
			auto Archive = Absolute(Input, Request.Environment.WorkingDirectory);
			const auto SignatureFile = U8Path(NativePathText(Archive) + ".sig.json");
			J Signatures;
			if (fs::exists(SignatureFile))
			{
				Signatures = Parse(Read(SignatureFile, W.Options.Limits.MaxManifestBytes), W.Options.Limits);
			}
			if (fs::exists(SignatureFile) && Signatures.is_null())
			{
				Fail("artifact-signatures", "signature sidecar cannot be null");
			}
			if (!IsPackageArchive(Archive))
			{
				Fail("publish-input", "publication accepts .vspkg or .zip ZIP archives only");
			}
			auto Temp = std::make_unique<FTempDirectory>(U8Path(Layout.Cache) / "publication-staging");
			auto Root = Temp->Path / "content";
			auto Snapshot = Temp->Path / "archive.zip";
			SnapshotArchive(Archive, Snapshot, W);
			Archive = Snapshot;
			Extract(Archive, Root, W);
			auto M = Manifest(ReadRecord(Root / "veroscript-package.json", W));
			Inventory(Root, W, false, M);
			FInstalledPackage Text;
			Text.Name = M["name"];
			Text.Version = M["version"];
			Text.Path = NativePathText(Root);
			Text.ManifestJson = M.dump();
			Text.TextDomain = "package." + Id(M);
			DisplayServices({Text}, W);
			J Version = {{"name", M.at("name")},
			    {"version", M.at("version")},
			    {"dependencies", M.value("dependencies", J::object())},
			    {"conflicts", M.value("conflicts", J::object())}};
			if (Base.is_null())
			{
				Base = Version;
			}
			else if (Base != Version)
			{
				Fail("publish-version",
				    "all artifacts must have identical package, version, dependencies and conflicts");
			}
			auto Hash = HashFile(Archive, W, W.Options.Limits.MaxArchiveBytes);
			if (!Digests.insert(Hash).second)
			{
				Fail("publish-duplicate", "the same archive was supplied more than once");
			}
			auto Target = M.value("target", J::object());
			for (const auto& A : Artifacts)
			{
				if (Overlap(Target, A.at("target")))
				{
					Fail("publish-target",
					    "artifact targets overlap; a version uses portable or disjoint platform artifacts");
				}
			}
			Artifacts.push_back(
			    {{"sha256", Hash},
			        {"size", fs::file_size(Archive)},
			        {"manifestSha256", HashFile(Root / "veroscript-package.json", W, W.Options.Limits.MaxManifestBytes)},
			        {"target", Target},
			        {"requires", M.value("requires", J::object())}});
			if (!Signatures.is_null())
			{
				ValidateDistributionSignatures(Signatures, Hash, fs::file_size(Archive), W);
				Artifacts.back()["signatures"] = Signatures;
			}
			if (W.Options.ArtifactDecoder)
			{
				VerifyDistributionArchive(Archive, Signatures.is_null() ? nullptr : &Signatures, W);
			}
			Paths.push_back(Archive);
			Temps.push_back(std::move(Temp));
		}
		Base["artifacts"] = Artifacts;
		Base["sourceId"] = Source.at("id");
		auto OperationId = Id(J{{"api", Api}, {"metadata", Base}});
		auto RecordPath = U8Path(Layout.Cache) / "publication" / (OperationId + ".json");
		FFileLock Guard(RecordPath.parent_path() / (OperationId + ".lock"), true, true, true, &W);
		J Record;
		if (fs::exists(RecordPath))
		{
			Record = ReadRecord(RecordPath, W);
			if (!Record.is_object() || Record.value("format", J{}) != "veroscript-publication-operation" ||
			    Record.value("formatVersion", J{}) != FormatVersion ||
			    Record.value("mode", J{}) != "append" || Record.value("metadata", J{}) != Base ||
			    !Record.contains("baseline") || !Record.contains("uploads") ||
			    !Record["uploads"].is_array() || Record["uploads"].size() > Artifacts.size())
			{
				Fail("publish-record", "invalid publication recovery record or different artifact bytes");
			}
			String(Record.at("idempotencyKey"), "publication.idempotencyKey");
			Record["baseline"] = PublishedArtifacts(Record["baseline"], W);
			std::set<std::string> UploadIds, Uploaded;
			for (const auto& Ref : Record["uploads"])
			{
				W.Check();
				Keys(Ref, {"sha256", "size", "manifestSha256", "target", "requires", "uploadId", "signatures"},
				    "publication upload");
				String(Ref.at("uploadId"), "publication.uploadId");
				auto Data = ArtifactData(Ref);
				if (std::find(Artifacts.begin(), Artifacts.end(), Data) == Artifacts.end() ||
				    !UploadIds.insert(Ref["uploadId"].get<std::string>()).second ||
				    !Uploaded.insert(Id(Data)).second)
				{
					Fail("publish-record", "invalid or duplicate recovered upload reference");
				}
			}
		}
		else
		{
			std::random_device R;
			Record = {{"format", "veroscript-publication-operation"},
			    {"formatVersion", FormatVersion},
			    {"mode", "append"},
			    {"metadata", Base},
			    {"idempotencyKey", Hash(std::to_string(R()) + std::to_string(R()) + Id(Base))},
			    {"baseline", J::array()},
			    {"uploads", J::array()}};
		}
		const auto FetchVersion = [&]()
		{
			FHttpRequest Q;
			Q.Url = Api + "/packages/" + EscapeUrl(Base["name"]) + "/versions/" + EscapeUrl(Base["version"]);
			// Observe the authoritative origin, never a mirror or installation cache.
			Q.Headers["Cache-Control"] = "no-cache";
			return Http(Q, Request.Options, W, true);
		};
		auto Current = FetchVersion();
		auto Existing = Current.is_null() ? J::array() : VerifyVersion(Current, Base, W);
		VerifyPreserved(Existing, Record["baseline"], W);
		auto Complete = ContainsRequested(Existing, Artifacts, W);
		Record["baseline"] = Existing;
		Record["published"] = Complete;
		AtomicWrite(RecordPath, Record.dump(2));
		if (Complete)
		{
			return Current;
		}
		for (std::size_t I = 0; I < Paths.size(); ++I)
		{
			W.Check();
			if (std::any_of(Existing.begin(), Existing.end(),
			        [&](const J& A)
			{
				return ArtifactData(A) == Artifacts[I];
			}) ||
			    std::any_of(Record["uploads"].begin(), Record["uploads"].end(),
			        [&](const J& A)
			{
				return ArtifactData(A) == Artifacts[I];
			}))
			{
				continue;
			}
			J Allocate = Artifacts[I];
			Allocate["name"] = Base["name"];
			Allocate["version"] = Base["version"];
			Allocate["idempotencyKey"] = Record.at("idempotencyKey").get<std::string>() + ":" +
			    Artifacts[I].at("sha256").get<std::string>();
			FHttpRequest Q;
			Q.Method = "POST";
			Q.Url = Api + "/uploads";
			Q.Body = Allocate.dump();
			Q.Headers["Content-Type"] = "application/json";
			auto Upload = Http(Q, Request.Options, W);
			Keys(Upload,
			    {"format", "formatVersion", "sourceId", "uploadId", "method", "url", "headers", "expiresAt"},
			    "upload");
			if (Upload.value("format", "") != "veroscript-upload" ||
			    Upload.value("formatVersion", "") != FormatVersion ||
			    Upload.at("sourceId") != Source.at("id") || Upload.value("method", "") != "PUT")
			{
				Fail("upload-contract", "invalid registry upload response");
			}
			String(Upload.at("uploadId"), "uploadId");
			String(Upload.at("url"), "upload.url");
			FHttpRequest Put;
			Put.Method = "PUT";
			Put.Url = Upload.at("url");
			Put.BodyFile = NativePathText(Paths[I]);
			if (Upload.contains("headers"))
			{
				if (!Upload["headers"].is_object())
				{
					Fail("upload-contract", "upload headers must be a string map");
				}
				for (auto H = Upload["headers"].begin(); H != Upload["headers"].end(); ++H)
				{
					String(H.value(), "upload.header");
					Put.Headers[H.key()] = H.value();
				}
			}
			Http(Put, Request.Options, W);
			auto Ref = Artifacts[I];
			Ref["uploadId"] = Upload.at("uploadId");
			Record["uploads"].push_back(Ref);
			AtomicWrite(RecordPath, Record.dump(2));
		}
		J Commit = Base;
		Commit["mode"] = "append";
		Commit["artifacts"] = Record["uploads"];
		Commit["idempotencyKey"] = Record["idempotencyKey"];
		if (Record.contains("requestSha256") && Record["requestSha256"] != Id(Commit))
		{
			Fail("publish-record", "recovered publication request changed after submission");
		}
		Record["requestSha256"] = Id(Commit);
		AtomicWrite(RecordPath, Record.dump(2));
		const auto Confirm = [&](const J& Result)
		{
			auto Actual = VerifyVersion(Result, Base, W);
			VerifyPreserved(Actual, Record["baseline"], W);
			if (!ContainsRequested(Actual, Artifacts, W))
			{
				Fail("publish-incomplete", "publication did not include every requested artifact");
			}
			Record["baseline"] = Actual;
			Record["published"] = true;
			AtomicWrite(RecordPath, Record.dump(2));
		};
		FHttpRequest Q;
		Q.Method = "POST";
		Q.Url = Api + "/packages/" + EscapeUrl(Base.at("name")) + "/versions";
		Q.Body = Commit.dump();
		Q.Headers["Content-Type"] = "application/json";
		try
		{
			auto Result = Http(Q, Request.Options, W);
			Confirm(Result);
			return Result;
		}
		catch (const FFailure& Original)
		{
			// A lost response may be followed by another disjoint append. Require
			// all observed and requested identities, while allowing those additions.
			try
			{
				auto Published = FetchVersion();
				if (Published.is_null())
				{
					throw Original;
				}
				Confirm(Published);
				return Published;
			}
			catch (const FFailure& Verify)
			{
				if (Verify.Diagnostic.Code == "publish-conflict" ||
				    Verify.Diagnostic.Code == "publish-target" ||
				    Verify.Diagnostic.Code == "publish-contract" || Verify.Diagnostic.Code == "publish-state")
				{
					throw;
				}
				throw Original;
			}
		}
	}
} // namespace VeroScript::Packages::Detail
