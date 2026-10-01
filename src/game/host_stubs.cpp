// Host implementations of libultra functions that N64Recomp renames to
// <name>_recomp and expects the runtime to provide.
#include "recomp.h"

// Raw hardware register access has no meaning under the runtime.
extern "C" void __osContAddressCrc_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 0;
}

extern "C" void osPiRawReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 0;
}
