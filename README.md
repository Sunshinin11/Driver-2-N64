# Driver 2 — Nintendo 64 Port

[🇧🇷 Português](#-driver-2-para-nintendo-64) | [🇺🇸 English](#-driver-2-for-nintendo-64)

---

# 🇧🇷 Driver 2 para Nintendo 64

Port nativo de **Driver 2: The Wheelman Is Back** (PlayStation 1, 2000) para o console **Nintendo 64**, construído a partir da descompilação e engenharia reversa do projeto de código aberto [REDRIVER2](https://github.com/OpenDriver2/REDRIVER2) e utilizando o SDK moderno [libdragon](https://github.com/DragonMinded/libdragon).

O código original em C/C++ executa diretamente no processador MIPS VR4300 do N64 sem emulação de CPU, empregando transformações vetoriais nativas, renderização direta no RDP com o motor [Tiny3D](https://github.com/sp1180/tiny3d), áudio comprimido em VADPCM/WAV64 e armazenamento de ativos no sistema de arquivos de cartucho DragonFS.

> ⚠️ **Aviso Legal e Imagens de Disco**: Este repositório **NÃO contém dados proprietários, código binário original, modelos, texturas ou músicas protegidos por direitos autorais**. Para compilar a ROM jogável, o usuário deve fornecer uma cópia legítima do disco original do PlayStation 1 (*Driver 2 — CD1*).

---

## Índice

- [Visão Geral e Estado Atual](#visão-geral-e-estado-atual)
- [Arquitetura Técnica](#arquitetura-técnica)
- [Requisitos de Sistema](#requisitos-de-sistema)
- [Obtenção e Preparação dos Discos](#obtenção-e-preparação-dos-discos)
- [Instruções de Compilação](#instruções-de-compilação)
- [Como Jogar e Mapeamento de Controles](#como-jogar-e-mapeamento-de-controles)
- [Emulação e Hardware Real](#emulação-e-hardware-real)
- [Galeria](#galeria)
- [Créditos e Licenças](#créditos-e-licenças)

---

## Visão Geral e Estado Atual

### O Que Funciona
- **Lógica e Física do Jogo**: Simulação completa dos veículos, tráfego urbano, inteligência artificial da polícia e colisões 3D fielmente adaptadas.
- **Exploração e Modo Praticar**: Navegação livre pela cidade de Chicago com renderização de edifícios, malha viária, cruzamentos e sinalização.
- **Renderizador Híbrido RDP / Tiny3D**:
  - **Edifícios e Cenário Urbano**: Processados e rasterizados via microcódigo RSP do **Tiny3D** (`d2_t3d_world.cpp`), com transformações matriciais em hardware e intercalação na Ordering Table (OT) do PS1.
  - **Malha Viária e Pistas**: Renderizadas diretamente no RDP com correção de perspectiva de quads (`draw_ground_perspective`), eliminando distorções de mapeamento afim.
  - **Veículos, Sprites e HUD**: Projetados na CPU VR4300 com comandos diretos ao RDP (`psyx_rdp.cpp`).
- **Subdivisão e Clipping**: Projeção com clipping de planos próximos e subdivisão adaptativa de polígonos para contornar limitações de precisão de ponto fixo.
- **Áudio e Efeitos Sonoros**: Motor de som VADPCM reproduzindo efeitos sonoros (SFX), música do jogo em tempo real e faixas de fala XA convertidas em formato WAV64.
- **Vídeos e Cinemáticas (FMV)**: Suporte à reprodução de vídeos em formato MPEG-1/WAV64 através de streaming de cartucho.

### Limitações Conhecidas
- **Expansion Pak Obrigatório**: A memória padrão de 4 MB do console é insuficiente; o jogo exige a expansão de 8 MB RDRAM.
- **Taxa de Quadros em Cenas Complexas**: Dependendo da densidade poligonal de prédios e do número de viaturas em perseguição, quedas de framerate podem ocorrer. Otimizações de pipeline continuam em desenvolvimento ativo.
- **Suporte Multicidades**: O build padrão embute a cidade de Chicago para garantir estabilidade e tempos ágeis de compilação. As cidades de Havana, Rio de Janeiro e Las Vegas requerem ativação seletiva através de sinalizadores do Makefile.

---

## Arquitetura Técnica

```
                    ┌────────────────────────────┐
                    │    REDRIVER2 Game Engine   │
                    │   (Física, IA, Câmera)     │
                    └─────────────┬──────────────┘
                                  │
                    ┌─────────────┴──────────────┐
                    │  Camada de Port N64 / GTE  │
                    │  (Transformações Nativas)  │
                    └──────┬──────────────┬──────┘
                           │              │
           ┌───────────────┴────┐   ┌─────┴────────────────┐
           │ Tiny3D / RDP Direct│   │  libdragon Subsystems│
           │  (Rasterização)    │   │ (DragonFS, Audio/SFX)│
           └────────────────────┘   └──────────────────────┘
```

1. **CPU VR4300 @ 93.75 MHz**: Executa o código C nativo compilado pelo GCC MIPS64. As rotinas da GTE (Geometry Transformation Engine) do PS1 foram substituídas por operações trigonométricas e matriciais nativas de 32/64 bits com ordenação de funções para máximo aproveitamento do cache de instruções (16 KB L1 icache).
2. **Reality Coprocessor (RCP - RSP / RDP)**: O RSP processa listas de comandos de display através de microcódigos modernos do libdragon/Tiny3D, enquanto o RDP desenha texturas em 16-bit RGBA com filtragem de ponto e correção de perspectiva de textura.
3. **Gerenciamento de Memória**: O jogo original foi projetado para a arquitetura de 2 MB de RAM do PS1. No N64, o heap de fases, tabelas de modelos e buffers de vértices foram recalibrados para operar confortavelmente dentro do limite seguro dos 8 MB da RDRAM.
4. **DragonFS (DFS)**: O sistema de arquivos do cartucho armazena texturas, tabelas de colisão, modelos e áudio convertidos, lidos diretamente via DMA de cartucho sem latência mecânica de CD-ROM.

---

## Requisitos de Sistema

### Para Jogar
- **Hardware Real**: Console Nintendo 64 com acessório **Expansion Pak (8 MB de RAM)** instalado + Flashcart (EverDrive-64 v2.5+, EverDrive-64 X7, SummerCart64 ou ED64Plus).
- **Emulador**: [Ares](https://ares-emulator.github.io/) (versão v138 ou superior recomendada, com Expansion Pak ativado). Outros emuladores compatíveis com microcódigos RDPQ modernos do libdragon (ex.: simple64) também são suportados.

### Para Compilar
- **Sistema Operacional**: Linux (Ubuntu 22.04 LTS / 24.04 LTS) ou Windows 10/11 com **WSL2** (Ubuntu).
- **Compilador e SDK**:
  - `mips64-elf-gcc` (GCC 14+ ou GCC 16+ do libdragon toolchain).
  - [libdragon](https://github.com/DragonMinded/libdragon) (branch `preview`, com ferramentas instaladas em `/opt/libdragon`).
- **Dependências de Build**:
  - `python3` (para scripts de extração e empacotamento).
  - `ffmpeg` e `ffprobe` (para conversão de áudio XA e cinemáticas FMV).
  - `git` e `make`.

---

## Obtenção e Preparação dos Discos

Para extrair os recursos originais do jogo:

1. Obtenha a imagem de disco do **Driver 2 (PlayStation 1, Disco 1)** no formato binário bruto (`.bin` com 2352 bytes por setor).
2. Crie a pasta `data/` na raiz do projeto (caso não exista).
3. Renomeie e posicione o arquivo como:
   ```
   data/Driver2CD1.bin
   ```
4. *(Opcional)* Se desejar cinemáticas ou conteúdos adicionais do Disco 2, forneça o caminho através da variável `CD2=/caminho/Driver2CD2.bin` durante a compilação.

---

## Instruções de Compilação

### 1. Clonar o Repositório com Submódulos

```bash
git clone --recurse-submodules https://github.com/Sunshinin11/Driver-2-N64.git
cd Driver-2-N64
```

Se você já clonou sem a opção recursiva, inicialize os submódulos:
```bash
git submodule update --init --recursive
```

### 2. Configurar o Motor REDRIVER2

Execute o script de preparação para baixar e aplicar os patches de compatibilidade no motor do jogo:

```bash
bash setup_engine.sh
```

Esse script realiza:
- Clone/atualização do commit fixado do REDRIVER2 e PsyCross.
- Ajuste de chamadas de trap de CPU para o MIPS VR4300.
- Execução de `patch_engine.py` para compatibilidade de endianness, ajustes de limites de memória e ganchos gráficos.

### 3. Compilar a ROM

Com o arquivo `data/Driver2CD1.bin` presente, execute:

```bash
bash build.sh
```

O processo irá:
1. Extrair os arquivos de fase, modelos, texturas e tabelas do CD1 para a pasta `filesystem_d2/`.
2. Converter amostras sonoras e falas XA para o formato nativo WAV64.
3. Compilar todos os módulos em C/C++ gerando o executável ELF otimizado.
4. Empacotar o sistema de arquivos DragonFS e gerar a ROM final: **`d2_n64.z64`**.

Para limpar os binários e recompilar:
```bash
bash build.sh clean
```

---

## Como Jogar e Mapeamento de Controles

O esquema de controles do Nintendo 64 foi desenhado para proporcionar a mesma resposta clássica de direção:

| Controle Nintendo 64 | Função no Driver 2 | Ação Original PS1 |
|---|---|---|
| **Alavanca Analógica / D-Pad** | Direção (Virar à esquerda / direita) | Direcional / Analógico Esquerdo |
| **Botão A** | Acelerar | Botão ✕ (Cruz) |
| **Botão B** | Frear / Marcha à ré | Botão ◻ (Quadrado) |
| **Gatilho Z** ou **Botão C-Baixo** | Freio de mão | Botão △ (Triângulo) / R1 |
| **Botão C-Cima** | Alterar visão de câmera | Botão Seleção de Câmera |
| **Botão C-Esquerda / C-Direita** | Olhar para os lados / retrovisor | Botões L2 / R2 |
| **Gatilho R** | Buzina / Ações de missão | Botão L1 / R1 |
| **Gatilho L** | Queimar pneus / Burnout | Acelerar + Freio |
| **Botão Start** | Pausar jogo / Menu | Botão Start |

---

## Emulação e Hardware Real

### Emuladores Recomendados
- **[Ares](https://ares-emulator.github.io/)**: O emulador de maior fidelidade para microcódigos RSP/RDP modernos do libdragon. Certifique-se de que a opção de **Expansion Pak (8 MB)** esteja ativada nas configurações do sistema.
- **simple64**: Excelente compatibilidade com títulos libdragon contemporâneos.

### Flashcarts no Console Real
- A ROM gerada (`d2_n64.z64`) pode ser transferida diretamente para o cartão SD do seu **EverDrive-64** (OS 2.0+ recomendado) ou **SummerCart64**.
- Certifique-se de que o Expansion Pak físico original ou compatível de qualidade esteja devidamente instalado no console.

---

## Galeria

> 📸 **Capturas Autorizadas Pendentes**: A pasta [`docs/media/`](docs/media/) está reservada para armazenar capturas reais do port em funcionamento no console e no emulador Ares. As imagens de gameplay estão em fase de homologação e serão adicionadas diretamente a esse diretório pelo desenvolvedor. Nenhuma imagem simulada ou de terceiros foi incluída.

---

## Créditos e Licenças

- **[REDRIVER2 Team](https://github.com/OpenDriver2/REDRIVER2)**: Projeto de descompilação e engenharia reversa do jogo original para PC.
- **[PsyCross Team](https://github.com/OpenDriver2/PsyCross)**: Camada de emulação/compatibilidade das APIs do PlayStation 1.
- **[libdragon Team](https://github.com/DragonMinded/libdragon)**: SDK moderno e de código aberto para o console Nintendo 64.
- **[Tiny3D](https://github.com/sp1180/tiny3d)**: Biblioteca de renderização 3D de alta performance para libdragon.
- **Reflections Interactive & Ubisoft**: Desenvolvedores e detentores dos direitos intelectuais do título clássico Driver 2.

---
---

# 🇺🇸 Driver 2 for Nintendo 64

A native Nintendo 64 port of **Driver 2: The Wheelman Is Back** (PlayStation 1, 2000), built upon the decompilation and reverse-engineering work of the open-source [REDRIVER2](https://github.com/OpenDriver2/REDRIVER2) project and powered by the modern [libdragon](https://github.com/DragonMinded/libdragon) SDK.

The game's original C/C++ logic runs natively on the N64's MIPS VR4300 processor without CPU emulation, featuring native vector transformations, direct RDP rasterization powered by [Tiny3D](https://github.com/sp1180/tiny3d), compressed VADPCM/WAV64 audio, and DragonFS cartridge-based asset storage.

> ⚠️ **Legal Disclaimer & Game Discs**: This repository **DOES NOT contain copyrighted game assets, original binary code, 3D models, textures, or audio**. To compile a playable ROM, the user must provide a legitimate copy of their original PlayStation 1 disc (*Driver 2 — CD1*).

---

## Table of Contents

- [Overview & Current Status](#overview--current-status)
- [Technical Architecture](#technical-architecture)
- [System Requirements](#system-requirements)
- [Obtaining and Preparing Game Discs](#obtaining-and-preparing-game-discs)
- [Build Instructions](#build-instructions)
- [Controls & Gameplay](#controls--gameplay)
- [Emulation and Real Hardware](#emulation-and-real-hardware)
- [Gallery](#gallery)
- [Credits & Licenses](#credits--licenses)

---

## Overview & Current Status

### What Works
- **Game Engine & Vehicle Physics**: Full vehicle simulation, tire traction, city traffic, police AI pursuit logic, and 3D collision systems.
- **Free-Roam & Practice Mode**: Exploration of the Chicago city map with full building geometry, roads, intersections, and street props.
- **Hybrid RDP / Tiny3D Rendering**:
  - **Buildings & Urban Scenery**: Rasterized via **Tiny3D** RSP microcode (`d2_t3d_world.cpp`), using hardware matrix transformations and interleaving with the PS1 Ordering Table (OT) buckets.
  - **Roads & Ground Planes**: Rendered directly through RDP commands with hardware perspective quad correction (`draw_ground_perspective`), eliminating affine texture warping.
  - **Vehicles, Dynamic Props & HUD**: Projected on the VR4300 CPU and dispatched directly to RDPQ display lists (`psyx_rdp.cpp`).
- **Clipping & Adaptive Subdivision**: Near-plane polygon clipping and adaptive subdivision to eliminate fixed-point coordinate overflow artifacts.
- **Audio & Sound Effects**: VADPCM sound synthesis for sound effects (SFX), background music, and converted XA spoken mission dialogue via WAV64 streams.
- **FMV Video Playback**: Support for MPEG-1/WAV64 cutscene video playback streamed directly from cartridge space.

### Known Limitations
- **Expansion Pak Required**: The console's base 4 MB memory is insufficient; the port requires the 8 MB RDRAM expansion.
- **Framerate in Dense Scenes**: In scenes with complex building clusters and multiple police cars in pursuit, framerate drops may occur. Profiling and microcode optimization remain ongoing.
- **Multi-City Configuration**: The default build targets Chicago to maintain fast compilation and stability. Havana, Rio de Janeiro, and Las Vegas can be enabled through Makefile flags.

---

## Technical Architecture

```
                    ┌────────────────────────────┐
                    │    REDRIVER2 Game Engine   │
                    │   (Physics, AI, Camera)    │
                    └─────────────┬──────────────┘
                                  │
                    ┌─────────────┴──────────────┐
                    │  N64 Platform / Native GTE │
                    │   (Vector Transformations) │
                    └──────┬──────────────┬──────┘
                           │              │
           ┌───────────────┴────┐   ┌─────┴────────────────┐
           │ Tiny3D / Direct RDP│   │  libdragon Subsystems│
           │   (Rasterization)  │   │ (DragonFS, Audio/SFX)│
           └────────────────────┘   └──────────────────────┘
```

1. **VR4300 CPU @ 93.75 MHz**: Executes native C code compiled by GCC MIPS64. PS1 Geometry Transformation Engine (GTE) operations have been replaced by native 32/64-bit matrix math routines, with critical function placement configured via linker script (`hot.ld`) to maximize the 16 KB L1 instruction cache.
2. **Reality Coprocessor (RCP - RSP / RDP)**: The RSP dispatches display lists via modern libdragon and Tiny3D microcodes, while the RDP renders 16-bit RGBA primitives with point sampling and perspective texture correction.
3. **Memory Management**: Originally designed for the PS1's 2 MB RAM layout, level heaps, model pointer arrays, and vertex queues were retuned to stay safely within the 8 MB N64 address space.
4. **DragonFS (DFS)**: The cartridge filesystem stores level lumps, models, textures, and converted audio, loaded with zero seek latency via cart DMA.

---

## System Requirements

### To Play
- **Original Hardware**: Nintendo 64 console with an **Expansion Pak (8 MB RAM)** installed + a compatible flashcart (EverDrive-64 v2.5+, X7, SummerCart64, or ED64Plus).
- **Emulator**: [Ares](https://ares-emulator.github.io/) (v138+ recommended with Expansion Pak enabled). Other emulators supporting modern libdragon RDPQ microcode (e.g. simple64) are also supported.

### To Build
- **Operating System**: Linux (Ubuntu 22.04 LTS / 24.04 LTS) or Windows 10/11 using **WSL2** (Ubuntu).
- **Toolchain & SDK**:
  - `mips64-elf-gcc` (GCC 14+ or GCC 16+ from the libdragon toolchain).
  - [libdragon](https://github.com/DragonMinded/libdragon) (preview branch, installed to `/opt/libdragon`).
- **Build Utilities**:
  - `python3` (for data extraction and asset patching).
  - `ffmpeg` & `ffprobe` (for XA audio and FMV video conversion).
  - `git` & `make`.

---

## Obtaining and Preparing Game Discs

To extract the original game data:

1. Obtain a raw binary CD image of **Driver 2 (PlayStation 1, Disc 1)** in `.bin` format (2352 bytes per sector).
2. Create the `data/` folder in the project root if it does not already exist.
3. Place and rename the file as:
   ```
   data/Driver2CD1.bin
   ```
4. *(Optional)* To include cutscenes from Disc 2, set the `CD2=/path/to/Driver2CD2.bin` variable during build.

---

## Build Instructions

### 1. Clone the Repository with Submodules

```bash
git clone --recurse-submodules https://github.com/Sunshinin11/Driver-2-N64.git
cd Driver-2-N64
```

If already cloned without recursive submodules:
```bash
git submodule update --init --recursive
```

### 2. Set Up the REDRIVER2 Engine

Run the engine setup script to download and patch upstream engine sources:

```bash
bash setup_engine.sh
```

This script:
- Fetches the pinned commits of REDRIVER2 and PsyCross.
- Replaces x86-specific trap calls with MIPS-safe equivalents.
- Executes `patch_engine.py` to adapt endianness, memory heaps, and rendering hooks.

### 3. Build the ROM

With `data/Driver2CD1.bin` in place, run:

```bash
bash build.sh
```

The build pipeline will:
1. Extract city maps, textures, and models from CD1 into `filesystem_d2/`.
2. Convert audio samples and XA voice lines into native WAV64 format.
3. Compile all C/C++ source files and link the ELF executable.
4. Pack the DragonFS cartridge filesystem and produce the output ROM: **`d2_n64.z64`**.

To clean intermediate build files:
```bash
bash build.sh clean
```

---

## Controls & Gameplay

The Nintendo 64 controller layout maps closely to the classic Driver 2 controls:

| Nintendo 64 Controller | In-Game Action | Original PS1 Input |
|---|---|---|
| **Analog Stick / D-Pad** | Steering (Turn Left / Right) | D-Pad / Left Stick |
| **A Button** | Accelerate | ✕ (Cross) |
| **B Button** | Brake / Reverse | ◻ (Square) |
| **Z Trigger** or **C-Down Button** | Handbrake | △ (Triangle) / R1 |
| **C-Up Button** | Cycle Camera View | Camera Toggle |
| **C-Left / C-Right Buttons** | Look Left / Look Right / Rear View | L2 / R2 |
| **R Trigger** | Horn / Mission Actions | L1 / R1 |
| **L Trigger** | Burnout / Wheelspin | Accelerate + Brake |
| **Start Button** | Pause / Options Menu | Start |

---

## Emulation and Real Hardware

### Recommended Emulators
- **[Ares](https://ares-emulator.github.io/)**: Provides highest-accuracy emulation of modern RSP/RDP microcodes used by libdragon. Ensure the **Expansion Pak (8 MB)** option is enabled in system settings.
- **simple64**: High compatibility with recent libdragon homebrew titles.

### Flashcarts on Real Hardware
- The compiled ROM (`d2_n64.z64`) can be copied directly to the SD card of your **EverDrive-64** (v2.5+ or X7) or **SummerCart64**.
- An authentic or high-quality third-party **Expansion Pak** must be installed in the console's memory jumper bay.

---

## Gallery

> 📸 **Authorized Screenshots Pending**: The [`docs/media/`](docs/media/) directory is reserved for genuine in-game screenshots captured from hardware and high-accuracy Ares emulator runs. Gameplay captures are undergoing authorization and will be added directly to this folder by the developer. No simulated or third-party placeholder images are included.

---

## Credits & Licenses

- **[REDRIVER2 Team](https://github.com/OpenDriver2/REDRIVER2)**: Original reverse engineering and decompilation of Driver 2 for PC.
- **[PsyCross Team](https://github.com/OpenDriver2/PsyCross)**: Cross-platform implementation of PlayStation 1 system APIs.
- **[libdragon Team](https://github.com/DragonMinded/libdragon)**: Modern open-source software development kit for Nintendo 64.
- **[Tiny3D](https://github.com/sp1180/tiny3d)**: High-performance 3D rasterization library for libdragon.
- **Reflections Interactive & Ubisoft**: Original game designers, programmers, and copyright holders of Driver 2.
