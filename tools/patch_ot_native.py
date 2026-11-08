"""Etapa 3: o OT deixa de ser uma lista ligada de slots do PS1 (ClearOTagR ligando i -> i-1 com terminador) e passa a ser
um vetor nativo de baldes: cada slot guarda so a cabeca da sua lista de comandos (0 = vazio), ClearOTagR zera o vetor e
o desenho (d2_native_draw_ot) percorre os slots por indice, do mais alto ao mais baixo. Sem terminador, sem prim_terminator."""

def register(P, R):
    R('PsyCross/include/psx/libgpu.h',
      r'#define isendprim\(p\) \t\t\(\(\(\(P_TAG \*\)\(p\)\)->addr\) == \(uintptr_t\)&prim_terminator\)',
      '#define isendprim(p) \t\t((((P_TAG *)(p))->addr) == 0) /*d2ot*/')
    R('PsyCross/include/psx/libgpu.h',
      r'#define termPrim\(p\)\t\t\tsetaddr\(p, &prim_terminator\)',
      '#define termPrim(p)\t\t\tsetaddr(p, 0) /*d2ot*/')
    import re
    R('PsyCross/src/psx/LIBGPU.C',
      r'u_long\* ClearOTag\(u_long\* ot, int n\)\r?\n\{\r?\n\tOT_TAG\* ptag_list;.*?\r?\n\}\r?\n\r?\nu_long\* ClearOTagR\(u_long\* ot, int n\)\r?\n\{.*?\r?\n\}\r?\n',
      '''/*d2ot*/
#ifdef __cplusplus
extern "C"
#endif
void d2_ot_register(void* ot, int n);

/* OT nativo: n baldes de 8 bytes, cada um com a cabeca da lista de comandos do slot (0 = vazio) */
u_long* ClearOTag(u_long* ot, int n)
{
	if (n == 0)
		return NULL;
	d2_ot_register(ot, n);
	memset(ot, 0, (size_t)n * sizeof(OT_TAG));
	return NULL;
}

u_long* ClearOTagR(u_long* ot, int n)
{
	return ClearOTag(ot, n);
}
''', re.S)
    # o terminador do PS1 nao existe mais
    R('PsyCross/src/gpu/PsyX_GPU.cpp', r'OT_TAG prim_terminator = \{ -1, 0 \};[^\n]*\r?\n', '')
    R('PsyCross/src/gpu/PsyX_GPU.h', r'extern OT_TAG prim_terminator;[^\n]*\r?\n', '')
    R('PsyCross/include/psx/libgpu.h', r'extern OT_TAG prim_terminator;[^\n]*\r?\n', '')
