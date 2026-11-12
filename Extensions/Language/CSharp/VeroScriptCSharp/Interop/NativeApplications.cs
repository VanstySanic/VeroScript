// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace VeroScript.Interop;
internal static unsafe partial class NativeMethods
{
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_application_services_create(NativeStringView options, nint environment, nint* services, nint* error);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern void vs_interop_application_services_destroy(nint services);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_session_load_application(nint session, NativeStringView path, nint services, NativeRunResult* result);
	[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
	internal static extern VeroScriptStatus vs_interop_application_read(NativeByteView bytes, NativeStringView signatures, nint services, nint* package, nint* error);
}

internal sealed unsafe class NativeApplicationServices : IDisposable
{
	internal nint Handle { get; private set; }

	internal NativeApplicationServices(string options)
	{
		VeroScriptRuntime.EnsureCompatible();
		using var text = new Utf8(options);
		nint services = 0, error = 0;
		try
		{
			var status = NativeMethods.vs_interop_application_services_create(text.View, 0, &services, &error);
			var ownedError = error;
			error = 0;
			var detail = NativeBuffer.CopyOptionalErrorAndDestroy(ownedError);
			if (status != VeroScriptStatus.Ok)
			{
				throw new VeroScriptException(status, string.IsNullOrEmpty(detail) ? "Application services are unavailable." : detail);
			}

			if (services == 0)
			{
				throw new VeroScriptException(VeroScriptStatus.InternalError, "Application services returned a null handle.");
			}

			Handle = services;
			services = 0;
		}
		finally
		{
			if (error != 0)
			{
				NativeMethods.vs_interop_buffer_destroy(error);
			}

			if (services != 0)
			{
				NativeMethods.vs_interop_application_services_destroy(services);
			}
		}
	}

	public void Dispose()
	{
		var handle = Handle;
		Handle = 0;
		if (handle != 0)
		{
			NativeMethods.vs_interop_application_services_destroy(handle);
		}
	}
}
