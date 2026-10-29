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
extern char g_CurrentLevelFileName[64];
extern FILE *d2_fopen(const char *path, const char *mode);
int d2_read_sectors(char *dest, int sector, int nsectors)
{
    static FILE *fp;
    static char opened[64];
    if (g_CurrentLevelFileName[0] == 0) return 0;
    if (!fp || strcmp(opened, g_CurrentLevelFileName) != 0)
    {
        char name[64];
        if (fp) { fclose(fp); fp = NULL; }
        strncpy(name, g_CurrentLevelFileName, sizeof name); name[63] = 0;
        for (char *c = name; *c; c++) if (*c == '\\') *c = '/';
        fp = d2_fopen(name, "rb");
        if (!fp) { opened[0] = 0; return 0; }
        strncpy(opened, g_CurrentLevelFileName, sizeof opened); opened[63] = 0;
    }
    fseek(fp, (long)sector * 2048, SEEK_SET);
    return (int)fread(dest, 2048, nsectors, fp);
}
