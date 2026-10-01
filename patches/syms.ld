__start = 0x80000000;

/* Game functions are intentionally left undefined here: N64Recomp resolves the
 * resulting undefined references against bh.us.syms.toml. */

/* Dummy addresses that get recompiled into calls to host functions. */
recomp_load_overlays = 0x8F000090;
osRecvMesg_recomp = 0x8F000010;
osCreateMesgQueue_recomp = 0x8F000048;
osContGetReadData_recomp = 0x8F000074;
osSetTimer_recomp = 0x8F000098;
recomp_get_target_aspect_ratio = 0x8F0000A0;

/* Game data symbols come from bh.us.datasyms.toml. */
