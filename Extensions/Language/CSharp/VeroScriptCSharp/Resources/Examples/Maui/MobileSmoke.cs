// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript;

namespace VeroScriptMauiHost;

internal static class MobileSmoke
{
	internal static string Run(string resourceRoot, string cacheRoot)
	{
		uint abi = VeroScriptRuntime.AbiVersion;
		if ((VeroScriptRuntime.CompileCapabilities & VeroScriptCompileCapabilities.AnnotationProcessing) == 0)
		{
			throw new InvalidOperationException("Native mobile compile services do not match this SDK.");
		}
		VeroScriptRuntime.LoadCatalogDirectory(Path.Combine(resourceRoot, "localization"));
		var output = new List<string>();
		using var session = new VeroScriptSession(new() { Print = output.Add, Diagnostic = output.Add });
		session.ApplyConfiguration(new()
		{
			ExecutionBackend = VeroScriptExecutionBackend.Vm,
			StandardLibrarySearchPaths = [Path.Combine(resourceRoot, "stdlib")],
			BytecodeCacheDirectory = Path.Combine(cacheRoot, "veroscript"),
			MaxCallDepth = 256,
			EnableExecutionControl = true,
			MaxInstructions = 100_000,
			TimeoutMilliseconds = 5_000,
			CapabilityPolicyEnabled = true,
			AllowedScriptCapabilities = VeroScriptScriptCapabilities.ConsoleOutput
		});
		session.SetMemoryLimits(new(16UL * 1024 * 1024, 32UL * 1024 * 1024));
		int callbacks = 0;
		session.RegisterFunction("host_increment", VeroScriptNativeType.Int32,
			[new(VeroScriptNativeType.Int32)], (context, result) =>
			{
				++callbacks;
				result.Set(context.GetArgument(0).GetInt32() + 1);
			});
		if (!session.LoadSource("print(\"initialized\"); void main(){print(\"unexpected main\");}").Success
			|| !session.Run(VeroScriptRunMode.EvalOnce).Success
			|| !output.SequenceEqual(["initialized"]))
		{
			throw new InvalidOperationException("Native mobile one-shot initialization failed.");
		}
		output.Clear();
		var load = session.LoadSource("""
            import "core.vs";
            int answer(int value) { return host_increment(value); }
            void main()
            {
                assert(!std::memory::native_addresses_available());
                auto bytes = new std::memory::buffer(8);
                std::memory::write<uint64>(bytes, 0, 18446744073709551615u64);
                auto header = bytes.readonly_slice(0, 8);
                assert(std::memory::read<uint64>(header, 0) == 18446744073709551615u64);
                print(answer(41));
            }
            """, Path.Combine(resourceRoot, "main.vs"));
		if (!load.Success || !session.Run().Success)
		{
			throw new InvalidOperationException("Native mobile script failed: " + session.GetLastError());
		}
		using var argument = new VeroScriptValue();
		argument.Set(41);
		var (value, call) = session.CallFunction("answer", argument);
		using (value)
		{
			if (!call.Success || value.GetInt32() != 42 || callbacks != 2 || !output.Contains("42"))
			{
				throw new InvalidOperationException("Native mobile callback/value checks failed.");
			}
		}
		return $"PASS: ABI 0x{abi:X8}, {VeroScriptRuntime.HostPlatform}, run modes, callbacks, values, stdlib, localization\n"
			+ string.Join('\n', output);
	}
}
