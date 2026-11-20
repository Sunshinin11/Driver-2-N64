# Build do Driver 2 no WSL. Substitui apenas d2_n64.z64.
# Padrao: um bloco LEVPACK, perfil desligado.
param(
    [switch]$Clean,
    [ValidateSet(1, 2, 3, 4)][int]$LpkCacheBlocks = 1,
    [switch]$LpkProfile,
    # Ritmo de apresentacao: 3 = minimo de 3 VBlanks, 20 FPS travado (padrao); 0 = livre
    [ValidateSet(0, 1, 2, 3, 4, 5)][int]$FramePace = 3,
    # Aplica a fila maior do RSP na libdragon, reinstala e recompila tudo do zero.
    # -Libdragon e opcional: sem ele o script procura o clone sozinho no WSL.
    [switch]$AtualizarLibdragon,
    [string]$Libdragon = '',
    # Roteiro de medicao: no jogo o carro dirige sozinho, preso ao CameraCnt (mesmo trajeto em qualquer build)
    [switch]$Bench,
    # Flags extras para o compilador (D2_EXTRA), ex.: '-DD2_TEST_MISSION=50' (entra direto no free ride de Chicago)
    [string]$Extra = '',
    # Variaveis extras do Makefile, ex.: 'D2_T3D_WORLD=1'
    [string]$MakeVars = ''
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if ($AtualizarLibdragon -or $Libdragon -ne '') {
    wsl --cd "$here" bash -lc "bash tools/libdragon_rspq.sh $Libdragon"
    if ($LASTEXITCODE -ne 0) { throw 'Falha ao atualizar a libdragon no WSL.' }
    wsl --cd "$here" bash -lc 'rm -rf submodules/tiny3d/build'
    $Clean = $true
}
if ($Clean) {
    wsl --cd "$here" bash -lc 'bash build.sh clean'
    if ($LASTEXITCODE -ne 0) { throw 'Falha ao limpar o build no WSL.' }
}
$profileValue = if ($LpkProfile) { 1 } else { 0 }
$flags = @()
if ($Bench) { $flags += '-DD2_BENCH_ROUTE' }
if ($Extra -ne '') { $flags += $Extra }
$extra = if ($flags.Count) { "D2_EXTRA='" + ($flags -join ' ') + "'" } else { "" }
Write-Host "LEVPACK: blocos=$LpkCacheBlocks perfil=$profileValue ritmo=$FramePace roteiro=$([int]$Bench.IsPresent)" -ForegroundColor Cyan
wsl --cd "$here" bash -lc "set -o pipefail; bash build.sh LPK_CACHE_BLOCKS=$LpkCacheBlocks LPK_PROFILE=$profileValue FRAME_PACE=$FramePace $MakeVars $extra 2>&1 | tee build_d2.log"
if ($LASTEXITCODE -ne 0) { throw 'BUILD FALHOU. Confira a saida e build_d2.log, se atualizado.' }
$romPath = Join-Path $here 'd2_n64.z64'
if (-not (Test-Path -LiteralPath $romPath)) { throw 'Build terminou sem gerar d2_n64.z64.' }
Write-Host "`nBUILD OK: $romPath" -ForegroundColor Green
