# Build do Driver 2 no WSL. Substitui apenas d2_n64.z64.
# Padrao: um bloco LEVPACK, perfil desligado.
param(
    [switch]$Clean,
    [ValidateSet(1, 2, 3, 4)][int]$LpkCacheBlocks = 1,
    [switch]$LpkProfile
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if ($Clean) {
    wsl --cd "$here" bash -lc 'bash build.sh clean'
    if ($LASTEXITCODE -ne 0) { throw 'Falha ao limpar o build no WSL.' }
}
$profileValue = if ($LpkProfile) { 1 } else { 0 }
Write-Host "LEVPACK: blocos=$LpkCacheBlocks perfil=$profileValue" -ForegroundColor Cyan
wsl --cd "$here" bash -lc "set -o pipefail; bash build.sh LPK_CACHE_BLOCKS=$LpkCacheBlocks LPK_PROFILE=$profileValue 2>&1 | tee build_d2.log"
if ($LASTEXITCODE -ne 0) { throw 'BUILD FALHOU. Confira a saida e build_d2.log, se atualizado.' }
$romPath = Join-Path $here 'd2_n64.z64'
if (-not (Test-Path -LiteralPath $romPath)) { throw 'Build terminou sem gerar d2_n64.z64.' }
Write-Host "`nBUILD OK: $romPath" -ForegroundColor Green
