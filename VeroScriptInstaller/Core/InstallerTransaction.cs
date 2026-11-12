// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScriptInstaller.Core.Platform;

namespace VeroScriptInstaller.Core;

internal sealed record InstallerTransactionComponent(
	string Destination,
	string OriginalDestination,
	string StagePath,
	string BackupPath,
	bool OriginalExisted,
	bool RemoveDestinationIfEmpty = false);

internal sealed class InstallerTransaction : IAsyncDisposable
{
	private const int JournalSchemaVersion = 1;
	private const string PreparedState = "prepared";
	private const string CommittedState = "committed";
	private const string RolledBackState = "rolledback";
	private readonly FileStream[] _lockStreams;
	private readonly string[] _journalPaths;
	private bool _disposed;

	private InstallerTransaction(
		TransactionJournal journal,
		FileStream[] lockStreams,
		string[] journalPaths)
	{
		Journal = journal;
		_lockStreams = lockStreams;
		_journalPaths = journalPaths;
	}

	private TransactionJournal Journal { get; }

	internal static async Task<InstallerTransaction> BeginAsync(
		string transactionId,
		string operation,
		IReadOnlyList<InstallerTransactionComponent> components,
		CancellationToken cancellationToken)
	{
		if (!Guid.TryParseExact(transactionId, "N", out _)
			|| operation is not ("install" or "uninstall")
			|| components.Count == 0)
		{
			throw new ArgumentException("Invalid installer transaction metadata.");
		}
		var entries = components.Select(component => new TransactionEntry
		{
			Destination = Path.GetFullPath(component.Destination),
			OriginalDestination = Path.GetFullPath(component.OriginalDestination),
			StagePath = Path.GetFullPath(component.StagePath),
			BackupPath = Path.GetFullPath(component.BackupPath),
			OriginalExisted = component.OriginalExisted,
			RemoveDestinationIfEmpty = component.RemoveDestinationIfEmpty,
		}).ToList();
		var journal = new TransactionJournal
		{
			TransactionId = transactionId,
			Operation = operation,
			State = PreparedState,
			Entries = entries,
		};
		Validate(journal);
		var journalPaths = GetJournalPaths(journal);
		var lockPaths = GetLockPaths(journal);
		var lockStreams = AcquireLocks(lockPaths);
		var transaction = new InstallerTransaction(journal, lockStreams, journalPaths);
		try
		{
			await transaction.SaveAsync(cancellationToken);
			return transaction;
		}
		catch
		{
			foreach (var journalPath in journalPaths)
			{
				await FileSystemRetry.TryDeleteFileAsync(journalPath);
			}
			await transaction.DisposeAsync();
			throw;
		}
	}

	internal async Task RecordAddedPathIntentAsync(string path, CancellationToken cancellationToken)
	{
		Journal.AddedPathEntry = Path.GetFullPath(path);
		await SaveAsync(cancellationToken);
	}

	internal async Task RecordRemovedPathIntentAsync(string path, CancellationToken cancellationToken)
	{
		Journal.RemovedPathEntry = Path.GetFullPath(path);
		await SaveAsync(cancellationToken);
	}

	internal async Task RecordFileAssociationsIntentAsync(FileAssociationChange change, CancellationToken cancellationToken)
	{
		FileAssociationRegistration.ValidateChange(change);
		Journal.FileAssociations = change;
		await SaveAsync(cancellationToken);
	}

	internal async Task MarkCommittedAsync(CancellationToken cancellationToken)
	{
		await PersistTerminalStateAsync(
			Journal, _journalPaths, CommittedState, cancellationToken);
	}

	private static async Task PersistTerminalStateAsync(
		TransactionJournal journal,
		IReadOnlyList<string> journalPaths,
		string state,
		CancellationToken cancellationToken)
	{
		var previousState = journal.State;
		journal.State = state;
		Validate(journal);
		try
		{
			// The first, deterministically ordered copy is the canonical
			// durable terminal marker and is always deleted last.
			await InstallerJson.WriteAtomicAsync(journalPaths[0], journal, cancellationToken);
		}
		catch
		{
			journal.State = previousState;
			throw;
		}
		foreach (var path in journalPaths.Skip(1))
		{
			try
			{
				await InstallerJson.WriteAtomicAsync(path, journal, CancellationToken.None);
			}
			catch
			{
				// The canonical terminal copy remains authoritative. Recovery
				// reconciles it with any older prepared sibling copy.
			}
		}
	}

	internal async Task FinishCommittedAsync()
	{
		try
		{
			await FinalizeCommittedAsync(Journal);
			await DeleteJournalsAsync(Journal);
		}
		catch
		{
			// The live generation is already committed. Retain any surviving
			// journal so a later run can finish non-critical cleanup.
		}
		finally
		{
			await DisposeAsync();
		}
	}

	internal async Task TryRollbackAsync(IPathIntegration pathIntegration, IFileAssociationRegistry? fileAssociations = null)
	{
		try
		{
			if (Journal.State == CommittedState)
			{
				await FinalizeCommittedAsync(Journal);
			}
			else if (Journal.State == PreparedState)
			{
				await RollbackAsync(Journal, pathIntegration, fileAssociations ?? FileAssociationRegistryFactory.Create());
				await PersistTerminalStateAsync(
					Journal, _journalPaths, RolledBackState, CancellationToken.None);
			}
			await DeleteJournalsAsync(Journal);
			await DisposeAsync();
		}
		catch
		{
			await DisposeAsync();
			// Keep the journal and any backup for recovery by the next run.
		}
	}

	internal static async Task RecoverForRootsAsync(
		IEnumerable<string> roots,
		IPathIntegration pathIntegration,
		CancellationToken cancellationToken,
		IFileAssociationRegistry? fileAssociations = null)
	{
		var journalFiles = roots.Select(Path.GetFullPath)
			.Select(Path.GetDirectoryName)
			.Where(parent => parent is not null && Directory.Exists(parent))
			.Distinct(TransactionPathComparer)
			.SelectMany(parent => Directory.EnumerateFiles(parent!, ".veroscript-transaction-*.json",
				SearchOption.TopDirectoryOnly))
			.Distinct(TransactionPathComparer)
			.ToArray();
		var recovered = new HashSet<string>(StringComparer.Ordinal);
		foreach (var journalPath in journalFiles)
		{
			cancellationToken.ThrowIfCancellationRequested();
			if (!File.Exists(journalPath))
			{
				continue;
			}
			var journal = await LoadAuthoritativeJournalAsync(journalPath, cancellationToken);
			if (!recovered.Add(journal.TransactionId))
			{
				continue;
			}
			var lockPaths = GetLockPaths(journal);
			var lockStreams = AcquireLocks(lockPaths);
			try
			{
				if (journal.State == CommittedState)
				{
					await FinalizeCommittedAsync(journal);
				}
				else if (journal.State == PreparedState)
				{
					await RollbackAsync(journal, pathIntegration, fileAssociations ?? FileAssociationRegistryFactory.Create());
					await PersistTerminalStateAsync(
						journal, GetJournalPaths(journal), RolledBackState, CancellationToken.None);
				}
				await DeleteJournalsAsync(journal);
			}
			finally
			{
				foreach (var stream in lockStreams)
				{
					await stream.DisposeAsync();
				}
			}
		}
	}

	private async Task SaveAsync(CancellationToken cancellationToken)
	{
		Validate(Journal);
		foreach (var path in _journalPaths)
		{
			await InstallerJson.WriteAtomicAsync(path, Journal, cancellationToken);
		}
	}

	private static async Task<TransactionJournal> LoadAuthoritativeJournalAsync(
		string journalPath,
		CancellationToken cancellationToken)
	{
		var journal = await InstallerJson.ReadAsync<TransactionJournal>(journalPath, cancellationToken);
		Validate(journal);
		var expectedPaths = GetJournalPaths(journal);
		if (!expectedPaths.Any(path => PathsEqual(path, journalPath)))
		{
			throw new InvalidDataException("Installer transaction journal is not stored beside a protected root.");
		}
		foreach (var copyPath in expectedPaths)
		{
			if (!File.Exists(copyPath))
			{
				if (journal.State is CommittedState or RolledBackState)
				{
					// Terminal cleanup may have removed some copies before a
					// transient failure. A surviving durable terminal marker
					// is authoritative and cannot replay the operation.
					continue;
				}
				throw new InvalidDataException("Installer transaction journal is missing a protected-root copy.");
			}
			if (PathsEqual(copyPath, journalPath))
			{
				continue;
			}
			var copy = await InstallerJson.ReadAsync<TransactionJournal>(copyPath, cancellationToken);
			Validate(copy);
			if (!JournalsDescribeSameTransaction(journal, copy))
			{
				throw new InvalidDataException("Installer transaction journal copies disagree on protected paths.");
			}
			journal.State = MergeState(journal.State, copy.State);
			journal.AddedPathEntry = MergeOptionalPath(
				journal.AddedPathEntry, copy.AddedPathEntry, "added PATH entry");
			journal.RemovedPathEntry = MergeOptionalPath(
				journal.RemovedPathEntry, copy.RemovedPathEntry, "removed PATH entry");
			if (journal.FileAssociations is null)
			{
				journal.FileAssociations = copy.FileAssociations;
			}
			else if (copy.FileAssociations is not null
				&& System.Text.Json.JsonSerializer.Serialize(journal.FileAssociations, InstallerJson.Options)
					!= System.Text.Json.JsonSerializer.Serialize(copy.FileAssociations, InstallerJson.Options))
			{
				throw new InvalidDataException("Installer transaction journal copies disagree on file associations.");
			}
		}
		return journal;
	}

	private static bool JournalsDescribeSameTransaction(TransactionJournal left, TransactionJournal right)
	{
		if (left.TransactionId != right.TransactionId
			|| left.Operation != right.Operation
			|| left.Entries.Count != right.Entries.Count)
		{
			return false;
		}
		for (var index = 0; index < left.Entries.Count; ++index)
		{
			var leftEntry = left.Entries[index];
			var rightEntry = right.Entries[index];
			if (!PathsEqual(leftEntry.Destination, rightEntry.Destination)
				|| !PathsEqual(leftEntry.OriginalDestination, rightEntry.OriginalDestination)
				|| !PathsEqual(leftEntry.StagePath, rightEntry.StagePath)
				|| !PathsEqual(leftEntry.BackupPath, rightEntry.BackupPath)
				|| leftEntry.OriginalExisted != rightEntry.OriginalExisted
				|| leftEntry.RemoveDestinationIfEmpty != rightEntry.RemoveDestinationIfEmpty)
			{
				return false;
			}
		}
		return true;
	}

	private static string? MergeOptionalPath(string? left, string? right, string field)
	{
		if (left is null)
		{
			return right;
		}
		if (right is null || PathsEqual(left, right))
		{
			return left;
		}
		throw new InvalidDataException($"Installer transaction journal copies disagree on {field}.");
	}

	private static string MergeState(string left, string right)
	{
		if ((left == CommittedState && right == RolledBackState)
			|| (left == RolledBackState && right == CommittedState))
		{
			throw new InvalidDataException("Installer transaction journal copies disagree on the terminal state.");
		}
		if (left == CommittedState || right == CommittedState)
		{
			return CommittedState;
		}
		return left == RolledBackState || right == RolledBackState
			? RolledBackState
			: PreparedState;
	}

	private static async Task RollbackAsync(TransactionJournal journal, IPathIntegration pathIntegration, IFileAssociationRegistry fileAssociations)
	{
		if (journal.FileAssociations is { } associations)
		{
			await Task.Run(() => FileAssociationRegistration.Undo(fileAssociations, associations, notify: false));
		}
		foreach (var entry in journal.Entries.AsEnumerable().Reverse())
		{
			var stageStillExists = Directory.Exists(entry.StagePath);
			if (Directory.Exists(entry.BackupPath))
			{
				if (!stageStillExists && Directory.Exists(entry.Destination))
				{
					await FileSystemRetry.DeleteDirectoryAsync(entry.Destination, CancellationToken.None);
				}
				if (Directory.Exists(entry.OriginalDestination))
				{
					throw new IOException(
						$"Cannot restore installer backup because its original path is occupied: {entry.OriginalDestination}");
				}
				await FileSystemRetry.MoveDirectoryAsync(
					entry.BackupPath, entry.OriginalDestination, CancellationToken.None);
			}
			else if (!entry.OriginalExisted && !stageStillExists && Directory.Exists(entry.Destination))
			{
				await FileSystemRetry.DeleteDirectoryAsync(entry.Destination, CancellationToken.None);
			}
			if (Directory.Exists(entry.StagePath))
			{
				await FileSystemRetry.DeleteDirectoryAsync(entry.StagePath, CancellationToken.None);
			}
		}
		if (journal.RemovedPathEntry is not null)
		{
			await pathIntegration.AddToUserPathAsync(journal.RemovedPathEntry, CancellationToken.None);
		}
		if (journal.AddedPathEntry is not null)
		{
			await pathIntegration.RemoveFromUserPathAsync(journal.AddedPathEntry, CancellationToken.None);
		}
		// Relocation may restore icons to a different root. Notify only when the
		// registry references and their original files are both usable again.
		if (journal.FileAssociations is { Values.Count: > 0 })
		{
			await Task.Run(fileAssociations.NotifyChanged);
		}
	}

	private static async Task FinalizeCommittedAsync(TransactionJournal journal)
	{
		foreach (var entry in journal.Entries)
		{
			if (Directory.Exists(entry.StagePath))
			{
				await FileSystemRetry.DeleteDirectoryAsync(entry.StagePath, CancellationToken.None);
			}
			if (Directory.Exists(entry.BackupPath))
			{
				await FileSystemRetry.DeleteDirectoryAsync(entry.BackupPath, CancellationToken.None);
			}
			if (entry.RemoveDestinationIfEmpty
				&& Directory.Exists(entry.Destination)
				&& !Directory.EnumerateFileSystemEntries(entry.Destination).Any())
			{
				Directory.Delete(entry.Destination);
			}
		}
	}

	private static void Validate(TransactionJournal journal)
	{
		if (journal.SchemaVersion != JournalSchemaVersion
			|| !Guid.TryParseExact(journal.TransactionId, "N", out _)
			|| journal.Operation is not ("install" or "uninstall")
			|| journal.State is not (PreparedState or CommittedState or RolledBackState)
			|| journal.Entries is null || journal.Entries.Count is < 1 or > 128)
		{
			throw new InvalidDataException("Invalid installer transaction journal.");
		}
		if (journal.FileAssociations is not null)
		{
			FileAssociationRegistration.ValidateChange(journal.FileAssociations);
		}
		foreach (var entry in journal.Entries)
		{
			ValidateTransactionPath(entry.Destination);
			ValidateTransactionPath(entry.OriginalDestination);
			ValidateTransactionPath(entry.StagePath);
			ValidateTransactionPath(entry.BackupPath);
			var destinationParent = Path.GetDirectoryName(entry.Destination)!;
			var originalParent = Path.GetDirectoryName(entry.OriginalDestination)!;
			var destinationLeaf = Path.GetFileName(entry.Destination.TrimEnd(
				Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
			var originalLeaf = Path.GetFileName(entry.OriginalDestination.TrimEnd(
				Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
			if (!PathsEqual(destinationParent, Path.GetDirectoryName(entry.StagePath)!)
				|| !PathsEqual(originalParent, Path.GetDirectoryName(entry.BackupPath)!)
				|| Path.GetFileName(entry.StagePath) != $".{destinationLeaf}.veroscript-stage-{journal.TransactionId}"
				|| Path.GetFileName(entry.BackupPath) != $".{originalLeaf}.veroscript-backup-{journal.TransactionId}")
			{
				throw new InvalidDataException("Installer transaction paths do not match their protected roots.");
			}
		}
	}

	private static void ValidateTransactionPath(string path)
	{
		if (string.IsNullOrWhiteSpace(path) || !Path.IsPathRooted(path))
		{
			throw new InvalidDataException("Installer transaction contains a non-absolute path.");
		}
		var fullPath = Path.GetFullPath(path);
		if (PathsEqual(fullPath, Path.GetPathRoot(fullPath)!))
		{
			throw new InvalidDataException("Installer transaction cannot target a filesystem root.");
		}
	}

	private static string[] GetJournalPaths(TransactionJournal journal) => journal.Entries
		.SelectMany(entry => new[] { Path.GetDirectoryName(entry.StagePath)!, Path.GetDirectoryName(entry.BackupPath)! })
		.Distinct(TransactionPathComparer)
		.OrderBy(path => path, TransactionPathComparer)
		.Select(parent => Path.Combine(parent, $".veroscript-transaction-{journal.TransactionId}.json"))
		.ToArray();

	private static string[] GetLockPaths(TransactionJournal journal) => GetJournalPaths(journal)
		.Select(Path.GetDirectoryName)
		.Select(parent => Path.Combine(parent!, ".veroscript-installer.lock"))
		.ToArray();

	private static FileStream[] AcquireLocks(IEnumerable<string> lockPaths)
	{
		var streams = new List<FileStream>();
		try
		{
			foreach (var lockPath in lockPaths)
			{
				Directory.CreateDirectory(Path.GetDirectoryName(lockPath)!);
				streams.Add(new FileStream(
					lockPath, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None));
			}
			return streams.ToArray();
		}
		catch (IOException exception)
		{
			foreach (var stream in streams)
			{
				stream.Dispose();
			}
			throw new InvalidOperationException(
				"Another installer process is already modifying this installation.", exception)
				.WithInstallerError(InstallerError.Busy);
		}
		catch
		{
			foreach (var stream in streams)
			{
				stream.Dispose();
			}
			throw;
		}
	}

	private static async Task DeleteJournalsAsync(TransactionJournal journal)
	{
		var paths = GetJournalPaths(journal);
		foreach (var path in paths.Skip(1).Append(paths[0]))
		{
			await FileSystemRetry.DeleteFileAsync(path, CancellationToken.None);
		}
	}

	public ValueTask DisposeAsync()
	{
		if (!_disposed)
		{
			_disposed = true;
			foreach (var stream in _lockStreams)
			{
				stream.Dispose();
			}
		}
		return ValueTask.CompletedTask;
	}

	private static bool PathsEqual(string left, string right) => string.Equals(
		Path.GetFullPath(left).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
		Path.GetFullPath(right).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
		OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);

	private static StringComparer TransactionPathComparer => OperatingSystem.IsWindows()
		? StringComparer.OrdinalIgnoreCase
		: StringComparer.Ordinal;

	private sealed class TransactionJournal
	{
		public int SchemaVersion { get; set; } = JournalSchemaVersion;
		public string TransactionId { get; set; } = string.Empty;
		public string Operation { get; set; } = string.Empty;
		public string State { get; set; } = PreparedState;
		public List<TransactionEntry> Entries { get; set; } = [];
		public string? AddedPathEntry { get; set; }
		public string? RemovedPathEntry { get; set; }
		public FileAssociationChange? FileAssociations { get; set; }
	}

	private sealed class TransactionEntry
	{
		public string Destination { get; set; } = string.Empty;
		public string OriginalDestination { get; set; } = string.Empty;
		public string StagePath { get; set; } = string.Empty;
		public string BackupPath { get; set; } = string.Empty;
		public bool OriginalExisted { get; set; }
		public bool RemoveDestinationIfEmpty { get; set; }
	}
}
