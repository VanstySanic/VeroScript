# Python values and containers

`Value` is the Python boundary for an explicitly typed VeroScript value. Use
it when a script API requires a fixed numeric width, a container type, a
mutable reference argument, an object handle, or a value that should not be
inferred from Python's runtime type.

Each `Value` owns its native handle. Session operations and retained callables
also depend on the session lifetime. Owned host-object values and their
protocols remain usable after the originating session closes; see
[host bindings](host-bindings.md).

## Scalars

```Python
from veroscript import Value

count = Value(4, "int32")
count.value = 5
assert count.value == 5

wide = Value(2.5, "double")
text = Value("hello", "string")
```

The `kind`, `type_name`, and `value` properties expose the value's current
representation. Use `clone()` to get another owned `Value` handle. It follows
runtime clone semantics and does not promise a deep copy. Numeric types
such as `int8`, `uint16`, `int64`, `float`, and `double` remain distinct at the
Interop boundary.

`Value(None)` creates a VeroScript `null` value, and assigning `None` to a
value whose declared type is not nullable raises `TypeError`. A null value is
not the same as an invalid or empty handle.

## Typed containers

Container factories require element or key/value types. This makes empty
containers unambiguous and avoids Python-side type guessing:

```Python
numbers = Value.array("int", [1, 2, 3])
lookup = Value.map("string", "int", {"one": 1})
ordered = Value.map("string", "int", {"first": 1}, ordered=True)
unique = Value.set("int", [1, 1, 2])
pending = Value.queue("string", ["a"])
urgent = Value.priority_queue("int", [3, 1, 2])
history = Value.stack("int", [1, 2])
window = Value.deque("double", [1.0, 2.0])
channel = Value.channel("int")
```

The supported factories are `array`, `map`, `set`, `queue`,
`priority_queue`, `stack`, `deque`, and `channel`. `len`, iteration, indexing,
`get`, `contains`, `keys`, `values`, and `items` expose read-only Python
container behavior. The ordinary `map` and `set` factories use unique-key and
unique-element semantics; use the VeroScript runtime or a host binding when a
multi-map or multi-set is required.
`to_python()` converts a value to ordinary Python data where the type supports
that conversion.

Map and set iteration returns owned snapshots. Scalars become ordinary Python values; other elements have independent owned `Value` handles. The order of an ordinary map or
set is unspecified; an ordered map preserves first-insertion order. Python's
current value surface intentionally does not provide write-through mutation of
container elements; mutate through a VeroScript function or a host callback.

Channels use `send(value)`, `try_send(value)`, `send_for(value, timeout_ms)`,
`receive()`, `try_receive()`, `receive_for(timeout_ms)`, and
`close_channel()`. `try_send` and `send_for` return `bool`; the non-blocking
and timed receive methods return a value or `None`. `count` reports the
current pending element count, `channel_capacity` reports the configured
capacity, and `channel_closed` reports whether the channel is closed. A
blocking receive releases the GIL. The element type must be supported by the
runtime's channel contract.

## Mutable references

Declare a host parameter with `HostParameter`:

```Python
from veroscript import HostParameter, Session, Value

with Session() as session:
	session.register_function(
		"bump",
		lambda value: setattr(value, "value", value.value + 1),
		parameters=(HostParameter("int", by_ref=True),),
	)
	session.load_source("int main() { int x = 4; bump(x); print(x); return 0; }", "ref.vs")
	value = Value(4, "int32")
	session.call("bump", value)
	assert value.value == 5
```

`by_ref=True` marks a mutable script reference; `const_ref=True` marks a
read-only reference. A parameter cannot set both flags. Reference write-back
follows the core runtime contract and does not occur after a failed call or an
explicit exit.

## Objects and callables

`Session.construct` returns a `Value` containing a script or host object.
`get_field`, `set_field`, and `call_method` operate on that value. A script
function value can be retained while its owning session remains open:

```Python
from veroscript import Session

with Session() as session:
	session.load_source("""
int add(int left, int right) { return left + right; }
function(int, int)->int make_callback() { return add; }
void main() {}
""", "callback.vs")
	function_value = session.call("make_callback")
	with session.retain_callable(function_value) as callback:
		result = callback(20, 22)
		assert result == 42
```

Retained callables expose `closed` and `is_valid` so a host can distinguish an
explicitly closed wrapper from a callable whose script target is no longer
valid. They are creator-thread confined by default. `weak_this=True`
avoids retaining the receiver when the callable is a method, but the caller
must then handle an expired receiver according to the runtime result.

## Lifetime and errors

`Value` instances own their native value handles, which are released when the
Python objects are destroyed. The context manager is available for sessions
and retained callables; a `Value` has no separate public `close()` method. A
value returned by a session operation is owned by the caller.
`TypeMismatchError` indicates that the Python value or declared type does not
match the VeroScript value contract.

Owned Python host values retain their clone/equality/hash callbacks while they remain alive, even if the callbacks form a reference cycle or the originating session is closed. Automatic session cleanup on its creator thread preserves these live protocols. Use a session context manager for deterministic cleanup; close the session on its creator thread before handing the final value to another thread.

`count`, `channel_capacity` and `channel_closed` are read-only properties, without parentheses. The following complete example succeeds without output.

```Python
from veroscript import Value

channel = Value.channel("int", 1)
assert channel.channel_capacity == 1 and not channel.channel_closed
assert channel.try_send(42)
assert channel.count == 1
assert channel.receive() == 42
channel.close_channel()
assert channel.channel_closed
```

## Exact numerics

`RuntimeCapabilities.NUMERICS` identifies Core bigint/rational support. These
values remain opaque `Value` instances with `kind == "host_object"`;
`NumericKind(value.numeric_kind)` verifies `BIGINT`, `RATIONAL` or `NONE`.
They are passed to scripts and host callbacks as `Value`, without automatic
conversion to Python integers or floats.

`session.numerics.bigint(text)`, `rational(text)` and
`parse(text, NumericKind.BIGINT)` parse explicit text. `to_text(value)` returns
canonical text. `convert(value, type_name, exact_float=False)` returns a `Value`
preserving the requested fixed integer, `float` or `double` type; overflow and
an inexact conversion with `exact_float=True` raise an error.

```Python
from veroscript import Backend, NumericKind, Session

with Session(backend=Backend.VM) as session:
	numerics = session.numerics
	integer = numerics.bigint("18446744073709551615")
	assert integer.numeric_kind == NumericKind.BIGINT
	assert numerics.convert(integer, "uint64").value == 18446744073709551615
	assert numerics.to_text(numerics.rational("6/18")) == "1/3"
```

`Session.set_numeric_limits` accepts keyword arguments `max_value_bits`,
`max_input_bytes`, `max_output_bytes`, `max_scratch_bytes` and `max_work_units`.
Zero selects finite Core defaults. These operation budgets do not cap the
process's total memory. Session numeric operations require an idle session on
its creator thread. Inside a host callback use `current_numerics()` for the
borrowed active context; its methods fail outside an active callback. Native
numeric operations release the GIL. Standalone Python AOT output does not
support bigint/rational.

## Module-dependent value ownership

See [Interop SDK — Module-dependent value ownership](../../../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md#value-domain-confinement).

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](values.md) · [fr-FR](../fr-FR/values.md) · [ja-JP](../ja-JP/values.md) · [ko-KR](../ko-KR/values.md) · [zh-CN](../zh-CN/values.md) · [zh-TW](../zh-TW/values.md)
