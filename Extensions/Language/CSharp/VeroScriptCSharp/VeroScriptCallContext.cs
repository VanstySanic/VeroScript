// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript.Interop;

namespace VeroScript;

/// <summary>Exposes callback arguments and nested invocation for a managed host function.</summary>
/// <remarks>Use this context and its borrowed values only on the active callback thread.</remarks>
public sealed unsafe class VeroScriptCallContext
{
	private readonly nint _handle;
	private readonly BorrowScope _scope;
	internal bool HasError { get; private set; }

	internal VeroScriptCallContext(nint handle, BorrowScope scope) => (_handle, _scope) = (handle, scope);

	public VeroScriptMemory Memory => new(this);
	internal nint MemoryCallHandle
	{
		get
		{
			_scope.ThrowIfUnavailable();
			return _handle;
		}
	}

	public ulong GetArgumentAddress(ulong index)
	{
		using var lifetime = new NativeLifetime(this);
		nint handle = MemoryCallHandle;
		VeroScriptMemory.EnsureSupported();
		ulong address;
		VeroScriptValue.ThrowIfFailed(
			NativeMethods.vs_interop_memory_call_argument_address(handle, index, &address),
			"query original argument address");
		return address;
	}

	public ulong ArgumentCount
	{
		get
		{
			_scope.ThrowIfUnavailable();
			return NativeMethods.vs_interop_call_context_argument_count(_handle);
		}
	}
	public ulong ProgramGeneration
	{
		get
		{
			_scope.ThrowIfUnavailable();
			return NativeMethods.vs_interop_call_context_program_generation(_handle);
		}
	}

	public T? GetHostThis<T>() where T : class
	{
		_scope.ThrowIfUnavailable();
		nint payload = NativeMethods.vs_interop_call_context_host_this(_handle);
		return VeroScriptValue.GetManagedHostPayload<T>(payload);
	}

	public VeroScriptBorrowedValue GetArgument(ulong index)
	{
		_scope.ThrowIfUnavailable();
		nint value = 0;
		VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_call_context_get_argument(_handle, index, &value), "get callback argument");
		if (value == 0)
		{
			throw new VeroScriptException(VeroScriptStatus.InternalError,
						"The callback argument handle is unavailable.");
		}

		return new(value, _scope);
	}

	public VeroScriptBorrowedValue GetMutableArgument(ulong index)
	{
		_scope.ThrowIfUnavailable();
		nint value = 0;
		VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_call_context_get_mutable_argument(_handle, index, &value), "get mutable callback argument");
		if (value == 0)
		{
			throw new VeroScriptException(VeroScriptStatus.InternalError,
						"The mutable callback argument handle is unavailable.");
		}

		return new(value, _scope);
	}

	public void SetError(string message)
	{
		_scope.ThrowIfUnavailable();
		using var text = new Utf8(message);
		NativeMethods.vs_interop_call_context_set_error(_handle, text.View);
		HasError = true;
	}

	public VeroScriptValue InvokeFunction(string name, params VeroScriptValue[] arguments)
	{
		_scope.ThrowIfUnavailable();
		ArgumentNullException.ThrowIfNull(arguments);
		using var functionName = new Utf8(name);
		Span<nint> handles = arguments.Length <= ValueHandleMarshaller.StackAllocationThreshold
			? stackalloc nint[arguments.Length]
			: new nint[arguments.Length];
		using var handlesLifetime = new NativeLifetime(ValueHandleMarshaller.Copy(arguments, handles, nameof(arguments),
			"Arguments cannot contain null."));
		var result = new VeroScriptValue();
		try
		{
			fixed (nint* data = handles)
			{
				VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_call_context_invoke_function(
								_handle, functionName.View, data, (ulong)handles.Length, result.Handle), $"invoke nested function '{name}'");
			}

			return result;
		}
		catch
		{
			result.Dispose();
			throw;
		}
	}

	public VeroScriptValue InvokeCallable(VeroScriptValue functionValue, params VeroScriptValue[] arguments)
	{
		using var nativeLifetime = new NativeLifetime(this, functionValue);
		_scope.ThrowIfUnavailable();
		ArgumentNullException.ThrowIfNull(functionValue);
		ArgumentNullException.ThrowIfNull(arguments);
		Span<nint> handles = arguments.Length <= ValueHandleMarshaller.StackAllocationThreshold
			? stackalloc nint[arguments.Length]
			: new nint[arguments.Length];
		using var handlesLifetime = new NativeLifetime(ValueHandleMarshaller.Copy(arguments, handles, nameof(arguments),
			"Arguments cannot contain null."));
		var result = new VeroScriptValue();
		try
		{
			fixed (nint* data = handles)
			{
				VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_call_context_invoke_callable(
								_handle, functionValue.Handle, data, (ulong)handles.Length, result.Handle),
								"invoke nested callable");
			}

			return result;
		}
		catch
		{
			result.Dispose();
			throw;
		}
	}
}
