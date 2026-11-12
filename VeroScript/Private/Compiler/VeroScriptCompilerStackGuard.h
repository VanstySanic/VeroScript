// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace VeroScript
{
	inline bool HasCompilerTypeStackHeadroom() noexcept
	{
		#if defined(_WIN32) && _WIN32_WINNT >= 0x0602
		// Each dependency retains several type-building frames. MSVC debug
		// frames can exhaust the default stack before the logical depth limit.
		// Keep the same diagnostic/unwind reserve as the VM's native guard.
		ULONG_PTR Low = 0, High = 0;
		GetCurrentThreadStackLimits(&Low, &High);
		const auto Current = reinterpret_cast<std::uintptr_t>(&Low);
		return !Low || Current < Low || Current >= High || Current - Low >= 128u * 1024u;
		#else
		return true;
		#endif
	}
}
