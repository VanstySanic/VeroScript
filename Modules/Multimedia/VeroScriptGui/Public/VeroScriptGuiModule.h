// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

#if defined(_WIN32)
#define VEROSCRIPT_GUI_MODULE_CALL __cdecl
#else
#define VEROSCRIPT_GUI_MODULE_CALL
#endif

#if defined(_WIN32) && !defined(VEROSCRIPT_GUI_STATIC_TOOLS)
#if defined(VEROSCRIPT_GUI_MODULE_EXPORTS)
#define VEROSCRIPT_GUI_MODULE_API __declspec(dllexport)
#else
#define VEROSCRIPT_GUI_MODULE_API __declspec(dllimport)
#endif
#else
#define VEROSCRIPT_GUI_MODULE_API
#endif

extern "C" VEROSCRIPT_GUI_MODULE_API std::uint32_t VEROSCRIPT_GUI_MODULE_CALL
veroscript_gui_tools_query(std::uint32_t Version, std::uint32_t Size, void* Output);
