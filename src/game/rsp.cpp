#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "bodyharvest_game.h"

extern RspUcodeFunc aspMain;

// OSTask field offsets within the copy that run_task places at DMEM 0xFC0.
constexpr uint32_t task_dmem_offset = 0xFC0;
constexpr uint32_t task_ucode_data_size = 0x1C;
constexpr uint32_t dmem_data_end = 0xF80;

// librecomp's run_task always DMAs 0xF80 bytes of ucode data into DMEM. BH's
// audio boot code only loads ucode_data_size (0x800) bytes, and the audio
// ucode keeps voice/ADPCM state in the rest of DMEM between tasks. Restore
// that region from the previous task before running, and save it afterwards.
static RspExitReason bh_aspMain(uint8_t* rdram, uint32_t ucode_addr) {
    static uint8_t persistent[dmem_data_end];

    uint32_t data_size = RSP_MEM_W_LOAD(task_ucode_data_size, task_dmem_offset);
    if (data_size == 0 || data_size > dmem_data_end) {
        fprintf(stderr, "Unexpected audio task ucode_data_size 0x%" PRIX32 "\n", data_size);
        return RspExitReason::Unsupported;
    }

    std::memcpy(dmem + data_size, persistent + data_size, dmem_data_end - data_size);
    RspExitReason exit_reason = aspMain(rdram, ucode_addr);
    std::memcpy(persistent + data_size, dmem + data_size, dmem_data_end - data_size);
    return exit_reason;
}

RspUcodeFunc* bodyharvest::get_rsp_microcode(const OSTask* task) {
    switch (task->t.type) {
    case M_AUDTASK: {
        static const bool use_hle = [] {
            const char* e = std::getenv("BH_AUDIO_HLE");
            return e != nullptr && std::strcmp(e, "1") == 0;
        }();
        return use_hle ? bh_audio_hle : bh_aspMain;
    }

    default:
        fprintf(stderr, "Unknown task: %" PRIu32 "\n", task->t.type);
        return nullptr;
    }
}
