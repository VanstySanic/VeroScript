// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript
{
	inline constexpr std::uint64_t VEROSCRIPT_BYTECODE_MAX_BYTES =
		512ull * 1024ull * 1024ull;
	// Development layouts must be identified independently of the product version.
	inline constexpr std::uint64_t VEROSCRIPT_BYTECODE_LAYOUT = 0xD5F91E6A8B704C32ull;

	/**
	 * C++17-compatible, non-owning byte view used at package / bytecode boundaries.
	 * The caller retains ownership of Data for the duration of the call.
	 */
	struct FByteView
	{
		FByteView() = default;

		FByteView(const std::uint8_t* InData, const std::size_t InSize)
			: Data(InData)
			, Size(InSize)
		{
		}

		explicit FByteView(const std::vector<std::uint8_t>& Bytes)
			: Data(Bytes.data())
			, Size(Bytes.size())
		{
		}

		const std::uint8_t* Data = nullptr;
		std::size_t Size = 0;

	};

	/**
	 * Serialize / deserialize FBytecodeModule with source fingerprint validation.
	 */
	class VeroScriptBytecodeIO
	{
	public:
		/**
		 * Validate structural bytecode invariants required by both the VM and JIT.
		 * This is suitable for host-built modules as well as deserialized caches.
		 */
		static bool ValidateModule(
			const FBytecodeModule& Module,
			std::string& OutError);

		/**
		 * Empty CacheRootDirectory returns the project-local fallback path:
		 * `<script-dir>/.verocache/<basename>.vsbc`.
		 * With CacheRootDirectory: `<root>/<path-hash>/<basename>.vsbc` so same basenames
		 * from different directories do not collide. Engine/CLI callers normally resolve
		 * an empty setting to the platform user cache before calling this helper.
		 */
		static std::string GetCachePathForScript(
			std::string_view ScriptPath,
			std::string_view CacheRootDirectory = {});

		/**
		 * Canonical, platform-independent VM bytecode. No JIT/native machine code
		 * is emitted. The bytecode magic and version identify the binary layout;
		 * callers may deliberately skip only source-fingerprint matching.
		 */
		static bool SerializeBytecode(
			const FBytecodeModule& Module,
			std::uint64_t SourceFingerprint,
			std::vector<std::uint8_t>& OutBytes,
			std::string& OutError);

		static bool WriteFile(
			const std::string& Path,
			const FBytecodeModule& Module,
			std::uint64_t SourceFingerprint);

		// Write a cache file and preserve the concrete validation/I/O failure.
		static bool WriteFile(
			const std::string& Path,
			const FBytecodeModule& Module,
			std::uint64_t SourceFingerprint,
			std::string& OutError);

		static std::unique_ptr<FBytecodeModule> ReadFile(
			const std::string& Path,
			std::uint64_t ExpectedFingerprint,
			std::string& OutError);

		static std::unique_ptr<FBytecodeModule> DeserializeBytecode(
			FByteView Bytes,
			std::optional<std::uint64_t> ExpectedFingerprint,
			std::string& OutError);
	};
}
