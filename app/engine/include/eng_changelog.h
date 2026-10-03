/*
 * eng_changelog — what changed in each release, for the About section.
 */
#ifndef ENG_CHANGELOG_H
#define ENG_CHANGELOG_H

typedef enum {
    ENG_CL_NEW = 0,
    ENG_CL_FIXED,
    ENG_CL_IMPROVED,
    ENG_CL_REMOVED,
    ENG_CL_VERSION
} eng_changelog_kind;

typedef struct eng_changelog_item {
    eng_changelog_kind kind;
    const char        *text;
} eng_changelog_item;

typedef struct eng_changelog_release {
    const char               *version;      /* "0.7.0" */
    const char               *tagline;      /* "EMBY PASSWORD AUTH & REFINED UI" */
    const char               *date;         /* "AUGUST 2026" */
    const eng_changelog_item *items;
    int                       item_count;
} eng_changelog_release;

/*
 * Release 0.10.0
 *
 * Five weeks and 315 commits: the payload became a real app, the picture moved
 * onto the console's own decoder and the interface onto its GPU. Trimmed to 12
 * items on purpose - ENG_RMLUI_CL_ITEMS is 12 because #changelog-detail is
 * 714dp of fixed, overflow:hidden height and a .clitem costs 46dp. Anything
 * past that is clipped rather than scrolled, so the list is curated to what
 * fits instead of relying on the "+ N MORE" tail.
 */
static const eng_changelog_item ENG_CL_0100[] = {
    { ENG_CL_NEW,      "IS A REAL PS5 APP - LAUNCH IT FROM THE GAMES ROW" },
    { ENG_CL_NEW,      "HARDWARE VIDEO DECODE - 4K H.264, HEVC AND VP9" },
    { ENG_CL_NEW,      "10-BIT HDR WITH HDR10 AND HLG TONE MAPPING" },
    { ENG_CL_NEW,      "THE WHOLE INTERFACE RENDERS ON THE GPU" },
    { ENG_CL_NEW,      "AN INTERFACE REBUILT FROM SCRATCH - EVERY SCREEN" },
    { ENG_CL_NEW,      "COLOUR THEMES - MIDNIGHT, CARBON, EMBER, AURORA, USB" },
    { ENG_CL_NEW,      "STORAGE BROWSER REBUILT WITH A THUMBNAIL ON EVERY CARD" },
    { ENG_CL_NEW,      "SCREENSHOT CAPTURE, AND A QUIT THAT CLOSES CLEANLY" },
    { ENG_CL_FIXED,    "THE SCRUB HEAD STARTS WHERE THE PICTURE IS" },
    { ENG_CL_FIXED,    "CHOOSING A THEME ACTUALLY CHANGES THE COLOURS" },
    { ENG_CL_FIXED,    "SEEKING KEEPS THE AUDIO IN STEP WITH THE PICTURE" },
    { ENG_CL_REMOVED,  "EMBY IS OFF WHILE IT IS REWORKED - IT RETURNS SOON" }
};

/* Release 0.7.0 */
static const eng_changelog_item ENG_CL_070[] = {
    { ENG_CL_NEW,      "EMBY PASSWORD AUTHENTICATION WITH SECURE INPUT & PERSISTENCE" },
    { ENG_CL_NEW,      "NATIVE OPENSSL HTTPS AND TLS CLIENT INTEGRATION" },
    { ENG_CL_NEW,      "REDESIGNED CHANGELOG VIEWER WITH MASTER-DETAIL HIERARCHY" },
    { ENG_CL_NEW,      "DIRECT MEMORY BUFFER MANAGER FOR STREAMING I/O PERFORMANCE" },
    { ENG_CL_NEW,      "NATIVE PS5 IME DIALOG INTEGRATION WITH MULTI-LANGUAGE SUPPORT" },
    { ENG_CL_NEW,      "INTERACTIVE MEDIA DIRECTORY SEARCH IN FILE BROWSER" },
    { ENG_CL_FIXED,    "CUSTOM AVIO LIFECYCLE STABILITY FOR NETWORK STREAMS" },
    { ENG_CL_IMPROVED, "PARALLEL CPU AVX2 SIMD YUV PIPELINE OPTIMIZATIONS" }
};

/* Release 0.6.0 */
static const eng_changelog_item ENG_CL_060[] = {
    { ENG_CL_NEW,      "EMBY MEDIA SERVER ADDON - BROWSE AND STREAM DIRECTLY" },
    { ENG_CL_NEW,      "ON-SCREEN VIRTUAL KEYBOARD FOR SERVER AND CREDENTIAL SETUP" },
    { ENG_CL_NEW,      "SURROUND SOUND STUDIO - 360-DEG TOP-DOWN THEATER" },
    { ENG_CL_NEW,      "8-CHANNEL HARDWARE CALIBRATION FOR 5.1 AND 7.1" },
    { ENG_CL_NEW,      "AUTO TEST CYCLES AND 360-DEG ROTATION SWEEP" },
    { ENG_CL_NEW,      "DYNAMIC 5.1 SPEAKER HIDING AND 2D SPATIAL DPAD NAVIGATION" },
    { ENG_CL_NEW,      "ORGANIZED SETTINGS HIERARCHY WITH EXPANDED SECTIONS" },
    { ENG_CL_NEW,      "CONFIGURABLE DEFAULT SUBTITLE SIZING PREFERENCE" },
    { ENG_CL_FIXED,    "PS5 S16_8CH GAIN INITIALIZATION AND CLICK-FREE FADES" }
};

/* Release 0.5.0 */
static const eng_changelog_item ENG_CL_050[] = {
    { ENG_CL_NEW,      "TEXT READER - OPEN TXT LOG MD NFO AND SUBTITLES" },
    { ENG_CL_NEW,      "SCROLL WITH DPAD - SHOULDERS PAGE - TRIANGLE RESIZES" },
    { ENG_CL_NEW,      "THE FONT HAS PUNCTUATION AT LAST" },
    { ENG_CL_FIXED,    "THE RELEASE NOW SHIPS THE FILE THAT ACTUALLY RUNS" },
    { ENG_CL_FIXED,    "THE MEDIA TILE REPORTS THE RIGHT VERSION" }
};

/* Release 0.4.0 */
static const eng_changelog_item ENG_CL_040[] = {
    { ENG_CL_NEW,      "CIRCLE ASKS BEFORE IT STOPS PLAYBACK" },
    { ENG_CL_NEW,      "COVER ART IS LARGER - TILES READ AS POSTERS" },
    { ENG_CL_NEW,      "SOUND AND LIGHTBAR SURVIVE A RELAUNCH" },
    { ENG_CL_FIXED,    "SMOOTHER 4K - LESS WORK PER FRAME CONVERTED" },
    { ENG_CL_FIXED,    "LESS STUTTER - NO THREAD CHURN EVERY FRAME" },
    { ENG_CL_FIXED,    "TILE ART IS CROPPED - NOT SQUASHED" },
    { ENG_CL_FIXED,    "THE RAIL NO LONGER OPENS OVER A PANEL" }
};

/* Release 0.3.0 */
static const eng_changelog_item ENG_CL_030[] = {
    { ENG_CL_NEW,      "HOME SCREEN TILE - OPEN FROM MEDIA - NO BROWSER" },
    { ENG_CL_NEW,      "THIS CHANGELOG - UNDER ABOUT" },
    { ENG_CL_NEW,      "A REAL APPLICATION ICON - DRAWN FROM VECTORS" },
    { ENG_CL_FIXED,    "THE TILE KEEPS ITS OWN COPY OF THE PLAYER" }
};

/* Release 0.2.0 */
static const eng_changelog_item ENG_CL_020[] = {
    { ENG_CL_NEW,      "SUBTITLE TRACK PICKER - PRESS DOWN WHILE PLAYING" },
    { ENG_CL_NEW,      "TRACK NAMES FROM LANGUAGE CODES - 50 MAPPED" },
    { ENG_CL_FIXED,    "TRACKS RANKED BY CUE COUNT - NOT BY METADATA" },
    { ENG_CL_FIXED,    "NEAR EMPTY TRACKS MARKED SIGNS ONLY - STILL OFFERED" },
    { ENG_CL_FIXED,    "MARQUEE SCROLLS AT ONE SPEED AT ANY FRAME RATE" }
};

/* Release 0.1.0 */
static const eng_changelog_item ENG_CL_010[] = {
    { ENG_CL_NEW,      "LAUNCH SCREEN - RESUME HERO AND TWO SHELVES" },
    { ENG_CL_NEW,      "BROWSER INSPECTOR - PREVIEW FRAME AND CODEC DETAIL" },
    { ENG_CL_NEW,      "SIDE NAVIGATION RAIL - BACK IS A STACK NOW" },
    { ENG_CL_NEW,      "HOLD TO SCROLL - SHOULDERS PAGE - L2/R2 JUMP A-Z" },
    { ENG_CL_NEW,      "L3 CAPTURES A SCREENSHOT DURING PLAYBACK" },
    { ENG_CL_FIXED,    "THE EIGHTH SETTINGS ROW WAS UNREACHABLE" },
    { ENG_CL_FIXED,    "AUDIO FAILURES NO LONGER ALL BLAMED ON E-AC3" },
    { ENG_CL_FIXED,    "PREVIEWS NO LONGER PIXELATED BY DOUBLE RESAMPLING" },
    { ENG_CL_FIXED,    "4K CONVERTER KEEPS ITS WORKERS - 11.6 TO 9.2 MS" },
    { ENG_CL_REMOVED,  "HAPTICS - EVERY VIBRATION PATH IS DEAD IN THIS SLOT" }
};

/* Release 0.0.2 */
static const eng_changelog_item ENG_CL_002[] = {
    { ENG_CL_NEW,      "THEME FILES FROM USB - FOUR THEMES BUILT IN" },
    { ENG_CL_NEW,      "CARDS DRAWN FROM SDF - GENERATED VECTOR ICONS" },
    { ENG_CL_NEW,      "NAVIGATION SOUNDS AND A THEMED LIGHTBAR" }
};

/* Release 0.0.1 */
static const eng_changelog_item ENG_CL_001[] = {
    { ENG_CL_NEW,      "7.1 SURROUND OUTPUT WITH STEREO FALLBACK" },
    { ENG_CL_NEW,      "FLIP SYNCHRONISED PRESENTATION - NO TEARING" },
    { ENG_CL_NEW,      "FASTER TILE SWIZZLE AND FOLDERS FIRST BROWSING" }
};

static const eng_changelog_release ENG_CHANGELOG_RELEASES[] = {
    { "0.10.0", "THE PS5 APP, GPU & HARDWARE DECODE", "SEPTEMBER 2026", ENG_CL_0100, sizeof(ENG_CL_0100)/sizeof(ENG_CL_0100[0]) },
    { "0.7.0", "EMBY AUTH & SYSTEM ENHANCEMENTS", "AUGUST 2026", ENG_CL_070, sizeof(ENG_CL_070)/sizeof(ENG_CL_070[0]) },
    { "0.6.0", "EMBY ADDON & SURROUND SOUND STUDIO", "AUGUST 2026", ENG_CL_060, sizeof(ENG_CL_060)/sizeof(ENG_CL_060[0]) },
    { "0.5.0", "A TEXT READER & FIXED INSTALL", "AUGUST 2026", ENG_CL_050, sizeof(ENG_CL_050)/sizeof(ENG_CL_050[0]) },
    { "0.4.0", "SMOOTHER PLAYBACK & SAFER STOP", "JULY 2026", ENG_CL_040, sizeof(ENG_CL_040)/sizeof(ENG_CL_040[0]) },
    { "0.3.0", "LAUNCH FROM THE CONSOLE", "JULY 2026", ENG_CL_030, sizeof(ENG_CL_030)/sizeof(ENG_CL_030[0]) },
    { "0.2.0", "SUBTITLES & TRACK PICKER", "JULY 2026", ENG_CL_020, sizeof(ENG_CL_020)/sizeof(ENG_CL_020[0]) },
    { "0.1.0", "A REBUILD OF THE INTERFACE", "JULY 2026", ENG_CL_010, sizeof(ENG_CL_010)/sizeof(ENG_CL_010[0]) },
    { "0.0.2", "THEMES & SDF VECTOR ICONS", "JULY 2026", ENG_CL_002, sizeof(ENG_CL_002)/sizeof(ENG_CL_002[0]) },
    { "0.0.1", "THE INITIAL FORK", "JULY 2026", ENG_CL_001, sizeof(ENG_CL_001)/sizeof(ENG_CL_001[0]) },
};

#define ENG_CHANGELOG_RELEASE_COUNT \
    ((int)(sizeof(ENG_CHANGELOG_RELEASES) / sizeof(ENG_CHANGELOG_RELEASES[0])))

#endif /* ENG_CHANGELOG_H */
