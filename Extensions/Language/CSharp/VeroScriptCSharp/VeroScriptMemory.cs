// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Buffers;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using VeroScript.Interop;

namespace VeroScript;

public enum VeroScriptByteOrder : uint
{
	Little,
	Big
}

public readonly record struct VeroScriptMemoryLimits(ulong MaxBufferBytes = 0, ulong MaxRetainedBytes = 0);
public readonly record struct VeroScriptMemoryStats(ulong OwnedBytes, ulong BorrowedBytes, ulong PeakOwnedBytes);

public sealed unsafe partial class VeroScriptSession
{
	public VeroScriptMemory Memory => new(this);

	public void SetMemoryLimits(VeroScriptMemoryLimits limits)
	{
		using var lifetime = new NativeLifetime(this);
		CheckOwner();
		_ = Handle;
		VeroScriptMemory.EnsureSupported();
		NativeMemoryLimits native =
			new() { StructSize = (uint)sizeof(NativeMemoryLimits), MaxBufferBytes = limits.MaxBufferBytes,
					MaxRetainedBytes = limits.MaxRetainedBytes };
		CheckStatus(NativeMethods.vs_interop_session_set_memory_limits(Handle, &native), "set memory limits");
	}

	internal nint MemorySessionHandle
	{
		get
		{
			CheckOwner();
			return Handle;
		}
	}
}

/// <summary>Checked memory operations in a Session or an active native callback.</summary>
/// <remarks>The loaded runtime must advertise VeroScriptCapabilities.Memory. Otherwise operations
/// throw NotSupportedException before invoking optional native exports. Callback access expires
/// when its callback returns and requires the callback thread; session access follows the session's
/// ownership and thread rules.</remarks>
public sealed unsafe class VeroScriptMemory
{
	public VeroScriptMemoryStats Stats
	{
		get
		{
			var (s, c) = Access;
			using var lifetime = new NativeLifetime(_owner);
			NativeMemoryStats stats = new() { StructSize = (uint)sizeof(NativeMemoryStats) };
			VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_memory_get_stats(s, c, &stats),
										  "query memory statistics");
			return new(stats.OwnedBytes, stats.BorrowedBytes, stats.PeakOwnedBytes);
		}
	}

	public bool NativeAddressesAvailable
	{
		get
		{
			var (s, c) = Access;
			using var lifetime = new NativeLifetime(_owner);
			byte available;
			VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_memory_native_addresses_available(s, c, &available),
										  "query memory address capability");
			return available != 0;
		}
	}

	public VeroScriptValue CreateBuffer(long size = 0) =>
		Create((s, c, o) => NativeMethods.vs_interop_memory_create_buffer(s, c, size, o));

	public VeroScriptValue CreateView(bool readOnly = false) =>
		Create((s, c, o) => NativeMethods.vs_interop_memory_create_view(s, c, readOnly ? (byte)1 : (byte)0, o));

	public VeroScriptValue Slice(VeroScriptValue region, long offset, long count, bool readOnly = false)
	{
		ArgumentNullException.ThrowIfNull(region);
		using var lifetime = new NativeLifetime(region);
		return Create((s, c, o) => NativeMethods.vs_interop_memory_slice(s, c, region.Handle, offset, count,
																		 readOnly ? (byte)1 : (byte)0, o));
	}

	public VeroScriptValue Clone(VeroScriptValue source)
	{
		ArgumentNullException.ThrowIfNull(source);
		using var lifetime = new NativeLifetime(source);
		return Create((s, c, o) => NativeMethods.vs_interop_memory_clone(s, c, source.Handle, o));
	}

	public VeroScriptValue FromArray(VeroScriptValue source)
	{
		ArgumentNullException.ThrowIfNull(source);
		using var lifetime = new NativeLifetime(source);
		return Create((s, c, o) => NativeMethods.vs_interop_memory_from_array(s, c, source.Handle, o));
	}

	public VeroScriptValue ToArray(VeroScriptValue source)
	{
		ArgumentNullException.ThrowIfNull(source);
		using var lifetime = new NativeLifetime(source);
		return Create((s, c, o) => NativeMethods.vs_interop_memory_to_array(s, c, source.Handle, o));
	}

	public VeroScriptValue Read(VeroScriptValue source, long offset, VeroScriptNativeTypeKind type,
								VeroScriptByteOrder order = VeroScriptByteOrder.Little)
	{
		ArgumentNullException.ThrowIfNull(source);
		using var lifetime = new NativeLifetime(source);
		return Create(
			(s, c, o) => NativeMethods.vs_interop_memory_read(s, c, source.Handle, offset, (uint)type, (uint)order, o));
	}

	public void Write(VeroScriptValue destination, long offset, VeroScriptNativeTypeKind type, VeroScriptValue value,
					  VeroScriptByteOrder order = VeroScriptByteOrder.Little)
	{
		ArgumentNullException.ThrowIfNull(destination);
		ArgumentNullException.ThrowIfNull(value);
		var (s, c) = Access;
		using var lifetime = new NativeLifetime(_owner, destination, value);
		VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_memory_write(s, c, destination.Handle, offset,
																			(uint)type, (uint)order, value.Handle),
									  "write memory");
	}

	public long Size(VeroScriptValue region)
	{
		ArgumentNullException.ThrowIfNull(region);
		var (s, c) = Access;
		using var lifetime = new NativeLifetime(_owner, region);
		long n;
		VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_memory_size(s, c, region.Handle, &n),
									  "query memory size");
		return n;
	}

	public long Capacity(VeroScriptValue buffer)
	{
		ArgumentNullException.ThrowIfNull(buffer);
		var (s, c) = Access;
		using var lifetime = new NativeLifetime(_owner, buffer);
		long n;
		VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_memory_capacity(s, c, buffer.Handle, &n),
									  "query memory capacity");
		return n;
	}

	public void Resize(VeroScriptValue buffer, long size, bool reserveOnly = false)
	{
		ArgumentNullException.ThrowIfNull(buffer);
		var (s, c) = Access;
		using var lifetime = new NativeLifetime(_owner, buffer);
		VeroScriptValue.ThrowIfFailed(
			NativeMethods.vs_interop_memory_resize(s, c, buffer.Handle, size, reserveOnly ? (byte)1 : (byte)0),
			"resize memory");
	}

	public void Copy(VeroScriptValue destination, long destinationOffset, VeroScriptValue source, long sourceOffset,
					 long count)
	{
		ArgumentNullException.ThrowIfNull(destination);
		ArgumentNullException.ThrowIfNull(source);
		var (s, c) = Access;
		using var lifetime = new NativeLifetime(_owner, destination, source);
		VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_memory_copy(s, c, destination.Handle, destinationOffset,
																		   source.Handle, sourceOffset, count),
									  "copy memory");
	}

	public void Fill(VeroScriptValue destination, long offset, long count, byte value)
	{
		ArgumentNullException.ThrowIfNull(destination);
		var (s, c) = Access;
		using var lifetime = new NativeLifetime(_owner, destination);
		VeroScriptValue.ThrowIfFailed(
			NativeMethods.vs_interop_memory_fill(s, c, destination.Handle, offset, count, value), "fill memory");
	}

	public bool Equal(VeroScriptValue left, long leftOffset, VeroScriptValue right, long rightOffset, long count)
	{
		ArgumentNullException.ThrowIfNull(left);
		ArgumentNullException.ThrowIfNull(right);
		var (s, c) = Access;
		using var lifetime = new NativeLifetime(_owner, left, right);
		byte equal;
		VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_memory_equal(s, c, left.Handle, leftOffset, right.Handle,
																			rightOffset, count, &equal),
									  "compare memory");
		return equal != 0;
	}

	public ulong ObjectId(VeroScriptValue value) => Number(value, NativeMethods.vs_interop_memory_object_id);

	public ulong ObjectAddress(VeroScriptValue value) => Number(value, NativeMethods.vs_interop_memory_object_address);

	public ulong DataAddress(VeroScriptValue value) => Number(value, NativeMethods.vs_interop_memory_data_address);

	public ulong MutationVersion(VeroScriptValue region) => Number(region,
																   NativeMethods.vs_interop_memory_mutation_version);

	public void InvalidateExternal(VeroScriptValue region)
	{
		ArgumentNullException.ThrowIfNull(region);
		var (s, c) = Access;
		using var lifetime = new NativeLifetime(_owner, region);
		VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_memory_invalidate_external(s, c, region.Handle),
									  "invalidate external memory");
	}

	public VeroScriptMemoryLease AcquireLease(VeroScriptValue region, bool write = false)
	{
		ArgumentNullException.ThrowIfNull(region);
		var (s, c) = Access;
		using var lifetime = new NativeLifetime(_owner, region);
		var result = new VeroScriptMemoryLease(write);
		NativeMemoryLease lease = new() { StructSize = (uint)sizeof(NativeMemoryLease) };
		VeroScriptValue.ThrowIfFailed(
			NativeMethods.vs_interop_memory_acquire_lease(s, c, region.Handle, write ? (byte)1 : (byte)0, &lease),
			"acquire memory lease");
		result.Initialize(lease);
		return result;
	}

	/// <summary>Pins memory until all returned views and leases are released.</summary>
	/// <remarks>A custom MemoryManager must support pinning and allow Unpin on the final releasing thread.</remarks>
	public VeroScriptValue WrapExternal(Memory<byte> memory, bool readOnly = false)
	{
		// Validate the owner and runtime before pinning caller-managed memory.
		_ = Access;
		var pin = memory.Pin();
		GCHandle owner = default;
		try
		{
			owner = GCHandle.Alloc(new PinnedOwner(pin));
			var result = Create((s, c, o) => NativeMethods.vs_interop_memory_wrap_external(
									s, c, (nint)pin.Pointer, (ulong)memory.Length, readOnly ? (byte)1 : (byte)0,
									GCHandle.ToIntPtr(owner), &ReleasePinned, o));
			return result;
		}
		catch
		{
			if (owner.IsAllocated)
			{
				owner.Free();
			}
			pin.Dispose();
			throw;
		}
	}

	internal VeroScriptMemory(VeroScriptSession session) => _owner = session;

	internal VeroScriptMemory(VeroScriptCallContext context) => _owner = context;

	internal static void EnsureSupported()
	{
		if ((VeroScriptRuntime.Capabilities & VeroScriptCapabilities.Memory) == 0)
		{
			throw new NotSupportedException("The loaded VeroScript runtime does not support checked memory operations.");
		}
	}

	private delegate VeroScriptStatus Query(nint session, nint call, nint value, ulong* result);

	private sealed class PinnedOwner(MemoryHandle handle)
	{
		internal MemoryHandle Handle = handle;
	}

	private (nint Session, nint Call) Access
	{
		get
		{
			var handles = _owner is VeroScriptSession session
				? (session.MemorySessionHandle, (nint)0)
				: ((nint)0, ((VeroScriptCallContext)_owner).MemoryCallHandle);
			EnsureSupported();
			return handles;
		}
	}

	private VeroScriptValue Create(Func<nint, nint, nint, VeroScriptStatus> action)
	{
		var (s, c) = Access;
		var result = new VeroScriptValue();
		using var lifetime = new NativeLifetime(_owner, result);
		try
		{
			VeroScriptValue.ThrowIfFailed(action(s, c, result.Handle), "memory operation");
			return result;
		}
		catch
		{
			result.Dispose();
			throw;
		}
	}

	private ulong Number(VeroScriptValue value, Query query)
	{
		ArgumentNullException.ThrowIfNull(value);
		var (s, c) = Access;
		using var lifetime = new NativeLifetime(_owner, value);
		ulong n;
		VeroScriptValue.ThrowIfFailed(query(s, c, value.Handle, &n), "query memory");
		return n;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static void ReleasePinned(nint owner)
	{
		var handle = GCHandle.FromIntPtr(owner);
		try
		{
			((PinnedOwner)handle.Target!).Handle.Dispose();
		}
		catch
		{
		}
		finally
		{
			handle.Free();
		}
	}

	private readonly object _owner;
}

/// <summary>Owns a native memory lease. Keep it alive through native work and
/// dispose after that work completes. Release is valid on any thread.</summary>
public sealed unsafe class VeroScriptMemoryLease : IDisposable
{
	public void Dispose()
	{
		if (Interlocked.Exchange(ref _closed, 1) == 0 && _lease.Token.Serial != 0)
		{
			VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_memory_release_lease(_lease.Token),
										  "release memory lease");
		}
		GC.SuppressFinalize(this);
	}

	public ulong Size
	{
		get
		{
			Check();
			return _lease.Size;
		}
	}

	public nint Data
	{
		get
		{
			Check();
			return _lease.Data;
		}
	}

	public bool Writable => _write;

	public ReadOnlySpan<byte> Bytes
	{
		get
		{
			Check();
			return new((void*)_lease.Data, checked((int)_lease.Size));
		}
	}

	public Span<byte> WritableBytes
	{
		get
		{
			Check();
			if (!_write)
			{
				throw new VeroScriptException(VeroScriptStatus.ReadOnly, "The lease is read-only.");
			}
			return new((void*)_lease.WritableData, checked((int)_lease.Size));
		}
	}

	internal VeroScriptMemoryLease(bool write) => _write = write;

	internal void Initialize(NativeMemoryLease lease) => _lease = lease;

	private void Check() => ObjectDisposedException.ThrowIf(Volatile.Read(ref _closed) != 0, this);

	~VeroScriptMemoryLease()
	{
		if (Interlocked.Exchange(ref _closed, 1) == 0 && _lease.Token.Serial != 0)
		{
			_ = NativeMethods.vs_interop_memory_release_lease(_lease.Token);
		}
	}

	private NativeMemoryLease _lease;
	private readonly bool _write;
	private int _closed;
}
