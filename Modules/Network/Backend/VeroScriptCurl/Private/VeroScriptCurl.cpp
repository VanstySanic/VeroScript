// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptCurl.h"

namespace VeroScript::Http
{
	std::shared_ptr<Backend::IClient> CreateCurlClient(Network::FContext&, FClientOptions);
}
namespace VeroScript::Curl
{
	namespace
	{
		class FHttpClientService final : public Http::Backend::IClientService
		{
		public:
			const char* GetBackendId() const noexcept override
			{
				return "curl";
			}
			const char* GetFoundationIdentity() const noexcept override
			{
				return Network::Backend::FoundationIdentity();
			}
			std::shared_ptr<Http::Backend::IClient> Create(Network::FContext& Context, Http::FClientOptions Options) override
			{
				return Http::CreateCurlClient(Context, std::move(Options));
			}
		};
	}
	std::unique_ptr<Http::Backend::IClientService> NewHttpClientService()
	{
		return std::make_unique<FHttpClientService>();
	}
	std::shared_ptr<Http::Backend::IClientService> CreateHttpClientService()
	{
		return NewHttpClientService();
	}
} // namespace VeroScript::Curl
