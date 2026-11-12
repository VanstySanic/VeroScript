// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript;

internal static class JitStatisticsContractTests
{
	internal static void Run()
	{
		string? backend = Environment.GetEnvironmentVariable("VEROSCRIPT_TEST_JIT_BACKEND");
		foreach (VeroScriptExecutionBackend policy in Enum.GetValues<VeroScriptExecutionBackend>())
		{
			if (policy != VeroScriptExecutionBackend.Vm && string.IsNullOrEmpty(backend))
			{
				continue;
			}

			using var session = new VeroScriptSession();
			if (!string.IsNullOrEmpty(backend))
			{
				session.SetJitBackend(backend);
			}
			session.ApplyConfiguration(new() { ExecutionBackend = policy });
			session.SetJitPolicy(new(1, 128, 128, 128));
			Require(session.LoadSource(
				"int Answer(){return 7;}"
				+ "int Sum(int n){int total=0;for(int i=0;i<n;i++)total+=i;return total;}"
				+ "int Protected(){try{return 9;}finally{int ignored=1;}}"
				+ "int Divide(int a,int b){return a/b;}void main(){}",
				"managed-jit-statistics.vs").Success, "load statistics program");
			bool nativeCode = session.GetJitBackendInfo().NativeCode;
			using var result = new VeroScriptValue();
			VeroScriptJitCacheStats? coldCache = null;
			for (int call = 0; call < 3; ++call)
			{
				Require(session.CallFunction("Answer", [], result).Success && result.GetInt32() == 7,
					$"{policy} answer call {call}");
				VeroScriptJitStats stats = session.GetLastJitStats();
				Require(OrdinaryEntries(stats) == (policy == VeroScriptExecutionBackend.Vm ? 0UL : 1UL)
					&& stats.ScalarNativeFastPathCalls == 0,
					$"{policy} call {call} reports one actual entry independently of host calls");
				Require(stats.NativeLeafExecutions + stats.NativeCfgExecutions
					== (policy != VeroScriptExecutionBackend.Vm && nativeCode ? 1UL : 0UL),
					$"{policy} actual native entries agree with the selected backend");
				VeroScriptJitCacheStats cache = session.GetJitCacheStats();
				if (call == 0)
				{
					coldCache = cache;
					Require(policy == VeroScriptExecutionBackend.Vm
						|| cache.CompileAttempts > 0 && cache.RetainedPayloadBytes > 0,
						$"{policy} cold call retains compiled code");
				}
				else
				{
					Require(stats.CompileAttempts == 0 && cache.CompileAttempts == coldCache!.CompileAttempts
						&& cache.RetainedPayloadBytes == coldCache.RetainedPayloadBytes,
						$"{policy} warm call resets execution statistics and preserves the cache budget");
				}
			}

			using var argument = new VeroScriptValue();
			using var zero = new VeroScriptValue();
			argument.Set(16);
			zero.Set(0);
			Require(session.CallFunction("Sum", [argument], result).Success && result.GetInt32() == 120,
				$"{policy} CFG execution result");
			Require(policy == VeroScriptExecutionBackend.Vm || !nativeCode || IntPtr.Size != 8
				|| session.GetLastJitStats().NativeCfgExecutions == 1,
				$"{policy} reports the actual native CFG entry");
			Require(session.CallFunction("Protected", [], result).Success && result.GetInt32() == 9,
				$"{policy} protected execution result");
			VeroScriptJitStats fallback = session.GetLastJitStats();
			Require(OrdinaryEntries(fallback) == fallback.VmTrampolineExecutions
				&& fallback.VmTrampolineExecutions == fallback.Dispatches,
				$"{policy} canonical VM fallback reports only dispatched trampolines");
			result.Set(99);
			Require(!session.CallFunction("Divide", [argument, zero], result).Success && result.GetInt32() == 99
				&& OrdinaryEntries(session.GetLastJitStats()) == (policy == VeroScriptExecutionBackend.Vm ? 0UL : 1UL),
				$"{policy} failed execution counts its entry and preserves the output");
			session.Clear();
			Require(OrdinaryEntries(session.GetLastJitStats()) == 0,
				$"{policy} clearing the session resets actual execution counters");
		}
	}

	private static ulong OrdinaryEntries(VeroScriptJitStats stats) => stats.NativeLeafExecutions
		+ stats.NativeCfgExecutions + stats.BridgeExecutions + stats.LinearExecutions + stats.VmTrampolineExecutions;

	private static void Require(bool condition, string message)
	{
		if (!condition)
		{
			throw new InvalidOperationException($"JIT statistics contract failed: {message}.");
		}
	}
}
