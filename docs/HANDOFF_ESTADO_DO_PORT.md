# Driver 2 N64 — Estado do port e passagem de bastão

Atualizado em 31/10/2026. Projeto: `C:\Desenvolvimento\Nintendo 64\Driver 2 64\D2_N64` (versão principal). Base: REDRIVER2 (decomp do PS1) + libdragon (preview) + Expansion Pak. Testes no Ares v148. Build: `.\build_d2.ps1` (WSL, `/opt/libdragon`).

## Regras de trabalho (o usuário exige)
- Um problema por vez. Pesquisa e comparação de código ANTES de qualquer build. Nada de "tiro no escuro".
- Nenhuma causa é considerada resolvida antes do teste visual do usuário no Ares.
- Todo número de desempenho precisa vir de log da execução (citar arquivo e linha). Relatórios do Antigravity já inventaram números duas vezes: sempre conferir os logs.
- FPS real = Δframes × 60 / Δvbl (linhas `[fps]`). A instrumentação `D2_PERFDBG` custa ~30%: medir FPS com ela desligada.
- Um commit por alteração; flags novas desligadas por padrão até validação.
- Não repetir hipóteses já descartadas (lista abaixo).

## Corrigido e validado no Ares
| Problema | Causa | Commit |
|---|---|---|
| Tela de loading com metade em ruído | `load_tile` com largura 0 dava a volta na TMEM e apagava a TLUT de 256 cores | `dbf8f93` (+ reenvio de TLUT 8 bits por bloco em `upload_region`) |
| Prédios invisíveis + explosões de vértices (parede rosa em Gold Coast) | `d2_swap_model` convertia o bloco de colisão com 11 shorts por pacote; `COLLISION_PACKET` tem 10. Os 2 bytes extras trocavam `id`/`texture_set` do 1º polígono de todos os 216 modelos com colisão | `0ef5efb` |
| Placas gigantes de chão perto da câmera | Falta a regra da GPU do PS1 (descarta triângulo > 1023×511) em `tri_core` | `ef24b11` |
| Ruído no HUD e nas batidas | Blender aditivo do RDP não satura (doc do libdragon: dá a volta) | `fa14399`, `de9d5da`, `d3d8398`, `c3838df`, `9292d80`, `1000333` |
| Luzes, cone do farol, sombras, fumaça | Alfa proporcional ao brilho (textura × vértice, RGBA32); subtração como multiplicação; cor de vértice direta em polígono sem textura; cor uniforme por primitiva no aditivo | idem acima |
| Sombra do Tanner (quadrado com lixo) | Jogo renderiza o Tanner em área offscreen da VRAM; backend não suporta. Sombra desligada | `8c563d6` (`D2_NO_TANNER_SHADOW`) |
| "L" vermelho da seta de missão a pé | Vértice atrás da câmera (divisão saturada) | `3d5a70f` |
| Input automático atrapalhando testes | Roteiro de botões só sob `D2_BENCH` | já em `main` |

## Revertido (não funcionou / causou regressões) — NÃO repetir
- `D2_GTE_WIDE` (projeção sem trava de ±1024), agrupamento por textura dentro de cada balde da OT, evitar reenvio de TMEM, recorte em blocos mais barato, cache de `get_region`. Revertidos em `69213c5`..`ddc3a7c`. Ganho medido ~3,8% de ticks, sem ganho de FPS, com regressões (linhas no chão/minimapa, HUD piscando).
- Hipóteses descartadas: paleta/TMEM como causa das explosões; GTE nativa/cache de vértices como causa das explosões (A/B sem `D2_GTE_NATIVE`/`D2_VTXCACHE` não mudou nada); wrap-around 16 bits no `FrustrumCheck16` (código inofensivo, mas não era a causa).

## Bugs abertos
1. **Triângulos/cunhas esticadas a pé** perto de paredes altas (chão e paredes). Causa medida (`logs/diag_mission1.log`, `drawMesh` modelo 725): vértice À FRENTE da câmera (SZ≈1000), mas projeção passa de ±1024 e a GTE trava em ±1024, achatando dois cantos. Solução estrutural: Tiny3D (recorte no RSP). Tentativa de afrouxar a trava foi revertida.
2. **Sem som no jogo**: `d2_audio_pump` (mixer) só roda na espera de vblank, que nunca acontece no jogo (medido: 0 chamadas). Menu tem som estourado: `d2_spu_shape` chama `mixer_ch_play` com 44100 Hz e perde o pitch; volume sem master; 24 vozes saturam.
3. **Velocidade do jogo**: 1 `StepGame` por quadro desenhado (não-PSX em `main.c`). A ~10 FPS o jogo roda a ~1/3 da velocidade. Versão PSX tem `gMultiStep`. Confirmar com o usuário e decidir a cadência.
4. **Prédios distantes com textura de ruído roxo**: usam `texture_set` não carregada (antes da correção do swap era 21). Reavaliar depois do `0ef5efb`.
5. Bit STP do PS1 ignorado pelo backend (baixa prioridade).

## Desempenho (dados reais, `logs/perf_*.log`, `logs/diag_*.log`)
- Gargalo é CPU. Espera pelo RDP ~0,3%.
- Quadro dirigindo de dia: backend ~65%, montagem da cena ~29%, lógica ~4–9%.
- 144 uploads de textura/quadro, 70% repetidos (ordem de pintor da OT). Recorte em blocos: ~400k ticks para ~42 triângulos.
- Pedestres: 5–14 mil ticks por pedestre (medição anterior de 535k estava errada).
- FPS em gameplay: ~9–12 sem instrumentação.

## Tiny3D (direção atual, igual ao port N64 de Silent Hill)
Referências: https://github.com/SlickAmogus/silent-hill-decomp/tree/n64-port (`n64_port/src/t3d_world.c`, `tools/mkworld.py`), https://github.com/HailToDodongo/tiny3d.
- Flag `D2_T3D_WORLD` (desligada por padrão). Código: `port/n64/d2_t3d_world.cpp`. Commits: `9f24cdd`, `0baf29c`, `0613f08`, `a95ff2e`, `9daab36`, `09749b1`, `9c59e14`, `bd85fa3`, `fd1bb4f`, `30c2efb`.
- 297 modelos de prédio convertidos (permanentes); 200 ficam no caminho antigo; modelos do spool de regiões NÃO convertidos (provavelmente a maioria dos prédios visíveis). Arena de vértices estática 128 KB (107 KB usados); blocos rspq no heap (~174 KB; heap livre ~220 KB).
- **Problema atual: com a flag ligada, nenhum prédio aparece no Ares.** A "validação 0,00 px" foi feita na CPU (`t3d_viewport_calc_viewspace_pos`) e não prova desenho no RSP.
- **Próximo passo imediato:** testar no Ares a ROM `d2_n64_smoke.z64` (commit `30c2efb`, `D2_T3D_SMOKE`): um triângulo vermelho fixo no centro desenhado pelo Tiny3D. Logs já indicam viewport 320×240 criado após `display_init` e mesmo framebuffer do rdpq.
  - Se o triângulo **aparece**: pipeline OK; investigar dados/posição dos prédios (winding, eixo Y, escala, ponto de corte 0x1070 na OT, algo pintando por cima).
  - Se **não aparece**: investigar ordem rspq/rdpq (overlay do Tiny3D carregado? `t3d_frame_start`/modo, sync), scissor, e se o desenho acontece antes de o framebuffer ser anexado/limpo.

## Plano depois do Tiny3D funcionar (parecer do ChatGPT incorporado)
1. Fatia vertical: chão + prédio texturizado + carro do backend antigo com oclusão correta (Z-buffer exige preservar profundidade por vértice) + troca de região (spool).
2. Ligar o som cedo para medir o custo.
3. Decidir cadência da lógica (velocidade do jogo).
4. Texturas offline no conversor, com opção de reduzir resolução.
5. Cortes de "demake" configuráveis e medidos (LOD, distância, tráfego por último).
Meta realista: 20 FPS; 30 FPS só com redução estrutural agressiva.

## Outros projetos analisados (referência)
- RE1 64 (homebrew): libdragon, tudo convertido offline (fundos RGBA16, `.skin`, `.adp`, `.rmv`), sem emulação de VRAM/GPU do PS1.
- X-Men/TMNT arcade N64: Musashi (68000) + blocos nativos + pulo de laço ocioso + vídeo HLE no rdpq + áudio nativo; feitos com agente de IA (Codex).
