/* Stub minimo de SDL para o port N64: o jogo so usa SDL para caixas de mensagem (e codigo de depuracao do PC). */
#ifndef D2N64_SDL_STUB_H
#define D2N64_SDL_STUB_H
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SDL_MESSAGEBOX_ERROR 0x10
#define SDL_MESSAGEBOX_WARNING 0x20
#define SDL_MESSAGEBOX_INFORMATION 0x40
static inline int SDL_ShowSimpleMessageBox(unsigned flags, const char* title, const char* msg, void* win)
{ (void)flags; (void)win; fprintf(stderr, "[%s] %s\n", title, msg); return 0; }
#ifdef __cplusplus
}
#endif
#endif
