// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace VeroScript.Interop;

internal sealed unsafe class NativeConfigLease : IDisposable
{
	private readonly List<Utf8> _strings = [];
	private readonly List<nint> _allocations = [];
	internal NativeConfig Value;

	internal NativeConfigLease(VeroScriptConfiguration configuration)
	{
		ArgumentNullException.ThrowIfNull(configuration);
		try
		{
			Value = new NativeConfig
			{
				StructSize = (uint)sizeof(NativeConfig),
				ImportPaths = MakeStringList(configuration.ImportPaths),
				ImplicitImportPaths = MakeStringList(configuration.ImplicitImportPaths),
				ScriptArguments = MakeStringList(configuration.ScriptArguments),
				Defines = MakeStringList(configuration.Defines),
				StandardLibrarySearchPaths = MakeStringList(configuration.StandardLibrarySearchPaths),
				BytecodeCacheDirectory = MakeString(configuration.BytecodeCacheDirectory),
				MaxCallDepth = configuration.MaxCallDepth,
				ExecutionBackend = configuration.ExecutionBackend,
				UseBytecodeCache = configuration.UseBytecodeCache ? (byte)1 : (byte)0,
				AllowCircularImports = configuration.AllowCircularImports ? (byte)1 : (byte)0,
				AllowProjectWideAccess = configuration.AllowProjectWideAccess ? (byte)1 : (byte)0,
				MaxInstructions = configuration.MaxInstructions,
				TimeoutMilliseconds = configuration.TimeoutMilliseconds,
				AllowedScriptCapabilities = (uint)configuration.AllowedScriptCapabilities,
				EnableExecutionControl = configuration.EnableExecutionControl ? (byte)1 : (byte)0,
				CapabilityPolicyEnabled = configuration.CapabilityPolicyEnabled ? (byte)1 : (byte)0
			};

			VeroScriptSourceOverlay[] overlays = (configuration.SourceOverlays
				?? throw new ArgumentException("Source overlays cannot be null.", nameof(configuration)))
				.ToArray();
			if (overlays.Length != 0)
			{
				Value.SourceOverlays = Allocate<NativeSourceOverlay>(overlays.Length);
				Value.SourceOverlayCount = (ulong)overlays.Length;
				for (int i = 0; i < overlays.Length; ++i)
				{
					VeroScriptSourceOverlay overlay = overlays[i]
						?? throw new ArgumentException("Source overlays cannot contain null.", nameof(configuration));
					Value.SourceOverlays[i] = new NativeSourceOverlay
					{
						Path = MakeString(overlay.Path),
						Source = MakeString(overlay.Source)
					};
				}
			}
			VeroScriptPredefinedMacro[] macros = (configuration.PredefinedMacros
				?? throw new ArgumentException("Predefined macros cannot be null.", nameof(configuration)))
				.ToArray();
			if (macros.Length != 0)
			{
				Value.PredefinedMacros = Allocate<NativePredefinedMacro>(macros.Length);
				Value.PredefinedMacroCount = (ulong)macros.Length;
				for (int i = 0; i < macros.Length; ++i)
				{
					VeroScriptPredefinedMacro macro = macros[i]
						?? throw new ArgumentException("Predefined macros cannot contain null.", nameof(configuration));
					Value.PredefinedMacros[i] = new NativePredefinedMacro
					{
						StructSize = (uint)sizeof(NativePredefinedMacro),
						Name = MakeString(macro.Name),
						Parameters = MakeStringList(macro.Parameters ?? []),
						Body = MakeString(macro.Body),
						SourceName = MakeString(macro.SourceName),
						Variadic = macro.Variadic ? (byte)1 : (byte)0
					};
				}
			}
		}
		catch
		{
			Dispose();
			throw;
		}
	}

	private NativeStringList MakeStringList(IReadOnlyList<string>? values)
	{
		if (values is null)
		{
			throw new ArgumentException("String lists cannot be null.");
		}

		string[] snapshot = values.ToArray();
		if (snapshot.Length == 0) return default;
		NativeStringView* nativeValues = Allocate<NativeStringView>(snapshot.Length);
		for (int i = 0; i < snapshot.Length; ++i)
		{
			nativeValues[i] = MakeString(snapshot[i]
						?? throw new ArgumentException("String lists cannot contain null."));
		}

		return new NativeStringList { Values = nativeValues, Count = (ulong)snapshot.Length };
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

	private T* Allocate<T>(int count) where T : unmanaged
	{
		nint memory = Marshal.AllocHGlobal(checked(sizeof(T) * count));
		try
		{
			new Span<byte>((void*)memory, checked(sizeof(T) * count)).Clear();
			_allocations.Add(memory);
		}
		catch
		{
			Marshal.FreeHGlobal(memory);
			throw;
		}
		return (T*)memory;
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
