# Instala a dependencia LZ4 no WSL e compila a ROM atual.
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

Write-Host 'Instalando python3-lz4 no WSL. O sudo pode pedir sua senha do Linux.' -ForegroundColor Cyan
wsl bash -lc 'sudo apt-get update && sudo apt-get install -y python3-lz4 && python3 -c "import lz4.block"'
if ($LASTEXITCODE -ne 0) {
    throw 'Falha ao instalar ou verificar python3-lz4. O build nao foi iniciado.'
}

Write-Host 'Dependencia verificada. Compilando Driver 2...' -ForegroundColor Cyan
& (Join-Path $PSScriptRoot 'build_d2.ps1')
if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'd2_n64.z64'))) {
    throw 'A ROM nao foi gerada. Confira build_d2.log.'
}
