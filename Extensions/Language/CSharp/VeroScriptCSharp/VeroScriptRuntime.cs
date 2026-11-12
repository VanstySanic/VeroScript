// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScript.Interop;

namespace VeroScript;

/// <summary>Provides process-wide ABI, platform, capability, and localization operations.</summary>
public static unsafe class VeroScriptRuntime
{
	public const uint SupportedAbiVersion = NativeMethods.AbiVersion;

	public static uint AbiVersion
	{
		get
		{
			EnsureCompatible();
			return NativeMethods.vs_interop_get_abi_version();
		}
	}
	public static string RuntimeVersion
	{
		get
		{
			EnsureCompatible();
			return Utf8.Copy(NativeMethods.vs_interop_get_runtime_version());
		}
	}
	public static string HostPlatform
	{
		get
		{
			EnsureCompatible();
			return Utf8.Copy(NativeMethods.vs_interop_get_host_platform());
		}
	}
	public static VeroScriptCapabilities Capabilities
	{
		get
		{
			EnsureCompatible();
			return (VeroScriptCapabilities)NativeMethods.vs_interop_get_capabilities();
		}
	}

	/// <summary>Returns optional compile-time services, or None for older same-ABI runtimes.</summary>
	public static VeroScriptCompileCapabilities CompileCapabilities
	{
		get
		{
			EnsureCompatible();
			return _compileCapabilities.Value;
		}
	}

	public static string CurrentLanguage
	{
		get
		{
			EnsureCompatible();
			return Utf8.Copy(NativeMethods.vs_interop_locale_get_language());
		}
		set
		{
			EnsureCompatible();
			using var language = new Utf8(value);
			VeroScriptValue.ThrowIfFailed(NativeMethods.vs_interop_locale_set_language(language.View), "set language");
		}
	}

	public static string GetAvailableLanguagesJson()
	{
		EnsureCompatible();
		nint buffer = 0;
		try
		{
			VeroScriptValue.ThrowIfFailed(
				NativeMethods.vs_interop_locale_get_available_languages(&buffer),
				"get available languages");
			return NativeBuffer.CopyUtf8AndDestroy(ref buffer);
		}
		finally
		{
			NativeBuffer.Destroy(ref buffer);
		}
	}

	/// <summary>Translates an English diagnostic using the current process-wide language and catalogs.</summary>
	/// <remarks>Unknown messages are returned unchanged. Embedded NUL characters are preserved.</remarks>
	public static string Translate(string englishMessage)
	{
		EnsureCompatible();
		using var message = new Utf8(englishMessage, allowEmbeddedNul: true);
		nint buffer = 0;
		try
		{
			VeroScriptValue.ThrowIfFailed(
				NativeMethods.vs_interop_locale_translate(message.View, &buffer),
				"translate diagnostic");
			return NativeBuffer.CopyUtf8AndDestroy(ref buffer);
		}
		finally
		{
			NativeBuffer.Destroy(ref buffer);
		}
	}

	public static void LoadCatalogDirectory(string directory)
		=> UpdateCatalogDirectory(directory, merge: false, overrideExisting: false);

	public static void MergeCatalogDirectory(string directory, bool overrideExisting = false)
		=> UpdateCatalogDirectory(directory, merge: true, overrideExisting);

	private static void UpdateCatalogDirectory(string directory, bool merge, bool overrideExisting)
	{
		EnsureCompatible();
		using var path = new Utf8(directory);
		nint error = 0;
		VeroScriptStatus status = merge
			? NativeMethods.vs_interop_locale_merge_catalog_directory(path.View,
				overrideExisting ? (byte)1 : (byte)0, &error)
			: NativeMethods.vs_interop_locale_load_catalog_directory(path.View, &error);
		string detail = NativeBuffer.CopyOptionalErrorAndDestroy(error);
		if (status != VeroScriptStatus.Ok)
		{
			throw new VeroScriptException(status, string.IsNullOrWhiteSpace(detail)
				? $"VeroScript failed to {(merge ? "merge" : "load")} the catalog directory: {status}."
				: detail);
		}
	}

	internal static void EnsureCompatible()
	{
		if (Volatile.Read(ref _compatible) != 0) return;
		NativeMethods.EnsureLoaded();
		uint actual = NativeMethods.vs_interop_get_abi_version();
		if (actual != SupportedAbiVersion)
		{
			throw new NotSupportedException(
				$"VeroScript Interop ABI 0x{actual:X8} is incompatible with the required ABI 0x{SupportedAbiVersion:X8}.");
		}
		Interlocked.Exchange(ref _compatible, 1);
	}

	private static VeroScriptCompileCapabilities ReadCompileCapabilities()
	{
		try
		{
			return (VeroScriptCompileCapabilities)NativeMethods.vs_interop_get_compile_capabilities();
		}
		catch (EntryPointNotFoundException)
		{
			// Compile services were added without changing the existing ABI.
			return VeroScriptCompileCapabilities.None;
		}
	}

	private static readonly Lazy<VeroScriptCompileCapabilities> _compileCapabilities = new(ReadCompileCapabilities);
	private static int _compatible;
}
