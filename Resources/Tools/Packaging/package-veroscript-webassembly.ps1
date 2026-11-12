#Requires -Version 5.1
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

<#
.SYNOPSIS
  Build and package the UI-free VeroScript WebAssembly SDK.

.DESCRIPTION
  Produces a versioned WebAssembly SDK directory and ZIP containing the Emscripten
  module, Wasm binary, and TypeScript bindings. The stdlib and localization
  resources are embedded in the Wasm module by the WebAssembly build.
  Interop libraries and desktop executables are intentionally excluded.
#>
[CmdletBinding()]
param(
	[ValidateSet("Debug", "Release")]
	[string]$Config = "Release",
	[ValidatePattern('^\d+\.\d+\.\d+$')]
	[string]$Version = '',
	[string]$BuildDirectory = "build/webassembly",
	# An explicit override stages a component; the default uses the business publisher.
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
$ReleaseBusiness = 'WebAssembly'
if (-not $ReleaseConfiguration.businesses.PSObject.Properties[$ReleaseBusiness]) {
	throw "Unknown release business: $ReleaseBusiness"
}
if (-not $Version) {
	$Version = $ReleaseConfiguration.businesses.$ReleaseBusiness
}
if (Invoke-VeroScriptDefaultBusinessPackage $PSCommandPath $PSBoundParameters $RepoRoot $ReleaseBusiness $Version 'win-x64') {
	return
}

if (-not [string]::IsNullOrWhiteSpace($OutRoot)) { Assert-VeroScriptComponentOutputPath $OutRoot $RepoRoot }
if ([string]::IsNullOrWhiteSpace($OutRoot)) {
	$OutRoot = "$ModuleRelative\Release\$($ReleaseConfiguration.businessPaths.WebAssembly)\win-x64\$Version"
}
$BuildScript = Join-Path $PSScriptRoot "build-veroscript-webassembly.ps1"
$VerifyScript = Join-Path $PSScriptRoot "verify-veroscript-webassembly.ps1"
$WebAssemblyRoot = Join-Path $RepoRoot "$ModuleRelative\Extensions\Platform\WebAssembly\VeroScriptWebAssembly"
$BuildRoot = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
	[IO.Path]::GetFullPath($BuildDirectory)
}
else
{
	[IO.Path]::GetFullPath((Join-Path $RepoRoot $BuildDirectory))
}
$BinRoot = Join-Path $BuildRoot "bin"
$OutRootPath = if ([IO.Path]::IsPathRooted($OutRoot)) {
	[IO.Path]::GetFullPath($OutRoot)
}
else
{
	[IO.Path]::GetFullPath((Join-Path $RepoRoot $OutRoot))
}
$RepoPrefix = $RepoRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
Assert-VeroScriptComponentOutputPath $OutRootPath $RepoRoot
if ($OutRootPath -eq $RepoRoot -or
	-not $OutRootPath.StartsWith($RepoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
	throw 'The WebAssembly SDK output must be a dedicated directory below the repository root.'
}
$PackageName = "VeroScriptWebAssemblySDK-$Version-wasm32"
$PackageDir = Join-Path $OutRootPath $PackageName
$ZipPath = Join-Path $OutRootPath "$PackageName.zip"
$StagingRoot = Join-Path $OutRootPath ("_vs_webassembly_package_stage-" + [guid]::NewGuid().ToString("N"))
$StagingPackage = Join-Path $StagingRoot $PackageName
$StagingZip = Join-Path $StagingRoot "$PackageName.zip"

function Assert-WebAssemblyPackageArchive(
	[string]$DirectoryPath,
	[string]$ArchivePath,
	[string[]]$ExpectedFiles
) {
	Add-Type -AssemblyName System.IO.Compression.FileSystem
	$ExpectedPaths = @($ExpectedFiles | Sort-Object)
	$Archive = [System.IO.Compression.ZipFile]::OpenRead($ArchivePath)
	try {
		$Prefix = (Split-Path -Leaf $DirectoryPath) + '/'
		$FileEntries = @($Archive.Entries | Where-Object { $_.Name })
		$ArchivePaths = @($FileEntries | ForEach-Object {
			$NormalizedPath = $_.FullName.Replace('\', '/')
			if (-not $NormalizedPath.StartsWith($Prefix, [StringComparison]::Ordinal)) {
				throw "Unexpected WebAssembly SDK archive path: $($_.FullName)"
			}
			$NormalizedPath.Substring($Prefix.Length)
		} | Sort-Object)
		$PathDifferences = @(Compare-Object `
			-ReferenceObject $ExpectedPaths `
			-DifferenceObject $ArchivePaths)
		if ($ArchivePaths.Count -ne $ExpectedPaths.Count -or $PathDifferences.Count -ne 0) {
			throw 'WebAssembly SDK archive file set differs from the staged package.'
		}

		foreach ($Entry in $FileEntries) {
			$RelativePath = $Entry.FullName.Replace('\', '/').Substring($Prefix.Length)
			$HashAlgorithm = [System.Security.Cryptography.SHA256]::Create()
			$Stream = $null
			try {
				$Stream = $Entry.Open()
				$ArchiveHash = [BitConverter]::ToString(
					$HashAlgorithm.ComputeHash($Stream)).Replace('-', '')
			}
			finally
			{
				if ($null -ne $Stream) {
					$Stream.Dispose()
				}
				$HashAlgorithm.Dispose()
			}
			$FileHash = (Get-FileHash `
				-LiteralPath (Join-Path $DirectoryPath $RelativePath) `
				-Algorithm SHA256).Hash
			if ($ArchiveHash -ne $FileHash) {
				throw "WebAssembly SDK archive content differs from the staged file: $RelativePath"
			}
		}
		Write-Host "Verified WebAssembly SDK archive: $($ArchivePaths.Count) files" -ForegroundColor Green
	}
	finally
	{
		$Archive.Dispose()
	}
}

foreach ($ScriptPath in @($BuildScript, $VerifyScript)) {
	if (-not (Test-Path -LiteralPath $ScriptPath -PathType Leaf)) {
		throw "Required Web script not found: $ScriptPath"
	}
}

Write-Host "Building VeroScript WebAssembly SDK ($Config)..." -ForegroundColor Cyan
& $BuildScript -Configuration $Config -BuildDirectory $BuildRoot
if ($LASTEXITCODE -ne 0) {
	throw "WebAssembly build failed with exit code $LASTEXITCODE."
}

$RequiredFiles = @(
	"veroscript-engine.mjs",
	"veroscript-engine.wasm",
	"veroscript-engine.d.mts",
	"veroscript-engine-fast.mjs",
	"veroscript-engine-fast.wasm",
	"veroscript-engine-fast.d.mts",
	"veroscript-webassembly.ts",
	"veroscript-webassembly.d.ts"
)
$ExpectedPackageFiles = @($RequiredFiles + @("LICENSE"))
foreach ($FileName in $RequiredFiles) {
	$Path = Join-Path $BinRoot $FileName
	if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or (Get-Item -LiteralPath $Path).Length -eq 0) {
		throw "Required WebAssembly SDK output is missing or empty: $Path"
	}
}

try {
	New-Item -ItemType Directory -Path $StagingPackage -Force | Out-Null

	foreach ($FileName in $RequiredFiles) {
		Copy-Item -LiteralPath (Join-Path $BinRoot $FileName) -Destination $StagingPackage
	}
	Copy-Item -LiteralPath (Join-Path $RepoRoot "LICENSE") -Destination $StagingPackage

	& $VerifyScript -ArtifactDirectory $StagingPackage -Configuration $Config
	if ($LASTEXITCODE -ne 0) {
		throw "Staged WebAssembly SDK verification failed with exit code $LASTEXITCODE."
	}

	Get-ChildItem -LiteralPath $StagingPackage -Directory -Recurse -Force -ErrorAction SilentlyContinue |
		Where-Object { $_.Name -eq ".verocache" } |
		Sort-Object { $_.FullName.Length } -Descending |
		Remove-Item -Recurse -Force -ErrorAction SilentlyContinue

	$ActualPackageFiles = @(Get-ChildItem -LiteralPath $StagingPackage -File -Recurse -Force |
		ForEach-Object {
			$_.FullName.Substring($StagingPackage.Length + 1).Replace('\', '/')
		} | Sort-Object)
	$ExpectedPackagePaths = @($ExpectedPackageFiles | Sort-Object)
	$PackageDifferences = @(Compare-Object `
		-ReferenceObject $ExpectedPackagePaths `
		-DifferenceObject $ActualPackageFiles)
	if ($ActualPackageFiles.Count -ne $ExpectedPackagePaths.Count -or
		$PackageDifferences.Count -ne 0) {
		throw 'Staged WebAssembly SDK contains missing or unexpected files.'
	}

	New-VeroScriptPortableArchive $StagingPackage $StagingZip $RepoRoot
	Assert-WebAssemblyPackageArchive `
		-DirectoryPath $StagingPackage `
		-ArchivePath $StagingZip `
		-ExpectedFiles $ExpectedPackageFiles

	if (Test-Path -LiteralPath $PackageDir) {
		Remove-Item -LiteralPath $PackageDir -Recurse -Force
	}
	Move-Item -LiteralPath $StagingPackage -Destination $PackageDir

	if (Test-Path -LiteralPath $ZipPath) {
		Remove-Item -LiteralPath $ZipPath -Force
	}
	Move-Item -LiteralPath $StagingZip -Destination $ZipPath
}
finally
{
	if (Test-Path -LiteralPath $StagingRoot -PathType Container) {
		Remove-Item -LiteralPath $StagingRoot -Recurse -Force
	}
}

Write-Host "WebAssembly SDK directory: $PackageDir" -ForegroundColor Green
Write-Host "WebAssembly SDK archive:   $ZipPath" -ForegroundColor Green
