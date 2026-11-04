#pragma once

#include <stdint.h>

// ABI-compatible declaration of ps4-libjbc's public jbc_cred structure.
// The upstream library uses uid_t/gid_t as 32-bit values on PS4; using fixed
// width fields here avoids importing its freestanding defs.h into the UI C++.
struct jbc_cred
{
    uint32_t uid;
    uint32_t ruid;
    uint32_t svuid;
    uint32_t rgid;
    uint32_t svgid;
    uintptr_t prison;
    uintptr_t cdir;
    uintptr_t rdir;
    uintptr_t jdir;
    uint64_t sceProcType;
    uint64_t sonyCred;
    uint64_t sceProcCap;
};

#if defined(__cplusplus)
static_assert(sizeof(uintptr_t) == 8, "libjbc requires the PS4 64-bit ABI");
static_assert(sizeof(jbc_cred) == 80, "Unexpected libjbc credential structure layout");
extern "C" {
#endif
int jbc_get_cred(struct jbc_cred *ans);
int jbc_jailbreak_cred(struct jbc_cred *ans);
int jbc_set_cred(const struct jbc_cred *ans);
#if defined(__cplusplus)
}
#endif
