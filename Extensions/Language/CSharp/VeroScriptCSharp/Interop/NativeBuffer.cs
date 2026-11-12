// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScript.Interop;

internal static unsafe class NativeBuffer
{
	internal static byte[] CopyAndDestroy(ref nint handle)
		=> CopyAndDestroyCore(Interlocked.Exchange(ref handle, 0));

	private static byte[] CopyAndDestroyCore(nint handle)
	{
		if (handle == 0) return [];
		try
		{
			ulong required = 0;
			VeroScriptStatus query = NativeMethods.vs_interop_buffer_copy(handle,
				null, 0, &required);
			if (query != VeroScriptStatus.Ok && query != VeroScriptStatus.BufferTooSmall)
			{
				VeroScriptValue.ThrowIfFailed(query, "query native buffer size");
			}

			if (required > int.MaxValue)
			{
				throw new OverflowException("The native buffer is too large for a managed array.");
			}

			if (required == 0) return [];
			byte[] bytes = new byte[(int)required];
			fixed (byte* destination = bytes)
			{
				VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_buffer_copy(handle,
								destination, required, &required), "copy native buffer");
			}

			if (required > (ulong)bytes.Length)
			{
				throw new VeroScriptException(VeroScriptStatus.InternalError,
								"The native buffer size grew while it was being copied.");
			}

			if (required != (ulong)bytes.Length)
			{
				Array.Resize(ref bytes, checked((int)required));
			}

			return bytes;
		}
		finally
		{
			NativeMethods.vs_interop_buffer_destroy(handle);
		}
	}

	internal static string CopyUtf8AndDestroy(ref nint handle)
		=> Utf8.Decode(CopyAndDestroy(ref handle));

	internal static string CopyOptionalErrorAndDestroy(nint handle)
		=> handle == 0 ? string.Empty : Utf8.Decode(CopyAndDestroyCore(handle));

	internal static void Destroy(ref nint handle)
	{
		nint owned = Interlocked.Exchange(ref handle, 0);
		if (owned != 0)
		{
			NativeMethods.vs_interop_buffer_destroy(owned);
		}
	}
}
