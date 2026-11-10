"""Paletas (CLUT) viram handles nativos: getClut(x, y) devolve o handle de um slot do pool de paletas (port/n64/d2_vram.h), nao mais uma
coordenada da VRAM, e o motor converte handle -> posicao com d2_clut_pos. Os uploads de paletas passam por d2_LoadColor (pool), os de indices
(paginas de 4/8 bits) continuam por LoadImage."""

def register(P, R):
    R('Game/C/texture.c', r'LoadImage\(&tmpclut,', 'd2_LoadColor(&tmpclut,')
    R('Game/C/texture.c', r'LoadImage\(cluts, \(u_long\*\)tpageaddress\);', 'd2_LoadColor(cluts, (u_long*)tpageaddress);')
    R('Game/C/cars.c', r'LoadImage\(&clutpos, ', 'd2_LoadColor(&clutpos, ')
    R('Game/C/spool.c', r'LoadImage\(&cluts, ', 'd2_LoadColor(&cluts, ')
    R('Game/C/spool.c', r'LoadImage2\(&specCluts, ', 'd2_LoadColor(&specCluts, ')
    R('Game/C/overmap.c', r'LoadImage\(&mapclutpos, ', 'd2_LoadColor(&mapclutpos, ')
    R('Game/C/pres.c', r'LoadImage\(&fontclutpos, ', 'd2_LoadColor(&fontclutpos, ')
    # LoadClut / LoadClut2 (carregamentos de tela, fontes): tambem sao paletas
    R('PsyCross/src/psx/LIBGPU.C', r'LoadImage\(&rect, clut\);', 'd2_LoadColor(&rect, clut);')
    R('PsyCross/src/psx/LIBGPU.C', r'LoadImage\(&drawArea, clut\);', 'd2_LoadColor(&drawArea, clut);')

    # getClut / GetClut devolvem o handle do pool; quem precisava da coordenada pede a posicao do handle
    R('PsyCross/include/psx/libgpu.h', r'#define getClut\(x, y\) \\\n\t\(\(\(y\)<<6\)\|\(\(\(x\)>>4\)&0x3f\)\)', '#define getClut(x, y) d2_clut_h((x), (y))')
    P('PsyCross/include/psx/libgpu.h', 'extern u_short GetClut(int x, int y);\n',
      'extern u_short GetClut(int x, int y);\nextern u_short d2_clut_h(int x, int y);\nextern void d2_clut_pos(unsigned int h, int *x, int *y);\n')
    P('Game/C/objanim.c', '\t\t\tcyc->vx = vram.x = (temp & 63) << 4;\n\t\t\tcyc->vy = vram.y = (temp >> 6);\n',
      '\t\t\t{ int cx_, cy_; d2_clut_pos(temp, &cx_, &cy_); cyc->vx = vram.x = cx_; cyc->vy = vram.y = cy_; }\n')
    P('Game/C/texture.c', '\t\t\ttmpclut.x = (clutN & 0x3f) << 4;\n\t\t\ttmpclut.y = (clutN >> 6);\n',
      '\t\t\t{ int cx_, cy_; d2_clut_pos(clutN, &cx_, &cy_); tmpclut.x = cx_; tmpclut.y = cy_; }\n')
    P('Game/C/pres.c', '\tx = (clutID & 63) * 16;\n\ty = (clutID >> 6);\n', '\td2_clut_pos(clutID, &x, &y);\n')
