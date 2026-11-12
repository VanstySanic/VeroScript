// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptHttpModule.h"
#include "VeroScriptHttp.h"
#include "HttpBinding.h"
#include "NetworkModule.h"
#include "VeroScriptNetworkBuildInfo.h"

namespace VeroScript::Http
{
	namespace
	{
		using namespace NetworkBinding;
		#include "HttpRegistration.inl"

		struct FModuleTraits
		{
			static constexpr const char* Id = "http";
			static constexpr const char* DiagnosticId = "http.binding";
			static constexpr const char* OperationType = "http::operation";
			static constexpr auto RegisterTypes = &Register;
		};
	} // namespace

	const char* GetImplementationIdentity() noexcept
	{
		return VEROSCRIPT_NETWORK_IMPLEMENTATION_ID;
	}
	std::uint32_t VEROSCRIPT_EXTENSION_CALL QueryModule(
		std::uint32_t RequestedVersion, std::uint32_t Size, void* Descriptor)
	{
		return TModule<FModuleTraits>::Query(RequestedVersion, Size, Descriptor);
	}
} // namespace VeroScript::Http
