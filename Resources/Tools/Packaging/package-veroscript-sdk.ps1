#Requires -Version 5.1
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

<# Package the language Core SDK; language adapters/AOT are independent products. #>
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
. (Join-Path $PSScriptRoot 'veroscript-assets.ps1')
. (Join-Path $PSScriptRoot 'veroscript-output.ps1')
$ReleaseConfiguration = Get-VeroScriptReleaseConfiguration $RepoRoot
$ReleaseBusiness = 'CoreSDK'
if (-not $ReleaseConfiguration.businesses.PSObject.Properties[$ReleaseBusiness]) {
	throw "Unknown release business: $ReleaseBusiness"
}
if (-not $Version) {
	$Version = $ReleaseConfiguration.businesses.$ReleaseBusiness
}
if (Invoke-VeroScriptDefaultBusinessPackage $PSCommandPath $PSBoundParameters $RepoRoot $ReleaseBusiness $Version "win-$Architecture") {
	return
}

if (-not [string]::IsNullOrWhiteSpace($OutRoot)) { Assert-VeroScriptComponentOutputPath $OutRoot $RepoRoot }
$BuildRoot = Join-Path (Join-Path $RepoRoot "build") $ConfigurePreset
$BinRoot = Join-Path (Join-Path $BuildRoot "bin") $Config
$LibRoot = Join-Path (Join-Path $BuildRoot "lib") $Config
$CMakeCachePath = Join-Path $BuildRoot "CMakeCache.txt"
if ([string]::IsNullOrWhiteSpace($OutRoot)) {
	$OutRoot = "$ModuleRelative\Release\CoreSDK\win-$Architecture\$Version"
}
$OutRootPath = if ([IO.Path]::IsPathRooted($OutRoot)) { [IO.Path]::GetFullPath($OutRoot) }
else
{
	[IO.Path]::GetFullPath((Join-Path $RepoRoot $OutRoot))
}
$PackageName = "VeroScriptSDK-$Version-win-$Architecture"
Assert-VeroScriptComponentOutputPath $OutRootPath $RepoRoot
$PackageDir = Join-Path $OutRootPath $PackageName
$ZipPath = Join-Path $OutRootPath "$PackageName.zip"
$WorkId = [guid]::NewGuid().ToString('N')
# Keep transient names short. MSVC FileTracker still encounters MAX_PATH in
# generated TryCompile trees even when the surrounding PowerShell process is
# long-path aware; nesting the consumer below a descriptive OutRoot can make an
# otherwise valid package request fail before the SDK is tested.
$Stage = Join-Path $OutRootPath ".vss-$WorkId"
$ConsumerBuild = Join-Path (Join-Path $RepoRoot ".tmp") "vss-consumer-$WorkId"
Assert-VeroScriptComponentOutputPath $ConsumerBuild $RepoRoot
$RequiredStdlibModules = @(
	"algorithm.vs", "attribute.vs", "callable.vs", "compiler.vs", "concurrent.vs", "core.vs", "delegate.vs", "i18n.vs", "io.vs",
	"math.vs", "memory.vs", "numeric.vs", "path.vs", "random.vs", "string.vs")

function Get-RequiredCMakeBool([string]$Name)
{
	if (-not (Test-Path -LiteralPath $CMakeCachePath -PathType Leaf))
	{
		throw "Core SDK CMake cache not found: $CMakeCachePath"
	}
	$Line = Get-Content -LiteralPath $CMakeCachePath |
		Where-Object { $_ -match "^$([regex]::Escape($Name)):(BOOL|INTERNAL)=" } |
		Select-Object -First 1
	if (-not $Line) { throw "Core SDK CMake cache is missing $Name" }
	$Value = ($Line -replace '^[^=]*=', '').Trim()
	if ($Value -match '^(1|ON|TRUE|YES|Y)$') { return 1 }
	if ($Value -match '^(0|OFF|FALSE|NO|N)$') { return 0 }
	throw "Core SDK CMake cache has invalid $Name value '$Value'"
}

if ($BuildPreset)
{
	& cmake --build --preset $BuildPreset --target VeroScript VeroScriptTooling VeroScriptCli VeroScriptLsp VeroScriptDap
	if ($LASTEXITCODE -ne 0) { throw "Core SDK build failed ($LASTEXITCODE)." }
	if ((Get-RequiredCMakeBool 'VEROSCRIPT_BUILD_APPLICATION') -eq 1) {
		& cmake --build --preset $BuildPreset --target VeroScriptRuntime VeroScriptLauncher
		if ($LASTEXITCODE -ne 0) { throw "Application Runtime build failed ($LASTEXITCODE)." }
	}
}

$Libraries = @("VeroScript.lib", "VeroScriptTooling.lib")
$HasBackendModule = Get-RequiredCMakeBool "VEROSCRIPT_BUILD_ASMJIT_BACKEND"
$HasAsmJit = if ($HasBackendModule -eq 1) { Get-RequiredCMakeBool "VEROSCRIPT_ASMJIT_NATIVE_ENABLED" }
else
{
	0
}
$Tools = @("veroscript.exe", "veroscript-lsp.exe", "veroscript-dap.exe")
foreach ($Path in @($Libraries | ForEach-Object { Join-Path $LibRoot $_ }) + @($Tools | ForEach-Object { Join-Path $BinRoot $_ }))
{
	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Core SDK build artifact not found: $Path" }
}
$CoreBuildId = (Get-FileHash -LiteralPath (Join-Path $LibRoot "VeroScript.lib") -Algorithm SHA256).Hash.ToLowerInvariant()
$ToolVersion = (& (Join-Path $BinRoot "veroscript.exe") --version 2>&1 | Out-String).Trim()
if (($LASTEXITCODE -ne 0) -or (-not $ToolVersion.StartsWith("VeroScript $($ReleaseConfiguration.nativeVersion) ", [StringComparison]::Ordinal)) -or (-not $ToolVersion.Contains("[win-$Architecture;")))
{
	throw "Core SDK CLI version/architecture mismatch: $ToolVersion"
}
$JitIdentity = Get-VeroScriptJitIdentity $ToolVersion
Assert-VeroScriptJitBuildIdentity $JitIdentity $HasBackendModule $HasAsmJit
$JitTestHooks = if ($HasBackendModule -eq 1) { Get-RequiredCMakeBool "VEROSCRIPT_ENABLE_JIT_TEST_HOOKS" }
else
{
	0
}
if ($JitTestHooks -ne 0)
{
	throw "The production Core SDK requires VEROSCRIPT_ENABLE_JIT_TEST_HOOKS=OFF"
}
$CmakeArchitecture = switch ($Architecture.ToLowerInvariant())
{
	"x64" { "x64" }
	"x86" { "Win32" }
	"arm64" { "ARM64" }
	default { throw "Unsupported Windows SDK architecture '$Architecture'." }
}
$PackagePointerSize = if ($Architecture -eq "x86") { 4 }
else
{
	8
}
$AsmJitX64 = if ($Architecture -eq "x64") { 1 }
else
{
	0
}
$AsmJitA64 = if ($Architecture -eq "arm64") { 1 }
else
{
	0
}
$AsmJitX86 = if ($Architecture -eq "x86") { 1 }
else
{
	0
}
$VersionWriter = Join-Path $PSScriptRoot "../CMake/write-cmake-package-version.cmake"
if (-not (Test-Path -LiteralPath $VersionWriter -PathType Leaf))
{
	throw "CMake package-version writer not found: $VersionWriter"
}

try
{
	New-Item -ItemType Directory -Path (Split-Path $ConsumerBuild -Parent) -Force | Out-Null
	foreach ($Directory in @("include", "lib", "bin", "lib\cmake\VeroScript", "assets"))
	{
		New-Item -ItemType Directory -Path (Join-Path $Stage $Directory) -Force | Out-Null
	}
	foreach ($HeaderSet in @(
		@{ Source = "$ModuleRelative\VeroScript\Public"; Destination = "include\Script" },
		@{ Source = "$ModuleRelative\VeroScriptTooling\Public"; Destination = "include\Tooling" }
	))
	{
		Copy-Item -LiteralPath (Join-Path $RepoRoot $HeaderSet.Source) `
			-Destination (Join-Path $Stage $HeaderSet.Destination) -Recurse
	}
	Copy-Item -LiteralPath (Join-Path $BuildRoot "VeroScript/Generated/VeroScriptJitBuild.h") -Destination (Join-Path $Stage "include/Script")
	Copy-Item -LiteralPath ($Libraries | ForEach-Object { Join-Path $LibRoot $_ }) -Destination (Join-Path $Stage "lib")
	Copy-Item -LiteralPath ($Tools | ForEach-Object { Join-Path $BinRoot $_ }) -Destination (Join-Path $Stage "bin")
	Copy-VeroScriptWindowsRuntime $BuildRoot (Join-Path $Stage 'bin') $Config
	Copy-Item -LiteralPath (Join-Path $RepoRoot "$ModuleRelative\VeroScript\Resources\Scripts\stdlib") -Destination (Join-Path $Stage "assets\stdlib") -Recurse
	Copy-VeroScriptLocalization $RepoRoot (Join-Path $Stage "assets\localization") runtime
	foreach ($Module in $RequiredStdlibModules)
	{
		$ModulePath = Join-Path $Stage ("assets\stdlib\" + $Module)
		if (-not (Test-Path -LiteralPath $ModulePath -PathType Leaf))
		{
			throw "Core SDK staging package is missing standard-library module: $Module"
		}
	}
	Copy-Item -LiteralPath (Join-Path $RepoRoot "LICENSE") -Destination $Stage
	. (Join-Path $PSScriptRoot 'veroscript-packages.ps1')
	. (Join-Path $PSScriptRoot 'veroscript-vm.ps1')
	Copy-VeroScriptVmSdk $BuildRoot $Stage $Config
	Copy-VeroScriptPackageManagerSdk $BuildRoot $Stage $Config $Version
	Copy-VeroScriptPackageClient $ModuleRoot (Join-Path $Stage 'bin/veroscript.exe') $Stage 'core-sdk'
	Copy-VeroScriptApplicationRuntime $ModuleRoot $BinRoot $Stage 'bin'

	$SdkManifest = [ordered]@{ schemaVersion = 1; product = 'VeroScript Core SDK'; version = $Version; buildId = $CoreBuildId; platform = 'windows'; architecture = $Architecture; jit = $JitIdentity }
	$SdkManifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Stage 'sdk-manifest.json') -Encoding UTF8
	$ConfigText = Get-Content -LiteralPath (Join-Path $ModuleRoot 'VeroScript/Resources/Tools/CMake/VeroScriptConfig.cmake.in') -Raw
	$Values = @{ VERSION = $Version; BUILD_ID = $CoreBuildId; STATIC_PREFIX = ''; STATIC_SUFFIX = '.lib' }
	foreach ($Key in $Values.Keys) { $ConfigText = $ConfigText.Replace(('@' + $Key + '@'), [string]$Values[$Key]) }
	if ($ConfigText -match '@[A-Z_]+@') { throw 'Unexpanded SDK configuration token' }
	$ConfigText | Set-Content -LiteralPath (Join-Path $Stage "lib\cmake\VeroScript\VeroScriptConfig.cmake") -Encoding UTF8
	$VersionFile = Join-Path $Stage "lib\cmake\VeroScript\VeroScriptConfigVersion.cmake"
	& cmake "-DOUTPUT_FILE:FILEPATH=$($VersionFile.Replace('\', '/'))" `
		"-DPACKAGE_SEMANTIC_VERSION:STRING=$Version" `
		"-DPACKAGE_POINTER_SIZE:STRING=$PackagePointerSize" -P $VersionWriter
	if ($LASTEXITCODE -ne 0) { throw "Core SDK CMake version generation failed ($LASTEXITCODE)." }
	Get-ChildItem -LiteralPath $Stage -Directory -Recurse -Force -ErrorAction SilentlyContinue |
		Where-Object { $_.Name -in @(".veroscript", ".verocache") } | Sort-Object FullName -Descending | Remove-Item -Recurse -Force

	$Generator = (Get-Content -LiteralPath (Join-Path $BuildRoot 'CMakeCache.txt') |
		Where-Object { $_ -match '^CMAKE_GENERATOR:INTERNAL=' }) -replace '^CMAKE_GENERATOR:INTERNAL=', ''
	if (-not $Generator) { throw 'Native CMake generator not found for SDK verification.' }
	& cmake -S (Join-Path $RepoRoot "$ModuleRelative/VeroScript/Resources/Examples/EmbedMinimal") -B $ConsumerBuild `
		"-DCMAKE_PREFIX_PATH=$Stage" "-DVEROSCRIPT_SDK_PACKAGE_VERSION:STRING=$Version" `
		-G $Generator -A $CmakeArchitecture
	if ($LASTEXITCODE -ne 0) { throw "Core SDK external-consumer configure failed ($LASTEXITCODE)." }
	& cmake --build $ConsumerBuild --config $Config -- /m:2
	if ($LASTEXITCODE -ne 0) { throw "Core SDK external-consumer build failed ($LASTEXITCODE)." }
	& ctest --test-dir $ConsumerBuild -C $Config --output-on-failure --no-tests=error
	if ($LASTEXITCODE -ne 0) { throw "Core SDK external-consumer test failed ($LASTEXITCODE)." }

	$OldVeroPath = [Environment]::GetEnvironmentVariable("VERO_PATH", "Process")
	$OldStdlibPath = [Environment]::GetEnvironmentVariable("VERO_STDLIB_PATH", "Process")
	Push-Location $ConsumerBuild
	try
	{
		[Environment]::SetEnvironmentVariable("VERO_PATH", $null, "Process")
		[Environment]::SetEnvironmentVariable("VERO_STDLIB_PATH", $null, "Process")
		& (Join-Path $Stage "bin\veroscript.exe") --bc-cache=off `
			(Join-Path $RepoRoot "$ModuleRelative/VeroScript/Resources/Scripts/examples/standard_library_algorithms.vs")
		if ($LASTEXITCODE -ne 0) { throw "Packaged Core SDK default stdlib discovery failed ($LASTEXITCODE)." }
	}
	finally
	{
		Pop-Location
		[Environment]::SetEnvironmentVariable("VERO_PATH", $OldVeroPath, "Process")
		[Environment]::SetEnvironmentVariable("VERO_STDLIB_PATH", $OldStdlibPath, "Process")
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
	if (Test-Path -LiteralPath $ConsumerBuild) { Remove-Item -LiteralPath $ConsumerBuild -Recurse -Force -ErrorAction SilentlyContinue }
}

Write-Host "Core SDK directory: $PackageDir" -ForegroundColor Green
Write-Host "Core SDK archive:   $ZipPath" -ForegroundColor Green
