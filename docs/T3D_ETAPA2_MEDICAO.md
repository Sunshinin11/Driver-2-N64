# Medições das Etapas 1, 2a, 2b e 2c — Mundo Nativo Tiny3D, Céu PSX e Áudio SPU

Data: 31/10/2026
Emulador: Ares v148 (`ares.exe --system "Nintendo 64"`, Expansion Pak 8 MB)
Metodologia:
- Todas as medições de FPS e `ticks/frame` de referência usam `D2_PERFDBG=0` e `D2_ORIGIN_PROF=0`.
- Percurso determinístico controlado por `CameraCnt` (`D2_BENCH=1` em `port/n64/sys_n64.c`), garantindo exatamente a mesma posição `(vx, vz)` da câmera em cada janela de 32 passos de simulação (`cc=64, 96, ..., 512`).
- Segunda rodada separada com `D2_ORIGIN_PROF=1` apenas para decompor o custo por subsistema (`[OPROF_SUM]` / `[OPROF]`).
- Frequência do contador CP0 Count do VR4300: `46.875.000` ticks/s (`781.250` ticks por vblank NTSC a 60 Hz; orçamento de 20 FPS = `2.343.750` ticks/quadro).

---

## 1. Commits Avaliados

| Etapa | Commit | Resumo |
|---|---|---|
| Base | `471fa86` | `t3d: preserva correcoes de matriz/luz, medicao inicial e handoff` |
| Etapa 1 | `3fffab3` | `perf: adiciona perfil leve por origem (D2_ORIGIN_PROF) e benchmark por CameraCnt` |
| Etapa 2a | `c2938c2` | `t3d: migra chao/ruas (DrawTILES) e geometria estatica adicional para o Tiny3D` |
| Etapa 2b | `9d5edf2` | `perf(sky): restaura culling PSX da cupula do ceu e evita recorte UV por quadro` |
| Etapa 2c | `36b5a73` | `audio: bombeia SPU fora da ISR, corrige pitch/volume e otimiza busca ADPCM` |

---

## 2. Uso de Memória (`.bss`, Blocos RSPQ e Heap Livre)

### Tamanhos ELF (`mips64-elf-size`, `D2_BENCH=0 D2_ORIGIN_PROF=0 D2_PERFDBG=0`)
- **Legado (`D2_T3D_WORLD=0`, `build_leg_clean/d2_n64.elf`)**: `text=916.720`, `data=50.552`, `bss=5.987.832`, `dec=6.955.104` (`0x6a2060`) — zero pools estáticos do Tiny3D.
- **Tiny3D (`D2_T3D_WORLD=1`, `build_d2/d2_n64.elf`)**: `text=950.608`, `data=69.960`, `bss=6.189.072`, `dec=7.209.640` (`0x6e02a8`) — acréscimo de `201.240` B (`196,5 KB`) em `.bss` sob `#ifdef D2_T3D_WORLD`.

### Arenas Estáticas e Heap em Execução (Chicago — Take a Ride)
- **Pool estático de vértices Tiny3D (`s_t3d_vtx_pool`)**: `144 KB` (`9.216` vértices); uso medido em Chicago = **`7.986 / 9.216` vértices (`124 KB`)** para **`425` modelos convertidos** (`tiles=132, predios=293`, restando `72` modelos de adereços/árvores/postes/alfa no caminho antigo, de `497` modelos não vazios) — `logs/e2c_opt_ref.log:113`.
- **Pool estático de matrizes FP (`s_t3d_mat_pool`)**: `36 KB` (`2 × 288 × 64` B: até `192` tiles + `96` prédios por quadro).
- **Blocos RSPQ no heap (`d2_t3d_on_models_loaded`)**: `211.936` B.
  - Em `44.100 Hz` (`e2b`): heap livre após carga = `145.200` B (`logs/e2b_t3d_ref.log:141`).
  - Em `32.000 Hz` (`e2c`, `audio_init(32000, 4)`): heap livre após carga subiu para **`152.992` B** (`364.928 -> 152.992`, ganho de `+7.792` B) — `logs/e2c_opt_ref.log:114`.

---

## 3. Evolução de Desempenho em Gameplay (`D2_ORIGIN_PROF=0 D2_PERFDBG=0`)

### 3.1. Progressão de Renderização Pura (antes do bombeamento de áudio em gameplay: Legado -> Etapa 1 -> Etapa 2a -> Etapa 2b)

Todas as janelas abaixo percorrem a mesma trajetória (`pos=vx,vz`) em Chicago (`area=7`):

| Janela (`pos`) | Legado (`e1_leg_ref.log`) | Etapa 1 (`e1_t3d_ref.log`) | Etapa 2a (`e2_t3d_ref.log`) | Etapa 2b (`e2b_t3d_ref.log`) | Ganho E2b vs Legado |
|---|---|---|---|---|---|
| `cc=64` (`6216,-223550`) | `4.777.658` ticks (`9,85` FPS, L314) | `4.332.664` (`10,85` FPS, L299) | `3.812.867` (`12,39` FPS, L283) | **`2.654.762`** (`p95=2.785.561`, **`17,78` FPS**, L266) | **`-44,4%` ticks / `+80,5%` FPS** |
| `cc=96` (`6216,-223550`) | `4.783.843` ticks (`9,85` FPS, L413) | `4.338.522` (`10,79` FPS, L389) | `3.808.251` (`12,31` FPS, L363) | **`2.660.704`** (`p95=2.780.260`, **`17,61` FPS**, L321) | **`-44,4%` ticks / `+78,8%` FPS** |
| `cc=160` (`6210,-222320`) | `5.163.518` ticks (`9,10` FPS, L624) | `4.614.325` (`10,21` FPS, L578) | `4.182.608` (`11,29` FPS, L535) | **`3.029.949`** (`p95=3.268.250`, **`15,61` FPS**, L444) | **`-41,3%` ticks / `+71,5%` FPS** |
| `cc=256` (`6188,-211196`) | `4.991.857` ticks (`9,41` FPS, L948) | `4.309.053` (`10,91` FPS, L860) | `4.057.542` (`11,57` FPS, L806) | **`2.954.791`** (`p95=3.149.894`, **`16,00` FPS**, L650) | **`-40,8%` ticks / `+70,0%` FPS** |
| `cc=352` (`6173,-194596`) | `5.579.250` ticks (`8,42` FPS, L1289) | `4.830.945` (`9,75` FPS, L1159) | `4.599.055` (`10,21` FPS, L1091) | **`3.485.537`** (`p95=3.951.124`, **`13,52` FPS**, L862) | **`-37,5%` ticks / `+60,6%` FPS** |
| `cc=384` (`6216,-189988`) | `5.482.818` ticks (`8,57` FPS, L1403) | `5.131.012` (`9,14` FPS, L1266) | `4.765.531` (`9,85` FPS, L1190) | **`3.654.827`** (`p95=4.060.625`, **`12,80` FPS**, L937) | **`-33,3%` ticks / `+49,4%` FPS** |
| `cc=448` (`6712,-182629`, 8 carros) | `6.341.323` ticks (`7,41` FPS, L1658) | `6.037.058` (`7,80` FPS, L1508) | `5.931.751` (`7,90` FPS, L1428) | **`4.639.071`** (`p95=4.847.278`, **`10,11` FPS**, L1123) | **`-26,8%` ticks / `+36,4%` FPS** |
| `cc=512` (`6896,-173471`) | `5.616.447` ticks (`8,38` FPS, L1910) | `5.233.122` (`9,01` FPS, L1743) | `4.973.133` (`9,46` FPS, L1658) | **`3.777.710`** (`p95=4.198.711`, **`12,47` FPS**, L1298) | **`-32,7%` ticks / `+48,8%` FPS** |

- **Confirmação independente pelas linhas `[fps]` (`Δframes × 60 / Δvbl`)**:
  - Legado em `cc=64`: `f=510 vbl=1827` (`logs/e1_leg_ref.log:301`) -> `f=540 vbl=2009` (`logs/e1_leg_ref.log:394`): `30 × 60 / 182 =` **`9,89 FPS`**.
  - Etapa 2b em `cc=64`: `f=720 vbl=1843` (`logs/e2b_t3d_ref.log:284`) -> `f=750 vbl=1946` (`logs/e2b_t3d_ref.log:337`): `30 × 60 / 103 =` **`17,48 FPS`**.
- **Missão 1 a pé no beco (`D2_PERF_MISSION1`, Etapa 2a antes mesmo do ganho do céu da Etapa 2b)**:
  - `cc=928` (`pos=339,179658`): **`2.426.235` ticks/quadro (`19,39` FPS)** (`logs/e2_m1_t3d_ref.log:1833`); `[fps]` `f=1290 vbl=4761` -> `f=1320 vbl=4855` (`logs/e2_m1_t3d_ref.log:1822,1896`): `30 × 60 / 94 =` **`19,15 FPS`**.

### 3.2. Impacto Real do Áudio Ativo em Gameplay (Etapa 2c: ISR Desacoplada + Busca ADPCM $O(1)$ + Mixer 32 kHz)

Até a Etapa 2b, `d2_audio_pump()` só era chamado dentro do laço de espera de `d2_sys_wait_vbl_until` (`port/n64/sys_n64.c`), que nunca iterava quando o jogo rodava abaixo de 60 FPS (`0` chamadas em gameplay), enquanto `VsyncProc` (`XM_Update` + `SoundHandler`) rodava dentro da ISR de hardware `vi_handler` (gerando centenas de avisos `VI WARNING: __vblank_interrupt outside of vblank period` e condição de corrida contra `mixer_try_play()`).

Na Etapa 2c (`36b5a73`):
1. `VsyncProc` saiu da ISR de VI e passou a rodar de forma síncrona na thread principal via `d2_sys_pump_vbl_cb()`, eliminando **100% dos avisos `VI WARNING` e 100% das asserções de concorrência do mixer** em execuções completas de 90 s.
2. `d2_audio_pump()` passou a ser chamado em `PsyX_BeginScene` e `PsyX_EndScene`, ativando música XM + efeitos SPU em gameplay e revelando o custo real de decodificar até 24 vozes ADPCM do PS1 por software na CPU VR4300.
3. A busca linear $O(N)$ em `d2_spu_seek` (que re-decodificava a amostra desde o bloco 0 a cada repetição de loop de motor/sirene/música) foi substituída por busca $O(1)$ (`d->loopBlock`) + `d2_spu_decode_28` desenrolado direto no `samplebuffer_append` + saída a `32.000 Hz`:

| Janela (`pos`) | Etapa 2b s/ áudio (`e2b_t3d_ref.log`) | Etapa 2c áudio $O(N)$ (`e2c_t3d_ref.log`) | Etapa 2c áudio otimizado $O(1)$ (`e2c_opt_ref.log`) | Economia da otimização ADPCM |
|---|---|---|---|---|
| `cc=64` (`6216,-223550`) | `2.654.762` (`17,78` FPS, L266) | `3.619.412` (`12,97` FPS, L146) | **`3.440.048`** (`p95=3.872.060`, **`13,71` FPS**, L146) | `-179.364` ticks (`-5,0%`) |
| `cc=96` (`6216,-223550`) | `2.660.704` (`17,61` FPS, L321) | `3.737.831` (`12,63` FPS, L152) | **`3.417.687`** (`p95=3.893.060`, **`13,71` FPS**, L152) | `-320.144` ticks (`-8,6%`) |
| `cc=160` (`6210,-222320`) | `3.029.949` (`15,61` FPS, L444) | `4.727.845` (`9,95` FPS, L165) | **`4.311.380`** (`p95=4.780.289`, **`10,91` FPS**, L165) | `-416.465` ticks (`-8,8%`) |
| `cc=256` (`6188,-211196`) | `2.954.791` (`16,00` FPS, L650) | `6.710.176` (`7,01` FPS, L188) | **`4.921.393`** (`p95=5.382.908`, **`9,55` FPS**, L188) | **`-1.788.783` ticks (`-26,7%`)** |
| `cc=320` (`6178,-200357`) | `3.297.694` (`14,33` FPS, L794) | `8.984.575` (`5,23` FPS, L200) | **`6.530.220`** (`p95=7.926.529`, **`7,19` FPS**, L200) | **`-2.454.355` ticks (`-27,3%`)** |
| `cc=352` (`6173,-194596`) | `3.485.537` (`13,52` FPS, L862) | `8.780.813` (`5,35` FPS, L206) | **`6.338.527`** (`p95=7.288.339`, **`7,41` FPS**, L206) | **`-2.442.286` ticks (`-27,8%`)** |
| `cc=384` (`6216,-189988`) | `3.654.827` (`12,80` FPS, L937) | `9.709.494` (`4,85` FPS, L212) | **`7.055.676`** (`p95=8.240.863`, **`6,67` FPS**, L212) | **`-2.653.818` ticks (`-27,3%`)** |
| `cc=448` (`6712,-182629`) | `4.639.071` (`10,11` FPS, L1123) | *(não alcançado em 90s)* | **`8.869.324`** (`p95=9.873.866`, **`5,30` FPS**, L224) | Completou até `cc=480` sem falhas |

- **Confirmação independente pelas linhas `[fps]` em `e2c_opt_ref.log`**:
  - `cc=64`: `f=540 vbl=1767` (`logs/e2c_opt_ref.log:142`) -> `f=570 vbl=1897` (`logs/e2c_opt_ref.log:148`): `30 × 60 / 130 =` **`13,85 FPS`**.

---

## 4. Decomposição por Subsistema (`D2_ORIGIN_PROF=1`, `cc=96`)

Comparação direta na mesma janela `cc=96` (`pos=6216,-223550`, `area=7`) entre todos os estágios:

| Categoria | Legado (`e1_leg_prof.log:453-465`) | Etapa 1 (`e1_t3d_prof.log:421-434`) | Etapa 2a (`e2_t3d_prof.log:381-395`) | Etapa 2b (`e2b_t3d_prof.log:325-339`) | Etapa 2c c/ Áudio (`e2c_opt_prof.log:183-197`) |
|---|---|---|---|---|---|
| `GROUND` (legado) | `1.367.262` (`488.592` B) | `1.338.559` (`482.384` B) | `23.592` (`0` B) | `23.440` (`0` B) | `37.769` (`0` B) |
| `T3D_GRD` (nativo) | — | — | `165.138` (`114` tiles, `424` tris) | `163.350` (`114` tiles, `424` tris) | `157.632` (`114` tiles, `424` tris) |
| `PERM_BLD` (legado) | `645.814` (`13` objs) | `26.071` (`2` objs) | `284.206` (`6` objs, `uvclip=17`) | `254.604` (`6` objs, `uvclip=17`) | `301.161` (`6` objs, `uvclip=17`) |
| `T3D_BLD` (nativo) | — | `23.763` (`11` objs) | `10.379` (`7` objs, `156` tris) | `9.612` (`7` objs, `156` tris) | `11.418` (`7` objs, `156` tris) |
| `SKY` | `972.265` (`uvclip=38, 260 KB`) | `829.431` (`uvclip=38, 260 KB`) | `942.548` (`uvclip=38, 260 KB`) | **`347.872` (`uvclip=0, 70 KB`)** | `377.407` (`uvclip=0, 70 KB`) |
| `CARS` (legado) | `593.199` (`4` carros) | `566.034` (`4` carros) | `629.077` (`4` carros) | `600.880` (`4` carros) | `672.314` (`4` carros) |
| `SHADOWS` | `262.905` | `309.617` | `323.037` | **`111.776`** (sem thrash de slots) | `116.770` |
| `SPRITES` | `227.203` | `254.003` | `308.542` | `268.828` | `272.871` |
| `MAP_WALK` | `207.706` | `206.724` | `205.930` | `217.057` | `214.553` |
| `HUD` | `170.845` | `172.326` | `174.696` | `164.653` | `176.010` |
| `step` (`StepGame`) | `365.563` | `371.296` | `355.053` | `359.806` | `345.079` |
| `ot_ovh` (+ áudio em E2c) | `158.834` | `150.864` | `120.564` | `116.812` | **`630.554`** (`+513.742` ticks de áudio) |

---

## 5. Gargalos Remanescentes Hierarquizados (Rumo aos 20 FPS Estáveis com Áudio)

Com chão/ruas e prédios estáticos já migrados para o Tiny3D (`169K` ticks somados para `121` instâncias e `580` triângulos em `cc=96`) e o recorte UV float do céu eliminado, os dois maiores custos do quadro são:

1. **`CARS` (`DrawAllTheCars` no caminho legado PS1/GTE)**:
   - `600K–672K` ticks com 4 carros (`cc=96`, `logs/e2c_opt_prof.log:190`) e escala até **`1,78M` ticks** com 9 carros (`cc=352`, `logs/e2c_opt_prof.log:363`).
   - Além do próprio custo de CPU/TMEM, cada milissegundo extra gasto em `CARS` aumenta o número de vblanks por quadro e multiplica o trabalho de áudio por quadro!
2. **Decodificação PS1 ADPCM por software na CPU + `XM_Update` (`ot_ovh`)**:
   - Consome `~105K–215K` ticks por vblank de 1/60 s (`13%–27%` contínuos da CPU VR4300), totalizando `~514K` ticks/quadro a `13,7 FPS` (`cc=96`, `logs/e2c_opt_prof.log:183`) e `1,75M–2,0M` ticks/quadro sob tráfego intenso (`cc=320..352`, `logs/e2c_opt_prof.log:335,356`).
   - **Solução nativa N64**: converter os blocos ADPCM do SPU (offline ou na carga do banco em `PsyX_SPUAL_Write`) para **`WAVEFORM_FORMAT_VADPCM`** nativo do N64 — decodificado diretamente pelo **RSP em DMEM com zero toque por amostra na CPU** (`libdragon` `mixer.h`), além de opção configurável para música XM (`D2_NO_XM_MUSIC` ou limite de canais XM).
3. **72 modelos estáticos/adereços restantes em `PERM_BLD` (`uvclip=17..40`)**:
   - Postes, árvores, barreiras e grades que ainda caem no recorte UV do backend (`254K–502K` ticks em `logs/e2c_opt_prof.log:186,292`).
4. **Texturas CI4 nativas + sincronização de spool (`d2_t3d_on_spool_change`) + Z-buffer (Etapas 3 e 4)**:
   - Atualmente tiles e prédios no Tiny3D usam cores por face/vértice amostradas da textura PS1 em VRAM (`sample_poly_color`) + ordenação back-to-front sem Z-buffer. A validação visual e sonora cabe ao teste humano no Ares.
