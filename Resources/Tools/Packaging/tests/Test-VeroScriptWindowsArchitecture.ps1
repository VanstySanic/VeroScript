#Requires -Version 5.1
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

$ErrorActionPreference = 'Stop'
$PackagingRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$RepositoryRoot = (Resolve-Path (Join-Path $PackagingRoot '../../..')).Path
$Fixture = Join-Path $RepositoryRoot ('.tmp/veroscript-windows-architecture-' + [guid]::NewGuid().ToString('N'))

# Load only the production helpers, without starting a full release publication.
$ParseErrors = $null
$Ast = [System.Management.Automation.Language.Parser]::ParseFile(
	(Join-Path $PackagingRoot 'package-veroscript-all.ps1'), [ref]$null, [ref]$ParseErrors)
if ($ParseErrors.Count) {
	throw 'Cannot parse Windows release packager.'
}
foreach ($Name in @('Assert-NativeArchitecture', 'Write-CoreSdkCMakePackage', 'Get-RequiredCMakeBool', 'Invoke-Checked')) {
	$Function = $Ast.Find({ param($Node)
		$Node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $Node.Name -eq $Name
	}, $true)
	if ($null -eq $Function) {
		throw "Missing release helper: $Name"
	}
	. ([scriptblock]::Create($Function.Extent.Text.Replace('$PSScriptRoot', '$PackagingRoot')))
}

New-Item -ItemType Directory -Path $Fixture -Force | Out-Null
try {
	$CMakeCachePath = Join-Path $Fixture 'CMakeCache.txt'
	[IO.File]::WriteAllText($CMakeCachePath, "VEROSCRIPT_BUILD_APPLICATION:BOOL=OFF`n", [Text.UTF8Encoding]::new($false))
	$Versions = @{ CoreSDK = '1.0.0' }
	$Version = '1.0.0'
	$CoreBuildId = 'a' * 64
	$CoreHasAsmJit = 1
	$CoreJitTestHooks = 0
	$CMakeVersionWriter = Join-Path $PackagingRoot '../CMake/write-cmake-package-version.cmake'
	$ModuleRoot = (Resolve-Path (Join-Path $PackagingRoot '../../..')).Path
	$NativeCMake = Get-Content -LiteralPath (Join-Path $ModuleRoot 'Modules/Execution/VeroScriptAsmJit/CMakeLists.txt') -Raw
	$SelectionStart = $NativeCMake.IndexOf('set(_VeroScript_use_asmjit OFF)')
	$SelectionEnd = $NativeCMake.IndexOf('if(VEROSCRIPT_ENABLE_ASMJIT AND NOT')
	if ($SelectionStart -lt 0 -or $SelectionEnd -le $SelectionStart) {
		throw 'Cannot locate native backend selection for architecture regression.'
	}
	$Selection = $NativeCMake.Substring($SelectionStart, $SelectionEnd - $SelectionStart)
	$CSharpCMake = Get-Content -LiteralPath (Join-Path $ModuleRoot 'Extensions/Language/CSharp/VeroScriptCSharp/CMakeLists.txt') -Raw
	$CSharpStart = $CSharpCMake.IndexOf('set(_VeroScriptCSharp_run_arch_args "")')
	$CSharpEnd = $CSharpCMake.IndexOf('foreach(_VeroScriptCSharp_test_tfm IN ITEMS')
	if ($CSharpStart -lt 0 -or $CSharpEnd -le $CSharpStart) {
		throw 'Cannot locate C# test architecture selection.'
	}
	$CSharpSelection = $CSharpCMake.Substring($CSharpStart, $CSharpEnd - $CSharpStart)
	$Probe = @'
cmake_minimum_required(VERSION 3.25)
set(WIN32 TRUE)
set(APPLE FALSE)
set(CMAKE_CONFIGURATION_TYPES "")
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_HOST_SYSTEM_NAME Windows)
set(CMAKE_HOST_SYSTEM_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}")
@SELECTION@
if(NOT _VeroScript_asmjit_arch STREQUAL EXPECTED_BACKEND)
  message(FATAL_ERROR "Wrong backend for explicit target: ${_VeroScript_asmjit_arch}")
endif()
@CSHARP_SELECTION@
if(NOT _VeroScriptCSharp_native_aot_rid STREQUAL EXPECTED_AOT_RID)
  message(FATAL_ERROR "Wrong C# NativeAOT target: ${_VeroScriptCSharp_native_aot_rid}")
endif()
if(NOT _VeroScriptCSharp_dotnet_arch STREQUAL EXPECTED_GUI)
  message(FATAL_ERROR "Native binding requires a .NET test process with the same target architecture")
endif()
'@
	$Probe = $Probe.Replace('@SELECTION@', $Selection).Replace('@CSHARP_SELECTION@', $CSharpSelection)
	$ProbePath = Join-Path $Fixture 'target-selection.cmake'
	[IO.File]::WriteAllText($ProbePath, $Probe, [Text.UTF8Encoding]::new($false))
	foreach ($Case in @(
		@('x64', 'ARM64', 'ARM64', 'x64', 'x64', '8'),
		@('ARM64', 'AMD64', 'x64', 'a64', 'arm64', '8'),
		@('', 'ARM64', 'x64', 'x64', 'x64', '8'),
		@('', 'AMD64', 'ARM64', 'a64', 'arm64', '8'),
		@('Win32', 'ARM64', 'x64', 'x86', 'x86', '4'))) {
		$ExpectedAotRid = if ($Case[5] -eq '4') {
			''
		}
		else
		{
			'win-' + $Case[4]
		}
		Invoke-Checked 'cmake' @("-DCMAKE_GENERATOR_PLATFORM:STRING=$($Case[0])",
			"-DCMAKE_SYSTEM_PROCESSOR:STRING=$($Case[1])",
			"-DCMAKE_CXX_COMPILER_ARCHITECTURE_ID:STRING=$($Case[2])",
			"-DEXPECTED_BACKEND:STRING=$($Case[3])", "-DEXPECTED_GUI:STRING=$($Case[4])",
			"-DEXPECTED_AOT_RID:STRING=$ExpectedAotRid",
			"-DCMAKE_SIZEOF_VOID_P:STRING=$($Case[5])", '-P', $ProbePath)
	}
	foreach ($Architecture in @('x64', 'arm64')) {
		$Runtime = "win-$Architecture"
		$Sdk = Join-Path $Fixture $Runtime
		New-Item -ItemType Directory -Path (Join-Path $Sdk 'include/Script'), (Join-Path $Sdk 'include/Tooling') -Force | Out-Null
		$Pe = Join-Path $Sdk 'native.exe'
		$Bytes = [byte[]]::new(128)
		[BitConverter]::GetBytes([uint16]0x5A4D).CopyTo($Bytes, 0)
		[BitConverter]::GetBytes([uint32]64).CopyTo($Bytes, 60)
		[BitConverter]::GetBytes([uint32]0x00004550).CopyTo($Bytes, 64)
		$Machine = if ($Architecture -eq 'arm64') {
			0xAA64
		}
		else
		{
			0x8664
		}
		[BitConverter]::GetBytes([uint16]$Machine).CopyTo($Bytes, 68)
		[IO.File]::WriteAllBytes($Pe, $Bytes)
		Assert-NativeArchitecture $Pe
		$OtherMachine = if ($Architecture -eq 'arm64') {
			0x8664
		}
		else
		{
			0xAA64
		}
		[BitConverter]::GetBytes([uint16]$OtherMachine).CopyTo($Bytes, 68)
		[IO.File]::WriteAllBytes($Pe, $Bytes)
		$Rejected = $false
		try {
			Assert-NativeArchitecture $Pe
		}
		catch
		{
			$Rejected = $true
		}
		if (-not $Rejected) {
			throw "Accepted the wrong PE machine for $Runtime"
		}
		[IO.File]::WriteAllBytes($Pe, [byte[]]::new(2))
		$Rejected = $false
		try {
			Assert-NativeArchitecture $Pe
		}
		catch
		{
			$Rejected = $true
		}
		if (-not $Rejected) {
			throw 'Accepted a truncated PE image.'
		}

		Write-CoreSdkCMakePackage $Sdk
		$ExpectedX64 = [int]($Architecture -eq 'x64')
		$ExpectedArm64 = [int]($Architecture -eq 'arm64')
		$Consumer = Join-Path $Sdk 'consumer'
		New-Item -ItemType Directory -Path $Consumer -Force | Out-Null
		$Project = @'
cmake_minimum_required(VERSION 3.25)
project(WindowsSdkArchitecture LANGUAGES CXX)
find_package(VeroScript 1.0.0 EXACT CONFIG REQUIRED)
get_target_property(definitions VeroScript::VeroScript INTERFACE_COMPILE_DEFINITIONS)
if(definitions MATCHES "VEROSCRIPT_(HAS_ASMJIT|ASMJIT_|ENABLE_JIT_TEST_HOOKS)")
  message(FATAL_ERROR "Core SDK exports concrete backend definitions: ${definitions}")
endif()
'@
		$Project = $Project.Replace('@X64@', [string]$ExpectedX64).Replace('@ARM64@', [string]$ExpectedArm64)
		[IO.File]::WriteAllText((Join-Path $Consumer 'CMakeLists.txt'), $Project, [Text.UTF8Encoding]::new($false))
		Invoke-Checked 'cmake' @('-S', $Consumer, '-B', (Join-Path $Consumer 'build'), "-DCMAKE_PREFIX_PATH=$Sdk")
	}
	Write-Output 'PASSED: Windows JIT/C# target selection, x64/ARM64 PE rejection and external SDK architecture definitions'
}
finally
{
	$ResolvedFixture = [IO.Path]::GetFullPath($Fixture)
	$ExpectedPrefix = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot '.tmp')) + [IO.Path]::DirectorySeparatorChar
	if (-not $ResolvedFixture.StartsWith($ExpectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
		throw 'Unsafe architecture fixture cleanup path.'
	}
	Remove-Item -LiteralPath $ResolvedFixture -Recurse -Force
}
