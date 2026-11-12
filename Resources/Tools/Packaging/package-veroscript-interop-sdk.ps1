#Requires -Version 5.1
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

<# Package the stable C ABI Interop SDK. #>
[CmdletBinding()]
param(
	[string]$ConfigurePreset = "windows-msvc-vs2022",
	[ValidateSet("Debug", "Release", "RelWithDebInfo")]
	[string]$Config = "Release",
	[string]$BuildPreset = "",
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
$ReleaseBusiness = 'Interop'
if (-not $ReleaseConfiguration.businesses.PSObject.Properties[$ReleaseBusiness]) {
	throw "Unknown release business: $ReleaseBusiness"
}
if (-not $Version) {
	$Version = $ReleaseConfiguration.businesses.$ReleaseBusiness
}
if (Invoke-VeroScriptDefaultBusinessPackage $PSCommandPath $PSBoundParameters $RepoRoot $ReleaseBusiness $Version "win-$Architecture") {
	return
}

. (Join-Path $PSScriptRoot 'veroscript-assets.ps1')
if (-not [string]::IsNullOrWhiteSpace($OutRoot)) { Assert-VeroScriptComponentOutputPath $OutRoot $RepoRoot }
$BuildRoot = Join-Path (Join-Path $RepoRoot "build") $ConfigurePreset
$BinRoot = Join-Path (Join-Path $BuildRoot "bin") $Config
$LibRoot = Join-Path (Join-Path $BuildRoot "lib") $Config
$CMakeCachePath = Join-Path $BuildRoot "CMakeCache.txt"
if ([string]::IsNullOrWhiteSpace($OutRoot)) {
	$OutRoot = "$ModuleRelative\Release\Interop\win-$Architecture\$Version"
}
$OutRootPath = if ([IO.Path]::IsPathRooted($OutRoot)) { [IO.Path]::GetFullPath($OutRoot) }
else
{
	[IO.Path]::GetFullPath((Join-Path $RepoRoot $OutRoot))
}
$PackageName = "VeroScriptInteropSDK-$Version-win-$Architecture"
Assert-VeroScriptComponentOutputPath $OutRootPath $RepoRoot
$PackageDir = Join-Path $OutRootPath $PackageName
$ZipPath = Join-Path $OutRootPath "$PackageName.zip"
$WorkId = [guid]::NewGuid().ToString('N')
# CMake's generated MSVC TryCompile paths are substantially deeper than the
# package itself. Use compact staging names and a short repository-local build
# root so a supported custom OutRoot cannot trip FileTracker's MAX_PATH limit.
$Stage = Join-Path $OutRootPath ".vsi-$WorkId"
$ConsumerBuild = Join-Path (Join-Path $RepoRoot ".tmp") "vsi-consumer-$WorkId"
Assert-VeroScriptComponentOutputPath $ConsumerBuild $RepoRoot
$CmakeArchitecture = switch ($Architecture.ToLowerInvariant())
{
	"x64" { "x64" }
	"x86" { "Win32" }
	"arm64" { "ARM64" }
	default { throw "Unsupported Windows Interop SDK architecture '$Architecture'." }
}
$PackagePointerSize = if ($Architecture -eq "x86") { 4 }
else
{
	8
}
$VersionWriter = Join-Path $PSScriptRoot "../CMake/write-cmake-package-version.cmake"
if (-not (Test-Path -LiteralPath $VersionWriter -PathType Leaf))
{
	throw "CMake package-version writer not found: $VersionWriter"
}

function Get-RequiredCMakeBool([string]$Name)
{
	if (-not (Test-Path -LiteralPath $CMakeCachePath -PathType Leaf))
	{
		throw "Interop SDK CMake cache not found: $CMakeCachePath"
	}
	$Line = Get-Content -LiteralPath $CMakeCachePath |
		Where-Object { $_ -match "^$([regex]::Escape($Name)):BOOL=" } |
		Select-Object -First 1
	if (-not $Line) { throw "Interop SDK CMake cache is missing $Name" }
	$Value = ($Line -replace '^[^=]*=', '').Trim()
	if ($Value -match '^(1|ON|TRUE|YES|Y)$') { return 1 }
	if ($Value -match '^(0|OFF|FALSE|NO|N)$') { return 0 }
	throw "Interop SDK CMake cache has invalid $Name value '$Value'"
}

if ($BuildPreset)
{
	& cmake --build --preset $BuildPreset --target VeroScriptInterop
	if ($LASTEXITCODE -ne 0) { throw "Interop SDK build failed ($LASTEXITCODE)." }
}
$Dll = Join-Path $BinRoot "VeroScriptInterop.dll"
$Lib = Join-Path $LibRoot "VeroScriptInterop.lib"
$Header = Join-Path $RepoRoot "$ModuleRelative\VeroScriptInterop\Public\VeroScriptInterop.h"
foreach ($Path in @($Dll, $Lib, $Header)) { if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Interop SDK artifact missing: $Path" } }
$JitTestHooks = Get-RequiredCMakeBool "VEROSCRIPT_ENABLE_JIT_TEST_HOOKS"
if ($JitTestHooks -ne 0)
{
	throw "The production Interop SDK requires VEROSCRIPT_ENABLE_JIT_TEST_HOOKS=OFF"
}
try
{
	New-Item -ItemType Directory -Path (Split-Path $ConsumerBuild -Parent) -Force | Out-Null
	foreach ($Directory in @("bin", "lib", "include\VeroScript", "lib\cmake\VeroScriptInterop")) { New-Item -ItemType Directory -Path (Join-Path $Stage $Directory) -Force | Out-Null }
	Copy-Item -LiteralPath $Dll -Destination (Join-Path $Stage "bin")
	Copy-VeroScriptWindowsRuntime $BuildRoot (Join-Path $Stage 'bin') $Config
	Copy-VeroScriptLocalization $RepoRoot (Join-Path $Stage "bin/localization") interop
	Copy-Item -LiteralPath $Lib -Destination (Join-Path $Stage "lib")
	Get-ChildItem -LiteralPath (Split-Path $Header -Parent) -Filter '*.h' -File |
		Copy-Item -Destination (Join-Path $Stage "include\VeroScript")
	Copy-Item -LiteralPath (Join-Path $RepoRoot "LICENSE") -Destination $Stage
	if ((Get-RequiredCMakeBool "VEROSCRIPT_BUILD_APPLICATION") -ne 0)
	{
		# Application support is linked into Interop; carry its embedded vendors'
		# notices just as the VeroScriptInterop install component does.
		$ApplicationVendor = Join-Path $ModuleRoot 'VeroScriptApplication/ThirdParty'
		foreach ($Notice in @('miniz/LICENSE', 'json/LICENSE.MIT', 'sha256/LICENSE', 'sources.json'))
		{
			$NoticeDestination = Join-Path $Stage "licenses/application/$Notice"
			New-Item -ItemType Directory -Path (Split-Path $NoticeDestination -Parent) -Force | Out-Null
			Copy-Item -LiteralPath (Join-Path $ApplicationVendor $Notice) -Destination $NoticeDestination
		}
	}
	@"
get_filename_component(_VEROSCRIPT_INTEROP_PREFIX "`${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT TARGET VeroScript::VeroScriptInterop)
  add_library(VeroScript::VeroScriptInterop SHARED IMPORTED)
  set_target_properties(VeroScript::VeroScriptInterop PROPERTIES
    IMPORTED_LOCATION "`${_VEROSCRIPT_INTEROP_PREFIX}/bin/VeroScriptInterop.dll"
    IMPORTED_IMPLIB "`${_VEROSCRIPT_INTEROP_PREFIX}/lib/VeroScriptInterop.lib"
    INTERFACE_INCLUDE_DIRECTORIES "`${_VEROSCRIPT_INTEROP_PREFIX}/include/VeroScript")
endif()
set(VeroScriptInterop_VERSION "$Version")
"@ | Set-Content -LiteralPath (Join-Path $Stage "lib\cmake\VeroScriptInterop\VeroScriptInteropConfig.cmake") -Encoding UTF8
	$VersionFile = Join-Path $Stage "lib\cmake\VeroScriptInterop\VeroScriptInteropConfigVersion.cmake"
	& cmake "-DOUTPUT_FILE:FILEPATH=$($VersionFile.Replace('\', '/'))" `
		"-DPACKAGE_SEMANTIC_VERSION:STRING=$Version" `
		"-DPACKAGE_POINTER_SIZE:STRING=$PackagePointerSize" -P $VersionWriter
	if ($LASTEXITCODE -ne 0) { throw "Interop SDK CMake version generation failed ($LASTEXITCODE)." }

	& cmake -S (Join-Path $RepoRoot "$ModuleRelative/VeroScriptInterop/Resources/Examples/InteropMinimal") -B $ConsumerBuild `
		"-DCMAKE_PREFIX_PATH=$Stage" "-DVEROSCRIPT_SDK_PACKAGE_VERSION:STRING=$Version" `
		"-DVEROSCRIPT_NATIVE_VERSION:STRING=$($ReleaseConfiguration.nativeVersion)" `
		"-DVEROSCRIPT_SDK_ABI_TEST_SOURCE:FILEPATH=$(Join-Path $RepoRoot 'VeroScriptInterop/Private/Tests/VeroScriptInteropCHeaderTest.c')" `
		-A $CmakeArchitecture
	if ($LASTEXITCODE -ne 0) { throw "Interop SDK external-consumer configure failed ($LASTEXITCODE)." }
	& cmake --build $ConsumerBuild --config $Config -- /m:2
	if ($LASTEXITCODE -ne 0) { throw "Interop SDK external-consumer build failed ($LASTEXITCODE)." }
	& ctest --test-dir $ConsumerBuild -C $Config --output-on-failure --no-tests=error
	if ($LASTEXITCODE -ne 0) { throw "Interop SDK external-consumer run failed ($LASTEXITCODE)." }

	Get-ChildItem -LiteralPath $Stage -Directory -Recurse -Force |
		Where-Object { $_.Name -eq '.veroscript' } |
		Sort-Object { $_.FullName.Length } -Descending |
		Remove-Item -Recurse -Force
	Remove-VeroScriptAuxiliaryContent $Stage
	if (Test-Path -LiteralPath $PackageDir) { Remove-Item -LiteralPath $PackageDir -Recurse -Force }
	Move-Item -LiteralPath $Stage -Destination $PackageDir
	if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
	Compress-Archive -Path $PackageDir -DestinationPath $ZipPath -CompressionLevel Optimal
}
finally
{
	if (Test-Path -LiteralPath $Stage) { Remove-Item -LiteralPath $Stage -Recurse -Force -ErrorAction SilentlyContinue }
	if (Test-Path -LiteralPath $ConsumerBuild) { Remove-Item -LiteralPath $ConsumerBuild -Recurse -Force -ErrorAction SilentlyContinue }
}
Write-Host "Interop SDK directory: $PackageDir" -ForegroundColor Green
Write-Host "Interop SDK archive:   $ZipPath" -ForegroundColor Green
