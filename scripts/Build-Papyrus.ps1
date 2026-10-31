[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Caprica,
    [Parameter(Mandatory=$true)][string]$VanillaSources,
    [Parameter(Mandatory=$true)][string]$F4seSources,
    [string]$OutDir = '',
    [string]$RunnerSource = '',
    [string]$Python = '',
    [switch]$DryRun
)
# Offline only: explicit canonical sources, separate runtime/test payloads and assembly verification.
# No game files, saves, install journals, process control, downloads or cleanup are performed.
$ErrorActionPreference = 'Stop'
if (-not $OutDir) { $OutDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'artifacts\papyrus' }
$OutDir = [IO.Path]::GetFullPath($OutDir)
$caprica = [IO.Path]::GetFullPath($Caprica)
$vanilla = [IO.Path]::GetFullPath($VanillaSources)
$f4se = [IO.Path]::GetFullPath($F4seSources)
$clobber = Join-Path $PSScriptRoot 'pas_clobber.py'
$nativeRoot = Join-Path (Split-Path -Parent $PSScriptRoot) 'native'
$runtimeSrc = Join-Path $nativeRoot 'papyrus_scripts'
$nativeSrc = Join-Path $nativeRoot 'papyrus'
foreach ($required in @($caprica, (Join-Path $vanilla 'Institute_Papyrus_Flags.flg'), $clobber)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "missing offline tool/input: $required" }
}
if (-not $Python) { $Python = (Get-Command python -ErrorAction Stop).Source }
$sources = @((Join-Path $nativeSrc 'SuperPeasantNative.psc')) + @(
    Get-ChildItem -LiteralPath (Join-Path $runtimeSrc 'SuperPeasant') -Filter '*.psc' -File | Sort-Object Name | ForEach-Object { $_.FullName }
)
if ($RunnerSource) {
    $RunnerSource = [IO.Path]::GetFullPath($RunnerSource)
    if (-not (Test-Path -LiteralPath $RunnerSource -PathType Leaf)) { throw "Runner source missing: $RunnerSource" }
    $sources += Join-Path $runtimeSrc 'SuperPeasant\Test\Golden.psc'
    $sources += $RunnerSource
}
$manifest = @()
$payloads = @()
foreach ($source in $sources) {
    $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    $declaration = [regex]::Match([IO.File]::ReadAllText($source), '(?im)^Scriptname\s+([\w:]+)')
    if (-not $declaration.Success) { throw "Scriptname missing: $source" }
    $pexRelative = ($declaration.Groups[1].Value -replace ':', '\') + '.pex'
    $isTest = $source -eq $RunnerSource -or $source -like '*\Test\Golden.psc'
    $kind = if ($isTest) { 'test' } else { 'runtime' }
    $pexOut = Join-Path $OutDir "$kind\Scripts"
    $asmOut = Join-Path $OutDir "$kind\asm"
    # Vanilla must precede F4SE: F4SE's ObjectReference contains a member Caprica rejects.
    $common = @('-g', 'fallout4', '--ignorecwd', '--strict=on', '-f', (Join-Path $vanilla 'Institute_Papyrus_Flags.flg'),
        '-i', $vanilla, '-i', $nativeSrc, '-i', $runtimeSrc, '-i', $f4se)
    if ($source -eq $RunnerSource) { $common += @('-i', (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $RunnerSource)))) }
    foreach ($pass in @('pex', 'asm')) {
        $target = if ($pass -eq 'asm') { $asmOut } else { $pexOut }
        $argv = $common + @('-o', $target)
        if ($pass -eq 'asm') { $argv += '--dump-asm' }
        $argv += $source
        if ($DryRun) { Write-Output "$pass $source -> $target"; continue }
        [void][IO.Directory]::CreateDirectory($target)
        $oldPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try { $lines = @(& $caprica @argv 2>&1 | ForEach-Object { "$_" }); $code = $LASTEXITCODE }
        finally { $ErrorActionPreference = $oldPreference }
        $log = Join-Path $OutDir ("{0}-{1}-{2}.txt" -f $kind, [IO.Path]::GetFileNameWithoutExtension($source), $pass)
        [IO.File]::WriteAllLines($log, [string[]](@("source=$source", "argv=$($argv -join ' ')", "exit=$code") + $lines))
        $bad = @($lines | Where-Object { $_ -match '(?i)\b(error|warning)\b' -and $_ -notmatch '(?i)\b0 (errors?|warnings?)\b' })
        if ($code -ne 0 -or $bad.Count -gt 0) { throw "Caprica $pass failed or warned: $log" }
        if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $sourceHash) { throw "source changed during compilation: $source" }
    }
    if (-not $DryRun) {
        $expectedPex = Join-Path $pexOut $pexRelative
        if (-not (Test-Path -LiteralPath $expectedPex -PathType Leaf)) { throw "expected compiled PEX missing: $expectedPex" }
        $payloads += [ordered]@{ path = $expectedPex; sha256 = (Get-FileHash -LiteralPath $expectedPex -Algorithm SHA256).Hash }
    }
    $manifest += [ordered]@{ source = $source; sha256 = $sourceHash; payload = $kind }
}
if ($DryRun) { Write-Output 'DRYRUN: no output generated'; exit 0 }
foreach ($kind in @('runtime', 'test')) {
    $asm = Join-Path $OutDir "$kind\asm"
    if (-not (Test-Path -LiteralPath $asm)) { continue }
    $lines = @(& $Python -B -I $clobber $asm 2>&1 | ForEach-Object { "$_" })
    $code = $LASTEXITCODE
    $lines | Set-Content -LiteralPath (Join-Path $OutDir "$kind-clobber.txt") -Encoding UTF8
    if ($code -ne 0 -or $lines -notcontains 'CLOBBERS=0') { throw "assembly clobber check failed: $kind" }
}
@{ sources = $manifest; pex = $payloads } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutDir 'manifest.json') -Encoding UTF8
Write-Output "RESULT offline_papyrus=PASS sources=$($sources.Count) pex=$($payloads.Count) runtime=$OutDir\runtime\Scripts"
