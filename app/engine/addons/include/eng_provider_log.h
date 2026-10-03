/*
 * eng_provider_log.h — one diagnostic line, two destinations.
 *
 * The provider seam originally logged with fprintf(stderr, ...), which was
 * wrong twice over on the app module:
 *
 *   - stderr does not reach /mnt/usb0/engine.log. That file is written by
 *     eng_boot_log / eng_bt, so every provider diagnostic went nowhere and
 *     #90's "confirm the load lines in engine.log" was unsatisfiable.
 *   - nothing in the app module has ever written to stderr, and on the
 *     native-app CRT touching it dereferences null. The FFmpeg av_log fix
 *     landed earlier the same day for exactly this reason; the provider code
 *     walked straight back into it.
 *
 * So: eng_bt on the device (durable log + klog, and a popup only with
 * --breadcrumbs), plain stderr on the host, where the uiview harness and the
 * test runner both want to see it and stderr is a real stream.
 */
#ifndef ENG_PROVIDER_LOG_H
#define ENG_PROVIDER_LOG_H

#ifdef ENG_APP_MODULE

#include "eng_boot_trace.h"
/* eng_bt already prefixes "PS5VR: "; the tag keeps provider lines greppable
 * in a log that carries the whole boot and playback trace. */
#define PROV_LOG(fmt, ...) eng_bt("provider: " fmt, ##__VA_ARGS__)

#else /* host / payload */

#include <stdio.h>
#define PROV_LOG(fmt, ...) fprintf(stderr, "[provider] " fmt "\n", ##__VA_ARGS__)

#endif

#endif /* ENG_PROVIDER_LOG_H */
