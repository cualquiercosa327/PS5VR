#pragma once
/*
 * One playback as PS5VR's page describes it (the JSON body of
 * POST /api/player/play, see AppTVSmart js/platform/ps5/ps5NativePlayer.js):
 * the stream, what it is, the other sources, the episodes around it, the
 * subtitle addons to ask, and the viewer's preferences and language.
 */
#include "app_subs.h"

#include <map>
#include <string>
#include <vector>

struct AppSource {
    std::string id, title, description, addon, url, quality;
    std::string headers;          /* "Name: value\r\n" lines */
    std::string user_agent;
};

struct AppEpisode {
    int season = 0, episode = 0;
    std::string title, thumbnail, video_id, overview, released_label;
    bool released = true;
    bool watched = false;
};

struct AppSubtitleRef {
    std::string url, lang, label, headers;
};

struct AppSkip {
    std::string type;             /* intro, recap, outro, credits, preview */
    double start = 0, end = 0;
};

struct AppPrefs {
    std::vector<std::string> audio_langs;     /* preferred, in order */
    std::vector<std::string> subtitle_langs;
    bool subtitles_enabled = true;
    bool forced_only_when_off = true;
    app_sub_style style = {100, 0xffffff, 0, 1, 0.0f, 0.0f};
    bool autoplay_next = true;
    /* PS5VR's next-episode card rule (playerNextEpisodeRules.js). */
    bool next_by_minutes = false;            /* MINUTES_BEFORE_END, else PERCENTAGE */
    double next_percent = 99.0;
    double next_minutes = 2.0;
    bool show_clock = true;
    bool clock_24h = true;
    bool skip_intro = true;
    int still_watching_episodes = 3;         /* 0 = never ask */
    bool has_tz = false;                     /* the page's UTC offset, for the clock */
    int tz_offset_min = 0;
};

struct AppRequest {
    std::string id, url, headers, user_agent;
    std::string title, episode_title, year, description, genres, runtime, rating, item_type;
    std::string logo, poster, backdrop, thumbnail;
    int season = 0, episode = 0;
    double start_position = 0;
    std::string stream_title, stream_description, stream_addon;
    std::string vr;               /* PS5VR: "360_tb", "180_sbs", "flat", "none"... */
    int vr_sharpen = -1, vr_bits = 10, vr_eye = 0;   /* PS5VR picture options */
    bool vr_autopause = true;                        /* pause while the headset is off */
    std::vector<AppSource> sources;
    int source_index = 0;
    std::vector<AppSubtitleRef> subtitles;
    struct SubtitleRequest { std::string url, addon; };
    std::vector<SubtitleRequest> subtitle_requests;   /* Stremio addon subtitle lookups */
    std::vector<AppEpisode> episodes;
    bool has_next = false;
    AppEpisode next;
    std::vector<AppSkip> skips;
    AppPrefs prefs;
    std::map<std::string, std::string> strings;
    int autoplay_count = 0;       /* episodes played back to back so far */

    /* A UI string in the viewer's language (PS5VR's translation), else fallback. */
    const char *str(const char *key, const char *fallback) const;
    /* "Show", "S1 E3 · Title" lines as PS5VR's player header shows them. */
    std::string header_title() const;
    std::string header_subtitle() const;
};

/* Parses the request; false when it carries no playable url. */
bool app_request_parse(const char *json, AppRequest &out);

/* "Name: value\r\n" lines from a JSON headers object; User-Agent apart. */
std::string app_headers_from_json(const void *cjson_object, std::string *user_agent);

/* How the playback ended, for PS5VR's page (POST /api/player/state). */
struct AppResult {
    std::string state;            /* stopped | ended | error */
    double position = 0, duration = 0;
    std::string error;
    std::string action;           /* "", next, episode, source */
    int season = 0, episode = 0;  /* action episode */
    int source_index = -1;        /* action source */
    std::string audio_lang, subtitle_lang;
    bool subtitles_on = false;
    int subtitle_delay_ms = 0;
};

std::string app_result_json(const AppRequest &req, const AppResult &res);
