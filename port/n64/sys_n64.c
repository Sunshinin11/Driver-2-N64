// Ponte "pura libdragon" (sem cabecalhos do PS1, para nao haver conflito de nomes): tempo, vblank e controle.
#include <libdragon.h>
#include <stdint.h>

static volatile int vbl_count;
static void (*vbl_cb)(void);

static volatile int last_present_vbl;
static void d2_hang_report(void);
extern void d2_audio_pump(void);

static void vi_handler(void)
{
    vbl_count++;
    if (vbl_cb) vbl_cb();
    d2_hang_report();
}

void d2_sys_init(void) { register_VI_handler(vi_handler); }
int  d2_sys_vbl_count(void) { return vbl_count; }
void d2_sys_set_vbl_cb(void (*f)(void)) { vbl_cb = f; }
double d2_sys_now_ms(void) { return (double)timer_ticks() * 1000.0 / (double)TICKS_PER_SECOND; }

// Espera ate o contador de vblank chegar em 'target' (com saida de seguranca se o VI parar: 100 ms por quadro).
void d2_sys_wait_vbl_until(int target)
{
    double t0 = d2_sys_now_ms();
    while ((vbl_count - target) < 0)
    {
        d2_audio_pump();
        if (d2_sys_now_ms() - t0 > 100.0 * 4) break;
    }
}

typedef struct { uint16_t btn; int8_t sx, sy; } d2_pad_raw_t;
enum { D2_A = 1, D2_B = 2, D2_Z = 4, D2_START = 8, D2_DU = 16, D2_DD = 32, D2_DL = 64, D2_DR = 128,
       D2_L = 256, D2_R = 512, D2_CU = 1024, D2_CD = 2048, D2_CL = 4096, D2_CR = 8192 };

int d2_pad_poll(int port, d2_pad_raw_t *o)
{
    static int inited;
    if (!inited) { joypad_init(); inited = 1; }
    joypad_poll();
    joypad_port_t p = (port == 0) ? JOYPAD_PORT_1 : JOYPAD_PORT_2;
    uint16_t b = 0;
    int8_t sx = 0, sy = 0;
    if (joypad_is_connected(p)) {
        joypad_inputs_t in = joypad_get_inputs(p);
        if (in.btn.a)       b |= D2_A;
        if (in.btn.b)       b |= D2_B;
        if (in.btn.z)       b |= D2_Z;
        if (in.btn.start)   b |= D2_START;
        if (in.btn.d_up)    b |= D2_DU;
        if (in.btn.d_down)  b |= D2_DD;
        if (in.btn.d_left)  b |= D2_DL;
        if (in.btn.d_right) b |= D2_DR;
        if (in.btn.l)       b |= D2_L;
        if (in.btn.r)       b |= D2_R;
        if (in.btn.c_up)    b |= D2_CU;
        if (in.btn.c_down)  b |= D2_CD;
        if (in.btn.c_left)  b |= D2_CL;
        if (in.btn.c_right) b |= D2_CR;
        sx = in.stick_x; sy = in.stick_y;
    }
#ifdef D2_BENCH
    // Roteiro automatico deterministico (in_bench.txt) quando nenhum botao fisico e pressionado:
    if (port == 0 && b == 0) {
        int v = vbl_count;
        if (v >= 760 && v < 766) b |= D2_DD;
        else if (v >= 790 && v < 796) b |= D2_A;
        else if (v >= 860 && v < 866) b |= D2_A;
        else if (v >= 930 && v < 936) b |= D2_A;
        else if (v >= 1000 && v < 1006) b |= D2_A;
        else if (v >= 2200) b |= D2_A; // acelera o carro em Chicago apos 10s de cena parada
        if (b && (v % 60 == 0 || (v >= 760 && v <= 1010))) debugf("[bench] vbl=%d b=%04x\n", v, b);
    }
#endif
    o->btn = b; o->sx = sx; o->sy = sy;
    return 1;
}

// ---- Arquivos do jogo: o jogo faz fopen("DRIVER2\\DATA\\...") como no PC; aqui viram "rom:/DRIVER2/DATA/..." (DragonFS no cartucho). ----
#include <stdio.h>
#include <string.h>
FILE *d2_fopen(const char *path, const char *mode)
{
    char buf[256];
    size_t n = 0;
    if (!path || !mode) return NULL;
    if (mode[0] != 'r' || strchr(mode, '+')) return NULL; // cartucho e somente leitura (saves: etapa futura)
    if (strncmp(path, "rom:", 4) == 0) return fopen(path, mode);
    memcpy(buf, "rom:/", 5); n = 5;
    while (path[0] == '.' && (path[1] == '/' || path[1] == '\\')) path += 2;
    while (*path == '/' || *path == '\\') path++;
    while (*path && n < sizeof(buf) - 1) { char c = *path++; buf[n++] = (c == '\\') ? '/' : c; }
    buf[n] = 0;
    return fopen(buf, mode);
}

// ---- Diagnostico (ate o renderizador E4 existir): log proprio desenhado na tela (nao usa o console do libdragon) ----
#include <stdio.h>
#include <string.h>
#define D2_LOG_LINES 24
#define D2_LOG_COLS  36
static char log_lines[D2_LOG_LINES][D2_LOG_COLS + 1];
static int log_count;
static char log_cur[D2_LOG_COLS + 1];
static int log_cur_len;
static void log_commit(void)
{
    log_cur[log_cur_len] = 0;
    memcpy(log_lines[log_count % D2_LOG_LINES], log_cur, D2_LOG_COLS + 1);
    log_count++; log_cur_len = 0;
}
void d2_log_push(const char *s)
{
    for (; *s; s++)
    {
        if (*s == '\n') { log_commit(); continue; }
        char c = (*s < 32 || *s > 126) ? '?' : *s;
        if (log_cur_len < D2_LOG_COLS) log_cur[log_cur_len++] = c;
    }
}
extern int d2_rdp_scene_active(void);
extern int d2_rdp_frame_count(void);
extern void d2_rdp_abort_scene(void);
int d2_present_force;                      /* 1 = mostrar o log mesmo com o jogo desenhando (STUB, excecao) */
static int last_frames_seen, last_game_frame_vbl;
/* o jogo apresentou um quadro de verdade (PsyX_EndScene) */
void d2_sys_note_frame(void) { last_present_vbl = vbl_count; }
void d2_sys_present(const char *who)
{
    static int last_vbl = -1, beats;
    if (last_vbl == vbl_count) return;
    last_vbl = vbl_count;
    last_present_vbl = vbl_count;
    int fc = d2_rdp_frame_count();
    if (fc != last_frames_seen) { last_frames_seen = fc; last_game_frame_vbl = vbl_count; }
    if (!d2_present_force)
    {
        if (d2_rdp_scene_active()) return;                          /* quadro do jogo em andamento */
        if (fc > 0) return;                                          /* o jogo ja apresenta quadros proprios: nao cobrir */
    }
    else d2_rdp_abort_scene();
    if (who && (++beats % 120) == 0) { char b[32]; snprintf(b, sizeof b, "[%s] %d\n", who, beats); d2_log_push(b); }
    surface_t *d = display_get();
    graphics_fill_screen(d, 0);
    graphics_set_color(graphics_make_color(255, 255, 255, 255), 0);
    int first = log_count > D2_LOG_LINES ? log_count - D2_LOG_LINES : 0, y = 16;
    for (int n = first; n < log_count; n++, y += 8) graphics_draw_text(d, 24, y, log_lines[n % D2_LOG_LINES]);
    if (log_cur_len) { log_cur[log_cur_len] = 0; graphics_draw_text(d, 24, y, log_cur); }
    display_show(d);
}

// ---- Vigia de travamento: se o jogo ficar >2 s sem apresentar quadro, desenha (direto no quadro exibido) o PC onde ele esta preso. ----
static void d2_hang_report(void)
{
    if (vbl_count - last_present_vbl < 120 || (vbl_count % 30) != 0) return;
    uint32_t epc = (uint32_t)C0_EPC();
    uint32_t org = (*(volatile uint32_t *)0xA4400004) & 0x00FFFFFF;
    surface_t fb = surface_make((void *)(0xA0000000 | org), FMT_RGBA16, 320, 240, 640);
    char buf[48];
    snprintf(buf, sizeof buf, "TRAVADO? EPC=%08lX t=%d", (unsigned long)epc, vbl_count - last_present_vbl);
    graphics_set_color(graphics_make_color(255, 255, 0, 255), graphics_make_color(0, 0, 0, 255));
    graphics_draw_box(&fb, 20, 20, 280, 12, graphics_make_color(0, 0, 0, 255));
    graphics_draw_text(&fb, 24, 22, buf);
    debugf("%s\n", buf);
}
