#ifndef ENG_DATA_PATH_H
#define ENG_DATA_PATH_H

/*
 * eng_data_path - where the engine's persistent data lives.
 *
 * As an elfldr payload the engine runs unsandboxed and writes to /data/engine/.
 * As the PPSA99039 app module the process launches into a fresh per-title
 * sandbox: /data is ENOENT until eng_jailbreak_self() lifts it. Once lifted,
 * /data/engine/ is the durable home - the same as the payload.
 * /download0/engine/ is only a fallback for the window before the self-unjail
 * lands (or if it never does); it is a savedata-relative mount with no
 * sceSaveDataMount2/commit behind it, so writes there do NOT survive a
 * relaunch (issue #46).
 *
 * The root therefore has to be picked at RUNTIME, not compile time:
 *   - payload / host build:            always /data/engine
 *   - app module, sandbox open:        /data/engine   (cached once resolved)
 *   - app module, sandbox still shut:  /download0/engine (transient, not cached)
 *
 * eng_data_dir() / eng_data_path() do that resolution. Nothing else should
 * reference ENG_DATA_DIR - it survives only as the fallback literal inside
 * eng_data_path.c.
 */

#ifdef ENG_APP_MODULE
#define ENG_DATA_DIR "/download0/engine"   /* fallback only - see above */
#else
#define ENG_DATA_DIR "/data/engine"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The data root, no trailing slash. Resolved once, lazily; on the app module
 * it stays on the /download0 fallback (uncached) until the sandbox opens, then
 * pins to /data/engine. */
const char *eng_data_dir(void);

/*
 * Join `leaf` onto the data root: eng_data_path("emby.conf") ->
 * "/data/engine/emby.conf". A leading '/' on `leaf` is ignored.
 *
 * Returns a pointer to a per-thread static buffer - use it immediately (as an
 * fopen()/open() argument, say); do not stash it or pass two results of this
 * call into the same expression. On overflow it returns eng_data_dir().
 */
const char *eng_data_path(const char *leaf);

/*
 * Drop the cached root so the next eng_data_dir()/eng_data_path() re-resolves.
 * Call after a late eng_jailbreak_ensure() flips the sandbox open - config
 * loaded from the /download0 fallback then needs to move to /data (issue #46).
 */
void eng_data_path_rebind(void);

/*
 * mkdir() that works in the app-module sandbox. The clean-room libc.prx surface
 * exposes sceKernelMkdir, not the POSIX name (same gap that made eng_readdir.c
 * wrap getdents(2) directly), so the app build goes straight to the libkernel
 * export. Returns 0 on success or if the directory already exists.
 */
int eng_mkdir(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* ENG_DATA_PATH_H */
