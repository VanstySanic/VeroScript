// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using Foundation;

namespace VeroScriptMauiHost;

[Register("AppDelegate")]
public sealed class AppDelegate : MauiUIApplicationDelegate
{
	protected override MauiApp CreateMauiApp() => MauiProgram.CreateMauiApp();
}
