/*
 * eng_demux.h — the demux thread: one av_read_frame loop feeding the video /
 * audio packet queues, plus the in-place (no-reopen) seek path it services.
 *
 * Verbatim move of the PROSPERO_TRUE_AV_SEEK region + the two PacketQueue
 * instances + the stream indices from main.c — Track A step A5 of
 * docs/modularisation-plan.md.
 *
 * TRANSITIONAL: still reads the playback-core decode context (play_fmt /
 * play_ctx / play_frame / play_pkt), the video-decode flags and g_pp_pb from
 * main.c as plain externs. The seek executor's codec-flush / clock-reset guts
 * move into the playback session at A7; the pp_playback notify calls become a
 * passed-in pointer.
 */
#ifndef ENG_DEMUX_H
#define ENG_DEMUX_H

#include <pthread.h>

#include "eng_packet_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The demux thread's output queues. Cleared by start/stop_video_playback and
 * drained by the video-decode thread, all still in main.c. */
extern PacketQueue video_packet_queue;
extern PacketQueue audio_packet_queue;

/* Selected stream indices — written by start_video_playback's stream select. */
extern int video_stream_index;
extern int audio_stream_index;

/* Demux thread handle + run flag (spawned / joined by main.c). */
extern volatile int demux_thread_running;
extern pthread_t    demux_thread;

void *demux_thread_func(void *arg);

/* Queue an in-place seek to `target_seconds`; the demux thread performs it on
 * its next loop. `restore_paused` = leave playback paused afterwards.
 * Returns 0 when no file is open. */
int prospero_request_inplace_seek(double target_seconds, int restore_paused);

/* A seek is queued, running, or only just finished. */
int eng_demux_seek_busy(void);

/* Re-arm the pre-buffer mid-play (an underrun): decoders and clocks hold until
 * `target_us` of video is queued or `max_ms` passes. */
void eng_demux_rebuffer(int64_t target_us, int max_ms);

/* 0..1 toward the current pre-buffer target. */
float eng_demux_prebuffer_progress(void);

/* Seconds of playback queued ahead of the decoder. */
double eng_demux_buffered_s(void);

#ifdef __cplusplus
}
#endif

#endif /* ENG_DEMUX_H */
