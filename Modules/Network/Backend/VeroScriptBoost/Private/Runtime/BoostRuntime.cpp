// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "NetworkAsio.h"
#include "NetworkNativeStream.h"
#include "VeroScriptNetworkBackend.h"
#include <limits>

namespace VeroScript::Boost
{
	namespace N = Network::Detail;
	namespace B = Network::Backend;
	namespace
	{
		std::error_code Convert(N::FSystemError Error)
		{
			return Error ? std::error_code(Error.value(), std::system_category()) : std::error_code{};
		}
		class FTimer final : public B::ITimer
		{
		public:
			explicit FTimer(N::Asio::io_context& Io) : Timer(Io)
			{
			}
			void Cancel() noexcept override
			{
				try
				{
					Timer.cancel();
				}
				catch (...)
				{
				}
			}
			void ExpiresAt(std::chrono::steady_clock::time_point Deadline) override
			{
				Timer.expires_at(Deadline);
			}
			void AsyncWait(B::FCompletion Completion) override
			{
				Timer.async_wait([Completion = std::move(Completion)](N::FSystemError Error)
				{
					Completion(Convert(Error));
				});
			}

		private:
			N::Asio::steady_timer Timer;
		};
		class FSocketWatch final : public B::ISocketWatch
		{
		public:
			FSocketWatch(N::Asio::io_context& Io, std::uintptr_t Handle, std::error_code& Error) : Socket(Io)
			{
				using FHandle = N::FTcp::socket::native_handle_type;
				#if defined(_WIN32)
				using FRawHandle = SOCKET;
				#else
				using FRawHandle = int;
				#endif
				if (Handle > static_cast<std::uintptr_t>(std::numeric_limits<FRawHandle>::max()))
				{
					Error = std::make_error_code(std::errc::invalid_argument);
					return;
				}
				N::FSystemError Native;
				Socket.assign(N::FTcp::v4(), FHandle(static_cast<FRawHandle>(Handle)), Native);
				Error = Convert(Native);
			}
			~FSocketWatch() override
			{
				Release();
			}
			void Cancel() noexcept override
			{
				N::FSystemError Ignored;
				Socket.cancel(Ignored);
			}
			void Release() noexcept override
			{
				N::FSystemError Ignored;
				if (Socket.is_open())
				{
					Socket.release(Ignored);
				}
			}
			void AsyncWait(bool Read, B::FCompletion Completion) override
			{
				Socket.async_wait(Read ? N::FTcp::socket::wait_read : N::FTcp::socket::wait_write,
				    [Completion = std::move(Completion)](N::FSystemError Error)
				{
					Completion(Convert(Error));
				});
			}

		private:
			N::FTcp::socket Socket;
		};
		class FRuntime final : public B::IRuntime
		{
		public:
			FRuntime() : Work(N::Asio::make_work_guard(Io))
			{
			}
			const char* GetBackendId() const noexcept override
			{
				return "boost";
			}
			void Run() override
			{
				Io.run();
			}
			void Stop() noexcept override
			{
				Io.stop();
			}
			void Restart() override
			{
				Io.restart();
			}
			std::size_t PollOne() override
			{
				return Io.poll_one();
			}
			void ReleaseWork() noexcept override
			{
				Work.reset();
			}
			void Post(std::function<void()> Function) override
			{
				N::Asio::post(Io, std::move(Function));
			}
			std::shared_ptr<B::ITimer> CreateTimer() override
			{
				return std::make_shared<FTimer>(Io);
			}
			std::shared_ptr<B::ISocketWatch> WatchSocket(std::uintptr_t Handle, std::error_code& Error) override
			{
				auto Watch = std::make_shared<FSocketWatch>(Io, Handle, Error);
				return Error ? nullptr : Watch;
			}
			void* QueryInterface(std::string_view Contract) noexcept override
			{
				return Contract == "veroscript.boost.asio-context/1" ? &Io : nullptr;
			}

		private:
			N::Asio::io_context Io{1};
			N::Asio::executor_work_guard<N::Asio::io_context::executor_type> Work;
		};
	}
	VEROSCRIPT_NETWORK_RUNTIME_API std::shared_ptr<B::IRuntime> CreateRuntime()
	{
		return std::make_shared<FRuntime>();
	}
} // namespace VeroScript::Boost
