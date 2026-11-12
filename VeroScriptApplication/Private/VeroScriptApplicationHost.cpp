// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptApplication.h"
#include "ApplicationInternal.h"
#include "VeroScriptNativeExtension.h"
#include "VeroScriptVmModule.h"
#include "VeroScriptPlatform.h"
#include "VeroScriptVersion.h"
#include <cstdlib>
#include <regex>
#if defined(_WIN32)
#include <shlobj.h>
#endif

namespace VeroScript::Application
{
	bool ValidateApplicationTarget(std::string_view Json, FDiagnostic& Error, const FLimits& Limits)
	{
		try
		{
			const auto T = Detail::Parse(Json, Limits);
			Detail::Keys(T, {"core", "coreBuild", "os", "arch", "environment", "abis", "minimumVersions", "capabilities"}, "target");
			static const std::regex Id("[A-Za-z_][A-Za-z0-9_.-]{0,254}"), Digest("[0-9a-f]{64}");
			if (T.at("core") != FormatVersion(VEROSCRIPT_VERSION) || !std::regex_match(T.at("coreBuild").get<std::string>(), Digest))
			{
				Detail::Fail("application-target", "target requires the exact Core version and a canonical Core build identity");
			}
			for (const auto* K : {"os", "arch", "environment"})
			{
				if (!std::regex_match(T.at(K).get<std::string>(), Id))
				{
					Detail::Fail("application-target", "invalid platform identifier", K);
				}
			}
			for (const auto* K : {"abis", "minimumVersions"})
			{
				if (!T.at(K).is_object() || T.at(K).size() > 4096)
				{
					Detail::Fail("application-target", "invalid target requirement map", K);
				}
				for (auto I = T.at(K).begin(); I != T.at(K).end(); ++I)
				{
					if (!std::regex_match(I.key(), Id) || !I.value().is_string() || I.value().get<std::string>().empty() ||
					    I.value().get_ref<const std::string&>().size() > 65536 || I.value().get_ref<const std::string&>().find('\0') != std::string::npos)
					{
						Detail::Fail("application-target", "invalid ABI or minimum version requirement", K);
					}
				}
			}
			if (!T.at("capabilities").is_array() || T.at("capabilities").size() > 4096)
			{
				Detail::Fail("application-target", "invalid target capabilities");
			}
			std::set<std::string> Seen;
			for (const auto& C : T.at("capabilities"))
			{
				if (!C.is_string() || !std::regex_match(C.get<std::string>(), Id) || !Seen.insert(C.get<std::string>()).second)
				{
					Detail::Fail("application-target", "invalid or duplicate capability identifier");
				}
			}
			Error = {};
			return true;
		}
		catch (const Detail::FFailure& E)
		{
			Error = E.Diagnostic;
			return false;
		}
		catch (const std::exception&)
		{
			Error = {"application-target", "incomplete or incorrectly typed application target", {}, {}};
			return false;
		}
	}
	std::string HostApplicationTargetJson()
	{
		const auto P = GetHostPlatformInfo();
		Detail::J Capabilities = Detail::J::array();
		if (P.bAllowsNativeJit)
		{
			Capabilities.push_back("native-jit");
		}
		if (P.bAllowsDynamicJitLibraries)
		{
			Capabilities.push_back("dynamic-jit");
		}
		return Detail::J{{"core", FormatVersion(VEROSCRIPT_VERSION)}, {"coreBuild", VEROSCRIPT_CORE_IMPLEMENTATION_ID},
		    {"os", P.OperatingSystem}, {"arch", P.Architecture}, {"environment", P.Environment},
		    {"abis", {{"native-extension", BuildNativeExtensionAbi()}, {"jit", BuildJitModuleAbi()}, {"vm", BuildVmModuleAbi()}}},
		    {"minimumVersions", Detail::J::object()}, {"capabilities", Capabilities}}
		    .dump();
	}
	std::string DefaultApplicationCacheRoot()
	{
		#if defined(_WIN32)
		PWSTR Directory = nullptr;
		if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &Directory))) return {};
		const auto Root = std::filesystem::path(Directory);
		CoTaskMemFree(Directory);
		return PathUtil::ToUtf8((Root / L"VeroScript" / L"Cache" / L"Applications"));
		#else
		const char* Xdg = std::getenv("XDG_CACHE_HOME");
		if (Xdg && PathUtil::FromUtf8(Xdg).is_absolute()) return PathUtil::ToUtf8((PathUtil::FromUtf8(Xdg) / "VeroScript/Applications"));
		const char* UserHome = std::getenv("HOME");
		if (UserHome && PathUtil::FromUtf8(UserHome).is_absolute())
		{
			#if defined(__APPLE__)
			return PathUtil::ToUtf8((PathUtil::FromUtf8(UserHome) / "Library/Caches/VeroScript/Applications"));
			#else
			return PathUtil::ToUtf8((PathUtil::FromUtf8(UserHome) / ".cache/VeroScript/Applications"));
			#endif
		}
		return {};
		#endif
	}
	FApplicationServices CreateApplicationServices()
	{
		FApplicationServices S;
		S.CacheRoot = DefaultApplicationCacheRoot();
		const auto ExecutableDirectory = PathUtil::FromUtf8(PathUtil::ExecutableDirectory());
		for (const auto& Root : {ExecutableDirectory / "stdlib", ExecutableDirectory / "../assets/stdlib"})
		{
			if (std::filesystem::is_directory(Root))
			{
				S.StandardLibraryPaths.push_back(PathUtil::ToUtf8(Root.lexically_normal()));
				break;
			}
		}
		S.ValidateTarget = [](const std::string& Target, std::string& Error)
		{
			try
			{
				const auto Actual = Detail::Parse(Target, {});
				const auto Expected = Detail::Parse(HostApplicationTargetJson(), {});
				if (Actual != Expected)
				{
					Error = "application target or native ABI does not match this Runtime";
					return false;
				}
				return true;
			}
			catch (...)
			{
				Error = "invalid application target descriptor";
				return false;
			}
		};
		S.ValidateRequirements = [](const std::string& Text, const FModuleEnvironment* Environment, std::string& Error)
		{
			try
			{
				const auto Requirements = Detail::Parse(Text, {});
				if (!Requirements.is_array())
				{
					Error = "application requirements must be an array";
					return false;
				}
				std::vector<FExternalDependency> Required;
				for (const auto& R : Requirements)
				{
					Detail::Keys(R, {"id", "version", "abi", "buildIdentity", "contentIdentity"}, "dependency");
					Required.push_back({R.at("id"), R.at("version"), R.at("abi"), R.at("buildIdentity"), R.at("contentIdentity")});
				}
				return CheckExternalDependencies(Required, Environment ? Environment->GetDescription().AvailableDependencies : std::vector<FExternalDependency>{}, Error);
			}
			catch (...)
			{
				Error = "invalid application dependency identity";
				return false;
			}
		};
		return S;
	}
}
