[CmdletBinding()]
param(
    [string]$Xmake = 'xmake',
    [string]$Target = 'SuperPeasantNative',
    [ValidateSet('releasedbg','debug')][string]$Mode = 'releasedbg'
)
$ErrorActionPreference = 'Stop'
$nativeRoot = Join-Path (Split-Path -Parent $PSScriptRoot) 'native'
Push-Location $nativeRoot
try {
    & $Xmake f -p windows -a x64 -m $Mode --cost_probe=n
    if ($LASTEXITCODE) { throw 'Native configuration failed' }
    & $Xmake build $Target
    if ($LASTEXITCODE) { throw "Native build failed: $Target" }
} finally { Pop-Location }
