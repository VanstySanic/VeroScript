// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Input;
using Avalonia.VisualTree;
using System.Text.Json;
using VeroScriptInstaller.Core;
using VeroScriptInstaller.Core.Platform;
using VeroScriptInstaller.Localization;

namespace VeroScriptInstaller.UiTests;

internal static partial class Program
{
	private static readonly string[] Cultures = ["en-US", "fr-FR", "ja-JP", "ko-KR", "zh-CN", "zh-TW"];
	private static void PressKey(MainWindow window, PhysicalKey key)
	{
		window.KeyPressQwerty(key, RawInputModifiers.None);
		window.KeyReleaseQwerty(key, RawInputModifiers.None);
	}

	private static void ChooseLanguage(MainWindow window, string culture) => Control<ComboBox>(window, "LanguageBox").SelectedItem
		= Control<ComboBox>(window, "LanguageBox").Items.OfType<ComboBoxItem>().Single(item => item.Tag as string == culture);

	private static async Task GoToLocation(MainWindow window)
	{
		for (var count = 0; window.CurrentPage == InstallerWizardPage.Components && count < 10; count++)
		{
			await Until(() => Control<Button>(window, "InstallButton").IsEnabled);
			Click(window, "InstallButton");
			await Pump();
		}
		Require(window.CurrentPage == InstallerWizardPage.Location, "The location page was not reached.");
	}

	private static async Task SetLocation(MainWindow window, string path)
	{
		Control<TextBox>(window, "InstallRootBox").Text = path;
		await Pump();
		await Until(() => Control<Button>(window, "InstallButton").IsEnabled);
	}

	private static async Task CheckCancellationAndPathConfirmation()
	{
		await using var fixture = await Fixture.Create();
		var integration = new ControlledPathIntegration();
		var engine = new InstallerEngine(integration);
		var destination = Path.Combine(fixture.Root, "cancellation-target");
		var window = await Open(fixture.Options("en-US"), engine);
		try
		{
			await GoToLocation(window);
			await SetLocation(window, destination);
			Control<CheckBox>(window, "AddToPathCheckBox").IsChecked = true;
			await Pump();
			Click(window, "InstallButton");
			await Pump();
			foreach (var closeWindow in new[] { false, true })
			{
				integration.Pause();
				Click(window, "InstallButton");
				await Until(() => integration.Entered);
				Require(window.CurrentPage == InstallerWizardPage.Progress && !Control<Button>(window, "InstallButton").IsEnabled
					&& !Control<Button>(window, "BackButton").IsVisible, "An active operation allowed navigation.");
				var closed = false;
				EventHandler handler = (_, _) => closed = true;
				window.Closed += handler;
				if (closeWindow)
				{
					window.Close();
				}
				else
				{
					Click(window, "CancelButton");
				}
				Require(!closed && window.CurrentPage == InstallerWizardPage.Progress, "Cancellation closed the window before rollback.");
				Require(!Control<Button>(window, "CancelButton").IsEnabled
					&& Control<TextBlock>(window, "StatusText").Text == InstallerLocalizer.Instance["Wizard.Cancelling"], "Cancellation feedback was not stable.");
				integration.Release();
				await Until(() => window.CurrentPage == InstallerWizardPage.Review);
				window.Closed -= handler;
				Require(integration.Entries.Count == 0 && !File.Exists(Path.Combine(destination, "Runtime", InstallerConstants.InstallRecordFileName))
					&& !Directory.EnumerateFiles(destination, "fixture.txt", SearchOption.AllDirectories).Any(), "Cancellation left managed files or PATH state.");
				Require(!Directory.EnumerateFileSystemEntries(destination, "*", SearchOption.AllDirectories)
					.Any(path => Path.GetFileName(path).Contains(".veroscript-stage-", StringComparison.Ordinal)
						|| Path.GetFileName(path).Contains(".veroscript-backup-", StringComparison.Ordinal)
						|| Path.GetFileName(path).StartsWith(".veroscript-transaction-", StringComparison.Ordinal)), "Cancellation left a transaction incomplete.");
				Require(Control<Button>(window, "InstallButton").IsEnabled, "Cancellation disabled retry.");
			}
			Click(window, "InstallButton");
			await Until(() => window.CurrentPage == InstallerWizardPage.Completed || Control<TextBlock>(window, "ReviewErrorText").IsVisible);
			Require(window.CurrentPage == InstallerWizardPage.Completed && integration.Entries.Count == 1, "Retry after cancellation failed.");
		}
		finally
		{
			window.Close();
			integration.Release();
			await Until(() => window.CurrentPage != InstallerWizardPage.Progress);
			window.Close();
		}
		Pass("cancel-and-window-close-roll-back-before-retry");

		var record = Path.Combine(destination, "Runtime", InstallerConstants.InstallRecordFileName);
		var update = await Open(fixture.Options("en-US"), engine);
		try
		{
			await GoToLocation(update);
			Control<CheckBox>(update, "AddToPathCheckBox").IsChecked = true;
			Control<CheckBox>(update, "AddToPathCheckBox").IsChecked = false;
			await Pump();
			await SetLocation(update, destination);
			Require(Control<CheckBox>(update, "AddToPathCheckBox").IsChecked == false, "An initial update probe overwrote an explicit PATH choice.");
			Click(update, "InstallButton");
			await Pump();
			foreach (var culture in Cultures)
			{
				ChooseLanguage(update, culture);
				await Pump();
				Require(Control<TextBlock>(update, "ReviewPathText").Text == InstallerLocalizer.Instance["Wizard.PathRemove"], "Update confirmation did not describe PATH removal.");
				CheckLayout(update);
				Capture(update, culture + "-path-removal-review");
			}
		}
		finally
		{
			update.Close();
		}
		Pass("six-locale-update-path-removal-confirmation");
		await CheckProbeIsolation(fixture, engine, destination);

		var uninstall = await Open(InstallerLaunchOptions.Parse(["--uninstall-record", record, "--language", "en-US"]), engine);
		try
		{
			foreach (var culture in Cultures)
			{
				ChooseLanguage(uninstall, culture);
				await Pump();
				Require(uninstall.CurrentPage == InstallerWizardPage.Review && Control<TextBlock>(uninstall, "ReviewPathText").IsVisible
					&& Control<TextBlock>(uninstall, "ReviewPathText").Text == InstallerLocalizer.Instance["Wizard.PathRemove"], "Uninstall confirmation described adding PATH.");
				Require(Control<TextBlock>(uninstall, "SubtitleText").Text == InstallerLocalizer.Instance["UninstallReady"], "Uninstall still described choosing installation components.");
				CheckLayout(uninstall);
				Capture(uninstall, culture + "-uninstall-review");
			}
			Click(uninstall, "InstallButton");
			await Until(() => uninstall.CurrentPage == InstallerWizardPage.Completed || Control<TextBlock>(uninstall, "ReviewErrorText").IsVisible);
			Require(uninstall.CurrentPage == InstallerWizardPage.Completed && integration.Entries.Count == 0 && !File.Exists(record), "Uninstall did not remove owned state.");
		}
		finally
		{
			uninstall.Close();
		}
		Pass("six-locale-uninstall-path-removal-confirmation");
	}

	private static async Task CheckProbeIsolation(Fixture fixture, InstallerEngine engine, string installed)
	{
		var window = await Open(fixture.Options("en-US"), engine);
		try
		{
			await GoToLocation(window);
			await SetLocation(window, installed);
			var invalid = Path.Combine(fixture.Root, "invalid-record");
			Directory.CreateDirectory(Path.Combine(invalid, "Runtime"));
			await File.WriteAllTextAsync(Path.Combine(invalid, "Runtime", InstallerConstants.InstallRecordFileName), "invalid");
			await SetLocation(window, invalid);
			Click(window, "InstallButton");
			await Pump();
			Require(Control<Button>(window, "InstallButton").Content as string == InstallerLocalizer.Instance["Install"], "A failed probe retained a previous installation record.");
			Click(window, "BackButton");
			await Pump();
			Control<TextBox>(window, "InstallRootBox").Text = installed;
			await Pump();
			await SetLocation(window, Path.Combine(fixture.Root, "fresh-target"));
			Click(window, "InstallButton");
			await Pump();
			Require(Control<Button>(window, "InstallButton").Content as string == InstallerLocalizer.Instance["Install"], "A late probe replaced the current destination's state.");
		}
		finally
		{
			window.Close();
		}
		Pass("failed-and-superseded-location-probes-are-isolated");
	}

	private static async Task CheckLiveLanguageChanges()
	{
		await using var fixture = await Fixture.Create();
		var window = await Open(fixture.Options("en-US"));
		try
		{
			var tools = Control<StackPanel>(window, "ComponentsHost").GetVisualDescendants().OfType<CheckBox>().Single(check => check.Tag as string == "tools");
			Require(tools.Focus(), "Keyboard navigation could not focus a component.");
			PressKey(window, PhysicalKey.Space);
			await Pump();
			Require(tools.IsChecked == false && window.FocusManager?.GetFocusedElement() == tools, "Keyboard selection lost its state or focus.");
			Require(Control<Button>(window, "InstallButton").Focus(), "Keyboard navigation could not focus Next.");
			PressKey(window, PhysicalKey.Enter);
			await Pump();
			Require(Control<StackPanel>(window, "ComponentsHost").GetVisualDescendants().OfType<CheckBox>().First().Tag as string == "aot", "Enter did not advance to the next component page.");
			Require(Control<Button>(window, "BackButton").Focus(), "Keyboard navigation could not focus Back.");
			PressKey(window, PhysicalKey.Space);
			await Pump();
			Require(Control<StackPanel>(window, "ComponentsHost").GetVisualDescendants().OfType<CheckBox>().Any(check => check == tools)
				&& tools.IsChecked == false, "Keyboard Back navigation reset the selection.");
			Pass("keyboard-space-enter-and-back-navigation");
			var first = Control<StackPanel>(window, "ComponentsHost").Children.ToArray();
			foreach (var culture in Cultures)
			{
				ChooseLanguage(window, culture);
				await Pump();
				Require(window.CurrentPage == InstallerWizardPage.Components && Control<StackPanel>(window, "ComponentsHost").Children.SequenceEqual(first)
					&& Control<Button>(window, "InstallButton").Content as string == InstallerLocalizer.Instance["Wizard.Next"], "Live language switching reset the component page.");
				CheckLayout(window);
			}
			await GoToLocation(window);
			Control<TextBox>(window, "InstallRootBox").Text = "relative-path";
			await Pump();
			foreach (var culture in Cultures)
			{
				ChooseLanguage(window, culture);
				await Pump();
				Require(!Control<Button>(window, "InstallButton").IsEnabled && Control<TextBlock>(window, "LocationValidationText").IsVisible
					&& Control<TextBlock>(window, "LocationValidationText").Text == InstallerLocalizer.Instance["Wizard.InvalidLocation"], "Language switching cleared location validation.");
				CheckLayout(window);
			}
			Click(window, "CancelButton");
			Require(!Directory.Exists(Path.Combine(fixture.Root, "installed")), "Pre-install cancellation created an installation.");
		}
		finally
		{
			window.Close();
		}
		Pass("live-language-switching-preserves-navigation-and-validation");
	}

	private static async Task CheckLegacyMaintenance()
	{
		await using var fixture = await Fixture.Create();
		var manifest = await ManifestValidator.LoadAsync(fixture.Manifest);
		var destination = Path.Combine(fixture.Root, "legacy-installation");
		var engine = new InstallerEngine(new ControlledPathIntegration());
		var result = await engine.InstallAsync(new InstallOptions
		{
			ManifestPath = fixture.Manifest, PayloadRoot = fixture.Bundle,
			SelectedComponents = manifest.Components.Select(component => component.Id).ToHashSet(StringComparer.Ordinal),
			ComponentDestinations = manifest.Components.ToDictionary(component => component.Id,
				component => Path.Combine(destination, component.InstallDirectory), StringComparer.Ordinal),
		});
		var record = result.InstallRecordPaths.First();
		var documentation = Path.Combine(destination, "Docs", "fixture.txt");
		File.Delete(documentation);
		var repair = await Open(InstallerLaunchOptions.Parse(["--manifest", fixture.Manifest, "--payload", fixture.Bundle,
			"--repair-record", record, "--language", "zh-CN"]), engine);
		try
		{
			Require(repair.CurrentPage == InstallerWizardPage.Review && !Control<Control>(repair, "ComponentPanel").IsVisible, "Legacy repair exposed a component-selection page.");
			Require(Control<TextBlock>(repair, "SubtitleText").Text == InstallerLocalizer.Instance["RepairReady"], "Repair did not describe the maintenance operation.");
			Click(repair, "InstallButton");
			await Until(() => repair.CurrentPage == InstallerWizardPage.Completed || Control<TextBlock>(repair, "ReviewErrorText").IsVisible);
			Require(repair.CurrentPage == InstallerWizardPage.Completed && File.Exists(documentation), "Legacy repair no longer restored the original component set.");
		}
		finally
		{
			repair.Close();
		}
		var uninstall = await Open(InstallerLaunchOptions.Parse(["--manifest", Path.Combine(fixture.Root, "missing.json"),
			"--payload", Path.Combine(fixture.Root, "missing-payload"), "--uninstall-record", record, "--language", "zh-CN"]), engine);
		try
		{
			Require(uninstall.CurrentPage == InstallerWizardPage.Review, "Uninstall attempted to read a missing payload.");
			Click(uninstall, "InstallButton");
			await Until(() => uninstall.CurrentPage == InstallerWizardPage.Completed || Control<TextBlock>(uninstall, "ReviewErrorText").IsVisible);
			Require(uninstall.CurrentPage == InstallerWizardPage.Completed && !File.Exists(record) && !File.Exists(documentation), "Legacy uninstall left its managed files.");
		}
		finally
		{
			uninstall.Close();
		}
		Pass("legacy-docs-repair-and-payload-independent-uninstall");
	}

	private static async Task CheckOptionalOnlyUpdate()
	{
		await using var fixture = await Fixture.Create();
		var manifest = await ManifestValidator.LoadAsync(fixture.Manifest);
		foreach (var component in manifest.Components)
		{
			component.Required = false;
			component.DefaultSelected = false;
		}
		await File.WriteAllTextAsync(fixture.Manifest, JsonSerializer.Serialize(manifest));
		var destination = Path.Combine(fixture.Root, "optional-only");
		var runtimeRoot = Path.Combine(destination, "Runtime");
		var engine = new InstallerEngine(new ControlledPathIntegration());
		await engine.InstallAsync(new InstallOptions
		{
			ManifestPath = fixture.Manifest, PayloadRoot = fixture.Bundle, SelectedComponents = new HashSet<string> { "runtime" },
			ComponentDestinations = new Dictionary<string, string> { ["runtime"] = runtimeRoot },
		});
		var window = await Open(fixture.Options("en-US"), engine);
		try
		{
			var runtime = Control<StackPanel>(window, "ComponentsHost").GetVisualDescendants().OfType<CheckBox>().Single(check => check.Tag as string == "runtime");
			runtime.IsChecked = true;
			runtime.IsChecked = false;
			await Pump();
			Click(window, "InstallButton");
			await Pump();
			Click(window, "InstallButton");
			await Pump();
			Control<StackPanel>(window, "ComponentsHost").GetVisualDescendants().OfType<CheckBox>().Single().IsChecked = true;
			await Pump();
			await GoToLocation(window);
			await SetLocation(window, destination);
			Click(window, "InstallButton");
			await Pump();
			Require(Control<Button>(window, "InstallButton").Content as string == InstallerLocalizer.Instance["Update"], "An update removing every old optional component lost its installation record.");
			Click(window, "InstallButton");
			await Until(() => window.CurrentPage == InstallerWizardPage.Completed || Control<TextBlock>(window, "ReviewErrorText").IsVisible);
			Require(window.CurrentPage == InstallerWizardPage.Completed && !File.Exists(Path.Combine(runtimeRoot, "fixture.txt"))
				&& File.Exists(Path.Combine(destination, "Extra", "fixture.txt")), "An optional-only update left the removed component installed.");
		}
		finally
		{
			window.Close();
		}
		Pass("optional-only-update-can-replace-the-entire-component-set");
	}

	// In-memory PATH operations make the real engine and rollback deterministic
	// without changing the test host's environment or shell configuration.
	private sealed class ControlledPathIntegration : IPathIntegration
	{
		internal HashSet<string> Entries { get; } = new(PathSafety.RelativePathComparer);
		internal bool Entered { get; private set; }
		private TaskCompletionSource? _gate;
		internal void Pause()
		{
			Entered = false;
			_gate = new(TaskCreationOptions.RunContinuationsAsynchronously);
		}
		internal void Release()
		{
			_gate?.TrySetResult();
			_gate = null;
		}
		public async Task<bool> ContainsUserPathAsync(string directory, CancellationToken cancellationToken = default)
		{
			if (_gate is { } gate)
			{
				Entered = true;
				await gate.Task;
			}
			cancellationToken.ThrowIfCancellationRequested();
			return Entries.Contains(directory);
		}
		public Task<PathMutation> AddToUserPathAsync(string directory, CancellationToken cancellationToken = default)
		{
			cancellationToken.ThrowIfCancellationRequested();
			return Task.FromResult(new PathMutation { Entry = directory, Changed = Entries.Add(directory) });
		}
		public Task RemoveFromUserPathAsync(string directory, CancellationToken cancellationToken = default)
		{
			cancellationToken.ThrowIfCancellationRequested();
			Entries.Remove(directory);
			return Task.CompletedTask;
		}
		public Task UndoAsync(PathMutation mutation, CancellationToken cancellationToken = default) => mutation.Changed
			? RemoveFromUserPathAsync(mutation.Entry, cancellationToken) : Task.CompletedTask;
	}
}
