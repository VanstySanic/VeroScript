// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using VeroScript.Interop;

namespace VeroScript;
/// <summary>Configures managed output callbacks for a VeroScript session.</summary>
public sealed class VeroScriptSessionOptions
{
	/// <summary>Receives script output and may be called concurrently by VM workers.</summary>
	public Action<string>? Print { get; init; }
	/// <summary>Receives diagnostics and may be called concurrently by VM workers.</summary>
	public Action<string>? Diagnostic { get; init; }
	/// <summary>Queries terminal capabilities synchronously, including during cleanup.
	/// Null or a callback exception leaves controls unsupported. May run concurrently on VM workers.</summary>
	public Func<VeroScriptConsoleInfo>? ConsoleQuery { get; init; }
	/// <summary>Executes a terminal operation; true means success. May run concurrently on VM workers.
	/// ClearLine modes: after=0, before=1, all=2. Colors: ANSI palette 0..15, default=-1.
	/// SetColor uses foreground,background; ClearLine and visibility use the first argument.
	/// MoveCursor uses zero based column,row; visibility uses 0/1. Other arguments are zero.
	/// Cleanup may reset style/show cursor. Callback exceptions return failure.</summary>
	public Func<VeroScriptConsoleOperation, int, int, bool>? ConsoleControl { get; init; }
	/// <summary>Writes exact console::write text without an added newline; true means success.
	/// Null retains buffered Print output. False or a callback exception fails the script write.
	/// May run concurrently on VM workers.</summary>
	public Func<string, bool>? ConsoleWrite { get; init; }
}

/// <summary>
/// Owns one thread-affine VeroScript compiler and runtime session.
/// </summary>
/// <remarks>
/// Dispose the session on the managed thread that created it. Registered callbacks
/// remain rooted until the session is disposed. Host protocol callbacks remain
/// rooted until their last native stamped value is released. Script callbacks may run
/// concurrently on VM worker threads; callback-owned state must be synchronized.
/// </remarks>
public sealed unsafe partial class VeroScriptSession : IDisposable
{
	private sealed record GenericFactoryState(VeroScriptSession Session, VeroScriptGenericHostTypeFactory Factory);
	private sealed class ProtocolCallbackOwner
	{
		internal ProtocolCallbackOwner(VeroScriptNativeFunction callback)
		{
			_callback = callback;
			_handle = GCHandle.Alloc(this);
		}

		internal void Retain(VeroScriptSession session)
		{
			session.CheckStatus(NativeMethods.vs_interop_session_retain_callback_user_data(session.Handle, Pointer, &ReleaseProtocolUserData), "retain host protocol callback");
			Volatile.Write(ref _ownership, 2);
		}

		internal void ReleaseManaged()
		{
			if (Interlocked.CompareExchange(ref _ownership, 0, 1) == 1)
			{
				_handle.Free();
				_callback = null;
			}
		}

		internal void ReleaseNative()
		{
			if (Interlocked.Exchange(ref _ownership, 0) != 0)
			{
				_handle.Free();
				_callback = null;
			}
		}

		internal VeroScriptNativeFunction Callback => _callback ?? throw new ObjectDisposedException(nameof(ProtocolCallbackOwner));
		internal nint Pointer => GCHandle.ToIntPtr(_handle);
		internal bool IsManagedOwned => Volatile.Read(ref _ownership) == 1;

		private GCHandle _handle;
		private VeroScriptNativeFunction? _callback;
		// Managed registration owns the root first; native protocol stamps take
		// ownership only after retention succeeds. Release remains idempotent.
		private int _ownership = 1;
	}

	private sealed class GenericCallbackTransaction : IDisposable
	{
		private readonly VeroScriptSession _session;
		private readonly GenericCallbackTransaction? _previous;
		private readonly List<GCHandle> _handles = [];
		private readonly List<ProtocolCallbackOwner> _protocols = [];
		private bool _committed;
		internal GenericCallbackTransaction(VeroScriptSession session)
		{
			_session = session;
			_previous = session._activeGenericCallbackTransaction;
			session._activeGenericCallbackTransaction = this;
		}

		internal void Add(GCHandle handle)
		{
			_handles.EnsureCapacity(checked(_handles.Count + 1));
			_handles.Add(handle);
		}

		internal bool Remove(GCHandle handle) => _handles.Remove(handle);
		internal void AddProtocols(List<ProtocolCallbackOwner> protocols)
		{
			_protocols.EnsureCapacity(checked(_protocols.Count + protocols.Count));
			_session._protocolCallbacks.EnsureCapacity(checked(_session._protocolCallbacks.Count + _protocols.Count + protocols.Count));
			_protocols.AddRange(protocols);
		}

		internal void Commit()
		{
			try
			{
				_session.TrackCallbacks(_handles);
				_session._protocolCallbacks.AddRange(_protocols);
				_handles.Clear();
				_committed = true;
				// Instantiation has left the native configuration activity.
				// Retention is idle-only and cannot run inside the factory.
				foreach (ProtocolCallbackOwner protocol in _protocols)
				{
					if (protocol.IsManagedOwned)
					{
						protocol.Retain(_session);
					}
				}

				_protocols.Clear();
			}
			catch
			{
				_session.Dispose();
				throw;
			}
		}

		public void Dispose()
		{
			_session._activeGenericCallbackTransaction = _previous;
			if (_committed)
			{
				return;
			}

			foreach (GCHandle handle in _handles)
			{
				if (handle.IsAllocated)
				{
					handle.Free();
				}
			}

			_handles.Clear();
			foreach (ProtocolCallbackOwner protocol in _protocols)
			{
				protocol.ReleaseManaged();
			}

			_protocols.Clear();
		}
	}

	private sealed record ExternalSerializerState(VeroScriptExternalSerializeHandler Serialize, VeroScriptExternalDeserializeHandler Deserialize);
	private sealed class ExternalReferenceEnumerationState(VeroScriptExternalReferenceVisitor visitor)
	{
		private ExceptionDispatchInfo? _exception;
		internal VeroScriptExternalReferenceVisitor Visitor { get; } = visitor;
		internal ExceptionDispatchInfo? Exception => Volatile.Read(ref _exception);

		internal void Capture(Exception exception) => Interlocked.CompareExchange(ref _exception, ExceptionDispatchInfo.Capture(exception), null);
	}

	private nint _handle;
	private readonly int _ownerThreadId;
	private readonly GCHandle _outputHandle;
	private readonly object _callbackHandleSync = new();
	private readonly List<GCHandle> _callbackHandles = [];
	private readonly List<ProtocolCallbackOwner> _protocolCallbacks = [];
	private GenericCallbackTransaction? _activeGenericCallbackTransaction;
	private GCHandle _interceptorHandle;
	private GCHandle _mutationHandle;
	internal static delegate* unmanaged[Cdecl]<nint, nint, nint, uint> NativeCallback => &OnNativeCall;

	public VeroScriptSession(VeroScriptSessionOptions? options = null)
	{
		VeroScriptRuntime.EnsureCompatible();
		_ownerThreadId = Environment.CurrentManagedThreadId;
		_outputHandle = GCHandle.Alloc(options ?? new VeroScriptSessionOptions());
		try
		{
			var nativeOptions = new NativeSessionOptions
			{
				StructSize = (uint)sizeof(NativeSessionOptions),
				PrintCallback = &OnPrint,
				DiagnosticCallback = &OnDiagnostic,
				ConsoleQueryCallback = options?.ConsoleQuery is null ? null : &OnConsoleQuery,
				ConsoleControlCallback = options?.ConsoleControl is null ? null : &OnConsoleControl,
				ConsoleWriteCallback = options?.ConsoleWrite is null ? null : &OnConsoleWrite,
				UserData = GCHandle.ToIntPtr(_outputHandle)
			};
			nint handle = 0;
			VeroScriptStatus status = NativeMethods.vs_interop_session_create(&nativeOptions, &handle);
			if (status != VeroScriptStatus.Ok)
			{
				if (handle != 0)
				{
					_ = NativeMethods.vs_interop_session_destroy(handle);
				}

				VeroScriptValue.ThrowIfFailed(status, "create session");
			}

			if (handle == 0)
			{
				throw new VeroScriptException(VeroScriptStatus.InternalError, "VeroScript created a null session handle.");
			}

			_handle = handle;
		}
		catch
		{
			if (_outputHandle.IsAllocated)
			{
				_outputHandle.Free();
			}

			throw;
		}
	}

	public bool IsLoaded
	{
		get
		{
			CheckOwner();
			return NativeMethods.vs_interop_session_is_loaded(Handle) != 0;
		}
	}

	public bool HotReloadEnabled
	{
		get
		{
			CheckOwner();
			return NativeMethods.vs_interop_session_is_hot_reload_enabled(Handle) != 0;
		}

		set
		{
			CheckOwner();
			CheckStatus(NativeMethods.vs_interop_session_set_hot_reload_enabled(Handle, value ? (byte)1 : (byte)0), "set hot reload");
		}
	}

	public void ApplyConfiguration(VeroScriptConfiguration configuration) => ApplyConfigurationCore(configuration, null);
	/// <summary>Applies configuration and explicitly selects a native module environment. Zero removes it.</summary>
	/// <remarks>The native SDK supplies a valid opaque snapshot handle, kept alive through this call.
	/// The session retains the snapshot after success, so the caller may release its handle.
	/// Clear the loaded program before changing snapshots. Requires an idle creator-thread session
	/// and VeroScriptCapabilities.ModuleEnvironment. Ordinary ApplyConfiguration preserves the selection.</remarks>
	public void ApplyConfiguration(VeroScriptConfiguration configuration, nint moduleEnvironment) => ApplyConfigurationCore(configuration, moduleEnvironment);
	private void ApplyConfigurationCore(VeroScriptConfiguration configuration, nint? moduleEnvironment)
	{
		CheckOwner();
		ArgumentNullException.ThrowIfNull(configuration);
		_ = Handle;
		using var lifetime = new NativeLifetime(this);
		if (moduleEnvironment.HasValue && (VeroScriptRuntime.Capabilities & VeroScriptCapabilities.ModuleEnvironment) == 0)
		{
			throw new NotSupportedException("The loaded VeroScript runtime does not support module environments.");
		}

		using var lease = new NativeConfigLease(configuration);
		NativeConfig config = lease.Value;
		// User-provided collections can dispose the session while being copied.
		nint handle = Handle;
		VeroScriptStatus status = moduleEnvironment.HasValue ? NativeMethods.vs_interop_session_apply_config_with_environment(handle, &config, moduleEnvironment.Value) : NativeMethods.vs_interop_session_apply_config(handle, &config);
		CheckStatus(status, "apply configuration");
	}

	/// <summary>Requests cooperative cancellation of an execution-control-enabled session.</summary>
	/// <remarks>
	/// This is the one session operation that may be called from a non-owner thread.
	/// The caller must keep the session alive until the call returns; it never
	/// asynchronously interrupts a native or managed callback already in progress.
	/// </remarks>
	public void RequestCancellation()
	{
		nint handle = Volatile.Read(ref _handle);
		ObjectDisposedException.ThrowIf(handle == 0, this);
		VeroScriptStatus status = NativeMethods.vs_interop_session_request_cancel(handle);
		// A rejected cancellation can leave the native error buffer unchanged.
		// Report its status without reusing an earlier operation's error.
		VeroScriptValue.ThrowIfFailed(status, "request cancellation");
	}

	/// <summary>Attaches a service handle supplied by a native extension. Zero clears it; the session retains its configuration.</summary>
	/// <remarks>The handle must be valid through this call. The caller may release it after attachment;
	/// processor code and callback data must remain valid through their last use. Requires an idle session
	/// and VeroScriptCompileCapabilities.AnnotationProcessing. ApplyConfiguration preserves the service.</remarks>
	public void SetAnnotationProcessingService(nint service)
	{
		CheckOwner();
		nint handle = Handle;
		if ((VeroScriptRuntime.CompileCapabilities & VeroScriptCompileCapabilities.AnnotationProcessing) == 0)
		{
			throw new NotSupportedException("The loaded VeroScript runtime does not support annotation processing services.");
		}

		CheckStatus(NativeMethods.vs_interop_session_set_annotation_service(handle, service), "set annotation processing service");
	}

	/// <summary>Attaches a native general compilation service. Zero clears it; ordinary configuration preserves it.</summary>
	/// <remarks>The native SDK supplies a valid opaque service handle, kept alive through this call.
	/// The session copies its configuration; the caller may then release the handle. Processor code
	/// and callback data must remain valid through their last use. Requires an idle creator-thread
	/// session and VeroScriptCompileCapabilities.GeneralProcessing.</remarks>
	public void SetCompileProcessingService(nint service)
	{
		CheckOwner();
		nint handle = Handle;
		using var lifetime = new NativeLifetime(this);
		if ((VeroScriptRuntime.CompileCapabilities & VeroScriptCompileCapabilities.GeneralProcessing) == 0)
		{
			throw new NotSupportedException("The loaded VeroScript runtime does not support general compilation services.");
		}

		CheckStatus(NativeMethods.vs_interop_session_set_compile_processing_service(handle, service), "set compile processing service");
	}

	/// <summary>Copies compilation extension configuration while idle. Null restores defaults.</summary>
	public void SetScriptCompileExtensions(VeroScriptCompileExtensionsOptions? options)
	{
		CheckOwner();
		using var lifetime = new NativeLifetime(this);
		if ((VeroScriptRuntime.CompileCapabilities & VeroScriptCompileCapabilities.ScriptExtensions) == 0)
		{
			throw new NotSupportedException("The loaded runtime does not support script compilation extensions.");
		}

		if (options is null)
		{
			CheckStatus(NativeMethods.vs_interop_session_set_script_compile_options(Handle, null), "set script compile extensions");
			return;
		}

		ArgumentNullException.ThrowIfNull(options.Roots);
		ArgumentNullException.ThrowIfNull(options.Configuration);
		ArgumentNullException.ThrowIfNull(options.AllowedRoots);
		if (options.Roots.Count > 64 || options.Configuration.Count > 4096 || options.AllowedRoots.Count > 4096)
		{
			throw new ArgumentException("Compilation extension configuration exceeds its entry limits.", nameof(options));
		}

		var strings = new List<Utf8>();
		NativeStringView Encode(string value)
		{
			var utf8 = new Utf8(value);
			try
			{
				strings.Add(utf8);
				return utf8.View;
			}
			catch
			{
				utf8.Dispose();
				throw;
			}
		}

		try
		{
			var roots = options.Roots.Select(root => new NativeCompileExtensionRoot { Path = Encode(root.Path), RegistrationEntry = Encode(root.RegistrationEntry) }).ToArray();
			var configuration = options.Configuration.Select(pair => new NativeCompileConfiguration { Key = Encode(pair.Key), Value = Encode(pair.Value) }).ToArray();
			var allowedRoots = options.AllowedRoots.Select(Encode).ToArray();
			fixed (NativeCompileExtensionRoot* rootData = roots)
			fixed (NativeCompileConfiguration* configData = configuration)
			fixed (NativeStringView* allowedData = allowedRoots)
			{
				NativeScriptCompileOptions native = new()
				{
					StructSize = (uint)sizeof(NativeScriptCompileOptions),
					AllowSourceImports = options.AllowSourceImports ? (byte)1 : (byte)0,
					Roots = rootData,
					RootCount = (ulong)roots.Length,
					Configuration = configData,
					ConfigurationCount = (ulong)configuration.Length,
					MaxInstructions = options.MaxInstructions,
					TimeoutMilliseconds = options.TimeoutMilliseconds,
					AllowedRoots = allowedData,
					AllowedRootCount = (ulong)allowedRoots.Length
				};
				CheckStatus(NativeMethods.vs_interop_session_set_script_compile_options(Handle, &native), "set script compile extensions");
			}
		}
		finally
		{
			foreach (var value in strings)
			{
				value.Dispose();
			}
		}
	}

	/// <summary>Selects a backend file, directory or installed ID while idle. "auto" resolves the environment; "none", null or empty disables it.</summary>
	/// <remarks>A failed load preserves the current backend. Successful replacement invalidates retained JIT code.</remarks>
	public void SetJitBackend(string? path)
	{
		CheckOwner();
		using var nativePath = new Utf8(path ?? string.Empty);
		CheckStatus(NativeMethods.vs_interop_session_set_jit_backend(Handle, nativePath.View), "set JIT backend");
	}

	/// <summary>Selects an interpreter file, directory or installed ID before the first execution. "auto" resolves the environment.</summary>
	/// <remarks>Null or empty clears selection. A failed replacement preserves the selection.
	/// Execution, including JIT, freezes the domain; Clear/reload does not thaw it.
	/// Requires <see cref = "VeroScriptCapabilities.VmBackendSelection"/>.</remarks>
	public void SetVmBackend(string? path)
	{
		CheckOwner();
		_ = Handle;
		EnsureVmBackendSupported();
		using var nativePath = new Utf8(path ?? string.Empty);
		CheckStatus(NativeMethods.vs_interop_session_set_vm_backend(Handle, nativePath.View), "set VM backend");
	}

	/// <summary>Copies the selected interpreter's metadata and execution-domain state.</summary>
	/// <remarks>Requires <see cref = "VeroScriptCapabilities.VmBackendSelection"/>.</remarks>
	public VeroScriptVmBackendInfo GetVmBackendInfo()
	{
		CheckOwner();
		_ = Handle;
		EnsureVmBackendSupported();
		var info = new NativeVmBackendInfo
		{
			StructSize = (uint)sizeof(NativeVmBackendInfo)
		};
		CheckStatus(NativeMethods.vs_interop_session_get_vm_backend_info(Handle, &info), "get VM backend information");
		return new(Utf8.Copy(info.Id), Utf8.Copy(info.Version), Utf8.Copy(info.Target), Utf8.Copy(info.LibraryPath), info.Capabilities, (info.Flags & 1) != 0, (info.Flags & 2) != 0);
	}

	private static void EnsureVmBackendSupported()
	{
		if ((VeroScriptRuntime.Capabilities & VeroScriptCapabilities.VmBackendSelection) == 0)
		{
			throw new NotSupportedException("The loaded VeroScript runtime does not support VM backend selection.");
		}
	}

	public VeroScriptJitBackend GetJitBackend()
	{
		CheckOwner();
		VeroScriptJitBackend backend;
		CheckStatus(NativeMethods.vs_interop_session_get_jit_backend(Handle, &backend), "get JIT backend");
		return backend;
	}

	/// <summary>Copies metadata for this session's selected backend.</summary>
	public VeroScriptJitBackendInfo GetJitBackendInfo()
	{
		CheckOwner();
		var info = new NativeJitBackendInfo
		{
			StructSize = (uint)sizeof(NativeJitBackendInfo)
		};
		var status = NativeMethods.vs_interop_session_get_jit_backend_info(Handle, &info);
		CheckStatus(status, "get JIT backend information");
		return new(Utf8.Copy(info.Id), Utf8.Copy(info.Target), info.Capabilities, info.NativeCode != 0);
	}

	/// <summary>Sets adaptive JIT policy and discards any reusable session-code context.</summary>
	public void SetJitPolicy(VeroScriptJitPolicy policy)
	{
		CheckOwner();
		var native = new NativeJitPolicy
		{
			StructSize = (uint)sizeof(NativeJitPolicy),
			MixedHotCallThreshold = policy.MixedHotCallThreshold,
			MaxCompileAttempts = policy.MaxCompileAttempts,
			MaxCachedEntries = policy.MaxCachedEntries,
			EagerCompileBudget = policy.EagerCompileBudget,
			ReuseSessionCode = policy.ReuseSessionCode ? (byte)1 : (byte)0,
			EnableControlledMixed = policy.EnableControlledMixed ? (byte)1 : (byte)0
		};
		CheckStatus(NativeMethods.vs_interop_session_set_jit_policy(Handle, &native), "set JIT policy");
	}

	/// <summary>
	/// Sets the retained native-code and complete-cache payload limits for eligible
	/// execution and discards the current reusable context. Zero
	/// permits no retained payload of that kind.
	/// </summary>
	/// <exception cref = "NotSupportedException">The loaded native runtime lacks the session-cache capability.</exception>
	public void SetJitCacheLimits(VeroScriptJitCacheLimits limits)
	{
		CheckOwner();
		EnsureJitCacheCapability();
		var native = new NativeJitCacheLimits
		{
			StructSize = (uint)sizeof(NativeJitCacheLimits),
			MaxNativeBytes = limits.MaxNativeBytes,
			MaxRetainedPayloadBytes = limits.MaxRetainedPayloadBytes
		};
		CheckStatus(NativeMethods.vs_interop_session_set_jit_cache_limits(Handle, &native), "set JIT cache limits");
	}

	/// <summary>Gets the configured limits for payload retained between eligible executions.</summary>
	/// <exception cref = "NotSupportedException">The loaded native runtime lacks the session-cache capability.</exception>
	public VeroScriptJitCacheLimits GetJitCacheLimits()
	{
		CheckOwner();
		EnsureJitCacheCapability();
		var native = new NativeJitCacheLimits
		{
			StructSize = (uint)sizeof(NativeJitCacheLimits)
		};
		CheckStatus(NativeMethods.vs_interop_session_get_jit_cache_limits(Handle, &native), "get JIT cache limits");
		return new(native.MaxNativeBytes, native.MaxRetainedPayloadBytes);
	}

	/// <summary>
	/// Gets settled, cumulative cache-resource statistics for the current session
	/// cache context. This is not a live counter during execution and
	/// is reset when that context is invalidated.
	/// </summary>
	/// <exception cref = "NotSupportedException">The loaded native runtime lacks the session-cache capability.</exception>
	public VeroScriptJitCacheStats GetJitCacheStats()
	{
		CheckOwner();
		EnsureJitCacheCapability();
		var native = new NativeJitCacheStats
		{
			StructSize = (uint)sizeof(NativeJitCacheStats)
		};
		CheckStatus(NativeMethods.vs_interop_session_get_jit_cache_stats(Handle, &native), "get JIT cache statistics");
		return new VeroScriptJitCacheStats
		{
			CompileAttempts = native.CompileAttempts,
			RetainedPayloadBytes = native.RetainedPayloadBytes,
			NativeBytes = native.NativeBytes,
			ResourceLimitReached = native.ResourceLimitReached != 0
		};
	}

	public VeroScriptJitStats GetLastJitStats()
	{
		CheckOwner();
		var native = new NativeJitStats
		{
			StructSize = (uint)sizeof(NativeJitStats)
		};
		CheckStatus(NativeMethods.vs_interop_session_get_last_jit_stats(Handle, &native), "get JIT statistics");
		return new VeroScriptJitStats
		{
			CompileAttempts = native.CompileAttempts,
			CacheHits = native.CacheHits,
			CompiledEntries = native.CompiledEntries,
			NativeLeafEntries = native.NativeLeafEntries,
			NativeCfgEntries = native.NativeCfgEntries,
			BridgeTrampolineEntries = native.BridgeTrampolineEntries,
			LinearTrampolineEntries = native.LinearTrampolineEntries,
			VmTrampolineEntries = native.VmTrampolineEntries,
			UnsupportedEntries = native.UnsupportedEntries,
			DispatchAttempts = native.DispatchAttempts,
			Dispatches = native.Dispatches,
			Fallbacks = native.Fallbacks,
			CostModelVmFallbacks = native.CostModelVmFallbacks,
			Failures = native.Failures,
			CacheInvalidations = native.CacheInvalidations,
			HotnessDeferrals = native.HotnessDeferrals,
			HotFunctions = native.HotFunctions,
			CompileBudgetSkips = native.CompileBudgetSkips,
			CacheCapacitySkips = native.CacheCapacitySkips,
			EagerCompileSkips = native.EagerCompileSkips,
			ArrayFastPathPushes = native.ArrayFastPathPushes,
			ArrayFastPathGets = native.ArrayFastPathGets,
			ArrayFastPathSets = native.ArrayFastPathSets,
			ScalarMapFastPathGets = native.ScalarMapFastPathGets,
			ScalarMapFastPathSets = native.ScalarMapFastPathSets,
			NativeTypedLoopExecutions = native.NativeTypedLoopExecutions,
			ControlledNativeExecutions = native.ControlledNativeExecutions,
			AotExecutions = native.AotExecutions,
			NativeLeafExecutions = native.NativeLeafExecutions,
			NativeCfgExecutions = native.NativeCfgExecutions,
			BridgeExecutions = native.BridgeExecutions,
			LinearExecutions = native.LinearExecutions,
			VmTrampolineExecutions = native.VmTrampolineExecutions,
			ScalarMethodFastPathInt32 = native.ScalarMethodFastPathI32,
			ScalarMethodFastPathUInt32 = native.ScalarMethodFastPathU32,
			ScalarMethodFastPathDouble = native.ScalarMethodFastPathF64,
			ScalarMethodFastPathMixed = native.ScalarMethodFastPathMixed,
			ScalarFunctionFastPathInt32 = native.ScalarFunctionFastPathI32,
			ScalarFunctionFastPathUInt32 = native.ScalarFunctionFastPathU32,
			ScalarFunctionFastPathDouble = native.ScalarFunctionFastPathF64,
			ScalarFunctionFastPathMixed = native.ScalarFunctionFastPathMixed,
			ScalarNativeFastPathCalls = native.ScalarNativeFastPathCalls,
			DirectNativeCalls = native.DirectNativeCalls,
			InlinedCallSites = native.InlinedCallSites,
			LinearFrameReuses = native.LinearFrameReuses,
			LinearScratchReuses = native.LinearScratchReuses,
			LinearScratchAllocations = native.LinearScratchAllocations,
			LinearArgumentScratchLeases = native.LinearArgumentScratchLeases,
			LinearArgumentScratchReuses = native.LinearArgumentScratchReuses
		};
	}

	private static void EnsureJitCacheCapability()
	{
		if (!VeroScriptRuntime.Capabilities.HasFlag(VeroScriptCapabilities.SessionJitCache))
		{
			throw new NotSupportedException("The loaded VeroScript runtime does not support session JIT-cache controls.");
		}
	}

	public VeroScriptRunResult LoadSource(string source, string fileLabel = "<memory>")
	{
		CheckOwner();
		using var sourceText = new Utf8(source);
		using var labelText = new Utf8(fileLabel);
		NativeRunResult result = NewRunResult();
		CheckRunStatus(NativeMethods.vs_interop_session_load_source(Handle, sourceText.View, labelText.View, &result), result, "load source");
		return ToManaged(result);
	}

	public VeroScriptRunResult LoadFile(string path)
	{
		CheckOwner();
		using var pathText = new Utf8(path);
		NativeRunResult result = NewRunResult();
		CheckRunStatus(NativeMethods.vs_interop_session_load_file(Handle, pathText.View, &result), result, "load file");
		return ToManaged(result);
	}

	public VeroScriptRunResult Run(VeroScriptRunMode mode = VeroScriptRunMode.Script)
	{
		CheckOwner();
		_ = Handle;
		if (mode == VeroScriptRunMode.EvalOnce && (VeroScriptRuntime.Capabilities & VeroScriptCapabilities.EvalOnce) == 0)
		{
			throw new NotSupportedException("The loaded VeroScript runtime does not support one-shot evaluation.");
		}

		NativeRunResult result = NewRunResult();
		CheckRunStatus(NativeMethods.vs_interop_session_run(Handle, mode, &result), result, "run session");
		return ToManaged(result);
	}

	public VeroScriptRunResult Reload()
	{
		CheckOwner();
		NativeRunResult result = NewRunResult();
		CheckRunStatus(NativeMethods.vs_interop_session_reload(Handle, &result), result, "reload session");
		return ToManaged(result);
	}

	/// <summary>Gets the last reload operation and current generations on the creator thread.</summary>
	/// <remarks>A pending candidate settles when initialized. Reading the status preserves diagnostics.</remarks>
	/// <exception cref="NotSupportedException">The native runtime does not advertise reload status.</exception>
	public VeroScriptReloadStatus GetReloadStatus()
	{
		CheckOwner();
		nint handle = Handle;
		if ((VeroScriptRuntime.Capabilities & VeroScriptCapabilities.ReloadStatus) == 0)
		{
			throw new NotSupportedException("The loaded VeroScript runtime does not support reload status.");
		}

		var status = new NativeReloadStatus { StructSize = (uint)sizeof(NativeReloadStatus) };
		CheckStatus(NativeMethods.vs_interop_session_get_reload_status(handle, &status), "get reload status");
		return new(status.State, status.OperationId, status.PreviousGeneration,
			status.CandidateGeneration, status.CurrentGeneration, status.CommittedGeneration);
	}

	public VeroScriptRunResult LoadPackage(VeroScriptPackage package, string logicalPath = "<package>") => LoadPackageCore(package, logicalPath, reload: false);
	public VeroScriptRunResult ReloadPackage(VeroScriptPackage package, string logicalPath = "<package>") => LoadPackageCore(package, logicalPath, reload: true);
	public (byte[] PackageBytes, VeroScriptRunResult Result) BuildPackageFile(string sourcePath, VeroScriptPackageBuildOptions options)
	{
		CheckOwner();
		using var nativePath = new Utf8(sourcePath);
		using var lease = new NativePackageBuildLease(options);
		NativePackageBuildOptions nativeOptions = lease.Value;
		NativeRunResult result = NewRunResult();
		nint buffer = 0;
		try
		{
			CheckRunStatus(NativeMethods.vs_interop_session_build_package_file(Handle, nativePath.View, &nativeOptions, &buffer, &result), result, "build package file");
			return (NativeBuffer.CopyAndDestroy(ref buffer), ToManaged(result));
		}
		finally
		{
			NativeBuffer.Destroy(ref buffer);
		}
	}

	public void Clear()
	{
		CheckOwner();
		CheckStatus(NativeMethods.vs_interop_session_clear(Handle), "clear session");
	}

	public void SetInput(string input)
	{
		CheckOwner();
		using var text = new Utf8(input);
		CheckStatus(NativeMethods.vs_interop_session_set_input(Handle, text.View), "set input");
	}

	public bool HasFunction(string name, uint argumentCount)
	{
		CheckOwner();
		using var functionName = new Utf8(name);
		byte found;
		CheckStatus(NativeMethods.vs_interop_session_has_function(Handle, functionName.View, argumentCount, &found), "query function");
		return found != 0;
	}

	public bool HasMethod(VeroScriptValue instance, string name, uint argumentCount)
	{
		using var nativeLifetime = new NativeLifetime(this, instance);
		CheckOwner();
		ArgumentNullException.ThrowIfNull(instance);
		using var methodName = new Utf8(name);
		byte found;
		CheckStatus(NativeMethods.vs_interop_session_has_method(Handle, instance.Handle, methodName.View, argumentCount, &found), "query method");
		return found != 0;
	}

	public (VeroScriptValue ReturnValue, VeroScriptRunResult Result) CallFunction(string name, params VeroScriptValue[] arguments)
	{
		var returnValue = new VeroScriptValue();
		try
		{
			VeroScriptRunResult result = CallFunction(name, arguments, returnValue);
			return (returnValue, result);
		}
		catch
		{
			returnValue.Dispose();
			throw;
		}
	}

	public VeroScriptRunResult CallFunction(string name, IReadOnlyList<VeroScriptValue> arguments, VeroScriptValue? returnValue)
	{
		using var nativeLifetime = new NativeLifetime(this, returnValue);
		CheckOwner();
		ArgumentNullException.ThrowIfNull(arguments);
		using var functionName = new Utf8(name);
		int argumentCount = arguments.Count;
		Span<nint> handles = argumentCount <= ValueHandleMarshaller.StackAllocationThreshold ? stackalloc nint[argumentCount] : new nint[argumentCount];
		using var handlesLifetime = new NativeLifetime(ValueHandleMarshaller.Copy(arguments, handles, nameof(arguments), "Arguments cannot contain null."));
		NativeRunResult result = NewRunResult();
		fixed (nint* data = handles)
		{
			CheckRunStatus(NativeMethods.vs_interop_session_call_function(Handle, functionName.View, data, (ulong)handles.Length, returnValue?.Handle ?? 0, &result), result, $"call function '{name}'");
		}

		return ToManaged(result);
	}

	public (VeroScriptValue ReturnValue, VeroScriptRunResult Result) CallMethod(VeroScriptValue instance, string name, params VeroScriptValue[] arguments) => CallMethodCore(instance, name, arguments, bypassInterceptor: false);
	public (VeroScriptValue ReturnValue, VeroScriptRunResult Result) CallMethodBypassingInterceptor(VeroScriptValue instance, string name, params VeroScriptValue[] arguments) => CallMethodCore(instance, name, arguments, bypassInterceptor: true);
	public (VeroScriptValue Instance, VeroScriptRunResult Result) Construct(string typeName, params VeroScriptValue[] arguments)
	{
		CheckOwner();
		ArgumentNullException.ThrowIfNull(arguments);
		using var nativeName = new Utf8(typeName);
		Span<nint> handles = arguments.Length <= ValueHandleMarshaller.StackAllocationThreshold ? stackalloc nint[arguments.Length] : new nint[arguments.Length];
		using var handlesLifetime = new NativeLifetime(ValueHandleMarshaller.Copy(arguments, handles, nameof(arguments), "Arguments cannot contain null."));
		var instance = new VeroScriptValue();
		NativeRunResult result = NewRunResult();
		try
		{
			fixed (nint* data = handles)
			{
				CheckRunStatus(NativeMethods.vs_interop_session_construct_object(Handle, nativeName.View, data, (ulong)handles.Length, instance.Handle, &result), result, $"construct '{typeName}'");
			}

			return (instance, ToManaged(result));
		}
		catch
		{
			instance.Dispose();
			throw;
		}
	}

	public (VeroScriptValue Value, VeroScriptRunResult Result) GetField(VeroScriptValue instance, string name)
	{
		using var nativeLifetime = new NativeLifetime(this, instance);
		CheckOwner();
		ArgumentNullException.ThrowIfNull(instance);
		using var fieldName = new Utf8(name);
		var value = new VeroScriptValue();
		NativeRunResult result = NewRunResult();
		try
		{
			CheckRunStatus(NativeMethods.vs_interop_session_get_field(Handle, instance.Handle, fieldName.View, value.Handle, &result), result, $"get field '{name}'");
			return (value, ToManaged(result));
		}
		catch
		{
			value.Dispose();
			throw;
		}
	}

	public VeroScriptRunResult SetField(VeroScriptValue instance, string name, VeroScriptValue value)
	{
		using var nativeLifetime = new NativeLifetime(this, instance, value);
		CheckOwner();
		ArgumentNullException.ThrowIfNull(instance);
		ArgumentNullException.ThrowIfNull(value);
		using var fieldName = new Utf8(name);
		NativeRunResult result = NewRunResult();
		CheckRunStatus(NativeMethods.vs_interop_session_set_field(Handle, instance.Handle, fieldName.View, value.Handle, &result), result, $"set field '{name}'");
		return ToManaged(result);
	}

	public (VeroScriptValue Value, VeroScriptRunResult Result) GetGlobal(string name)
	{
		CheckOwner();
		using var globalName = new Utf8(name);
		var value = new VeroScriptValue();
		NativeRunResult result = NewRunResult();
		try
		{
			CheckRunStatus(NativeMethods.vs_interop_session_get_global(Handle, globalName.View, value.Handle, &result), result, $"get global '{name}'");
			return (value, ToManaged(result));
		}
		catch
		{
			value.Dispose();
			throw;
		}
	}

	public VeroScriptRunResult SetGlobal(string name, VeroScriptValue value)
	{
		using var nativeLifetime = new NativeLifetime(this, value);
		CheckOwner();
		ArgumentNullException.ThrowIfNull(value);
		using var globalName = new Utf8(name);
		NativeRunResult result = NewRunResult();
		CheckRunStatus(NativeMethods.vs_interop_session_set_global(Handle, globalName.View, value.Handle, &result), result, $"set global '{name}'");
		return ToManaged(result);
	}

	public VeroScriptRunResult ApplyClassDefaults(VeroScriptValue instance)
	{
		using var nativeLifetime = new NativeLifetime(this, instance);
		CheckOwner();
		ArgumentNullException.ThrowIfNull(instance);
		NativeRunResult result = NewRunResult();
		CheckRunStatus(NativeMethods.vs_interop_session_apply_class_defaults(Handle, instance.Handle, &result), result, "apply class defaults");
		return ToManaged(result);
	}

	public void RegisterFunction(string name, VeroScriptNativeType returnType, IReadOnlyList<VeroScriptNativeParameter> parameters, VeroScriptNativeFunction callback)
	{
		CheckOwner();
		ArgumentNullException.ThrowIfNull(parameters);
		ArgumentNullException.ThrowIfNull(callback);
		VeroScriptNativeParameter[] parameterSnapshot = parameters.ToArray();
		using var functionName = new Utf8(name);
		using var types = new NativeTypeMarshaller();
		NativeTypeDescriptor nativeReturn = types.Marshal(returnType);
		NativeParameterDescriptor[] nativeParameters = new NativeParameterDescriptor[parameterSnapshot.Length];
		for (int i = 0; i < parameterSnapshot.Length; ++i)
		{
			nativeParameters[i] = new NativeParameterDescriptor
			{
				StructSize = (uint)sizeof(NativeParameterDescriptor),
				Type = types.Marshal(parameterSnapshot[i].Type),
				Flags = parameterSnapshot[i].Flags
			};
		}

		string operation = $"register native function '{name}'";
		GCHandle callbackHandle = GCHandle.Alloc(callback);
		try
		{
			TrackCallback(callbackHandle);
			fixed (NativeParameterDescriptor* parameterData = nativeParameters)
			{
				CheckStatus(NativeMethods.vs_interop_session_register_native(Handle, functionName.View, &nativeReturn, parameterData, (ulong)nativeParameters.Length, &OnNativeCall, GCHandle.ToIntPtr(callbackHandle)), operation);
			}
		}
		catch
		{
			ReleaseTrackedCallback(callbackHandle);
			throw;
		}
	}

	public void RegisterHostFunction(VeroScriptHostFunctionDefinition definition)
	{
		CheckOwner();
		ArgumentNullException.ThrowIfNull(definition);
		ArgumentNullException.ThrowIfNull(definition.Callback);
		string operation = $"register host function '{definition.Name}'";
		GCHandle callbackHandle = GCHandle.Alloc(definition.Callback);
		try
		{
			TrackCallback(callbackHandle);
			using var lease = new NativeBindingLease();
			NativeHostFunctionDescriptor descriptor = lease.MakeFunction(definition, &OnNativeCall, GCHandle.ToIntPtr(callbackHandle));
			CheckStatus(NativeMethods.vs_interop_session_register_host_function(Handle, &descriptor), operation);
		}
		catch
		{
			ReleaseTrackedCallback(callbackHandle);
			throw;
		}
	}

	public void RegisterAnnotation(VeroScriptAnnotationDefinition definition)
	{
		CheckOwner();
		ArgumentNullException.ThrowIfNull(definition);
		using var lease = new NativeBindingLease();
		NativeAnnotationDescriptor descriptor = lease.MakeAnnotationDefinition(definition);
		CheckStatus(NativeMethods.vs_interop_session_register_annotation(Handle, &descriptor), $"register annotation '{definition.Name}'");
	}

	public uint RegisterHostType(VeroScriptHostTypeDefinition definition)
	{
		CheckOwner();
		ArgumentNullException.ThrowIfNull(definition);
		using var lease = new NativeBindingLease();
		NativeHostTypeDescriptor descriptor = lease.MakeType(definition);
		uint typeId;
		CheckStatus(NativeMethods.vs_interop_session_register_host_type(Handle, &descriptor, &typeId), $"register host type '{definition.Name}'");
		return typeId;
	}

	public void RegisterHostMember(uint typeId, VeroScriptHostMemberDefinition definition)
	{
		CheckOwner();
		ArgumentNullException.ThrowIfNull(definition);
		string operation = $"register host member '{definition.Name}'";
		GCHandle callbackHandle = default;
		nint userData = 0;
		delegate* unmanaged[Cdecl]<nint, nint, nint, uint> callback = null;
		if (definition.Callback is not null)
		{
			callbackHandle = GCHandle.Alloc(definition.Callback);
			userData = GCHandle.ToIntPtr(callbackHandle);
			callback = &OnNativeCall;
		}

		try
		{
			if (callbackHandle.IsAllocated)
			{
				TrackCallback(callbackHandle);
			}

			using var lease = new NativeBindingLease();
			NativeHostMemberDescriptor descriptor = lease.MakeMember(definition, callback, userData);
			CheckStatus(NativeMethods.vs_interop_session_register_host_member(Handle, typeId, &descriptor), operation);
		}
		catch
		{
			if (callbackHandle.IsAllocated)
			{
				ReleaseTrackedCallback(callbackHandle);
			}

			throw;
		}
	}

	/// <summary>Registers native host value protocols and retains their managed callbacks until the final stamped value releases them.</summary>
	/// <remarks>Requires a runtime with callback retention support when callbacks are supplied.
	/// Callbacks may run after session disposal; they must not access the disposed session.
	/// A native retention failure after registration closes the session to protect callback data.</remarks>
	public void RegisterHostProtocol(uint typeId, VeroScriptHostTypeProtocolDefinition definition)
	{
		CheckOwner();
		ArgumentNullException.ThrowIfNull(definition);
		EnsureProtocolLifetimeSupported(definition);
		var handles = new List<ProtocolCallbackOwner>(4);
		var protocol = new NativeHostTypeProtocol
		{
			StructSize = (uint)sizeof(NativeHostTypeProtocol)
		};
		bool registered = false;
		try
		{
			AddProtocolCallback(definition.CloneCallback, handles, out protocol.CloneCallback, out protocol.CloneUserData);
			AddProtocolCallback(definition.EqualsCallback, handles, out protocol.EqualsCallback, out protocol.EqualsUserData);
			AddProtocolCallback(definition.HashCallback, handles, out protocol.HashCallback, out protocol.HashUserData);
			AddProtocolCallback(definition.SubclassFactoryCallback, handles, out protocol.SubclassFactoryCallback, out protocol.SubclassFactoryUserData);
			_protocolCallbacks.EnsureCapacity(checked(_protocolCallbacks.Count + handles.Count));
			_protocolCallbacks.AddRange(handles);
			CheckStatus(NativeMethods.vs_interop_session_register_host_protocol(Handle, typeId, &protocol), "register host type protocol");
			registered = true;
			foreach (ProtocolCallbackOwner callback in handles)
			{
				callback.Retain(this);
			}
		}
		catch
		{
			if (registered)
			{
				// A protocol was installed, but its roots could not all be
				// transferred. Close the session before releasing managed data.
				Dispose();
			}
			else
			{
				foreach (ProtocolCallbackOwner callback in handles)
				{
					_protocolCallbacks.Remove(callback);
					callback.ReleaseManaged();
				}
			}

			throw;
		}
	}

	public void RegisterTypeAlias(string name, IReadOnlyList<string> typeParameters, string targetTypeName, string documentation = "")
	{
		CheckOwner();
		using var lease = new NativeBindingLease();
		NativeStringView nativeName = lease.MarshalString(name);
		NativeStringList nativeParameters = lease.MarshalStringList(typeParameters);
		NativeStringView nativeTarget = lease.MarshalString(targetTypeName);
		NativeStringView nativeDocumentation = lease.MarshalString(documentation);
		CheckStatus(NativeMethods.vs_interop_session_register_type_alias(Handle, nativeName, nativeParameters, nativeTarget, nativeDocumentation), $"register type alias '{name}'");
	}

	public uint RegisterGenericHostTypeTemplate(string name, uint arity, string documentation, VeroScriptGenericHostTypeFactory factory)
	{
		CheckOwner();
		ArgumentNullException.ThrowIfNull(factory);
		using var lease = new NativeBindingLease();
		NativeStringView nativeName = lease.MarshalString(name);
		NativeStringView nativeDocumentation = lease.MarshalString(documentation);
		string operation = $"register generic host type template '{name}'";
		GCHandle stateHandle = GCHandle.Alloc(new GenericFactoryState(this, factory));
		try
		{
			TrackCallback(stateHandle);
			uint templateId;
			CheckStatus(NativeMethods.vs_interop_session_register_generic_host_type_template(Handle, nativeName, arity, nativeDocumentation, &OnGenericHostType, GCHandle.ToIntPtr(stateHandle), &templateId), operation);
			return templateId;
		}
		catch
		{
			ReleaseTrackedCallback(stateHandle);
			throw;
		}
	}

	public uint InstantiateGenericHostType(uint templateId, IReadOnlyList<string> typeArguments)
	{
		CheckOwner();
		using var callbackTransaction = new GenericCallbackTransaction(this);
		using var lease = new NativeBindingLease();
		NativeStringList arguments = lease.MarshalStringList(typeArguments);
		uint typeId;
		CheckStatus(NativeMethods.vs_interop_session_instantiate_generic_host_type(Handle, templateId, arguments, &typeId), "instantiate generic host type");
		callbackTransaction.Commit();
		return typeId;
	}

	public void SetInstanceCallInterceptor(VeroScriptInstanceInterceptor? interceptor)
	{
		CheckOwner();
		GCHandle replacement = default;
		nint userData = 0;
		delegate* unmanaged[Cdecl]<nint, nint, nint, uint> callback = null;
		if (interceptor is not null)
		{
			replacement = GCHandle.Alloc(interceptor);
			userData = GCHandle.ToIntPtr(replacement);
			callback = &OnInstanceIntercept;
		}

		try
		{
			CheckStatus(NativeMethods.vs_interop_session_set_instance_call_interceptor(Handle, callback, userData), "set instance call interceptor");
		}
		catch
		{
			if (replacement.IsAllocated)
			{
				replacement.Free();
			}

			throw;
		}

		if (_interceptorHandle.IsAllocated)
		{
			_interceptorHandle.Free();
		}

		_interceptorHandle = replacement;
	}

	public void SetMutationHandler(VeroScriptMutationHandler? handler)
	{
		CheckOwner();
		GCHandle replacement = default;
		nint userData = 0;
		delegate* unmanaged[Cdecl]<NativeMutationEvent*, nint, void> callback = null;
		if (handler is not null)
		{
			replacement = GCHandle.Alloc(handler);
			userData = GCHandle.ToIntPtr(replacement);
			callback = &OnMutation;
		}

		try
		{
			CheckStatus(NativeMethods.vs_interop_session_set_mutation_callback(Handle, callback, userData), "set mutation callback");
		}
		catch
		{
			if (replacement.IsAllocated)
			{
				replacement.Free();
			}

			throw;
		}

		if (_mutationHandle.IsAllocated)
		{
			_mutationHandle.Free();
		}

		_mutationHandle = replacement;
	}

	public void RegisterExternalSerializer(string typeName, uint version, VeroScriptExternalSerializeHandler serialize, VeroScriptExternalDeserializeHandler deserialize)
	{
		CheckOwner();
		ArgumentNullException.ThrowIfNull(serialize);
		ArgumentNullException.ThrowIfNull(deserialize);
		using var nativeTypeName = new Utf8(typeName);
		string operation = $"register external serializer for '{typeName}'";
		GCHandle stateHandle = GCHandle.Alloc(new ExternalSerializerState(serialize, deserialize));
		try
		{
			TrackCallback(stateHandle);
			CheckStatus(NativeMethods.vs_interop_session_register_external_serializer(Handle, nativeTypeName.View, version, &OnExternalSerialize, &OnExternalDeserialize, GCHandle.ToIntPtr(stateHandle)), operation);
		}
		catch
		{
			ReleaseTrackedCallback(stateHandle);
			throw;
		}
	}

	public (byte[] Bytes, uint Version) SerializeObject(VeroScriptValue instance)
	{
		using var nativeLifetime = new NativeLifetime(this, instance);
		CheckOwner();
		ArgumentNullException.ThrowIfNull(instance);
		nint buffer = 0;
		uint version;
		try
		{
			CheckStatus(NativeMethods.vs_interop_session_serialize_object(Handle, instance.Handle, &buffer, &version), "serialize object");
			return (NativeBuffer.CopyAndDestroy(ref buffer), version);
		}
		finally
		{
			NativeBuffer.Destroy(ref buffer);
		}
	}

	public void DeserializeObject(VeroScriptValue instance, ReadOnlySpan<byte> bytes, uint encodedVersion)
	{
		using var nativeLifetime = new NativeLifetime(this, instance);
		CheckOwner();
		ArgumentNullException.ThrowIfNull(instance);
		fixed (byte* data = bytes)
		{
			CheckStatus(NativeMethods.vs_interop_session_deserialize_object(Handle, instance.Handle, new NativeByteView(data, (ulong)bytes.Length), encodedVersion), "deserialize object");
		}
	}

	public void EnumerateObjectReferences(VeroScriptValue instance, VeroScriptExternalReferenceVisitor visitor)
	{
		using var nativeLifetime = new NativeLifetime(this, instance);
		CheckOwner();
		ArgumentNullException.ThrowIfNull(instance);
		ArgumentNullException.ThrowIfNull(visitor);
		var state = new ExternalReferenceEnumerationState(visitor);
		GCHandle visitorHandle = GCHandle.Alloc(state);
		try
		{
			VeroScriptStatus status = NativeMethods.vs_interop_session_enumerate_object_references(Handle, instance.Handle, &OnExternalReference, GCHandle.ToIntPtr(visitorHandle));
			state.Exception?.Throw();
			CheckStatus(status, "enumerate object references");
		}
		finally
		{
			visitorHandle.Free();
		}
	}

	public VeroScriptCallable RetainCallable(VeroScriptValue functionValue, bool ownerThreadOnly = true, bool weakThis = false)
	{
		using var nativeLifetime = new NativeLifetime(this, functionValue);
		CheckOwner();
		ArgumentNullException.ThrowIfNull(functionValue);
		nint callable = 0;
		VeroScriptStatus status = NativeMethods.vs_interop_session_retain_callable(Handle, functionValue.Handle, ownerThreadOnly ? (byte)1 : (byte)0, weakThis ? (byte)1 : (byte)0, &callable);
		if (status != VeroScriptStatus.Ok)
		{
			if (callable != 0)
			{
				NativeMethods.vs_interop_callable_destroy(callable);
			}

			CheckStatus(status, "retain callable");
		}

		if (callable == 0)
		{
			throw new VeroScriptException(VeroScriptStatus.InternalError, "VeroScript retained a null callable handle.");
		}

		try
		{
			return new VeroScriptCallable(callable, ownerThreadOnly, _ownerThreadId);
		}
		catch
		{
			NativeMethods.vs_interop_callable_destroy(callable);
			throw;
		}
	}

	public IReadOnlyList<VeroScriptDiagnostic> GetDiagnostics()
	{
		CheckOwner();
		ulong count = NativeMethods.vs_interop_session_diagnostic_count(Handle);
		if (count > int.MaxValue)
		{
			throw new OverflowException("Too many native diagnostics.");
		}

		var diagnostics = new List<VeroScriptDiagnostic>((int)count);
		for (ulong i = 0; i < count; ++i)
		{
			var value = new NativeDiagnostic
			{
				StructSize = (uint)sizeof(NativeDiagnostic)
			};
			CheckStatus(NativeMethods.vs_interop_session_get_diagnostic(Handle, i, &value), "read diagnostic");
			diagnostics.Add(new(Utf8.Copy(value.Message), Utf8.Copy(value.MessageId), Utf8.Copy(value.FileLabel), value.Line, value.Column) { Severity = ReadDiagnosticSeverity(i) });
		}

		return diagnostics;
	}

	private VeroScriptDiagnosticSeverity ReadDiagnosticSeverity(ulong index)
	{
		if (Volatile.Read(ref _diagnosticSeverityUnavailable) != 0)
		{
			return VeroScriptDiagnosticSeverity.Error;
		}

		try
		{
			uint severity;
			CheckStatus(NativeMethods.vs_interop_session_get_diagnostic_severity(Handle, index, &severity), "read diagnostic severity");
			return (VeroScriptDiagnosticSeverity)severity;
		}
		catch (EntryPointNotFoundException)
		{
			// Older same-ABI runtimes expose only error diagnostics. Cache the
			// missing entry point to avoid an exception for each diagnostic.
			Volatile.Write(ref _diagnosticSeverityUnavailable, 1);
			return VeroScriptDiagnosticSeverity.Error;
		}
	}

	public IReadOnlyList<string> GetDiagnosticArguments(ulong diagnosticIndex)
	{
		CheckOwner();
		ulong count;
		CheckStatus(NativeMethods.vs_interop_session_diagnostic_argument_count(Handle, diagnosticIndex, &count), "get diagnostic argument count");
		if (count > int.MaxValue)
		{
			throw new OverflowException("Too many diagnostic arguments.");
		}

		var arguments = new List<string>((int)count);
		for (ulong i = 0; i < count; ++i)
		{
			NativeStringView argument;
			CheckStatus(NativeMethods.vs_interop_session_get_diagnostic_argument(Handle, diagnosticIndex, i, &argument), "get diagnostic argument");
			arguments.Add(Utf8.Copy(argument));
		}

		return arguments;
	}

	public string FormatDiagnostic(ulong diagnosticIndex)
	{
		CheckOwner();
		nint buffer = 0;
		try
		{
			CheckStatus(NativeMethods.vs_interop_session_format_diagnostic(Handle, diagnosticIndex, &buffer), "format diagnostic");
			return NativeBuffer.CopyUtf8AndDestroy(ref buffer);
		}
		finally
		{
			NativeBuffer.Destroy(ref buffer);
		}
	}

	public string ExportBindingsJson()
	{
		CheckOwner();
		nint buffer = 0;
		try
		{
			CheckStatus(NativeMethods.vs_interop_session_export_bindings_json(Handle, &buffer), "export bindings JSON");
			return NativeBuffer.CopyUtf8AndDestroy(ref buffer);
		}
		finally
		{
			NativeBuffer.Destroy(ref buffer);
		}
	}

	public string ExportReflectionJson()
	{
		CheckOwner();
		nint buffer = 0;
		try
		{
			CheckStatus(NativeMethods.vs_interop_session_export_reflection_json(Handle, &buffer), "export reflection JSON");
			return NativeBuffer.CopyUtf8AndDestroy(ref buffer);
		}
		finally
		{
			NativeBuffer.Destroy(ref buffer);
		}
	}

	public string GetLastError()
	{
		CheckOwner();
		ulong required = 0;
		VeroScriptStatus status = NativeMethods.vs_interop_session_copy_last_error(Handle, null, 0, &required);
		if (status != VeroScriptStatus.Ok && status != VeroScriptStatus.BufferTooSmall)
		{
			return status.ToString();
		}

		if (required == 0)
		{
			return string.Empty;
		}

		if (required > int.MaxValue)
		{
			throw new OverflowException("The native error is too large.");
		}

		byte[] bytes = new byte[(int)required];
		fixed (byte* data = bytes)
		{
			VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_session_copy_last_error(Handle, data, required, &required), "copy last error");
		}

		if (required > (ulong)bytes.Length)
		{
			throw new VeroScriptException(VeroScriptStatus.InternalError, "The native error size grew while it was being copied.");
		}

		int length = checked((int)required);
		if (length > 0 && bytes[length - 1] == 0)
		{
			--length;
		}

		return Utf8.Decode(bytes.AsSpan(0, length));
	}

	public void Dispose()
	{
		if (_handle == 0)
		{
			return;
		}

		CheckOwner();
		nint handle = _handle;
		VeroScriptStatus status = NativeMethods.vs_interop_session_destroy(handle);
		if (status != VeroScriptStatus.Ok)
		{
			CheckStatus(status, "destroy session");
		}

		_handle = 0;
		foreach (ProtocolCallbackOwner protocol in _protocolCallbacks)
		{
			protocol.ReleaseManaged();
		}

		_protocolCallbacks.Clear();
		lock (_callbackHandleSync)
		{
			foreach (GCHandle callback in _callbackHandles)
			{
				callback.Free();
			}

			_callbackHandles.Clear();
		}

		if (_interceptorHandle.IsAllocated)
		{
			_interceptorHandle.Free();
		}

		if (_mutationHandle.IsAllocated)
		{
			_mutationHandle.Free();
		}

		if (_outputHandle.IsAllocated)
		{
			_outputHandle.Free();
		}
	}

	private nint Handle => _handle != 0 ? _handle : throw new ObjectDisposedException(nameof(VeroScriptSession));

	private void CheckOwner()
	{
		if (Environment.CurrentManagedThreadId != _ownerThreadId)
		{
			throw new VeroScriptException(VeroScriptStatus.WrongThread, "VeroScript sessions may only be used and disposed on their creator thread.");
		}
	}

	private void CheckStatus(VeroScriptStatus status, string operation)
	{
		if (status == VeroScriptStatus.Ok)
		{
			return;
		}

		string detail = _handle == 0 ? string.Empty : GetLastError();
		throw new VeroScriptException(status, string.IsNullOrWhiteSpace(detail) ? $"VeroScript failed to {operation}: {status}." : $"VeroScript failed to {operation}: {detail}");
	}

	private void CheckRunStatus(VeroScriptStatus status, NativeRunResult result, string operation)
	{
		// Compilation and script/runtime failures are represented by Success=false
		// so callers can inspect structured diagnostics without exception control flow.
		if (status == VeroScriptStatus.Ok || (status == VeroScriptStatus.EngineError && result.Success == 0))
		{
			return;
		}

		CheckStatus(status, operation);
	}

	private static void AddProtocolCallback(VeroScriptNativeFunction? callback, List<ProtocolCallbackOwner> handles, out delegate* unmanaged[Cdecl]<nint, nint, nint, uint> nativeCallback, out nint userData)
	{
		if (callback is null)
		{
			nativeCallback = null;
			userData = 0;
			return;
		}

		var handle = new ProtocolCallbackOwner(callback);
		try
		{
			handles.Add(handle);
		}
		catch
		{
			handle.ReleaseManaged();
			throw;
		}

		nativeCallback = &OnNativeCall;
		userData = handle.Pointer;
	}

	private void EnsureProtocolLifetimeSupported(VeroScriptHostTypeProtocolDefinition definition)
	{
		_ = Handle;
		if (definition.CloneCallback is null && definition.EqualsCallback is null && definition.HashCallback is null && definition.SubclassFactoryCallback is null)
		{
			return;
		}

		if (Volatile.Read(ref _protocolLifetimeSupport) == 0)
		{
			try
			{
				// Null user data probes this optional entry point without
				// transferring ownership or changing the registry.
				CheckStatus(NativeMethods.vs_interop_session_retain_callback_user_data(Handle, 0, &ReleaseProtocolUserData), "probe host protocol callback lifetime");
			}
			catch (EntryPointNotFoundException)
			{
				Volatile.Write(ref _protocolLifetimeSupport, -1);
			}
			catch (VeroScriptException exception)when (exception.Status == VeroScriptStatus.InvalidArgument)
			{
				Volatile.Write(ref _protocolLifetimeSupport, 1);
			}
		}

		if (Volatile.Read(ref _protocolLifetimeSupport) < 0)
		{
			throw new NotSupportedException("The loaded VeroScript runtime cannot retain managed host protocol callbacks.");
		}
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static void ReleaseProtocolUserData(nint userData)
	{
		try
		{
			((ProtocolCallbackOwner)GCHandle.FromIntPtr(userData).Target!).ReleaseNative();
		}
		catch
		{
		}
	}

	internal void TrackCallback(GCHandle callback)
	{
		try
		{
			lock (_callbackHandleSync)
			{
				_callbackHandles.Add(callback);
			}
		}
		catch
		{
			callback.Free();
			throw;
		}
	}

	private void TrackCallbacks(List<GCHandle> callbacks)
	{
		lock (_callbackHandleSync)
		{
			_callbackHandles.EnsureCapacity(checked(_callbackHandles.Count + callbacks.Count));
			_callbackHandles.AddRange(callbacks);
		}
	}

	internal void TrackGenericCallback(GCHandle callback) => (_activeGenericCallbackTransaction ?? throw new InvalidOperationException("Generic callbacks may only be retained during type instantiation.")).Add(callback);
	internal void ReleaseGenericCallback(GCHandle callback)
	{
		bool removed = _activeGenericCallbackTransaction?.Remove(callback) ?? false;
		if (!removed)
		{
			lock (_callbackHandleSync)
			{
				_callbackHandles.Remove(callback);
			}
		}

		if (callback.IsAllocated)
		{
			callback.Free();
		}
	}

	internal void ReleaseTrackedCallback(GCHandle callback)
	{
		bool removed;
		lock (_callbackHandleSync)
		{
			removed = _callbackHandles.Remove(callback);
		}

		// Reentrant disposal may already have freed the root while user-supplied
		// binding metadata was being copied. A GCHandle copy does not know that.
		if (removed)
		{
			callback.Free();
		}
	}

	internal void SetGenericContextProtocol(nint context, VeroScriptHostTypeProtocolDefinition definition)
	{
		EnsureProtocolLifetimeSupported(definition);
		var handles = new List<ProtocolCallbackOwner>(4);
		var protocol = new NativeHostTypeProtocol
		{
			StructSize = (uint)sizeof(NativeHostTypeProtocol)
		};
		try
		{
			AddProtocolCallback(definition.CloneCallback, handles, out protocol.CloneCallback, out protocol.CloneUserData);
			AddProtocolCallback(definition.EqualsCallback, handles, out protocol.EqualsCallback, out protocol.EqualsUserData);
			AddProtocolCallback(definition.HashCallback, handles, out protocol.HashCallback, out protocol.HashUserData);
			AddProtocolCallback(definition.SubclassFactoryCallback, handles, out protocol.SubclassFactoryCallback, out protocol.SubclassFactoryUserData);
			(_activeGenericCallbackTransaction ?? throw new InvalidOperationException("Generic protocols require an active type instantiation.")).AddProtocols(handles);
			VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_generic_type_context_set_protocol(context, &protocol), "set generic host type protocol");
		}
		catch
		{
			foreach (ProtocolCallbackOwner callback in handles)
			{
				callback.ReleaseManaged();
			}

			throw;
		}
	}

	private (VeroScriptValue ReturnValue, VeroScriptRunResult Result) CallMethodCore(VeroScriptValue instance, string name, VeroScriptValue[] arguments, bool bypassInterceptor)
	{
		using var nativeLifetime = new NativeLifetime(this, instance);
		CheckOwner();
		ArgumentNullException.ThrowIfNull(instance);
		ArgumentNullException.ThrowIfNull(arguments);
		using var methodName = new Utf8(name);
		Span<nint> handles = arguments.Length <= ValueHandleMarshaller.StackAllocationThreshold ? stackalloc nint[arguments.Length] : new nint[arguments.Length];
		using var handlesLifetime = new NativeLifetime(ValueHandleMarshaller.Copy(arguments, handles, nameof(arguments), "Arguments cannot contain null."));
		var returnValue = new VeroScriptValue();
		NativeRunResult result = NewRunResult();
		try
		{
			VeroScriptStatus status;
			fixed (nint* data = handles)
			{
				status = bypassInterceptor ? NativeMethods.vs_interop_session_call_method_bypassing_interceptor(Handle, instance.Handle, methodName.View, data, (ulong)handles.Length, returnValue.Handle, &result) : NativeMethods.vs_interop_session_call_method(Handle, instance.Handle, methodName.View, data, (ulong)handles.Length, returnValue.Handle, &result);
			}

			CheckRunStatus(status, result, $"call method '{name}'");
			return (returnValue, ToManaged(result));
		}
		catch
		{
			returnValue.Dispose();
			throw;
		}
	}

	private VeroScriptRunResult LoadPackageCore(VeroScriptPackage package, string logicalPath, bool reload)
	{
		using var nativeLifetime = new NativeLifetime(this, package);
		CheckOwner();
		ArgumentNullException.ThrowIfNull(package);
		package.EnsureModuleAvailable();
		using var path = new Utf8(logicalPath);
		NativeRunResult result = NewRunResult();
		VeroScriptStatus status = reload ? NativeMethods.vs_interop_session_reload_package(Handle, package.Handle, path.View, &result) : NativeMethods.vs_interop_session_load_package(Handle, package.Handle, path.View, &result);
		CheckRunStatus(status, result, reload ? "reload package" : "load package");
		if (status == VeroScriptStatus.Ok && result.Success != 0)
		{
			package.MarkModuleConsumed();
		}

		return ToManaged(result);
	}

	private static NativeRunResult NewRunResult() => new()
	{
		StructSize = (uint)sizeof(NativeRunResult)
	};
	private static VeroScriptRunResult ToManaged(NativeRunResult result) => new(result.Success != 0, result.ExitRequested != 0, result.ExitCode, result.DiagnosticCount);
	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static void OnPrint(NativeStringView line, nint userData) => DispatchLine(line, userData, false);
	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static void OnDiagnostic(NativeStringView line, nint userData) => DispatchLine(line, userData, true);
	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static byte OnConsoleQuery(NativeConsoleInfo* info, nint userData)
	{
		try
		{
			if (info is null || info->StructSize < (uint)sizeof(NativeConsoleInfo))
			{
				return 0;
			}

			var options = (VeroScriptSessionOptions)GCHandle.FromIntPtr(userData).Target!;
			VeroScriptConsoleInfo? value = options.ConsoleQuery?.Invoke();
			if (value is null)
			{
				return 0;
			}

			info->Flags = (value.OutputTerminal ? 1u : 0u) | (value.Color ? 2u : 0u) | (value.Cursor ? 4u : 0u) | (value.Clear ? 8u : 0u);
			info->Width = value.Width;
			info->Height = value.Height;
			return 1;
		}
		catch
		{
			return 0;
		}
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static byte OnConsoleControl(uint operation, int first, int second, nint userData)
	{
		try
		{
			var options = (VeroScriptSessionOptions)GCHandle.FromIntPtr(userData).Target!;
			return options.ConsoleControl?.Invoke((VeroScriptConsoleOperation)operation, first, second) == true ? (byte)1 : (byte)0;
		}
		catch
		{
			return 0;
		}
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static byte OnConsoleWrite(NativeStringView text, nint userData)
	{
		try
		{
			var options = (VeroScriptSessionOptions)GCHandle.FromIntPtr(userData).Target!;
			return options.ConsoleWrite?.Invoke(Utf8.Copy(text)) == true ? (byte)1 : (byte)0;
		}
		catch
		{
			return 0;
		}
	}

	private static void DispatchLine(NativeStringView line, nint userData, bool diagnostic)
	{
		try
		{
			var options = (VeroScriptSessionOptions)GCHandle.FromIntPtr(userData).Target!;
			(diagnostic ? options.Diagnostic : options.Print)?.Invoke(Utf8.Copy(line));
		}
		catch
		{ /* Managed exceptions must never cross the C ABI. */
		}
	}

	private static Utf8 CreateCallbackError(Exception exception)
	{
		const string fallback = "Managed callback failed.";
		string message = fallback;
		try
		{
			string? detail = exception.Message;
			if (!string.IsNullOrWhiteSpace(detail))
			{
				message = detail;
			}
		}
		catch
		{ /* Even a custom Exception.Message implementation is untrusted here. */
		}

		try
		{
			return new Utf8(message);
		}
		catch (ArgumentException)
		{
			return new Utf8(fallback);
		}
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static uint OnNativeCall(nint nativeContext, nint outReturn, nint userData)
	{
		BorrowScope? scope = null;
		try
		{
			scope = new BorrowScope();
			object target = GCHandle.FromIntPtr(userData).Target!;
			VeroScriptNativeFunction callback = target is ProtocolCallbackOwner protocol ? protocol.Callback : (VeroScriptNativeFunction)target;
			var context = new VeroScriptCallContext(nativeContext, scope);
			callback(context, new VeroScriptBorrowedValue(outReturn, scope));
			return context.HasError ? 1u : 0u;
		}
		catch (Exception exception)
		{
			try
			{
				using var message = CreateCallbackError(exception);
				NativeMethods.vs_interop_call_context_set_error(nativeContext, message.View);
			}
			catch
			{
			}

			return 1;
		}
		finally
		{
			scope?.Expire();
		}
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static uint OnInstanceIntercept(nint nativeContext, nint outReturn, nint userData)
	{
		BorrowScope? scope = null;
		try
		{
			scope = new BorrowScope();
			var callback = (VeroScriptInstanceInterceptor)GCHandle.FromIntPtr(userData).Target!;
			var context = new VeroScriptInterceptContext(nativeContext, scope);
			VeroScriptInterceptResult result = callback(context, new VeroScriptBorrowedValue(outReturn, scope));
			if (!Enum.IsDefined(result))
			{
				context.SetError("The managed interceptor returned an invalid result.");
				return (uint)VeroScriptInterceptResult.Failed;
			}

			return (uint)(context.HasError ? VeroScriptInterceptResult.Failed : result);
		}
		catch (Exception exception)
		{
			try
			{
				using var message = CreateCallbackError(exception);
				NativeMethods.vs_interop_intercept_context_set_error(nativeContext, message.View);
			}
			catch
			{
			}

			return (uint)VeroScriptInterceptResult.Failed;
		}
		finally
		{
			scope?.Expire();
		}
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static void OnMutation(NativeMutationEvent* nativeEvent, nint userData)
	{
		if (nativeEvent is null)
		{
			return;
		}

		BorrowScope? scope = null;
		try
		{
			scope = new BorrowScope();
			var callback = (VeroScriptMutationHandler)GCHandle.FromIntPtr(userData).Target!;
			callback(new VeroScriptMutationEvent { Object = nativeEvent->Object != 0 ? new VeroScriptBorrowedValue(nativeEvent->Object, scope) : throw new VeroScriptException(VeroScriptStatus.InternalError, "The mutation event object is unavailable."), ObjectTypeName = Utf8.Copy(nativeEvent->ObjectTypeName), FieldName = Utf8.Copy(nativeEvent->FieldName), FieldIndex = nativeEvent->FieldIndex, Kind = nativeEvent->Kind, Value = nativeEvent->Value == 0 ? null : new VeroScriptBorrowedValue(nativeEvent->Value, scope), ObjectMutationVersion = nativeEvent->ObjectMutationVersion });
		}
		catch
		{ /* Mutation notifications cannot report failures across the ABI. */
		}
		finally
		{
			scope?.Expire();
		}
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static uint OnGenericHostType(nint nativeContext, nint userData)
	{
		BorrowScope? scope = null;
		try
		{
			scope = new BorrowScope();
			var state = (GenericFactoryState)GCHandle.FromIntPtr(userData).Target!;
			var context = new VeroScriptGenericHostTypeContext(nativeContext, state.Session, scope);
			state.Factory(context);
			return context.HasError ? 1u : 0u;
		}
		catch (Exception exception)
		{
			try
			{
				using var message = CreateCallbackError(exception);
				NativeMethods.vs_interop_generic_type_context_set_error(nativeContext, message.View);
			}
			catch
			{
			}

			return 1;
		}
		finally
		{
			scope?.Expire();
		}
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static uint OnExternalSerialize(nint nativeContext, nint userData)
	{
		BorrowScope? scope = null;
		try
		{
			scope = new BorrowScope();
			var state = (ExternalSerializerState)GCHandle.FromIntPtr(userData).Target!;
			var context = new VeroScriptExternalSerializeContext(nativeContext, scope);
			state.Serialize(context);
			return context.HasError ? 1u : 0u;
		}
		catch (Exception exception)
		{
			try
			{
				using var message = CreateCallbackError(exception);
				NativeMethods.vs_interop_external_serialize_context_set_error(nativeContext, message.View);
			}
			catch
			{
			}

			return 1;
		}
		finally
		{
			scope?.Expire();
		}
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static uint OnExternalDeserialize(nint nativeContext, nint userData)
	{
		BorrowScope? scope = null;
		try
		{
			scope = new BorrowScope();
			var state = (ExternalSerializerState)GCHandle.FromIntPtr(userData).Target!;
			var context = new VeroScriptExternalDeserializeContext(nativeContext, scope);
			state.Deserialize(context);
			return context.HasError ? 1u : 0u;
		}
		catch (Exception exception)
		{
			try
			{
				using var message = CreateCallbackError(exception);
				NativeMethods.vs_interop_external_deserialize_context_set_error(nativeContext, message.View);
			}
			catch
			{
			}

			return 1;
		}
		finally
		{
			scope?.Expire();
		}
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static uint OnExternalReference(VeroScriptExternalReferenceKind kind, nint value, nint userData)
	{
		BorrowScope? scope = null;
		try
		{
			scope = new BorrowScope();
			if (value == 0)
			{
				return 1;
			}

			var state = (ExternalReferenceEnumerationState)GCHandle.FromIntPtr(userData).Target!;
			state.Visitor(kind, new VeroScriptBorrowedValue(value, scope));
			return 0;
		}
		catch (Exception exception)
		{
			try
			{
				var state = (ExternalReferenceEnumerationState)GCHandle.FromIntPtr(userData).Target!;
				state.Capture(exception);
			}
			catch
			{
			}

			return 1;
		}
		finally
		{
			scope?.Expire();
		}
	}

	private static int _diagnosticSeverityUnavailable;
	private static int _protocolLifetimeSupport;
}
