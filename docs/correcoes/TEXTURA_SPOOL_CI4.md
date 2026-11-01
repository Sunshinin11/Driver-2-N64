# Correção da extração CI4 de páginas do spool — 01/11/2026

Sintoma: o modelo 234 recebia uma fachada de janelas fragmentadas/listradas.
A imagem já estava corrompida no PNG, antes de passar por mksprite/Tiny3D.

O conversor tratava uma página de região como página permanente comprimida.
O caminho UpdateSpoolPC/SendTPage do REDRIVER2 lê um setor de paletas e depois
16 setores de pixels crus. O conversor começava os pixels logo após as paletas,
no meio do primeiro setor, e aplicava RLE indevidamente.

Alteração em tools/build_ci4_material.py:

- Lê o diretório de seções e as tabelas de materiais/regiões do próprio LEV.
- Resolve a página virtual pelo AreaData/AreaTPages, sem setor fixo de Chicago.
- Lê as paletas no primeiro setor e os 32.768 bytes de pixels a partir do segundo.
- Rejeita páginas permanentes não suportadas em vez de substituir pela página 5.
- Verifica limites, paletas e cópias conflitantes de páginas.
- Evita SameFileError quando staging e origem são hard links do mesmo sprite.

Material regenerado: filesystem_vadpcm/TEX/tex_s5_t6.sprite (2216 bytes).
Em Chicago, página 5: área 7, setor 2614, offset 6393856.
TEXINF: x=64, y=0, largura=64, altura=64; paleta 6.

Verificações executadas:

- Todos os 4096 índices do PNG conferem com endereçamento independente por
  palavras little-endian da VRAM PS1 e seleção dos nibbles.
- Todas as 16 cores conferem com a CLUT 6 original.
- Página 6 resolve no setor 2631 e tem pixels distintos da página 5.
- Página permanente 0 é rejeitada explicitamente por este extrator de spool.
- Compilação concluída, material corrigido localizado dentro da ROM final.

Comandos no WSL:

```sh
python3 tools/build_ci4_material.py 5 6
bash build.sh D2_T3D_WORLD=1 D2_VADPCM=1 D2_BENCH=0 D2_PERFDBG=0 D2_ORIGIN_PROF=0
```

ROM entregue: d2_n64_spool_texture_fix.z64, também copiada em d2_n64.z64.
Backup anterior: d2_n64_before_spool_texture_fix.z64.
Log: build_spool_texture_fix.log.

Escopo: não modifica geometria, UVs nem a cobertura do renderizador. A condição
mid==234 continua limitando o caminho texturizado; chão e demais modelos seguem
com cores sólidas. A validação visual desta ROM no jogo está pendente do usuário.
