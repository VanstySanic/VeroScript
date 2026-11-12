#Requires -Version 5.1
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

<# Publish one data-driven, single-file VeroScript installer product. #>
[CmdletBinding(DefaultParameterSetName = "Profile")]
param(
	[Parameter(Mandatory = $true, ParameterSetName = "Profile")]
	[string]$Profile,

	[Parameter(Mandatory = $true, ParameterSetName = "Definition")]
	[string]$Definition,

	[Parameter(Mandatory = $true)]
	[ValidateNotNullOrEmpty()]
	[hashtable]$ComponentSources,

	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$')]
	[string]$InstallerName,

	[ValidatePattern('^(win|windows)-(x64|x86|arm64)$')]
	[string]$Runtime = "win-x64",

	[ValidatePattern('^\d+\.\d+\.\d+$')]
	[string]$Version = '',

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
. (Join-Path $PSScriptRoot 'veroscript-output.ps1')
. (Join-Path $PSScriptRoot 'veroscript-assets.ps1')
$ReleaseConfiguration = Get-VeroScriptReleaseConfiguration $RepoRoot
$ReleaseBusiness = ($InstallerName -replace '^VeroScript', '' -replace 'Installer$', '')
if (-not $ReleaseConfiguration.businesses.PSObject.Properties[$ReleaseBusiness]) {
	throw "Unknown release business: $ReleaseBusiness"
}
if ($ReleaseBusiness -notin $ReleaseConfiguration.installerBusinesses) {
	throw "Business '$ReleaseBusiness' does not provide a standalone installer. Modules use functional packages."
}
if (-not $Version) {
	$Version = $ReleaseConfiguration.businesses.$ReleaseBusiness
}
if (Invoke-VeroScriptDefaultBusinessPackage $PSCommandPath $PSBoundParameters $RepoRoot $ReleaseBusiness $Version ($Runtime -replace '^windows-', 'win-')) {
	return
}

if (-not [string]::IsNullOrWhiteSpace($OutRoot)) { Assert-VeroScriptComponentOutputPath $OutRoot $RepoRoot }
$ToolProject = Join-Path $RepoRoot "$ModuleRelative\VeroScriptInstaller\Tool\VeroScriptInstaller.Tool.csproj"
$AppProject = Join-Path $RepoRoot "$ModuleRelative\VeroScriptInstaller\App\VeroScriptInstaller.csproj"
$SigningScript = Join-Path $RepoRoot "$ModuleRelative\Resources\Tools\Packaging\sign-veroscript-installer.ps1"
if ([string]::IsNullOrWhiteSpace($OutRoot)) {
	$OutRoot = "$ModuleRelative\Release\$($ReleaseConfiguration.businessPaths.$ReleaseBusiness)\$($Runtime -replace '^windows-', 'win-')\$Version"
}
$OutRootPath = if ([IO.Path]::IsPathRooted($OutRoot)) {
	[IO.Path]::GetFullPath($OutRoot)
}
else
{
	[IO.Path]::GetFullPath((Join-Path $RepoRoot $OutRoot))
}
$RuntimeParts = $Runtime.Split('-')
Assert-VeroScriptComponentOutputPath $OutRootPath $RepoRoot
$Platform = $RuntimeParts[0].ToLowerInvariant()
if ($Platform -eq "win") { $Platform = "windows" }
$Architecture = $RuntimeParts[$RuntimeParts.Count - 1].ToLowerInvariant()
$DotnetRuntime = "win-$Architecture"

$BundleDirectory = Join-Path ([IO.Path]::GetTempPath()) ("veroscript-product-bundle-" + [guid]::NewGuid().ToString("N"))
$EmbeddedArchive = Join-Path ([IO.Path]::GetTempPath()) ("veroscript-product-bundle-" + [guid]::NewGuid().ToString("N") + ".zip")
$PublishDirectory = Join-Path ([IO.Path]::GetTempPath()) ("veroscript-product-publish-" + [guid]::NewGuid().ToString("N"))
$InstallerPath = Join-Path $OutRootPath ("$InstallerName-$Version-$DotnetRuntime.exe")
$Stage = $null

try
{
	$BundleArguments = @(
		"run", "--project", $ToolProject, "-c", "Release", "--", "build-bundle",
		"--output", $BundleDirectory,
		"--version", $Version,
		"--platform", $Platform,
		"--architecture", $Architecture
	)
	if ($PSCmdlet.ParameterSetName -eq "Definition")
	{
		$DefinitionPath = (Get-Item -LiteralPath $Definition -ErrorAction Stop).FullName
		$BundleArguments += @("--definition", $DefinitionPath)
	}
	else
	{
		$BundleArguments += @("--profile", $Profile)
	}

	foreach ($ComponentId in @($ComponentSources.Keys | Sort-Object))
	{
		if ($ComponentId -notmatch '^[a-z][a-z0-9._-]{0,63}$')
		{
			throw "Invalid component id '$ComponentId'."
		}
		$Source = (Get-Item -LiteralPath ([string]$ComponentSources[$ComponentId]) -ErrorAction Stop).FullName
		if (-not (Test-Path -LiteralPath $Source -PathType Container))
		{
			throw "Component source is not a directory: $Source"
		}
		Invoke-VeroScriptIcons @('verify-executables', '--repo-root', $RepoRoot, '--root', $Source, '--target', $DotnetRuntime)
		$BundleArguments += @("--source-$ComponentId", $Source)
	}

	Invoke-VeroScriptInstallerDotNet $RepoRoot $BundleArguments
	if ($ReleaseBusiness -eq 'Runtime') {
		Invoke-VeroScriptIcons @('verify-bundle', '--repo-root', $RepoRoot, '--root', $BundleDirectory, '--target', $DotnetRuntime)
	}
	Invoke-VeroScriptInstallerDotNet $RepoRoot @('run', '--project', $ToolProject, '-c', 'Release', '--', 'verify', '--bundle', $BundleDirectory)
	Invoke-VeroScriptInstallerDotNet $RepoRoot @('run', '--project', $ToolProject, '-c', 'Release', '--', 'pack-bundle',
		'--bundle', $BundleDirectory, '--output', $EmbeddedArchive)

	Invoke-VeroScriptInstallerDotNet $RepoRoot @('publish', $AppProject, '-c', 'Release', '-r', $DotnetRuntime, '--self-contained', 'true',
		'-p:PublishSingleFile=true', '-p:IncludeNativeLibrariesForSelfExtract=true', '-p:EnableCompressionInSingleFile=true',
		'-p:PublishTrimmed=false', '-p:PublishReadyToRun=false', '-p:DebugType=None', '-p:DebugSymbols=false',
		"-p:Version=$Version", "-p:InstallerBundleArchive=$EmbeddedArchive", '-o', $PublishDirectory)

	$PublishedInstaller = Join-Path $PublishDirectory "VeroScriptInstaller.exe"
	Assert-VeroScriptInstallerLicense $PublishedInstaller (Join-Path $RepoRoot 'LICENSE')
	foreach ($Arguments in @("--verify-localization", "--verify-embedded-bundle"))
	{
		$Process = Start-Process -FilePath $PublishedInstaller -ArgumentList $Arguments -WindowStyle Hidden -Wait -PassThru
		if ($Process.ExitCode -ne 0) { throw "Installer verification '$Arguments' failed ($($Process.ExitCode))." }
	}

	New-Item -ItemType Directory -Path $OutRootPath -Force | Out-Null
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
			throw "Signed installer verification '$VerificationArgument' failed ($($SignedVerification.ExitCode))."
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
	foreach ($Path in @($BundleDirectory, $PublishDirectory))
	{
		if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction SilentlyContinue }
	}
	if (Test-Path -LiteralPath $EmbeddedArchive) { Remove-Item -LiteralPath $EmbeddedArchive -Force -ErrorAction SilentlyContinue }
}

Write-Host "Installer: $InstallerPath" -ForegroundColor Green
