// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"

#include "VeroScriptNetworkStream.h"
#include "NetworkStream.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace N = VeroScript::Network;
#define CHECK(X) \
	do \
	{ \
		if (!(X)) \
		{ \
			throw std::runtime_error(#X); \
		} \
	} \
	while (false)

int main()
{
	try
	{
		N::FError Error;
		N::FBytePipe Pipe;
		{
			N::FContextOptions Options;
			Options.MaxBufferBytes = 64;
			Options.MaxRetainedBytes = 64;
			N::FContext Context(VeroScript::Boost::CreateNetworkService(), Options);
			N::FBytePipe First, Second, Extra;
			CHECK(N::TryCreateBytePipe(Context, 32, First, Error));
			CHECK(N::TryCreateBytePipe(Context, 32, Second, Error));
			CHECK(!N::TryCreateBytePipe(Context, 1, Extra, Error) && Error.Code == N::EError::Limit);
			CHECK(Context.MemoryUsage().RetainedBytes == 64 && Context.MemoryUsage().PeakRetainedBytes == 64);
			First.Reader.reset();
			CHECK(Context.MemoryUsage().RetainedBytes == 64);
			First.Writer.reset();
			CHECK(Context.MemoryUsage().RetainedBytes == 32);
			Context.Shutdown();
			CHECK(Context.MemoryUsage().RetainedBytes == 32);
			CHECK(!N::TryCreateBytePipe(Context, 1, Extra, Error) && Error.Code == N::EError::Closed);
			Second = {};
			CHECK(Context.MemoryUsage().RetainedBytes == 0);
		}
		{
			N::FContextOptions Options;
			Options.MaxRetainedBytes = 64;
			N::FContext Context(VeroScript::Boost::CreateNetworkService(), Options);
			std::atomic<bool> Okay{true};
			std::vector<std::thread> Workers;
			for (unsigned I = 0; I < 4; ++I)
			{
				Workers.emplace_back(
				    [&]
				{
					for (unsigned J = 0; J < 500; ++J)
					{
						N::FBytePipe Local;
						N::FError Failure;
						const bool Made = N::TryCreateBytePipe(Context, 32, Local, Failure);
						if (!Made && Failure.Code != N::EError::Limit)
						{
							Okay = false;
						}
						const auto Usage = Context.MemoryUsage();
						if (Usage.RetainedBytes > 64 || Usage.PeakRetainedBytes > 64 ||
						    Usage.PeakRetainedBytes < Usage.RetainedBytes)
						{
							Okay = false;
						}
					}
				});
			}
			for (auto& Worker : Workers)
			{
				Worker.join();
			}
			CHECK(Okay && Context.MemoryUsage().RetainedBytes == 0);
			Context.Shutdown();
		}
		CHECK(N::TryCreateBytePipe(4, Pipe, Error));
		auto Original = Pipe.Reader;
		CHECK(!N::TryCreateBytePipe(0, Pipe, Error) && Pipe.Reader == Original);
		CHECK(!N::TryCreateBytePipe(UINT64_MAX, Pipe, Error));
		char Data[32]{};
		CHECK(Pipe.Reader->Read(Data, 1).State == N::EStreamState::WouldBlock);
		CHECK(Pipe.Reader->Read(nullptr, 0).State == N::EStreamState::Progress);
		CHECK(Pipe.Reader->Read(nullptr, 1).Error.Code == N::EError::InvalidArgument);
		CHECK(Pipe.Reader->Read(Data, -1).Error.Code == N::EError::InvalidArgument);
		CHECK(Pipe.Writer->WriteAll("hello", 5).Error.Code == N::EError::Limit);
		CHECK(Pipe.Writer->WriteAll("abc", 3).Bytes == 3);
		CHECK(Pipe.Writer->WriteAll("de", 2).State == N::EStreamState::WouldBlock);
		CHECK(Pipe.Reader->BufferedBytes() == 3);
		CHECK(Pipe.Reader->Read(Data, 2).Bytes == 2 && std::string(Data, 2) == "ab");
		CHECK(Pipe.Writer->WriteAll("def", 3).Bytes == 3);
		CHECK(Pipe.Writer->Write("g", 1).State == N::EStreamState::WouldBlock);
		Pipe.Writer->Finish();
		CHECK(Pipe.Reader->Read(Data, 32).Bytes == 4 && std::string(Data, 4) == "cdef");
		CHECK(Pipe.Reader->Read(Data, 1).State == N::EStreamState::End);
		CHECK(Pipe.Writer->Write("x", 1).Error.Code == N::EError::Closed);

		Original.reset();
		CHECK(N::TryCreateBytePipe(4, Pipe, Error));
		CHECK(Pipe.Writer->Write("abcdef", 6).Bytes == 4);
		Error.Code = N::EError::Protocol;
		Pipe.Writer->Fail(Error);
		CHECK(Pipe.Reader->Read(Data, 32).Bytes == 4);
		CHECK(Pipe.Reader->Read(Data, 1).Error.Code == N::EError::Protocol);
		Pipe.Writer->Finish();
		CHECK(Pipe.Reader->Read(Data, 1).Error.Code == N::EError::Protocol);

		CHECK(N::TryCreateBytePipe(4, Pipe, Error));
		unsigned Cancelled = 0;
		unsigned ReaderWake = 0, WriterWake = 0;
		N::Detail::FStreamAccess::OnReadable(*Pipe.Reader,
		    [&]
		{
			++ReaderWake;
		});
		N::Detail::FStreamAccess::OnWritable(*Pipe.Writer,
		    [&]
		{
			++WriterWake;
		});
		N::Detail::FStreamAccess::OnCancel(*Pipe.Reader,
		    [&]
		{
			++Cancelled;
		});
		Pipe.Reader->Cancel();
		Pipe.Reader->Cancel();
		CHECK(Cancelled == 1);
		CHECK(ReaderWake == 1 && WriterWake == 1);
		CHECK(Pipe.Writer->Write("x", 1).Error.Code == N::EError::Cancelled);
		CHECK(Pipe.Reader->Read(Data, 1).Error.Code == N::EError::Cancelled);

		CHECK(N::TryCreateBytePipe(4, Pipe, Error));
		Pipe.Writer.reset();
		CHECK(Pipe.Reader->Read(Data, 1).Error.Code == N::EError::Closed);
		CHECK(N::TryCreateBytePipe(4, Pipe, Error));
		N::Detail::FStreamAccess::OnCancel(*Pipe.Reader,
		    [&]
		{
			++Cancelled;
		});
		Pipe.Reader.reset();
		CHECK(Cancelled == 2 && Pipe.Writer->Write("x", 1).State == N::EStreamState::Failed);

		auto Owner = std::make_shared<int>(123);
		std::weak_ptr<int> Weak = Owner;
		CHECK(N::TryCreateBytePipe(4, Pipe, Error, Owner));
		Owner.reset();
		Pipe.Writer->Finish();
		Pipe.Writer.reset();
		CHECK(!Weak.expired());
		Pipe.Reader.reset();
		CHECK(Weak.expired());

		CHECK(N::TryCreateBytePipe(257, Pipe, Error));
		std::atomic<bool> Okay{true};
		constexpr std::size_t Total = 128 * 1024;
		const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		std::thread Producer(
		    [&]
		{
			std::uint8_t Bytes[113];
			std::size_t Sent = 0;
			while (Sent < Total && Okay && std::chrono::steady_clock::now() < Deadline)
			{
				const auto Count = std::min(sizeof(Bytes), Total - Sent);
				for (std::size_t I = 0; I < Count; ++I)
				{
					Bytes[I] = static_cast<std::uint8_t>((Sent + I) % 251);
				}
				const auto R = Pipe.Writer->Write(Bytes, Count);
				if (R.State == N::EStreamState::Failed)
				{
					Okay = false;
					break;
				}
				Sent += static_cast<std::size_t>(R.Bytes);
				if (!R.Bytes)
				{
					std::this_thread::yield();
				}
			}
			if (Sent != Total)
			{
				Okay = false;
			}
			Pipe.Writer->Finish();
		});
		std::size_t Received = 0;
		while (Okay && std::chrono::steady_clock::now() < Deadline)
		{
			std::uint8_t Bytes[97];
			const auto R = Pipe.Reader->Read(Bytes, sizeof(Bytes));
			if (R.State == N::EStreamState::End)
			{
				break;
			}
			if (R.State == N::EStreamState::Failed)
			{
				Okay = false;
				break;
			}
			for (std::size_t I = 0; I < R.Bytes; ++I)
			{
				if (Bytes[I] != static_cast<std::uint8_t>((Received + I) % 251))
				{
					Okay = false;
				}
			}
			Received += static_cast<std::size_t>(R.Bytes);
			if (!R.Bytes)
			{
				std::this_thread::yield();
			}
		}
		Okay = Okay && Received == Total;
		Producer.join();
		CHECK(Okay);
		std::cout << "Byte pipe ownership, limits, atomic writes, cancellation and concurrent backpressure passed\n";
	}
	catch (const std::exception& E)
	{
		std::cerr << E.what() << '\n';
		return 1;
	}
	return 0;
}
