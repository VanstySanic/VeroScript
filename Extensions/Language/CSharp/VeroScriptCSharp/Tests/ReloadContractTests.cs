// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript;

internal static class ReloadContractTests
{
	internal static void Run()
	{
		Require((VeroScriptRuntime.Capabilities & VeroScriptCapabilities.ReloadStatus) != 0,
			"runtime advertises reload status");
		string path = Path.Combine(Path.GetTempPath(), $"vs-csharp-reload-{Guid.NewGuid():N}.vs");
		const string prelude = "class Item{public:int value=8;};Item make(){return Item();}int divide(int n){return 10/n;}";
		const string badInitializer = prelude + "int bad=divide(0);int version(){return 2;}void main(){}";
		using var session = new VeroScriptSession();
		session.ApplyConfiguration(new() { ExecutionBackend = VeroScriptExecutionBackend.Vm });
		session.HotReloadEnabled = true;
		try
		{
			File.WriteAllText(path, prelude + "int version(){return 1;}void main(){}");
			Require(session.LoadFile(path).Success && session.Run(VeroScriptRunMode.Eval).Success,
				"initialize the original program");
			VeroScriptReloadStatus original = session.GetReloadStatus();
			Require(original.State == VeroScriptReloadState.None && original.OperationId == 0
				&& original.CommittedGeneration != 0 && original.CurrentGeneration == original.CommittedGeneration,
				"initial state identifies the committed generation");

			var (held, make) = session.CallFunction("make");
			using (held)
			{
				Require(make.Success, "export a module value");
				File.WriteAllText(path, badInitializer);
				Require(!session.Reload().Success, "an external value blocks reload");
				string error = session.GetLastError();
				Require(error.Contains("release external value roots", StringComparison.Ordinal),
					"blocked reload explains how to recover");
				Require(session.GetReloadStatus() == original && session.GetLastError() == error,
					"query preserves reload state and diagnostics");
			}

			File.WriteAllText(path, "int version( {");
			Require(!session.Reload().Success, "reject an invalid replacement");
			VeroScriptReloadStatus rejected = session.GetReloadStatus();
			Require(rejected.State == VeroScriptReloadState.Rejected
				&& rejected.OperationId > original.OperationId
				&& rejected.CurrentGeneration == original.CurrentGeneration
				&& rejected.CommittedGeneration == original.CommittedGeneration,
				"rejection preserves the committed generation");

			File.WriteAllText(path, badInitializer);
			Require(session.Reload().Success, "stage a replacement with a failing initializer");
			VeroScriptReloadStatus pending = session.GetReloadStatus();
			Require(pending.State == VeroScriptReloadState.Pending && pending.OperationId > rejected.OperationId
				&& pending.PreviousGeneration == original.CommittedGeneration
				&& pending.CandidateGeneration == pending.CurrentGeneration
				&& pending.CurrentGeneration != pending.CommittedGeneration
				&& pending.CommittedGeneration == original.CommittedGeneration,
				"staged and committed generations differ");
			Require(!session.Run(VeroScriptRunMode.Eval).Success, "initializer fails");
			VeroScriptReloadStatus rolledBack = session.GetReloadStatus();
			Require(rolledBack.State == VeroScriptReloadState.RolledBack && rolledBack.OperationId == pending.OperationId
				&& rolledBack.CurrentGeneration == original.CommittedGeneration
				&& rolledBack.CommittedGeneration == original.CommittedGeneration,
				"failed initialization restores the previous program");
			RequireVersion(session, 1);

			File.WriteAllText(path, prelude + "int added=2;int version(){return added;}void main(){divide(0);}");
			Require(session.Reload().Success, "stage a replacement with a failing main");
			pending = session.GetReloadStatus();
			Require(pending.State == VeroScriptReloadState.Pending && pending.OperationId > rolledBack.OperationId,
				"each staged operation has a distinct identity");
			Require(!session.Run().Success, "main fails after initialization");
			VeroScriptReloadStatus applied = session.GetReloadStatus();
			Require(applied.State == VeroScriptReloadState.AppliedWithErrors && applied.OperationId == pending.OperationId
				&& applied.CurrentGeneration == pending.CandidateGeneration
				&& applied.CommittedGeneration == pending.CandidateGeneration,
				"execution failure after commit keeps the replacement");
			RequireVersion(session, 2);
		}
		finally
		{
			File.Delete(path);
		}
		CheckRejectedHotReloadSetting();
	}

	private static void CheckRejectedHotReloadSetting()
	{
		string path = Path.Combine(Path.GetTempPath(), $"vs-csharp-reset-{Guid.NewGuid():N}.vs");
		const string source = "class Item{public:int id;Item(){id=0;}Item(int n){id=n;}"
			+ "~Item(){if(id==2){try{for(;;){}}finally{for(;;){}}}}};"
			+ "class Owner{public:Item item=Item(1);};Owner root=Owner();void main(){}";
		using var session = new VeroScriptSession();
		session.ApplyConfiguration(new() { ExecutionBackend = VeroScriptExecutionBackend.Vm });
		session.HotReloadEnabled = true;
		try
		{
			File.WriteAllText(path, source);
			Require(session.LoadFile(path).Success && session.Run(VeroScriptRunMode.Eval).Success,
				"initialize bounded rollback fixture");
			File.WriteAllText(path, source + "int fail(){root.item=Item(2);throw 7;}int trigger=fail();");
			Require(session.Reload().Success, "stage a replacement with bounded cleanup");
			session.ApplyConfiguration(new()
			{
				ExecutionBackend = VeroScriptExecutionBackend.Vm,
				MaxInstructions = 80
			});
			Require(!session.Run(VeroScriptRunMode.Eval).Success, "replacement cleanup exhausts its budget");
			VeroScriptReloadStatus status = session.GetReloadStatus();
			Require(status.State == VeroScriptReloadState.ResetRequired, "unfinished rollback requires recovery");
			bool rejected = false;
			try
			{
				session.HotReloadEnabled = false;
			}
			catch (VeroScriptException exception) when (exception.Status == VeroScriptStatus.EngineError)
			{
				rejected = exception.Message.Contains("Destroy and recreate", StringComparison.Ordinal);
			}
			Require(rejected && session.HotReloadEnabled && session.GetReloadStatus() == status,
				"rejected hot-reload setting preserves the flag, operation and recovery state");
		}
		finally
		{
			File.Delete(path);
		}
	}

	private static void RequireVersion(VeroScriptSession session, int expected)
	{
		var (value, call) = session.CallFunction("version");
		using (value)
		{
			Require(call.Success && value.GetInt32() == expected, "calls use the committed version");
		}
	}

	private static void Require(bool condition, string message)
	{
		if (!condition)
		{
			throw new InvalidOperationException("Reload status: " + message);
		}
	}
}
