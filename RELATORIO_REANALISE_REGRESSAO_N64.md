# Reanálise Crítica: Regressão de Desempenho entre `a30b79d` e `a30b79d_completo` (Driver 2 N64)

> **Escopo e restrições:** Documento produzido em modo estritamente somente leitura sobre os binários, ELFs, logs, manifestos e fontes existentes em `C:\Desenvolvimento\Nintendo 64\Driver 2 64\D2_N64`. Nenhum código, SDK, histórico Git ou ROM foi alterado, e nenhuma nova compilação ou execução foi gerada. O relatório anterior ([RELATORIO_INVESTIGACAO_REGRESSAO_N64.md](file:///C:/Desenvolvimento/Nintendo%2064/Driver%202%2064/D2_N64/RELATORIO_INVESTIGACAO_REGRESSAO_N64.md)) foi preservado integralmente.

---

## 1. Sumário Executivo

### A pergunta central e a resposta direta

**Pergunta:** *Por que a build histórica original `a30b79d` (`d2_n64_a30b79d.z64`) registrou ~20 FPS, enquanto `d2_n64_a30b79d_completo.z64` (compilada com a mesma base, mesmas flags de compilação e mesmo renderizador) registrou ~15,6 FPS?*

**Resposta direta com base nas evidências disponíveis:** **A causa exata dessa queda específica permanece `Inconclusivo` nos artefatos existentes**, principalmente porque **não existe nenhum arquivo de log (`telemetria_a30b79d.log` ou equivalente) da execução de `d2_n64_a30b79d.z64`** no workspace. Sem a telemetria por janela (`pos`, `avg_tris`, `ticks`, `ft4_*`, `ovl_sw`) da execução original de `a30b79d`, não é possível alinhar espacialmente a cena dos ~20 FPS com a cena dos ~15,57 FPS de `a30b79d_completo`.

O relatório anterior ([RELATORIO_INVESTIGACAO_REGRESSAO_N64.md](file:///C:/Desenvolvimento/Nintendo%2064/Driver%202%2064/D2_N64/RELATORIO_INVESTIGACAO_REGRESSAO_N64.md)) falhou em responder à pergunta central porque:
1. **Substituiu a comparação principal** (`a30b79d` vs `a30b79d_completo`) pela comparação secundária (`a30b79d_completo` vs `HEAD` / `lpk1` / `lpk2`), onde existem dezenas de outras mudanças de renderização e layout.
2. **Classificou como "comprovadas" hipóteses sem cadeia causal fechada** (como *thrashing* de `ovl_sw`, contenção de `libdragon.a` com buffers `0x200` e impacto de cache/layout), misturando problemas gerais do port — que **já existiam igualmente na `a30b79d` original de ~20 FPS** — com possíveis causas da regressão introduzida pela integração de conteúdo.
3. **Extrapolou ganhos numéricos sem medição** (`+2-4 FPS`, `-6-10 ms`, metas `<20 ovl_sw`, `<30-40 ms`) e calculou incorretamente o custo de memória de `RSPQ_DRAM_LOWPRI_BUFFER_SIZE` (`+112 KiB` em vez dos `+28 KiB` reais).

O que esta reanálise **comprova** e **descarta** objetivamente:
- **Descartado para o cenário medido:** Descompressão ativa de `LEVPACK` em tempo real (`0,178 ms/quadro` em média no gameplay; `0` em 65 de 70 janelas) e reprodução ativa de áudio `XA`/`ULC` (`0/2.100` quadros no gameplay) **não** causam os ~15,6 FPS contínuos.
- **Comprovado como idêntico entre `a30b79d` e `a30b79d_completo`:** Ambas usam a mesma `libdragon.a` com buffers `rspq_init_context` de `2.048 B` (`0x200` palavras), watermark de `1.800 B` e `RSPQ_DEBUG=1` ativo; ambas usam o mesmo compilador/flags (`-Os` + `O2_LIST`), o mesmo `hot.ld` e o mesmo renderizador.
- **Comprovado como diferente entre `a30b79d` e `a30b79d_completo`:**
  1. `.text`+`.data` descomprimidos aumentaram `+15.128 B` (`1.235.752 B -> 1.250.880 B`), deslocando endereços de código (I-cache direta de 16 KiB) e `_gp` (`0x80135508 -> 0x80139000`).
  2. `.bss` aumentou `+67.104 B` (`memsz`: `6.879.512 B -> 6.946.616 B`), sendo `+15.168 B` antes de `fs_n64.o` e **`+51.936 B` inseridos por `fs_n64.o`** — criando um deslocamento relativo de `2.784 B mod 8.192 B` (**174 conjuntos** na D-cache direta de 8 KiB) entre os buffers anteriores a `fs_n64.o` (`d2_vramLc`, `d2_vramR`, `d2_vramL`, `d2_clut_*`) e os posteriores (`ot`, `s_palCache`, `s_palRing`, `s_order_faces`, `s_t3d_*`, `Mixer`).
  3. Diferença de percurso/cena não verificável para `a30b79d`: nas medições disponíveis, `avg_tris` explica `71,7%` a `91,6%` da variância de `ticks` (`R² = 0,717..0,916`), e diferenças de percurso (como visto em `lpk1` estacionado a `20–23,7 FPS` por 450 quadros) alteram fortemente a média global da sessão.

---

## 2. Inventário Verificado de Artefatos (ROMs, ELFs e Logs)

Todos os tamanhos, datas e hashes SHA-256 abaixo foram verificados diretamente no disco:

| Artefato | Tamanho (B) | Timestamp | SHA-256 | Observação |
|---|---:|---|---|---|
| `d2_n64_a30b79d.z64` | 47.824.896 | `2026-11-14 09:37:44` | `F46106D664E8EDE5F6A270E15E6599283FEE98F913B93C4D5FB07DEDEF65FE58` | Base histórica original |
| `d2_n64_a30b79d_completo.z64` | 64.454.656 | `2026-11-14 11:19:50` | `90E97EC8216D7885B28EC9ABDA07422BC296C29BCB78DEC3CB1CC5BAFF2B0CE7` | Mesma base + conteúdo completo |
| `d2_n64_a30b79d_diagnostico.z64` | 64.454.656 | `2026-11-14 11:33:55` | `478B50B07C18AE9AF61A298C3003230DBA9BB71E2E46934A65CA2637FB3AD899` | Mesma base + conteúdo + instrumentação |
| `d2_n64.z64` | 64.536.576 | `2026-11-14 11:02:45` | `FD97EFFA2F0DE0305860A7551C9A43A0D1BF3380C913D36CA9081D6F28BF1A1A` | Linha atual (`HEAD`) |

ELFs embutidos extraídos e descomprimidos a partir do offset `0x1e00` de cada ROM:

| ELF extraído | Entry | `PH[2]` comprimido | `PH[2]` descomprimido | `memsz` total | `_gp` |
|---|---:|---:|---:|---:|---:|
| `a30b79d.z64` | `0x800084e4` | 860.509 B | 1.235.752 B | 6.879.512 B | `0x80135508` |
| `a30b79d_completo.z64` | `0x800084e4` | 871.099 B | 1.250.880 B (`+15.128 B`) | 6.946.616 B (`+67.104 B`) | `0x80139000` |
| `a30b79d_diagnostico.z64` | `0x800084e4` | 872.688 B | 1.253.200 B | 6.949.000 B | — |
| `d2_n64.z64` (`HEAD`) | `0x80007fa0` | 952.160 B | 1.357.592 B (`0x14b718`, igual a `build_d2/d2_n64.elf`) | 7.078.264 B | — |

**Lacuna crítica confirmada:** Não existe `telemetria_a30b79d.log` (nem qualquer log bruto da execução de `d2_n64_a30b79d.z64`) em `D2_N64` ou `D2_N64_release`. Os únicos registros da `a30b79d` original estão em [PERFORMANCE_BASELINE.md](file:///C:/Desenvolvimento/Nintendo%2064/Driver%202%2064/D2_N64/PERFORMANCE_BASELINE.md) (`f=390..2580 -> 20,02 FPS` e o teste separado reportado com XA desligado: `~2,00 M` vs `~2,33 M` ticks/quadro).

---

## 3. Revisão das Afirmações do Relatório Anterior

Classificações adotadas: **Comprovado** | **Indício** | **Hipótese** | **Descartado para o cenário medido** | **Inconclusivo**.

| Afirmação em `RELATORIO_INVESTIGACAO_REGRESSAO_N64.md` | Classificação anterior | Classificação revisada | Correção e evidência objetiva |
|---|---|---|---|
| `LEVPACK` e `XA`/`ULC` ativos explicam os ~15,6 FPS contínuos | Descartado / secundário | **Descartado para o cenário medido** | Confirmado em `telemetria_a30b79d_diagnostico.log`: em 70 janelas de gameplay (`f >= 400`, `134.754,8 ms`), `fs_total` soma `373,28 ms` (`0,277%` do tempo, `0` em 65/70 janelas) e `xa_frames = 0` em todas as 83 janelas. |
| *Thrashing* de `ovl_sw` (`~11k/30q` = `~367/q`) força ~367 DMAs de microcódigo RSP por quadro | "Causa raiz comprovada" | **Hipótese** (mecanismo real no RSP não medido; interpretação do contador incorreta) | `track_ovl_switch` em [psyx_rdp.cpp](file:///C:/Desenvolvimento/Nintendo%2064/Driver%202%2064/D2_N64/port/n64/psyx_rdp.cpp#L84-L89) conta trocas de ID em CPU (`s_lastOvlSent`), e `rsp_d2gpu` é o único ucode custom registrado (`ovl_id=0x30000000`). O contador já era alto em `a30b79d_completo` (`avg=7.635,6/30q`, máx `19.940`) e em `diagnostico` (`avg=12.194,7/30q`). Não há contador de DMAs reais de overlay no RSP nem medição em `a30b79d` original. |
| Buffer `RSPQ` de `0x200` (`2.048 B`) e `RSPQ_DEBUG=1` em `libdragon.a` causam contenção | Apresentado como causa ativa da regressão | **Comprovado** (como característica do SDK); **Descartado** como diferencial `a30b79d -> completo` | O disassembly de `rspq_init_context` em `a30b79d.bin` (`0x800ee720`) e `completo.bin` (`0x800f0980`) prova que **ambas** as ROMs linkam exatamente o mesmo código: `li a0, 2048` (`0x800` B = `0x200` palavras) x 2 por contexto (`8 KiB` totais), watermark `1.800 B` e chamada a `debug_assert_func_f`. É um gargalo geral do port na base `a30b79d`, não uma mudança de `completo`. |
| Aumentar `RSPQ_DRAM_LOWPRI_BUFFER_SIZE` de `0x200` para `0x1000` custa `+112 KiB` | Afirmado como fato numérico | **Falso / Erro de cálculo** | `rspq_init_context` aloca `lowpri` + `highpri` para 2 contextos = 4 buffers. Com `0x200` palavras (`2.048 B` cada): `4 * 2.048 = 8 KiB`. Com `0x1000` palavras (`16.384 B` cada, caso ambos fossem alterados; o patch altera `LOWPRI`): `2 * 16.384 + 2 * 2.048 = 36 KiB`, um aumento de **`+28 KiB`** (ou `+56 KiB` se os 4 buffers fossem `16 KiB`), nunca `+112 KiB`. Os `128 KiB` são `rspq_rdp_dynamic_buffers` (`2 * 0x10000` em `rspq_init`), inalterados pelo patch. |
| `d2_vram_r_flush` / barreiras de cache após cada bloco VRAM explicam a perda | Apontado como fator importante sem isolamento | **Hipótese** (para custo absoluto); **Inconclusivo** para `a30b79d -> completo` | `psyx_backend.cpp` já executava `data_cache_hit_writeback` em `a30b79d`. Não há medição comparando chamadas/ciclos de `d2_vram_r_flush` entre `a30b79d` e `completo`. Além disso, remover barreiras sem mecanismo alternativo seguro corrompe leituras DMA do RDP. |
| Desalinhamento de I-cache / D-cache por inserção de `fs_n64.o` e crescimento de `.text`/`.bss` | Mencionado genericamente como "possível" | **Indício** (mudança física **Comprovada**; impacto no FPS **Inconclusivo**) | Comprovado nos binários: `fs_n64.o` inseriu `+51.936 B` em `.bss`, deslocando os símbolos posteriores em `174` conjuntos na D-cache direta (`8 KiB`) em relação a `d2_vram*`, e `.text` deslocou `+15.128 B` na I-cache direta (`16 KiB`). Porém, sem contadores de cache miss ou perfilagem em `a30b79d`, o efeito em ms/FPS não está isolado. |
| Diferença de cena / carga de triângulos (`avg_tris`) explica `~15,6` vs `~18` (`lpk1`) e possivelmente `~20` (`a30b79d`) | Minimizado ou ignorado na comparação com `a30b79d` | **Comprovado** para `completo` vs `lpk1`; **Inconclusivo** para `a30b79d` | A regressão `ticks vs avg_tris` (`cc > 32`) tem `R² = 0,916` em `completo` e `R² = 0,832` em `lpk1`. Ademais, `lpk1` passa `cc=32..480` estacionado a `20–23,7 FPS` antes de seguir a rota de Chicago de `completo`. Para `a30b79d`, a ausência do log impede verificar `avg_tris` e `pos`. |
| Ganhos projetados (`+2-4 FPS`, `-6-10 ms`, metas `<20 ovl_sw`, `<30-40 ms`) | Apresentados como estimativas técnicas | **Removido / Sem base empírica** | Números inventados por extrapolação; não derivam de nenhuma medição nos binários ou logs deste projeto. |

---

## 4. Comparação Justa de Cenários e Limitações Metodológicas

### A. Correlação `ticks` vs `avg_tris` nos logs existentes (`cc > 32`)

| Log | Janelas (`n`) | Faixa `avg_tris` | `R²` (`ticks ~ avg_tris`) | `RMSE` (ticks) | `RMSE` (ms) |
|---|---:|---|---:|---:|---:|
| `telemetria_a30b79d_completo.log` | 11 | `682..1.256` | `0,916` | 131.138 | 2,80 ms |
| `telemetria_a30b79d_diagnostico.log` | 66 | `702..1.362` | `0,717` | 187.081 | 3,99 ms |
| `telemetria_lpk1.log` | 72 | `665..1.228` | `0,832` | 160.400 | 3,42 ms |
| `telemetria_lpk2.log` | 74 | `652..1.267` | `0,808` | 179.807 | 3,84 ms |
| `telemetria.log` (`HEAD`) | 80 | `665..1.302` | `0,793` | 183.641 | 3,92 ms |

*Nota:* Em `a30b79d_completo`, apenas 11 janelas com `cc > 32` foram gravadas (`12` amostras de `avg_tris` no total), tornando a amostra curta (`~15,57 FPS` médios). Já `a30b79d_diagnostico` gravou 67 janelas (`avg_tris` médio `993,2` vs `1.004,7` em `completo`).

### B. Alinhamento espacial por `pos` (`completo` vs `diagnostico` vs `lpk1`)

O alinhamento por coordenada `pos` mostra:
1. **`completo` (`cc=64..384`) vs `diagnostico` (`cc=64..384`):** Mesma rota em Chicago, com distância espacial entre amostras pares de `0..990` unidades.
2. **`lpk1` (`cc=544..864`) vs `completo` (`cc=64..384`):** `lpk1` percorre exatamente a mesma rota (`dist=0..854`), **porém** somente após passar `cc=32..480` praticamente estacionado em `pos=(17387,142670)..(17608,141168)` rodando a **`20–23,7 FPS`**. Isso explica diretamente por que a média global de `lpk1` (`17,96 FPS`) parece muito superior à de `completo` (`15,57 FPS`) sem refletir ganho equivalente no mesmo trecho da cidade.
3. **`a30b79d` original:** Sem `telemetria_a30b79d.log`, **não há coordenadas `pos` nem `avg_tris` por janela** para saber se a execução de `20,02 FPS` (`f=390..2580`) percorreu exatamente o mesmo trajeto de `completo` ou permaneceu mais tempo em trechos leves.

---

## 5. Análise dos Mecanismos Concretos

### 1. `fs_n64.cpp`, `LEVPACK` e busca de arquivos (`lpk_find` / `lpk_block`)
- **Evidência medida (`diagnostico.log`):** Em 70 janelas de gameplay (`f >= 400`, `134.754,8 ms`), `fs_total = 373,28 ms` (`0,277%` do tempo total, `0,178 ms/quadro` em média), com **65 das 70 janelas registrando `fs_total = 0`**. No total da sessão (83 janelas, `167.256,7 ms`), `fs_total = 92.208.901` ticks (`1,18%`).
- **Conclusão:** A descompressão/leitura ativa do `LEVPACK` está **Descartada** como causa da queda contínua de ~20 para ~15,6 FPS. (O impacto real ocorreu indiretamente no layout de memória — ver item 5).

### 2. `fmv_n64.cpp`, `psyx_backend.cpp`, áudio e estado persistente pós-FMV/menu
- **XA/ULC ativo:** `xa_frames = 0` em todas as 83 janelas de `diagnostico.log`. **Descartado** como custo ativo durante o gameplay medido.
- **Mixer e callbacks de áudio (`[content_audio]`):**
  - No gameplay (`f >= 400`, 70 janelas): `callback_ticks = 1.611,22 ms` (`1,196%` do span) e `mix_ticks = 3.916,72 ms` (`2,91%` do span, `64.553` chamadas, `~1,87 ms/quadro`).
  - No menu/carregamento (`f=59`, `6.000,7 ms`): `d2_audio_pump()` em laço de espera acumulou `1.495.576` chamadas, consumindo `1.680,4 ms` (`28,0%` daquela janela).
- **Limitação:** Como `d2_audio_pump()` também roda durante a espera de VBlank em `PsyX_WaitForTimestep` e `d2_audio_mix()` roda durante o desenho da OT, os `2,91%` do mixer no gameplay podem incluir tempo ocioso de espera de VBlank ou trabalho efetivo de CPU. E, principalmente, **não há medição equivalente de `a30b79d` original** para saber se a inicialização/encerramento de `fmv_n64.cpp` (`audio_close`/`mixer_close` e reinicialização) deixou estado ou fragmentação diferente em `completo`.

### 3. `rspq` e `libdragon.a` (`rspq_init_context`)
- **Evidência binária direta:** O disassembly de `a30b79d.bin` (`0x800ee720..0x800ee7cc`) e `completo.bin` (`0x800f0980..0x800f0a2c`) confirma instruções idênticas (diferindo apenas nos offsets de relocação):
  - `li a0, 2048` (`0x24040800`) duas vezes por contexto (`4 * 2.048 B = 8 KiB` totais em `malloc_uncached`).
  - Watermark em `v1 + 1800` (`addiu a0, v1, 1800`).
  - Chamadas a `debug_assert_func_f` (`RSPQ_DEBUG=1` ativo na `libdragon.a`).
  - Em `rspq_init`, duas alocações `malloc_uncached(0x10000)` = `128 KiB` (`rspq_rdp_dynamic_buffers`).
- **Conclusão:** Qualquer limitação do buffer `0x200` ou overhead de `RSPQ_DEBUG=1` já estava presente **de forma idêntica** na `a30b79d` original (~20 FPS). Não explica a diferença `a30b79d -> completo`.

### 4. `track_ovl_switch` (`ovl_sw`)
- Em [psyx_rdp.cpp](file:///C:/Desenvolvimento/Nintendo%2064/Driver%202%2064/D2_N64/port/n64/psyx_rdp.cpp#L84-L89), `track_ovl_switch(uint32_t ovl)` incrementa `s_ovlSwitches` toda vez que `ovl != s_lastOvlSent` na CPU.
- Valores observados nos logs: `completo` (`avg=7.635,6/30q`, máx `19.940`), `diagnostico` (`avg=12.194,7/30q`, máx `21.934`), `HEAD` (`avg=11.539,6/30q`, máx `25.902`).
- Apenas um ucode custom (`rsp_d2gpu`, `ovl_id=0x30000000`) é registrado; o outro é o ucode RDP/RSP padrão do `libdragon` (`rdpq`). Mesmo assim, não existe medição de `ovl_sw` em `a30b79d` original, nem contador de ciclos/DMAs do RSP isolando quanto `ovl_sw` custa no `rsp_d2gpu` vs `rdpq` em `a30b79d` vs `completo`.

### 5. Deslocamentos de `.text` (I-cache) e `.bss` (D-cache)
- **Geometria verificada do VR4300:**
  - **I-cache:** 16 KiB, mapeamento direto, linha de 32 B, 512 conjuntos (`(addr >> 5) & 511`).
  - **D-cache:** 8 KiB, mapeamento direto, linha de 16 B, 512 conjuntos (`(addr >> 4) & 511`), *write-back*.
- **Diferenças binárias comprovadas entre `a30b79d.bin` e `completo.bin`:**
  - Em `.text`/`.data`: `+15.128 B` (`1.235.752 B -> 1.250.880 B`). Em `0x80000400..0x80020400`, `1.351` de `1.367` palavras diferentes têm o mesmo opcode, mudando apenas offsets de `$gp`/`.data`/`.bss`.
  - Em `.bss`: buffers pré-`fs_n64` (`d2_clut_cell`, `d2_clut_pool`, `d2_clut_map`, `d2_vramLc`, `d2_vramR`, `d2_vramL`, `scratchData`) deslocaram **`+15.168 B` (`+0x3B40`)**; em seguida, `fs_n64.o` (`lpkFiles`, `lpkBlocks`, `lpkMapBuf`, `lpkCache`) inseriu **`+51.936 B`**; os buffers pós-`fs_n64` (`ot`, `s_winTag`, `s_shTag`, `s_palCache`, `s_palRing`, `s_order_faces`, `s_t3d_*`, `Mixer`) deslocaram **`+67.104 B` (`+0x10620`)**.
  - Como `51.936 mod 8.192 = 2.784 B` (**174 conjuntos de D-cache**), toda a relação de conflito de conjunto na D-cache direta entre as tabelas de VRAM/CLUT e os caches do renderizador (`s_palCache`, `s_palRing`, `ot`, `s_t3d_*`) mudou entre `a30b79d` e `completo`. O impacto real em ciclos, contudo, não foi medido (`Indício` estrutural, efeito quantitativo `Inconclusivo`).

### 6. Cobertura de `d2_content_diag` e `[heap]`
- A instrumentação de `d2_n64_a30b79d_diagnostico.z64` cobriu `fs_n64` (`[content]`, `[content_calls]`), callbacks/mixer (`[content_audio]`), contadores `ovl_sw`/`ft4_*` (`[rsp_ovl]`), `[janela]`, `[prims]`, `[custo]` e `[heap]`.
- `[heap]` em `diagnostico.log` estabilizou em `usado=1.055.360`, `livre_aprox=7.333.248`, `menor_livre=7.333.248` (`usado + livre = 8.388.608 B` = 8 MiB; ou seja, `usado` reflete `mallinfo().uordblks` ~1,01 MiB, enquanto a imagem estática `memsz` ocupa `6.949.000 B` = ~6,63 MiB).
- Caminhos **não** cronometrados isoladamente em `diagnostico`: espera síncrona de RSP/RDP (`rspq_wait`, `rdpq_detach_show` — o tag `[wait]` só foi adicionado posteriormente em `HEAD`), custo detalhado de `StepGame` fora de `carros`/`predios`/`tiles`, e stalls de I-cache/D-cache.

---

## 6. Cadeias Causais Completas (e Pontos de Ruptura)

| Causa candidata | Alteração introduzida (`a30b79d -> completo`) | Mecanismo em execução | Evidência do custo | Efeito observado & Status |
|---|---|---|---|---|
| **1. Carga ativa de `LEVPACK` / `XA`** | Inclusão de `fs_n64.cpp` (`LEVPACK`) e `XA`/`ULC` completo no DFS | Leitura/seek/decode durante o jogo; streaming de voz XA | `[content]`: `0,178 ms/q` (0 em 65/70 janelas); `xa_frames=0/2.100` | **Descartado para o cenário medido** |
| **2. Diferença de cena / rota / `avg_tris`** | Desconhecida para `a30b79d` (sem log) | Mais triângulos/primitivas por quadro aumentam custo de CPU e RDP | `R²=0,717..0,916` (`ticks ~ avg_tris`); `lpk1` variou `~2,4 FPS` só pelo trecho estacionado | **Inconclusivo** entre `a30b79d` e `completo` por falta de `telemetria_a30b79d.log` |
| **3. Conflitos de I-cache / D-cache por crescimento de `.text`/`.bss`** | `.text`/`.data` `+15.128 B`; `fs_n64.o` inseriu `+51.936 B` em `.bss` (`174` conjuntos D-cache entre VRAM e caches do renderizador) | Deslocamento de conjuntos em caches diretas (16 KiB I$, 8 KiB D$) altera taxa de *cache miss* | Mudança de layout **comprovada** nos ELFs/binários; custo em ciclos/misses **não medido** | **Indício** (cadeia incompleta na etapa de medição de misses) |
| **4. Estado/custo residual de áudio / FMV / VBlank** | Chamadas reais a FMVs/menus de conteúdo completo e `d2_audio_mix()` | `mixer_try_play()` e callbacks consomem CPU ou alteram estado pós-FMV | `[content_audio]` mede `2,91%` (`mix`) + `1,20%` (`cb`) no gameplay; **zero dados** para `a30b79d` | **Inconclusivo** (sem baseline comparável em `a30b79d`) |
| **5. `rspq` (`0x200` B, `RSPQ_DEBUG=1`) e `ovl_sw`** | **Nenhuma** em `rspq` (binários idênticos); `ovl_sw` sem baseline em `a30b79d` | Buffers curtos e alternância de ucode `rsp_d2gpu` / `rdpq` | Idêntico em `rspq_init_context` entre as duas ROMs; `ovl_sw` não medido em `a30b79d` | **Descartado** para `rspq` como diferencial; **Inconclusivo** para `ovl_sw` |

---

## 7. Separação: Regressão Específica vs. Otimizações Gerais do Port

1. **O que pode explicar a regressão específica `a30b79d -> a30b79d_completo`:**
   - Diferença de cena/rota (`avg_tris`, visibilidade, `pos`) entre a sessão histórica de `a30b79d` e a sessão de `a30b79d_completo` (não verificável sem `telemetria_a30b79d.log`).
   - Efeitos colaterais indiretos da integração de conteúdo: deslocamento de `.bss` (`+51.936 B` de `fs_n64.o` = 174 conjuntos na D-cache) e `.text` (`+15.128 B` na I-cache), ou estado persistente de áudio/FMV/heap após abertura com conteúdo completo.

2. **O que são otimizações gerais do port (não explicam `a30b79d -> completo`):**
   - Aumento dos buffers `RSPQ` (`tools/libdragon-rspq-buffer.patch`) e desativação de `RSPQ_DEBUG=1`: ambos os problemas já estavam igualmente presentes na `a30b79d` de ~20 FPS.
   - Redução de trocas de overlay `ovl_sw` (batching `rsp_d2gpu` vs `rdpq`), otimizações de OT/projeção/layout quente em `HEAD`, e redução de `d2_vram_r_flush`: pertencem à evolução geral de desempenho do port.
   - *Restrição de segurança:* Não se deve remover barreiras de cache (`data_cache_hit_writeback` / `d2_vram_r_flush`) nem cortar alimentação de áudio sem provar substituição segura equivalente (ex.: gravação direta em endereço uncached ou sincronização garantida antes do DMA do RDP), sob risco de corrupção visual ou falha de áudio.

---

## 8. Lacunas de Evidência e Próximos Passos de Medição (Quando Houver Nova Execução)

### Lacunas nos artefatos atuais
1. Ausência total de `telemetria_a30b79d.log` (impede comparar `pos`, `avg_tris`, `ticks`, `ft4_*`, `ovl_sw` com `completo`).
2. Ausência de contadores de espera RDP/RSP (`[wait]`) nas builds da família `a30b79d` (o `[wait]` existe apenas em `HEAD`/`lpk1`/`lpk2`).
3. Ausência de medição isolada de `cache miss` (I-cache/D-cache) ou de impacto do deslocamento de `.bss` por `fs_n64.o`.

### Protocolo mínimo recomendado para fechar a resposta (em etapa futura)
1. **Rodar `d2_n64_a30b79d.z64` e `d2_n64_a30b79d_completo.z64` exatamente no mesmo save/percurso** (ou comparar janelas pareadas por `pos` e `avg_tris`), registrando o log completo de `a30b79d`.
2. **Isolar o efeito de layout de `.bss`/`.text`:** comparar `a30b79d` com uma variante tendo apenas um padding equivalente em `.bss` (`+51.936 B` na posição de `fs_n64.o`), sem alterar código de jogo.
3. **Medir `ovl_sw` e esperas RSP/RDP** na própria base `a30b79d` (com e sem conteúdo completo) no mesmo trecho alinhado por `pos`.
