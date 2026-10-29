#!/usr/bin/env python3
"""Gera port/n64/gen_stubs.c: uma funcao 'fraca' (weak) para cada simbolo que o jogo pede e que a camada N64 ainda nao implementa.
Quando um stub e chamado, d2_stub_hit() mostra o nome na tela do N64 - assim sabemos exatamente o que implementar a seguir.
Uso: python3 tools/gen_stubs.py   (le tools/needed_syms.txt, escreve port/n64/gen_stubs.c)
Implementacoes reais (fortes) em outros arquivos substituem o stub automaticamente; nao precisa editar este arquivo."""
import re, sys, os
here = os.path.dirname(os.path.abspath(__file__))
names = [l.strip() for l in open(os.path.join(here, "needed_syms.txt")) if l.strip()]
# ja fornecidos pela libc/libdragon ou pelo compilador
SKIP = set("""exit fclose fopen fprintf fread free fseek ftell fwrite malloc memcpy memmove memset rand sprintf sqrtf srand stderr
strcat strchr strcmp strcpy strlen strncpy hypot _GLOBAL_OFFSET_TABLE_ _Unwind_Resume __cxa_guard_abort __cxa_guard_acquire
__cxa_guard_release __gxx_personality_v0 __stack_chk_fail""".split())
# variaveis (nao funcoes): nome -> bytes
DATA = {"_scratchData": 8, "g_FP_SXYZ0": 16, "g_FP_SXYZ1": 16, "g_FP_SXYZ2": 16,
        "g_cfg_bilinearFiltering": 4, "g_cfg_gameOnTextInput": 4, "rcossin_tbl": 16384}
def pretty(n):
    m = re.match(r"_Z(\d+)(.*)", n)
    if m:
        k = int(m.group(1)); return m.group(2)[:k]
    return n
out = ['/* GERADO por tools/gen_stubs.py - nao edite. */', '#include <stddef.h>', 'extern long d2_stub_hit(const char *name);', '']
i = 0
for n in names:
    if n in SKIP: continue
    if n in DATA:
        out.append('char %s[%d] __attribute__((weak, aligned(16)));' % (n, DATA[n])); continue
    out.append('long d2s_%d(void) __asm__("%s");' % (i, n))
    out.append('__attribute__((weak)) long d2s_%d(void) { return d2_stub_hit("%s"); }' % (i, pretty(n)))
    i += 1
open(os.path.join(here, "..", "port", "n64", "gen_stubs.c"), "w").write("\n".join(out) + "\n")
print("stubs de funcao: %d, variaveis: %d" % (i, len([n for n in names if n in DATA])))
