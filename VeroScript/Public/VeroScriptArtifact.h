// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace VeroScript
{
	struct FArtifactRequest
	{
		std::string Path, Kind, OwnerId;
	};
	struct FArtifactWork
	{
		std::uint64_t MaxBytes = 640ull * 1024ull * 1024ull;
		std::function<void()> CheckCancelled;
	};
	struct FArtifactData
	{
		// Retain content and service leases until the last consumer releases them.
		std::shared_ptr<const void> Owner;
		std::vector<std::uint8_t> Bytes;
		std::string ContentIdentity;
		// Set only by a trusted reader after applying the matching trust policy.
		std::string TrustIdentity;
		// Presence is distinct from an absent signature, including an invalid empty file.
		std::optional<std::string> SignatureRecords;
	};
	class IArtifactReader
	{
	public:
		virtual ~IArtifactReader() = default;
		virtual std::string GetIdentity() const = 0;
		// A configured reader owns resolution; failure never enables file fallback.
		virtual bool Read(const FArtifactRequest&, const FArtifactWork&, FArtifactData&,
			std::string& Error) const = 0;
	};
	class IArtifactDecoder
	{
	public:
		virtual ~IArtifactDecoder() = default;
		virtual std::string GetIdentity() const = 0;
		// Called for both protected and plain inputs. Policy belongs to the decoder.
		virtual bool Decode(const FArtifactRequest&, const FArtifactWork&, FArtifactData&,
			std::string& Error) const = 0;
	};
	bool ReadArtifactFile(const FArtifactRequest&, const FArtifactWork&, FArtifactData&,
		std::string& Error);
	bool ReadArtifact(const FArtifactRequest&, const FArtifactWork&, FArtifactData&,
		std::string& Error, const IArtifactReader* Reader = nullptr,
		const IArtifactDecoder* Decoder = nullptr);
	bool DecodeArtifact(const FArtifactRequest&, const FArtifactWork&, FArtifactData&,
		std::string& Error, const IArtifactDecoder* Decoder = nullptr);
}
