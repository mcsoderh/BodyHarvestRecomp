// BH copies all of its code overlays (frontend, outside, inside and the
// per-level code) from ROM through func_800101F0_10DF0. Register the
// recompiled functions for whatever was loaded so jumps into it resolve.

#include "patches.h"
#include "misc_funcs.h"

// ROM start of the gameplay overlays that share vram 0x80070270.
#define ROM_OVERLAY_FRONTEND 0x00040720
#define ROM_OVERLAY_OUTSIDE  0x0007F220
#define ROM_OVERLAY_INSIDE   0x00158330

// PI manager message queue.
extern char D_80067F70[];

// Chunked PI DMA: returns rom + size.
unsigned int func_8000FFC0_10BC0(void* mq, void* ram, unsigned int rom, unsigned int size);

int bh_frontend_loaded = 0;

RECOMP_PATCH unsigned int func_800101F0_10DF0(void* ram, unsigned int rom, unsigned int size) {
    unsigned int ret = func_8000FFC0_10BC0(D_80067F70, ram, rom, size);
    recomp_load_overlays(rom, ram, size);

    if (rom == ROM_OVERLAY_FRONTEND) {
        bh_frontend_loaded = 1;
    } else if (rom == ROM_OVERLAY_OUTSIDE || rom == ROM_OVERLAY_INSIDE) {
        bh_frontend_loaded = 0;
    }
    return ret;
}
