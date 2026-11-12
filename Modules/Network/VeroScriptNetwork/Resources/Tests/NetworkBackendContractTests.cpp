// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNetworkBackend.h"
#include "VeroScriptNetworkFiles.h"
#include "NetworkProvider.h"
#include <deque>
#include <iostream>
#include <stdexcept>

namespace
{
	namespace N = VeroScript::Network;
	namespace D = N::Detail;
	// An executor without vendor headers verifies that the foundation can host a different backend.
	class FRuntime final : public N::Backend::IRuntime
	{
	public:
		const char* GetBackendId() const noexcept override
		{
			return "contract-test";
		}
		void Run() override
		{
			while (PollOne())
			{
			}
		}
		void Stop() noexcept override
		{
			Stopped = true;
		}
		void Restart() override
		{
			Stopped = false;
		}
		std::size_t PollOne() override
		{
			std::function<void()> Function;
			{
				std::lock_guard<std::mutex> Lock(Mutex);
				if (Stopped || Work.empty()) return 0;
				Function = std::move(Work.front());
				Work.pop_front();
			}
			Function();
			return 1;
		}
		void ReleaseWork() noexcept override
		{
		}
		void Post(std::function<void()> Function) override
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			Work.push_back(std::move(Function));
		}
		// Deliberately unsupported: finite operations must fail clearly instead of dereferencing null.
		std::shared_ptr<N::Backend::ITimer> CreateTimer() override
		{
			return {};
		}
		std::shared_ptr<N::Backend::ISocketWatch> WatchSocket(std::uintptr_t, std::error_code& Error) override
		{
			Error = std::make_error_code(std::errc::operation_not_supported);
			return {};
		}
		void* QueryInterface(std::string_view) noexcept override
		{
			return nullptr;
		}

	private:
		std::deque<std::function<void()>> Work;
		std::mutex Mutex;
		std::atomic<bool> Stopped{false};
	};
	class FService final : public N::Backend::IService
	{
	public:
		const char* GetBackendId() const noexcept override
		{
			return "contract-test";
		}
		const char* GetFoundationIdentity() const noexcept override
		{
			return ForeignIdentity ? "incompatible" : N::Backend::FoundationIdentity();
		}
		bool ForeignIdentity = false;
		bool TlsAvailable() const noexcept override
		{
			return false;
		}
		std::shared_ptr<N::Backend::IRuntime> CreateRuntime() override
		{
			return std::make_shared<FRuntime>();
		}
		N::FOperation Resolve(N::FContext& Context, std::string Host, std::uint16_t Port, N::FOperationOptions Options) override
		{
			auto State = D::FAccess::Context(Context);
			auto Op = D::Begin(State, Options);
			if (!D::Terminal(D::State(Op)))
			{
				D::Post(State, [Op, Host = std::move(Host), Port]
				{
					if (!D::Terminal(D::State(Op)))
					{
						D::Complete(Op, std::vector<N::FAddress>{{Host, Port, false}});
					}
				});
			}
			return D::FAccess::Wrap(Op);
		}
		N::FOperation ConnectTcp(N::FContext& Context, std::string, std::uint16_t, N::FOperationOptions Options) override
		{
			auto Op = D::Begin(D::FAccess::Context(Context), Options);
			D::Fail(Op, N::EError::Unsupported, "No TCP transport in the contract test");
			return D::FAccess::Wrap(Op);
		}
		std::shared_ptr<N::FTcpListener> ListenTcp(N::FContext&, std::string, std::uint16_t, N::FError& Error, int) override
		{
			Error.Code = N::EError::Unsupported;
			return {};
		}
		std::shared_ptr<N::FUdpSocket> BindUdp(N::FContext&, std::string, std::uint16_t, N::FError& Error) override
		{
			Error.Code = N::EError::Unsupported;
			return {};
		}
	};
}

int main()
{
	try
	{
		bool Rejected = false;
		try
		{
			N::FContext Missing(nullptr);
		}
		catch (const std::invalid_argument&)
		{
			Rejected = true;
		}
		if (!Rejected) return 1;
		N::FContextOptions Options;
		Options.Mode = N::EDriveMode::HostDriven;
		auto Foreign = std::make_shared<FService>();
		Foreign->ForeignIdentity = true;
		Rejected = false;
		try
		{
			N::FContext Mismatched(Foreign, Options);
		}
		catch (const std::invalid_argument&)
		{
			Rejected = true;
		}
		if (!Rejected) return 12;
		auto Service = std::make_shared<FService>();
		std::weak_ptr<FService> Probe = Service;
		N::FOperation Completed;
		{
			N::FContext Context(Service, Options);
			Service.reset();
			if (std::string_view(Context.GetBackendId()) != "contract-test") return 2;
			Rejected = false;
			try
			{
				N::FFileExecutor Files(Context);
			}
			catch (const std::invalid_argument&)
			{
				Rejected = true;
			}
			if (!Rejected) return 12;
			if (N::Resolve(Context, "finite", 80).Error().Code != N::EError::Unsupported) return 3;
			N::FOperationOptions Infinite;
			Infinite.Timeout = std::nullopt;
			Completed = N::Resolve(Context, "opaque-host", 42, Infinite);
			if (Completed.IsComplete()) return 4;
			unsigned Callbacks = 0;
			N::FError Error;
			if (!Completed.OnComplete([&](const auto&)
			{
				++Callbacks;
			}, Error))
			{
				return 5;
			}
			Context.PollIo(Error);
			auto Addresses = Completed.Result<std::vector<N::FAddress>>();
			if (Error || Callbacks || !Addresses || Addresses->at(0).Address != "opaque-host") return 6;
			Context.Dispatch(Error);
			if (Error || Callbacks != 1) return 7;
			auto Pending = N::Resolve(Context, "shutdown", 42, Infinite);
			Context.Shutdown();
			if (!Pending.IsComplete() || Pending.Error().Code != N::EError::Cancelled) return 8;
		}
		if (Probe.expired()) return 9;
		Completed = {};
		if (!Probe.expired()) return 10;
		std::cout << "Independent backend contract passed\n";
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 11;
	}
}
