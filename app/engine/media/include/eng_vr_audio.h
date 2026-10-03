/*
 * eng_vr_audio.h - PS5VR: head-tracked spatial audio rendered to stereo.
 */
#ifndef ENG_VR_AUDIO_H
#define ENG_VR_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

/* The head's orientation in the video's space (x,y,z,w; head -> video), from
 * the tracker. tracked = 0 renders as if facing the front. */
void eng_vr_audio_set_head(const float q[4], int tracked);

/* 0: no head tracking - stereo plays as it is, other layouts as a fixed
 * binaural downmix facing the front. 1 (the default): head-tracked. */
void eng_vr_audio_set_enabled(int on);

/* Render `frames` interleaved samples of `ch` channels (1, 2, 8 = FFmpeg's
 * 7.1 order; 4 with ambisonic = 1: first-order AmbiX) to interleaved stereo. */
void eng_vr_audio_render(const float *in, int ch, int ambisonic, int frames, float *out);

#ifdef __cplusplus
}
#endif

#endif
