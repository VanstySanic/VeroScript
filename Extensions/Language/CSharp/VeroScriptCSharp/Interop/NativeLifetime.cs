// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScript.Interop;

/// <summary>Keeps managed owners alive through calls that use their raw native handles or views.</summary>
internal readonly ref struct NativeLifetime
{
	internal NativeLifetime(object? owner, object? argument = null, object? secondArgument = null)
		=> (_owner, _argument, _secondArgument) = (owner, argument, secondArgument);

	public void Dispose()
	{
		GC.KeepAlive(_owner);
		GC.KeepAlive(_argument);
		GC.KeepAlive(_secondArgument);
	}

	private readonly object? _owner;
	private readonly object? _argument;
	private readonly object? _secondArgument;
}
