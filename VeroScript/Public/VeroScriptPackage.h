// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecodeIO.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <optional>

namespace VeroScript
{
	inline constexpr std::uint32_t VEROSCRIPT_PACKAGE_MAGIC = 0x4B505356u; // "VSPK"
	inline constexpr std::uint16_t VEROSCRIPT_PACKAGE_FORMAT_MAJOR = 1;
	inline constexpr std::uint16_t VEROSCRIPT_PACKAGE_FORMAT_MINOR = 0;
	inline constexpr std::uint64_t VEROSCRIPT_PACKAGE_MAX_BYTES =
		640ull * 1024ull * 1024ull;

	enum class EPackageKind : std::uint8_t
	{
		Application = 1,
		Library = 2,
	};
	struct FCompileExtensionDependency
	{
		std::string Path;
		std::string RegistrationEntry = "register_extension";
	};
	struct FCompileExtensionManifest
	{
		std::string ModuleId;
		std::string RegistrationEntry = "register_extension";
		std::string Version = "0.1.1";
		std::string ServiceFingerprint;
		std::string HostBindingFingerprint;
		std::vector<FCompileExtensionDependency> Dependencies;
	};

	/**
	 * Binary-library ABI v1:
	 * - `import "Library.vslib"` and configured implicit imports expose exported
	 *   non-generic free functions, enums, aliases, structs, classes, interfaces,
	 *   and globals through an embedded declaration interface.
	 * - private functions remain implementation-only; `export import` controls
	 *   transitive exposure exactly as it does for source modules.
	 * - the compiler links dependency bytecode into one validated closed image,
	 *   so linked VM code needs no .vslib reads at runtime and VM/JIT dispatch stays direct.
	 *
	 * - Libraries may contain already-linked .vslib dependencies. Their exported
	 *   interfaces retain ordinary import/re-export boundaries, while the closed
	 *   dependency bytecode makes the resulting package self-contained.
	 * - Component ownership deduplicates shared/diamond dependencies and their
	 *   initializer segments. Reusing a package name/version for different component
	 *   fingerprints is rejected rather than resolved by input order.
	 * - Open generic exports carry a preprocessed template module graph. Consumers
	 *   instantiate it normally; closed/non-generic definitions still come from
	 *   bytecode and are never compiled twice.
	 */

	/**
	 * Host-neutral package inputs. PackageVersion belongs to the application /
	 * deployment policy; it is deliberately independent from package-format and
	 * bytecode versions.
	 *
	 * HostMetadata values are opaque to VeroScript. A host can use namespaced
	 * keys such as "ue.binding_abi.v1". Signatures and encryption envelopes stay
	 * outside this structure so the signed byte range is never self-referential.
	 */
	struct FPackageBuildOptions
	{
		EPackageKind Kind = EPackageKind::Application;
		// Stable logical identity used by diagnostics and dependency linking.
		// Required for both application and library packages.
		std::string PackageName;
		std::uint64_t PackageVersion = 0;
		std::map<std::string, std::vector<std::uint8_t>> HostMetadata;
		// Remove source paths, line tables, and local debug names from the packaged
		// copy. Runtime linkage, declarations, captures, and reload schema remain.
		bool bStripDebugInfo = false;
		std::optional<FCompileExtensionManifest> CompileExtension;
	};

	struct FPackageData
	{
		EPackageKind Kind = EPackageKind::Application;
		std::string PackageName;
		std::uint64_t PackageVersion = 0;
		std::map<std::string, std::vector<std::uint8_t>> HostMetadata;
		/**
		 * Exact canonical bytes accepted by ReadPackage. Verify a detached host
		 * signature against this range before trusting or reloading Module.
		 */
		std::vector<std::uint8_t> CanonicalSignedBytes;
		std::unique_ptr<FBytecodeModule> Module;
		std::optional<FCompileExtensionManifest> CompileExtension;
	};

	/**
	 * Build one deterministic, platform-independent Core application image or .vslib.
	 * Application images are the inner bytecode payload, not .vsapp containers.
	 * Use the Application module to assemble and archive a distributable .vsapp.
	 * The contained VM bytecode has source fingerprint zero because source-cache
	 * identity is not part of a deployed package's trust policy.
	 */
	bool BuildPackage(
		const FBytecodeModule& Module,
		const FPackageBuildOptions& Options,
		std::vector<std::uint8_t>& OutBytes,
		std::string& OutError);

	/**
	 * Strictly parse a canonical Core application image or .vslib. Unknown host metadata is preserved
	 * unchanged. Noncanonical ordering/encoding, trailing data, invalid bytecode,
	 * and unsafe lengths are rejected.
	 */
	bool ReadPackage(
		FByteView Bytes,
		FPackageData& OutPackage,
		std::string& OutError);
}
