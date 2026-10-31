# Driver 2 N64 — Estado do port e passagem de bastão

Atualizado em 31/10/2026. Projeto: `C:\Desenvolvimento\Nintendo 64\Driver 2 64\D2_N64` (versão principal). Base: REDRIVER2 (decomp do PS1) + libdragon (preview) + Tiny3D + Expansion Pak (8 MB). Testes no Ares v148. Build: `./build.sh` via WSL (`/opt/libdragon`).

## Regras de trabalho (o usuário exige)
- Um problema por vez. Pesquisa e comparação de código ANTES de qualquer build. Nada de "tiro no escuro".
- Nenhuma causa é considerada resolvida antes do teste visual/sonoro do usuário no Ares.
- Todo número de desempenho precisa vir de log da execução (citar arquivo e linha). Relatórios anteriores já inventaram números: sempre conferir os logs.
- FPS de referência SEMPRE com `D2_PERFDBG=0` e `D2_ORIGIN_PROF=0`: `Δframes × 60 / Δvbl` (linhas `[fps]`) e janelas determinísticas `[bench] cc=...` alinhadas por `CameraCnt`.
- Um commit por alteração conceitual; ROM raiz `d2_n64.z64` sempre jogável (`D2_BENCH=0`).
- Não repetir hipóteses já descartadas (lista abaixo).

## Corrigido e validado no Ares (sessões anteriores)
| Problema | Causa | Commit |
|---|---|---|
| Tela de loading com metade em ruído | `load_tile` com largura 0 dava a volta na TMEM e apagava a TLUT de 256 cores | `dbf8f93` |
| Prédios invisíveis + explosões de vértices (parede rosa em Gold Coast) | `d2_swap_model` convertia o bloco de colisão com 11 shorts por pacote; `COLLISION_PACKET` tem 10 | `0ef5efb` |
| Placas gigantes de chão perto da câmera | Regra da GPU do PS1 (descarta triângulo > 1023×511) em `tri_core` | `ef24b11` |
| Ruído no HUD e nas batidas / luzes / sombras / fumaça | Blender aditivo do RDP não satura; alfa proporcional ao brilho | `fa14399`..`1000333` |
| Sombra do Tanner (quadrado com lixo) | Render offscreen não suportado no backend; sombra desligada | `8c563d6` (`D2_NO_TANNER_SHADOW`) |
| "L" vermelho da seta de missão a pé | Vértice atrás da câmera (divisão saturada) | `3d5a70f` |
| Input automático atrapalhando testes | Roteiro de botões só sob `D2_BENCH` | já em `main` |

## Implementado e medido nesta frente (aguardando validação visual/sonora no Ares)
Detalhes completos e linhas de log em `docs/T3D_ETAPA2_MEDICAO.md` e `docs/T3D_MEDICAO_2026-10-31.md`:

| Etapa | Commit | O que mudou | Resultado medido (`D2_ORIGIN_PROF=0 D2_PERFDBG=0`) |
|---|---|---|---|
| Base T3D | `471fa86` | Correção da matriz/luz do Tiny3D (prédios permanentes visíveis) | `4.777.658 -> 4.332.664` ticks em `cc=64` (`9,85 -> 10,85` FPS) |
| Etapa 1 | `3fffab3` | Perfil leve por origem (`D2_ORIGIN_PROF`) + benchmark determinístico por `CameraCnt` | Mesma posição `(vx,vz)` garantida entre builds lentas e rápidas |
| Etapa 2a | `c2938c2` | Migração de chão/ruas/água/grama (`DrawTILES`, 132 modelos + LODs) + 293 prédios/canteiros (`425` modelos no RSP, `7.986/9.216` vértices = `124 KB`) | `GROUND` caiu `-87,7%` (`1.367.262 -> 165.138` ticks em `T3D_GRD`, `0` B de upload de textura); Missão 1 a pé chegou a **`19,39` FPS** (`logs/e2_m1_t3d_ref.log:1833`) |
| Etapa 2b | `9d5edf2` | Culling PSX de quadrantes em `DrawSkyDome` + subdivisão inteira `2x2` (`64x42 <= 2 KB` TMEM) em `PlotSkyPoly` | `uvclip` do céu `38 -> 0`; gameplay (sem áudio bombeado) subiu para **`17,78` FPS** (`2.654.762` ticks em `cc=64`, **`-44,4%` ticks / `+80,5%` FPS** sobre o legado; `logs/e2b_t3d_ref.log:266`) |
| Etapa 2c | `36b5a73` | `VsyncProc` removido da ISR de VI (`0` `VI WARNING`, `0` corridas no mixer), `d2_audio_pump()` em `BeginScene`/`EndScene`, correção de pitch/volume SPU, mixer a `32 kHz` (`152.992` B de heap livre) e busca ADPCM $O(1)$ (`d->loopBlock`) | Áudio ativo em gameplay medido: **`13,71` FPS** (`3.440.048` ticks em `cc=64`, `logs/e2c_opt_ref.log:146`), economizando até **`2,45M` ticks/quadro (`-27,8%`)** vs busca ADPCM $O(N)$ (`logs/e2c_t3d_ref.log:200,206`) |

## Revertido (não funcionou / causou regressões) — NÃO repetir
- `D2_GTE_WIDE` (projeção sem trava de ±1024), agrupamento por textura dentro de cada balde da OT, evitar reenvio de TMEM, recorte em blocos mais barato, cache de `get_region`. Revertidos em `69213c5`..`ddc3a7c`.
- Hipóteses descartadas: paleta/TMEM como causa das explosões; GTE nativa/cache de vértices como causa das explosões; wrap-around 16 bits no `FrustrumCheck16`.

## Próximos passos priorizados por ganho medido (rumo aos 20 FPS estáveis COM áudio)
1. **Áudio SPU nativo em `WAVEFORM_FORMAT_VADPCM` no RSP (`port/n64/psyx_backend.cpp` + ferramenta offline)**:
   - Hoje a decodificação por software de até 24 vozes ADPCM do PS1 + `XM_Update` consome `~514K` ticks/quadro a `13,7 FPS` e `1,75M–2,0M` ticks/quadro em tráfego pesado (`13%–27%` contínuos da CPU VR4300). O `mixer` do libdragon decodifica `VADPCM` diretamente em DMEM no RSP com **zero CPU por amostra**. Avaliar também flag configurável `D2_NO_XM_MUSIC` / limite de canais XM.
2. **Carros nativos no Tiny3D (`DrawAllTheCars` / `CARS`)**:
   - Maior custo gráfico isolado restante: `600K–672K` ticks com 4 carros (`cc=96`) até **`1,78M–2,58M` ticks** com 9–12 carros (`cc=352..448`).
3. **Etapa 3 — Texturas CI4 nativas + ciclo de vida de spool (`d2_t3d_on_spool_change`)**:
   - Atualmente os 425 modelos no Tiny3D usam cores por face/vértice amostradas da textura PS1 (`sample_poly_color`) sem textura TMEM. Adicionar materiais CI4 pré-empacotados (`<= 2 KB` TMEM) e reconstrução incremental de slots de região no spool.
4. **Etapa 4 — Z-buffer + 72 modelos restantes (`PERM_BLD` com `uvclip`)**:
   - Ativar Z-buffer (`display_init` + `rdpq_attach` com Z-buf) preservando profundidade quando o backend legado desenhar carros/pedestres sobre o mundo Tiny3D, e migrar postes/árvores/adereços que ainda custam `254K–502K` ticks/quadro em `PERM_BLD`.
5. **Cadência da simulação (`StepGame` / `gMultiStep`) e cortes configuráveis de demake (`D2_DRAW_DISTANCE`, LOD, tráfego)**:
   - Ajustar a velocidade real do jogo após estabilizar o orçamento de CPU com áudio.
