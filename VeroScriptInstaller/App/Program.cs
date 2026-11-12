// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using Avalonia;
using VeroScriptInstaller.Core;
using VeroScriptInstaller.Localization;

namespace VeroScriptInstaller;

internal static class Program
{
	internal static InstallerLaunchOptions LaunchOptions { get; private set; } = new();

	[STAThread]
	public static void Main(string[] args)
	{
		if (args.SequenceEqual(["--license"], StringComparer.Ordinal))
		{
			Console.Write(ProjectLicense.Read());
			return;
		}
		if (args.SequenceEqual(["--third-party-licenses"], StringComparer.Ordinal))
		{
			Console.OutputEncoding = new System.Text.UTF8Encoding(false);
			Console.Write(ProjectLicense.ReadThirdPartyNotices());
			return;
		}
		if (args.Length >= 1 && args[0] == "--verify-localization")
		{
			// Validate all shipped cultures and product/component resources before
			// overlays can mask an incomplete embedded catalog.
			InstallerLocalizer.VerifyBuiltInCatalogs();
			string? temporaryRoot = null;
			var verifySyntheticExtension = args.Length == 1;
			try
			{
				string extensionRoot;
				if (args.Length == 1)
				{
					temporaryRoot = Path.Combine(
						Path.GetTempPath(),
						"veroscript-installer-localization-" + Guid.NewGuid().ToString("N"));
					var cultureRoot = Path.Combine(temporaryRoot, "fr-FR");
					Directory.CreateDirectory(cultureRoot);
					File.WriteAllText(
						Path.Combine(cultureRoot, "ui.json"),
						"{\"Title\":\"Programme d’installation VeroScript\"}");
					foreach (var culture in new[] { "pt-BR", "pt-PT-x-pack1" })
					{
						var root = Path.Combine(temporaryRoot, culture);
						Directory.CreateDirectory(root);
						File.WriteAllText(
							Path.Combine(root, "ui.json"),
							"{\"Title\":\"Instalação do VeroScript\"}");
					}
					var chineseRoot = Path.Combine(temporaryRoot, "zh-CN");
					Directory.CreateDirectory(chineseRoot);
					File.WriteAllText(
						Path.Combine(chineseRoot, "ui.json"),
						"{\"Title\":\"VeroScript 扩展安装\"}");
					var englishRoot = Path.Combine(temporaryRoot, "en-US");
					Directory.CreateDirectory(englishRoot);
					File.WriteAllText(
						Path.Combine(englishRoot, "ui.json"),
						"{\"Heading\":\"Install VeroScript Extended\"}");
					extensionRoot = temporaryRoot;
				}
				else if (args.Length == 3 && args[1] == "--localization")
				{
					extensionRoot = args[2];
				}
				else
				{
					throw new ArgumentException(
						"Usage: --verify-localization [--localization <directory>]");
				}

				var verificationOptions = InstallerLaunchOptions.Parse(
					["--localization", extensionRoot]);
				InstallerLocalizer.ConfigureExternalLocalizationRoot(
					verificationOptions.LocalizationRoot);
				var localizer = InstallerLocalizer.Instance;
				InstallerLocalizationVerification.Run(localizer);
				if (!localizer.SupportedCultures.Contains("en-US", StringComparer.Ordinal)
					|| (verifySyntheticExtension
						&& !localizer.SupportedCultures.Contains("fr-FR", StringComparer.Ordinal)))
				{
					throw new InvalidDataException(
						"Installer localization catalog is missing the fallback or external culture.");
				}

				foreach (var culture in localizer.SupportedCultures)
				{
					localizer.SetCulture(culture);
					if (!string.Equals(localizer.Culture, culture, StringComparison.Ordinal)
						|| localizer["Title"] == "Title")
					{
						throw new InvalidDataException(
							$"Installer localization culture '{culture}' cannot be selected or is missing Title.");
					}
				}
				localizer.SetCulture("zh_Hans.UTF-8");
				if (localizer.Culture != "zh-CN"
					|| (verifySyntheticExtension
						&& localizer["Title"] != "VeroScript 扩展安装"))
				{
					throw new InvalidDataException(
						"Installer failed to resolve Simplified Chinese through BCP 47 fallback.");
				}
				localizer.SetCulture("zh-Hant-HK");
				if (localizer.Culture != "zh-TW")
				{
					throw new InvalidDataException(
						"Installer failed to resolve Traditional Chinese through BCP 47 fallback.");
				}
				if (verifySyntheticExtension)
				{
					localizer.SetCulture("pt-PT");
					if (localizer.Culture != "pt-PT-x-pack1")
					{
						throw new InvalidDataException(
							"Installer failed to prefer the closest BCP 47 language-pack candidate.");
					}
				}

				if (verifySyntheticExtension)
				{
					localizer.SetCulture("fr-FR");
					if (localizer["Title"] != "Programme d’installation VeroScript"
						|| localizer["Heading"] != "Installer VeroScript")
					{
						throw new InvalidDataException(
							"Installer external language-pack override replaced an unrelated built-in translation.");
					}

					localizer.SetCulture("pt-BR");
					if (localizer["Heading"] != "Install VeroScript Extended")
					{
						throw new InvalidDataException(
							"Installer partial external language-pack or external en-US fallback failed.");
					}
				}
			}
			finally
			{
				if (temporaryRoot != null && Directory.Exists(temporaryRoot))
				{
					Directory.Delete(temporaryRoot, recursive: true);
				}
			}
			return;
		}

		if (args.SequenceEqual(["--verify-embedded-bundle"], StringComparer.Ordinal))
		{
			using var verifiedBundle = EmbeddedInstallerBundle.Open();
			return;
		}

		EmbeddedInstallerBundle? embeddedBundle = null;
		var usesExternalBundle = args.Contains("--manifest", StringComparer.Ordinal)
			|| args.Contains("--payload", StringComparer.Ordinal);
		var uninstallOnly = args.Contains("--uninstall-record", StringComparer.Ordinal);
		if (!usesExternalBundle && !uninstallOnly && EmbeddedInstallerBundle.IsAvailable)
		{
			embeddedBundle = EmbeddedInstallerBundle.Open();
		}
		try
		{
			LaunchOptions = InstallerLaunchOptions.Parse(args, embeddedBundle?.Root);
			InstallerLocalizer.ConfigureExternalLocalizationRoot(LaunchOptions.LocalizationRoot);
			BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
		}
		finally
		{
			embeddedBundle?.Dispose();
		}
	}

	public static AppBuilder BuildAvaloniaApp() => AppBuilder.Configure<App>()
		.UsePlatformDetect()
		.WithInterFont()
		.LogToTrace();
}

internal sealed class InstallerLaunchOptions
{
	internal string ManifestPath { get; private init; } =
		Path.Combine(AppContext.BaseDirectory, Core.InstallerConstants.ManifestFileName);
	internal string PayloadRoot { get; private init; } = AppContext.BaseDirectory;
	internal string? Language { get; private init; }
	internal string? LocalizationRoot { get; private init; }
	internal string? UninstallRecordPath { get; private init; }
	internal string? RepairRecordPath { get; private init; }

	internal static InstallerLaunchOptions Parse(string[] args, string? embeddedBundleRoot = null)
	{
		string? manifest = null;
		string? payload = null;
		string? language = null;
		string? localization = null;
		string? uninstall = null;
		string? repair = null;
		var seen = new HashSet<string>(StringComparer.Ordinal);
		for (var index = 0; index < args.Length; ++index)
		{
			var argument = args[index];
			if (argument is not ("--manifest" or "--payload" or "--language" or "--localization"
				or "--uninstall-record" or "--repair-record"))
			{
				throw new ArgumentException($"Unknown installer option '{argument}'.");
			}
			if (!seen.Add(argument))
			{
				throw new ArgumentException($"Duplicate installer option '{argument}'.");
			}
			if (++index >= args.Length || args[index].StartsWith("--", StringComparison.Ordinal))
			{
				throw new ArgumentException($"Missing value after {argument}.");
			}
			switch (argument)
			{
				case "--manifest":
					{
						manifest = args[index];
						break;
					}

				case "--payload":
					{
						payload = args[index];
						break;
					}

				case "--language":
					{
						language = args[index];
						break;
					}

				case "--localization":
					{
						localization = args[index];
						break;
					}

				case "--uninstall-record":
					{
						uninstall = args[index];
						break;
					}

				case "--repair-record":
					{
						repair = args[index];
						break;
					}

				default:
					{
						break;
					}
			}
		}
		if (uninstall is not null && repair is not null)
		{
			throw new ArgumentException("Choose either uninstall or repair mode, not both.");
		}
		var defaultRoot = embeddedBundleRoot ?? AppContext.BaseDirectory;
		return new InstallerLaunchOptions
		{
			ManifestPath = Path.GetFullPath(manifest ?? Path.Combine(defaultRoot, Core.InstallerConstants.ManifestFileName)),
			PayloadRoot = Path.GetFullPath(payload ?? defaultRoot),
			Language = language,
			LocalizationRoot = localization is null ? null : Path.GetFullPath(localization),
			UninstallRecordPath = uninstall is null ? null : Path.GetFullPath(uninstall),
			RepairRecordPath = repair is null ? null : Path.GetFullPath(repair),
		};
	}
}
