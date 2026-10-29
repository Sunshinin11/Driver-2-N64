# Build do Driver 2 N64 no WSL (mostra o progresso ao vivo e grava build_d2.log).
#   .\build_d2.ps1           -> compila (incremental)
#   .\build_d2.ps1 -Clean    -> limpa tudo e compila do zero (necessario quando mudam as opcoes de compilacao)
param([switch]$Clean)
$here = $PSScriptRoot
if ($Clean) { wsl --cd "$here" bash -lc "bash build.sh clean" }
wsl --cd "$here" bash -lc "bash build.sh 2>&1 | tee build_d2.log"
if (Test-Path "$here\d2_n64.z64") { Write-Host "`nBUILD OK: $here\d2_n64.z64" -ForegroundColor Green } else { Write-Host "`nBUILD FALHOU (veja build_d2.log)" -ForegroundColor Red }
