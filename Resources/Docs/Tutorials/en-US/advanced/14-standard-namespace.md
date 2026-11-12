# 14: Use the `std` standard namespace

This tutorial refactors a small script that uses unqualified module declarations into the recommended public `std` spelling. `std::` is a source-level alias for standard names; it does not create a second container type or change runtime behavior.

## Step 1: prepare a score script

Create `scores.vs`:

```VeroScript
import "math.vs";
import "string.vs";

int main()
{
	array<int> scores = {81, 92, 88};
	optional<int> selected = optional<int>(scores[1]);
	double root = std::sqrt(81.0);

	print(format("selected={}, root={}", selected.get(), root));
	return 0;
}
```

Check and run it first:

```powershell
veroscript --check scores.vs
veroscript scores.vs
```

The first version already qualifies the module-owned math function. Module-owned standard-library functions are available only through their public `std` namespace; only the small core builtin surface remains global.

## Step 2: migrate to `std::`

Change only the name qualification, not the algorithm:

```VeroScript
import "math.vs";
import "string.vs";

int main()
{
	std::array<int> scores = {81, 92, 88};
	std::optional<int> selected = std::optional<int>(scores[1]);
	double root = std::sqrt(81.0);

	std::print(std::format("selected={}, root={}", selected.get(), root));
	std::print(std::strings::contains("scores", "core"));
	return 0;
}
```

Run the same check and execution commands again. `std::array<int>` and `array<int>` refer to the same language type, and `std::print` and `print` refer to the same runtime function. `std::` is resolved during compilation and adds no runtime dispatch.

Math, I/O, filesystem, string, path, concurrency, time, thread, environment, and typed-input entry points are namespace-only. Their unqualified standard-library spellings, as well as flat compatibility names such as `std::now`, are not provided. User code may still define an unrelated function with the same unqualified name.

## Step 3: understand import boundaries

A standard-namespace alias is not a module import. Source standard-library functions still require the module that provides them:

- `math.vs` provides `sqrt`, `sin`, `cos`, `tan`, `floor`, `ceil`, `round`, `trunc`, `pow`, `exp`, `log`, `log2`, `asin`, `acos`, `atan2`, `isfinite`, `isnan`, `pi`, `e`, `clamp`, and `clamp_int`.
- `string.vs` provides string queries through `std::strings`, including `last_find`, `repeat`, `remove_prefix`, and `remove_suffix`.
- `io.vs` provides stream operations through `std::io` and filesystem operations through `std::filesystem`.
- `path.vs` adds path operations to `std::filesystem`; `concurrent.vs` uses `std::concurrent`.

Runtime console functions do not require a module import. Use
`std::console::write` and `std::console::error` for newline-free output,
`std::console::flush()` to publish pending output, and
`std::console::is_interactive()` to detect an interactive terminal.

For example, removing `import "math.vs";` does not make `std::sqrt` load the math module automatically. Import the module first, then use the qualified name so the dependency remains explicit.

## Step 4: migrate containers and value types

The standard namespace also covers maps, sets, queues, channels, tasks, and reflection information types:

```VeroScript
int main()
{
	std::map<string, int> totals = {"math": 2, "io": 1};

	std::print("math={}, io={}", totals["math"], totals["io"]);
	return 0;
}
```

Use the grouped `std` spelling consistently. Standard-library modules export reserved implementation symbols and the namespace layer maps public names to those symbols without runtime dispatch.

## Step 5: keep value protection and conversions explicit

The current example also combines the `std::readonly<T>` alias with the global conversion built-ins. `std::` does not prefix every built-in: keep `cast<T>`, `try_cast<T>`, and `const_cast<T>` unqualified.

```VeroScript
std::readonly<int> published_score = std::readonly<int>(selected.get());
double exact_score = cast<double>(scores[1]);
std::optional<int> narrowed = try_cast<int>(exact_score);

const std::array<int> explicitly_unlocked = {13};
// Use this deliberate const escape only when the underlying storage is known writable.
const_cast<std::array<int>>(explicitly_unlocked).push(published_score.get());
```

`cast<T>` requires the conversion, while `try_cast<T>` returns an `optional<T>` that the caller can inspect. `const_cast<T>` does not convert a value or perform a runtime type check; it only removes compile-time const access and therefore requires a deliberate, controlled use. The complete rules are in [Type conversions](../../../../../VeroScript/Resources/Docs/User/en-US/language-reference/type-conversions.md).

## Root-qualified names

A leading `::` starts lookup at the global/root scope. It bypasses the current namespace, local aliases, and `using namespace` fallback:

```VeroScript
int GlobalValue = 3;

namespace Feature
{
	int GlobalValue = 9;

	int read_root_value()
	{
		return ::GlobalValue;
	}
}
```

Use `::Name` when the root declaration is intentional. `std::readonly<T>` is also the standard-namespace spelling of the `readonly<T>` wrapper; it is a source alias, not a second runtime type.


## Exercises

1. Do not replace the example with `std::unordered_map`; consult the complete alias table and explain which supported container is appropriate instead.
2. Add a `std::set<int>` and use it to remove duplicate scores.
3. Import `string.vs` and call `std::strings::contains` or `std::strings::starts_with`.

See the complete [`std` standard namespace](../../../../../VeroScript/Resources/Docs/User/en-US/language-basics/standard-namespace.md) table. The runnable example is [`examples/standard_namespace.vs`](../../../../../VeroScript/Resources/Scripts/examples/standard_namespace.vs).

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](14-standard-namespace.md) · [fr-FR](../../fr-FR/advanced/14-standard-namespace.md) · [ja-JP](../../ja-JP/advanced/14-standard-namespace.md) · [ko-KR](../../ko-KR/advanced/14-standard-namespace.md) · [zh-CN](../../zh-CN/advanced/14-standard-namespace.md) · [zh-TW](../../zh-TW/advanced/14-standard-namespace.md)
