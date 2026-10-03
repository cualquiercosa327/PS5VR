/*
 * eng_theme — the engine theming.
 *
 * Every colour and metric the UI draws with lives in one struct. Screens read
 * tokens by meaning ("surface", "accent", "text_muted") rather than naming
 * literal colours, so a theme swap restyles the whole player without touching
 * drawing code.
 *
 * Themes come from two places:
 *   1. built-ins, compiled in (see eng_theme.c)
 *   2. .theme files dropped in /mnt/usb0/eng_themes/ - plug and play, no
 *      rebuild, discovered at startup
 *
 * Colours are 0xAABBGGRR to match the framebuffer and RR_BGRA.
 */
#ifndef ENG_THEME_H
#define ENG_THEME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* r,g,b,a in the natural order, packed for the framebuffer. */
#define ENG_RGBA(r, g, b, a)                                                  \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) |                          \
     ((uint32_t)(g) << 8)  |  (uint32_t)(r))

#define ENG_THEME_NAME_MAX 24
#define ENG_THEME_MAX      12   /* built-ins + files from USB */

typedef struct eng_theme {
    char name[ENG_THEME_NAME_MAX];

    /* ---- surfaces ---------------------------------------------------- */
    uint32_t bg_top;         /* page background, top of gradient          */
    uint32_t bg_bottom;      /* page background, bottom                   */
    uint32_t scrim;          /* darkening laid over artwork/video         */

    uint32_t surface;        /* card fill, top of gradient                */
    uint32_t surface_alt;    /* card fill, bottom - subtle depth          */
    uint32_t surface_sel;    /* selected card, top                        */
    uint32_t surface_sel_alt;/* selected card, bottom                     */

    uint32_t border;         /* hairline around a resting card            */
    uint32_t border_sel;     /* hairline around the selected card         */
    uint32_t shadow;         /* drop shadow beneath cards (alpha matters) */

    /* ---- accents ------------------------------------------------------ */
    uint32_t accent;         /* primary accent: icons, rails, highlights  */
    uint32_t accent_soft;    /* the same accent as a diffuse glow         */
    uint32_t accent_alt;     /* secondary accent, used sparingly          */
    /*
     * Failure. Deliberately its own token rather than a tint of the accent:
     * a theme whose accent is already red would produce an error colour
     * indistinguishable from its normal one, and "something went wrong" is
     * the one message that must never be ambiguous.
     */
    uint32_t danger;

    /* ---- text ---------------------------------------------------------- */
    uint32_t text_primary;   /* titles, row labels                        */
    uint32_t text_secondary; /* descriptions                              */
    uint32_t text_muted;     /* footers, hints, disabled                  */

    /* ---- metrics (pixels, at 1080p) ------------------------------------- */
    int16_t radius;          /* card corner radius                        */
    int16_t border_px;       /* hairline width                            */
    int16_t shadow_px;       /* drop shadow spread                        */
    int16_t rail_px;         /* accent rail width on a selected card      */
    int16_t row_h;           /* card height                               */
    int16_t row_gap;         /* vertical gap between cards                */
    int16_t pad_x;           /* inner horizontal padding                  */
} eng_theme;

/* The theme currently in use. Never NULL. */
const eng_theme *eng_theme_current(void);

/* How many themes are available (built-ins + any found on USB). */
int eng_theme_count(void);

/* Name of theme `index`, for the settings row. NULL if out of range. */
const char *eng_theme_name(int index);

/* Index of the active theme. */
int eng_theme_index(void);

/* Activate a theme by index. Wraps around. Returns the index applied. */
int eng_theme_set(int index);

/*
 * Activate a theme by name - what settings persist, so that a USB theme
 * appearing or disappearing between boots cannot shift the user's choice onto
 * a different theme. Unknown names leave the current theme alone.
 */
int eng_theme_set_by_name(const char *name);

/*
 * Load built-ins, then scan /mnt/usb0/eng_themes/ for *.theme files.
 * Safe to call more than once; call before the first frame is drawn.
 */
void eng_theme_init(void);

/*
 * Forget the loaded set so the next eng_theme_init() re-scans the theme
 * directory. Used when the data root is rebound to a different filesystem
 * mid-run (issue #46) - otherwise the one-shot guard would keep the themes
 * discovered from the pre-unjail fallback path.
 */
void eng_theme_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ENG_THEME_H */
