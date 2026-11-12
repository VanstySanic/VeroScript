// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptSdl3.h"

#if defined(_WIN32)
#define VEROSCRIPT_SDL3_CALL __cdecl
#else
#define VEROSCRIPT_SDL3_CALL
#endif

// Named entry for static/mobile hosts. Dynamic loading also exposes the generic Core query symbol.
#if !defined(VEROSCRIPT_SDL3_NATIVE_EXTENSION) || VEROSCRIPT_SDL3_NATIVE_EXTENSION
extern "C" VEROSCRIPT_SDL3_API std::uint32_t VEROSCRIPT_SDL3_CALL veroscript_sdl3_query(std::uint32_t Version,
																						std::uint32_t DescriptorSize,
																						void* Output);
#endif
