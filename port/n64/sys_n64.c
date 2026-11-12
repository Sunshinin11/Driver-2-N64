// Ponte "pura libdragon" (sem cabecalhos do PS1, para nao haver conflito de nomes): tempo, vblank e controle.
#include <libdragon.h>
#include <stdint.h>

static volatile int vbl_count;
static void (*vbl_cb)(void);
static int last_cb_vbl;

static volatile int last_present_vbl;
static void d2_hang_report(void);
extern void d2_audio_pump(void);

static void vi_handler(void)
{
    vbl_count++;
    d2_hang_report();
}

void d2_sys_init(void) { register_VI_handler(vi_handler); }
int  d2_sys_vbl_count(void) { return vbl_count; }
void d2_sys_set_vbl_cb(void (*f)(void)) { vbl_cb = f; last_cb_vbl = vbl_count; }
void d2_sys_pump_vbl_cb(void)
{
    int cur = vbl_count;
    if (!vbl_cb) { last_cb_vbl = cur; return; }
    int pending = cur - last_cb_vbl;
    if (pending <= 0) return;
    if (pending > 8) { last_cb_vbl = cur - 1; pending = 1; }
    while (pending-- > 0) {
        last_cb_vbl++;
        vbl_cb();
    }
}
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

// Mapeia o controle diretamente para as acoes do jogo.
#include <sys/types.h>
typedef struct DUPLICATION DUPLICATION;
typedef struct MAPPING MAPPING;
typedef struct PAD PAD;
#include "../redriver2/src_rebuild/Game/C/pad.h"

void d2_read_buttons(uint16_t buttons[2])
{
    joypad_poll();
    for (int i = 0; i < 2; i++) {
        joypad_port_t port = i == 0 ? JOYPAD_PORT_1 : JOYPAD_PORT_2;
        buttons[i] = 0;
        if (!joypad_is_connected(port))
            continue;

        joypad_inputs_t in = joypad_get_inputs(port);
        if (in.btn.a)                           buttons[i] |= MPAD_CROSS;
        if (in.btn.b)                           buttons[i] |= MPAD_SQUARE;
        if (in.btn.z)                           buttons[i] |= MPAD_L2;
        if (in.btn.start)                       buttons[i] |= MPAD_START;
        if (in.btn.d_up || in.stick_y > 40)     buttons[i] |= MPAD_D_UP;
        if (in.btn.d_down || in.stick_y < -40)  buttons[i] |= MPAD_D_DOWN;
        if (in.btn.d_left || in.stick_x < -40)  buttons[i] |= MPAD_D_LEFT;
        if (in.btn.d_right || in.stick_x > 40)  buttons[i] |= MPAD_D_RIGHT;
        if (in.btn.l)                           buttons[i] |= MPAD_L1;
        if (in.btn.r)                           buttons[i] |= MPAD_R1;
        if (in.btn.c_up)                        buttons[i] |= MPAD_TRIANGLE;
        if (in.btn.c_down)                      buttons[i] |= MPAD_CIRCLE;
        if (in.btn.c_left)                      buttons[i] |= MPAD_R2;
        if (in.btn.c_right)                     buttons[i] |= MPAD_SELECT;
    }
}


// ---- Saves (configuracao e progresso): SRAM do cartucho (256 kbit). Duas fichas de 8 KB: 0 = config, 1 = progresso. ----
// Cada ficha: { magic, tamanho, soma } + dados. A leitura confere magic e soma; ficha invalida = "sem dados salvos".
#define D2_SRAM_BASE   0x08000000u
#define D2_SAVE_SLOT   8192
#define D2_SAVE_MAGIC  0x44325356u   /* 'D2SV' */
typedef struct { uint32_t magic, size, sum, pad; } d2_save_hdr_t;
static uint8_t d2_save_buf[D2_SAVE_SLOT] __attribute__((aligned(64)));
static int d2_save_slot_of(const char *name) { return name[0] == 'c' ? 0 : 1; }
static uint32_t d2_save_sum(const uint8_t *p, uint32_t n) { uint32_t a = 0x811c9dc5u; while (n--) { a ^= *p++; a *= 16777619u; } return a; }
static void d2_sram_setup(void)
{
    static int done;
    if (done) return;
    done = 1;
    *(volatile uint32_t *)0xA4600024 = 0x05;   /* PI_BSD_DOM2_LAT */
    *(volatile uint32_t *)0xA4600028 = 0x0C;   /* PWD */
    *(volatile uint32_t *)0xA460002C = 0x0D;   /* PGS */
    *(volatile uint32_t *)0xA4600030 = 0x02;   /* RLS */
}
static void d2_sram_read(uint32_t off, void *dst, uint32_t n)
{
    d2_sram_setup();
    data_cache_hit_writeback_invalidate(dst, n);
    dma_read_raw_async(dst, D2_SRAM_BASE + off, n);
    dma_wait();
    data_cache_hit_invalidate(dst, n);
}
static void d2_sram_write(uint32_t off, const void *src, uint32_t n)
{
    d2_sram_setup();
    data_cache_hit_writeback((void *)src, n);
    dma_write_raw_async(src, D2_SRAM_BASE + off, n);
    dma_wait();
}
/* devolve o tamanho lido (>0) ou 0 se nao ha dados validos */
int d2_save_read(const char *name, char *dst, int max)
{
    const uint32_t off = (uint32_t)d2_save_slot_of(name) * D2_SAVE_SLOT;
    d2_sram_read(off, d2_save_buf, D2_SAVE_SLOT);
    const d2_save_hdr_t *h = (const d2_save_hdr_t *)d2_save_buf;
    if (h->magic != D2_SAVE_MAGIC || h->size == 0 || h->size > D2_SAVE_SLOT - sizeof(*h) || (int)h->size > max) return 0;
    if (d2_save_sum(d2_save_buf + sizeof(*h), h->size) != h->sum) return 0;
    memcpy(dst, d2_save_buf + sizeof(*h), h->size);
    return (int)h->size;
}
/* 1 = gravou */
int d2_save_write(const char *name, const char *src, int size)
{
    if (size <= 0 || size > (int)(D2_SAVE_SLOT - sizeof(d2_save_hdr_t))) return 0;
    d2_save_hdr_t *h = (d2_save_hdr_t *)d2_save_buf;
    memset(d2_save_buf, 0, D2_SAVE_SLOT);
    memcpy(d2_save_buf + sizeof(*h), src, (size_t)size);
    h->magic = D2_SAVE_MAGIC; h->size = (uint32_t)size; h->sum = d2_save_sum(d2_save_buf + sizeof(*h), (uint32_t)size);
    d2_sram_write((uint32_t)d2_save_slot_of(name) * D2_SAVE_SLOT, d2_save_buf, D2_SAVE_SLOT);
    d2_sram_read((uint32_t)d2_save_slot_of(name) * D2_SAVE_SLOT, d2_save_buf, sizeof(*h));   /* confere que gravou */
    return h->magic == D2_SAVE_MAGIC;
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
