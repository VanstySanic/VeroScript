// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using System.Text;

namespace VeroScript.Interop;

internal sealed unsafe class Utf8 : IDisposable
{
	private static readonly UTF8Encoding StrictEncoding = new(false, true);
	private nint _data;
	internal NativeStringView View { get; }

	internal Utf8(string value, bool allowEmbeddedNul = false)
	{
		ArgumentNullException.ThrowIfNull(value);
		if (!allowEmbeddedNul && value.Contains('\0', StringComparison.Ordinal))
		{
			throw new ArgumentException("Embedded NUL characters are not accepted here.", nameof(value));
		}

		byte[] bytes;
		try
		{
			bytes = StrictEncoding.GetBytes(value);
		}
		catch (EncoderFallbackException exception)
		{
			throw new ArgumentException(
				"The string contains invalid UTF-16 data and cannot be encoded as UTF-8.",
				nameof(value), exception);
		}
		nint data = Marshal.AllocHGlobal(checked(bytes.Length + 1));
		try
		{
			Marshal.Copy(bytes, 0, data, bytes.Length);
			((byte*)data)[bytes.Length] = 0;
			_data = data;
			View = new NativeStringView((byte*)data, (ulong)bytes.Length);
		}
		catch
		{
			Marshal.FreeHGlobal(data);
			throw;
		}
	}

	public void Dispose()
	{
		nint data = Interlocked.Exchange(ref _data, 0);
		if (data != 0)
		{
			Marshal.FreeHGlobal(data);
		}
	}

	internal static string Copy(NativeStringView view)
	{
		if (view.Size == 0) return string.Empty;
		if (view.Data is null)
		{
			throw new VeroScriptException(VeroScriptStatus.InternalError,
						"The native UTF-8 view returned a null pointer for non-empty data.");
		}

		if (view.Size > int.MaxValue)
		{
			throw new OverflowException("The native UTF-8 view is too large for a managed string.");
		}

		try
		{
			return StrictEncoding.GetString(view.Data, (int)view.Size);
		}
		catch (DecoderFallbackException)
		{
			throw new VeroScriptException(VeroScriptStatus.InternalError,
				"The native string contains invalid UTF-8 data.");
		}
	}

	internal static string Decode(ReadOnlySpan<byte> bytes)
	{
		try
		{
			return StrictEncoding.GetString(bytes);
		}
		catch (DecoderFallbackException)
		{
			throw new VeroScriptException(VeroScriptStatus.InternalError,
				"The native buffer contains invalid UTF-8 data.");
		}
	}
}
