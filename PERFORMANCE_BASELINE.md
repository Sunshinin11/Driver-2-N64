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

## Teste guiado pelo PowerShell

Na pasta D2_N64, executar primeiro:

    powershell -ExecutionPolicy Bypass -File .\testar_levpack.ps1 -Blocos 1

Percorrer a rota habitual durante 180 segundos. Depois repetir com:

    powershell -ExecutionPolicy Bypass -File .\testar_levpack.ps1 -Blocos 2

Os dois testes ligam o mesmo perfil e substituem a mesma d2_n64.z64. Geram
telemetria_lpk1.log / telemetria_lpk2.log, stderr e registro de build com flags,
SHA256 e tamanho da ROM. A coleta so inicia se o build tiver sucesso e as flags
corresponderem. A execucao anterior da mesma configuracao e substituida.
O script encerra o Ares aberto, inicia a ROM para jogar e fecha no fim da coleta.
A configuracao normal continua: .\build_d2.ps1 (1 bloco, perfil desligado).

## A/B historico com conteudo completo (14/11/2026)

Executar .\build_ab_a30b79d_completo.ps1 para preparar e compilar uma arvore
isolada em /tmp, diretamente do commit a30b79da425814c27c1a5405fdb670ac32d69d6c.
A pasta atual e d2_n64.z64 nao sao substituidas. Saida separada:
d2_n64_a30b79d_completo.z64; registro: build_a30b79d_completo.log.

Somente oito arquivos diferem da base: Makefile.d2 (selecao e empacotamento),
fs_n64.cpp (LEVPACK original), patch_engine.py (sete patches de acesso ao pacote),
convert_xa_wav64.py, convert_fmv_n64.py, pack_levels.py e inicializacao do codec
ULC em psyx_backend.cpp/fmv_n64.cpp. Fontes de conteudo vindos de 49edd99.
Nenhum fonte e copiado da arvore de trabalho atual. Manifesto de hashes gerado.

Preservados exatamente: renderizador, carros, chao, paletas, hot.ld, flags de
compilacao (inclusive -Os seletivo original), logica, conversores de musica/SFX,
SDK selecionado pelo ambiente e commit Tiny3D da base. Libdragon/toolchain do
ambiente nao sao restaurados a uma versao antiga; comparar ROMs com o mesmo SDK.
Sem multistep posterior, saves novos, correcao posterior de carro especial,
cache LRU recente, perfil LPK ou ajuste posterior de volume XA.

O LEVPACK e a conversao de midia sao necessarios para acomodar o conteudo completo
em 64 MiB. Essa ROM testa o motor historico com essa integracao; nao e um binario
identico ao historico com apenas arquivos extras no DFS. As limitacoes de missoes,
cutscenes e carro especial da base continuam. Os videos usam a conversao do
pacote completo (incluindo parametros de qualidade/tempo desse conversor).

O build exige os dois discos em data/, internet para fontes fixados, SDK e ffmpeg,
e python3-lz4 no WSL. So copia a ROM apos verificar tamanho <=64 MiB, quatro cidades
no pacote e 33 videos no staging. Fontes atuais e ROM principal ficam preservados.
Nenhuma compilacao MIPS ou validacao visual desta variante foi feita nesta sessao
porque o WSL devolveu Wsl/E_ACCESSDENIED. Nao ha ganho de desempenho declarado.

## Diagnostico da integracao sobre a base historica

Executar no PowerShell: powershell -ExecutionPolicy Bypass -File .\testar_integracao.ps1
O comando prepara/compila uma arvore isolada, gera d2_n64_a30b79d_diagnostico.z64,
e abre o Ares por 180 s. Log: telemetria_a30b79d_diagnostico.log. As outras ROMs
nao sao substituidas. WSL bloqueado nesta sessao: a compilacao MIPS permanece
pendente de execucao no PowerShell do usuario.

Instrumentacao especifica adicionada sobre a30b79d + conteudo completo:
- [content]: ticks inclusivos por janela de 30 quadros; fs_total e chamadas
  externas do carregador, find e busca/inicializacao, map e troca de tabela,
  seek, read, decode LZ4 e copy do bloco para o destino final.
- [content_calls]: numero de chamadas dessas categorias.
- [content_audio]: CPU nos callbacks de VBlank, CPU em mixer_try_play e
  quantidade de quadros amostrados com XA efetivamente tocando.

Ticks CP0 / 46875 = milissegundos. Dividir por n para media por quadro.
fs_total inclui seus filhos; find/map tambem incluem suas leituras. Nao somar
categorias inclusivas. A instrumentacao acrescenta custo e altera o layout.
Tempo do mixer inclui esperas, nao mede sozinho execucao assincrona do RSP.
Tempos fora das categorias nao podem ser atribuidos automaticamente a layout.
O diagnostico orienta a proxima variante isolada; nao garante atribuir toda
regressao a um unico subsistema sem comparacao adicional.

Verificados preparacao, sintaxe Python/PowerShell e sintaxe C++ do carregador
instrumentado e do coletor com stubs de plataforma. Ainda sem validacao MIPS
ou visual no Ares desta variante. Sem mudancas no renderer de producao.
