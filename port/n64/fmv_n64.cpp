#include <libdragon.h>
#include <fmv.h>
#include <mpeg1.h>
#include <stdio.h>
#include "C/fmvplay.h"

static void CheckSkip(void *ctx, int, float, fmv_control_t *control)
{
    bool *skip = static_cast<bool *>(ctx);
    joypad_poll();
    if (joypad_get_buttons_pressed(JOYPAD_PORT_1).start) {
        *skip = true;
        control->stop(control);
    }
}

int FMV_main(RENDER_ARGS *args)
{
    static bool registered;
    bool skip = false;
    bool audio_started = false;
    for (unsigned i = 0; i < args->nRenders && i < 4 && !skip; ++i) {
        char path[32];
        snprintf(path, sizeof(path), "/FMV64/R%02u.m1v", (unsigned)args->Args[i].render);
        if (!dfs_rom_addr(path))
            continue;

        if (!registered) {
            video_register_codec(&mpeg1_codec);
            registered = true;
        }
        if (!audio_started) {
            rspq_wait();
            audio_init(32000, 4);
            mixer_init(1);
            audio_started = true;
        }
        char rom_path[40];
        snprintf(rom_path, sizeof(rom_path), "rom:%s", path);
        fmv_parms_t options = {};
        options.disable_display_init = true;
        options.disable_subtitles = !args->subtitle;
        options.decode_ahead_pics = 3;
        options.osd_callback = CheckSkip;
        options.osd_ctx = &skip;
        fmv_play(rom_path, &options);
    }

    if (audio_started) {
        mixer_close();
        audio_close();
    }
    return skip;
}
