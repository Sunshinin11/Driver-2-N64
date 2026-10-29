// Ponte C -> C++ e preparacao do jogo (equivale ao main() de redriver2_psxpc.cpp, sem SDL/ini/janela).
// O jogo (Game/C/*.c) e compilado como C++, entao redriver2_main tem nome "mangled".
#include "driver2.h"
#include "C/system.h"

int redriver2_main(int argc, char **argv);

// Buffers fixos do jogo (mesmos tamanhos do PC com USE_CRT_MALLOC=0).
static char g_Overlay_buffer[0x50000];
static char g_Frontend_buffer[0x60000];
static char g_Other_buffer[0x50000];
static char g_Other_buffer2[0x50000];
static OTTYPE g_OT1[OTSIZE];
static OTTYPE g_OT2[OTSIZE];
static char g_PrimTab1[PRIMTAB_SIZE];
static char g_PrimTab2[PRIMTAB_SIZE];
static char g_SBank_buffer[0x50000];
static char g_Replay_buffer[0x50000];

extern int gDrawDistance;
extern int gUserLanguage;
extern int gNoFMV;

// No PC troca o mapeamento de teclas/controle entre menu e jogo; no N64 o mapeamento do controle e proprio (psyx_backend.cpp).
void SwitchMappings(int menu) { (void)menu; }

extern "C" int d2_run_game(void)
{
    static char arg0[] = "driver2";
    static char *argv[] = { arg0, 0 };

    _overlay_buffer = g_Overlay_buffer;
    _frontend_buffer = g_Frontend_buffer;
    _other_buffer = g_Other_buffer;
    _other_buffer2 = g_Other_buffer2;
    _OT1 = g_OT1;
    _OT2 = g_OT2;
    _primTab1 = g_PrimTab1;
    _primTab2 = g_PrimTab2;
    _sbank_buffer = g_SBank_buffer;
    _replay_buffer = g_Replay_buffer;

    gDrawDistance = 600;   // valor do PC
    gNoFMV = 1;            // videos (FMV) ficam para o fim do projeto: o jogo pula direto
    gUserLanguage = 0;     // ingles (traducao PT-BR fica para o fim)
    return redriver2_main(1, argv);
}
