#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
source_dir="$PWD"
commit="$(git rev-parse a30b79d^{commit})"
work_dir="$(mktemp -d /tmp/driver2-ab-a30b79d.XXXXXX)"
echo "Build isolado: $work_dir"
echo "Commit: $commit"
git archive "$commit" | tar -x -C "$work_dir"
mkdir -p "$work_dir/data"
for asset in Driver2CD1.bin Driver2CD2.bin; do
    if [ -f "$source_dir/data/$asset" ]; then ln -s "$source_dir/data/$asset" "$work_dir/data/$asset"; fi
done
t3d_commit="$(git ls-tree "$commit" submodules/tiny3d | awk '{print $3}')"
git clone --no-checkout https://github.com/HailToDodongo/tiny3d.git "$work_dir/submodules/tiny3d"
git -C "$work_dir/submodules/tiny3d" checkout --detach "$t3d_commit"
cd "$work_dir"
bash setup_engine.sh
set -o pipefail
bash build.sh 2>&1 | tee "$source_dir/build_a30b79d.log"
test -s d2_n64.z64
cp d2_n64.z64 "$source_dir/d2_n64_a30b79d.z64"
echo "ROM gerada: $source_dir/d2_n64_a30b79d.z64"
sha256sum "$source_dir/d2_n64_a30b79d.z64"
echo "Fontes temporarias preservadas em: $work_dir"
