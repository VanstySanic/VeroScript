# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

function Get-VeroScriptJitIdentity([string]$VersionOutput)
{
	if ($VersionOutput -notmatch '; jit-backend=([A-Za-z0-9_.-]+); jit-target=([^;\] ]+); native=(on|off)\]') {
		throw "Runtime does not report its JIT module identity: $VersionOutput"
	}
	return [ordered]@{ backend = $Matches[1]; target = $Matches[2]; nativeCode = ($Matches[3] -eq 'on') }
}

function Invoke-VeroScriptIcons([string[]]$Arguments, [string]$PythonExecutable = '')
{
	if ([string]::IsNullOrWhiteSpace($PythonExecutable)) {
		$Commands = @(Get-Command python3, python -All -CommandType Application -ErrorAction SilentlyContinue)
		$Python = $Commands | Where-Object { $_.Source -notmatch '[/\\]Microsoft[/\\]WindowsApps[/\\]' } | Select-Object -First 1
		if (-not $Python) { $Python = $Commands | Select-Object -First 1 }
		if ($Python) { $PythonExecutable = $Python.Source }
	}
	if ([string]::IsNullOrWhiteSpace($PythonExecutable)) { throw 'Python 3.11+ is required for icon staging and verification.' }
	& $PythonExecutable (Join-Path $PSScriptRoot 'veroscript_icons.py') @Arguments
	if ($LASTEXITCODE -ne 0) { throw "VeroScript icon operation failed ($LASTEXITCODE)." }
}

function Copy-VeroScriptRuntimeIcons([string]$RepoRoot, [string]$Destination, [string]$Target)
{
	Invoke-VeroScriptIcons @('stage-runtime', '--repo-root', $RepoRoot, '--root', $Destination, '--target', $Target)
}

function Assert-VeroScriptJitBuildIdentity($Identity, [int]$HasBackendModule, [int]$NativeEnabled)
{
	if ($Identity.backend -ne 'none' -or $Identity.nativeCode)
	{
		throw 'The default runtime must use the interpreter without an explicit JIT backend.'
	}
}

function Copy-VeroScriptLocalization([string]$RepoRoot, [string]$Destination, [string]$Profile = 'runtime')
{
	& cmake "-DVEROSCRIPT_SOURCE_ROOT=$RepoRoot" `
		"-DVEROSCRIPT_MODULE_ROOT=$([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..')))" `
		"-DVEROSCRIPT_LOCALIZATION_OUTPUT=$Destination" `
		"-DVEROSCRIPT_LOCALIZATION_PROFILE=$Profile" `
		-P (Join-Path $PSScriptRoot '../CMake/VeroScriptLocalization.cmake')
	if ($LASTEXITCODE -ne 0) {
		throw "VeroScript localization assembly failed ($LASTEXITCODE)."
	}
}

function Invoke-VeroScriptDocs([string[]]$Arguments, [string]$PythonExecutable = '')
{
	if ([string]::IsNullOrWhiteSpace($PythonExecutable)) {
		$Commands = @(Get-Command python3, python -All -CommandType Application -ErrorAction SilentlyContinue)
		$Python = $Commands | Where-Object { $_.Source -notmatch '[/\\]Microsoft[/\\]WindowsApps[/\\]' } | Select-Object -First 1
		if (-not $Python) {
			$Python = $Commands | Select-Object -First 1
		}
		if ($Python) {
			$PythonExecutable = $Python.Source
		}
	}
	if ([string]::IsNullOrWhiteSpace($PythonExecutable)) {
		throw 'Python 3.11+ is required for module documentation assembly.'
	}
	& $PythonExecutable -c 'import sys; sys.exit(0 if sys.version_info >= (3, 11) else 1)'
	if ($LASTEXITCODE -ne 0) {
		throw 'Python 3.11+ is required for module documentation assembly.'
	}
	& $PythonExecutable (Join-Path $PSScriptRoot 'veroscript_docs.py') @Arguments
	if ($LASTEXITCODE -ne 0) {
		throw "VeroScript documentation operation failed ($LASTEXITCODE)."
	}
}

# Source scripts belong to the core module. Release assets remain adjacent.
function Get-VeroScriptPortableAssetBytes([string]$Path)
{
	$Bytes = [IO.File]::ReadAllBytes($Path)
	if ([IO.Path]::GetExtension($Path) -ieq '.md') {
		$Encoded = Invoke-VeroScriptDocs @('asset-file', '--source', $Path)
		return ,([Convert]::FromBase64String($Encoded))
	}
	return ,$Bytes
}

function ConvertTo-VeroScriptPortableAssetTree([string]$Root, [string]$SourceRoot = '')
{
	if (-not $SourceRoot) {
		$SourceRoot = $Root
	}
	Invoke-VeroScriptDocs @('portable', '--source', $SourceRoot, '--output', $Root)
}

function Resolve-VeroScriptUserDocsRoot([string]$Path)
{
	$Candidate = (Get-Item -LiteralPath $Path -ErrorAction Stop).FullName
	if (-not (Test-Path -LiteralPath $Candidate -PathType Container)) {
		throw "Docs package directory not found: $Candidate"
	}
	$Children = @(Get-ChildItem -LiteralPath $Candidate -Force)
	if ($Children.Count -eq 1 -and $Children[0].PSIsContainer -and $Children[0].Name -eq 'docs') {
		$Candidate = $Children[0].FullName
	}
	# Selecting only User/ would omit shared Tutorials and leave escaping links.
	return (Invoke-VeroScriptDocs @('select-user', '--source', $Candidate))
}

function Get-VeroScriptPortableAssetHash([string]$Path)
{
	$Hasher = [Security.Cryptography.SHA256]::Create()
	try {
		return [BitConverter]::ToString($Hasher.ComputeHash((Get-VeroScriptPortableAssetBytes $Path))).Replace('-', '')
	}
	finally
	{
		$Hasher.Dispose()
	}
}

function ConvertTo-VeroScriptSdkAssetTree([string]$Root, [switch]$Examples)
{
	$Arguments = @('sdk', '--output', $Root)
	if ($Examples) {
		$Arguments += '--examples'
	}
	Invoke-VeroScriptDocs $Arguments
}

function Copy-VeroScriptWindowsRuntime([string]$BuildRoot, [string]$Destination, [string]$Config = 'Release')
{
	if ($Config -ne 'Release') {
		throw 'Redistributable Windows packages require Release binaries.'
	}
	& cmake --install $BuildRoot --config $Config --prefix $Destination --component VeroScriptWindowsRuntime
	if ($LASTEXITCODE -ne 0) { throw 'MSVC runtime staging failed.' }
	$Required = @('msvcp140.dll', 'vcruntime140.dll', 'licenses/MicrosoftVisualCpp/NOTICE.txt')
	foreach ($Name in $Required) {
		if (-not (Test-Path -LiteralPath (Join-Path $Destination $Name) -PathType Leaf)) {
			throw "Missing Windows deployment dependency '$Name'. Reconfigure the MSVC build with its redistributable libraries installed."
		}
	}
	$Stream = [IO.File]::OpenRead((Join-Path $Destination 'msvcp140.dll'))
	$Reader = [IO.BinaryReader]::new($Stream)
	try {
		$Stream.Position = 0x3C
		$Stream.Position = $Reader.ReadUInt32() + 4
		$Machine = $Reader.ReadUInt16()
	}
	finally
	{
		$Reader.Dispose()
	}
	if ($Machine -eq 0x8664 -and -not (Test-Path -LiteralPath (Join-Path $Destination 'vcruntime140_1.dll') -PathType Leaf)) {
		throw 'Missing x64 MSVC runtime vcruntime140_1.dll.'
	}
}
