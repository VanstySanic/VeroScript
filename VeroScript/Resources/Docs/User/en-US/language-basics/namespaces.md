# Namespaces

Namespaces organize names and prevent collisions between separate areas of a script. A namespace can contain variables, functions, structs, classes, interfaces, enums, and other declarations.

## Declaring and qualifying names

Declare a namespace with `namespace`, and access its members with `::`:

```VeroScript
namespace Util
{
	int Seed = 10;

	int next_seed()
	{
		Seed = Seed + 1;
		return Seed;
	}
}

int current = Util::Seed;
int next = Util::next_seed();
```

Qualified names make the owning namespace explicit. Prefer `::` for type members, enum members, and static members as well:

```VeroScript
enum class State
{
	Ready,
	Running
};

State state = State::Running;
```

## Global qualification

A leading `::` explicitly begins name lookup at the global/root scope:

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

The root qualifier bypasses the current namespace, local aliases, and `using namespace` fallback. Use `::Name` when a root declaration is intended rather than a declaration found through ordinary lexical lookup. It can also qualify a root-level type or namespace, and it is accepted in `using namespace ::Name`.

## Anonymous namespaces

`namespace { ... }` declares an anonymous namespace. In VeroScript, it is a transparent declaration group: it does not create a synthetic namespace and does not provide C++-style translation-unit linkage. Its members remain in the surrounding namespace.

At global scope, members of an anonymous namespace can be used without an additional qualification:

```VeroScript
namespace
{
	int transparent_helper()
	{
		return 40;
	}
}

int main()
{
	print(transparent_helper() + 2);
	return 0;
}
```

The same rule applies inside a named namespace. The members retain the enclosing namespace qualification:

```VeroScript
namespace Outer
{
	namespace
	{
		int nested_helper()
		{
			return 9;
		}
	}
}

int main()
{
	print(Outer::nested_helper());
	return 0;
}
```

An anonymous namespace does not change export or module visibility. Export individual declarations when another module should use them; use an anonymous namespace when a transparent grouping improves source organization.

## The standard namespace

Built-in containers, utility value types, and public runtime functions have a
recommended spelling under the reserved `std` namespace:

```VeroScript
import "string.vs";

std::array<int> values = {1, 2, 3};
std::optional<int> selected = std::optional<int>(values[0]);
std::print(std::format("selected: {}", selected.get()));
```

Existing global spellings such as `array`, `optional`, `print`, and `format`
remain compatibility aliases. `std::` is resolved at compile time and adds no
VM or JIT dispatch. Source-library functions such as `std::sqrt` still require
their normal module import; qualification does not bypass `import`/`export`.
The global `std` namespace is reserved and cannot be extended by scripts.

A namespace organizes names but does not automatically change their export status. Whether a declaration is visible outside its module is still controlled by the module's `export` rules.

See [The `std` namespace](standard-namespace.md) for the complete type and function alias tables.

## Using a namespace

`using namespace` makes direct members of a namespace available in the containing scope:

```VeroScript
namespace MathTools
{
	int base = 2;
}

using namespace MathTools;

int value = base + 3;
```

The directive affects its containing scope and the code that follows it. It does not re-export names or affect sibling namespaces. Keep the full qualification when names may collide:

```VeroScript
int value = MathTools::base + 3;
```

Use namespaces for stable name boundaries. Use a module `import` when the goal is to bring declarations in from another source file.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](namespaces.md) · [fr-FR](../../fr-FR/language-basics/namespaces.md) · [ja-JP](../../ja-JP/language-basics/namespaces.md) · [ko-KR](../../ko-KR/language-basics/namespaces.md) · [zh-CN](../../zh-CN/language-basics/namespaces.md) · [zh-TW](../../zh-TW/language-basics/namespaces.md)
