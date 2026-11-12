// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNativeExtension.h"

namespace VeroScript
{
	struct FNativeExtensionLoadOptions
	{
		std::shared_ptr<const void> FileLease;
		std::string ExpectedId, ExpectedVersion;
		// Static registrations supply a stable implementation identity to allow
		// processor caching. Dynamic loading hashes the actual library bytes.
		std::string ContentIdentity;
		std::function<void()> CheckCancelled;
		std::uint32_t ExpectedRoles = 0;
	};
	class FLoadedNativeExtension final
	{
	public:
		~FLoadedNativeExtension();
		const FNativeExtensionDescriptor& GetDescriptor() const noexcept;
		const std::string& GetPath() const noexcept;
		const std::string& GetContentIdentity() const noexcept;
	private:
		friend class FNativeExtensionRegistry;
		friend std::shared_ptr<const FLoadedNativeExtension> LoadNativeExtension(const std::string&,
			std::string&, FNativeExtensionLoadOptions);
		friend std::shared_ptr<const FLoadedNativeExtension> QueryNativeExtension(FNativeExtensionQuery,
			std::shared_ptr<const void>, std::string&, FNativeExtensionLoadOptions);
		struct FImpl;
		explicit FLoadedNativeExtension(std::shared_ptr<FImpl>);
		std::shared_ptr<FImpl> Impl;
	};
	// Explicit absolute file only. No PATH/cwd/installation discovery. Static
	// extensions use QueryNativeExtension with the same POD query and SDK checks.
	std::shared_ptr<const FLoadedNativeExtension> LoadNativeExtension(const std::string& Path,
		std::string& Error, FNativeExtensionLoadOptions Options = {});
	std::shared_ptr<const FLoadedNativeExtension> QueryNativeExtension(FNativeExtensionQuery,
		std::shared_ptr<const void> Owner, std::string& Error, FNativeExtensionLoadOptions Options = {});
}
