// Ponto de entrada N64 do Driver 2 (REDRIVER2 + camada de plataforma N64).
// E1: sobe o hardware, monta o sistema de arquivos da ROM e entra no jogo (redriver2_main).
#include <libdragon.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

int d2_run_game(void);
void d2_log_push(const char *s);
void d2_sys_present(const char *who);
void d2_sys_init(void);                      // port/n64/d2_entry.cpp

#ifndef N64_OVERSCAN_X
#define N64_OVERSCAN_X 36
#endif
#ifndef N64_OVERSCAN_Y
#define N64_OVERSCAN_Y 28
#endif

// Fracas: se a libc do libdragon nao tiver, o jogo ainda linka.
__attribute__((weak)) char *getenv(const char *n) { (void)n; return 0; }
__attribute__((weak)) int mkdir(const char *p, unsigned m) { (void)p; (void)m; return -1; }

double d2_now_ms(void) { return (double)timer_ticks() * 1000.0 / (double)TICKS_PER_SECOND; }

// Primeiro olhar numa excecao: guarda registradores e enderecos de codigo da pilha em d2_crash[] (o harness le essa area da RAM; a tela do inspector
// e o ISViewer nao sao confiaveis numa excecao).
extern void __inspector_exception(exception_t *exc);
volatile uint32_t d2_crash[160] __attribute__((aligned(16)));
static void d2_exc_handler(exception_t *exc)
{
    if (!d2_crash[0])
    {
        uint32_t sp = (uint32_t)exc->regs->gpr[29];
        d2_crash[1] = (uint32_t)exc->regs->epc; d2_crash[2] = (uint32_t)C0_BADVADDR(); d2_crash[3] = (uint32_t)exc->code;
        for (int i = 0; i < 32; i++) d2_crash[4 + i] = (uint32_t)exc->regs->gpr[i];
        d2_crash[36] = (uint32_t)exc->regs->cr;
        uint32_t n = 0;
        if (sp >= 0x80000000u && sp < 0x80800000u)
            for (uint32_t a = sp; a < sp + 0x800 && a < 0x80800000u && n < 100; a += 4)
            {
                uint32_t w = *(volatile uint32_t *)a;
                if (w >= 0x80000400u && w < 0x800f0000u) d2_crash[40 + n++] = w;
            }
        d2_crash[0] = 0xC0DE0000u | n;
    }
    __inspector_exception(exc);
}

int main(void)
{
    register_exception_handler(d2_exc_handler);
    debug_init_isviewer();
    debug_init_usblog();
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    /* sem console_init(): ele recria o display em 640x240 (614 KB transitorios - estoura a RAM) e a imagem do jogo ficava cortada na metade
       esquerda. O printf/debugf de depuracao vai pelo ISViewer/USB. Use -DD2_FB640 para o console em 640x240. */
    /* margem de seguranca (overscan) - DEPOIS do console_init(); valores em pontos do VI (base 640x480). */
    vi_set_borders((vi_borders_t){ N64_OVERSCAN_X, N64_OVERSCAN_X, N64_OVERSCAN_Y, N64_OVERSCAN_Y });
    timer_init();
    joypad_init();
    d2_sys_init();

    d2_log_push("DRIVER 2 N64 - E2\n");
    const int dfs = dfs_init(DFS_DEFAULT_LOCATION);
    char b[64]; snprintf(b, sizeof b, "dfs_init = %d  RAM %d KB\n", dfs, (int)(get_memory_size() / 1024)); d2_log_push(b);
    d2_sys_present(0);

    const int rc = d2_run_game();
    snprintf(b, sizeof b, "Jogo terminou: codigo %d\n", rc); d2_log_push(b);
    for (;;) { d2_sys_present(0); }
}
