# Driver 2 N64: otimização de desempenho (28 e 29/10/2026)

Este documento registra o que foi feito para acelerar o port, como medir e o que falta.
Todo o trabalho foi feito e testado no ares (headless, na nuvem). Ainda não foi testado no console.

## 1. Resultado

Cena de referência: Chicago, modo Praticar, carro parado (o trânsito e os pedestres continuam se movendo).
Unidade: ticks do contador do VR4300 por quadro do jogo (46.875.000 ticks = 1 segundo).

| Etapa | Ticks/quadro | fps (ares) |
|---|---|---|
| Início (código de 25/10) | 8,37M | ~5,6 |
| Janela de textura 64x64 + bancos de paleta | 6,10M | ~7,7 |
| Caminho direto OT -> RDP + menos revalidação | 5,83M | ~8,0 |
| GTE nativa (sem o emulador nas operações mais usadas) | 4,90M | ~9,6 |
| Fase 1 completa (cache de vértices, carga direta, OT enxuta, arranjo de código) | **4,456M** | **~10,5** |

A imagem foi comparada quadro a quadro com a versão anterior em cada etapa, parado e dirigindo: sem diferenças visíveis.
Os prédios com ruído roxo e vermelho em Chicago **já existiam antes** destas mudanças (bug separado, ainda aberto).

## 2. Como compilar

Nada muda no fluxo: `bash build.sh` (ou `build_d2.ps1`). As otimizações vêm ligadas por padrão no `Makefile.d2`:

```
D2_OPT ?= -DD2_DIRECT -DD2_NOSUBQ -DD2_GTE_NATIVE -DD2_VTXCACHE -DD2_LEANLOAD -DD2_FASTOT
```

Para compilar sem elas (comparação): `make -f Makefile.d2 D2_OPT=`.
O `patch_engine.sh` / `tools/patch_engine.py` aplicam no motor (`port/redriver2`) os ajustes novos; são idempotentes, como antes.
Uma cópia limpa do projeto, com estes arquivos e o `build.sh` normal, reproduz os 10,5 fps (conferido).

## 3. O que foi feito (arquivo por arquivo)

### 3.1 Texturas e paletas (`port/n64/psyx_rdp.cpp`)
- **Janela de 64x64 na TMEM** (texturas de 4 bits na metade direita da VRAM, `D2_TEXWIN`, ligada por padrão):
  em vez de carregar o retângulo exato de cada triângulo, carrega um quadrado de 64x64 da VRAM sombra e reaproveita enquanto
  os triângulos seguintes couberem nele (~60% de acerto). A carga é um único `LOAD_TILE` com tiles fixos.
- **Bancos de paleta** (`D2_PALBANK`, ligado por padrão): 15 paletas de 16 cores ficam na TMEM ao mesmo tempo (cache LRU);
  no acerto basta trocar o banco do `TILE0`. Acabou com as esperas `rspq_wait` que o anel de 64 paletas causava.
- **Sem revalidação por triângulo:** como o jogo quase não escreve na VRAM durante a corrida (0 escritas por quadro),
  a sombra é sincronizada só quando a VRAM muda (`shadow_sync_all`), e a paleta tem atalho para a mesma CLUT.
- **Carga direta** (`D2_LEANLOAD`): as texturas que não usam a janela (8 bits, 16 bits, páginas abaixo de x=512 e
  regiões largas) agora carregam com comandos fixos (`SET_TEXTURE_IMAGE`, `SET_TILE`, `LOAD_TILE`) em vez de
  `rdpq_tex_upload_sub`. Ganho: -7%.
- **Núcleo por triângulo comum** (`tri_core`) usado pelo caminho antigo e pelo novo.
- **Medição embutida:** a cada 32 passos do jogo o log mostra `[bench] cc=... ticks=... tris=... tex=...`, e a cada
  30 quadros `[tex]` (cargas de textura/paleta) e `[cyc]`.

### 3.2 Caminho direto OT -> RDP (`D2_DIRECT`)
- Os polígonos F3/F4/G3/G4/FT3/FT4/GT3/GT4 vão direto do primitivo do PS1 para o RDP (`d2_direct_poly` em
  `psyx_rdp.cpp`), sem montar `GrVertex`, sem triangular e sem lotes. Sprites, retângulos, linhas e trocas de ambiente
  continuam no caminho antigo, e os lotes pendentes são desenhados antes de cada polígono direto (ordem da OT mantida).
- Com isso o buffer de vértices do PsyX caiu de 24576 para 8192 (`D2_MAX_VERTS`), **liberando 320 KB de RAM**, com
  proteção contra estouro (`ParsePrimitive` esvazia os lotes se o buffer encher).
- Patches no motor: ganchos em `ProcessFlatPoly`/`ProcessGouraudPoly` e a função `d2_flush_splits` (PsyX_GPU.cpp).

### 3.3 GTE nativa (`D2_GTE_NATIVE`)
- `port/compat/d2_gte_inline.h`: o macro `doCOP2` despacha em tempo de compilação. RTPS, RTPT, NCLIP, AVSZ3/4 e MVMVA
  de rotação são feitos em C direto, sem o emulador e sem as flags que o jogo nunca lê (o Driver 2 não usa `gte_stflg`).
  As demais operações continuam no emulador.
- `port/n64/gte_native.cpp`: corpo das operações pesadas (uma cópia só) e a tabela da divisão UNR do PS1.
- **Conferido contra o emulador em 200 mil casos aleatórios: 0 diferenças** nos registradores que o jogo lê.
- Patch no motor: `#undef doCOP2` em `INLINE_C.C`.

### 3.4 Cache de vértices (`D2_VTXCACHE`, `port/n64/d2_vtxcache.cpp`)
- `Tile1x1`, `Tile1x1Lit`, `drawMesh`, `drawMeshLit`, `PlotBuildingModel` e `PlotBuildingModelSubdivNxN` reescritas:
  cada vértice do modelo é projetado uma vez (na primeira vez que um polígono o usa) em vez de 3 a 4 vezes.
- As originais continuam no motor com o sufixo `_orig` (macro `D2_VC_ORIG` em `d2_n64_compat.h`) e o linker as descarta.
- Ganho: ~5% (medido desligando só este item).

### 3.5 OT enxuta (`D2_FASTOT`, patches no motor)
- `ClearOTagR`: escreve duas palavras por entrada e usa a instrução de cache `Create Dirty Exclusive` para não ler a
  RAM antes de escrever (custo caiu de 3,4% para 1,8% do quadro).
- `ParsePrimitivesLinkedList`: só atualiza o lote quando a entrada tinha primitivos (percurso -18%).

### 3.6 Arranjo do código no cache de instruções (`hot.ld`, `tools/hot.py`)
- O VR4300 tem 16 KB de cache de instruções. Quando funções muito usadas caem na mesma posição do cache, uma expulsa a
  outra, e o mesmo código pode variar até ±10% só pela posição.
- `tools/hot.py` lê o perfil do ares e gera `hot.ld` com as funções mais quentes juntas no início do `.text`; o
  `Makefile.d2` passa `--section-ordering-file=hot.ld` ao linker quando o arquivo existe.
- O `hot.ld` atual (orçamento de 64 KB) foi o melhor entre 7 orçamentos testados. **Deve ser regerado** sempre que o
  código quente mudar bastante (ver seção 5).

### 3.7 Testados e descartados (não repetir sem motivo novo)
- Dividir o céu (quads de 128x84 de UV) em sub-quads em vez do recorte em float: empatou ou piorou (fica atrás de
  `D2_NOSUBQ`, que mantém o recorte antigo).
- Desligar `-ftrivial-auto-var-init=pattern` (flag do libdragon): sem ganho mensurável.
- Cache de 16 paletas sem bancos e `-O2` no jogo inteiro (das sessões anteriores): sem ganho / tela preta.

## 4. Achados importantes

1. **RAM no limite.** Antes da Fase 1 sobravam ~300 KB livres. Um aumento de código de 136 KB (versão inline da GTE) e
   depois de só 8 KB fez a alocação do cache de texturas (`s_slots`) falhar **em silêncio**: o jogo rodava, mas não
   desenhava nenhum triângulo. Os 320 KB recuperados em 3.2 aliviaram isso, mas o problema volta se a memória
   apertar. Recomendação: fazer a falha de alocação de `s_slots` parar o jogo com mensagem.
2. **Medir pelo passo do jogo, não pelo quadro do emulador.** O jogo avança um passo de lógica por quadro desenhado;
   uma versão mais rápida chega mais longe na cena no mesmo tempo. Comparar pelo `CameraCnt` (linhas `[bench]`)
   garante o mesmo estado (mesmo número de triângulos) entre versões.
3. **Onde está o tempo hoje (10,5 fps):**
   - montagem por polígono do PS1 na CPU (`d2_direct_poly` + `tri_core`): ~25%
   - matemática 3D (GTE nativa + cache de vértices): ~13%
   - cargas de textura/paleta: ~10%
   - percurso e limpeza da OT: ~6%
   - lógica do jogo: ~18%
4. **Gravar na fila do RSP é barato:** sem gravar nenhum triângulo o quadro só cai ~5%. O caro é a lógica que a CPU roda
   por polígono (decodificar, escalar, decidir textura e paleta).

## 5. Ferramentas (`tools/nuvem/`)

Scripts usados na nuvem (caminhos `/opt/...`; adaptar para o WSL):
- `mk.sh nome "flags"`: compila e guarda `nome.z64/.elf/.map`.
- `run.sh rom quadros a_cada entradas pasta`: roda no ares headless e monta a folha de capturas.
- `bench.sh rom...`: roda 2 versões em paralelo; `cc.sh rom...` resume as linhas `[bench]` (média de ticks/quadro no mesmo
  trecho do jogo, `CC0`..`CC1`).
- `pgo.sh nome "flags"`: compila, roda o profiler, gera `hot.ld` para esta versão e recompila (`nomep.z64`).
- `layout.sh`: testa vários orçamentos de `hot.ld` e mostra o melhor.
- `in_bench.txt`: entradas (menu -> Praticar -> Chicago, carro parado). `in_drive.txt`: o mesmo, dirigindo.
- `gte_test.cpp`: teste no PC da GTE nativa contra o emulador (200 mil casos).
- Harness do ares: `tools/ares_headless/harness.cpp`, compilado contra o ares (núcleo n64) com
  `cmake -DARES_CORES=n64` e alvos `ares mia nall libco` (ver `tools/nuvem/LEIA.md`).

## 6. O que falta

### Fase 2: o RSP como GPU do PS1 (decidida, não iniciada no código)
Objetivo: tirar da CPU os ~40% de trabalho por polígono (montagem + texturas + paletas + OT).
Desenho proposto (estudado com a API do libdragon `rsp_rdpq.inc` e `rsp_rdpq_tri.inc`):
1. **Overlay de RSP `rsp_d2gpu.S`** (regra de `.S` com prefixo `rsp` do `n64.mk`; incluir `rsp_rdpq.inc` e
   `rsp_rdpq_tri.inc` no fim). Comandos:
   - `D2G_Init` (endereço da VRAM sombra), `D2G_Screen` (offset da tela), `D2G_WinInval` (a CPU mexeu em TMEM/TILE0);
   - um comando por tipo de polígono (F3, FT3, G3, GT3, F4, FT4, G4, GT4), com o cabeçalho (banco de paleta, flags) e as
     palavras do primitivo do PS1 copiadas da OT.
2. **No RSP, por polígono:** decodificar x/y/u/v/cor; XY em s13.2 (`(x+ofx)<<2`, escala 1:1, 320x240); cor
   `(c*255)>>7` com saturação; caixa de UV; decisão da janela 64x64 (mesma regra da CPU: alinhamento 32, depois 16,
   depois 2) e, se for carregar: `SYNC_LOAD`, `SET_TEXTURE_IMAGE` (sombra, base 0 ou 512), `SET_TILE` TILE6 I8,
   `LOAD_TILE`, `SYNC_TILE`, `SET_TILE` TILE0 CI4 com o banco, `SET_TILE_SIZE`; ST em s10.5; montar os vértices no
   formato do `RDPQ_Triangle_Send_Async` (XY, Z=0, clip=0, rej=0xFF, RGBA, ST, W=INVW=0x10000) e enviar 1 ou 2
   triângulos; `RDPQ_Triangle_Send_End` no fim.
3. **Na CPU** (`d2_direct_poly`): só decidir se o polígono vai ao RSP (sem textura, ou CI4 na janela com UV+1 <= 64,
   escala 1:1, sem textura de override), trocar o modo do RDP quando a combinação muda, cuidar da paleta (banco) e
   copiar as palavras do primitivo para a fila. O resto continua no caminho atual. Depois de polígonos do RSP, marcar
   `__rdpq_autosync_use(AUTOSYNC_TILES | AUTOSYNC_TMEMS | AUTOSYNC_PIPE)` antes de o CPU voltar a carregar textura, e
   invalidar o estado de janela/tiles da CPU (`s_tmemWin`, `s_leanTiles`, `s_winBase`, `s_curBank`).
4. **Etapa seguinte:** o RSP percorrer a OT sozinho (sem a CPU) e cuidar também das paletas.
5. Medir cada etapa com `cc.sh` e regerar o `hot.ld` no fim.

### Fase 3: console real
1. Fazer a falha de alocação do cache de texturas (`s_slots`) parar com mensagem, em vez de não desenhar nada.
2. Medir a RAM livre no boot e depois de carregar a cidade; manter folga.
3. Testar no SummerCart64 e comparar com o ares.

### Outros pendentes (das notas anteriores)
- Prédios com textura corrompida (ruído roxo) em Chicago.
- Áudio, cutscene (`CUT1.R`), save, pedestres/trânsito completos, FMV, PT-BR, tamanho do ROM.
