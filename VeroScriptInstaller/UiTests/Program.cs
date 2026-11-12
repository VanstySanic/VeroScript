// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Interactivity;
using Avalonia.Threading;
using Avalonia.VisualTree;
using VeroScriptInstaller.Core;
using VeroScriptInstaller.Localization;

namespace VeroScriptInstaller.UiTests;

public static class TestApplication
{
	public static AppBuilder BuildAvaloniaApp() => AppBuilder.Configure<App>()
		.UseHeadless(new AvaloniaHeadlessPlatformOptions { UseHeadlessDrawing = false })
		.UseSkia().WithInterFont();
}

internal static partial class Program
{
	private static string? _artifacts;
	private static readonly List<string> Passed = [];

	public static int Main(string[] args)
	{
		if (args.Length == 2 && args[0] == "--artifacts")
		{
			_artifacts = Path.GetFullPath(args[1]);
			Directory.CreateDirectory(_artifacts);
		}
		else if (args.Length != 0)
		{
			throw new ArgumentException("Usage: [--artifacts <directory>]");
		}
		try
		{
			CheckStateMachine();
			using var session = HeadlessUnitTestSession.StartNew(typeof(TestApplication));
			using var timeout = new CancellationTokenSource(TimeSpan.FromMinutes(3));
			session.Dispatch(async () =>
			{
				await RunUiChecks();
				return true;
			}, timeout.Token)
				.WaitAsync(timeout.Token).GetAwaiter().GetResult();
			Console.WriteLine($"PASS {Passed.Count} installer wizard checks");
			if (_artifacts is not null)
			{
				File.WriteAllText(Path.Combine(_artifacts, "ui-tests.json"), JsonSerializer.Serialize(Passed, new JsonSerializerOptions { WriteIndented = true }));
			}
			return 0;
		}
		catch (Exception exception)
		{
			Console.Error.WriteLine(exception);
			return 1;
		}
	}

	private static void Pass(string name)
	{
		Passed.Add(name);
		Console.WriteLine("PASS " + name);
	}
	private static void Require(bool value, string message)
	{
		if (!value)
		{
			throw new InvalidOperationException(message);
		}
	}
	private static T Control<T>(MainWindow window, string name) where T : Control => window.FindControl<T>(name) ?? throw new InvalidOperationException("Missing control: " + name);
	private static void Click(MainWindow window, string name)
	{
		var button = Control<Button>(window, name);
		Require(button.IsEnabled && button.IsEffectivelyVisible, "Button cannot be used: " + name);
		button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
	}
	private static async Task Pump()
	{
		await Dispatcher.UIThread.InvokeAsync(() =>
		{
		}, DispatcherPriority.Background);
	}
	private static async Task Until(Func<bool> condition)
	{
		for (var attempt = 0; attempt < 400 && !condition(); attempt++)
		{
			await Task.Delay(10);
		}
		Require(condition(), "The installer did not reach the expected state.");
	}

	private static void CheckStateMachine()
	{
		var state = new InstallerWizardState();
		Require(!state.BeginOperation(true) && !state.Next(true), "Loading allowed navigation.");
		state.Initialize(5, false);
		Require(state.PageCount == 5 && !state.BeginOperation(true), "Component paging or operation guard failed.");
		Require(!state.Next(false) && state.PageNumber == 1, "Invalid selection advanced.");
		Require(state.Next(true) && state.Back() && state.PageNumber == 1, "Back navigation failed.");
		while (state.Page != InstallerWizardPage.Review)
		{
			Require(state.Next(true), "Could not reach review.");
		}
		Require(!state.BeginOperation(false) && state.BeginOperation(true), "Review validation failed.");
		Require(!state.Back() && !state.Next(true), "Busy wizard allowed navigation.");
		state.EndOperation();
		Require(state.Page == InstallerWizardPage.Review, "Cancellation did not return to review.");
		Require(state.BeginOperation(true), "Retry was disabled.");
		state.CompleteOperation();
		state.EndOperation();
		Require(state.Page == InstallerWizardPage.Completed && !state.Back() && !state.BeginOperation(true), "Completed wizard allowed another operation.");
		var maintenance = new InstallerWizardState();
		maintenance.Initialize(0, true);
		Require(maintenance.Page == InstallerWizardPage.Review && !maintenance.CanGoBack, "Maintenance did not start at confirmation.");
		var manifest = new InstallerManifest { Components = [
			new InstallerComponent { Id = "runtime", Required = true },
			new InstallerComponent { Id = "tools", Dependencies = ["runtime"] },
			new InstallerComponent { Id = "docs" }, new InstallerComponent { Id = "examples" }] };
		Require(InstallerWizardState.TryResolveSelection(manifest, ["tools"], out var selected) && selected.SetEquals(["runtime", "tools"]), "Dependency closure failed.");
		Require(!InstallerWizardState.TryResolveSelection(manifest, ["docs"], out _), "Documentation became selectable.");
		manifest.Components[1].Dependencies.Add("docs");
		Require(!InstallerWizardState.TryResolveSelection(manifest, ["tools"], out _), "A hidden auxiliary dependency was accepted.");
		manifest.Components[2].Required = true;
		Require(!InstallerWizardState.TryResolveSelection(manifest, ["runtime"], out _), "Required auxiliary content was accepted.");
		Pass("wizard-navigation-and-operation-guards");
	}

	private static async Task RunUiChecks()
	{
		InstallerLocalizer.VerifyBuiltInCatalogs();
		InstallerLocalizationVerification.Run(InstallerLocalizer.Instance);
		Pass("six-locale-wizard-catalogs");
		await using var fixture = await Fixture.Create();
		foreach (var culture in new[] { "en-US", "fr-FR", "ja-JP", "ko-KR", "zh-CN", "zh-TW" })
		{
			var window = await Open(fixture.Options(culture));
			try
			{
				Require(window.CurrentPage == InstallerWizardPage.Components, "Initial component page missing.");
				var first = Control<StackPanel>(window, "ComponentsHost");
				Require(first.Children.Count == 2, "Component page is not bounded.");
				Require(!first.GetVisualDescendants().OfType<CheckBox>().Any(check => check.Tag as string is "docs" or "examples"), "Auxiliary option is still visible.");
				var tools = first.GetVisualDescendants().OfType<CheckBox>().Single(check => check.Tag as string == "tools");
				Require(tools.Focus(), "The optional component could not receive keyboard focus.");
				tools.IsChecked = false;
				await Pump();
				Require(window.FocusManager?.GetFocusedElement() == tools, "Refreshing component choices lost keyboard focus.");
				Click(window, "InstallButton");
				await Pump();
				Click(window, "BackButton");
				await Pump();
				Require(tools.IsChecked == false, "Back navigation lost the component selection.");
				CheckLayout(window);
				Capture(window, culture + "-components");
				for (var count = 0; window.CurrentPage == InstallerWizardPage.Components && count < 10; count++)
				{
					CheckLayout(window);
					Click(window, "InstallButton");
					await Pump();
				}
				Require(window.CurrentPage == InstallerWizardPage.Location, "Location page missing.");
				var path = Control<TextBox>(window, "InstallRootBox");
				path.Text = "relative-path";
				await Pump();
				Require(!Control<Button>(window, "InstallButton").IsEnabled, "Relative path was accepted.");
				path.Text = Path.Combine(fixture.Root, "installation 中文", new string('a', 110), new string('b', 90));
				await Until(() => Control<Button>(window, "InstallButton").IsEnabled);
				CheckLayout(window);
				Capture(window, culture + "-location");
				Click(window, "InstallButton");
				await Pump();
				Require(window.CurrentPage == InstallerWizardPage.Review, "Confirmation page missing.");
				Require(!Directory.Exists(path.Text), "Navigation performed an installation.");
				CheckLayout(window);
				Capture(window, culture + "-review");
				Pass("layout-and-navigation-" + culture);
			}
			finally
			{
				window.Close();
			}
		}
		await CheckInstallationAndMaintenance(fixture);
		await CheckLongText();
		await CheckSelectionVariants();
		await CheckCancellationAndPathConfirmation();
		await CheckLiveLanguageChanges();
		await CheckLegacyMaintenance();
		await CheckOptionalOnlyUpdate();
		await CheckFileAssociations();
		var invalid = await Open(InstallerLaunchOptions.Parse(["--manifest", Path.Combine(fixture.Root, "missing.json"), "--language", "zh-CN"]));
		Require(invalid.CurrentPage == InstallerWizardPage.Error && !Control<Button>(invalid, "BackButton").IsVisible, "Load failure did not show a terminal error page.");
		CheckLayout(invalid);
		Capture(invalid, "zh-CN-load-error");
		invalid.Close();
		Pass("package-load-error-page");
	}

	private static async Task CheckInstallationAndMaintenance(Fixture fixture)
	{
		var window = await Open(fixture.Options("zh-CN"));
		var destination = Path.Combine(fixture.Root, "installed");
		try
		{
			for (var count = 0; window.CurrentPage == InstallerWizardPage.Components && count < 10; count++)
			{
				Click(window, "InstallButton");
				await Pump();
			}
			Require(window.CurrentPage == InstallerWizardPage.Location, "Installation did not reach the location page.");
			Control<TextBox>(window, "InstallRootBox").Text = Path.Combine(fixture.Bundle, "overlap");
			await Pump();
			await Until(() => Control<Button>(window, "InstallButton").IsEnabled);
			Click(window, "InstallButton");
			await Pump();
			Require(window.CurrentPage == InstallerWizardPage.Review && !Directory.Exists(destination), "Installation did not wait for confirmation.");
			Click(window, "InstallButton");
			await Until(() => window.CurrentPage == InstallerWizardPage.Review && Control<TextBlock>(window, "ReviewErrorText").IsVisible);
			CheckLayout(window);
			Capture(window, "zh-CN-install-error");
			Require(!Directory.Exists(Path.Combine(fixture.Bundle, "overlap")), "Invalid destination was modified.");
			Click(window, "BackButton");
			await Pump();
			Control<TextBox>(window, "InstallRootBox").Text = destination;
			await Pump();
			await Until(() => Control<Button>(window, "InstallButton").IsEnabled);
			Click(window, "InstallButton");
			await Pump();
			Click(window, "InstallButton");
			await Until(() => window.CurrentPage is InstallerWizardPage.Completed || Control<TextBlock>(window, "ReviewErrorText").IsVisible);
			Require(window.CurrentPage == InstallerWizardPage.Completed, Control<TextBlock>(window, "StatusText").Text ?? "Installation failed.");
			Require(!Directory.Exists(Path.Combine(destination, "Docs")) && !Directory.Exists(Path.Combine(destination, "Examples")), "Auxiliary content was installed.");
			CheckLayout(window);
			Capture(window, "zh-CN-completed");
		}
		finally
		{
			window.Close();
		}
		Pass("confirmed-installation-excludes-docs-and-examples");
		Pass("installation-failure-and-retry");
		var record = Path.Combine(destination, "Runtime", InstallerConstants.InstallRecordFileName);
		var update = await Open(fixture.Options("en-US"));
		try
		{
			var tools = Control<StackPanel>(update, "ComponentsHost").GetVisualDescendants().OfType<CheckBox>().Single(check => check.Tag as string == "tools");
			tools.IsChecked = false;
			await Pump();
			for (var count = 0; update.CurrentPage == InstallerWizardPage.Components && count < 10; count++)
			{
				Click(update, "InstallButton");
				await Pump();
			}
			Require(update.CurrentPage == InstallerWizardPage.Location, "Update did not reach the location page.");
			Control<TextBox>(update, "InstallRootBox").Text = destination;
			await Pump();
			await Until(() => Control<Button>(update, "InstallButton").IsEnabled);
			Click(update, "BackButton");
			await Pump();
			for (var count = 0; Control<Button>(update, "BackButton").IsVisible && count < 10; count++)
			{
				Click(update, "BackButton");
				await Pump();
			}
			Require(update.CurrentPage == InstallerWizardPage.Components && !Control<Button>(update, "BackButton").IsVisible, "Update could not return to the first component page.");
			Require(tools.IsChecked == false, "Choosing an existing destination overwrote an explicit component choice.");
			tools.IsChecked = true;
			await Pump();
			await Until(() => Control<Button>(update, "InstallButton").IsEnabled);
			tools.IsChecked = false;
			await Pump();
			await Until(() => Control<Button>(update, "InstallButton").IsEnabled);
			Require(tools.IsChecked == false, "Update detection restored a deselected optional component.");
			await File.WriteAllTextAsync(Path.Combine(destination, "Tools", "user.txt"), "keep this user file");
			for (var count = 0; update.CurrentPage != InstallerWizardPage.Review && count < 10; count++)
			{
				await Until(() => Control<Button>(update, "InstallButton").IsEnabled);
				Click(update, "InstallButton");
				await Pump();
			}
			Require(update.CurrentPage == InstallerWizardPage.Review && Control<Button>(update, "InstallButton").Content as string == InstallerLocalizer.Instance["Update"], "Existing installation was not recognized.");
			Click(update, "InstallButton");
			await Until(() => update.CurrentPage == InstallerWizardPage.Completed || Control<TextBlock>(update, "ReviewErrorText").IsVisible);
			Require(update.CurrentPage == InstallerWizardPage.Completed, "Confirmed update failed.");
			Require(!File.Exists(Path.Combine(destination, "Tools", "fixture.txt")) && File.Exists(Path.Combine(destination, "Tools", "user.txt")), "Update did not preserve ownership boundaries.");
		}
		finally
		{
			update.Close();
		}
		Pass("update-detection-preserves-deselection");
		foreach (var operation in new[] { "--repair-record", "--uninstall-record" })
		{
			var maintenance = await Open(InstallerLaunchOptions.Parse(["--manifest", fixture.Manifest, "--payload", fixture.Bundle,
				operation, record, "--language", "zh-CN"]));
			try
			{
				Require(maintenance.CurrentPage == InstallerWizardPage.Review, "Maintenance skipped confirmation.");
				Require(!Control<Button>(maintenance, "BackButton").IsVisible, "Maintenance exposed editable setup steps.");
				Click(maintenance, "InstallButton");
				await Until(() => maintenance.CurrentPage is InstallerWizardPage.Completed || Control<TextBlock>(maintenance, "ReviewErrorText").IsVisible);
				Require(maintenance.CurrentPage == InstallerWizardPage.Completed, Control<TextBlock>(maintenance, "StatusText").Text ?? "Maintenance failed.");
				CheckLayout(maintenance);
			}
			finally
			{
				maintenance.Close();
			}
		}
		Require(!File.Exists(record), "Uninstall left its managed installation record.");
		Pass("repair-and-uninstall-confirmation-flows");
	}

	private static async Task CheckLongText()
	{
		await using var fixture = await Fixture.Create(longText: true);
		foreach (var culture in new[] { "en-US", "fr-FR", "ja-JP", "ko-KR", "zh-CN", "zh-TW" })
		{
			var window = await Open(fixture.Options(culture));
			try
			{
				Require(window.CurrentPage == InstallerWizardPage.Components, Control<TextBlock>(window, "StatusText").Text ?? "Long-text fixture failed to load.");
				for (var count = 0; window.CurrentPage != InstallerWizardPage.Review && count < 10; count++)
				{
					CheckLayout(window);
					Capture(window, culture + "-long-" + count);
					if (window.CurrentPage == InstallerWizardPage.Location)
					{
						var location = Control<TextBox>(window, "InstallRootBox");
						var previous = location.Text;
						location.Text = "relative-path";
						await Pump();
						CheckLayout(window);
						Capture(window, culture + "-long-invalid-location");
						location.Text = previous;
						await Pump();
						await Until(() => Control<Button>(window, "InstallButton").IsEnabled);
					}
					Click(window, "InstallButton");
					await Pump();
				}
				Require(window.CurrentPage == InstallerWizardPage.Review, "Long text prevented navigation.");
				CheckLayout(window);
			}
			finally
			{
				window.Close();
			}
		}
		Pass("six-locale-long-text-without-scroll");
	}

	private static async Task<MainWindow> Open(InstallerLaunchOptions options, InstallerEngine? engine = null, Func<string, bool>? openDefaultApps = null)
	{
		var window = new MainWindow(options, engine, openDefaultApps) { Width = 720, Height = 580 };
		window.Show();
		await window.Initialization;
		await Pump();
		return window;
	}

	private static async Task CheckSelectionVariants()
	{
		await using var fixture = await Fixture.Create();
		var manifest = JsonSerializer.Deserialize<InstallerManifest>(await File.ReadAllTextAsync(fixture.Manifest))!;
		manifest.Components.Single(component => component.Id == "tools").DefaultSelected = false;
		manifest.Components.Single(component => component.Id == "aot").Dependencies = ["tools"];
		await File.WriteAllTextAsync(fixture.Manifest, JsonSerializer.Serialize(manifest));
		var dependencies = await Open(fixture.Options("en-US"));
		try
		{
			var tools = Control<StackPanel>(dependencies, "ComponentsHost").GetVisualDescendants().OfType<CheckBox>().Single(check => check.Tag as string == "tools");
			Require(tools.IsChecked == true, "The default selection omitted a dependency from the visible checkboxes.");
		}
		finally
		{
			dependencies.Close();
		}
		Pass("default-selection-shows-dependency-closure");
		foreach (var component in manifest.Components)
		{
			component.Required = false;
			component.DefaultSelected = false;
		}
		await File.WriteAllTextAsync(fixture.Manifest, JsonSerializer.Serialize(manifest));
		var optional = await Open(fixture.Options("en-US"));
		try
		{
			Click(optional, "InstallButton");
			await Pump();
			Click(optional, "InstallButton");
			await Pump();
			Require(optional.CurrentPage == InstallerWizardPage.Components && !Control<Button>(optional, "InstallButton").IsEnabled, "An empty final selection was accepted.");
			Control<StackPanel>(optional, "ComponentsHost").GetVisualDescendants().OfType<CheckBox>().Single().IsChecked = true;
			await Pump();
			Click(optional, "InstallButton");
			await Pump();
			Require(optional.CurrentPage == InstallerWizardPage.Location, "Could not select a component on a later page.");
		}
		finally
		{
			optional.Close();
		}
		Pass("later-page-only-component-selection");
	}

	private static void CheckLayout(MainWindow window)
	{
		// Headless captures contain client pixels only; check the native-frame
		// request separately without claiming to render the OS title bar.
		Require(window.SystemDecorations == SystemDecorations.Full && !window.ExtendClientAreaToDecorationsHint,
			"The installer no longer requests its system title bar and window buttons.");
		window.UpdateLayout();
		foreach (var name in new[] { "InstallButton", "CancelButton", "BackButton", "StepHeadingText" })
		{
			var control = Control<Control>(window, name);
			if (!control.IsEffectivelyVisible)
			{
				continue;
			}
			var point = control.TranslatePoint(default, window) ?? throw new InvalidOperationException("Control is detached.");
			Require(point.X >= -1 && point.Y >= -1 && point.X + control.Bounds.Width <= window.ClientSize.Width + 1
				&& point.Y + control.Bounds.Height <= window.ClientSize.Height + 1, "Control is clipped: " + name);
		}
		foreach (var page in new[] { "ComponentPanel", "LocationPage", "AssociationsPage", "ReviewPage", "OperationPage" })
		{
			var control = Control<Control>(window, page);
			Require(!control.GetVisualAncestors().OfType<ScrollViewer>().Any(), "Wizard page is inside a scrolling container.");
			if (!control.IsEffectivelyVisible)
			{
				continue;
			}
			var pageOrigin = control.TranslatePoint(default, window)!.Value;
			var footerOrigin = Control<Button>(window, "InstallButton").TranslatePoint(default, window)!.Value;
			Require(pageOrigin.Y + control.Bounds.Height <= footerOrigin.Y - 4, "Wizard page overlaps the navigation buttons: " + page);
			foreach (var child in control.GetVisualDescendants().OfType<Border>().Where(child => child.Classes.Contains("card") && child.IsEffectivelyVisible))
			{
				var point = child.TranslatePoint(default, control)!.Value;
				Require(point.Y + child.Bounds.Height <= control.Bounds.Height + 1, "Page content overflows: " + page);
			}
		}
	}

	private static void Capture(MainWindow window, string name)
	{
		if (_artifacts is null) return;
		Dispatcher.UIThread.RunJobs();
		window.UpdateLayout();
		AvaloniaHeadlessPlatform.ForceRenderTimerTick(3);
		using var image = window.CaptureRenderedFrame() ?? throw new InvalidOperationException("No rendered frame.");
		image.Save(Path.Combine(_artifacts, name + ".png"));
	}

	private sealed class Fixture : IAsyncDisposable
	{
		internal string Root { get; } = Path.Combine(Path.GetTempPath(), "vero-installer-ui-" + Guid.NewGuid().ToString("N"));
		internal string Bundle => Path.Combine(Root, "bundle");
		internal string Manifest => Path.Combine(Bundle, InstallerConstants.ManifestFileName);
		internal InstallerLaunchOptions Options(string culture) => InstallerLaunchOptions.Parse(["--manifest", Manifest, "--payload", Bundle, "--language", culture]);
		internal static async Task<Fixture> Create(bool longText = false, bool associations = false)
		{
			var fixture = new Fixture();
			Directory.CreateDirectory(fixture.Bundle);
			var manifest = new InstallerManifest { ProfileId = "wizard-fixture", ProductId = "dev.veroscript.wizard-fixture", ProductName = "VeroScript Wizard",
				ProductVersion = "1.2.3", Description = "Installer wizard layout and navigation fixture.",
				InstallRoot = new InstallerRootDefinition { Kind = InstallerRootKinds.Developer, DirectoryName = "WizardFixture" },
				PathIntegration = new InstallerPathDefinition { ComponentId = "runtime", DefaultEnabled = false } };
			var longLabel = string.Concat(Enumerable.Repeat("VeroScript 安装组件 Composant d’installation コンポーネント 구성 요소 ", 8));
			if (longText)
			{
				manifest.ProductName = longLabel[..128];
				manifest.Description = longLabel;
				manifest.InstallRoot.DirectoryName = "WizardFixture-" + new string('a', 82);
			}
			foreach (var (id, directory) in new[] { ("runtime", "Runtime"), ("tools", "Tools"), ("aot", "Aot"), ("symbols", "Symbols"), ("extra", "Extra"), ("docs", "Docs"), ("examples", "Examples") })
			{
				var payload = Path.Combine(fixture.Bundle, "payload", id);
				Directory.CreateDirectory(payload);
				var bytes = System.Text.Encoding.UTF8.GetBytes("synthetic UI fixture " + id);
				await File.WriteAllBytesAsync(Path.Combine(payload, "fixture.txt"), bytes);
				manifest.Components.Add(new InstallerComponent { Id = id, DisplayName = id == "runtime" ? "Runtime" : id,
					Description = "A functional component used by the installation wizard.", InstallDirectory = directory,
					PayloadDirectory = "payload/" + id, Required = id == "runtime", DefaultSelected = true,
					Files = [new InstallerFile { Path = "fixture.txt", Size = bytes.Length, Sha256 = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant() }] });
				if (associations && id == "runtime")
				{
					foreach (var file in new[] { "veroscript.exe", "assets/icons/veroscript-source.ico", "assets/icons/veroscript-app.ico", "assets/icons/veroscript-package.ico" })
					{
						var full = Path.Combine(payload, file);
						Directory.CreateDirectory(Path.GetDirectoryName(full)!);
						await File.WriteAllBytesAsync(full, bytes);
						manifest.Components[^1].Files.Add(new InstallerFile { Path = file, Size = bytes.Length, Sha256 = Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant() });
					}
				}
				if (longText)
				{
					manifest.Components[^1].DisplayName = longLabel[..128];
					manifest.Components[^1].Description = longLabel;
				}
			}
			await File.WriteAllTextAsync(fixture.Manifest, JsonSerializer.Serialize(manifest, new JsonSerializerOptions { WriteIndented = true }));
			return fixture;
		}
		public async ValueTask DisposeAsync()
		{
			var temporary = Path.TrimEndingDirectorySeparator(Path.GetFullPath(Path.GetTempPath())) + Path.DirectorySeparatorChar;
			if (!Path.GetFullPath(Root).StartsWith(temporary, StringComparison.OrdinalIgnoreCase))
			{
				throw new InvalidOperationException("Test root escaped its temporary directory.");
			}
			for (var attempt = 0; Directory.Exists(Root); attempt++)
			{
				try
				{
					Directory.Delete(Root, true);
				}
				catch (IOException) when (attempt < 20)
				{
					await Task.Delay(50);
				}
			}
		}
	}
}
