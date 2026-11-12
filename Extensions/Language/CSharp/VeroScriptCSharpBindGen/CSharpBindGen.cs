// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using VeroScript.CSharpFrontend;

namespace VeroScript.CSharpBindGen;

public static class CSharpBindGen
{
	private static readonly JsonSerializerOptions JsonOptions = new()
	{
		WriteIndented = true,
		PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
		DefaultIgnoreCondition = JsonIgnoreCondition.Never
	};

	public static CSharpBindGenResult GenerateBindings(
		CSharpTranslationUnit unit,
		CSharpBindGenOptions? options = null)
	{
		ArgumentNullException.ThrowIfNull(unit);
		options ??= new CSharpBindGenOptions();
		var diagnostics = new List<CSharpDiagnostic>();
		var exported = unit.Types.Where(type => IsExported(type, options)).ToArray();
		var knownHosts = exported.Where(type => type.Kind is CSharpTypeKind.Class or CSharpTypeKind.Struct)
			.Select(type => type.QualifiedName).ToArray();
		var knownEnums = exported.Where(type => type.Kind == CSharpTypeKind.Enum)
			.Select(type => type.QualifiedName).ToArray();
		var mapOptions = new CSharpTypeMapOptions
		{
			KnownHostTypes = knownHosts,
			KnownEnumTypes = knownEnums
		};
		var types = new List<CatalogType>();
		foreach (CSharpTypeDeclaration type in exported)
		{
			if (type.IsRefLike)
			{
				Error(diagnostics, "VSCB1014",
					$"Ref-like host type '{type.QualifiedName}' cannot cross the object-based VeroScript host boundary.",
					FirstLocation(type));
				continue;
			}
			CSharpSourceLocation requiredLocation = type.Fields.FirstOrDefault(field => field.IsRequired)?.Location
				?? type.Properties.FirstOrDefault(property => property.IsRequired)?.Location
				?? CSharpSourceLocation.None;
			if (requiredLocation != CSharpSourceLocation.None)
			{
				Error(diagnostics, "VSCB1015",
					$"Host type '{type.QualifiedName}' contains required members; generated constructors cannot preserve C# object-initializer requirements.",
					requiredLocation);
				continue;
			}
			// A nested declaration can still depend on an outer type's parameters.
			if (type.TypeParameters.Count != 0 || type.QualifiedName.Contains('<', StringComparison.Ordinal))
			{
				Error(diagnostics, "VSCB1001", $"Generic host type '{type.QualifiedName}' is not supported yet.", FirstLocation(type));
				continue;
			}
			if (type.Kind is not (CSharpTypeKind.Class or CSharpTypeKind.Struct or CSharpTypeKind.Enum))
			{
				Error(diagnostics, "VSCB1002", $"Public {type.Kind.ToString().ToLowerInvariant()} '{type.QualifiedName}' cannot be represented by a binding catalog.", FirstLocation(type));
				continue;
			}
			CatalogType? catalogType = BuildType(type, mapOptions, diagnostics, options);
			if (catalogType is not null)
			{
				types.Add(catalogType);
			}
		}

		string registrationSource = options.EmitRegistrationSource
			? CSharpRegistrationGenerator.Generate(exported, mapOptions, options, diagnostics) : "";
		string aotBridgeSource = options.EmitAotBridgeSource
			? CSharpAotBridgeGenerator.Generate(exported, options, diagnostics) : "";
		if (diagnostics.Any(diagnostic => diagnostic.Severity == CSharpDiagnosticSeverity.Error))
		{
			return new("", "", "", diagnostics);
		}

		var catalog = new Catalog(1, [], [], types, [], []);
		string json = JsonSerializer.Serialize(catalog, JsonOptions) + Environment.NewLine;
		return new(json, options.EmitApiReference ? BuildApiReference(types) : "",
			registrationSource, diagnostics)
		{
			AotBridgeSource = aotBridgeSource
		};
	}

	public static CSharpBindGenResult GenerateBindingsFromSources(
		IReadOnlyList<CSharpSourceFile> sources,
		CSharpParseOptions? parseOptions = null,
		CSharpBindGenOptions? bindGenOptions = null)
	{
		CSharpParseResult parsed = CSharpFrontend.CSharpFrontend.ParseSources(sources, parseOptions);
		if (!parsed.Succeeded) return new("", "", "", parsed.Diagnostics);
		CSharpBindGenResult generated = GenerateBindings(parsed.Unit, bindGenOptions);
		return generated with
		{
			Diagnostics = parsed.Diagnostics.Concat(generated.Diagnostics).ToArray()
		};
	}

	public static CSharpBindGenResult GenerateBindingsFromSource(
		string source,
		string sourcePath = "<csharp-source>",
		CSharpParseOptions? parseOptions = null,
		CSharpBindGenOptions? bindGenOptions = null)
		=> GenerateBindingsFromSources([new(sourcePath, source)], parseOptions, bindGenOptions);

	public static CSharpBindGenResult GenerateBindingsFromFiles(
		IReadOnlyList<string> filePaths,
		CSharpParseOptions? parseOptions = null,
		CSharpBindGenOptions? bindGenOptions = null)
	{
		ArgumentNullException.ThrowIfNull(filePaths);
		return GenerateBindingsFromSources(filePaths.Select(path =>
			new CSharpSourceFile(path, File.ReadAllText(path))).ToArray(), parseOptions, bindGenOptions);
	}

	public static CSharpBindGenProjectResult GenerateBindingProject(
		IReadOnlyList<string> filePaths,
		CSharpParseOptions? parseOptions = null,
		CSharpBindGenOptions? bindGenOptions = null,
		CSharpBindGenProjectLayout? layout = null)
	{
		ArgumentNullException.ThrowIfNull(filePaths);
		bindGenOptions ??= new CSharpBindGenOptions();
		layout ??= new CSharpBindGenProjectLayout();
		var diagnostics = new List<CSharpDiagnostic>();
		if (filePaths.Count == 0)
		{
			Error(diagnostics, "VSCB2000", "At least one C# input file is required.",
				CSharpSourceLocation.None);
			return new("", "", [], "", diagnostics);
		}
		if (string.IsNullOrWhiteSpace(layout.CatalogPath)
			|| bindGenOptions.EmitRegistrationSource
				&& string.IsNullOrWhiteSpace(layout.RegistrationPath)
			|| bindGenOptions.EmitAotBridgeSource
				&& string.IsNullOrWhiteSpace(layout.AotBridgePath))
		{
			Error(diagnostics, "VSCB2002",
				"Project catalog and enabled registration paths must not be empty.",
				CSharpSourceLocation.None);
			return new("", "", [], "", diagnostics);
		}

		var sources = new List<CSharpSourceFile>(filePaths.Count);
		var seen = new HashSet<string>(PathComparer);
		foreach (string? filePath in filePaths)
		{
			if (string.IsNullOrWhiteSpace(filePath))
			{
				throw new ArgumentException("File paths cannot contain null or empty values.", nameof(filePaths));
			}

			string fullPath = Path.GetFullPath(filePath);
			if (!seen.Add(fullPath))
			{
				Error(diagnostics, "VSCB2001",
					$"The same C# input file was supplied more than once: '{filePath}'.",
					new(filePath, 1, 1, 0, 0));
				continue;
			}
			sources.Add(new(filePath, File.ReadAllText(filePath)));
		}
		if (diagnostics.Any(diagnostic => diagnostic.Severity == CSharpDiagnosticSeverity.Error))
		{
			return new("", "", [], "", diagnostics);
		}

		CSharpParseResult parsed = CSharpFrontend.CSharpFrontend.ParseSources(sources, parseOptions);
		diagnostics.AddRange(parsed.Diagnostics);
		if (!parsed.Succeeded) return new("", "", [], "", diagnostics);

		var mergedOptions = new CSharpBindGenOptions
		{
			EmitApiReference = false,
			EmitRegistrationSource = bindGenOptions.EmitRegistrationSource,
			EmitAotBridgeSource = bindGenOptions.EmitAotBridgeSource,
			RegistrationNamespace = bindGenOptions.RegistrationNamespace,
			RegistrationClassName = bindGenOptions.RegistrationClassName,
			RequireBindingAttribute = bindGenOptions.RequireBindingAttribute,
			BindingAttributeName = bindGenOptions.BindingAttributeName,
			IgnoreAttributeName = bindGenOptions.IgnoreAttributeName
		};
		CSharpBindGenResult merged = GenerateBindings(parsed.Unit, mergedOptions);
		diagnostics.AddRange(merged.Diagnostics);
		if (!merged.Succeeded) return new("", "", [], "", diagnostics);

		var apiReferences = new List<CSharpBindGenApiReference>();
		if (bindGenOptions.EmitApiReference)
		{
			Catalog catalog = JsonSerializer.Deserialize<Catalog>(merged.CatalogJson, JsonOptions)
				?? throw new InvalidOperationException("Generated binding catalog could not be read back.");
			var ownerByType = parsed.Unit.Types
				.Where(type => type.Declarations.Count != 0)
				.ToDictionary(type => ToScriptName(type.QualifiedName),
					type => Path.GetFullPath(type.Declarations[0].File), StringComparer.Ordinal);
			// Build ownership once instead of scanning every catalog type for
			// every input file. Lookup groups retain the catalog's member order.
			ILookup<string, CatalogType> typesByOwner = catalog.Types
				.Where(type => ownerByType.ContainsKey(type.Name))
				.ToLookup(type => ownerByType[type.Name], PathComparer);
			Dictionary<string, int> stemCounts = sources
				.GroupBy(source => PortableNameKey(OutputStem(source.Path)), StringComparer.Ordinal)
				.ToDictionary(group => group.Key, group => group.Count(), StringComparer.Ordinal);
			var outputNames = new Dictionary<string, string>(StringComparer.Ordinal);
			var usedOutputNames = new HashSet<string>(StringComparer.Ordinal);
			// Allocate names in path order so input reordering cannot change them.
			foreach (CSharpSourceFile source in sources.OrderBy(source => source.Path, StringComparer.Ordinal))
			{
				string stem = OutputStem(source.Path);
				string outputStem = stem
					+ (stemCounts[PortableNameKey(stem)] > 1
						? "." + StablePathSuffix(source.Path) : "");
				string outputName = outputStem + ".api.vs.txt";
				for (int suffix = 1; !usedOutputNames.Add(PortableNameKey(outputName)); ++suffix)
				{
					outputName = outputStem + "." + suffix.ToString(CultureInfo.InvariantCulture) + ".api.vs.txt";
				}
				outputNames.Add(source.Path, outputName);
			}
			foreach (CSharpSourceFile source in sources)
			{
				string outputName = outputNames[source.Path];
				CatalogType[] ownedTypes = typesByOwner[Path.GetFullPath(source.Path)].ToArray();
				apiReferences.Add(new(source.Path, outputName, BuildApiReference(ownedTypes)));
			}
		}

		var manifest = new ProjectManifest(1, "veroscript.csharp-binding-project",
			"merged", layout.CatalogPath,
			bindGenOptions.EmitRegistrationSource ? "merged" : "",
			bindGenOptions.EmitRegistrationSource ? layout.RegistrationPath : "",
			bindGenOptions.EmitAotBridgeSource ? "merged" : "",
			bindGenOptions.EmitAotBridgeSource ? layout.AotBridgePath : "",
			sources.Select(source => source.Path).ToArray(),
			apiReferences.Select(reference => new ProjectApiReference(
				reference.InputPath,
				JoinManifestPath(layout.ApiReferenceDirectory, reference.OutputName))).ToArray());
		string manifestJson = JsonSerializer.Serialize(manifest, JsonOptions) + Environment.NewLine;
		return new(merged.CatalogJson, merged.RegistrationSource,
			apiReferences, manifestJson, diagnostics)
		{
			AotBridgeSource = merged.AotBridgeSource
		};
	}

	private static CatalogType? BuildType(CSharpTypeDeclaration type, CSharpTypeMapOptions mapOptions,
		List<CSharpDiagnostic> diagnostics, CSharpBindGenOptions options)
	{
		var members = new List<CatalogMember>();
		if (type.BaseType is not null)
		{
			Error(diagnostics, "VSCB1009",
						$"Host-to-host base type '{type.BaseType.DisplayName}' on '{type.QualifiedName}' cannot be represented by Binding Catalog v1.",
						FirstLocation(type));
		}

		if (type.Kind == CSharpTypeKind.Enum)
		{
			foreach (CSharpEnumValueDeclaration value in type.EnumValues)
			{
				if (!long.TryParse(value.ConstantValue, NumberStyles.Integer, CultureInfo.InvariantCulture, out long number))
				{
					Error(diagnostics, "VSCB1003", $"Enum value '{value.QualifiedName}' is outside the supported signed 64-bit catalog domain.", value.Location);
					continue;
				}
				members.Add(Member("enumMember", value.Name, value.DocumentationXml,
					"enum", [], [], number, ToScriptName(type.QualifiedName), [], [], "", "", "", [], [], [], false, ""));
			}
		}
		else
		{
			foreach (CSharpEventDeclaration eventDeclaration in type.Events.Where(eventDeclaration =>
				IsPublic(eventDeclaration.Accessibility)
				&& !HasAttribute(eventDeclaration.Attributes, options.IgnoreAttributeName)))
			{
				Error(diagnostics, "VSCB1008",
								$"Event '{eventDeclaration.QualifiedName}' has no Binding Catalog v1 representation.",
								eventDeclaration.Location);
			}

			foreach (CSharpFieldDeclaration field in type.Fields.Where(field => IsPublic(field.Accessibility)
							&& !HasAttribute(field.Attributes, options.IgnoreAttributeName)))
			{
				if (field.Type.RefKind != CSharpRefKind.None)
				{
					Error(diagnostics, "VSCB1011",
						$"Field '{field.QualifiedName}' uses by-reference storage, which Binding Catalog v1 cannot preserve.",
						field.Location);
					continue;
				}
				MappedType? mapped = Map(field.Type, mapOptions, diagnostics, field.Location);
				if (mapped is null)
				{
					continue;
				}

				if (field.IsStatic && !field.IsConst && !field.IsReadOnly)
				{
					Error(diagnostics, "VSCB1004", $"Mutable static field '{field.QualifiedName}' cannot be represented by the binding catalog.", field.Location);
					continue;
				}
				string kind = field.IsStatic ? "staticConstant" : "field";
				members.Add(Member(kind, field.Name, field.DocumentationXml, mapped.NativeType, [], [], 0,
					mapped.HostName, [], [], mapped.ElementName, mapped.KeyName, mapped.ValueName,
					[], [], [], false, ""));
				if (!field.IsStatic && !field.IsConst && !field.IsReadOnly)
				{
					members.Add(Member("propertySetter", field.Name, field.DocumentationXml, "void",
										[mapped.NativeType], ["value"], 0, "", [mapped.HostName], [0], "", "", "",
										[mapped.ElementName], [mapped.KeyName], [mapped.ValueName], false, ""));
				}
			}
			foreach (CSharpPropertyDeclaration property in type.Properties.Where(property => IsPublic(property.Accessibility)
				&& !HasAttribute(property.Attributes, options.IgnoreAttributeName)))
			{
				AddProperty(type, property, mapOptions, diagnostics, members);
			}

			foreach (CSharpCallableDeclaration callable in type.Callables.Where(callable => IsPublic(callable.Accessibility)
							&& !HasAttribute(callable.Attributes, options.IgnoreAttributeName)))
			{
				AddCallable(type, callable, mapOptions, diagnostics, members);
			}
		}
		foreach (IGrouping<(string Kind, string Name), CatalogMember> group in members
			.Where(member => member.Kind is not ("constructor" or "enumMember"))
			.GroupBy(member => (member.Kind, member.Name)))
		{
			if (group.Count() <= 1)
			{
				continue;
			}

			Error(diagnostics, "VSCB1012",
							$"Overloaded member '{type.QualifiedName}.{group.Key.Name}' cannot be represented by Binding Catalog v1; only constructor overloads are supported.",
							FirstLocation(type));
		}
		string kindName = type.Kind switch
		{
			CSharpTypeKind.Enum => "enum",
			CSharpTypeKind.Struct => "struct",
			_ => "class"
		};
		string underlying = type.Kind == CSharpTypeKind.Enum && type.EnumUnderlyingType is not null
			? Map(type.EnumUnderlyingType, mapOptions, diagnostics, FirstLocation(type))?.VeroType ?? "" : "";
		return new(ToScriptName(type.QualifiedName), CSharpDocumentation.Parse(type.DocumentationXml).Summary,
			[], kindName,
			false, type.Kind == CSharpTypeKind.Enum,
			underlying, "", [], false, false, false, false, members);
	}

	private static void AddCallable(CSharpTypeDeclaration owner, CSharpCallableDeclaration callable,
		CSharpTypeMapOptions mapOptions, List<CSharpDiagnostic> diagnostics, List<CatalogMember> members)
	{
		if (owner.IsAbstract && callable.Kind == CSharpCallableKind.Constructor) return;
		if (callable.ReturnType.RefKind != CSharpRefKind.None)
		{
			Error(diagnostics, "VSCB1010",
				$"Callable '{callable.QualifiedName}' returns by reference, which Binding Catalog v1 cannot preserve.",
				callable.Location);
			return;
		}
		if (callable.TypeParameters.Count != 0 || callable.IsAsync || callable.Kind is CSharpCallableKind.Operator or CSharpCallableKind.Conversion)
		{
			Error(diagnostics, "VSCB1005", $"Callable '{callable.QualifiedName}' uses unsupported generic, async, or operator semantics.", callable.Location);
			return;
		}
		MappedType? result = callable.Kind == CSharpCallableKind.Constructor
			? new("object", ToScriptName(owner.QualifiedName), "", "", "", ToScriptName(owner.QualifiedName))
			: Map(callable.ReturnType, mapOptions, diagnostics, callable.Location);
		if (result is null) return;
		if (!TryMapParameters(callable.Parameters, mapOptions, diagnostics, out ParameterMap parameters)) return;
		string kind = callable.Kind == CSharpCallableKind.Constructor ? "constructor"
			: callable.IsStatic ? "staticMethod" : "method";
		string name = callable.Kind == CSharpCallableKind.Constructor ? owner.Name : callable.Name;
		members.Add(Member(kind, name, callable.DocumentationXml, result.NativeType,
			parameters.NativeTypes, callable.Parameters.Select(parameter => parameter.Name).ToArray(),
			0, result.HostName, parameters.HostNames, parameters.Flags,
			result.ElementName, result.KeyName, result.ValueName, parameters.ElementNames, parameters.KeyNames,
			parameters.ValueNames, false, callable.Kind == CSharpCallableKind.Constructor
				? CSharpAotBridgeGenerator.ConstructorNativeName(owner, callable.Parameters) : ""));
	}

	private static void AddProperty(CSharpTypeDeclaration owner, CSharpPropertyDeclaration property,
		CSharpTypeMapOptions mapOptions, List<CSharpDiagnostic> diagnostics, List<CatalogMember> members)
	{
		if (property.IsIndexer)
		{
			Error(diagnostics, "VSCB1017",
				$"Indexer '{property.QualifiedName}' cannot be represented by Binding Catalog v1 until host-index lowering is available.",
				property.Location);
			return;
		}
		if (property.Type.RefKind != CSharpRefKind.None)
		{
			Error(diagnostics, "VSCB1010",
				$"Property '{property.QualifiedName}' returns by reference, which Binding Catalog v1 cannot preserve.",
				property.Location);
			return;
		}
		if (property.IsStatic && (!property.HasGetter
			|| property.HasSetter && IsPublic(property.SetterAccessibility)
			|| property.GetterAccessibility != CSharpAccessibility.Public))
		{
			Error(diagnostics, "VSCB1007",
				$"Static property '{property.QualifiedName}' has no faithful Binding Catalog v1 member kind.",
				property.Location);
			return;
		}
		MappedType? value = Map(property.Type, mapOptions, diagnostics, property.Location);
		if (value is null || !TryMapParameters(property.Parameters, mapOptions, diagnostics, out ParameterMap index)) return;
		if (property.HasGetter && IsPublic(property.GetterAccessibility))
		{
			members.Add(Member(property.IsStatic ? "staticConstant" : "propertyGetter",
						property.Name, property.DocumentationXml, value.NativeType,
						index.NativeTypes, property.Parameters.Select(parameter => parameter.Name).ToArray(),
						0, value.HostName, index.HostNames, index.Flags, value.ElementName,
						value.KeyName, value.ValueName, index.ElementNames, index.KeyNames, index.ValueNames, false, ""));
		}

		if (!property.IsStatic && property.HasSetter && !property.IsInitOnly
					&& IsPublic(property.SetterAccessibility))
		{
			var native = index.NativeTypes.Append(value.NativeType).ToArray();
			var hosts = index.HostNames.Append(value.HostName).ToArray();
			var flags = index.Flags.Append(0).ToArray();
			members.Add(Member("propertySetter", property.Name, property.DocumentationXml, "void",
				native, property.Parameters.Select(parameter => parameter.Name).Append("value").ToArray(),
				0, "", hosts, flags, "", "", "", index.ElementNames.Append(value.ElementName).ToArray(),
				index.KeyNames.Append(value.KeyName).ToArray(), index.ValueNames.Append(value.ValueName).ToArray(),
				false, ""));
		}
	}

	private static bool TryMapParameters(IReadOnlyList<CSharpParameterDeclaration> source,
		CSharpTypeMapOptions options, List<CSharpDiagnostic> diagnostics, out ParameterMap result)
	{
		var mapped = new List<MappedType>();
		var flags = new List<int>();
		foreach (CSharpParameterDeclaration parameter in source)
		{
			if (parameter.HasDefaultValue || parameter.IsParams)
			{
				Error(diagnostics, "VSCB1006", $"Parameter '{parameter.Name}' uses default or params semantics that the catalog cannot preserve.", parameter.Location);
				result = default!;
				return false;
			}
			MappedType? type = Map(parameter.Type, options, diagnostics, parameter.Location);
			if (type is null)
			{
				result = default!;
				return false;
			}
			mapped.Add(type);
			flags.Add(parameter.Type.RefKind == CSharpRefKind.None ? 0
				: parameter.Type.RefKind is CSharpRefKind.In or CSharpRefKind.RefReadOnly ? 3 : 1);
		}
		result = new(mapped.Select(x => x.NativeType).ToArray(), mapped.Select(x => x.HostName).ToArray(),
			mapped.Select(x => x.ElementName).ToArray(), mapped.Select(x => x.KeyName).ToArray(),
			mapped.Select(x => x.ValueName).ToArray(), flags);
		return true;
	}

	private static MappedType? Map(CSharpTypeReference type, CSharpTypeMapOptions options,
		List<CSharpDiagnostic> diagnostics, CSharpSourceLocation location)
	{
		CSharpTypeReference? undeclaredEnum = FindUndeclaredEnum(type, options);
		if (undeclaredEnum is not null)
		{
			Error(diagnostics, "VSCB1013",
				$"Enum type '{undeclaredEnum.QualifiedName}' must be included in the binding inputs so its values and underlying type are present in the catalog.",
				location);
			return null;
		}
		CSharpTypeReference? unsupportedKey = FindUnsupportedContainerKey(type, options);
		if (unsupportedKey is not null)
		{
			Error(diagnostics, "VSCB1016",
				$"Container key type '{unsupportedKey.DisplayName}' is not supported by the VeroScript binding contract; use sbyte, int, uint, long, ulong, string, or a declared enum.",
				location);
			return null;
		}
		CSharpTypeMapResult mapped = CSharpTypeMapper.MapTypeToVeroScript(type, options);
		if (!mapped.Success)
		{
			Error(diagnostics, mapped.ErrorCode, mapped.ErrorMessage, location);
			return null;
		}
		string value = mapped.VeroType;
		string native = value.Contains('<', StringComparison.Ordinal) ? value[..value.IndexOf('<')] : value;
		if (type.Kind == CSharpTypeKind.Enum)
		{
			native = "enum";
		}
		else if (options.KnownHostTypes.Contains(type.QualifiedName, StringComparer.Ordinal))
		{
			native = "object";
		}

		native = native switch
		{
			"int8" => "char",
			"uint8" => "unsigned char",
			"int16" => "short",
			"uint16" => "unsigned short",
			_ => native
		};
		string host = type.Kind == CSharpTypeKind.Enum
			|| options.KnownHostTypes.Contains(type.QualifiedName, StringComparer.Ordinal)
			? ToScriptName(type.QualifiedName) : "";
		string element = type.TypeArguments.Count == 1 ? CSharpTypeMapper.MapTypeToVeroScript(type.TypeArguments[0], options).VeroType : "";
		string key = type.TypeArguments.Count == 2 ? CSharpTypeMapper.MapTypeToVeroScript(type.TypeArguments[0], options).VeroType : "";
		string val = type.TypeArguments.Count == 2 ? CSharpTypeMapper.MapTypeToVeroScript(type.TypeArguments[1], options).VeroType : "";
		return new(native, host, element, key, val, value);
	}

	private static CSharpTypeReference? FindUndeclaredEnum(CSharpTypeReference type,
		CSharpTypeMapOptions options)
	{
		if (type.Kind == CSharpTypeKind.Enum
			&& !options.KnownEnumTypes.Contains(type.QualifiedName, StringComparer.Ordinal))
		{
			return type;
		}

		foreach (CSharpTypeReference argument in type.TypeArguments)
		{
			CSharpTypeReference? undeclared = FindUndeclaredEnum(argument, options);
			if (undeclared is not null) return undeclared;
		}
		return null;
	}

	private static CSharpTypeReference? FindUnsupportedContainerKey(CSharpTypeReference type,
		CSharpTypeMapOptions options)
	{
		string genericBase = GenericBase(type);
		if (genericBase is "System.Collections.Generic.Dictionary"
			or "System.Collections.Generic.SortedDictionary")
		{
			if (type.TypeArguments.Count == 2 && !IsSupportedContainerKey(type.TypeArguments[0], options))
			{
				return type.TypeArguments[0];
			}
		}
		else if (genericBase == "System.Collections.Generic.HashSet")
		{
			if (type.TypeArguments.Count == 1 && !IsSupportedContainerKey(type.TypeArguments[0], options))
			{
				return type.TypeArguments[0];
			}
		}
		foreach (CSharpTypeReference argument in type.TypeArguments)
		{
			CSharpTypeReference? unsupported = FindUnsupportedContainerKey(argument, options);
			if (unsupported is not null) return unsupported;
		}
		return null;
	}

	private static bool IsSupportedContainerKey(CSharpTypeReference type, CSharpTypeMapOptions options)
		=> !type.IsNullable && type.RefKind == CSharpRefKind.None
			&& (type.DisplayName is "sbyte" or "int" or "uint" or "long" or "ulong" or "string"
				|| type.Kind == CSharpTypeKind.Enum
					&& options.KnownEnumTypes.Contains(type.QualifiedName, StringComparer.Ordinal));

	private static string GenericBase(CSharpTypeReference type)
	{
		int open = type.QualifiedName.IndexOf('<', StringComparison.Ordinal);
		return open < 0 ? type.QualifiedName : type.QualifiedName[..open];
	}

	private static CatalogMember Member(string kind, string name, string documentation, string returnType,
		IReadOnlyList<string> paramTypes, IReadOnlyList<string> paramNames, long enumValue,
		string returnHost, IReadOnlyList<string> paramHosts,
		IReadOnlyList<int> flags, string returnElement, string returnKey, string returnValue,
		IReadOnlyList<string> paramElements,
		IReadOnlyList<string> paramKeys, IReadOnlyList<string> paramValues, bool op, string nativeName)
	{
		CSharpDocumentation parsed = CSharpDocumentation.Parse(documentation);
		return new(kind, name, parsed.Summary, [], [], paramNames,
			paramNames.Select(parameter => parsed.Parameters.GetValueOrDefault(parameter, "")).ToArray(),
			parsed.Returns, returnType, paramTypes, enumValue,
			enumValue.ToString(CultureInfo.InvariantCulture), returnHost, paramHosts, returnElement,
			paramElements, returnKey, returnValue, paramKeys, paramValues, flags, op, false, false, nativeName);
	}

	private static bool IsExported(CSharpTypeDeclaration type, CSharpBindGenOptions options)
		=> type.IsEffectivelyPublic && !HasAttribute(type.Attributes, options.IgnoreAttributeName)
			&& (!options.RequireBindingAttribute || HasAttribute(type.Attributes, options.BindingAttributeName));
	private static bool IsPublic(CSharpAccessibility value) => value == CSharpAccessibility.Public;
	private static bool HasAttribute(IReadOnlyList<CSharpAttribute> attributes, string name)
		=> attributes.Any(attribute => attribute.TypeName == name || attribute.TypeName.EndsWith("." + name, StringComparison.Ordinal));
	private static string ToScriptName(string name) => name.Replace(".", "::", StringComparison.Ordinal);

	private static StringComparer PathComparer { get; } = OperatingSystem.IsWindows()
		? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal;

	private static string OutputStem(string path)
	{
		string stem = Path.GetFileNameWithoutExtension(path);
		return string.IsNullOrWhiteSpace(stem) ? "bindings" : stem;
	}

	private static string PortableNameKey(string value)
		=> value.Normalize(NormalizationForm.FormC).ToUpperInvariant();

	private static string StablePathSuffix(string path)
	{
		const ulong offset = 14695981039346656037UL;
		const ulong prime = 1099511628211UL;
		ulong hash = offset;
		foreach (byte value in Encoding.UTF8.GetBytes(path.Replace('\\', '/')))
		{
			hash ^= value;
			hash *= prime;
		}
		return hash.ToString("x16", CultureInfo.InvariantCulture);
	}

	private static string JoinManifestPath(string directory, string name)
		=> string.IsNullOrWhiteSpace(directory) ? name
			: directory.TrimEnd('/', '\\') + "/" + name;

	private static CSharpSourceLocation FirstLocation(CSharpTypeDeclaration type)
		=> type.Declarations.Count == 0 ? CSharpSourceLocation.None : type.Declarations[0];
	private static void Error(List<CSharpDiagnostic> diagnostics, string code, string message, CSharpSourceLocation location)
		=> diagnostics.Add(new(CSharpDiagnosticSeverity.Error, code, message, location));

	private static string BuildApiReference(IReadOnlyList<CatalogType> types)
	{
		var text = new StringBuilder("// Generated C# binding API reference.\n// Load the JSON catalog for compilation; this is not an implementation.\n\n");
		foreach (CatalogType type in types)
		{
			text.Append(type.Kind).Append(' ').Append(type.Name).AppendLine(" {");
			foreach (CatalogMember member in type.Members)
			{
				text.Append("  ").Append(member.Kind).Append(' ').Append(member.ReturnType).Append(' ')
								.Append(member.Name).Append('(')
								.Append(string.Join(", ", member.ParamTypes.Select((parameter, index) => parameter
									+ (index < member.ParamNames.Count ? " " + member.ParamNames[index] : ""))))
								.AppendLine(");");
			}

			text.AppendLine("}\n");
		}
		return text.ToString();
	}

	private sealed record MappedType(string NativeType, string HostName, string ElementName, string KeyName, string ValueName, string VeroType);
	private sealed record ParameterMap(IReadOnlyList<string> NativeTypes, IReadOnlyList<string> HostNames,
		IReadOnlyList<string> ElementNames, IReadOnlyList<string> KeyNames, IReadOnlyList<string> ValueNames, IReadOnlyList<int> Flags);
	private sealed record Catalog(int Version, IReadOnlyList<object> Functions, IReadOnlyList<object> GenericTypeTemplates,
		IReadOnlyList<CatalogType> Types, IReadOnlyList<object> TypeAliases, IReadOnlyList<object> Annotations);
	private sealed record CatalogType(string Name, string Documentation, IReadOnlyList<object> Annotations,
		string Kind, bool Inheritable, bool ScopedEnum, string EnumUnderlyingType, string GenericTemplateName,
		IReadOnlyList<string> GenericTypeArguments, bool HasClone, bool HasEquals, bool HasHash, bool HasSubclass,
		IReadOnlyList<CatalogMember> Members);
	private sealed record CatalogMember(string Kind, string Name, string Documentation, IReadOnlyList<object> Annotations,
		IReadOnlyList<object> ParamAnnotations, IReadOnlyList<string> ParamNames, IReadOnlyList<string> ParamDocumentation,
		string ReturnDocumentation, string ReturnType, IReadOnlyList<string> ParamTypes, long EnumValue, string EnumValueInt64,
		string ReturnHostTypeName, IReadOnlyList<string> ParamHostTypeNames, string ReturnElementTypeName,
		IReadOnlyList<string> ParamElementTypeNames, string ReturnKeyTypeName, string ReturnValueTypeName,
		IReadOnlyList<string> ParamKeyTypeNames, IReadOnlyList<string> ParamValueTypeNames, IReadOnlyList<int> ParamFlags,
		bool Operator, bool ConstMethod, bool MutableThis, string NativeName);
	private sealed record ProjectApiReference(string Input, string Path);
	private sealed record ProjectManifest(int Version, string Kind, string CatalogRole,
		string Catalog, string RegistrationRole, string Registration,
		string AotBridgeRole, string AotBridge, IReadOnlyList<string> Inputs,
		IReadOnlyList<ProjectApiReference> ApiReferences);
}
