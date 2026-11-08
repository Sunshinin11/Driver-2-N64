"""Etapa 3/7 (fechamento): o conversor de pacotes PS1 -> vertices do PsyCross (PsyX_GPU.cpp, lotes/splits, GR_DrawTriangles) saiu do
executavel. O desenho e feito so por d2_native_draw_ot / d2_native_draw_prim (port/n64/psyx_rdp.cpp). Aqui some a ponta que ainda o chamava."""

def register(P, R):
    # funcoes de compatibilidade do PsyCross que passavam pelo conversor de pacotes
    for fn in ('ClearImage', 'ClearImage2', 'DrawSync', 'LoadImage', 'LoadImage2', 'MoveImage', 'DrawOTagEnv', 'DrawOTag', 'DrawPrim'):
        R('PsyCross/src/psx/LIBGPU.C', r'(?ms)^(?:int|void) ' + fn + r'\([^)]*\)\r?\n\{.*?\r?\n\}\r?\n', '/*d2gpu: ' + fn + ' removido*/\n')
    R('PsyCross/src/psx/LIBGPU.C', r'(?m)^(\t+)ClearSplits\(\);', r'\1/*d2gpu*/')


# os renomes antigos (X -> PsyX_compat_X) deixam de existir: as funcoes originais sao removidas acima
COMPAT = ('ClearImage', 'ClearImage2', 'DrawSync', 'LoadImage', 'LoadImage2', 'MoveImage', 'DrawOTagEnv', 'DrawOTag', 'DrawPrim')

def is_old_rename(x):
    return x[0].endswith('LIBGPU.C') and any(x[2].lstrip().split('(')[0].endswith('PsyX_compat_' + f) for f in COMPAT)
