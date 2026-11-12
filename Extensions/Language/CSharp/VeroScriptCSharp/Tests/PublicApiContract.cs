// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Reflection;

internal static class PublicApiContract
{
	private const BindingFlags DeclaredPublic = BindingFlags.Public
		| BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;

	internal static void Verify(Assembly assembly, string baselinePath, string contractName)
	{
		string[] expected = File.ReadAllLines(baselinePath);
		string[] actual = Generate(assembly);
		if (expected.SequenceEqual(actual, StringComparer.Ordinal)) return;

		string missing = string.Join(Environment.NewLine,
			expected.Except(actual, StringComparer.Ordinal).Select(line => $"- {line}"));
		string added = string.Join(Environment.NewLine,
			actual.Except(expected, StringComparer.Ordinal).Select(line => $"+ {line}"));
		throw new InvalidOperationException(
			$"The {contractName} public API differs from PublicApi.txt.{Environment.NewLine}" +
			$"{missing}{Environment.NewLine}{added}".TrimEnd());
	}

	internal static string[] Generate(Assembly assembly)
	{
		var nullability = new NullabilityInfoContext();
		var lines = new List<string>();
		Type[] types = assembly.GetExportedTypes();
		Array.Sort(types, (left, right) => string.CompareOrdinal(left.FullName, right.FullName));
		foreach (Type type in types)
		{
			lines.Add(FormatTypeDeclaration(type));

			foreach (ConstructorInfo constructor in type.GetConstructors(DeclaredPublic))
			{
				lines.Add($"  ctor {FormatMethodName(type)}({FormatParameters(constructor, nullability)})");
			}

			foreach (FieldInfo field in type.GetFields(DeclaredPublic))
			{
				string modifiers = field.IsLiteral ? "const " : field.IsInitOnly ? "readonly " : string.Empty;
				string value = field.IsLiteral ? $" = {FormatConstant(field.GetRawConstantValue())}" : string.Empty;
				lines.Add($"  field {(field.IsStatic ? "static " : string.Empty)}{modifiers}" +
					$"{FormatType(field.FieldType, nullability.Create(field))} {field.Name}{value}");
			}

			foreach (PropertyInfo property in type.GetProperties(DeclaredPublic))
			{
				MethodInfo? accessor = property.GetMethod ?? property.SetMethod;
				string accessors = FormatPropertyAccessors(property);
				string required = property.GetCustomAttributesData().Any(attribute =>
					attribute.AttributeType.FullName ==
						"System.Runtime.CompilerServices.RequiredMemberAttribute")
					? "required " : string.Empty;
				string index = property.GetIndexParameters().Length == 0
					? property.Name
					: $"this[{FormatParameters(property.GetIndexParameters(), nullability)}]";
				lines.Add($"  property {(accessor?.IsStatic == true ? "static " : string.Empty)}" +
					$"{required}{FormatType(property.PropertyType, nullability.Create(property))} " +
					$"{index} {accessors}");
			}

			foreach (EventInfo eventInfo in type.GetEvents(DeclaredPublic))
			{
				MethodInfo? accessor = eventInfo.AddMethod ?? eventInfo.RemoveMethod;
				lines.Add($"  event {(accessor?.IsStatic == true ? "static " : string.Empty)}" +
					$"{FormatType(eventInfo.EventHandlerType!)} {eventInfo.Name}");
			}

			foreach (MethodInfo method in type.GetMethods(DeclaredPublic)
				.Where(method => !IsAccessor(method)))
			{
				string generic = method.IsGenericMethodDefinition
					? $"<{string.Join(", ", method.GetGenericArguments().Select(argument => argument.Name))}>"
					: string.Empty;
				lines.Add($"  method {(method.IsStatic ? "static " : string.Empty)}" +
					$"{FormatType(method.ReturnType, nullability.Create(method.ReturnParameter))} " +
					$"{method.Name}{generic}({FormatParameters(method, nullability)})" +
					FormatGenericConstraints(method.GetGenericArguments()));
			}

			lines.Add(string.Empty);
		}
		if (lines.Count != 0)
		{
			lines.RemoveAt(lines.Count - 1);
		}

		return lines.ToArray();
	}

	private static string FormatTypeDeclaration(Type type)
	{
		string kind = type.IsEnum ? "enum"
			: type.IsInterface ? "interface"
			: typeof(MulticastDelegate).IsAssignableFrom(type.BaseType) ? "delegate"
			: type.IsValueType ? "struct"
			: "class";
		string readOnly = type.IsValueType && type.GetCustomAttributesData().Any(attribute =>
			attribute.AttributeType.FullName ==
				"System.Runtime.CompilerServices.IsReadOnlyAttribute") ? "readonly " : string.Empty;
		string modifiers = readOnly + (type.IsAbstract && type.IsSealed ? "static "
			: type.IsAbstract && !type.IsInterface ? "abstract "
			: type.IsSealed && !type.IsValueType && kind != "delegate" ? "sealed "
			: string.Empty);
		var bases = new List<string>();
		if (type.BaseType is not null && type.BaseType != typeof(object)
			&& type.BaseType != typeof(ValueType) && type.BaseType != typeof(Enum)
			&& type.BaseType != typeof(MulticastDelegate))
		{
			bases.Add(FormatType(type.BaseType));
		}

		bases.AddRange(type.GetInterfaces().Select(item => FormatType(item))
					.Order(StringComparer.Ordinal));
		string inheritance = bases.Count == 0 ? string.Empty : $" : {string.Join(", ", bases)}";
		return $"type {modifiers}{kind} {FormatMethodName(type)}{inheritance}" +
			FormatGenericConstraints(type.GetGenericArguments());
	}

	private static string FormatMethodName(Type type)
	{
		string name = type.FullName ?? type.Name;
		int tick = name.IndexOf('`', StringComparison.Ordinal);
		if (tick >= 0)
		{
			name = name[..tick];
		}

		if (!type.IsGenericTypeDefinition) return name;
		return $"{name}<{string.Join(", ", type.GetGenericArguments().Select(argument => argument.Name))}>";
	}

	private static string FormatParameters(MethodBase method, NullabilityInfoContext nullability)
		=> FormatParameters(method.GetParameters(), nullability);

	private static string FormatParameters(ParameterInfo[] parameters, NullabilityInfoContext nullability)
		=> string.Join(", ", parameters.Select(parameter =>
		{
			Type type = parameter.ParameterType;
			string modifier = parameter.IsOut ? "out "
				: type.IsByRef && parameter.IsIn ? "in "
				: type.IsByRef ? "ref "
				: parameter.GetCustomAttribute<ParamArrayAttribute>() is not null ? "params "
				: string.Empty;
			string optional = parameter.HasDefaultValue
				? $" = {FormatConstant(parameter.DefaultValue)}" : string.Empty;
			return $"{modifier}{FormatType(type, nullability.Create(parameter))} {parameter.Name}{optional}";
		}));

	private static string FormatPropertyAccessors(PropertyInfo property)
	{
		var accessors = new List<string>(2);
		if (property.GetMethod?.IsPublic == true)
		{
			accessors.Add("get;");
		}

		if (property.SetMethod?.IsPublic == true)
		{
			bool init = property.SetMethod.ReturnParameter.GetRequiredCustomModifiers()
				.Contains(typeof(System.Runtime.CompilerServices.IsExternalInit));
			accessors.Add(init ? "init;" : "set;");
		}
		return $"{{ {string.Join(' ', accessors)} }}";
	}

	private static bool IsAccessor(MethodInfo method)
		=> method.IsSpecialName && (method.Name.StartsWith("get_", StringComparison.Ordinal)
			|| method.Name.StartsWith("set_", StringComparison.Ordinal)
			|| method.Name.StartsWith("add_", StringComparison.Ordinal)
			|| method.Name.StartsWith("remove_", StringComparison.Ordinal));

	private static string FormatGenericConstraints(Type[] arguments)
	{
		var clauses = new List<string>();
		foreach (Type argument in arguments.Where(argument => argument.IsGenericParameter))
		{
			var constraints = new List<string>();
			GenericParameterAttributes attributes = argument.GenericParameterAttributes
				& GenericParameterAttributes.SpecialConstraintMask;
			if (attributes.HasFlag(GenericParameterAttributes.ReferenceTypeConstraint))
			{
				constraints.Add("class");
			}

			if (attributes.HasFlag(GenericParameterAttributes.NotNullableValueTypeConstraint))
			{
				constraints.Add("struct");
			}

			constraints.AddRange(argument.GetGenericParameterConstraints()
							.Select(item => FormatType(item)));
			if (attributes.HasFlag(GenericParameterAttributes.DefaultConstructorConstraint)
				&& !attributes.HasFlag(GenericParameterAttributes.NotNullableValueTypeConstraint))
			{
				constraints.Add("new()");
			}

			if (constraints.Count != 0)
			{
				clauses.Add($" where {argument.Name} : {string.Join(", ", constraints)}");
			}
		}
		return string.Concat(clauses);
	}

	private static string FormatType(Type type, NullabilityInfo? nullability = null)
	{
		if (type.IsByRef)
		{
			return FormatType(type.GetElementType()!, nullability?.ElementType);
		}

		if (type.IsPointer)
		{
			return $"{FormatType(type.GetElementType()!, nullability?.ElementType)}*";
		}

		if (type.IsArray)
		{
			return $"{FormatType(type.GetElementType()!, nullability?.ElementType)}" +
						$"[{new string(',', type.GetArrayRank() - 1)}]";
		}

		if (type.IsGenericParameter) return type.Name;

		string name;
		if (type.IsGenericType)
		{
			Type definition = type.GetGenericTypeDefinition();
			name = definition.FullName ?? definition.Name;
			int tick = name.IndexOf('`', StringComparison.Ordinal);
			if (tick >= 0)
			{
				name = name[..tick];
			}

			Type[] arguments = type.GetGenericArguments();
			name += $"<{string.Join(", ", arguments.Select((argument, index) =>
				FormatType(argument, nullability?.GenericTypeArguments.ElementAtOrDefault(index))))}>";
		}
		else
		{
			name = type.FullName ?? type.Name;
		}

		if (!type.IsValueType && nullability?.ReadState == NullabilityState.Nullable)
		{
			name += "?";
		}

		return name.Replace('+', '.');
	}

	private static string FormatConstant(object? value)
	{
		if (value is null || value == DBNull.Value || value == Missing.Value) return "null";
		if (value is string text)
		{
			return $"\"{text.Replace("\\", "\\\\", StringComparison.Ordinal)
						.Replace("\"", "\\\"", StringComparison.Ordinal)}\"";
		}

		if (value is char character) return $"'{character}'";
		if (value is bool boolean) return boolean ? "true" : "false";
		return Convert.ToString(value, CultureInfo.InvariantCulture) ?? value.ToString()!;
	}
}
