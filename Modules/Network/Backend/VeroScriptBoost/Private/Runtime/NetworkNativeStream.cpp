// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#define BOOST_ASIO_SOURCE
#include "NetworkNativeStream.h"
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/empty_body.hpp>
#if VEROSCRIPT_NETWORK_TLS
#include <boost/asio/ssl.hpp>
#include <openssl/ssl.h>
#include <openssl/err.h>
#ifdef _WIN32
#include <wincrypt.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif
#endif
#endif

namespace VeroScript::Network::Detail
{
	struct FNativeTlsContext::FImpl
	{
		#if VEROSCRIPT_NETWORK_TLS
		explicit FImpl(bool Server) : Context(Server ? Asio::ssl::context::tls_server : Asio::ssl::context::tls_client)
		{
		}
		Asio::ssl::context Context;
		#endif
	};
	std::shared_ptr<FNativeTlsContext> FNativeTlsContext::Create(const FTlsOptions& Options, bool Server, FError& Error)
	{
		Error = {};
		#if VEROSCRIPT_NETWORK_TLS
		try
		{
			std::uint64_t Bytes = 0;
			for (const auto* Text :
				{&Options.TrustPem, &Options.CertificateChainPem, &Options.PrivateKeyPem, &Options.PrivateKeyPassword})
			{
				if (Text->size() > 4 * 1024 * 1024 - Bytes)
				{
					Error.Code = EError::Limit;
					Error.Message = "TLS configuration limit exceeded";
					return {};
				}
				Bytes += Text->size();
			}
			if (Options.CertificateChainPem.empty() != Options.PrivateKeyPem.empty() ||
				(Server && Options.CertificateChainPem.empty()))
			{
				Error.Code = EError::InvalidArgument;
				Error.Message = "TLS server requires a certificate chain and matching private key";
				return {};
			}
			auto P = std::make_shared<FImpl>(Server);
			P->Context.set_options(Asio::ssl::context::default_workarounds);
			if (!SSL_CTX_set_min_proto_version(P->Context.native_handle(), TLS1_2_VERSION))
			{
				throw std::runtime_error("Cannot configure TLS protocol");
			}
			if (Options.bUseSystemTrust && !Server)
			{
				P->Context.set_default_verify_paths();
				#ifdef _WIN32
				// OpenSSL's filesystem defaults do not enumerate the Windows ROOT store.
				HCERTSTORE Store = CertOpenSystemStoreW(0, L"ROOT");
				if (Store)
				{
					PCCERT_CONTEXT Cert = nullptr;
					while ((Cert = CertEnumCertificatesInStore(Store, Cert)))
					{
						const auto* Encoded = Cert->pbCertEncoded;
						X509* Parsed = d2i_X509(nullptr, &Encoded, Cert->cbCertEncoded);
						if (Parsed)
						{
							X509_STORE_add_cert(SSL_CTX_get_cert_store(P->Context.native_handle()), Parsed);
							X509_free(Parsed);
						}
					}
					CertCloseStore(Store, 0);
					ERR_clear_error();
				}
				#elif defined(__APPLE__) && TARGET_OS_OSX
				// A redistributed OpenSSL build has no macOS keychain lookup.
				// Copy the system's trusted anchors without changing trust settings.
				CFArrayRef Anchors = nullptr;
				if (SecTrustCopyAnchorCertificates(&Anchors) == errSecSuccess && Anchors)
				{
					for (CFIndex Index = 0; Index < CFArrayGetCount(Anchors); ++Index)
					{
						const auto Certificate = static_cast<SecCertificateRef>(
							const_cast<void*>(CFArrayGetValueAtIndex(Anchors, Index)));
						const auto Data = SecCertificateCopyData(Certificate);
						if (!Data)
						{
							continue;
						}
						const auto* Encoded = CFDataGetBytePtr(Data);
						X509* Parsed = d2i_X509(nullptr, &Encoded, CFDataGetLength(Data));
						if (Parsed)
						{
							X509_STORE_add_cert(SSL_CTX_get_cert_store(P->Context.native_handle()), Parsed);
							X509_free(Parsed);
						}
						CFRelease(Data);
					}
				}
				if (Anchors)
				{
					CFRelease(Anchors);
				}
				ERR_clear_error();
				#endif
			}
			if (!Options.TrustPem.empty())
			{
				P->Context.add_certificate_authority(Asio::buffer(Options.TrustPem));
			}
			if (!Options.PrivateKeyPassword.empty())
			{
				P->Context.set_password_callback(
					[Password = Options.PrivateKeyPassword](auto, auto)
					{
						return Password;
					});
			}
			if (!Options.CertificateChainPem.empty())
			{
				P->Context.use_certificate_chain(Asio::buffer(Options.CertificateChainPem));
				P->Context.use_private_key(Asio::buffer(Options.PrivateKeyPem), Asio::ssl::context::pem);
				if (!SSL_CTX_check_private_key(P->Context.native_handle()))
				{
					throw std::runtime_error("TLS certificate and key do not match");
				}
			}
			P->Context.set_verify_mode(Server ? Asio::ssl::verify_none : Asio::ssl::verify_peer);
			return std::shared_ptr<FNativeTlsContext>(new FNativeTlsContext(std::move(P)));
		}
		catch (const std::bad_alloc&)
		{
			Error.Code = EError::Allocation;
		}
		catch (const std::exception& E)
		{
			Error.Code = EError::InvalidArgument;
			Error.Message = E.what();
		}
		#else
		(void)Options;
		(void)Server;
		Error.Code = EError::Unsupported;
		Error.Message = "TLS is not enabled in this network runtime";
		#endif
		return {};
	}

	FNativeTlsContext::~FNativeTlsContext() = default;

	bool FNativeTlsContext::Available() noexcept
	{
		return VEROSCRIPT_NETWORK_TLS != 0;
	}

	FNativeTlsContext::FNativeTlsContext(std::shared_ptr<FImpl> P) : Impl(std::move(P))
	{
	}

	struct FNativeStream::FImpl : std::enable_shared_from_this<FImpl>
	{
		// TLS owns its socket; plain transports use Socket directly.
		std::shared_ptr<FNativeTlsContext> Tls;
		#if VEROSCRIPT_NETWORK_TLS
		std::unique_ptr<Asio::ssl::stream<FTcp::socket>> Secure;
		#endif
		std::unique_ptr<FTcp::socket> Plain;
		FBytes Replay;
		std::shared_ptr<Asio::steady_timer> HandshakeTimer;
		std::size_t ReplayOffset = 0;
		FImpl(FTcp::socket Socket, std::shared_ptr<FNativeTlsContext> T) : Tls(std::move(T))
		{
			#if VEROSCRIPT_NETWORK_TLS
			if (Tls)
			{
				Secure = std::make_unique<Asio::ssl::stream<FTcp::socket>>(std::move(Socket), Tls->Impl->Context);
			}
			else
			#endif
				Plain = std::make_unique<FTcp::socket>(std::move(Socket));
		}
		FTcp::socket& Socket()
		{
			#if VEROSCRIPT_NETWORK_TLS
			if (Secure)
			{
				return Secure->next_layer();
			}
			#endif
			return *Plain;
		}
	};
	FNativeStream::FNativeStream(Asio::io_context& Io, std::shared_ptr<FNativeTlsContext> Tls)
		: FNativeStream(FTcp::socket(Io), std::move(Tls))
	{
	}
	FNativeStream::FNativeStream(FTcp::socket Socket, std::shared_ptr<FNativeTlsContext> Tls)
		: Impl(std::make_shared<FImpl>(std::move(Socket), std::move(Tls)))
	{
	}
	FNativeStream::~FNativeStream() = default;
	FNativeStream::FNativeStream(FNativeStream&&) noexcept = default;
	FNativeStream& FNativeStream::operator=(FNativeStream&&) noexcept = default;
	FNativeStream::executor_type FNativeStream::get_executor() noexcept
	{
		return Impl->Socket().get_executor();
	}

	void FNativeStream::Connect(FTcp::resolver::results_type Addresses, FSimpleCompletion Done)
	{
		auto P = Impl;
		Asio::async_connect(P->Socket(),
			Addresses,
			[P, Done = std::move(Done)](FSystemError E, const auto&) mutable
			{
				Done(E);
			});
	}

	void FNativeStream::Tunnel(std::string Authority, std::string Authorization, FSimpleCompletion Done)
	{
		// Bound both the request and each response head. The caller reserves this
		// storage against its context before starting the physical connection.
		auto Valid = [](std::string_view Text)
		{
			return std::all_of(Text.begin(),
				Text.end(),
				[](unsigned char C)
				{
					return C >= 0x20 && C < 0x7f;
				});
		};
		if (Authority.empty() || Authority.size() > 4096 || Authorization.size() > 4096 || !Valid(Authority) ||
			!Valid(Authorization))
		{
			Asio::post(get_executor(),
				[Done = std::move(Done)]() mutable
				{
					Done(Asio::error::invalid_argument);
				});
			return;
		}
		struct FTunnel : std::enable_shared_from_this<FTunnel>
		{
			std::shared_ptr<FImpl> Transport;
			std::string Request, Response;
			FSimpleCompletion Done;
			unsigned Informational = 0;
			void Read()
			{
				Asio::async_read_until(Transport->Socket(),
					Asio::dynamic_buffer(Response, 65536),
					"\r\n\r\n",
					[Self = shared_from_this()](FSystemError Error, std::size_t Count)
					{
						if (Error)
						{
							Self->Done(Error);
							return;
						}
						// CONNECT success switches protocols at the end of the head.
						// RFC 9110 9.3.6 requires ignoring both framing fields, even
						// when they conflict. Neutralize their names before the general
						// HTTP parser, retaining its field/value syntax validation.
						std::string Head(Self->Response.data(), Count);
						auto Equal = [](std::string_view A, std::string_view B)
						{
							if (A.size() != B.size())
							{
								return false;
							}
							for (std::size_t I = 0; I < A.size(); ++I)
							{
								const unsigned char C = A[I];
								if ((C >= 'A' && C <= 'Z' ? C + ('a' - 'A') : C) != B[I])
								{
									return false;
								}
							}
							return true;
						};
						for (auto At = Head.find("\r\n") + 2; At < Head.size() - 2;)
						{
							const auto End = Head.find("\r\n", At), Colon = Head.find(':', At);
							if (End == Head.npos)
							{
								break;
							}
							if (Colon < End)
							{
								const std::string_view Name(Head.data() + At, Colon - At);
								if (Equal(Name, "content-length") || Equal(Name, "transfer-encoding"))
								{
									Head[At] = 'X';
								}
							}
							At = End + 2;
						}
						boost::beast::http::response_parser<boost::beast::http::empty_body> Parser;
						Parser.header_limit(65536);
						Parser.skip(true);
						Parser.put(Asio::buffer(Head), Error);
						if (Error || !Parser.is_header_done())
						{
							Self->Done(Error ? Error : Asio::error::invalid_argument);
							return;
						}
						const auto Status = Parser.get().result_int();
						Self->Response.erase(0, Count);
						if (Status >= 100 && Status < 200 && Status != 101 && ++Self->Informational <= 8)
						{
							Self->Read();
							return;
						}
						if (Status < 200 || Status >= 300)
						{
							Self->Done(Asio::error::connection_refused);
							return;
						}
						// A TLS peer cannot send a handshake response before ClientHello.
						// For a plain tunnel, preserve any first protocol bytes already read.
						if (!Self->Response.empty())
						{
							if (Self->Transport->Tls)
							{
								Self->Done(Asio::error::invalid_argument);
								return;
							}
							Self->Transport->Replay.assign(Self->Response.begin(), Self->Response.end());
							Self->Transport->ReplayOffset = 0;
						}
						Self->Done({});
					});
			}
		};
		auto State = std::make_shared<FTunnel>();
		State->Transport = Impl;
		State->Done = std::move(Done);
		State->Request = "CONNECT " + Authority + " HTTP/1.1\r\nHost: " + Authority + "\r\n";
		if (!Authorization.empty())
		{
			State->Request += "Proxy-Authorization: " + Authorization + "\r\n";
		}
		State->Request += "\r\n";
		Asio::async_write(Impl->Socket(),
			Asio::buffer(State->Request),
			[State](FSystemError Error, std::size_t)
			{
				if (Error)
				{
					State->Done(Error);
				}
				else
				{
					State->Read();
				}
			});
	}

	void FNativeStream::Handshake(
		bool Server, std::string Host, FSimpleCompletion Done, std::optional<std::chrono::milliseconds> Timeout)
	{
		auto P = Impl;
		#if VEROSCRIPT_NETWORK_TLS
		if (P->Secure)
		{
			std::shared_ptr<Asio::steady_timer> Timer;
			if (Timeout)
			{
				Timer = std::make_shared<Asio::steady_timer>(get_executor());
				P->HandshakeTimer = Timer;
				Timer->expires_after(std::max(std::chrono::milliseconds(0), *Timeout));
				Timer->async_wait(
					[Weak = std::weak_ptr<FImpl>(P)](FSystemError Error)
					{
						if (!Error)
						{
							if (auto State = Weak.lock())
							{
								FSystemError Ignore;
								State->Socket().close(Ignore);
							}
						}
					});
			}
			if (!Server)
			{
				FSystemError AddressError;
				Asio::ip::make_address(Host, AddressError);
				if (AddressError && !SSL_set_tlsext_host_name(P->Secure->native_handle(), Host.c_str()))
				{
					Asio::post(get_executor(),
						[Done = std::move(Done)]() mutable
						{
							Done(Asio::error::invalid_argument);
						});
					return;
				}
				P->Secure->set_verify_callback(Asio::ssl::host_name_verification(Host));
			}
			P->Secure->async_handshake(Server ? Asio::ssl::stream_base::server : Asio::ssl::stream_base::client,
				[P, Timer, Done = std::move(Done)](FSystemError Error) mutable
				{
					if (Timer)
					{
						Timer->cancel();
					}
					P->HandshakeTimer.reset();
					Done(Error);
				});
			return;
		}
		#else
		(void)Server;
		(void)Host;
		(void)Timeout;
		#endif
		Asio::post(get_executor(),
			[P, Done = std::move(Done)]() mutable
			{
				Done({});
			});
	}

	void FNativeStream::Shutdown(FSimpleCompletion Done)
	{
		auto P = Impl;
		#if VEROSCRIPT_NETWORK_TLS
		if (P->Secure)
		{
			P->Secure->async_shutdown(
				[P, Done = std::move(Done)](FSystemError Error) mutable
				{
					FSystemError Ignore;
					P->Socket().close(Ignore);
					Done(Error);
				});
			return;
		}
		#endif
		Close();
		Asio::post(get_executor(),
			[P, Done = std::move(Done)]() mutable
			{
				Done({});
			});
	}

	void FNativeStream::Close() noexcept
	{
		if (Impl)
		{
			FSystemError Ignore;
			Impl->Socket().cancel(Ignore);
			Impl->Socket().close(Ignore);
		}
	}

	void FNativeStream::Prepend(FBytes Bytes)
	{
		auto& P = *Impl;
		Bytes.insert(Bytes.end(), P.Replay.begin() + static_cast<std::ptrdiff_t>(P.ReplayOffset), P.Replay.end());
		P.Replay = std::move(Bytes);
		P.ReplayOffset = 0;
	}

	void FNativeStream::Read(std::vector<Asio::mutable_buffer> Buffers, FCompletion Done)
	{
		auto P = Impl;
		if (P->ReplayOffset < P->Replay.size())
		{
			const auto Count = Asio::buffer_copy(
				Buffers, Asio::buffer(P->Replay.data() + P->ReplayOffset, P->Replay.size() - P->ReplayOffset));
			P->ReplayOffset += Count;
			if (P->ReplayOffset == P->Replay.size())
			{
				P->Replay.clear();
				P->ReplayOffset = 0;
			}
			Asio::post(get_executor(),
				[P, Done = std::move(Done), Count]() mutable
				{
					Done({}, Count);
				});
			return;
		}
		auto Complete = [P, Done = std::move(Done)](FSystemError E, std::size_t N) mutable
		{
			Done(E, N);
		};
		#if VEROSCRIPT_NETWORK_TLS
		if (P->Secure)
		{
			P->Secure->async_read_some(Buffers, std::move(Complete));
			return;
		}
		#endif
		P->Socket().async_read_some(Buffers, std::move(Complete));
	}

	void FNativeStream::Write(std::vector<Asio::const_buffer> Buffers, FCompletion Done)
	{
		auto P = Impl;
		auto Complete = [P, Done = std::move(Done)](FSystemError E, std::size_t N) mutable
		{
			Done(E, N);
		};
		#if VEROSCRIPT_NETWORK_TLS
		if (P->Secure)
		{
			P->Secure->async_write_some(Buffers, std::move(Complete));
			return;
		}
		#endif
		P->Socket().async_write_some(Buffers, std::move(Complete));
	}
} // namespace VeroScript::Network::Detail
