// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScript.Interop;

/// <summary>Copies managed value handles into native call buffers and returns owners to retain.</summary>
internal static class ValueHandleMarshaller
{
	// Keep small native handle buffers on the stack and bound their stack usage.
	internal const int StackAllocationThreshold = 16;

	internal static VeroScriptValue[] Copy(
		VeroScriptValue[] values,
		Span<nint> destination,
		string parameterName,
		string nullValueMessage)
	{
		var owners = destination.IsEmpty ? Array.Empty<VeroScriptValue>() : new VeroScriptValue[destination.Length];
		for (int i = 0; i < destination.Length; ++i)
		{
			VeroScriptValue value = values[i]
				?? throw new ArgumentException(nullValueMessage, parameterName);
			// Retain the exact wrapper used for this handle, even if a managed
			// callback or another list indexer later replaces the caller's slot.
			owners[i] = value;
			destination[i] = value.Handle;
		}
		return owners;
	}

	internal static VeroScriptValue[] Copy(
		IReadOnlyList<VeroScriptValue> values,
		Span<nint> destination,
		string parameterName,
		string nullValueMessage)
	{
		if (values is VeroScriptValue[] array)
		{
			return Copy(array, destination, parameterName, nullValueMessage);
		}
		var owners = destination.IsEmpty ? Array.Empty<VeroScriptValue>() : new VeroScriptValue[destination.Length];
		for (int i = 0; i < destination.Length; ++i)
		{
			VeroScriptValue value = values[i]
				?? throw new ArgumentException(nullValueMessage, parameterName);
			owners[i] = value;
		}
		// A later user indexer can dispose an earlier element. Finish all user
		// code before extracting handles so no stale pointer reaches the ABI.
		CopyHandles(owners, destination);
		return owners;
	}

	internal static void EnsureAlive(VeroScriptValue[] owners)
	{
		foreach (VeroScriptValue owner in owners)
		{
			_ = owner.Handle;
		}
	}

	private static void CopyHandles(VeroScriptValue[] owners, Span<nint> destination)
	{
		for (int i = 0; i < destination.Length; ++i)
		{
			destination[i] = owners[i].Handle;
		}
	}
}
