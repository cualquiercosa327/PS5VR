/*
 * The engine's toast() messages ("OPEN FAIL", "SLOW FILE", ...). the engine drew them
 * through its RmlUi layer; the PS5VR Player logs them, and keeps the latest
 * one for its own controls to show.
 */
#include "eng_toast.h"

#include "eng_boot_log.h"
#include "eng_boot_trace.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static char s_title[64];
static char s_msg[192];
static double s_until;

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void toast(const char *title, const char *msg)
{
    snprintf(s_title, sizeof s_title, "%s", title ? title : "");
    snprintf(s_msg, sizeof s_msg, "%s", msg ? msg : "");
    s_until = now_s() + 3.0;
    eng_bt("toast: %s - %s", s_title, s_msg);
}

void draw_prospero_toast(uint32_t *fb)
{
    (void)fb;
}

int eng_toast_visible(void)
{
    return now_s() < s_until;
}
