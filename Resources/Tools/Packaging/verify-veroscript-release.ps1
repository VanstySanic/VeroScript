#Requires -Version 5.1
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

[CmdletBinding()]
param(
	[string]$ReleaseDirectory = '',
	[ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version = '',
	[ValidateSet('win-x64','win-x86','win-arm64','linux-x64','linux-arm64','macos-x64','macos-arm64','android','ios','any','wasm32')]
	[string]$Target = 'win-x64',
	[string]$PythonExecutable = '',
	[switch]$RequireSignedInstallers,
	[switch]$RequireComplete,
	[switch]$CheckSource
)
$ErrorActionPreference = 'Stop'
$RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
. (Join-Path $PSScriptRoot 'veroscript-output.ps1')
if (-not $ReleaseDirectory)
{
	$ReleaseDirectory = Join-Path $RepoRoot 'Release'
}
$Root = if ([IO.Path]::IsPathRooted($ReleaseDirectory))
{
	[IO.Path]::GetFullPath($ReleaseDirectory)
}
else
{
	[IO.Path]::GetFullPath((Join-Path $RepoRoot $ReleaseDirectory))
}
if (-not $PythonExecutable)
{
	$Commands = @(Get-Command python3, python -All -CommandType Application -ErrorAction SilentlyContinue)
	$Python = $Commands | Where-Object { $_.Source -notmatch '[/\\]Microsoft[/\\]WindowsApps[/\\]' } | Select-Object -First 1
	if (-not $Python)
	{
		$Python = $Commands | Select-Object -First 1
	}
	if (-not $Python)
	{
		throw 'Python 3.11 or newer is required to audit a release.'
	}
	$PythonExecutable = $Python.Source
}
& $PythonExecutable -c 'import sys; sys.exit(0 if sys.version_info >= (3, 11) else 1)'
if ($LASTEXITCODE -ne 0)
{
	throw 'Python 3.11 or newer is required to audit a release.'
}
$Collection = Test-Path -LiteralPath (Join-Path $Root 'release-index.json') -PathType Leaf
$AuditArguments = @('--release-directory', $Root, '--repo-root', $RepoRoot)
if ($Collection -or $PSBoundParameters.ContainsKey('Target'))
{
	$AuditArguments += @('--target', $Target)
}
if ($RequireComplete)
{
	$AuditArguments += '--require-complete'
}
if ($CheckSource)
{
	$AuditArguments += '--check-source'
}
if ($Version)
{
	$AuditArguments += @('--version', $Version)
}
# Validate schemas, paths and references before using their contents in PowerShell.
& $PythonExecutable (Join-Path $PSScriptRoot 'verify-veroscript-release.py') @AuditArguments
if ($LASTEXITCODE -ne 0)
{
	throw 'Release verification failed.'
}
$InstallerPaths = @()
if ($Collection)
{
	$Index = Get-Content -LiteralPath (Join-Path $Root "indexes/$Target.json") -Raw -Encoding UTF8 | ConvertFrom-Json
	foreach ($Business in $Index.businesses.PSObject.Properties)
	{
		foreach ($Reference in $Business.Value.manifests)
		{
			if ($Version -and $Reference.version -ne $Version)
			{
				throw 'Requested version does not match an indexed business version.'
			}
			$Leaf = Split-Path -Parent (Join-Path $Root $Reference.manifest)
			$Manifest = Get-Content -LiteralPath (Join-Path $Leaf 'release-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
			$InstallerPaths += @($Manifest.artifacts | Where-Object { $_.role -eq 'installers' -and $_.path -match '\.exe$' } | ForEach-Object { Join-Path $Leaf $_.path })
		}
	}
}
else
{
	$Manifest = Get-Content -LiteralPath (Join-Path $Root 'release-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
	if (-not $PSBoundParameters.ContainsKey('Target'))
	{
		$Target = [string]$Manifest.target
	}
	foreach ($Business in $Manifest.businessVersions.PSObject.Properties)
	{
		if (($null -eq $Manifest.selectedBusinesses -or @($Manifest.selectedBusinesses).Count -eq 0 -or $Business.Name -in $Manifest.selectedBusinesses) -and $Version -and $Business.Value -ne $Version)
		{
			throw 'Requested version does not match a candidate business version.'
		}
	}
	if ($null -eq $Manifest.businessVersions -and $Version -and $Manifest.version -ne $Version)
	{
		throw 'Requested version does not match the candidate version.'
	}
	$InstallerPaths = @($Manifest.artifacts | Where-Object { $_.path -match '^installer/[^/]+\.exe$' } | ForEach-Object { Join-Path $Root $_.path })
}
if ($RequireSignedInstallers)
{
	if ($Target -notlike 'win-*')
	{
		throw 'Authenticode verification requires a Windows target.'
	}
	foreach ($Installer in $InstallerPaths)
	{
		$Signature = Get-AuthenticodeSignature -LiteralPath $Installer
		if ($Signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid)
		{
			throw "Invalid installer Authenticode signature: $Installer"
		}
	}
}
