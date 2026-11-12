// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using VeroScript.Interop;

namespace VeroScript;

internal sealed unsafe class NativeBindingLease : IDisposable
{
	private readonly NativeTypeMarshaller _types = new();
	private readonly List<Utf8> _strings = [];
	private readonly List<nint> _allocations = [];

	internal NativeStringView MarshalString(string value) => MakeString(value);
	internal NativeStringList MarshalStringList(IReadOnlyList<string> values) => MakeStringList(values);

	internal NativeHostFunctionDescriptor MakeFunction(VeroScriptHostFunctionDefinition definition,
		delegate* unmanaged[Cdecl]<nint, nint, nint, uint> callback, nint userData)
	{
		ArgumentNullException.ThrowIfNull(definition.ReturnType);
		ArgumentNullException.ThrowIfNull(definition.Parameters);
		ArgumentNullException.ThrowIfNull(definition.ParameterNames);
		ArgumentNullException.ThrowIfNull(definition.ParameterDocumentation);
		ArgumentNullException.ThrowIfNull(definition.Annotations);
		ArgumentNullException.ThrowIfNull(definition.ParameterAnnotations);
		ArgumentNullException.ThrowIfNull(definition.TypeParameters);
		VeroScriptNativeParameter[] parameters = definition.Parameters.ToArray();
		string[] parameterNames = definition.ParameterNames.ToArray();
		string[] parameterDocumentation = definition.ParameterDocumentation.ToArray();
		VeroScriptAnnotation[] annotations = definition.Annotations.ToArray();
		IReadOnlyList<VeroScriptAnnotation>[] parameterAnnotations =
			definition.ParameterAnnotations.ToArray();
		string[] typeParameters = definition.TypeParameters.ToArray();
		ValidateParameterMetadata(parameters.Length, parameterNames,
			parameterDocumentation, parameterAnnotations);
		return new NativeHostFunctionDescriptor
		{
			StructSize = (uint)sizeof(NativeHostFunctionDescriptor),
			Name = MakeString(definition.Name),
			ReturnType = _types.Marshal(definition.ReturnType),
			Parameters = MakeParameters(parameters),
			ParameterCount = (ulong)parameters.Length,
			Callback = callback,
			UserData = userData,
			Documentation = MakeString(definition.Documentation),
			ParameterNames = MakeStringList(parameterNames),
			ParameterDocumentation = MakeStringList(parameterDocumentation),
			ReturnDocumentation = MakeString(definition.ReturnDocumentation),
			Annotations = MakeAnnotationList(annotations),
			ParameterAnnotations = MakeParameterAnnotations(parameterAnnotations),
			ParameterAnnotationCount = (ulong)parameterAnnotations.Length,
			TypeParameters = MakeStringList(typeParameters),
			ScriptReturnTypeName = MakeString(definition.ScriptReturnTypeName),
			IsGenericHostObjectCast = definition.IsGenericHostObjectCast ? (byte)1 : (byte)0
		};
	}

	internal NativeHostTypeDescriptor MakeType(VeroScriptHostTypeDefinition definition)
	{
		ArgumentNullException.ThrowIfNull(definition.Annotations);
		VeroScriptAnnotation[] annotations = definition.Annotations.ToArray();
		return new()
		{
			StructSize = (uint)sizeof(NativeHostTypeDescriptor),
			Name = MakeString(definition.Name),
			Kind = definition.Kind,
			Documentation = MakeString(definition.Documentation),
			Annotations = MakeAnnotationList(annotations),
			Inheritable = definition.Inheritable ? (byte)1 : (byte)0,
			ScopedEnum = definition.ScopedEnum ? (byte)1 : (byte)0,
			EnumUnderlyingTypeName = MakeString(definition.EnumUnderlyingTypeName)
		};
	}

	internal NativeAnnotationDescriptor MakeAnnotationDefinition(
		VeroScriptAnnotationDefinition definition)
	{
		VeroScriptAnnotationArgumentDefinition[] arguments = (definition.Arguments ?? []).ToArray();
		NativeAnnotationArgumentDescriptor* nativeArguments = arguments.Length == 0
			? null : Allocate<NativeAnnotationArgumentDescriptor>(arguments.Length);
		for (int i = 0; i < arguments.Length; ++i)
		{
			VeroScriptAnnotationArgumentDefinition argument = arguments[i]
				?? throw new ArgumentException("Annotation argument definitions cannot contain null.");
			nativeArguments[i] = new NativeAnnotationArgumentDescriptor
			{
				StructSize = (uint)sizeof(NativeAnnotationArgumentDescriptor),
				Name = MakeString(argument.Name),
				Detail = MakeString(argument.Detail),
				Documentation = MakeString(argument.Documentation)
			};
		}
		return new NativeAnnotationDescriptor
		{
			StructSize = (uint)sizeof(NativeAnnotationDescriptor),
			Name = MakeString(definition.Name),
			Detail = MakeString(definition.Detail),
			Documentation = MakeString(definition.Documentation),
			Arguments = nativeArguments,
			ArgumentCount = (ulong)arguments.Length,
			NameFlags = definition.CaseSensitive ? 0u : 1u
		};
	}

	internal NativeHostMemberDescriptor MakeMember(VeroScriptHostMemberDefinition definition,
		delegate* unmanaged[Cdecl]<nint, nint, nint, uint> callback, nint userData)
	{
		ArgumentNullException.ThrowIfNull(definition.ReturnType);
		ArgumentNullException.ThrowIfNull(definition.Parameters);
		ArgumentNullException.ThrowIfNull(definition.ParameterNames);
		ArgumentNullException.ThrowIfNull(definition.ParameterDocumentation);
		ArgumentNullException.ThrowIfNull(definition.Annotations);
		ArgumentNullException.ThrowIfNull(definition.ParameterAnnotations);
		VeroScriptNativeParameter[] parameters = definition.Parameters.ToArray();
		string[] parameterNames = definition.ParameterNames.ToArray();
		string[] parameterDocumentation = definition.ParameterDocumentation.ToArray();
		VeroScriptAnnotation[] annotations = definition.Annotations.ToArray();
		IReadOnlyList<VeroScriptAnnotation>[] parameterAnnotations =
			definition.ParameterAnnotations.ToArray();
		ValidateParameterMetadata(parameters.Length, parameterNames,
			parameterDocumentation, parameterAnnotations);
		return new NativeHostMemberDescriptor
		{
			StructSize = (uint)sizeof(NativeHostMemberDescriptor),
			Kind = definition.Kind,
			Name = MakeString(definition.Name),
			ReturnType = _types.Marshal(definition.ReturnType),
			Parameters = MakeParameters(parameters),
			ParameterCount = (ulong)parameters.Length,
			Callback = callback,
			UserData = userData,
			EnumValue = definition.EnumValue,
			Documentation = MakeString(definition.Documentation),
			ParameterNames = MakeStringList(parameterNames),
			ParameterDocumentation = MakeStringList(parameterDocumentation),
			ReturnDocumentation = MakeString(definition.ReturnDocumentation),
			Annotations = MakeAnnotationList(annotations),
			ParameterAnnotations = MakeParameterAnnotations(parameterAnnotations),
			ParameterAnnotationCount = (ulong)parameterAnnotations.Length,
			IsOperator = definition.IsOperator ? (byte)1 : (byte)0,
			IsConstMethod = definition.IsConstMethod ? (byte)1 : (byte)0,
			MutableThis = definition.MutableThis ? (byte)1 : (byte)0
		};
	}

	private NativeParameterDescriptor* MakeParameters(IReadOnlyList<VeroScriptNativeParameter> parameters)
	{
		VeroScriptNativeParameter[] snapshot = parameters.ToArray();
		if (snapshot.Length == 0) return null;
		NativeParameterDescriptor* result = Allocate<NativeParameterDescriptor>(snapshot.Length);
		for (int i = 0; i < snapshot.Length; ++i)
		{
			result[i] = new NativeParameterDescriptor
			{
				StructSize = (uint)sizeof(NativeParameterDescriptor),
				Type = _types.Marshal(snapshot[i].Type),
				Flags = snapshot[i].Flags
			};
		}

		return result;
	}

	private NativeAnnotationList MakeAnnotationList(IReadOnlyList<VeroScriptAnnotation>? annotations)
	{
		if (annotations is null) return default;
		VeroScriptAnnotation[] snapshot = annotations.ToArray();
		if (snapshot.Length == 0) return default;
		NativeAnnotation* result = Allocate<NativeAnnotation>(snapshot.Length);
		for (int i = 0; i < snapshot.Length; ++i)
		{
			VeroScriptAnnotation annotation = snapshot[i]
				?? throw new ArgumentException("Annotations cannot contain null.");
			VeroScriptAnnotationArgument[] arguments = (annotation.Arguments ?? []).ToArray();
			NativeAnnotationArgument* nativeArguments = arguments.Length == 0
				? null : Allocate<NativeAnnotationArgument>(arguments.Length);
			for (int j = 0; j < arguments.Length; ++j)
			{
				VeroScriptAnnotationArgument argument = arguments[j]
					?? throw new ArgumentException("Annotation arguments cannot contain null.");
				nativeArguments[j] = new NativeAnnotationArgument
				{
					Name = MakeString(argument.Name),
					Value = MakeString(argument.Value ?? string.Empty),
					HasValue = argument.Value is null ? (byte)0 : (byte)1
				};
			}
			result[i] = new NativeAnnotation
			{
				StructSize = (uint)sizeof(NativeAnnotation),
				Name = MakeString(annotation.Name),
				Arguments = nativeArguments,
				ArgumentCount = (ulong)arguments.Length
			};
		}
		return new NativeAnnotationList { Values = result, Count = (ulong)snapshot.Length };
	}

	private NativeAnnotationList* MakeParameterAnnotations(
		IReadOnlyList<IReadOnlyList<VeroScriptAnnotation>> annotations)
	{
		IReadOnlyList<VeroScriptAnnotation>[] snapshot = annotations.ToArray();
		if (snapshot.Length == 0) return null;
		NativeAnnotationList* result = Allocate<NativeAnnotationList>(snapshot.Length);
		for (int i = 0; i < snapshot.Length; ++i)
		{
			result[i] = MakeAnnotationList(snapshot[i]
						?? throw new ArgumentException("Parameter annotation lists cannot contain null."));
		}

		return result;
	}

	private NativeStringList MakeStringList(IReadOnlyList<string> values)
	{
		ArgumentNullException.ThrowIfNull(values);
		string[] snapshot = values.ToArray();
		if (snapshot.Length == 0) return default;
		NativeStringView* result = Allocate<NativeStringView>(snapshot.Length);
		for (int i = 0; i < snapshot.Length; ++i)
		{
			result[i] = MakeString(snapshot[i]);
		}

		return new NativeStringList { Values = result, Count = (ulong)snapshot.Length };
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
		int size = checked(sizeof(T) * count);
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
		return (T*)memory;
	}

	private static void ValidateParameterMetadata(int parameterCount,
		string[] names, string[] documentation,
		IReadOnlyList<VeroScriptAnnotation>[] annotations)
	{
		if (names.Length != 0 && names.Length != parameterCount)
		{
			throw new ArgumentException("Parameter names must be empty or match the parameter count.");
		}

		if (documentation.Length != 0 && documentation.Length != parameterCount)
		{
			throw new ArgumentException("Parameter documentation must be empty or match the parameter count.");
		}

		if (annotations.Length != 0 && annotations.Length != parameterCount)
		{
			throw new ArgumentException("Parameter annotations must be empty or match the parameter count.");
		}
	}

	public void Dispose()
	{
		_types.Dispose();
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
