# DRIVER 2 N64 — ETAPA B1: PROVA DE CONCEITO DE TEXTURAS NATIVAS CI8 NO TINY3D

**Data:** 31 de Outubro de 2026  
**Projeto:** `C:\Desenvolvimento\Nintendo 64\Driver 2 64\D2_N64`  
**Status:** Concluído e pronto para validação visual pelo usuário no Ares  
**ROMs entregues:**
- `d2_n64_etapaB1_ci8.z64` (na raiz do projeto e em `D2_N64/`)

---

## 1. Visão Geral da Etapa B1

A Etapa B1 teve como objetivo iniciar a restauração das texturas originais do PS1 nos modelos migrados para Tiny3D através de uma prova de conceito real e funcional com **um prédio (Building Model 1)** e **um trecho de rua (Tile Model 4)**, utilizando o formato nativo **CI8 (Color Index de 8 bits com TLUT RGBA16 de até 256 cores)**.

Em cumprimento estrito às regras do projeto:
1. **Formato CI8 nativo**: nenhum modelo foi convertido para CI4 como substituto automático.
2. **Conversão offline**: todas as sub-texturas e paletas CLUT foram extraídas offline diretamente dos lumps de `CHICAGO.LEV` (`CITYLUMP_TPAGE` e `CITYLUMP_DATA1`), gerando sprites nativos da libdragon via ferramenta oficial `mksprite -f CI8 -c 0`.
3. **Respeito aos limites da TMEM**: cada sprite foi adaptado para 2048 bytes de dados de texels ($64 \times 32$ texels CI8), reservando a outra metade da TMEM (2048 bytes) para a tabela de cores TLUT de 256 entradas RGBA16 (512 bytes em RDRAM).
4. **Preservação de UVs e Iluminação**: as coordenadas de textura foram integradas no `T3DVertPacked` em ponto fixo 10.5 (`pixel_coords << 5`) do Tiny3D, com combinador do RDP modulado pela cor PRIM (`combointensity`), preservando iluminação e horário de Driver 2.
5. **Autocontenção**: o upload do sprite e o combinador foram encapsulados no próprio bloco RSPQ de cada modelo com restauração imediata do pipeline padrão ao final, garantindo zero overhead para os demais 423 modelos do mundo.

---

## 2. Engenharia Reversa e Pipeline de Conversão Offline

### 2.1 Estrutura dos Lumps de Textura em `CHICAGO.LEV`
- `CITYLUMP_DATA1` (offset 2048): contém o Lump 34 (`LUMP_TEXTUREINFO`), que mapeia as sub-texturas (UVs, dimensões, `texture_set` e `texture_id`).
- `CITYLUMP_TPAGE` (offset 219136): contém os 11 texture pages permanentes comprimidos com RLE PS1.
- `CITYLUMP_DATA2` (offset 784384): contém o Lump 1 (`LUMP_MODELS`) com os 425 modelos migrados para Tiny3D.

### 2.2 Modelos Escolhidos para a Prova de Conceito
1. **Building Model 1** (Prédio permanente de Chicago):
   - 20 polígonos (`PL_POLYFT4`), todos mapeando para `tset = 0`, `texture_id = 4`.
   - Sub-textura original: UV $(0, 88)$ até $(63, 151)$ ($64 \times 64$ pixels).
   - Paleta: CLUT 4 do TPage 0 (16 cores RGBA5551 convertidas para RGBA16).
2. **Tile Model 4** (Trecho de asfalto/rua de Chicago):
   - 1 polígono (`PL_POLYFT4`), mapeando para `tset = 0`, `texture_id = 11`.
   - Sub-textura original: UV $(0, 24)$ até $(63, 87)$ ($64 \times 64$ pixels).
   - Paleta: CLUT 11 do TPage 0.

### 2.3 Ferramenta de Conversão Offline (`tools/build_ci8_textures.py`)
O script realiza:
- Descompressão RLE PS1 dos 32.768 bytes do bitmap de 4 bits do TPage 0.
- Extração das paletas CLUT correspondentes.
- Subamostragem vertical limpa ($2 \times 1$ box filter) adaptando $64 \times 64 \to 64 \times 32$ (2048 texels), respeitando a TMEM sem truncar detalhes horizontais.
- Compilação via `/opt/libdragon/bin/mksprite -f CI8 -c 0` diretamente em `filesystem_vadpcm/TEX/`:
  - `bld1.sprite`: 2696 bytes (32B header + 512B TLUT + 2048B texels CI8).
  - `tile4.sprite`: 2696 bytes (32B header + 512B TLUT + 2048B texels CI8).

---

## 3. Implementação no Tiny3D (`port/n64/d2_t3d_world.cpp`)

1. **Carga dos Sprites em Inicialização**:
   - `s_tex_bld1 = sprite_load("rom:/TEX/bld1.sprite");`
   - `s_tex_tile4 = sprite_load("rom:/TEX/tile4.sprite");`
   - Realizada uma única vez por fase através do DragonFS do cartucho.
2. **Ativação e Isolamento no RSPQ Block**:
   ```cpp
   if (is_textured_proof) {
       sprite_t* sp = (mid == 1) ? s_tex_bld1 : s_tex_tile4;
       if (sp) {
           rdpq_sprite_upload(TILE0, sp, NULL);
           rdpq_mode_combiner(RDPQ_COMBINER1((TEX0, ZERO, PRIM, ZERO), (ZERO, ZERO, ZERO, ONE)));
           t3d_state_set_drawflags((enum T3DDrawFlags)(T3D_FLAG_SHADED | T3D_FLAG_TEXTURED | T3D_FLAG_NO_LIGHT));
       }
   }
   ```
3. **Mapeamento UV e Janelas de Vértices**:
   - As coordenadas locais relativas à sub-textura são convertidas em ponto fixo 10.5 (`pixel << 5`) para `stA` e `stB` do `T3DVertPacked`.
   - Vértices de faces adjacentes que compartilham a mesma posição geométrica mas coordenadas UV diferentes são tratados como vértices locais distintos através de `find_local_vtx(orig, vcol, s, t)`.
   - O particionamento em janelas de 64 vértices (`flush_window`) acomoda perfeitamente os 75 vértices texturizados do Building Model 1 em duas janelas do RSP sem overflow.
4. **Restauração Limpa**:
   - Ao final do modelo texturizado, o bloco RSPQ restaura `rdpq_mode_combiner(RDPQ_COMBINER1((PRIM, ZERO, SHADE, ZERO), ...))` e `t3d_state_set_drawflags(T3D_FLAG_SHADED | T3D_FLAG_NO_LIGHT)`.

---

## 4. Medições e Logs no Ares Emulator

Executado benchmark automatizado determinístico (`D2_BENCH=1 D2_T3D_WORLD=1 D2_VADPCM=1 D2_PERFDBG=0 D2_ORIGIN_PROF=0`):

### 4.1 Carregamento e Alocação de Memória
Arquivo de log: `log_bench_b1.txt`
```
[T3D_TEX] Carregado bld1.sprite (w=64, h=32)
[T3D_TEX] Carregado tile4.sprite (w=64, h=32)
[T3D] Convertidos 425 modelos no T3D (tiles=132, predios=293), 72 no caminho antigo (497 total). Vertices: 8032/9216 (125 KB de 144 KB)
[T3D_MEM] Heap alocado para blocos RSPQ: 213016 bytes (heap livre: 313584 -> 100568)
```
- **Memória residente de texturas**: 5.392 bytes (2 sprites de 2.696 bytes cada).
- **Heap livre**: 100.568 bytes livres mesmo com todos os blocos RSPQ e áudio VADPCM carregados.
- **Tamanho da ROM**: 42.270.720 bytes (40,3 MB).

### 4.2 Amostras de Desempenho em Gameplay (Trajetória Idêntica)
Amostras registradas no log `log_bench_b1.txt` durante aceleração do veículo em Chicago:
- `cc=32` (Tanner parado no semáforo): `ticks=3406250` (~18,3 FPS equivalente)
- `cc=64`: `ticks=3345306` (~18,7 FPS equivalente)
- `cc=96`: `ticks=3328607` (~18,7 FPS equivalente)
- `cc=128` (arranque em direção ao cruzamento): `ticks=3588392` (~17,4 FPS equivalente)
- `cc=256` (velocidade cruzeiro): `ticks=4362203` (~14,3 FPS equivalente)
- `cc=320`: `ticks=4875123` (~12,8 FPS equivalente)
- `cc=704`: `ticks=3998072` (~15,6 FPS equivalente)
- `cc=736`: `ticks=3916476` (~16,0 FPS equivalente)

### 4.3 Estabilidade e Fidelidade
- **Erros / Travamentos**: zero. Nenhum "Out of memory", nenhuma exceção de barramento e nenhuma corrupção no HUD.
- **Áudio VADPCM**: todos os 13 bancos SFX carregados com decodificação no RSP simultânea à renderização 3D.
- **TMEM**: carga de TLUT de 256 cores (RGBA16) e texels CI8 realizada dentro dos limites estritos de 4096 bytes do RDP.

---

## 5. Próximos Passos (Aguardando Validação do Usuário)

Conforme as regras do projeto:
- **Nenhuma alteração visual é considerada validada sem teste do usuário no Ares.**
- O usuário deve carregar a ROM `d2_n64_etapaB1_ci8.z64` no Ares, entrar em Chicago (Take a Ride ou Replay) e verificar visualmente as texturas reais em CI8 no prédio 1 e na rua 4.
- Somente após a aprovação visual do usuário iniciaremos a **Etapa B2 (Limitar população ambiental a 20%)**.
