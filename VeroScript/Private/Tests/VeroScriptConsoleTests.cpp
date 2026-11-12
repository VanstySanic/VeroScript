// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestVm.h"
#include "VeroScriptCompiler.h"
#include "VeroScriptConsoleNative.h"
#include "VeroScriptLexer.h"
#include "VeroScriptParser.h"
#include "VeroScriptVm.h"

#include <sstream>
#include <tuple>
#include <vector>

using namespace VeroScript;
static_assert(static_cast<unsigned>(EBuiltinId::ConsoleSupportsColor)
	== static_cast<unsigned>(EBuiltinId::MemoryWrite) + 1, "append builtin IDs");
namespace
{
	class FTerminalBuffer final : public std::stringbuf, public Console::IHost
	{
	public:
		//~ Begin std::stringbuf Interface
		Console::FInfo QueryConsole() override
		{
			if (FailQuery && ++Queries > 2)
			{
				throw std::runtime_error("terminal query failed");
			}

			return Info;
		}

		bool ExecuteConsole(Console::EOperation Operation, int First, int Second) override
		{
			Commands.emplace_back(Operation, First, Second);
			if (FailReset && Operation == Console::EOperation::ResetStyle)
			{
				throw std::runtime_error("reset failed");
			}

			return true;
		}
		//~ End std::stringbuf Interface

		Console::FInfo Info{true, true, true, true, 80, 25};

		std::vector<std::tuple<Console::EOperation, int, int>> Commands;

		int Queries = 0;

		bool FailQuery = false, FailReset = false;
	};
	std::unique_ptr<FBytecodeModule> Compile(std::string Source)
	{
		VeroScriptLexer Lexer(Source, "console.vs");
		VeroScriptParser Parser(Lexer.Tokenize(), "console.vs");
		auto Program = Parser.ParseProgram();
		if (!Program) return {};
		VeroScriptCompiler Compiler;
		return Compiler.Compile(*Program);
	}
	bool Run(std::string Source, FTerminalBuffer& Buffer, bool OutputAllowed = true, std::uint64_t Limit = 0)
	{
		auto Module = Compile(std::move(Source));
		if (!Module) return false;
		std::ostream Output(&Buffer);
		std::istringstream Input;
		std::ostringstream Error;
		VeroScriptVm Vm(Output, Input, Error);
		VeroScript::Test::Configure(Vm);
		FScriptCapabilities Capabilities;
		Capabilities.bAllowConsoleOutput = OutputAllowed;
		Vm.SetCapabilities(Capabilities);
		FExecutionLimits Limits;
		Limits.MaxInstructions = Limit;
		Vm.SetExecutionLimits(Limits);
		return Vm.Execute(*Module, ERunMode::Script);
	}
}
int main()
{
	int Failures = 0;
	const auto Expect = [&](bool Condition, const char* Name)
	{
		if (!Condition)
		{
			std::cerr << "FAIL: " << Name << '\n';
			++Failures;
		}
	};
	FTerminalBuffer Buffer;
	Expect(Run(R"VS(void main() {
        assert(std::console::supports_color());
        assert(std::console::supports_cursor());
        assert(std::console::supports_clear());
        assert(std::console::is_output_terminal());
        assert(!std::console::is_interactive());
        assert(std::console::width() == 80 && std::console::height() == 25);
        assert(std::console::set_color("bright_red", "default"));
        assert(std::console::set_cursor_visible(false));
        assert(std::console::move_cursor(5, 3));
        assert(!std::console::move_cursor(80, 25));
        assert(std::console::clear_line("after"));
        assert(std::console::clear_line("before"));
        assert(std::console::clear_line("all"));
        assert(std::console::clear());
        std::console::write("hello");
        std::console::flush();
    })VS", Buffer), "host capabilities and controls without terminal input");
	Expect(Buffer.str() == "hello", "controls do not emit text to a host");
	Expect(Buffer.Commands.size() == 9, "controls and boundary cleanup");
	if (Buffer.Commands.size() == 9)
	{
		Expect(Buffer.Commands[0] == std::make_tuple(Console::EOperation::SetColor, 9, -1), "ANSI color identity");
		Expect(std::get<0>(Buffer.Commands[7]) == Console::EOperation::ResetStyle, "automatic style reset");
		Expect(Buffer.Commands[8] == std::make_tuple(Console::EOperation::SetCursorVisible, 1, 0), "automatic cursor restore");
	}
	for (const char* Failure : {
		"std::console::set_color(\"invalid\", \"default\");",
		"std::console::clear_line(\"invalid\");",
		"std::console::move_cursor(-1, 0);",
		"std::console::move_cursor(0, 32767);",
		"std::console::move_cursor(18446744073709551615u, 0);"})
	{
		FTerminalBuffer Invalid;
		Expect(!Run(std::string("void main() {") + Failure + "}", Invalid), "invalid values rejected");
		Expect(Invalid.Commands.empty(), "invalid values cause no terminal change");
	}
	for (const char* Failure :
		{"std::console::set_color(1, 2);", "std::console::clear_line(true);",
			"std::console::set_cursor_visible(1);", "std::console::move_cursor(1.5, 2);", "console_width();"})
	{
		Expect(!Compile(std::string("void main() {") + Failure + "}"),
			"invalid types/unqualified intrinsic rejected");
	}

	for (const char* Control : {"std::console::set_color(\"red\", \"default\");",
			 "std::console::reset_style();", "std::console::clear();", "std::console::clear_line(\"all\");",
			 "std::console::move_cursor(1, 2);", "std::console::set_cursor_visible(false);"})
	{
		FTerminalBuffer Denied;
		Expect(!Run(std::string("void main() {") + Control + "}", Denied, false), "output permission covers all controls");
		Expect(Denied.Commands.empty(), "denied controls never call host");
	}
	for (bool Limited : {false, true})
	{
		FTerminalBuffer Failed;
		Expect(!Run(std::string("void main() { std::console::set_color(\"red\", \"default\"); std::console::set_cursor_visible(false);")
			+ (Limited ? "while(true) {}" : "assert(false);") + "}", Failed, true, Limited ? 60 : 0), "runtime failure/limit");
		Expect(Failed.Commands.size() == 4 && std::get<0>(Failed.Commands[2]) == Console::EOperation::ResetStyle
			&& std::get<1>(Failed.Commands[3]) == 1, "cleanup survives runtime failure and instruction exhaustion");
	}
	FTerminalBuffer Plain;
	FTerminalBuffer QueryFailed;
	QueryFailed.FailQuery = QueryFailed.FailReset = true;
	Expect(!Run("void main() { std::console::set_color(\"red\", \"default\"); "
		"std::console::set_cursor_visible(false); std::console::supports_color(); }", QueryFailed), "host query failure reported");
	Expect(QueryFailed.Commands.size() == 4 && std::get<1>(QueryFailed.Commands.back()) == 1,
		"cursor cleanup survives query and style-reset exceptions");
	Plain.Info = {};
	Expect(Run(R"VS(void main() {
        assert(!std::console::supports_color() && !std::console::supports_cursor() && !std::console::supports_clear());
        assert(!std::console::is_output_terminal());
        assert(std::console::width() == 0 && std::console::height() == 0);
        assert(!std::console::set_color("red", "default"));
        assert(!std::console::set_cursor_visible(false));
        assert(!std::console::clear() && !std::console::clear_line("all"));
        assert(!std::console::move_cursor(1, 1) && !std::console::reset_style());
        std::console::write("plain");
    })VS", Plain), "unsupported controls retain plain output");
	Expect(Plain.Commands.empty() && Plain.str() == "plain", "no escape sequences or cleanup on plain streams");
	for (const char* Name :
		{"black", "red", "green", "yellow", "blue", "magenta", "cyan", "white", "bright_black", "bright_red",
			"bright_green", "bright_yellow", "bright_blue", "bright_magenta", "bright_cyan", "bright_white"})
	{
		Expect(Console::ColorIndex(Name) >= 0, "all sixteen color names");
	}

	#if defined(_WIN32)
	// An inactive buffer exercises native controls without touching the user's
	// terminal contents. Restore the process handle before reporting any failures.
	const HANDLE PreviousOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	const bool Allocated = AllocConsole() != 0;
	if (Allocated)
	{
		ShowWindow(GetConsoleWindow(), SW_HIDE);
	}

	const HANDLE Fixture = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CONSOLE_TEXTMODE_BUFFER, nullptr);
	bool NativeOk = Fixture != INVALID_HANDLE_VALUE && SetStdHandle(STD_OUTPUT_HANDLE, Fixture);
	if (NativeOk)
	{
		for (const WORD Original : {WORD{0x2e}, WORD{0x4b}})
		{
			const CONSOLE_CURSOR_INFO Cursor{31, FALSE};
			NativeOk &= SetConsoleTextAttribute(Fixture, Original) != 0;
			NativeOk &= SetConsoleCursorInfo(Fixture, &Cursor) != 0;
			{
				Console::Detail::FScopedRestore Restore(std::cout);
				Console::Detail::FScopedRestore::Mark(std::cout, Console::EOperation::SetColor);
				Console::Detail::FScopedRestore::Mark(std::cout, Console::EOperation::SetCursorVisible);
				NativeOk &= Console::Detail::ExecuteRaw(std::cout, Console::EOperation::SetColor, 9, -1);
				NativeOk &= Console::Detail::ExecuteRaw(std::cout, Console::EOperation::SetCursorVisible, 1);
				CONSOLE_SCREEN_BUFFER_INFO Changed{};
				NativeOk &= GetConsoleScreenBufferInfo(Fixture, &Changed) != 0;
				NativeOk &= Changed.wAttributes == WORD{static_cast<WORD>((Original & ~0xf) | 0xc)};
				NativeOk &= Console::Detail::ExecuteRaw(std::cout, Console::EOperation::MoveCursor, 4, 1);
				NativeOk &= GetConsoleScreenBufferInfo(Fixture, &Changed) != 0;
				const COORD Position = Changed.dwCursorPosition;
				NativeOk &= Position.X == Changed.srWindow.Left + 4 && Position.Y == Changed.srWindow.Top + 1;
				NativeOk &= Console::Detail::ExecuteRaw(std::cout, Console::EOperation::ClearLine, 2);
				CONSOLE_SCREEN_BUFFER_INFO Cleared{};
				NativeOk &= GetConsoleScreenBufferInfo(Fixture, &Cleared) != 0;
				NativeOk &= Cleared.dwCursorPosition.X == Position.X && Cleared.dwCursorPosition.Y == Position.Y;
				NativeOk &= Console::Detail::ExecuteRaw(std::cout, Console::EOperation::Clear);
				NativeOk &= GetConsoleScreenBufferInfo(Fixture, &Cleared) != 0;
				NativeOk &= Cleared.dwCursorPosition.X == Cleared.srWindow.Left && Cleared.dwCursorPosition.Y == Cleared.srWindow.Top;
			}
			CONSOLE_SCREEN_BUFFER_INFO Restored{};
			CONSOLE_CURSOR_INFO RestoredCursor{};
			NativeOk &= GetConsoleScreenBufferInfo(Fixture, &Restored) != 0;
			NativeOk &= GetConsoleCursorInfo(Fixture, &RestoredCursor) != 0;
			NativeOk &= Restored.wAttributes == Original && !RestoredCursor.bVisible && RestoredCursor.dwSize == 31;
		}
	}
	SetStdHandle(STD_OUTPUT_HANDLE, PreviousOutput);
	if (Fixture != INVALID_HANDLE_VALUE)
	{
		CloseHandle(Fixture);
	}

	if (Allocated)
	{
		FreeConsole();
	}

	Expect(NativeOk,
		"Windows native controls preserve cursor position and restore original state on repeat execution");
	#endif
	return Failures == 0 ? 0 : 1;
}
