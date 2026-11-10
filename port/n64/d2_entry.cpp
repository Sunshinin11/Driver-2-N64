// Ponte C -> C++ e preparacao do jogo (equivale ao main() de redriver2_psxpc.cpp, sem SDL/ini/janela).
// O jogo (Game/C/*.c) e compilado como C++, entao redriver2_main tem nome "mangled".
#include "driver2.h"
#include "C/system.h"

int redriver2_main(int argc, char **argv);

// Buffers fixos do jogo.
// No PS1 original e no REDRIVER2, estes buffers eram sobrepostos (overlays):
// _frontend_buffer compartilhava o mesmo endereco de _other_buffer (_otag1_org).
// _sbank_buffer era usado apenas durante o carregamento de som para transferir
// amostras ao SPU e residia dentro de mallocTab.
// O som nativo so le a tabela de amostras de cada lump (g_SBank_buffer de 4 KB, sem o corpo ADPCM do PS1).
static char g_Overlay_buffer[0x50000];
static char g_SBank_buffer[0x1000];
static char g_Other_buffer[0x50000];
static char g_Other_buffer2[0x50000];
static OTTYPE g_OT1[OTSIZE];
static OTTYPE g_OT2[OTSIZE];
static char g_PrimTab1[PRIMTAB_SIZE];
static char g_PrimTab2[PRIMTAB_SIZE];
// No mapa de memoria original do PS1 o replay ocupa somente o fim da RAM
// (0x1FABBC..0x200000). Mantemos uma pequena folga e devolvemos o excesso ao
// mixer/heap do N64.
static char g_Replay_buffer[0x10000];

extern int gDrawDistance;
extern int gUserLanguage;
extern int gNoFMV;

#ifndef D2_DRAW_DISTANCE
#define D2_DRAW_DISTANCE 441
#endif

// No PC troca o mapeamento de teclas/controle entre menu e jogo; no N64 o mapeamento do controle e proprio (psyx_backend.cpp).
void SwitchMappings(int menu) { (void)menu; }

extern "C" int d2_run_game(void)
{
    static char arg0[] = "driver2";
    static char *argv[] = { arg0, 0 };

    _overlay_buffer = g_Overlay_buffer;
    _frontend_buffer = g_Other_buffer;  // overlay legitimo do PS1 (_otag1_org)
    _other_buffer = g_Other_buffer;
    _other_buffer2 = g_Other_buffer2;
    _OT1 = g_OT1;
    _OT2 = g_OT2;
    _primTab1 = g_PrimTab1;
    _primTab2 = g_PrimTab2;
    _sbank_buffer = g_SBank_buffer;      // buffer proprio: som carrega junto com a tela de loading
    _replay_buffer = g_Replay_buffer;

    gDrawDistance = D2_DRAW_DISTANCE;
    gNoFMV = 0;            // FMV_main pula os renders ausentes no cartucho
    gUserLanguage = 0;     // ingles (traducao PT-BR fica para o fim)
    return redriver2_main(1, argv);
}
