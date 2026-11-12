// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptNetwork.h"
#include <system_error>

namespace VeroScript::Network::Backend
{
	inline constexpr const char* ServiceId = "network";
	inline constexpr const char* ContractId = "veroscript.network";
	inline constexpr const char* ContractVersion = "0.1.1";
	// Identifies the opaque foundation implementation and configuration in this native image.
	const char* FoundationIdentity() noexcept;
	using FCompletion = std::function<void(std::error_code)>;

	// Runtime objects and their callbacks are confined to the context's I/O executor.
	class ITimer
	{
	public:
		virtual ~ITimer() = default;
		virtual void Cancel() noexcept = 0;
		virtual void ExpiresAt(std::chrono::steady_clock::time_point Deadline) = 0;
		virtual void AsyncWait(FCompletion Completion) = 0;
	};

	// Borrows the native socket. Release/retirement never closes the caller's socket.
	class ISocketWatch
	{
	public:
		virtual ~ISocketWatch() = default;
		virtual void Cancel() noexcept = 0;
		virtual void Release() noexcept = 0;
		virtual void AsyncWait(bool Read, FCompletion Completion) = 0;
	};

	class IRuntime
	{
	public:
		virtual ~IRuntime() = default;
		virtual const char* GetBackendId() const noexcept = 0;
		virtual void Run() = 0;
		virtual void Stop() noexcept = 0;
		virtual void Restart() = 0;
		virtual std::size_t PollOne() = 0;
		virtual void ReleaseWork() noexcept = 0;
		// Thread-safe; always enqueue for Run/PollOne, including while stopped.
		// Never invoke inline: shutdown joins workers before draining queued work.
		virtual void Post(std::function<void()> Function) = 0;
		virtual std::shared_ptr<ITimer> CreateTimer() = 0;
		virtual std::shared_ptr<ISocketWatch> WatchSocket(std::uintptr_t Socket, std::error_code& Error) = 0;
		// Versioned provider interoperability hook; ordinary consumers never need it.
		virtual void* QueryInterface(std::string_view Contract) noexcept = 0;
	};

	class ITcpSocket
	{
	public:
		virtual ~ITcpSocket() = default;
		virtual FOperation Read(std::uint64_t MaxBytes, FOperationOptions Options) = 0;
		virtual FOperation Write(FBytes Bytes, FOperationOptions Options) = 0;
		virtual void RequestClose() = 0;
		virtual void Dispose() noexcept = 0;
	};

	class ITcpListener
	{
	public:
		virtual ~ITcpListener() = default;
		virtual std::uint16_t Port() const noexcept = 0;
		virtual FOperation Accept(FOperationOptions Options) = 0;
		virtual void RequestClose() = 0;
		virtual void Dispose() noexcept = 0;
	};

	class IUdpSocket
	{
	public:
		virtual ~IUdpSocket() = default;
		virtual std::uint16_t Port() const noexcept = 0;
		virtual FOperation SendTo(FBytes Bytes, FAddress Peer, FOperationOptions Options) = 0;
		virtual FOperation Receive(std::uint64_t MaxBytes, FOperationOptions Options) = 0;
		virtual FOperation SetBroadcast(bool Enabled, FOperationOptions Options) = 0;
		virtual FOperation Multicast(FMulticastGroup Group, bool Join, FOperationOptions Options) = 0;
		virtual void RequestClose() = 0;
		virtual void Dispose() noexcept = 0;
	};

	// Factories implemented by a backend; facade construction remains owned by Network.
	struct FAccess
	{
		static std::shared_ptr<FTcpSocket> Wrap(const std::shared_ptr<Detail::FContext>&, std::shared_ptr<ITcpSocket>);
		static std::shared_ptr<FTcpListener> Wrap(const std::shared_ptr<Detail::FContext>&, std::shared_ptr<ITcpListener>);
		static std::shared_ptr<FUdpSocket> Wrap(const std::shared_ptr<Detail::FContext>&, std::shared_ptr<IUdpSocket>);
	};

	class IService
	{
	public:
		virtual ~IService() = default;
		virtual const char* GetBackendId() const noexcept = 0;
		virtual const char* GetFoundationIdentity() const noexcept = 0;
		virtual bool TlsAvailable() const noexcept = 0;
		virtual std::shared_ptr<IRuntime> CreateRuntime() = 0;
		virtual FOperation Resolve(FContext&, std::string Host, std::uint16_t Port, FOperationOptions) = 0;
		virtual FOperation ConnectTcp(FContext&, std::string Host, std::uint16_t Port, FOperationOptions) = 0;
		virtual std::shared_ptr<FTcpListener> ListenTcp(FContext&, std::string Address, std::uint16_t Port,
			FError&, int Backlog) = 0;
		virtual std::shared_ptr<FUdpSocket> BindUdp(FContext&, std::string Address, std::uint16_t Port, FError&) = 0;
	};
} // namespace VeroScript::Network::Backend
