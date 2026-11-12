// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript;

internal static class VmBackendContractTests
{
	internal static void Run()
	{
		if ((VeroScriptRuntime.Capabilities & VeroScriptCapabilities.VmBackendSelection) == 0)
		{
			return;
		}

		string? path = Environment.GetEnvironmentVariable("VEROSCRIPT_TEST_VM_BACKEND");
		CheckSelection(null);
		if (!string.IsNullOrEmpty(path))
		{
			CheckSelection(path);
			CheckRetry(path);
		}

		string? jit = Environment.GetEnvironmentVariable("VEROSCRIPT_TEST_JIT_BACKEND");
		if (!string.IsNullOrEmpty(jit))
		{
			CheckJitFreeze(jit, path);
		}
	}

	private static void CheckSelection(string? path)
	{
		using var session = new VeroScriptSession();
		VeroScriptVmBackendInfo initial = session.GetVmBackendInfo();
		Require(!initial.Frozen && (initial.Selected ? !string.IsNullOrEmpty(initial.Id) : initial.Id == "none"), "initial metadata");
		session.SetJitBackend("none");
		session.SetJitBackend("auto");
		if (path is null && initial.Selected)
		{
			session.SetVmBackend("none");
			Require(!session.GetVmBackendInfo().Selected, "none disables the assembled VM");
			session.SetVmBackend("auto");
			Require(session.GetVmBackendInfo().Selected, "auto restores the product fallback");
		}

		if (path is not null)
		{
			session.SetVmBackend(path);
		}

		VeroScriptVmBackendInfo selected = session.GetVmBackendInfo();
		Reject(() => session.SetVmBackend(Path.Combine(AppContext.BaseDirectory, "missing-vm-" + Guid.NewGuid())), VeroScriptStatus.InvalidArgument);
		Require(session.GetVmBackendInfo() == selected, "failed replacement preserves copied metadata");
		session.ApplyConfiguration(new() { ExecutionBackend = VeroScriptExecutionBackend.Vm });
		Require(session.GetVmBackendInfo() == selected, "configuration preserves selection");
		Task.Run(() =>
		{
			Reject(() => session.GetVmBackendInfo(), VeroScriptStatus.WrongThread);
			Reject(() => session.SetVmBackend(null), VeroScriptStatus.WrongThread);
		}).GetAwaiter().GetResult();
		if (!selected.Selected)
		{
			return; // Custom Interop products can omit a default VM.
		}

		Require((selected.Capabilities & 0x1fUL) == 0x1fUL, "bytecode VM capabilities");
		Require(session.LoadSource("int answer(){return 42;}", "vm-choice.vs").Success, "load source");
		var(answer, call) = session.CallFunction("answer");
		using (answer)
		{
			Require(call.Success && answer.GetInt32() == 42, "execute selected VM");
		}

		Require(session.GetVmBackendInfo()is { Frozen: true, Selected: true }, "execution freezes selection");
		session.Clear();
		Reject(() => session.SetVmBackend(null), VeroScriptStatus.InvalidArgument);
		Require(session.GetVmBackendInfo().Frozen && !selected.Frozen, "Clear preserves freeze and snapshots");
		session.Dispose();
		Require(selected.Id.Length > 0, "metadata survives disposal");
		Disposed(() => session.GetVmBackendInfo());
		Disposed(() => session.SetVmBackend(null));
	}

	private static void CheckRetry(string path)
	{
		using var session = new VeroScriptSession();
		session.SetVmBackend(null);
		Require(session.GetVmBackendInfo()is { Id: "none", Selected: false, Frozen: false, Capabilities: 0 }, "clear selection before execution");
		Require(session.LoadSource("int answer(){return 42;}", "vm-retry.vs").Success, "load without a VM");
		using var output = new VeroScriptValue();
		output.Set(17);
		Require(!session.CallFunction("answer", [], output).Success && output.GetInt32() == 17 && !session.GetVmBackendInfo().Frozen, "missing VM preserves output and permits retry");
		session.SetVmBackend(path);
		Require(session.CallFunction("answer", [], output).Success && output.GetInt32() == 42 && session.GetVmBackendInfo().Frozen, "select an external VM and retry");
	}

	private static void CheckJitFreeze(string jit, string? vm)
	{
		using var session = new VeroScriptSession();
		if (!string.IsNullOrEmpty(vm))
		{
			session.SetVmBackend(vm);
		}

		session.SetJitBackend(jit);
		session.ApplyConfiguration(new() { ExecutionBackend = VeroScriptExecutionBackend.Jit });
		Require(session.LoadSource("int answer(){return 42;}", "vm-jit-freeze.vs").Success, "load JIT source");
		var(value, run) = session.CallFunction("answer");
		using (value)
		{
			Require(run.Success && value.GetInt32() == 42 && session.GetVmBackendInfo().Frozen, "JIT execution freezes its VM domain too");
		}

		session.Clear();
		Reject(() => session.SetVmBackend(null), VeroScriptStatus.InvalidArgument);
	}

	private static void Reject(Action action, VeroScriptStatus status)
	{
		try
		{
			action();
		}
		catch (VeroScriptException exception)when (exception.Status == status)
		{
			return;
		}

		throw new InvalidOperationException("VM backend: expected " + status);
	}

	private static void Disposed(Action action)
	{
		try
		{
			action();
		}
		catch (ObjectDisposedException)
		{
			return;
		}

		throw new InvalidOperationException("VM backend: disposed session was accepted");
	}

	private static void Require(bool condition, string message)
	{
		if (!condition)
		{
			throw new InvalidOperationException("VM backend: " + message);
		}
	}
}
