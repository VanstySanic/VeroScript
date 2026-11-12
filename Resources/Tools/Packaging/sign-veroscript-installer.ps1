#Requires -Version 5.1
# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$Path,

	[string]$CertificateThumbprint = $env:VEROSCRIPT_SIGNING_CERTIFICATE_THUMBPRINT,

	[string]$TimestampServer = $env:VEROSCRIPT_TIMESTAMP_SERVER,

	[switch]$RequireSignature
)

$ErrorActionPreference = 'Stop'
$fullPath = (Get-Item -LiteralPath $Path -ErrorAction Stop).FullName
if ([string]::IsNullOrWhiteSpace($CertificateThumbprint)) {
	if ($RequireSignature) {
		throw 'A code-signing certificate thumbprint is required for this release.'
	}
	Write-Warning "Installer is unsigned because no code-signing certificate was configured: $fullPath"
	return
}
if ($RequireSignature -and [string]::IsNullOrWhiteSpace($TimestampServer)) {
	throw 'A timestamp server is required when signed installers are mandatory.'
}

$normalizedThumbprint = ($CertificateThumbprint -replace '[^0-9A-Fa-f]', '').ToUpperInvariant()
$certificate = @(
	Get-ChildItem -LiteralPath 'Cert:\CurrentUser\My' -ErrorAction SilentlyContinue
	Get-ChildItem -LiteralPath 'Cert:\LocalMachine\My' -ErrorAction SilentlyContinue
) | Where-Object {
	$_.Thumbprint -eq $normalizedThumbprint -and $_.HasPrivateKey
} | Select-Object -First 1
if (-not $certificate) {
	throw "Code-signing certificate with a private key was not found: $normalizedThumbprint"
}
$codeSigningOid = '1.3.6.1.5.5.7.3.3'
if (-not @($certificate.EnhancedKeyUsageList | Where-Object { $_.ObjectId.Value -eq $codeSigningOid })) {
	throw "Certificate is not valid for code signing: $normalizedThumbprint"
}

$arguments = @{
	FilePath = $fullPath
	Certificate = $certificate
	HashAlgorithm = 'SHA256'
}
if (-not [string]::IsNullOrWhiteSpace($TimestampServer)) {
	$arguments.TimestampServer = $TimestampServer
}
$signature = Set-AuthenticodeSignature @arguments
if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
	throw "Installer signature verification failed for '$fullPath': $($signature.StatusMessage)"
}
Write-Host "Signed installer: $fullPath" -ForegroundColor Green
