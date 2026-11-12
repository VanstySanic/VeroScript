#Requires -Version 5.1
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

<# Package VeroScript documentation as a reusable zip archive. #>
[CmdletBinding()]
param(
	[ValidatePattern('^\d+\.\d+\.\d+$')]
	[string]$Version = '',
	[string]$OutRoot = '',
	[string]$SourceRoot = "",
	[ValidateSet('All', 'User')]
	[string]$Audience = 'All'
)

$ErrorActionPreference = "Stop"
$Audience = if ($Audience -ieq 'User') {
	'User'
}
else
{
	'All'
}
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
. (Join-Path $PSScriptRoot 'veroscript-assets.ps1')
. (Join-Path $PSScriptRoot 'veroscript-output.ps1')
$ReleaseConfiguration = Get-VeroScriptReleaseConfiguration $RepoRoot
$ReleaseBusiness = 'Resources'
if (-not $ReleaseConfiguration.businesses.PSObject.Properties[$ReleaseBusiness]) {
	throw "Unknown release business: $ReleaseBusiness"
}
if (-not $Version) {
	$Version = $ReleaseConfiguration.businesses.$ReleaseBusiness
}
if (-not [string]::IsNullOrWhiteSpace($OutRoot)) {
	Assert-VeroScriptComponentOutputPath $OutRoot $RepoRoot
}
if ([string]::IsNullOrWhiteSpace($OutRoot)) {
	# Standalone documentation never publishes into the managed product collection.
	$PublicationView = if ($SourceRoot)
	{
		'Custom'
	}
	else
	{
		$Audience
	}
	$OutRoot = "dist/docs/$PublicationView/$Version"
}
$PackageName = "VeroScriptDocs-$Version"
$OutRootPath = if ([IO.Path]::IsPathRooted($OutRoot))
{
	[IO.Path]::GetFullPath($OutRoot)
}
else
{
	[IO.Path]::GetFullPath((Join-Path $RepoRoot $OutRoot))
}
$PackageDir = Join-Path $OutRootPath $PackageName
Assert-VeroScriptComponentOutputPath $OutRootPath $RepoRoot
$ZipPath = "$PackageDir.zip"

if ($SourceRoot -and -not (Test-Path -LiteralPath $SourceRoot -PathType Container))
{
	throw "Documentation source directory not found: $SourceRoot"
}
if ($SourceRoot) {
	if ($PSBoundParameters.ContainsKey('Audience')) {
		throw '-Audience selects the default integrated collection; custom -SourceRoot exports its supplied tree.'
	}
	$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
	$SourcePath = [IO.Path]::GetFullPath($SourceRoot).TrimEnd('\', '/')
	$OutputPath = $OutRootPath.TrimEnd('\', '/')
	$Comparison = [StringComparison]::OrdinalIgnoreCase
	$Separator = [IO.Path]::DirectorySeparatorChar
	if ($SourcePath.Equals($OutputPath, $Comparison) -or
		$SourcePath.StartsWith(($OutputPath + $Separator), $Comparison) -or
		$OutputPath.StartsWith(($SourcePath + $Separator), $Comparison)) {
		throw 'Documentation source and output directories must not overlap.'
	}
}

$StageRoot = Join-Path $RepoRoot ('.tmp/docs-package-' + [guid]::NewGuid().ToString('N'))
Assert-VeroScriptComponentOutputPath $StageRoot $RepoRoot
$StagedPackage = Join-Path $StageRoot $PackageName
$StagedZip = "$StagedPackage.zip"
try
{
	$DocsDirectory = Join-Path $StagedPackage 'docs'
	if ($SourceRoot) {
		Invoke-VeroScriptDocs @('check-tree', '--output', $SourceRoot, '--structure-only')
		New-Item -ItemType Directory -Path $DocsDirectory -Force | Out-Null
		Get-ChildItem -LiteralPath $SourceRoot -Force | ForEach-Object {
			Copy-Item -LiteralPath $_.FullName -Destination $DocsDirectory -Recurse -Force
		}
		ConvertTo-VeroScriptPortableAssetTree $DocsDirectory -SourceRoot $SourceRoot
	}
	else
	{
		Invoke-VeroScriptDocs @('publish', '--audience', $Audience, '--output', $DocsDirectory)
		Invoke-VeroScriptDocs @('verify-publication', '--audience', $Audience, '--output', $DocsDirectory)
	}
	Get-ChildItem -LiteralPath $StagedPackage -Directory -Recurse -Force -ErrorAction SilentlyContinue |
		Where-Object { $_.Name -in @('.veroscript', '.verocache') } | Sort-Object FullName -Descending | Remove-Item -Recurse -Force
	Invoke-VeroScriptDocs @('check-tree', '--output', $DocsDirectory)
	# zipfile includes hidden linked assets that Compress-Archive omits.
	Invoke-VeroScriptDocs @('archive', '--source', $StagedPackage, '--output', $StagedZip)

	# Prepare and validate before replacing the previously generated package.
	Assert-VeroScriptComponentOutputPath $OutRootPath $RepoRoot
	New-Item -ItemType Directory -Path $OutRootPath -Force | Out-Null
	$BackupRoot = Join-Path $OutRootPath ('.docs-backup-' + [guid]::NewGuid().ToString('N'))
	Assert-VeroScriptComponentOutputPath $BackupRoot $RepoRoot
	New-Item -ItemType Directory -Path $BackupRoot | Out-Null
	$OldPackage = Join-Path $BackupRoot $PackageName
	$OldZip = "$OldPackage.zip"
	$InstalledPackage = $false
	$InstalledZip = $false
	try {
		if (Test-Path -LiteralPath $PackageDir) {
			Move-Item -LiteralPath $PackageDir -Destination $OldPackage
		}
		if (Test-Path -LiteralPath $ZipPath) {
			Move-Item -LiteralPath $ZipPath -Destination $OldZip
		}
		Move-Item -LiteralPath $StagedPackage -Destination $PackageDir
		$InstalledPackage = $true
		Move-Item -LiteralPath $StagedZip -Destination $ZipPath
		$InstalledZip = $true
	}
	catch
	{
		# If rollback itself fails, keep the backup in the output for recovery.
		Assert-VeroScriptComponentOutputPath $OutRootPath $RepoRoot
		if ($InstalledPackage) {
			Remove-Item -LiteralPath $PackageDir -Recurse -Force
		}
		if ($InstalledZip) {
			Remove-Item -LiteralPath $ZipPath -Force
		}
		if (Test-Path -LiteralPath $OldPackage) {
			Move-Item -LiteralPath $OldPackage -Destination $PackageDir
		}
		if (Test-Path -LiteralPath $OldZip) {
			Move-Item -LiteralPath $OldZip -Destination $ZipPath
		}
		Assert-VeroScriptComponentOutputPath $BackupRoot $RepoRoot
		Remove-Item -LiteralPath $BackupRoot -Recurse -Force
		throw
	}
	Assert-VeroScriptComponentOutputPath $BackupRoot $RepoRoot
	Remove-Item -LiteralPath $BackupRoot -Recurse -Force
}
finally
{
	$ScratchPrefix = [IO.Path]::GetFullPath((Join-Path $RepoRoot '.tmp')) + [IO.Path]::DirectorySeparatorChar
	if ([IO.Path]::GetFullPath($StageRoot).StartsWith($ScratchPrefix, [StringComparison]::OrdinalIgnoreCase) -and
		(Test-Path -LiteralPath $StageRoot -PathType Container)) {
		Assert-VeroScriptComponentOutputPath $StageRoot $RepoRoot
		Remove-Item -LiteralPath $StageRoot -Recurse -Force
	}
	# Keep the standalone publication directory beside its archive.
}

Write-Host "Docs package directory: $PackageDir" -ForegroundColor Green
Write-Host "Docs package archive:   $ZipPath" -ForegroundColor Green
