// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScript.Interop;

/// <summary>Protects callback-owned native contexts and value views before native access.</summary>
internal sealed class BorrowScope
{
	internal void Expire() => Volatile.Write(ref _alive, 0);

	internal void ThrowIfUnavailable()
	{
		if (Volatile.Read(ref _alive) == 0)
		{
			throw new InvalidOperationException("This borrowed VeroScript context or value is no longer valid outside its callback.");
		}
		// Native callback state is unsynchronized and can be destroyed on return.
		// Confinement also prevents an expiry race between this check and P/Invoke.
		if (Environment.CurrentManagedThreadId != _threadId)
		{
			throw new VeroScriptException(VeroScriptStatus.WrongThread,
				"Borrowed VeroScript contexts and values may only be used on their callback thread.");
		}
	}

	private readonly int _threadId = Environment.CurrentManagedThreadId;
	private int _alive = 1;
}
