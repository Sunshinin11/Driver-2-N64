# Relatório Técnico de Investigação: Regressão de Desempenho e Caminho para 30 FPS no Driver 2 (Nintendo 64)

**Projeto:** `C:\Desenvolvimento\Nintendo 64\Driver 2 64\D2_N64`  
**Escopo:** Investigação técnica somente-leitura (sem alterações em código de produção, histórico Git ou ROMs existentes), preservando as 4 cidades (`CHICAGO`, `HAVANA`, `RIO`, `VEGAS`), músicas, SFX, 33 vídeos FMV e limite de cartucho `<= 64 MiB`.

---

## 1. Resumo Executivo e Classificação de Evidências

| Conclusão | Classificação | Evidência Principal |
| :--- | :--- | :--- |
| **`a30b79d_completo` (`15,57–15,63 FPS`) vs `HEAD` (`17,96–18,06 FPS`):** a compilação com `GAME_OPT ?= -Os` na base `a30b79d` penaliza o custo de CPU de travessia/lógica (~+430k ticks/quadro; `carros` 316k vs 159k, `tiles` 282k vs 135k) em relação ao `-O2` global do `HEAD`. | **Comprovado** | `Makefile.d2` (`-Os` em `a30b79d` vs `-O2` em `HEAD`), `telemetria_a30b79d_diagnostico.log` (`3,04 M` ticks/q) vs `telemetria_lpk1.log` (`2,61 M` ticks/q). |
| **A regressão entre sessões (`20,02 FPS` em `a30b79d` vs `15,57–15,63 FPS` em `a30b79d_completo`) NÃO é causada por I/O de LEVPACK ou decodificação XA/ULC em tempo real durante o gameplay normal.** | **Comprovado** | `telemetria_a30b79d_diagnostico.log`: em **65 de 70 janelas** (`f >= 390`), `fs_total = 0` e `xa_frames = 0/2100`; mesmo assim, a mediana permaneceu em `3.039.200` ticks/quadro (`15,63 FPS`). |
| **Variação de cena/rota (`avg_tris`) explica grande parte da diferença aparente entre sessões de FPS médio:** normalizando por `avg_tris`, `a30b79d_completo` roda a **`19,82 FPS` @ 750 tris** e **`15,60 FPS` @ 1000 tris**, enquanto `HEAD` roda a **`22,04 FPS` @ 750 tris** e **`17,12 FPS` @ 1000 tris**. | **Comprovado** | Regressão linear sobre `[bench]` (`cc > 32`): `completo` (`mean_tris=995`, `445.579 + 2559,8 * tris`), `HEAD` (`mean_tris=944,4`, `293.525 + 2443,9 * tris`). |
| **As 17 funções quentes em `0x80000400..0x80008000` possuem exatamente as mesmas instruções em `a30b79d` e `a30b79d_completo`** (apenas imediatos de relocação `.data`/`.bss`/`$gp` mudaram). | **Comprovado** | Descompressão LZ4 e diff binário dos ELFs extraídos de `d2_n64_a30b79d.z64` e `d2_n64_a30b79d_completo.z64`. |
| **O gargalo dominante que impede 30 FPS (33,3 ms = 1,56 M ticks) é o pipeline de submissão RSP/RDP (`[prims]` = 22,1–26,0 ms/quadro), agravado por 306–645 trocas de overlay RSP por quadro (`ovl_sw`), `draw_ground_perspective` no CPU/RDPQ e buffer RSPQ padrão de apenas 2 KB (`0x200`) no SDK instalado.** | **Comprovado** | `[prims]` em `lpk1`/`diagnostico`, `[rsp_ovl] ovl_sw=9.192..19.364` por 30 quadros, inspeção de `psyx_rdp.cpp` e `/opt/libdragon/mips64-elf/include/rspq_constants.h`. |
| **`hot.ld` excede o cache de instruções de 16 KB (31.756 B empacotados sem `d2_hotpad.cpp`), causando colisões diretas de conjunto entre `tri_core`, `d2_direct_poly`, `rsp_ft4` e `upload_region` (9.900 B); além disso, 3 símbolos de `hot.ld` não casam no `HEAD`.** | **Comprovado** | Análise de `build_d2/d2_n64.elf` com `mips64-elf-nm` e `hot.ld`. |
| **Deslocamentos de `.bss` (`+51.976 B` no `completo`, `+16.656 B` no `lpk2`) alteram o alinhamento de D-cache (8 KB direto) e o início do heap, podendo explicar oscilações de ~100–140k ticks/quadro e maior espera em `d2_vram_r_barrier`.** | **Indício forte** | `telemetria_lpk1.log` (`2,62 M` ticks) vs `telemetria_lpk2.log` (`2,76 M` ticks, `[wait]` site 1 de `316k` para `708k` ticks/30q) sem acessos ao LEVPACK após `f=600`. |
| **Picos de carregamento do LEVPACK (até `149,3 ms` numa janela de 30 quadros) vêm de decodificação LZ4 em C byte-a-byte via `stdio` (`fseek`/`fread`) e perda de localidade de setores após deduplicação global.** | **Comprovado** | `[content]` em `diagnostico.log`, `[lpk]` em `lpk1.log` (`io_ticks=25,4M`, `decode_ticks=19,6M` na janela 2), `fs_n64.cpp`, `pack_levels.py`. |
| **O cálculo antigo `livre_aprox = 8 MB - uordblks` subestima gravemente o uso de memória; a margem real total (heap livre + pilhas) é de apenas ~254 KiB (`HEAD`) a ~385 KiB (`completo`).** | **Comprovado** | `PH[2].p_memsz` dos ELFs (`6,56–6,75 MiB` estáticos) + `mallinfo().uordblks` (`~1,01 MiB`). |
| **Descartado como causa da queda contínua de FPS:** decodificação de FMV em gameplay, decodificação ULC de XA (inativa: `0/2100` quadros), e tamanho da ROM em si (64 MiB). | **Descartado para o cenário medido** | `telemetria_a30b79d_diagnostico.log` (`xa_frames=0/2100`, `fs_total=0` em 65/70 janelas). |

---

## 2. Inventário Verificado: ROMs, ELFs, Logs e Commits

### 2.1 ROMs verificadas em disco (`D2_N64`)
| Arquivo | Tamanho (bytes) | Data | SHA-256 |
| :--- | ---: | :--- | :--- |
| `d2_n64_a30b79d.z64` | 47.824.896 | 2026-11-14 09:37:44 | `F46106D664E8EDE5F6A270E15E6599283FEE98F913B93C4D5FB07DEDEF65FE58` |
| `d2_n64_a30b79d_completo.z64` | 64.454.656 | 2026-11-14 11:19:50 | `90E97EC8216D7885B28EC9ABDA07422BC296C29BCB78DEC3CB1CC5BAFF2B0CE7` |
| `d2_n64_a30b79d_diagnostico.z64` | 64.454.656 | 2026-11-14 11:33:55 | `478B50B07C18AE9AF61A298C3003230DBA9BB71E2E46934A65CA2637FB3AD899` |
| `d2_n64.z64` | 64.536.576 | 2026-11-14 11:02:45 | `FD97EFFA2F0DE0305860A7551C9A43A0D1BF3380C913D36CA9081D6F28BF1A1A` |

### 2.2 Segmentos ELF extraídos do payload das ROMs (`offset 0x1e00`, comprimido via `n64elfcompress`/LZ4)
| Variante | Entry | Comprimido (`PH[2].filesz`) | Descomprimido (`.text+.rodata+.data+.sdata`) | `PH[2].p_memsz` (incl. `.sbss+.bss`) | `.sbss+.bss` | `_end` estimado |
| :--- | :--- | ---: | ---: | ---: | ---: | :--- |
| `a30b79d` | `0x800084e4` | 860.509 B | 1.235.752 B | 6.879.512 B | 5.643.760 B | `0x8068fc98` (6,56 MiB) |
| `completo` | `0x800084e4` | 871.099 B | 1.250.880 B (`+15.128 B`) | 6.946.616 B (`+67.104 B`) | 5.695.736 B (`+51.976 B`) | `0x806a02b8` (6,63 MiB) |
| `diagnostico` | `0x800084e4` | 872.688 B | 1.253.200 B (`+2.320 B`) | 6.949.000 B (`+2.384 B`) | 5.695.800 B (`+64 B`) | `0x806a0c08` (6,63 MiB) |
| `head` (`d2_n64.z64`) | `0x80007fa0` | 952.160 B | 1.357.592 B (`+106.712 B` vs completo) | 7.078.264 B | 5.720.672 B | `0x806c0578` (6,75 MiB) |

### 2.3 Histórico Git relevante
- `a30b79d` (10/11/2026): Referência histórica de desempenho (`GAME_OPT ?= -Os`, `O2_LIST = draw mdraw map cell tile models sky shadow overlay`, `CITIES = CHICAGO`, `FMV_IDS = 0`).
- `49edd99`: Integração completa (4 cidades, 33 FMVs, ULC, LEVPACK, `GAME_OPT ?= -O2`, multi-passo de lógica, `d2_t3d_cars.inc`).
- `041e9d1`: Remove `d2_t3d_cars.inc` e restaura `plotCarPolyGT3` de `a30b79d`.
- `3d7adb3` / `aaf4975`: Adiciona cache LRU opcional (`LPK_CACHE_BLOCKS=1..4`) e perfil `[lpk]` (`LPK_PROFILE=1`).
- `b6d40bf` / `84d5ac9` (`HEAD`): Scripts isolados (`prepare_ab_a30b79d_completo.py`, `instrument_ab_content.py`) que geraram `d2_n64_a30b79d_completo.z64` e `d2_n64_a30b79d_diagnostico.z64` alterando apenas 8 arquivos sobre `a30b79d`.

---

## 3. Análise Comparativa da Telemetria Existente

### 3.1 Resumo das execuções medidas no Ares (paraLLEl-RDP)
| Log | Intervalo `[fps]` | FPS Global (`Δf*60/Δvbl`) | Mediana `[cyc]` (ticks/q) | Regressão `[bench]` (`ticks = a + b*tris`) | FPS @ 750 tris | FPS @ 1000 tris |
| :--- | :--- | ---: | ---: | :--- | ---: | ---: |
| `telemetria_a30b79d_completo.log` | `f=390..780` | **15,57 FPS** | 3.034.377 (64,73 ms) | `445.579 + 2559,8 * tris` (`n=11`) | **19,82 FPS** | **15,60 FPS** |
| `telemetria_a30b79d_diagnostico.log` | `f=390..2490` | **15,63 FPS** | 3.039.200 (64,84 ms) | `819.441 + 2210,7 * tris` (`n=66`) | **18,92 FPS** | **15,47 FPS** |
| `telemetria_lpk1.log` (`HEAD`, 1 bloco) | `f=390..2940` | **17,96 FPS** | 2.611.227 (55,71 ms) | `326.212 + 2422,6 * tris` (`n=72`) | **21,87 FPS** | **17,05 FPS** |
| `telemetria_lpk2.log` (`HEAD`, 2 blocos) | `f=390..2760` | **17,08 FPS** | 2.803.659 (59,81 ms) | `347.910 + 2560,0 * tris` (`n=74`) | **20,67 FPS** | **16,12 FPS** |
| `telemetria.log` (`HEAD`, perfil off) | `f=390..2940` | **18,06 FPS** | 2.587.941 (55,21 ms) | `293.525 + 2443,9 * tris` (`n=80`) | **22,04 FPS** | **17,12 FPS** |

### 3.2 Decomposição do tempo por quadro (`[prims]`, `[custo]`, `[content]`, `[content_audio]`)
Comparando janelas de gameplay (excluindo os primeiros 300 quadros de menu/carga):
- **`telemetria_a30b79d_diagnostico.log`** (mediana `3.039.200` ticks = **64,84 ms**):
  - `[prims]` (despacho da OT em `d2_native_dispatch`): `chao=372.098` (`n=119`), `ft4=466.621` (`n=216`), `direto=380.146` (`n=235`) -> **1.218.865 ticks/quadro (26,00 ms / 40,1%)**.
  - `[custo]` (travessia/submissão no motor): `carros=316.530`, `predios=181.993`, `tiles=282.365` -> **780.888 ticks/quadro (16,66 ms / 25,7%)**.
  - `[content_audio]`: `callback=0,7672 ms`, `mix=1,8651 ms` (`xa_frames=0/2100`) -> **~123.389 ticks/quadro (2,63 ms / 4,1%)**.
  - `[content]` (LEVPACK): média **0,1778 ms/quadro** (`zero_fs=65/70` janelas; pico máximo `149,30 ms` em janela de loading).
  - Restante (lógica `StepGame`/IA/física + `PsyX_EndScene` + esperas): **~916.000 ticks/quadro (19,5 ms / 30,1%)**.
- **`telemetria_lpk1.log` (`HEAD` com `-O2`)** (mediana `2.611.227` ticks = **55,71 ms**):
  - `[prims]`: `chao=237.788` (`n=84`), `ft4=493.337` (`n=241`), `direto=306.228` (`n=176`) -> **1.037.353 ticks/quadro (22,13 ms)**.
  - `[custo]`: `carros=158.796`, `predios=175.254`, `tiles=134.889` -> **468.939 ticks/quadro (10,00 ms)**.
  - Comparação em posição idêntica (`pos ~ 14389,142744`, `~1040 tris`): `a30b79d_completo` levou `3.527.084` ticks (`13,33 FPS`) vs `lpk1` com `2.587.619` ticks (`18,11 FPS`).

**Conclusão sobre a regressão histórica:**
1. A queda da versão `HEAD` pré-`041e9d1` (`15,69 FPS`) foi fortemente causada por `d2_t3d_cars.inc` (já revertido em `041e9d1`, subindo para `~18,0 FPS`).
2. Na base `a30b79d_completo`, `GAME_OPT ?= -Os` penaliza pesadamente `cars.c` e código de jogo fora da `O2_LIST` (`-O2` no `HEAD` é ~2,4 FPS mais rápido em cenas densas).
3. Parte da diferença `20,02 FPS` (`a30b79d`) vs `15,6 FPS` (`a30b79d_completo`) reflete diferença de `avg_tris` entre sessões (não alinhadas por `CameraCnt`, como já alertado em `PERFORMANCE_BASELINE.md`), somada a eventuais efeitos de alinhamento de `.bss`/D-cache e overhead fixo de áudio/callbacks.

---

## 4. Análise Binária, ELF, `$gp`, `hot.ld` e Cache (I$ / D$)

1. **Invariância das funções quentes em `a30b79d` vs `completo`:**  
   Nos primeiros 128 KB (`0x80000400..0x80020400`), das 1.367 palavras que diferem entre `a30b79d.bin` e `completo.bin`, **1.351 têm exatamente o mesmo opcode**, alterando apenas deslocamentos `$gp` (`_gp` moveu de `0x80135508` para `0x80139000`, `+0x3AF8`) e endereços altos de `.data`/`.bss`. Todas as funções de `hot.ld` permaneceram nos mesmos endereços.
2. **Saturação do I-cache (16 KB, mapeamento direto, linhas de 32 B) por `hot.ld`:**  
   No `build_d2/d2_n64.elf`, os símbolos casados por `hot.ld` somam **31.756 bytes** (`1,94x` a capacidade do I-cache), empacotados de forma contígua em `0x80000400..0x80007f88` **sem `d2_hotpad.cpp`**.  
   - `tri_core` (`0x80003380`, `5.464 B`, sets `[412..70]`) colide diretamente nos mesmos conjuntos de cache que `vc_proj` (`sets 32..51`) e `rsp_ft4` (`sets 52..101`).
   - `d2_direct_poly` (`0x800048e0`, `6.980 B`, sets `[71..289]`) colide diretamente com `rsp_ft4` (`sets 71..101`) e `upload_region` (`sets 102..411`, **9.900 B** sozinha!).
   - **3 símbolos de `hot.ld` falharam o casamento no `HEAD`:** `d2_native_ft4.part.0` (virou `d2_native_ft4_inner.part.0`), `_ZL15upload_pal_banki` (dividido em inline + `upload_pal_bank_slow`), e `_ZL16track_ovl_switchm.part.0`. Além disso, `memset` está em `0x80023740` (por conta de `--wrap=memset` / `__wrap_memset`).
3. **Efeito de D-cache (8 KB direto) pelo deslocamento de `.bss`:**  
   `completo` adicionou `+51.976 B` em `.bss` (`lpkCache`, `lpkBlocks`, `lpkMapBuf`, `lpkFiles`) e `lpk2` adicionou `+16.656 B` em `fs_n64.o` sobre `lpk1`. Como `lpk1` e `lpk2` rodaram zero acessos LEVPACK após `f=600`, a diferença de `~139k` ticks/quadro e o dobro de espera em `d2_vram_r_barrier` (`316k` -> `708k` ticks/30q) apontam para variação de rota somada a possível conflito de D-cache/alinhamento de buffers dinâmicos após `_end`.

---

## 5. Análise do LEVPACK e Streaming (`pack_levels.py` + `fs_n64.cpp`)

1. **Perda de localidade pós-deduplicação:** `pack_levels.py` deduplica setores de 2048 B entre 94 arquivos (117,9 MB -> 34,0 MB, 26.889 setores únicos de 57.598, 3.362 blocos de 16 KB) na ordem em que aparecem nos arquivos. Um arquivo carregado posteriormente (ex.: `RIO.LEV` ou `VEGAS.LEV`) referencia setores cuja primeira ocorrência estava em `CHICAGO.LEV`, espalhando leituras sequenciais por blocos distantes no `LEVPACK.BIN`.
2. **Cache de 1 bloco (`16 KiB`):** Em `telemetria_lpk1.log` (janela 2, `f=300..600`), houve `767 hits` e `140 misses` com **`140 evictions` e `139 decodes`**, custando `io_ticks=25.422.464` (542 ms) + `decode_ticks=19.644.310` (419 ms) = **~0,96 s** numa janela de 300 quadros.
3. **Caminho lento de I/O e descompressão:** `fs_n64.cpp` usa `fopen`/`fseek`/`fread` (camada `stdio`/Newlib) em vez de `dfs_open`/`dfs_read` ou `dma_read` direto da ROM, e usa `lz4_decode()` em C byte-a-byte com `memmove`/`memcpy` por token em vez do decodificador LZ4 em assembly MIPS já presente na `libdragon` (ou rotina MIPS otimizada por palavras).

---

## 6. Análise de Áudio, ULC, FMV e VBlank

1. **Custo medido em `diagnostico.log`:** Com `xa_frames = 0/2100`, `d2_sys_pump_vbl_cb()` consumiu `0,767 ms/quadro` e `mixer_try_play()` consumiu `1,865 ms/quadro` (**2,63 ms/quadro** total). Esse valor é pequeno frente aos 64,8 ms do quadro, mas representa **~7,9% do orçamento de 30 FPS (33,3 ms)**.
2. **Spin-loop de `d2_sys_wait_vbl_until`:** Em telas de menu/loading, `d2_sys_wait_vbl_until` chama `d2_audio_pump()` em laço apertado (registrando até **1.495.576 chamadas** em 30 quadros).
3. **`d2_audio_mix()` dentro de `d2_native_draw_ot`:** `psyx_rdp.cpp:2152` chama `d2_audio_mix()` (`mixer_try_play()`) a cada bloco de 64 slots da OT. Como o mixer usa o RSP (`rsp_mixer`), intercalar chamadas ao mixer no meio do desenho da OT pode competir com o overlay gráfico ativo se o buffer de áudio precisar de mixagem imediata.
4. **ULC (`wav64_init_compression(2)`):** Conforme documentado por PhobosLab (`n64-ulc-codec`), o decodificador ULC no RSP consome ~4% de CPU e ~5% de RSP para áudio estéreo 44,1 kHz. Quando uma faixa XA (ULC) estiver tocando ativamente, somará esse custo sobre os 2,63 ms já medidos para VADPCM (SFX + música).

---

## 7. Análise do Pipeline de Renderização (O Principal Gargalo para 30 FPS)

Os números de `telemetria_lpk1.log` revelam que **`[prims]` consome 22,1 ms/quadro (~1.065 ticks CP0 = ~2.130 ciclos de CPU por triângulo!)**:

1. **Tempestade de trocas de overlay RSP (`ovl_sw = 306..645` por quadro):**  
   `[rsp_ovl]` registrou `9.192` a `19.364` trocas de overlay a cada 30 quadros!
   - Em `upload_win` (`psyx_rdp.cpp:711-723`), cada miss de textura/TLUT alterna `s_d2gpuOvl` -> `0` (`rdpq_set_texture_image_raw`) -> `s_d2gpuOvl` (`D2G_CMD_LOAD_TILE`) -> `0` (`rdpq_load_tlut_raw`) -> `s_d2gpuOvl` (`D2G_CMD_POLY_FT4`).
   - Em `draw_ground_perspective` (`psyx_rdp.cpp:1770-1835`), **cada quad de chão (`84–119` por quadro)** alterna para o overlay `RDPQ_OVL_ID` (`0`), chama `upload_region` (a função de 9.900 B!), emite `rdpq_mode_persp(true)` + 6 vértices `RDPQ_CMD_TRIANGLE_DATA` + 2 `RDPQ_CMD_TRIANGLE` + `rdpq_mode_persp(false)`, custando **2.831–3.127 ticks (~60–67 µs) por quad** (`5,1–7,9 ms/quadro` no total).
   - Em `d2_t3d_draw_world_until` (`d2_t3d_world_order.inc:110,128`), cada lote intercalado com a OT chama `d2_rdp_invalidate_backend_cache()`, forçando re-sincronização e reenvio de estado/textura no retorno ao backend PSX.
2. **Patch da fila RSPQ NÃO aplicado ao SDK instalado:**  
   `tools/libdragon-rspq-buffer.patch` aumenta `RSPQ_DRAM_LOWPRI_BUFFER_SIZE` de `0x200` (2 KB) para `0x1000` (16 KB). Porém, `/opt/libdragon/mips64-elf/include/rspq_constants.h` ainda está com **`0x200` (512 palavras) e `RSPQ_DEBUG = 1`**. Um buffer de 2 KB estoura dezenas de vezes por quadro, reduzindo o paralelismo CPU/RSP e aumentando travamentos.
3. **Barreira de VRAM (`d2_vram_r_barrier`):**  
   `[wait]` site 1 (`d2_vram_r_barrier`) bloqueou o CPU com `rspq_wait()` por `316.433` a `707.620` ticks por 30 quadros sempre que o jogo atualizou texturas na VRAM antes do RDP concluir a leitura do quadro anterior.
4. **HUD de FPS por `rdpq_fill_rectangle`:**  
   `PsyX_EndScene` (`1036-1066`) executa até 22 chamadas `rdpq_fill_rectangle` + `joypad_get_buttons_held` por quadro quando `fpsOn` está ativo.

---

## 8. Orçamento Real de RAM (8 MiB RDRAM)

> **Nota:** `livre_aprox = 8 MB - mallinfo().uordblks` (~7,33 MB) era incorreto porque ignorava toda a imagem estática (`.text` + `.data` + `.bss`).

| Componente (RDRAM = 8.388.608 B / 8 MiB) | `a30b79d` | `a30b79d_completo` | `HEAD` (`d2_n64.z64`) |
| :--- | ---: | ---: | ---: |
| Imagem estática (`0x80000400 + PH[2].p_memsz`) | 6.880.408 B (6,56 MiB) | 6.947.512 B (6,63 MiB) | 7.079.160 B (6,75 MiB) |
| Principais buffers em `.bss` (`HEAD`): | | | |
| - `g_allocatedMem` | — | — | 1.310.720 B (1,25 MiB) |
| - `d2_vramR` + `d2_vramLc` + `d2_vramL` (3x 512 KiB) | — | — | 1.572.864 B (1,50 MiB) |
| - `g_Overlay/Other/PrimTab` (5x 320 KiB) + `g_Replay` (64 KiB) | — | — | 1.703.936 B (1,625 MiB) |
| - `s_t3d_vtx_pool` + `mat_pool` + `order_faces` | — | — | 233.472 B (228 KiB) |
| - `d2_otbits` (128 KiB) + `g_OT1/2` (66 KiB) | — | — | 198.656 B (194 KiB) |
| - `s_palCache` + `s_palRing` + `d2_clut_map/pool` | — | — | 196.608 B (192 KiB) |
| - `lpkCache` + `lpkBlocks` + `lpkMapBuf` + `lpkFiles` | 0 B | ~33 KiB | ~50 KiB (`LPK=2`) / ~33 KiB (`LPK=1`) |
| Heap alocado em runtime (`mallinfo().uordblks`) | ~1.055.000 B | ~1.055.248 B | ~1.055.360 B (1,01 MiB) |
| **Margem real restante (heap livre + pilhas)** | **~453 KiB** | **~386 KiB** | **~254 KiB** |

Com apenas **~254 KiB** livres no `HEAD`, qualquer cache adicional (ex.: `LPK_CACHE_BLOCKS=4` = +49 KiB) exige recuperação prévia de `.bss` ou consolidação de buffers antes de ser ativado por padrão.

---

## 9. Diferenças Ares (paraLLEl-RDP) vs. Hardware Real N64

1. **Paralelismo CPU / RSP / RDP:** O paraLLEl-RDP rasteriza em Vulkan na GPU do PC muito mais rápido que o RDP real (25–50 MHz fillrate efetivo compartilhado com RDRAM). Em hardware real, **overdraw** (camadas de chão + prédios + sombras) e **trocas de TMEM/TLUT** podem tornar o RDP o limitante se não houver batching.
2. **Latência de RDRAM e Cache:** O VR4300 real tem penalidade severa de cache miss em RDRAM (~30–40 ciclos por linha); colisões de I-cache (`hot.ld` de 31,7 KB) e D-cache (`+52 KB` `.bss`) tendem a ser **mais caras no console real** do que no Ares.
3. **PI DMA e Cartucho:** O Ares praticamente não penaliza latência de ROM/PI DMA como um flashcart real (EverDrive/SummerCart64), tornando o streaming LEVPACK e o custo de `fread` ainda mais críticos no hardware.

---

## 10. Pesquisa Externa em Fontes Primárias

| Projeto / Fonte | Mecanismo Verificado | Aplicabilidade ao Driver 2 N64 | Risco |
| :--- | :--- | :--- | :--- |
| **`ToxyCola/Half-Life-Nintendo-64-Port`** | Patch `libdragon-rspq-buffer.patch` (`RSPQ_DRAM_LOWPRI_BUFFER_SIZE = 0x1000`, +30% FPS em mapas grandes), PVS/frustum culling agressivo, ULC para áudio/música, triangle strips. | **Imediata:** aplicar `RSPQ_DRAM_LOWPRI_BUFFER_SIZE = 0x1000` (e `RSPQ_DEBUG = 0` em release) no `/opt/libdragon` usado no build. | Baixo (+~112 KB RAM para buffers RSPQ; validar com margem atual de 254 KiB). |
| **`JackTulli/halo-64`** | Orçamento rigoroso de RDRAM/Z-buffer, compactação de memória de modelos/atores, batching de comandos RSPQ/Tiny3D para evitar stalls. | **Alta:** consolidar buffers estáticos (ex.: `d2_vramL`/`Lc` e `PrimTab` sobrando) para recuperar >256 KiB de RAM. | Médio (exige revisão de tamanhos máximos de OT/VRAM). |
| **`n64brew/libdragon`** | Decodificador **LZ4 em assembly MIPS**, leitura assíncrona/direta via `dfs_read` / `dma_read`, `wav64_init_compression` (ULC). | **Alta:** substituir `lz4_decode()` em C + `stdio` em `fs_n64.cpp` por `dma_read` + LZ4 em assembly MIPS. | Baixo. |
| **`HailToDodongo/tiny3d`** | Cache de vértices no RSP (até 70 vértices), layout intercalado 16 B (`T3DVertPacked`), otimização de vertex cache / triangle strips, RSPL com reordenação. | **Alta:** mover `draw_ground_perspective` para comando dedicado no `rsp_d2gpu` (ou lote único sem troca de overlay por quad). | Médio. |
| **`phoboslab.org/log/2026/11/n64-ulc-codec`** | Codec ULC em ~1.400 linhas de assembly RSP (~4% CPU, ~5% RSP em 44,1 kHz estéreo). | **Validado:** confirma que ULC não é gargalo quando inativo e tem custo previsível/baixo quando ativo. | Baixo. |
| **`OpenLara` / `Portal 64` / `Quake 64`** | Minimizar cargas de TMEM (4 KB), agrupar por textura/estado, evitar `rspq_wait()` mid-frame, reduzir overdraw. | **Alta:** eliminar as 306–645 trocas de overlay/quadro e eliminar `d2_vram_r_barrier` síncrono. | Médio. |

---

## 11. Plano de Ação Priorizado (Recuperar ~20+ FPS -> Rumo a 30 FPS Estáveis)

| Fase | Ação Técnica | Alvo / Ganho Esperado | Impacto em RAM / Risco |
| :--- | :--- | :--- | :--- |
| **1. Rápido & Seguro (Build/SDK)** | Aplicar `libdragon-rspq-buffer.patch` (`0x1000`, e testar `RSPQ_DEBUG=0` em release) no `/opt/libdragon`; corrigir `hot.ld` (atualizar nomes `d2_native_ft4_inner.part.0`, `upload_pal_bank_slow`, `__wrap_memset`), dividir `upload_region` (9.9 KB) em caminho rápido CI4 vs fallback frio, e regenerar `hot.ld` + `d2_hotpad.cpp` dentro de **16 KB** (ou <=14 KB) no `-O2` atual. | **+2 a +4 FPS** (reduz stalls de fila RSPQ e elimina thrashing de I-cache). | Médio (`0x1000` usa ~112 KB extras; compensar na Fase 3). |
| **2. Eliminar Trocas de Overlay RSP (`ovl_sw`)** | **(a)** Mover `D2G_CMD_SET_TEX_IMAGE` / `LOAD_TLUT` para dentro do `rsp_d2gpu.S` (ou emitir via `rdpq` apenas quando mudar de fato fora do loop) para acabar com as 3 trocas de overlay por `upload_win`. **(b)** Implementar `D2G_CMD_GROUND_QUAD` (perspectiva + divisão `1/d` no RSP ou lote único no `rdpq` sem `rdpq_mode_persp(true/false)` por quad). | **-6 a -10 ms/quadro** (`draw_ground_perspective` + `ovl_sw` caem de 645/q para <20/q). | Baixo em RAM; médio em complexidade de microcode RSP. |
| **3. Recuperar Margem de RAM (>512 KiB livres)** | Auditar e enxugar `.bss` estática: `d2_vramL` + `d2_vramLc` (1 MiB) podem compartilhar/reduzir se o caminho nativo já não precisar da cópia completa; revisar `g_Overlay_buffer` / `g_Other_buffer2` (640 KiB) e `s_t3d_vtx_pool`/`mat_pool` se reduzíveis sem perda visual. | Libera **+300 a +600 KiB** para heap, buffers RSPQ `0x1000`, e cache LEVPACK seguro. | Baixo/Médio (validar saves, FMVs e cidades). |
| **4. Acelerar LEVPACK & Eliminar Picos** | Em `pack_levels.py`: agrupar setores por cidade/nível (ou ordenar o pool por frequência/localidade do `.LEV`) em vez de ordem global cega. Em `fs_n64.cpp`: trocar `stdio` por `dma_read` direto da ROM + LZ4 em assembly MIPS da `libdragon`, e usar 2–4 blocos de cache após Fase 3. | Reduz picos de loading (`149 ms` -> **<30–40 ms**). | Baixo (após Fase 3). |
| **5. Áudio & Sincronização** | Em `d2_vram_r_barrier`, usar double-buffering de páginas dinâmicas de VRAM em vez de `rspq_wait()` síncrono; limitar `d2_audio_mix()` na OT para não interromper lotes `rsp_d2gpu` continuamente; evitar spin-loop agressivo em `d2_sys_wait_vbl_until`. | **-1 a -2 ms/quadro** + menos jitter. | Baixo. |

---

## 12. Validações Necessárias e Próximos Passos

1. **Alinhamento de rota para A/B:** Usar `CameraCnt` + `pos` idênticos (ou replay determinístico gravado) ao comparar builds, evitando confundir variação de `avg_tris` (750 vs 1000) com regressão de código.
2. **Medir em hardware real (ou com perfilador de RDP/I-cache):** Validar após cada fase que nenhuma cidade (`CHICAGO`, `HAVANA`, `RIO`, `VEGAS`), FMV (33), música, SFX, save SRAM ou carro especial quebrou, e que `d2_n64.z64` permanece `<= 64 MiB`.
