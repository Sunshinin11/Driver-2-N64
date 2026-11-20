#!/usr/bin/env bash
# Ajusta a fila do RSP na libdragon e reinstala (rodar no WSL, uma vez):
#   bash tools/libdragon_rspq.sh [pasta do clone da libdragon]
# - RSPQ_DRAM_LOWPRI_BUFFER_SIZE 0x200 -> 0x1000 palavras (2 KB -> 16 KB por buffer; +28 KB de RAM)
# - RSPQ_DEBUG 1 -> 0 (tira as verificacoes de depuracao da fila, na CPU e no RSP)
# Medido no Ares (roteiro fixo de Chicago, mesmas janelas de CameraCnt): -6,1% de ticks por quadro.
# Sem clone no WSL: baixa o codigo da libdragon em ~/libdragon-src e escolhe a versao
# cujos headers batem com os instalados em $N64_INST (nao instala nada se nenhuma bater).
# Depois de reinstalar: recompilar o Tiny3D e o jogo do zero (build_d2.ps1 ja faz isso).
set -e
export N64_INST="${N64_INST:-/opt/libdragon}"
export PATH="$N64_INST/bin:$PATH"
INC="$N64_INST/mips64-elf/include"
REPO=https://github.com/DragonMinded/libdragon
# Versao usada nas medicoes (branch preview, 15/09/2026)
TESTADA=39d0d6096130836a65710da6730290701d060272

eh_clone() { [ -f "$1/include/rspq_constants.h" ] && [ -f "$1/src/rspq/rspq.c" ] && [ -f "$1/Makefile" ]; }

# Quantos headers do clone diferem dos instalados (ignora rspq_constants.h, que este script muda)
difs() {
    local n=0 f b
    for f in "$1"/include/*.h; do
        b=$(basename "$f")
        [ "$b" = rspq_constants.h ] && continue
        [ -f "$INC/$b" ] || continue
        igual "$f" "$INC/$b" || n=$((n+1))
    done
    echo $n
}
igual() { cmp -s "$1" "$2" || diff -q --strip-trailing-cr "$1" "$2" >/dev/null 2>&1; }
lista() {
    for f in "$1"/include/*.h; do b=$(basename "$f"); [ "$b" = rspq_constants.h ] && continue
        [ -f "$INC/$b" ] && ! igual "$f" "$INC/$b" && echo "  $b"; done | head -20
    echo "  (headers instalados: $(ls "$INC"/*.h | wc -l); no clone: $(ls "$1"/include/*.h | wc -l))"
}

[ -f "$INC/libdragon.h" ] || { echo "ERRO: libdragon instalada nao encontrada em $INC (N64_INST=$N64_INST)."; exit 1; }

LD="${1:-}"
if [ -n "$LD" ] && ! eh_clone "$LD"; then
    echo "Aviso: '$LD' nao e um clone da libdragon; procurando..."
    LD=""
fi
if [ -z "$LD" ]; then
    for c in $(find "$HOME" /opt /usr/local /srv -maxdepth 5 -type f -path '*/src/rspq/rspq.c' 2>/dev/null); do
        d="${c%/src/rspq/rspq.c}"
        [ "$d" = "$HOME/libdragon-src" ] && continue   # clone deste script: sempre passa pela busca de versao
        if eh_clone "$d"; then LD="$d"; break; fi
    done
fi

if [ -z "$LD" ]; then
    LD="$HOME/libdragon-src"
    command -v git >/dev/null || { echo "ERRO: git nao instalado no WSL (sudo apt install git)."; exit 1; }
    if [ ! -d "$LD/.git" ]; then
        echo "Baixando o codigo da libdragon em $LD ..."
        git clone --quiet -c core.autocrlf=false -c core.eol=lf "$REPO" "$LD"
    elif [ "$(git -C "$LD" config core.autocrlf || echo false)" != false ]; then
        # Clone antigo com CRLF (core.autocrlf global): regrava os arquivos com LF
        git -C "$LD" config core.autocrlf false; git -C "$LD" config core.eol lf
        git -C "$LD" ls-files -z | (cd "$LD" && xargs -0 rm -f); git -C "$LD" reset --quiet --hard
    fi
    git -C "$LD" fetch --quiet origin '+refs/heads/*:refs/remotes/origin/*'
    git -C "$LD" checkout --quiet -- include/rspq_constants.h 2>/dev/null || true
    # 1) A instalacao registra a versao em libdragon.version (branch e hash)
    VH=""
    if [ -f "$INC/libdragon.version" ]; then
        echo "Versao registrada na instalacao:"; cat "$INC/libdragon.version"; echo
        VH=$(grep -o '"hash"[^"]*"[0-9a-f]*"' "$INC/libdragon.version" | grep -o '[0-9a-f]\{40\}' || true)
    fi
    # 2) Candidatos: o hash registrado, a versao testada e o historico recente de cada branch
    CANDS="$VH $TESTADA"
    for b in preview unstable trunk; do
        git -C "$LD" rev-parse --verify --quiet "origin/$b" >/dev/null && CANDS="$CANDS $(git -C "$LD" rev-list --max-count=400 "origin/$b")"
    done
    achou=""; melhor=""; mdif=9999
    for c in $CANDS; do
        git -C "$LD" cat-file -e "$c^{commit}" 2>/dev/null || continue
        git -C "$LD" checkout --quiet --force "$c"
        n=$(difs "$LD")
        if [ "$n" = 0 ]; then achou=$c; break; fi
        if [ "$n" -lt "$mdif" ]; then mdif=$n; melhor=$c; fi
    done
    if [ -z "$achou" ]; then
        echo "ERRO: nenhuma versao da libdragon (preview/unstable/trunk) tem os mesmos headers da instalada em $N64_INST."
        if [ -n "$melhor" ]; then
            git -C "$LD" checkout --quiet --force "$melhor"
            echo "Mais parecida: $(git -C "$LD" log -1 --format='%h %cd %s' --date=short) ($mdif headers diferentes):"
            lista "$LD"
        fi
        echo "Nao instalei nada, para nao trocar a sua libdragon por outra versao."
        exit 1
    fi
    echo "Versao da libdragon igual a instalada: $(git -C "$LD" log -1 --format='%h %cd %s' --date=short)"
else
    echo "libdragon encontrada em: $LD"
    n=$(difs "$LD")
    if [ "$n" != 0 ]; then
        echo "ERRO: o clone em $LD tem $n headers diferentes da libdragon instalada em $N64_INST."
        lista "$LD"
        echo "Nao instalei nada, para nao trocar a sua libdragon por outra versao."
        exit 1
    fi
fi

H="$LD/include/rspq_constants.h"
sed -i 's/^#define RSPQ_DEBUG\([[:space:]]*\)1/#define RSPQ_DEBUG\10/' "$H"
sed -i 's/^#define RSPQ_DRAM_LOWPRI_BUFFER_SIZE\([[:space:]]*\)0x200\([[:space:]]\)/#define RSPQ_DRAM_LOWPRI_BUFFER_SIZE\10x1000\2/' "$H"
grep -E "^#define RSPQ_DEBUG|^#define RSPQ_DRAM_LOWPRI_BUFFER_SIZE" "$H"
grep -qE "^#define RSPQ_DEBUG[[:space:]]+0" "$H" && grep -qE "^#define RSPQ_DRAM_LOWPRI_BUFFER_SIZE[[:space:]]+0x1000" "$H" \
    || { echo "ERRO: nao consegui mudar rspq_constants.h."; exit 1; }

make -C "$LD" -j"$(nproc)" libdragon
if [ -w "$N64_INST/mips64-elf/lib" ] && [ -w "$INC" ]; then
    make -C "$LD" install
else
    echo "Instalar em $N64_INST precisa de sudo (vai pedir a senha do WSL):"
    sudo env "N64_INST=$N64_INST" "PATH=$PATH" make -C "$LD" install
fi
echo "libdragon reinstalada em $N64_INST com a fila maior do RSP."
