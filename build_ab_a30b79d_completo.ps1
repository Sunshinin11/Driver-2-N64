$ErrorActionPreference = 'Stop'
wsl --cd "$PSScriptRoot" bash -lc "sed 's/\r$//' build_ab_a30b79d_completo.sh | bash"
if ($LASTEXITCODE -ne 0) { throw 'Build A/B completo falhou; nao considere uma ROM anterior como resultado deste build.' }
