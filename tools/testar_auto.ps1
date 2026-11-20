# testar_auto.ps1 - compila uma variante com o roteiro automatico (free ride de Chicago, sem menus) e mede no Ares.
# Gera d2_n64_<Nome>.z64 e <Nome>.log; compara com -Base (log) se for dado. Nao precisa de ninguem no controle.
# Uso: powershell -File tools\testar_auto.ps1 -Nome l3a [-Extra '-DFOO=1'] [-Base auto_l2_a.log] [-Segundos 150]
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Nome,
    [string]$Extra = '',
    [string]$Base = '',
    [int]$Segundos = 150,
    [string]$MakeVars = '',
    [switch]$SemBuild
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$rom = "d2_n64_$Nome.z64"
if (-not $SemBuild) {
    $flags = "-DD2_TEST_MISSION=50 $Extra".Trim()
    $bargs = @('-ExecutionPolicy', 'Bypass', '-File', '.\build_d2.ps1', '-FramePace', '0', '-Bench', '-Extra', $flags)
    if ($MakeVars -ne '') { $bargs += @('-MakeVars', $MakeVars) }
    & powershell @bargs *> "build_$Nome.txt"
    if ($LASTEXITCODE -ne 0) { Select-String -Path "build_$Nome.txt" -Pattern 'error|undefined' | Select-Object -First 15 | ForEach-Object Line; throw "build falhou: build_$Nome.txt" }
    Copy-Item d2_n64.z64 $rom -Force
    Copy-Item build_d2\d2_n64.elf "d2_n64_$Nome.elf" -Force
}
powershell -ExecutionPolicy Bypass -File .\coletar_telemetria.ps1 -Segundos $Segundos -Rom $rom -NomeLog $Nome *> $null
$L = Get-Content "$Nome.log"
"TRAVADO=$(($L | Select-String 'TRAVADO').Count) excecoes=$(($L | Select-String 'exception|assert|bad_alloc' ).Count) ultimo_bench=$(($L | Select-String '^\[bench\]' | Select-Object -Last 1).Line)"
if ($Base -ne '') { powershell -ExecutionPolicy Bypass -File tools\comparar_bench.ps1 -A $Base -B "$Nome.log" }
