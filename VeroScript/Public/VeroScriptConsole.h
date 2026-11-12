// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <iosfwd>
#include <stdexcept>
#include <string_view>

namespace VeroScript::Console
{
	// These values also form the C/managed host callback contract.
	enum class EOperation : std::uint32_t
	{
		Clear = 0, ClearLine, SetColor, ResetStyle, MoveCursor, SetCursorVisible
	};
	struct FInfo
	{
		bool bOutputTerminal = false;
		bool bColor = false;
		bool bCursor = false;
		bool bClear = false;
		std::int32_t Width = 0;
		std::int32_t Height = 0;
	};
	/** Optional interface on an output streambuf. Queries have no side effects.
	 * Execute must complete synchronously and return false on unsupported/failure.
	 * The owner keeps this interface alive throughout execution, including cleanup.
	 * Worker callbacks can be concurrent; a host must synchronize its terminal.
	 */
	class IHost
	{
	public:
		virtual ~IHost() = default;
		virtual FInfo QueryConsole() = 0;
		virtual bool ExecuteConsole(EOperation Operation, std::int32_t First, std::int32_t Second) = 0;
		virtual bool HasConsoleTextOutput() const
		{
			return false;
		}
		virtual bool WriteConsoleText(std::string_view)
		{
			return false;
		}
		virtual bool FlushConsoleText()
		{
			return true;
		}
		// Stream adapters expose their backing stream for execution-boundary
		// restoration. Return nullptr for a callback/UI terminal.
		virtual std::ostream* ConsoleOutputStream()
		{
			return nullptr;
		}
	};
	inline int ColorIndex(const std::string_view Name)
	{
		constexpr std::string_view Names[] = {"black", "red", "green", "yellow",
			"blue", "magenta", "cyan", "white", "bright_black", "bright_red",
			"bright_green", "bright_yellow", "bright_blue", "bright_magenta",
			"bright_cyan", "bright_white"};
		if (Name == "default") return -1;
		for (int Index = 0; Index < 16; ++Index)
		{
			if (Name == Names[Index]) return Index;
		}

		throw std::runtime_error("std::console::set_color: unknown color name");
	}
	inline int LineMode(const std::string_view Name)
	{
		if (Name == "after") return 0;
		if (Name == "before") return 1;
		if (Name == "all") return 2;
		throw std::runtime_error("std::console::clear_line: expected all, before or after");
	}
	// Native terminals are detected conservatively. Callback/UI hosts opt in by
	// implementing IHost on their streambuf. Queries do not change terminal state.
	bool IsInputTerminal(const std::istream& Input);
	FInfo Query(const std::ostream& Output);
	// Parameters are validated before capability probing. False means unsupported,
	// out of the known window, or failed; no escape codes are emitted to plain sinks.
	bool Execute(std::ostream& Output, EOperation Operation, std::int32_t First = 0,
		std::int32_t Second = 0);
}
