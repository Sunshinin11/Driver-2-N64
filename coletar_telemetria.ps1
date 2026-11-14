# coletar_telemetria.ps1 - roda a ROM no Ares e extrai as linhas de telemetria
param(
  [int]$Segundos = 120,
  [string]$Rom = "d2_n64.z64",
  [string]$Ares = ""
)
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Ares) {
  $cand = @("C:\Users\georg\Downloads\ares-v148\ares.exe") +
          (Get-ChildItem "C:\Users\georg\Downloads" -Recurse -Filter ares.exe -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
  $Ares = $cand | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Ares) { Write-Host "ERRO: ares.exe nao encontrado. Rode de novo com: -Ares 'C:\caminho\ares.exe'" -ForegroundColor Red; exit 1 }
$romPath = Join-Path $here $Rom
if (-not (Test-Path $romPath)) { Write-Host "ERRO: ROM nao encontrada: $romPath" -ForegroundColor Red; exit 1 }
Get-Process ares -ErrorAction SilentlyContinue | Stop-Process -Force
$log = Join-Path $here "telemetria.log"
$err = Join-Path $here "telemetria_stderr.log"
Write-Host "Ares: $Ares"
Write-Host "Rodando $romPath por $Segundos s (jogue o cenario que quer medir)..."
$p = Start-Process -FilePath $Ares -ArgumentList "`"$romPath`"" -RedirectStandardOutput $log -RedirectStandardError $err -PassThru
Start-Sleep -Seconds $Segundos
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }

$resumo = Join-Path $here "telemetria_resumo.txt"
Select-String -Path $log, $err -Pattern "\[frametime\]|\[heap\]|\[custo\]|\[cyc\]|\[fps\]|\[tex\]|TRAVADO|exception|bad_alloc|assert" |
  ForEach-Object { $_.Line } | Set-Content -Encoding utf8 $resumo

Write-Host ""
Write-Host "Pronto. Resumo: $resumo"
Get-Content $resumo | Select-Object -Last 15
