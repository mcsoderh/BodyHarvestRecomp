// Host functions callable from patches (see patches/syms.ld).
#include <algorithm>

#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "librecomp/overlays.hpp"
#include "ultramodern/config.hpp"
#include "recompui/recompui.h"

extern "C" void recomp_load_overlays(uint8_t* rdram, recomp_context* ctx) {
    u32 rom = _arg<0, u32>(rdram, ctx);
    PTR(void) ram = _arg<1, PTR(void)>(rdram, ctx);
    u32 size = _arg<2, u32>(rdram, ctx);

    load_overlays(rom, ram, size);
}

// Aspect ratio the game is being presented at: the window's when the image is
// expanded to fill it, otherwise the original one passed in.
extern "C" void recomp_get_target_aspect_ratio(uint8_t* rdram, recomp_context* ctx) {
    float original = _arg<0, float>(rdram, ctx);
    if (ultramodern::renderer::get_graphics_config().ar_option != ultramodern::renderer::AspectRatio::Expand) {
        _return(ctx, original);
        return;
    }
    int width, height;
    recompui::get_window_size(width, height);
    _return(ctx, std::max(static_cast<float>(width) / height, original));
}
