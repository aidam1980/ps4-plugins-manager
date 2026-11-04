#include "jbc_api.h"

// Keep the low-level library isolated from the UI translation unit. This
// wrapper returns a distinct negative code for each initialization stage.
extern "C" int GoldenPluginE_JbcBootstrap(void)
{
    jbc_cred cred = {};

    if (jbc_get_cred(&cred) != 0)
        return -1;
    if (jbc_jailbreak_cred(&cred) != 0)
        return -2;
    if (jbc_set_cred(&cred) != 0)
        return -3;

    return 0;
}
