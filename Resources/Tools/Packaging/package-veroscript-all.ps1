#Requires -Version 5.1
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

[CmdletBinding()]
param(
	[ValidatePattern('^\d+\.\d+\.\d+$')]
	[string]$Version = '',
	[string]$VersionsFile = '',
	[string[]]$Businesses = @(),
	[string]$BuildDirectory = '',
	[string]$CoreSdk = '',
	[string]$CppSdk = '',
	[switch]$DataOnly,
	[string]$ConfigurePreset = '',
	[ValidateSet('Release')]
	[string]$Config = 'Release',
	[ValidateSet('win-x64','win-arm64')]
	[string]$Runtime = 'win-x64',
	[string]$OutRoot = '',
	[switch]$SkipBuild,
	[switch]$KeepFailedStage,
	[string[]]$PythonExecutables = @(),
	[string]$SigningCertificateThumbprint = $env:VEROSCRIPT_SIGNING_CERTIFICATE_THUMBPRINT,
	[string]$TimestampServer = $env:VEROSCRIPT_TIMESTAMP_SERVER,
	[switch]$RequireSignedInstallers
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrEmpty($env:DOTNET_ROLL_FORWARD)) {
	$env:DOTNET_ROLL_FORWARD = 'Major'
}
$Architecture = $Runtime.Substring(4)
$CMakeArchitecture = if ($Architecture -eq 'arm64') {
	'ARM64'
}
else
{
	'x64'
}
# Full publication runs target binaries, Python and installer checks on this host.
$NativeHostArchitecture = if ($env:PROCESSOR_ARCHITEW6432) {
	$env:PROCESSOR_ARCHITEW6432
}
else
{
	$env:PROCESSOR_ARCHITECTURE
}
$ExpectedHostArchitecture = if ($Architecture -eq 'arm64') {
	'ARM64'
}
else
{
	'AMD64'
}
if ($NativeHostArchitecture -ine $ExpectedHostArchitecture) {
	throw "Full $Runtime publication requires a native $ExpectedHostArchitecture Windows host. Use standalone SDK packaging for cross-compilation."
}
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$ModuleRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$ModuleRelative = if ($ModuleRoot -eq $RepoRoot) { '.' }
else
{
	$ModuleRoot.Substring($RepoRoot.TrimEnd('\', '/').Length + 1)
}
. (Join-Path $PSScriptRoot 'veroscript-assets.ps1')
. (Join-Path $PSScriptRoot 'veroscript-output.ps1')
. (Join-Path $PSScriptRoot 'veroscript-packages.ps1')
	. (Join-Path $PSScriptRoot 'veroscript-vm.ps1')
$ReleaseConfiguration = Get-VeroScriptReleaseConfiguration $RepoRoot $VersionsFile
$Versions = $ReleaseConfiguration.businesses
$KnownBusinesses = @($ReleaseConfiguration.targetBusinesses.$Runtime)
if ($Businesses.Count -eq 0) { $Businesses = $KnownBusinesses }
if (@($Businesses | Where-Object { $_ -cnotin $KnownBusinesses }).Count) { throw 'Unknown release business.' }
if ($Version) {
	foreach ($Business in $Businesses) {
		$Versions.$Business = $Version
	}
}
if (@($Businesses | Select-Object -Unique).Count -ne $Businesses.Count) {
	throw 'Duplicate release businesses.'
}
$NativeVersion = $ReleaseConfiguration.nativeVersion
if (@($Businesses | Where-Object { $_ -notin @('Json', 'Network', 'Http', 'WebSocket', 'Boost', 'Curl', 'Platform', 'Graphics', 'Audio', 'Sdl3', 'Image', 'Font', 'Stb', 'FontBackend', 'Gui', 'RmlUi') }).Count -eq 0) {
	$ModuleArguments = @((Join-Path $PSScriptRoot 'package-veroscript-modules.py'), '--repo-root', $RepoRoot, '--target', $Runtime)
	foreach ($Business in $Businesses) { $ModuleArguments += @('--business', $Business) }
	if ($BuildDirectory) { $ModuleArguments += @('--build-directory', $BuildDirectory) }
	elseif ($ConfigurePreset)
	{
		$ModuleArguments += @('--build-directory', (Join-Path $RepoRoot "build/$ConfigurePreset"))
	}
	if ($VersionsFile) { $ModuleArguments += @('--versions-file', $VersionsFile) }
	if ($Version) { $ModuleArguments += @('--version', $Version) }
	if ($OutRoot) { $ModuleArguments += @('--out-root', $OutRoot) }
	if ($CoreSdk) { $ModuleArguments += @('--core-sdk', $CoreSdk) }
	if ($CppSdk) { $ModuleArguments += @('--cpp-sdk', $CppSdk) }
	if ($SkipBuild) { $ModuleArguments += '--skip-build' }
	if ($DataOnly) { $ModuleArguments += '--data-only' }
	& python @ModuleArguments
	if ($LASTEXITCODE -ne 0) { throw "Module publication failed ($LASTEXITCODE)." }
	return
}
if ($DataOnly) { throw '-DataOnly requires an SDK module-only business selection.' }
$Version = $Versions.CoreSDK
if ('Python' -in $Businesses) {
	$InterpreterArguments = @('--check-interpreters', '--repo-root', $RepoRoot, '--target', $Runtime)
	foreach ($Executable in $PythonExecutables) { $InterpreterArguments += @('--python', $Executable) }
}

function Assert-SafeReleaseOutputPath([string]$Path)
{
	foreach ($Collection in @((Join-Path (Get-VeroScriptModuleRoot $RepoRoot) 'Release'), (Join-Path $RepoRoot 'dist'), (Join-Path $RepoRoot '.tmp'))) {
		if ($Path.TrimEnd('\', '/').Equals($Collection, [StringComparison]::OrdinalIgnoreCase)) {
			throw "Refusing to replace a release collection root: $Path"
		}
	}
	$Current = [IO.DirectoryInfo]::new($Path)
	while ($null -ne $Current -and
		   -not $Current.FullName.Equals($RepoRoot, [StringComparison]::OrdinalIgnoreCase)) {
		$Attributes = Get-VeroScriptPathAttributes $Current.FullName
		if ($null -ne $Attributes -and ($Attributes -band [IO.FileAttributes]::ReparsePoint)) {
			throw "The release output must not use a junction or symbolic-link path: $($Current.FullName)"
		}
		$Current = $Current.Parent
	}

	if (Test-Path -LiteralPath $Path -PathType Leaf) {
		throw "The release output points to a file: $Path"
	}
	if (-not (Test-Path -LiteralPath $Path -PathType Container)) { return }
	if (@(Get-ChildItem -LiteralPath $Path -Force).Count -eq 0) { return }

	$ExistingManifestPath = Join-Path $Path 'release-manifest.json'
	$ExistingChecksumsPath = Join-Path $Path 'checksums.txt'
	if (-not (Test-Path -LiteralPath $ExistingManifestPath -PathType Leaf) -or
		-not (Test-Path -LiteralPath $ExistingChecksumsPath -PathType Leaf)) {
		throw "Refusing to replace a non-empty directory that is not a VeroScript release: $Path"
	}
	try {
		$ExistingManifest = Get-Content -LiteralPath $ExistingManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
	}
	catch
	{
		throw "Refusing to replace a directory with an invalid release manifest: $Path"
	}
	if ($ExistingManifest.schemaVersion -ne 2 -or $ExistingManifest.product -ne 'VeroScript Release Candidate') {
		throw "Refusing to replace a directory that is not a VeroScript release: $Path"
	}
	if (($ExistingManifest.version -ne $Version) -or
		$ExistingManifest.platform -ne 'windows' -or $ExistingManifest.architecture -ne $Architecture) {
		throw "Release output belongs to a different version or target: $Path"
	}
}

$PresetSuffix = if ($Architecture -eq 'arm64') {
	'-arm64'
}
else
{
	''
}
$MsvcPresets = @(
	@{ Name = "windows-msvc-vs2026$PresetSuffix"; Major = 18 },
	@{ Name = "windows-msvc-vs2022$PresetSuffix"; Major = 17 },
	@{ Name = "windows-msvc-vs2019$PresetSuffix"; Major = 16 }
)
if ([string]::IsNullOrWhiteSpace($ConfigurePreset)) {
	$ConfigurePreset = $MsvcPresets |
		Where-Object { Test-Path -LiteralPath (Join-Path $RepoRoot ("build\{0}\CMakeCache.txt" -f $_.Name)) -PathType Leaf } |
		Select-Object -First 1 -ExpandProperty Name

	if (-not $ConfigurePreset) {
		$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
		if (Test-Path -LiteralPath $VsWhere -PathType Leaf) {
			$RequiredComponent = if ($Architecture -eq 'arm64') {
				'Microsoft.VisualStudio.Component.VC.Tools.ARM64'
			}
			else
			{
				'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
			}
			$InstalledVersion = (& $VsWhere -latest -products '*' -requires $RequiredComponent -property installationVersion | Select-Object -First 1)
			if ($InstalledVersion) {
				$InstalledMajor = ([version]$InstalledVersion).Major
				$ConfigurePreset = $MsvcPresets |
					Where-Object { $_.Major -eq $InstalledMajor } |
					Select-Object -First 1 -ExpandProperty Name
			}
		}
	}

	if (-not $ConfigurePreset) {
		throw 'No supported MSVC preset could be selected. Pass -ConfigurePreset explicitly.'
	}
	Write-Host "Selected native release preset: $ConfigurePreset" -ForegroundColor Cyan
}
$UsesDefaultOutputRoot = [string]::IsNullOrWhiteSpace($OutRoot)
if ($UsesDefaultOutputRoot) {
	$OutRoot = "$ModuleRelative\Release"
}
$OutputRoot = if ([IO.Path]::IsPathRooted($OutRoot)) {
	[IO.Path]::GetFullPath($OutRoot)
}
else
{
	[IO.Path]::GetFullPath((Join-Path $RepoRoot $OutRoot))
}
$RepoPrefix = $RepoRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if ($OutputRoot -eq $RepoRoot -or -not $OutputRoot.StartsWith($RepoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
	throw 'The release output must be a dedicated directory below the repository root.'
}
Invoke-VeroScriptBusinessRelease @('guard', '--repo-root', $RepoRoot, '--release-directory', $OutputRoot)

$BuildRoot = if ($BuildDirectory) {
	if ([IO.Path]::IsPathRooted($BuildDirectory)) { [IO.Path]::GetFullPath($BuildDirectory) }
	else
	{
		[IO.Path]::GetFullPath((Join-Path $RepoRoot $BuildDirectory))
	}
}
else
{
	Join-Path $RepoRoot (Join-Path 'build' $ConfigurePreset)
}
$BinRoot = Join-Path $BuildRoot (Join-Path 'bin' $Config)
$LibRoot = Join-Path $BuildRoot (Join-Path 'lib' $Config)
$ScriptsRoot = Join-Path $RepoRoot "$ModuleRelative\VeroScript\Resources\Scripts"
$WebAssemblyBuildRoot = Join-Path $RepoRoot 'build\webassembly'
$WebAssemblyBinRoot = Join-Path $WebAssemblyBuildRoot 'bin'
$WebAssemblyBuildScript = Join-Path $PSScriptRoot 'build-veroscript-webassembly.ps1'
$WebAssemblyVerifyScript = Join-Path $PSScriptRoot 'verify-veroscript-webassembly.ps1'
$ExamplesValidationScript = Join-Path $RepoRoot "$ModuleRelative\VeroScriptCli\Private\Tests\VeroScriptExamplesCliTest.cmake"
$InstallerSigningScript = Join-Path $PSScriptRoot 'sign-veroscript-installer.ps1'
$CMakeVersionWriter = Join-Path $PSScriptRoot '../CMake/write-cmake-package-version.cmake'
$CMakeCachePath = Join-Path $BuildRoot 'CMakeCache.txt'
foreach ($Directory in @($BuildRoot, $WebAssemblyBuildRoot)) {
	$CachePath = Join-Path $Directory 'CMakeCache.txt'
	if (-not (Test-Path -LiteralPath $CachePath -PathType Leaf)) { continue }
	$CachedSource = (Get-Content -LiteralPath $CachePath -Encoding UTF8 |
		Where-Object { $_ -like 'CMAKE_HOME_DIRECTORY:INTERNAL=*' } |
		Select-Object -First 1) -replace '^CMAKE_HOME_DIRECTORY:INTERNAL=', ''
	if (-not $CachedSource -or -not ([IO.Path]::GetFullPath($CachedSource)).Equals($RepoRoot, [StringComparison]::OrdinalIgnoreCase)) {
		throw "Build cache belongs to a different source: $Directory"
	}
}
$StageRoot = Join-Path $RepoRoot ('.tmp\veroscript-release-stage-' + [guid]::NewGuid().ToString('N'))
Assert-VeroScriptComponentOutputPath $StageRoot $RepoRoot
$ReleaseRoot = Join-Path $StageRoot 'release'
$WorkRoot = Join-Path $StageRoot 'work'
$RequiredStdlibModules = @(
	'algorithm.vs', 'attribute.vs', 'callable.vs', 'compiler.vs', 'concurrent.vs', 'core.vs', 'delegate.vs', 'i18n.vs', 'io.vs',
	'math.vs', 'memory.vs', 'numeric.vs', 'path.vs', 'random.vs', 'string.vs')

function Invoke-Checked([string]$FilePath, [string[]]$Arguments)
{
	$null = Get-Command $FilePath -ErrorAction Stop
	# Windows PowerShell turns redirected native stderr into error records.
	# Tools also write progress there; their exit code decides success.
	$ErrorActionPreference = 'Continue'
	$global:LASTEXITCODE = 1
	& $FilePath @Arguments 2>&1 | ForEach-Object { $_.ToString() }
	if ($LASTEXITCODE -ne 0) {
		throw "Command failed ($LASTEXITCODE): $FilePath $($Arguments -join ' ')"
	}
}

function Require-File([string]$Path)
{
	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Required file not found: $Path" }
}

function Assert-NativeArchitecture([string]$Path)
{
	$Stream = [IO.File]::OpenRead($Path)
	$Reader = [IO.BinaryReader]::new($Stream)
	try {
		if ($Reader.ReadUInt16() -ne 0x5A4D) {
			throw "Not a PE executable: $Path"
		}
		$Stream.Position = 0x3C
		$PeOffset = $Reader.ReadUInt32()
		if ($PeOffset -gt ($Stream.Length - 6)) {
			throw "Invalid PE header: $Path"
		}
		$Stream.Position = $PeOffset
		if ($Reader.ReadUInt32() -ne 0x00004550) {
			throw "Invalid PE signature: $Path"
		}
		$ExpectedMachine = if ($Architecture -eq 'arm64') {
			0xAA64
		}
		else
		{
			0x8664
		}
		if ($Reader.ReadUInt16() -ne $ExpectedMachine) {
			throw "Native binary does not target ${Runtime}: $Path"
		}
	}
	finally
	{
		$Reader.Dispose()
	}
}

function Require-Directory([string]$Path)
{
	if (-not (Test-Path -LiteralPath $Path -PathType Container)) { throw "Required directory not found: $Path" }
}

function Get-RequiredCMakeBool([string]$Name)
{
	if (-not (Test-Path -LiteralPath $CMakeCachePath -PathType Leaf)) {
		throw "Release CMake cache not found: $CMakeCachePath"
	}
	$Line = Get-Content -LiteralPath $CMakeCachePath |
		Where-Object { $_ -match "^$([regex]::Escape($Name)):(BOOL|INTERNAL)=" } |
		Select-Object -First 1
	if (-not $Line) { throw "Release CMake cache is missing $Name" }
	$Value = ($Line -replace '^[^=]*=', '').Trim()
	if ($Value -match '^(1|ON|TRUE|YES|Y)$') { return 1 }
	if ($Value -match '^(0|OFF|FALSE|NO|N)$') { return 0 }
	throw "Release CMake cache has invalid $Name value '$Value'"
}

function Invoke-FileSystemRetry([scriptblock]$Operation, [string]$Description)
{
	for ($Attempt = 1; $Attempt -le 10; ++$Attempt) {
		try {
			& $Operation
			return
		}
		catch
		{
			if ($Attempt -eq 10) { throw }
			Write-Verbose "$Description is temporarily unavailable; retrying ($Attempt/10)."
			Start-Sleep -Milliseconds ([Math]::Min(200 * [Math]::Pow(2, $Attempt - 1), 2000))
		}
	}
}

function Move-DirectoryWithRetry([string]$Source, [string]$Destination, [string]$Description)
{
	for ($Attempt = 1; $Attempt -le 10; ++$Attempt) {
		if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
			if (Test-Path -LiteralPath $Destination -PathType Container) { return }
			throw "$Description failed because the source directory disappeared: $Source"
		}
		if (Test-Path -LiteralPath $Destination) {
			throw "$Description failed because the destination already exists: $Destination"
		}

		try {
			# Directory.Move performs a single same-volume rename. PowerShell's
			# Move-Item can partially merge a directory before reporting a
			# transient sharing violation, which makes a blind retry unsafe.
			[IO.Directory]::Move($Source, $Destination)
			return
		}
		catch
		{
			# A provider or filesystem can report an error after the rename was
			# committed. Treat that observable terminal state as success.
			if (-not (Test-Path -LiteralPath $Source) -and
				(Test-Path -LiteralPath $Destination -PathType Container)) {
				return
			}
			if ($Attempt -eq 10) { throw }
			Write-Verbose "$Description is temporarily unavailable; retrying ($Attempt/10)."
			Start-Sleep -Milliseconds ([Math]::Min(200 * [Math]::Pow(2, $Attempt - 1), 2000))
		}
	}
}

function Copy-VerifiedReleaseWhenMoveIsLocked([string]$Source, [string]$Destination)
{
	if (Test-Path -LiteralPath $Destination) {
		throw "The release destination already exists: $Destination"
	}

	# Keep the destination absent until a complete copied candidate has passed
	# the same release verifier. Prefer one final rename when Windows allows it.
	# Keep the verification copy in the staging workspace so the destination
	# verifier cannot mistake it for a source resource.
	$CopyRoot = Join-Path $RepoRoot ('.tmp\veroscript-release-copy-' + [guid]::NewGuid().ToString('N'))
	Assert-VeroScriptComponentOutputPath $CopyRoot $RepoRoot
	if (Test-Path -LiteralPath $CopyRoot) {
		throw "The release copy destination already exists: $CopyRoot"
	}
	try {
		& robocopy $Source $CopyRoot /E /COPY:DAT /DCOPY:DAT /R:3 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
		if ($LASTEXITCODE -gt 7) {
			throw "Copying the staged release failed (robocopy exit $LASTEXITCODE)."
		}
		& (Join-Path $PSScriptRoot 'verify-veroscript-release.ps1') `
			-ReleaseDirectory $CopyRoot -Target $Runtime `
			-RequireSignedInstallers:$RequireSignedInstallers
		try {
			Move-DirectoryWithRetry $CopyRoot $Destination "Publishing the verified release copy"
		}
		catch
		{
			if (Test-Path -LiteralPath $Destination) { throw }
			# Some Windows file watchers hold directory handles long after file
			# verification. Copy only the already verified candidate, then
			# verify the exact destination before treating it as published.
			Write-Warning "The verified release copy could not be renamed; copying to the destination and verifying it."
			try {
				& robocopy $CopyRoot $Destination /E /COPY:DAT /DCOPY:DAT /R:3 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
				if ($LASTEXITCODE -gt 7) {
					throw "Copying the verified release failed (robocopy exit $LASTEXITCODE)."
				}
				& (Join-Path $PSScriptRoot 'verify-veroscript-release.ps1') `
					-ReleaseDirectory $Destination -Target $Runtime `
					-RequireSignedInstallers:$RequireSignedInstallers
			}
			catch
			{
				if (Test-Path -LiteralPath $Destination) {
					Invoke-FileSystemRetry {
						Remove-Item -LiteralPath $Destination -Recurse -Force -ErrorAction Stop
					} "Removing the incomplete release copy"
				}
				throw
			}
		}
	}
	finally
	{
		if (Test-Path -LiteralPath $CopyRoot) {
			Invoke-FileSystemRetry {
				Remove-Item -LiteralPath $CopyRoot -Recurse -Force -ErrorAction Stop
			} "Removing the temporary release copy"
		}
	}
}

function Require-StandardLibrary([string]$Root)
{
	foreach ($Module in $RequiredStdlibModules) {
		Require-File (Join-Path $Root $Module)
	}
}

function Copy-Directory([string]$Source, [string]$Destination, [string[]]$ExcludeRoot = @())
{
	Require-Directory $Source
	if (Test-Path -LiteralPath $Destination) { Remove-Item -LiteralPath $Destination -Recurse -Force }
	New-Item -ItemType Directory -Path (Split-Path $Destination -Parent) -Force | Out-Null
	if ($ExcludeRoot.Count) {
		New-Item -ItemType Directory -Path $Destination -Force | Out-Null
		Get-ChildItem -LiteralPath $Source -Force |
			Where-Object { $_.Name -notin $ExcludeRoot } |
			ForEach-Object {
				Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $Destination $_.Name) -Recurse -Force
			}
	}
	else
	{
		Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
	}
	ConvertTo-VeroScriptPortableAssetTree $Destination -SourceRoot $Source
}

function Copy-CleanExamples([string]$Destination)
{
	$Source = Join-Path $ScriptsRoot 'examples'
	Require-Directory $Source
	New-Item -ItemType Directory -Path $Destination -Force | Out-Null
	Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
		if ($_.Name -in @('.veroscript', '.verocache') -or $_.Name.StartsWith('_') -or $_.Extension -in @('.vsbc', '.vsc')) { return }
		Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $Destination $_.Name) -Recurse -Force
	}
	ConvertTo-VeroScriptPortableAssetTree $Destination -SourceRoot $Source
}

function Remove-PythonCaches([string]$Root)
{
	Get-ChildItem -LiteralPath $Root -Recurse -Directory -Force -ErrorAction SilentlyContinue |
		Where-Object { $_.Name -in @('.veroscript', '__pycache__', '.pytest_cache') } |
		Sort-Object { $_.FullName.Length } -Descending |
		Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
	Get-ChildItem -LiteralPath $Root -Recurse -File -Force -ErrorAction SilentlyContinue |
		Where-Object { $_.Extension -in @('.pyc', '.pyo') } |
		Remove-Item -Force -ErrorAction SilentlyContinue
}

function Remove-GeneratedCaches([string]$Root, [switch]$PreserveXml)
{
	Remove-PythonCaches $Root
	Get-ChildItem -LiteralPath $Root -Recurse -Directory -Force -ErrorAction SilentlyContinue |
		Where-Object { $_.Name -in @('.veroscript', '.verocache') } |
		Sort-Object { $_.FullName.Length } -Descending |
		Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
	$GeneratedExtensions = @('.vsbc', '.vsc', '.pdb')
	if (-not $PreserveXml) { $GeneratedExtensions += '.xml' }
	Get-ChildItem -LiteralPath $Root -Recurse -File -Force -ErrorAction SilentlyContinue |
		Where-Object { $_.Extension -in $GeneratedExtensions } |
		Remove-Item -Force -ErrorAction SilentlyContinue
}

function New-PackageZip([string]$PackageDirectory, [string]$ZipPath)
{
	Remove-VeroScriptAuxiliaryContent $PackageDirectory
	# Workspace settings are local state, never portable release payload.
	Get-ChildItem -LiteralPath $PackageDirectory -Directory -Recurse -Force |
		Where-Object { $_.Name -eq '.veroscript' } |
		Sort-Object { $_.FullName.Length } -Descending |
		Remove-Item -Recurse -Force
	for ($Attempt = 1; $Attempt -le 5; ++$Attempt) {
		try {
			if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
			New-VeroScriptPortableArchive $PackageDirectory $ZipPath $RepoRoot
			return
		}
		catch
		{
			if ($Attempt -eq 5) { throw }
			# A newly published executable can be held briefly by Windows
			# Defender/indexing. Retry only this recoverable packaging step.
			Start-Sleep -Milliseconds 250
		}
	}
}

function Copy-LicenseFiles([string]$PackageDirectory)
{
	Copy-Item -LiteralPath (Join-Path $RepoRoot 'LICENSE') -Destination (Join-Path $PackageDirectory 'LICENSE') -Force

}


function Write-CoreSdkCMakePackage([string]$SdkRoot)
{
	$CMakeRoot = Join-Path $SdkRoot 'lib\cmake\VeroScript'
	New-Item -ItemType Directory -Path $CMakeRoot -Force | Out-Null
	$ConfigText = Get-Content -LiteralPath (Join-Path $ModuleRoot 'VeroScript/Resources/Tools/CMake/VeroScriptConfig.cmake.in') -Raw
	$Values = @{
		VERSION = $Versions.CoreSDK; BUILD_ID = $CoreBuildId; STATIC_PREFIX = ''; STATIC_SUFFIX = '.lib'; EXE_SUFFIX = '.exe'
		HAS_ASMJIT = $CoreHasAsmJit; JIT_TEST_HOOKS = $CoreJitTestHooks; ASMJIT_X64 = [int]($Architecture -eq 'x64'); ASMJIT_A64 = [int]($Architecture -eq 'arm64')
		APPLICATION_ENABLED = Get-RequiredCMakeBool 'VEROSCRIPT_BUILD_APPLICATION'
	}
	foreach ($Key in $Values.Keys) {
		$ConfigText = $ConfigText.Replace(('@' + $Key + '@'), [string]$Values[$Key])
	}
	if ($ConfigText -match '@[A-Z_]+@') {
		throw 'Unexpanded SDK configuration token'
	}
	[IO.File]::WriteAllText((Join-Path $CMakeRoot 'VeroScriptConfig.cmake'), $ConfigText, [Text.UTF8Encoding]::new($false))
	Invoke-Checked 'cmake' @(
		"-DOUTPUT_FILE:FILEPATH=$((Join-Path $CMakeRoot 'VeroScriptConfigVersion.cmake').Replace('\', '/'))",
		"-DPACKAGE_SEMANTIC_VERSION:STRING=$Version",
		'-DPACKAGE_POINTER_SIZE:STRING=8',
		'-P', $CMakeVersionWriter)
}

function Write-CppSdkCMakePackage([string]$RuntimeRoot)
{
	$CMakeRoot = Join-Path $RuntimeRoot 'lib\cmake\VeroScriptCpp'
	New-Item -ItemType Directory -Path $CMakeRoot -Force | Out-Null
	$ConfigText = Get-Content -LiteralPath (Join-Path $ModuleRoot 'Extensions/Language/Cpp/VeroScriptCpp/Resources/Tools/CMake/VeroScriptCppConfig.cmake.in') -Raw
	$Values = @{
		VERSION = $Versions.Cpp; CORE_VERSION = $Versions.CoreSDK; BUILD_ID = $CoreBuildId; STATIC_PREFIX = ''; STATIC_SUFFIX = '.lib'; EXE_SUFFIX = '.exe'
		APPLICATION_ENABLED = Get-RequiredCMakeBool 'VEROSCRIPT_BUILD_APPLICATION'
		HAS_ASMJIT = $CoreHasAsmJit; JIT_TEST_HOOKS = $CoreJitTestHooks; ASMJIT_X64 = [int]($Architecture -eq 'x64'); ASMJIT_A64 = [int]($Architecture -eq 'arm64')
	}
	foreach ($Key in $Values.Keys) {
		$ConfigText = $ConfigText.Replace(('@' + $Key + '@'), [string]$Values[$Key])
	}
	if ($ConfigText -match '@[A-Z_]+@') {
		throw 'Unexpanded SDK configuration token'
	}
	[IO.File]::WriteAllText((Join-Path $CMakeRoot 'VeroScriptCppConfig.cmake'), $ConfigText, [Text.UTF8Encoding]::new($false))
	Invoke-Checked 'cmake' @(
		"-DOUTPUT_FILE:FILEPATH=$((Join-Path $CMakeRoot 'VeroScriptCppConfigVersion.cmake').Replace('\', '/'))",
		"-DPACKAGE_SEMANTIC_VERSION:STRING=$($Versions.Cpp)",
		'-DPACKAGE_POINTER_SIZE:STRING=8',
		'-P', $CMakeVersionWriter)
}

function Publish-EmbeddedInstaller(
	[string]$BundleRoot,
	[string]$InstallerName)
{
	if ($InstallerName -notmatch '-(\d+\.\d+\.\d+)-win-(?:x64|arm64)$') {
		throw 'Invalid installer version.'
	}
	$PackageVersion = $Matches[1]
	$ToolProject = Join-Path $RepoRoot "$ModuleRelative\VeroScriptInstaller\Tool\VeroScriptInstaller.Tool.csproj"
	$AppProject = Join-Path $RepoRoot "$ModuleRelative\VeroScriptInstaller\App\VeroScriptInstaller.csproj"
	$Archive = Join-Path $WorkRoot ($InstallerName + '-bundle.zip')
	Invoke-VeroScriptInstallerDotNet $RepoRoot @('run', '--project', $ToolProject, '-c', 'Release', '--', 'verify', '--bundle', $BundleRoot)
	Invoke-VeroScriptInstallerDotNet $RepoRoot @('run', '--project', $ToolProject, '-c', 'Release', '--', 'pack-bundle', '--bundle', $BundleRoot, '--output', $Archive)
	$PublishRoot = $null
	for ($Attempt = 1; $Attempt -le 5; ++$Attempt) {
		# Antivirus/indexing can briefly retain the newly bundled executable.
		# Publish retries use a fresh output directory so a transient lock can
		# never leave the next attempt targeting the same partial executable.
		$CandidatePublishRoot = Join-Path $WorkRoot ($InstallerName + '-publish-' + $Attempt)
		Invoke-VeroScriptInstallerDotNet $RepoRoot @('publish', $AppProject, '-c', 'Release', '-r', $Runtime, '--self-contained', 'true',
			'-p:PublishSingleFile=true', '-p:IncludeNativeLibrariesForSelfExtract=true',
			'-p:EnableCompressionInSingleFile=true', '-p:PublishTrimmed=false',
			'-p:PublishReadyToRun=false', '-p:DebugType=None', '-p:DebugSymbols=false',
			"-p:Version=$PackageVersion", "-p:InstallerBundleArchive=$Archive", '-o', $CandidatePublishRoot) -AllowFailure
		if ($LASTEXITCODE -eq 0) {
			$PublishRoot = $CandidatePublishRoot
			break
		}
		if ($Attempt -eq 5) {
			throw "Command failed ($LASTEXITCODE): dotnet publish $AppProject"
		}
		Start-Sleep -Milliseconds (500 * $Attempt)
	}
	$Published = Join-Path $PublishRoot 'VeroScriptInstaller.exe'
	Require-File $Published
	Assert-NativeArchitecture $Published
	Assert-VeroScriptInstallerLicense $Published (Join-Path $RepoRoot 'LICENSE')
	foreach ($Argument in @('--verify-localization', '--verify-embedded-bundle')) {
		$Process = Start-Process -FilePath $Published -ArgumentList $Argument -WindowStyle Hidden -Wait -PassThru
		if ($Process.ExitCode -ne 0) { throw "$InstallerName verification failed: $Argument" }
	}
	$InstallerRoot = Join-Path $ReleaseRoot 'installer'
	New-Item -ItemType Directory -Path $InstallerRoot -Force | Out-Null
	Copy-Item -LiteralPath $Published -Destination (Join-Path $InstallerRoot ($InstallerName + '.exe')) -Force
}

function Test-SdkConsumer([string]$SdkRoot, [string]$Example, [string]$Name)
{
	$PackageVersion = if ($Name -eq 'interop-sdk') {
		$Versions.Interop
	}
	else
	{
		$Versions.CoreSDK
	}
	# A real external consumer prevents a ZIP with missing imported targets,
	# headers or transitive libraries from being promoted as a release.
	$Cache = Get-Content -LiteralPath (Join-Path $BuildRoot 'CMakeCache.txt')
	$Generator = ($Cache | Where-Object { $_ -match '^CMAKE_GENERATOR:INTERNAL=' }) -replace '^CMAKE_GENERATOR:INTERNAL=', ''
	if (-not $Generator) { throw 'Native CMake generator not found for SDK verification.' }
	$ConsumerRoot = Join-Path $WorkRoot ($Name + '-consumer')
	$ConsumerSource = switch ($Name) {
		'core-sdk' { Join-Path $RepoRoot "$ModuleRelative/VeroScript/Resources/Examples/EmbedMinimal" }
		'core-annotation-sdk' { Join-Path $RepoRoot "$ModuleRelative/VeroScript/Resources/Examples/AnnotationProcessing" }
		'core-script-extensions-sdk' { Join-Path $RepoRoot "$ModuleRelative/VeroScript/Resources/Examples/ScriptCompileExtensions" }
		'interop-sdk' { Join-Path $RepoRoot "$ModuleRelative/VeroScriptInterop/Resources/Examples/InteropMinimal" }
		default { throw "Unknown SDK consumer: $Name" }
	}
	$ConsumerArguments = @('-S', $ConsumerSource, '-B', $ConsumerRoot,
		'-G', $Generator, '-A', $CMakeArchitecture, "-DCMAKE_PREFIX_PATH=$SdkRoot",
		"-DVEROSCRIPT_SDK_PACKAGE_VERSION:STRING=$PackageVersion", "-DVEROSCRIPT_NATIVE_VERSION:STRING=$NativeVersion")
	if ($Name -eq 'interop-sdk') {
		$ConsumerArguments += "-DVEROSCRIPT_SDK_ABI_TEST_SOURCE:FILEPATH=$(Join-Path $RepoRoot "$ModuleRelative/VeroScriptInterop/Private/Tests/VeroScriptInteropCHeaderTest.c")"
	}
	Invoke-Checked 'cmake' $ConsumerArguments
	Invoke-Checked 'cmake' @('--build', $ConsumerRoot, '--config', $Config, '--parallel', '2')
	Invoke-Checked 'ctest' @('--test-dir', $ConsumerRoot, '-C', $Config, '--output-on-failure', '--no-tests=error')
}

function Test-CppSdkConsumer([string]$CoreSdkRoot, [string]$CppPackageRoot)
{
	# Configure from the staged package only. The C++ config must discover the
	# separately staged, exact-build Core package and its public host services.
	$Cache = Get-Content -LiteralPath (Join-Path $BuildRoot 'CMakeCache.txt')
	$Generator = ($Cache | Where-Object { $_ -match '^CMAKE_GENERATOR:INTERNAL=' }) -replace '^CMAKE_GENERATOR:INTERNAL=', ''
	if (-not $Generator) { throw 'Native CMake generator not found for C++ SDK verification.' }
	$ConsumerRoot = Join-Path $WorkRoot 'cpp-sdk-consumer'
	$CppRuntimeRoot = Join-Path $CppPackageRoot 'Runtime'
	$ConsumerSource = Join-Path $RepoRoot "$ModuleRelative/Extensions/Language/Cpp/VeroScriptCpp/Resources/Examples/CppMinimal"
	$PrefixPath = $CoreSdkRoot + ';' + $CppRuntimeRoot
	Invoke-Checked 'cmake' @('-S', $ConsumerSource, '-B', $ConsumerRoot,
		'-G', $Generator, '-A', $CMakeArchitecture, "-DCMAKE_PREFIX_PATH=$PrefixPath",
		"-DVEROSCRIPT_SDK_PACKAGE_VERSION:STRING=$($Versions.Cpp)",
		"-DVEROSCRIPT_NATIVE_VERSION:STRING=$NativeVersion")
	Invoke-Checked 'cmake' @('--build', $ConsumerRoot, '--config', $Config, '--parallel', '2')
	Invoke-Checked 'ctest' @('--test-dir', $ConsumerRoot, '-C', $Config, '--output-on-failure', '--no-tests=error')
}

function Test-CoreSdkRuntimeAssets([string]$SdkRoot)
{
	$OldVeroPath = [Environment]::GetEnvironmentVariable('VERO_PATH', 'Process')
	$OldStdlibPath = [Environment]::GetEnvironmentVariable('VERO_STDLIB_PATH', 'Process')
	Push-Location $WorkRoot
	try {
		[Environment]::SetEnvironmentVariable('VERO_PATH', $null, 'Process')
		[Environment]::SetEnvironmentVariable('VERO_STDLIB_PATH', $null, 'Process')
		& (Join-Path $SdkRoot 'bin\veroscript.exe') --bc-cache=off `
			(Join-Path $ScriptsRoot 'examples/standard_library_algorithms.vs')
		if ($LASTEXITCODE -ne 0) {
			throw "Packaged Core SDK default stdlib discovery failed ($LASTEXITCODE)."
		}
	}
	finally
	{
		Pop-Location
		[Environment]::SetEnvironmentVariable('VERO_PATH', $OldVeroPath, 'Process')
		[Environment]::SetEnvironmentVariable('VERO_STDLIB_PATH', $OldStdlibPath, 'Process')
	}
}

try {
	New-Item -ItemType Directory -Path $ReleaseRoot, $WorkRoot -Force | Out-Null
	$IdeBusinesses = @($Businesses | Where-Object { $_ -in $ReleaseConfiguration.ideBusinesses })
	$IdeBuildIds = @{}
	$IdeDependencies = $null
	if ($IdeBusinesses.Count -gt 0) {
		$IdeVersionsFile = Join-Path $WorkRoot 'ide-versions.json'
		[IO.File]::WriteAllText($IdeVersionsFile, ($ReleaseConfiguration | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
		$IdeCheckArguments = @((Join-Path $PSScriptRoot 'package-veroscript-ide.py'), '--repo-root', $RepoRoot,
			'--versions-file', $IdeVersionsFile, '--check-sources', '--check-prerequisites')
		foreach ($Business in $IdeBusinesses) { $IdeCheckArguments += @('--business', $Business) }
		Invoke-Checked 'python' $IdeCheckArguments
	}
	Invoke-VeroScriptDocs @('check')
	Invoke-VeroScriptDocs @('check-collection')
	if ('Python' -in $Businesses) { Invoke-VeroScriptPythonWheels $InterpreterArguments }
	Require-Directory (Join-Path $RepoRoot "$ModuleRelative/Resources/Assets")
	foreach ($Required in @('stdlib', 'examples')) {
		Require-Directory (Join-Path $ScriptsRoot $Required)
	}
	Require-StandardLibrary (Join-Path $ScriptsRoot 'stdlib')
	Require-File $WebAssemblyBuildScript
	Require-File $WebAssemblyVerifyScript
	Require-File $ExamplesValidationScript
	Require-File $InstallerSigningScript
	Require-File $CMakeVersionWriter
	Invoke-VeroScriptInstallerDotNet $RepoRoot @('--version')

	if (-not $SkipBuild) {
		$CMakeCache = Join-Path $BuildRoot 'CMakeCache.txt'
		if (-not (Test-Path -LiteralPath $CMakeCache -PathType Leaf)) {
			Write-Host "Configuring native release build: $ConfigurePreset" -ForegroundColor Cyan
			Invoke-Checked 'cmake' @('--preset', $ConfigurePreset, '-S', $RepoRoot, '-B', $BuildRoot)
		}
		Invoke-Checked 'cmake' @('-S', $RepoRoot, '-B', $BuildRoot, "-DVS_VEROSCRIPT_CSHARP_VERSION=$($Versions.CSharp)", "-DVS_VEROSCRIPT_ASMJIT_VERSION=$($Versions.Jit)", "-DVS_VEROSCRIPT_VM_VERSION=$($Versions.Vm)",
			'-DVEROSCRIPT_BUILD_VM_BACKEND=ON', '-DVEROSCRIPT_BUILD_APPLICATION=ON', '-DVEROSCRIPT_BUILD_BUILD_SYSTEM=ON',
			'-DVEROSCRIPT_BUILD_PACKAGE_MANAGER=ON', '-DVEROSCRIPT_BUILD_PACKAGE_RUNTIME=ON',
			'-DVEROSCRIPT_BUILD_JSON=ON', '-DVEROSCRIPT_BUILD_NETWORK=ON', '-DVEROSCRIPT_BUILD_HTTP=ON', '-DVEROSCRIPT_BUILD_WEBSOCKET=ON', '-DVEROSCRIPT_BUILD_BOOST=ON', '-DVEROSCRIPT_BUILD_CURL=ON',
			'-DVEROSCRIPT_JSON_BUILD_BINDINGS=ON', '-DVEROSCRIPT_NETWORK_BUILD_BINDINGS=ON',
			'-DVEROSCRIPT_BUILD_PLATFORM=ON', '-DVEROSCRIPT_BUILD_GRAPHICS=ON', '-DVEROSCRIPT_BUILD_AUDIO=ON',
			'-DVEROSCRIPT_BUILD_SDL3=ON', '-DVEROSCRIPT_VISUAL_SCRIPT_BINDINGS=ON',
			'-DVEROSCRIPT_BUILD_GUI=ON', '-DVEROSCRIPT_GUI_SCRIPTING=ON', '-DVEROSCRIPT_GUI_BUILD_RMLUI=ON',
			'-DVEROSCRIPT_BUILD_STB=ON', '-DVEROSCRIPT_BUILD_FONT_BACKEND=ON', '-DVEROSCRIPT_RMLUI_NATIVE_EXTENSION=ON',
			'-DVEROSCRIPT_STB_NATIVE_EXTENSION=ON', '-DVEROSCRIPT_FONT_BACKEND_NATIVE_EXTENSION=ON',
			'-DVEROSCRIPT_SDL3_GPU=ON', '-DVEROSCRIPT_SDL3_AUDIO=ON', '-DVEROSCRIPT_SDL3_CONTROLLERS=ON')
		$NativeBuildArguments = @('--build', $BuildRoot, '--config', $Config, '--target',
			'VeroScriptCli', 'VeroScriptDap', 'VeroScriptLsp', 'VeroScriptCpp', 'VeroScriptRuntime', 'VeroScriptLauncher',
			'VeroScriptCppFrontend', 'VeroScriptCppBindGen', 'VeroScriptCppTranspiler',
			'VeroScriptCppAot', 'VeroScriptCSharpAot', 'VeroScriptPythonAot',
			'VeroScriptCSharp', 'VeroScriptCSharpPackage',
			'VeroScriptCSharpFrontend', 'VeroScriptCSharpBindGen',
			'VeroScriptCSharpTranspiler', 'VeroScriptCSharpTranspilerCli',
			'VeroScriptCSharpBindGenCli', 'VeroScriptCSharpToolsPackage',
			'VeroScriptPython', 'VeroScriptPythonFrontend', 'VeroScriptPythonBindGen',
			'VeroScriptJsonModule', 'VeroScriptNetworkModule', 'VeroScriptHttpModule', 'VeroScriptWebSocketModule', 'VeroScriptBoostModule', 'VeroScriptCurlModule', 'VeroScriptModulePackageTool',
			'VeroScriptPlatformBindings', 'VeroScriptGraphicsBindings', 'VeroScriptAudioBindings', 'VeroScriptSdl3',
			'VeroScriptImageBindings', 'VeroScriptFontBindings', 'VeroScriptStbModule', 'VeroScriptFontBackendModule',
			'VeroScriptGuiScript', 'VeroScriptGuiToolsModule', 'VeroScriptRmlUiModule',
			'VeroScriptPythonTranspiler', 'VeroScriptTooling', 'VeroScriptInterop', 'VeroScriptWebAssembly')
		Invoke-Checked 'cmake' ($NativeBuildArguments + @('--', '/m'))
		& $WebAssemblyBuildScript -Configuration $Config -BuildDirectory $WebAssemblyBuildRoot
		if ($LASTEXITCODE -ne 0) {
			throw "WebAssembly build failed with exit code $LASTEXITCODE."
		}
	}

	$CSharpCachedVersion = (Get-Content -LiteralPath (Join-Path $BuildRoot 'CMakeCache.txt') | Where-Object { $_ -match '^VS_VEROSCRIPT_CSHARP_VERSION:STRING=' }) -replace '^VS_VEROSCRIPT_CSHARP_VERSION:STRING=', ''
	if ($CSharpCachedVersion -ne $Versions.CSharp) {
		throw 'C# build/package version mismatch; rebuild without -SkipBuild.'
	}

	foreach ($File in @('veroscript.exe', 'veroscript-lsp.exe', 'veroscript-dap.exe', 'VeroScriptCppBindGen.exe', 'VeroScriptCppTranspiler.exe', 'VeroScriptInterop.dll')) {
		Require-File (Join-Path $BinRoot $File)
		Assert-NativeArchitecture (Join-Path $BinRoot $File)
	}
	$HasBackendModule = Get-RequiredCMakeBool 'VEROSCRIPT_BUILD_ASMJIT_BACKEND'
	if ($HasBackendModule -ne 1) { throw 'Full release requires the independent AsmJit backend target.' }
	if (-not $SkipBuild) { Invoke-Checked 'cmake' @('--build', $BuildRoot, '--config', $Config, '--target', 'VeroScriptAsmJitBackend') }
	$CoreHasAsmJit = if ($HasBackendModule -eq 1) { Get-RequiredCMakeBool 'VEROSCRIPT_ASMJIT_NATIVE_ENABLED' }
	else
	{
		0
	}
	$BackendLibraries = @()
	foreach ($File in ($BackendLibraries + @('VeroScript.lib', 'VeroScriptCpp.lib', 'VeroScriptCppFrontend.lib', 'VeroScriptCppBindGenLib.lib', 'VeroScriptCppTranspilerLib.lib', 'VeroScriptCppAot.lib', 'VeroScriptCSharpAot.lib', 'VeroScriptPythonAot.lib', 'VeroScriptTooling.lib', 'VeroScriptInterop.lib'))) {
		Require-File (Join-Path $LibRoot $File)
	}
	$CoreBuildId = (Get-FileHash -LiteralPath (Join-Path $LibRoot 'VeroScript.lib') -Algorithm SHA256).Hash.ToLowerInvariant()
	$PackageClientEnabled = (Get-RequiredCMakeBool 'VEROSCRIPT_BUILD_PACKAGE_MANAGER') -eq 1
	$ApplicationEnabled = (Get-RequiredCMakeBool 'VEROSCRIPT_BUILD_APPLICATION') -eq 1
	$PackageRuntimeEnabled = $PackageClientEnabled -or ((Get-RequiredCMakeBool 'VEROSCRIPT_BUILD_PACKAGE_RUNTIME') -eq 1)
	$BuildSystemEnabled = (Get-RequiredCMakeBool 'VEROSCRIPT_BUILD_BUILD_SYSTEM') -eq 1
	$CoreJitTestHooks = if ($HasBackendModule -eq 1) { Get-RequiredCMakeBool 'VEROSCRIPT_ENABLE_JIT_TEST_HOOKS' }
	else
	{
		0
	}
	if ($CoreJitTestHooks -ne 0) { throw 'The production release requires VEROSCRIPT_ENABLE_JIT_TEST_HOOKS=OFF' }
	$ToolVersion = (& (Join-Path $BinRoot 'veroscript.exe') --version 2>&1 | Out-String).Trim()
	if ($LASTEXITCODE -ne 0 -or -not $ToolVersion.StartsWith("VeroScript $NativeVersion ", [StringComparison]::Ordinal) -or
		-not $ToolVersion.Contains("[$Runtime;")) {
		throw "Release runtime identity mismatch: $ToolVersion"
	}

	Invoke-Checked 'cmake' @(
		"-DVERO_CLI=$(Join-Path $BinRoot 'veroscript.exe')",
		"-DVERO_EXAMPLES_ROOT=$(Join-Path $ScriptsRoot 'examples')",
	'-DVERO_PROCESS_TIMEOUT_SECONDS=600',
		"-DVERO_PACKAGE_OUTPUT_ROOT=$WorkRoot",
		'-P', $ExamplesValidationScript)

	# Portable runtime and installers share the functional payload policy.
	$RuntimeName = "VeroScriptRuntime-$($Versions.Runtime)-$Runtime"
	$RuntimeDirectory = Join-Path (Join-Path $ReleaseRoot 'runtime') $RuntimeName
	New-Item -ItemType Directory -Path $RuntimeDirectory -Force | Out-Null
	Copy-Item -LiteralPath (Join-Path $BinRoot 'veroscript.exe'), (Join-Path $BinRoot 'veroscript-lsp.exe'), (Join-Path $BinRoot 'veroscript-dap.exe') -Destination $RuntimeDirectory
	Copy-VeroScriptWindowsRuntime $BuildRoot $RuntimeDirectory $Config
	Copy-VeroScriptVmRuntime $BuildRoot $RuntimeDirectory $Config
	Copy-Directory (Join-Path $ScriptsRoot 'stdlib') (Join-Path $RuntimeDirectory 'stdlib')
	Require-StandardLibrary (Join-Path $RuntimeDirectory 'stdlib')
	Copy-VeroScriptLocalization $RepoRoot (Join-Path $RuntimeDirectory 'localization') runtime
	Copy-VeroScriptRuntimeIcons $RepoRoot $RuntimeDirectory $Runtime
	Copy-LicenseFiles $RuntimeDirectory
	Copy-VeroScriptPackageClient $ModuleRoot (Join-Path $RuntimeDirectory 'veroscript.exe') $RuntimeDirectory 'runtime'
	$JitIdentity = Get-VeroScriptJitIdentity $ToolVersion
	Assert-VeroScriptJitBuildIdentity $JitIdentity $HasBackendModule $CoreHasAsmJit
	$RuntimeManifest = [ordered]@{ schemaVersion = 1; product = 'VeroScript Runtime'; version = $Versions.Runtime; platform = 'windows'; architecture = $Architecture; includesExamples = $false; packageManager = $PackageClientEnabled; application = $ApplicationEnabled; jit = $JitIdentity }
	[IO.File]::WriteAllText((Join-Path $RuntimeDirectory 'runtime-manifest.json'), ($RuntimeManifest | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
	Remove-GeneratedCaches $RuntimeDirectory
	if ($ApplicationEnabled) { Copy-VeroScriptApplicationRuntime $ModuleRoot $BinRoot $RuntimeDirectory }
	New-PackageZip $RuntimeDirectory (Join-Path (Split-Path $RuntimeDirectory -Parent) ($RuntimeName + '.zip'))


	# Core SDK exports the core, tooling and optional package-manager SDK.
	# Language integrations are packaged independently below.
	$SdkName = "VeroScriptSDK-$Version-$Runtime"
	$SdkDirectory = Join-Path (Join-Path $ReleaseRoot 'sdk') $SdkName
	New-Item -ItemType Directory -Path $SdkDirectory -Force | Out-Null
	Copy-Directory (Join-Path $RepoRoot "$ModuleRelative\VeroScript\Public") (Join-Path $SdkDirectory 'include\Script')
	Copy-Item -LiteralPath (Join-Path $BuildRoot "VeroScript/Generated/VeroScriptJitBuild.h") -Destination (Join-Path $SdkDirectory "include/Script")
	Copy-Directory (Join-Path $RepoRoot "$ModuleRelative\VeroScriptTooling\Public") (Join-Path $SdkDirectory 'include\Tooling')
	New-Item -ItemType Directory -Path (Join-Path $SdkDirectory 'lib'), (Join-Path $SdkDirectory 'bin'), (Join-Path $SdkDirectory 'assets') -Force | Out-Null
	foreach ($File in ($BackendLibraries + @('VeroScript.lib', 'VeroScriptTooling.lib'))) {
		Copy-Item -LiteralPath (Join-Path $LibRoot $File) -Destination (Join-Path $SdkDirectory 'lib')
	}
	Copy-Item -LiteralPath (Join-Path $BinRoot 'veroscript.exe'), (Join-Path $BinRoot 'veroscript-lsp.exe'), (Join-Path $BinRoot 'veroscript-dap.exe') -Destination (Join-Path $SdkDirectory 'bin')
	Copy-VeroScriptWindowsRuntime $BuildRoot (Join-Path $SdkDirectory 'bin') $Config
	Copy-Directory (Join-Path $ScriptsRoot 'stdlib') (Join-Path $SdkDirectory 'assets\stdlib')
	Require-StandardLibrary (Join-Path $SdkDirectory 'assets\stdlib')
	Copy-VeroScriptLocalization $RepoRoot (Join-Path $SdkDirectory 'assets\localization') runtime
	Copy-LicenseFiles $SdkDirectory
	Copy-VeroScriptPackageManagerSdk $BuildRoot $SdkDirectory $Config $Versions.CoreSDK
	Copy-VeroScriptPackageClient $ModuleRoot (Join-Path $SdkDirectory 'bin/veroscript.exe') $SdkDirectory 'core-sdk'
	Copy-VeroScriptVmSdk $BuildRoot $SdkDirectory $Config
	Write-CoreSdkCMakePackage $SdkDirectory
	$SdkManifest = [ordered]@{ schemaVersion = 1; product = 'VeroScript Core SDK'; version = $Versions.CoreSDK; buildId = $CoreBuildId; platform = 'windows'; architecture = $Architecture; packageManager = $PackageClientEnabled; application = $ApplicationEnabled; packageRuntime = $PackageRuntimeEnabled; buildSystem = $BuildSystemEnabled; jit = $JitIdentity }
	[IO.File]::WriteAllText((Join-Path $SdkDirectory 'sdk-manifest.json'), ($SdkManifest | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
	Remove-GeneratedCaches $SdkDirectory
	if ($ApplicationEnabled) { Copy-VeroScriptApplicationRuntime $ModuleRoot $BinRoot $SdkDirectory 'bin' }
	Require-File (Join-Path $SdkDirectory 'include\Tooling\VeroScriptTooling.h')
	if (Get-ChildItem (Join-Path $SdkDirectory 'lib') -File | Where-Object { $_.Name -match 'Cpp|CSharp|Python|Interop|Web|Aot' }) {
		throw 'The Core SDK contains a language-specific integration library.'
	}
	Test-SdkConsumer $SdkDirectory 'examples\embed-minimal' 'core-sdk'
	Test-SdkConsumer $SdkDirectory 'examples\annotation-processing' 'core-annotation-sdk'
	Test-SdkConsumer $SdkDirectory 'examples\script-compile-extensions' 'core-script-extensions-sdk'
	Test-CoreSdkRuntimeAssets $SdkDirectory
	New-PackageZip $SdkDirectory (Join-Path (Split-Path $SdkDirectory -Parent) ($SdkName + '.zip'))

	# Stable C ABI SDK.
	$InteropName = "VeroScriptInteropSDK-$($Versions.Interop)-$Runtime"
	$InteropDirectory = Join-Path (Join-Path $ReleaseRoot 'interop') $InteropName
	Invoke-Checked 'cmake' @('--install', $BuildRoot, '--config', $Config, '--prefix', $InteropDirectory, '--component', 'VeroScriptInterop')
	Copy-VeroScriptWindowsRuntime $BuildRoot (Join-Path $InteropDirectory 'bin') $Config
	$InteropConfigRoot = Join-Path $InteropDirectory 'lib/cmake/VeroScriptInterop'
	Invoke-Checked 'cmake' @("-DOUTPUT_FILE:FILEPATH=$(Join-Path $InteropConfigRoot 'VeroScriptInteropConfigVersion.cmake')", "-DPACKAGE_SEMANTIC_VERSION:STRING=$($Versions.Interop)", '-DPACKAGE_POINTER_SIZE:STRING=8', '-P', $CMakeVersionWriter)
	$InteropConfigPath = Join-Path $InteropConfigRoot 'VeroScriptInteropConfig.cmake'
	$InteropConfigText = (Get-Content -LiteralPath $InteropConfigPath -Raw) -replace 'set\(VeroScriptInterop_VERSION "[^"]+"\)', ('set(VeroScriptInterop_VERSION "' + $Versions.Interop + '")')
	[IO.File]::WriteAllText($InteropConfigPath, $InteropConfigText, [Text.UTF8Encoding]::new($false))
	Copy-LicenseFiles $InteropDirectory
	ConvertTo-VeroScriptPortableAssetTree $InteropDirectory -SourceRoot (Join-Path $RepoRoot "$ModuleRelative/VeroScriptInterop")
	Test-SdkConsumer $InteropDirectory 'examples\interop-minimal' 'interop-sdk'
	New-PackageZip $InteropDirectory (Join-Path (Split-Path $InteropDirectory -Parent) ($InteropName + '.zip'))

	# WebAssembly SDK: the Wasm already embeds current stdlib and localization.
	$WebAssemblyName = "VeroScriptWebAssemblySDK-$($Versions.WebAssembly)-wasm32"
	$WebAssemblyDirectory = Join-Path (Join-Path $ReleaseRoot 'webassembly') $WebAssemblyName
	New-Item -ItemType Directory -Path $WebAssemblyDirectory -Force | Out-Null
	foreach ($File in @('veroscript-engine.mjs', 'veroscript-engine.wasm', 'veroscript-engine.d.mts', 'veroscript-engine-fast.mjs', 'veroscript-engine-fast.wasm', 'veroscript-engine-fast.d.mts', 'veroscript-webassembly.ts', 'veroscript-webassembly.d.ts')) { Copy-Item -LiteralPath (Join-Path $WebAssemblyBinRoot $File) -Destination $WebAssemblyDirectory -Force }
	Copy-Item -LiteralPath (Join-Path $RepoRoot 'LICENSE') -Destination $WebAssemblyDirectory -Force
	& $WebAssemblyVerifyScript -ArtifactDirectory $WebAssemblyDirectory -Configuration $Config
	if ($LASTEXITCODE -ne 0) {
		throw "Staged WebAssembly SDK verification failed with exit code $LASTEXITCODE."
	}
	New-PackageZip $WebAssemblyDirectory (Join-Path (Split-Path $WebAssemblyDirectory -Parent) ($WebAssemblyName + '.zip'))

	# Language integrations are independent products. Runtime integration is
	# required; tools, AOT, examples and docs are separately selectable modules.
	$LanguageModules = [ordered]@{}
	$LanguageDisplayNames = [ordered]@{ cpp = 'Cpp'; csharp = 'CSharp'; python = 'Python' }

	$CppRuntime = Join-Path $WorkRoot 'cpp-runtime'
	$CppTools = Join-Path $WorkRoot 'cpp-tools'
	$CppAot = Join-Path $WorkRoot 'cpp-aot'
	Copy-Directory (Join-Path $RepoRoot "$ModuleRelative\Extensions\Language\Cpp\VeroScriptCpp\Public") (Join-Path $CppRuntime 'include\Cpp')
	New-Item -ItemType Directory -Path (Join-Path $CppRuntime 'lib') -Force | Out-Null
	Copy-Item -LiteralPath (Join-Path $LibRoot 'VeroScriptCpp.lib') -Destination (Join-Path $CppRuntime 'lib') -Force
	Copy-Item -LiteralPath (Join-Path $RepoRoot 'LICENSE') -Destination (Join-Path $CppRuntime 'LICENSE') -Force
	Write-CppSdkCMakePackage $CppRuntime
	Copy-VeroScriptLocalization $RepoRoot (Join-Path $CppRuntime 'localization') cpp
	foreach ($Pair in @(
		@("$ModuleRelative\Extensions\Language\Cpp\VeroScriptCppFrontend\Public", 'include\CppFrontend'),
		@("$ModuleRelative\Extensions\Language\Cpp\VeroScriptCppBindGen\Public", 'include\CppBindGen'),
		@("$ModuleRelative\Extensions\Language\Cpp\VeroScriptCppTranspiler\Public", 'include\CppTranspiler'))) {
		Copy-Directory (Join-Path $RepoRoot $Pair[0]) (Join-Path $CppTools $Pair[1])
	}
	New-Item -ItemType Directory -Path (Join-Path $CppTools 'lib'), (Join-Path $CppTools 'bin') -Force | Out-Null
	Copy-Item -LiteralPath (Join-Path $LibRoot 'VeroScriptCppFrontend.lib'), (Join-Path $LibRoot 'VeroScriptCppBindGenLib.lib'), (Join-Path $LibRoot 'VeroScriptCppTranspilerLib.lib') -Destination (Join-Path $CppTools 'lib') -Force
	Copy-Item -LiteralPath (Join-Path $BinRoot 'VeroScriptCppBindGen.exe'), (Join-Path $BinRoot 'VeroScriptCppTranspiler.exe') -Destination (Join-Path $CppTools 'bin') -Force
	Copy-VeroScriptWindowsRuntime $BuildRoot (Join-Path $CppTools 'bin') $Config
	Copy-Directory (Join-Path $RepoRoot "$ModuleRelative\Extensions\Language\Cpp\VeroScriptCppAot\Public") (Join-Path $CppAot 'include\CppAot')
	New-Item -ItemType Directory -Path (Join-Path $CppAot 'lib') -Force | Out-Null
	Copy-Item -LiteralPath (Join-Path $LibRoot 'VeroScriptCppAot.lib') -Destination (Join-Path $CppAot 'lib') -Force
	$CppSymbols = Join-Path $WorkRoot 'cpp-symbols'
	New-Item -ItemType Directory -Path $CppSymbols -Force | Out-Null
	foreach ($File in @('VeroScriptCpp.pdb', 'VeroScriptCppFrontend.pdb', 'VeroScriptCppBindGenLib.pdb', 'VeroScriptCppTranspilerLib.pdb', 'VeroScriptCppAot.pdb')) { Copy-Item -LiteralPath (Join-Path $LibRoot $File) -Destination $CppSymbols -Force }
	foreach ($File in @('VeroScriptCppBindGen.pdb', 'VeroScriptCppTranspiler.pdb')) { Copy-Item -LiteralPath (Join-Path $BinRoot $File) -Destination $CppSymbols -Force }
	$LanguageModules.cpp = [ordered]@{ Runtime = $CppRuntime; Tools = $CppTools; Aot = $CppAot; Symbols = $CppSymbols }

	$CSharpRuntime = Join-Path $WorkRoot 'csharp-runtime'
	$CSharpTools = Join-Path $WorkRoot 'csharp-tools'
	$CSharpAot = Join-Path $WorkRoot 'csharp-aot'
	Invoke-Checked 'cmake' @('--install', $BuildRoot, '--config', $Config, '--prefix', $CSharpRuntime, '--component', 'VeroScriptCSharp')
	Invoke-Checked 'cmake' @('--install', $BuildRoot, '--config', $Config, '--prefix', $CSharpTools, '--component', 'VeroScriptCSharpTools')
	Invoke-Checked 'cmake' @('--install', $BuildRoot, '--config', $Config, '--prefix', $CSharpAot, '--component', 'VeroScriptCSharpAot')
	Invoke-Checked 'python' @((Join-Path $PSScriptRoot 'veroscript_csharp_packages.py'), '--build', $BuildRoot,
		'--runtime', $CSharpRuntime, '--tools', $CSharpTools, '--output', (Join-Path $ReleaseRoot 'distributions/csharp'),
		'--version', $Versions.CSharp)
	foreach ($Extension in @('nupkg', 'snupkg')) {
		Require-File (Join-Path $CSharpRuntime "share\VeroScript\nuget\VeroScript.CSharp.$($Versions.CSharp).$Extension")
		foreach ($PackageId in @(
				'VeroScript.CSharpFrontend',
				'VeroScript.CSharpBindGen',
				'VeroScript.CSharpBindGen.Tool',
				'VeroScript.CSharpTranspiler',
				'VeroScript.CSharpTranspiler.Tool')) {
			Require-File (Join-Path $CSharpTools "share\VeroScript\nuget\$PackageId.$($Versions.CSharp).$Extension")
		}
	}
	foreach ($TargetFramework in @('net8.0', 'net9.0')) {
		$ManagedRuntimeDirectory = Join-Path $CSharpRuntime "lib\dotnet\$TargetFramework"
		Require-Directory $ManagedRuntimeDirectory
		Copy-Item -LiteralPath (Join-Path $BinRoot 'VeroScriptInterop.dll') -Destination $ManagedRuntimeDirectory -Force
		Copy-VeroScriptWindowsRuntime $BuildRoot $ManagedRuntimeDirectory $Config
		Copy-VeroScriptLocalization $RepoRoot (Join-Path $ManagedRuntimeDirectory 'localization') interop
	}
	Get-ChildItem -LiteralPath $CSharpTools -Recurse -Filter 'VeroScriptCSharpBindGen.Cli.runtimeconfig.json' |
		ForEach-Object {
			Copy-Item -LiteralPath (Join-Path $BinRoot 'VeroScriptInterop.dll') -Destination $_.Directory.FullName -Force
			Copy-VeroScriptWindowsRuntime $BuildRoot $_.Directory.FullName $Config
		}
	Copy-Item -LiteralPath (Join-Path $RepoRoot 'LICENSE') -Destination (Join-Path $CSharpRuntime 'LICENSE') -Force
	$CSharpSymbols = Join-Path $WorkRoot 'csharp-symbols'
	New-Item -ItemType Directory -Path (Join-Path $CSharpSymbols 'native'), (Join-Path $CSharpSymbols 'net8.0'), (Join-Path $CSharpSymbols 'net9.0') -Force | Out-Null
	Copy-Item -LiteralPath (Join-Path $LibRoot 'VeroScriptCSharpAot.pdb'), (Join-Path $BinRoot 'VeroScriptInterop.pdb') -Destination (Join-Path $CSharpSymbols 'native') -Force
	foreach ($TargetFramework in @('net8.0', 'net9.0')) {
		$ManagedSymbols = Join-Path $CSharpSymbols $TargetFramework
		foreach ($RelativePdb in @(
				"VeroScriptCSharp\bin\$Config\$TargetFramework\VeroScriptCSharp.pdb",
				"VeroScriptCSharpFrontend\bin\$Config\$TargetFramework\VeroScriptCSharpFrontend.pdb",
				"VeroScriptCSharpBindGen\bin\$Config\$TargetFramework\VeroScriptCSharpBindGen.pdb",
				"VeroScriptCSharpBindGen\Cli\bin\$Config\$TargetFramework\VeroScriptCSharpBindGen.Cli.pdb",
				"VeroScriptCSharpTranspiler\bin\$Config\$TargetFramework\VeroScriptCSharpTranspiler.pdb",
				"VeroScriptCSharpTranspiler\Cli\bin\$Config\$TargetFramework\VeroScriptCSharpTranspiler.Cli.pdb")) {
			Copy-Item -LiteralPath (Join-Path $RepoRoot "$ModuleRelative\Extensions\Language\CSharp\$RelativePdb") -Destination $ManagedSymbols -Force
		}
	}
	$LanguageModules.csharp = [ordered]@{ Runtime = $CSharpRuntime; Tools = $CSharpTools; Aot = $CSharpAot; Symbols = $CSharpSymbols }

	$PythonRuntime = Join-Path $WorkRoot 'python-runtime'
	$PythonTools = Join-Path $WorkRoot 'python-tools'
	$PythonAot = Join-Path $WorkRoot 'python-aot'
	Invoke-Checked 'cmake' @('--install', $BuildRoot, '--config', $Config, '--prefix', $PythonRuntime, '--component', 'VeroScriptPython')
	Invoke-Checked 'cmake' @('--install', $BuildRoot, '--config', $Config, '--prefix', $PythonTools, '--component', 'VeroScriptPythonTools')
	Copy-Directory (Join-Path $RepoRoot "$ModuleRelative\Extensions\Language\Python\VeroScriptPythonAot\Public") (Join-Path $PythonAot 'include\PythonAot')
	New-Item -ItemType Directory -Path (Join-Path $PythonAot 'lib') -Force | Out-Null
	Copy-Item -LiteralPath (Join-Path $LibRoot 'VeroScriptPythonAot.lib') -Destination (Join-Path $PythonAot 'lib') -Force
	Copy-Item -LiteralPath (Join-Path $RepoRoot 'LICENSE') -Destination (Join-Path $PythonRuntime 'LICENSE') -Force
	$PythonSymbols = Join-Path $WorkRoot 'python-symbols'
	New-Item -ItemType Directory -Path $PythonSymbols -Force | Out-Null
	Copy-Item -LiteralPath (Join-Path $LibRoot 'VeroScriptPythonAot.pdb') -Destination $PythonSymbols -Force
	$LanguageModules.python = [ordered]@{ Runtime = $PythonRuntime; Tools = $PythonTools; Aot = $PythonAot; Symbols = $PythonSymbols }

	foreach ($Language in $LanguageModules.Keys) {
		$LanguageVersion = $Versions.($LanguageDisplayNames[$Language])
		foreach ($ModuleName in $LanguageModules[$Language].Keys) {
			$Module = $LanguageModules[$Language][$ModuleName]
			Require-Directory $Module
			# CMake/package-tool install steps may copy interpreter caches from
			# source examples. Clean module roots before they feed either the
			# portable ZIP or an embedded installer payload.
			Remove-PythonCaches $Module
			# Some managed publish/install steps emit PDB files beside the tools.
			# Symbols are a separately selectable module, so keeping those copies
			# in Runtime/Tools/Aot would duplicate payload and blur module roles.
			if ($ModuleName -ne 'Symbols') {
				Get-ChildItem -LiteralPath $Module -Recurse -File -Filter '*.pdb' -ErrorAction SilentlyContinue |
					Remove-Item -Force
			}
		}
		$LanguageName = "VeroScript$($LanguageDisplayNames[$Language])-$LanguageVersion-$Runtime"
		$LanguagePortable = Join-Path (Join-Path $ReleaseRoot 'languages') $LanguageName
		foreach ($ModuleName in $LanguageModules[$Language].Keys) {
			Copy-Directory $LanguageModules[$Language][$ModuleName] (Join-Path $LanguagePortable $ModuleName)
		}
		Copy-LicenseFiles $LanguagePortable
		if ($Language -eq 'cpp') {
			Require-File (Join-Path $LanguagePortable 'Runtime\lib\cmake\VeroScriptCpp\VeroScriptCppConfig.cmake')
			Test-CppSdkConsumer $SdkDirectory $LanguagePortable
		}
		elseif ($Language -eq 'csharp') {
			Invoke-Checked 'python' @((Join-Path $PSScriptRoot 'veroscript_csharp_consumer.py'),
				'--runtime', (Join-Path $LanguagePortable 'Runtime'), '--architecture', $Architecture,
				'--work', (Join-Path $WorkRoot 'csharp-sdk-consumer'))
		}
		Remove-PythonCaches $LanguagePortable
		New-PackageZip $LanguagePortable (Join-Path (Split-Path $LanguagePortable -Parent) ($LanguageName + '.zip'))
	}

	# Publish integrated documentation through Resources, outside functional packages.
	$InstallerDocs = Join-Path $WorkRoot 'installer-docs'
	Invoke-VeroScriptDocs @('publish', '--audience', 'User', '--output', $InstallerDocs)

	# Matching native symbols, offered only by the SDK installer / dedicated ZIP.
	$SymbolsName = "VeroScriptSymbols-$Version-$Runtime"
	$SymbolsDirectory = Join-Path (Join-Path $ReleaseRoot 'symbols') $SymbolsName
	New-Item -ItemType Directory -Path (Join-Path $SymbolsDirectory 'bin'), (Join-Path $SymbolsDirectory 'lib') -Force | Out-Null
	foreach ($File in @('veroscript.pdb', 'veroscript-lsp.pdb', 'veroscript-dap.pdb')) { Copy-Item -LiteralPath (Join-Path $BinRoot $File) -Destination (Join-Path $SymbolsDirectory 'bin') -Force }
	foreach ($File in @('VeroScript.pdb', 'VeroScriptTooling.pdb')) { Copy-Item -LiteralPath (Join-Path $LibRoot $File) -Destination (Join-Path $SymbolsDirectory 'lib') -Force }
	Copy-Item -LiteralPath (Join-Path $RepoRoot 'LICENSE') -Destination (Join-Path $SymbolsDirectory 'LICENSE') -Force
	New-PackageZip $SymbolsDirectory (Join-Path (Split-Path $SymbolsDirectory -Parent) ($SymbolsName + '.zip'))

	# Each product gets a small single-file installer backed by the same generic
	# engine. Optional modules remain independent payload components.
	$RuntimeBundle = Join-Path $WorkRoot 'runtime-installer-bundle'
	Invoke-VeroScriptInstallerDotNet $RepoRoot @('run', '--project', (Join-Path $RepoRoot "$ModuleRelative\VeroScriptInstaller\Tool\VeroScriptInstaller.Tool.csproj"), '-c', 'Release', '--', 'build-bundle', '--output', $RuntimeBundle, '--version', $Versions.Runtime, '--profile', 'runtime', '--platform', 'windows', '--architecture', $Architecture, '--source-runtime', $RuntimeDirectory)
	Invoke-VeroScriptIcons @('verify-bundle', '--repo-root', $RepoRoot, '--root', $RuntimeBundle, '--target', $Runtime)
	Publish-EmbeddedInstaller $RuntimeBundle "VeroScriptRuntimeInstaller-$($Versions.Runtime)-$Runtime"


	$SdkBundle = Join-Path $WorkRoot 'sdk-installer-bundle'
	Invoke-VeroScriptInstallerDotNet $RepoRoot @('run', '--project', (Join-Path $RepoRoot "$ModuleRelative\VeroScriptInstaller\Tool\VeroScriptInstaller.Tool.csproj"), '-c', 'Release', '--', 'build-bundle', '--output', $SdkBundle, '--version', $Versions.CoreSDK, '--profile', 'core-sdk', '--platform', 'windows', '--architecture', $Architecture, '--source-sdk', $SdkDirectory, '--source-symbols', $SymbolsDirectory)
	Publish-EmbeddedInstaller $SdkBundle "VeroScriptCoreSDKInstaller-$Version-$Runtime"

	$InteropBundle = Join-Path $WorkRoot 'interop-installer-bundle'
	$InteropInstallerSymbols = Join-Path $WorkRoot 'interop-installer-symbols'
	New-Item -ItemType Directory -Path $InteropInstallerSymbols -Force | Out-Null
	Copy-Item -LiteralPath (Join-Path $BinRoot 'VeroScriptInterop.pdb') -Destination $InteropInstallerSymbols -Force
	Invoke-VeroScriptInstallerDotNet $RepoRoot @('run', '--project', (Join-Path $RepoRoot "$ModuleRelative\VeroScriptInstaller\Tool\VeroScriptInstaller.Tool.csproj"), '-c', 'Release', '--', 'build-bundle', '--output', $InteropBundle, '--version', $Versions.Interop, '--profile', 'interop', '--platform', 'windows', '--architecture', $Architecture, '--source-interop-sdk', $InteropDirectory, '--source-symbols', $InteropInstallerSymbols)
	Publish-EmbeddedInstaller $InteropBundle "VeroScriptInteropInstaller-$($Versions.Interop)-$Runtime"

	foreach ($Language in @('cpp', 'csharp', 'python')) {
		$LanguageVersion = $Versions.($LanguageDisplayNames[$Language])
		$LanguageBundle = Join-Path $WorkRoot ($Language + '-installer-bundle')
		$Modules = $LanguageModules[$Language]
		Invoke-VeroScriptInstallerDotNet $RepoRoot @('run', '--project', (Join-Path $RepoRoot "$ModuleRelative\VeroScriptInstaller\Tool\VeroScriptInstaller.Tool.csproj"), '-c', 'Release', '--', 'build-bundle', '--output', $LanguageBundle, '--version', $LanguageVersion, '--profile', $Language, '--platform', 'windows', '--architecture', $Architecture, "--source-$Language-runtime", $Modules.Runtime, "--source-$Language-tools", $Modules.Tools, "--source-$Language-aot", $Modules.Aot, '--source-symbols', $Modules.Symbols)
		Publish-EmbeddedInstaller $LanguageBundle "VeroScript$($LanguageDisplayNames[$Language])Installer-$LanguageVersion-$Runtime"
	}

	$WebAssemblyBundle = Join-Path $WorkRoot 'webassembly-installer-bundle'
	Invoke-VeroScriptInstallerDotNet $RepoRoot @('run', '--project', (Join-Path $RepoRoot "$ModuleRelative\VeroScriptInstaller\Tool\VeroScriptInstaller.Tool.csproj"), '-c', 'Release', '--', 'build-bundle', '--output', $WebAssemblyBundle, '--version', $Versions.WebAssembly, '--profile', 'web', '--platform', 'windows', '--architecture', $Architecture, '--source-web-sdk', $WebAssemblyDirectory)
	Publish-EmbeddedInstaller $WebAssemblyBundle "VeroScriptWebAssemblyInstaller-$($Versions.WebAssembly)-$Runtime"

	$VmStageRoot = Join-Path $WorkRoot 'vm-stage'
	Invoke-Checked 'python' @((Join-Path $PSScriptRoot 'package-veroscript-vm.py'), '--repo-root', $RepoRoot,
		'--build-directory', $BuildRoot, '--target', $Runtime, '--version', $Versions.Vm, '--out-root', $VmStageRoot)
	$VmPackageName = "vs-bytecode-vm-$($Versions.Vm)-$Runtime"
	$VmReleaseRoot = Join-Path $ReleaseRoot 'vm'
	New-Item -ItemType Directory -Path $VmReleaseRoot -Force | Out-Null
	Copy-Item -LiteralPath (Join-Path $VmStageRoot ($VmPackageName + '.zip')) -Destination $VmReleaseRoot
	Copy-Item -LiteralPath (Join-Path $VmStageRoot ($VmPackageName + '.vspkg')) -Destination $VmReleaseRoot
	$JitStageRoot = Join-Path $WorkRoot 'jit-stage'
	Invoke-Checked 'python' @((Join-Path $PSScriptRoot 'package-veroscript-jit.py'), '--repo-root', $RepoRoot, '--build-directory', $BuildRoot, '--target', $Runtime, '--version', $Versions.Jit, '--out-root', $JitStageRoot)
	$JitPackageName = "vs-asmjit-$($Versions.Jit)-$Runtime"
	$JitPackageDirectory = Join-Path $JitStageRoot $JitPackageName
	Require-Directory $JitPackageDirectory
	$JitReleaseRoot = Join-Path $ReleaseRoot 'jit'
	New-Item -ItemType Directory -Path $JitReleaseRoot -Force | Out-Null
	Copy-Item -LiteralPath (Join-Path $JitStageRoot ($JitPackageName + '.zip')) -Destination $JitReleaseRoot
	Copy-Item -LiteralPath (Join-Path $JitStageRoot ($JitPackageName + '.vspkg')) -Destination $JitReleaseRoot
	$FeatureVersionsFile = Join-Path $WorkRoot 'feature-versions.json'
	[IO.File]::WriteAllText($FeatureVersionsFile, ($ReleaseConfiguration | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
	$FeatureRelease = Join-Path $WorkRoot 'feature-release'
	Invoke-Checked 'python' @((Join-Path $PSScriptRoot 'package-veroscript-modules.py'), '--repo-root', $RepoRoot,
		'--build-directory', $BuildRoot, '--versions-file', $FeatureVersionsFile, '--candidate-directory', $FeatureRelease,
		'--business', 'Json', '--business', 'Network', '--business', 'Http', '--business', 'WebSocket', '--business', 'Boost', '--business', 'Curl',
		'--business', 'Platform', '--business', 'Graphics', '--business', 'Audio', '--business', 'Sdl3',
		'--business', 'Image', '--business', 'Font', '--business', 'Stb', '--business', 'FontBackend', '--business', 'Gui', '--business', 'RmlUi',
		'--core-sdk', $SdkDirectory, '--cpp-sdk', $CppRuntime, '--target', $Runtime, '--skip-build')
	$FeatureBuildIds = @{}
	$FeatureManifest = Get-Content -LiteralPath (Join-Path $FeatureRelease 'release-manifest.json') -Raw | ConvertFrom-Json
	foreach ($Business in $FeatureManifest.businessBuildIds.PSObject.Properties) {
		$FeatureBuildIds[$Business.Name] = $Business.Value
	}
	foreach ($Category in @('modules', 'packages')) {
		$FeatureSource = Join-Path $FeatureRelease $Category
		if (Test-Path -LiteralPath $FeatureSource -PathType Container) {
			$FeatureOutput = Join-Path $ReleaseRoot $Category
			New-Item -ItemType Directory -Path $FeatureOutput -Force | Out-Null
			foreach ($Archive in Get-ChildItem -LiteralPath $FeatureSource -File) {
				Copy-Item -LiteralPath $Archive.FullName -Destination $FeatureOutput
			}
		}
	}

	# Publish resources from their current authoritative sources.
	$ReleaseResourcesRoot = Join-Path $ReleaseRoot 'Resources'
	$SharedAssetsRoot = Join-Path $RepoRoot "$ModuleRelative\Resources\Assets"
	$ReleaseAssetsRoot = Join-Path $ReleaseResourcesRoot 'Assets'
	Copy-Directory $SharedAssetsRoot $ReleaseAssetsRoot
	$ReleaseScriptsRoot = Join-Path $ReleaseResourcesRoot 'Scripts'
	Copy-Directory (Join-Path $ScriptsRoot 'stdlib') (Join-Path $ReleaseScriptsRoot 'stdlib')
	Copy-Directory (Join-Path $ScriptsRoot 'examples') (Join-Path $ReleaseScriptsRoot 'examples')
	Copy-Directory (Join-Path $RepoRoot "$ModuleRelative/Modules/Multimedia/VeroScriptGui/Resources/Examples") (Join-Path $ReleaseResourcesRoot 'Examples/Gui')
	ConvertTo-VeroScriptPortableAssetTree $ReleaseScriptsRoot -SourceRoot $ScriptsRoot
	Copy-Directory $InstallerDocs (Join-Path $ReleaseResourcesRoot 'Docs')
	Remove-GeneratedCaches $ReleaseResourcesRoot -PreserveXml
	Require-StandardLibrary (Join-Path $ReleaseScriptsRoot 'stdlib')

	if ($IdeBusinesses.Count -gt 0) {
		$IdeOutput = Join-Path $WorkRoot 'ide-packages'
		$IdeArguments = @((Join-Path $PSScriptRoot 'package-veroscript-ide.py'), '--repo-root', $RepoRoot,
			'--versions-file', $IdeVersionsFile, '--out-root', $IdeOutput, '--runtime-directory', $RuntimeDirectory,
			'--jit-backend', (Join-Path $JitPackageDirectory 'VeroScriptJitAsmJit.dll'), '--target', $Runtime)
		foreach ($Business in $IdeBusinesses) { $IdeArguments += @('--business', $Business) }
		Invoke-Checked 'python' $IdeArguments
		Copy-Directory (Join-Path $IdeOutput 'ide') (Join-Path $ReleaseRoot 'ide')
		$IdeManifest = Get-Content -LiteralPath (Join-Path $IdeOutput 'release-manifest.json') -Raw | ConvertFrom-Json
		$LoadedIdeBuildIds = $IdeManifest.businessBuildIds
		foreach ($Property in $LoadedIdeBuildIds.PSObject.Properties) { $IdeBuildIds[$Property.Name] = $Property.Value }
		if ($IdeManifest.ideDependencies) { $IdeDependencies = $IdeManifest.ideDependencies }
	}

	# The expanded package trees are inputs for ZIP creation and embedded
	# installers, not public release artifacts. Keep them in the staging area
	# only so dist contains archives, installers, Resources and integrity metadata.
	foreach ($Zip in @(Get-ChildItem -LiteralPath $ReleaseRoot -Recurse -File -Filter '*.zip')) {
		$ExpandedPackage = Join-Path $Zip.DirectoryName $Zip.BaseName
		if (Test-Path -LiteralPath $ExpandedPackage -PathType Container) {
			Remove-Item -LiteralPath $ExpandedPackage -Recurse -Force
		}
	}

	if ('Python' -in $Businesses) {
		$WheelArguments = @('--repo-root', $RepoRoot, '--target', $Runtime, '--version', $Versions.Python,
			'--output', (Join-Path $ReleaseRoot 'distributions/python'),
			'--work-directory', (Join-Path $WorkRoot 'python-wheels'),
			'--native-executable', (Join-Path $BinRoot 'veroscript.exe'))
		$GeneratorLine = Get-Content -LiteralPath $CMakeCachePath | Where-Object { $_ -match '^CMAKE_GENERATOR:INTERNAL=' } | Select-Object -First 1
		$WheelArguments += @('--cmake-generator', ($GeneratorLine -split '=', 2)[1])
		foreach ($Executable in $PythonExecutables) { $WheelArguments += @('--python', $Executable) }
		Invoke-VeroScriptPythonWheels $WheelArguments
	}
	$ModuleSymbolStatusFile = Join-Path $WorkRoot 'module-symbol-status.json'
	Invoke-Checked 'python' @((Join-Path $PSScriptRoot 'veroscript_module_symbols.py'), '--repo-root', $RepoRoot,
		'--build-directory', $BuildRoot, '--candidate', $ReleaseRoot, '--target', $Runtime,
		'--status-output', $ModuleSymbolStatusFile)
	$ModuleSymbolStatus = Get-Content -LiteralPath $ModuleSymbolStatusFile -Raw -Encoding UTF8 | ConvertFrom-Json
	# Checksums and machine-readable release manifest cover distributable files only.
	foreach ($Installer in @(Get-ChildItem -LiteralPath (Join-Path $ReleaseRoot 'installer') -File -Filter '*.exe')) {
		& $InstallerSigningScript -Path $Installer.FullName `
			-CertificateThumbprint $SigningCertificateThumbprint `
			-TimestampServer $TimestampServer `
			-RequireSignature:$RequireSignedInstallers
		foreach ($VerificationArgument in @('--verify-localization', '--verify-embedded-bundle')) {
			$Verification = Start-Process -FilePath $Installer.FullName `
				-ArgumentList $VerificationArgument -WindowStyle Hidden -Wait -PassThru
			if ($Verification.ExitCode -ne 0) {
				throw "Signed installer verification '$VerificationArgument' failed for '$($Installer.FullName)' ($($Verification.ExitCode))."
			}
		}
	}
	$ReleaseResourcesPrefix = $ReleaseResourcesRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
	$Artifacts = @()
	foreach ($File in Get-ChildItem -LiteralPath $ReleaseRoot -Recurse -File | Where-Object {
		$_.Extension -in @('.zip', '.vsix', '.vspkg') -or
		$_.FullName.StartsWith(((Join-Path $ReleaseRoot 'distributions') + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase) -or
		($_.Extension -eq '.exe' -and $_.DirectoryName -eq (Join-Path $ReleaseRoot 'installer')) -or
		$_.FullName.StartsWith($ReleaseResourcesPrefix, [StringComparison]::OrdinalIgnoreCase) -or
		$_.FullName.StartsWith(($ReleaseAssetsRoot + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase)
	} | Sort-Object FullName) {
		$Relative = $File.FullName.Substring($ReleaseRoot.Length + 1).Replace('\', '/')
		$Hash = (Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
		$Artifacts += [ordered]@{ path = $Relative; size = $File.Length; sha256 = $Hash }
	}
	foreach ($Business in $FeatureBuildIds.Keys) { $IdeBuildIds[$Business] = $FeatureBuildIds[$Business] }
	$StagedNativeBusinesses = @($ReleaseConfiguration.nativeBusinesses)
	$Manifest = [ordered]@{ schemaVersion = 2; product = 'VeroScript Release Candidate'; version = $Versions.CoreSDK; businessVersions = $Versions; nativeVersion = $NativeVersion; selectedBusinesses = $StagedNativeBusinesses + $IdeBusinesses; buildId = $CoreBuildId; businessBuildIds = $IdeBuildIds; platform = 'windows'; architecture = $Architecture; target = $Runtime; jit = $JitIdentity; artifacts = $Artifacts }
	$Manifest.functionalPayloadBusinesses = @($Manifest.selectedBusinesses)
	$Manifest.moduleSymbolStatus = $ModuleSymbolStatus
	if ($null -ne $IdeDependencies) { $Manifest.ideDependencies = $IdeDependencies }
	$Manifest.installableModuleBusinesses = @('Jit', 'Vm') + @($FeatureManifest.installableModuleBusinesses)
	if ('Python' -notin $Businesses) { $Manifest.partialBusinesses = @('Python') }
	[IO.File]::WriteAllText((Join-Path $ReleaseRoot 'release-manifest.json'), ($Manifest | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
	$ChecksumLines = $Artifacts | ForEach-Object { "$($_.sha256)  $($_.path)" }
	[IO.File]::WriteAllLines((Join-Path $ReleaseRoot 'checksums.txt'), $ChecksumLines, [Text.UTF8Encoding]::new($false))

	& (Join-Path $PSScriptRoot 'verify-veroscript-release.ps1') `
		-ReleaseDirectory $ReleaseRoot -Target $Runtime `
		-RequireSignedInstallers:$RequireSignedInstallers

	$PublicationArguments = @('publish', '--repo-root', $RepoRoot, '--candidate', $ReleaseRoot, '--release-directory', $OutputRoot)
	foreach ($Business in $Businesses) {
		$PublicationArguments += @('--business', $Business)
	}
	Invoke-VeroScriptBusinessRelease $PublicationArguments

	$PublicationSucceeded = $true
	Write-Host "VeroScript business releases: $OutputRoot" -ForegroundColor Green
}
finally
{
	if (Test-Path -LiteralPath $StageRoot) {
		if ($KeepFailedStage -and -not $PublicationSucceeded) {
			Write-Warning "Failed staging retained for diagnostics: $StageRoot"
		}
		else
		{
			Remove-Item -LiteralPath $StageRoot -Recurse -Force -ErrorAction SilentlyContinue
		}
	}
}
