# Modules

Modules organize declarations around source-file boundaries and control visibility. A module is normally represented by a `.vs` file, imported with `import`, and made available to other modules with `export`.

## Importing a module

Use `import` to import a source module or a library package:

```VeroScript
import "string.vs";

int main()
{
	print(std::strings::join(std::strings::split("a,b,c", ","), "-"));
}
```

When a module is imported more than once, its module identity suppresses duplicate loading. Importing a module does not expose all of its private declarations; only exported declarations are available to external modules.

## Exporting declarations

Place `export` before a declaration to make it available to importing modules:

```VeroScript
export struct Box<T>
{
	T value;

	Box(T initial)
	{
		value = initial;
	}

	T get() const
	{
		return value;
	}
};

int hidden_value()
{
	return 99;
}
```

`Box` is available as part of the module's public API, while `hidden_value` is private because it is not exported. Functions, variables, structs, classes, interfaces, enums, and generic declarations can be exported.

## Re-exporting a module

`export import` imports a module and makes that module's exported declarations available through the current module:

```VeroScript
export import "string.vs";
```

Another module can import the facade and use the declarations re-exported by it:

```VeroScript
import "string_facade.vs";

string text = std::strings::join(std::strings::split("a,b", ","), "-");
```

Re-exporting does not turn private declarations into public ones and does not copy source text.

## Modules and namespaces

Modules handle source-file dependencies and visibility. Namespaces organize names within a module. A module can declare multiple namespaces and can import public declarations from namespaces in another module.

## Importing a library package

Source modules and binary library packages use the same `import` syntax:

```VeroScript
import "DemoLibrary.vslib";
```

A library package can expose only declarations that were exported when it was built. See [Packages and distribution](../../../../../../VeroScriptCli/Resources/Docs/User/en-US/packages.md) for package creation, checking, and distribution.

## Public macros

See [Preprocessor — Public macros](preprocessor.md#exported-macro-contract).

Nested types, local declarations, access rules and segmented generic arguments are described in [Type scopes](scope.md).

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](modules.md) · [fr-FR](../../fr-FR/language-basics/modules.md) · [ja-JP](../../ja-JP/language-basics/modules.md) · [ko-KR](../../ko-KR/language-basics/modules.md) · [zh-CN](../../zh-CN/language-basics/modules.md) · [zh-TW](../../zh-TW/language-basics/modules.md)
