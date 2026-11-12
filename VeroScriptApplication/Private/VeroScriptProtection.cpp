// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptProtection.h"
#include "ApplicationInternal.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <regex>

namespace VeroScript::Application
{
	using namespace Detail;
	namespace
	{
		constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string Base64(const std::vector<std::uint8_t>& Bytes)
		{
			std::string S;
			for (std::size_t I = 0; I < Bytes.size(); I += 3)
			{
				const auto V = static_cast<std::uint32_t>(Bytes[I]) << 16 |
				    (I + 1 < Bytes.size() ? static_cast<std::uint32_t>(Bytes[I + 1]) << 8 : 0) |
				    (I + 2 < Bytes.size() ? Bytes[I + 2] : 0);
				S += Alphabet[(V >> 18) & 63];
				S += Alphabet[(V >> 12) & 63];
				S += I + 1 < Bytes.size() ? Alphabet[(V >> 6) & 63] : '=';
				S += I + 2 < Bytes.size() ? Alphabet[V & 63] : '=';
			}
			return S;
		}
		std::vector<std::uint8_t> Unbase64(const std::string& S)
		{
			if (S.size() % 4 || S.size() > 1024 * 1024)
			{
				Fail("base64", "invalid or oversized Base64 data");
			}
			std::vector<std::uint8_t> Out;
			for (std::size_t I = 0; I < S.size(); I += 4)
			{
				std::uint32_t V = 0;
				for (std::size_t K = 0; K < 4; ++K)
				{
					const auto* P = std::strchr(Alphabet, S[I + K]);
					if ((!P || S[I + K] == '\0') && S[I + K] != '=')
					{
						Fail("base64", "invalid Base64 character");
					}
					V = (V << 6) | (S[I + K] == '=' ? 0 : static_cast<std::uint32_t>(P - Alphabet));
				}
				Out.push_back(static_cast<std::uint8_t>(V >> 16));
				if (S[I + 2] != '=')
				{
					Out.push_back(static_cast<std::uint8_t>(V >> 8));
				}
				if (S[I + 3] != '=')
				{
					Out.push_back(static_cast<std::uint8_t>(V));
				}
			}
			if (Base64(Out) != S)
			{
				Fail("base64", "noncanonical Base64 data");
			}
			return Out;
		}
		void Token(const std::string& S)
		{
			static const std::regex Pattern("[A-Za-z0-9][A-Za-z0-9_.-]{0,254}");
			if (!std::regex_match(S, Pattern))
			{
				Fail("key-identity", "invalid protection identity");
			}
		}
		void Erase(std::vector<std::uint8_t>& S)
		{
			volatile std::uint8_t* P = S.data();
			for (std::size_t I = 0; I < S.size(); ++I)
			{
				P[I] = 0;
			}
		}
		struct FSecretScope
		{
			~FSecretScope()
			{
				Erase(Key.Secret);
			}
			FProtectionKey& Key;
		};
		std::vector<std::uint8_t> Unhex(const std::string& S)
		{
			if (S.size() != 64 || !std::all_of(S.begin(), S.end(), [](char C)
			{
				return (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f');
			}))
			{
				Fail("digest", "expected a canonical SHA-256 digest");
			}
			std::vector<std::uint8_t> Out;
			for (std::size_t I = 0; I < S.size(); I += 2)
			{
				auto Digit = [](char C)
				{
					return C <= '9' ? C - '0' : C - 'a' + 10;
				};
				Out.push_back(static_cast<std::uint8_t>(Digit(S[I]) * 16 + Digit(S[I + 1])));
			}
			return Out;
		}
		void Integer(std::vector<std::uint8_t>& Out, std::uint64_t V, unsigned Width)
		{
			for (unsigned I = 0; I < Width; ++I)
			{
				Out.push_back(static_cast<std::uint8_t>(V));
				V >>= 8;
			}
		}
		std::vector<std::uint8_t> SignatureMessage(const J& R)
		{
			const std::string Domain = "VeroScript.ArtifactSignature.0.1.1";
			auto Out = Bytes(Domain);
			Out.push_back(0);
			const auto Kind = R.at("kind").get<std::string>();
			const std::vector<std::string> Kinds{"application", "library", "distribution", "applicationInventory", "resource", "generic"};
			const auto I = std::find(Kinds.begin(), Kinds.end(), Kind);
			if (I == Kinds.end())
			{
				Fail("signature-kind", "unsupported signature subject kind");
			}
			Out.push_back(static_cast<std::uint8_t>(I - Kinds.begin() + 1));
			for (const char* Field : {"algorithm", "keyId", "keyVersion"})
			{
				const auto S = R.at(Field).get<std::string>();
				Token(S);
				Integer(Out, S.size(), 2);
				Out.insert(Out.end(), S.begin(), S.end());
			}
			Integer(Out, Count(R.at("bytes"), "signature.bytes"), 8);
			const auto Hash = Unhex(R.at("sha256").get<std::string>());
			Out.insert(Out.end(), Hash.begin(), Hash.end());
			return Out;
		}
		class FKeyring final : public IProtectionKeys
		{
		public:
			std::string GetIdentity() const override
			{
				return Identity;
			}
			bool Get(const std::string& Name, const std::string& Version, const std::string& Purpose,
			    FProtectionKey& Result, std::string& Error) const override
			{
				const auto I = Records.find({Name, Version});
				if (I == Records.end() || !I->second.Purposes.count(Purpose) ||
				    (Purpose == "verify" && !I->second.Trusted))
				{
					Error = "key is missing or not authorized for the requested purpose";
					return false;
				}
				Result = I->second.Key;
				if (Purpose == "verify")
				{
					Erase(Result.Secret);
					Result.Secret.clear();
				}
				return true;
			}
			~FKeyring() override
			{
				for (auto& R : Records)
				{
					Erase(R.second.Key.Secret);
				}
			}
			struct FRecord
			{
				FProtectionKey Key;
				std::set<std::string> Purposes;
				bool Trusted = false;
			};
			std::map<std::pair<std::string, std::string>, FRecord> Records;
			std::string Identity;
		};
	}
	std::shared_ptr<const IProtectionKeys> LoadProtectionKeys(const std::string& Path,
	    FDiagnostic& Error, const FLimits& Limits)
	{
		try
		{
			auto MetadataLimits = Limits;
			MetadataLimits.MaxFileBytes = Limits.MaxMetadataBytes;
			auto Data = ReadFile(PathUtil::FromUtf8(Path), MetadataLimits);
			const auto Root = Parse(std::string_view(reinterpret_cast<const char*>(Data.data()), Data.size()), Limits);
			Keys(Root, {"format", "formatVersion", "revision", "keys"}, "keyring");
			if (Root.value("format", "") != "veroscript-keyring" || Root.value("formatVersion", "") != "0.1.1" ||
			    !Root.at("keys").is_array() || Root.at("keys").size() > 4096)
			{
				Fail("keyring-format", "invalid protection keyring");
			}
			auto Ring = std::make_shared<FKeyring>();
			Ring->Identity = "keyring:" + ContentSha256(Data);
			for (const auto& R : Root.at("keys"))
			{
				Keys(R, {"id", "version", "secret", "public", "purposes", "trusted"}, "key");
				FKeyring::FRecord Record;
				Record.Key.Id = R.at("id").get<std::string>();
				Record.Key.Version = R.at("version").get<std::string>();
				Token(Record.Key.Id);
				Token(Record.Key.Version);
				Record.Key.Secret = Unbase64(R.value("secret", ""));
				Record.Key.Public = Unbase64(R.value("public", ""));
				Record.Trusted = R.value("trusted", false);
				for (const auto& P : R.at("purposes"))
				{
					const auto Purpose = P.get<std::string>();
					if (Purpose != "encrypt" && Purpose != "decrypt" && Purpose != "sign" && Purpose != "verify")
					{
						Fail("key-purpose", "unknown key purpose");
					}
					if (!Record.Purposes.insert(Purpose).second)
					{
						Fail("key-purpose", "duplicate key purpose");
					}
				}
				if ((Record.Purposes.count("encrypt") || Record.Purposes.count("decrypt")) && Record.Key.Secret.size() != 32)
				{
					Fail("key-size", "encryption keys must contain 32 bytes");
				}
				if (Record.Purposes.count("sign") && Record.Key.Secret.size() != 64)
				{
					Fail("key-size", "signing keys must contain 64 bytes");
				}
				if (Record.Purposes.count("verify") && Record.Key.Public.size() != 32)
				{
					Fail("key-size", "verification keys must contain 32 bytes");
				}
				const auto Id = std::make_pair(Record.Key.Id, Record.Key.Version);
				if (!Ring->Records.emplace(Id, std::move(Record)).second)
				{
					Fail("duplicate-key", "duplicate key identity");
				}
			}
			Erase(Data);
			Error = {};
			return Ring;
		}
		catch (const FFailure& E)
		{
			Error = {E.Diagnostic.Code, "invalid protection keyring", Path, {}};
			return {};
		}
		catch (const std::exception&)
		{
			Error = {"keyring-format", "invalid protection keyring", Path, {}};
			return {};
		}
	}

	std::string FProtection::GetIdentity() const
	{
		return "protection:" + ContentSha256(J{{"crypto", Crypto ? Crypto->GetIdentity() : ""}, {"keys", Keys ? Keys->GetIdentity() : ""}, {"required", Policy.RequireSignature}, {"threshold", Policy.MinimumSignatures}, {"requiredKeys", Policy.RequiredKeys}, {"policy", Policy.Identity}}.dump());
	}
	bool FProtection::Decode(const FArtifactRequest& Request, const FArtifactWork& Work,
	    FArtifactData& Data, std::string& Error) const
	{
		Error.clear();
		try
		{
			FLimits Limits;
			Limits.MaxFileBytes = Work.MaxBytes;
			Limits.CheckCancelled = Work.CheckCancelled;
			Check(Limits);
			if (Data.SignatureRecords || (Data.TrustIdentity != GetIdentity() && Policy.RequireSignature))
			{
				std::string Records;
				if (Data.SignatureRecords)
				{
					Records = *Data.SignatureRecords;
				}
				else
				{
					auto MetadataLimits = Limits;
					MetadataLimits.MaxFileBytes = Limits.MaxMetadataBytes;
					const auto RecordBytes = ReadFile(PathUtil::FromUtf8(Request.Path + ".sig.json"), MetadataLimits);
					Records.assign(RecordBytes.begin(), RecordBytes.end());
				}
				if (Records.empty())
				{
					Fail("signature-set", "an existing artifact signature cannot be empty");
				}
				FDiagnostic Diagnostic;
				if (!Verify(Data.Bytes, Request.Kind, Records, Diagnostic, Limits))
				{
					Fail(Diagnostic.Code, Diagnostic.Message, Request.Path);
				}
				Data.TrustIdentity = GetIdentity();
				Data.SignatureRecords.reset();
			}
			if (Data.Bytes.size() < 4 || std::memcmp(Data.Bytes.data(), "VSAE", 4) != 0) return true;
			if (!Crypto || !Keys)
			{
				Fail("protection-service", "protected artifact requires crypto and key services");
			}
			if (Data.Bytes.size() < 8)
			{
				Fail("envelope", "truncated artifact envelope");
			}
			std::uint32_t Length = 0;
			for (unsigned I = 0; I < 4; ++I)
			{
				Length |= static_cast<std::uint32_t>(Data.Bytes[4 + I]) << (8 * I);
			}
			if (Length > 65536 || Length > Data.Bytes.size() - 8)
			{
				Fail("envelope-limit", "invalid artifact envelope header length");
			}
			const auto Header = Parse(std::string_view(reinterpret_cast<const char*>(Data.Bytes.data() + 8), Length), Limits);
			Detail::Keys(Header, {"format", "formatVersion", "kind", "algorithm", "keyId", "keyVersion", "nonce", "plaintextBytes"}, "envelope");
			const auto PlainSize = Count(Header.at("plaintextBytes"), "envelope.plaintextBytes");
			if (Header.value("format", "") != "veroscript-artifact-envelope" || Header.value("formatVersion", "") != "0.1.1" ||
			    Header.value("algorithm", "") != "xchacha20poly1305-ietf" || Header.at("kind") != Request.Kind ||
			    PlainSize > Work.MaxBytes || Data.Bytes.size() - 8 - Length < 16 ||
			    PlainSize != Data.Bytes.size() - 8 - Length - 16)
			{
				Fail("envelope", "artifact envelope kind, algorithm or size differs");
			}
			FProtectionKey Key;
			FSecretScope Eraser{Key};
			const auto Id = Header.at("keyId").get<std::string>();
			const auto Version = Header.at("keyVersion").get<std::string>();
			Token(Id);
			Token(Version);
			if (!Keys->Get(Id, Version, "decrypt", Key, Error) || Key.Id != Id || Key.Version != Version)
			{
				Fail("decryption-key", "decryption key is missing or unauthorized");
			}
			std::vector<std::uint8_t> Associated(Data.Bytes.begin(), Data.Bytes.begin() + 8 + Length);
			std::vector<std::uint8_t> Cipher(Data.Bytes.begin() + 8 + Length, Data.Bytes.end()), Plain;
			const auto Nonce = Unbase64(Header.at("nonce").get<std::string>());
			if (!Crypto->Decrypt(Cipher, Associated, Nonce, Key.Secret, Plain, Error) || Plain.size() != PlainSize)
			{
				Fail("authentication", "protected artifact authentication failed");
			}
			Check(Limits);
			Data.Bytes = std::move(Plain);
			return true;
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic.Message;
			return false;
		}
		catch (const J::exception&)
		{
			Error = "invalid artifact envelope metadata";
			return false;
		}
	}
	FProtection::FProtection(std::shared_ptr<const ICryptoProvider> Provider,
	    std::shared_ptr<const IProtectionKeys> KeySource, FTrustPolicy Trust)
	    : Crypto(std::move(Provider)), Keys(std::move(KeySource)), Policy(std::move(Trust))
	{
		Policy.MinimumSignatures = std::max<std::size_t>(1, Policy.MinimumSignatures);
		if (Policy.MinimumSignatures > 16)
		{
			throw std::invalid_argument("signature threshold exceeds the record budget");
		}
		if (!Policy.RequiredKeys.empty())
		{
			Policy.RequireSignature = true;
		}
	}
	bool FProtection::Encrypt(const std::vector<std::uint8_t>& Plain, const std::string& Kind,
	    const std::string& Reference, const std::string& Version, std::vector<std::uint8_t>& Out,
	    FDiagnostic& Error, const FLimits& Limits) const
	{
		Out.clear();
		try
		{
			Check(Limits);
			if (!Crypto || !Keys || Plain.size() > Limits.MaxFileBytes)
			{
				Fail("protection-service", "encryption services are missing or input exceeds budget");
			}
			if (Kind != "application-image" && Kind != "library" && Kind != "resource")
			{
				Fail("encryption-kind", "unsupported encryption subject kind");
			}
			FProtectionKey Key;
			FSecretScope Eraser{Key};
			std::string Failure;
			if (!Keys->Get(Reference, Version, "encrypt", Key, Failure))
			{
				Fail("encryption-key", "encryption key is missing or unauthorized");
			}
			Token(Key.Id);
			Token(Key.Version);
			std::vector<std::uint8_t> Nonce(24);
			if (!Crypto->Random(Nonce, Failure))
			{
				Fail("random", "cannot obtain a cryptographic nonce");
			}
			const auto Header = J{{"format", "veroscript-artifact-envelope"}, {"formatVersion", "0.1.1"},
			    {"kind", Kind}, {"algorithm", "xchacha20poly1305-ietf"}, {"keyId", Key.Id},
			    {"keyVersion", Key.Version}, {"nonce", Base64(Nonce)}, {"plaintextBytes", std::to_string(Plain.size())}}
			                        .dump();
			Out = Bytes("VSAE");
			Integer(Out, Header.size(), 4);
			Out.insert(Out.end(), Header.begin(), Header.end());
			if (Out.size() > Limits.MaxFileBytes || Plain.size() > Limits.MaxFileBytes - Out.size() ||
			    Limits.MaxFileBytes - Out.size() - Plain.size() < 16)
			{
				Fail("envelope-budget", "encrypted artifact exceeds its stored byte budget");
			}
			std::vector<std::uint8_t> Cipher;
			if (!Crypto->Encrypt(Plain, Out, Nonce, Key.Secret, Cipher, Failure))
			{
				Fail("encryption", "artifact encryption failed");
			}
			Out.insert(Out.end(), Cipher.begin(), Cipher.end());
			Check(Limits);
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Out.clear();
			Error = E.Diagnostic;
			return false;
		}
	}
	bool FProtection::Sign(const std::vector<std::uint8_t>& Data, const std::string& Kind,
	    const std::string& Reference, const std::string& Version, std::string& Records,
	    FDiagnostic& Error, const FLimits& Limits) const
	{
		Records.clear();
		try
		{
			Check(Limits);
			if (Data.size() > ((Kind == "application" || Kind == "distribution" || Kind == "generic") ? Limits.MaxArchiveBytes : Limits.MaxFileBytes))
			{
				Fail("signature-budget", "signature subject exceeds its byte budget");
			}
			if (!Crypto || !Keys)
			{
				Fail("protection-service", "signing requires crypto and key services");
			}
			FProtectionKey Key;
			FSecretScope Eraser{Key};
			std::string Failure;
			if (!Keys->Get(Reference, Version, "sign", Key, Failure))
			{
				Fail("signing-key", "signing key is missing or unauthorized");
			}
			J Record{{"format", "veroscript-artifact-signature"}, {"formatVersion", "0.1.1"},
			    {"kind", Kind}, {"algorithm", "ed25519"}, {"keyId", Key.Id}, {"keyVersion", Key.Version},
			    {"bytes", std::to_string(Data.size())}, {"sha256", ContentSha256(Data)}};
			std::vector<std::uint8_t> Signature;
			if (!Crypto->Sign(SignatureMessage(Record), Key.Secret, Signature, Failure))
			{
				Fail("signing", "artifact signing failed");
			}
			Record["signature"] = Base64(Signature);
			Records = J::array({Record}).dump();
			if (Records.size() > Limits.MaxMetadataBytes)
			{
				Fail("signature-budget", "signature records exceed metadata budget");
			}
			Check(Limits);
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic;
			return false;
		}
	}
	bool FProtection::Verify(const std::vector<std::uint8_t>& Data, const std::string& Kind,
	    const std::string& RecordsJson, FDiagnostic& Error, const FLimits& Limits) const
	{
		try
		{
			Check(Limits);
			if (Data.size() > ((Kind == "application" || Kind == "distribution" || Kind == "generic") ? Limits.MaxArchiveBytes : Limits.MaxFileBytes))
			{
				Fail("signature-budget", "signature subject exceeds its byte budget");
			}
			if (RecordsJson.empty())
			{
				if (Policy.RequireSignature)
				{
					Fail("signature-required", "a trusted signature is required");
				}
				Error = {};
				return true;
			}
			if (!Crypto || !Keys)
			{
				Fail("protection-service", "signature verification requires crypto and trusted key services");
			}
			const auto Records = Parse(RecordsJson, Limits);
			if (!Records.is_array() || Records.empty() || Records.size() > 16)
			{
				Fail("signature-set", "expected between one and sixteen signature records");
			}
			const auto Hash = ContentSha256(Data);
			std::set<std::string> Seen, Verified, PublicKeys;
			for (const auto& R : Records)
			{
				Check(Limits);
				Detail::Keys(R, {"format", "formatVersion", "algorithm", "kind", "bytes", "sha256", "keyId", "keyVersion", "signature"}, "signature");
				const auto Message = SignatureMessage(R);
				const auto Id = R.at("keyId").get<std::string>();
				const auto Version = R.at("keyVersion").get<std::string>();
				const auto Algorithm = R.at("algorithm").get<std::string>();
				const auto Identity = Algorithm + ":" + Id + "@" + Version + ":" + R.at("kind").get<std::string>();
				if (!Seen.insert(Identity).second || R.value("format", "") != "veroscript-artifact-signature" ||
				    R.value("formatVersion", "") != "0.1.1" || R.at("kind") != Kind ||
				    Count(R.at("bytes"), "signature.bytes") != Data.size() || R.at("sha256") != Hash)
				{
					Fail("signature-subject", "duplicate signature or signature subject differs");
				}
				const auto Signature = Unbase64(R.at("signature").get<std::string>());
				if (Algorithm != "ed25519")
				{
					continue;
				}
				FProtectionKey Key;
				FSecretScope Eraser{Key};
				std::string Failure;
				if (!Keys->Get(Id, Version, "verify", Key, Failure) || Key.Id != Id || Key.Version != Version ||
				    !Crypto->Verify(Message, Key.Public, Signature, Failure))
				{
					continue;
				}
				Verified.insert(Id + "@" + Version);
				PublicKeys.insert(ContentSha256(Key.Public));
			}
			if (PublicKeys.size() < std::max<std::size_t>(1, Policy.MinimumSignatures) ||
			    !std::includes(Verified.begin(), Verified.end(), Policy.RequiredKeys.begin(), Policy.RequiredKeys.end()))
			{
				Fail("signature-trust", "artifact signatures do not satisfy the host trust policy");
			}
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Error = E.Diagnostic;
			return false;
		}
		catch (const J::exception&)
		{
			Error = {"signature-format", "invalid signature record structure", {}, {}};
			return false;
		}
	}
	const FTrustPolicy& FProtection::GetPolicy() const noexcept
	{
		return Policy;
	}
}
