// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "../Memory/VeroScriptMemoryInternal.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace
{
	// This process uses no workers. Keep the recorder trivial so it remains valid
	// after the registry and all other dynamically initialized objects are destroyed.
	void* TrackedAllocations[64]{};
	std::size_t AllocationsUntilFailure = 0;
	bool bTrackAllocations = false;
	bool bFailureInjected = false;
	bool bChecksPassed = false;

	void Track(void* Memory)
	{
		for (void*& Entry : TrackedAllocations)
		{
			if (!Entry)
			{
				Entry = Memory;
				return;
			}
		}
		std::fputs("FAIL: allocation recorder exhausted\n", stderr);
		std::abort();
	}

	void Untrack(void* Memory) noexcept
	{
		for (void*& Entry : TrackedAllocations)
		{
			if (Entry == Memory)
			{
				Entry = nullptr;
				return;
			}
		}
	}

	void CheckReleasedAllocations()
	{
		std::size_t Retained = 0;
		for (const void* Memory : TrackedAllocations)
		{
			Retained += Memory != nullptr;
		}
		if (Retained)
		{
			std::fprintf(stderr, "FAIL: %zu allocation(s) retained after registry destruction\n", Retained);
			std::_Exit(EXIT_FAILURE);
		}
		if (bChecksPassed)
		{
			std::puts("VeroScriptMemoryAllocationTests passed");
		}
	}
}

void* operator new(const std::size_t Size)
{
	if (AllocationsUntilFailure && --AllocationsUntilFailure == 0)
	{
		bFailureInjected = true;
		throw std::bad_alloc();
	}
	if (void* Memory = std::malloc(Size == 0 ? 1 : Size))
	{
		if (bTrackAllocations)
		{
			Track(Memory);
		}
		return Memory;
	}
	throw std::bad_alloc();
}

void* operator new[](const std::size_t Size)
{
	return ::operator new(Size);
}

void operator delete(void* Memory) noexcept
{
	Untrack(Memory);
	std::free(Memory);
}

void operator delete[](void* Memory) noexcept
{
	::operator delete(Memory);
}

#if defined(__cpp_sized_deallocation)
void operator delete(void* Memory, std::size_t) noexcept
{
	::operator delete(Memory);
}

void operator delete[](void* Memory, std::size_t) noexcept
{
	::operator delete(Memory);
}
#endif

int main(int Argc, char** Argv)
{
	if (Argc != 2 || Argv[1][0] < '1' || Argv[1][0] > '3' || Argv[1][1] != '\0')
	{
		std::fputs("Usage: VeroScriptMemoryAllocationTests <1|2|3>\n", stderr);
		return EXIT_FAILURE;
	}
	// Register before the first MakeValue call: the registry destructor is
	// registered later and runs first (LIFO). Its sentinel, entries, and weak
	// control blocks must be gone before checking for an unowned host object.
	if (std::atexit(CheckReleasedAllocations) != 0)
	{
		return EXIT_FAILURE;
	}

	using namespace VeroScript;
	auto Descriptor = std::make_shared<Memory::Detail::FDescriptor>();
	constexpr std::int32_t Sentinel = 12345;
	FValue Value = Sentinel;
	AllocationsUntilFailure = static_cast<std::size_t>(Argv[1][0] - '0');
	bTrackAllocations = true;
	const auto Failed = Memory::Detail::MakeValue(Descriptor, Value);
	bTrackAllocations = false;
	AllocationsUntilFailure = 0;
	// Object, shared control block, and registry entry require at least three
	// allocations; some standard libraries also allocate a registry sentinel.
	if (!bFailureInjected || Failed.Status != EMemoryStatus::OutOfMemory)
	{
		std::fputs("FAIL: requested first-use allocation did not return OutOfMemory\n", stderr);
		return EXIT_FAILURE;
	}
	const auto* Preserved = std::get_if<std::int32_t>(&Value);
	if (!Preserved || *Preserved != Sentinel)
	{
		std::fputs("FAIL: allocation failure changed the output value\n", stderr);
		return EXIT_FAILURE;
	}
	if (!Memory::Detail::MakeValue(Descriptor, Value) || Memory::Detail::FindDescriptor(Value) != Descriptor)
	{
		std::fputs("FAIL: normal retry did not create the requested memory value\n", stderr);
		return EXIT_FAILURE;
	}
	Value = Sentinel;
	if (Descriptor.use_count() != 1)
	{
		std::fputs("FAIL: releasing the value retained its descriptor\n", stderr);
		return EXIT_FAILURE;
	}
	bChecksPassed = true;
	return EXIT_SUCCESS;
}
