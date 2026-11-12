// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace Demo;
public enum Mode : byte
{
	Off,
	On = 3
}

#pragma warning disable CA1012 // Exercise an abstract host with a public constructor.

public abstract class AbstractCounter
{
	public AbstractCounter()
	{
	}

	public abstract int Read();
	public static AbstractCounter Create() => new Implementation();
	private sealed class Implementation : AbstractCounter
	{
		public override int Read() => 42;
	}
}

#pragma warning restore CA1012
/// <summary>A counter exposed to script.</summary>
public sealed class Counter
{
	public int Value { get; set; }
	public int ReadOnlyOutside { get; private set; }
	public int InitialOnly { get; init; }

	public Counter(int initial)
	{
		Value = initial;
	}

	public int Add(in int amount) => Value + amount;
	public void Bump(ref int value) => value += Value;
	public void Assign(out int value) => value = Value;
	public static string Label(int value) => value.ToString(System.Globalization.CultureInfo.InvariantCulture);
	public System.Collections.Generic.List<int> EchoValues(System.Collections.Generic.List<int> values) => Value >= 0 ? values : [];
	public int[] EchoArray(int[] values) => Value >= 0 ? values : [];
	public System.Collections.Generic.List<string> EchoText(System.Collections.Generic.List<string> values) => Value >= 0 ? values : [];
	public System.Collections.Generic.Dictionary<string, int> EchoMap(System.Collections.Generic.Dictionary<string, int> values) => Value >= 0 ? values : [];
	public System.Collections.Generic.HashSet<int> EchoSet(System.Collections.Generic.HashSet<int> values) => Value >= 0 ? values : [];
	public System.Collections.Generic.Dictionary<sbyte, int> EchoSignedByteMap(System.Collections.Generic.Dictionary<sbyte, int> values) => Value >= 0 ? values : [];
	public System.Collections.Generic.HashSet<sbyte> EchoSignedByteSet(System.Collections.Generic.HashSet<sbyte> values) => Value >= 0 ? values : [];
	public System.Collections.Generic.Queue<int> EchoQueue(System.Collections.Generic.Queue<int> values) => Value >= 0 ? values : [];
	public System.Collections.Generic.Stack<int> EchoStack(System.Collections.Generic.Stack<int> values) => Value >= 0 ? values : [];
	public const int Maximum = 10;
}
