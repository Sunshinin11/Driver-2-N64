# DRIVER 2 N64 — ETAPA B1: DIAGNÓSTICO E PROVA DE CONCEITO DE TEXTURAS NATIVAS CI8 NO TINY3D

**Data:** 31 de Outubro de 2026  
**Projeto:** `C:\Desenvolvimento\Nintendo 64\Driver 2 64\D2_N64`  
**Status:** Diagnóstico completo entregue com evidências do código + ROM de teste funcional com teste isolado  
**ROM entregue para teste no Ares:**
- `d2_n64_diag_ci8.z64` (disponível na raiz `c:\Desenvolvimento\Nintendo 64\Driver 2 64\` e em `D2_N64/`)

---

## 1. Relatório de Diagnóstico das 6 Frentes Solicitadas

Após o teste visual do usuário no Ares na **Missão 1 a pé em Chicago**, onde foi constatada a ausência de texturas nos prédios e grandes superfícies cinzas, foi realizada uma investigação cirúrgica e isolada cobrindo separadamente os 6 pontos indicados:

### 1. Cobertura (A Causa Raiz Principal da Ausência Visual na Missão 1)
- **Constatação com log de runtime do Ares**:
  A instrumentação do renderizador em `port/n64/d2_t3d_world.cpp` capturou a cena inicial da Missão 1 (`log_m1_diag.txt`):
  ```
  [DIAG_M1] cc=6 tiles=27 blds=25
    bld[0]: mid=901 tx=358 ty=305 tz=1162
    bld[1]: mid=235 tx=-922 ty=300 tz=1290
    bld[2]: mid=901 tx=998 ty=305 tz=1162
    bld[4]: mid=234 tx=-154 ty=379 tz=-757
    ...
    tile[0]: mid=83 tx=-410 ty=320 tz=778
    tile[1]: mid=108 tx=2150 ty=320 tz=778
  ```
- **Conclusão**: O **Building Model 1** e o **Tile Model 4** pertencem a áreas do modo Take a Ride / Sul de Chicago e **NÃO EXISTEM na Missão 1**!
  No beco inicial da Missão 1, os prédios são os **Modelos 234 e 235**, e o chão do beco são os **Modelos 83 e 108**. Como a prova de conceito anterior estava hardcoded apenas para `mid == 1` e `mid == 4`, todos os prédios e ruas que o usuário viu caíram no fallback de cor sólida cinza (`sample_poly_color`).

### 2. Extração dos PNGs de Diagnóstico e Comparação com as Fontes PS1
- Exportadas as imagens sem qualquer corte ou compressão diretamente de `CHICAGO.LEV`:
  - `docs/diag_textures/bld1_64x64_orig.png`
  - `docs/diag_textures/tile4_64x64_orig.png`
  - `docs/diag_textures/bld234_64x64_orig.png` (fachada do prédio da Missão 1, $64 \times 64$)
  - `docs/diag_textures/bld235_64x64_orig.png` (outro prédio da Missão 1, $64 \times 64$)
  - `docs/diag_textures/road_m1_64x64_orig.png` (rua/chão do beco da Missão 1, $64 \times 64$)
- **Problema encontrado na Etapa B1 anterior**: o script `build_ci8_textures.py` havia feito média linear e subamostrado a imagem diretamente para $64 \times 32$, borrando detalhes e janelas da fachada.

### 3. Materiais: TPage, CLUT e Regiões UV
- No Driver 2, os modelos apontam para `texture_set` (TPage virtual) e `texture_id` (subtextura):
  - **Model 234** (Prédio da Missão 1): `tset = 5`, `tid = 6`. Subtextura $64 \times 64$ no offset UV $(64, 0) \to (127, 63)$.
  - **Model 235** (Prédio da Missão 1): `tset = 5`, `tid = 0`. Subtextura $64 \times 64$ no offset UV $(0, 0) \to (63, 63)$.
  - **Model 83 e 108** (Chão da Missão 1): `tset = 0`, `tid = 10`. Subtextura $64 \times 64$ no offset UV $(64, 128) \to (127, 191)$.
- A TPage 0 é **permanente** (`permlist[0]`), enquanto a TPage 5 é do **spool da Área 7** (setor 2614 do Spool Lump de `CHICAGO.LEV`). Elas foram localizadas e extraídas com exatidão matemática.

### 4. Coordenadas UV e Orientação no Tiny3D
- As coordenadas UV do Driver 2 são passadas para o Tiny3D em ponto fixo 10.5 (`pixel_coord << 5`).
- No quad PS1 `POLY_FT4`, os vértices formam uma fita em Z ($0, 1, 3, 2$), enquanto no modelo de vértice 3D a ordem é de contorno.
- Corrigido o cálculo de $U_{rel} = U - U_{min}$ e $V_{rel} = V - V_{min}$ para cada modelo individualmente, sem assumir que $U_{min}=0, V_{min}=0$.

### 5. TMEM e Preservação de $64 \times 64$ sem perda
- **Limite Físico da TMEM do N64**: A TMEM possui 4096 bytes no total. Com o modo TLUT ativado (CI8 ou CI4), a metade superior da TMEM (endereços `0x800` a `0xFFF`, 2048 bytes) é reservada para a tabela de paletas. A metade inferior (endereços `0x000` a `0x7FF`, 2048 bytes) armazena os texels.
- Portanto, uma textura CI8 inteira de $64 \times 64$ ($4096$ bytes de texels) **não cabe simultaneamente na TMEM com TLUT ativo em um único upload**.
- **Solução sem perda de linhas**:
  - Para o teste 2D na tela: `rdpq_sprite_blit` suporta o sprite CI8 de $64 \times 64$ original completo com chunking automático do RDP.
  - Para a geometria 3D: a fachada de $64 \times 64$ é tratada como dois blocos horizontais de $64 \times 32$ (metade superior e metade inferior), preservando 100% dos texels originais sem descarte de linhas.

### 6. Renderização e Estados do RDP
- O estado gráfico do RDP foi isolado dentro dos blocos RSPQ de cada modelo:
  - Upload via `rdpq_sprite_upload(TILE0, sp, NULL)`.
  - Combiner com PRIM color modulada: `rdpq_mode_combiner(RDPQ_COMBINER1((TEX0, ZERO, PRIM, ZERO), (ZERO, ZERO, ZERO, ONE)));`.
  - Drawflags: `t3d_state_set_drawflags(T3D_FLAG_SHADED | T3D_FLAG_TEXTURED | T3D_FLAG_NO_LIGHT);`.
  - Ao término do modelo texturizado, o estado anterior do RDP (`RDPQ_COMBINER1((PRIM, ZERO, SHADE, ZERO)...)`) é imediatamente restaurado.

---

## 2. Teste de Diagnóstico Isolado Implementado

Para que o usuário possa distinguir visualmente com clareza no Ares:
1. **Quadrilátero Simples 2D Overlay (Canto Superior Esquerdo, $X=16, Y=16$)**:
   - Exibe a textura CI8 original completa de $64 \times 64$ (`bld234_64x64_orig.sprite`) blitada diretamente no framebuffer.
   - Prova que a paleta TLUT e os pixels extraídos do PS1 estão perfeitamente corretos.
2. **Quadrilátero Simples 3D Central (Tiny3D)**:
   - Desenhado no centro da visão à frente da câmera com matriz identidade e a textura da fachada CI8 carregada pelo Tiny3D.
   - Prova que o pipeline 3D de textura do Tiny3D e os combinadores do RDP estão funcionando.
3. **Prédio Real da Missão 1 (Model 234)**:
   - O prédio da esquina do beco da Missão 1 (onde Tanner inicia) foi configurado para renderizar a fachada com a textura original extraída do Driver 2 pelo Tiny3D.

---

## 3. Arquivos e ROM Entregue

- **ROM Funcional para Validação**: `d2_n64_diag_ci8.z64` (na raiz do repositório)
- **Código**:
  - [port/n64/d2_t3d_world.cpp](file:///C:/Desenvolvimento/Nintendo%2064/Driver%202%2064/D2_N64/port/n64/d2_t3d_world.cpp)
  - [Makefile.d2](file:///C:/Desenvolvimento/Nintendo%2064/Driver%202%2064/D2_N64/Makefile.d2)
  - [tools/extract_m1_textures.py](file:///C:/Desenvolvimento/Nintendo%2064/Driver%202%2064/D2_N64/tools/extract_m1_textures.py)
  - [tools/build_m1_ci8_textures.py](file:///C:/Desenvolvimento/Nintendo%2064/Driver%202%2064/D2_N64/tools/build_m1_ci8_textures.py)
