#ifndef __MISC_FUNCS_H__
#define __MISC_FUNCS_H__

// Host functions implemented in src/game/recomp_api.cpp. Their addresses are
// assigned in syms.ld.
void recomp_load_overlays(unsigned int rom, void* ram, unsigned int size);
float recomp_get_target_aspect_ratio(float original);

#endif
