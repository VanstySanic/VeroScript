# Python host bindings

Owned host-object `Value` instances and their subtype checks remain safe after the originating `Session` closes. Type lookup uses weak Python class references. Values with clone/equality/hash protocols retain the callback graph needed to use those protocols, including as map/set keys, until the final related value is released. Values without these protocols do not retain unrelated classes or the session callback graph.

Register host functions and classes before loading source that refers to them.
The Python adapter always supplies an explicit VeroScript signature; callback
annotations alone do not define the script-visible type.

## Typed functions

```Python
from veroscript import HostParameter, Session, Value

with Session() as session:
	session.register_function(
		"py_add",
		lambda left, right: left + right,
		return_type="int",
		parameters=("int", "int"),
		documentation="Adds two integers.",
	)

	@session.host_function(
		"py_bump",
		parameters=(HostParameter("int", by_ref=True),),
	)
	def py_bump(value: Value) -> None:
		value.value += 1
```

Scalar parameters are converted to ordinary Python values. A mutable `ref`
parameter is delivered as `Value`, and assigning its `.value` writes back to
the script. `HostParameter` also carries an optional name and documentation;
`const_ref` exposes a read-only reference.

The declared type names must be valid VeroScript/Interop names. Use recursive
container or function signatures where needed, for example
`array<int>`, `map<string, int>`, or `fn(int)->int` according to the binding
contract. A host callback must return a value compatible with its declared
return type.

## Python-backed classes

```Python
class Counter:
	def __init__(self, initial: int) -> None:
		self.value = initial

	def add(self, delta: int) -> int:
		self.value += delta
		return self.value

with Session() as session:
	counter = session.register_class("Counter", Counter)
	counter.constructor(parameters=("int",))
	counter.method("Add", "add", return_type="int", parameters=("int",))
```

`register_class` creates a host class description. `HostClass` exposes
`constructor(...)`, `method(...)`, `static_method(...)`, `property(...)`, and
`field(...)` for constructors, instance/static methods, Python property
descriptors, and ordinary attributes. Host class members currently use
explicitly described signatures. Host function and member signatures may use
scalars, registered host objects, `enum<Name>`, and nested container spellings
such as `array<int>` and `map<string,PythonObject>`; non-scalar callback values
are represented by `Value`. Host class members currently accept by-value
parameters, while free functions additionally support mutable scalar `ref`
parameters and read-only `const_ref` parameters.

`field(...)` exposes an ordinary instance attribute through a live getter and a
writable setter by default. Pass `writable=False` for read-only access.
`property(...)` is intended for Python `property` descriptors and follows the
descriptor's getter/setter contract.

`register_struct(...)` describes a Python-backed value-semantic type. Register a
clone callback with `protocol(...)` whenever VeroScript may copy the value; a
value used as a map or set key also needs equality and hash callbacks. Use
`register_enum(...)` for an `IntEnum` or `IntFlag` host enum; a plain
`enum.Enum` is rejected. Generic host class and struct
templates are registered with `register_generic_class(...)` or
`register_generic_struct(...)` and must be explicitly instantiated with
`instantiate(...)` before dependent source is loaded. For an inheritable host
class, pass `inheritable=True` to `register_class(...)` and provide a
`subclass_factory` through `protocol(...)` when script subclasses need to
create managed instances.

Host objects are reference-counted through the Interop value contract. The
Python object remains rooted while a native value retains its host payload.

When a registered base host type is used in a signature, a managed instance of
a registered compatible subtype may be passed through the same host-object
boundary. The runtime uses the registered subtype relationship during type
validation; it does not infer compatibility from an unrelated Python class.

## Serialization and mutation callbacks

External serialization is registered before loading source:

```Python
session.register_serializer(
	"Counter",
	1,
	lambda type_name, version, fields: str(fields["value"]).encode(),
	lambda data, version: {"value": int(data)},
)
```

The serializer receives the module-qualified type name, mutation version, and
field mapping. The deserializer returns field names mapped to scalar Python
values or explicit `Value` objects. `serialize_object` and
`deserialize_object` perform the corresponding operations on an instance.

`apply_class_defaults(instance)` applies registered class defaults after an
object is created or loaded. `set_mutation_callback` observes field writes and
container mutations. A callback exception is reported after the native operation
returns; it does not
roll back a mutation that has already occurred.

## Metadata and instance interception

`register_annotation(...)` registers an annotation vocabulary and its allowed
arguments. `register_type_alias(...)` registers a concrete or generic type
alias. These declarations affect the host contract and reflection metadata;
they do not infer a callback signature from a Python type annotation.

Annotation names match exactly by default. Use
`register_annotation("@GuiWidget", case_sensitive=False)` to ignore ASCII letter
case in processor subscriptions and script reflection while retaining source
spelling. Argument names and metadata keys stay exact. Overlapping name policies
raise `Error` and leave the existing registration intact. This registration does
not install a compile-time processor.

`set_instance_call_interceptor(...)` receives a resolved instance call through
an `InterceptContext`. Return `InterceptResult.UNHANDLED` to use the normal
implementation, call `context.proceed()` at most once and return
`InterceptResult.HANDLED` to continue with that result, or return
`(InterceptResult.HANDLED, replacement)` to replace the result. Return
`InterceptResult.FAILED` when the call must fail. The context is valid only
while the interceptor callback is running. Trusted host-side code can use
`call_method_bypassing_interceptor(...)` when interception must be skipped.

## Binding and callback limits

The session retains registered callbacks until it is closed. Callback
exceptions are translated to `CallbackError` and do not unwind through the
native ABI. Script callbacks can run on worker threads; protect shared Python
state. A worker callback must not call creator-thread-confined `Session` or
retained-callable methods. Generic host types must be instantiated before
source that names them is loaded, and host structs must provide the protocols
required by their value and key semantics.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](host-bindings.md) · [fr-FR](../fr-FR/host-bindings.md) · [ja-JP](../ja-JP/host-bindings.md) · [ko-KR](../ko-KR/host-bindings.md) · [zh-CN](../zh-CN/host-bindings.md) · [zh-TW](../zh-TW/host-bindings.md)
