/*
 * eng_provider_bundle.h — provider UI bundles: format, fetch, cache, limits.
 *
 * A bundle is the "look" half of a provider (see eng_provider.h): the markup
 * and styling the service authors for itself, fetched over HTTP at runtime and
 * cached under /data/engine/providers/<id>/. Emby looks like Emby; IPTV is
 * a channel grid. the engine renders it and supplies the data; it does not design it.
 *
 * WHY THIS IS SAFE TO DOWNLOAD
 *
 * A bundle contains no code. RmlUi's scripting is off and stays off, so the
 * only things in here are .rml, .rcss, fonts and images. What a hostile or
 * merely broken bundle can still do is escape its directory, exhaust memory,
 * or mis-render silently on the sceAgc backend - and those are what the limits
 * and the validation pass below exist for. Every one of them is enforced, not
 * assumed:
 *
 *   - every path is resolved and must land inside the provider's own cache
 *     directory. Absolute paths, "..", and any URI scheme are rejected before
 *     the file interface ever sees them.
 *   - entry count, per-file size and total size are capped at fetch time
 *     against the manifest, and again against what actually arrives.
 *   - documents, DOM nodes, fonts and texture bytes are capped at load time.
 *   - the manifest declares a minimum ENG_PROVIDER_API_VERSION; a bundle
 *     written against a newer seam is refused rather than half-rendered.
 *
 * Any failure at all drops to the embedded fallback skin with a visible
 * message. Never a blank screen, never a hang - #90 is explicit about that,
 * because a blank screen on a console with no console is unreportable.
 *
 * THE MANIFEST
 *
 *   {
 *     "id":          "iptv",
 *     "name":        "IPTV",
 *     "version":     "1.0.0",
 *     "api_version": 1,
 *     "entry":       "main.rml",
 *     "data_model":  "iptv",
 *     "assets": [
 *       { "path": "main.rml",  "bytes": 4210, "sha256": "<64 hex>" },
 *       { "path": "main.rcss", "bytes": 8817, "sha256": "<64 hex>" }
 *     ]
 *   }
 *
 * `api_version` is the MINIMUM seam the bundle needs. `data_model` is the name
 * the bundle's data-* attributes bind to, and must match what the provider's
 * host registers - a mismatch is the difference between a bound list and an
 * empty one, so it is checked rather than hoped for.
 *
 * Hashes are sha256 over the file bytes, lowercase hex. They are an integrity
 * check against a truncated download or a mangled cache, not an authenticity
 * one: the manifest arrives over the same connection as the files, so a
 * man-in-the-middle rewrites both. Use https for a bundle you care about.
 */
#ifndef ENG_PROVIDER_BUNDLE_H
#define ENG_PROVIDER_BUNDLE_H

#include <stddef.h>
#include <stdint.h>

#include "eng_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Limits                                                                    */
/* ------------------------------------------------------------------------- */
/*
 * These are the numbers, in one place, because "cap it" without a number is
 * how an unbounded growth bug gets written. Sized so that a bundle cannot
 * meaningfully move the app slot's memory ceiling: the whole cache is smaller
 * than one 4K frame.
 */
#define ENG_BUNDLE_MAX_ENTRIES      64          /* files in one bundle       */
#define ENG_BUNDLE_MAX_FILE_BYTES   (2u << 20)  /* 2 MiB, any single file    */
#define ENG_BUNDLE_MAX_TOTAL_BYTES  (8u << 20)  /* 8 MiB, whole bundle       */
#define ENG_BUNDLE_MAX_MANIFEST     (64u << 10) /* 64 KiB of manifest.json   */
#define ENG_BUNDLE_MAX_PATH         128         /* a path inside the bundle  */

/* Load-time caps, enforced by the RmlUi host (eng_rmlui_provider.cpp) rather
 * than here, but declared here so there is one list. */
#define ENG_BUNDLE_MAX_DOCUMENTS    4
#define ENG_BUNDLE_MAX_DOM_NODES    4000
#define ENG_BUNDLE_MAX_FONTS        4
#define ENG_BUNDLE_MAX_TEXTURE_BYTES (24u << 20) /* 24 MiB of decoded art    */

/* ------------------------------------------------------------------------- */
/* Types                                                                     */
/* ------------------------------------------------------------------------- */

typedef struct eng_bundle_entry {
    char     path[ENG_BUNDLE_MAX_PATH];  /* relative, forward slashes only  */
    uint32_t bytes;
    char     sha256[65];                 /* lowercase hex, or "" for none   */
} eng_bundle_entry_t;

typedef struct eng_bundle_manifest {
    char id[ENG_PROVIDER_MAX_ID];
    char name[64];
    char version[24];
    int  api_version;
    char entry[ENG_BUNDLE_MAX_PATH];     /* the .rml to LoadDocument        */
    char data_model[32];
    eng_bundle_entry_t entries[ENG_BUNDLE_MAX_ENTRIES];
    int  entry_count;
    uint64_t total_bytes;
} eng_bundle_manifest_t;

/*
 * Why a bundle is not usable. Every one of these must produce a visible
 * message, so they are an enum and not a bare -1: the fallback skin shows the
 * string from eng_bundle_status_str().
 */
typedef enum {
    ENG_BUNDLE_OK = 0,
    ENG_BUNDLE_ERR_NO_URL,          /* provider has no ui_bundle_url        */
    ENG_BUNDLE_ERR_FETCH,           /* HTTP failed or timed out             */
    ENG_BUNDLE_ERR_MANIFEST,        /* not JSON, or missing a required key  */
    ENG_BUNDLE_ERR_API_VERSION,     /* written against a newer seam         */
    ENG_BUNDLE_ERR_ID_MISMATCH,     /* manifest id is not the provider's    */
    ENG_BUNDLE_ERR_PATH,            /* absolute, "..", a scheme, or too long*/
    ENG_BUNDLE_ERR_TOO_MANY,        /* over ENG_BUNDLE_MAX_ENTRIES          */
    ENG_BUNDLE_ERR_TOO_BIG,         /* over a file or total byte cap        */
    ENG_BUNDLE_ERR_HASH,            /* sha256 did not match                 */
    ENG_BUNDLE_ERR_WRITE,           /* could not write the cache            */
    ENG_BUNDLE_ERR_INCOMPLETE       /* cache is missing a manifest entry    */
} eng_bundle_status_t;

const char *eng_bundle_status_str(eng_bundle_status_t st);

/* ------------------------------------------------------------------------- */
/* Paths                                                                     */
/* ------------------------------------------------------------------------- */

/*
 * The cache directory for `provider_id`, with no trailing slash:
 * "/data/engine/providers/iptv". Returns NULL if the id is not valid.
 * The buffer is per-thread static - use it immediately.
 */
const char *eng_bundle_dir(const char *provider_id);

/*
 * Join `rel` onto that directory, refusing anything that could escape it.
 * This is THE path check - the render backends and the file interface both
 * reach disk with a plain fopen(), so a bundle path that gets past here is a
 * bundle path that can read /data.
 *
 * Rejects: a leading '/', a leading drive letter, any "..", any backslash, any
 * "scheme:" prefix, an empty component, a component that is just dots, and
 * anything longer than ENG_BUNDLE_MAX_PATH. Returns 0 on success.
 */
int eng_bundle_path(const char *provider_id, const char *rel,
                    char *out, size_t out_sz);

/* 1 when `rel` passes the same rules, without building a path. */
int eng_bundle_path_safe(const char *rel);

/*
 * mkdir -p over the directory part of a path from eng_bundle_path(). Returns 0
 * when the directory exists afterwards.
 *
 * Anything writing into a provider's cache has to call this first: nothing else
 * creates the intermediate levels, so the data root, "providers", the provider's
 * own directory and any subdirectory below it may all be absent.
 */
int eng_bundle_ensure_parent_dirs(const char *path);

/* ------------------------------------------------------------------------- */
/* Fetch and cache                                                           */
/* ------------------------------------------------------------------------- */

/*
 * Read the cached manifest for `provider_id` and verify every entry it names
 * is present on disk at the right size. This is the offline and first-frame
 * path: it touches no network, so a provider screen can open instantly from
 * cache and refresh behind itself.
 */
eng_bundle_status_t eng_bundle_load_cached(const char *provider_id,
                                           eng_bundle_manifest_t *out);

/*
 * Fetch `base_url`'s manifest.json and every asset it lists, verify, and write
 * the result into the cache directory.
 *
 * SYNCHRONOUS, and therefore only callable from a worker - never from the
 * render thread. The async entry point that a screen uses is
 * eng_bundle_refresh_async().
 *
 * `base_url` is a directory URL; "manifest.json" and each entry path are
 * appended to it. An existing cache is replaced only once every file has
 * arrived and verified, so a failed refresh leaves the previous bundle intact
 * rather than a half-written one.
 */
eng_bundle_status_t eng_bundle_fetch_sync(const char *provider_id,
                                           const char *base_url,
                                           eng_bundle_manifest_t *out);

typedef void (*eng_bundle_cb)(eng_bundle_status_t st,
                              const eng_bundle_manifest_t *m,
                              void *ud);

/*
 * Refresh in the background and call back on the main thread from
 * eng_net_poll(). If the cached copy is already current - the manifest's
 * version and the stored ETag both match - the callback fires with
 * ENG_BUNDLE_OK and nothing is downloaded.
 *
 * Returns 0 if the refresh was started. One refresh per provider at a time; a
 * second call while one is in flight returns -2 and does not call back.
 */
int eng_bundle_refresh_async(const char *provider_id, const char *base_url,
                             eng_bundle_cb cb, void *ud);

/* Pump a completed refresh. Called from eng_net_poll()'s frame, alongside it. */
void eng_bundle_poll(void);

/* Drop the cache for one provider. Used by the settings row and by a
 * ENG_BUNDLE_ERR_HASH recovery, which otherwise loops on the bad cache. */
int eng_bundle_clear(const char *provider_id);

/* ------------------------------------------------------------------------- */
/* Remote artwork                                                            */
/* ------------------------------------------------------------------------- */
/*
 * Poster walls are the point of "the service's own look", so art is fetched
 * separately from the bundle - it is per-item, unbounded in count, and must
 * not block a row from appearing.
 *
 * A request returns the `engine:mem/<key>` name the markup should use for its
 * <img src>. Both render backends already resolve that registry before
 * touching disk (eng_rmlui_render.cpp LoadTexture, eng_rmlui_render_agc.cpp),
 * so nothing in the render path changes. Until the bytes arrive the name
 * resolves to nothing and the element draws empty, which is why the markup
 * wants a background colour behind its posters.
 *
 * The registry is an LRU under a byte cap; evicting a texture that is still on
 * screen just makes it draw empty again, which is why the cap is generous.
 */
/*
 * Ask for `url`'s artwork and receive the `engine:mem/...` key it will be
 * registered under. The key is stable for a given URL, so a caller can ask
 * once, keep the key, and poll eng_provider_art_ready() - it does not have to
 * hold the URL or re-request.
 *
 *   1  the texture is registered and drawable now
 *   0  queued; the key becomes drawable after a later eng_provider_art_poll()
 *  -1  the URL is unusable - do not ask for it again
 *  -2  the in-flight queue is full; nothing was started. Keep the URL and ask
 *      again on a later tick, and stop asking for other rows this tick.
 *
 * Neither negative case is worth surfacing: the element draws empty, which is
 * what it was already doing.
 */
int  eng_provider_art_request(const char *provider_id, const char *url,
                              char *out_key, size_t out_sz);

/* 1 when `key` is registered and drawable. */
int  eng_provider_art_ready(const char *key);

/* Complete arrived downloads: decode, register, evict under the byte cap.
 * Called once per frame from the provider host's Tick(). */
void eng_provider_art_poll(void);

void eng_provider_art_set_budget(size_t bytes);
void eng_provider_art_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* ENG_PROVIDER_BUNDLE_H */
