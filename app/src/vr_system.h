/*
 * vr_system - PS VR2 system features besides the display: hand tracking
 * (libSceVrHand).
 *
 * Not here: libSceVrSetupDialog. LoginMgr refuses it with "invalid video out"
 * whatever the param holds - it is the PS VR (HMU) setup; on PS VR2 the fit
 * adjustment is in the system's quick menu, over any app.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Loads the libraries; with the other modules, before the sandbox opens. */
void vr_system_preload(void);
/* Once per headset frame. */
void vr_system_tick(void);
/* Stops what runs, before the app ends. */
void vr_system_shutdown(void);
/* Asks ShellUI to close the app (as the PS menu's Close does). 0 when asked. */
int vr_system_exit_app(void);

/* Hand tracking: 1 on, 0 off, 2 save the next result to /data/ps5vr/hand.bin. */
void vr_hands_request(int what);
int  vr_hands_on(void);

/* OpenXR joint order. */
enum {
    VR_HAND_PALM = 0, VR_HAND_WRIST = 1, VR_HAND_THUMB_TIP = 5, VR_HAND_INDEX_PROXIMAL = 7,
    VR_HAND_INDEX_TIP = 10, VR_HAND_MIDDLE_PROXIMAL = 12, VR_HAND_JOINTS = 26
};
typedef struct {
    int tracked;
    float joint[VR_HAND_JOINTS][3];   /* tracker space, metres */
    float radius[VR_HAND_JOINTS];
} vr_hand_t;
/* The latest pose of hand 0 or 1; 0 when it is not tracked. */
int vr_hands_get(int hand, vr_hand_t *out);

#ifdef __cplusplus
}
#endif
