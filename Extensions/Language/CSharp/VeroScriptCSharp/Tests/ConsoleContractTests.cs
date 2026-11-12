// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using VeroScript;

internal static class ConsoleContractTests
{
	internal static void Run()
	{
		CheckControls();
		CheckUnsupported();
		CheckCallbackFailures();
		CheckCleanup();
		CheckPermission();
	}

	private static void CheckControls()
	{
		string? backend = Environment.GetEnvironmentVariable("VEROSCRIPT_TEST_JIT_BACKEND");
		foreach (VeroScriptExecutionBackend policy in Enum.GetValues<VeroScriptExecutionBackend>())
		{
			if (policy == VeroScriptExecutionBackend.Jit && string.IsNullOrEmpty(backend))
			{
				continue;
			}

			var commands = new ConcurrentQueue<(VeroScriptConsoleOperation, int, int)>();
			var writes = new ConcurrentQueue<string>();
			var lines = new ConcurrentQueue<string>();
			using var session = new VeroScriptSession(new() { Print = lines.Enqueue, ConsoleQuery = () => new(true, true, true, true, 80, 25), ConsoleControl = (operation, first, second) =>
			{
				commands.Enqueue((operation, first, second));
				return true;
			}, ConsoleWrite = text =>
			{
				writes.Enqueue(text);
				return true;
			} });
			if (!string.IsNullOrEmpty(backend))
			{
				session.SetJitBackend(backend);
			}

			session.ApplyConfiguration(new() { ExecutionBackend = policy });
			session.SetInput("");
			Require(session.LoadSource("""
                void main() {
                    assert(std::console::is_output_terminal());
                    assert(!std::console::is_interactive());
                    assert(std::console::supports_color() && std::console::supports_cursor()
                        && std::console::supports_clear());
                    assert(std::console::width() == 80 && std::console::height() == 25);
                    assert(std::console::set_color("bright_red", "default"));
                    assert(std::console::set_cursor_visible(false));
                    assert(std::console::move_cursor(5, 3));
                    assert(!std::console::move_cursor(80, 25));
                    assert(std::console::clear_line("after"));
                    assert(std::console::clear_line("before"));
                    assert(std::console::clear_line("all"));
                    assert(std::console::clear());
                    assert(std::console::reset_style());
                    std::console::write("你好\u0000X\n");
                    std::console::flush();
                    print("line");
                }
                """, "console-managed.vs").Success, "load terminal operations");
			GC.Collect();
			GC.WaitForPendingFinalizers();
			Require(session.Run().Success, "terminal operations with rooted callbacks: " + policy + " / " + session.GetLastError() + " / " + string.Join("; ", session.GetDiagnostics().Select(item => item.Message + " at " + item.Line)));
			Require(writes.SequenceEqual(["你好\0X\n"]) && lines.SequenceEqual(["line"]), "exact UTF-8 text and embedded NUL stay separate from line output");
			Require(commands.SequenceEqual(new (VeroScriptConsoleOperation, int, int)[] { (VeroScriptConsoleOperation.SetColor, 9, -1), (VeroScriptConsoleOperation.SetCursorVisible, 0, 0), (VeroScriptConsoleOperation.MoveCursor, 5, 3), (VeroScriptConsoleOperation.ClearLine, 0, 0), (VeroScriptConsoleOperation.ClearLine, 1, 0), (VeroScriptConsoleOperation.ClearLine, 2, 0), (VeroScriptConsoleOperation.Clear, 0, 0), (VeroScriptConsoleOperation.ResetStyle, 0, 0), (VeroScriptConsoleOperation.ResetStyle, 0, 0), (VeroScriptConsoleOperation.SetCursorVisible, 1, 0) }), "native operation identities, argument order, bounds and cleanup");
		}
	}

	private static void CheckUnsupported()
	{
		const string unsupported = """
            void main() {
                assert(!std::console::supports_color() && !std::console::supports_cursor()
                    && !std::console::supports_clear());
                assert(!std::console::set_color("red", "default"));
                assert(!std::console::set_cursor_visible(false));
                assert(!std::console::move_cursor(1, 1));
                assert(!std::console::clear() && !std::console::clear_line("all")
                    && !std::console::reset_style());
                assert(std::console::width() == 0 && std::console::height() == 0);
                std::console::write("plain");
                std::console::flush();
            }
            """;
		var lines = new List<string>();
		int commands = 0;
		using (var session = new VeroScriptSession(new() { Print = lines.Add, ConsoleControl = (_, _, _) =>
		{
			commands++;
			return true;
		} }))
		{
			Require(session.LoadSource(unsupported).Success && session.Run().Success, "missing query leaves controls unsupported");
		}

		Require(commands == 0 && lines.SequenceEqual(["plain"]), "plain buffered output emits no controls");
		using var withoutControl = new VeroScriptSession(new() { ConsoleQuery = () => new(true, true, true, true, -1, -2) });
		Require(withoutControl.LoadSource(unsupported).Success && withoutControl.Run().Success, "missing control masks advertised controls and nonpositive dimensions mean unknown");
	}

	private static void CheckCallbackFailures()
	{
		using (var queryFailure = new VeroScriptSession(new() { ConsoleQuery = () => throw new InvalidOperationException("query failure"), ConsoleControl = (_, _, _) => throw new InvalidOperationException("unexpected control") }))
		{
			Require(queryFailure.LoadSource("void main(){assert(!std::console::supports_color());" + "assert(std::console::width()==0);assert(!std::console::clear());}").Success && queryFailure.Run().Success, "query exceptions do not unwind through the native ABI");
		}

		using (var controlFailure = new VeroScriptSession(new() { ConsoleQuery = () => new(true, true, true, true), ConsoleControl = (_, _, _) => throw new InvalidOperationException("control failure") }))
		{
			Require(controlFailure.LoadSource("void main(){assert(!std::console::set_color(\"red\",\"default\"));" + "assert(!std::console::set_cursor_visible(false));}").Success && controlFailure.Run().Success, "control and cleanup exceptions report failure without unwinding");
		}

		foreach (bool throws in new[]
		{
			false,
			true
		}

		)
		{
			using var writeFailure = new VeroScriptSession(new() { ConsoleWrite = _ => throws ? throw new InvalidOperationException("write failure") : false });
			Require(writeFailure.LoadSource("void main(){std::console::write(\"text\");}").Success && !writeFailure.Run().Success && writeFailure.GetDiagnostics().Count != 0, "false and throwing exact-write callbacks fail script execution");
		}
	}

	private static void CheckCleanup()
	{
		foreach (bool limited in new[]
		{
			false,
			true
		}

		)
		{
			var commands = new List<(VeroScriptConsoleOperation, int, int)>();
			using var session = new VeroScriptSession(new() { ConsoleQuery = () => new(true, true, true, true), ConsoleControl = (operation, first, second) =>
			{
				commands.Add((operation, first, second));
				return true;
			} });
			session.ApplyConfiguration(new() { ExecutionBackend = VeroScriptExecutionBackend.Vm, MaxInstructions = limited ? 60UL : 0UL });
			Require(session.LoadSource("void main(){std::console::set_color(\"red\",\"default\");" + "std::console::set_cursor_visible(false);" + (limited ? "while(true){}" : "assert(false);") + "}").Success && !session.Run().Success, "script failure or instruction limit");
			Require(commands.Count == 4 && commands[2] == (VeroScriptConsoleOperation.ResetStyle, 0, 0) && commands[3] == (VeroScriptConsoleOperation.SetCursorVisible, 1, 0), "failure cleanup retains managed callback state");
		}
	}

	private static void CheckPermission()
	{
		int commands = 0;
		int writes = 0;
		using var session = new VeroScriptSession(new() { ConsoleQuery = () => new(true, true, true, true), ConsoleControl = (_, _, _) =>
		{
			commands++;
			return true;
		}, ConsoleWrite = _ =>
		{
			writes++;
			return true;
		} });
		session.ApplyConfiguration(new() { CapabilityPolicyEnabled = true, AllowedScriptCapabilities = VeroScriptScriptCapabilities.None, ExecutionBackend = VeroScriptExecutionBackend.Vm });
		foreach (string operation in new[]
		{
			"std::console::clear();",
			"std::console::write(\"denied\");"
		}

		)
		{
			Require(session.LoadSource("void main(){" + operation + "}").Success && !session.Run().Success, "console output permission is required");
		}

		Require(commands == 0 && writes == 0, "denied output never reaches managed callbacks");
	}

	private static void Require(bool condition, string message)
	{
		if (!condition)
		{
			throw new InvalidOperationException("Console: " + message);
		}
	}
}
