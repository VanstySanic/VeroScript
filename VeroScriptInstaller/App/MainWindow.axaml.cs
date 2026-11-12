// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using VeroScriptInstaller.Core;
using VeroScriptInstaller.Core.Platform;
using VeroScriptInstaller.Localization;

namespace VeroScriptInstaller;

public partial class MainWindow : Window
{
	private readonly InstallerLocalizer _localizer = InstallerLocalizer.Instance;
	private readonly InstallerWizardState _wizard = new();
	private readonly InstallerLaunchOptions _options;
	private readonly InstallerEngine _engine;
	private readonly Func<string, bool> _openDefaultApps;
	private readonly HashSet<string> _editedAssociations = new(StringComparer.Ordinal);
	private FileAssociationOptions _associationSupport = new();
	private string? _defaultApplicationName;
	private string? _defaultSettingsNotice;
	private bool _associationSettingsPreserved;
	private readonly CancellationTokenSource _loadCancellation = new();
	private readonly Dictionary<string, CheckBox> _componentChecks = new(StringComparer.Ordinal);
	private readonly Dictionary<string, TextBlock> _componentDescriptions = new(StringComparer.Ordinal);
	private readonly Dictionary<string, Border> _componentRows = new(StringComparer.Ordinal);
	private readonly HashSet<string> _editedComponents = new(StringComparer.Ordinal);
	private IReadOnlyList<InstallerComponent> _visibleComponents = [];
	private InstallerManifest? _manifest;
	private InstallationRecord? _existingInstallation;
	private string? _existingInstallRecordPath;
	private CancellationTokenSource? _operationCancellation;
	private bool _updateDetected;
	private bool _synchronizing;
	private bool _loadingBundle = true;
	private bool _checkingInstallation;
	private bool _operationFailed;
	private bool _pathChoiceEdited;
	private bool _closed;
	private int _updateProbeVersion;
	private InstallerStatus? _status = new("Loading");
	internal Task Initialization { get; }
	internal InstallerWizardPage CurrentPage => _wizard.Page;
	private bool IsMaintenance => _options.RepairRecordPath is not null || _options.UninstallRecordPath is not null;

	public MainWindow() : this(Program.LaunchOptions)
	{
	}

	internal MainWindow(InstallerLaunchOptions options, InstallerEngine? engine = null, Func<string, bool>? openDefaultApps = null)
	{
		_options = options;
		_engine = engine ?? new InstallerEngine();
		_openDefaultApps = openDefaultApps ?? DefaultAppsSettings.TryOpen;
		InitializeComponent();
		Closing += OnClosing;
		Closed += (_, _) =>
		{
			_closed = true;
			_updateProbeVersion++;
			_localizer.Changed -= ApplyLocalization;
			_loadCancellation.Cancel();
		};
		_localizer.Changed += ApplyLocalization;
		PopulateLanguages();
		if (!string.IsNullOrWhiteSpace(_options.Language))
		{
			_localizer.SetCulture(_options.Language);
		}
		SelectLanguageItem();
		ApplyLocalization();
		Initialization = LoadAsync();
	}

	private async Task LoadAsync()
	{
		try
		{
			if (_options.UninstallRecordPath is { } uninstallRecord)
			{
				_existingInstallation = await _engine.LoadRecordAsync(uninstallRecord, _loadCancellation.Token);
				if (_closed) return;
				_existingInstallRecordPath = uninstallRecord;
				_wizard.Initialize(0, maintenance: true);
			}
			else
			{
				_manifest = await ManifestValidator.LoadAsync(_options.ManifestPath, _loadCancellation.Token);
				await ManifestValidator.VerifyPayloadAsync(_manifest, _options.PayloadRoot, _loadCancellation.Token);
				if (_closed) return;
				BuildComponentUi();
				InstallRootBox.Text = PlatformInfo.GetDefaultInstallDirectory(_manifest.InstallRoot, _manifest.ProductVersion);
				if (_options.RepairRecordPath is { } repairRecord)
				{
					_existingInstallation = await _engine.LoadRecordAsync(repairRecord, _loadCancellation.Token);
					if (_closed) return;
					_existingInstallRecordPath = repairRecord;
					ApplyExistingInstallation(_existingInstallation);
				}
				_wizard.Initialize(_visibleComponents.Count, IsMaintenance);
				await RefreshUpdateModeAsync();
			}
			_status = null;
		}
		catch (OperationCanceledException) when (_closed)
		{
		}
		catch (Exception exception)
		{
			if (_closed) return;
			_wizard.FailLoading();
			ShowFailure(exception);
		}
		finally
		{
			_loadingBundle = false;
			if (!_closed)
			{
				ApplyLocalization();
			}
		}
	}

	private void BuildComponentUi()
	{
		ComponentsHost.Children.Clear();
		_componentChecks.Clear();
		_componentDescriptions.Clear();
		_componentRows.Clear();
		if (_manifest is null) return;
		_associationSupport = _engine.GetSupportedFileAssociations(_manifest);
		_visibleComponents = _manifest.Components.Where(InstallerWizardState.IsSelectable)
			.OrderByDescending(component => component.Required).ToArray();
		foreach (var component in _visibleComponents)
		{
			var check = new CheckBox
			{
				Content = new TextBlock { Text = component.DisplayName, TextWrapping = TextWrapping.Wrap, MaxLines = 2, TextTrimming = TextTrimming.CharacterEllipsis },
				IsChecked = component.Required || component.DefaultSelected,
				IsEnabled = !component.Required,
				Tag = component.Id,
			};
			check.IsCheckedChanged += OnComponentChanged;
			_componentChecks.Add(component.Id, check);
			var content = new StackPanel { Spacing = 6 };
			content.Children.Add(check);
			if (!string.IsNullOrWhiteSpace(component.Description))
			{
				var description = new TextBlock
				{
					Margin = new Thickness(25, 0, 0, 0), Opacity = 0.72,
					TextWrapping = TextWrapping.Wrap, MaxLines = 2, TextTrimming = TextTrimming.CharacterEllipsis,
				};
				_componentDescriptions.Add(component.Id, description);
				content.Children.Add(description);
			}
			var row = new Border { Child = content };
			row.Classes.Add("card");
			_componentRows.Add(component.Id, row);
		}
		PathPanel.IsVisible = _manifest.PathIntegration is not null;
		_synchronizing = true;
		AddToPathCheckBox.IsChecked = _manifest.PathIntegration?.DefaultEnabled == true;
		_synchronizing = false;
		SynchronizeComponentSelection();
	}

	private void PopulateLanguages()
	{
		LanguageBox.Items.Clear();
		foreach (var culture in _localizer.SupportedCultures)
		{
			LanguageBox.Items.Add(new ComboBoxItem { Content = _localizer.GetCultureDisplayName(culture), Tag = culture });
		}
	}

	private void SelectLanguageItem() => LanguageBox.SelectedItem = LanguageBox.Items.OfType<ComboBoxItem>()
		.FirstOrDefault(item => string.Equals(item.Tag as string, _localizer.Culture, StringComparison.OrdinalIgnoreCase));

	private void OnLanguageChanged(object? sender, SelectionChangedEventArgs eventArgs)
	{
		if (LanguageBox.SelectedItem is ComboBoxItem { Tag: string culture })
		{
			_localizer.SetCulture(culture);
		}
	}

	private string ResolveLocalized(IReadOnlyDictionary<string, string> values, string fallback)
	{
		if (values.TryGetValue(_localizer.Culture, out var exact)) return exact;
		var language = _localizer.Culture.Split('-')[0];
		return values.OrderBy(pair => pair.Key, StringComparer.Ordinal)
			.FirstOrDefault(pair => pair.Key.StartsWith(language + '-', StringComparison.OrdinalIgnoreCase)).Value
			?? (values.TryGetValue("en-US", out var english) ? english : fallback);
	}

	private string ResolveComponentText(string? resourceKey, IReadOnlyDictionary<string, string> values, string fallback)
	{
		if (!string.IsNullOrWhiteSpace(resourceKey) && _localizer[resourceKey] != resourceKey) return _localizer[resourceKey];
		return ResolveLocalized(values, fallback);
	}

	private string OperationName => _localizer[_options.UninstallRecordPath is not null ? "Uninstall"
		: _options.RepairRecordPath is not null ? "Repair" : _updateDetected ? "Update" : "Install"];

	private static void SetText(TextBlock control, string text)
	{
		control.Text = text;
		ToolTip.SetTip(control, text);
	}

	private string Format(string key, params object[] arguments) => string.Format(_localizer[key], arguments);

	private void ApplyLocalization()
	{
		if (_closed) return;
		var productName = _manifest is not null ? ResolveLocalized(_manifest.LocalizedNames, _manifest.ProductName)
			: _existingInstallation is not null ? ResolveLocalized(_existingInstallation.LocalizedNames, _existingInstallation.ProductName) : "VeroScript";
		Title = productName + " - " + _localizer["Title"];
		SetText(HeadingText, OperationName + " " + productName);
		SetText(SubtitleText, IsMaintenance ? _localizer[_options.UninstallRecordPath is not null ? "UninstallReady" : "RepairReady"]
			: _manifest is null ? _localizer["Subtitle"]
			: ResolveComponentText(_manifest.DescriptionResourceKey, _manifest.LocalizedDescriptions, _manifest.Description));
		foreach (var component in _visibleComponents)
		{
			if (_componentChecks[component.Id].Content is TextBlock name)
			{
				SetText(name, ResolveComponentText(component.DisplayNameResourceKey, component.LocalizedNames, component.DisplayName));
			}
			if (_componentDescriptions.TryGetValue(component.Id, out var description))
			{
				SetText(description, ResolveComponentText(component.DescriptionResourceKey, component.LocalizedDescriptions, component.Description));
			}
		}
		LanguageLabel.Text = _localizer["Language"];
		InstallLocationLabel.Text = _localizer["InstallLocation"];
		SetText(InstallLocationDescriptionText, _manifest?.InstallRoot.IncludeVersion == true
			? $"{_manifest.InstallRoot.DirectoryName} / {_manifest.ProductVersion}" : _manifest?.InstallRoot.DirectoryName ?? string.Empty);
		BrowseButton.Content = _localizer["Browse"];
		SetText(AddToPathText, _localizer["AddToPath"]);
		SetText(AddToPathDescriptionText, _localizer["AddToPathDescription"]);
		SetText(AssociateSourceText, _localizer["Associations.Source"]);
		SetText(AssociateApplicationText, _localizer["Associations.Application"]);
		SetText(PackageIconText, _localizer["Associations.PackageIcon"]);
		SetText(ChooseDefaultsText, _localizer["Associations.ChooseDefaults"]);
		SetText(AssociationsDescriptionText, _localizer["Associations.Description"]);
		OpenDefaultsButton.Content = _localizer["Associations.OpenDefaults"];
		BackButton.Content = _localizer["Wizard.Back"];
		ReviewComponentsLabel.Text = _localizer["Wizard.SelectedComponents"];
		ReviewLocationLabel.Text = _localizer["InstallLocation"];
		RenderPage();
	}

	private void RenderPage()
	{
		var page = _wizard.Page;
		ComponentPanel.IsVisible = page == InstallerWizardPage.Components;
		LocationPage.IsVisible = page == InstallerWizardPage.Location;
		AssociationsPage.IsVisible = page == InstallerWizardPage.Associations;
		ReviewPage.IsVisible = page == InstallerWizardPage.Review;
		OperationPage.IsVisible = page is InstallerWizardPage.Loading or InstallerWizardPage.Progress or InstallerWizardPage.Completed or InstallerWizardPage.Error;
		ProgressBar.IsVisible = page is InstallerWizardPage.Loading or InstallerWizardPage.Progress;
		if (page == InstallerWizardPage.Components)
		{
			var rows = _visibleComponents.Skip(_wizard.ComponentPageIndex * InstallerWizardState.ComponentsPerPage)
				.Take(InstallerWizardState.ComponentsPerPage).Select(component => _componentRows[component.Id]).ToArray();
			// Keep the active controls attached during probes and language changes
			// so keyboard focus survives a component selection.
			if (!ComponentsHost.Children.SequenceEqual(rows))
			{
				ComponentsHost.Children.Clear();
				foreach (var row in rows)
				{
					ComponentsHost.Children.Add(row);
				}
			}
		}
		var title = page switch
		{
			InstallerWizardPage.Components => "Wizard.ComponentsTitle",
			InstallerWizardPage.Location => "Wizard.LocationTitle",
			InstallerWizardPage.Associations => "Associations.Title",
			InstallerWizardPage.Review => "Wizard.ReviewTitle",
			InstallerWizardPage.Completed => "Wizard.CompletedTitle",
			InstallerWizardPage.Error => "Wizard.LoadFailedTitle",
			InstallerWizardPage.Progress => _options.UninstallRecordPath is not null ? "Wizard.UninstallingTitle"
				: _options.RepairRecordPath is not null ? "Wizard.RepairingTitle" : _updateDetected ? "Wizard.UpdatingTitle" : "Wizard.InstallingTitle",
			_ => "Loading",
		};
		StepHeadingText.Text = _localizer[title];
		SetText(StepDescriptionText, page switch
		{
			InstallerWizardPage.Components => _visibleComponents.Count > InstallerWizardState.ComponentsPerPage
				? Format("Wizard.ComponentsPageFormat", _wizard.ComponentPageIndex * InstallerWizardState.ComponentsPerPage + 1,
					Math.Min((_wizard.ComponentPageIndex + 1) * InstallerWizardState.ComponentsPerPage, _visibleComponents.Count), _visibleComponents.Count)
				: _localizer["Wizard.ComponentsDescription"],
			InstallerWizardPage.Location => _localizer["Wizard.LocationDescription"],
			InstallerWizardPage.Associations => _localizer["Associations.Subtitle"],
			InstallerWizardPage.Review => _localizer["Wizard.ReviewDescription"],
			_ => string.Empty,
		});
		StepDescriptionText.IsVisible = !string.IsNullOrEmpty(StepDescriptionText.Text);
		StepNumberText.Text = _wizard.IsReady && !_wizard.IsBusy && !_wizard.IsCompleted && !_wizard.HasLoadError
			? Format("Wizard.StepFormat", _wizard.PageNumber, _wizard.PageCount) : string.Empty;
		RefreshReview();
		RefreshStatus();
		UpdateComponentState();
	}

	private string ComponentName(string id)
	{
		var component = _manifest?.Components.FirstOrDefault(item => item.Id == id);
		if (component is not null) return ResolveComponentText(component.DisplayNameResourceKey, component.LocalizedNames, component.DisplayName);
		var key = id switch { "runtime" => "Runtime", "sdk" => "SdkCore", "interop-sdk" => "InteropSdk", "web-sdk" => "WebAssemblySdk", "symbols" => "Symbols", "docs" => "Docs", "examples" => "Examples", _ => id + ".Name" };
		return _localizer[key] == key ? id : _localizer[key];
	}

	private void RefreshReview()
	{
		var selected = IsMaintenance ? _existingInstallation?.Components.Select(component => component.Id) ?? [] : SelectedComponentIds();
		SetText(ReviewComponentsText, string.Join(", ", selected.Select(ComponentName)));
		SetText(ReviewLocationText, IsMaintenance
			? string.Join(Environment.NewLine, _existingInstallation?.Components.Select(component => component.RootPath).Distinct(PathSafety.RelativePathComparer) ?? [])
			: InstallRootBox.Text?.Trim() ?? string.Empty);
		ReviewPathText.IsVisible = _manifest?.PathIntegration is not null || _existingInstallation?.PathAdded == true;
		var removesPath = _existingInstallation?.PathAdded == true
			&& (_options.UninstallRecordPath is not null || !IsMaintenance && AddToPathCheckBox.IsChecked != true);
		SetText(ReviewPathText, _localizer[removesPath ? "Wizard.PathRemove"
			: (IsMaintenance ? _existingInstallation?.PathAdded == true : AddToPathCheckBox.IsChecked == true)
				? "Wizard.PathEnabled" : "Wizard.PathDisabled"]);
		ReviewAssociationsText.IsVisible = _associationSupport.Any || _existingInstallation?.FileAssociations is not null;
		var associations = IsMaintenance ? _existingInstallation?.FileAssociations?.Options ?? new() : SelectedAssociations();
		var types = new List<string>();
		if (associations.Source)
		{
			types.Add(".vs");
		}
		if (associations.Application)
		{
			types.Add(".vsapp");
		}
		SetText(ReviewAssociationsText, _options.UninstallRecordPath is not null ? _localizer["Associations.Remove"]
			: Format("Associations.Review", types.Count == 0 ? _localizer["Associations.None"] : string.Join(", ", types),
				_localizer[associations.PackageIcon ? "Associations.Enabled" : "Associations.None"])
			+ (ChooseDefaultsCheckBox.IsChecked == true && !IsMaintenance ? " " + _localizer["Associations.DefaultReview"] : ""));
	}

	private void RefreshStatus()
	{
		var text = _status?.Render(_localizer) ?? _localizer[_options.UninstallRecordPath is not null ? "UninstallReady"
			: _options.RepairRecordPath is not null ? "RepairReady" : _updateDetected ? "UpdateReady" : "Ready"];
		SetText(StatusText, text);
		SetText(ReviewErrorText, text);
		ReviewErrorText.IsVisible = _operationFailed;
		var notices = new List<string>();
		if (_associationSettingsPreserved)
		{
			notices.Add(_localizer["Associations.Preserved"]);
		}
		if (_defaultSettingsNotice is not null)
		{
			notices.Add(_localizer[_defaultSettingsNotice]);
		}
		SetText(AssociationNoticeText, string.Join(Environment.NewLine, notices));
		AssociationNoticeText.IsVisible = _wizard.IsCompleted && notices.Count > 0;
		StatusText.MaxLines = AssociationNoticeText.IsVisible ? 5 : 9;
		OpenDefaultsButton.IsVisible = _wizard.IsCompleted && _defaultApplicationName is not null && ChooseDefaultsCheckBox.IsChecked == true;
	}

	private void SetStatus(string key, params object[] arguments)
	{
		_status = new InstallerStatus(key, arguments);
		RefreshStatus();
	}

	private void ShowFailure(Exception exception)
	{
		_operationFailed = true;
		_status = InstallerStatus.Failure(exception, InstallerDiagnostics.WriteFailure(exception));
		RefreshStatus();
		UpdateComponentState();
	}

	private HashSet<string> SelectedComponentIds() => _componentChecks.Where(pair => pair.Value.IsChecked == true)
		.Select(pair => pair.Key).ToHashSet(StringComparer.Ordinal);

	private bool TryResolveSelection(out HashSet<string> selected)
	{
		selected = [];
		return _manifest is not null && InstallerWizardState.TryResolveSelection(_manifest, SelectedComponentIds(), out selected);
	}

	private Dictionary<string, string> GetDestinations(IReadOnlySet<string> selected)
	{
		if (_manifest is null || string.IsNullOrWhiteSpace(InstallRootBox.Text)) return [];
		var root = Path.GetFullPath(InstallRootBox.Text.Trim());
		return _manifest.Components.Where(component => selected.Contains(component.Id)).ToDictionary(component => component.Id,
			component => component.InstallDirectory == "." ? root : PathSafety.ResolveUnderRoot(root, component.InstallDirectory), StringComparer.Ordinal);
	}

	private bool IsLocationValid()
	{
		if (!Path.IsPathFullyQualified(InstallRootBox.Text?.Trim() ?? string.Empty)) return false;
		try
		{
			return TryResolveSelection(out var selected) && GetDestinations(selected).Count == selected.Count;
		}
		catch (Exception exception) when (exception is ArgumentException or NotSupportedException or IOException)
		{
			return false;
		}
	}

	private bool CanProceed()
	{
		if (_loadingBundle || _checkingInstallation || !_wizard.IsReady || _wizard.IsBusy || _wizard.HasLoadError || _wizard.IsCompleted) return false;
		if (IsMaintenance) return _existingInstallation is not null && (_options.UninstallRecordPath is not null || _manifest is not null);
		if (_wizard.Page == InstallerWizardPage.Components && !_wizard.IsLastComponentPage && SelectedComponentIds().Count == 0) return true;
		return TryResolveSelection(out _) && (_wizard.Page == InstallerWizardPage.Components || IsLocationValid());
	}

	private void UpdateComponentState()
	{
		var editable = !_loadingBundle && !_wizard.IsBusy && !_wizard.IsCompleted && !_wizard.HasLoadError && !IsMaintenance;
		foreach (var component in _visibleComponents)
		{
			_componentChecks[component.Id].IsEnabled = editable && !component.Required;
		}
		InstallRootBox.IsEnabled = editable;
		BrowseButton.IsEnabled = editable;
		AddToPathCheckBox.IsEnabled = editable && _manifest?.PathIntegration is { } path && SelectedComponentIds().Contains(path.ComponentId);
		var runtimeSelected = SelectedComponentIds().Contains(InstallerConstants.RuntimeComponentId);
		AssociateSourceCheckBox.IsEnabled = editable && runtimeSelected && _associationSupport.Source;
		AssociateApplicationCheckBox.IsEnabled = editable && runtimeSelected && _associationSupport.Application;
		PackageIconCheckBox.IsEnabled = editable && runtimeSelected && _associationSupport.PackageIcon;
		ChooseDefaultsCheckBox.IsEnabled = editable && (AssociateSourceCheckBox.IsChecked == true || AssociateApplicationCheckBox.IsChecked == true);
		LanguageBox.IsEnabled = !_wizard.IsBusy;
		var terminal = _wizard.IsCompleted || _wizard.HasLoadError;
		CancelButton.IsVisible = !terminal;
		CancelButton.IsEnabled = !_wizard.IsBusy || _operationCancellation?.IsCancellationRequested != true;
		CancelButton.Content = _localizer["Cancel"];
		BackButton.IsVisible = _wizard.CanGoBack;
		BackButton.IsEnabled = _wizard.CanGoBack && !_checkingInstallation;
		InstallButton.Content = _localizer[terminal ? _wizard.IsCompleted ? "Wizard.Finish" : "Wizard.Close"
			: _wizard.Page == InstallerWizardPage.Review ? _options.UninstallRecordPath is not null ? "Uninstall"
				: _options.RepairRecordPath is not null ? "Repair" : _updateDetected ? "Update" : "Install" : "Wizard.Next"];
		InstallButton.IsEnabled = terminal || CanProceed();
		var validSelection = TryResolveSelection(out _);
		ComponentValidationText.IsVisible = _wizard.Page == InstallerWizardPage.Components && !validSelection
			&& (_wizard.IsLastComponentPage || SelectedComponentIds().Count > 0);
		ComponentValidationText.Text = _localizer[SelectedComponentIds().Count == 0 ? "SelectComponent" : "Wizard.UnsupportedComponents"];
		LocationValidationText.IsVisible = _wizard.Page == InstallerWizardPage.Location && (_operationFailed || _checkingInstallation || !IsLocationValid());
		SetText(LocationValidationText, _operationFailed ? _status?.Render(_localizer) ?? string.Empty
			: _localizer[_checkingInstallation ? "Wizard.CheckingLocation" : "Wizard.InvalidLocation"]);
	}

	private void OnComponentChanged(object? sender, RoutedEventArgs eventArgs)
	{
		if (_synchronizing || _manifest is null) return;
		if (sender is CheckBox { Tag: string id })
		{
			_editedComponents.Add(id);
		}
		if (ReferenceEquals(sender, AddToPathCheckBox))
		{
			_pathChoiceEdited = true;
		}
		SynchronizeComponentSelection();
		_operationFailed = false;
		_status = null;
		UpdateComponentState();
		_ = RefreshUpdateModeAsync();
	}

	private void SynchronizeComponentSelection()
	{
		if (_manifest is null) return;
		_synchronizing = true;
		try
		{
			if (TryResolveSelection(out var selected))
			{
				foreach (var id in selected)
				{
					_componentChecks[id].IsChecked = true;
				}
			}
			if (_manifest.PathIntegration is { } path && !SelectedComponentIds().Contains(path.ComponentId))
			{
				AddToPathCheckBox.IsChecked = false;
			}
			if (!SelectedComponentIds().Contains(InstallerConstants.RuntimeComponentId))
			{
				AssociateSourceCheckBox.IsChecked = false;
				AssociateApplicationCheckBox.IsChecked = false;
				PackageIconCheckBox.IsChecked = false;
			}
			if (AssociateSourceCheckBox.IsChecked != true && AssociateApplicationCheckBox.IsChecked != true)
			{
				ChooseDefaultsCheckBox.IsChecked = false;
			}
			_wizard.SetAssociationsEnabled(_associationSupport.Any && SelectedComponentIds().Contains(InstallerConstants.RuntimeComponentId));
		}
		finally
		{
			_synchronizing = false;
		}
	}

	private void OnInstallPathChanged(object? sender, TextChangedEventArgs eventArgs)
	{
		if (_synchronizing) return;
		_operationFailed = false;
		_status = null;
		UpdateComponentState();
		_ = RefreshUpdateModeAsync();
	}

	private async Task RefreshUpdateModeAsync()
	{
		var probe = ++_updateProbeVersion;
		if (_closed || !_wizard.IsReady || _wizard.IsBusy || _wizard.IsCompleted || _wizard.HasLoadError || _manifest is null || IsMaintenance) return;
		_checkingInstallation = true;
		UpdateComponentState();
		try
		{
			// A deselected optional component may hold the only existing record.
			// Probe every functional location so removing it remains an update.
			var destinations = GetDestinations(_visibleComponents.Select(component => component.Id).ToHashSet(StringComparer.Ordinal));
			InstallationRecord? found = null;
			string? foundPath = null;
			foreach (var destination in destinations.Values.Distinct(PathSafety.RelativePathComparer))
			{
				var marker = Path.Combine(destination, InstallerConstants.InstallRecordFileName);
				if (!File.Exists(marker))
				{
					continue;
				}
				var record = await _engine.LoadRecordAsync(marker, _loadCancellation.Token);
				if (record.ProductId != _manifest.ProductId)
				{
					continue;
				}
				if (found is not null && found.InstallationId != record.InstallationId)
				{
					found = null;
					foundPath = null;
					break;
				}
				found = record;
				foundPath = marker;
			}
			if (_closed || probe != _updateProbeVersion || _wizard.IsBusy || _wizard.IsCompleted) return;
			var changed = found?.InstallationId != _existingInstallation?.InstallationId;
			_existingInstallation = found;
			_existingInstallRecordPath = foundPath;
			_updateDetected = found is not null;
			// Repeated probes must not restore options the user just deselected.
			if (found is not null && changed)
			{
				ApplyExistingInstallation(found);
			}
			else if (found is null && changed)
			{
				_synchronizing = true;
				try
				{
					foreach (var check in new[] { AssociateSourceCheckBox, AssociateApplicationCheckBox, PackageIconCheckBox })
					{
						if (!_editedAssociations.Contains(check.Name!))
						{
							check.IsChecked = false;
						}
					}
				}
				finally
				{
					_synchronizing = false;
				}
				SynchronizeComponentSelection();
			}
			ApplyLocalization();
		}
		catch (OperationCanceledException) when (_closed)
		{
		}
		catch
		{
			// Never carry a previous location's update/relocation record into a
			// failed probe. InstallAsync validates the current destination itself.
			if (!_closed && probe == _updateProbeVersion)
			{
				_existingInstallation = null;
				_existingInstallRecordPath = null;
				_updateDetected = false;
				ApplyLocalization();
			}
		}
		finally
		{
			if (probe == _updateProbeVersion)
			{
				_checkingInstallation = false;
				if (!_closed)
				{
					UpdateComponentState();
				}
			}
		}
	}

	private void ApplyExistingInstallation(InstallationRecord record)
	{
		if (_manifest is null) return;
		_synchronizing = true;
		try
		{
			var installedIds = record.Components.Select(component => component.Id).ToHashSet(StringComparer.Ordinal);
			foreach (var component in _visibleComponents)
			{
				if (!_editedComponents.Contains(component.Id))
				{
					_componentChecks[component.Id].IsChecked = component.Required || installedIds.Contains(component.Id);
				}
			}
			if (!_pathChoiceEdited)
			{
				AddToPathCheckBox.IsChecked = record.PathAdded;
			}
			var associations = record.FileAssociations?.Options ?? new();
			if (!_editedAssociations.Contains(nameof(AssociateSourceCheckBox)))
			{
				AssociateSourceCheckBox.IsChecked = associations.Source && _associationSupport.Source;
			}
			if (!_editedAssociations.Contains(nameof(AssociateApplicationCheckBox)))
			{
				AssociateApplicationCheckBox.IsChecked = associations.Application && _associationSupport.Application;
			}
			if (!_editedAssociations.Contains(nameof(PackageIconCheckBox)))
			{
				PackageIconCheckBox.IsChecked = associations.PackageIcon && _associationSupport.PackageIcon;
			}
			var first = record.Components.FirstOrDefault(component => _componentChecks.ContainsKey(component.Id));
			if (first is not null)
			{
				var definition = _manifest.Components.Single(component => component.Id == first.Id);
				var discoveredRoot = definition.InstallDirectory == "." ? first.RootPath : AscendPath(first.RootPath, definition.InstallDirectory);
				if (!string.Equals(Path.GetFullPath(InstallRootBox.Text ?? discoveredRoot), Path.GetFullPath(discoveredRoot),
					OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal))
				{
					InstallRootBox.Text = discoveredRoot;
				}
			}
		}
		finally
		{
			_synchronizing = false;
		}
		SynchronizeComponentSelection();
	}

	private static string AscendPath(string path, string relativeDirectory)
	{
		var result = Path.GetFullPath(path);
		foreach (var _ in PathSafety.NormalizeRelativePath(relativeDirectory).Split('/'))
		{
			result = Directory.GetParent(result)?.FullName ?? result;
		}
		return result;
	}

	private async void OnBrowse(object? sender, RoutedEventArgs eventArgs)
	{
		try
		{
			IStorageFolder? start = null;
			if (!string.IsNullOrWhiteSpace(InstallRootBox.Text) && Directory.Exists(InstallRootBox.Text))
			{
				start = await StorageProvider.TryGetFolderFromPathAsync(InstallRootBox.Text);
			}
			var folders = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions { AllowMultiple = false, SuggestedStartLocation = start });
			if (!_closed && folders.Count == 1 && Directory.Exists(folders[0].Path.LocalPath))
			{
				InstallRootBox.Text = Path.GetFullPath(folders[0].Path.LocalPath);
			}
		}
		catch (Exception exception)
		{
			if (!_closed)
			{
				ShowFailure(exception);
			}
		}
	}

	private void OnBack(object? sender, RoutedEventArgs eventArgs)
	{
		if (_wizard.Back())
		{
			RenderPage();
		}
	}

	private async void OnInstall(object? sender, RoutedEventArgs eventArgs)
	{
		if (_wizard.IsCompleted || _wizard.HasLoadError)
		{
			Close();
			return;
		}
		if (_wizard.Page != InstallerWizardPage.Review)
		{
			if (_wizard.Next(CanProceed()))
			{
				RenderPage();
			}
			return;
		}
		if (!_wizard.BeginOperation(CanProceed())) return;
		_operationCancellation = new CancellationTokenSource();
		_operationFailed = false;
		ProgressBar.IsIndeterminate = true;
		_defaultApplicationName = null;
		_defaultSettingsNotice = null;
		_associationSettingsPreserved = false;
		SetStatus("Loading");
		RenderPage();
		try
		{
			if (_options.UninstallRecordPath is { } uninstall)
			{
				await _engine.UninstallAsync(uninstall, _operationCancellation.Token);
				Complete("UninstallSuccess");
			}
			else if (_options.RepairRecordPath is { } repair)
			{
				var result = await _engine.RepairAsync(repair, _options.ManifestPath, _options.PayloadRoot, CreateProgress(), _operationCancellation.Token);
				_associationSettingsPreserved = result.AssociationSettingsPreserved;
				Complete("RepairSuccess", result.ProductVersion);
			}
			else
			{
				if (_manifest is null || !TryResolveSelection(out var selected))
				{
					throw new InvalidOperationException(_localizer["SelectComponent"]);
				}
				var wasUpdate = _updateDetected;
				var result = await _engine.InstallAsync(new InstallOptions
				{
					ManifestPath = _options.ManifestPath, PayloadRoot = _options.PayloadRoot, SelectedComponents = selected,
					ComponentDestinations = GetDestinations(selected), AddToUserPath = AddToPathCheckBox.IsChecked == true,
					ExistingInstallRecordPath = _existingInstallRecordPath,
					FileAssociations = SelectedAssociations(),
				}, CreateProgress(), _operationCancellation.Token);
				_defaultApplicationName = result.DefaultApplicationName;
				_associationSettingsPreserved = result.AssociationSettingsPreserved;
				Complete(wasUpdate ? "UpdateSuccess" : "Success", result.ProductVersion);
				if (ChooseDefaultsCheckBox.IsChecked == true && !_operationCancellation.IsCancellationRequested)
				{
					if (_defaultApplicationName is not null)
					{
						OpenDefaultApps();
					}
					else
					{
						_defaultSettingsNotice = "Associations.DefaultUnavailable";
					}
				}
			}
		}
		catch (OperationCanceledException)
		{
			_status = null;
		}
		catch (Exception exception)
		{
			ShowFailure(exception);
		}
		finally
		{
			_wizard.EndOperation();
			_operationCancellation.Dispose();
			_operationCancellation = null;
			ApplyLocalization();
		}
	}

	private void Complete(string key, params object[] arguments)
	{
		_wizard.CompleteOperation();
		SetStatus(key, arguments);
	}

	private FileAssociationOptions SelectedAssociations() => !SelectedComponentIds().Contains(InstallerConstants.RuntimeComponentId) ? new() : new()
	{
		Source = _associationSupport.Source && AssociateSourceCheckBox.IsChecked == true,
		Application = _associationSupport.Application && AssociateApplicationCheckBox.IsChecked == true,
		PackageIcon = _associationSupport.PackageIcon && PackageIconCheckBox.IsChecked == true,
	};

	private void OnAssociationChanged(object? sender, RoutedEventArgs eventArgs)
	{
		if (_synchronizing) return;
		if (sender is CheckBox { Name: { } name })
		{
			_editedAssociations.Add(name);
		}
		SynchronizeComponentSelection();
		UpdateComponentState();
		RefreshReview();
	}

	private void OpenDefaultApps()
	{
		if (_defaultApplicationName is null) return;
		_defaultSettingsNotice = _openDefaultApps(_defaultApplicationName) ? "Associations.DefaultRequested" : "Associations.DefaultFailed";
		RefreshStatus();
	}

	private void OnOpenDefaults(object? sender, RoutedEventArgs eventArgs) => OpenDefaultApps();

	private IProgress<InstallProgress> CreateProgress()
	{
		var operation = _operationCancellation;
		return new UiProgress<InstallProgress>(value =>
		{
			if (!_wizard.IsBusy || _operationCancellation != operation || operation?.IsCancellationRequested == true) return;
			if (value.Stage == "copy")
			{
				ProgressBar.IsIndeterminate = false;
				ProgressBar.Maximum = Math.Max(1, value.TotalFiles);
				ProgressBar.Value = value.CompletedFiles;
				SetStatus("Copying", ComponentName(value.ComponentId ?? string.Empty), value.CompletedFiles, value.TotalFiles);
			}
			else
			{
				ProgressBar.IsIndeterminate = true;
				SetStatus("Committing");
			}
		});
	}

	private sealed class UiProgress<T>(Action<T> handler) : IProgress<T>
	{
		public void Report(T value)
		{
			if (Dispatcher.UIThread.CheckAccess())
			{
				handler(value);
			}
			else
			{
				Dispatcher.UIThread.Post(() => handler(value));
			}
		}
	}

	private void OnCancel(object? sender, RoutedEventArgs eventArgs)
	{
		if (_wizard.IsBusy)
		{
			RequestCancellation();
		}
		else
		{
			Close();
		}
	}

	private void RequestCancellation()
	{
		_operationCancellation?.Cancel();
		SetStatus("Wizard.Cancelling");
		UpdateComponentState();
	}

	private void OnClosing(object? sender, WindowClosingEventArgs eventArgs)
	{
		if (!_wizard.IsBusy) return;
		eventArgs.Cancel = true;
		RequestCancellation();
	}
}
