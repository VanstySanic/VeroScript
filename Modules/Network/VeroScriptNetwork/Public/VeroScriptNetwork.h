// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <any>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VeroScript::Network
{
	namespace Backend
	{
		class IService;
		class ITcpSocket;
		class ITcpListener;
		class IUdpSocket;
		struct FAccess;
	}
	enum class EError : std::uint8_t
	{
		None,
		InvalidArgument,
		Unsupported,
		QueueFull,
		Cancelled,
		TimedOut,
		Closed,
		Resolve,
		Connect,
		Read,
		Write,
		Protocol,
		Limit,
		State,
		Callback,
		Allocation,
		Internal
	};

	enum class EOperationState : std::uint8_t
	{
		Created,
		Queued,
		Running,
		Succeeded,
		Failed,
		Cancelled,
		TimedOut
	};

	enum class EWaitResult : std::uint8_t
	{
		Completed,
		WaitTimedOut,
		RequiresPoll
	};

	enum class EDriveMode : std::uint8_t
	{
		NativeBackground,
		HostDriven
	};

	inline constexpr const char* Version = "0.1.1";
	using FBytes = std::vector<std::uint8_t>;

	struct FError
	{
		explicit operator bool() const noexcept;
		const char* Id() const noexcept;
		EError Code = EError::None;
		std::string Message;
		int SystemCode = 0;
	};

	struct FContextOptions
	{
		EDriveMode Mode = EDriveMode::NativeBackground;
		std::uint32_t MaxOperations = 1024;
		std::uint32_t MaxCallbacks = 1024;
		std::uint32_t MaxConnections = 256;
		std::uint32_t MaxQueuedWrites = 64;
		std::uint64_t MaxBufferBytes = 16 * 1024 * 1024;
		// Transport-owned payloads and retained results share this context-wide budget.
		std::uint64_t MaxRetainedBytes = 256 * 1024 * 1024;
	};

	struct FMemoryStatistics
	{
		std::uint64_t RetainedBytes = 0;
		std::uint64_t PeakRetainedBytes = 0;
		std::uint64_t LimitBytes = 0;
	};

	struct FOperationOptions
	{
		// Nullopt means infinite. A finite deadline starts at submission.
		std::optional<std::chrono::milliseconds> Timeout = std::chrono::seconds(30);
	};

	struct FQueryParameter
	{
		std::string Name;
		std::string Value;
		bool bHasEquals = true;
	};

	struct FUrl
	{
		static bool TryParse(std::string_view Text, FUrl& Out, FError& Error);
		// Resolve a reference against an absolute hierarchical network URL (RFC 3986).
		static bool TryResolve(std::string_view Base, std::string_view Reference, FUrl& Out, FError& Error);
		std::string Authority() const;
		std::string Target() const;
		std::string String(bool bIncludeFragment = true) const;
		std::string Scheme;
		std::string Host;
		std::uint16_t Port = 0;
		bool bExplicitPort = false;
		bool bIpv6 = false;
		std::string Path;
		std::string Query;
		bool bHasQuery = false;
		std::string Fragment;
		bool bHasFragment = false;
	};

	struct FAddress
	{
		std::string Address;
		std::uint16_t Port = 0;
		bool bIpv6 = false;
	};

	struct FReadResult
	{
		FBytes Bytes;
		bool bEnd = false;
	};

	struct FDatagram
	{
		FAddress Peer;
		FBytes Bytes;
		std::uint64_t OriginalBytes = 0;
		bool bTruncated = false;
	};

	struct FMulticastGroup
	{
		std::string Group;
		// IPv4 interface address (empty selects INADDR_ANY); IPv6 uses InterfaceIndex.
		std::string InterfaceAddress;
		std::uint32_t InterfaceIndex = 0;
	};

	namespace Detail
	{
		struct FContext;
		struct FOperation;
		struct FAccess;
	} // namespace Detail

	class FOperation
	{
		friend struct Detail::FAccess;

	public:
		FOperation() = default;
		bool IsValid() const noexcept;
		EOperationState State() const noexcept;
		bool IsComplete() const noexcept;
		bool Cancel() const;
		FError Error() const;
		EWaitResult Wait(std::chrono::milliseconds ObservationTimeout) const;
		// Registered callbacks run exclusively during owner-thread Dispatch.
		bool OnComplete(std::function<void(const FOperation&)> Callback, FError& Error) const;
		template <class T>
		std::shared_ptr<const T> Result() const
		{
			auto Owner = ResultValue(typeid(T).name(), sizeof(T), alignof(T));
			const T* Value = static_cast<const T*>(Owner.get());
			return Value ? std::shared_ptr<const T>(std::move(Owner), Value) : nullptr;
		}

	private:
		std::shared_ptr<const void> ResultValue(const char* Type, std::size_t Size, std::size_t Alignment) const;
		std::shared_ptr<Detail::FOperation> Impl;
	};

	class FContext
	{
		friend struct Detail::FAccess;

	public:
		explicit FContext(std::shared_ptr<Backend::IService> Service, FContextOptions Options = {});
		~FContext();
		FContext(const FContext&) = delete;
		FContext& operator=(const FContext&) = delete;
		void Shutdown();
		bool IsClosed() const noexcept;
		EDriveMode Mode() const noexcept;
		const char* GetBackendId() const noexcept;
		// Captured in the creating image; readable before accessing its opaque state.
		const char* GetFoundationIdentity() const noexcept
		{
			return Identity;
		}
		FMemoryStatistics MemoryUsage() const noexcept;
		// HostDriven only: polls ready handlers; completion callbacks use Dispatch.
		// Deferred shutdown may reclaim outstanding native work before returning.
		std::size_t PollIo(FError& Error, std::size_t MaxHandlers = 64);
		std::size_t Dispatch(FError& Error, std::size_t MaxCallbacks = 64);
		// Retains native backend code until all I/O and retained results are destroyed.
		// Owner must have a control block. Aliases (including nullptr) retain that ownership.
		bool RetainCodeOwner(std::shared_ptr<const void> Owner, FError& Error);

	private:
		const char* Identity;
		std::shared_ptr<Detail::FContext> Impl;
	};

	std::string PercentEncode(std::string_view Bytes, bool bForm = false);
	bool TryPercentDecode(std::string_view Text, std::string& Out, FError& Error, bool bForm = false);
	bool TryParseQuery(std::string_view Text, std::vector<FQueryParameter>& Out, FError& Error, bool bForm = false);
	std::string BuildQuery(const std::vector<FQueryParameter>& Parameters, bool bForm = false);

	FOperation Resolve(FContext& Context, std::string Host, std::uint16_t Port, FOperationOptions Options = {});

	class FTcpSocket
	{
		friend struct Detail::FAccess;
		friend class FTcpListener;

	public:
		static FOperation Connect(
		    FContext& Context, std::string Host, std::uint16_t Port, FOperationOptions Options = {});
		~FTcpSocket();
		FOperation Read(std::uint64_t MaxBytes, FOperationOptions Options = {});
		FOperation Write(FBytes Bytes, FOperationOptions Options = {});
		void Close();

	private:
		friend struct Backend::FAccess;
		explicit FTcpSocket(std::shared_ptr<Backend::ITcpSocket> InImpl);
		std::shared_ptr<Backend::ITcpSocket> Impl;
	};

	class FTcpListener
	{
	public:
		static std::shared_ptr<FTcpListener> Listen(
		    FContext& Context, std::string Address, std::uint16_t Port, FError& Error, int Backlog = 128);
		~FTcpListener();
		std::uint16_t Port() const noexcept;
		// One pending accept. Cancelling it leaves the listener available for the next call.
		FOperation Accept(FOperationOptions Options = {});
		void Close();

	private:
		friend struct Backend::FAccess;
		explicit FTcpListener(std::shared_ptr<Backend::ITcpListener> Value);
		std::shared_ptr<Backend::ITcpListener> Impl;
	};

	class FUdpSocket
	{
	public:
		static std::shared_ptr<FUdpSocket> Bind(
		    FContext& Context, std::string Address, std::uint16_t Port, FError& Error);
		~FUdpSocket();
		std::uint16_t Port() const noexcept;
		FOperation SendTo(FBytes Bytes, FAddress Peer, FOperationOptions Options = {});
		FOperation Receive(std::uint64_t MaxBytes = 65536, FOperationOptions Options = {});
		FOperation SetBroadcast(bool bEnabled, FOperationOptions Options = {});
		FOperation JoinMulticast(FMulticastGroup Group, FOperationOptions Options = {});
		FOperation LeaveMulticast(FMulticastGroup Group, FOperationOptions Options = {});
		void Close();

	private:
		friend struct Backend::FAccess;
		explicit FUdpSocket(std::shared_ptr<Backend::IUdpSocket> Value);
		FOperation Multicast(FMulticastGroup Group, bool bJoin, FOperationOptions Options);
		std::shared_ptr<Backend::IUdpSocket> Impl;
	};
} // namespace VeroScript::Network
