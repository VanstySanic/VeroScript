// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace VeroScript.Interop;

internal sealed unsafe class NativePackageBuildLease : IDisposable
{
	private readonly List<Utf8> _strings = [];
	private readonly List<nint> _allocations = [];
	internal NativePackageBuildOptions Value;

	internal NativePackageBuildLease(VeroScriptPackageBuildOptions options)
	{
		ArgumentNullException.ThrowIfNull(options);
		try
		{
			var name = MakeString(options.PackageName);
			Value = new NativePackageBuildOptions
			{
				StructSize = (uint)sizeof(NativePackageBuildOptions),
				Kind = options.Kind,
				PackageName = name,
				PackageVersion = options.PackageVersion,
				StripDebugInfo = options.StripDebugInfo ? (byte)1 : (byte)0
			};

			IReadOnlyDictionary<string, byte[]> metadata = options.HostMetadata
				?? throw new ArgumentException("Host metadata cannot be null.", nameof(options));
			KeyValuePair<string, byte[]>[] entries = metadata.ToArray();
			if (entries.Length == 0) return;
			nint entriesMemory = Allocate(checked(sizeof(NativePackageMetadata) * entries.Length));
			Value.HostMetadata = (NativePackageMetadata*)entriesMemory;
			Value.HostMetadataCount = (ulong)entries.Length;
			int index = 0;
			foreach ((string key, byte[] bytes) in entries)
			{
				if (bytes is null)
				{
					throw new ArgumentException("Host metadata values cannot be null.", nameof(options));
				}

				NativeStringView nativeKey = MakeString(key);
				byte* data = null;
				if (bytes.Length != 0)
				{
					nint bytesMemory = Allocate(bytes.Length);
					Marshal.Copy(bytes, 0, bytesMemory, bytes.Length);
					data = (byte*)bytesMemory;
				}
				Value.HostMetadata[index++] = new NativePackageMetadata
				{
					Key = nativeKey,
					Value = new NativeByteView(data, (ulong)bytes.Length)
				};
			}
		}
		catch
		{
			Dispose();
			throw;
		}
	}

	private NativeStringView MakeString(string value)
	{
		var text = new Utf8(value);
		try
		{
			_strings.Add(text);
		}
		catch
		{
			text.Dispose();
			throw;
		}
		return text.View;
	}

	private nint Allocate(int size)
	{
		nint memory = Marshal.AllocHGlobal(size);
		try
		{
			new Span<byte>((void*)memory, size).Clear();
			_allocations.Add(memory);
		}
		catch
		{
			Marshal.FreeHGlobal(memory);
			throw;
		}
		return memory;
	}

	public void Dispose()
	{
		foreach (Utf8 text in _strings)
		{
			text.Dispose();
		}

		foreach (nint memory in _allocations)
		{
			Marshal.FreeHGlobal(memory);
		}
	}
}
