/*
 * eng_vdec_ffmpeg.c — the FFmpeg implementation of eng_vdec.h.
 *
 * A near-verbatim lift of the inline avcodec_* video path out of main.c
 * (Track A step A6, docs/modularisation-plan.md §5). It owns the
 * AVCodecContext and the receive AVFrame, and adapts decoded frames into the
 * FFmpeg-free pp_frame. PURE: no clocks, no sleeps, no pp_playback, no
 * globals — pacing and present stay in the play loop.
 *
 * pp_map_avframe adapts a decoded AVFrame into a pp_frame. GL-5 (#81) removed
 * the old pp_map_yuv420p10_to_8 CPU pack — 10-bit planar goes through as
 * PP_FRAME_YUV420P10 and the GL shader samples it as GL_R16.
 */
#include "eng_vdec.h"
#include "eng_vdec_native.h"   /* sceVideodec2 backend (stubs off the app module) */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#ifdef APP_APP
#include "dv_rpu.h"
#endif
/* PS5VR: MV-HEVC ("spatial video" from iPhone 15 Pro / 16 and Apple Vision
 * Pro). The hardware decoder only knows the base view, so these go to FFmpeg
 * 7.1's decoder with both views on, and each pair is joined side by side. */
#if defined(APP_VR) && LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 19, 100)
#define ENG_VDEC_MULTIVIEW 1
#include <libavutil/cpu.h>
#include <libavutil/pixdesc.h>
#include <libavutil/stereo3d.h>
int eng_vdec_multiview_active;
/* H.264 MVC (3D Blu-ray): FFmpeg decodes the base view only, so a stream whose
 * packets carry MVC NAL units (14 prefix, 15 subset SPS, 20 slice extension)
 * goes to edge264 (third_party/edge264, BSD), which decodes both views. The
 * PS5 libc has no ENODATA; the Makefile builds edge264 with the same value. */
#ifndef ENODATA
#define ENODATA 9919
#endif
#include "edge264/edge264.h"
#define MVC_OUT_RING  12          /* joined frames; the engine borrows a few */
#define MVC_PTS_MAX   64
#endif

#ifndef FF_PROFILE_UNKNOWN
#define FF_PROFILE_UNKNOWN (-99)
#endif

/*
 * This file owns the public eng_vdec.h surface and dispatches: a NATIVE
 * request that opens goes to eng_vdec_native.c, everything else (and every
 * native failure) is the FFmpeg path below. `nat` is non-NULL iff
 * backend == ENG_VDEC_BACKEND_NATIVE, and then the ctx/frame/pkt fields are
 * unused.
 */
#define FFMPEG_FRAME_RING 4
struct eng_vdec {
    eng_vdec_backend backend;
    int              codec_id;   /* AVCodecID — backend-independent, for the UI */
    AVCodecContext  *ctx;
    AVFrame         *frames[FFMPEG_FRAME_RING]; /* ring for receive; planes borrowed until slot recycled */
    int              frame_idx;
    AVPacket        *pkt;     /* scratch for send */
    eng_vdec_native *nat;     /* sceVideodec2 sub-backend, or NULL */

    /* #8 instrumentation. `pending_us` accumulates the cost of the send() calls
     * and the empty receive() polls since the last frame came out, so the cost
     * of a frame includes the work that produced it rather than only the call
     * that collected it — a decoder with a 4-deep pipeline otherwise reports
     * near-zero per-frame times. */
    eng_vdec_stats stats;
    uint64_t       pending_us;
#ifdef ENG_VDEC_MULTIVIEW
    int            mv;                    /* two views -> one side-by-side frame */
    AVFrame       *mv_left;               /* view 0, waiting for its view 1 */
    AVFrame       *mv_out[FFMPEG_FRAME_RING];
    int            mv_idx;
    /* MVC through edge264 */
    int             mvc_scan;              /* H.264: packets still being checked */
    int             nal_len;               /* AVCC NAL length size, 0 = Annex B */
    Edge264Decoder *e264;
    AVFrame        *mvc_out[MVC_OUT_RING]; /* joined frames, oldest overwritten */
    int             mvc_w;                 /* next slot to fill */
    int             mvc_r, mvc_n;          /* ready frames: first slot, count */
    int64_t         mvc_pts[MVC_PTS_MAX];  /* pending input pts, ascending */
    int             mvc_npts;
    const uint8_t  *mvc_resume_pkt;        /* a packet cut short by ENOBUFS... */
    int             mvc_resume_size, mvc_resume_off;   /* ...and where to go on */
    const AVCodecParameters *par;
#endif
};

#ifdef ENG_VDEC_MULTIVIEW
/* Apple's MV-HEVC carries a 'vexu' box, exported as an UNSPEC stereo3d. */
static int is_multiview_hevc(const AVCodecParameters *par)
{
    if (!par || par->codec_id != AV_CODEC_ID_HEVC)
        return 0;
    const AVPacketSideData *sd = av_packet_side_data_get(par->coded_side_data,
                                                         par->nb_coded_side_data,
                                                         AV_PKT_DATA_STEREO3D);
    return sd && sd->size >= sizeof(AVStereo3D) &&
           ((const AVStereo3D *)sd->data)->type == AV_STEREO3D_UNSPEC;
}

/* Join view 0 (left) and view 1 into one frame twice as wide. */
static AVFrame *mv_join(eng_vdec *v, const AVFrame *l, const AVFrame *r)
{
    const AVPixFmtDescriptor *d = av_pix_fmt_desc_get((enum AVPixelFormat)r->format);
    if (!d || (d->flags & AV_PIX_FMT_FLAG_PLANAR) == 0 || l->width != r->width ||
        l->height != r->height || l->format != r->format)
        return NULL;
    v->mv_idx = (v->mv_idx + 1) % FFMPEG_FRAME_RING;
    AVFrame *o = v->mv_out[v->mv_idx];
    av_frame_unref(o);
    o->format = r->format;
    o->width = r->width * 2;
    o->height = r->height;
    if (av_frame_get_buffer(o, 64) < 0)
        return NULL;
    av_frame_copy_props(o, l);
    const int bps = (d->comp[0].depth + 7) / 8;
    for (int p = 0; p < 4 && l->data[p]; p++) {
        const int sub_w = (p == 1 || p == 2) ? d->log2_chroma_w : 0;
        const int sub_h = (p == 1 || p == 2) ? d->log2_chroma_h : 0;
        const int row = (r->width >> sub_w) * bps, rows = r->height >> sub_h;
        for (int y = 0; y < rows; y++) {
            memcpy(o->data[p] + (size_t)y * o->linesize[p], l->data[p] + (size_t)y * l->linesize[p], row);
            memcpy(o->data[p] + (size_t)y * o->linesize[p] + row,
                   r->data[p] + (size_t)y * r->linesize[p], row);
        }
    }
    return o;
}

/* Walk the NAL units of a packet: AVCC (length-prefixed) or Annex B. Calls
 * fn(nal, end, arg) for each; stops early when fn returns non-zero (its value is
 * returned, with *stop_off = the offset of that NAL's prefix). */
static int for_each_nal(const uint8_t *d, int size, int nal_len,
                        int (*fn)(const uint8_t *, const uint8_t *, void *), void *arg,
                        int start_off, int *stop_off)
{
    const uint8_t *end = d + size, *p = d + start_off;
    if (nal_len) {
        while (p + nal_len <= end) {
            uint32_t n = 0;
            for (int i = 0; i < nal_len; i++)
                n = (n << 8) | p[i];
            const uint8_t *nal = p + nal_len, *ne = nal + n;
            if (n == 0 || ne > end)
                break;
            int r = fn(nal, ne, arg);
            if (r) {
                if (stop_off) *stop_off = (int)(p - d);
                return r;
            }
            p = ne;
        }
        return 0;
    }
    const uint8_t *nal = edge264_find_start_code(p, end, 0);
    while (nal < end) {
        const uint8_t *body = nal + 3;
        const uint8_t *next = edge264_find_start_code(body, end, 0);
        const uint8_t *ne = next;
        while (ne > body && ne[-1] == 0)          /* a 4-byte start code's zero */
            ne--;
        if (ne > body) {
            int r = fn(body, ne, arg);
            if (r) {
                if (stop_off) *stop_off = (int)(nal - d);
                return r;
            }
        }
        nal = next;
    }
    return 0;
}

/* bit 0: an MVC NAL unit (14/15/20), bit 1: an IDR slice (5) */
static int scan_nal(const uint8_t *nal, const uint8_t *end, void *arg)
{
    int *flags = arg;
    const int t = nal[0] & 31;
    if (t == 14 || t == 15 || t == 20)
        *flags |= 1;
    if (t == 5)
        *flags |= 2;
    return 0;
}

static void mvc_pts_push(eng_vdec *v, int64_t pts)
{
    if (pts == INT64_MIN)
        return;
    if (v->mvc_npts == MVC_PTS_MAX) {                 /* never emptied: drop oldest */
        memmove(v->mvc_pts, v->mvc_pts + 1, (MVC_PTS_MAX - 1) * sizeof(int64_t));
        v->mvc_npts--;
    }
    int i = v->mvc_npts++;
    while (i > 0 && v->mvc_pts[i - 1] > pts) {
        v->mvc_pts[i] = v->mvc_pts[i - 1];
        i--;
    }
    v->mvc_pts[i] = pts;
}

/* edge264 outputs in display order, so a picture's pts is the smallest pending. */
static int64_t mvc_pts_pop(eng_vdec *v)
{
    if (!v->mvc_npts)
        return INT64_MIN;
    int64_t p = v->mvc_pts[0];
    memmove(v->mvc_pts, v->mvc_pts + 1, (size_t)(--v->mvc_npts) * sizeof(int64_t));
    return p;
}

/* Copy every finished picture out of edge264 as one side-by-side frame. Stops
 * when the ring holds frames the engine has not taken yet. */
static void mvc_collect(eng_vdec *v)
{
    Edge264Frame f;
    while (v->mvc_n < MVC_OUT_RING - 4 && edge264_get_frame(v->e264, &f, 0) == 0) {
        const int w = f.width_Y, h = f.height_Y, cw = f.width_C, ch = f.height_C;
        AVFrame *o = v->mvc_out[v->mvc_w];
        av_frame_unref(o);
        o->format = AV_PIX_FMT_YUV420P;
        o->width = 2 * w;
        o->height = h;
        if (w <= 0 || h <= 0 || av_frame_get_buffer(o, 64) < 0)
            continue;
        for (int pl = 0; pl < 3; pl++) {
            const int pw = pl ? cw : w, ph = pl ? ch : h;
            const int ss = pl ? f.stride_C : f.stride_Y;
            const uint8_t *l = f.samples[pl];
            const uint8_t *r = f.samples_mvc[pl] ? f.samples_mvc[pl] : f.samples[pl];
            for (int y = 0; y < ph; y++) {
                memcpy(o->data[pl] + (size_t)y * o->linesize[pl], l + (size_t)y * ss, pw);
                memcpy(o->data[pl] + (size_t)y * o->linesize[pl] + pw, r + (size_t)y * ss, pw);
            }
        }
        o->pts = o->best_effort_timestamp = mvc_pts_pop(v);
        v->mvc_w = (v->mvc_w + 1) % MVC_OUT_RING;
        v->mvc_n++;
    }
}

static int mvc_feed_nal(const uint8_t *nal, const uint8_t *end, void *arg)
{
    eng_vdec *v = arg;
    for (int tries = 0; tries < 2; tries++) {
        int r = edge264_decode_NAL(v->e264, nal, end, NULL, NULL);
        mvc_collect(v);
        if (r != ENOBUFS)
            return 0;                        /* errors in one NAL do not stop the stream */
    }
    return 1;                                /* output full: the engine must take frames */
}

/* Feed the SPS/PPS of an avcC header. */
static void mvc_feed_avcc(eng_vdec *v, const uint8_t *x, int n)
{
    if (!x || n < 7 || x[0] != 1)
        return;
    int i = 6, sps = x[5] & 31;
    for (int k = 0; k < sps && i + 2 <= n; k++) {
        int len = (x[i] << 8) | x[i + 1];
        if (i + 2 + len > n) return;
        mvc_feed_nal(x + i + 2, x + i + 2 + len, v);
        i += 2 + len;
    }
    if (i >= n) return;
    int pps = x[i++];
    for (int k = 0; k < pps && i + 2 <= n; k++) {
        int len = (x[i] << 8) | x[i + 1];
        if (i + 2 + len > n) return;
        mvc_feed_nal(x + i + 2, x + i + 2 + len, v);
        i += 2 + len;
    }
}

static void mvc_arm(eng_vdec *v, const eng_vdec_open_params *p)
{
    v->par = (const AVCodecParameters *)p->avctx_params;
    if (p->codec_id == AV_CODEC_ID_H264) {
        v->mvc_scan = 300;                   /* packets to look at */
        v->nal_len = (v->par && v->par->extradata_size > 4 && v->par->extradata[0] == 1)
                         ? (v->par->extradata[4] & 3) + 1 : 0;
    }
}

/* The hardware decoder took the stream; MVC turned up: move to edge264. */
static int mvc_from_native(eng_vdec *v)
{
    const AVCodec *dec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!dec || !(v->ctx = avcodec_alloc_context3(dec)))
        return -1;
    if (v->par)
        avcodec_parameters_to_context(v->ctx, v->par);
    for (int i = 0; i < FFMPEG_FRAME_RING; i++)
        if (!v->frames[i] && !(v->frames[i] = av_frame_alloc()))
            return -1;
    if (!v->pkt && !(v->pkt = av_packet_alloc()))
        return -1;
    eng_vdec_native_close(v->nat);
    v->nat = NULL;
    v->backend = ENG_VDEC_BACKEND_FFMPEG;
    return 0;
}

static int mvc_start(eng_vdec *v)
{
    v->e264 = edge264_alloc(6, NULL, NULL, 0, NULL, NULL, NULL);
    if (!v->e264)
        return -1;
    for (int i = 0; i < MVC_OUT_RING; i++)
        if (!(v->mvc_out[i] = av_frame_alloc()))
            return -1;
    if (v->par)
        mvc_feed_avcc(v, v->par->extradata, v->par->extradata_size);
    if (avcodec_is_open(v->ctx))
        avcodec_flush_buffers(v->ctx);      /* nothing more goes to FFmpeg */
    eng_vdec_multiview_active = 1;
    return 0;
}

/* The MVC path of send: 0 consumed, 1 = take frames then send it again. */
static int mvc_send(eng_vdec *v, const uint8_t *data, int size, int64_t pts_us)
{
    if (!data || size <= 0) {                /* end of stream: bump every picture */
        static const uint8_t eos[1];
        edge264_decode_NAL(v->e264, eos + 1, eos + 1, NULL, NULL);
        mvc_collect(v);
        return 0;
    }
    int off = 0;
    if (v->mvc_resume_pkt == data && v->mvc_resume_size == size) {
        off = v->mvc_resume_off;             /* the rest of a packet cut short */
    } else {
        mvc_pts_push(v, pts_us);
    }
    int stop = 0;
    if (for_each_nal(data, size, v->nal_len, mvc_feed_nal, v, off, &stop)) {
        v->mvc_resume_pkt = data;
        v->mvc_resume_size = size;
        v->mvc_resume_off = stop;
        return 1;
    }
    v->mvc_resume_pkt = NULL;
    return 0;
}
#endif

/* ---------------------------------------------------------------------------
 * #8 — decode timing at the seam
 * ------------------------------------------------------------------------ */

static int vdec_send_inner(eng_vdec *v, const uint8_t *data, int size, int64_t pts_us);
static int vdec_receive_inner(eng_vdec *v, pp_frame *out);

static eng_vdec_open_result g_last_open_result = ENG_VDEC_OPEN_OK;

static uint64_t vdec_now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)(ts.tv_nsec / 1000);
}

/* Charge `us` to the decoder. `produced` marks the call that yielded a frame:
 * the accumulated pending cost is flushed into the per-frame ring with it. */
static void vdec_note(eng_vdec *v, uint64_t us, int produced)
{
    v->stats.decode_us_total += us;
    v->pending_us += us;
    if (!produced)
        return;
    uint64_t frame_us = v->pending_us;
    v->pending_us = 0;
    if (frame_us > v->stats.decode_us_max)
        v->stats.decode_us_max = frame_us;
    /* Rolling ring of the last 256 frames, same shape as pp_playback's. */
    uint32_t i;
    if (v->stats.decode_ring_count < 256u) {
        i = v->stats.decode_ring_count;
        v->stats.decode_ring_count++;
    } else {
        i = (uint32_t)(v->stats.frames_out % 256u);
    }
    v->stats.decode_ring[i] = frame_us;
}

eng_vdec_open_result eng_vdec_last_open_result(void)
{
    return g_last_open_result;
}

const char *eng_vdec_open_result_name(eng_vdec_open_result r)
{
    switch (r) {
    case ENG_VDEC_OPEN_OK:         return "ok";
    case ENG_VDEC_OPEN_DOWNGRADED: return "downgraded";
    case ENG_VDEC_OPEN_NO_DECODER: return "no_decoder";
    case ENG_VDEC_OPEN_CTX_FAIL:   return "ctx_fail";
    case ENG_VDEC_OPEN_BAD_ARGS:   return "bad_args";
    }
    return "?";
}

void eng_vdec_get_stats(const eng_vdec *v, eng_vdec_stats *out)
{
    if (!out)
        return;
    if (!v) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = v->stats;
    out->backend  = v->backend;
    out->codec_id = v->codec_id;
}

uint64_t eng_vdec_decode_p95_us(const eng_vdec *v)
{
    uint64_t tmp[256];
    uint32_t n, i, j, idx;
    if (!v)
        return 0;
    n = v->stats.decode_ring_count;
    if (n == 0)
        return 0;
    if (n > 256u)
        n = 256u;
    memcpy(tmp, v->stats.decode_ring, n * sizeof(uint64_t));
    for (i = 1; i < n; i++) {          /* insertion sort, n <= 256 */
        uint64_t val = tmp[i];
        j = i;
        while (j > 0 && tmp[j - 1] > val) {
            tmp[j] = tmp[j - 1];
            j--;
        }
        tmp[j] = val;
    }
    idx = (n * 95u) / 100u;
    if (idx >= n)
        idx = n - 1;
    return tmp[idx];
}

int eng_vdec_probe(void)
{
    return eng_vdec_native_probe();
}

void eng_vdec_prefer_nv12(int on)
{
    eng_vdec_native_prefer_nv12(on);
}

eng_vdec_backend eng_vdec_pref_resolve(eng_vdec_pref pref, int codec_id)
{
    if (pref == ENG_VDEC_PREF_FFMPEG)
        return ENG_VDEC_BACKEND_FFMPEG;
    if (!eng_vdec_probe())
        return ENG_VDEC_BACKEND_FFMPEG;   /* NATIVE or AUTO, but nothing to open */
    /* AUTO only asks for native on a codec whose resident decoder actually came
     * up this session (H.264 / HEVC Main / VP9 Profile 0) — an explicit NATIVE
     * request is still passed through unsupported, since eng_vdec_open()
     * downgrades that itself. Profile/bit-depth/dimension gating happens in
     * eng_vdec_native_open(); here we only need the codec-level check. */
    if (pref == ENG_VDEC_PREF_AUTO &&
        !eng_vdec_native_supports(codec_id, FF_PROFILE_UNKNOWN, 8, 0, 0))
        return ENG_VDEC_BACKEND_FFMPEG;
    return ENG_VDEC_BACKEND_NATIVE;
}

int eng_vdec_native_can_open(int codec_id, int profile, int bit_depth,
                             int w, int h)
{
    if (!eng_vdec_probe())
        return 0;
    return eng_vdec_native_supports(codec_id, profile, bit_depth, w, h);
}

static int pp_map_avframe(const AVFrame *frame, pp_frame *out, int64_t pts_us)
{
    if (!frame || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->width = (uint32_t)frame->width;
    out->height = (uint32_t)frame->height;
    out->pts_us = pts_us;
    out->color_trc = (int)frame->color_trc;
    if (frame->format == AV_PIX_FMT_YUV420P || frame->format == AV_PIX_FMT_YUVJ420P) {
        out->format = PP_FRAME_YUV420P;
        out->planes[0] = frame->data[0];
        out->planes[1] = frame->data[1];
        out->planes[2] = frame->data[2];
        out->strides[0] = frame->linesize[0];
        out->strides[1] = frame->linesize[1];
        out->strides[2] = frame->linesize[2];
        return 0;
    }
    if (frame->format == AV_PIX_FMT_NV12) {
        out->format = PP_FRAME_NV12;
        out->planes[0] = frame->data[0];
        out->planes[1] = frame->data[1];
        out->strides[0] = frame->linesize[0];
        out->strides[1] = frame->linesize[1];
        return 0;
    }
    /* GL-5 (#81): 10-bit planar 4:2:0 goes straight through as a 16-bit frame -
     * the GL video shader samples it as GL_R16. No CPU pack. Little-endian only
     * (PS5 is LE, and yuv420p10be effectively never occurs in real files) - a BE
     * frame falls through to the swscale path like any other exotic format. */
    if (frame->format == AV_PIX_FMT_YUV420P10LE) {
        out->format = PP_FRAME_YUV420P10;
        out->planes[0] = frame->data[0];
        out->planes[1] = frame->data[1];
        out->planes[2] = frame->data[2];
        out->strides[0] = frame->linesize[0];   /* bytes (= 2 * samples) */
        out->strides[1] = frame->linesize[1];
        out->strides[2] = frame->linesize[2];
        return 0;
    }
    return -2;
}

/* ---------------------------------------------------------------------------
 * eng_vdec interface
 * ------------------------------------------------------------------------ */

static void ffmpeg_apply_tuning(AVCodecContext *ctx, const eng_vdec_open_params *p)
{
    if (p->thread_count > 0 && p->thread_count != ENG_VDEC_KEEP)
        ctx->thread_count = p->thread_count;
    if (p->thread_type != ENG_VDEC_KEEP)
        ctx->thread_type = p->thread_type;
#ifdef AV_CODEC_FLAG2_FAST
    if (p->flag2_fast)
        ctx->flags2 |= AV_CODEC_FLAG2_FAST;
#endif
    if (p->skip_loop_filter != ENG_VDEC_KEEP)
        ctx->skip_loop_filter = (enum AVDiscard)p->skip_loop_filter;
    if (p->skip_frame != ENG_VDEC_KEEP)
        ctx->skip_frame = (enum AVDiscard)p->skip_frame;
    if (p->skip_idct != ENG_VDEC_KEEP)
        ctx->skip_idct = (enum AVDiscard)p->skip_idct;
}

eng_vdec *eng_vdec_open(const eng_vdec_open_params *p, eng_vdec_backend *chosen)
{
    if (chosen)
        *chosen = ENG_VDEC_BACKEND_FFMPEG;
    g_last_open_result = ENG_VDEC_OPEN_BAD_ARGS;
    if (!p)
        return NULL;
    int wanted_native = (p->backend == ENG_VDEC_BACKEND_NATIVE);
    int try_native = wanted_native;
#ifdef ENG_VDEC_MULTIVIEW
    const int mv = is_multiview_hevc((const AVCodecParameters *)p->avctx_params);
    eng_vdec_multiview_active = 0;
    if (mv)
        try_native = 0;            /* the hardware decodes the base view only */
#endif

    /* Native first, if asked. Any failure falls through to FFmpeg — the seam
     * contract is "never NULL when FFmpeg could have opened". */
    if (try_native) {
        eng_vdec_native *nat = eng_vdec_native_open(p);
        if (nat) {
            eng_vdec *v = (eng_vdec *)calloc(1, sizeof(*v));
            if (v) {
                v->backend  = ENG_VDEC_BACKEND_NATIVE;
                v->codec_id = p->codec_id;
                v->nat = nat;
#ifdef ENG_VDEC_MULTIVIEW
                mvc_arm(v, p);
#endif
                if (chosen)
                    *chosen = ENG_VDEC_BACKEND_NATIVE;
                g_last_open_result = ENG_VDEC_OPEN_OK;
                return v;
            }
            eng_vdec_native_close(nat);
        }
    }

    /* Past this point any failure is FFmpeg's, so the sweep can separate "no
     * decoder exists" from "the decoder exists but would not come up". */
    g_last_open_result = ENG_VDEC_OPEN_CTX_FAIL;

    const AVCodec *dec = avcodec_find_decoder((enum AVCodecID)p->codec_id);
    if (!dec) {
        g_last_open_result = ENG_VDEC_OPEN_NO_DECODER;
        return NULL;
    }

    eng_vdec *v = (eng_vdec *)calloc(1, sizeof(*v));
    if (!v)
        return NULL;
    v->backend  = ENG_VDEC_BACKEND_FFMPEG;
    v->codec_id = p->codec_id;

    v->ctx = avcodec_alloc_context3(dec);
    for (int i = 0; i < FFMPEG_FRAME_RING; i++) {
        v->frames[i] = av_frame_alloc();
    }
    v->pkt = av_packet_alloc();
    if (!v->ctx || !v->pkt || !v->frames[0] || !v->frames[1] || !v->frames[2] || !v->frames[3]) {
        eng_vdec_close(v);
        return NULL;
    }

    if (p->avctx_params &&
        avcodec_parameters_to_context(v->ctx,
                                      (const AVCodecParameters *)p->avctx_params) < 0) {
        eng_vdec_close(v);
        return NULL;
    }

    ffmpeg_apply_tuning(v->ctx, p);

    /* Feed the decoder timestamps in microseconds so best_effort_timestamp
     * comes straight back as pts_us — the caller rescales stream PTS -> us. */
    v->ctx->pkt_timebase = (AVRational){ 1, 1000000 };

    AVDictionary *opts = NULL;
#ifdef ENG_VDEC_MULTIVIEW
    if (mv) {
        av_dict_set(&opts, "view_ids", "-1", 0);      /* every view */
        /* Two views per frame: as many threads as 4K software decode gets. */
        const int cpus = av_cpu_count();
        v->ctx->thread_count = cpus < 8 ? 8 : (cpus > 12 ? 12 : cpus);
        v->ctx->thread_type = FF_THREAD_FRAME;
        v->mv = 1;
        v->mv_left = av_frame_alloc();
        for (int i = 0; i < FFMPEG_FRAME_RING; i++)
            v->mv_out[i] = av_frame_alloc();
    }
#endif
    if (avcodec_open2(v->ctx, dec, &opts) < 0) {
        av_dict_free(&opts);
        eng_vdec_close(v);
        return NULL;
    }
    av_dict_free(&opts);
#ifdef ENG_VDEC_MULTIVIEW
    eng_vdec_multiview_active = v->mv;
    mvc_arm(v, p);
#endif
    g_last_open_result = wanted_native ? ENG_VDEC_OPEN_DOWNGRADED
                                       : ENG_VDEC_OPEN_OK;
    return v;
}

int eng_vdec_send(eng_vdec *v, const uint8_t *data, int size, int64_t pts_us)
{
    if (!v)
        return -1;
    uint64_t t0 = vdec_now_us();
    int r = vdec_send_inner(v, data, size, pts_us);
    vdec_note(v, vdec_now_us() - t0, 0);
    v->stats.send_calls++;
    if (r > 0)
        v->stats.send_stalls++;
    else if (r < 0)
        v->stats.fatal_errors++;
    return r;
}

static int vdec_send_inner(eng_vdec *v, const uint8_t *data, int size, int64_t pts_us)
{
#ifdef ENG_VDEC_MULTIVIEW
    if (v->e264)
        return mvc_send(v, data, size, pts_us);
    if (v->mvc_scan > 0 && data && size > 0) {
        int flags = 0;
        for_each_nal(data, size, v->nal_len, scan_nal, &flags, 0, NULL);
        v->mvc_scan--;
        if ((flags & 3) == 3) {              /* an MVC access unit with an IDR: switch */
            v->mvc_scan = 0;
            if ((v->backend != ENG_VDEC_BACKEND_NATIVE || mvc_from_native(v) == 0) &&
                mvc_start(v) == 0)
                return mvc_send(v, data, size, pts_us);
            if (v->e264)
                edge264_free(&v->e264);
            eng_vdec_multiview_active = 0;
        }
    }
#endif
    if (v->backend == ENG_VDEC_BACKEND_NATIVE)
        return eng_vdec_native_send(v->nat, data, size, pts_us);
    if (!v->ctx)
        return -1;
    av_packet_unref(v->pkt);

    int flush = !(data && size > 0);
    if (!flush) {
        /* Borrow the caller's buffer (alive for this call); avcodec_send_packet
         * copies into the decoder's own storage. dts left unset — packets are in
         * decode order and B-frame pts is non-monotonic; the decoder reorders to
         * display order and fills best_effort_timestamp from the pts we give it
         * (microseconds, matching ctx->pkt_timebase). */
        v->pkt->data = (uint8_t *)data;
        v->pkt->size = size;
        v->pkt->pts  = (pts_us == INT64_MIN) ? AV_NOPTS_VALUE : pts_us;
        v->pkt->dts  = AV_NOPTS_VALUE;
    }

    int ret = avcodec_send_packet(v->ctx, flush ? NULL : v->pkt);

    /* Drop the borrowed pointer before the next unref touches the packet. */
    v->pkt->data = NULL;
    v->pkt->size = 0;

    if (ret == 0)
        return 0;
    if (ret == AVERROR(EAGAIN))
        return 1;   /* drain receive first, packet not consumed */
    return -1;
}

int eng_vdec_receive(eng_vdec *v, pp_frame *out)
{
    if (!v || !out)
        return -1;
    uint64_t t0 = vdec_now_us();
    int r = vdec_receive_inner(v, out);
    vdec_note(v, vdec_now_us() - t0, r > 0);
    if (r > 0) {
        v->stats.frames_out++;
        if (r == 2)
            v->stats.frames_sw_mapped++;
    } else if (r < 0) {
        v->stats.fatal_errors++;
    }
    return r;
}

static int vdec_receive_inner(eng_vdec *v, pp_frame *out)
{
    if (v->backend == ENG_VDEC_BACKEND_NATIVE)
        return eng_vdec_native_receive(v->nat, out);   /* 1 / 0 / <0, never 2 */
    if (!v->ctx)
        return -1;
#ifdef ENG_VDEC_MULTIVIEW
    if (v->e264) {
        mvc_collect(v);
        if (!v->mvc_n)
            return 0;
        AVFrame *f = v->mvc_out[v->mvc_r];
        v->mvc_r = (v->mvc_r + 1) % MVC_OUT_RING;
        v->mvc_n--;
        return pp_map_avframe(f, out, f->pts) == 0 ? 1 : 2;
    }
#endif

    int next_idx = (v->frame_idx + 1) % FFMPEG_FRAME_RING;
    av_frame_unref(v->frames[next_idx]);

    int ret = avcodec_receive_frame(v->ctx, v->frames[next_idx]);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        return 0;   /* EOF: drained after a NULL send — not a decode failure */
    if (ret < 0)
        return -1;

    v->frame_idx = next_idx;
    AVFrame *frame = v->frames[next_idx];
#ifdef ENG_VDEC_MULTIVIEW
    if (v->mv) {
        const AVFrameSideData *vid = av_frame_get_side_data(frame, AV_FRAME_DATA_VIEW_ID);
        const int view = (vid && vid->size >= sizeof(int)) ? *(const int *)vid->data : 0;
        if (view == 0) {                      /* hold it for its view 1 */
            av_frame_unref(v->mv_left);
            av_frame_ref(v->mv_left, frame);
            return vdec_receive_inner(v, out);
        }
        if (!v->mv_left->buf[0])
            return 0;                         /* view 1 without its left: skip */
        AVFrame *joined = mv_join(v, v->mv_left, frame);
        av_frame_unref(v->mv_left);
        if (!joined)
            return -1;
        frame = joined;
    }
#endif

    int64_t pts_us = INT64_MIN;
    if (frame->best_effort_timestamp != AV_NOPTS_VALUE)
        pts_us = frame->best_effort_timestamp;
    else if (frame->pts != AV_NOPTS_VALUE)
        pts_us = frame->pts;

#ifdef APP_APP
    if (dv_session_active()) {
        AVFrameSideData *sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
        dv_params dvp;
        if (sd && dv_from_avdovi(sd->data, &dvp) == 0)
            dv_store(pts_us, &dvp);
    }
#endif
    if (pp_map_avframe(frame, out, pts_us) == 0)
        return 1;

    /* Decoded, but an exotic pixel format — caller runs the swscale path. */
    return 2;
}

void eng_vdec_flush(eng_vdec *v)
{
    if (!v)
        return;
    v->pending_us = 0;   /* #8: don't charge pre-seek work to the next frame */
    if (v->backend == ENG_VDEC_BACKEND_NATIVE) {
        eng_vdec_native_flush(v->nat);
        return;
    }
    if (v->ctx)
        avcodec_flush_buffers(v->ctx);
    for (int i = 0; i < FFMPEG_FRAME_RING; i++) {
        if (v->frames[i])
            av_frame_unref(v->frames[i]);
    }
#ifdef ENG_VDEC_MULTIVIEW
    if (v->mv_left)
        av_frame_unref(v->mv_left);
    if (v->e264) {
        edge264_flush(v->e264);
        v->mvc_n = 0;
        v->mvc_r = v->mvc_w;
        v->mvc_npts = 0;
        v->mvc_resume_pkt = NULL;
    }
#endif
    if (v->pkt)
        av_packet_unref(v->pkt);
}

void eng_vdec_close(eng_vdec *v)
{
    if (!v)
        return;
    if (v->backend == ENG_VDEC_BACKEND_NATIVE) {
        eng_vdec_native_close(v->nat);
        free(v);
        return;
    }
    if (v->pkt)
        av_packet_free(&v->pkt);
    for (int i = 0; i < FFMPEG_FRAME_RING; i++) {
        if (v->frames[i])
            av_frame_free(&v->frames[i]);
    }
#ifdef ENG_VDEC_MULTIVIEW
    av_frame_free(&v->mv_left);
    for (int i = 0; i < FFMPEG_FRAME_RING; i++)
        av_frame_free(&v->mv_out[i]);
    if (v->e264) {
        edge264_free(&v->e264);
        eng_vdec_multiview_active = 0;
    }
    for (int i = 0; i < MVC_OUT_RING; i++)
        av_frame_free(&v->mvc_out[i]);
#endif
    if (v->ctx)
        avcodec_free_context(&v->ctx);
    free(v);
}

eng_vdec_backend eng_vdec_active(const eng_vdec *v)
{
    return v ? v->backend : ENG_VDEC_BACKEND_FFMPEG;
}

int eng_vdec_codec_id(const eng_vdec *v)
{
    return v ? v->codec_id : 0 /* AV_CODEC_ID_NONE */;
}

/* ---- FFmpeg-backend accessors ---- */
int eng_vdec_ffmpeg_width(const eng_vdec *v)
{
    return (v && v->ctx) ? v->ctx->width : 0;
}
int eng_vdec_ffmpeg_height(const eng_vdec *v)
{
    return (v && v->ctx) ? v->ctx->height : 0;
}
int eng_vdec_ffmpeg_color_trc(const eng_vdec *v)
{
    return (v && v->ctx) ? (int)v->ctx->color_trc : 0;
}
int eng_vdec_ffmpeg_pix_fmt(const eng_vdec *v)
{
    return (v && v->ctx) ? (int)v->ctx->pix_fmt : -1;
}
const char *eng_vdec_ffmpeg_codec_name(const eng_vdec *v)
{
    if (v && v->ctx && v->ctx->codec && v->ctx->codec->name)
        return v->ctx->codec->name;
    return "";
}
void *eng_vdec_ffmpeg_avframe(eng_vdec *v)
{
    return (v && v->backend == ENG_VDEC_BACKEND_FFMPEG) ? v->frames[v->frame_idx] : NULL;
}
