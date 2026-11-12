# 07: Modules, imports, and the preprocessor

Once a program spans more than one file, modules organize dependencies and public APIs. The preprocessor selects source before compilation. This tutorial starts with a private implementation and exports only the function that other files need.

## Step 1: create a module

Create `math_utils.vs`:

```VeroScript
export int square(int value)
{
	return value * value;
}

int hidden_value()
{
	return 99;
}
```

`export` makes `square` part of the module's public declarations. `hidden_value` is private to the module and is not a public library API.

Create `main.vs`:

```VeroScript
import "math_utils.vs";

int main()
{
	print(square(7));
	return 0;
}
```

Run both files from the same directory:

```powershell
veroscript --check main.vs
veroscript main.vs
```

## Step 2: re-export a module

Create `facade.vs`:

```VeroScript
export import "math_utils.vs";
```

The entry point can now import only the facade:

```VeroScript
import "facade.vs";

int main()
{
	print(square(8));
	return 0;
}
```

Re-exporting continues to expose declarations that the original module exported; it does not make `hidden_value` public.

## Step 3: add a compile-time condition

The preprocessor selects source at compile time; use ordinary runtime `if` statements for runtime decisions:

```VeroScript
#define MESSAGE "development"

#ifdef RELEASE
#define MESSAGE "release"
#endif

int main()
{
	print(MESSAGE);
	return 0;
}
```

Define `RELEASE` on the command line:

```powershell
veroscript --check main.vs
veroscript -D RELEASE main.vs
```

Macros expand before compilation. Avoid giving macros the same names as functions, and use a real function when the logic becomes substantial.

## Step 4: group declarations with an anonymous namespace

An anonymous namespace uses `namespace { ... }`. It groups declarations without adding another qualification. It is transparent in VeroScript; it does not create a synthetic namespace or provide C++-style internal linkage.

```VeroScript
namespace
{
	int local_helper()
	{
		return 6;
	}
}

int main()
{
	print(local_helper());
	return 0;
}
```

Inside a named namespace, the members retain the enclosing namespace:

```VeroScript
namespace Diagnostics
{
	namespace
	{
		int code()
		{
			return 200;
		}
	}
}

int main()
{
	print(Diagnostics::code());
	return 0;
}
```

Use `export` on individual declarations when another module must see them. An anonymous namespace itself is not exported and does not make declarations private by itself.

## Exercises

1. Export a `cube` function and call it from the main module.
2. Create a facade that re-exports both the string module and your math module.
3. Add an `#if` branch that prints extra information in a debug build.
4. Place a helper in an anonymous namespace inside a named namespace and call it through the enclosing qualification.

See [Modules](../../../../../VeroScript/Resources/Docs/User/en-US/language-basics/modules.md), [Preprocessor](../../../../../VeroScript/Resources/Docs/User/en-US/language-basics/preprocessor.md), and [Namespaces](../../../../../VeroScript/Resources/Docs/User/en-US/language-basics/namespaces.md). The distributed examples are [`examples/reexport_main.vs`](../../../../../VeroScript/Resources/Scripts/examples/reexport_main.vs) and [`examples/preprocessor.vs`](../../../../../VeroScript/Resources/Scripts/examples/preprocessor.vs).

Next: [Strings, files, and paths](../standard-library/08-strings-files-and-paths.md).

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](07-modules-and-preprocessor.md) · [fr-FR](../../fr-FR/language/07-modules-and-preprocessor.md) · [ja-JP](../../ja-JP/language/07-modules-and-preprocessor.md) · [ko-KR](../../ko-KR/language/07-modules-and-preprocessor.md) · [zh-CN](../../zh-CN/language/07-modules-and-preprocessor.md) · [zh-TW](../../zh-TW/language/07-modules-and-preprocessor.md)
