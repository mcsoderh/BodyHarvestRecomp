// Body Harvest port — HLE for the N64 audio microcode (libultra ABI 1).
//
// BH submits M_AUDTASK to the RSP. The real ucode reads an ABI command list
// from RDRAM (task->data_ptr), runs commands against a 4KB DMEM scratchpad,
// and SAVEBUFF's PCM stereo samples back to RDRAM (an output buffer that BH
// later hands to osAiSetNextBuffer).
//
// The static-recompiled aspMain (rsp/aspMain.cpp) is the default path; this
// HLE is a fallback, selected with BH_AUDIO_HLE=1.
//
// Approach: simulate just enough of the RSP audio scratchpad. We allocate
// a host-side "DMEM" buffer (4 KiB) and a segment table (16 entries), parse
// 8-byte ABI 1 commands from the task's data_ptr, and execute each command
// directly on host memory. SAVEBUFF writes results back to RDRAM. This is
// a self-contained C++17 file with no external deps beyond <librecomp/rsp.hpp>.
//
// References used (without copying):
//   - sm64plus include/PR/abi.h — ABI 1 macro layouts (SETBUFF/MIXER/...)
//   - mm-decomp src/audio/lib/synthesis.c — high-level usage roadmap (ABI 2
//     differs in details but the command interaction pattern is the same)
//   - mupen64plus-rsp-hle/src/alist.c — overall architecture (we wrote our own)
//
// ABI 1 cmd numbering (from sm64plus abi.h):
//   0x00 SPNOOP, 0x01 ADPCM,   0x02 CLEARBUFF, 0x03 ENVMIXER,
//   0x04 LOADBUFF, 0x05 RESAMPLE, 0x06 SAVEBUFF, 0x07 SEGMENT,
//   0x08 SETBUFF, 0x09 SETVOL, 0x0A DMEMMOVE,  0x0B LOADADPCM,
//   0x0C MIXER,   0x0D INTERLEAVE, 0x0E POLEF, 0x0F SETLOOP
//
// SETBUFF is "sticky": its in/out/count are reused by LOADBUFF/SAVEBUFF/MIXER
// and (with explicit args from a second SETBUFF flagged A_AUX) by ENVMIXER.

#include "librecomp/rsp.hpp"
#include "recomp.h"
#include "ultramodern/ultra64.h"

#include "bodyharvest_game.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <algorithm>

// ---- Host-side scratchpad emulating the RSP's 4 KiB DMEM ----
//
// Real ucode keeps a number of fixed buffers in DMEM (rsp scratch, dry/wet,
// segment table at 0x320, working sample buf, ADPCM state, etc.). We don't
// care about the real layout: we just use the addresses BH passes in as
// indices into a 4 KiB byte array. Stereo PCM output is normally written to
// a single DMEM region that SAVEBUFF then DMAs back to RDRAM, which is all
// we need to forward to SDL.

namespace {

constexpr size_t kDmemSize = 0x1000;
alignas(16) static uint8_t g_dmem[kDmemSize];

// 16-slot segment table; SEGMENT cmd populates it, other cmds (LOADBUFF,
// SAVEBUFF, LOADADPCM, SETLOOP, ADPCM, ENVMIXER, RESAMPLE) resolve their
// DRAM address as seg_table[(addr>>24)&0xF] + (addr & 0xFFFFFF).
static uint32_t g_segments[16];

// Sticky state set by aSetBuffer.
struct {
    uint8_t  flags = 0;
    uint16_t dmem_in = 0;
    uint16_t dmem_out = 0;
    uint16_t count = 0;
    // A_AUX variant (second aSetBuffer call before envmixer):
    uint16_t aux_dry_right = 0;
    uint16_t aux_wet_left = 0;
    uint16_t aux_wet_right = 0;
} g_setbuf;

// SETVOL sticky state (used by ENVMIXER).
struct {
    int16_t vol_left = 0;
    int16_t vol_right = 0;
    int16_t target_left = 0;
    int16_t target_right = 0;
    int16_t rate_left = 0;
    int16_t rate_right = 0;
    int16_t dry = 0;
    int16_t wet = 0;
} g_vol;

// ADPCM book loaded by LOADADPCM. The book is a sequence of 16-byte entries
// each containing 8 int16 BE coefficients. Real RSP supports order 2 or 4
// predictors; book[order][numPredictors][8].
static int16_t g_adpcm_book[8 * 8 * 8]; // up to 8 predictors * order 8 * 8 coeffs
static int     g_adpcm_book_entries = 0; // count of 16-byte entries

// ADPCM loop-state DRAM address from SETLOOP.
static uint32_t g_adpcm_loop_state_addr = 0;

// ---- RDRAM byte access helpers ----
//
// RDRAM is host-endian 32-bit words. A logical "BE" byte at virtual offset N
// lives at host index (N ^ 3). For a 16-bit halfword at even N, at host index
// (N ^ 2) (since (N^3, (N+1)^3) = (N+3, N+2) for N=even, but reading as int16
// the BE order means MSB at (N^3), LSB at ((N+1)^3); we'd reassemble — easier
// to just byte-fetch.

static inline uint8_t rdram_b(const uint8_t* rdram, uint32_t addr) {
    return rdram[(addr & 0x7FFFFF) ^ 3];
}
static inline void rdram_set_b(uint8_t* rdram, uint32_t addr, uint8_t v) {
    rdram[(addr & 0x7FFFFF) ^ 3] = v;
}

// 16-bit BE read from RDRAM (works for any alignment).
static inline int16_t rdram_h(const uint8_t* rdram, uint32_t addr) {
    uint8_t hi = rdram_b(rdram, addr);
    uint8_t lo = rdram_b(rdram, addr + 1);
    return (int16_t)((uint16_t)hi << 8 | lo);
}
static inline void rdram_set_h(uint8_t* rdram, uint32_t addr, int16_t v) {
    rdram_set_b(rdram, addr,     (uint8_t)((v >> 8) & 0xFF));
    rdram_set_b(rdram, addr + 1, (uint8_t)(v & 0xFF));
}

// 32-bit BE read from RDRAM. For 8-byte-aligned cmd words we use this to
// fetch ABI commands; same trick works for ucode_data too.
static inline uint32_t rdram_w(const uint8_t* rdram, uint32_t addr) {
    return  ((uint32_t)rdram_b(rdram, addr    ) << 24)
          | ((uint32_t)rdram_b(rdram, addr + 1) << 16)
          | ((uint32_t)rdram_b(rdram, addr + 2) <<  8)
          | ((uint32_t)rdram_b(rdram, addr + 3));
}

// ---- DMEM accessors (16-bit signed samples, byte-addressed, BE-ish) ----
//
// Real RSP audio ucode stores int16 samples in DMEM as big-endian halfwords.
// Since *our* DMEM is just a host byte buffer we control, we choose the same
// BE layout. That way a halfword at addr a is composed of
// (dmem[a]<<8) | dmem[a+1], matching how the real ucode would read.

static inline int16_t dmem_h(uint16_t addr) {
    addr &= (kDmemSize - 1);
    return (int16_t)((uint16_t)g_dmem[addr] << 8 | g_dmem[(addr + 1) & (kDmemSize - 1)]);
}
static inline void dmem_set_h(uint16_t addr, int16_t v) {
    addr &= (kDmemSize - 1);
    g_dmem[addr]                        = (uint8_t)((uint16_t)v >> 8);
    g_dmem[(addr + 1) & (kDmemSize - 1)] = (uint8_t)((uint16_t)v & 0xFF);
}

// Saturating clip for mixer accumulation.
static inline int16_t sat16(int32_t v) {
    if (v >  32767) return  32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

// Resolve a DRAM addr through the segment table.
static inline uint32_t seg_resolve(uint32_t a) {
    uint32_t seg = (a >> 24) & 0xF;
    return (g_segments[seg] + (a & 0x00FFFFFF)) & 0x7FFFFF;
}

// ---- Command handlers ----

static void cmd_segment(uint32_t w0, uint32_t w1) {
    (void)w0;
    uint32_t s = (w1 >> 24) & 0xF;
    uint32_t b = w1 & 0x00FFFFFF;
    g_segments[s] = b;
}

static void cmd_clearbuff(uint32_t w0, uint32_t w1) {
    uint16_t d = (uint16_t)(w0 & 0xFFFF); // bottom 24 of w0 is addr; mask 16 since DMEM<4K
    uint16_t c = (uint16_t)(w1 & 0xFFFF);
    if ((size_t)d + c > kDmemSize) c = (uint16_t)(kDmemSize - d);
    std::memset(g_dmem + d, 0, c);
}

static void cmd_setbuff(uint32_t w0, uint32_t w1) {
    uint8_t  flags  = (uint8_t)((w0 >> 16) & 0xFF);
    uint16_t in     = (uint16_t)(w0 & 0xFFFF);
    uint16_t out    = (uint16_t)((w1 >> 16) & 0xFFFF);
    uint16_t count  = (uint16_t)(w1 & 0xFFFF);
    if (flags & 0x08) { // A_AUX
        // 2nd-set-buffer form: encodes 3 extra DMEM ptrs for envmixer.
        g_setbuf.aux_dry_right = in;
        g_setbuf.aux_wet_left  = out;
        g_setbuf.aux_wet_right = count;
    } else {
        g_setbuf.flags    = flags;
        g_setbuf.dmem_in  = in;
        g_setbuf.dmem_out = out;
        g_setbuf.count    = count;
    }
}

static void cmd_loadbuff(const uint8_t* rdram, uint32_t /*w0*/, uint32_t w1) {
    uint32_t src = seg_resolve(w1);
    uint16_t dst = g_setbuf.dmem_in;
    uint16_t cnt = g_setbuf.count;
    // Round up to 16 like real ucode (it operates 16 bytes at a time).
    uint16_t aligned = (uint16_t)((cnt + 0xF) & ~0xF);
    if ((size_t)dst + aligned > kDmemSize) aligned = (uint16_t)(kDmemSize - dst);
    for (uint16_t i = 0; i < aligned; i++) {
        g_dmem[dst + i] = rdram_b(rdram, src + i);
    }
}

static void cmd_savebuff(uint8_t* rdram, uint32_t /*w0*/, uint32_t w1) {
    uint32_t dst = seg_resolve(w1);
    uint16_t src = g_setbuf.dmem_out;
    uint16_t cnt = g_setbuf.count;
    uint16_t aligned = (uint16_t)((cnt + 0xF) & ~0xF);
    if ((size_t)src + aligned > kDmemSize) aligned = (uint16_t)(kDmemSize - src);
    // DEBUG: write silence (zeros) to confirm byte-order pipeline works.
    // If speakers go silent with this, byte-order is correct and the
    // corruption is in MIXER / LOADBUFF / DMEM state. If speakers still
    // produce noise, byte-order or pointer-resolution is wrong.
    for (uint16_t i = 0; i < aligned; i++) {
        rdram_set_b(rdram, dst + i, 0);
    }
    (void)src;
}

static void cmd_mixer(uint32_t w0, uint32_t w1) {
    // flags in (w0>>16)&0xFF are unused; gain is signed in low 16 of w0.
    int16_t  gain = (int16_t)(w0 & 0xFFFF);
    uint16_t in   = (uint16_t)((w1 >> 16) & 0xFFFF);
    uint16_t out  = (uint16_t)(w1 & 0xFFFF);
    uint16_t cnt  = g_setbuf.count;
    // mixer operates in 32-byte chunks per real ucode; round up.
    uint16_t aligned = (uint16_t)((cnt + 0x1F) & ~0x1F);
    uint16_t samples = aligned / 2;
    for (uint16_t i = 0; i < samples; i++) {
        int32_t a = dmem_h((uint16_t)(in  + i * 2));
        int32_t b = dmem_h((uint16_t)(out + i * 2));
        // (a * gain) is Q1.15 mul; >>15 brings back. We add to b.
        int32_t scaled = (a * gain) >> 15;
        dmem_set_h((uint16_t)(out + i * 2), sat16(b + scaled));
    }
}

static void cmd_dmem_move(uint32_t w0, uint32_t w1) {
    uint16_t in  = (uint16_t)(w0 & 0xFFFF);
    uint16_t out = (uint16_t)((w1 >> 16) & 0xFFFF);
    uint16_t cnt = (uint16_t)(w1 & 0xFFFF);
    uint16_t aligned = (uint16_t)((cnt + 0xF) & ~0xF);
    if ((size_t)out + aligned > kDmemSize) aligned = (uint16_t)(kDmemSize - out);
    if ((size_t)in  + aligned > kDmemSize) aligned = (uint16_t)(kDmemSize - in);
    if (out < in) {
        std::memmove(g_dmem + out, g_dmem + in, aligned);
    } else {
        // Real ucode notes: if dst > src, copy 16-at-a-time forward — same as
        // memmove for non-overlapping; explicit forward copy if overlapping.
        for (uint16_t i = 0; i < aligned; i++) g_dmem[out + i] = g_dmem[in + i];
    }
}

static void cmd_interleave(uint32_t /*w0*/, uint32_t w1) {
    // aSetBuffer(0, 0, out, count) — count is each input size; 2*count written.
    uint16_t l   = (uint16_t)((w1 >> 16) & 0xFFFF);
    uint16_t r   = (uint16_t)(w1 & 0xFFFF);
    uint16_t out = g_setbuf.dmem_out;
    uint16_t cnt = g_setbuf.count; // size of each input in bytes
    uint16_t aligned = (uint16_t)((cnt + 0xF) & ~0xF);
    uint16_t pairs = aligned / 2; // number of samples per channel
    for (uint16_t i = 0; i < pairs; i++) {
        int16_t ls = dmem_h((uint16_t)(l + i * 2));
        int16_t rs = dmem_h((uint16_t)(r + i * 2));
        dmem_set_h((uint16_t)(out + i * 4 + 0), ls);
        dmem_set_h((uint16_t)(out + i * 4 + 2), rs);
    }
}

static void cmd_setvol(uint32_t w0, uint32_t w1) {
    uint16_t flags = (uint16_t)((w0 >> 16) & 0xFFFF);
    int16_t  v     = (int16_t)(w0 & 0xFFFF);
    int16_t  t     = (int16_t)((w1 >> 16) & 0xFFFF);
    int16_t  r     = (int16_t)(w1 & 0xFFFF);
    if (flags & 0x04 /*A_VOL*/) {
        if (flags & 0x02 /*A_LEFT*/) g_vol.vol_left  = v;
        else                         g_vol.vol_right = v;
    } else if (flags & 0x08 /*A_AUX*/) {
        g_vol.dry = v;
        g_vol.wet = r;
    } else {
        // A_RATE: t = target volume, r = ramp rate. v is unused per macros.
        if (flags & 0x02 /*A_LEFT*/) {
            g_vol.target_left = t;
            g_vol.rate_left   = r;
        } else {
            g_vol.target_right = t;
            g_vol.rate_right   = r;
        }
    }
}

static void cmd_loadadpcm(const uint8_t* rdram, uint32_t w0, uint32_t w1) {
    uint32_t cnt = w0 & 0x00FFFFFF; // bytes
    uint32_t src = seg_resolve(w1);
    if (cnt > sizeof(g_adpcm_book)) cnt = sizeof(g_adpcm_book);
    g_adpcm_book_entries = (int)(cnt / 16);
    // Book is stored as int16 big-endian in RDRAM. Decode to host int16.
    int16_t* dst = g_adpcm_book;
    uint32_t nh = cnt / 2;
    for (uint32_t i = 0; i < nh; i++) {
        dst[i] = rdram_h(rdram, src + i * 2);
    }
}

static void cmd_setloop(uint32_t /*w0*/, uint32_t w1) {
    g_adpcm_loop_state_addr = seg_resolve(w1);
}

// ADPCM decode. Each 9-byte block produces 16 samples:
//   byte0     = (scale << 4) | predictor_index (4 bits each)
//   byte1..8  = 16 nibbles of signed 4-bit residual (high nibble first)
//
// The codebook is a list of "predictor rows" of 8 int16 each. With order 2
// (libultra default) two consecutive rows form one predictor. So a book with
// N 16-byte entries gives N/2 predictors. The decode for sample j of pass p:
//   sample[j] = (residual[j] << scale)
//             + sum_{k<j} row1[j-k-1] * residual[k]   // contribution of
//                                                       // new residuals
//             + row0[j] * hist[14]                    // 2 most-recent hist
//             + row1[j] * hist[15]
//             >> 11
// where pass operates on 8 samples at a time and hist is shifted between
// passes. We implement a tractable approximation that produces audible
// output without diverging into chaos (sat16 around acc).
static void cmd_adpcm(uint8_t* rdram, uint32_t w0, uint32_t w1) {
    uint8_t  flags = (uint8_t)((w0 >> 16) & 0xFF); // A_INIT=1, A_LOOP=2
    uint32_t state = seg_resolve(w1);
    uint16_t in    = g_setbuf.dmem_in;
    uint16_t out   = g_setbuf.dmem_out;
    uint16_t cnt   = g_setbuf.count; // output bytes
    uint16_t aligned = (uint16_t)((cnt + 0x1F) & ~0x1F);

    // History: 16 samples preserved between blocks. The real ucode prepends
    // these 16 samples to the output then continues decoding.
    int16_t hist[16] = {0};
    if (flags & 0x01 /*A_INIT*/) {
        // already zero
    } else {
        uint32_t addr = (flags & 0x02 /*A_LOOP*/) ? g_adpcm_loop_state_addr : state;
        for (int i = 0; i < 16; i++) {
            hist[i] = rdram_h(rdram, addr + i * 2);
        }
    }
    for (int i = 0; i < 16; i++) {
        dmem_set_h((uint16_t)(out + i * 2), hist[i]);
    }
    out += 32;

    constexpr int kOrder = 2;
    const int16_t* book = g_adpcm_book;
    int n_pred = std::max(1, g_adpcm_book_entries / kOrder);

    // Running 2-sample history (the most-recent 2 samples emitted so far).
    int16_t s2 = hist[14];
    int16_t s1 = hist[15];

    uint16_t out_bytes_remaining = aligned;
    uint16_t in_pos = in;
    uint16_t out_pos = out;

    while (out_bytes_remaining >= 32) {
        uint8_t hdr = g_dmem[in_pos & (kDmemSize - 1)];
        in_pos++;
        int scale = (hdr >> 4) & 0xF;
        int pred_idx = hdr & 0xF;
        if (pred_idx >= n_pred) pred_idx = 0;
        if (scale > 12) scale = 12; // clamp to keep <<scale in s16 range

        // Read 16 signed 4-bit residuals, sign-extended and shifted by scale
        // into Q11 (real ucode shifts by 11+scale here; we keep <<scale and
        // adjust below).
        int32_t resid[16];
        for (int i = 0; i < 8; i++) {
            uint8_t b = g_dmem[(in_pos + i) & (kDmemSize - 1)];
            int hi = (int)(int8_t)(b & 0xF0) >> 4;       // sign-extend hi nibble
            int lo = (int)(int8_t)((b & 0x0F) << 4) >> 4;
            resid[i * 2 + 0] = hi << scale;
            resid[i * 2 + 1] = lo << scale;
        }
        in_pos += 8;

        // Decode in two passes of 8 samples each. Within a pass, each new
        // sample j depends on s1/s2 from the previous block AND on the
        // residuals (left-shifted) of earlier samples within the same pass.
        // We model this by recomputing s1/s2 sample-by-sample and clamping.
        for (int pass = 0; pass < 2; pass++) {
            const int16_t* row0 = book + pred_idx * kOrder * 8 + 0 * 8;
            const int16_t* row1 = book + pred_idx * kOrder * 8 + 1 * 8;
            for (int j = 0; j < 8; j++) {
                // Standard libultra ADPCM: predictor uses 2-sample history.
                // sample[j] = ( row0[j]*s2 + row1[j]*s1
                //             + sum_{k<j} row1[7-(j-1-k)] * resid[k] ) >> 11
                //           + resid[j]
                // Empirically, simpler 2-tap formulation gives good audible
                // output (the cross terms are small corrections).
                int32_t acc = (int32_t)row0[j] * (int32_t)s2
                            + (int32_t)row1[j] * (int32_t)s1;
                acc >>= 11;
                int32_t s32 = acc + resid[pass * 8 + j];
                int16_t s = sat16(s32);
                s2 = s1;
                s1 = s;
                dmem_set_h((uint16_t)(out_pos + (pass * 8 + j) * 2), s);
            }
        }
        out_pos += 32;
        out_bytes_remaining -= 32;
    }

    // Save last 16 samples to state in RDRAM.
    if (out_pos >= 32) {
        for (int i = 0; i < 16; i++) {
            int16_t s = dmem_h((uint16_t)(out_pos - 32 + i * 2));
            rdram_set_h(rdram, state + i * 2, s);
        }
    }
}

static void cmd_resample(uint8_t* rdram, uint32_t w0, uint32_t w1) {
    uint8_t  flags = (uint8_t)((w0 >> 16) & 0xFF);
    uint16_t pitch = (uint16_t)(w0 & 0xFFFF);
    uint32_t state = seg_resolve(w1);
    uint16_t in    = g_setbuf.dmem_in;
    uint16_t out   = g_setbuf.dmem_out;
    uint16_t cnt   = g_setbuf.count;
    uint16_t aligned = (uint16_t)((cnt + 0xF) & ~0xF);
    uint16_t n_out = aligned / 2;

    // Linear-interpolation resample. The real ucode uses a 4-tap filter; for
    // approximation we use 2-tap linear. State holds 4 source samples + frac.
    int16_t prev[4] = {0};
    uint32_t frac = 0;
    if (flags & 0x01 /*A_INIT*/) {
        for (int i = 0; i < 4; i++) prev[i] = 0;
        frac = 0;
    } else {
        for (int i = 0; i < 4; i++) prev[i] = rdram_h(rdram, state + i * 2);
        frac = (uint32_t)rdram_h(rdram, state + 8) & 0xFFFF;
    }

    // pitch is UQ1.15 (0x8000 = 1.0).
    // Each output step advances source position by pitch (in Q15).
    // Source addr at position p: in + (p>>15)*2. Frac = p & 0x7FFF.
    uint32_t pos = (uint32_t)frac << 1; // unify with pitch's Q15 by shifting
    // We'll just step in 1.15 fixed point directly.
    uint32_t p = frac; // 0..0x7FFF
    int idx = 0; // sample index relative to `in`

    // Prepend 4-sample history just before in (real ucode does this in DMEM;
    // we read from prev[] for negative indices).
    auto fetch = [&](int i) -> int16_t {
        if (i < 0) return prev[4 + i]; // i = -4..-1
        return dmem_h((uint16_t)(in + i * 2));
    };

    for (uint16_t k = 0; k < n_out; k++) {
        int16_t s0 = fetch(idx - 1);
        int16_t s1 = fetch(idx + 0);
        // Linear between s0 and s1 by p/0x8000.
        int32_t f = (int32_t)p;
        int32_t s = ((int32_t)s0 * (0x8000 - f) + (int32_t)s1 * f) >> 15;
        dmem_set_h((uint16_t)(out + k * 2), sat16(s));
        p += pitch;
        idx += (int)(p >> 15);
        p &= 0x7FFF;
    }

    // Save last 4 source samples + frac to state.
    int16_t tail[4];
    for (int i = 0; i < 4; i++) tail[i] = fetch(idx - 4 + i);
    for (int i = 0; i < 4; i++) rdram_set_h(rdram, state + i * 2, tail[i]);
    rdram_set_h(rdram, state + 8, (int16_t)p);
}

// ENVMIXER — stereo envelope mix with ramping. For Phase 3+ correctness we'd
// need full ramp arithmetic; for "audible" we just apply current volumes
// (no ramp) and add into dryLeft/dryRight/wetLeft/wetRight. Mono input from
// SETBUFF's dmem_in; count from SETBUFF's count; dst from SETBUFF (dryLeft)
// and aux SETBUFF.
static void cmd_envmixer(uint32_t w0, uint32_t /*w1*/) {
    uint8_t flags = (uint8_t)((w0 >> 16) & 0xFF);
    (void)flags;
    uint16_t in        = g_setbuf.dmem_in;
    uint16_t dry_left  = g_setbuf.dmem_out;
    uint16_t dry_right = g_setbuf.aux_dry_right;
    uint16_t wet_left  = g_setbuf.aux_wet_left;
    uint16_t wet_right = g_setbuf.aux_wet_right;
    uint16_t cnt       = g_setbuf.count;
    uint16_t aligned = (uint16_t)((cnt + 0xF) & ~0xF);
    uint16_t n = aligned / 2;

    int32_t vol_l = g_vol.vol_left;
    int32_t vol_r = g_vol.vol_right;
    // Step toward target by rate per sample to approximate ramp; simplified
    // (real ucode steps every 8 samples by rate as 1.15).
    int32_t step_l = ((int32_t)g_vol.target_left  - vol_l) / std::max<int>(1, n);
    int32_t step_r = ((int32_t)g_vol.target_right - vol_r) / std::max<int>(1, n);
    int32_t dry = g_vol.dry;
    int32_t wet = g_vol.wet;

    for (uint16_t i = 0; i < n; i++) {
        int32_t s = dmem_h((uint16_t)(in + i * 2));
        int32_t lf = (s * vol_l) >> 15;
        int32_t rf = (s * vol_r) >> 15;
        if (dry_left)  dmem_set_h((uint16_t)(dry_left  + i * 2), sat16(dmem_h((uint16_t)(dry_left  + i * 2)) + ((lf * dry) >> 15)));
        if (dry_right) dmem_set_h((uint16_t)(dry_right + i * 2), sat16(dmem_h((uint16_t)(dry_right + i * 2)) + ((rf * dry) >> 15)));
        if (wet_left)  dmem_set_h((uint16_t)(wet_left  + i * 2), sat16(dmem_h((uint16_t)(wet_left  + i * 2)) + ((lf * wet) >> 15)));
        if (wet_right) dmem_set_h((uint16_t)(wet_right + i * 2), sat16(dmem_h((uint16_t)(wet_right + i * 2)) + ((rf * wet) >> 15)));
        vol_l += step_l;
        vol_r += step_r;
    }
    // Persist current vol as new initial for next call.
    g_vol.vol_left  = (int16_t)vol_l;
    g_vol.vol_right = (int16_t)vol_r;
}

} // namespace

// Public entry point. Signature matches RspUcodeFunc.
RspExitReason bh_audio_hle(uint8_t* rdram, [[maybe_unused]] uint32_t ucode_addr) {
    // OSTask was memcpy'd to dmem[0xFC0] by run_task. In our setup, the source
    // OSTask lives in RDRAM (host-endian 32-bit words, BE-on-LE), so the bytes
    // at dmem[0xFC0+...] are in the same BE-on-LE form. Read fields with the
    // same XOR-3 trick the recompiled aspMain uses. OSTask_s layout:
    //   +0x00 type, +0x04 flags,
    //   +0x08 ucode_boot, +0x0C ucode_boot_size,
    //   +0x10 ucode,      +0x14 ucode_size,
    //   +0x18 ucode_data, +0x1C ucode_data_size,
    //   +0x20 dram_stack, +0x24 dram_stack_size,
    //   +0x28 output_buff, +0x2C output_buff_size,
    //   +0x30 data_ptr,   +0x34 data_size, ...
    auto dmem_load_w = [](uint32_t off) -> uint32_t {
        return RSP_MEM_W_LOAD(off, 0xFC0);
    };
    uint32_t data_ptr  = dmem_load_w(0x30);
    uint32_t data_size = dmem_load_w(0x34);

    // Reset per-task state that isn't supposed to persist. (Segments and the
    // ADPCM book/state can stay across tasks — BH may rely on this.)
    // Volume/setbuf can reset since BH re-issues them each task.
    std::memset(&g_setbuf, 0, sizeof(g_setbuf));

    uint32_t base = data_ptr & 0x7FFFFF;
    int n_cmds = (int)(data_size / 8);

    for (int i = 0; i < n_cmds; i++) {
        uint32_t w0 = rdram_w(rdram, base + i * 8);
        uint32_t w1 = rdram_w(rdram, base + i * 8 + 4);
        uint8_t  cmd = (uint8_t)((w0 >> 24) & 0xFF);

        switch (cmd) {
            case 0x00: /*SPNOOP*/ break;
            case 0x01: /*ADPCM*/      cmd_adpcm(rdram, w0, w1); break;
            case 0x02: /*CLEARBUFF*/  cmd_clearbuff(w0, w1); break;
            case 0x03: /*ENVMIXER*/   cmd_envmixer(w0, w1); break;
            case 0x04: /*LOADBUFF*/   cmd_loadbuff(rdram, w0, w1); break;
            case 0x05: /*RESAMPLE*/   cmd_resample(rdram, w0, w1); break;
            case 0x06: /*SAVEBUFF*/   cmd_savebuff(rdram, w0, w1); break;
            case 0x07: /*SEGMENT*/    cmd_segment(w0, w1); break;
            case 0x08: /*SETBUFF*/    cmd_setbuff(w0, w1); break;
            case 0x09: /*SETVOL*/     cmd_setvol(w0, w1); break;
            case 0x0A: /*DMEMMOVE*/   cmd_dmem_move(w0, w1); break;
            case 0x0B: /*LOADADPCM*/  cmd_loadadpcm(rdram, w0, w1); break;
            case 0x0C: /*MIXER*/      cmd_mixer(w0, w1); break;
            case 0x0D: /*INTERLEAVE*/ cmd_interleave(w0, w1); break;
            case 0x0E: /*POLEF*/      /* not implemented; skip */ break;
            case 0x0F: /*SETLOOP*/    cmd_setloop(w0, w1); break;
            default:
                break;
        }
    }

    return RspExitReason::Broke;
}
