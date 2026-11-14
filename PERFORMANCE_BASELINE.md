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
