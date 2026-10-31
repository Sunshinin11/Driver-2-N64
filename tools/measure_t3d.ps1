param(
    [Parameter(Mandatory=$true)][string]$Rom,
    [Parameter(Mandatory=$true)][string]$Name,
    [int]$Seconds = 100
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$aresRoot = 'C:\Users\georg\Downloads\ares-v148'
$romPath = (Resolve-Path -LiteralPath $Rom).Path
$outDir = Join-Path $taskRoot 'logs'
$settingsPath = Join-Path $outDir ($Name + '.bml')
$settingsText = Get-Content -LiteralPath (Join-Path $aresRoot 'settings.bml') -Raw
# Keep the user's configuration untouched; block physical input while unfocused.
$settingsText = $settingsText.Replace('Defocus: Pause', 'Defocus: Block')
[IO.File]::WriteAllText($settingsPath, $settingsText)
$outPath = Join-Path $outDir ($Name + '.log')
$errPath = Join-Path $outDir ($Name + '_stderr.log')
$argsText = '--settings-file "' + $settingsPath + '" "' + $romPath + '"'
$testProcess = Start-Process -FilePath (Join-Path $aresRoot 'ares.exe') -ArgumentList $argsText -WindowStyle Hidden -PassThru -RedirectStandardOutput $outPath -RedirectStandardError $errPath
try {
    Write-Output "Benchmark $Name started (PID $($testProcess.Id))."
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    while ([DateTime]::UtcNow -lt $deadline -and !$testProcess.WaitForExit(1000)) {}
} finally {
    if (!$testProcess.HasExited) { $testProcess.Kill(); $testProcess.WaitForExit() }
}
Write-Output "Benchmark saved: $outPath"
