#Requires -Version 5.1
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

<# Package matching PDB files already produced by the selected build configuration. #>
[CmdletBinding()]
param(
	[string]$ConfigurePreset = "windows-msvc-vs2022",
	[ValidateSet("Debug", "Release", "RelWithDebInfo")]
	[string]$Config = "Release",
	[ValidatePattern('^\d+\.\d+\.\d+$')]
	[string]$Version = '',
	[ValidateSet("x64", "x86", "arm64")]
	[string]$Architecture = "x64",
	[string]$OutRoot = ''
)
$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$ModuleRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$ModuleRelative = if ($ModuleRoot -eq $RepoRoot) { '.' }
else
{
	$ModuleRoot.Substring($RepoRoot.TrimEnd('\', '/').Length + 1)
}
. (Join-Path $PSScriptRoot 'veroscript-output.ps1')
$ReleaseConfiguration = Get-VeroScriptReleaseConfiguration $RepoRoot
if (-not $Version) {
	$Version = $ReleaseConfiguration.businesses.CoreSDK
}
if (Invoke-VeroScriptDefaultBusinessPackage $PSCommandPath $PSBoundParameters $RepoRoot 'CoreSDK' $Version "win-$Architecture") {
	return
}
if (-not [string]::IsNullOrWhiteSpace($OutRoot)) { Assert-VeroScriptComponentOutputPath $OutRoot $RepoRoot }
$BuildRoot = Join-Path (Join-Path $RepoRoot "build") $ConfigurePreset
if ([string]::IsNullOrWhiteSpace($OutRoot)) {
	$OutRoot = "$ModuleRelative\Release\CoreSDK\win-$Architecture\$Version"
}
$OutRootPath = if ([IO.Path]::IsPathRooted($OutRoot)) { [IO.Path]::GetFullPath($OutRoot) }
else
{
	[IO.Path]::GetFullPath((Join-Path $RepoRoot $OutRoot))
}
$PackageName = "VeroScriptSymbols-$Version-win-$Architecture"
Assert-VeroScriptComponentOutputPath $OutRootPath $RepoRoot
$PackageDir = Join-Path $OutRootPath $PackageName
$ZipPath = Join-Path $OutRootPath "$PackageName.zip"
$Stage = Join-Path $OutRootPath ".$PackageName.stage-$([guid]::NewGuid().ToString('N'))"
$SearchRoots = @(
	(Join-Path (Join-Path $BuildRoot "bin") $Config),
	(Join-Path (Join-Path $BuildRoot "lib") $Config)
)
$AllowedSymbols = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
@(
	"veroscript", "veroscript-lsp", "veroscript-dap",
	"VeroScript", "VeroScriptTooling"
) | ForEach-Object { [void]$AllowedSymbols.Add($_) }
$Symbols = @($SearchRoots | Where-Object { Test-Path -LiteralPath $_ } | ForEach-Object {
	Get-ChildItem -LiteralPath $_ -Filter *.pdb -File | Where-Object {
		$AllowedSymbols.Contains($_.BaseName)
	}
} | Sort-Object FullName -Unique)
if ($Symbols.Count -eq 0)
{
	throw "No matching $Config PDB files were found. Build the same configuration with debug information before packaging symbols."
}
try
{
	New-Item -ItemType Directory -Path (Join-Path $Stage "bin") -Force | Out-Null
	New-Item -ItemType Directory -Path (Join-Path $Stage "lib") -Force | Out-Null
	$BinSymbolRoot = [IO.Path]::GetFullPath($SearchRoots[0])
	foreach ($Symbol in $Symbols)
	{
		$DestinationLeaf = if ([IO.Path]::GetFullPath($Symbol.DirectoryName) -eq $BinSymbolRoot) { "bin" }
		else
		{
			"lib"
		}
		Copy-Item -LiteralPath $Symbol.FullName -Destination (Join-Path $Stage $DestinationLeaf)
	}
	Remove-VeroScriptAuxiliaryContent $Stage
	Copy-Item -LiteralPath (Join-Path $RepoRoot 'LICENSE') -Destination (Join-Path $Stage 'LICENSE') -Force
	if (Test-Path -LiteralPath $PackageDir) { Remove-Item -LiteralPath $PackageDir -Recurse -Force }
	Move-Item -LiteralPath $Stage -Destination $PackageDir
	if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
	Compress-Archive -Path $PackageDir -DestinationPath $ZipPath -CompressionLevel Optimal
}
finally
{
	if (Test-Path -LiteralPath $Stage) { Remove-Item -LiteralPath $Stage -Recurse -Force -ErrorAction SilentlyContinue }
}
Write-Host "Symbols directory: $PackageDir" -ForegroundColor Green
Write-Host "Symbols archive:   $ZipPath" -ForegroundColor Green
