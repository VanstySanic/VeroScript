// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript.Interop;

namespace VeroScript;

public sealed unsafe partial class VeroScriptSession
{
	/// <summary>Loads an application container or directory with explicit native cache and trust options.</summary>
	/// <remarks>Paths in the JSON and the application path must be absolute. The application retains its services.</remarks>
	public VeroScriptRunResult LoadApplication(string absolutePath, string optionsJson = "{}")
	{
		CheckOwner();
		using var services = new NativeApplicationServices(optionsJson);
		using var path = new Utf8(absolutePath);
		NativeRunResult result = NewRunResult();
		CheckRunStatus(NativeMethods.vs_interop_session_load_application(Handle, path.View, services.Handle, &result), result, "load application");
		return ToManaged(result);
	}
}
