#ifndef __BODYHARVEST_GAME_H__
#define __BODYHARVEST_GAME_H__

#include <cstdint>
#include <string>
#include "recomp.h"
#include "librecomp/rsp.hpp"
#include "ultramodern/ultra64.h"

namespace bodyharvest {
    void register_overlays();
    void register_patches();
    void register_print_exports();

    void thread_create_callback(uint8_t* rdram, recomp_context* ctx);
    std::string get_game_thread_name(const OSThread* t);

    RspUcodeFunc* get_rsp_microcode(const OSTask* task);
};

// High-level emulation of the audio microcode, used instead of the recompiled
// aspMain when BH_AUDIO_HLE=1 is set.
RspExitReason bh_audio_hle(uint8_t* rdram, uint32_t ucode_addr);

#endif
