/*
 * eng_adec — the audio decoder seam.
 *
 * Mirrors eng_vdec: one interface, an FFmpeg backend and a native
 * (libSceAudiodec / AJM) backend, chosen per stream. The native route covers
 * AAC and MP3 only — those are the two codecs proven callable through the
 * public sceAudiodec* family. AC-3, E-AC-3, DTS, FLAC, Vorbis and PCM have no
 * public native decoder on this firmware and stay on FFmpeg, so this is an
 * offload for part of the library, not a replacement for it.
 *
 * Evidence and ABI: third_party/ps5-audio-decoding-research/docs/{DECODING,
 * API-MATRIX}.md and examples/native-audio-poc/.
 */
#ifndef ENG_ADEC_H
#define ENG_ADEC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ENG_ADEC_BACKEND_FFMPEG = 0,
    ENG_ADEC_BACKEND_NATIVE = 1
} eng_adec_backend;

typedef struct eng_adec eng_adec;

typedef struct {
    int codec_id;        /* AVCodecID */
    int sample_rate;     /* from the stream's codecpar */
    int channels;
    const uint8_t *extradata;      /* AudioSpecificConfig for raw AAC, or NULL */
    int extradata_size;
} eng_adec_open_params;

/*
 * Load libSceAudiodec and report whether the native route is usable at all.
 * Call once early in boot, before eng_jailbreak_self(): the same credential
 * swap that makes libSceVideodec2 calls fail applies here.
 */
int eng_adec_native_probe(void);

/* Whether this codec has a native decoder, before committing to an open. */
int eng_adec_native_supports(int codec_id);

/*
 * Open a decoder for one stream. Falls back to reporting FFmpeg through
 * `chosen` when the native route is unavailable or refuses the stream; the
 * caller keeps its AVCodecContext either way.
 */
eng_adec *eng_adec_open(const eng_adec_open_params *p, eng_adec_backend *chosen);

/*
 * Decode one access unit. `pcm` receives interleaved signed-16 at the rate and
 * channel count reported by eng_adec_rate()/eng_adec_channels(). Returns the
 * number of bytes written, 0 for "no output from this AU", or negative on a
 * fatal decoder error — the caller should fall back to FFmpeg on negative.
 */
int eng_adec_decode(eng_adec *a, const uint8_t *data, int size,
                    int16_t *pcm, int pcm_capacity_bytes);

int eng_adec_rate(const eng_adec *a);
int eng_adec_channels(const eng_adec *a);
eng_adec_backend eng_adec_active(const eng_adec *a);

void eng_adec_flush(eng_adec *a);   /* seek */
void eng_adec_close(eng_adec *a);

#ifdef __cplusplus
}
#endif

#endif /* ENG_ADEC_H */
