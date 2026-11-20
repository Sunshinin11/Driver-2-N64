#include <rsp_queue.inc>

    .data

    RSPQ_BeginOverlayHeader
        RSPQ_DefineCommand D2G_Cmd_SetOffset,    8   # 0
        RSPQ_DefineCommand D2G_Cmd_SetTexOrigin, 8   # 1
        RSPQ_DefineCommand D2G_Cmd_PolyF3,       16  # 2
        RSPQ_DefineCommand D2G_Cmd_PolyF4,       20  # 3
        RSPQ_DefineCommand D2G_Cmd_PolyFT3,      20  # 4 (reservado: o FT3 vai pelo PolyGT3 com cor unica)
        RSPQ_DefineCommand D2G_Cmd_PolyFT4,      28  # 5
        RSPQ_DefineCommand D2G_Cmd_DrawEnd,      4   # 6
        RSPQ_DefineCommand D2G_Cmd_SetTilePal,   8   # 7
        RSPQ_DefineCommand D2G_Cmd_PolyGT3,      32  # 8
        RSPQ_DefineCommand D2G_Cmd_LoadWin,      12  # 9
        RSPQ_DefineCommand D2G_Cmd_SetAlpha,     8   # 10
        RSPQ_DefineCommand D2G_Cmd_GroundQuad,   52  # 11
    RSPQ_EndOverlayHeader

    RSPQ_BeginSavedState
D2G_OUTPUT_OFFSET:  .byte 0
D2G_ALPHA:          .byte 0xFF
                    .align 1
D2G_OFX:            .short 0
D2G_OFY:            .short 0
D2G_TEX_OU:         .short 0
D2G_TEX_OV:         .short 0
                    .align 3
    RSPQ_EndSavedState

    .data
    .align 4
    # Vertices no formato de RDPQ_Triangle (rsp_rdpq_tri.inc): XY s13.2, Z/flags (0x3F = dentro), RGBA, ST s10.5, W, 1/W
D2G_VTX_BUFFER:
    .word 0, 0x003F, 0, 0, 0x00010000, 0x00010000, 0, 0
    .word 0, 0x003F, 0, 0, 0x00010000, 0x00010000, 0, 0
    .word 0, 0x003F, 0, 0, 0x00010000, 0x00010000, 0, 0
    .word 0, 0x003F, 0, 0, 0x00010000, 0x00010000, 0, 0
    # chao: buffer proprio (W e 1/W variam por vertice; o buffer acima fica com W = 1/W = 1,0 para os outros comandos)
D2G_VTX_GROUND:
    .word 0, 0x003F, 0, 0, 0x00010000, 0x00010000, 0, 0
    .word 0, 0x003F, 0, 0, 0x00010000, 0x00010000, 0, 0
    .word 0, 0x003F, 0, 0, 0x00010000, 0x00010000, 0, 0
    .word 0, 0x003F, 0, 0, 0x00010000, 0x00010000, 0, 0

    .bss
    .align 4
D2G_OUTPUT_BUFFER:  .ds.b 0xB0 * 2

    .text

    #############################################################
    # D2G_Cmd_SetOffset (Command 0, size 8)
    # a1: [31..16: ofx] [15..0: ofy]
    #############################################################
    .func D2G_Cmd_SetOffset
D2G_Cmd_SetOffset:
    sra t0, a1, 16
    sh t0, %lo(D2G_OFX)
    jr ra
    sh a1, %lo(D2G_OFY)
    .endfunc

    #############################################################
    # D2G_Cmd_SetTexOrigin (Command 1, size 8)
    # a1: [31..16: ou]  [15..0: ov]
    #############################################################
    .func D2G_Cmd_SetTexOrigin
D2G_Cmd_SetTexOrigin:
    sra t0, a1, 16
    sh t0, %lo(D2G_TEX_OU)
    jr ra
    sh a1, %lo(D2G_TEX_OV)
    .endfunc

    #############################################################
    # D2G_Cmd_DrawEnd (Command 6, size 4)
    # Flush pending async triangle DMA and update DP_END.
    #############################################################
    .func D2G_Cmd_DrawEnd
D2G_Cmd_DrawEnd:
    sb zero, %lo(D2G_OUTPUT_OFFSET)
    j RDPQ_Triangle_Send_End
    nop
    .endfunc

    #############################################################
    # D2G_Cmd_SetTilePal (Command 7, size 8)
    # Configures palette index in TILE0 without switching overlays.
    # a1: palette index (0..15)
    #############################################################
    .func D2G_Cmd_SetTilePal
D2G_Cmd_SetTilePal:
    mfc0 t0, COP0_DMA_BUSY
1:  bnez t0, 1b
    mfc0 t0, COP0_DMA_BUSY

    sb zero, %lo(D2G_OUTPUT_OFFSET)

    lw a0, %lo(RDPQ_CURRENT)
    jal RSPQCmd_RdpAppendBuffer
    nop

    lui a0, 0x3540
    ori a0, a0, 0x0800
    andi a1, a1, 0xF
    sll a1, a1, 20

    jal RDPQ_Write8
    nop

    j RDPQ_Finalize
    nop
    .endfunc

    #############################################################
    # D2G_Cmd_SetAlpha (Command 10, size 8)
    # a1: alfa (0..255) usado pelos proximos quads FT4
    #############################################################
    .func D2G_Cmd_SetAlpha
D2G_Cmd_SetAlpha:
    jr ra
    sb a1, %lo(D2G_ALPHA)
    .endfunc

    #############################################################
    # D2G_Cmd_LoadWin (Command 9, size 12)
    # SYNC_LOAD + LOAD_TILE sem trocar de overlay (a troca custava mais que a carga).
    # a1: palavra 0 do LOAD_TILE   a2: palavra 1 do LOAD_TILE
    #############################################################
    .func D2G_Cmd_LoadWin
D2G_Cmd_LoadWin:
    mfc0 t0, COP0_DMA_BUSY
1:  bnez t0, 1b
    mfc0 t0, COP0_DMA_BUSY

    sb zero, %lo(D2G_OUTPUT_OFFSET)

    move t8, a1
    move t9, a2
    lw a0, %lo(RDPQ_CURRENT)
    jal RSPQCmd_RdpAppendBuffer
    nop

    lui a0, 0x2600
    li a1, 0
    move a2, t8
    move a3, t9
    jal RDPQ_Write16
    nop

    j RDPQ_Finalize
    nop
    .endfunc

    #############################################################
    # Sub-rotinas de vertice (s4 = vertice de destino; usam t8/t9)
    #
    # D2G_PutXY: a0 = (x << 16) | (y & 0xFFFF) -> XY s13.2 com o offset (t1 = ofx, t2 = ofy)
    # D2G_PutST: a0 = (u << 8) | v (16 bits baixos) -> ST s10.5 relativo a origem da janela (s5 = ou, s6 = ov)
    #############################################################
    .func D2G_PutXY
D2G_PutXY:
    sra t8, a0, 16
    add t8, t8, t1
    sll t8, t8, 18
    sll t9, a0, 16
    sra t9, t9, 16
    add t9, t9, t2
    sll t9, t9, 2
    andi t9, t9, 0xFFFF
    or t8, t8, t9
    jr ra
    sw t8, 0x00(s4)
    .endfunc

    .func D2G_PutST
D2G_PutST:
    srl t8, a0, 8
    andi t8, t8, 0xFF
    sub t8, t8, s5
    sll t8, t8, 21
    andi t9, a0, 0xFF
    sub t9, t9, s6
    sll t9, t9, 5
    or t8, t8, t9
    jr ra
    sw t8, 0x0C(s4)
    .endfunc

    #############################################################
    # D2G_FillQuad: 4 vertices em s4 (cor t0; XY a1, a2, a3, t6; UV t5 = uv0 uv1, t4 = uv2 uv3)
    # Carrega offset e origem da textura. Usa t7 para o retorno; devolve s4 intacto.
    #############################################################
    .func D2G_FillQuad
D2G_FillQuad:
    move t7, ra
    lh t1, %lo(D2G_OFX)
    lh t2, %lo(D2G_OFY)
    lh s5, %lo(D2G_TEX_OU)
    lh s6, %lo(D2G_TEX_OV)
    sw t0, 0x08(s4)
    sw t0, 0x28(s4)
    sw t0, 0x48(s4)
    sw t0, 0x68(s4)
    jal D2G_PutXY
    move a0, a1
    jal D2G_PutST
    srl a0, t5, 16
    addiu s4, s4, 0x20
    jal D2G_PutXY
    move a0, a2
    jal D2G_PutST
    andi a0, t5, 0xFFFF
    addiu s4, s4, 0x20
    jal D2G_PutXY
    move a0, a3
    jal D2G_PutST
    srl a0, t4, 16
    addiu s4, s4, 0x20
    jal D2G_PutXY
    move a0, t6
    jal D2G_PutST
    andi a0, t4, 0xFFFF
    jr t7
    addiu s4, s4, -0x60
    .endfunc

    #############################################################
    # Envio: s7 = comando do triangulo (0x0C00 cor, 0x0E00 cor + textura), s4 = vertices
    # D2G_SendTri: triangulo 0-1-2    D2G_SendQuad: triangulos 0-1-2 mais 1-3-2
    # RDPQ_Triangle_Send_Async preserva s4/s7 (usa t0-t6, a0-a3, s3, v0, fp).
    #############################################################
    .func D2G_SendQuad
D2G_SendQuad:
    lbu t7, %lo(D2G_OUTPUT_OFFSET)
    li sp, %lo(RSPQ_Loop)
    move a0, s7
    move a1, s4
    addiu a2, s4, 0x20
    addiu a3, s4, 0x40
    li v0, 2
    jal RDPQ_Triangle_Send_Async
    addi s3, t7, %lo(D2G_OUTPUT_BUFFER)
    xor t7, 0xB0
    sb t7, %lo(D2G_OUTPUT_OFFSET)
    addiu s4, s4, 0x20          # segundo triangulo: (1, 3, 2) = s4+0x20, s4+0x60, s4+0x40
    move a0, s7
    move a1, s4
    addiu a2, s4, 0x40
    j D2G_SendTri2
    addiu a3, s4, 0x20
    .endfunc

    .func D2G_SendTri
D2G_SendTri:
    move a0, s7
    move a1, s4
    addiu a2, s4, 0x20
    addiu a3, s4, 0x40
D2G_SendTri2:
    lbu t7, %lo(D2G_OUTPUT_OFFSET)
    li sp, %lo(RSPQ_Loop)
    li v0, 2
    jal RDPQ_Triangle_Send_Async
    addi s3, t7, %lo(D2G_OUTPUT_BUFFER)
    xor t7, 0xB0
    sb t7, %lo(D2G_OUTPUT_OFFSET)
    j RSPQ_Loop
    nop
    .endfunc

    #############################################################
    # D2G_Cmd_PolyF3 (Command 2, size 16)
    # a0: [CMD][RRGGBB]; a1..a3: (x << 16) | (y & 0xFFFF)
    #############################################################
    .func D2G_Cmd_PolyF3
D2G_Cmd_PolyF3:
    sll t0, a0, 8
    ori t0, t0, 0xFF
    lh t1, %lo(D2G_OFX)
    lh t2, %lo(D2G_OFY)
    li s4, %lo(D2G_VTX_BUFFER)
    sw t0, 0x08(s4)
    sw t0, 0x28(s4)
    sw t0, 0x48(s4)
    jal D2G_PutXY
    move a0, a1
    addiu s4, s4, 0x20
    jal D2G_PutXY
    move a0, a2
    addiu s4, s4, 0x20
    jal D2G_PutXY
    move a0, a3
    addiu s4, s4, -0x40
    j D2G_SendTri
    li s7, 0x0C00
    .endfunc

    #############################################################
    # D2G_Cmd_PolyF4 (Command 3, size 20)
    # a0: [CMD][RRGGBB]; a1..a3, word 4: XY dos vertices 0..3 (ST nao e usado: triangulo sem textura)
    #############################################################
    .func D2G_Cmd_PolyF4
D2G_Cmd_PolyF4:
    lw t6, CMD_ADDR(16, 20)
    sll t0, a0, 8
    ori t0, t0, 0xFF
    li t5, 0
    li t4, 0
    jal D2G_FillQuad
    li s4, %lo(D2G_VTX_BUFFER)
    j D2G_SendQuad
    li s7, 0x0C00
    .endfunc

    #############################################################
    # D2G_Cmd_PolyFT3 (Command 4): nao usado
    #############################################################
    .func D2G_Cmd_PolyFT3
D2G_Cmd_PolyFT3:
    jr ra
    nop
    .endfunc

    #############################################################
    # D2G_Cmd_PolyFT4 (Command 5, size 28)
    # a0: [CMD][RRGGBB]; a1..a3, word 4: XY 0..3
    # word 5: (u0 << 24) | (v0 << 16) | (u1 << 8) | v1;  word 6: (u2 << 24) | (v2 << 16) | (u3 << 8) | v3
    #############################################################
    .func D2G_Cmd_PolyFT4
D2G_Cmd_PolyFT4:
    lw t6, CMD_ADDR(16, 28)
    lw t5, CMD_ADDR(20, 28)
    lw t4, CMD_ADDR(24, 28)
    sll t0, a0, 8
    lbu t1, %lo(D2G_ALPHA)      # alfa do estado: 0xFF para quads opacos
    or t0, t0, t1
    jal D2G_FillQuad
    li s4, %lo(D2G_VTX_BUFFER)
    j D2G_SendQuad
    li s7, 0x0E00
    .endfunc

    #############################################################
    # D2G_Cmd_GroundQuad (Command 11, size 52): quad do chao com perspectiva correta
    # a0: [CMD][RRGGBB]; a1..a3, word 4: XY 0..3; words 5/6: UV como no FT4
    # words 7..10: 1/W (s16.16) dos vertices 0..3 (calculado na CPU, mesma divisao de antes)
    # word 11: (d0 << 16) | d1;  word 12: (d2 << 16) | d3  (profundidade; W = d << 16, d = 0 vira 1, d >= 32768 satura)
    #############################################################
    .func D2G_Cmd_GroundQuad
D2G_Cmd_GroundQuad:
    lw t6, CMD_ADDR(16, 52)
    lw t5, CMD_ADDR(20, 52)
    lw t4, CMD_ADDR(24, 52)
    sll t0, a0, 8
    ori t0, t0, 0xFF
    jal D2G_FillQuad
    li s4, %lo(D2G_VTX_GROUND)
    addiu t5, rspq_dmem_buf_ptr, %lo(RSPQ_DMEM_BUFFER) + 28 - 52    # 1/W do vertice 0
    addiu t3, rspq_dmem_buf_ptr, %lo(RSPQ_DMEM_BUFFER) + 44 - 52    # profundidade do vertice 0
    move t6, s4
    addiu t4, s4, 0x80
1:  lw t8, 0(t5)
    lhu t9, 0(t3)
    sw t8, 0x14(t6)
    bnez t9, 2f
    sll t8, t9, 16
    lui t8, 1                   # d = 0 -> W = 1 << 16
2:  bgez t8, 3f
    nop
    li t8, 0x7FFFFFFF
3:  sw t8, 0x10(t6)
    addiu t5, t5, 4
    addiu t6, t6, 0x20
    bne t6, t4, 1b
    addiu t3, t3, 2
    j D2G_SendQuad
    li s7, 0x0E00
    .endfunc

    #############################################################
    # D2G_Cmd_PolyGT3 (Command 8, size 32): triangulo texturizado com cor por vertice
    # a0: [CMD][cor0]; a1..a3: XY 0..2
    # word 4: (u0 << 24) | (v0 << 16) | (u1 << 8) | v1;  word 5: (u2 << 24) | (v2 << 16)
    # word 6: cor1 RRGGBB, word 7: cor2 RRGGBB
    #############################################################
    .func D2G_Cmd_PolyGT3
D2G_Cmd_PolyGT3:
    lw t5, CMD_ADDR(16, 32)
    lw t4, CMD_ADDR(20, 32)
    lw t6, CMD_ADDR(24, 32)
    lw v1, CMD_ADDR(28, 32)
    sll t0, a0, 8
    ori t0, t0, 0xFF
    sll t6, t6, 8
    ori t6, t6, 0xFF
    sll v1, v1, 8
    ori v1, v1, 0xFF
    lh t1, %lo(D2G_OFX)
    lh t2, %lo(D2G_OFY)
    lh s5, %lo(D2G_TEX_OU)
    lh s6, %lo(D2G_TEX_OV)
    li s4, %lo(D2G_VTX_BUFFER)
    sw t0, 0x08(s4)
    sw t6, 0x28(s4)
    sw v1, 0x48(s4)
    jal D2G_PutXY
    move a0, a1
    jal D2G_PutST
    srl a0, t5, 16
    addiu s4, s4, 0x20
    jal D2G_PutXY
    move a0, a2
    jal D2G_PutST
    andi a0, t5, 0xFFFF
    addiu s4, s4, 0x20
    jal D2G_PutXY
    move a0, a3
    jal D2G_PutST
    srl a0, t4, 16
    addiu s4, s4, -0x40
    j D2G_SendTri
    li s7, 0x0E00
    .endfunc

RDPQ_Triangle_Cull:
    jr ra
    nop

RDPQ_Triangle_Clip:
    jr ra
    nop

#include <rsp_rdpq.inc>
