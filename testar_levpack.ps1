# Executa UMA etapa de teste. Rode primeiro -Blocos 1, depois -Blocos 2.
# Substitui a ROM atual; conserva logs separados para cada configuracao.
param(
    [ValidateSet(1, 2, 3, 4)][int]$Blocos = 1,
    [ValidateRange(30, 1800)][int]$Segundos = 180,
    [string]$Ares = ''
)
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
& (Join-Path $PSScriptRoot 'build_d2.ps1') -LpkCacheBlocks $Blocos -LpkProfile
$rom = Join-Path $PSScriptRoot 'd2_n64.z64'
$prefix = "telemetria_lpk$Blocos"
$flags = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'build_d2/.d2_flags') -Raw
if ($flags -notmatch "(?:^|\s)-DD2_LPK_CACHE_BLOCKS=$Blocos(?:\s|$)" -or
    $flags -notmatch '(?:^|\s)-DD2_LPK_PROFILE=1(?:\s|$)') {
    throw 'Flags do build nao correspondem ao teste. Coleta cancelada.'
}
$metadata = Join-Path $PSScriptRoot "${prefix}_build.log"
@(
    "Data: $([DateTimeOffset]::Now.ToString('o'))"
    "ROM SHA256: $((Get-FileHash -LiteralPath $rom -Algorithm SHA256).Hash)"
    "ROM bytes: $((Get-Item -LiteralPath $rom).Length)"
    "Cache: $Blocos blocos; perfil: 1"
    "Flags: $flags"
) | Set-Content -LiteralPath $metadata -Encoding utf8
Get-Content -LiteralPath (Join-Path $PSScriptRoot 'build_d2.log') | Add-Content -LiteralPath $metadata -Encoding utf8
Write-Host "Teste com $Blocos bloco(s). Faca o mesmo percurso; Ares fecha apos $Segundos segundos." -ForegroundColor Cyan
& (Join-Path $PSScriptRoot 'coletar_telemetria.ps1') -Segundos $Segundos -Ares $Ares -NomeLog $prefix
