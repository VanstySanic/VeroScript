#Requires -Version 5.1
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

<#
.SYNOPSIS
Install a local Runtime through the managed installer engine. PATH is opt-in.
.EXAMPLE
.\Resources\Tools\Packaging\install-veroscript.ps1 -ConfigurePreset windows-msvc-vs2026 -InstallDir D:\Tools\VeroScript
.EXAMPLE
.\Resources\Tools\Packaging\install-veroscript.ps1 -SourceDir D:\Packages\Runtime -AddToPath
#>
[CmdletBinding()]
param(
	[string]$ConfigurePreset = '',
	[ValidateSet('Debug', 'Release')][string]$Config = 'Release',
	[string]$SourceDir = '',
	[string]$InstallDir = '',
	[switch]$AddToPath,
	[switch]$NoAddToPath
)
$ErrorActionPreference = 'Stop'
if ($AddToPath -and $NoAddToPath) { throw 'Choose either -AddToPath or -NoAddToPath.' }
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$ModuleRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$ModuleRelative = if ($ModuleRoot -eq $RepoRoot) { '.' }
else
{
	$ModuleRoot.Substring($RepoRoot.TrimEnd('\', '/').Length + 1)
}
if (-not $ConfigurePreset) {
	foreach ($Candidate in @('windows-msvc-vs2026', 'windows-msvc-vs2022', 'windows-msvc-vs2019')) {
		if (Test-Path -LiteralPath (Join-Path $RepoRoot "build/$Candidate/bin/$Config/veroscript.exe") -PathType Leaf) {
			$ConfigurePreset = $Candidate
			break
		}
	}
	if (-not $ConfigurePreset) { $ConfigurePreset = 'windows-msvc-vs2022' }
}
if (-not $SourceDir) { $SourceDir = Join-Path $RepoRoot "build/$ConfigurePreset/bin/$Config" }
$SourceDir = [IO.Path]::GetFullPath($SourceDir)
# Prefer the interpreter already selected by CMake to avoid Windows Store aliases.
$Python = $null
$CachePath = Join-Path $RepoRoot "build/$ConfigurePreset/CMakeCache.txt"
if (Test-Path -LiteralPath $CachePath -PathType Leaf) {
	foreach ($Line in Get-Content -LiteralPath $CachePath) {
		if ($Line -match '^_?Python3_EXECUTABLE:[^=]+=(.+)$' -and (Test-Path -LiteralPath $Matches[1] -PathType Leaf)) {
			$Python = $Matches[1]
			break
		}
	}
}
if (-not $Python) {
	foreach ($Name in @('python3', 'python')) {
		$Command = Get-Command $Name -ErrorAction SilentlyContinue
		if ($Command) { $Python = $Command.Source; break }
	}
}
if (-not $Python) { throw 'Local installation requires Python 3.11+ and .NET 9 SDK (or a compatible newer SDK).' }
$Arguments = @((Join-Path $PSScriptRoot 'install-veroscript-runtime.py'), '--repo-root', $RepoRoot, '--source-dir', $SourceDir, '--config', $Config)
if ($InstallDir) { $Arguments += @('--install-dir', [IO.Path]::GetFullPath($InstallDir)) }
if ($AddToPath) { $Arguments += '--add-to-path' }
& $Python @Arguments
if ($LASTEXITCODE -ne 0) { throw "Managed Runtime installation failed with exit code $LASTEXITCODE." }
