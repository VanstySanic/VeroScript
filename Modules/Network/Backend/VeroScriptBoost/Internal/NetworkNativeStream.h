// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "NetworkAsio.h"
#include "VeroScriptNetworkTls.h"
#include <boost/asio/async_result.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/beast/core/role.hpp>

#if defined(_WIN32) && !defined(VEROSCRIPT_NETWORK_STATIC_RUNTIME)
#if defined(VEROSCRIPT_NETWORK_RUNTIME_BUILD)
#define VEROSCRIPT_NETWORK_RUNTIME_API __declspec(dllexport)
#else
#define VEROSCRIPT_NETWORK_RUNTIME_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define VEROSCRIPT_NETWORK_RUNTIME_API __attribute__((visibility("default")))
#else
#define VEROSCRIPT_NETWORK_RUNTIME_API
#endif

namespace VeroScript::Network::Detail
{
	class FNativeTlsContext
	{
		friend class FNativeStream;

	public:
		static VEROSCRIPT_NETWORK_RUNTIME_API std::shared_ptr<FNativeTlsContext> Create(
			const FTlsOptions&, bool Server, FError&);
		VEROSCRIPT_NETWORK_RUNTIME_API ~FNativeTlsContext();
		static VEROSCRIPT_NETWORK_RUNTIME_API bool Available() noexcept;

	private:
		struct FImpl;
		VEROSCRIPT_NETWORK_RUNTIME_API explicit FNativeTlsContext(std::shared_ptr<FImpl>);
		std::shared_ptr<FImpl> Impl;
	};

	// One transport type for HTTP and WebSocket; TLS stays in the shared runtime.
	// A moved transport can carry pre-read bytes into a protocol upgrade.
	class FNativeStream
	{
	public:
		using executor_type = Asio::any_io_executor;
		using FCompletion = std::function<void(FSystemError, std::size_t)>;
		using FSimpleCompletion = std::function<void(FSystemError)>;
		VEROSCRIPT_NETWORK_RUNTIME_API FNativeStream(Asio::io_context&, std::shared_ptr<FNativeTlsContext> Tls = {});
		VEROSCRIPT_NETWORK_RUNTIME_API FNativeStream(FTcp::socket, std::shared_ptr<FNativeTlsContext> Tls = {});
		VEROSCRIPT_NETWORK_RUNTIME_API ~FNativeStream();
		VEROSCRIPT_NETWORK_RUNTIME_API FNativeStream(FNativeStream&&) noexcept;
		VEROSCRIPT_NETWORK_RUNTIME_API FNativeStream& operator=(FNativeStream&&) noexcept;
		FNativeStream(const FNativeStream&) = delete;
		FNativeStream& operator=(const FNativeStream&) = delete;
		VEROSCRIPT_NETWORK_RUNTIME_API executor_type get_executor() noexcept;
		VEROSCRIPT_NETWORK_RUNTIME_API void Connect(FTcp::resolver::results_type, FSimpleCompletion);
		// CONNECT uses the raw socket before TLS negotiation; credentials belong only to the proxy.
		VEROSCRIPT_NETWORK_RUNTIME_API void Tunnel(std::string Authority, std::string Authorization, FSimpleCompletion);
		VEROSCRIPT_NETWORK_RUNTIME_API void Handshake(bool Server, std::string Host, FSimpleCompletion,
			std::optional<std::chrono::milliseconds> Timeout = std::chrono::seconds(30));
		VEROSCRIPT_NETWORK_RUNTIME_API void Shutdown(FSimpleCompletion);
		VEROSCRIPT_NETWORK_RUNTIME_API void Close() noexcept;
		VEROSCRIPT_NETWORK_RUNTIME_API void Prepend(FBytes);
		template <class Sequence, class Token>
		auto async_read_some(const Sequence& Buffers, Token&& Completion)
		{
			return Asio::async_initiate<Token, void(FSystemError, std::size_t)>(
				[this, Buffers](auto Handler) mutable
				{
					auto Executor = Asio::get_associated_executor(Handler, get_executor());
					auto Owned = std::make_shared<decltype(Handler)>(std::move(Handler));
					std::vector<Asio::mutable_buffer> Views(
						Asio::buffer_sequence_begin(Buffers), Asio::buffer_sequence_end(Buffers));
					Read(std::move(Views),
						[Owned, Executor](FSystemError Error, std::size_t Bytes) mutable
						{
							Asio::dispatch(Executor,
								[Owned, Error, Bytes]() mutable
								{
									(*Owned)(Error, Bytes);
								});
						});
				},
				Completion);
		}
		template <class Sequence, class Token>
		auto async_write_some(const Sequence& Buffers, Token&& Completion)
		{
			return Asio::async_initiate<Token, void(FSystemError, std::size_t)>(
				[this, Buffers](auto Handler) mutable
				{
					auto Executor = Asio::get_associated_executor(Handler, get_executor());
					auto Owned = std::make_shared<decltype(Handler)>(std::move(Handler));
					std::vector<Asio::const_buffer> Views(
						Asio::buffer_sequence_begin(Buffers), Asio::buffer_sequence_end(Buffers));
					Write(std::move(Views),
						[Owned, Executor](FSystemError Error, std::size_t Bytes) mutable
						{
							Asio::dispatch(Executor,
								[Owned, Error, Bytes]() mutable
								{
									(*Owned)(Error, Bytes);
								});
						});
				},
				Completion);
		}

	private:
		VEROSCRIPT_NETWORK_RUNTIME_API void Read(std::vector<Asio::mutable_buffer>, FCompletion);
		VEROSCRIPT_NETWORK_RUNTIME_API void Write(std::vector<Asio::const_buffer>, FCompletion);
		struct FImpl;
		std::shared_ptr<FImpl> Impl;
	};
	inline void beast_close_socket(FNativeStream& Stream)
	{
		Stream.Close();
	}
	template <class Handler>
	void async_teardown(boost::beast::role_type, FNativeStream& Stream, Handler&& Completion)
	{
		auto Executor = Asio::get_associated_executor(Completion, Stream.get_executor());
		auto Owned = std::make_shared<std::decay_t<Handler>>(std::forward<Handler>(Completion));
		Stream.Shutdown(
			[Owned, Executor](FSystemError Error) mutable
			{
				Asio::dispatch(Executor,
					[Owned, Error]() mutable
					{
						(*Owned)(Error);
					});
			});
	}
} // namespace VeroScript::Network::Detail
