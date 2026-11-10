#pragma once

// Private ordering-table packet. The first nine words are a normal FT4.
enum { D2_GROUND_CODE = 0xB4, D2_GROUND_WORDS = 11, D2_PLOT_GROUND = 1 << 5 };

struct D2GroundQuad {
    POLY_FT4 prim;
    unsigned short depth[4];
};
static_assert(sizeof(D2GroundQuad) / 4 - P_LEN == D2_GROUND_WORDS,
              "Unexpected ground packet layout");
