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
