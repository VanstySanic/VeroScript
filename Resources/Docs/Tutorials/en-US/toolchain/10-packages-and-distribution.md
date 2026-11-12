# 10: Building packages and distributing an application

When a script becomes a deliverable program, package it as an application or a library. `.vsapp` is intended to run, while `.vslib` is intended to be imported by another script.

## Step 1: create a library module

Create `package_library.vs`:

```VeroScript
export enum class OperationKind
{
	Add
};

export int calculate(int left, int right)
{
	return left + right;
}
```

Only exported declarations become part of the library API. Build and inspect the library:

```powershell
veroscript --emit-package DemoLibrary.vslib package_library.vs
veroscript package inspect DemoLibrary.vslib
```

## Step 2: create the application entry point

Create `package_app.vs`:

```VeroScript
#ifdef VERO_USE_BINARY_PACKAGE
import "DemoLibrary.vslib";
#else
import "package_library.vs";
#endif

int main()
{
	print(calculate(20, 22));
	return 0;
}
```

Build and run the application:

```powershell
veroscript -D VERO_USE_BINARY_PACKAGE --emit-package DemoApp.vsapp package_app.vs
veroscript package inspect DemoApp.vsapp
veroscript DemoApp.vsapp
```

The application package contains its entry point and dependencies, so the source library package does not need to sit beside it at runtime. `package_app.vs` imports the source module by default; `-D VERO_USE_BINARY_PACKAGE` switches it to the generated `DemoLibrary.vslib`, which must therefore be available while compiling the application.

## Step 3: review the public boundary

An unexported function can be used inside its own library source, but downstream scripts cannot call it. Before publishing a package, check:

1. Only the intended types and functions are exported.
2. Parameter types, return types, and enum values are stable.
3. The package version is compatible with the target runtime and bindings.
4. `package inspect` has been run on the generated files.

The distributed examples include a complete package workflow in [`examples/package/README.md`](../../../../../VeroScript/Resources/Scripts/examples/package/README.md), [`package_library.vs`](../../../../../VeroScript/Resources/Scripts/examples/package/package_library.vs), and [`package_app.vs`](../../../../../VeroScript/Resources/Scripts/examples/package/package_app.vs).

## Exercises

1. Add an exported `subtract` function.
2. Remove `export` and observe the application compile error.
3. Set a package version and record the exact build command used for release.

See [Packages and distribution](../../../../../VeroScriptCli/Resources/Docs/User/en-US/packages.md). Next: [Debugging and AOT generation](11-debugging-and-aot.md).

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](10-packages-and-distribution.md) · [fr-FR](../../fr-FR/toolchain/10-packages-and-distribution.md) · [ja-JP](../../ja-JP/toolchain/10-packages-and-distribution.md) · [ko-KR](../../ko-KR/toolchain/10-packages-and-distribution.md) · [zh-CN](../../zh-CN/toolchain/10-packages-and-distribution.md) · [zh-TW](../../zh-TW/toolchain/10-packages-and-distribution.md)
