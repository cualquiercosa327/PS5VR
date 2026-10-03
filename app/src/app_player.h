#pragma once
/*
 * The PS5VR Player: full-screen native playback on the engine's engine, with
 * PS5VR's player interface drawn over it (app_osd*.cpp) and subtitles by
 * libass (app_subs.c).
 *
 * Hardware HEVC/H.264 (sceVideodec2) up to 4K, 10-bit HDR10/HLG output,
 * software AV1/VP9/MPEG-2/VC-1, and every audio format FFmpeg decodes (TrueHD,
 * DTS-HD MA, E-AC-3, FLAC, ...) out as multichannel PCM.
 */
#ifdef __cplusplus
extern "C" {
#endif

/* After the display is up: creates the engine and the overlay canvases.
 * user_id owns the controller (opened only while a stream plays, because the
 * browser dialog gets no input while the app holds it). */
void app_player_init(int user_id);

/* Plays one request (the page's JSON) until it ends or the viewer leaves,
 * then posts the result for PS5VR's page. Blocks for the whole playback. */
void app_player_run(const char *request_json);

#ifdef __cplusplus
}
#endif
