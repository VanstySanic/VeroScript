// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"
#include "BoostNetworkBackend.h"
#include "NetworkNativeStream.h"
#if VEROSCRIPT_BOOST_HTTP
#include "VeroScriptHttpBackend.h"
#endif
#if VEROSCRIPT_BOOST_WEBSOCKET
#include "VeroScriptWebSocketBackend.h"
#endif

namespace VeroScript::Http
{
	#if VEROSCRIPT_BOOST_HTTP
	std::shared_ptr<FServer> BoostListenHttp(Network::FContext&, std::string, std::uint16_t,
	    Network::FError&, FServerOptions, Backend::FExchangeFactory);
	#endif
}
namespace VeroScript::WebSocket
{
	#if VEROSCRIPT_BOOST_WEBSOCKET
	Network::FOperation BoostConnectWebSocket(const std::shared_ptr<Network::Detail::FContext>&, std::string, FOptions);
	Network::FOperation BoostAcceptWebSocketUpgrade(Network::FContext&, std::shared_ptr<Network::FUpgradeSource>, FOptions);
	std::shared_ptr<FServer> BoostListenWebSocket(Network::FContext&, std::string, std::uint16_t, Network::FError&, FOptions);
	#endif
}

namespace VeroScript::Boost
{
	VEROSCRIPT_NETWORK_RUNTIME_API std::shared_ptr<Network::Backend::IRuntime> CreateRuntime();
	namespace N = Network::Detail;
	namespace
	{
		bool Compatible(const std::shared_ptr<N::FContext>& Context, Network::FError& Error)
		{
			if (N::Runtime(Context)->QueryInterface("veroscript.boost.asio-context/1")) return true;
			Error = {Network::EError::Unsupported, "Boost service requires a compatible Boost network context", 0};
			return false;
		}
		class FNetworkService final : public Network::Backend::IService
		{
		public:
			const char* GetBackendId() const noexcept override
			{
				return "boost";
			}
			const char* GetFoundationIdentity() const noexcept override
			{
				return Network::Backend::FoundationIdentity();
			}
			bool TlsAvailable() const noexcept override
			{
				return N::FNativeTlsContext::Available();
			}
			std::shared_ptr<Network::Backend::IRuntime> CreateRuntime() override
			{
				return Boost::CreateRuntime();
			}
			Network::FOperation Resolve(Network::FContext& C, std::string Host, std::uint16_t Port, Network::FOperationOptions O) override
			{
				Network::FError Error;
				const auto State = N::FAccess::Context(C);
				if (!Compatible(State, Error))
				{
					auto Op = N::Begin(State, O);
					N::Fail(Op, Error.Code, std::move(Error.Message));
					return N::FAccess::Wrap(Op);
				}
				return Network::BoostResolve(C, std::move(Host), Port, O);
			}
			Network::FOperation ConnectTcp(Network::FContext& C, std::string Host, std::uint16_t Port, Network::FOperationOptions O) override
			{
				Network::FError Error;
				const auto State = N::FAccess::Context(C);
				if (!Compatible(State, Error))
				{
					auto Op = N::Begin(State, O);
					N::Fail(Op, Error.Code, std::move(Error.Message));
					return N::FAccess::Wrap(Op);
				}
				return Network::BoostConnectTcp(C, std::move(Host), Port, O);
			}
			std::shared_ptr<Network::FTcpListener> ListenTcp(Network::FContext& C, std::string Address,
			    std::uint16_t Port, Network::FError& E, int Backlog) override
			{
				if (!Compatible(N::FAccess::Context(C), E)) return {};
				return Network::BoostListenTcp(C, std::move(Address), Port, E, Backlog);
			}
			std::shared_ptr<Network::FUdpSocket> BindUdp(Network::FContext& C, std::string Address,
			    std::uint16_t Port, Network::FError& E) override
			{
				if (!Compatible(N::FAccess::Context(C), E)) return {};
				return Network::BoostBindUdp(C, std::move(Address), Port, E);
			}
		};
		#if VEROSCRIPT_BOOST_HTTP
		class FHttpServerService final : public Http::Backend::IServerService
		{
		public:
			const char* GetBackendId() const noexcept override
			{
				return "boost";
			}
			const char* GetFoundationIdentity() const noexcept override
			{
				return Network::Backend::FoundationIdentity();
			}
			std::shared_ptr<Http::FServer> Listen(Network::FContext& C, std::string Address, std::uint16_t Port,
			    Network::FError& E, Http::FServerOptions O, Http::Backend::FExchangeFactory Factory) override
			{
				if (!Compatible(N::FAccess::Context(C), E)) return {};
				return Http::BoostListenHttp(C, std::move(Address), Port, E, std::move(O), Factory);
			}
		};
		#endif
		#if VEROSCRIPT_BOOST_WEBSOCKET
		class FWebSocketService final : public WebSocket::Backend::IService
		{
		public:
			const char* GetBackendId() const noexcept override
			{
				return "boost";
			}
			const char* GetFoundationIdentity() const noexcept override
			{
				return Network::Backend::FoundationIdentity();
			}
			bool CompressionSupported() const noexcept override
			{
				return VEROSCRIPT_WEBSOCKET_COMPRESSION != 0;
			}
			Network::FOperation Connect(const std::shared_ptr<N::FContext>& C, std::string Url, WebSocket::FOptions O) override
			{
				Network::FError E;
				if (!Compatible(C, E))
				{
					auto Op = N::Begin(C, O.Operation);
					N::Fail(Op, E.Code, std::move(E.Message));
					return N::FAccess::Wrap(Op);
				}
				return WebSocket::BoostConnectWebSocket(C, std::move(Url), std::move(O));
			}
			Network::FOperation AcceptUpgrade(Network::FContext& C, std::shared_ptr<Network::FUpgradeSource> Source,
			    WebSocket::FOptions O) override
			{
				Network::FError E;
				if (!Compatible(N::FAccess::Context(C), E))
				{
					auto Op = N::Begin(N::FAccess::Context(C), O.Operation);
					N::Fail(Op, E.Code, std::move(E.Message));
					return N::FAccess::Wrap(Op);
				}
				return WebSocket::BoostAcceptWebSocketUpgrade(C, std::move(Source), std::move(O));
			}
			std::shared_ptr<WebSocket::FServer> Listen(Network::FContext& C, std::string Address, std::uint16_t Port,
			    Network::FError& E, WebSocket::FOptions O) override
			{
				if (!Compatible(N::FAccess::Context(C), E)) return {};
				return WebSocket::BoostListenWebSocket(C, std::move(Address), Port, E, std::move(O));
			}
		};
		#endif
	}
	std::unique_ptr<Network::Backend::IService> NewNetworkService()
	{
		return std::make_unique<FNetworkService>();
	}
	std::shared_ptr<Network::Backend::IService> CreateNetworkService()
	{
		return NewNetworkService();
	}
	#if VEROSCRIPT_BOOST_HTTP
	std::unique_ptr<Http::Backend::IServerService> NewHttpServerService()
	{
		return std::make_unique<FHttpServerService>();
	}
	#endif
	#if VEROSCRIPT_BOOST_WEBSOCKET
	std::unique_ptr<WebSocket::Backend::IService> NewWebSocketService()
	{
		return std::make_unique<FWebSocketService>();
	}
	#endif
	std::shared_ptr<Http::Backend::IServerService> CreateHttpServerService()
	{
		#if VEROSCRIPT_BOOST_HTTP
		return NewHttpServerService();
		#else
		return {};
		#endif
	}
	std::shared_ptr<WebSocket::Backend::IService> CreateWebSocketService()
	{
		#if VEROSCRIPT_BOOST_WEBSOCKET
		return NewWebSocketService();
		#else
		return {};
		#endif
	}
} // namespace VeroScript::Boost
