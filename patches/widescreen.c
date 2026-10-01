// Keep the frontend (intro, title and menus) at 4:3 when RT64 widens the view.
//
// The frontend scenes use 4:3 2D backgrounds and park 3D models (e.g. the
// intro's abducting alien) just outside the 4:3 frame, where a real N64 never
// shows them. While the frontend overlay is loaded, start each frame's display
// list by anchoring viewports to the screen centre with RT64's extended GBI:
// RT64 then draws the 3D at 4:3 instead of widening it. Gameplay is untouched
// and stays widescreen.

#include "patches.h"
#include "rt64_extended_gbi.h"

typedef struct {
    short scale[4];
    short trans[4];
} Vp;

// Index of the framebuffer being drawn.
extern int D_80031B84;
// One viewport per framebuffer.
extern Vp D_80031B60[];
// Per-frame buffer pointers, all carved out of the current frame's buffer.
extern char* D_8005BB20;
extern Vp* D_8005BB24;
extern char* D_8005BB28;
extern GfxCommand* D_8005BB2C; // Display list write pointer.
extern char* D_8005BB30;
extern char* D_8005BB34;
extern char* D_8005BB38;
extern char* D_8005BB3C;
extern char* D_8005BB40;
// VI output size in pixels.
extern int D_80068084;
extern int D_80068088;

extern int bh_frontend_loaded;

#define FRAME_BUFFERS_END 0x801CE710
#define FRAME_BUFFER_SIZE 0x22B00

// Frame start: point the per-frame allocators at this frame's buffer and set
// the full-screen viewport.
RECOMP_PATCH void func_8000F368_FF68(void) {
    int fb = D_80031B84;
    char* base = (char*)(FRAME_BUFFERS_END - fb * FRAME_BUFFER_SIZE);
    Vp* vp = &D_80031B60[fb];

    D_8005BB20 = base;
    D_8005BB28 = base;
    D_8005BB3C = base + 0x180;
    D_8005BB40 = base + 0x200;
    D_8005BB2C = (GfxCommand*)(base + 0x280);
    D_8005BB30 = base + 0xE380;
    D_8005BB34 = base + 0xF500;
    D_8005BB38 = base + 0x1E280;

    D_8005BB24 = vp;
    vp->scale[0] = D_80068084 * 2;
    vp->scale[1] = D_80068088 * 2;
    vp->trans[0] = D_80068084 * 2;
    vp->trans[1] = D_80068088 * 2;

    if (bh_frontend_loaded) {
        GfxCommand* dl = D_8005BB2C;
        gEXEnable(dl++);
        // Viewport translations are in 10.2 fixed point and get moved by half
        // the screen width when anchored to the centre; undo that shift.
        gEXSetViewportAlign(dl, G_EX_ORIGIN_CENTER, -D_80068084 * 2, 0);
        dl += 2;
        D_8005BB2C = dl;
    }
}
