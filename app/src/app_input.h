#pragma once
/*
 * Controller input for the PS5VR Player: the DualSense through scePad, plus
 * buttons injected by the payload's command channel ("key" commands), merged
 * into one stream of presses with auto-repeat for held directions.
 *
 * The pad is opened only while a stream plays: while the app holds it, the
 * browser dialog that shows PS5VR's page gets no input.
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* scePad button bits. */
enum {
    APP_BTN_L3       = 0x00000002,
    APP_BTN_R3       = 0x00000004,
    APP_BTN_OPTIONS  = 0x00000008,
    APP_BTN_UP       = 0x00000010,
    APP_BTN_RIGHT    = 0x00000020,
    APP_BTN_DOWN     = 0x00000040,
    APP_BTN_LEFT     = 0x00000080,
    APP_BTN_L2       = 0x00000100,
    APP_BTN_R2       = 0x00000200,
    APP_BTN_L1       = 0x00000400,
    APP_BTN_R1       = 0x00000800,
    APP_BTN_TRIANGLE = 0x00001000,
    APP_BTN_CIRCLE   = 0x00002000,
    APP_BTN_CROSS    = 0x00004000,
    APP_BTN_SQUARE   = 0x00008000,
    APP_BTN_TOUCHPAD = 0x00100000,

    APP_BTN_DPAD = APP_BTN_UP | APP_BTN_RIGHT | APP_BTN_DOWN | APP_BTN_LEFT,
};

typedef struct app_input_state {
    uint32_t pressed;    /* went down this poll (D-pad: also auto-repeats) */
    uint32_t released;   /* went up this poll */
    uint32_t held;       /* down now */
    uint32_t repeats;    /* the subset of pressed that is an auto-repeat */
    double   held_for;   /* seconds the current D-pad direction has been held */
} app_input_state;

/* Opens the user's controller. Presses already down are ignored until they
 * are released, so the button that started playback does not act twice. */
void app_input_open(int user_id);
void app_input_close(void);

/* A press from the command channel, held for hold_ms (0 = one tap). */
void app_input_inject(uint32_t button, int hold_ms);

/* Button bit for a command-channel name ("cross", "left", ...), 0 if unknown. */
uint32_t app_input_button_named(const char *name);

/* Reads the controller once; call every frame. */
void app_input_poll(app_input_state *out);

#ifdef __cplusplus
}
#endif
