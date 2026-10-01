// Extend the gameplay sky's cloud band into the widescreen margins.
//
// The sky is two fill rectangles (above and below the horizon) plus a
// scrolling 640 px wide cloud panorama drawn as 2D texture tiles that the game
// clips to the 4:3 screen. RT64 widens the full-width fill rectangles but keeps
// the tiles at 4:3, so the cloud band's rows in the widescreen margins were
// never drawn and showed stale framebuffer contents. Draw the band once more on
// each side, shifted by one 4:3 screen width with RT64's rect alignment, under
// a scissor that spans the whole window.

#include "patches.h"
#include "rt64_extended_gbi.h"

#define SCISSOR_NON_INTERLACE 0
#define SKY_TEXTURE ((void*)0x802CA8D0)
#define BAND_WIDTH 320
#define BAND_HEIGHT 128
#define BAND_PERIOD (BAND_WIDTH * 2)
// func_80005C5C_685C clips its tiles to x < 319, so the margin copies are
// shifted by that much to meet the centre copy without a gap.
#define CLIP_WIDTH 319

typedef struct {
    unsigned char r, g, b;
} SkyColor;

extern int D_80047F90;          // Sky mode; 2 may draw a solid colour instead.
extern SkyColor D_8004773C;     // Colour below the cloud band.
extern SkyColor D_80047740;     // Colour above the cloud band.
extern unsigned char D_80047748[]; // Cloud band palette.
extern short D_8004794E;        // Camera pitch.
extern short D_80047964;        // Camera yaw.
extern short* D_80052B34;       // Optional horizon shift source (field at +2).
extern int D_8003161C;          // Horizon base row.
extern float D_80031618;        // Cloud band vertical scale.
extern int D_80068088;          // VI output height.
extern GfxCommand* D_8005BB2C;  // Display list write pointer.

int func_8000726C_7E6C(int a0, int a1);
void func_80004DDC_59DC(int r, int g, int b, int uly, int lry);
void func_80005C5C_685C(void* tex, int fmt, int siz, int bits, int x, int y, int width, int height,
                        float scale_x, float scale_y, void* tlut);

// Draws the periodic cloud band so that it covers the 4:3 screen for a scroll
// position of x.
static void draw_band(int x, int y, void* tlut) {
    x %= BAND_PERIOD;
    if (x < 0) {
        x += BAND_PERIOD;
    }
    func_80005C5C_685C(SKY_TEXTURE, 2, 1, 8, x, y, BAND_WIDTH, BAND_HEIGHT, 2.0f, D_80031618, tlut);
    func_80005C5C_685C(SKY_TEXTURE, 2, 1, 8, x - BAND_PERIOD, y, BAND_WIDTH, BAND_HEIGHT, 2.0f, D_80031618, 0);
}

// Draws the band shifted sideways by one 4:3 screen width (in pixels).
static void draw_band_margin(int scroll, int y, int shift) {
    GfxCommand* dl = D_8005BB2C;
    gEXSetRectAlign(dl, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, shift * 4, 0, shift * 4, 0);
    D_8005BB2C = dl + 2;
    draw_band(scroll - shift, y, 0);
}

RECOMP_PATCH void func_800069FC_75FC(void) {
    int bottom = D_80068088 - 1;

    if (D_80047F90 == 2 && func_8000726C_7E6C(0, 30) == 0) {
        func_80004DDC_59DC(D_8004773C.r, D_8004773C.g, D_8004773C.b, 0, bottom);
        return;
    }

    int scroll = ((((unsigned short)(D_80047964 * 2)) >> 2) * 320) >> 13;
    float shift = (D_80052B34 != 0) ? (float)D_80052B34[1] : 0.0f;
    int horizon = (int)((float)(D_8004794E / 36 + D_8003161C) + shift / 15.0f);

    if (horizon >= 2) {
        func_80004DDC_59DC(D_80047740.r, D_80047740.g, D_80047740.b, 0, horizon - 1);
    }
    float band_bottom = (float)horizon + 128.0f * D_80031618;
    if (band_bottom < (float)bottom) {
        func_80004DDC_59DC(D_8004773C.r, D_8004773C.g, D_8004773C.b, (int)band_bottom, bottom);
    }

    draw_band(scroll, horizon, D_80047748);

    GfxCommand* dl = D_8005BB2C;
    gEXEnable(dl++);
    gEXPushScissor(dl++);
    gEXSetScissor(dl, SCISSOR_NON_INTERLACE, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, D_80068088);
    D_8005BB2C = dl + 2;

    draw_band_margin(scroll, horizon, -CLIP_WIDTH);
    draw_band_margin(scroll, horizon, CLIP_WIDTH);

    dl = D_8005BB2C;
    gEXSetRectAlign(dl, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
    dl += 2;
    gEXPopScissor(dl++);
    D_8005BB2C = dl;
}
