// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Globalization;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using RoslynParseOptions = Microsoft.CodeAnalysis.CSharp.CSharpParseOptions;

namespace VeroScript.CSharpFrontend;

public static class CSharpFrontend
{
	public static CSharpParseResult ParseSource(
		string source,
		string sourcePath = "<csharp-source>",
		CSharpParseOptions? options = null)
	{
		ArgumentNullException.ThrowIfNull(source);
		return ParseSources([new(sourcePath, source)], options);
	}

	public static CSharpParseResult ParseFile(
		string filePath,
		CSharpParseOptions? options = null)
	{
		ArgumentException.ThrowIfNullOrWhiteSpace(filePath);
		return ParseSources([new(filePath, File.ReadAllText(filePath))], options);
	}

	public static CSharpParseResult ParseFiles(
		IReadOnlyList<string> filePaths,
		CSharpParseOptions? options = null)
	{
		ArgumentNullException.ThrowIfNull(filePaths);
		var sources = new List<CSharpSourceFile>(filePaths.Count);
		var diagnostics = new List<CSharpDiagnostic>();
		var paths = new HashSet<string>(PathComparer);
		for (int i = 0; i < filePaths.Count; ++i)
		{
			string path = filePaths[i]
				?? throw new ArgumentException("File paths cannot contain null.", nameof(filePaths));
			ArgumentException.ThrowIfNullOrWhiteSpace(path);
			if (!paths.Add(Path.GetFullPath(path)))
			{
				diagnostics.Add(new(CSharpDiagnosticSeverity.Error, "VSCF0002",
					$"Duplicate C# source path '{path}'.", CSharpSourceLocation.None));
				continue;
			}
			sources.Add(new(path, File.ReadAllText(path)));
		}
		CSharpParseResult parsed = ParseSources(sources, options);
		return parsed with
		{
			Diagnostics = diagnostics.Concat(parsed.Diagnostics).ToArray()
		};
	}

	public static CSharpParseResult ParseSources(
		IReadOnlyList<CSharpSourceFile> sources,
		CSharpParseOptions? options = null)
	{
		ArgumentNullException.ThrowIfNull(sources);
		options ??= new CSharpParseOptions();
		var diagnostics = new List<CSharpDiagnostic>();
		if (!LanguageVersionFacts.TryParse(options.LanguageVersion, out LanguageVersion languageVersion))
		{
			diagnostics.Add(new(CSharpDiagnosticSeverity.Error, "VSCF0001",
				$"Unknown C# language version '{options.LanguageVersion}'.",
				CSharpSourceLocation.None));
			return new(new([], []), diagnostics);
		}

		var parseOptions = new RoslynParseOptions(
			languageVersion,
			DocumentationMode.Parse,
			SourceCodeKind.Regular,
			options.PreprocessorSymbols);
		var trees = new SyntaxTree[sources.Count];
		var paths = new HashSet<string>(PathComparer);
		for (int i = 0; i < trees.Length; ++i)
		{
			CSharpSourceFile file = sources[i]
				?? throw new ArgumentException("Sources cannot contain null.", nameof(sources));
			string path = string.IsNullOrWhiteSpace(file.Path) ? $"<source-{i}>" : file.Path;
			if (!paths.Add(path))
			{
				diagnostics.Add(new(CSharpDiagnosticSeverity.Error, "VSCF0002",
					$"Duplicate C# source path '{path}'.", CSharpSourceLocation.None));
			}
			trees[i] = CSharpSyntaxTree.ParseText(file.Text, parseOptions, path);
		}

		ImmutableArray<MetadataReference> references = CreateReferences(options, diagnostics);
		var compilation = CSharpCompilation.Create(
			"VeroScript.CSharpFrontend.Analysis",
			trees,
			references,
			new CSharpCompilationOptions(
				OutputKind.DynamicallyLinkedLibrary,
				allowUnsafe: options.AllowUnsafe,
				nullableContextOptions: NullableContextOptions.Enable,
				deterministic: true,
				warningLevel: 9999,
				generalDiagnosticOption: options.TreatWarningsAsErrors
					? ReportDiagnostic.Error
					: ReportDiagnostic.Default));

		diagnostics.AddRange(compilation.GetDiagnostics()
			.Where(diagnostic => diagnostic.Severity is DiagnosticSeverity.Info
				or DiagnosticSeverity.Warning or DiagnosticSeverity.Error)
			.Select(ToDiagnostic));

		var builders = new Dictionary<string, TypeBuilder>(StringComparer.Ordinal);
		foreach (SyntaxTree tree in trees)
		{
			SemanticModel model = compilation.GetSemanticModel(tree, ignoreAccessibility: true);
			CompilationUnitSyntax root = tree.GetCompilationUnitRoot();
			foreach (BaseTypeDeclarationSyntax declaration in root.DescendantNodes()
				.OfType<BaseTypeDeclarationSyntax>())
			{
				if (model.GetDeclaredSymbol(declaration) is not INamedTypeSymbol symbol)
				{
					continue;
				}

				string qualifiedName = GetQualifiedName(symbol);
				if (!builders.TryGetValue(qualifiedName, out TypeBuilder? builder))
				{
					builder = new(symbol);
					builders.Add(qualifiedName, builder);
				}
				builder.AddDeclaration(declaration, model);
			}

			foreach (DelegateDeclarationSyntax declaration in root.DescendantNodes()
				.OfType<DelegateDeclarationSyntax>())
			{
				if (model.GetDeclaredSymbol(declaration) is not INamedTypeSymbol symbol)
				{
					continue;
				}

				string qualifiedName = GetQualifiedName(symbol);
				if (!builders.TryGetValue(qualifiedName, out TypeBuilder? builder))
				{
					builder = new(symbol);
					builders.Add(qualifiedName, builder);
				}
				builder.AddDelegate(declaration, model);
			}
		}

		CSharpTypeDeclaration[] types = builders.Values
			.OrderBy(builder => builder.QualifiedName, StringComparer.Ordinal)
			.Select(builder => builder.Build())
			.ToArray();
		return new(new(sources.Select(source => source.Path).ToArray(), types), diagnostics);
	}

	private static ImmutableArray<MetadataReference> CreateReferences(
		CSharpParseOptions options,
		List<CSharpDiagnostic> diagnostics)
	{
		var paths = new HashSet<string>(PathComparer);
		if (AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") is string platformAssemblies)
		{
			foreach (string path in platformAssemblies.Split(Path.PathSeparator,
				StringSplitOptions.RemoveEmptyEntries))
			{
				paths.Add(path);
			}
		}
		foreach (string path in options.ReferencePaths)
		{
			paths.Add(Path.GetFullPath(path));
		}

		var references = ImmutableArray.CreateBuilder<MetadataReference>();
		foreach (string path in paths.Order(PathComparer))
		{
			try
			{
				references.Add(MetadataReference.CreateFromFile(path));
			}
			catch (Exception exception) when (exception is IOException
				or UnauthorizedAccessException or BadImageFormatException)
			{
				diagnostics.Add(new(CSharpDiagnosticSeverity.Error, "VSCF0003",
					$"Cannot load metadata reference '{path}': {exception.Message}",
					CSharpSourceLocation.None));
			}
		}
		return references.ToImmutable();
	}

	private static CSharpDiagnostic ToDiagnostic(Diagnostic diagnostic)
		=> new(diagnostic.Severity switch
		{
			DiagnosticSeverity.Error => CSharpDiagnosticSeverity.Error,
			DiagnosticSeverity.Warning => CSharpDiagnosticSeverity.Warning,
			_ => CSharpDiagnosticSeverity.Info
		}, diagnostic.Id, diagnostic.GetMessage(CultureInfo.InvariantCulture),
			ToLocation(diagnostic.Location));

	internal static CSharpSourceLocation ToLocation(Location? location)
	{
		if (location is null || location == Location.None || !location.IsInSource)
		{
			return CSharpSourceLocation.None;
		}

		FileLinePositionSpan line = location.GetLineSpan();
		return new(line.Path, line.StartLinePosition.Line + 1,
			line.StartLinePosition.Character + 1,
			location.SourceSpan.Start, location.SourceSpan.Length);
	}

	private static StringComparer PathComparer { get; } = OperatingSystem.IsWindows()
		? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal;

	internal static string GetQualifiedName(ISymbol symbol)
		=> symbol.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat)
			.Replace("global::", "", StringComparison.Ordinal);

	internal static CSharpAccessibility ToAccessibility(Accessibility accessibility)
		=> accessibility switch
		{
			Accessibility.Private => CSharpAccessibility.Private,
			Accessibility.ProtectedAndInternal => CSharpAccessibility.ProtectedAndInternal,
			Accessibility.Protected => CSharpAccessibility.Protected,
			Accessibility.Internal => CSharpAccessibility.Internal,
			Accessibility.ProtectedOrInternal => CSharpAccessibility.ProtectedOrInternal,
			Accessibility.Public => CSharpAccessibility.Public,
			_ => CSharpAccessibility.NotApplicable
		};

	internal static CSharpRefKind ToRefKind(RefKind refKind)
		=> refKind switch
		{
			RefKind.Ref => CSharpRefKind.Ref,
			RefKind.Out => CSharpRefKind.Out,
			RefKind.In => CSharpRefKind.In,
			RefKind.RefReadOnlyParameter => CSharpRefKind.RefReadOnly,
			_ => CSharpRefKind.None
		};

	private static CSharpRefKind ToReturnRefKind(RefKind refKind)
		=> refKind switch
		{
			RefKind.Ref => CSharpRefKind.Ref,
			// Roslyn aliases RefReadOnly to the same enum value as In. Return
			// position disambiguates it from an `in` parameter.
			RefKind.In => CSharpRefKind.RefReadOnly,
			_ => CSharpRefKind.None
		};

	internal static CSharpTypeReference ToType(
		ITypeSymbol type,
		CSharpRefKind refKind = CSharpRefKind.None,
		Location? location = null)
	{
		CSharpTypeKind kind = type switch
		{
			IDynamicTypeSymbol => CSharpTypeKind.Dynamic,
			IArrayTypeSymbol => CSharpTypeKind.Array,
			IPointerTypeSymbol => CSharpTypeKind.Pointer,
			ITypeParameterSymbol => CSharpTypeKind.TypeParameter,
			IErrorTypeSymbol => CSharpTypeKind.Error,
			INamedTypeSymbol named when named.TypeKind == TypeKind.Class => CSharpTypeKind.Class,
			INamedTypeSymbol named when named.TypeKind == TypeKind.Struct => CSharpTypeKind.Struct,
			INamedTypeSymbol named when named.TypeKind == TypeKind.Interface => CSharpTypeKind.Interface,
			INamedTypeSymbol named when named.TypeKind == TypeKind.Enum => CSharpTypeKind.Enum,
			INamedTypeSymbol named when named.TypeKind == TypeKind.Delegate => CSharpTypeKind.Delegate,
			_ => CSharpTypeKind.Unknown
		};
		IReadOnlyList<CSharpTypeReference> arguments = type switch
		{
			IArrayTypeSymbol array => [ToType(array.ElementType)],
			IPointerTypeSymbol pointer => [ToType(pointer.PointedAtType)],
			INamedTypeSymbol named => named.TypeArguments.Select(argument => ToType(argument)).ToArray(),
			_ => []
		};
		int rank = type is IArrayTypeSymbol arrayType ? arrayType.Rank : 0;
		string qualified = type is IArrayTypeSymbol or IPointerTypeSymbol
			? type.ToDisplayString(SymbolDisplayFormat.CSharpErrorMessageFormat)
			: GetQualifiedName(type);
		return new(type.ToDisplayString(SymbolDisplayFormat.CSharpErrorMessageFormat),
			qualified, kind, type.NullableAnnotation == NullableAnnotation.Annotated,
			rank, refKind, arguments, ToLocation(location));
	}

	internal static IReadOnlyList<CSharpAttribute> ToAttributes(ISymbol symbol)
		=> symbol.GetAttributes().Select(attribute => new CSharpAttribute(
			attribute.AttributeClass is null ? "<unknown>" : GetQualifiedName(attribute.AttributeClass),
			attribute.ConstructorArguments.Select(FormatTypedConstant).ToArray(),
			attribute.NamedArguments.ToDictionary(pair => pair.Key,
				pair => FormatTypedConstant(pair.Value), StringComparer.Ordinal),
			ToLocation(attribute.ApplicationSyntaxReference?.GetSyntax().GetLocation())))
			.ToArray();

	private static string FormatTypedConstant(TypedConstant value)
	{
		if (value.IsNull) return "null";
		if (value.Kind == TypedConstantKind.Array)
		{
			return "[" + string.Join(", ", value.Values.Select(FormatTypedConstant)) + "]";
		}

		return Convert.ToString(value.Value, CultureInfo.InvariantCulture) ?? "";
	}

	private sealed class TypeBuilder(INamedTypeSymbol symbol)
	{
		private readonly List<CSharpSourceLocation> _declarations = [];
		private readonly List<CSharpFieldDeclaration> _fields = [];
		private readonly List<CSharpPropertyDeclaration> _properties = [];
		private readonly HashSet<string> _propertyNames = new(StringComparer.Ordinal);
		private readonly List<CSharpEventDeclaration> _events = [];
		private readonly List<CSharpCallableDeclaration> _callables = [];
		private readonly HashSet<string> _callableNames = new(StringComparer.Ordinal);
		private readonly List<CSharpEnumValueDeclaration> _enumValues = [];
		private bool _isPartial;

		internal string QualifiedName { get; } = GetQualifiedName(symbol);

		internal void AddDelegate(DelegateDeclarationSyntax declaration, SemanticModel model)
		{
			_declarations.Add(ToLocation(declaration.GetLocation()));
			if (model.GetDeclaredSymbol(declaration) is INamedTypeSymbol delegateSymbol
				&& delegateSymbol.DelegateInvokeMethod is IMethodSymbol invoke)
			{
				AddCallable(ToCallable(invoke, declaration, CSharpCallableKind.Method));
			}
		}

		internal void AddDeclaration(BaseTypeDeclarationSyntax declaration, SemanticModel model)
		{
			_declarations.Add(ToLocation(declaration.GetLocation()));
			_isPartial |= declaration.Modifiers.Any(SyntaxKind.PartialKeyword);
			if (declaration is EnumDeclarationSyntax enumDeclaration)
			{
				foreach (EnumMemberDeclarationSyntax member in enumDeclaration.Members)
				{
					if (model.GetDeclaredSymbol(member) is IFieldSymbol field)
					{
						_enumValues.Add(new(field.Name, GetQualifiedName(field),
							FormatConstant(field.ConstantValue), field.GetDocumentationCommentXml() ?? "",
							ToAttributes(field), ToLocation(member.GetLocation())));
					}
				}
				return;
			}
			if (declaration is not TypeDeclarationSyntax typeDeclaration) return;
			if (typeDeclaration is RecordDeclarationSyntax { ParameterList: not null } recordDeclaration)
			{
				foreach (ParameterSyntax parameter in recordDeclaration.ParameterList.Parameters)
				{
					IPropertySymbol? property = symbol.GetMembers(parameter.Identifier.ValueText)
						.OfType<IPropertySymbol>()
						.FirstOrDefault();
					if (property is not null)
					{
						AddProperty(ToProperty(property, parameter.GetLocation()));
					}
				}
			}
			foreach (MemberDeclarationSyntax member in typeDeclaration.Members)
			{
				switch (member)
				{
					case FieldDeclarationSyntax fieldDeclaration:
						{
							AddFields(fieldDeclaration, model);
							break;
						}

					case PropertyDeclarationSyntax propertyDeclaration:
						{
							if (model.GetDeclaredSymbol(propertyDeclaration) is IPropertySymbol property)
							{
								AddProperty(ToProperty(property, propertyDeclaration.GetLocation()));
							}

							break;
						}

					case IndexerDeclarationSyntax indexerDeclaration:
						{
							if (model.GetDeclaredSymbol(indexerDeclaration) is IPropertySymbol indexer)
							{
								AddProperty(ToProperty(indexer, indexerDeclaration.GetLocation()));
							}

							break;
						}

					case MethodDeclarationSyntax methodDeclaration:
						{
							if (model.GetDeclaredSymbol(methodDeclaration) is IMethodSymbol method)
							{
								AddCallable(ToCallable(method, methodDeclaration, CSharpCallableKind.Method));
							}

							break;
						}

					case ConstructorDeclarationSyntax constructorDeclaration:
						{
							if (model.GetDeclaredSymbol(constructorDeclaration) is IMethodSymbol constructor)
							{
								AddCallable(ToCallable(constructor, constructorDeclaration,
															CSharpCallableKind.Constructor));
							}

							break;
						}

					case OperatorDeclarationSyntax operatorDeclaration:
						{
							if (model.GetDeclaredSymbol(operatorDeclaration) is IMethodSymbol op)
							{
								AddCallable(ToCallable(op, operatorDeclaration, CSharpCallableKind.Operator));
							}

							break;
						}

					case ConversionOperatorDeclarationSyntax conversionDeclaration:
						{
							if (model.GetDeclaredSymbol(conversionDeclaration) is IMethodSymbol conversion)
							{
								AddCallable(ToCallable(conversion, conversionDeclaration,
															CSharpCallableKind.Conversion));
							}

							break;
						}

					case EventFieldDeclarationSyntax eventFieldDeclaration:
						{
							AddEvents(eventFieldDeclaration, model);
							break;
						}

					case EventDeclarationSyntax eventDeclaration:
						{
							if (model.GetDeclaredSymbol(eventDeclaration) is IEventSymbol eventSymbol)
							{
								_events.Add(ToEvent(eventSymbol, eventDeclaration.GetLocation()));
							}

							break;
						}

					default:
						{
							break;
						}
				}
			}
		}

		private void AddFields(FieldDeclarationSyntax declaration, SemanticModel model)
		{
			foreach (VariableDeclaratorSyntax variable in declaration.Declaration.Variables)
			{
				if (model.GetDeclaredSymbol(variable) is not IFieldSymbol field)
				{
					continue;
				}

				_fields.Add(new(field.Name, GetQualifiedName(field),
									ToType(field.Type, ToReturnRefKind(field.RefKind), variable.GetLocation()),
									ToAccessibility(field.DeclaredAccessibility), field.IsStatic,
									field.IsReadOnly, field.IsConst, FormatConstant(field.ConstantValue),
									variable.Initializer?.Value.ToString() ?? "", field.GetDocumentationCommentXml() ?? "",
									ToAttributes(field), ToLocation(variable.GetLocation()))
				{
					IsRequired = field.IsRequired
				});
			}
		}

		private void AddEvents(EventFieldDeclarationSyntax declaration, SemanticModel model)
		{
			foreach (VariableDeclaratorSyntax variable in declaration.Declaration.Variables)
			{
				if (model.GetDeclaredSymbol(variable) is IEventSymbol eventSymbol)
				{
					_events.Add(ToEvent(eventSymbol, variable.GetLocation()));
				}
			}
		}

		private void AddProperty(CSharpPropertyDeclaration property)
		{
			if (_propertyNames.Add(property.QualifiedName))
			{
				_properties.Add(property);
			}
		}

		private void AddCallable(CSharpCallableDeclaration callable)
		{
			// A partial method's definition and implementation are two syntax nodes for
			// one callable symbol. Keep the stable declaration model free of duplicates.
			if (_callableNames.Add(CallableIdentity(callable)))
			{
				_callables.Add(callable);
			}
		}

		private static string CallableIdentity(CSharpCallableDeclaration callable)
			=> callable.QualifiedName + "|" + callable.Kind + "|" + callable.TypeParameters.Count
				+ "|" + string.Join(";", callable.Parameters.Select(parameter =>
					parameter.Type.RefKind + ":" + parameter.Type.QualifiedName));

		internal CSharpTypeDeclaration Build()
		{
			if (symbol.TypeKind is TypeKind.Class or TypeKind.Struct)
			{
				foreach (IMethodSymbol constructor in symbol.InstanceConstructors)
				{
					string qualifiedName = GetQualifiedName(constructor);
					Location? location = constructor.Locations.FirstOrDefault(candidate => candidate.IsInSource);
					AddCallable(new(constructor.Name, qualifiedName, CSharpCallableKind.Constructor,
						ToType(constructor.ReturnType, location: location),
						constructor.Parameters.Select(ToParameter).ToArray(), [],
						ToAccessibility(constructor.DeclaredAccessibility), false, false, false,
						false, false, false, false, "", "", ToAttributes(constructor), ToLocation(location)));
				}
			}
			CSharpTypeKind kind = symbol.TypeKind switch
			{
				TypeKind.Class => CSharpTypeKind.Class,
				TypeKind.Struct => CSharpTypeKind.Struct,
				TypeKind.Interface => CSharpTypeKind.Interface,
				TypeKind.Enum => CSharpTypeKind.Enum,
				TypeKind.Delegate => CSharpTypeKind.Delegate,
				_ => CSharpTypeKind.Unknown
			};
			CSharpTypeReference? baseType = symbol.BaseType is null
				|| symbol.BaseType.SpecialType == SpecialType.System_Object
				|| symbol.TypeKind is TypeKind.Struct or TypeKind.Enum or TypeKind.Delegate
				? null : ToType(symbol.BaseType);
			CSharpTypeReference? enumUnderlying = symbol.EnumUnderlyingType is null
				? null : ToType(symbol.EnumUnderlyingType);
			return new(symbol.Name, QualifiedName, kind,
				ToAccessibility(symbol.DeclaredAccessibility), IsEffectivelyPublic(symbol), symbol.IsStatic,
				symbol.IsAbstract, symbol.IsSealed, _isPartial,
				baseType, symbol.Interfaces.Select(type => ToType(type)).ToArray(),
				symbol.TypeParameters.Select(type => type.Name).ToArray(),
				_fields, _properties, _events, _callables, enumUnderlying, _enumValues,
				symbol.GetDocumentationCommentXml() ?? "", ToAttributes(symbol), _declarations)
			{
				IsRefLike = symbol.IsRefLikeType
			};
		}

		private static CSharpPropertyDeclaration ToProperty(
			IPropertySymbol property, Location location)
			=> new(property.Name, GetQualifiedName(property),
				ToType(property.Type, ToReturnRefKind(property.RefKind), location),
				ToAccessibility(property.DeclaredAccessibility), property.IsStatic,
				property.GetMethod is null ? CSharpAccessibility.NotApplicable
					: ToAccessibility(property.GetMethod.DeclaredAccessibility),
				property.SetMethod is null ? CSharpAccessibility.NotApplicable
					: ToAccessibility(property.SetMethod.DeclaredAccessibility),
				property.GetMethod is not null, property.SetMethod is not null,
				property.SetMethod?.IsInitOnly == true, property.IsIndexer,
				property.Parameters.Select(ToParameter).ToArray(),
				property.GetDocumentationCommentXml() ?? "", ToAttributes(property),
				ToLocation(location))
			{
				IsRequired = property.IsRequired
			};

		private static bool IsEffectivelyPublic(INamedTypeSymbol type)
		{
			for (INamedTypeSymbol? current = type; current is not null; current = current.ContainingType)
			{
				if (current.DeclaredAccessibility != Accessibility.Public) return false;
			}

			return true;
		}

		private static CSharpEventDeclaration ToEvent(IEventSymbol eventSymbol, Location location)
			=> new(eventSymbol.Name, GetQualifiedName(eventSymbol),
				ToType(eventSymbol.Type, location: location),
				ToAccessibility(eventSymbol.DeclaredAccessibility), eventSymbol.IsStatic,
				eventSymbol.GetDocumentationCommentXml() ?? "", ToAttributes(eventSymbol),
				ToLocation(location));

		private static CSharpCallableDeclaration ToCallable(
			IMethodSymbol method, SyntaxNode declaration, CSharpCallableKind kind)
		{
			string body = declaration switch
			{
				BaseMethodDeclarationSyntax methodSyntax =>
					methodSyntax.Body?.ToFullString()
					?? methodSyntax.ExpressionBody?.Expression.ToFullString() ?? "",
				DelegateDeclarationSyntax => "",
				_ => ""
			};
			return new(method.Name, GetQualifiedName(method), kind,
				ToType(method.ReturnType, ToReturnRefKind(method.RefKind), declaration.GetLocation()),
				method.Parameters.Select(ToParameter).ToArray(),
				method.TypeParameters.Select(type => type.Name).ToArray(),
				ToAccessibility(method.DeclaredAccessibility), method.IsStatic,
				method.IsAbstract, method.IsVirtual, method.IsOverride, method.IsSealed,
				method.IsExtern, method.IsAsync, body, method.GetDocumentationCommentXml() ?? "",
				ToAttributes(method), ToLocation(declaration.GetLocation()));
		}

		private static CSharpParameterDeclaration ToParameter(IParameterSymbol parameter)
			=> new(parameter.Name,
				ToType(parameter.Type, ToRefKind(parameter.RefKind), parameter.Locations.FirstOrDefault()),
				parameter.HasExplicitDefaultValue,
				parameter.HasExplicitDefaultValue ? FormatConstant(parameter.ExplicitDefaultValue) : "",
				parameter.IsParams, ToAttributes(parameter),
				ToLocation(parameter.Locations.FirstOrDefault()));

		private static string FormatConstant(object? value)
		{
			if (value is null) return "null";
			return value switch
			{
				bool boolean => boolean ? "true" : "false",
				char character => character.ToString(),
				string text => text,
				_ => Convert.ToString(value, CultureInfo.InvariantCulture) ?? ""
			};
		}
	}
}
