#Requires -Version 5.1
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$ArtifactDirectory,
	[ValidateSet('Debug', 'Release')]
	[string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$ModuleRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$ModuleRelative = if ($ModuleRoot -eq $RepoRoot) { '.' }
else
{
	$ModuleRoot.Substring($RepoRoot.TrimEnd('\', '/').Length + 1)
}
$WebAssemblyRoot = Join-Path $RepoRoot "$ModuleRelative\Extensions\Platform\WebAssembly\VeroScriptWebAssembly"
$ArtifactRoot = (Resolve-Path -LiteralPath $ArtifactDirectory).Path
$NormalizedConfiguration = if ($Configuration -ieq 'Debug') { 'Debug' }
else
{
	'Release'
}

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

$NodeCommand = if ($env:EMSDK_NODE -and (Test-Path -LiteralPath $env:EMSDK_NODE -PathType Leaf)) {
	$env:EMSDK_NODE
}
else
{
	$Node = Get-Command node, node.exe -ErrorAction SilentlyContinue | Select-Object -First 1
	if ($Node) { $Node.Source }
	else
	{
		$null
	}
}
if (-not $NodeCommand) {
	throw 'Node.js is required to verify the generated WebAssembly SDK.'
}

$Npm = Get-Command npm, npm.cmd -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $Npm) {
	throw 'npm is required to type-check and browser-test the WebAssembly SDK.'
}

Write-Host 'Type-checking WebAssembly SDK bindings and declarations...' -ForegroundColor Cyan
Push-Location $WebAssemblyRoot
try {
	Invoke-Checked $Npm.Source @('ci', '--no-audit', '--no-fund')
	Invoke-Checked $Npm.Source @('run', 'typecheck')
}
finally
{
	Pop-Location
}

foreach ($FileName in @(
	'veroscript-engine.d.mts',
	'veroscript-engine-fast.d.mts',
	'veroscript-webassembly.ts',
	'veroscript-webassembly.d.ts'
)) {
	$SourcePath = Join-Path (Join-Path $WebAssemblyRoot 'Bindings') $FileName
	$ArtifactPath = Join-Path $ArtifactRoot $FileName
	if (-not (Test-Path -LiteralPath $ArtifactPath -PathType Leaf)) {
		throw "Required WebAssembly SDK declaration or binding is missing: $ArtifactPath"
	}
	if ((Get-FileHash -LiteralPath $SourcePath -Algorithm SHA256).Hash -ne
		(Get-FileHash -LiteralPath $ArtifactPath -Algorithm SHA256).Hash) {
		throw "Staged WebAssembly SDK declaration or binding is stale: $ArtifactPath"
	}
}

$ArtifactAudit = Join-Path $WebAssemblyRoot 'Tests\VeroScriptWebAssemblyArtifactAudit.mjs'
$SmokeTest = Join-Path $WebAssemblyRoot 'Tests\VeroScriptWebAssemblySmoke.mjs'
$BindingsSmokeTest = Join-Path $WebAssemblyRoot 'Tests\VeroScriptWebAssemblyBindingsSmoke.mjs'

Write-Host 'Validating staged WebAssembly SDK artifacts...' -ForegroundColor Cyan
Invoke-Checked $NodeCommand @($ArtifactAudit, $ArtifactRoot, $NormalizedConfiguration)
Invoke-Checked $NodeCommand @($SmokeTest, (Join-Path $ArtifactRoot 'veroscript-engine.mjs'))
Invoke-Checked $NodeCommand @(
	$SmokeTest,
	(Join-Path $ArtifactRoot 'veroscript-engine-fast.mjs'),
	'--non-interactive'
)
Invoke-Checked $NodeCommand @(
	$BindingsSmokeTest,
	(Join-Path $ArtifactRoot 'veroscript-webassembly.ts')
)
Invoke-Checked $Npm.Source @(
	'--prefix',
	$WebAssemblyRoot,
	'run',
	'test:browser',
	'--',
	$ArtifactRoot
)

Write-Host "VeroScript WebAssembly SDK verification passed: $ArtifactRoot" -ForegroundColor Green
