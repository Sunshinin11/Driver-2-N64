#!/usr/bin/env python3
"""Patches de fonte no motor copiado (port/redriver2): conversao little-endian (PS1) -> big-endian (N64) e outros ajustes.
Idempotente: cada patch so e aplicado se o trecho antigo existir; trechos ja trocados sao ignorados."""
import sys, os
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'port', 'redriver2', 'src_rebuild')
PATCHES = []
def P(path, old, new): PATCHES.append((path, old, new))
import re
REGEX = []   # (path, compiled regex, repl)  -- idempotentes se o resultado nao casar de novo com o padrao
def R(path, pattern, repl, flags=0): REGEX.append((path, re.compile(pattern, flags), repl))
def PM(path, macros, pattern, repl):
    """aplica um regex apenas dentro das definicoes '#define <macro>' (ate a proxima linha em branco)"""
    REGEX.append((path, ('macro', set(macros), re.compile(pattern), repl), None))

# ---- fontes ----
for _old, _new in (
    ('int ClearImage(RECT16* rect, u_char r, u_char g, u_char b)', 'int PsyX_compat_ClearImage(RECT16* rect, u_char r, u_char g, u_char b)'),
    ('int ClearImage2(RECT16* rect, u_char r, u_char g, u_char b)', 'int PsyX_compat_ClearImage2(RECT16* rect, u_char r, u_char g, u_char b)'),
    ('int DrawSync(int mode)', 'int PsyX_compat_DrawSync(int mode)'),
    ('int LoadImage(RECT16* rect, u_long* p)', 'int PsyX_compat_LoadImage(RECT16* rect, u_long* p)'),
    ('int LoadImage2(RECT16* rect, u_long* p)', 'int PsyX_compat_LoadImage2(RECT16* rect, u_long* p)'),
    ('int MoveImage(RECT16* rect, int x, int y)', 'int PsyX_compat_MoveImage(RECT16* rect, int x, int y)'),
    ('void DrawOTagEnv(u_long* p, DRAWENV* env)', 'void PsyX_compat_DrawOTagEnv(u_long* p, DRAWENV* env)'),
    ('void DrawOTag(u_long* p)', 'void PsyX_compat_DrawOTag(u_long* p)'),
    ('void DrawPrim(void* p)', 'void PsyX_compat_DrawPrim(void* p)'),
): P('PsyCross/src/psx/LIBGPU.C', _old, _new)

P('Game/C/pres.c',
  "\tnchars = *(int *)file;\n\tfile += sizeof(int);\n\n\t// copy character info",
  "\tnchars = (int)d2_rd32(file);\n\tfile += sizeof(int);\n\n\t// copy character info")
P('Game/C/pres.c',
  "\tfor (i = 0; i < 16; i++)\n\t\t*pclut++ &= ~0x8000;\n\n\tclut[1] |= 0x8000;\n\tclut[2] |= 0x8000;\n",
  "\td2_swap16_array(clut, 16);\t// N64: os bytes do CLUT vem little-endian\n\tfor (i = 0; i < 16; i++)\n\t\t*pclut++ &= ~0x8000;\n\n\tclut[1] |= 0x8000;\n\tclut[2] |= 0x8000;\n\td2_swap16_array(clut, 16);\t// LoadImage troca de novo\n")
P('Game/dr2locale.c',
  "\t\tnchars = *(int *)file;\n\t\tfile += sizeof(int);\n\n\t\t// skip character info",
  "\t\tnchars = (int)d2_rd32(file);\n\t\tfile += sizeof(int);\n\n\t\t// skip character info")

# ---- Loadfile/LoadfileSeg: sem retorno quando o arquivo nao existe (UB em C++) ----
P('Game/C/system.c',
  "#if USE_CD_FILESYSTEM\n\treturn LoadfileSeg(name, addr, 0, -1);\n#endif\n}",
  "#if USE_CD_FILESYSTEM\n\treturn LoadfileSeg(name, addr, 0, -1);\n#endif\n\treturn 0;\n}")
P('Game/C/system.c',
  "\tSDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, \"ERROR\", errPrint, NULL);\n\treturn 0;",
  "\tprintWarning(\"Cannot open '%s'\\n\", namebuffer);\n\treturn 0;")
# ---- som ----
P('Game/C/sound.c',
  "\tnum_samples = *(int *)address;\n\tslength = num_samples * sizeof(SAMPLE_DATA) + sizeof(int);\n\n\t// copy sample info\n\tmemcpy((u_char*)samples[bank], (u_char*)(address + sizeof(int)), num_samples * sizeof(SAMPLE_DATA));\n",
  "\tnum_samples = (int)d2_rd32(address);\n\tslength = num_samples * sizeof(SAMPLE_DATA) + sizeof(int);\n\n\t// copy sample info\n\tmemcpy((u_char*)samples[bank], (u_char*)(address + sizeof(int)), num_samples * sizeof(SAMPLE_DATA));\n\td2_swap32_array(samples[bank], num_samples * 4);\n")
P('Game/C/sound.c',
  "\tnum_samples = *(int *)address;\n\tslength = num_samples * sizeof(SAMPLE_DATA) + sizeof(int);\n\n\tmemcpy((u_char*)(samples[dbank] + lsbTabs.count[dbank]), (u_char*)(address + sizeof(int)), num_samples * sizeof(SAMPLE_DATA));\n",
  "\tnum_samples = (int)d2_rd32(address);\n\tslength = num_samples * sizeof(SAMPLE_DATA) + sizeof(int);\n\n\tmemcpy((u_char*)(samples[dbank] + lsbTabs.count[dbank]), (u_char*)(address + sizeof(int)), num_samples * sizeof(SAMPLE_DATA));\n\td2_swap32_array(samples[dbank] + lsbTabs.count[dbank], num_samples * 4);\n")

P('Game/C/gamesnd.c',
  "\t\tLoadfileSeg(name, (char*)blockLimit, 0, sizeof(blockLimit));\n",
  "\t\t{ LoadfileSeg(name, (char*)blockLimit, 0, sizeof(blockLimit)); d2_swap32_array(blockLimit, 73); }\n")
P('Game/C/gamesnd.c',
  "\tLoadfileSeg(name, (char*)musicpos, musicnum * 8, sizeof(musicpos));\n",
  "\tLoadfileSeg(name, (char*)musicpos, musicnum * 8, sizeof(musicpos));\n\td2_swap32_array(musicpos, 3);\n")
# ---- frontend ----
P('Game/Frontend/FEmain.c',
  "\t\t\t\tmemcpy((u_char*)&PsxScreens[i].buttons[j], (u_char*)ptr, sizeof(PSXBUTTON));\n",
  "\t\t\t\tmemcpy((u_char*)&PsxScreens[i].buttons[j], (u_char*)ptr, sizeof(PSXBUTTON));\n\t\t\t\t{ PSXBUTTON* pb = &PsxScreens[i].buttons[j];\n\t\t\t\t  pb->x = d2_bs16(pb->x); pb->y = d2_bs16(pb->y); pb->w = d2_bs16(pb->w); pb->h = d2_bs16(pb->h);\n\t\t\t\t  pb->userDrawFunctionNum = d2_bs32(pb->userDrawFunctionNum); pb->s_x = d2_bs16(pb->s_x); pb->s_y = d2_bs16(pb->s_y);\n\t\t\t\t  pb->action = d2_bs32(pb->action); pb->var = d2_bs32(pb->var); }\n")
P('Game/Frontend/FEmain.c',
  "\t\tmemcpy((u_char*)&feFont, (u_char*)_other_buffer, sizeof(feFont));\n",
  "\t\tmemcpy((u_char*)&feFont, (u_char*)_other_buffer, sizeof(feFont));\n\t\tfeFont.NumFonts = d2_bs32(feFont.NumFonts);\n")

# ---- missoes (MISSIONS.BLK / .D2MS): cabecalho, alvos, script e rota em little-endian ----
P('Game/C/mission.c',
  "// [D] [T]\nvoid LoadMission(int missionnum)\n{",
  """static void d2_swap_mission_header(MS_MISSION* m)
{
	d2_swap32_array(m, offsetof(MS_MISSION, timer) / 4);	// id..type
	m->timer = d2_bs16(m->timer);
	m->timerFlags = d2_bs16(m->timerFlags);
	d2_swap32_array(&m->strings, (sizeof(MS_MISSION) - offsetof(MS_MISSION, strings)) / 4);
}

static void d2_swap_mission_body(MS_MISSION* m)
{
	int i, j;
	MS_TARGET* t = (MS_TARGET*)((char*)m + m->size);
	u_int* script = (u_int*)(t + MAX_MISSION_TARGETS);
	for (i = 0; i < MAX_MISSION_TARGETS; i++, t++)
	{
		t->type = d2_bs32(t->type);
		if (t->type == Target_MultiCar)
		{
			for (j = 0; j < 5; j++)
			{
				t->multiCar[j].x = d2_bs32(t->multiCar[j].x);
				t->multiCar[j].z = d2_bs32(t->multiCar[j].z);
				t->multiCar[j].rot = d2_bs16(t->multiCar[j].rot);
			}
		}
		else
			d2_swap32_array((int*)t + 1, 15);
	}
	d2_swap32_array(script, m->strings);
}

// [D] [T]
void LoadMission(int missionnum)
{""")
P('Game/C/mission.c',
  "		LoadfileSeg(filename, (char*)&header, missionnum * 4, sizeof(int));\n\t\toffset = header & 0x7ffff;",
  "		LoadfileSeg(filename, (char*)&header, missionnum * 4, sizeof(int));\n\t\theader = d2_bs32(header);\n\t\toffset = header & 0x7ffff;")
P('Game/C/mission.c',
  "\tLoadfileSeg(filename, (char *)MissionLoadAddress, offset, sizeof(MS_MISSION));\n\n\tMissionHeader = MissionLoadAddress;",
  "\tLoadfileSeg(filename, (char *)MissionLoadAddress, offset, sizeof(MS_MISSION));\n\td2_swap_mission_header(MissionLoadAddress);\n\n\tMissionHeader = MissionLoadAddress;")
P('Game/C/mission.c',
  "\tmissionSize = LoadfileSeg(filename, (char *)MissionLoadAddress, offset, loadsize);\n",
  "\tmissionSize = LoadfileSeg(filename, (char *)MissionLoadAddress, offset, loadsize);\n\td2_swap_mission_header(MissionLoadAddress);\n\td2_swap_mission_body(MissionLoadAddress);\n")
P('Game/C/mission.c',
  "\t\t\trinfo = (ROUTE_INFO *)(MissionStrings + MissionHeader->route);\n",
  "\t\t\trinfo = (ROUTE_INFO *)(MissionStrings + MissionHeader->route);\n\t\t\trinfo->nJunctions = d2_bs32(rinfo->nJunctions);\n\t\t\td2_swap32_array(&rinfo->parameters, sizeof(LEAD_PARAMETERS) / 4);\n")

# ---- cosmeticos de carros (LCF): ponteiros int + struct de shorts ----
P('Game/C/cosmetic.c',
  "\t\t\toffset = *(int*)(lump_ptr + model * sizeof(int));\n\t\t\tcar_cosmetics[i] = *(CAR_COSMETICS*)((u_char*)lump_ptr + offset);\n",
  "\t\t\toffset = (int)d2_rd32(lump_ptr + model * sizeof(int));\n\t\t\tcar_cosmetics[i] = *(CAR_COSMETICS*)((u_char*)lump_ptr + offset);\n\t\t\td2_swap16_array(&car_cosmetics[i], sizeof(CAR_COSMETICS) / 2);\n")
P('Game/C/cosmetic.c',
  "\t\toffset = *(int*)(lump_ptr + model * sizeof(int));\n\t\tlevelSpecCosmetics[i] = *(CAR_COSMETICS*)((u_char*)lump_ptr + offset);\n",
  "\t\toffset = (int)d2_rd32(lump_ptr + model * sizeof(int));\n\t\tlevelSpecCosmetics[i] = *(CAR_COSMETICS*)((u_char*)lump_ptr + offset);\n\t\td2_swap16_array(&levelSpecCosmetics[i], sizeof(CAR_COSMETICS) / 2);\n")

# ---- nivel: citylumps, cabecalhos de cutscene ----
P('Game/C/system.c',
  "\t\tfread(citylumps[GameLevel], 1, sizeof(citylumps[GameLevel]), levFp);\n",
  "\t\tfread(citylumps[GameLevel], 1, sizeof(citylumps[GameLevel]), levFp);\n\t\td2_swap32_array(citylumps[GameLevel], sizeof(citylumps[GameLevel]) / 4);\n")
P('Game/C/cutscene.c',
  "\t\t\tLoadfileSeg(filename, (char*)&CutsceneHeader, 0, sizeof(CUTSCENE_HEADER));\n",
  "\t\t\t{ LoadfileSeg(filename, (char*)&CutsceneHeader, 0, sizeof(CUTSCENE_HEADER)); d2_swap_cuthdr(&CutsceneHeader); }\n")
P('Game/C/cutscene.c',
  "\t\t\tLoadfileSeg(filename, (char*)&ChaseHeader, 0, sizeof(CUTSCENE_HEADER));\n",
  "\t\t\t{ LoadfileSeg(filename, (char*)&ChaseHeader, 0, sizeof(CUTSCENE_HEADER)); d2_swap_cuthdr(&ChaseHeader); }\n")
P('Game/C/cutscene.c',
  "\t\tLoadfileSeg(filename, (char *)&header, 0, sizeof(CUTSCENE_HEADER));\n",
  "\t\tLoadfileSeg(filename, (char *)&header, 0, sizeof(CUTSCENE_HEADER));\n\t\td2_swap_cuthdr(&header);\n")
P('Game/C/cutrecorder.c',
  "\t\tLoadfileSeg(filename, (char*)&header, 0, sizeof(CUTSCENE_HEADER));\n",
  "\t\tLoadfileSeg(filename, (char*)&header, 0, sizeof(CUTSCENE_HEADER));\n\t\td2_swap_cuthdr(&header);\n")

# ---- lumps do nivel (LEV): cabecalhos, texturas, paletas ----
P('Game/C/main.c',
  "\t\tlump_type = *(int*)lump_ptr;\n\t\tseg_size = *(int*)(lump_ptr + 4);\n",
  "\t\tlump_type = (int)d2_rd32(lump_ptr);\n\t\tseg_size = (int)d2_rd32(lump_ptr + 4);\n")
P('Game/C/texture.c',
  "\ttpage_amount =  *(int *)lump_ptr;\n\ttexamount = *(int *)(lump_ptr + 4);\n\ttpage_position = (TP *)(lump_ptr + 8);\n",
  "\ttpage_amount =  (int)d2_rd32(lump_ptr);\n\ttexamount = (int)d2_rd32(lump_ptr + 4);\n\ttpage_position = (TP *)(lump_ptr + 8);\n\td2_swap32_array(tpage_position, (tpage_amount + 1) * 2);\n")
P('Game/C/texture.c',
  "\t\ttexamount = *(int *)ptr;\n\t\tptr += 4;\n\n#ifndef PSX\n\t\ttpage_ids[i] = (TEXINF *)D_MALLOC(sizeof(TEXINF) * texamount);\n\t\tmemcpy(tpage_ids[i], ptr, sizeof(TEXINF) * texamount);\n",
  "\t\ttexamount = (int)d2_rd32(ptr);\n\t\tptr += 4;\n\n#ifndef PSX\n\t\ttpage_ids[i] = (TEXINF *)D_MALLOC(sizeof(TEXINF) * texamount);\n\t\tmemcpy(tpage_ids[i], ptr, sizeof(TEXINF) * texamount);\n\t\t{ int k; for (k = 0; k < texamount; k++) { tpage_ids[i][k].id = d2_bs16(tpage_ids[i][k].id); tpage_ids[i][k].nameoffset = d2_bs16(tpage_ids[i][k].nameoffset); } }\n")
P('Game/C/texture.c',
  "\tnperms = *(int *)ptr;\n\tpermlist = (XYPAIR *)(ptr + 4);\n\n\tptr = (char *)&permlist[16];\n\n\tnspecpages = *(int *)ptr;\n\tspeclist = (XYPAIR *)(ptr + 4);\n",
  "\tnperms = (int)d2_rd32(ptr);\n\tpermlist = (XYPAIR *)(ptr + 4);\n\td2_swap32_array(permlist, 32);\n\n\tptr = (char *)&permlist[16];\n\n\tnspecpages = (int)d2_rd32(ptr);\n\tspeclist = (XYPAIR *)(ptr + 4);\n\td2_swap32_array(speclist, nspecpages * 2);\n")
P('Game/C/texture.c',
  "\tnpalettes = *(int *)tpageaddress;\n\ttpageaddress += 4;\n",
  "\tnpalettes = (int)d2_rd32(tpageaddress);\n\ttpageaddress += 4;\n")
P('Game/C/texture.c',
  "\t\t\tnpalettes = *(int *)tpagebuffer;\n",
  "\t\t\tnpalettes = (int)d2_rd32(tpagebuffer);\n")
P('Game/C/cars.c',
  "\ttotal_cluts = *(int*)lump_ptr;\n",
  "\ttotal_cluts = (int)d2_rd32(lump_ptr);\n")
P('Game/C/cars.c',
  "\twhile (*buffPtr != -1)\n\t{\n\t\tpalette = buffPtr[0];\n\t\ttexnum = buffPtr[1];\n\t\ttpageindex = buffPtr[2];\n\t\tclut_number = buffPtr[3];\n",
  "\twhile ((int)d2_rd32(buffPtr) != -1)\n\t{\n\t\tpalette = (int)d2_rd32(&buffPtr[0]);\n\t\ttexnum = (int)d2_rd32(&buffPtr[1]);\n\t\ttpageindex = (int)d2_rd32(&buffPtr[2]);\n\t\tclut_number = (int)d2_rd32(&buffPtr[3]);\n")

# ---- GTE: palavras empacotadas (dois shorts / quatro bytes) com semantica LE ----
_INL = 'PsyCross/include/psx/inline_c.h'
PM(_INL, ['gte_ldv0','gte_ldv1','gte_ldv2','gte_ldv3','gte_ldsxy3','gte_ldsxy3c','gte_SetLightMatrix','gte_SetColorMatrix','gte_SetRotMatrix'],
   r'\*\(uint\*\)(\(\(char\*\)\(r\d\)(?:\+\d+)?\)|\(r\d\))', r'd2_ld_ss(\1)')
PM(_INL, ['gte_ldrgb','gte_ldrgb3'], r'\*\(uint\*\)\(\(char\*\)\(r(\d)\)\)', r'd2_rd32((char*)(r\1))')
PM(_INL, ['gte_stsxy','gte_stsxy3','gte_stsxy1','gte_stsxy0'],
   r'\*\(uint\*\)\(\(char\*\)\(r(\d)\)\) = (MFC2\(\d+\));', r'd2_st_ss((char*)(r\1), \2);')
PM(_INL, ['gte_strgb','gte_strgb3'], r'\*\(uint\*\)\(\(char\*\)\(r(\d)\)\) = (MFC2\(\d+\));', r'd2_st_b4((char*)(r\1), \2);')
PM(_INL, ['gte_ReadRotMatrix','gte_ReadColorMatrix','gte_ReadLightMatrix'],
   r'\*\(uint\*\)\(\(char\*\)\(r0\)((?:\+(?:0|4|8|12|16))?)\) = (CFC2\(\d+\));', r'd2_st_ss((char*)(r0)\1, \2);')

# ---- modelos: swap + helpers ----
P('Game/C/models.c',
  "// [D] [T]\nvoid ProcessMDSLump(char *lump_file, int lump_size)\n{",
  """static int d2_force_geo = 0;
void d2_swap_model(void *mp, int size)
{
	MODEL* m = (MODEL*)mp;
	unsigned char* b = (unsigned char*)mp;
	int offs[5], k, end, o;
	d2_swap16_array(b, 3);
	d2_swap16_array(b + 8, 4);
	d2_swap32_array(b + 16, 5);
	if (m->instance_number != -1 && !d2_force_geo)
		return;		// geometria vem do modelo pai

	offs[0] = m->vertices; offs[1] = m->poly_block; offs[2] = m->normals; offs[3] = m->point_normals; offs[4] = m->collision_block;
	if (m->vertices && m->num_vertices)
		d2_swap16_array(b + m->vertices, m->num_vertices * 4);
	if (m->point_normals && m->num_point_normals)
		d2_swap16_array(b + m->point_normals, m->num_point_normals * 4);
	if (m->normals)
	{
		end = size;
		for (k = 0; k < 5; k++)
			if (k != 2 && offs[k] >= m->normals && offs[k] < end)
				end = offs[k];
		if (end > m->normals)
			d2_swap16_array(b + m->normals, (end - m->normals) / 2);
	}
	if (m->collision_block)
	{
		o = m->collision_block;
		d2_swap32_array(b + o, 1);
		k = *(int*)(b + o);
		if (k > 0 && k < 1000)
			d2_swap16_array(b + o + 4, k * 10);   // COLLISION_PACKET = 10 shorts (20 bytes)
	}
}

void d2_swap_car_models(char *lump, int lump_size)
{
	int i, j, next;
	int* offs = (int*)(lump + 4);
	char* base = lump + 4 + 160;
	d2_swap32_array(lump, 41);
	d2_force_geo = 1;	// modelos de carro: instance_number != -1 no arquivo, mas a geometria e propria
	for (i = 0; i < 39; i++)
	{
		if (offs[i] == -1)
			continue;
		next = lump_size - 164;
		for (j = 0; j < 39; j++)
			if (offs[j] > offs[i] && offs[j] < next)
				next = offs[j];
		d2_swap_model(base + offs[i], next - offs[i]);
	}
	d2_force_geo = 0;
}

// [D] [T]
void ProcessMDSLump(char *lump_file, int lump_size)
{""")
P('Game/C/models.c',
  "\tmodelAmts = *(int *)lump_file;\n",
  "\tmodelAmts = (int)d2_rd32(lump_file);\n")
P('Game/C/models.c',
  "\t\tsize = *(int*)mdsfile;\n\t\tmdsfile += sizeof(int);\n\n\t\tif (size)\n\t\t{\n\t\t\t// add the usage bit\n\t\t\tpermanentModelSlotBitfield[i >> 5] |= 1 << (i & 31);\n\t\t\t\n\t\t\tmodel = (MODEL*)mdsfile;\n",
  "\t\tsize = (int)d2_rd32(mdsfile);\n\t\tmdsfile += sizeof(int);\n\n\t\tif (size)\n\t\t{\n\t\t\t// add the usage bit\n\t\t\tpermanentModelSlotBitfield[i >> 5] |= 1 << (i & 31);\n\t\t\t\n\t\t\tmodel = (MODEL*)mdsfile;\n\t\t\td2_swap_model(model, size);\n")
P('Game/C/main.c',
  "\t\t\tcar_models_lump = (char*)ptr;\n",
  "\t\t\tcar_models_lump = (char*)ptr;\n\t\t\td2_swap_car_models((char*)ptr, seg_size);\n")
# captura de movimento (Tanner/pedestres): lump inteiro e int16 little-endian
P('Game/C/motion_c.c',
  "\t\tmemcpy((u_char*)MotionCaptureData[ThisMotion], (u_char*)lump_ptr, size);\n",
  "\t\tmemcpy((u_char*)MotionCaptureData[ThisMotion], (u_char*)lump_ptr, size);\n\t\td2_swap16_array(MotionCaptureData[ThisMotion], size / 2);\n")
# overmap: tabela de offsets (u16 little-endian) dos mapas RNC
P('Game/C/overmap.c',
  "UnpackRNC(MapBitMaps + *((ushort*)MapBitMaps + idx), MapBuffer);",
  "UnpackRNC(MapBitMaps + d2_rd16(MapBitMaps + idx * 2), MapBuffer);")
# carros: pacote de UVs e escrita nas primitivas
R('Game/C/cars.c', r'\*\(ushort ?\*\)&((?:pft3|pft4|pgt3|pgt4))->(uv\d)', r'd2_rd16(&\1->\2)')
R('Game/C/cars.c', r'\.pad = \*\(short ?\*\)&(c\d);', r'.pad = (short)d2_rd16(&\1);')
R('Game/C/cars.c', r'\*\(u_int ?\*\)&(\w+)->r(\d) = ([^;]+);', r'd2_st_b4(&\1->r\2, \3);')
R('Game/C/cars.c', r'\*\(u_int ?\*\)&(\w+)->u(\d) = ([^;]+);', r'd2_put_uvw(&\1->u\2, \3);')

# ---- spool info / mapa ----
P('Game/C/spool.c',
  "\tint value = *(int*)*ptr;\n\t*ptr += 4;",
  "\tint value = (int)d2_rd32(*ptr);\n\t*ptr += 4;")
P('Game/C/spool.c',
  "\tAreaData = (AreaDataStr *)ptr; \n\tptr += sizeof(AreaDataStr) * NumAreas;\n",
  "\tAreaData = (AreaDataStr *)ptr; \n\tfor (i = 0; i < NumAreas; i++) d2_swap16_array(&AreaData[i], 4);\n\tptr += sizeof(AreaDataStr) * NumAreas;\n")
P('Game/C/spool.c',
  "\tint* pvsSizes = (int *)ptr;\n\tptr += sizeof(int) * 4;\n",
  "\tint* pvsSizes = (int *)ptr;\n\tptr += sizeof(int) * 4;\n\td2_swap32_array(slots_add, 12);\n")
P('Game/C/spool.c',
  "\tspoolinfo_offsets = (ushort*)ptr;\n\tptr += sizeof(ushort) * num_of_regions;\n\n\tint spoolinfo_size = getIntAdv(ptr);\n\tRegionSpoolInfo = ptr;\n",
  "\tspoolinfo_offsets = (ushort*)ptr;\n\td2_swap16_array(spoolinfo_offsets, num_of_regions);\n\tptr += sizeof(ushort) * num_of_regions;\n\n\tint spoolinfo_size = getIntAdv(ptr);\n\tRegionSpoolInfo = ptr;\n\t{\n\t\tint a, b;\n\t\tfor (a = 0; a < num_of_regions; a++)\n\t\t{\n\t\t\tint dup = 0;\n\t\t\tif (spoolinfo_offsets[a] == 0xffff) continue;\n\t\t\tfor (b = 0; b < a; b++) if (spoolinfo_offsets[b] == spoolinfo_offsets[a]) { dup = 1; break; }\n\t\t\tif (!dup) d2_swap16_array(RegionSpoolInfo + spoolinfo_offsets[a], 1);\n\t\t}\n\t}\n")
P('Game/C/map.c',
  "\tmemcpy((u_char*)&cell_header, (u_char*)lump_ptr, sizeof(OUT_CELL_FILE_HEADER));\n",
  "\tmemcpy((u_char*)&cell_header, (u_char*)lump_ptr, sizeof(OUT_CELL_FILE_HEADER));\n\td2_swap32_array(&cell_header, sizeof(OUT_CELL_FILE_HEADER) / 4);\n")
P('Game/C/map.c',
  "\tnum_straddlers = *(int*)lump_ptr;\n\n\tInitCellData();\n\tmemcpy((u_char*)cell_objects, (u_char*)lump_ptr + 4, num_straddlers * sizeof(PACKED_CELL_OBJECT));\n",
  "\tnum_straddlers = (int)d2_rd32(lump_ptr);\n\n\tInitCellData();\n\tmemcpy((u_char*)cell_objects, (u_char*)lump_ptr + 4, num_straddlers * sizeof(PACKED_CELL_OBJECT));\n\td2_swap16_array(cell_objects, num_straddlers * 4);\n")

# ---- estradas, denting, detalhe baixo ----
P('Game/C/convert.c',
  "\t*dest = *source;\n\tdest[1] = source[1];\n\tdest[2] = source[2];\n\tdest[3] = source[3];\n",
  "\t*dest = source[3];\n\tdest[1] = source[2];\n\tdest[2] = source[1];\n\tdest[3] = source[0];\n")
P('Game/C/dr2roads.h',
  "((*(ushort*)&(rd)->NumLanes & 0xFFFF) == 0xFF01)",
  "(((rd)->NumLanes == 1) && ((u_char)(rd)->LaneDirs == 0xFF))")
P('Game/C/dr2roads.c',
  "\tDriver2StraightsPtr = (DRIVER2_STRAIGHT *)(lump_file + 4);\n",
  "\tDriver2StraightsPtr = (DRIVER2_STRAIGHT *)(lump_file + 4);\n\t{ int k; for (k = 0; k < NumDriver2Straights; k++) { d2_swap32_array(&Driver2StraightsPtr[k], 2); d2_swap16_array((char*)&Driver2StraightsPtr[k] + 8, 7); } }\n")
P('Game/C/dr2roads.c',
  "\tDriver2CurvesPtr = (DRIVER2_CURVE *)(lump_file + 4);\n",
  "\tDriver2CurvesPtr = (DRIVER2_CURVE *)(lump_file + 4);\n\t{ int k; for (k = 0; k < NumDriver2Curves; k++) { d2_swap32_array(&Driver2CurvesPtr[k], 2); d2_swap16_array((char*)&Driver2CurvesPtr[k] + 8, 8); } }\n")
P('Game/C/dr2roads.c',
  "\t\tfor (loop = 0; loop < NumDriver2Junctions; loop++)\n\t\t{\n\t\t\tp->flags = old->flags;\n",
  "\t\tfor (loop = 0; loop < NumDriver2Junctions; loop++)\n\t\t{\n\t\t\td2_swap32_array(old, 2); d2_swap16_array((char*)old + 8, 8);\n\t\t\tp->flags = old->flags;\n")
P('Game/C/dr2roads.c',
  "\t// [A] patch Vegas roads\n\tif (GameLevel == 2)\n\t{\n\t\tDriver2JunctionsPtr[8244 & 0x1fff].ExitIdx[1] = 348;",
  "\tif (!fix)\n\t{\n\t\tfor (loop = 0; loop < NumDriver2Junctions; loop++)\n\t\t{\n\t\t\td2_swap16_array(&Driver2JunctionsPtr[loop], 4);\n\t\t\td2_swap32_array((char*)&Driver2JunctionsPtr[loop] + 8, 1);\n\t\t}\n\t}\n\n\t// [A] patch Vegas roads\n\tif (GameLevel == 2)\n\t{\n\t\tDriver2JunctionsPtr[8244 & 0x1fff].ExitIdx[1] = 348;")
P('Game/C/tile.c',
  "\tLow2HighDetailTable = (ushort *)lump_ptr;\n",
  "\tLow2HighDetailTable = (ushort *)lump_ptr;\n\td2_swap16_array(lump_ptr, lump_size / 2);\n")
P('Game/C/tile.c',
  "tileTypes = *(u_int *)(model + 1) >> 2;",
  "tileTypes = d2_rd32(model + 1) >> 2;")
P('Game/C/event.c',
  "\t\tevent = (EVENT*)mallocptr;\n",
  "\t\tevent = (EVENT*)mallocptr;\n\t\tmemset(event, 0, sizeof(EVENT) * 128);\t// no PS1 a RAM comeca zerada; aqui o heap tem lixo\n")
P('Game/C/pedest.c',
  "void ProcessChairLump(char* lump_file, int lump_size)\n{\n\tseated_pedestrian = (SEATEDPTR)lump_file;\n}",
  "void ProcessChairLump(char* lump_file, int lump_size)\n{\n\t{ unsigned char* cp = (unsigned char*)lump_file; int cn = 0;\n\t\twhile (cn < lump_size / 12) { short crot = (short)d2_rd16(cp + 8); d2_swap32_array(cp, 2); d2_swap16_array(cp + 8, 1); if (crot == 9999) break; cp += 12; cn++; } }\n\tseated_pedestrian = (SEATEDPTR)lump_file;\n}")
P('Game/C/denting.c',
  "\t\t\toffset = *(int *)(lump_ptr + model * 4);\n",
  "\t\t\toffset = (int)d2_rd32(lump_ptr + model * 4);\n")
P('Game/C/mission.c',
  "\t\t\tLeadValues = rinfo->parameters;\n",
  "\t\t\td2_swap32_array(Driver2TempJunctionsPtr, NumTempJunctions);\n\t\t\tLeadValues = rinfo->parameters;\n")

# ---- spool (regioes, areas, tpages): leitura sob demanda do .LEV na ROM + conversao LE->BE ----
R('Game/C/spool.c', r'\t// read entire spool data\r?\n\tif \(g_CurrentLevelSpoolData == NULL\)\r?\n\t\{.*?\r?\n\t\}\r?\n', '', re.S)
R('Game/C/spool.c', r'\t\tchar\* spoolDataPtr;\r?\n', '\t\tint spoolSector;\n')
R('Game/C/spool.c', r'\t\t\tint readSize = \(nsectors\)\*CDSECTOR_SIZE; \\\r?\n\t\t\tmemcpy\(dest, spoolDataPtr, readSize\); \\\r?\n\t\t\tspoolDataPtr \+= readSize; \\\r?\n',
  '\t\t\tint ns_ = (nsectors); \\\n\t\t\td2_read_sectors((char*)(dest), spoolSector, ns_); \\\n\t\t\tspoolSector += ns_; \\\n')
R('Game/C/spool.c', r'\t\tspoolDataPtr = g_CurrentLevelSpoolData - SpoolLumpOffset \+ current->sector \* CDSECTOR_SIZE;',
  '\t\tspoolSector = current->sector;')
P('Game/C/spool.c',
  "#if defined(SIMPLE_SPOOL)\nextern char g_CurrentLevelFileName[64];",
  "extern int d2_read_sectors(char* dest, int sector, int nsectors);\n#if defined(SIMPLE_SPOOL)\nextern char g_CurrentLevelFileName[64];")
P('Game/C/spool.c',
  "\tpacktype = *(int *)(cell_addr + 4);\n\tsource_packed_data = (ushort *)(cell_addr + 8);\n",
  "\tpacktype = (int)d2_rd32(cell_addr + 4);\n\tsource_packed_data = (ushort *)(cell_addr + 8);\n\td2_swap16_array(source_packed_data, 1100);\n")
P('Game/C/spool.c',
  "\t\t\tnpalettes = *(int *)(model_spool_buffer + 0xE000);\n",
  "\t\t\tnpalettes = (int)d2_rd32(model_spool_buffer + 0xE000);\n")
P('Game/C/spool.c',
  "\t\t\t\tclutptr[0] = getClut(cluts.x + 16, cluts.y) << 0x10 | getClut(cluts.x, cluts.y);\n\t\t\t\tclutptr[1] = getClut(cluts.x + 48, cluts.y) << 0x10 | getClut(cluts.x + 32, cluts.y);\n",
  "\t\t\t\t((ushort*)clutptr)[0] = getClut(cluts.x, cluts.y);\n\t\t\t\t((ushort*)clutptr)[1] = getClut(cluts.x + 16, cluts.y);\n\t\t\t\t((ushort*)clutptr)[2] = getClut(cluts.x + 32, cluts.y);\n\t\t\t\t((ushort*)clutptr)[3] = getClut(cluts.x + 48, cluts.y);\n")
P('Game/C/spool.c',
  "\tnmodels = *newmodels;\n",
  "\tnmodels = d2_rd16(newmodels);\n")
P('Game/C/spool.c',
  "\t\tmodel_number = new_model_numbers[i];\n\n\t\tsize = *(int *)addr;\n\t\tmodel = (MODEL *)(addr + 4);\n",
  "\t\tmodel_number = d2_rd16(&new_model_numbers[i]);\n\n\t\tsize = (int)d2_rd32(addr);\n\t\tmodel = (MODEL *)(addr + 4);\n\t\td2_swap_model(model, size);\n")
P('Game/C/spool.c',
  "\tspool = &spool_regioninfo[spool_regionpos];\n\n\tunpack_cellpointers(",
  "\tspool = &spool_regioninfo[spool_regionpos];\n\n\td2_swap_region_data(spool);\n\tunpack_cellpointers(")
P('Game/C/spool.c',
  "// [D] [T]\nvoid GotRegion(void)\n{",
  """// N64: dados de regiao chegam em little-endian (cells, objetos, mapa de estradas/BSP)
static void d2_swap_roadmap(char* pvs, int bytes)
{
	int i, k;
	int cbr, end;
	unsigned short* sb;
	int offs[3], ends[3];
	char* base;

	d2_swap32_array(pvs - 4, 1);
	cbr = *(int*)(pvs - 4);
	if (cbr < 0 || cbr >= bytes)
		return;
	base = pvs + cbr;
	end = bytes - cbr;			// bytes que sobram depois de 'base'
	sb = (unsigned short*)base;
	d2_swap16_array(base, 4);
	if (sb[0] != 2)
		return;
	offs[0] = sb[1]; offs[1] = sb[2]; offs[2] = sb[3];
	for (i = 0; i < 3; i++)
	{
		ends[i] = end;
		for (k = 0; k < 3; k++)
			if (offs[k] > offs[i] && offs[k] < ends[i])
				ends[i] = offs[k];
	}
	{
		int first = end;
		for (i = 0; i < 3; i++) if (offs[i] < first) first = offs[i];
		d2_swap16_array(base + 8, (first - 8) / 2);					// grade de superficies
	}
	d2_swap16_array(base + offs[1], (ends[1] - offs[1]) / 2);		// dados BSP (shorts)
	for (k = offs[0]; k + 12 <= ends[0]; k += 12)					// sdPlane
	{
		d2_swap16_array(base + k, 4);
		d2_swap32_array(base + k + 8, 1);
	}
	if (offs[2] != offs[1])										// (BSP e nos podem compartilhar a mesma area)
		d2_swap16_array(base + offs[2], (ends[2] - offs[2]) / 2);		// sdNode (pares de shorts)
}

static void d2_swap_region_data(SPL_REGIONINFO* r)
{
	Spool* sp;
	int t;

	if (spoolinfo_offsets[r->region_to_unpack] == 0xffff)
		return;
	sp = (Spool*)(RegionSpoolInfo + spoolinfo_offsets[r->region_to_unpack]);
	t = r->target_barrel_region;

	d2_swap16_array(cells + cell_slots_add[t], sp->cell_data_size[0] * 1024);
	d2_swap16_array(cell_objects + num_straddlers + cell_objects_add[t], sp->cell_data_size[2] * 1024);
	d2_swap_roadmap(PVS_Buffers[t], sp->roadm_size * 2048 - 4);
}

// [D] [T]
void GotRegion(void)
{""")
# bitfields do BSP (sdNode): ordem dos campos invertida em big-endian
R('Game/dr2types.h', r'struct sdNodePACKED\r?\n\{\r?\n\tint angle : 11;\r?\n\tint dist : 12;\r?\n\tint offset : 8;\r?\n\tint node : 1;\r?\n\};',
  'struct sdNodePACKED\n{\n\tint node : 1;\n\tint offset : 8;\n\tint dist : 12;\n\tint angle : 11;\n};')

# ---- RNC: cabecalho little-endian, possivelmente desalinhado ----
R('Game/ASM/rnc_2.c', r'    RNCheader\* hdr = \(RNCheader\*\)src;\r?\n\r?\n    int method = testRNC\(hdr->identifier\);\r?\n\r?\n    if \(method == 2\)\r?\n        RNCunpack2\(\(unsigned char\*\)src \+ RNC_HEADER_LENGTH, hdr->packSize, \(unsigned char\*\)dest, hdr->unpackSize\);',
  '    int method = testRNC(d2_rd32(src));\n\n    if (method == 2)\n        RNCunpack2((unsigned char*)src + RNC_HEADER_LENGTH, d2_rd32(src + 8), (unsigned char*)dest, d2_rd32(src + 4));')

# ---- PVS: nibbles em bytes (sem escrita de short desalinhada) ----
P('Game/C/map.c',
  "\t\t((ushort*)nybblearray)[i] = M_SHRT_2(celldata[i], celldata[i] >> 4) & 0xf0f;",
  "\t{ nybblearray[i * 2] = (celldata[i] >> 4) & 0xf; nybblearray[i * 2 + 1] = celldata[i] & 0xf; }")


# ---- sdNode (BSP de estradas): regiao trocada em 16 bits; valor/bitfields reconstruidos do par de shorts ----
P('Game/dr2types.h',
  "} sdNode;\n",
  """} sdNode;

static inline int d2_sd_value(const sdNode* n)
{
	const unsigned short* s = (const unsigned short*)n;
	return (int)((unsigned)s[0] | ((unsigned)s[1] << 16));
}
static inline int d2_sd_angle(const sdNode* n) { return ((int)((unsigned)d2_sd_value(n) << 21)) >> 21; }
static inline int d2_sd_dist(const sdNode* n) { return ((int)((unsigned)d2_sd_value(n) << 9)) >> 20; }
static inline int d2_sd_offset(const sdNode* n) { return ((int)((unsigned)d2_sd_value(n) << 1)) >> 24; }
""")
P('Game/C/dr2roads.c',
  "	while (node->value < 0) // & 0x80000000U)\n\t{\n\t\tdot = pos->y * RCOS(node->n.angle) - pos->x * RSIN(node->n.angle);\n\n\t\tif (dot < node->n.dist * 4096)\n\t\t\tnode++;\n\t\telse\n\t\t\tnode += node->n.offset;",
  "\twhile (d2_sd_value(node) < 0) // & 0x80000000U)\n\t{\n\t\tdot = pos->y * RCOS(d2_sd_angle(node)) - pos->x * RSIN(d2_sd_angle(node));\n\n\t\tif (dot < d2_sd_dist(node) * 4096)\n\t\t\tnode++;\n\t\telse\n\t\t\tnode += d2_sd_offset(node);")
P('Game/C/dr2roads.c',
  "		if (node->value > -1)\n\t\t{\n\t\t\tbase += node->value;",
  "\t\tif (d2_sd_value(node) > -1)\n\t\t{\n\t\t\tbase += d2_sd_value(node);")
P('Game/C/dr2roads.c',
  "		plane = FindRoadInBSP(node + 1, base);\n\t\tnode += node->n.offset;",
  "\t\tplane = FindRoadInBSP(node + 1, base);\n\t\tnode += d2_sd_offset(node);")
P('Game/ASM/d2mapasm.c',
  "while (search->value < 0)", "while (d2_sd_value(search) < 0)")
P('Game/ASM/d2mapasm.c',
  "search += search->n.offset;", "search += d2_sd_offset(search);")

# ---- escritas de cor (r,g,b,code) em primitivas: palavra empacotada little-endian -> bytes ----
for _f in ['Game/C/draw.c','Game/C/tile.c','Game/C/debris.c','Game/C/shadow.c','Game/C/sky.c','Game/C/job_fx.c','Game/C/event.c','Game/C/overmap.c','Game/C/targets.c','Game/C/motion_c.c','Game/C/cars.c']:
    R(_f, r'\*\s*\(\s*(?:u_int|uint|u_long|ulong)\s*\*?\s*\)\s*&\s*([\w\[\]\.>-]+(?:->|\.)(?:r0|r1|r2|r3))\s*=\s*([^;]+);', r'd2_st_b4(&\1, \2);')


# ---- primitivas DR_* (palavras de comando u_int): guardadas em ordem de bytes little-endian para que P_TAG.code (byte 11) valha ----
R('PsyCross/include/psx/libgpu.h',
  r'(?<!#endif\n#endif\n\n) /\*\r?\n  \* Primitive Handling Macros\r?\n  \*/',
  "#ifndef PSX_LEW\n#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__\n#define PSX_LEW(x) __builtin_bswap32((u_int)(x))\n#else\n#define PSX_LEW(x) ((u_int)(x))\n#endif\n#endif\n\n /*\n  * Primitive Handling Macros\n  */")
R('PsyCross/include/psx/libgpu.h',
  r'\(\((p|pt)\)->code\[0\] = (?!PSX_LEW)(_get_mode\(dfe, dtd, tpage\)|_get_tw\(tw\)|0xa0000000|0xe6000000\|\(pbw\?0x01:0\))\)',
  r'((\1)->code[0] = PSX_LEW(\2))')
R('PsyCross/include/psx/libgpu.h',
  r'\(\(p\)->code\[1\] = (?!PSX_LEW)(_get_tw\(\(RECT16 \*\)tw\))\)',
  r'((p)->code[1] = PSX_LEW(\1))')
R('PsyCross/src/psx/LIBGPU.C', r'dr_env->code\[(\d)\] = (?!PSX_LEW\()([^;]+);', r'dr_env->code[\1] = PSX_LEW(\2);')
R('PsyCross/src/psx/LIBGPU.C', r'\tp->code\[([01])\] = (?!PSX_LEW\()(\(+(?:r->x|\(r->x \+ r->w\))[^;]+);', r'\tp->code[\1] = PSX_LEW(\2);')
P('PsyCross/src/psx/LIBGPU.C', "\tp->code[0] = 0x1000000;", "\tp->code[0] = PSX_LEW(0x1000000);")
P('PsyCross/src/psx/LIBGPU.C', "\tp->code[0] = 0xB1000000 | grTextureId;", "\tp->code[0] = PSX_LEW(0xB1000000 | grTextureId);")
P('PsyCross/src/psx/LIBGPU.C', "\tp->code = 0xB2000000;", "\tp->code = PSX_LEW(0xB2000000);")
P('PsyCross/src/gpu/PsyX_GPU.cpp', "\t\tconst u_int code = codePtr[i];", "\t\tconst u_int code = PSX_LEW(codePtr[i]);")
P('PsyCross/src/gpu/PsyX_GPU.cpp', "overrideTexture = psytex->code[0] & 0xFFFFFF;", "overrideTexture = PSX_LEW(psytex->code[0]) & 0xFFFFFF;")


# ---- GTE: MTC2/MFC2/CTC2 inline (sem chamada nem switch em cada registrador) ----
P('PsyCross/include/psx/inline_c.h', "/*\n * Type 1 functions\n */", '#include "d2_gte_inline.h"\n/*\n * Type 1 functions\n */')
P('PsyCross/src/psx/INLINE_C.C', '#include "../gte/PsyX_GTE.h"\n', '#include "../gte/PsyX_GTE.h"\n#undef MTC2\n#undef MTC2_S\n#undef MFC2\n#undef MFC2_S\n#undef CTC2\n')

# GTE emulado: funcoes auxiliares static inline (o gcc passa a embuti-las em GTE_RotTransPers / GTE_operator)
R('PsyCross/src/gte/PsyX_GTE.cpp', r'^(?!static )(int|long long|unsigned int) (A1|A2|A3|Lm_B1|Lm_B2|Lm_B3|Lm_B3_sf|Lm_C1|Lm_C2|Lm_C3|Lm_D|Lm_E|F|Lm_G1|Lm_G2|Lm_G1_ia|Lm_G2_ia|Lm_H|BOUNDS)\(', r'static inline \1 \2(', re.M)
# GTE: RTPS/RTPT em caminho rapido (so 32 bits + somas de 64): vale quando nada estoura/satura (sem flags); senao cai na versao original.
_RTPS_OLD = "int GTE_RotTransPers(int idx, int lm)\n{\n\tint h_over_sz3;\n"
_RTPS_NEW = r"""static int GTE_RotTransPers_slow(int idx, int lm);
// caminho rapido do RTPS (sf=1): so quando nenhuma saturacao/estouro acontece; resultado identico ao original
int GTE_RotTransPers(int idx, int lm)
{
	if (m_sf == 1 && idx < 3)
	{
		const int vx = gteRegs.CP2D.p[idx << 1].sw.l, vy = gteRegs.CP2D.p[idx << 1].sw.h, vz = gteRegs.CP2D.p[(idx << 1) + 1].sw.l;
		const long long m1 = ((long long)C2_TRX << 12) + (long long)(C2_R11 * vx) + (long long)(C2_R12 * vy) + (long long)(C2_R13 * vz);
		const long long m2 = ((long long)C2_TRY << 12) + (long long)(C2_R21 * vx) + (long long)(C2_R22 * vy) + (long long)(C2_R23 * vz);
		const long long m3 = ((long long)C2_TRZ << 12) + (long long)(C2_R31 * vx) + (long long)(C2_R32 * vy) + (long long)(C2_R33 * vz);
		if (m1 <= 0x7ffffffffffLL && m1 >= -0x8000000000LL && m2 <= 0x7ffffffffffLL && m2 >= -0x8000000000LL && m3 <= 0x7ffffffffffLL && m3 >= -0x8000000000LL)
		{
			const int a1 = (int)(m1 >> 12), a2 = (int)(m2 >> 12), a3 = (int)(m3 >> 12);
			const int imin = lm ? 0 : -0x8000;
			if (a1 <= 0x7fff && a1 >= imin && a2 <= 0x7fff && a2 >= imin && a3 <= 0x7fff && a3 > 0 && a3 >= imin)
			{
				unsigned int hh = gte_divide(C2_H, a3);
				if (hh != 0xffffffffu)
				{
					if (hh > 0x1ffff) hh = 0x1ffff;
					const long long fx = (long long)C2_OFX + (long long)a1 * (int)hh;
					const long long fy = (long long)C2_OFY + (long long)a2 * (int)hh;
					if (fx <= 0x7fffffffLL && fx >= -0x80000000LL && fy <= 0x7fffffffLL && fy >= -0x80000000LL)
					{
						int sx = (int)(fx >> 16), sy = (int)(fy >> 16);
						if (sx > 0x3ff) { sx = 0x3ff; C2_FLAG |= (1 << 31) | (1 << 14); } else if (sx < -0x400) { sx = -0x400; C2_FLAG |= (1 << 31) | (1 << 14); }
						if (sy > 0x3ff) { sy = 0x3ff; C2_FLAG |= (1 << 31) | (1 << 13); } else if (sy < -0x400) { sy = -0x400; C2_FLAG |= (1 << 31) | (1 << 13); }
						m_mac3 = m3; m_mac0 = fy;
						C2_MAC1 = a1; C2_MAC2 = a2; C2_MAC3 = a3;
						C2_IR1 = (short)a1; C2_IR2 = (short)a2; C2_IR3 = (short)a3;
						C2_SZ0 = C2_SZ1; C2_SZ1 = C2_SZ2; C2_SZ2 = C2_SZ3; C2_SZ3 = (unsigned short)a3;
						C2_SXY0 = C2_SXY1; C2_SXY1 = C2_SXY2;
						C2_SX2 = (short)sx; C2_SY2 = (short)sy;
						return (int)hh;
					}
				}
			}
		}
	}
	return GTE_RotTransPers_slow(idx, lm);
}

static int GTE_RotTransPers_slow(int idx, int lm)
{
	int h_over_sz3;
"""
P('PsyCross/src/gte/PsyX_GTE.cpp', _RTPS_OLD, _RTPS_NEW)


# ---- N64: perfil visual do PS1: mesmos limites de objetos e distancias de LOD, sem ativar o scratchpad do PS1 ----
P('Game/C/draw.h', "#else\n\n#define MAX_DRAWN_BUILDINGS\t\t384\n#define MAX_DRAWN_TILES\t\t\t750\n#define MAX_DRAWN_ANIMATING\t\t48\n#define MAX_DRAWN_SPRITES\t\t128\n\n#define DRAW_LOD_DIST_HIGH\t\t7000\n#define DRAW_LOD_DIST_LOW\t\t10000\n\nextern _pct& plotContext;",
  "#elif defined(D2_N64_PERF)\n\n#define MAX_DRAWN_BUILDINGS\t\t192\n#define MAX_DRAWN_TILES\t\t\t320\n#define MAX_DRAWN_ANIMATING\t\t20\n#define MAX_DRAWN_SPRITES\t\t75\n\n#define DRAW_LOD_DIST_HIGH\t\t4000\n#define DRAW_LOD_DIST_LOW\t\t7000\n\nextern _pct& plotContext;\n\n#else\n\n#define MAX_DRAWN_BUILDINGS\t\t384\n#define MAX_DRAWN_TILES\t\t\t750\n#define MAX_DRAWN_ANIMATING\t\t48\n#define MAX_DRAWN_SPRITES\t\t128\n\n#define DRAW_LOD_DIST_HIGH\t\t7000\n#define DRAW_LOD_DIST_LOW\t\t10000\n\nextern _pct& plotContext;")
P('Game/C/draw.h', "#elif defined(D2_N64_PERF)\n\n#define MAX_DRAWN_BUILDINGS",
  "#elif defined(D2_N64_LOW)\n\n#define MAX_DRAWN_BUILDINGS\t\t128\n#define MAX_DRAWN_TILES\t\t\t192\n#define MAX_DRAWN_ANIMATING\t\t12\n#define MAX_DRAWN_SPRITES\t\t48\n\n#define DRAW_LOD_DIST_HIGH\t\t2500\n#define DRAW_LOD_DIST_LOW\t\t4500\n\nextern _pct& plotContext;\n\n#elif defined(D2_N64_PERF)\n\n#define MAX_DRAWN_BUILDINGS")
P('Game/C/draw.c', "int goFaster = 0;\t// [A] was 1", "int goFaster = 1;\t// N64: como no PS1 (metade das celulas visiveis)")
P('Game/C/draw.c', "\tdrawData.farClipLimit = 125000;", "\tdrawData.farClipLimit = 60000;\t// N64: como no PS1")

# ---- Fidelidade PS1 no N64: remove expansoes de limites/efeitos/conteudo feitas para PC, sem ativar caminhos de plataforma PSX ----
R('Game/dr2limits.h', r'#ifndef PSX', '#if !defined(PSX) && !defined(D2_PSX_FIDELITY)')
P('Game/C/camera.h', "#ifdef PSX\n#define VIEW_DRAW_DISTANCE 16000", "#if defined(PSX) || defined(D2_PSX_FIDELITY)\n#define VIEW_DRAW_DISTANCE 16000")
P('Game/C/draw.c', "#ifdef PSX\n#define MAX_TREE_SHADOW_DISTANCE 7000", "#if defined(PSX) || defined(D2_PSX_FIDELITY)\n#define MAX_TREE_SHADOW_DISTANCE 7000")
# ---- Evita wrap-around de 16 bits no FrustrumCheck16 em DrawMapPSX (explosao de sprites distantes) ----
P('Game/C/draw.c',
  "model = modelpointers[(ppco->value >> 6) | ((ppco->pos).vy & 1) << 10];\n\n\t\t\t\t\tif (FrustrumCheck16(ppco, model->bounding_sphere) != -1)",
  "model = modelpointers[(ppco->value >> 6) | ((ppco->pos).vy & 1) << 10];\n\n\t\t\t\t\t{\n\t\t\t\t\t\tint _dx = (ci.nearCell.x + (short)(ppco->pos.vx - ci.nearCell.x)) - camera_position.vx;\n\t\t\t\t\t\tint _dz = (ci.nearCell.z + (short)(ppco->pos.vz - ci.nearCell.z)) - camera_position.vz;\n\t\t\t\t\t\tif (ABS(_dx) > VIEW_DRAW_DISTANCE || ABS(_dz) > VIEW_DRAW_DISTANCE) continue;   // evita wrap-around de 16 bits no FrustrumCheck16\n\t\t\t\t\t}\n\n\t\t\t\t\tif (FrustrumCheck16(ppco, model->bounding_sphere) != -1)")
P('Game/C/system.h', "#ifdef PSX\n#define\tOTSIZE\t0x1080", "#if defined(PSX) || defined(D2_PSX_FIDELITY)\n#define\tOTSIZE\t0x1080")
P('Game/driver2.h', "#define ENABLE_GAME_ENCHANCEMENTS\t1\n#define ENABLE_BONUS_CONTENT\t\t1",
  "#ifdef D2_PSX_FIDELITY\n#define ENABLE_GAME_ENCHANCEMENTS\t0\n#define ENABLE_BONUS_CONTENT\t\t0\n#else\n#define ENABLE_GAME_ENCHANCEMENTS\t1\n#define ENABLE_BONUS_CONTENT\t\t1\n#endif")
P('Game/C/cars.c', "#ifndef PSX\n#define CAR_LOD_SWITCH_DISTANCE switch_detail_distance",
  "#if !defined(PSX) && !defined(D2_PSX_FIDELITY)\n#define CAR_LOD_SWITCH_DISTANCE switch_detail_distance")
P('Game/C/main.c', "#ifndef PSX\n\tint screenW, screenH;\n\tPsyX_GetScreenSize(&screenW, &screenH);",
  "#if !defined(PSX) && !defined(D2_PSX_FIDELITY)\n\tint screenW, screenH;\n\tPsyX_GetScreenSize(&screenW, &screenH);")
P('Game/C/main.c', "#ifdef PSX\n\t\tsetXYWH(poly, 0, 0, 320, 256);",
  "#if defined(PSX) || defined(D2_PSX_FIDELITY)\n\t\tsetXYWH(poly, 0, 0, 320, 256);")
P('Game/C/debris.c', "#ifndef PSX\n\tx = (poly->x0 + poly->x3) * 0.5f;",
  "#if !defined(PSX) && !defined(D2_PSX_FIDELITY)\n\tx = (poly->x0 + poly->x3) * 0.5f;")
P('Game/C/debris.c', "#ifdef PSX\n\t\t\tint dx, dy;",
  "#if defined(PSX) || defined(D2_PSX_FIDELITY)\n\t\t\tint dx, dy;")
P('Game/C/debris.c', "#ifdef PSX\n\t\tpolys->x0 = 0;\n\t\tpolys->w = 320;",
  "#if defined(PSX) || defined(D2_PSX_FIDELITY)\n\t\tpolys->x0 = 0;\n\t\tpolys->w = 320;")
P('Game/Frontend/FEmain.c', "#ifndef PSX\n\t\t// replay theater",
  "#if !defined(PSX) && !defined(D2_PSX_FIDELITY)\n\t\t// replay theater")
P('Game/C/main.c', "\t// [A] Driver 1 music support\n#ifndef PSX",
  "\t// [A] Driver 1 music support\n#if !defined(PSX) && !defined(D2_PSX_FIDELITY)")
P('Game/C/main.c', "#ifdef PSX\n\t\tCloseShutters(2, 320, 512);",
  "#if defined(PSX) || defined(D2_PSX_FIDELITY)\n\t\tCloseShutters(2, 320, 512);")
P('Game/C/event.c', "#ifdef PSX\n#define EVENT_DRAW_DISTANCE VIEW_DRAW_DISTANCE",
  "#if defined(PSX) || defined(D2_PSX_FIDELITY)\n#define EVENT_DRAW_DISTANCE VIEW_DRAW_DISTANCE")
P('Game/Frontend/FEmain.c', "#ifndef PSX\n\t\t// [A] SCREEN HACKS",
  "#if !defined(PSX) && !defined(D2_PSX_FIDELITY)\n\t\t// [A] SCREEN HACKS")
P('Game/C/camera.c', "\t\t\t// [A] default just adds 248, but it's too close for big vehicles\n\t\t\taddDist = MAX(248, car_cos->colBox.vy * 3);",
  "\t\t\t// PS1 usa a distancia fixa; o PC a aumentava para veiculos grandes\n#ifdef D2_PSX_FIDELITY\n\t\t\taddDist = 248;\n#else\n\t\t\taddDist = MAX(248, car_cos->colBox.vy * 3);\n#endif")

# ---- registradores do GTE: unions/structs assumem LE (low half = primeiro campo). Em BE a ordem dos campos precisa ser invertida ----
_GTE_BE = "#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__\n#define D2_GTE_BE 1\n#endif\n"
R('PsyCross/include/psx/gtereg.h', r'(?<!#endif\n)#ifdef Status \r?\n', _GTE_BE.replace('\\','\\\\')+"#ifdef Status \n")
R('PsyCross/include/psx/gtereg.h', r'(?:#if defined\(__BYTE_ORDER__\) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__\n#define D2_GTE_BE 1\n#endif\n){2,}', _GTE_BE.replace('\\','\\\\'))
R('PsyCross/include/psx/gtereg.h', r'\tstruct \{ unsigned char l, h, h2, h3; \} b;', "#ifdef D2_GTE_BE\n\tstruct { unsigned char h3, h2, h, l; } b;\n#else\n\tstruct { unsigned char l, h, h2, h3; } b;\n#endif")
R('PsyCross/include/psx/gtereg.h', r'\tstruct \{ unsigned short l, h; \} w;', "#ifdef D2_GTE_BE\n\tstruct { unsigned short h, l; } w;\n#else\n\tstruct { unsigned short l, h; } w;\n#endif")
R('PsyCross/include/psx/gtereg.h', r'\tstruct \{ char l, h, h2, h3; \} sb;', "#ifdef D2_GTE_BE\n\tstruct { char h3, h2, h, l; } sb;\n#else\n\tstruct { char l, h, h2, h3; } sb;\n#endif")
R('PsyCross/include/psx/gtereg.h', r'\tstruct \{ short l, h; \} sw;', "#ifdef D2_GTE_BE\n\tstruct { short h, l; } sw;\n#else\n\tstruct { short l, h; } sw;\n#endif")
R('PsyCross/include/psx/gtereg.h', r'typedef struct \{\r?\n\tunsigned char r, g, b, c;\r?\n\} CBGR;', "typedef struct {\n#ifdef D2_GTE_BE\n\tunsigned char c, b, g, r;\n#else\n\tunsigned char r, g, b, c;\n#endif\n} CBGR;")
R('PsyCross/include/psx/gtereg.h', r'typedef struct \{\r?\n\tshort x, y, z, pad;\r?\n\} SVector3D;', "typedef struct {\n#ifdef D2_GTE_BE\n\tshort y, x, pad, z;\n#else\n\tshort x, y, z, pad;\n#endif\n} SVector3D;")
R('PsyCross/include/psx/gtereg.h', r'typedef struct \{\r?\n\tshort x, y;\r?\n\} SVector2D;', "typedef struct {\n#ifdef D2_GTE_BE\n\tshort y, x;\n#else\n\tshort x, y;\n#endif\n} SVector2D;")
R('PsyCross/include/psx/gtereg.h', r'typedef struct \{\r?\n\tshort z, pad;\r?\n\} SVector2Dz;', "typedef struct {\n#ifdef D2_GTE_BE\n\tshort pad, z;\n#else\n\tshort z, pad;\n#endif\n} SVector2Dz;")
R('PsyCross/include/psx/gtereg.h', r'typedef struct \{\r?\n\tshort m11, m12, m13, m21, m22, m23, m31, m32, m33, pad;\r?\n\} SMatrix3D;', "typedef struct {\n#ifdef D2_GTE_BE\n\tshort m12, m11, m21, m13, m23, m22, m32, m31, pad, m33;\n#else\n\tshort m11, m12, m13, m21, m22, m23, m31, m32, m33, pad;\n#endif\n} SMatrix3D;")


# ---- cores empacotadas em variaveis int (nao em memoria de CVECTOR): carregar o valor direto no registrador RGBC ----
R('Game/C/cars.c', r'gte_ldrgb\(&(FT3rgb|GT3rgb|lightlevel)\)', r'MTC2(\1, 6)')

# ---- caminho direto OT -> RDP (port/n64/psyx_rdp.cpp, D2_DIRECT): poligonos F/G/FT/GT 3/4 vao direto ao RDP, sem GrVertex/lotes ----
P('PsyCross/src/gpu/PsyX_GPU.cpp',
  "static int ProcessFlatPoly(P_TAG* polyTag)\n{\n",
  "static int ProcessFlatPoly(P_TAG* polyTag)\n{\n#ifdef D2_DIRECT\n\t{ const int dl = d2_direct_poly(polyTag); if (dl) return dl; }\n#endif\n")
P('PsyCross/src/gpu/PsyX_GPU.cpp',
  "static int ProcessGouraudPoly(P_TAG* polyTag)\n{\n",
  "static int ProcessGouraudPoly(P_TAG* polyTag)\n{\n#ifdef D2_DIRECT\n\t{ const int dl = d2_direct_poly(polyTag); if (dl) return dl; }\n#endif\n")
P('PsyCross/src/gpu/PsyX_GPU.cpp',
  "// forward declarations\nint ParsePrimitive(P_TAG* polyTag);\n",
  "// N64 (caminho direto): desenha os lotes pendentes antes de um poligono direto, para manter a ordem da OT\nextern \"C\" int d2_direct_poly(P_TAG* tag);\nextern \"C\" void d2_flush_splits(void)\n{\n\tif (g_splitIndex <= 0 || g_vertexIndex <= 0) return;\n\tGPUDrawSplit& l = g_splits[g_splitIndex];\n\tl.numVerts = g_vertexIndex - l.startVertex;\n\tDrawAllSplits();\n}\n\n// forward declarations\nint ParsePrimitive(P_TAG* polyTag);\n")

# ---- GTE nativo (port/compat/d2_gte_inline.h, D2_GTE_NATIVE): o macro doCOP2 nao pode renomear a definicao da funcao ----
P('PsyCross/src/psx/INLINE_C.C', "#undef CTC2\n", "#undef CTC2\n#undef doCOP2\n")

# ---- Fase 1.1 (port/n64/d2_vtxcache.cpp, D2_VTXCACHE): versoes com cache de vertices substituem estas; as originais ficam com _orig ----
P('Game/C/tile.c', "void Tile1x1Lit(MODEL* model)\n{", "void Tile1x1Lit(MODEL* model);\nvoid drawMeshLit(MVERTEX(*VSP)[5][5], int m, int n, _pct* pc);\nvoid D2_VC_ORIG(Tile1x1Lit)(MODEL* model)\n{")
P('Game/C/tile.c', "void Tile1x1(MODEL *model)\n{", "void D2_VC_ORIG(Tile1x1)(MODEL *model)\n{")
P('Game/C/tile.c', "void drawMesh(MVERTEX(*VSP)[5][5], int m, int n, _pct *pc)\n{", "void D2_VC_ORIG(drawMesh)(MVERTEX(*VSP)[5][5], int m, int n, _pct *pc)\n{")
P('Game/C/tile.c', "void drawMeshLit(MVERTEX(*VSP)[5][5], int m, int n, _pct* pc)\n{", "void D2_VC_ORIG(drawMeshLit)(MVERTEX(*VSP)[5][5], int m, int n, _pct* pc)\n{")
P('Game/C/draw.c', "void PlotBuildingModel(MODEL* model, int rot, _pct* pc)\n{", "void PlotBuildingModel(MODEL* model, int rot, _pct* pc);\nvoid PlotBuildingModelSubdivNxN(MODEL* model, int rot, _pct* pc, int n);\nvoid D2_VC_ORIG(PlotBuildingModel)(MODEL* model, int rot, _pct* pc)\n{")
P('Game/C/draw.c', "void PlotBuildingModelSubdivNxN(MODEL* model, int rot, _pct* pc, int n)\n{", "void D2_VC_ORIG(PlotBuildingModelSubdivNxN)(MODEL* model, int rot, _pct* pc, int n)\n{")

# ---- buffer de vertices e splits menores: nunca escrever alem dele; economiza ~305 KB de RAM ----
P('PsyCross/src/gpu/PsyX_GPU.cpp', "#define MAX_DRAW_SPLITS 1024", "#define MAX_DRAW_SPLITS 16")
P('PsyCross/src/gpu/PsyX_GPU.cpp',
  "int ParsePrimitive(P_TAG* polyTag)\n{",
  "extern \"C\" int d2_native_ft4(POLY_FT4*);\nextern \"C\" int d2_native_rect(P_TAG*);\nextern \"C\" int d2_native_line(P_TAG*);\nextern \"C\" int d2_native_drawenv(P_TAG*);\nextern \"C\" int d2_native_psyx(P_TAG*);\nextern \"C\" int d2_native_vram_packet(P_TAG*);\nint ParsePrimitive(P_TAG* polyTag)\n{\n#ifdef D2_DIRECT\n\tif (polyTag->code == 0xB3) return d2_native_ft4((POLY_FT4*)polyTag);\n\tif ((polyTag->code & 0xF0) == 0x00 || (polyTag->code & 0xF0) == 0xA0) { int n=d2_native_vram_packet(polyTag); if (n) return n; }\n\tif ((polyTag->code & 0xF0) == 0x20 || (polyTag->code & 0xF0) == 0x30) { int n = d2_direct_poly(polyTag); if (n) return n; }\n\tif ((polyTag->code & 0xF0) == 0x40 || (polyTag->code & 0xF0) == 0x50) { int n = d2_native_line(polyTag); if (n) return n; }\n\tif ((polyTag->code & 0xF0) == 0x60 || (polyTag->code & 0xF0) == 0x70) { int n = d2_native_rect(polyTag); if (n) return n; }\n\tif ((polyTag->code & 0xF0) == 0xB0) { int n = d2_native_psyx(polyTag); if (n) return n; }\n\tif ((polyTag->code & 0xF0) == 0xE0) return d2_native_drawenv(polyTag);\n\tif (g_vertexIndex > MAX_VERTEX_BUFFER_SIZE - 64) d2_flush_splits();\n#endif")

P('PsyCross/src/gpu/PsyX_GPU.cpp',
  "void ParsePrimitivesLinkedList(u_long* p, int singlePrimitive)\n{",
  "extern \"C\" void d2_native_draw_ot(u_long* p);\nvoid ParsePrimitivesLinkedList(u_long* p, int singlePrimitive)\n{\n#ifdef D2_NATIVE_OT\n\tif (!singlePrimitive) { d2_native_draw_ot(p); return; }\n#endif")

# ---- Fase 1.3: OT enxuta (D2_FASTOT) ----
# ClearOTagR: duas palavras por entrada (endereco da anterior + len/pgxp zerados) em vez de escrita de campo de bits
P('PsyCross/src/psx/LIBGPU.C',
  "\t// initialize a linked list with it's previous items\n\tfor (int i = 1; i < n; ++i)\n\t{\n\t\tsetaddr(&ptag_list[i], &ptag_list[i-1]);\n\t\tsetlen(&ptag_list[i], 0);\n\t}\n",
  "\t// initialize a linked list with it's previous items\n#if defined(D2_FASTOT)\n\tstatic_assert(sizeof(OT_TAG) == 8, \"OT_TAG de 8 bytes\");\n\t{\n\t\tunsigned int* w = (unsigned int*)ptag_list;\n\t\tint i = 1;\n\t\t// linhas de cache inteiras (16 bytes = 2 entradas): cria a linha sem ler a RAM (Create Dirty Exclusive) e escreve\n\t\tfor (; i < n && (((uintptr_t)&w[2 * i]) & 15); ++i) { w[2 * i] = (unsigned int)(uintptr_t)&ptag_list[i - 1]; w[2 * i + 1] = 0; }\n\t\tfor (; i + 1 < n; i += 2) {\n\t\t\t__asm__ volatile(\"cache 0x0d, 0(%0)\" :: \"r\"(&w[2 * i]) : \"memory\");\n\t\t\tw[2 * i] = (unsigned int)(uintptr_t)&ptag_list[i - 1]; w[2 * i + 1] = 0; w[2 * i + 2] = (unsigned int)(uintptr_t)&ptag_list[i]; w[2 * i + 3] = 0;\n\t\t}\n\t\tfor (; i < n; ++i) { w[2 * i] = (unsigned int)(uintptr_t)&ptag_list[i - 1]; w[2 * i + 1] = 0; }\n\t}\n#else\n\tfor (int i = 1; i < n; ++i)\n\t{\n\t\tsetaddr(&ptag_list[i], &ptag_list[i-1]);\n\t\tsetlen(&ptag_list[i], 0);\n\t}\n#endif\n")
# percurso da OT: so atualiza o lote quando a entrada tinha primitivos (a maioria das entradas e vazia)
P('PsyCross/src/gpu/PsyX_GPU.cpp',
  "\t\t\t}\n\n\t\t\tGPUDrawSplit& lastSplit = g_splits[g_splitIndex];\n\t\t\tlastSplit.numVerts = g_vertexIndex - lastSplit.startVertex;\n\n\t\t\tif (isendprim(basePacket))",
  "#ifdef D2_FASTOT\n\t\t\t\tGPUDrawSplit& lastSplit = g_splits[g_splitIndex];\n\t\t\t\tlastSplit.numVerts = g_vertexIndex - lastSplit.startVertex;\n\t\t\t}\n#else\n\t\t\t}\n\n\t\t\tGPUDrawSplit& lastSplit = g_splits[g_splitIndex];\n\t\t\tlastSplit.numVerts = g_vertexIndex - lastSplit.startVertex;\n#endif\n\n\t\t\tif (isendprim(basePacket))")

# ---- Diagnostico de origem de primitivas (D2_ORIGIN) ----
P('PsyCross/include/psx/libgpu.h',
  "#else\n#define addPrim(ot, p)\t\tsetaddr(p, getaddr(ot)), setaddr(ot, p)\n#endif\n\n#define addPrims",
  "#else\n#ifdef D2_ORIGIN\n#define addPrim(ot, p)\t\t(D2_ORIGIN_TAG_AUTO(p), setaddr(p, getaddr(ot)), setaddr(ot, p))\n#else\n#define addPrim(ot, p)\t\tsetaddr(p, getaddr(ot)), setaddr(ot, p)\n#endif\n#endif\n\n#define addPrims")

P('Game/C/draw.c',
  "model = Z > DRAW_LOD_DIST_LOW ? pLodModels[cop->type] : modelpointers[cop->type];",
  "D2_ORIGIN_SET_SCOPE(\"PlotBuildingModel\");\n\t\tD2_ORIGIN_SET_CELL(cop->type, cop->pos.vx, cop->pos.vy, cop->pos.vz);\n\t\tmodel = Z > DRAW_LOD_DIST_LOW ? pLodModels[cop->type] : modelpointers[cop->type];")

P('Game/C/draw.c',
  "numShadows = 0;\n\n\twhile (numFound--)",
  "numShadows = 0;\n\tD2_ORIGIN_SET_SCOPE(\"DrawSprites\");\n\n\twhile (numFound--)")

P('Game/C/draw.c',
  "z = Apply_InvCameraMatrixAndSetMatrix((VECTOR_NOPAD*)plotContext.scribble, (MATRIX2*)&face_camera);",
  "D2_ORIGIN_SET_CELL(modelnumber, pco->pos.vx, pco->pos.vy, pco->pos.vz);\n\t\tz = Apply_InvCameraMatrixAndSetMatrix((VECTOR_NOPAD*)plotContext.scribble, (MATRIX2*)&face_camera);")

P('Game/C/draw.c',
  "void RenderModel(MODEL* model, MATRIX* matrix, VECTOR* pos, int zBias, int flags, int subdiv, int nrot)\n{",
  "void RenderModel(MODEL* model, MATRIX* matrix, VECTOR* pos, int zBias, int flags, int subdiv, int nrot)\n{\n\tD2_ORIGIN_SET_SCOPE(\"RenderModel\");\n\tD2_ORIGIN_SET_CELL(-1, pos ? pos->vx : 0, pos ? pos->vy : 0, pos ? pos->vz : 0);")

P('Game/C/draw.c',
  "void DrawAllTheCars(int view)\n{",
  "void DrawAllTheCars(int view)\n{\n\tD2_ORIGIN_SET_SCOPE(\"DrawCars\");")

P('Game/C/tile.c',
  "tilePointers = (PACKED_CELL_OBJECT **)tiles;\n\n\twhile (tile_amount--)",
  "tilePointers = (PACKED_CELL_OBJECT **)tiles;\n\tD2_ORIGIN_SET_SCOPE(\"DrawTILES\");\n\n\twhile (tile_amount--)")

P('Game/C/tile.c',
  "model_number = (ppco->value >> 6) | (ppco->pos.vy & 1) << 10;\n\n\t\tif (previous_matrix == yang)",
  "model_number = (ppco->value >> 6) | (ppco->pos.vy & 1) << 10;\n\t\tD2_ORIGIN_SET_CELL(model_number, ppco->pos.vx, ppco->pos.vy, ppco->pos.vz);\n\n\t\tif (previous_matrix == yang)")

P('Game/C/overlay.c',
  "void DrawPercentageBar(PERCENTAGE_BAR *bar)\n{",
  "void DrawPercentageBar(PERCENTAGE_BAR *bar)\n{\n\tD2_ORIGIN_SET_SCOPE(\"overlay/HUD/Bar\");")

P('Game/C/overlay.c',
  "\t\tpoly = (POLY_G4 *)current->primptr;\n\t\tsetPolyG4(poly);",
  "\t\tpoly = (POLY_G4 *)current->primptr;\n#ifdef D2_ORIGIN\n\t\tD2_ORIGIN_TAG(poly, \"overlay/HUD/Bar/Fill\", -1, NULL, 0x38, NULL, NULL);\n#endif\n\t\tsetPolyG4(poly);")

P('Game/C/overlay.c',
  "\tpoly2 = (POLY_G4 *)current->primptr;\n\n\tsetPolyG4(poly2);",
  "\tpoly2 = (POLY_G4 *)current->primptr;\n#ifdef D2_ORIGIN\n\t\tD2_ORIGIN_TAG(poly2, \"overlay/HUD/Bar/Trans\", -1, NULL, 0x38, NULL, NULL);\n#endif\n\n\tsetPolyG4(poly2);")

P('Game/C/overlay.c',
  "\tLINE_F4* lineF4 = (LINE_F4*)current->primptr;\n\tsetLineF4(lineF4);",
  "\tLINE_F4* lineF4 = (LINE_F4*)current->primptr;\n#ifdef D2_ORIGIN\n\t\tD2_ORIGIN_TAG(lineF4, \"overlay/HUD/Bar/Line4\", -1, NULL, 0x4C, NULL, NULL);\n#endif\n\tsetLineF4(lineF4);")

P('Game/C/overlay.c',
  "\tLINE_F2* lineF2 = (LINE_F2*)current->primptr;\n\tsetLineF2(lineF2);",
  "\tLINE_F2* lineF2 = (LINE_F2*)current->primptr;\n#ifdef D2_ORIGIN\n\t\tD2_ORIGIN_TAG(lineF2, \"overlay/HUD/Bar/Line2\", -1, NULL, 0x40, NULL, NULL);\n#endif\n\tsetLineF2(lineF2);")

P('Game/C/overlay.c',
  "\tnull = (DR_TPAGE*)current->primptr;\n\tsetDrawTPage(null, 1, 0, tpage);",
  "\tnull = (DR_TPAGE*)current->primptr;\n#ifdef D2_ORIGIN\n\t\tD2_ORIGIN_TAG(null, \"overlay/HUD/TransparencyOn\", -1, NULL, 0xE1, NULL, NULL);\n#endif\n\tsetDrawTPage(null, 1, 0, tpage);")

P('Game/C/overlay.c',
  "void DisplayOverlays(void)\n{",
  "void DisplayOverlays(void)\n{\n\tD2_ORIGIN_SET_SCOPE(\"overlay/HUD\");")

P('Game/C/shadow.c',
  "void PlaceShadowForCar(VECTOR *shadowPoints, int subdiv, int zOfs, int flag)\n{",
  "void PlaceShadowForCar(VECTOR *shadowPoints, int subdiv, int zOfs, int flag)\n{\n\tD2_ORIGIN_SET_SCOPE(\"shadow\");")

P('Game/C/debris.c',
  "void HandleDebris(void)\n{",
  "void HandleDebris(void)\n{\n\tD2_ORIGIN_SET_SCOPE(\"debris\");")

P('Game/C/cosmetic.c',
  "void AddBrakeLight(CAR_DATA *cp)\n{",
  "void AddBrakeLight(CAR_DATA *cp)\n{\n\tD2_ORIGIN_SET_SCOPE(\"lights/BrakeLight\");")

P('Game/C/cosmetic.c',
  "void AddNightLights(CAR_DATA *cp)\n{",
  "void AddNightLights(CAR_DATA *cp)\n{\n\tD2_ORIGIN_SET_SCOPE(\"lights/NightLights\");")

P('Game/C/debris.c',
  "void PlacePoolForCar(CAR_DATA *cp, CVECTOR *col, int front, int in_car)\n{",
  "void PlacePoolForCar(CAR_DATA *cp, CVECTOR *col, int front, int in_car)\n{\n\tD2_ORIGIN_SET_SCOPE(front ? \"lights/PoolFront\" : \"lights/PoolBack\");")

P('Game/C/debris.c',
  "void ShowCarlight(SVECTOR *v1, CAR_DATA *cp, CVECTOR *col, short size, short flare_size, TEXTURE_DETAILS *texture,int flag)\n{",
  "void ShowCarlight(SVECTOR *v1, CAR_DATA *cp, CVECTOR *col, short size, short flare_size, TEXTURE_DETAILS *texture,int flag)\n{\n\tD2_ORIGIN_SET_SCOPE(\"lights/Carlight\");")

P('Game/C/debris.c',
  "void ShowLight(VECTOR *v1, CVECTOR *col, short size, TEXTURE_DETAILS *texture)\n{",
  "void ShowLight(VECTOR *v1, CVECTOR *col, short size, TEXTURE_DETAILS *texture)\n{\n\tD2_ORIGIN_SET_SCOPE(\"lights/ShowLight\");")

P('Game/C/debris.c',
  "void ShowFlare(VECTOR* v1, CVECTOR* col, short size, int rotation)\n{",
  "void ShowFlare(VECTOR* v1, CVECTOR* col, short size, int rotation)\n{\n\tD2_ORIGIN_SET_SCOPE(\"lights/ShowFlare\");")

P('Game/C/debris.c',
  "void ShowGroundLight(VECTOR *v1, CVECTOR *col, short size)\n{",
  "void ShowGroundLight(VECTOR *v1, CVECTOR *col, short size)\n{\n\tD2_ORIGIN_SET_SCOPE(\"lights/GroundLight\");")

P('Game/C/debris.c',
  "void AddTrafficLight(CELL_OBJECT *cop, int x, int y, int z, int flag, int yang)\n{",
  "void AddTrafficLight(CELL_OBJECT *cop, int x, int y, int z, int flag, int yang)\n{\n\tD2_ORIGIN_SET_SCOPE(\"lights/TrafficLight\");")

P('Game/C/debris.c',
  "void DisplaySmoke(SMOKE* smoke)\n{",
  "void DisplaySmoke(SMOKE* smoke)\n{\n\tD2_ORIGIN_SET_SCOPE(\"debris/Smoke\");")

P('Game/C/debris.c',
  "void DisplaySpark(SMOKE *spark)\n{",
  "void DisplaySpark(SMOKE *spark)\n{\n\tD2_ORIGIN_SET_SCOPE(\"debris/Spark\");")

P('Game/C/shadow.c',
  "void sQuad(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, SVECTOR *v3, CVECTOR* light_col, int LightSortCorrect)\n{",
  "void sQuad(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, SVECTOR *v3, CVECTOR* light_col, int LightSortCorrect)\n{\n\tD2_ORIGIN_SET_SCOPE(\"lights/HeadlightCone\");")

P('Game/C/sky.c',
  "void DrawSkyDome(void)\n{",
  "void DrawSkyDome(void)\n{\n\tD2_ORIGIN_SET_SCOPE(\"sky\");")

P('Game/C/pres.c',
  "int PrintStringHires(char* string, int x, int y)\n{",
  "int PrintStringHires(char* string, int x, int y)\n{\n\tD2_ORIGIN_SET_SCOPE(\"text/pres\");")

P('Game/C/targets.c',
  "void DrawTargetArrowModel(TARGET_ARROW_MODEL *pTargetArrowModel, VECTOR *pPosition, int shadow, int invert)\n{",
  "void DrawTargetArrowModel(TARGET_ARROW_MODEL *pTargetArrowModel, VECTOR *pPosition, int shadow, int invert)\n{\n\tD2_ORIGIN_SET_SCOPE(\"DrawTargetArrow\");\n\tD2_ORIGIN_SET_CELL(999, pPosition ? pPosition->vx : 0, pPosition ? pPosition->vy : 0, pPosition ? pPosition->vz : 0);")

P('Game/C/targets.c',
  "\t\ttri_indices += 3;\n\t\tgte_rtpt();\n\n\t\tsetPolyF3(poly);",
  "\t\ttri_indices += 3;\n\t\tgte_rtpt();\n\t\tconst unsigned int _h = (unsigned short)C2_H;\n\t\tif (2 * (unsigned int)C2_SZ1 <= _h || 2 * (unsigned int)C2_SZ2 <= _h || 2 * (unsigned int)C2_SZ3 <= _h)\n\t\t\tcontinue;\n\n\t\tsetPolyF3(poly);")

P('Game/C/targets.c',
  "\t\taddPrim(current->ot + z, poly);\n\n\t\tcurrent->primptr += sizeof(POLY_F3);",
  "\t\t#ifdef D2_ORIGIN\n\t\tshort _sxy[4][2] = { {poly->x0, poly->y0}, {poly->x1, poly->y1}, {poly->x2, poly->y2}, {0, 0} };\n\t\tshort _sz[4] = { (short)MFC2(17), (short)MFC2(18), (short)MFC2(19), 0 };\n\t\td2_origin_tag_cull(poly, \"DrawTargetArrow\", 999, NULL, 0x20, _sxy, _sz, 0, z);\n\t\t#endif\n\t\taddPrim(current->ot + z, poly);\n\n\t\tcurrent->primptr += sizeof(POLY_F3);")

P('Game/Frontend/FEmain.c',
  "wantedTimeOfDay = TIME_DAY;",
  "#if defined(D2_TRANSDBG) || defined(D2_PERF_NIGHT)\n\t\twantedTimeOfDay = TIME_NIGHT;\n#else\n\t\twantedTimeOfDay = TIME_DAY;\n#endif")

P('Game/C/mission.c',
  "\tif(wantedTimeOfDay > -1)\n\t\tgTimeOfDay = wantedTimeOfDay;",
  "#if defined(D2_TRANSDBG) || defined(D2_PERF_NIGHT)\n\tgTimeOfDay = TIME_NIGHT;\n#else\n\tif(wantedTimeOfDay > -1)\n\t\tgTimeOfDay = wantedTimeOfDay;\n#endif")


# ---- D2_PERFDBG: instrumentacao de desempenho ----
P('Game/C/system.c',
  "\tPutDispEnv(&current->disp);\n\tPutDrawEnv(&current->draw);\n\tDrawOTag((u_long*)(current->ot + OTSIZE-1));\n",
  "\tPutDispEnv(&current->disp);\n\tPutDrawEnv(&current->draw);\n#ifdef D2_PERFDBG\n\tPERF_START(bk);\n\tDrawOTag((u_long*)(current->ot + OTSIZE-1));\n\tPERF_END_ADD(bk, backend_ticks);\n#else\n\tDrawOTag((u_long*)(current->ot + OTSIZE-1));\n#endif\n")

P('Game/C/main.c',
  "\tif (!FilterFrameTime())\n\t\treturn;\n\n\tUpdatePadData();",
  "#ifdef D2_PERFDBG\n\tPERF_START(vbl);\n\twhile (!FilterFrameTime())\n\t{\n\t\td2_audio_pump();\n\t}\n\tPERF_END_ADD(vbl, vblank_wait_ticks);\n\tperf_frame_start();\n#else\n\tif (!FilterFrameTime())\n\t\treturn;\n#endif\n\n\tUpdatePadData();")

P('Game/C/main.c',
  "\tcnt = FastForward ? 7 : 1;\n\twhile (--cnt >= 0)\n\t\tStepGame();",
  "\tcnt = FastForward ? 7 : 1;\n#ifdef D2_PERFDBG\n\tPERF_START(step);\n\twhile (--cnt >= 0)\n\t\tStepGame();\n\tPERF_END_ADD(step, stepgame_ticks);\n#else\n\twhile (--cnt >= 0)\n\t\tStepGame();\n#endif")

P('Game/C/main.c',
  "\t_CutRec_Draw();\n\tDrawGame();\n\t\n\tif (game_over)",
  "\t_CutRec_Draw();\n\tDrawGame();\n#ifdef D2_PERFDBG\n\tperf_frame_end();\n#endif\n\t\n\tif (game_over)")

P('Game/C/main.c',
  "\t\tObjectDrawnValue = FrameCnt;\n\t\tDrawPauseMenus();\n\n\t\tRenderGame2(0);\n\t\tSwapDrawBuffers();",
  "\t\tObjectDrawnValue = FrameCnt;\n\t\tDrawPauseMenus();\n\n#ifdef D2_PERFDBG\n\t\tPERF_START(draw);\n\t\tRenderGame2(0);\n\t\tPERF_END_ADD(draw, drawgame_ticks);\n\t\tSwapDrawBuffers();\n#else\n\t\tRenderGame2(0);\n\t\tSwapDrawBuffers();\n#endif")

P('Game/C/main.c',
  "\tif (doSpooling)\n\t{\n\t\tCheckValidSpoolData();\n\t\tControlMap();\n\t}",
  "\tif (doSpooling)\n\t{\n#ifdef D2_PERFDBG\n\t\tPERF_START(spool);\n\t\tCheckValidSpoolData();\n\t\tControlMap();\n\t\tPERF_END_ADD(spool, step_spool_ticks);\n#else\n\t\tCheckValidSpoolData();\n\t\tControlMap();\n#endif\n\t}")

P('Game/C/main.c',
  "\tif (gInGameCutsceneActive == 0)\n\t{\n\t\tCheckSpecialSpool();\n\t}",
  "\tif (gInGameCutsceneActive == 0)\n\t{\n#ifdef D2_PERFDBG\n\t\tPERF_START(s_spool);\n\t\tCheckSpecialSpool();\n\t\tPERF_END_ADD(s_spool, step_spool_ticks);\n#else\n\t\tCheckSpecialSpool();\n#endif\n\t}")

P('Game/C/main.c',
  "\t\t\tcase CONTROL_TYPE_CIV_AI:\n\t\t\t\tCivControl(cp);\n\t\t\t\tbreak;\n\t\t\tcase CONTROL_TYPE_PURSUER_AI:\n\t\t\t\tCopControl(cp);\n\t\t\t\tbreak;",
  "\t\t\tcase CONTROL_TYPE_CIV_AI:\n#ifdef D2_PERFDBG\n\t\t\t\t{\n\t\t\t\t\tPERF_START(cai);\n\t\t\t\t\tCivControl(cp);\n\t\t\t\t\tPERF_END_ADD(cai, step_ai_ticks);\n\t\t\t\t}\n#else\n\t\t\t\tCivControl(cp);\n#endif\n\t\t\t\tbreak;\n\t\t\tcase CONTROL_TYPE_PURSUER_AI:\n#ifdef D2_PERFDBG\n\t\t\t\t{\n\t\t\t\t\tPERF_START(pai);\n\t\t\t\t\tCopControl(cp);\n\t\t\t\t\tPERF_END_ADD(pai, step_ai_ticks);\n\t\t\t\t}\n#else\n\t\t\t\tCopControl(cp);\n#endif\n\t\t\t\tbreak;")

P('Game/C/main.c',
  "\t\tStepCarPhysics(cp);\n\t\tcp++;",
  "#ifdef D2_PERFDBG\n\t\tPERF_START(phys);\n\t\tStepCarPhysics(cp);\n\t\tPERF_END_ADD(phys, step_cars_physics_ticks);\n#else\n\t\tStepCarPhysics(cp);\n#endif\n\t\tcp++;")

P('Game/C/main.c',
  "\t\tProcessTannerPad(pl->pPed, t0, t1, t2);\n\t}",
  "#ifdef D2_PERFDBG\n\t\tPERF_START(tpad);\n\t\tProcessTannerPad(pl->pPed, t0, t1, t2);\n\t\tPERF_END_ADD(tpad, step_pedestrians_ticks);\n#else\n\t\tProcessTannerPad(pl->pPed, t0, t1, t2);\n#endif\n\t}")

P('Game/C/main.c',
  "\tif (!game_over)\n\t{\n\t\tif(pathAILoaded)\n\t\t\tControlCops();\n\n\t\tif (gLoadedMotionCapture)\n\t\t\tHandlePedestrians();\n\t}\n\n\tGlobalTimeStep();\n\tUpdatePlayers();\n\tDoScenaryCollisions();",
  "\tif (!game_over)\n\t{\n\t\tif(pathAILoaded)\n\t\t{\n#ifdef D2_PERFDBG\n\t\t\tPERF_START(cops);\n\t\t\tControlCops();\n\t\t\tPERF_END_ADD(cops, step_ai_ticks);\n#else\n\t\t\tControlCops();\n#endif\n\t\t}\n\n\t\tif (gLoadedMotionCapture)\n\t\t{\n#ifdef D2_PERFDBG\n\t\t\tPERF_START(hpeds);\n\t\t\tHandlePedestrians();\n\t\t\tPERF_END_ADD(hpeds, step_pedestrians_ticks);\n#else\n\t\t\tHandlePedestrians();\n#endif\n\t\t}\n\t}\n\n#ifdef D2_PERFDBG\n\tPERF_START(gtime);\n\tGlobalTimeStep();\n\tPERF_END_ADD(gtime, step_cars_physics_ticks);\n#else\n\tGlobalTimeStep();\n#endif\n\tUpdatePlayers();\n#ifdef D2_PERFDBG\n\tPERF_START(coll);\n\tDoScenaryCollisions();\n\tPERF_END_ADD(coll, step_collision_ticks);\n#else\n\tDoScenaryCollisions();\n#endif")

P('Game/C/main.c',
  "\tif (gLoadedMotionCapture != 0)\n\t\tDrawAllPedestrians();",
  "\tif (gLoadedMotionCapture != 0)\n\t{\n#ifdef D2_PERFDBG\n\t\tPERF_START(ped);\n\t\tDrawAllPedestrians();\n\t\tPERF_END_ADD(ped, draw_pedestrians_ticks);\n#else\n\t\tDrawAllPedestrians();\n#endif\n\t}")

P('Game/C/main.c',
  "\tDrawSmashable_sprites();\n\tHandleDebris();\n\n\tcurrent->ot -= 10;\n\n\tDrawAllExplosions();",
  "#ifdef D2_PERFDBG\n\tPERF_START(fx);\n#endif\n\tDrawSmashable_sprites();\n\tHandleDebris();\n\n\tcurrent->ot -= 10;\n\n\tDrawAllExplosions();\n#ifdef D2_PERFDBG\n\tPERF_END_ADD(fx, draw_effects_ticks);\n#endif")

P('Game/C/main.c',
  "\tif (gLoadedOverlay)\n\t\tDisplayOverlays();",
  "\tif (gLoadedOverlay)\n\t{\n#ifdef D2_PERFDBG\n\t\tPERF_START(fx_ovl);\n\t\tDisplayOverlays();\n\t\tPERF_END_ADD(fx_ovl, draw_effects_ticks);\n#else\n\t\tDisplayOverlays();\n#endif\n\t}")

P('Game/C/main.c',
  "\tif (notInDreaAndStevesEvilLair)\n\t{\t\t\n\t\tDrawSkyDome();\n\n\t\tif (current->primtab - (current->primptr - PRIMTAB_SIZE) > 40000)\n\t\t{\n\t\t\tDoWeather(gWeather);\n\t\t}\n\t}\n\n\tif (current->primtab - (current->primptr - PRIMTAB_SIZE) > 37000)\n\t{\n\t\tDrawTyreTracks();\n\t}",
  "#ifdef D2_PERFDBG\n\tPERF_START(fx_env);\n#endif\n\tif (notInDreaAndStevesEvilLair)\n\t{\t\t\n\t\tDrawSkyDome();\n\n\t\tif (current->primtab - (current->primptr - PRIMTAB_SIZE) > 40000)\n\t\t{\n\t\t\tDoWeather(gWeather);\n\t\t}\n\t}\n\n\tif (current->primtab - (current->primptr - PRIMTAB_SIZE) > 37000)\n\t{\n\t\tDrawTyreTracks();\n\t}\n#ifdef D2_PERFDBG\n\tPERF_END_ADD(fx_env, draw_effects_ticks);\n#endif")

P('Game/C/draw.c',
  "\tif (drawData.sprites_found)\n\t\tDrawSprites((PACKED_CELL_OBJECT**)spriteList, drawData.sprites_found);\n\n\tif (drawData.tiles_found)\n\t\tDrawTILES((PACKED_CELL_OBJECT**)model_tile_ptrs, drawData.tiles_found);\n\n\tif (drawData.other_models_found)\n\t\tDrawAllBuildings((CELL_OBJECT**)model_object_ptrs, drawData.other_models_found);",
  "\tif (drawData.sprites_found)\n\t{\n#ifdef D2_PERFDBG\n\t\tPERF_START(spr);\n\t\tDrawSprites((PACKED_CELL_OBJECT**)spriteList, drawData.sprites_found);\n\t\tPERF_END_ADD(spr, draw_sprites_ticks);\n#else\n\t\tDrawSprites((PACKED_CELL_OBJECT**)spriteList, drawData.sprites_found);\n#endif\n\t}\n\n\tif (drawData.tiles_found)\n\t{\n#ifdef D2_PERFDBG\n\t\tPERF_START(tiles);\n\t\tDrawTILES((PACKED_CELL_OBJECT**)model_tile_ptrs, drawData.tiles_found);\n\t\tPERF_END_ADD(tiles, draw_ground_ticks);\n#else\n\t\tDrawTILES((PACKED_CELL_OBJECT**)model_tile_ptrs, drawData.tiles_found);\n#endif\n\t}\n\n\tif (drawData.other_models_found)\n\t{\n#ifdef D2_PERFDBG\n\t\tPERF_START(bld);\n\t\tDrawAllBuildings((CELL_OBJECT**)model_object_ptrs, drawData.other_models_found);\n\t\tPERF_END_ADD(bld, draw_buildings_ticks);\n#else\n\t\tDrawAllBuildings((CELL_OBJECT**)model_object_ptrs, drawData.other_models_found);\n#endif\n\t}")

P('Game/C/draw.c',
  "\t\t\tDrawCar(cars_to_draw[i], view);\n\t\t\t\n\t\t\tspacefree -= 2000;",
  "#ifdef D2_PERFDBG\n\t\t\tuint32_t _shd0 = g_perf_cur.draw_shadows_ticks;\n\t\t\tPERF_START(car);\n\t\t\tDrawCar(cars_to_draw[i], view);\n\t\t\tuint32_t _dcar = perf_cnt0() - _pt0_car;\n\t\t\tuint32_t _dshd = g_perf_cur.draw_shadows_ticks - _shd0;\n\t\t\tif (_dcar >= _dshd) g_perf_cur.draw_cars_ticks += (_dcar - _dshd);\n#else\n\t\t\tDrawCar(cars_to_draw[i], view);\n#endif\n\t\t\t\n\t\t\tspacefree -= 2000;")

P('Game/C/shadow.c',
  "void PlaceShadowForCar(VECTOR *shadowPoints, int subdiv, int zOfs, int flag)\n{\n\tD2_ORIGIN_SET_SCOPE(\"shadow\");",
  "void PlaceShadowForCar(VECTOR *shadowPoints, int subdiv, int zOfs, int flag)\n{\n\tD2_ORIGIN_SET_SCOPE(\"shadow\");\n#ifdef D2_PERFDBG\n\tPERF_START(shd);\n#endif")

P('Game/C/shadow.c',
  "\tdrawMesh((MVERTEX(*)[5][5])subdivVerts, subdiv, subdiv, &plotContext);\n\n\tcurrent->primptr = plotContext.primptr;\n}",
  "\tdrawMesh((MVERTEX(*)[5][5])subdivVerts, subdiv, subdiv, &plotContext);\n\n\tcurrent->primptr = plotContext.primptr;\n#ifdef D2_PERFDBG\n\tPERF_END_ADD(shd, draw_shadows_ticks);\n#endif\n}")

# ---- sombra do Tanner: render offscreen nao suportado no N64 ----
P('Game/C/motion_c.c',
  "\t\t\tTannerShadow(pPed, &v, moon_position + GameLevel, &cV, pPed->dir.vy);\n\t\t}\n\t\telse\n\t\t{\n\t\t\tcV.b = 32;\n\t\t\tcV.g = 32;\n\t\t\tcV.r = 32;\n\t\t\tTannerShadow(pPed, &v, sun_position + GameLevel, &cV, pPed->dir.vy);",
  "#ifndef D2_NO_TANNER_SHADOW\n\t\t\tTannerShadow(pPed, &v, moon_position + GameLevel, &cV, pPed->dir.vy);\n#endif\n\t\t}\n\t\telse\n\t\t{\n\t\t\tcV.b = 32;\n\t\t\tcV.g = 32;\n\t\t\tcV.r = 32;\n#ifndef D2_NO_TANNER_SHADOW\n\t\t\tTannerShadow(pPed, &v, sun_position + GameLevel, &cV, pPed->dir.vy);\n#endif")

P('Game/C/motion_c.c',
  "\t\t\tTannerShadow(pPed, &v, moon_position + GameLevel, &cV, pPed->dir.vy);\n\t\t}\n\t\telse\n\t\t{\n\t\t\tcV.b = cV.g = cV.r = 32;\n\t\t\tTannerShadow(pPed, &v, sun_position + GameLevel, &cV, pPed->dir.vy);",
  "#ifndef D2_NO_TANNER_SHADOW\n\t\t\tTannerShadow(pPed, &v, moon_position + GameLevel, &cV, pPed->dir.vy);\n#endif\n\t\t}\n\t\telse\n\t\t{\n\t\t\tcV.b = cV.g = cV.r = 32;\n#ifndef D2_NO_TANNER_SHADOW\n\t\t\tTannerShadow(pPed, &v, sun_position + GameLevel, &cV, pPed->dir.vy);\n#endif")

def main():
    ok = skipped = missing = 0
    cache = {}
    for path, old, new in PATCHES:
        fp = os.path.join(ROOT, path)
        if not os.path.exists(fp):
            print('patch_engine.py: arquivo ausente', path); missing += 1; continue
        s = cache.get(fp)
        if s is None: s = open(fp, encoding='latin-1', newline='').read()
        crlf = '\r\n' in s
        o, n = (old.replace('\n', '\r\n'), new.replace('\n', '\r\n')) if crlf else (old, new)
        if n in s:
            skipped += 1
        elif o in s:
            s = s.replace(o, n, 1); ok += 1
        else:
            print('patch_engine.py: trecho NAO encontrado em', path, '->', old[:60].replace('\n', '\\n')); missing += 1
        cache[fp] = s
    for path, rx, repl in REGEX:
        fp = os.path.join(ROOT, path)
        if not os.path.exists(fp):
            print('patch_engine.py: arquivo ausente', path); missing += 1; continue
        s = cache.get(fp)
        if s is None: s = open(fp, encoding='latin-1', newline='').read()
        if isinstance(rx, tuple):
            _, names, pat, rp = rx
            out = []; cnt = 0
            for blk in re.split(r'(\r?\n[ \t]*\r?\n)', s):
                m = re.match(r'(?s).*?#define[ \t]+(\w+)', blk)
                if m and m.group(1) in names:
                    blk, n = pat.subn(rp, blk); cnt += n
                out.append(blk)
            s = ''.join(out)
        else:
            s, cnt = rx.subn(repl, s)
        ok += 1 if cnt else 0
        cache[fp] = s
    for fp, s in cache.items():
        open(fp, 'w', encoding='latin-1', newline='').write(s)
    print('patch_engine.py: %d aplicados, %d ja aplicados, %d faltando' % (ok, skipped, missing))
    return 0
sys.exit(main())
