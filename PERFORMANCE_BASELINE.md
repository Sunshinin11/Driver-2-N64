# Referencia de desempenho e recuperacao do port

## Melhor desempenho historico informado pelo autor

O commit a30b79da425814c27c1a5405fdb670ac32d69d6c (10/11/2026, 22:49 BRT)
e a referencia de melhor desempenho historico do projeto, segundo Georges.
Nao confundir essa referencia com a versao mais completa em conteudo.
Preservar as quatro cidades, musicas, videos e o limite de ROM de 64 MiB.

Na coleta local de 14/11, no mesmo percurso informado pelo usuario, o intervalo
390-2580 de quadros apresentou 20,02 FPS na antiga e 15,69 FPS na versao atual
entao testada (delta frames * 60 / delta VBlanks). As janelas nao alinham cenas
por CameraCnt; esses numeros descrevem as sessoes, nao uma causa isolada.
A versao antiga tambem apresentou picos de streaming e nao atingiu 30 FPS fixos.

Claude informou outro teste: aproximadamente 2,00 M contra 2,33 M ticks/quadro,
com leitura + descompressao representando 4-5% do tempo atual. Sao resultados
relatados, nao reproduzidos neste ambiente; naquela antiga o XA estava desligado.
Claude tambem informou que -Os piorou para 2,66 M: manter -O2 enquanto se isolam
as outras causas. Nao somar percentuais dessas duas metodologias.

## Plano autorizado em 14/11/2026

1. Registrar o estado atual antes de alterar o renderizador.
2. Restaurar a integracao de desenho dos carros do a30b79d, removendo o caminho
   de carroceria Tiny3D. Preservar saves, carregamento/correcao de endianness de
   carros especiais e cutscenes. A antiga desabilitava carros especiais; nao
   recuperar essa limitacao ao restaurar apenas o desenho.
3. Instrumentar o LEVPACK e oferecer cache pequeno de blocos para teste A/B,
   desligado por padrao ate verificar RAM e desempenho.
4. Validar compilacao e imagem no Ares antes de declarar ganho de performance.

Nao atribuir toda a regressao ao tamanho da ROM. Nao remover mapas ou videos.
ROMs, dumps de RAM, logs e arquivos temporarios permanecem fora do Git.

## Implementacao: carros e LEVPACK

O desenho dos carros voltou a chamar plotCarPolyGT3 diretamente, como em a30b79d.
O modulo d2_t3d_cars.inc, o pacote 0xB5 e seus hooks foram removidos. Os regex de
migracao em patch_engine.py tambem limpam fontes gerados anteriormente; remover
apenas o patch de criacao nao removeria os hooks de uma arvore ja remendada.
Saves e as correcoes de carregamento de carros especiais continuam presentes.

LEVPACK mantem o formato existente e um bloco de cache por padrao. O descompressor
LZ4 nao foi trocado. LPK_CACHE_BLOCKS=2 ou 4 habilita LRU com armazenamento estatico;
LPK_PROFILE=1 habilita contadores e timers. Perfil e cache ampliado ficam desligados
no build padrao. Nenhum ganho de FPS ou seguranca de RAM no N64 foi validado ainda.

Comandos dentro do WSL, na raiz do projeto (cada um substitui d2_n64.z64):

    bash build.sh LPK_CACHE_BLOCKS=1 LPK_PROFILE=1
    bash build.sh LPK_CACHE_BLOCKS=2 LPK_PROFILE=1
    bash build.sh LPK_CACHE_BLOCKS=4 LPK_PROFILE=1

Testar primeiro a restauracao dos carros com o build padrao e mesma rota. Depois
comparar cache 1/2/4 mantendo perfil ligado em todos os lados. Voltar ao padrao:

    bash build.sh LPK_CACHE_BLOCKS=1 LPK_PROFILE=0

O arquivo de flags existente registra esses -D e provoca recompilacao quando mudam.
Nao alterar GAME_OPT nessa comparacao: manter -O2.

A linha [lpk] aparece a cada 300 quadros. hit/miss contam consultas por setor, nao
pedidos de leitura completos; leituras sequenciais no mesmo bloco geram acertos.
Comparar tambem decode, evict, io_ticks e decode_ticks. Somar os ticks e dividir
por 300 e 46875000 para obter segundos medios por quadro naquela janela. A primeira
janela inclui carregamento inicial. Os timers cobrem leitura do payload e decode,
nao busca de nomes, carga de tabelas nem copias finais para o motor.

Cache 2/4 custa aproximadamente mais 16/49 KiB que um bloco, alem de metadados.
Conferir mapa ELF, pilha e heap real; o antigo campo livre_aprox do log nao mede
memoria livre total. Nao habilitar quatro blocos por padrao sem validar a margem.

Verificacao local: tools/test_lpk_cache.py compila o codigo real do carregador em
WebAssembly com I/O simulado e executa em Node. Cobre LRU, blocos brutos/comprimidos,
ultimo bloco parcial, leitura entre blocos, erros e perfil ligado/desligado. Nao
substitui compilacao MIPS, teste visual no Ares ou medicao em hardware real.
