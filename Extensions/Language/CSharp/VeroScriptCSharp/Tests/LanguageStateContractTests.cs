// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript;

internal static class LanguageStateContractTests
{
	private const string Source = """
        int publicCounter = 9;
        namespace LanguageStateFunctions { int answer() { return 42; } }
        fn()->int make_function_capture() { return [copy = LanguageStateFunctions::answer]() -> int { return copy(); }; }
        fn()->int make_mutable(int seed) { return [count = seed]() mutable -> int { return ++count; }; }
        fn()->int make_reference() { int seed = 40; return [&value = seed]() -> int { return ++value; }; }
        fn()->int make_shared_static(int seed) { return [seed]() -> int { static int count = seed; return ++count; }; }
        int next_static(int seed) { static int count = seed; return ++count; }
        int invoke(fn()->int operation) { return operation(); }
        int static_batch() { auto a = make_shared_static(10); auto b = make_shared_static(90); return a() * 10000 + b() * 100 + a(); }
        int plain_static_batch() { int first = next_static(30); return first * 100 + next_static(99); }
        int backend_probe(int value) { return value + 1; }
        class StaticLifetime { public: ~StaticLifetime() { state_destroyed(); } };
        void touch_static() { static StaticLifetime instance = StaticLifetime(); }
        """;
	internal static void Run()
	{
		string? vm = Environment.GetEnvironmentVariable("VEROSCRIPT_TEST_VM_BACKEND");
		string? jit = Environment.GetEnvironmentVariable("VEROSCRIPT_TEST_JIT_BACKEND");
		foreach (string? backend in string.IsNullOrEmpty(vm) ? new string? []
		{
			null
		}

		: [null, vm])
		{
			CheckState(backend, null, persistent: false);
			CheckState(backend, null, persistent: true);
			if (!string.IsNullOrEmpty(jit))
			{
				CheckState(backend, jit, persistent: false);
			}
		}
	}

	private static void CheckState(string? vm, string? jit, bool persistent)
	{
		var diagnostics = new List<string>();
		using var session = new VeroScriptSession(new() { Diagnostic = diagnostics.Add });
		if (vm is not null)
		{
			session.SetVmBackend(vm);
		}

		session.HotReloadEnabled = persistent;
		if (jit is not null)
		{
			session.SetJitBackend(jit);
			session.ApplyConfiguration(new() { ExecutionBackend = VeroScriptExecutionBackend.Jit });
		}

		int destroyed = 0;
		session.RegisterFunction("state_destroyed", VeroScriptNativeType.Void, [], (_, _) => ++destroyed);
		Require(session.LoadSource(Source, "managed-language-state.vs").Success, "load captures and local statics: " + string.Join("; ", diagnostics));
		using var seed = new VeroScriptValue();
		using var output = new VeroScriptValue();
		using var function = new VeroScriptValue();
		using var other = new VeroScriptValue();
		void Make(string name, int value, VeroScriptValue result)
		{
			seed.Set(value);
			Require(session.CallFunction(name, [seed], result).Success, "call " + name);
		}

		void Invoke(VeroScriptValue value, int expected)
		{
			Require(session.CallFunction("invoke", [value], output).Success && output.GetInt32() == expected, "invoke closure with shared captured state");
		}

		Require(session.CallFunction("make_function_capture", [], function).Success, "export initialized namespace function capture");
		Invoke(function, 42);
		Make("make_mutable", 40, function);
		Make("make_mutable", 100, other);
		using var alias = function.Clone();
		using var retained = session.RetainCallable(function);
		function.Reset();
		GC.Collect();
		GC.WaitForPendingFinalizers();
		var(returned, invocation) = retained.Invoke();
		using (returned)
		{
			Require(invocation.Success && returned.GetInt32() == 41, "retained initialized capture survives reset and collection");
		}

		Invoke(alias, 42);
		Invoke(other, 101);
		Require(session.CallFunction("make_reference", [], function).Success, "export initialized reference capture");
		Invoke(function, 41);
		Invoke(function, 42);
		if (persistent)
		{
			Make("make_shared_static", 10, function);
			Make("make_shared_static", 90, other);
			Invoke(function, 11);
			Invoke(other, 12);
			Invoke(function, 13);
			Make("next_static", 30, output);
			Require(output.GetInt32() == 31, "initialize retained state static");
			Make("next_static", 99, output);
			Require(output.GetInt32() == 32, "reuse retained state static");
			using var isolated = new VeroScriptSession();
			if (vm is not null)
			{
				isolated.SetVmBackend(vm);
			}

			isolated.HotReloadEnabled = true;
			isolated.RegisterFunction("state_destroyed", VeroScriptNativeType.Void, [], (_, _) =>
			{
			});
			Require(isolated.LoadSource(Source, "isolated-language-state.vs").Success, "load independent state");
			seed.Set(5);
			Require(isolated.CallFunction("next_static", [seed], output).Success && output.GetInt32() == 6, "local statics are isolated between sessions");
			Make("next_static", 0, output);
			Require(output.GetInt32() == 33, "independent session does not mutate existing state");
		}
		else
		{
			for (int repeat = 0; repeat != 2; ++repeat)
			{
				Require(session.CallFunction("static_batch", [], output).Success && output.GetInt32() == 111213, "lambda statics share inside a call and reset between fresh calls");
				Require(session.CallFunction("plain_static_batch", [], output).Success && output.GetInt32() == 3132, "function statics follow fresh state lifetime");
			}
		}

		Require(session.CallFunction("touch_static", [], output).Success && destroyed == (persistent ? 0 : 1), "static object cleanup follows execution state lifetime");
		string reflection = session.ExportReflectionJson();
		Require(reflection.Contains("\"name\":\"publicCounter\"", StringComparison.Ordinal) && !reflection.Contains("\"name\":\"count\"", StringComparison.Ordinal) && !reflection.Contains("$static$", StringComparison.Ordinal), "local statics stay private in reflection");
		if (jit is not null)
		{
			Make("backend_probe", 41, output);
			Require(output.GetInt32() == 42 && session.GetLastJitStats().Dispatches != 0, "JIT remains active after closure and static operations");
		}

		try
		{
			session.Clear();
			throw new InvalidOperationException("Clear accepted live external roots.");
		}
		catch (VeroScriptException error)when (error.Status == VeroScriptStatus.EngineError)
		{
		}

		Require(session.IsLoaded && retained.IsValid && destroyed == (persistent ? 0 : 1), "refused Clear preserves the loaded state and live callable");
		Invoke(alias, 43);
		retained.Dispose();
		function.Reset();
		other.Reset();
		alias.Reset();
		session.Clear();
		Require(destroyed == 1 && !session.IsLoaded, "Clear destroys statics once after roots are released");
		Require(session.LoadSource(Source, "managed-language-state-reset.vs").Success, "reload source after Clear");
		Make("next_static", 10, output);
		Require(output.GetInt32() == 11, "Clear discards previous declaration static storage");
		Make("make_mutable", 5, function);
		using var closing = session.RetainCallable(function);
		using var closingAlias = function.Clone();
		Require(session.CallFunction("touch_static", [], output).Success, "initialize a new object static after Clear");
		session.Dispose();
		Require(destroyed == 2 && !closing.IsValid && closingAlias.Kind == VeroScriptValueKind.Invalid, "Dispose cleans the new state once and invalidates retained values");
	}

	private static void Require(bool condition, string message)
	{
		if (!condition)
		{
			throw new InvalidOperationException("Language state: " + message);
		}
	}
}
