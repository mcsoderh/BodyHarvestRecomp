#include "bodyharvest_game.h"
#include "ultramodern/ultramodern.hpp"

// BH's scheduler thread (id 4, created by osCreateScheduler) gets a 12 KB
// stack growing down from 0x8006A330. Recompiled code uses larger stack
// frames than the original, so that stack overflows into the boot thread's
// OSThread at 0x80067388. Relocate it into the upper 4 MB of RDRAM, which
// BH (a 4 MB game) never touches.
constexpr OSId scheduler_thread_id = 4;
constexpr gpr scheduler_stack_top = (gpr)(int32_t)0x807F0000;

void bodyharvest::thread_create_callback(uint8_t* rdram, recomp_context* ctx) {
    OSThread* t = TO_PTR(OSThread, ultramodern::this_thread());
    if (t->id == scheduler_thread_id) {
        ctx->r29 = scheduler_stack_top;
    }
}

std::string bodyharvest::get_game_thread_name(const OSThread* t) {
    if (t->id == scheduler_thread_id) {
        return "[Game] SCHED";
    }
    return "[Game] " + std::to_string(t->id);
}
