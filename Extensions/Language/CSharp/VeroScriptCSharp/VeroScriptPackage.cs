// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript.Interop;

namespace VeroScript;

/// <summary>Specifies inputs used to build a VeroScript package.</summary>
public sealed class VeroScriptPackageBuildOptions
{
	public VeroScriptPackageKind Kind { get; init; } = VeroScriptPackageKind.Application;
	public string PackageName { get; init; } = string.Empty;
	public ulong PackageVersion { get; init; }
	public IReadOnlyDictionary<string, byte[]> HostMetadata { get; init; }
		= new Dictionary<string, byte[]>();
	public bool StripDebugInfo { get; init; }
}

/// <summary>Owns a parsed VeroScript package and its compiled module.</summary>
/// <remarks>A successful load transfers the compiled module to the destination session.</remarks>
public sealed unsafe class VeroScriptPackage : IDisposable
{
	private nint _handle;
	private int _moduleAvailable = 1;

	private VeroScriptPackage(nint handle) => _handle = handle;
	~VeroScriptPackage() => Release();

	internal nint Handle => _handle != 0
		? _handle : throw new ObjectDisposedException(nameof(VeroScriptPackage));

	public VeroScriptPackageKind Kind
	{
		get
		{
			using var nativeLifetime = new NativeLifetime(this);
			return NativeMethods.vs_interop_package_get_kind(Handle);
		}
	}
	public string Name
	{
		get
		{
			using var nativeLifetime = new NativeLifetime(this);
			return Utf8.Copy(NativeMethods.vs_interop_package_get_name(Handle));
		}
	}
	public ulong Version
	{
		get
		{
			using var nativeLifetime = new NativeLifetime(this);
			return NativeMethods.vs_interop_package_get_version(Handle);
		}
	}
	public bool HasCompiledModule => _handle != 0 && Volatile.Read(ref _moduleAvailable) != 0;

	public static VeroScriptPackage Read(ReadOnlySpan<byte> bytes)
	{
		VeroScriptRuntime.EnsureCompatible();
		nint package = 0;
		nint error = 0;
		VeroScriptStatus status;
		fixed (byte* data = bytes)
		{
			status = NativeMethods.vs_interop_package_read(
						new NativeByteView(data, (ulong)bytes.Length), &package, &error);
		}

		try
		{
			string detail = NativeBuffer.CopyOptionalErrorAndDestroy(error);
			if (status != VeroScriptStatus.Ok)
			{
				throw new VeroScriptException(status, string.IsNullOrWhiteSpace(detail)
								? $"VeroScript failed to read package: {status}." : detail);
			}

			if (package == 0)
			{
				throw new VeroScriptException(VeroScriptStatus.InternalError,
								"VeroScript parsed a null package handle.");
			}

			var result = new VeroScriptPackage(package);
			package = 0;
			return result;
		}
		finally
		{
			if (package != 0)
			{
				NativeMethods.vs_interop_package_destroy(package);
			}
		}
	}

	public IReadOnlyList<VeroScriptPackageMetadata> GetMetadata()
	{
		using var nativeLifetime = new NativeLifetime(this);
		ulong count = NativeMethods.vs_interop_package_metadata_count(Handle);
		if (count > int.MaxValue)
		{
			throw new OverflowException("Too many package metadata entries.");
		}

		var metadata = new List<VeroScriptPackageMetadata>((int)count);
		for (ulong i = 0; i < count; ++i)
		{
			NativePackageMetadata entry;
			VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_package_get_metadata(
				Handle, i, &entry), "get package metadata");
			if (entry.Value.Size > int.MaxValue)
			{
				throw new OverflowException("A package metadata value is too large.");
			}

			if (entry.Value.Size != 0 && entry.Value.Data is null)
			{
				throw new VeroScriptException(VeroScriptStatus.InternalError,
								"Package metadata returned a null data pointer.");
			}

			byte[] value = entry.Value.Size == 0 ? []
							: new ReadOnlySpan<byte>(entry.Value.Data, (int)entry.Value.Size).ToArray();
			metadata.Add(new(Utf8.Copy(entry.Key), value));
		}
		return metadata;
	}

	public byte[] GetCanonicalSignedBytes()
	{
		using var nativeLifetime = new NativeLifetime(this);
		NativeByteView view = NativeMethods.vs_interop_package_get_canonical_signed_bytes(Handle);
		if (view.Size > int.MaxValue)
		{
			throw new OverflowException("The canonical package data is too large.");
		}

		if (view.Size != 0 && view.Data is null)
		{
			throw new VeroScriptException(VeroScriptStatus.InternalError,
						"The canonical package data returned a null pointer.");
		}

		return view.Size == 0 ? [] : new ReadOnlySpan<byte>(view.Data, (int)view.Size).ToArray();
	}

	public void Dispose()
	{
		Release();
		GC.SuppressFinalize(this);
	}

	private void Release()
	{
		nint handle = Interlocked.Exchange(ref _handle, 0);
		if (handle != 0)
		{
			NativeMethods.vs_interop_package_destroy(handle);
		}
	}

	internal void EnsureModuleAvailable()
	{
		_ = Handle;
		if (Volatile.Read(ref _moduleAvailable) == 0)
		{
			throw new InvalidOperationException(
						"This package's compiled module has already been transferred to a session.");
		}
	}

	internal void MarkModuleConsumed() => Interlocked.Exchange(ref _moduleAvailable, 0);
}
