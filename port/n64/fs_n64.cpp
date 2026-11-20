// Substitui utils/fs.cpp (que depende de Windows/glob do Linux). No N64 os caminhos usam '/', e nao ha busca em pasta por enquanto.
#include "../redriver2/src_rebuild/utils/fs.h"

void FS_FixPathSlashes(char *pathbuff)
{
    while (*pathbuff) { if (*pathbuff == '\\') *pathbuff = '/'; pathbuff++; }
}
const char *FS_FindFirst(const char *wildcard, FS_FINDDATA **findData) { if (findData) *findData = 0; return 0; }
const char *FS_FindNext(FS_FINDDATA *findData) { return 0; }
void        FS_FindClose(FS_FINDDATA *findData) {}
bool        FS_FindIsDirectory(FS_FINDDATA *findData) { return false; }

// Texturas HQ (TGA) nao existem no N64: o jogo usa as fontes/texturas originais do PS1.
bool LoadTGAImage(const char *, unsigned char **, int &, int &, int &) { return false; }

// ---- Leitura de setores (2048 B) do arquivo de nivel (.LEV) direto da ROM: o spool do PC lia o arquivo inteiro na RAM (16 MB), aqui nao cabe. ----
#include <stdio.h>
#include <string.h>
#include <limits.h>
#ifdef D2_DIAG_FS
#include <stdint.h>
static struct {
    char asset[64];
    uintptr_t destination;
    long fileSize;
    int sector, count, actual;
} readTrace;

extern "C" void d2_save_read_trace(volatile uint32_t *out)
{
    out[1] = readTrace.destination;
    out[2] = readTrace.sector;
    out[3] = readTrace.count;
    out[4] = readTrace.actual;
    out[5] = readTrace.fileSize;
    volatile char *asset = (volatile char *)&out[6];
    for (unsigned i = 0; i < sizeof(readTrace.asset); ++i)
        asset[i] = readTrace.asset[i];
    out[0] = 0x44324653; // D2FS
}
#endif
extern char g_CurrentLevelFileName[64];
extern FILE *d2_fopen(const char *path, const char *mode);

// ---- LEVPACK.BIN: todos os .LEV (cidades, dia/noite, multiplayer) em um pacote unico com setores deduplicados e blocos LZ4 (tools/pack_levels.py) ----
#include <stdlib.h>
#include <debug.h>
#include <stdint.h>
#ifndef D2_LPK_CACHE_BLOCKS
#define D2_LPK_CACHE_BLOCKS 1
#endif
#ifndef D2_LPK_PROFILE
#define D2_LPK_PROFILE 0
#endif
#if D2_LPK_CACHE_BLOCKS < 1 || D2_LPK_CACHE_BLOCKS > 4
#error "D2_LPK_CACHE_BLOCKS deve ser 1, 2, 3 ou 4"
#endif
#if D2_LPK_PROFILE
#include <libdragon.h>
#define LPK_STAT(expr) do { expr; } while (0)
#else
#define LPK_STAT(expr) do {} while (0)
#endif
// No N64: blocos lidos por DMA direto da ROM (sem stdio/DFS por leitura) e LZ4 em assembly da libdragon.
// Fora do N64 (tools/test_lpk_cache.py) ou com -DD2_LPK_STDIO: fseek/fread e o lz4_decode em C abaixo.
#if defined(N64) && !defined(D2_LPK_STDIO)
#define D2_LPK_DMA 1
#include <libdragon.h>
extern "C" int decompress_lz4_full_inplace(const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size);
#else
#define D2_LPK_DMA 0
#endif
namespace {
struct LpkFile { char name[48]; uint32_t size, nsec, mapIdx; };
enum { LPK_BLOCK_BYTES = 8 * 2048, LPK_MAX_MAP = 5400, LPK_MAX_BLOCKS = 4200, LPK_MAX_FILES = 128, LPK_MARGIN = 256 };
// tudo em .bss (~40 KB): o heap do N64 esta no limite, nada e alocado em tempo de execucao
// Cache fixo: nenhuma alocacao no heap; o padrao preserva um bloco.
// Isola buffers/tabelas exclusivos do LEVPACK em .gnu.linkonce.b.lpk: no n64.ld essa secao fica dentro da .bss
// (zerada no boot antes de __bss_end/end), porem apos *(.bss) e *(.bss*) do renderizador e do audio.
#define D2_LPK_BSS __attribute__((section(".gnu.linkonce.b.lpk")))

struct LpkCacheSlot {
    unsigned char data[LPK_BLOCK_BYTES + LPK_MARGIN] __attribute__((aligned(16)));
    uint32_t block, lastUse;
    bool valid;
};
D2_LPK_BSS LpkCacheSlot lpkCache[D2_LPK_CACHE_BLOCKS];
#if D2_LPK_CACHE_BLOCKS > 1
uint32_t lpkCacheClock;
#endif
#if D2_LPK_PROFILE
struct LpkStats {
    uint32_t hits, misses, evictions, decoded, raw, errors;
    uint32_t ioTicks, decodeTicks, inputBytes, outputBytes;
};
D2_LPK_BSS LpkStats lpkStats;
#endif
D2_LPK_BSS uint16_t lpkMapBuf[LPK_MAX_MAP];
D2_LPK_BSS uint32_t lpkBlocks[LPK_MAX_BLOCKS + 1];
D2_LPK_BSS LpkFile lpkFiles[LPK_MAX_FILES];
FILE *lpkFp; bool lpkTried, lpkOk;
uint32_t lpkNFiles, lpkNBlocks, lpkNPool, lpkDataOff, lpkMapOff; const LpkFile *lpkMapFile;
#if D2_LPK_DMA
uint32_t lpkRom;   // endereco PI do LEVPACK.BIN (0: sem DMA direto, usa stdio)
#endif

// decodificador LZ4 (formato de bloco); a origem pode ficar no fim do mesmo buffer da saida (descompressao no lugar)
__attribute__((unused)) bool lz4_decode(const unsigned char *src, int slen, unsigned char *dst, int dlen)
{
    const unsigned char *se = src + slen; unsigned char *d = dst, *de = dst + dlen;
    while (src < se)
    {
        const unsigned tok = *src++;
        unsigned lit = tok >> 4;
        if (lit == 15) { unsigned b; do { if (src >= se) return false; b = *src++; lit += b; } while (b == 255); }
        if (lit > (unsigned)(se - src) || lit > (unsigned)(de - d)) return false;
        memmove(d, src, lit); d += lit; src += lit;
        if (src >= se) break;
        if (se - src < 2) return false;
        const unsigned off = src[0] | (src[1] << 8); src += 2;
        unsigned ml = tok & 15;
        if (ml == 15) { unsigned b; do { if (src >= se) return false; b = *src++; ml += b; } while (b == 255); }
        ml += 4;
        if (off == 0 || off > (unsigned)(d - dst) || ml > (unsigned)(de - d)) return false;
        const unsigned char *m = d - off;
        if (off >= ml) { memcpy(d, m, ml); d += ml; }
        else { while (ml--) *d++ = *m++; }
    }
    return d == de;
}
bool lpk_init()
{
    if (lpkTried) return lpkOk;
    lpkTried = true;
    lpkFp = fopen("rom:/DRIVER2/LEVPACK.BIN", "rb");
    if (!lpkFp) return false;
    uint32_t h[9];
    if (fread(h, 4, 9, lpkFp) != 9 || h[0] != 0x4C504B31u) return false;
    lpkNFiles = h[1]; lpkNBlocks = h[3]; lpkNPool = h[4]; lpkDataOff = h[8]; lpkMapOff = h[6];
    if (h[2] != 8 || lpkNFiles > LPK_MAX_FILES || lpkNBlocks > LPK_MAX_BLOCKS) { debugf("[LPK] pacote maior que as tabelas\n"); return false; }
    if (fseek(lpkFp, h[5], SEEK_SET) != 0 || fread(lpkFiles, sizeof(LpkFile), lpkNFiles, lpkFp) != lpkNFiles) return false;
    for (uint32_t i = 0; i < lpkNFiles; i++) if (lpkFiles[i].nsec > LPK_MAX_MAP) { debugf("[LPK] arquivo maior que o mapa\n"); return false; }
    if (fseek(lpkFp, h[7], SEEK_SET) != 0 || fread(lpkBlocks, 4, lpkNBlocks + 1, lpkFp) != lpkNBlocks + 1) return false;
#if D2_LPK_DMA
    lpkRom = (uint32_t)dfs_rom_addr("DRIVER2/LEVPACK.BIN");
    debugf("[LPK] leitura %s\n", lpkRom ? "por DMA direto" : "por stdio");
#endif
    return lpkOk = true;
}
// mapa de setores do arquivo (carregado sob demanda: so o do nivel atual fica na RAM)
bool lpk_use_map(const LpkFile *f)
{
    if (lpkMapFile == f) return true;
    if (fseek(lpkFp, lpkMapOff + f->mapIdx * 2, SEEK_SET) != 0 || fread(lpkMapBuf, 2, f->nsec, lpkFp) != f->nsec) { lpkMapFile = NULL; return false; }
    lpkMapFile = f;
    return true;
}
const LpkFile *lpk_find(const char *path)
{
    if (!lpk_init()) return NULL;
    const size_t pl = strlen(path);
    for (uint32_t i = 0; i < lpkNFiles; i++)
    {
        const size_t nl = strlen(lpkFiles[i].name);
        if (nl > pl) continue;
        const char *tail = path + pl - nl;
        if (tail != path && tail[-1] != '/' && tail[-1] != '\\') continue;
        size_t k = 0;
        for (; k < nl; k++) { char a = tail[k], b = lpkFiles[i].name[k]; if (a == '\\') a = '/'; if (a >= 'a' && a <= 'z') a -= 32; if (a != b) break; }
        if (k == nl) return &lpkFiles[i];
    }
    return NULL;
}
static bool lpk_read_payload(uint32_t offset, void *dst, uint32_t bytes)
{
#if D2_LPK_PROFILE
    const uint32_t t0 = (uint32_t)get_ticks();
#endif
    bool ok;
#if D2_LPK_DMA
    // DMA exige RAM e ROM com a mesma paridade; o chamador alinha o destino quando pode, senao cai no stdio
    if (lpkRom && (((uintptr_t)dst ^ (lpkRom + offset)) & 1) == 0)
    {
        data_cache_hit_writeback_invalidate(dst, bytes);
        dma_read(dst, lpkRom + offset, bytes);
        ok = true;
    }
    else
#endif
    ok = fseek(lpkFp, offset, SEEK_SET) == 0 && fread(dst, 1, bytes, lpkFp) == bytes;
    LPK_STAT(lpkStats.ioTicks += (uint32_t)get_ticks() - t0);
    LPK_STAT(if (ok) lpkStats.inputBytes += bytes; else lpkStats.errors++);
    return ok;
}
const unsigned char *lpk_block(uint32_t b)
{
    if (b >= lpkNBlocks) { LPK_STAT(lpkStats.errors++); return NULL; }
    int victim = 0;
#if D2_LPK_CACHE_BLOCKS == 1
    if (lpkCache[0].valid && lpkCache[0].block == b) {
        LPK_STAT(lpkStats.hits++);
        return lpkCache[0].data;
    }
#else
    const uint32_t now = ++lpkCacheClock;
    for (int i = 0; i < D2_LPK_CACHE_BLOCKS; i++) {
        if (lpkCache[i].valid && lpkCache[i].block == b) {
            lpkCache[i].lastUse = now;
            LPK_STAT(lpkStats.hits++);
            return lpkCache[i].data;
        }
    }
    for (int i = 0; i < D2_LPK_CACHE_BLOCKS; i++) {
        if (!lpkCache[i].valid) { victim = i; break; }
        if (now - lpkCache[i].lastUse > now - lpkCache[victim].lastUse) victim = i;
    }
#endif
    LPK_STAT(lpkStats.misses++);
    LPK_STAT(if (lpkCache[victim].valid) lpkStats.evictions++);
    lpkCache[victim].valid = false;
    unsigned char *lpkSlot = lpkCache[victim].data;
    const uint32_t o0 = lpkBlocks[b], o1 = lpkBlocks[b + 1];
    const bool raw = (o0 & 0x80000000u) != 0;
    const uint32_t csize = (o1 & 0x7FFFFFFFu) - (o0 & 0x7FFFFFFFu);
    const uint32_t rawBytes = (b == lpkNBlocks - 1) ? (lpkNPool - b * 8) * 2048 : LPK_BLOCK_BYTES;
    if (csize > rawBytes || (raw && csize != rawBytes)) { LPK_STAT(lpkStats.errors++); return NULL; }
    if (raw) {
        if (!lpk_read_payload(lpkDataOff + (o0 & 0x7FFFFFFFu), lpkSlot, csize)) return NULL;
        LPK_STAT(lpkStats.raw++);
    } else {
        unsigned char *cp = lpkSlot + rawBytes + LPK_MARGIN - csize;
#if D2_LPK_DMA
        // mesmo alinhamento de 8 bytes da origem na ROM (DMA rapido); recua no maximo 7 bytes dentro da margem
        if (lpkRom) cp -= ((uintptr_t)cp - (lpkRom + lpkDataOff + (o0 & 0x7FFFFFFFu))) & 7;
#endif
        if (!lpk_read_payload(lpkDataOff + (o0 & 0x7FFFFFFFu), cp, csize)) return NULL;
#if D2_LPK_PROFILE
        const uint32_t t0 = (uint32_t)get_ticks();
#endif
#if D2_LPK_DMA
        // descompressao no lugar: margem exigida (csize>>8)+32 <= 96 bytes, o slot reserva LPK_MARGIN (256) apos o bloco
        const bool ok = decompress_lz4_full_inplace(cp, csize, lpkSlot, rawBytes) == (int)rawBytes;
#else
        const bool ok = lz4_decode(cp, (int)csize, lpkSlot, (int)rawBytes);
#endif
        LPK_STAT(lpkStats.decodeTicks += (uint32_t)get_ticks() - t0);
        if (!ok) { LPK_STAT(lpkStats.errors++); return NULL; }
        LPK_STAT(lpkStats.decoded++);
    }
    LPK_STAT(lpkStats.outputBytes += rawBytes);
    lpkCache[victim].block = b;
#if D2_LPK_CACHE_BLOCKS > 1
    lpkCache[victim].lastUse = now;
#endif
    lpkCache[victim].valid = true;
    return lpkSlot;
}
// le 'n' bytes a partir do byte 'off' do arquivo (dentro do pacote); devolve bytes lidos
long lpk_read(const LpkFile *f, long off, void *dst, long n)
{
    unsigned char *out = (unsigned char *)dst; long done = 0;
    if (!lpk_use_map(f)) return 0;
    if (off < 0 || off >= (long)f->size) return 0;
    if (n > (long)f->size - off) n = (long)f->size - off;
    while (done < n)
    {
        const long pos = off + done, sec = pos / 2048, so = pos % 2048;
        const uint32_t pid = lpkMapBuf[sec];
        const unsigned char *blk = lpk_block(pid / 8);
        if (!blk) break;
        long chunk = 2048 - so; if (chunk > n - done) chunk = n - done;
        memcpy(out + done, blk + (pid % 8) * 2048 + so, chunk);
        done += chunk;
    }
    return done;
}
}
// Chamado a cada 300 quadros; sem leituras de timer quando o perfil esta desligado.
extern "C" void d2_lpk_report(void)
{
#if D2_LPK_PROFILE
    debugf("[lpk] blocks=%u cache_bytes=%u hit=%u miss=%u evict=%u decode=%u raw=%u io_ticks=%u decode_ticks=%u in=%u out=%u errors=%u (janela de 300 quadros; hit/miss por consulta de setor)\n",
        (unsigned)D2_LPK_CACHE_BLOCKS, (unsigned)sizeof(lpkCache), lpkStats.hits,
        lpkStats.misses, lpkStats.evictions, lpkStats.decoded, lpkStats.raw,
        lpkStats.ioTicks, lpkStats.decodeTicks, lpkStats.inputBytes, lpkStats.outputBytes, lpkStats.errors);
    memset(&lpkStats, 0, sizeof(lpkStats));
#endif
}
// arquivos de dados inteiros (DATA/GFX/REPLAYS) vindos do pacote: devolve bytes lidos, ou -1 se o arquivo nao esta no pacote
extern "C" int d2_pack_load(const char *path, char *dst, int offset, int size)
{
    const LpkFile *f = lpk_find(path);
    if (!f) return -1;
    if (size < 0 || size > (long)f->size - offset) size = (int)((long)f->size - offset);
    if (size <= 0) return 0;
    return (int)lpk_read(f, offset, dst, size);
}
extern "C" int d2_pack_exists(const char *path) { return lpk_find(path) != NULL; }
extern "C" int d2_lev_header(const char *path, void *dst, int off, int n)
{
    const LpkFile *f = lpk_find(path);
    debugf("[lev] %s %s\n", path, f ? "no pacote" : "NAO ACHADO");
    return f && lpk_read(f, off, dst, n) == n;
}
int d2_read_sectors(char *dest, int sector, int nsectors)
{
    static FILE *fp;
    static char opened[64];
    static long fileSize;
#ifdef D2_DIAG_FS
    memcpy(readTrace.asset, g_CurrentLevelFileName, sizeof(readTrace.asset) - 1);
    readTrace.destination = (uintptr_t)dest;
    readTrace.sector = sector;
    readTrace.count = nsectors;
    readTrace.actual = -1;
    readTrace.fileSize = -1;
#endif
    if (!dest || sector < 0 || nsectors <= 0 || sector > LONG_MAX / 2048 ||
        nsectors > LONG_MAX / 2048 || g_CurrentLevelFileName[0] == 0)
        return 0;
    {
        const LpkFile *lf = lpk_find(g_CurrentLevelFileName);
        if (lf)
        {
            const long offset = (long)sector * 2048;
            if (offset >= (long)lf->nsec * 2048) return 0;
            int want = nsectors; if (want > (int)lf->nsec - sector) want = (int)lf->nsec - sector;
            const long got = lpk_read(lf, offset, dest, (long)want * 2048);
            return (int)(got / 2048);
        }
    }
    if (!fp || strcmp(opened, g_CurrentLevelFileName) != 0)
    {
        char name[64];
        if (fp) { fclose(fp); fp = NULL; }
        strncpy(name, g_CurrentLevelFileName, sizeof name); name[63] = 0;
        for (char *c = name; *c; c++) if (*c == '\\') *c = '/';
        fp = d2_fopen(name, "rb");
        if (!fp) { opened[0] = 0; return 0; }
        if (fseek(fp, 0, SEEK_END) != 0 || (fileSize = ftell(fp)) < 0) {
            fclose(fp);
            fp = NULL;
            opened[0] = 0;
            return 0;
        }
        strncpy(opened, g_CurrentLevelFileName, sizeof opened); opened[63] = 0;
    }
    const long offset = (long)sector * 2048;
#ifdef D2_DIAG_FS
    readTrace.fileSize = fileSize;
#endif
    if (offset > fileSize || (long)nsectors * 2048 > fileSize - offset ||
        fseek(fp, offset, SEEK_SET) != 0)
        return 0;

    const int actual = (int)fread(dest, 2048, nsectors, fp);
#ifdef D2_DIAG_FS
    readTrace.actual = actual;
#endif
    return actual;
}
