// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using VeroScript;

internal static class NativeLifetimeTests
{
	internal static void Run()
	{
		CheckPartiallyConstructedCallable();
		CheckCallbackRootOwnership();
		CheckRegistrationDisposal();
		CheckSharedModuleContainers();
		CheckModuleContainerBoundaries();
		CheckWorkerValueExports();
		CheckSubtypeContainers();
		CheckBlockingReceive();
		CheckReplacedElements();
		CheckCallbackArguments(retained: false);
		CheckCallbackArguments(retained: true);
		CheckGeneratedElements();
		CheckDisposedElements();
		CheckContainerCounts();
		CheckMapVisitor();
	}

	private static void CheckPartiallyConstructedCallable()
	{
		// Reproduce the state left when lock initialization fails during
		// construction without provoking process-wide memory exhaustion.
		var partial = (VeroScriptCallable)RuntimeHelpers.GetUninitializedObject(typeof(VeroScriptCallable));
		try
		{
			partial.Dispose();
		}
		finally
		{
			GC.SuppressFinalize(partial);
		}
	}

	private static void CheckCallbackRootOwnership()
	{
		using var session = new VeroScriptSession();
		session.Dispose();
		WeakReference<object> target = CreateCallbackRoot(out GCHandle handle);
		try
		{
			// An old callback slot can have been reused by another owner after
			// reentrant session disposal. The session must not free that root.
			MethodInfo release = typeof(VeroScriptSession).GetMethod("ReleaseTrackedCallback", BindingFlags.Instance | BindingFlags.NonPublic) ?? throw new InvalidOperationException("Callback ownership cleanup is unavailable.");
			release.Invoke(session, [handle]);
			Collect();
			Require(target.TryGetTarget(out _), "an untracked callback root was released by the session");
		}
		finally
		{
			if (target.TryGetTarget(out _))
			{
				handle.Free();
			}
		}
	}

	[MethodImpl(MethodImplOptions.NoInlining)]
	private static WeakReference<object> CreateCallbackRoot(out GCHandle handle)
	{
		object target = new();
		handle = GCHandle.Alloc(target);
		return new(target);
	}

	private static void CheckRegistrationDisposal()
	{
		foreach (bool member in new[]
		{
			false,
			true
		}

		)
		{
			using var session = new VeroScriptSession();
			var parameters = new DisposingMetadata<VeroScriptNativeParameter>(session.Dispose);
			if (member)
			{
				uint typeId = session.RegisterHostType(new() { Name = "DisposedRegistration" });
				RequireDisposed(() => session.RegisterHostMember(typeId, new() { Kind = VeroScriptHostMemberKind.StaticMethod, Name = "dispose_member", Callback = static (_, _) =>
				{
				}, Parameters = parameters }), "member registration must reject disposal while metadata is copied");
			}
			else
			{
				RequireDisposed(() => session.RegisterHostFunction(new() { Name = "dispose_function", ReturnType = VeroScriptNativeType.Void, Callback = static (_, _) =>
				{
				}, Parameters = parameters }), "function registration must reject disposal while metadata is copied");
			}
		}
	}

	private static void CheckSharedModuleContainers()
	{
		foreach (bool scriptCreated in new[]
		{
			false,
			true
		}

		)
		{
			foreach (string function in new[]
			{
				"fill",
				"fill_fail",
				"fill_exit"
			}

			)
			{
				using var session = new VeroScriptSession();
				session.ApplyConfiguration(new() { ExecutionBackend = VeroScriptExecutionBackend.Vm });
				int destroyed = 0;
				session.RegisterFunction("notify_cleanup", VeroScriptNativeType.Void, [], (_, _) => Interlocked.Increment(ref destroyed));
				Require(session.LoadSource("""
                export class Item { public: int value=7; ~Item(){notify_cleanup();} };
                array<Item> empty(){return array<Item>();}
                int fill(array<Item> items,ref int marker){items.push(Item());marker=99;return 42;}
                int fill_fail(array<Item> items,ref int marker){items.push(Item());marker=99;assert(false);return 42;}
                int fill_exit(array<Item> items,ref int marker){items.push(Item());marker=99;exit(23);return 42;}
                void main(){}
                """).Success, "load shared module container fixture: " + session.GetLastError());
				using var items = new VeroScriptValue();
				if (scriptCreated)
				{
					Require(session.CallFunction("empty", [], items).Success, "export empty module container");
				}
				else
				{
					items.SetArray("Item", []);
				}

				using var alias = items.Clone();
				using var marker = new VeroScriptValue();
				using var returned = new VeroScriptValue();
				marker.Set(5);
				returned.Set(77);
				VeroScriptRunResult call = session.CallFunction(function, [items, marker], returned);
				Require(call.Success == (function != "fill_fail") && call.ExitRequested == (function == "fill_exit") && (function != "fill_exit" || call.ExitCode == 23) && marker.GetInt32() == (function == "fill" ? 99 : 5) && returned.GetInt32() == (function == "fill" ? 42 : 77), "shared mutation preserves failed/exit ref and return publication: " + function);
				Require(alias.SequenceCount == 1, "an earlier clone observes promoted shared storage");
				using (var item = alias.GetSequenceElement(0))
				{
					var(field, read) = session.GetField(item, "value");
					using (field)
					{
						Require(read.Success && field.GetInt32() == 7, "promoted container retains its script item");
					}
				}

				items.Reset();
				Require(destroyed == 0 && alias.SequenceCount == 1, "reset preserves the remaining shared alias");
				alias.Reset();
				Require(destroyed == 1, "last module container alias releases its script item exactly once");
			}
		}
	}

	private static void CheckModuleContainerBoundaries()
	{
		const string source = "export class Item{}; array<Item> empty(){return array<Item>();}" + "int inspect(array<Item> items){return 0;}void main(){}";
		using var session = new VeroScriptSession();
		using var other = new VeroScriptSession();
		Require(session.LoadSource(source).Success && other.LoadSource(source).Success, "load identical module layouts in separate sessions: " + session.GetLastError() + other.GetLastError());
		var(items, made) = session.CallFunction("empty");
		using (items)
		{
			using (var alias = items.Clone())
			{
				Require(made.Success && alias.SequenceCount == 0, "empty script container is exported");
				Task.Run(() => RequireStatus(() => _ = alias.SequenceCount, VeroScriptStatus.WrongThread, "empty module containers keep creator-thread ownership")).GetAwaiter().GetResult();
				var(foreign, rejected) = other.CallFunction("inspect", alias);
				using (foreign)
				{
					Require(!rejected.Success && other.GetDiagnostics().Count > 0 && alias.SequenceCount == 0, "matching foreign layouts do not transfer module container ownership");
				}

				session.Dispose();
				RequireStatus(() => _ = alias.SequenceCount, VeroScriptStatus.EngineError, "session closure invalidates even empty module aliases");
				RequireStatus(() =>
				{
					using var copy = alias.Clone();
				}, VeroScriptStatus.EngineError, "cloning cannot detach an invalid module value");
			}
		}
	}

	private static void CheckWorkerValueExports()
	{
		string standardLibrary = Environment.GetEnvironmentVariable("VEROSCRIPT_STDLIB_DIR") ?? throw new InvalidOperationException("VEROSCRIPT_STDLIB_DIR is required.");
		int owner = Environment.CurrentManagedThreadId;
		int calls = 0;
		using var external = new VeroScriptValue();
		external.SetArray("variant", []);
		using var session = new VeroScriptSession();
		session.ApplyConfiguration(new() { ExecutionBackend = VeroScriptExecutionBackend.Vm, StandardLibrarySearchPaths = [standardLibrary] });
		var array = new VeroScriptNativeType(VeroScriptNativeTypeKind.Array, null, VeroScriptNativeType.Variant);
		session.RegisterFunction("capture_worker", array, [new(array), new(VeroScriptNativeType.Int32)], (context, result) =>
		{
			Require(Environment.CurrentManagedThreadId != owner, "exercise the actual VM worker callback");
			using var scalar = context.GetArgument(1).Clone();
			Require(scalar.GetInt32() == 42, "independent worker values remain exportable");
			RequireStatus(() =>
			{
				using var copy = context.GetArgument(0).Clone();
			}, VeroScriptStatus.EngineError, "worker cannot export empty variant storage");
			result.Set(context.GetArgument(0));
			RequireStatus(() => result.Set(external), VeroScriptStatus.EngineError, "worker cannot acquire external storage with module-value slots");
			Interlocked.Increment(ref calls);
		});
		Require(session.LoadSource("""
            import "concurrent.vs";
            class Item{};
            export void worker(channel<int> output){
                array<variant> items=array<variant>();
                array<variant> forwarded=capture_worker(items,42);
                forwarded.push(Item());
                output.send(42);
            }
            int main(){
                task<int> work=std::concurrent::spawn_task<int>("worker");
                return work.await();
            }
            """).Success, "load worker value ownership fixture: " + session.GetLastError());
		VeroScriptRunResult run = session.Run();
		Require(run.Success && run.ExitCode == 42 && Volatile.Read(ref calls) == 1 && external.SequenceCount == 0, "worker forwarding and rejection preserve storage: " + session.GetLastError());
	}

	private static void CheckSubtypeContainers()
	{
		using var session = new VeroScriptSession();
		Require(session.LoadSource("""
            export interface IRead { virtual int read()=0; };
            export class Base:public IRead { public:virtual int read(){return 7;} };
            export class Child:public Base {}; export class Other {};
            Child make_child(){return Child();} Other make_other(){return Other();}
            int read_base(array<Base> values){return values[0].read();}
            int read_interface(array<IRead> values){return values[0].read();}
            """).Success, "load subtype container fixture: " + session.GetLastError());
		var(child, created) = session.CallFunction("make_child");
		var(other, otherCreated) = session.CallFunction("make_other");
		using (child)
		{
			using (other)
			{
				Require(created.Success && otherCreated.Success, "create script subtype values");
				foreach (string type in new[]
				{
					"Base",
					"IRead"
				}

				)
				{
					using var values = new VeroScriptValue();
					values.SetArray(type, [child]);
					RequireStatus(() => values.SetArray(type, [other]), VeroScriptStatus.TypeMismatch, "unrelated script objects are still rejected");
					var(read, call) = session.CallFunction(type == "Base" ? "read_base" : "read_interface", values);
					using (read)
					{
						Require(call.Success && read.GetInt32() == 7 && values.SequenceCount == 1, "subtype assignment and failed replacement preserve the original container");
					}

					using var queue = new VeroScriptValue();
					queue.SetSequence(VeroScriptValueKind.Queue, type, [child]);
					Require(queue.SequenceCount == 1, "queue accepts script subtypes");
					using var channel = new VeroScriptValue();
					RequireStatus(() => channel.SetChannel(type, 1), VeroScriptStatus.InvalidArgument, "channels continue to reject non-transferable script object element types");
				}
			}
		}
	}

	private static void RequireStatus(Action action, VeroScriptStatus expected, string message)
	{
		try
		{
			action();
		}
		catch (VeroScriptException exception)when (exception.Status == expected)
		{
			return;
		}

		throw new InvalidOperationException("Native lifetime: " + message);
	}

	[MethodImpl(MethodImplOptions.NoInlining)]
	private static void Receive(ReceiveState state)
	{
		try
		{
			var receiver = new TrackedValue(state);
			receiver.SetChannel("int", 1);
			state.Alias = receiver.Clone();
			state.Entered.Set();
			using VeroScriptValue? result = receiver.ChannelReceiveFor(5_000);
			Require(result is not null && result.GetInt32() == 42, "native receiver did not receive its value");
		}
		catch (Exception error)
		{
			state.Error = error;
			state.Entered.Set();
		}
		finally
		{
			state.Completed.Set();
		}
	}

	private static void CheckBlockingReceive()
	{
		var state = new ReceiveState();
		var worker = new Thread(() => Receive(state));
		worker.IsBackground = true;
		worker.Start();
		try
		{
			Require(state.Entered.Wait(5_000), "native receiver did not start");
			Thread.Sleep(100);
			if (state.Error is not null)
			{
				throw new InvalidOperationException("Native receive setup failed.", state.Error);
			}

			Require(!state.Completed.IsSet, "native receiver did not remain blocked");
			Collect();
			bool finalized = Volatile.Read(ref state.Finalized) != 0;
			using var sent = new VeroScriptValue();
			sent.Set(42);
			Require(state.Alias is not null && state.Alias.ChannelTrySend(sent), "could not release native receiver");
			Require(worker.Join(5_000), "native receiver did not complete");
			if (state.Error is not null)
			{
				throw new InvalidOperationException("Native receive lifetime regression failed.", state.Error);
			}

			Require(!finalized, "value owner was finalized during a blocking native call");
		}
		finally
		{
			state.Alias?.ChannelClose();
			worker.Join(6_000);
			state.Alias?.Dispose();
		}
	}

	private static void CheckGeneratedElements()
	{
		var state = new LifetimeState();
		using var array = new VeroScriptValue();
		array.SetArray("int", new GeneratedValues(state));
		using var first = array.GetSequenceElement(0);
		using var second = array.GetSequenceElement(1);
		Require(first.GetInt32() == 1 && second.GetInt32() == 2, "generated value list was corrupted");
	}

	private static void CheckContainerCounts()
	{
		using var first = new VeroScriptValue();
		using var second = new VeroScriptValue();
		first.Set(1);
		second.Set(2);
		VeroScriptValue[] values = [first, second];
		foreach (VeroScriptValueKind kind in new[]
		{
			VeroScriptValueKind.Map,
			VeroScriptValueKind.OrderedMap,
			VeroScriptValueKind.MultiMap,
			VeroScriptValueKind.OrderedMultiMap
		}

		)
		{
			using var map = new VeroScriptValue();
			map.SetMap(kind, "int", "int", new CountSnapshotValues(values), new CountSnapshotValues(values));
			Require(map.MapCount == 2, "map kinds use matching captured input lengths: " + kind);
		}

		foreach (VeroScriptValueKind kind in new[]
		{
			VeroScriptValueKind.Set,
			VeroScriptValueKind.MultiSet,
			VeroScriptValueKind.OrderedMultiSet
		}

		)
		{
			using var set = new VeroScriptValue();
			set.SetSet(kind, "int", new CountSnapshotValues(values));
			Require(set.SetCount == 2 && set.SetContains(first) && set.SetContains(second), "set kinds use a captured input length: " + kind);
		}
	}

	[MethodImpl(MethodImplOptions.NoInlining)]
	private static VeroScriptValue CreateTrackedInteger(LifetimeState state, int value)
	{
		var result = new TrackedValue(state);
		result.Set(value);
		return result;
	}

	private static void CheckReplacedElements()
	{
		var state = new LifetimeState();
		VeroScriptValue[] keys = [CreateTrackedInteger(state, 1)];
		using var replacement = new VeroScriptValue();
		replacement.Set(2);
		using var map = new VeroScriptValue();
		map.SetMap("int", "int", keys, new ReplacingValues(keys, replacement, state));
		int visits = 0;
		map.EnumerateMap((key, value) =>
		{
			Require(key.GetInt32() == 1 && value.GetInt32() == 2, "replacing a source array element changed its marshalled value");
			++visits;
		});
		Require(visits == 1, "map did not retain its original marshalled key");
	}

	private static void CheckDisposedElements()
	{
		using (var first = new VeroScriptValue())
		{
			using (var second = new VeroScriptValue())
			{
				using (var array = new VeroScriptValue())
				{
					first.Set(1);
					second.Set(2);
					var elements = new DisposingValues([first, second], index =>
					{
						if (index == 1)
						{
							first.Dispose();
						}
					});
					RequireDisposed(() => array.SetArray("int", elements), "later list indexers must not pass a disposed earlier element to native code");
				}
			}
		}

		using (var session = new VeroScriptSession())
		{
			using (var first = new VeroScriptValue())
			{
				using (var second = new VeroScriptValue())
				{
					session.ApplyConfiguration(new() { ExecutionBackend = VeroScriptExecutionBackend.Vm });
					Require(session.LoadSource("int marshal_sum(int a,int b){return a+b;}").Success, "load argument marshalling lifetime source");
					first.Set(1);
					second.Set(2);
					var arguments = new DisposingValues([first, second], index =>
					{
						if (index == 1)
						{
							first.Dispose();
						}
					});
					RequireDisposed(() => _ = session.CallFunction("marshal_sum", arguments, null), "later argument indexers must not pass a disposed earlier argument to native code");
				}
			}
		}

		foreach (bool explicitKind in new[]
		{
			false,
			true
		}

		)
		{
			using var key = new VeroScriptValue();
			using var value = new VeroScriptValue();
			using var map = new VeroScriptValue();
			key.Set(1);
			value.Set(2);
			var values = new DisposingValues([value], _ => key.Dispose());
			RequireDisposed(() =>
			{
				if (explicitKind)
				{
					map.SetMap(VeroScriptValueKind.MultiMap, "int", "int", [key], values);
				}
				else
				{
					map.SetMap("int", "int", [key], values);
				}
			}, "map value indexers must not pass disposed keys to native code");
		}
	}

	private static void RequireDisposed(Action operation, string message)
	{
		try
		{
			operation();
		}
		catch (ObjectDisposedException)
		{
			return;
		}

		throw new InvalidOperationException(message);
	}

	private static void CheckCallbackArguments(bool retained)
	{
		var state = new LifetimeState();
		VeroScriptValue[] arguments = [CreateTrackedInteger(state, 1)];
		var original = new WeakReference<VeroScriptValue>(arguments[0]);
		using var replacement = new VeroScriptValue();
		replacement.Set(2);
		using var session = new VeroScriptSession();
		session.RegisterFunction("host_replace", VeroScriptNativeType.Void, [], (_, _) =>
		{
			arguments[0] = replacement;
			Collect();
			Require(Volatile.Read(ref state.Finalized) == 0, "a replaced argument was finalized before native ref writeback");
		});
		Require(session.LoadSource("""
            int lifetime_bump(ref int value) { host_replace(); value += 41; return value; }
            fn(ref int)->int lifetime_callable() { return lifetime_bump; }
            """).Success, "callback lifetime source did not load");
		try
		{
			if (retained)
			{
				var(function, lookup) = session.CallFunction("lifetime_callable");
				using (function)
				{
					Require(lookup.Success, "callback lifetime callable lookup failed");
					using var callable = session.RetainCallable(function);
					var(value, result) = callable.Invoke(arguments);
					using (value)
					{
						Require(result.Success && value.GetInt32() == 42, "callback lifetime callable invocation failed");
					}
				}
			}
			else
			{
				var(value, result) = session.CallFunction("lifetime_bump", arguments);
				using (value)
				{
					Require(result.Success && value.GetInt32() == 42, "callback lifetime function invocation failed");
				}
			}

			Require(original.TryGetTarget(out var written) && written.GetInt32() == 42, "native ref writeback did not update the originally marshalled argument");
			Require(replacement.GetInt32() == 2, "native ref writeback updated the replacement array element");
		}
		finally
		{
			if (original.TryGetTarget(out var value))
			{
				value.Dispose();
			}
		}
	}

	[MethodImpl(MethodImplOptions.NoInlining)]
	private static void CheckMapVisitor()
	{
		var state = new LifetimeState();
		var map = new TrackedValue(state);
		using var key = new VeroScriptValue();
		using var value = new VeroScriptValue();
		key.Set(1);
		value.Set(2);
		map.SetMap("int", "int", [key], [value]);
		map.EnumerateMap((entryKey, entryValue) =>
		{
			Collect();
			Require(Volatile.Read(ref state.Finalized) == 0, "map owner was finalized during its native visitor");
			Require(entryKey.GetInt32() == 1 && entryValue.GetInt32() == 2, "native visitor views were corrupted");
			using var owned = entryKey.Clone();
			Task.Run(() =>
			{
				try
				{
					using var invalidClone = entryKey.Clone();
					throw new InvalidOperationException("A borrowed visitor value escaped to another thread.");
				}
				catch (VeroScriptException exception)when (exception.Status == VeroScriptStatus.WrongThread)
				{
				}

				Require(owned.GetInt32() == 1, "an owned clone could not be transferred to another thread");
			}).GetAwaiter().GetResult();
		});
	}

	private static void Collect()
	{
		GC.Collect();
		GC.WaitForPendingFinalizers();
		GC.Collect();
	}

	private static void Require(bool condition, string message)
	{
		if (!condition)
		{
			throw new InvalidOperationException("Native lifetime: " + message);
		}
	}

	private class LifetimeState
	{
		internal int Finalized;
	}

	private sealed class ReceiveState : LifetimeState
	{
		internal readonly ManualResetEventSlim Entered = new();
		internal readonly ManualResetEventSlim Completed = new();
		internal VeroScriptValue? Alias;
		internal Exception? Error;
	}

	private sealed class TrackedValue(LifetimeState state) : VeroScriptValue
	{
		~TrackedValue() => Interlocked.Increment(ref state.Finalized);
	}

	private sealed class GeneratedValues(LifetimeState state) : IReadOnlyList<VeroScriptValue>
	{
		public int Count => 2;

		public VeroScriptValue this[int index]
		{
			get
			{
				if (index == 1)
				{
					Collect();
					Require(Volatile.Read(ref state.Finalized) == 0, "an earlier generated element was finalized while its handles were being marshalled");
				}

				VeroScriptValue value = index == 0 ? new TrackedValue(state) : new VeroScriptValue();
				value.Set(index + 1);
				return value;
			}
		}

		public IEnumerator<VeroScriptValue> GetEnumerator()
		{
			for (int index = 0; index < Count; ++index)
			{
				yield return this[index];
			}
		}

		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
	}

	private sealed class CountSnapshotValues(VeroScriptValue[] values) : IReadOnlyList<VeroScriptValue>
	{
		private int _countReads;
		public int Count => ++_countReads == 1 ? values.Length : throw new InvalidOperationException("Native input Count was read again after its size was captured.");

		public VeroScriptValue this[int index] => values[index];
		public IEnumerator<VeroScriptValue> GetEnumerator() => ((IEnumerable<VeroScriptValue>)values).GetEnumerator();
		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
	}

	private sealed class ReplacingValues(VeroScriptValue[] keys, VeroScriptValue replacement, LifetimeState state) : IReadOnlyList<VeroScriptValue>
	{
		public int Count => 1;

		public VeroScriptValue this[int index]
		{
			get
			{
				Require(index == 0, "unexpected replacement list index");
				keys[0] = replacement;
				Collect();
				Require(Volatile.Read(ref state.Finalized) == 0, "replaced array element was finalized while its handle was retained for native use");
				return replacement;
			}
		}

		public IEnumerator<VeroScriptValue> GetEnumerator()
		{
			yield return this[0];
		}

		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
	}

	private sealed class DisposingValues(VeroScriptValue[] values, Action<int> beforeRead) : IReadOnlyList<VeroScriptValue>
	{
		public int Count => values.Length;

		public VeroScriptValue this[int index]
		{
			get
			{
				beforeRead(index);
				return values[index];
			}
		}

		public IEnumerator<VeroScriptValue> GetEnumerator()
		{
			for (int index = 0; index < Count; ++index)
			{
				yield return this[index];
			}
		}

		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
	}

	private sealed class DisposingMetadata<T>(Action dispose) : IReadOnlyList<T>
	{
		public int Count
		{
			get
			{
				dispose();
				return 0;
			}
		}

		public T this[int index] => throw new ArgumentOutOfRangeException(nameof(index));
		public IEnumerator<T> GetEnumerator()
		{
			dispose();
			return Enumerable.Empty<T>().GetEnumerator();
		}

		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
	}
}
