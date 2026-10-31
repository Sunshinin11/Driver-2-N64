# Relatório Etapa A — Áudio Nativo e Comprimido (VADPCM 12 kHz)

## 1. Contexto e Objetivos

Na arquitetura herdada do PS1 (REDRIVER2 / PsyX), o subsistema de áudio executava a emulação da SPU inteiramente na CPU do N64 (`d2_audio_pump` / `d2_spu_decode_28`). Cada voz de efeito sonoro ativa (motor do carro, derrapagens, colisões, sirenes, tráfego) exigia que a CPU decodificasse continuamente blocos ADPCM de 28 amostras e reamostrasse em software para um buffer intermediário de 32 kHz.

Isso consumia entre 1,2 e 1,5 milhão de ciclos de CPU por quadro durante o gameplay (`ot_ovh` no profiler), limitando severamente a taxa de quadros e concorrendo com o pipeline de renderização do mundo.

O objetivo da Etapa A foi:
1. Converter offline todos os bancos de áudio SFX do Driver 2 (`VOICES2.BLK`, 72 lumps / 337 amostras) para WAV64 comprimido em hardware via VADPCM mono a $\le 12.000$ Hz utilizando `audioconv64`.
2. Preservar pontos de loop, identificadores, volume relativo, panning estéreo e pitch dinâmico contínuo.
3. Manter a saída do mixer libdragon em 32.000 Hz e manter a reprodução de música XM intocada no banco 0 (canais 16 a 23).
4. Descarregar 100% da decodificação de efeitos sonoros para o microcódigo do RSP via DMA direto (`WAV64_STREAMING_NONE`), eliminando todo o processamento de ADPCM pela CPU para vozes de SFX.
5. Manter as flags experimentais desligadas por padrão (`D2_VADPCM ?= 0` no `Makefile.d2`).

---

## 2. Implementação Técnica

### 2.1 Conversor Offline (`tools/convert_sfx_wav64.py`)
- O script analisa os 72 lumps em `filesystem_d2/DRIVER2/SOUND/VOICES2.BLK`.
- Para cada amostra:
  - Extrai as amostras PCM nativas e preserva os marcadores de loop (`loop_start`).
  - Aplica resampling para até 12.000 Hz quando a taxa original era superior.
  - Converte para WAV64 com compressão de hardware VADPCM mono via `audioconv64 --wav-vadpcm 1 --wav-loop true`.
- Resultado: 337 arquivos `.wav64` em `filesystem_vadpcm/SFX64/L%02d_S%02d.wav64` ocupando apenas 2,05 MB no cartucho (DFS).

### 2.2 Motor de Reprodução Nativo (`port/n64/psyx_backend.cpp`)
- `s_native_sfx[72][35]`: tabela de descritores de ondas nativas em RDRAM.
- `d2_vadpcm_process_lump(dbank, start_idx, num_samples, samps_ptr)`: chamado durante o carregamento de cada fase (`LoadGameLevel` / `LoadSoundBankDynamic`). Abre os arquivos `.wav64` da fase sem streaming (`WAV64_STREAMING_NONE`), garantindo que os quadros de 9 bytes VADPCM fiquem residentes na RDRAM sem travamentos de barramento PI ou DMA durante o spooling do mapa.
- Marcação de voz (`D2_SFX_TAG = 0x40000000u | (lump << 8) | idx`): o ponteiro `s_spuVoices[v].spu_addr` recebe a tag nativa quando a amostra é uma voz VADPCM, interceptando o disparo em `d2_spu_shape`.
- Em `d2_spu_apply_voice`, a libdragon ajusta a frequência dinamicamente com base no pitch do REDRIVER2:
  $$\text{freq} = 44100.0 \times \frac{\text{pitch}}{4096.0} \times \frac{\text{wav\_freq}}{\text{orig\_sr}}$$
- Preserva volumes independentes esquerdo e direito via `mixer_ch_set_vol(ch, lvol, rvol)`.
- `d2_vadpcm_reset()`: desaloca e fecha todas as formas de onda carregadas e para canais 0..15 ao trocar de fase ou reiniciar a missão, evitando vazamento de memória heap.

### 2.3 Integração no REDRIVER2 (`tools/patch_engine.py`)
- `Game/C/gamesnd.c` (`LoadBankFromLump`): notifica o backend do lump ativo com `d2_vadpcm_set_current_lump(lump)`.
- `Game/C/sound.c` (`LoadSoundBankDynamic`): no `case 0` chama `d2_vadpcm_reset()`. No carregamento de cada lump, chama `d2_vadpcm_process_lump()` e ignora a transferência para a memória SPU de 512 KB, permitindo encolher `s_spuRam` de 512 KB para 128 KB.
- `Game/C/sound.c` (`InitSound`): sob `D2_VADPCM`, reduz `banksize[1]` de 412 KB para 1 KB, compatível com a redução da `s_spuRam`.

---

## 3. Comprovação da Eliminação de ADPCM na CPU

Medição registrada em `logs/ea_proof.log`:
- Amostras carregadas para Chicago Take a Ride:
  - Lumps 12 (3/3), 4 (3/3), 13 (3/3), 1 (12/12), 66 (6/6), 69 (1/1), 29 (6/6), 32 (11/11), 21 (6/6), 67 (1/1), 2 (3/3), 18 (3/3), 10 (3/3).
- Contagem periódica de blocos ADPCM decodificados pela CPU a cada 30 quadros durante gameplay (aceleração, tráfego, derrapagens, câmeras):
  ```
  [AUDIO_ADPCM_CPU] f=510 sfx_blocks=0 xm_blocks=2858
  [AUDIO_ADPCM_CPU] f=540 sfx_blocks=0 xm_blocks=3272
  [AUDIO_ADPCM_CPU] f=570 sfx_blocks=0 xm_blocks=3198
  [AUDIO_ADPCM_CPU] f=600 sfx_blocks=0 xm_blocks=4085
  [AUDIO_ADPCM_CPU] f=630 sfx_blocks=0 xm_blocks=4751
  [AUDIO_ADPCM_CPU] f=660 sfx_blocks=0 xm_blocks=6391
  [AUDIO_ADPCM_CPU] f=690 sfx_blocks=0 xm_blocks=10075
  [AUDIO_ADPCM_CPU] f=720 sfx_blocks=0 xm_blocks=9732
  [AUDIO_ADPCM_CPU] f=750 sfx_blocks=0 xm_blocks=9514
  [AUDIO_ADPCM_CPU] f=780 sfx_blocks=0 xm_blocks=11298
  [AUDIO_ADPCM_CPU] f=810 sfx_blocks=0 xm_blocks=11203
  [AUDIO_ADPCM_CPU] f=840 sfx_blocks=0 xm_blocks=16391
  [AUDIO_ADPCM_CPU] f=870 sfx_blocks=0 xm_blocks=17042
  ```
- **Resultado comprovado**: `sfx_blocks == 0` em 100% dos quadros de gameplay! Toda a decodificação de efeitos sonoros é realizada pelo RSP em hardware via DMA. O canal de música XM (`xm_blocks`) opera normalmente em paralelo.

---

## 4. Medições de Desempenho (FPS e Ticks)

Comparativo rigoroso do benchmark automatizado (Take a Ride - Chicago) entre o baseline da Etapa 2c (`logs/e2c_opt_ref.log`) e a Etapa A com VADPCM (`logs/ea_vadpcm_ref.log`), ambas sem profiler ativo (`D2_BENCH=1 D2_ORIGIN_PROF=0 D2_PERFDBG=0`):

| Janela (`CameraCnt`) | Baseline Ticks (`e2c_opt_ref.log`) | Baseline FPS | VADPCM Ticks (`ea_vadpcm_ref.log`) | VADPCM FPS | Variação Ticks | Ganho de FPS |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| `cc=32` | 4.394.020 | 10,72 | 3.095.729 | 15,24 | -1.298.291 (-29,5%) | **+4,52 FPS (+42,2%)** |
| `cc=64` | 4.144.370 | 11,38 | 2.975.543 | 15,87 | -1.168.827 (-28,2%) | **+4,49 FPS (+39,5%)** |
| `cc=96` | 4.186.299 | 11,26 | 2.976.456 | 15,74 | -1.209.843 (-28,9%) | **+4,48 FPS (+39,8%)** |
| `cc=128` | 4.436.564 | 10,63 | 3.167.566 | 14,88 | -1.268.998 (-28,6%) | **+4,25 FPS (+40,0%)** |
| `cc=160` | 4.908.681 | 9,60 | 3.426.034 | 13,71 | -1.482.647 (-30,2%) | **+4,11 FPS (+42,8%)** |
| `cc=192` | 5.378.182 | 8,76 | 3.740.786 | 12,55 | -1.637.396 (-30,4%) | **+3,79 FPS (+43,3%)** |
| `cc=224` | 5.679.673 | 8,30 | 3.982.477 | 11,78 | -1.697.196 (-29,9%) | **+3,48 FPS (+41,9%)** |
| `cc=256` | 5.518.397 | 8,54 | 3.807.675 | 12,39 | -1.710.722 (-31,0%) | **+3,85 FPS (+45,1%)** |
| `cc=288` | 6.353.692 | 7,42 | 4.440.297 | 10,55 | -1.913.395 (-30,1%) | **+3,13 FPS (+42,2%)** |
| `cc=320` | 6.096.356 | 7,73 | 4.265.533 | 11,03 | -1.830.823 (-30,0%) | **+3,30 FPS (+42,7%)** |
| `cc=352` | 6.338.527 | 7,41 | 4.417.740 | 10,67 | -1.920.787 (-30,3%) | **+3,26 FPS (+44,0%)** |
| `cc=384` | 7.055.676 | 6,67 | 5.012.383 | 9,37 | -2.043.293 (-29,0%) | **+2,70 FPS (+40,5%)** |
| `cc=416` | 8.371.616 | 5,61 | 5.936.816 | 7,93 | -2.434.800 (-29,1%) | **+2,32 FPS (+41,4%)** |

### Redução de Overhead de Áudio na CPU (`ot_ovh`)
Comparando os logs de profiler (`logs/e2c_opt_prof.log` vs `logs/ea_proof.log`):
- `cc=352`: `ot_ovh` caiu de 2.228.881 ticks para 933.747 ticks (**-1.295.134 ticks economizados por quadro, redução de -58%**).
- `cc=384`: `ot_ovh` caiu de 2.165.584 ticks para 970.500 ticks (**-1.195.084 ticks economizados por quadro, redução de -55%**).

---

## 5. Memória (.bss, Heap e ROM)

| Métrica | Baseline (`D2_VADPCM=0`) | Etapa A (`D2_VADPCM=1`) | Diferença |
| :--- | :---: | :---: | :---: |
| **Seção `.bss`** | 6.189.072 bytes (6.044 KB) | 5.816.016 bytes (5.679 KB) | **-373.056 bytes (-364,3 KB)** |
| **Residente VADPCM em RDRAM** | 0 bytes | 259.850 bytes (253,8 KB) | +259.850 bytes (+253,8 KB) |
| **Balanço Líquido de RDRAM** | - | - | **+113.206 bytes (+110,5 KB livres adicionais)** |
| **Tamanho da ROM (`z64`)** | 40.091.648 bytes (38,23 MB) | 42.254.336 bytes (40,29 MB) | +2.162.688 bytes (+2,06 MB) |

- A redução do buffer `s_spuRam` de 512 KB para 128 KB economizou mais memória estática (.bss) do que todo o conjunto de amostras VADPCM pré-carregadas para Chicago em RDRAM, resultando em saldo positivo de memória livre para o jogo.
- Com `D2_VADPCM=0`, o `.bss` e o sistema de arquivos DFS permanecem rigorosamente idênticos ao baseline (zero regressão).

---

## 6. ROM de Entrega e Verificação

A ROM jogável de entrega para teste do usuário no Ares foi gerada com todas as flags de profiling e benchmark desligadas (`D2_T3D_WORLD=1 D2_VADPCM=1 D2_BENCH=0 D2_ORIGIN_PROF=0 D2_PERFDBG=0`):

- **Arquivo**: `d2_n64_etapaA_vadpcm.z64`
- **Tamanho**: 42.254.336 bytes
- **SHA-256**: `2dbf185f78c971a2bebdb30e3b389883fdbe09d3267fd742a1eceb4d9d0e056f`

---

## 7. Instruções para Validação pelo Usuário no Ares

O usuário deve carregar a ROM `d2_n64_etapaA_vadpcm.z64` no Ares emulator e verificar auditivamente durante o modo Take a Ride (Chicago):
1. Som do motor do carro do jogador acelerando, desacelerando e trocando marchas de acordo com a velocidade (pitch dinâmico).
2. Sons de derrapagem (freio e curvas bruscas) e colisões com postes, prédios e tráfego.
3. Sons de sirenes e carros da polícia quando perseguindo Tanner.
4. Música XM tocando normalmente e sem interferência nos canais de SFX.
5. Fluidez e resposta dos controles decorrentes do ganho de ~40% a 45% no FPS.
