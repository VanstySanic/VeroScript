# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

function Install-VeroScriptVmComponent([string]$BuildRoot, [string]$Destination, [string]$Component, [string]$Config)
{
	& cmake --install $BuildRoot --config $Config --prefix $Destination --component $Component
	if ($LASTEXITCODE -ne 0) { throw "VM component staging failed: $Component ($LASTEXITCODE)" }
}

function Copy-VeroScriptVmRuntime([string]$BuildRoot, [string]$Destination, [string]$Config = 'Release')
{
	Install-VeroScriptVmComponent $BuildRoot $Destination 'VeroScriptRuntimeVm' $Config
	$Libraries = @(Get-ChildItem -LiteralPath (Join-Path $Destination 'Vm') -File -ErrorAction SilentlyContinue |
		Where-Object { $_.Name -like '*VeroScriptVmBytecode*' -and $_.Extension -in @('.dll', '.so', '.dylib') })
	if ($Libraries.Count -ne 1) { throw 'Runtime staging requires exactly one official VM library' }
}

function Copy-VeroScriptVmSdk([string]$BuildRoot, [string]$Destination, [string]$Config = 'Release')
{
	Install-VeroScriptVmComponent $BuildRoot $Destination 'VeroScriptExecutionContract' $Config
	Install-VeroScriptVmComponent $BuildRoot $Destination 'VeroScriptBytecodeVmSdk' $Config
	Copy-VeroScriptVmRuntime $BuildRoot (Join-Path $Destination 'bin') $Config
}
