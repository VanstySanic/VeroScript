# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

[CmdletBinding()]
param(
	[ValidateSet('Debug', 'Release')]
	[string]$Configuration = 'Release',
	[string]$BuildDirectory = 'build/webassembly'
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$moduleRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$resolvedBuildDirectory = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
	[System.IO.Path]::GetFullPath($BuildDirectory)
}
else
{
	[System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDirectory))
}

function Import-BatchEnvironment([string]$BatchFile) {
	$commandLine = 'call "{0}" >nul && set' -f $BatchFile.Replace('"', '""')
	$startInfo = New-Object System.Diagnostics.ProcessStartInfo
	$startInfo.FileName = $env:ComSpec
	$startInfo.Arguments = "/d /c `"$commandLine`""
	$startInfo.UseShellExecute = $false
	$startInfo.RedirectStandardOutput = $true
	$startInfo.RedirectStandardError = $true
	$startInfo.CreateNoWindow = $true

	$process = [System.Diagnostics.Process]::Start($startInfo)
	$standardOutput = $process.StandardOutput.ReadToEnd()
	$errorText = $process.StandardError.ReadToEnd()
	$process.WaitForExit()
	$environmentLines = $standardOutput -split "[`r`n]+"
	$exitCode = $process.ExitCode
	if ($exitCode -ne 0) {
		if ($errorText) { Write-Host $errorText }
		throw "Failed to activate the Emscripten SDK environment: $BatchFile"
	}
	foreach ($line in $environmentLines) {
		$separator = $line.IndexOf('=')
		if ($separator -le 0) { continue }
		$name = $line.Substring(0, $separator)
		$value = $line.Substring($separator + 1)
		[Environment]::SetEnvironmentVariable($name, $value, 'Process')
	}
}

$emcmake = Get-Command emcmake, emcmake.bat -ErrorAction SilentlyContinue |
	Select-Object -First 1
if (-not $emcmake) {
	$emsdkEnvironmentCandidates = @()
	if ($env:EMSDK) {
		$emsdkEnvironmentCandidates += Join-Path $env:EMSDK 'emsdk_env.bat'
	}
	$emsdkEnvironmentCandidates += Join-Path $repoRoot '.tmp\emsdk\emsdk_env.bat'
	$emsdkEnvironment = $emsdkEnvironmentCandidates |
		Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
		Select-Object -First 1
	if ($emsdkEnvironment) {
		Import-BatchEnvironment $emsdkEnvironment
		$emcmake = Get-Command emcmake, emcmake.bat -ErrorAction SilentlyContinue |
			Select-Object -First 1
	}
}
if (-not $emcmake) {
	throw 'emcmake was not found. Install/activate Emscripten or place an SDK under .tmp/emsdk.'
}

# Let the active Emscripten configuration select its pinned Binaryen toolchain.
# An explicit EM_BINARYEN_ROOT remains supported, but a repository-local
# standalone Binaryen must not silently override an incompatible SDK version.
$visualStudioToolRoots = @()
if (${env:ProgramFiles(x86)}) {
	$vsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
	if (Test-Path -LiteralPath $vsWhere -PathType Leaf) {
		$visualStudioToolRoots = @(& $vsWhere -all -products '*' -property installationPath)
	}
}

$ninja = Get-Command ninja, ninja.exe -ErrorAction SilentlyContinue |
	Select-Object -First 1
if ($ninja) {
	$ninjaPath = $ninja.Source
}
else
{
	$localNinja = Join-Path $repoRoot '.tmp\ninja\ninja.exe'
	if (Test-Path -LiteralPath $localNinja -PathType Leaf) {
		$ninjaPath = $localNinja
	}
}
if (-not $ninjaPath) {
	foreach ($visualStudioRoot in $visualStudioToolRoots) {
		$candidate = Join-Path $visualStudioRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
		if (Test-Path -LiteralPath $candidate -PathType Leaf) {
			$ninjaPath = $candidate
			break
		}
	}
}
if (-not $ninjaPath -and $env:ProgramFiles) {
	$visualStudioNinja = Get-ChildItem `
		-Path "$env:ProgramFiles\Microsoft Visual Studio" `
		-Filter ninja.exe `
		-Recurse `
		-ErrorAction SilentlyContinue |
		Where-Object { $_.FullName -like '*CommonExtensions*Microsoft*CMake*Ninja*' } |
		Select-Object -First 1
	if ($visualStudioNinja) {
		$ninjaPath = $visualStudioNinja.FullName
	}
}
if (-not $ninjaPath) {
	throw 'ninja was not found. Install Ninja, place ninja.exe under .tmp/ninja, or use a Visual Studio installation that includes CMake tools.'
}

$cmake = Get-Command cmake, cmake.exe -ErrorAction SilentlyContinue |
	Select-Object -First 1
$cmakePath = $null
if ($cmake) {
	$cmakePath = $cmake.Source
}
if (-not $cmakePath) {
	foreach ($visualStudioRoot in $visualStudioToolRoots) {
		$candidate = Join-Path $visualStudioRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
		if (Test-Path -LiteralPath $candidate -PathType Leaf) {
			$cmakePath = $candidate
			break
		}
	}
}
if (-not $cmakePath) {
	$bundledCmakeRoot = Split-Path -Parent (Split-Path -Parent $ninjaPath)
	$candidate = Join-Path $bundledCmakeRoot 'CMake\bin\cmake.exe'
	if (Test-Path -LiteralPath $candidate -PathType Leaf) {
		$cmakePath = $candidate
	}
}
if (-not $cmakePath) {
	throw 'cmake was not found. Install CMake or use a Visual Studio installation that includes CMake tools.'
}

& $emcmake.Source $cmakePath `
	-S $repoRoot `
	-B $resolvedBuildDirectory `
	-G Ninja `
	"-DCMAKE_MAKE_PROGRAM=$ninjaPath" `
	"-DCMAKE_BUILD_TYPE=$Configuration" `
	"-DVEROSCRIPT_MODULE_ROOT=$moduleRoot" `
	-DBUILD_TESTING=OFF `
	-DVS_ENABLE_IPO=ON `
	-DVS_BUILD_VEROSCRIPT_WEBASSEMBLY_ONLY=ON `
	-DVEROSCRIPT_BUILD_VM_BACKEND=ON `
	-DVEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF `
	-DVEROSCRIPT_ENABLE_ASMJIT=OFF
if ($LASTEXITCODE -ne 0) {
	throw "Emscripten CMake configuration failed with exit code $LASTEXITCODE."
}

& $cmakePath --build $resolvedBuildDirectory --target VeroScriptWebAssemblySdkSources
if ($LASTEXITCODE -ne 0) {
	throw "VeroScript WebAssembly build failed with exit code $LASTEXITCODE."
}

Write-Host "VeroScript WebAssembly SDK: $resolvedBuildDirectory\bin"
