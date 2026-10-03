/*
 * the engine features the vendored engine still calls but the PS5VR Player
 * has no use for. Each is the "not there" answer the engine already handles.
 */
#include "eng_provider.h"

#include <stddef.h>

/* Providers (Emby, IPTV, ...) are the engine's; PS5VR streams carry no provider
 * object, so progress reporting and link resolution are skipped. */
const eng_provider_t *eng_provider_find(const char *id)
{
    (void)id;
    return NULL;
}

/* Folder thumbnails are part of the engine's file browser, which PS5VR has none of. */
void prospero_thumbnail_close_context(void)
{
}

/* the engine's "recently played" list. PS5VR keeps watch progress itself (the
 * player reports the final position to the page), so nothing is stored. */
#include "eng_recent.h"

void recent_add_or_update(const char *path, const char *title, double last_pos, double duration)
{
    (void)path;
    (void)title;
    (void)last_pos;
    (void)duration;
}

void recent_save(void)
{
}

/* eng_jailbreak_is_open() lives in eng_jailbreak.c (PS5VR unjails itself through etaHEN). */
