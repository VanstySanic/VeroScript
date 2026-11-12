# Script compilation extensions

<a id="source-extension-activation"></a>

Use an extension supplied by your host or application to process annotations or generate declarations before normal checking. Activate source extensions with import compiler at the start of the entry file, following optional comments and earlier compiler imports. Ordinary import does not execute extension registration.

## Descriptive extension

Save the first file as notes.vs and the second as main.vs. This extension only documents @Note; it does not generate business behavior.

```VeroScript
import "compiler.vs";

export void register_extension(std::compiler::registration reg)
{
	std::compiler::annotation_definition note = std::compiler::annotation_definition();
	note.name = "@Note";
	std::compiler::configuration_value label = std::compiler::configuration("note.text");
	note.documentation = label.exists() ? label.value() : "A descriptive annotation.";
	note.targets = array<std::compiler::target>{std::compiler::target::class_type};
	reg.register_annotation(note);
}
```

```VeroScript
import compiler "notes.vs";

@Note class Message {};

int main()
{
	type<Message> info = type<Message>();
	print(info.annotation(0).definition().documentation());
	return 0;
}
```

```sh
veroscript --backend=vm --check main.vs
veroscript --backend=vm main.vs
```

The program prints A descriptive annotation. The Runtime standard library must include compiler.vs. core.vs does not re-export this compilation-only entry.

## Packaged extensions and limits

compiler-package produces a .vslib extension without running registration. To consume it, save consumer.vs with the same business program but omit import compiler, then select notes.vslib explicitly. Do not activate the source and package copies together.

```sh
veroscript compiler-package notes.vs notes.vslib
veroscript --backend=vm --compiler-extension notes.vslib consumer.vs
```

See [Command-line tool — Script compilation extensions](../../../../../../VeroScriptCli/Resources/Docs/User/en-US/index.md#cli-compiler-options).

```sh
veroscript --backend=vm --compiler-extension notes.vslib --compiler-config "note.text=Configured annotation." consumer.vs
```

```text
Configured annotation.
```

[Annotations](../language-basics/annotations.md)

The default total extension budget is 1,000,000 VM instructions and 2,000 ms. A zero value disables the corresponding budget; the limits do not preempt a blocking native callback.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md) · [en-US](compiler.md) · [fr-FR](../../fr-FR/standard-library/compiler.md) · [ja-JP](../../ja-JP/standard-library/compiler.md) · [ko-KR](../../ko-KR/standard-library/compiler.md) · [zh-CN](../../zh-CN/standard-library/compiler.md) · [zh-TW](../../zh-TW/standard-library/compiler.md)
