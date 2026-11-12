#Requires -Version 5.1
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

<#
.SYNOPSIS
  Build a self-contained, localized single-file Windows Runtime installer.

.DESCRIPTION
  The checksummed manifest and component payloads are embedded into the installer
  executable. The runtime includes CLI, LSP, DAP and the standard library. Adding
  the runtime to the user PATH remains an explicit installer checkbox.

.EXAMPLE
  .\Resources\Tools\Packaging\package-veroscript-installer.ps1 -Runtime win-x64
#>
[CmdletBinding()]
param(
	[ValidatePattern('^(win|windows)-(x64|x86|arm64)$')]
	[string]$Runtime = "win-x64",
	[ValidatePattern('^\d+\.\d+\.\d+$')]
	[string]$Version = '',
	[string]$RuntimePackageDir = "",
	[string]$OutRoot = '',
	[string]$SigningCertificateThumbprint = $env:VEROSCRIPT_SIGNING_CERTIFICATE_THUMBPRINT,
	[string]$TimestampServer = $env:VEROSCRIPT_TIMESTAMP_SERVER,
	[switch]$RequireSignature
)

$ErrorActionPreference = "Stop"
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
	throw 'This entry requires Windows. Use package-veroscript-installer.sh on Linux and macOS.'
}
$Runtime = $Runtime.ToLowerInvariant() -replace '^windows-', 'win-'
$Architecture = $Runtime.Substring(4)
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$ModuleRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$ModuleRelative = if ($ModuleRoot -eq $RepoRoot) { '.' }
else
{
	$ModuleRoot.Substring($RepoRoot.TrimEnd('\', '/').Length + 1)
}
. (Join-Path $PSScriptRoot 'veroscript-output.ps1')
$ReleaseConfiguration = Get-VeroScriptReleaseConfiguration $RepoRoot
$ReleaseBusiness = 'Runtime'
if (-not $ReleaseConfiguration.businesses.PSObject.Properties[$ReleaseBusiness]) {
	throw "Unknown release business: $ReleaseBusiness"
}
if (-not $Version) {
	$Version = $ReleaseConfiguration.businesses.$ReleaseBusiness
}
if (Invoke-VeroScriptDefaultBusinessPackage $PSCommandPath $PSBoundParameters $RepoRoot $ReleaseBusiness $Version $Runtime) {
	return
}

. (Join-Path $PSScriptRoot 'veroscript-assets.ps1')
if (-not [string]::IsNullOrWhiteSpace($OutRoot)) { Assert-VeroScriptComponentOutputPath $OutRoot $RepoRoot }
$OutRootPath = if ([IO.Path]::IsPathRooted($OutRoot)) {
	[IO.Path]::GetFullPath($OutRoot)
}
else
{
	[IO.Path]::GetFullPath((Join-Path $RepoRoot $OutRoot))
}

Assert-VeroScriptComponentOutputPath $OutRootPath $RepoRoot

if (-not $RuntimePackageDir)
{
	$RuntimePackageDir = Join-Path (Split-Path $OutRootPath -Parent) "runtime\VeroScriptRuntime-$Version-$Runtime"
}

if (-not (Test-Path -LiteralPath $RuntimePackageDir -PathType Container))
{
	throw "Runtime package directory not found: $RuntimePackageDir"
}
$ExpectedHostTag = "[$Runtime;"
foreach ($ToolPath in @(Join-Path $RuntimePackageDir 'veroscript.exe'))
{
	if (-not $ToolPath) { continue }
	if (-not (Test-Path -LiteralPath $ToolPath -PathType Leaf))
	{
		throw "VeroScript CLI not found in component package: $ToolPath"
	}
	try
	{
		$ToolVersion = (& $ToolPath --version 2>&1 | Out-String).Trim()
	}
	catch
	{
		throw "Cannot execute '$ToolPath' to validate its target architecture: $_"
	}
	if ($LASTEXITCODE -ne 0 -or -not $ToolVersion.Contains($ExpectedHostTag))
	{
		throw "Component CLI target mismatch. Expected '$ExpectedHostTag' in: $ToolVersion"
	}
	if (-not $ToolVersion.StartsWith("VeroScript $($ReleaseConfiguration.nativeVersion) ", [StringComparison]::Ordinal))
	{
		throw "Component CLI version mismatch. Expected native '$($ReleaseConfiguration.nativeVersion)' in: $ToolVersion"
	}
}

$InstallerProject = Join-Path $RepoRoot "$ModuleRelative\VeroScriptInstaller\App\VeroScriptInstaller.csproj"
$ToolProject = Join-Path $RepoRoot "$ModuleRelative\VeroScriptInstaller\Tool\VeroScriptInstaller.Tool.csproj"
$SigningScript = Join-Path $RepoRoot "$ModuleRelative\Resources\Tools\Packaging\sign-veroscript-installer.ps1"
$InstallerFileName = "VeroScriptRuntimeInstaller-$Version-$Runtime.exe"
$InstallerPath = Join-Path $OutRootPath $InstallerFileName
$PublishDirectory = Join-Path ([IO.Path]::GetTempPath()) ("veroscript-installer-publish-" + [guid]::NewGuid().ToString("N"))
$BundleDirectory = Join-Path ([IO.Path]::GetTempPath()) ("veroscript-installer-bundle-" + [guid]::NewGuid().ToString("N"))
$EmbeddedArchive = Join-Path ([IO.Path]::GetTempPath()) ("veroscript-installer-bundle-" + [guid]::NewGuid().ToString("N") + ".zip")
$InstallerStage = $null

try
{
	Invoke-VeroScriptIcons @('verify-runtime', '--repo-root', $RepoRoot, '--root', $RuntimePackageDir, '--target', $Runtime)
	New-Item -ItemType Directory -Path $OutRootPath -Force | Out-Null
	$ToolArguments = @(
		"run", "--project", $ToolProject, "-c", "Release", "--",
		"build-bundle",
		"--output", $BundleDirectory,
		"--version", $Version,
		"--profile", "runtime",
		"--source-runtime", $RuntimePackageDir,
		"--platform", "windows",
		"--architecture", $Architecture
	)
	Invoke-VeroScriptInstallerDotNet $RepoRoot $ToolArguments
	Invoke-VeroScriptIcons @('verify-bundle', '--repo-root', $RepoRoot, '--root', $BundleDirectory, '--target', $Runtime)

	Invoke-VeroScriptInstallerDotNet $RepoRoot @('run', '--project', $ToolProject, '-c', 'Release', '--', 'verify', '--bundle', $BundleDirectory)

	Invoke-VeroScriptInstallerDotNet $RepoRoot @('run', '--project', $ToolProject, '-c', 'Release', '--', 'pack-bundle',
		'--bundle', $BundleDirectory, '--output', $EmbeddedArchive)

	Write-Host "Publishing localized single-file installer ($Runtime)..." -ForegroundColor Cyan
	Invoke-VeroScriptInstallerDotNet $RepoRoot @('publish', $InstallerProject, '-c', 'Release', '-r', $Runtime, '--self-contained', 'true',
		'-p:PublishSingleFile=true', '-p:IncludeNativeLibrariesForSelfExtract=true', '-p:EnableCompressionInSingleFile=true',
		'-p:PublishTrimmed=false', '-p:DebugType=None', '-p:DebugSymbols=false', "-p:Version=$Version",
		"-p:InstallerBundleArchive=$EmbeddedArchive", '-o', $PublishDirectory)

	$PublishedInstaller = Join-Path $PublishDirectory 'VeroScriptInstaller.exe'
	if (-not (Test-Path -LiteralPath $PublishedInstaller -PathType Leaf))
	{
		throw "Published installer executable not found: $PublishedInstaller"
	}
	Assert-VeroScriptInstallerLicense $PublishedInstaller (Join-Path $RepoRoot 'LICENSE')
	$LocalizationVerifyProcess = Start-Process -FilePath $PublishedInstaller `
		-ArgumentList "--verify-localization" -WindowStyle Hidden -Wait -PassThru
	if ($LocalizationVerifyProcess.ExitCode -ne 0)
	{
		throw "installer localization verification failed with exit code $($LocalizationVerifyProcess.ExitCode)"
	}
	$VerifyProcess = Start-Process -FilePath $PublishedInstaller `
		-ArgumentList "--verify-embedded-bundle" -WindowStyle Hidden -Wait -PassThru
	if ($VerifyProcess.ExitCode -ne 0)
	{
		throw "embedded installer bundle verification failed with exit code $($VerifyProcess.ExitCode)"
	}

	$InstallerStage = Join-Path $OutRootPath (
		"$InstallerFileName.new-$([guid]::NewGuid().ToString('N')).exe")
	Copy-Item -LiteralPath $PublishedInstaller -Destination $InstallerStage
	& $SigningScript -Path $InstallerStage `
		-CertificateThumbprint $SigningCertificateThumbprint `
		-TimestampServer $TimestampServer `
		-RequireSignature:$RequireSignature
	foreach ($VerificationArgument in @('--verify-localization', '--verify-embedded-bundle')) {
		$SignedVerification = Start-Process -FilePath $InstallerStage `
			-ArgumentList $VerificationArgument -WindowStyle Hidden -Wait -PassThru
		if ($SignedVerification.ExitCode -ne 0) {
			throw "Signed installer verification '$VerificationArgument' failed ($($SignedVerification.ExitCode))."
		}
	}
	Move-Item -LiteralPath $InstallerStage -Destination $InstallerPath -Force
	$InstallerStage = $null

	Write-Host ""
	Write-Host "Single-file installer: $InstallerPath" -ForegroundColor Green
	Write-Host "PATH modification is an explicit, default-off checkbox in the installer." -ForegroundColor Yellow
}
finally
{
	if ($InstallerStage -and (Test-Path -LiteralPath $InstallerStage -PathType Leaf))
	{
		Remove-Item -LiteralPath $InstallerStage -Force -ErrorAction SilentlyContinue
	}
	if (Test-Path -LiteralPath $PublishDirectory)
	{
		Remove-Item -LiteralPath $PublishDirectory -Recurse -Force -ErrorAction SilentlyContinue
	}
	if (Test-Path -LiteralPath $BundleDirectory)
	{
		Remove-Item -LiteralPath $BundleDirectory -Recurse -Force -ErrorAction SilentlyContinue
	}
	if (Test-Path -LiteralPath $EmbeddedArchive)
	{
		Remove-Item -LiteralPath $EmbeddedArchive -Force -ErrorAction SilentlyContinue
	}
}
