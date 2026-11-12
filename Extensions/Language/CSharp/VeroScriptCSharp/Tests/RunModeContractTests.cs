// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript;

internal static class RunModeContractTests
{
	internal static void Run()
	{
		Require((VeroScriptRuntime.Capabilities & VeroScriptCapabilities.EvalOnce) != 0,
			"runtime advertises one-shot evaluation");
		string? backend = Environment.GetEnvironmentVariable("VEROSCRIPT_TEST_JIT_BACKEND");
		foreach (VeroScriptExecutionBackend policy in Enum.GetValues<VeroScriptExecutionBackend>())
		{
			if (policy == VeroScriptExecutionBackend.Jit && string.IsNullOrEmpty(backend))
			{
				continue;
			}
			foreach (VeroScriptRunMode mode in Enum.GetValues<VeroScriptRunMode>())
			{
				var output = new List<string>();
				using var session = new VeroScriptSession(new() { Print = output.Add });
				if (policy == VeroScriptExecutionBackend.Jit)
				{
					session.SetJitBackend(backend);
				}
				session.ApplyConfiguration(new() { ExecutionBackend = policy });
				Require(session.LoadSource("int value=42; print(value); void main(){print(7);}").Success,
					"load run-mode source");
				Require(session.Run(mode).Success
					&& output.SequenceEqual(mode == VeroScriptRunMode.Script ? ["42", "7"] : ["42"]),
					$"initialization and main selection for {policy}/{mode}");
				if (mode != VeroScriptRunMode.EvalOnce)
				{
					continue;
				}
				if (policy == VeroScriptExecutionBackend.Jit)
				{
					Require(session.GetLastJitStats().Dispatches > 0, "initializer enters the selected JIT backend");
				}
				output.Clear();
				Require(session.Run(mode).Success && output.SequenceEqual(["42"]),
					"ordinary one-shot evaluation initializes on every run");
				output.Clear();
				session.HotReloadEnabled = true;
				Require(session.Run(mode).Success && output.SequenceEqual(["42"])
					&& session.GetLastJitStats().Dispatches == 0,
					"retained initialization runs once on the VM");
				output.Clear();
				Require(session.Run(mode).Success && output.Count == 0,
					"hot-reload preserves initialized globals");
			}
		}
		if (!string.IsNullOrEmpty(backend))
		{
			CheckPersistentJit(backend);
		}

		var effects = new List<string>();
		using var withoutBackend = new VeroScriptSession(new() { Print = effects.Add });
		withoutBackend.ApplyConfiguration(new() { ExecutionBackend = VeroScriptExecutionBackend.Jit });
		Require(withoutBackend.LoadSource("print(42);").Success, "load forced-JIT initializer");
		Require(!withoutBackend.Run(VeroScriptRunMode.EvalOnce).Success && effects.Count == 0,
			"forced JIT without a backend fails before initialization");
		try
		{
			withoutBackend.Run((VeroScriptRunMode)3);
			throw new InvalidOperationException("Unknown run mode was accepted.");
		}
		catch (VeroScriptException exception) when (exception.Status == VeroScriptStatus.InvalidArgument)
		{
			Require(effects.Count == 0, "invalid run mode produced no side effects");
		}
	}

	private static void CheckPersistentJit(string backend)
	{
		using var session = new VeroScriptSession();
		session.SetJitBackend(backend);
		session.ApplyConfiguration(new() { ExecutionBackend = VeroScriptExecutionBackend.Jit });
		session.HotReloadEnabled = true;
		Require(session.LoadSource("int state=7; int next(){state++;return state;} void main(){}").Success,
			"load persistent JIT program");
		using var value = new VeroScriptValue();
		Require(session.CallFunction("next", Array.Empty<VeroScriptValue>(), value).Success
			&& value.GetInt32() == 8 && session.GetLastJitStats().Dispatches > 0,
			"persistent execution uses the selected backend");
		Require(session.CallFunction("next", Array.Empty<VeroScriptValue>(), value).Success
			&& value.GetInt32() == 9 && session.GetLastJitStats().Dispatches > 0
			&& session.GetLastJitStats().CompileAttempts == 0,
			"persistent execution keeps state and reuses the compiled entry");
	}

	private static void Require(bool condition, string message)
	{
		if (!condition)
		{
			throw new InvalidOperationException("Run modes: " + message);
		}
	}
}
