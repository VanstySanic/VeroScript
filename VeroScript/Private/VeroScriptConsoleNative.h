// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptConsole.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
// Windows' unsuffixed file macro conflicts with the bytecode builtin enum.
#ifdef DeleteFile
#undef DeleteFile
#endif
#elif (defined(__unix__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace VeroScript::Console::Detail
{
	inline std::ostream& ResolveOutput(std::ostream& Output)
	{
		auto* Current = &Output;
		for (int Depth = 0; Depth < 8; ++Depth)
		{
			auto* Host = dynamic_cast<IHost*>(Current->rdbuf());
			auto* Backing = Host ? Host->ConsoleOutputStream() : nullptr;
			if (!Backing || Backing == Current)
			{
				break;
			}

			Current = Backing;
		}
		return *Current;
	}
	#if defined(_WIN32)
	inline thread_local WORD NativeDefaultAttributes = 7;
	#endif

	inline bool IsInputTerminal(const std::istream& Input)
	{
		if (&Input != &std::cin) return false;
		#if defined(_WIN32)
		const int Descriptor = _fileno(stdin);
		return Descriptor >= 0 && _isatty(Descriptor) != 0;
		#elif (defined(__unix__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
		return isatty(fileno(stdin)) != 0;
		#else
		return false;
		#endif
	}
	inline FInfo Query(const std::ostream& Output)
	{
		if (auto* Host = dynamic_cast<IHost*>(Output.rdbuf()))
		{
			FInfo Info = Host->QueryConsole();
			Info.Width = Info.Width > 0 ? Info.Width : 0;
			Info.Height = Info.Height > 0 ? Info.Height : 0;
			return Info;
		}
		FInfo Info;
		if (&Output != &std::cout) return Info;
		#if defined(_WIN32)
		CONSOLE_SCREEN_BUFFER_INFO Buffer{};
		const int Descriptor = _fileno(stdout);
		if (Descriptor < 0 || _isatty(Descriptor) == 0) return Info;
		if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &Buffer)) return Info;
		Info.bOutputTerminal = Info.bColor = Info.bCursor = Info.bClear = true;
		Info.Width = Buffer.srWindow.Right - Buffer.srWindow.Left + 1;
		Info.Height = Buffer.srWindow.Bottom - Buffer.srWindow.Top + 1;
		#elif (defined(__unix__) || defined(__APPLE__)) && !defined(__EMSCRIPTEN__)
		Info.bOutputTerminal = isatty(fileno(stdout)) != 0;
		const char* Term = std::getenv("TERM");
		Info.bColor = Info.bCursor = Info.bClear = Info.bOutputTerminal
			&& Term != nullptr && *Term != '\0' && std::string_view(Term) != "dumb";
		winsize Size{};
		if (Info.bOutputTerminal && ioctl(fileno(stdout), TIOCGWINSZ, &Size) == 0)
		{
			Info.Width = Size.ws_col;
			Info.Height = Size.ws_row;
		}
		#endif
		return Info;
	}
	inline bool ExecuteRaw(std::ostream& Output, const EOperation Operation,
		const std::int32_t First = 0, const std::int32_t Second = 0)
	{
		// Finish pending text before changing its style or position.
		Output.flush();
		if (!Output) return false;
		if (auto* Host = dynamic_cast<IHost*>(Output.rdbuf()))
		{
			return Host->FlushConsoleText() && Host->ExecuteConsole(Operation, First, Second);
		}

		if (&Output != &std::cout) return false;
		#if defined(_WIN32)
		const HANDLE Handle = GetStdHandle(STD_OUTPUT_HANDLE);
		CONSOLE_SCREEN_BUFFER_INFO Buffer{};
		if (!GetConsoleScreenBufferInfo(Handle, &Buffer)) return false;
		if (Operation == EOperation::SetColor || Operation == EOperation::ResetStyle)
		{
			// Match ANSI color order while preserving the other attribute bits.
			constexpr WORD Colors[] = {0, 4, 2, 6, 1, 5, 3, 7};
			const auto Bits = [&](int Color, bool Background) -> WORD
			{
				if (Color < 0) return Background ? NativeDefaultAttributes & 0xf0 : NativeDefaultAttributes & 0x0f;
				const WORD Value = Colors[Color % 8] | (Color >= 8 ? 8 : 0);
				return Background ? static_cast<WORD>(Value << 4) : Value;
			};
			const int Foreground = Operation == EOperation::ResetStyle ? -1 : First;
			const int Background = Operation == EOperation::ResetStyle ? -1 : Second;
			const WORD Attribute = static_cast<WORD>((Buffer.wAttributes & ~0xff)
				| Bits(Foreground, false) | Bits(Background, true));
			return SetConsoleTextAttribute(Handle, Attribute) != 0;
		}
		if (Operation == EOperation::SetCursorVisible)
		{
			CONSOLE_CURSOR_INFO Cursor{};
			if (!GetConsoleCursorInfo(Handle, &Cursor)) return false;
			Cursor.bVisible = First != 0;
			return SetConsoleCursorInfo(Handle, &Cursor) != 0;
		}
		if (Operation == EOperation::MoveCursor)
		{
			const COORD Position{static_cast<SHORT>(Buffer.srWindow.Left + First),
				static_cast<SHORT>(Buffer.srWindow.Top + Second)};
			return SetConsoleCursorPosition(Handle, Position) != 0;
		}
		const auto Fill = [&](COORD Position, DWORD Count)
		{
			DWORD Written = 0;
			return FillConsoleOutputCharacterW(Handle, L' ', Count, Position, &Written)
				&& Written == Count && FillConsoleOutputAttribute(Handle,
					Buffer.wAttributes, Count, Position, &Written) && Written == Count;
		};
		if (Operation == EOperation::ClearLine)
		{
			COORD Start = Buffer.dwCursorPosition;
			const SHORT Left = Buffer.srWindow.Left, Right = Buffer.srWindow.Right;
			if (Start.X < Left || Start.X > Right) return false;
			const DWORD Count = First == 0 ? Right - Start.X + 1
				: First == 1			   ? Start.X - Left + 1
										   : Right - Left + 1;
			if (First != 0)
			{
				Start.X = Left;
			}

			return Fill(Start, Count);
		}
		if (Operation == EOperation::Clear)
		{
			for (int Row = Buffer.srWindow.Top; Row <= Buffer.srWindow.Bottom; ++Row)
			{
				if (!Fill({Buffer.srWindow.Left, static_cast<SHORT>(Row)},
						Buffer.srWindow.Right - Buffer.srWindow.Left + 1))
				{
					return false;
				}
			}

			return SetConsoleCursorPosition(Handle, {Buffer.srWindow.Left, Buffer.srWindow.Top}) != 0;
		}
		return false;
		#else
		std::string Sequence;
		switch (Operation)
		{
		case EOperation::Clear:
			{
				Sequence = "\x1b[2J\x1b[H";
				break;
			}
		case EOperation::ClearLine:
			{
				Sequence = "\x1b[" + std::to_string(First) + "K";
				break;
			}
		case EOperation::ResetStyle:
			{
				Sequence = "\x1b[0m";
				break;
			}
		case EOperation::SetColor:
			{

				Sequence = "\x1b[" +
					std::to_string(First < 0 ? 39
							: First < 8		 ? 30 + First
											 : 90 + First - 8) +
					";" +
					std::to_string(Second < 0 ? 49
							: Second < 8	  ? 40 + Second
											  : 100 + Second - 8) +
					"m";
				break;
			}
		case EOperation::MoveCursor:
			{

				Sequence = "\x1b[" + std::to_string(Second + 1) + ";" + std::to_string(First + 1) + "H";
				break;
			}
		case EOperation::SetCursorVisible:
			{
				Sequence = First ? "\x1b[?25h" : "\x1b[?25l";
				break;
			}

		default:
			{
				break;
			}
		}
		Output << Sequence;
		Output.flush();
		return static_cast<bool>(Output);
		#endif
	}

	/** Restore style/visibility at the outer execution boundary even on failure or
	 * cancellation. Nested script/host calls share that boundary on each thread.
	 * POSIX/custom hosts restore default style and visible cursor; Windows restores
	 * the original console attributes and visibility. No history is deleted.
	 */
	class FScopedRestore
	{
	public:
		explicit FScopedRestore(std::ostream& InOutput) : Output(ResolveOutput(InOutput)), Previous(Current)
		{
			Current = this;
		}
		FScopedRestore(const FScopedRestore&) = delete;
		FScopedRestore& operator=(const FScopedRestore&) = delete;
		~FScopedRestore() noexcept
		{
			Current = Previous;
			if (!bStyle && !bCursor) return;
			// Failed text output/query must not block independent terminal cleanup.
			try
			{
				Output.flush();
			}
			catch (...)
			{
			}
			bool RestoreStyle = bStyle, RestoreCursor = bCursor;
			#if defined(_WIN32)
			if (bHaveAttributes && bStyle)
			{
				SetConsoleTextAttribute(Handle, Buffer.wAttributes);
				RestoreStyle = false;
			}
			if (bHaveCursor && bCursor)
			{
				SetConsoleCursorInfo(Handle, &Cursor);
				RestoreCursor = false;
			}
			#endif
			if (!RestoreStyle && !RestoreCursor) return;
			if (auto* Host = dynamic_cast<IHost*>(Output.rdbuf()))
			{
				try
				{
					Host->FlushConsoleText();
				}
				catch (...)
				{
				}
				try
				{
					if (RestoreStyle)
					{
						Host->ExecuteConsole(EOperation::ResetStyle, 0, 0);
					}
				}
				catch (...)
				{
				}
				try
				{
					if (RestoreCursor)
					{
						Host->ExecuteConsole(EOperation::SetCursorVisible, 1, 0);
					}
				}
				catch (...)
				{
				}
				return;
			}
			// Re-probe native output so a new redirection never receives escapes.
			FInfo Info;
			try
			{
				Info = Query(Output);
			}
			catch (...)
			{
				return;
			}
			try
			{
				if (RestoreStyle && Info.bColor)
				{
					ExecuteRaw(Output, EOperation::ResetStyle);
				}
			}
			catch (...)
			{
			}
			try
			{
				if (RestoreCursor && Info.bCursor)
				{
					ExecuteRaw(Output, EOperation::SetCursorVisible, 1);
				}
			}
			catch (...)
			{
			}
		}
		static void Mark(std::ostream& InOutput, EOperation Operation)
		{
			if (Operation != EOperation::SetColor && Operation != EOperation::ResetStyle &&
				Operation != EOperation::SetCursorVisible)
			{
				return;
			}

			std::ostream& Output = ResolveOutput(InOutput);
			FScopedRestore* Owner = nullptr;
			for (auto* Scope = Current; Scope; Scope = Scope->Previous)
			{
				if (&Scope->Output == &Output)
				{
					Owner = Scope;
				}
			}

			if (!Owner) return;
			#if defined(_WIN32)
			if (!Owner->bStyle && !Owner->bCursor && &Output == &std::cout
				&& !dynamic_cast<IHost*>(Output.rdbuf()))
			{
				Owner->Handle = GetStdHandle(STD_OUTPUT_HANDLE);
				Owner->bHaveAttributes = GetConsoleScreenBufferInfo(Owner->Handle, &Owner->Buffer) != 0;
				Owner->bHaveCursor = GetConsoleCursorInfo(Owner->Handle, &Owner->Cursor) != 0;
				if (Owner->bHaveAttributes)
				{
					NativeDefaultAttributes = Owner->Buffer.wAttributes;
				}
			}
			#endif
			Owner->bStyle |= Operation == EOperation::SetColor || Operation == EOperation::ResetStyle;
			Owner->bCursor |= Operation == EOperation::SetCursorVisible;
		}
	private:
		inline static thread_local FScopedRestore* Current = nullptr;
		std::ostream& Output;
		FScopedRestore* Previous;
		bool bStyle = false, bCursor = false;
		#if defined(_WIN32)
		HANDLE Handle = nullptr;
		CONSOLE_SCREEN_BUFFER_INFO Buffer{};
		CONSOLE_CURSOR_INFO Cursor{};
		bool bHaveAttributes = false, bHaveCursor = false;
		#endif
	};
	inline bool Execute(std::ostream& Output, const EOperation Operation,
		const std::int32_t First = 0, const std::int32_t Second = 0)
	{
		if (static_cast<std::uint32_t>(Operation) > static_cast<std::uint32_t>(EOperation::SetCursorVisible))
		{
			throw std::runtime_error("std::console: invalid operation");
		}

		if (Operation == EOperation::SetColor && (First < -1 || First > 15 || Second < -1 || Second > 15))
		{
			throw std::runtime_error("std::console::set_color: invalid color");
		}

		if (Operation == EOperation::ClearLine && (First < 0 || First > 2))
		{
			throw std::runtime_error("std::console::clear_line: invalid mode");
		}

		if (Operation == EOperation::SetCursorVisible && First != 0 && First != 1)
		{
			throw std::runtime_error("std::console::set_cursor_visible expects bool");
		}

		if (Operation == EOperation::MoveCursor &&
			(First < 0 || Second < 0 || First > 32766 || Second > 32766))
		{
			throw std::runtime_error("std::console::move_cursor coordinates must be in 0..32766");
		}

		const FInfo Info = Query(Output);
		const bool bSupported = Operation == EOperation::SetColor || Operation == EOperation::ResetStyle
			? Info.bColor : Operation == EOperation::MoveCursor || Operation == EOperation::SetCursorVisible
			? Info.bCursor : Info.bClear;
		if (!bSupported) return false;
		if (Operation == EOperation::MoveCursor &&
			((Info.Width > 0 && First >= Info.Width) || (Info.Height > 0 && Second >= Info.Height)))
		{
			return false;
		}

		FScopedRestore::Mark(Output, Operation);
		return ExecuteRaw(Output, Operation, First, Second);
	}
}
