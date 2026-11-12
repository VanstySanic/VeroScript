// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript.Interop;

namespace VeroScript;

/// <summary>Owns a retained script callable that can be invoked after its source callback returns.</summary>
/// <remarks>
/// A callable retained without owner-thread confinement still requires host-side
/// serialization with every other operation on its associated session.
/// </remarks>
public sealed unsafe class VeroScriptCallable : IDisposable
{
	private nint _handle;
	private readonly bool _ownerThreadOnly;
	private readonly int _ownerThreadId;
	private readonly object _sync = new();
	private readonly object _invocationSync = new();
	private int _activeInvocations;
	private nint _pendingDestroy;

	internal VeroScriptCallable(nint handle, bool ownerThreadOnly, int ownerThreadId)
		=> (_handle, _ownerThreadOnly, _ownerThreadId) = (handle, ownerThreadOnly, ownerThreadId);

	~VeroScriptCallable() => Release();

	public bool IsValid
	{
		get
		{
			lock (_sync)
			{
				return _handle != 0
								&& NativeMethods.vs_interop_callable_is_valid(_handle) != 0;
			}
		}
	}

	public (VeroScriptValue ReturnValue, VeroScriptRunResult Result) Invoke(params VeroScriptValue[] arguments)
	{
		ArgumentNullException.ThrowIfNull(arguments);
		if (_ownerThreadOnly && Environment.CurrentManagedThreadId != _ownerThreadId)
		{
			throw new VeroScriptException(VeroScriptStatus.WrongThread,
						"This retained callable may only be invoked on its owner thread.");
		}

		Span<nint> nativeArguments = arguments.Length <= ValueHandleMarshaller.StackAllocationThreshold
					? stackalloc nint[arguments.Length]
					: new nint[arguments.Length];
		using var nativeArgumentsLifetime = new NativeLifetime(ValueHandleMarshaller.Copy(arguments, nativeArguments, nameof(arguments),
			"Arguments cannot contain null."));
		var returnValue = new VeroScriptValue();
		var result = new NativeRunResult { StructSize = (uint)sizeof(NativeRunResult) };
		try
		{
			VeroScriptStatus status;
			lock (_invocationSync)
			{
				nint handle;
				lock (_sync)
				{
					ObjectDisposedException.ThrowIf(_handle == 0, this);
					handle = _handle;
					++_activeInvocations;
				}
				try
				{
					// Native execution can wait for a callback that disposes this wrapper
					// on another thread. Keep its lifetime lock out of that wait cycle.
					fixed (nint* data = nativeArguments)
					{
						status = NativeMethods.vs_interop_callable_invoke(handle, data,
												(ulong)nativeArguments.Length, returnValue.Handle, &result);
					}
				}
				finally
				{
					nint pendingDestroy = 0;
					lock (_sync)
					{
						--_activeInvocations;
						if (_activeInvocations == 0)
						{
							pendingDestroy = _pendingDestroy;
							_pendingDestroy = 0;
						}
					}
					if (pendingDestroy != 0)
					{
						NativeMethods.vs_interop_callable_destroy(pendingDestroy);
					}
				}
			}
			if (status != VeroScriptStatus.Ok
				&& !(status == VeroScriptStatus.EngineError && result.Success == 0))
			{
				VeroScriptValue.ThrowIfFailed(status, "invoke retained callable");
			}

			return (returnValue, new(result.Success != 0, result.ExitRequested != 0,
							result.ExitCode, result.DiagnosticCount));
		}
		catch
		{
			returnValue.Dispose();
			throw;
		}
	}

	/// <summary>Invalidates this wrapper, deferring native destruction until an active invocation finishes.</summary>
	public void Dispose()
	{
		Release();
		GC.SuppressFinalize(this);
	}

	private void Release()
	{
		// Field initialization can fail before the constructor owns a handle.
		// Such partially constructed objects may still reach the finalizer.
		if (Volatile.Read(ref _handle) == 0) return;
		nint destroy = 0;
		lock (_sync)
		{
			nint handle = Interlocked.Exchange(ref _handle, 0);
			if (handle == 0) return;
			if (_activeInvocations == 0)
			{
				destroy = handle;
			}
			else
			{
				_pendingDestroy = handle;
			}
		}
		if (destroy != 0)
		{
			NativeMethods.vs_interop_callable_destroy(destroy);
		}
	}
}
