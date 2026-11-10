"""Etapa 6: musica nativa. As amostras do XM (MUSIC.BIN) viram wav64 VADPCM offline (tools/convert_music_wav64.py)
e tocam no mixer do libdragon; o SPU RAM com ADPCM do PS1 nao e mais usado para a musica."""

def register(P, R):
    P('Game/C/gamesnd.c',
      "\tcurrent_music_id = musicnum;\n",
      "\tcurrent_music_id = musicnum;\n#if D2_VADPCM\n\td2_vadpcm_set_current_song(musicnum);\n#endif\n")
    P('Game/C/sound.c',
      "\t// transfer sample data\n\tSpuSetTransferMode(SPU_TRANSFER_BY_DMA);\n\n\tSpuSetTransferStartAddr(spuaddress);",
      "#if D2_VADPCM\n\tif (d2_vadpcm_process_music(num_samples, samples[bank]))\n\t\treturn num_samples;\n#endif\n\n\t// transfer sample data\n\tSpuSetTransferMode(SPU_TRANSFER_BY_DMA);\n\n\tSpuSetTransferStartAddr(spuaddress);")
