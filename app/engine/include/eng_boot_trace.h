/*
 * eng_boot_trace.h - Phase 1b app-module bring-up breadcrumbs.
 *
 * #51: eng_bt("...") writes to three places whenever ENG_APP_MODULE is defined:
 *   - the single diagnostic log /mnt/usb0/engine.log (via eng_boot_log) — durable,
 *     the place to look after the fact;
 *   - the kernel log (tools/klog.sh) — visible live before VideoOut is up and
 *     without a USB stick;
 *   - a system notification, but only with ENG_BOOT_TRACE_POPUP
 *     (scripts/package-app.sh --breadcrumbs) — noise on the TV otherwise.
 *
 * Compiles to nothing outside the app module (host / payload builds).
 */
#ifndef ENG_BOOT_TRACE_H
#define ENG_BOOT_TRACE_H

#ifdef ENG_APP_MODULE

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "eng_boot_log.h"   /* eng_bt lines also land in /mnt/usb0/engine.log */

#ifdef __cplusplus
extern "C" {
#endif

struct eng_bt_req { char pad[45]; char msg[3075]; };
extern int sceKernelSendNotificationRequest(int, void *, unsigned long, int);
extern int sceKernelDebugOutText(int, const char *);   /* -> kernel log (klog.sh) */

static inline void eng_bt_(const char *fmt, ...)
{
    struct eng_bt_req r;
    char line[512];
    va_list ap;

    memset(&r, 0, sizeof r);
    va_start(ap, fmt);
    vsnprintf(r.msg, sizeof r.msg, fmt, ap);
    va_end(ap);

#ifdef ENG_BOOT_TRACE_POPUP
    /* On-screen popup - opt-in (--breadcrumbs), for watching the TV without
     * klog attached. */
    sceKernelSendNotificationRequest(0, &r, sizeof r, 0);
#endif

    /* Durable: the one diagnostic log. */
    eng_boot_log("%s", r.msg);

    /* Kernel log - captured remotely by tools/klog.sh, so an unattended
     * deploy/launch/collect loop needs no TV. Always on in the app module. */
    snprintf(line, sizeof line, "%s\n", r.msg);
    sceKernelDebugOutText(0, line);
}

#ifdef __cplusplus
}
#endif

#define eng_bt(...) eng_bt_("PS5VR: " __VA_ARGS__)

#else /* !ENG_APP_MODULE */

#define eng_bt(...) ((void)0)

#endif

#endif /* ENG_BOOT_TRACE_H */
