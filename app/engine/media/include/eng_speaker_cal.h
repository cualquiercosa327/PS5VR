/*
 * eng_speaker_cal - per-speaker level + delay trims for 8-channel playback
 * (#106, AUTO CALIBRATION (MIC) in the Surround Sound Studio).
 *
 * The Surround Studio measures each speaker through the DualSense microphone
 * and hands the result here on APPLY. The profile is persisted as
 * speaker_calibration.cfg under the data root and applied to every 8-channel
 * block the playback path queues (eng_audio_out.c, audio_queue_push) - the
 * single funnel both the FFmpeg and the native audio decoder go through.
 *
 * Channel order is the AudioOut S16_8CH interleave the engine opens the port with,
 * hardware-verified in eng_audio_resample.c: FL FR FC LFE BL BR SL SR.
 *
 * Thread model: set/clear/load run on the UI thread, process runs on the
 * audio decode thread. The active profile is published through a generation
 * counter; process picks a new one up at its next block boundary.
 */
#ifndef ENG_SPEAKER_CAL_H
#define ENG_SPEAKER_CAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ENG_SPEAKER_CAL_CHANNELS 8
/* 30 ms at 48 kHz: well past any in-room path difference (~10 m). */
#define ENG_SPEAKER_CAL_MAX_DELAY_MS 30.0f
#define ENG_SPEAKER_CAL_MAX_TRIM_DB  12.0f

typedef struct {
    int   enabled;
    float gain_db[ENG_SPEAKER_CAL_CHANNELS];   /* trim, clamped to +/-12 dB */
    float delay_ms[ENG_SPEAKER_CAL_CHANNELS];  /* 0..30 ms                  */
} eng_speaker_cal_t;

/* Load speaker_calibration.cfg from the data root. Missing file = disabled. */
void eng_speaker_cal_load(void);
/* Make `cal` the active profile and persist it. Returns 0 if the save failed
 * (the profile is still applied for this session). */
int  eng_speaker_cal_apply(const eng_speaker_cal_t *cal);
/* Copy of the active profile. */
void eng_speaker_cal_get(eng_speaker_cal_t *out);

/* In-place trim of `frames` interleaved S16 frames. A no-op unless the port
 * is 8 channels and a profile is enabled. Audio decode thread only. */
void eng_speaker_cal_process(int16_t *pcm, int frames, int channels);
/* Drop the delay lines' history (seek / stream change). */
void eng_speaker_cal_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ENG_SPEAKER_CAL_H */
