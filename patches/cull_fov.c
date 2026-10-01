// Widescreen object and terrain culling.
//
// func_800B33BC_C236C turns the camera's field of view into the horizontal
// view angle (D_8014FD2A) that the visibility tests (func_800B9228_C81D8,
// func_800B93AC_C835C, func_800B960C_C85BC) cull against. It is computed for
// the 4:3 screen, so anything in the widescreen margins got culled. Widen the
// angle to the aspect ratio the game is presented at.

#include "patches.h"
#include "misc_funcs.h"

#define ORIGINAL_ASPECT_RATIO (4.0f / 3.0f)
#define FOV_NO_CULL_THRESHOLD 0x2E39
#define FULL_CIRCLE_HALF 0x8000

extern float D_80142E20;
extern float D_80142E24;
extern unsigned short D_8014FD2A;  // Horizontal view angle used for culling.

short sins(unsigned short angle);
short coss(unsigned short angle);
short func_80003740_4340(float x);  // atan, returns a binary angle.

// The original uses libultra's sinf/cosf, but patches can't call them: the
// patch recompiler emits their plain names, which clash with the C library.
// The 16-bit binary angle versions are accurate enough for culling.
static unsigned short radians_to_binang(float radians) {
    return (unsigned short)(int)(radians * (32768.0f / 3.14159265f));
}

RECOMP_PATCH void func_800B33BC_C236C(unsigned short fov) {
    float ratio = (float)sins(radians_to_binang(D_80142E24)) / (float)coss(radians_to_binang(D_80142E20));
    float half_tan = (float)((double)ratio / ((double)coss(fov) / 32768.0));
    half_tan *= recomp_get_target_aspect_ratio(ORIGINAL_ASPECT_RATIO) / ORIGINAL_ASPECT_RATIO;

    D_8014FD2A = func_80003740_4340(half_tan) * 2;
    if ((short)fov >= FOV_NO_CULL_THRESHOLD) {
        D_8014FD2A = FULL_CIRCLE_HALF;
    }
}
