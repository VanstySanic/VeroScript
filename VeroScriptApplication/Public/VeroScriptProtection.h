// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptArchive.h"

#include <set>

namespace VeroScript::Application
{
	struct FProtectionKey
	{
		std::string Id, Version;
		std::vector<std::uint8_t> Secret, Public;
	};
	struct FProtectionOptions
	{
		std::string EncryptionKey, EncryptionVersion, SigningKey, SigningVersion;
		std::string SignatureScope = "content";
	};
	struct FTrustPolicy
	{
		bool RequireSignature = false;
		std::size_t MinimumSignatures = 1;
		std::set<std::string> RequiredKeys;
		std::string Identity;
	};
	class IProtectionKeys
	{
	public:
		virtual ~IProtectionKeys() = default;
		virtual std::string GetIdentity() const = 0;
		virtual bool Get(const std::string& Key, const std::string& Version,
			const std::string& Purpose, FProtectionKey&, std::string& Error) const = 0;
	};
	class ICryptoProvider
	{
	public:
		virtual ~ICryptoProvider() = default;
		virtual std::string GetIdentity() const = 0;
		virtual bool Random(std::vector<std::uint8_t>&, std::string& Error) const = 0;
		virtual bool Encrypt(const std::vector<std::uint8_t>& Plain,
			const std::vector<std::uint8_t>& Associated, const std::vector<std::uint8_t>& Nonce,
			const std::vector<std::uint8_t>& Key, std::vector<std::uint8_t>& Cipher, std::string& Error) const = 0;
		virtual bool Decrypt(const std::vector<std::uint8_t>& Cipher,
			const std::vector<std::uint8_t>& Associated, const std::vector<std::uint8_t>& Nonce,
			const std::vector<std::uint8_t>& Key, std::vector<std::uint8_t>& Plain, std::string& Error) const = 0;
		virtual bool Sign(const std::vector<std::uint8_t>& Message, const std::vector<std::uint8_t>& Key,
			std::vector<std::uint8_t>& Signature, std::string& Error) const = 0;
		virtual bool Verify(const std::vector<std::uint8_t>& Message, const std::vector<std::uint8_t>& Key,
			const std::vector<std::uint8_t>& Signature, std::string& Error) const = 0;
	};
	// Loading is explicit and the factory retains the native module lease.
	std::shared_ptr<const ICryptoProvider> LoadSodiumCrypto(const std::string& AbsoluteLibrary,
		FDiagnostic&);
	std::shared_ptr<const IProtectionKeys> LoadProtectionKeys(const std::string& AbsoluteKeyring,
		FDiagnostic&, const FLimits& = {});

	class FProtection final : public IArtifactDecoder
	{
	public:
		std::string GetIdentity() const override;
		bool Decode(const FArtifactRequest&, const FArtifactWork&, FArtifactData&,
			std::string& Error) const override;
		FProtection(std::shared_ptr<const ICryptoProvider>, std::shared_ptr<const IProtectionKeys>,
			FTrustPolicy = {});
		bool Encrypt(const std::vector<std::uint8_t>&, const std::string& Kind,
			const std::string& Key, const std::string& Version, std::vector<std::uint8_t>&,
			FDiagnostic&, const FLimits& = {}) const;
		bool Sign(const std::vector<std::uint8_t>&, const std::string& Kind,
			const std::string& Key, const std::string& Version, std::string& RecordsJson,
			FDiagnostic&, const FLimits& = {}) const;
		bool Verify(const std::vector<std::uint8_t>&, const std::string& Kind,
			const std::string& RecordsJson, FDiagnostic&, const FLimits& = {}) const;
		const FTrustPolicy& GetPolicy() const noexcept;
	private:
		std::shared_ptr<const ICryptoProvider> Crypto;
		std::shared_ptr<const IProtectionKeys> Keys;
		FTrustPolicy Policy;
	};
}
