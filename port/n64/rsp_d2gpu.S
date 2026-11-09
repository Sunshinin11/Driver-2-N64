#include <rsp_queue.inc>

    .data

    RSPQ_BeginOverlayHeader
        RSPQ_DefineCommand D2G_Cmd_SetOffset,    8   # 0
        RSPQ_DefineCommand D2G_Cmd_SetTexOrigin, 8   # 1
        RSPQ_DefineCommand D2G_Cmd_PolyF3,       16  # 2
        RSPQ_DefineCommand D2G_Cmd_PolyF4,       20  # 3
        RSPQ_DefineCommand D2G_Cmd_PolyFT3,      20  # 4
        RSPQ_DefineCommand D2G_Cmd_PolyFT4,      28  # 5
        RSPQ_DefineCommand D2G_Cmd_DrawEnd,      4   # 6
        RSPQ_DefineCommand D2G_Cmd_SetTilePal,   8   # 7
        RSPQ_DefineCommand D2G_Cmd_PolyGT3,      32  # 8
        RSPQ_DefineCommand D2G_Cmd_LoadWin,      12  # 9
    RSPQ_EndOverlayHeader

    RSPQ_BeginSavedState
D2G_OUTPUT_OFFSET:  .byte 0
                    .align 1
D2G_OFX:            .short 0
D2G_OFY:            .short 0
D2G_TEX_OU:         .short 0
D2G_TEX_OV:         .short 0
                    .align 3
    RSPQ_EndSavedState

    .data
    .align 4
D2G_VTX_BUFFER:
    # V0
    .word 0, 0x003F, 0, 0, 0x00010000, 0x00010000, 0, 0
    # V1
    .word 0, 0x003F, 0, 0, 0x00010000, 0x00010000, 0, 0
    # V2
    .word 0, 0x003F, 0, 0, 0x00010000, 0x00010000, 0, 0
    # V3
    .word 0, 0x003F, 0, 0, 0x00010000, 0x00010000, 0, 0

    .bss
    .align 4
D2G_OUTPUT_BUFFER:  .ds.b 0xB0 * 2

    .text

    #############################################################
    # D2G_Cmd_SetOffset (Command 0, size 8)
    #
    # a0: [31..24: CMD] [23..0: unused]
    # a1: [31..16: ofx] [15..0: ofy]
    #############################################################
    .func D2G_Cmd_SetOffset
D2G_Cmd_SetOffset:
    sra t0, a1, 16
    sh t0, %lo(D2G_OFX)
    sh a1, %lo(D2G_OFY)
    jr ra
    nop
    .endfunc

    #############################################################
    # D2G_Cmd_SetTexOrigin (Command 1, size 8)
    #
    # a0: [31..24: CMD] [23..0: unused]
    # a1: [31..16: ou]  [15..0: ov]
    #############################################################
    .func D2G_Cmd_SetTexOrigin
D2G_Cmd_SetTexOrigin:
    sra t0, a1, 16
    sh t0, %lo(D2G_TEX_OU)
    sh a1, %lo(D2G_TEX_OV)
    jr ra
    nop
    .endfunc

    #############################################################
    # D2G_Cmd_DrawEnd (Command 6, size 4)
    #
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
    #
    # Configures palette index in TILE0 without switching overlays.
    # a0: [31..24: CMD] [23..0: unused]
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
    # D2G_Cmd_LoadWin (Command 9, size 12)
    #
    # SYNC_LOAD + LOAD_TILE sem trocar de overlay (a troca custava mais que a carga).
    # a0: [31..24: CMD]
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
    # D2G_Cmd_PolyF3 (Command 2, size 16)
    #
    # a0: [31..24: CMD] [23..0: color RRGGBB]
    # a1: (x0 << 16) | (y0 & 0xFFFF)
    # a2: (x1 << 16) | (y1 & 0xFFFF)
    # a3: (x2 << 16) | (y2 & 0xFFFF)
    #############################################################
    .func D2G_Cmd_PolyF3
D2G_Cmd_PolyF3:
    sll t0, a0, 8
    ori t0, t0, 0xFF

    lh t1, %lo(D2G_OFX)
    lh t2, %lo(D2G_OFY)
    li s4, %lo(D2G_VTX_BUFFER)

    # Vertice 0 (a1)
    sra t3, a1, 16
    add t3, t3, t1
    sll t3, t3, 2
    sll t4, a1, 16
    sra t4, t4, 16
    add t4, t4, t2
    sll t4, t4, 2
    andi t4, t4, 0xFFFF
    sll t3, t3, 16
    or t3, t3, t4
    sw t3,   0x00(s4)
    sw t0,   0x08(s4)
    sw zero, 0x0C(s4)

    # Vertice 1 (a2)
    sra t3, a2, 16
    add t3, t3, t1
    sll t3, t3, 2
    sll t4, a2, 16
    sra t4, t4, 16
    add t4, t4, t2
    sll t4, t4, 2
    andi t4, t4, 0xFFFF
    sll t3, t3, 16
    or t3, t3, t4
    sw t3,   0x20(s4)
    sw t0,   0x28(s4)
    sw zero, 0x2C(s4)

    # Vertice 2 (a3)
    sra t3, a3, 16
    add t3, t3, t1
    sll t3, t3, 2
    sll t4, a3, 16
    sra t4, t4, 16
    add t4, t4, t2
    sll t4, t4, 2
    andi t4, t4, 0xFFFF
    sll t3, t3, 16
    or t3, t3, t4
    sw t3,   0x40(s4)
    sw t0,   0x48(s4)
    sw zero, 0x4C(s4)

    lbu t7, %lo(D2G_OUTPUT_OFFSET)
    li sp, %lo(RSPQ_Loop)
    li a0, 0x0C00
    li a1, %lo(D2G_VTX_BUFFER) + 0x00
    li a2, %lo(D2G_VTX_BUFFER) + 0x20
    li a3, %lo(D2G_VTX_BUFFER) + 0x40
    li v0, 2
    jal RDPQ_Triangle_Send_Async
    addi s3, t7, %lo(D2G_OUTPUT_BUFFER)

    xor t7, 0xB0
    sb t7, %lo(D2G_OUTPUT_OFFSET)
    j RSPQ_Loop
    nop
    .endfunc

    #############################################################
    # D2G_Cmd_PolyF4 (Command 3, size 20)
    #
    # a0: [31..24: CMD] [23..0: color RRGGBB]
    # a1: (x0 << 16) | (y0 & 0xFFFF)
    # a2: (x1 << 16) | (y1 & 0xFFFF)
    # a3: (x2 << 16) | (y2 & 0xFFFF)
    # word 4: (x3 << 16) | (y3 & 0xFFFF) via CMD_ADDR(16, 20)
    #############################################################
    .func D2G_Cmd_PolyF4
D2G_Cmd_PolyF4:
    lw t6, CMD_ADDR(16, 20)

    sll t0, a0, 8
    ori t0, t0, 0xFF

    lh t1, %lo(D2G_OFX)
    lh t2, %lo(D2G_OFY)
    li s4, %lo(D2G_VTX_BUFFER)

    # Vertice 0 (a1)
    sra t3, a1, 16
    add t3, t3, t1
    sll t3, t3, 2
    sll t4, a1, 16
    sra t4, t4, 16
    add t4, t4, t2
    sll t4, t4, 2
    andi t4, t4, 0xFFFF
    sll t3, t3, 16
    or t3, t3, t4
    sw t3,   0x00(s4)
    sw t0,   0x08(s4)
    sw zero, 0x0C(s4)

    # Vertice 1 (a2)
    sra t3, a2, 16
    add t3, t3, t1
    sll t3, t3, 2
    sll t4, a2, 16
    sra t4, t4, 16
    add t4, t4, t2
    sll t4, t4, 2
    andi t4, t4, 0xFFFF
    sll t3, t3, 16
    or t3, t3, t4
    sw t3,   0x20(s4)
    sw t0,   0x28(s4)
    sw zero, 0x2C(s4)

    # Vertice 2 (a3)
    sra t3, a3, 16
    add t3, t3, t1
    sll t3, t3, 2
    sll t4, a3, 16
    sra t4, t4, 16
    add t4, t4, t2
    sll t4, t4, 2
    andi t4, t4, 0xFFFF
    sll t3, t3, 16
    or t3, t3, t4
    sw t3,   0x40(s4)
    sw t0,   0x48(s4)
    sw zero, 0x4C(s4)

    # Vertice 3 (t6)
    sra t3, t6, 16
    add t3, t3, t1
    sll t3, t3, 2
    sll t4, t6, 16
    sra t4, t4, 16
    add t4, t4, t2
    sll t4, t4, 2
    andi t4, t4, 0xFFFF
    sll t3, t3, 16
    or t3, t3, t4
    sw t3,   0x60(s4)
    sw t0,   0x68(s4)
    sw zero, 0x6C(s4)

    lbu t7, %lo(D2G_OUTPUT_OFFSET)

    # Triangulo 1 (0, 1, 2)
    li a0, 0x0C00
    li a1, %lo(D2G_VTX_BUFFER) + 0x00
    li a2, %lo(D2G_VTX_BUFFER) + 0x20
    li a3, %lo(D2G_VTX_BUFFER) + 0x40
    li v0, 2
    jal RDPQ_Triangle_Send_Async
    addi s3, t7, %lo(D2G_OUTPUT_BUFFER)

    xor t7, 0xB0

    # Triangulo 2 (1, 3, 2)
    li a0, 0x0C00
    li a1, %lo(D2G_VTX_BUFFER) + 0x20
    li a2, %lo(D2G_VTX_BUFFER) + 0x60
    li a3, %lo(D2G_VTX_BUFFER) + 0x40
    li v0, 2
    jal RDPQ_Triangle_Send_Async
    addi s3, t7, %lo(D2G_OUTPUT_BUFFER)

    xor t7, 0xB0
    sb t7, %lo(D2G_OUTPUT_OFFSET)
    j RSPQ_Loop
    nop
    .endfunc

    #############################################################
    # D2G_Cmd_PolyFT3 (Command 4, size 20)
    #
    # a0: [31..24: CMD] [23..0: color RRGGBB]
    # a1: (x0 << 16) | (y0 & 0xFFFF)
    # a2: (x1 << 16) | (y1 & 0xFFFF)
    # a3: (x2 << 16) | (y2 & 0xFFFF)
    # word 4: (u0 << 24) | (v0 << 16) | (u1 << 8) | (v1) via CMD_ADDR(16, 20)
    # Note: u2/v2 passed in low 16 bits of word 0?
    # In FT3: u0,v0, u1,v1, u2,v2 -> 6 bytes.
    # Better: word 0 holds (CMD << 24) | (u2 << 16) | (v2 << 8) | 0,
    # or pass UVs packed. We'll handle FT4 first and FT3 similarly.
    #############################################################
    .func D2G_Cmd_PolyFT3
D2G_Cmd_PolyFT3:
    jr ra
    nop
    .endfunc

    #############################################################
    # D2G_Cmd_PolyFT4 (Command 5, size 28)
    #
    # a0: [31..24: CMD] [23..0: color RRGGBB]
    # a1: (x0 << 16) | (y0 & 0xFFFF)
    # a2: (x1 << 16) | (y1 & 0xFFFF)
    # a3: (x2 << 16) | (y2 & 0xFFFF)
    # word 4: (x3 << 16) | (y3 & 0xFFFF)               via CMD_ADDR(16, 28)
    # word 5: (u0 << 24) | (v0 << 16) | (u1 << 8) | v1 via CMD_ADDR(20, 28)
    # word 6: (u2 << 24) | (v2 << 16) | (u3 << 8) | v3 via CMD_ADDR(24, 28)
    #############################################################
    .func D2G_Cmd_PolyFT4
D2G_Cmd_PolyFT4:
    lw t6, CMD_ADDR(16, 28)    # x3, y3
    lw t5, CMD_ADDR(20, 28)    # uv0, uv1
    lw t4, CMD_ADDR(24, 28)    # uv2, uv3

    # RGBA32
    sll t0, a0, 8
    ori t0, t0, 0xFF

    lh t1, %lo(D2G_OFX)
    lh t2, %lo(D2G_OFY)
    lh s5, %lo(D2G_TEX_OU)
    lh s6, %lo(D2G_TEX_OV)
    li s4, %lo(D2G_VTX_BUFFER)

    # Vertice 0: XY (a1), UV (t5 >> 16)
    sra t3, a1, 16
    add t3, t3, t1
    sll t3, t3, 2
    sll a0, a1, 16
    sra a0, a0, 16
    add a0, a0, t2
    sll a0, a0, 2
    andi a0, a0, 0xFFFF
    sll t3, t3, 16
    or t3, t3, a0
    sw t3,   0x00(s4)
    sw t0,   0x08(s4)
    # ST0
    srl a0, t5, 24             # u0
    sub a0, a0, s5
    sll a0, a0, 5              # s0
    srl t3, t5, 16             # v0
    andi t3, t3, 0xFF
    sub t3, t3, s6
    sll t3, t3, 5              # t0
    sll a0, a0, 16
    or a0, a0, t3
    sw a0,   0x0C(s4)

    # Vertice 1: XY (a2), UV (t5 & 0xFFFF)
    sra t3, a2, 16
    add t3, t3, t1
    sll t3, t3, 2
    sll a0, a2, 16
    sra a0, a0, 16
    add a0, a0, t2
    sll a0, a0, 2
    andi a0, a0, 0xFFFF
    sll t3, t3, 16
    or t3, t3, a0
    sw t3,   0x20(s4)
    sw t0,   0x28(s4)
    # ST1
    srl a0, t5, 8
    andi a0, a0, 0xFF          # u1
    sub a0, a0, s5
    sll a0, a0, 5
    andi t3, t5, 0xFF          # v1
    sub t3, t3, s6
    sll t3, t3, 5
    sll a0, a0, 16
    or a0, a0, t3
    sw a0,   0x2C(s4)

    # Vertice 2: XY (a3), UV (t4 >> 16)
    sra t3, a3, 16
    add t3, t3, t1
    sll t3, t3, 2
    sll a0, a3, 16
    sra a0, a0, 16
    add a0, a0, t2
    sll a0, a0, 2
    andi a0, a0, 0xFFFF
    sll t3, t3, 16
    or t3, t3, a0
    sw t3,   0x40(s4)
    sw t0,   0x48(s4)
    # ST2
    srl a0, t4, 24             # u2
    sub a0, a0, s5
    sll a0, a0, 5
    srl t3, t4, 16
    andi t3, t3, 0xFF          # v2
    sub t3, t3, s6
    sll t3, t3, 5
    sll a0, a0, 16
    or a0, a0, t3
    sw a0,   0x4C(s4)

    # Vertice 3: XY (t6), UV (t4 & 0xFFFF)
    sra t3, t6, 16
    add t3, t3, t1
    sll t3, t3, 2
    sll a0, t6, 16
    sra a0, a0, 16
    add a0, a0, t2
    sll a0, a0, 2
    andi a0, a0, 0xFFFF
    sll t3, t3, 16
    or t3, t3, a0
    sw t3,   0x60(s4)
    sw t0,   0x68(s4)
    # ST3
    srl a0, t4, 8
    andi a0, a0, 0xFF          # u3
    sub a0, a0, s5
    sll a0, a0, 5
    andi t3, t4, 0xFF          # v3
    sub t3, t3, s6
    sll t3, t3, 5
    sll a0, a0, 16
    or a0, a0, t3
    sw a0,   0x6C(s4)

    lbu t7, %lo(D2G_OUTPUT_OFFSET)
    li sp, %lo(RSPQ_Loop)

    # Triangulo 1 (0, 1, 2) - Shaded + Textured
    li a0, 0x0E00
    li a1, %lo(D2G_VTX_BUFFER) + 0x00
    li a2, %lo(D2G_VTX_BUFFER) + 0x20
    li a3, %lo(D2G_VTX_BUFFER) + 0x40
    li v0, 2
    jal RDPQ_Triangle_Send_Async
    addi s3, t7, %lo(D2G_OUTPUT_BUFFER)

    xor t7, 0xB0

    # Triangulo 2 (1, 3, 2) - Shaded + Textured
    li a0, 0x0E00
    li a1, %lo(D2G_VTX_BUFFER) + 0x20
    li a2, %lo(D2G_VTX_BUFFER) + 0x60
    li a3, %lo(D2G_VTX_BUFFER) + 0x40
    li v0, 2
    jal RDPQ_Triangle_Send_Async
    addi s3, t7, %lo(D2G_OUTPUT_BUFFER)

    xor t7, 0xB0
    sb t7, %lo(D2G_OUTPUT_OFFSET)
    j RSPQ_Loop
    nop
    .endfunc

    #############################################################
    # D2G_Cmd_PolyGT3 (Command 8, size 32): triangulo texturizado com cor por vertice
    #
    # a0: [31..24: CMD] [23..0: cor0 RRGGBB]
    # a1..a3: (x << 16) | (y & 0xFFFF) dos vertices 0..2
    # word 4: (u0 << 24) | (v0 << 16) | (u1 << 8) | v1
    # word 5: (u2 << 24) | (v2 << 16)
    # word 6: cor1 RRGGBB, word 7: cor2 RRGGBB
    #############################################################
    .func D2G_Cmd_PolyGT3
D2G_Cmd_PolyGT3:
    lw t5, CMD_ADDR(16, 32)    # uv0, uv1
    lw t4, CMD_ADDR(20, 32)    # uv2 (u2 << 24 | v2 << 16)
    lw t6, CMD_ADDR(24, 32)    # cor do vertice 1 (RRGGBB)
    lw v1, CMD_ADDR(28, 32)    # cor do vertice 2 (RRGGBB)

    # RGBA32 por vertice
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

    # Vertice 0: XY (a1), UV (t5 >> 16)
    sra t3, a1, 16
    add t3, t3, t1
    sll t3, t3, 2
    sll a0, a1, 16
    sra a0, a0, 16
    add a0, a0, t2
    sll a0, a0, 2
    andi a0, a0, 0xFFFF
    sll t3, t3, 16
    or t3, t3, a0
    sw t3,   0x00(s4)
    sw t0,   0x08(s4)
    # ST0
    srl a0, t5, 24             # u0
    sub a0, a0, s5
    sll a0, a0, 5              # s0
    srl t3, t5, 16             # v0
    andi t3, t3, 0xFF
    sub t3, t3, s6
    sll t3, t3, 5              # t0
    sll a0, a0, 16
    or a0, a0, t3
    sw a0,   0x0C(s4)

    # Vertice 1: XY (a2), UV (t5 & 0xFFFF)
    sra t3, a2, 16
    add t3, t3, t1
    sll t3, t3, 2
    sll a0, a2, 16
    sra a0, a0, 16
    add a0, a0, t2
    sll a0, a0, 2
    andi a0, a0, 0xFFFF
    sll t3, t3, 16
    or t3, t3, a0
    sw t3,   0x20(s4)
    sw t6,   0x28(s4)
    # ST1
    srl a0, t5, 8
    andi a0, a0, 0xFF          # u1
    sub a0, a0, s5
    sll a0, a0, 5
    andi t3, t5, 0xFF          # v1
    sub t3, t3, s6
    sll t3, t3, 5
    sll a0, a0, 16
    or a0, a0, t3
    sw a0,   0x2C(s4)

    # Vertice 2: XY (a3), UV (t4 >> 16)
    sra t3, a3, 16
    add t3, t3, t1
    sll t3, t3, 2
    sll a0, a3, 16
    sra a0, a0, 16
    add a0, a0, t2
    sll a0, a0, 2
    andi a0, a0, 0xFFFF
    sll t3, t3, 16
    or t3, t3, a0
    sw t3,   0x40(s4)
    sw v1,   0x48(s4)
    # ST2
    srl a0, t4, 24             # u2
    sub a0, a0, s5
    sll a0, a0, 5
    srl t3, t4, 16
    andi t3, t3, 0xFF          # v2
    sub t3, t3, s6
    sll t3, t3, 5
    sll a0, a0, 16
    or a0, a0, t3
    sw a0,   0x4C(s4)

    lbu t7, %lo(D2G_OUTPUT_OFFSET)
    li sp, %lo(RSPQ_Loop)

    # Triangulo 1 (0, 1, 2) - Shaded + Textured
    li a0, 0x0E00
    li a1, %lo(D2G_VTX_BUFFER) + 0x00
    li a2, %lo(D2G_VTX_BUFFER) + 0x20
    li a3, %lo(D2G_VTX_BUFFER) + 0x40
    li v0, 2
    jal RDPQ_Triangle_Send_Async
    addi s3, t7, %lo(D2G_OUTPUT_BUFFER)

    xor t7, 0xB0

    sb t7, %lo(D2G_OUTPUT_OFFSET)
    j RSPQ_Loop
    nop
    .endfunc

RDPQ_Triangle_Cull:
    jr ra
    nop

RDPQ_Triangle_Clip:
    jr ra
    nop

#include <rsp_rdpq.inc>
