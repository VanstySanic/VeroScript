// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "PackageSignatures.h"
#include <regex>

namespace VeroScript::Packages::Detail
{
	void ValidateDistributionSignatures(const J& Records, const std::string& Hash, std::uint64_t Size, const FWork& W)
	{
		if (!Records.is_array() || Records.empty() || Records.size() > 16 || Records.dump().size() > W.Options.Limits.MaxManifestBytes)
		{
			Fail("artifact-signatures", "expected between one and sixteen bounded signature records");
		}
		static const std::regex Token("[A-Za-z0-9][A-Za-z0-9_.-]{0,254}");
		static const std::regex Encoded("[A-Za-z0-9+/]*={0,2}");
		std::set<std::string> Seen;
		for (const auto& R : Records)
		{
			W.Check();
			Keys(R, {"format", "formatVersion", "algorithm", "kind", "bytes", "sha256", "keyId", "keyVersion", "signature"}, "artifact.signature");
			if (R.at("format") != "veroscript-artifact-signature" || R.at("formatVersion") != "0.1.1" ||
			    R.at("kind") != "distribution" || R.at("bytes") != std::to_string(Size) || R.at("sha256") != Hash)
			{
				Fail("artifact-signature-subject", "signature does not bind the distribution size and digest");
			}
			for (const auto* K : {"algorithm", "keyId", "keyVersion"})
			{
				if (!R.at(K).is_string() || !std::regex_match(R.at(K).get<std::string>(), Token))
				{
					Fail("artifact-signatures", "invalid signature identity");
				}
			}
			const auto Key = R.at("algorithm").get<std::string>() + ":" + R.at("keyId").get<std::string>() + "@" + R.at("keyVersion").get<std::string>();
			if (!Seen.insert(Key).second)
			{
				Fail("artifact-signatures", "duplicate signature identity");
			}
			const auto Signature = R.at("signature").get<std::string>();
			if (Signature.empty() || Signature.size() > 4096 || Signature.size() % 4 || !std::regex_match(Signature, Encoded) ||
			    (R.at("algorithm") == "ed25519" && (Signature.size() != 88 || Signature.substr(86) != "==")))
			{
				Fail("artifact-signatures", "invalid encoded signature");
			}
		}
	}
	void VerifyDistributionArchive(const fs::path& Path, const J* Signatures, const FWork& W)
	{
		if (!Signatures && !W.Options.ArtifactDecoder) return;
		FArtifactData Data;
		std::string Error;
		const FArtifactWork Work{W.Options.Limits.MaxArchiveBytes, [&]()
		{
			W.Check();
		}};
		if (!ReadArtifactFile({NativePathText(Path), "distribution", {}}, Work, Data, Error))
		{
			Fail("artifact-read", Error);
		}
		const auto Hash = HashFile(Path, W, W.Options.Limits.MaxArchiveBytes);
		if (Signatures)
		{
			ValidateDistributionSignatures(*Signatures, Hash, Data.Bytes.size(), W);
			Data.SignatureRecords = Signatures->dump();
		}
		if (!DecodeArtifact({NativePathText(Path), "distribution", {}}, Work, Data, Error, W.Options.ArtifactDecoder.get()))
		{
			Fail("artifact-trust", Error);
		}
		// An installation distribution remains a ZIP of its original stored bytes.
		if (Data.Bytes.size() != fs::file_size(Path) || Hash != Detail::Hash(std::string_view(reinterpret_cast<const char*>(Data.Bytes.data()), Data.Bytes.size())))
		{
			Fail("artifact-transform", "distribution verification cannot transform archive bytes");
		}
	}
}
