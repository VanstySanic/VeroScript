// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
namespace VeroScript.Interop;

internal static unsafe partial class NativeMethods
{
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_session_set_memory_limits(nint session,
																				 NativeMemoryLimits *limits);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_get_stats(nint session, nint call,
																		NativeMemoryStats *out_stats);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_create_buffer(nint session, nint call, long size,
																			nint out_value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_create_view(nint session, nint call, byte read_only,
																		  nint out_value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_size(nint session, nint call, nint region,
																   long *out_size);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_capacity(nint session, nint call, nint buffer,
																	   long *out_capacity);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_resize(nint session, nint call, nint buffer, long size,
																	 byte reserve_only);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_slice(nint session, nint call, nint region, long offset,
																	long count, byte read_only, nint out_value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_copy(nint session, nint call, nint destination,
																   long destination_offset, nint source,
																   long source_offset, long count);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_fill(nint session, nint call, nint destination,
																   long offset, long count, byte value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_equal(nint session, nint call, nint left,
																	long left_offset, nint right, long right_offset,
																	long count, byte *out_equal);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_clone(nint session, nint call, nint source,
																	nint out_value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_from_array(nint session, nint call, nint source,
																		 nint out_value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_to_array(nint session, nint call, nint source,
																	   nint out_value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_read(nint session, nint call, nint source, long offset,
																   uint type, uint byte_order, nint out_value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_write(nint session, nint call, nint destination,
																	long offset, uint type, uint byte_order,
																	nint value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_native_addresses_available(nint session, nint call,
																						 byte *out_available);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_object_id(nint session, nint call, nint value,
																		ulong *out_id);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_object_address(nint session, nint call, nint value,
																			 ulong *out_address);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_data_address(nint session, nint call, nint value,
																		   ulong *out_address);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_call_argument_address(nint call, ulong index,
																					ulong *out_address);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_wrap_external(
		nint session, nint call, nint data, ulong size, byte read_only, nint owner,
		delegate *unmanaged[Cdecl]<nint, void> release, nint out_value);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_invalidate_external(nint session, nint call, nint region);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_acquire_lease(nint session, nint call, nint region,
																			byte write, NativeMemoryLease *out_lease);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_release_lease(NativeMemoryLeaseToken token);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_memory_mutation_version(nint session, nint call, nint region,
																			   ulong *out_version);
}
