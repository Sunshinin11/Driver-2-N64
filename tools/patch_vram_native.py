"""Etapa 4 (fechamento): os uploads de CORES (paletas) passam por d2_LoadColor: a VRAM guarda direto o RGBA5551 do RDP, sem
converter de PS1 a cada decodificacao de textura. Uploads de indices (paginas de 4/8 bits) continuam por LoadImage."""

def register(P, R):
    R('Game/C/texture.c', r'LoadImage\(&tmpclut,', 'd2_LoadColor(&tmpclut,')
    R('Game/C/texture.c', r'LoadImage\(cluts, \(u_long\*\)tpageaddress\);', 'd2_LoadColor(cluts, (u_long*)tpageaddress);')
    R('Game/C/cars.c', r'LoadImage\(&clutpos, ', 'd2_LoadColor(&clutpos, ')
    R('Game/C/spool.c', r'LoadImage\(&cluts, ', 'd2_LoadColor(&cluts, ')
    R('Game/C/spool.c', r'LoadImage2\(&specCluts, ', 'd2_LoadColor(&specCluts, ')
    R('Game/C/overmap.c', r'LoadImage\(&mapclutpos, ', 'd2_LoadColor(&mapclutpos, ')
    R('Game/C/pres.c', r'LoadImage\(&fontclutpos, ', 'd2_LoadColor(&fontclutpos, ')
