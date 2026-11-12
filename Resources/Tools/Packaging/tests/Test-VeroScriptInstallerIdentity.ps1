#Requires -Version 5.1
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

param([Parameter(Mandatory = $true)][string]$RuntimePackageDir)
$ErrorActionPreference = 'Stop'
$WorkspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$RuntimePackageDir = (Resolve-Path -LiteralPath $RuntimePackageDir).Path
$Cli = Join-Path $RuntimePackageDir 'veroscript.exe'
$Identity = (& $Cli --version | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $Identity -notmatch '^VeroScript ([0-9]+\.[0-9]+\.[0-9]+) ') {
	throw "Cannot identify test CLI: $Identity"
}
$ActualVersion = $Matches[1]
if ($Identity -notmatch '\[(win-(?:x64|x86|arm64));') {
	throw "Cannot identify test CLI target: $Identity"
}
$Runtime = $Matches[1]
$DifferentVersion = if ($ActualVersion -eq '999.0.0') {
	'998.0.0'
}
else
{
	'999.0.0'
}
$Fixture = Join-Path $WorkspaceRoot ('.tmp/veroscript-installer-identity-' + [guid]::NewGuid().ToString('N'))
$ToolRoot = Join-Path $Fixture 'Resources/Tools/Packaging'
New-Item -ItemType Directory -Path $ToolRoot -Force | Out-Null
try {
	foreach ($Name in @('package-veroscript-installer.ps1', 'veroscript-output.ps1', 'veroscript-assets.ps1')) {
		Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('../' + $Name)) -Destination (Join-Path $ToolRoot $Name)
	}
	# The entry point now loads the shared product catalog/version configuration and
	# verifies formal Runtime icons before invoking dotnet. Keep these real inputs
	# in the isolated fixture rather than bypassing the production checks.
	Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot '..') -Filter '*.py' -File | ForEach-Object {
		Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $ToolRoot $_.Name)
	}
	$Marker = Join-Path $Fixture 'VeroScript/Public/VeroScriptVersion.h'
	New-Item -ItemType Directory -Path (Split-Path $Marker -Parent) -Force | Out-Null
	Copy-Item -LiteralPath (Join-Path $WorkspaceRoot 'VeroScript/Public/VeroScriptVersion.h') -Destination $Marker
	foreach ($Route in @('Resources/Assets/icons', 'Resources/Assets/file-associations/icons')) {
		$Destination = Join-Path $Fixture $Route
		New-Item -ItemType Directory -Path (Split-Path $Destination -Parent) -Force | Out-Null
		Copy-Item -LiteralPath (Join-Path $WorkspaceRoot $Route) -Destination $Destination -Recurse
	}
	$Configuration = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../ReleaseVersions.json') -Raw | ConvertFrom-Json
	$ConfigurationPath = Join-Path $ToolRoot 'ReleaseVersions.json'
	$InstallerScript = Join-Path $ToolRoot 'package-veroscript-installer.ps1'
	$InstallerProjectRoot = Join-Path $Fixture 'VeroScriptInstaller'
	New-Item -ItemType Directory -Path $InstallerProjectRoot -Force | Out-Null
	Copy-Item -LiteralPath (Join-Path $WorkspaceRoot 'VeroScriptInstaller/global.json') -Destination $InstallerProjectRoot
	# Stop at the build boundary so the positive case never publishes an installer.
	function dotnet {
		if ((Get-Location).Path -ne $InstallerProjectRoot) { throw 'Installer CLI missed its global.json directory' }
		throw 'EXPECTED_BUNDLE_BUILD_BOUNDARY'
	}
	$Configuration.nativeVersion = $DifferentVersion
	$Configuration | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $ConfigurationPath -Encoding UTF8
	$Output = Join-Path $Fixture '.tmp/Runtime-rejected'
	$Arguments = @{ Runtime = $Runtime; Version = $ActualVersion;
		RuntimePackageDir = $RuntimePackageDir; OutRoot = $Output }
	$Rejected = $false
	try {
		& $InstallerScript @Arguments
	}
	catch
	{
		if ($_.Exception.Message -notlike 'Component CLI version mismatch.*') {
			throw
		}
		$Rejected = $true
	}
	if (-not $Rejected -or (Test-Path -LiteralPath $Output)) {
		throw "Runtime accepted a mismatched CLI or created output before rejecting it."
	}
	$Configuration.nativeVersion = $ActualVersion
	$Configuration | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $ConfigurationPath -Encoding UTF8
	$Arguments.Version = $DifferentVersion
	$Arguments.OutRoot = Join-Path $Fixture '.tmp/Runtime-accepted'
	$ReachedBuild = $false
	try {
		& $InstallerScript @Arguments
	}
	catch
	{
		if ($_.Exception.Message -ne 'EXPECTED_BUNDLE_BUILD_BOUNDARY') {
			throw
		}
		$ReachedBuild = $true
	}
	if (-not $ReachedBuild) {
		throw "Runtime rejected the matching CLI."
	}
	Write-Output 'PASSED: Runtime native CLI mismatch rejected before output; independent package versions accepted'
}
finally
{
	$FullFixture = [IO.Path]::GetFullPath($Fixture)
	$ExpectedParent = [IO.Path]::GetFullPath((Join-Path $WorkspaceRoot '.tmp')).TrimEnd('\', '/')
	if (-not ([IO.Path]::GetDirectoryName($FullFixture)).Equals($ExpectedParent, [StringComparison]::OrdinalIgnoreCase) -or
		[IO.Path]::GetFileName($FullFixture) -notmatch '^veroscript-installer-identity-[0-9a-f]{32}$') {
		throw "Unsafe identity fixture cleanup path: $FullFixture"
	}
	$Ancestor = [IO.DirectoryInfo]::new($FullFixture)
	while ($null -ne $Ancestor) {
		if ($Ancestor.Exists -and ($Ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
			throw "Linked identity fixture cleanup path: $($Ancestor.FullName)"
		}
		$Ancestor = $Ancestor.Parent
	}
	if (Test-Path -LiteralPath $FullFixture) {
		Remove-Item -LiteralPath $FullFixture -Recurse -Force
	}
}
