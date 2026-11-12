// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using VeroScriptInstaller.Core.Platform;

namespace VeroScriptInstaller.Core;

public sealed class InstallerEngine
{
	private readonly IPathIntegration _pathIntegration;
	private readonly IFileAssociationRegistry _fileAssociations;
	public bool SupportsFileAssociations => _fileAssociations.IsSupported;
	public FileAssociationOptions GetSupportedFileAssociations(InstallerManifest manifest) => SupportsFileAssociations
		? FileAssociationRegistration.SupportedOptions(manifest) : new();

	public InstallerEngine(IPathIntegration? pathIntegration = null, IFileAssociationRegistry? fileAssociations = null)
	{
		_pathIntegration = pathIntegration ?? PathIntegrationFactory.Create();
		_fileAssociations = fileAssociations ?? FileAssociationRegistryFactory.Create();
	}

	public async Task<InstallResult> InstallAsync(
		InstallOptions options,
		IProgress<InstallProgress>? progress = null,
		CancellationToken cancellationToken = default)
	{
		ArgumentNullException.ThrowIfNull(options);
		ArgumentException.ThrowIfNullOrWhiteSpace(options.ManifestPath);
		ArgumentException.ThrowIfNullOrWhiteSpace(options.PayloadRoot);
		ArgumentNullException.ThrowIfNull(options.SelectedComponents);
		ArgumentNullException.ThrowIfNull(options.ComponentDestinations);
		var manifest = await ManifestValidator.LoadAsync(options.ManifestPath, cancellationToken);
		var selected = ResolveSelectedComponents(manifest, options);
		ArgumentNullException.ThrowIfNull(options.FileAssociations);
		if (options.FileAssociations.Any)
		{
			var supported = FileAssociationRegistration.SupportedOptions(manifest);
			if (!_fileAssociations.IsSupported || manifest.Platform != "windows")
			{
				throw new PlatformNotSupportedException("File associations require Windows.");
			}
			if (!selected.Any(item => item.Component.Id == InstallerConstants.RuntimeComponentId)
				|| options.FileAssociations.Source && !supported.Source
				|| options.FileAssociations.Application && !supported.Application
				|| options.FileAssociations.PackageIcon && !supported.PackageIcon)
			{
				throw new InvalidDataException("File associations require the Runtime executable and corresponding installed icons.");
			}
			FileAssociationRegistration.ValidateRuntimeRoot(selected.Single(item => item.Component.Id == InstallerConstants.RuntimeComponentId).Destination);
		}
		var recoveryRoots = selected.Select(item => item.Destination).ToList();
		if (!string.IsNullOrWhiteSpace(options.ExistingInstallRecordPath))
		{
			recoveryRoots.Add(Path.GetDirectoryName(Path.GetFullPath(options.ExistingInstallRecordPath))
				?? throw new InvalidDataException("The existing install record has no parent directory."));
		}
		await InstallerTransaction.RecoverForRootsAsync(
			recoveryRoots, _pathIntegration, cancellationToken, _fileAssociations);
		var pathDefinition = manifest.PathIntegration;
		if (options.PathAlreadyOwned
			&& (pathDefinition is null || selected.All(item => item.Component.Id != pathDefinition.ComponentId)))
		{
			throw new InvalidOperationException("Existing PATH ownership can only be transferred with its configured component.");
		}
		ValidateDestinationRoots(selected.Select(item => item.Destination), options.PayloadRoot);
		var selectedDestinations = selected.ToDictionary(
			item => item.Component.Id, item => item.Destination, StringComparer.Ordinal);
		var existingRecords = new List<InstallationRecord>();
		var existingByComponent = new Dictionary<string, InstallationRecord>(StringComparer.Ordinal);
		InstallationRecord? requestedExistingRecord = null;
		if (!string.IsNullOrWhiteSpace(options.ExistingInstallRecordPath))
		{
			var recordPath = Path.GetFullPath(options.ExistingInstallRecordPath);
			requestedExistingRecord = await InstallerJson.ReadAsync<InstallationRecord>(recordPath, cancellationToken);
			ValidateRecord(requestedExistingRecord);
			await ValidateRecordLocationAndSiblingsAsync(recordPath, requestedExistingRecord, cancellationToken);
			if (!requestedExistingRecord.ProductId.Equals(manifest.ProductId, StringComparison.Ordinal))
			{
				throw new InvalidOperationException("The selected installation belongs to a different product.")
					.WithInstallerError(InstallerError.WrongProduct);
			}
			if (!requestedExistingRecord.Platform.Equals(manifest.Platform, StringComparison.Ordinal)
				|| !requestedExistingRecord.Architecture.Equals(manifest.Architecture, StringComparison.Ordinal))
			{
				throw new InvalidOperationException(
					"An existing VeroScript installation cannot be replaced by a package for another platform or architecture.")
					.WithInstallerError(InstallerError.WrongPlatform);
			}
			if (!options.AllowDowngrade
				&& Version.Parse(requestedExistingRecord.ProductVersion)
					.CompareTo(Version.Parse(manifest.ProductVersion)) > 0)
			{
				throw new InvalidOperationException(
					$"Refusing to downgrade the installed product from {requestedExistingRecord.ProductVersion} "
					+ $"to {manifest.ProductVersion}. Use the explicit downgrade option if this is intentional.")
					.WithInstallerError(InstallerError.Downgrade);
			}
			existingRecords.Add(requestedExistingRecord);
			ValidateRelocationRoots(requestedExistingRecord, selectedDestinations);
		}
		foreach (var item in selected)
		{
			InstallationRecord? existing;
			var requestedComponent = requestedExistingRecord?.Components.SingleOrDefault(
				component => component.Id == item.Component.Id);
			if (requestedComponent is not null)
			{
				await ValidateRelocationDestinationAsync(
					requestedComponent.RootPath, item.Destination, cancellationToken);
				existing = requestedExistingRecord;
			}
			else
			{
				existing = await ValidateManagedDestinationAsync(
					item.Destination,
					item.Component.Id,
					manifest.ProductId,
					manifest.ProductVersion,
					manifest.Platform,
					manifest.Architecture,
					options.AllowDowngrade,
					selectedDestinations,
					cancellationToken);
			}
			if (existing is not null && existingRecords.All(record => record.InstallationId != existing.InstallationId))
			{
				existingRecords.Add(existing);
			}
			if (existing is not null)
			{
				existingByComponent[item.Component.Id] = existing;
			}
		}
		if (existingRecords.Select(record => record.InstallationId).Distinct(StringComparer.Ordinal).Count() > 1)
		{
			throw new InvalidOperationException("The selected destinations belong to different VeroScript installations.")
				.WithInstallerError(InstallerError.DestinationConflict);
		}
		var existingPathOwned = existingRecords.Any(record => record.PathAdded);
		if (options.PathAlreadyOwned && !existingPathOwned)
		{
			throw new InvalidOperationException(
				"PATH ownership cannot be transferred without a validated existing installation record.");
		}
		var existingPathEntry = existingRecords
			.Where(record => record.PathAdded)
			.Select(record => record.PathEntry)
			.FirstOrDefault(entry => !string.IsNullOrWhiteSpace(entry));

		var prepared = new List<PreparedComponent>();
		var preparedRemovals = new List<PreparedComponent>();
		var transactionId = Guid.NewGuid().ToString("N");
		InstallerTransaction? transaction = null;
		var removedComponents = requestedExistingRecord?.Components
			.Where(component => !selectedDestinations.ContainsKey(component.Id)).ToArray() ?? [];
		PathMutation? addedPathMutation = null;
		string? removedPathEntry = null;
		var pathOwned = false;
		string? pathEntry = null;
		var totalFiles = selected.Sum(item => item.Files.Count);
		var completedFiles = 0;
		try
		{
			foreach (var item in selected)
			{
				var existingComponent = existingByComponent.TryGetValue(item.Component.Id, out var existingRecord)
					? existingRecord.Components.Single(component => component.Id == item.Component.Id)
					: null;
				prepared.Add(CreatePreparedComponent(
					item.Component, item.Destination, existingComponent?.RootPath, transactionId));
			}
			foreach (var removed in removedComponents)
			{
				preparedRemovals.Add(CreatePreparedComponent(
					new InstallerComponent { Id = removed.Id }, removed.RootPath, removed.RootPath, transactionId));
			}

			transaction = await InstallerTransaction.BeginAsync(
				transactionId,
				"install",
				prepared.Select(component => ToTransactionComponent(component, removeIfEmpty: false))
					.Concat(preparedRemovals.Select(component => ToTransactionComponent(component, removeIfEmpty: true)))
					.ToArray(),
				cancellationToken);

			for (var index = 0; index < selected.Count; ++index)
			{
				cancellationToken.ThrowIfCancellationRequested();
				var item = selected[index];
				var preparedComponent = prepared[index];
				var existingComponent = existingByComponent.TryGetValue(item.Component.Id, out var existingRecord)
					? existingRecord.Components.Single(component => component.Id == item.Component.Id)
					: null;
				Directory.CreateDirectory(preparedComponent.StagePath);
				var componentPayloadRoot = PathSafety.ResolveUnderRoot(options.PayloadRoot, item.Component.PayloadDirectory);
				PathSafety.EnsureDirectoryIsNotLink(componentPayloadRoot);

				foreach (var file in item.Files)
				{
					cancellationToken.ThrowIfCancellationRequested();
					await CopyAndVerifyFileAsync(componentPayloadRoot, preparedComponent.StagePath, file, cancellationToken);
					++completedFiles;
					progress?.Report(new InstallProgress
					{
						Stage = "copy",
						ComponentId = item.Component.Id,
						RelativePath = file.Path,
						CompletedFiles = completedFiles,
						TotalFiles = totalFiles,
					});
				}
				if (existingComponent is not null)
				{
					await PreserveUnownedFilesAsync(
						existingComponent.RootPath,
						preparedComponent.StagePath,
						existingComponent,
						item.Files,
						cancellationToken);
				}
			}

			for (var index = 0; index < removedComponents.Length; ++index)
			{
				var removed = removedComponents[index];
				var removal = preparedRemovals[index];
				Directory.CreateDirectory(removal.StagePath);
				await PreserveUnownedFilesAsync(
					removed.RootPath, removal.StagePath, removed, [], cancellationToken);
			}

			if (options.AddToUserPath)
			{
				if (pathDefinition is null)
				{
					throw new InvalidOperationException("This product does not support PATH integration.");
				}
				var pathComponent = selected.SingleOrDefault(item => item.Component.Id == pathDefinition.ComponentId)
					?? throw new InvalidOperationException("PATH integration requires its configured component to be selected.");
				var configuredPath = pathDefinition.RelativeDirectory == "."
					? pathComponent.Destination
					: PathSafety.ResolveUnderRoot(pathComponent.Destination, pathDefinition.RelativeDirectory);
				var pathAlreadyPresent = await _pathIntegration.ContainsUserPathAsync(
					configuredPath, cancellationToken);
				if (!pathAlreadyPresent)
				{
					await transaction.RecordAddedPathIntentAsync(configuredPath, cancellationToken);
				}
				addedPathMutation = await _pathIntegration.AddToUserPathAsync(
					configuredPath, cancellationToken);
				if ((existingPathOwned || options.PathAlreadyOwned)
					&& existingPathEntry is not null
					&& PathsEqual(existingPathEntry, configuredPath))
				{
					pathOwned = true;
					pathEntry = configuredPath;
				}
				else
				{
					pathOwned = addedPathMutation.Changed;
					pathEntry = pathOwned ? addedPathMutation.Entry : null;
				}
			}
			if ((existingPathOwned || options.PathAlreadyOwned)
				&& existingPathEntry is not null
				&& (!options.AddToUserPath
					|| pathEntry is null
					|| !PathsEqual(existingPathEntry, pathEntry)))
			{
				await transaction.RecordRemovedPathIntentAsync(existingPathEntry, cancellationToken);
				await _pathIntegration.RemoveFromUserPathAsync(existingPathEntry, cancellationToken);
				removedPathEntry = existingPathEntry;
			}

			var record = CreateRecord(
				manifest,
				selected,
				pathOwned,
				pathEntry);
			var associationPlan = FileAssociationRegistration.Prepare(_fileAssociations, options.FileAssociations,
				selected.SingleOrDefault(item => item.Component.Id == InstallerConstants.RuntimeComponentId)?.Destination,
				existingRecords.SingleOrDefault()?.FileAssociations);
			record.FileAssociations = associationPlan.Record;
			ValidateRecord(record);
			if (associationPlan.Change is { } associationChange)
			{
				await transaction.RecordFileAssociationsIntentAsync(associationChange, cancellationToken);
			}
			foreach (var component in prepared)
			{
				await InstallerJson.WriteAsync(
					Path.Combine(component.StagePath, InstallerConstants.InstallRecordFileName),
					record,
					cancellationToken);
			}

			progress?.Report(new InstallProgress
			{
				Stage = "commit",
				CompletedFiles = completedFiles,
				TotalFiles = totalFiles,
			});
			foreach (var component in prepared)
			{
				await CommitAsync(component, cancellationToken);
			}
			foreach (var removal in preparedRemovals)
			{
				cancellationToken.ThrowIfCancellationRequested();
				await CommitAsync(removal, cancellationToken);
			}
			if (associationPlan.Change is { } pendingAssociations)
			{
				// Registry flushes may block. Keep the wizard responsive so cancellation
				// can be observed between individual registration writes.
				await Task.Run(() => FileAssociationRegistration.Apply(_fileAssociations, pendingAssociations, cancellationToken));
				progress?.Report(new InstallProgress { Stage = "associations", CompletedFiles = completedFiles, TotalFiles = totalFiles });
			}
			await transaction.MarkCommittedAsync(cancellationToken);
			await transaction.FinishCommittedAsync();
			transaction = null;
			return new InstallResult
			{
				InstallationId = record.InstallationId,
				ProductVersion = record.ProductVersion,
				InstallRecordPaths = record.Components
					.Select(component => Path.Combine(component.RootPath, InstallerConstants.InstallRecordFileName))
					.ToArray(),
				PathAdded = record.PathAdded,
				PathEntry = record.PathEntry,
				DefaultApplicationName = FileAssociationRegistration.DefaultApplicationName(record.FileAssociations),
				AssociationSettingsPreserved = associationPlan.PreservedSettings,
			};
		}
		catch
		{
			if (transaction is not null)
			{
				await transaction.TryRollbackAsync(_pathIntegration, _fileAssociations);
				await RollbackPathTransitionAsync(addedPathMutation, removedPathEntry);
				transaction = null;
			}
			else
			{
				for (var index = preparedRemovals.Count - 1; index >= 0; --index)
				{
					await RollbackAsync(preparedRemovals[index]);
				}
				for (var index = prepared.Count - 1; index >= 0; --index)
				{
					await RollbackAsync(prepared[index]);
				}
				await RollbackPathTransitionAsync(addedPathMutation, removedPathEntry);
			}
			throw;
		}
		finally
		{
			if (transaction is not null)
			{
				await transaction.DisposeAsync();
			}
		}
	}

	public async Task<InstallationRecord> LoadRecordAsync(
		string recordPath,
		CancellationToken cancellationToken = default)
	{
		var fullRecordPath = Path.GetFullPath(recordPath);
		var requestedRoot = Path.GetDirectoryName(fullRecordPath)
			?? throw new InvalidDataException("The install record has no parent directory.");
		await InstallerTransaction.RecoverForRootsAsync(
			[requestedRoot], _pathIntegration, cancellationToken, _fileAssociations);
		var record = await InstallerJson.ReadAsync<InstallationRecord>(fullRecordPath, cancellationToken);
		ValidateRecord(record);
		return record;
	}

	public async Task<InstallResult> RepairAsync(
		string recordPath,
		string manifestPath,
		string payloadRoot,
		IProgress<InstallProgress>? progress = null,
		CancellationToken cancellationToken = default)
	{
		var fullRecordPath = Path.GetFullPath(recordPath);
		var record = await LoadRecordAsync(fullRecordPath, cancellationToken);
		await ValidateRecordLocationAndSiblingsAsync(fullRecordPath, record, cancellationToken);
		var manifest = await ManifestValidator.LoadAsync(manifestPath, cancellationToken);
		if (manifest.ProductId != record.ProductId || manifest.ProductVersion != record.ProductVersion
			|| manifest.Platform != record.Platform || manifest.Architecture != record.Architecture)
		{
			throw new InvalidOperationException(
				"Repair requires an installer package with the same product version, platform, and architecture.")
				.WithInstallerError(InstallerError.RepairMismatch);
		}
		if (!ManifestValidator.ComputeFingerprint(manifest).Equals(
				record.ManifestFingerprint, StringComparison.OrdinalIgnoreCase))
		{
			throw new InvalidOperationException("Repair requires the exact installer package used for this installation.")
				.WithInstallerError(InstallerError.RepairMismatch);
		}
		var selected = record.Components.Select(component => component.Id).ToHashSet(StringComparer.Ordinal);
		foreach (var componentId in selected)
		{
			if (!manifest.Components.Any(component => component.Id == componentId))
			{
				throw new InvalidOperationException($"The repair package is missing installed component '{componentId}'.")
					.WithInstallerError(InstallerError.RepairMismatch);
			}
		}
		return await InstallAsync(new InstallOptions
		{
			ManifestPath = manifestPath,
			PayloadRoot = payloadRoot,
			SelectedComponents = selected,
			ComponentDestinations = record.Components.ToDictionary(
					component => component.Id, component => component.RootPath, StringComparer.Ordinal),
			AddToUserPath = record.PathAdded,
			PathAlreadyOwned = record.PathAdded,
			FileAssociations = record.FileAssociations?.Options ?? new(),
		}, progress, cancellationToken);
	}

	public async Task UninstallAsync(string recordPath, CancellationToken cancellationToken = default)
	{
		var fullRecordPath = Path.GetFullPath(recordPath);
		var record = await LoadRecordAsync(fullRecordPath, cancellationToken);
		await ValidateRecordLocationAndSiblingsAsync(fullRecordPath, record, cancellationToken);
		var transactionId = Guid.NewGuid().ToString("N");
		var prepared = new List<PreparedComponent>();
		InstallerTransaction? transaction = null;
		try
		{
			foreach (var component in record.Components)
			{
				prepared.Add(CreatePreparedComponent(
					new InstallerComponent { Id = component.Id },
					component.RootPath,
					component.RootPath,
					transactionId));
			}

			transaction = await InstallerTransaction.BeginAsync(
				transactionId,
				"uninstall",
				prepared.Select(component => ToTransactionComponent(component, removeIfEmpty: true)).ToArray(),
				cancellationToken);
			for (var index = 0; index < record.Components.Count; ++index)
			{
				cancellationToken.ThrowIfCancellationRequested();
				var component = record.Components[index];
				var preparedComponent = prepared[index];
				Directory.CreateDirectory(preparedComponent.StagePath);
				await PreserveUnownedFilesAsync(
					component.RootPath,
					preparedComponent.StagePath,
					component,
					[],
					cancellationToken);
			}
			if (record.PathAdded && !string.IsNullOrWhiteSpace(record.PathEntry))
			{
				await transaction.RecordRemovedPathIntentAsync(record.PathEntry, cancellationToken);
				await _pathIntegration.RemoveFromUserPathAsync(record.PathEntry, cancellationToken);
			}
			var associationPlan = FileAssociationRegistration.Prepare(_fileAssociations, new(), null, record.FileAssociations);
			if (associationPlan.Change is { } associationChange)
			{
				await transaction.RecordFileAssociationsIntentAsync(associationChange, cancellationToken);
			}
			foreach (var component in prepared)
			{
				await CommitAsync(component, cancellationToken);
			}
			if (associationPlan.Change is { } pendingAssociations)
			{
				await Task.Run(() => FileAssociationRegistration.Apply(_fileAssociations, pendingAssociations, cancellationToken));
			}
			await transaction.MarkCommittedAsync(cancellationToken);
			await transaction.FinishCommittedAsync();
			transaction = null;
		}
		catch
		{
			if (transaction is not null)
			{
				await transaction.TryRollbackAsync(_pathIntegration, _fileAssociations);
				transaction = null;
			}
			else
			{
				for (var index = prepared.Count - 1; index >= 0; --index)
				{
					await RollbackAsync(prepared[index]);
				}
			}
			throw;
		}
		finally
		{
			if (transaction is not null)
			{
				await transaction.DisposeAsync();
			}
		}
	}

	private static List<SelectedComponent> ResolveSelectedComponents(InstallerManifest manifest, InstallOptions options)
	{
		if (options.SelectedComponents.Count == 0)
		{
			throw new InvalidOperationException("Select at least one installer component.");
		}
		foreach (var required in manifest.Components.Where(component => component.Required))
		{
			if (!options.SelectedComponents.Contains(required.Id))
			{
				throw new InvalidOperationException($"Required component '{required.Id}' is not selected.");
			}
		}
		foreach (var selectedId in options.SelectedComponents)
		{
			var selectedComponent = manifest.Components.SingleOrDefault(component => component.Id == selectedId)
				?? throw new InvalidOperationException($"The package does not contain component '{selectedId}'.");
			var missingDependency = selectedComponent.Dependencies.FirstOrDefault(
				dependency => !options.SelectedComponents.Contains(dependency));
			if (missingDependency is not null)
			{
				throw new InvalidOperationException(
					$"Component '{selectedId}' requires component '{missingDependency}'.");
			}
		}

		var result = new List<SelectedComponent>();
		foreach (var componentId in options.SelectedComponents.OrderBy(value => value, StringComparer.Ordinal))
		{
			var component = manifest.Components.SingleOrDefault(candidate => candidate.Id == componentId)
				?? throw new InvalidOperationException($"The package does not contain component '{componentId}'.");
			if (!options.ComponentDestinations.TryGetValue(componentId, out var destination)
				|| string.IsNullOrWhiteSpace(destination))
			{
				throw new InvalidOperationException($"No installation path was provided for component '{componentId}'.");
			}
			var files = component.Files.ToArray();
			result.Add(new SelectedComponent(component, files, Path.GetFullPath(destination)));
		}
		return result;
	}

	private static void ValidateDestinationRoots(IEnumerable<string> roots, string protectedPayloadRoot)
	{
		var values = roots.Select(Path.GetFullPath).ToArray();
		var payloadRoot = Path.GetFullPath(protectedPayloadRoot);
		foreach (var value in values)
		{
			var pathRoot = Path.GetPathRoot(value);
			if (string.IsNullOrEmpty(pathRoot) || PathsEqual(value, pathRoot))
			{
				throw new InvalidOperationException("A filesystem root cannot be used as an installation directory.")
					.WithInstallerError(InstallerError.DestinationConflict);
			}
			if (PathsEqual(value, payloadRoot)
				|| PathSafety.IsWithinRoot(value, payloadRoot)
				|| PathSafety.IsWithinRoot(payloadRoot, value))
			{
				throw new InvalidOperationException("Installation directories must not overlap the installer payload.")
					.WithInstallerError(InstallerError.DestinationConflict);
			}
		}
		for (var left = 0; left < values.Length; ++left)
		{
			for (var right = left + 1; right < values.Length; ++right)
			{
				if (PathsEqual(values[left], values[right])
					|| PathSafety.IsWithinRoot(values[left], values[right])
					|| PathSafety.IsWithinRoot(values[right], values[left]))
				{
					throw new InvalidOperationException("Component installation directories must not overlap.")
						.WithInstallerError(InstallerError.DestinationConflict);
				}
			}
		}
	}

	private static async Task<InstallationRecord?> ValidateManagedDestinationAsync(
		string destination,
		string incomingComponentId,
		string incomingProductId,
		string incomingVersion,
		string incomingPlatform,
		string incomingArchitecture,
		bool allowDowngrade,
		IReadOnlyDictionary<string, string> selectedDestinations,
		CancellationToken cancellationToken)
	{
		if (File.Exists(destination))
		{
			throw new IOException($"The installation destination is an existing file: {destination}")
				.WithInstallerError(InstallerError.DestinationConflict);
		}
		if (!Directory.Exists(destination))
		{
			return null;
		}
		PathSafety.EnsureNotLink(new DirectoryInfo(destination));
		var entries = Directory.EnumerateFileSystemEntries(destination).ToArray();
		if (entries.Length == 0)
		{
			return null;
		}
		var marker = Path.Combine(destination, InstallerConstants.InstallRecordFileName);
		if (!File.Exists(marker))
		{
			throw new IOException($"Refusing to replace a non-empty directory not managed by VeroScript: {destination}")
				.WithInstallerError(InstallerError.DestinationConflict);
		}
		var record = await InstallerJson.ReadAsync<InstallationRecord>(marker, cancellationToken);
		ValidateRecord(record);
		if (!record.ProductId.Equals(incomingProductId, StringComparison.Ordinal))
		{
			throw new InvalidOperationException(
				$"The destination belongs to a different product ('{record.ProductId}').")
				.WithInstallerError(InstallerError.WrongProduct);
		}
		if (!record.Platform.Equals(incomingPlatform, StringComparison.Ordinal)
			|| !record.Architecture.Equals(incomingArchitecture, StringComparison.Ordinal))
		{
			throw new InvalidOperationException(
				"The destination belongs to a VeroScript installation for another platform or architecture.")
				.WithInstallerError(InstallerError.WrongPlatform);
		}
		var component = record.Components.SingleOrDefault(item => PathsEqual(item.RootPath, destination))
			?? throw new InvalidDataException("The existing install record does not own its directory.");
		if (!component.Id.Equals(incomingComponentId, StringComparison.Ordinal))
		{
			throw new InvalidOperationException(
				$"The destination is owned by component '{component.Id}', not '{incomingComponentId}'.")
				.WithInstallerError(InstallerError.DestinationConflict);
		}
		await ValidateRecordLocationAndSiblingsAsync(marker, record, cancellationToken);
		if (!allowDowngrade
			&& Version.Parse(record.ProductVersion).CompareTo(Version.Parse(incomingVersion)) > 0)
		{
			throw new InvalidOperationException(
				$"Refusing to downgrade the installed product from {record.ProductVersion} to {incomingVersion}. "
				+ "Use the explicit downgrade option if this is intentional.")
				.WithInstallerError(InstallerError.Downgrade);
		}
		foreach (var installedComponent in record.Components)
		{
			if (!selectedDestinations.TryGetValue(installedComponent.Id, out var selectedDestination)
				|| !PathsEqual(installedComponent.RootPath, selectedDestination))
			{
				throw new InvalidOperationException(
					"All components of an existing VeroScript installation must be replaced together.");
			}
		}
		return record;
	}

	private static void ValidateRelocationRoots(
		InstallationRecord existing,
		IReadOnlyDictionary<string, string> selectedDestinations)
	{
		foreach (var selected in selectedDestinations)
		{
			foreach (var installed in existing.Components)
			{
				if (selected.Key == installed.Id)
				{
					continue;
				}
				if (PathsEqual(selected.Value, installed.RootPath)
					|| PathSafety.IsWithinRoot(selected.Value, installed.RootPath)
					|| PathSafety.IsWithinRoot(installed.RootPath, selected.Value))
				{
					throw new InvalidOperationException(
						"A relocated component destination must not overlap another existing component directory.")
						.WithInstallerError(InstallerError.DestinationConflict);
				}
			}
		}
	}

	private static async Task ValidateRelocationDestinationAsync(
		string existingRoot,
		string destination,
		CancellationToken cancellationToken)
	{
		cancellationToken.ThrowIfCancellationRequested();
		var original = Path.GetFullPath(existingRoot);
		var target = Path.GetFullPath(destination);
		if (PathsEqual(original, target))
		{
			return;
		}
		if (PathSafety.IsWithinRoot(original, target) || PathSafety.IsWithinRoot(target, original))
		{
			throw new InvalidOperationException(
				"A relocated component destination must not overlap its existing installation directory.")
				.WithInstallerError(InstallerError.DestinationConflict);
		}
		if (File.Exists(target))
		{
			throw new IOException($"The installation destination is an existing file: {target}")
				.WithInstallerError(InstallerError.DestinationConflict);
		}
		if (!Directory.Exists(target))
		{
			return;
		}
		PathSafety.EnsureNotLink(new DirectoryInfo(target));
		if (Directory.EnumerateFileSystemEntries(target).Any())
		{
			throw new IOException($"Refusing to move VeroScript into a non-empty directory: {target}")
				.WithInstallerError(InstallerError.DestinationConflict);
		}
		await Task.CompletedTask;
	}

	private static async Task PreserveUnownedFilesAsync(
		string existingRoot,
		string stageRoot,
		InstalledComponentRecord existingComponent,
		IReadOnlyList<InstallerFile> incomingFiles,
		CancellationToken cancellationToken)
	{
		var previouslyOwned = existingComponent.Files.Select(PathSafety.NormalizeRelativePath)
			.ToHashSet(PathSafety.RelativePathComparer);
		previouslyOwned.Add(InstallerConstants.InstallRecordFileName);
		var previouslyOwnedDirectories = new HashSet<string>(PathSafety.RelativePathComparer);
		foreach (var path in previouslyOwned)
		{
			for (var separator = path.LastIndexOf('/'); separator > 0; separator = path.LastIndexOf('/', separator - 1))
			{
				previouslyOwnedDirectories.Add(path[..separator]);
			}
		}
		var incoming = incomingFiles.Select(file => PathSafety.NormalizeRelativePath(file.Path))
			.ToHashSet(PathSafety.RelativePathComparer);

		foreach (var directory in PathSafety.EnumerateDirectoriesWithoutLinks(existingRoot))
		{
			cancellationToken.ThrowIfCancellationRequested();
			var relative = PathSafety.NormalizeRelativePath(Path.GetRelativePath(existingRoot, directory));
			if (previouslyOwnedDirectories.Contains(relative))
			{
				continue;
			}
			Directory.CreateDirectory(PathSafety.ResolveUnderRoot(stageRoot, relative));
		}
		foreach (var source in PathSafety.EnumerateFilesWithoutLinks(existingRoot))
		{
			cancellationToken.ThrowIfCancellationRequested();
			PathSafety.EnsurePathHasNoLinks(existingRoot, source);
			var relative = PathSafety.NormalizeRelativePath(Path.GetRelativePath(existingRoot, source));
			if (previouslyOwned.Contains(relative))
			{
				continue;
			}
			if (incoming.Contains(relative))
			{
				throw new IOException(
					$"Update file '{relative}' conflicts with a user-owned file. Move or rename the user file and retry.")
					.WithInstallerError(InstallerError.DestinationConflict);
			}
			var destination = PathSafety.ResolveUnderRoot(stageRoot, relative);
			Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
			await using var input = new FileStream(source, FileMode.Open, FileAccess.Read, FileShare.Read, 64 * 1024,
				FileOptions.Asynchronous | FileOptions.SequentialScan);
			await using var output = new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None, 64 * 1024,
				FileOptions.Asynchronous | FileOptions.WriteThrough);
			await input.CopyToAsync(output, cancellationToken);
			await output.FlushAsync(cancellationToken);
			if (!OperatingSystem.IsWindows())
			{
				File.SetUnixFileMode(destination, File.GetUnixFileMode(source));
			}
		}
	}

	private static PreparedComponent CreatePreparedComponent(
		InstallerComponent component,
		string destination,
		string? originalDestination,
		string transactionId)
	{
		var parent = Path.GetDirectoryName(destination)
			?? throw new InvalidOperationException($"Installation path has no parent: {destination}");
		Directory.CreateDirectory(parent);
		var leaf = Path.GetFileName(destination.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
		var original = Path.GetFullPath(originalDestination ?? destination);
		var originalParent = Path.GetDirectoryName(original)
			?? throw new InvalidOperationException($"Existing installation path has no parent: {original}");
		var originalLeaf = Path.GetFileName(
			original.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
		return new PreparedComponent(
			component,
			destination,
			original,
			Path.Combine(parent, $".{leaf}.veroscript-stage-{transactionId}"),
			Path.Combine(originalParent, $".{originalLeaf}.veroscript-backup-{transactionId}"),
			Directory.Exists(original));
	}

	private static InstallerTransactionComponent ToTransactionComponent(
		PreparedComponent component,
		bool removeIfEmpty) => new(
		component.Destination,
		component.OriginalDestination,
		component.StagePath,
		component.BackupPath,
		component.OriginalExisted,
		removeIfEmpty);

	private static async Task CopyAndVerifyFileAsync(
		string payloadRoot,
		string stageRoot,
		InstallerFile file,
		CancellationToken cancellationToken)
	{
		var source = PathSafety.ResolveUnderRoot(payloadRoot, file.Path);
		PathSafety.EnsurePathHasNoLinks(payloadRoot, source);
		var sourceInfo = new FileInfo(source);
		if (sourceInfo.Length != file.Size)
		{
			throw new InvalidDataException($"Payload size mismatch: {file.Path}");
		}
		var destination = PathSafety.ResolveUnderRoot(stageRoot, file.Path);
		Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
		await using var input = new FileStream(source, FileMode.Open, FileAccess.Read, FileShare.Read, 64 * 1024,
			FileOptions.Asynchronous | FileOptions.SequentialScan);
		await using var output = new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None, 64 * 1024,
			FileOptions.Asynchronous | FileOptions.WriteThrough);
		using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
		var buffer = new byte[128 * 1024];
		long copied = 0;
		while (true)
		{
			var read = await input.ReadAsync(buffer, cancellationToken);
			if (read == 0)
			{
				break;
			}
			copied = checked(copied + read);
			if (copied > file.Size)
			{
				throw new InvalidDataException($"Payload changed while being installed: {file.Path}");
			}
			hash.AppendData(buffer.AsSpan(0, read));
			await output.WriteAsync(buffer.AsMemory(0, read), cancellationToken);
		}
		await output.FlushAsync(cancellationToken);
		if (copied != file.Size || !Convert.ToHexString(hash.GetHashAndReset()).Equals(file.Sha256, StringComparison.OrdinalIgnoreCase))
		{
			throw new InvalidDataException($"Payload SHA-256 mismatch: {file.Path}");
		}
		if (file.Executable && !OperatingSystem.IsWindows())
		{
			var mode = File.GetUnixFileMode(destination);
			File.SetUnixFileMode(destination, mode | UnixFileMode.UserExecute | UnixFileMode.GroupExecute | UnixFileMode.OtherExecute);
		}
	}

	private static InstallationRecord CreateRecord(
		InstallerManifest manifest,
		IReadOnlyList<SelectedComponent> selected,
		bool pathOwned,
		string? pathEntry)
	{
		return new InstallationRecord
		{
			InstallationId = Guid.NewGuid().ToString("D"),
			ProductId = manifest.ProductId,
			ProductName = manifest.ProductName,
			LocalizedNames = new Dictionary<string, string>(manifest.LocalizedNames, StringComparer.OrdinalIgnoreCase),
			ProductVersion = manifest.ProductVersion,
			Platform = manifest.Platform,
			Architecture = manifest.Architecture,
			ManifestFingerprint = ManifestValidator.ComputeFingerprint(manifest),
			InstalledAtUtc = DateTimeOffset.UtcNow,
			PathAdded = pathOwned,
			PathEntry = pathOwned ? pathEntry : null,
			PathComponentId = pathOwned ? manifest.PathIntegration?.ComponentId : null,
			Components = selected.Select(item => new InstalledComponentRecord
			{
				Id = item.Component.Id,
				RootPath = item.Destination,
				Files = item.Files.Select(file => PathSafety.NormalizeRelativePath(file.Path)).ToList(),
			}).ToList(),
		};
	}

	private static async Task CommitAsync(PreparedComponent component, CancellationToken cancellationToken)
	{
		if (!PathsEqual(component.OriginalDestination, component.Destination)
			&& Directory.Exists(component.Destination))
		{
			if (Directory.EnumerateFileSystemEntries(component.Destination).Any())
			{
				throw new IOException(
					$"The relocation destination became non-empty: {component.Destination}");
			}
			Directory.Delete(component.Destination);
		}
		if (Directory.Exists(component.OriginalDestination))
		{
			await FileSystemRetry.MoveDirectoryAsync(
				component.OriginalDestination, component.BackupPath, cancellationToken);
			component.BackupMoved = true;
		}
		await FileSystemRetry.MoveDirectoryAsync(component.StagePath, component.Destination, cancellationToken);
		component.Committed = true;
	}

	private static async Task RollbackAsync(PreparedComponent component)
	{
		try
		{
			if (component.Committed && Directory.Exists(component.Destination))
			{
				await FileSystemRetry.DeleteDirectoryAsync(component.Destination, CancellationToken.None);
			}
			if (component.BackupMoved && Directory.Exists(component.BackupPath)
				&& !Directory.Exists(component.OriginalDestination))
			{
				await FileSystemRetry.MoveDirectoryAsync(
					component.BackupPath, component.OriginalDestination, CancellationToken.None);
			}
		}
		catch
		{
			// Continue restoring the other component. A retained backup remains recoverable.
		}
	}

	private async Task RollbackPathTransitionAsync(
		PathMutation? addedPathMutation,
		string? removedPathEntry)
	{
		if (removedPathEntry is not null)
		{
			try
			{
				await _pathIntegration.AddToUserPathAsync(removedPathEntry, CancellationToken.None);
			}
			catch
			{
				// Continue undoing a newly added entry. PATH restoration is best effort.
			}
		}
		if (addedPathMutation is not null)
		{
			try
			{
				await _pathIntegration.UndoAsync(addedPathMutation, CancellationToken.None);
			}
			catch
			{
				// Preserve the original installation error. PATH rollback is best effort.
			}
		}
	}

	private static void ValidateRecord(InstallationRecord record)
	{
		if (record is null
			|| record.SchemaVersion != InstallerConstants.InstallRecordSchemaVersion
			|| string.IsNullOrWhiteSpace(record.ProductId)
			|| record.ProductId.Length > 128
			|| string.IsNullOrWhiteSpace(record.ProductName)
			|| record.ProductName.Length > 128
			|| record.LocalizedNames is null
			|| !Guid.TryParse(record.InstallationId, out _)
			|| !Version.TryParse(record.ProductVersion, out _)
			|| record.ManifestFingerprint is null
			|| record.ManifestFingerprint.Length != 64
			|| !record.ManifestFingerprint.All(Uri.IsHexDigit)
			|| record.Components is null
			|| record.Components.Count == 0)
		{
			throw new InvalidDataException("Invalid VeroScript installation record.");
		}
		ValidateRecordLocalizedNames(record.LocalizedNames);
		var ids = new HashSet<string>(StringComparer.Ordinal);
		var roots = new List<string>();
		var totalFiles = 0;
		foreach (var component in record.Components)
		{
			if (component is null || component.Files is null
				|| string.IsNullOrWhiteSpace(component.Id)
				|| component.Id.Length > 64
				|| !ids.Add(component.Id) || string.IsNullOrWhiteSpace(component.RootPath)
				|| !Path.IsPathRooted(component.RootPath))
			{
				throw new InvalidDataException("Invalid component in VeroScript installation record.");
			}
			var fullRoot = Path.GetFullPath(component.RootPath);
			if (PathsEqual(fullRoot, Path.GetPathRoot(fullRoot)!))
			{
				throw new InvalidDataException("An installation record cannot own a filesystem root.");
			}
			if (roots.Any(root => PathsEqual(root, fullRoot)
				|| PathSafety.IsWithinRoot(root, fullRoot)
				|| PathSafety.IsWithinRoot(fullRoot, root)))
			{
				throw new InvalidDataException("Installation record component roots overlap.");
			}
			roots.Add(fullRoot);
			var files = new HashSet<string>(PathSafety.RelativePathComparer);
			foreach (var file in component.Files)
			{
				var normalized = PathSafety.NormalizeRelativePath(file);
				if (normalized.Equals(InstallerConstants.InstallRecordFileName, StringComparison.OrdinalIgnoreCase)
					|| !files.Add(normalized))
				{
					throw new InvalidDataException("Duplicate file in VeroScript installation record.");
				}
			}
			totalFiles = checked(totalFiles + component.Files.Count);
			if (totalFiles > InstallerConstants.MaximumFileCount)
			{
				throw new InvalidDataException("The installation record contains too many files.");
			}
		}
		if (record.PathAdded)
		{
			var pathComponent = record.Components.SingleOrDefault(
				component => component.Id == record.PathComponentId);
			if (pathComponent is null || string.IsNullOrWhiteSpace(record.PathEntry)
				|| (!PathsEqual(pathComponent.RootPath, record.PathEntry)
					&& !PathSafety.IsWithinRoot(record.PathEntry, pathComponent.RootPath)))
			{
				throw new InvalidDataException("The installation record has invalid PATH ownership metadata.");
			}
		}
		else if (record.PathEntry is not null || record.PathComponentId is not null)
		{
			throw new InvalidDataException("The installation record has an unowned PATH entry.");
		}
		FileAssociationRegistration.ValidateRecord(record);
	}

	private static async Task ValidateRecordLocationAndSiblingsAsync(
		string recordPath,
		InstallationRecord record,
		CancellationToken cancellationToken)
	{
		if (!Path.GetFileName(recordPath).Equals(
				InstallerConstants.InstallRecordFileName,
				OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal))
		{
			throw new InvalidDataException("The installation record has an unexpected filename.");
		}
		var recordRoot = Path.GetDirectoryName(recordPath)
			?? throw new InvalidDataException("The install record has no parent directory.");
		if (!record.Components.Any(component => PathsEqual(component.RootPath, recordRoot)))
		{
			throw new InvalidDataException("The install record is not located in one of its component roots.");
		}

		foreach (var component in record.Components)
		{
			var root = Path.GetFullPath(component.RootPath);
			var siblingRecordPath = Path.Combine(root, InstallerConstants.InstallRecordFileName);
			var siblingRecord = await InstallerJson.ReadAsync<InstallationRecord>(siblingRecordPath, cancellationToken);
			ValidateRecord(siblingRecord);
			if (!RecordsDescribeSameInstallation(record, siblingRecord))
			{
				throw new InvalidDataException($"Component '{component.Id}' belongs to a different installation.");
			}
		}
	}

	private static bool RecordsDescribeSameInstallation(InstallationRecord left, InstallationRecord right)
	{
		if (left.InstallationId != right.InstallationId
			|| left.ProductId != right.ProductId
			|| left.ProductName != right.ProductName
			|| !LocalizedNamesEqual(left.LocalizedNames, right.LocalizedNames)
			|| left.ProductVersion != right.ProductVersion
			|| left.Platform != right.Platform
			|| left.Architecture != right.Architecture
			|| !left.ManifestFingerprint.Equals(right.ManifestFingerprint, StringComparison.OrdinalIgnoreCase)
			|| left.InstalledAtUtc != right.InstalledAtUtc
			|| left.PathAdded != right.PathAdded
			|| !OptionalPathsEqual(left.PathEntry, right.PathEntry)
			|| left.PathComponentId != right.PathComponentId
			|| System.Text.Json.JsonSerializer.Serialize(left.FileAssociations, InstallerJson.Options)
				!= System.Text.Json.JsonSerializer.Serialize(right.FileAssociations, InstallerJson.Options)
			|| left.Components.Count != right.Components.Count)
		{
			return false;
		}
		foreach (var component in left.Components)
		{
			var other = right.Components.SingleOrDefault(candidate => candidate.Id == component.Id);
			if (other is null || !PathsEqual(component.RootPath, other.RootPath)
				|| !component.Files.ToHashSet(PathSafety.RelativePathComparer)
					.SetEquals(other.Files.Select(PathSafety.NormalizeRelativePath)))
			{
				return false;
			}
		}
		return true;
	}

	private static bool OptionalPathsEqual(string? left, string? right) =>
		left is null || right is null ? left is null && right is null : PathsEqual(left, right);

	private static void ValidateRecordLocalizedNames(IReadOnlyDictionary<string, string> names)
	{
		if (names.Count > 128)
		{
			throw new InvalidDataException("The installation record has too many localized product names.");
		}

		foreach (var pair in names)
		{
			if (string.IsNullOrWhiteSpace(pair.Key) || pair.Key.Length > 64
				|| string.IsNullOrWhiteSpace(pair.Value) || pair.Value.Length > 128)
			{
				throw new InvalidDataException("The installation record has invalid localized product names.");
			}
		}
	}

	private static bool LocalizedNamesEqual(
		IReadOnlyDictionary<string, string> left,
		IReadOnlyDictionary<string, string> right) =>
		left.Count == right.Count && left.All(pair => right.TryGetValue(pair.Key, out var value) && value == pair.Value);

	private static bool PathsEqual(string left, string right) => string.Equals(
		Path.GetFullPath(left).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
		Path.GetFullPath(right).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
		OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);

	private sealed record SelectedComponent(
		InstallerComponent Component,
		IReadOnlyList<InstallerFile> Files,
		string Destination);

	private sealed class PreparedComponent(
		InstallerComponent component,
		string destination,
		string originalDestination,
		string stagePath,
		string backupPath,
		bool originalExisted)
	{
		public InstallerComponent Component { get; } = component;
		public string Destination { get; } = destination;
		public string OriginalDestination { get; } = originalDestination;
		public string StagePath { get; } = stagePath;
		public string BackupPath { get; } = backupPath;
		public bool OriginalExisted { get; } = originalExisted;
		public bool BackupMoved { get; set; }
		public bool Committed { get; set; }
	}

}
