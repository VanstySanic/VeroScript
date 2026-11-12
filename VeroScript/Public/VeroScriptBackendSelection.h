// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript
{
	class FModuleEnvironment;
	class IJitBackendFactory;
	class IVmBackendFactory;
	struct FEngineConfig;

	inline constexpr std::string_view AutomaticBackend = "auto";
	inline constexpr std::string_view DisabledBackend = "none";

	enum class EBackendSelectionState
	{
		Selected,
		Disabled,
		Unavailable,
		Failed
	};

	struct FBackendAttempt
	{
		std::string BackendId;
		std::string Error;
	};

	struct FBackendSelectionResult
	{
		EBackendSelectionState State = EBackendSelectionState::Unavailable;
		std::string BackendId;
		std::vector<FBackendAttempt> Rejected;
		std::string Error;
	};

	// An omitted request inherits the environment. A present empty configuration
	// value disables the service; explicit "auto" overrides a configured default.
	std::string ResolveBackendSelection(std::string_view Service, std::string_view Request,
		const std::map<std::string, std::string>& Configuration = {});

	// Only factories/preparation may be attempted here. A running service never
	// returns to this selector. Cancellation escapes rather than trying another backend.
	FBackendSelectionResult SelectServiceBackend(std::string_view Service, std::string_view Selection,
		std::vector<std::string> Candidates,
		const std::function<bool(std::string_view, std::string&)>& TryCandidate,
		const std::function<void()>& CheckCancelled = {});

	struct FNativeServiceCreateContext
	{
		std::shared_ptr<const FModuleEnvironment> Environment;
		std::map<std::string, std::string> Configuration;
		std::function<void()> CheckCancelled;
	};

	struct FNativeServiceProviderDesc
	{
		std::shared_ptr<void> Owner;
		std::string ServiceId, BackendId, ContractId, ContractVersion, ConfigurationKey;
		void* UserData = nullptr;
		void* (*Create)(const FNativeServiceCreateContext&, void*, std::string& Error) = nullptr;
		void (*Destroy)(void*, void*) = nullptr;
	};

	// Package/host adapters provide immutable candidates through public Core
	// contracts. Core does not scan installations or depend on a package manager.
	struct FExecutionBackendResolvers
	{
		std::string Identity;
		std::function<bool(std::string_view, std::shared_ptr<const IJitBackendFactory>&, std::string&)> Jit;
		std::function<bool(std::string_view, std::shared_ptr<const IVmBackendFactory>&, std::string&)> Vm;
	};

	bool ResolveExecutionBackends(const FEngineConfig& Input, FEngineConfig& Output, std::string& Error);
}
