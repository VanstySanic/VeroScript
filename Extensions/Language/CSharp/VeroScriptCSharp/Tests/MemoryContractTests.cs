// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Buffers;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using VeroScript;
using VeroScript.Interop;

internal static class MemoryContractTests
{
	internal static void Run()
	{
		CheckStandardLibraryLimits();
		CheckPinRelease();
		CheckFailureRelease();
		CheckCallbackLifetime();
		var manager = new TrackingMemoryManager();
		WeakReference<VeroScriptMemoryLease> abandoned = AbandonLease(manager);
		GC.Collect();
		GC.WaitForPendingFinalizers();
		GC.Collect();
		Require(!abandoned.TryGetTarget(out _) && manager.Unpins == 1, "lease finalizer releases the external pin");
	}

	internal static void CheckStandardLibraryLimits()
	{
		using var session = new VeroScriptSession();
		session.SetStandardLibraryLimits(new(MaxTextBytes: 5));
		Require(session.LoadSource("void main(){format(\"{}{}\",\"abc\",\"abc\");}").Success, "load stdlib limit fixture");
		Require(!session.Run().Success, "stdlib format expansion is limited");
		session.SetStandardLibraryLimits(new());
		Require(session.Run().Success, "zero removes the extra stdlib limit");
	}

	internal static void RunUnsupportedRuntime()
	{
		// This process loads a same-ABI fixture with no memory exports at all.
		using var session = new VeroScriptSession();
		const string libraryVariable = "VEROSCRIPT_INTEROP_LIBRARY";
		string? libraryPath = Environment.GetEnvironmentVariable(libraryVariable);
		try
		{
			Environment.SetEnvironmentVariable(libraryVariable, "relative-path-after-runtime-selection");
			Require(VeroScriptRuntime.Capabilities == VeroScriptCapabilities.None,
				"later imports use the original native runtime");
		}
		finally
		{
			Environment.SetEnvironmentVariable(libraryVariable, libraryPath);
		}

		using var value = new VeroScriptValue();
		Require(VeroScriptRuntime.CompileCapabilities == VeroScriptCompileCapabilities.None, "older runtime has no optional compile capabilities");
		Throws<NotSupportedException>(() => session.GetReloadStatus());
		Throws<NotSupportedException>(() => session.SetAnnotationProcessingService(0));
		Throws<NotSupportedException>(() => session.SetCompileProcessingService(0));
		Throws<NotSupportedException>(() => session.SetScriptCompileExtensions(null));
		Throws<NotSupportedException>(() => session.ApplyConfiguration(new(), 0));
		Throws<NotSupportedException>(() => session.RegisterHostProtocol(0, new() { CloneCallback = (_, _) => throw new InvalidOperationException("unsupported callback was invoked") }));
		for (int attempt = 0; attempt < 2; ++attempt)
		{
			Require(session.GetDiagnostics()is [{ Message: "legacy diagnostic", Line: 3, Column: 2, Severity: VeroScriptDiagnosticSeverity.Error }], "legacy diagnostics remain readable without a severity export");
		}

		var scope = new BorrowScope();
		var context = new VeroScriptCallContext(1, scope);
		var manager = new TrackingMemoryManager();
		foreach (VeroScriptMemory memory in new[]
		{
			session.Memory,
			context.Memory
		}

		)
		{
			foreach (Action operation in new Action[]
			{
				() => _ = memory.Stats,
				() => _ = memory.NativeAddressesAvailable,
				() => memory.CreateBuffer(),
				() => memory.CreateView(),
				() => memory.Slice(value, 0, 0),
				() => memory.Clone(value),
				() => memory.FromArray(value),
				() => memory.ToArray(value),
				() => memory.Read(value, 0, VeroScriptNativeTypeKind.Int32),
				() => memory.Write(value, 0, VeroScriptNativeTypeKind.Int32, value),
				() => memory.Size(value),
				() => memory.Capacity(value),
				() => memory.Resize(value, 0),
				() => memory.Copy(value, 0, value, 0, 0),
				() => memory.Fill(value, 0, 0, 0),
				() => memory.Equal(value, 0, value, 0, 0),
				() => memory.ObjectId(value),
				() => memory.ObjectAddress(value),
				() => memory.DataAddress(value),
				() => memory.MutationVersion(value),
				() => memory.InvalidateExternal(value),
				() => memory.AcquireLease(value),
				() => memory.WrapExternal(manager.Memory)
			}

			)
			{
				Throws<NotSupportedException>(operation);
			}
		}

		Throws<NotSupportedException>(() => session.SetMemoryLimits(new()));
		// The fixture also omits the run export: capability rejection must precede P/Invoke.
		Throws<NotSupportedException>(() => session.Run(VeroScriptRunMode.EvalOnce));
		Throws<NotSupportedException>(() => session.SetVmBackend(null));
		Throws<NotSupportedException>(() => session.GetVmBackendInfo());
		Throws<NotSupportedException>(() => context.GetArgumentAddress(0));
		Require(manager.Pins == 0, "unsupported runtime must not pin managed memory");
		scope.Expire();
		Throws<InvalidOperationException>(() => context.Memory.CreateBuffer());
		session.Dispose();
		Throws<ObjectDisposedException>(() => session.Memory.CreateBuffer());
		Throws<ObjectDisposedException>(() => session.Run(VeroScriptRunMode.EvalOnce));
		Throws<ObjectDisposedException>(() => session.SetAnnotationProcessingService(0));
		Throws<ObjectDisposedException>(() => session.SetVmBackend(null));
		Throws<ObjectDisposedException>(() => session.GetVmBackendInfo());
		Console.WriteLine("Memory compatibility guard passed without native memory exports.");
	}

	private static void CheckPinRelease()
	{
		using var session = new VeroScriptSession();
		var memory = session.Memory;
		var manager = new TrackingMemoryManager();
		var external = memory.WrapExternal(manager.Memory);
		VeroScriptMemoryLease lease = memory.AcquireLease(external, write: true);
		lease.WritableBytes[0] = 42;
		Require(manager.GetSpan()[0] == 42, "lease writes reach managed storage");
		external.Dispose();
		session.Dispose();
		Require(manager.Unpins == 0, "lease retains the pin after region and session disposal");
		Require(lease.Bytes[0] == 42, "retained lease remains readable");
		Task.Run(lease.Dispose).GetAwaiter().GetResult();
		lease.Dispose();
		Require(manager.Pins == 1 && manager.Unpins == 1, "pin releases exactly once on another thread");
		Throws<ObjectDisposedException>(() => _ = lease.Data);
		Throws<ObjectDisposedException>(() => _ = lease.Bytes);
		Throws<ObjectDisposedException>(() => memory.WrapExternal(manager.Memory));
		Require(manager.Pins == 1, "disposed session is rejected before pinning");
	}

	private static void CheckFailureRelease()
	{
		using var session = new VeroScriptSession();
		session.SetMemoryLimits(new(2, 2));
		var manager = new TrackingMemoryManager();
		Throws<VeroScriptException>(() => session.Memory.WrapExternal(manager.Memory));
		Require(manager.Pins == 1 && manager.Unpins == 1, "failed registration releases the pin once");
		Require(session.Memory.Stats.BorrowedBytes == 0, "failed external registration retains no bytes");
	}

	private static void CheckCallbackLifetime()
	{
		using var session = new VeroScriptSession();
		VeroScriptMemory? escapedMemory = null;
		VeroScriptCallContext? escapedContext = null;
		session.RegisterFunction("memory_callback", VeroScriptNativeType.Int32, [new(VeroScriptNativeType.Int32, VeroScriptParameterFlags.Ref)], (context, result) =>
		{
			escapedMemory = context.Memory;
			escapedContext = context;
			using var bytes = context.Memory.CreateBuffer(4);
			Require(context.Memory.Size(bytes) == 4, "memory uses the active callback context");
			Require(context.GetArgumentAddress(0) != 0, "ref argument has an original storage address");
			Task.Run(() =>
			{
				ThrowsWrongThread(() => _ = context.ArgumentCount);
				ThrowsWrongThread(() => context.GetArgument(0));
				ThrowsWrongThread(() => _ = result.Kind);
				ThrowsWrongThread(() => context.Memory.CreateBuffer());
				ThrowsWrongThread(() => _ = context.Memory.Stats);
				ThrowsWrongThread(() => _ = context.Memory.NativeAddressesAvailable);
				ThrowsWrongThread(() => context.GetArgumentAddress(0));
			}).GetAwaiter().GetResult();
			Throws<VeroScriptException>(() => session.Memory.CreateBuffer());
			result.Set(42);
		});
		Require(session.LoadSource("void main(){int x=1; print(memory_callback(ref x));}").Success, "load memory callback script");
		Require(session.Run().Success, "run memory callback script");
		Require(escapedMemory is not null && escapedContext is not null, "callback ran");
		Throws<InvalidOperationException>(() => escapedMemory!.CreateBuffer());
		Throws<InvalidOperationException>(() => escapedContext!.GetArgumentAddress(0));
	}

	[MethodImpl(MethodImplOptions.NoInlining)]
	private static WeakReference<VeroScriptMemoryLease> AbandonLease(TrackingMemoryManager manager)
	{
		using var session = new VeroScriptSession();
		using var external = session.Memory.WrapExternal(manager.Memory);
		return new(session.Memory.AcquireLease(external));
	}

	private static void Throws<T>(Action operation)
		where T : Exception
	{
		try
		{
			operation();
		}
		catch (T)
		{
			return;
		}

		throw new InvalidOperationException("Expected " + typeof(T).Name);
	}

	private static void ThrowsWrongThread(Action operation)
	{
		try
		{
			operation();
		}
		catch (VeroScriptException exception)when (exception.Status == VeroScriptStatus.WrongThread)
		{
			return;
		}

		throw new InvalidOperationException("Expected WrongThread for callback memory access.");
	}

	private static void Require(bool condition, string message)
	{
		if (!condition)
		{
			throw new InvalidOperationException(message);
		}
	}

	private sealed unsafe class TrackingMemoryManager : MemoryManager<byte>
	{
		public override Span<byte> GetSpan() => _bytes;
		public override MemoryHandle Pin(int elementIndex = 0)
		{
			if (elementIndex < 0 || elementIndex > _bytes.Length)
			{
				throw new ArgumentOutOfRangeException(nameof(elementIndex));
			}

			_pin = GCHandle.Alloc(_bytes, GCHandleType.Pinned);
			Interlocked.Increment(ref _pins);
			return new((byte*)_pin.AddrOfPinnedObject() + elementIndex, default, this);
		}

		public override void Unpin()
		{
			_pin.Free();
			Interlocked.Increment(ref _unpins);
		}

		protected override void Dispose(bool disposing)
		{
		}

		internal int Pins => Volatile.Read(ref _pins);
		internal int Unpins => Volatile.Read(ref _unpins);

		private readonly byte[] _bytes = [1, 2, 3];
		private GCHandle _pin;
		private int _pins;
		private int _unpins;
	}
}
