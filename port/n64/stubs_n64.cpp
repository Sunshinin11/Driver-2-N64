// Pontos do jogo que nao existem (ainda) no N64: XA/CDDA, camera livre e overlays de depuracao, FMV, PGXP.
#include <stdint.h>
struct RENDER_ARGS;
struct PGXPVData;

void PlayXA(int, int) {}
void StopXA() {}
void PauseXA() {}
void ResumeXA() {}
void PrepareXA() {}
void UnprepareXA() {}
int  XAPrepared() { return 0; }
void GetXAData(int) {}
void SetXAVolume(int) {}
void PrintXASubtitles(int) {}

void DoFreeCamera() {}
void BuildFreeCameraMatrix() {}
void DrawDebugOverlays() {}

int FMV_main(RENDER_ARGS *) { return 0; }

extern "C" {
unsigned short PGXP_EmitCacheData(PGXPVData *) { return 0xFFFF; }
void PGXP_SetZOffsetScale(float, float) {}
int PGXP_GetCacheData(PGXPVData *, unsigned, unsigned short) { return 0; }
unsigned short PGXP_GetIndex(int) { return 0xFFFF; }
}
