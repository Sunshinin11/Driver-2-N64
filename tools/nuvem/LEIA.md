# Ferramentas usadas na nuvem (Linux) para medir o port no ares

Caminhos padrão utilizados:
- Projeto: `$D2_ROOT` (padrão: `$D2_DIR/D2_N64`)
- Diretório de saída/ROMs: `$D2_DIR` (padrão: `/opt/d2`)
- Harness executável: `$HARNESS` (padrão: `/opt/harness/harness`)
- Diretório libdragon: `$N64_INST` (padrão: `/opt/libdragon`)

Todos os scripts em `tools/nuvem/` e na raiz respeitam essas variáveis de ambiente (`D2_DIR`, `HARNESS`, `N64_INST`, `OUTDIR`). Para alterar em outro ambiente (como WSL ou Codex Cloud), basta definir essas variáveis no shell (ex.: `export D2_DIR=/meu/path`).

## Montar o ambiente (Ubuntu)
1. Toolchain: pacote `gcc-toolchain-mips64-x86_64.deb` (release toolchain-continuous-prerelease do libdragon), extraido em /opt/libdragon.
2. libdragon preview: `git clone -b preview https://github.com/DragonMinded/libdragon && make install tools-install` (N64_INST=/opt/libdragon).
3. Pacotes: `mesa-vulkan-drivers libvulkan-dev libgl-dev libglx-dev libx11-dev libxrandr-dev libgtk-3-dev libsdl2-dev`.
4. ares (nucleo n64, sem interface):
   ```
   git clone --depth 1 https://github.com/ares-emulator/ares.git && cd ares
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DARES_CORES=n64 -DARES_SKIP_DEPS=ON
   ninja -C build ares mia nall libco
   ```
5. Harness (tools/ares_headless/harness.cpp), com A=pasta do ares e B=$A/build:
   ```
   c++ -O2 -std=c++20 -fno-char8_t -fwrapv -fno-strict-aliasing -DCORE_N64 -DMIA_LIBRARY -DPROFILE_PERFORMANCE -DVULKAN \
     -DBUILD_RELEASE -DARES_ENABLE_CHD -I$B/mia -I$A/mia -I$B/ares -I$A/ares -I$A -isystem $A/nall -I$A/thirdparty \
     -I$A/thirdparty/libchdr/include -I$B/thirdparty/libchdr/include -I$A/thirdparty/lzma -I$A/libco/.. \
     harness.cpp -o harness -flto=auto -Wl,--start-group $B/mia/libmia.a $B/ares/libares.a $B/nall/nall/CMakeFiles/nall.dir/*.o \
     $B/libco/liblibco.a $B/thirdparty/chdr-static.a $B/thirdparty/libchdr/deps/zstd-*/zstd.a $B/thirdparty/libchdr/deps/miniz-*/miniz.a \
     $B/thirdparty/sljit.a $B/thirdparty/tzxfile.a -Wl,--end-group -lpthread -ldl -lX11 -lz
   ```
   Rodar com `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json D2_VULKAN=1`.

## Fluxo de medicao
- `mk.sh nome "flags"` -> /opt/d2/nome.z64 (+ .elf/.map). As flags vao em D2_EXTRA (as otimizacoes padrao ja vem de D2_OPT).
- `bench.sh a b` (2 em paralelo, BF=quadros) e depois `cc.sh a b`: media de ticks/quadro no mesmo trecho do jogo (CameraCnt CC0..CC1).
- `pgo.sh nome "flags"`: perfil -> hot.ld desta versao -> rebuild (nomep.z64). `layout.sh`: varios orcamentos de hot.ld.
- Perfil por funcao: rodar com D2_PROF=1 D2_PROF_FROM=2100 e `python3 tools/profsum.py <map>`.

## Regras de medicao (fase 2 do roteiro de otimizacao)
- Compare sempre pelas janelas `[bench] cc=` (CameraCnt, mesmo estado do jogo), nunca por "ultimos N quadros": `bench.sh` + `cc.sh`, mesma entrada (`in_bench.txt`), mesma cena.
- O Ares do harness e determinista: repetir a mesma ROM da exatamente o mesmo numero. A incerteza vem de mudar o layout do codigo (hot.ld, tamanho das funcoes), que desloca o resultado por alguns por cento; so aceite ganho do quadro inteiro acima de 3% ou confirmado por timer de regiao.
- Log do jogador x harness: as janelas iniciais (cc 32..96, mesma rua parada) dao 3,4M x 3,3M ticks (+2%); as diferencas maiores aparecem so depois, quando o jogador dirige para outra area com mais transito. Nao compare cenas diferentes.
- Em medidas de nova versao, rode tambem a versao anterior no mesmo instante; nao reutilize numeros de outra sessao.

## libdragon: fila RSPQ mais funda (referencia: Half-Life N64 port)
`tools/libdragon-rspq-buffer.patch` sobe RSPQ_DRAM_LOWPRI_BUFFER_SIZE de 0x200 para 0x1000 palavras (2 KB -> 16 KB por buffer, +32 KB de RAM),
para a CPU preparar mais comandos enquanto RSP/RDP desenham. Aplicar no checkout do libdragon (`git apply`), `make libdragon && make install`, e recompilar o jogo.
