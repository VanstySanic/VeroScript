// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <istream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

namespace VeroScript::Tooling::Tests
{
	class FInputTieCheckingBuffer final : public std::streambuf
	{
	public:
		explicit FInputTieCheckingBuffer(std::string Text) : Text(std::move(Text))
		{
		}
		void Attach(std::istream& Stream)
		{
			Input = &Stream;
		}
		bool ObservedTieDuringRead() const
		{
			return bObservedTie.load();
		}

	protected:
		//~ Begin std::streambuf Interface
		int_type underflow() override
		{
			if (Input && Input->tie())
			{
				bObservedTie.store(true);
			}

			return Position < Text.size() ? traits_type::to_int_type(Text[Position]) : traits_type::eof();
		}
		int_type uflow() override
		{
			const auto Result = underflow();
			if (!traits_type::eq_int_type(Result, traits_type::eof()))
			{
				++Position;
			}

			return Result;
		}
		//~ End std::streambuf Interface

	private:
		std::string Text;
		std::size_t Position = 0;
		std::istream* Input = nullptr;
		std::atomic<bool> bObservedTie{false};
	};

	class FOutputThreadCheckingBuffer final : public std::stringbuf
	{
	public:
		bool FlushedByCaller() const
		{
			return bFlushedByCaller.load();
		}

	protected:
		int sync() override
		{
			if (std::this_thread::get_id() == Caller)
			{
				bFlushedByCaller.store(true);
			}

			return 0;
		}

	private:
		const std::thread::id Caller = std::this_thread::get_id();
		std::atomic<bool> bFlushedByCaller{false};
	};
}
