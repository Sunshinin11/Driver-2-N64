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
