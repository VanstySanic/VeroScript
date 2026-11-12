// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript.Interop;

namespace VeroScript;

/// <summary>Exposes the receiver, target, arguments, and continuation of an intercepted call.</summary>
public sealed unsafe class VeroScriptInterceptContext
{
	private readonly nint _handle;
	private readonly BorrowScope _scope;
	internal bool HasError { get; private set; }

	internal VeroScriptInterceptContext(nint handle, BorrowScope scope)
		=> (_handle, _scope) = (handle, scope);

	public VeroScriptBorrowedValue Receiver
	{
		get
		{
			_scope.ThrowIfUnavailable();
			nint value = NativeMethods.vs_interop_intercept_context_receiver(_handle);
			if (value == 0)
			{
				throw new VeroScriptException(VeroScriptStatus.InternalError,
				"The interceptor receiver is unavailable.");
			}

			return new(value, _scope);
		}
	}

	public ulong ArgumentCount
	{
		get
		{
			_scope.ThrowIfUnavailable();
			return NativeMethods.vs_interop_intercept_context_argument_count(_handle);
		}
	}

	public VeroScriptResolvedMethod GetTarget()
	{
		_scope.ThrowIfUnavailable();
		var target = new NativeResolvedMethod { StructSize = (uint)sizeof(NativeResolvedMethod) };
		VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_intercept_context_get_target(
			_handle, &target), "get intercepted method target");
		return new(target.ProgramGeneration, target.ModuleIdentity,
			target.DeclaringTypeIdentity, target.MethodIdentity, target.FunctionIndex,
			Utf8.Copy(target.ModuleName), Utf8.Copy(target.DeclaringTypeName),
			Utf8.Copy(target.MethodName), Utf8.Copy(target.QualifiedName),
			target.IsVirtual != 0, target.IsConst != 0, target.ReturnsVoid != 0);
	}

	public VeroScriptBorrowedValue GetArgument(ulong index)
	{
		_scope.ThrowIfUnavailable();
		nint value = 0;
		VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_intercept_context_get_argument(
			_handle, index, &value), "get interceptor argument");
		if (value == 0)
		{
			throw new VeroScriptException(VeroScriptStatus.InternalError,
						"The interceptor argument handle is unavailable.");
		}

		return new(value, _scope);
	}

	public void Proceed(VeroScriptBorrowedValue returnValue)
	{
		_scope.ThrowIfUnavailable();
		ArgumentNullException.ThrowIfNull(returnValue);
		VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_intercept_context_proceed(
			_handle, returnValue.Handle), "proceed with intercepted call");
	}

	public void SetError(string message)
	{
		_scope.ThrowIfUnavailable();
		using var text = new Utf8(message);
		NativeMethods.vs_interop_intercept_context_set_error(_handle, text.View);
		HasError = true;
	}
}

/// <summary>Describes one object or container mutation observed by the host.</summary>
public sealed class VeroScriptMutationEvent
{
	public required VeroScriptBorrowedValue Object { get; init; }
	public required string ObjectTypeName { get; init; }
	public required string FieldName { get; init; }
	public required uint FieldIndex { get; init; }
	public required VeroScriptMutationKind Kind { get; init; }
	public VeroScriptBorrowedValue? Value { get; init; }
	public required ulong ObjectMutationVersion { get; init; }
}
