// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptConcurrent.h"
#include "VeroScriptEngine.h"
#include "VeroScriptLexer.h"
#include "VeroScriptLocale.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"

#include "VeroScriptAst.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace
{
	bool Expect(const bool Condition, const char* Message)
	{
		if (!Condition)
		{
			std::cerr << "FAIL: " << Message << '\n';
		}
		return Condition;
	}

	std::unique_ptr<VeroScript::FBytecodeModule> CompileSource(const std::string& Source)
	{
		VeroScript::VeroScriptLexer Lexer(Source, "concurrent.vs");
		const std::vector<VeroScript::FToken>& Tokens = Lexer.Tokenize();
		if (Lexer.HasError())
		{
			return nullptr;
		}
		VeroScript::VeroScriptParser Parser(Tokens, "concurrent.vs");
		// Low-level concurrency tests intentionally target the implementation
		// intrinsics; user-facing coverage below goes through concurrent.vs.
		Parser.SetAllowInternalBuiltins(true);
		std::unique_ptr<VeroScript::FProgram> Program = Parser.ParseProgram();
		if (!Program || Parser.HasError())
		{
			std::cerr << "parse: " << Parser.GetError().Message << '\n';
			return nullptr;
		}
		VeroScript::VeroScriptCompiler Compiler;
		auto Module = Compiler.Compile(*Program);
		if (!Module || Compiler.HasError())
		{
			std::cerr << "compile: " << Compiler.GetError().Message << '\n';
			return nullptr;
		}
		return Module;
	}
}

int main()
{
	(void)VeroScript::Locale::TrySetLanguageFromTag("en-US");
	{
		auto Channel = std::make_shared<VeroScript::FChannel>();
		VeroScript::Concurrent::FTaskGroup Parents;
		std::promise<void> Entered;
		const auto Task = Parents.Spawn([&](std::string& WorkerError)
		{
			VeroScript::Concurrent::FTaskGroup Children;
			const auto Child = Children.Spawn([Channel](std::string& Error)
			{
				VeroScript::FValue Value;
				return Channel->Recv(Value, Error);
			});
			Entered.set_value();
			bool TimedOut = false;
			return !Children.Join(Child, WorkerError, &TimedOut) && !TimedOut;
		});
		if (Task < 0) return EXIT_FAILURE;
		Entered.get_future().wait();
		Parents.RequestShutdown();
		bool TimedOut = false;
		std::string Error;
		const bool Joined = Parents.Join(Task, Error, &TimedOut, 2000);
		if (TimedOut)
		{
			Channel->Close();
		}
		if (!Expect(Joined && !TimedOut && !Channel->IsClosed(), "owner shutdown releases nested task joins and their child waits")) return EXIT_FAILURE;
	}
	for (const bool Mutex : {false, true})
	{
		std::string Error;
		auto Capability = Mutex ? VeroScript::FChannel::MakeMutex() : VeroScript::FChannel::MakeSemaphore(0, 1, Error);
		auto Owner = std::make_shared<int>(0);
		auto Worker = std::make_shared<int>(0);
		if (Mutex)
		{
			bool Acquired = false;
			if (!Capability->AcquireSynchronization(true, Owner, -1, Acquired, Error, {}) || !Acquired) return EXIT_FAILURE;
		}
		VeroScript::Concurrent::FTaskGroup Group;
		std::promise<void> Entered;
		const auto Task = Group.Spawn([&](std::string& WorkerError)
		{
			Entered.set_value();
			bool Acquired = false;
			return !Capability->AcquireSynchronization(Mutex, Worker, -1, Acquired, WorkerError, {}) && !Acquired;
		});
		if (Task < 0) return EXIT_FAILURE;
		Entered.get_future().wait();
		Group.RequestShutdown();
		bool TimedOut = false;
		const bool Joined = Group.Join(Task, Error, &TimedOut, 2000);
		if (Mutex || TimedOut)
		{
			Capability->ReleaseSynchronization(Mutex, Owner, Error);
		}
		if (!Expect(Joined && !TimedOut && !Capability->IsClosed(), "owner shutdown interrupts blocked synchronization without closing it")) return EXIT_FAILURE;
	}
	for (int Operation = 0; Operation < 4; ++Operation)
	{
		VeroScript::FChannel Channel;
		std::string Error;
		Channel.ConfigureCapacity(1, Error);
		if (Operation >= 2)
		{
			Channel.Send(std::int32_t{1}, Error);
		}
		VeroScript::Concurrent::FTaskGroup Group;
		std::promise<void> Entered;
		const auto Task = Group.Spawn([&](std::string& WorkerError)
		{
			Entered.set_value();
			VeroScript::FValue Value;
			bool TimedOut = false;
			bool Completed = false;
			if (Operation == 0)
			{
				Completed = Channel.Recv(Value, WorkerError);
			}
			if (Operation == 1)
			{
				Completed = Channel.RecvFor(Value, 60000, TimedOut, WorkerError);
			}
			if (Operation == 2)
			{
				Completed = Channel.Send(std::int32_t{2}, WorkerError);
			}
			if (Operation == 3)
			{
				Completed = Channel.SendFor(std::int32_t{2}, 60000, TimedOut, WorkerError);
			}
			return !Completed && !TimedOut;
		});
		if (!Expect(Task >= 0, "shutdown channel test creates worker")) return EXIT_FAILURE;
		Entered.get_future().wait();
		Group.RequestShutdown();
		bool TimedOut = false;
		const bool Joined = Group.Join(Task, Error, &TimedOut, 2000);
		if (TimedOut)
		{
			Channel.Close();
		}
		if (!Expect(Joined && !TimedOut && !Channel.IsClosed(),
				"owner shutdown interrupts channel waits without closing unrelated channels"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		VeroScript::FChannel Channel;
		if (!Expect(!Channel.TryBindTypeNames("channel<int>", "string"),
				"channel rejects inconsistent type metadata")
			|| !Expect(Channel.TryBindTypeNames("channel<int>", "int"),
				"channel binds consistent type metadata")
			|| !Expect(Channel.TryBindTypeNames("channel<int32>", "int32"),
				"channel accepts canonical metadata revalidation"))
		{
			return EXIT_FAILURE;
		}
		std::string BoundType;
		std::string BoundElement;
		Channel.GetTypeNames(BoundType, BoundElement);
		if (!Expect(BoundType == "channel<int>" && BoundElement == "int",
				"channel metadata remains immutable after publication"))
		{
			return EXIT_FAILURE;
		}
		std::string DeepElement = "int";
		for (std::size_t Depth = 0; Depth < 64; ++Depth)
		{
			DeepElement = "channel<" + DeepElement + ">";
		}
		if (!Expect(VeroScript::NativeChannelElementTypeIsValid(DeepElement),
				"channel metadata accepts the documented nesting limit"))
		{
			return EXIT_FAILURE;
		}
		DeepElement = "channel<" + DeepElement + ">";
		if (!Expect(!VeroScript::NativeChannelElementTypeIsValid(DeepElement),
				"channel metadata rejects nesting beyond the limit")
			|| !Expect(
				VeroScript::CanonicalizeNativeElementTypeName(DeepElement).empty(),
				"generic native type canonicalization is depth bounded")
			|| !Expect(
				!VeroScript::NativeElementTypeNamesEqual(DeepElement, DeepElement),
				"invalid over-depth type names never compare equal"))
		{
			return EXIT_FAILURE;
		}
		Channel.SetMutationOrigin({{}, 7});
		VeroScript::FContainerMutationOrigin SameThreadOrigin;
		if (!Expect(Channel.TryGetMutationOriginForCurrentThread(SameThreadOrigin)
				&& SameThreadOrigin.FieldIndex == 7,
				"channel mutation origin remains visible to its owning VM thread"))
		{
			return EXIT_FAILURE;
		}
		const bool bWorkerCanAccessOrigin = std::async(
			std::launch::async,
			[&Channel]()
			{
				Channel.SetMutationOrigin({{}, 9});
				VeroScript::FContainerMutationOrigin ForeignOrigin;
				return Channel.TryGetMutationOriginForCurrentThread(ForeignOrigin);
			}).get();
		if (!Expect(!bWorkerCanAccessOrigin,
				"worker cannot follow a channel origin into another VM object graph"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FContainerMutationOrigin PreservedOrigin;
		if (!Expect(Channel.TryGetMutationOriginForCurrentThread(PreservedOrigin)
				&& PreservedOrigin.FieldIndex == 7,
				"worker cannot steal a channel mutation origin"))
		{
			return EXIT_FAILURE;
		}
		std::string Error;
		if (!Expect(Channel.ConfigureCapacity(1, Error), "configure bounded channel")
			|| !Expect(Channel.Capacity() == 1, "bounded channel reports capacity")
			|| !Expect(!Channel.ConfigureCapacity(2, Error), "capacity is construction-only")
			|| !Expect(!Channel.TrySend(std::string{"wrong"}),
				"public channel API enforces its declared element type")
			|| !Expect(Channel.TrySend(std::int32_t{1}), "bounded channel accepts first value")
			|| !Expect(!Channel.TrySend(std::int32_t{2}), "bounded channel rejects full try-send"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FChannel LongChannel;
		if (!Expect(LongChannel.TryBindTypeNames("channel<long>", "long"),
				"channel binds long metadata")
			|| !Expect(LongChannel.TrySend(std::int64_t{42}),
				"channel<long> accepts its int64 runtime representation"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FChannel UnsignedLongChannel;
		if (!Expect(UnsignedLongChannel.TryBindTypeNames(
					"channel<unsigned long>", "unsigned long"),
				"channel binds unsigned long metadata")
			|| !Expect(UnsignedLongChannel.TrySend(std::uint64_t{42}),
				"channel<unsigned long> accepts its uint64 runtime representation"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FValue Received;
		if (!Expect(Channel.TryRecv(Received), "bounded channel non-blocking receive")
			|| !Expect(std::get<std::int32_t>(Received) == 1, "bounded channel preserves value"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		// A shared producer/consumer wait queue can lose progress when notify_one
		// selects another sender after a send. A one-slot channel forces contention.
		VeroScript::FChannel Channel;
		std::string Error;
		if (!Expect(Channel.TryBindTypeNames("channel<int>", "int")
				&& Channel.ConfigureCapacity(1, Error), "contended channel configures"))
		{
			return EXIT_FAILURE;
		}
		std::atomic_int Finished{0};
		std::atomic_int Received{0};
		std::promise<void> Done;
		auto Completion = Done.get_future();
		std::vector<std::thread> Workers;
		constexpr int ItemsPerProducer = 2000;
		for (int Index = 0; Index < 8; ++Index)
		{
			Workers.emplace_back([&, Index]()
			{
				for (int Item = 0; Item < ItemsPerProducer; ++Item)
				{
					std::string LocalError;
					if (Index < 4)
					{
						if (!Channel.Send(std::int32_t{Item}, LocalError))
						{
							break;
						}
					}
					else
					{
						VeroScript::FValue Value;
						if (!Channel.Recv(Value, LocalError))
						{
							break;
						}
						Received.fetch_add(1, std::memory_order_relaxed);
					}
				}
				if (Finished.fetch_add(1) == 7)
				{
					Done.set_value();
				}
			});
		}
		const bool bCompleted = Completion.wait_for(std::chrono::seconds(10))
			== std::future_status::ready;
		// Wake all waiters even on a regression, so the test fails instead of hanging.
		Channel.Close();
		for (auto& Worker : Workers)
		{
			Worker.join();
		}
		if (!Expect(bCompleted && Received.load() == 4 * ItemsPerProducer,
				"multiple bounded-channel senders and receivers make progress"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		// An externally configured task timeout may legitimately reach int64 max.
		// The concurrent primitives must treat it as an effectively unbounded wait,
		// not overflow while converting it to a clock deadline.
		VeroScript::FChannel Channel;
		if (!Expect(Channel.TryBindTypeNames("channel<int>", "int"),
				"channel binds for saturated timeout coverage"))
		{
			return EXIT_FAILURE;
		}
		std::thread Sender([&Channel]()
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			(void)Channel.TrySend(std::int32_t{7});
		});
		VeroScript::FValue Received;
		std::string Error;
		bool bTimedOut = false;
		const bool bReceived = Channel.RecvFor(
			Received,
			std::numeric_limits<std::int64_t>::max(),
			bTimedOut,
			Error);
		Sender.join();
		if (!Expect(bReceived && !bTimedOut && Error.empty(),
				"maximum timeout remains a valid wait")
			|| !Expect(std::get<std::int32_t>(Received) == 7,
				"maximum timeout preserves the received value"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		auto Inner = std::make_shared<VeroScript::FChannel>();
		auto Outer = std::make_shared<VeroScript::FChannel>();
		if (!Expect(Inner->TryBindTypeNames("channel<int>", "int"),
				"inner channel metadata binds")
			|| !Expect(Outer->TryBindTypeNames(
					"channel<channel<int>>", "channel<int>"),
				"outer channel metadata binds")
			|| !Expect(!Outer->TrySend(std::int32_t{1}),
				"nested channel rejects a scalar payload")
			|| !Expect(!Outer->TrySend(Outer),
				"nested channel rejects a malformed self-send without deadlock")
			|| !Expect(Outer->TrySend(Inner),
				"nested channel accepts an exactly typed channel payload"))
		{
			return EXIT_FAILURE;
		}
		VeroScript::FValue Received;
		if (!Expect(Outer->TryRecv(Received), "nested channel receives its payload")
			|| !Expect(std::get<VeroScript::FChannelHandle>(Received) == Inner,
				"nested channel preserves handle identity"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		// Abandon must wake a concurrent indefinite Join immediately. Otherwise a
		// host teardown thread can remain blocked until the abandoned worker exits.
		std::mutex GateMutex;
		std::condition_variable GateCv;
		bool bWorkerStarted = false;
		bool bReleaseWorker = false;
		std::promise<void> WorkerFinishedPromise;
		std::future<void> WorkerFinished = WorkerFinishedPromise.get_future();
		VeroScript::Concurrent::FTaskGroup Group;
		const std::int32_t TaskId = Group.Spawn(
			[&](std::string&)
			{
				std::unique_lock<std::mutex> Lock(GateMutex);
				bWorkerStarted = true;
				GateCv.notify_all();
				GateCv.wait(Lock, [&]()
				{
					return bReleaseWorker;
				});
				Lock.unlock();
				WorkerFinishedPromise.set_value();
				return true;
			});
		if (!Expect(TaskId >= 0, "task group creates a worker"))
		{
			return EXIT_FAILURE;
		}
		{
			std::unique_lock<std::mutex> Lock(GateMutex);
			GateCv.wait(Lock, [&]()
			{
				return bWorkerStarted;
			});
		}
		auto JoinResult = std::async(
			std::launch::async,
			[&]()
			{
				std::string Error;
				bool bTimedOut = false;
				const bool bJoined = Group.Join(TaskId, Error, &bTimedOut, -1);
				return !bJoined && bTimedOut;
			});
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		Group.Abandon(TaskId);
		const bool bJoinWoke =
			JoinResult.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
		{
			std::lock_guard<std::mutex> Lock(GateMutex);
			bReleaseWorker = true;
		}
		GateCv.notify_all();
		const bool bReportedAbandon = JoinResult.get();
		WorkerFinished.wait();
		if (!Expect(bJoinWoke, "abandon wakes a concurrent join")
			|| !Expect(bReportedAbandon, "abandoned join reports timeout"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		VeroScript::Concurrent::FTaskGroup Group;
		const std::int32_t First = Group.Spawn(
			[](std::string&)
			{
				return true;
			});
		std::string Error;
		if (!Expect(First == 0 && Group.Join(First, Error),
				"task generation completes before reset")
			|| !Expect(Group.ResetCompleted(),
				"completed task generation is reclaimed"))
		{
			return EXIT_FAILURE;
		}
		const std::int32_t Next = Group.Spawn(
			[](std::string&)
			{
				return true;
			});
		if (!Expect(Next == 0 && Group.Join(Next, Error),
				"task ids restart in the next execution generation"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		// Exercise publication and generation reclamation against simultaneous
		// waiters. Fast workers can finish before Spawn stores its thread handle.
		VeroScript::Concurrent::FTaskGroup Group;
		std::atomic_bool bSpawnDone{false};
		std::atomic_int Completed{0};
		const auto Collect = [&]()
		{
			while (!bSpawnDone.load(std::memory_order_acquire))
			{
				Group.JoinAll();
				(void)Group.ResetCompleted();
				std::this_thread::yield();
			}
			Group.JoinAll();
		};
		auto FirstWaiter = std::async(std::launch::async, Collect);
		auto SecondWaiter = std::async(std::launch::async, Collect);
		bool bSpawnedAll = true;
		constexpr int WorkerCount = 256;
		for (int Index = 0; Index < WorkerCount; ++Index)
		{
			bSpawnedAll = Group.Spawn([&](std::string&)
			{
				Completed.fetch_add(1, std::memory_order_relaxed);
				return true;
			}) >= 0 &&
				bSpawnedAll;
			std::this_thread::yield();
		}
		bSpawnDone.store(true, std::memory_order_release);
		FirstWaiter.get();
		SecondWaiter.get();
		Group.JoinAll();
		if (!Expect(bSpawnedAll && Completed.load() == WorkerCount,
				"concurrent spawn and collection retain every worker")
			|| !Expect(Group.ResetCompleted(),
				"simultaneous waiters safely reclaim the completed generation"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		// Callback captures are destroyed after MarkFinished. Hold that cleanup
		// open so simultaneous joins must wait for actual worker termination.
		std::mutex ExitMutex;
		std::condition_variable ExitCv;
		bool bExitStarted = false;
		bool bReleaseExit = false;
		bool bSelfJoinRejected = false;
		std::int32_t TaskId = -1;
		VeroScript::Concurrent::FTaskGroup Group;
		auto Cleanup = std::shared_ptr<int>(new int(0), [&](int* Value)
		{
			delete Value;
			std::unique_lock<std::mutex> Lock(ExitMutex);
			bExitStarted = true;
			ExitCv.notify_all();
			ExitCv.wait(Lock, [&]()
			{
				return bReleaseExit;
			});
			Lock.unlock();
			std::string Error;
			bSelfJoinRejected = !Group.Join(TaskId, Error)
					&& Error == VeroScript::Locale::FormatEnglish("diag.cannot_join_running_task");
		});
		std::promise<void> AllowReturn;
		const auto ReturnAllowed = AllowReturn.get_future().share();
		TaskId = Group.Spawn([Cleanup, ReturnAllowed](std::string&)
		{
			ReturnAllowed.wait();
			return true;
		});
		if (TaskId < 0)
		{
			bReleaseExit = true;
			Cleanup.reset();
			return EXIT_FAILURE;
		}
		Cleanup.reset();
		AllowReturn.set_value();
		bool bCleanupEntered = false;
		{
			std::unique_lock<std::mutex> Lock(ExitMutex);
			bCleanupEntered = ExitCv.wait_for(Lock, std::chrono::seconds(5),
				[&]()
				{
					return bExitStarted;
				});
		}
		auto First = std::async(std::launch::async, [&]()
		{
			Group.JoinAll();
		});
		auto Second = std::async(std::launch::async, [&]()
		{
			Group.JoinAll();
		});
		const bool bEarlyJoin = First.wait_for(std::chrono::milliseconds(100))
			== std::future_status::ready
			|| Second.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready;
		const bool bEarlyReset = Group.ResetCompleted();
		{
			std::lock_guard<std::mutex> Lock(ExitMutex);
			bReleaseExit = true;
		}
		ExitCv.notify_all();
		First.get();
		Second.get();
		if (!Expect(bCleanupEntered && !bEarlyJoin && !bEarlyReset,
				"all collectors wait for worker exit before generation reclamation")
			|| !Expect(bSelfJoinRejected && Group.ResetCompleted(),
				"self-join stays rejected during collection and reset succeeds after exit"))
		{
			return EXIT_FAILURE;
		}
	}
	const std::string Source =
		"void worker(channel<int> ch) {\n"
		"    int x = __channel_recv(ch);\n"
		"    __channel_send(ch, x + 1);\n"
		"}\n"
		"\n"
		"void main() {\n"
		"    channel<int> ch = channel<int>();\n"
		"    int t = __spawn(\"worker\", ch);\n"
		"    __channel_send(ch, 41);\n"
		"    __join_task(t);\n"
		"    int y = __channel_recv(ch);\n"
		"    assert(y == 42);\n"
		"    print(y);\n"
		"\n"
		"    channel<string> cs = channel<string>();\n"
		"    __channel_send(cs, \"hi\");\n"
		"    string s = __channel_recv(cs);\n"
		"    assert(s == \"hi\");\n"
		"    __channel_close(cs);\n"
		"    print(s);\n"
		"\n"
		"    channel<int64> ci64 = channel<int64>();\n"
		"    __channel_send(ci64, 9223372036854775807i64);\n"
		"    print(__channel_recv(ci64));\n"
		"    channel<uint64> cu64 = channel<uint64>();\n"
		"    __channel_send(cu64, 18446744073709551615u64);\n"
		"    print(__channel_recv(cu64));\n"
		"}\n";

	std::unique_ptr<VeroScript::FBytecodeModule> Module = CompileSource(Source);
	if (!Expect(static_cast<bool>(Module), "compile channel<T> ping-pong"))
	{
		return EXIT_FAILURE;
	}

	{
		std::ostringstream PingPongOut;
		std::istringstream Input;
		std::ostringstream PingPongErr;
		VeroScript::VeroScriptVm Vm(PingPongOut, Input, PingPongErr);
		VeroScript::Test::Configure(Vm);
		if (!Expect(
				Vm.Execute(*Module, VeroScript::ERunMode::Script),
				"execute channel<T> ping-pong"))
		{
			std::cerr << Vm.GetError().Message << '\n';
			return EXIT_FAILURE;
		}

		if (!Expect(
				PingPongOut.str()
					== "42\nhi\n9223372036854775807\n18446744073709551615\n",
				"expected channel payloads to preserve all scalar widths"))
		{
			std::cerr << "got: " << PingPongOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream StdlibOut;
		std::ostringstream StdlibErr;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"string countWidth(int value) { return \"int\"; }\n"
			"string countWidth(int64 value) { return \"int64\"; }\n"
			"void worker(channel<int> ch) {\n"
			"    int x = ch.recv();\n"
			"    ch.send(x + 1);\n"
			"}\n"
			"void main() {\n"
			"    channel<int> ch = channel<int>();\n"
			"    int t = std::concurrent::spawn(\"worker\", ch);\n"
			"    std::concurrent::send(ch, 41);\n"
			"    std::concurrent::join_task(t);\n"
			"    print(std::concurrent::recv(ch));\n"
			"    channel<string> cs = channel<string>();\n"
			"    cs.send(\"ok\");\n"
			"    print(cs.recv());\n"
			"    cs.close();\n"
			"    channel<int64> wide = channel<int64>();\n"
			"    wide.send(9223372036854775807i64);\n"
			"    print(wide.recv());\n"
			"    print(countWidth(ch.pending()));\n"
			"}\n",
			"<concurrent-stdlib>",
			StdlibOut,
			StdlibErr,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "stdlib concurrent.vs helpers"))
		{
			std::cerr << StdlibErr.str();
			return EXIT_FAILURE;
		}
		if (!Expect(
				StdlibOut.str() == "42\nok\n9223372036854775807\nint64\n",
				"stdlib concurrent namespace and extension output"))
		{
			std::cerr << "got: " << StdlibOut.str() << '\n';
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream TaskOut;
		std::ostringstream TaskErr;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"export void answer(channel<int> output) { std::concurrent::channel_send(output, 42); }\n"
			"void main() {\n"
			"    task<int> work = std::concurrent::spawn_task<int>(\"answer\");\n"
			"    assert(work.valid());\n"
			"    assert(work.await() == 42);\n"
			"    assert(!work.valid());\n"
			"}\n",
			"<typed-task>",
			TaskOut,
			TaskErr,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "task<T> spawn/await lifecycle"))
		{
			std::cerr << TaskErr.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream TaskOut;
		std::ostringstream TaskErr;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"export void no_result(channel<int> output) {}\n"
			"void main() {\n"
			"    task<int> work = std::concurrent::spawn_task<int>(\"no_result\");\n"
			"    int ignored = work.await();\n"
			"}\n",
			"<typed-task-no-result>",
			TaskOut,
			TaskErr,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "task<T> missing result fails without blocking"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream TaskOut;
		std::ostringstream TaskErr;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"export void wait_for_cancel(channel<int> output) {\n"
			"    int ignored = std::concurrent::channel_recv(output);\n"
			"}\n"
			"void main() {\n"
			"    task<int> work = std::concurrent::spawn_task<int>(\"wait_for_cancel\");\n"
			"    work.cancel();\n"
			"    assert(!work.valid());\n"
			"}\n",
			"<typed-task-cancel>",
			TaskOut,
			TaskErr,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "task<T> cooperative cancellation"))
		{
			std::cerr << TaskErr.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"void worker(channel<int> ch) {\n"
			"    exit(7);\n"
			"}\n"
			"void main() {\n"
			"    channel<int> ch = channel<int>();\n"
			"    int t = std::concurrent::spawn(\"worker\", ch);\n"
			"    std::concurrent::join_task(t);\n"
			"}\n",
			"<spawn-exit>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "worker exit should fail join_task"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("exit(7)") != std::string::npos,
				"worker exit diagnostic mentions exit code"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"void worker(channel<int> ch) {\n"
			"    int x = std::concurrent::channel_recv(ch);\n"
			"    std::concurrent::channel_send(ch, x + 1);\n"
			"}\n"
			"void main() {\n"
			"    channel<int> ch = channel<int>();\n"
			"    int t = std::concurrent::spawn(\"worker\", ch);\n"
			"    std::concurrent::channel_send(ch, 1);\n"
			"    std::concurrent::join_task(t);\n"
			"    std::concurrent::join_task(t);\n"
			"    print(std::concurrent::channel_recv(ch));\n"
			"}\n",
			"<join-twice>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess && Output.str() == "2\n", "join_task is idempotent"))
		{
			std::cerr << Errors.str() << Output.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"void worker(channel<int> ch) {\n"
			"    int x = std::concurrent::channel_recv(ch);\n"
			"    std::concurrent::channel_send(ch, x);\n"
			"}\n"
			"void main() {\n"
			"    channel<int> ch = channel<int>();\n"
			"    int t = std::concurrent::spawn(\"worker\", ch);\n"
			"    std::concurrent::channel_send(ch, 9);\n"
			"    // run ends without explicit join; VM must join outstanding tasks\n"
			"}\n",
			"<auto-join>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "unjoined spawn is joined at run end"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"void worker(channel<int> ch) {\n"
			"    std::concurrent::channel_recv(ch);\n"
			"}\n"
			"void main() {\n"
			"    channel<int> ch = channel<int>();\n"
			"    int t = std::concurrent::spawn(\"worker\", ch);\n"
			"}\n",
			"<auto-join-blocked>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "blocked worker unblocked by channel close at run end"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		#if defined(_WIN32)
		_putenv_s("VERO_JOIN_TIMEOUT_MS", "200");
		#else
		setenv("VERO_JOIN_TIMEOUT_MS", "200", 1);
		#endif
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"void worker(channel<int> ch) {\n"
			"    std::concurrent::channel_recv(ch);\n"
			"}\n"
			"void main() {\n"
			"    channel<int> ch = channel<int>();\n"
			"    int t = std::concurrent::spawn(\"worker\", ch);\n"
			"    std::concurrent::join_task(t);\n"
			"}\n",
			"<join-timeout-blocked>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		#if defined(_WIN32)
		_putenv_s("VERO_JOIN_TIMEOUT_MS", "");
		#else
		unsetenv("VERO_JOIN_TIMEOUT_MS");
		#endif
		if (!Expect(!Result.bSuccess, "explicit join_task on blocked worker must fail (not hang)"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("timed out") != std::string::npos
					|| Errors.str().find("超时") != std::string::npos
					|| Errors.str().find("join_task_timed_out") != std::string::npos,
				"join timeout reports timed out diagnostic"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		// Closing the worker channel lets cooperative cleanup finish safely without detach.
	}

	{
		// Environment configuration must be parsed as one complete integer. A
		// partially numeric value used to become a zero-millisecond timeout.
		#if defined(_WIN32)
		_putenv_s("VERO_JOIN_TIMEOUT_MS", "0junk");
		#else
		setenv("VERO_JOIN_TIMEOUT_MS", "0junk", 1);
		#endif
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"void worker(channel<int> ch) { std::this_thread::sleep(20); std::concurrent::channel_send(ch, 1); }\n"
			"void main() {\n"
			"    channel<int> ch = channel<int>();\n"
			"    int t = std::concurrent::spawn(\"worker\", ch);\n"
			"    std::concurrent::join_task(t);\n"
			"    print(std::concurrent::channel_recv(ch));\n"
			"}\n",
			"<join-timeout-invalid-env>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		#if defined(_WIN32)
		_putenv_s("VERO_JOIN_TIMEOUT_MS", "");
		#else
		unsetenv("VERO_JOIN_TIMEOUT_MS");
		#endif
		if (!Expect(Result.bSuccess, "invalid join timeout uses the safe default")
			|| !Expect(Output.str() == "1\n", "invalid join timeout does not truncate"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"void worker(int x) { print(x); }\n"
			"void main() {\n"
			"    channel<int> ch = channel<int>();\n"
			"    int t = std::concurrent::spawn(\"worker\", ch);\n"
			"}\n",
			"<spawn-bad-param>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "spawn rejects non-channel worker parameter"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"void worker(channel<int> ch) {\n"
			"    int parent_task = std::concurrent::channel_recv(ch);\n"
			"    std::concurrent::join_task(parent_task);\n"
			"}\n"
			"void main() {\n"
			"    channel<int> ch = channel<int>();\n"
			"    int t = std::concurrent::spawn(\"worker\", ch);\n"
			"    std::concurrent::channel_send(ch, t);\n"
			"    std::concurrent::join_task(t);\n"
			"}\n",
			"<cross-vm-join>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess, "worker cannot join parent-scoped task id"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(
				Errors.str().find("invalid task id") != std::string::npos,
				"cross-vm join reports invalid task id"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		// A task that ignores its closed result channel must still be owned until it
		// exits. The former detach path let Run return while copied host callbacks
		// and their UserData could already be destroyed.
		std::ostringstream TaskOut;
		std::ostringstream TaskErr;
		const auto StartedAt = std::chrono::steady_clock::now();
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"export void slow_cancel(channel<int> output) {\n"
			"    std::this_thread::sleep(1250);\n"
			"}\n"
			"void main() {\n"
			"    task<int> work = std::concurrent::spawn_task<int>(\"slow_cancel\");\n"
			"    work.cancel();\n"
			"}\n",
			"<typed-task-cancel-lifetime>",
			TaskOut,
			TaskErr,
			VeroScript::ERunMode::Script);
		const auto Elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - StartedAt);
		if (!Expect(Result.bSuccess, "cancel retains an uncooperative worker")
			|| !Expect(Elapsed.count() >= 1150,
				"Run does not outlive a cancelled worker VM"))
		{
			std::cerr << TaskErr.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"void cooperative(channel<int> unused) {\n"
			"    for (int i = 0; i < 5000 && !std::concurrent::cancel_requested(); i++) { std::this_thread::sleep(1); }\n"
			"    assert(std::concurrent::cancel_requested());\n"
			"}\n"
			"void main() {\n"
			"    assert(!std::concurrent::cancel_requested());\n"
			"    channel<int> ch = channel<int>();\n"
			"    int id = std::concurrent::spawn(\"cooperative\", ch);\n"
			"    std::concurrent::cancel_task(id);\n"
			"    std::concurrent::join_task(id);\n"
			"}\n",
			"<cooperative-cancellation>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "per-task cancellation is observable in worker"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"void worker(channel<string> ch) {}\n"
			"void main() {\n"
			"    channel<int> ch = channel<int>();\n"
			"    int t = std::concurrent::spawn(\"worker\", ch);\n"
			"}\n",
			"<spawn-channel-type-mismatch>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess,
				"spawn rejects a channel whose element type differs from the target"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
		if (!Expect(Errors.str().find("channel<string>") != std::string::npos
				&& Errors.str().find("channel<int>") != std::string::npos,
				"spawn channel mismatch reports expected and actual types"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"export void slow_result(channel<int> output) {\n"
			"    std::this_thread::sleep(20);\n"
			"    output.send(42);\n"
			"}\n"
			"export void first_result(channel<int> output) { output.send(1); }\n"
			"export void second_result(channel<int> output) { output.send(2); }\n"
			"void main() {\n"
			"    channel<int> ch = std::concurrent::make_channel<int>(1);\n"
			"    assert(ch.capacity() == 1);\n"
			"    assert(ch.pending() == 0);\n"
			"    assert(ch.try_send(7));\n"
			"    assert(!ch.try_send(8));\n"
			"    assert(ch.pending() == 1);\n"
			"    optional<int> received = ch.try_recv();\n"
			"    assert(received.has_value() && received.get() == 7);\n"
			"    optional<int> empty = ch.try_recv();\n"
			"    assert(!empty.has_value());\n"
			"    assert(!ch.recv_for(0).has_value());\n"
			"    ch.close();\n"
			"    assert(ch.is_closed());\n"
			"    assert(!ch.try_send(9));\n"
			"    task<int> delayed = std::concurrent::spawn_task<int>(\"slow_result\");\n"
			"    assert(!delayed.await_for(0).has_value());\n"
			"    assert(delayed.valid());\n"
			"    assert(delayed.await() == 42);\n"
			"    std::concurrent::task_group<int> group = std::concurrent::task_group<int>();\n"
			"    group.add(std::concurrent::spawn_task<int>(\"first_result\"));\n"
			"    group.add(std::concurrent::spawn_task<int>(\"second_result\"));\n"
			"    array<int> all = group.await_all();\n"
			"    assert(len(all) == 2 && all[0] == 1 && all[1] == 2);\n"
			"}\n",
			"<structured-concurrency>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess, "bounded channels and structured task helpers"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"export void increment(channel<channel<int>> setup) {\n"
			"    channel<int> cell = setup.recv();\n"
			"    for (int i = 0; i < 250; i++) {\n"
			"        std::concurrent::synchronized_fetch_add(cell, 1);\n"
			"    }\n"
			"}\n"
			"export void lock_once(channel<channel<bool>> setup) {\n"
			"    std::concurrent::mutex gate = setup.recv();\n"
			"    std::concurrent::mutex_lock(gate);\n"
			"    std::concurrent::mutex_unlock(gate);\n"
			"    setup.send(gate);\n"
			"}\n"
			"void main() {\n"
			"    channel<channel<int>> cells = channel<channel<int>>();\n"
			"    channel<int> cell = std::concurrent::make_synchronized<int>(0);\n"
			"    int a = std::concurrent::spawn(\"increment\", cells);\n"
			"    int b = std::concurrent::spawn(\"increment\", cells);\n"
			"    int c = std::concurrent::spawn(\"increment\", cells);\n"
			"    int d = std::concurrent::spawn(\"increment\", cells);\n"
			"    cells.send(cell);\n"
			"    cells.send(cell);\n"
			"    cells.send(cell);\n"
			"    cells.send(cell);\n"
			"    std::concurrent::join_task(a);\n"
			"    std::concurrent::join_task(b);\n"
			"    std::concurrent::join_task(c);\n"
			"    std::concurrent::join_task(d);\n"
			"    assert(std::concurrent::synchronized_load(cell) == 1000);\n"
			"    assert(std::concurrent::synchronized_exchange(cell, 5) == 1000);\n"
			"    std::concurrent::synchronized_store(cell, 9);\n"
			"    assert(std::concurrent::synchronized_load(cell) == 9);\n"
			"    std::concurrent::counting_semaphore permits =\n"
			"        std::concurrent::make_semaphore(1, 2);\n"
			"    assert(std::concurrent::semaphore_acquire_for(permits, 0));\n"
			"    assert(!std::concurrent::semaphore_acquire_for(permits, 0));\n"
			"    std::concurrent::semaphore_release(permits);\n"
			"    std::concurrent::semaphore_release(permits);\n"
			"    assert(permits.pending() == 2);\n"
			"    channel<channel<bool>> gates = channel<channel<bool>>();\n"
			"    std::concurrent::mutex gate = std::concurrent::make_mutex();\n"
			"    std::concurrent::mutex_lock(gate);\n"
			"    assert(!std::concurrent::mutex_lock_for(gate, 0));\n"
			"    int worker = std::concurrent::spawn(\"lock_once\", gates);\n"
			"    gates.send(gate);\n"
			"    assert(!std::concurrent::mutex_try_lock(gate));\n"
			"    std::concurrent::mutex_unlock(gate);\n"
			"    std::concurrent::join_task(worker);\n"
			"    std::concurrent::mutex returned = gates.recv();\n"
			"    assert(returned == gate);\n"
			"    assert(std::concurrent::mutex_lock_for(returned, 0));\n"
			"    std::concurrent::mutex_unlock(returned);\n"
			"}\n",
			"<synchronization-capabilities>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess,
				"nested channel synchronization capabilities"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"void main() {\n"
			"    std::concurrent::counting_semaphore permits =\n"
			"        std::concurrent::make_semaphore(1, 1);\n"
			"    std::concurrent::semaphore_release(permits);\n"
			"}\n",
			"<semaphore-over-release>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess,
				"counting semaphore rejects release beyond maximum"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"void main() { channel<array<int>> unsafe = channel<array<int>>(); }\n",
			"<unsafe-shared-container>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess,
				"ordinary mutable containers remain forbidden channel payloads"))
		{
			return EXIT_FAILURE;
		}
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"int twice(int x) { return x * 2; }\n"
			"int overloaded(int x) { return x + 1; }\n"
			"int overloaded(string s) { return len(s); }\n"
			"int combine(int x, string s) { return x + len(s); }\n"
			"class Ops { public: static int plus(int x) { return x + 1; } };\n"
			"void notify(channel<int> output, int x) { output.send(x); }\n"
			"void main() {\n"
			"    int factor = 3;\n"
			"    task<int> a = std::concurrent::spawn_task<int>(twice, 21);\n"
			"    task<int> b = std::concurrent::spawn_task<int>("
			"[factor](int x) -> int => x * factor, 7);\n"
			"    task<int> c = std::concurrent::spawn_task<int>(combine, 4, \"abc\");\n"
			"    task<int> d = std::concurrent::spawn_task<int>(Ops::plus, 40);\n"
			"    task<int> e = std::concurrent::spawn_task<int>(overloaded, 41);\n"
			"    fn(int)->int stored = twice;\n"
			"    task<int> f = std::concurrent::spawn_task<int>(stored, 21);\n"
			"    channel<int> output = std::concurrent::make_channel<int>(1);\n"
			"    int worker = std::concurrent::spawn(notify, output, 9);\n"
			"    assert(a.await() == 42 && b.await() == 21);\n"
			"    assert(c.await() == 7 && d.await() == 41 && e.await() == 42\n"
			"        && f.await() == 42\n"
			"        && output.recv() == 9);\n"
			"    print(\"callable ok\");\n"
			"    std::concurrent::join_task(worker);\n"
			"}\n",
			"<callable-tasks>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess && Output.str() == "callable ok\n",
			"callable tasks run free functions, static methods and captured lambdas"))
		{
			std::cerr << Errors.str() << Output.str();
			return EXIT_FAILURE;
		}
	}
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"channel<int> pass(channel<int> input) { return input; }\n"
			"void main() {\n"
			"    channel<int> shared = std::concurrent::make_channel<int>(1);\n"
			"    task<channel<int>> work = std::concurrent::spawn_task<channel<int>>(pass, shared);\n"
			"    channel<int> received = work.await();\n"
			"    received.send(17);\n"
			"    print(shared.recv());\n"
			"}\n",
			"<callable-nested-channel>",
			Output, Errors, VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess && Output.str() == "17\n",
			"callable tasks transfer nested channel handles as arguments and results"))
		{
			std::cerr << Errors.str() << Output.str();
			return EXIT_FAILURE;
		}
	}
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"void main() {\n"
			"    int x = 1;\n"
			"    task<int> task = std::concurrent::spawn_task<int>("
			"[&x](int y) -> int => x + y, 2);\n"
			"}\n",
			"<callable-ref-capture>",
			Output,
			Errors,
			VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess,
			"callable tasks reject reference captures before starting a worker"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"int twice(int x) { return x * 2; }\n"
			"void main() {\n"
			"    task<string> invalid = std::concurrent::spawn_task<string>(twice, 21);\n"
			"}\n",
			"<callable-result-mismatch>",
			Output, Errors, VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess,
				"callable result type must match the explicit task type"))
		{
			return EXIT_FAILURE;
		}
	}
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"int cooperate() {\n"
			"    for (int i = 0; i < 5000 && !std::concurrent::cancel_requested(); i++) {\n"
			"        std::this_thread::sleep(1);\n"
			"    }\n"
			"    assert(std::concurrent::cancel_requested());\n"
			"    return 7;\n"
			"}\n"
			"void main() {\n"
			"    task<int> work = std::concurrent::spawn_task<int>(cooperate);\n"
			"    work.cancel();\n"
			"    assert(!work.valid());\n"
			"}\n",
			"<callable-cooperative-cancellation>",
			Output, Errors, VeroScript::ERunMode::Script);
		if (!Expect(Result.bSuccess,
				"callable task receives cooperative cancellation before or after start"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}
	{
		std::ostringstream Output;
		std::ostringstream Errors;
		const VeroScript::FRunResult Result = VeroScript::Test::FEngine::RunSource(
			"import \"concurrent.vs\";\n"
			"int fail() { assert(false, \"worker failed\"); return 1; }\n"
			"void main() {\n"
			"    task<int> work = std::concurrent::spawn_task<int>(fail);\n"
			"    print(work.await());\n"
			"}\n",
			"<callable-worker-error>",
			Output, Errors, VeroScript::ERunMode::Script);
		if (!Expect(!Result.bSuccess
				&& Errors.str().find("worker failed") != std::string::npos,
				"callable worker failure reaches await without a fabricated result"))
		{
			std::cerr << Errors.str();
			return EXIT_FAILURE;
		}
	}

	std::cout << "VeroScriptConcurrentTests passed\n";
	return EXIT_SUCCESS;
}
