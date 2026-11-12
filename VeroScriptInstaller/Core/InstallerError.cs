// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScriptInstaller.Core;

/// <summary>Stable, language-independent categories for expected installer failures.</summary>
public enum InstallerError
{
	WrongProduct,
	WrongPlatform,
	Downgrade,
	RepairMismatch,
	DestinationConflict,
	Busy,
}

public static class InstallerErrors
{
	private const string DataKey = "VeroScriptInstaller.Error";

	// Retain existing exception types and technical messages for CLI consumers and diagnostics.
	public static T WithInstallerError<T>(this T exception, InstallerError error) where T : Exception
	{
		exception.Data[DataKey] = error;
		return exception;
	}

	public static InstallerError? GetCode(Exception exception) =>
		exception.Data[DataKey] is InstallerError error ? error : null;
}
