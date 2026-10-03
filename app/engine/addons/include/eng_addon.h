/*
 * eng_addon.h — Addon subsystem data structures and interface definitions.
 */
#ifndef ENG_ADDON_H
#define ENG_ADDON_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ENG_ADDON_EMBY = 0,
    ENG_ADDON_JELLYFIN,
    ENG_ADDON_APP,
    ENG_ADDON_CUSTOM
} eng_addon_type_t;

typedef enum {
    ENG_MEDIA_FOLDER = 0,
    ENG_MEDIA_VIDEO,
    ENG_MEDIA_AUDIO,
    ENG_MEDIA_STREAM
} eng_media_kind_t;

typedef struct eng_media_entry {
    char id[128];
    char title[160];
    char detail[160];
    char stream_url[512];
    char overview[512];
    int64_t duration_sec;
    int64_t resume_pos_sec;
    eng_media_kind_t kind;
    int is_folder;
} eng_media_entry_t;

#ifdef __cplusplus
}
#endif

#endif /* ENG_ADDON_H */
