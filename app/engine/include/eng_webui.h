/*
 * eng_webui.h - a provider's own web UI in the PS5's system browser (#101).
 *
 * libSceWebBrowserDialog, opened beside the engine's nav rail. With an upstream set,
 * the engine reverse-proxies the site on 127.0.0.1 and injects a small hook that
 * sends the site's own player's stream to the engine's player instead; after
 * playback the engine reopens the page the user was on. src/eng_webui.c has the
 * details and the hardware history; docs/research/web-browser-dialog.md the
 * write-up.
 *
 * App module only; every other build gets the no-op inlines below.
 */
#ifndef ENG_WEBUI_H
#define ENG_WEBUI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(ENG_APP_MODULE)

/* Pre-unjail slot, next to eng_keyboard_ime_probe(): sceSysmoduleLoadModule
 * stops working after eng_jailbreak_self(), so the dialog's module has to be
 * loaded there even though nothing opens it until much later. */
void eng_webui_preload(void);

/* Once per frame from the main loop: opens, pumps and closes the dialog and
 * drains the proxy threads' log lines. Also honours the dev trigger file
 * /mnt/usb0/eng_web_probe on the first call. */
void eng_webui_pump(void);

/*
 * Open `upstream` (http[s]://<host>[:<port>], no path) through the proxy at
 * `path` (e.g. "/web/index.html"). Returns 0 when the dialog is on its way
 * (it opens a few frames later), 1 when a session is already running (it is
 * left alone), and negative when `upstream` does not parse. The browser always
 * talks plain HTTP to the loopback proxy; the proxy speaks TLS upstream.
 */
int  eng_webui_open(const char *upstream, const char *path);

/* eng_webui_open() with a hook profile: NULL for the media-server hook
 * (Emby/Jellyfin), "ps5vr" for PS5VR's player. */
int  eng_webui_open_ex(const char *upstream, const char *path, const char *hook_profile);

/* 1 while the system browser is up: the engine must not act on pad input. */
int  eng_webui_active(void);

/* Application::shutdown(): stop the proxy and terminate the subsystem. */
void eng_webui_shutdown(void);

/* 1 from eng_webui_open() until the user closes the browser - including while
 * the engine's player has a stream the page handed over and the page is waiting to
 * be reopened. */
int  eng_webui_session_active(void);

/* A page handed over a stream and the dialog has closed. Returns 1 once per
 * handoff with the URL to open and the title to show; the caller starts
 * playback and later reports back with eng_webui_playback_ended() (played = 0
 * when it never started), which reopens the page. */
int  eng_webui_take_play(char *url, size_t url_cap, char *title, size_t title_cap);
void eng_webui_playback_ended(int played);

#else

static inline void eng_webui_preload(void) {}
static inline void eng_webui_pump(void) {}
static inline int  eng_webui_open(const char *u, const char *p) { (void)u; (void)p; return -1; }
static inline int  eng_webui_open_ex(const char *u, const char *p, const char *h)
{ (void)u; (void)p; (void)h; return -1; }
static inline int  eng_webui_active(void) { return 0; }
static inline void eng_webui_shutdown(void) {}
static inline int  eng_webui_session_active(void) { return 0; }
static inline int  eng_webui_take_play(char *u, size_t uc, char *t, size_t tc)
{ (void)u; (void)uc; (void)t; (void)tc; return 0; }
static inline void eng_webui_playback_ended(int played) { (void)played; }

#endif

#ifdef __cplusplus
}
#endif

#endif /* ENG_WEBUI_H */
