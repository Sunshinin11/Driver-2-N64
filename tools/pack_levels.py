#!/usr/bin/env python3
"""Empacota os arquivos .LEV (LEVELS, NLEVELS, MLEVELS, MNLEVELS de todas as cidades) em um unico LEVPACK.BIN:
  - setores de 2048 bytes deduplicados entre todos os arquivos (cidades e dia/noite repetem muitos setores);
  - o conjunto de setores unicos (pool) e comprimido em blocos de BLOCK_SECTORS setores (LZ4, acesso aleatorio por bloco).
O jogo le os .LEV por setor (d2_read_sectors), entao a leitura aleatoria por bloco substitui o arquivo original.
Formato (tudo big-endian):
  header : 'LPK1', nfiles, block_sectors, nblocks, npool, files_off, map_off, blocks_off, data_off   (9 x u32)
  files  : nfiles x { char name[48]; u32 size_bytes; u32 nsectors; u32 map_index } (60 bytes)
  map    : u16 por setor de cada arquivo (id do setor no pool)
  blocks : (nblocks+1) x u32 offset_in_data (bit 31 = bloco bruto; o ultimo e o tamanho total)
  data   : blocos comprimidos
Uso: pack_levels.py <pasta DRIVER2> <saida LEVPACK.BIN> [--remove]   (--remove apaga os .LEV originais da pasta de staging)"""
import sys, os, struct, hashlib, glob
import lz4.block

BLOCK_SECTORS = 8
SECTOR = 2048

def main():
    src, out = sys.argv[1], sys.argv[2]
    remove = '--remove' in sys.argv
    files = []
    for d in ('LEVELS', 'NLEVELS', 'MLEVELS', 'MNLEVELS'):
        for p in sorted(glob.glob(os.path.join(src, d, '*.LEV'))):
            files.append((d + '/' + os.path.basename(p), p))
    # arquivos de dados lidos por Loadfile/LoadfileSeg inteiros (imagens, replays, cutscenes): comprimem bem
    for d in ('DATA', 'GFX', 'REPLAYS', 'MISSIONS', 'SOUND'):
        for dp, _, names in sorted(os.walk(os.path.join(src, d))):
            for n in sorted(names):
                p = os.path.join(dp, n)
                files.append((os.path.relpath(p, src).replace(os.sep, '/'), p))
    if not files:
        if os.path.exists(out): print('LEVPACK ja gerado'); return
        sys.exit('nenhum .LEV encontrado em ' + src)
    pool = []; seen = {}; maps = []; sizes = []
    for name, p in files:
        data = open(p, 'rb').read()
        sizes.append(len(data))
        n = (len(data) + SECTOR - 1) // SECTOR
        data += b'\0' * (n * SECTOR - len(data))
        m = []
        for i in range(n):
            s = data[i * SECTOR:(i + 1) * SECTOR]
            h = hashlib.md5(s).digest()
            pid = seen.get(h)
            if pid is None:
                pid = len(pool); seen[h] = pid; pool.append(s)
            m.append(pid)
        maps.append(m)
    npool = len(pool)
    if npool > 65535: sys.exit('pool grande demais para ids de 16 bits')
    nblocks = (npool + BLOCK_SECTORS - 1) // BLOCK_SECTORS
    blocks = []; datablob = bytearray()
    for b in range(nblocks):
        raw = b''.join(pool[b * BLOCK_SECTORS:(b + 1) * BLOCK_SECTORS])
        c = lz4.block.compress(raw, mode='high_compression', compression=12, store_size=False)
        if len(c) >= len(raw):
            blocks.append(len(datablob) | 0x80000000); datablob += raw
        else:
            blocks.append(len(datablob)); datablob += c
    blocks.append(len(datablob))   # sentinela: tamanho total
    nfiles = len(files)
    files_off = 36
    map_off = files_off + nfiles * 60
    blocks_off = map_off + sum(len(m) for m in maps) * 2
    data_off = blocks_off + (nblocks + 1) * 4
    o = bytearray(b'LPK1' + struct.pack('>8I', nfiles, BLOCK_SECTORS, nblocks, npool, files_off, map_off, blocks_off, data_off))
    idx = 0
    for (name, _), m, sz in zip(files, maps, sizes):
        o += name.encode().ljust(48, b'\0') + struct.pack('>3I', sz, len(m), idx)
        idx += len(m)
    for m in maps:
        o += struct.pack('>%dH' % len(m), *m)
    o += struct.pack('>%dI' % len(blocks), *blocks)
    o += datablob
    open(out, 'wb').write(o)
    tot = sum(sizes)
    print('LEVPACK: %d arquivos, %.1f MB -> %.1f MB (setores unicos %d de %d, blocos %d)' %
          (nfiles, tot / 1e6, len(o) / 1e6, npool, sum(len(m) for m in maps), nblocks))
    if remove:
        for _, p in files:
            if os.path.exists(p): os.remove(p)

main()
