#Requires -Version 5.1
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

$ErrorActionPreference = 'Stop'
$WorkspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
. (Join-Path $PSScriptRoot '../veroscript-output.ps1')
function Expect-OutputRejection([scriptblock]$Action, [string]$Pattern)
{
	$Rejected = $false
	try {
		& $Action
	}
	catch
	{
		$Rejected = $true
		if ($_.Exception.Message -notmatch $Pattern) {
			throw
		}
	}
	if (-not $Rejected) {
		throw 'Expected output rejection, but the operation succeeded.'
	}
}
# A successful action must never be mistaken for a passing rejection test.
$AssertionDetectedSuccess = $false
try {
	Expect-OutputRejection {} 'collection root'
}
catch
{
	if ($_.Exception.Message -like 'Expected output rejection,*') {
		$AssertionDetectedSuccess = $true
	}
	else
	{
		throw
	}
}
if (-not $AssertionDetectedSuccess) {
	throw 'The rejection assertion accepted a successful action.'
}
$Errors = @()
$Ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot '../package-veroscript-all.ps1'), [ref]$null, [ref]$Errors)
if ($Errors.Count) {
	throw ($Errors | Out-String)
}
foreach ($Name in @('Assert-SafeReleaseOutputPath')) {
	$Definition = $Ast.Find({ param($Node) $Node -is [Management.Automation.Language.FunctionDefinitionAst] -and $Node.Name -eq $Name }, $true)
	Invoke-Expression $Definition.Extent.Text
}
$VerifierAst = [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot '../verify-veroscript-release.ps1'), [ref]$null, [ref]$Errors)
if ($Errors.Count) {
	throw ($Errors | Out-String)
}
function Move-DirectoryWithRetry([string]$Source, [string]$Destination, [string]$Description)
{
	[IO.Directory]::Move($Source, $Destination)
}
$RepoRoot = Join-Path $WorkspaceRoot ('.tmp/veroscript-output-guard-fixture-' + [guid]::NewGuid().ToString('N'))
$Collection = Join-Path $RepoRoot 'Script/Release'
New-Item -ItemType Directory -Path $Collection -Force | Out-Null
$File = Join-Path $Collection 'fixture.txt'
[IO.File]::WriteAllText($File, 'old release')
$Hash = (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash
$Manifest = @{ schemaVersion = 2; product = 'VeroScript Release Candidate'; version = '1.0.0'; platform = 'windows'; architecture = 'x64'; artifacts = @(@{ path = 'fixture.txt'; size = (Get-Item $File).Length; sha256 = $Hash }) }
[IO.File]::WriteAllText((Join-Path $Collection 'release-manifest.json'), ($Manifest | ConvertTo-Json -Depth 6))
[IO.File]::WriteAllText((Join-Path $Collection 'checksums.txt'), "$Hash  fixture.txt")
$Version = '2.0.0'
$UsesDefaultOutputRoot = $true
Expect-OutputRejection { Assert-SafeReleaseOutputPath $Collection } 'collection root'
$Migrated = Join-Path $RepoRoot '.tmp/verified-stage'
New-Item -ItemType Directory -Path $Migrated -Force | Out-Null
foreach ($FixtureFile in Get-ChildItem -LiteralPath $Collection -File) {
	Move-Item -LiteralPath $FixtureFile.FullName -Destination $Migrated
}
if ((Get-FileHash -LiteralPath (Join-Path $Migrated 'fixture.txt') -Algorithm SHA256).Hash -ne $Hash) {
	throw 'Migration lost the old file'
}
Expect-OutputRejection { Assert-SafeReleaseOutputPath $Migrated } 'different version or target'
$Version = '1.0.0'
$Manifest.platform = 'linux'
[IO.File]::WriteAllText((Join-Path $Migrated 'release-manifest.json'), ($Manifest | ConvertTo-Json -Depth 6))
Expect-OutputRejection { Assert-SafeReleaseOutputPath $Migrated } 'different version or target'
Expect-OutputRejection { Assert-VeroScriptComponentOutputPath (Join-Path $Migrated 'runtime') $RepoRoot } 'cannot modify a complete release'
Expect-OutputRejection { Assert-VeroScriptComponentOutputPath (Join-Path $RepoRoot 'Script/VeroScript') $RepoRoot } 'repository source'
Assert-VeroScriptComponentOutputPath (Join-Path $RepoRoot '.tmp/components/win-x64/runtime') $RepoRoot
Expect-OutputRejection { Assert-VeroScriptComponentOutputPath (Join-Path $Collection 'win-x64') $RepoRoot } 'managed Release'
$PlatformFixture = Join-Path $Collection 'win-x64'
New-Item -ItemType Directory -Path $PlatformFixture -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $PlatformFixture 'release-index-win-x64.json'), '{}')
Expect-OutputRejection { Assert-VeroScriptComponentOutputPath (Join-Path $PlatformFixture 'Runtime/1.0.0/stage') $RepoRoot } 'managed Release'

$TargetDirectory = Join-Path $RepoRoot '.tmp/link-target'
$LinkDirectory = Join-Path $RepoRoot '.tmp/linked-output'
New-Item -ItemType Directory -Path $TargetDirectory -Force | Out-Null
New-Item -ItemType Junction -Path $LinkDirectory -Target $TargetDirectory | Out-Null
Expect-OutputRejection { Assert-VeroScriptComponentOutputPath (Join-Path $LinkDirectory 'runtime') $RepoRoot } 'Linked component output'
Expect-OutputRejection { Assert-SafeReleaseOutputPath (Join-Path $LinkDirectory 'release') } 'junction or symbolic-link'
$MovedTarget = Join-Path $RepoRoot '.tmp/link-target-preserved'
foreach ($MovePath in @($TargetDirectory, $MovedTarget)) {
	if (-not ([IO.Path]::GetFullPath($MovePath).StartsWith(($RepoRoot + '\'), [StringComparison]::OrdinalIgnoreCase))) {
		throw 'Invalid fixture move path'
	}
}
[IO.Directory]::Move($TargetDirectory, $MovedTarget)
Expect-OutputRejection { Assert-VeroScriptComponentOutputPath (Join-Path $LinkDirectory 'runtime') $RepoRoot } 'Linked component output'
Expect-OutputRejection { Assert-SafeReleaseOutputPath (Join-Path $LinkDirectory 'release') } 'junction or symbolic-link'
$EntryOutput = Join-Path $RepoRoot '.tmp/entry-output'
New-Item -ItemType Directory -Path $EntryOutput -Force | Out-Null
New-Item -ItemType Junction -Path (Join-Path $EntryOutput 'existing-package') -Target $MovedTarget | Out-Null
Expect-OutputRejection { Assert-VeroScriptComponentOutputPath $EntryOutput $RepoRoot } 'Linked component output entry'

# An ordinary output path must not hide a linked repository scratch root.
# Rejecting staging must preserve the previous release and the link target.
$FixtureRepoRoot = $RepoRoot
$ScratchRepo = Join-Path $FixtureRepoRoot '.tmp/scratch-repo'
$ScratchTarget = Join-Path $FixtureRepoRoot '.tmp/scratch-target'
$ScratchCollection = Join-Path $ScratchRepo 'Script/Release'
New-Item -ItemType Directory -Path $ScratchCollection, $ScratchTarget -Force | Out-Null
$ScratchLink = Join-Path $ScratchRepo '.tmp'
New-Item -ItemType Junction -Path $ScratchLink -Target $ScratchTarget | Out-Null
[IO.File]::WriteAllText((Join-Path $ScratchTarget 'preserved.txt'), 'outside scratch')
[IO.File]::WriteAllText((Join-Path $ScratchCollection 'fixture.txt'), 'old release')
$ScratchManifest = @{ schemaVersion = 2; product = 'VeroScript Release Candidate'; version = '1.0.0'; platform = 'windows'; architecture = 'x64'; artifacts = @(@{ path = 'fixture.txt'; size = (Get-Item (Join-Path $ScratchCollection 'fixture.txt')).Length; sha256 = $Hash }) }
[IO.File]::WriteAllText((Join-Path $ScratchCollection 'release-manifest.json'), ($ScratchManifest | ConvertTo-Json -Depth 6))
[IO.File]::WriteAllText((Join-Path $ScratchCollection 'checksums.txt'), "$Hash  fixture.txt")
try {
	foreach ($ScratchName in @('veroscript-release-stage-fixture', 'vss-consumer-fixture', 'vsi-consumer-fixture')) {
		Expect-OutputRejection { Assert-VeroScriptComponentOutputPath (Join-Path $ScratchLink $ScratchName) $ScratchRepo } 'Linked component output'
	}
	$RepoRoot = $ScratchRepo
	if ((Get-FileHash -LiteralPath (Join-Path $ScratchCollection 'fixture.txt') -Algorithm SHA256).Hash -ne $Hash -or
		@(Get-ChildItem -LiteralPath $ScratchTarget -Force).Count -ne 1 -or
		[IO.File]::ReadAllText((Join-Path $ScratchTarget 'preserved.txt')) -ne 'outside scratch') {
		throw 'Rejected scratch staging changed the previous release or link target'
	}
}
finally
{
	$RepoRoot = $FixtureRepoRoot
	[IO.Directory]::Delete($ScratchLink, $false)
}

# Invoke every public component entry point: rejection must precede build,
# source validation and any file mutation, even with a custom output path.
$Scripts = @('package-veroscript.ps1', 'package-veroscript-docs.ps1', 'package-veroscript-sdk.ps1',
	'package-veroscript-interop-sdk.ps1', 'package-veroscript-symbols.ps1', 'package-veroscript-webassembly.ps1',
	'package-veroscript-product-installer.ps1')
foreach ($ScriptName in $Scripts) {
	$Arguments = @{ OutRoot = (Join-Path $Migrated 'component-output') }
	if ($ScriptName -eq 'package-veroscript-product-installer.ps1') {
		$Arguments.Profile = 'core-sdk'; $Arguments.ComponentSources = @{ sdk = $Migrated }; $Arguments.InstallerName = 'VeroScriptCoreSDKInstaller'
	}
	Expect-OutputRejection { & (Join-Path $PSScriptRoot ('../' + $ScriptName)) @Arguments } 'cannot modify a complete release'
}
if ((Get-FileHash -LiteralPath (Join-Path $Migrated 'fixture.txt') -Algorithm SHA256).Hash -ne $Hash) {
	throw 'A rejected operation changed the release'
}
Write-Output 'PASSED: assertion, collection/version/platform guards, source/link/scratch protection and all 10 component entry points'
