// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Collections.Concurrent;
using VeroScript;
using VeroScript.Interop;

string publicApiBaseline = Path.Combine(AppContext.BaseDirectory, "PublicApi.txt");
if (args is ["--write-public-api", string outputPath])
{
	File.WriteAllLines(outputPath, PublicApiContract.Generate(typeof(VeroScriptRuntime).Assembly));
	return;
}

PublicApiContract.Verify(typeof(VeroScriptRuntime).Assembly, publicApiBaseline, "VeroScriptCSharp");
if (args is ["--vm-backend-contracts"])
{
	VmBackendContractTests.Run();
	Console.WriteLine("VM backend contracts passed.");
	return;
}

if (args is ["--variant-contracts"])
{
	VariantContractTests.Run();
	return;
}

if (args is ["--language-state-contracts"])
{
	LanguageStateContractTests.Run();
	return;
}

if (args is ["--stdlib-contracts"])
{
	MemoryContractTests.CheckStandardLibraryLimits();
	return;
}

if (args is ["--memory-unsupported"])
{
	MemoryContractTests.RunUnsupportedRuntime();
	return;
}

if (args is ["--annotation-contracts"])
{
	AnnotationContractTests.Run();
	return;
}

if (args is ["--native-sdk-contracts"])
{
	NativeSdkContractTests.Run();
	return;
}

if (args is ["--configuration-disposal"])
{
	NativeSdkContractTests.CheckConfigurationDisposal();
	return;
}

if (args is ["--script-extensions"])
{
	ScriptCompileExtensionTests.Run();
	return;
}

if (args is ["--console-contracts"])
{
	ConsoleContractTests.Run();
	return;
}

if (args is ["--core-type-contracts"])
{
	CoreTypeContractTests.Run();
	return;
}

if (args is ["--protocol-lifetimes"])
{
	ProtocolLifetimeTests.Run();
	return;
}

if (args is ["--native-lifetimes"])
{
	NativeLifetimeTests.Run();
	return;
}

static void Expect(bool condition, string message)
{
	if (!condition)
	{
		throw new InvalidOperationException($"FAILED: {message}");
	}
}

static void ExpectThrows<TException>(Action action, string message)
	where TException : Exception
{
	try
	{
		action();
	}
	catch (TException)
	{
		return;
	}

	throw new InvalidOperationException($"FAILED: {message}");
}

static void ExpectNativeSize<T>(int x64, int x86)
	where T : unmanaged => Expect(Unsafe.SizeOf<T>() == (Environment.Is64BitProcess ? x64 : x86), $"native layout for {typeof(T).Name}");
ExpectNativeSize<NativeMemoryLimits>(24, 24);
ExpectNativeSize<NativeMemoryStats>(32, 32);
ExpectNativeSize<NativeNumericLimits>(48, 48);
Expect((ulong)VeroScriptCapabilities.Numerics == 0x800000 && (ulong)VeroScriptJitBackendCapabilities.ControlledExecution == 0x10, "numeric and controlled-execution capability bits match the C ABI");
ExpectNativeSize<NativeMemoryLeaseToken>(16, 16);
ExpectNativeSize<NativeMemoryLease>(48, 40);
static unsafe void CopyInvalidUtf16()
{
	char invalid = '\udc00';
	_ = Utf16.Copy(&invalid, 1);
}

static unsafe void CheckAotDescriptorVersion()
{
	// The runtime display includes a trailing bytecode-version description.
	Version product = Version.Parse(VeroScriptRuntime.RuntimeVersion.Split(' ', 2)[0]);
	uint coreVersion = ((uint)product.Major << 24) | ((uint)product.Minor << 16) | (uint)product.Build;
	using var digest = new Utf8(new string ('0', 64));
	using var identity = new Utf8("managed-aot-description-test");
	using var target = new Utf8(VeroScriptRuntime.HostPlatform);
	var description = new NativeAotDescription
	{
		StructSize = (uint)sizeof(NativeAotDescription),
		ProgramDigest = digest.View,
		NativeBindingsDigest = digest.View,
		ServiceAbi = identity.View,
		CoreImplementation = identity.View,
		Target = target.View
	};
	foreach (bool matching in new[]
	{
		true,
		false
	}

	)
	{
		description.CoreVersion = matching ? coreVersion : coreVersion ^ 1u;
		nint program = 0, error = 0;
		try
		{
			VeroScriptStatus status = NativeMethods.vs_interop_aot_program_create(&description, null, 0, &program, &error);
			Expect(status == (matching ? VeroScriptStatus.Ok : VeroScriptStatus.InvalidArgument) && (program != 0) == matching && (error == 0) == matching, "AOT descriptors validate the encoded product version and preserve output ownership");
		}
		finally
		{
			NativeMethods.vs_interop_aot_program_release(program);
			NativeMethods.vs_interop_buffer_destroy(error);
		}
	}
}

static void ExpectSequenceAlias(string typeName, Action<VeroScriptValue> setValue, Func<VeroScriptValue, bool> matches)
{
	using var scalar = new VeroScriptValue();
	using var sequence = new VeroScriptValue();
	setValue(scalar);
	sequence.SetSequence(VeroScriptValueKind.Array, typeName, [scalar]);
	using VeroScriptValue item = sequence.GetSequenceElement(0);
	Expect(sequence.SequenceCount == 1 && matches(item), $"managed scalar container alias '{typeName}'");
}

static VeroScriptSession CreateTestSession(VeroScriptSessionOptions? options = null)
{
	var session = new VeroScriptSession(options);
	try
	{
		string? backendPath = Environment.GetEnvironmentVariable("VEROSCRIPT_TEST_JIT_BACKEND");
		if (!string.IsNullOrEmpty(backendPath))
		{
			session.SetJitBackend(backendPath);
		}

		return session;
	}
	catch
	{
		session.Dispose();
		throw;
	}
}

static VeroScriptJitBackendInfo TestBackendInfo()
{
	using var session = CreateTestSession();
	return session.GetJitBackendInfo();
}

static void TestJitBackendSelection()
{
	using var session = new VeroScriptSession();
	VeroScriptJitBackendInfo? selectedSnapshot = null;
	Expect(session.GetJitBackend() == VeroScriptJitBackend.None && session.GetJitBackendInfo()is { Id: "none", Target: "interpreter", CapabilityFlags: VeroScriptJitBackendCapabilities.None, NativeCode: false }, "no implicit backend or native capabilities");
	session.ApplyConfiguration(new VeroScriptConfiguration { ExecutionBackend = VeroScriptExecutionBackend.Jit });
	Expect(session.LoadSource("int value(){return 49;}void main(){print(value());}", "choice.vs").Success, "choice source");
	Expect(!session.Run().Success, "forced JIT requires explicit backend");
	string? backendPath = Environment.GetEnvironmentVariable("VEROSCRIPT_TEST_JIT_BACKEND");
	if (!string.IsNullOrEmpty(backendPath))
	{
		session.SetJitBackend(backendPath);
		session.SetJitPolicy(new VeroScriptJitPolicy(1, 128, 128, 128));
		VeroScriptJitBackendInfo selected = session.GetJitBackendInfo();
		selectedSnapshot = selected;
		Expect(session.GetJitBackend() == VeroScriptJitBackend.External && selected.Id == "asmjit", "loaded metadata");
		Expect(selected.NativeCode == selected.CapabilityFlags.HasFlag(VeroScriptJitBackendCapabilities.NativeCode), "backend native-code flag matches its capabilities");
		var(result, call) = session.CallFunction("value");
		using (result)
		{
			Expect(call.Success && result.GetInt32() == 49 && session.GetLastJitStats().Dispatches > 0, "dynamic backend dispatch");
		}

		VeroScriptJitCacheStats retained = session.GetJitCacheStats();
		Expect(retained.RetainedPayloadBytes > 0, "selected backend retains eligible compiled code");
		string missingBackend = Path.Combine(Path.GetTempPath(), "veroscript-missing-backend-" + Guid.NewGuid().ToString("N"));
		ExpectThrows<VeroScriptException>(() => session.SetJitBackend(missingBackend), "invalid path");
		Expect(session.GetJitBackend() == VeroScriptJitBackend.External && session.GetJitBackendInfo() == selected, "failed selection preserves backend metadata");
		Expect(session.GetJitCacheStats().RetainedPayloadBytes == retained.RetainedPayloadBytes, "failed selection preserves retained code");
		string backendDirectory = Directory.Exists(backendPath) ? Path.GetFullPath(backendPath) : Path.GetDirectoryName(Path.GetFullPath(backendPath))!;
		session.SetJitBackend(backendDirectory);
		Expect(session.GetJitBackendInfo() == selected, "backend directory selection");
		Expect(session.GetJitCacheStats().RetainedPayloadBytes == 0, "successful backend replacement discards retained code");
		var(replacementResult, replacementCall) = session.CallFunction("value");
		using (replacementResult)
		{
			Expect(replacementCall.Success && replacementResult.GetInt32() == 49, "replacement backend remains executable");
		}

		Expect(session.GetJitCacheStats().RetainedPayloadBytes > 0, "replacement backend retains eligible compiled code");
		session.SetJitBackend(null);
		Expect(selected.Id == "asmjit" && !string.IsNullOrEmpty(selected.Target), "copied backend metadata survives unload");
		Expect(session.GetJitCacheStats().NativeBytes == 0 && session.GetJitCacheStats().RetainedPayloadBytes == 0, "clearing backend discards retained code");
		Expect(!session.Run().Success, "forced JIT fails after backend removal");
		session.SetJitBackend(backendPath);
		var(restoredResult, restoredCall) = session.CallFunction("value");
		using (restoredResult)
		{
			Expect(restoredCall.Success && restoredResult.GetInt32() == 49, "backend can be restored after removal");
		}

		Expect(session.GetJitCacheStats().RetainedPayloadBytes > 0, "restored backend retains eligible compiled code");
		session.SetJitBackend(string.Empty);
		Expect(session.GetJitBackendInfo()is { Id: "none", Target: "interpreter", CapabilityFlags: VeroScriptJitBackendCapabilities.None, NativeCode: false }, "empty path clears loaded backend metadata");
		Expect(session.GetJitCacheStats().NativeBytes == 0 && session.GetJitCacheStats().RetainedPayloadBytes == 0, "empty path discards retained code");
		Expect(!session.Run().Success, "forced JIT fails after empty-path removal");
	}

	session.SetJitBackend(string.Empty);
	Expect(session.GetJitBackend() == VeroScriptJitBackend.None, "empty selection disables backend");
	session.ApplyConfiguration(new VeroScriptConfiguration { ExecutionBackend = VeroScriptExecutionBackend.Vm });
	int idleChecks = 0;
	session.RegisterFunction("verify_backend_idle", VeroScriptNativeType.Void, [], (_, _) =>
	{
		ExpectThrows<VeroScriptException>(() => session.SetJitBackend(null), "backend selection is rejected inside an active callback");
		Expect(session.GetJitBackend() == VeroScriptJitBackend.None, "rejected active selection preserves backend");
		idleChecks++;
	});
	Expect(session.LoadSource("void main(){verify_backend_idle();}", "backend-idle.vs").Success, "backend idle-check source");
	Expect(session.Run().Success && idleChecks == 1, "backend idle contract through managed callback");
	Exception? crossThreadFailure = null;
	Task.Run(() =>
	{
		try
		{
			session.SetJitBackend(null);
		}
		catch (Exception exception)
		{
			crossThreadFailure = exception;
		}
	}).GetAwaiter().GetResult();
	Expect(crossThreadFailure is VeroScriptException { Status: VeroScriptStatus.WrongThread }, "backend selection enforces its managed creator-thread contract");
	session.Dispose();
	Expect(selectedSnapshot is null || selectedSnapshot is { Id: "asmjit", Target.Length: > 0 }, "copied backend metadata survives session disposal");
	ExpectThrows<ObjectDisposedException>(() => session.GetJitBackendInfo(), "backend metadata access rejects a disposed session");
}

TestJitBackendSelection();
JitStatisticsContractTests.Run();
static int RunBackendParitySuite(VeroScriptExecutionBackend backend)
{
	using var backendSession = CreateTestSession();
	backendSession.ApplyConfiguration(new VeroScriptConfiguration { ExecutionBackend = backend });
	backendSession.SetJitPolicy(new VeroScriptJitPolicy(1, 256, 256, 256));
	backendSession.RegisterFunction("host_i8", VeroScriptNativeType.Int8, [new(VeroScriptNativeType.Int8)], (context, result) => result.Set((sbyte)(context.GetArgument(0).GetInt8() + 1)));
	backendSession.RegisterFunction("host_u8", VeroScriptNativeType.UInt8, [new(VeroScriptNativeType.UInt8)], (context, result) => result.Set((byte)(context.GetArgument(0).GetUInt8() + 1)));
	backendSession.RegisterFunction("host_i16", VeroScriptNativeType.Int16, [new(VeroScriptNativeType.Int16)], (context, result) => result.Set((short)(context.GetArgument(0).GetInt16() + 1)));
	backendSession.RegisterFunction("host_u16", VeroScriptNativeType.UInt16, [new(VeroScriptNativeType.UInt16)], (context, result) => result.Set((ushort)(context.GetArgument(0).GetUInt16() + 1)));
	backendSession.RegisterFunction("host_i32", VeroScriptNativeType.Int32, [new(VeroScriptNativeType.Int32)], (context, result) => result.Set(context.GetArgument(0).GetInt32() + 1));
	backendSession.RegisterFunction("host_u32", VeroScriptNativeType.UInt32, [new(VeroScriptNativeType.UInt32)], (context, result) => result.Set(context.GetArgument(0).GetUInt32() + 1));
	backendSession.RegisterFunction("host_i64", VeroScriptNativeType.Int64, [new(VeroScriptNativeType.Int64)], (context, result) => result.Set(context.GetArgument(0).GetInt64() + 1));
	backendSession.RegisterFunction("host_u64", VeroScriptNativeType.UInt64, [new(VeroScriptNativeType.UInt64)], (context, result) => result.Set(context.GetArgument(0).GetUInt64() + 1));
	backendSession.RegisterFunction("host_f32", VeroScriptNativeType.Float, [new(VeroScriptNativeType.Float)], (context, result) => result.Set(context.GetArgument(0).GetFloat() + 1.0f));
	backendSession.RegisterFunction("host_f64", VeroScriptNativeType.Double, [new(VeroScriptNativeType.Double)], (context, result) => result.Set(context.GetArgument(0).GetDouble() + 1.0));
	backendSession.RegisterFunction("host_bool", VeroScriptNativeType.Bool, [new(VeroScriptNativeType.Bool)], (context, result) => result.Set(!context.GetArgument(0).GetBool()));
	VeroScriptRunResult loaded = backendSession.LoadSource("""
        int sum(int n)
        {
            int total = 0;
            for (int i = 0; i < n; i++) total += i;
            return total;
        }
        int containers()
        {
            array<int8> narrow = array<int8>();
            int8 narrowValue = 11;
            narrow.push(narrowValue);
            map<int64,int64> mapped = map<int64,int64>();
            mapped[7i64] = 40i64;
            mapped[7i64] = 41i64;
            set<int> unique = set<int>();
            unique.add(9);
            unique.add(9);
            return to_int(narrow[0]) + to_int(mapped[7i64])
                + (unique.has(9) ? 1 : 0);
        }
        int host_scalars()
        {
            int8 i8 = -5;
            uint8 u8 = 6;
            int16 i16 = -8;
            uint16 u16 = 9;
            int i32 = 11;
            unsigned int u32 = 12U;
            int64 i64 = 13i64;
            uint64 u64 = 14u64;
            float f32 = 1.5f;
            double f64 = 2.5;
            return to_int(host_i8(i8)) + to_int(host_u8(u8))
                + to_int(host_i16(i16)) + to_int(host_u16(u16))
                + host_i32(i32) + to_int(host_u32(u32))
                + to_int(host_i64(i64)) + to_int(host_u64(u64))
                + to_int(host_f32(f32)) + to_int(host_f64(f64))
                + (host_bool(false) ? 1 : 0);
        }
        int exercise(int n)
        {
            shared_aliases();
            string text = "你好，世界";
            return sum(n) + containers() + host_scalars()
                + (text == "你好，世界" ? 1 : 0);
        }
        struct SharedPoint { int x = 1; }
        shared<T> keep_shared<T>(handle<T> value) { return value; }
        void shared_aliases()
        {
            SharedPoint source = SharedPoint();
            shared<SharedPoint> owner = shared(source);
            std::handle<SharedPoint> alias = owner.keep_shared();
            assert(owner == alias);
            alias.value.x = 7;
            assert(owner.value.x == 7 && source.x == 1);
            weak<shared<SharedPoint>> observer = weak<handle<SharedPoint>>(owner);
            std::shared<SharedPoint> locked = observer.lock();
            owner = nullptr; alias = nullptr;
            assert(!observer.expired() && locked.value.x == 7);
            locked = nullptr;
            assert(observer.expired() && observer.lock() == nullptr);
        }
        int many_arguments(
            int a0, int a1, int a2, int a3, int a4, int a5, int a6, int a7,
            int a8, int a9, int a10, int a11, int a12, int a13, int a14,
            int a15, int a16)
        {
            return a0 + a16;
        }
        void main() {}
        """, $"csharp-{backend.ToString().ToLowerInvariant()}-backend.vs");
	Expect(loaded.Success, $"{backend} backend source load: " + string.Join(" | ", backendSession.GetDiagnostics().Select(diagnostic => diagnostic.Message)));
	using var argument = new VeroScriptValue();
	argument.Set(16);
	bool usesJit = backend != VeroScriptExecutionBackend.Vm
		&& !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("VEROSCRIPT_TEST_JIT_BACKEND"));
	int value = 0;
	for (int invocation = 0; invocation < 3; ++invocation)
	{
		var(returnValue, call) = backendSession.CallFunction("exercise", argument);
		using (returnValue)
		{
			Expect(call.Success, $"{backend} backend call");
			int currentValue = returnValue.GetInt32();
			Expect(invocation == 0 || currentValue == value, $"{backend} warm call result parity");
			value = currentValue;
		}

		VeroScriptJitStats stats = backendSession.GetLastJitStats();
		if (!usesJit)
		{
			Expect(stats.CompileAttempts == 0 && stats.Dispatches == 0, $"{backend} without JIT must not compile or dispatch JIT entries");
		}
		else
		{
			bool compiled = invocation == 0
				? stats.CompileAttempts > 0 && stats.CompiledEntries > 0
				: stats.CompileAttempts == 0 && stats.CompiledEntries == 0;
			Expect(compiled && stats.DispatchAttempts > 0 && stats.Dispatches > 0,
				$"{backend} call {invocation + 1} JIT compilation and dispatch "
				+ $"(attempts={stats.CompileAttempts}, compiled={stats.CompiledEntries}, "
				+ $"dispatch-attempts={stats.DispatchAttempts}, dispatches={stats.Dispatches})");
		}
	}

	VeroScriptValue[] manyArguments = Enumerable.Repeat(argument, 17).ToArray();
	var(manyReturnValue, manyCall) = backendSession.CallFunction("many_arguments", manyArguments);
	using (manyReturnValue)
	{
		Expect(manyCall.Success && manyReturnValue.GetInt32() == 32, $"{backend} backend large argument buffer");
	}

	return value;
}

static void RunJitDispatchStatsSuite()
{
	using var session = CreateTestSession();
	session.ApplyConfiguration(new VeroScriptConfiguration { ExecutionBackend = VeroScriptExecutionBackend.Jit });
	session.SetJitPolicy(new VeroScriptJitPolicy(2, 128, 256, 64));
	VeroScriptRunResult loaded = session.LoadSource("""
        interface IDispatch { virtual int add(int value) = 0; };
        class AddOne : IDispatch { int add(int value) { return value + 1; } };
        class AddTwo : IDispatch { int add(int value) { return value + 2; } };
        int RunDispatchStats()
        {
            IDispatch a = AddOne();
            IDispatch b = AddTwo();
            IDispatch selected = a;
            int total = 0;
            for (int i = 0; i < 16; i++)
            {
                int choice = i & 1;
                if (choice == 0) selected = a;
                else selected = b;
                total = selected.add(total);
            }
            return total;
        }
        int DirectLeaf(int value)
        {
            for (int i = 0; i < 4; i++) value = (value * 3 + 1) % 10007;
            return value;
        }
        int RunDirectStats(int value)
        {
            int total = 0;
            for (int i = 0; i < 3; i++) total += DirectLeaf(value + i);
            return total;
        }
        void main() {}
        """, "csharp-jit-dispatch-stats.vs");
	Expect(loaded.Success, "JIT dispatch statistics source load: " + string.Join(" | ", session.GetDiagnostics().Select(diagnostic => diagnostic.Message)));
	var(returnValue, call) = session.CallFunction("RunDispatchStats");
	using (returnValue)
	{
		Expect(call.Success && returnValue.GetInt32() == 24, "polymorphic dispatch through the managed JIT host");
	}

	VeroScriptJitStats stats = session.GetLastJitStats();
	Expect(stats.DispatchAttempts >= stats.Dispatches && stats.Dispatches >= stats.ScalarMethodFastPathInt32, "managed JIT statistics must preserve coherent dispatch counters");
	// AsmJit's 32-bit emitter supports native leaves but not the CFGs exercised here.
	bool hasNativeScalarCfg = Environment.Is64BitProcess && session.GetJitBackendInfo().CapabilityFlags.HasFlag(VeroScriptJitBackendCapabilities.DirectScalarCalls);
	Expect(!hasNativeScalarCfg || stats.ScalarMethodFastPathInt32 >= 16, "managed JIT statistics must expose the selected-receiver scalar fast path");
	using var directArgument = new VeroScriptValue();
	directArgument.Set(2);
	var(directReturnValue, directCall) = session.CallFunction("RunDirectStats", directArgument);
	using (directReturnValue)
	{
		Expect(directCall.Success && directReturnValue.GetInt32() == 849, "generation-scoped direct scalar call through the managed host");
	}

	stats = session.GetLastJitStats();
	Expect(!hasNativeScalarCfg || stats.DirectNativeCalls >= 3, "managed JIT statistics must expose direct native calls");
}

static void RunJitOptimizationStatsSuite()
{
	static (VeroScriptRunResult Result, string[] Output, VeroScriptJitStats Stats) Run(string source, string label)
	{
		var output = new List<string>();
		using var session = CreateTestSession(new VeroScriptSessionOptions { Print = output.Add });
		session.ApplyConfiguration(new VeroScriptConfiguration { ExecutionBackend = VeroScriptExecutionBackend.Jit });
		session.SetJitPolicy(new VeroScriptJitPolicy(1, 256, 256, 256));
		VeroScriptRunResult loaded = session.LoadSource(source, label);
		Expect(loaded.Success, $"{label} source load: " + string.Join(" | ", session.GetDiagnostics().Select(diagnostic => diagnostic.Message)));
		VeroScriptRunResult result = session.Run();
		return (result, output.ToArray(), session.GetLastJitStats());
	}

	var typedMethods = Run("""
        interface ICounter { virtual int update(int salt) = 0; }
        class Counter : ICounter
        {
        public:
            int value = 1;
            int update(int salt)
            {
                value = (value + (salt % 17) + 1) % 1000000007;
                return value;
            }
        }
        int run_bound(int limit)
        {
            Counter c = Counter();
            int state = 7;
            for (int i = 0; i < limit; i++)
            {
                state = (state * 75 + 74) % 65521;
                c.update(state);
            }
            return c.value + state;
        }
        int run_virtual(int limit)
        {
            ICounter c = Counter();
            int state = 7;
            int value = 1;
            for (int i = 0; i < limit; i++)
            {
                state = (state * 75 + 74) % 65521;
                value = c.update(state);
            }
            return value + state;
        }
        void main() { print(run_bound(10000)); print(run_virtual(10000)); }
        """, "csharp-native-typed-loop-stats.vs");
	Expect(typedMethods.Result.Success && typedMethods.Output.SequenceEqual(["95018", "95018"]), "managed native typed-method loop parity");
	Expect(TestBackendInfo().CapabilityFlags.HasFlag(VeroScriptJitBackendCapabilities.NativeTypedLoops) ? typedMethods.Stats.NativeTypedLoopExecutions >= 2 : typedMethods.Stats.NativeTypedLoopExecutions == 0, "managed native typed-loop execution statistics");
	var indexedFallback = Run("""
        interface IndexedOperation { virtual int step(int value) = 0; }
        class IndexedAdd3 : IndexedOperation
        {
        public: int step(int value) { return (value + 3) % 1000000007; }
        }
        class IndexedAdd5 : IndexedOperation
        {
        public: int step(int value) { return (value + 5) % 1000000007; }
        }
        class IndexedAdd7 : IndexedOperation
        {
        public: int step(int value) { return (value + 7) % 1000000007; }
        }
        class IndexedAdd11 : IndexedOperation
        {
        public: int step(int value) { return (value + 11) % 1000000007; }
        }
        int indexedVirtualParams(IndexedOperation a, IndexedOperation b,
            IndexedOperation c, IndexedOperation d, int count)
        {
            array<int> order = {1, 1, 2, 0, 2, 2, 3};
            IndexedOperation current = a;
            int state = 9601;
            int total = 0;
            int i = 0;
            while (i < count)
            {
                state = (state * 469 + 1242) % 14581;
                int selector = order[(state + (i % 7)) % 7];
                if (selector == 0) current = a;
                else if (selector == 1) current = b;
                else if (selector == 2) current = c;
                else current = d;
                total = current.step(total);
                i++;
            }
            return total + state;
        }
        void main()
        {
            IndexedOperation a = IndexedAdd3();
            IndexedOperation b = IndexedAdd5();
            IndexedOperation c = IndexedAdd7();
            IndexedOperation d = IndexedAdd11();
            print(indexedVirtualParams(a, b, c, d, 100));
        }
        """, "csharp-cost-model-fallback-stats.vs");
	Expect(indexedFallback.Result.Success && indexedFallback.Output.SequenceEqual(["11878"]), "managed indexed virtual-parameter fallback parity");
	Expect(indexedFallback.Stats.CostModelVmFallbacks > 0 && indexedFallback.Stats.CostModelVmFallbacks <= indexedFallback.Stats.Fallbacks, "managed cost-model VM fallback statistics");
}

static void RunControlledMixedPolicySuite()
{
	using var session = CreateTestSession();
	VeroScriptJitBackendInfo backend = session.GetJitBackendInfo();
	bool supportsControlled = backend.NativeCode && backend.CapabilityFlags.HasFlag(VeroScriptJitBackendCapabilities.ControlledExecution);
	session.ApplyConfiguration(new VeroScriptConfiguration { ExecutionBackend = VeroScriptExecutionBackend.Mixed, MaxInstructions = 10000 });
	Expect(session.LoadSource("int work(int n){return n+1;}int main(){return work(6);}", "controlled-csharp.vs").Success, "controlled managed source load");
	foreach (bool enabled in new[]
	{
		false,
		true,
		false
	}

	)
	{
		session.SetJitPolicy(new VeroScriptJitPolicy(1, 128, 128, 128) { EnableControlledMixed = enabled });
		var result = session.Run();
		Expect(result.Success && result.ExitCode == 7, "controlled managed execution");
		var stats = session.GetLastJitStats();
		Expect(stats.AotExecutions == 0 && ((enabled && supportsControlled) || stats.ControlledNativeExecutions == 0), "controlled Mixed preserves fallback for unsupported or VM-preferred entries");
	}
}

static void RunGovernedJitCacheSuite()
{
	foreach (VeroScriptExecutionBackend backend in new[]
	{
		VeroScriptExecutionBackend.Jit,
		VeroScriptExecutionBackend.Mixed
	}

	)
	{
		using var session = CreateTestSession();
		VeroScriptJitBackendInfo info = session.GetJitBackendInfo();
		if (!info.NativeCode || !info.CapabilityFlags.HasFlag(VeroScriptJitBackendCapabilities.ControlledExecution))
		{
			return;
		}

		session.ApplyConfiguration(new VeroScriptConfiguration { ExecutionBackend = backend, MaxInstructions = 10000 });
		Expect(session.LoadSource("int main(){return 42;}", "governed-csharp-cache.vs").Success, "governed managed cache source load");
		foreach (bool unlimited in new[]
		{
			false,
			true
		}

		)
		{
			ulong budget = unlimited ? 0UL : 1UL;
			session.SetJitPolicy(new VeroScriptJitPolicy(1, budget, budget, 1) { EnableControlledMixed = backend == VeroScriptExecutionBackend.Mixed });
			VeroScriptRunResult cold = session.Run();
			VeroScriptJitCacheStats coldCache = session.GetJitCacheStats();
			Expect(cold.Success && cold.ExitCode == 42 && session.GetLastJitStats().ControlledNativeExecutions > 0 && coldCache.CompileAttempts == 1 && coldCache.RetainedPayloadBytes > 0 && !coldCache.ResourceLimitReached, $"governed {backend} cold call retains code with unlimited={unlimited}");
			VeroScriptRunResult warm = session.Run();
			VeroScriptJitStats warmRun = session.GetLastJitStats();
			VeroScriptJitCacheStats warmCache = session.GetJitCacheStats();
			Expect(warm.Success && warm.ExitCode == 42 && warmRun.ControlledNativeExecutions > 0 && warmRun.CompileAttempts == 0 && warmCache.CompileAttempts == coldCache.CompileAttempts && warmCache.RetainedPayloadBytes > 0 && !warmCache.ResourceLimitReached, $"governed {backend} warm call preserves governance and avoids recompilation with unlimited={unlimited}");
			var(cachedValue, cachedCall) = session.CallFunction("main");
			using (cachedValue)
			{
				Expect(cachedCall.Success && cachedValue.GetInt32() == 42, $"governed {backend} cached CallFunction returns its value with unlimited={unlimited}");
			}

			VeroScriptJitStats callStats = session.GetLastJitStats();
			VeroScriptJitCacheStats callCache = session.GetJitCacheStats();
			Expect(callStats.ControlledNativeExecutions > 0 && callStats.CompileAttempts == 0 && callCache.CompileAttempts == coldCache.CompileAttempts && callCache.RetainedPayloadBytes > 0 && !callCache.ResourceLimitReached, $"governed {backend} CallFunction reuses code warmed by Run with unlimited={unlimited}");
		}

		session.ApplyConfiguration(new VeroScriptConfiguration { ExecutionBackend = backend, MaxInstructions = 1 });
		Expect(!session.Run().Success && session.GetDiagnostics().Any(diagnostic => diagnostic.MessageId == "diag.runtime_execution_instruction_limit_exceeded"), $"governed {backend} cache honors a replacement instruction limit");
		session.ApplyConfiguration(new VeroScriptConfiguration { ExecutionBackend = backend, MaxInstructions = 10000 });
		VeroScriptRunResult recovered = session.Run();
		Expect(recovered.Success && recovered.ExitCode == 42 && session.GetLastJitStats().ControlledNativeExecutions > 0, $"governed {backend} execution recovers after restoring the instruction limit");
		if (backend == VeroScriptExecutionBackend.Mixed)
		{
			session.SetJitPolicy(new VeroScriptJitPolicy(1, 1, 1, 1));
			VeroScriptRunResult optedOut = session.Run();
			VeroScriptJitStats optedOutStats = session.GetLastJitStats();
			Expect(optedOut.Success && optedOut.ExitCode == 42 && optedOutStats.Dispatches == 0 && optedOutStats.ControlledNativeExecutions == 0 && session.GetJitCacheStats().RetainedPayloadBytes == 0, "governed Mixed opt-out discards previously warmed native code");
		}
	}
}

static void RunSessionJitCacheSuite()
{
	RunGovernedJitCacheSuite();
	var defaultPolicy = new VeroScriptJitPolicy(1, 128, 128, 128);
	Expect(defaultPolicy.ReuseSessionCode, "four-argument managed JIT policy enables session-code reuse by default");
	Expect(!defaultPolicy.EnableControlledMixed, "managed governed Mixed execution defaults to the VM");
	using var session = CreateTestSession();
	session.ApplyConfiguration(new VeroScriptConfiguration { ExecutionBackend = VeroScriptExecutionBackend.Jit });
	session.SetJitPolicy(defaultPolicy);
	Expect(session.GetJitCacheLimits() == new VeroScriptJitCacheLimits(8UL * 1024 * 1024, 32UL * 1024 * 1024), "managed default session JIT cache limits");
	var customLimits = new VeroScriptJitCacheLimits(4UL * 1024 * 1024, 16UL * 1024 * 1024);
	session.SetJitCacheLimits(customLimits);
	Expect(session.GetJitCacheLimits() == customLimits, "managed session JIT cache limit round trip");
	VeroScriptRunResult loaded = session.LoadSource("""
        int state = 7;
        int next() { state++; return state; }
        int add(int a, int b) { return a + b; }
        void main() {}
        """, "csharp-session-cache.vs");
	Expect(loaded.Success, "managed session JIT cache source load: " + string.Join(" | ", session.GetDiagnostics().Select(diagnostic => diagnostic.Message)));
	var(coldValue, coldCall) = session.CallFunction("next");
	using (coldValue)
	{
		Expect(coldCall.Success && coldValue.GetInt32() == 8, "managed cold session-cache call");
	}

	VeroScriptJitStats coldRun = session.GetLastJitStats();
	VeroScriptJitCacheStats coldCache = session.GetJitCacheStats();
	Expect(coldRun.CompileAttempts > 0 && coldCache.CompileAttempts > 0 && coldCache.RetainedPayloadBytes > 0, "managed cold call retains compiled session state");
	var(warmValue, warmCall) = session.CallFunction("next");
	using (warmValue)
	{
		Expect(warmCall.Success && warmValue.GetInt32() == 8, "managed warm cache call keeps fresh VM state");
	}

	VeroScriptJitStats warmRun = session.GetLastJitStats();
	VeroScriptJitCacheStats warmCache = session.GetJitCacheStats();
	Expect(warmRun.CompileAttempts == 0 && warmCache.CompileAttempts == coldCache.CompileAttempts && warmCache.RetainedPayloadBytes > 0, "managed warm session-code reuse avoids recompilation");
	session.SetJitPolicy(defaultPolicy with { ReuseSessionCode = false });
	for (int invocation = 0; invocation < 2; ++invocation)
	{
		var(uncachedValue, uncachedCall) = session.CallFunction("next");
		using (uncachedValue)
		{
			Expect(uncachedCall.Success && uncachedValue.GetInt32() == 8, "managed uncached call keeps fresh VM state");
		}

		Expect(session.GetLastJitStats().CompileAttempts > 0, "managed session-code reuse opt-out recompiles");
	}

	VeroScriptJitCacheStats disabledCache = session.GetJitCacheStats();
	Expect(disabledCache.CompileAttempts == 0 && disabledCache.RetainedPayloadBytes == 0 && disabledCache.NativeBytes == 0, "managed disabled cache retains no resources");
	session.SetJitPolicy(defaultPolicy);
	session.SetJitCacheLimits(new VeroScriptJitCacheLimits(4UL * 1024 * 1024, 1));
	var(limitedValue, limitedCall) = session.CallFunction("next");
	using (limitedValue)
	{
		Expect(limitedCall.Success && limitedValue.GetInt32() == 8, "managed resource-limited cache falls back safely");
	}

	VeroScriptJitCacheStats limitedCache = session.GetJitCacheStats();
	Expect(limitedCache.ResourceLimitReached && limitedCache.RetainedPayloadBytes == 0 && limitedCache.NativeBytes == 0, "managed session JIT cache resource-limit diagnostics");
}

static void RunStandardLibraryConcurrencySuite()
{
	string? standardLibrary = Environment.GetEnvironmentVariable("VEROSCRIPT_STDLIB_DIR");
	if (string.IsNullOrWhiteSpace(standardLibrary))
	{
		return;
	}

	Expect(Directory.Exists(standardLibrary), "configured standard-library directory");
	var standardLibraryOutput = new ConcurrentQueue<string>();
	using (var standardLibrarySession = CreateTestSession(new VeroScriptSessionOptions { Print = standardLibraryOutput.Enqueue }))
	{
		standardLibrarySession.ApplyConfiguration(new VeroScriptConfiguration { ExecutionBackend = VeroScriptExecutionBackend.Vm, StandardLibrarySearchPaths = [standardLibrary] });
		VeroScriptRunResult standardLibraryLoaded = standardLibrarySession.LoadSource("""
            import "algorithm.vs";
            import "random.vs";
            import "delegate.vs";
            import "path.vs";

            void main()
            {
                array<int> values = {3, 1, 2};
                std::algorithm::sort(values);
                array<int> copied = std::algorithm::copy(values);
                bool ok = std::algorithm::equal(values, copied)
                    && std::algorithm::contains(copied, 2)
                    && std::algorithm::is_sorted(copied)
                    && std::algorithm::min_index(copied) == 0
                    && std::algorithm::max_index(copied) == 2;
                std::algorithm::rotate(copied, 1);
                ok = ok && copied[0] == 2 && copied[2] == 1;
                std::algorithm::fill(copied, 4);
                ok = ok && copied[0] == 4 && copied[2] == 4;

                array<unsigned int> engine = std::random::make_engine(123u);
                int random_int = std::random::next_int(engine, -10, 10);
                double random_double = std::random::next_double_range(engine, 2.0, 3.0);
                array<int> shuffled = {1, 2, 3, 4};
                std::algorithm::shuffle(shuffled, engine);
                std::algorithm::sort(shuffled);
                array<int> expected = {1, 2, 3, 4};
                ok = ok && random_int >= -10 && random_int < 10
                    && random_double >= 2.0 && random_double < 3.0
                    && std::algorithm::equal(shuffled, expected);
                delegate<int> handler = delegate<int>();
                handler.bind(fn() -> int => 7);
                ok = ok && handler.is_bound() && handler.execute() == 7;
                handler.unbind();
                ok = ok && !handler.is_bound() && !handler.execute_if_bound();
                handler.bind(fn() -> int => 11);
                ok = ok && handler.execute() == 11;
                ok = ok && std::filesystem::join_path("left", "/root") == "/root"
                    && std::filesystem::join_path("C:", "relative") == "C:relative";
                print(ok ? 1 : 0);
            }
            """, "csharp-standard-library.vs");
		Expect(standardLibraryLoaded.Success, "standard-library source load: " + string.Join(" | ", standardLibrarySession.GetDiagnostics().Select(diagnostic => diagnostic.Message)));
		VeroScriptRunResult standardLibraryRun = standardLibrarySession.Run();
		Expect(standardLibraryRun.Success, "standard-library execution: " + string.Join(" | ", standardLibrarySession.GetDiagnostics().Select(diagnostic => diagnostic.Message)));
	}

	Expect(standardLibraryOutput.SequenceEqual(["1"]), "standard-library output");
	foreach ((bool allowEnvironmentWrite, bool allowDirectoryChange)in new[]
	{
		(true, false),
		(false, true),
		(true, true)
	}

	)
	{
		using var capabilitySession = CreateTestSession();
		capabilitySession.ApplyConfiguration(new VeroScriptConfiguration { ExecutionBackend = VeroScriptExecutionBackend.Jit, StandardLibrarySearchPaths = [standardLibrary], CapabilityPolicyEnabled = true, AllowedScriptCapabilities = (allowEnvironmentWrite ? VeroScriptScriptCapabilities.EnvironmentWrite : VeroScriptScriptCapabilities.None) | (allowDirectoryChange ? VeroScriptScriptCapabilities.ProcessDirectoryChange : VeroScriptScriptCapabilities.None) });
		Expect(capabilitySession.LoadSource("""
            import "io.vs";
            void main() { std::filesystem::chdir("."); }
            """, "csharp-directory-capability.vs").Success, "directory capability source load");
		VeroScriptRunResult capabilityRun = capabilitySession.Run();
		Expect(capabilityRun.Success == (allowEnvironmentWrite && allowDirectoryChange), "process directory change requires both capabilities");
		if (!allowEnvironmentWrite || !allowDirectoryChange)
		{
			Expect(capabilitySession.GetDiagnostics().Any(diagnostic => diagnostic.Message.Contains(allowEnvironmentWrite ? "process directory change" : "environment write", StringComparison.Ordinal)), "denied directory change reports the missing capability");
		}
	}

	int ownerThread = Environment.CurrentManagedThreadId;
	int callbackCalls = 0;
	int callbackRanOffOwner = 0;
	var concurrentOutput = new ConcurrentQueue<string>();
	using var session = CreateTestSession(new VeroScriptSessionOptions { Print = concurrentOutput.Enqueue });
	session.ApplyConfiguration(new VeroScriptConfiguration { ExecutionBackend = VeroScriptExecutionBackend.Vm, StandardLibrarySearchPaths = [standardLibrary] });
	session.RegisterFunction("host_worker_probe", VeroScriptNativeType.Void, [], (_, _) =>
	{
		if (Environment.CurrentManagedThreadId != ownerThread)
		{
			Volatile.Write(ref callbackRanOffOwner, 1);
		}

		Interlocked.Increment(ref callbackCalls);
	});
	VeroScriptRunResult loaded = session.LoadSource("""
        import "algorithm.vs";
        import "random.vs";
        import "concurrent.vs";

        export void csharp_worker(channel<int> output)
        {
            host_worker_probe();
            output.send(42);
        }

        void main()
        {
            task<int> work = std::concurrent::spawn_task<int>("csharp_worker");
            print(work.await());
        }
        """, "csharp-concurrency.vs");
	Expect(loaded.Success, "concurrency source load: " + string.Join(" | ", session.GetDiagnostics().Select(diagnostic => diagnostic.Message)));
	VeroScriptRunResult run = session.Run();
	Expect(run.Success, "concurrency execution: " + string.Join(" | ", session.GetDiagnostics().Select(diagnostic => diagnostic.Message)));
	Expect(concurrentOutput.SequenceEqual(["42"]), "concurrency output");
	Expect(Volatile.Read(ref callbackCalls) == 1 && Volatile.Read(ref callbackRanOffOwner) == 1, "managed host callback executes safely on a VM worker thread");
	var callableOutput = new ConcurrentQueue<string>();
	using var callableSession = CreateTestSession(new VeroScriptSessionOptions { Print = callableOutput.Enqueue });
	callableSession.ApplyConfiguration(new VeroScriptConfiguration { ExecutionBackend = VeroScriptExecutionBackend.Mixed, StandardLibrarySearchPaths = [standardLibrary] });
	VeroScriptRunResult callableLoaded = callableSession.LoadSource("""
        import "concurrent.vs";
        int twice(int value) { return value * 2; }
        void main()
        {
            int factor = 3;
            task<int> first = std::concurrent::spawn_task<int>(twice, 21);
            task<int> second = std::concurrent::spawn_task<int>(
                [factor](int value) -> int => value * factor, 7);
            print(first.await());
            print(second.await());
        }
        """, "csharp-callable-tasks.vs");
	Expect(callableLoaded.Success, "callable task source load: " + string.Join(" | ", callableSession.GetDiagnostics().Select(diagnostic => diagnostic.Message)));
	VeroScriptRunResult callableRun = callableSession.Run();
	Expect(callableRun.Success, "callable task execution: " + string.Join(" | ", callableSession.GetDiagnostics().Select(diagnostic => diagnostic.Message)));
	Expect(callableOutput.SequenceEqual(["42", "21"]), "managed callable task results");
}

static void RunExecutableReflectionSuite()
{
	using var session = CreateTestSession();
	session.ApplyConfiguration(new VeroScriptConfiguration { ExecutionBackend = VeroScriptExecutionBackend.Mixed });
	VeroScriptRunResult loaded = session.LoadSource("""
        class Box
        {
        public:
            mutable int value;
            Box() { value = 1; }
            int add(int amount) { return value + amount; }
        }
        Box item = Box();
        field_info<Box> selected_field = field_info<Box>(type<Box>().qualified_name, 0);
        int change(int next)
        {
            selected_field.set<int>(item, next);
            return selected_field.get<int>(item);
        }
        int invoke(int amount)
        {
            type<Box> info = type<Box>();
            for (int i = 0; i < info.method_count(); i++)
            {
                method_info<Box> candidate = method_info<Box>(info.qualified_name, i);
                if (candidate.name() == "add")
                    return candidate.invoke<int>(item, amount);
            }
            return -1;
        }
        void main() {}
        """, "csharp-executable-reflection.vs");
	Expect(loaded.Success, "executable reflection source load: " + string.Join(" | ", session.GetDiagnostics().Select(diagnostic => diagnostic.Message)));
	using var argument = new VeroScriptValue();
	argument.Set(19);
	var(fieldValue, fieldCall) = session.CallFunction("change", argument);
	using (fieldValue)
	{
		Expect(fieldCall.Success && fieldValue.GetInt32() == 19, "managed executable reflection field get/set");
	}

	argument.Set(3);
	var(methodValue, methodCall) = session.CallFunction("invoke", argument);
	using (methodValue)
	{
		Expect(methodCall.Success && methodValue.GetInt32() == 4, "managed executable reflection method invocation");
	}
}

string[] expectedImports = File.ReadAllLines(Path.Combine(AppContext.BaseDirectory, "VeroScriptInterop.def")).Select(line => line.Trim()).Where(line => line.StartsWith("vs_interop_", StringComparison.Ordinal)).Order(StringComparer.Ordinal).ToArray();
string[] actualImports = typeof(NativeMethods).GetMethods(BindingFlags.NonPublic | BindingFlags.Static).Where(method => method.GetCustomAttribute<DllImportAttribute>()is not null).Select(method => method.GetCustomAttribute<DllImportAttribute>()!.EntryPoint ?? method.Name).Order(StringComparer.Ordinal).ToArray();
Expect(expectedImports.Length == expectedImports.Distinct(StringComparer.Ordinal).Count(), "unique native export manifest entries");
Expect(actualImports.Length == actualImports.Distinct(StringComparer.Ordinal).Count(), "unique managed native declarations");
Expect(expectedImports.SequenceEqual(actualImports, StringComparer.Ordinal), "complete managed native declaration surface");
ExpectNativeSize<NativeJitBackendInfo>(56, 56);
ExpectNativeSize<NativeStringView>(16, 16);
ExpectNativeSize<NativeByteView>(16, 16);
ExpectNativeSize<NativePackageMetadata>(32, 32);
ExpectNativeSize<NativePackageBuildOptions>(56, 56);
ExpectNativeSize<NativeRunResult>(16, 16);
ExpectNativeSize<NativeReloadStatus>(48, 48);
ExpectNativeSize<NativeDiagnostic>(64, 64);
ExpectNativeSize<NativeSessionOptions>(56, 28);
ExpectNativeSize<NativeConsoleInfo>(16, 16);
ConsoleAbiContractTests.Run();
ExpectNativeSize<NativeStringList>(16, 16);
ExpectNativeSize<NativePredefinedMacro>(80, 80);
ExpectNativeSize<NativeSourceOverlay>(32, 32);
ExpectNativeSize<NativeConfig>(176, 176);
ExpectNativeSize<NativeJitPolicy>(40, 40);
ExpectNativeSize<NativeJitCacheLimits>(24, 24);
ExpectNativeSize<NativeJitCacheStats>(40, 40);
ExpectNativeSize<NativeJitStats>(400, 400);
ExpectNativeSize<NativeAotFunction>(24, 24);
ExpectNativeSize<NativeAotDescription>(88, 88);
ExpectNativeSize<NativeVmBackendInfo>(80, 80);
ExpectNativeSize<NativeTypeDescriptor>(40, 40);
ExpectNativeSize<NativeParameterDescriptor>(56, 56);
ExpectNativeSize<NativeAnnotationArgument>(40, 40);
ExpectNativeSize<NativeAnnotation>(40, 40);
ExpectNativeSize<NativeAnnotationList>(16, 16);
ExpectNativeSize<NativeAnnotationArgumentDescriptor>(56, 56);
ExpectNativeSize<NativeAnnotationDescriptor>(80, 80);
ExpectNativeSize<NativeHostFunctionDescriptor>(232, 224);
ExpectNativeSize<NativeHostTypeDescriptor>(88, 88);
ExpectNativeSize<NativeHostMemberDescriptor>(208, 200);
ExpectNativeSize<NativeHostTypeProtocol>(72, 36);
ExpectNativeSize<NativeResolvedMethod>(120, 120);
ExpectNativeSize<NativeMutationEvent>(72, 64);
unsafe
{
	var malformedUtf8 = new NativeStringView(null, 1);
	ExpectThrows<VeroScriptException>(() => Utf8.Copy(malformedUtf8), "non-empty native UTF-8 view with a null pointer");
	byte invalidUtf8 = 0xff;
	malformedUtf8 = new NativeStringView(&invalidUtf8, 1);
	ExpectThrows<VeroScriptException>(() => Utf8.Copy(malformedUtf8), "invalid native UTF-8 data");
	ExpectThrows<VeroScriptException>(CopyInvalidUtf16, "invalid native UTF-16 data");
}

ExpectThrows<ArgumentException>(() =>
{
	using var _ = new Utf8("\ud800");
}, "invalid managed UTF-16 data");
ExpectThrows<VeroScriptException>(() => VeroScriptPackage.Read([0xff, 0x00, 0x01]), "malformed package rejection");
Expect(VeroScriptRuntime.AbiVersion == VeroScriptRuntime.SupportedAbiVersion, "ABI version");
CheckAotDescriptorVersion();
VmBackendContractTests.Run();
NativeLifetimeTests.Run();
LanguageStateContractTests.Run();
MemoryContractTests.Run();
VariantContractTests.Run();
ConsoleContractTests.Run();
RunModeContractTests.Run();
ReloadContractTests.Run();
CoreTypeContractTests.Run();
ProtocolLifetimeTests.Run();
AnnotationContractTests.Run();
NativeSdkContractTests.Run();
ScriptCompileExtensionTests.Run();
using (var memorySession = new VeroScriptSession())
{
	memorySession.SetMemoryLimits(new(64, 128));
	var memory = memorySession.Memory;
	using var bytes = memory.CreateBuffer(16);
	using var number = new VeroScriptValue();
	number.Set(ulong.MaxValue);
	memory.Write(bytes, 1, VeroScriptNativeTypeKind.UInt64, number, VeroScriptByteOrder.Big);
	using var decoded = memory.Read(bytes, 1, VeroScriptNativeTypeKind.UInt64, VeroScriptByteOrder.Big);
	Expect(decoded.GetUInt64() == ulong.MaxValue, "memory unaligned uint64 big endian");
	using var view = memory.Slice(bytes, 0, 8, true);
	ExpectThrows<VeroScriptException>(() => memory.Fill(view, 0, 0, 1), "readonly memory");
	using (var lease = memory.AcquireLease(bytes, true))
	{
		lease.WritableBytes[0] = 42;
		ExpectThrows<VeroScriptException>(() => memory.Resize(bytes, 17), "lease excludes resize");
	}

	using var snapshot = memory.ToArray(bytes);
	using var clone = memory.FromArray(snapshot);
	Expect(memory.ObjectId(bytes) != memory.ObjectId(clone), "memory clone identity");
	memory.Resize(bytes, 17);
	ExpectThrows<VeroScriptException>(() => memory.Size(view), "expired memory view");
	byte[] managed = [1, 2, 3];
	using var external = memory.WrapExternal(managed);
	memory.Fill(external, 1, 1, 9);
	Expect(managed[1] == 9 && memory.Stats.BorrowedBytes == 3, "pinned external memory");
	var retained = memory.AcquireLease(external);
	memorySession.Dispose();
	retained.Dispose();
}

Expect(new VeroScriptConfiguration().ExecutionBackend == VeroScriptExecutionBackend.Mixed, "managed configuration defaults to the mixed backend");
Expect(VeroScriptCapabilities.None == 0, "empty capability set");
Expect(VeroScriptScriptCapabilities.All == (VeroScriptScriptCapabilities)0x3FF, "all script capability bits include process directory change");
Expect((VeroScriptRuntime.Capabilities & VeroScriptCapabilities.ContainerEnumeration) != 0, "container enumeration capability");
Expect(VeroScriptRuntime.Capabilities.HasFlag(VeroScriptCapabilities.ExecutionGovernance), "execution governance capability");
Expect(VeroScriptRuntime.Capabilities.HasFlag(VeroScriptCapabilities.SessionJitCache), "session JIT cache capability");
Expect(new[] { VeroScriptNativeType.Int8.Kind, VeroScriptNativeType.UInt8.Kind, VeroScriptNativeType.Int16.Kind, VeroScriptNativeType.UInt16.Kind, VeroScriptNativeType.Int32.Kind, VeroScriptNativeType.UInt32.Kind, VeroScriptNativeType.Int64.Kind, VeroScriptNativeType.UInt64.Kind, VeroScriptNativeType.Float.Kind, VeroScriptNativeType.Double.Kind, VeroScriptNativeType.Bool.Kind, VeroScriptNativeType.String.Kind }.Distinct().Count() == 12, "canonical native primitive descriptors");
Expect(!string.IsNullOrWhiteSpace(VeroScriptRuntime.RuntimeVersion), "runtime version");
Expect(VeroScriptRuntime.Capabilities.HasFlag(VeroScriptCapabilities.HostBindings), "host binding capability");
Expect(VeroScriptRuntime.GetAvailableLanguagesJson().Contains("en-US"), "available languages");
using (var cancellationSession = CreateTestSession())
{
	cancellationSession.ApplyConfiguration(new VeroScriptConfiguration { ExecutionBackend = VeroScriptExecutionBackend.Vm, EnableExecutionControl = true });
	Expect(cancellationSession.LoadSource("void main() { while (true) {} }", "managed-cancel.vs").Success, "managed cancellation source load");
	cancellationSession.RequestCancellation();
	Expect(!cancellationSession.Run().Success, "managed request cancellation stops an execution-control-enabled session");
}

using (var cancellationSession = CreateTestSession())
{
	Exception? cancellationFailure = null;
	Task.Run(() =>
	{
		try
		{
			cancellationSession.RequestCancellation();
		}
		catch (Exception exception)
		{
			cancellationFailure = exception;
		}
	}).GetAwaiter().GetResult();
	Expect(cancellationFailure is VeroScriptException { Status: VeroScriptStatus.InvalidArgument }, "cross-thread cancellation preserves native failures instead of reporting WrongThread");
}

string? interopLibrary = Environment.GetEnvironmentVariable("VEROSCRIPT_INTEROP_LIBRARY");
if (!string.IsNullOrWhiteSpace(interopLibrary))
{
	string catalogDirectory = Path.Combine(Path.GetDirectoryName(interopLibrary)!, "localization");
	if (Directory.Exists(catalogDirectory))
	{
		VeroScriptRuntime.LoadCatalogDirectory(catalogDirectory);
		VeroScriptRuntime.MergeCatalogDirectory(catalogDirectory);
	}
}

string originalLanguage = VeroScriptRuntime.CurrentLanguage;
try
{
	foreach (string language in new[]
	{
		"en-US",
		"fr-FR",
		"ja-JP",
		"ko-KR",
		"zh-CN",
		"zh-TW"
	}

	)
	{
		VeroScriptRuntime.CurrentLanguage = language;
		const string message = "JIT backend loading failed";
		string translated = VeroScriptRuntime.Translate(message);
		Expect(!string.IsNullOrEmpty(translated), $"translated diagnostic in {language}");
		Expect(language == "en-US" ? translated == message : translated != message, $"selected translation catalog in {language}");
		Expect(VeroScriptRuntime.Translate("opaque\0message") == "opaque\0message", $"unknown diagnostic preserves embedded NUL in {language}");
		Expect(VeroScriptRuntime.Translate(string.Empty) == string.Empty, $"empty diagnostic in {language}");
	}

	ExpectThrows<ArgumentNullException>(() => VeroScriptRuntime.Translate(null!), "null diagnostic");
	ExpectThrows<ArgumentException>(() => VeroScriptRuntime.Translate("\uD800"), "invalid UTF-16 diagnostic");
}
finally
{
	VeroScriptRuntime.CurrentLanguage = originalLanguage;
}

int vmBackendValue = RunBackendParitySuite(VeroScriptExecutionBackend.Vm);
int mixedBackendValue = RunBackendParitySuite(VeroScriptExecutionBackend.Mixed);
RunControlledMixedPolicySuite();
if (!string.IsNullOrEmpty(Environment.GetEnvironmentVariable("VEROSCRIPT_TEST_JIT_BACKEND")))
{
	Expect(RunBackendParitySuite(VeroScriptExecutionBackend.Jit) == vmBackendValue, "JIT backend parity");
	RunJitDispatchStatsSuite();
	RunJitOptimizationStatsSuite();
	RunSessionJitCacheSuite();
}
else
{
	Console.WriteLine("SKIP: JIT statistics and cache suites require an explicitly selected test backend");
}

RunStandardLibraryConcurrencySuite();
RunExecutableReflectionSuite();
Expect(vmBackendValue == 240 && mixedBackendValue == vmBackendValue, "VM and Mixed backend parity");
using (var scalar = new VeroScriptValue())
{
	scalar.Set((sbyte)-12);
	Expect(scalar.Kind == VeroScriptValueKind.Int8 && scalar.GetInt8() == -12, "int8 round trip");
	scalar.Set((byte)250);
	Expect(scalar.Kind == VeroScriptValueKind.UInt8 && scalar.GetUInt8() == 250, "uint8 round trip");
	scalar.Set((short)-1234);
	Expect(scalar.Kind == VeroScriptValueKind.Int16 && scalar.GetInt16() == -1234, "int16 round trip");
	scalar.Set((ushort)65000);
	Expect(scalar.Kind == VeroScriptValueKind.UInt16 && scalar.GetUInt16() == 65000, "uint16 round trip");
	scalar.Set(42);
	Expect(scalar.Kind == VeroScriptValueKind.Int32 && scalar.GetInt32() == 42, "int32 round trip");
	scalar.Set(4_000_000_000u);
	Expect(scalar.Kind == VeroScriptValueKind.UInt32 && scalar.GetUInt32() == 4_000_000_000u, "uint32 round trip");
	scalar.Set(-5_000_000_000L);
	Expect(scalar.Kind == VeroScriptValueKind.Int64 && scalar.GetInt64() == -5_000_000_000L, "int64 round trip");
	scalar.Set(10_000_000_000UL);
	Expect(scalar.Kind == VeroScriptValueKind.UInt64 && scalar.GetUInt64() == 10_000_000_000UL, "uint64 round trip");
	scalar.Set(2.5f);
	Expect(scalar.Kind == VeroScriptValueKind.Float && scalar.GetFloat() == 2.5f, "float round trip");
	scalar.Set(3.25d);
	Expect(scalar.Kind == VeroScriptValueKind.Double && scalar.GetDouble() == 3.25d, "double round trip");
	scalar.Set(true);
	Expect(scalar.Kind == VeroScriptValueKind.Bool && scalar.GetBool(), "bool round trip");
	scalar.SetEnum("ManagedMode", 7);
	Expect(scalar.GetEnum() == new VeroScriptEnumValue("ManagedMode", 7), "enum round trip");
	using (VeroScriptValue clone = scalar.Clone())
	{
		Expect(clone.GetEnum() == scalar.GetEnum(), "value clone");
	}

	using (var assigned = new VeroScriptValue())
	{
		assigned.Set(scalar);
		Expect(assigned.GetEnum() == scalar.GetEnum(), "complete value assignment");
		assigned.Set(assigned);
		Expect(assigned.GetEnum() == scalar.GetEnum(), "self value assignment");
	}

	scalar.Reset();
	Expect(scalar.Kind == VeroScriptValueKind.Invalid, "value reset");
	scalar.SetString("你好，世界");
	Expect(scalar.GetString() == "你好，世界", "UTF-8 string round trip");
	scalar.SetString("a\0b");
	Expect(scalar.GetString() == "a\0b", "length-delimited string round trip");
	scalar.SetWideString("宽字符串测试");
	Expect(scalar.GetWideString() == "宽字符串测试", "UTF-16 string round trip");
	scalar.SetNull();
	Expect(scalar.Kind == VeroScriptValueKind.Null && scalar.IsNull, "null value round trip");
	ExpectThrows<ArgumentException>(() => scalar.SetWideString("\ud800"), "invalid managed UTF-16 wide string");
}

using (var first = new VeroScriptValue())
{
	using (var second = new VeroScriptValue())
	{
		using (var array = new VeroScriptValue())
		{
			using (var map = new VeroScriptValue())
			{
				using (var set = new VeroScriptValue())
				{
					using (var channel = new VeroScriptValue())
					{
						first.Set(20);
						second.Set(22);
						array.SetArray("int", [first, second]);
						using var item = array.GetSequenceElement(1);
						Expect(array.SequenceCount == 2 && item.GetInt32() == 22, "array round trip");
						using var queue = new VeroScriptValue();
						queue.SetSequence(VeroScriptValueKind.Queue, "int", [first, second]);
						using var queued = queue.GetSequenceElement(0);
						Expect(queue.SequenceCount == 2 && queued.GetInt32() == 20, "sequence round trip");
						map.SetMap("int", "int", [first, second], [second, first], ordered: true);
						using var mapped = new VeroScriptValue();
						Expect(map.MapCount == 2 && map.TryGetMapValue(first, mapped) && mapped.GetInt32() == 22, "map round trip");
						var mapEntries = new List<(int Key, int Value)>();
						VeroScriptBorrowedValue? enumeratedMapKey = null;
						map.EnumerateMap((key, value) =>
						{
							enumeratedMapKey = key;
							mapEntries.Add((key.GetInt32(), value.GetInt32()));
						});
						Expect(mapEntries.SequenceEqual([(20, 22), (22, 20)]), "ordered map snapshot enumeration");
						ExpectThrows<InvalidOperationException>(() => enumeratedMapKey!.GetInt32(), "expired map enumeration value");
						var mapVisitorException = new InvalidOperationException("Expected map visitor failure.");
						bool mapVisitorExceptionPropagated = false;
						try
						{
							map.EnumerateMap((_, _) => throw mapVisitorException);
						}
						catch (InvalidOperationException exception)
						{
							mapVisitorExceptionPropagated = ReferenceEquals(exception, mapVisitorException);
						}

						Expect(mapVisitorExceptionPropagated, "map visitor exception propagation");
						ExpectThrows<ArgumentNullException>(() => map.EnumerateMap(null!), "null map visitor validation");
						ExpectThrows<ArgumentException>(() => map.SetMap("int", "int", [first, second], [second]), "map snapshot length validation");
						using var multiMap = new VeroScriptValue();
						multiMap.SetMap(VeroScriptValueKind.MultiMap, "int", "int", [first, first], [second, first]);
						using var multiMapValues = new VeroScriptValue();
						Expect(multiMap.Kind == VeroScriptValueKind.MultiMap && multiMap.MapCount == 1 && multiMap.TryGetMapValue(first, multiMapValues) && multiMapValues.SequenceCount == 2, "multi-map ABI construction");
						using var multiSet = new VeroScriptValue();
						multiSet.SetSet(VeroScriptValueKind.OrderedMultiSet, "int", [first, second, second]);
						var multiSetValues = new List<int>();
						multiSet.EnumerateSet(value => multiSetValues.Add(value.GetInt32()));
						Expect(multiSet.Kind == VeroScriptValueKind.OrderedMultiSet && multiSet.SetCount == 3 && multiSet.SetContains(second) && multiSetValues.SequenceEqual([20, 22, 22]), "multi-set ABI inspection preserves multiplicity and order");
						using var priorityQueue = new VeroScriptValue();
						priorityQueue.SetSequence(VeroScriptValueKind.PriorityQueue, "int", [first, second]);
						Expect(priorityQueue.Kind == VeroScriptValueKind.PriorityQueue && priorityQueue.SequenceCount == 2, "priority queue ABI construction");
						var countState = new MutablePairCountState();
						map.SetMap("int", "int", new CoordinatedCountReadOnlyList<VeroScriptValue>(first, second, countState.GetKeyCount), new CoordinatedCountReadOnlyList<VeroScriptValue>(second, first, countState.GetValueCount));
						Expect(map.MapCount == 1 && map.TryGetMapValue(first, mapped) && mapped.GetInt32() == 22, "mutable map inputs are snapshotted independently");
						set.SetSet("int", [first, second, first]);
						Expect(set.SetCount == 2 && set.SetContains(second), "set round trip");
						var setElements = new HashSet<int>();
						VeroScriptBorrowedValue? enumeratedSetElement = null;
						set.EnumerateSet(element =>
						{
							enumeratedSetElement = element;
							setElements.Add(element.GetInt32());
						});
						Expect(setElements.SetEquals([20, 22]), "set snapshot enumeration");
						ExpectThrows<InvalidOperationException>(() => enumeratedSetElement!.GetInt32(), "expired set enumeration value");
						var setVisitorException = new InvalidOperationException("Expected set visitor failure.");
						bool setVisitorExceptionPropagated = false;
						try
						{
							set.EnumerateSet(_ => throw setVisitorException);
						}
						catch (InvalidOperationException exception)
						{
							setVisitorExceptionPropagated = ReferenceEquals(exception, setVisitorException);
						}

						Expect(setVisitorExceptionPropagated, "set visitor exception propagation");
						ExpectThrows<ArgumentNullException>(() => set.EnumerateSet(null!), "null set visitor validation");
						channel.SetChannel("int", 1);
						Expect(channel.ChannelCapacity == 1 && channel.ChannelCount == 0 && !channel.IsChannelClosed, "bounded channel metadata");
						Expect(channel.ChannelTrySend(first) && !channel.ChannelTrySend(second) && channel.ChannelCount == 1, "bounded channel non-blocking send");
						using var received = channel.ChannelTryReceive();
						Expect(received is not null && received.GetInt32() == 20 && channel.ChannelReceiveFor(0)is null, "channel non-blocking receive");
						Expect(channel.ChannelSendFor(second, 0), "channel timed send");
						using var secondReceived = channel.ChannelReceive();
						Expect(secondReceived.GetInt32() == 22, "channel blocking round trip");
						Task concurrentSend = Task.Run(() =>
						{
							using var sentFromWorker = new VeroScriptValue();
							sentFromWorker.Set(33);
							channel.ChannelSend(sentFromWorker);
						});
						using VeroScriptValue? receivedFromWorker = channel.ChannelReceiveFor(5_000);
						concurrentSend.GetAwaiter().GetResult();
						Expect(receivedFromWorker?.GetInt32() == 33, "channel operations across managed threads");
						channel.ChannelClose();
						Expect(channel.IsChannelClosed && !channel.ChannelTrySend(first), "closed channel state");
					}
				}
			}
		}
	}
}

ExpectSequenceAlias("int32", value => value.Set(-123456), value => value.GetInt32() == -123456);
ExpectSequenceAlias("unsigned", value => value.Set(4_000_000_001u), value => value.GetUInt32() == 4_000_000_001u);
ExpectSequenceAlias("uint32", value => value.Set(4_000_000_002u), value => value.GetUInt32() == 4_000_000_002u);
ExpectSequenceAlias("long", value => value.Set(-5_000_000_001L), value => value.GetInt64() == -5_000_000_001L);
ExpectSequenceAlias("unsigned long", value => value.Set(15_000_000_000_000_000_001UL), value => value.GetUInt64() == 15_000_000_000_000_000_001UL);
var output = new List<string>();
using var session = CreateTestSession(new VeroScriptSessionOptions { Print = output.Add });
Parallel.For(0, 512, _ =>
{
	GCHandle retained = GCHandle.Alloc(new object ());
	try
	{
		session.TrackCallback(retained);
	}
	catch
	{
		session.ReleaseTrackedCallback(retained);
		throw;
	}
});
session.SetInput("managed test input");
ExpectThrows<ArgumentNullException>(() => session.ApplyConfiguration(null!), "null configuration validation");
ExpectThrows<ArgumentNullException>(() => session.CallFunction("unused", (VeroScriptValue[])null!), "null function argument-list validation");
ExpectThrows<ArgumentNullException>(() => session.SetGlobal("unused", null!), "null global value validation");
ExpectThrows<ArgumentNullException>(() => session.RegisterHostFunction(new VeroScriptHostFunctionDefinition { Name = "invalid_callback", ReturnType = VeroScriptNativeType.Void, Callback = null! }), "null host callback validation");
ExpectThrows<ArgumentNullException>(() => session.RegisterHostFunction(new VeroScriptHostFunctionDefinition { Name = "invalid_parameters", ReturnType = VeroScriptNativeType.Void, Parameters = null!, Callback = (_, _) =>
{
} }), "null host parameter-list validation");
var cyclicArguments = new VeroScriptNativeType[1];
var cyclicType = new VeroScriptNativeType(VeroScriptNativeTypeKind.Array, TypeArguments: cyclicArguments);
cyclicArguments[0] = cyclicType;
ExpectThrows<ArgumentException>(() => session.RegisterFunction("invalid_cyclic_type", VeroScriptNativeType.Void, [new(cyclicType)], (_, _) =>
{
}), "cyclic native type validation");
ExpectThrows<ArgumentException>(() => session.ApplyConfiguration(new VeroScriptConfiguration { PredefinedMacros = [null!] }), "invalid predefined macro validation");
session.ApplyConfiguration(new VeroScriptConfiguration { ExecutionBackend = VeroScriptExecutionBackend.Vm, AllowCircularImports = true, ImportPaths = new ChangingCountReadOnlyList<string>("first", "second"), PredefinedMacros = [new("TWICE", "((value) + (value))", ["value"], "<csharp-test>")] });
session.SetJitPolicy(new VeroScriptJitPolicy(2, 64, 64, 8));
session.HotReloadEnabled = true;
VeroScriptBorrowedValue? capturedCallbackValue = null;
VeroScriptCallable? callbackDisposedCallable = null;
Task? callableDisposalTask = null;
bool callableDisposedInCallback = false;
session.RegisterFunction("host_add_one", VeroScriptNativeType.Int32, [new(VeroScriptNativeType.Int32)], (context, result) =>
{
	capturedCallbackValue = context.GetArgument(0);
	result.Set(capturedCallbackValue.GetInt32() + 1);
});
session.RegisterFunction("host_bump", VeroScriptNativeType.Void, [new(VeroScriptNativeType.Int32, VeroScriptParameterFlags.Ref)], (context, _) =>
{
	VeroScriptBorrowedValue value = context.GetMutableArgument(0);
	value.Set(value.GetInt32() + 10);
});
session.RegisterFunction("host_fail", VeroScriptNativeType.Void, [], (context, _) => context.SetError("Expected callback failure."));
session.RegisterFunction("host_throw_empty", VeroScriptNativeType.Void, [], (_, _) => throw new Exception(string.Empty));
session.RegisterFunction("host_throw_invalid_message", VeroScriptNativeType.Void, [], (_, _) => throw new Exception("\ud800"));
session.RegisterFunction("host_throw_message_getter", VeroScriptNativeType.Void, [], (_, _) => throw new ThrowingMessageException());
session.RegisterFunction("host_dispose_callable", VeroScriptNativeType.Void, [], (_, _) => callbackDisposedCallable!.Dispose());
session.RegisterFunction("host_dispose_callable_cross_thread", VeroScriptNativeType.Void, [], (_, _) =>
{
	callableDisposalTask = Task.Run(() => callbackDisposedCallable!.Dispose());
	callableDisposedInCallback = callableDisposalTask.Wait(TimeSpan.FromSeconds(5));
});
session.RegisterFunction("host_nested", VeroScriptNativeType.Int32, [new(VeroScriptNativeType.Int32)], (context, result) =>
{
	using VeroScriptValue argumentCopy = context.GetArgument(0).Clone();
	using VeroScriptValue nested = context.InvokeFunction("Double", argumentCopy);
	result.Set(nested.GetInt32());
});
session.RegisterHostFunction(new VeroScriptHostFunctionDefinition { Name = "host_invoke", ReturnType = VeroScriptNativeType.Int32, Parameters = [new(new VeroScriptNativeType(VeroScriptNativeTypeKind.Function, "fn(int)->int")), new(VeroScriptNativeType.Int32)], Callback = (context, result) =>
{
	using VeroScriptValue argumentCopy = context.GetArgument(1).Clone();
	using VeroScriptValue nested = context.InvokeCallable(context.GetArgument(0), argumentCopy);
	result.Set(nested.GetInt32());
} });
session.RegisterAnnotation(new VeroScriptAnnotationDefinition("host_visible", "annotation (Category)", "Marks a declaration visible to the host", [new("Category", "string", "Tooling category")]) { CaseSensitive = false });
var hostVisibleAnnotation = new VeroScriptAnnotation("host_visible", [new("Category", "Managed")]);
session.RegisterHostFunction(new VeroScriptHostFunctionDefinition { Name = "structured_host", ReturnType = VeroScriptNativeType.Int32, Parameters = [new(VeroScriptNativeType.Int32)], Callback = (_, result) => result.Set(42), Documentation = "Structured managed host function", ParameterNames = ["seed"], ParameterDocumentation = ["Ignored test seed"], ReturnDocumentation = "The answer", Annotations = [hostVisibleAnnotation], ParameterAnnotations = [[hostVisibleAnnotation]] });
uint hostTypeId = session.RegisterHostType(new VeroScriptHostTypeDefinition { Name = "ManagedVisible", Kind = VeroScriptHostTypeKind.Class, Documentation = "Visible managed host class", Annotations = [hostVisibleAnnotation] });
uint hostEnumTypeId = session.RegisterHostType(new VeroScriptHostTypeDefinition { Name = "ManagedMode", Kind = VeroScriptHostTypeKind.Enum, ScopedEnum = true, EnumUnderlyingTypeName = "int64" });
uint hostStructTypeId = session.RegisterHostType(new VeroScriptHostTypeDefinition { Name = "ManagedValue", Kind = VeroScriptHostTypeKind.Struct });
session.RegisterHostMember(hostEnumTypeId, new VeroScriptHostMemberDefinition { Kind = VeroScriptHostMemberKind.EnumMember, Name = "Active", EnumValue = 1 });
session.RegisterHostProtocol(hostStructTypeId, new VeroScriptHostTypeProtocolDefinition { HashCallback = (_, result) => result.Set(1) });
session.RegisterTypeAlias("ManagedInt", [], "int", "Managed integer alias");
uint boxTemplateId = session.RegisterGenericHostTypeTemplate("ManagedBox", 1, "Managed generic box", context =>
{
	Expect(context.ArgumentCount == 1 && context.GetArgument(0) == "int", "generic type context argument");
	context.SetType(new VeroScriptHostTypeDefinition { Name = "ManagedBox", Kind = VeroScriptHostTypeKind.Struct, Documentation = "Concrete managed generic box" });
	context.AddMember(new VeroScriptHostMemberDefinition { Kind = VeroScriptHostMemberKind.StaticMethod, Name = "Answer", ReturnType = VeroScriptNativeType.Int32, Callback = (_, result) => result.Set(42) });
	context.SetProtocol(new VeroScriptHostTypeProtocolDefinition { HashCallback = (_, result) => result.Set(42) });
});
uint managedBoxTypeId = session.InstantiateGenericHostType(boxTemplateId, ["int"]);
Expect(managedBoxTypeId != 0, "generic host type instantiation");
WeakReference<object>? rejectedMemberPayload = null;
WeakReference<object>? rejectedProtocolPayload = null;
uint rejectedTemplateId = session.RegisterGenericHostTypeTemplate("RejectedManagedBox", 1, "Rejected managed generic box", context =>
{
	context.SetType(new VeroScriptHostTypeDefinition { Name = "RejectedManagedBox", Kind = VeroScriptHostTypeKind.Struct });
	object memberPayload = new();
	rejectedMemberPayload = new(memberPayload);
	context.AddMember(new VeroScriptHostMemberDefinition { Kind = VeroScriptHostMemberKind.StaticMethod, Name = "RejectedAnswer", ReturnType = VeroScriptNativeType.Int32, Callback = (_, result) =>
	{
		GC.KeepAlive(memberPayload);
		result.Set(42);
	} });
	object protocolPayload = new();
	rejectedProtocolPayload = new(protocolPayload);
	context.SetProtocol(new VeroScriptHostTypeProtocolDefinition { HashCallback = (_, result) =>
	{
		GC.KeepAlive(protocolPayload);
		result.Set(42UL);
	} });
	context.SetError("reject after retaining managed callbacks");
});
ExpectThrows<VeroScriptException>(() => session.InstantiateGenericHostType(rejectedTemplateId, ["int"]), "rejected generic host type instantiation");
for (int collection = 0; collection < 3; ++collection)
{
	GC.Collect();
	GC.WaitForPendingFinalizers();
}

Expect(rejectedMemberPayload is not null && !rejectedMemberPayload.TryGetTarget(out _) && rejectedProtocolPayload is not null && !rejectedProtocolPayload.TryGetTarget(out _), "rejected generic instantiation releases member and protocol callback roots");
int mutationCount = 0;
session.SetMutationHandler(_ => Interlocked.Increment(ref mutationCount));
session.RegisterExternalSerializer("Counter", 3, context =>
{
	VeroScriptSerializedField field = context.GetField(0);
	Span<byte> bytes = stackalloc byte[4];
	System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(bytes, field.Value.GetInt32());
	context.SetBytes(bytes);
}, context =>
{
	byte[] bytes = context.GetBytes();
	if (context.EncodedVersion != 3 || bytes.Length != 4)
	{
		context.SetError("Unsupported serialized Counter payload.");
		return;
	}

	using var restored = new VeroScriptValue();
	restored.Set(System.Buffers.Binary.BinaryPrimitives.ReadInt32LittleEndian(bytes));
	context.AddField("value", restored);
});
using (var hostObjectValue = new VeroScriptValue())
{
	var managedPayload = new object ();
	hostObjectValue.SetHostObject(hostTypeId, "ManagedVisible", managedPayload);
	VeroScriptHostObjectInfo hostObject = hostObjectValue.GetHostObject();
	Expect(hostObject.TypeId == hostTypeId && ReferenceEquals(hostObject.Value, managedPayload), "managed host object round trip");
}

const string source = """
    int GlobalValue = 7;
    int Double(int value) { return TWICE(value); }
    ManagedInt AliasDouble(ManagedInt value) { return value * 2; }
    int UseHost(int value) { return host_add_one(value); }
    int UseHostRef(int value) { host_bump(ref value); return value; }
    void UseHostFailure() { host_fail(); }
    void UseEmptyException() { host_throw_empty(); }
    void UseInvalidMessageException() { host_throw_invalid_message(); }
    void UseThrowingMessageException() { host_throw_message_getter(); }
    int UseNestedHost(int value) { return host_nested(value); }
    int UseHostCallable(int value) { return host_invoke(Double, value); }
    int UseStructuredHost() { return structured_host(0); }
    HOST_VISIBLE() class AnnotationNameProbe {};
    bool CheckAnnotationNamePolicy() { return type<AnnotationNameProbe>().has_annotation("HOST_VISIBLE")
        && type<AnnotationNameProbe>().annotation(0).definition().name()=="host_visible"; }
    function(int)->int GetCallable() { return Double; }
    int InvokeAndDispose(int value) { host_dispose_callable(); return value * 2; }
    function(int)->int GetDisposeCallable() { return InvokeAndDispose; }
    int InvokeAndDisposeCrossThread(int value) { host_dispose_callable_cross_thread(); return value * 2; }
    function(int)->int GetCrossThreadDisposeCallable() { return InvokeAndDisposeCrossThread; }
    class Counter {
    public:
        int value;
        Counter(int initial) { value = initial; }
        int Add(int amount) { value += amount; return value; }
    };
    class CounterHolder {
    public:
        Counter reference = nullptr;
    };
    struct ConstPayload {
        int value;
        ConstPayload(int initial) { value = initial; }
    };
    class ConstHolder {
    public:
        const int id;
        const ConstPayload payload;
        ConstHolder(int initial) : payload(initial + 1), id(initial) {}
        int Total() const { return id + payload.value; }
    };
    void main() { print(UseHost(4)); }
    """;
VeroScriptRunResult loaded = session.LoadSource(source, "csharp-test.vs");
Expect(loaded.Success && session.HasFunction("Double", 1), "source load");
using var argument = new VeroScriptValue();
argument.Set(21);
var(doubleValue, doubleCall) = session.CallFunction("Double", argument);
using (doubleValue)
{
	Expect(doubleCall.Success && doubleValue.GetInt32() == 42, "script call");
}

var(hostValue, hostCall) = session.CallFunction("UseHost", argument);
using (hostValue)
{
	Expect(hostCall.Success && hostValue.GetInt32() == 22, "managed callback");
}

var(nestedValue, nestedCall) = session.CallFunction("UseNestedHost", argument);
using (nestedValue)
{
	Expect(nestedCall.Success && nestedValue.GetInt32() == 42, "nested callback function invocation");
}

var(nestedCallableValue, nestedCallableCall) = session.CallFunction("UseHostCallable", argument);
using (nestedCallableValue)
{
	Expect(nestedCallableCall.Success && nestedCallableValue.GetInt32() == 42, "nested callback callable invocation");
}

Expect(capturedCallbackValue is not null, "borrowed callback value captured for lifetime test");
VeroScriptBorrowedValue captured = capturedCallbackValue!;
try
{
	_ = captured.GetInt32();
	throw new InvalidOperationException("FAILED: expired borrowed value was accepted");
}
catch (InvalidOperationException exception)when (exception.Message.Contains("no longer valid"))
{
}

var(refValue, refCall) = session.CallFunction("UseHostRef", argument);
using (refValue)
{
	Expect(refCall.Success && refValue.GetInt32() == 31, "managed ref callback");
}

argument.Set(7);
var(constHolder, constHolderConstructed) = session.Construct("ConstHolder", argument);
using (constHolder)
{
	Expect(constHolderConstructed.Success, "managed construction with direct const-field initializers");
	var(idValue, idRead) = session.GetField(constHolder, "id");
	using (idValue)
	{
		Expect(idRead.Success && idValue.GetInt32() == 7, "managed read of constructor-initialized const scalar field");
	}

	var(totalValue, totalCall) = session.CallMethod(constHolder, "Total");
	using (totalValue)
	{
		Expect(totalCall.Success && totalValue.GetInt32() == 15, "managed call observes constructor-initialized const object field");
	}
}

argument.Set(5);
var(counter, constructed) = session.Construct("Counter", argument);
using (counter)
{
	Expect(constructed.Success && session.HasMethod(counter, "Add", 1), "object construction");
	int interceptedCalls = 0;
	session.SetInstanceCallInterceptor((context, result) =>
	{
		if (context.GetTarget().MethodName != "Add")
		{
			return VeroScriptInterceptResult.Unhandled;
		}

		Expect(context.Receiver.Kind == VeroScriptValueKind.Object && context.ArgumentCount == 1, "interceptor receiver and argument metadata");
		Interlocked.Increment(ref interceptedCalls);
		context.Proceed(result);
		return VeroScriptInterceptResult.Handled;
	});
	argument.Set(2);
	var(methodValue, methodCall) = session.CallMethod(counter, "Add", argument);
	using (methodValue)
	{
		Expect(methodCall.Success && methodValue.GetInt32() == 7 && interceptedCalls == 1, "intercepted method call");
	}

	argument.Set(1);
	var(bypassValue, bypassCall) = session.CallMethodBypassingInterceptor(counter, "Add", argument);
	using (bypassValue)
	{
		Expect(bypassCall.Success && bypassValue.GetInt32() == 8 && interceptedCalls == 1, "interceptor bypass");
	}

	session.SetInstanceCallInterceptor((_, _) => (VeroScriptInterceptResult)999);
	var(invalidInterceptValue, invalidInterceptCall) = session.CallMethod(counter, "Add", argument);
	using (invalidInterceptValue)
	{
		Expect(!invalidInterceptCall.Success, "invalid interceptor result rejection");
	}

	session.SetInstanceCallInterceptor(null);
	var(fieldValue, fieldRead) = session.GetField(counter, "value");
	using (fieldValue)
	{
		Expect(fieldRead.Success && fieldValue.GetInt32() == 8, "field read");
	}

	argument.Set(9);
	Expect(session.SetField(counter, "value", argument).Success, "field write");
	var(serializedCounter, serializedVersion) = session.SerializeObject(counter);
	Expect(serializedVersion == 3 && serializedCounter.Length == 4, "external object serialization");
	argument.Set(12);
	Expect(session.SetField(counter, "value", argument).Success, "field rewrite");
	session.DeserializeObject(counter, serializedCounter, serializedVersion);
	var(restoredField, restoredRead) = session.GetField(counter, "value");
	using (restoredField)
	{
		Expect(restoredRead.Success && restoredField.GetInt32() == 9, "external object deserialization");
	}

	int referenceCount = 0;
	session.EnumerateObjectReferences(counter, (_, _) => ++referenceCount);
	Expect(referenceCount == 0, "external object reference enumeration");
	var(holder, holderConstructed) = session.Construct("CounterHolder");
	using (holder)
	{
		Expect(holderConstructed.Success, "reference holder construction");
		Expect(session.SetField(holder, "reference", counter).Success, "reference holder assignment");
		VeroScriptBorrowedValue? borrowedReference = null;
		referenceCount = 0;
		session.EnumerateObjectReferences(holder, (kind, value) =>
		{
			Expect(kind == VeroScriptExternalReferenceKind.ScriptObject, "script object reference kind");
			borrowedReference = value;
			++referenceCount;
		});
		Expect(referenceCount == 1, "script object reference enumeration");
		ExpectThrows<InvalidOperationException>(() => borrowedReference!.GetInt32(), "expired external reference value");
		var visitorException = new InvalidOperationException("Expected external reference visitor failure.");
		bool visitorExceptionPropagated = false;
		try
		{
			session.EnumerateObjectReferences(holder, (_, _) => throw visitorException);
		}
		catch (InvalidOperationException exception)
		{
			visitorExceptionPropagated = ReferenceEquals(exception, visitorException);
		}

		Expect(visitorExceptionPropagated, "external reference visitor exception propagation");
	}

	Expect(session.ApplyClassDefaults(counter).Success, "class defaults");
	session.SetInstanceCallInterceptor(null);
}

Expect(mutationCount != 0, "mutation callback");
VeroScriptRunResult run = session.Run();
Expect(run.Success && output.Contains("5"), "script output callback");
var(globalValue, globalRead) = session.GetGlobal("GlobalValue");
using (globalValue)
{
	Expect(globalRead.Success && globalValue.GetInt32() == 7, "global read");
}

argument.Set(11);
Expect(session.SetGlobal("GlobalValue", argument).Success, "global write");
_ = session.GetLastJitStats();
string bindingJson = session.ExportBindingsJson();
Expect(bindingJson.Contains("\"caseSensitive\": false"), "annotation name policy export");
Expect(bindingJson.Contains("host_add_one") && bindingJson.Contains("structured_host") && bindingJson.Contains("ManagedVisible") && bindingJson.Contains("ManagedMode") && bindingJson.Contains("Tooling category") && bindingJson.Contains("ManagedInt"), "binding JSON export");
Expect(session.ExportReflectionJson().Contains("Counter"), "reflection JSON export");
var(structuredValue, structuredCall) = session.CallFunction("UseStructuredHost");
var(annotationPolicy, annotationPolicyCall) = session.CallFunction("CheckAnnotationNamePolicy");
using (annotationPolicy)
{
	Expect(annotationPolicyCall.Success && annotationPolicy.GetBool(), "managed annotation policy reaches reflection");
}

using (structuredValue)
{
	Expect(structuredCall.Success && structuredValue.GetInt32() == 42, "structured host function call");
}

var(failedValue, failedCall) = session.CallFunction("UseHostFailure");
using (failedValue)
{
	Expect(!failedCall.Success, "callback SetError propagation");
}

var(emptyExceptionValue, emptyExceptionCall) = session.CallFunction("UseEmptyException");
string emptyExceptionDiagnostics = string.Join(" | ", session.GetDiagnostics().Select(diagnostic => diagnostic.Message));
using (emptyExceptionValue)
{
	Expect(!emptyExceptionCall.Success && emptyExceptionDiagnostics.Contains("Managed callback failed.", StringComparison.Ordinal), $"empty managed callback exception fallback [{emptyExceptionDiagnostics}] " + $"[{session.GetLastError()}]");
}

var(invalidMessageValue, invalidMessageCall) = session.CallFunction("UseInvalidMessageException");
string invalidMessageDiagnostics = string.Join(" | ", session.GetDiagnostics().Select(diagnostic => diagnostic.Message));
using (invalidMessageValue)
{
	Expect(!invalidMessageCall.Success && invalidMessageDiagnostics.Contains("Managed callback failed.", StringComparison.Ordinal), $"invalid UTF-16 managed callback exception fallback [{invalidMessageDiagnostics}] " + $"[{session.GetLastError()}]");
}

var(throwingMessageValue, throwingMessageCall) = session.CallFunction("UseThrowingMessageException");
string throwingMessageDiagnostics = string.Join(" | ", session.GetDiagnostics().Select(diagnostic => diagnostic.Message));
using (throwingMessageValue)
{
	Expect(!throwingMessageCall.Success && throwingMessageDiagnostics.Contains("Managed callback failed.", StringComparison.Ordinal), $"throwing managed callback exception message fallback [{throwingMessageDiagnostics}] " + $"[{session.GetLastError()}]");
}

var(functionValue, functionCall) = session.CallFunction("GetCallable");
using (functionValue)
{
	Expect(functionCall.Success, "function value retrieval");
	using VeroScriptCallable callable = session.RetainCallable(functionValue);
	ExpectThrows<ArgumentNullException>(() => callable.Invoke(null!), "null callable argument-list validation");
	argument.Set(6);
	var(callableValue, callableCall) = callable.Invoke(argument);
	using (callableValue)
	{
		Expect(callableCall.Success && callableValue.GetInt32() == 12, "retained callable invocation");
	}
}

var(disposeFunctionValue, disposeFunctionCall) = session.CallFunction("GetDisposeCallable");
using (disposeFunctionValue)
{
	Expect(disposeFunctionCall.Success, "self-disposing function value retrieval");
	var callable = session.RetainCallable(disposeFunctionValue);
	callbackDisposedCallable = callable;
	argument.Set(7);
	var(disposedInvokeValue, disposedInvokeCall) = callable.Invoke(argument);
	using (disposedInvokeValue)
	{
		Expect(disposedInvokeCall.Success && disposedInvokeValue.GetInt32() == 14 && !callable.IsValid, "deferred callable destruction during invocation");
	}

	callable.Dispose();
	callbackDisposedCallable = null;
}

var(crossThreadDisposeFunction, crossThreadDisposeFunctionCall) = session.CallFunction("GetCrossThreadDisposeCallable");
using (crossThreadDisposeFunction)
{
	Expect(crossThreadDisposeFunctionCall.Success, "cross-thread-disposing function value retrieval");
	using var callable = session.RetainCallable(crossThreadDisposeFunction);
	callbackDisposedCallable = callable;
	argument.Set(8);
	var(disposedInvokeValue, disposedInvokeCall) = callable.Invoke(argument);
	using (disposedInvokeValue)
	{
		Expect(callableDisposalTask is not null && callableDisposalTask.Wait(TimeSpan.FromSeconds(5)), "callable disposal task completes");
		Expect(callableDisposedInCallback && disposedInvokeCall.Success && disposedInvokeValue.GetInt32() == 16 && !callable.IsValid, "cross-thread disposal completes during a waiting callback and preserves its active invocation");
		ExpectThrows<ObjectDisposedException>(() => callable.Invoke(argument), "disposed callable rejects subsequent invocation");
	}

	callbackDisposedCallable = null;
}

string packageSourcePath = Path.Combine(Path.GetTempPath(), $"veroscript-csharp-{Guid.NewGuid():N}.vs");
try
{
	File.WriteAllText(packageSourcePath, "int PackageDouble(int value) { return value * 2; }\nvoid main() {}\n");
	var(packageBytes, packageBuild) = session.BuildPackageFile(packageSourcePath, new VeroScriptPackageBuildOptions { Kind = VeroScriptPackageKind.Application, PackageName = "csharp-test", PackageVersion = 3, HostMetadata = new Dictionary<string, byte[]> { ["managed"] = [1, 2, 3] } });
	Expect(packageBuild.Success && packageBytes.Length != 0, "package build");
	string applicationPath = Path.ChangeExtension(packageSourcePath, ".vsapp");
	File.WriteAllBytes(applicationPath, packageBytes);
	using (var applicationSession = CreateTestSession())
	{
		Expect(applicationSession.LoadApplication(applicationPath).Success, "application container load with native service");
		Expect(applicationSession.Run().Success, "application container run");
		ExpectThrows<VeroScriptException>(() => applicationSession.LoadApplication(applicationPath, "{\"unknown\":true}"), "application options are strict");
		Expect(applicationSession.Run().Success, "failed application options preserve loaded code");
	}

	using VeroScriptPackage package = VeroScriptPackage.Read(packageBytes);
	Expect(package.Name == "csharp-test" && package.Version == 3 && package.GetMetadata().Single().Value.SequenceEqual(new byte[] { 1, 2, 3 }) && package.GetCanonicalSignedBytes().Length != 0, "package inspection");
	using var packageSession = CreateTestSession();
	packageSession.HotReloadEnabled = true;
	Expect(packageSession.LoadPackage(package, "csharp-test.vsapp").Success, "package load");
	Expect(!package.HasCompiledModule, "package module transfer state");
	ExpectThrows<InvalidOperationException>(() => packageSession.ReloadPackage(package, "csharp-test.vsapp"), "consumed package rejection");
	using VeroScriptPackage reloadPackage = VeroScriptPackage.Read(packageBytes);
	Expect(packageSession.ReloadPackage(reloadPackage, "csharp-test.vsapp").Success, "package reload");
	argument.Set(8);
	var(packageValue, packageCall) = packageSession.CallFunction("PackageDouble", argument);
	using (packageValue)
	{
		Expect(packageCall.Success && packageValue.GetInt32() == 16, "package function call");
	}

	using var fileSession = CreateTestSession();
	fileSession.HotReloadEnabled = true;
	Expect(fileSession.LoadFile(packageSourcePath).Success, "file load");
	var(fileValue, fileCall) = fileSession.CallFunction("PackageDouble", argument);
	using (fileValue)
	{
		Expect(fileCall.Success && fileValue.GetInt32() == 16, "file function call");
	}

	File.WriteAllText(packageSourcePath, "int PackageDouble(int value) { return value * 3; }\nvoid main() {}\n");
	Expect(fileSession.Reload().Success, "file reload");
	var(reloadedValue, reloadedCall) = fileSession.CallFunction("PackageDouble", argument);
	using (reloadedValue)
	{
		Expect(reloadedCall.Success && reloadedValue.GetInt32() == 24, "reloaded file function call");
	}
}
finally
{
	File.Delete(packageSourcePath);
	File.Delete(Path.ChangeExtension(packageSourcePath, ".vsapp"));
}

VeroScriptRunResult invalid = session.LoadSource("void main() { MissingCall(); }", "bad.vs");
Expect(!invalid.Success && session.GetDiagnostics().Count != 0, "structured diagnostics");
Expect(session.GetDiagnosticArguments(0).Contains("MissingCall"), "diagnostic arguments");
VeroScriptRuntime.CurrentLanguage = "zh-CN";
Expect(!string.IsNullOrWhiteSpace(session.FormatDiagnostic(0)), "localized diagnostic formatting");
VeroScriptRuntime.CurrentLanguage = "en-US";
VeroScriptStatus crossThreadStatus = VeroScriptStatus.Ok;
var foreignThread = new Thread(() =>
{
	try
	{
		_ = session.IsLoaded;
	}
	catch (VeroScriptException exception)
	{
		crossThreadStatus = exception.Status;
	}
});
foreignThread.Start();
foreignThread.Join();
Expect(crossThreadStatus == VeroScriptStatus.WrongThread, "managed creator-thread guard");
_ = session.GetLastError();
session.Clear();
Expect(!session.IsLoaded, "session clear");
Console.WriteLine($"VeroScriptCSharp tests passed (.NET {Environment.Version}).");
sealed class ChangingCountReadOnlyList<T>(T first, T second) : IReadOnlyList<T>
{
	private int _countReads;
	public int Count => Interlocked.Increment(ref _countReads) <= 2 ? 1 : 2;

	public T this[int index] => index switch
	{
		0 => first,
		1 => second,
		_ => throw new ArgumentOutOfRangeException(nameof(index))};
	public IEnumerator<T> GetEnumerator()
	{
		yield return first;
		yield return second;
	}

	System.Collections.IEnumerator System.Collections.IEnumerable.GetEnumerator() => GetEnumerator();
}

sealed class MutablePairCountState
{
	private int _stage;
	public int GetKeyCount()
	{
		if (_stage == 0)
		{
			_stage = 1;
			return 1;
		}

		return 2;
	}

	public int GetValueCount()
	{
		if (_stage == 1)
		{
			_stage = 2;
		}

		return 1;
	}
}

sealed class CoordinatedCountReadOnlyList<T>(T first, T second, Func<int> getCount) : IReadOnlyList<T>
{
	public int Count => getCount();

	public T this[int index] => index switch
	{
		0 => first,
		1 => second,
		_ => throw new ArgumentOutOfRangeException(nameof(index))};
	public IEnumerator<T> GetEnumerator()
	{
		yield return first;
		yield return second;
	}

	System.Collections.IEnumerator System.Collections.IEnumerable.GetEnumerator() => GetEnumerator();
}

sealed class ThrowingMessageException : Exception
{
	public override string Message => throw new InvalidOperationException("Message getter failed.");
}
