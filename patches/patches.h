#ifndef __PATCHES_H__
#define __PATCHES_H__

#define RECOMP_EXPORT       __attribute__((section(".recomp_export")))
#define RECOMP_PATCH        __attribute__((section(".recomp_patch")))
#define RECOMP_FORCE_PATCH  __attribute__((section(".recomp_force_patch")))

// Declare a recomp event others can hook. Provides a weak no-op body.
#define RECOMP_DECLARE_EVENT(func) \
    __attribute__((noinline, weak, used, section(".recomp_event"))) void func {}

#endif
