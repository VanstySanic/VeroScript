// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptAnnotationProcessing.h"
#include "VeroScriptCompileProcessing.h"
#include <memory>

namespace VeroScript
{
	struct FProgram;
	struct FNativeFunction;
	class FPreparedProgram;

	class IHostBindingCatalog;
	struct FProgramPreparationResult
	{
		bool Succeeded() const
		{
			return bool(Prepared);
		}

		std::shared_ptr<const FPreparedProgram> Prepared;
		std::vector<FScriptError> Diagnostics;
		std::vector<FAnnotationDependency> Dependencies;
	};

	class FProgramPreparationSeal
	{
		friend class VeroScriptProgramPreparation;
		FProgramPreparationSeal(std::string Host, std::string Configuration)
		    : HostFingerprint(std::move(Host)), ConfigurationFingerprint(std::move(Configuration))
		{
		}
		std::string HostFingerprint, ConfigurationFingerprint;
	};
	class FPreparedProgram
	{
	public:
		const FProgram &GetProgram() const
		{
			return *Program;
		}
		const std::vector<FAnnotationDeclarationView> &GetDeclarations() const
		{
			return Declarations;
		}
		const std::vector<FAnnotationDependency> &GetDependencies() const
		{
			return Dependencies;
		}
		const std::vector<FCompileGenerationOperation> &GetGeneratedOperations() const
		{
			return GetCompileGeneratedOperations();
		}
		const std::vector<FGeneratedDeclarationDocument> &GetGeneratedDocuments() const;
		const std::string &GetFingerprint() const
		{
			return Fingerprint;
		}
		bool IsCacheable() const
		{
			return bCacheable;
		}
		bool MatchesHost(const std::vector<FNativeFunction> *, const IHostBindingCatalog *) const;
		const FSourceMetadataSnapshot &GetSourceMetadata() const;
		const std::vector<FCompileGenerationOperation> &GetCompileGeneratedOperations() const;
		bool MatchesProcessing(const FCompileProcessingOptions &, const IHostBindingCatalog * = nullptr) const;
		bool MatchesDependencies(const FCompileProcessingOptions &) const;

	private:
		friend class VeroScriptProgramPreparation;
		std::shared_ptr<const FProgram> Program;
		std::vector<FAnnotationDeclarationView> Declarations;
		std::vector<FAnnotationDependency> Dependencies;
		std::vector<FAnnotationGenerationOperation> Operations;
		std::string Fingerprint, HostFingerprint;
		bool bCacheable = true;
	};
	class VeroScriptProgramPreparation
	{
	public:
		// Eligibility/identity for a cache lookup before declaration preparation.
		// Combine with the loader's complete source/resolution fingerprints and the
		// compiler build identity. Processor, package replay and host-bound inputs
		// require the normal prepared-program dependency/identity validation.
		static bool TryGetCacheInputFingerprint(const FProgram &, const FAnnotationProcessingOptions &,
		    std::string &OutFingerprint, const std::vector<FNativeFunction> * = nullptr,
		    const IHostBindingCatalog * = nullptr);
		static bool MatchesHost(const FProgram &, const std::vector<FNativeFunction> *,
		    const IHostBindingCatalog *);
		static FProgramPreparationResult Prepare(std::unique_ptr<FProgram>,
		    const FAnnotationProcessingOptions & = {},
		    const std::vector<FNativeFunction> * = nullptr,
		    const IHostBindingCatalog * = nullptr);
		static FProgramPreparationResult Prepare(const FProgram &, const FAnnotationProcessingOptions & = {},
		    const std::vector<FNativeFunction> * = nullptr,
		    const IHostBindingCatalog * = nullptr);
		static FProgramPreparationResult PrepareWithProcessing(std::unique_ptr<FProgram>,
		    const FCompileProcessingOptions &, const std::vector<FNativeFunction> * = nullptr,
		    const IHostBindingCatalog * = nullptr);
	};
} // namespace VeroScript
