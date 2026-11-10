// APIs opcionais ainda sem implementacao no N64.
#include <stdint.h>
struct PGXPVData;

void GetXAData(int) {}
void PrintXASubtitles(int) {}

void DoFreeCamera() {}
void BuildFreeCameraMatrix() {}
void DrawDebugOverlays() {}

extern "C" {
unsigned short PGXP_EmitCacheData(PGXPVData *) { return 0xFFFF; }
void PGXP_SetZOffsetScale(float, float) {}
int PGXP_GetCacheData(PGXPVData *, unsigned, unsigned short) { return 0; }
unsigned short PGXP_GetIndex(int) { return 0xFFFF; }
}
