// Overscan-safe-area override.
//
// The game configures the VI output to 0x130 x 0xE6 = 304x230 pixels, the
// standard libultra anti-overscan crop. CRTs hid the outer edges; modern
// displays don't, leaving black bars on the right and bottom. Emit a full
// 320x240 framebuffer instead.

#include "patches.h"

extern void setVideoInterfaceXSize(int width);
extern void setVideoInterfaceYSize(int height);

RECOMP_PATCH void setGameplayResolution(void) {
    setVideoInterfaceXSize(0x140);  // 320 (was 0x130 / 304)
    setVideoInterfaceYSize(0xF0);   // 240 (was 0xE6 / 230)
}
