// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

#if defined(_WIN32)
#define VEROSCRIPT_RMLUI_CALL __cdecl
#else
#define VEROSCRIPT_RMLUI_CALL
#endif

#if defined(_WIN32) && !defined(VEROSCRIPT_RMLUI_STATIC_PROVIDER)
#if defined(VEROSCRIPT_RMLUI_MODULE_EXPORTS)
#define VEROSCRIPT_RMLUI_MODULE_API __declspec(dllexport)
#else
#define VEROSCRIPT_RMLUI_MODULE_API __declspec(dllimport)
#endif
#else
#define VEROSCRIPT_RMLUI_MODULE_API
#endif

// Named query for application-linked providers; dynamic modules also expose Core's generic query.
extern "C" VEROSCRIPT_RMLUI_MODULE_API std::uint32_t VEROSCRIPT_RMLUI_CALL
veroscript_rmlui_query(std::uint32_t Version, std::uint32_t DescriptorSize, void* Output);
