$ErrorActionPreference = 'Stop'
wsl --cd "$PSScriptRoot" bash -lc "sed 's/\r$//' build_ab_a30b79d.sh | bash"
if ($LASTEXITCODE -ne 0) { throw 'Build A/B falhou. Confira a saida e build_a30b79d.log, se gerado.' }
