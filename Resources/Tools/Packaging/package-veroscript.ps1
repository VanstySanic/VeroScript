#Requires -Version 5.1
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

<#
.SYNOPSIS
  Package the minimal versioned VeroScript desktop runtime.

.DESCRIPTION
  Produces the CLI, LSP/DAP hosts, standard library and localization resources.
  SDK headers/libraries, Interop, catalogs and examples are excluded.
#>
[CmdletBinding()]
param(
	[string]$ConfigurePreset = "windows-msvc-vs2022",
	[ValidateSet("Debug", "Release", "RelWithDebInfo")]
	[string]$Config = "Release",
	[string]$BuildPreset = "",
	[ValidatePattern('^\d+\.\d+\.\d+$')]
	[string]$Version = '',
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
. (Join-Path $PSScriptRoot 'veroscript-assets.ps1')
. (Join-Path $PSScriptRoot 'veroscript-output.ps1')
$ReleaseConfiguration = Get-VeroScriptReleaseConfiguration $RepoRoot
$ReleaseBusiness = 'Runtime'
if (-not $ReleaseConfiguration.businesses.PSObject.Properties[$ReleaseBusiness]) {
	throw "Unknown release business: $ReleaseBusiness"
}
if (-not $Version) {
	$Version = $ReleaseConfiguration.businesses.$ReleaseBusiness
}

if (-not [string]::IsNullOrWhiteSpace($OutRoot)) { Assert-VeroScriptComponentOutputPath $OutRoot $RepoRoot }
$BuildDir = Join-Path (Join-Path $RepoRoot "build") $ConfigurePreset
$BinDir = Join-Path (Join-Path $BuildDir "bin") $Config
$RequiredStdlibModules = @(
	"algorithm.vs", "attribute.vs", "callable.vs", "concurrent.vs", "core.vs", "delegate.vs", "i18n.vs", "io.vs",
	"math.vs", "memory.vs", "numeric.vs", "path.vs", "random.vs", "string.vs")

function Get-PeArchitectureTag([string]$Path)
{
	$Stream = [IO.File]::OpenRead($Path)
	$Reader = [IO.BinaryReader]::new($Stream)
	try
	{
		if ($Reader.ReadUInt16() -ne 0x5A4D) { throw "Not a PE executable: $Path" }
		$Stream.Position = 0x3C
		$PeOffset = $Reader.ReadUInt32()
		if ($PeOffset -gt ($Stream.Length - 6)) { throw "Invalid PE header: $Path" }
		$Stream.Position = $PeOffset
		if ($Reader.ReadUInt32() -ne 0x00004550) { throw "Invalid PE signature: $Path" }
		switch ($Reader.ReadUInt16())
		{
			0x014C { "x86" }
			0x8664 { "x64" }
			0xAA64 { "arm64" }
			default { throw "Unsupported PE architecture: $Path" }
		}
	}
	finally
	{
		$Reader.Dispose()
		$Stream.Dispose()
	}
}

if ($BuildPreset)
{
	& cmake --build --preset $BuildPreset --target VeroScriptCli VeroScriptDap VeroScriptLsp
	if ($LASTEXITCODE -ne 0) { throw "VeroScript runtime build failed ($LASTEXITCODE)." }
	if (Select-String -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt') -Pattern '^VEROSCRIPT_BUILD_APPLICATION:BOOL=ON$' -Quiet) {
		& cmake --build --preset $BuildPreset --target VeroScriptRuntime VeroScriptLauncher
		if ($LASTEXITCODE -ne 0) { throw "Application Runtime build failed ($LASTEXITCODE)." }
	}
}

$Tools = @("veroscript.exe", "veroscript-lsp.exe", "veroscript-dap.exe")
foreach ($Tool in $Tools)
{
	if (-not (Test-Path -LiteralPath (Join-Path $BinDir $Tool) -PathType Leaf))
	{
		throw "Required runtime tool not found: $(Join-Path $BinDir $Tool)"
	}
}
$Arch = Get-PeArchitectureTag (Join-Path $BinDir "veroscript.exe")
if (Invoke-VeroScriptDefaultBusinessPackage $PSCommandPath $PSBoundParameters $RepoRoot $ReleaseBusiness $Version "win-$Arch") {
	return
}
if ([string]::IsNullOrWhiteSpace($OutRoot)) {
	$OutRoot = "$ModuleRelative\Release\Runtime\win-$Arch\$Version"
}
$OutRootPath = if ([IO.Path]::IsPathRooted($OutRoot)) {
	[IO.Path]::GetFullPath($OutRoot)
}
else
{
	[IO.Path]::GetFullPath((Join-Path $RepoRoot $OutRoot))
}
$ToolVersion = (& (Join-Path $BinDir "veroscript.exe") --version 2>&1 | Out-String).Trim()
Assert-VeroScriptComponentOutputPath $OutRootPath $RepoRoot
if ($LASTEXITCODE -ne 0 -or -not $ToolVersion.StartsWith("VeroScript $($ReleaseConfiguration.nativeVersion) ", [StringComparison]::Ordinal))
{
	throw "Runtime version mismatch. Expected VeroScript $Version, got: $ToolVersion"
}
$JitIdentity = Get-VeroScriptJitIdentity $ToolVersion
$PackageName = "VeroScriptRuntime-$Version-win-$Arch"
$PackageDir = Join-Path $OutRootPath $PackageName
$ZipPath = Join-Path $OutRootPath "$PackageName.zip"
$Stage = Join-Path $OutRootPath ".$PackageName.stage-$([guid]::NewGuid().ToString('N'))"

try
{
	New-Item -ItemType Directory -Path $Stage -Force | Out-Null
	Copy-Item -LiteralPath ($Tools | ForEach-Object { Join-Path $BinDir $_ }) -Destination $Stage
	Copy-VeroScriptWindowsRuntime $BuildDir $Stage $Config
	Copy-Item -LiteralPath (Join-Path $RepoRoot "$ModuleRelative\VeroScript\Resources\Scripts\stdlib") `
		-Destination (Join-Path $Stage "stdlib") -Recurse
	Copy-VeroScriptLocalization $RepoRoot (Join-Path $Stage "localization") runtime
	Copy-VeroScriptRuntimeIcons $RepoRoot $Stage "win-$Arch"
	ConvertTo-VeroScriptPortableAssetTree $Stage
	Copy-Item -LiteralPath (Join-Path $RepoRoot "LICENSE") -Destination $Stage
	$AsmJitLicenseRoot = Join-Path $Stage "licenses\asmjit"
	if ($JitIdentity.backend -eq 'asmjit')
	{
		New-Item -ItemType Directory -Path $AsmJitLicenseRoot -Force | Out-Null
		Copy-Item -LiteralPath (Join-Path $RepoRoot "$ModuleRelative\Modules\Execution\VeroScriptAsmJit\ThirdParty\asmjit\VERSION.txt") -Destination $AsmJitLicenseRoot
		Copy-Item -LiteralPath (Join-Path $RepoRoot "$ModuleRelative\Modules\Execution\VeroScriptAsmJit\ThirdParty\asmjit\LICENSE.md") -Destination $AsmJitLicenseRoot
	}

	Get-ChildItem -LiteralPath $Stage -Directory -Recurse -Force -ErrorAction SilentlyContinue |
		Where-Object { $_.Name -in @(".veroscript", ".verocache") } |
		Sort-Object FullName -Descending |
		Remove-Item -Recurse -Force

	. (Join-Path $PSScriptRoot 'veroscript-packages.ps1')
	. (Join-Path $PSScriptRoot 'veroscript-vm.ps1')
	Copy-VeroScriptVmRuntime $BuildDir $Stage $Config
	Copy-VeroScriptPackageClient $ModuleRoot (Join-Path $Stage 'veroscript.exe') $Stage 'runtime'
	Copy-VeroScriptApplicationRuntime $ModuleRoot $BinDir $Stage

	$Manifest = [ordered]@{
		schemaVersion = 1
		product = "VeroScript Runtime"
		version = $Version
		platform = "windows"
		architecture = $Arch
		includesExamples = $false
		jit = $JitIdentity
	}
	$Manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Stage "runtime-manifest.json") -Encoding UTF8

	foreach ($Required in @("veroscript.exe", "veroscript-lsp.exe", "veroscript-dap.exe",
			"localization\en-US") + ($RequiredStdlibModules | ForEach-Object { "stdlib\$_" }))
	{
		if (-not (Test-Path -LiteralPath (Join-Path $Stage $Required)))
		{
			throw "Runtime staging package is incomplete: $Required"
		}
	}
	Remove-VeroScriptAuxiliaryContent $Stage
	if (Test-Path -LiteralPath $PackageDir) { Remove-Item -LiteralPath $PackageDir -Recurse -Force }
	Move-Item -LiteralPath $Stage -Destination $PackageDir
	if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
	Compress-Archive -Path $PackageDir -DestinationPath $ZipPath -CompressionLevel Optimal
}
finally
{
	if (Test-Path -LiteralPath $Stage) { Remove-Item -LiteralPath $Stage -Recurse -Force -ErrorAction SilentlyContinue }
}

Write-Host "Runtime directory: $PackageDir" -ForegroundColor Green
Write-Host "Runtime archive:   $ZipPath" -ForegroundColor Green
