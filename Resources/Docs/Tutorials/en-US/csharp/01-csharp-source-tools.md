# 01: Generate C# binding artifacts

Generate a binding catalog, API reference, live registration source and an AOT bridge from the same managed declarations. The semantic frontend analyzes source at build time; it does not create a script session.

Use matching `veroscript-csharp-bindgen` and `veroscript` tools on PATH, a writable lesson directory and a .NET host project compatible with the selected SDK. Run every command from that directory. Generation produces build inputs; compiling the host and registering callbacks are separate steps.

The AOT run below uses the generated `net8.0` project. Install a .NET SDK that can build that target and a matching .NET runtime to run it.

## Step 1: prepare the inputs

Save the host declaration as `HostApi.cs`:

```csharp
public sealed class HostApi
{
	public static int Add(int left, int right)
	{
		return left + right;
	}
}
```

Save the caller as `app.vs`:

```VeroScript
int main()
{
	print(HostApi.Add(20, 22));
	return 0;
}
```

## Step 2: generate live registration

```powershell
veroscript-csharp-bindgen HostApi.cs `
	--catalog generated/bindings.json `
	--registration generated/Bindings.g.cs `
	--api-reference generated/Bindings.vs.txt
veroscript bindings validate generated/bindings.json
```

Expect `bindings.json`, `Bindings.g.cs` and `Bindings.vs.txt`. Compile the registration source with the managed C# SDK and the original `HostApi.cs`, and invoke its generated registration entry before loading the script. The catalog describes the interface; it does not supply runtime callbacks. A configured host running this caller prints `42` and returns zero.

In the configured VM host, invoke this generated entry before loading `app.vs`; `session` is the host’s existing `VeroScriptSession`:

```csharp
VeroScript.GeneratedBindings.VeroScriptCSharpBindings.Register(session);
```

## Step 3: generate an AOT bridge

```powershell
veroscript-csharp-bindgen HostApi.cs `
	--catalog generated/bindings.json `
	--aot-bridge generated/Bindings.Aot.g.cs
veroscript aot --target=csharp app.vs `
	--binding-catalog generated/bindings.json `
	--output-dir generated/app --bundle-name App
```

Compile `HostApi.cs`, `Bindings.Aot.g.cs` and all generated application units in the same .NET project. The bridge makes direct managed calls without `VeroScriptInterop`. Keep the generated project runtime dependencies; source generation alone is not an executable.

The generated `generated/app/App.csproj` sets `EnableDefaultCompileItems=false` and lists its generated units explicitly. Add the following `ItemGroup` inside that project’s `Project` element, keeping the existing items. Paths are relative to `App.csproj`:

```xml
<ItemGroup>
	<Compile Include="../../HostApi.cs" />
	<Compile Include="../Bindings.Aot.g.cs" />
</ItemGroup>
```

Run the managed application from the lesson directory. The expected output is `42`, with exit code zero:

```powershell
dotnet run --project generated/app/App.csproj --configuration Release
```

Use `Bindings.Aot.g.cs` in this AOT project. `Bindings.g.cs` belongs to the VM host from step 2. Regenerating the bundle replaces `App.csproj`; apply these additional inputs again after regeneration.

## Step 4: generate one project

Create `Models/State.cs` with the following declaration; keep `HostApi.cs` from step 1:

```csharp
public enum HostState
{
	Ready,
	Done
}
```

```powershell
veroscript-csharp-bindgen HostApi.cs Models/State.cs `
	--catalog generated/bindings.json `
	--registration generated/Bindings.g.cs `
	--aot-bridge generated/Bindings.Aot.g.cs `
	--api-reference-dir generated/api `
	--manifest generated/bindings.manifest.json
```

Pass both files together, then keep catalog, registration, bridge, references and manifest from the same run. Partial/related declarations are merged before output. Same-stem files receive stable path-hash suffixes.

If you rebuild the AOT application after this project-generation step, first repeat the AOT command in step 3 with the new merged catalog. Add its project inputs again, including `<Compile Include="../../Models/State.cs" />`, before building. Keep the catalog, bridge, host declarations and generated application from this same input set.

## Step 5: check the boundary

Compare the catalog function names and signatures with the host source and inspect the generated API reference. Public declarations are considered by default. To select marked types, define/use `[VeroScriptBinding]` and pass `--require-attribute`; `[VeroScriptIgnore]` excludes a declaration. Unsupported nullable, pointer or adapter-specific signatures fail with diagnostics. Use an explicit wrapper rather than editing generated metadata to hide a mismatch.

## Exercises

1. Add a supported read-only property and inspect its generated contract.
2. Define a binding attribute, mark one type and compare output with `--require-attribute`.
3. Explain which host consumes live registration and which consumes the AOT bridge.
4. Add an unsupported nullable signature, read the diagnostic, then restore the supported API.

[BindGen reference](../../../../../Extensions/Language/CSharp/Resources/Docs/User/en-US/bindgen.md) · [C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/en-US/aot.md) · [C# source conversion](../../../../../Extensions/Language/CSharp/Resources/Docs/User/en-US/transpiler.md) · [Runtime values and ownership](../../../../../Extensions/Language/CSharp/Resources/Docs/User/en-US/index.md)

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](01-csharp-source-tools.md) · [fr-FR](../../fr-FR/csharp/01-csharp-source-tools.md) · [ja-JP](../../ja-JP/csharp/01-csharp-source-tools.md) · [ko-KR](../../ko-KR/csharp/01-csharp-source-tools.md) · [zh-CN](../../zh-CN/csharp/01-csharp-source-tools.md) · [zh-TW](../../zh-TW/csharp/01-csharp-source-tools.md)
