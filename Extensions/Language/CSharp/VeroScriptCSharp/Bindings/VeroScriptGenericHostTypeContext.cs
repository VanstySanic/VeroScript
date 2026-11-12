// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using VeroScript.Interop;

namespace VeroScript;

/// <summary>Builds one concrete host type inside a generic-host-type factory callback.</summary>
public sealed unsafe class VeroScriptGenericHostTypeContext
{
	private readonly nint _handle;
	private readonly VeroScriptSession _session;
	private readonly BorrowScope _scope;
	internal bool HasError { get; private set; }

	internal VeroScriptGenericHostTypeContext(nint handle, VeroScriptSession session, BorrowScope scope)
		=> (_handle, _session, _scope) = (handle, session, scope);

	public ulong ArgumentCount
	{
		get
		{
			_scope.ThrowIfUnavailable();
			return NativeMethods.vs_interop_generic_type_context_argument_count(_handle);
		}
	}

	public string GetArgument(ulong index)
	{
		_scope.ThrowIfUnavailable();
		NativeStringView argument;
		VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_generic_type_context_get_argument(
			_handle, index, &argument), "get generic type argument");
		return Utf8.Copy(argument);
	}

	public void SetType(VeroScriptHostTypeDefinition definition)
	{
		_scope.ThrowIfUnavailable();
		ArgumentNullException.ThrowIfNull(definition);
		using var lease = new NativeBindingLease();
		NativeHostTypeDescriptor descriptor = lease.MakeType(definition);
		VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_generic_type_context_set_type(
			_handle, &descriptor), "set generic host type");
	}

	public void AddMember(VeroScriptHostMemberDefinition definition)
	{
		_scope.ThrowIfUnavailable();
		ArgumentNullException.ThrowIfNull(definition);
		GCHandle callbackHandle = default;
		nint userData = 0;
		delegate* unmanaged[Cdecl]<nint, nint, nint, uint> callback = null;
		if (definition.Callback is not null)
		{
			callbackHandle = GCHandle.Alloc(definition.Callback);
			callback = VeroScriptSession.NativeCallback;
			userData = GCHandle.ToIntPtr(callbackHandle);
		}
		try
		{
			if (callbackHandle.IsAllocated)
			{
				_session.TrackGenericCallback(callbackHandle);
			}
			using var lease = new NativeBindingLease();
			NativeHostMemberDescriptor descriptor = lease.MakeMember(definition, callback, userData);
			VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_generic_type_context_add_member(
				_handle, &descriptor), "add generic host member");
		}
		catch
		{
			if (callbackHandle.IsAllocated)
			{
				_session.ReleaseGenericCallback(callbackHandle);
			}
			throw;
		}
	}

	/// <summary>Sets protocols whose managed callbacks remain rooted through the last stamped native value.</summary>
	/// <remarks>Callbacks can run after the session is disposed. Requires native callback-retention support.</remarks>
	public void SetProtocol(VeroScriptHostTypeProtocolDefinition definition)
	{
		_scope.ThrowIfUnavailable();
		ArgumentNullException.ThrowIfNull(definition);
		_session.SetGenericContextProtocol(_handle, definition);
	}

	public void SetError(string message)
	{
		_scope.ThrowIfUnavailable();
		using var text = new Utf8(message);
		NativeMethods.vs_interop_generic_type_context_set_error(_handle, text.View);
		HasError = true;
	}
}
