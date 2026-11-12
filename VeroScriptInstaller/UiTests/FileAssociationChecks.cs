// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using Avalonia.Controls;
using VeroScriptInstaller.Core;
using VeroScriptInstaller.Core.Platform;
using VeroScriptInstaller.Localization;

namespace VeroScriptInstaller.UiTests;

internal static partial class Program
{
	private static async Task GoToAssociations(MainWindow window, string destination)
	{
		await GoToLocation(window);
		await SetLocation(window, destination);
		Click(window, "InstallButton");
		await Pump();
		Require(window.CurrentPage == InstallerWizardPage.Associations, "File association page is missing.");
	}
	private static async Task CheckFileAssociations()
	{
		if (!OperatingSystem.IsWindows())
		{
			Console.WriteLine("SKIP Windows association wizard.");
			return;
		}
		await using var fixture = await Fixture.Create(associations: true);
		var registry = new UiAssociationRegistry();
		var engine = new InstallerEngine(new ControlledPathIntegration(), registry);
		var launches = 0;
		foreach (var culture in Cultures)
		{
			var window = await Open(fixture.Options(culture), engine, _ =>
			{
				launches++;
				return true;
			});
			try
			{
				await GoToAssociations(window, Path.Combine(fixture.Root, "未安装-" + culture));
				foreach (var name in new[] { "AssociateSourceCheckBox", "AssociateApplicationCheckBox", "PackageIconCheckBox", "ChooseDefaultsCheckBox" })
				{
					Require(Control<CheckBox>(window, name).IsChecked != true, "An association is enabled by default.");
				}
				Require(!Control<CheckBox>(window, "ChooseDefaultsCheckBox").IsEnabled, "Defaults are available without a run handler.");
				Control<CheckBox>(window, "PackageIconCheckBox").IsChecked = true;
				await Pump();
				Require(!Control<CheckBox>(window, "ChooseDefaultsCheckBox").IsEnabled, "Package icon enabled default-app assignment.");
				Control<CheckBox>(window, "AssociateSourceCheckBox").IsChecked = true;
				Control<CheckBox>(window, "AssociateApplicationCheckBox").IsChecked = true;
				Control<CheckBox>(window, "ChooseDefaultsCheckBox").IsChecked = true;
				await Pump();
				CheckLayout(window);
				Capture(window, culture + "-associations");
				Click(window, "BackButton");
				await Pump();
				Click(window, "InstallButton");
				await Pump();
				Require(Control<CheckBox>(window, "ChooseDefaultsCheckBox").IsChecked == true, "Back navigation lost consent.");
				Click(window, "InstallButton");
				await Pump();
				Require(window.CurrentPage == InstallerWizardPage.Review && Control<TextBlock>(window, "ReviewAssociationsText").Text!.Contains(".vsapp", StringComparison.Ordinal), "Review omits selected associations.");
				CheckLayout(window);
				Capture(window, culture + "-associations-review");
				Require(launches == 0 && registry.Values.Count == 0, "Navigation registered associations or opened Settings.");
			}
			finally
			{
				window.Close();
			}
		}
		Pass("six-locale-association-page-layout-opt-in-and-review");
		var destination = Path.Combine(fixture.Root, "关联安装 中文");
		var recordPath = Path.Combine(destination, "Runtime", InstallerConstants.InstallRecordFileName);
		var launchSucceeds = false;
		var install = await Open(fixture.Options("zh-CN"), engine, name =>
		{
			Require(File.Exists(recordPath) && registry.Values.Count > 0 && name.StartsWith("VeroScript Runtime (", StringComparison.Ordinal), "Defaults opened before commit.");
			launches++;
			return launchSucceeds;
		});
		try
		{
			await GoToAssociations(install, destination);
			foreach (var name in new[] { "AssociateSourceCheckBox", "AssociateApplicationCheckBox", "PackageIconCheckBox", "ChooseDefaultsCheckBox" })
			{
				Control<CheckBox>(install, name).IsChecked = true;
			}
			Click(install, "InstallButton");
			await Pump();
			Click(install, "InstallButton");
			await Until(() => install.CurrentPage == InstallerWizardPage.Completed);
			Require(launches == 1 && Control<TextBlock>(install, "AssociationNoticeText").Text == InstallerLocalizer.Instance["Associations.DefaultFailed"], "Settings failure was reported as an installation failure or successful default change.");
			CheckLayout(install);
			Capture(install, "zh-CN-default-settings-failed");
			launchSucceeds = true;
			Click(install, "OpenDefaultsButton");
			await Pump();
			Require(launches == 2 && Control<TextBlock>(install, "AssociationNoticeText").Text == InstallerLocalizer.Instance["Associations.DefaultRequested"], "Settings retry or honest status is missing.");
			ChooseLanguage(install, "fr-FR");
			await Pump();
			CheckLayout(install);
			Require(Control<TextBlock>(install, "AssociationNoticeText").Text == InstallerLocalizer.Instance["Associations.DefaultRequested"], "Settings notice did not switch language.");
		}
		finally
		{
			install.Close();
		}
		Pass("defaults-only-after-commit-with-failure-retry-and-localized-status");
		var update = await Open(fixture.Options("en-US"), engine, _ =>
		{
			launches++;
			return true;
		});
		try
		{
			await GoToAssociations(update, destination);
			Require(Control<CheckBox>(update, "AssociateSourceCheckBox").IsChecked == true && Control<CheckBox>(update, "PackageIconCheckBox").IsChecked == true
				&& Control<CheckBox>(update, "ChooseDefaultsCheckBox").IsChecked != true, "Update lost options or reused default consent.");
			Click(update, "BackButton");
			await Pump();
			await SetLocation(update, Path.Combine(fixture.Root, "fresh"));
			Require(Control<CheckBox>(update, "AssociateSourceCheckBox").IsChecked != true && Control<CheckBox>(update, "PackageIconCheckBox").IsChecked != true,
				"A fresh destination inherited automatic opt-ins.");
			Control<CheckBox>(update, "AssociateSourceCheckBox").IsChecked = true;
			Control<CheckBox>(update, "AssociateSourceCheckBox").IsChecked = false;
			await SetLocation(update, destination);
			Click(update, "InstallButton");
			await Pump();
			Require(Control<CheckBox>(update, "AssociateSourceCheckBox").IsChecked == false && Control<CheckBox>(update, "AssociateApplicationCheckBox").IsChecked == true, "An update probe overwrote explicit opt-out.");
			Control<CheckBox>(update, "ChooseDefaultsCheckBox").IsChecked = true;
			Control<CheckBox>(update, "AssociateApplicationCheckBox").IsChecked = false;
			Control<CheckBox>(update, "PackageIconCheckBox").IsChecked = false;
			await Pump();
			Require(Control<CheckBox>(update, "ChooseDefaultsCheckBox").IsChecked == false && !Control<CheckBox>(update, "ChooseDefaultsCheckBox").IsEnabled, "Disabling all handlers retained a defaults request.");
			Click(update, "InstallButton");
			await Pump();
			Click(update, "InstallButton");
			await Until(() => update.CurrentPage == InstallerWizardPage.Completed);
			Require(launches == 2 && registry.Values.Count == 0, "Opt-out opened Settings or retained owned registrations.");
		}
		finally
		{
			update.Close();
		}
		Pass("association-probes-preserve-opt-out-and-reset-fresh-destination");
		var packageOnly = await Open(fixture.Options("en-US"), engine, _ =>
		{
			launches++;
			return true;
		});
		try
		{
			await GoToAssociations(packageOnly, destination);
			Control<CheckBox>(packageOnly, "PackageIconCheckBox").IsChecked = true;
			Click(packageOnly, "InstallButton");
			await Pump();
			Click(packageOnly, "InstallButton");
			await Until(() => packageOnly.CurrentPage == InstallerWizardPage.Completed);
			Require(launches == 2 && registry.Values.Count == 2 && !Control<Button>(packageOnly, "OpenDefaultsButton").IsVisible, "Package icon created a handler or opened Settings.");
		}
		finally
		{
			packageOnly.Close();
		}
		var handlerOnly = await Open(fixture.Options("en-US"), engine, _ =>
		{
			launches++;
			return true;
		});
		try
		{
			await GoToAssociations(handlerOnly, destination);
			Control<CheckBox>(handlerOnly, "AssociateSourceCheckBox").IsChecked = true;
			Require(Control<CheckBox>(handlerOnly, "ChooseDefaultsCheckBox").IsChecked != true, "Choosing a handler also chose defaults.");
			Click(handlerOnly, "InstallButton");
			await Pump();
			Click(handlerOnly, "InstallButton");
			await Until(() => handlerOnly.CurrentPage == InstallerWizardPage.Completed);
			Require(launches == 2 && registry.Values.Count > 2 && !Control<Button>(handlerOnly, "OpenDefaultsButton").IsVisible, "Handler registration requested defaults without consent.");
		}
		finally
		{
			handlerOnly.Close();
		}
		Pass("handler-registration-does-not-imply-default-app-consent");
		var uninstall = await Open(InstallerLaunchOptions.Parse(["--uninstall-record", recordPath, "--language", "zh-CN"]), engine);
		try
		{
			Require(Control<TextBlock>(uninstall, "ReviewAssociationsText").IsVisible && Control<TextBlock>(uninstall, "ReviewAssociationsText").Text == InstallerLocalizer.Instance["Associations.Remove"], "Uninstall omits icon cleanup.");
			Click(uninstall, "InstallButton");
			await Until(() => uninstall.CurrentPage == InstallerWizardPage.Completed);
			Require(registry.Values.Count == 0 && launches == 2, "Uninstall retained icons or opened Settings.");
		}
		finally
		{
			uninstall.Close();
		}
		Pass("package-icon-only-and-uninstall-owned-cleanup");
		await CheckCancellationDuringRegistration(fixture);
	}

	private static async Task CheckCancellationDuringRegistration(Fixture fixture)
	{
		var registry = new UiAssociationRegistry();
		var engine = new InstallerEngine(new ControlledPathIntegration(), registry);
		var destination = Path.Combine(fixture.Root, "cancel-during-registration");
		var window = await Open(fixture.Options("zh-CN"), engine, _ => throw new InvalidOperationException("Cancelled install opened Settings."));
		try
		{
			await GoToAssociations(window, destination);
			Control<CheckBox>(window, "AssociateSourceCheckBox").IsChecked = true;
			Control<CheckBox>(window, "ChooseDefaultsCheckBox").IsChecked = true;
			Click(window, "InstallButton");
			await Pump();
			registry.PauseNextWrite();
			Click(window, "InstallButton");
			await Until(() => registry.Entered);
			Require(window.CurrentPage == InstallerWizardPage.Progress, "Paused registration is not in progress.");
			Click(window, "CancelButton");
			Require(!Control<Button>(window, "CancelButton").IsEnabled && window.CurrentPage == InstallerWizardPage.Progress,
				"Cancellation did not stay pending until registry rollback.");
			registry.ReleaseWrite();
			await Until(() => window.CurrentPage == InstallerWizardPage.Review && Control<Button>(window, "InstallButton").IsEnabled);
			Require(registry.Values.Count == 0 && !File.Exists(Path.Combine(destination, "Runtime", InstallerConstants.InstallRecordFileName)),
				"Registry cancellation returned before removing partial registrations and files.");
			CheckLayout(window);
			Capture(window, "zh-CN-registration-cancelled");
		}
		finally
		{
			registry.ReleaseWrite();
			await Until(() => window.CurrentPage != InstallerWizardPage.Progress);
			window.Close();
		}
		Pass("registry-writes-keep-cancel-responsive-and-roll-back-before-returning");
	}

	private sealed class UiAssociationRegistry : IFileAssociationRegistry
	{
		internal Dictionary<string, AssociationRegistryValue> Values { get; } = new(StringComparer.OrdinalIgnoreCase);
		private readonly HashSet<string> _keys = new(StringComparer.OrdinalIgnoreCase);
		private readonly ManualResetEventSlim _writeGate = new(true);
		private volatile bool _pauseNextWrite;
		internal volatile bool Entered;
		internal void PauseNextWrite()
		{
			Entered = false;
			_writeGate.Reset();
			_pauseNextWrite = true;
		}
		internal void ReleaseWrite() => _writeGate.Set();
		public bool IsSupported => true;
		public bool KeyExists(string key) => _keys.Contains(key);
		public AssociationRegistryValue? Read(string key, string name) => Values.GetValueOrDefault(key + "\0" + name);
		public void WriteString(string key, string name, string value)
		{
			_keys.Add(key);
			Values[key + "\0" + name] = AssociationRegistryValue.String(value);
			if (_pauseNextWrite)
			{
				_pauseNextWrite = false;
				Entered = true;
				if (!_writeGate.Wait(TimeSpan.FromSeconds(20)))
				{
					throw new TimeoutException("Registration test write was not released.");
				}
			}
		}
		public void DeleteValue(string key, string name) => Values.Remove(key + "\0" + name);
		public void DeleteKeyIfEmpty(string key)
		{
			if (!Values.Keys.Any(value => value.StartsWith(key + "\0", StringComparison.OrdinalIgnoreCase))
				&& !_keys.Any(value => value.StartsWith(key + "\\", StringComparison.OrdinalIgnoreCase)))
			{
				_keys.Remove(key);
			}
		}
		public void NotifyChanged()
		{
		}
	}
}
