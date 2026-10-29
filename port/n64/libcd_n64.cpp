// Substitui PsyCross/src/psx/LIBCD.C. No Nintendo 64 nao existe CD: o jogo (modo "PC filesystem") le tudo por fopen()
// de arquivos que estao no cartucho (DragonFS, ver d2_fopen em sys_n64.c). Aqui o CD e so um "disco vazio":
// CdSearchFile nao acha nada (o jogo cai no caminho de arquivos) e os comandos viram no-ops.
#include "psx/types.h"
#include "psx/libcd.h"
#include <string.h>

#define D2_BCD_ENC(x) ((((x) / 10) << 4) | ((x) % 10))
#define D2_BCD_DEC(x) ((((x) >> 4) * 10) + ((x) & 0xF))

static CdlCB   s_readyCb;
static void  (*s_dataCb)(void);
static int     s_lastCom;

extern "C" {
int  CdInit(void) { return 1; }
int  CdReset(int mode) { return 1; }
int  CdStatus(void) { return 0; }
int  CdLastCom(void) { return s_lastCom; }
int  CdMode(void) { return 0; }
int  CdDiskReady(int mode) { return CdlComplete; }
int  CdGetDiskType(void) { return 0; }
void CdFlush(void) {}
int  CdSetDebug(int level) { return 0; }
CdlFILE *CdSearchFile(CdlFILE *fp, char *name) { if (fp) memset(fp, 0, sizeof(*fp)); return 0; }
int  CdControl(u_char com, u_char *param, u_char *result) { s_lastCom = com; return 0; }
int  CdControlB(u_char com, u_char *param, u_char *result) { s_lastCom = com; return 1; }
int  CdControlF(u_char com, u_char *param) { s_lastCom = com; return 0; }
int  CdSync(int mode, u_char *result) { return CdlComplete; }
int  CdReady(int mode, u_char *result) { return CdlComplete; }
int  CdRead(int sectors, u_long *buf, int mode) { return 0; }
int  CdReadSync(int mode, u_char *result) { return 0; }
int  CdReadFile(char *file, u_long *addr, int nbyte) { return -1; }
int  CdGetSector(void *madr, int size) { return 0; }
int  CdDataSync(int mode) { return 0; }
CdlLOC *CdIntToPos(int i, CdlLOC *p)
{
    i += 150;
    p->sector = D2_BCD_ENC(i % 75);
    int rem = i / 75;
    p->second = D2_BCD_ENC(rem % 60);
    p->minute = D2_BCD_ENC(rem / 60);
    p->track = 0;
    return p;
}
int  CdPosToInt(CdlLOC *p)
{
    return (75 * (60 * D2_BCD_DEC(p->minute) + D2_BCD_DEC(p->second))) + D2_BCD_DEC(p->sector) - 150;
}
CdlCB CdReadyCallback(CdlCB func) { CdlCB o = s_readyCb; s_readyCb = func; return o; }
CdlCB CdReadCallback(CdlCB func) { return 0; }
CdlCB CdSyncCallback(CdlCB func) { return 0; }
void *CdDataCallback(void (*func)()) { void *o = (void *)s_dataCb; s_dataCb = func; return o; }
void PsyX_CDFS_Init(const char *imageFileName, int track, int sectorSize) {}
void PsyX_CDFS_Init_Mem(const u_int *data, int size, int track, int sectorSize) {}
int  PsyX_IsCDImageInit(void) { return 0; }
}
