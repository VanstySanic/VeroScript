# VeroScript MAUI example

This application installs SDK resources, then creates, runs and disposes a
VeroScript session on one worker thread. The UI shows the checked script result,
host callback count and ABI/platform identity. Script permissions allow captured
output; input, environment, filesystem access and task creation are disabled.
Imports and the bytecode cache use directories supplied by the application.
The example uses VM execution, cancellation support, instruction/time limits and
a call-depth limit. The host callback increments an integer and accesses no
external resources.

## Prepare the native SDK

Run commands from the repository root. Supply .NET 10 with the MAUI/platform
workloads, the Android SDK/NDK for Android, or macOS with Xcode for iOS.
Prepare a native SDK from matching Android/iOS build artifacts using the
[mobile integration guide](../../../../Resources/Docs/Developer/en-US/mobile.md#native-sdk-preparation).
`VeroScriptMobileNativeRoot` below must be its absolute directory.

## Build and run

Android arm64:

```sh
dotnet build Extensions/Language/CSharp/VeroScriptCSharp/Resources/Examples/Maui/MauiHost.csproj -c Release -p:TargetFrameworks=net10.0-android -f net10.0-android -p:RuntimeIdentifier=android-arm64 -p:VeroScriptMobileNativeRoot=/absolute/path/to/native
```

iOS arm64 simulator, on macOS:

```sh
dotnet build Extensions/Language/CSharp/VeroScriptCSharp/Resources/Examples/Maui/MauiHost.csproj -c Release -p:TargetFrameworks=net10.0-ios -f net10.0-ios -p:RuntimeIdentifier=iossimulator-arm64 -p:VeroScriptMobileNativeRoot=/absolute/path/to/native
```

Use `android-x64` for the x86_64 Android emulator, `iossimulator-x64` for the
x86_64 iOS simulator, or `ios-arm64` for an iOS device with the app owner's signing
settings. Replace the sample directory on Windows with your actual absolute
path. Select `VeroScriptMobilePython` if SDK auditing needs an explicit host
Python executable.

Deploy and launch through the corresponding IDE/device tooling, then select
**Run native integration checks**. A build alone does not execute the checks.
The displayed PASS requires imports, execution, callbacks and returned values
to succeed. Resource installation happens before the worker session is created;
UI updates occur after the worker returns. Cancellation is cooperative and
cannot interrupt a blocking host callback.

This source example uses a ProjectReference and imports the mobile `.targets`
explicitly. A consumer of the mobile-enabled `VeroScript.CSharp` NuGet package
receives those targets through `buildTransitive`.

## Verification boundary

The desktop `Tests/Mobile/MobileContracts.csproj` runs this same `MobileSmoke`
code against desktop Interop and checks resource installation. Its success
does not establish Android/iOS compilation, Release trimming/AOT, APK alignment,
iOS signing or device acceptance. Those checks remain required on the target
platforms; see the [mobile guide](../../Docs/MOBILE.md).
