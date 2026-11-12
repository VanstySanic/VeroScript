// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using Android.App;
using Android.Runtime;

namespace VeroScriptMauiHost;

[Application]
public sealed class MainApplication(nint handle, JniHandleOwnership ownership) : MauiApplication(handle, ownership)
{
	protected override MauiApp CreateMauiApp() => MauiProgram.CreateMauiApp();
}
