# comparar_bench.ps1 - compara duas telemetrias do roteiro (D2_BENCH_ROUTE) janela a janela pelo CameraCnt.
# Ignora a primeira janela (carga) e as janelas com foto (congelamento a cada 300 passos).
# Uso: powershell -File tools\comparar_bench.ps1 -A base.log -B novo.log [-Detalhe]
param([Parameter(Mandatory)][string]$A, [Parameter(Mandatory)][string]$B, [switch]$Detalhe)

function Read-Bench([string]$path) {
    $h = @{}
    Select-String -Path $path -Pattern '^\[bench\] cc=(\d+) ticks=(\d+) p95=(\d+) max=(\d+).*pos=([-\d]+),([-\d]+)' | ForEach-Object {
        $g = $_.Matches[0].Groups; $cc = [int]$g[1].Value
        if ($cc -gt 32 -and [math]::Floor($cc / 300) -eq [math]::Floor(($cc - 32) / 300)) {
            $h[$cc] = [pscustomobject]@{ cc = $cc; t = [double]$g[2].Value; p95 = [double]$g[3].Value; max = [double]$g[4].Value; pos = "$($g[5].Value),$($g[6].Value)" }
        }
    }
    $h
}
function Get-Hist([string]$path) {
    $h = @(0, 0, 0, 0, 0, 0, 0)
    Select-String -Path $path -Pattern '^\[frametime\].* h=([\d,]+)' | ForEach-Object {
        $v = $_.Matches[0].Groups[1].Value.Split(','); for ($k = 0; $k -lt 7; $k++) { $h[$k] += [int]$v[$k] } }
    $h
}
$ha = Read-Bench $A; $hb = Read-Bench $B
$common = $ha.Keys | Where-Object { $hb.ContainsKey($_) } | Sort-Object
if (-not $common) { Write-Host 'Nenhuma janela em comum.'; exit 1 }
$rows = foreach ($cc in $common) {
    [pscustomobject]@{ cc = $cc; A = $ha[$cc].t; B = $hb[$cc].t; pct = 100 * ($hb[$cc].t / $ha[$cc].t - 1)
        maxA = $ha[$cc].max; maxB = $hb[$cc].max; mesmaPos = $ha[$cc].pos -eq $hb[$cc].pos }
}
$mA = ($rows.A | Measure-Object -Average).Average; $mB = ($rows.B | Measure-Object -Average).Average
$sorted = $rows.pct | Sort-Object; $med = $sorted[[int]($sorted.Count / 2)]
"janelas=$($rows.Count) mesma_posicao=$(($rows | Where-Object mesmaPos).Count)/$($rows.Count)"
"A: ticks=$([int]$mA) fps=$([math]::Round(46875000 / $mA, 2)) maior_max=$([int]($rows.maxA | Measure-Object -Maximum).Maximum)"
"B: ticks=$([int]$mB) fps=$([math]::Round(46875000 / $mB, 2)) maior_max=$([int]($rows.maxB | Measure-Object -Maximum).Maximum)"
"diferenca media=$([math]::Round(100 * ($mB / $mA - 1), 2))%  mediana por janela=$([math]::Round($med, 2))%  min=$([math]::Round($sorted[0], 1))%  max=$([math]::Round($sorted[-1], 1))%"
"hist VB 1..7+ A: $((Get-Hist $A) -join ',')"
"hist VB 1..7+ B: $((Get-Hist $B) -join ',')"
if ($Detalhe) { $rows | Format-Table -AutoSize | Out-String -Width 160 }
