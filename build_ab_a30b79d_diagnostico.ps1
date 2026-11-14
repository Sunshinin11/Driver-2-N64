$ErrorActionPreference = 'Stop'
wsl --cd "$PSScriptRoot" bash -lc "sed 's/\r$//' build_ab_a30b79d_diagnostico.sh | bash"
if ($LASTEXITCODE -ne 0) { throw 'Build de diagnostico falhou. Confira a saida e build_a30b79d_diagnostico.log.' }
