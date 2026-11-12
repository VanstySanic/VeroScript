#Requires -Version 5.1
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

function Assert-VeroScriptInstallerLicense([string]$InstallerPath, [string]$LicensePath)
{
	$LicenseOutputPath = [IO.Path]::GetTempFileName()
	try {
		$Notices = [ordered]@{
			'--license' = $LicensePath
			'--third-party-licenses' = Join-Path $PSScriptRoot '../../../VeroScriptInstaller/Resources/Licenses/THIRD-PARTY-NOTICES.txt'
		}
		foreach ($Notice in $Notices.GetEnumerator()) {
			$LicenseProcess = Start-Process -FilePath $InstallerPath -ArgumentList $Notice.Key `
				-WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $LicenseOutputPath
			if ($LicenseProcess.ExitCode -ne 0) {
				throw "Installer license output '$($Notice.Key)' failed ($($LicenseProcess.ExitCode))."
			}
			$ExpectedNotice = (Get-Content -LiteralPath $Notice.Value -Raw -Encoding UTF8).Replace("`r`n", "`n")
			$ActualNotice = (Get-Content -LiteralPath $LicenseOutputPath -Raw -Encoding UTF8)
			if ($null -eq $ActualNotice -or $ActualNotice.Replace("`r`n", "`n") -ne $ExpectedNotice) {
				throw "The installer does not retain the complete license notice for '$($Notice.Key)'."
			}
		}
	}
	finally
	{
		Remove-Item -LiteralPath $LicenseOutputPath -Force -ErrorAction SilentlyContinue
	}
}

function Get-VeroScriptModuleRoot([string]$RepoRoot)
{
	$Owned = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
	$Repository = [IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/')
	if ($Owned.Equals($Repository, [StringComparison]::OrdinalIgnoreCase) -or
		$Owned.StartsWith(($Repository + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase)) {
		return $Owned
	}
	$Candidates = @(@($Repository) + @(Get-ChildItem -LiteralPath $Repository -Directory | ForEach-Object FullName) |
		Where-Object {
			if (Test-Path -LiteralPath (Join-Path $_ 'VeroScript/Public/VeroScriptVersion.h') -PathType Leaf) {
				return $true
			}
			$CatalogPath = Join-Path $_ 'Resources/Docs/documentation.json'
			if (-not (Test-Path -LiteralPath $CatalogPath -PathType Leaf)) {
				return $false
			}
			# Windows also matches Valence resources/docs; identify that consumer.
			$Identity = (Get-Content -LiteralPath $CatalogPath -Raw -Encoding UTF8 | ConvertFrom-Json).PSObject.Properties['product']
			return $null -eq $Identity -or $Identity.Value -cne 'ValenceStudio'
		})
	if ($Candidates.Count -eq 0) {
		$Candidates = @(Get-ChildItem -LiteralPath $Repository -Directory | Where-Object {
			(Test-Path -LiteralPath (Join-Path $_.FullName 'VeroScript/Resources') -PathType Container) -or
			(Test-Path -LiteralPath (Join-Path $_.FullName 'Resources/Assets') -PathType Container) -or
			(Test-Path -LiteralPath (Join-Path $_.FullName 'Release') -PathType Container)
		} | ForEach-Object FullName)
	}
	if ($Candidates.Count -gt 1) {
		throw 'Ambiguous VeroScript module roots.'
	}
	if ($Candidates.Count -eq 1) {
		return $Candidates[0]
	}
	return Join-Path $Repository (Split-Path $Owned -Leaf)
}

function Get-VeroScriptPathAttributes([string]$Path)
{
	try {
		return [IO.File]::GetAttributes($Path)
	}
	catch [IO.FileNotFoundException]
	{
		return $null
	}
	catch [IO.DirectoryNotFoundException]
	{
		return $null
	}
}

function Assert-VeroScriptComponentOutputPath([string]$Path, [string]$RepoRoot)
{
	$Repository = [IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/')
	$FullPath = if ([IO.Path]::IsPathRooted($Path)) {
		[IO.Path]::GetFullPath($Path)
	}
	else
	{
		[IO.Path]::GetFullPath((Join-Path $Repository $Path))
	}
	$Comparison = [StringComparison]::OrdinalIgnoreCase
	if ($FullPath.TrimEnd('\', '/').Equals([IO.Path]::GetPathRoot($FullPath).TrimEnd('\', '/'), $Comparison)) {
		throw "Refusing a filesystem root as component output: $FullPath"
	}
	$RepositoryPrefix = $Repository + [IO.Path]::DirectorySeparatorChar
	if ($FullPath.Equals($Repository, $Comparison)) {
		throw "Refusing repository source as component output: $FullPath"
	}
	if ($FullPath.StartsWith($RepositoryPrefix, $Comparison)) {
		$CanonicalRelease = Join-Path (Get-VeroScriptModuleRoot $Repository) 'Release'
		if ($FullPath.Equals($CanonicalRelease, $Comparison) -or $FullPath.StartsWith(($CanonicalRelease + [IO.Path]::DirectorySeparatorChar), $Comparison)) {
			throw "Publish managed Release output through the business publisher: $FullPath"
		}
		if ((Split-Path $FullPath -Parent).Equals($CanonicalRelease, $Comparison) -and
			(Split-Path $FullPath -Leaf) -match '^((win|linux|macos)-(x64|x86|arm64)|android|ios)$') {
			throw "Refusing a release platform collection root as component output: $FullPath"
		}
		$Allowed = $false
		foreach ($Directory in @($CanonicalRelease, (Join-Path $Repository 'dist'), (Join-Path $Repository '.tmp'))) {
			$Collection = [IO.Path]::GetFullPath($Directory).TrimEnd('\', '/')
			if ($FullPath.TrimEnd('\', '/').Equals($Collection, $Comparison)) {
				throw "Refusing a release collection root as component output: $FullPath"
			}
			if ($FullPath.StartsWith(($Collection + [IO.Path]::DirectorySeparatorChar), $Comparison)) {
				$Allowed = $true
			}
		}
		if (-not $Allowed) {
			throw "Refusing repository source as component output: $FullPath"
		}
	}
	# Dedicated external output directories remain supported. Check all existing
	# ancestors before any package is created or an old package is replaced.
	$Current = [IO.DirectoryInfo]::new($FullPath)
	while ($null -ne $Current) {
		if (Test-Path -LiteralPath $Current.FullName -PathType Leaf) {
			throw "Component output has a file ancestor: $($Current.FullName)"
		}
		$Attributes = Get-VeroScriptPathAttributes $Current.FullName
		if ($null -ne $Attributes -and ($Attributes -band [IO.FileAttributes]::ReparsePoint)) {
			throw "Linked component output path is forbidden: $($Current.FullName)"
		}
		if (Test-Path -LiteralPath (Join-Path $Current.FullName 'release-manifest.json') -PathType Leaf) {
			throw "Component output cannot modify a complete release: $($Current.FullName)"
		}
		if (Test-Path -LiteralPath $Current.FullName -PathType Container) {
			if (@(Get-ChildItem -LiteralPath $Current.FullName -File -Filter 'release-index*.json').Count) {
				throw "Component output cannot modify a release platform collection: $($Current.FullName)"
			}
			if (@(Get-ChildItem -LiteralPath $Current.FullName -File -Filter 'release-manifest-*.json').Count) {
				throw "Component output cannot directly modify a managed business release; use the default business publisher: $($Current.FullName)"
			}
		}
		$Current = $Current.Parent
	}
	# A link below the output can also redirect package replacement. Enumerate
	# explicitly so junctions are rejected before recursion can follow them.
	if (Test-Path -LiteralPath $FullPath -PathType Container) {
		$Pending = [Collections.Generic.Stack[IO.DirectoryInfo]]::new()
		$Pending.Push([IO.DirectoryInfo]::new($FullPath))
		while ($Pending.Count -gt 0) {
			foreach ($Entry in $Pending.Pop().EnumerateFileSystemInfos()) {
				if ($Entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
					throw "Linked component output entry is forbidden: $($Entry.FullName)"
				}
				if ($Entry.Attributes -band [IO.FileAttributes]::Directory) {
					$Pending.Push([IO.DirectoryInfo]$Entry)
				}
			}
		}
	}
}

function New-VeroScriptPortableArchive([string]$Source, [string]$Output, [string]$RepoRoot)
{
	Assert-VeroScriptComponentOutputPath (Split-Path -Parent $Output) $RepoRoot
	$Commands = @(Get-Command python3, python -All -CommandType Application -ErrorAction SilentlyContinue)
	$Python = $Commands | Where-Object { $_.Source -notmatch '[/\\]Microsoft[/\\]WindowsApps[/\\]' } | Select-Object -First 1
	if (-not $Python) { throw 'Python is required to create a canonical portable archive.' }
	& $Python.Source -c 'import sys; from pathlib import Path; sys.path.insert(0, sys.argv[1]); from veroscript_release import archive_tree; archive_tree(Path(sys.argv[2]), Path(sys.argv[3]))' $PSScriptRoot $Source $Output
	if ($LASTEXITCODE -ne 0) { throw 'Portable archive creation failed.' }
}

function Get-VeroScriptReleaseConfiguration([string]$RepoRoot, [string]$VersionsFile = '')
{
	if (-not $VersionsFile) {
		$VersionsFile = Join-Path (Get-VeroScriptModuleRoot $RepoRoot) 'Resources/Tools/Packaging/ReleaseVersions.json'
	}
	elseif (-not [IO.Path]::IsPathRooted($VersionsFile))
	{
		$VersionsFile = Join-Path $RepoRoot $VersionsFile
	}
	$Commands = @(Get-Command python3, python -All -CommandType Application -ErrorAction SilentlyContinue)
	$Python = $Commands | Where-Object { $_.Source -notmatch '[/\\]Microsoft[/\\]WindowsApps[/\\]' } | Select-Object -First 1
	if (-not $Python) { $Python = $Commands | Select-Object -First 1 }
	if (-not $Python) { throw 'Python 3.11+ is required for business release configuration.' }
	$Json = & $Python.Source (Join-Path $PSScriptRoot 'veroscript_business_release.py') configuration `
		--repo-root $RepoRoot --versions-file $VersionsFile
	if ($LASTEXITCODE -ne 0) { throw "Invalid release version configuration ($LASTEXITCODE)." }
	return ($Json | ConvertFrom-Json)
}

function Remove-VeroScriptAuxiliaryContent([string]$Directory)
{
	Assert-VeroScriptComponentOutputPath $Directory $RepoRoot
	& python (Join-Path $PSScriptRoot 'veroscript_products.py') clean --directory $Directory
	if ($LASTEXITCODE -ne 0) { throw "Functional package cleanup failed ($LASTEXITCODE)." }
}

function Invoke-VeroScriptPythonWheels([string[]]$Arguments)
{
	$Commands = @(Get-Command python3, python -All -CommandType Application -ErrorAction SilentlyContinue)
	$Coordinator = $Commands | Where-Object { $_.Source -notmatch '[/\\]Microsoft[/\\]WindowsApps[/\\]' } | Select-Object -First 1
	if (-not $Coordinator) { $Coordinator = $Commands | Select-Object -First 1 }
	if (-not $Coordinator) { throw 'Python 3.11 or newer is required for wheel builds.' }
	& $Coordinator.Source (Join-Path $PSScriptRoot 'build-veroscript-python-wheels.py') @Arguments
	if ($LASTEXITCODE -ne 0) { throw 'Python wheel matrix build or validation failed.' }
}

function Invoke-VeroScriptBusinessRelease([string[]]$Arguments, [string]$PythonExecutable = '')
{
	if ([string]::IsNullOrWhiteSpace($PythonExecutable)) {
		$Commands = @(Get-Command python3, python -All -CommandType Application -ErrorAction SilentlyContinue)
		$Python = $Commands | Where-Object { $_.Source -notmatch '[/\\]Microsoft[/\\]WindowsApps[/\\]' } | Select-Object -First 1
		if (-not $Python) { $Python = $Commands | Select-Object -First 1 }
		if (-not $Python) {
			throw 'Python 3.11+ is required for business release publication.'
		}
		$PythonExecutable = $Python.Source
	}
	& $PythonExecutable (Join-Path $PSScriptRoot 'veroscript_business_release.py') @Arguments
	if ($LASTEXITCODE -ne 0) {
		throw "Business release operation failed ($LASTEXITCODE)."
	}
}

function Invoke-VeroScriptInstallerDotNet([string]$RepoRoot, [string[]]$Arguments, [switch]$AllowFailure)
{
	# The CLI discovers global.json from its working directory, not the project path.
	$ProjectRoot = Join-Path (Get-VeroScriptModuleRoot $RepoRoot) 'VeroScriptInstaller'
	Push-Location -LiteralPath $ProjectRoot
	try {
		& dotnet @Arguments
		if ($LASTEXITCODE -ne 0 -and -not $AllowFailure) {
			throw "Installer .NET command failed ($LASTEXITCODE): dotnet $($Arguments[0])"
		}
	}
	finally
	{
		Pop-Location
	}
}

function Invoke-VeroScriptDefaultBusinessPackage([string]$ScriptPath, [System.Collections.IDictionary]$Parameters,
	[string]$RepoRoot, [string]$Business, [string]$Version, [string]$Target = 'win-x64')
{
	if ($Parameters.ContainsKey('OutRoot') -and -not [string]::IsNullOrWhiteSpace($Parameters['OutRoot'])) {
		return $false
	}
	$Stage = Join-Path $RepoRoot ('.tmp/business-component-' + [guid]::NewGuid().ToString('N'))
	Assert-VeroScriptComponentOutputPath $Stage $RepoRoot
	$Arguments = @{}
	foreach ($Key in $Parameters.Keys) {
		$Arguments[$Key] = $Parameters[$Key]
	}
	$Arguments.Version = $Version
	$Arguments.OutRoot = $Stage
	try {
		$Configuration = Get-VeroScriptReleaseConfiguration $RepoRoot
		$ModuleRoot = Get-VeroScriptModuleRoot $RepoRoot
		Invoke-VeroScriptBusinessRelease @('guard', '--repo-root', $RepoRoot, '--release-directory', (Join-Path $ModuleRoot 'Release')) | Out-Null
		$EntryName = [IO.Path]::GetFileName($ScriptPath)
		if ($EntryName -in @('package-veroscript-installer.ps1', 'package-veroscript-sdk-installer.ps1')) {
			$SourceParameter = if ($Business -eq 'CoreSDK') {
				'SdkPackageDir'
			}
			else
			{
				'RuntimePackageDir'
			}
			$Prefix = if ($Business -eq 'CoreSDK') {
				'VeroScriptSDK'
			}
			else
			{
				'VeroScript' + $Business
			}
			if (-not $Arguments[$SourceParameter]) {
				$Route = $Configuration.businessPaths.$Business
				$ArtifactCategory = if ($Business -eq 'CoreSDK')
				{
					'sdks'
				}
				else
				{
					'archives'
				}
				Invoke-VeroScriptBusinessRelease @('verify', '--repo-root', $RepoRoot, '--release-directory', (Join-Path $ModuleRoot 'Release'), '--target', $Target)
				$Archive = Join-Path $ModuleRoot "Release/$Route/$Target/$Version/$ArtifactCategory/$Prefix-$Version-$Target.zip"
				if (-not (Test-Path -LiteralPath $Archive -PathType Leaf)) {
					throw "Package the $Business archive first, or supply -${SourceParameter}: $Archive"
				}
				$Inputs = $Stage + '-inputs'
				Assert-VeroScriptComponentOutputPath $Inputs $RepoRoot
				New-Item -ItemType Directory -Path $Inputs -Force | Out-Null
				Expand-Archive -LiteralPath $Archive -DestinationPath $Inputs
				$Arguments[$SourceParameter] = Join-Path $Inputs "$Prefix-$Version-$Target"
			}
		}
		& $ScriptPath @Arguments
		Invoke-VeroScriptBusinessRelease @('publish-component', '--repo-root', $RepoRoot, '--candidate', $Stage,
			'--business', $Business, '--version', $Version, '--target', $Target)
	}
	finally
	{
		Assert-VeroScriptComponentOutputPath $Stage $RepoRoot
		if (Test-Path -LiteralPath $Stage) {
			Remove-Item -LiteralPath $Stage -Recurse -Force
		}
		if ($Inputs) {
			Assert-VeroScriptComponentOutputPath $Inputs $RepoRoot
			if (Test-Path -LiteralPath $Inputs) {
				Remove-Item -LiteralPath $Inputs -Recurse -Force
			}
		}
	}
	return $true
}
