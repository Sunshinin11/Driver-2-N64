# Driver 2 for Nintendo 64 (Port / Tech Demo)

Port do jogo **Driver 2** (PlayStation 1) para o **Nintendo 64**, baseado na engenharia reversa [REDRIVER2](https://github.com/OpenDriver2/REDRIVER2) e no SDK [libdragon](https://github.com/DragonMinded/libdragon).

O código do jogo original roda na CPU VR4300 do N64 (requer **Expansion Pak / 8 MB de RAM**) sobre uma camada de compatibilidade de PS1 ([PsyCross](https://github.com/OpenDriver2/PsyCross) adaptado), renderizador RDP nativo, sistema de arquivos em cartucho (DragonFS) e stubs/implementações de hardware.

> **Repositório Privado**: Este repositório é privado e inclui os arquivos de código-fonte, scripts de build/harness, ferramentas de medição e a imagem de disco CD1 via Git LFS para viabilizar compilação e testes autônomos em ambientes na nuvem (Codex Cloud).

---

## Estrutura do Projeto

| Caminho | Descrição |
|---|---|
| `port/n64/` | Camada de plataforma do N64: renderizador RDP direto (`psyx_rdp.cpp`), entrada (`psyx_backend.cpp`), stubs/runtime, cache de vértices (`d2_vtxcache.cpp`) e GTE nativa (`gte_native.cpp`). |
| `port/compat/` | Cabeçalhos de compatibilidade (endianness, wrappers de tipos PSX/SDL, `d2_gte_inline.h`). |
| `port/redriver2/` | Código do motor descompactado e modificado (gerado por `setup_engine.sh`, ignorado no Git). |
| `tools/` | Utilitários de build: extrator de dados de CD (`extract_cd.py`), patches do motor (`patch_engine.py`), ordenação de funções no icache (`hot.py`). |
| `tools/ares_headless/` | Harness em C++ para executar a ROM no emulador ares sem interface gráfica, injetar botões e coletar métricas. |
| `tools/nuvem/` | Scripts de medição e benchmark automatizados (`bench.sh`, `cc.sh`, `mk.sh`, `run.sh`, `pgo.sh`, `layout.sh`). |
| `data/` | Imagem do disco original (`Driver2CD1.bin`) rastreada via **Git LFS**. |
| `Makefile.d2`, `build.sh` | Sistema de compilação libdragon / N64. |
| `setup_engine.sh`, `patch_engine.sh` | Scripts de obtenção e aplicação de patches no motor upstream. |
| `hot.ld` | Script de linker que posiciona funções críticas juntas nos 16 KB de cache de instruções do VR4300. |

---

## Pré-requisitos de Build

1. **Sistema Operacional**: Linux (Ubuntu 22.04+ / 24.04+) ou Windows através de WSL (Ubuntu).
2. **Toolchain MIPS64**: GCC 14+ / 16+ para `mips64-elf`, normalmente instalado em `/opt/libdragon`.
3. **libdragon**: branch `preview` (instalado com `make install tools-install`).
4. **Git e Git LFS**: para clonagem e download dos arquivos binários grandes.
5. **Python 3** (com bibliotecas padrão e opcionalmente Pillow/PIL para converter capturas PPM -> PNG).
6. **Mesa / Vulkan (opcional para ares headless)**: drivers Vulkan de software (`mesa-vulkan-drivers`) para execução sem GPU física.

---

## Obtenção dos Arquivos Grandes (Git LFS)

A imagem do jogo utilizada para extrair os dados das fases (`data/Driver2CD1.bin`, ~617 MB) é versionada usando o **Git LFS**.

Após clonar o repositório, certifique-se de baixar o conteúdo real do disco:

```bash
git lfs install
git lfs pull
```

Verifique se `data/Driver2CD1.bin` possui cerca de 647.268.048 bytes (e não apenas o ponteiro de texto do LFS).

> **Por que apenas o CD1?** O script `tools/extract_cd.py` e o `Makefile.d2` utilizam unicamente o CD1. As quatro cidades do jogo (Chicago, Havana, Rio, Vegas) estão presentes de forma idêntica tanto no CD1 quanto no CD2. O CD2 difere essencialmente nas cenas de vídeo (FMV), que não são incluídas no cartucho de N64. Manter apenas o CD1 economiza espaço e preserva a cota gratuita do Git LFS (~617 MB < 1 GB).

---

## Preparação do Motor (`setup_engine.sh`)

O código do REDRIVER2 e do PsyCross não é versionado diretamente na pasta `port/redriver2/`. O script `setup_engine.sh` faz o download dos commits fixados e aplica as alterações necessárias de forma idempotente:

- **REDRIVER2 upstream**: commit `b2d8857426f815007486def9bd23f9a6007a2c64`
- **PsyCross upstream**: commit `e56e4cde1c2b8a15e0d4e38b26cdd9202e0d17e6`

Para preparar o motor:

```bash
bash setup_engine.sh
```

Para forçar uma reconstrução limpa do motor a partir do zero:

```bash
bash setup_engine.sh --force
```

O script automaticamente baixa os fontes, copia os módulos necessários, extrai as strings de idioma (`data/DRIVER2/LANG`) e executa `patch_engine.sh` (que chama `tools/patch_engine.py` para aplicar conversões de endianness, ajustes de alocação de memória e ganchos de renderização).

---

## Compilação

### Build no Linux / WSL

Compilação completa / incremental gerando `d2_n64.z64`:

```bash
bash build.sh
```

Limpeza dos artefatos de compilação:

```bash
bash build.sh clean
```

*(Nota: `clean` não apaga `filesystem_d2`, que contém os dados extraídos do CD).*

### Build no Windows via PowerShell

```powershell
.\build_d2.ps1          # build incremental
.\build_d2.ps1 -Clean   # limpeza e build completo
```

### Variáveis e Opções do Makefile (`Makefile.d2`)

- `N64_INST`: caminho de instalação do libdragon (padrão: `/opt/libdragon`).
- `CD1`: caminho da imagem do CD1 (padrão busca em `data/Driver2CD1.bin`).
- `CITIES`: cidades embutidas na ROM (padrão: `CHICAGO`; opções: `CHICAGO,HAVANA,RIO,VEGAS`).
- `D2_OPT`: sinalizadores de otimização ativos (padrão: `-DD2_DIRECT -DD2_NOSUBQ -DD2_GTE_NATIVE -DD2_VTXCACHE -DD2_LEANLOAD -DD2_FASTOT`).
- `HOT_LD`: arquivo de ordenação de seções para icache (padrão: `hot.ld`).

---

## Execução no ares com o Harness Headless

Para testar sem interface gráfica e coletar medições precisas:

1. Compile o harness em `tools/ares_headless/harness.cpp` contra o núcleo `n64` do emulador ares (veja seção de configuração abaixo).
2. Execute a ROM através do script:

```bash
bash run.sh [quadros] [a_cada_N_quadros] [entradas.txt]
```

Exemplo:
```bash
# Executa por 900 quadros, salvando captura a cada 60 quadros
bash run.sh 900 60
```

As variáveis de ambiente configuráveis são:
- `HARNESS`: caminho do executável `harness` (padrão: `/opt/harness/harness`).
- `OUTDIR`: pasta onde capturas PPM/PNG e logs são salvos (padrão: `/opt/run1`).
- `VK_ICD_FILENAMES`: driver Vulkan (padrão: `/usr/share/vulkan/icd.d/lvp_icd.json`).

---

## Método de Medição por CameraCnt

### Por que NÃO medir por quadros de emulador ou VBL?
O motor de Driver 2 roda a lógica do jogo desacoplada ou travada em etapas discretas. Em emulação, contar meramente os quadros gerados por segundo no emulador pode enganar, pois uma versão mais rápida avança a lógica do jogo mais depressa e renderiza cenas com quantidade diferente de polígonos.

### Métrica Oficial: ticks por passo de lógica (`CameraCnt`)
O port emite a cada 32 passos da câmera uma linha de benchmark no log do emulador (ISViewer):

```text
[bench] cc=128 ticks=4456120 tris=1420 tex=48
```

- `cc`: valor de `CameraCnt` (passo exato da lógica da câmera e da simulação).
- `ticks`: contagem de ciclos do contador do processador MIPS VR4300 consumidos naquele intervalo (46.875.000 ticks = 1 segundo exato no hardware N64).
- Comparando o mesmo intervalo de `CameraCnt` (ex.: `CC0=96` até `CC1=320` no modo Praticar em Chicago) com o mesmo roteiro de botões (`in_bench.txt`), obtém-se uma comparação determinística e à prova de divergências.

Para resumir os logs de benchmark de duas ou mais ROMs:
```bash
bash tools/nuvem/cc.sh versao_a versao_b
```

---

## Localização de Logs e Capturas

- **Log de execução do emulador**: `$OUTDIR/run.log` (ou `/tmp/run.log`).
- **Log do build**: `build_d2.log` (ou `/tmp/build.log`).
- **Capturas de tela**: `$OUTDIR/frame_XXXXX.ppm`, folha de contatos `$OUTDIR/sheet.png` e imagem final `$OUTDIR/final.png`.
- **Perfil do processador**: `/tmp/prof.txt` (quando executado com `D2_PROF=1`).

---

## Configuração do Ambiente na Nuvem (Codex Cloud / Ubuntu 22.04+)

Para reproduzir o ambiente de desenvolvimento completo em uma nova máquina virtual:

```bash
# 1. Dependências do sistema e renderização Vulkan de software
sudo apt-get update && sudo apt-get install -y \
  build-essential git git-lfs make python3 python3-pil \
  cmake ninja-build \
  mesa-vulkan-drivers libvulkan-dev libgl-dev libglx-dev \
  libx11-dev libxrandr-dev libgtk-3-dev libsdl2-dev

# 2. Toolchain GCC MIPS64 do libdragon
# Baixe a versão correspondente do gcc-toolchain do libdragon e extraia em /opt/libdragon
sudo mkdir -p /opt/libdragon
# (Alternativamente, instale via o pacote .deb da release do libdragon)

# 3. Compilação e instalação do libdragon (branch preview)
git clone -b preview https://github.com/DragonMinded/libdragon.git /tmp/libdragon
cd /tmp/libdragon
make install tools-install N64_INST=/opt/libdragon
cd / && rm -rf /tmp/libdragon

# 4. Compilação do ares (núcleo n64 headless)
git clone --depth 1 https://github.com/ares-emulator/ares.git /tmp/ares
cd /tmp/ares
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DARES_CORES=n64 -DARES_SKIP_DEPS=ON
ninja -C build ares mia nall libco

# 5. Compilação do harness
A=/tmp/ares
B=$A/build
c++ -O2 -std=c++20 -fno-char8_t -fwrapv -fno-strict-aliasing -DCORE_N64 -DMIA_LIBRARY -DPROFILE_PERFORMANCE -DVULKAN \
  -DBUILD_RELEASE -DARES_ENABLE_CHD -I$B/mia -I$A/mia -I$B/ares -I$A/ares -I$A -isystem $A/nall -I$A/thirdparty \
  -I$A/thirdparty/libchdr/include -I$B/thirdparty/libchdr/include -I$A/thirdparty/lzma -I$A/libco/.. \
  tools/ares_headless/harness.cpp -o /opt/harness/harness -flto=auto -Wl,--start-group \
  $B/mia/libmia.a $B/ares/libares.a $B/nall/nall/CMakeFiles/nall.dir/*.o \
  $B/libco/liblibco.a $B/thirdparty/chdr-static.a $B/thirdparty/libchdr/deps/zstd-*/zstd.a \
  $B/thirdparty/libchdr/deps/miniz-*/miniz.a $B/thirdparty/sljit.a $B/thirdparty/tzxfile.a -Wl,--end-group -lpthread -ldl -lX11 -lz
```

