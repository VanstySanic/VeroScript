// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNativeExtensionLoader.h"
#include "VeroScriptPath.h"
#if defined(VEROSCRIPT_STB_STATIC_PROVIDER)
#include "VeroScriptStbModule.h"
#endif
#include <filesystem>
#include <iostream>

#if defined(_WIN32)
int wmain(int Count, wchar_t **Arguments)
#else
int main(int Count, char **Arguments)
#endif
{
	std::string Error;
	#if defined(VEROSCRIPT_STB_STATIC_PROVIDER)
	auto Module = VeroScript::QueryNativeExtension(veroscript_stb_query, {}, Error);
	#else
	if (Count != 2)
	{
		return 1;
	}
	auto Module = VeroScript::LoadNativeExtension(
		VeroScript::PathUtil::ToUtf8(std::filesystem::absolute(Arguments[1])), Error);
	#endif
	VeroScript::FNativeExtensionRegistry Registry;
	VeroScript::FNativeExtensionRegistrationOptions Registration;
	Registration.Origin.ProviderId = "installed-provider";
	if (!Module || !Registry.Add(Module, Registration, Error) || Registry.GetProviders().size() != 1)
	{
		std::cerr << Error;
		return 2;
	}
	return 0;
}
