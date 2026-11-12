# 12: Host bindings and script integration

One important VeroScript use case is embedding it in a C++ host. The host creates an engine or session and registers functions and types; the script calls the stable public surface. This tutorial first uses a binding catalog to make that boundary concrete, then describes the integration order.

## Step 1: inspect a binding catalog

A binding catalog describes the host API in JSON. Copy [bindings.sample.json](../../../../../VeroScript/Resources/Scripts/examples/bindings.sample.json) from these documentation resources or the separate Resources distribution into a writable lesson directory as `bindings.sample.json`. SDK archives do not include examples. Run the following commands from that lesson directory:

```powershell
veroscript bindings validate bindings.sample.json
veroscript bindings format bindings.sample.json -o formatted-bindings.json
```

A catalog includes top-level fields such as `version`, `functions`, `genericTypeTemplates`, `types`, `typeAliases`, and `annotations`. It describes an interface; it does not replace C++ registration code or provide a runtime implementation by itself.

Treat the catalog as the interface list visible to the compiler: a function requires at least `name`, `returnType`, and `paramTypes`; host objects and containers also need the corresponding host-type, element, or key/value metadata. Reference parameters use `paramFlags` parallel to `paramTypes`, and parameter annotations use parallel `paramAnnotations`. These fields must match the C++ registration signature.

For example, the sample catalog's `host_add` function entry uses two `int` parameters and annotates the first parameter:

```json
{
	"name": "host_add",
	"returnType": "int",
	"paramTypes": [
		"int",
		"int"
	],
	"paramFlags": [
		0,
		0
	],
	"paramAnnotations": [
		[
			{
				"name": "HOST_PARAM",
				"arguments": [
					{
						"name": "DisplayName",
						"value": "Left"
					}
				]
			}
		],
		[]
	]
}
```

Do not treat catalog metadata arrays as runtime implementations; they describe the interface and tooling surface only. See [Host bindings reference](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/bindings.md) for every field, supported type names, generic templates, aliases, and JSON limits.

## Step 2: call host capabilities from a script

After the host registers a function, a script can call it like an ordinary declaration. For example, assume the host registered `HostLog`:

```VeroScript
int main()
{
	HostLog("script started");
	return 0;
}
```

For a type with static members, the script normally uses the type name:

```VeroScript
int main()
{
	int value = HostMath.clamp(120, 0, 100);
	print(value);
	return 0;
}
```

These names only demonstrate the call shape; the actual names must come from the host registration and catalog.

## Step 3: integrate in order

In the C++ host, use this sequence:

Before execution, select a compatible VM through `FEngineConfig::VmBackendFactory`/`VmBackendPath` or the Session selection API. Linking the C++ SDK alone does not select an interpreter. Script compile processors also need a VM. Follow the [VeroScriptBytecodeVm](../../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/User/en-US/index.md) for the required SDK targets and setup.

1. Create a `VeroScriptEngine` or a long-lived `VeroScriptSession`.
2. Register native functions, host types, members, and operators.
3. Add documentation and parameter metadata for APIs that tooling should expose.
4. Export or load the binding catalog and run `bindings validate`.
5. Call `CheckSource`/`CheckFile` first, then `RunSource`/`RunFile` after checking succeeds.
6. Preserve diagnostics for check and run failures instead of returning only a boolean.

Editors, games, and host tools with a long script lifetime normally use `VeroScriptSession` for modules, bindings, execution state, and hot reload. One-shot execution can use the source and file entry points on `VeroScriptEngine`.

## Step 4: keep the interface stable

Treat bindings as a versioned API. Adding a function is normally a compatible extension; removing members, changing parameter types, changing enum values, or changing ownership semantics can break scripts. Before shipping a host version, check:

- The binding catalog validates.
- Scripts pass `--check`.
- The registration table matches the catalog.
- Object, struct, and handle lifetimes are documented.

If a registered static host member has an explicit `HostObject` parameter, you
can also test the nullable boundary with `nullptr`. The callback receives an
empty host-object handle, so check it before dereferencing the payload. A null
instance receiver remains invalid.

## Exercises

1. Design a stable name, parameter list, and return type for one host function.
2. Add short documentation and parameter descriptions to the binding.
3. Modify one catalog entry and run both catalog validation and script checking.

See [Host integration](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/embedding.md) and [Host bindings](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/bindings.md) for the complete boundaries.


Next: [Runtime reflection](13-reflection.md).

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](12-host-bindings.md) · [fr-FR](../../fr-FR/advanced/12-host-bindings.md) · [ja-JP](../../ja-JP/advanced/12-host-bindings.md) · [ko-KR](../../ko-KR/advanced/12-host-bindings.md) · [zh-CN](../../zh-CN/advanced/12-host-bindings.md) · [zh-TW](../../zh-TW/advanced/12-host-bindings.md)
