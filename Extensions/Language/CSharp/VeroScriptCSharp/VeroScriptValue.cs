// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Collections.Concurrent;
using VeroScript.Interop;

namespace VeroScript;

/// <summary>Owns a dynamically typed value allocated by the VeroScript Interop ABI.</summary>
/// <remarks>
/// Instances are not generally thread-safe. Channel operations are synchronized by
/// the native runtime and may be called from multiple threads while the wrapper stays
/// alive and remains a channel; do not concurrently reset, replace, or dispose it.
/// Native calls retain their managed owners through completion, including blocking
/// channel operations. Input value owners are retained while their handles are marshalled and used.
/// Replacing an input array element does not release the wrapper whose handle was marshalled.
/// Module-dependent payloads remain bound to their session's creator thread and
/// become unavailable when it closes, including empty containers with module-value slots.
/// Cloning creates an owned handle without removing these payload restrictions.
/// Dispose owned values when no longer needed.
/// </remarks>
public unsafe partial class VeroScriptValue : IDisposable
{
	private sealed class MapEnumerationState(
		Action<VeroScriptBorrowedValue, VeroScriptBorrowedValue> visitor)
	{
		private ExceptionDispatchInfo? _exception;
		internal Action<VeroScriptBorrowedValue, VeroScriptBorrowedValue> Visitor { get; } = visitor;
		internal ExceptionDispatchInfo? Exception => Volatile.Read(ref _exception);
		internal void Capture(Exception exception)
			=> Interlocked.CompareExchange(ref _exception,
				ExceptionDispatchInfo.Capture(exception), null);
	}

	private sealed class SetEnumerationState(Action<VeroScriptBorrowedValue> visitor)
	{
		private ExceptionDispatchInfo? _exception;
		internal Action<VeroScriptBorrowedValue> Visitor { get; } = visitor;
		internal ExceptionDispatchInfo? Exception => Volatile.Read(ref _exception);
		internal void Capture(Exception exception)
			=> Interlocked.CompareExchange(ref _exception,
				ExceptionDispatchInfo.Capture(exception), null);
	}

	private sealed record ManagedHostObject(object Value, Func<string, bool>? IsType);

	private static readonly ConcurrentDictionary<nint, ManagedHostObject> ManagedHostObjects = new();
	private nint _handle;
	private readonly bool _ownsHandle;
	private readonly BorrowScope? _scope;

	public VeroScriptValue()
	{
		VeroScriptRuntime.EnsureCompatible();
		nint handle = 0;
		VeroScriptStatus status = NativeMethods.vs_interop_value_create(&handle);
		if (status != VeroScriptStatus.Ok)
		{
			if (handle != 0)
			{
				NativeMethods.vs_interop_value_destroy(handle);
			}

			ThrowIfFailed(status, "create value");
		}
		if (handle == 0)
		{
			throw new VeroScriptException(VeroScriptStatus.InternalError,
						"VeroScript created a null value handle.");
		}

		_handle = handle;
		_ownsHandle = true;
	}

	internal VeroScriptValue(nint handle, bool ownsHandle, BorrowScope? scope = null)
		=> (_handle, _ownsHandle, _scope) = (handle, ownsHandle, scope);

	~VeroScriptValue() => Release();

	internal nint Handle
	{
		get
		{
			ObjectDisposedException.ThrowIf(_handle == 0, this);
			_scope?.ThrowIfUnavailable();
			return _handle;
		}
	}

	public VeroScriptValueKind Kind
	{
		get
		{
			using var nativeLifetime = new NativeLifetime(this);
			return NativeMethods.vs_interop_value_get_kind(Handle);
		}
	}

	public string TypeName
	{
		get
		{
			using var nativeLifetime = new NativeLifetime(this);
			NativeStringView view;
			ThrowIfFailed(NativeMethods.vs_interop_value_get_type_name(Handle, &view), "get value type");
			return Utf8.Copy(view);
		}
	}

	public void Reset()
	{
		using var nativeLifetime = new NativeLifetime(this);
		ThrowIfFailed(NativeMethods.vs_interop_value_reset(Handle), "reset value");
	}

	public VeroScriptValue Clone()
	{
		using var nativeLifetime = new NativeLifetime(this);
		nint clone = 0;
		VeroScriptStatus status = NativeMethods.vs_interop_value_clone(Handle, &clone);
		if (status != VeroScriptStatus.Ok)
		{
			if (clone != 0)
			{
				NativeMethods.vs_interop_value_destroy(clone);
			}

			ThrowIfFailed(status, "clone value");
		}
		if (clone == 0)
		{
			throw new VeroScriptException(VeroScriptStatus.InternalError,
						"VeroScript cloned a null value handle.");
		}

		try
		{
			return new VeroScriptValue(clone, true);
		}
		catch
		{
			NativeMethods.vs_interop_value_destroy(clone);
			throw;
		}
	}

	/// <summary>Replaces this value with the complete runtime value held by <paramref name="source"/>.</summary>
	/// <remarks>
	/// This preserves the runtime's container, object, callable, and channel identity semantics.
	/// The destination must be writable; read-only borrowed callback values are rejected.
	/// </remarks>
	public void Set(VeroScriptValue source)
	{
		using var nativeLifetime = new NativeLifetime(this, source);
		ArgumentNullException.ThrowIfNull(source);
		ThrowIfFailed(NativeMethods.vs_interop_value_assign(Handle, source.Handle), "assign value");
	}

	public void Set(sbyte value)
	{
		using var nativeLifetime = new NativeLifetime(this);
		ThrowIfFailed(NativeMethods.vs_interop_value_set_int8(Handle, value), "set int8");
	}
	public void Set(byte value)
	{
		using var nativeLifetime = new NativeLifetime(this);
		ThrowIfFailed(NativeMethods.vs_interop_value_set_uint8(Handle, value), "set uint8");
	}
	public void Set(short value)
	{
		using var nativeLifetime = new NativeLifetime(this);
		ThrowIfFailed(NativeMethods.vs_interop_value_set_int16(Handle, value), "set int16");
	}
	public void Set(ushort value)
	{
		using var nativeLifetime = new NativeLifetime(this);
		ThrowIfFailed(NativeMethods.vs_interop_value_set_uint16(Handle, value), "set uint16");
	}
	public void Set(int value)
	{
		using var nativeLifetime = new NativeLifetime(this);
		ThrowIfFailed(NativeMethods.vs_interop_value_set_int32(Handle, value), "set int32");
	}
	public void Set(uint value)
	{
		using var nativeLifetime = new NativeLifetime(this);
		ThrowIfFailed(NativeMethods.vs_interop_value_set_uint32(Handle, value), "set uint32");
	}
	public void Set(long value)
	{
		using var nativeLifetime = new NativeLifetime(this);
		ThrowIfFailed(NativeMethods.vs_interop_value_set_int64(Handle, value), "set int64");
	}
	public void Set(ulong value)
	{
		using var nativeLifetime = new NativeLifetime(this);
		ThrowIfFailed(NativeMethods.vs_interop_value_set_uint64(Handle, value), "set uint64");
	}
	public void Set(float value)
	{
		using var nativeLifetime = new NativeLifetime(this);
		ThrowIfFailed(NativeMethods.vs_interop_value_set_float(Handle, value), "set float");
	}
	public void Set(double value)
	{
		using var nativeLifetime = new NativeLifetime(this);
		ThrowIfFailed(NativeMethods.vs_interop_value_set_double(Handle, value), "set double");
	}
	public void Set(bool value)
	{
		using var nativeLifetime = new NativeLifetime(this);
		ThrowIfFailed(NativeMethods.vs_interop_value_set_bool(Handle, value ? (byte)1 : (byte)0), "set bool");
	}

	public void SetString(string value)
	{
		using var nativeLifetime = new NativeLifetime(this);
		using var utf8 = new Utf8(value, allowEmbeddedNul: true);
		ThrowIfFailed(NativeMethods.vs_interop_value_set_string(Handle, utf8.View), "set string");
	}

	public void SetWideString(string value)
	{
		using var nativeLifetime = new NativeLifetime(this);
		Utf16.Validate(value, nameof(value));
		fixed (char* data = value)
		{
			ThrowIfFailed(NativeMethods.vs_interop_value_set_wstring(Handle, data, (ulong)value.Length), "set wstring");
		}
	}

	public void SetEnum(string typeName, long value)
	{
		using var nativeLifetime = new NativeLifetime(this);
		using var name = new Utf8(typeName);
		ThrowIfFailed(NativeMethods.vs_interop_value_set_enum(Handle, name.View, value), "set enum");
	}

	public sbyte GetInt8()
	{
		using var nativeLifetime = new NativeLifetime(this);
		sbyte v;
		ThrowIfFailed(NativeMethods.vs_interop_value_get_int8(Handle, &v), "get int8");
		return v;
	}
	public byte GetUInt8()
	{
		using var nativeLifetime = new NativeLifetime(this);
		byte v;
		ThrowIfFailed(NativeMethods.vs_interop_value_get_uint8(Handle, &v), "get uint8");
		return v;
	}
	public short GetInt16()
	{
		using var nativeLifetime = new NativeLifetime(this);
		short v;
		ThrowIfFailed(NativeMethods.vs_interop_value_get_int16(Handle, &v), "get int16");
		return v;
	}
	public ushort GetUInt16()
	{
		using var nativeLifetime = new NativeLifetime(this);
		ushort v;
		ThrowIfFailed(NativeMethods.vs_interop_value_get_uint16(Handle, &v), "get uint16");
		return v;
	}
	public int GetInt32()
	{
		using var nativeLifetime = new NativeLifetime(this);
		int v;
		ThrowIfFailed(NativeMethods.vs_interop_value_get_int32(Handle, &v), "get int32");
		return v;
	}
	public uint GetUInt32()
	{
		using var nativeLifetime = new NativeLifetime(this);
		uint v;
		ThrowIfFailed(NativeMethods.vs_interop_value_get_uint32(Handle, &v), "get uint32");
		return v;
	}
	public long GetInt64()
	{
		using var nativeLifetime = new NativeLifetime(this);
		long v;
		ThrowIfFailed(NativeMethods.vs_interop_value_get_int64(Handle, &v), "get int64");
		return v;
	}
	public ulong GetUInt64()
	{
		using var nativeLifetime = new NativeLifetime(this);
		ulong v;
		ThrowIfFailed(NativeMethods.vs_interop_value_get_uint64(Handle, &v), "get uint64");
		return v;
	}
	public float GetFloat()
	{
		using var nativeLifetime = new NativeLifetime(this);
		float v;
		ThrowIfFailed(NativeMethods.vs_interop_value_get_float(Handle, &v), "get float");
		return v;
	}
	public double GetDouble()
	{
		using var nativeLifetime = new NativeLifetime(this);
		double v;
		ThrowIfFailed(NativeMethods.vs_interop_value_get_double(Handle, &v), "get double");
		return v;
	}
	public bool GetBool()
	{
		using var nativeLifetime = new NativeLifetime(this);
		byte v;
		ThrowIfFailed(NativeMethods.vs_interop_value_get_bool(Handle, &v), "get bool");
		return v != 0;
	}

	public string GetString()
	{
		using var nativeLifetime = new NativeLifetime(this);
		NativeStringView view;
		ThrowIfFailed(NativeMethods.vs_interop_value_get_string(Handle, &view), "get string");
		return Utf8.Copy(view);
	}

	public string GetWideString()
	{
		using var nativeLifetime = new NativeLifetime(this);
		char* data;
		ulong size;
		ThrowIfFailed(NativeMethods.vs_interop_value_get_wstring(Handle, &data, &size), "get wstring");
		return Utf16.Copy(data, size);
	}

	public VeroScriptEnumValue GetEnum()
	{
		using var nativeLifetime = new NativeLifetime(this);
		NativeStringView name;
		long value;
		ThrowIfFailed(NativeMethods.vs_interop_value_get_enum(Handle, &name, &value), "get enum");
		return new(Utf8.Copy(name), value);
	}

	public void SetHostObject(uint typeId, string typeName, object value)
		=> SetHostObject(typeId, typeName, value, null);

	/// <summary>Stores a managed host object with an optional script subtype predicate.</summary>
	/// <param name="typeId">The host type identifier registered with VeroScript.</param>
	/// <param name="typeName">The canonical script host type name.</param>
	/// <param name="value">The managed object retained by the native value.</param>
	/// <param name="isType">Returns true when the object is assignable to the requested script type name.</param>
	public void SetHostObject(uint typeId, string typeName, object value,
		Func<string, bool>? isType)
	{
		using var nativeLifetime = new NativeLifetime(this);
		ArgumentNullException.ThrowIfNull(value);
		using var nativeTypeName = new Utf8(typeName);
		GCHandle payload = GCHandle.Alloc(value);
		nint payloadPointer = GCHandle.ToIntPtr(payload);
		bool registered = false;
		try
		{
			if (!ManagedHostObjects.TryAdd(payloadPointer, new ManagedHostObject(value, isType)))
			{
				throw new VeroScriptException(VeroScriptStatus.InternalError,
								"The managed host object payload could not be registered.");
			}

			registered = true;
			VeroScriptStatus status = NativeMethods.vs_interop_value_set_host_object_ex(Handle,
				typeId, nativeTypeName.View, payloadPointer, &DestroyHostObject,
				isType is null ? null : &HostObjectIsType,
				isType is null ? 0 : payloadPointer);
			if (status == VeroScriptStatus.Ok) return;
			if (ManagedHostObjects.TryRemove(payloadPointer, out _))
			{
				payload.Free();
			}

			payload = default;
			registered = false;
			ThrowIfFailed(status, "set host object");
		}
		catch
		{
			bool release = registered
				? ManagedHostObjects.TryRemove(payloadPointer, out _)
				: payload.IsAllocated;
			if (release && payload.IsAllocated)
			{
				payload.Free();
			}

			throw;
		}
	}

	public VeroScriptHostObjectInfo GetHostObject()
	{
		using var nativeLifetime = new NativeLifetime(this);
		uint typeId;
		NativeStringView typeName;
		nint payload;
		ThrowIfFailed(NativeMethods.vs_interop_value_get_host_object(Handle,
			&typeId, &typeName, &payload), "get host object");
		if (!ManagedHostObjects.TryGetValue(payload, out ManagedHostObject? hostObject))
		{
			throw new VeroScriptException(VeroScriptStatus.Unsupported,
						"The host object payload was not created by VeroScriptCSharp.");
		}

		return new(typeId, Utf8.Copy(typeName), hostObject.Value);
	}

	public T GetHostObject<T>() where T : class
	{
		VeroScriptHostObjectInfo info = GetHostObject();
		return info.Value as T ?? throw new VeroScriptException(VeroScriptStatus.TypeMismatch,
			$"The host object is not assignable to {typeof(T).FullName}.");
	}

	public void SetArray(string elementType, IReadOnlyList<VeroScriptValue> elements)
	{
		using var nativeLifetime = new NativeLifetime(this);
		ArgumentNullException.ThrowIfNull(elements);
		using var type = new Utf8(elementType);
		int elementCount = elements.Count;
		Span<nint> handles = elementCount <= ValueHandleMarshaller.StackAllocationThreshold
			? stackalloc nint[elementCount]
			: new nint[elementCount];
		using var handlesLifetime = new NativeLifetime(ValueHandleMarshaller.Copy(elements, handles, nameof(elements),
			"Value lists cannot contain null."));
		fixed (nint* data = handles)
		{
			ThrowIfFailed(NativeMethods.vs_interop_value_create_array(Handle,
						type.View, data, (ulong)handles.Length), "create array");
		}
	}

	/// <summary>Replaces this value with the script <c>nullptr</c> value.</summary>
	public void SetNull()
	{
		using var nativeLifetime = new NativeLifetime(this);
		ThrowIfFailed(NativeMethods.vs_interop_value_set_null(Handle), "set null");
	}

	/// <summary>Returns whether this value represents script <c>nullptr</c>.</summary>
	public bool IsNull
	{
		get
		{
			using var nativeLifetime = new NativeLifetime(this);
			byte isNull;
			ThrowIfFailed(NativeMethods.vs_interop_value_is_null(Handle, &isNull), "query null");
			return isNull != 0;
		}
	}

	public void SetSequence(VeroScriptValueKind kind, string elementType, IReadOnlyList<VeroScriptValue> elements)
	{
		using var nativeLifetime = new NativeLifetime(this);
		ArgumentNullException.ThrowIfNull(elements);
		using var type = new Utf8(elementType);
		int elementCount = elements.Count;
		Span<nint> handles = elementCount <= ValueHandleMarshaller.StackAllocationThreshold
			? stackalloc nint[elementCount]
			: new nint[elementCount];
		using var handlesLifetime = new NativeLifetime(ValueHandleMarshaller.Copy(elements, handles, nameof(elements),
			"Value lists cannot contain null."));
		fixed (nint* data = handles)
		{
			ThrowIfFailed(NativeMethods.vs_interop_value_create_sequence(Handle, kind,
						type.View, data, (ulong)handles.Length), "create sequence");
		}
	}

	public ulong SequenceCount
	{
		get
		{
			using var nativeLifetime = new NativeLifetime(this);
			ulong count;
			ThrowIfFailed(NativeMethods.vs_interop_value_sequence_count(Handle, &count), "get sequence count");
			return count;
		}
	}

	public VeroScriptValue GetSequenceElement(ulong index)
	{
		using var nativeLifetime = new NativeLifetime(this);
		var result = new VeroScriptValue();
		try
		{
			ThrowIfFailed(NativeMethods.vs_interop_value_sequence_get(Handle, index, result.Handle), "get sequence element");
			return result;
		}
		catch
		{
			result.Dispose();
			throw;
		}
	}

	public void SetMap(string keyType, string valueType,
		IReadOnlyList<VeroScriptValue> keys, IReadOnlyList<VeroScriptValue> values,
		bool ordered = false)
	{
		using var nativeLifetime = new NativeLifetime(this);
		ArgumentNullException.ThrowIfNull(keys);
		ArgumentNullException.ThrowIfNull(values);
		int keyCount = keys.Count;
		int valueCount = values.Count;
		if (keyCount != valueCount)
		{
			throw new ArgumentException("Map keys and values must have the same count.");
		}

		Span<nint> keyHandles = keyCount <= ValueHandleMarshaller.StackAllocationThreshold
					? stackalloc nint[keyCount]
					: new nint[keyCount];
		Span<nint> valueHandles = valueCount <= ValueHandleMarshaller.StackAllocationThreshold
			? stackalloc nint[valueCount]
			: new nint[valueCount];
		VeroScriptValue[] keyOwners = ValueHandleMarshaller.Copy(keys, keyHandles, nameof(keys),
			"Value lists cannot contain null.");
		using var keyHandlesLifetime = new NativeLifetime(keyOwners);
		using var valueHandlesLifetime = new NativeLifetime(ValueHandleMarshaller.Copy(values, valueHandles, nameof(values),
			"Value lists cannot contain null."));
		ValueHandleMarshaller.EnsureAlive(keyOwners);
		using var nativeKeyType = new Utf8(keyType);
		using var nativeValueType = new Utf8(valueType);
		fixed (nint* keyData = keyHandles)
		{
			fixed (nint* valueData = valueHandles)
			{
				ThrowIfFailed(NativeMethods.vs_interop_value_create_map(Handle,
							nativeKeyType.View, nativeValueType.View, ordered ? (byte)1 : (byte)0,
							keyData, valueData, (ulong)keyHandles.Length), "create map");
			}
		}
	}

	/// <summary>Creates a map or multi-map value of the requested ABI kind.</summary>
	public void SetMap(VeroScriptValueKind kind, string keyType, string valueType,
		IReadOnlyList<VeroScriptValue> keys, IReadOnlyList<VeroScriptValue> values)
	{
		using var nativeLifetime = new NativeLifetime(this);
		if (kind is not (VeroScriptValueKind.Map or VeroScriptValueKind.OrderedMap
			or VeroScriptValueKind.MultiMap or VeroScriptValueKind.OrderedMultiMap))
		{
			throw new ArgumentOutOfRangeException(nameof(kind));
		}

		ArgumentNullException.ThrowIfNull(keys);
		ArgumentNullException.ThrowIfNull(values);
		int keyCount = keys.Count;
		int valueCount = values.Count;
		if (keyCount != valueCount)
		{
			throw new ArgumentException("Map keys and values must have the same count.");
		}

		using var nativeKeyType = new Utf8(keyType);
		using var nativeValueType = new Utf8(valueType);
		Span<nint> keyHandles = keyCount <= ValueHandleMarshaller.StackAllocationThreshold
			? stackalloc nint[keyCount] : new nint[keyCount];
		Span<nint> valueHandles = valueCount <= ValueHandleMarshaller.StackAllocationThreshold
			? stackalloc nint[valueCount] : new nint[valueCount];
		VeroScriptValue[] keyOwners = ValueHandleMarshaller.Copy(keys, keyHandles, nameof(keys), "Value lists cannot contain null.");
		using var keyHandlesLifetime = new NativeLifetime(keyOwners);
		using var valueHandlesLifetime = new NativeLifetime(ValueHandleMarshaller.Copy(values, valueHandles, nameof(values), "Value lists cannot contain null."));
		ValueHandleMarshaller.EnsureAlive(keyOwners);
		fixed (nint* keyData = keyHandles)
		{
			fixed (nint* valueData = valueHandles)
			{
				ThrowIfFailed(NativeMethods.vs_interop_value_create_map_kind(Handle, kind,
							nativeKeyType.View, nativeValueType.View, keyData, valueData,
							(ulong)keyHandles.Length), "create map");
			}
		}
	}

	public ulong MapCount
	{
		get
		{
			using var nativeLifetime = new NativeLifetime(this);
			ulong count;
			ThrowIfFailed(NativeMethods.vs_interop_value_map_count(Handle, &count), "get map count");
			return count;
		}
	}

	public bool TryGetMapValue(VeroScriptValue key, VeroScriptValue destination)
	{
		using var nativeLifetime = new NativeLifetime(this, key, destination);
		ArgumentNullException.ThrowIfNull(key);
		ArgumentNullException.ThrowIfNull(destination);
		byte found;
		ThrowIfFailed(NativeMethods.vs_interop_value_map_get(Handle, key.Handle,
			destination.Handle, &found), "get map value");
		return found != 0;
	}

	/// <summary>Visits a read-only snapshot of all map entries.</summary>
	/// <remarks>Key and value views expire when each visitor invocation returns.</remarks>
	public void EnumerateMap(
		Action<VeroScriptBorrowedValue, VeroScriptBorrowedValue> visitor)
	{
		using var nativeLifetime = new NativeLifetime(this);
		ArgumentNullException.ThrowIfNull(visitor);
		if ((VeroScriptRuntime.Capabilities
				& VeroScriptCapabilities.ContainerEnumeration) == 0)
		{
			throw new VeroScriptException(VeroScriptStatus.Unsupported,
						"The loaded VeroScript runtime does not support container enumeration.");
		}

		var state = new MapEnumerationState(visitor);
		GCHandle stateHandle = GCHandle.Alloc(state);
		try
		{
			VeroScriptStatus status = NativeMethods.vs_interop_value_map_enumerate(
				Handle, &OnMapEntry, GCHandle.ToIntPtr(stateHandle));
			state.Exception?.Throw();
			ThrowIfFailed(status, "enumerate map");
		}
		finally
		{
			stateHandle.Free();
		}
	}

	public void SetSet(string elementType, IReadOnlyList<VeroScriptValue> elements)
	{
		using var nativeLifetime = new NativeLifetime(this);
		ArgumentNullException.ThrowIfNull(elements);
		using var nativeType = new Utf8(elementType);
		int elementCount = elements.Count;
		Span<nint> handles = elementCount <= ValueHandleMarshaller.StackAllocationThreshold
			? stackalloc nint[elementCount]
			: new nint[elementCount];
		using var handlesLifetime = new NativeLifetime(ValueHandleMarshaller.Copy(elements, handles, nameof(elements),
			"Value lists cannot contain null."));
		fixed (nint* data = handles)
		{
			ThrowIfFailed(NativeMethods.vs_interop_value_create_set(Handle,
						nativeType.View, data, (ulong)handles.Length), "create set");
		}
	}

	/// <summary>Creates a set or multi-set value of the requested ABI kind.</summary>
	public void SetSet(VeroScriptValueKind kind, string elementType,
		IReadOnlyList<VeroScriptValue> elements)
	{
		using var nativeLifetime = new NativeLifetime(this);
		if (kind is not (VeroScriptValueKind.Set or VeroScriptValueKind.MultiSet
			or VeroScriptValueKind.OrderedMultiSet))
		{
			throw new ArgumentOutOfRangeException(nameof(kind));
		}

		ArgumentNullException.ThrowIfNull(elements);
		using var nativeType = new Utf8(elementType);
		int elementCount = elements.Count;
		Span<nint> handles = elementCount <= ValueHandleMarshaller.StackAllocationThreshold
			? stackalloc nint[elementCount] : new nint[elementCount];
		using var handlesLifetime = new NativeLifetime(ValueHandleMarshaller.Copy(elements, handles, nameof(elements),
			"Value lists cannot contain null."));
		fixed (nint* data = handles)
		{
			ThrowIfFailed(NativeMethods.vs_interop_value_create_set_kind(Handle, kind,
						nativeType.View, data, (ulong)handles.Length), "create set");
		}
	}

	public ulong SetCount
	{
		get
		{
			using var nativeLifetime = new NativeLifetime(this);
			ulong count;
			ThrowIfFailed(NativeMethods.vs_interop_value_set_count(Handle, &count), "get set count");
			return count;
		}
	}

	public bool SetContains(VeroScriptValue element)
	{
		using var nativeLifetime = new NativeLifetime(this, element);
		ArgumentNullException.ThrowIfNull(element);
		byte contains;
		ThrowIfFailed(NativeMethods.vs_interop_value_set_contains(Handle, element.Handle, &contains), "query set");
		return contains != 0;
	}

	/// <summary>Visits a read-only snapshot of all set elements.</summary>
	/// <remarks>Element views expire when each visitor invocation returns.</remarks>
	public void EnumerateSet(Action<VeroScriptBorrowedValue> visitor)
	{
		using var nativeLifetime = new NativeLifetime(this);
		ArgumentNullException.ThrowIfNull(visitor);
		if ((VeroScriptRuntime.Capabilities
				& VeroScriptCapabilities.ContainerEnumeration) == 0)
		{
			throw new VeroScriptException(VeroScriptStatus.Unsupported,
						"The loaded VeroScript runtime does not support container enumeration.");
		}

		var state = new SetEnumerationState(visitor);
		GCHandle stateHandle = GCHandle.Alloc(state);
		try
		{
			VeroScriptStatus status = NativeMethods.vs_interop_value_set_enumerate(
				Handle, &OnSetElement, GCHandle.ToIntPtr(stateHandle));
			state.Exception?.Throw();
			ThrowIfFailed(status, "enumerate set");
		}
		finally
		{
			stateHandle.Free();
		}
	}

	public void SetChannel(string elementType)
	{
		using var nativeLifetime = new NativeLifetime(this);
		using var nativeType = new Utf8(elementType);
		ThrowIfFailed(NativeMethods.vs_interop_value_create_channel(Handle, nativeType.View), "create channel");
	}

	/// <summary>Creates a typed channel. Capacity zero denotes an unbounded channel.</summary>
	public void SetChannel(string elementType, ulong capacity)
	{
		using var nativeLifetime = new NativeLifetime(this);
		using var nativeType = new Utf8(elementType);
		ThrowIfFailed(NativeMethods.vs_interop_value_create_bounded_channel(
			Handle, nativeType.View, capacity), "create bounded channel");
	}

	public ulong ChannelCount
	{
		get
		{
			using var nativeLifetime = new NativeLifetime(this);
			ulong count;
			ThrowIfFailed(NativeMethods.vs_interop_value_channel_count(Handle, &count), "get channel count");
			return count;
		}
	}

	/// <summary>Zero denotes an unbounded channel.</summary>
	public ulong ChannelCapacity
	{
		get
		{
			using var nativeLifetime = new NativeLifetime(this);
			ulong capacity;
			ThrowIfFailed(NativeMethods.vs_interop_value_channel_capacity(Handle, &capacity), "get channel capacity");
			return capacity;
		}
	}

	public bool IsChannelClosed
	{
		get
		{
			using var nativeLifetime = new NativeLifetime(this);
			byte closed;
			ThrowIfFailed(NativeMethods.vs_interop_value_channel_is_closed(Handle, &closed), "query channel state");
			return closed != 0;
		}
	}

	public void ChannelSend(VeroScriptValue value)
	{
		using var nativeLifetime = new NativeLifetime(this, value);
		ArgumentNullException.ThrowIfNull(value);
		nint error = 0;
		VeroScriptStatus status = NativeMethods.vs_interop_value_channel_send(Handle, value.Handle, &error);
		ThrowContainerStatus(status, error, "send to channel");
	}

	public bool ChannelTrySend(VeroScriptValue value)
	{
		using var nativeLifetime = new NativeLifetime(this, value);
		ArgumentNullException.ThrowIfNull(value);
		byte sent;
		ThrowIfFailed(NativeMethods.vs_interop_value_channel_try_send(
			Handle, value.Handle, &sent), "try send to channel");
		return sent != 0;
	}

	public bool ChannelSendFor(VeroScriptValue value, ulong timeoutMilliseconds)
	{
		using var nativeLifetime = new NativeLifetime(this, value);
		ArgumentNullException.ThrowIfNull(value);
		byte sent;
		ThrowIfFailed(NativeMethods.vs_interop_value_channel_send_for(
			Handle, value.Handle, timeoutMilliseconds, &sent), "timed send to channel");
		return sent != 0;
	}

	public VeroScriptValue ChannelReceive()
	{
		using var nativeLifetime = new NativeLifetime(this);
		var value = new VeroScriptValue();
		nint error = 0;
		try
		{
			VeroScriptStatus status = NativeMethods.vs_interop_value_channel_receive(Handle, value.Handle, &error);
			ThrowContainerStatus(status, error, "receive from channel");
			return value;
		}
		catch
		{
			value.Dispose();
			throw;
		}
	}

	public VeroScriptValue? ChannelTryReceive()
		=> ChannelReceiveForCore(0, false);

	public VeroScriptValue? ChannelReceiveFor(ulong timeoutMilliseconds)
		=> ChannelReceiveForCore(timeoutMilliseconds, true);

	private VeroScriptValue? ChannelReceiveForCore(ulong timeoutMilliseconds, bool timed)
	{
		using var nativeLifetime = new NativeLifetime(this);
		var value = new VeroScriptValue();
		byte received;
		try
		{
			VeroScriptStatus status = timed
				? NativeMethods.vs_interop_value_channel_receive_for(
					Handle, timeoutMilliseconds, value.Handle, &received)
				: NativeMethods.vs_interop_value_channel_try_receive(
					Handle, value.Handle, &received);
			ThrowIfFailed(status, timed ? "timed receive from channel" : "try receive from channel");
			if (received != 0) return value;
			value.Dispose();
			return null;
		}
		catch
		{
			value.Dispose();
			throw;
		}
	}

	public void ChannelClose()
	{
		using var nativeLifetime = new NativeLifetime(this);
		ThrowIfFailed(NativeMethods.vs_interop_value_channel_close(Handle), "close channel");
	}

	public void Dispose()
	{
		Dispose(true);
		GC.SuppressFinalize(this);
	}

	protected virtual void Dispose(bool disposing) => Release();

	private void Release()
	{
		nint handle = Interlocked.Exchange(ref _handle, 0);
		if (handle != 0 && _ownsHandle)
		{
			NativeMethods.vs_interop_value_destroy(handle);
		}
	}

	internal static void ThrowIfFailed(VeroScriptStatus status, string operation)
	{
		if (status != VeroScriptStatus.Ok)
		{
			throw new VeroScriptException(status, $"VeroScript failed to {operation}: {status}.");
		}
	}

	private static void ThrowContainerStatus(VeroScriptStatus status, nint error, string operation)
	{
		string detail = NativeBuffer.CopyOptionalErrorAndDestroy(error);
		if (status != VeroScriptStatus.Ok)
		{
			throw new VeroScriptException(status, string.IsNullOrWhiteSpace(detail)
						? $"VeroScript failed to {operation}: {status}." : detail);
		}
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static void DestroyHostObject(nint payload)
	{
		try
		{
			if (payload != 0 && ManagedHostObjects.TryRemove(payload, out _))
			{
				GCHandle.FromIntPtr(payload).Free();
			}
		}
		catch
		{
			/* Destruction callbacks must never cross the C ABI. */
		}
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static byte HostObjectIsType(nint payload, nint userData,
		NativeStringView expectedTypeName)
	{
		try
		{
			if (payload == 0 || payload != userData
				|| !ManagedHostObjects.TryGetValue(payload, out ManagedHostObject? hostObject)
				|| hostObject.IsType is null)
			{
				return 0;
			}

			return hostObject.IsType(Utf8.Copy(expectedTypeName)) ? (byte)1 : (byte)0;
		}
		catch
		{
			return 0;
		}
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static uint OnMapEntry(nint key, nint mappedValue, nint userData)
	{
		BorrowScope? scope = null;
		try
		{
			scope = new BorrowScope();
			if (key == 0 || mappedValue == 0)
			{
				throw new VeroScriptException(VeroScriptStatus.InternalError,
								"The native map visitor supplied an unavailable value.");
			}

			var state = (MapEnumerationState)GCHandle.FromIntPtr(userData).Target!;
			state.Visitor(new VeroScriptBorrowedValue(key, scope),
				new VeroScriptBorrowedValue(mappedValue, scope));
			return 0;
		}
		catch (Exception exception)
		{
			try
			{
				var state = (MapEnumerationState)GCHandle.FromIntPtr(userData).Target!;
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

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static uint OnSetElement(nint element, nint userData)
	{
		BorrowScope? scope = null;
		try
		{
			scope = new BorrowScope();
			if (element == 0)
			{
				throw new VeroScriptException(VeroScriptStatus.InternalError,
								"The native set visitor supplied an unavailable value.");
			}

			var state = (SetEnumerationState)GCHandle.FromIntPtr(userData).Target!;
			state.Visitor(new VeroScriptBorrowedValue(element, scope));
			return 0;
		}
		catch (Exception exception)
		{
			try
			{
				var state = (SetEnumerationState)GCHandle.FromIntPtr(userData).Target!;
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

	internal static T? GetManagedHostPayload<T>(nint payload) where T : class
		=> payload != 0 && ManagedHostObjects.TryGetValue(payload, out ManagedHostObject? value)
			? value.Value as T : null;
}

/// <summary>A callback-scoped value view that does not own its native handle.</summary>
/// <remarks>Use the view only on its callback thread. Call <see cref="VeroScriptValue.Clone"/>
/// inside the callback to retain its contents or transfer an owned value to another thread.</remarks>
public sealed class VeroScriptBorrowedValue : VeroScriptValue
{
	internal VeroScriptBorrowedValue(nint handle, BorrowScope scope) : base(handle, false, scope)
		=> GC.SuppressFinalize(this);
}
