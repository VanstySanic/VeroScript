# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

function Copy-VeroScriptPackageClient([string]$Repository, [string]$Cli, [string]$Root, [string]$Role)
{
	if ($Role -notin @('runtime', 'core-sdk')) { throw "Unsupported package-client component role: $Role" }
	$SavedErrorActionPreference = $ErrorActionPreference
	try {
		# Windows PowerShell turns redirected native stderr into error records.
		# Capture optional-capability failures before applying our exit policy.
		$ErrorActionPreference = 'Continue'
		$Raw = (& $Cli env --json "--path=$Root" 2>&1 | Out-String)
		$CliExitCode = $LASTEXITCODE
	}
	finally
	{
		$ErrorActionPreference = $SavedErrorActionPreference
	}
	if ($CliExitCode -ne 0) {
		if ($Raw -match 'package management was not built') { return }
		throw "Cannot validate package client: $Raw"
	}
	$Target = ($Raw | ConvertFrom-Json).target
	$Descriptor = @{ format = 'veroscript-component'; formatVersion = '0.1.1'; role = $Role; root = $(if ($Role -eq 'core-sdk') { '..' }
	else
	{
		'.'
	}); target = $Target }
	[IO.File]::WriteAllText((Join-Path (Split-Path $Cli -Parent) 'veroscript-component.json'), ($Descriptor | ConvertTo-Json -Depth 12), (New-Object Text.UTF8Encoding $false))
	$Verified = (& $Cli env --root --path $Root --json 2>&1 | Out-String)
	if ($LASTEXITCODE -ne 0) { throw "Cannot validate package component root: $Verified" }
	if (($Verified | ConvertFrom-Json).scope -ne $(if ($Role -eq 'core-sdk') { 'sdk' }
	else
	{
		'runtime'
	})) {
		throw 'Package-client descriptor does not select its release component'
	}
	foreach ($Relative in @('json/LICENSE.MIT','miniz/LICENSE','sha256/LICENSE','curl/COPYING','sources.json')) {
		$Destination = Join-Path $Root "licenses/package-manager/$Relative"
		New-Item -ItemType Directory -Path (Split-Path $Destination -Parent) -Force | Out-Null
		Copy-Item -LiteralPath (Join-Path $Repository "VeroScriptPackageManager/ThirdParty/$Relative") -Destination $Destination
	}
}
function Copy-VeroScriptPackageManagerSdk([string]$Build, [string]$Stage, [string]$Configuration, [string]$CoreVersion)
{
	& python (Join-Path $PSScriptRoot 'veroscript_applications.py') stage-sdk --build $Build --directory $Stage --config $Configuration
	if ($LASTEXITCODE -ne 0) { throw 'Application SDK installation failed' }
	if (-not (Select-String -LiteralPath (Join-Path $Build 'CMakeCache.txt') -Pattern '^VEROSCRIPT_BUILD_PACKAGE_MANAGER:(BOOL|INTERNAL)=(1|ON|TRUE|YES|Y)\s*$' -Quiet)) {
		& python (Join-Path $PSScriptRoot 'veroscript_applications.py') finalize-sdk --directory $Stage --core-version $CoreVersion
		if ($LASTEXITCODE -ne 0) { throw 'Application SDK identity finalization failed' }
		return
	}
	& cmake --install $Build --prefix $Stage --config $Configuration --component VeroScriptPackageManager
	if ($LASTEXITCODE -ne 0) { throw 'Package manager SDK installation failed' }
	& python (Join-Path $PSScriptRoot 'veroscript_packages.py') finalize-sdk --directory $Stage --version $CoreVersion
	if ($LASTEXITCODE -ne 0) { throw 'Package manager SDK identity finalization failed' }
}

function Copy-VeroScriptApplicationRuntime([string]$Repository, [string]$Binaries, [string]$Stage, [string]$ExecutableDirectory = '')
{
	& python (Join-Path $PSScriptRoot 'veroscript_applications.py') stage-runtime --repo $Repository --binaries $Binaries --directory $Stage "--executable-directory=$ExecutableDirectory"
	if ($LASTEXITCODE -ne 0) { throw 'Application Runtime staging failed' }
}
