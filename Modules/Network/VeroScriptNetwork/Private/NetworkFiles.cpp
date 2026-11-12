// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "NetworkFileInternal.h"
#include "NetworkStream.h"
#include <algorithm>
#include <stdexcept>

namespace VeroScript::Network::Detail
{
	FFileJob::~FFileJob()
	{
		// Open native handles are retired by workers before the pool releases its job.
		if (File == -1 && Prefix == -1 && Progress)
		{
			Progress->bPhysicalComplete = true;
		}
		if (bClaimed && Source)
		{
			Source->Cancel();
		}
	}
	struct FFilePool : std::enable_shared_from_this<FFilePool>
	{
		FFilePool(const std::shared_ptr<FContext>& C, FFileExecutorOptions O)
		    : Context(C), RawContext(C.get()), Options(O), Timer(C->Runtime->CreateTimer())
		{
			if (!Timer)
			{
				throw std::invalid_argument("File executor backend does not provide timers");
			}
			Jobs.reserve(O.MaxPendingFiles);
			Queue.resize(O.MaxPendingFiles);
			Threads.reserve(O.WorkerThreads);
			FError Error;
			Memory = ReserveBytes(C,
			    sizeof(FFilePool) + O.MaxPendingFiles * (sizeof(std::shared_ptr<FFileJob>) + sizeof(FFileJob*)) +
			        O.WorkerThreads * sizeof(std::thread),
			    Error);
			if (!Memory)
			{
				throw std::length_error("File executor metadata budget exceeded");
			}
		}
		~FFilePool()
		{
			Shutdown();
		}
		void Start()
		{
			try
			{
				for (std::uint32_t I = 0; I < Options.WorkerThreads; ++I)
				{
					Threads.emplace_back(
					    [this]
					{
						Worker();
					});
				}
			}
			catch (...)
			{
				Shutdown();
				throw;
			}
		}
		bool Matches(const FFileExecutorOptions& O) const noexcept
		{
			return O.WorkerThreads == Options.WorkerThreads && O.MaxPendingFiles == Options.MaxPendingFiles &&
			    O.BlockBytes == Options.BlockBytes && O.PollInterval == Options.PollInterval;
		}
		void Wake() noexcept
		{
			if (Stopped || WakePosted.exchange(true))
			{
				return;
			}
			try
			{
				// File workers must never run cleanup inline or wait on ControlMutex:
				// context shutdown holds it while joining these workers.
				RawContext->Runtime->Post(
				    [Weak = weak_from_this()]
				{
					if (auto Self = Weak.lock())
					{
						Self->WakePosted = false;
						Self->TickSafe();
					}
				});
			}
			catch (...)
			{
				// Every admitted command already has a bounded fallback timer on the I/O executor.
				WakePosted = false;
			}
		}
		void Worker() noexcept
		{
			for (;;)
			{
				FFileJob* Job;
				{
					std::unique_lock<std::mutex> Lock(Mutex);
					Changed.wait(Lock,
					    [&]
					{
						return StopWorkers || Count != 0;
					});
					if (!Count)
					{
						return;
					}
					Job = Queue[Head];
					Head = (Head + 1) % Queue.size();
					--Count;
					++RunningWorkers;
				}
				ExecuteFileCommand(*Job);
				{
					std::lock_guard<std::mutex> Lock(Mutex);
					--RunningWorkers;
					Job->Ready.store(true, std::memory_order_release);
				}
				Changed.notify_all();
				// No job ownership, stream callbacks or context completion is released on this thread.
				Wake();
			}
		}
		void Enqueue(FFileJob& Job, EFileCommand Command)
		{
			Job.Command = Command;
			Job.Ready = false;
			Job.bInFlight = true;
			{
				std::lock_guard<std::mutex> Lock(Mutex);
				Queue[(Head + Count) % Queue.size()] = &Job;
				++Count;
			}
			Changed.notify_one();
		}
		void Arm()
		{
			if (TimerPending || Jobs.empty() || Stopped)
			{
				return;
			}
			Timer->ExpiresAt(std::chrono::steady_clock::now() + Options.PollInterval);
			Timer->AsyncWait(
			    [Self = shared_from_this()](std::error_code Error)
			{
				Self->TimerPending = false;
				if (!Error)
				{
					Self->TickSafe();
				}
			});
			TimerPending = true;
		}
		void FailJob(FFileJob& J, FError Error)
		{
			if (!Terminal(J.Op->Status))
			{
				auto C = Context.lock();
				if (C)
				{
					C->Finish(J.Op, EOperationState::Failed, std::move(Error));
				}
			}
			J.Cancelled = true;
			if (!J.bInFlight)
			{
				Enqueue(J, EFileCommand::Abort);
			}
		}
		bool Pump(FFileJob& J)
		{
			if (J.Owner->Closed && !Terminal(J.Op->Status))
			{
				Fail(J.Op, EError::Closed, "File executor closed");
				J.Cancelled = true;
			}
			if (J.bInFlight && !J.Ready.load(std::memory_order_acquire))
			{
				return false;
			}
			const bool Ready = J.bInFlight;
			J.bInFlight = false;
			// Cancellation may arrive after the worker published readiness. In that window
			// the completed command still owns open handles and needs an explicit Abort command.
			if (Ready && (J.Command == EFileCommand::Abort || J.NativeError != EError::None || (J.Cancelled && J.File == -1 && J.Prefix == -1)))
			{
				J.Progress->bPhysicalComplete = true;
				if (!Terminal(J.Op->Status))
				{
					auto C = Context.lock();
					FError E = J.SourceError;
					if (!E)
					{
						E.Code = J.NativeError != EError::None ? J.NativeError : EError::Cancelled;
						E.SystemCode = J.SystemCode;
						E.Message = "File operation failed";
					}
					if (C)
					{
						C->Finish(J.Op, EOperationState::Failed, std::move(E));
					}
				}
				return true;
			}
			if (Ready && J.Command == EFileCommand::Finish)
			{
				J.Progress->bPhysicalComplete = true;
				if (!Terminal(J.Op->Status))
				{
					auto& Result = *static_cast<FFileResult*>(J.Success->Data);
					Result.Bytes = J.Progress->Bytes;
					Result.bCommitted = J.Progress->Commit == EFileCommitState::Committed;
					if (auto C = Context.lock())
					{
						C->Finish(J.Op,
						    EOperationState::Succeeded,
						    {},
						    J.Success);
					}
				}
				return true;
			}
			if (J.Owner->Closed || Terminal(J.Op->Status))
			{
				FailJob(J, {EError::Closed, "File executor or operation closed", 0});
				return false;
			}
			if (!J.bWrite)
			{
				if (J.Bytes)
				{
					auto R = J.Output->WriteAll(J.Buffer.data(), static_cast<std::int64_t>(J.Bytes));
					if (R.State == EStreamState::WouldBlock)
					{
						return false;
					}
					if (R.State != EStreamState::Progress || R.Bytes != J.Bytes)
					{
						FailJob(J, R.Error ? std::move(R.Error) : FError{EError::Write, "File reader stopped", 0});
						return false;
					}
					J.Bytes = 0;
				}
				Enqueue(J, J.Remaining ? EFileCommand::Read : EFileCommand::Finish);
				return false;
			}
			if (J.PrefixRemaining)
			{
				Enqueue(J, EFileCommand::Prefix);
				return false;
			}
			const auto Limit = std::min(J.WriteOptions.MaxBytes, J.WriteOptions.ExpectedLength.value_or(UINT64_MAX));
			const auto Left = Limit - std::min(Limit, J.Progress->Bytes.load());
			const auto ReadSize = std::min<std::uint64_t>(J.Buffer.size(), Left ? Left : 1);
			auto R = J.Source->Read(J.Buffer.data(), static_cast<std::int64_t>(ReadSize));
			if (R.State == EStreamState::WouldBlock)
			{
				return false;
			}
			if (R.State == EStreamState::End)
			{
				Enqueue(J, EFileCommand::Finish);
				return false;
			}
			if (R.State != EStreamState::Progress || !R.Bytes || R.Bytes > ReadSize)
			{
				FailJob(J, R.Error ? std::move(R.Error) : FError{EError::Read, "Invalid file source progress", 0});
				return false;
			}
			if (R.Bytes > Left)
			{
				FailJob(J,
				    {J.WriteOptions.ExpectedLength && *J.WriteOptions.ExpectedLength <= J.WriteOptions.MaxBytes
				            ? EError::Protocol
				            : EError::Limit,
				        "File output exceeds its length or byte limit",
				        0});
				return false;
			}
			J.Bytes = static_cast<std::size_t>(R.Bytes);
			Enqueue(J, EFileCommand::Write);
			return false;
		}
		void TickSafe() noexcept
		{
			if (Stopped)
			{
				return;
			}
			auto C = Context.lock();
			if (!C)
			{
				return;
			}
			try
			{
				for (std::size_t I = 0; I < Jobs.size();)
				{
					if (Pump(*Jobs[I]))
					{
						Jobs.erase(Jobs.begin() + I);
						Pending.fetch_sub(1);
					}
					else
					{
						++I;
					}
				}
				Arm();
			}
			catch (const std::bad_alloc&)
			{
				C->Fault(EError::Allocation);
			}
			catch (...)
			{
				C->Fault(EError::Internal);
			}
		}
		void Add(std::shared_ptr<FFileJob> J)
		{
			if (Stopped || J->Owner->Closed || Terminal(J->Op->Status))
			{
				J->Progress->bPhysicalComplete = true;
				Fail(J->Op, EError::Closed, "File executor closed");
				return;
			}
			if (Jobs.size() >= Options.MaxPendingFiles)
			{
				J->Progress->bPhysicalComplete = true;
				Fail(J->Op, EError::QueueFull, "File executor pending limit exceeded");
				return;
			}
			Jobs.push_back(J);
			++Pending;
			// Establish the fallback before a worker is allowed to complete a command.
			Arm();
			Running(J->Op);
			Enqueue(*J, EFileCommand::Open);
		}
		void Shutdown() noexcept
		{
			if (Stopped.exchange(true))
			{
				return;
			}
			try
			{
				Timer->Cancel();
			}
			catch (...)
			{
			}
			for (const auto& J : Jobs)
			{
				J->Cancelled = true;
			}
			{
				std::unique_lock<std::mutex> Lock(Mutex);
				Changed.wait(Lock,
				    [&]
				{
					return Count == 0 && RunningWorkers == 0;
				});
				// Retire handles left by completed commands that the I/O thread has not yet observed.
				for (const auto& J : Jobs)
				{
					J->Command = EFileCommand::Abort;
					Queue[(Head + Count) % Queue.size()] = J.get();
					++Count;
				}
				StopWorkers = true;
			}
			Changed.notify_all();
			for (auto& T : Threads)
			{
				if (T.joinable())
				{
					T.join();
				}
			}
			for (const auto& J : Jobs)
			{
				J->Progress->bPhysicalComplete = true;
				if (J->Source)
				{
					J->Source->Cancel();
				}
				if (J->Output)
				{
					J->Output->Fail({EError::Closed, {}, 0});
				}
			}
			Jobs.clear();
			Pending = 0;
			Memory.reset();
		}
		std::weak_ptr<FContext> Context;
		FContext* RawContext;
		FFileExecutorOptions Options;
		std::shared_ptr<FByteLease> Memory;
		std::shared_ptr<Backend::ITimer> Timer;
		std::vector<std::shared_ptr<FFileJob>> Jobs;
		std::vector<FFileJob*> Queue;
		std::vector<std::thread> Threads;
		std::mutex Mutex;
		std::condition_variable Changed;
		std::size_t Head = 0, Count = 0;
		std::uint32_t RunningWorkers = 0;
		std::atomic<std::uint32_t> Pending{0};
		std::atomic<bool> Stopped{false}, WakePosted{false};
		bool StopWorkers = false, TimerPending = false;
	};
} // namespace VeroScript::Network::Detail

namespace VeroScript::Network
{
	namespace N = Detail;
	namespace
	{
		bool Utf8(std::string_view Text)
		{
			for (std::size_t At = 0; At < Text.size();)
			{
				const auto First = static_cast<unsigned char>(Text[At++]);
				if (First < 0x80)
				{
					continue;
				}
				unsigned Count = 0;
				std::uint32_t Code = 0, Minimum = 0;
				if (First >= 0xC2 && First <= 0xDF)
				{
					Count = 1;
					Code = First & 0x1F;
					Minimum = 0x80;
				}
				else if (First >= 0xE0 && First <= 0xEF)
				{
					Count = 2;
					Code = First & 0x0F;
					Minimum = 0x800;
				}
				else if (First >= 0xF0 && First <= 0xF4)
				{
					Count = 3;
					Code = First & 0x07;
					Minimum = 0x10000;
				}
				else
				{
					return false;
				}
				if (Count > Text.size() - At) return false;
				while (Count--)
				{
					const auto Next = static_cast<unsigned char>(Text[At++]);
					if ((Next & 0xC0) != 0x80) return false;
					Code = (Code << 6) | (Next & 0x3F);
				}
				if (Code < Minimum || Code > 0x10FFFF || (Code >= 0xD800 && Code <= 0xDFFF)) return false;
			}
			return true;
		}
		std::filesystem::path Path(std::string_view Text, std::string_view Base)
		{
			if (Text.empty() || Text.size() > 1024 * 1024 || Base.size() > 1024 * 1024 ||
			    Text.find('\0') != Text.npos || Base.find('\0') != Base.npos || !Utf8(Text) || !Utf8(Base))
			{
				throw std::invalid_argument("Invalid file path");
			}
			auto P = std::filesystem::u8path(Text.begin(), Text.end());
			if (!P.is_absolute())
			{
				auto B = std::filesystem::u8path(Base.begin(), Base.end());
				if (!B.is_absolute())
				{
					throw std::invalid_argument("Relative file paths require an absolute base");
				}
				P = B / P;
			}
			return P.lexically_normal();
		}
	} // namespace
	struct FFileExecutor::FImpl
	{
		std::shared_ptr<N::FContext> Context;
		std::shared_ptr<N::FFilePool> Pool;
		std::shared_ptr<N::FFileOwner> Owner = std::make_shared<N::FFileOwner>();
		bool PrepareResult(const std::shared_ptr<N::FFileJob>& J)
		{
			FFileResult Value{J->Progress->Path, 0, false};
			auto Lease =
			    N::ReserveBytes(Context, sizeof(N::FOwnedResult) + sizeof(Value) + Value.Path.capacity() + 128, J->Op);
			if (!Lease)
			{
				return false;
			}
			// Allocate the retained result before opening a file or claiming a source.
			J->Success = N::CreateTypedResult(Context, std::move(Lease), std::move(Value));
			return true;
		}
		std::shared_ptr<N::FFileJob> Make(const std::shared_ptr<N::FOperation>& Op, std::uint64_t TextBytes)
		{
			auto J = std::make_shared<N::FFileJob>();
			J->Op = Op;
			J->Progress = std::make_shared<N::FFileProgressState>();
			J->Progress->Lifetime = Context;
			J->Owner = Owner;
			const auto Limit = Context->Options.MaxRetainedBytes;
			const auto Fixed = Pool->Options.BlockBytes + sizeof(N::FFileJob) + sizeof(N::FFileProgressState) + 512;
			if (Fixed > Limit || TextBytes > (Limit - Fixed) / 8)
			{
				throw std::length_error("File job metadata exceeds its budget");
			}
			J->Memory = N::ReserveBytes(Context, Fixed + TextBytes * 8, Op);
			if (!J->Memory)
			{
				return {};
			}
			J->Buffer.resize(static_cast<std::size_t>(Pool->Options.BlockBytes));
			return J;
		}
		void Submit(const std::shared_ptr<N::FFileJob>& J)
		{
			N::ObserveTerminal(J->Op,
			    [Weak = std::weak_ptr<N::FFileJob>(J), Output = J->Output, Source = J->Source](
			        auto State, const auto& E)
			{
				if (State != EOperationState::Succeeded)
				{
					if (auto P = Weak.lock())
					{
						P->Cancelled = true;
					}
				}
				if (Source)
				{
					Source->Cancel();
				}
				if (Output)
				{
					if (State == EOperationState::Succeeded)
					{
						Output->Finish();
					}
					else
					{
						FError Error;
						Error.Code = E.Code;
						Error.SystemCode = E.SystemCode;
						try
						{
							Error.Message = E.Message;
						}
						catch (...)
						{
						}
						Output->Fail(std::move(Error));
					}
				}
			});
			if (J->Source)
			{
				N::FStreamAccess::OnReadable(*J->Source,
				    [Weak = std::weak_ptr<N::FFilePool>(Pool)]
				{
					if (auto P = Weak.lock())
					{
						P->Wake();
					}
				});
			}
			if (J->Output)
			{
				N::FStreamAccess::OnWritable(*J->Output,
				    [Weak = std::weak_ptr<N::FFilePool>(Pool)]
				{
					if (auto P = Weak.lock())
					{
						P->Wake();
					}
				});
			}
			Context->Post(
			    [P = Pool, J]
			{
				P->Add(J);
			});
		}
	};
	FFileExecutor::FFileExecutor(FContext& Context, FFileExecutorOptions Options) : Impl(std::make_shared<FImpl>())
	{
		auto C = N::FAccess::Context(Context);
		if (!Options.WorkerThreads || Options.WorkerThreads > 8 || !Options.MaxPendingFiles ||
		    Options.MaxPendingFiles > 4096 || !Options.BlockBytes || Options.BlockBytes > C->Options.MaxBufferBytes ||
		    Options.BlockBytes > 16 * 1024 * 1024 || Options.PollInterval.count() < 1 ||
		    Options.PollInterval > std::chrono::seconds(1))
		{
			throw std::invalid_argument("Invalid file executor limits");
		}
		std::lock_guard<std::mutex> Lock(C->FilePoolMutex);
		if (C->Closed)
		{
			throw std::invalid_argument("File context closed");
		}
		if (!C->FilePool)
		{
			auto P = std::make_shared<N::FFilePool>(C, Options);
			P->Start();
			const auto Id = C->AddResource(
			    [Weak = std::weak_ptr<N::FFilePool>(P)]
			{
				if (auto Pool = Weak.lock())
				{
					Pool->Shutdown();
				}
			});
			if (!Id)
			{
				throw std::length_error("File executor resource limit exceeded");
			}
			C->FilePool = std::move(P);
		}
		else if (!C->FilePool->Matches(Options))
		{
			throw std::invalid_argument("File executor options differ in one context");
		}
		Impl->Context = C;
		Impl->Pool = C->FilePool;
	}
	FFileExecutor::~FFileExecutor()
	{
		Close();
	}
	FFileOperation FFileExecutor::Read(FFileReadOptions Options)
	{
		FFileOperation Result;
		auto P = Impl;
		auto Op = N::Begin(P->Context, Options.Operation);
		Result.Op = N::FAccess::Wrap(Op);
		if (N::Terminal(Op->Status))
		{
			return Result;
		}
		try
		{
			if (P->Owner->Closed || P->Pool->Stopped)
			{
				N::Fail(Op, EError::Closed, "File executor closed");
				return Result;
			}
			auto J = P->Make(Op, Options.Path.size() + Options.BaseDirectory.size());
			if (!J)
			{
				return Result;
			}
			J->Path = Path(Options.Path, Options.BaseDirectory);
			J->Progress->Path = N::FilePathUtf8(J->Path);
			if (!P->PrepareResult(J))
			{
				return Result;
			}
			J->ReadOptions = std::move(Options);
			FBytePipe Pipe;
			FError E;
			auto Memory = N::ReserveBytes(P->Context, P->Pool->Options.BlockBytes, Op);
			if (!Memory || !N::FStreamAccess::CreatePipe(P->Context, P->Pool->Options.BlockBytes, Pipe, E, std::move(Memory)))
			{
				N::Fail(Op, E ? E.Code : EError::Allocation, "Cannot allocate file reader");
				return Result;
			}
			Result.Reader = Pipe.Reader;
			Result.State = J->Progress;
			J->Output = std::move(Pipe.Writer);
			N::FStreamAccess::OnCancel(*Pipe.Reader,
			    [Weak = std::weak_ptr<N::FOperation>(Op)]
			{
				if (auto O = Weak.lock())
				{
					N::FAccess::Wrap(std::move(O)).Cancel();
				}
			});
			P->Submit(J);
		}
		catch (const std::bad_alloc&)
		{
			N::Fail(Op, EError::Allocation, "File read allocation failed");
		}
		catch (const std::invalid_argument&)
		{
			N::Fail(Op, EError::InvalidArgument, "Invalid file read path");
		}
		catch (const std::filesystem::filesystem_error& E)
		{
			N::Fail(Op, EError::InvalidArgument, "Invalid file read path", E.code().value());
		}
		catch (const std::length_error&)
		{
			N::Fail(Op, EError::Limit, "File read metadata limit exceeded");
		}
		return Result;
	}
	FFileOperation FFileExecutor::Write(FFileWriteOptions Options, std::shared_ptr<FReadableStream> Source)
	{
		FFileOperation Result;
		auto P = Impl;
		auto Op = N::Begin(P->Context, Options.Operation);
		Result.Op = N::FAccess::Wrap(Op);
		if (N::Terminal(Op->Status))
		{
			return Result;
		}
		try
		{
			if (P->Owner->Closed || P->Pool->Stopped)
			{
				N::Fail(Op, EError::Closed, "File executor closed");
				return Result;
			}
			if (!Source || (Options.PrefixLength && Options.PrefixPath.empty()) ||
			    Options.PrefixLength > Options.MaxBytes ||
			    (Options.ExpectedLength &&
			        (*Options.ExpectedLength < Options.PrefixLength || *Options.ExpectedLength > Options.MaxBytes)))
			{
				N::Fail(Op, EError::InvalidArgument, "Invalid file output source or lengths");
				return Result;
			}
			auto J = P->Make(Op, Options.Path.size() + Options.BaseDirectory.size() + Options.PrefixPath.size());
			if (!J)
			{
				return Result;
			}
			J->bWrite = true;
			J->Path = Path(Options.Path, Options.BaseDirectory);
			if (!Options.PrefixPath.empty())
			{
				J->PrefixPath = Path(Options.PrefixPath, Options.BaseDirectory);
			}
			J->Progress->Path = N::FilePathUtf8(J->Path);
			if (!P->PrepareResult(J))
			{
				return Result;
			}
			if (Options.ExpectedLength)
			{
				J->Progress->Total = *Options.ExpectedLength;
				J->Progress->bHasTotal = true;
			}
			J->WriteOptions = std::move(Options);
			J->Source = std::move(Source);
			if (!N::FStreamAccess::ClaimReader(*J->Source))
			{
				N::Fail(Op, EError::State, "File source has already been consumed or cancelled");
				return Result;
			}
			J->bClaimed = true;
			Result.State = J->Progress;
			P->Submit(J);
		}
		catch (const std::bad_alloc&)
		{
			N::Fail(Op, EError::Allocation, "File write allocation failed");
		}
		catch (const std::invalid_argument&)
		{
			N::Fail(Op, EError::InvalidArgument, "Invalid file output path");
		}
		catch (const std::filesystem::filesystem_error& E)
		{
			N::Fail(Op, EError::InvalidArgument, "Invalid file output path", E.code().value());
		}
		catch (const std::length_error&)
		{
			N::Fail(Op, EError::Limit, "File write metadata limit exceeded");
		}
		return Result;
	}
	void FFileExecutor::Close() noexcept
	{
		if (Impl && !Impl->Owner->Closed.exchange(true))
		{
			Impl->Pool->Wake();
		}
	}
	FFileExecutorStatistics FFileExecutor::Statistics() const noexcept
	{
		auto& P = *Impl->Pool;
		std::lock_guard<std::mutex> Lock(P.Mutex);
		return {P.Pending, P.RunningWorkers, static_cast<std::uint32_t>(P.Threads.size())};
	}
	bool FFileExecutor::BelongsTo(const FContext& Context) const noexcept
	{
		if (std::string_view(Context.GetFoundationIdentity()) != Backend::FoundationIdentity()) return false;
		return Impl->Context == N::FAccess::Context(const_cast<FContext&>(Context));
	}
	const FOperation& FFileOperation::Operation() const noexcept
	{
		return Op;
	}
	std::shared_ptr<FReadableStream> FFileOperation::Body() const noexcept
	{
		return Reader;
	}
	FFileProgress FFileOperation::Progress() const
	{
		FFileProgress R;
		if (!State)
		{
			R.bPhysicalComplete = Op.IsComplete();
			return R;
		}
		std::lock_guard<std::mutex> Lock(State->Mutex);
		R.Path = State->Path;
		R.PartialPath = State->PartialPath;
		R.Bytes = State->Bytes;
		if (State->bHasTotal)
		{
			R.TotalBytes = State->Total;
		}
		R.Commit = State->Commit;
		R.bPhysicalComplete = State->bPhysicalComplete;
		R.CleanupSystemCode = State->CleanupSystemCode;
		return R;
	}
} // namespace VeroScript::Network
