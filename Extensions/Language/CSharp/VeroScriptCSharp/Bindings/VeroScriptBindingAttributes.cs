// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScript;

/// <summary>Marks a managed type for explicit VeroScript binding generation.</summary>
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct | AttributeTargets.Enum,
	AllowMultiple = false, Inherited = false)]
public sealed class VeroScriptBindingAttribute : Attribute;

/// <summary>Excludes a managed type or member from VeroScript binding generation.</summary>
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct | AttributeTargets.Enum
	| AttributeTargets.Constructor | AttributeTargets.Method | AttributeTargets.Property
	| AttributeTargets.Field | AttributeTargets.Event,
	AllowMultiple = false, Inherited = false)]
public sealed class VeroScriptIgnoreAttribute : Attribute;
