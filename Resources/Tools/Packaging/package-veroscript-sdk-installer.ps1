#Requires -Version 5.1
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

<# Build the selectable, embedded VeroScript developer SDK installer. #>
[CmdletBinding()]
param(
	[ValidatePattern('^(win|windows)-(x64|x86|arm64)$')]
	[string]$Runtime = "win-x64",
	[ValidatePattern('^\d+\.\d+\.\d+$')]
	[string]$Version = '',
	[string]$SdkPackageDir = "",
	[string]$SymbolsPackageDir = "",
	[string]$OutRoot = '',
	[string]$SigningCertificateThumbprint = $env:VEROSCRIPT_SIGNING_CERTIFICATE_THUMBPRINT,
	[string]$TimestampServer = $env:VEROSCRIPT_TIMESTAMP_SERVER,
	[switch]$RequireSignature
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
if (Invoke-VeroScriptDefaultBusinessPackage $PSCommandPath $PSBoundParameters $RepoRoot $ReleaseBusiness $Version ($Runtime -replace '^windows-', 'win-')) {
	return
}

if (-not [string]::IsNullOrWhiteSpace($OutRoot)) { Assert-VeroScriptComponentOutputPath $OutRoot $RepoRoot }
if ([string]::IsNullOrWhiteSpace($OutRoot)) {
	$OutRoot = "$ModuleRelative\Release\CoreSDK\$($Runtime -replace '^windows-', 'win-')\$Version"
}
$OutRootPath = if ([IO.Path]::IsPathRooted($OutRoot)) { [IO.Path]::GetFullPath($OutRoot) }
else
{
	[IO.Path]::GetFullPath((Join-Path $RepoRoot $OutRoot))
}

Assert-VeroScriptComponentOutputPath $OutRootPath $RepoRoot

$Parts = $Runtime.Split('-')
if ($Parts.Count -lt 2 -or $Parts[0].ToLowerInvariant() -notin @("win", "windows")) { throw "The SDK installer currently requires a Windows RID such as win-x64." }
$Architecture = $Parts[$Parts.Count - 1].ToLowerInvariant()
$DotnetRuntime = "win-$Architecture"
$ReleaseRoot = Split-Path $OutRootPath -Parent
if (-not $SdkPackageDir) { $SdkPackageDir = Join-Path $ReleaseRoot "sdk\VeroScriptSDK-$Version-win-$Architecture" }
if (-not (Test-Path -LiteralPath $SdkPackageDir -PathType Container)) { throw "Core SDK package not found: $SdkPackageDir" }
Invoke-VeroScriptIcons @('verify-executables', '--repo-root', $RepoRoot, '--root', $SdkPackageDir, '--target', $DotnetRuntime)

$DefaultSymbolsPackageDir = Join-Path $ReleaseRoot "symbols\VeroScriptSymbols-$Version-win-$Architecture"
if (-not $SymbolsPackageDir -and (Test-Path -LiteralPath $DefaultSymbolsPackageDir -PathType Container)) { $SymbolsPackageDir = $DefaultSymbolsPackageDir }
if ($SymbolsPackageDir -and -not (Test-Path -LiteralPath $SymbolsPackageDir -PathType Container)) { throw "Symbols package not found: $SymbolsPackageDir" }

$InstallerProject = Join-Path $RepoRoot "$ModuleRelative\VeroScriptInstaller\App\VeroScriptInstaller.csproj"
$ToolProject = Join-Path $RepoRoot "$ModuleRelative\VeroScriptInstaller\Tool\VeroScriptInstaller.Tool.csproj"
$SigningScript = Join-Path $RepoRoot "$ModuleRelative\Resources\Tools\Packaging\sign-veroscript-installer.ps1"
$PackageName = "VeroScriptCoreSDKInstaller-$Version-win-$Architecture"
$InstallerPath = Join-Path $OutRootPath "$PackageName.exe"
$PublishDirectory = Join-Path ([IO.Path]::GetTempPath()) ("veroscript-sdk-installer-publish-" + [guid]::NewGuid().ToString("N"))
$BundleDirectory = Join-Path ([IO.Path]::GetTempPath()) ("veroscript-sdk-installer-bundle-" + [guid]::NewGuid().ToString("N"))
$EmbeddedArchive = Join-Path ([IO.Path]::GetTempPath()) ("veroscript-sdk-installer-bundle-" + [guid]::NewGuid().ToString("N") + ".zip")
$Stage = $null
try
{
	$SdkComponentSource = [IO.Path]::GetFullPath($SdkPackageDir)
	New-Item -ItemType Directory -Path $OutRootPath -Force | Out-Null
	$BundleArguments = @(
		"run", "--project", $ToolProject, "-c", "Release", "--", "build-bundle",
		"--output", $BundleDirectory, "--version", $Version,
		"--profile", "core-sdk",
		"--platform", "windows", "--architecture", $Architecture,
		"--source-sdk", $SdkComponentSource
	)
	if ($SymbolsPackageDir) { $BundleArguments += @("--source-symbols", ([IO.Path]::GetFullPath($SymbolsPackageDir))) }
	Invoke-VeroScriptInstallerDotNet $RepoRoot $BundleArguments
	Invoke-VeroScriptInstallerDotNet $RepoRoot @('run', '--project', $ToolProject, '-c', 'Release', '--', 'verify', '--bundle', $BundleDirectory)
	Invoke-VeroScriptInstallerDotNet $RepoRoot @('run', '--project', $ToolProject, '-c', 'Release', '--', 'pack-bundle',
		'--bundle', $BundleDirectory, '--output', $EmbeddedArchive)

	Invoke-VeroScriptInstallerDotNet $RepoRoot @('publish', $InstallerProject, '-c', 'Release', '-r', $DotnetRuntime, '--self-contained', 'true',
		'-p:PublishSingleFile=true', '-p:IncludeNativeLibrariesForSelfExtract=true', '-p:EnableCompressionInSingleFile=true',
		'-p:PublishTrimmed=false', '-p:DebugType=None', '-p:DebugSymbols=false', "-p:Version=$Version",
		"-p:InstallerBundleArchive=$EmbeddedArchive", '-o', $PublishDirectory)
	$PublishedInstaller = Join-Path $PublishDirectory "VeroScriptInstaller.exe"
	Assert-VeroScriptInstallerLicense $PublishedInstaller (Join-Path $RepoRoot 'LICENSE')
	foreach ($Arguments in @("--verify-localization", "--verify-embedded-bundle"))
	{
		$Process = Start-Process -FilePath $PublishedInstaller -ArgumentList $Arguments -WindowStyle Hidden -Wait -PassThru
		if ($Process.ExitCode -ne 0) { throw "SDK installer verification '$Arguments' failed ($($Process.ExitCode))." }
	}
	$Stage = Join-Path $OutRootPath (
		"$([IO.Path]::GetFileNameWithoutExtension($InstallerPath)).new-$([guid]::NewGuid().ToString('N')).exe")
	Copy-Item -LiteralPath $PublishedInstaller -Destination $Stage
	& $SigningScript -Path $Stage `
		-CertificateThumbprint $SigningCertificateThumbprint `
		-TimestampServer $TimestampServer `
		-RequireSignature:$RequireSignature
	foreach ($VerificationArgument in @('--verify-localization', '--verify-embedded-bundle'))
	{
		$SignedVerification = Start-Process -FilePath $Stage `
			-ArgumentList $VerificationArgument -WindowStyle Hidden -Wait -PassThru
		if ($SignedVerification.ExitCode -ne 0) {
			throw "Signed SDK installer verification '$VerificationArgument' failed ($($SignedVerification.ExitCode))."
		}
	}
	Move-Item -LiteralPath $Stage -Destination $InstallerPath -Force
	$Stage = $null
}
finally
{
	if ($Stage -and (Test-Path -LiteralPath $Stage -PathType Leaf))
	{
		Remove-Item -LiteralPath $Stage -Force -ErrorAction SilentlyContinue
	}
	$TempPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
	foreach ($Path in @($PublishDirectory, $BundleDirectory)) {
		$FullPath = [IO.Path]::GetFullPath($Path)
		if (-not $FullPath.StartsWith($TempPrefix, [StringComparison]::OrdinalIgnoreCase)) {
			throw "SDK installer cleanup path escapes its temporary directory: $FullPath"
		}
		if (Test-Path -LiteralPath $FullPath) { Remove-Item -LiteralPath $FullPath -Recurse -Force -ErrorAction SilentlyContinue }
	}
	if (Test-Path -LiteralPath $EmbeddedArchive) { Remove-Item -LiteralPath $EmbeddedArchive -Force -ErrorAction SilentlyContinue }
}
Write-Host "SDK installer: $InstallerPath" -ForegroundColor Green
