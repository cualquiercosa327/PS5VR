/*
 * Module: eng_data_path - see include/eng_data_path.h.
 */

#include "eng_data_path.h"
#include "eng_jailbreak.h"   /* eng_jailbreak_is_open() - (1) on non-app builds */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef ENG_APP_MODULE
/* libc's mkdir() is not in the clean-room libc.prx surface (imports.txt lists
 * sceKernelMkdir, not the POSIX name). Go straight to the libkernel export. */
int sceKernelMkdir(const char *path, int mode);
#endif

/* Resolved data root. Only cached once it is the durable root; while the app
 * sandbox is still shut resolve_root() keeps returning the fallback WITHOUT
 * caching, so a later self-unjail (or eng_data_path_rebind()) heals it. */
static const char *g_root;

static const char *resolve_root(void)
{
    if (g_root)
        return g_root;

#ifdef ENG_APP_MODULE
    if (!eng_jailbreak_is_open())
        return ENG_DATA_DIR;              /* "/download0/engine" - transient */
    g_root = "/data/engine";
#else
    /*
     * #90: the host renderer needs a writable data root somewhere other than
     * /data, so a provider bundle and its config can be staged in a temp
     * directory and the provider screen exercised with no console
     * (tools/uiview_playback_rml.sh).
     *
     * Host builds only - inside the #else, so the app module cannot be talked
     * into writing somewhere else by an environment it does not control.
     */
    const char *over = getenv("ENG_DATA_DIR_OVERRIDE");
    if (over && *over) {
        static char buf[512];
        snprintf(buf, sizeof buf, "%s", over);
        g_root = buf;
        return g_root;
    }
    g_root = ENG_DATA_DIR;                /* "/data/engine" - always */
#endif
    return g_root;
}

void eng_data_path_rebind(void)
{
    g_root = NULL;
}

const char *eng_data_dir(void)
{
    return resolve_root();
}

const char *eng_data_path(const char *leaf)
{
    static _Thread_local char buf[512];
    const char *root = resolve_root();

    if (!leaf)
        leaf = "";
    while (*leaf == '/')
        leaf++;

    int n = snprintf(buf, sizeof buf, "%s/%s", root, leaf);
    if (n < 0 || (size_t)n >= sizeof buf)
        return root;

    return buf;
}

int eng_mkdir(const char *path)
{
#ifdef ENG_APP_MODULE
    int r = sceKernelMkdir(path, 0777);
    if (r == 0 || r == (int)0x80020011 /* SCE_KERNEL_ERROR_EEXIST */)
        return 0;
    return r;
#else
    if (mkdir(path, 0777) == 0 || errno == EEXIST)
        return 0;
    return -1;
#endif
}
