// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <thread>

#if defined(__APPLE__)
#include <cerrno>
#include <exception>
#include <functional>
#include <memory>
#include <pthread.h>
#include <system_error>
#include <utility>
#endif

namespace VeroScript::Tooling::Detail
{
	#if defined(__APPLE__)
	// Darwin's 512 KiB worker stack is too small for the bounded recursive
	// parser. Match the 8 MiB main-thread budget without changing language limits.
	class FLspThread
	{
	public:
		template <class F>
		explicit FLspThread(F&& Function)
		{
			auto Task = std::make_unique<std::function<void()>>(std::forward<F>(Function));
			pthread_attr_t Attributes;
			int Error = pthread_attr_init(&Attributes);
			if (Error)
			{
				throw std::system_error(Error, std::generic_category(), "LSP thread attributes");
			}
			Error = pthread_attr_setstacksize(&Attributes, 8 * 1024 * 1024);
			if (!Error)
			{
				Error = pthread_create(&Handle, &Attributes, [](void* Data) noexcept -> void*
				{
					std::unique_ptr<std::function<void()>> Entry(static_cast<std::function<void()>*>(Data));
					(*Entry)();
					return nullptr;
				}, Task.get());
			}
			pthread_attr_destroy(&Attributes);
			if (Error)
			{
				throw std::system_error(Error, std::generic_category(), "LSP thread creation");
			}
			Task.release();
			Joinable = true;
		}
		FLspThread(const FLspThread&) = delete;
		FLspThread& operator=(const FLspThread&) = delete;
		~FLspThread()
		{
			if (Joinable)
			{
				std::terminate();
			}
		}
		void join()
		{
			const int Error = Joinable ? pthread_join(Handle, nullptr) : EINVAL;
			if (Error)
			{
				throw std::system_error(Error, std::generic_category(), "LSP thread join");
			}
			Joinable = false;
		}

	private:
		pthread_t Handle{};
		bool Joinable = false;
	};
	#else
	using FLspThread = std::thread;
	#endif
}
