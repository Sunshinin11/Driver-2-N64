import re
"""Remocao do GTE: cada trecho que usava registradores virtuais (SetRotMatrix/ldv0/rtv0tr/stlvnl...) passa a usar
funcoes de matematica nativa (port/compat/d2_xform.h), com a matriz passada por parametro.
Cada 'new' leva o marcador /*d2x*/ para o patch_engine reconhecer que ja foi aplicado."""

def register(P, R):
    # ---------------- handling.c ----------------
    P('Game/C/handling.c',
      "\tgte_SetRotMatrix(&c0->hd.where);\n\tgte_SetTransMatrix(&c0->hd.where);\n\n\tcar_cos = &car_cosmetics[c0->ap.model];\n",
      "\t/*d2x*/\n\tcar_cos = &car_cosmetics[c0->ap.model];\n")
    P('Game/C/handling.c',
      "\t\tgte_ldv0(&car_cos->cPoints[i]);\n\t\tgte_rtv0tr();\n\t\tgte_stlvnl(&outpoints[i]);\n",
      "\t\td2x_rtv_tr3(&c0->hd.where, &car_cos->cPoints[i], (int*)&outpoints[i]); /*d2x*/\n")
    P('Game/C/handling.c',
      "\tgte_SetRotMatrix(&cp->hd.where);\n\tgte_SetTransMatrix(&cp->hd.where);\n\n\tcar_cos = &car_cosmetics[cp->ap.model];\n\n\tboxDisp.vx = -car_cos->cog.vx;",
      "\t/*d2x*/\n\tcar_cos = &car_cosmetics[cp->ap.model];\n\n\tboxDisp.vx = -car_cos->cog.vx;")
    P('Game/C/handling.c',
      "\tgte_ldv0(&boxDisp);\n\tgte_rtv0tr();\n\n\tif (cp->controlType == CONTROL_TYPE_PURSUER_AI)",
      "\td2x_rtv_tr3(&cp->hd.where, &boxDisp, (int*)&cp->hd.oBox.location); /*d2x*/\n\n\tif (cp->controlType == CONTROL_TYPE_PURSUER_AI)")
    P('Game/C/handling.c',
      "\tgte_stlvnl(&cp->hd.oBox.location);\n\n\tSVECTOR svx",
      "\t/*d2x*/\n\n\tSVECTOR svx")
    P('Game/C/handling.c',
      "\tgte_ldsv(&svx);\n\n\tgte_rtir();\n\tcp->hd.oBox.length[1] = car_cos->colBox.vy;\n\tgte_stsv(&cp->hd.oBox.radii[0]);\n",
      "\td2x_rt_sv(&cp->hd.where, &svx, (SVECTOR*)&cp->hd.oBox.radii[0]); /*d2x*/\n\tcp->hd.oBox.length[1] = car_cos->colBox.vy;\n")
    P('Game/C/handling.c',
      "\tgte_ldsv(&svy);\n\tgte_rtir();\n\tcp->hd.oBox.length[2] = car_cos->colBox.vz;\n\tgte_stsv(&cp->hd.oBox.radii[1]);\n",
      "\td2x_rt_sv(&cp->hd.where, &svy, (SVECTOR*)&cp->hd.oBox.radii[1]); /*d2x*/\n\tcp->hd.oBox.length[2] = car_cos->colBox.vz;\n")
    P('Game/C/handling.c',
      "\tgte_ldsv(&svz);\n\tgte_rtir();\n\tgte_stsv(&cp->hd.oBox.radii[2]);\n",
      "\td2x_rt_sv(&cp->hd.where, &svz, (SVECTOR*)&cp->hd.oBox.radii[2]); /*d2x*/\n")

    # ---------------- wheelforces.c ----------------
    P('Game/C/wheelforces.c',
      "\t\tgte_ldv0(&car_cos->wheelDisp[i]);\n\n\t\tgte_rtv0tr();\n\t\tgte_stlvnl(wheelPos);\n",
      "\t\td2x_rtv_tr3(&cp->hd.where, &car_cos->wheelDisp[i], (int*)wheelPos); /*d2x*/\n")
    P('Game/C/wheelforces.c',
      "\tgte_SetRotMatrix(&cp->hd.where);\n\tgte_SetTransMatrix(&cp->hd.where);\n\n\tcount = 12;",
      "\t/*d2x*/\n\n\tcount = 12;")
    P('Game/C/wheelforces.c',
      "\t\tgte_ldv0(&car_cos->cPoints[count]);\n\n\t\tgte_rtv0tr();\n\n\t\tgte_stlvnl(pointPos);\n",
      "\t\td2x_rtv_tr3(&cp->hd.where, &car_cos->cPoints[count], (int*)pointPos); /*d2x*/\n")
    P('Game/C/wheelforces.c',
      "\t\t\tgte_SetTransMatrix(&cp->hd.where);\n\n\t\t\t_cl.extraangulardamping = 1;",
      "\t\t\t/*d2x*/\n\n\t\t\t_cl.extraangulardamping = 1;")

    # ---------------- bcollide.c ----------------
    P('Game/C/bcollide.c',
      "\t\tgte_SetRotMatrix(&cp->hd.where);\n\t\tgte_SetTransMatrix(&cp->hd.where);\n\n\t\tboxDisp.vx = -car_cos->cog.vx;",
      "\t\t/*d2x*/\n\n\t\tboxDisp.vx = -car_cos->cog.vx;")
    P('Game/C/bcollide.c',
      "\t\tgte_ldv0(&boxDisp);\n\t\tgte_rtv0tr();\n\t\tgte_stlvnl(&cd[0].x);\n",
      "\t\td2x_rtv_tr3(&cp->hd.where, &boxDisp, (int*)&cd[0].x); /*d2x*/\n")

    # ---------------- camera.c ----------------
    P('Game/C/camera.c',
      "\t\tgte_SetRotMatrix(&car_data[lp->cameraCarId].hd.where);\n\t\tgte_SetTransMatrix(&car_data[lp->cameraCarId].hd.where);\n",
      "\t\t/*d2x*/\n")
    P('Game/C/camera.c',
      "\t\tgte_ldv0(&boxDisp);\n\n\t\tgte_rtv0tr();\n\t\tgte_stlvnl(basePos);\n",
      "\t\td2x_rtv_tr3(&car_data[lp->cameraCarId].hd.where, &boxDisp, (int*)basePos); /*d2x*/\n")

    # ---------------- civ_ai.c ----------------
    P('Game/C/civ_ai.c',
      "\t\t\t\tgte_SetRotMatrix(&cp0->hd.where.m);\n\t\t\t\tgte_SetTransMatrix(&cp0->hd.where.m);\n\n\t\t\t\tboxDisp.vx = -car0_cos->cog.vx;",
      "\t\t\t\t/*d2x*/\n\n\t\t\t\tboxDisp.vx = -car0_cos->cog.vx;")
    P('Game/C/civ_ai.c',
      "\t\t\t\tgte_ldv0(&boxDisp);\n\t\t\t\tgte_rtv0tr();\n\t\t\t\tgte_stlvnl(&cd[0].x);\n",
      "\t\t\t\td2x_rtv_tr3(&cp0->hd.where, &boxDisp, (int*)&cd[0].x); /*d2x*/\n")

    # ---------------- pedest.c ----------------
    P('Game/C/pedest.c',
      "\t\tgte_SetRotMatrix(&cp1->hd.where);\n\t\tgte_SetTransMatrix(&cp1->hd.where);\n\n\t\tboxDisp.vx = -car_cos->cog.vx;",
      "\t\t/*d2x*/\n\n\t\tboxDisp.vx = -car_cos->cog.vx;")
    P('Game/C/pedest.c',
      "\t\tgte_ldv0(&boxDisp);\n\n\t\tgte_rtv0tr();\n\n\t\tgte_stlvnl(&cd[1].x);\n",
      "\t\td2x_rtv_tr3(&cp1->hd.where, &boxDisp, (int*)&cd[1].x); /*d2x*/\n")

    # ---------------- cor de entrada, TR e Z lidos direto do estado nativo ----------------
    R('Game/C/cars.c', r'MTC2\((\w+), 6\);', r'd2x_setrgbc(\1);')
    R('Game/C/draw.c', r'C2_TRX, C2_TRY, C2_TRZ', 'd2xf.tr[0], d2xf.tr[1], d2xf.tr[2]')
    R('Game/C/tile.c', r'C2_TRX, C2_TRY, C2_TRZ', 'd2xf.tr[0], d2xf.tr[1], d2xf.tr[2]')
    R('Game/C/targets.c', r'\(unsigned short\)C2_H\b', '(unsigned short)d2xf.h')
    R('Game/C/targets.c', r'\(unsigned int\)C2_SZ([123])\b', r'(unsigned int)d2xf.sz[\1]')
    R('Game/C/targets.c', r'\(short\)MFC2\(17\)', '(short)d2xf.sz[1]')
    R('Game/C/targets.c', r'\(short\)MFC2\(18\)', '(short)d2xf.sz[2]')
    R('Game/C/targets.c', r'\(short\)MFC2\(19\)', '(short)d2xf.sz[3]')

    # ---------------- DebugOverlay.cpp: o macro antigo dispensava o ';' ----------------
    R('utils/DebugOverlay.cpp', r'((?:gte|d2x)_stsxy0\(&line->x0\))(\s*\n)', r'\1;\2')

    # ---------------- LIBGTE.C: estado de geometria e funcoes sem uso saem ----------------
    P('PsyCross/src/psx/LIBGTE.C',
      "\tC2_ZSF3 = 341;\n\tC2_ZSF4 = 256;\n\tC2_H = 1000;\n\tC2_DQA = -98;\n\tC2_DQB = 340;\n\tC2_OFX = 0;\n\tC2_OFY = 0;\n",
      "\td2xf.zsf3 = 341;\n\td2xf.zsf4 = 256;\n\td2xf.h = 1000;\n\td2xf.dqa = -98;\n\td2xf.dqb = 340;\n\td2xf.ofx = 0;\n\td2xf.ofy = 0;\n")
    P('PsyCross/src/psx/LIBGTE.C',
      "\tC2_OFX = (ofx << 16);\n\tC2_OFY = (ofy << 16);\n",
      "\td2xf.ofx = (ofx << 16);\n\td2xf.ofy = (ofy << 16);\n")
    P('PsyCross/src/psx/LIBGTE.C',
      "void SetGeomScreen(int h)\n{\n\tC2_H = h;\n}",
      "void SetGeomScreen(int h)\n{\n\td2xf.h = (unsigned short)h;\n}")
    # blocos que uma versao anterior deste patch tinha apenas desativado com #if 0
    R('PsyCross/src/psx/LIBGTE.C', r'#if 0 /\*d2x\*/\r?\n.*?\r?\n\}\r?\n#endif\r?\n', '', re.S)
    for _fn in ('PushMatrix', 'PopMatrix', 'RotTransSV', 'RotTransPers3', 'RotTransPers4', 'NormalColor', 'NormalColor3',
                'NormalColorDpq', 'NormalColorCol', 'NormalColorCol3', 'DpqColor', 'ColorDpq', 'ColorCol', 'NormalClip',
                'LocalLight', 'RotAverageNclip4', 'MulMatrix', 'MulMatrix2', 'SetBackColor', 'SetFarColor', 'ApplyMatrixLV',
                'CompMatrix', 'CompMatrixLV', 'SetDQA', 'SetDQB', 'SetFogNear', 'SetFogNearFar'):
        R('PsyCross/src/psx/LIBGTE.C',
          r'(?m)^((?:MATRIX\*|VECTOR\*|SVECTOR\*|int|void)[ \t]+' + _fn + r'\([^\n]*\)\r?\n\{.*?\r?\n\}\r?\n)',
          '', re.S)

# Arquivos do GTE que foram substituidos por inteiro (port/compat/d2_xf.h): os patches antigos sobre eles deixam de valer.
GONE = {
    'PsyCross/include/psx/inline_c.h', 'PsyCross/include/psx/gtemac.h', 'PsyCross/include/psx/gtereg.h',
    'PsyCross/src/gte/PsyX_GTE.cpp', 'PsyCross/src/gte/PsyX_GTE.h', 'PsyCross/src/psx/INLINE_C.C',
}
# gte_algo -> d2x_algo (os contadores gte_peds_* do perfil ficam fora)
RENAME_RX = re.compile(r'\bgte_(?!peds_)([A-Za-z0-9_]+)')
RENAME_FILES = [
    'Game/C/debris.c', 'Game/C/cars.c', 'Game/C/tile.c', 'Game/C/motion_c.c', 'Game/C/draw.c', 'Game/C/shadow.c',
    'Game/C/event.c', 'Game/C/job_fx.c', 'Game/C/sky.c', 'Game/C/targets.c', 'Game/C/cosmetic.c', 'Game/C/sound.c',
    'Game/C/overmap.c', 'Game/C/drivinggames.c', 'Game/C/bomberman.c', 'Game/ASM/rndrasm.c',
    'utils/DebugOverlay.cpp', 'PsyCross/src/psx/LIBGTE.C',
]
def rename(t): return RENAME_RX.sub(lambda m: 'd2x_' + m.group(1), t)
