#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
source_dir="$PWD"
for asset in Driver2CD1.bin Driver2CD2.bin; do
    test -f "$source_dir/data/$asset" || { echo "Falta data/$asset"; exit 1; }
done
python3 -c 'import lz4.block'
work_dir="$(mktemp -d /tmp/driver2-ab-completo.XXXXXX)"
echo "Fontes isolados: $work_dir"
python3 "$source_dir/tools/prepare_ab_a30b79d_completo.py" --out "$work_dir"
python3 "$source_dir/tools/instrument_ab_content.py" --out "$work_dir"
mkdir -p "$work_dir/data"
for asset in Driver2CD1.bin Driver2CD2.bin; do
    ln -s "$source_dir/data/$asset" "$work_dir/data/$asset"
done
t3d_commit="$(git ls-tree a30b79da425814c27c1a5405fdb670ac32d69d6c submodules/tiny3d | awk '{print $3}')"
git clone --no-checkout https://github.com/HailToDodongo/tiny3d.git "$work_dir/submodules/tiny3d"
git -C "$work_dir/submodules/tiny3d" checkout --detach "$t3d_commit"
cd "$work_dir"
bash setup_engine.sh
bash build.sh 2>&1 | tee "$source_dir/build_a30b79d_diagnostico.log"
# Nao publica um resultado que exceda o cartucho ou deixe conteudo faltando.
python3 - "$work_dir" <<'PY'
import pathlib,sys
p=pathlib.Path(sys.argv[1]); rom=p/'d2_n64.z64'
if not rom.is_file() or rom.stat().st_size>64*1024*1024:
    raise SystemExit('ROM ausente ou maior que 64 MiB; resultado nao publicado.')
fmv=list((p/'build_d2/fs/FMV64').glob('R??.m1v'))
if len(fmv)!=33: raise SystemExit(f'Esperados 33 videos; encontrados {len(fmv)}. Resultado nao publicado.')
import struct
pack=(p/'build_d2/fs/DRIVER2/LEVPACK.BIN').read_bytes()
h=struct.unpack_from('>9I',pack)
names=[pack[h[5]+i*60:h[5]+i*60+48].split(b'\0',1)[0].decode() for i in range(h[1])]
for city in ('CHICAGO','HAVANA','RIO','VEGAS'):
    if f'LEVELS/{city}.LEV' not in names: raise SystemExit('Cidade faltando: '+city)
print('Conteudo verificado: quatro cidades e 33 videos; ROM <=64 MiB.')
PY
cp d2_n64.z64 "$source_dir/d2_n64_a30b79d_diagnostico.z64"
cp AB_CONTENT_MANIFEST.json "$source_dir/build_a30b79d_diagnostico_manifest.json"
echo "ROM: $source_dir/d2_n64_a30b79d_diagnostico.z64"
sha256sum "$source_dir/d2_n64_a30b79d_diagnostico.z64"
