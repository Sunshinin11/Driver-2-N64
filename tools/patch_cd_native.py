"""Etapa 5: sem CD. O jogo le tudo por arquivo (DragonFS, UpdateSpoolPC / d2_read_sectors); o caminho de setores do CD
(CdRead, CdControl, callbacks de setor do spool e do loader) fica fora da compilacao."""

def register(P, R):
    R('Game/driver2.h', r'#define USE_CD_FILESYSTEM\t\t\t1\t// use always', '#define USE_CD_FILESYSTEM\t\t\t0\t/*d2cd*/ // sem CD no N64')

    # spool.c: callbacks de setor (WaitCloseLid ... changemode) so existem com CD
    P('Game/C/spool.c',
      "// [D] [T]\nvoid WaitCloseLid(void)\n{",
      "#if USE_CD_FILESYSTEM /*d2cd*/\n// [D] [T]\nvoid WaitCloseLid(void)\n{")
    P('Game/C/spool.c',
      "\n\n// [D] [T]\nvoid StartSpooling(void)\n{\n\tstatic u_char param[8];\n\tstatic u_char result[8];\n\t\n\tif (spoolcounter == 0 || spoolactive)\n\t\treturn;\n\n\tif (XAPrepared())\n\t\treturn;\n\n\tparam[0] = CdlModeSpeed;\n\tCdControlB(CdlSetmode, param, result);\n\n\tif (*result & (CdlStatError | CdlStatShellOpen))\n\t\tWaitCloseLid();\n\t\n\tspoolactive = 1;",
      "\n#endif /*d2cd*/\n\n// [D] [T]\nvoid StartSpooling(void)\n{\n\tif (spoolcounter == 0 || spoolactive)\n\t\treturn;\n\n\tif (XAPrepared())\n\t\treturn;\n\n\tspoolactive = 1;")
    P('Game/C/spool.c',
      "void UpdateSpool(void)\n{\n\tCdlLOC pos;\n\tSPOOLQ* current;\n\n#if defined(SIMPLE_SPOOL)\n\tif (UpdateSpoolPC())\n\t\treturn;\n#endif\n\n\tif (XAPrepared())\n\t\treturn;\n\n\tcurrent = &spooldata[spoolpos_reading];",
      "void UpdateSpool(void)\n{\n#if USE_CD_FILESYSTEM /*d2cd*/\n\tCdlLOC pos;\n\tSPOOLQ* current;\n#endif\n\n#if defined(SIMPLE_SPOOL)\n\tif (UpdateSpoolPC())\n\t\treturn;\n#endif\n\n\tif (XAPrepared())\n\t\treturn;\n\n#if USE_CD_FILESYSTEM /*d2cd*/\n\tcurrent = &spooldata[spoolpos_reading];")
    P('Game/C/spool.c',
      "\t// run sector reading\n\tCdIntToPos(current_sector, &pos);\n\tCdControlF(CdlReadS, (u_char*)&pos);\n}\n",
      "\t// run sector reading\n\tCdIntToPos(current_sector, &pos);\n\tCdControlF(CdlReadS, (u_char*)&pos);\n#endif /*d2cd*/\n}\n")
    # inicializacao do CD: nao existe
    R('Game/C/system.c', r'(?m)^(\t+)CdInit\(\);', r'\1/*d2cd*/')
    R('Game/C/main.c', r'(?m)^(\t+)CdInit\(\);', r'\1/*d2cd*/')

    # system.c / fmvplay.c: o que sobrou do caminho de setores de CD
    R('Game/C/fmvplay.c', r'(?m)^(\t+)CdInit\(\);', r'\1/*d2cd*/')
    R('Game/C/system.c', r'(?s)void ReportMode\(int on\)\r?\n\{(?!\r?\n\t/\*d2cd).*?\r?\n\}\r?\n', 'void ReportMode(int on)\n{\n\t/*d2cd: sem CD*/\n}\n')
    P('Game/C/system.c',
      "static volatile u_char endread = 0;\nstatic volatile u_char load_complete = 0;\n",
      "#if USE_CD_FILESYSTEM /*d2cd*/\nstatic volatile u_char endread = 0;\nstatic volatile u_char load_complete = 0;\n")
    P('Game/C/system.c',
      "\t\tCdIntToPos(current_sector, &p);\n\t\tCdControlF(CdlReadS, (u_char*)&p);\n\t}\n}\n",
      "\t\tCdIntToPos(current_sector, &p);\n\t\tCdControlF(CdlReadS, (u_char*)&p);\n\t}\n}\n#endif /*d2cd*/\n")
    R('Game/C/system.c', r'(?s)\tload_complete = 0;\r?\n\tendread = 0;\r?\n.*?\tShowLoading\(\);\r?\n\}', '\t/*d2cd: leitura so por arquivo (DragonFS)*/\n}')
