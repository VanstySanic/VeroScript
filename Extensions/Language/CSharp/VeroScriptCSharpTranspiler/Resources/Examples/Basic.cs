// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

public sealed class Counter
{
	public int Value { get; set; }

	public Counter(int initial)
	{
		Value = initial;
	}

	public int Increment(int amount)
	{
		Value += amount;
		return Value;
	}
}

public static class Program
{
	public static int Main()
	{
		var counter = new Counter(2);
		return counter.Increment(3) == 5 ? 0 : 1;
	}
}
