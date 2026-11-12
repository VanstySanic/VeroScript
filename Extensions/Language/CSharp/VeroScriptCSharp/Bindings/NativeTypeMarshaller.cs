// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using VeroScript.Interop;

namespace VeroScript;

internal sealed unsafe class NativeTypeMarshaller : IDisposable
{
	private const int MaxNestingDepth = 64;
	private readonly List<IDisposable> _strings = [];
	private readonly List<nint> _allocations = [];

	internal NativeTypeDescriptor Marshal(VeroScriptNativeType type)
		=> Marshal(type, new HashSet<VeroScriptNativeType>(ReferenceEqualityComparer.Instance), 0);

	private NativeTypeDescriptor Marshal(VeroScriptNativeType type,
		HashSet<VeroScriptNativeType> path, int depth)
	{
		ArgumentNullException.ThrowIfNull(type);
		if (depth >= MaxNestingDepth)
		{
			throw new ArgumentException(
						$"Native type descriptions cannot exceed {MaxNestingDepth} nesting levels.",
						nameof(type));
		}

		if (!path.Add(type))
		{
			throw new ArgumentException("Native type descriptions cannot contain cycles.", nameof(type));
		}

		try
		{
			return MarshalCore(type, path, depth);
		}
		finally
		{
			path.Remove(type);
		}
	}

	private NativeTypeDescriptor MarshalCore(VeroScriptNativeType type,
		HashSet<VeroScriptNativeType> path, int depth)
	{
		var result = new NativeTypeDescriptor
		{
			StructSize = (uint)sizeof(NativeTypeDescriptor),
			Type = type.Kind
		};
		if (!string.IsNullOrEmpty(type.Name))
		{
			var text = new Utf8(type.Name);
			try
			{
				_strings.Add(text);
			}
			catch
			{
				text.Dispose();
				throw;
			}
			result.Name = text.View;
		}

		VeroScriptNativeType[] typeArguments = type.TypeArguments
			?? throw new ArgumentException("Native type arguments cannot be null.", nameof(type));
		if (typeArguments.Length != 0)
		{
			int bytes = checked(sizeof(NativeTypeDescriptor) * typeArguments.Length);
			nint memory = System.Runtime.InteropServices.Marshal.AllocHGlobal(bytes);
			try
			{
				_allocations.Add(memory);
			}
			catch
			{
				System.Runtime.InteropServices.Marshal.FreeHGlobal(memory);
				throw;
			}
			var arguments = (NativeTypeDescriptor*)memory;
			for (int i = 0; i < typeArguments.Length; ++i)
			{
				arguments[i] = Marshal(typeArguments[i]
								?? throw new ArgumentException("Native type arguments cannot contain null.", nameof(type)),
								path, depth + 1);
			}

			result.TypeArguments = arguments;
			result.TypeArgumentCount = (ulong)typeArguments.Length;
		}
		return result;
	}

	public void Dispose()
	{
		foreach (IDisposable item in _strings)
		{
			item.Dispose();
		}

		foreach (nint item in _allocations)
		{
			System.Runtime.InteropServices.Marshal.FreeHGlobal(item);
		}
	}
}
