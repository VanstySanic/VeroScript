# Enums

An enum represents a finite set of named integer values. VeroScript supports ordinary enums and scoped enums (`enum class`), and allows the underlying integer type to be specified explicitly.

## Declaring an enum

Members of an ordinary enum start at `0` by default, and subsequent members increase by one:

```VeroScript
enum State
{
	Stopped,
	Running,
	Paused
};
```

Members may also be assigned explicit integer values. A member without an explicit value continues from the preceding member:

```VeroScript
enum ErrorCode
{
	None = 0,
	NotFound = 404,
	Timeout = 408
};
```

Enum member values must be integral constant expressions representable by the underlying type. Checked arithmetic, preceding enumerators and qualified [constexpr calls](../language-reference/constexpr.md) are supported; forward references, cycles and overflow are errors.

## Scoped enums

Use `enum class` to keep members inside the enum type's scope:

```VeroScript
enum class RunState : uint8
{
	Stopped = 0u,
	Running = 1u
};

RunState state = RunState::Running;
```

Members of a scoped enum are not introduced as bare names in the enclosing scope. This makes `enum class` a good choice when member names may collide or when different enum types should remain clearly distinct. Documentation and new code should prefer the `Type::Member` form for member access.

Members of an ordinary enum can be used directly:

```VeroScript
State state = Running;
```

## Underlying integer types

When no underlying type is specified, an enum uses `int` by default. To control its representable range explicitly, use fixed-width integer types such as `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`, and `int64`:

```VeroScript
enum class ByteState : uint8
{
	Empty = 0u,
	Full = 1u
};

enum class SignedLimit : int16
{
	Minimum = -32768,
	Maximum = 32767
};
```

An unsigned underlying type cannot represent negative values. Once an underlying type is specified, every member value must fit within that type's range.

## Using enum values

Enum values can be compared and can be used as keys or elements in containers:

```VeroScript
enum class RunState : uint8
{
	Stopped = 0u,
	Running = 1u,
	Paused = 2u
};

map<RunState, string> labels = map<RunState, string>();
labels[RunState::Running] = "running";

set<RunState> states = set<RunState>();
states.add(RunState::Paused);
```

Enums are suitable for finite and stable sets of states. For an `enum class`, preserve its enum type semantics instead of treating it as an arbitrary integer that can be freely mixed with unrelated values.

## Exporting an enum

An enum can be exported for use by other modules through `import`:

```VeroScript
export enum class Operation : uint8
{
	Add = 1u,
	Multiply = 2u
};
```

After importing the module, use the complete enum type name to access scoped members:

```VeroScript
Operation operation = Operation::Add;
```

Nested types, local declarations, access rules and segmented generic arguments are described in [Type scopes](scope.md).

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](enums.md) · [fr-FR](../../fr-FR/language-basics/enums.md) · [ja-JP](../../ja-JP/language-basics/enums.md) · [ko-KR](../../ko-KR/language-basics/enums.md) · [zh-CN](../../zh-CN/language-basics/enums.md) · [zh-TW](../../zh-TW/language-basics/enums.md)
