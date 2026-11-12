# C# values and ownership

`VeroScriptValue` owns an independent Interop value handle; it is not a session child object.
Dispose owned values after use; `Clone()` creates another owned handle according to runtime clone semantics, without promising a deep copy.
`Reset()` replaces the current value with an empty value. Instances are not
safe for concurrent mutation or disposal.

## Module-dependent payloads

An owned handle does not make its payload independent of a session. Script objects
and functions, and mutable containers with module-value slots, remain confined to
the session's creator thread and become unavailable when it closes, even if empty.
Host-created storage with these slots binds when first used by a session; existing
shared aliases follow that ownership. `Clone()` does not remove these restrictions.
VM worker callbacks may export only independent owned values; empty containers
with variant/function slots cannot be cloned into external owned storage. Private
borrowed callback returns can still forward values within the worker. Dispose
owned handles after use, including after their session has closed.

## Scalar values

The value API keeps numeric widths distinct:

| Set method | Get method | Script kind |
| --- | --- | --- |
| `Set(sbyte)` | `GetInt8()` | `int8` |
| `Set(byte)` | `GetUInt8()` | `uint8` |
| `Set(short)` | `GetInt16()` | `int16` |
| `Set(ushort)` | `GetUInt16()` | `uint16` |
| `Set(int)` | `GetInt32()` | `int32` |
| `Set(uint)` | `GetUInt32()` | `uint32` |
| `Set(long)` | `GetInt64()` | `int64` |
| `Set(ulong)` | `GetUInt64()` | `uint64` |
| `Set(float)` | `GetFloat()` | `float` |
| `Set(double)` | `GetDouble()` | `double` |
| `Set(bool)` | `GetBool()` | `bool` |
| `SetString` | `GetString` | `string` |
| `SetWideString` | `GetWideString` | `wstring` |

`SetEnum(typeName, value)` and `GetEnum()` handle typed enum values. `Kind`
and `TypeName` describe the current value representation.

`SetNull()` creates the script `nullptr` value. `IsNull` only tests for that
value; after `Reset()`, `Kind` is `Invalid` and `IsNull` is false. Accessing a
disposed wrapper throws `ObjectDisposedException`. Typed native calls may
normalize a null host-object value to the declared nullable host reference;
the receiving API still decides whether null is permitted by its signature.

Use `Set(VeroScriptValue)` when assigning a complete runtime value rather than
a scalar. This preserves the runtime identity semantics of containers, host
objects, callables, and channels. The destination must be writable; read-only
borrowed values cannot be used as the destination.

## Containers

Use `SetArray`, `SetSequence`, `SetMap`, `SetSet`, and `SetChannel` with
explicit type names. `VeroScriptValueKind` distinguishes `Array`, `Queue`,
`PriorityQueue`, `Stack`, `Deque`, `Map`, `OrderedMap`, `MultiMap`,
`OrderedMultiMap`, `Set`, `MultiSet`, `OrderedMultiSet`, `Channel`, native
arrays, and other runtime values. The kind-aware overloads
`SetSequence(kind, ...)`, `SetMap(kind, ...)`, and `SetSet(kind, ...)` select
the exact runtime container. `SetMap(kind, ...)` accepts `Map`, `OrderedMap`,
`MultiMap`, or `OrderedMultiMap`; `SetSet(kind, ...)` accepts `Set`,
`MultiSet`, or `OrderedMultiSet`.

Ordinary maps replace a repeated key, while multi-maps preserve every
key/value association. Ordinary sets remove repeated elements, while
multi-sets preserve their multiplicity. A priority queue normalizes its input
according to the runtime priority-queue contract; its `top` and `pop`
operations expose the highest-priority element first.

`SequenceCount` and `GetSequenceElement` access sequence values. `MapCount` and
`TryGetMapValue` read maps. `SetCount` and `SetContains` query sets.
`EnumerateMap` and `EnumerateSet` visit read-only snapshots when the loaded
runtime advertises `VeroScriptCapabilities.ContainerEnumeration`.

```CSharp
using var key = new VeroScriptValue();
key.SetString("answer");
using var number = new VeroScriptValue();
number.Set(42);
using var map = new VeroScriptValue();
map.SetMap("string", "int", [key], [number]);

map.EnumerateMap((entryKey, entryValue) =>
{
	Console.WriteLine(entryKey.GetString());
	Console.WriteLine(entryValue.GetInt32());
});
```

The key and value parameters of an enumeration callback are
`VeroScriptBorrowedValue` instances. They expire when that callback invocation
returns; call `Clone()` inside the callback if a value must be retained.

Channels expose `ChannelSend`, `ChannelTrySend`, `ChannelSendFor`,
`ChannelReceive`, `ChannelTryReceive`, `ChannelReceiveFor`, and
`ChannelClose`. `ChannelTrySend` returns `bool`; `ChannelSendFor` accepts a
timeout in milliseconds. The non-blocking and timed receive methods return an
owned value or `null`. `ChannelCount`, `ChannelCapacity`, and
`IsChannelClosed` report channel state. A blocking receive follows the runtime
channel contract and should not be used while the creator thread must remain
responsive. Channel operations are synchronized by the runtime and may run on multiple threads while the wrapper remains a channel; do not concurrently reset, replace or dispose it.

## Host objects

`SetHostObject(typeId, typeName, managedObject)` roots a managed payload while
the native value retains it. The overload accepting
`Func<string, bool> isType` can report compatible host subtypes during
overload and type validation. `GetHostObject()` returns
`VeroScriptHostObjectInfo`; `GetHostObject<T>()` checks the managed type.

## Borrowed values and callables

`VeroScriptBorrowedValue` is valid only for the current callback scope. It is
used for native callback return values, callback arguments, mutation events,
container enumeration, and reference visitors. Never store it; clone it when
the value must outlive the callback.

`VeroScriptCallable` is an owned retained script function or closure. Dispose
it after use. Invocation is synchronized, but creator-thread-only invocation
is the default; opt into cross-thread invocation only when the callable and all
associated host state are safe. With `RetainCallable(..., ownerThreadOnly: false)`, the host must serialize invocation with every other operation on the associated session; the callable’s internal lock does not protect the whole session.

## Callback thread ownership

Use borrowed contexts and values only on the thread running their current callback. This includes native functions, interceptors, generic factories, serialization, mutations and container/reference visitors. Foreign-thread access throws `VeroScriptException` with `WrongThread`; access after callback return throws `InvalidOperationException`. Clone a permitted independent payload on the callback thread before transferring it to another thread; cloning does not detach module-dependent payloads.

## Exact numeric APIs

C# keeps Core bigint/rational results as opaque VeroScriptValue host values. VeroScriptCapabilities.Numerics reports the native feature; it does not add automatic System.Numerics.BigInteger conversion or a public managed numeric service. Use typed script functions or the public native C ABI for that integration.

<a id="managed-call-lifetime"></a>

## Managed-call lifetime

Native operations retain the managed value/package wrappers supplying their handles until the call and returned native-view copy finish. This includes values produced by an `IReadOnlyList<VeroScriptValue>` indexer and receivers of blocking channel operations or visitors. The retained wrappers are snapshots: replacing an argument array slot during a callback does not redirect native `ref`/`out` writeback to the replacement. These lifetime guarantees do not permit concurrent explicit disposal or extend borrowed callback scopes.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](values.md) · [fr-FR](../fr-FR/values.md) · [ja-JP](../ja-JP/values.md) · [ko-KR](../ko-KR/values.md) · [zh-CN](../zh-CN/values.md) · [zh-TW](../zh-TW/values.md)
