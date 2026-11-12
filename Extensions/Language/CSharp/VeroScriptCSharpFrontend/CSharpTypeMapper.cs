// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScript.CSharpFrontend;

public static class CSharpTypeMapper
{
	private static readonly Dictionary<string, string> PrimitiveTypes =
		new Dictionary<string, string>(StringComparer.Ordinal)
		{
			["void"] = "void",
			["bool"] = "bool",
			["sbyte"] = "int8",
			["byte"] = "uint8",
			["short"] = "int16",
			["ushort"] = "uint16",
			["int"] = "int",
			["uint"] = "unsigned int",
			["long"] = "int64",
			["ulong"] = "uint64",
			["float"] = "float",
			["double"] = "double",
			// A C# char is one unsigned UTF-16 code unit, not VeroScript's int8 `char`.
			["char"] = "uint16",
			["string"] = "string"
		};

	private static readonly Dictionary<string, string> GenericContainers =
		new Dictionary<string, string>(StringComparer.Ordinal)
		{
			["System.Collections.Generic.List<T>"] = "array",
			["System.Collections.Generic.Dictionary<TKey, TValue>"] = "map",
			["System.Collections.Generic.SortedDictionary<TKey, TValue>"] = "ordered_map",
			["System.Collections.Generic.HashSet<T>"] = "set",
			["System.Collections.Generic.Queue<T>"] = "queue",
			["System.Collections.Generic.Stack<T>"] = "stack"
		};

	public static CSharpTypeMapResult MapTypeToVeroScript(
		CSharpTypeReference type,
		CSharpTypeMapOptions? options = null)
	{
		ArgumentNullException.ThrowIfNull(type);
		options ??= new CSharpTypeMapOptions();
		if (type.IsNullable)
		{
			return Fail("VSCF2001", $"Nullable C# type '{type.DisplayName}' has no implicit VeroScript mapping.");
		}

		if (type.Kind == CSharpTypeKind.Dynamic)
		{
			return Fail("VSCF2002", "C# dynamic cannot be mapped without changing runtime semantics.");
		}

		if (type.Kind == CSharpTypeKind.Pointer)
		{
			return Fail("VSCF2003", "C# pointer types require an explicit host ownership policy.");
		}

		if (type.Kind == CSharpTypeKind.Delegate)
		{
			return Fail("VSCF2004", "C# delegates require an explicit callable lifetime policy.");
		}

		if (type.Kind == CSharpTypeKind.TypeParameter)
		{
			return Fail("VSCF2005", $"Open type parameter '{type.DisplayName}' cannot be mapped directly.");
		}

		if (type.Kind is CSharpTypeKind.Error or CSharpTypeKind.Unknown)
		{
			return Fail("VSCF2006", $"Unresolved C# type '{type.DisplayName}' cannot be mapped.");
		}

		if (type.Kind == CSharpTypeKind.Array)
		{
			if (type.ArrayRank != 1 || type.TypeArguments.Count != 1)
			{
				return Fail("VSCF2007", "Only one-dimensional C# arrays have a VeroScript array mapping.");
			}

			CSharpTypeMapResult element = MapTypeToVeroScript(type.TypeArguments[0], options);
			return element.Success ? Success($"array<{element.VeroType}>") : element;
		}

		if (PrimitiveTypes.TryGetValue(type.DisplayName, out string? primitive))
		{
			return Success(primitive);
		}

		string genericDefinition = GetGenericDefinition(type.QualifiedName, type.TypeArguments.Count);
		if (GenericContainers.TryGetValue(genericDefinition, out string? container))
		{
			var mappedArguments = new string[type.TypeArguments.Count];
			for (int i = 0; i < mappedArguments.Length; ++i)
			{
				CSharpTypeMapResult mapped = MapTypeToVeroScript(type.TypeArguments[i], options);
				if (!mapped.Success) return mapped;
				mappedArguments[i] = mapped.VeroType;
			}
			return Success($"{container}<{string.Join(",", mappedArguments)}>");
		}

		if (type.Kind == CSharpTypeKind.Enum
			|| options.KnownEnumTypes.Contains(type.QualifiedName, StringComparer.Ordinal)
			|| options.KnownHostTypes.Contains(type.QualifiedName, StringComparer.Ordinal))
		{
			return Success(ToVeroQualifiedName(type.QualifiedName));
		}

		return Fail("VSCF2008",
			$"C# type '{type.DisplayName}' is not a known host type or supported container.");
	}

	private static string GetGenericDefinition(string qualifiedName, int arity)
	{
		if (arity is not (1 or 2)) return qualifiedName;
		int open = qualifiedName.IndexOf('<', StringComparison.Ordinal);
		if (open < 0) return qualifiedName;
		int close = qualifiedName.LastIndexOf('>');
		if (close < open) return qualifiedName;
		string[] names = arity == 1 ? ["T"] : ["TKey", "TValue"];
		return qualifiedName[..open] + "<" + string.Join(", ", names) + ">";
	}

	private static string ToVeroQualifiedName(string name)
		=> name.Replace(".", "::", StringComparison.Ordinal);

	private static CSharpTypeMapResult Success(string type) => new(true, type, "", "");

	private static CSharpTypeMapResult Fail(string code, string message)
		=> new(false, "", code, message);
}
