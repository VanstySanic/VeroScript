// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using VeroScript;
using VeroScript.Interop;

internal static unsafe class ConsoleAbiContractTests
{
	internal static void Run()
	{
		uint prefix = (uint)Marshal.OffsetOf<NativeSessionOptions>(nameof(NativeSessionOptions.ConsoleQueryCallback));
		Require(prefix == (Environment.Is64BitProcess ? 32u : 16u), "original options prefix is unchanged");
		uint pointerSize = (uint)sizeof(nint);
		for (uint size = prefix; size <= sizeof(NativeSessionOptions); size += pointerSize)
		{
			var options = new NativeSessionOptions
			{
				StructSize = size,
				ConsoleQueryCallback = &OnQuery,
				ConsoleControlCallback = &OnControl,
				ConsoleWriteCallback = &OnWrite
			};
			nint session = 0;
			Require(NativeMethods.vs_interop_session_create(&options, &session) == VeroScriptStatus.Ok
				&& session != 0, "original and intermediate options sizes are accepted");
			try
			{
				string body = size == prefix ? "assert(!std::console::is_output_terminal());"
					: "assert(std::console::is_output_terminal());";
				body += size < prefix + 2 * pointerSize ? "assert(!std::console::clear());"
					: "assert(std::console::clear());";
				body += "std::console::write(\"text\");";
				using var source = new Utf8("void main(){" + body + "}");
				using var label = new Utf8("console-options.vs");
				var result = new NativeRunResult { StructSize = (uint)sizeof(NativeRunResult) };
				Require(NativeMethods.vs_interop_session_load_source(session, source.View, label.View, &result)
					== VeroScriptStatus.Ok && result.Success != 0, "load legacy-options source");
				_writes = 0;
				Require(NativeMethods.vs_interop_session_run(session, VeroScriptRunMode.Script, &result)
					== VeroScriptStatus.Ok && result.Success != 0, "run legacy-options source");
				Require(_writes == (size == sizeof(NativeSessionOptions) ? 1 : 0),
					"callbacks outside the caller's struct size are never read");
			}
			finally
			{
				Require(NativeMethods.vs_interop_session_destroy(session) == VeroScriptStatus.Ok, "destroy legacy session");
			}
		}
		var invalid = new NativeSessionOptions { StructSize = prefix - 1 };
		nint rejected = 0;
		Require(NativeMethods.vs_interop_session_create(&invalid, &rejected) == VeroScriptStatus.InvalidArgument
			&& rejected == 0, "incomplete original options prefix is rejected");
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static byte OnQuery(NativeConsoleInfo* info, nint userData)
	{
		info->Flags = 15;
		return 1;
	}

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static byte OnControl(uint operation, int first, int second, nint userData) => 1;

	[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
	private static byte OnWrite(NativeStringView text, nint userData)
	{
		_writes++;
		return 1;
	}

	private static void Require(bool condition, string message)
	{
		if (!condition)
		{
			throw new InvalidOperationException("Console ABI: " + message);
		}
	}

	private static int _writes;
}
