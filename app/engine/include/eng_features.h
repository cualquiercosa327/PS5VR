/*
 * eng_features.h — compile-time feature switches.
 *
 * One header so a feature can be turned off in every place that references it
 * without a build-system change, and turned back on by editing one line.
 */
#ifndef ENG_FEATURES_H
#define ENG_FEATURES_H

/*
 * Providers - the network media sources behind the eng_provider_t seam (#90):
 * IPTV today, Emby/Jellyfin/Torbox/Real-Debrid as their own stories follow.
 *
 * This is the generalisation of what ENG_ENABLE_EMBY used to be. That flag was
 * named for one service but had always been about ONE rail slot and ONE screen,
 * which is what the whole seam now shares - the slot, the icon and the
 * navbar.rml elements all already exist, so nothing else has to change when it
 * flips.
 *
 * What the flag does NOT gate is the provider logic. eng_provider_mgr, the
 * providers themselves and addons/addon_emby.c are compiled and registered
 * regardless: a provider with no credentials reports is_configured() false and
 * stays invisible, which is a better gate than a compile-time switch because
 * it is also the correct behaviour for a shipped build.
 *
 * Three places read it: the launch screen's library tiles, ScreenManager's
 * rail section table, and the RmlUi nav rail, which hides the icon so the
 * remaining sections do not sit next to a gap.
 */
/*
 * ON as of the #90 hardware-verification build.
 *
 * The rail slot now lands on ProviderHostScreen, not the old Emby setup stub,
 * and that screen is safe with nothing configured: it toasts "No provider is
 * set up yet" and sends the user to Settings. So turning it on does not depend
 * on the per-provider setup screens, which are separate stories - and leaving
 * it off made the provider screen unreachable, which meant it could not be
 * tested on a console at all.
 *
 * Back to 0 hides the launch tile, the rail section and the rail icon again.
 */
#ifndef ENG_ENABLE_PROVIDERS
#define ENG_ENABLE_PROVIDERS 1
#endif

/*
 * The old name, kept as an alias.
 *
 * Every existing reader spells it ENG_ENABLE_EMBY, and those readers are the
 * rail-geometry arithmetic in ScreenManager - the places where a mechanical
 * rename buys nothing and risks getting the section indices wrong. New code
 * uses ENG_ENABLE_PROVIDERS.
 */
#ifndef ENG_ENABLE_EMBY
#define ENG_ENABLE_EMBY ENG_ENABLE_PROVIDERS
#endif

#endif /* ENG_FEATURES_H */
