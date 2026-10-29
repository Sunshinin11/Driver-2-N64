// Configuracao e log da camada PsyX (as variaveis g_cfg_* e PsyX_Log_*) para o N64. Nao ha janela/teclado: so o essencial.
#include <libdragon.h>
#include <stdarg.h>
#include <stdio.h>

int   g_cfg_pgxpTextureCorrection = 0;
int   g_cfg_pgxpZBuffer = 0;
int   g_cfg_bilinearFiltering = 0;
int   g_cfg_swapInterval = 1;
void *g_cfg_gameOnTextInput = 0;

void d2_log_push(const char *s);
static void d2_log(const char *tag, const char *fmt, va_list ap)
{
    char buf[256];
    vsnprintf(buf, sizeof buf, fmt, ap);
    debugf("[%s] %s", tag, buf);
    char line[300]; snprintf(line, sizeof line, "[%s] %s", tag, buf); d2_log_push(line);
}
void PsyX_Log_Info(const char *fmt, ...)    { va_list ap; va_start(ap, fmt); d2_log("I", fmt, ap); va_end(ap); }
void PsyX_Log_Warning(const char *fmt, ...) { va_list ap; va_start(ap, fmt); d2_log("W", fmt, ap); va_end(ap); }
void PsyX_Log_Error(const char *fmt, ...)   { va_list ap; va_start(ap, fmt); d2_log("E", fmt, ap); va_end(ap); }
void PsyX_Log(const char *fmt, ...)         { va_list ap; va_start(ap, fmt); d2_log("L", fmt, ap); va_end(ap); }
void PsyX_Log_Success(const char *fmt, ...) { va_list ap; va_start(ap, fmt); d2_log("S", fmt, ap); va_end(ap); }
