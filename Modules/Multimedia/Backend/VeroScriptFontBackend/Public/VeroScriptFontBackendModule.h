// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#if defined(_WIN32) && !defined(VEROSCRIPT_FONT_BACKEND_STATIC_PROVIDER)
#if defined(VEROSCRIPT_FONT_BACKEND_MODULE_EXPORTS)
#define VEROSCRIPT_FONT_BACKEND_MODULE_API __declspec(dllexport)
#else
#define VEROSCRIPT_FONT_BACKEND_MODULE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define VEROSCRIPT_FONT_BACKEND_MODULE_API __attribute__((visibility("default")))
#else
#define VEROSCRIPT_FONT_BACKEND_MODULE_API
#endif
#if defined(_WIN32)
#define VEROSCRIPT_FONT_BACKEND_MODULE_CALL __cdecl
#else
#define VEROSCRIPT_FONT_BACKEND_MODULE_CALL
#endif
extern "C" VEROSCRIPT_FONT_BACKEND_MODULE_API std::uint32_t VEROSCRIPT_FONT_BACKEND_MODULE_CALL
veroscript_font_backend_query(std::uint32_t Version, std::uint32_t Size, void *Output);
