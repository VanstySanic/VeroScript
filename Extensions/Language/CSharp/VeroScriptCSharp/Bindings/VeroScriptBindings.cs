// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScript;

/// <summary>Specifies one annotation argument attached to a host declaration.</summary>
public sealed record VeroScriptAnnotationArgument(string Name, string? Value = null);
/// <summary>Specifies an annotation attached to a host declaration.</summary>
public sealed record VeroScriptAnnotation(string Name,
	IReadOnlyList<VeroScriptAnnotationArgument>? Arguments = null);
/// <summary>Describes one argument accepted by a registered annotation.</summary>
public sealed record VeroScriptAnnotationArgumentDefinition(
	string Name, string Detail = "", string Documentation = "");
/// <summary>Defines an annotation name policy and documentation for compilation, reflection and tooling.</summary>
public sealed record VeroScriptAnnotationDefinition(
	string Name, string Detail = "", string Documentation = "",
	IReadOnlyList<VeroScriptAnnotationArgumentDefinition>? Arguments = null)
{
	/// <summary>Gets whether the annotation name uses exact matching; defaults to true.</summary>
	/// <remarks>
	/// When false, only ASCII letters in the annotation name are folded.
	/// Argument names and metadata keys remain exact, and source spelling is preserved.
	/// </remarks>
	public bool CaseSensitive { get; init; } = true;
}

/// <summary>Defines a documented and annotated managed host function.</summary>
public sealed class VeroScriptHostFunctionDefinition
{
	public required string Name { get; init; }
	public required VeroScriptNativeType ReturnType { get; init; }
	public IReadOnlyList<VeroScriptNativeParameter> Parameters { get; init; } = [];
	public required VeroScriptNativeFunction Callback { get; init; }
	public string Documentation { get; init; } = string.Empty;
	public IReadOnlyList<string> ParameterNames { get; init; } = [];
	public IReadOnlyList<string> ParameterDocumentation { get; init; } = [];
	public string ReturnDocumentation { get; init; } = string.Empty;
	public IReadOnlyList<VeroScriptAnnotation> Annotations { get; init; } = [];
	public IReadOnlyList<IReadOnlyList<VeroScriptAnnotation>> ParameterAnnotations { get; init; } = [];
	public IReadOnlyList<string> TypeParameters { get; init; } = [];
	public string ScriptReturnTypeName { get; init; } = string.Empty;
	public bool IsGenericHostObjectCast { get; init; }
}

/// <summary>Defines a managed enum, struct, or class exposed to scripts.</summary>
public sealed class VeroScriptHostTypeDefinition
{
	public required string Name { get; init; }
	public VeroScriptHostTypeKind Kind { get; init; } = VeroScriptHostTypeKind.Class;
	public string Documentation { get; init; } = string.Empty;
	public IReadOnlyList<VeroScriptAnnotation> Annotations { get; init; } = [];
	public bool Inheritable { get; init; }
	public bool ScopedEnum { get; init; }
	public string EnumUnderlyingTypeName { get; init; } = string.Empty;
}

/// <summary>Defines one constructor, method, property, field, constant, or enum member.</summary>
public sealed class VeroScriptHostMemberDefinition
{
	public VeroScriptHostMemberKind Kind { get; init; }
	public required string Name { get; init; }
	public VeroScriptNativeType ReturnType { get; init; } = VeroScriptNativeType.Void;
	public IReadOnlyList<VeroScriptNativeParameter> Parameters { get; init; } = [];
	public VeroScriptNativeFunction? Callback { get; init; }
	public long EnumValue { get; init; }
	public string Documentation { get; init; } = string.Empty;
	public IReadOnlyList<string> ParameterNames { get; init; } = [];
	public IReadOnlyList<string> ParameterDocumentation { get; init; } = [];
	public string ReturnDocumentation { get; init; } = string.Empty;
	public IReadOnlyList<VeroScriptAnnotation> Annotations { get; init; } = [];
	public IReadOnlyList<IReadOnlyList<VeroScriptAnnotation>> ParameterAnnotations { get; init; } = [];
	public bool IsOperator { get; init; }
	public bool IsConstMethod { get; init; }
	public bool MutableThis { get; init; }
}

/// <summary>Defines optional value and subclass protocols for a host type.</summary>
public sealed class VeroScriptHostTypeProtocolDefinition
{
	public VeroScriptNativeFunction? CloneCallback { get; init; }
	public VeroScriptNativeFunction? EqualsCallback { get; init; }
	public VeroScriptNativeFunction? HashCallback { get; init; }
	public VeroScriptNativeFunction? SubclassFactoryCallback { get; init; }
}
