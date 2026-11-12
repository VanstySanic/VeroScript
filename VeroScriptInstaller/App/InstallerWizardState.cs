// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScriptInstaller.Core;

namespace VeroScriptInstaller;

internal enum InstallerWizardPage
{
	Loading, Components, Location, Associations, Review, Progress, Completed, Error,
}

internal sealed class InstallerWizardState
{
	internal const int ComponentsPerPage = 2;
	private int _componentPages;
	private int _position;
	private bool _maintenance;
	private bool _associations;
	internal bool IsReady { get; private set; }
	internal bool IsBusy { get; private set; }
	internal bool IsCompleted { get; private set; }
	internal bool HasLoadError { get; private set; }
	internal int PageCount => _maintenance ? 1 : _componentPages + 2 + (_associations ? 1 : 0);
	internal int PageNumber => _position + 1;
	internal int ComponentPageIndex => _position;
	internal bool IsLastComponentPage => _position == _componentPages - 1;
	internal bool CanGoBack => IsReady && !IsBusy && !IsCompleted && !HasLoadError && _position > 0;
	internal InstallerWizardPage Page => IsBusy ? InstallerWizardPage.Progress
		: HasLoadError ? InstallerWizardPage.Error
		: IsCompleted ? InstallerWizardPage.Completed
		: !IsReady ? InstallerWizardPage.Loading
		: _maintenance ? InstallerWizardPage.Review
		: _position < _componentPages ? InstallerWizardPage.Components
		: _position == _componentPages ? InstallerWizardPage.Location
		: _associations && _position == _componentPages + 1 ? InstallerWizardPage.Associations : InstallerWizardPage.Review;

	internal void SetAssociationsEnabled(bool enabled) => _associations = enabled;

	internal void Initialize(int componentCount, bool maintenance)
	{
		if (!maintenance && componentCount < 1)
		{
			throw new InvalidDataException("The installer has no functional components.");
		}
		_componentPages = Math.Max(1, (componentCount + ComponentsPerPage - 1) / ComponentsPerPage);
		_maintenance = maintenance;
		_position = 0;
		IsReady = true;
	}

	internal bool Next(bool valid)
	{
		if (!valid || !IsReady || IsBusy || IsCompleted || HasLoadError || Page == InstallerWizardPage.Review) return false;
		_position++;
		return true;
	}

	internal bool Back()
	{
		if (!CanGoBack) return false;
		_position--;
		return true;
	}

	internal bool BeginOperation(bool valid)
	{
		if (!valid || Page != InstallerWizardPage.Review) return false;
		IsBusy = true;
		return true;
	}

	internal void CompleteOperation() => IsCompleted = true;
	internal void EndOperation() => IsBusy = false;
	internal void FailLoading() => HasLoadError = true;

	// Old manifests/records remain readable for repair and uninstall. New
	// interactive selections never offer auxiliary content as a component.
	internal static bool IsSelectable(InstallerComponent component) => component.Id is not ("docs" or "examples");

	internal static bool TryResolveSelection(InstallerManifest manifest, IEnumerable<string> ids, out HashSet<string> selected)
	{
		var components = manifest.Components.Where(IsSelectable).ToDictionary(component => component.Id, StringComparer.Ordinal);
		selected = ids.ToHashSet(StringComparer.Ordinal);
		if (manifest.Components.Any(component => component.Required && !IsSelectable(component))) return false;
		selected.UnionWith(components.Values.Where(component => component.Required).Select(component => component.Id));
		var pending = new Queue<string>(selected);
		while (pending.TryDequeue(out var id))
		{
			if (!components.TryGetValue(id, out var component)) return false;
			foreach (var dependency in component.Dependencies)
			{
				if (!components.ContainsKey(dependency)) return false;
				if (selected.Add(dependency))
				{
					pending.Enqueue(dependency);
				}
			}
		}
		return selected.Count > 0;
	}
}
