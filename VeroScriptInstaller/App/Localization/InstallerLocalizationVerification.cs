// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScriptInstaller.Core;

namespace VeroScriptInstaller.Localization;

internal static class InstallerLocalizationVerification
{
	internal static void Run(InstallerLocalizer localizer)
	{
		InstallerLocalizer.VerifyBuiltInCatalogs();
		foreach (var invalid in new[] { "", " ", "{", "{1}", "no argument", "{0} {1}", "{0:{" })
		{
			var rejected = false;
			try
			{
				InstallerLocalizer.ValidateFormatContract("test", "{0}", invalid);
			}
			catch (InvalidDataException)
			{
				rejected = true;
			}
			Require(rejected, "Invalid format contract was accepted: " + invalid);
		}
		InstallerLocalizer.ValidateFormatContract("test", "{0} / {1}", "{1} / {0}");
		InstallerLocalizer.ValidateFormatContract("test", "{{escaped}} {0}", "{0} {{escaped}}");
		InstallerLocalizer.ValidateFormatContract("test", "{{{0}}}", "{0}");

		var originalCulture = localizer.Culture;
		try
		{
			foreach (var (requested, expected) in new[]
			{
				("zh-Hans-TW", "zh-CN"), ("zh-Hant-CN", "zh-TW"),
				("zh-SG-x-Hant", "zh-CN"), ("zh-MO-x-Hans", "zh-TW"),
			})
			{
				localizer.SetCulture(requested);
				Require(localizer.Culture == expected,
					$"Installer Chinese script resolution failed: {requested} -> {localizer.Culture}, expected {expected}.");
			}
			var messages = new[]
			{
				new InstallerStatus("Success", "1.2.3"),
				new InstallerStatus("UpdateSuccess", "1.2.3"),
				new InstallerStatus("RepairSuccess", "1.2.3"),
				new InstallerStatus("UninstallSuccess"),
				new InstallerStatus("Copying", new InstallerStatus("cpp-runtime.Name"), 1, 2),
			};
			var failures = Enum.GetValues<InstallerError>()
				.Select(code => (Exception)new InvalidOperationException("technical-only").WithInstallerError(code))
				.Concat(new Exception[]
				{
					new UnauthorizedAccessException("technical-only"),
					new FileNotFoundException("technical-only"),
					new DirectoryNotFoundException("technical-only"),
					new InvalidDataException("technical-only"),
					new System.Text.Json.JsonException("technical-only"),
					new PlatformNotSupportedException("technical-only"),
					new IOException("technical-only"),
					new ArgumentException("technical-only"),
					new Exception("technical-only"),
					new IOException("technical-only", unchecked((int)0x80070020)),
					new IOException("technical-only", unchecked((int)0x80070070)),
				})
				.Select(exception => InstallerStatus.Failure(exception, "diagnostic.log")).ToArray();
			localizer.SetCulture("en-US");
			var englishMessages = messages.Concat(failures).Select(message => message.Render(localizer)).ToArray();
			foreach (var culture in new[] { "fr-FR", "ja-JP", "ko-KR", "zh-CN", "zh-TW" })
			{
				localizer.SetCulture(culture);
				var index = 0;
				foreach (var message in messages.Concat(failures))
				{
					var translated = message.Render(localizer);
					Require(translated != englishMessages[index++], "Status did not switch language: " + culture);
					Require(!translated.Contains("technical-only", StringComparison.Ordinal),
						"Primary failure leaked raw technical text.");
				}
				foreach (var code in Enum.GetValues<InstallerError>())
				{
					Require(localizer["Failure" + code] != "Failure" + code, "Missing failure translation.");
				}

				Require(messages[4].Render(localizer).Contains(localizer["cpp-runtime.Name"], StringComparison.Ordinal),
									"Progress did not use the localized component name.");
				Require(messages[0].Render(localizer).Contains("1.2.3", StringComparison.Ordinal),
					"Result lost its version.");
				var noLog = InstallerStatus.Failure(new IOException("technical-only"), null).Render(localizer);
				Require(noLog.Contains("technical-only", StringComparison.Ordinal),
					"Technical details were lost when diagnostics could not be written.");
			}
		}
		finally
		{
			localizer.SetCulture(originalCulture);
		}
	}

	private static void Require(bool condition, string message)
	{
		if (!condition)
		{
			throw new InvalidDataException(message);
		}
	}
}
