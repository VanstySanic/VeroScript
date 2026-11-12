# C# host bindings

Register host declarations before loading source that uses them. The managed
API describes signatures recursively with `VeroScriptNativeType`; callback
annotations or C# reflection do not replace an explicit VeroScript signature.

## Native type descriptions

`VeroScriptNativeType` supports `Void`, all fixed-width integer and floating
types, `Bool`, `String`, `HostObject`, `Enum`, containers, and `Function`.
Container and function types use `TypeArguments`. Function names at the native
boundary use canonical signatures such as `fn(int)->int`, while VeroScript
source spells the same type as `function(int)->int`.

`VeroScriptNativeParameter` combines a type with `None`, `Ref`, or `ConstRef`.
Use `Ref` only when the callback writes back to the script storage.

The registration surface includes the compact `RegisterFunction` API and the
metadata-oriented `RegisterHostFunction`, `RegisterAnnotation`,
`RegisterHostType`, `RegisterHostMember`, `RegisterHostProtocol`,
`RegisterTypeAlias`, `RegisterGenericHostTypeTemplate`, and
`InstantiateGenericHostType` methods. Register declarations before loading
source that refers to them.

## Functions and callbacks

```CSharp
session.RegisterFunction(
	"host_double",
	VeroScriptNativeType.Int32,
	[VeroScriptNativeType.Int32],
	(context, result) =>
	{
		result.Set(context.GetArgument(0).GetInt32() * 2);
	});
```

The callback receives a `VeroScriptCallContext` and a borrowed return value.
Use `GetArgument` for read-only arguments and `GetMutableArgument` for a
declared mutable reference. `InvokeFunction` and `InvokeCallable` can call
script targets from a callback. `ArgumentCount`, `ProgramGeneration`, and
`GetHostThis` expose callback context; write the return value into the supplied
view and call `context.SetError(...)` for a deliberate failure. Managed
exceptions are translated into callback failures.

For richer metadata use `VeroScriptHostFunctionDefinition`, including
documentation, parameter names and documentation, annotations, type
parameters, and generic host-object flags.

## Host types and members

`VeroScriptHostTypeDefinition` describes enum, struct, and class host types.
`VeroScriptHostMemberDefinition` registers constructors, methods, static
methods, properties, fields, static constants, enum members, and operators.
Members can describe const methods, mutable receivers, parameter metadata,
annotations, and return documentation.

Generic host types use `VeroScriptGenericHostTypeContext`. A factory receives
the concrete type arguments and can add members or install clone, equality,
hash, and subclass protocols. Protocol callbacks are retained by the session.
The factory context exposes `ArgumentCount`, `GetArgument`, `SetType`,
`AddMember`, `SetProtocol`, and `SetError`; its borrowed arguments are valid
only during the factory callback.

## Interception and mutation

An instance interceptor receives a resolved method and arguments. Install it
with `SetInstanceCallInterceptor`. It may return `Unhandled`, provide a
replacement result, call `Proceed`, or return `Failed`. Use
`CallMethodBypassingInterceptor` when a host operation must avoid the
interceptor. `VeroScriptInterceptContext` exposes `Receiver`, `ArgumentCount`,
`GetTarget`, `GetArgument`, `Proceed`, and `SetError`; its receiver and
arguments are borrowed for the callback scope.

Mutation handlers are installed with `SetMutationHandler` and observe field
writes and container additions, removals, sets, and clears. Mutation views are
callback-scoped borrowed values. A notification cannot roll back a mutation
that has already completed.

## Serialization and references

`RegisterExternalSerializer` installs external persistence callbacks.
`SerializeObject` and `DeserializeObject` exchange an arbitrary byte payload
with an encoding version, while `ApplyClassDefaults` applies registered
defaults after object creation or loading. `EnumerateObjectReferences` visits
script and host references; borrowed values expire after each visitor callback.
`RetainCallable` creates an owned callable wrapper for a script function or
closure. Serialization callbacks use `TypeName`, `MutationVersion`,
`FieldCount`, `GetField`, and `SetBytes` on the serialize side, and
`EncodedVersion`, `GetBytes`, `AddField`, and `SetError` on the deserialize
side.

These facilities are for persistence and object-graph management. They do not
make package bytes or host libraries trusted; validate the expected package identity and
native library provenance separately.

## Annotation name policy

`RegisterAnnotation` uses `VeroScriptAnnotationDefinition.CaseSensitive = true`
by default. Set it to `false` to fold ASCII letters in one annotation's name.
Argument names and metadata keys remain exact, and reflection retains source
spelling. Overlapping exact and folded registrations fail without changing the
catalog; `ExportBindingsJson()` preserves `caseSensitive`. Registration also
supplies documentation for compilation, reflection and tooling; processors are
installed separately. Deploy the adapter with the matching current native SDK,
whose annotation descriptor includes the name-policy field.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](host-bindings.md) · [fr-FR](../fr-FR/host-bindings.md) · [ja-JP](../ja-JP/host-bindings.md) · [ko-KR](../ko-KR/host-bindings.md) · [zh-CN](../zh-CN/host-bindings.md) · [zh-TW](../zh-TW/host-bindings.md)
