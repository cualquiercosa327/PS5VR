/*
 * eng_playback.c — the playback session: video decode/pace/present loop, the
 * media clock, the video thread. Track A step A7 of
 * docs/modularisation-plan.md §5.
 *
 * prospero_media_clock_seconds, present_pp_frame, convert_frame_via_sws,
 * prospero_video_queue_drain_nonkey, decode_next_video_frame and
 * video_decode_thread_func are lifted verbatim from main.c. The A/V-sync
 * pacing block inside decode_next_video_frame is unchanged.
 */
#include "eng_playback.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>

#include "pp_playback.h"
#include "pp_frame.h"
#include "pp_stage_breadcrumb.h"

#include "eng_vdec.h"
#include "eng_packet_queue.h"
#include "eng_demux.h"
#include "eng_audio_out.h"
#include "eng_direct_mem.h"

#ifndef SCREEN_PLAYER
#define SCREEN_PLAYER 2
#endif

#define WIDTH  1920
#define HEIGHT 1080

/* ---------------------------------------------------------------------------
 * TRANSITIONAL: state still owned by main.c. Replaced by eng_pb_*() at A8.
 * ------------------------------------------------------------------------ */
extern int                 player_paused;
extern int                 screen;
extern double              media_duration_sec;
extern int                 playback_profile;
/* Start-of-stream pre-buffer hold - see the note in Bridge.cpp. */
extern volatile int        pb_prebuffer_hold;
extern volatile int        pb_scrub_hold;
extern struct SwsContext  *play_sws;
extern AVFormatContext    *play_fmt;

extern eng_vdec           *g_vdec;   /* the live video decoder (A6, owned by main.c) */
extern pp_playback         g_pp_pb;
extern int                 g_first_frame_bc_done;

extern int       dbg_video_frames;
extern int       dbg_video_thread_alive;
extern int       dbg_swaps;
extern long long dbg_last_pts;
extern int       perf_decode_frames;

long long      now_ms(void);
void           toast(const char *title, const char *msg);
pp_aspect_mode prospero_view_mode_to_aspect(void);

/* ---------------------------------------------------------------------------
 * Playback session state (exported via eng_playback.h).
 * ------------------------------------------------------------------------ */
double first_video_pts_seconds = -1.0;
double video_clock_seconds = 0.0;
double video_fps = 60.0;
int    video_decode_ready = 0;
int    video_decode_done = 0;

/* Sustained decoder failure. The decode loop keeps feeding packets to a
 * decoder that returns fatal on every receive; the state corrupts (POC errors)
 * and it eventually faults. Instead: count the streak, and once it is clearly
 * not recoverable, raise this flag so main.c ends playback with a toast rather
 * than crashing. Reset per open in start_video_playback.
 *
 * NOTE: this fires for the hardware (sceVideodec2) backend too, so the toast
 * must not say "software". A stream whose SPS/PPS the HW decoder rejects after
 * a seek is the common trigger (#57). */
int    g_pb_decode_fatal = 0;
static int s_vdec_fatal_streak = 0;
#define ENG_VDEC_FATAL_STREAK_LIMIT 16
int eng_pb_decode_fatal(void) { return g_pb_decode_fatal; }

/* Clear the fatal state AND the streak counter. start_video_playback() calls
 * this on every (re)open — clearing only g_pb_decode_fatal leaves the streak
 * latched at the limit, so the first transient fatal after a reopen (e.g. the
 * FFmpeg-fallback reopen for #57) would re-trip it immediately. */
void eng_pb_reset_decode_fatal(void)
{
    g_pb_decode_fatal   = 0;
    s_vdec_fatal_streak = 0;
}

#ifdef ENG_APP_MODULE
extern void pp_stage_bc(const char *stage_id, const char *detail);
#endif

static void note_vdec_result(int fatal)
{
    if (!fatal) {
        s_vdec_fatal_streak = 0;
        return;
    }
    if (++s_vdec_fatal_streak < ENG_VDEC_FATAL_STREAK_LIMIT || g_pb_decode_fatal)
        return;
    g_pb_decode_fatal = 1;
    video_decode_done = 1;   /* stop the demux/decode loop */
#ifdef ENG_APP_MODULE
    pp_stage_bc("P8_VDEC_FATAL", "decode failed repeatedly - ending playback");
#endif
    toast("PLAYBACK", "Video decode failed - can't play this file");
}

volatile int video_thread_running = 0;
pthread_t    video_thread;

/*
 * 1 while the decode thread is idling (paused / off-screen / not ready) and is
 * therefore NOT inside pp_playback_push_frame. Kept as a debug/diagnostic
 * signal; the VO reconfigure it used to guard (#32) went with the CPU present
 * path in GL-4.
 */
volatile int video_decode_parked = 1;
/* Set by a seek (eng_demux.c) for as long as it needs the decoder to itself:
 * the decode thread parks and stays out of eng_vdec_* until it clears. */
volatile int video_decode_hold = 0;

AVPacket *video_video_pending_pkt = NULL;
pthread_mutex_t video_frame_mutex = PTHREAD_MUTEX_INITIALIZER;

uint32_t *video_frame_pixels = NULL;   /* aliases into the ring; read by main.c's legacy renderers */
int video_frame_w = 0;
int video_frame_h = 0;
int video_frame_loaded = 0;

/*
 * Internal to convert_frame_via_sws — nothing outside touches the ring.
 *
 * #6: this is the exotic-pixel-format swscale fallback (eng_vdec_receive() == 2
 * or the product backend inactive) — the product path presents through
 * pp_playback's display model, not this ring. Backed by the eng_direct_mem
 * slab and grow-only: the buffers are (re)allocated only when a frame arrives
 * larger than the current slot size, so a seek or a same-resolution re-open
 * reuses them and playback never churns the heap. Count trimmed 8 -> 3
 * (VIDEO_ROTATE_BUFFERS) — enough for one presented + one just-written + one
 * in-flight without the legacy renderer tearing.
 */
static uint32_t *video_rotate_pixels[VIDEO_ROTATE_BUFFERS] = {0};
static size_t    video_rotate_slot_bytes = 0;
static int video_rotate_index = 0;

static int present_pp_frame(const pp_frame *pf);
static int convert_frame_via_sws(AVFrame *frame);
static void prospero_video_queue_drain_nonkey(int max_packets);

double prospero_media_clock_seconds(void)
{
    double audio_rel = audio_clock_seconds;
    double video_rel = 0.0;

    if (first_video_pts_seconds >= 0.0)
        video_rel = video_clock_seconds - first_video_pts_seconds;
    else if (video_clock_seconds > 0.0)
        video_rel = video_clock_seconds;
    if (video_rel < 0.0)
        video_rel = 0.0;

    /*
     * Prefer audio when a playable track is open and the out clock has
     * started. Otherwise drive UI/resume from video so silent E-AC3
     * files still move the progress bar.
     */
    if (audio_stream_index >= 0 && audio_handle >= 1 && audio_rel >= 0.05)
        return audio_rel;
    return video_rel;
}


/*
 * present_pp_frame — job 4 of the old decode_next_video_frame (§5): hand a
 * decoded pp_frame to the product playback pipeline. The VO-ready gate and the
 * first-frame breadcrumb are kept verbatim from convert_frame_to_rgb().
 * Returns 1 when the frame was pushed, 0 when dropped (VO not ready).
 */
static int present_pp_frame(const pp_frame *pf)
{
    if (!g_first_frame_bc_done) {
        char d[80];
        snprintf(d, sizeof(d), "fmt=%d %ux%u", (int)pf->format, pf->width, pf->height);
        pp_stage_bc_checkpoint("009_FIRST_FRAME_ENTER", d);
        g_first_frame_bc_done = 1;
    }
    g_pp_pb.aspect = prospero_view_mode_to_aspect();
    g_pp_pb.stats.aspect = (int)g_pp_pb.aspect;
    (void)pp_playback_push_frame(&g_pp_pb, (pp_frame *)pf);
    video_frame_loaded = pp_playback_has_display(&g_pp_pb);
    return 1;
}

/*
 * convert_frame_via_sws — the legacy swscale RGBA path, used only when the
 * decoder hands back an exotic pixel format eng_vdec can't map to pp_frame
 * (eng_vdec_receive() == 2), or when the product backend is inactive. Verbatim
 * from the tail of the old convert_frame_to_rgb().
 */
static int convert_frame_via_sws(AVFrame *frame)
{
    if (!frame) return 0;

    {
        static int s_sws_warned;
        if (!s_sws_warned) {
            const char *pn = av_get_pix_fmt_name((enum AVPixelFormat)frame->format);
            char msg[80];
            snprintf(msg, sizeof(msg), "slow path fmt=%s", pn ? pn : "?");
            toast("CONVERT", msg);
            s_sws_warned = 1;
        }
    }

    if (video_frame_w != frame->width || video_frame_h != frame->height || video_rotate_pixels[0] == NULL) {
        size_t need = (size_t)frame->width * (size_t)frame->height * 4u;

        pthread_mutex_lock(&video_frame_mutex);

        /* Grow-only: keep the existing slots when the new frame fits. */
        if (need > video_rotate_slot_bytes || video_rotate_pixels[0] == NULL) {
            for (int i = 0; i < VIDEO_ROTATE_BUFFERS; i++) {
                eng_direct_mem_free(video_rotate_pixels[i]);
                video_rotate_pixels[i] = NULL;
            }
            video_rotate_slot_bytes = 0;

            for (int i = 0; i < VIDEO_ROTATE_BUFFERS; i++) {
                video_rotate_pixels[i] = (uint32_t *)eng_direct_mem_alloc(need);
                if (!video_rotate_pixels[i]) {
                    for (int j = 0; j < i; j++) {
                        eng_direct_mem_free(video_rotate_pixels[j]);
                        video_rotate_pixels[j] = NULL;
                    }
                    video_frame_pixels = NULL;
                    video_frame_loaded = 0;
                    pthread_mutex_unlock(&video_frame_mutex);
                    return 0;
                }
            }
            video_rotate_slot_bytes = need;
        }

        video_frame_w = frame->width;
        video_frame_h = frame->height;
        video_rotate_index = 0;
        video_frame_loaded = 0;

        video_frame_pixels = video_rotate_pixels[0];
        pthread_mutex_unlock(&video_frame_mutex);
    }

    if (!play_sws) {
        play_sws = sws_getContext(
            frame->width, frame->height, frame->format,
            frame->width, frame->height, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, NULL, NULL, NULL
        );

        if (!play_sws) return 0;
    }

    int write_index = (video_rotate_index + 1) % VIDEO_ROTATE_BUFFERS;

    uint8_t *dst_data[4] = {(uint8_t*)video_rotate_pixels[write_index], NULL, NULL, NULL};
    int dst_linesize[4] = {video_frame_w * 4, 0, 0, 0};

    sws_scale(play_sws, (const uint8_t * const*)frame->data,
              frame->linesize, 0, frame->height,
              dst_data, dst_linesize);

    pthread_mutex_lock(&video_frame_mutex);
    video_rotate_index = write_index;
    video_frame_pixels = video_rotate_pixels[video_rotate_index];
    video_frame_loaded = 1;
    dbg_swaps++;
    pthread_mutex_unlock(&video_frame_mutex);

    return 1;
}


static void prospero_video_queue_drain_nonkey(int max_packets)
{
    /*
     * Drop only non-keyframes so the decoder never waits on a full flush
     * (flush → freeze until next key every few seconds).
     */
    int n = 0;
    while (n < max_packets) {
        AVPacket *drop = packet_queue_pop(&video_packet_queue);
        if (!drop)
            break;
        if (drop->flags & AV_PKT_FLAG_KEY) {
            /* Put keyframe back as pending so we don't skip the GOP start */
            if (!video_video_pending_pkt)
                video_video_pending_pkt = drop;
            else
                av_packet_free(&drop);
            break;
        }
        av_packet_free(&drop);
        n++;
    }
}


#ifdef APP_VR
#include "eng_boot_log.h"
/*
 * PS5VR: a decoder that cannot keep up (8K60 HEVC peaks around 48 fps on the
 * PS5) falls further behind the audio until the audio throttle starts parking
 * output - slow motion with crackling sound. While the last picture came out
 * late, skip access units that no other picture references before they reach
 * the decoder (HEVC sub-layer non-reference pictures at the top temporal
 * layer, H.264 nal_ref_idc 0). References are never touched, so nothing that
 * is decoded is damaged; the picture just holds a frame longer.
 */
static double s_video_late_s;         /* audio - video at the last picture */
static unsigned s_late_nonref_drops;
/* The audio clock counts samples handed to the port, so a video that keeps up
 * still reads ~0.07 s "late". Only a sustained lag past that is the decoder
 * falling short; once a file shows it, it plays at half rate (every reference
 * picture) for good - an even 30 fps beats hopping between 30 and 45. */
#define VR_LATE_S       0.15
#define VR_LATE_FRAMES  30
static const void *s_late_fmt;        /* the file the state below belongs to */
static int s_late_frames;
static int s_half_rate;
/* Steady lag: the mean (audio - video) over 3 s of presented frames. What
 * is left beyond the audio port's own ~0.07 s is decode pipeline delay (frame
 * threads, a decoder at its limit) that the picture never makes up, so the
 * sound is held back by it, once (up to three times a file). */
#define VR_SYNC_PORT_S  0.07
static double s_win_start = -1.0, s_win_sum;
static int s_win_n;
static int s_resyncs;
static int s_hevc_max_tid;

/* The hardware decoder's ceiling: 8K60 HEVC peaks at ~45-50 fps (~30 while
 * the headset renders), 4K60 and 8K30 keep up. A stream past this goes to half
 * rate from its first picture - not after seconds of late, out-of-sync video. */
#define VR_DECODE_MAX_PX_S  1.2e9

/* A new file: forget the last one's state, and judge this one's rate. */
static void vr_file_state(const AVFormatContext *fmt, int vstream)
{
    if ((const void *)fmt == s_late_fmt)
        return;
    s_late_fmt = fmt;
    s_late_frames = 0;
    s_half_rate = 0;
    s_win_start = -1.0;
    s_resyncs = 0;
    s_hevc_max_tid = 0;
    if (!fmt || vstream < 0 || vstream >= (int)fmt->nb_streams)
        return;
    const AVStream *st = fmt->streams[vstream];
    const AVCodecParameters *par = st->codecpar;
    AVRational r = st->avg_frame_rate.num > 0 ? st->avg_frame_rate : st->r_frame_rate;
    const double fps = (r.num > 0 && r.den > 0) ? (double)r.num / r.den : 0.0;
    const double px_s = (double)par->width * par->height * fps;
    if (px_s > VR_DECODE_MAX_PX_S &&
        (par->codec_id == AV_CODEC_ID_HEVC || par->codec_id == AV_CODEC_ID_H264) &&
        eng_vdec_active(g_vdec) == ENG_VDEC_BACKEND_NATIVE) {
        s_half_rate = 1;
        eng_boot_log("vr: %dx%d at %.0f fps is past the decoder - half rate (reference "
                     "pictures only) from the start", par->width, par->height, fps);
    }
}

static int au_nal_len_size(const AVCodecParameters *par)
{
    const uint8_t *e = par->extradata;
    if (!e || par->extradata_size < 7 || e[0] != 1)
        return 0;                     /* Annex B in the packets */
    if (par->codec_id == AV_CODEC_ID_HEVC)
        return par->extradata_size > 22 ? (e[21] & 3) + 1 : 0;
    return (e[4] & 3) + 1;            /* avcC */
}

/* 1 = this NAL is VCL and non-reference, 0 = VCL reference, -1 = not VCL. */
static int nal_nonref(int hevc, const uint8_t *n, int len)
{
    if (len < 2)
        return -1;
    if (hevc) {
        const int type = (n[0] >> 1) & 0x3f, tid = (n[1] & 7) - 1;
        if (type >= 32)
            return -1;
        if (tid > s_hevc_max_tid)
            s_hevc_max_tid = tid;
        return (type <= 14 && !(type & 1) && tid == s_hevc_max_tid) ? 1 : 0;
    }
    const int type = n[0] & 0x1f;
    if (type < 1 || type > 5)
        return -1;
    return ((n[0] >> 5) & 3) == 0 ? 1 : 0;
}

static int au_droppable(const AVPacket *pkt, const AVCodecParameters *par)
{
    const int hevc = par->codec_id == AV_CODEC_ID_HEVC;
    if (!hevc && par->codec_id != AV_CODEC_ID_H264)
        return 0;
    const uint8_t *d = pkt->data, *end = pkt->data + pkt->size;
    const int ls = au_nal_len_size(par);
    int vcl = 0;
    while (d < end) {
        const uint8_t *n;
        int len;
        if (ls) {
            if (end - d < ls)
                break;
            uint32_t l = 0;
            for (int i = 0; i < ls; i++)
                l = (l << 8) | d[i];
            n = d + ls;
            if (l > (uint32_t)(end - n))
                break;
            len = (int)l;
            d = n + len;
        } else {
            const uint8_t *p = d;
            while (p + 3 <= end && !(p[0] == 0 && p[1] == 0 && p[2] == 1))
                p++;
            if (p + 3 > end)
                break;
            n = p + 3;
            const uint8_t *q = n;
            while (q + 3 <= end && !(q[0] == 0 && q[1] == 0 && (q[2] == 1 || q[2] == 0)))
                q++;
            if (q + 3 > end)
                q = end;
            len = (int)(q - n);
            d = q;
        }
        const int r = nal_nonref(hevc, n, len);
        if (r == 0)
            return 0;                 /* a reference picture: keep */
        if (r == 1)
            vcl = 1;
    }
    return vcl;
}
#endif

int decode_next_video_frame(void)
{
    if (!video_decode_ready || player_paused) return 0;

    /*
     * Audio-master pacing:
     *  - If video is AHEAD of audio: wait (full remaining gap, chunked) so
     *    picture is not "very fast".
     *  - If video is slightly late: present now (no freeze).
     *  - If video is badly late: drain non-keys, still present this frame.
     *  - Always convert (skip-convert = frozen picture).
     *  - Never avcodec_flush mid-stream.
     */
    for (int attempts = 0; attempts < 48; attempts++) {
        /* A seek (or pause) set player_paused and is waiting for this thread
         * to park before it flushes the decoder - get out between calls
         * rather than after up to 47 more of them. */
        if (player_paused || video_decode_hold)
            return 1;
        pp_frame pf;
        int recv_ret = eng_vdec_receive(g_vdec, &pf);   /* job 1 — pure decode */
        note_vdec_result(recv_ret < 0);
        if (g_pb_decode_fatal)
            return 0;
        if (recv_ret >= 1) {
            double video_rel = 0.0;
            double audio_rel = 0.0;
            double behind = 0.0;
            int wait_iters = 0;

            dbg_video_frames++;
            dbg_last_pts = pf.pts_us;   /* now microseconds (was raw stream PTS) */
            perf_decode_frames++;

            /*
             * #32: in the seek-discard window every frame between the keyframe
             * and the seek target is dropped by pp_playback_push_frame(). Pacing
             * them to the frame rate (the branches below) makes a seek across a
             * long 4K GOP take ~GOP-length wall time - the slow GTA-trailer
             * seek. Decode + discard as fast as the decoder returns instead;
             * sceVideodec2Decode is synchronous so this can't outrun it.
             */
            int seek_discarding = g_pp_pb.active && g_pp_pb.seek_discarding &&
                                  pf.pts_us < g_pp_pb.seek_target_us;

            /*
             * job 2 — media clock from the frame PTS (already microseconds).
             *
             * Frames inside the discard window are decoded only to reach the
             * target; none of them is shown. Latching first_video_pts_seconds
             * on one of them anchors the relative video clock at the keyframe
             * while the audio clock restarts at the target, so the instant the
             * picture resumes video_rel reads a whole run-up ahead of
             * audio_rel and the audio-master wait below freezes the picture
             * until audio covers the difference - the post-seek hitch. Anchor
             * on the first frame that is actually presented instead. The seek
             * path zeroes video_clock_seconds, so the UI position falls back
             * to the seek target while the window is open.
             */
            if (pf.pts_us != INT64_MIN && !seek_discarding) {
                video_clock_seconds = (double)pf.pts_us / 1000000.0;
                if (first_video_pts_seconds < 0.0)
                    first_video_pts_seconds = video_clock_seconds;
            }

            video_rel = 0.0;
            if (first_video_pts_seconds >= 0.0)
                video_rel = video_clock_seconds - first_video_pts_seconds;
            if (video_rel < 0.0)
                video_rel = 0.0;
            audio_rel = audio_clock_seconds;
            behind = audio_rel - video_rel; /* >0 => video late; <0 => video early */
#ifdef APP_VR
            vr_file_state(play_fmt, video_stream_index);
            if (!seek_discarding && audio_rel > 0.05 && video_rel >= 2.0) {
                if (s_win_start < 0.0 || video_rel < s_win_start || eng_av_resync_s > 0.0) {
                    s_win_start = video_rel;          /* (re)start, also while a hold runs */
                    s_win_sum = 0.0;
                    s_win_n = 0;
                }
                s_win_sum += behind;
                s_win_n++;
                if (video_rel - s_win_start >= 3.0 && s_win_n > 0) {
                    const double lag = s_win_sum / s_win_n;
                    static int s_lag_logs;
                    if (s_lag_logs++ % 5 == 0)
                        eng_boot_log("vr: A/V lag %.3f s (3 s mean, %d frames, port %.2f)", lag,
                                     s_win_n, VR_SYNC_PORT_S);
                    if (lag > VR_SYNC_PORT_S + 0.05 && s_resyncs < 3) {
                        eng_av_resync_s = lag - VR_SYNC_PORT_S;
                        s_resyncs++;
                        eng_boot_log("vr: steady A/V lag %.3f s - holding the sound back %.3f s",
                                     lag, lag - VR_SYNC_PORT_S);
                    }
                    s_win_start = -1.0;
                }
            }
            s_video_late_s = (seek_discarding || audio_rel <= 0.05) ? 0.0 : behind;
            if (!seek_discarding && audio_rel > 0.05 && video_rel >= ENG_AV_SYNC_SETTLE_SEC) {
                if (behind > VR_LATE_S && eng_vdec_active(g_vdec) == ENG_VDEC_BACKEND_NATIVE) {
                    if (++s_late_frames >= VR_LATE_FRAMES && !s_half_rate) {
                        s_half_rate = 1;
                        eng_boot_log("vr: decoder %.3f s behind for %d frames - half rate "
                                     "(reference pictures only) for this file",
                                     behind, s_late_frames);
                    }
                } else {
                    s_late_frames = 0;
                }
            }
#endif

            if (seek_discarding) {
                /* no pacing - the frame is about to be thrown away */
            } else if (audio_rel > 0.05 &&
                       video_rel >= ENG_AV_SYNC_SETTLE_SEC) {
                /*
                 * Outside the cold-start window (ENG_AV_SYNC_SETTLE_SEC - see
                 * eng_audio_out.h for why a freshly reset clock pair must not
                 * throttle each other):
                 *
                 * Video ahead of audio: wait for audio, but NEVER freeze the
                 * picture if audio clock stops (underrun / 44.1k stall).
                 * Y2JB-class clips froze ~2s in when wait never broke out.
                 */
                double audio_at_wait = audio_rel;
                int stuck_iters = 0;

                while (behind < -0.008 &&
                       !player_paused &&
                       video_decode_ready &&
                       wait_iters < 100) {
                    int sleep_us = (int)((-behind) * 1000000.0);
                    if (sleep_us > 15000)
                        sleep_us = 15000;
                    if (sleep_us < 500)
                        sleep_us = 500;
                    usleep(sleep_us);
                    audio_rel = audio_clock_seconds;
                    behind = audio_rel - video_rel;
                    wait_iters++;
                    if (audio_rel <= audio_at_wait + 0.0005)
                        stuck_iters++;
                    else {
                        audio_at_wait = audio_rel;
                        stuck_iters = 0;
                    }
                    /* ~120ms with no audio progress → present anyway */
                    if (stuck_iters >= 8)
                        break;
                }

                /* Badly late vs audio: drop queued non-keys, still show frame.
                 * When g_pp_pb is active, pp_clock handles late frame dropping
                 * at presentation time; dropping demux packets breaks decoder GOP integrity. */
                if (behind > 0.45 && !g_pp_pb.active)
                    prospero_video_queue_drain_nonkey(24);
            } else if (video_fps > 1.0 && !g_pp_pb.active) {
                /*
                 * Audio not running yet: pace by nominal frame interval so
                 * we don't race through the open before audio primes.
                 */
                static double s_last_present_host = -1.0;
                struct timespec ts;
                double now_s, target_s, frame_s;

                /* Reset host pacer on a fresh open (no first video PTS yet) */
                if (first_video_pts_seconds < 0.0 &&
                    audio_samples_played == 0)
                    s_last_present_host = -1.0;

                clock_gettime(CLOCK_MONOTONIC, &ts);
                now_s = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
                frame_s = 1.0 / video_fps;
                if (frame_s < 0.016)
                    frame_s = 0.016;
                if (frame_s > 0.050)
                    frame_s = 0.050;
                if (s_last_present_host > 0.0) {
                    target_s = s_last_present_host + frame_s;
                    if (now_s < target_s) {
                        int sleep_us = (int)((target_s - now_s) * 1000000.0);
                        if (sleep_us > 40000)
                            sleep_us = 40000;
                        if (sleep_us > 500)
                            usleep(sleep_us);
                    }
                }
                clock_gettime(CLOCK_MONOTONIC, &ts);
                s_last_present_host =
                    (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
            }


            /* jobs 3+4 — present. Always show something (skip = frozen). */
            if (recv_ret == 1 && g_pp_pb.active)
                present_pp_frame(&pf);
            else
                convert_frame_via_sws((AVFrame *)eng_vdec_ffmpeg_avframe(g_vdec));

            /*
             * One line per seek, the moment the discard window closes: what
             * the settle cost and how far apart the two clocks are when the
             * picture comes back. A healthy seek reads vrel ~= arel; a large
             * arel - vrel gap is audio replaying the run-up to the target.
             */
            {
                static int s_was_discarding = 0;
                int now_discarding = g_pp_pb.active && g_pp_pb.seek_discarding;
                if (s_was_discarding && !now_discarding) {
                    pp_playback_stats st;
                    char d[128];
                    pp_playback_get_stats(&g_pp_pb, &st);
                    snprintf(d, sizeof d,
                             "ms=%llu disc=%llu pts=%.3f vrel=%.3f arel=%.3f aq=%d",
                             (unsigned long long)st.seek_to_first_frame_ms,
                             (unsigned long long)st.frames_discarded_seek,
                             (double)pf.pts_us / 1000000.0,
                             video_rel, (double)audio_clock_seconds,
                             audio_queue_count);
                    pp_stage_bc("SEEK_SETTLE", d);
                }
                s_was_discarding = now_discarding;
            }
            /* eng_vdec_receive() unrefs its scratch frame on the next call. */
            return 1;
        }

        if (!video_video_pending_pkt) {
            video_video_pending_pkt = packet_queue_pop(&video_packet_queue);
        }

        if (!video_video_pending_pkt) {
            /*
             * End of stream: flush the decoder once so buffered pictures still
             * come out (the native backend holds a small PTS-reorder window;
             * FFmpeg buffers frame-threaded latency). Send a NULL AU, loop back
             * so eng_vdec_receive() drains the tail, then stop. s_eof_drained
             * re-arms as soon as the stream is no longer at EOF (seek/replay).
             */
            static int s_eof_drained = 0;
            if (!video_decode_done) {
                s_eof_drained = 0;
                usleep(100);
                return 1;
            }
            if (!s_eof_drained) {
                s_eof_drained = 1;
                eng_vdec_send(g_vdec, NULL, 0, INT64_MIN);
                continue;
            }
            return 0;
        }

        int64_t send_pts_us = INT64_MIN;
        if (video_video_pending_pkt->pts != AV_NOPTS_VALUE && play_fmt &&
            video_stream_index >= 0)
            send_pts_us = av_rescale_q(
                video_video_pending_pkt->pts,
                play_fmt->streams[video_stream_index]->time_base,
                (AVRational){ 1, 1000000 });

#ifdef APP_VR
        /* Half rate, or badly late: let the decoder spend its time on the
         * pictures the rest of the stream depends on. */
        /* Hardware decoder only: a software multiview stream carries its two
         * views per access unit, and skipping one would split a pair. */
        vr_file_state(play_fmt, video_stream_index);
        if ((s_half_rate || s_video_late_s > 2.0 * VR_LATE_S) && play_fmt &&
            video_stream_index >= 0 && eng_vdec_active(g_vdec) == ENG_VDEC_BACKEND_NATIVE &&
            !(g_pp_pb.active && g_pp_pb.seek_discarding) &&
            au_droppable(video_video_pending_pkt,
                         play_fmt->streams[video_stream_index]->codecpar)) {
            if (s_late_nonref_drops++ % 300 == 0)
                eng_boot_log("vr: video late %.3f s, skipped %u non-reference pictures",
                             s_video_late_s, s_late_nonref_drops);
            av_packet_free(&video_video_pending_pkt);
            continue;
        }
#endif
        int send_ret = eng_vdec_send(g_vdec, video_video_pending_pkt->data,
                                     video_video_pending_pkt->size, send_pts_us);

        if (send_ret == 0) {
            av_packet_free(&video_video_pending_pkt);
            continue;
        }

        if (send_ret > 0) {   /* EAGAIN: keep the packet pending, drain first */
            usleep(50);
            continue;
        }

        /* send_ret < 0 — fatal for this packet. */
        note_vdec_result(1);
        if (g_pb_decode_fatal)
            return 0;
        av_packet_free(&video_video_pending_pkt);
    }

    return 1;
}


void *video_decode_thread_func(void *arg) {
    long long next_ms = 0;
    double frame_ms = 33.333;

    while (video_thread_running) {
        if (
            player_paused ||
            video_decode_hold ||
            pb_prebuffer_hold ||
            pb_scrub_hold ||
            screen != SCREEN_PLAYER ||
            !video_decode_ready
        ) {
            video_decode_parked = 1;
            /* Parked: paused, off the player screen, or not ready. 1 ms was
             * 1000 pointless wakeups a second; 5 ms is still well inside a
             * frame, so nothing notices on resume. */
            usleep(5000);
            next_ms = 0;
            continue;
        }

        video_decode_parked = 0;
        dbg_video_thread_alive++;

        /* Audio-master wait is inside decode_next_video_frame. */
        decode_next_video_frame();
        usleep(playback_profile >= 2 ? 100 : 200);
    }

    return NULL;
}


/* ---- §4 façade ---- */
int eng_pb_is_active(void)          { return video_decode_ready; }
int eng_pb_is_paused(void)          { return player_paused; }
int eng_pb_is_eof(void)            { return video_decode_done; }
double eng_pb_position_s(void)      { return prospero_media_clock_seconds(); }
double eng_pb_duration_s(void)      { return media_duration_sec; }
double eng_pb_audio_clock_s(void)   { return (double)audio_clock_seconds; }
double eng_pb_video_clock_s(void)   { return video_clock_seconds; }
double eng_pb_video_fps(void)       { return video_fps; }
int eng_pb_active_backend(void)     { return (int)eng_vdec_active(g_vdec); }

void eng_pb_queue_depth(int *vpkts, int *apkts, int *ablocks)
{
    if (vpkts)  *vpkts  = packet_queue_count(&video_packet_queue);
    if (apkts)  *apkts  = packet_queue_count(&audio_packet_queue);
    if (ablocks) *ablocks = audio_queue_count;
}
