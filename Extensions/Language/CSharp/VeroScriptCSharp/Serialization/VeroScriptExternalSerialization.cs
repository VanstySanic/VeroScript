// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript.Interop;

namespace VeroScript;

/// <summary>Describes one field offered to an external serializer.</summary>
public readonly record struct VeroScriptSerializedField(
	string Name, string DeclaredType, string RuntimeType,
	uint FieldIndex, VeroScriptBorrowedValue Value);

/// <summary>Provides object fields and accepts externally encoded bytes during serialization.</summary>
public sealed unsafe class VeroScriptExternalSerializeContext
{
	private readonly nint _handle;
	private readonly BorrowScope _scope;
	internal bool HasError { get; private set; }

	internal VeroScriptExternalSerializeContext(nint handle, BorrowScope scope)
		=> (_handle, _scope) = (handle, scope);

	public string TypeName
	{
		get
		{
			_scope.ThrowIfUnavailable();
			return Utf8.Copy(NativeMethods.vs_interop_external_serialize_context_type_name(_handle));
		}
	}
	public ulong MutationVersion
	{
		get
		{
			_scope.ThrowIfUnavailable();
			return NativeMethods.vs_interop_external_serialize_context_mutation_version(_handle);
		}
	}
	public ulong FieldCount
	{
		get
		{
			_scope.ThrowIfUnavailable();
			return NativeMethods.vs_interop_external_serialize_context_field_count(_handle);
		}
	}

	public VeroScriptSerializedField GetField(ulong index)
	{
		_scope.ThrowIfUnavailable();
		NativeStringView name, declaredType, runtimeType;
		uint fieldIndex;
		nint value;
		VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_external_serialize_context_get_field(
			_handle, index, &name, &declaredType, &runtimeType, &fieldIndex, &value),
			"get external serialization field");
		if (value == 0)
		{
			throw new VeroScriptException(VeroScriptStatus.InternalError,
						"The external serialization field handle is unavailable.");
		}

		return new(Utf8.Copy(name), Utf8.Copy(declaredType), Utf8.Copy(runtimeType),
					fieldIndex, new VeroScriptBorrowedValue(value, _scope));
	}

	public void SetBytes(ReadOnlySpan<byte> bytes)
	{
		_scope.ThrowIfUnavailable();
		fixed (byte* data = bytes)
		{
			VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_external_serialize_context_set_bytes(
						_handle, new NativeByteView(data, (ulong)bytes.Length)),
						"set external serialization bytes");
		}
	}

	public void SetError(string message)
	{
		_scope.ThrowIfUnavailable();
		using var text = new Utf8(message);
		NativeMethods.vs_interop_external_serialize_context_set_error(_handle, text.View);
		HasError = true;
	}
}

/// <summary>Provides externally encoded bytes and accepts reconstructed fields during deserialization.</summary>
public sealed unsafe class VeroScriptExternalDeserializeContext
{
	private readonly nint _handle;
	private readonly BorrowScope _scope;
	internal bool HasError { get; private set; }

	internal VeroScriptExternalDeserializeContext(nint handle, BorrowScope scope)
		=> (_handle, _scope) = (handle, scope);

	public uint EncodedVersion
	{
		get
		{
			_scope.ThrowIfUnavailable();
			return NativeMethods.vs_interop_external_deserialize_context_encoded_version(_handle);
		}
	}

	public byte[] GetBytes()
	{
		_scope.ThrowIfUnavailable();
		NativeByteView view = NativeMethods.vs_interop_external_deserialize_context_bytes(_handle);
		if (view.Size > int.MaxValue)
		{
			throw new OverflowException("External serialized data is too large.");
		}

		if (view.Size != 0 && view.Data is null)
		{
			throw new VeroScriptException(VeroScriptStatus.InternalError,
						"External serialized data returned a null pointer.");
		}

		return view.Size == 0 ? [] : new ReadOnlySpan<byte>(view.Data, (int)view.Size).ToArray();
	}

	public void AddField(string fieldName, VeroScriptValue value)
	{
		using var nativeLifetime = new NativeLifetime(this, value);
		_scope.ThrowIfUnavailable();
		ArgumentNullException.ThrowIfNull(value);
		using var name = new Utf8(fieldName);
		VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_external_deserialize_context_add_field(
			_handle, name.View, value.Handle), "add external deserialization field");
	}

	public void SetError(string message)
	{
		_scope.ThrowIfUnavailable();
		using var text = new Utf8(message);
		NativeMethods.vs_interop_external_deserialize_context_set_error(_handle, text.View);
		HasError = true;
	}
}
