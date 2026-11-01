# Ordem do cenário nativo e isolamento do menu — 01/11/2026

Relato do usuário após restaurar as texturas gerais: barras/objetos desenhados
através de paredes, além de retângulos brancos e seleções incorretas no menu.

Problemas comprovados no código anterior:

- Toda a fila Tiny3D era desenhada num ponto fixo da OT (0x1070), antes dos
  objetos legados, independentemente da profundidade relativa.
- A rotina do mundo executava viewport/modos mesmo sem instâncias enfileiradas.
  Nesse caso, também não invalidava o cache do backend ao sair.
- O caminho nativo enviava faces traseiras que PlotBuildingModel/Tile1x1
  normalmente descartam pelo sinal positivo de NCLIP.

Mudança:

- d2_t3d_world_order.inc cria chaves de profundidade por face e intercala os
  desenhos com os baldes da OT, preservando o RSP para transformação/recorte.
- Escala de profundidade reproduz os fatores padrão ZSF3=341 e ZSF4=256,
  incluindo divisão por dois da OT, offset dos tiles e zBias dos prédios.
- Descarte de faces traseiras usa o determinante em espaço de câmera quando
  os três pontos estão à frente do plano próximo. Faces que cruzam o plano
  permanecem para recorte no RSP.
- Cada lote nativo invalida o estado compartilhado na entrada e na saída.
- OT sem cenário não envia comandos gráficos do Tiny3D. Não abre emulador.

Validação realizada sem interface:

- Compilação N64 concluída: build_world_order.log.
- Testes C++ no host usando as funções reais begin/draw_until/end e simulando
  apenas as chamadas gráficas: menu vazio sem chamadas à GPU, intercalamento
  de faces nativas e objetos legados em profundidades distintas, pilha de
  matrizes balanceada e retorno ao menu sem alteração gráfica. Todos passaram.
- git diff --check passou; D2_BENCH/D2_PERFDBG/D2_ORIGIN_PROF desligados.

ROM: d2_n64_world_order.z64 (também d2_n64.z64).
Backup: d2_n64_before_world_order.z64.

Limitações: ordenação por profundidade média, como a OT, não oferece oclusão
por pixel equivalente a Z-buffer. Há um limite explícito de 4096 faces por
quadro (excesso produz diagnóstico). Foram adicionados aproximadamente 49 KB
de dados estáticos para a fila de faces. Não há medição de FPS desta alteração.

Validação visual pendente exclusivamente do usuário: menu principal/submenus,
beco da Missão 1 a pé e dirigindo, paredes e objetos em distâncias diferentes.
Nenhuma execução do Ares foi iniciada nesta correção.

## Retorno do usuário e correção do stride da OT

O usuário confirmou o menu normalizado, mas relatou objetos ainda sobrepostos
às paredes. Encontrado erro em psyx_rdp.cpp: o cálculo da base e do índice da
OT usava sizeof(u_long), 4 bytes, enquanto ClearOTagR e OTTYPE usam OT_TAG
estendido de 8 bytes. Assim, apenas parte da tabela era reconhecida e as
profundidades calculadas não correspondiam às entradas reais.

Todos os cálculos desse percurso agora usam sizeof(OT_TAG), com static_assert
contra P_LEN. Teste no host usando as expressões reais do backend e entradas
de 8 bytes: as 4224 entradas decodificaram para seus respectivos índices e
as 4224 palavras internas de comprimento foram rejeitadas como entradas.
Compilação concluída: build_ot_stride_fix.log.

Nova ROM: d2_n64_ot_stride_fix.z64 (também d2_n64.z64).
Validação visual desta revisão ainda pendente. Não foi iniciado emulador.

## Validação e telemetria

O usuário confirmou o jogo funcional após a revisão de stride. Nova regra:
gerar exclusivamente d2_n64.z64; não criar cópias, backups ou outras ROMs.
Não iniciar/controlar o Ares; validação e execução pertencem ao usuário.

Telemetria opcional: D2_T3D_TELEMETRY=1 (padrão 0), sem D2_BENCH,
D2_PERFDBG ou D2_ORIGIN_PROF. A compilação corrente foi gerada nessa opção.
Em gameplay emite uma linha [T3D_PERF] por 60 percursos de OT com mundo.
Tempos em ticks COP0, médias por percurso:

- prep: preparação total das faces/matrizes, incluindo sort.
- sort: somente qsort (subconjunto de prep).
- render: lotes nativos completos, incluindo bind e emit.
- bind: preparação/upload de textura, incluindo bloqueios internos.
- emit: montagem/envio de vértices, incluindo ring_wait.
- ring_wait: espera explícita antes de reutilizar o buffer de vértices.
- ot_span: intervalo de preparação até fim da OT, incluindo desenho legado;
  NÃO é tempo do quadro completo nem tempo exclusivo do RSP/RDP.
- faces/vtx/tris/batches/matrices: contagens médias por percurso.
- rings/missing: totais na janela de 60 percursos.

Não somar tempos inclusivos/subconjuntos. São medidas instrumentadas; ganhos
de FPS devem ser confirmados recompilando sem telemetria, na mesma trajetória.
Não há novo resultado de FPS: somente compilação, sem execução do emulador.

Candidatos à otimização, ainda dependentes de perfil: recorte por UV repetido
por instância/quadro, reconstrução de vértices, transformação CPU duplicada
para classificação/culling, qsort, trocas de estado/TMEM em cada lote, carros
legados e áudio. Priorizar amortização/cache e fluxo de comandos antes de
reduzir geometria, tráfego ou visibilidade.
