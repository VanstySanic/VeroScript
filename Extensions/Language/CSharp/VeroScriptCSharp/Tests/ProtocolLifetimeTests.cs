// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.CompilerServices;
using VeroScript;

internal static class ProtocolLifetimeTests
{
	internal static void Run()
	{
		foreach (bool generic in new[]
		{
			false,
			true
		}

		)
		{
			var(value, equalValue, callback) = Create(generic);
			try
			{
				Collect();
				Require(ReadCount(callback) >= 0, "protocol delegate remains rooted after session disposal");
				using (VeroScriptValue copy = value.Clone())
				{
					Require(copy.GetHostObject<Payload>().Number == 7 && ReadCount(callback) >= 0, "cloned native wrappers retain the stamped protocol after session disposal");
				}

				Task.Run(() => CheckMapCallbacks(value, equalValue, callback)).GetAwaiter().GetResult();
			}
			finally
			{
				// The final stamped owner can be released by a foreign thread.
				Task.Run(() =>
				{
					value.Dispose();
					equalValue.Dispose();
				}).GetAwaiter().GetResult();
			}

			Collect();
			Require(ReadCount(callback) == -1, "final native value releases the managed protocol root");
		}
	}

	[MethodImpl(MethodImplOptions.NoInlining)]
	private static (VeroScriptValue, VeroScriptValue, WeakReference<Counter>) Create(bool generic)
	{
		using var session = new VeroScriptSession();
		session.ApplyConfiguration(new() { ExecutionBackend = VeroScriptExecutionBackend.Vm });
		var counter = new Counter();
		uint typeId = 0;
		string name = generic ? "LifetimeGeneric<int>" : "LifetimeValue";
		var protocol = new VeroScriptHostTypeProtocolDefinition
		{
			CloneCallback = (context, result) =>
			{
				Interlocked.Increment(ref counter.Clones);
				result.SetHostObject(typeId, name, new Payload(context.GetArgument(0).GetHostObject<Payload>().Number));
			},
			EqualsCallback = (context, result) =>
			{
				Interlocked.Increment(ref counter.Equalities);
				result.Set(context.GetArgument(0).GetHostObject<Payload>().Number == context.GetArgument(1).GetHostObject<Payload>().Number);
			},
			HashCallback = (context, result) =>
			{
				Interlocked.Increment(ref counter.Hashes);
				result.Set((ulong)context.GetArgument(0).GetHostObject<Payload>().Number);
			}
		};
		if (generic)
		{
			uint template = session.RegisterGenericHostTypeTemplate("LifetimeGeneric", 1, "", context =>
			{
				context.SetType(new() { Name = "LifetimeGeneric", Kind = VeroScriptHostTypeKind.Struct });
				context.SetProtocol(protocol);
			});
			typeId = session.InstantiateGenericHostType(template, ["int"]);
		}
		else
		{
			typeId = session.RegisterHostType(new() { Name = name, Kind = VeroScriptHostTypeKind.Struct });
			session.RegisterHostProtocol(typeId, protocol);
		}

		session.RegisterHostMember(typeId, new() { Kind = VeroScriptHostMemberKind.Constructor, Name = generic ? "LifetimeGeneric" : name, ReturnType = new(VeroScriptNativeTypeKind.HostObject, name), Callback = (_, result) => result.SetHostObject(typeId, name, new Payload(7)) });
		Require(session.LoadSource(name + " make_wrapper(){return " + name + "();}" + name + " copy_wrapper(" + name + " source){return source;}void main(){}").Success, "load protocol lifetime source: " + session.GetLastError());
		var(value, call) = session.CallFunction("make_wrapper");
		if (!call.Success)
		{
			value.Dispose();
			throw new InvalidOperationException("Protocol lifetime value creation failed: " + session.GetLastError());
		}

		try
		{
			var(equalValue, secondCall) = session.CallFunction("copy_wrapper", value);
			try
			{
				Require(secondCall.Success, "second protocol lifetime value creation: " + session.GetLastError());
				Require(Volatile.Read(ref counter.Clones) > 0, "script value copying invokes the clone protocol");
				session.Dispose();
				return (value, equalValue, new(counter));
			}
			catch
			{
				equalValue.Dispose();
				throw;
			}
		}
		catch
		{
			value.Dispose();
			throw;
		}
	}

	private static void CheckMapCallbacks(VeroScriptValue value, VeroScriptValue equalValue, WeakReference<Counter> callback)
	{
		using var map = new VeroScriptValue();
		using var first = new VeroScriptValue();
		using var replacement = new VeroScriptValue();
		using var result = new VeroScriptValue();
		first.Set(1);
		replacement.Set(2);
		// Distinct host wrappers with equal payloads force both stamped callbacks;
		// a shallow Clone alone would take the native pointer-identity shortcut.
		map.SetMap(value.TypeName, "int", [value, equalValue], [first, replacement]);
		Require(map.MapCount == 1 && map.TryGetMapValue(equalValue, result) && result.GetInt32() == 2, "escaped host values remain usable as map keys on a foreign thread");
		Require(callback.TryGetTarget(out Counter? counter) && Volatile.Read(ref counter.Hashes) > 0 && Volatile.Read(ref counter.Equalities) > 0, "hash and equality callbacks execute after session disposal");
	}

	[MethodImpl(MethodImplOptions.NoInlining)]
	private static int ReadCount(WeakReference<Counter> callback) => callback.TryGetTarget(out Counter? counter) ? Volatile.Read(ref counter.Clones) : -1;
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
			throw new InvalidOperationException("Protocol lifetime: " + message);
		}
	}

	private sealed record Payload(int Number);
	private sealed class Counter
	{
		internal int Clones;
		internal int Hashes;
		internal int Equalities;
	}
}
