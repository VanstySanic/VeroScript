// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Reflection;
using System.Text;
using System.Text.Json;
using VeroScriptInstaller.Core;

namespace VeroScriptInstaller.Localization;

internal sealed class InstallerLocalizer
{
	private const string FallbackCulture = "en-US";
	private static readonly Lazy<InstallerLocalizer> LazyInstance = new(() => new InstallerLocalizer());
	private static string? _configuredExternalLocalizationRoot;
	private readonly Dictionary<string, CultureCatalog> _resources = new(StringComparer.OrdinalIgnoreCase);

	internal static InstallerLocalizer Instance => LazyInstance.Value;
	internal string Culture { get; private set; } = FallbackCulture;
	internal event Action? Changed;

	internal static void ConfigureExternalLocalizationRoot(string? root)
	{
		if (LazyInstance.IsValueCreated)
		{
			throw new InvalidOperationException(
				"The installer localization root must be configured before localization is used.");
		}
		_configuredExternalLocalizationRoot = string.IsNullOrWhiteSpace(root)
			? null
			: Path.GetFullPath(root.Trim());
	}

	private InstallerLocalizer()
	{
		var cultures = DiscoverEmbeddedCultures();
		if (!cultures.Contains(FallbackCulture, StringComparer.Ordinal))
		{
			throw new InvalidDataException(
				$"Missing installer localization culture directory '{FallbackCulture}'.");
		}
		foreach (var culture in cultures)
		{
			_resources.Add(culture, LoadCulture(culture));
		}

		var builtInEnglish = _resources[FallbackCulture];
		foreach (var culture in _resources)
		{
			ValidateTranslationCatalog(culture.Key, builtInEnglish, culture.Value, requireComplete: true);
			ValidateProductResources(culture.Key, culture.Value);
		}
		foreach (var root in EnumerateExternalLocalizationRoots().Distinct(PathComparer))
		{
			var overlay = TryLoadCatalogDirectory(root);
			if (overlay == null)
			{
				continue;
			}
			ValidateOverlayCatalog(root, builtInEnglish, overlay);
			MergeCatalogs(overlay);
		}

		var english = _resources[FallbackCulture];
		foreach (var culture in _resources)
		{
			if (!string.Equals(culture.Key, FallbackCulture, StringComparison.OrdinalIgnoreCase))
			{
				ValidateTranslationCatalog(culture.Key, english, culture.Value);
			}
		}
		SetCulture(CultureInfo.CurrentUICulture.Name);
	}

	internal IReadOnlyList<string> SupportedCultures => _resources.Keys
		.OrderBy(tag => tag == FallbackCulture ? 0 : 1)
		.ThenBy(tag => tag, StringComparer.Ordinal)
		.ToArray();

	internal string GetCultureDisplayName(string culture)
	{
		try
		{
			return CultureInfo.GetCultureInfo(culture).NativeName;
		}
		catch (CultureNotFoundException)
		{
			return culture;
		}
	}

	internal string this[string key]
	{
		get
		{
			if (_resources.TryGetValue(Culture, out var current)
				&& current.Entries.TryGetValue(key, out var value))
			{
				return value;
			}
			return _resources[FallbackCulture].Entries.TryGetValue(key, out var fallback)
				? fallback
				: key;
		}
	}

	internal void SetCulture(string? culture)
	{
		var requestedCulture = culture ?? CultureInfo.CurrentUICulture.Name;
		var normalized = ResolveAvailableCulture(requestedCulture, _resources.Keys);
		if (Culture == normalized)
		{
			return;
		}
		Culture = normalized;
		Changed?.Invoke();
	}

	private static IReadOnlyList<string> DiscoverEmbeddedCultures()
	{
		const string marker = ".Resources.Localization.";
		var cultures = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
		foreach (var name in Assembly.GetExecutingAssembly().GetManifestResourceNames())
		{
			var markerIndex = name.IndexOf(marker, StringComparison.OrdinalIgnoreCase);
			if (markerIndex < 0 || !name.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
			{
				continue;
			}
			var suffix = name[(markerIndex + marker.Length)..];
			var separator = suffix.IndexOf('.');
			if (separator <= 0
				|| !TryCanonicalizeCulture(suffix[..separator].Replace('_', '-'), out var canonical)
				|| !canonical.Contains('-'))
			{
				throw new InvalidDataException($"Invalid embedded installer localization resource: {name}");
			}
			cultures.Add(canonical);
		}
		return cultures.OrderBy(culture => culture, StringComparer.Ordinal).ToArray();
	}

	private static bool TryCanonicalizeCulture(string? culture, out string canonical)
	{
		canonical = string.Empty;
		if (string.IsNullOrWhiteSpace(culture))
		{
			return false;
		}
		var normalized = culture.Trim().Replace('_', '-');
		var suffixIndex = normalized.IndexOfAny(['.', '@']);
		if (suffixIndex >= 0)
		{
			normalized = normalized[..suffixIndex];
		}
		var parts = normalized.Split('-', StringSplitOptions.None);
		if (parts.Length == 0 || parts.Any(part => part.Length == 0
			|| part.Any(ch => !char.IsAsciiLetterOrDigit(ch)))
			|| parts[0].Length is < 2 or > 8
			|| parts[0].Any(ch => !char.IsAsciiLetter(ch)))
		{
			return false;
		}
		for (var index = 0; index < parts.Length; ++index)
		{
			var part = parts[index].ToLowerInvariant();
			if (index != 0 && part.Length == 4 && part.All(char.IsAsciiLetter))
			{
				part = char.ToUpperInvariant(part[0]) + part[1..];
			}
			else if (index != 0 && part.Length == 2 && part.All(char.IsAsciiLetter))
			{
				part = part.ToUpperInvariant();
			}
			parts[index] = part;
		}
		canonical = string.Join('-', parts);
		return true;
	}

	private static string ResolveAvailableCulture(string? requested, IEnumerable<string> availableCultures)
	{
		// Keep candidate filtering and scoring aligned with
		// VeroScriptLocale.cpp::ResolveLanguageTag.
		var available = availableCultures.ToHashSet(StringComparer.OrdinalIgnoreCase);
		if (string.Equals(requested?.Trim(), "C", StringComparison.OrdinalIgnoreCase)
			|| string.Equals(requested?.Trim(), "POSIX", StringComparison.OrdinalIgnoreCase))
		{
			return FallbackCulture;
		}
		if (!TryCanonicalizeCulture(requested, out var canonical))
		{
			return FallbackCulture;
		}
		if (available.TryGetValue(canonical, out var exact))
		{
			return exact;
		}
		for (var parent = canonical; parent.Contains('-');)
		{
			parent = parent[..parent.LastIndexOf('-')];
			if (available.TryGetValue(parent, out var parentMatch))
			{
				return parentMatch;
			}
		}
		var language = PrimaryLanguage(canonical);
		var requestedChineseScript = language == "zh" ? ChineseScript(canonical) : null;
		return available
			.Where(candidate => PrimaryLanguage(candidate) == language)
			.Select(candidate => new
			{
				Tag = candidate,
				Script = language == "zh" ? ChineseScript(candidate) : null,
			})
			.Where(candidate => requestedChineseScript == null
				|| candidate.Script == requestedChineseScript)
			.OrderByDescending(candidate =>
				(requestedChineseScript != null && candidate.Script == requestedChineseScript ? 100 : 0)
				+ (canonical.StartsWith(candidate.Tag, StringComparison.Ordinal)
					|| candidate.Tag.StartsWith(canonical, StringComparison.Ordinal) ? 10 : 0))
			.ThenBy(candidate => candidate.Tag, StringComparer.Ordinal)
			.Select(candidate => candidate.Tag)
			.FirstOrDefault() ?? FallbackCulture;
	}

	private static string PrimaryLanguage(string tag)
	{
		var separator = tag.IndexOf('-');
		return separator < 0 ? tag : tag[..separator];
	}

	private static string? ChineseScript(string tag)
	{
		string? inferred = null;
		foreach (var part in tag.Split('-').Skip(1))
		{
			if (part.Length == 1)
			{
				// Extensions and private use are not script/region.
				break;
			}
			if (part is "Hant" or "Hans") return part; // Explicit script wins over region.
			inferred ??= part switch
			{
				"TW" or "HK" or "MO" => "Hant",
				"CN" or "SG" => "Hans",
				_ => null,
			};
		}
		return inferred;
	}

	private static IEnumerable<string> EnumerateExternalLocalizationRoots()
	{
		yield return Path.Combine(AppContext.BaseDirectory, "localization");
		if (_configuredExternalLocalizationRoot != null)
		{
			yield return _configuredExternalLocalizationRoot;
		}
	}

	private static IReadOnlyDictionary<string, CultureCatalog>? TryLoadCatalogDirectory(string root)
	{
		if (!Directory.Exists(root))
		{
			return null;
		}

		var cultures = new Dictionary<string, CultureCatalog>(StringComparer.OrdinalIgnoreCase);
		foreach (var directory in Directory.EnumerateDirectories(root)
			.OrderBy(path => path, StringComparer.Ordinal))
		{
			var name = Path.GetFileName(directory);
			if (!TryCanonicalizeCulture(name, out var canonical)
				|| canonical != name
				|| !canonical.Contains('-'))
			{
				throw new InvalidDataException(
					$"Installer localization culture directory must use a qualified canonical BCP 47 tag: {name}");
			}

			var files = Directory.EnumerateFiles(directory, "*.json", SearchOption.AllDirectories)
				.OrderBy(path => path, StringComparer.Ordinal)
				.ToArray();
			if (files.Length == 0)
			{
				throw new InvalidDataException(
					$"Installer localization culture directory contains no JSON files: {directory}");
			}

			var entries = new Dictionary<string, string>(StringComparer.Ordinal);
			var keysByRelativePath = new Dictionary<string, IReadOnlySet<string>>(StringComparer.Ordinal);
			foreach (var file in files)
			{
				var relativePath = Path.GetRelativePath(directory, file).Replace('\\', '/');
				using var stream = File.OpenRead(file);
				var fileEntries = ParseCatalogFile(stream, relativePath);
				if (!keysByRelativePath.TryAdd(
						relativePath,
						fileEntries.Keys.ToHashSet(StringComparer.Ordinal)))
				{
					throw new InvalidDataException(
						$"Duplicate {canonical} installer localization file '{relativePath}'.");
				}
				foreach (var entry in fileEntries)
				{
					if (!entries.TryAdd(entry.Key, entry.Value))
					{
						throw new InvalidDataException(
							$"Duplicate {canonical} installer localization key '{entry.Key}' " +
							$"in '{relativePath}'.");
					}
				}
			}
			if (!cultures.TryAdd(canonical, new CultureCatalog(entries, keysByRelativePath)))
			{
				throw new InvalidDataException(
					$"Duplicate installer localization culture directory: {canonical}");
			}
		}
		return cultures.Count == 0 ? null : cultures;
	}

	private static void ValidateOverlayCatalog(
		string root,
		CultureCatalog builtInEnglish,
		IReadOnlyDictionary<string, CultureCatalog> overlay)
	{
		foreach (var culture in overlay)
		{
			foreach (var file in culture.Value.KeysByRelativePath)
			{
				if (!builtInEnglish.KeysByRelativePath.TryGetValue(file.Key, out var englishKeys))
				{
					throw new InvalidDataException(
						$"{culture.Key} installer localization file has no built-in en-US counterpart " +
						$"in '{root}': {file.Key}");
				}
				foreach (var key in file.Value)
				{
					if (!englishKeys.Contains(key))
					{
						throw new InvalidDataException(
							$"{culture.Key} installer localization key '{key}' has no built-in en-US entry " +
							$"in '{root}/{culture.Key}/{file.Key}'.");
					}
					ValidateFormatContract(key, builtInEnglish.Entries[key], culture.Value.Entries[key]);
				}
			}
		}
	}

	private void MergeCatalogs(IReadOnlyDictionary<string, CultureCatalog> overlay)
	{
		foreach (var culture in overlay)
		{
			if (!_resources.TryGetValue(culture.Key, out var current))
			{
				_resources.Add(culture.Key, culture.Value);
				continue;
			}

			var entries = current.Entries.ToDictionary(
				entry => entry.Key, entry => entry.Value, StringComparer.Ordinal);
			foreach (var entry in culture.Value.Entries)
			{
				entries[entry.Key] = entry.Value;
			}
			var keysByFile = current.KeysByRelativePath.ToDictionary(
				entry => entry.Key,
				entry => (IReadOnlySet<string>)entry.Value.ToHashSet(StringComparer.Ordinal),
				StringComparer.Ordinal);
			foreach (var file in culture.Value.KeysByRelativePath)
			{
				keysByFile[file.Key] = keysByFile.TryGetValue(file.Key, out var existing)
					? existing.Concat(file.Value).ToHashSet(StringComparer.Ordinal)
					: file.Value;
			}
			_resources[culture.Key] = new CultureCatalog(entries, keysByFile);
		}
	}

	private static StringComparer PathComparer => OperatingSystem.IsWindows()
		? StringComparer.OrdinalIgnoreCase
		: StringComparer.Ordinal;

	private static CultureCatalog LoadCulture(string culture)
	{
		var assembly = Assembly.GetExecutingAssembly();
		// MSBuild converts '-' to '_' while deriving manifest resource names
		// from region-tagged directories (en-US -> en_US). Public culture tags
		// and on-disk directories remain canonical BCP 47 names.
		var manifestCulture = culture.Replace('-', '_');
		var marker = $".Resources.Localization.{manifestCulture}.";
		var resources = assembly.GetManifestResourceNames()
			.Where(name => name.Contains(marker, StringComparison.OrdinalIgnoreCase)
				&& name.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
			.Select(name => new
			{
				ResourceName = name,
				RelativePath = name[(name.IndexOf(marker, StringComparison.OrdinalIgnoreCase) + marker.Length)..],
			})
			.OrderBy(resource => resource.RelativePath, StringComparer.Ordinal)
			.ToArray();
		if (resources.Length == 0)
		{
			throw new InvalidOperationException(
				$"Missing installer localization culture directory '{culture}'.");
		}

		var entries = new Dictionary<string, string>(StringComparer.Ordinal);
		var keysByRelativePath = new Dictionary<string, IReadOnlySet<string>>(StringComparer.Ordinal);
		foreach (var resource in resources)
		{
			using var stream = assembly.GetManifestResourceStream(resource.ResourceName)
				?? throw new InvalidOperationException(
					$"Cannot open installer localization resource '{resource.ResourceName}'.");
			var fileEntries = ParseCatalogFile(stream, resource.RelativePath);
			if (!keysByRelativePath.TryAdd(
					resource.RelativePath,
					fileEntries.Keys.ToHashSet(StringComparer.Ordinal)))
			{
				throw new InvalidDataException(
					$"Duplicate {culture} installer localization file '{resource.RelativePath}'.");
			}
			foreach (var entry in fileEntries)
			{
				if (!entries.TryAdd(entry.Key, entry.Value))
				{
					throw new InvalidDataException(
						$"Duplicate {culture} installer localization key '{entry.Key}' in '{resource.RelativePath}'.");
				}
			}
		}
		return new CultureCatalog(entries, keysByRelativePath);
	}

	private static IReadOnlyDictionary<string, string> ParseCatalogFile(
		Stream stream,
		string relativePath)
	{
		using var document = JsonDocument.Parse(stream);
		if (document.RootElement.ValueKind != JsonValueKind.Object)
		{
			throw new InvalidDataException(
				$"Installer localization resource '{relativePath}' must contain a JSON object.");
		}

		var entries = new Dictionary<string, string>(StringComparer.Ordinal);
		foreach (var property in document.RootElement.EnumerateObject())
		{
			if (property.Value.ValueKind != JsonValueKind.String)
			{
				throw new InvalidDataException(
					$"Installer localization value for '{property.Name}' must be a string in '{relativePath}'.");
			}
			ValidateFormatContract(property.Name, property.Value.GetString()!, property.Value.GetString()!);
			if (!entries.TryAdd(property.Name, property.Value.GetString()!))
			{
				throw new InvalidDataException(
					$"Duplicate installer localization key '{property.Name}' in '{relativePath}'.");
			}
		}
		return entries;
	}

	private static void ValidateTranslationCatalog(
		string culture,
		CultureCatalog english,
		CultureCatalog translation,
		bool requireComplete = false)
	{
		if (requireComplete && !english.KeysByRelativePath.Keys.ToHashSet(StringComparer.Ordinal)
			.SetEquals(translation.KeysByRelativePath.Keys))
		{
			throw new InvalidDataException($"Incomplete built-in installer localization files: {culture}");
		}

		foreach (var file in translation.KeysByRelativePath)
		{
			if (!english.KeysByRelativePath.TryGetValue(file.Key, out var englishKeys))
			{
				throw new InvalidDataException(
					$"{culture} installer localization file has no en-US counterpart: {file.Key}");
			}
			if (requireComplete && !englishKeys.SetEquals(file.Value))
			{
				throw new InvalidDataException($"Incomplete built-in installer localization: {culture}/{file.Key}");
			}

			foreach (var key in file.Value)
			{
				if (!englishKeys.Contains(key))
				{
					throw new InvalidDataException(
						$"{culture} installer localization key '{key}' has no en-US entry in '{file.Key}'.");
				}
				ValidateFormatContract(key, english.Entries[key], translation.Entries[key]);
			}
		}
	}

	internal static void ValidateFormatContract(string key, string english, string translation)
	{
		if (!FormatArguments(english).SetEquals(FormatArguments(translation)))
		{
			throw new InvalidDataException($"Installer localization placeholders do not match: {key}");
		}
	}

	private static HashSet<int> FormatArguments(string value)
	{
		if (string.IsNullOrWhiteSpace(value))
		{
			throw new InvalidDataException("Installer localization values must not be empty.");
		}

		try
		{
			_ = CompositeFormat.Parse(value);
			var arguments = new HashSet<int>();
			for (var index = 0; index < value.Length; index++)
			{
				if (value[index] != '{')
				{
					continue;
				}

				if (value[index + 1] == '{')
				{
					index++;
					continue;
				}
				var start = ++index;
				while (index < value.Length && char.IsAsciiDigit(value[index]))
				{
					index++;
				}

				arguments.Add(int.Parse(value.AsSpan(start, index - start), CultureInfo.InvariantCulture));
			}
			return arguments;
		}
		catch (FormatException exception)
		{
			throw new InvalidDataException("Invalid installer localization format string.", exception);
		}
	}

	private static void ValidateProductResources(string culture, CultureCatalog catalog)
	{
		foreach (var product in InstallerProductCatalog.All)
		{
			Require(product.DescriptionResourceKey, product.ProfileId);
			foreach (var component in product.Components.Values)
			{
				Require(component.DisplayNameResourceKey, component.Id);
				Require(component.DescriptionResourceKey, component.Id);
			}
		}
		void Require(string? key, string owner)
		{
			if (key is null || !catalog.Entries.TryGetValue(key, out var value)
				|| string.IsNullOrWhiteSpace(value))
			{
				throw new InvalidDataException($"Missing {culture} localization for {owner}: {key}");
			}
			// Product/component descriptions and labels are plain text, not format templates.
			if (FormatArguments(value).Count != 0)
			{
				throw new InvalidDataException($"Unexpected placeholders in {culture}/{key}.");
			}
		}
	}

	internal static void VerifyBuiltInCatalogs()
	{
		var cultures = DiscoverEmbeddedCultures();
		foreach (var required in new[] { "en-US", "fr-FR", "ja-JP", "ko-KR", "zh-CN", "zh-TW" })
		{
			if (!cultures.Contains(required, StringComparer.Ordinal))
			{
				throw new InvalidDataException($"Missing built-in installer culture: {required}");
			}
		}

		var english = LoadCulture(FallbackCulture);
		foreach (var culture in cultures)
		{
			var catalog = LoadCulture(culture);
			ValidateTranslationCatalog(culture, english, catalog, requireComplete: true);
			ValidateProductResources(culture, catalog);
		}

		// Partial overlays remain supported, but incomplete built-in translations must fail.
		var incomplete = new CultureCatalog(
			english.Entries.Where(entry => entry.Key != "Title").ToDictionary(),
			english.KeysByRelativePath.ToDictionary(
				file => file.Key,
				file => (IReadOnlySet<string>)file.Value.Where(key => key != "Title").ToHashSet()));
		ValidateTranslationCatalog("test", english, incomplete);
		try
		{
			ValidateTranslationCatalog("test", english, incomplete, requireComplete: true);
		}
		catch (InvalidDataException)
		{
			return;
		}
		throw new InvalidDataException("Incomplete built-in catalog validation failed.");
	}

	private sealed record CultureCatalog(
		IReadOnlyDictionary<string, string> Entries,
		IReadOnlyDictionary<string, IReadOnlySet<string>> KeysByRelativePath);
}
