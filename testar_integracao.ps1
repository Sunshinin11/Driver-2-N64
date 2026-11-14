param([ValidateRange(30,1800)][int]$Segundos = 180)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build_ab_a30b79d_diagnostico.ps1')
& (Join-Path $PSScriptRoot 'coletar_telemetria.ps1') -Segundos $Segundos -Rom 'd2_n64_a30b79d_diagnostico.z64' -NomeLog 'telemetria_a30b79d_diagnostico'
