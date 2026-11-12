// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBoost.h"
#include "NetworkTaskBinding.h"
#include "VeroScriptPath.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <new>

namespace
{
	thread_local bool FailAllocation = false;
	void* Allocate(std::size_t Bytes)
	{
		if (FailAllocation)
		{
			throw std::bad_alloc();
		}
		if (auto* P = std::malloc(Bytes ? Bytes : 1))
		{
			return P;
		}
		throw std::bad_alloc();
	}
	void Check(bool Good, const char* Message)
	{
		if (!Good)
		{
			throw std::runtime_error(Message);
		}
	}
	template <class F>
	void RejectAllocation(F&& Action)
	{
		bool Rejected = false;
		FailAllocation = true;
		try
		{
			Action();
		}
		catch (const std::bad_alloc&)
		{
			Rejected = true;
		}
		catch (...)
		{
			FailAllocation = false;
			throw;
		}
		FailAllocation = false;
		Check(Rejected, "The script wrapper allocation boundary was not exercised");
	}
	void Run(bool Generic)
	{
		using namespace VeroScript;
		namespace B = NetworkBinding;
		Network::FContextOptions Options;
		Options.Mode = Network::EDriveMode::HostDriven;
		auto Context = std::make_shared<Network::FContext>(VeroScript::Boost::CreateNetworkService(), Options);
		auto Payload = std::make_shared<B::FContextValue>();
		Payload->Value = Context;
		auto Owner = std::make_shared<FHostObject>();
		Owner->PayloadOwner = Payload;
		Owner->Payload = Payload.get();
		B::FState State;
		auto CodeOwner = std::make_shared<int>(1);
		State.CodeOwner = CodeOwner;
		Network::FError Error;
		Check(Context->RetainCodeOwner(CodeOwner, Error), "Cannot prepare the binding code owner");
		Network::FFileExecutor Files(*Context);
		FValue Out;
		std::string Message;
		bool Started = false;
		RejectAllocation(
			[&]
			{
				B::StartTask<Network::FFileOperation>(
					State,
					"Network::file_operation",
					Owner,
					[&]
					{
						Started = true;
						return Network::FFileOperation{};
					},
					Out,
					Message);
			});
		Check(!Started, "A native action started before its script wrapper could be allocated");
		Network::FBytePipe Pipe;
		Check(Network::TryCreateBytePipe(16, Pipe, Error), "Cannot prepare the file source");
		Network::FFileWriteOptions W;
		W.Path = PathUtil::ToUtf8(std::filesystem::current_path() / "binding-admission-must-not-create.bin");
		// HostDriven: no worker is submitted until PollIo. Failure must cancel this pending write.
		auto Task = std::make_shared<Network::FFileOperation>(Files.Write(W, Pipe.Reader));
		if (Generic)
		{
			RejectAllocation(
				[&]
				{
					B::Start(
						State,
						"Network::operation",
						Owner,
						[&](auto&)
						{
							Started = true;
							return Task->Operation();
						},
						Out,
						Message);
				});
			Check(Started, "The generic operation admission guard was not exercised");
		}
		else
		{
			RejectAllocation(
				[&]
				{
					B::MakeTask(State, "Network::file_operation", Owner, Task, Out, Message);
				});
		}
		Check(Task->Operation().IsComplete(), "A failed script handle left its native operation running");
		Check(Pipe.Writer->Write("x", 1).State == Network::EStreamState::Failed,
			"A failed script handle stranded its producer");
		Context->Shutdown();
		Check(Task->Progress().bPhysicalComplete && Files.Statistics().PendingFiles == 0,
			"Failed script admission did not retire physical file work");
	}
} // namespace
void* operator new(std::size_t N)
{
	return Allocate(N);
}
void* operator new[](std::size_t N)
{
	return Allocate(N);
}
void operator delete(void* P) noexcept
{
	std::free(P);
}
void operator delete[](void* P) noexcept
{
	std::free(P);
}
void operator delete(void* P, std::size_t) noexcept
{
	std::free(P);
}
void operator delete[](void* P, std::size_t) noexcept
{
	std::free(P);
}

int main()
{
	#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL > 0
	std::cout << "Allocation injection requires the release STL; skipped\n";
	return 77;
	#endif
	try
	{
		Run(false);
		Run(true);
		std::cout << "Script task allocation precedes startup; failed admission cancels native work\n";
		return 0;
	}
	catch (const std::exception& E)
	{
		std::cerr << E.what() << '\n';
		return 1;
	}
}
