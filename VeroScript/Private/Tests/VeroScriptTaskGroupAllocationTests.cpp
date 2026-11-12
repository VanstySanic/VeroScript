// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptConcurrent.h"

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <new>

namespace
{
	// Fail only allocations on the owner thread; worker completion stays usable.
	thread_local bool bRejectAllocations = false;
}

void* operator new(const std::size_t Size)
{
	if (!bRejectAllocations)
	{
		if (void* Memory = std::malloc(Size == 0 ? 1 : Size))
		{
			return Memory;
		}
	}
	throw std::bad_alloc();
}

void* operator new[](const std::size_t Size)
{
	return ::operator new(Size);
}

void operator delete(void* Memory) noexcept
{
	std::free(Memory);
}

void operator delete[](void* Memory) noexcept
{
	std::free(Memory);
}

#if defined(__cpp_sized_deallocation)
void operator delete(void* Memory, std::size_t) noexcept
{
	std::free(Memory);
}

void operator delete[](void* Memory, std::size_t) noexcept
{
	std::free(Memory);
}
#endif

int main()
{
	std::atomic_int Completed{0};
	std::int32_t RejectedTask = 0;
	for (const bool bSuccess : {true, false})
	{
		{
			VeroScript::Concurrent::FTaskGroup Group;
			if (Group.Spawn([&Completed, bSuccess](std::string& Error)
			{
				Completed.fetch_add(1, std::memory_order_relaxed);
				if (!bSuccess)
				{
					Error = "worker failed";
				}
				return bSuccess;
			}) < 0)
			{
				std::fputs("FAIL: worker creation\n", stderr);
				return EXIT_FAILURE;
			}
			bRejectAllocations = true;
			RejectedTask = Group.Spawn([](std::shared_ptr<std::atomic_bool>, std::string&)
			{
				return true;
			});
			// Destruction must collect the existing worker with allocation disabled,
			// including its failure path. An allocating noexcept destructor terminates.
		}
		bRejectAllocations = false;
		if (RejectedTask != -1)
		{
			std::fputs("FAIL: allocation failure should reject spawn\n", stderr);
			return EXIT_FAILURE;
		}
	}
	if (Completed.load(std::memory_order_relaxed) != 2)
	{
		std::fputs("FAIL: destruction did not collect every worker\n", stderr);
		return EXIT_FAILURE;
	}
	std::puts("VeroScriptTaskGroupAllocationTests passed");
	return EXIT_SUCCESS;
}
